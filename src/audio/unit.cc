#include "unit.h"
#include "portaudio/pa_ringbuffer.h"
#include "node.h"
#include "node_buffer.h"

#include <speex/speex_resampler.h>
#include <speex/speex_echo.h>
#include <string.h> // memset

#include "platform/mac.h"

#define MIN(a, b) ((a) > (b) ? (b) : (a))

namespace vock {
namespace audio {

using node::Buffer;

HALUnit::HALUnit(double rate,
                 uv_async_t* in_cb,
                 uv_async_t* inready_cb,
                 uv_async_t* outready_cb)
    : in_unit_(PlatformUnit::kInputUnit, rate),
      out_unit_(PlatformUnit::kOutputUnit, rate),
      in_cb_(in_cb),
      inready_cb_(inready_cb),
      outready_cb_(outready_cb),
      inready_(false),
      outready_(false) {

  in_unit_.SetInputCallback(InputCallback, this);
  out_unit_.SetOutputCallback(OutputCallback, this);

  if (PaUtil_InitializeRingBuffer(&in_ring_,
                                  2,
                                  sizeof(in_ring_buf_) / 2,
                                  in_ring_buf_) == -1) {
    abort();
  }
  if (PaUtil_InitializeRingBuffer(&out_ring_,
                                  2,
                                  sizeof(out_ring_buf_) / 2,
                                  out_ring_buf_)== -1) {
    abort();
  }

  if (rate != in_unit_.GetInputSampleRate()) {
    int err;
    resampler_ = speex_resampler_init(1,
                                      in_unit_.GetInputSampleRate(),
                                      rate,
                                      SPEEX_RESAMPLER_QUALITY_VOIP,
                                      &err);
    if (resampler_ == NULL) {
      fprintf(stderr, "Failed to allocted resampler!\n");
      abort();
    }
  } else {
    resampler_ = NULL;
  }
}


HALUnit::~HALUnit() {
  if (resampler_ != NULL) speex_resampler_destroy(resampler_);

  PaUtil_FlushRingBuffer(&in_ring_);
  PaUtil_FlushRingBuffer(&out_ring_);
}


void HALUnit::InputCallback(void* arg, size_t bytes) {
  HALUnit* unit = reinterpret_cast<HALUnit*>(arg);

  // Send "ready" callback
  if (!unit->inready_) {
    unit->inready_ = true;
    uv_async_send(unit->inready_cb_);
  }

  // Get data from microphone
  unit->in_unit_.Render(unit->in_buff_, MIN(bytes, sizeof(unit->in_buff_)));

  // Put data to the ring
  PaUtil_WriteRingBuffer(&unit->in_ring_, unit->in_buff_, bytes / 2);

  // Send message to event-loop's thread
  uv_async_send(unit->in_cb_);
}


void HALUnit::OutputCallback(void* arg, char* out, size_t size) {
  HALUnit* unit = reinterpret_cast<HALUnit*>(arg);

  // Send "ready" callback
  if (!unit->outready_) {
    unit->outready_ = true;
    uv_async_send(unit->outready_cb_);
  }

  size_t available = PaUtil_GetRingBufferReadAvailable(&unit->out_ring_);

  if (available * 2 > size) available = size / 2;

  size_t read = PaUtil_ReadRingBuffer(&unit->out_ring_, out, available);

  // Fill rest with zeroes
  if (size > read * 2) {
    memset(out + read * 2, 0, size - read * 2);
  }
}


void HALUnit::Start() {
  inready_ = false;
  outready_ = false;

  in_unit_.Start();
  out_unit_.Start();
}


void HALUnit::Stop() {
  in_unit_.Stop();
  out_unit_.Stop();
}


Buffer* HALUnit::Read(size_t size) {
  Buffer* result = NULL;

  size_t need_size;

  if (resampler_ == NULL) {
    need_size = size;
  } else {
    uint32_t num, denum;

    speex_resampler_get_ratio(resampler_, &num, &denum);
    need_size = (size * num) / denum;
  }

  // Not enough data in ring
  size_t available = PaUtil_GetRingBufferReadAvailable(&in_ring_);
  if (available * 2 < need_size) return result;

  result = Buffer::New(size);

  if (resampler_ == NULL) {
    PaUtil_ReadRingBuffer(&in_ring_, Buffer::Data(result), size / 2);
  } else {
    char tmp[10 * 1024];
    spx_uint32_t tmp_samples;
    spx_uint32_t out_samples;
    int r;

    PaUtil_ReadRingBuffer(&in_ring_, tmp, need_size / 2);

    // Get size in samples
    tmp_samples = need_size / sizeof(int16_t);
    out_samples = size / sizeof(int16_t);
    r = speex_resampler_process_int(
        resampler_,
        0,
        reinterpret_cast<spx_int16_t*>(tmp),
        &tmp_samples,
        reinterpret_cast<spx_int16_t*>(Buffer::Data(result)),
        &out_samples);

    if (r) abort();
  }

  return result;
}


void HALUnit::Put(char* data, size_t size) {
  PaUtil_WriteRingBuffer(&out_ring_, data, size / 2);
}

} // namespace audio
} // namespace vock

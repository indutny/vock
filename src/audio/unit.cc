#include "unit.h"
#include "portaudio/pa_ringbuffer.h"
#include "node.h"
#include "node_buffer.h"

#include <speex/speex_resampler.h>
#include <speex/speex_echo.h>
#include <pthread.h> // pthread_t
#include <string.h> // memset

#include "platform/mac.h"

#define MIN(a, b) ((a) > (b) ? (b) : (a))

namespace vock {
namespace audio {

using node::Buffer;

HALUnit::HALUnit(double rate,
                 size_t frame_size,
                 uv_async_t* in_cb,
                 uv_async_t* inready_cb,
                 uv_async_t* outready_cb)
    : frame_size_(frame_size),
      in_unit_(PlatformUnit::kInputUnit, rate),
      out_unit_(PlatformUnit::kOutputUnit, rate),
      in_cb_(in_cb),
      inready_cb_(inready_cb),
      outready_cb_(outready_cb),
      inready_(false),
      outready_(false) {

  in_unit_.SetInputCallback(InputCallback, this);
  out_unit_.SetOutputCallback(OutputCallback, this);

  int r;
  r = PaUtil_InitializeRingBuffer(&cancel_ring_,
                                  2,
                                  sizeof(cancel_ring_buf_) / 2,
                                  cancel_ring_buf_);
  if (r == -1) abort();
  r = PaUtil_InitializeRingBuffer(&in_ring_,
                                  2,
                                  sizeof(in_ring_buf_) / 2,
                                  in_ring_buf_);
  if (r == -1) abort();
  r = PaUtil_InitializeRingBuffer(&out_ring_,
                                  2,
                                  sizeof(out_ring_buf_) / 2,
                                  out_ring_buf_);
  if (r == -1) abort();
  r = PaUtil_InitializeRingBuffer(&used_ring_,
                                  2,
                                  sizeof(used_ring_buf_) / 2,
                                  used_ring_buf_);
  if (r == -1) abort();

  // Init resampler if hardware doesn't support desired sample rate
  if (rate != in_unit_.GetInputSampleRate()) {
    int err;
    resampler_ = speex_resampler_init(1,
                                      in_unit_.GetInputSampleRate(),
                                      rate,
                                      SPEEX_RESAMPLER_QUALITY_VOIP,
                                      &err);
    if (resampler_ == NULL) {
      fprintf(stderr, "Failed to allocate resampler!\n");
      abort();
    }
  } else {
    resampler_ = NULL;
  }

  // Init echo cancellation
  canceller_ = speex_echo_state_init(frame_size / 2, (frame_size / 2) * 23);
  if (canceller_ == NULL) {
    fprintf(stderr, "Failed to allocate echo canceller!\n");
    abort();
  }

  int irate = rate;
  if (speex_echo_ctl(canceller_, SPEEX_ECHO_SET_SAMPLING_RATE, &irate) != 0) {
    fprintf(stderr, "Failed to set echo canceller's rate!\n");
    abort();
  }

  // Init semaphores
  uv_sem_init(&canceller_sem_, 0);
  pthread_create(&canceller_thread_, NULL, EchoCancelLoop, this);
}


HALUnit::~HALUnit() {
  if (resampler_ != NULL) speex_resampler_destroy(resampler_);
  speex_echo_state_destroy(canceller_);

  PaUtil_FlushRingBuffer(&cancel_ring_);
  PaUtil_FlushRingBuffer(&in_ring_);
  PaUtil_FlushRingBuffer(&out_ring_);
  PaUtil_FlushRingBuffer(&used_ring_);

  pthread_cancel(canceller_thread_);
  uv_sem_destroy(&canceller_sem_);
}


void HALUnit::InputCallback(void* arg, size_t bytes) {
  HALUnit* unit = reinterpret_cast<HALUnit*>(arg);

  // Send "ready" callback
  if (!unit->inready_) {
    unit->inready_ = true;
    uv_async_send(unit->inready_cb_);
  }

  // Get data from microphone
  unit->in_unit_.Render(unit->mic_buff_,
                        MIN(bytes, sizeof(unit->mic_buff_)));

  // Put data to the ring
  PaUtil_WriteRingBuffer(&unit->cancel_ring_, unit->mic_buff_, bytes / 2);

  // Send semaphore signal to canceller thread
  uv_sem_post(&unit->canceller_sem_);
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

  // Put data to the `used` ring
  PaUtil_WriteRingBuffer(&unit->used_ring_, out, size / 2);

  // Send semaphore signal to canceller thread
  uv_sem_post(&unit->canceller_sem_);
}


void* HALUnit::EchoCancelLoop(void* arg) {
  HALUnit* u = reinterpret_cast<HALUnit*>(arg);
  char tmp[100 * 1024];
  char rec[100 * 1024];
  char used[100 * 1024];

  for (;;) {
    uv_sem_wait(&u->canceller_sem_);

    // Read as much frames as possible from input
    for (;;) {
      size_t in_avail = PaUtil_GetRingBufferReadAvailable(&u->cancel_ring_);
      size_t out_avail = PaUtil_GetRingBufferReadAvailable(&u->used_ring_);

      size_t in_needed = u->frame_size_ / 2;
      size_t out_needed = MIN(out_avail, u->frame_size_ / 2);

      // buffer will change size after resampling,
      // take this into account
      if (u->resampler_ != NULL) {
        uint32_t num, denum;

        speex_resampler_get_ratio(u->resampler_, &num, &denum);
        in_needed = (in_needed * num) / denum;
      }

      // Skip if we don't have enough data yet
      if (in_needed > in_avail) break;

      // Read mic buffer
      size_t read;
      if (u->resampler_ == NULL) {
        read = PaUtil_ReadRingBuffer(&u->cancel_ring_, rec, in_needed);
      } else {
        read = PaUtil_ReadRingBuffer(&u->cancel_ring_, tmp, in_needed);
      }
      if (read != in_needed) abort();

      // Read used buffer
      read = PaUtil_ReadRingBuffer(&u->used_ring_, used, out_needed);
      if (read != out_needed) abort();

      // Fill rest with zeroes
      if (read < u->frame_size_ / 2) {
        memset(used + read * 2, 0, u->frame_size_ - 2 * read);
      }

      // Resample input
      if (u->resampler_ != NULL) {
        spx_uint32_t tmp_samples;
        spx_uint32_t out_samples;
        int r;

        // Get size in samples
        tmp_samples = in_needed;
        out_samples = u->frame_size_ / 2;

        // Resample!
        r = speex_resampler_process_int(
            u->resampler_,
            0,
            reinterpret_cast<spx_int16_t*>(tmp),
            &tmp_samples,
            reinterpret_cast<spx_int16_t*>(rec),
            &out_samples);
        if (r) abort();
      }

      // Cancel echo
      speex_echo_cancellation(u->canceller_,
                              reinterpret_cast<spx_int16_t*>(rec),
                              reinterpret_cast<spx_int16_t*>(used),
                              reinterpret_cast<spx_int16_t*>(tmp));

      // Put resampled and cancelled frame into in_ring
      PaUtil_WriteRingBuffer(&u->in_ring_, tmp, u->frame_size_ / 2);

      // Send message to event-loop's thread
      uv_async_send(u->in_cb_);
    }
  }

  return NULL;
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

  // Not enough data in ring
  size_t available = PaUtil_GetRingBufferReadAvailable(&in_ring_);
  if (available * 2 < size) return result;

  result = Buffer::New(size);

  PaUtil_ReadRingBuffer(&in_ring_, Buffer::Data(result), size / 2);

  return result;
}


void HALUnit::Put(char* data, size_t size) {
  PaUtil_WriteRingBuffer(&out_ring_, data, size / 2);
}

} // namespace audio
} // namespace vock

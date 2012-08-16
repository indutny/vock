#include "linux.h"
#include "uv.h"

#include <stdint.h>
#include <string.h> // memmove
#include <alsa/asoundlib.h>
#include <pthread.h>

#define CHECK(fn, msg)\
{\
  int _err_c = fn;\
  if (_err_c < 0) {\
    fprintf(stderr, "%s (%d)\n", msg, _err_c);\
    abort();\
  }\
}

#define RECOVER(device, fn, res)\
{\
  int _err_r;\
  do {\
    _err_r = fn;\
    if (_err_r < 0) {\
      CHECK(snd_pcm_recover(device, _err_r, 1), "Recover failed")\
    }\
  } while (_err_r < 0);\
  res = _err_r;\
}

namespace vock {
namespace audio {

PlatformUnit::PlatformUnit(Kind kind, double rate) : kind_(kind), rate_(rate) {
  // Init device
  snd_pcm_stream_t dir;

  if (kind == kInputUnit) {
    dir = SND_PCM_STREAM_CAPTURE;
  } else {
    dir = SND_PCM_STREAM_PLAYBACK;
  }

  CHECK(snd_pcm_open(&device_, "hw:0,0", dir, 0), "Failed to open device")
  CHECK(snd_pcm_hw_params_malloc(&params_), "Failed to allocate parameters")
  CHECK(snd_pcm_hw_params_any(device_, params_), "Failed to get parameters")
  CHECK(snd_pcm_hw_params_set_access(device_,
                                     params_,
                                     SND_PCM_ACCESS_RW_INTERLEAVED),
        "Failed to set access")
  CHECK(snd_pcm_hw_params_set_format(device_, params_, SND_PCM_FORMAT_S16_LE),
        "Failed to set format")
  CHECK(snd_pcm_hw_params_set_rate(device_, params_, rate, 0),
        "Failed to set rate")
  CHECK(snd_pcm_hw_params_get_channels(params_, &channels_),
        "Set channels failed")

  input_rate_ = rate;

  // Init buffer
  buff_size_ = rate / 100;
  buff_ = new int16_t[channels_ * buff_size_];

  // Apply other params
  CHECK(snd_pcm_hw_params_set_buffer_size(device_, params_, buff_size_ * 3),
        "Failed to set buffer size")
  CHECK(snd_pcm_hw_params_set_period_size(device_, params_, buff_size_, 0),
        "Failed to set period size")
  CHECK(snd_pcm_hw_params(device_, params_),
        "Failed to apply params")
  snd_pcm_hw_params_free(params_);
  CHECK(snd_pcm_prepare(device_), "Failed to prepare device");

  // Add async handler
  if (kind_ == kInputUnit) {
    CHECK(snd_async_add_pcm_handler(&handler_, device_, InputCallback, this),
          "Failed to add async handler for input")
  } else {
    CHECK(snd_async_add_pcm_handler(&handler_, device_, OutputCallback, this),
          "Failed to add async handler for output")
  }
}


PlatformUnit::~PlatformUnit() {
  snd_pcm_drain(device_);
  snd_pcm_close(device_);
  delete[] buff_;
}


void PlatformUnit::InputCallback(snd_async_handler_t* handler) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(
      snd_async_handler_get_callback_private(handler));
  snd_pcm_sframes_t avail;

  RECOVER(unit->device_, snd_pcm_avail_update(unit->device_), avail)
  while (avail >= unit->buff_size_) {
    unit->input_cb_(unit->input_arg_, unit->buff_size_ * 2);

    // Update available bytes count
    RECOVER(unit->device_, snd_pcm_avail_update(unit->device_), avail)
  }
}


void PlatformUnit::OutputCallback(snd_async_handler_t* handler) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(
      snd_async_handler_get_callback_private(handler));
  snd_pcm_sframes_t avail;

  RECOVER(unit->device_, snd_pcm_avail_update(unit->device_), avail)
  unit->RenderOutput(avail);
  RECOVER(unit->device_, snd_pcm_avail_update(unit->device_), avail)
}


void PlatformUnit::Start() {
  snd_pcm_start(device_);
}


void PlatformUnit::Stop() {
  snd_pcm_drop(device_);
}


void PlatformUnit::Render(char* out, size_t size) {
  int err;
  int16_t* buff = buff_;
  int16_t* iout = reinterpret_cast<int16_t*>(out);

  // Read to internal buffer first
  do {
    err = snd_pcm_readi(device_, buff, size / 2);
    if (err < 0) {
      CHECK(snd_pcm_recover(device_, err, 1), "Recover failed")
    }
  } while (err < 0);

  // Set left bytes to zero
  if (err < static_cast<ssize_t>(size / 2)) {
    memset(iout + err, 0, size - 2 * err);
  }

  // Get channel from interleaved stream
  // (By mixing two)
  for (size_t i = 0; i < size / 2; i++) {
    iout[i] = 0;
    for (size_t j = 0; j < channels_; j++) {
      iout[i] += buff[i * channels_ + j] / channels_;
    }
  }
}


void PlatformUnit::RenderOutput(snd_pcm_sframes_t frames) {
  int err;
  int16_t* buff = buff_;

  // Render buff_size maximum
  if (buff_size_ < frames) {
    frames = buff_size_;
  }
  output_cb_(output_arg_, reinterpret_cast<char*>(buff), frames * 2);

  // Non-interleaved mono -> Interleaved multi-channel
  ssize_t i = frames - 1;
  for (; i >= 0; i--) {
    for (size_t j = 0; j < channels_; j++) {
      buff[i * channels_ + j] = buff[i];
    }
  }

  do {
    err = snd_pcm_writei(device_, buff, frames);
    if (err < 0) {
      CHECK(snd_pcm_recover(device_, err, 1), "Recover failed")
    }
  } while (err < 0);
}


double PlatformUnit::GetInputSampleRate() {
  return input_rate_;
}


void PlatformUnit::SetInputCallback(InputCallbackFn cb, void* arg) {
  input_cb_ = cb;
  input_arg_ = arg;
}


void PlatformUnit::SetOutputCallback(OutputCallbackFn cb, void* arg) {
  output_cb_ = cb;
  output_arg_ = arg;
}

} // namespace audio
} // namespace vock


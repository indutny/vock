#include "linux.h"

#include <stdint.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#define CHECK(fn, msg)\
{\
  int err = fn;\
  if (err < 0) {\
    fprintf(stderr, "%s (%d)\n", msg, err);\
    abort();\
  }\
}

namespace vock {
namespace audio {

PlatformUnit::PlatformUnit(Kind kind, double rate) : kind_(kind), rate_(rate) {
  snd_pcm_stream_t dir;

  if (kind == kInputUnit) {
    dir = SND_PCM_STREAM_CAPTURE;
  } else {
    dir = SND_PCM_STREAM_PLAYBACK;
  }

  CHECK(snd_pcm_open(&device_, "hw:0,0", dir, 0), "Failed to open device")
  CHECK(snd_pcm_hw_params_malloc(&params_), "Failed to allocate parameters")
  CHECK(snd_pcm_hw_params_any(device_, params_), "Failed to bind parameters")
  CHECK(snd_pcm_hw_params_set_access(device_,
                                     params_,
                                     SND_PCM_ACCESS_RW_INTERLEAVED),
        "Failed to set access")
  CHECK(snd_pcm_hw_params_set_format(device_, params_, SND_PCM_FORMAT_S16_LE),
        "Failed to set format")
  CHECK(snd_pcm_hw_params_set_rate(device_, params_, rate, 0),
        "Failed to set rate")
  input_rate_ = rate;
  CHECK(snd_pcm_hw_params_get_channels(params_, &channels_),
        "Set channels failed")
  CHECK(snd_pcm_hw_params_set_buffer_size(device_, params_, rate / 100),
        "Failed to set buffer size")
  CHECK(snd_pcm_hw_params_set_period_size(device_, params_, rate / 100, 0),
        "Failed to set period size")
  CHECK(snd_pcm_hw_params(device_, params_),
        "Failed to apply params")
  snd_pcm_hw_params_free(params_);

  buff_size_ = rate / 100;
  buff_ = new int16_t[channels_ * buff_size_];

  pthread_create(&loop_, NULL, PlatformUnit::Loop, this);
}


PlatformUnit::~PlatformUnit() {
  pthread_cancel(loop_);
  snd_pcm_close(device_);
  delete[] buff_;
}


void* PlatformUnit::Loop(void* arg) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(arg);
  int err = 0;
  int16_t* buff;

  if (unit->kind_ == kInputUnit) {
    while (1) {
      buff = unit->buff_;
      err = snd_pcm_readi(unit->device_, buff, unit->buff_size_);
      if (err == -EPIPE) {
        CHECK(snd_pcm_prepare(unit->device_), "Prepare failed")
      } else if (err < 0) {
        fprintf(stderr, "Failed to read: %d\n", err);
        abort();
      }
    }
  } else if (unit->kind_ == kOutputUnit) {
    while (1) {
      buff = unit->buff_;
      err = snd_pcm_writei(unit->device_, buff, unit->buff_size_);
      if (err == -EPIPE) {
        CHECK(snd_pcm_prepare(unit->device_), "Prepare failed")
      } else if (err < 0) {
        fprintf(stderr, "Failed to write: %d\n", err);
        abort();
      }
    }
  }

  return NULL;
}


void PlatformUnit::Start() {
}


void PlatformUnit::Stop() {
}


void PlatformUnit::Render(char* out, size_t size) {
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


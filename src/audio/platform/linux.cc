#include "linux.h"
#include "uv.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h> // fprintf
#include <string.h> // memcpy, memset
#include <stdlib.h> // abort
#include <pulse/pulseaudio.h>

namespace vock {
namespace audio {

PlatformUnit::PlatformUnit(Kind kind, double rate) : pa_ml_(NULL),
                                                     pa_mlapi_(NULL),
                                                     pa_ctx_(NULL),
                                                     pa_stream_(NULL),
                                                     active_(false),
                                                     kind_(kind),
                                                     rate_(rate) {
  pa_ss_.format = PA_SAMPLE_S16LE;
  pa_ss_.channels = 1;
  pa_ss_.rate = rate;
  input_rate_ = rate;

  buff_size_ = 2 * rate / 100;

  uv_sem_init(&loop_terminate_, 0);
  uv_mutex_init(&stream_mutex_);
  uv_thread_create(&loop_, Loop, this);
}


PlatformUnit::~PlatformUnit() {
  Stop();
  uv_sem_post(&loop_terminate_);
  uv_thread_join(&loop_);
  uv_mutex_destroy(&stream_mutex_);
  uv_sem_destroy(&loop_terminate_);
}


void PlatformUnit::Loop(void* arg) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(arg);

  unit->StartLoop();

  while(1) {
    if (!unit->RunLoop()) break;
    if (uv_sem_trywait(&unit->loop_terminate_) == 0) break;
  }

  unit->EndLoop();
}


void PlatformUnit::StateCallback(pa_context* ctx, void* arg) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(arg);

  switch (pa_context_get_state(ctx)) {
   case PA_CONTEXT_UNCONNECTED:
   case PA_CONTEXT_CONNECTING:
   case PA_CONTEXT_AUTHORIZING:
   case PA_CONTEXT_SETTING_NAME:
   default:
    break;
   case PA_CONTEXT_FAILED:
   case PA_CONTEXT_TERMINATED:
    unit->pa_state_ = 2;
    break;
   case PA_CONTEXT_READY:
    unit->pa_state_ = 1;
   break;
  }
}


void PlatformUnit::StartLoop() {
  pa_buffer_attr attr;
  pa_stream_flags_t flags;
  pa_stream* stream;

  // Initialize loop, API and context
  pa_ml_ = pa_mainloop_new();
  assert(pa_ml_ != NULL);
  pa_mlapi_ = pa_mainloop_get_api(pa_ml_);
  pa_ctx_ = pa_context_new(pa_mlapi_, "Vock");
  assert(pa_ctx_ != NULL);

  // Connect to server
  pa_state_ = 0;
  pa_context_set_state_callback(pa_ctx_, StateCallback, this);
  pa_context_connect(pa_ctx_, NULL, PA_CONTEXT_NOFLAGS, NULL);

  // Wait for connection establishment
  while (pa_state_ == 0) {
    pa_mainloop_iterate(pa_ml_, 1, NULL);
  }
  assert(pa_state_ == 1);

  // Create stream
  attr.maxlength = buff_size_ * 2;
  attr.tlength = buff_size_;
  attr.prebuf = 0xffffffff;
  attr.minreq = 0xffffffff;
  attr.fragsize = buff_size_;

  stream = pa_stream_new(pa_ctx_, "Vock.Stream", &pa_ss_, NULL);
  assert(stream != NULL);

  uv_mutex_lock(&stream_mutex_);
  flags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY |
                                         PA_STREAM_INTERPOLATE_TIMING |
                                         PA_STREAM_AUTO_TIMING_UPDATE |
                                         (active_ ?
                                            0 : PA_STREAM_START_CORKED));

  // Connect it to playback/record
  if (kind_ == kInputUnit) {
    pa_stream_set_read_callback(stream, RequestCallback, this);
    pa_stream_connect_record(stream, NULL, &attr, flags);
  } else {
    pa_stream_set_write_callback(stream, RequestCallback, this);
    pa_stream_connect_playback(stream, NULL, &attr, flags, NULL, NULL);
  }

  pa_stream_ = stream;
  uv_mutex_unlock(&stream_mutex_);
}


void PlatformUnit::EndLoop() {
  pa_context_disconnect(pa_ctx_);
  pa_context_unref(pa_ctx_);
  pa_mainloop_free(pa_ml_);
}


bool PlatformUnit::RunLoop() {
  pa_mainloop_iterate(pa_ml_, 1, NULL);

  // Continue polling
  return true;
}


void PlatformUnit::RequestCallback(pa_stream* p, size_t bytes, void* arg) {
  reinterpret_cast<PlatformUnit*>(arg)->RequestCallback(bytes);
}


void PlatformUnit::RequestCallback(size_t bytes) {
  if (kind_ == kInputUnit) {
    input_cb_(input_arg_, bytes);
  } else {
    int r;
    char* buff;

    uv_mutex_lock(&stream_mutex_);
    assert(pa_stream_begin_write(pa_stream_,
                                 reinterpret_cast<void**>(&buff),
                                 &bytes) == 0);
    output_cb_(output_arg_, buff, bytes);
    r = pa_stream_write(pa_stream_, buff, bytes, NULL, 0, PA_SEEK_RELATIVE);
    assert(r >= 0);
    uv_mutex_unlock(&stream_mutex_);
  }
}


void PlatformUnit::Start() {
  uv_mutex_lock(&stream_mutex_);
  if (!active_) {
    active_ = true;

    if (pa_stream_ != NULL) {
      pa_stream_cork(pa_stream_, 0, NULL, NULL);
    }
  }
  uv_mutex_unlock(&stream_mutex_);
}


void PlatformUnit::Stop() {
  uv_mutex_lock(&stream_mutex_);
  if (active_) {
    active_ = false;

    if (pa_stream_ != NULL) {
      pa_stream_cork(pa_stream_, 1, NULL, NULL);
    }
  }
  uv_mutex_unlock(&stream_mutex_);
}


void PlatformUnit::Render(char* out, size_t size) {
  int r;
  const void* data;
  size_t bytes = size;

  uv_mutex_lock(&stream_mutex_);
  r = pa_stream_peek(pa_stream_, &data, &bytes);
  assert(r >= 0);
  assert(bytes <= size);

  memcpy(out, data, bytes);
  memset(out + bytes, 0, size - bytes);

  pa_stream_drop(pa_stream_);
  uv_mutex_unlock(&stream_mutex_);
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


#include "linux.h"

#include <stdint.h>
#include <alsa/asoundlib.h>

#define CHECK(fn, msg)\
{\
  OSStatus st = fn;\
  if (st != noErr) {\
    fprintf(stderr, "%s (%d)\n", msg, st);\
    abort();\
  }\
}

namespace vock {
namespace audio {

PlatformUnit::PlatformUnit(Kind kind, double rate) : rate_(rate) {
}


PlatformUnit::~PlatformUnit() {
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


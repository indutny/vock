#ifndef _SRC_AUDIO_PLATFORM_LINUX_
#define _SRC_AUDIO_PLATFORM_LINUX_

#include "uv.h"
#include <pulse/pulseaudio.h>

namespace vock {
namespace audio {

typedef void (*InputCallbackFn)(void*, size_t);
typedef void (*OutputCallbackFn)(void*, char*, size_t);

class PlatformUnit {
 public:
  enum Kind {
    kInputUnit,
    kOutputUnit
  };

  PlatformUnit(Kind kind, double rate);
  ~PlatformUnit();

  void Start();
  void Stop();

  void Render(char* out, size_t size);

  double GetInputSampleRate();

  void SetInputCallback(InputCallbackFn cb, void* arg);
  void SetOutputCallback(OutputCallbackFn cb, void* arg);

 private:
  static void Loop(void* arg);
  static void StateCallback(pa_context* ctx, void* arg);
  static void RequestCallback(pa_stream* p, size_t bytes, void* arg);

  void StartLoop();
  void EndLoop();
  bool RunLoop();
  void RequestCallback(size_t bytes);

  pa_mainloop* pa_ml_;
  pa_mainloop_api* pa_mlapi_;
  pa_context* pa_ctx_;
  pa_sample_spec pa_ss_;
  pa_stream* pa_stream_;
  volatile int pa_state_;

  uv_thread_t loop_;
  uv_sem_t loop_terminate_;
  uv_mutex_t stream_mutex_;

  bool active_;

  Kind kind_;
  double rate_;
  double input_rate_;
  unsigned int channels_;

  ssize_t buff_size_;

  InputCallbackFn input_cb_;
  void* input_arg_;

  OutputCallbackFn output_cb_;
  void* output_arg_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_PLATFORM_LINUX_

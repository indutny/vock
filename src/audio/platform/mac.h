#ifndef _SRC_AUDIO_PLATFORM_MAC_
#define _SRC_AUDIO_PLATFORM_MAC_

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

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

  struct InputCallbackState {
    AudioUnitRenderActionFlags* flags;
    const AudioTimeStamp* ts;
    UInt32 bus;
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
  static const int kInputBus = 1;
  static const int kOutputBus = 0;

  static OSStatus InputCallback(void* arg,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* ts,
                                UInt32 bus,
                                UInt32 frame_count,
                                AudioBufferList* data);
  static OSStatus OutputCallback(void* arg,
                                 AudioUnitRenderActionFlags* flags,
                                 const AudioTimeStamp* ts,
                                 UInt32 bus,
                                 UInt32 frame_count,
                                 AudioBufferList* data);

  AudioUnit unit_;
  double rate_;
  double input_rate_;

  InputCallbackFn input_cb_;
  void* input_arg_;
  InputCallbackState input_state_;
  AudioBufferList in_list_;

  OutputCallbackFn output_cb_;
  void* output_arg_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_PLATFORM_MAC_

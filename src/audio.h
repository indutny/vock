#ifndef _SRC_AUDIO_H_
#define _SRC_AUDIO_H_

#include "node.h"
#include "node_object_wrap.h"
#include "circle.h"

#include <AudioUnit/AudioUnit.h>

namespace vock {
namespace audio {

using namespace node;

class Audio : public ObjectWrap {
 public:
  Audio();
  ~Audio();

  AudioUnit CreateAudioUnit(bool is_input);

  static void Init(v8::Handle<v8::Object> target);

  static v8::Handle<v8::Value> New(const v8::Arguments& arg);
  static v8::Handle<v8::Value> Start(const v8::Arguments& arg);
  static v8::Handle<v8::Value> Stop(const v8::Arguments& arg);
  static v8::Handle<v8::Value> Enqueue(const v8::Arguments& arg);

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

  static void InputAsyncCallback(uv_async_t* async, int status);

 protected:
  static const int kInputBus = 1;
  static const int kOutputBus = 0;

  AudioStreamBasicDescription desc_;
  AudioUnit in_unit_;
  AudioUnit out_unit_;
  Circle in_buffer_;
  Circle out_buffer_;
  uv_mutex_t in_mutex_;
  uv_mutex_t out_mutex_;

  uv_async_t in_async_;
  AudioBufferList* blist_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_H_

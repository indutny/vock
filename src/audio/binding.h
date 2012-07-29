#ifndef _SRC_AUDIO_BINDING_H_
#define _SRC_AUDIO_BINDING_H_

#include "unit.h"

#include "node.h"
#include "node_object_wrap.h"

namespace vock {
namespace audio {

using namespace node;

class Audio : public ObjectWrap {
 public:
  Audio(Float64 rate, size_t frame_size);
  ~Audio();

  static void Init(v8::Handle<v8::Object> target);

  static v8::Handle<v8::Value> New(const v8::Arguments& arg);
  static v8::Handle<v8::Value> Start(const v8::Arguments& arg);
  static v8::Handle<v8::Value> Stop(const v8::Arguments& arg);
  static v8::Handle<v8::Value> Enqueue(const v8::Arguments& arg);
  static v8::Handle<v8::Value> GetRms(const v8::Arguments& arg);
  static v8::Handle<v8::Value> ApplyGain(const v8::Arguments& arg);

  static void InputAsyncCallback(uv_async_t* async, int status);
  static void InputReadyCallback(uv_async_t* async, int status);
  static void OutputReadyCallback(uv_async_t* async, int status);

 protected:
  HALUnit* unit_;
  size_t frame_size_;
  bool input_ready_;
  bool output_ready_;
  bool active_;

  uv_async_t in_async_;
  uv_async_t inready_async_;
  uv_async_t outready_async_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_BINDING_H_

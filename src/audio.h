#ifndef _SRC_AUDIO_H_
#define _SRC_AUDIO_H_

#include "node.h"
#include "node_object_wrap.h"

#include "AudioToolbox/AudioToolbox.h"

namespace vock {
namespace audio {

using namespace node;

struct QueueItem {
  AudioQueueBufferRef buf;
  UInt32 packet_count;
};

class Recorder : public ObjectWrap {
 public:
  Recorder(Float64 rate, Float64 seconds);
  ~Recorder();

  static void InputCallback(void* data,
                            AudioQueueRef queue,
                            AudioQueueBufferRef buf,
                            const AudioTimeStamp* start_time,
                            UInt32 packet_count,
                            const AudioStreamPacketDescription* packets);
  static void AsyncCallback(uv_async_t* async, int status);

  static void Init(v8::Handle<v8::Object> target);

  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> Start(const v8::Arguments& args);
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);

 protected:
  static const int bufferCount_ = 16;

  AudioQueueRef aq_;
  AudioStreamBasicDescription desc_;
  AudioQueueBufferRef buffers_[bufferCount_];
  UInt32 bufferSize_;

  // Internal buffer queue
  QueueItem queue_[bufferCount_];
  int queue_index_;
  uv_mutex_t queue_mutex_;

  uv_async_t async_;
  bool recording_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_H_

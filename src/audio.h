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

class Base : public ObjectWrap {
 public:
  Base() : active_(false) {
  }

  static v8::Handle<v8::Value> Start(const v8::Arguments& args);
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);

 protected:
  AudioQueueRef aq_;
  AudioStreamBasicDescription desc_;
  bool active_;
};

class Recorder : public Base {
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

 protected:
  static const int kBufferCount = 16;

  AudioQueueBufferRef buffers_[kBufferCount];
  UInt32 buffer_size_;

  // Internal buffer queue
  QueueItem queue_[kBufferCount];
  int queue_index_;
  uv_mutex_t queue_mutex_;

  uv_async_t async_;
};

class Player : public Base {
 public:
  Player(Float64 rate);
  ~Player();

  static void OutputCallback(void* data,
                             AudioQueueRef queue,
                             AudioQueueBufferRef buf);
  static void AsyncCallback(uv_async_t* async, int status);

  static void Init(v8::Handle<v8::Object> target);

  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> Enqueue(const v8::Arguments& args);

 protected:
  static const int kQueueLen = 512;

  UInt32 buffer_size_;
  uv_async_t async_;

  AudioQueueBufferRef queue_[kQueueLen];
  int queue_index_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_H_

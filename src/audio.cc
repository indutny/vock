#include "audio.h"
#include "common.h"

#include "node.h"
#include "node_buffer.h"

#include <AudioToolbox/AudioToolbox.h>
#include <string.h>
#include <unistd.h>

namespace vock {
namespace audio {

using namespace node;
using v8::HandleScope;
using v8::Handle;
using v8::Persistent;
using v8::Local;
using v8::Array;
using v8::String;
using v8::Value;
using v8::Arguments;
using v8::Object;
using v8::Null;
using v8::True;
using v8::False;
using v8::Function;
using v8::FunctionTemplate;
using v8::ThrowException;

static Persistent<String> ondata_sym;
static Persistent<String> onconsume_sym;

Handle<Value> Base::Start(const Arguments& args) {
  HandleScope scope;
  Base* r = ObjectWrap::Unwrap<Base>(args.This());

  if (r->active_) return scope.Close(False());

  OSStatus st = AudioQueueStart(r->aq_, NULL);
  if (st) {
    return scope.Close(ThrowException(String::New("AudioQueueStart failed!")));
  }
  r->active_ = true;
  r->Ref();

  return scope.Close(True());
}


Handle<Value> Base::Stop(const Arguments& args) {
  HandleScope scope;
  Base* r = ObjectWrap::Unwrap<Base>(args.This());

  if (!r->active_) return scope.Close(False());

  OSStatus st = AudioQueueStop(r->aq_, NULL);
  if (st) {
    return scope.Close(ThrowException(String::New("AudioQueueStop failed!")));
  }
  r->active_ = false;
  r->Unref();

  return scope.Close(True());
}


Recorder::Recorder(Float64 rate, Float64 seconds) {
  OSStatus st;

  // Fill AudioQueue description
  memset(&desc_, 0, sizeof(desc_));
  desc_.mSampleRate = rate;
  desc_.mFormatID = kAudioFormatLinearPCM;
  desc_.mFormatFlags = CalculateLPCMFlags(16, 16, false, true);
  desc_.mFramesPerPacket = 1;
  desc_.mChannelsPerFrame = 1;
  desc_.mBitsPerChannel = 16;
  desc_.mBytesPerPacket =
      desc_.mBytesPerFrame = (desc_.mBitsPerChannel >> 3) *
                              desc_.mChannelsPerFrame;
  desc_.mReserved = 0;

  // Calculate optimal buffer size
  buffer_size_ = static_cast<UInt32>(desc_.mSampleRate * desc_.mBytesPerPacket *
                                     seconds);

  // Create AudioQueue
  st = AudioQueueNewInput(&desc_, InputCallback, this, NULL, NULL, 0, &aq_);
  if (st) {
    ThrowException(String::New("AudioQueueNewInput failed!"));
    return;
  }

  // Create buffers and enqueue them
  for (int i = 0; i < kBufferCount; i++) {
    st = AudioQueueAllocateBuffer(aq_, buffer_size_, &buffers_[i]);
    if (st) {
      for (int j = 0; j < i; j++) {
        AudioQueueFreeBuffer(aq_, buffers_[j]);
      }
      AudioQueueDispose(aq_, true);
      ThrowException(String::New("AudioQueueAllocateBuffer failed!"));
      return;
    }

    st = AudioQueueEnqueueBuffer(aq_, buffers_[i], 0, NULL);
    if (st) {
      for (int j = 0; j <= i; j++) {
        AudioQueueFreeBuffer(aq_, buffers_[j]);
      }
      AudioQueueDispose(aq_, true);
      ThrowException(String::New("AudioQueueEnqueueBuffer failed!"));
      return;
    }
  }

  // Create async handle
  if (uv_async_init(uv_default_loop(), &async_, AsyncCallback)) {
    for (int i = 0; i < kBufferCount; i++) {
      AudioQueueFreeBuffer(aq_, buffers_[i]);
    }
    AudioQueueDispose(aq_, true);
    ThrowException(String::New("uv_async_init failed!"));
    return;
  }

  // Prepare buffer queue
  if (uv_mutex_init(&queue_mutex_)) abort();
  memset(queue_, 0, sizeof(queue_));
  queue_index_ = 0;
}


Recorder::~Recorder() {
  // TODO: Stop recording?
  AudioQueueReset(aq_);
  for (int i = 0; i < kBufferCount; i++) {
    AudioQueueFreeBuffer(aq_, buffers_[i]);
  }
  AudioQueueDispose(aq_, true);
  uv_close(reinterpret_cast<uv_handle_t*>(&async_), NULL);
  uv_mutex_destroy(&queue_mutex_);
}


void Recorder::InputCallback(void* data,
                             AudioQueueRef queue,
                             AudioQueueBufferRef buf,
                             const AudioTimeStamp* start_time,
                             UInt32 packet_count,
                             const AudioStreamPacketDescription* packets) {
  Recorder* r = reinterpret_cast<Recorder*>(data);
  uv_mutex_lock(&r->queue_mutex_);

  r->queue_[r->queue_index_].buf = buf;
  r->queue_[r->queue_index_].packet_count = packet_count;

  r->queue_index_++;
  assert(r->queue_index_ <= r->kBufferCount);

  uv_async_send(&r->async_);

  uv_mutex_unlock(&r->queue_mutex_);
}


void Recorder::AsyncCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Recorder* r = container_of(async, Recorder, async_);

  uv_mutex_lock(&r->queue_mutex_);
  if (r->active_ == false) {
    if (r->queue_index_ == r->kBufferCount) {
      r->Unref();
    }
    return;
  }

  Local<Array> buffers = Array::New();
  int j = 0;
  for (int i = 0; i < r->queue_index_; i++) {
    if (r->queue_[i].buf == NULL) continue;

    // Pull buffer from queue
    AudioQueueBufferRef buffer = r->queue_[i].buf;
    UInt32 packet_count = r->queue_[i].packet_count;
    r->queue_[i].buf = NULL;

    // Put it in js buffer
    Buffer* b = Buffer::New(reinterpret_cast<char*>(buffer->mAudioData),
                            packet_count * r->desc_.mBytesPerPacket);
    buffers->Set(j++, b->handle_);

    // And enqueue again
    if (AudioQueueEnqueueBuffer(r->aq_, buffer, 0, NULL)) {
      // XXX: Handle this?!
      abort();
    }
  }

  r->queue_index_ = 0;
  uv_mutex_unlock(&r->queue_mutex_);

  if (j != 0) {
    Handle<Value> argv[1] = { buffers };

    if (ondata_sym.IsEmpty()) {
      ondata_sym = Persistent<String>::New(String::NewSymbol("ondata"));
    }
    MakeCallback(r->handle_, ondata_sym, 1, argv);
  };
}


Handle<Value> Recorder::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
    return scope.Close(ThrowException(String::New(
        "First two arguments should be numbers")));
  }

  // Second argument is in msec
  Recorder* r = new Recorder(args[0]->NumberValue(),
                             args[1]->NumberValue() / 1000);
  r->Wrap(args.Holder());

  return scope.Close(args.This());
}


void Recorder::Init(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(Recorder::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Recorder"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Recorder::Start);
  NODE_SET_PROTOTYPE_METHOD(t, "stop", Recorder::Stop);

  target->Set(String::NewSymbol("Recorder"), t->GetFunction());
}


Player::Player(Float64 rate) : queue_index_(0) {
  OSStatus st;

  // Nullify queue
  memset(&queue_, 0, sizeof(queue_));

  // Fill AudioQueue description
  memset(&desc_, 0, sizeof(desc_));
  desc_.mSampleRate = rate;
  desc_.mFormatID = kAudioFormatLinearPCM;
  desc_.mFormatFlags = CalculateLPCMFlags(16, 16, false, true);
  desc_.mFramesPerPacket = 1;
  desc_.mChannelsPerFrame = 1;
  desc_.mBitsPerChannel = 16;
  desc_.mBytesPerPacket =
      desc_.mBytesPerFrame = (desc_.mBitsPerChannel >> 3) *
                              desc_.mChannelsPerFrame;
  desc_.mReserved = 0;

  // Calculate optimal buffer size
  buffer_size_ = static_cast<UInt32>(desc_.mSampleRate * desc_.mBytesPerPacket *
                                     0.2);

  // Create AudioQueue
  st = AudioQueueNewOutput(&desc_, OutputCallback, this, NULL, NULL, 0, &aq_);
  if (st) {
    ThrowException(String::New("AudioQueueNewOutput failed!"));
    return;
  }

  uv_async_init(uv_default_loop(), &async_, AsyncCallback);
}


Player::~Player() {
  AudioQueueReset(aq_);
  for (int i = 0; i < kQueueLen; i++) {
    if (queue_[i] != NULL) {
      AudioQueueFreeBuffer(aq_, queue_[i]);
    }
  }
  AudioQueueDispose(aq_, true);
  uv_close(reinterpret_cast<uv_handle_t*>(&async_), NULL);
}


void Player::OutputCallback(void* data,
                            AudioQueueRef queue,
                            AudioQueueBufferRef buf) {
  Player* p = reinterpret_cast<Player*>(data);

  // Insert buffer into queue (to reuse it later)
  if (p->queue_index_ < kQueueLen) {
    p->queue_[p->queue_index_++] = buf;
  }

  uv_async_send(&p->async_);
}


void Player::AsyncCallback(uv_async_t* async, int status) {
  Player* p = container_of(async, Player, async_);

  Handle<Value> argv[0] = { };

  if (onconsume_sym.IsEmpty()) {
    onconsume_sym = Persistent<String>::New(String::NewSymbol("onconsume"));
  }
  MakeCallback(p->handle_, onconsume_sym, 0, argv);
}


Handle<Value> Player::Enqueue(const Arguments& args) {
  HandleScope scope;

  Player* p = ObjectWrap::Unwrap<Player>(args.This());
  if (args.Length() < 1 || !Buffer::HasInstance(args[0])) {
    return scope.Close(ThrowException(String::New(
        "First argument should be Buffer")));
  }

  OSStatus st;

  char* data = Buffer::Data(args[0].As<Object>());
  size_t size = Buffer::Length(args[0].As<Object>());

  // Reuse existing buffers or create new
  while (size > 0) {
    AudioQueueBufferRef buffer;

    if (p->queue_index_ > 1) {
      buffer = p->queue_[--p->queue_index_];
      p->queue_[p->queue_index_] = NULL;
    } else {
      st = AudioQueueAllocateBuffer(p->aq_, p->buffer_size_, &buffer);
      if (st) {
        return scope.Close(ThrowException(String::New(
            "AudioQueueAllocateBuffer failed!")));
      }
    }

    size_t buf_size = size > p->buffer_size_ ?
        buffer->mAudioDataByteSize :
        size;

    memcpy(buffer->mAudioData, data, buf_size);
    buffer->mAudioDataByteSize = buf_size;

    data += buf_size;
    size -= buf_size;

    st = AudioQueueEnqueueBuffer(p->aq_, buffer, 0, NULL);
    if (st) {
      return scope.Close(ThrowException(String::New(
              "AudioQueueEnqueueBuffer failed!")));
    }
  }

  return scope.Close(args.This());
}


Handle<Value> Player::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return scope.Close(ThrowException(String::New(
        "First argument should be number")));
  }

  // Second argument is in msec
  Player* p = new Player(args[0]->NumberValue());
  p->Wrap(args.Holder());

  return scope.Close(args.This());
}


void Player::Init(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(Player::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Player"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Player::Start);
  NODE_SET_PROTOTYPE_METHOD(t, "stop", Player::Stop);
  NODE_SET_PROTOTYPE_METHOD(t, "enqueue", Player::Enqueue);

  target->Set(String::NewSymbol("Player"), t->GetFunction());
}

} // namespace audio
} // namespace vock

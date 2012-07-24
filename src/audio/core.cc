#include "core.h"
#include "au.h"
#include "common.h"

#include "node.h"
#include "node_buffer.h"
#include "speex/speex_echo.h"

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
using v8::Number;
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
static Persistent<String> oninputready_sym;
static Persistent<String> onoutputready_sym;

Audio::Audio(Float64 rate, size_t frame_size) : frame_size_(frame_size) {
  // Setup async callbacks
  if (uv_async_init(uv_default_loop(), &in_async_, InputAsyncCallback)) {
    abort();
  }
  if (uv_async_init(uv_default_loop(), &inready_async_, InputReadyCallback)) {
    abort();
  }
  if (uv_async_init(uv_default_loop(), &outready_async_, OutputReadyCallback)) {
    abort();
  }
  uv_unref(reinterpret_cast<uv_handle_t*>(&in_async_));
  uv_unref(reinterpret_cast<uv_handle_t*>(&inready_async_));
  uv_unref(reinterpret_cast<uv_handle_t*>(&outready_async_));

  // Init Hardware abstraction layer's unit
  unit_ = new HALUnit(rate, &in_async_, &inready_async_, &outready_async_);

  if (unit_->err != NULL) {
    ThrowException(String::New(unit_->err));
  }

  // Init echo canceller
  size_t sample_frame_size = frame_size / sizeof(int16_t);
  echo_state_ = speex_echo_state_init_mc(sample_frame_size,
                                         42 * sample_frame_size,
                                         1,
                                         1,
                                         rate);
}


Audio::~Audio() {
  uv_close(reinterpret_cast<uv_handle_t*>(&in_async_), NULL);
  delete unit_;
  speex_echo_state_destroy(echo_state_);
}


Handle<Value> Audio::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
    return scope.Close(ThrowException(String::New(
        "First argument should be number")));
  }

  // Second argument is in msec
  Audio* a = new Audio(args[0]->NumberValue(), args[1]->Int32Value());
  a->Wrap(args.Holder());

  return scope.Close(args.This());
}


Handle<Value> Audio::Start(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (a->unit_->Start()) {
    return scope.Close(ThrowException(String::New(
        "Failed to start unit!")));
  }

  uv_ref(reinterpret_cast<uv_handle_t*>(&a->in_async_));
  a->Ref();

  return scope.Close(Null());
}


Handle<Value> Audio::Stop(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (a->unit_->Stop()) {
    return scope.Close(ThrowException(String::New(
        "Failed to stop unit!")));
  }
  uv_unref(reinterpret_cast<uv_handle_t*>(&a->in_async_));
  a->Unref();

  return scope.Close(Null());
}


Handle<Value> Audio::Enqueue(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (args.Length() < 1 || !Buffer::HasInstance(args[0])) {
    return scope.Close(ThrowException(String::New(
        "First argument should be a Buffer!")));
  }

  a->unit_->Put(Buffer::Data(args[0].As<Object>()),
                Buffer::Length(args[0].As<Object>()));

  return scope.Close(Null());
}


Handle<Value> Audio::CancelEcho(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (args.Length() < 2 ||
      !Buffer::HasInstance(args[0]) ||
      !Buffer::HasInstance(args[1])) {
    return scope.Close(ThrowException(String::New(
        "First two arguments should be Buffers!")));
  }

  if (Buffer::Length(args[0].As<Object>()) != a->frame_size_ ||
      Buffer::Length(args[1].As<Object>()) != a->frame_size_) {
    return scope.Close(ThrowException(String::New(
        "Buffers have incorrect size!")));
  }

  char* rec = Buffer::Data(args[0].As<Object>());
  char* play = Buffer::Data(args[1].As<Object>());
  Buffer* out = Buffer::New(Buffer::Length(args[1].As<Object>()));

  speex_echo_cancellation(a->echo_state_,
                          reinterpret_cast<int16_t*>(rec),
                          reinterpret_cast<int16_t*>(play),
                          reinterpret_cast<int16_t*>(Buffer::Data(out)));

  return scope.Close(out->handle_);
}


void Audio::InputAsyncCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = container_of(async, Audio, in_async_);

  Buffer* buffer;
  for (;;) {
    buffer = a->unit_->Read(a->frame_size_);
    if (buffer == NULL) break;

    Handle<Value> argv[1] = { buffer->handle_ };
    MakeCallback(a->handle_, ondata_sym, 1, argv);
  }
}


void Audio::InputReadyCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = container_of(async, Audio, inready_async_);

  Handle<Value> argv[0] = {};
  MakeCallback(a->handle_, oninputready_sym, 0, argv);
}


void Audio::OutputReadyCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = container_of(async, Audio, outready_async_);

  Handle<Value> argv[0] = {};
  MakeCallback(a->handle_, onoutputready_sym, 0, argv);
}


void Audio::Init(Handle<Object> target) {
  HandleScope scope;

  ondata_sym = Persistent<String>::New(String::NewSymbol("ondata"));
  oninputready_sym = Persistent<String>::New(String::NewSymbol("oninputready"));
  onoutputready_sym = Persistent<String>::New(
      String::NewSymbol("onoutputready"));

  Local<FunctionTemplate> t = FunctionTemplate::New(Audio::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Audio"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Audio::Start);
  NODE_SET_PROTOTYPE_METHOD(t, "stop", Audio::Stop);
  NODE_SET_PROTOTYPE_METHOD(t, "enqueue", Audio::Enqueue);
  NODE_SET_PROTOTYPE_METHOD(t, "cancelEcho", Audio::CancelEcho);

  target->Set(String::NewSymbol("Audio"), t->GetFunction());
}

} // namespace audio
} // namespace vock

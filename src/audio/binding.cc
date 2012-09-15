#include "binding.h"
#include "unit.h"
#include "common.h"

#include "node.h"
#include "node_buffer.h"

#include <string.h>
#include <unistd.h>
#include <math.h> // sqrt
#include <stdlib.h> // abort

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

Audio::Audio(double rate, size_t frame_size, ssize_t latency)
    : frame_size_(frame_size),
      input_ready_(false),
      output_ready_(false),
      active_(false) {
  uv_async_t** handles[3] = { &in_async_, &inready_async_, &outready_async_ };
  for (int i = 0; i < 3; i++) {
    uv_async_t* handle = new uv_async_t();
    handle->data = this;
    *handles[i] = handle;
  }

  // Setup async callbacks
  if (uv_async_init(uv_default_loop(), in_async_, InputAsyncCallback)) {
    abort();
  }
  if (uv_async_init(uv_default_loop(), inready_async_, InputReadyCallback)) {
    abort();
  }
  if (uv_async_init(uv_default_loop(), outready_async_, OutputReadyCallback)) {
    abort();
  }
  uv_unref(reinterpret_cast<uv_handle_t*>(in_async_));
  uv_unref(reinterpret_cast<uv_handle_t*>(inready_async_));
  uv_unref(reinterpret_cast<uv_handle_t*>(outready_async_));

  // Init Hardware abstraction layer's unit
  unit_ = new HALUnit(rate,
                      frame_size,
                      latency,
                      in_async_,
                      inready_async_,
                      outready_async_);
}


void OnAsyncClose(uv_handle_t* handle) {
  delete handle;
}


Audio::~Audio() {
  uv_close(reinterpret_cast<uv_handle_t*>(in_async_), OnAsyncClose);
  uv_close(reinterpret_cast<uv_handle_t*>(inready_async_), OnAsyncClose);
  uv_close(reinterpret_cast<uv_handle_t*>(outready_async_), OnAsyncClose);
  delete unit_;
}


Handle<Value> Audio::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 3 ||
      !args[0]->IsNumber() ||
      !args[1]->IsNumber() ||
      !args[2]->IsNumber()) {
    return scope.Close(ThrowException(String::New(
        "First three arguments should be numbers")));
  }

  // Second argument is in msec
  Audio* a = new Audio(args[0]->NumberValue(),
                       args[1]->Int32Value(),
                       args[2]->Int32Value());
  a->Wrap(args.Holder());

  return scope.Close(args.This());
}


Handle<Value> Audio::Start(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (a->active_) {
    return scope.Close(ThrowException(String::New(
        "Unit is already started!")));
  }

  a->unit_->Start();

  uv_ref(reinterpret_cast<uv_handle_t*>(a->in_async_));
  a->Ref();
  a->active_ = true;

  return scope.Close(Null());
}


Handle<Value> Audio::Stop(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (a->active_) {
    return scope.Close(ThrowException(String::New(
        "Unit is already stopped!")));
  }

  a->unit_->Stop();

  uv_unref(reinterpret_cast<uv_handle_t*>(a->in_async_));
  a->Unref();
  a->input_ready_ = false;
  a->output_ready_ = false;
  a->active_ = false;

  return scope.Close(Null());
}


Handle<Value> Audio::Enqueue(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  if (args.Length() < 2 || !args[0]->IsNumber() ||
      !Buffer::HasInstance(args[1])) {
    return scope.Close(ThrowException(String::New(
        "First argument should be a number, second - Buffer!")));
  }

  a->unit_->Put(args[0]->IntegerValue(),
                Buffer::Data(args[1].As<Object>()),
                Buffer::Length(args[1].As<Object>()));

  return scope.Close(Null());
}


Handle<Value> Audio::GetRms(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 1 || !Buffer::HasInstance(args[0])) {
    return scope.Close(ThrowException(String::New(
        "First two arguments should be Buffers!")));
  }

  int16_t* data = reinterpret_cast<int16_t*>(
      Buffer::Data(args[0].As<Object>()));
  size_t len = Buffer::Length(args[0].As<Object>()) / sizeof(int16_t);

  if (len == 0 || (len & sizeof(int16_t)) != 0) {
    return scope.Close(ThrowException(String::New(
        "Buffer has incorrect size!")));
  }

  double rms = 0;
  for (size_t i = 0; i < len; i++) {
    double sample = static_cast<double>(data[i]);
    rms += sample * sample;
  }
  rms /= len;
  rms = sqrt(rms);

  return scope.Close(Number::New(rms));
}


Handle<Value> Audio::ApplyGain(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2 || !Buffer::HasInstance(args[0]) ||
      !args[1]->IsNumber()) {
    return scope.Close(ThrowException(String::New(
        "First two arguments should be Buffers!")));
  }

  int16_t* in = reinterpret_cast<int16_t*>(
      Buffer::Data(args[0].As<Object>()));
  size_t len = Buffer::Length(args[0].As<Object>()) / sizeof(int16_t);

  if (len == 0 || (len & sizeof(int16_t)) != 0) {
    return scope.Close(ThrowException(String::New(
        "Buffer has incorrect size!")));
  }
  double gain = args[1]->NumberValue();

  for (size_t i = 0; i < len; i++) {
    in[i] = in[i] * gain;
  }

  return scope.Close(Null());
}


void Audio::InputAsyncCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = reinterpret_cast<Audio*>(async->data);

  Buffer* buffer;
  for (;;) {
    buffer = a->unit_->Read(a->frame_size_);
    if (buffer == NULL) break;

    if (a->input_ready_ && a->output_ready_) {
      Handle<Value> argv[1] = { buffer->handle_ };
      MakeCallback(a->handle_, ondata_sym, 1, argv);
    }
  }
}


void Audio::InputReadyCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = reinterpret_cast<Audio*>(async->data);

  a->input_ready_ = true;
}


void Audio::OutputReadyCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = reinterpret_cast<Audio*>(async->data);

  a->output_ready_ = true;
}


void Audio::Init(Handle<Object> target) {
  HandleScope scope;

  ondata_sym = Persistent<String>::New(String::NewSymbol("ondata"));

  Local<FunctionTemplate> t = FunctionTemplate::New(Audio::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Audio"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Audio::Start);
  NODE_SET_PROTOTYPE_METHOD(t, "stop", Audio::Stop);
  NODE_SET_PROTOTYPE_METHOD(t, "enqueue", Audio::Enqueue);
  NODE_SET_PROTOTYPE_METHOD(t, "getRms", Audio::GetRms);
  NODE_SET_PROTOTYPE_METHOD(t, "applyGain", Audio::ApplyGain);

  target->Set(String::NewSymbol("Audio"), t->GetFunction());
}

} // namespace audio
} // namespace vock

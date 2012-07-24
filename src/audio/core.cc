#include "core.h"
#include "au.h"
#include "common.h"

#include "node.h"
#include "node_buffer.h"

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

Audio::Audio(Float64 rate) {
  // Setup async callbacks
  if (uv_async_init(uv_default_loop(), &in_async_, InputAsyncCallback)) {
    abort();
  }
  uv_unref(reinterpret_cast<uv_handle_t*>(&in_async_));

  unit_ = new HALUnit(rate, &in_async_);
  if (unit_ == NULL) abort();
}


Audio::~Audio() {
  uv_close(reinterpret_cast<uv_handle_t*>(&in_async_), NULL);
  delete unit_;
}


Handle<Value> Audio::New(const Arguments& args) {
  HandleScope scope;

  // Second argument is in msec
  Audio* a = new Audio(48000.0);
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


void Audio::InputAsyncCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = container_of(async, Audio, in_async_);

  Handle<Value> argv[1] = { a->unit_->Read()->handle_ };
  MakeCallback(a->handle_, ondata_sym, 1, argv);
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

  target->Set(String::NewSymbol("Audio"), t->GetFunction());
}

} // namespace audio
} // namespace vock

#include "binding.h"

#include "node.h"
#include "node_buffer.h"
#include "node_object_wrap.h"
#include "opus.h"

namespace vock {
namespace opus {

using namespace node;
using namespace v8;

#define UNWRAP\
    Opus* o = ObjectWrap::Unwrap<Opus>(args.This());

#define THROW_OPUS_ERROR(err)\
    ThrowException(String::Concat(String::New("Opus error: "),\
                                  String::New(opus_strerror(err))))

Opus::Opus(opus_int32 rate, int channels) : rate_(rate),
                                            channels_(channels),
                                            enc_(NULL),
                                            dec_(NULL) {
  int err;

  enc_ = opus_encoder_create(rate, channels, OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK) {
    THROW_OPUS_ERROR(err);
    return;
  }

  dec_ = opus_decoder_create(rate, channels, &err);
  if (err != OPUS_OK) {
    THROW_OPUS_ERROR(err);
    return;
  }
}


Opus::~Opus() {
  if (enc_ != NULL) opus_encoder_destroy(enc_);
  if (dec_ != NULL) opus_decoder_destroy(dec_);
}


Handle<Value> Opus::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
    return scope.Close(ThrowException(String::New("Incorrect arguments!")));
  }

  Opus* o = new Opus(args[0]->Int32Value(), args[1]->Int32Value());
  o->Wrap(args.Holder());

  return scope.Close(args.This());
}


Handle<Value> Opus::Encode(const Arguments& args) {
  HandleScope scope;

  UNWRAP

  if (args.Length() < 1 || !Buffer::HasInstance(args[0])) {
    return scope.Close(ThrowException(String::New(
            "First argument should be Buffer")));
  }

  char* data = Buffer::Data(args[0].As<Object>());
  size_t len = Buffer::Length(args[0].As<Object>());

  if ((len & sizeof(opus_int16)) != 0) {
    return scope.Close(ThrowException(String::New(
            "Buffer has incorrect size!")));
  }

  unsigned char out[4096];

  opus_int32 ret;

  ret = opus_encode(o->enc_,
                    reinterpret_cast<opus_int16*>(data),
                    len / sizeof(opus_int16),
                    out,
                    sizeof(out));
  if (ret < 0) {
    return scope.Close(THROW_OPUS_ERROR(ret));
  }

  return scope.Close(Buffer::New(reinterpret_cast<char*>(out), ret)->handle_);
}


Handle<Value> Opus::Decode(const Arguments& args) {
  HandleScope scope;

  UNWRAP

  if (args.Length() < 1 || !Buffer::HasInstance(args[0])) {
    return scope.Close(ThrowException(String::New(
            "First argument should be Buffer")));
  }

  char* data = Buffer::Data(args[0].As<Object>());
  size_t len = Buffer::Length(args[0].As<Object>());

  opus_int16 out[10 * 1024];
  opus_int16 ret;

  ret = opus_decode(o->dec_,
                    reinterpret_cast<const unsigned char*>(data),
                    len,
                    out,
                    sizeof(out) / sizeof(out[0]),
                    0);
  if (ret < 0) {
    return scope.Close(THROW_OPUS_ERROR(ret));
  }

  return scope.Close(Buffer::New(reinterpret_cast<char*>(out),
                                 ret * sizeof(out[0]))->handle_);
}


void Opus::Init(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(Opus::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Opus"));

  NODE_SET_PROTOTYPE_METHOD(t, "encode", Opus::Encode);
  NODE_SET_PROTOTYPE_METHOD(t, "decode", Opus::Decode);

  target->Set(String::NewSymbol("Opus"), t->GetFunction());
}


NODE_MODULE(opus, Opus::Init);

} // namespace opus
} // namespace vock

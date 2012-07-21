#include "node_opus.h"
#include "node.h"
#include "node_object_wrap.h"
#include "opus.h"

#include <stdint.h> // int32_t

namespace vock {
namespace opus {

using namespace node;
using namespace v8;

Opus::Opus(int32_t rate) {
  int err;
  enc = opus_encoder_create(rate, 1, OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK) abort();
}


Handle<Value> Opus::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return scope.Close(ThrowException(String::New("Incorrect arguments!")));
  }

  Opus* o = new Opus(args[0]->IntegerValue());
  o->Wrap(args.Holder());

  return scope.Close(args.This());
}


void Opus::Init(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(Opus::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Opus"));

  // NODE_SET_PROTOTYPE_METHOD(t, "init", O::Init);

  target->Set(String::NewSymbol("Opus"), t->GetFunction());
}


NODE_MODULE(opus, Opus::Init);

} // namespace opus
} // namespace vock

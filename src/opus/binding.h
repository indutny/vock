#ifndef _SRC_OPUS_BINDING_H_
#define _SRC_OPUS_BINDING_H_

#include "node.h"
#include "v8.h"
#include "node_object_wrap.h"
#include "opus.h"

namespace vock {
namespace opus {

using namespace node;

class Opus : public ObjectWrap {
 public:
  Opus(opus_int32 rate, int channels);
  ~Opus();

  static void Init(v8::Handle<v8::Object> target);

  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> Encode(const v8::Arguments& args);
  static v8::Handle<v8::Value> Decode(const v8::Arguments& args);

 protected:
  opus_int32 rate_;
  int channels_;
  OpusEncoder* enc_;
  OpusDecoder* dec_;
};

} // namespace opus
} // namespace vock

#endif // _SRC_OPUS_BINDING_H_

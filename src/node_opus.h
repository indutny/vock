#ifndef _SRC_NODE_OPUS_H_
#define _SRC_NODE_OPUS_H_

#include "node.h"
#include "node_object_wrap.h"
#include "opus.h"

namespace vock {
namespace opus {

using namespace node;
using namespace v8;

class Opus : public ObjectWrap {
 public:
  Opus(int32_t rate);

  static void Init(Handle<Object> target);

  static Handle<Value> New(const Arguments& args);

 private:
  OpusEncoder* enc;
  OpusDecoder* dec;
};

} // namespace opus
} // namespace vock

#endif // _SRC_NODE_OPUS_H_

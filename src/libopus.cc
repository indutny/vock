#include "libopus.h"
#include "node.h"
#include "node_object_wrap.h"

namespace libopus {

using namespace node;
using namespace v8;

NODE_MODULE(libopus, Opus::Init);

} // namespace libopus

#ifndef _SRC_AUDIO_UNIT_H_
#define _SRC_AUDIO_UNIT_H_

#include "node.h"
#include "node_buffer.h"
#include "platform/mac.h"
#include "portaudio/pa_ringbuffer.h"

#include <speex/speex_resampler.h>
#include <speex/speex_echo.h>

namespace vock {
namespace audio {

class HALUnit {
 public:
  HALUnit(double rate,
          uv_async_t* in_cb,
          uv_async_t* inready_cb,
          uv_async_t* outready_cb);
  ~HALUnit();

  void Start();
  void Stop();

  size_t GetReadSize();
  node::Buffer* Read(size_t size);
  void Put(char* data, size_t size);

 protected:
  static void InputCallback(void* arg, size_t bytes);
  static void OutputCallback(void* arg, char* out, size_t bytes);

  PlatformUnit in_unit_;
  PlatformUnit out_unit_;

  SpeexResamplerState* resampler_;

  PaUtilRingBuffer in_ring_;
  PaUtilRingBuffer out_ring_;

  // NOTE: Should be a power of two
  int16_t in_ring_buf_[128 * 1024];
  int16_t out_ring_buf_[128 * 1024];

  char in_buff_[10 * 1024];

  uv_async_t* in_cb_;
  uv_async_t* inready_cb_;
  uv_async_t* outready_cb_;
  bool inready_;
  bool outready_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_UNIT_H_

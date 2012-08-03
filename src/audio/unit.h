#ifndef _SRC_AUDIO_UNIT_H_
#define _SRC_AUDIO_UNIT_H_

#include "node.h"
#include "node_buffer.h"
#include "platform/mac.h"
#include "portaudio/pa_ringbuffer.h"

#include <speex/speex_resampler.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

namespace vock {
namespace audio {

class HALUnit {
 public:
  HALUnit(double rate,
          size_t frame_size,
          uv_async_t* in_cb,
          uv_async_t* inready_cb,
          uv_async_t* outready_cb);
  ~HALUnit();

  void Start();
  void Stop();

  size_t GetReadSize();
  node::Buffer* Read(size_t size);
  void Put(int index, char* data, size_t size);

 protected:
  static const int kOutRingCount = 64;
  static const int kRingBufferSize = 64 * 1024;

  static void InputCallback(void* arg, size_t bytes);
  static void OutputCallback(void* arg, char* out, size_t bytes);
  static void* EchoCancelLoop(void* arg);

  size_t frame_size_;

  // Echo canceller thread
  pthread_t canceller_thread_;
  uv_sem_t canceller_sem_;

  PlatformUnit in_unit_;
  PlatformUnit out_unit_;

  SpeexResamplerState* resampler_;
  SpeexEchoState* canceller_;
  SpeexPreprocessState* preprocess_;

  PaUtilRingBuffer cancel_ring_;
  PaUtilRingBuffer in_ring_;
  PaUtilRingBuffer out_rings_[kOutRingCount];
  PaUtilRingBuffer used_ring_;

  // NOTE: Should be a power of two
  int16_t cancel_ring_buf_[kRingBufferSize];
  int16_t in_ring_buf_[kRingBufferSize];
  int16_t out_rings_buf_[kOutRingCount][kRingBufferSize];
  int16_t used_ring_buf_[kRingBufferSize];

  // buffer for Render function
  char mic_buff_[10 * 1024];

  uv_async_t* in_cb_;
  uv_async_t* inready_cb_;
  uv_async_t* outready_cb_;
  bool inready_;
  bool outready_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_UNIT_H_

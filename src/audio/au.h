#ifndef _SRC_AUDIO_AU_H_
#define _SRC_AUDIO_AU_H_

#include "node.h"
#include "node_buffer.h"
#include "ring_buffer.h"

#include <AudioUnit/AudioUnit.h>

namespace vock {
namespace audio {

class HALUnit {
 public:
  enum UnitKind {
    kInputUnit,
    kOutputUnit
  };

  HALUnit(Float64 rate, uv_async_t* input_cb);
  ~HALUnit();

  int Start();
  int Stop();

  size_t GetReadSize();
  node::Buffer* Read();
  void Put(char* data, size_t size);

  const char* err;
  OSStatus err_st;

 protected:
  AudioUnit CreateUnit(UnitKind kind, Float64 rate);

  static OSStatus InputCallback(void* arg,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* ts,
                                UInt32 bus,
                                UInt32 frame_count,
                                AudioBufferList* data);
  static OSStatus OutputCallback(void* arg,
                                 AudioUnitRenderActionFlags* flags,
                                 const AudioTimeStamp* ts,
                                 UInt32 bus,
                                 UInt32 frame_count,
                                 AudioBufferList* data);

  static const int kInputBus = 1;
  static const int kOutputBus = 0;

  AudioUnit in_unit_;
  AudioUnit out_unit_;

  RingBuffer in_ring_;
  RingBuffer out_ring_;
  AudioBufferList* blist_;

  uv_async_t* input_cb_;
  uv_mutex_t in_mutex_;
  uv_mutex_t out_mutex_;
};

} // namespace audio
} // namespace vock

#endif // _SRC_AUDIO_AU_H_

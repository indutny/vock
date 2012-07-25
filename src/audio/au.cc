#include "au.h"
#include "ring_buffer.h"
#include "node.h"
#include "node_buffer.h"

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <speex/speex_resampler.h>
#include <string.h> // memset

#define CHECK(fn, msg)\
    {\
      OSStatus st = fn;\
      if (st != noErr) {\
        err = msg;\
        err_st = st;\
        return NULL;\
      }\
    }

namespace vock {
namespace audio {

using node::Buffer;

HALUnit::HALUnit(Float64 rate,
                 uv_async_t* in_cb,
                 uv_async_t* inready_cb,
                 uv_async_t* outready_cb) : err(NULL),
                                            in_ring_(100 * 1024),
                                            out_ring_(100 * 1024),
                                            in_cb_(in_cb),
                                            inready_cb_(inready_cb),
                                            outready_cb_(outready_cb),
                                            inready_(false),
                                            outready_(false) {
  in_unit_ = CreateUnit(kInputUnit, rate);
  out_unit_ = CreateUnit(kOutputUnit, rate);
  if (uv_mutex_init(&in_mutex_)) abort();
  if (uv_mutex_init(&out_mutex_)) abort();

  if (rate != input_rate_) {
    int err;
    resampler_ = speex_resampler_init(1,
                                      input_rate_,
                                      rate,
                                      SPEEX_RESAMPLER_QUALITY_VOIP,
                                      &err);
    if (resampler_ == NULL) abort();
  } else {
    resampler_ = NULL;
  }
}


HALUnit::~HALUnit() {
  if (in_unit_ != NULL) AudioUnitUninitialize(in_unit_);
  if (out_unit_ != NULL) AudioUnitUninitialize(out_unit_);
  if (resampler_ != NULL) speex_resampler_destroy(resampler_);
  uv_mutex_destroy(&in_mutex_);
  uv_mutex_destroy(&out_mutex_);
}


AudioUnit HALUnit::CreateUnit(UnitKind kind, Float64 rate) {
  UInt32 enable = 1;
  UInt32 disable = 0;
  AudioStreamBasicDescription asbd;
  AudioComponentDescription comp_desc;
  AudioComponent comp;
  AURenderCallbackStruct callback;
  AudioUnit unit;

  // Initialize Component description
  comp_desc.componentType = kAudioUnitType_Output;
  comp_desc.componentSubType = kAudioUnitSubType_HALOutput;
  comp_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  comp_desc.componentFlags = 0;
  comp_desc.componentFlagsMask = 0;

  // Find appropriate component
  comp = AudioComponentFindNext(NULL, &comp_desc);
  if (comp == NULL) {
    err = "Failed to find component by description!";
    err_st = -1;
    return NULL;
  }

  // Instantiate Component (Unit)
  CHECK(AudioComponentInstanceNew(comp, &unit), "Failed to instantiate unit")

  // Attach callbacks
  callback.inputProcRefCon = this;
  if (kind == kInputUnit) {
    callback.inputProc = InputCallback;
    CHECK(AudioUnitSetProperty(unit,
                               kAudioOutputUnitProperty_SetInputCallback,
                               kAudioUnitScope_Global,
                               kInputBus,
                               &callback,
                               sizeof(callback)),
          "Failed to set input callback")
  } else if (kind == kOutputUnit) {
    callback.inputProc = OutputCallback;
    CHECK(AudioUnitSetProperty(unit,
                               kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input,
                               kOutputBus,
                               &callback,
                               sizeof(callback)),
          "Failed to set output callback")
  }

  // Enable IO
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Input,
                             kInputBus,
                             kind == kInputUnit ? &enable : &disable,
                             sizeof(enable)),
        "Failed to enable IO for input")
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Output,
                             kOutputBus,
                             kind == kInputUnit ? &disable : &enable,
                             sizeof(enable)),
        "Failed to enable IO for output")

  // Set formats
  memset(&asbd, 0, sizeof(asbd));

  if (kind == kInputUnit) {
    // Attach device to the input
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);

    AudioObjectPropertyAddress addr = {
      kAudioHardwarePropertyDefaultInputDevice,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMaster
    };

    CHECK(AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                     &addr,
                                     0,
                                     NULL,
                                     &size,
                                     &device),
          "Failed to get default input device")
    CHECK(AudioUnitSetProperty(unit,
                               kAudioOutputUnitProperty_CurrentDevice,
                               kAudioUnitScope_Global,
                               0,
                               &device,
                               size),
          "Failed to set default device")

    // Get device's sample rate
    size = sizeof(input_rate_);
    CHECK(AudioUnitGetProperty(unit,
                               kAudioUnitProperty_SampleRate,
                               kAudioUnitScope_Input,
                               kInputBus,
                               &input_rate_,
                               &size),
          "Failed to set input's format")

    // Use device's native sample rate
    asbd.mSampleRate = input_rate_;
  } else if (kind == kOutputUnit) {
    asbd.mSampleRate = rate;
  }

  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
  asbd.mChannelsPerFrame = 1;
  asbd.mBitsPerChannel = 16;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerPacket = asbd.mBitsPerChannel >> 3;
  asbd.mBytesPerFrame = asbd.mBytesPerPacket * asbd.mChannelsPerFrame;
  asbd.mReserved = 0;

  if (kind == kInputUnit) {
    CHECK(AudioUnitSetProperty(unit,
                               kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Output,
                               kInputBus,
                               &asbd,
                               sizeof(asbd)),
          "Failed to set input's format")
  } else if (kind == kOutputUnit) {
    CHECK(AudioUnitSetProperty(unit,
                               kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Input,
                               kOutputBus,
                               &asbd,
                               sizeof(asbd)),
          "Failed to set output's format")
  }

  if (kind == kInputUnit) {
  }

  CHECK(AudioUnitSetProperty(unit,
                             kAudioUnitProperty_ShouldAllocateBuffer,
                             kAudioUnitScope_Output,
                             kInputBus,
                             &disable,
                             sizeof(disable)),
        "Input: ShouldAllocateBuffer failed")

  // Initialize Unit
  CHECK(AudioUnitInitialize(unit), "Failed to initialize unit")

  return unit;
}

OSStatus HALUnit::InputCallback(void* arg,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* ts,
                                UInt32 bus,
                                UInt32 frame_count,
                                AudioBufferList* data) {
  HALUnit* unit = reinterpret_cast<HALUnit*>(arg);

  // Send "ready" callback
  if (!unit->inready_) {
    unit->inready_ = true;
    uv_async_send(unit->inready_cb_);
  }

  uv_mutex_lock(&unit->in_mutex_);
  AudioBufferList list;
  list.mNumberBuffers = 1;
  list.mBuffers[0].mData = unit->in_ring_.Produce(frame_count * 2);
  list.mBuffers[0].mDataByteSize = frame_count * 2;
  list.mBuffers[0].mNumberChannels = 1;

  OSStatus st = AudioUnitRender(unit->in_unit_,
                                flags, ts, bus, frame_count, &list);
  if (st != noErr) {
    fprintf(stderr, "Failed to read from input buffer (%d)\n", st);
    unit->Stop();
  }
  uv_mutex_unlock(&unit->in_mutex_);

  // Send message to event-loop's thread
  uv_async_send(unit->in_cb_);

  return st;
}


OSStatus HALUnit::OutputCallback(void* arg,
                                 AudioUnitRenderActionFlags* flags,
                                 const AudioTimeStamp* ts,
                                 UInt32 bus,
                                 UInt32 frame_count,
                                 AudioBufferList* data) {
  HALUnit* unit = reinterpret_cast<HALUnit*>(arg);

  // Send "ready" callback
  if (!unit->outready_) {
    unit->outready_ = true;
    uv_async_send(unit->outready_cb_);
  }

  char* buff = reinterpret_cast<char*>(data->mBuffers[0].mData);
  size_t size = frame_count * 2;
  size_t written;

  uv_mutex_lock(&unit->out_mutex_);
  written = unit->out_ring_.Fill(buff, size);
  uv_mutex_unlock(&unit->out_mutex_);

  if (written < size) {
    memset(buff + written, 0, size - written);
  }
  return noErr;
}


int HALUnit::Start() {
  OSStatus st;

  inready_ = false;
  outready_ = false;

  st = AudioOutputUnitStart(in_unit_);
  if (st != noErr) return -1;
  st = AudioOutputUnitStart(out_unit_);
  if (st != noErr) return -1;

  return 0;
}


int HALUnit::Stop() {
  OSStatus st;

  st = AudioOutputUnitStop(in_unit_);
  if (st != noErr) return -1;
  st = AudioOutputUnitStop(out_unit_);
  if (st != noErr) return -1;
  return 0;
}


Buffer* HALUnit::Read(size_t size) {
  uv_mutex_lock(&in_mutex_);

  Buffer* result = NULL;

  size_t read_size = size == 0 ? in_ring_.Size() : size;
  size_t need_size;

  if (resampler_ == NULL) {
    need_size = read_size;
  } else {
    uint32_t num, denum;

    speex_resampler_get_ratio(resampler_, &num, &denum);
    need_size = (read_size * num) / denum;
  }

  if (in_ring_.Size() >= need_size) {
    result = Buffer::New(read_size);

    if (resampler_ == NULL) {
      in_ring_.Fill(Buffer::Data(result), size);
    } else {
      char tmp[10 * 1024];
      spx_uint32_t tmp_samples;
      spx_uint32_t out_samples;
      int r;

      in_ring_.Fill(tmp, need_size);

      // Get size in samples
      tmp_samples = need_size / sizeof(int16_t);
      out_samples = size / sizeof(int16_t);
      r = speex_resampler_process_int(
          resampler_,
          0,
          reinterpret_cast<spx_int16_t*>(tmp),
          &tmp_samples,
          reinterpret_cast<spx_int16_t*>(Buffer::Data(result)),
          &out_samples);

      if (r) abort();
    }
  }

  uv_mutex_unlock(&in_mutex_);
  return result;
}


void HALUnit::Put(char* data, size_t size) {
  uv_mutex_lock(&out_mutex_);

  char* out = out_ring_.Produce(size);
  memcpy(out, data, size);

  uv_mutex_unlock(&out_mutex_);
}

} // namespace audio
} // namespace vock

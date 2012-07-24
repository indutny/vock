#include "au.h"
#include "ring_buffer.h"
#include "node.h"
#include "node_buffer.h"

#include <AudioUnit/AudioUnit.h>
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

HALUnit::HALUnit(Float64 rate, uv_async_t* input_cb) : err(NULL),
                                                       in_ring_(100 * 1024),
                                                       out_ring_(100 * 1024),
                                                       input_cb_(input_cb) {
  in_unit_ = CreateUnit(kInputUnit, rate);
  out_unit_ = CreateUnit(kOutputUnit, rate);
  if (uv_mutex_init(&in_mutex_)) abort();
  if (uv_mutex_init(&out_mutex_)) abort();
}


HALUnit::~HALUnit() {
  if (in_unit_ != NULL) AudioUnitUninitialize(in_unit_);
  if (out_unit_ != NULL) AudioUnitUninitialize(out_unit_);
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

  // Set formats
  memset(&asbd, 0, sizeof(asbd));
  asbd.mSampleRate = rate;
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                      kLinearPCMFormatFlagIsPacked;
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

  // Attach device to the input
  if (kind == kInputUnit) {
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

  uv_mutex_lock(&unit->in_mutex_);
  AudioBufferList list;
  list.mNumberBuffers = 1;
  list.mBuffers[0].mData = unit->in_ring_.Produce(frame_count * 2);
  list.mBuffers[0].mDataByteSize = frame_count * 2;
  list.mBuffers[0].mNumberChannels = 1;

  OSStatus st = AudioUnitRender(unit->in_unit_,
                                flags, ts, bus, frame_count, &list);
  if (st != noErr) abort();
  uv_mutex_unlock(&unit->in_mutex_);

  // Send message to event-loop's thread
  uv_async_send(unit->input_cb_);

  return st;
}


OSStatus HALUnit::OutputCallback(void* arg,
                                 AudioUnitRenderActionFlags* flags,
                                 const AudioTimeStamp* ts,
                                 UInt32 bus,
                                 UInt32 frame_count,
                                 AudioBufferList* data) {
  HALUnit* unit = reinterpret_cast<HALUnit*>(arg);

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


Buffer* HALUnit::Read() {
  uv_mutex_lock(&in_mutex_);

  Buffer* result = Buffer::New(in_ring_.Size());
  in_ring_.Flush(Buffer::Data(result));

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

#include "mac.h"

#include <stdint.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#define CHECK(fn, msg)\
{\
  OSStatus st = fn;\
  if (st != noErr) {\
    fprintf(stderr, "%s (%d)\n", msg, st);\
    abort();\
  }\
}

namespace vock {
namespace audio {

PlatformUnit::PlatformUnit(Kind kind, double rate) : rate_(rate) {
  UInt32 enable = 1;
  UInt32 disable = 0;

  AudioStreamBasicDescription asbd;
  AudioComponentDescription comp_desc;
  AudioComponent comp;
  AURenderCallbackStruct callback;

  // Initialize Component description
  comp_desc.componentType = kAudioUnitType_Output;
  comp_desc.componentSubType = kAudioUnitSubType_HALOutput;
  comp_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  comp_desc.componentFlags = 0;
  comp_desc.componentFlagsMask = 0;

  // Find appropriate component
  comp = AudioComponentFindNext(NULL, &comp_desc);
  if (comp == NULL) {
    fprintf(stderr, "Failed to find component by description!\n");
    abort();
  }

  // Instantiate Component (Unit)
  CHECK(AudioComponentInstanceNew(comp, &unit_), "Failed to instantiate unit")

  // Attach callbacks
  callback.inputProcRefCon = this;
  if (kind == kInputUnit) {
    callback.inputProc = InputCallback;
    CHECK(AudioUnitSetProperty(unit_,
                               kAudioOutputUnitProperty_SetInputCallback,
                               kAudioUnitScope_Global,
                               kInputBus,
                               &callback,
                               sizeof(callback)),
          "Failed to set input callback")
  } else if (kind == kOutputUnit) {
    callback.inputProc = OutputCallback;
    CHECK(AudioUnitSetProperty(unit_,
                               kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input,
                               kOutputBus,
                               &callback,
                               sizeof(callback)),
          "Failed to set output callback")
  }

  // Enable IO
  CHECK(AudioUnitSetProperty(unit_,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Input,
                             kInputBus,
                             kind == kInputUnit ? &enable : &disable,
                             sizeof(enable)),
        "Failed to enable IO for input")
  CHECK(AudioUnitSetProperty(unit_,
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
    CHECK(AudioUnitSetProperty(unit_,
                               kAudioOutputUnitProperty_CurrentDevice,
                               kAudioUnitScope_Global,
                               0,
                               &device,
                               size),
          "Failed to set default device")

    // Get device's sample rate
    size = sizeof(input_rate_);
    CHECK(AudioUnitGetProperty(unit_,
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
    CHECK(AudioUnitSetProperty(unit_,
                               kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Output,
                               kInputBus,
                               &asbd,
                               sizeof(asbd)),
          "Failed to set input's format")
  } else if (kind == kOutputUnit) {
    CHECK(AudioUnitSetProperty(unit_,
                               kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Input,
                               kOutputBus,
                               &asbd,
                               sizeof(asbd)),
          "Failed to set output's format")
  }

  CHECK(AudioUnitSetProperty(unit_,
                             kAudioUnitProperty_ShouldAllocateBuffer,
                             kAudioUnitScope_Output,
                             kInputBus,
                             &disable,
                             sizeof(disable)),
        "Input: ShouldAllocateBuffer failed")

  // Low latency = small buffer
  if (kind == kInputUnit) {
    uint32_t frame_size = input_rate_ / 100;
    AudioUnitSetProperty(unit_,
                         kAudioDevicePropertyBufferFrameSize,
                         kAudioUnitScope_Output,
                         kInputBus,
                         &frame_size,
                         sizeof(frame_size));
  }

  // Initialize Unit
  CHECK(AudioUnitInitialize(unit_), "Failed to initialize unit")

  // Init buffer
  in_list_.mNumberBuffers = 1;
  in_list_.mBuffers[0].mNumberChannels = 1;
  in_list_.mBuffers[0].mData = NULL;
}


PlatformUnit::~PlatformUnit() {
  AudioUnitUninitialize(unit_);
}


void PlatformUnit::Start() {
  CHECK(AudioOutputUnitStart(unit_), "AudioOutputUnitStart failed");
}


void PlatformUnit::Stop() {
  CHECK(AudioOutputUnitStop(unit_), "AudioOutputUnitStop failed");
}


void PlatformUnit::Render(char* out, size_t size) {
  in_list_.mBuffers[0].mData = out;
  in_list_.mBuffers[0].mDataByteSize = size;

  InputCallbackState* s = &input_state_;
  CHECK(AudioUnitRender(unit_, s->flags, s->ts, s->bus, size / 2, &in_list_),
        "AudioUnitRender failed")
}


double PlatformUnit::GetInputSampleRate() {
  return input_rate_;
}


void PlatformUnit::SetInputCallback(InputCallbackFn cb, void* arg) {
  input_cb_ = cb;
  input_arg_ = arg;
}


void PlatformUnit::SetOutputCallback(OutputCallbackFn cb, void* arg) {
  output_cb_ = cb;
  output_arg_ = arg;
}


OSStatus PlatformUnit::InputCallback(void* arg,
                                     AudioUnitRenderActionFlags* flags,
                                     const AudioTimeStamp* ts,
                                     UInt32 bus,
                                     UInt32 frame_count,
                                     AudioBufferList* data) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(arg);

  unit->input_state_.flags = flags;
  unit->input_state_.ts = ts;
  unit->input_state_.bus = bus;
  unit->input_cb_(unit->input_arg_, frame_count * 2);

  return noErr;
}


OSStatus PlatformUnit::OutputCallback(void* arg,
                                      AudioUnitRenderActionFlags* flags,
                                      const AudioTimeStamp* ts,
                                      UInt32 bus,
                                      UInt32 frame_count,
                                      AudioBufferList* data) {
  PlatformUnit* unit = reinterpret_cast<PlatformUnit*>(arg);

  char* buff = reinterpret_cast<char*>(data->mBuffers[0].mData);
  unit->output_cb_(unit->output_arg_, buff, frame_count * 2);

  return noErr;
}

} // namespace audio
} // namespace vock

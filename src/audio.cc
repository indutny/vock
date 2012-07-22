#include "audio.h"
#include "common.h"

#include "node.h"
#include "node_buffer.h"

#include <AudioUnit/AudioUnit.h>
#include <string.h>
#include <unistd.h>

namespace vock {
namespace audio {

using namespace node;
using v8::HandleScope;
using v8::Handle;
using v8::Persistent;
using v8::Local;
using v8::Array;
using v8::String;
using v8::Value;
using v8::Arguments;
using v8::Object;
using v8::Null;
using v8::True;
using v8::False;
using v8::Function;
using v8::FunctionTemplate;
using v8::ThrowException;

static Persistent<String> ondata_sym;

#define CHECK(op, msg)\
    {\
      OSStatus st = 0;\
      if ((st = op)) {\
        char err[1024];\
        snprintf(err, sizeof(err), "%s - %d", msg, st);\
        ThrowException(String::New(err));\
        return NULL;\
      }\
    }

Audio::Audio(Float64 rate) : rate_(rate),
                             in_unit_(NULL),
                             out_unit_(NULL),
                             in_circle_(10 * 1024),
                             out_circle_(10 * 1024) {
  // Initialize description
  memset(&desc_, 0, sizeof(desc_));
  desc_.mSampleRate = rate;
  desc_.mFormatID = kAudioFormatLinearPCM;
  desc_.mFormatFlags = kAudioFormatFlagIsSignedInteger |
                       kAudioFormatFlagIsPacked |
                       kAudioFormatFlagIsNonInterleaved;
  desc_.mFramesPerPacket = 1;
  desc_.mChannelsPerFrame = 1;
  desc_.mBitsPerChannel = 16;
  desc_.mBytesPerPacket = 2;
  desc_.mBytesPerFrame = 2;
  desc_.mReserved = 0;

  // Setup input/output units
  in_unit_ = CreateAudioUnit(true);
  out_unit_ = CreateAudioUnit(false);

  // Setup async callbacks
  if (uv_async_init(uv_default_loop(), &in_async_, InputAsyncCallback)) {
    abort();
  }
}


Audio::~Audio() {
  AudioUnitUninitialize(in_unit_);
  AudioUnitUninitialize(out_unit_);
}


AudioUnit Audio::CreateAudioUnit(bool is_input) {
  UInt32 enable = 1;
  UInt32 disable = 0;

  // Initialize Unit
  AudioComponentDescription au_desc;
  AudioComponent au_component;
  AudioComponentInstance unit;

  au_desc.componentType = kAudioUnitType_Output;
  au_desc.componentSubType = kAudioUnitSubType_HALOutput;
  au_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  au_desc.componentFlags = 0;
  au_desc.componentFlagsMask = 0;

  au_component = AudioComponentFindNext(NULL, &au_desc);
  if (au_component == NULL) {
    ThrowException(String::New("AudioComponentFindNext() failed"));
    return NULL;
  }

  CHECK(AudioComponentInstanceNew(au_component, &unit),
        "AudioComponentInstanceNew() failed")

  CHECK(AudioUnitSetProperty(unit,
                             kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Output,
                             kInputBus,
                             &desc_,
                             sizeof(desc_)),
        "Input: set StreamFormat failed")
  CHECK(AudioUnitSetProperty(unit,
                             kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input,
                             kOutputBus,
                             &desc_,
                             sizeof(desc_)),
        "Output: set StreamFormat failed")
  CHECK(AudioUnitSetProperty(unit,
                             kAudioUnitProperty_ShouldAllocateBuffer,
                             kAudioUnitScope_Output,
                             kInputBus,
                             &disable,
                             sizeof(disable)),
        "Input: ShouldAllocateBuffer failed")

  // Setup callbacks
  AURenderCallbackStruct callback;

  if (is_input) {
    callback.inputProc = InputCallback;
    callback.inputProcRefCon = this;
    CHECK(AudioUnitSetProperty(unit,
                               kAudioOutputUnitProperty_SetInputCallback,
                               kAudioUnitScope_Global,
                               kInputBus,
                               &callback,
                               sizeof(callback)),
          "Input: set callback failed")
  } else {
    callback.inputProc = OutputCallback;
    callback.inputProcRefCon = this;
    CHECK(AudioUnitSetProperty(unit,
                               kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Global,
                               kOutputBus,
                               &callback,
                               sizeof(callback)),
          "Output: set callback failed")
  }

  CHECK(AudioUnitInitialize(unit), "AudioUnitInitialized() failed")

  // Attach input/output
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Input,
                             kInputBus,
                             is_input ? &enable : &disable,
                             sizeof(enable)),
        "Input: EnableIO failed")
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Output,
                             kOutputBus,
                             is_input ? &disable : &enable,
                             sizeof(enable)),
       "Output: EnableIO failed")

  if (is_input) {
    // Set input device
    UInt32 input_size = sizeof(AudioDeviceID);
    AudioDeviceID input;
    CHECK(AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice,
                                   &input_size,
                                   &input),
          "Failed to get input device")
    CHECK(AudioUnitSetProperty(unit,
                               kAudioOutputUnitProperty_CurrentDevice,
                               kAudioUnitScope_Global,
                               kOutputBus,
                               &input,
                               sizeof(input)),
          "Failed to set input device")
  }

  return unit;
}


Handle<Value> Audio::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return scope.Close(ThrowException(String::New(
        "First argument should be number")));
  }

  // Second argument is in msec
  Audio* a = new Audio(args[0]->NumberValue());
  a->Wrap(args.Holder());

  return scope.Close(args.This());
}


Handle<Value> Audio::Start(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  OSStatus st;

  st = AudioOutputUnitStart(a->in_unit_);
  st = AudioOutputUnitStart(a->out_unit_);
  if (st) {
    return scope.Close(ThrowException(String::New(
        "Failed to start unit!")));
  }

  return scope.Close(Null());
}


Handle<Value> Audio::Stop(const Arguments& args) {
  HandleScope scope;
  Audio* a = ObjectWrap::Unwrap<Audio>(args.This());

  OSStatus st;

  st = AudioOutputUnitStop(a->in_unit_);
  st = AudioOutputUnitStop(a->out_unit_);
  if (st) {
    return scope.Close(ThrowException(String::New(
        "Failed to stop unit!")));
  }

  return scope.Close(Null());
}


OSStatus Audio::InputCallback(void* arg,
                              AudioUnitRenderActionFlags* flags,
                              const AudioTimeStamp* ts,
                              UInt32 bus,
                              UInt32 frame_count,
                              AudioBufferList* data) {
  Audio* a = reinterpret_cast<Audio*>(arg);

  // Setup buffer list
  AudioBufferList list;
  list.mNumberBuffers = 1;
  list.mBuffers[0].mNumberChannels = 1;
  list.mBuffers[0].mDataByteSize = a->rate_ * a->desc_.mBytesPerFrame; // 1sec
  list.mBuffers[0].mData = NULL;
  // Write received data to buffer list
  if (AudioUnitRender(a->in_unit_,
                      flags,
                      ts,
                      bus,
                      frame_count,
                      &list)) {
    abort();
  }

  uv_async_send(&a->in_async_);

  return 0;
}


void Audio::InputAsyncCallback(uv_async_t* async, int status) {
  HandleScope scope;
  Audio* a = container_of(async, Audio, in_async_);

  Handle<Value> argv[0] = {};
  MakeCallback(a->handle_, ondata_sym, 0, argv);
}


OSStatus Audio::OutputCallback(void* arg,
                               AudioUnitRenderActionFlags* flags,
                               const AudioTimeStamp* ts,
                               UInt32 bus,
                               UInt32 frame_count,
                               AudioBufferList* data) {
  for (UInt32 i = 0; i < data->mNumberBuffers; i++) {
    memset(data->mBuffers[i].mData, 255, data->mBuffers[i].mDataByteSize);
  }
  return 0;
}


void Audio::Init(Handle<Object> target) {
  HandleScope scope;

  ondata_sym = Persistent<String>::New(String::NewSymbol("ondata"));

  Local<FunctionTemplate> t = FunctionTemplate::New(Audio::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Audio"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Audio::Start);
  NODE_SET_PROTOTYPE_METHOD(t, "stop", Audio::Stop);

  target->Set(String::NewSymbol("Audio"), t->GetFunction());
}

} // namespace audio
} // namespace vock

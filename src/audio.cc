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

static Persistent<String> onincoming;
static Persistent<String> onoutcoming;

#define CHECK(op, msg)\
    {\
      OSStatus st;\
      if ((st = op)) {\
        char err[1024];\
        snprintf(err, sizeof(err), "%s - %d", msg, st);\
        ThrowException(String::New(err));\
        return;\
      }\
    }

Audio::Audio(Float64 rate) : unit_(NULL) {
  UInt32 enable = 1;
  UInt32 disable = 0;

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
    return;
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

  callback.inputProc = InputCallback;
  callback.inputProcRefCon = this;
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_SetInputCallback,
                             kAudioUnitScope_Global,
                             kInputBus,
                             &callback,
                             sizeof(callback)),
        "Input: set callback failed")

  callback.inputProc = OutputCallback;
  callback.inputProcRefCon = this;
  CHECK(AudioUnitSetProperty(unit,
                             kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Global,
                             kOutputBus,
                             &callback,
                             sizeof(callback)),
        "Output: set callback failed")

  CHECK(AudioUnitInitialize(unit), "AudioUnitInitialized() failed")

  // Attach input/output
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Input,
                             kInputBus,
                             &enable,
                             sizeof(enable)),
        "Input: EnableIO failed")
  CHECK(AudioUnitSetProperty(unit,
                             kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Output,
                             kOutputBus,
                             &enable,
                             sizeof(enable)),
       "Output: EnableIO failed")

  unit_ = unit;
}


Audio::~Audio() {
  AudioUnitUninitialize(unit_);
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

  st = AudioOutputUnitStart(a->unit_);
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

  st = AudioOutputUnitStop(a->unit_);
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
  fprintf(stdout, "input\n");
  return 0;
}


OSStatus Audio::OutputCallback(void* arg,
                               AudioUnitRenderActionFlags* flags,
                               const AudioTimeStamp* ts,
                               UInt32 bus,
                               UInt32 frame_count,
                               AudioBufferList* data) {
  fprintf(stdout, "output\n");
  for (UInt32 i = 0; i < data->mNumberBuffers; i++) {
    memset(data->mBuffers[i].mData, 255, data->mBuffers[i].mDataByteSize);
  }
  return 0;
}


void Audio::Init(Handle<Object> target) {
  HandleScope scope;

  onincoming = Persistent<String>::New(String::NewSymbol("onincoming"));
  onoutcoming = Persistent<String>::New(String::NewSymbol("onoutcoming"));

  Local<FunctionTemplate> t = FunctionTemplate::New(Audio::New);

  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(String::NewSymbol("Audio"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Audio::Start);
  NODE_SET_PROTOTYPE_METHOD(t, "stop", Audio::Stop);

  target->Set(String::NewSymbol("Audio"), t->GetFunction());
}

} // namespace audio
} // namespace vock

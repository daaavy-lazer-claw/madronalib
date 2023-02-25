// VST3 example code for madronalib
// (c) 2020, Madrona Labs LLC, all rights reserved
// see LICENSE.txt for details

#include "pluginProcessor.h"
#include "pluginController.h"

#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstpresetkeys.h"  // for use of IStreamAttributes
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cmath>
#include <cstdlib>
#include <math.h>
#include <iostream>

namespace Steinberg {
namespace Vst {
namespace llllpluginnamellll {

FUID PluginProcessor::uid(0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB);

PluginProcessor::PluginProcessor()
  : SignalProcessor(kInputChannels, kOutputChannels)
{
  // register its editor class(the same than used in againentry.cpp)
	setControllerClass(PluginController::uid);
}

PluginProcessor::~PluginProcessor()
{
}

tresult PLUGIN_API PluginProcessor::initialize(FUnknown* context)
{
  //---always initialize the parent-------
  tresult result = AudioEffect::initialize(context);

  if(result != kResultOk)
  {
    return result;
  }
  
  addEventInput (STR16 ("Events In"), 1);
  
  addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
  
  return kResultOk;
}

tresult PLUGIN_API PluginProcessor::terminate()
{
  return AudioEffect::terminate();
}

tresult PLUGIN_API PluginProcessor::setActive(TBool state)
{
  return AudioEffect::setActive(state);
}

tresult PLUGIN_API PluginProcessor::process(ProcessData& data)
{
  // process parameter changes and events, generating input signals.
  processParameterChanges(data.inputParameterChanges);
  processEvents(data.inputEvents);
  
  processSignals(data);
  return kResultTrue;
}

tresult PLUGIN_API PluginProcessor::setState(IBStream* state)
{
  // called when we load a preset, the model has to be reloaded
  
  IBStreamer streamer(state, kLittleEndian);
  
  int32 savedBypass = 0;
  if(streamer.readInt32(savedBypass) == false)
    return kResultFalse;
  
  float savedCutoff = 0.f;
  if(streamer.readFloat(savedCutoff) == false)
    return kResultFalse;
  
  bBypass = savedBypass > 0;
  fCutoff = savedCutoff;
  
  // Example of using the IStreamAttributes interface
  FUnknownPtr<IStreamAttributes> stream(state);
  if(stream)
  {
    IAttributeList* list = stream->getAttributes();
    if(list)
    {
      // get the current type(project/Default..) of this state
      String128 string = {0};
      if(list->getString(PresetAttributes::kStateType, string, 128 * sizeof(TChar)) ==
          kResultTrue)
      {
        UString128 tmp(string);
        char ascii[128];
        tmp.toAscii(ascii, 128);
        if(!strncmp(ascii, StateType::kProject, strlen(StateType::kProject)))
        {
          // we are in project loading context...
        }
      }
      
      // get the full file path of this state
      TChar fullPath[1024];
      memset(fullPath, 0, 1024 * sizeof(TChar));
      if(list->getString(PresetAttributes::kFilePathStringType, fullPath,
                           1024 * sizeof(TChar)) == kResultTrue)
      {
        // here we have the full path ...
      }
    }
  }
  
  return kResultOk;
}

tresult PLUGIN_API PluginProcessor::getState(IBStream* state)
{
  // here we need to save the model
  IBStreamer streamer(state, kLittleEndian);
  streamer.writeInt32(bBypass ? 1 : 0);
  streamer.writeFloat(fCutoff);
  return kResultOk;
}

tresult PLUGIN_API PluginProcessor::setupProcessing(ProcessSetup& newSetup)
{
  // called before the process call, always in a disabled state(not active)
  // here we could keep a trace of the processing mode(offline,...) for example.
  // currentProcessMode = newSetup.processMode;
  
  _sampleRate = newSetup.sampleRate;
  
  // setup synth inputs
  _synthInput = make_unique< EventsToSignals >(_sampleRate);
  _synthInput->setPolyphony(kMaxVoices);
  
  float glideTimeInSeconds{0.01f};
  _cutoffGlide.setGlideTimeInSamples(_sampleRate*glideTimeInSeconds);
  _cutoffGlide.setValue(0.5f);
  
  return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API PluginProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts)
{
  if(numIns)
  {
    if(SpeakerArr::getChannelCount(inputs[0]) != 0)
      return kResultFalse;
  }
  if(numOuts)
  {
    if(SpeakerArr::getChannelCount(outputs[0]) != 2)
      return kResultFalse;
  }
  return kResultTrue;
}

tresult PLUGIN_API PluginProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
  if(symbolicSampleSize == kSample32)
    return kResultTrue;
  
  // we support double processing
  if(symbolicSampleSize == kSample64)
    return kResultTrue;
  
  return kResultFalse;
}

tresult PLUGIN_API PluginProcessor::notify(IMessage* message)
{
  // we could respond to messages here
  return AudioEffect::notify(message);
}

// --------------------------------------------------------------------------------
// private implementation

bool PluginProcessor::processParameterChanges(IParameterChanges* changes)
{
  if(changes)
  {
    int32 numParamsChanged = changes->getParameterCount();
    
    // for each parameter that changes in this audio block:
    for(int32 i = 0; i < numParamsChanged; ++i)
    {
      if(auto* paramQueue = changes->getParameterData(i))
      {
        ParamValue value;
        int sampleOffset;
        int numPoints = paramQueue->getPointCount();
        int id = paramQueue->getParameterId();
        
        // std::cout << "param ID " << id << " : \n";
        
        if(id < (int)kNumPluginParameters)
        {
          // handle plugin parameters
          switch(id)
          {
            case kBypassId:
            {
              if(paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
              {
                bBypass = (value > 0.5f);
              }
              break;
            }
            case kCutoffId:
            {
              if(paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
              {
                std::cout << "    cutoff " << " = " << value << "\n";
                fCutoff = (float)value;
              }
              break;
            }
          }
        }
        else if(id < kVST3MIDITotalParams)
        {
          int midiID = id - kNumPluginParameters;
          int channel = midiID/kVST3MIDIParamsPerChannel;
          int paramIdx = midiID - channel*kVST3MIDIParamsPerChannel;
          
          //std::cout << "channel: " << channel << ", index: " << paramIdx << "\n";
          switch(paramIdx)
          {
            // special params: aftertouch, pitch bend
            case Steinberg::Vst::kAfterTouch:
            {
              if(paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
              {
                _synthInput->addEvent(ml::EventsToSignals::Event{ml::EventsToSignals::Event::kNotePressure, channel, 0, sampleOffset,
                  float(value), 0, 0, 0});
              }
              
              break;
            }
            case Steinberg::Vst::kPitchBend:
            {
              if(paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
              {
                float bendValue = (value - 0.5f)*2.0f;
                _synthInput->addEvent(ml::EventsToSignals::Event{ml::EventsToSignals::Event::kPitchWheel, channel, 0, sampleOffset,
                  bendValue, 0, 0, 0});
              }
              break;
            }
              
            // other params: send Controller # in event
            default:
            {
              if(paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
              {
                  _synthInput->addEvent(ml::EventsToSignals::Event{ml::EventsToSignals::Event::kController, channel, 0, sampleOffset,
                    float(value), float(paramIdx), 0, 0});
              }
              break;
              break;
            }
          }
        }
      }
    }
  }
  return false; 
}

void PluginProcessor::processEvents (IEventList* events)
{
  if(!_synthInput.get()) return;
  
  // send all VST events to our EventsToSignals
  if (events)
  {
    int32 npos=0;
    int32 count = events->getEventCount ();
    for (int32 i = 0; i < count; i++)
    {
      Steinberg::Vst::Event e;
      events->getEvent(i, e);
      
      int channel{1};
      int creatorID{e.noteOn.pitch};
      int time{e.sampleOffset};
      float pitch{e.noteOn.pitch + 0.f};
      
      switch (e.type)
      {
        case Event::kNoteOnEvent:
        {
          _synthInput->addEvent(ml::EventsToSignals::Event{ml::EventsToSignals::Event::kNoteOn, channel, creatorID, time,
            pitch, e.noteOn.velocity});
          break;
        }
        case Event::kNoteOffEvent:
        {
          _synthInput->addEvent(ml::EventsToSignals::Event{ml::EventsToSignals::Event::kNoteOff, channel, creatorID, time,
            pitch, 0});
          break;
        }
        default:
          continue;
      }
    }
  }
}

void setParameter (ParamID index, ParamValue newValue, int32 sampleOffset) {
}


using processFnType = std::function< void(MainInputs, MainOutputs, void *) >;
void PluginProcessorProcessVectorFn(MainInputs ins, MainOutputs outs, void* state)
{
  PluginProcessor* pProc = static_cast< PluginProcessor* >(state);
  return pProc->synthProcessVector(ins, outs);
} ;


// ProcessSignals() adapts the VST process() call with its arbitrary frame size to madronalib's
// fixed vector size processing.

void PluginProcessor::processSignals(ProcessData& data)
{
  if(data.numOutputs == 0)
  {
    // nothing to do
    return;
  }
  
  // our outputs are not silent
  data.outputs[0].silenceFlags = 0;
  
  // cast I/O pointers: necessary ugliness due to VST's use of void*
  void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
  float** outputs = reinterpret_cast<float**>(out);

  assert(processSetup.symbolicSampleSize == kSample32);
  
  // mark our outputs has not silent
  data.outputs[0].silenceFlags = 0;
  
  // run buffered processing
  processBuffer.process(nullptr, outputs, data.numSamples, PluginProcessorProcessVectorFn, this);
}

// the main process routine! does everything.
//
void PluginProcessor::synthProcessVector(MainInputs inputs, MainOutputs outputs)
{
  if(_synthInput)
  {
    _synthInput->process();
  }
  
  // clear outs
  outputs[0] = 0.f;
  outputs[1] = 0.f;

  if(!bBypass)
  {
    auto cutoffSig = _cutoffGlide(fCutoff);
    
    // sum voices to outputs
    for(int v=0; v < _synthInput->getPolyphony(); ++v)
    {
      auto& allocatorVoice = (_synthInput->voices)[v];
      auto& pitchSignal = allocatorVoice.outputs.row(EventsToSignals::kPitch);
      auto& pitchBendSignal = allocatorVoice.outputs.row(EventsToSignals::kPitchBend);
      auto& velSignal = allocatorVoice.outputs.row(EventsToSignals::kVelocity);
      
      auto voiceOutput = _voices[v].processVector(pitchSignal, velSignal, pitchBendSignal, _sampleRate);
      
      outputs[0] += voiceOutput.row(0);
      outputs[1] += voiceOutput.row(1);
      
      
      // TEMP
      // std::cout << "c" << cutoffSig[0] << " \n";
    }
  }
  
  _debugCounter += kFloatsPerDSPVector;
  if(_debugCounter > _sampleRate)
  {
    _debugCounter -= _sampleRate;
    debugStuff();
  }
}

DSPVectorArray< 2 > PluginProcessor::Voice::processVector(DSPVector pitch, DSPVector vel, DSPVector pitchBend, float sr)
{
  // convert 1/oct pitch to frequency
  constexpr float kFundamentalPitch = 440.f;
  
  constexpr float kBendSemitones = 7;
  constexpr float kBendRange = 1.0f*kBendSemitones/12;
  
  // it's up to the Process how to combine pitch with pitch bend.
  DSPVector fundamental(kFundamentalPitch);
  DSPVector freq = exp2Approx(pitch + pitchBend*kBendRange)*fundamental;
  DSPVector invSampleRate(1.0f / sr);
  
  auto oscOut = osc1(freq*invSampleRate)*vel;
  return concatRows(oscOut, oscOut);
}

void PluginProcessor::debugStuff()
{
  size_t p = _synthInput->getPolyphony();
  for(int v = 0; v < p; ++v )
  {
    auto& voice = (_synthInput->voices)[v];
    DSPVector vVel = voice.outputs.row(EventsToSignals::kVelocity);
    DSPVector vPitch = voice.outputs.row(EventsToSignals::kPitch);
    DSPVector vPitchBend = voice.outputs.row(EventsToSignals::kPitchBend);
    DSPVector vVoice = voice.outputs.row(EventsToSignals::kVoice);
    DSPVector vMod = voice.outputs.row(EventsToSignals::kMod);
    DSPVector vX = voice.outputs.row(EventsToSignals::kX);
    DSPVector vY = voice.outputs.row(EventsToSignals::kY);
    DSPVector vZ = voice.outputs.row(EventsToSignals::kZ);
    DSPVector vTime = voice.outputs.row(EventsToSignals::kElapsedTime);
    float vel = vVel[0];
    float pitch = vPitch[0];
    float bend = vPitchBend[0];
    float vox = vVoice[0];
    float mod = vMod[0];
    float x = vX[0];
    float y = vY[0];
    float z = vZ[0];
    float time = vTime[0];

    
    if(vel > 0)
    {
      std::cout << "voice " << v << " : [" << vel << ", " << pitch << ", " << bend << ", " << vox << ", " << mod << ", " << mod << "]\n";
      std::cout << "          [" << x << ", " << y << ", " << z << ", " << time << "]\n";
    }
  }
}
 
}}} // namespaces

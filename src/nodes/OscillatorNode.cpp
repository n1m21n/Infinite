#include "OscillatorNode.h"
#include "WavetableSynthCore.h"

OscillatorNode::OscillatorNode()
{
   Wavetable::EnsureBuilt();

   engine.on = true;
   engine.table = 0;
   engine.volume = 0.8f;
   engine.warpMode = SynthModes::kWarpSync;
   engine.warpAmount = 1.0f;
   engine.warpRatio = 1.0f;
   engine.pitchAmount = 0.0f;
   engine.filterAmount = 0.0f;
   engine.stereoWidth = 0.4f;
   engine.phaseRandomize = 0.0f;
}

OscillatorNode::~OscillatorNode() = default;

void OscillatorNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();

   WavetableSynthParams params;
   params.engines[0] = engine;
   params.engines[0].on = true;
   params.engines[0].table = 0; // Table 0 has Sine, Triangle, Saw, Square
   switch (waveform)
   {
      case kSine:     params.engines[0].position = 0.0f; break;
      case kTriangle: params.engines[0].position = 1.0f / 3.0f; break;
      case kSaw:      params.engines[0].position = 2.0f / 3.0f; break;
      case kSquare:   params.engines[0].position = 1.0f; break;
      default:        params.engines[0].position = 2.0f / 3.0f; break;
   }
   params.engines[0].warpMode = SynthModes::kWarpSync;
   params.engines[0].warpAmount = 1.0f;
   params.engines[0].pitchAmount = 0.0f;
   params.engines[0].filterAmount = 0.0f;
   params.engines[1].on = false;
   params.mix = 0.0f;
   params.volume = 1.0f;
   params.frequency = frequency;
   params.glide = glide;
   params.pitchBend = pitchBend;

   mAudioNode->PushParams(params);
}

AudioNode* OscillatorNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();
   return mAudioNode.get();
}

double OscillatorNode::DebugMailboxSampleRate() const
{
   return mAudioNode ? mAudioNode->DebugMailboxSampleRate() : 0.0;
}

int OscillatorNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int OscillatorNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void OscillatorNode::VisitParams(ParamVisitor& v)
{
   v.Float("frequency", frequency);
   v.Float("glide", glide);
   v.Float("pitchBend", pitchBend);
   v.Int("waveform", waveform);
   v.Float("fine", engine.fine);
   v.Int("octave", engine.octave);
   v.Int("semi", engine.semi);
   v.Float("volume", engine.volume);
   v.Float("phase", engine.phase);
   v.Int("unison", engine.unison);
   v.Float("detune", engine.detune);
   v.Int("filterType", engine.filterType);
   v.Float("cutoff", engine.cutoff);
   v.Float("resonance", engine.resonance);
   v.Float("sync", engine.warpRatio);
   v.Float("pan", engine.pan);
   v.Float("ampAttack", engine.ampAttack);
   v.Float("ampDecay", engine.ampDecay);
   v.Float("ampSustain", engine.ampSustain);
   v.Float("ampRelease", engine.ampRelease);
}

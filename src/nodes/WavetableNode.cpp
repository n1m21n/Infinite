#include "WavetableNode.h"
#include "WavetableSynthCore.h"

// -------------------------------------------------------------- main thread
WavetableNode::WavetableNode()
{
   // Main-thread build of the shared bank, before any AudioNode of ours can
   // exist - Wavetable::Frame allocates nothing and must never see an unbuilt
   // bank from the audio thread.
   Wavetable::EnsureBuilt();

   // Engine B is off by default: a two-engine node that starts with both
   // engines summing is 6 dB hotter than the single-oscillator node it
   // replaces, and every patch would open needing the mix pulled down.
   engines[1].on = false;
   engines[1].table = 3;      // Formant - audibly different from A's Basic Shapes
   engines[1].octave = -1;
   engines[1].position = 0.4f;
}

WavetableNode::~WavetableNode() = default;

void WavetableNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();

   WavetableSynthParams params;
   for (int e = 0; e < kEngines; e++)
      params.engines[e] = engines[e];
   params.mix = mix;
   params.volume = volume;
   params.frequency = frequency;
   params.glide = glide;
   params.pitchBend = pitchBend;

   mAudioNode->PushParams(params);
}

AudioNode* WavetableNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();
   return mAudioNode.get();
}

double WavetableNode::DebugMailboxSampleRate() const
{
   return mAudioNode ? mAudioNode->DebugMailboxSampleRate() : 0.0;
}

int WavetableNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int WavetableNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void WavetableNode::VisitParams(ParamVisitor& v)
{
   v.Float("mix", mix);
   v.Float("volume", volume);
   v.Float("frequency", frequency);
   v.Float("glide", glide);
   v.Float("pitchBend", pitchBend);

   // Per-engine names are prefixed rather than indexed generically so a saved
   // patch stays readable and a future third engine can't silently renumber
   // the existing two.
   static const char* const kPrefix[kEngines] = { "a", "b" };
   for (int e = 0; e < kEngines; e++)
   {
      WavetableEngine& s = engines[e];
      const std::string p = std::string(kPrefix[e]) + ".";
      v.Bool((p + "on").c_str(), s.on);
      v.Int((p + "table").c_str(), s.table);
      v.Float((p + "position").c_str(), s.position);
      v.Float((p + "volume").c_str(), s.volume);
      v.Float((p + "pan").c_str(), s.pan);
      v.Int((p + "unison").c_str(), s.unison);
      v.Float((p + "detune").c_str(), s.detune);
      v.Float((p + "stereoWidth").c_str(), s.stereoWidth);
      v.Int((p + "octave").c_str(), s.octave);
      v.Int((p + "semi").c_str(), s.semi);
      v.Float((p + "fine").c_str(), s.fine);
      v.Float((p + "phase").c_str(), s.phase);
      v.Float((p + "phaseRandomize").c_str(), s.phaseRandomize);
      v.Int((p + "warpMode").c_str(), s.warpMode);
      v.Float((p + "warpAmount").c_str(), s.warpAmount);
      v.Float((p + "warpRatio").c_str(), s.warpRatio);
      v.Int((p + "filterType").c_str(), s.filterType);
      v.Float((p + "cutoff").c_str(), s.cutoff);
      v.Float((p + "resonance").c_str(), s.resonance);
      v.Float((p + "ampAttack").c_str(), s.ampAttack);
      v.Float((p + "ampDecay").c_str(), s.ampDecay);
      v.Float((p + "ampSustain").c_str(), s.ampSustain);
      v.Float((p + "ampRelease").c_str(), s.ampRelease);
      v.Float((p + "pitchAmount").c_str(), s.pitchAmount);
      v.Float((p + "pitchAttack").c_str(), s.pitchAttack);
      v.Float((p + "pitchDecay").c_str(), s.pitchDecay);
      v.Float((p + "pitchSustain").c_str(), s.pitchSustain);
      v.Float((p + "pitchRelease").c_str(), s.pitchRelease);
      v.Float((p + "filterAmount").c_str(), s.filterAmount);
      v.Float((p + "filterAttack").c_str(), s.filterAttack);
      v.Float((p + "filterDecay").c_str(), s.filterDecay);
      v.Float((p + "filterSustain").c_str(), s.filterSustain);
      v.Float((p + "filterRelease").c_str(), s.filterRelease);
   }
}

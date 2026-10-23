#include "WavetableNode.h"
#include "WavetableSynthCore.h"

#include <algorithm>
#include <utility>

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
   params.fmDepth = fmDepth;
   params.fmMode = fmMode;

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

int WavetableNode::DebugFmMode() const
{
   return mAudioNode ? mAudioNode->DebugFmMode() : 0;
}

int WavetableNode::VoiceParamFor(const float* member) const
{
   using namespace WavetableSynthCore;
   if (member == &mix)
      return kMix;
   if (member == &fmDepth)
      return kFmDepth;
   for (int e = 0; e < kEngines; e++)
   {
      const WavetableEngine& s = engines[e];
      const std::pair<const float*, int> kVoiceParams[] = {
         { &s.position, kEngPosition }, { &s.volume, kEngVolume }, { &s.pan, kEngPan },
         { &s.detune, kEngDetune }, { &s.stereoWidth, kEngStereoWidth }, { &s.fine, kEngFine },
         { &s.warpAmount, kEngWarpAmount }, { &s.warpRatio, kEngWarpRatio },
         { &s.cutoff, kEngCutoff }, { &s.resonance, kEngResonance },
      };
      for (const auto& [ptr, id] : kVoiceParams)
         if (ptr == member)
            return EngineParamId(e, id);
   }
   return -1;
}

void WavetableNode::PublishVoicePlan(const VoiceMod::Plan& plan)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWavetableNode>();
   mAudioNode->PublishVoicePlan(plan);
}

float WavetableNode::DebugVoiceSource(int voice, int source) const
{
   return mAudioNode ? mAudioNode->DebugVoiceSource(voice, source) : 0.0f;
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
   v.Float("fmDepth", fmDepth);
   v.Int("fmMode", fmMode);

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
      // Loaded unchecked; same range as the dropdowns (body index, note + octave * 12).
      s.octave = std::clamp(s.octave, -4, 4);
      s.semi = std::clamp(s.semi, -12, 12);
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
      s.ClampToRange();
   }
   mix = std::clamp(mix, 0.0f, 1.0f);
   volume = std::clamp(volume, 0.0f, 1.0f);
   frequency = std::clamp(frequency, 20.0f, 8000.0f);
   glide = std::clamp(glide, 0.0f, 2.0f);
   pitchBend = std::clamp(pitchBend, -2.0f, 2.0f);
   fmDepth = std::clamp(fmDepth, 0.0f, 2.0f);
}

void WavetableEngine::ClampToRange()
{
   constexpr float kMaxEnvMs = 4000.0f;
   position = std::clamp(position, 0.0f, 1.0f);
   volume = std::clamp(volume, 0.0f, 1.0f);
   pan = std::clamp(pan, -1.0f, 1.0f);
   detune = std::clamp(detune, 0.0f, 100.0f);
   stereoWidth = std::clamp(stereoWidth, 0.0f, 1.0f);
   fine = std::clamp(fine, -50.0f, 50.0f);
   phase = std::clamp(phase, 0.0f, 1.0f);
   phaseRandomize = std::clamp(phaseRandomize, 0.0f, 1.0f);
   warpAmount = std::clamp(warpAmount, 0.0f, 1.0f);
   warpRatio = std::clamp(warpRatio, 0.25f, 16.0f);
   cutoff = std::clamp(cutoff, 20.0f, 18000.0f);
   resonance = std::clamp(resonance, 0.0f, 1.0f);
   ampAttack = std::clamp(ampAttack, 0.0f, kMaxEnvMs);
   ampDecay = std::clamp(ampDecay, 0.0f, kMaxEnvMs);
   ampSustain = std::clamp(ampSustain, 0.0f, 1.0f);
   ampRelease = std::clamp(ampRelease, 0.0f, kMaxEnvMs);
   pitchAmount = std::clamp(pitchAmount, -48.0f, 48.0f);
   pitchAttack = std::clamp(pitchAttack, 0.0f, kMaxEnvMs);
   pitchDecay = std::clamp(pitchDecay, 0.0f, kMaxEnvMs);
   pitchSustain = std::clamp(pitchSustain, 0.0f, 1.0f);
   pitchRelease = std::clamp(pitchRelease, 0.0f, kMaxEnvMs);
   filterAmount = std::clamp(filterAmount, -8.0f, 8.0f);
   filterAttack = std::clamp(filterAttack, 0.0f, kMaxEnvMs);
   filterDecay = std::clamp(filterDecay, 0.0f, kMaxEnvMs);
   filterSustain = std::clamp(filterSustain, 0.0f, 1.0f);
   filterRelease = std::clamp(filterRelease, 0.0f, kMaxEnvMs);
}

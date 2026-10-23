#include "AnalogNode.h"
#include "AnalogSynthCore.h"

#include <algorithm>
#include <utility>

AnalogNode::AnalogNode() = default;
AnalogNode::~AnalogNode() = default;

void AnalogNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioAnalogNode>();

   AnalogSynthParams params;
   params.volume = volume;
   params.freq = freq;
   params.glide = glide;
   params.pitchBend = pitchBend;
   params.fine1 = fine1;
   params.semi1 = semi1;
   params.oct1 = oct1;
   params.fine2 = fine2;
   params.semi2 = semi2;
   params.oct2 = oct2;
   params.pw1 = pw1;
   params.osc1Vol = osc1Vol;
   params.osc2Vol = osc2Vol;
   params.voices = voices;
   params.spread = spread;
   params.fm = fm;
   params.detune = detune;
   params.oscMix = oscMix;
   params.sub = sub;
   params.noise = noise;
   params.cutoff = cutoff;
   params.resonance = resonance;
   params.drive = drive;
   params.keyTrack = keyTrack;
   params.attack = attack;
   params.decay = decay;
   params.sustain = sustain;
   params.release = release;

   params.wave1 = wave1;
   params.wave2 = wave2;
   params.filterType = filterType;
   params.sync = sync;
   params.analog = analog;

   mAudioNode->PushParams(params);
}

AudioNode* AnalogNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioAnalogNode>();
   return mAudioNode.get();
}

int AnalogNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int AnalogNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

double AnalogNode::DebugMailboxSampleRate() const
{
   return mAudioNode ? mAudioNode->DebugMailboxSampleRate() : 0.0;
}

int AnalogNode::VoiceParamFor(const float* member) const
{
   using namespace AnalogSynthCore;
   const std::pair<const float*, int> kVoiceParams[] = {
      { &fine1, kFine1 }, { &semi1, kSemi1 }, { &oct1, kOct1 }, { &fine2, kFine2 }, { &semi2, kSemi2 },
      { &oct2, kOct2 }, { &pw1, kPw1 }, { &fm, kFm }, { &oscMix, kOscMix }, { &osc1Vol, kOsc1Vol },
      { &osc2Vol, kOsc2Vol }, { &sub, kSub }, { &noise, kNoise }, { &cutoff, kCutoff },
      { &resonance, kResonance }, { &drive, kDrive }, { &keyTrack, kKeyTrack }, { &detune, kDetune },
      { &attack, kAttack }, { &decay, kDecay }, { &sustain, kSustain }, { &release, kRelease },
   };
   for (const auto& [ptr, id] : kVoiceParams)
      if (ptr == member)
         return id;
   return -1;
}

void AnalogNode::PublishVoicePlan(const VoiceMod::Plan& plan)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioAnalogNode>();
   mAudioNode->PublishVoicePlan(plan);
   mPublishedRoutes = plan.numRoutes;
   mPublishedSourceId = plan.numSources > 0 ? plan.sources[0].id : 0;
}

float AnalogNode::DebugVoiceSource(int voice, int source) const
{
   return mAudioNode ? mAudioNode->DebugVoiceSource(voice, source) : 0.0f;
}

void AnalogNode::VisitParams(ParamVisitor& v)
{
   v.Float("volume", volume);
   v.Float("freq", freq);
   v.Float("glide", glide);
   v.Float("pitchBend", pitchBend);
   v.Float("fine1", fine1);
   v.Float("semi1", semi1);
   v.Float("oct1", oct1);
   v.Float("fine2", fine2);
   v.Float("semi2", semi2);
   v.Float("oct2", oct2);
   v.Float("pw1", pw1);
   v.Float("osc1Vol", osc1Vol);
   v.Float("osc2Vol", osc2Vol);
   v.Float("voices", voices);
   v.Float("spread", spread);
   v.Float("fm", fm);
   v.Float("detune", detune);
   v.Float("oscMix", oscMix);
   v.Float("sub", sub);
   v.Float("noise", noise);
   v.Float("cutoff", cutoff);
   v.Float("resonance", resonance);
   v.Float("drive", drive);
   v.Float("keyTrack", keyTrack);
   v.Float("attack", attack);
   v.Float("decay", decay);
   v.Float("sustain", sustain);
   v.Float("release", release);
   // A patch file, OSC or RPC set stores any finite float and the knobs bind
   // only the UI; volume, osc levels, resonance and drive reach the voice
   // unclamped (B-K-034). Hold each to its knob's or dropdown's range.
   constexpr float kMaxEnvMs = 4000.0f;
   volume = std::clamp(volume, 0.0f, 1.0f);
   freq = std::clamp(freq, 20.0f, 8000.0f);
   glide = std::clamp(glide, 0.0f, 2.0f);
   pitchBend = std::clamp(pitchBend, -2.0f, 2.0f);
   fine1 = std::clamp(fine1, -50.0f, 50.0f);
   fine2 = std::clamp(fine2, -50.0f, 50.0f);
   semi1 = std::clamp(semi1, -24.0f, 24.0f);
   semi2 = std::clamp(semi2, -24.0f, 24.0f);
   oct1 = std::clamp(oct1, -4.0f, 4.0f);
   oct2 = std::clamp(oct2, -4.0f, 4.0f);
   pw1 = std::clamp(pw1, 0.01f, 0.99f);
   osc1Vol = std::clamp(osc1Vol, 0.0f, 1.0f);
   osc2Vol = std::clamp(osc2Vol, 0.0f, 1.0f);
   voices = std::clamp(voices, 1.0f, 7.0f);
   spread = std::clamp(spread, 0.0f, 1.0f);
   fm = std::clamp(fm, 0.0f, 1.0f);
   detune = std::clamp(detune, 0.0f, 100.0f);
   oscMix = std::clamp(oscMix, 0.0f, 1.0f);
   sub = std::clamp(sub, 0.0f, 1.0f);
   noise = std::clamp(noise, 0.0f, 1.0f);
   cutoff = std::clamp(cutoff, 20.0f, 18000.0f);
   resonance = std::clamp(resonance, 0.0f, 1.0f);
   drive = std::clamp(drive, 0.0f, 1.0f);
   keyTrack = std::clamp(keyTrack, 0.0f, 1.0f);
   attack = std::clamp(attack, 0.0f, kMaxEnvMs);
   decay = std::clamp(decay, 0.0f, kMaxEnvMs);
   sustain = std::clamp(sustain, 0.0f, 1.0f);
   release = std::clamp(release, 0.0f, kMaxEnvMs);

   v.Int("wave1", wave1);
   v.Int("wave2", wave2);
   v.Int("filterType", filterType);
   v.Bool("sync", sync);
   v.Bool("analog", analog);
}

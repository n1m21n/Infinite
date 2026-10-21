#include "AnalogNode.h"
#include "AnalogSynthCore.h"

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

   v.Int("wave1", wave1);
   v.Int("wave2", wave2);
   v.Int("filterType", filterType);
   v.Bool("sync", sync);
   v.Bool("analog", analog);
}

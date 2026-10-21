#include "FMNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/dsp/Oversampler.h"

namespace
{
   using FMDsp::kNumOps;

   enum SmoothedParam
   {
      kParamVolume = 0,
      kParamFeedback,
      kParamOp0, // per operator: ratio, freq, level
      kPerOp = 3,
      kNumSmoothedParams = kParamOp0 + kPerOp * kNumOps
   };

   constexpr int kOversample = 8;
   // Phase deviation, in cycles, of a modulator at full level: ~6.3 rad,
   // about the deepest a DX-style operator goes.
   constexpr float kModCycles = 1.0f;
   // Operator 4's self-feedback at 1: a quarter cycle on the average of its
   // last two outputs, which keeps it a buzz rather than noise.
   constexpr float kFeedbackCycles = 0.25f;
   constexpr int kControlChunk = 32;

   // Sine over one cycle in turns, linearly interpolated: about -118 dB of
   // error, below the 8x filter's own floor.
   struct SineTable
   {
      static constexpr int kSize = 2048;
      float v[kSize + 1];
      SineTable()
      {
         for (int i = 0; i <= kSize; i++)
            v[i] = (float)std::sin(2.0 * M_PI * (double)i / kSize);
      }
      static const SineTable& Instance()
      {
         static const SineTable s;
         return s;
      }
      float operator()(float turns) const
      {
         turns -= std::floor(turns);
         const float x = turns * (float)kSize;
         const int i = std::min((int)x, kSize - 1);
         const float f = x - (float)i;
         return v[i] + f * (v[i + 1] - v[i]);
      }
   };

   struct FMVoice
   {
      bool active = false;
      int note = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float bend = 0.0f;
      uint64_t age = 0;
      double phase[kNumOps] = {};
      float out[kNumOps] = {};
      float fbHistory[2] = {};
      Envelope env[kNumOps];
   };
}

class AudioFMNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = FMNode::kMaxVoices;

   // Ready to render before PrepareToPlay: a node can be processed first.
   AudioFMNode() { mOversampler.Prepare(kOversample, 32); }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mOversampler.Prepare(kOversample, 32);
      for (auto& v : mVoices)
         for (auto& e : v.env)
            e.SetSampleRate(mSampleRate);
      Reset();
   }

   void Reset() override
   {
      for (auto& v : mVoices)
      {
         v.active = false;
         for (auto& e : v.env)
         {
            e.NoteOff();
            e.ResetLevel();
         }
      }
      mOversampler.Reset();
      mNextAge = 1;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void SetClipPitchOverride(float semitones) override { mClipPitch.store(semitones, std::memory_order_relaxed); }

   MeterRing& ScopeRing() { return mScopeRing; }
   int ActiveVoices() const { return mActiveVoiceCount.load(std::memory_order_relaxed); }

   void PushParams(const FMNode& node)
   {
      mMailbox.Push(kParamVolume, node.volume);
      mMailbox.Push(kParamFeedback, node.feedback);
      for (int o = 0; o < kNumOps; o++)
      {
         const FMNode::Operator& op = node.ops[o];
         mMailbox.Push(kParamOp0 + o * kPerOp + 0, op.ratio);
         mMailbox.Push(kParamOp0 + o * kPerOp + 1, op.freq);
         mMailbox.Push(kParamOp0 + o * kPerOp + 2, op.level);
         mFixed[o].store(op.fixed ? 1 : 0, std::memory_order_relaxed);
         mAttack[o].store(op.attack, std::memory_order_relaxed);
         mDecay[o].store(op.decay, std::memory_order_relaxed);
         mSustain[o].store(op.sustain, std::memory_order_relaxed);
         mRelease[o].store(op.release, std::memory_order_relaxed);
      }
      mAlgorithm.store(std::clamp(node.algorithm, 0, FMDsp::kNumAlgorithms - 1), std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      if (output.numChannels <= 0 || output.numFrames <= 0)
         return;
      float* outL = output.channels[0];
      float* outR = (output.numChannels > 1 && output.channels[1]) ? output.channels[1] : nullptr;
      for (int ch = 2; ch < output.numChannels; ch++)
         if (output.channels[ch])
            std::fill_n(output.channels[ch], output.numFrames, 0.0f);

      NoteEvent evts[64];
      const int numEvts = mNoteInbox ? mNoteInbox->Pop(mNoteCursor, evts, 64) : 0;
      int nextEvt = 0;

      const FMDsp::Algorithm& alg = FMDsp::AlgorithmAt(mAlgorithm.load(std::memory_order_relaxed));
      int numCarriers = 0;
      for (int o = 0; o < kNumOps; o++)
         numCarriers += (alg.carriers >> o) & 1;
      const float carrierGain = 1.0f / std::sqrt((float)std::max(1, numCarriers));
      bool fixed[kNumOps];
      for (int o = 0; o < kNumOps; o++)
         fixed[o] = mFixed[o].load(std::memory_order_relaxed) != 0;
      const float clipPitch = mClipPitch.load(std::memory_order_relaxed);
      const SineTable& sine = SineTable::Instance();
      const double osRate = mSampleRate * kOversample;

      for (int i = 0; i < output.numFrames; i++)
      {
         while (nextEvt < numEvts && evts[nextEvt].frameOffset <= i)
            HandleEvent(evts[nextEvt++]);

         if ((i % kControlChunk) == 0)
         {
            for (auto& v : mVoices)
               if (v.active)
                  for (int o = 0; o < kNumOps; o++)
                     v.env[o].SetADSR(mAttack[o].load(std::memory_order_relaxed),
                                      mDecay[o].load(std::memory_order_relaxed),
                                      mSustain[o].load(std::memory_order_relaxed),
                                      mRelease[o].load(std::memory_order_relaxed));
         }

         const float volume = mMailbox.SmoothedValue(kParamVolume);
         const float feedback = std::clamp(mMailbox.SmoothedValue(kParamFeedback), 0.0f, 1.0f) * kFeedbackCycles;
         float ratio[kNumOps], freq[kNumOps], level[kNumOps];
         for (int o = 0; o < kNumOps; o++)
         {
            ratio[o] = std::max(0.0f, mMailbox.SmoothedValue(kParamOp0 + o * kPerOp + 0));
            freq[o] = std::max(0.0f, mMailbox.SmoothedValue(kParamOp0 + o * kPerOp + 1));
            level[o] = std::clamp(mMailbox.SmoothedValue(kParamOp0 + o * kPerOp + 2), 0.0f, 1.0f);
         }

         float os[kOversample] = {};
         for (auto& v : mVoices)
         {
            if (!v.active)
               continue;
            const double noteHz = 440.0 * std::pow(2.0, ((double)v.note - 69.0 + v.bend + clipPitch) / 12.0);
            double inc[kNumOps];
            float amp[kNumOps];
            for (int o = 0; o < kNumOps; o++)
            {
               inc[o] = (fixed[o] ? (double)freq[o] : noteHz * ratio[o]) / osRate;
               amp[o] = v.env[o].Process() * level[o];
            }
            const float velGain = v.velocity;
            for (int s = 0; s < kOversample; s++)
            {
               float sum = 0.0f;
               for (int o = kNumOps - 1; o >= 0; o--)
               {
                  float pm = 0.0f;
                  for (int m = o + 1; m < kNumOps; m++)
                     if ((alg.mods[o] >> m) & 1)
                        pm += v.out[m];
                  pm *= kModCycles;
                  if (o == kNumOps - 1)
                     pm += feedback * 0.5f * (v.fbHistory[0] + v.fbHistory[1]);
                  v.out[o] = sine((float)v.phase[o] + pm) * amp[o];
                  v.phase[o] += inc[o];
                  v.phase[o] -= std::floor(v.phase[o]);
                  if ((alg.carriers >> o) & 1)
                     sum += v.out[o];
               }
               v.fbHistory[1] = v.fbHistory[0];
               v.fbHistory[0] = v.out[kNumOps - 1];
               os[s] += sum * velGain * carrierGain;
            }

            bool sounding = false;
            for (int o = 0; o < kNumOps; o++)
               if (((alg.carriers >> o) & 1) && v.env[o].IsActive())
                  sounding = true;
            if (!sounding)
               v.active = false;
         }

         float y = mOversampler.Down(0, os) * volume;
         if (!std::isfinite(y))
            y = 0.0f;
         y = std::clamp(y, -4.0f, 4.0f);
         outL[i] = y;
         if (outR)
            outR[i] = y;
         if ((i & 7) == 0)
            mScopeRing.Write(&y, 1);
      }
      while (nextEvt < numEvts)
         HandleEvent(evts[nextEvt++]);

      int count = 0;
      for (const auto& v : mVoices)
         count += v.active ? 1 : 0;
      mActiveVoiceCount.store(count, std::memory_order_relaxed);
   }

private:
   void HandleEvent(const NoteEvent& e)
   {
      if (e.bendUpdate)
      {
         for (auto& v : mVoices)
            if (v.active && v.voiceId == e.voiceId)
               v.bend = e.bendSemitones;
         return;
      }
      if (e.isNoteOn && e.velocity > 0.0f)
      {
         NoteOn(e);
         return;
      }
      for (auto& v : mVoices)
         if (v.active && v.note == e.note && (v.voiceId == e.voiceId || e.voiceId == 0))
            for (auto& env : v.env)
               env.NoteOff();
   }

   void NoteOn(const NoteEvent& e)
   {
      // Same note retriggers its voice, else a free one, else the oldest.
      int slot = -1;
      for (int k = 0; k < kMaxVoices && slot < 0; k++)
         if (mVoices[k].active && mVoices[k].note == e.note)
            slot = k;
      for (int k = 0; k < kMaxVoices && slot < 0; k++)
         if (!mVoices[k].active)
            slot = k;
      const bool stolen = slot < 0;
      if (stolen)
      {
         uint64_t oldest = UINT64_MAX;
         for (int k = 0; k < kMaxVoices; k++)
            if (mVoices[k].age < oldest)
            {
               oldest = mVoices[k].age;
               slot = k;
            }
      }
      FMVoice& v = mVoices[slot];
      const bool fresh = !v.active || stolen;
      v.active = true;
      v.note = e.note;
      v.voiceId = e.voiceId;
      v.velocity = std::clamp(e.velocity, 0.0f, 1.0f);
      v.bend = e.bendSemitones;
      v.age = mNextAge++;
      for (int o = 0; o < kNumOps; o++)
      {
         v.env[o].SetADSR(mAttack[o].load(std::memory_order_relaxed), mDecay[o].load(std::memory_order_relaxed),
                          mSustain[o].load(std::memory_order_relaxed), mRelease[o].load(std::memory_order_relaxed));
         if (fresh)
         {
            // Phases restart at zero so every note starts with the same timbre.
            v.env[o].ResetLevel();
            v.phase[o] = 0.0;
            v.out[o] = 0.0f;
         }
         v.env[o].NoteOn();
      }
      if (fresh)
         v.fbHistory[0] = v.fbHistory[1] = 0.0f;
   }

   double mSampleRate = 44100.0;
   FMVoice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;
   Oversampler mOversampler;

   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   MeterRing mScopeRing;

   std::atomic<int> mAlgorithm{ 0 };
   std::atomic<int> mFixed[kNumOps] = {};
   std::atomic<float> mAttack[kNumOps] = {};
   std::atomic<float> mDecay[kNumOps] = {};
   std::atomic<float> mSustain[kNumOps] = {};
   std::atomic<float> mRelease[kNumOps] = {};
   std::atomic<float> mClipPitch{ 0.0f };
   std::atomic<int> mActiveVoiceCount{ 0 };
};

FMNode::FMNode()
{
   // A bright electric-piano-ish stack: 4>3>2>1, each modulator a little
   // lower and shorter than what it feeds.
   const float kLevel[FMDsp::kNumOps] = { 1.0f, 0.5f, 0.3f, 0.25f };
   const float kRatio[FMDsp::kNumOps] = { 1.0f, 1.0f, 2.0f, 1.0f };
   const float kDecay[FMDsp::kNumOps] = { 900.0f, 500.0f, 350.0f, 250.0f };
   const float kSustain[FMDsp::kNumOps] = { 0.6f, 0.4f, 0.3f, 0.2f };
   for (int o = 0; o < FMDsp::kNumOps; o++)
   {
      ops[o].level = kLevel[o];
      ops[o].ratio = kRatio[o];
      ops[o].decay = kDecay[o];
      ops[o].sustain = kSustain[o];
   }
}

FMNode::~FMNode() = default;

void FMNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioFMNode>();
   mAudioNode->PushParams(*this);
}

AudioNode* FMNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioFMNode>();
   return mAudioNode.get();
}

int FMNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int FMNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

std::vector<INode::SweepParamPrereq> FMNode::SweepPrerequisitesFor(const std::string& paramName) const
{
   // freqN only sounds once its operator is fixed.
   if (paramName.size() == 5 && paramName.compare(0, 4, "freq") == 0)
      return { { std::string("fixed") + paramName[4], 1.0f } };
   return {};
}

void FMNode::VisitParams(ParamVisitor& v)
{
   v.Int("algorithm", algorithm);
   v.Float("feedback", feedback);
   v.Float("volume", volume);
   static const char* kRatio[] = { "ratio1", "ratio2", "ratio3", "ratio4" };
   static const char* kFixed[] = { "fixed1", "fixed2", "fixed3", "fixed4" };
   static const char* kFreq[] = { "freq1", "freq2", "freq3", "freq4" };
   static const char* kLevel[] = { "level1", "level2", "level3", "level4" };
   static const char* kAttack[] = { "attack1", "attack2", "attack3", "attack4" };
   static const char* kDecay[] = { "decay1", "decay2", "decay3", "decay4" };
   static const char* kSustain[] = { "sustain1", "sustain2", "sustain3", "sustain4" };
   static const char* kRelease[] = { "release1", "release2", "release3", "release4" };
   for (int o = 0; o < FMDsp::kNumOps; o++)
   {
      v.Float(kRatio[o], ops[o].ratio);
      v.Bool(kFixed[o], ops[o].fixed);
      v.Float(kFreq[o], ops[o].freq);
      v.Float(kLevel[o], ops[o].level);
      v.Float(kAttack[o], ops[o].attack);
      v.Float(kDecay[o], ops[o].decay);
      v.Float(kSustain[o], ops[o].sustain);
      v.Float(kRelease[o], ops[o].release);
   }
}

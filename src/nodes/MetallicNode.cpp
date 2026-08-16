#include "MetallicNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/dsp/MetallicResonator.h"

namespace
{
   enum SmoothedParam
   {
      kParamFrequency = 0,
      kParamTransient,
      kParamDecay,
      kParamStiffness,
      kParamCutoff,
      kParamResonance,
      kParamDrive,
      kParamWidth,
      kParamVolume,
      kParamGlide,
      kParamFine,
      kNumSmoothedParams
   };

   inline float MidiNoteToHz(int midiNote, float fineCents = 0.0f)
   {
      return 440.0f * powf(2.0f, (float)(midiNote - 69 + fineCents * 0.01f) / 12.0f);
   }
}

// ---------------------------------------------------------------------------
// AudioMetallicNode (Audio-thread real-time DSP processor)
// ---------------------------------------------------------------------------
class AudioMetallicNode : public AudioNode
{
public:
   static constexpr int kMaxVoices = MetallicNode::kMaxVoices;

   AudioMetallicNode()
   {
      for (int v = 0; v < kMaxVoices; v++)
      {
         mVoices[v].Reset();
      }
   }

   ~AudioMetallicNode() override = default;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mFilterL1.SetSampleRate(mSampleRate);
      mFilterL2.SetSampleRate(mSampleRate);
      mFilterR1.SetSampleRate(mSampleRate);
      mFilterR2.SetSampleRate(mSampleRate);
      Reset();
   }

   void Reset() override
   {
      for (int v = 0; v < kMaxVoices; v++)
         mVoices[v].Reset();
      mFilterL1.Reset();
      mFilterL2.Reset();
      mFilterR1.Reset();
      mFilterR2.Reset();
      mNextAge = 1;
      mFreeRunTriggerTimer = 0;
   }

   void SetNoteInbox(NoteEventQueue* inbox) override
   {
      mNoteInbox = inbox;
   }

   MeterRing& ScopeRing() { return mScopeRing; }
   int ActiveVoices() const { return mActiveVoiceCount.load(std::memory_order_relaxed); }

   void PushParams(const MetallicNode& node, bool manualTrigger)
   {
      mMailbox.Push(kParamFrequency, node.frequency);
      mMailbox.Push(kParamTransient, node.transient);
      mMailbox.Push(kParamDecay, node.decay);
      mMailbox.Push(kParamStiffness, node.stiffness);
      mMailbox.Push(kParamCutoff, node.filterCutoff);
      mMailbox.Push(kParamResonance, node.filterResonance);
      mMailbox.Push(kParamDrive, node.drive);
      mMailbox.Push(kParamWidth, node.width);
      mMailbox.Push(kParamVolume, node.volume);
      mMailbox.Push(kParamGlide, node.glide);
      mMailbox.Push(kParamFine, node.fine);

      mMaterial.store(node.material, std::memory_order_relaxed);
      mOctave.store(node.octave, std::memory_order_relaxed);
      mSemi.store(node.semi, std::memory_order_relaxed);
      mFilterType.store(node.filterType, std::memory_order_relaxed);
      if (manualTrigger)
         mManualStrikeTrigger.store(1, std::memory_order_release);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      if (output.numChannels <= 0 || output.numFrames <= 0)
         return;

      for (int ch = 0; ch < output.numChannels; ch++)
      {
         if (output.channels[ch])
            std::fill_n(output.channels[ch], output.numFrames, 0.0f);
      }

      float* outL = output.channels[0];
      float* outR = (output.numChannels > 1 && output.channels[1]) ? output.channels[1] : outL;

      const int currentMaterial = mMaterial.load(std::memory_order_relaxed);
      const int currentOctave = mOctave.load(std::memory_order_relaxed);
      const int currentSemi = mSemi.load(std::memory_order_relaxed);
      const int currentFilterType = mFilterType.load(std::memory_order_relaxed);
      bool manualStrikePending = mManualStrikeTrigger.exchange(0, std::memory_order_acq_rel) != 0;

      const float tuningSemis = (float)(currentOctave * 12 + currentSemi);

      // The block is walked in fixed control-rate chunks. ParamMailbox smoothers
      // advance by exactly one sample per SmoothedValue() call, so reading them
      // once per block would stretch their 5 ms time constant to 5 ms * blockSize
      // (~2.5 s at 512 frames) - which is what made every knob move sound like a
      // multi-second sweep. Advancing them `chunk` times per chunk keeps the
      // smoothing honest while still recomputing voice coefficients only at
      // control rate.
      int frame = 0;
      while (frame < output.numFrames)
      {
         const int chunk = std::min(kControlChunk, output.numFrames - frame);

         float transientVal = 0.0f;
         float decayVal = 0.0f;
         float stiffnessVal = 0.0f;
         float widthVal = 0.0f;
         float fineVal = 0.0f;
         float glideSec = 0.0f;
         float baseFreq = 0.0f;
         for (int s = 0; s < chunk; s++)
         {
            transientVal = mMailbox.SmoothedValue(kParamTransient);
            decayVal = mMailbox.SmoothedValue(kParamDecay);
            stiffnessVal = mMailbox.SmoothedValue(kParamStiffness);
            widthVal = mMailbox.SmoothedValue(kParamWidth);
            fineVal = mMailbox.SmoothedValue(kParamFine);
            glideSec = mMailbox.SmoothedValue(kParamGlide);
            baseFreq = mMailbox.SmoothedValue(kParamFrequency);
         }

         if (mNoteInbox != nullptr)
         {
            NoteEvent evts[64];
            const int numEvts = mNoteInbox->Pop(evts, 64);
            for (int i = 0; i < numEvts; i++)
            {
               const auto& event = evts[i];
               if (event.isNoteOn && event.velocity > 0.0f)
               {
                  const float noteHz = MidiNoteToHz(event.note, fineVal) * powf(2.0f, tuningSemis / 12.0f);
                  const int voiceIdx = AllocateVoice();
                  mVoices[voiceIdx].age = mNextAge++;
                  mVoices[voiceIdx].Trigger(event.note, event.velocity, event.voiceId, noteHz,
                                            transientVal, decayVal, stiffnessVal,
                                            currentMaterial, widthVal, mSampleRate);
               }
               else
               {
                  for (int v = 0; v < kMaxVoices; v++)
                  {
                     if (mVoices[v].active && (mVoices[v].voiceId == event.voiceId || mVoices[v].midiNote == event.note))
                        mVoices[v].Release(mSampleRate);
                  }
               }
            }

            if (manualStrikePending)
            {
               manualStrikePending = false;
               const float noteHz = MidiNoteToHz(60, fineVal) * powf(2.0f, tuningSemis / 12.0f);
               const int voiceIdx = AllocateVoice();
               mVoices[voiceIdx].age = mNextAge++;
               mVoices[voiceIdx].Trigger(60, 0.85f, -1, noteHz,
                                         transientVal, decayVal, stiffnessVal,
                                         currentMaterial, widthVal, mSampleRate);
            }

            // Retune held voices at control rate. Transient is deliberately not
            // consulted here - a knob move must never restrike a sounding voice.
            for (int v = 0; v < kMaxVoices; v++)
            {
               if (!mVoices[v].active || mVoices[v].midiNote < 0)
                  continue;
               const float targetHz = MidiNoteToHz(mVoices[v].midiNote, fineVal) * powf(2.0f, tuningSemis / 12.0f);
               const float glidedHz = mVoices[v].GlideTo(targetHz, glideSec, chunk, mSampleRate);
               mVoices[v].UpdateAcoustics(glidedHz, decayVal, stiffnessVal, currentMaterial, widthVal, mSampleRate);
            }
         }
         else
         {
            // Free-running unpatched note mode
            const float tuningMult = powf(2.0f, (tuningSemis + fineVal * 0.01f) / 12.0f);
            const float freeRunHz = std::clamp(baseFreq * tuningMult, 20.0f, 8000.0f);

            mFreeRunTriggerTimer -= chunk;
            if (manualStrikePending || mFreeRunTriggerTimer <= 0)
            {
               manualStrikePending = false;
               const int voiceIdx = AllocateVoice();
               mVoices[voiceIdx].age = mNextAge++;
               mVoices[voiceIdx].Trigger(60, 0.85f, 0, freeRunHz,
                                         transientVal, decayVal, stiffnessVal,
                                         currentMaterial, widthVal, mSampleRate);

               const float intervalSec = std::clamp(decayVal * 1.2f, 1.5f, 5.0f);
               mFreeRunTriggerTimer = (int)(intervalSec * (float)mSampleRate);
            }

            for (int v = 0; v < kMaxVoices; v++)
            {
               if (!mVoices[v].active)
                  continue;
               const float glidedHz = mVoices[v].GlideTo(freeRunHz, glideSec, chunk, mSampleRate);
               mVoices[v].UpdateAcoustics(glidedHz, decayVal, stiffnessVal, currentMaterial, widthVal, mSampleRate);
            }
         }

         // Render this control chunk
         for (int i = frame; i < frame + chunk; i++)
         {
            const float cutoffHz = std::clamp(mMailbox.SmoothedValue(kParamCutoff), 20.0f, (float)mSampleRate * 0.48f);
            const float resQ = std::clamp(0.5f + mMailbox.SmoothedValue(kParamResonance) * 8.0f, 0.5f, 10.0f);
            const float driveAmount = mMailbox.SmoothedValue(kParamDrive);
            const float masterVol = mMailbox.SmoothedValue(kParamVolume);

            mFilterL1.SetCutoff(cutoffHz, resQ);
            mFilterL2.SetCutoff(cutoffHz, resQ);
            mFilterR1.SetCutoff(cutoffHz, resQ);
            mFilterR2.SetCutoff(cutoffHz, resQ);

            float sL = 0.0f;
            float sR = 0.0f;

            for (int v = 0; v < kMaxVoices; v++)
            {
               if (mVoices[v].active)
               {
                  mVoices[v].Process(sL, sR);
               }
            }

            // Soft body saturation
            if (driveAmount > 0.001f)
            {
               const float driveGain = 1.0f + driveAmount * 2.5f;
               sL = DspMath::FastTanh(sL * driveGain);
               sR = DspMath::FastTanh(sR * driveGain);
            }

            // Master Shaping Filter
            if (currentFilterType != MetallicDsp::kFilterOff)
            {
               auto fOutL1 = mFilterL1.Process(sL);
               auto fOutR1 = mFilterR1.Process(sR);

               switch (currentFilterType)
               {
               case MetallicDsp::kFilterLP12:
                  sL = fOutL1.low;
                  sR = fOutR1.low;
                  break;
               case MetallicDsp::kFilterLP24:
               {
                  auto fOutL2 = mFilterL2.Process(fOutL1.low);
                  auto fOutR2 = mFilterR2.Process(fOutR1.low);
                  sL = fOutL2.low;
                  sR = fOutR2.low;
                  break;
               }
               case MetallicDsp::kFilterHP12:
                  sL = fOutL1.high;
                  sR = fOutR1.high;
                  break;
               case MetallicDsp::kFilterBP:
                  sL = fOutL1.band;
                  sR = fOutR1.band;
                  break;
               default:
                  break;
               }
            }

            sL *= masterVol;
            sR *= masterVol;
            if (!std::isfinite(sL)) sL = 0.0f;
            if (!std::isfinite(sR)) sR = 0.0f;

            outL[i] = std::clamp(sL, -4.0f, 4.0f);
            if (outR != outL)
               outR[i] = std::clamp(sR, -4.0f, 4.0f);

            // Decimated mono sum to scope ring
            if ((i & 7) == 0)
            {
               const float s = 0.5f * (outL[i] + outR[i]);
               mScopeRing.Write(&s, 1);
            }
         }

         frame += chunk;
      }

      // Voice count publication
      int activeCount = 0;
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (mVoices[v].active)
            activeCount++;
      }
      mActiveVoiceCount.store(activeCount, std::memory_order_relaxed);
   }

private:
   int AllocateVoice()
   {
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (!mVoices[v].active)
            return v;
      }
      int oldestIdx = 0;
      uint64_t oldestAge = UINT64_MAX;
      for (int v = 0; v < kMaxVoices; v++)
      {
         if (mVoices[v].age < oldestAge)
         {
            oldestAge = mVoices[v].age;
            oldestIdx = v;
         }
      }
      return oldestIdx;
   }

   // Control-rate subdivision: voice coefficients are recomputed at most this
   // often, independent of the host's block size.
   static constexpr int kControlChunk = 32;

   double mSampleRate = 44100.0;
   MetallicDsp::MetallicVoice mVoices[kMaxVoices];
   uint64_t mNextAge = 1;
   int mFreeRunTriggerTimer = 0;

   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   MeterRing mScopeRing;

   DspMath::TptSvf mFilterL1;
   DspMath::TptSvf mFilterL2;
   DspMath::TptSvf mFilterR1;
   DspMath::TptSvf mFilterR2;

   std::atomic<int> mMaterial{ MetallicDsp::kSteel };
   std::atomic<int> mOctave{ 0 };
   std::atomic<int> mSemi{ 0 };
   std::atomic<int> mFilterType{ MetallicDsp::kFilterLP24 };
   std::atomic<int> mManualStrikeTrigger{ 0 };
   std::atomic<int> mActiveVoiceCount{ 0 };
};

// ---------------------------------------------------------------------------
// MetallicNode (Main thread node model & parameter management)
// ---------------------------------------------------------------------------
MetallicNode::MetallicNode()
{
   SetMaterialPreset(MetallicDsp::kSteel);
}

MetallicNode::~MetallicNode() = default;

void MetallicNode::SetMaterialPreset(int preset)
{
   material = std::clamp(preset, 0, (int)MetallicDsp::kNumMaterials - 1);
   auto prof = MetallicDsp::GetMaterialProfile(material);
   stiffness = prof.defaultStiffness;
   decay = prof.defaultDecay;
   transient = prof.strikeHardness * 0.8f;
}

void MetallicNode::TriggerStrike()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMetallicNode>();
   mAudioNode->PushParams(*this, true);
}

void MetallicNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMetallicNode>();

   mAudioNode->PushParams(*this, false);
}

AudioNode* MetallicNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMetallicNode>();
   return mAudioNode.get();
}

int MetallicNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity) : 0;
}

int MetallicNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

void MetallicNode::VisitParams(ParamVisitor& v)
{
   v.Int("material", material);
   v.Float("transient", transient);
   v.Float("decay", decay);
   v.Float("stiffness", stiffness);
   v.Float("frequency", frequency);
   v.Int("octave", octave);
   v.Int("semi", semi);
   v.Float("fine", fine);
   v.Float("glide", glide);
   v.Int("filterType", filterType);
   v.Float("filterCutoff", filterCutoff);
   v.Float("filterResonance", filterResonance);
   v.Float("drive", drive);
   v.Float("width", width);
   v.Float("volume", volume);
}

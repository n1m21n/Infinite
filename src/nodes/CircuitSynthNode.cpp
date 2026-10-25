#include "CircuitSynthNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "audio/NoteEventQueue.h"
#include "audio/ParamMailbox.h"
#include "audio/dsp/CircuitSynthDsp.h"

namespace
{
   enum SmoothedParam
   {
      kParamPitch = 0,
      kParamGlide,
      kParamContact,
      kParamAttack,
      kParamRelease,
      kParamLfoRate,
      kParamLfoDepth,
      kParamWidth,
      kParamSpread,
      kParamLevel,
      kParamKnob0, // .. kParamKnob0 + 7
      kNumSmoothedParams = kParamKnob0 + CircuitSynthNode::kKnobs
   };
   static_assert(kNumSmoothedParams <= ParamMailbox::kMaxParams, "mailbox slots");

   constexpr int kTapNote = 48;
   constexpr int kTapVoiceId = -4801; // never a producer's id (those count up from 1)
}

// ---------------------------------------------------------------------------
// AudioCircuitSynthNode (audio thread)
// ---------------------------------------------------------------------------
class AudioCircuitSynthNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
      mMailbox.PrepareToPlay(mSampleRate);
      mEngine.Prepare(mSampleRate);
      mTapFrames = 0;
   }

   void Reset() override
   {
      mEngine.Reset();
      mTapFrames = 0;
   }

   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mNoteInbox = inbox;
      mNoteCursor = cursor;
   }

   void PushParams(const CircuitSynthNode& n)
   {
      mMailbox.Push(kParamPitch, n.pitch);
      mMailbox.Push(kParamGlide, n.glide);
      mMailbox.Push(kParamContact, n.contactMs);
      mMailbox.Push(kParamAttack, n.attack);
      mMailbox.Push(kParamRelease, n.release);
      mMailbox.Push(kParamLfoRate, n.lfoRate);
      mMailbox.Push(kParamLfoDepth, n.lfoDepth);
      mMailbox.Push(kParamWidth, n.width);
      mMailbox.Push(kParamSpread, n.spread);
      mMailbox.Push(kParamLevel, n.level);
      for (int k = 0; k < CircuitSynthNode::kKnobs; k++)
         mMailbox.Push(kParamKnob0 + k, n.knob[k]);
      mPreset.store(n.preset, std::memory_order_relaxed);
      mWave.store(n.wave, std::memory_order_relaxed);
      mMode.store(n.mode, std::memory_order_relaxed);
      mTrigger.store(n.trigger, std::memory_order_relaxed);
      mFollow.store((n.cFollow ? 1 : 0) | (n.lFollow ? 2 : 0), std::memory_order_relaxed);
      mLfoShape.store(n.lfoShape, std::memory_order_relaxed);
      mLfoTarget.store(n.lfoTarget, std::memory_order_relaxed);
      mProbeA.store(n.probeA, std::memory_order_relaxed);
      mProbeB.store(n.probeB, std::memory_order_relaxed);
   }

   void RequestTap() { mTapRequest.store(1, std::memory_order_release); }
   MeterRing& ScopeRing() { return mScopeRing; }
   int ActiveVoices() const { return mActive.load(std::memory_order_relaxed); }
   int MaxVoices() const { return mMaxVoices.load(std::memory_order_relaxed); }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      if (output.numChannels <= 0 || output.numFrames <= 0)
         return;
      float* outL = output.channels[0];
      float* outR = (output.numChannels > 1 && output.channels[1]) ? output.channels[1] : nullptr;
      const int frames = output.numFrames;

      // Notes of this block, in frame order (a producer pushes them in
      // order; a merge of several may not be).
      NoteEvent evts[kMaxEvents];
      int numEvts = 0;
      if (mNoteInbox != nullptr)
         numEvts = mNoteInbox->Pop(mNoteCursor, evts, kMaxEvents);
      for (int i = 1; i < numEvts; i++)
         for (int j = i; j > 0 && evts[j].frameOffset < evts[j - 1].frameOffset; j--)
            std::swap(evts[j], evts[j - 1]);
      bool tapPending = mTapRequest.exchange(0, std::memory_order_acq_rel) != 0;

      CircuitDsp::Params p;
      p.preset = mPreset.load(std::memory_order_relaxed);
      p.wave = mWave.load(std::memory_order_relaxed);
      p.mode = mMode.load(std::memory_order_relaxed);
      p.trigger = mTrigger.load(std::memory_order_relaxed);
      const int follow = mFollow.load(std::memory_order_relaxed);
      p.cFollow = (follow & 1) != 0;
      p.lFollow = (follow & 2) != 0;
      p.lfoShape = mLfoShape.load(std::memory_order_relaxed);
      p.lfoTarget = mLfoTarget.load(std::memory_order_relaxed);
      p.probeA = mProbeA.load(std::memory_order_relaxed);
      p.probeB = mProbeB.load(std::memory_order_relaxed);

      int ei = 0;
      int frame = 0;
      while (frame < frames)
      {
         // Segments end at the next note event and at least every control
         // chunk, so a note lands on its own frame and the smoothed params
         // (and the circuit's values) move at control rate.
         int end = std::min(frames, frame + kControlChunk);
         if (ei < numEvts && evts[ei].frameOffset > frame)
            end = std::min(end, evts[ei].frameOffset);
         if (mTapFrames > 0)
            end = std::min(end, frame + mTapFrames);
         const int len = std::max(1, end - frame);

         p.pitch = mMailbox.BlockValue(kParamPitch, len);
         p.glide = mMailbox.BlockValue(kParamGlide, len);
         p.contactMs = mMailbox.BlockValue(kParamContact, len);
         p.attack = mMailbox.BlockValue(kParamAttack, len);
         p.release = mMailbox.BlockValue(kParamRelease, len);
         p.lfoRate = mMailbox.BlockValue(kParamLfoRate, len);
         p.lfoDepth = mMailbox.BlockValue(kParamLfoDepth, len);
         p.width = mMailbox.BlockValue(kParamWidth, len);
         p.spread = mMailbox.BlockValue(kParamSpread, len);
         p.level = mMailbox.BlockValue(kParamLevel, len);
         for (int k = 0; k < CircuitSynthNode::kKnobs; k++)
            p.knob[k] = mMailbox.BlockValue(kParamKnob0 + k, len);
         mEngine.Apply(p);

         while (ei < numEvts && evts[ei].frameOffset <= frame)
         {
            const NoteEvent& e = evts[ei++];
            if (e.bendUpdate)
               continue;
            if (e.isNoteOn && e.velocity > 0.0f)
               mEngine.NoteOn(e.note, e.velocity, e.voiceId);
            else
               mEngine.NoteOff(e.note, e.voiceId);
         }
         if (tapPending)
         {
            tapPending = false;
            mEngine.NoteOn(kTapNote, 0.8f, kTapVoiceId);
            mTapFrames = (int)(0.3 * mSampleRate);
         }

         float* r = outR ? outR + frame : mScratch;
         mEngine.Render(outL + frame, r, len, p);
         if (outR == nullptr)
            for (int i = 0; i < len; i++)
               outL[frame + i] = 0.5f * (outL[frame + i] + mScratch[i]);

         for (int i = 0; i < len; i++)
            if (((mScopeCounter++) & 7) == 0)
            {
               const float pair[2] = { outL[frame + i], outR ? outR[frame + i] : outL[frame + i] };
               mScopeRing.Write(pair, 2);
            }

         if (mTapFrames > 0)
         {
            mTapFrames -= len;
            if (mTapFrames <= 0)
            {
               mTapFrames = 0;
               mEngine.NoteOff(kTapNote, kTapVoiceId);
            }
         }
         frame += len;
      }
      mActive.store(mEngine.ActiveVoices(), std::memory_order_relaxed);
      mMaxVoices.store(p.mode == CircuitDsp::kPoly ? mEngine.MaxVoices() : 1, std::memory_order_relaxed);
   }

private:
   static constexpr int kMaxEvents = 128;
   static constexpr int kControlChunk = 16;
   static_assert(kControlChunk <= CircuitDsp::kMaxChunk, "engine chunk");

   double mSampleRate = 48000.0;
   CircuitDsp::Engine mEngine;
   ParamMailbox mMailbox;
   NoteEventQueue* mNoteInbox = nullptr;
   int mNoteCursor = -1;
   MeterRing mScopeRing;
   unsigned mScopeCounter = 0;
   int mTapFrames = 0;
   float mScratch[CircuitDsp::kMaxChunk] = {};

   std::atomic<int> mPreset{ 0 }, mWave{ 0 }, mMode{ 0 }, mTrigger{ 1 }, mFollow{ 3 };
   std::atomic<int> mLfoShape{ 0 }, mLfoTarget{ 0 }, mProbeA{ 0 }, mProbeB{ 1 };
   std::atomic<int> mTapRequest{ 0 }, mActive{ 0 }, mMaxVoices{ 1 };
};

// ---------------------------------------------------------------------------
// CircuitSynthNode (main thread)
// ---------------------------------------------------------------------------
CircuitSynthNode::CircuitSynthNode()
{
   ApplyPreset(CircuitDsp::DefaultPreset());
}

CircuitSynthNode::~CircuitSynthNode() = default;

void CircuitSynthNode::ApplyPreset(int index)
{
   preset = std::clamp(index, 0, CircuitDsp::NumPresets() - 1);
   const CircuitDsp::Preset& p = CircuitDsp::GetPreset(preset);
   wave = p.wave;
   trigger = p.trigger;
   cFollow = p.cFollow;
   lFollow = p.lFollow;
   contactMs = p.contactMs;
   attack = p.attack;
   release = p.release;
   spread = p.spread;
   probeA = p.probeA;
   probeB = p.probeB;
   for (float& k : knob)
      k = 0.0f;
}

void CircuitSynthNode::Tap()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioCircuitSynthNode>();
   mAudioNode->RequestTap();
}

void CircuitSynthNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioCircuitSynthNode>();
   mAudioNode->PushParams(*this);
}

AudioNode* CircuitSynthNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioCircuitSynthNode>();
   return mAudioNode.get();
}

int CircuitSynthNode::ReadScope(float* out, int capacity)
{
   return mAudioNode ? mAudioNode->ScopeRing().Read(out, capacity & ~1) : 0;
}

int CircuitSynthNode::ActiveVoices() const
{
   return mAudioNode ? mAudioNode->ActiveVoices() : 0;
}

int CircuitSynthNode::MaxVoices() const
{
   return mAudioNode ? mAudioNode->MaxVoices() : 1;
}

void CircuitSynthNode::VisitParams(ParamVisitor& v)
{
   // Names are the patch file's keys: never rename one. Presets are saved by
   // index, so the preset table only ever grows at its end.
   v.Int("preset", preset);
   v.Int("wave", wave);
   v.Int("mode", mode);
   v.Int("trigger", trigger);
   v.Bool("cFollow", cFollow);
   v.Bool("lFollow", lFollow);
   v.Float("pitch", pitch);
   v.Float("glide", glide);
   v.Float("contact", contactMs);
   v.Float("attack", attack);
   v.Float("release", release);
   v.Float("lfoRate", lfoRate);
   v.Float("lfoDepth", lfoDepth);
   v.Int("lfoShape", lfoShape);
   v.Int("lfoTarget", lfoTarget);
   v.Int("probeA", probeA);
   v.Int("probeB", probeB);
   v.Float("width", width);
   v.Float("spread", spread);
   v.Float("level", level);
   static const char* kKnobNames[kKnobs] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
   for (int k = 0; k < kKnobs; k++)
      v.Float(kKnobNames[k], knob[k]);
}

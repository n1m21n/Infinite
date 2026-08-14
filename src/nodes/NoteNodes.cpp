#include "NoteNodes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace
{
   // Held-key bitmap helpers: 128 MIDI notes as two 64-bit words, published
   // by the audio thread and read by the UI. Two relaxed atomic loads is a
   // cheaper and far simpler cross-thread surface than 128 flags or a ring,
   // and a keyboard display that is one block stale is indistinguishable
   // from a live one.
   inline void SetKeyBit(std::atomic<uint64_t>* words, int note, bool on)
   {
      if (note < 0 || note > 127)
         return;
      const int w = note >> 6;
      const uint64_t bit = 1ull << (note & 63);
      uint64_t cur = words[w].load(std::memory_order_relaxed);
      words[w].store(on ? (cur | bit) : (cur & ~bit), std::memory_order_relaxed);
   }
}

// ------------------------------------------------------------------ MIDI Notes
class AudioMidiNotesNode : public AudioNode
{
public:
   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      // Start at the live end of the stream: a topology rebuild shouldn't
      // replay every note played since the app launched.
      mCursor = Platform::MidiNoteStreamPosition();
      mHeld[0].store(0, std::memory_order_relaxed);
      mHeld[1].store(0, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const int channelFilter = mChannel.load(std::memory_order_relaxed);
      const int transpose = mTranspose.load(std::memory_order_relaxed);
      const float velScale = mVelocityScale.load(std::memory_order_relaxed);

      // Bounded: at most 64 messages per block, the same cap every note
      // consumer in the system uses on its Pop.
      Platform::MidiNoteMessage msgs[64];
      const int n = Platform::MidiReadNotesSince(mCursor, msgs, 64);

      for (int i = 0; i < n; i++)
      {
         if (channelFilter >= 0 && msgs[i].channel != channelFilter)
            continue;
         const int note = msgs[i].note + transpose;
         if (note < 0 || note > 127)
            continue;

         NoteEvent e;
         e.note = note;
         e.velocity = std::clamp(msgs[i].velocity01 * velScale, 0.0f, 1.0f);
         e.isNoteOn = msgs[i].isNoteOn;
         // See MidiNotesNode's header comment: Platform's ring carries no
         // sample timestamp, so everything lands at the block boundary.
         e.frameOffset = 0;
         e.source = this;
         mOutbox.Push(e);

         SetKeyBit(mHeld, note, e.isNoteOn);
         if (e.isNoteOn)
            mLastNote.store(note, std::memory_order_relaxed);
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const MidiNotesNode& n)
   {
      mChannel.store(n.channel, std::memory_order_relaxed);
      mTranspose.store(n.transpose, std::memory_order_relaxed);
      mVelocityScale.store(n.velocityScale, std::memory_order_relaxed);
   }

   uint64_t HeldWord(int w) const { return mHeld[w].load(std::memory_order_relaxed); }
   int LastNote() const { return mLastNote.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   unsigned long long mCursor = 0;

   std::atomic<uint64_t> mHeld[2] { { 0 }, { 0 } };
   std::atomic<int> mLastNote { -1 };
   std::atomic<int> mChannel { -1 };
   std::atomic<int> mTranspose { 0 };
   std::atomic<float> mVelocityScale { 1.0f };
};

MidiNotesNode::MidiNotesNode() = default;
MidiNotesNode::~MidiNotesNode() = default;

bool MidiNotesNode::StartListening()
{
   std::string error;
   return Platform::MidiStart(error);
}

void MidiNotesNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMidiNotesNode>();
   mAudioNode->PushParams(*this);
}

void MidiNotesNode::VisitParams(ParamVisitor& v)
{
   v.Int("channel", channel);
   v.Int("transpose", transpose);
   v.Float("velocityScale", velocityScale);
}

AudioNode* MidiNotesNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMidiNotesNode>();
   return mAudioNode.get();
}

void MidiNotesNode::HeldKeys(bool out[128]) const
{
   const uint64_t w0 = mAudioNode ? mAudioNode->HeldWord(0) : 0;
   const uint64_t w1 = mAudioNode ? mAudioNode->HeldWord(1) : 0;
   for (int i = 0; i < 64; i++)
   {
      out[i] = (w0 >> i) & 1ull;
      out[i + 64] = (w1 >> i) & 1ull;
   }
}

int MidiNotesNode::HeldCount() const
{
   if (!mAudioNode)
      return 0;
   int count = 0;
   for (int w = 0; w < 2; w++)
   {
      uint64_t bits = mAudioNode->HeldWord(w);
      while (bits)
      {
         bits &= bits - 1;
         count++;
      }
   }
   return count;
}

int MidiNotesNode::LastNote() const
{
   return mAudioNode ? mAudioNode->LastNote() : -1;
}

// -------------------------------------------------------------------- Envelope
class AudioEnvelopeNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mEnv.SetSampleRate(sampleRate);
      mEnv.SetADSR(mAttackMs.load(std::memory_order_relaxed), mDecayMs.load(std::memory_order_relaxed),
                   mSustainLevel.load(std::memory_order_relaxed), mReleaseMs.load(std::memory_order_relaxed));
      mLastPulseStep = -1;
      mGateOpen = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      mEnv.SetADSR(mAttackMs.load(std::memory_order_relaxed), mDecayMs.load(std::memory_order_relaxed),
                   mSustainLevel.load(std::memory_order_relaxed), mReleaseMs.load(std::memory_order_relaxed));

      const int numFrames = output.numFrames;
      const int trigger = mTrigger.load(std::memory_order_relaxed);
      float level = mEnv.Process(); // in case numFrames==0, still publish something sane

      if (trigger == EnvelopeNode::kTriggerPulse)
      {
         // Free-runs off the transport, exactly like Chorder's "groove" step
         // clock (NoteNodes.cpp's AudioChorderNode) - no note input needed.
         // One NoteOn/NoteOff pair per rateDiv-beats period, held open for
         // gatePct of that period. Step/gate checked once per block (the
         // same ~block-length granularity Chorder/Stutter already use for
         // their own tempo-synced triggers), not per sample.
         const int rateDiv = mRateDiv.load(std::memory_order_relaxed);
         const double beatsPerDiv = std::max(0.015625, MusicTime::BeatsFor((MusicTime::RateDivision)rateDiv));
         const double gatePct = std::clamp((double)mGatePct.load(std::memory_order_relaxed), 0.01, 1.0);
         const double stepPos = Transport::Instance().Beats() / beatsPerDiv;
         const long long step = (long long)std::floor(stepPos);
         const double phase = stepPos - (double)step; // 0..1 through the current pulse

         if (step != mLastPulseStep)
         {
            mLastPulseStep = step;
            mEnv.NoteOn();
            mGateOpen = true;
         }
         if (mGateOpen && phase >= gatePct)
         {
            mEnv.NoteOff();
            mGateOpen = false;
         }

         for (int i = 0; i < numFrames; i++)
            level = mEnv.Process();
      }
      else
      {
         NoteEvent evts[64];
         const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;
         int evtIdx = 0;

         for (int i = 0; i < numFrames; i++)
         {
            while (evtIdx < n && evts[evtIdx].frameOffset == i)
            {
               if (evts[evtIdx].isNoteOn)
                  mEnv.NoteOn();
               else
                  mEnv.NoteOff();
               evtIdx++;
            }
            level = mEnv.Process();
         }
      }
      mLevel.store(level, std::memory_order_relaxed);
   }

   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const EnvelopeNode& n)
   {
      mAttackMs.store(n.attackMs, std::memory_order_relaxed);
      mDecayMs.store(n.decayMs, std::memory_order_relaxed);
      mSustainLevel.store(n.sustainLevel, std::memory_order_relaxed);
      mReleaseMs.store(n.releaseMs, std::memory_order_relaxed);
      mTrigger.store(n.trigger, std::memory_order_relaxed);
      mRateDiv.store(n.rateDiv, std::memory_order_relaxed);
      mGatePct.store(n.gatePct, std::memory_order_relaxed);
   }

   float Level() const { return mLevel.load(std::memory_order_relaxed); }

private:
   Envelope mEnv;
   NoteEventQueue* mInbox = nullptr;
   std::atomic<float> mLevel { 0.0f };
   std::atomic<float> mAttackMs { 10.0f };
   std::atomic<float> mDecayMs { 200.0f };
   std::atomic<float> mSustainLevel { 0.6f };
   std::atomic<float> mReleaseMs { 400.0f };
   std::atomic<int> mTrigger { EnvelopeNode::kTriggerNote };
   std::atomic<int> mRateDiv { 6 };
   std::atomic<float> mGatePct { 0.5f };
   double mSampleRate = 44100.0;
   bool mGateOpen = false;
   long long mLastPulseStep = -1;
};

EnvelopeNode::EnvelopeNode() = default;
EnvelopeNode::~EnvelopeNode() = default;

void EnvelopeNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioEnvelopeNode>();
   mAudioNode->PushParams(*this);
}

void EnvelopeNode::VisitParams(ParamVisitor& v)
{
   v.Float("attackMs", attackMs);
   v.Float("decayMs", decayMs);
   v.Float("sustainLevel", sustainLevel);
   v.Float("releaseMs", releaseMs);
   v.Int("trigger", trigger);
   v.Int("rateDiv", rateDiv);
   v.Float("gatePct", gatePct);
}

AudioNode* EnvelopeNode::AudioNodeForNotePorts()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioEnvelopeNode>();
   return mAudioNode.get();
}

float EnvelopeNode::Value01()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioEnvelopeNode>();
   return mAudioNode->Level();
}

// ---------------------------------------------------------------- Note to CV
class AudioNoteToCVNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;
      // Only note-on moves the target - note-off is ignored entirely so the
      // CV holds the last played pitch instead of falling back toward 0 on
      // release (see the class comment on NoteToCVNode).
      for (int i = 0; i < n; i++)
         if (evts[i].isNoteOn)
            mLastNote.store(evts[i].note, std::memory_order_relaxed);

      const int rangeLow = mRangeLow.load(std::memory_order_relaxed);
      const int rangeHigh = std::max(rangeLow + 1, mRangeHigh.load(std::memory_order_relaxed));
      const int lastNote = mLastNote.load(std::memory_order_relaxed);
      const float target =
         lastNote < 0 ? 0.0f : std::clamp((float)(lastNote - rangeLow) / (float)(rangeHigh - rangeLow), 0.0f, 1.0f);

      const float glideMs = std::max(0.0f, mGlideMs.load(std::memory_order_relaxed));
      // One-pole time-constant coefficient, recomputed per block (glideMs
      // changes rarely, at knob-turn rate - not worth per-sample recompute).
      const float coef = glideMs > 0.0f ? expf(-1.0f / (float)(mSampleRate * 0.001 * glideMs)) : 0.0f;

      float level = mLevel.load(std::memory_order_relaxed);
      for (int i = 0; i < numFrames; i++)
         level = target + coef * (level - target);
      mLevel.store(level, std::memory_order_relaxed);
   }

   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteToCVNode& n)
   {
      mRangeLow.store(n.rangeLow, std::memory_order_relaxed);
      mRangeHigh.store(n.rangeHigh, std::memory_order_relaxed);
      mGlideMs.store(n.glideMs, std::memory_order_relaxed);
   }

   float Level() const { return mLevel.load(std::memory_order_relaxed); }
   int LastNote() const { return mLastNote.load(std::memory_order_relaxed); }

private:
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 44100.0;
   std::atomic<float> mLevel { 0.0f };
   std::atomic<int> mLastNote { -1 };
   std::atomic<int> mRangeLow { 0 };
   std::atomic<int> mRangeHigh { 127 };
   std::atomic<float> mGlideMs { 20.0f };
};

NoteToCVNode::NoteToCVNode() = default;
NoteToCVNode::~NoteToCVNode() = default;

void NoteToCVNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteToCVNode>();
   mAudioNode->PushParams(*this);
}

void NoteToCVNode::VisitParams(ParamVisitor& v)
{
   v.Int("rangeLow", rangeLow);
   v.Int("rangeHigh", rangeHigh);
   v.Float("glideMs", glideMs);
}

AudioNode* NoteToCVNode::AudioNodeForNotePorts()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteToCVNode>();
   return mAudioNode.get();
}

float NoteToCVNode::Value01()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteToCVNode>();
   return mAudioNode->Level();
}

int NoteToCVNode::LastNote() const
{
   return mAudioNode ? mAudioNode->LastNote() : -1;
}

// ---------------------------------------------------------------- Note Filter
class AudioNoteFilterNode : public AudioNode
{
public:
   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      // -1 = "this input note is not currently passing" - safe as a sentinel
      // since real MIDI notes are 0..127. Reset on every rebuild so a stale
      // mapping from a previous topology generation can't outlive it.
      for (int i = 0; i < 128; i++)
         mPassingAs[i] = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const int scale = mScale.load(std::memory_order_relaxed);
      const int root = mRoot.load(std::memory_order_relaxed);
      const int rangeLow = mRangeLow.load(std::memory_order_relaxed);
      const int rangeHigh = mRangeHigh.load(std::memory_order_relaxed);
      const float chance = mChance.load(std::memory_order_relaxed);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            const int snapped = MusicTime::SnapToScale(in.note, root, scale, MusicTime::kSnapNearest);
            const bool inRange = snapped >= rangeLow && snapped <= rangeHigh;
            const bool luckyRoll = chance >= 100.0f || mRng.Next() * 50.0f + 50.0f <= chance;
            const bool pass = inRange && luckyRoll && snapped >= 0 && snapped <= 127;

            mPassingAs[in.note] = pass ? snapped : -1;
            mLastNoteIn.store(in.note, std::memory_order_relaxed);
            mLastPassed.store(pass, std::memory_order_relaxed);

            if (pass)
            {
               NoteEvent out = in;
               out.note = snapped;
               out.source = this;
               mOutbox.Push(out);
            }
         }
         else
         {
            const int mapped = mPassingAs[in.note];
            if (mapped >= 0)
            {
               NoteEvent out = in;
               out.note = mapped;
               out.source = this;
               mOutbox.Push(out);
               mPassingAs[in.note] = -1;
            }
         }
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteFilterNode& n)
   {
      mScale.store(n.scale, std::memory_order_relaxed);
      mRoot.store(n.root, std::memory_order_relaxed);
      mRangeLow.store(n.rangeLow, std::memory_order_relaxed);
      mRangeHigh.store(n.rangeHigh, std::memory_order_relaxed);
      mChance.store(n.chance, std::memory_order_relaxed);
   }

   int LastNoteIn() const { return mLastNoteIn.load(std::memory_order_relaxed); }
   bool LastPassed() const { return mLastPassed.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mPassingAs[128] = {};
   DspMath::WhiteNoise mRng;

   std::atomic<int> mScale { 13 };
   std::atomic<int> mRoot { 0 };
   std::atomic<int> mRangeLow { 0 };
   std::atomic<int> mRangeHigh { 127 };
   std::atomic<float> mChance { 100.0f };
   std::atomic<int> mLastNoteIn { -1 };
   std::atomic<bool> mLastPassed { false };
};

NoteFilterNode::NoteFilterNode() = default;
NoteFilterNode::~NoteFilterNode() = default;

void NoteFilterNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteFilterNode>();
   mAudioNode->PushParams(*this);
}

void NoteFilterNode::VisitParams(ParamVisitor& v)
{
   v.Int("scale", scale);
   v.Int("root", root);
   v.Int("rangeLow", rangeLow);
   v.Int("rangeHigh", rangeHigh);
   v.Float("chance", chance);
}

AudioNode* NoteFilterNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteFilterNode>();
   return mAudioNode.get();
}

int NoteFilterNode::LastNoteIn() const
{
   return mAudioNode ? mAudioNode->LastNoteIn() : -1;
}

bool NoteFilterNode::LastPassed() const
{
   return mAudioNode ? mAudioNode->LastPassed() : false;
}

// ---------------------------------------------------------------- Note Modify
// Grid divisions quantizeDiv indexes into, in beats (a beat is a quarter
// note) - index 0 is "off" and never reaches this table.
static const double kQuantizeBeats[] = { 1.0, 1.0, 0.5, 0.25, 0.125 };
static constexpr int kNumQuantizeDiv = 5;

class AudioNoteModifyNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 96;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mLastOutNote = -1;
      for (int i = 0; i < 128; i++)
      {
         mOutNote[i] = -1;
         mSuppressOff[i] = false;
         mScheduledActive[i] = false;
      }
      for (int i = 0; i < kMaxPending; i++)
         mPending[i].active = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int transpose = mTranspose.load(std::memory_order_relaxed);
      const int pitch = mPitch.load(std::memory_order_relaxed);
      const float curve = mVelocityCurve.load(std::memory_order_relaxed);
      const float humanizeMs = mHumanizeTimingMs.load(std::memory_order_relaxed);
      const float humanizeVel = mHumanizeVelocity.load(std::memory_order_relaxed);
      const float gateHoldMs = mGateHoldMs.load(std::memory_order_relaxed);
      const int quantizeDiv = std::clamp(mQuantizeDiv.load(std::memory_order_relaxed), 0, kNumQuantizeDiv - 1);
      const float glideMs = std::max(0.0f, mGlideMs.load(std::memory_order_relaxed));
      const double holdSamples = gateHoldMs * 0.001 * mSampleRate;
      const double bpm = (double)Transport::Instance().Tempo();
      const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            const int outNote = std::clamp(in.note + transpose + pitch, 0, 127);
            float v = std::clamp(in.velocity, 0.0f, 1.0f);
            v = std::pow(v, curve);
            v += mRng.Next() * (humanizeVel / 100.0f) * 0.5f;
            v = std::clamp(v, 0.0f, 1.0f);

            int offset = in.frameOffset;
            if (humanizeMs > 0.0f)
            {
               const int jitterSamples = (int)(mRng.Next() * humanizeMs * 0.001 * mSampleRate);
               offset = std::clamp(offset + jitterSamples, 0, std::max(0, numFrames - 1));
            }

            uint64_t onsetAbs = mSamplePos + (uint64_t)offset;

            // Quantize: snap forward to the next grid line at the chosen
            // division, rather than nearest - guarantees the onset never
            // moves earlier than it actually arrived.
            if (quantizeDiv > 0)
            {
               const double gridSamples = std::max(1.0, kQuantizeBeats[quantizeDiv] * samplesPerBeat);
               onsetAbs = (uint64_t)(std::ceil((double)onsetAbs / gridSamples) * gridSamples);
            }

            // Glide: a fast chromatic run from the previous output note into
            // this one, filling the gap it pushes the real onset back by.
            // NoteEvent carries no continuous pitch, so this approximates
            // portamento as a glissando rather than a true pitch ramp.
            if (glideMs > 0.0f && mLastOutNote >= 0 && mLastOutNote != outNote)
            {
               const int steps = std::clamp(std::abs(outNote - mLastOutNote), 1, 16);
               const int dir = outNote > mLastOutNote ? 1 : -1;
               const double stepSamples = std::max(1.0, (glideMs * 0.001 * mSampleRate) / (double)steps);
               const uint64_t stepBase = onsetAbs;
               for (int k = 0; k < steps - 1; k++)
               {
                  const int stepNote = std::clamp(mLastOutNote + dir * (k + 1), 0, 127);
                  const uint64_t onSample = stepBase + (uint64_t)((double)k * stepSamples);
                  const uint64_t offSample = stepBase + (uint64_t)((double)(k + 1) * stepSamples);
                  if (Pending* on = FreeSlot())
                  {
                     on->active = true;
                     on->note = stepNote;
                     on->velocity = v;
                     on->isNoteOn = true;
                     on->isFinal = false;
                     on->targetSample = onSample;
                  }
                  if (Pending* off = FreeSlot())
                  {
                     off->active = true;
                     off->note = stepNote;
                     off->velocity = 0.0f;
                     off->isNoteOn = false;
                     off->isFinal = false;
                     off->targetSample = offSample;
                  }
               }
               onsetAbs = stepBase + (uint64_t)((double)(steps - 1) * stepSamples);
            }
            mLastOutNote = outNote;
            mLastNoteOut.store(outNote, std::memory_order_relaxed);
            mLastVelocityOut.store(v, std::memory_order_relaxed);

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               EmitFinal(in.note, outNote, v, (int)(onsetAbs - mSamplePos), gateHoldMs, holdSamples, onsetAbs);
            }
            else if (Pending* on = FreeSlot())
            {
               on->active = true;
               on->note = outNote;
               on->velocity = v;
               on->isNoteOn = true;
               on->isFinal = true;
               on->inputNote = in.note;
               on->holdSamples = gateHoldMs > 0.0f ? std::max(0.0, holdSamples) : -1.0;
               on->targetSample = onsetAbs;
            }
         }
         else
         {
            const int outNote = mOutNote[in.note];
            if (outNote < 0)
               continue;

            if (mSuppressOff[in.note])
               continue; // the scheduled internal note-off will close this voice

            NoteEvent out;
            out.note = outNote;
            out.velocity = in.velocity;
            out.isNoteOn = false;
            out.frameOffset = in.frameOffset;
            out.source = this;
            mOutbox.Push(out);
            mOutNote[in.note] = -1;
         }
      }

      // Fire any internally-scheduled note-offs (gateHoldMs override) that
      // fall inside this block.
      for (int note = 0; note < 128; note++)
      {
         if (!mScheduledActive[note])
            continue;
         const uint64_t target = mScheduledTarget[note];
         if (target >= mSamplePos && target < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent out;
            out.note = mOutNote[note];
            out.velocity = 0.0f;
            out.isNoteOn = false;
            out.frameOffset = (int)(target - mSamplePos);
            out.source = this;
            mOutbox.Push(out);
            mScheduledActive[note] = false;
            mSuppressOff[note] = false;
            mOutNote[note] = -1;
         }
      }

      // Fire due glide steps and deferred final notes.
      for (int i = 0; i < kMaxPending; i++)
      {
         Pending& p = mPending[i];
         if (!p.active)
            continue;
         if (p.targetSample >= mSamplePos && p.targetSample < mSamplePos + (uint64_t)numFrames)
         {
            const int frameOffset = (int)(p.targetSample - mSamplePos);
            if (p.isFinal)
               EmitFinal(p.inputNote, p.note, p.velocity, frameOffset, p.holdSamples >= 0.0 ? 1.0f : 0.0f,
                         p.holdSamples, p.targetSample);
            else
            {
               NoteEvent out;
               out.note = p.note;
               out.velocity = p.velocity;
               out.isNoteOn = p.isNoteOn;
               out.frameOffset = frameOffset;
               out.source = this;
               mOutbox.Push(out);
            }
            p.active = false;
         }
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteModifyNode& n)
   {
      mTranspose.store(n.transposeSemi, std::memory_order_relaxed);
      mPitch.store(n.pitchSemi, std::memory_order_relaxed);
      mVelocityCurve.store(n.velocityCurve, std::memory_order_relaxed);
      mHumanizeTimingMs.store(n.humanizeTimingMs, std::memory_order_relaxed);
      mHumanizeVelocity.store(n.humanizeVelocity, std::memory_order_relaxed);
      mGateHoldMs.store(n.gateHoldMs, std::memory_order_relaxed);
      mQuantizeDiv.store(n.quantizeDiv, std::memory_order_relaxed);
      mGlideMs.store(n.glideMs, std::memory_order_relaxed);
   }

   int LastNoteOut() const { return mLastNoteOut.load(std::memory_order_relaxed); }
   float LastVelocityOut() const { return mLastVelocityOut.load(std::memory_order_relaxed); }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
      bool isFinal = false;     // the real (possibly glide/quantize-delayed) target note-on
      int inputNote = 0;        // isFinal only: which input note this closes the loop for
      double holdSamples = -1.0; // isFinal only: >=0 means gateHoldMs override is active
   };

   Pending* FreeSlot()
   {
      for (int i = 0; i < kMaxPending; i++)
         if (!mPending[i].active)
            return &mPending[i];
      return nullptr;
   }

   // Shared by the immediate path and the deferred-Pending path: registers
   // this input note's bookkeeping and pushes the actual NoteEvent.
   void EmitFinal(int inputNote, int outNote, float velocity, int frameOffset, float gateHoldMsFlag,
                  double holdSamples, uint64_t onsetAbs)
   {
      mOutNote[inputNote] = outNote;

      NoteEvent out;
      out.note = outNote;
      out.velocity = velocity;
      out.isNoteOn = true;
      out.frameOffset = frameOffset;
      out.source = this;
      mOutbox.Push(out);

      if (gateHoldMsFlag > 0.0f && holdSamples >= 0.0)
      {
         mSuppressOff[inputNote] = true;
         mScheduledActive[inputNote] = true;
         mScheduledTarget[inputNote] = onsetAbs + (uint64_t)holdSamples;
      }
      else
      {
         mSuppressOff[inputNote] = false;
         mScheduledActive[inputNote] = false;
      }
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;

   int mOutNote[128] = {};
   bool mSuppressOff[128] = {};
   bool mScheduledActive[128] = {};
   uint64_t mScheduledTarget[128] = {};
   int mLastOutNote = -1;
   Pending mPending[kMaxPending];

   std::atomic<int> mTranspose { 0 };
   std::atomic<int> mPitch { 0 };
   std::atomic<float> mVelocityCurve { 1.0f };
   std::atomic<float> mHumanizeTimingMs { 0.0f };
   std::atomic<float> mHumanizeVelocity { 0.0f };
   std::atomic<float> mGateHoldMs { 0.0f };
   std::atomic<int> mQuantizeDiv { 0 };
   std::atomic<float> mGlideMs { 0.0f };
   std::atomic<int> mLastNoteOut { -1 };
   std::atomic<float> mLastVelocityOut { 0.0f };
};

NoteModifyNode::NoteModifyNode() = default;
NoteModifyNode::~NoteModifyNode() = default;

void NoteModifyNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteModifyNode>();
   mAudioNode->PushParams(*this);
}

void NoteModifyNode::VisitParams(ParamVisitor& v)
{
   v.Int("transposeSemi", transposeSemi);
   v.Int("pitchSemi", pitchSemi);
   v.Float("velocityCurve", velocityCurve);
   v.Float("humanizeTimingMs", humanizeTimingMs);
   v.Float("humanizeVelocity", humanizeVelocity);
   v.Float("gateHoldMs", gateHoldMs);
   v.Int("quantizeDiv", quantizeDiv);
   v.Float("glideMs", glideMs);
}

AudioNode* NoteModifyNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteModifyNode>();
   return mAudioNode.get();
}

int NoteModifyNode::LastNoteOut() const
{
   return mAudioNode ? mAudioNode->LastNoteOut() : -1;
}

float NoteModifyNode::LastVelocityOut() const
{
   return mAudioNode ? mAudioNode->LastVelocityOut() : 0.0f;
}

// ------------------------------------------------------- Note Modify, split up
// The eight nodes below are Note Modify's controls, one concern per node -
// see the class comments on their declarations in NoteNodes.h. A small
// shared helper for the four that need to delay a note-on into a future
// block (Gate's internal note-off, Humanizer/Quantizer's delayed onset,
// Glide's glissando steps) - the same Pending-slot mechanism
// AudioNoteModifyNode uses above, pulled out since four separate nodes need
// it rather than one.
namespace
{
   struct DeferredNote
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
   };

   template <int N>
   struct DeferredNoteQueue
   {
      DeferredNote slots[N];

      DeferredNote* FreeSlot()
      {
         for (auto& s : slots)
            if (!s.active)
               return &s;
         return nullptr;
      }

      // Emits, and clears, every slot whose target sample falls inside
      // [samplePos, samplePos + numFrames).
      void FireDue(uint64_t samplePos, int numFrames, NoteEventQueue& outbox, const void* source)
      {
         for (auto& s : slots)
         {
            if (!s.active)
               continue;
            if (s.targetSample >= samplePos && s.targetSample < samplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = s.note;
               out.velocity = s.velocity;
               out.isNoteOn = s.isNoteOn;
               out.frameOffset = (int)(s.targetSample - samplePos);
               out.source = source;
               outbox.Push(out);
               s.active = false;
            }
         }
      }
   };
}

// ---- Transpose / Pitch Bend: shared semitone-shift DSP ----
class AudioSemitoneShiftNode : public AudioNode
{
public:
   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      for (int i = 0; i < 128; i++)
         mOutNote[i] = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const int semi = mSemitones.load(std::memory_order_relaxed);
      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         NoteEvent out;
         out.frameOffset = in.frameOffset;
         out.source = this;

         if (in.isNoteOn)
         {
            const int outNote = std::clamp(in.note + semi, 0, 127);
            mOutNote[in.note] = outNote;
            out.note = outNote;
            out.velocity = in.velocity;
            out.isNoteOn = true;
            mOutbox.Push(out);
            mLastNoteOut.store(outNote, std::memory_order_relaxed);
         }
         else
         {
            const int outNote = mOutNote[in.note];
            if (outNote < 0)
               continue;
            out.note = outNote;
            out.velocity = in.velocity;
            out.isNoteOn = false;
            mOutbox.Push(out);
            mOutNote[in.note] = -1;
         }
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetSemitones(int semi) { mSemitones.store(semi, std::memory_order_relaxed); }
   int LastNoteOut() const { return mLastNoteOut.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mOutNote[128] = {};
   std::atomic<int> mSemitones { 0 };
   std::atomic<int> mLastNoteOut { -1 };
};

NoteTransposeNode::NoteTransposeNode() = default;
NoteTransposeNode::~NoteTransposeNode() = default;

void NoteTransposeNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSemitoneShiftNode>();
   mAudioNode->SetSemitones(semitones);
}

void NoteTransposeNode::VisitParams(ParamVisitor& v) { v.Int("semitones", semitones); }

AudioNode* NoteTransposeNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioSemitoneShiftNode>();
   return mAudioNode.get();
}

int NoteTransposeNode::LastNoteOut() const
{
   return mAudioNode ? mAudioNode->LastNoteOut() : -1;
}

// PitchBendNode is now a plain IModulator - see its class comment in
// NoteNodes.h - so it needs none of the two-object machinery below; its
// CookIfNeeded/VisitParams/Value01 are trivial enough to live inline in the
// header, the same as ConstantNode/MacroKnobNode.

// ---- Velocity Curve ----
class AudioVelocityCurveNode : public AudioNode
{
public:
   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override {}

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const float curve = mCurve.load(std::memory_order_relaxed);
      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         NoteEvent out = evts[i];
         out.source = this;
         if (out.isNoteOn)
         {
            const float vIn = std::clamp(out.velocity, 0.0f, 1.0f);
            const float vOut = std::pow(vIn, curve);
            mLastVelocityIn.store(vIn, std::memory_order_relaxed);
            mLastVelocityOut.store(vOut, std::memory_order_relaxed);
            out.velocity = vOut;
         }
         mOutbox.Push(out);
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetCurve(float c) { mCurve.store(c, std::memory_order_relaxed); }
   float LastVelocityIn() const { return mLastVelocityIn.load(std::memory_order_relaxed); }
   float LastVelocityOut() const { return mLastVelocityOut.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   std::atomic<float> mCurve { 1.0f };
   std::atomic<float> mLastVelocityIn { 0.0f };
   std::atomic<float> mLastVelocityOut { 0.0f };
};

VelocityCurveNode::VelocityCurveNode() = default;
VelocityCurveNode::~VelocityCurveNode() = default;

void VelocityCurveNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioVelocityCurveNode>();
   mAudioNode->SetCurve(curve);
}

void VelocityCurveNode::VisitParams(ParamVisitor& v) { v.Float("curve", curve); }

AudioNode* VelocityCurveNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioVelocityCurveNode>();
   return mAudioNode.get();
}

float VelocityCurveNode::LastVelocityIn() const
{
   return mAudioNode ? mAudioNode->LastVelocityIn() : 0.0f;
}

float VelocityCurveNode::LastVelocityOut() const
{
   return mAudioNode ? mAudioNode->LastVelocityOut() : 0.0f;
}

// ---- Gate ----
class AudioGateNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      for (int i = 0; i < 128; i++)
      {
         mSuppressOff[i] = false;
         mScheduledActive[i] = false;
      }
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const float holdMs = mHoldMs.load(std::memory_order_relaxed);
      const double holdSamples = holdMs * 0.001 * mSampleRate;

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);

            if (holdMs > 0.0f)
            {
               mSuppressOff[in.note] = true;
               mScheduledActive[in.note] = true;
               mScheduledTarget[in.note] = mSamplePos + (uint64_t)in.frameOffset + (uint64_t)holdSamples;
            }
            else
            {
               mSuppressOff[in.note] = false;
               mScheduledActive[in.note] = false;
            }
         }
         else
         {
            if (mSuppressOff[in.note])
               continue; // the scheduled internal note-off will close this voice
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
         }
      }

      for (int note = 0; note < 128; note++)
      {
         if (!mScheduledActive[note])
            continue;
         const uint64_t target = mScheduledTarget[note];
         if (target >= mSamplePos && target < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent out;
            out.note = note;
            out.velocity = 0.0f;
            out.isNoteOn = false;
            out.frameOffset = (int)(target - mSamplePos);
            out.source = this;
            mOutbox.Push(out);
            mScheduledActive[note] = false;
            mSuppressOff[note] = false;
         }
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetHoldMs(float ms) { mHoldMs.store(ms, std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   bool mSuppressOff[128] = {};
   bool mScheduledActive[128] = {};
   uint64_t mScheduledTarget[128] = {};
   std::atomic<float> mHoldMs { 0.0f };
};

GateNode::GateNode() = default;
GateNode::~GateNode() = default;

void GateNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGateNode>();
   mAudioNode->SetHoldMs(holdMs);
}

void GateNode::VisitParams(ParamVisitor& v) { v.Float("holdMs", holdMs); }

AudioNode* GateNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGateNode>();
   return mAudioNode.get();
}

// ---- Humanizer ----
class AudioHumanizerNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 32;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      for (int i = 0; i < 128; i++)
         mOutNote[i] = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const float timingMs = mTimingMs.load(std::memory_order_relaxed);
      const float velocityPct = mVelocityPct.load(std::memory_order_relaxed);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            float v = std::clamp(in.velocity, 0.0f, 1.0f);
            v += mRng.Next() * (velocityPct / 100.0f) * 0.5f;
            v = std::clamp(v, 0.0f, 1.0f);

            int offset = in.frameOffset;
            if (timingMs > 0.0f)
            {
               const int jitterSamples = (int)(mRng.Next() * timingMs * 0.001 * mSampleRate);
               offset = std::clamp(offset + jitterSamples, 0, std::max(0, numFrames - 1));
            }
            const uint64_t onsetAbs = mSamplePos + (uint64_t)offset;
            mOutNote[in.note] = in.note; // no pitch change - just bookkeeping for the matching note-off

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = in.note;
               out.velocity = v;
               out.isNoteOn = true;
               out.frameOffset = (int)(onsetAbs - mSamplePos);
               out.source = this;
               mOutbox.Push(out);
            }
            else if (DeferredNote* slot = mPending.FreeSlot())
            {
               slot->active = true;
               slot->note = in.note;
               slot->velocity = v;
               slot->isNoteOn = true;
               slot->targetSample = onsetAbs;
            }
         }
         else
         {
            if (mOutNote[in.note] < 0)
               continue;
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
            mOutNote[in.note] = -1;
         }
      }

      mPending.FireDue(mSamplePos, numFrames, mOutbox, this);
      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetParams(float timingMs, float velocityPct)
   {
      mTimingMs.store(timingMs, std::memory_order_relaxed);
      mVelocityPct.store(velocityPct, std::memory_order_relaxed);
   }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;
   int mOutNote[128] = {};
   DeferredNoteQueue<kMaxPending> mPending;
   std::atomic<float> mTimingMs { 0.0f };
   std::atomic<float> mVelocityPct { 0.0f };
};

HumanizerNode::HumanizerNode() = default;
HumanizerNode::~HumanizerNode() = default;

void HumanizerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioHumanizerNode>();
   mAudioNode->SetParams(timingMs, velocityPct);
}

void HumanizerNode::VisitParams(ParamVisitor& v)
{
   v.Float("timingMs", timingMs);
   v.Float("velocityPct", velocityPct);
}

AudioNode* HumanizerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioHumanizerNode>();
   return mAudioNode.get();
}

// ---- Quantizer ----
class AudioQuantizerNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 32;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      for (int i = 0; i < 128; i++)
         mOutNote[i] = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int div = std::clamp(mDiv.load(std::memory_order_relaxed), 0, kNumQuantizeDiv - 1);
      const double bpm = (double)Transport::Instance().Tempo();
      const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            uint64_t onsetAbs = mSamplePos + (uint64_t)in.frameOffset;
            if (div > 0)
            {
               const double gridSamples = std::max(1.0, kQuantizeBeats[div] * samplesPerBeat);
               onsetAbs = (uint64_t)(std::ceil((double)onsetAbs / gridSamples) * gridSamples);
            }
            mOutNote[in.note] = in.note;

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = in.note;
               out.velocity = in.velocity;
               out.isNoteOn = true;
               out.frameOffset = (int)(onsetAbs - mSamplePos);
               out.source = this;
               mOutbox.Push(out);
            }
            else if (DeferredNote* slot = mPending.FreeSlot())
            {
               slot->active = true;
               slot->note = in.note;
               slot->velocity = in.velocity;
               slot->isNoteOn = true;
               slot->targetSample = onsetAbs;
            }
         }
         else
         {
            if (mOutNote[in.note] < 0)
               continue;
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
            mOutNote[in.note] = -1;
         }
      }

      mPending.FireDue(mSamplePos, numFrames, mOutbox, this);
      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetDiv(int d) { mDiv.store(d, std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   int mOutNote[128] = {};
   DeferredNoteQueue<kMaxPending> mPending;
   std::atomic<int> mDiv { 0 };
};

QuantizerNode::QuantizerNode() = default;
QuantizerNode::~QuantizerNode() = default;

void QuantizerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioQuantizerNode>();
   mAudioNode->SetDiv(div);
}

void QuantizerNode::VisitParams(ParamVisitor& v) { v.Int("div", div); }

AudioNode* QuantizerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioQuantizerNode>();
   return mAudioNode.get();
}

// ---- Glide ----
class AudioGlideNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 64; // up to 16 glide steps x 2 events + headroom

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mLastOutNote = -1;
      for (int i = 0; i < 128; i++)
         mOutNote[i] = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const float glideMs = std::max(0.0f, mGlideMs.load(std::memory_order_relaxed));

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            uint64_t onsetAbs = mSamplePos + (uint64_t)in.frameOffset;

            if (glideMs > 0.0f && mLastOutNote >= 0 && mLastOutNote != in.note)
            {
               const int steps = std::clamp(std::abs(in.note - mLastOutNote), 1, 16);
               const int dir = in.note > mLastOutNote ? 1 : -1;
               const double stepSamples = std::max(1.0, (glideMs * 0.001 * mSampleRate) / (double)steps);
               const uint64_t stepBase = onsetAbs;
               for (int k = 0; k < steps - 1; k++)
               {
                  const int stepNote = std::clamp(mLastOutNote + dir * (k + 1), 0, 127);
                  const uint64_t onSample = stepBase + (uint64_t)((double)k * stepSamples);
                  const uint64_t offSample = stepBase + (uint64_t)((double)(k + 1) * stepSamples);
                  if (DeferredNote* on = mPending.FreeSlot())
                  {
                     on->active = true;
                     on->note = stepNote;
                     on->velocity = in.velocity;
                     on->isNoteOn = true;
                     on->targetSample = onSample;
                  }
                  if (DeferredNote* off = mPending.FreeSlot())
                  {
                     off->active = true;
                     off->note = stepNote;
                     off->velocity = 0.0f;
                     off->isNoteOn = false;
                     off->targetSample = offSample;
                  }
               }
               onsetAbs = stepBase + (uint64_t)((double)(steps - 1) * stepSamples);
            }

            mLastOutNote = in.note;
            mOutNote[in.note] = in.note;

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = in.note;
               out.velocity = in.velocity;
               out.isNoteOn = true;
               out.frameOffset = (int)(onsetAbs - mSamplePos);
               out.source = this;
               mOutbox.Push(out);
            }
            else if (DeferredNote* on = mPending.FreeSlot())
            {
               on->active = true;
               on->note = in.note;
               on->velocity = in.velocity;
               on->isNoteOn = true;
               on->targetSample = onsetAbs;
            }
         }
         else
         {
            if (mOutNote[in.note] < 0)
               continue;
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
            mOutNote[in.note] = -1;
         }
      }

      mPending.FireDue(mSamplePos, numFrames, mOutbox, this);
      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetGlideMs(float ms) { mGlideMs.store(ms, std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   int mOutNote[128] = {};
   int mLastOutNote = -1;
   DeferredNoteQueue<kMaxPending> mPending;
   std::atomic<float> mGlideMs { 0.0f };
};

GlideNode::GlideNode() = default;
GlideNode::~GlideNode() = default;

void GlideNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGlideNode>();
   mAudioNode->SetGlideMs(glideMs);
}

void GlideNode::VisitParams(ParamVisitor& v) { v.Float("glideMs", glideMs); }

AudioNode* GlideNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioGlideNode>();
   return mAudioNode.get();
}

// ---- Vibrato ----
// No AudioNode/two-object pair here, deliberately - like LFONode
// (ModulatorNodes.cpp), a pin-less modulator's ProcessBlock would never get
// ticked (nothing in the audio graph calls it; only downstream consumers
// sample Value01()), so this computes straight off Transport::Seconds() on
// whichever thread reads it, the same way LFONode reads Transport::Beats().
VibratoNode::VibratoNode() = default;
VibratoNode::~VibratoNode() = default;

void VibratoNode::CookIfNeeded(int /*frameId*/) {}

void VibratoNode::VisitParams(ParamVisitor& v) { v.Float("rateHz", rateHz); }

float VibratoNode::Value01()
{
   const double seconds = Transport::Instance().Seconds();
   const double hz = (double)std::max(0.01f, rateHz);
   const double phase = std::fmod(seconds * hz, 1.0);
   return (float)(0.5 + 0.5 * std::sin(phase * 2.0 * M_PI));
}

// ------------------------------------------------------------------ Note Echo
class AudioNoteEchoNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 128;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      for (int i = 0; i < kMaxPending; i++)
         mPending[i].active = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const float delayMs = mDelayMs.load(std::memory_order_relaxed);
      const int repeats = mRepeats.load(std::memory_order_relaxed);
      const float decay = mDecay.load(std::memory_order_relaxed) / 100.0f;
      const int transposeStep = mTransposeStep.load(std::memory_order_relaxed);
      const double delaySamples = delayMs * 0.001 * mSampleRate;

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];

         // Repeat 0: dry passthrough, unchanged.
         NoteEvent dry = in;
         dry.source = this;
         mOutbox.Push(dry);

         for (int k = 1; k <= repeats; k++)
         {
            Pending* slot = FreeSlot();
            if (slot == nullptr)
               break; // saturated - drop remaining repeats, nothing is left stuck
            slot->active = true;
            slot->note = std::clamp(in.note + transposeStep * k, 0, 127);
            slot->velocity = in.isNoteOn ? std::clamp(in.velocity * std::pow(decay, (float)k), 0.0f, 1.0f)
                                          : in.velocity;
            slot->isNoteOn = in.isNoteOn;
            slot->targetSample = mSamplePos + (uint64_t)in.frameOffset + (uint64_t)(delaySamples * k);
         }
      }

      int activeCount = 0;
      for (int i = 0; i < kMaxPending; i++)
      {
         Pending& p = mPending[i];
         if (!p.active)
            continue;
         if (p.targetSample >= mSamplePos && p.targetSample < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent out;
            out.note = p.note;
            out.velocity = p.velocity;
            out.isNoteOn = p.isNoteOn;
            out.frameOffset = (int)(p.targetSample - mSamplePos);
            out.source = this;
            mOutbox.Push(out);
            p.active = false;
         }
         else
         {
            activeCount++;
         }
      }
      mPendingCount.store(activeCount, std::memory_order_relaxed);

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteEchoNode& n)
   {
      mDelayMs.store(n.delayMs, std::memory_order_relaxed);
      mRepeats.store(n.repeats, std::memory_order_relaxed);
      mDecay.store(n.decay, std::memory_order_relaxed);
      mTransposeStep.store(n.transposePerRepeat, std::memory_order_relaxed);
   }

   int PendingCount() const { return mPendingCount.load(std::memory_order_relaxed); }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
   };

   Pending* FreeSlot()
   {
      for (int i = 0; i < kMaxPending; i++)
         if (!mPending[i].active)
            return &mPending[i];
      return nullptr;
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   Pending mPending[kMaxPending];

   std::atomic<float> mDelayMs { 150.0f };
   std::atomic<int> mRepeats { 3 };
   std::atomic<float> mDecay { 60.0f };
   std::atomic<int> mTransposeStep { 0 };
   std::atomic<int> mPendingCount { 0 };
};

NoteEchoNode::NoteEchoNode() = default;
NoteEchoNode::~NoteEchoNode() = default;

void NoteEchoNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteEchoNode>();
   mAudioNode->PushParams(*this);
}

void NoteEchoNode::VisitParams(ParamVisitor& v)
{
   v.Float("delayMs", delayMs);
   v.Int("repeats", repeats);
   v.Float("decay", decay);
   v.Int("transposePerRepeat", transposePerRepeat);
}

AudioNode* NoteEchoNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteEchoNode>();
   return mAudioNode.get();
}

int NoteEchoNode::PendingCount() const
{
   return mAudioNode ? mAudioNode->PendingCount() : 0;
}

// ---------------------------------------------------------------- Note Router
class AudioNoteRouterNode : public AudioNode
{
public:
   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      mNextIndex = 0;
      mLastRoutedNote = -1;
      for (int i = 0; i < 128; i++)
         mRoutedMask[i] = 0;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const int mode = mMode.load(std::memory_order_relaxed);
      const float probability = mProbability.load(std::memory_order_relaxed);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            int mask = 0;
            switch (mode)
            {
            case NoteRouterNode::kRoundRobin:
               mask = 1 << mNextIndex;
               mNextIndex = (mNextIndex + 1) % 4;
               break;
            case NoteRouterNode::kRandom:
               mask = 1 << std::clamp((int)((mRng.Next() * 0.5f + 0.5f) * 4.0f), 0, 3);
               break;
            case NoteRouterNode::kProbability:
               for (int o = 0; o < 4; o++)
                  if ((mRng.Next() * 0.5f + 0.5f) * 100.0f < probability)
                     mask |= (1 << o);
               if (mask == 0)
                  mask = 1; // never silently drop a note - fall back to output 0
               break;
            case NoteRouterNode::kChain:
            default:
               if (in.note != mLastRoutedNote)
                  mNextIndex = (mNextIndex + 1) % 4;
               mask = 1 << mNextIndex;
               break;
            }

            mRoutedMask[in.note] = (uint8_t)mask;
            mLastRoutedNote = in.note;
            mLastMask.store(mask, std::memory_order_relaxed);

            for (int o = 0; o < 4; o++)
            {
               if ((mask & (1 << o)) == 0)
                  continue;
               NoteEvent out = in;
               out.source = this;
               mOutbox[o].Push(out);
            }
         }
         else
         {
            const int mask = mRoutedMask[in.note];
            if (mask == 0)
               continue;
            for (int o = 0; o < 4; o++)
            {
               if ((mask & (1 << o)) == 0)
                  continue;
               NoteEvent out = in;
               out.source = this;
               mOutbox[o].Push(out);
            }
            mRoutedMask[in.note] = 0;
         }
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox[0]; }
   NoteEventQueue* NoteOutbox(int outputSlot) override
   {
      return &mOutbox[std::clamp(outputSlot, 0, 3)];
   }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteRouterNode& n)
   {
      mMode.store(n.mode, std::memory_order_relaxed);
      mProbability.store(n.probability, std::memory_order_relaxed);
   }

   int LastMask() const { return mLastMask.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox[4];
   NoteEventQueue* mInbox = nullptr;
   DspMath::WhiteNoise mRng;

   int mNextIndex = 0;
   int mLastRoutedNote = -1;
   uint8_t mRoutedMask[128] = {};

   std::atomic<int> mMode { NoteRouterNode::kRoundRobin };
   std::atomic<float> mProbability { 50.0f };
   std::atomic<int> mLastMask { 0 };
};

NoteRouterNode::NoteRouterNode() = default;
NoteRouterNode::~NoteRouterNode() = default;

void NoteRouterNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteRouterNode>();
   mAudioNode->PushParams(*this);
}

void NoteRouterNode::VisitParams(ParamVisitor& v)
{
   v.Int("mode", mode);
   v.Float("probability", probability);
}

AudioNode* NoteRouterNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteRouterNode>();
   return mAudioNode.get();
}

const char* NoteRouterNode::OutputLabel(int index) const
{
   static const char* kLabels[4] = { "1", "2", "3", "4" };
   return kLabels[std::clamp(index, 0, 3)];
}

int NoteRouterNode::LastRoutedMask() const
{
   return mAudioNode ? mAudioNode->LastMask() : 0;
}

// ---------------------------------------------------------------- Arpeggiator
class AudioArpeggiatorNode : public AudioNode
{
public:
   static constexpr int kMaxHeld = 16;
   static constexpr int kMaxExpanded = kMaxHeld * 4; // kMaxHeld held notes x up to 4 octaves

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mHeldCount = 0;
      mLastStep = -1;
      mStepCounter = 0;
      mCurrentOutNote = -1;
      mPendingOffActive = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int mode = mMode.load(std::memory_order_relaxed);
      const int octaves = std::clamp(mOctaves.load(std::memory_order_relaxed), 1, 4);
      const int rateMode = mRateMode.load(std::memory_order_relaxed);
      const float rateBeatsP = std::max(0.015625f, mRateBeats.load(std::memory_order_relaxed));
      const float rateSecondsP = std::max(0.01f, mRateSeconds.load(std::memory_order_relaxed));
      const float gatePercent = std::clamp(mGatePercent.load(std::memory_order_relaxed), 1.0f, 100.0f);
      const double bpm = (double)Transport::Instance().Tempo();
      // Free (seconds) mode is expressed as an equivalent beat count at the
      // current tempo, so it can reuse the same beats-based step clock every
      // other generator in this file uses - tempo-independent by construction
      // since samplesPerBeat and this both scale with bpm and cancel out.
      const double rateBeats = std::max(0.001, rateMode == 1 ? (double)rateSecondsP * bpm / 60.0 : (double)rateBeatsP);

      // Held-note bookkeeping from the incoming stream - this is the only
      // thing incoming events affect. The arp's own output is timed
      // separately, below.
      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;
      for (int i = 0; i < n; i++)
      {
         const NoteEvent& e = evts[i];
         if (e.note < 0 || e.note > 127)
            continue;
         if (e.isNoteOn)
         {
            bool found = false;
            for (int h = 0; h < mHeldCount; h++)
               if (mHeld[h].note == e.note) { mHeld[h].velocity = e.velocity; found = true; break; }
            if (!found && mHeldCount < kMaxHeld)
               mHeld[mHeldCount++] = { e.note, e.velocity };
         }
         else
         {
            for (int h = 0; h < mHeldCount; h++)
            {
               if (mHeld[h].note == e.note)
               {
                  for (int k = h; k < mHeldCount - 1; k++)
                     mHeld[k] = mHeld[k + 1];
                  mHeldCount--;
                  break;
               }
            }
         }
      }
      mHeldCountReadout.store(mHeldCount, std::memory_order_relaxed);

      // Fire a previously-scheduled gate-off before considering a new step,
      // so two output notes are never sounding at once.
      if (mPendingOffActive && mPendingOffSample >= mSamplePos && mPendingOffSample < mSamplePos + (uint64_t)numFrames)
      {
         NoteEvent off;
         off.note = mCurrentOutNote;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = (int)(mPendingOffSample - mSamplePos);
         off.source = this;
         mOutbox.Push(off);
         mPendingOffActive = false;
         mCurrentOutNote = -1;
      }

      const double beats = Transport::Instance().Beats();
      const long long step = (long long)std::floor(beats / (double)rateBeats);
      if (step != mLastStep)
      {
         mLastStep = step;

         if (mHeldCount > 0)
         {
            int seqNote[kMaxExpanded];
            float seqVel[kMaxExpanded];
            const int seqLen = BuildSequence(mode, octaves, seqNote, seqVel);

            if (seqLen > 0)
            {
               int idx = 0;
               if (mode == ArpeggiatorNode::kRandom)
               {
                  idx = std::clamp((int)((mRng.Next() * 0.5f + 0.5f) * (float)seqLen), 0, seqLen - 1);
               }
               else if ((mode == ArpeggiatorNode::kUpDown || mode == ArpeggiatorNode::kDownUp) && seqLen > 1)
               {
                  const int pingpongLen = 2 * (seqLen - 1);
                  const int p = (int)(mStepCounter % (uint64_t)pingpongLen);
                  idx = (p < seqLen) ? p : (pingpongLen - p);
               }
               else
               {
                  idx = (int)(mStepCounter % (uint64_t)seqLen);
               }
               mStepCounter++;

               // Close any still-sounding note first (gate >= 100% case).
               if (mCurrentOutNote >= 0)
               {
                  NoteEvent off;
                  off.note = mCurrentOutNote;
                  off.velocity = 0.0f;
                  off.isNoteOn = false;
                  off.frameOffset = 0;
                  off.source = this;
                  mOutbox.Push(off);
                  mPendingOffActive = false;
               }

               NoteEvent on;
               on.note = seqNote[idx];
               on.velocity = seqVel[idx];
               on.isNoteOn = true;
               on.frameOffset = 0;
               on.source = this;
               mOutbox.Push(on);
               mCurrentOutNote = on.note;
               mCurrentOutNoteReadout.store(on.note, std::memory_order_relaxed);

               const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);
               const double stepSamples = rateBeats * samplesPerBeat;
               const double gateSamples = stepSamples * (double)gatePercent / 100.0;
               mPendingOffSample = mSamplePos + (uint64_t)gateSamples;
               mPendingOffActive = true;
            }
         }
         else if (mCurrentOutNote >= 0)
         {
            // Nothing held any more - close whatever was still sounding.
            NoteEvent off;
            off.note = mCurrentOutNote;
            off.velocity = 0.0f;
            off.isNoteOn = false;
            off.frameOffset = 0;
            off.source = this;
            mOutbox.Push(off);
            mCurrentOutNote = -1;
            mCurrentOutNoteReadout.store(-1, std::memory_order_relaxed);
            mPendingOffActive = false;
         }
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const ArpeggiatorNode& n)
   {
      mMode.store(n.mode, std::memory_order_relaxed);
      mOctaves.store(n.octaves, std::memory_order_relaxed);
      mRateMode.store(n.rateMode, std::memory_order_relaxed);
      mRateBeats.store(n.rateBeats, std::memory_order_relaxed);
      mRateSeconds.store(n.rateSeconds, std::memory_order_relaxed);
      mGatePercent.store(n.gatePercent, std::memory_order_relaxed);
   }

   int HeldCount() const { return mHeldCountReadout.load(std::memory_order_relaxed); }
   int CurrentNote() const { return mCurrentOutNoteReadout.load(std::memory_order_relaxed); }

private:
   struct HeldNote
   {
      int note = 0;
      float velocity = 0.0f;
   };

   int BuildSequence(int mode, int octaves, int outNote[kMaxExpanded], float outVel[kMaxExpanded])
   {
      struct Pair { int note; float vel; };
      Pair tmp[kMaxExpanded];
      int n = 0;
      for (int o = 0; o < octaves; o++)
      {
         for (int i = 0; i < mHeldCount; i++)
         {
            if (n >= kMaxExpanded)
               break;
            tmp[n].note = std::clamp(mHeld[i].note + 12 * o, 0, 127);
            tmp[n].vel = mHeld[i].velocity;
            n++;
         }
      }

      if (mode != ArpeggiatorNode::kAsPlayed && mode != ArpeggiatorNode::kRandom)
      {
         std::sort(tmp, tmp + n, [](const Pair& a, const Pair& b) { return a.note < b.note; });
         if (mode == ArpeggiatorNode::kDown || mode == ArpeggiatorNode::kDownUp)
            std::reverse(tmp, tmp + n);
         else if (mode == ArpeggiatorNode::kConverge)
         {
            // Outside-in: lowest, highest, 2nd-lowest, 2nd-highest, ...
            Pair rearr[kMaxExpanded];
            int lo = 0, hi = n - 1, w = 0;
            while (lo <= hi)
            {
               rearr[w++] = tmp[lo++];
               if (lo <= hi)
                  rearr[w++] = tmp[hi--];
            }
            std::copy(rearr, rearr + n, tmp);
         }
         else if (mode == ArpeggiatorNode::kDiverge)
         {
            // Inside-out: from the middle, alternating outward.
            Pair rearr[kMaxExpanded];
            int w = 0, lo, hi;
            const int mid = n / 2;
            if (n % 2 == 1)
            {
               rearr[w++] = tmp[mid];
               lo = mid - 1;
               hi = mid + 1;
            }
            else
            {
               lo = mid - 1;
               hi = mid;
            }
            while (lo >= 0 || hi < n)
            {
               if (lo >= 0)
                  rearr[w++] = tmp[lo--];
               if (hi < n)
                  rearr[w++] = tmp[hi++];
            }
            std::copy(rearr, rearr + n, tmp);
         }
      }

      for (int i = 0; i < n; i++)
      {
         outNote[i] = tmp[i].note;
         outVel[i] = tmp[i].vel;
      }
      return n;
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;

   HeldNote mHeld[kMaxHeld];
   int mHeldCount = 0;

   long long mLastStep = -1;
   uint64_t mStepCounter = 0;
   int mCurrentOutNote = -1;
   bool mPendingOffActive = false;
   uint64_t mPendingOffSample = 0;

   std::atomic<int> mMode { ArpeggiatorNode::kUp };
   std::atomic<int> mOctaves { 1 };
   std::atomic<int> mRateMode { 0 };
   std::atomic<float> mRateBeats { 0.25f };
   std::atomic<float> mRateSeconds { 0.2f };
   std::atomic<float> mGatePercent { 80.0f };
   std::atomic<int> mHeldCountReadout { 0 };
   std::atomic<int> mCurrentOutNoteReadout { -1 };
};

ArpeggiatorNode::ArpeggiatorNode() = default;
ArpeggiatorNode::~ArpeggiatorNode() = default;

void ArpeggiatorNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioArpeggiatorNode>();
   mAudioNode->PushParams(*this);
}

void ArpeggiatorNode::VisitParams(ParamVisitor& v)
{
   v.Int("mode", mode);
   v.Int("octaves", octaves);
   v.Int("rateMode", rateMode);
   v.Float("rateBeats", rateBeats);
   v.Float("rateSeconds", rateSeconds);
   v.Float("gatePercent", gatePercent);
}

AudioNode* ArpeggiatorNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioArpeggiatorNode>();
   return mAudioNode.get();
}

int ArpeggiatorNode::HeldCount() const
{
   return mAudioNode ? mAudioNode->HeldCount() : 0;
}

int ArpeggiatorNode::CurrentNote() const
{
   return mAudioNode ? mAudioNode->CurrentNote() : -1;
}

// ------------------------------------------------------------- Note Sequencer
class AudioNoteSequencerNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mLastStep = -1;
      mCurrentOutNote = -1;
      mPendingOffActive = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int steps = std::clamp(mSteps.load(std::memory_order_relaxed), 1, NoteSequencerNode::kMaxSteps);
      const int rateMode = mRateMode.load(std::memory_order_relaxed);
      const float rateBeatsP = std::max(0.015625f, mRateBeats.load(std::memory_order_relaxed));
      const float rateSecondsP = std::max(0.01f, mRateSeconds.load(std::memory_order_relaxed));
      const float gatePercent = std::clamp(mGatePercent.load(std::memory_order_relaxed), 1.0f, 100.0f);
      const double bpm = (double)Transport::Instance().Tempo();
      const double rateBeats = std::max(0.001, rateMode == 1 ? (double)rateSecondsP * bpm / 60.0 : (double)rateBeatsP);

      if (mPendingOffActive && mPendingOffSample >= mSamplePos && mPendingOffSample < mSamplePos + (uint64_t)numFrames)
      {
         NoteEvent off;
         off.note = mCurrentOutNote;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = (int)(mPendingOffSample - mSamplePos);
         off.source = this;
         mOutbox.Push(off);
         mPendingOffActive = false;
         mCurrentOutNote = -1;
      }

      const double beats = Transport::Instance().Beats();
      const long long step = (long long)std::floor(beats / (double)rateBeats);
      if (step != mLastStep)
      {
         mLastStep = step;
         const int idx = (int)(((step % steps) + steps) % steps);
         mCurrentStepReadout.store(idx, std::memory_order_relaxed);

         if (mEnabled[idx].load(std::memory_order_relaxed))
         {
            if (mCurrentOutNote >= 0)
            {
               NoteEvent off;
               off.note = mCurrentOutNote;
               off.velocity = 0.0f;
               off.isNoteOn = false;
               off.frameOffset = 0;
               off.source = this;
               mOutbox.Push(off);
               mPendingOffActive = false;
            }

            NoteEvent on;
            on.note = std::clamp(mNote[idx].load(std::memory_order_relaxed), 0, 127);
            on.velocity = std::clamp(mVelocity[idx].load(std::memory_order_relaxed), 0.0f, 1.0f);
            on.isNoteOn = true;
            on.frameOffset = 0;
            on.source = this;
            mOutbox.Push(on);
            mCurrentOutNote = on.note;

            const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);
            const double stepSamples = rateBeats * samplesPerBeat;
            const double gateSamples = stepSamples * (double)gatePercent / 100.0;
            mPendingOffSample = mSamplePos + (uint64_t)gateSamples;
            mPendingOffActive = true;
         }
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const NoteSequencerNode& n)
   {
      mSteps.store(n.steps, std::memory_order_relaxed);
      mRateMode.store(n.rateMode, std::memory_order_relaxed);
      mRateBeats.store(n.rateBeats, std::memory_order_relaxed);
      mRateSeconds.store(n.rateSeconds, std::memory_order_relaxed);
      mGatePercent.store(n.gatePercent, std::memory_order_relaxed);
      for (int i = 0; i < NoteSequencerNode::kMaxSteps; i++)
      {
         mNote[i].store(n.stepNote[i], std::memory_order_relaxed);
         mVelocity[i].store(n.stepVelocity[i], std::memory_order_relaxed);
         mEnabled[i].store(n.stepEnabled[i], std::memory_order_relaxed);
      }
   }

   int CurrentStep() const { return mCurrentStepReadout.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;

   long long mLastStep = -1;
   int mCurrentOutNote = -1;
   bool mPendingOffActive = false;
   uint64_t mPendingOffSample = 0;

   std::atomic<int> mSteps { 8 };
   std::atomic<int> mRateMode { 0 };
   std::atomic<float> mRateBeats { 0.25f };
   std::atomic<float> mRateSeconds { 0.2f };
   std::atomic<float> mGatePercent { 70.0f };
   std::atomic<int> mNote[NoteSequencerNode::kMaxSteps] = {};
   std::atomic<float> mVelocity[NoteSequencerNode::kMaxSteps] = {};
   std::atomic<bool> mEnabled[NoteSequencerNode::kMaxSteps] = {};
   std::atomic<int> mCurrentStepReadout { -1 };
};

NoteSequencerNode::NoteSequencerNode()
{
   for (int i = 0; i < kMaxSteps; i++)
   {
      stepNote[i] = 60;
      stepVelocity[i] = 0.8f;
      stepEnabled[i] = true;
   }
}
NoteSequencerNode::~NoteSequencerNode() = default;

void NoteSequencerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteSequencerNode>();
   mAudioNode->PushParams(*this);
}

void NoteSequencerNode::VisitParams(ParamVisitor& v)
{
   v.Int("steps", steps);
   v.Int("rateMode", rateMode);
   v.Float("rateBeats", rateBeats);
   v.Float("rateSeconds", rateSeconds);
   v.Float("gatePercent", gatePercent);
   for (int i = 0; i < kMaxSteps; i++)
   {
      char key[16];
      snprintf(key, sizeof(key), "note%d", i);
      v.Int(key, stepNote[i]);
      snprintf(key, sizeof(key), "vel%d", i);
      v.Float(key, stepVelocity[i]);
      snprintf(key, sizeof(key), "en%d", i);
      v.Bool(key, stepEnabled[i]);
   }
}

AudioNode* NoteSequencerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteSequencerNode>();
   return mAudioNode.get();
}

int NoteSequencerNode::CurrentStep() const
{
   return mAudioNode ? mAudioNode->CurrentStep() : -1;
}

// ------------------------------------------------------- Random Note Generator
class AudioRandomNoteGeneratorNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mLastStep = -1;
      mCurrentOutNote = -1;
      mPendingOffActive = false;
      mPrevNote = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int rangeLow = mRangeLow.load(std::memory_order_relaxed);
      const int rangeHigh = std::max(rangeLow, mRangeHigh.load(std::memory_order_relaxed));
      const int scale = mScale.load(std::memory_order_relaxed);
      const int root = mRoot.load(std::memory_order_relaxed);
      const int rateMode = mRateMode.load(std::memory_order_relaxed);
      const float rateBeatsP = std::max(0.015625f, mRateBeats.load(std::memory_order_relaxed));
      const float rateSecondsP = std::max(0.01f, mRateSeconds.load(std::memory_order_relaxed));
      const int maxStep = std::max(1, mMaxStep.load(std::memory_order_relaxed));
      const double bpm = (double)Transport::Instance().Tempo();
      const double rateBeats = std::max(0.001, rateMode == 1 ? (double)rateSecondsP * bpm / 60.0 : (double)rateBeatsP);

      if (mPendingOffActive && mPendingOffSample >= mSamplePos && mPendingOffSample < mSamplePos + (uint64_t)numFrames)
      {
         NoteEvent off;
         off.note = mCurrentOutNote;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = (int)(mPendingOffSample - mSamplePos);
         off.source = this;
         mOutbox.Push(off);
         mPendingOffActive = false;
         mCurrentOutNote = -1;
      }

      const double beats = Transport::Instance().Beats();
      const long long step = (long long)std::floor(beats / rateBeats);
      if (step != mLastStep)
      {
         mLastStep = step;

         const int base = (mPrevNote >= 0) ? mPrevNote : (rangeLow + rangeHigh) / 2;
         const int delta = (int)std::lround(mRng.Next() * (float)maxStep);
         const int raw = std::clamp(base + delta, 0, 127);
         const int snapped = MusicTime::SnapToScale(raw, root, scale, MusicTime::kSnapNearest);
         const int note = std::clamp(snapped, rangeLow, rangeHigh);
         mPrevNote = note;
         mLastNoteReadout.store(note, std::memory_order_relaxed);

         if (mCurrentOutNote >= 0)
         {
            NoteEvent off;
            off.note = mCurrentOutNote;
            off.velocity = 0.0f;
            off.isNoteOn = false;
            off.frameOffset = 0;
            off.source = this;
            mOutbox.Push(off);
            mPendingOffActive = false;
         }

         NoteEvent on;
         on.note = note;
         on.velocity = 0.6f + (mRng.Next() * 0.5f + 0.5f) * 0.3f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         on.source = this;
         mOutbox.Push(on);
         mCurrentOutNote = note;

         const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);
         const double stepSamples = rateBeats * samplesPerBeat;
         mPendingOffSample = mSamplePos + (uint64_t)(stepSamples * 0.7); // fixed 70% gate
         mPendingOffActive = true;
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const RandomNoteGeneratorNode& n)
   {
      mRangeLow.store(n.rangeLow, std::memory_order_relaxed);
      mRangeHigh.store(n.rangeHigh, std::memory_order_relaxed);
      mScale.store(n.scale, std::memory_order_relaxed);
      mRoot.store(n.root, std::memory_order_relaxed);
      mRateMode.store(n.rateMode, std::memory_order_relaxed);
      mRateBeats.store(n.rateBeats, std::memory_order_relaxed);
      mRateSeconds.store(n.rateSeconds, std::memory_order_relaxed);
      mMaxStep.store(n.maxStep, std::memory_order_relaxed);
   }

   int LastNote() const { return mLastNoteReadout.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;

   long long mLastStep = -1;
   int mCurrentOutNote = -1;
   int mPrevNote = -1;
   bool mPendingOffActive = false;
   uint64_t mPendingOffSample = 0;

   std::atomic<int> mRangeLow { 48 };
   std::atomic<int> mRangeHigh { 72 };
   std::atomic<int> mScale { 13 };
   std::atomic<int> mRoot { 0 };
   std::atomic<int> mRateMode { 0 };
   std::atomic<float> mRateBeats { 0.25f };
   std::atomic<float> mRateSeconds { 0.2f };
   std::atomic<int> mMaxStep { 4 };
   std::atomic<int> mLastNoteReadout { -1 };
};

RandomNoteGeneratorNode::RandomNoteGeneratorNode() = default;
RandomNoteGeneratorNode::~RandomNoteGeneratorNode() = default;

void RandomNoteGeneratorNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioRandomNoteGeneratorNode>();
   mAudioNode->PushParams(*this);
}

void RandomNoteGeneratorNode::VisitParams(ParamVisitor& v)
{
   v.Int("rangeLow", rangeLow);
   v.Int("rangeHigh", rangeHigh);
   v.Int("scale", scale);
   v.Int("root", root);
   v.Int("rateMode", rateMode);
   v.Float("rateBeats", rateBeats);
   v.Float("rateSeconds", rateSeconds);
   v.Int("maxStep", maxStep);
}

AudioNode* RandomNoteGeneratorNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioRandomNoteGeneratorNode>();
   return mAudioNode.get();
}

int RandomNoteGeneratorNode::LastNote() const
{
   return mAudioNode ? mAudioNode->LastNote() : -1;
}

// ----------------------------------------------------------------- Chorder
class AudioChorderNode : public AudioNode
{
public:
   static constexpr int kMaxPending = 128;
   static constexpr int kMaxChordNotes = 7; // 6 stacked tones + 1 upper harmonic

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mLastStep = -1;
      for (int i = 0; i < kMaxPending; i++)
         mPending[i].active = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int scale = mScale.load(std::memory_order_relaxed);
      const int root = mRoot.load(std::memory_order_relaxed);
      const int chordSize = std::clamp(mChordSize.load(std::memory_order_relaxed), 2, 6);
      const float rateBeats = std::max(0.015625f, mRateBeats.load(std::memory_order_relaxed));
      const float strumMs = std::max(0.0f, mStrumMs.load(std::memory_order_relaxed));
      const float humanizeMs = std::max(0.0f, mHumanizeTimingMs.load(std::memory_order_relaxed));
      const float humanizeVel = std::max(0.0f, mHumanizeVelocity.load(std::memory_order_relaxed));
      const float upperHarmonics = std::clamp(mUpperHarmonics.load(std::memory_order_relaxed), 0.0f, 100.0f);

      const double beats = Transport::Instance().Beats();
      const long long step = (long long)std::floor(beats / (double)rateBeats);
      if (step != mLastStep)
      {
         mLastStep = step;

         const int degreeCount = std::max(1, MusicTime::ScaleTable(scale).count);
         const int startDegree = (int)((mRng.Next() * 0.5f + 0.5f) * (float)degreeCount);
         const double bpm = (double)Transport::Instance().Tempo();
         const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);
         const double strumSamples = strumMs * 0.001 * mSampleRate;
         const double gateSamples = (double)rateBeats * samplesPerBeat * 0.6; // fixed 60% gate

         int notes[kMaxChordNotes];
         int n = 0;
         for (int i = 0; i < chordSize && n < kMaxChordNotes; i++)
         {
            notes[n++] = std::clamp(MusicTime::DegreeToNote(startDegree + i * 2, 4, root, scale), 0, 127);
         }
         if (n < kMaxChordNotes && (mRng.Next() * 0.5f + 0.5f) * 100.0f < upperHarmonics)
         {
            const int pick = notes[(int)((mRng.Next() * 0.5f + 0.5f) * (float)n) % std::max(1, n)];
            notes[n++] = std::clamp(pick + 12, 0, 127);
         }
         mLastChordSizeReadout.store(n, std::memory_order_relaxed);

         for (int i = 0; i < n; i++)
         {
            const double jitter = humanizeMs > 0.0f ? (double)(mRng.Next() * humanizeMs * 0.001 * mSampleRate) : 0.0;
            const uint64_t onset = mSamplePos + (uint64_t)std::max(0.0, (double)i * strumSamples + jitter);
            float vel = 0.75f;
            if (humanizeVel > 0.0f)
               vel = std::clamp(vel + mRng.Next() * (humanizeVel / 100.0f) * 0.5f, 0.05f, 1.0f);

            if (Pending* on = FreeSlot())
            {
               on->active = true;
               on->note = notes[i];
               on->velocity = vel;
               on->isNoteOn = true;
               on->targetSample = onset;
            }
            if (Pending* off = FreeSlot())
            {
               off->active = true;
               off->note = notes[i];
               off->velocity = 0.0f;
               off->isNoteOn = false;
               off->targetSample = onset + (uint64_t)gateSamples;
            }
         }
      }

      for (int i = 0; i < kMaxPending; i++)
      {
         Pending& p = mPending[i];
         if (!p.active)
            continue;
         if (p.targetSample >= mSamplePos && p.targetSample < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent out;
            out.note = p.note;
            out.velocity = p.velocity;
            out.isNoteOn = p.isNoteOn;
            out.frameOffset = (int)(p.targetSample - mSamplePos);
            out.source = this;
            mOutbox.Push(out);
            p.active = false;
         }
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const ChorderNode& n)
   {
      mScale.store(n.scale, std::memory_order_relaxed);
      mRoot.store(n.root, std::memory_order_relaxed);
      mChordSize.store(n.chordSize, std::memory_order_relaxed);
      mRateBeats.store(n.rateBeats, std::memory_order_relaxed);
      mStrumMs.store(n.strumMs, std::memory_order_relaxed);
      mHumanizeTimingMs.store(n.humanizeTimingMs, std::memory_order_relaxed);
      mHumanizeVelocity.store(n.humanizeVelocity, std::memory_order_relaxed);
      mUpperHarmonics.store(n.upperHarmonics, std::memory_order_relaxed);
   }

   int LastChordSize() const { return mLastChordSizeReadout.load(std::memory_order_relaxed); }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
   };

   Pending* FreeSlot()
   {
      for (int i = 0; i < kMaxPending; i++)
         if (!mPending[i].active)
            return &mPending[i];
      return nullptr;
   }

   NoteEventQueue mOutbox;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;
   long long mLastStep = -1;
   Pending mPending[kMaxPending];

   std::atomic<int> mScale { 0 };
   std::atomic<int> mRoot { 0 };
   std::atomic<int> mChordSize { 3 };
   std::atomic<float> mRateBeats { 1.0f };
   std::atomic<float> mStrumMs { 15.0f };
   std::atomic<float> mHumanizeTimingMs { 10.0f };
   std::atomic<float> mHumanizeVelocity { 15.0f };
   std::atomic<float> mUpperHarmonics { 20.0f };
   std::atomic<int> mLastChordSizeReadout { 0 };
};

ChorderNode::ChorderNode() = default;
ChorderNode::~ChorderNode() = default;

void ChorderNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioChorderNode>();
   mAudioNode->PushParams(*this);
}

void ChorderNode::VisitParams(ParamVisitor& v)
{
   v.Int("scale", scale);
   v.Int("root", root);
   v.Int("chordSize", chordSize);
   v.Float("rateBeats", rateBeats);
   v.Float("strumMs", strumMs);
   v.Float("humanizeTimingMs", humanizeTimingMs);
   v.Float("humanizeVelocity", humanizeVelocity);
   v.Float("upperHarmonics", upperHarmonics);
}

AudioNode* ChorderNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioChorderNode>();
   return mAudioNode.get();
}

int ChorderNode::LastChordSize() const
{
   return mAudioNode ? mAudioNode->LastChordSize() : 0;
}

// ------------------------------------------------------------- Scale Notes
class AudioScaleNotesNode : public AudioNode
{
public:
   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const int scale = mScale.load(std::memory_order_relaxed);
      const int root = mRoot.load(std::memory_order_relaxed);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;
      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         NoteEvent out = in;
         if (in.note >= 0 && in.note <= 127)
         {
            const int snapped = MusicTime::SnapToScale(in.note, root, scale, MusicTime::kSnapNearest);
            out.note = std::clamp(snapped, 0, 127);
            if (in.isNoteOn)
               mLastNoteOut.store(out.note, std::memory_order_relaxed);
         }
         out.source = this;
         mOutbox.Push(out);
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const ScaleNotesNode& n)
   {
      mScale.store(n.scale, std::memory_order_relaxed);
      mRoot.store(n.root, std::memory_order_relaxed);
   }

   int LastNoteOut() const { return mLastNoteOut.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;

   std::atomic<int> mScale { 0 };
   std::atomic<int> mRoot { 0 };
   std::atomic<int> mLastNoteOut { -1 };
};

ScaleNotesNode::ScaleNotesNode() = default;
ScaleNotesNode::~ScaleNotesNode() = default;

void ScaleNotesNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioScaleNotesNode>();
   mAudioNode->PushParams(*this);
}

void ScaleNotesNode::VisitParams(ParamVisitor& v)
{
   v.Int("scale", scale);
   v.Int("root", root);
}

AudioNode* ScaleNotesNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioScaleNotesNode>();
   return mAudioNode.get();
}

int ScaleNotesNode::LastNoteOut() const
{
   return mAudioNode ? mAudioNode->LastNoteOut() : -1;
}

// -------------------------------------------------------------- Note Capturer
class AudioNoteCapturerNode : public AudioNode
{
public:
   enum Command { kNone, kStartRecord, kStopRecord, kStartPlay, kStopPlay, kClear };

   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      // Deliberately does NOT touch mRecordedCount/mRecorded - a topology
      // rebuild (any cable connect/disconnect anywhere in the patch, not
      // just this node's own input) must not erase a take. Only the
      // transient transport/voice-safety state resets here.
      mRecording = false;
      mPlaying = false;
      for (int i = 0; i < 128; i++)
         mSounding[i] = false;
      mHeldBits[0].store(0, std::memory_order_relaxed);
      mHeldBits[1].store(0, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const double beats = Transport::Instance().Beats();

      switch (mCommand.exchange(kNone, std::memory_order_relaxed))
      {
      case kStartRecord:
         mRecordedCount = 0;
         mRecording = true;
         mPlaying = false;
         mRecordStartBeat = beats;
         break;
      case kStopRecord:
      {
         mRecording = false;
         mRecordedLengthBeats = std::max(0.03125, beats - mRecordStartBeat);
         const int quantizeDiv = mQuantizeDiv.load(std::memory_order_relaxed);
         if (quantizeDiv > 0 && quantizeDiv < kNumQuantizeDiv)
         {
            const double gridBeats = kQuantizeBeats[quantizeDiv];
            for (int i = 0; i < mRecordedCount; i++)
               mRecorded[i].beat = std::max(0.0, std::round(mRecorded[i].beat / gridBeats) * gridBeats);
         }
         break;
      }
      case kStartPlay:
         if (mRecordedCount > 0)
         {
            mPlaying = true;
            mPlayStartBeat = beats;
            mPlayIndex = 0;
         }
         break;
      case kStopPlay:
         mPlaying = false;
         ForceOffAllSounding();
         break;
      case kClear:
         mRecordedCount = 0;
         mRecording = false;
         mPlaying = false;
         ForceOffAllSounding();
         break;
      default:
         break;
      }

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;
      for (int i = 0; i < n; i++)
      {
         const NoteEvent& e = evts[i];
         // Live monitor: input always passes straight through.
         NoteEvent thru = e;
         thru.source = this;
         mOutbox.Push(thru);
         if (e.note >= 0 && e.note <= 127)
            SetKeyBit(mHeldBits, e.note, e.isNoteOn);

         if (mRecording && e.note >= 0 && e.note <= 127 && mRecordedCount < NoteCapturerNode::kMaxEvents)
            mRecorded[mRecordedCount++] = { e.note, e.velocity, e.isNoteOn, beats - mRecordStartBeat };
      }

      if (mPlaying)
      {
         double playhead = beats - mPlayStartBeat;
         const double loopLen = std::max(0.03125, mRecordedLengthBeats);
         if (playhead >= loopLen)
         {
            if (loop)
            {
               mPlayStartBeat += loopLen;
               mPlayIndex = 0;
               ForceOffAllSounding();
               playhead = beats - mPlayStartBeat;
            }
            else
            {
               mPlaying = false;
               ForceOffAllSounding();
            }
         }

         if (mPlaying)
         {
            while (mPlayIndex < mRecordedCount && mRecorded[mPlayIndex].beat <= playhead)
            {
               const RecEvt& r = mRecorded[mPlayIndex];
               NoteEvent out;
               out.note = r.note;
               out.velocity = r.velocity;
               out.isNoteOn = r.isNoteOn;
               out.frameOffset = 0;
               out.source = this;
               mOutbox.Push(out);
               if (r.note >= 0 && r.note <= 127)
               {
                  mSounding[r.note] = r.isNoteOn;
                  SetKeyBit(mHeldBits, r.note, r.isNoteOn);
               }
               mPlayIndex++;
            }
         }
         mPlayheadReadout.store(playhead / loopLen, std::memory_order_relaxed);
      }

      mRecordingReadout.store(mRecording, std::memory_order_relaxed);
      mPlayingReadout.store(mPlaying, std::memory_order_relaxed);
      mRecordedCountReadout.store(mRecordedCount, std::memory_order_relaxed);
      mRecordedLengthReadout.store((float)(mRecording ? (beats - mRecordStartBeat) : mRecordedLengthBeats),
                                   std::memory_order_relaxed);
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteCapturerNode& n)
   {
      mLoop.store(n.loop, std::memory_order_relaxed);
      mQuantizeDiv.store(n.quantizeDiv, std::memory_order_relaxed);
   }
   void SendCommand(Command c) { mCommand.store(c, std::memory_order_relaxed); }

   bool IsRecording() const { return mRecordingReadout.load(std::memory_order_relaxed); }
   bool IsPlaying() const { return mPlayingReadout.load(std::memory_order_relaxed); }
   double RecordedLengthBeats() const { return (double)mRecordedLengthReadout.load(std::memory_order_relaxed); }
   int RecordedCount() const { return mRecordedCountReadout.load(std::memory_order_relaxed); }
   double Playhead01() const { return (double)mPlayheadReadout.load(std::memory_order_relaxed); }

   // Main thread only - reads the same fixed array the audio thread wrote,
   // tolerating the rare torn read the same way the held-key bitmap does.
   int RecordedNoteOns(int outNote[NoteCapturerNode::kMaxEvents], float outBeat[NoteCapturerNode::kMaxEvents]) const
   {
      const int count = std::min(mRecordedCount, NoteCapturerNode::kMaxEvents);
      int n = 0;
      for (int i = 0; i < count; i++)
      {
         if (!mRecorded[i].isNoteOn)
            continue;
         outNote[n] = mRecorded[i].note;
         outBeat[n] = (float)mRecorded[i].beat;
         n++;
      }
      return n;
   }

private:
   struct RecEvt
   {
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      double beat = 0.0;
   };

   void ForceOffAllSounding()
   {
      for (int note = 0; note < 128; note++)
      {
         if (!mSounding[note])
            continue;
         NoteEvent off;
         off.note = note;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = 0;
         off.source = this;
         mOutbox.Push(off);
         mSounding[note] = false;
         SetKeyBit(mHeldBits, note, false);
      }
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;

   RecEvt mRecorded[NoteCapturerNode::kMaxEvents];
   int mRecordedCount = 0;
   bool mRecording = false;
   bool mPlaying = false;
   double mRecordStartBeat = 0.0;
   double mRecordedLengthBeats = 1.0;
   double mPlayStartBeat = 0.0;
   int mPlayIndex = 0;
   bool mSounding[128] = {};
   std::atomic<uint64_t> mHeldBits[2] { { 0 }, { 0 } };

   std::atomic<int> mCommand { kNone };
   std::atomic<bool> mLoop { true };
   std::atomic<int> mQuantizeDiv { 0 };
   std::atomic<bool> mRecordingReadout { false };
   std::atomic<bool> mPlayingReadout { false };
   std::atomic<int> mRecordedCountReadout { 0 };
   std::atomic<float> mRecordedLengthReadout { 1.0f };
   std::atomic<float> mPlayheadReadout { 0.0f };

public:
   uint64_t HeldWord(int w) const { return mHeldBits[w].load(std::memory_order_relaxed); }
};

NoteCapturerNode::NoteCapturerNode() = default;
NoteCapturerNode::~NoteCapturerNode() = default;

void NoteCapturerNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteCapturerNode>();
   mAudioNode->PushParams(*this);
}

void NoteCapturerNode::VisitParams(ParamVisitor& v)
{
   v.Bool("loop", loop);
   v.Int("quantizeDiv", quantizeDiv);
}

AudioNode* NoteCapturerNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteCapturerNode>();
   return mAudioNode.get();
}

void NoteCapturerNode::StartRecording()
{
   GetAudioNode();
   mAudioNode->SendCommand(AudioNoteCapturerNode::kStartRecord);
}

void NoteCapturerNode::StopRecording()
{
   GetAudioNode();
   mAudioNode->SendCommand(AudioNoteCapturerNode::kStopRecord);
}

void NoteCapturerNode::StartPlayback()
{
   GetAudioNode();
   mAudioNode->SendCommand(AudioNoteCapturerNode::kStartPlay);
}

void NoteCapturerNode::StopPlayback()
{
   GetAudioNode();
   mAudioNode->SendCommand(AudioNoteCapturerNode::kStopPlay);
}

void NoteCapturerNode::ClearRecording()
{
   GetAudioNode();
   mAudioNode->SendCommand(AudioNoteCapturerNode::kClear);
}

bool NoteCapturerNode::IsRecording() const
{
   return mAudioNode ? mAudioNode->IsRecording() : false;
}

bool NoteCapturerNode::IsPlaying() const
{
   return mAudioNode ? mAudioNode->IsPlaying() : false;
}

double NoteCapturerNode::RecordedLengthBeats() const
{
   return mAudioNode ? mAudioNode->RecordedLengthBeats() : 0.0;
}

int NoteCapturerNode::RecordedCount() const
{
   return mAudioNode ? mAudioNode->RecordedCount() : 0;
}

double NoteCapturerNode::Playhead01() const
{
   return mAudioNode ? mAudioNode->Playhead01() : 0.0;
}

int NoteCapturerNode::RecordedNoteOns(int outNote[kMaxEvents], float outBeat[kMaxEvents]) const
{
   return mAudioNode ? mAudioNode->RecordedNoteOns(outNote, outBeat) : 0;
}

void NoteCapturerNode::HeldKeys(bool out[128]) const
{
   const uint64_t w0 = mAudioNode ? mAudioNode->HeldWord(0) : 0;
   const uint64_t w1 = mAudioNode ? mAudioNode->HeldWord(1) : 0;
   for (int i = 0; i < 64; i++)
   {
      out[i] = (w0 >> i) & 1ull;
      out[i + 64] = (w1 >> i) & 1ull;
   }
}

// ------------------------------------------------------------- Bouncing Balls
class AudioBouncingBallsNode : public AudioNode
{
public:
   static constexpr float kBound = BouncingBallsNode::kBound;
   static constexpr int kMaxPending = 64;

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      for (int i = 0; i < BouncingBallsNode::kMaxBalls; i++)
         mInitialized[i] = false;
      for (int i = 0; i < kMaxPending; i++)
         mPending[i].active = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int shape = mShape.load(std::memory_order_relaxed);
      const int numBalls = std::clamp(mNumBalls.load(std::memory_order_relaxed), 1, BouncingBallsNode::kMaxBalls);
      const float ballSize = std::clamp(mBallSize.load(std::memory_order_relaxed), 0.01f, 0.3f);
      const float ballSpeed = std::max(0.0f, mBallSpeed.load(std::memory_order_relaxed));
      const int rangeLow = mRangeLow.load(std::memory_order_relaxed);
      const int rangeHigh = std::max(rangeLow, mRangeHigh.load(std::memory_order_relaxed));
      const double dt = (double)numFrames / mSampleRate;

      for (int i = 0; i < numBalls; i++)
      {
         if (!mInitialized[i])
         {
            const float angle = mRng.Next() * 3.14159265f;
            mX[i] = mRng.Next() * kBound * 0.4f;
            mY[i] = mRng.Next() * kBound * 0.4f;
            mVX[i] = std::cos(angle);
            mVY[i] = std::sin(angle);
            mInitialized[i] = true;
         }

         const float speed = std::sqrt(mVX[i] * mVX[i] + mVY[i] * mVY[i]);
         if (speed > 0.0001f)
         {
            mVX[i] /= speed;
            mVY[i] /= speed;
         }
         mX[i] += mVX[i] * ballSpeed * (float)dt;
         mY[i] += mVY[i] * ballSpeed * (float)dt;

         bool collided = false;
         if (shape == BouncingBallsNode::kSquare)
         {
            if (mX[i] + ballSize > kBound) { mVX[i] = -std::fabs(mVX[i]); mX[i] = kBound - ballSize; collided = true; }
            if (mX[i] - ballSize < -kBound) { mVX[i] = std::fabs(mVX[i]); mX[i] = -kBound + ballSize; collided = true; }
            if (mY[i] + ballSize > kBound) { mVY[i] = -std::fabs(mVY[i]); mY[i] = kBound - ballSize; collided = true; }
            if (mY[i] - ballSize < -kBound) { mVY[i] = std::fabs(mVY[i]); mY[i] = -kBound + ballSize; collided = true; }
         }
         else if (shape == BouncingBallsNode::kTriangle)
         {
            // Equilateral triangle inscribed in radius kBound, point up.
            static const float kVx[3] = { 0.0f, -0.8660254f * kBound, 0.8660254f * kBound };
            static const float kVy[3] = { kBound, -0.5f * kBound, -0.5f * kBound };
            for (int e = 0; e < 3 && !collided; e++)
            {
               const int e2 = (e + 1) % 3;
               const float ex = kVx[e2] - kVx[e], ey = kVy[e2] - kVy[e];
               float nx = ey, ny = -ex;
               const float nlen = std::sqrt(nx * nx + ny * ny);
               if (nlen < 0.0001f)
                  continue;
               nx /= nlen;
               ny /= nlen;
               // Outward normal points away from the centroid (origin).
               if (nx * (0.0f - kVx[e]) + ny * (0.0f - kVy[e]) > 0.0f) { nx = -nx; ny = -ny; }
               const float signedDist = nx * (mX[i] - kVx[e]) + ny * (mY[i] - kVy[e]);
               if (signedDist + ballSize > 0.0f)
               {
                  const float dot = mVX[i] * nx + mVY[i] * ny;
                  mVX[i] -= 2.0f * dot * nx;
                  mVY[i] -= 2.0f * dot * ny;
                  const float pen = signedDist + ballSize;
                  mX[i] -= nx * pen;
                  mY[i] -= ny * pen;
                  collided = true;
               }
            }
         }
         else // kCircle
         {
            const float dist = std::sqrt(mX[i] * mX[i] + mY[i] * mY[i]);
            if (dist + ballSize > kBound && dist > 0.0001f)
            {
               const float nx = mX[i] / dist, ny = mY[i] / dist;
               const float dot = mVX[i] * nx + mVY[i] * ny;
               mVX[i] -= 2.0f * dot * nx;
               mVY[i] -= 2.0f * dot * ny;
               const float pen = dist + ballSize - kBound;
               mX[i] -= nx * pen;
               mY[i] -= ny * pen;
               collided = true;
            }
         }

         if (collided)
         {
            const int span = std::max(0, rangeHigh - rangeLow);
            const int note = rangeLow + (int)((mRng.Next() * 0.5f + 0.5f) * (float)(span + 1));
            if (Pending* on = FreeSlot())
            {
               on->active = true;
               on->note = std::clamp(note, 0, 127);
               on->velocity = 0.6f + (mRng.Next() * 0.5f + 0.5f) * 0.3f;
               on->isNoteOn = true;
               on->targetSample = mSamplePos;
            }
            if (Pending* off = FreeSlot())
            {
               off->active = true;
               off->note = std::clamp(note, 0, 127);
               off->velocity = 0.0f;
               off->isNoteOn = false;
               off->targetSample = mSamplePos + (uint64_t)(0.15 * mSampleRate);
            }
         }

         // Flash level: snaps to 1 on collision, decays over ~250ms so the
         // UI can briefly recolor a ball that just hit the wall.
         if (collided)
            mFlash[i] = 1.0f;
         else
            mFlash[i] = std::max(0.0f, mFlash[i] - (float)(dt / 0.25));
         mFlashReadout[i].store(mFlash[i], std::memory_order_relaxed);

         mXReadout[i].store(mX[i], std::memory_order_relaxed);
         mYReadout[i].store(mY[i], std::memory_order_relaxed);
      }

      for (int i = 0; i < kMaxPending; i++)
      {
         Pending& p = mPending[i];
         if (!p.active)
            continue;
         if (p.targetSample >= mSamplePos && p.targetSample < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent out;
            out.note = p.note;
            out.velocity = p.velocity;
            out.isNoteOn = p.isNoteOn;
            out.frameOffset = (int)(p.targetSample - mSamplePos);
            out.source = this;
            mOutbox.Push(out);
            p.active = false;
         }
      }

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const BouncingBallsNode& n)
   {
      mShape.store(n.shape, std::memory_order_relaxed);
      mNumBalls.store(n.numBalls, std::memory_order_relaxed);
      mBallSize.store(n.ballSize, std::memory_order_relaxed);
      mBallSpeed.store(n.ballSpeed, std::memory_order_relaxed);
      mRangeLow.store(n.rangeLow, std::memory_order_relaxed);
      mRangeHigh.store(n.rangeHigh, std::memory_order_relaxed);
   }

   int BallPositions(float outX[BouncingBallsNode::kMaxBalls], float outY[BouncingBallsNode::kMaxBalls],
                     float outFlash[BouncingBallsNode::kMaxBalls]) const
   {
      const int numBalls = std::clamp(mNumBalls.load(std::memory_order_relaxed), 1, BouncingBallsNode::kMaxBalls);
      for (int i = 0; i < numBalls; i++)
      {
         outX[i] = mXReadout[i].load(std::memory_order_relaxed);
         outY[i] = mYReadout[i].load(std::memory_order_relaxed);
         outFlash[i] = mFlashReadout[i].load(std::memory_order_relaxed);
      }
      return numBalls;
   }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
   };

   Pending* FreeSlot()
   {
      for (int i = 0; i < kMaxPending; i++)
         if (!mPending[i].active)
            return &mPending[i];
      return nullptr;
   }

   NoteEventQueue mOutbox;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;
   Pending mPending[kMaxPending];

   bool mInitialized[BouncingBallsNode::kMaxBalls] = {};
   float mX[BouncingBallsNode::kMaxBalls] = {};
   float mY[BouncingBallsNode::kMaxBalls] = {};
   float mVX[BouncingBallsNode::kMaxBalls] = {};
   float mVY[BouncingBallsNode::kMaxBalls] = {};
   std::atomic<float> mXReadout[BouncingBallsNode::kMaxBalls] = {};
   std::atomic<float> mYReadout[BouncingBallsNode::kMaxBalls] = {};
   float mFlash[BouncingBallsNode::kMaxBalls] = {};
   std::atomic<float> mFlashReadout[BouncingBallsNode::kMaxBalls] = {};

   std::atomic<int> mShape { BouncingBallsNode::kCircle };
   std::atomic<int> mNumBalls { 4 };
   std::atomic<float> mBallSize { 0.06f };
   std::atomic<float> mBallSpeed { 0.6f };
   std::atomic<int> mRangeLow { 48 };
   std::atomic<int> mRangeHigh { 84 };
};

BouncingBallsNode::BouncingBallsNode() = default;
BouncingBallsNode::~BouncingBallsNode() = default;

void BouncingBallsNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBouncingBallsNode>();
   mAudioNode->PushParams(*this);
}

void BouncingBallsNode::VisitParams(ParamVisitor& v)
{
   v.Int("shape", shape);
   v.Int("numBalls", numBalls);
   v.Float("ballSize", ballSize);
   v.Float("ballSpeed", ballSpeed);
   v.Int("rangeLow", rangeLow);
   v.Int("rangeHigh", rangeHigh);
}

AudioNode* BouncingBallsNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioBouncingBallsNode>();
   return mAudioNode.get();
}

int BouncingBallsNode::BallPositions(float outX[kMaxBalls], float outY[kMaxBalls], float outFlash[kMaxBalls]) const
{
   return mAudioNode ? mAudioNode->BallPositions(outX, outY, outFlash) : 0;
}

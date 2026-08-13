#include "NoteNodes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/AudioVoice.h"
#include "audio/NoteEventQueue.h"
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
      mEnv.SetSampleRate(sampleRate);
      mEnv.SetADSR(mAttackMs.load(std::memory_order_relaxed), mDecayMs.load(std::memory_order_relaxed),
                   mSustainLevel.load(std::memory_order_relaxed), mReleaseMs.load(std::memory_order_relaxed));
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      mEnv.SetADSR(mAttackMs.load(std::memory_order_relaxed), mDecayMs.load(std::memory_order_relaxed),
                   mSustainLevel.load(std::memory_order_relaxed), mReleaseMs.load(std::memory_order_relaxed));

      const int numFrames = output.numFrames;
      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;
      int evtIdx = 0;
      float level = mEnv.Process(); // in case numFrames==0, still publish something sane

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

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

   // Small fixed-capacity NoteEvent::voiceId -> Value map, real-time-safe (no
   // allocation, linear scan bounded by Capacity - fine at the handful of
   // concurrent voices any of these nodes actually carry). Consumers that
   // used to key per-note bookkeeping with `T x[128]` can't do that for
   // voiceId: unlike a MIDI note, it isn't bounded 0-127, it only grows.
   template <typename Value, int Capacity>
   class VoiceIdMap
   {
   public:
      Value* Find(int voiceId)
      {
         for (auto& e : mEntries)
            if (e.used && e.voiceId == voiceId)
               return &e.value;
         return nullptr;
      }

      // Returns the existing entry for voiceId, or a freshly value-initialized
      // one. If the table is somehow full of *other* voiceIds (shouldn't
      // happen given bounded per-block event counts), overwrites slot 0
      // rather than silently dropping the voice.
      Value& GetOrInsert(int voiceId)
      {
         if (Value* v = Find(voiceId))
            return *v;
         for (auto& e : mEntries)
         {
            if (!e.used)
            {
               e.used = true;
               e.voiceId = voiceId;
               e.value = Value {};
               return e.value;
            }
         }
         mEntries[0] = Entry { true, voiceId, Value {} };
         return mEntries[0].value;
      }

      void Erase(int voiceId)
      {
         for (auto& e : mEntries)
         {
            if (e.used && e.voiceId == voiceId)
            {
               e.used = false;
               return;
            }
         }
      }

      void Clear()
      {
         for (auto& e : mEntries)
            e.used = false;
      }

      // fn(int voiceId, Value& value) -> bool; returning true erases the
      // entry once fn returns (safe to do from inside fn - this only flips
      // a flag on the current slot, no other entries move).
      template <typename Fn>
      void ForEach(Fn&& fn)
      {
         for (auto& e : mEntries)
         {
            if (e.used && fn(e.voiceId, e.value))
               e.used = false;
         }
      }

   private:
      struct Entry
      {
         bool used = false;
         int voiceId = 0;
         Value value {};
      };
      Entry mEntries[Capacity];
   };
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
         int note = msgs[i].note + transpose;
         if (note < 0 || note > 127)
            continue;

         if (mUseGlobalScale.load(std::memory_order_relaxed))
            note = std::clamp(MusicTime::SnapToScale(note, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest), 0, 127);

         NoteEvent e;
         e.note = note;
         e.velocity = std::clamp(msgs[i].velocity01 * velScale, 0.0f, 1.0f);
         e.isNoteOn = msgs[i].isNoteOn;
         // See MidiNotesNode's header comment: Platform's ring carries no
         // sample timestamp, so everything lands at the block boundary.
         e.frameOffset = 0;
         e.source = this;
         if (e.isNoteOn)
         {
            e.voiceId = NextVoiceId();
            mActiveVoiceId[msgs[i].note] = e.voiceId;
            mMappedNote[msgs[i].note] = note;
         }
         else
         {
            e.voiceId = mActiveVoiceId[msgs[i].note];
            e.note = mMappedNote[msgs[i].note];
         }
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
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }

   uint64_t HeldWord(int w) const { return mHeld[w].load(std::memory_order_relaxed); }
   int LastNote() const { return mLastNote.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   unsigned long long mCursor = 0;
   int mActiveVoiceId[128] = {};
   int mMappedNote[128] = {};

   std::atomic<uint64_t> mHeld[2] { { 0 }, { 0 } };
   std::atomic<int> mLastNote { -1 };
   std::atomic<int> mChannel { -1 };
   std::atomic<int> mTranspose { 0 };
   std::atomic<float> mVelocityScale { 1.0f };
   std::atomic<bool> mUseGlobalScale { false };
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
   v.Bool("useGlobalScale", useGlobalScale);
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
      mPassingAs.Clear();
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const bool useGlobal = mUseGlobalScale.load(std::memory_order_relaxed);
      const int scale = useGlobal ? Transport::Instance().Scale() : mScale.load(std::memory_order_relaxed);
      const int root = useGlobal ? Transport::Instance().Key() : mRoot.load(std::memory_order_relaxed);
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

            mLastNoteIn.store(in.note, std::memory_order_relaxed);
            mLastPassed.store(pass, std::memory_order_relaxed);

            if (pass)
            {
               mPassingAs.GetOrInsert(in.voiceId) = snapped;
               NoteEvent out = in;
               out.note = snapped;
               out.voiceId = in.voiceId;
               out.source = this;
               mOutbox.Push(out);
            }
            else
            {
               mPassingAs.Erase(in.voiceId);
            }
         }
         else if (in.bendUpdate)
         {
            if (int* mapped = mPassingAs.Find(in.voiceId))
            {
               NoteEvent out = in;
               out.note = *mapped;
               out.voiceId = in.voiceId;
               out.source = this;
               mOutbox.Push(out);
            }
         }
         else
         {
            if (int* mapped = mPassingAs.Find(in.voiceId))
            {
               NoteEvent out = in;
               out.note = *mapped;
               out.voiceId = in.voiceId;
               out.source = this;
               mOutbox.Push(out);
               mPassingAs.Erase(in.voiceId);
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
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }

   int LastNoteIn() const { return mLastNoteIn.load(std::memory_order_relaxed); }
   bool LastPassed() const { return mLastPassed.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   VoiceIdMap<int, 128> mPassingAs;
   DspMath::WhiteNoise mRng;

   std::atomic<int> mScale { 13 };
   std::atomic<int> mRoot { 0 };
   std::atomic<int> mRangeLow { 0 };
   std::atomic<int> mRangeHigh { 127 };
   std::atomic<float> mChance { 100.0f };
   std::atomic<bool> mUseGlobalScale { false };
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
   v.Bool("useGlobalScale", useGlobalScale);
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

// Grid divisions quantizeDiv indexes into, in beats (a beat is a quarter
// note) - index 0 is "off" and never reaches this table. Shared by Quantizer
// and Note Capturer.
static const double kQuantizeBeats[] = { 1.0, 1.0, 0.5, 0.25, 0.125 };
static constexpr int kNumQuantizeDiv = 5;

// ------------------------------------------------------------- Note editors
// The eight nodes below are the note-modification surface, one concern per
// node - see the class comments on their declarations in NoteNodes.h. A
// small shared helper for the four that need to delay a note-on into a
// future block (Gate's internal note-off, Humanizer/Quantizer's delayed
// onset, Glide's glissando steps) - a single Pending-slot mechanism pulled
// out since four separate nodes need it rather than one.
namespace
{
   struct DeferredNote
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
      int voiceId = 0;
      float bendSemitones = 0.0f; // the input note-on's bend, carried through the delay
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
               out.bendSemitones = s.bendSemitones;
               out.source = source;
               out.voiceId = s.voiceId;
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
      mOutNote.Clear();
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
         out.voiceId = in.voiceId;

         if (in.isNoteOn)
         {
            int outNote = in.note + semi;
            if (mUseGlobalScale.load(std::memory_order_relaxed))
               outNote = MusicTime::SnapToScale(outNote, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
            outNote = std::clamp(outNote, 0, 127);
            mOutNote.GetOrInsert(in.voiceId) = outNote;
            out.note = outNote;
            out.velocity = in.velocity;
            out.isNoteOn = true;
            out.bendSemitones = in.bendSemitones;
            mOutbox.Push(out);
            mLastNoteOut.store(outNote, std::memory_order_relaxed);
         }
         else if (in.bendUpdate)
         {
            int* outNote = mOutNote.Find(in.voiceId);
            if (outNote == nullptr)
               continue;
            out.note = *outNote;
            out.velocity = in.velocity;
            out.isNoteOn = false;
            out.bendUpdate = true;
            out.bendSemitones = in.bendSemitones;
            mOutbox.Push(out);
         }
         else
         {
            int* outNote = mOutNote.Find(in.voiceId);
            if (outNote == nullptr)
               continue;
            out.note = *outNote;
            out.velocity = in.velocity;
            out.isNoteOn = false;
            mOutbox.Push(out);
            mOutNote.Erase(in.voiceId);
         }
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void PushParams(const NoteTransposeNode& n)
   {
      mSemitones.store(n.semitones, std::memory_order_relaxed);
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }
   int LastNoteOut() const { return mLastNoteOut.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   VoiceIdMap<int, 128> mOutNote;
   std::atomic<int> mSemitones { 0 };
   std::atomic<bool> mUseGlobalScale { false };
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
   mAudioNode->PushParams(*this);
}

void NoteTransposeNode::VisitParams(ParamVisitor& v)
{
   v.Int("semitones", semitones);
   v.Bool("useGlobalScale", useGlobalScale);
}

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

// ---- Pitch Bend ----
// A note-chain effect, not just a passthrough-with-offset like
// AudioSemitoneShiftNode above: it has to keep tracking every note it has
// forwarded for as long as that note is held (keyed by voiceId, same as
// every other stuck-note guard in this file), so a knob move mid-hold can
// re-emit a bendUpdate for it. `mHeld` also remembers the *upstream* bend
// each held note arrived with (its bendSemitones at note-on, or whatever an
// upstream Pitch Bend last re-based it to via its own bendUpdate) so this
// node's own contribution stays correctly additive no matter how many Pitch
// Bend nodes are stacked in the chain.
class AudioPitchBendNode : public AudioNode
{
public:
   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      mHeld.Clear();
      // Seed "last emitted" from the current knob value so the first block
      // after a (re)start doesn't immediately fire a spurious bendUpdate for
      // a note that only just attacked in that same block.
      mLastEmittedBend = mBend.load(std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      const float bend = mBend.load(std::memory_order_relaxed);

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];
         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            HeldNote& held = mHeld.GetOrInsert(in.voiceId);
            held.note = in.note;
            held.upstreamBend = in.bendSemitones;

            NoteEvent out = in;
            out.bendSemitones = in.bendSemitones + bend;
            out.source = this;
            mOutbox.Push(out);
         }
         else if (in.bendUpdate)
         {
            // An upstream Pitch Bend re-bent a note this node is also
            // holding - re-base against its new value and re-forward, but
            // only if we're actually still tracking this voice as held.
            HeldNote* held = mHeld.Find(in.voiceId);
            if (held == nullptr)
               continue;
            held->upstreamBend = in.bendSemitones;

            NoteEvent out = in;
            out.note = held->note;
            out.bendSemitones = in.bendSemitones + bend;
            out.bendUpdate = true;
            out.isNoteOn = false;
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
            mHeld.Erase(in.voiceId);
         }
      }

      // The knob itself moved this block - slide every note currently held,
      // whether it attacked before or after the last time we did this. This
      // is what lets Pitch Bend slide a note that's already sounding, the
      // one thing a note-on-time-only transpose can never do.
      if (bend != mLastEmittedBend)
      {
         mHeld.ForEach([&](int voiceId, HeldNote& held) -> bool
         {
            NoteEvent out;
            out.note = held.note;
            out.velocity = 0.0f;
            out.isNoteOn = false;
            out.frameOffset = 0;
            out.bendUpdate = true;
            out.bendSemitones = held.upstreamBend + bend;
            out.source = this;
            out.voiceId = voiceId;
            mOutbox.Push(out);
            return false; // still held - don't erase
         });
         mLastEmittedBend = bend;
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetBend(float b) { mBend.store(b, std::memory_order_relaxed); }

private:
   struct HeldNote
   {
      int note = -1;
      float upstreamBend = 0.0f;
   };

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   VoiceIdMap<HeldNote, 128> mHeld;
   float mLastEmittedBend = 0.0f; // audio-thread-only, not shared with the knob atomic

   std::atomic<float> mBend { 0.0f };
};

PitchBendNode::PitchBendNode() = default;
PitchBendNode::~PitchBendNode() = default;

void PitchBendNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPitchBendNode>();
   mAudioNode->SetBend(bendSemitones);
}

void PitchBendNode::VisitParams(ParamVisitor& v) { v.Float("bendSemitones", bendSemitones); }

AudioNode* PitchBendNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPitchBendNode>();
   return mAudioNode.get();
}

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
      mGateState.Clear();
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

            GateState& state = mGateState.GetOrInsert(in.voiceId);
            state.note = in.note;
            if (holdMs > 0.0f)
            {
               state.suppressOff = true;
               state.scheduledActive = true;
               state.scheduledTarget = mSamplePos + (uint64_t)in.frameOffset + (uint64_t)holdSamples;
            }
            else
            {
               state.suppressOff = false;
               state.scheduledActive = false;
            }
         }
         else if (in.bendUpdate)
         {
            // The note is still conceptually sounding even while its real
            // note-off is being suppressed for a scheduled internal hold -
            // forward unconditionally, ahead of (not through) the
            // suppressOff check below, and don't touch mGateState at all.
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            GateState* state = mGateState.Find(in.voiceId);
            if (state != nullptr && state->suppressOff)
               continue; // the scheduled internal note-off will close this voice
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
            mGateState.Erase(in.voiceId);
         }
      }

      mGateState.ForEach([&](int voiceId, GateState& state) -> bool
      {
         if (!state.scheduledActive)
            return false;
         if (state.scheduledTarget >= mSamplePos && state.scheduledTarget < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent out;
            out.note = state.note;
            out.velocity = 0.0f;
            out.isNoteOn = false;
            out.frameOffset = (int)(state.scheduledTarget - mSamplePos);
            out.source = this;
            out.voiceId = voiceId;
            mOutbox.Push(out);
            return true; // erase - voice fully closed
         }
         return false;
      });

      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void SetHoldMs(float ms) { mHoldMs.store(ms, std::memory_order_relaxed); }

private:
   struct GateState
   {
      int note = -1;
      bool suppressOff = false;
      bool scheduledActive = false;
      uint64_t scheduledTarget = 0;
   };

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   VoiceIdMap<GateState, 128> mGateState;
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
      mOutNote.Clear();
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
            mOutNote.GetOrInsert(in.voiceId) = in.note; // no pitch change - just bookkeeping for the matching note-off

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = in.note;
               out.velocity = v;
               out.isNoteOn = true;
               out.frameOffset = (int)(onsetAbs - mSamplePos);
               out.bendSemitones = in.bendSemitones;
               out.source = this;
               out.voiceId = in.voiceId;
               mOutbox.Push(out);
            }
            else if (DeferredNote* slot = mPending.FreeSlot())
            {
               slot->active = true;
               slot->note = in.note;
               slot->velocity = v;
               slot->isNoteOn = true;
               slot->targetSample = onsetAbs;
               slot->voiceId = in.voiceId;
               slot->bendSemitones = in.bendSemitones;
            }
         }
         else if (in.bendUpdate)
         {
            // No remapping here (Humanizer doesn't touch pitch) - just check
            // this voice is still one we're tracking, then forward as-is.
            if (mOutNote.Find(in.voiceId) == nullptr)
               continue;
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            if (mOutNote.Find(in.voiceId) == nullptr)
               continue;
            NoteEvent out = in;
            out.source = this;
            out.voiceId = in.voiceId;
            mOutbox.Push(out);
            mOutNote.Erase(in.voiceId);
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
   VoiceIdMap<int, 128> mOutNote;
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
      mOutNote.Clear();
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
            mOutNote.GetOrInsert(in.voiceId) = in.note;

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = in.note;
               out.velocity = in.velocity;
               out.isNoteOn = true;
               out.frameOffset = (int)(onsetAbs - mSamplePos);
               out.bendSemitones = in.bendSemitones;
               out.source = this;
               out.voiceId = in.voiceId;
               mOutbox.Push(out);
            }
            else if (DeferredNote* slot = mPending.FreeSlot())
            {
               slot->active = true;
               slot->note = in.note;
               slot->velocity = in.velocity;
               slot->isNoteOn = true;
               slot->targetSample = onsetAbs;
               slot->voiceId = in.voiceId;
               slot->bendSemitones = in.bendSemitones;
            }
         }
         else if (in.bendUpdate)
         {
            // No remapping here (Quantizer doesn't touch pitch) - just check
            // this voice is still one we're tracking, then forward as-is.
            if (mOutNote.Find(in.voiceId) == nullptr)
               continue;
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            if (mOutNote.Find(in.voiceId) == nullptr)
               continue;
            NoteEvent out = in;
            out.source = this;
            out.voiceId = in.voiceId;
            mOutbox.Push(out);
            mOutNote.Erase(in.voiceId);
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
   VoiceIdMap<int, 128> mOutNote;
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
      mOutNote.Clear();
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
                  const int stepVoiceId = NextVoiceId();
                  if (DeferredNote* on = mPending.FreeSlot())
                  {
                     on->active = true;
                     on->note = stepNote;
                     on->velocity = in.velocity;
                     on->isNoteOn = true;
                     on->targetSample = onSample;
                     on->voiceId = stepVoiceId;
                  }
                  if (DeferredNote* off = mPending.FreeSlot())
                  {
                     off->active = true;
                     off->note = stepNote;
                     off->velocity = 0.0f;
                     off->isNoteOn = false;
                     off->targetSample = offSample;
                     off->voiceId = stepVoiceId;
                  }
               }
               onsetAbs = stepBase + (uint64_t)((double)(steps - 1) * stepSamples);
            }

            mLastOutNote = in.note;
            mOutNote.GetOrInsert(in.voiceId) = in.note;

            if (onsetAbs < mSamplePos + (uint64_t)numFrames)
            {
               NoteEvent out;
               out.note = in.note;
               out.velocity = in.velocity;
               out.isNoteOn = true;
               out.frameOffset = (int)(onsetAbs - mSamplePos);
               out.bendSemitones = in.bendSemitones;
               out.source = this;
               out.voiceId = in.voiceId;
               mOutbox.Push(out);
            }
            else if (DeferredNote* on = mPending.FreeSlot())
            {
               on->active = true;
               on->note = in.note;
               on->velocity = in.velocity;
               on->isNoteOn = true;
               on->targetSample = onsetAbs;
               on->voiceId = in.voiceId;
               on->bendSemitones = in.bendSemitones;
            }
         }
         else if (in.bendUpdate)
         {
            // No remapping here (Glide's glissando steps are a separate
            // synthetic voice - this is still about the real input note) -
            // just check we're still tracking this voice, then forward.
            if (mOutNote.Find(in.voiceId) == nullptr)
               continue;
            NoteEvent out = in;
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            if (mOutNote.Find(in.voiceId) == nullptr)
               continue;
            NoteEvent out = in;
            out.source = this;
            out.voiceId = in.voiceId;
            mOutbox.Push(out);
            mOutNote.Erase(in.voiceId);
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
   VoiceIdMap<int, 128> mOutNote;
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
            slot->voiceId = in.voiceId; // independent delayed copies - just forward the source voice's id
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
            out.voiceId = p.voiceId;
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
      int voiceId = 0;
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
      mRoutedMask.Clear();
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

            mRoutedMask.GetOrInsert(in.voiceId) = (uint8_t)mask;
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
         else if (in.bendUpdate)
         {
            // Re-send to the same output channel(s) the note-on was routed
            // to - Router doesn't remap pitch, so the note number is
            // unchanged - and don't clear mRoutedMask; a note that was never
            // routed (dropped in kProbability mode) has nothing to re-send.
            const uint8_t* maskPtr = mRoutedMask.Find(in.voiceId);
            if (maskPtr == nullptr || *maskPtr == 0)
               continue;
            const int mask = *maskPtr;
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
            const uint8_t* maskPtr = mRoutedMask.Find(in.voiceId);
            if (maskPtr == nullptr || *maskPtr == 0)
               continue;
            const int mask = *maskPtr;
            for (int o = 0; o < 4; o++)
            {
               if ((mask & (1 << o)) == 0)
                  continue;
               NoteEvent out = in;
               out.source = this;
               mOutbox[o].Push(out);
            }
            mRoutedMask.Erase(in.voiceId);
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
   VoiceIdMap<uint8_t, 128> mRoutedMask;

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
   static constexpr int kMaxExpanded = kMaxHeld * 4 * 4; // held notes x up to 4 octaves x up to 4x (repeat x4 / stairs)

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mHeldCount = 0;
      mLastStep = -1;
      mStepCounter = 0;
      mGridStep = 0;
      mCurrentOutNote = -1;
      mCurrentOutVoiceId = 0;
      mPendingOffActive = false;
      mGridStepReadout.store(-1, std::memory_order_relaxed);
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
               if (mHeld[h].note == e.note) { mHeld[h].velocity = e.velocity; mHeld[h].voiceId = e.voiceId; found = true; break; }
            if (!found && mHeldCount < kMaxHeld)
               mHeld[mHeldCount++] = { e.note, e.velocity, e.voiceId };
         }
         else
         {
            // Match by voiceId, not note: a legato replay or doubled chord
            // note at the same pitch reuses this pitch's held slot (see the
            // note-on branch above), so a stale note-off from the note-on it
            // actually displaced must not be allowed to release the slot
            // that now belongs to a newer voice.
            for (int h = 0; h < mHeldCount; h++)
            {
               if (mHeld[h].voiceId == e.voiceId)
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
      if (mPendingOffActive && mPendingOffSample < mSamplePos + (uint64_t)numFrames)
      {
         NoteEvent off;
         off.note = mCurrentOutNote;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         const int offset = (mPendingOffSample > mSamplePos) ? (int)(mPendingOffSample - mSamplePos) : 0;
         off.frameOffset = std::clamp(offset, 0, numFrames - 1);
         off.source = this;
         off.voiceId = mCurrentOutVoiceId;
         mOutbox.Push(off);
         mPendingOffActive = false;
         mCurrentOutNote = -1;
      }

      const double beats = Transport::Instance().Beats();
      const long long step = (long long)std::floor(beats / (double)rateBeats);
      if (step != mLastStep)
      {
         mLastStep = step;

         // The grid-gate clock advances on every step tick, unconditionally -
         // including when nothing is held and when the step is gated off.
         // mStepCounter (below) stays a distinct counter that only advances
         // while notes are actually being selected, i.e. the note-*selection*
         // index; this one is purely the gate grid's own position.
         const int gridStep = (int)(mGridStep % (uint64_t)ArpeggiatorNode::kGateSteps);
         mGridStep++;
         const bool gated = ((mStepGates.load(std::memory_order_relaxed) >> gridStep) & 1) != 0;

         if (mHeldCount > 0)
         {
            mGridStepReadout.store(gridStep, std::memory_order_relaxed);

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
               // Advances on a gated-off step too, so muting a step punches a
               // hole in the pattern rather than time-shifting the notes
               // after it (matches Ableton's/Elektron's step-mute behaviour).
               mStepCounter++;

               // Close any still-sounding note first (gate >= 100% case, or
               // the previous step's note when this one is gated off).
               if (mCurrentOutNote >= 0)
               {
                  NoteEvent off;
                  off.note = mCurrentOutNote;
                  off.velocity = 0.0f;
                  off.isNoteOn = false;
                  off.frameOffset = 0;
                  off.source = this;
                  off.voiceId = mCurrentOutVoiceId;
                  mOutbox.Push(off);
                  mPendingOffActive = false;
               }

               if (gated)
               {
                  NoteEvent on;
                  on.note = seqNote[idx];
                  on.velocity = seqVel[idx];
                  on.isNoteOn = true;
                  on.frameOffset = 0;
                  on.source = this;
                  on.voiceId = NextVoiceId();
                  mOutbox.Push(on);
                  mCurrentOutNote = on.note;
                  mCurrentOutVoiceId = on.voiceId;
                  mCurrentOutNoteReadout.store(on.note, std::memory_order_relaxed);

                  const double samplesPerBeat = mSampleRate * 60.0 / std::max(1.0, bpm);
                  const double stepSamples = rateBeats * samplesPerBeat;
                  const double gateSamples = stepSamples * (double)gatePercent / 100.0;
                  mPendingOffSample = mSamplePos + (uint64_t)gateSamples;
                  mPendingOffActive = true;
               }
               else
               {
                  mCurrentOutNote = -1;
                  mCurrentOutNoteReadout.store(-1, std::memory_order_relaxed);
               }
            }
         }
         else
         {
            mGridStepReadout.store(-1, std::memory_order_relaxed);
            if (mCurrentOutNote >= 0)
            {
               // Nothing held any more - close whatever was still sounding.
               NoteEvent off;
               off.note = mCurrentOutNote;
               off.velocity = 0.0f;
               off.isNoteOn = false;
               off.frameOffset = 0;
               off.source = this;
               off.voiceId = mCurrentOutVoiceId;
               mOutbox.Push(off);
               mCurrentOutNote = -1;
               mCurrentOutNoteReadout.store(-1, std::memory_order_relaxed);
               mPendingOffActive = false;
            }
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
      mStepGates.store(n.stepGates, std::memory_order_relaxed);
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }

   int HeldCount() const { return mHeldCountReadout.load(std::memory_order_relaxed); }
   int CurrentNote() const { return mCurrentOutNoteReadout.load(std::memory_order_relaxed); }
   int CurrentGridStep() const { return mGridStepReadout.load(std::memory_order_relaxed); }

private:
   struct HeldNote
   {
      int note = 0;
      float velocity = 0.0f;
      int voiceId = 0;
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
         if (mode == ArpeggiatorNode::kDown || mode == ArpeggiatorNode::kDownUp || mode == ArpeggiatorNode::kStairsDown)
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

      // At this point tmp[0..n) is the base sequence Up/Down/Converge/
      // Diverge/Random/As Played already use - ascending (or descending, or
      // outside-in/inside-out reordered) pitch, octave-outer. That base *is*
      // "spread" (C3 D3 E3, C4 D4 E4), so Spread needs no branch of its own.
      // The remaining new modes reshape this base sequence further.
      if (mode == ArpeggiatorNode::kJoin || mode == ArpeggiatorNode::kJoinSpread)
      {
         // Octave-interleaved (C3 C4, D3 D4, E3 E4): sort the held notes
         // (pre-octave-expansion) ascending, then walk octaves inner per note.
         // Join/Spread alternates between this ordering and the base
         // (spread) ordering above once per full cycle through the pattern.
         const bool useJoinOrdering = (mode == ArpeggiatorNode::kJoin) || n <= 0 ||
                                       (((mStepCounter / (uint64_t)n) % 2) == 0);
         if (useJoinOrdering)
         {
            Pair heldSorted[kMaxHeld];
            const int hn = std::min(mHeldCount, kMaxHeld);
            for (int i = 0; i < hn; i++)
               heldSorted[i] = { mHeld[i].note, mHeld[i].velocity };
            std::sort(heldSorted, heldSorted + hn, [](const Pair& a, const Pair& b) { return a.note < b.note; });

            int w = 0;
            for (int i = 0; i < hn; i++)
            {
               for (int o = 0; o < octaves; o++)
               {
                  if (w >= kMaxExpanded)
                     break;
                  tmp[w].note = std::clamp(heldSorted[i].note + 12 * o, 0, 127);
                  tmp[w].vel = heldSorted[i].vel;
                  w++;
               }
            }
            n = w;
         }
      }
      else if (mode == ArpeggiatorNode::kRepeat2 || mode == ArpeggiatorNode::kRepeat4)
      {
         // Pitch-sorted ascending, each entry emitted 2 / 4 times consecutively.
         const int reps = (mode == ArpeggiatorNode::kRepeat4) ? 4 : 2;
         Pair rearr[kMaxExpanded];
         int w = 0;
         for (int i = 0; i < n && w < kMaxExpanded; i++)
            for (int r = 0; r < reps && w < kMaxExpanded; r++)
               rearr[w++] = tmp[i];
         std::copy(rearr, rearr + w, tmp);
         n = w;
      }
      else if (mode == ArpeggiatorNode::kStairsUp || mode == ArpeggiatorNode::kStairsDown)
      {
         // Two-forward walking pairs, wrapping: for C-E-G -> C E, E G, G C.
         // (base sequence is already descending for StairsDown, above.)
         Pair rearr[kMaxExpanded];
         int w = 0;
         if (n > 0)
         {
            for (int i = 0; i < n && w + 1 < kMaxExpanded; i++)
            {
               rearr[w++] = tmp[i];
               rearr[w++] = tmp[(i + 1) % n];
            }
         }
         std::copy(rearr, rearr + w, tmp);
         n = w;
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
   uint64_t mGridStep = 0;
   int mCurrentOutNote = -1;
   int mCurrentOutVoiceId = 0;
   bool mPendingOffActive = false;
   uint64_t mPendingOffSample = 0;

   std::atomic<int> mMode { ArpeggiatorNode::kUp };
   std::atomic<int> mOctaves { 1 };
   std::atomic<int> mRateMode { 0 };
   std::atomic<float> mRateBeats { 0.25f };
   std::atomic<float> mRateSeconds { 0.2f };
   std::atomic<float> mGatePercent { 80.0f };
   std::atomic<int> mStepGates { 0xFF };
   std::atomic<bool> mUseGlobalScale { false };
   std::atomic<int> mHeldCountReadout { 0 };
   std::atomic<int> mCurrentOutNoteReadout { -1 };
   std::atomic<int> mGridStepReadout { -1 };
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
   v.Int("stepGates", stepGates);
   v.Bool("useGlobalScale", useGlobalScale);
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

int ArpeggiatorNode::CurrentGridStep() const
{
   return mAudioNode ? mAudioNode->CurrentGridStep() : -1;
}

const std::vector<std::string>& ArpeggiatorNode::PresetNames()
{
   static const std::vector<std::string> kNames = { "Classic Up", "Trance Gate", "Pedal Stairs",
                                                     "Chord Spread", "Broken Random", "Wide Converge",
                                                     "Octave Join" };
   return kNames;
}

void ArpeggiatorNode::ApplyPreset(int index)
{
   // Each preset sets a complete state (mode, octaves, rate, gate, step
   // gates) rather than layering on whatever was already dialed in.
   mode = kUp;
   octaves = 1;
   rateMode = 0;
   rateBeats = 0.25f; // 1/16
   gatePercent = 80.0f;
   stepGates = 0xFF;

   switch (index)
   {
      case 0: // Classic Up
         break;
      case 1: // Trance Gate
         octaves = 2;
         gatePercent = 40.0f;
         stepGates = 0b10111011;
         break;
      case 2: // Pedal Stairs
         mode = kStairsUp;
         rateBeats = 0.5f; // 1/8
         break;
      case 3: // Chord Spread
         mode = kSpread;
         octaves = 3;
         rateBeats = 0.5f; // 1/8
         gatePercent = 95.0f;
         break;
      case 4: // Broken Random
         mode = kRandom;
         octaves = 2;
         stepGates = 0b11011010;
         break;
      case 5: // Wide Converge
         mode = kConverge;
         octaves = 2;
         rateBeats = 0.5f; // 1/8
         break;
      case 6: // Octave Join
         mode = kJoin;
         octaves = 2;
         gatePercent = 70.0f;
         break;
      default:
         break;
   }
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
      mCurrentOutVoiceId = 0;
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

      if (mPendingOffActive && mPendingOffSample < mSamplePos + (uint64_t)numFrames)
      {
         NoteEvent off;
         off.note = mCurrentOutNote;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         const int offset = (mPendingOffSample > mSamplePos) ? (int)(mPendingOffSample - mSamplePos) : 0;
         off.frameOffset = std::clamp(offset, 0, numFrames - 1);
         off.source = this;
         off.voiceId = mCurrentOutVoiceId;
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
               off.voiceId = mCurrentOutVoiceId;
               mOutbox.Push(off);
               mPendingOffActive = false;
            }

            NoteEvent on;
            int outNote = mNote[idx].load(std::memory_order_relaxed);
            if (mUseGlobalScale.load(std::memory_order_relaxed))
               outNote = MusicTime::SnapToScale(outNote, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
            on.note = std::clamp(outNote, 0, 127);
            on.velocity = std::clamp(mVelocity[idx].load(std::memory_order_relaxed), 0.0f, 1.0f);
            on.isNoteOn = true;
            on.frameOffset = 0;
            on.source = this;
            on.voiceId = NextVoiceId();
            mOutbox.Push(on);
            mCurrentOutNote = on.note;
            mCurrentOutVoiceId = on.voiceId;

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
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
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
   int mCurrentOutVoiceId = 0;
   bool mPendingOffActive = false;
   uint64_t mPendingOffSample = 0;

   std::atomic<int> mSteps { 8 };
   std::atomic<int> mRateMode { 0 };
   std::atomic<float> mRateBeats { 0.25f };
   std::atomic<float> mRateSeconds { 0.2f };
   std::atomic<float> mGatePercent { 70.0f };
   std::atomic<bool> mUseGlobalScale { false };
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
   v.Bool("useGlobalScale", useGlobalScale);
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
      mCurrentOutVoiceId = 0;
      mPendingOffActive = false;
      mPrevNote = -1;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const int rangeLow = mRangeLow.load(std::memory_order_relaxed);
      const int rangeHigh = std::max(rangeLow, mRangeHigh.load(std::memory_order_relaxed));
      const bool useGlobal = mUseGlobalScale.load(std::memory_order_relaxed);
      const int scale = useGlobal ? Transport::Instance().Scale() : mScale.load(std::memory_order_relaxed);
      const int root = useGlobal ? Transport::Instance().Key() : mRoot.load(std::memory_order_relaxed);
      const int rateMode = mRateMode.load(std::memory_order_relaxed);
      const float rateBeatsP = std::max(0.015625f, mRateBeats.load(std::memory_order_relaxed));
      const float rateSecondsP = std::max(0.01f, mRateSeconds.load(std::memory_order_relaxed));
      const int maxStep = std::max(1, mMaxStep.load(std::memory_order_relaxed));
      const double bpm = (double)Transport::Instance().Tempo();
      const double rateBeats = std::max(0.001, rateMode == 1 ? (double)rateSecondsP * bpm / 60.0 : (double)rateBeatsP);

      if (mPendingOffActive && mPendingOffSample < mSamplePos + (uint64_t)numFrames)
      {
         NoteEvent off;
         off.note = mCurrentOutNote;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         const int offset = (mPendingOffSample > mSamplePos) ? (int)(mPendingOffSample - mSamplePos) : 0;
         off.frameOffset = std::clamp(offset, 0, numFrames - 1);
         off.source = this;
         off.voiceId = mCurrentOutVoiceId;
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
            off.voiceId = mCurrentOutVoiceId;
            mOutbox.Push(off);
            mPendingOffActive = false;
         }

         NoteEvent on;
         on.note = note;
         on.velocity = 0.6f + (mRng.Next() * 0.5f + 0.5f) * 0.3f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         on.source = this;
         on.voiceId = NextVoiceId();
         mOutbox.Push(on);
         mCurrentOutNote = note;
         mCurrentOutVoiceId = on.voiceId;

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
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }

   int LastNote() const { return mLastNoteReadout.load(std::memory_order_relaxed); }

private:
   NoteEventQueue mOutbox;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;
   DspMath::WhiteNoise mRng;

   long long mLastStep = -1;
   int mCurrentOutNote = -1;
   int mCurrentOutVoiceId = 0;
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
   std::atomic<bool> mUseGlobalScale { true };
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
   v.Bool("useGlobalScale", useGlobalScale);
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
      const bool useGlobal = mUseGlobalScale.load(std::memory_order_relaxed);
      const int scale = useGlobal ? Transport::Instance().Scale() : mScale.load(std::memory_order_relaxed);
      const int root = useGlobal ? Transport::Instance().Key() : mRoot.load(std::memory_order_relaxed);
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

            const int noteVoiceId = NextVoiceId();
            if (Pending* on = FreeSlot())
            {
               on->active = true;
               on->note = notes[i];
               on->velocity = vel;
               on->isNoteOn = true;
               on->voiceId = noteVoiceId;
               on->targetSample = onset;
            }
            if (Pending* off = FreeSlot())
            {
               off->active = true;
               off->note = notes[i];
               off->velocity = 0.0f;
               off->isNoteOn = false;
               off->voiceId = noteVoiceId;
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
            out.voiceId = p.voiceId;
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
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }

   int LastChordSize() const { return mLastChordSizeReadout.load(std::memory_order_relaxed); }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      int voiceId = 0;
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
   std::atomic<bool> mUseGlobalScale { true };
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
   v.Bool("useGlobalScale", useGlobalScale);
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

// -------------------------------------------------------------- Note Stack
class AudioNoteStackNode : public AudioNode
{
public:
   static constexpr int kVoices = NoteStackNode::kVoices;

   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override
   {
      mVoiceState.Clear();
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      int semis[kVoices];
      bool ens[kVoices];
      for (int i = 0; i < kVoices; i++)
      {
         semis[i] = mSemitones[i].load(std::memory_order_relaxed);
         ens[i] = mEnabled[i].load(std::memory_order_relaxed);
      }

      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(evts, 64) : 0;

      for (int i = 0; i < n; i++)
      {
         const NoteEvent& in = evts[i];

         if (in.bendUpdate)
         {
            // Not a note-on/off - a live pitch-bend update for an already-
            // sounding voice. Forward it to the dry copy and to every voice
            // this input note is currently driving, matched by the voiceId
            // captured at note-on; never touch mVoiceState here.
            NoteEvent dry = in;
            dry.source = this;
            mOutbox.Push(dry);

            StackVoice* sv = mVoiceState.Find(in.voiceId);
            if (sv != nullptr)
            {
               for (int k = 0; k < sv->extraCount; k++)
               {
                  NoteEvent out;
                  out.note = sv->extraNote[k];
                  out.velocity = in.velocity;
                  out.isNoteOn = false;
                  out.bendUpdate = true;
                  out.bendSemitones = in.bendSemitones;
                  out.frameOffset = in.frameOffset;
                  out.source = this;
                  out.voiceId = sv->extraVoiceId[k];
                  mOutbox.Push(out);
               }
            }
            continue;
         }

         if (in.note < 0 || in.note > 127)
            continue;

         if (in.isNoteOn)
         {
            // Dry note: always passes through unchanged, on its own voiceId.
            NoteEvent dry = in;
            dry.source = this;
            mOutbox.Push(dry);

            // Capture the enabled set now; the note-off replays exactly
            // these pitches/voiceIds regardless of what the knobs do later.
            StackVoice& sv = mVoiceState.GetOrInsert(in.voiceId);
            sv.extraCount = 0;

            int seen[kVoices + 1];
            int seenCount = 0;
            seen[seenCount++] = in.note; // dedupe against the dry pitch too

            for (int v = 0; v < kVoices; v++)
            {
               if (!ens[v])
                  continue;
               int target = in.note + semis[v];
               if (mUseGlobalScale.load(std::memory_order_relaxed))
                  target = MusicTime::SnapToScale(target, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
               if (target < 0 || target > 127)
                  continue; // out of range - dropped, not folded

               bool dup = false;
               for (int s = 0; s < seenCount; s++)
               {
                  if (seen[s] == target)
                  {
                     dup = true;
                     break;
                  }
               }
               if (dup)
                  continue;
               seen[seenCount++] = target;

               const int newVoiceId = NextVoiceId();
               NoteEvent out;
               out.note = target;
               out.velocity = in.velocity;
               out.isNoteOn = true;
               out.frameOffset = in.frameOffset;
               out.bendSemitones = in.bendSemitones;
               out.source = this;
               out.voiceId = newVoiceId;
               mOutbox.Push(out);

               sv.extraNote[sv.extraCount] = (int8_t)target;
               sv.extraVoiceId[sv.extraCount] = newVoiceId;
               sv.extraCount++;
            }

            mLastStackSize.store(1 + sv.extraCount, std::memory_order_relaxed);
         }
         else
         {
            // Dry note-off: same passthrough shape as the note-on.
            NoteEvent dry = in;
            dry.source = this;
            mOutbox.Push(dry);

            StackVoice* sv = mVoiceState.Find(in.voiceId);
            if (sv != nullptr)
            {
               for (int k = 0; k < sv->extraCount; k++)
               {
                  NoteEvent out;
                  out.note = sv->extraNote[k];
                  out.velocity = in.velocity;
                  out.isNoteOn = false;
                  out.frameOffset = in.frameOffset;
                  out.source = this;
                  out.voiceId = sv->extraVoiceId[k];
                  mOutbox.Push(out);
               }
               mVoiceState.Erase(in.voiceId);
            }
         }
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   // Main thread only.
   void PushParams(const NoteStackNode& n)
   {
      for (int i = 0; i < kVoices; i++)
      {
         mSemitones[i].store(n.semitones[i], std::memory_order_relaxed);
         mEnabled[i].store(n.enabled[i], std::memory_order_relaxed);
      }
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
   }

   int LastStackSize() const { return mLastStackSize.load(std::memory_order_relaxed); }

private:
   struct StackVoice
   {
      int8_t extraCount = 0;
      int8_t extraNote[kVoices] = { -1, -1, -1, -1, -1, -1, -1, -1 };
      int extraVoiceId[kVoices] = { 0, 0, 0, 0, 0, 0, 0, 0 };
   };

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   VoiceIdMap<StackVoice, 128> mVoiceState;

   std::atomic<int> mSemitones[kVoices] = {};
   std::atomic<bool> mEnabled[kVoices] = {};
   std::atomic<bool> mUseGlobalScale { false };
   std::atomic<int> mLastStackSize { 0 };
};

NoteStackNode::NoteStackNode() = default;
NoteStackNode::~NoteStackNode() = default;

void NoteStackNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteStackNode>();
   mAudioNode->PushParams(*this);
}

void NoteStackNode::VisitParams(ParamVisitor& v)
{
   char key[24];
   for (int i = 0; i < kVoices; i++)
   {
      snprintf(key, sizeof(key), "semi%d", i);
      v.Int(key, semitones[i]);
      snprintf(key, sizeof(key), "enabled%d", i);
      v.Bool(key, enabled[i]);
   }
   v.Bool("useGlobalScale", useGlobalScale);
}

AudioNode* NoteStackNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteStackNode>();
   return mAudioNode.get();
}

int NoteStackNode::LastStackSize() const
{
   return mAudioNode ? mAudioNode->LastStackSize() : 0;
}

// ---------------------------------------------------------------- Note Strum
class AudioStrumNode : public AudioNode
{
public:
   struct DelayedEvent
   {
      NoteEvent evt;
      int64_t targetSample = 0;
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      mCurrentSample = 0;
      mDelayedCount = 0;
      mAppliedDelayPerVoice.Clear();
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      const double sampleRate = mSampleRate > 0.0 ? mSampleRate : 44100.0;
      const float strumMs = std::max(0.0f, mStrumMs.load(std::memory_order_relaxed));
      const double strumFrames = (double)strumMs * 0.001 * sampleRate;

      const int64_t blockStartSample = mCurrentSample;
      const int64_t blockEndSample = blockStartSample + numFrames;

      NoteEvent inEvents[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(inEvents, 64) : 0;

      NoteEvent noteOns[64];
      int numNoteOns = 0;
      NoteEvent others[64];
      int numOthers = 0;

      for (int i = 0; i < n; i++)
      {
         if (inEvents[i].isNoteOn)
         {
            if (numNoteOns < 64)
               noteOns[numNoteOns++] = inEvents[i];
         }
         else
         {
            if (numOthers < 64)
               others[numOthers++] = inEvents[i];
         }
      }

      if (numNoteOns > 0)
         mLastStrumCount.store(numNoteOns, std::memory_order_relaxed);

      std::sort(noteOns, noteOns + numNoteOns, [](const NoteEvent& a, const NoteEvent& b) {
         return a.note < b.note;
      });

      for (int i = 0; i < numNoteOns; i++)
      {
         const int64_t delayOffset = (int64_t)std::round((double)i * strumFrames);
         mAppliedDelayPerVoice.GetOrInsert(noteOns[i].voiceId) = delayOffset;

         const int64_t targetSample = blockStartSample + noteOns[i].frameOffset + delayOffset;
         if (targetSample < blockEndSample)
         {
            NoteEvent out = noteOns[i];
            out.frameOffset = (int)std::clamp(targetSample - blockStartSample, (int64_t)0, (int64_t)(numFrames - 1));
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            if (mDelayedCount < kMaxDelayed)
            {
               mDelayed[mDelayedCount++] = { noteOns[i], targetSample };
            }
         }
      }

      for (int i = 0; i < numOthers; i++)
      {
         int64_t delayOffset = 0;
         if (int64_t* d = mAppliedDelayPerVoice.Find(others[i].voiceId))
         {
            delayOffset = *d;
            if (!others[i].bendUpdate)
               mAppliedDelayPerVoice.Erase(others[i].voiceId);
         }

         const int64_t targetSample = blockStartSample + others[i].frameOffset + delayOffset;
         if (targetSample < blockEndSample)
         {
            NoteEvent out = others[i];
            out.frameOffset = (int)std::clamp(targetSample - blockStartSample, (int64_t)0, (int64_t)(numFrames - 1));
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            if (mDelayedCount < kMaxDelayed)
            {
               mDelayed[mDelayedCount++] = { others[i], targetSample };
            }
         }
      }

      int writeIdx = 0;
      for (int i = 0; i < mDelayedCount; i++)
      {
         if (mDelayed[i].targetSample < blockEndSample)
         {
            NoteEvent out = mDelayed[i].evt;
            out.frameOffset = (int)std::clamp(mDelayed[i].targetSample - blockStartSample, (int64_t)0, (int64_t)(numFrames - 1));
            out.source = this;
            mOutbox.Push(out);
         }
         else
         {
            mDelayed[writeIdx++] = mDelayed[i];
         }
      }
      mDelayedCount = writeIdx;

      mCurrentSample += numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox) override { mInbox = inbox; }

   void PushParams(const NoteStrumNode& n)
   {
      mStrumMs.store(n.strumMs, std::memory_order_relaxed);
   }

   int LastStrumCount() const { return mLastStrumCount.load(std::memory_order_relaxed); }

private:
   static constexpr int kMaxDelayed = 128;
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   double mSampleRate = 44100.0;
   int64_t mCurrentSample = 0;

   DelayedEvent mDelayed[kMaxDelayed];
   int mDelayedCount = 0;
   VoiceIdMap<int64_t, 128> mAppliedDelayPerVoice;

   std::atomic<float> mStrumMs { 25.0f };
   std::atomic<int> mLastStrumCount { 0 };
};

NoteStrumNode::NoteStrumNode() = default;
NoteStrumNode::~NoteStrumNode() = default;

void NoteStrumNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioStrumNode>();
   mAudioNode->PushParams(*this);
}

void NoteStrumNode::VisitParams(ParamVisitor& v)
{
   v.Float("strumMs", strumMs);
}

AudioNode* NoteStrumNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioStrumNode>();
   return mAudioNode.get();
}

int NoteStrumNode::LastStrumCount() const
{
   return mAudioNode ? mAudioNode->LastStrumCount() : 0;
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
      mSounding.Clear();
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
            mRecorded[mRecordedCount++] = { e.note, e.velocity, e.isNoteOn, beats - mRecordStartBeat, e.voiceId };
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
               out.voiceId = r.voiceId;
               mOutbox.Push(out);
               if (r.note >= 0 && r.note <= 127)
               {
                  if (r.isNoteOn)
                     mSounding.GetOrInsert(r.voiceId) = r.note;
                  else
                     mSounding.Erase(r.voiceId);
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
      int voiceId = 0;
   };

   void ForceOffAllSounding()
   {
      mSounding.ForEach([&](int voiceId, int& note) -> bool
      {
         NoteEvent off;
         off.note = note;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = 0;
         off.source = this;
         off.voiceId = voiceId;
         mOutbox.Push(off);
         SetKeyBit(mHeldBits, note, false);
         return true; // erase - fully released
      });
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
   VoiceIdMap<int, 128> mSounding; // voiceId -> note, for ones currently sounding during playback
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
            int rawNote = rangeLow + (int)((mRng.Next() * 0.5f + 0.5f) * (float)(span + 1));
            const bool useGlobal = mUseGlobalScale.load(std::memory_order_relaxed);
            const int scale = useGlobal ? Transport::Instance().Scale() : mScale.load(std::memory_order_relaxed);
            const int root = useGlobal ? Transport::Instance().Key() : mRoot.load(std::memory_order_relaxed);
            int note = MusicTime::SnapToScale(rawNote, root, scale, MusicTime::kSnapNearest);
            note = std::clamp(note, rangeLow, rangeHigh);

            const int noteVoiceId = NextVoiceId();
            if (Pending* on = FreeSlot())
            {
               on->active = true;
               on->note = std::clamp(note, 0, 127);
               on->velocity = 0.6f + (mRng.Next() * 0.5f + 0.5f) * 0.3f;
               on->isNoteOn = true;
               on->voiceId = noteVoiceId;
               on->targetSample = mSamplePos;
            }
            if (Pending* off = FreeSlot())
            {
               off->active = true;
               off->note = std::clamp(note, 0, 127);
               off->velocity = 0.0f;
               off->isNoteOn = false;
               off->voiceId = noteVoiceId;
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
            out.voiceId = p.voiceId;
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
      mScale.store(n.scale, std::memory_order_relaxed);
      mRoot.store(n.root, std::memory_order_relaxed);
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
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
      int voiceId = 0;
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
   std::atomic<int> mScale { 0 };
   std::atomic<int> mRoot { 0 };
   std::atomic<bool> mUseGlobalScale { true };
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
   v.Int("scale", scale);
   v.Int("root", root);
   v.Bool("useGlobalScale", useGlobalScale);
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

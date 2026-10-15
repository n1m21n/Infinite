#include "NoteLooperNode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "core/MidiFile.h"
#include "core/Transport.h"
#include "nodes/NotePatternLibrary.h"

namespace
{
   constexpr int kMaxSnap = NoteLooperNode::kMaxNotesPerSeq;
   constexpr int kMaxLocal = 1024;
   constexpr int kRingSize = 2048;   // power of two
   constexpr int kMaxOffs = 512;
   constexpr int kMaxOpen = 128;

   const double kQuantBeats[] = { 0.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
   constexpr int kNumQuant = (int)(sizeof(kQuantBeats) / sizeof(kQuantBeats[0]));

   struct PlayNote
   {
      double beat;
      double dur;
      int key;
      float vel;
      int id;
      int pass;
   };

   struct Snapshot
   {
      int epoch = 0;
      bool restart = false;     // a sequence switch (see NoteLooperNode::switchMode)
      double lengthBeats = 0.0; // the loop that plays (trimmed)
      double startBeats = 0.0;  // trim start within the take
      double fullBeats = 0.0;   // the whole take
      int includesUpTo = 0;
      int count = 0;
      PlayNote notes[kMaxSnap];
   };

   class SnapshotTripleBuffer
   {
   public:
      Snapshot& Back() { return mSlots[mBack]; }
      void Publish() { mBack = mMiddle.exchange(mBack | kDirty, std::memory_order_acq_rel) & 3; }
      bool Acquire()
      {
         if ((mMiddle.load(std::memory_order_acquire) & kDirty) == 0)
            return false;
         mFront = mMiddle.exchange(mFront, std::memory_order_acq_rel) & 3;
         return true;
      }
      const Snapshot& Front() const { return mSlots[mFront]; }
      bool Pending() const { return (mMiddle.load(std::memory_order_acquire) & kDirty) != 0; }

   private:
      static constexpr int kDirty = 4;
      Snapshot mSlots[3];
      std::atomic<int> mMiddle { 1 };
      int mBack = 2;
      int mFront = 0;
   };

   // Recorded note or a message, audio -> main.
   struct RecMsg
   {
      enum Kind : int { kNote = 0, kClosed, kTakeStarted } kind = kNote;
      PlayNote note {};
      double lengthBeats = 0.0;
      bool overdub = false;
      int epoch = 0; // the audio half's epoch when it was recorded; stale ones are dropped
   };

   class RecRing
   {
   public:
      bool Push(RecMsg m)
      {
         const int head = mHead.load(std::memory_order_relaxed);
         const int next = (head + 1) & (kRingSize - 1);
         if (next == mTail.load(std::memory_order_acquire))
            return false;
         mItems[head] = m;
         mHead.store(next, std::memory_order_release);
         return true;
      }
      bool Pop(RecMsg& m)
      {
         const int tail = mTail.load(std::memory_order_relaxed);
         if (tail == mHead.load(std::memory_order_acquire))
            return false;
         m = mItems[tail];
         mTail.store((tail + 1) & (kRingSize - 1), std::memory_order_release);
         return true;
      }

   private:
      RecMsg mItems[kRingSize];
      std::atomic<int> mHead { 0 }, mTail { 0 };
   };

   double QuantizeBeat(double beat, int quant, float strength, float swing)
   {
      if (quant <= 0 || quant >= kNumQuant)
         return beat;
      const double grid = kQuantBeats[quant];
      const double cell = std::round(beat / grid);
      double q = beat + (cell * grid - beat) * std::clamp((double)strength, 0.0, 1.0);
      const bool straight = quant == 1 || quant == 2 || quant == 4 || quant == 6;
      if (straight && swing > 0.0f && ((long long)cell & 1))
         q += (double)swing * grid * 0.5;
      return std::max(0.0, q);
   }
}

class AudioNoteLooperNode : public AudioNode
{
public:
   enum Cmd { kNone = 0, kArm, kArmOverdub, kClose, kPlay, kStop, kOverdubOn, kOverdubOff, kCancelArm, kDiscard };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      ReleaseAll(0);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int frames = std::max(1, output.numFrames);
      const Transport& tr = Transport::Instance();
      const bool transportOn = tr.IsPlaying();
      const double bpm = std::max(1.0f, tr.Tempo());
      const double bpf = bpm / (60.0 * mSampleRate);
      const double b0 = tr.Beats();
      const double b1 = b0 + (double)frames * bpf;
      const double bpb = std::max(1.0, tr.BeatsPerBar());
      auto frameAt = [&](double beat) { return std::clamp((int)std::floor((beat - b0) / bpf), 0, frames - 1); };
      auto nextBar = [&](double b) { return std::ceil(b / bpb - 1e-9) * bpb; };

      // ---- loop playback over [w0, w1) of one snapshot ----------------------
      auto playWindow = [&](const Snapshot& sn, double w0, double w1) {
         if (mMode != NoteLooperNode::kPlaying || !transportOn || mLength <= 0.0 || w1 <= w0 || w1 <= mAnchor)
            return;
         const int quant = mQuant.load(std::memory_order_relaxed);
         const float strength = mStrength.load(std::memory_order_relaxed);
         const float swing = mSwing.load(std::memory_order_relaxed);
         const double rel0 = std::max(0.0, w0 - mAnchor);
         const long long first = (long long)std::floor(rel0 / mLength);
         const long long last = (long long)std::floor((w1 - mAnchor) / mLength);
         const double full = mFull > 0.0 ? mFull : mLength;
         auto schedule = [&](const PlayNote& pn) {
            double q = QuantizeBeat(pn.beat, quant, strength, swing);
            if (q >= full)
               q -= full; // quantized past the end of the take: its start
            q -= mStart;  // trim
            if (q < 0.0 || q >= mLength)
               return;
            for (long long li = first; li <= last; li++)
            {
               // A note recorded in this pass only plays from the next one.
               if (pn.pass >= 0 && li <= pn.pass)
                  continue;
               const double t = mAnchor + (double)li * mLength + q;
               if (t < w0 || t >= w1)
                  continue;
               Emit(pn.key, pn.vel, frameAt(t), t + std::max(0.02, std::min(pn.dur, mLength - 0.01)));
            }
         };
         for (int i = 0; i < sn.count; i++)
            schedule(sn.notes[i]);
         for (int i = 0; i < mLocalCount; i++)
            schedule(mLocal[i]);
      };

      // ---- sequence switch ----------------------------------------------------
      // A switch request arrives just before its snapshot. Unless it is "now",
      // the current snapshot stays in the front slot and keeps playing until
      // the switch point (next bar or the loop's end); the snapshot is taken in
      // the block that reaches it, after the old notes up to that point.
      // Pending() is read first: if the switch snapshot is visible, so is the
      // request stored before it.
      (void)mBuffer.Pending();
      const int req = mSwitchReq.load(std::memory_order_acquire);
      if (req != mSwitchSeen)
      {
         mSwitchSeen = req;
         mSwitchAt = -1.0;
         const int sm = mSwitchMode.load(std::memory_order_relaxed);
         if (mMode == NoteLooperNode::kPlaying && transportOn && mLength > 0.0 && sm != NoteLooperNode::kSwitchNow)
         {
            if (b0 < mAnchor)
               mSwitchAt = mAnchor; // not started yet: take over at its start
            else if (sm == NoteLooperNode::kSwitchBar)
               mSwitchAt = nextBar(b0);
            else
               mSwitchAt = mAnchor + std::ceil((b0 - mAnchor) / mLength - 1e-9) * mLength;
         }
      }
      if (mSwitchAt >= 0.0 && (mMode != NoteLooperNode::kPlaying || !transportOn))
         mSwitchAt = -1.0;
      const bool holdSnapshot = mSwitchAt >= 0.0 && b1 <= mSwitchAt;
      if (mSwitchAt >= 0.0 && !holdSnapshot)
         playWindow(mBuffer.Front(), b0, mSwitchAt); // the old sequence up to the switch

      // ---- snapshot from the main thread ------------------------------------
      if (!holdSnapshot && mBuffer.Acquire())
      {
         const Snapshot& s = mBuffer.Front();
         if (s.epoch != mEpoch)
         {
            mEpoch = s.epoch;
            mLocalCount = 0;
            if (s.restart && mMode == NoteLooperNode::kPlaying)
            {
               if (mSwitchAt >= 0.0)
               {
                  ReleaseAll(frameAt(mSwitchAt));
                  mAnchor = mSwitchAt;
               }
               else
               {
                  ReleaseAll(0);
                  mAnchor = b0; // "now": the new sequence from its top
               }
            }
            else
               ReleaseAll(0);
         }
         else
         {
            // Drop the locally appended notes the sequence now includes.
            int w = 0;
            for (int i = 0; i < mLocalCount; i++)
               if (mLocal[i].id > s.includesUpTo)
                  mLocal[w++] = mLocal[i];
            mLocalCount = w;
         }
         mSwitchAt = -1.0;
         if (mMode != NoteLooperNode::kRecording)
         {
            mLength = s.lengthBeats;
            mStart = s.startBeats;
            mFull = s.fullBeats;
         }
         if (mMode == NoteLooperNode::kPlaying && mLength <= 0.0)
         {
            mMode = NoteLooperNode::kIdle;
            mOverdub = false;
            ReleaseAll(0);
         }
      }
      const Snapshot& snap = mBuffer.Front();
      // ---- commands ---------------------------------------------------------
      switch ((Cmd)mCmd.exchange(kNone, std::memory_order_acq_rel))
      {
      case kArm:
      case kArmOverdub:
      {
         const bool od = mCmdArg.load(std::memory_order_relaxed) != 0 || mLength > 0.0;
         mArmOverdub = od;
         mStartBeat = (mSyncStart ? nextBar(b0) : b0) + (mCountIn.load(std::memory_order_relaxed) ? bpb : 0.0);
         mMode = NoteLooperNode::kArmed;
         break;
      }
      case kCancelArm:
         if (mMode == NoteLooperNode::kArmed)
            mMode = mLength > 0.0 && mWasPlaying ? NoteLooperNode::kPlaying : NoteLooperNode::kIdle;
         break;
      case kClose:
         if (mMode == NoteLooperNode::kRecording)
            CloseTake(b0, bpb, true);
         break;
      case kPlay:
         if (mLength > 0.0 && mMode == NoteLooperNode::kIdle)
         {
            mMode = NoteLooperNode::kPlaying;
            mAnchor = mSyncStart ? nextBar(b0) : b0;
            mPass = -1;
         }
         break;
      case kStop:
         if (mMode == NoteLooperNode::kRecording)
            CloseTake(b0, bpb, true);
         mMode = NoteLooperNode::kIdle;
         mOverdub = false;
         CloseOpenNotes(b0);
         ReleaseAll(0);
         break;
      case kOverdubOn:
         mOverdub = true;
         break;
      case kDiscard: // clear: stop and forget everything in flight, report nothing
         mMode = NoteLooperNode::kIdle;
         mOverdub = false;
         mOpenCount = 0;
         mLocalCount = 0;
         ReleaseAll(0);
         break;
      case kOverdubOff:
         CloseOpenNotes(b0);
         mOverdub = false;
         break;
      default:
         break;
      }

      if (!transportOn && mMode == NoteLooperNode::kPlaying)
         ReleaseAll(0);

      // Armed -> recording / overdub.
      if (mMode == NoteLooperNode::kArmed && transportOn && b1 > mStartBeat)
      {
         if (mArmOverdub && mLength > 0.0)
         {
            mMode = NoteLooperNode::kPlaying;
            mAnchor = mStartBeat;
            mOverdub = true;
            mPass = -1;
         }
         else
         {
            mMode = NoteLooperNode::kRecording;
            mAnchor = mStartBeat;
            const int bars = mLengthBars.load(std::memory_order_relaxed);
            mRecEnd = bars > 0 ? mAnchor + (double)bars * bpb : -1.0;
            mOpenCount = 0;
            RecMsg m;
            m.kind = RecMsg::kTakeStarted;
            m.epoch = mEpoch;
            mRing.Push(m);
         }
      }

      const bool recording = mMode == NoteLooperNode::kRecording;
      const bool overdubbing = mMode == NoteLooperNode::kPlaying && mOverdub && mLength > 0.0;

      // ---- input: thru, record, erase keys ------------------------------------
      NoteEvent evts[64];
      int n;
      const bool thru = mThru.load(std::memory_order_relaxed);
      while ((n = mInbox != nullptr ? mInbox->Pop(mNoteCursor, evts, 64) : 0) > 0)
      {
         for (int i = 0; i < n; i++)
         {
            const NoteEvent& e = evts[i];
            if (thru)
            {
               NoteEvent t = e;
               t.source = this;
               mOutbox.Push(t);
            }
            if (e.bendUpdate || e.note < 0 || e.note > 127)
               continue;
            SetHeld(e.note, e.isNoteOn);
            const double t = b0 + (double)std::max(0, e.frameOffset) * bpf;
            if (recording && t >= mAnchor)
               RecordEdge(e, t - mAnchor, -1);
            else if (overdubbing && t >= mAnchor)
            {
               const double rel = t - mAnchor;
               const int pass = (int)std::floor(rel / mLength);
               RecordEdge(e, std::fmod(rel, mLength) + mStart, pass);
            }
         }
         if (n < 64)
            break;
      }

      // ---- fixed-length take closes by itself -----------------------------------
      if (mMode == NoteLooperNode::kRecording && mRecEnd > 0.0 && b1 >= mRecEnd)
         CloseTake(mRecEnd, bpb, false);

      // ---- playback --------------------------------------------------------------
      EmitOffsBefore(b1, frameAt);
      double playhead = 0.0;
      if (mMode == NoteLooperNode::kRecording)
         playhead = std::max(0.0, b0 - mAnchor);
      playWindow(snap, b0, b1);
      if (mMode == NoteLooperNode::kPlaying && transportOn && mLength > 0.0 && b1 > mAnchor)
      {
         const double rel0 = std::max(0.0, b0 - mAnchor);
         mPass = (int)std::floor(rel0 / mLength);
         playhead = mStart + std::fmod(rel0, mLength);
      }
      EmitOffsBefore(b1, frameAt);

      mModeReadout.store((int)mMode, std::memory_order_relaxed);
      mOverdubReadout.store(mOverdub, std::memory_order_relaxed);
      mPlayheadReadout.store(playhead, std::memory_order_relaxed);
      mLengthReadout.store(mMode == NoteLooperNode::kRecording
                              ? (mRecEnd > 0.0 ? mRecEnd - mAnchor : std::max(0.0, b0 - mAnchor))
                              : mLength,
                           std::memory_order_relaxed);
      mWasPlaying = mMode == NoteLooperNode::kPlaying;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override
   {
      mInbox = inbox;
      mNoteCursor = cursor;
   }

   // Main thread.
   void Command(Cmd c, int arg = 0)
   {
      mCmdArg.store(arg, std::memory_order_relaxed);
      mCmd.store((int)c, std::memory_order_release);
   }
   bool CommandPending() const { return mCmd.load(std::memory_order_acquire) != kNone; }
   void RequestSwitch(int epoch) { mSwitchReq.store(epoch, std::memory_order_release); }
   void PushParams(const NoteLooperNode& n)
   {
      mSwitchMode.store(n.switchMode, std::memory_order_relaxed);
      mLengthBars.store(n.lengthBars, std::memory_order_relaxed);
      mQuant.store(n.quantize, std::memory_order_relaxed);
      mStrength.store(n.strength, std::memory_order_relaxed);
      mSwing.store(n.swing, std::memory_order_relaxed);
      mCountIn.store(n.countIn, std::memory_order_relaxed);
      mThru.store(n.thru, std::memory_order_relaxed);
   }
   Snapshot& BackSnapshot() { return mBuffer.Back(); }
   void PublishSnapshot() { mBuffer.Publish(); }
   bool PopRecorded(RecMsg& m) { return mRing.Pop(m); }
   int ModeReadout() const { return mModeReadout.load(std::memory_order_relaxed); }
   bool OverdubReadout() const { return mOverdubReadout.load(std::memory_order_relaxed); }
   double PlayheadReadout() const { return mPlayheadReadout.load(std::memory_order_relaxed); }
   double LengthReadout() const { return mLengthReadout.load(std::memory_order_relaxed); }
   bool InputHeld(int key) const
   {
      if (key < 0 || key > 127)
         return false;
      return (mHeldBits[key >> 6].load(std::memory_order_relaxed) >> (key & 63)) & 1ull;
   }

private:
   struct OpenNote
   {
      int key;
      float vel;
      double start;
      int pass;
   };
   struct Off
   {
      double beat;
      int note;
      int voiceId;
   };

   void SetHeld(int key, bool on)
   {
      const uint64_t bit = 1ull << (key & 63);
      std::atomic<uint64_t>& w = mHeldBits[key >> 6];
      if (on)
         w.fetch_or(bit, std::memory_order_relaxed);
      else
         w.fetch_and(~bit, std::memory_order_relaxed);
   }

   void RecordEdge(const NoteEvent& e, double loopBeat, int pass)
   {
      if (e.isNoteOn)
      {
         if (mOpenCount < kMaxOpen)
            mOpen[mOpenCount++] = { e.note, e.velocity, loopBeat, pass };
         return;
      }
      for (int i = 0; i < mOpenCount; i++)
      {
         if (mOpen[i].key != e.note)
            continue;
         double dur = loopBeat - mOpen[i].start;
         if (pass >= 0 && dur < 0.0)
            dur += mLength; // held across the loop end
         FinishNote(mOpen[i], std::max(0.02, dur));
         mOpen[i] = mOpen[--mOpenCount];
         return;
      }
   }

   void FinishNote(const OpenNote& o, double dur)
   {
      PlayNote pn { o.start, dur, o.key, o.vel, ++mNextId, o.pass };
      RecMsg m;
      m.kind = RecMsg::kNote;
      m.note = pn;
      m.overdub = o.pass >= 0;
      m.epoch = mEpoch;
      if (!mRing.Push(m))
         return;
      // Recorded notes play from the next pass right away (the first take as
      // soon as the loop closes), without waiting for the main thread.
      if (mLocalCount < kMaxLocal)
         mLocal[mLocalCount++] = pn;
   }

   void CloseOpenNotes(double beatNow)
   {
      for (int i = 0; i < mOpenCount; i++)
      {
         double end = beatNow - mAnchor;
         if (mOpen[i].pass >= 0 && mLength > 0.0)
            end = std::fmod(std::max(0.0, end), mLength) + mStart;
         double dur = end - mOpen[i].start;
         if (dur <= 0.0 && mLength > 0.0)
            dur += mLength;
         FinishNote(mOpen[i], std::max(0.02, dur));
      }
      mOpenCount = 0;
   }

   // Ends the first take at `endBeat` (rounded up to whole bars for a free one).
   void CloseTake(double endBeat, double bpb, bool roundToBar)
   {
      double len = std::max(0.0, endBeat - mAnchor);
      if (roundToBar)
         len = std::max(bpb, std::ceil(len / bpb - 1e-6) * bpb);
      CloseOpenNotes(mAnchor + std::min(len, std::max(0.0, endBeat - mAnchor)));
      mLength = len;
      mStart = 0.0;
      mFull = len;
      mMode = NoteLooperNode::kPlaying;
      mOverdub = false;
      mPass = 0;
      RecMsg m;
      m.kind = RecMsg::kClosed;
      m.lengthBeats = len;
      m.epoch = mEpoch;
      mRing.Push(m);
   }

   template <typename FrameAt>
   void EmitOffsBefore(double limit, FrameAt frameAt)
   {
      for (int i = 0; i < mOffCount;)
      {
         if (mOffs[i].beat < limit)
         {
            NoteEvent off;
            off.note = mOffs[i].note;
            off.velocity = 0.0f;
            off.isNoteOn = false;
            off.frameOffset = frameAt(mOffs[i].beat);
            off.source = this;
            off.voiceId = mOffs[i].voiceId;
            mOutbox.Push(off);
            mOffs[i] = mOffs[--mOffCount];
         }
         else
            i++;
      }
   }

   void ReleaseAll(int frame)
   {
      for (int i = 0; i < mOffCount; i++)
      {
         NoteEvent off;
         off.note = mOffs[i].note;
         off.velocity = 0.0f;
         off.isNoteOn = false;
         off.frameOffset = frame;
         off.source = this;
         off.voiceId = mOffs[i].voiceId;
         mOutbox.Push(off);
      }
      mOffCount = 0;
   }

   void Emit(int key, float vel, int frame, double offBeat)
   {
      if (key < 0 || key > 127 || mOffCount >= kMaxOffs)
         return;
      NoteEvent on;
      on.note = key;
      on.velocity = std::clamp(vel, 0.01f, 1.0f);
      on.isNoteOn = true;
      on.frameOffset = frame;
      on.source = this;
      on.voiceId = NextVoiceId();
      mOutbox.Push(on);
      mOffs[mOffCount++] = { offBeat, key, on.voiceId };
   }

   SnapshotTripleBuffer mBuffer;
   RecRing mRing;
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;
   double mSampleRate = 44100.0;

   NoteLooperNode::Mode mMode = NoteLooperNode::kIdle;
   bool mOverdub = false;
   bool mArmOverdub = false;
   bool mWasPlaying = false;
   bool mSyncStart = true;
   double mAnchor = 0.0, mStartBeat = 0.0, mRecEnd = -1.0, mLength = 0.0;
   double mStart = 0.0, mFull = 0.0;  // trim start and the whole take
   double mSwitchAt = -1.0;           // beat where a pending sequence switch takes over
   int mSwitchSeen = 0;
   int mPass = -1;
   int mEpoch = 0;
   int mNextId = 0;
   OpenNote mOpen[kMaxOpen];
   int mOpenCount = 0;
   PlayNote mLocal[kMaxLocal];
   int mLocalCount = 0;
   Off mOffs[kMaxOffs];
   int mOffCount = 0;

   std::atomic<int> mCmd { kNone }, mCmdArg { 0 };
   std::atomic<int> mSwitchReq { 0 }, mSwitchMode { 0 };
   std::atomic<int> mLengthBars { 2 }, mQuant { 4 };
   std::atomic<float> mStrength { 1.0f }, mSwing { 0.0f };
   std::atomic<bool> mCountIn { false }, mThru { true };
   std::atomic<int> mModeReadout { 0 };
   std::atomic<bool> mOverdubReadout { false };
   std::atomic<double> mPlayheadReadout { 0.0 }, mLengthReadout { 0.0 };
   std::atomic<uint64_t> mHeldBits[2] {};
};

// ============================================================== main thread

int NoteLooperNode::Sequence::NoteCount() const
{
   int n = 0;
   for (const Layer& l : layers)
      n += (int)l.notes.size();
   return n;
}

double NoteLooperNode::Sequence::TrimStart() const
{
   return std::clamp(trimStart, 0.0, std::max(0.0, lengthBeats - 0.25));
}

double NoteLooperNode::Sequence::TrimEnd() const
{
   if (trimEnd <= 0.0 || trimEnd >= lengthBeats)
      return lengthBeats;
   return std::max(TrimStart() + 0.25, trimEnd);
}

NoteLooperNode::NoteLooperNode() = default;
NoteLooperNode::~NoteLooperNode() = default;

AudioNoteLooperNode& NoteLooperNode::Audio()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioNoteLooperNode>();
   return *mAudioNode;
}

AudioNode* NoteLooperNode::GetAudioNode() { return &Audio(); }

const std::vector<std::string>& NoteLooperNode::QuantizeNames()
{
   static const std::vector<std::string> kNames = { "off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
   return kNames;
}

const std::vector<std::string>& NoteLooperNode::LengthNames()
{
   static const std::vector<std::string> kNames = { "free", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
   return kNames;
}

const std::vector<std::string>& NoteLooperNode::SwitchNames()
{
   static const std::vector<std::string> kNames = { "next bar", "end of loop", "now" };
   return kNames;
}

int NoteLooperNode::LengthBarsFromIndex(int i)
{
   static const int kBars[] = { 0, 1, 2, 4, 8, 16 };
   return kBars[std::clamp(i, 0, 5)];
}

int NoteLooperNode::LengthIndexFromBars(int bars)
{
   static const int kBars[] = { 0, 1, 2, 4, 8, 16 };
   for (int i = 0; i < 6; i++)
      if (kBars[i] == bars)
         return i;
   return 2;
}

NoteLooperNode::Mode NoteLooperNode::CurrentMode() const
{
   return mAudioNode ? (Mode)mAudioNode->ModeReadout() : kIdle;
}
bool NoteLooperNode::Overdubbing() const { return mAudioNode && mAudioNode->OverdubReadout(); }
double NoteLooperNode::PlayheadBeats() const { return mAudioNode ? mAudioNode->PlayheadReadout() : 0.0; }
double NoteLooperNode::LoopLengthBeats() const
{
   const Mode m = CurrentMode();
   if (m == kRecording && mAudioNode)
      return mAudioNode->LengthReadout();
   return seqs[std::clamp(activeSeq, 0, kSeqs - 1)].lengthBeats;
}

void NoteLooperNode::PressRec()
{
   Sequence& s = seqs[activeSeq];
   switch (CurrentMode())
   {
   case kIdle:
      if (!Transport::Instance().IsPlaying())
         Transport::Instance().SetPlaying(true);
      Audio().Command(AudioNoteLooperNode::kArm, s.lengthBeats > 0.0 ? 1 : 0);
      mStatus = s.lengthBeats > 0.0 ? "overdub starts on the next bar" : "recording starts on the next bar";
      break;
   case kArmed:
      Audio().Command(AudioNoteLooperNode::kCancelArm);
      mStatus = "rec cancelled";
      break;
   case kRecording:
      if (lengthBars == 0)
      {
         Audio().Command(AudioNoteLooperNode::kClose);
         mStatus = "loop closed";
      }
      break;
   case kPlaying:
      ToggleOverdub();
      break;
   }
}

void NoteLooperNode::PressPlayStop()
{
   const Mode m = CurrentMode();
   if (m == kIdle)
   {
      if (seqs[activeSeq].lengthBeats <= 0.0)
      {
         mStatus = "nothing recorded in this sequence";
         return;
      }
      if (!Transport::Instance().IsPlaying())
         Transport::Instance().SetPlaying(true);
      Audio().Command(AudioNoteLooperNode::kPlay);
      mStatus = "plays from the next bar";
   }
   else
   {
      Audio().Command(AudioNoteLooperNode::kStop);
      mStatus = "stopped";
   }
}

void NoteLooperNode::ToggleOverdub()
{
   if (CurrentMode() != kPlaying)
      return;
   const bool on = !Overdubbing();
   Audio().Command(on ? AudioNoteLooperNode::kOverdubOn : AudioNoteLooperNode::kOverdubOff);
   mStatus = on ? "overdub: a new layer each pass" : "overdub off";
}

void NoteLooperNode::Undo()
{
   Sequence& s = seqs[activeSeq];
   if (s.layers.empty())
      return;
   s.layers.pop_back();
   if (s.layers.empty())
   {
      s = Sequence();
      Audio().Command(AudioNoteLooperNode::kDiscard);
   }
   mEpoch++; // the audio half drops its local copies of the removed notes
   mDirty = true;
   mStatus = "removed the last layer";
}

void NoteLooperNode::Clear()
{
   seqs[activeSeq] = Sequence();
   Audio().Command(AudioNoteLooperNode::kDiscard);
   mEpoch++;
   mDirty = true;
   mStatus = "sequence cleared";
}

void NoteLooperNode::SelectSeq(int seq)
{
   seq = std::clamp(seq, 0, kSeqs - 1);
   if (seq == activeSeq)
      return;
   const Mode m = CurrentMode();
   if (m == kArmed || m == kRecording)
   {
      mStatus = "stop the recording before switching";
      return;
   }
   activeSeq = seq;
   mEpoch++;
   mDirty = true;
   mPendingSeq = seq;
   static const char* kWhen[] = { "from the next bar", "at the end of the loop", "now" };
   char buf[64];
   snprintf(buf, sizeof(buf), "sequence %c %s", 'A' + seq,
            m == kPlaying ? kWhen[std::clamp(switchMode, 0, 2)] : "selected");
   mStatus = buf;
}

void NoteLooperNode::SetTrim(double start, double end)
{
   Sequence& s = seqs[activeSeq];
   if (s.lengthBeats <= 0.0)
      return;
   auto snap = [](double b) { return std::round(b * 4.0) / 4.0; };
   start = std::clamp(snap(start), 0.0, std::max(0.0, s.lengthBeats - 0.25));
   end = std::clamp(snap(end), start + 0.25, s.lengthBeats);
   s.trimStart = start;
   s.trimEnd = end >= s.lengthBeats - 1e-9 ? 0.0 : end;
   mDirty = true;
}

void NoteLooperNode::SetErase(bool held)
{
   if (held && !mErase)
      mErasePrev = -1.0;
   mErase = held;
}

void NoteLooperNode::SetLayerMute(int layer, bool mute)
{
   Sequence& s = seqs[activeSeq];
   if (layer < 0 || layer >= (int)s.layers.size())
      return;
   s.layers[layer].mute = mute;
   mDirty = true;
}

void NoteLooperNode::DrainRecorded()
{
   RecMsg m;
   Sequence& s = seqs[activeSeq];
   while (Audio().PopRecorded(m))
   {
      if (m.epoch != mEpoch)
         continue; // recorded before a clear / switch / load
      switch (m.kind)
      {
      case RecMsg::kTakeStarted:
         s.layers.clear();
         s.layers.push_back(Layer());
         break;
      case RecMsg::kClosed:
         s.lengthBeats = m.lengthBeats;
         s.trimStart = 0.0;
         s.trimEnd = 0.0;
         mDirty = true;
         break;
      case RecMsg::kNote:
      {
         Note n;
         n.beat = m.note.beat;
         n.dur = m.note.dur;
         n.key = m.note.key;
         n.vel = m.note.vel;
         n.id = m.note.id;
         n.pass = m.note.pass;
         if (s.NoteCount() >= kMaxNotesPerSeq)
            break;
         if (!m.overdub)
         {
            if (s.layers.empty())
               s.layers.push_back(Layer());
            s.layers.front().notes.push_back(n);
         }
         else
         {
            // One layer per overdub pass (the last one takes the rest when full).
            if (s.layers.empty() || s.layers.back().pass != n.pass)
            {
               if ((int)s.layers.size() < kMaxLayers)
               {
                  Layer l;
                  l.pass = n.pass;
                  s.layers.push_back(l);
               }
               else
                  s.layers.back().pass = n.pass;
            }
            s.layers.back().notes.push_back(n);
         }
         mIncludesUpTo = std::max(mIncludesUpTo, n.id);
         mDirty = true;
         break;
      }
      }
   }
}

void NoteLooperNode::Publish(bool restart)
{
   const Sequence& s = seqs[std::clamp(activeSeq, 0, kSeqs - 1)];
   Snapshot& snap = Audio().BackSnapshot();
   snap.epoch = mEpoch;
   snap.restart = restart;
   snap.lengthBeats = s.PlayLength();
   snap.startBeats = s.lengthBeats > 0.0 ? s.TrimStart() : 0.0;
   snap.fullBeats = s.lengthBeats;
   snap.includesUpTo = mIncludesUpTo;
   int c = 0;
   for (const Layer& l : s.layers)
   {
      if (l.mute)
         continue;
      for (const Note& n : l.notes)
      {
         if (c >= kMaxSnap)
            break;
         snap.notes[c++] = { n.beat, n.dur, n.key, n.vel, n.id, -1 }; // pass only matters for local notes
      }
   }
   snap.count = c;
   if (restart)
      Audio().RequestSwitch(mEpoch); // before the snapshot: the audio half holds the old one until the switch point
   Audio().PublishSnapshot();
}

void NoteLooperNode::ApplyErase()
{
   if (!mErase || CurrentMode() != kPlaying)
   {
      mErasePrev = -1.0;
      return;
   }
   Sequence& s = seqs[activeSeq];
   const double len = s.lengthBeats;
   if (len <= 0.0)
      return;
   const double now = PlayheadBeats();
   if (mErasePrev < 0.0)
   {
      mErasePrev = now;
      return;
   }
   auto inWindow = [&](double b) {
      return mErasePrev <= now ? (b >= mErasePrev && b < now) : (b >= mErasePrev || b < now);
   };
   bool removed = false;
   for (Layer& l : s.layers)
   {
      const size_t before = l.notes.size();
      l.notes.erase(std::remove_if(l.notes.begin(), l.notes.end(),
                                   [&](const Note& n) {
                                      double q = QuantizeBeat(n.beat, quantize, strength, swing);
                                      if (q >= len)
                                         q -= len;
                                      return mAudioNode->InputHeld(n.key) && inWindow(q);
                                   }),
                    l.notes.end());
      removed = removed || l.notes.size() != before;
   }
   if (removed)
      mDirty = true;
   mErasePrev = now;
}

void NoteLooperNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   lengthBars = LengthBarsFromIndex(LengthIndexFromBars(lengthBars));
   quantize = std::clamp(quantize, 0, kNumQuant - 1);
   strength = std::clamp(strength, 0.0f, 1.0f);
   swing = std::clamp(swing, 0.0f, 1.0f);
   activeSeq = std::clamp(activeSeq, 0, kSeqs - 1);
   switchMode = std::clamp(switchMode, 0, 2);
   Audio().PushParams(*this);
   DrainRecorded();
   ApplyErase();
   if (mDirty)
   {
      const bool restart = mPendingSeq >= 0;
      Publish(restart);
      mPendingSeq = -1;
      mDirty = false;
   }
}

// ------------------------------------------------------------- persistence
// "len;mute,pass|beat,dur,key,vel|...;mute,pass|..." with beats to 1/1000.
std::string NoteLooperNode::Encode(const Sequence& s)
{
   if (s.lengthBeats <= 0.0)
      return std::string();
   std::ostringstream o;
   o.setf(std::ios::fixed);
   o.precision(3);
   o << s.lengthBeats;
   if (s.trimStart > 0.0 || s.trimEnd > 0.0)
      o << ',' << s.trimStart << ',' << s.trimEnd;
   for (const Layer& l : s.layers)
   {
      o << ';' << (l.mute ? 1 : 0) << ',' << l.pass;
      for (const Note& n : l.notes)
         o << '|' << n.beat << ',' << n.dur << ',' << n.key << ',' << (int)std::lround(n.vel * 127.0f);
   }
   return o.str();
}

void NoteLooperNode::Decode(const std::string& text, Sequence& s)
{
   s = Sequence();
   if (text.empty())
      return;
   std::vector<std::string> parts;
   {
      std::string cur;
      for (char c : text)
      {
         if (c == ';')
         {
            parts.push_back(cur);
            cur.clear();
         }
         else
            cur += c;
      }
      parts.push_back(cur);
   }
   {
      double len = 0.0, ts = 0.0, te = 0.0;
      sscanf(parts[0].c_str(), "%lf,%lf,%lf", &len, &ts, &te);
      s.lengthBeats = std::max(0.0, len);
      s.trimStart = std::max(0.0, ts);
      s.trimEnd = std::max(0.0, te);
   }
   for (size_t i = 1; i < parts.size() && (int)s.layers.size() < kMaxLayers; i++)
   {
      Layer l;
      std::istringstream in(parts[i]);
      std::string field;
      bool head = true;
      while (std::getline(in, field, '|'))
      {
         if (head)
         {
            int mute = 0, pass = -1;
            sscanf(field.c_str(), "%d,%d", &mute, &pass);
            l.mute = mute != 0;
            l.pass = pass;
            head = false;
            continue;
         }
         Note n;
         int key = 60, vel = 100;
         double beat = 0.0, dur = 0.25;
         if (sscanf(field.c_str(), "%lf,%lf,%d,%d", &beat, &dur, &key, &vel) == 4)
         {
            n.beat = beat;
            n.dur = dur;
            n.key = std::clamp(key, 0, 127);
            n.vel = std::clamp(vel, 1, 127) / 127.0f;
            l.notes.push_back(n);
         }
      }
      s.layers.push_back(std::move(l));
   }
   if (s.layers.empty())
      s.lengthBeats = 0.0;
}

void NoteLooperNode::VisitParams(ParamVisitor& v)
{
   v.Int("lengthBars", lengthBars);
   v.Int("quantize", quantize);
   v.Float("strength", strength);
   v.Float("swing", swing);
   v.Bool("countIn", countIn);
   v.Bool("thru", thru);
   v.Int("activeSeq", activeSeq);
   v.Int("switchMode", switchMode);
   // One text per sequence. Writers read the encoded text; a reader replaces
   // it, which is how a load is told apart from a save.
   for (int i = 0; i < kSeqs; i++)
   {
      char key[16];
      snprintf(key, sizeof(key), "seq%d", i);
      const std::string encoded = Encode(seqs[i]);
      std::string text = encoded;
      v.Text(key, text);
      if (text != encoded)
      {
         Decode(text, seqs[i]);
         mEpoch++;
         mDirty = true;
      }
   }
}

// --------------------------------------------------------------- .mid files
bool NoteLooperNode::ExportMidi(const std::string& path, std::string& error) const
{
   const Sequence& s = seqs[activeSeq];
   if (s.lengthBeats <= 0.0)
   {
      error = "the sequence is empty";
      return false;
   }
   const double len = s.PlayLength();
   std::vector<NotePatterns::MidiNote> out;
   for (const Layer& l : s.layers)
   {
      if (l.mute)
         continue;
      for (const Note& n : l.notes)
      {
         double q = QuantizeBeat(n.beat, quantize, strength, swing);
         if (q >= s.lengthBeats)
            q -= s.lengthBeats;
         q -= s.TrimStart(); // what plays: the trimmed loop
         if (q < 0.0 || q >= len)
            continue;
         out.push_back({ q, std::min(n.dur, len - q), n.key, (int)std::lround(n.vel * 127.0f) });
      }
   }
   const Transport& tr = Transport::Instance();
   return NotePatterns::WriteMidi(path, out, tr.Tempo(), tr.TimeSigNumerator(), tr.TimeSigDenominator(),
                                  len, error);
}

bool NoteLooperNode::ImportMidi(const std::string& path, std::string& error)
{
   MidiFile::Data data;
   if (!MidiFile::Load(path, data, error))
      return false;
   if (data.notes.empty())
   {
      error = "no notes in the file";
      return false;
   }
   Sequence& s = seqs[activeSeq];
   if ((int)s.layers.size() >= kMaxLayers)
   {
      error = "this sequence already has 8 layers";
      return false;
   }
   const double bpb = std::max(1.0, Transport::Instance().BeatsPerBar());
   double len = std::max(bpb, std::ceil(data.LengthBeats() / bpb - 1e-6) * bpb);
   if (s.lengthBeats > 0.0)
      len = s.lengthBeats; // folds into the existing loop
   Layer l;
   for (const MidiFile::Note& mn : data.notes)
   {
      const double b = data.TicksToBeats(mn.startTick);
      if (b >= len)
         continue;
      Note n;
      n.beat = b;
      n.dur = std::max(0.02, data.TicksToBeats(mn.endTick) - b);
      n.key = mn.key;
      n.vel = std::clamp(mn.velocity / 127.0f, 0.01f, 1.0f);
      l.notes.push_back(n);
      if ((int)l.notes.size() + s.NoteCount() >= kMaxNotesPerSeq)
         break;
   }
   s.lengthBeats = len;
   s.layers.push_back(std::move(l));
   mDirty = true;
   mStatus = "imported " + std::filesystem::u8path(path).filename().u8string();
   return true;
}

std::string NoteLooperNode::SaveToBank(std::string& error) const
{
   const std::string folder = NotePatterns::UserFolder();
   if (folder.empty())
   {
      error = "no user data folder";
      return std::string();
   }
   const std::filesystem::path dir = std::filesystem::u8path(folder) / "Note Looper";
   std::error_code ec;
   std::filesystem::create_directories(dir, ec);
   const std::time_t t = std::time(nullptr);
   std::tm lt {};
#ifdef _WIN32
   localtime_s(&lt, &t);
#else
   localtime_r(&t, &lt);
#endif
   char name[64];
   snprintf(name, sizeof(name), "take %04d-%02d-%02d %02d%02d%02d %c.mid", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
            lt.tm_hour, lt.tm_min, lt.tm_sec, 'A' + activeSeq);
   const std::string path = (dir / name).u8string();
   if (!ExportMidi(path, error))
      return std::string();
   NotePatterns::Rescan();
   return path;
}

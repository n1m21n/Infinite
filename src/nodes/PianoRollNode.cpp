#include "PianoRollNode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include "arrange/NoteSchedule.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"

using Arrange::NoteSchedule;
using Arrange::SchedNote;

// The audio half. Each block plays the schedule's notes whose on falls in the
// block's beat range [tStart, tEnd), found by binary search, and ends held
// notes whose off falls there. Expression curves go out as bendUpdate events
// every kExprFrames. Nothing here allocates: the schedule is read-only and the
// held notes live in a fixed array.
class AudioPianoRollNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mHeldCount = 0;
      mLastTransport = -1.0;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      mBlocks.fetch_add(1, std::memory_order_relaxed);
      const int numFrames = std::max(1, output.numFrames);
      mOutCount = 0;

      // Thru first: the input's events keep their frames and voices.
      NoteEvent in[64];
      const int nIn = mInbox != nullptr ? mInbox->Pop(mNoteCursor, in, 64) : 0;
      if (mThru.load(std::memory_order_relaxed))
         for (int i = 0; i < nIn; i++)
         {
            NoteEvent e = in[i];
            e.source = this;
            Emit(e);
         }

      const NoteSchedule* s = mSched.load(std::memory_order_acquire);
      const uint64_t gen = s ? s->gen : 0;
      if (gen != mGen)
      {
         Swap(s);
         mGen = gen;
         mAck.store(gen, std::memory_order_release);
      }

      Transport& tr = Transport::Instance();
      if (!s || !tr.IsPlaying())
      {
         ReleaseAll(0);
         mLastTransport = -1.0;
         mPlayhead.store(-1.0, std::memory_order_relaxed);
         Flush();
         return;
      }

      const double bpm = std::max(1.0, (double)tr.Tempo());
      const double dT = (double)numFrames * bpm / (60.0 * mSampleRate);
      const double tStart = tr.BlockStartBeats(), tEnd = tStart + dT;
      // A seek, a loop back or a fresh start: nothing held carries over, and
      // the next pass rolls chance anew.
      if (mLastTransport < 0.0 || tStart < mLastTransport - 1e-6 || tStart > mLastTransport + dT * 4.0 + 0.05)
      {
         ReleaseAll(0);
         mPass++;
      }
      mLastTransport = tEnd;
      mPlayhead.store(tStart, std::memory_order_relaxed);
      auto frameOf = [&](double b) { return std::clamp((int)((b - tStart) / dT * (double)numFrames), 0, numFrames - 1); };

      // Offs of notes held from earlier blocks.
      for (int i = 0; i < mHeldCount;)
         if (mHeld[i].off < tEnd)
         {
            PushOff(mHeld[i], frameOf(mHeld[i].off));
            mHeld[i] = mHeld[--mHeldCount];
         }
         else
            i++;

      const int transpose = mTranspose.load(std::memory_order_relaxed);
      const float velScale = mVelocity.load(std::memory_order_relaxed);
      const bool snap = mUseGlobalScale.load(std::memory_order_relaxed);
      const auto& notes = s->notes;
      auto it = std::lower_bound(notes.begin(), notes.end(), tStart, [](const SchedNote& n, double b) { return n.on < b; });
      for (; it != notes.end() && it->on < tEnd; ++it)
      {
         const SchedNote& n = *it;
         if (!Arrange::ChancePlays(n.key, mPass, n.chance))
            continue;
         int pitch = std::clamp((int)n.pitch + transpose, 0, 127);
         if (snap)
            pitch = std::clamp(MusicTime::SnapToScale(pitch, tr.Key(), tr.Scale(), MusicTime::kSnapNearest), 0, 127);
         NoteEvent on;
         on.note = pitch;
         on.velocity = std::clamp((float)n.vel / 127.0f * velScale, 0.0f, 1.0f);
         on.isNoteOn = true;
         on.frameOffset = frameOf(n.on);
         on.source = this;
         on.voiceId = NextVoiceId();
         FillExpr(s, n, 0.0, on);
         Held h { n.on, n.off, n.key, (uint8_t)pitch, n.relVel, on.voiceId, (int)(it - notes.begin()) };
         Emit(on);
         if (n.off < tEnd)
            PushOff(h, std::max(on.frameOffset, frameOf(n.off)));
         else if (mHeldCount < kMaxHeld)
            mHeld[mHeldCount++] = h;
         else
            PushOff(h, numFrames - 1); // more than kMaxHeld at once: the newest is a blip, never a stuck note
      }

      // Expression on held notes, every kExprFrames.
      int budget = kMaxExprEvents;
      for (int i = 0; i < mHeldCount && budget > 0; i++)
      {
         const Held& h = mHeld[i];
         const SchedNote& n = notes[(size_t)h.index];
         if (n.exprN[0] == 0 && n.exprN[1] == 0 && n.exprN[2] == 0)
            continue;
         for (int f = 0; f < numFrames && budget > 0; f += kExprFrames, budget--)
         {
            const double b = tStart + dT * (double)f / (double)numFrames;
            if (b < h.on)
               continue;
            NoteEvent e;
            e.note = h.outNote;
            e.isNoteOn = false;
            e.bendUpdate = true;
            e.frameOffset = f;
            e.source = this;
            e.voiceId = h.voiceId;
            FillExpr(s, n, b - h.on, e);
            Emit(e);
         }
      }
      Flush();
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override { mInbox = inbox; mNoteCursor = cursor; }

   // Main thread only.
   void PushParams(const PianoRollNode& n)
   {
      mTranspose.store(n.transpose, std::memory_order_relaxed);
      mVelocity.store(n.velocity, std::memory_order_relaxed);
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
      mThru.store(n.thru, std::memory_order_relaxed);
   }
   void Publish(const NoteSchedule* s) { mSched.store(s, std::memory_order_release); }
   uint64_t Acked() const { return mAck.load(std::memory_order_acquire); }
   long long Blocks() const { return mBlocks.load(std::memory_order_relaxed); }
   double Playhead() const { return mPlayhead.load(std::memory_order_relaxed); }

private:
   static constexpr int kMaxHeld = 128;
   static constexpr int kExprFrames = 64;
   static constexpr int kMaxExprEvents = 128;
   static constexpr int kMaxOut = 512;
   struct Held
   {
      double on, off;
      uint32_t key;
      uint8_t outNote, relVel;
      int voiceId;
      int index; // into the current schedule's notes
   };

   static void FillExpr(const NoteSchedule* s, const SchedNote& n, double at, NoteEvent& e)
   {
      if (n.exprN[0] > 0)
         e.bendSemitones = Arrange::SampleExpr(&s->exprs[n.expr[0]], n.exprN[0], at);
      if (n.exprN[1] > 0 || n.exprN[2] > 0)
      {
         e.hasExpression = true;
         e.pressure = n.exprN[1] > 0 ? Arrange::SampleExpr(&s->exprs[n.expr[1]], n.exprN[1], at) : 0.0f;
         e.slide = n.exprN[2] > 0 ? Arrange::SampleExpr(&s->exprs[n.expr[2]], n.exprN[2], at) : 0.0f;
      }
   }

   // A new schedule landed. Each held note is looked up by (on, key): still
   // there, it keeps sounding to its new off; gone, it ends now. The old
   // schedule is never read here - only the values the held array copied.
   void Swap(const NoteSchedule* s)
   {
      for (int i = 0; i < mHeldCount;)
      {
         Held& h = mHeld[i];
         int found = -1;
         if (s)
         {
            auto it = std::lower_bound(s->notes.begin(), s->notes.end(), h.on - 1e-9,
                                       [](const SchedNote& n, double b) { return n.on < b; });
            for (; it != s->notes.end() && it->on <= h.on + 1e-9; ++it)
               if (it->key == h.key)
               {
                  found = (int)(it - s->notes.begin());
                  break;
               }
         }
         if (found >= 0 && s->notes[(size_t)found].off > mLastTransport)
         {
            h.off = s->notes[(size_t)found].off;
            h.index = found;
            i++;
         }
         else
         {
            PushOff(h, 0);
            mHeld[i] = mHeld[--mHeldCount];
         }
      }
   }

   void PushOff(const Held& h, int offset)
   {
      NoteEvent off;
      off.note = h.outNote;
      off.velocity = (float)h.relVel / 127.0f;
      off.isNoteOn = false;
      off.frameOffset = offset;
      off.source = this;
      off.voiceId = h.voiceId;
      Emit(off);
   }
   void ReleaseAll(int offset)
   {
      for (int i = 0; i < mHeldCount; i++)
         PushOff(mHeld[i], offset);
      mHeldCount = 0;
   }

   void Emit(const NoteEvent& e)
   {
      if (mOutCount < kMaxOut)
         mOut[mOutCount++] = e;
   }
   // Frame order for the consumers; equal frames keep their order, so an off
   // queued before an on of the same pitch stays first.
   void Flush()
   {
      // Insertion sort: stable, in place (std::stable_sort may allocate), and
      // the events arrive almost sorted.
      for (int i = 1; i < mOutCount; i++)
      {
         const NoteEvent e = mOut[i];
         int j = i;
         for (; j > 0 && mOut[j - 1].frameOffset > e.frameOffset; j--)
            mOut[j] = mOut[j - 1];
         mOut[j] = e;
      }
      for (int i = 0; i < mOutCount; i++)
         mOutbox.Push(mOut[i]);
      mOutCount = 0;
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;
   double mSampleRate = 48000.0;
   double mLastTransport = -1.0;
   uint32_t mPass = 0;
   uint64_t mGen = 0;
   Held mHeld[kMaxHeld] = {};
   int mHeldCount = 0;
   NoteEvent mOut[kMaxOut];
   int mOutCount = 0;
   std::atomic<const NoteSchedule*> mSched { nullptr };
   std::atomic<uint64_t> mAck { 0 };
   std::atomic<long long> mBlocks { 0 };
   std::atomic<int> mTranspose { 0 };
   std::atomic<float> mVelocity { 1.0f };
   std::atomic<bool> mUseGlobalScale { false };
   std::atomic<bool> mThru { true };
   std::atomic<double> mPlayhead { -1.0 };
};

PianoRollNode::PianoRollNode() = default;
PianoRollNode::~PianoRollNode() = default;

void PianoRollNode::SetSchedule(std::unique_ptr<NoteSchedule> s)
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPianoRollNode>();
   if (s)
   {
      s->gen = mNextGen++;
      mCurrent = s.get();
      mSchedules.push_back(std::move(s));
   }
   else
      mCurrent = nullptr;
   mAudioNode->Publish(mCurrent);
   RetireSchedules();
}

// Frees every schedule the audio thread has moved past. If the audio thread
// has not run a block for two seconds (audio stopped, node not in the
// topology) it holds none of them, and all but the current one go.
void PianoRollNode::RetireSchedules()
{
   if (mSchedules.size() <= 1 || !mAudioNode)
      return;
   const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
   const long long blocks = mAudioNode->Blocks();
   if (blocks != mBlocksSeen)
   {
      mBlocksSeen = blocks;
      mBlocksStillMs = nowMs;
   }
   const bool idle = nowMs - mBlocksStillMs > 2000;
   const uint64_t acked = mAudioNode->Acked();
   mSchedules.erase(std::remove_if(mSchedules.begin(), mSchedules.end() - 1,
                                   [&](const std::unique_ptr<NoteSchedule>& p) { return idle || p->gen < acked; }),
                    mSchedules.end() - 1);
}

double PianoRollNode::PlayheadBeats() const
{
   return mAudioNode ? mAudioNode->Playhead() : -1.0;
}

void PianoRollNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPianoRollNode>();
   mAudioNode->PushParams(*this);
   RetireSchedules();
}

void PianoRollNode::VisitParams(ParamVisitor& v)
{
   v.Int("transpose", transpose);
   v.Float("velocity", velocity);
   v.Bool("useGlobalScale", useGlobalScale);
   v.Bool("thru", thru);
}

AudioNode* PianoRollNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPianoRollNode>();
   return mAudioNode.get();
}

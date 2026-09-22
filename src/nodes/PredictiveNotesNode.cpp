#include "nodes/PredictiveNotesNode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEventQueue.h"
#include "audio/NoteTheory.h"
#include "core/Transport.h"

using NoteModel::Event;
using NoteModel::Tables;

// ------------------------------------------------------------------ audio object
class AudioPredictiveNotesNode : public AudioNode
{
public:
   using Cap = PredictiveNotesNode::Cap;
   static constexpr int kRing = 4096;
   static constexpr int kMaxPending = 64;
   static constexpr int kMaxBlockEvents = 192;

   ~AudioPredictiveNotesNode() override
   {
      delete mLive.load();
      for (Retired& r : mRetired)
         delete r.tables;
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mHasNext = false;
      for (Pending& p : mPending)
         p.active = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      Transport& tr = Transport::Instance();
      Render(output.numFrames, tr.Beats(), (double)tr.Tempo(), tr.BeatsPerBar());
   }

   // The whole block, with the clock passed in so a test can drive it without a transport.
   void Render(int numFrames, double beatsStart, double bpm, double bpb)
   {
      NoteEvent in[64];
      const int nIn = (mInbox != nullptr) ? mInbox->Pop(mNoteCursor, in, 64) : 0;
      NoteEvent out[kMaxBlockEvents];
      int nOut = 0;
      const double bps = std::max(1.0, bpm) / 60.0 / mSampleRate; // beats per sample
      const double end = beatsStart + (double)numFrames * bps;
      const bool learning = mLearning.load(std::memory_order_relaxed);

      if (beatsStart < mLastBeatsStart - 1e-6)
      {
         mHasNext = false;
         FlushOffs(out, nOut);
      }
      mLastBeatsStart = beatsStart;

      const int srcMode = mSourceMode.load(std::memory_order_relaxed);
      if (srcMode == 1)
      {
         // Source B: deterministic mapping from movement / transport
         const int lo = mLo.load(std::memory_order_relaxed);
         const int hi = std::max(lo, mHi.load(std::memory_order_relaxed));
         const bool useGlobal = mUseGlobalScale.load(std::memory_order_relaxed);
         const int root = useGlobal ? Transport::Instance().Key() : 0;
         const int scale = useGlobal ? Transport::Instance().Scale() : 0;
         int scaleNotes[NoteTheory::kMaxScaleNotes];
         int nScale = NoteTheory::CollectScaleNotes(root, scale, lo, hi, scaleNotes, NoteTheory::kMaxScaleNotes);
         if (nScale > 0)
         {
            const double stepBeats = 0.5; // 8th note grid
            double nextStep = std::floor(beatsStart / stepBeats) * stepBeats;
            while (nextStep < beatsStart)
               nextStep += stepBeats;
            while (nextStep < end && nOut < kMaxBlockEvents - 2)
            {
               int noteIdx = (int)std::floor((std::sin(nextStep * 1.5) * 0.5 + 0.5) * (nScale - 1));
               noteIdx = std::clamp(noteIdx, 0, nScale - 1);
               int note = scaleNotes[noteIdx];
               float vel = std::clamp(0.6f + 0.3f * (float)std::cos(nextStep * 2.0), 0.1f, 1.0f);

               NoteEvent on;
               on.note = note;
               on.velocity = vel;
               on.isNoteOn = true;
               on.frameOffset = std::clamp((int)((nextStep - beatsStart) / bps), 0, numFrames - 1);
               on.source = this;
               on.voiceId = NextVoiceId();
               out[nOut++] = on;

               NoteEvent off;
               off.note = note;
               off.velocity = 0.0f;
               off.isNoteOn = false;
               double offBeat = nextStep + stepBeats * 0.8;
               off.frameOffset = std::clamp((int)((offBeat - beatsStart) / bps), on.frameOffset + 1, numFrames - 1);
               off.source = this;
               off.voiceId = on.voiceId;
               out[nOut++] = off;

               mLastNote.store(note, std::memory_order_relaxed);
               nextStep += stepBeats;
            }
         }
      }
      else if (learning)
      {
         for (int i = 0; i < nIn; i++)
         {
            const NoteEvent& e = in[i];
            if (!e.bendUpdate && e.note >= 0 && e.note <= 127)
            {
               Cap c;
               c.beat = beatsStart + (double)e.frameOffset * bps;
               c.voiceId = e.voiceId;
               c.note = (uint8_t)e.note;
               c.vel = (uint8_t)std::clamp((int)std::lround(e.velocity * 127.0f), 0, 127);
               c.on = e.isNoteOn;
               PushCapture(c);
            }
            if (nOut < kMaxBlockEvents)
               out[nOut++] = e; // pass through: you keep hearing what you play while it learns
         }
         FlushOffs(out, nOut);
         mHasNext = false;
      }
      else
      {
         const Tables* T = mLive.load(std::memory_order_acquire);
         if (T == nullptr || T->nEvents == 0)
         {
            FlushOffs(out, nOut);
            mHasNext = false;
         }
         else
         {
            mParams.stray = mStray.load(std::memory_order_relaxed);
            mParams.memory = mMemory.load(std::memory_order_relaxed);
            mParams.lengthSpread = mLenSpread.load(std::memory_order_relaxed);
            mParams.velSpread = mVelSpread.load(std::memory_order_relaxed);
            mParams.lowNote = mLo.load(std::memory_order_relaxed);
            mParams.highNote = std::max(mParams.lowNote, mHi.load(std::memory_order_relaxed));
            if (mPlayer.Bound() != T)
            {
               mPlayer.Reset(T, (uint32_t)mSeed.load(std::memory_order_relaxed));
               mHasNext = false;
            }
            if (mHasNext && (mNext.onsetBeats > beatsStart + 64.0 || mNext.onsetBeats < beatsStart - 2.0))
               mHasNext = false;
            if (!mHasNext)
            {
               mPrevOnset = std::floor(beatsStart * NoteModel::kTicksPerBeat) / NoteModel::kTicksPerBeat;
               mPlayer.Next(*T, mParams, mPrevOnset, bpb, mNext);
               mHasNext = true;
            }
            int guard = 0;
            while (mNext.onsetBeats < end && guard++ < 64 && nOut < kMaxBlockEvents - 1)
            {
               Pending* slot = nullptr;
               for (Pending& p : mPending)
                  if (!p.active)
                  {
                     slot = &p;
                     break;
                  }
               if (slot != nullptr)
               {
                  const bool useGlobal = mUseGlobalScale.load(std::memory_order_relaxed);
                  int outNote = mNext.note;
                  if (useGlobal)
                     outNote = MusicTime::SnapToScaleInRange(outNote, Transport::Instance().Key(), Transport::Instance().Scale(), mParams.lowNote, mParams.highNote);

                  NoteEvent on;
                  on.note = outNote;
                  on.velocity = mNext.velocity;
                  on.isNoteOn = true;
                  on.frameOffset = std::clamp((int)((mNext.onsetBeats - beatsStart) / bps), 0, numFrames - 1);
                  on.source = this;
                  on.voiceId = NextVoiceId();
                  out[nOut++] = on;
                  slot->active = true;
                  slot->note = outNote;
                  slot->voiceId = on.voiceId;
                  slot->offBeat = mNext.onsetBeats + mNext.durBeats;
                  mLastNote.store(outNote, std::memory_order_relaxed);
               }
               mPrevOnset = mNext.onsetBeats;
               mPlayer.Next(*T, mParams, mPrevOnset, bpb, mNext);
            }
            for (Pending& p : mPending)
               if (p.active && p.offBeat < end && nOut < kMaxBlockEvents)
               {
                  NoteEvent off;
                  off.note = p.note;
                  off.velocity = 0.0f;
                  off.isNoteOn = false;
                  off.frameOffset = std::clamp((int)((p.offBeat - beatsStart) / bps), 0, numFrames - 1);
                  off.source = this;
                  off.voiceId = p.voiceId;
                  out[nOut++] = off;
                  p.active = false;
               }
            // Time order, note-offs before note-ons on a tie so a repeated pitch re-attacks cleanly.
            std::stable_sort(out, out + nOut, [](const NoteEvent& a, const NoteEvent& b) {
               if (a.frameOffset != b.frameOffset)
                  return a.frameOffset < b.frameOffset;
               return !a.isNoteOn && b.isNoteOn;
            });
         }
      }

      for (int i = 0; i < nOut; i++)
         mOutbox.Push(out[i]);
      mSamplePos += (uint64_t)numFrames;
      mBlocks.fetch_add(1, std::memory_order_release);
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override { mInbox = inbox; mNoteCursor = cursor; }

   // ---- main thread ----
   void PushParams(const PredictiveNotesNode& n, bool learning)
   {
      mSourceMode.store(n.sourceMode, std::memory_order_relaxed);
      mStray.store(n.stray, std::memory_order_relaxed);
      mMemory.store(n.memory, std::memory_order_relaxed);
      mLenSpread.store(n.lengthSpread, std::memory_order_relaxed);
      mVelSpread.store(n.velocitySpread, std::memory_order_relaxed);
      mLo.store(n.rangeLow, std::memory_order_relaxed);
      mHi.store(n.rangeHigh, std::memory_order_relaxed);
      mSeed.store(n.seed, std::memory_order_relaxed);
      mUseGlobalScale.store(n.useGlobalScale, std::memory_order_relaxed);
      mLearning.store(learning, std::memory_order_relaxed);
   }

   bool PopCapture(Cap& c)
   {
      const size_t head = mHead.load(std::memory_order_relaxed);
      if (head == mTail.load(std::memory_order_acquire))
         return false;
      c = mRing[head % kRing];
      mHead.store(head + 1, std::memory_order_release);
      return true;
   }
   void PushCapture(const Cap& c) // audio thread (tests: the "audio" thread)
   {
      const size_t tail = mTail.load(std::memory_order_relaxed);
      if (tail - mHead.load(std::memory_order_acquire) >= (size_t)kRing)
      {
         mDropped.fetch_add(1, std::memory_order_relaxed);
         return;
      }
      mRing[tail % kRing] = c;
      mTail.store(tail + 1, std::memory_order_release);
   }

   Tables* SwapTables(Tables* next) { return mLive.exchange(next, std::memory_order_acq_rel); }
   void Retire(Tables* old)
   {
      if (old != nullptr)
         mRetired.push_back({ old, mBlocks.load(std::memory_order_acquire) });
   }
   // Frees tables the audio thread has provably moved past: it loads the live pointer once per
   // block, so once two more blocks have finished no block can still hold the retired one.
   int CollectRetired()
   {
      const uint64_t now = mBlocks.load(std::memory_order_acquire);
      int freed = 0;
      for (size_t i = 0; i < mRetired.size();)
      {
         if (now >= mRetired[i].stamp + 2)
         {
            delete mRetired[i].tables;
            mRetired.erase(mRetired.begin() + (long)i);
            mLastFreeThread = std::this_thread::get_id();
            freed++;
         }
         else
            i++;
      }
      return freed;
   }
   uint64_t Blocks() const { return mBlocks.load(std::memory_order_acquire); }
   int Dropped() const { return mDropped.load(std::memory_order_relaxed); }
   std::thread::id LastFreeThread() const { return mLastFreeThread; }
   int LastNote() const { return mLastNote.load(std::memory_order_relaxed); }
   int RetiredCount() const { return (int)mRetired.size(); }

private:
   struct Pending
   {
      bool active = false;
      int note = 0;
      int voiceId = 0;
      double offBeat = 0.0;
   };
   struct Retired
   {
      Tables* tables;
      uint64_t stamp;
   };

   void FlushOffs(NoteEvent* out, int& nOut)
   {
      for (Pending& p : mPending)
         if (p.active && nOut < kMaxBlockEvents)
         {
            NoteEvent off;
            off.note = p.note;
            off.velocity = 0.0f;
            off.isNoteOn = false;
            off.frameOffset = 0;
            off.source = this;
            off.voiceId = p.voiceId;
            out[nOut++] = off;
            p.active = false;
         }
   }

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;

   NoteModel::Player mPlayer;
   NoteModel::Params mParams;
   NoteModel::Out mNext;
   bool mHasNext = false;
   double mPrevOnset = 0.0;
   double mLastBeatsStart = 0.0;
   Pending mPending[kMaxPending];

   std::atomic<Tables*> mLive { nullptr };
   std::vector<Retired> mRetired; // main thread only
   std::thread::id mLastFreeThread;
   std::atomic<uint64_t> mBlocks { 0 };

   Cap mRing[kRing];
   std::atomic<size_t> mHead { 0 }, mTail { 0 };
   std::atomic<int> mDropped { 0 };

   std::atomic<float> mStray { 0.5f }, mLenSpread { 0.25f }, mVelSpread { 0.25f };
   std::atomic<int> mMemory { 4 }, mLo { 36 }, mHi { 96 }, mSeed { 1 };
   std::atomic<int> mSourceMode { 0 };
   std::atomic<bool> mUseGlobalScale { false };
   std::atomic<bool> mLearning { false };
   std::atomic<int> mLastNote { -1 };
};

// ------------------------------------------------------------------ main-thread node
namespace
{
   // Captured note on/off pairs -> snapped, bar-aware training events. Note-offs are matched to their
   // note-on by voiceId (pitch alone mis-pairs a doubled or retriggered note); a note still held when
   // Learn stops lasts until the last captured beat.
   std::vector<Event> Assemble(const std::vector<PredictiveNotesNode::Cap>& caps, double bpb)
   {
      struct Rec { double onset, dur; uint8_t note, vel; };
      std::vector<Rec> recs;
      struct Open { int voice; size_t idx; };
      std::vector<Open> open;
      double lastBeat = 0.0;
      for (const auto& c : caps)
      {
         lastBeat = std::max(lastBeat, c.beat);
         if (c.on)
         {
            recs.push_back({ c.beat, -1.0, c.note, c.vel });
            open.push_back({ c.voiceId, recs.size() - 1 });
         }
         else
         {
            for (size_t i = open.size(); i-- > 0;)
               if (open[i].voice == c.voiceId || (c.voiceId == 0 && recs[open[i].idx].note == c.note))
               {
                  recs[open[i].idx].dur = std::max(0.0, c.beat - recs[open[i].idx].onset);
                  open.erase(open.begin() + (long)i);
                  break;
               }
         }
      }
      for (Rec& r : recs)
         if (r.dur < 0.0)
            r.dur = std::max(0.25, lastBeat - r.onset);
      std::stable_sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) { return a.onset < b.onset; });

      std::vector<Event> ev;
      const int n = std::min<int>((int)recs.size(), NoteModel::kMaxEvents);
      ev.reserve(n);
      int prevTick = n ? NoteModel::SnapTicks(recs[0].onset) : 0;
      for (int i = 0; i < n; i++)
      {
         const int tick = NoteModel::SnapTicks(recs[i].onset);
         Event e;
         e.note = recs[i].note;
         e.vel = recs[i].vel;
         e.ioiTicks = (uint16_t)std::clamp(tick - prevTick, 0, NoteModel::kMaxIoiTicks);
         const double bar = std::fmod((double)tick / NoteModel::kTicksPerBeat, std::max(1.0, bpb));
         e.metric = (uint8_t)NoteModel::MetricOf(bar < 0.0 ? bar + bpb : bar);
         e.durTicks = (uint16_t)std::clamp(NoteModel::SnapTicks(recs[i].dur), 1, NoteModel::kMaxIoiTicks);
         ev.push_back(e);
         prevTick = tick;
      }
      return ev;
   }
}

PredictiveNotesNode::PredictiveNotesNode() = default;

PredictiveNotesNode::~PredictiveNotesNode()
{
   if (mBuild.valid())
      delete mBuild.get(); // finished-but-unswapped tables; the live and retired ones die with the audio object
}

AudioNode* PredictiveNotesNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPredictiveNotesNode>();
   return mAudioNode.get();
}

void PredictiveNotesNode::VisitParams(ParamVisitor& v)
{
   v.Int("sourceMode", sourceMode);
   v.Float("stray", stray);
   v.Int("memory", memory);
   v.Float("lengthSpread", lengthSpread);
   v.Float("velocitySpread", velocitySpread);
   v.Int("rangeLow", rangeLow);
   v.Int("rangeHigh", rangeHigh);
   v.Int("seed", seed);
   v.Bool("useGlobalScale", useGlobalScale);
   v.Text("model", model);
}

float PredictiveNotesNode::Confidence01() const
{
   // This used to be a shape, not a measurement: a fixed 0.85 for Movement source, a
   // notes-captured ramp while learning, and 0.40 + 0.50*(1 - exp(-learned/20)) afterwards - so a
   // model that predicted nothing still displayed ~90% conf once enough notes had gone past it.
   //
   // The node already computes the only honest number available: mCurve holds the held-out
   // cross-entropy gain in bits per event over an order-0 baseline (UpdateMeter, via
   // NoteModel::HeldOutCrossEntropy). Report that, mapped through 1 - 2^-gain, which is the
   // fraction of the baseline's uncertainty the model actually removes: 0 bits -> 0%, 1 bit ->
   // 50%, 2 bits -> 75%. No floor, because "I have learned nothing yet" is a real answer and the
   // status line next to this badge already says what it is doing instead.
   // Source B (Movement) never learns anything - it's a deterministic scale walk driven by
   // Transport, not the Markov model mCurve was measured against. Reporting mCurve's last
   // Markov gain here would show a stale, irrelevant number while Movement is actually playing.
   if (sourceMode != 0)
      return 0.0f;
   if (mCurve.empty())
      return 0.0f;
   const float gain = mCurve.back();
   if (!(gain > 0.0f))
      return 0.0f;
   return std::clamp(1.0f - std::exp2(-gain), 0.0f, 1.0f);
}

int PredictiveNotesNode::LastNote() const { return mAudioNode ? mAudioNode->LastNote() : -1; }

void PredictiveNotesNode::SetLearning(bool on)
{
   if (on == mLearning)
      return;
   if (!on)
   {
      FinishLearn();
      return;
   }
   GetAudioNode();
   NoteModel::Tables* unused = nullptr;
   (void)unused;
   AudioPredictiveNotesNode::Cap stale;
   while (mAudioNode->PopCapture(stale))
   {
   }
   mCaps.clear();
   mCurve.clear();
   mNotesCaptured = 0;
   mBars = 0;
   mLastLearnTooShort = false;
   mBeatsPerBar = Transport::Instance().BeatsPerBar();
   mLearning = true;
}

void PredictiveNotesNode::DrainCaptures()
{
   Cap c;
   while (mAudioNode->PopCapture(c))
   {
      if (mCaps.size() < 16384)
         mCaps.push_back(c);
      if (c.on)
         mNotesCaptured++;
   }
}

void PredictiveNotesNode::UpdateMeter(bool force)
{
   if (mCaps.empty())
      return;
   const double first = mCaps.front().beat, last = mCaps.back().beat;
   const int bars = (int)(std::floor(last / mBeatsPerBar) - std::floor(first / mBeatsPerBar));
   if (!force && bars <= mBars)
      return;
   mBars = bars;
   const std::vector<Event> ev = Assemble(mCaps, mBeatsPerBar);
   float ceModel = 0.0f, ceBase = 0.0f;
   if (NoteModel::HeldOutCrossEntropy(ev, std::clamp(memory, 0, NoteModel::kMaxOrder), ceModel, ceBase))
      mCurve.push_back(ceBase - ceModel);

   // Stop rules (README §8.3): the default window of 8 bars and 64 notes, or a plateau - three bars
   // improving the held-out gain by under 2% - once there is enough to have learned something.
   const int notes = (int)ev.size();
   const bool full = (bars >= 8 && notes >= 64) || notes >= NoteModel::kMaxEvents;
   bool plateau = false;
   if (mCurve.size() >= 4 && notes >= 16)
   {
      const float g0 = mCurve[mCurve.size() - 4], g1 = mCurve.back();
      plateau = (g1 - g0) < 0.02f * std::max(std::abs(g0), 0.05f);
   }
   if (full || plateau)
      FinishLearn();
}

void PredictiveNotesNode::FinishLearn()
{
   if (!mLearning)
      return;
   DrainCaptures();
   mLearning = false;
   mAudioNode->PushParams(*this, false);
   const std::vector<Event> ev = Assemble(mCaps, mBeatsPerBar);
   mCaps.clear();
   mLastLearnTooShort = ev.size() < 2;
   if (ev.size() >= 2)
   {
      model = NoteModel::EncodeEvents(ev);
      mAppliedModel = model;
      mLearned = (int)ev.size();
      StartBuild(ev);
   }
   // else: too little captured to replace anything - the previous model/tables (if any) are
   // left exactly as they were, and mLastLearnTooShort tells the status line why.
}

void PredictiveNotesNode::StartBuild(const std::vector<Event>& events)
{
   if (mBuild.valid())
   {
      mQueued = true;
      mQueuedEvents = events;
      return;
   }
   mBuild = std::async(std::launch::async, [events]() { return NoteModel::Build(events); });
}

void PredictiveNotesNode::SwapIn(Tables* t)
{
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapTables(t));
}

void PredictiveNotesNode::PollBuild()
{
   if (!mBuild.valid() || mBuild.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
   SwapIn(mBuild.get());
   if (mQueued)
   {
      mQueued = false;
      std::vector<Event> ev = std::move(mQueuedEvents);
      StartBuild(ev);
   }
}

void PredictiveNotesNode::WaitForBuild()
{
   GetAudioNode();
   while (mBuild.valid())
   {
      mBuild.wait();
      PollBuild();
   }
}

void PredictiveNotesNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();

   // The saved model text changed (patch load, undo): decode and rebuild the tables from it.
   if (model != mAppliedModel)
   {
      mAppliedModel = model;
      std::vector<Event> ev;
      if (NoteModel::DecodeEvents(model, ev) && ev.size() >= 2)
      {
         mLearned = (int)ev.size();
         // Re-measure the model that just came off disk. Confidence01 reports the held-out gain
         // in mCurve, which is runtime-only state - without this a patch-loaded model would read
         // 0% forever even though the same events measured fine in the session that learned
         // them. Cheap: the same call UpdateMeter already makes once per captured bar.
         mCurve.clear();
         float ceModel = 0.0f, ceBase = 0.0f;
         if (NoteModel::HeldOutCrossEntropy(ev, std::clamp(memory, 0, NoteModel::kMaxOrder), ceModel, ceBase))
            mCurve.push_back(ceBase - ceModel);
         StartBuild(ev);
      }
      else
      {
         mLearned = 0;
         mCurve.clear();
         SwapIn(nullptr);
      }
   }
   PollBuild();
   mAudioNode->PushParams(*this, mLearning);
   mAudioNode->CollectRetired();
   if (mLearning)
   {
      DrainCaptures();
      UpdateMeter(false);
   }
}

// ------------------------------------------------------------------ tests
namespace
{
   int gFail = 0;
#define PM_CHECK(cond, ...)                                          \
   do                                                                \
   {                                                                 \
      if (!(cond))                                                   \
      {                                                              \
         gFail++;                                                    \
         std::printf("[PREDMIDITEST] FAIL: " __VA_ARGS__);           \
         std::printf("\n");                                          \
      }                                                              \
   } while (0)

   struct Xs
   {
      uint32_t s = 12345;
      uint32_t Next()
      {
         s ^= s << 13; s ^= s >> 17; s ^= s << 5;
         return s;
      }
      int Below(int n) { return (int)(Next() % (uint32_t)n); }
   };

   Event Ev(int note, int ioi, int dur, int vel = 90, int metric = 0)
   {
      Event e;
      e.note = (uint8_t)note;
      e.ioiTicks = (uint16_t)ioi;
      e.durTicks = (uint16_t)dur;
      e.vel = (uint8_t)vel;
      e.metric = (uint8_t)metric;
      return e;
   }

   bool InCMajor(int n)
   {
      static const bool k[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
      return k[n % 12];
   }

   std::vector<Event> LoopEvents(int periods)
   {
      static const int loop[16] = { 60, 64, 67, 62, 65, 69, 72, 60, 59, 62, 67, 71, 64, 60, 65, 69 };
      std::vector<Event> ev;
      for (int p = 0; p < periods; p++)
         for (int i = 0; i < 16; i++)
            ev.push_back(Ev(loop[i], i == 0 && p == 0 ? 0 : 12, 9, 96, ((i * 2) % 16)));
      return ev;
   }
}

void PredictiveNotesNode::SweepPrepare()
{
   // Notes mode plays nothing until a model is learned; give it one so every param is audible.
   model = NoteModel::EncodeEvents(LoopEvents(2));
   CookIfNeeded(0);
   WaitForBuild();
}

namespace PredictiveNotes
{
   bool RunPredMidiTest()
   {
      gFail = 0;
      using namespace NoteModel;

      // 1. A two-bar loop, twice: with Stray at minimum the output is the loop, note for note.
      {
         const std::vector<Event> ev = LoopEvents(2);
         std::unique_ptr<Tables> T(Build(ev));
         Player pl;
         pl.Reset(T.get(), 7);
         Params p;
         p.stray = 0.0f;
         p.memory = 8;
         p.lengthSpread = p.velSpread = 0.0f;
         static const int loop[16] = { 60, 64, 67, 62, 65, 69, 72, 60, 59, 62, 67, 71, 64, 60, 65, 69 };
         double onset = 100.0;
         int bad = 0;
         for (int i = 0; i < 48; i++)
         {
            Out o;
            pl.Next(*T, p, onset, 4.0, o);
            if (o.note != loop[i % 16] || o.ioiTicks != 12 || std::abs(o.velocity * 127.0f - 96.0f) > 1.0f)
               bad++;
            onset = o.onsetBeats;
         }
         PM_CHECK(bad == 0, "replay deviated from the loop on %d of 48 notes", bad);
      }

      // 2. Random C-major stream: faithful sampling stays in key; Stray max is uniform in Range.
      {
         Xs r;
         static const int cm[7] = { 0, 2, 4, 5, 7, 9, 11 };
         std::vector<Event> ev;
         for (int i = 0; i < 200; i++)
            ev.push_back(Ev(48 + 12 * r.Below(3) + cm[r.Below(7)], i ? 6 : 0, 5, 80 + r.Below(30), (i * 1) % 16));
         std::unique_ptr<Tables> T(Build(ev));
         Player pl;
         pl.Reset(T.get(), 3);
         Params p;
         p.lowNote = 48;
         p.highNote = 83;
         p.memory = 3;
         p.stray = 0.5f;
         double onset = 0.0;
         int outKey = 0;
         for (int i = 0; i < 1000; i++)
         {
            Out o;
            pl.Next(*T, p, onset, 4.0, o);
            onset = o.onsetBeats;
            if (!InCMajor(o.note) || o.note < 48 || o.note > 83)
               outKey++;
         }
         PM_CHECK(outKey == 0, "%d faithful notes left C major / the range", outKey);

         p.stray = 1.0f;
         p.lowNote = 48;
         p.highNote = 72;
         int counts[128] = {};
         const int N = 6000;
         pl.Reset(T.get(), 5);
         for (int i = 0; i < N; i++)
         {
            Out o;
            pl.Next(*T, p, onset, 4.0, o);
            onset = o.onsetBeats;
            counts[std::clamp(o.note, 0, 127)]++;
         }
         const float expect = (float)N / 25.0f;
         int worst = 0, outside = 0;
         for (int n = 0; n < 128; n++)
         {
            if (n < 48 || n > 72)
               outside += counts[n];
            else if (counts[n] < expect * 0.6f || counts[n] > expect * 1.5f)
               worst++;
         }
         PM_CHECK(outside == 0 && worst == 0, "stray max not uniform in range (outside %d, uneven %d)", outside, worst);

         float cm2, cb;
         PM_CHECK(HeldOutCrossEntropy(LoopEvents(4), 4, cm2, cb) && cm2 < cb - 0.5f,
                  "held-out gain of the blend over order 0 too small (%.2f vs %.2f bits)", cm2, cb);
      }

      // 3. Table swap during playback on a live "audio" thread: no crash, notes keep coming, and the
      //    retired tables are freed on this (main) thread.
      {
         PredictiveNotesNode n;
         n.model = EncodeEvents(LoopEvents(2));
         n.stray = 0.4f;
         n.CookIfNeeded(1);
         n.WaitForBuild();
         AudioPredictiveNotesNode* a = n.Audio();
         a->PrepareToPlay(48000.0, 512);
         NoteEventQueue* q = a->NoteOutbox();
         q->ResetConsumers();
         const int cur = q->RegisterConsumer();
         std::atomic<bool> run { true };
         std::thread audio([&]() {
            double beats = 0.0;
            const double bps = 120.0 / 60.0 / 48000.0;
            while (run.load())
            {
               a->Render(512, beats, 120.0, 4.0);
               beats += 512.0 * bps;
               std::this_thread::sleep_for(std::chrono::microseconds(150));
            }
         });
         int ons = 0, offs = 0, outOfRange = 0;
         auto drain = [&]() {
            NoteEvent b[64];
            int k;
            while ((k = q->Pop(cur, b, 64)) > 0)
               for (int i = 0; i < k; i++)
               {
                  (b[i].isNoteOn ? ons : offs)++;
                  if (b[i].isNoteOn && (b[i].note < 36 || b[i].note > 96))
                     outOfRange++;
               }
         };
         const std::thread::id mainId = std::this_thread::get_id();
         for (int k = 0; k < 12; k++)
         {
            const uint64_t target = a->Blocks() + 80;
            while (a->Blocks() < target)
            {
               drain();
               std::this_thread::yield();
            }
            std::vector<Event> alt = LoopEvents(2);
            for (Event& e : alt)
               e.note = (uint8_t)std::min(127, e.note + k % 5);
            n.TestSwapTables(Build(alt));
            n.CookIfNeeded(2 + k);
         }
         const uint64_t target = a->Blocks() + 6;
         while (a->Blocks() < target)
            std::this_thread::yield();
         n.CookIfNeeded(100);
         run.store(false);
         audio.join();
         drain();
         PM_CHECK(ons >= 30, "only %d notes played across the swaps", ons);
         PM_CHECK(outOfRange == 0, "%d notes outside Range", outOfRange);
         PM_CHECK(ons - offs >= 0 && ons - offs <= AudioPredictiveNotesNode::kMaxPending, "unbalanced ons %d offs %d", ons, offs);
         PM_CHECK(a->RetiredCount() == 0 && a->LastFreeThread() == mainId, "retired tables not freed on the main thread");
      }

      // 4. Deleting the node while it has tables, builds and pending offs in flight.
      {
         auto* n = new PredictiveNotesNode();
         n->model = EncodeEvents(LoopEvents(2));
         n->CookIfNeeded(1);
         n->GetAudioNode()->PrepareToPlay(48000.0, 512);
         n->Audio()->Render(512, 0.0, 120.0, 4.0);
         n->model = EncodeEvents(LoopEvents(3));
         n->CookIfNeeded(2);
         delete n; // a build is in flight here
      }

      // 5. Save / load: the model text round-trips and a fresh node plays identically with one seed.
      {
         const std::vector<Event> ev = LoopEvents(2);
         std::vector<Event> back;
         PM_CHECK(DecodeEvents(EncodeEvents(ev), back) && back.size() == ev.size() &&
                     std::memcmp(back.data(), ev.data(), ev.size() * sizeof(Event)) == 0,
                  "model text did not round-trip");
         auto run = [&](const std::string& text) {
            PredictiveNotesNode n;
            n.model = text;
            n.seed = 9;
            n.stray = 0.55f;
            n.CookIfNeeded(1);
            n.WaitForBuild();
            std::vector<int> notes;
            n.Audio()->PrepareToPlay(48000.0, 512);
            NoteEventQueue* q = n.Audio()->NoteOutbox();
            q->ResetConsumers();
            const int cur = q->RegisterConsumer();
            double beats = 0.0;
            for (int b = 0; b < 600; b++)
            {
               n.Audio()->Render(512, beats, 120.0, 4.0);
               beats += 512.0 * 120.0 / 60.0 / 48000.0;
               NoteEvent e[64];
               const int k = q->Pop(cur, e, 64);
               for (int i = 0; i < k; i++)
                  if (e[i].isNoteOn)
                     notes.push_back(e[i].note);
            }
            return notes;
         };
         const std::string text = EncodeEvents(ev);
         const std::vector<int> a = run(text), b = run(text);
         PM_CHECK(a.size() >= 20 && a == b, "same model + seed gave different notes (%zu vs %zu)", a.size(), b.size());
      }

      // 6. Learn: feed captured notes through the real path; it learns, meters, stops and builds.
      {
         PredictiveNotesNode n;
         n.CookIfNeeded(1);
         n.SetLearning(true);
         static const int loop[8] = { 60, 64, 67, 64, 62, 65, 69, 65 };
         double beat = 0.0;
         int frame = 2;
         for (int bar = 0; bar < 12 && n.IsLearning(); bar++)
         {
            for (int i = 0; i < 8; i++)
            {
               const int vid = NextVoiceId();
               n.Audio()->PushCapture({ beat, vid, (uint8_t)loop[i], 100, true });
               n.Audio()->PushCapture({ beat + 0.4, vid, (uint8_t)loop[i], 0, false });
               beat += 0.5;
            }
            n.CookIfNeeded(frame++);
         }
         n.WaitForBuild();
         PM_CHECK(!n.IsLearning(), "Learn never stopped");
         PM_CHECK(n.LearnedNotes() >= 16 && !n.Curve().empty(), "learned %d notes, %zu meter points", n.LearnedNotes(), n.Curve().size());
         PM_CHECK(!n.model.empty(), "learned model not written to the patch text");
      }

      // 7. Improved Random Note / Chorder theory.
      {
         Xs r;
         int notes[NoteTheory::kMaxScaleNotes];
         const int cnt = NoteTheory::CollectScaleNotes(0, MusicTime::kMajor, 48, 72, notes, NoteTheory::kMaxScaleNotes);
         NoteTheory::MelodyState st;
         int outKey = 0, tones = 0, big = 0, leapRev = 0, leapCont = 0, prev = -1, prevDir = 0, prevLeap = 0;
         const int N = 20000;
         for (int i = 0; i < N; i++)
         {
            const int nt = NoteTheory::PickMelodyNote(st, notes, cnt, 0, 4, 0.0f, (float)(r.Next() >> 8) / 16777216.0f);
            if (!InCMajor(nt) || nt < 48 || nt > 72)
               outKey++;
            const int pc = nt % 12;
            if (pc == 0 || pc == 4 || pc == 7)
               tones++;
            if (prev >= 0)
            {
               const int d = nt - prev;
               if (std::abs(d) > 7)
                  big++;
               if (prevLeap >= 5 && d != 0)
                  ((d > 0) == (prevDir > 0) ? leapCont : leapRev)++;
               prevDir = d > 0 ? 1 : (d < 0 ? -1 : 0);
               prevLeap = std::abs(d);
            }
            prev = nt;
         }
         PM_CHECK(outKey == 0, "%d melody notes left the scale or range", outKey);
         PM_CHECK(tones > N * 55 / 100, "chord tones only %d of %d", tones, N);
         PM_CHECK(big < N / 8, "%d leaps over a fifth", big);
         PM_CHECK(leapRev > leapCont, "no gap fill: reversed %d, continued %d", leapRev, leapCont);

         int homeAfterV = 0;
         for (int i = 0; i < 2000; i++)
            if (NoteTheory::NextChordDegree(4, 7, (float)(r.Next() >> 8) / 16777216.0f) == 0)
               homeAfterV++;
         PM_CHECK(homeAfterV > 900, "V resolved home only %d of 2000", homeAfterV);

         const int prevVoicing[3] = { 60, 64, 67 };
         int v[NoteTheory::kMaxVoicing];
         NoteTheory::VoiceChord(4, 3, 0, MusicTime::kMajor, prevVoicing, 3, v);
         const int move = std::abs(v[0] - 60) + std::abs(v[1] - 64) + std::abs(v[2] - 67);
         PM_CHECK(move <= 6 && v[0] < v[1] && v[1] < v[2], "I->V voicing moved %d semitones", move);
      }

      std::printf("[PREDMIDITEST] %s\n", gFail == 0 ? "OK" : "FAIL");
      return gFail == 0;
   }
}

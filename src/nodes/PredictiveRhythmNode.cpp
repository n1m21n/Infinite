// Ported from upstream Infinite (github.com/n1m21n/Infinite) into Infinite-Turbo.
#include "nodes/PredictiveRhythmNode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"

using NoteModel::Event;
using NoteModel::Tables;

// ------------------------------------------------------------------ audio object
class AudioPredictiveRhythmNode : public AudioNode
{
public:
   static constexpr int kRing = 4096;
   static constexpr int kMaxPending = 32;
   static constexpr int kMaxBlockEvents = 96;

   using Cap = PredictiveRhythmNode::Cap;

   ~AudioPredictiveRhythmNode() override
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

   void Render(int numFrames, double beatsStart, double bpm, double bpb)
   {
      NoteEvent in[64];
      const int nIn = (mInbox != nullptr) ? mInbox->Pop(mNoteCursor, in, 64) : 0;
      NoteEvent out[kMaxBlockEvents];
      int nOut = 0;
      const double bps = std::max(1.0, bpm) / 60.0 / mSampleRate;
      const double end = beatsStart + (double)numFrames * bps;
      const bool learning = mLearning.load(std::memory_order_relaxed);

      if (beatsStart < mLastBeatsStart - 1e-6)
      {
         mHasNext = false;
         FlushOffs(out, nOut);
      }
      mLastBeatsStart = beatsStart;

      if (learning)
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
         const float mixv = std::clamp(mMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
         if (T == nullptr || T->nEvents == 0 || mixv <= 0.0f)
         {
            FlushOffs(out, nOut);
            mHasNext = false;
         }
         else
         {
            mParams.stray = mStray.load(std::memory_order_relaxed);
            mParams.memory = mMemory.load(std::memory_order_relaxed);
            const int root = mRoot.load(std::memory_order_relaxed);
            if (mPlayer.Bound() != T)
            {
               mPlayer.Reset(T, 7u);
               mHasNext = false;
            }
            if (mHasNext && (mNext.onsetBeats > beatsStart + 64.0 || mNext.onsetBeats < beatsStart - 2.0))
               mHasNext = false;
            if (!mHasNext)
            {
               mPrevOnset = std::floor(beatsStart * NoteModel::kTicksPerBeat) / NoteModel::kTicksPerBeat;
               mPlayer.NextRhythm(*T, mParams, mPrevOnset, bpb, root, mNext);
               mHasNext = true;
            }
            int guard = 0;
            while (mNext.onsetBeats < end && guard++ < 64 && nOut < kMaxBlockEvents - 1)
            {
               Pending* slot = nullptr;
               for (Pending& p : mPending)
                  if (!p.active) { slot = &p; break; }
               if (slot != nullptr)
               {
                  NoteEvent on;
                  on.note = mNext.note;
                  on.velocity = std::clamp(mNext.velocity * mixv, 0.0f, 1.0f);
                  on.isNoteOn = true;
                  on.frameOffset = std::clamp((int)((mNext.onsetBeats - beatsStart) / bps), 0, numFrames - 1);
                  on.source = this;
                  on.voiceId = NextVoiceId();
                  out[nOut++] = on;
                  slot->active = true;
                  slot->note = mNext.note;
                  slot->voiceId = on.voiceId;
                  slot->offBeat = mNext.onsetBeats + mNext.durBeats;
               }
               mPrevOnset = mNext.onsetBeats;
               // The root can change live (the user re-picks it, or it's modulated) - re-read it each
               // onset rather than freezing the value from the top of this block.
               const int rootNow = mRoot.load(std::memory_order_relaxed);
               mPlayer.NextRhythm(*T, mParams, mPrevOnset, bpb, rootNow, mNext);
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
   void PushParams(const PredictiveRhythmNode& n, bool learning)
   {
      mStray.store(n.stray, std::memory_order_relaxed);
      mMemory.store(n.memory, std::memory_order_relaxed);
      mMix.store(n.mix, std::memory_order_relaxed);
      mRoot.store(std::clamp(n.root, 0, 127), std::memory_order_relaxed);
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
   void PushCapture(const Cap& c)
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
   int RetiredCount() const { return (int)mRetired.size(); }

private:
   struct Pending { bool active = false; int note = 0; int voiceId = 0; double offBeat = 0.0; };
   struct Retired { Tables* tables; uint64_t stamp; };

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
   std::vector<Retired> mRetired;
   std::thread::id mLastFreeThread;
   std::atomic<uint64_t> mBlocks { 0 };

   Cap mRing[kRing];
   std::atomic<size_t> mHead { 0 }, mTail { 0 };
   std::atomic<int> mDropped { 0 };

   std::atomic<float> mStray { 0.5f }, mMix { 1.0f };
   std::atomic<int> mMemory { 4 }, mRoot { 60 };
   std::atomic<bool> mLearning { false };
};

// ------------------------------------------------------------------ main-thread node
namespace
{
   // Captured note on/off pairs -> a relPitch-populated NoteModel::Event stream, pitch measured
   // against `root`. Same voiceId pairing discipline as PredictiveNotesNode::Assemble; a note still
   // held when Learn stops lasts until the last beat.
   std::vector<Event> Assemble(const std::vector<PredictiveRhythmNode::Cap>& caps, double bpb, int root)
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
         e.relPitch = (int8_t)std::clamp((int)recs[i].note - root, -NoteModel::kRelPitchRange, NoteModel::kRelPitchRange);
         e.ioiTicks = (uint16_t)std::clamp(tick - prevTick, 0, NoteModel::kMaxIoiTicks);
         const double bar = std::fmod((double)tick / NoteModel::kTicksPerBeat, std::max(1.0, bpb));
         e.metric = (uint8_t)NoteModel::MetricOf(bar < 0.0 ? bar + bpb : bar);
         e.durTicks = (uint16_t)std::clamp(NoteModel::SnapTicks(recs[i].dur), 1, NoteModel::kMaxIoiTicks);
         ev.push_back(e);
         prevTick = tick;
      }
      return ev;
   }

   // The root a rhythm is "about": the most frequent captured note-on pitch, ties broken toward the
   // lowest. Called once, when Learn finishes - the user can still override it by hand afterward.
   int DetectRoot(const std::vector<PredictiveRhythmNode::Cap>& caps)
   {
      std::array<int, 128> counts {};
      for (const auto& c : caps)
         if (c.on)
            counts[c.note]++;
      int best = -1, bestCount = 0;
      for (int note = 0; note < 128; note++)
         if (counts[note] > bestCount)
         {
            bestCount = counts[note];
            best = note;
         }
      return best >= 0 ? best : 60;
   }

   // Cross-session "house rhythm style" pool - every Predictive Rhythm instance that finishes a
   // Learn contributes its captured events here, and every instance's Confidence01() reads a
   // small weight from it (same shape as PredictiveNotesNode's GlobalEngine). A rolling window of
   // raw training events, most-recent kept.
   class GlobalEngine
   {
   public:
      static GlobalEngine& Instance()
      {
         static GlobalEngine e;
         return e;
      }

      void AddEvents(const std::vector<Event>& events)
      {
         if (events.empty())
            return;
         mEvents.insert(mEvents.end(), events.begin(), events.end());
         if (mEvents.size() > kMaxSharedEvents)
            mEvents.erase(mEvents.begin(), mEvents.begin() + (long)(mEvents.size() - kMaxSharedEvents));
         mVersion++;
      }

      const std::vector<Event>& Events() const { return mEvents; }
      bool HasLearnedData() const { return mEvents.size() >= 2; }
      uint64_t Version() const { return mVersion; }

      // Held-out cross-entropy over the shared pool, cached and only recomputed when the pool
      // actually changes - every Predictive Rhythm instance in the patch calls this every frame.
      float Confidence01() const
      {
         if (mConfVersion != mVersion)
         {
            mConfVersion = mVersion;
            float ceModel = 0.0f, ceBase = 0.0f;
            if (mEvents.size() >= 2 && NoteModel::HeldOutCrossEntropy(mEvents, NoteModel::kMaxOrder, ceModel, ceBase))
            {
               const float gain = ceBase - ceModel;
               mConfCache = (gain > 0.0f) ? std::clamp(1.0f - std::exp2(-gain), 0.0f, 1.0f) : 0.0f;
            }
            else
            {
               mConfCache = 0.0f;
            }
         }
         return mConfCache;
      }

      bool Load(const std::string& directory)
      {
         std::ifstream f(Path(directory), std::ios::binary | std::ios::ate);
         if (!f.is_open())
            return false;
         const size_t sz = (size_t)f.tellg();
         f.seekg(0, std::ios::beg);
         std::string text(sz, '\0');
         f.read(text.data(), (std::streamsize)sz);
         std::vector<Event> loaded;
         if (!NoteModel::DecodeEvents(text, loaded))
            return false;
         mEvents = std::move(loaded);
         mVersion++;
         return true;
      }

      bool Save(const std::string& directory) const
      {
         std::error_code ec;
         std::filesystem::create_directories(directory, ec);
         std::ofstream f(Path(directory), std::ios::binary | std::ios::trunc);
         if (!f.is_open())
            return false;
         const std::string text = NoteModel::EncodeEvents(mEvents);
         f.write(text.data(), (std::streamsize)text.size());
         return true;
      }

   private:
      static std::string Path(const std::string& dir)
      {
         std::filesystem::path p(dir);
         return (p / "rhythm_style.bin").string();
      }

      static constexpr size_t kMaxSharedEvents = 4000;
      std::vector<Event> mEvents;
      uint64_t mVersion = 0;
      mutable uint64_t mConfVersion = ~0ull;
      mutable float mConfCache = 0.0f;
   };
}

PredictiveRhythmNode::PredictiveRhythmNode() = default;

PredictiveRhythmNode::~PredictiveRhythmNode()
{
   if (mBuild.valid())
      delete mBuild.get();
}

AudioNode* PredictiveRhythmNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPredictiveRhythmNode>();
   return mAudioNode.get();
}

void PredictiveRhythmNode::VisitParams(ParamVisitor& v)
{
   v.Float("stray", stray);
   v.Int("memory", memory);
   v.Float("mix", mix);
   v.Int("root", root);
   v.Text("model", model);
}

void PredictiveRhythmNode::SetLearning(bool on)
{
   if (on == mLearning)
      return;
   if (!on)
   {
      FinishLearn();
      return;
   }
   GetAudioNode();
   AudioPredictiveRhythmNode::Cap stale;
   while (mAudioNode->PopCapture(stale)) { }
   mCaps.clear();
   mNotesCaptured = 0;
   mMeterBars = 0;
   mCurve.clear();
   mLastLearnTooShort = false;
   mBeatsPerBar = Transport::Instance().BeatsPerBar();
   mLearning = true;
}

void PredictiveRhythmNode::ResetLearnedState()
{
   if (mLearning)
      mLearning = false; // no FinishLearn(): this is a discard, not a capture to keep
   if (mAudioNode)
      mAudioNode->Retire(mAudioNode->SwapTables(nullptr));
   mCaps.clear();
   mCurve.clear();
   mNotesCaptured = 0;
   mMeterBars = 0;
   mLearned = 0;
   mLastLearnTooShort = false;
   model.clear();
   mAppliedModel.clear();
}

void PredictiveRhythmNode::DrainCaptures()
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

void PredictiveRhythmNode::UpdateMeter(bool force)
{
   if (mCaps.empty())
      return;
   const double first = mCaps.front().beat, last = mCaps.back().beat;
   const int bars = (int)(std::floor(last / mBeatsPerBar) - std::floor(first / mBeatsPerBar));
   if (!force && bars <= mMeterBars)
      return;
   mMeterBars = bars;
   // Root isn't re-detected here - only FinishLearn overwrites the saved `root` param - but the
   // meter still needs *a* root to fold pitch against, so it uses the same detector locally without
   // mutating the field the user might have already hand-picked mid-take.
   const std::vector<Event> ev = Assemble(mCaps, mBeatsPerBar, DetectRoot(mCaps));
   float ceModel = 0.0f, ceBase = 0.0f;
   if (NoteModel::HeldOutCrossEntropy(ev, std::clamp(memory, 0, NoteModel::kMaxOrder), ceModel, ceBase))
      mCurve.push_back(ceBase - ceModel);
}

float PredictiveRhythmNode::Confidence01() const
{
   if (mCurve.empty())
      return 0.0f;
   const float gain = mCurve.back();
   float localConf = 0.0f;
   if (gain > 0.0f)
      localConf = std::clamp(1.0f - std::exp2(-gain), 0.0f, 1.0f);

   // Blend in the cross-session "house rhythm style" pool other Predictive Rhythm instances
   // have contributed, minority-weighted (80% local / 20% shared, same split as Predictive
   // Coloring and Predictive Notes - step-10 review).
   constexpr float kLocalWeight = 0.8f;
   constexpr float kSharedWeight = 0.2f;
   const float sharedConf = GlobalEngine::Instance().Confidence01();
   return std::clamp(kLocalWeight * localConf + kSharedWeight * sharedConf, 0.0f, 1.0f);
}

int PredictiveRhythmNode::Dropped() const
{
   return mAudioNode ? mAudioNode->Dropped() : 0;
}

void PredictiveRhythmNode::FinishLearn()
{
   if (!mLearning)
      return;
   DrainCaptures();
   mLearning = false;
   mAudioNode->PushParams(*this, false);
   mLastLearnTooShort = mNotesCaptured < 2;
   if (mNotesCaptured >= 2)
   {
      root = DetectRoot(mCaps); // "this detects roots" - overwrite; the user can still redial it
      const std::vector<Event> ev = Assemble(mCaps, mBeatsPerBar, root);
      mLastLearnTooShort = ev.size() < 2;
      if (ev.size() >= 2)
      {
         model = NoteModel::EncodeEvents(ev);
         mAppliedModel = model;
         mLearned = (int)ev.size();
         StartBuild(ev);
         // Contribute this take's captured events to the cross-session house rhythm pool.
         GlobalEngine::Instance().AddEvents(ev);
      }
   }
   mCaps.clear();
}

void PredictiveRhythmNode::StartBuild(const std::vector<Event>& events)
{
   if (mBuild.valid())
   {
      mQueued = true;
      mQueuedEvents = events;
      return;
   }
   mBuild = std::async(std::launch::async, [events]() { return NoteModel::Build(events); });
}

void PredictiveRhythmNode::SwapIn(Tables* t)
{
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapTables(t));
}

void PredictiveRhythmNode::PollBuild()
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

void PredictiveRhythmNode::WaitForBuild()
{
   GetAudioNode();
   while (mBuild.valid())
   {
      mBuild.wait();
      PollBuild();
   }
}

void PredictiveRhythmNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();

   if (model != mAppliedModel)
   {
      mAppliedModel = model;
      std::vector<Event> ev;
      if (NoteModel::DecodeEvents(model, ev) && ev.size() >= 2)
      {
         mLearned = (int)ev.size();
         // Re-measure the model that just came off disk, same reasoning as PredictiveNotesNode:
         // Confidence01 reads mCurve, which is runtime-only and would otherwise read 0% forever.
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
#define PR_CHECK(cond, ...)                                         \
   do                                                                \
   {                                                                 \
      if (!(cond))                                                   \
      {                                                              \
         gFail++;                                                    \
         std::printf("[PREDRHYTHMTEST] FAIL: " __VA_ARGS__);         \
         std::printf("\n");                                          \
      }                                                              \
   } while (0)

   // A steady quarter-note pattern that mostly sits on relPitch 0, with an occasional +2 deviation -
   // e.g. root root root +2 root root root, matching the "C2 C2 C2 D3 C2 C2 C2" shape from the design
   // conversation.
   std::vector<Event> RootMostlyLoop(int notes)
   {
      std::vector<Event> ev;
      for (int i = 0; i < notes; i++)
      {
         Event e;
         e.relPitch = (i % 4 == 3) ? 2 : 0;
         e.ioiTicks = (uint16_t)(i == 0 ? 0 : NoteModel::kTicksPerBeat);
         e.durTicks = (uint16_t)(NoteModel::kTicksPerBeat / 2);
         e.vel = 100;
         e.metric = (uint8_t)((i * 4) % 16);
         ev.push_back(e);
      }
      return ev;
   }
}

void PredictiveRhythmNode::SweepPrepare()
{
   model = NoteModel::EncodeEvents(RootMostlyLoop(16));
   root = 60;
   CookIfNeeded(0);
   WaitForBuild();
}

namespace PredictiveRhythmStyle
{
   bool Load(const std::string& directory) { return GlobalEngine::Instance().Load(directory); }
   bool Save(const std::string& directory) { return GlobalEngine::Instance().Save(directory); }
   bool HasLearnedData() { return GlobalEngine::Instance().HasLearnedData(); }
}

namespace PredictiveRhythm
{
   bool RunPredRhythmTest()
   {
      gFail = 0;
      using namespace NoteModel;

      // 1. Learn a mostly-one-note pattern (60,60,60,62 repeating) through a single note cable, root
      //    auto-detects to the most common captured note (60), and replaying against that root
      //    reproduces the same mostly-repeated-note shape.
      {
         PredictiveRhythmNode n;
         n.GetAudioNode();
         NoteEventQueue inQ;
         const int icur = inQ.RegisterConsumer();
         n.Audio()->PrepareToPlay(48000.0, 512);
         n.Audio()->SetNoteInbox(&inQ, icur);
         n.SetLearning(true);

         double beats = 0.0;
         const double bpm = 120.0, bpb = 4.0, bps = bpm / 60.0 / 48000.0;
         for (int i = 0; i < 16; i++)
         {
            const int note = (i % 4 == 3) ? 62 : 60;
            NoteEvent on;
            on.note = note; on.velocity = 0.8f; on.isNoteOn = true; on.frameOffset = 0; on.voiceId = NextVoiceId();
            inQ.Push(on);
            n.Audio()->Render(512, beats, bpm, bpb);
            beats += 512.0 * bps;
            NoteEvent off = on;
            off.isNoteOn = false;
            inQ.Push(off);
            n.Audio()->Render(512, beats, bpm, bpb);
            beats += 512.0 * bps;
            n.CookIfNeeded(i + 1);
         }
         n.SetLearning(false);
         n.WaitForBuild();
         PR_CHECK(n.LearnedNotes() >= 8, "too few notes learned (%d)", n.LearnedNotes());
         PR_CHECK(n.root == 60, "root did not auto-detect to the most common note (got %d)", n.root);

         n.Audio()->PrepareToPlay(48000.0, 512);
         NoteEventQueue* outQ = n.Audio()->NoteOutbox();
         outQ->ResetConsumers();
         const int ocur = outQ->RegisterConsumer();
         n.stray = 0.0f;
         n.mix = 1.0f;
         std::vector<int> notes;
         beats = 0.0;
         for (int b = 0; b < 400; b++)
         {
            n.CookIfNeeded(100 + b);
            n.Audio()->Render(512, beats, bpm, bpb);
            beats += 512.0 * bps;
            NoteEvent e[64];
            const int k = outQ->Pop(ocur, e, 64);
            for (int i = 0; i < k; i++)
               if (e[i].isNoteOn)
                  notes.push_back(e[i].note);
         }
         PR_CHECK(notes.size() >= 4, "only %zu notes played back", notes.size());
         int offRoot = 0;
         for (int nt : notes)
            if (nt != 60 && nt != 62)
               offRoot++;
         PR_CHECK(offRoot == 0, "%d of %zu notes were neither the root nor the learned deviation", offRoot, notes.size());

         // 2. Changing root after learning transposes playback without relearning.
         n.root = 48; // down a fifth from 60/62's tonal centre
         std::vector<int> notes2;
         n.Audio()->PrepareToPlay(48000.0, 512);
         outQ->ResetConsumers();
         const int ocur2 = outQ->RegisterConsumer();
         beats = 0.0;
         for (int b = 0; b < 400; b++)
         {
            n.CookIfNeeded(600 + b);
            n.Audio()->Render(512, beats, bpm, bpb);
            beats += 512.0 * bps;
            NoteEvent e[64];
            const int k = outQ->Pop(ocur2, e, 64);
            for (int i = 0; i < k; i++)
               if (e[i].isNoteOn)
                  notes2.push_back(e[i].note);
         }
         PR_CHECK(notes2.size() >= 4, "only %zu notes played back after changing root", notes2.size());
         int offRoot2 = 0;
         for (int nt : notes2)
            if (nt != 48 && nt != 50)
               offRoot2++;
         PR_CHECK(offRoot2 == 0, "%d of %zu notes did not transpose with the new root", offRoot2, notes2.size());
      }

      // 3. mix = 0 produces no output.
      {
         PredictiveRhythmNode n;
         n.model = EncodeEvents(RootMostlyLoop(16));
         n.root = 60;
         n.stray = 0.0f;
         n.mix = 0.0f;
         n.CookIfNeeded(1);
         n.WaitForBuild();
         AudioPredictiveRhythmNode* a = n.Audio();
         a->PrepareToPlay(48000.0, 512);
         NoteEventQueue* q = a->NoteOutbox();
         q->ResetConsumers();
         const int cur = q->RegisterConsumer();
         double beats = 0.0;
         for (int b = 0; b < 100; b++)
         {
            a->Render(512, beats, 120.0, 4.0);
            beats += 512.0 * 120.0 / 60.0 / 48000.0;
         }
         NoteEvent e[64];
         const int k = q->Pop(cur, e, 64);
         PR_CHECK(k == 0, "mix=0 still produced %d events", k);
      }

      // 4. Save/load round trip preserves relPitch.
      {
         const std::vector<Event> ev = RootMostlyLoop(10);
         const std::string enc = EncodeEvents(ev);
         std::vector<Event> dec;
         PR_CHECK(DecodeEvents(enc, dec), "decode failed");
         PR_CHECK(dec.size() == ev.size(), "decoded %zu events, expected %zu", dec.size(), ev.size());
         bool relOk = true;
         for (size_t i = 0; i < dec.size() && i < ev.size(); i++)
            if (dec[i].relPitch != ev[i].relPitch)
               relOk = false;
         PR_CHECK(relOk, "relPitch did not round-trip through encode/decode");
      }

      // 5. Single-NoteCable wiring safety: spawn, wire, save/load, delete mid-playback.
      {
         PredictiveRhythmNode src;
         auto* n = new PredictiveRhythmNode();
         n->noteInput.Connect(&src, 0);
         PR_CHECK(n->NoteInputSlot(0) == &n->noteInput, "the note-cable slot is not addressable");
         PR_CHECK(std::string(n->InputLabel(0)) == "notes", "input pin label changed unexpectedly");
         n->model = EncodeEvents(RootMostlyLoop(8));
         n->root = 60;
         n->CookIfNeeded(1);
         n->GetAudioNode()->PrepareToPlay(48000.0, 512);
         n->Audio()->Render(512, 0.0, 120.0, 4.0);
         n->model = EncodeEvents(RootMostlyLoop(12));
         n->CookIfNeeded(2);
         n->noteInput.Disconnect();
         PR_CHECK(!n->noteInput.IsConnected(), "the cable stayed bound after Disconnect");
         delete n; // a build may be in flight here
      }

      std::printf("[PREDRHYTHMTEST] %s\n", gFail == 0 ? "OK" : "FAIL");
      return gFail == 0;
   }
}

#include "nodes/PredictiveBasslineNode.h"

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
#include "core/Transport.h"

using NoteModel::Event;
using NoteModel::Tables;

namespace
{
   // Small fixed-capacity voiceId -> held-note map for root tracking. Same shape as
   // AudioQuantizerNode::mOutNote (NoteNodes.cpp:1465, VoiceIdMap<int,128>) - that one exists
   // locally to NoteNodes.cpp too, so this is a second, independent copy of the same idiom rather
   // than sharing code across translation units.
   template <int Capacity>
   class HeldNoteMap
   {
   public:
      void Set(int voiceId, int note)
      {
         if (int* v = Find(voiceId)) { *v = note; return; }
         for (Entry& e : mEntries)
            if (!e.used) { e.used = true; e.voiceId = voiceId; e.note = note; return; }
         mEntries[0] = Entry { true, voiceId, note };
      }
      int* Find(int voiceId)
      {
         for (Entry& e : mEntries)
            if (e.used && e.voiceId == voiceId)
               return &e.note;
         return nullptr;
      }
      void Erase(int voiceId)
      {
         for (Entry& e : mEntries)
            if (e.used && e.voiceId == voiceId) { e.used = false; return; }
      }
      void Clear() { for (Entry& e : mEntries) e.used = false; }
      // Lowest currently-held note (the node's root convention, design doc §2.2.1). False if nothing
      // is held.
      bool Lowest(int& out) const
      {
         bool any = false;
         int lo = 128;
         for (const Entry& e : mEntries)
            if (e.used) { any = true; lo = std::min(lo, e.note); }
         if (any)
            out = lo;
         return any;
      }

   private:
      struct Entry { bool used = false; int voiceId = 0; int note = 0; };
      Entry mEntries[Capacity];
   };
}

// ------------------------------------------------------------------ audio object
class AudioPredictiveBasslineNode : public AudioNode
{
public:
   static constexpr int kRing = 4096;
   static constexpr int kMaxPending = 32;
   static constexpr int kMaxBlockEvents = 96;
   static constexpr int kMaxHeldRoots = 32;
   static constexpr int kFallbackRoot = 36; // C2: used only if harmonyInput has never held a note

   using Cap = PredictiveBasslineNode::Cap;

   ~AudioPredictiveBasslineNode() override
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
      mHeld.Clear();
      mHaveRoot = false;
      mLiveRoot = kFallbackRoot;
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
      NoteEvent harmIn[64];
      const int nHarm = (mHarmonyInbox != nullptr) ? mHarmonyInbox->Pop(mHarmonyCursor, harmIn, 64) : 0;
      NoteEvent learnIn[64];
      const int nLearn = (mLearnInbox != nullptr) ? mLearnInbox->Pop(mLearnCursor, learnIn, 64) : 0;

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

      // Root tracking runs every block regardless of Learn state: play needs the live root, and
      // Learn needs to snapshot it per captured note (design doc §2.2.3).
      for (int i = 0; i < nHarm; i++)
      {
         const NoteEvent& e = harmIn[i];
         if (e.bendUpdate || e.note < 0 || e.note > 127)
            continue;
         if (e.isNoteOn)
            mHeld.Set(e.voiceId, e.note);
         else
            mHeld.Erase(e.voiceId);
      }
      int lowest = 0;
      if (mHeld.Lowest(lowest))
      {
         mLiveRoot = lowest;
         mHaveRoot = true;
      }
      // else: harmonyInput disconnected or silent right now - hold the last known root rather than
      // falling back to silence or a dangling read (design doc §7 test 4).

      if (learning)
      {
         for (int i = 0; i < nLearn; i++)
         {
            const NoteEvent& e = learnIn[i];
            if (e.bendUpdate || e.note < 0 || e.note > 127)
               continue;
            Cap c;
            c.beat = beatsStart + (double)e.frameOffset * bps;
            c.voiceId = e.voiceId;
            c.note = (uint8_t)e.note;
            c.vel = (uint8_t)std::clamp((int)std::lround(e.velocity * 127.0f), 0, 127);
            c.rootAtCapture = mHaveRoot ? (int8_t)std::clamp(mLiveRoot, 0, 127) : (int8_t)-1;
            c.on = e.isNoteOn;
            PushCapture(c);
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
            const int root = mHaveRoot ? mLiveRoot : kFallbackRoot;
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
               mPlayer.NextBassline(*T, mParams, mPrevOnset, bpb, root, mNext);
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
               // Bass onset timing is entirely its own (design doc §2.3) - the root sampled here is
               // whatever is live right now, but the next onset time comes only from the learned
               // rhythm, never from a harmony change.
               mPrevOnset = mNext.onsetBeats;
               const int rootNow = mHaveRoot ? mLiveRoot : kFallbackRoot;
               mPlayer.NextBassline(*T, mParams, mPrevOnset, bpb, rootNow, mNext);
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
   void SetNoteInbox(int inputSlot, NoteEventQueue* inbox, int cursor) override
   {
      if (inputSlot == 0) { mHarmonyInbox = inbox; mHarmonyCursor = cursor; }
      else if (inputSlot == 1) { mLearnInbox = inbox; mLearnCursor = cursor; }
   }

   // ---- main thread ----
   void PushParams(const PredictiveBasslineNode& n, bool learning)
   {
      mStray.store(n.stray, std::memory_order_relaxed);
      mMemory.store(n.memory, std::memory_order_relaxed);
      mMix.store(n.mix, std::memory_order_relaxed);
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
   int LiveRoot() const { return mHaveRoot ? mLiveRoot : -1; }

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
   NoteEventQueue* mHarmonyInbox = nullptr;
   int mHarmonyCursor = -1;
   NoteEventQueue* mLearnInbox = nullptr;
   int mLearnCursor = -1;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;

   HeldNoteMap<kMaxHeldRoots> mHeld;
   bool mHaveRoot = false;
   int mLiveRoot = kFallbackRoot;

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
   std::atomic<int> mMemory { 4 };
   std::atomic<bool> mLearning { false };
};

// ------------------------------------------------------------------ main-thread node
namespace
{
   // Captured note on/off pairs on learnInput, plus the root snapshotted at each note-on, -> a
   // relPitch-populated NoteModel::Event stream. Same voiceId pairing discipline as
   // PredictiveNotesNode::Assemble; a note still held when Learn stops lasts until the last beat.
   std::vector<Event> Assemble(const std::vector<PredictiveBasslineNode::Cap>& caps, double bpb)
   {
      struct Rec { double onset, dur; uint8_t note, vel; int8_t root; };
      std::vector<Rec> recs;
      struct Open { int voice; size_t idx; };
      std::vector<Open> open;
      double lastBeat = 0.0;
      for (const auto& c : caps)
      {
         lastBeat = std::max(lastBeat, c.beat);
         if (c.on)
         {
            recs.push_back({ c.beat, -1.0, c.note, c.vel, c.rootAtCapture });
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
         // No root ever seen (harmony never wired during Learn): treat the played note as its own
         // root, i.e. relPitch 0 - degrades to "the bass just plays what it played" rather than
         // producing a meaningless huge interval against a fabricated root.
         const int root = recs[i].root >= 0 ? recs[i].root : (int)recs[i].note;
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
}

PredictiveBasslineNode::PredictiveBasslineNode() = default;

PredictiveBasslineNode::~PredictiveBasslineNode()
{
   if (mBuild.valid())
      delete mBuild.get();
}

int PredictiveBasslineNode::LiveRoot() const { return mAudioNode ? mAudioNode->LiveRoot() : -1; }

AudioNode* PredictiveBasslineNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPredictiveBasslineNode>();
   return mAudioNode.get();
}

void PredictiveBasslineNode::VisitParams(ParamVisitor& v)
{
   v.Float("stray", stray);
   v.Int("memory", memory);
   v.Float("mix", mix);
   v.Text("model", model);
}

void PredictiveBasslineNode::SetLearning(bool on)
{
   if (on == mLearning)
      return;
   if (!on)
   {
      FinishLearn();
      return;
   }
   GetAudioNode();
   AudioPredictiveBasslineNode::Cap stale;
   while (mAudioNode->PopCapture(stale)) { }
   mCaps.clear();
   mNotesCaptured = 0;
   mLastLearnTooShort = false;
   mBeatsPerBar = Transport::Instance().BeatsPerBar();
   mLearning = true;
}

void PredictiveBasslineNode::DrainCaptures()
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

void PredictiveBasslineNode::FinishLearn()
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
}

void PredictiveBasslineNode::StartBuild(const std::vector<Event>& events)
{
   if (mBuild.valid())
   {
      mQueued = true;
      mQueuedEvents = events;
      return;
   }
   mBuild = std::async(std::launch::async, [events]() { return NoteModel::Build(events); });
}

void PredictiveBasslineNode::SwapIn(Tables* t)
{
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapTables(t));
}

void PredictiveBasslineNode::PollBuild()
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

void PredictiveBasslineNode::WaitForBuild()
{
   GetAudioNode();
   while (mBuild.valid())
   {
      mBuild.wait();
      PollBuild();
   }
}

void PredictiveBasslineNode::CookIfNeeded(int frameId)
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
         StartBuild(ev);
      }
      else
      {
         mLearned = 0;
         SwapIn(nullptr);
      }
   }
   PollBuild();
   mAudioNode->PushParams(*this, mLearning);
   mAudioNode->CollectRetired();
   if (mLearning)
      DrainCaptures();
}

// ------------------------------------------------------------------ tests
namespace
{
   int gFail = 0;
#define PB_CHECK(cond, ...)                                          \
   do                                                                \
   {                                                                 \
      if (!(cond))                                                   \
      {                                                              \
         gFail++;                                                    \
         std::printf("[PREDBASSLINETEST] FAIL: " __VA_ARGS__);       \
         std::printf("\n");                                          \
      }                                                              \
   } while (0)

   // A steady quarter-note, root-only bass part: relPitch 0 throughout.
   std::vector<Event> RootOnlyLoop(int notes)
   {
      std::vector<Event> ev;
      for (int i = 0; i < notes; i++)
      {
         Event e;
         e.relPitch = 0;
         e.ioiTicks = (uint16_t)(i == 0 ? 0 : NoteModel::kTicksPerBeat);
         e.durTicks = (uint16_t)(NoteModel::kTicksPerBeat / 2);
         e.vel = 100;
         e.metric = (uint8_t)((i * 4) % 16);
         ev.push_back(e);
      }
      return ev;
   }
}

void PredictiveBasslineNode::SweepPrepare()
{
   model = NoteModel::EncodeEvents(RootOnlyLoop(16));
   CookIfNeeded(0);
   WaitForBuild();
}

namespace PredictiveBassline
{
   bool RunPredBasslineTest()
   {
      gFail = 0;
      using namespace NoteModel;

      auto renderFor = [](AudioPredictiveBasslineNode* a, int blocks, int frames, double bpm, double bpb, std::vector<int>& notes)
      {
         double beats = 0.0;
         const double bps = bpm / 60.0 / 48000.0;
         for (int b = 0; b < blocks; b++)
         {
            a->Render(frames, beats, bpm, bpb);
            beats += (double)frames * bps;
         }
         (void)notes;
      };
      (void)renderFor;

      // 1. Learn a root-only quarter-note part against a fixed C-major-triad harmony (root 48);
      //    play back against the same harmony - output pitch matches the root, rhythm matches the
      //    learned quarter-note pulse.
      {
         PredictiveBasslineNode n;
         n.model = EncodeEvents(RootOnlyLoop(16));
         n.stray = 0.0f; // replay: exact
         n.mix = 1.0f;
         n.CookIfNeeded(1);
         n.WaitForBuild();
         AudioPredictiveBasslineNode* a = n.Audio();
         a->PrepareToPlay(48000.0, 512);
         NoteEventQueue* q = a->NoteOutbox();
         q->ResetConsumers();
         const int cur = q->RegisterConsumer();

         // Hold a C-major triad (48, 52, 55) on the harmony input from block 0.
         a->SetNoteInbox(0, nullptr, -1); // no upstream queue in this direct test; feed via a manual inbox
         NoteEventQueue harmQ;
         const int hcur = harmQ.RegisterConsumer();
         a->SetNoteInbox(0, &harmQ, hcur);
         NoteEvent on;
         on.note = 48; on.velocity = 0.8f; on.isNoteOn = true; on.frameOffset = 0; on.voiceId = NextVoiceId();
         harmQ.Push(on);
         on.note = 52; on.voiceId = NextVoiceId(); harmQ.Push(on);
         on.note = 55; on.voiceId = NextVoiceId(); harmQ.Push(on);

         std::vector<int> notes;
         double beats = 0.0;
         const double bpm = 120.0, bpb = 4.0;
         const double bps = bpm / 60.0 / 48000.0;
         for (int b = 0; b < 400; b++)
         {
            a->Render(512, beats, bpm, bpb);
            beats += 512.0 * bps;
            NoteEvent e[64];
            const int k = q->Pop(cur, e, 64);
            for (int i = 0; i < k; i++)
               if (e[i].isNoteOn)
                  notes.push_back(e[i].note);
         }
         PB_CHECK(notes.size() >= 4, "only %zu notes played against a held C-major triad", notes.size());
         int bad = 0;
         for (int nt : notes)
            if (nt != 48)
               bad++;
         PB_CHECK(bad == 0, "%d of %zu notes were not the tracked root 48", bad, notes.size());

         // 2. Same tables, transposed harmony (root now 55) never seen during Learn - output should
         //    track the new root, proving relative-pitch resolution, not memorized absolute notes.
         a->PrepareToPlay(48000.0, 512);
         q->ResetConsumers();
         const int cur2 = q->RegisterConsumer();
         NoteEventQueue harmQ2;
         const int hcur2 = harmQ2.RegisterConsumer();
         a->SetNoteInbox(0, &harmQ2, hcur2);
         NoteEvent on2;
         on2.note = 55; on2.velocity = 0.8f; on2.isNoteOn = true; on2.frameOffset = 0; on2.voiceId = NextVoiceId();
         harmQ2.Push(on2);
         on2.note = 59; on2.voiceId = NextVoiceId(); harmQ2.Push(on2);
         on2.note = 62; on2.voiceId = NextVoiceId(); harmQ2.Push(on2);

         std::vector<int> notes2;
         beats = 0.0;
         for (int b = 0; b < 400; b++)
         {
            a->Render(512, beats, bpm, bpb);
            beats += 512.0 * bps;
            NoteEvent e[64];
            const int k = q->Pop(cur2, e, 64);
            for (int i = 0; i < k; i++)
               if (e[i].isNoteOn)
                  notes2.push_back(e[i].note);
         }
         PB_CHECK(notes2.size() >= 4, "only %zu notes played against a transposed triad", notes2.size());
         int bad2 = 0;
         for (int nt : notes2)
            if (nt != 55)
               bad2++;
         PB_CHECK(bad2 == 0, "%d of %zu notes did not track the transposed root 55", bad2, notes2.size());
      }

      // 3. Chord change mid-note: bass does not re-trigger until its own next learned onset.
      {
         PredictiveBasslineNode n;
         n.model = EncodeEvents(RootOnlyLoop(16));
         n.stray = 0.0f;
         n.mix = 1.0f;
         n.CookIfNeeded(1);
         n.WaitForBuild();
         AudioPredictiveBasslineNode* a = n.Audio();
         a->PrepareToPlay(48000.0, 512);
         NoteEventQueue* q = a->NoteOutbox();
         q->ResetConsumers();
         const int cur = q->RegisterConsumer();
         NoteEventQueue harmQ;
         const int hcur = harmQ.RegisterConsumer();
         a->SetNoteInbox(0, &harmQ, hcur);
         NoteEvent hon;
         hon.note = 40; hon.velocity = 0.8f; hon.isNoteOn = true; hon.frameOffset = 0; hon.voiceId = NextVoiceId();
         harmQ.Push(hon);

         int onCount = 0, offCount = 0;
         double beats = 0.0;
         const double bpm = 120.0, bps = bpm / 60.0 / 48000.0;
         a->Render(512, beats, bpm, 4.0);
         beats += 512.0 * bps;
         {
            NoteEvent e[64];
            const int k = q->Pop(cur, e, 64);
            for (int i = 0; i < k; i++) (e[i].isNoteOn ? onCount : offCount)++;
         }
         // Change the chord mid-block-stream, without any note-off on the previous root - a plain
         // extra note-on lowers/raises the tracked lowest note immediately.
         NoteEvent hon2;
         hon2.note = 45; hon2.velocity = 0.8f; hon2.isNoteOn = true; hon2.frameOffset = 0; hon2.voiceId = NextVoiceId();
         harmQ.Push(hon2);
         const int onsBefore = onCount;
         a->Render(64, beats, bpm, 4.0); // a tiny block, far shorter than the learned IOI
         beats += 64.0 * bps;
         {
            NoteEvent e[64];
            const int k = q->Pop(cur, e, 64);
            for (int i = 0; i < k; i++) (e[i].isNoteOn ? onCount : offCount)++;
         }
         PB_CHECK(onCount == onsBefore, "a harmony change re-triggered the bass instead of waiting for its own next onset (%d -> %d)", onsBefore, onCount);
      }

      // 4. harmonyInput disconnected mid-playback: no crash, holds the last known root.
      {
         PredictiveBasslineNode n;
         n.model = EncodeEvents(RootOnlyLoop(16));
         n.stray = 0.0f;
         n.mix = 1.0f;
         n.CookIfNeeded(1);
         n.WaitForBuild();
         AudioPredictiveBasslineNode* a = n.Audio();
         a->PrepareToPlay(48000.0, 512);
         NoteEventQueue harmQ;
         const int hcur = harmQ.RegisterConsumer();
         a->SetNoteInbox(0, &harmQ, hcur);
         NoteEvent hon;
         hon.note = 43; hon.velocity = 0.8f; hon.isNoteOn = true; hon.frameOffset = 0; hon.voiceId = NextVoiceId();
         harmQ.Push(hon);
         a->Render(512, 0.0, 120.0, 4.0);
         PB_CHECK(a->LiveRoot() == 43, "root not tracked before disconnect (got %d)", a->LiveRoot());
         a->SetNoteInbox(0, nullptr, -1); // disconnect
         a->Render(512, 512.0 * 120.0 / 60.0 / 48000.0, 120.0, 4.0);
         PB_CHECK(a->LiveRoot() == 43, "root did not hold its last known value after disconnect (got %d)", a->LiveRoot());
      }

      // 5. Dual-NoteCable wiring safety: spawn, wire both slots, save/load, delete mid-playback.
      {
         PredictiveBasslineNode src1, src2;
         auto* n = new PredictiveBasslineNode();
         n->harmonyInput.Connect(&src1, 0);
         n->learnInput.Connect(&src2, 0);
         PB_CHECK(n->NoteInputSlot(0) == &n->harmonyInput && n->NoteInputSlot(1) == &n->learnInput,
                  "the two note-cable slots are not distinct/addressable");
         PB_CHECK(std::string(n->InputLabel(0)) == "harmony" && std::string(n->InputLabel(1)) == "learn from",
                  "input pin labels do not distinguish the two slots");
         n->model = EncodeEvents(RootOnlyLoop(8));
         n->CookIfNeeded(1);
         n->GetAudioNode()->PrepareToPlay(48000.0, 512);
         n->Audio()->Render(512, 0.0, 120.0, 4.0);
         n->model = EncodeEvents(RootOnlyLoop(12));
         n->CookIfNeeded(2);
         n->harmonyInput.Disconnect();
         n->learnInput.Disconnect();
         PB_CHECK(!n->harmonyInput.IsConnected() && !n->learnInput.IsConnected(), "a cable stayed bound after Disconnect");
         delete n; // a build may be in flight here
      }

      std::printf("[PREDBASSLINETEST] %s\n", gFail == 0 ? "OK" : "FAIL");
      return gFail == 0;
   }
}

#include "nodes/PredictiveQuantizeNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEventQueue.h"
#include "audio/NoteModel.h"
#include "core/Transport.h"

namespace
{
   constexpr int kMaxModes = 4;
   constexpr int kHistBins = NoteModel::kMaxIoiTicks + 1; // ticks 0..kMaxIoiTicks inclusive

   // The learned spacing template: up to kMaxModes inter-onset tick values and each one's share of
   // the captured mass. Crosses to the audio thread the same way PredictiveNotesNode::Tables does -
   // atomic pointer swap, freed on the main thread once retired (see AudioPredictiveQuantizeNode).
   struct ModeSet
   {
      int count = 0;
      float tick[kMaxModes] = {};
      float weight[kMaxModes] = {};
   };

   // Finds up to kMaxModes local-maxima peaks in a 0..kMaxIoiTicks inter-onset-spacing histogram.
   // Bin 0 (same-onset spacing - a chord, not a rhythmic interval) is never a candidate peak. This
   // is the "marginal extraction" step step-09 §1 flags as possibly shared with a future Bassline
   // node; kept local here since nothing else uses it yet (YAGNI - factor into NoteModel.h only
   // once a second caller exists).
   ModeSet FindModes(const uint32_t hist[kHistBins], uint32_t total)
   {
      ModeSet out;
      if (total == 0)
         return out;

      // A minimum-mass floor so a single stray onset doesn't get promoted to a "learned" spacing.
      constexpr float kMinShare = 0.03f;
      const uint32_t minCount = std::max<uint32_t>(1, (uint32_t)(kMinShare * (float)total));

      struct Peak { double bin; uint32_t count; };
      std::vector<Peak> peaks;
      for (int i = 1; i < kHistBins; i++)
      {
         if (hist[i] < minCount)
            continue;
         const uint32_t left = hist[i - 1];
         const uint32_t right = (i + 1 < kHistBins) ? hist[i + 1] : 0;
         if (hist[i] >= left && hist[i] >= right)
            peaks.push_back({ (double)i, hist[i] });
      }

      // Merge peaks within a few ticks of each other (a real peak alternating by 1 count between two
      // adjacent bins would otherwise register as two modes). The merged position is the count-
      // weighted centroid of the peaks folded in, not just the first one found.
      constexpr double kMergeTicks = 3.0;
      std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.bin < b.bin; });
      std::vector<Peak> merged;
      for (const Peak& p : peaks)
      {
         if (!merged.empty() && p.bin - merged.back().bin <= kMergeTicks)
         {
            Peak& m = merged.back();
            const double totalCount = (double)m.count + (double)p.count;
            m.bin = (m.bin * (double)m.count + p.bin * (double)p.count) / totalCount;
            m.count = (uint32_t)totalCount;
         }
         else
         {
            merged.push_back(p);
         }
      }

      std::sort(merged.begin(), merged.end(), [](const Peak& a, const Peak& b) { return a.count > b.count; });
      out.count = std::min<int>(kMaxModes, (int)merged.size());
      for (int i = 0; i < out.count; i++)
      {
         out.tick[i] = (float)merged[i].bin;
         out.weight[i] = (float)merged[i].count / (float)total;
      }
      return out;
   }

   // Nearest learned tick to a raw inter-onset spacing, or the raw spacing itself (identity) when
   // nothing has been learned yet - the node's cold-start behavior falls straight out of this rather
   // than a separate gate.
   double NearestMode(const ModeSet* modes, double ioiTicks)
   {
      if (modes == nullptr || modes->count == 0)
         return ioiTicks;
      double best = std::abs(ioiTicks - (double)modes->tick[0]);
      double nearest = (double)modes->tick[0];
      for (int i = 1; i < modes->count; i++)
      {
         const double d = std::abs(ioiTicks - (double)modes->tick[i]);
         if (d < best)
         {
            best = d;
            nearest = (double)modes->tick[i];
         }
      }
      return nearest;
   }

   // The global, cross-patch, cross-session learned spacing profile (PredictiveQuantizeProfile in
   // the header is the public face of this). A rolling window of the most recent captured IOIs -
   // oldest dropped once full - rather than an ever-growing history, so the profile tracks the
   // player's *current* feel. Refit is a full histogram pass over the window; at kMaxWindowSamples
   // that's a few thousand int comparisons, cheap enough to redo on every CookIfNeeded that finds
   // new data rather than maintaining an incremental histogram.
   constexpr size_t kMaxWindowSamples = 4000;

   class GlobalEngine
   {
   public:
      static GlobalEngine& Instance()
      {
         static GlobalEngine e;
         return e;
      }

      void AddSample(int ioiTicks)
      {
         mSamples.push_back(std::clamp(ioiTicks, 0, NoteModel::kMaxIoiTicks));
         if (mSamples.size() > kMaxWindowSamples)
            mSamples.pop_front();
         mVersion++;
      }

      uint64_t Version() const { return mVersion; }
      int TotalSamples() const { return (int)mSamples.size(); }

      ModeSet Refit() const
      {
         std::vector<uint32_t> hist(kHistBins, 0);
         for (int t : mSamples)
            hist[(size_t)t]++;
         return FindModes(hist.data(), (uint32_t)mSamples.size());
      }

      bool Load(const std::string& directory)
      {
         std::ifstream f(Path(directory), std::ios::binary | std::ios::ate);
         if (!f.is_open())
            return false;
         const size_t sz = (size_t)f.tellg();
         f.seekg(0, std::ios::beg);
         if (sz < sizeof(uint32_t))
            return false;
         uint32_t count = 0;
         f.read(reinterpret_cast<char*>(&count), sizeof(count));
         if (sz != sizeof(uint32_t) + (size_t)count * sizeof(int32_t))
            return false; // corrupt/foreign file: leave the engine as it was rather than guess
         std::deque<int> loaded;
         for (uint32_t i = 0; i < count; i++)
         {
            int32_t v = 0;
            f.read(reinterpret_cast<char*>(&v), sizeof(v));
            loaded.push_back(v);
         }
         mSamples = std::move(loaded);
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
         const uint32_t count = (uint32_t)mSamples.size();
         f.write(reinterpret_cast<const char*>(&count), sizeof(count));
         for (int v : mSamples)
         {
            const int32_t v32 = v;
            f.write(reinterpret_cast<const char*>(&v32), sizeof(v32));
         }
         return true;
      }

      bool HasLearnedData() const { return !mSamples.empty(); }

      // Test-only: INFINITE_PREDQUANTIZETEST runs in the same process as a possibly-already-loaded
      // real profile, so each test that wants a clean window must reset first and must not leave
      // synthetic data behind for whatever runs after it.
      void ResetForTest()
      {
         mSamples.clear();
         mVersion++;
      }

   private:
      static std::string Path(const std::string& dir)
      {
         std::filesystem::path p(dir);
         return (p / "quantize_profile.bin").string();
      }

      std::deque<int> mSamples;
      uint64_t mVersion = 0;
   };
}

// ------------------------------------------------------------------ audio object
class AudioPredictiveQuantizeNode : public AudioNode
{
public:
   using Cap = PredictiveQuantizeNode::Cap;
   static constexpr int kRing = 2048;
   static constexpr int kMaxPending = 32;
   static constexpr int kMaxBlockEvents = 64;

   ~AudioPredictiveQuantizeNode() override
   {
      delete mLive.load();
      for (Retired& r : mRetired)
         delete r.modes;
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mSamplePos = 0;
      mHaveGroup = false;
      for (DeferredNote& s : mPending)
         s.active = false;
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      Transport& tr = Transport::Instance();
      Render(output.numFrames, tr.Beats(), (double)tr.Tempo());
   }

   // The whole block, with the clock passed in so a test can drive it without a transport. Every
   // onset is captured (for the global profile) and corrected (against the currently-live one) in
   // the same pass - there is no learning/not-learning split anymore.
   void Render(int numFrames, double beatsStart, double bpm)
   {
      NoteEvent in[64];
      const int nIn = (mInbox != nullptr) ? mInbox->Pop(mNoteCursor, in, 64) : 0;
      NoteEvent out[kMaxBlockEvents];
      int nOut = 0;
      const double bps = std::max(1.0, bpm) / 60.0 / mSampleRate; // beats per sample
      const double blockEndBeat = beatsStart + (double)numFrames * bps;
      const float mix = std::clamp(mMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const ModeSet* modes = mLive.load(std::memory_order_acquire);

      for (int i = 0; i < nIn; i++)
      {
         const NoteEvent& e = in[i];

         if (!e.bendUpdate && e.isNoteOn && e.note >= 0 && e.note <= 127)
         {
            Cap c;
            c.beat = beatsStart + (double)e.frameOffset * bps;
            PushCapture(c);
         }

         if (e.bendUpdate || !e.isNoteOn)
         {
            // Bend updates and note-offs are never individually re-timed - they carry their
            // note-on's own voiceId, so they land correctly without this node tracking them.
            if (nOut < kMaxBlockEvents)
               out[nOut++] = e;
            continue;
         }

         const double rawBeat = beatsStart + (double)e.frameOffset * bps;
         const double rawTicks = rawBeat * (double)NoteModel::kTicksPerBeat;
         double correctedTicks;

         constexpr double kChordEpsilonTicks = 2.0; // near-simultaneous onsets = one chord, one correction
         if (!mHaveGroup)
         {
            correctedTicks = rawTicks;
            mGroupRawTicks = rawTicks;
            mGroupCorrectedTicks = rawTicks;
            mHaveGroup = true;
         }
         else if (std::abs(rawTicks - mGroupRawTicks) < kChordEpsilonTicks)
         {
            // Same onset group as the previous note: reuse its already-decided correction so a
            // chord doesn't get pulled apart in time relative to itself (step-09 §5).
            correctedTicks = mGroupCorrectedTicks;
         }
         else
         {
            const double ioiTicks = rawTicks - mGroupRawTicks;
            const double nearest = NearestMode(modes, ioiTicks);
            const double correctedIoi = ioiTicks + (double)mix * (nearest - ioiTicks);
            correctedTicks = mGroupCorrectedTicks + correctedIoi;
            mGroupRawTicks = rawTicks;
            mGroupCorrectedTicks = correctedTicks;
         }

         const double correctedBeat = correctedTicks / (double)NoteModel::kTicksPerBeat;

         if (correctedBeat < blockEndBeat)
         {
            int fo = (int)std::lround((correctedBeat - beatsStart) / bps);
            fo = std::clamp(fo, 0, numFrames - 1);
            NoteEvent o = e;
            o.frameOffset = fo;
            o.source = this;
            if (nOut < kMaxBlockEvents)
               out[nOut++] = o;
         }
         else if (DeferredNote* slot = FreeSlot())
         {
            slot->active = true;
            slot->note = e.note;
            slot->velocity = e.velocity;
            slot->isNoteOn = true;
            slot->targetSample = mSamplePos + (uint64_t)std::llround((correctedBeat - beatsStart) / bps);
            slot->voiceId = e.voiceId;
            slot->bendSemitones = e.bendSemitones;
         }
      }

      FireDue(out, nOut, numFrames);
      for (int i = 0; i < nOut; i++)
         mOutbox.Push(out[i]);
      mSamplePos += (uint64_t)numFrames;
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override { mInbox = inbox; mNoteCursor = cursor; }

   // ---- main thread ----
   void PushParams(float mixVal) { mMix.store(mixVal, std::memory_order_relaxed); }

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
         return; // ring full: drop rather than block the audio thread
      }
      mRing[tail % kRing] = c;
      mTail.store(tail + 1, std::memory_order_release);
   }

   int Dropped() const { return mDropped.load(std::memory_order_relaxed); }

   ModeSet* SwapModes(ModeSet* next) { return mLive.exchange(next, std::memory_order_acq_rel); }
   void Retire(ModeSet* old)
   {
      if (old != nullptr)
         mRetired.push_back({ old, mBlocksDone });
   }
   // Frees mode sets the audio thread has provably moved past. Called once per CookIfNeeded, so
   // "two calls ago" is a safe proxy for "the audio thread has rendered past it" the same way
   // PredictiveNotesNode uses a block counter - the main thread only ever touches mRetired.
   int CollectRetired()
   {
      mBlocksDone++;
      int freed = 0;
      for (size_t i = 0; i < mRetired.size();)
      {
         if (mBlocksDone >= mRetired[i].stamp + 2)
         {
            delete mRetired[i].modes;
            mRetired.erase(mRetired.begin() + (long)i);
            freed++;
         }
         else
            i++;
      }
      return freed;
   }

private:
   struct DeferredNote
   {
      bool active = false;
      int note = 0;
      float velocity = 0.0f;
      bool isNoteOn = false;
      uint64_t targetSample = 0;
      int voiceId = 0;
      float bendSemitones = 0.0f;
   };
   struct Retired
   {
      ModeSet* modes;
      uint64_t stamp;
   };

   DeferredNote* FreeSlot()
   {
      for (auto& s : mPending)
         if (!s.active)
            return &s;
      return nullptr;
   }
   // Emits, and clears, every deferred slot whose target sample falls inside
   // [mSamplePos, mSamplePos + numFrames) - same contract as NoteNodes.cpp's file-local
   // DeferredNoteQueue::FireDue, reimplemented here since that one is anonymous-namespace-scoped
   // to NoteNodes.cpp and not shared across translation units.
   void FireDue(NoteEvent* out, int& nOut, int numFrames)
   {
      for (auto& s : mPending)
      {
         if (!s.active || nOut >= kMaxBlockEvents)
            continue;
         if (s.targetSample >= mSamplePos && s.targetSample < mSamplePos + (uint64_t)numFrames)
         {
            NoteEvent o;
            o.note = s.note;
            o.velocity = s.velocity;
            o.isNoteOn = s.isNoteOn;
            o.frameOffset = (int)(s.targetSample - mSamplePos);
            o.bendSemitones = s.bendSemitones;
            o.source = this;
            o.voiceId = s.voiceId;
            out[nOut++] = o;
            s.active = false;
         }
      }
   }
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;
   double mSampleRate = 48000.0;
   uint64_t mSamplePos = 0;

   bool mHaveGroup = false;
   double mGroupRawTicks = 0.0;
   double mGroupCorrectedTicks = 0.0;

   DeferredNote mPending[kMaxPending];

   std::atomic<ModeSet*> mLive { nullptr };
   std::vector<Retired> mRetired; // main thread only
   uint64_t mBlocksDone = 0;      // main thread only

   Cap mRing[kRing];
   std::atomic<size_t> mHead { 0 }, mTail { 0 };
   std::atomic<int> mDropped { 0 };

   std::atomic<float> mMix { 0.75f };
};

PredictiveQuantizeNode::PredictiveQuantizeNode() = default;
PredictiveQuantizeNode::~PredictiveQuantizeNode() = default;

AudioNode* PredictiveQuantizeNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPredictiveQuantizeNode>();
   return mAudioNode.get();
}

void PredictiveQuantizeNode::VisitParams(ParamVisitor& v)
{
   v.Float("mix", mix);
}

int PredictiveQuantizeNode::ModeCount() const
{
   return mCachedModeCount;
}

float PredictiveQuantizeNode::Confidence01() const
{
   if (mCachedModeCount == 0)
      return 0.0f;
   // Honest, not decorative: adequacy (have we actually captured enough to trust a histogram) times
   // concentration (how much of the captured mass the learned modes actually explain, vs. spread
   // thin across many spacings that never solidified into a real peak).
   const float adequacy = std::clamp((float)GlobalEngine::Instance().TotalSamples() / 32.0f, 0.0f, 1.0f);
   return std::clamp(adequacy * std::clamp(mCachedWeightSum, 0.0f, 1.0f), 0.0f, 1.0f);
}

int PredictiveQuantizeNode::Dropped() const
{
   return mAudioNode ? mAudioNode->Dropped() : 0;
}

int PredictiveQuantizeNode::TotalCaptured() const
{
   return GlobalEngine::Instance().TotalSamples();
}

void PredictiveQuantizeNode::DrainCaptures()
{
   Cap c;
   while (mAudioNode->PopCapture(c))
   {
      if (mHaveLastBeat)
      {
         const int prevTick = NoteModel::SnapTicks(mLastCapturedBeat);
         const int tick = NoteModel::SnapTicks(c.beat);
         const int ioi = std::clamp(tick - prevTick, 0, NoteModel::kMaxIoiTicks);
         GlobalEngine::Instance().AddSample(ioi);
      }
      mLastCapturedBeat = c.beat;
      mHaveLastBeat = true;
   }
}

void PredictiveQuantizeNode::RefitIfDirty()
{
   GlobalEngine& engine = GlobalEngine::Instance();
   if (engine.Version() == mAppliedProfileVersion)
      return;
   mAppliedProfileVersion = engine.Version();

   const ModeSet fit = engine.Refit();
   mCachedModeCount = fit.count;
   mCachedWeightSum = 0.0f;
   for (int i = 0; i < fit.count; i++)
      mCachedWeightSum += fit.weight[i];

   ModeSet* fresh = new ModeSet(fit);
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapModes(fresh));
}

void PredictiveQuantizeNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();

   DrainCaptures();
   RefitIfDirty();
   mAudioNode->PushParams(mix);
   mAudioNode->CollectRetired();
}

void PredictiveQuantizeNode::SweepPrepare()
{
   // Plays nothing observably-different from pass-through until a mode set exists; give it one so
   // mix's effect is audible/testable (matches PredictiveNotesNode::SweepPrepare's reasoning). This
   // seeds the per-instance audio object directly rather than the global profile, so a hygiene sweep
   // never pollutes real learned data.
   ModeSet seed;
   seed.count = 2;
   seed.tick[0] = 12.0f; // an eighth note at 24 ticks/beat
   seed.weight[0] = 0.6f;
   seed.tick[1] = 24.0f; // a quarter note
   seed.weight[1] = 0.4f;
   mCachedModeCount = seed.count;
   mCachedWeightSum = seed.weight[0] + seed.weight[1];

   GetAudioNode();
   ModeSet* fresh = new ModeSet(seed);
   mAudioNode->Retire(mAudioNode->SwapModes(fresh));
}

namespace PredictiveQuantizeProfile
{
   bool Load(const std::string& directory) { return GlobalEngine::Instance().Load(directory); }
   bool Save(const std::string& directory) { return GlobalEngine::Instance().Save(directory); }
   bool HasLearnedData() { return GlobalEngine::Instance().HasLearnedData(); }
}

// ------------------------------------------------------------------ tests
namespace PredictiveQuantize
{
   namespace
   {
      int gFail = 0;
#define PQ_CHECK(cond, ...)                                       \
   do                                                              \
   {                                                                \
      if (!(cond))                                                 \
      {                                                             \
         gFail++;                                                   \
         std::printf("[PREDQUANTIZETEST] FAIL: " __VA_ARGS__);      \
         std::printf("\n");                                         \
      }                                                             \
   } while (0)
   }

   bool RunPredQuantizeTest()
   {
      gFail = 0;

      // 1. mix = 0 (forced by the encoded mode set's mix param at 0): output onset ticks are
      // bit-identical to input, even with a mode set loaded.
      {
         AudioPredictiveQuantizeNode node;
         node.PrepareToPlay(48000.0, 512);
         ModeSet* m = new ModeSet();
         m->count = 1;
         m->tick[0] = 12.0f;
         m->weight[0] = 1.0f;
         node.Retire(node.SwapModes(m));
         node.PushParams(0.0f);

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);

         NoteEvent on;
         on.note = 60;
         on.velocity = 0.8f;
         on.isNoteOn = true;
         on.frameOffset = 100;
         on.voiceId = NextVoiceId();
         inbox.Push(on);

         const int outCursor = node.NoteOutbox()->RegisterConsumer();
         node.Render(512, 0.0, 120.0);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PQ_CHECK(n == 1 && evts[0].frameOffset == 100, "mix=0 must not move the onset (got n=%d, frameOffset=%d)", n, n > 0 ? evts[0].frameOffset : -1);
      }

      // 2. mix = 1, two onsets whose spacing is near (but not exactly) a learned mode: the second
      // onset's spacing from the first lands exactly on that mode.
      {
         AudioPredictiveQuantizeNode node;
         node.PrepareToPlay(48000.0, 512);
         ModeSet* m = new ModeSet();
         m->count = 1;
         m->tick[0] = 12.0f; // an eighth note at 24 ticks/beat
         m->weight[0] = 1.0f;
         node.Retire(node.SwapModes(m));
         node.PushParams(1.0f);

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);

         const double bpm = 120.0;
         const double samplesPerBeat = 48000.0 * 60.0 / bpm;
         const double ticksPerSample = (double)NoteModel::kTicksPerBeat / samplesPerBeat;

         NoteEvent on1;
         on1.note = 60;
         on1.velocity = 0.8f;
         on1.isNoteOn = true;
         on1.frameOffset = 0;
         on1.voiceId = NextVoiceId();
         inbox.Push(on1);

         // Slightly off an eighth note (14 ticks instead of 12), so mix=1 should visibly correct it.
         const double offsetTicks = 14.0;
         NoteEvent on2;
         on2.note = 64;
         on2.velocity = 0.8f;
         on2.isNoteOn = true;
         on2.frameOffset = (int)std::lround(offsetTicks / ticksPerSample);
         on2.voiceId = NextVoiceId();
         inbox.Push(on2);

         // Wide enough that both the raw arrival (~14000 samples in, at this tempo/rate) and its
         // corrected landing fall inside one block - this test is about the correction math, not
         // block-boundary/deferred-note scheduling (covered by a real block size implicitly by
         // every other test here using a normal-sized block).
         const int outCursor = node.NoteOutbox()->RegisterConsumer();
         node.Render(20000, 0.0, bpm);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PQ_CHECK(n == 2, "expected 2 onsets, got %d", n);
         if (n == 2)
         {
            const double correctedTicks = (double)evts[1].frameOffset * ticksPerSample;
            PQ_CHECK(std::abs(correctedTicks - 12.0) < 0.5, "mix=1 should land on the learned mode (12 ticks), got %.2f", correctedTicks);
         }
      }

      // 3. Chord grouping: two near-simultaneous note-ons reuse the same correction, so they don't
      // arpeggiate apart in time.
      {
         AudioPredictiveQuantizeNode node;
         node.PrepareToPlay(48000.0, 512);
         ModeSet* m = new ModeSet();
         m->count = 1;
         m->tick[0] = 12.0f;
         m->weight[0] = 1.0f;
         node.Retire(node.SwapModes(m));
         node.PushParams(1.0f);

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);

         NoteEvent on1;
         on1.note = 60;
         on1.velocity = 0.8f;
         on1.isNoteOn = true;
         on1.frameOffset = 100;
         on1.voiceId = NextVoiceId();
         inbox.Push(on1);

         NoteEvent on2; // a chord note, a hair later in the same block
         on2.note = 64;
         on2.velocity = 0.8f;
         on2.isNoteOn = true;
         on2.frameOffset = 101;
         on2.voiceId = NextVoiceId();
         inbox.Push(on2);

         const int outCursor = node.NoteOutbox()->RegisterConsumer();
         node.Render(512, 0.0, 120.0);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PQ_CHECK(n == 2, "expected 2 chord onsets, got %d", n);
         if (n == 2)
            PQ_CHECK(evts[0].frameOffset == evts[1].frameOffset, "chord notes must share one corrected onset (got %d vs %d)", evts[0].frameOffset, evts[1].frameOffset);
      }

      // 4. Cold start: no mode set learned yet -> NearestMode returns the raw spacing -> identity,
      // regardless of mix.
      {
         AudioPredictiveQuantizeNode node;
         node.PrepareToPlay(48000.0, 512);
         node.PushParams(1.0f); // mix maxed, but no mode set was ever swapped in

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);

         NoteEvent on;
         on.note = 60;
         on.velocity = 0.8f;
         on.isNoteOn = true;
         on.frameOffset = 77;
         on.voiceId = NextVoiceId();
         inbox.Push(on);

         const int outCursor = node.NoteOutbox()->RegisterConsumer();
         node.Render(512, 0.0, 120.0);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PQ_CHECK(n == 1 && evts[0].frameOffset == 77, "cold start must be identity (got n=%d, frameOffset=%d)", n, n > 0 ? evts[0].frameOffset : -1);
      }

      // 5. The global profile's rolling window: a dominant early spacing is forgotten once enough
      // later samples of a different spacing push it out of the window, and a save/load round trip
      // preserves whatever the window currently holds.
      {
         GlobalEngine& engine = GlobalEngine::Instance();
         engine.ResetForTest();

         for (int i = 0; i < 50; i++)
            engine.AddSample(12); // an eighth note, heavily dominant at first
         ModeSet early = engine.Refit();
         PQ_CHECK(early.count >= 1 && std::abs(early.tick[0] - 12.0f) < 0.5f, "early window should learn the 12-tick mode (count=%d)", early.count);

         for (size_t i = 0; i < kMaxWindowSamples; i++)
            engine.AddSample(24); // a quarter note, enough to fully evict the 12s from a 4000-sample window
         ModeSet late = engine.Refit();
         PQ_CHECK(late.count >= 1 && std::abs(late.tick[0] - 24.0f) < 0.5f, "rolling window should forget the 12-tick mode once evicted (count=%d, tick0=%.2f)", late.count, late.count > 0 ? late.tick[0] : -1.0f);
         PQ_CHECK(engine.TotalSamples() == (int)kMaxWindowSamples, "window should be capped at kMaxWindowSamples (got %d)", engine.TotalSamples());

         const std::string tmpDir = (std::filesystem::temp_directory_path() / "infinite_predquantize_test").string();
         PQ_CHECK(engine.Save(tmpDir), "save must succeed");
         engine.ResetForTest();
         PQ_CHECK(engine.TotalSamples() == 0, "reset must clear the window");
         PQ_CHECK(engine.Load(tmpDir), "load must succeed");
         PQ_CHECK(engine.TotalSamples() == (int)kMaxWindowSamples, "load must restore the full window (got %d)", engine.TotalSamples());
         ModeSet reloaded = engine.Refit();
         PQ_CHECK(reloaded.count >= 1 && std::abs(reloaded.tick[0] - 24.0f) < 0.5f, "reloaded window should still show the 24-tick mode (count=%d)", reloaded.count);

         std::error_code ec;
         std::filesystem::remove_all(tmpDir, ec);
         engine.ResetForTest(); // leave no synthetic data behind for the running app or later tests
      }

      if (gFail == 0)
         std::printf("[PREDQUANTIZETEST] All checks passed.\n");
      return gFail == 0;
   }
}

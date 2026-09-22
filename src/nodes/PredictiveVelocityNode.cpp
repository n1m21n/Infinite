#include "nodes/PredictiveVelocityNode.h"

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

namespace
{
   // The learned remap: one mean played velocity (vel127 units) per NoteModel::kVelBins nominal
   // bin, monotonic non-decreasing. Crosses to the audio thread by atomic pointer swap, freed on
   // the main thread once retired - same shape as PredictiveQuantizeNode's ModeSet.
   struct Curve
   {
      float binMean[NoteModel::kVelBins] = {};
   };

   // Bin center of nominal bin i, in vel127 units (bins are NoteModel::VelBin's equal-width 16s).
   constexpr float kBinWidth = 128.0f / (float)NoteModel::kVelBins;
   float BinCenter(int i) { return kBinWidth * (float)i + kBinWidth * 0.5f; }

   // Piecewise-linear through the 8 (center, binMean) points, clamped flat beyond the first/last
   // center - a player who never plays outside a sub-range still gets a defined curve everywhere.
   float EvalCurve(const Curve& c, float v127)
   {
      const float pos = (v127 - BinCenter(0)) / kBinWidth;
      if (pos <= 0.0f)
         return c.binMean[0];
      if (pos >= (float)(NoteModel::kVelBins - 1))
         return c.binMean[NoteModel::kVelBins - 1];
      const int i0 = (int)pos;
      const float frac = pos - (float)i0;
      return c.binMean[i0] + frac * (c.binMean[i0 + 1] - c.binMean[i0]);
   }

   // Fits an 8-bin mean-velocity curve from raw captured vel127 samples, with nearest-neighbor
   // gap-fill for bins the player never reached and a monotonicity pass (real playing is noisy
   // enough that two adjacent bins can invert by a hair).
   Curve FitCurve(const std::deque<int>& samples)
   {
      double sum[NoteModel::kVelBins] = {};
      int count[NoteModel::kVelBins] = {};
      for (int v127 : samples)
      {
         const int b = NoteModel::VelBin(v127);
         sum[b] += (double)v127;
         count[b]++;
      }

      Curve fit;
      for (int i = 0; i < NoteModel::kVelBins; i++)
         fit.binMean[i] = count[i] > 0 ? (float)(sum[i] / (double)count[i]) : -1.0f; // -1 = not yet filled

      for (int i = 1; i < NoteModel::kVelBins; i++)
         if (fit.binMean[i] < 0.0f)
            fit.binMean[i] = fit.binMean[i - 1];
      for (int i = NoteModel::kVelBins - 2; i >= 0; i--)
         if (fit.binMean[i] < 0.0f)
            fit.binMean[i] = fit.binMean[i + 1];

      for (int i = 1; i < NoteModel::kVelBins; i++)
         fit.binMean[i] = std::max(fit.binMean[i], fit.binMean[i - 1]);

      return fit;
   }

   int CoveredBins(const std::deque<int>& samples)
   {
      bool seen[NoteModel::kVelBins] = {};
      for (int v127 : samples)
         seen[NoteModel::VelBin(v127)] = true;
      int n = 0;
      for (bool b : seen)
         if (b)
            n++;
      return n;
   }

   // The global, cross-patch, cross-session learned dynamics profile (PredictiveVelocityProfile in
   // the header is the public face of this). Rolling window of the most recent captured velocities -
   // same shape and window size as PredictiveQuantizeNode's GlobalEngine.
   constexpr size_t kMaxWindowSamples = 4000;

   class GlobalEngine
   {
   public:
      static GlobalEngine& Instance()
      {
         static GlobalEngine e;
         return e;
      }

      void AddSample(int v127)
      {
         mSamples.push_back(std::clamp(v127, 0, 127));
         if (mSamples.size() > kMaxWindowSamples)
            mSamples.pop_front();
         mVersion++;
      }

      uint64_t Version() const { return mVersion; }
      int TotalSamples() const { return (int)mSamples.size(); }
      int CoveredBins() const { return ::CoveredBins(mSamples); }

      Curve Refit() const { return FitCurve(mSamples); }

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

      // Test-only: INFINITE_PREDVELOCITYTEST runs in the same process as a possibly-already-loaded
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
         return (p / "velocity_profile.bin").string();
      }

      std::deque<int> mSamples;
      uint64_t mVersion = 0;
   };
}

// ------------------------------------------------------------------ audio object
class AudioPredictiveVelocityNode : public AudioNode
{
public:
   using Cap = int; // captured vel127

   static constexpr int kRing = 2048;

   ~AudioPredictiveVelocityNode() override
   {
      delete mLive.load();
      for (Retired& r : mRetired)
         delete r.curve;
   }

   void PrepareToPlay(double /*sampleRate*/, int /*maxBlockSize*/) override {}

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      NoteEvent evts[64];
      const int n = (mInbox != nullptr) ? mInbox->Pop(mNoteCursor, evts, 64) : 0;
      const float mix = std::clamp(mMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const Curve* curve = mLive.load(std::memory_order_acquire);

      for (int i = 0; i < n; i++)
      {
         NoteEvent out = evts[i];
         out.source = this;

         if (out.isNoteOn && !out.bendUpdate)
         {
            const int v127 = std::clamp((int)std::lround(std::clamp(out.velocity, 0.0f, 1.0f) * 127.0f), 0, 127);
            PushCapture(v127);
            if (curve != nullptr)
            {
               const float remapped = std::clamp(EvalCurve(*curve, (float)v127) / 127.0f, 0.0f, 1.0f);
               out.velocity = std::clamp(out.velocity + mix * (remapped - out.velocity), 0.0f, 1.0f);
            }
            // else: no curve learned yet - identity, matching Predictive Quantize's cold-start.
         }
         // Note-offs and bend updates are never touched: velocity shaping only ever applies at the
         // onset that actually carries a played dynamic.

         mOutbox.Push(out);
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }
   void SetNoteInbox(NoteEventQueue* inbox, int cursor) override { mInbox = inbox; mNoteCursor = cursor; }

   // ---- main thread ----
   void PushParams(float mixVal) { mMix.store(mixVal, std::memory_order_relaxed); }

   bool PopCapture(int& v127)
   {
      const size_t head = mHead.load(std::memory_order_relaxed);
      if (head == mTail.load(std::memory_order_acquire))
         return false;
      v127 = mRing[head % kRing];
      mHead.store(head + 1, std::memory_order_release);
      return true;
   }
   void PushCapture(int v127) // audio thread (tests: the "audio" thread)
   {
      const size_t tail = mTail.load(std::memory_order_relaxed);
      if (tail - mHead.load(std::memory_order_acquire) >= (size_t)kRing)
      {
         mDropped.fetch_add(1, std::memory_order_relaxed);
         return; // ring full: drop rather than block the audio thread
      }
      mRing[tail % kRing] = v127;
      mTail.store(tail + 1, std::memory_order_release);
   }

   int Dropped() const { return mDropped.load(std::memory_order_relaxed); }

   Curve* SwapCurve(Curve* next) { return mLive.exchange(next, std::memory_order_acq_rel); }
   void Retire(Curve* old)
   {
      if (old != nullptr)
         mRetired.push_back({ old, mBlocksDone });
   }
   // Frees curves the audio thread has provably moved past - same "two CookIfNeeded calls ago" proxy
   // PredictiveQuantizeNode uses.
   int CollectRetired()
   {
      mBlocksDone++;
      int freed = 0;
      for (size_t i = 0; i < mRetired.size();)
      {
         if (mBlocksDone >= mRetired[i].stamp + 2)
         {
            delete mRetired[i].curve;
            mRetired.erase(mRetired.begin() + (long)i);
            freed++;
         }
         else
            i++;
      }
      return freed;
   }

private:
   struct Retired
   {
      Curve* curve;
      uint64_t stamp;
   };

   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox = nullptr;
   int mNoteCursor = -1;

   std::atomic<Curve*> mLive { nullptr };
   std::vector<Retired> mRetired; // main thread only
   uint64_t mBlocksDone = 0;      // main thread only

   int mRing[kRing] = {};
   std::atomic<size_t> mHead { 0 }, mTail { 0 };
   std::atomic<int> mDropped { 0 };

   std::atomic<float> mMix { 0.75f };
};

PredictiveVelocityNode::PredictiveVelocityNode() = default;
PredictiveVelocityNode::~PredictiveVelocityNode() = default;

AudioNode* PredictiveVelocityNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPredictiveVelocityNode>();
   return mAudioNode.get();
}

void PredictiveVelocityNode::VisitParams(ParamVisitor& v)
{
   v.Float("mix", mix);
}

bool PredictiveVelocityNode::HasCurve() const
{
   return mCachedHasCurve;
}

float PredictiveVelocityNode::Confidence01() const
{
   if (!mCachedHasCurve)
      return 0.0f;
   // Same shape as PredictiveQuantizeNode::Confidence01: adequacy (captured enough notes to trust
   // the fit) times coverage (how many of the 8 velocity bins actually saw a played note, vs. how
   // many were only filled in by FitCurve's nearest-neighbor propagation).
   const float adequacy = std::clamp((float)GlobalEngine::Instance().TotalSamples() / 32.0f, 0.0f, 1.0f);
   const float coverage = std::clamp((float)mCachedBinsCovered / (float)NoteModel::kVelBins, 0.0f, 1.0f);
   return std::clamp(adequacy * coverage, 0.0f, 1.0f);
}

int PredictiveVelocityNode::Dropped() const
{
   return mAudioNode ? mAudioNode->Dropped() : 0;
}

int PredictiveVelocityNode::TotalCaptured() const
{
   return GlobalEngine::Instance().TotalSamples();
}

void PredictiveVelocityNode::DrainCaptures()
{
   int v;
   while (mAudioNode->PopCapture(v))
      GlobalEngine::Instance().AddSample(v);
}

void PredictiveVelocityNode::RefitIfDirty()
{
   GlobalEngine& engine = GlobalEngine::Instance();
   if (engine.Version() == mAppliedProfileVersion)
      return;
   mAppliedProfileVersion = engine.Version();

   // Needs real coverage across the range to fit meaningfully, not just replay 2-3 samples as if
   // they were the whole distribution (step-10-predictive-velocity.md §4 "fitting from too little
   // data"). Below the floor, whatever curve is already live is left exactly as it was.
   constexpr int kMinNotes = 16;
   if (engine.TotalSamples() < kMinNotes)
      return;

   const Curve fit = engine.Refit();
   mCachedHasCurve = true;
   mCachedBinsCovered = engine.CoveredBins();

   Curve* fresh = new Curve(fit);
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapCurve(fresh));
}

void PredictiveVelocityNode::CookIfNeeded(int frameId)
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

void PredictiveVelocityNode::SweepPrepare()
{
   // Plays nothing observably different from pass-through until a curve exists; give it one, well
   // away from identity, so mix's effect is audible/testable (matches PredictiveQuantizeNode's
   // SweepPrepare reasoning). This seeds the per-instance audio object directly rather than the
   // global profile, so a hygiene sweep never pollutes real learned data. Unlike Predictive
   // Quantize's timing correction, a velocity remap applies at the very note-on the generic sweep
   // rig already drives, so no second onset is needed for this to be observable.
   Curve seed;
   for (int i = 0; i < NoteModel::kVelBins; i++)
      seed.binMean[i] = BinCenter(i) * 0.5f; // compresses everything toward the bottom half
   mCachedHasCurve = true;
   mCachedBinsCovered = NoteModel::kVelBins;

   GetAudioNode();
   Curve* fresh = new Curve(seed);
   mAudioNode->Retire(mAudioNode->SwapCurve(fresh));
}

namespace PredictiveVelocityProfile
{
   bool Load(const std::string& directory) { return GlobalEngine::Instance().Load(directory); }
   bool Save(const std::string& directory) { return GlobalEngine::Instance().Save(directory); }
   bool HasLearnedData() { return GlobalEngine::Instance().HasLearnedData(); }
}

// ------------------------------------------------------------------ tests
namespace PredictiveVelocity
{
   namespace
   {
      int gFail = 0;
#define PV_CHECK(cond, ...)                                      \
   do                                                              \
   {                                                                \
      if (!(cond))                                                 \
      {                                                             \
         gFail++;                                                   \
         std::printf("[PREDVELOCITYTEST] FAIL: " __VA_ARGS__);      \
         std::printf("\n");                                         \
      }                                                             \
   } while (0)
   }

   bool RunPredVelocityTest()
   {
      gFail = 0;

      // 1. mix = 0: output velocity bit-identical to input, even with a curve loaded.
      {
         AudioPredictiveVelocityNode node;
         node.PrepareToPlay(48000.0, 512);
         Curve* c = new Curve();
         for (int i = 0; i < NoteModel::kVelBins; i++)
            c->binMean[i] = 20.0f; // maximally different from identity
         node.Retire(node.SwapCurve(c));
         node.PushParams(0.0f);

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);
         const int outCursor = node.NoteOutbox()->RegisterConsumer();

         NoteEvent on;
         on.note = 60;
         on.velocity = 0.9f;
         on.isNoteOn = true;
         inbox.Push(on);

         AudioBuffer out;
         float scratchL[16] = {}, scratchR[16] = {};
         float* chans[2] = { scratchL, scratchR };
         out.channels = chans;
         out.numChannels = 2;
         out.numFrames = 16;
         node.ProcessBlock(nullptr, 0, out);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PV_CHECK(n == 1 && std::abs(evts[0].velocity - 0.9f) < 1e-6f, "mix=0 must not touch velocity (got n=%d, vel=%.4f)", n, n > 0 ? evts[0].velocity : -1.0f);
      }

      // 2. mix = 1: output velocity follows the learned curve exactly.
      {
         AudioPredictiveVelocityNode node;
         node.PrepareToPlay(48000.0, 512);
         Curve* c = new Curve();
         for (int i = 0; i < NoteModel::kVelBins; i++)
            c->binMean[i] = 40.0f; // every input collapses onto 40/127 regardless of bin
         node.Retire(node.SwapCurve(c));
         node.PushParams(1.0f);

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);
         const int outCursor = node.NoteOutbox()->RegisterConsumer();

         NoteEvent on;
         on.note = 60;
         on.velocity = 1.0f; // nominal loudest
         on.isNoteOn = true;
         inbox.Push(on);

         AudioBuffer out;
         float scratchL[16] = {}, scratchR[16] = {};
         float* chans[2] = { scratchL, scratchR };
         out.channels = chans;
         out.numChannels = 2;
         out.numFrames = 16;
         node.ProcessBlock(nullptr, 0, out);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         const float expected = 40.0f / 127.0f;
         PV_CHECK(n == 1 && std::abs(evts[0].velocity - expected) < 0.01f, "mix=1 must land on the learned curve (got n=%d, vel=%.4f, want %.4f)", n, n > 0 ? evts[0].velocity : -1.0f, expected);
      }

      // 3. Too-few-notes: the global profile's refit is gated on a minimum sample count, so a
      // handful of captures must not install a curve.
      {
         GlobalEngine& engine = GlobalEngine::Instance();
         engine.ResetForTest();

         PredictiveVelocityNode node;
         node.GetAudioNode();
         for (int i = 0; i < 3; i++)
            node.Audio()->PushCapture(90);
         node.CookIfNeeded(1); // drains captures into the global profile and attempts a refit
         PV_CHECK(!node.HasCurve(), "3 captured notes must not install a curve");

         engine.ResetForTest();
      }

      // 4. Cold start: no curve learned yet -> identity, regardless of mix.
      {
         AudioPredictiveVelocityNode node;
         node.PrepareToPlay(48000.0, 512);
         node.PushParams(1.0f); // mix maxed, but no curve was ever swapped in

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);
         const int outCursor = node.NoteOutbox()->RegisterConsumer();

         NoteEvent on;
         on.note = 60;
         on.velocity = 0.42f;
         on.isNoteOn = true;
         inbox.Push(on);

         AudioBuffer out;
         float scratchL[16] = {}, scratchR[16] = {};
         float* chans[2] = { scratchL, scratchR };
         out.channels = chans;
         out.numChannels = 2;
         out.numFrames = 16;
         node.ProcessBlock(nullptr, 0, out);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PV_CHECK(n == 1 && std::abs(evts[0].velocity - 0.42f) < 1e-6f, "cold start must be identity (got n=%d, vel=%.4f)", n, n > 0 ? evts[0].velocity : -1.0f);
      }

      // 5. Note-off events are never remapped, even with mix=1.
      {
         AudioPredictiveVelocityNode node;
         node.PrepareToPlay(48000.0, 512);
         Curve* c = new Curve();
         for (int i = 0; i < NoteModel::kVelBins; i++)
            c->binMean[i] = 10.0f;
         node.Retire(node.SwapCurve(c));
         node.PushParams(1.0f);

         NoteEventQueue inbox;
         const int cursor = inbox.RegisterConsumer();
         node.SetNoteInbox(&inbox, cursor);
         const int outCursor = node.NoteOutbox()->RegisterConsumer();

         NoteEvent off;
         off.note = 60;
         off.velocity = 0.6f;
         off.isNoteOn = false;
         inbox.Push(off);

         AudioBuffer out;
         float scratchL[16] = {}, scratchR[16] = {};
         float* chans[2] = { scratchL, scratchR };
         out.channels = chans;
         out.numChannels = 2;
         out.numFrames = 16;
         node.ProcessBlock(nullptr, 0, out);

         NoteEvent evts[8];
         const int n = node.NoteOutbox()->Pop(outCursor, evts, 8);
         PV_CHECK(n == 1 && std::abs(evts[0].velocity - 0.6f) < 1e-6f, "note-off velocity must pass through untouched (got n=%d, vel=%.4f)", n, n > 0 ? evts[0].velocity : -1.0f);
      }

      // 6. The global profile's rolling window forgets old samples once evicted, and a save/load
      // round trip preserves whatever the window currently holds - same coverage as
      // PredictiveQuantize's window test.
      {
         GlobalEngine& engine = GlobalEngine::Instance();
         engine.ResetForTest();

         for (int i = 0; i < 50; i++)
            engine.AddSample(20); // soft, dominant at first
         Curve early = engine.Refit();
         PV_CHECK(early.binMean[0] < 30.0f, "early window should reflect soft playing (bin0=%.2f)", early.binMean[0]);

         for (size_t i = 0; i < kMaxWindowSamples; i++)
            engine.AddSample(110); // loud, enough to fully evict the soft samples from the window
         Curve late = engine.Refit();
         PV_CHECK(late.binMean[NoteModel::kVelBins - 1] > 90.0f, "rolling window should forget soft playing once evicted (top bin=%.2f)", late.binMean[NoteModel::kVelBins - 1]);
         PV_CHECK(engine.TotalSamples() == (int)kMaxWindowSamples, "window should be capped at kMaxWindowSamples (got %d)", engine.TotalSamples());

         const std::string tmpDir = (std::filesystem::temp_directory_path() / "infinite_predvelocity_test").string();
         PV_CHECK(engine.Save(tmpDir), "save must succeed");
         engine.ResetForTest();
         PV_CHECK(engine.TotalSamples() == 0, "reset must clear the window");
         PV_CHECK(engine.Load(tmpDir), "load must succeed");
         PV_CHECK(engine.TotalSamples() == (int)kMaxWindowSamples, "load must restore the full window (got %d)", engine.TotalSamples());

         std::error_code ec;
         std::filesystem::remove_all(tmpDir, ec);
         engine.ResetForTest(); // leave no synthetic data behind for the running app or later tests
      }

      if (gFail == 0)
         std::printf("[PREDVELOCITYTEST] All checks passed.\n");
      return gFail == 0;
   }
}

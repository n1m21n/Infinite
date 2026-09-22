#include "nodes/PredictiveVelocityNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
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

   std::string EncodeCurve(const Curve& c)
   {
      std::string s;
      char buf[32];
      for (int i = 0; i < NoteModel::kVelBins; i++)
      {
         std::snprintf(buf, sizeof(buf), "%s%.2f", i ? ";" : "", (double)c.binMean[i]);
         s += buf;
      }
      return s;
   }

   bool DecodeCurve(const std::string& s, Curve& out)
   {
      int n = 0;
      size_t pos = 0;
      while (pos <= s.size() && n < NoteModel::kVelBins)
      {
         const size_t semi = s.find(';', pos);
         const std::string tok = s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
         if (tok.empty())
            break;
         out.binMean[n++] = std::strtof(tok.c_str(), nullptr);
         if (semi == std::string::npos)
            break;
         pos = semi + 1;
      }
      return n == NoteModel::kVelBins;
   }
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
      const bool learning = mLearning.load(std::memory_order_relaxed);
      const float mix = learning ? 0.0f : std::clamp(mMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const Curve* curve = learning ? nullptr : mLive.load(std::memory_order_acquire);

      for (int i = 0; i < n; i++)
      {
         NoteEvent out = evts[i];
         out.source = this;

         if (out.isNoteOn && !out.bendUpdate)
         {
            const int v127 = std::clamp((int)std::lround(std::clamp(out.velocity, 0.0f, 1.0f) * 127.0f), 0, 127);
            if (learning)
               PushCapture(v127);
            else if (curve != nullptr)
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
   void PushParams(float mixVal, bool learning)
   {
      mMix.store(mixVal, std::memory_order_relaxed);
      mLearning.store(learning, std::memory_order_relaxed);
   }

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
         return; // ring full: drop rather than block the audio thread
      mRing[tail % kRing] = v127;
      mTail.store(tail + 1, std::memory_order_release);
   }

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

   std::atomic<float> mMix { 0.75f };
   std::atomic<bool> mLearning { false };
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
   v.Text("velCurve", velCurve);
}

bool PredictiveVelocityNode::HasCurve() const
{
   Curve c;
   return DecodeCurve(velCurve, c);
}

void PredictiveVelocityNode::SetLearning(bool on)
{
   if (on == mLearning)
      return;
   if (!on)
   {
      FinishLearn();
      return;
   }
   GetAudioNode();
   int stale;
   while (mAudioNode->PopCapture(stale))
   {
   }
   mCapturedVel127.clear();
   mNotesCaptured = 0;
   mLastLearnTooShort = false;
   mLearning = true;
}

void PredictiveVelocityNode::DrainCaptures()
{
   int v;
   while (mAudioNode->PopCapture(v))
   {
      if (mCapturedVel127.size() < 16384)
         mCapturedVel127.push_back(v);
      mNotesCaptured++;
   }
}

void PredictiveVelocityNode::FitCurve()
{
   double sum[NoteModel::kVelBins] = {};
   int count[NoteModel::kVelBins] = {};
   for (int v127 : mCapturedVel127)
   {
      const int b = NoteModel::VelBin(v127);
      sum[b] += (double)v127;
      count[b]++;
   }

   Curve fit;
   for (int i = 0; i < NoteModel::kVelBins; i++)
      fit.binMean[i] = count[i] > 0 ? (float)(sum[i] / (double)count[i]) : -1.0f; // -1 = not yet filled

   // A nominal bin the player never actually reached (e.g. never plays above 110) has no data to
   // average - fill it from the nearest bin that does, so the curve stays defined across the whole
   // range instead of leaving a hole. This is what turns "never plays below 40 or above 110" into
   // "the curve compresses onto that learned band" (step-10-predictive-velocity.md §2-3) rather
   // than a fit failure - the overall too-little-data case is gated on total count below, not on
   // making every one of 8 bins individually well-populated.
   for (int i = 1; i < NoteModel::kVelBins; i++)
      if (fit.binMean[i] < 0.0f)
         fit.binMean[i] = fit.binMean[i - 1];
   for (int i = NoteModel::kVelBins - 2; i >= 0; i--)
      if (fit.binMean[i] < 0.0f)
         fit.binMean[i] = fit.binMean[i + 1];

   // Enforce monotonicity: real playing is noisy enough that two adjacent bins can invert by a
   // hair, which would make EvalCurve locally non-monotonic (a louder nominal input mapping quieter
   // than a softer one) - a running max keeps the curve honest as "louder nominal -> louder or
   // equal output".
   for (int i = 1; i < NoteModel::kVelBins; i++)
      fit.binMean[i] = std::max(fit.binMean[i], fit.binMean[i - 1]);

   velCurve = EncodeCurve(fit);
}

void PredictiveVelocityNode::FinishLearn()
{
   if (!mLearning)
      return;
   DrainCaptures();
   mLearning = false;

   // Needs real coverage across the range to fit meaningfully, not just replay 2-3 samples as if
   // they were the whole distribution (step-10-predictive-velocity.md §4 "fitting from too little
   // data"). Below the floor, the previous curve (if any) is left exactly as it was.
   constexpr size_t kMinNotes = 16;
   mLastLearnTooShort = mCapturedVel127.size() < kMinNotes;
   if (!mLastLearnTooShort)
      FitCurve();
   mCapturedVel127.clear();
   mAppliedCurve = velCurve;

   Curve* fresh = new Curve();
   DecodeCurve(velCurve, *fresh);
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapCurve(fresh));
}

void PredictiveVelocityNode::TestSwapCurve(const std::string& encoded)
{
   velCurve = encoded;
   mAppliedCurve = encoded;
   Curve* fresh = new Curve();
   DecodeCurve(velCurve, *fresh);
   GetAudioNode();
   mAudioNode->Retire(mAudioNode->SwapCurve(fresh));
}

void PredictiveVelocityNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();

   // The saved curve text changed (patch load, undo): install it without going through Learn.
   if (velCurve != mAppliedCurve)
   {
      mAppliedCurve = velCurve;
      Curve* fresh = new Curve();
      DecodeCurve(velCurve, *fresh);
      mAudioNode->Retire(mAudioNode->SwapCurve(fresh));
   }

   mAudioNode->PushParams(mix, mLearning);
   mAudioNode->CollectRetired();
   if (mLearning)
      DrainCaptures();
}

void PredictiveVelocityNode::SweepPrepare()
{
   // Plays nothing observably different from pass-through until a curve exists; give it one, well
   // away from identity, so mix's effect is audible/testable (matches PredictiveQuantizeNode's
   // SweepPrepare reasoning). Unlike Predictive Quantize's timing correction, a velocity remap
   // applies at the very note-on the generic sweep rig already drives, so no second onset is needed
   // for this to be observable.
   Curve seed;
   for (int i = 0; i < NoteModel::kVelBins; i++)
      seed.binMean[i] = BinCenter(i) * 0.5f; // compresses everything toward the bottom half
   velCurve = EncodeCurve(seed);
   mAppliedCurve = velCurve;
   GetAudioNode();
   Curve* fresh = new Curve();
   DecodeCurve(velCurve, *fresh);
   mAudioNode->Retire(mAudioNode->SwapCurve(fresh));
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
         node.PushParams(0.0f, false);

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
         node.PushParams(1.0f, false);

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

      // 3. Too-few-notes: FinishLearn falls back to identity (no curve installed) rather than
      // overfitting to a couple of samples.
      {
         PredictiveVelocityNode node;
         node.SetLearning(true);
         node.GetAudioNode();
         for (int i = 0; i < 3; i++)
            node.Audio()->PushCapture(90);
         node.SetLearning(false); // FinishLearn
         PV_CHECK(node.LastLearnTooShort(), "3 captured notes should be reported as too short");
         PV_CHECK(!node.HasCurve(), "too-short learn must not install a curve");
      }

      // 4. Cold start: no curve learned yet -> identity, regardless of mix.
      {
         AudioPredictiveVelocityNode node;
         node.PrepareToPlay(48000.0, 512);
         node.PushParams(1.0f, false); // mix maxed, but no curve was ever swapped in

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

      // 5. Note-off events are never remapped, even mid-Learn or with mix=1.
      {
         AudioPredictiveVelocityNode node;
         node.PrepareToPlay(48000.0, 512);
         Curve* c = new Curve();
         for (int i = 0; i < NoteModel::kVelBins; i++)
            c->binMean[i] = 10.0f;
         node.Retire(node.SwapCurve(c));
         node.PushParams(1.0f, false);

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

      // 6. Save/load round-trip: encode then decode a curve and confirm the values survive.
      {
         Curve c;
         for (int i = 0; i < NoteModel::kVelBins; i++)
            c.binMean[i] = 10.0f + (float)i * 12.5f;
         const std::string encoded = EncodeCurve(c);
         Curve decoded;
         const bool ok = DecodeCurve(encoded, decoded);
         PV_CHECK(ok, "round-trip must decode all %d bins", NoteModel::kVelBins);
         if (ok)
            for (int i = 0; i < NoteModel::kVelBins; i++)
               PV_CHECK(std::abs(decoded.binMean[i] - c.binMean[i]) < 0.01f, "bin %d mismatch: got %.4f want %.4f", i, decoded.binMean[i], c.binMean[i]);
      }

      if (gFail == 0)
         std::printf("[PREDVELOCITYTEST] All checks passed.\n");
      return gFail == 0;
   }
}

#include "nodes/PredictiveModulatorNode.h"

#include "core/Base64.h"
#include "core/Transport.h"

#include <algorithm>
#include <cstring>

PredictiveModulatorNode::PredictiveModulatorNode()
{
   // Two Modulators dropped on the canvas must not free-run identical noise (RandomNode::NextSeed's
   // lesson, same as DriftNode/MovesNode).
   static int sSpawn = 0;
   mFreeRng = 0x9E3779B97F4A7C15ull ^ (uint64_t)(++sSpawn) * 0x2545F4914F6CDD1Dull;
}

void PredictiveModulatorNode::CookIfNeeded(int)
{
   LoadFitFromSaved();
}

// Synthetic KeyIds for the delay-embedded copies of the single input signal. This engine is
// private to this node, so these never need to line up with any real GraphNode's uid - they
// just need to be distinct and stable across a Learn session.
namespace
{
   MovementStats::KeyId LagKey(int i) { return MovementStats::KeyId{ (uint64_t)(i + 1), 0 }; }
   constexpr int kMaxRank = 8;
}

void PredictiveModulatorNode::SetLearning(bool on)
{
   if (on == mLearning)
      return;

   if (on)
   {
      const int r = std::clamp(rank, 2, kMaxRank);
      mEngine.Reset();
      for (int i = 0; i < r; i++)
         mEngine.RegisterKey(LagKey(i), "PredictiveModulator", "lag", true);
      mDelayLine.clear();
      mSamplesCaptured = 0;
      mLastCaptureSeconds = -1.0;
      mLearning = true;
      // Engine::Tick()'s own activity gate ("mPlaying || a Hand/Perf touch in the last 30s")
      // would otherwise leave this private engine permanently idle: nothing ever feeds it a
      // Hand/Perf observation (Observe() below always tags Source::Modulator), so without this
      // the gate never opens and posHistory - what FitDMD actually reads - never grows no
      // matter how long Learn runs.
      mEngine.SetPlaying(true);
   }
   else
   {
      mLearning = false;
      mEngine.SetPlaying(false);
      FinishLearn();
   }
}

void PredictiveModulatorNode::CaptureTick()
{
   // Engine::Advance()/kGridDt are a real-time, 100ms-seconds grid (MovementStats.h) - Beats()
   // is musical time and would make the learning cadence scale with tempo.
   const double seconds = Transport::Instance().Seconds();
   if (mLastCaptureSeconds >= 0.0 && seconds == mLastCaptureSeconds)
      return; // already captured this tick - Value01() can be read more than once per frame

   const int r = std::clamp(rank, 2, kMaxRank);
   const float raw = std::clamp(input ? input->Value01() : constantIn, 0.0f, 1.0f);

   mDelayLine.insert(mDelayLine.begin(), raw);
   if ((int)mDelayLine.size() > r)
      mDelayLine.resize(r);

   if ((int)mDelayLine.size() == r)
   {
      for (int i = 0; i < r; i++)
         mEngine.Observe(LagKey(i), seconds, mDelayLine[i], MovementLog::Source::Modulator, 0);
      mEngine.Advance(seconds);
      mSamplesCaptured++;
   }
   mLastCaptureSeconds = seconds;
}

void PredictiveModulatorNode::FinishLearn()
{
   const int r = std::clamp(rank, 2, kMaxRank);
   mFit = mEngine.FitDMD(r);
   mHasLearnAttempt = true;

   if (mFit.valid)
   {
      // A is rank x (rank+1) (affine: linear part + bias column) and noiseStd is rank entries -
      // both need to survive a save/reload, or a reloaded fit would free-run with no noise term
      // and decay onto a silent fixed point instead of a generated sequence.
      const size_t floatCount = mFit.A.size() + mFit.noiseStd.size();
      std::vector<uint8_t> bytes(sizeof(int32_t) + sizeof(float) + floatCount * sizeof(float));
      size_t off = 0;
      const int32_t rankLE = (int32_t)mFit.rank;
      std::memcpy(bytes.data() + off, &rankLE, sizeof(rankLE));
      off += sizeof(rankLE);
      std::memcpy(bytes.data() + off, &mFit.spectralRadius, sizeof(mFit.spectralRadius));
      off += sizeof(mFit.spectralRadius);
      for (float v : mFit.A)
      {
         std::memcpy(bytes.data() + off, &v, sizeof(v));
         off += sizeof(v);
      }
      for (float v : mFit.noiseStd)
      {
         std::memcpy(bytes.data() + off, &v, sizeof(v));
         off += sizeof(v);
      }
      fitData = Base64::Encode(bytes.data(), bytes.size());
      // Seed the free-run state from the tail of what was just learned, so playback picks up
      // where Learn left off instead of jumping to a flat 0.5 - the delay line is already in
      // the same most-recent-first order the lag keys were registered in.
      mFreeState = mDelayLine;
      mFreeState.resize(mFit.rank, 0.5f);
      mPrevValue = mFreeState[0];
      mTargetValue = mFreeState[0];
   }
   else
   {
      fitData.clear();
      mFreeState.clear();
   }
   mAppliedFitData = fitData;
   mLastStepSeconds = -1.0;
}

void PredictiveModulatorNode::LoadFitFromSaved()
{
   if (fitData == mAppliedFitData)
      return;
   mAppliedFitData = fitData;
   mFit = MovementStats::DMDFit();
   mFreeState.clear();
   mLastStepSeconds = -1.0;
   if (fitData.empty())
      return;

   std::vector<uint8_t> bytes;
   if (!Base64::Decode(fitData, bytes) || bytes.size() < sizeof(int32_t) + sizeof(float))
      return;

   size_t off = 0;
   int32_t rankLE = 0;
   std::memcpy(&rankLE, bytes.data() + off, sizeof(rankLE));
   off += sizeof(rankLE);
   float sr = 0.0f;
   std::memcpy(&sr, bytes.data() + off, sizeof(sr));
   off += sizeof(sr);

   if (rankLE <= 0 || rankLE > kMaxRank)
      return;
   const size_t aCount = (size_t)rankLE * (size_t)(rankLE + 1);
   const size_t need = off + (aCount + (size_t)rankLE) * sizeof(float);
   if (bytes.size() < need)
      return;

   mFit.rank = rankLE;
   mFit.spectralRadius = sr;
   mFit.A.resize(aCount);
   for (auto& v : mFit.A)
   {
      std::memcpy(&v, bytes.data() + off, sizeof(v));
      off += sizeof(v);
   }
   mFit.noiseStd.resize(rankLE);
   for (auto& v : mFit.noiseStd)
   {
      std::memcpy(&v, bytes.data() + off, sizeof(v));
      off += sizeof(v);
   }
   mFit.keys.clear();
   for (int i = 0; i < rankLE; i++)
      mFit.keys.push_back(LagKey(i));
   mFit.valid = true;
   mFreeState.assign(rankLE, 0.5f);
   mPrevValue = 0.5f;
   mTargetValue = 0.5f;
}

float PredictiveModulatorNode::Value01()
{
   LoadFitFromSaved();

   const float raw = std::clamp(input ? input->Value01() : constantIn, 0.0f, 1.0f);
   if (bypassed)
      return low + (high - low) * raw;

   if (mLearning)
   {
      CaptureTick(); // pass-through while learning - never drives the destination blind
      return low + (high - low) * raw;
   }

   if (!mFit.valid || mFreeState.empty())
      return low + (high - low) * raw; // no fit yet: behave like a plain pass-through/constant

   // Free-run, open-loop: step the learned model's own state at the configured speed cadence,
   // smoothly interpolating between 10 Hz discrete steps so the output renders as silky curves.
   const double seconds = Transport::Instance().Seconds();
   const float effSpeed = std::clamp(speed, 0.05f, 20.0f);
   const double stepDt = 0.1 / (double)effSpeed;
   if (mLastStepSeconds < 0.0)
   {
      mLastStepSeconds = seconds;
      mPrevValue = mFreeState[0];
      mFit.Step(mFreeState, &mFreeRng);
      mTargetValue = mFreeState[0];
   }
   else if (seconds >= mLastStepSeconds + stepDt || seconds < mLastStepSeconds)
   {
      int steps = (int)((seconds - mLastStepSeconds) / stepDt);
      steps = std::clamp(steps, 1, 4);
      for (int s = 0; s < steps; ++s)
      {
         mPrevValue = mTargetValue;
         mFit.Step(mFreeState, &mFreeRng);
         mTargetValue = mFreeState[0];
      }
      mLastStepSeconds = seconds;
   }

   const float alpha = (stepDt > 0.0) ? (float)std::clamp((seconds - mLastStepSeconds) / stepDt, 0.0, 1.0) : 1.0f;
   const float smoothAlpha = alpha * alpha * (3.0f - 2.0f * alpha);
   const float interpolated = mPrevValue + (mTargetValue - mPrevValue) * smoothAlpha;
   const float normalized = std::clamp(interpolated, 0.0f, 1.0f);
   return low + (high - low) * normalized;
}

int PredictiveModulatorNode::LearningPercent() const
{
   // Every registered lag key is observed together each capture tick, so any one of them
   // reports the same history length FitDMD will actually see.
   const size_t have = mEngine.HistoryLengthFor(LagKey(0));
   return (int)std::clamp(100.0 * (double)have / (double)kMinFitSamples, 0.0, 100.0);
}

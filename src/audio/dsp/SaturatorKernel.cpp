#include "SaturatorKernel.h"

#include "nodes/AudioEffectNode.h"

using namespace SaturatorDsp;

void SaturatorKernel::PrepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
   mSampleRate = sampleRate;
   mMailbox.PrepareToPlay(sampleRate);
   mOversampler.Prepare(kOversample, sampleRate);
   mDryOversampler.Prepare(kOversample, sampleRate);
   const double osRate = sampleRate * kOversample;
   mFeedbackDelay = std::max(1, (int)std::lround(kFeedbackDelaySec * sampleRate));
   mFeedbackLpCoef = (float)std::exp(-2.0 * M_PI * kFeedbackLowpassHz / sampleRate);
   mSwitchStep = (float)(1.0 / std::max(1.0, kRoutingFadeSec * sampleRate));
   for (int ch = 0; ch < kMaxChannels; ch++)
   {
      mLowSplit[ch].Prepare(osRate, kLowCrossoverHz);
      mHighSplit[ch].Prepare(osRate, kHighCrossoverHz);
      mLowBandAllpass[ch].Prepare(osRate, kHighCrossoverHz);
      mDryAllpassLow[ch].Prepare(osRate, kLowCrossoverHz);
      mDryAllpassHigh[ch].Prepare(osRate, kHighCrossoverHz);
      mDcBlockers[ch].SetSampleRate(sampleRate);
      mFeedbackLine[ch].assign((size_t)mFeedbackDelay, 0.0f);
   }
   Reset();
}

void SaturatorKernel::Reset()
{
   mOversampler.Reset();
   mDryOversampler.Reset();
   for (int ch = 0; ch < kMaxChannels; ch++)
   {
      for (int s = 0; s < kStages; s++)
         mAdaa[s][ch].Reset();
      mLowSplit[ch].Reset();
      mHighSplit[ch].Reset();
      mLowBandAllpass[ch].Reset();
      mDryAllpassLow[ch].Reset();
      mDryAllpassHigh[ch].Reset();
      mDcBlockers[ch].Reset();
      std::fill(mFeedbackLine[ch].begin(), mFeedbackLine[ch].end(), 0.0f);
      mFeedbackLpState[ch] = 0.0f;
   }
   mFeedbackPos = 0;
   mActiveRouting = mRouting.load(std::memory_order_relaxed);
   mSwitchGain = 1.0f;
}

void SaturatorKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{
   static const char* kModelNames[kStages] = { "model1", "model2", "model3" };
   static const char* kAmountNames[kStages] = { "amount1", "amount2", "amount3" };
   for (int s = 0; s < kStages; s++)
   {
      mModel[s].store(std::clamp((int)std::lround(node.Param(kModelNames[s])), 0, kNumModels - 1),
                      std::memory_order_relaxed);
      mMailbox.Push(kAmount1 + s, node.Param(kAmountNames[s]));
   }
   mRouting.store(std::clamp((int)std::lround(node.Param("routing")), 0, kNumRoutings - 1),
                  std::memory_order_relaxed);
   mMailbox.Push(kFeedbackAmount, node.Param("feedback"));
   mMailbox.Push(kOutputDb, node.Param("output"));
}

// One stage on one signal; `slot` picks the ADAA history (a channel, or mid
// and side). An off stage passes the signal and forgets its history.
float SaturatorKernel::RunStage(int stage, int slot, float x, const Curve& c)
{
   Adaa& a = mAdaa[stage][slot];
   if (c.model == kOff)
   {
      a.Reset();
      return x;
   }
   return a.Process(x, c);
}

float SaturatorKernel::Serial(int ch, float x, const Curve* c)
{
   for (int s = 0; s < kStages; s++)
      x = RunStage(s, ch, x, c[s]);
   return x;
}

void SaturatorKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
   const int target = mRouting.load(std::memory_order_relaxed);
   int models[kStages];
   for (int s = 0; s < kStages; s++)
      models[s] = mModel[s].load(std::memory_order_relaxed);
   const int factor = mOversampler.Factor();

   for (int i = 0; i < out.numFrames; i++)
   {
      // A different routing is a different graph: fade the wet out, start
      // the crossovers and feedback line clean, switch, fade back in. A
      // hard switch steps the output (FXAUDIT measured +31 dB).
      if (target != mActiveRouting)
      {
         mSwitchGain -= mSwitchStep;
         if (mSwitchGain <= 0.0f)
         {
            mSwitchGain = 0.0f;
            mActiveRouting = target;
            ClearRoutingState();
         }
      }
      else if (mSwitchGain < 1.0f)
         mSwitchGain = std::min(1.0f, mSwitchGain + mSwitchStep);
      const int routing = mActiveRouting;
      const bool midSide = routing == kMidSide && numChannels >= 2;

      Curve c[kStages];
      int active = 0;
      for (int s = 0; s < kStages; s++)
      {
         c[s] = Curve(models[s], mMailbox.SmoothedValue(kAmount1 + s));
         active += models[s] != kOff ? 1 : 0;
      }
      // Feedback is the loop gain at small signals: divided by the stages'
      // drive, so 0.95 stays under 1 and the loop never howls on its own
      // (a raw 0.5 at 18 dB of drive is a loop gain of 4).
      double drive = 1.0;
      for (int s = 0; s < kStages; s++)
         if (c[s].model != kOff && c[s].model != kCrush)
            drive *= c[s].gain;
      const float feedback = std::clamp(mMailbox.SmoothedValue(kFeedbackAmount), 0.0f, 0.95f) / (float)drive;
      const float outputGain = DspMath::DbToLinear(mMailbox.SmoothedValue(kOutputDb));

      float os[kMaxChannels][MinPhaseOversampler::kMaxFactor];
      for (int ch = 0; ch < numChannels; ch++)
      {
         float x = in.channels[ch][i];
         if (routing == kFeedback)
         {
            const float delayed = mFeedbackLine[ch][(size_t)mFeedbackPos];
            mFeedbackLpState[ch] = delayed + mFeedbackLpCoef * (mFeedbackLpState[ch] - delayed);
            x += feedback * mFeedbackLpState[ch];
         }
         mOversampler.Up(ch, x, os[ch]);
      }

      for (int k = 0; k < factor; k++)
      {
         if (midSide)
         {
            const float l = os[0][k], r = os[1][k];
            const float m = RunStage(0, 0, 0.5f * (l + r), c[0]);
            const float sd = RunStage(1, 1, 0.5f * (l - r), c[1]);
            os[0][k] = RunStage(2, 0, m + sd, c[2]);
            os[1][k] = RunStage(2, 1, m - sd, c[2]);
         }
         for (int ch = midSide ? 2 : 0; ch < numChannels; ch++)
         {
            float& v = os[ch][k];
            switch (routing)
            {
            case kParallel:
            {
               if (active == 0)
                  break;
               float sum = 0.0f;
               for (int s = 0; s < kStages; s++)
                  if (c[s].model != kOff)
                     sum += RunStage(s, ch, v, c[s]);
                  else
                     mAdaa[s][ch].Reset();
               v = sum / (float)active;
               break;
            }
            case kBands:
            {
               float low, rest, mid, high;
               mLowSplit[ch].Split(v, low, rest);
               mHighSplit[ch].Split(rest, mid, high);
               low = mLowBandAllpass[ch].Process(low);
               v = RunStage(0, ch, low, c[0]) + RunStage(1, ch, mid, c[1]) + RunStage(2, ch, high, c[2]);
               break;
            }
            case kMidSide:
               // Mono: side is silent, so s2 has nothing to shape.
               v = numChannels == 1 ? RunStage(2, 0, RunStage(0, 0, v, c[0]), c[2]) : Serial(ch, v, c);
               break;
            default: // serial, feedback
               v = Serial(ch, v, c);
               break;
            }
         }
      }

      for (int ch = 0; ch < numChannels; ch++)
      {
         const float y = mDcBlockers[ch].Process(mOversampler.Down(ch, os[ch]));
         if (routing == kFeedback)
            mFeedbackLine[ch][(size_t)mFeedbackPos] = y;
         out.channels[ch][i] = y * outputGain * mSwitchGain;
      }
      if (routing == kFeedback && ++mFeedbackPos >= mFeedbackDelay)
         mFeedbackPos = 0;
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

void SaturatorKernel::ClearRoutingState()
{
   for (int ch = 0; ch < kMaxChannels; ch++)
   {
      mLowSplit[ch].Reset();
      mHighSplit[ch].Reset();
      mLowBandAllpass[ch].Reset();
      mDryAllpassLow[ch].Reset();
      mDryAllpassHigh[ch].Reset();
      std::fill(mFeedbackLine[ch].begin(), mFeedbackLine[ch].end(), 0.0f);
      mFeedbackLpState[ch] = 0.0f;
   }
   mFeedbackPos = 0;
}

// The input through the wet path with every stage taken out: the same up +
// down filters, plus the crossovers' allpass in 3-band (SaturatorKernel.h).
// Not the DC blocker: at mix 0 the input comes back whole, DC included
// (DSPTEST's unity-DC check), as Drive's does.
bool SaturatorKernel::ProcessDry(const AudioBuffer& in, AudioBuffer& dry)
{
   const int numChannels = std::min(in.numChannels, std::min(dry.numChannels, kMaxChannels));
   const bool bands = mActiveRouting == kBands;
   for (int ch = 0; ch < numChannels; ch++)
      for (int i = 0; i < dry.numFrames; i++)
      {
         float os[MinPhaseOversampler::kMaxFactor];
         mDryOversampler.Up(ch, in.channels[ch][i], os);
         if (bands)
            for (int k = 0; k < mDryOversampler.Factor(); k++)
               os[k] = mDryAllpassHigh[ch].Process(mDryAllpassLow[ch].Process(os[k]));
         dry.channels[ch][i] = mDryOversampler.Down(ch, os);
      }
   for (int ch = numChannels; ch < dry.numChannels; ch++)
      std::fill(dry.channels[ch], dry.channels[ch] + dry.numFrames, 0.0f);
   return true;
}

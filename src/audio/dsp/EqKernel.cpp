#include "EqKernel.h"

#include "nodes/AudioEffectNode.h"

// Main thread only. Reads each band's raw params (type/freq/q/gain/on) and
// pushes precomputed coefficients - never freq/q themselves - so
// ProcessBlock (audio thread) never runs tan()/cos(). A disabled band pushes
// exact bypass coefficients rather than a per-sample enable branch, so the
// mailbox's smoothing crossfades it out instead of clicking.
void EqKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   static const char* const kTypeNames[EqKernel::kNumBands] =
      { "band1Type", "band2Type", "band3Type", "band4Type", "band5Type" };
   static const char* const kFreqNames[EqKernel::kNumBands] =
      { "band1Freq", "band2Freq", "band3Freq", "band4Freq", "band5Freq" };
   static const char* const kQNames[EqKernel::kNumBands] =
      { "band1Q", "band2Q", "band3Q", "band4Q", "band5Q" };
   static const char* const kGainNames[EqKernel::kNumBands] =
      { "band1Gain", "band2Gain", "band3Gain", "band4Gain", "band5Gain" };
   static const char* const kOnNames[EqKernel::kNumBands] =
      { "band1On", "band2On", "band3On", "band4On", "band5On" };

   for (int b = 0; b < kNumBands; b++)
   {
      const int type = std::clamp((int)(node.Param(kTypeNames[b]) + 0.5f), 0, EqDsp::kNumBandTypes - 1);
      const float freq = node.Param(kFreqNames[b]);
      const float q = node.Param(kQNames[b]);
      const float gainDb = node.Param(kGainNames[b]);
      const bool on = node.Param(kOnNames[b]) >= 0.5f;

      DspMath::Biquad bq;
      if (on)
         EqDsp::ConfigureBiquad(bq, type, freq, q, gainDb, sampleRate);
      else
         EqDsp::ConfigureBypass(bq);

      mMailbox.Push(b * kCoeffsPerBand + 0, bq.b0);
      mMailbox.Push(b * kCoeffsPerBand + 1, bq.b1);
      mMailbox.Push(b * kCoeffsPerBand + 2, bq.b2);
      mMailbox.Push(b * kCoeffsPerBand + 3, bq.a1);
      mMailbox.Push(b * kCoeffsPerBand + 4, bq.a2);
   }

   mMailbox.Push(kOutputGainSlot, node.Param("outputGainDb"));
}

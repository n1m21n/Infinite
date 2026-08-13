#include "AudioFilterKernel.h"

#include "nodes/AudioEffectNode.h"

// Main thread only. Reads the node's raw params (type/freq/Q/gain) and
// pushes precomputed coefficients - never freq/Q themselves - so
// ProcessBlock (audio thread) never runs tan()/cos().
void AudioFilterKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   const int type = std::clamp((int)(node.Param("type") + 0.5f), 0, AudioFilterDsp::kNumFilterTypes - 1);
   const float freq = node.Param("freq");
   const float q = node.Param("q");
   const float gainDb = node.Param("gain");

   mType.store(type, std::memory_order_relaxed);

   float coeffs[kStages][kCoeffsPerStage] = {};
   if (AudioFilterDsp::IsSvf(type))
   {
      const float g = tanf((float)M_PI * freq / (float)sampleRate);
      const float k = 1.0f / std::max(0.01f, q);
      const int stages = AudioFilterDsp::SvfStageCount(type);
      for (int s = 0; s < stages; s++)
      {
         coeffs[s][0] = g;
         coeffs[s][1] = k;
      }
   }
   else
   {
      DspMath::Biquad bq;
      AudioFilterDsp::ConfigureBiquad(bq, type, freq, q, gainDb, sampleRate);
      coeffs[0][0] = bq.b0;
      coeffs[0][1] = bq.b1;
      coeffs[0][2] = bq.b2;
      coeffs[0][3] = bq.a1;
      coeffs[0][4] = bq.a2;
   }

   for (int s = 0; s < kStages; s++)
      for (int c = 0; c < kCoeffsPerStage; c++)
         mMailbox.Push(s * kCoeffsPerStage + c, coeffs[s][c]);

   mMailbox.Push(kOutputGainSlot, node.Param("outputGainDb"));
}

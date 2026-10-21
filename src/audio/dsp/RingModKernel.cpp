#include "RingModKernel.h"

#include "AnalogPrimitives.h"
#include "nodes/AudioEffectNode.h"

void RingModKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kFreqHz, node.Param("freq"));
   mWaveform.store(std::clamp((int)(node.Param("waveform") + 0.5f), 0, (int)DspMath::kWaveSquare),
                    std::memory_order_relaxed);
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void RingModKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 8));
   const int waveform = mWaveform.load(std::memory_order_relaxed);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   const int factor = mOversampler.Factor();
   const double innerRate = mSampleRate * factor;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float freqHz = std::max(0.0f, mMailbox.SmoothedValue(kFreqHz));
      mOsc.SetFrequency(freqHz, innerRate);
      float mod[Oversampler::kMaxFactor];
      for (int k = 0; k < factor; k++)
      {
         mod[k] = mOsc.Generate(waveform);
         mOsc.Advance();
      }

      float carrierBleedScale = 0.0f;
      if (analog)
      {
         float inMax = 0.0f;
         for (int ch = 0; ch < numChannels; ch++)
            inMax = std::max(inMax, std::fabs(in.channels[ch][i]));
         const float envCoef = inMax > mEnvFollower ? 0.05f : 0.001f;
         mEnvFollower += envCoef * (inMax - mEnvFollower);
         mEnvFollower = DspMath::FlushDenormal(mEnvFollower);
         carrierBleedScale = 0.008f * std::min(1.0f, mEnvFollower * 4.0f);
      }

      for (int ch = 0; ch < numChannels; ch++)
      {
         float os[Oversampler::kMaxFactor];
         mOversampler.Up(ch, in.channels[ch][i], os);
         if (analog)
            for (int k = 0; k < factor; k++)
            {
               const float x = AnalogDsp::AsymTanh(os[k], 0.08f);
               const float ringOut = AnalogDsp::DiodeDeadZone(x * mod[k], 0.10f);
               os[k] = AnalogDsp::AsymTanh(ringOut + mod[k] * carrierBleedScale, 0.05f);
            }
         else
            for (int k = 0; k < factor; k++)
               os[k] *= mod[k];
         out.channels[ch][i] = mOversampler.Down(ch, os);
      }
   }
}

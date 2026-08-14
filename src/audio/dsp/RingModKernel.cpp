#include "RingModKernel.h"

#include "nodes/AudioEffectNode.h"

void RingModKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kFreqHz, node.Param("freq"));
   mWaveform.store(std::clamp((int)(node.Param("waveform") + 0.5f), 0, (int)DspMath::kWaveSquare),
                    std::memory_order_relaxed);
}

void RingModKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 8));
   const int waveform = mWaveform.load(std::memory_order_relaxed);

   for (int i = 0; i < out.numFrames; i++)
   {
      const float freqHz = std::max(0.0f, mMailbox.SmoothedValue(kFreqHz));
      mOsc.SetFrequency(freqHz, mSampleRate);
      const float mod = mOsc.Generate(waveform);
      mOsc.Advance();

      for (int ch = 0; ch < numChannels; ch++)
         out.channels[ch][i] = in.channels[ch][i] * mod;
   }
}

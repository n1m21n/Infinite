#include "TremoloKernel.h"

#include "core/Transport.h"
#include "nodes/AudioEffectNode.h"

void TremoloKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kDepth, node.Param("depth"));
   mMailbox.Push(kStereoPhaseDeg, node.Param("stereoPhase"));
   mShape.store(std::clamp((int)(node.Param("shape") + 0.5f), 0, 3), std::memory_order_relaxed);
   mSync.store(node.Param("sync") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mRateDiv.store(std::clamp((int)(node.Param("rateDiv") + 0.5f), 0, MusicTime::kNumRateDivisions - 1),
                  std::memory_order_relaxed);
}

void TremoloKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 8));
   const int shape = mShape.load(std::memory_order_relaxed);
   const int dspWaveform = ShapeToDspWaveform(shape);
   const bool rampDown = shape == 3;
   const bool sync = mSync.load(std::memory_order_relaxed) != 0;
   const int rateDiv = mRateDiv.load(std::memory_order_relaxed);

   for (int i = 0; i < out.numFrames; i++)
   {
      float rateHz;
      if (sync)
      {
         const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
         rateHz = (float)MusicTime::HzForRateDivision((MusicTime::RateDivision)rateDiv, bpm);
      }
      else
         rateHz = std::max(0.0f, mMailbox.SmoothedValue(kRateHz));

      const float depth = std::clamp(mMailbox.SmoothedValue(kDepth), 0.0f, 1.0f);
      const float stereoPhaseCycles = std::clamp(mMailbox.SmoothedValue(kStereoPhaseDeg), 0.0f, 180.0f) / 360.0f;

      mOsc.SetFrequency(rateHz, mSampleRate);

      float lfoL = mOsc.Generate(dspWaveform, 0.5f, mOsc.phase);
      float lfoR = mOsc.Generate(dspWaveform, 0.5f, mOsc.phase + stereoPhaseCycles);
      if (rampDown)
      {
         lfoL = -lfoL;
         lfoR = -lfoR;
      }
      mOsc.Advance();

      const float gainL = GainFromLfo(lfoL, depth);
      const float gainR = GainFromLfo(lfoR, depth);

      for (int ch = 0; ch < numChannels; ch++)
         out.channels[ch][i] = in.channels[ch][i] * (ch == 1 ? gainR : gainL);
   }
}

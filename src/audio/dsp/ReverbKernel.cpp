#include "ReverbKernel.h"

#include "nodes/AudioEffectNode.h"

// Main thread only - four continuous params, all through the mailbox for
// per-sample smoothing (no discrete selectors left now that the convolution
// engine is gone; see ReverbKernel.h's class comment).
void ReverbKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kSize, node.Param("size"));
   mMailbox.Push(kDecaySeconds, node.Param("decay"));
   mMailbox.Push(kDamping, node.Param("damping"));
   mMailbox.Push(kPredelayMs, node.Param("predelay"));
}

void ReverbKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   using namespace ReverbDsp;

   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const float rateScale = (float)(mSampleRate / 44100.0);
   const float outScale = 0.35355339059f; // 1/sqrt(kNumLines), keeps the 8-way sum near unity

   float blockDryPeak = 0.0f, blockWetPeak = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float size = std::clamp(mMailbox.SmoothedValue(kSize), 0.0f, 1.0f);
      const float decaySeconds = std::max(0.05f, mMailbox.SmoothedValue(kDecaySeconds));
      const float damping = std::clamp(mMailbox.SmoothedValue(kDamping), 0.0f, 1.0f);
      const float predelayMs = std::max(0.0f, mMailbox.SmoothedValue(kPredelayMs));

      const float inMono = numChannels >= 2 ? 0.5f * (in.channels[0][i] + in.channels[1][i]) : in.channels[0][i];

      // Predelay: a plain integer-sample read/write, no interpolation - the
      // gap it carves before the FDN's own tail begins is the whole point of
      // the visualizer's "predelay gap" (see DrawReverbVisualizer).
      const int predelaySamples = std::clamp((int)std::lround(predelayMs * 0.001f * (float)mSampleRate), 0,
                                              mPredelayCapacity - 1);
      mPredelay[(size_t)mPredelayWrite] = inMono;
      int readPos = mPredelayWrite - predelaySamples;
      readPos %= mPredelayCapacity;
      if (readPos < 0)
         readPos += mPredelayCapacity;
      const float predelayed = mPredelay[(size_t)readPos];
      mPredelayWrite++;
      if (mPredelayWrite >= mPredelayCapacity)
         mPredelayWrite = 0;

      const float diffused = mDiffuser[1].Process(mDiffuser[0].Process(predelayed));

      // `size` scales every line's active delay length between 15% and 100%
      // of its allocated (size=1) capacity - see FdnLine::ReadAtDelay's
      // comment for why shrinking the active length mid-stream is safe
      // without a buffer clear or reallocation.
      const float scaleFactor = 0.15f + 0.85f * size;

      float delayedOut[kNumLines];
      int activeLen[kNumLines];
      for (int line = 0; line < kNumLines; line++)
      {
         activeLen[line] = std::clamp((int)std::lround(kBaseLengths44k[line] * rateScale * scaleFactor), 8,
                                       mLines[line].capacity);
         delayedOut[line] = mLines[line].ReadAtDelay(activeLen[line]);
      }

      float mixed[kNumLines];
      HadamardMix8(delayedOut, mixed);

      // Damping cutoff: damping=0 leaves the feedback path essentially flat
      // (18kHz, above audibility of the shelving itself), damping=1 pulls it
      // down to 800Hz for an audibly darker, faster-decaying tail.
      const float cutoffHz = 18000.0f - damping * 17200.0f;
      const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * cutoffHz / (float)mSampleRate);

      for (int line = 0; line < kNumLines; line++)
      {
         // 10^(-3*L/(fs*RT60)): the per-line loop gain that brings this
         // line's contribution to -60dB after `decaySeconds` (Jot & Chaigne)
         // - see ReverbKernel.h's class comment for the derivation.
         const float decayGain =
            std::pow(10.0f, -3.0f * (float)activeLen[line] / ((float)mSampleRate * decaySeconds));
         const float fb = mixed[line] * decayGain;

         FdnLine& l = mLines[line];
         l.dampState = FlushDenormal(l.dampState + dampCoeff * (fb - l.dampState));

         const float inject = diffused * (line < 4 ? 0.5f : -0.5f);
         l.Write(FlushDenormal(inject + l.dampState));
      }

      // Cheap fixed stereo spread (no width control in Tier 1): even lines
      // biased left, odd lines biased right.
      float sumEven = 0.0f, sumOdd = 0.0f;
      for (int line = 0; line < kNumLines; line++)
      {
         if (line % 2 == 0)
            sumEven += delayedOut[line];
         else
            sumOdd += delayedOut[line];
      }
      const float wetL = (sumEven + 0.6f * sumOdd) * outScale;
      const float wetR = (sumOdd + 0.6f * sumEven) * outScale;

      out.channels[0][i] = wetL;
      if (numChannels >= 2)
         out.channels[1][i] = wetR;
      for (int ch = 2; ch < numChannels; ch++)
         out.channels[ch][i] = 0.0f;

      for (int ch = 0; ch < numChannels; ch++)
      {
         blockDryPeak = std::max(blockDryPeak, std::fabs(in.channels[ch][i]));
         blockWetPeak = std::max(blockWetPeak, std::fabs(out.channels[ch][i]));
      }
   }

   const float payload[2] = { blockDryPeak, blockWetPeak };
   mLevelMeter.Write(payload, 2);
}

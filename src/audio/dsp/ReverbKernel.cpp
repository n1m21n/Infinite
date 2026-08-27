#include "ReverbKernel.h"

#include "nodes/AudioEffectNode.h"

void ReverbKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kSize, node.Param("size"));
   mMailbox.Push(kDecaySeconds, node.Param("decay"));
   mMailbox.Push(kDamping, node.Param("damping"));
   mMailbox.Push(kPredelayMs, node.Param("predelay"));
   mMailbox.Push(kWidth, node.Param("width"));
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void ReverbKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   using namespace ReverbDsp;

   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const float rateScale = (float)(mSampleRate / 44100.0);
   const float outScale = 0.35355339059f; // 1/sqrt(kNumLines), keeps the 8-way sum near unity
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   // Prime-detuned LFO rates for analog tank modulation
   static const float kLfoRates[kNumLines] = { 0.29f, 0.41f, 0.53f, 0.67f, 0.37f, 0.47f, 0.73f, 0.83f };

   float blockDryPeak = 0.0f, blockWetPeak = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float size = std::clamp(mMailbox.SmoothedValue(kSize), 0.0f, 1.0f);
      const float decaySeconds = std::max(0.05f, mMailbox.SmoothedValue(kDecaySeconds));
      const float damping = std::clamp(mMailbox.SmoothedValue(kDamping), 0.0f, 1.0f);
      const float predelayMs = std::max(0.0f, mMailbox.SmoothedValue(kPredelayMs));
      const float width = std::clamp(mMailbox.SmoothedValue(kWidth), 0.0f, 1.0f);

      const float inMono = numChannels >= 2 ? 0.5f * (in.channels[0][i] + in.channels[1][i]) : in.channels[0][i];

      // Predelay: a plain integer-sample read/write, no interpolation
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
      const float scaleFactor = 0.15f + 0.85f * size;

      float delayedOut[kNumLines];
      int activeLen[kNumLines];

      if (analog)
      {
         const float diffusedIn = AnalogDsp::AsymTanh(diffused, 0.12f);
         mInputEnv += (std::fabs(inMono) - mInputEnv) * 0.001f;
         mInputEnv = DspMath::FlushDenormal(mInputEnv);

         const float dynamicAir = std::clamp(mInputEnv * 4.0f, 0.0f, 1.0f);
         const float cutoffHz = std::max(600.0f, (18000.0f - damping * 17200.0f) * (1.0f - 0.20f * (1.0f - dynamicAir)));
         const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * cutoffHz / (float)mSampleRate);

         for (int line = 0; line < kNumLines; line++)
         {
            activeLen[line] = std::clamp((int)std::lround(kBaseLengths44k[line] * rateScale * scaleFactor), 8,
                                          mLines[line].capacity - 32);
            const float lfoVal = mLfo[line].Advance(kLfoRates[line], 0.25f, 0.08f, mSampleRate);
            const float modDelay = std::clamp((float)activeLen[line] + lfoVal * 10.0f, 4.0f, (float)(mLines[line].capacity - 4));
            delayedOut[line] = mLines[line].Read(modDelay);
         }

         float mixed[kNumLines];
         HadamardMix8(delayedOut, mixed);

         for (int line = 0; line < kNumLines; line++)
         {
            const float decayGain =
               std::pow(10.0f, -3.0f * (float)activeLen[line] / ((float)mSampleRate * decaySeconds));
            const float fb = mixed[line] * decayGain;

            FdnLine& l = mLines[line];
            l.dampState = FlushDenormal(l.dampState + dampCoeff * (fb - l.dampState));

            const float inject = diffusedIn * (line < 4 ? 0.5f : -0.5f);
            l.Write(FlushDenormal(inject + l.dampState));
         }
      }
      else
      {
         for (int line = 0; line < kNumLines; line++)
         {
            activeLen[line] = std::clamp((int)std::lround(kBaseLengths44k[line] * rateScale * scaleFactor), 8,
                                          mLines[line].capacity);
            delayedOut[line] = mLines[line].ReadAtDelay(activeLen[line]);
         }

         float mixed[kNumLines];
         HadamardMix8(delayedOut, mixed);

         const float cutoffHz = 18000.0f - damping * 17200.0f;
         const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * cutoffHz / (float)mSampleRate);

         for (int line = 0; line < kNumLines; line++)
         {
            const float decayGain =
               std::pow(10.0f, -3.0f * (float)activeLen[line] / ((float)mSampleRate * decaySeconds));
            const float fb = mixed[line] * decayGain;

            FdnLine& l = mLines[line];
            l.dampState = FlushDenormal(l.dampState + dampCoeff * (fb - l.dampState));

            const float inject = diffused * (line < 4 ? 0.5f : -0.5f);
            l.Write(FlushDenormal(inject + l.dampState));
         }
      }

      // Stereo spread
      float sumEven = 0.0f, sumOdd = 0.0f;
      for (int line = 0; line < kNumLines; line++)
      {
         if (line % 2 == 0)
            sumEven += delayedOut[line];
         else
            sumOdd += delayedOut[line];
      }
      const float crossGain = 1.0f - width * 0.4f;
      const float wetL = (sumEven + crossGain * sumOdd) * outScale;
      const float wetR = (sumOdd + crossGain * sumEven) * outScale;

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

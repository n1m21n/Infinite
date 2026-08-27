#include "PitchShiftKernel.h"

#include "nodes/AudioEffectNode.h"

void PitchShiftKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kSemitones, node.Param("pitch"));
   mMailbox.Push(kGrainMs, node.Param("grain"));
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void PitchShiftKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float semitones = mMailbox.SmoothedValue(kSemitones);
      const float grainMs = std::clamp(mMailbox.SmoothedValue(kGrainMs), 10.0f, kMaxGrainMs);
      const float grainSamples = grainMs * 0.001f * (float)mSampleRate;
      const float ratio = powf(2.0f, semitones / 12.0f);
      const float delta = 1.0f - ratio;

      if (!mTapsInitialized)
      {
         mTap0 = 1.0f;
         mTap1 = grainSamples * 0.5f;
         mTapsInitialized = true;
      }

      mTap0 += delta;
      if (mTap0 < 1.0f)
         mTap0 += grainSamples;
      else if (mTap0 >= grainSamples)
         mTap0 -= grainSamples;

      mTap1 += delta;
      if (mTap1 < 1.0f)
         mTap1 += grainSamples;
      else if (mTap1 >= grainSamples)
         mTap1 -= grainSamples;

      const float w0 = 1.0f - std::fabs(2.0f * (mTap0 / grainSamples) - 1.0f);
      const float w1 = 1.0f - std::fabs(2.0f * (mTap1 / grainSamples) - 1.0f);

      const float inL = in.channels[0][i];
      const float inR = numChannels >= 2 ? in.channels[1][i] : inL;
      mLineL.Write(inL);
      mLineR.Write(inR);

      if (analog)
      {
         mJitterPhase += 17.3f / (float)mSampleRate;
         if (mJitterPhase >= 1.0f)
            mJitterPhase -= 1.0f;
         const float jitter = sinf(mJitterPhase * 2.0f * (float)M_PI) * 0.35f;

         const float readTap0 = std::max(1.0f, mTap0 + jitter);
         const float readTap1 = std::max(1.0f, mTap1 + jitter);

         float outL = mLineL.Read(readTap0) * w0 + mLineL.Read(readTap1) * w1;
         float outR = mLineR.Read(readTap0) * w0 + mLineR.Read(readTap1) * w1;

         // 4th-order 9kHz lowpass reconstruction filter
         outL = mFilterL[0].Process(outL).low;
         outL = mFilterL[1].Process(outL).low;
         outR = mFilterR[0].Process(outR).low;
         outR = mFilterR[1].Process(outR).low;

         // Harmonic grain saturation
         outL = AnalogDsp::AsymTanh(outL, 0.12f);
         outR = AnalogDsp::AsymTanh(outR, 0.12f);

         out.channels[0][i] = outL;
         if (numChannels >= 2)
            out.channels[1][i] = outR;
      }
      else
      {
         const float outL = mLineL.Read(mTap0) * w0 + mLineL.Read(mTap1) * w1;
         const float outR = mLineR.Read(mTap0) * w0 + mLineR.Read(mTap1) * w1;

         out.channels[0][i] = outL;
         if (numChannels >= 2)
            out.channels[1][i] = outR;
      }

      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

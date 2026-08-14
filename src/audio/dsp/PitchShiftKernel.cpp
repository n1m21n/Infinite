#include "PitchShiftKernel.h"

#include "nodes/AudioEffectNode.h"

void PitchShiftKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kSemitones, node.Param("pitch"));
   mMailbox.Push(kGrainMs, node.Param("grain"));
}

void PitchShiftKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));

   for (int i = 0; i < out.numFrames; i++)
   {
      const float semitones = mMailbox.SmoothedValue(kSemitones);
      const float grainMs = std::clamp(mMailbox.SmoothedValue(kGrainMs), 10.0f, kMaxGrainMs);
      const float grainSamples = grainMs * 0.001f * (float)mSampleRate;
      const float ratio = powf(2.0f, semitones / 12.0f);
      const float delta = 1.0f - ratio;

      if (!mTapsInitialized)
      {
         // Half a grain apart, so the two triangle windows are a half-cycle
         // out of phase and sum to a constant 1 everywhere - without this,
         // both taps start in lockstep and briefly double the gain until
         // their first wrap re-separates them.
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

      const float outL = mLineL.Read(mTap0) * w0 + mLineL.Read(mTap1) * w1;
      const float outR = mLineR.Read(mTap0) * w0 + mLineR.Read(mTap1) * w1;

      out.channels[0][i] = outL;
      if (numChannels >= 2)
         out.channels[1][i] = outR;
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

#include "TransientShaperKernel.h"

#include "nodes/AudioEffectNode.h"

void TransientShaperKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kAttackDb, node.Param("attack"));
   mMailbox.Push(kSustainDb, node.Param("sustain"));
}

void TransientShaperKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 8));

   const float fastAtk = expf(-1.0f / (0.0005f * (float)mSampleRate));  // 0.5ms
   const float fastRel = expf(-1.0f / (0.030f * (float)mSampleRate));   // 30ms
   const float slowAtk = expf(-1.0f / (0.030f * (float)mSampleRate));   // 30ms
   const float slowRel = expf(-1.0f / (0.300f * (float)mSampleRate));   // 300ms

   float blockTransient = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float attackDb = mMailbox.SmoothedValue(kAttackDb);
      const float sustainDb = mMailbox.SmoothedValue(kSustainDb);

      float peak = 0.0f;
      for (int ch = 0; ch < numChannels; ch++)
         peak = std::max(peak, std::fabs(in.channels[ch][i]));

      mFastEnv = peak > mFastEnv ? peak + (mFastEnv - peak) * fastAtk : peak + (mFastEnv - peak) * fastRel;
      mSlowEnv = peak > mSlowEnv ? peak + (mSlowEnv - peak) * slowAtk : peak + (mSlowEnv - peak) * slowRel;

      // How far the fast (instantaneous) envelope sits above the slow
      // (sustained) one, normalized by the sustained level itself so a
      // quiet passage's transients are weighted the same as a loud one's.
      const float transientAmount = std::clamp((mFastEnv - mSlowEnv) / (mSlowEnv + 0.01f), 0.0f, 1.0f);
      blockTransient = std::max(blockTransient, transientAmount);

      const float gainDb = attackDb * transientAmount + sustainDb * (1.0f - transientAmount);
      const float gain = DspMath::DbToLinear(gainDb);

      for (int ch = 0; ch < numChannels; ch++)
         out.channels[ch][i] = in.channels[ch][i] * gain;
   }

   mTransientMeter.Write(&blockTransient, 1);
}

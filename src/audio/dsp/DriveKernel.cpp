#include "DriveKernel.h"

#include "nodes/AudioEffectNode.h"

void DriveKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   mMailbox.Push(kDriveDb, node.Param("drive"));
   mMailbox.Push(kBias, node.Param("bias"));
   mMailbox.Push(kTone, node.Param("tone"));
   mMailbox.Push(kColor, node.Param("color"));
   mMailbox.Push(kOutputDb, node.Param("output"));
}

void DriveKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));

   for (int i = 0; i < out.numFrames; i++)
   {
      const float driveDb = mMailbox.SmoothedValue(kDriveDb);
      const float bias = mMailbox.SmoothedValue(kBias);
      const float tone = mMailbox.SmoothedValue(kTone);
      const float color = std::clamp(mMailbox.SmoothedValue(kColor), 0.0f, 1.0f);
      const float outputGain = DspMath::DbToLinear(mMailbox.SmoothedValue(kOutputDb));

      for (int ch = 0; ch < numChannels; ch++)
      {
         float x = in.channels[ch][i];
         x = DriveDsp::Shape(x, driveDb, bias, color);
         x = mDcBlockers[ch].Process(x);
         x = mToneFilters[ch].Process(x, tone, (float)mSampleRate);
         out.channels[ch][i] = x * outputGain;
      }
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

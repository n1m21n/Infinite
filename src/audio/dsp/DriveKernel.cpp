#include "DriveKernel.h"

#include "nodes/AudioEffectNode.h"

// The sample rate comes from PrepareToPlay, never from here: writing it
// from the main thread raced the audio thread's read.
void DriveKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{
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

      const DriveDsp::ShapeParams shape(driveDb, bias, color);
      for (int ch = 0; ch < numChannels; ch++)
      {
         float os[MinPhaseOversampler::kMaxFactor];
         mOversampler.Up(ch, in.channels[ch][i], os);
         for (int k = 0; k < mOversampler.Factor(); k++)
            os[k] = mAdaa[ch].Process(os[k], shape);
         float x = mOversampler.Down(ch, os);
         x = mDcBlockers[ch].Process(x);
         x = mToneFilters[ch].Process(x, tone, (float)mSampleRate);
         out.channels[ch][i] = x * outputGain;
      }
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

// The input through the same up + down filters as the wet path, with no
// curve between them: what the mix has to blend against (DriveKernel.h).
bool DriveKernel::ProcessDry(const AudioBuffer& in, AudioBuffer& dry)
{
   const int numChannels = std::min(in.numChannels, std::min(dry.numChannels, kMaxChannels));
   for (int ch = 0; ch < numChannels; ch++)
      for (int i = 0; i < dry.numFrames; i++)
      {
         float os[MinPhaseOversampler::kMaxFactor];
         mDryOversampler.Up(ch, in.channels[ch][i], os);
         dry.channels[ch][i] = mDryOversampler.Down(ch, os);
      }
   for (int ch = numChannels; ch < dry.numChannels; ch++)
      std::fill(dry.channels[ch], dry.channels[ch] + dry.numFrames, 0.0f);
   return true;
}

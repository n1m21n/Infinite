#include "StereoKernel.h"

#include "nodes/AudioEffectNode.h"

void StereoKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kWidth, node.Param("width"));
   mMailbox.Push(kPan, node.Param("pan"));
   mMailbox.Push(kBassMonoHz, node.Param("bassMono"));
}

void StereoKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   if (in.numChannels < 2 || out.numChannels < 2)
   {
      // Mono passthrough - nothing to stereo-process.
      for (int i = 0; i < out.numFrames; i++)
         out.channels[0][i] = in.channels[0][i];
      return;
   }

   float blockCorrSum = 0.0f, blockL2 = 0.0f, blockR2 = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float width = std::max(0.0f, mMailbox.SmoothedValue(kWidth));
      const float pan = std::clamp(mMailbox.SmoothedValue(kPan), -1.0f, 1.0f);
      const float bassMonoHz = std::max(0.0f, mMailbox.SmoothedValue(kBassMonoHz));

      float l = in.channels[0][i];
      float r = in.channels[1][i];

      // Bass-mono fold: below `bassMonoHz`, force the two channels to their
      // shared low band; above it, each channel keeps its own high band.
      // 0 Hz means "off" - both SVFs sit at their reset (silent) low output
      // and every high output equals the dry signal.
      if (bassMonoHz > 0.0f)
      {
         mSvfL.SetSampleRate(mSampleRate);
         mSvfL.SetCutoff(bassMonoHz, 0.707f);
         mSvfR.SetSampleRate(mSampleRate);
         mSvfR.SetCutoff(bassMonoHz, 0.707f);
         const DspMath::TptSvf::Outputs oL = mSvfL.Process(l);
         const DspMath::TptSvf::Outputs oR = mSvfR.Process(r);
         const float lowMid = 0.5f * (oL.low + oR.low);
         l = oL.high + lowMid;
         r = oR.high + lowMid;
      }

      const float mid = 0.5f * (l + r);
      const float side = 0.5f * (l - r) * width;
      float outL = mid + side;
      float outR = mid - side;

      float leftGain, rightGain;
      DspMath::EqualPowerPan(pan, leftGain, rightGain);
      outL *= 2.0f * leftGain;
      outR *= 2.0f * rightGain;

      out.channels[0][i] = outL;
      out.channels[1][i] = outR;
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;

      blockCorrSum += l * r;
      blockL2 += l * l;
      blockR2 += r * r;
   }

   const float denom = sqrtf(std::max(1e-9f, blockL2 * blockR2));
   const float corr = std::clamp(blockCorrSum / denom, -1.0f, 1.0f);
   mCorrMeter.Write(&corr, 1);
}

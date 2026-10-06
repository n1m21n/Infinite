#include "WavetableShaperKernel.h"

#include "nodes/AudioEffectNode.h"

void WavetableShaperKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   mMailbox.Push(kTable, node.Param("table"));
   mMailbox.Push(kPosition, node.Param("position"));
   mMailbox.Push(kDrive, node.Param("drive"));
   mMailbox.Push(kBias, node.Param("bias"));
   mMailbox.Push(kSmooth, node.Param("smooth"));
   mMailbox.Push(kOutputDb, node.Param("output"));
   mMailbox.Push(kStereo, node.Param("stereo"));
}

void WavetableShaperKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
   float lastInput = 0.0f;

   for (int i = 0; i < out.numFrames; i++)
   {
      const int table = (int)lroundf(mMailbox.SmoothedValue(kTable));
      const float position = mMailbox.SmoothedValue(kPosition);
      const float driveDb = mMailbox.SmoothedValue(kDrive);
      const float bias = mMailbox.SmoothedValue(kBias);
      const float smooth = mMailbox.SmoothedValue(kSmooth);
      const float outputGain = DspMath::DbToLinear(mMailbox.SmoothedValue(kOutputDb));
      const float stereo = mMailbox.SmoothedValue(kStereo);

      // L reads position - stereo*0.5, R reads position + stereo*0.5; both
      // land on the same frame at stereo=0. Shape()/RawShape() already
      // clamp position to [0,1] internally, so no separate wrap/clamp policy
      // is invented here. Channels beyond L/R (rare, up to kMaxChannels) fall
      // back to the L curve, the same "ch==1 only" precedent FrequencyShifterKernel
      // and TremoloKernel use for their own stereo spread.
      const float positionL = position - stereo * 0.5f;
      const float positionR = position + stereo * 0.5f;

      if (numChannels > 0)
         lastInput = in.channels[0][i];

      for (int ch = 0; ch < numChannels; ch++)
      {
         const float chPosition = (ch == 1) ? positionR : positionL;
         float x = in.channels[ch][i];
         x = WavetableShaperDsp::Shape(x, table, chPosition, driveDb, bias, smooth);
         x = mDcBlockers[ch].Process(x);
         out.channels[ch][i] = x * outputGain;
      }
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }

   const float payload[1] = { lastInput }; // visualizer's live operating-point dot
   mLevelMeter.Write(payload, 1);
}

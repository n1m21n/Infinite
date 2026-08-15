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

      if (numChannels > 0)
         lastInput = in.channels[0][i];

      for (int ch = 0; ch < numChannels; ch++)
      {
         float x = in.channels[ch][i];
         x = WavetableShaperDsp::Shape(x, table, position, driveDb, bias, smooth);
         x = mDcBlockers[ch].Process(x);
         out.channels[ch][i] = x * outputGain;
      }
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }

   const float payload[1] = { lastInput }; // visualizer's live operating-point dot
   mLevelMeter.Write(payload, 1);
}

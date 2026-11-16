#include "WavetableShaperKernel.h"

#include "nodes/AudioEffectNode.h"

// The sample rate comes from PrepareToPlay, never from here: writing it
// from the main thread raced the audio thread's read.
void WavetableShaperKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{
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
      const float outputDb = mMailbox.SmoothedValue(kOutputDb);
      if (!(outputDb == mCachedOutputDb))
      {
         mCachedOutputDb = outputDb;
         mCachedOutputGain = DspMath::DbToLinear(outputDb);
      }
      const float outputGain = mCachedOutputGain;
      const float stereo = mMailbox.SmoothedValue(kStereo);

      if (!(driveDb == mCachedDriveDb))
      {
         mCachedDriveDb = driveDb;
         mCachedDriveLin = DspMath::DbToLinear(driveDb);
      }
      const double driveLin = (double)mCachedDriveLin;

      if (!(smooth == mCachedSmooth))
      {
         mCachedSmooth = smooth;
         mCachedSmoothMip = WavetableShaperDsp::SmoothToMip(smooth);
         mCachedSlewThresh = std::exp2((double)mCachedSmoothMip) / (2.0 * Wavetable::kMaxHarmonic);
      }
      const float smoothMip = mCachedSmoothMip;
      const double slewThresh = mCachedSlewThresh;

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

      const double phaseZero = 0.5 + 0.5 * (double)bias * driveLin;
      if (table != mCachedZeroTable || !(positionL == mCachedZeroPosL) || !(positionR == mCachedZeroPosR) ||
          !(smoothMip == mCachedZeroSmoothMip) || !(phaseZero == mCachedZeroPhase))
      {
         mCachedZeroTable = table;
         mCachedZeroPosL = positionL;
         mCachedZeroPosR = positionR;
         mCachedZeroSmoothMip = smoothMip;
         mCachedZeroPhase = phaseZero;
         mCachedZeroRefL = WavetableShaperDsp::ReadAtMip(phaseZero, table, positionL, smoothMip);
         mCachedZeroRefR = (positionR == positionL)
            ? mCachedZeroRefL
            : WavetableShaperDsp::ReadAtMip(phaseZero, table, positionR, smoothMip);
      }

      for (int ch = 0; ch < numChannels; ch++)
      {
         const float chPosition = (ch == 1) ? positionR : positionL;
         // Shape()'s zero reference, at the knob's own mip level.
         const float zeroRef = (ch == 1) ? mCachedZeroRefR : mCachedZeroRefL;
         float os[MinPhaseOversampler::kMaxFactor];
         mOversampler.Up(ch, in.channels[ch][i], os);
         for (int k = 0; k < mOversampler.Factor(); k++)
         {
            const double phase = 0.5 + 0.5 * (double)(os[k] + bias) * driveLin;
            const double phaseSpeed = std::fabs(phase - mPrevPhase[ch]);
            mPrevPhase[ch] = phase;
            const float mipPos = (phaseSpeed > slewThresh)
               ? std::max(smoothMip, WavetableShaperDsp::MipForPhaseSpeed(phaseSpeed))
               : smoothMip;
            // Negated, as in WavetableShaperDsp::RawShape.
            os[k] = zeroRef - WavetableShaperDsp::ReadAtMip(phase, table, chPosition, mipPos);
         }
         float x = mOversampler.Down(ch, os);
         x = mDcBlockers[ch].Process(x);
         out.channels[ch][i] = x * outputGain;
      }
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }

   const float payload[1] = { lastInput }; // visualizer's live operating-point dot
   mLevelMeter.Write(payload, 1);
}

bool WavetableShaperKernel::ProcessDry(const AudioBuffer& in, AudioBuffer& dry)
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

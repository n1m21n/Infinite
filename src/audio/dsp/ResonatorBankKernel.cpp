#include "ResonatorBankKernel.h"

#include "nodes/AudioEffectNode.h"

void ResonatorBankKernel::PrepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
   mSampleRate = sampleRate;

   uint32_t state = 0x853c49e6u;
   for (int i = 0; i < kMaxPoles; i++)
   {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      mScatterOffsets[i] = (float)(state & 0x00FFFFFFu) / 8388608.0f - 1.0f;
   }

   for (int p = 0; p < kMaxPoles; p++)
   {
      mSvf[p].SetSampleRate(sampleRate);
   }

   Reset();
}

void ResonatorBankKernel::Reset()
{
   for (int p = 0; p < kMaxPoles; p++)
      mSvf[p].Reset();
   mFirstBlock = true;
}

void ResonatorBankKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   const float rootFreq = std::clamp(node.Param("rootFreq"), 20.0f, 2000.0f);
   const int structure = std::clamp((int)std::round(node.Param("structure")), 0, 3);
   const int numPolesRequested = std::clamp((int)std::round(node.Param("poles")), 1, 16);
   const float decay = std::clamp(node.Param("decay"), 0.05f, 10.0f);
   const float scatter = std::clamp(node.Param("scatter"), 0.0f, 1.0f);
   const float spread = std::clamp(node.Param("spread"), 0.0f, 1.0f);
   const bool analog = node.Param("analog") > 0.5f;

   // If pole count increased, mark newly activated poles for audio-thread reset
   if (numPolesRequested > mLastPoles)
   {
      uint32_t mask = 0;
      for (int p = mLastPoles; p < numPolesRequested; p++)
         mask |= (1u << p);
      mResetMask.fetch_or(mask, std::memory_order_release);
   }
   mLastPoles = numPolesRequested;

   PoleData tempPoles[kMaxPoles];
   for (int i = 0; i < numPolesRequested; i++)
   {
      float f_i = rootFreq;
      switch (structure)
      {
         case 0: // Harmonic
            f_i = rootFreq * (float)(i + 1);
            break;
         case 1: // Odd
            f_i = rootFreq * (float)(2 * i + 1);
            break;
         case 2: // Chord
         {
            static const int kChordSet[6] = { 0, 7, 12, 16, 19, 24 };
            const int s_i = kChordSet[i % 6] + 12 * (i / 6);
            f_i = rootFreq * powf(2.0f, (float)s_i / 12.0f);
            break;
         }
         case 3: // Metallic
         default:
            f_i = rootFreq * (float)(i + 1) * sqrtf(1.0f + 0.001f * (float)((i + 1) * (i + 1)));
            break;
      }

      f_i *= 1.0f + scatter * mScatterOffsets[i] * 0.05f;
      f_i = std::clamp(f_i, 20.0f, (float)(0.45 * sampleRate));

      const float q_i = std::clamp(0.4547f * f_i * decay, 0.5f, 500.0f);

      const float pos = (numPolesRequested > 1)
         ? ((float)i / (float)(numPolesRequested - 1) - 0.5f) * spread + 0.5f
         : 0.5f;
      float panL = 0.707f, panR = 0.707f;
      DspMath::EqualPowerPan(pos * 2.0f - 1.0f, panL, panR);

      const float g_i = tanf((float)M_PI * f_i / (float)sampleRate);
      const float k_i = 1.0f / q_i;

      tempPoles[i] = { g_i, k_i, panL, panR };
   }

   // Energy-normalise for pole count only (uncorrelated bandpass sums add in
   // quadrature). DspMath::TptSvf's band output has unity peak gain at
   // resonance independent of Q - that's the point of the TPT topology - so
   // there is nothing for a Q-dependent term to compensate for; a qMean-based
   // factor here only cuts level on exactly the high-decay/high-Q settings
   // that make a resonator interesting (at this node's own defaults - 8
   // poles, 2.5s decay - it used to attenuate by -28 dB before the mix knob
   // even gets involved). Matches how Ableton's Resonators and modal-synth
   // devices like Rings keep output level decay/Q-independent.
   const float gainScale = 1.0f / sqrtf((float)numPolesRequested);

   // Seqlock write
   mSeq.fetch_add(1, std::memory_order_release); // now odd
   memcpy(mPolesTarget, tempPoles, sizeof(tempPoles));
   mActivePolesTarget = numPolesRequested;
   mGainScaleTarget = gainScale;
   mAnalogTarget = analog ? 1 : 0;
   mSeq.fetch_add(1, std::memory_order_release); // now even
}

void ResonatorBankKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   if (out.numFrames <= 0)
      return;

   // Check if any newly activated poles need state reset
   const uint32_t resetMask = mResetMask.exchange(0, std::memory_order_acq_rel);
   if (resetMask != 0)
   {
      for (int p = 0; p < kMaxPoles; p++)
      {
         if (resetMask & (1u << p))
            mSvf[p].Reset();
      }
   }

   // Seqlock read
   uint32_t s0, s1;
   PoleData localTargets[kMaxPoles];
   int localActivePoles = mCurrentActivePoles;
   float localGainScale = mCurrentGainScale;
   int localAnalog = mCurrentAnalog;

   do {
      s0 = mSeq.load(std::memory_order_acquire);
      if (s0 & 1u)
      {
         // Mid-write: keep last snapshot
         memcpy(localTargets, mLastSnapshot, sizeof(localTargets));
         break;
      }
      memcpy(localTargets, mPolesTarget, sizeof(localTargets));
      localActivePoles = mActivePolesTarget;
      localGainScale = mGainScaleTarget;
      localAnalog = mAnalogTarget;
      s1 = mSeq.load(std::memory_order_acquire);
   } while (s0 != s1);

   memcpy(mLastSnapshot, localTargets, sizeof(localTargets));
   mCurrentActivePoles = localActivePoles;
   mCurrentAnalog = localAnalog;

   // Handle initial block or jump straight to target on new poles
   if (mFirstBlock)
   {
      memcpy(mCurrent, localTargets, sizeof(mCurrent));
      mCurrentGainScale = localGainScale;
      mFirstBlock = false;
   }

   // If resetMask was set, snap newly activated poles directly
   if (resetMask != 0)
   {
      for (int p = 0; p < kMaxPoles; p++)
      {
         if (resetMask & (1u << p))
            mCurrent[p] = localTargets[p];
      }
   }

   // Compute per-sample ramp step
   PoleData step[kMaxPoles];
   const float inv = 1.0f / (float)out.numFrames;
   for (int p = 0; p < localActivePoles; p++)
   {
      step[p].g    = (localTargets[p].g    - mCurrent[p].g)    * inv;
      step[p].k    = (localTargets[p].k    - mCurrent[p].k)    * inv;
      step[p].panL = (localTargets[p].panL - mCurrent[p].panL) * inv;
      step[p].panR = (localTargets[p].panR - mCurrent[p].panR) * inv;
   }
   const float gainStep = (localGainScale - mCurrentGainScale) * inv;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float x = (in.numChannels > 1)
         ? (in.channels[0][i] + in.channels[1][i]) * 0.5f
         : (in.numChannels > 0 ? in.channels[0][i] : 0.0f);

      float sumL = 0.0f;
      float sumR = 0.0f;

      for (int p = 0; p < localActivePoles; p++)
      {
         mCurrent[p].g    += step[p].g;
         mCurrent[p].k    += step[p].k;
         mCurrent[p].panL += step[p].panL;
         mCurrent[p].panR += step[p].panR;

         mSvf[p].g = mCurrent[p].g;
         mSvf[p].k = mCurrent[p].k;

         const float b = mSvf[p].Process(x).band;
         sumL += b * mCurrent[p].panL;
         sumR += b * mCurrent[p].panR;
      }

      mCurrentGainScale += gainStep;
      sumL *= mCurrentGainScale;
      sumR *= mCurrentGainScale;

      if (localAnalog)
      {
         sumL = AnalogDsp::AsymTanh(sumL);
         sumR = AnalogDsp::AsymTanh(sumR);
      }

      if (out.numChannels > 0)
         out.channels[0][i] = DspMath::FlushDenormal(sumL);
      if (out.numChannels > 1)
         out.channels[1][i] = DspMath::FlushDenormal(sumR);
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

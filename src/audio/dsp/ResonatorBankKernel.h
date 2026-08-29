#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/dsp/AnalogPrimitives.h"

// Resonator Bank: a bank of up to 16 parallel bandpass resonators (DspMath::TptSvf)
// excited by the incoming audio signal.
class AudioEffectNode;

class ResonatorBankKernel : public IEffectKernel
{
public:
   static constexpr int kMaxPoles = 16;

   struct PoleData
   {
      float g = 0.0f;
      float k = 1.0f;
      float panL = 0.707f;
      float panR = 0.707f;
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override;
   void Reset() override;
   void PushParams(const AudioEffectNode& node, double sampleRate) override;
   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   double mSampleRate = 44100.0;
   float mScatterOffsets[kMaxPoles] = {};

   // Seqlock-protected parameter snapshot
   std::atomic<uint32_t> mSeq { 0 };
   PoleData mPolesTarget[kMaxPoles] = {};
   int mActivePolesTarget = 8;
   float mGainScaleTarget = 1.0f;
   int mAnalogTarget = 0;

   // Pole reset tracking
   int mLastPoles = 8;
   std::atomic<uint32_t> mResetMask { 0 };

   // Audio thread ramp state
   bool mFirstBlock = true;
   PoleData mCurrent[kMaxPoles] = {};
   float mCurrentGainScale = 1.0f;
   PoleData mLastSnapshot[kMaxPoles] = {};
   int mCurrentActivePoles = 8;
   int mCurrentAnalog = 0;

   DspMath::TptSvf mSvf[kMaxPoles];
};

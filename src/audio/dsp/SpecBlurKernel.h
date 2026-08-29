#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/dsp/AnalogPrimitives.h"
#include "audio/dsp/PortableFft.h"

// Spec Blur: streaming STFT phase vocoder (N=2048, hop=512) with per-bin
// leaky magnitude integration, spectral tilt, phase diffusion and freeze.
class AudioEffectNode;

class SpecBlurKernel : public IEffectKernel
{
public:
   static constexpr int kFftSize = 2048;
   static constexpr int kLog2Fft = 11;
   static constexpr int kHopSize = 512;
   static constexpr int kNumBins = kFftSize / 2; // 1024
   static constexpr int kMaxChannels = 2;

   struct ChannelState
   {
      std::vector<float> inputFifo;
      std::vector<float> outputOla;
      std::vector<float> magState;
      std::vector<float> phasePrev;
      std::vector<float> phaseSynth;
      std::vector<float> deltaPhiFrozen;
      int fifoSamples = 0;
      uint32_t rngState = 0x853c49e6u;

      void Init()
      {
         inputFifo.assign(kFftSize, 0.0f);
         outputOla.assign(kFftSize * 2, 0.0f);
         magState.assign(kNumBins + 1, 0.0f);
         phasePrev.assign(kNumBins + 1, 0.0f);
         phaseSynth.assign(kNumBins + 1, 0.0f);
         deltaPhiFrozen.assign(kNumBins + 1, 0.0f);
         fifoSamples = 0;
      }

      void Reset()
      {
         std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
         std::fill(outputOla.begin(), outputOla.end(), 0.0f);
         std::fill(magState.begin(), magState.end(), 0.0f);
         std::fill(phasePrev.begin(), phasePrev.end(), 0.0f);
         std::fill(phaseSynth.begin(), phaseSynth.end(), 0.0f);
         std::fill(deltaPhiFrozen.begin(), deltaPhiFrozen.end(), 0.0f);
         fifoSamples = 0;
      }
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override;
   void Reset() override;
   void PushParams(const AudioEffectNode& node, double sampleRate) override;
   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return kFftSize; }

   // For test verification of pure framing without blur
   void SetPassthroughOnly(bool enable) { mPassthroughOnly = enable; }

private:
   double mSampleRate = 44100.0;
   bool mPassthroughOnly = false;

   PortableFft::RealFft mFft;
   std::vector<float> mWindow;
   std::vector<float> mRealScratch;
   std::vector<float> mImagScratch;
   std::vector<float> mTimeScratch;

   // Seqlock-protected per-bin alpha table
   std::atomic<uint32_t> mSeq { 0 };
   float mAlphaTableTarget[kNumBins + 1] = {};
   float mDiffusionTarget = 0.25f;
   int mFreezeTarget = 0;
   int mAnalogTarget = 0;

   // Audio thread snapshot
   float mAlphaTableLocal[kNumBins + 1] = {};
   float mDiffusionLocal = 0.25f;
   int mFreezeLocal = 0;
   int mAnalogLocal = 0;

   ChannelState mChannels[kMaxChannels];
};

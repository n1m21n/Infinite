#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/dsp/PortableFft.h"

// Spectrum Slide: morphs input A into input B by optimal transport of the two
// magnitude spectra instead of cross-fading them. Both spectra are treated as
// energy distributions over frequency; in one dimension the optimal transport
// map is "match the cumulative energy curves", so the morph at position s is
// the average of the two quantile functions, Q_s = (1-s) Q_A + s Q_B. A peak
// that sits at 440 Hz in A and 880 Hz in B is at 660 Hz at s = 0.5, not two
// half-level peaks (Bonneel et al., "Sliced and Radon Wasserstein Barycenters
// of Measures", 2015, for the 1-D case; Villani, "Optimal Transport: Old and
// New", 2009, ch. 2). Phase: each output peak advances its own phase at the
// morphed instantaneous frequency and its neighbouring bins keep the source
// phase relationships (Laroche & Dolson 1999, identity phase locking).
class AudioEffectNode;

class SpectrumSlideKernel : public IEffectKernel
{
public:
   static constexpr int kFftSize = 2048;
   static constexpr int kLog2Fft = 11;
   static constexpr int kHopSize = 512;
   static constexpr int kNumBins = kFftSize / 2; // 1024
   static constexpr int kMaxChannels = 2;
   static constexpr int kQuantiles = 4096; // energy levels transported per frame
   static constexpr int kOlaSize = 4096;

   struct Side
   {
      std::vector<float> inputFifo;
      std::vector<float> phasePrev;
      int fifoSamples = 0;
      void Init();
      void Reset();
   };

   struct ChannelState
   {
      Side a, b;
      std::vector<float> outputOla;
      std::vector<float> synthPhase; // final (locked) output phase per bin, last frame
      int olaRead = 0;
      void Init();
      void Reset();
   };

   void PrepareToPlay(double sampleRate, int maxBlockSize) override;
   void Reset() override;
   void PushParams(const AudioEffectNode& node, double sampleRate) override;
   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return kFftSize; }

private:
   struct Spectrum
   {
      std::vector<float> re, im, mag, phase, inst, cdf; // cdf: normalised cumulative energy
      float energy = 0.0f;
      void Init();
   };

   // Windowed forward FFT of `fifo` into sp.re/sp.im.
   void Transform(const float* fifo, Spectrum& sp);
   // Magnitude / phase / instantaneous frequency / cumulative energy of
   // sp.re/sp.im, tracking phase in `side`.
   void AnalyseBins(Side& side, Spectrum& sp);
   void ProcessFrame(ChannelState& st);
   // One channel's output from the mid's decided frame (see mMid).
   void ApplyToChannel(const Spectrum& chA, const Spectrum& chB);

   double mSampleRate = 44100.0;
   PortableFft::RealFft mFft;
   std::vector<float> mWindow;
   std::vector<float> mTime, mOutRe, mOutIm;
   Spectrum mA, mB;
   // Per output bin: transported energy, summed instantaneous frequency (bins)
   // weighted by energy, and the source bins that fed it.
   std::vector<float> mHist, mInstNum, mSrcA, mSrcB, mOutMag, mOutInst;

   // Stereo link. The transport and the synthesis phases are decided once a
   // frame on the mid (the average of the channels' spectra, with its own
   // phase state in mMid); each channel then gets the mid's output scaled by
   // its own level and offset by its own phase relative to the mid at the
   // bins that fed it - so the stereo image passes through. Deciding per
   // channel let each side's synthesis phases random-walk apart (DSPTEST:
   // L/R correlation 0.89 in, -0.01 out - it cancelled in mono).
   ChannelState mMid;
   Spectrum mChA[kMaxChannels], mChB[kMaxChannels];

   std::atomic<float> mSlide { 0.5f };

   ChannelState mChannels[kMaxChannels];
};

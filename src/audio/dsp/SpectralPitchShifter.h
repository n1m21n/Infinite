#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "PortableFft.h"

// Phase-vocoder pitch shifter for polyphonic material - Pitch Shifter's
// "spectral" mode.
//
// Streaming STFT (N = 4096, hop 1024, Hann analysis and synthesis, 75%
// overlap), zero-phase framed (each frame rotated by N/2 so the window is
// centred on sample 0 and a sinusoid's main-lobe bins share one phase).
// N = 4096 because chords live low: at 2048 (23 Hz bins at 48 kHz) a triad
// around 220-330 Hz is under the window's resolution and its partials merge
// (PITCHTEST chord: -1.5 dB at 2048, -54 dB at 4096). Each frame: every spectral peak's true frequency is measured from
// its phase advance; the peak, with the bins of its region (valley to
// valley), is moved to the shifted frequency, and the region is rotated as a
// block so its bins keep their phase relationship to the peak (identity
// phase locking - Laroche & Dolson, "Improved Phase Vocoder Time-Scale
// Modification of Audio", IEEE TSAP 1999, and "New phase-vocoder techniques
// for pitch-shifting", 1999). Each shifted peak's phase advances at its new
// frequency from the phase it had in the previous frame, so a held note is
// one continuous sinusoid. This is what keeps a chord a chord: every
// partial moves by the same ratio on its own, where a time-domain splice can
// only align one of them.
//
// Transients: when spectral flux (the rise in magnitude summed over bins,
// relative to the frame's energy) jumps, the frame takes the analysis phases
// as they are instead of continuing the synthesis phases - a phase reset -
// so an attack keeps its shape rather than being smeared across the window.
//
// Same frame engine as KeySnapKernel (which snaps each peak to a scale
// instead of shifting all of them by one ratio), with a much lower peak
// floor: every partial within 80 dB of the loudest gets its own region and
// moves at its own correct ratio, rather than riding along in a loud
// neighbour's region at that neighbour's shift.
//
// Each region moves by the exact fractional number of bins that puts its
// peak's true frequency on target (resampled across neighbouring bins): a
// whole-bin move leaves the content off the frequency its phase advances
// at, and overlapping frames then beat at the frame rate.
//
// Latency: kFftSize samples (~85 ms at 48 kHz). At ratio 1 the frame is passed through
// unmodified, so 0 st is an exact (delayed) reconstruction.
//
// Real-time: Prepare() allocates (main thread); Process() does not.
class SpectralPitchShifter
{
public:
   static constexpr int kFftSize = 4096;
   static constexpr int kLog2Fft = 12;
   static constexpr int kHop = 1024;
   static constexpr int kBins = kFftSize / 2;
   static constexpr int kMaxChannels = 2;
   static constexpr int kMaxPeaks = 1024;
   static constexpr int kOlaSize = 8192; // power of two, >= kFftSize + kHop

   void Prepare(double sampleRate);
   void Reset();
   int Latency() const { return kFftSize; }

   // One sample of one channel in, one out (kFftSize samples late). `ratio`
   // is read at each frame boundary.
   float Process(int ch, float x, double ratio);

private:
   struct Channel
   {
      std::vector<float> fifo, ola, phasePrev, magPrev, synthPhase, synthPhaseNext;
      std::vector<uint8_t> hasSynth, hasSynthNext;
      int fifoCount = 0;
      int olaRead = 0;
   };

   void Frame(Channel& c, double ratio);

   double mSampleRate = 48000.0;
   PortableFft::RealFft mFft;
   std::vector<float> mWindow, mTime, mRe, mIm, mOutRe, mOutIm, mMag, mPhase, mInst;
   Channel mCh[kMaxChannels];
};

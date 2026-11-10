#pragma once

#include <cstdint>
#include <vector>

#include "PortableFft.h"
#include "core/field/SampleProgram.h"

// Streaming STFT host for Spectral Field kernels
// (docs/plans/spectral-field/README.md 5c-5f).
//
// Analysis: Hann window, hop = Size / 4, zero-phase framed (each frame
// rotated by Size / 2 so a sinusoid's main-lobe bins share one phase). Per
// bin: mag (window-gain compensated: a 0 dBFS sine reads 1.0 on its peak
// bin), phase, and true frequency in Hz from the phase advance (Csound pvs
// style). The kernel then runs once per bin, bins = Size / 2 + 1.
//
// Synthesis, chosen by what the kernel writes:
//   raw      - it writes phase: the bin is rebuilt from mag and that phase.
//   vocoder  - otherwise. A bin whose freq the kernel left equal to the
//              analysis value keeps the analysis phase (so mag-only kernels
//              reconstruct exactly). Any other bin advances last frame's
//              synthesis phase by 2 pi freq hop / sr; then each peak of the
//              moved bins hands its phase to its region, valley to valley
//              (identity phase locking, Laroche & Dolson 1999).
// Hann synthesis window, overlap-add; Hann^2 at 75% overlap sums to 1.5.
//
// Latency: exactly Size samples. State: one bank of cells per bin, two
// banks (this frame / last frame) so state.at(k) reads last frame.
//
// Real-time: Prepare() allocates for the largest Size (main thread).
// SetSize(), Reset() and Process() never allocate or lock.
class SpectralFieldEngine
{
public:
   static constexpr int kMinLog2 = 9;   // 512
   static constexpr int kMaxLog2 = 13;  // 8192
   static constexpr int kMaxSize = 1 << kMaxLog2;
   static constexpr int kMaxBins = kMaxSize / 2 + 1;
   static constexpr int kMaxChannels = 2;
   static constexpr int kOlaSize = kMaxSize * 2; // power of two, >= Size + hop
   static constexpr int kBands = 64;             // spectrum display bands

   void Prepare(double sampleRate);
   // Clears FIFOs, overlap-add, phases and state (state to each cell's
   // initial value on the next frame). Audio thread safe.
   void Reset();
   // log2 of the FFT size, kMinLog2..kMaxLog2. A change resets. Audio thread safe.
   void SetSizeLog2(int log2Size);
   // A new program became active (audio thread). Forces the state banks to
   // re-map on the next frame even if the allocator reused an address.
   void ProgramSwapped() { for (Channel& c : mCh) c.swapPending = true; }
   int Size() const { return mSize; }
   int Latency() const { return mSize; }

   // One sample of channel `ch` in, one out (Size samples late). `b` is the
   // bin input's sample (0 when unconnected). `prog` null or invalid runs
   // the identity. `params` is indexed like prog->params.
   float Process(int ch, float x, float b, const Field::SampleProgram* prog, const float* params);

   // Display: peak mag per log band (20 Hz .. sr/2) of channel 0's last
   // frame, input and output. `frames` counts frames, so a reader can tell
   // when a new set landed.
   const float* BandsIn() const { return mBandsIn; }
   const float* BandsOut() const { return mBandsOut; }
   uint32_t Frames() const { return mFrames; }

private:
   struct Channel
   {
      std::vector<float> fifo, bfifo, ola;
      std::vector<float> mag, phase, freq, prevPhase;      // analysis
      std::vector<float> bmag, bphase, bfreq, bprevPhase;  // bin input
      std::vector<float> outMag, outPhase, outFreq;
      std::vector<double> synthPhase;
      std::vector<uint8_t> moved;
      std::vector<float> stateA, stateB; // kMaxBins * kSpectralMaxStateCells
      float* statePrev = nullptr;
      float* stateNext = nullptr;
      const Field::SampleProgram* lastProg = nullptr;
      bool stateValid = false;
      bool swapPending = false;
      bool firstFrame = true;
      int fifoCount = 0;
      int olaRead = 0;
      uint32_t rng = 0x9E3779B9u;
   };

   void Analyse(const float* fifo, float* mag, float* phase, float* freq, float* prevPhase, bool first);
   void Frame(Channel& c, int ch, const Field::SampleProgram* prog, const float* params);
   void SyncState(Channel& c, const Field::SampleProgram* prog);
   void UpdateBands(const Channel& c);

   double mSampleRate = 48000.0;
   int mLog2 = 11;
   int mSize = 2048;
   int mHop = 512;
   int mBins = 1025;
   PortableFft::RealFft mFft;
   std::vector<float> mWindow, mTime, mRe, mIm;
   std::vector<int> mPeaks;
   int mBandLo[kBands] = {};
   int mBandHi[kBands] = {};
   float mBandsIn[kBands] = {};
   float mBandsOut[kBands] = {};
   uint32_t mFrames = 0;
   Channel mCh[kMaxChannels];
};

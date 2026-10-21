#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "IEffectKernel.h"
#include "PortableFft.h"

class AudioEffectNode;

// Convolution Reverb: the room is designed, not picked from a list. Six
// controls describe a space (size, shape, material, distance, decay, spread)
// and a worker thread turns them into a stereo impulse response; an audio
// file can stand in for the designed one. The audio thread only convolves.
//
// The impulse response (DesignImpulse):
//   - a shoebox room from size and shape; its wall absorption follows from the
//     decay time by Eyring's formula, so early and late sound agree;
//   - the direct sound at t = 0, then early reflections by the image-source
//     method (Allen & Berkley 1979), panned by their arrival angle;
//   - past the mixing time, Gaussian noise at the level the image sum
//     converges to (reflection density 4 pi c^3 t^2 / V, each 1/(c t) in
//     amplitude), with the same exponential decay (Moorer 1979);
//   - decay per frequency band from the material and from air absorption.
//
// The convolver is zero-latency and non-uniformly partitioned (Gardner 1995;
// Wefers 2015): the first kHead taps run as a direct FIR, and three
// frequency-domain delay lines with growing block sizes take the rest. A tier
// with block B starts at IR offset B (the result of an input block is needed
// the sample after the block completes), except the largest, which starts at
// 2B and spreads its work over the following block so no single audio
// callback pays for a whole large FFT pass.
//
// A new impulse response is crossfaded in: each tier computes one block with
// both and fades between them, so turning a knob never clicks.
class ConvolutionKernel : public IEffectKernel
{
public:
   static constexpr int kChannels = 2;
   static constexpr int kHead = 64;
   static constexpr int kTiers = 3;
   static constexpr int kBlockLog2[kTiers] = { 6, 10, 12 }; // 64, 1024, 4096
   static constexpr int kTierStart[kTiers] = { 64, 1024, 8192 };
   static constexpr float kMaxSeconds = 10.0f;
   static constexpr int kMaxSamples = 960000; // 10 s at 96 kHz; higher rates get a shorter cap
   static constexpr int kEnvelopePoints = 96;
   static constexpr int kNumShapes = 4;

   struct Design
   {
      float size = 0.45f;     // 0..1 -> longest side 3..60 m
      int shape = 0;          // 0 room, 1 hall, 2 chamber, 3 tunnel
      float material = 0.5f;  // 0 soft (curtains, carpet) .. 1 hard (stone, tile)
      float distance = 0.5f;  // 0 next to the source .. 1 across the room
      float decay = 1.8f;     // RT60 at mid frequencies, seconds
      float spread = 1.0f;    // 0 mono .. 1 fully wide
      bool operator==(const Design& o) const
      {
         return size == o.size && shape == o.shape && material == o.material && distance == o.distance &&
                decay == o.decay && spread == o.spread;
      }
   };

   // One ready-to-run impulse response: head taps and every tier's partition
   // spectra. Built off the audio thread, read-only on it.
   struct IrSet
   {
      int length = 0; // samples per channel
      float head[kChannels][kHead] = {};
      int parts[kTiers] = {};
      std::vector<float> spec[kTiers][kChannels]; // parts * 2B floats: B real, then B imaginary
      float envelopeDb[kEnvelopePoints] = {};     // peak level over the IR, for the display
      float seconds = 0.0f;
   };

   ConvolutionKernel();
   ~ConvolutionKernel() override;

   void PrepareToPlay(double sampleRate, int maxBlockSize) override;
   void Reset() override;
   void PushParams(const AudioEffectNode& node, double sampleRate) override;
   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;
   bool Preparing() const override { return !mHaveIr || mJobRunning; }

   // Main thread only: the impulse response now sounding, as kEnvelopePoints
   // peak levels (dB) over `seconds`. Returns 0 before the first build.
   int Envelope(float* outDb, int maxCount, float* seconds) const;
   bool Building() const { return mJobRunning; }
   // Empty when the designed space is in use or the file loaded; otherwise why it did not.
   const std::string& FileError() const { return mFileError; }
   bool UsingFile() const { return mUsingFile; }

   // --- offline pieces, also driven directly by CONVOLUTIONTEST ---
   static const char* ShapeName(int shape);
   static int MaxIrSamples(double sampleRate);
   static void DesignImpulse(const Design& design, double sampleRate, std::vector<float>& left, std::vector<float>& right);
   // Fits a decoded file to the engine rate and the length cap.
   static void ImpulseFromFile(const float* fileL, const float* fileR, int frames, double fileRate, double sampleRate,
                               std::vector<float>& left, std::vector<float>& right);
   // Normalises (pink noise in comes out at the level it went in) unless told not to, and builds the spectra.
   static IrSet* BuildSet(std::vector<float>& left, std::vector<float>& right, double sampleRate, bool normalise = true);
   // Main thread, tests: installs a set at once and waits for nothing.
   void InstallForTest(IrSet* set);

private:
   struct Job
   {
      Design design;
      bool fromFile = false;
      std::vector<float> fileL, fileR;
      double fileRate = 0.0;
      double sampleRate = 48000.0;
   };
   void StartJob(Job&& job);
   void CollectJob();
   void Hand(IrSet* set);

   void RunTier(int t, int ch, const IrSet* set, float* outTime);
   void Accumulate(int t, int ch, const IrSet* set, int firstPart, int lastPart);
   void BigTierTick();
   void BigTierFinish();

   double mSampleRate = 48000.0;

   // --- main thread ---
   bool mHaveIr = false;
   bool mJobRunning = false;
   bool mHaveRequest = false;
   Design mRequested;
   std::string mRequestedFile;
   double mRequestedRate = 0.0;
   std::string mFileError;
   bool mUsingFile = false;
   bool mJobUsesFile = false;
   std::chrono::steady_clock::time_point mLastJobStart {};
   std::thread mWorker;
   std::atomic<bool> mJobDone { false };
   IrSet* mJobResult = nullptr;
   float mEnvelope[kEnvelopePoints] = {};
   float mEnvelopeSeconds = 0.0f;

   // --- main <-> audio: one slot each way ---
   std::atomic<IrSet*> mPending { nullptr }; // main -> audio
   std::atomic<IrSet*> mRetired { nullptr }; // audio -> main, to be freed there

   // --- audio thread ---
   struct Tier
   {
      int log2B = 0, B = 0, maxParts = 0;
      int pos = 0;                             // fill position inside the block
      int slot = 0;                            // FDL slot the next block's spectrum goes to
      const IrSet* set = nullptr;              // what this tier convolves with now
      std::vector<float> window[kChannels];    // 2B: previous block, current block
      std::vector<float> fdl[kChannels];       // maxParts * 2B input spectra
      std::vector<float> out[kChannels];       // B samples being played
      std::vector<float> next[kChannels];      // big tier only: the block after
   };
   Tier mTier[kTiers];
   PortableFft::RealFft mFft;
   std::vector<float> mAccRe, mAccIm, mTime, mTimeB;

   const IrSet* mCur = nullptr;  // head taps in use; a tier follows at its next block
   const IrSet* mNext = nullptr; // set being faded in, or null
   int mHeadFade = 0;            // samples of head crossfade left
   float mHist[kChannels][kHead * 2] = {};
   int mHistPos = 0;

   // Big tier job, spread over the block after the one it belongs to.
   struct BigJob
   {
      bool active = false;
      const IrSet* a = nullptr; // result = a, or a fading to b
      const IrSet* b = nullptr;
      int slot = 0; // FDL slot of the block being worked on
      int step = 0, steps = 0, perTick = 1;
   };
   BigJob mBig;
   std::vector<float> mBigIn[kChannels];                     // 2B: the window the job transforms
   std::vector<float> mBigAccRe[2][kChannels], mBigAccIm[2][kChannels]; // per set (a, b), per channel
};

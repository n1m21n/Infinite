#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "IEffectKernel.h"
#include "audio/dsp/MeshModalSolver.h"

class AudioEffectNode;

// Shape Resonator: rings the vibration modes of a geometry input. The mesh's
// surface-Laplacian eigenvalues (MeshModalSolver) give the mode frequencies
// f_i = tune * sqrt(lambda_i / lambda_0) - the dispersion of a membrane or
// thin plate, where frequency goes as the square root of the eigenvalue - so
// a square plate rings its familiar inharmonic set and a long thin bar rings
// a near-harmonic one. Each mode is a two-pole resonator; the input is
// weighted by the mode shape at the strike position and the output by the
// shape at the pickup position, so where you hit the shape changes which modes
// speak (a node of a mode does not ring when struck there).
//
// The solve (about 0.3 s for 1000 vertices) runs on a worker thread, never the
// audio thread; the audio thread only ever sees finished resonator
// coefficients, handed over through a lock-free triple buffer.
class ShapeResonatorKernel : public IEffectKernel
{
public:
   static constexpr int kMaxModes = MeshModalSolver::kMaxModes;
   static constexpr int kMaxChannels = 2;

   ShapeResonatorKernel();
   ~ShapeResonatorKernel() override;

   void PrepareToPlay(double sampleRate, int maxBlockSize) override;
   void Reset() override;
   void PushParams(const AudioEffectNode& node, double sampleRate) override;
   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   // Main thread only (UI): frequencies of the modes currently sounding, Hz.
   int ModeFrequencies(float* out, int maxCount) const;
   // Main thread only: the last finished solve had this many modes, or 0 while
   // the first solve is still running.
   int SolvedModeCount() const { return mModes.valid ? mModes.count : 0; }
   bool Solving() const { return mJobRunning; }

private:
   struct Coeffs
   {
      int n = 0;
      float bIn[kMaxModes] = {};
      float a1[kMaxModes] = {};
      float a2[kMaxModes] = {};
      float gOut[kMaxModes] = {};
   };

   void StartSolve(std::vector<float> positions, std::vector<uint32_t> indices);
   void CollectSolve();
   void Publish(const Coeffs& c);

   double mSampleRate = 44100.0;

   // --- main thread ---
   MeshModalSolver::Modes mModes;
   const void* mSolvedGeo = nullptr;
   unsigned long long mSolvedRev = 0;
   bool mHaveRequest = false;
   bool mJobRunning = false;
   const void* mJobGeo = nullptr;
   unsigned long long mJobRev = 0;
   std::thread mWorker;
   std::atomic<bool> mJobDone { false };
   MeshModalSolver::Modes mJobResult;
   float mFreqs[kMaxModes] = {};
   int mFreqCount = 0;

   // --- triple buffer main -> audio ---
   Coeffs mBuf[3];
   int mWriteIdx = 0;
   int mReadIdx = 1;
   std::atomic<int> mMailbox { 2 }; // buffer index | 4 when fresh

   // --- audio thread ---
   Coeffs mCur, mTarget;
   float mY1[kMaxChannels][kMaxModes] = {};
   float mY2[kMaxChannels][kMaxModes] = {};
};

#pragma once

#include <cmath>

// Band-limited wavetable bank shared by every Wavetable node.
//
// Every table in the bank is defined *spectrally* - as a harmonic amplitude
// and phase series per frame - never as raw sampled audio. That is not a
// stylistic choice: it is what makes the mip pyramid below exact. A table
// stored as time-domain samples can only be band-limited after the fact by
// filtering, which smears the very edges (a square's step, a saw's ramp) that
// give the waveform its character. Synthesising each mip level directly from
// the first H harmonics gives a level that is *exactly* band-limited to H and
// otherwise identical to the full-bandwidth frame.
//
// Layout, and why each number is what it is:
//
//   kFrameSize  1024  - one cycle. A power of two so the phase accumulator's
//                       wrap and the sine table's index are a mask, not a
//                       modulo, and so harmonic h at sample i is exactly
//                       sinTable[(h * i) & (kFrameSize - 1)] with no rounding.
//   kFrames        8  - morph positions per table. `position` interpolates
//                       between adjacent frames, so 8 is 7 crossfades - enough
//                       for a sweep to read as continuous motion rather than
//                       as stepping between presets.
//   kMipLevels    10  - harmonic ceilings 512, 256, ... 1. Level L holds
//                       harmonics 1..(512 >> L), so playback picks the level
//                       whose ceiling sits just under Nyquist for the current
//                       pitch and never aliases.
//
// Total storage is kNumTables * kFrames * kMipLevels * kFrameSize floats
// (~6.9 MB at the current kNumTables), built once per process on the main
// thread by EnsureBuilt() and
// immutable afterwards - which is what lets ProcessBlock read it with no
// synchronisation at all.
namespace Wavetable
{
   constexpr int kFrameSize = 1024;
   constexpr int kFrames = 8;
   constexpr int kMipLevels = 10;
   constexpr int kMaxHarmonic = kFrameSize / 2; // 512 - Nyquist for one frame
   constexpr int kNumTables = 22;

   // Main thread, idempotent, ~100 ms the first time. Every accessor below
   // assumes it has already run; the Wavetable node calls it from its
   // constructor so the audio thread never races the build.
   void EnsureBuilt();

   int NumTables();
   const char* TableName(int table);

   // kFrameSize contiguous floats. `table`, `frame` and `mip` are clamped, so
   // a stale saved index can never index out of the bank.
   const float* Frame(int table, int frame, int mip);

   // The finest mip level whose harmonic ceiling still sits below Nyquist for
   // a phase increment of `phaseInc` cycles/sample. phaseInc <= 0 (a stopped
   // oscillator) returns the finest level - there is nothing to alias.
   inline int MipForPhaseInc(double phaseInc)
   {
      if (phaseInc <= 0.0)
         return 0;
      const double maxHarmonic = 0.5 / phaseInc; // highest harmonic under Nyquist
      int level = 0;
      int ceiling = kMaxHarmonic;
      while (level < kMipLevels - 1 && (double)ceiling > maxHarmonic)
      {
         ceiling >>= 1;
         level++;
      }
      return level;
   }

   // Two-dimensional read: linear between the two samples either side of
   // `phase01`, then linear between the two frames either side of `position`.
   // Both frames must come from the same mip level or the crossfade would mix
   // two different bandwidths and buzz as `position` moves.
   inline float Sample(const float* frameLo, const float* frameHi, float frameFrac, double phase01)
   {
      phase01 -= floor(phase01);
      const double x = phase01 * (double)kFrameSize;
      const int i0 = (int)x & (kFrameSize - 1);
      const int i1 = (i0 + 1) & (kFrameSize - 1);
      const float fx = (float)(x - floor(x));
      const float a = frameLo[i0] + (frameLo[i1] - frameLo[i0]) * fx;
      const float b = frameHi[i0] + (frameHi[i1] - frameHi[i0]) * fx;
      return a + (b - a) * frameFrac;
   }
}

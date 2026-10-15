#pragma once

// Baked MIT KEMAR HRTF grid (see tools/spatial/bake_hrtf.py for provenance and
// the licence note). Regular grid: elevation -40..90 step 10 (14 rows),
// azimuth 0..355 step 5 (72 columns; 0 front, +90 right), 128 taps at 44.1 kHz,
// per-ear onset delay in samples stored separately, diffuse-field equalised.
namespace HrtfKemar
{
   constexpr int kNumEl = 14;
   constexpr int kNumAz = 72;
   constexpr int kTaps = 128;
   constexpr int kElMin = -40;
   constexpr int kElStep = 10;
   constexpr int kAzStep = 5;
   constexpr double kSampleRate = 44100.0;

   extern const float kScale;
   extern const float kDelay[kNumEl * kNumAz * 2];
   extern const short kIr[kNumEl * kNumAz * 2 * kTaps];
}

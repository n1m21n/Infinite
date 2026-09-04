#pragma once

#include <atomic>
#include <vector>

// Transient (onset) detection for SlicerNode. Pure math: no INode, no ImGui,
// no GL, no threads of its own - the caller (SlicerNode's worker thread) owns
// all of that. Implemented from primary references, clean-room:
//
//   - Onset detection function: half-wave-rectified spectral flux over a
//     log-compressed magnitude spectrogram, and the adaptive-threshold peak
//     picker (local maximum over +-w frames, plus a mean over an asymmetric
//     window offset by a fixed delta) - Dixon, S., "Onset Detection
//     Revisited", Proc. of the 9th Int. Conference on Digital Audio Effects
//     (DAFx-06), Montreal, 2006. Spectral flux was the best-performing and
//     cheapest of the ODF family evaluated there (F = 0.964, 8.8 ms mean
//     absolute error), which is why it is the one implemented here.
//
//   - The 3-bin maximum filter applied to the *reference* (previous) frame
//     before differencing, which suppresses the false positives vibrato and
//     pitch glides otherwise generate - Boeck, S. & Widmer, G., "Maximum
//     Filter Vibrato Suppression for Onset Detection", Proc. DAFx-13,
//     Maynooth, 2013 ("SuperFlux").
//
//   - STFT framing conventions (Hann analysis window, constant hop) -
//     Portnoff (1976) / Dolson (1986).
//
// The FFT itself is the repo's existing PortableFft (vDSP zrip conventions,
// x2-scaled bins) - no new dependency.
namespace SlicerDsp
{
   struct Params
   {
      float sensitivity = 65.0f;   // 0..100; 100 = most sensitive (lowest delta)
      double minIoiSeconds = 0.028; // minimum inter-onset interval
      float silenceGateDb = -70.0f; // reject onsets sitting in silence
      int fftSize = 1024;           // power of two; hop scales with sample rate
      int maxSlices = 64;
   };

   // Worker-thread only. Allocates freely during setup; allocation-free
   // inside the inner loops. Returns onset positions in FRAMES, strictly
   // ascending, with 0 always present, and the matching peak strengths
   // (normalised ODF value at the accepted frame; the forced onset at 0
   // carries a sentinel strength so a top-N prune can never drop it).
   //
   // Honours `abort`: if non-null and it becomes true, returns early with
   // whatever was completed (possibly just the forced onset at 0).
   void Detect(const float* mono, int len, double sr, const Params& p,
               std::vector<int>& outOnsets, std::vector<float>& outStrengths,
               const std::atomic<bool>* abort = nullptr);

   // Strength value stamped on the always-present onset at frame 0, so that
   // "keep the strongest N" never discards the slice the whole sample starts
   // from. Public because the node's top-N prune compares against it.
   constexpr float kForcedOnsetStrength = 1.0e9f;
}

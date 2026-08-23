#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Analysis -> mutable genome -> additive resynthesis engine for MolderNode.
// Pure math: no INode, no ImGui, no GL, no threads of its own - the caller
// (MolderNode's worker thread) owns all of that. Implemented from primary
// references, clean-room:
//   - YIN pitch: de Cheveigne & Kawahara, "YIN, a fundamental frequency
//     estimator for speech and music", 2002.
//   - STFT / phase vocoder framing: Portnoff (1976) / Dolson (1986).
//   - Sinusoids-plus-residual decomposition: Serra & Smith, "Spectral
//     Modeling Synthesis", 1990.
//   - Stiff-string inharmonicity law: Fletcher & Rossing, "The Physics of
//     Musical Instruments".
namespace MolderDsp
{
   constexpr int kMaxPartials = 48;
   constexpr int kNumBands = 8;

   // Result of one full-sample analysis pass. Built entirely on the worker
   // thread; the node hands out a const reference to the main/audio threads
   // only after the worker has finished writing it (see MolderNode's
   // mResultReady handoff).
   struct Analysis
   {
      double sourceSR = 44100.0;
      int sourceLen = 0;
      int hop = 256;
      int fftSize = 2048;
      int numFrames = 0;
      int numPartials = 0;
      int peakFrame = 0; // frame with maximum voiced energy - phase/envelope seed point

      float globalF0 = 220.0f;
      float globalConfidence = 0.0f;

      // Per analysis frame.
      std::vector<float> frameF0;      // Hz, glided/tracked
      std::vector<float> frameVoicing; // 0..1

      // Per (frame, partial), flattened as frame * kMaxPartials + h.
      std::vector<float> partialAmp;
      // Phase-vocoder instantaneous frequency (Hz) per (frame, partial),
      // same flattening. This is what Render interpolates for playback
      // frequency - far more precise than re-deriving it from the coarser
      // frameF0(t)*harmonic contour, whose tracking noise would otherwise
      // scale with harmonic number and accumulate into audible phase drift
      // over anything longer than a fraction of a second.
      std::vector<float> partialFreq;

      // Per partial, single values captured across the whole analysis.
      float measuredRatio[kMaxPartials] = {}; // f_measured / f0, from the peak frame
      float seedPhase[kMaxPartials] = {};     // radians, back-propagated to sample 0
      float envelopeFreq[kMaxPartials] = {};  // Hz, spectral envelope sample point
      float envelopeAmp[kMaxPartials] = {};   // linear amplitude at envelopeFreq

      // Residual, split by transient/steady classification (spectral flux),
      // full sourceLen each. residualBand[b] is residualSteady+residualTransient
      // passed through a fixed octave-band bandpass, precomputed here so
      // Render only has to do a weighted sum.
      std::vector<float> residualSteady;
      std::vector<float> residualTransient;
      std::vector<float> residualBand[kNumBands];

      bool valid = false;
   };

   // Runs the whole analysis pipeline (YIN -> STFT -> per-frame tracking ->
   // voicing gate -> partial extraction -> residual) on `mono`/`len` at
   // `sr`. Worker-thread only: allocates freely, may take 100s of ms.
   // If `abort` is non-null and becomes true mid-analysis, returns early
   // with whatever was completed and out.valid left false.
   void Analyze(const float* mono, int len, double sr, Analysis& out, const std::atomic<bool>* abort = nullptr);

   // Mutable genome mutated by rolling. Baseline (every field at its default
   // below) must render back to (approximately) the analysed source - this
   // is exactly what INFINITE_MOLDERTEST's reconstruction assertion checks.
   struct Genome
   {
      float partialAmp[kMaxPartials];
      float bandAmp[kNumBands];
      float noiseAmount = 1.0f;      // steady-residual ("air") level
      float transientAmount = 1.0f;  // transient-residual ("snap") level
      float tonalAmount = 1.0f;      // partial-bank overall level
      float attackScale = 1.0f;
      float decayScale = 1.0f;
      float decayTilt = 0.0f;
      float brightnessTilt = 0.0f;   // -1..1
      float inharmonicity = 0.0f;    // 0 = measured stretch, >0 exaggerates it
      float harmonicStretch = 1.0f;  // freq ~ f0 * (h+1)^stretch, before inharmonicity
      float pitchShiftSemitones = 0.0f;
      bool reverseResidual = false;

      Genome()
      {
         for (int i = 0; i < kMaxPartials; ++i) partialAmp[i] = 1.0f;
         for (int i = 0; i < kNumBands; ++i) bandAmp[i] = 1.0f;
      }
   };

   // Seeded xorshift32 - deterministic so a (seed, generation) pair always
   // replays to the same genome (see MolderNode's 4f seed/generation scheme).
   struct Rng
   {
      uint32_t state;
      explicit Rng(uint32_t seed) : state(seed != 0 ? seed : 0x9E3779B9u) {}
      uint32_t NextU32();
      float NextFloat();               // [0, 1)
      float NextRange(float lo, float hi);
   };

   // Mutates `g` in place: reverts 10% toward baseline, then applies
   // log-normal per-partial jitter plus structural moves (decimation,
   // amplitude shuffle, dropouts, pitch walk, residual reverse), each gated
   // on `strength` (the chaos param, expected in [0.25, 1.3]).
   void Mutate(Genome& g, float strength, Rng& rng);

   // Renders `analysis` through `genome` into `out` (resized to
   // analysis.sourceLen, at analysis.sourceSR). Worker-thread only.
   // If `abort` is non-null and becomes true mid-render, stops early and
   // leaves `out` at whatever length it had already reached.
   //
   // `outR`, if non-null, is filled with a second channel and `out` becomes
   // the left channel: alternate partials get a fixed +-cents micro-detune
   // (opposite sign per channel) and a small alternating pan, so the additive
   // stack gains stereo width instead of resolving to a dead-centre mono
   // spike. Passing outR==nullptr (the default) renders identically to
   // before this existed - every existing mono call site, including
   // INFINITE_MOLDERTEST's reconstruction assertions, is untouched by it.
   void Render(const Analysis& analysis, const Genome& genome, std::vector<float>& out,
               const std::atomic<bool>* abort = nullptr, std::vector<float>* outR = nullptr);
}

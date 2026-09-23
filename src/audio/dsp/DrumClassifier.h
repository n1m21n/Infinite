#pragma once

#include <string>

// Pure DSP slice classifier for BeatArrangerNode.
// Classifies audio slices into 10 classes: 7 unpitched drum classes plus
// 3 pitched/tonal classes appended for v2 (Synth, Piano, Tonal).
//
// Clean-room implementation based on primary literature:
//   - Feature set definitions (spectral centroid, spectral flatness, band energy
//     ratios, attack/decay envelopes, zero-crossing rate):
//     Peeters, G., "A large set of audio descriptors for sound description
//     (musical content) in the CUIDADO project", IRCAM Technical Report, 2004.
//
//   - Drum sound feature selection and classification techniques:
//     Herrera, P., Yeterian, A., & Gouyon, F., "Automatic Classification of
//     Drum Sounds: A Comparison of Feature Selection Methods and Classification
//     Techniques", Proc. ICMC / Music & AI, 2002.
//
//   - Micro-onset temporal signatures (claps vs snares) and drum loop transcription:
//     Gillet, O., & Richard, G., "Automatic Transcription of Drum Loops",
//     Proc. IEEE ICASSP, 2004.
//
//   - Piano partial stretch (inharmonicity), f_k = k*f0*sqrt(1 + B*k^2):
//     Fletcher, N. H., & Rossing, T. D., "The Physics of Musical Instruments",
//     2nd ed., Springer, 1998.
//
//   - Pitch detection (normalised-difference / autocorrelation style), used
//     here as a clean-room reimplementation of the method described in:
//     de Cheveigne, A., & Kawahara, H., "YIN, a fundamental frequency
//     estimator for speech and music", J. Acoust. Soc. Am. 111(4), 2002.
//
// Pure math: no INode, no ImGui, no GL, no threads of its own.

namespace DrumClassifier
{
   enum class DrumClass
   {
      Kick = 0,
      Bass,
      Snare,
      Clap,
      HatClosed,
      HatOpen,
      Perc,
      // append-only: the index is saved in patches
      Synth,
      Piano,
      Tonal
   };

   constexpr int kNumClasses = 10;

   const char* ClassName(DrumClass c);
   DrumClass ClassFromName(const char* name);

   struct Features
   {
      // Band-energy ratios (sum to ~1.0), computed from a time-domain
      // low-pass filter-bank cascade so the sub band is meaningful at any
      // sample rate (no FFT-bin-count dependency):
      // sub 20-90 Hz, low 90-250, lowmid 250-1k, mid 1-4k, high 4-10k, air >10k
      float energySub = 0.0f;
      float energyLow = 0.0f;
      float energyLowMid = 0.0f;
      float energyMid = 0.0f;
      float energyHigh = 0.0f;
      float energyAir = 0.0f;

      // Spectral centroid (mean + trajectory over first 50 ms in Hz)
      float centroidMean = 0.0f;
      float centroidTrajectory = 0.0f; // end - start change (Hz, negative = falling)
      float centroidDropRatio = 0.0f;  // (start - end) / start, over first 50 ms; >0 = falling

      // Spectral flatness (Wiener entropy, 0 = tonal, 1 = white noise)
      float spectralFlatness = 0.0f;

      // Zero-crossing rate
      float zcr = 0.0f;

      // Attack time in milliseconds (10% -> 90% peak)
      float attackTimeMs = 0.0f;

      // Decay time in milliseconds (peak -> -20 dB / 10% amplitude), measured
      // from an RMS envelope with a window >= one period of 30 Hz (~33 ms).
      float decayTimeMs = 0.0f;

      // Low-band (30-250 Hz) pitch stability (0..1), low-passed first and
      // requiring >= 2 correlation windows (0 = unknown/too short, not "low").
      float lowBandPitchStability = 0.0f;

      // Micro-onset count in first 40 ms (peaks 5-15 ms apart, clap signature)
      int microOnsets40ms = 0;

      // ---- Pitchedness (wideband, 80-2000 Hz), for Synth/Piano/Tonal ----
      float f0Hz = 0.0f;             // 0 = no usable estimate
      float f0Confidence = 0.0f;     // 0..1, normalised autocorrelation peak
      float harmonicRatio = 0.0f;    // 0..1, energy at harmonic peaks / total
      float inharmonicityStretch = 0.0f; // 0..1, partial stretch vs ideal harmonics (Piano cue)
      float decayLinearityR2 = 0.0f; // 0..1, how well log-envelope fits a line (smooth exp decay)
      bool hasSustainPlateau = false; // a flat mid-envelope segment (Synth cue)
   };

   struct Result
   {
      DrumClass cls = DrumClass::Perc;
      float confidence = 0.0f; // margin between top two scores
      float scores[kNumClasses] = {};
      Features f;
   };

   // Classifies a mono PCM slice.
   // `fileNameHint`: optional filename prior (only applied for single-slice samples).
   // Whole-token match only (split on non-alphanumerics and case boundaries) -
   // "sub" never matches inside "subtle", "hat" never matches inside "what".
   Result Classify(const float* mono, int len, double sr, const char* fileNameHint = nullptr);
}

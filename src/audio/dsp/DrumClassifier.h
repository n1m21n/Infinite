#pragma once

#include <string>

// Pure DSP drum slice classifier for BeatArrangerNode.
// Classifies audio slices into 7 drum classes:
//   Kick, Bass, Snare, Clap, HatClosed, HatOpen, Perc
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
      Perc
   };

   constexpr int kNumClasses = 7;

   const char* ClassName(DrumClass c);
   DrumClass ClassFromName(const char* name);

   struct Features
   {
      // Band-energy ratios (sum to ~1.0):
      // sub 20–90 Hz, low 90–250, lowmid 250–1k, mid 1–4k, high 4–10k, air >10k
      float energySub = 0.0f;
      float energyLow = 0.0f;
      float energyLowMid = 0.0f;
      float energyMid = 0.0f;
      float energyHigh = 0.0f;
      float energyAir = 0.0f;

      // Spectral centroid (mean + trajectory over first 50 ms in Hz)
      float centroidMean = 0.0f;
      float centroidTrajectory = 0.0f; // end - start change (negative = falling)

      // Spectral flatness (Wiener entropy, 0 = tonal, 1 = white noise)
      float spectralFlatness = 0.0f;

      // Zero-crossing rate
      float zcr = 0.0f;

      // Attack time in milliseconds (10% -> 90% peak)
      float attackTimeMs = 0.0f;

      // Decay time in milliseconds (peak -> -20 dB / 10% amplitude)
      float decayTimeMs = 0.0f;

      // Low-band pitch stability (0..1 autocorrelation peak consistency in 30-250 Hz)
      float lowBandPitchStability = 0.0f;

      // Micro-onset count in first 40 ms (peaks 5-15 ms apart, clap signature)
      int microOnsets40ms = 0;
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
   Result Classify(const float* mono, int len, double sr, const char* fileNameHint = nullptr);
}

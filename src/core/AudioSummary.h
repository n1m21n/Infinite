#pragma once

#include <string>
#include <vector>

// =========================================================================
// AudioSummary - the numbers `Infinite --audio-summary` reports about a
// stretch of stereo audio (docs/fix-briefs/headless-engine.md, block 2.1):
// loudness, peaks, clipping, silence, stereo correlation, a coarse spectrum
// and a tempo guess. Pure DSP on samples pushed block by block: no GL, no
// ImGui, no engine, so it is testable on a synthesized signal (SelfTest).
//
// Named AudioSummary, not AudioAnalysis: src/platform/common/AudioAnalysis.h
// already owns that header name (the live Audio Analyze node's FFT maths).
// =========================================================================

namespace AudioSummary
{
   constexpr int kBands = 32;

   // Linear values throughout; ToJson turns them into dB. A value that does
   // not exist for this signal (loudness of silence, correlation of a mono
   // side, tempo of a drone) is NaN here and `null` in the JSON.
   struct Result
   {
      double sampleRate = 0.0;
      long long frames = 0;
      double seconds = 0.0;

      double integratedLufs = 0.0; // ITU-R BS.1770-4: K-weighted, 400 ms blocks, -70 LUFS and -10 LU gates
      double truePeak = 0.0;       // 4x oversampled, linear
      double samplePeak = 0.0;     // linear, either channel
      double rms[2] = { 0.0, 0.0 };
      double dcOffset[2] = { 0.0, 0.0 };
      long long clippedFrames = 0; // frames with |sample| >= 1.0 on either channel
      double clippedPercent = 0.0;
      double silenceRatio = 0.0;   // share of 100 ms blocks under -60 dBFS RMS
      double correlation = 0.0;    // sum(LR) / sqrt(sum(LL) sum(RR)), -1..1

      // Estimates: spectral-flux onsets and the strongest repeat interval
      // between them. Good enough to tell "120-ish" from "90-ish", not a
      // beat tracker.
      int onsetCount = 0;
      double bpmEstimate = 0.0;
      double bpmConfidence = 0.0; // autocorrelation of the flux at that interval, 0..1

      // 32 log-spaced bands from 20 Hz to min(20 kHz, Nyquist): mean power
      // per band over the whole signal (a full-scale sine reads -3 dB in its band).
      double bandHz[kBands] = {};
      double bandPower[kBands] = {};
      int loudestBand = -1; // -1: silence, no band stands out

      std::vector<double> loudnessPerSecond; // LUFS of each whole second, ungated
   };

   class Analyzer
   {
   public:
      explicit Analyzer(double sampleRate);
      ~Analyzer();
      Analyzer(const Analyzer&) = delete;
      Analyzer& operator=(const Analyzer&) = delete;

      // `right` may equal `left` for a mono signal.
      void Push(const float* left, const float* right, int frames);
      Result Finish();

   private:
      struct State;
      State* mState;
   };

   // 20*log10(linear), NaN at or below zero.
   double Db(double linear);

   // `full` adds the spectrum and the per-second loudness list; without it
   // only the headline numbers are written (the status line's copy).
   std::string ToJson(const Result& r, bool full);

   // Synthesized signals with known answers: a 1 kHz sine at -20 dBFS reads
   // -20 LUFS and peaks in the 1 kHz band, an inter-sample peak is found, a
   // 120 BPM click track counts its clicks. One line per check in `report`.
   bool SelfTest(std::string& report);
}

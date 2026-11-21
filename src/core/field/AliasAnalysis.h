#pragma once

#include "SampleProgram.h"
#include <string>
#include <vector>

// Aliasing analysis for compiled Field sample programs (phase 1 of
// docs/research/aliasing-aware-field). Report only: nothing here changes how
// a kernel runs.
//
// Method (periodic harmonic balance). Driven by one tone of frequency f0,
// a kernel's ideal output is a periodic function of the input phase. The
// analysis finds that periodic steady state on a grid of one input period:
//   - memoryless code: the program is evaluated once per grid point;
//   - state cells: next = G(theta, state one sample ago) is solved as a
//     periodic fixed point, with the one-sample delay applied exactly as a
//     phase shift of each harmonic (exp(-i h 2pi f0 / fs)), by a Newton
//     iteration preconditioned with the grid-mean Jacobian, which solves
//     linear filters in one step;
//   - phase accumulators (self-Jacobian 1) are recognised by checking that
//     a ramp locked to f0 is a fixed point.
//   - delay lines, delay(x, N): one more cell with lag N instead of 1, the
//     same exact shift exp(-i h N 2pi f0 / fs) (phase 5, README section 15).
// The grid spectrum gives the ideal (alias-free) harmonics up to H; past H
// a power law fitted to the top two octave bands extrapolates the tail.
// Sampling at fs folds harmonic h to |h f0 mod fs| (reflected), so the
// alias at any oversampling factor follows in closed form from one analysis
// per sample rate (the rate matters only through state delays and sr).
//
// The program runs on the real interpreter (RunSampleProgram), so every op
// has its runtime meaning. Programs whose output is not a function of the
// input phase (sample counter, tables, rand, beat clock, note
// emit, declared inputs) are reported unsupported, as are feedback loops
// with no periodic steady state (self-oscillation, chaos).
namespace Field
{
   struct AliasContext
   {
      double f0Hz = 1000.0;     // the driving fundamental
      float inAmplitude = 0.5f; // `in` = inAmplitude * sin(phase)
      bool usesInput = true;    // false: `in` = 0, f0 comes from an accumulator
      int gridLog2 = 13;        // grid points per input period
      // Segment analysis (phase 2 rate planning): registers set from a
      // periodic waveform before every run, one sample per grid point
      // (inputWaves[i].size() == 1 << gridLog2), and registers whose
      // periodic waveform is returned in AliasSpectrum::captured.
      std::vector<int> inputRegs;
      std::vector<std::vector<double>> inputWaves;
      std::vector<int> captureRegs;
   };

   struct AliasSpectrum
   {
      bool ok = false;
      std::string status;            // "ok" or why there is no prediction
      double sampleRate = 0.0;       // fs the state delays / sr were analysed at
      std::vector<double> power;     // one-sided |amplitude|^2 per harmonic, 0..H
      double tailDensity = 0.0;      // tail: P(h) = tailDensity * (h / tailRef)^-tailExp, h > H
      double tailRef = 1.0;
      double tailExp = 0.0;
      int iterations = 0;            // Newton iterations (0 = memoryless)
      long long evaluations = 0;     // program runs spent
      int accumulators = 0;          // state cells recognised as phase accumulators
      double loopRadius = 0.0;       // spectral radius of the mean state Jacobian (< 1: stable)
      std::vector<std::vector<double>> captured; // one grid waveform per AliasContext::captureRegs
   };

   // Periodic steady state and harmonic spectrum of `prog`'s output at
   // sample rate `sampleRate`, params at their defaults.
   bool AnalyzePeriodicSpectrum(const SampleProgram& prog, const AliasContext& ctx, double sampleRate,
                                AliasSpectrum& out);

   // Harmonic power and tail of one periodic grid waveform (what
   // AnalyzePeriodicSpectrum does with the output). Fills power/tail*, sets ok.
   void SpectrumFromWave(const std::vector<double>& y, AliasSpectrum& out);

   // Zeroes every harmonic above maxHarmonic of a periodic grid waveform
   // (an ideal low-pass: what a trip through the base rate leaves).
   void BandLimitWave(std::vector<double>& y, double maxHarmonic);

   // Energy of components that fold below `audibleHz` at fs = factor *
   // baseRate and do not sit within `harmonicTolHz` of a harmonic of f0, in
   // dB re the fundamental. A decimator back to baseRate is taken as ideal.
   double PredictAliasDb(const AliasSpectrum& s, double f0Hz, double baseRate, int factor, double audibleHz,
                         double harmonicTolHz);
}

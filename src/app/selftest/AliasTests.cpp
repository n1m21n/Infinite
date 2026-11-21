// INFINITE_ALIASTEST: how much aliasing each static nonlinearity produces.
//
// A pure tone goes through the real AudioEffectNode -> AudioEffectRuntime ->
// kernel chain at 48 kHz. Its harmonics above Nyquist fold back to
// frequencies that are not multiples of the tone, so after a
// Blackman-Harris windowed FFT every bin below 20 kHz that is not a harmonic
// (or DC) is audible aliasing. The figure reported is that energy relative
// to the fundamental. Folding into 20-24 kHz is not counted: an oversampler's
// transition band legitimately lets a little of it through, and it is above
// hearing.
//
// The tone sits on an odd FFT bin of a power-of-two transform, so a folded
// harmonic |h*k0 - m*N| can never land on a harmonic bin j*k0: that would
// need (h +- j) * k0 to be a multiple of N, impossible for an odd k0.
//
// Only static nonlinearities are measured. A modulated effect (chorus, wow
// and flutter in an analog mode) spreads sidebands around every harmonic,
// which this metric would misread as aliasing.
//
// Ratchet: a case with a gateDb must reach it or the test fails. A node is
// gated once it has real anti-aliasing (oversampling and/or ADAA), at what
// it measurably achieves; ungated cases are reported so the numbers are
// visible before and after. Drive before its anti-aliasing (same metric):
// -21 dB at 50% drive, -12 dB at 100%.
// A linear effect runs as the rig's own control and must read clean.
#include "app/AppShared.h"
#include "audio/dsp/MinPhaseOversampler.h"
#include "audio/dsp/PortableFft.h"
#include "nodes/AnalogNode.h"
#include "nodes/FieldSampleNode.h"
#include "field/AliasAnalysis.h"
#include "field/BackendRegister.h"
#include "field/RatePlan.h"
#include "field/SampleRuntime.h"
#include <chrono>
#include <complex>
#include <filesystem>
#include <cstring>
#include <functional>
#include <set>
#include <map>
#include "nodes/WavetableNode.h"

namespace app
{
namespace
{
   constexpr double kRate = 48000.0;
   constexpr int kLog2N = 14;
   constexpr int kN = 1 << kLog2N;
   constexpr int kFundBin = 1707; // 5001 Hz; odd, see the file comment
   constexpr int kLobe = 4;       // 4-term Blackman-Harris main lobe half-width, bins
   constexpr double kAudibleHz = 20000.0;
   constexpr float kAmplitude = 0.5f;
   constexpr double kControlDb = -100.0;

   struct AliasMeasurement
   {
      double aliasDb = 0.0;    // non-harmonic energy re fundamental
      double harmonicDb = 0.0; // in-band harmonic energy re fundamental
   };

   // carrierBin > 0: a ring modulator's output, whose legitimate components
   // are every |h * fundBin +- m * carrierBin| (input harmonic h times
   // carrier harmonic m), all of them counted as "fundamental". Bins 1707 or
   // 171 against carrier bin 211 (prime, odd) keep folded products off that
   // set to within the window's main lobe.
   AliasMeasurement Measure(const std::vector<float>& x, int fundBin, int carrierBin = 0)
   {
      std::vector<float> windowed((size_t)kN);
      for (int i = 0; i < kN; i++)
      {
         const double t = 2.0 * M_PI * (double)i / (double)kN;
         const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
         windowed[(size_t)i] = (float)(x[(size_t)i] * w);
      }
      PortableFft::RealFft fft;
      fft.Prepare(kLog2N);
      std::vector<float> re((size_t)kN / 2), im((size_t)kN / 2);
      fft.Forward(windowed.data(), kLog2N, re.data(), im.data());

      std::vector<char> harmonic((size_t)kN / 2, 0);
      for (int k = 0; k <= kLobe; k++)
         harmonic[(size_t)k] = 1; // DC
      for (int h = 1; h * fundBin - kLobe < kN / 2; h++)
         for (int d = -kLobe; d <= kLobe; d++)
         {
            const int k = h * fundBin + d;
            if (k > 0 && k < kN / 2)
               harmonic[(size_t)k] = 1;
         }
      std::vector<char> sideband((size_t)kN / 2, 0);
      if (carrierBin > 0)
      {
         std::fill(harmonic.begin(), harmonic.end(), 0);
         // h = 0..4: the analog mode's input shaping adds input harmonics
         // and its carrier bleed adds the carrier alone, both real products.
         for (int h = 0; h <= 4; h++)
            for (int m = 0; m * carrierBin - h * fundBin - kLobe < kN / 2; m++)
               for (int sgn : { -1, 1 })
                  for (int d = -kLobe; d <= kLobe; d++)
                  {
                     const int k = std::abs(h * fundBin + sgn * m * carrierBin) + d;
                     if (k > 0 && k < kN / 2)
                        sideband[(size_t)k] = 1;
                  }
      }

      double fund = 0.0, alias = 0.0, harm = 0.0;
      const int audibleTop = (int)(kAudibleHz / kRate * kN);
      for (int k = 1; k < kN / 2; k++)
      {
         if (k > audibleTop && !harmonic[(size_t)k] && !sideband[(size_t)k])
            continue;
         const double p = (double)re[(size_t)k] * re[(size_t)k] + (double)im[(size_t)k] * im[(size_t)k];
         if (sideband[(size_t)k] || (carrierBin == 0 && std::abs(k - fundBin) <= kLobe))
            fund += p;
         else if (harmonic[(size_t)k])
            harm += p;
         else
            alias += p;
      }
      fund = std::max(fund, 1.0e-30);
      AliasMeasurement m;
      m.aliasDb = 10.0 * std::log10(std::max(alias, 1.0e-30) / fund);
      m.harmonicDb = 10.0 * std::log10(std::max(harm, 1.0e-30) / fund);
      return m;
   }

   struct AliasCase
   {
      const char* label;
      const char* effect;
      std::vector<std::pair<const char*, float>> params; // value as a fraction of the param's range
      double gateDb = 0.0; // 0 = report only; otherwise the case must reach this
      bool control = false;
      int fundBin = kFundBin; // odd; 171 = 501 Hz
      int carrierBin = 0;     // Ring Mod: carrier on this (odd) bin, see Measure
   };

   bool RunCase(const AliasCase& c, AliasMeasurement& out)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == c.effect)
            def = &d;
      if (def == nullptr)
         return false;

      auto node = std::make_unique<AudioEffectNode>(*def);
      node->mix = 1.0f;
      for (const auto& [name, frac] : c.params)
      {
         const EffectParamDef* pd = FindEffectParamDef(c.effect, name);
         float* p = node->ParamPtr(name);
         if (pd == nullptr || p == nullptr)
            return false;
         *p = pd->minVal + frac * (pd->maxVal - pd->minVal);
      }
      if (c.carrierBin > 0)
         *node->ParamPtr("freq") = (float)(c.carrierBin * kRate / kN);
      node->GetAudioNode()->PrepareToPlay(kRate, 512);
      node->CookIfNeeded(1);

      const double omega = 2.0 * M_PI * (double)c.fundBin / (double)kN;
      const int warm = (int)(0.3 * kRate);
      std::vector<std::vector<float>> rendered;
      DelayTest::RunSamples(
         *node, warm + kN, 1,
         [omega](int i, int) { return kAmplitude * (float)std::sin(omega * (double)i); }, &rendered);
      std::vector<float> tail(rendered[0].begin() + warm, rendered[0].end());
      out = Measure(tail, c.fundBin, c.carrierBin);
      return true;
   }

   // A synth voice measured the same way: AnalogNode free-running (no note
   // cable) at a frequency on an odd FFT bin, analog drift/hiss off so the
   // noise floor is not read as aliasing, filter open unless the case says.
   struct SynthCase
   {
      const char* label;
      int fundBin;
      std::function<void(AnalogNode&)> setup;
      double gateDb = 0.0;
   };

   // Renders any free-running IAudioSource-style node (stereo out, no
   // inputs) after warm-up and measures its left channel.
   template <typename Node>
   AliasMeasurement MeasureSource(Node& node, int fundBin)
   {
      node.GetAudioNode()->PrepareToPlay(kRate, 512);
      node.CookIfNeeded(1);

      constexpr int kBlock = 256;
      std::vector<float> l(kBlock), r(kBlock), all;
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer out;
      out.channels = chans;
      out.numChannels = 2;
      out.numFrames = kBlock;
      const int warm = (int)(0.3 * kRate);
      while ((int)all.size() < warm + kN)
      {
         node.GetAudioNode()->ProcessBlock(nullptr, 0, out);
         all.insert(all.end(), l.begin(), l.end());
      }
      std::vector<float> tail(all.begin() + warm, all.begin() + warm + kN);
      return Measure(tail, fundBin);
   }

   AliasMeasurement RunSynthCase(const SynthCase& c)
   {
      AnalogNode node;
      node.freq = (float)((double)c.fundBin * kRate / (double)kN);
      node.analog = false;
      node.filterType = kAFilterOff;
      node.oscMix = 0.0f; // osc1 only unless the case mixes osc2 in
      node.sub = 0.0f;
      node.noise = 0.0f;
      node.voices = 1.0f;
      node.volume = 0.5f;
      c.setup(node);
      return MeasureSource(node, c.fundBin);
   }

   // The Wavetable synth (and the Oscillator node, which runs the same
   // WavetableSynthCore): engine A only, free-running, filter off.
   struct WavetableCase
   {
      const char* label;
      int fundBin;
      std::function<void(WavetableEngine&)> setup;
      double gateDb = 0.0;
   };

   AliasMeasurement RunWavetableCase(const WavetableCase& c)
   {
      WavetableNode node;
      node.frequency = (float)((double)c.fundBin * kRate / (double)kN);
      node.mix = 0.0f;
      node.volume = 0.5f;
      node.engines[1].on = false;
      WavetableEngine& e = node.engines[0];
      e.unison = 1;
      e.filterType = SynthModes::kFilterOff;
      c.setup(e);
      return MeasureSource(node, c.fundBin);
   }
}

bool RunAliasTest()
{

   // Fractions of each param's declared range, so a range change in
   // EffectDefs.cpp moves the probe with it instead of silently clamping.
   const std::vector<AliasCase> cases = {
      { "Stereo (linear control)", "Stereo", {}, 0.0, true },
      // Drive: 8x minimum-phase oversampling + first-order ADAA (DriveKernel.h).
      // Gates sit about 5 dB under what it measures (2026-10-14), which is
      // what the 73-sample linear-phase oversampler measured or better.
      { "Drive tanh, drive 50%", "Drive", { { "drive", 0.5f }, { "color", 0.0f } }, -118.0 },
      { "Drive atan, drive 50%", "Drive", { { "drive", 0.5f }, { "color", 1.0f } }, -115.0 },
      { "Drive tanh, drive 100%", "Drive", { { "drive", 1.0f }, { "color", 0.0f } }, -64.0 },
      { "Drive asymmetric, drive 50%", "Drive", { { "drive", 0.5f }, { "bias", 0.75f } }, -114.0 },
      // Saturator (2.17): Drive's 8x min-phase + ADAA, per stage. Model and
      // routing are fractions too: model k is k/6, routing k/4. Gates about
      // 5 dB under what it measures (2026-10-14). Three stages in series
      // stack 54 dB of drive inside one 8x domain, so the third stage's
      // harmonics fold there: -46 dB, a measured miss against Drive.
      { "Saturator tube, 50%", "Saturator", { { "model1", 1.0f / 6.0f }, { "amount1", 0.5f } }, -118.0 },
      { "Saturator tape, 50%", "Saturator", { { "model1", 2.0f / 6.0f }, { "amount1", 0.5f } }, -121.0 },
      { "Saturator diode, 50%", "Saturator", { { "model1", 3.0f / 6.0f }, { "amount1", 0.5f } }, -100.0 },
      { "Saturator fold, 50%", "Saturator", { { "model1", 4.0f / 6.0f }, { "amount1", 0.5f } }, -100.0 },
      // Rectify is report-only: it removes the fundamental the probe
      // measures against (|x| of a sine is all even harmonics), so its
      // ratio reads +80 dB whatever the aliasing is.
      { "Saturator rectify, 50%", "Saturator", { { "model1", 5.0f / 6.0f }, { "amount1", 0.5f } }, 0.0 },
      { "Saturator crush, 50%", "Saturator", { { "model1", 1.0f }, { "amount1", 0.5f } }, -81.0 },
      { "Saturator tube>tape>diode, 50%", "Saturator",
        { { "model1", 1.0f / 6.0f }, { "model2", 2.0f / 6.0f }, { "model3", 3.0f / 6.0f },
          { "amount1", 0.5f }, { "amount2", 0.5f }, { "amount3", 0.5f } }, -41.0 },
      { "Saturator 3-band, 50%", "Saturator",
        { { "routing", 0.75f }, { "model1", 1.0f / 6.0f }, { "model2", 2.0f / 6.0f }, { "model3", 3.0f / 6.0f },
          { "amount1", 0.5f }, { "amount2", 0.5f }, { "amount3", 0.5f } }, -103.0 },
      { "Saturator feedback 0.5, 50%", "Saturator",
        { { "routing", 1.0f }, { "feedback", 0.5f / 0.95f }, { "amount1", 0.5f } }, -69.0 },
      // Wavetable Shaper: 8x oversampling + slew-tracking mip (WavetableShaperKernel.h).
      // Before (same metric): -2.9 dB at 50% drive, +10.3 dB at 100%.
      { "Wavetable Shaper, drive 50%", "Wavetable Shaper", { { "drive", 0.5f } }, -112.0 },
      { "Wavetable Shaper, drive 100%", "Wavetable Shaper", { { "drive", 1.0f } }, -96.0 },
      { "Dynamics analog, -30 dB / 8:1", "Dynamics",
        { { "analog", 1.0f }, { "threshold", 0.5f }, { "ratio", 7.0f / 19.0f } } },
      // Cycle Shaper: phase-locked oscillator on band-limited tables
      // (CycleShaperKernel.h). Before: -10 dB on the default sine at 5 kHz.
      // At 5 kHz a cycle is under 10 samples, so cycle tracking sets the
      // floor; at musical pitches it is far lower. Linear crossing/peak
      // estimates read -50 here; the quintic interpolant reads -75.
      { "Cycle Shaper sine (default)", "Cycle Shaper", {}, -70.0 },
      { "Cycle Shaper square, smooth 0", "Cycle Shaper", { { "waveform", 0.5f }, { "smooth", 0.0f } }, -70.0 },
      { "Cycle Shaper triangle, smooth 0", "Cycle Shaper", { { "waveform", 1.0f }, { "smooth", 0.0f } }, -70.0 },
      { "Cycle Shaper sine @ 500 Hz", "Cycle Shaper", {}, -105.0, false, 171 },
      { "Cycle Shaper square @ 500 Hz", "Cycle Shaper", { { "waveform", 0.5f }, { "smooth", 0.0f } }, -75.0, false,
        171 },
      { "Bitcrush (aliasing is the effect)", "Bitcrush", {} },
      // Carrier 618 Hz (bin 211). Before 4x oversampling (same metric):
      // square -38, saw -36, square @ 500 Hz -41 dB. Analog mode's diode
      // dead zone is a hard kink, so some of its products still fold at 4x.
      { "Ring Mod square carrier", "Ring Mod", { { "waveform", 1.0f } }, -75.0, false, kFundBin, 211 },
      { "Ring Mod saw carrier", "Ring Mod", { { "waveform", 2.0f / 3.0f } }, -75.0, false, kFundBin, 211 },
      { "Ring Mod square, analog", "Ring Mod", { { "waveform", 1.0f }, { "analog", 1.0f } }, -40.0, false, kFundBin,
        211 },
      { "Ring Mod square @ 500 Hz input", "Ring Mod", { { "waveform", 1.0f } }, -78.0, false, 171, 211 },
   };

   bool all = true;
   int gated = 0;
   for (const AliasCase& c : cases)
   {
      AliasMeasurement m;
      if (!RunCase(c, m))
      {
         printf("ALIASTEST %s: could not build the case  FAIL\n", c.label);
         all = false;
         continue;
      }
      const char* verdict = "info";
      if (c.control)
      {
         const bool ok = m.aliasDb <= kControlDb;
         verdict = ok ? "control OK" : "control FAIL";
         all &= ok;
      }
      else if (c.gateDb < 0.0)
      {
         const bool ok = m.aliasDb <= c.gateDb;
         static char buf[48];
         snprintf(buf, sizeof(buf), "gate %.0f dB %s", c.gateDb, ok ? "OK" : "FAIL");
         verdict = buf;
         all &= ok;
         gated++;
      }
      printf("ALIASTEST %-36s alias<20k %7.1f dB  harmonics %7.1f dB  (re fundamental, -6 dBFS, 48 kHz)  %s\n",
             c.label, m.aliasDb, m.harmonicDb, verdict);
   }
   // Analog synth voice (AnalogSynthCore.h). Bin 597 = 1749 Hz (~A6), 75 = 220 Hz.
   const std::vector<SynthCase> synthCases = {
      // Before (same metric): saw -29 / -37 dB (1.75 kHz / 220 Hz), square -30,
      // triangle -50, sync -12 / -22, drive 100% -21, ladder 8 kHz res 0.5
      // +13.6 (a limit cycle, not aliasing). Then: BLEP/BLAMP oscillators
      // (BlepOscillator.h), exact ZDF ladder, drive + filter at 2x with ADAA:
      // saw -69 / -78, square -70, triangle -103, sync -69 / -76, FM -58,
      // drive 100% -51, ladder -49. Now (2026-10-13, node-audits F2): narrow-
      // band oscillators and drive + filter at 4x: saw -89 / -97, square -88,
      // triangle -123, sync -87 / -96, FM -59, drive 100% -80, ladder -73.
      // Gates sit about 4 dB under "now", so a return to the wide kernel or
      // to 2x fails here.
      { "Analog saw 1.75 kHz", 597, [](AnalogNode& n) { n.wave1 = kAWaveSaw; }, -85.0 },
      { "Analog square 1.75 kHz", 597, [](AnalogNode& n) { n.wave1 = kAWaveSquare; }, -84.0 },
      { "Analog triangle 1.75 kHz", 597, [](AnalogNode& n) { n.wave1 = kAWaveTriangle; }, -115.0 },
      { "Analog saw 220 Hz", 75, [](AnalogNode& n) { n.wave1 = kAWaveSaw; }, -93.0 },
      { "Analog hard sync (osc2 +7 st) 1.75 kHz", 597,
        [](AnalogNode& n) {
           n.sync = true;
           n.semi2 = 7.0f;
           n.oscMix = 1.0f; // hear the synced slave
        },
        -83.0 },
      { "Analog hard sync (osc2 +7 st) 220 Hz", 75,
        [](AnalogNode& n) {
           n.sync = true;
           n.semi2 = 7.0f;
           n.oscMix = 1.0f;
        },
        -91.0 },
      // Zero-mean modulator, bounded depth (FmRatio): the output stays periodic at the note.
      // Gate -54 -> -51 dB when FM went from linear 1 + 2.5 fm to the bounded depth: measures -52.7.
      { "Analog FM 0.3 (sine osc2 -> saw) 1.75 kHz", 597,
        [](AnalogNode& n) {
           n.wave2 = kAWaveSine;
           n.fm = 0.3f;
        },
        -51.0 },
      { "Analog saw + drive 100% 1.75 kHz", 597, [](AnalogNode& n) { n.drive = 1.0f; }, -76.0 },
      { "Analog saw -> ladder 8 kHz res 0.5 1.75 kHz", 597,
        [](AnalogNode& n) {
           n.filterType = kAFilterLadder;
           n.cutoff = 8000.0f;
           n.resonance = 0.5f;
           n.drive = 0.5f;
        },
        -69.0 },
   };
   for (const SynthCase& c : synthCases)
   {
      const AliasMeasurement m = RunSynthCase(c);
      const char* verdict = "info";
      static char buf[48];
      if (c.gateDb < 0.0)
      {
         const bool ok = m.aliasDb <= c.gateDb;
         snprintf(buf, sizeof(buf), "gate %.0f dB %s", c.gateDb, ok ? "OK" : "FAIL");
         verdict = buf;
         all &= ok;
         gated++;
      }
      printf("ALIASTEST %-36s alias<20k %7.1f dB  harmonics %7.1f dB  (re fundamental)  %s\n", c.label, m.aliasDb,
             m.harmonicDb, verdict);
   }

   // Wavetable synth / Oscillator node (table 0 is a pure sine, so a warp's
   // harmonics are all its own). Before, same metric: sync -8.4 / -17.2 dB
   // (1.75 kHz / 220 Hz), bend -43.4, mirror -34.4, quantize -27.5, PD -54.1,
   // rectify -42.7; on a saw table sync -2.4, bend -24.9. Now: read-speed mip
   // for phase warps + BLEP/BLAMP at each warp's breakpoints. Asym (a cusp,
   // see WarpBreakpoints) is reported, not gated.
   using SM = SynthModes::WarpMode;
   auto warp = [](int mode, float amount, float ratio = 1.0f) {
      return [=](WavetableEngine& e) {
         e.warpMode = mode;
         e.warpAmount = amount;
         e.warpRatio = ratio;
      };
   };
   const std::vector<WavetableCase> wtCases = {
      { "Wavetable plain 1.75 kHz", 597, warp(SM::kWarpOff, 0.0f), -110.0 },
      { "Wavetable plain 220 Hz", 75, warp(SM::kWarpOff, 0.0f), -110.0 },
      { "Wavetable sync x2.7 1.75 kHz", 597, warp(SM::kWarpSync, 1.0f, 2.7f), -42.0 },
      { "Wavetable sync x2.7 220 Hz", 75, warp(SM::kWarpSync, 1.0f, 2.7f), -88.0 },
      { "Wavetable bend+ 100% 1.75 kHz", 597, warp(SM::kWarpBendPlus, 1.0f), -78.0 },
      { "Wavetable asym+ 100% 1.75 kHz", 597, warp(SM::kWarpAsymPlus, 1.0f) },
      { "Wavetable mirror 100% 1.75 kHz", 597, warp(SM::kWarpMirror, 1.0f), -70.0 },
      { "Wavetable quantize 50% 1.75 kHz", 597, warp(SM::kWarpQuantize, 0.5f), -88.0 },
      { "Wavetable PD 50% (op x1) 1.75 kHz", 597, warp(SM::kWarpPD, 0.5f, 1.0f), -66.0 },
      { "Wavetable FM 30% (op x1) 1.75 kHz", 597, warp(SM::kWarpFM, 0.3f, 1.0f), -108.0 },
      { "Wavetable rectify 50% 1.75 kHz", 597, warp(SM::kWarpRectify, 0.5f), -80.0 },
      // v0.6 2.26: the PhaseShape warps shared with Field. Pinch's read has
      // matching slopes but opposite curvature across the wrap, which no
      // BLAMP corrects - it is the weakest of the gated warps.
      { "Wavetable formant x2.7 1.75 kHz", 597, warp(SM::kWarpFormant, 1.0f, 2.7f), -58.0 },
      { "Wavetable formant x2.7 220 Hz", 75, warp(SM::kWarpFormant, 1.0f, 2.7f), -86.0 },
      { "Wavetable pinch 100% 1.75 kHz", 597, warp(SM::kWarpPinch, 1.0f), -46.0 },
      { "Wavetable skew 70% 1.75 kHz", 597, warp(SM::kWarpSkew, 0.7f), -63.0 },
      // A bright table: "Multi Saw" (24), so read-speed aliasing shows too.
      { "Wavetable[saw] plain 1.75 kHz", 597, [](WavetableEngine& e) { e.table = 24; }, -90.0 },
      { "Wavetable[saw] sync x2.7 1.75 kHz", 597,
        [](WavetableEngine& e) {
           e.table = 24;
           e.warpMode = SynthModes::kWarpSync;
           e.warpAmount = 1.0f;
           e.warpRatio = 2.7f;
        },
        -37.0 },
      { "Wavetable[saw] asym+ 100% 1.75 kHz", 597,
        [](WavetableEngine& e) {
           e.table = 24;
           e.warpMode = SynthModes::kWarpAsymPlus;
           e.warpAmount = 1.0f;
        } },
      { "Wavetable[saw] bend+ 100% 1.75 kHz", 597,
        [](WavetableEngine& e) {
           e.table = 24;
           e.warpMode = SynthModes::kWarpBendPlus;
           e.warpAmount = 1.0f;
        },
        -38.0 },
   };
   for (const WavetableCase& c : wtCases)
   {
      const AliasMeasurement m = RunWavetableCase(c);
      const char* verdict = "info";
      static char buf[48];
      if (c.gateDb < 0.0)
      {
         const bool ok = m.aliasDb <= c.gateDb;
         snprintf(buf, sizeof(buf), "gate %.0f dB %s", c.gateDb, ok ? "OK" : "FAIL");
         verdict = buf;
         all &= ok;
         gated++;
      }
      printf("ALIASTEST %-36s alias<20k %7.1f dB  harmonics %7.1f dB  (re fundamental)  %s\n", c.label, m.aliasDb,
             m.harmonicDb, verdict);
   }

   printf("ALIASTEST %d gated case(s) at their limits  %s\n", gated, all ? "OK" : "FAIL");
   return all;
}

// MinPhaseOversampler.h on its own, then the two effects built on it.
//   - a round trip passes 20 Hz - 20 kHz flat;
//   - Up() leaves no image above -115 dB, Down() folds nothing back above it;
//   - Latency() is the round trip's real low-frequency delay;
//   - Drive and Wavetable Shaper report at most 6 samples of latency;
//   - at mix 50% each is as flat as at mix 100% (the dry half is phase
//     matched; mixing against a plainly delayed dry would notch the highs).
namespace
{
   // Power of `x` at `cycles` whole cycles across it, and of everything else.
   void SplitTone(const std::vector<double>& x, double cycles, double& tone, double& rest, double* phase = nullptr)
   {
      const size_t n = x.size();
      double c = 0.0, s = 0.0, total = 0.0;
      for (size_t i = 0; i < n; i++)
      {
         const double a = 2.0 * M_PI * cycles * (double)i / (double)n;
         c += x[i] * std::cos(a);
         s += x[i] * std::sin(a);
         total += x[i] * x[i];
      }
      c *= 2.0 / (double)n;
      s *= 2.0 / (double)n;
      tone = 0.5 * (c * c + s * s);
      rest = std::max(0.0, total / (double)n - tone);
      if (phase != nullptr)
         *phase = std::atan2(c, s); // of A sin(a + phase)
   }

   double Db(double ratio) { return 10.0 * std::log10(std::max(ratio, 1.0e-30)); }

   bool OversamplerAtRate(double rate)
   {
      constexpr int kLen = 4096;
      constexpr int kWarm = 4096;
      constexpr int kF = 8;
      bool ok = true;
      MinPhaseOversampler os;
      os.Prepare(kF, rate);

      // Up(): a 15 kHz sine, everything but the sine itself is image.
      {
         os.Reset();
         const double cycles = std::round(15000.0 * kLen / rate);
         std::vector<double> y;
         for (int i = 0; i < kWarm + kLen; i++)
         {
            float o[kF];
            os.Up(0, (float)(0.5 * std::sin(2.0 * M_PI * cycles * (double)i / (double)kLen)), o);
            if (i >= kWarm)
               y.insert(y.end(), o, o + kF);
         }
         double tone, rest;
         SplitTone(y, cycles, tone, rest);
         const double db = Db(rest / tone);
         const bool pass = db <= -115.0;
         ok &= pass;
         printf("OVERSAMPLETEST %6.0f Hz  up images        %7.1f dB  (<= -115)  %s\n", rate, db, pass ? "OK" : "FAIL");
      }

      // Down(): tones above the base Nyquist that would land in the audible
      // band (17, 5, 5 and 3 kHz), across every stage's stopband.
      for (double hz : { rate - 17000.0, 2.0 * rate - 5000.0, 3.0 * rate + 5000.0, 4.0 * rate - 3000.0 })
      {
         os.Reset();
         const double ratio = hz / rate;
         const double cycles = std::round(ratio * kLen);
         double sum = 0.0;
         for (int i = 0; i < kWarm + kLen; i++)
         {
            float in[kF];
            for (int k = 0; k < kF; k++)
               in[k] = (float)(0.5 * std::sin(2.0 * M_PI * cycles * (double)(i * kF + k) / (double)(kLen * kF)));
            const double v = os.Down(0, in);
            if (i >= kWarm)
               sum += v * v;
         }
         const double db = Db((sum / kLen) / 0.125);
         const bool pass = db <= -115.0;
         ok &= pass;
         printf("OVERSAMPLETEST %6.0f Hz  down, %.2f fs in   %7.1f dB  (<= -115)  %s\n", rate, ratio, db, pass ? "OK" : "FAIL");
      }

      // Round trip: flat passband, and the delay Latency() claims.
      double worst = 0.0, lowDelay = 0.0;
      for (double hz : { 20.0, 200.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0 })
      {
         os.Reset();
         const int len = (hz < 100.0) ? 8 * kLen : kLen;
         const double cycles = std::max(1.0, std::round(hz * len / rate));
         std::vector<double> y;
         for (int i = 0; i < kWarm + len; i++)
         {
            float o[kF];
            os.Up(0, (float)(0.5 * std::sin(2.0 * M_PI * cycles * (double)i / (double)len)), o);
            const double v = os.Down(0, o);
            if (i >= kWarm)
               y.push_back(v);
         }
         double tone, rest, phase;
         SplitTone(y, cycles, tone, rest, &phase);
         worst = std::max(worst, std::fabs(Db(tone / 0.125)));
         if (hz == 200.0)
         {
            // kWarm is a whole number of cycles, so the lag is the phase itself.
            lowDelay = -phase / (2.0 * M_PI * cycles / (double)len);
         }
      }
      {
         const bool pass = worst <= 0.01;
         ok &= pass;
         printf("OVERSAMPLETEST %6.0f Hz  passband 20-20k   %7.4f dB  (<= 0.01)  %s\n", rate, worst, pass ? "OK" : "FAIL");
         const bool delayOk = std::fabs(lowDelay - (double)os.Latency()) <= 0.6 && os.Latency() <= 6;
         ok &= delayOk;
         printf("OVERSAMPLETEST %6.0f Hz  delay %.2f samples, reports %d (sections %d/%d/%d)  %s\n", rate, lowDelay,
                os.Latency(), os.Sections(0), os.Sections(1), os.Sections(2), delayOk ? "OK" : "FAIL");
      }
      return ok;
   }

   // Level of `effect` at `hz` with a -60 dBFS sine in (small enough that
   // the curve is a plain gain), in dB re the input.
   bool SmallSignalGain(const char* effect, float mix, double hz, double& gainDb)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == effect)
            def = &d;
      if (def == nullptr)
         return false;
      auto node = std::make_unique<AudioEffectNode>(*def);
      node->mix = mix;
      node->GetAudioNode()->PrepareToPlay(kRate, 512);
      node->CookIfNeeded(1);
      static constexpr int kLen = 4096;
      const int warm = (int)(0.3 * kRate);
      const double cycles = std::round(hz * kLen / kRate);
      std::vector<std::vector<float>> rendered;
      DelayTest::RunSamples(
         *node, warm + kLen, 1,
         [cycles](int i, int) { return 0.001f * (float)std::sin(2.0 * M_PI * cycles * (double)i / (double)kLen); },
         &rendered);
      std::vector<double> tail(rendered[0].begin() + warm, rendered[0].end());
      double tone, rest;
      SplitTone(tail, cycles, tone, rest);
      gainDb = Db(tone / (0.5 * 0.001 * 0.001));
      return true;
   }
}

bool RunOversampleTest()
{
   bool all = true;
   for (double rate : { 44100.0, 48000.0, 96000.0 })
      all &= OversamplerAtRate(rate);

   for (const char* effect : { "Drive", "Wavetable Shaper", "Saturator" })
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == effect)
            def = &d;
      if (def == nullptr)
      {
         printf("OVERSAMPLETEST %s: no such effect  FAIL\n", effect);
         all = false;
         continue;
      }
      {
         auto node = std::make_unique<AudioEffectNode>(*def);
         node->GetAudioNode()->PrepareToPlay(kRate, 512);
         node->CookIfNeeded(1);
         const int latency = node->LatencySamples();
         const bool pass = latency <= 6;
         all &= pass;
         printf("OVERSAMPLETEST %-17s latency %d samples at 48 kHz  (<= 6)  %s\n", effect, latency, pass ? "OK" : "FAIL");
      }
      // How far the response at mix 50% strays from the response at mix
      // 100%, 1 kHz as the reference for both.
      double worst = 0.0;
      double ref[2] = {};
      double lastWet = 0.0;
      bool built = true;
      for (double hz : { 1000.0, 3000.0, 6000.0, 10000.0, 14000.0, 18000.0 })
         for (int m = 0; m < 2; m++)
         {
            double g = 0.0;
            built &= SmallSignalGain(effect, m == 0 ? 1.0f : 0.5f, hz, g);
            if (hz == 1000.0)
               ref[m] = g;
            if (m == 1)
               worst = std::max(worst, std::fabs((g - ref[1]) - lastWet + ref[0]));
            else
               lastWet = g;
         }
      const bool pass = built && worst <= 0.1;
      all &= pass;
      printf("OVERSAMPLETEST %-17s mix 50%% vs 100%%, 1-18 kHz: %.3f dB apart  (<= 0.1)  %s\n", effect, worst,
             pass ? "OK" : "FAIL");
   }
   printf("OVERSAMPLETEST %s\n", all ? "OK" : "FAIL");
   return all;
}
// INFINITE_FIELDALIASTEST: phase 0 of docs/research/aliasing-aware-field.
// Ground truth for an aliasing-aware Field compiler: each kernel in a
// corpus is rendered by the real Field Effect node at F x 48 kHz
// (F = 1, 2, 4, 8, 16, 32), decimated back to 48 kHz with the same
// minimum-phase half-band chain the effects use, and measured with the
// rig's metric above. The curve alias(F) per kernel is what a static
// analysis has to predict (H1) and what a per-subgraph factor choice is
// costed against (H2). Report only, no gates, except the linear control.
//
// The kernel sees sr = F x 48 kHz, so sr-relative code (oscillators, the
// presets' tone filters) means the same thing at every factor. Kernels whose
// behaviour is counted in samples (a sample-and-hold decimator) are left
// out: oversampling changes what they do, not only how they alias.
namespace
{
   struct FieldAliasCase
   {
      std::string label;
      std::string klass; // linear, poly, kink, jump, analytic, chain, feedback, osc, preset
      std::string code;
      int fundBin = kFundBin;
      bool usesInput = true;
   };

   constexpr int kFieldFactors[] = { 1, 2, 4, 8, 16, 32 };

   // Renders `code` at factor x kRate and returns kN base-rate samples after
   // warm-up, or an empty vector with `err` set.
   std::vector<float> RenderFieldAt(const FieldAliasCase& c, int factor, std::string& err)
   {
      FieldSampleNode node;
      node.code = c.code;
      if (!node.Apply())
      {
         err = node.LastError();
         return {};
      }
      // Apply() keeps a param's value across recompiles by name, and the
      // node is born on preset 0 (Tube Overdrive: drive 3.0, tone 4000), so
      // a kernel declaring `drive = 2.5` would run at 3.0. Every kernel here
      // is measured at its own declared defaults.
      for (Field::ParamEntry& p : node.GetParamTable().Params())
         if (p.isDeclared)
            p.value = p.defaultValue;
      const double rate = kRate * (double)factor;
      constexpr int kBlock = 256;
      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay(rate, kBlock);
      node.CookIfNeeded(1);

      // Decimator: one min-phase chain for F <= 8; for 16 and 32 a 2x or 4x
      // chain at 8 x 48 kHz first, then the 8x chain.
      std::unique_ptr<MinPhaseOversampler> pre, post;
      int preFactor = 1, postFactor = factor;
      if (factor > 8)
      {
         preFactor = factor / 8;
         postFactor = 8;
         pre = std::make_unique<MinPhaseOversampler>();
         pre->Prepare(preFactor, kRate * 8.0);
      }
      if (postFactor > 1)
      {
         post = std::make_unique<MinPhaseOversampler>();
         post->Prepare(postFactor, kRate);
      }

      const double omega = 2.0 * M_PI * (double)c.fundBin / (double)kN / (double)factor;
      const int warm = (int)(0.3 * kRate);
      const int baseTotal = warm + kN;
      const long long hiTotal = (long long)baseTotal * factor;

      std::vector<float> inData(kBlock), outData(kBlock);
      float* inChans[1] = { inData.data() };
      float* outChans[1] = { outData.data() };
      AudioBuffer inBuf;
      inBuf.channels = inChans;
      inBuf.numChannels = 1;
      inBuf.numFrames = kBlock;
      AudioBuffer outBuf;
      outBuf.channels = outChans;
      outBuf.numChannels = 1;
      outBuf.numFrames = kBlock;
      const AudioBuffer* inputs[1] = { &inBuf };

      std::vector<float> base;
      base.reserve((size_t)baseTotal);
      std::vector<float> hiFrame((size_t)factor), midFrame(8);
      int hiFill = 0, midFill = 0;
      for (long long done = 0; done < hiTotal; done += kBlock)
      {
         for (int i = 0; i < kBlock; i++)
            inData[(size_t)i] = c.usesInput ? kAmplitude * (float)std::sin(omega * (double)(done + i)) : 0.0f;
         an->ProcessBlock(inputs, 1, outBuf);
         for (int i = 0; i < kBlock; i++)
         {
            const float y = outData[(size_t)i];
            if (factor == 1)
            {
               base.push_back(y);
               continue;
            }
            if (pre)
            {
               hiFrame[(size_t)hiFill++] = y;
               if (hiFill < preFactor)
                  continue;
               hiFill = 0;
               midFrame[(size_t)midFill++] = pre->Down(0, hiFrame.data());
               if (midFill < 8)
                  continue;
               midFill = 0;
               base.push_back(post->Down(0, midFrame.data()));
            }
            else
            {
               hiFrame[(size_t)hiFill++] = y;
               if (hiFill < postFactor)
                  continue;
               hiFill = 0;
               base.push_back(post->Down(0, hiFrame.data()));
            }
         }
      }
      if ((int)base.size() < baseTotal)
      {
         err = "short render";
         return {};
      }
      return std::vector<float>(base.begin() + warm, base.begin() + warm + kN);
   }

   std::vector<FieldAliasCase> FieldAliasCorpus()
   {
      std::vector<FieldAliasCase> cs;
      auto add = [&](std::string label, std::string klass, std::string code) {
         cs.push_back({ std::move(label), std::move(klass), std::move(code) });
      };
      auto g = [](double v) {
         char b[32];
         snprintf(b, sizeof(b), "%.4g", v);
         return std::string(b);
      };

      add("linear gain", "linear", "out = in * 0.7\n");
      add("one-pole lowpass", "linear", "state float y = 0\ny = y + 0.2 * (in - y)\nout = y\n");

      // Polynomials: degree d spreads a tone to d x f0.
      for (double a : { 0.5, 1.0 })
         add("poly d2 a" + g(a), "poly", "out = in + " + g(a) + " * in * in\n");
      for (double d : { 1.0, 2.0 })
         add("poly d3 cubic clip drive " + g(d), "poly",
             "x = in * " + g(d) + "\nout = x - x * x * x / 3.0\n");
      add("poly d5", "poly", "x = in * 1.5\nout = x - (x ^ 5.0) * 0.2\n");
      add("poly d7", "poly", "x = in * 1.5\nout = x - (x ^ 7.0) * 0.1\n");
      add("poly d9 cubic of cubic", "chain",
          "x = in * 1.5\ny = x - x * x * x / 3.0\nout = y - y * y * y / 3.0\n");
      add("poly d27 cubic x3", "chain",
          "x = in * 1.5\ny = x - x * x * x / 3.0\nz = y - y * y * y / 3.0\nout = z - z * z * z / 3.0\n");

      // Analytic, not polynomial: tanh (via exp), sin folder, rational clip.
      for (double d : { 1.0, 2.0, 4.0, 8.0 })
         add("tanh drive " + g(d), "analytic",
             "x = in * " + g(d) + "\ne = exp(2.0 * x)\nout = (e - 1.0) / (e + 1.0) * 0.5\n");
      for (double d : { 1.0, 2.0, 4.0, 8.0 })
         add("sin fold drive " + g(d), "analytic", "out = sin(in * " + g(d) + " * 3.14159) * 0.5\n");
      for (double d : { 2.0, 8.0 })
         add("rational x/(1+|x|) drive " + g(d), "kink",
             "x = in * " + g(d) + "\nout = x / (1.0 + abs(x)) * 0.5\n");

      // Kinks: continuous value, discontinuous slope.
      add("abs mix", "kink", "out = in + 0.5 * abs(in)\n");
      add("half-wave max", "kink", "out = in * 0.5 + max(in, 0.0)\n");
      for (double d : { 1.0, 2.0, 4.0, 8.0 })
         add("hard clip drive " + g(d), "kink",
             "out = clamp(in * " + g(d) + ", -0.4, 0.4)\n");

      // Jumps: discontinuous value.
      for (double s : { 4.0, 16.0 })
         add("quantize " + g(s) + " steps", "jump", "out = floor(in * " + g(s) + " + 0.5) / " + g(s) + "\n");
      add("comparator square + dry", "jump", "out = in * 0.5 + if(in > 0.0, 0.25, -0.25)\n");
      add("wrap fold mod", "jump", "x = in * 2.0 + 4.0\nout = (x % 1.0) - 0.5\n");

      // Chains of analytic stages: the Saturator three-stage case in Field.
      add("tanh x3 chain drive 4", "chain",
          "x = in * 4.0\ne1 = exp(2.0 * x)\ny1 = (e1 - 1.0) / (e1 + 1.0) * 4.0\n"
          "e2 = exp(2.0 * y1)\ny2 = (e2 - 1.0) / (e2 + 1.0) * 4.0\n"
          "e3 = exp(2.0 * y2)\nout = (e3 - 1.0) / (e3 + 1.0) * 0.5\n");
      add("clip then cubic", "chain",
          "x = clamp(in * 3.0, -1.0, 1.0)\nout = (x - x * x * x / 3.0) * 0.5\n");

      // Nonlinearity inside state feedback.
      for (double fb : { 0.3, 0.7 })
         add("tanh feedback " + g(fb), "feedback",
             "state float y = 0\nx = in * 2.0 + y * " + g(fb) + "\ne = exp(2.0 * x)\n"
             "y = (e - 1.0) / (e + 1.0)\nout = y * 0.5\n");
      add("clip feedback 0.5", "feedback",
          "state float y = 0\ny = clamp(in * 2.0 + y * 0.5, -0.6, 0.6)\nout = y\n");

      // Free-running oscillators (no input): naive phase accumulators.
      auto osc = [&](std::string label, std::string klass, std::string body) {
         FieldAliasCase c;
         c.label = std::move(label);
         c.klass = std::move(klass);
         c.code = "state float ph = 0\nph = ph + 1748.84765625 / sr\nph = ph - floor(ph)\n" + body;
         c.fundBin = 597;
         c.usesInput = false;
         cs.push_back(std::move(c));
      };
      osc("osc sine 1.75k", "linear", "out = sin(ph * 6.2831853) * 0.5\n");
      osc("osc naive saw 1.75k", "osc", "out = (ph * 2.0 - 1.0) * 0.5\n");
      osc("osc naive square 1.75k", "osc", "out = if(ph < 0.5, 0.5, -0.5)\n");
      osc("osc naive triangle 1.75k", "osc", "out = (abs(ph * 4.0 - 2.0) - 1.0) * 0.5\n");
      osc("osc sine pm (ph^3) 1.75k", "osc", "out = sin(ph * ph * ph * 6.2831853) * 0.5\n");
      osc("osc pinch 100% 1.75k", "osc", "out = sin(pinch(ph, 1.0) * 6.2831853) * 0.5\n");

      // Shipping presets with a static nonlinearity, at their defaults.
      for (const char* name : { "Tube Overdrive", "Soft Saturation", "Analog Tape Saturator", "Bass Ribbon Driver",
                                "Wavefolder", "Moog 4-Pole Ladder Filter" })
         for (const auto& p : FieldSampleNode::Presets())
            if (std::string(p.name) == name)
               add(std::string("preset ") + name, "preset", p.code);
      return cs;
   }
}

namespace
{
   // H1 scoring helpers (phase 1).
   std::vector<double> AverageRanks(const std::vector<double>& v)
   {
      std::vector<size_t> idx(v.size());
      for (size_t i = 0; i < idx.size(); i++)
         idx[i] = i;
      std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
      std::vector<double> r(v.size());
      for (size_t i = 0; i < idx.size();)
      {
         size_t j = i;
         while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]])
            j++;
         for (size_t k = i; k <= j; k++)
            r[idx[k]] = 0.5 * (double)(i + j);
         i = j + 1;
      }
      return r;
   }

   double Spearman(const std::vector<double>& a, const std::vector<double>& b)
   {
      const std::vector<double> ra = AverageRanks(a), rb = AverageRanks(b);
      const double n = (double)a.size();
      double ma = 0.0, mb = 0.0;
      for (size_t i = 0; i < a.size(); i++)
      {
         ma += ra[i] / n;
         mb += rb[i] / n;
      }
      double sab = 0.0, saa = 0.0, sbb = 0.0;
      for (size_t i = 0; i < a.size(); i++)
      {
         sab += (ra[i] - ma) * (rb[i] - mb);
         saa += (ra[i] - ma) * (ra[i] - ma);
         sbb += (rb[i] - mb) * (rb[i] - mb);
      }
      return (saa > 0.0 && sbb > 0.0) ? sab / std::sqrt(saa * sbb) : 0.0;
   }

   double Median(std::vector<double> v)
   {
      if (v.empty())
         return 0.0;
      std::sort(v.begin(), v.end());
      const size_t m = v.size() / 2;
      return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
   }

   // Smallest factor reaching -80 dB, or 0 for "never" within 32x.
   int FactorNeeded(const double* db)
   {
      for (int k = 0; k < 6; k++)
         if (db[k] <= -80.0)
            return kFieldFactors[k];
      return 0;
   }

   constexpr double kScoreFloorDb = -100.0; // = kControlDb: below it is "clean"
}

bool RunFieldAliasTest()
{
   const std::vector<FieldAliasCase> corpus = FieldAliasCorpus();
   FILE* csv = nullptr;
   if (const char* path = getenv("INFINITE_FIELDALIAS_CSV"))
   {
      csv = fopen(path, "w");
      if (csv != nullptr)
         fprintf(csv, "label,class,factor,alias_db,harmonics_db,predicted_db,status,iterations,evaluations\n");
   }
   bool all = true;
   printf("FIELDALIAS %zu kernels, alias<20k dB re fundamental at F x 48 kHz then decimated\n", corpus.size());
   printf("FIELDALIAS %-34s %-9s %7s %7s %7s %7s %7s %7s\n", "kernel", "class", "1x", "2x", "4x", "8x", "16x",
          "32x");
   std::vector<double> allMeas, allPred;
   std::map<std::string, std::vector<double>> errByClass;
   int factorAgree = 0, factorWithin1 = 0, factorKernels = 0, unpredicted = 0;
   double renderSec = 0.0, analyzeSec = 0.0;
   long long analyzeEvals = 0, renderEvals = 0;
   for (const FieldAliasCase& c : corpus)
   {
      double db[6] = {}, pred[6] = {};
      bool ok = true, predOk = true;
      std::string err, why;
      Field::SampleProgram prog;
      Field::FieldError ferr;
      const bool compiled = Field::CompileSampleProgram(c.code, nullptr, prog, ferr);
      Field::AliasContext ctx;
      ctx.f0Hz = (double)c.fundBin * kRate / (double)kN;
      ctx.inAmplitude = kAmplitude;
      ctx.usesInput = c.usesInput;
      int iters[6] = {};
      long long evals[6] = {};
      for (int k = 0; k < 6 && ok; k++)
      {
         const auto t0 = std::chrono::steady_clock::now();
         const std::vector<float> x = RenderFieldAt(c, kFieldFactors[k], err);
         const auto t1 = std::chrono::steady_clock::now();
         renderSec += std::chrono::duration<double>(t1 - t0).count();
         renderEvals += (long long)(0.3 * kRate + kN) * kFieldFactors[k];
         if (x.empty())
         {
            ok = false;
            break;
         }
         const AliasMeasurement m = Measure(x, c.fundBin);
         db[k] = m.aliasDb;

         Field::AliasSpectrum spec;
         const auto t2 = std::chrono::steady_clock::now();
         const bool got = compiled && Field::AnalyzePeriodicSpectrum(prog, ctx, kRate * kFieldFactors[k], spec);
         analyzeSec += std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count();
         analyzeEvals += spec.evaluations;
         iters[k] = spec.iterations;
         evals[k] = spec.evaluations;
         if (got)
            pred[k] = Field::PredictAliasDb(spec, ctx.f0Hz, kRate, kFieldFactors[k], kAudibleHz,
                                            (double)kLobe * kRate / (double)kN);
         else
         {
            predOk = false;
            why = compiled ? spec.status : ferr.message;
         }
         if (csv != nullptr)
            fprintf(csv, "\"%s\",%s,%d,%.2f,%.2f,%.2f,\"%s\",%d,%lld\n", c.label.c_str(), c.klass.c_str(),
                    kFieldFactors[k], m.aliasDb, m.harmonicDb, got ? pred[k] : 0.0, got ? "ok" : spec.status.c_str(),
                    spec.iterations, spec.evaluations);
      }
      if (!ok)
      {
         printf("FIELDALIAS %-34s could not render: %s  FAIL\n", c.label.c_str(), err.c_str());
         all = false;
         continue;
      }
      const char* verdict = "";
      if (c.klass == "linear")
      {
         const bool clean = db[0] <= kControlDb;
         verdict = clean ? "control OK" : "control FAIL";
         all &= clean;
      }
      printf("FIELDALIAS %-34s %-9s %7.1f %7.1f %7.1f %7.1f %7.1f %7.1f  %s\n", c.label.c_str(), c.klass.c_str(),
             db[0], db[1], db[2], db[3], db[4], db[5], verdict);
      if (predOk)
      {
         printf("FIELDALIAS %-34s %-9s %7.1f %7.1f %7.1f %7.1f %7.1f %7.1f  predicted, %d-%d iter\n", "",
                "", pred[0], pred[1], pred[2], pred[3], pred[4], pred[5], *std::min_element(iters, iters + 6),
                *std::max_element(iters, iters + 6));
         if (c.klass != "linear")
         {
            for (int k = 0; k < 6; k++)
            {
               const double mm = std::max(db[k], kScoreFloorDb), pp = std::max(pred[k], kScoreFloorDb);
               allMeas.push_back(mm);
               allPred.push_back(pp);
               errByClass[c.klass].push_back(std::fabs(mm - pp));
               errByClass["(all)"].push_back(std::fabs(mm - pp));
            }
            const int fm = FactorNeeded(db), fp = FactorNeeded(pred);
            factorKernels++;
            factorAgree += fm == fp;
            auto step = [](int f) { return f == 0 ? 6 : (int)std::lround(std::log2((double)f)); };
            factorWithin1 += std::abs(step(fm) - step(fp)) <= 1;
         }
      }
      else
      {
         printf("FIELDALIAS %-34s %-9s no prediction: %s\n", "", "", why.c_str());
         if (c.klass != "linear")
            unpredicted++;
      }
      fflush(stdout);
   }
   if (csv != nullptr)
      fclose(csv);

   // H1: rank agreement and error over nonlinear kernels x factors, both
   // floored at kScoreFloorDb.
   const double rho = Spearman(allMeas, allPred);
   printf("FIELDALIAS H1 %zu points (%d nonlinear kernels predicted, %d not): Spearman rho %.3f (>= 0.8), "
          "median |error| %.1f dB (<= 10)  %s\n",
          allMeas.size(), factorKernels, unpredicted, rho, Median(errByClass["(all)"]),
          (rho >= 0.8 && Median(errByClass["(all)"]) <= 10.0) ? "H1 HOLDS" : "H1 FAILS");
   for (const auto& [klass, errs] : errByClass)
   {
      std::vector<double> sorted = errs;
      std::sort(sorted.begin(), sorted.end());
      printf("FIELDALIAS H1 class %-9s n=%3zu median |err| %5.1f dB, p90 %5.1f dB\n", klass.c_str(), errs.size(),
             Median(errs), sorted[(size_t)(0.9 * (double)(sorted.size() - 1))]);
   }
   printf("FIELDALIAS H1 factor for -80 dB: %d/%d exact, %d/%d within one doubling\n", factorAgree, factorKernels,
          factorWithin1, factorKernels);
   printf("FIELDALIAS cost: render+measure %.1f s (%lld program runs), analysis %.2f s (%lld program runs)\n",
          renderSec, renderEvals, analyzeSec, analyzeEvals);
   printf("FIELDALIAS %s\n", all ? "OK" : "FAIL");
   return all;
}
// INFINITE_FIELDRATEPLAN: phase 2a of docs/research/aliasing-aware-field (H2).
// Each nonlinear corpus kernel is split into segments at its own
// oversampling factors (Field::ChooseRatePlan) and run offline by a
// multi-rate executor: values crossing a cut go down to 48 kHz and up again
// through the effects' min-phase chain. Its measured alias, timbre and
// calibrated cost are set against the whole kernel at the single factor the
// phase-1 analysis predicts. Also: the factor at the corners of an input
// amplitude x frequency range, and held-out Field Effect presets. Report
// only; nothing here touches the live audio path.
namespace
{
   // Between the base rate and F x 48 kHz, F = 1..32, up to 8 channels
   // (16, 32: a cascade through 8 x 48 kHz, as RenderFieldAt).
   struct PlanResampler
   {
      int factor = 1, preFactor = 1;
      std::unique_ptr<MinPhaseOversampler> pre, post;
      float mid[8] = {};

      void Prepare(int f)
      {
         factor = f;
         if (f > 8)
         {
            preFactor = f / 8;
            pre = std::make_unique<MinPhaseOversampler>();
            pre->Prepare(preFactor, kRate * 8.0);
            post = std::make_unique<MinPhaseOversampler>();
            post->Prepare(8, kRate);
         }
         else if (f > 1)
         {
            post = std::make_unique<MinPhaseOversampler>();
            post->Prepare(f, kRate);
         }
      }

      float Down(int ch, const float* hi)
      {
         if (factor == 1)
            return hi[0];
         if (pre)
         {
            for (int i = 0; i < 8; i++)
               mid[i] = pre->Down(ch, hi + i * preFactor);
            return post->Down(ch, mid);
         }
         return post->Down(ch, hi);
      }

      void Up(int ch, float x, float* hi)
      {
         if (factor == 1)
         {
            hi[0] = x;
            return;
         }
         if (pre)
         {
            post->Up(ch, x, mid);
            for (int i = 0; i < 8; i++)
               pre->Up(ch, mid[i], hi + i * preFactor);
            return;
         }
         post->Up(ch, x, hi);
      }
   };

   // Runs a segmented program (segment 0 driven by the ideal tone at its own
   // rate, as the node rig does) and returns kN base-rate samples after
   // warm-up. Params at their declared defaults.
   std::vector<float> RenderPlanned(const std::vector<Field::RateSegment>& segs, const FieldAliasCase& c,
                                    double* seconds)
   {
      const int m = (int)segs.size();
      struct SegRt
      {
         std::vector<float> cur, next, params;
         float regs[Field::kSampleMaxRegs] = {};
         PlanResampler up, down;
         std::vector<std::vector<float>> hiIn, hiOut;
         std::vector<float> baseOut;
         std::vector<int> fromPrev; // inRegs[i] = previous segment's outRegs[fromPrev[i]]
      };
      std::vector<SegRt> rt((size_t)m);
      PlanResampler dec;
      for (int s = 0; s < m; s++)
      {
         const Field::RateSegment& seg = segs[(size_t)s];
         SegRt& r = rt[(size_t)s];
         for (const auto& st : seg.prog.state)
            r.cur.push_back(st.initialValue);
         r.cur.resize(std::max<size_t>(r.cur.size(), 1));
         r.next = r.cur;
         for (const auto& p : seg.prog.params)
            r.params.push_back(p.defaultValue);
         r.params.resize(std::max<size_t>(r.params.size(), 1));
         const int k = seg.factor;
         if (s > 0)
         {
            const Field::RateSegment& prev = segs[(size_t)s - 1];
            for (int reg : seg.inRegs)
            {
               const auto at = std::find(prev.outRegs.begin(), prev.outRegs.end(), reg);
               if (at == prev.outRegs.end())
                  return {};
               r.fromPrev.push_back((int)(at - prev.outRegs.begin()));
            }
            r.up.Prepare(k);
         }
         if (s < m - 1)
            r.down.Prepare(k);
         r.hiIn.assign(seg.inRegs.size(), std::vector<float>((size_t)k));
         r.hiOut.assign(s < m - 1 ? seg.outRegs.size() : 1, std::vector<float>((size_t)k));
         r.baseOut.assign(seg.outRegs.size(), 0.0f);
      }
      dec.Prepare(segs.back().factor);

      const int warm = (int)(0.3 * kRate);
      const int total = warm + kN;
      std::vector<float> out;
      out.reserve((size_t)total);
      const auto t0 = std::chrono::steady_clock::now();
      for (long long n = 0; n < total; n++)
      {
         for (int s = 0; s < m; s++)
         {
            const Field::RateSegment& seg = segs[(size_t)s];
            SegRt& r = rt[(size_t)s];
            const int k = seg.factor;
            for (size_t i = 0; i < seg.inRegs.size(); i++)
            {
               const float x = rt[(size_t)s - 1].baseOut[(size_t)r.fromPrev[i]];
               r.up.Up((int)i, x, r.hiIn[i].data());
            }
            const double omega = 2.0 * M_PI * (double)c.fundBin / (double)kN / (double)k;
            Field::SampleRuntimeInput rin;
            rin.sr = (float)(kRate * k);
            rin.paramVals = r.params.data();
            rin.stateCur = r.cur.data();
            rin.stateNext = r.next.data();
            for (int j = 0; j < k; j++)
            {
               for (size_t i = 0; i < seg.inRegs.size(); i++)
                  r.regs[seg.inRegs[i]] = r.hiIn[i][(size_t)j];
               rin.in = (s == 0 && c.usesInput) ? kAmplitude * (float)std::sin(omega * (double)(n * k + j)) : 0.0f;
               float y = Field::RunSampleProgram(seg.prog, rin, r.regs);
               if (!r.cur.empty())
                  std::memcpy(r.cur.data(), r.next.data(), r.cur.size() * sizeof(float));
               if (s == m - 1)
               {
                  if (std::isnan(y) || std::isinf(y))
                     y = 0.0f;
                  r.hiOut[0][(size_t)j] = std::clamp(y, -4.0f, 4.0f);
               }
               else
                  for (size_t o = 0; o < seg.outRegs.size(); o++)
                     r.hiOut[o][(size_t)j] = r.regs[seg.outRegs[o]];
            }
            if (s == m - 1)
               out.push_back(dec.Down(0, r.hiOut[0].data()));
            else
               for (size_t o = 0; o < seg.outRegs.size(); o++)
                  r.baseOut[o] = r.down.Down((int)o, r.hiOut[o].data());
         }
      }
      if (seconds != nullptr)
         *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      return std::vector<float>(out.begin() + warm, out.end());
   }

   // One period of a periodic wave (length n1) at another length n2, every
   // bin at or above the base Nyquist (kN / 2 on the base grid) removed: an
   // ideal trip down to the base rate and up again.
   std::vector<float> IdealResample(const std::vector<float>& x, int n2)
   {
      const int n1 = (int)x.size();
      const int l1 = (int)std::lround(std::log2((double)n1)), l2 = (int)std::lround(std::log2((double)n2));
      PortableFft::RealFft fft;
      fft.Prepare(std::max(l1, l2));
      std::vector<float> re1((size_t)n1 / 2), im1((size_t)n1 / 2);
      fft.Forward(x.data(), l1, re1.data(), im1.data());
      std::vector<float> re2((size_t)n2 / 2, 0.0f), im2((size_t)n2 / 2, 0.0f);
      re2[0] = 0.5f * re1[0];
      for (int k = 1; k < kN / 2 && k < n1 / 2 && k < n2 / 2; k++)
      {
         re2[(size_t)k] = 0.5f * re1[(size_t)k];
         im2[(size_t)k] = 0.5f * im1[(size_t)k];
      }
      std::vector<float> y((size_t)n2);
      fft.Inverse(re2.data(), im2.data(), l2, y.data());
      for (float& v : y)
         v /= (float)n1;
      return y;
   }

   // RenderPlanned with ideal resamplers: each segment runs two periods of
   // the tone (the first warms its state) and passes one period on through
   // IdealResample. Separates resampler error from analysis error.
   std::vector<float> RenderPlannedIdeal(const std::vector<Field::RateSegment>& segs, const FieldAliasCase& c)
   {
      std::vector<std::vector<float>> waves; // previous segment's outRegs, one period at its rate
      for (size_t s = 0; s < segs.size(); s++)
      {
         const Field::RateSegment& seg = segs[s];
         const int k = seg.factor, period = k * kN;
         std::vector<std::vector<float>> in;
         for (int reg : seg.inRegs)
         {
            const auto at = std::find(segs[s - 1].outRegs.begin(), segs[s - 1].outRegs.end(), reg);
            if (at == segs[s - 1].outRegs.end())
               return {};
            in.push_back(IdealResample(waves[(size_t)(at - segs[s - 1].outRegs.begin())], period));
         }
         std::vector<float> cur, next, params;
         for (const auto& st : seg.prog.state)
            cur.push_back(st.initialValue);
         cur.resize(std::max<size_t>(cur.size(), 1));
         next = cur;
         for (const auto& p : seg.prog.params)
            params.push_back(p.defaultValue);
         params.resize(std::max<size_t>(params.size(), 1));
         float regs[Field::kSampleMaxRegs] = {};
         const bool last = s + 1 == segs.size();
         std::vector<std::vector<float>> out(last ? 1 : seg.outRegs.size(), std::vector<float>((size_t)period));
         const double omega = 2.0 * M_PI * (double)c.fundBin / (double)kN / (double)k;
         Field::SampleRuntimeInput rin;
         rin.sr = (float)(kRate * k);
         rin.paramVals = params.data();
         rin.stateCur = cur.data();
         rin.stateNext = next.data();
         for (int n = 0; n < 2 * period; n++)
         {
            const int j = n % period;
            for (size_t i = 0; i < in.size(); i++)
               regs[seg.inRegs[i]] = in[i][(size_t)j];
            rin.in = (s == 0 && c.usesInput) ? kAmplitude * (float)std::sin(omega * (double)j) : 0.0f;
            float y = Field::RunSampleProgram(seg.prog, rin, regs);
            std::memcpy(cur.data(), next.data(), cur.size() * sizeof(float));
            if (n < period)
               continue;
            if (last)
               out[0][(size_t)j] = std::isfinite(y) ? std::clamp(y, -4.0f, 4.0f) : 0.0f;
            else
               for (size_t o = 0; o < seg.outRegs.size(); o++)
                  out[o][(size_t)j] = regs[seg.outRegs[o]];
         }
         waves = std::move(out);
      }
      return IdealResample(waves[0], kN);
   }

   // In-band harmonic amplitudes (h f0 < 20 kHz), power summed over the
   // window's main lobe.
   std::vector<double> HarmonicAmps(const std::vector<float>& x, int fundBin)
   {
      std::vector<float> windowed((size_t)kN);
      for (int i = 0; i < kN; i++)
      {
         const double t = 2.0 * M_PI * (double)i / (double)kN;
         const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
         windowed[(size_t)i] = (float)(x[(size_t)i] * w);
      }
      PortableFft::RealFft fft;
      fft.Prepare(kLog2N);
      std::vector<float> re((size_t)kN / 2), im((size_t)kN / 2);
      fft.Forward(windowed.data(), kLog2N, re.data(), im.data());
      std::vector<double> a;
      for (int h = 1; (double)(h * fundBin) * kRate / (double)kN < kAudibleHz; h++)
      {
         double p = 0.0;
         for (int d = -kLobe; d <= kLobe; d++)
         {
            const int k = h * fundBin + d;
            if (k > 0 && k < kN / 2)
               p += (double)re[(size_t)k] * re[(size_t)k] + (double)im[(size_t)k] * im[(size_t)k];
         }
         a.push_back(std::sqrt(p));
      }
      return a;
   }

   // Timbre change against a reference: in-band harmonic amplitude error
   // energy re the reference fundamental, dB.
   double DeviationDb(const std::vector<float>& x, const std::vector<float>& ref, int fundBin)
   {
      const std::vector<double> a = HarmonicAmps(x, fundBin), r = HarmonicAmps(ref, fundBin);
      double e = 0.0;
      for (size_t h = 0; h < a.size() && h < r.size(); h++)
         e += (a[h] - r[h]) * (a[h] - r[h]);
      return 10.0 * std::log10(std::max(e, 1e-30) / std::max(r.empty() ? 0.0 : r[0] * r[0], 1e-30));
   }

   // Smallest factor whose predicted alias reaches -80 dB: 0 = none within
   // 32x, -1 = no prediction (why set).
   int PredictFactor(const Field::SampleProgram& prog, double f0, float amp, bool usesInput, double* pred,
                     std::string& why)
   {
      Field::AliasContext ctx;
      ctx.f0Hz = f0;
      ctx.inAmplitude = amp;
      ctx.usesInput = usesInput;
      double local[6];
      double* db = pred != nullptr ? pred : local;
      for (int k = 0; k < 6; k++)
      {
         Field::AliasSpectrum spec;
         if (!Field::AnalyzePeriodicSpectrum(prog, ctx, kRate * kFieldFactors[k], spec))
         {
            why = spec.status;
            return -1;
         }
         db[k] = Field::PredictAliasDb(spec, f0, kRate, kFieldFactors[k], kAudibleHz, (double)kLobe * kRate / (double)kN);
      }
      return FactorNeeded(db);
   }

   double NsPerCall(const std::function<void()>& f, int calls)
   {
      const auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < calls; i++)
         f();
      return 1e9 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / (double)calls;
   }

   // Cost per base sample, ns, measured on this build.
   Field::RateCostModel CalibrateCost()
   {
      Field::RateCostModel m;
      Field::SampleProgram shortP, longP;
      Field::FieldError err;
      Field::CompileSampleProgram("out = in * 0.7\n", nullptr, shortP, err);
      std::string lc = "x = in * 0.5\n";
      for (int i = 0; i < 24; i++)
         lc += "x = x - x * x * x / 3.0\n";
      lc += "out = x\n";
      Field::CompileSampleProgram(lc, nullptr, longP, err);
      float regs[Field::kSampleMaxRegs] = {};
      float st[1] = {}, pv[1] = {};
      Field::SampleRuntimeInput rin;
      rin.sr = (float)kRate;
      rin.stateCur = st;
      rin.stateNext = st;
      rin.paramVals = pv;
      volatile float sink = 0.0f;
      auto timeProg = [&](const Field::SampleProgram& p) {
         return NsPerCall([&] {
            rin.in += 1e-6f;
            sink = sink + Field::RunSampleProgram(p, rin, regs);
         }, 400000);
      };
      timeProg(longP);
      const double ts = timeProg(shortP), tl = timeProg(longP);
      const double ns = (double)shortP.code.size(), nl = (double)longP.code.size();
      m.instr = std::max(0.0, (tl - ts) / (nl - ns));
      m.run = std::max(0.0, ts - m.instr * ns);
      double up[4] = {}, down[4] = {};
      for (int fi = 1; fi <= 3; fi++)
      {
         const int k = 1 << fi;
         MinPhaseOversampler os;
         os.Prepare(k, kRate);
         float hi[8] = {};
         float x = 0.1f;
         up[fi] = NsPerCall([&] {
            x = -x;
            os.Up(0, x, hi);
         }, 200000);
         down[fi] = NsPerCall([&] { sink = sink + os.Down(0, hi); }, 200000);
      }
      for (int fi = 0; fi < 4; fi++)
      {
         m.up[fi] = up[fi];
         m.down[fi] = down[fi];
      }
      m.up[4] = up[3] + 8.0 * up[1];
      m.up[5] = up[3] + 8.0 * up[2];
      m.down[4] = down[3] + 8.0 * down[1];
      m.down[5] = down[3] + 8.0 * down[2];
      return m;
   }

   std::string PlanText(const Field::RatePlan& p)
   {
      std::string s;
      for (size_t i = 0; i < p.segs.size(); i++)
      {
         if (i > 0)
            s += "|";
         s += std::to_string(p.segs[i].factor) + "x" + std::to_string(p.segs[i].end - p.segs[i].first);
      }
      return s;
   }
}

bool RunFieldRatePlanTest()
{
   const std::vector<FieldAliasCase> corpus = FieldAliasCorpus();
   FILE* csv = nullptr;
   if (const char* path = getenv("INFINITE_FIELDRATEPLAN_CSV"))
   {
      csv = fopen(path, "w");
      if (csv != nullptr)
         fprintf(csv, "section,label,class,whole_factor,whole_cost,plan,plan_cost,eff_cost,saving,node_db,whole_db,"
                      "staged_db,staged_pred_db,dev_whole_db,dev_staged_db,whole_ms,staged_ms\n");
   }
   // Costs measured in this build (Debug in dev), unless
   // INFINITE_FIELDRATE_COST gives -O2 figures from
   // tools/field_rate_cost_bench.cpp: run,instr,up2,up4,up8,down2,down4,down8.
   Field::RateCostModel cm = CalibrateCost();
   const char* costSource = "measured in this build";
   if (const char* env = getenv("INFINITE_FIELDRATE_COST"))
   {
      double v[8];
      if (sscanf(env, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8)
      {
         cm.run = std::max(0.0, v[0]);
         cm.instr = v[1];
         for (int fi = 1; fi <= 3; fi++)
         {
            cm.up[fi] = v[1 + fi];
            cm.down[fi] = v[4 + fi];
         }
         cm.up[4] = cm.up[3] + 8.0 * cm.up[1];
         cm.up[5] = cm.up[3] + 8.0 * cm.up[2];
         cm.down[4] = cm.down[3] + 8.0 * cm.down[1];
         cm.down[5] = cm.down[3] + 8.0 * cm.down[2];
         costSource = "INFINITE_FIELDRATE_COST";
      }
   }
   printf("FIELDRATE cost model (ns per base sample, %s): run %.1f, per instr %.2f, up 2/4/8/16/32 "
          "%.0f/%.0f/%.0f/%.0f/%.0f, down %.0f/%.0f/%.0f/%.0f/%.0f\n",
          costSource, cm.run, cm.instr, cm.up[1], cm.up[2], cm.up[3], cm.up[4], cm.up[5], cm.down[1], cm.down[2], cm.down[3],
          cm.down[4], cm.down[5]);
   printf("FIELDRATE %-30s %-8s %5s %8s %-22s %8s %7s %7s %7s %7s %6s %6s %7s %7s\n", "kernel", "class", "whole",
          "cost", "plan", "cost", "saving", "node", "whole", "staged", "devW", "devS", "ms W", "ms S");
   bool all = true;
   double sumWhole = 0.0, sumEff = 0.0;
   std::vector<double> savings;
   int included = 0, skipped = 0, planMissed = 0, sanityBad = 0;
   bool chainBeats8 = false, chainSeen = false;
   const double tolHz = (double)kLobe * kRate / (double)kN;
   for (const FieldAliasCase& c : corpus)
   {
      if (c.klass == "linear")
         continue;
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!Field::CompileSampleProgram(c.code, nullptr, prog, ferr))
         continue;
      const double f0 = (double)c.fundBin * kRate / (double)kN;
      std::string why;
      double pred[6];
      const int wholeF = PredictFactor(prog, f0, kAmplitude, c.usesInput, pred, why);
      if (wholeF < 2)
      {
         printf("FIELDRATE %-30s %-8s skipped: %s\n", c.label.c_str(), c.klass.c_str(),
                wholeF < 0 ? why.c_str() : wholeF == 0 ? "no factor within 32x" : "clean at 1x");
         skipped++;
         continue;
      }
      Field::RatePlanOptions opt;
      opt.f0Hz = f0;
      opt.inAmplitude = kAmplitude;
      opt.usesInput = c.usesInput;
      opt.audibleHz = kAudibleHz;
      opt.harmonicTolHz = tolHz;
      opt.cost = cm;
      Field::RatePlan plan;
      Field::RatePlanReport rep;
      const auto tp = std::chrono::steady_clock::now();
      const bool planned = Field::ChooseRatePlan(prog, opt, plan, rep);
      const double planSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - tp).count();

      Field::SampleProgram prepared;
      int inReg = -1;
      std::vector<Field::RateSegment> whole;
      Field::PrepareForSplit(prog, prepared, inReg, why);
      Field::SplitProgram(prepared, {}, whole, why);
      whole[0].factor = wholeF;
      double wSec = 0.0, sSec = 0.0;
      const std::vector<float> yw = RenderPlanned(whole, c, &wSec);
      std::string err;
      const std::vector<float> yn = RenderFieldAt(c, wholeF, err);
      const std::vector<float> ref = RenderFieldAt(c, 32, err);
      if (yw.empty() || yn.empty() || ref.empty())
      {
         printf("FIELDRATE %-30s render failed  FAIL\n", c.label.c_str());
         all = false;
         continue;
      }
      const double dbW = Measure(yw, c.fundBin).aliasDb, dbN = Measure(yn, c.fundBin).aliasDb;
      if (std::fabs(dbW - dbN) > 3.0 && std::max(dbW, dbN) > -100.0)
         sanityBad++;
      const double costW = Field::WholeCost(prog, wholeF, cm);
      double dbS = 0.0, devS = 0.0, costEff = costW;
      std::vector<float> ys; // the staged render, kept for the audio dump
      std::string planText = "(none)";
      if (planned)
      {
         planText = PlanText(plan);
         // INFINITE_FIELDRATE_DUMP=<kernel label>: the plan's segments.
         if (const char* dump = getenv("INFINITE_FIELDRATE_DUMP"); dump != nullptr && c.label == dump)
         {
            const std::vector<float> yi = RenderPlannedIdeal(plan.segs, c);
            const std::vector<float> wi = RenderPlannedIdeal(whole, c);
            printf("FIELDRATE dump ideal resamplers: plan %.1f dB, whole at %dx %.1f dB\n",
                   yi.empty() ? 0.0 : Measure(yi, c.fundBin).aliasDb, wholeF,
                   wi.empty() ? 0.0 : Measure(wi, c.fundBin).aliasDb);
            for (size_t si = 0; si < plan.segs.size(); si++)
            {
               const Field::RateSegment& sg = plan.segs[si];
               printf("FIELDRATE dump seg %zu at %dx, [%d,%d) in:", si, sg.factor, sg.first, sg.end);
               for (int r : sg.inRegs)
                  printf(" r%d", r);
               printf(" out:");
               for (int r : sg.outRegs)
                  printf(" r%d", r);
               printf(" pred %.1f dB\n", plan.segDb[si]);
               for (const Field::SampleInstr& ins : sg.prog.code)
                  printf("FIELDRATE dump    op %2d r%d <- r%d r%d r%d imm %g\n", (int)ins.op, ins.dst, ins.a, ins.b,
                         ins.c, ins.imm);
            }
         }
         ys = RenderPlanned(plan.segs, c, &sSec);
         dbS = ys.empty() ? 0.0 : Measure(ys, c.fundBin).aliasDb;
         devS = ys.empty() ? 0.0 : DeviationDb(ys, ref, c.fundBin);
         if (plan.meetsTarget && !ys.empty() && dbS <= -80.0)
            costEff = std::min(plan.cost, costW);
         else
            planMissed++;
      }
      else
         planMissed++;
      const double devW = DeviationDb(yw, ref, c.fundBin);
      const double saving = 1.0 - costEff / costW;
      included++;
      sumWhole += costW;
      sumEff += costEff;
      savings.push_back(saving);
      printf("FIELDRATE %-30s %-8s %4dx %8.0f %-22s %8.0f %6.0f%% %7.1f %7.1f %7.1f %6.1f %6.1f %7.0f %7.0f  "
             "pred %.1f, %d cands, %d sets, %.2f s%s\n",
             c.label.c_str(), c.klass.c_str(), wholeF, costW, planText.c_str(), planned ? plan.cost : 0.0,
             100.0 * saving, dbN, dbW, dbS, devW, devS, 1e3 * wSec, 1e3 * sSec, planned ? plan.predictedDb : 0.0,
             rep.candidates, rep.cutSets, planSec, planned && !plan.meetsTarget ? ", plan misses target" : "");
      if (csv != nullptr)
         fprintf(csv, "h2,\"%s\",%s,%d,%.1f,%s,%.1f,%.1f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f\n",
                 c.label.c_str(), c.klass.c_str(), wholeF, costW, planText.c_str(), planned ? plan.cost : 0.0, costEff,
                 saving, dbN, dbW, dbS, planned ? plan.predictedDb : 0.0, devW, devS, 1e3 * wSec, 1e3 * sSec);
      if (c.label == "tanh x3 chain drive 4")
      {
         chainSeen = true;
         const double cost8 = Field::WholeCost(prog, 8, cm);
         const std::vector<float> y8 = RenderFieldAt(c, 8, err);
         const double db8 = y8.empty() ? 0.0 : Measure(y8, c.fundBin).aliasDb;
         const bool stagedOk = planned && dbS <= -80.0;
         chainBeats8 = stagedOk && (db8 > -80.0 || plan.cost < cost8);
         printf("FIELDRATE tanh x3 vs fixed 8x: 8x cost %.0f, alias %.1f dB; staged cost %.0f, alias %.1f dB  %s\n",
                cost8, db8, planned ? plan.cost : 0.0, dbS, chainBeats8 ? "staged beats 8x" : "staged does not beat 8x");
         // INFINITE_FIELDRATE_AUDIO=<dir>: the four renders as raw float32 mono at 48 kHz (one steady-state
         // period of kN samples each, after warm-up), for the listening examples in the lab note.
         if (const char* dir = getenv("INFINITE_FIELDRATE_AUDIO"); dir != nullptr && *dir != 0)
         {
            const std::vector<float> y1 = RenderFieldAt(c, 1, err);
            const std::pair<const char*, const std::vector<float>*> clips[] = {
               { "tanh3-1x", &y1 }, { "tanh3-fixed8x", &y8 }, { "tanh3-staged", &ys }, { "tanh3-32x", &ref } };
            for (const auto& [name, y] : clips)
            {
               const std::string path = std::string(dir) + "/" + name + ".f32";
               if (FILE* f = fopen(path.c_str(), "wb"))
               {
                  fwrite(y->data(), sizeof(float), y->size(), f);
                  fclose(f);
                  printf("FIELDRATE audio %s %zu samples, alias %.1f dB\n", path.c_str(), y->size(),
                         y->empty() ? 0.0 : Measure(*y, c.fundBin).aliasDb);
               }
            }
         }
      }
      fflush(stdout);
   }
   const double agg = sumWhole > 0.0 ? 1.0 - sumEff / sumWhole : 0.0;
   printf("FIELDRATE H2 %d kernels (%d skipped, %d plans fell back to whole), executor vs node at the whole factor "
          "off by > 3 dB on %d\n",
          included, skipped, planMissed, sanityBad);
   const bool h2 = agg >= 0.25 && (!chainSeen || chainBeats8);
   printf("FIELDRATE H2 aggregate saving %.1f%% (>= 25%%), median per kernel %.1f%%, tanh x3 %s  %s\n", 100.0 * agg,
          100.0 * Median(savings), chainBeats8 ? "beats 8x" : "does not beat 8x", h2 ? "H2 HOLDS" : "H2 FAILS");

   // Corner sweep: the whole-kernel factor at input amplitude x frequency
   // corners (prediction only). Free-running oscillators have no input.
   printf("FIELDRATE corners: factor at amp 0.25/0.5/1.0 x f0 0.5/1/2 x 5 kHz (row = amp)\n");
   int cornerKernels = 0, cornerChanged = 0, cornerWorse = 0;
   for (const FieldAliasCase& c : corpus)
   {
      if (c.klass == "linear" || !c.usesInput)
         continue;
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!Field::CompileSampleProgram(c.code, nullptr, prog, ferr))
         continue;
      const double f0 = (double)c.fundBin * kRate / (double)kN;
      std::string why;
      const int nominal = PredictFactor(prog, f0, kAmplitude, true, nullptr, why);
      if (nominal < 0)
         continue;
      auto rank = [](int f) { return f == 0 ? 64 : f; };
      std::string cells;
      int worst = nominal;
      bool changed = false, failed = false;
      for (float amp : { 0.25f, 0.5f, 1.0f })
         for (double mul : { 0.5, 1.0, 2.0 })
         {
            const int f = PredictFactor(prog, f0 * mul, amp, true, nullptr, why);
            if (f < 0)
               failed = true;
            char b[8];
            snprintf(b, sizeof(b), "%3s", f < 0 ? "?" : f == 0 ? ">32" : std::to_string(f).c_str());
            cells += b;
            cells += mul == 2.0 ? "  " : " ";
            if (f >= 0)
            {
               changed |= f != nominal;
               if (rank(f) > rank(worst))
                  worst = f;
            }
         }
      cornerKernels++;
      cornerChanged += changed;
      cornerWorse += rank(worst) > rank(nominal);
      printf("FIELDRATE corner %-30s nominal %3s  %s worst %3s%s\n", c.label.c_str(),
             nominal == 0 ? ">32" : std::to_string(nominal).c_str(), cells.c_str(),
             worst == 0 ? ">32" : std::to_string(worst).c_str(), failed ? "  (some corners unpredicted)" : "");
      fflush(stdout);
   }
   printf("FIELDRATE corners: %d kernels, factor changes somewhere on %d, worst corner needs more than nominal on %d\n",
          cornerKernels, cornerChanged, cornerWorse);

   // Held-out: every Field Effect preset not in the corpus. Field Synth
   // presets are note-driven and out of scope here.
   std::set<std::string> inCorpus;
   for (const FieldAliasCase& c : corpus)
      if (c.klass == "preset")
         inCorpus.insert(c.label.substr(7));
   int heldAccepted = 0, heldDeclined = 0, heldExact = 0;
   std::vector<double> heldErr;
   for (const auto& p : FieldSampleNode::Presets())
   {
      if (inCorpus.count(p.name) != 0)
         continue;
      FieldAliasCase c;
      c.label = std::string("held ") + p.name;
      c.klass = "held";
      c.code = p.code;
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!Field::CompileSampleProgram(c.code, nullptr, prog, ferr))
      {
         printf("FIELDRATE held %-34s declined: does not compile (%s)\n", p.name, ferr.message.c_str());
         heldDeclined++;
         continue;
      }
      std::string why;
      double pred[6];
      const int fp = PredictFactor(prog, (double)kFundBin * kRate / (double)kN, kAmplitude, true, pred, why);
      if (fp < 0)
      {
         printf("FIELDRATE held %-34s declined: %s\n", p.name, why.c_str());
         heldDeclined++;
         continue;
      }
      double db[6];
      std::string err;
      bool ok = true;
      for (int k = 0; k < 6 && ok; k++)
      {
         const std::vector<float> x = RenderFieldAt(c, kFieldFactors[k], err);
         ok = !x.empty();
         if (ok)
            db[k] = Measure(x, c.fundBin).aliasDb;
      }
      if (!ok)
      {
         printf("FIELDRATE held %-34s render failed: %s\n", p.name, err.c_str());
         continue;
      }
      heldAccepted++;
      const int fm = FactorNeeded(db);
      heldExact += fm == fp;
      double worstErr = 0.0;
      for (int k = 0; k < 6; k++)
      {
         const double e = std::fabs(std::max(db[k], kScoreFloorDb) - std::max(pred[k], kScoreFloorDb));
         heldErr.push_back(e);
         worstErr = std::max(worstErr, e);
      }
      printf("FIELDRATE held %-34s meas %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f | pred %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f"
             " | factor %s/%s, worst err %.1f dB\n",
             p.name, db[0], db[1], db[2], db[3], db[4], db[5], pred[0], pred[1], pred[2], pred[3], pred[4], pred[5],
             fm == 0 ? ">32" : std::to_string(fm).c_str(), fp == 0 ? ">32" : std::to_string(fp).c_str(), worstErr);
      if (csv != nullptr)
         fprintf(csv, "held,\"%s\",held,%d,,,,,,%.2f,,,%.2f,,,,\n", p.name, fp, db[2], pred[2]);
      fflush(stdout);
   }
   printf("FIELDRATE held-out: %d accepted (factor exact %d/%d, median |err| %.1f dB), %d declined\n", heldAccepted,
          heldExact, heldAccepted, Median(heldErr), heldDeclined);
   if (csv != nullptr)
      fclose(csv);
   printf("FIELDRATE %s\n", all ? "OK" : "FAIL");
   return all;
}

// Phase 4 held-out scoring (docs/research/aliasing-aware-field/README.md
// section 13). INFINITE_FIELDHELDOUT=<dir> scores every <dir>/<set>/*.field
// file: the same compile, PredictFactor, 1x-32x render and Measure as the
// FIELDRATE held-out presets above, one CSV row per kernel to
// INFINITE_FIELDHELDOUT_CSV. Kernels are in-driven Field Effect code; a
// kernel that does not compile or that the analysis declines is a row with
// its reason, never a silent drop. Test code only: the analysis is frozen.
bool RunFieldHeldOutTest()
{
   const char* dir = getenv("INFINITE_FIELDHELDOUT");
   const char* csvPath = getenv("INFINITE_FIELDHELDOUT_CSV");
   if (dir == nullptr || csvPath == nullptr)
   {
      printf("FIELDHELDOUT needs INFINITE_FIELDHELDOUT=<dir> and INFINITE_FIELDHELDOUT_CSV=<file>\n");
      return false;
   }
   FILE* csv = fopen(csvPath, "w");
   if (csv == nullptr)
      return false;
   fprintf(csv, "set,label,status,reason,memoryless,meas_1,meas_2,meas_4,meas_8,meas_16,meas_32,"
                "pred_1,pred_2,pred_4,pred_8,pred_16,pred_32,factor_meas,factor_pred\n");
   std::vector<std::filesystem::path> files;
   std::error_code ec;
   for (const auto& set : std::filesystem::directory_iterator(dir, ec))
      if (set.is_directory())
         for (const auto& f : std::filesystem::directory_iterator(set.path(), ec))
            if (f.path().extension() == ".field")
               files.push_back(f.path());
   std::sort(files.begin(), files.end());
   auto quote = [](std::string v) {
      for (char& ch : v)
         if (ch == '"' || ch == '\n' || ch == '\r')
            ch = '\'';
      return "\"" + v + "\"";
   };
   int scored = 0, declined = 0;
   for (const auto& path : files)
   {
      FILE* fp = fopen(path.string().c_str(), "rb");
      if (fp == nullptr)
         continue;
      std::string code;
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
         code.append(buf, n);
      fclose(fp);
      const std::string set = path.parent_path().filename().string(), label = path.stem().string();
      FieldAliasCase c;
      c.label = label;
      c.klass = "heldout";
      c.code = code;
      auto row = [&](const char* status, const std::string& reason, int memoryless, const double* db,
                     const double* pred, int fm, int fpred) {
         fprintf(csv, "%s,%s,%s,%s,%s", set.c_str(), quote(label).c_str(), status, quote(reason).c_str(),
                 memoryless < 0 ? "" : (memoryless ? "1" : "0"));
         for (int k = 0; k < 6; k++)
            db != nullptr ? fprintf(csv, ",%.2f", db[k]) : fprintf(csv, ",");
         for (int k = 0; k < 6; k++)
            pred != nullptr ? fprintf(csv, ",%.2f", pred[k]) : fprintf(csv, ",");
         fm >= 0 ? fprintf(csv, ",%d", fm) : fprintf(csv, ",");
         fpred >= 0 ? fprintf(csv, ",%d\n", fpred) : fprintf(csv, ",\n");
      };
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!Field::CompileSampleProgram(code, nullptr, prog, ferr))
      {
         printf("FIELDHELDOUT %s/%-34s declined: does not compile (%s)\n", set.c_str(), label.c_str(),
                ferr.message.c_str());
         row("compile", ferr.message, -1, nullptr, nullptr, -1, -1);
         declined++;
         continue;
      }
      const int memoryless = prog.state.empty() && prog.delays.empty() && prog.tables.empty() ? 1 : 0;
      std::string why;
      double pred[6];
      const int fpred = PredictFactor(prog, (double)kFundBin * kRate / (double)kN, kAmplitude, true, pred, why);
      if (fpred < 0)
      {
         printf("FIELDHELDOUT %s/%-34s declined: %s\n", set.c_str(), label.c_str(), why.c_str());
         row("declined", why, memoryless, nullptr, nullptr, -1, -1);
         declined++;
         continue;
      }
      double db[6];
      std::string err;
      bool ok = true;
      for (int k = 0; k < 6 && ok; k++)
      {
         const std::vector<float> x = RenderFieldAt(c, kFieldFactors[k], err);
         ok = !x.empty();
         if (ok)
            db[k] = Measure(x, c.fundBin).aliasDb;
      }
      if (!ok)
      {
         printf("FIELDHELDOUT %s/%-34s render failed: %s\n", set.c_str(), label.c_str(), err.c_str());
         row("render", err, memoryless, nullptr, pred, -1, fpred);
         declined++;
         continue;
      }
      const int fm = FactorNeeded(db);
      row("ok", "", memoryless, db, pred, fm, fpred);
      scored++;
      printf("FIELDHELDOUT %s/%-34s meas %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f | pred %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f"
             " | factor %s/%s\n",
             set.c_str(), label.c_str(), db[0], db[1], db[2], db[3], db[4], db[5], pred[0], pred[1], pred[2], pred[3],
             pred[4], pred[5], fm == 0 ? ">32" : std::to_string(fm).c_str(), fpred == 0 ? ">32" : std::to_string(fpred).c_str());
      fflush(stdout);
   }
   fclose(csv);
   printf("FIELDHELDOUT %zu files: %d scored, %d declined or failed\n", files.size(), scored, declined);
   return true;
}

// Phase 6 real-input scoring (docs/research/aliasing-aware-field/README.md
// section 17). INFINITE_FIELDREAL=<heldout dir>, INFINITE_FIELDREAL_CLIPS=
// <dir of clip kernels>, INFINITE_FIELDREAL_MODE = clips | check | measure |
// p1, INFINITE_FIELDREAL_CSV=<file>; optional INFINITE_FIELDREAL_LIST (lines
// "<set>/<label>"), INFINITE_FIELDREAL_SHARD ("i/n") and
// INFINITE_FIELDREAL_WAVDIR (clips mode). The oracle: the clip ideally
// upsampled 64x; for factor F the test is the kernel at F x 48 kHz on every
// (64/F)-th fine sample, the reference 64/F interleaved copies of it, both
// band-limited to 20 kHz. Test code only.
namespace
{
   using cplx = std::complex<double>;
   constexpr int kRealN = 16384;         // clip length at 48 kHz
   constexpr int kRealUp = 64;           // oracle rate, x 48 kHz
   constexpr int kRealFine = kRealN * kRealUp;
   constexpr int kRealBand = 6826;       // bins at or below 20 kHz (2.93 Hz each)
   constexpr int kRealFade = 480;        // 10 ms raised-cosine fades
   constexpr int kRealWin = 4800, kRealHop = 2400;

   void FftD(std::vector<cplx>& a, bool inverse)
   {
      const size_t n = a.size();
      static std::map<size_t, std::vector<cplx>> twiddles; // e^{-2 pi i k / n}, k < n / 2
      std::vector<cplx>& tw = twiddles[n];
      if (tw.size() != n / 2)
      {
         tw.resize(n / 2);
         for (size_t k = 0; k < n / 2; k++)
            tw[k] = std::polar(1.0, -2.0 * M_PI * (double)k / (double)n);
      }
      for (size_t i = 1, j = 0; i < n; i++)
      {
         size_t bit = n >> 1;
         for (; j & bit; bit >>= 1)
            j ^= bit;
         j ^= bit;
         if (i < j)
            std::swap(a[i], a[j]);
      }
      for (size_t len = 2; len <= n; len <<= 1)
      {
         const size_t stride = n / len, half = len / 2;
         for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < half; k++)
            {
               const cplx w = inverse ? std::conj(tw[k * stride]) : tw[k * stride];
               const cplx u = a[i + k], v = a[i + k + half] * w;
               a[i + k] = u + v;
               a[i + k + half] = u - v;
            }
      }
   }

   // M independent instances of one kernel (state, delay lines), run the way
   // FieldSampleNode runs it: params at defaults, output NaN -> 0, clamped.
   struct KernelBank
   {
      const Field::SampleProgram& prog;
      int dsize = 1;
      std::vector<float> cur, next, dbuf, params;
      std::vector<int> cursors;
      float regs[Field::kSampleMaxRegs] = {};
      KernelBank(const Field::SampleProgram& p, int copies) : prog(p)
      {
         for (const auto& dl : p.delays)
            dsize = std::max(dsize, dl.bufferOffset + dl.length);
         const int k = Field::kSampleMaxStateCells;
         cur.assign((size_t)copies * k, 0.0f);
         for (int c = 0; c < copies; c++)
            for (size_t i = 0; i < p.state.size() && i < (size_t)k; i++)
               cur[(size_t)c * k + i] = p.state[i].initialValue;
         next = cur;
         dbuf.assign((size_t)copies * dsize, 0.0f);
         cursors.assign((size_t)copies * Field::kSampleMaxDelayLines, 0);
         params.assign(Field::kSampleMaxParams, 0.0f);
         for (size_t i = 0; i < p.params.size() && i < (size_t)Field::kSampleMaxParams; i++)
            params[i] = p.params[i].defaultValue;
      }
      float Run(int c, float in, float sr, double n)
      {
         const int k = Field::kSampleMaxStateCells;
         Field::SampleRuntimeInput rin;
         rin.in = in;
         rin.sr = sr;
         rin.n = (float)n;
         rin.paramVals = params.data();
         float* sc = cur.data() + (size_t)c * k;
         float* sn = next.data() + (size_t)c * k;
         rin.stateCur = sc;
         rin.stateNext = sn;
         rin.delayBuf = dbuf.data() + (size_t)c * dsize;
         rin.delayCursors = cursors.data() + (size_t)c * Field::kSampleMaxDelayLines;
         float y = Field::RunSampleProgram(prog, rin, regs);
         std::memcpy(sc, sn, sizeof(float) * std::min(prog.state.size(), (size_t)k));
         if (std::isnan(y) || std::isinf(y))
            y = 0.0f;
         return std::clamp(y, -4.0f, 4.0f);
      }
   };

   bool ReadText(const std::filesystem::path& p, std::string& out)
   {
      FILE* fp = fopen(p.string().c_str(), "rb");
      if (fp == nullptr)
         return false;
      char buf[4096];
      size_t n;
      out.clear();
      while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
         out.append(buf, n);
      fclose(fp);
      return true;
   }

   // A clip kernel rendered at 48 kHz (in = 0), faded and scaled to peak 1.
   bool RenderClip(const std::string& code, std::vector<double>& x, std::string& err)
   {
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!Field::CompileSampleProgram(code, nullptr, prog, ferr))
         return err = ferr.message, false;
      KernelBank bank(prog, 1);
      x.assign(kRealN, 0.0);
      for (int i = 0; i < kRealN; i++)
         x[(size_t)i] = bank.Run(0, 0.0f, (float)kRate, (double)i);
      double peak = 0.0;
      for (int i = 0; i < kRealN; i++)
      {
         double w = 1.0;
         if (i < kRealFade)
            w = 0.5 - 0.5 * std::cos(M_PI * (double)i / (double)kRealFade);
         else if (i >= kRealN - kRealFade)
            w = 0.5 - 0.5 * std::cos(M_PI * (double)(kRealN - 1 - i) / (double)kRealFade);
         x[(size_t)i] *= w;
         peak = std::max(peak, std::fabs(x[(size_t)i]));
      }
      if (peak <= 0.0)
         return err = "silent clip", false;
      for (double& v : x)
         v /= peak;
      return true;
   }

   // Ideal (periodic) 64x upsampling, 2^14 -> 2^20 points.
   std::vector<float> Upsample64(const std::vector<double>& x)
   {
      std::vector<cplx> a(kRealN), b(kRealFine, cplx(0.0, 0.0));
      for (int i = 0; i < kRealN; i++)
         a[(size_t)i] = x[(size_t)i];
      FftD(a, false);
      for (int k = 0; k < kRealN / 2; k++)
         b[(size_t)k] = a[(size_t)k];
      for (int k = 1; k < kRealN / 2; k++)
         b[(size_t)(kRealFine - k)] = a[(size_t)(kRealN - k)];
      b[(size_t)(kRealN / 2)] = 0.5 * a[(size_t)(kRealN / 2)];
      b[(size_t)(kRealFine - kRealN / 2)] = 0.5 * a[(size_t)(kRealN / 2)];
      FftD(b, true);
      std::vector<float> y(kRealFine);
      for (int i = 0; i < kRealFine; i++)
         y[(size_t)i] = (float)(b[(size_t)i].real() / (double)kRealN);
      return y;
   }

   // The 64x fine output, ideally low-passed at 20 kHz and taken on the 48 kHz
   // grid, for the reference (all fine samples) and the test (every m-th):
   // one 2^14-point FFT per polyphase phase r, combined with e^{-2 pi i b r / 2^20}.
   void BandTo48(const std::vector<double>& fine, int m, std::vector<double>& ref48, std::vector<double>& test48)
   {
      static std::vector<cplx> rot; // e^{-2 pi i b r / 2^20}, [r][b]
      if (rot.empty())
      {
         rot.resize((size_t)kRealUp * (kRealBand + 1));
         for (int r = 0; r < kRealUp; r++)
            for (int b = 0; b <= kRealBand; b++)
               rot[(size_t)r * (kRealBand + 1) + (size_t)b] =
                  std::polar(1.0, -2.0 * M_PI * (double)b * (double)r / (double)kRealFine);
      }
      std::vector<cplx> a(kRealN), y((size_t)kRealBand + 1, cplx(0.0, 0.0)), t = y;
      for (int r = 0; r < kRealUp; r++)
      {
         for (int i = 0; i < kRealN; i++)
            a[(size_t)i] = fine[(size_t)i * kRealUp + (size_t)r];
         FftD(a, false);
         const bool inTest = r % m == 0;
         for (int b = 0; b <= kRealBand; b++)
         {
            const cplx v = a[(size_t)b] * rot[(size_t)r * (kRealBand + 1) + (size_t)b];
            y[(size_t)b] += v;
            if (inTest)
               t[(size_t)b] += v;
         }
      }
      auto back = [&](const std::vector<cplx>& spec, double scale, std::vector<double>& out) {
         std::vector<cplx> c(kRealN, cplx(0.0, 0.0));
         c[0] = spec[0] * scale;
         for (int b = 1; b <= kRealBand; b++)
         {
            c[(size_t)b] = spec[(size_t)b] * scale;
            c[(size_t)(kRealN - b)] = std::conj(c[(size_t)b]);
         }
         FftD(c, true);
         out.resize(kRealN);
         for (int i = 0; i < kRealN; i++)
            out[(size_t)i] = c[(size_t)i].real();
      };
      back(y, 1.0 / (double)kRealFine, ref48);
      back(t, (double)m / (double)kRealFine, test48);
   }

   // Worst 100 ms window of error over reference power, among windows within
   // 30 dB of the loudest. NaN when the reference is silent.
   double WorstWindowDb(const std::vector<double>& t, const std::vector<double>& r)
   {
      std::vector<double> ew, rw;
      for (int s = 0; s + kRealWin <= kRealN; s += kRealHop)
      {
         double e = 0.0, p = 0.0;
         for (int i = s; i < s + kRealWin; i++)
         {
            const double d = t[(size_t)i] - r[(size_t)i];
            e += d * d;
            p += r[(size_t)i] * r[(size_t)i];
         }
         ew.push_back(e);
         rw.push_back(p);
      }
      const double pmax = *std::max_element(rw.begin(), rw.end());
      if (!(pmax > 1e-20))
         return std::nan("");
      double worst = -400.0;
      for (size_t w = 0; w < ew.size(); w++)
         if (rw[w] >= 1e-3 * pmax)
            worst = std::max(worst, 10.0 * std::log10(std::max(ew[w], 1e-40) / rw[w]));
      return worst;
   }

   // Alias of one kernel on one upsampled clip at each of the six factors.
   bool MeasureReal(const Field::SampleProgram& prog, const std::vector<float>& x64, double* db)
   {
      std::vector<double> fine(kRealFine);
      for (int k = 0; k < 6; k++)
      {
         const int f = kFieldFactors[k], m = kRealUp / f;
         KernelBank bank(prog, m);
         const float sr = (float)(kRate * f);
         // Two periods; the second is the periodic steady state. (A copy that
         // starts between base samples would otherwise miss the input's
         // inter-sample past.)
         for (int n = 0; n < 2 * kRealFine; n++)
         {
            const float y = bank.Run(n % m, x64[(size_t)(n % kRealFine)], sr, (double)(n / m));
            if (n >= kRealFine)
               fine[(size_t)(n - kRealFine)] = y;
         }
         std::vector<double> ref48, test48;
         BandTo48(fine, m, ref48, test48);
         db[k] = WorstWindowDb(test48, ref48);
         if (std::isnan(db[k]))
            return false;
      }
      return true;
   }

   bool WriteWavF32(const std::string& path, const std::vector<double>& x, int rate)
   {
      FILE* fp = fopen(path.c_str(), "wb");
      if (fp == nullptr)
         return false;
      auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); };
      auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, fp); };
      const uint32_t bytes = (uint32_t)(x.size() * 4);
      fwrite("RIFF", 1, 4, fp);
      u32(36 + bytes);
      fwrite("WAVEfmt ", 1, 8, fp);
      u32(16);
      u16(3);
      u16(1);
      u32((uint32_t)rate);
      u32((uint32_t)rate * 4);
      u16(4);
      u16(32);
      fwrite("data", 1, 4, fp);
      u32(bytes);
      for (double v : x)
      {
         const float f = (float)v;
         fwrite(&f, 4, 1, fp);
      }
      fclose(fp);
      return true;
   }

   int FactorRank(int f) { return f == 0 ? 64 : f; }

   // PredictFactor with an explicit analysis grid (P1's low tones need more
   // points per period).
   int PredictFactorGrid(const Field::SampleProgram& prog, double f0, float amp, int gridLog2, std::string& why)
   {
      Field::AliasContext ctx;
      ctx.f0Hz = f0;
      ctx.inAmplitude = amp;
      ctx.usesInput = true;
      ctx.gridLog2 = gridLog2;
      double db[6];
      for (int k = 0; k < 6; k++)
      {
         Field::AliasSpectrum spec;
         if (!Field::AnalyzePeriodicSpectrum(prog, ctx, kRate * kFieldFactors[k], spec))
            return why = spec.status, -1;
         db[k] = Field::PredictAliasDb(spec, f0, kRate, kFieldFactors[k], kAudibleHz, (double)kLobe * kRate / (double)kN);
      }
      return FactorNeeded(db);
   }

   struct RealClip
   {
      std::string name;
      std::vector<double> x;  // 48 kHz, peak = level
      std::vector<float> x64; // ideal 64x
   };
}

bool RunFieldRealTest()
{
   const char* dir = getenv("INFINITE_FIELDREAL");
   const char* clipDir = getenv("INFINITE_FIELDREAL_CLIPS");
   const char* mode = getenv("INFINITE_FIELDREAL_MODE");
   const char* csvPath = getenv("INFINITE_FIELDREAL_CSV");
   if (dir == nullptr || clipDir == nullptr || mode == nullptr)
   {
      printf("FIELDREAL needs INFINITE_FIELDREAL, INFINITE_FIELDREAL_CLIPS and INFINITE_FIELDREAL_MODE\n");
      return false;
   }
   const std::string md = mode;
   const auto t0 = std::chrono::steady_clock::now();
   auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

   // Clips: <source>-hi (peak 0.9) and <source>-lo (peak 0.25).
   std::vector<RealClip> clips;
   if (md != "p1")
   {
      std::vector<std::filesystem::path> cf;
      std::error_code ec;
      for (const auto& f : std::filesystem::directory_iterator(clipDir, ec))
         if (f.path().extension() == ".field")
            cf.push_back(f.path());
      std::sort(cf.begin(), cf.end());
      for (const auto& p : cf)
      {
         std::string code, err;
         std::vector<double> x;
         if (!ReadText(p, code) || !RenderClip(code, x, err))
         {
            printf("FIELDREAL clip %s failed: %s\n", p.stem().string().c_str(), err.c_str());
            return false;
         }
         for (int lv = 0; lv < 2; lv++)
         {
            RealClip c;
            c.name = p.stem().string() + (lv == 0 ? "-hi" : "-lo");
            const double level = lv == 0 ? 0.9 : 0.25;
            c.x = x;
            for (double& v : c.x)
               v *= level;
            if (md == "check" || md == "measure")
               c.x64 = Upsample64(c.x);
            clips.push_back(std::move(c));
         }
      }
      printf("FIELDREAL %zu clips ready (%.1f s)\n", clips.size(), secs());
   }

   if (md == "clips")
   {
      const char* wavDir = getenv("INFINITE_FIELDREAL_WAVDIR");
      for (const auto& c : clips)
      {
         double peak = 0.0, pw = 0.0;
         for (double v : c.x)
            peak = std::max(peak, std::fabs(v)), pw += v * v;
         printf("FIELDREAL clip %-10s peak %.3f rms %.1f dBFS\n", c.name.c_str(), peak,
                10.0 * std::log10(pw / (double)c.x.size()));
         if (wavDir != nullptr && !WriteWavF32(std::string(wavDir) + "/" + c.name + ".wav", c.x, (int)kRate))
            return false;
      }
      return true;
   }

   if (md == "check")
   {
      // Linear kernels must measure at the float floor on every clip.
      const char* codes[2] = { "out = in * 0.5\n", "state float z = 0\nz = z + 0.2 * (in - z)\nout = z\n" };
      const char* names[2] = { "gain", "one-pole" };
      bool pass = true;
      for (int ci = 0; ci < 2; ci++)
      {
         const char* code = codes[ci];
         Field::SampleProgram prog;
         Field::FieldError ferr;
         if (!Field::CompileSampleProgram(code, nullptr, prog, ferr))
            return false;
         for (const auto& c : clips)
         {
            double db[6];
            const bool ok = MeasureReal(prog, c.x64, db);
            double worst = -400.0;
            for (double v : db)
               worst = std::max(worst, v);
            pass = pass && ok && worst <= -120.0;
            printf("FIELDREAL check %-10s %-9s worst %.1f dB %s (%.1f s)\n", c.name.c_str(), names[ci], worst,
                   ok && worst <= -120.0 ? "ok" : "FAIL", secs());
         }
      }
      printf("FIELDREAL check %s (%.1f s)\n", pass ? "PASS" : "FAIL", secs());
      return pass;
   }

   if (csvPath == nullptr)
   {
      printf("FIELDREAL %s needs INFINITE_FIELDREAL_CSV\n", md.c_str());
      return false;
   }
   // Kernels: the list file if given, else every <dir>/<set>/*.field.
   std::vector<std::string> names;
   if (const char* list = getenv("INFINITE_FIELDREAL_LIST"))
   {
      std::string text;
      if (!ReadText(list, text))
         return false;
      size_t a = 0;
      while (a < text.size())
      {
         size_t b = text.find('\n', a);
         if (b == std::string::npos)
            b = text.size();
         std::string line = text.substr(a, b - a);
         while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
         if (!line.empty())
            names.push_back(line);
         a = b + 1;
      }
   }
   else
   {
      std::error_code ec;
      for (const auto& set : std::filesystem::directory_iterator(dir, ec))
         if (set.is_directory())
            for (const auto& f : std::filesystem::directory_iterator(set.path(), ec))
               if (f.path().extension() == ".field")
                  names.push_back(set.path().filename().string() + "/" + f.path().stem().string());
      std::sort(names.begin(), names.end());
   }
   int shardI = 0, shardN = 1;
   if (const char* sh = getenv("INFINITE_FIELDREAL_SHARD"))
      if (sscanf(sh, "%d/%d", &shardI, &shardN) != 2 || shardN < 1)
         shardI = 0, shardN = 1;

   FILE* csv = fopen(csvPath, "w");
   if (csv == nullptr)
      return false;
   if (md == "measure")
      fprintf(csv, "set,label,clip,status,meas_1,meas_2,meas_4,meas_8,meas_16,meas_32,factor_meas\n");
   else if (md == "p1")
      fprintf(csv, "set,label,status,reason,p1_factor,p1_worst_tone\n");
   int done = 0;
   for (size_t ki = 0; ki < names.size(); ki++)
   {
      if ((int)(ki % (size_t)shardN) != shardI)
         continue;
      const std::string& nm = names[ki];
      const size_t slash = nm.find('/');
      const std::string set = nm.substr(0, slash), label = nm.substr(slash + 1);
      std::string code;
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!ReadText(std::filesystem::path(dir) / set / (label + ".field"), code) ||
          !Field::CompileSampleProgram(code, nullptr, prog, ferr))
      {
         printf("FIELDREAL %s does not load\n", nm.c_str());
         continue;
      }
      if (md == "measure")
      {
         for (const auto& c : clips)
         {
            double db[6];
            const bool ok = MeasureReal(prog, c.x64, db);
            const int fm = ok ? FactorNeeded(db) : -1;
            fprintf(csv, "%s,\"%s\",%s,%s", set.c_str(), label.c_str(), c.name.c_str(), ok ? "ok" : "silent");
            for (int k = 0; k < 6; k++)
               ok ? fprintf(csv, ",%.2f", db[k]) : fprintf(csv, ",");
            ok ? fprintf(csv, ",%d\n", fm) : fprintf(csv, ",\n");
            if (ok)
               printf("FIELDREAL %s/%-12s %-9s %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f | %s\n", set.c_str(), label.c_str(),
                      c.name.c_str(), db[0], db[1], db[2], db[3], db[4], db[5],
                      fm == 0 ? ">32" : std::to_string(fm).c_str());
            else
               printf("FIELDREAL %s/%-12s %-9s silent\n", set.c_str(), label.c_str(), c.name.c_str());
         }
      }
      else if (md == "p1")
      {
         const double tones[7] = { 100.0, 300.0, 1000.0, 3000.0, 5001.0, 10000.0, 15000.0 };
         const float amps[3] = { 0.25f, 0.5f, 0.9f };
         int best = -1;
         std::string worst, why;
         bool ok = true;
         for (double f0 : tones)
            for (float a : amps)
            {
               if (!ok)
                  break;
               const int g = std::clamp(13 + (int)std::lround(std::log2(5001.0 / f0)), 13, 17);
               const int fp = PredictFactorGrid(prog, f0, a, g, why);
               if (fp < 0)
               {
                  ok = false;
                  break;
               }
               if (FactorRank(fp) > FactorRank(best < 0 ? 1 : best) || best < 0)
               {
                  best = fp;
                  char b[64];
                  snprintf(b, sizeof(b), "%g Hz at %.2f", f0, a);
                  worst = b;
               }
            }
         for (char& ch : why)
            if (ch == '"' || ch == '\n')
               ch = '\'';
         fprintf(csv, "%s,\"%s\",%s,\"%s\",%s,\"%s\"\n", set.c_str(), label.c_str(), ok ? "ok" : "declined",
                 ok ? "" : why.c_str(), ok ? std::to_string(best).c_str() : "", ok ? worst.c_str() : "");
         printf("FIELDREAL p1 %s/%-12s %s\n", set.c_str(), label.c_str(),
                ok ? (std::to_string(best) + " (" + worst + ")").c_str() : ("declined: " + why).c_str());
      }
      fflush(csv);
      fflush(stdout);
      done++;
   }
   fclose(csv);
   printf("FIELDREAL %s: %d kernels (%.1f s)\n", md.c_str(), done, secs());
   return true;
}
}

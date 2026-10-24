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
}

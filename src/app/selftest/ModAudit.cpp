// INFINITE_MODAUDIT: a measurement audit of every modulator (IModulator).
//
// The control-signal twin of FXAUDIT and IMGAUDIT. Each section drives the
// real nodes off the real Transport (live sample clock, offline render clock,
// or the no-audio fallback), so "is this as accurate as Bitwig / Ableton /
// Serum" is a table, not an opinion (docs/plans/modulation/README.md):
//
//   CLOCK     Beats() vs exact sample arithmetic over 10 minutes: live audio
//             clock at several rates/blocks/tempos, a tempo change, a loop,
//             a seek, the offline clock, and the no-audio Tick fallback.
//             Drift in samples.
//   SHAPE     LFO shapes, Remap and phase against the analytic formula (near
//             0 and at beat 1e5), the phase origin of each shape, S&H hold,
//             Vibrato against sin(2 pi f t), Random/Pattern smooth continuity.
//   QUANT     each MusicTime division as an LFO rate: does the slider reach
//             it, how far float storage drifts the phase in 10 min in beats
//             mode (legacy), and in synced mode (exact period).
//   ZIPPER    the delivery path to an audio destination - one Value01() per
//             UI frame on the frame clock (Transport::BeginFrameClock),
//             pushed to the real ParamMailbox and read per sample through its
//             stamped ramps - vs the ideal per-sample curve delayed by the
//             ramp delay. "excess" = worst per-sample step beyond the ideal
//             curve's own step, as a fraction of range; lag reported apart.
//             (Real Transport, nodes and mailbox; only the frame and block
//             schedule and the wall clock are modelled.)
//   VIDEO     value on each 60 fps frame live (frame clock latched between
//             audio blocks) vs offline (exact video time): render - realtime
//             gap, and bit-exactness where both clocks sit on the same sample.
//   DSMOOTH   per-destination smoothing (Modulation::SmoothStep) at 24..144
//             fps vs 1440 fps on the true input (includes input sampling)
//             and on the same frame samples (the filter alone).
//   FRATE     stateful nodes, new maths and legacy, offline at 24..144 fps
//             vs 1440 fps, both references as in DSMOOTH. Legacy rows report.
//   ENV       Envelope ADSR output against the analytic ADSR at 48 kHz and
//             24/60/61 fps; legacy centred stage times; Audio to CV.
//   SEEK      seek / loop wrap / tempo change behaviour of pure and stateful
//             nodes.
//   IDEM      1 vs 8 Value01() calls per frame (new-modulator-node 2).
//   SEED      same seed = same output; distinct spawns differ; LFO S&H; legacy.
//   CPU       ns per Value01() for every registered modulator, plus its
//             output range over a 4 s run.
//
// Only hard invariants fail the run (non-finite output, or a clamped node
// leaving 0..1). Everything else is reported, with FLAG lines past the
// success-metric thresholds, for a person to triage against the baseline.
#include "core/ModShapes.h"
#include "app/AppShared.h"
#include "audio/AudioBuffer.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"
#include "nodes/MacroNodes.h"
#include "nodes/MidiNodes.h"
#include "nodes/ModulatorNodes.h"
#include "nodes/NoteNodes.h"
#include "nodes/OscNodes.h"
#include "nodes/PathNode.h"
#include "nodes/PredictionNodes.h"
#include "nodes/PredictiveModulatorNode.h"
#include "nodes/GeometryTableNode.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace app
{
namespace
{
   // Success metrics (docs/plans/modulation/README.md section 4).
   constexpr double kDriftLimitSamples = 1.0;
   constexpr double kStepLimit = 1.0 / 1024.0;
   constexpr double kShapeLimit = 1e-4;
   constexpr double kStatefulLimit = 1e-4;
   constexpr double kSr = 48000.0;
   constexpr double kTwoPi = 6.283185307179586;

   struct Flags
   {
      int count = 0;
      void Add(const char* section, const std::string& who, const char* what, double v)
      {
         printf("MODAUDIT FLAG %-7s %-22s %-14s %.4g\n", section, who.c_str(), what, v);
         count++;
      }
   };

   Transport& T() { return Transport::Instance(); }

   // Every section starts from a clean, stopped clock at time 0.
   void ResetClock(float bpm)
   {
      T().NotifyAudioEngineStopped();
      T().SetOfflineMode(false);
      T().SetLoop(false, 0.0, 0.0);
      T().SetPlaying(false);
      T().SetTempo(bpm);
      T().Seek(0.0); // commits the tempo and zeroes every offset
   }
   void StartLive(float bpm, double sr)
   {
      ResetClock(bpm);
      T().NotifyAudioEngineStarted(sr);
      T().SetPlaying(true);
   }
   void StartOffline(float bpm)
   {
      ResetClock(bpm);
      T().SetOfflineMode(true, kSr);
      T().SetPlaying(true);
      T().SetOfflineVideoTime(0.0);
   }
   void StartFallback(float bpm)
   {
      ResetClock(bpm);
      T().SetPlaying(true);
   }
   void At(double seconds) { T().SetOfflineVideoTime(seconds); }

   // A patched input driven by a function of transport seconds.
   struct FnMod : IModulator
   {
      std::function<float(double)> f;
      float Value01() override { return f(T().Seconds()); }
   };

   template <class N>
   std::unique_ptr<N> Make()
   {
      return std::unique_ptr<N>(static_cast<N*>(N::Create()));
   }

   // A node as a patch saved before Block 3 loads it (BeforeLoadParams sets
   // the legacy flags), for the "legacy" rows next to the new maths.
   template <class N>
   std::unique_ptr<N> MakeLegacy()
   {
      auto n = Make<N>();
      n->BeforeLoadParams();
      return n;
   }

   double LfoRef(int shape, double t)
   {
      t -= std::floor(t);
      switch (shape)
      {
         case 0: return 0.5 + 0.5 * std::sin(t * kTwoPi);
         case 1: return t < 0.5 ? t * 2.0 : 2.0 - t * 2.0;
         case 2: return t;
         case 3: return 1.0 - t;
         default: return t < 0.5 ? 0.0 : 1.0;
      }
   }
   const char* kShapeName[] = { "sine", "triangle", "saw up", "saw down", "square", "S&H" };

   // ======================================================================== CLOCK
   void AuditClock(Flags& flags)
   {
      const double dur = 600.0;
      struct Case { double sr; int block; float bpm; };
      const Case cases[] = { { 48000, 512, 120.0f }, { 44100, 441, 128.0f }, { 96000, 1024, 174.0f }, { 48000, 128, 97.3f } };
      for (const Case& c : cases)
      {
         StartLive(c.bpm, c.sr);
         const long double bpm = (long double)T().Tempo();
         const long long total = (long long)(dur * c.sr);
         double worst = 0.0;
         for (long long n = 0; n < total;)
         {
            T().AdvanceAudioClock(c.block);
            n += c.block;
            const long double exact = (long double)n * bpm / (60.0L * c.sr);
            const double err = (double)std::fabs((long double)T().Beats() - exact) * 60.0 * c.sr / (double)bpm;
            worst = std::max(worst, err);
         }
         char who[64];
         std::snprintf(who, sizeof(who), "live %g/%d %gbpm", c.sr, c.block, (double)c.bpm);
         printf("MODAUDIT CLOCK %-26s drift %.3g samples over %.0f s\n", who, worst, dur);
         if (worst >= kDriftLimitSamples)
            flags.Add("CLOCK", who, "drift", worst);
      }

      // Tempo change half way: 120 -> 140 lands on the next block.
      {
         StartLive(120.0f, kSr);
         const int block = 512;
         const long long total = (long long)(dur * kSr);
         long long n = 0, n0 = -1;
         double worst = 0.0, jump = 0.0;
         while (n < total)
         {
            if (n0 < 0 && n >= total / 2)
            {
               const double before = T().Beats();
               T().SetTempo(140.0f);
               n0 = n;
               T().AdvanceAudioClock(block);
               n += block;
               jump = std::fabs(T().Beats() - before - block * 140.0 / 60.0 / kSr) * 60.0 * kSr / 140.0;
            }
            else
            {
               T().AdvanceAudioClock(block);
               n += block;
            }
            long double exact = (long double)n * 120.0L / (60.0L * kSr);
            if (n0 >= 0)
               exact = (long double)n0 * 120.0L / (60.0L * kSr) + (long double)(n - n0) * 140.0L / (60.0L * kSr);
            const double bpmNow = n0 >= 0 ? 140.0 : 120.0;
            worst = std::max(worst, (double)std::fabs((long double)T().Beats() - exact) * 60.0 * kSr / bpmNow);
         }
         printf("MODAUDIT CLOCK %-26s drift %.3g samples, jump at change %.3g samples\n", "tempo 120->140 @300 s", worst, jump);
         if (worst >= kDriftLimitSamples || jump >= kDriftLimitSamples)
            flags.Add("CLOCK", "tempo change", "drift/jump", std::max(worst, jump));
      }

      // Loop: 16 beats (block-aligned) and 3.7 beats (not).
      for (double loopEnd : { 16.0, 3.7 })
      {
         StartLive(120.0f, kSr);
         T().SetLoop(true, 0.0, loopEnd);
         const int block = 512;
         const long long total = (long long)(dur * kSr);
         double worst = 0.0;
         for (long long n = 0; n < total;)
         {
            T().AdvanceAudioClock(block);
            n += block;
            const long double lin = (long double)n * 120.0L / (60.0L * kSr);
            const double exact = (double)std::fmod(lin, (long double)loopEnd);
            double d = std::fabs(T().Beats() - exact);
            d = std::min(d, loopEnd - d);
            worst = std::max(worst, d * 60.0 * kSr / 120.0);
         }
         char who[48];
         std::snprintf(who, sizeof(who), "loop 0..%g beats", loopEnd);
         printf("MODAUDIT CLOCK %-26s drift %.3g samples over %.0f s\n", who, worst, dur);
         if (worst >= kDriftLimitSamples)
            flags.Add("CLOCK", who, "drift", worst);
      }

      // Seek while the audio clock runs.
      {
         StartLive(120.0f, kSr);
         for (int i = 0; i < 5000; i++)
            T().AdvanceAudioClock(512);
         T().SeekBeats(37.25);
         const double pending = T().Beats();
         T().AdvanceAudioClock(512);
         const double after = T().Beats();
         const double errPending = std::fabs(pending - 37.25) * 60.0 * kSr / 120.0;
         const double errAfter = std::fabs(after - (37.25 + 512.0 * 2.0 / kSr)) * 60.0 * kSr / 120.0;
         printf("MODAUDIT CLOCK %-26s pending %.3g samples, after block %.3g samples\n", "seek 37.25 beats live", errPending, errAfter);
         if (std::max(errPending, errAfter) >= kDriftLimitSamples)
            flags.Add("CLOCK", "seek live", "error", std::max(errPending, errAfter));
      }

      // Offline render clock: audio blocks and video frames.
      {
         StartOffline(120.0f);
         double worstA = 0.0, worstV = 0.0;
         const long long total = (long long)(dur * kSr);
         for (long long n = 0; n < total;)
         {
            T().BeginOfflineAudioBlock(512);
            n += 512;
            const long double exact = (long double)n * 2.0L / kSr;
            worstA = std::max(worstA, (double)std::fabs((long double)T().Beats() - exact) * 0.5 * kSr);
            T().EndOfflineAudioBlock();
         }
         StartOffline(120.0f);
         for (int k = 0; k <= (int)(dur * 30); k++)
         {
            At(k / 30.0);
            worstV = std::max(worstV, std::fabs(T().Beats() - 2.0 * k / 30.0) * 0.5 * kSr);
         }
         printf("MODAUDIT CLOCK %-26s audio drift %.3g samples, video drift %.3g samples\n", "offline", worstA, worstV);
         if (std::max(worstA, worstV) >= kDriftLimitSamples)
            flags.Add("CLOCK", "offline", "drift", std::max(worstA, worstV));
      }

      // No-audio fallback (Tick with the frame's float dt): accumulation
      // error against the long-double sum of the same dts.
      {
         StartFallback(120.0f);
         long double sum = 0.0L;
         for (int k = 0; k < (int)(dur * 60); k++)
         {
            const float dt = (k & 1) ? 1.0f / 59.0f : 1.0f / 61.0f;
            T().Tick(dt);
            sum += (long double)dt;
         }
         const double err = (double)std::fabs((long double)T().Beats() - sum * 2.0L) * 0.5 * kSr;
         printf("MODAUDIT CLOCK %-26s drift %.3g samples over %.0f s (wall-clock driven, not sample-locked)\n", "no-audio Tick fallback", err, dur);
         if (err >= kDriftLimitSamples)
            flags.Add("CLOCK", "Tick fallback", "drift", err);
      }
   }

   // ======================================================================== SHAPE
   double LfoAtBeats(LFONode& lfo, double beats)
   {
      T().SeekBeats(beats); // fallback clock: Beats() returns exactly this
      return lfo.Value01();
   }

   void AuditShapes(Flags& flags)
   {
      ResetClock(120.0f);
      const int N = 4096;
      for (int shape = 0; shape <= 4; shape++)
      {
         auto lfo = Make<LFONode>();
         lfo->shape = shape;
         lfo->rateBeats = 1.0f;
         double worst = 0.0, worstFar = 0.0, worstPhase = 0.0, worstRemap = 0.0;
         for (int i = 0; i < N; i++)
         {
            const double t = (i + 0.5) / N;
            worst = std::max(worst, std::fabs(LfoAtBeats(*lfo, t) - LfoRef(shape, t)));
            worstFar = std::max(worstFar, std::fabs(LfoAtBeats(*lfo, 1e5 + t) - LfoRef(shape, t)));
         }
         lfo->phase = 0.25f;
         for (int i = 0; i < N; i++)
         {
            const double t = (i + 0.5) / N;
            worstPhase = std::max(worstPhase, std::fabs(LfoAtBeats(*lfo, t) - LfoRef(shape, t + 0.25)));
         }
         lfo->phase = 0.0f;
         lfo->low = 0.2f;
         lfo->high = 0.7f;
         for (int i = 0; i < N; i++)
         {
            const double t = (i + 0.5) / N;
            worstRemap = std::max(worstRemap, std::fabs(LfoAtBeats(*lfo, t) - (0.2 + 0.5 * LfoRef(shape, t))));
         }
         lfo->low = 0.0f;
         lfo->high = 1.0f;
         const double v0 = LfoAtBeats(*lfo, 0.0);
         const double v1 = LfoAtBeats(*lfo, 1.0 / N);
         printf("MODAUDIT SHAPE LFO %-9s err %.2e, @beat 1e5 %.2e, phase .25 %.2e, low/high %.2e | origin %.3f %s\n",
                kShapeName[shape], worst, worstFar, worstPhase, worstRemap, v0,
                v1 > v0 ? "rising" : (v1 < v0 ? "falling" : "flat"));
         const double w = std::max(std::max(worst, worstFar), std::max(worstPhase, worstRemap));
         if (w > kShapeLimit)
            flags.Add("SHAPE", std::string("LFO ") + kShapeName[shape], "max err", w);
      }

      // S&H: holds within a cycle, moves between cycles.
      {
         auto lfo = Make<LFONode>();
         lfo->shape = 5;
         lfo->rateBeats = 1.0f;
         double drift = 0.0;
         int changes = 0;
         double prev = -1.0;
         for (int c = 0; c < 64; c++)
         {
            const double first = LfoAtBeats(*lfo, c + 0.001);
            for (int i = 1; i < 64; i++)
               drift = std::max(drift, std::fabs(LfoAtBeats(*lfo, c + i / 64.0) - first));
            if (prev >= 0.0 && first != prev)
               changes++;
            prev = first;
         }
         printf("MODAUDIT SHAPE LFO S&H     hold drift %.2e, new value on %d of 63 cycle edges\n", drift, changes);
      }

      // Vibrato: free-running Hz on Transport::Seconds().
      {
         StartOffline(120.0f);
         auto vib = Make<VibratoNode>();
         double worst = 0.0, worstFar = 0.0;
         for (int i = 0; i < 10000; i++)
         {
            const double s = i / 1000.0;
            At(s);
            worst = std::max(worst, std::fabs(vib->Value01() - (0.5 + 0.5 * std::sin(kTwoPi * vib->rateHz * s))));
            At(3600.0 + s);
            worstFar = std::max(worstFar, std::fabs(vib->Value01() - (0.5 + 0.5 * std::sin(kTwoPi * vib->rateHz * (3600.0 + s)))));
         }
         printf("MODAUDIT SHAPE Vibrato 5 Hz  err %.2e, @1 h %.2e\n", worst, worstFar);
         if (std::max(worst, worstFar) > kShapeLimit)
            flags.Add("SHAPE", "Vibrato", "max err", std::max(worst, worstFar));
         ResetClock(120.0f);
      }

      // Rate in Hz (transport seconds, like Vibrato) and a synced triplet
      // (the exact MusicTime period, not the float one), against the formula.
      {
         StartOffline(120.0f);
         auto lfo = Make<LFONode>();
         lfo->rateMode = LFONode::kRateHz;
         lfo->rateHz = 3.7f;
         double worst = 0.0;
         for (int i = 0; i < 10000; i++)
         {
            const double s = i / 1000.0;
            for (double base : { 0.0, 3600.0 })
            {
               At(base + s);
               worst = std::max(worst, std::fabs(lfo->Value01() - LfoRef(0, (double)lfo->rateHz * (base + s))));
            }
         }
         printf("MODAUDIT SHAPE LFO Hz 3.7    err %.2e (0..10 s and @1 h)\n", worst);
         if (worst > kShapeLimit)
            flags.Add("SHAPE", "LFO Hz", "max err", worst);
         ResetClock(120.0f);
      }
      {
         auto lfo = Make<LFONode>();
         lfo->shape = 2;
         const double exact = MusicTime::BeatsFor(MusicTime::kQuarterTrip);
         lfo->rateBeats = (float)exact;
         double worst = 0.0;
         for (int i = 0; i < 4096; i++)
         {
            const double b = 1e5 + i * 0.0137;
            const double want = LfoRef(2, b / exact);
            double d = std::fabs(LfoAtBeats(*lfo, b) - want);
            d = std::min(d, 1.0 - d); // saw wrap
            worst = std::max(worst, d);
         }
         printf("MODAUDIT SHAPE LFO synced 1/4T  err %.2e @beat 1e5 (period %.9f, exact %.9f)\n", worst, lfo->PeriodBeats(), exact);
         if (worst > kShapeLimit)
            flags.Add("SHAPE", "LFO synced", "max err", worst);
      }
      // Free run: phase from the first call, at the rate, whatever the transport does.
      {
         StartOffline(120.0f);
         auto lfo = Make<LFONode>();
         lfo->phaseMode = LFONode::kPhaseFree;
         lfo->rateMode = LFONode::kRateHz;
         lfo->rateHz = 2.0f;
         lfo->shape = 2;
         double worst = 0.0;
         for (int k = 0; k <= 600; k++)
         {
            At(k / 60.0);
            double d = std::fabs(lfo->Value01() - LfoRef(2, 2.0 * k / 60.0));
            worst = std::max(worst, std::min(d, 1.0 - d));
         }
         printf("MODAUDIT SHAPE LFO free 2 Hz err %.2e over 10 s at 60 fps\n", worst);
         if (worst > kShapeLimit)
            flags.Add("SHAPE", "LFO free", "max err", worst);
         ResetClock(120.0f);
      }

      // Random / Pattern smooth: value and slope continuity, per beat.
      auto continuity = [](const std::function<double(double)>& f, double& valueJump, double& slopeJump) {
         const int perBeat = 8192;
         const double h = 1.0 / perBeat;
         valueJump = slopeJump = 0.0;
         double prevD = std::nan("");
         for (int i = 0; i < perBeat * 16; i++)
         {
            const double b = i * h;
            const double a = f(b), c = f(b + h);
            const double d = (c - a) / h; // slope per beat
            valueJump = std::max(valueJump, std::fabs(c - a));
            if (!std::isnan(prevD))
               slopeJump = std::max(slopeJump, std::fabs(d - prevD));
            prevD = d;
         }
      };
      for (float smooth : { 0.5f, 1.0f })
      {
         auto rnd = Make<RandomNode>();
         rnd->seed = 7.3f;
         rnd->rateBeats = 1.0f;
         rnd->smooth = smooth;
         double vj, sj;
         continuity([&](double b) { T().SeekBeats(b); return (double)rnd->Value01(); }, vj, sj);
         printf("MODAUDIT SHAPE Random smooth %.1f  max value step %.2e per 1/8192 beat, max slope jump %.3g /beat\n", smooth, vj, sj);
      }
      {
         auto rnd = Make<RandomNode>();
         rnd->seed = 7.3f;
         rnd->smooth = 0.0f;
         double vj, sj;
         continuity([&](double b) { T().SeekBeats(b); return (double)rnd->Value01(); }, vj, sj);
         printf("MODAUDIT SHAPE Random stepped    max value step %.3g (a stepped S&H, by design)\n", vj);
      }
      {
         auto pat = Make<PatternNode>();
         double worst = 0.0;
         for (int i = 0; i < pat->length; i++)
         {
            T().SeekBeats(i * pat->stepBeats + 0.5);
            worst = std::max(worst, (double)std::fabs(pat->Value01() - pat->steps[i]));
         }
         pat->smoothSteps = true;
         double vj, sj;
         continuity([&](double b) { T().SeekBeats(b); return (double)pat->Value01(); }, vj, sj);
         printf("MODAUDIT SHAPE Pattern          step value err %.2e | smooth: max value step %.2e, slope jump %.3g /beat\n", worst, vj, sj);
         if (worst > kShapeLimit)
            flags.Add("SHAPE", "Pattern", "step err", worst);
      }
   }

   // ======================================================================== QUANT
   void AuditQuant(Flags& flags)
   {
      // LFO/Random rate is a free float slider (ParamBodies1.cpp: 0.05..32
      // beats); nothing snaps it to MusicTime. What a user typing the
      // division in gets, stored as float, after 10 min at 120 bpm:
      const double beats10 = 1200.0;
      double worst = 0.0;
      int outOfRange = 0;
      for (int d = 0; d < MusicTime::kNumRateDivisions; d++)
      {
         const double exact = MusicTime::BeatsFor((MusicTime::RateDivision)d);
         const float stored = (float)exact;
         const bool inRange = stored >= 0.05f && stored <= 32.0f;
         if (!inRange)
            outOfRange++;
         const double cyclesErr = beats10 / (double)stored - beats10 / exact;
         const double samples = std::fabs(cyclesErr) * exact * 0.5 * kSr;
         worst = std::max(worst, samples);
         printf("MODAUDIT QUANT %-6s %8.5f beats  slider %s  float drift %.3g samples / 10 min\n",
                MusicTime::RateDivisionName(d), exact, inRange ? "ok " : "OUT", samples);
      }
      // Synced (the new default): the division dropdown stores the float, the
      // node runs at the exact MusicTime period it names.
      double worstSynced = 0.0;
      {
         auto lfo = Make<LFONode>();
         for (int d = 0; d < MusicTime::kNumRateDivisions; d++)
         {
            const double exact = MusicTime::BeatsFor((MusicTime::RateDivision)d);
            lfo->rateBeats = (float)exact;
            const double cyclesErr = beats10 / lfo->PeriodBeats() - beats10 / exact;
            worstSynced = std::max(worstSynced, std::fabs(cyclesErr) * exact * 0.5 * kSr);
         }
      }
      printf("MODAUDIT QUANT summary: %d divisions outside the LFO slider; legacy beats mode float drift %.3g samples (saved patches, kept); synced mode drift %.3g samples\n",
             outOfRange, worst, worstSynced);
      if (worstSynced >= kDriftLimitSamples)
         flags.Add("QUANT", "synced rate", "drift", worstSynced);
      if (outOfRange > 0)
         flags.Add("QUANT", "LFO rate slider", "out of range", outOfRange);
   }

   // ======================================================================== ZIPPER
   struct PureSource
   {
      std::string name;
      std::function<std::unique_ptr<INode>()> make;
   };
   std::vector<PureSource> PureSources()
   {
      std::vector<PureSource> s;
      auto lfo = [](int shape, float rate) {
         return [shape, rate]() { auto n = Make<LFONode>(); n->shape = shape; n->rateBeats = rate; return std::unique_ptr<INode>(n.release()); };
      };
      s.push_back({ "LFO sine 1/4 beat", lfo(0, 0.25f) });
      s.push_back({ "LFO sine 1 beat", lfo(0, 1.0f) });
      s.push_back({ "LFO sine 4 beats", lfo(0, 4.0f) });
      s.push_back({ "LFO tri 1 beat", lfo(1, 1.0f) });
      s.push_back({ "Random smooth 1 beat", []() { auto n = Make<RandomNode>(); n->seed = 7.3f; n->smooth = 1.0f; return std::unique_ptr<INode>(n.release()); } });
      s.push_back({ "Pattern smooth", []() { auto n = Make<PatternNode>(); n->smoothSteps = true; return std::unique_ptr<INode>(n.release()); } });
      s.push_back({ "Vibrato 5 Hz", []() { return std::unique_ptr<INode>(Make<VibratoNode>().release()); } });
      s.push_back({ "LFO sine 2.5 Hz", []() { auto n = Make<LFONode>(); n->rateMode = LFONode::kRateHz; n->rateHz = 2.5f; return std::unique_ptr<INode>(n.release()); } });
      return s;
   }
   IModulator* AsMod(INode* n) { return dynamic_cast<IModulator*>(n); }

   // The modelled wall clock Transport reads in these sections
   // (Transport::SetWallClockForTest), so the frame clock interpolates
   // against the schedule below rather than the machine's own clock.
   double gWall = 0.0;
   double ModelWall() { return gWall; }

   // One UI frame as ApplyModulationAndPalette runs it: latch the frame
   // clock, read the modulator, publish the frame's stamp and delay, push.
   float RunUiFrame(IModulator* m, ParamMailbox* box, double frameDt)
   {
      T().BeginFrameClock();
      const float v = m->Value01();
      if (box != nullptr)
      {
         ParamMailbox::PublishFrame(T().EngineFrameSeconds(),
                                    std::clamp(frameDt + T().LastBlockSeconds(), 0.002, 0.25));
         box->Push(0, v);
      }
      return v;
   }

   void AuditZipper(Flags& flags)
   {
      const double dur = 8.0;
      const int total = (int)(dur * kSr);
      // jitter: frames land 0..3 ms late and callbacks 0..1 ms late on the
      // wall clock (deterministic), as a loaded host does.
      struct Sched { int block; double fps; bool jitter; };
      const Sched scheds[] = { { 512, 60.0, false }, { 128, 60.0, false }, { 512, 144.0, false }, { 512, 24.0, false }, { 512, 60.0, true } };
      Transport::SetWallClockForTest(&ModelWall);
      for (const PureSource& src : PureSources())
      {
         // Ideal per-sample curve: the same pure node at each sample's exact time.
         std::vector<float> ideal(total);
         {
            StartOffline(120.0f);
            auto node = src.make();
            IModulator* m = AsMod(node.get());
            for (int n = 0; n < total; n++)
            {
               At(n / kSr);
               ideal[n] = m->Value01();
            }
         }
         for (const Sched& sc : scheds)
         {
            // The real path: frames on the modelled schedule, the real
            // frame clock, the real ParamMailbox ramps read once per sample.
            // Callback j starts at wall j*block/sr and covers samples
            // [j*block, (j+1)*block); a frame at wall tw lands between them.
            StartLive(120.0f, kSr);
            gWall = 0.0;
            auto node = src.make();
            IModulator* m = AsMod(node.get());
            auto box = std::make_unique<ParamMailbox>();
            box->PrepareToPlay(kSr);
            box->SetImmediate(0, m->Value01());
            ParamMailbox::PublishFrame(-1.0, 0.0);
            const double lag = 1.0 / sc.fps + sc.block / kSr; // the published delay
            int nextFrame = 0;
            double excess = 0.0, sq = 0.0, sqRaw = 0.0;
            long long counted = 0;
            float prevD = 0.0f;
            uint32_t rng = 12345u;
            auto jit = [&](double maxSeconds) {
               rng = rng * 1664525u + 1013904223u;
               return sc.jitter ? maxSeconds * (double)(rng >> 8) / 16777216.0 : 0.0;
            };
            double frameWall = jit(0.003);
            for (int j = 0; j * sc.block < total; j++)
            {
               const double callbackTime = (double)j * sc.block / kSr + jit(0.001);
               while (frameWall <= callbackTime)
               {
                  gWall = frameWall;
                  RunUiFrame(m, box.get(), 1.0 / sc.fps);
                  nextFrame++;
                  frameWall = nextFrame / sc.fps + jit(0.003);
               }
               gWall = callbackTime;
               T().AdvanceAudioClock(sc.block);
               for (int i = 0; i < sc.block; i++)
               {
                  const int n = j * sc.block + i;
                  if (n >= total)
                     break;
                  const float d = box->SmoothedValue(0);
                  sqRaw += (double)(d - ideal[n]) * (d - ideal[n]);
                  // Compare against the ideal curve delayed by the ramp delay:
                  // the delivery is a fixed-latency interpolation, and the
                  // latency is reported on its own.
                  const double nd = n - lag * kSr;
                  if (nd >= 1.0 && n > (int)(0.5 * kSr))
                  {
                     const int i0 = (int)nd;
                     const double f = nd - i0;
                     const double id = ideal[i0] + (ideal[i0 + 1] - ideal[i0]) * f;
                     const double idPrev = ideal[i0 - 1] + (ideal[i0] - ideal[i0 - 1]) * f;
                     excess = std::max(excess, std::fabs((double)(d - prevD) - (id - idPrev)));
                     sq += (d - id) * (d - id);
                     counted++;
                  }
                  prevD = d;
               }
            }
            char who[64];
            std::snprintf(who, sizeof(who), "%s b%d %gfps%s", src.name.c_str(), sc.block, sc.fps, sc.jitter ? " jit" : "");
            printf("MODAUDIT ZIPPER %-34s excess step %.2e (limit %.2e)  lag %.1f ms  tracking rms %.3g after lag, %.3g raw\n",
                   who, excess, kStepLimit, lag * 1000.0, std::sqrt(sq / std::max(1LL, counted)), std::sqrt(sqRaw / total));
            if (excess > kStepLimit)
               flags.Add("ZIPPER", src.name, "excess step", excess);
         }
      }
      Transport::SetWallClockForTest(nullptr);
      ParamMailbox::PublishFrame(-1.0, 0.0);
   }

   // ======================================================================== VIDEO
   void AuditVideo(Flags& flags)
   {
      const double dur = 10.0, fps = 60.0;
      const int frames = (int)(dur * fps);
      std::vector<PureSource> sources = PureSources();
      sources.push_back({ "Macro Step Gate", []() { return std::unique_ptr<INode>(Make<MacroStepGateNode>().release()); } });
      Transport::SetWallClockForTest(&ModelWall);
      for (const PureSource& src : sources)
      {
         std::vector<float> offline(frames);
         {
            StartOffline(120.0f);
            auto node = src.make();
            for (int k = 0; k < frames; k++)
            {
               At(k / fps);
               offline[k] = AsMod(node.get())->Value01();
            }
         }
         // Live: callbacks due by each frame's wall time, then the frame
         // latches its clock (BeginFrameClock), as ApplyModulationAndPalette does.
         // Frames land at a different phase of the block each time.
         double worst = 0.0, minOff = 1e9, maxOff = -1e9;
         {
            StartLive(120.0f, kSr);
            gWall = 0.0;
            auto node = src.make();
            long long n = 0;
            for (int k = 0; k < frames; k++)
            {
               const double tw = k / fps;
               while ((double)n / kSr <= tw)
               {
                  gWall = (double)n / kSr;
                  T().AdvanceAudioClock(512);
                  n += 512;
               }
               gWall = tw;
               const float v = RunUiFrame(AsMod(node.get()), nullptr, 1.0 / fps);
               const double off = (T().Seconds() - tw) * 1000.0;
               minOff = std::min(minOff, off);
               maxOff = std::max(maxOff, off);
               worst = std::max(worst, (double)std::fabs(v - offline[k]));
            }
         }
         // Same, with callbacks 0..1 ms late on the wall clock (the latch
         // reads the late wall start as the block's time).
         double worstJit = 0.0;
         {
            StartLive(120.0f, kSr);
            gWall = 0.0;
            auto node = src.make();
            uint32_t rng = 777u;
            long long n = 0;
            for (int k = 0; k < frames; k++)
            {
               const double tw = k / fps;
               while ((double)n / kSr <= tw)
               {
                  rng = rng * 1664525u + 1013904223u;
                  gWall = std::min(tw, (double)n / kSr + 0.001 * (double)(rng >> 8) / 16777216.0);
                  T().AdvanceAudioClock(512);
                  n += 512;
               }
               gWall = tw;
               const float v = RunUiFrame(AsMod(node.get()), nullptr, 1.0 / fps);
               worstJit = std::max(worstJit, (double)std::fabs(v - offline[k]));
            }
         }
         // Same sample on both clocks: bit-exact?
         int mismatches = 0;
         {
            auto a = src.make();
            auto b = src.make();
            for (int k = 1; k <= 200; k++)
            {
               const long long n = (long long)k * 512 * 7;
               StartLive(120.0f, kSr);
               for (long long done = 0; done < n; done += 512)
                  T().AdvanceAudioClock(512);
               const float live = AsMod(a.get())->Value01();
               StartOffline(120.0f);
               At((double)n / kSr);
               const float off = AsMod(b.get())->Value01();
               if (std::memcmp(&live, &off, sizeof(float)) != 0)
                  mismatches++;
            }
         }
         printf("MODAUDIT VIDEO %-22s render-realtime %.3g of range (%.3g with 0..1 ms callback jitter) | live clock %+.3f..%+.3f ms off frame time | same-sample bit mismatches %d/200\n",
                src.name.c_str(), worst, worstJit, minOff, maxOff, mismatches);
         if (worst > kShapeLimit)
            flags.Add("VIDEO", src.name, "render!=live", worst);
         if (mismatches > 0)
            flags.Add("VIDEO", src.name, "bit mismatch", mismatches);
      }
      Transport::SetWallClockForTest(nullptr);
   }

   // ======================================================================== DSMOOTH
   // Per-destination smoothing (Modulation::SmoothStep, the apply loop's
   // smoother) driven at 24..144 fps by a moving and a stepping input, vs a
   // 1440 fps reference at shared instants: is it frame-rate independent?
   void AuditDestSmooth(Flags& flags)
   {
      struct In { const char* name; std::function<double(double)> f; };
      const In ins[] = {
         { "sine 2 Hz", [](double t) { return 0.5 + 0.5 * std::sin(4.0 * 3.14159265358979323846 * t); } },
         { "step at 0.5 s", [](double t) { return t < 0.5 ? 0.0 : 1.0; } },
      };
      const double taus[] = { 0.02, 0.1, 0.5 };
      for (const In& in : ins)
         for (double tau : taus)
         {
            // sampledFps > 0: the input as the frames at that rate see it
            // (a straight line between frame samples), so the reference
            // isolates the filter's own frame-rate dependence from sampling.
            auto run = [&](double fps, std::vector<double>& outAt, double sampledFps = 0.0) {
               const auto input = [&](double t) {
                  if (sampledFps <= 0.0)
                     return in.f(t);
                  const double p = t * sampledFps;
                  const double k0 = std::floor(p + 1e-9);
                  const double a = in.f(k0 / sampledFps), b = in.f((k0 + 1.0) / sampledFps);
                  return a + (b - a) * std::max(0.0, p - k0);
               };
               const int frames = (int)(4.0 * fps);
               const int every = (int)std::lround(fps / 12.0); // shared 12 Hz instants
               double x = input(0.0), y = x;
               outAt.clear();
               for (int k = 1; k <= frames; k++)
               {
                  const double x1 = input(k / fps);
                  y = Modulation::SmoothStep(y, x, x1, 1.0 / fps, tau);
                  x = x1;
                  if (k % every == 0)
                     outAt.push_back(y);
               }
            };
            std::vector<double> ref, at, refSampled;
            run(1440.0, ref);
            double row[3] = {}, filt[3] = {};
            const double fpsList[3] = { 24.0, 60.0, 144.0 };
            for (int i = 0; i < 3; i++)
            {
               run(fpsList[i], at);
               run(1440.0, refSampled, fpsList[i]);
               for (size_t k = 0; k < at.size() && k < ref.size(); k++)
               {
                  row[i] = std::max(row[i], std::fabs(at[k] - ref[k]));
                  filt[i] = std::max(filt[i], std::fabs(at[k] - refSampled[k]));
               }
            }
            printf("MODAUDIT DSMOOTH %-14s tau %3.0f ms  vs 1440 fps true input: 24 %.2e  60 %.2e  144 %.2e | filter vs 1440 fps of the same frame samples: %.1e %.1e %.1e\n",
                   in.name, tau * 1000.0, row[0], row[1], row[2], filt[0], filt[1], filt[2]);
            // The first number includes input sampling (a step is only seen at
            // frame times: physics, not the filter); only the second can flag.
            if (std::max({ filt[0], filt[1], filt[2] }) > kShapeLimit)
               flags.Add("DSMOOTH", in.name, "filter fps dep", std::max({ filt[0], filt[1], filt[2] }));
         }
   }

   // ======================================================================== FRATE
   struct StatefulCase
   {
      std::string name;
      std::function<std::unique_ptr<INode>(FnMod*)> make;
      std::function<float(double)> input;
      bool legacy = false; // maths 0 (a pre-Block 3 patch): reported, never flagged
   };
   float StepInput(double s) { return s < 1.0 ? 0.2f : (s < 2.5 ? 0.8f : 0.4f); }
   float GateInput(double s) { return (s >= 0.5 && s < 2.0) ? 1.0f : 0.0f; }
   float SineInput(double s) { return (float)(0.5 + 0.5 * std::sin(kTwoPi * 1.3 * s)); }

   std::vector<StatefulCase> StatefulCases()
   {
      std::vector<StatefulCase> c;
      for (int legacy = 1; legacy >= 0; legacy--)
      {
         const bool old = legacy != 0;
         const std::string tag = old ? "legacy " : "";
         auto smooth = [old](int mode) {
            return [mode, old](FnMod* in) { auto n = old ? MakeLegacy<SmoothNode>() : Make<SmoothNode>(); n->mode = mode; n->input = in; return std::unique_ptr<INode>(n.release()); };
         };
         c.push_back({ tag + "Smooth amount (default)", smooth(SmoothNode::kPerFrame), StepInput, old });
         c.push_back({ tag + "Smooth time 0.25 s", smooth(SmoothNode::kTime), StepInput, old });
         c.push_back({ tag + "Smooth time, sine in", smooth(SmoothNode::kTime), SineInput, old });
         c.push_back({ tag + "Smooth spring 3 Hz", smooth(SmoothNode::kSpring), StepInput, old });
         c.push_back({ tag + "Smooth spring, sine in", smooth(SmoothNode::kSpring), SineInput, old });
         c.push_back({ tag + "Envelope", [old](FnMod* in) { auto n = old ? MakeLegacy<EnvelopeNode>() : Make<EnvelopeNode>(); n->input = in; return std::unique_ptr<INode>(n.release()); }, GateInput, old });
         c.push_back({ tag + "CV to Pitch glide 100ms", [old](FnMod* in) { auto n = old ? MakeLegacy<CVToPitchNode>() : Make<CVToPitchNode>(); n->glideMs = 100.0f; n->input = in; return std::unique_ptr<INode>(n.release()); }, StepInput, old });
         c.push_back({ tag + "CV to Pitch, sine in", [old](FnMod* in) { auto n = old ? MakeLegacy<CVToPitchNode>() : Make<CVToPitchNode>(); n->glideMs = 100.0f; n->input = in; return std::unique_ptr<INode>(n.release()); }, SineInput, old });
      }
      return c;
   }

   // The input as frames at `fps` see it: a straight line between samples.
   std::function<float(double)> FrameSampled(std::function<float(double)> f, double fps)
   {
      return [f, fps](double t) {
         const double p = t * fps;
         const double k0 = std::floor(p + 1e-9);
         const double a = f(k0 / fps), b = f((k0 + 1.0) / fps);
         return (float)(a + (b - a) * std::max(0.0, p - k0));
      };
   }

   // Runs a stateful case offline at fps for 4 s, calling Value01() `calls`
   // times per frame; returns the value at every 1/6 s checkpoint.
   std::vector<float> RunAtFps(const StatefulCase& sc, double fps, int calls = 1, double sampledFps = 0.0)
   {
      StartOffline(120.0f);
      FnMod in;
      in.f = sampledFps > 0.0 ? FrameSampled(sc.input, sampledFps) : sc.input;
      auto node = sc.make(&in);
      IModulator* m = AsMod(node.get());
      std::vector<float> out;
      const int perCheck = (int)std::lround(fps / 6.0);
      for (int k = 0; k <= (int)std::lround(4.0 * fps); k++)
      {
         At(k / fps);
         float v = 0.0f;
         for (int c = 0; c < calls; c++)
            v = m->Value01();
         if (k % perCheck == 0)
            out.push_back(v);
      }
      return out;
   }

   void AuditFrameRate(Flags& flags)
   {
      for (const StatefulCase& sc : StatefulCases())
      {
         // Two references: the true input at 1440 fps (includes input
         // sampling, which no node can undo: a step between frames is seen
         // at the next frame), and 1440 fps fed the same frame samples as a
         // line (the node maths alone, which is what can be exact).
         const std::vector<float> ref = RunAtFps(sc, 1440.0);
         printf("MODAUDIT FRATE %-31s", sc.name.c_str());
         double worstAll = 0.0, worstMaths = 0.0;
         std::string mathsRow;
         for (double fps : { 24.0, 30.0, 60.0, 120.0, 144.0 })
         {
            const std::vector<float> got = RunAtFps(sc, fps);
            const std::vector<float> refSampled = RunAtFps(sc, 1440.0, 1, fps);
            double worst = 0.0, maths = 0.0;
            for (size_t i = 0; i < std::min(ref.size(), got.size()); i++)
            {
               worst = std::max(worst, (double)std::fabs(got[i] - ref[i]));
               if (i < refSampled.size())
                  maths = std::max(maths, (double)std::fabs(got[i] - refSampled[i]));
            }
            worstAll = std::max(worstAll, worst);
            worstMaths = std::max(worstMaths, maths);
            char cell[32];
            std::snprintf(cell, sizeof(cell), " %.1e", maths);
            mathsRow += cell;
            printf(" %g:%.3g", fps, worst);
         }
         printf("  vs 1440 true | maths vs 1440 same samples:%s\n", mathsRow.c_str());
         if (!sc.legacy && worstMaths > kStatefulLimit)
            flags.Add("FRATE", sc.name, "maths fps dep", worstMaths);
      }
   }

   // ======================================================================== ENV
   // Analytic AHDSR for a gate open on [on, off): attack from 0, hold, decay
   // to sustain, release from the level the gate closed at, each stage on
   // its own bend (MsegSegment's closed form; curve 0 is a straight line).
   double AdsrRef(const EnvelopeNode& e, double on, double off, double t)
   {
      const double a = e.attackMs * 0.001, hd = e.holdMs * 0.001, d = e.decayMs * 0.001, r = e.releaseMs * 0.001;
      const float sus = e.sustainLevel;
      auto held = [&](double u) -> double {
         if (u < 0.0) return 0.0;
         if (u < a) return ModShapes::MsegSegment(0.0f, 1.0f, e.attackCurve, (float)(u / a));
         if (u < a + hd) return 1.0;
         if (u < a + hd + d) return ModShapes::MsegSegment(1.0f, sus, e.decayCurve, (float)((u - a - hd) / d));
         return sus;
      };
      if (t < off)
         return held(t - on);
      const double from = held(off - on), u = t - off;
      return u < r ? ModShapes::MsegSegment((float)from, 0.0f, e.releaseCurve, (float)(u / r)) : 0.0;
   }

   void AuditEnvelope(Flags& flags)
   {
      // New maths (ADSR output): every frame's value against the analytic
      // ADSR. The gate is a step, so the node places its edge where the
      // straight line between the two frames around it crosses the
      // threshold (mid-frame for a 0 -> 1 jump); the reference uses those
      // same edges, so the row is the node maths alone. The edge offset
      // from the true step is reported apart (input sampling).
      for (double rate : { kSr, 60.0, 61.0, 24.0 })
      {
         StartOffline(120.0f);
         FnMod in;
         in.f = GateInput;
         auto env = Make<EnvelopeNode>();
         env->input = &in;
         const double dt = 1.0 / rate;
         const double kOn = std::ceil(0.5 * rate - 1e-9), kOff = std::ceil(2.0 * rate - 1e-9);
         const double on = (kOn - 0.5) * dt, off = (kOff - 0.5) * dt; // mid-frame crossings
         double worst = 0.0;
         for (long long k = 0; k <= (long long)(3.0 * rate); k++)
         {
            const double s = k * dt;
            At(s);
            worst = std::max(worst, std::fabs(env->Value01() - AdsrRef(*env, on, off, s)));
         }
         printf("MODAUDIT ENV   Envelope ADSR @%-6g vs analytic ADSR %.2e | gate edges %.2f / %.2f ms from the true steps (half a frame: input sampling)\n",
                rate, worst, (on - 0.5) * 1000.0, (off - 2.0) * 1000.0);
         if (worst > kStatefulLimit)
            flags.Add("ENV", rate == kSr ? "ADSR @48k" : "ADSR @fps", "vs analytic", worst);
      }

      // Legacy (centred output, per-call stage edges): what saved patches get.
      for (double rate : { kSr, 60.0 })
      {
         StartOffline(120.0f);
         FnMod in;
         in.f = GateInput;
         auto env = MakeLegacy<EnvelopeNode>();
         env->input = &in;
         double attackEnd = -1, decayEnd = -1, releaseEnd = -1;
         float rest = -1, peak = 0, sustainOut = 0, releaseMin = 1;
         for (long long k = 0; k <= (long long)(3.0 * rate); k++)
         {
            const double s = k / rate;
            At(s);
            const float out = env->Value01();
            const float gate = GateInput(s);
            const float level = gate > 0.5f ? (out - 0.5f) * 2.0f : (0.5f - out) * 2.0f;
            if (s < 0.5 && rest < 0)
               rest = out;
            peak = std::max(peak, out);
            if (s >= 0.5 && s < 2.0)
            {
               if (attackEnd < 0 && level >= 0.9999f)
                  attackEnd = s;
               if (attackEnd >= 0 && decayEnd < 0 && level <= env->sustainLevel + 1e-4f)
                  decayEnd = s;
               if (s > 1.9)
                  sustainOut = out;
            }
            if (s >= 2.0)
            {
               releaseMin = std::min(releaseMin, out);
               if (releaseEnd < 0 && level <= 1e-4f)
                  releaseEnd = s;
            }
         }
         const double aMs = (attackEnd - 0.5) * 1000.0, dMs = (decayEnd - attackEnd) * 1000.0, rMs = (releaseEnd - 2.0) * 1000.0;
         printf("MODAUDIT ENV   legacy Envelope @%-6g attack %.2f ms (set %.0f) decay %.2f ms (set %.0f) release %.2f ms (set %.0f) | out: rest %.3f peak %.3f sustain %.3f release-min %.3f\n",
                rate, aMs, env->attackMs, dMs, env->decayMs, rMs, env->releaseMs, rest, peak, sustainOut, releaseMin);
         (void)flags; // legacy behaviour is kept on purpose: reported, not flagged
      }

      // Audio to CV follower: DC step 0 -> 1 -> 0, time to 63.2% / 36.8%.
      {
         auto cv = Make<AudioToCVNode>();
         cv->CookIfNeeded(1); // pushes params
         AudioNode* an = cv->AudioNodeForNotePorts();
         an->PrepareToPlay(kSr, 64);
         std::vector<float> l(64), r(64), outL(64), outR(64);
         float* inCh[2] = { l.data(), r.data() };
         float* outCh[2] = { outL.data(), outR.data() };
         AudioBuffer inBuf{ inCh, 2, 64 }, outBuf{ outCh, 2, 64 };
         const AudioBuffer* ins[1] = { &inBuf };
         auto runUntil = [&](float level, std::function<bool(float)> done) {
            std::fill(l.begin(), l.end(), level);
            std::fill(r.begin(), r.end(), level);
            for (int blocks = 0; blocks < 48000; blocks++)
            {
               an->ProcessBlock(ins, 1, outBuf);
               if (done(cv->RawEnvelope()))
                  return (blocks + 1) * 64 * 1000.0 / kSr;
            }
            return -1.0;
         };
         const double att = runUntil(1.0f, [](float e) { return e >= 0.632f; });
         runUntil(1.0f, [](float e) { return e >= 0.9999f; });
         const double rel = runUntil(0.0f, [](float e) { return e <= 0.368f; });
         printf("MODAUDIT ENV   Audio to CV  attack tau %.2f ms (set %.1f), release tau %.2f ms (set %.1f), 64-sample resolution; delivered to params once per UI frame\n",
                att, cv->attackMs, rel, cv->releaseMs);
         if (std::fabs(att - cv->attackMs) > std::max(0.01 * cv->attackMs, 64000.0 / kSr))
            flags.Add("ENV", "Audio to CV", "attack ms", att - cv->attackMs);
         if (std::fabs(rel - cv->releaseMs) > std::max(0.01 * cv->releaseMs, 64000.0 / kSr))
            flags.Add("ENV", "Audio to CV", "release ms", rel - cv->releaseMs);
      }
   }

   // ======================================================================== SEEK
   void AuditSeek(Flags& flags)
   {
      // Pure nodes: a jump lands exactly where a fresh node would be.
      for (const PureSource& src : PureSources())
      {
         StartOffline(120.0f);
         auto a = src.make();
         auto b = src.make();
         double worst = 0.0;
         for (int i = 0; i < 100; i++)
         {
            At(i * 0.37);
            AsMod(a.get())->Value01();
            const double t2 = std::fmod(i * 1.913, 30.0);
            At(t2);
            const float va = AsMod(a.get())->Value01();
            const float vb = AsMod(b.get())->Value01();
            worst = std::max(worst, (double)std::fabs(va - vb));
         }
         printf("MODAUDIT SEEK  %-22s jump vs fresh node %.2e\n", src.name.c_str(), worst);
         if (worst > 0.0)
            flags.Add("SEEK", src.name, "seek err", worst);
      }

      // Envelope: seek back inside the attack while the gate stays open.
      {
         StartOffline(120.0f);
         FnMod in;
         in.f = GateInput;
         for (int legacy = 1; legacy >= 0; legacy--)
         {
            StartOffline(120.0f);
            auto env = legacy ? MakeLegacy<EnvelopeNode>() : Make<EnvelopeNode>();
            env->input = &in;
            for (int k = 0; k <= 727; k++) // 1440 fps to 0.505 s
            {
               At(k / 1440.0);
               env->Value01();
            }
            const float before = env->Value01();
            At(0.501);
            const float after = env->Value01();
            printf("MODAUDIT SEEK  %sEnvelope back 4 ms in attack: out %.3f -> %.3f (follows the clock)\n", legacy ? "legacy " : "", before, after);
            if (legacy && after < 0.5f)
               flags.Add("SEEK", "legacy Envelope", "back-seek out", after);
            if (!legacy)
            {
               // Gate edge at the mid-frame crossing 0.5 - 1/2880 s.
               const double want = (0.501 - (0.5 - 0.5 / 1440.0)) * 1000.0 / env->attackMs;
               if (std::fabs(after - std::min(1.0, want)) > 1e-3)
                  flags.Add("SEEK", "Envelope", "back-seek level", after - want);
            }
         }
      }
      // Smooth time: seek back snaps to the input.
      {
         StartOffline(120.0f);
         FnMod in;
         in.f = StepInput;
         auto sm = Make<SmoothNode>();
         sm->mode = SmoothNode::kTime;
         sm->input = &in;
         for (int k = 0; k <= 66; k++)
         {
            At(1.0 + k / 60.0);
            sm->Value01();
         }
         const float before = sm->Value01();
         At(1.05);
         const float after = sm->Value01();
         printf("MODAUDIT SEEK  Smooth time back-seek: %.3f -> %.3f (snaps to input %.3f)\n", before, after, StepInput(1.05));
      }

      // Loop wrap with a live clock: 1-beat LFO stays continuous; a 3-beat
      // LFO in a 4-beat loop jumps (phase follows the transport, as Bitwig Sync).
      for (float rate : { 1.0f, 3.0f })
      {
         StartLive(120.0f, kSr);
         T().SetLoop(true, 0.0, 4.0);
         auto lfo = Make<LFONode>();
         lfo->rateBeats = rate;
         float prev = lfo->Value01();
         double normal = 0.0, atWrap = 0.0;
         double prevBeats = T().Beats();
         for (int j = 0; j < 2000; j++)
         {
            T().AdvanceAudioClock(512);
            const float v = lfo->Value01();
            const bool wrapped = T().Beats() < prevBeats;
            const double d = std::fabs(v - prev);
            if (wrapped)
               atWrap = std::max(atWrap, d);
            else
               normal = std::max(normal, d);
            prev = v;
            prevBeats = T().Beats();
         }
         printf("MODAUDIT SEEK  LFO %g beat(s), loop 0..4: per-block step %.3g normally, %.3g at the wrap\n", rate, normal, atWrap);
      }

      // Free run: the same 3-beat LFO in the 4-beat loop, phase from its own
      // clock, so the wrap is a normal step.
      {
         StartLive(120.0f, kSr);
         T().SetLoop(true, 0.0, 4.0);
         auto lfo = Make<LFONode>();
         lfo->phaseMode = LFONode::kPhaseFree;
         lfo->rateMode = LFONode::kRateBeats;
         lfo->rateBeats = 3.0f;
         Transport::SetWallClockForTest(&ModelWall);
         gWall = 0.0;
         T().BeginFrameClock();
         float prev = lfo->Value01();
         double normal = 0.0, atWrap = 0.0;
         double prevBeats = T().Beats();
         for (int j = 0; j < 2000; j++)
         {
            gWall = j * 512.0 / kSr;
            T().AdvanceAudioClock(512);
            T().BeginFrameClock();
            const float v = lfo->Value01();
            const bool wrapped = T().Beats() < prevBeats;
            const double d = std::fabs(v - prev);
            (wrapped ? atWrap : normal) = std::max(wrapped ? atWrap : normal, d);
            prev = v;
            prevBeats = T().Beats();
         }
         Transport::SetWallClockForTest(nullptr);
         printf("MODAUDIT SEEK  LFO free 3 beats, loop 0..4: per-block step %.3g normally, %.3g at the wrap\n", normal, atWrap);
         if (atWrap > normal * 1.01 + 1e-6)
            flags.Add("SEEK", "LFO free", "wrap jump", atWrap);
      }
      // Retrigger: the trig input rising through 0.5 restarts the cycle at
      // the crossing (between frames, on the straight line).
      {
         StartOffline(120.0f);
         FnMod trig;
         trig.f = [](double s) { return (s >= 1.0 && s < 1.5) || s >= 2.3 ? 1.0f : 0.0f; };
         auto lfo = Make<LFONode>();
         lfo->phaseMode = LFONode::kPhaseRetrigger;
         lfo->rateMode = LFONode::kRateHz;
         lfo->rateHz = 1.25f;
         lfo->shape = 2;
         lfo->trigger = &trig;
         double worst = 0.0;
         for (int k = 0; k <= 180; k++)
         {
            const double s = k / 60.0;
            At(s);
            const float v = lfo->Value01();
            // Edges at the mid-frame crossings before 1.0 and 2.3 s.
            const double e1 = 1.0 - 0.5 / 60.0, e2 = std::ceil(2.3 * 60.0 - 1e-9) / 60.0 - 0.5 / 60.0;
            if (s >= e1)
            {
               const double since = s >= e2 ? s - e2 : s - e1;
               double d = std::fabs(v - LfoRef(2, 1.25 * since));
               worst = std::max(worst, std::min(d, 1.0 - d));
            }
         }
         printf("MODAUDIT SEEK  LFO retrigger 1.25 Hz: phase vs (t - edge) * rate %.2e\n", worst);
         if (worst > kShapeLimit)
            flags.Add("SEEK", "LFO retrigger", "phase err", worst);
      }

      // Tempo change: LFO phase continuous across it.
      {
         StartLive(120.0f, kSr);
         auto lfo = Make<LFONode>();
         lfo->rateBeats = 1.0f;
         lfo->shape = 2; // saw: value = phase
         double maxNormal = 0.0, atChange = 0.0;
         float prev = lfo->Value01();
         for (int j = 0; j < 2000; j++)
         {
            if (j == 1000)
               T().SetTempo(90.0f);
            T().AdvanceAudioClock(512);
            const float v = lfo->Value01();
            double d = v - prev;
            if (d < -0.5)
               d += 1.0; // saw wrap
            if (j == 1000)
               atChange = d;
            else if (j < 1000)
               maxNormal = std::max(maxNormal, d);
            prev = v;
         }
         const double expected = 512.0 / kSr * 90.0 / 60.0;
         printf("MODAUDIT SEEK  tempo 120->90: phase step %.6f at the change (expected %.6f at the new tempo, %.6f before)\n", atChange, expected, maxNormal);
         if (std::fabs(atChange - expected) > 1.0 / kSr)
            flags.Add("SEEK", "tempo change", "phase jump", atChange - expected);
      }
   }

   // ======================================================================== IDEM
   void AuditIdempotency(Flags& flags)
   {
      for (const StatefulCase& sc : StatefulCases())
      {
         const std::vector<float> one = RunAtFps(sc, 60.0, 1);
         const std::vector<float> eight = RunAtFps(sc, 60.0, 8);
         double worst = 0.0;
         for (size_t i = 0; i < one.size(); i++)
            worst = std::max(worst, (double)std::fabs(one[i] - eight[i]));
         printf("MODAUDIT IDEM  %-27s 1 vs 8 calls per frame: %.2e\n", sc.name.c_str(), worst);
         if (worst > 0.0)
            flags.Add("IDEM", sc.name, "fan-out diff", worst);
      }
   }

   // ======================================================================== SEED
   void AuditSeed(Flags& flags)
   {
      ResetClock(120.0f);
      auto seq = [](IModulator* m, int steps) {
         std::vector<float> v;
         for (int i = 0; i < steps; i++)
         {
            T().SeekBeats(i + 0.5);
            v.push_back(m->Value01());
         }
         return v;
      };
      auto a = Make<RandomNode>(), b = Make<RandomNode>();
      a->seed = b->seed = 3.0f;
      const bool same = seq(a.get(), 1000) == seq(b.get(), 1000);
      auto c = Make<RandomNode>(), d = Make<RandomNode>(); // two spawns: NextSeed
      const bool spawnsDiffer = seq(c.get(), 1000) != seq(d.get(), 1000);
      auto e = Make<RandomNode>(), f = Make<RandomNode>();
      e->seed = 1.0f;
      f->seed = 1.0005f;
      const bool nearSeedsCollide = seq(e.get(), 1000) == seq(f.get(), 1000);
      auto l1 = Make<LFONode>(), l2 = Make<LFONode>();
      l1->shape = l2->shape = 5;
      const bool shLockstep = seq(l1.get(), 1000) == seq(l2.get(), 1000);
      auto l3 = Make<LFONode>(), l4 = Make<LFONode>();
      l3->shape = l4->shape = 5;
      l3->seed = l4->seed = 42.0f;
      const bool shSameSeed = seq(l3.get(), 1000) == seq(l4.get(), 1000);
      // Legacy (saved patches): kept as they were.
      auto le = MakeLegacy<RandomNode>(), lf = MakeLegacy<RandomNode>();
      le->seed = 1.0f;
      lf->seed = 1.0005f;
      auto ll1 = MakeLegacy<LFONode>(), ll2 = MakeLegacy<LFONode>();
      ll1->shape = ll2->shape = 5;
      const bool legacyCollide = seq(le.get(), 1000) == seq(lf.get(), 1000);
      const bool legacyLockstep = seq(ll1.get(), 1000) == seq(ll2.get(), 1000);
      printf("MODAUDIT SEED  Random same seed same output: %s | two spawns differ: %s | seeds 1.0 vs 1.0005 collide: %s | two LFO S&H spawns identical: %s | LFO S&H same seed same output: %s\n",
             same ? "yes" : "NO", spawnsDiffer ? "yes" : "NO", nearSeedsCollide ? "YES" : "no", shLockstep ? "YES" : "no", shSameSeed ? "yes" : "NO");
      printf("MODAUDIT SEED  legacy (maths 0, kept): seeds 1.0 vs 1.0005 collide: %s | LFO S&H lockstep: %s\n",
             legacyCollide ? "yes" : "no", legacyLockstep ? "yes" : "no");
      if (!shSameSeed)
         flags.Add("SEED", "LFO S&H", "same seed", 0);
      if (!same)
         flags.Add("SEED", "Random", "same seed", 0);
      if (!spawnsDiffer)
         flags.Add("SEED", "Random", "spawns equal", 0);
      if (nearSeedsCollide)
         flags.Add("SEED", "Random", "seed collision", 1.0005);
      if (shLockstep)
         flags.Add("SEED", "LFO S&H", "lockstep", 1);

      // Offline render twice: every stateful case reproduces bit for bit.
      int diffs = 0;
      for (const StatefulCase& sc : StatefulCases())
         if (RunAtFps(sc, 60.0) != RunAtFps(sc, 60.0))
            diffs++;
      printf("MODAUDIT SEED  offline run twice, stateful cases differing: %d\n", diffs);
      if (diffs > 0)
         flags.Add("SEED", "offline rerun", "differs", diffs);
   }

   // ======================================================================== CPU
   struct Registered
   {
      const char* name;
      std::function<INode*()> create;
      bool clamped;
   };
   template <class N>
   Registered Reg(const char* name, bool clamped = true)
   {
      return { name, []() { return N::Create(); }, clamped };
   }
   // The same node as a pre-Block-3 patch loads it, so the baseline compares
   // like with like and the new maths' own cost shows as the difference.
   template <class N>
   Registered RegLegacy(const char* name)
   {
      return { name, []() { return (INode*)MakeLegacy<N>().release(); }, true };
   }

   bool AuditCpu(Flags& flags)
   {
      const Registered all[] = {
         Reg<LFONode>("LFO"), Reg<ConstantNode>("Constant"), Reg<RandomNode>("Random"), Reg<PatternNode>("Pattern"),
         Reg<MathNode>("Math"), Reg<CompareNode>("Compare"), Reg<RangeToRangeNode>("Range to Range", false),
         Reg<SmoothNode>("Smooth"), Reg<CVRecorderNode>("CV Recorder"), Reg<ModDepthNode>("Mod Depth"),
         Reg<EnvelopeNode>("Envelope"), Reg<CVToPitchNode>("CV to Pitch"), Reg<NullModulatorNode>("Null"),
         Reg<InvertNode>("Invert", false), Reg<ModCurveNode>("Mod Curve"), Reg<MSEGNode>("MSEG"), Reg<VoiceSpreadNode>("Voice Spread"), Reg<NoteExpressionNode>("Note Expression"), Reg<AudioToCVNode>("Audio to CV"),
         Reg<VibratoNode>("Vibrato"), Reg<NoteToCVNode>("Note to CV"), Reg<VelocityToCVNode>("Velocity to CV"),
         Reg<MacroKnobNode>("Macro Knob"), Reg<MacroXYNode>("Macro XY"), Reg<MacroSliderNode>("Macro Slider"),
         Reg<MacroBipolarKnobNode>("Macro Bipolar"), Reg<MacroToggleNode>("Macro Toggle"), Reg<MacroTriggerNode>("Macro Trigger"),
         Reg<MacroNumBoxNode>("Macro NumBox"), Reg<MacroRadioSelectorNode>("Macro Radio"), Reg<MacroStepGateNode>("Macro Step Gate"),
         Reg<MidiCCNode>("MIDI CC"), Reg<MidiTriggerNode>("MIDI Trigger"), Reg<OscReceiveNode>("OSC Receive"),
         Reg<PathNode>("Path"), Reg<GeometryTableNode>("Geometry Table"), Reg<DriftNode>("Drift"),
         Reg<MovesNode>("Moves"), Reg<PredictiveModulatorNode>("Predictive"),
         RegLegacy<LFONode>("LFO legacy"), RegLegacy<RandomNode>("Random legacy"), RegLegacy<SmoothNode>("Smooth legacy"),
         RegLegacy<EnvelopeNode>("Envelope legacy"), RegLegacy<CVToPitchNode>("CV Pitch legacy"),
      };
      bool ok = true;
      FnMod lfoIn;
      lfoIn.f = SineInput;
      for (const Registered& r : all)
      {
         std::unique_ptr<INode> node(r.create());
         IModulator* m = AsMod(node.get());
         if (m == nullptr)
         {
            printf("MODAUDIT CPU   %-16s not an IModulator?\n", r.name);
            continue;
         }
         // Patch an LFO-like input into slot 0 where there is one, so
         // transforms do real work.
         if (node->ModulatorInputCount() > 0)
            if (IModulator** slot = node->ModulatorInputSlot(0))
               *slot = &lfoIn;
         StartOffline(120.0f);
         float lo = 1e9f, hi = -1e9f;
         bool finite = true;
         const int calls = 200000;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < calls; i++)
         {
            At(i * (4.0 / calls)); // 4 s, a new instant every call
            const float v = m->Value01();
            if (!std::isfinite(v))
               finite = false;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
         }
         const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / calls;
         // Subtract the clock store.
         printf("MODAUDIT CPU   %-16s %7.1f ns/call | range %.3f..%.3f%s\n", r.name, ns, lo, hi, finite ? "" : " NON-FINITE");
         if (!finite || (r.clamped && (lo < 0.0f || hi > 1.0f)))
         {
            printf("MODAUDIT FAIL  %s: %s\n", r.name, finite ? "left 0..1" : "non-finite output");
            ok = false;
         }
      }
      {
         StartOffline(120.0f);
         const int calls = 200000;
         volatile double sink = 0.0;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < calls; i++)
         {
            At(i * (4.0 / calls));
            sink = sink + T().Beats();
         }
         const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / calls;
         printf("MODAUDIT CPU   %-16s %7.1f ns/call (clock set + Beats(), included in every row above)\n", "(harness)", ns);
      }
      {
         // Beats() while the main thread holds a frame latch (live, inside
         // ApplyModulationAndPalette): the path every live modulator read takes.
         StartLive(120.0f, kSr);
         T().AdvanceAudioClock(512);
         T().BeginFrameClock();
         const int calls = 200000;
         volatile double sink = 0.0;
         const auto t0 = std::chrono::steady_clock::now();
         for (int i = 0; i < calls; i++)
            sink = sink + T().Beats();
         const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / calls;
         printf("MODAUDIT CPU   %-16s %7.1f ns/call (Beats() under a live frame latch)\n", "(latched)", ns);
         ResetClock(120.0f);
      }
      (void)flags;
      return ok;
   }
}

bool RunModAudit()
{
   Flags flags;
   const char* only = getenv("INFINITE_MODAUDIT_ONLY");
   auto want = [only](const char* s) { return only == nullptr || *only == 0 || std::strstr(only, s) != nullptr; };
   bool ok = true;
   if (want("CLOCK")) AuditClock(flags);
   if (want("SHAPE")) AuditShapes(flags);
   if (want("QUANT")) AuditQuant(flags);
   if (want("ZIPPER")) AuditZipper(flags);
   if (want("VIDEO")) AuditVideo(flags);
   if (want("DSMOOTH")) AuditDestSmooth(flags);
   if (want("FRATE")) AuditFrameRate(flags);
   if (want("ENV")) AuditEnvelope(flags);
   if (want("SEEK")) AuditSeek(flags);
   if (want("IDEM")) AuditIdempotency(flags);
   if (want("SEED")) AuditSeed(flags);
   if (want("CPU")) ok = AuditCpu(flags) && ok;
   ResetClock(120.0f);
   printf("MODAUDIT %d flags\n", flags.count);
   printf("MODAUDIT: %s\n", ok ? "PASS" : "FAIL");
   fflush(stdout);
   return ok;
}
} // namespace app

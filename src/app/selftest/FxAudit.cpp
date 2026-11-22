// INFINITE_FXAUDIT: a measurement audit of every audio effect (EffectDef).
//
// Each effect runs through the real AudioEffectNode -> AudioEffectRuntime ->
// kernel chain, wet only (mix 1), in 256-frame blocks, and gets the same
// battery of numbers, so "does it sound professional" is a table rather than
// an opinion:
//
//   gain     wet level re input for white noise and a 1 kHz tone at -20 dBFS
//   stereo   mono noise in: L/R correlation and balance out
//   dc       output mean while fed noise
//   tail     level of the last 0.5 s of 10 s of silence after the noise
//   cpu      ns per stereo frame at defaults
//   stress   peak and non-finite samples with a 0 dBFS sweep + noise at
//            all-max, all-min and seeded random settings
//   click    worst param step (30% -> 70% of range, instant) on a 440 Hz
//            tone: second-difference peak in the 50 ms after the step re
//            the steady state either side and the same 50 ms rendered
//            with the param held at either end, dB
//   rate     44.1 / 48 / 96 kHz at defaults: level spread (dB) of a 100 Hz /
//            1 kHz / 6 kHz tone (octave bands of noise for a decorrelating
//            wet, whose modes move with the rate) and
//            -30 dB release-time ratio after a noise burst
//
// plus per-effect truths (reverb RT60 vs decay, delay time, tremolo rate,
// filter cutoff across rates, resonator bank pitch).
//
// Only hard invariants fail the run (non-finite output, a tail that never
// ends). A big stress peak is reported, not failed: EQ/filter/shaper gain
// params legitimately reach +24 dB or more. Everything else is reported, with FLAG lines past
// the thresholds below, for a person to triage.
#include "app/AppShared.h"
#include "audio/dsp/EqKernel.h"
#include "audio/dsp/PortableFft.h"

#include <chrono>
#include <map>
#include <thread>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace app
{
namespace
{
   constexpr int kBlock = 256;

   // Thresholds for FLAG lines (reported, not failed).
   constexpr double kFlagGainDb = 6.0;     // |wet gain| for noise or tone
   constexpr double kFlagTailDbfs = -100.0; // tail after 10 s of silence
   constexpr double kFlagDcDbfs = -50.0;
   constexpr double kFlagClickDb = 12.0;
   constexpr double kFlagRateDb = 1.0;
   constexpr double kFlagRateTime = 1.15;
   constexpr double kRelResolveS = 0.05; // ReleaseTime's window: smaller release differences are not resolved
   constexpr double kFlagCpuNs = 4000.0;

   struct Rig
   {
      std::unique_ptr<AudioEffectNode> node;
      double sr = 48000.0;
      int frame = 1;

      void Cook() { node->CookIfNeeded(++frame); }
      void Set(const std::string& name, float v)
      {
         if (node->ParamIndex(name.c_str()) >= 0)
            *node->ParamPtr(name.c_str()) = v;
      }
   };

   std::unique_ptr<Rig> MakeRig(const EffectDef& def, double sr, const std::map<std::string, float>& over = {})
   {
      auto rig = std::make_unique<Rig>();
      rig->sr = sr;
      rig->node = std::make_unique<AudioEffectNode>(def);
      rig->node->mix = 1.0f;
      for (const auto& kv : over)
         rig->Set(kv.first, kv.second);
      rig->node->GetAudioNode()->PrepareToPlay(sr, kBlock);
      rig->Cook();
      for (int t = 0; t < 400 && rig->node->KernelPreparing(); t++)
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         rig->Cook();
      }
      rig->Cook();
      return rig;
   }

   // Runs in -> out in kBlock blocks, cooking once per block (as the app does
   // once per frame), stereo.
   void Process(Rig& rig, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL,
                std::vector<float>& outR)
   {
      const int n = (int)inL.size();
      outL.assign((size_t)n, 0.0f);
      outR.assign((size_t)n, 0.0f);
      float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
      float* iCh[2] = { il, ir };
      float* oCh[2] = { ol, orr };
      AudioNode* an = rig.node->GetAudioNode();
      for (int s = 0; s < n; s += kBlock)
      {
         const int len = std::min(kBlock, n - s);
         for (int i = 0; i < len; i++)
         {
            il[i] = inL[(size_t)(s + i)];
            ir[i] = inR[(size_t)(s + i)];
         }
         AudioBuffer in { iCh, 2, len };
         AudioBuffer out { oCh, 2, len };
         const AudioBuffer* ins[1] = { &in };
         rig.Cook();
         an->ProcessBlock(ins, 1, out);
         for (int i = 0; i < len; i++)
         {
            outL[(size_t)(s + i)] = ol[i];
            outR[(size_t)(s + i)] = orr[i];
         }
      }
   }

   struct Lcg
   {
      uint32_t s;
      explicit Lcg(uint32_t seed) : s(seed) {}
      float Next() // -1..1
      {
         s = s * 1664525u + 1013904223u;
         return (float)((double)(s >> 8) / 8388608.0 - 1.0);
      }
   };

   double Rms(const std::vector<float>& x, size_t a, size_t b)
   {
      double e = 0.0;
      b = std::min(b, x.size());
      for (size_t i = a; i < b; i++)
         e += (double)x[i] * x[i];
      return b > a ? std::sqrt(e / (double)(b - a)) : 0.0;
   }
   double Db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

   std::vector<float> Noise(int n, float rms, uint32_t seed)
   {
      Lcg r(seed);
      std::vector<float> v((size_t)n);
      const float a = rms * std::sqrt(3.0f);
      for (auto& x : v)
         x = a * r.Next();
      return v;
   }
   // Pink noise (Kellet's economy filter, -3 dB/octave), scaled to `rms`:
   // closer to music than white noise for a loudness reading.
   std::vector<float> PinkNoise(int n, float rms, uint32_t seed)
   {
      std::vector<float> v = Noise(n, 1.0f, seed);
      float b0 = 0, b1 = 0, b2 = 0;
      double sum = 0;
      for (auto& x : v)
      {
         b0 = 0.99765f * b0 + x * 0.0990460f;
         b1 = 0.96300f * b1 + x * 0.2965164f;
         b2 = 0.57000f * b2 + x * 1.0526913f;
         x = b0 + b1 + b2 + x * 0.1848f;
         sum += (double)x * x;
      }
      const float k = rms / (float)std::sqrt(sum / std::max(1, n));
      for (auto& x : v)
         x *= k;
      return v;
   }
   // Periodic noise with an exactly flat spectrum: unit magnitude and a
   // random phase in every bin of one power-of-two period (2.7-3.0 s at
   // 44.1/48/96 kHz), repeated `periods` times. Through a linear system the
   // band power of the last period is then the band average of |H|^2 with no
   // realisation variance: plain white noise through a resonator with
   // ~1 Hz-wide modes left a few degrees of freedom per seed, and the
   // Resonator Bank's 100 Hz band read 2.4 dB apart across rates by luck
   // (node-audits F15). *periodOut receives the period in samples.
   std::vector<float> FlatNoise(double sr, int periods, float rms, uint32_t seed, int* periodOut)
   {
      int log2N = 2;
      while ((1 << (log2N + 1)) <= (int)(3.0 * sr))
         log2N++;
      const int n = 1 << log2N;
      PortableFft::RealFft fft;
      fft.Prepare(log2N);
      std::vector<float> re((size_t)n / 2, 0.0f), im((size_t)n / 2, 0.0f), one((size_t)n);
      Lcg r(seed);
      for (int k = 1; k < n / 2; k++)
      {
         const double ph = M_PI * (double)r.Next(); // Next() is uniform in [-1, 1)
         re[(size_t)k] = (float)std::cos(ph);
         im[(size_t)k] = (float)std::sin(ph);
      }
      fft.Inverse(re.data(), im.data(), log2N, one.data());
      double e = 0.0;
      for (float x : one)
         e += (double)x * x;
      const float g = (float)(rms / std::sqrt(std::max(1e-30, e / (double)n)));
      std::vector<float> v((size_t)n * (size_t)periods);
      for (size_t i = 0; i < v.size(); i++)
         v[i] = one[i % (size_t)n] * g;
      *periodOut = n;
      return v;
   }
   std::vector<float> Tone(int n, double hz, double sr, float rms)
   {
      std::vector<float> v((size_t)n);
      for (int i = 0; i < n; i++)
         v[(size_t)i] = (float)(rms * std::sqrt(2.0) * std::sin(2.0 * M_PI * hz * i / sr));
      return v;
   }

   bool IsDiscrete(const std::string& n)
   {
      static const char* k[] = { "type", "sync", "rateDiv", "taps", "order", "shape", "structure", "waveform", "table",
                                 "freeze", "dither", "bounce", "steps", "gateMask", "modes", "scale", "root", "mode",
                                 "sidechainHpf", "spectral" };
      for (const char* s : k)
         if (n == s)
            return true;
      return false;
   }

   // Second-difference peak in [a, a+len).
   double D2Peak(const std::vector<float>& y, size_t a, size_t len)
   {
      double m = 0.0;
      for (size_t i = std::max<size_t>(a, 2); i < std::min(a + len, y.size()); i++)
         m = std::max(m, (double)std::fabs(y[i] - 2.0f * y[i - 1] + y[i - 2]));
      return m;
   }

   // Time (s) after `from` for the 50 ms-RMS envelope of L+R to fall `dropDb`
   // below `ref`; -1 when it never does.
   double ReleaseTime(const std::vector<float>& l, const std::vector<float>& r, size_t from, double ref, double dropDb,
                      double sr)
   {
      const size_t w = (size_t)(0.05 * sr);
      const double target = ref * std::pow(10.0, -dropDb / 20.0);
      for (size_t a = from; a + w <= l.size(); a += w / 4)
      {
         double e = 0.0;
         for (size_t i = a; i < a + w; i++)
            e += 0.5 * ((double)l[i] * l[i] + (double)r[i] * r[i]);
         if (std::sqrt(e / (double)w) < target)
            return (double)(a - from) / sr;
      }
      return -1.0;
   }

   // Schroeder backward integral RT60 from the -5..-35 dB span.
   double Rt60(const std::vector<float>& l, const std::vector<float>& r, double sr)
   {
      const size_t n = l.size();
      std::vector<double> e(n + 1, 0.0);
      for (size_t i = n; i-- > 0;)
         e[i] = e[i + 1] + (double)l[i] * l[i] + (double)r[i] * r[i];
      if (e[0] <= 0.0)
         return -1.0;
      double t5 = -1.0, t35 = -1.0;
      for (size_t i = 0; i < n; i++)
      {
         const double d = 10.0 * std::log10(std::max(e[i] / e[0], 1e-30));
         if (t5 < 0.0 && d <= -5.0)
            t5 = (double)i / sr;
         if (d <= -35.0)
         {
            t35 = (double)i / sr;
            break;
         }
      }
      return (t5 >= 0.0 && t35 > t5) ? 2.0 * (t35 - t5) : -1.0;
   }

   // One-octave RBJ band-pass (Q 1.41) of `x` around `hz`: an ISO 3382-style
   // octave band, for measurements that must not be dominated by whichever
   // band decays or resonates fastest.
   std::vector<float> Band(const std::vector<float>& x, double hz, double sr)
   {
      DspMath::Biquad bq;
      bq.SetBandpass(hz, 1.41, sr);
      std::vector<float> y(x.size());
      for (size_t i = 0; i < x.size(); i++)
         y[i] = bq.Process(x[i]);
      return y;
   }

   double MagAt(const std::vector<float>& h, double hz, double sr)
   {
      double re = 0.0, im = 0.0;
      for (size_t i = 0; i < h.size(); i++)
      {
         const double ph = 2.0 * M_PI * hz * (double)i / sr;
         re += h[i] * std::cos(ph);
         im -= h[i] * std::sin(ph);
      }
      return std::sqrt(re * re + im * im);
   }

   // One measured value for the quality system (docs/quality/README.md): tools/quality/quality.py keeps
   // the worst per metric and subject and owns the target and the release gate.
   void Quality(const char* id, const std::string& node, double v)
   {
      printf("QUALITY id=%s subject=\"node:%s\" value=%.4f\n", id, node.c_str(), v);
   }

   struct Flagger
   {
      int count = 0;
      void operator()(const std::string& fx, const char* what, const char* fmt, double v)
      {
         char buf[160];
         std::snprintf(buf, sizeof(buf), fmt, v);
         printf("FXAUDIT FLAG %-18s %-8s %s\n", fx.c_str(), what, buf);
         count++;
      }
   };

   // ---------------------------------------------------------------- per-effect
   bool AuditOne(const EffectDef& def, Flagger& flag)
   {
      bool ok = true;
      const std::string& fx = def.name;
      const double sr = 48000.0;
      const int n3 = (int)(3.0 * sr);

      // gain, stereo, dc, cpu, tail: one run of noise then 10 s of silence.
      double gNoise, gTone, corr, bal, dcDb, tailDb, cpuNs;
      {
         auto rig = MakeRig(def, sr);
         const int nSil = (int)(10.0 * sr);
         std::vector<float> in = Noise(n3, 0.1f, 1), z((size_t)nSil, 0.0f), oL, oR, tL, tR;
         const auto t0 = std::chrono::steady_clock::now();
         Process(*rig, in, in, oL, oR);
         const auto t1 = std::chrono::steady_clock::now();
         Process(*rig, z, z, tL, tR);
         cpuNs = std::chrono::duration<double, std::nano>(t1 - t0).count() / n3;
         const size_t a = (size_t)sr;
         const double outRms = std::sqrt(0.5 * (Rms(oL, a, oL.size()) * Rms(oL, a, oL.size()) +
                                                Rms(oR, a, oR.size()) * Rms(oR, a, oR.size())));
         gNoise = Db(outRms / 0.1);
         double sl = 0, sr2 = 0, slr = 0, mean = 0;
         for (size_t i = a; i < oL.size(); i++)
         {
            sl += (double)oL[i] * oL[i];
            sr2 += (double)oR[i] * oR[i];
            slr += (double)oL[i] * oR[i];
            mean += 0.5 * (oL[i] + oR[i]);
         }
         corr = (sl > 0 && sr2 > 0) ? slr / std::sqrt(sl * sr2) : 1.0;
         bal = 10.0 * std::log10(std::max(sl, 1e-30) / std::max(sr2, 1e-30));
         dcDb = Db(std::fabs(mean / (double)(oL.size() - a)));
         const size_t ta = tL.size() - (size_t)(0.5 * sr);
         tailDb = Db(std::max(Rms(tL, ta, tL.size()), Rms(tR, ta, tR.size())));
      }
      {
         auto rig = MakeRig(def, sr);
         std::vector<float> in = Tone(n3, 1000.0, sr, 0.1f), oL, oR;
         Process(*rig, in, in, oL, oR);
         gTone = Db(Rms(oL, (size_t)sr, oL.size()) / 0.1);
      }

      // stress
      float worstPeak = 0.0f;
      std::string worstWhere;
      long nonFinite = 0;
      {
         std::vector<float> in((size_t)n3);
         Lcg r(7);
         for (int i = 0; i < n3; i++)
         {
            const double t = (double)i / sr;
            const double f = 50.0 * std::pow(200.0, t / 3.0);
            in[(size_t)i] = (float)(0.7 * std::sin(2.0 * M_PI * 50.0 * 3.0 / std::log(200.0) * (std::pow(200.0, t / 3.0) - 1.0)) +
                                    0.3 * r.Next());
            (void)f;
         }
         for (int c = 0; c < 8; c++)
         {
            std::map<std::string, float> over;
            Lcg pr(100 + c);
            for (const auto& p : def.params)
            {
               float v = c == 0 ? p.maxVal : c == 1 ? p.minVal : p.minVal + (p.maxVal - p.minVal) * 0.5f * (pr.Next() + 1.0f);
               if (IsDiscrete(p.name))
                  v = std::round(v);
               over[p.name] = v;
            }
            auto rig = MakeRig(def, sr, over);
            std::vector<float> oL, oR;
            Process(*rig, in, in, oL, oR);
            for (size_t i = 0; i < oL.size(); i++)
               for (float v : { oL[i], oR[i] })
               {
                  if (!std::isfinite(v))
                     nonFinite++;
                  else if (std::fabs(v) > worstPeak)
                  {
                     worstPeak = std::fabs(v);
                     worstWhere = c == 0 ? "all-max" : c == 1 ? "all-min" : "random" + std::to_string(c);
                  }
               }
         }
      }

      // click per continuous param
      double worstClick = -99.0;
      std::string worstClickParam = "-";
      for (const auto& p : def.params)
      {
         if (IsDiscrete(p.name) || p.uiOnly)
            continue;
         const float lo = p.minVal + 0.3f * (p.maxVal - p.minVal), hi = p.minVal + 0.7f * (p.maxVal - p.minVal);
         auto rig = MakeRig(def, sr, { { p.name, lo } });
         const int nPre = (int)sr, nPost = (int)(0.6 * sr);
         std::vector<float> a = Tone(nPre, 440.0, sr, 0.177f), b((size_t)nPost), oL1, oR1, oL2, oR2;
         for (int i = 0; i < nPost; i++)
            b[(size_t)i] = (float)(0.25 * std::sin(2.0 * M_PI * 440.0 * (nPre + i) / sr));
         Process(*rig, a, a, oL1, oR1);
         rig->Set(p.name, hi);
         Process(*rig, b, b, oL2, oR2);
         const size_t w10 = (size_t)(0.01 * sr);
         double pre = 0.0;
         for (size_t s = oL1.size() - (size_t)(0.3 * sr); s + w10 <= oL1.size(); s += w10)
            pre = std::max(pre, D2Peak(oL1, s, w10));
         double post = 0.0;
         for (size_t s = (size_t)(0.25 * sr); s + w10 <= oL2.size(); s += w10)
            post = std::max(post, D2Peak(oL2, s, w10));
         const double step = D2Peak(oL2, 0, (size_t)(0.05 * sr));
         // The same 50 ms with no step, the param held at lo and at hi from
         // the start: a param that moves an internal LFO's depth (Audio
         // Filter envAmount) or a time-varying wet legitimately looks
         // different at the step than 0.3 s either side. A click is what
         // exceeds both steady settings at the same moment (F13).
         double steady = 0.0;
         for (const float v : { lo, hi })
         {
            auto ref = MakeRig(def, sr, { { p.name, v } });
            std::vector<float> rL1, rR1, rL2, rR2;
            Process(*ref, a, a, rL1, rR1);
            Process(*ref, b, b, rL2, rR2);
            steady = std::max(steady, D2Peak(rL2, 0, (size_t)(0.05 * sr)));
         }
         const double c = Db(step / std::max({ pre, post, steady, 1e-5 }));
         if (getenv("INFINITE_FXAUDIT_VERBOSE"))
            printf("FXAUDIT CLICK %-18s %-10s step %.2e pre %.2e post %.2e steady %.2e -> %+.1f dB\n", fx.c_str(),
                   p.name.c_str(), step, pre, post, steady, c);
         if (c > worstClick)
         {
            worstClick = c;
            worstClickParam = p.name;
         }
      }

      // rate consistency at defaults
      double toneSpread = 0.0, relRatio = 1.0, relSpreadS = 0.0;
      double relAt[3] = {};
      {
         const double rates[3] = { 44100.0, 48000.0, 96000.0 };
         double relSum[3] = {};
         int relCnt[3] = {};
         auto addRelease = [&](int ri, unsigned seedFrom, unsigned seedTo)
         {
            // Release after a 1 s noise burst, averaged over three seeds and band-limited to
            // 16 kHz. One seed read a few-mode resonator's release 0.51-0.64 s across rates: where
            // a random burst stops decides how loud the modes are ringing, a few dB either way.
            // And full-band white noise at 96 kHz puts half its power above 24 kHz, in a reverb's
            // fastest-dying band, so its broadband release read shorter though every audible band
            // decays at the same rate. Narrow, long-ringing modes still read a few percent apart per
            // seed, so a ratio past the flag is confirmed with nine more seeds before it counts.
            const double rr = rates[ri];
            const int nb = (int)(1.0 * rr), ns = (int)(8.0 * rr);
            for (unsigned seed = seedFrom; seed <= seedTo; seed++)
            {
               auto rig = MakeRig(def, rr);
               std::vector<float> in = Noise(nb, 0.1f, seed), z((size_t)ns, 0.0f), oL, oR, tL, tR;
               DspMath::Biquad lp1, lp2;
               lp1.SetLowpass(16000.0, 0.5412, rr);
               lp2.SetLowpass(16000.0, 1.3066, rr);
               for (float& v : in)
                  v = lp2.Process(lp1.Process(v));
               Process(*rig, in, in, oL, oR);
               Process(*rig, z, z, tL, tR);
               const double ref = std::sqrt(0.5 * (std::pow(Rms(oL, (size_t)(0.5 * rr), oL.size()), 2) +
                                                   std::pow(Rms(oR, (size_t)(0.5 * rr), oR.size()), 2)));
               const double rel = ref > 1e-6 ? ReleaseTime(tL, tR, 0, ref, 30.0, rr) : -1.0;
               if (getenv("INFINITE_FXAUDIT_VERBOSE"))
                  printf("FXAUDIT REL %-18s %.1f kHz seed %u: ref %.4f burst-end %.4f release %.3f s\n", fx.c_str(),
                         rr / 1000.0, seed, ref,
                         std::sqrt(0.5 * (std::pow(Rms(oL, oL.size() - (size_t)(0.05 * rr), oL.size()), 2) +
                                          std::pow(Rms(oR, oR.size() - (size_t)(0.05 * rr), oR.size()), 2))),
                         rel);
               if (rel >= 0.0)
               {
                  relSum[ri] += rel;
                  relCnt[ri]++;
               }
            }
         };
         double lv[3][3] = {};
         for (int ri = 0; ri < 3; ri++)
         {
            const double rr = rates[ri];
            const double tones[3] = { 100.0, 1000.0, 6000.0 };
            if (def.linearMix)
               for (int ti = 0; ti < 3; ti++)
               {
                  auto rig = MakeRig(def, rr);
                  const int n = (int)(3.0 * rr);
                  std::vector<float> in = Tone(n, tones[ti], rr, 0.1f), oL, oR;
                  Process(*rig, in, in, oL, oR);
                  lv[ri][ti] = Db(std::sqrt(0.5 * (std::pow(Rms(oL, (size_t)rr, oL.size()), 2) + std::pow(Rms(oR, (size_t)rr, oR.size()), 2))));
               }
            else
            {
               // A decorrelating wet (reverb, delay, modulation, spectral):
               // one sine lands on or between its modes, which moves with
               // the rate, so it is noise in octave bands re the input's.
               // Power averaged over three noise seeds: a wet that repeats
               // a short slice (Stutter) leaves only a couple of harmonics
               // in the 100 Hz band, and one seed's luck read 1.9 dB apart.
               // The noise is flat-spectrum and periodic (FlatNoise), and
               // only the second period is measured, once the wet's own
               // transient has rung out.
               double pin[3] = {}, pout[3] = {};
               for (unsigned seed : { 11u, 12u, 13u })
               {
                  auto rig = MakeRig(def, rr);
                  int period = 0;
                  std::vector<float> in = FlatNoise(rr, 2, 0.1f, seed, &period), oL, oR;
                  Process(*rig, in, in, oL, oR);
                  for (int ti = 0; ti < 3; ti++)
                  {
                     const std::vector<float> bi = Band(in, tones[ti], rr), bl = Band(oL, tones[ti], rr),
                                              br = Band(oR, tones[ti], rr);
                     pin[ti] += std::pow(Rms(bi, (size_t)period, bi.size()), 2);
                     pout[ti] += 0.5 * (std::pow(Rms(bl, (size_t)period, bl.size()), 2) +
                                        std::pow(Rms(br, (size_t)period, br.size()), 2));
                  }
               }
               for (int ti = 0; ti < 3; ti++)
                  lv[ri][ti] = Db(std::sqrt(pout[ti]) / std::max(1e-12, std::sqrt(pin[ti])) * 0.1);
            }
            addRelease(ri, 3u, 5u);
            relAt[ri] = relCnt[ri] > 0 ? relSum[ri] / relCnt[ri] : -1.0;
         }
         if (getenv("INFINITE_FXAUDIT_VERBOSE"))
            for (int ri = 0; ri < 3; ri++)
               printf("FXAUDIT RATE %-18s %.1f kHz: 100 Hz %+.2f, 1 kHz %+.2f, 6 kHz %+.2f dBFS\n", fx.c_str(),
                      rates[ri] / 1000.0, lv[ri][0], lv[ri][1], lv[ri][2]);
         for (int ti = 0; ti < 3; ti++)
         {
            const double mx = std::max({ lv[0][ti], lv[1][ti], lv[2][ti] });
            const double mn = std::min({ lv[0][ti], lv[1][ti], lv[2][ti] });
            if (mx > -80.0)
               toneSpread = std::max(toneSpread, mx - mn);
         }
         const auto ratioOf = [&]() {
            const double mx = std::max({ relAt[0], relAt[1], relAt[2] }), mn = std::min({ relAt[0], relAt[1], relAt[2] });
            if (mn > 0.03)
               return mx / mn;
            if (mx > 0.1)
               return 99.0; // one rate rings, another does not
            return 1.0;
         };
         // A release shorter than the 50 ms analysis window (shapers, filters) reads 0.04-0.06 s
         // from one 12.5 ms step to the next: a ratio of two such numbers is not a rate drift.
         const auto spreadOf = [&]() {
            return std::max({ relAt[0], relAt[1], relAt[2] }) - std::min({ relAt[0], relAt[1], relAt[2] });
         };
         relRatio = ratioOf();
         if (relRatio > kFlagRateTime && relRatio < 99.0 && spreadOf() >= kRelResolveS)
         {
            for (int ri = 0; ri < 3; ri++)
            {
               addRelease(ri, 6u, 14u);
               relAt[ri] = relCnt[ri] > 0 ? relSum[ri] / relCnt[ri] : -1.0;
            }
            relRatio = ratioOf();
         }
         relSpreadS = spreadOf();
      }

      printf("FXAUDIT %-18s gain noise %+6.1f tone %+6.1f | corr %5.2f bal %+5.1f | dc %6.0f | tail %5.0f | cpu %6.0f ns"
             " | stress peak %6.2f (%s) nan %ld | click %+5.1f dB (%s) | rate tone %.1f dB release x%.2f (%.2f/%.2f/%.2f s)\n",
             fx.c_str(), gNoise, gTone, corr, bal, dcDb, tailDb, cpuNs, worstPeak, worstWhere.c_str(), nonFinite, worstClick,
             worstClickParam.c_str(), toneSpread, relRatio, relAt[0], relAt[1], relAt[2]);

      if (std::fabs(gNoise) > kFlagGainDb)
         flag(fx, "gain", "noise %+.1f dB at defaults", gNoise);
      if (std::fabs(gTone) > kFlagGainDb)
         flag(fx, "gain", "1 kHz tone %+.1f dB at defaults", gTone);
      if (std::fabs(bal) > 1.0)
         flag(fx, "stereo", "mono in -> L/R balance %+.1f dB", bal);
      // Insert gain: what a user hears when the node opens - default mix,
      // pink noise at -20 dBFS RMS, level change after 1 s (v0.6 1.9). A
      // steady sine is the wrong probe here: it adds coherently with a delay
      // or reverb's own echoes (Delay read +3 dB on a 1 kHz tone, +0.1 on this).
      double gInsert;
      {
         auto rig = MakeRig(def, sr, {});
         rig->node->mix = def.defaultMix;
         std::vector<float> in = PinkNoise(n3, 0.1f, 5), oL, oR;
         Process(*rig, in, in, oL, oR);
         const size_t a = (size_t)sr;
         const double inRms = Rms(in, a, in.size());
         const double outRms = std::sqrt(0.5 * (std::pow(Rms(oL, a, oL.size()), 2) + std::pow(Rms(oR, a, oR.size()), 2)));
         gInsert = Db(outRms / std::max(1e-12, inRms));
      }
      printf("FXAUDIT %-18s insert gain %+.2f dB (mix %.2f, pink noise)\n", fx.c_str(), gInsert, def.defaultMix);
      Quality("fx.insert_gain_db", fx, std::fabs(gInsert));
      // Level null for the modulated delays (v0.6 1.10): a linear effect
      // gives the same output at -1 dBFS as at -12 dBFS scaled up, so the
      // residual is the distortion a hot input adds. THD is the wrong tool
      // here (the modulation's own sidebands are not harmonics), and so is a
      // file bounce: dry + wet passes 0 dBFS on an in-phase sine and an
      // integer WAV clamps it (builtins-kit read that clamp as -24.5 dB THD).
      if (fx == "Chorus" || fx == "Flanger")
      {
         const float hot = 0.891f * 0.7071f, ref = 0.251f * 0.7071f; // -1, -12 dBFS peak
         std::vector<float> oH, oHr, oR1, oR1r;
         {
            auto rig = MakeRig(def, sr, {});
            rig->node->mix = def.defaultMix;
            std::vector<float> in = Tone(n3, 1000.0, sr, hot);
            Process(*rig, in, in, oH, oHr);
         }
         {
            auto rig = MakeRig(def, sr, {});
            rig->node->mix = def.defaultMix;
            std::vector<float> in = Tone(n3, 1000.0, sr, ref);
            Process(*rig, in, in, oR1, oR1r);
         }
         const float k = hot / ref;
         float peak = 0.0f;
         std::vector<float> diff(oH.size());
         for (size_t i = 0; i < oH.size(); i++)
         {
            diff[i] = oH[i] - k * oR1[i];
            peak = std::max(peak, std::fabs(oH[i]));
         }
         const size_t a = (size_t)sr;
         const double nullDb = Db(Rms(diff, a, diff.size()) / std::max(1e-12, Rms(oH, a, oH.size())));
         printf("FXAUDIT %-18s hot null %.1f dB (-1 vs -12 dBFS, 1 kHz), output peak %.3f\n", fx.c_str(), nullDb, peak);
         Quality("fx.hot_null_db", fx, nullDb);
      }
      Quality("fx.dc_dbfs", fx, dcDb);
      Quality("fx.tail_dbfs", fx, tailDb);
      Quality("fx.click_db", fx, worstClick);
      Quality("fx.rate_spread_db", fx, toneSpread);
      if (dcDb > kFlagDcDbfs)
         flag(fx, "dc", "%.0f dBFS mean under noise", dcDb);
      if (tailDb > kFlagTailDbfs)
         flag(fx, "tail", "%.0f dBFS after 10 s of silence", tailDb);
      if (cpuNs > kFlagCpuNs)
         flag(fx, "cpu", "%.0f ns per stereo frame", cpuNs);
      if (worstClick > kFlagClickDb)
      {
         char what[64];
         std::snprintf(what, sizeof(what), "click:%s", worstClickParam.c_str());
         flag(fx, what, "%+.1f dB step transient", worstClick);
      }
      if (toneSpread > kFlagRateDb)
         flag(fx, "rate", "tone level differs %.1f dB across 44.1/48/96 kHz", toneSpread);
      if (relRatio > kFlagRateTime && (relRatio >= 99.0 || relSpreadS >= kRelResolveS))
         flag(fx, "rate", "release time differs x%.2f across 44.1/48/96 kHz", relRatio);
      if (worstPeak > 4.0f)
         flag(fx, "stress", "peak %.1f under 0 dBFS input", worstPeak);

      if (nonFinite > 0 || tailDb > -40.0)
      {
         printf("FXAUDIT %-18s FAIL (non-finite %ld, peak %.1f, tail %.0f dBFS)\n", fx.c_str(), nonFinite, worstPeak, tailDb);
         ok = false;
      }
      return ok;
   }

   // ------------------------------------------------------------ specific truths
   void Specific(const EffectDef& def, Flagger& flag)
   {
      const std::string& fx = def.name;
      const double rates[3] = { 44100.0, 48000.0, 96000.0 };
      // Half a second of silence first, so smoothed params have reached their targets.
      auto impulse = [](Rig& rig, double secs, std::vector<float>& l, std::vector<float>& r) {
         std::vector<float> pre((size_t)(0.5 * rig.sr), 0.0f), pl, pr;
         Process(rig, pre, pre, pl, pr);
         const int n = (int)(secs * rig.sr);
         std::vector<float> in((size_t)n, 0.0f);
         in[0] = 1.0f;
         Process(rig, in, in, l, r);
      };

      if (fx == "Reverb")
         for (float decay : { 1.0f, 4.0f })
            for (double rr : { 48000.0, 96000.0 })
            {
               auto rig = MakeRig(def, rr, { { "decay", decay }, { "predelay", 0.0f } });
               std::vector<float> l, r;
               impulse(*rig, decay * 2.5 + 1.0, l, r);
               // RT60 in the 1 kHz octave band - the mid-band figure a decay
               // knob is specified by; damping is meant to shorten the highs.
               const double rt = Rt60(Band(l, 1000.0, rr), Band(r, 1000.0, rr), rr);
               printf("FXAUDIT SPEC Reverb decay %.1f s @ %.0f kHz -> RT60 (1 kHz octave) %.2f s, broadband %.2f s\n",
                      decay, rr / 1000.0, rt, Rt60(l, r, rr));
               if (rt < 0.0 || std::fabs(rt / decay - 1.0) > 0.25)
                  flag(fx, "rt60", "measured/decay knob ratio %.2f", rt / decay);
            }

      if (fx == "Delay")
      {
         for (double rr : rates)
         {
            auto rig = MakeRig(def, rr, { { "sync", 0.0f }, { "timeMs", 300.0f }, { "feedback", 0.0f } });
            std::vector<float> l, r;
            impulse(*rig, 0.6, l, r);
            float mx = 0.0f;
            for (float v : l)
               mx = std::max(mx, std::fabs(v));
            size_t at = 0;
            while (at < l.size() && std::fabs(l[at]) < 0.5f * mx)
               at++;
            const double ms = 1000.0 * (double)at / rr;
            printf("FXAUDIT SPEC Delay timeMs 300 @ %.1f kHz -> %.2f ms\n", rr / 1000.0, ms);
            if (std::fabs(ms - 300.0) > 0.5)
               flag(fx, "time", "%.2f ms for a 300 ms setting", ms);
         }
         auto rig = MakeRig(def, 48000.0, { { "sync", 0.0f }, { "timeMs", 120.0f }, { "feedback", 110.0f } });
         std::vector<float> in = Noise((int)(0.5 * 48000), 0.5f, 9), z((size_t)(15 * 48000), 0.0f), l, r, l2, r2;
         Process(*rig, in, in, l, r);
         Process(*rig, z, z, l2, r2);
         const double late = Db(Rms(l2, l2.size() - 48000, l2.size()));
         printf("FXAUDIT SPEC Delay feedback 110%% self-oscillation after 15 s: %.1f dBFS\n", late);
         if (late > 0.5)
            flag(fx, "fb", "feedback 110%% runs to %.1f dBFS", late);
      }

      if (fx == "Phaser")
      {
         // Analog mode across rates: octave-band power re the input, three
         // noise seeds, like the defaults rate check above.
         const double tones[3] = { 100.0, 1000.0, 6000.0 };
         double lv[3][3] = {};
         for (int ri = 0; ri < 3; ri++)
         {
            const double rr = rates[ri];
            double pin[3] = {}, pout[3] = {};
            for (unsigned seed : { 11u, 12u, 13u })
            {
               auto rig = MakeRig(def, rr, { { "analog", 1.0f } });
               std::vector<float> in = Noise((int)(4.0 * rr), 0.1f, seed), oL, oR;
               Process(*rig, in, in, oL, oR);
               for (int ti = 0; ti < 3; ti++)
               {
                  const std::vector<float> bi = Band(in, tones[ti], rr), bl = Band(oL, tones[ti], rr),
                                           br = Band(oR, tones[ti], rr);
                  pin[ti] += std::pow(Rms(bi, (size_t)rr, bi.size()), 2);
                  pout[ti] += 0.5 * (std::pow(Rms(bl, (size_t)rr, bl.size()), 2) + std::pow(Rms(br, (size_t)rr, br.size()), 2));
               }
            }
            for (int ti = 0; ti < 3; ti++)
               lv[ri][ti] = Db(std::sqrt(pout[ti]) / std::max(1e-12, std::sqrt(pin[ti])));
         }
         double spread = 0.0;
         for (int ti = 0; ti < 3; ti++)
            spread = std::max(spread, std::max({ lv[0][ti], lv[1][ti], lv[2][ti] }) - std::min({ lv[0][ti], lv[1][ti], lv[2][ti] }));
         printf("FXAUDIT SPEC Phaser analog 44.1/48/96 kHz: 100 Hz %+.2f/%+.2f/%+.2f, 1 kHz %+.2f/%+.2f/%+.2f, 6 kHz %+.2f/%+.2f/%+.2f dB -> spread %.2f dB\n",
                lv[0][0], lv[1][0], lv[2][0], lv[0][1], lv[1][1], lv[2][1], lv[0][2], lv[1][2], lv[2][2], spread);
         if (spread > 1.0)
            flag(fx, "rate", "analog mode level differs %.1f dB across 44.1/48/96 kHz", spread);
      }

      if (fx == "Tremolo")
         for (double rr : { 48000.0, 96000.0 })
         {
            auto rig = MakeRig(def, rr, { { "sync", 0.0f }, { "rate", 2.0f }, { "depth", 1.0f } });
            const int n = (int)(4.0 * rr);
            std::vector<float> in = Tone(n, 1000.0, rr, 0.3f), l, r;
            Process(*rig, in, in, l, r);
            // 10 ms RMS envelope, then the autocorrelation peak between 0.2 and 2 s.
            const int hop = (int)(0.005 * rr), w = (int)(0.01 * rr);
            std::vector<double> env;
            for (int a = (int)rr / 2; a + w < n; a += hop)
               env.push_back(Rms(l, (size_t)a, (size_t)(a + w)));
            double mean = 0;
            for (double v : env)
               mean += v;
            mean /= (double)env.size();
            // The first autocorrelation peak within 10% of the strongest one (later
            // peaks are multiples of the period).
            std::vector<double> ac;
            const int lag0 = (int)(0.2 / 0.005), lag1 = std::min((int)(1.2 / 0.005), (int)env.size() / 2);
            double bestC = -1e30;
            for (int lag = lag0; lag < lag1; lag++)
            {
               double c = 0;
               for (size_t i = 0; i + lag < env.size(); i++)
                  c += (env[i] - mean) * (env[i + lag] - mean);
               ac.push_back(c / (double)(env.size() - lag));
               bestC = std::max(bestC, ac.back());
            }
            int best = lag0;
            for (size_t k = 1; k + 1 < ac.size(); k++)
               if (ac[k] >= 0.9 * bestC && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1])
               {
                  best = lag0 + (int)k;
                  break;
               }
            const double hz = 1.0 / (best * 0.005);
            printf("FXAUDIT SPEC Tremolo rate 2 Hz @ %.0f kHz -> %.2f Hz\n", rr / 1000.0, hz);
            if (std::fabs(hz - 2.0) > 0.1)
               flag(fx, "lfo", "%.2f Hz for a 2 Hz setting", hz);
         }

      if (fx == "Audio Filter")
      {
         for (double rr : rates)
         {
            auto rig = MakeRig(def, rr, { { "freq", 1000.0f }, { "q", 0.707f } });
            std::vector<float> l, r;
            impulse(*rig, 0.5, l, r);
            const double ref = MagAt(l, 50.0, rr);
            double f3 = -1.0;
            for (double f = 200.0; f < 5000.0; f *= 1.005)
               if (MagAt(l, f, rr) < ref * 0.7079)
               {
                  f3 = f;
                  break;
               }
            const double at1k = Db(MagAt(l, 1000.0, rr) / ref);
            printf("FXAUDIT SPEC Audio Filter default type, 1 kHz q 0.707 @ %.1f kHz -> -3 dB at %.0f Hz, %.1f dB at 1 kHz\n",
                   rr / 1000.0, f3, at1k);
            if (f3 < 0.0 || std::fabs(f3 / 1000.0 - 1.0) > 0.08)
               flag(fx, "cutoff", "-3 dB point %.0f Hz for a 1 kHz setting", f3);
         }
         auto rig = MakeRig(def, 44100.0, { { "freq", 18000.0f }, { "q", 18.0f } });
         std::vector<float> in = Noise(44100 * 2, 0.5f, 5), l, r;
         Process(*rig, in, in, l, r);
         float pk = 0.0f;
         for (float v : l)
            pk = std::isfinite(v) ? std::max(pk, std::fabs(v)) : 1e9f;
         printf("FXAUDIT SPEC Audio Filter 18 kHz q 18 @ 44.1 kHz, -6 dBFS noise -> peak %.2f\n", pk);
         if (pk > 16.0f)
            flag(fx, "reso", "peak %.1f at 18 kHz q 18", pk);
      }

      if (fx == "EQ")
      {
         // Analog reference: one band at a time, impulse through the real node,
         // magnitude vs the analog prototype (cascaded per stage with the same
         // Butterworth stage Qs) from 20 Hz to min(20 kHz, 0.42 fs), skipping
         // deep stopband below -30 dB. The bilinear (RBJ) EQ this replaced was
         // off by 10-30 dB near the top of the band at 44.1 kHz.
         struct Case { int type; float freq, q, gain; };
         const Case cases[] = {
            { EqDsp::kPeak, 10000.0f, 1.0f, 12.0f }, { EqDsp::kPeak, 12000.0f, 2.0f, -12.0f },
            { EqDsp::kPeak, 1000.0f, 0.707f, 6.0f }, { EqDsp::kHighShelf, 8000.0f, 0.707f, 6.0f },
            { EqDsp::kHighShelf, 12000.0f, 0.707f, -9.0f }, { EqDsp::kLowShelf, 100.0f, 0.707f, 6.0f },
            { EqDsp::kLp24, 15000.0f, 0.707f, 0.0f }, { EqDsp::kHp36, 50.0f, 0.707f, 0.0f },
            { EqDsp::kLp12, 8000.0f, 2.0f, 0.0f }, { EqDsp::kBP, 12000.0f, 2.0f, 0.0f },
            // Resonant shelves: the overshoot bump next to the corner.
            { EqDsp::kHighShelf, 10000.0f, 3.0f, 9.0f }, { EqDsp::kHighShelf, 6000.0f, 2.0f, -9.0f },
            { EqDsp::kLowShelf, 200.0f, 3.0f, 9.0f }, { EqDsp::kLowShelf, 80.0f, 2.0f, -9.0f },
         };
         auto kindOf = [](int t) {
            switch (t)
            {
               case EqDsp::kLowShelf: return MatchedBiquad::kLowShelf;
               case EqDsp::kHighShelf: return MatchedBiquad::kHighShelf;
               case EqDsp::kHp12: case EqDsp::kHp24: case EqDsp::kHp36: return MatchedBiquad::kHighpass;
               case EqDsp::kLp12: case EqDsp::kLp24: case EqDsp::kLp36: return MatchedBiquad::kLowpass;
               case EqDsp::kBP: return MatchedBiquad::kBandpass;
               case EqDsp::kNotch: return MatchedBiquad::kNotch;
               case EqDsp::kAllpass: return MatchedBiquad::kAllpass;
               default: return MatchedBiquad::kPeak;
            }
         };
         for (double rr : rates)
         {
            double worst = 0.0;
            const char* worstName = "";
            float worstF = 0.0f, worstAt = 0.0f;
            for (const Case& c : cases)
            {
               std::map<std::string, float> over;
               for (int b = 1; b <= 5; b++)
                  over["band" + std::to_string(b) + "On"] = b == 3 ? 1.0f : 0.0f;
               over["band3Type"] = (float)c.type;
               over["band3Freq"] = c.freq;
               over["band3Q"] = c.q;
               over["band3Gain"] = c.gain;
               auto rig = MakeRig(def, rr, over);
               std::vector<float> l, r;
               impulse(*rig, 1.0, l, r);
               for (double f = 20.0; f <= std::min(20000.0, 0.42 * rr); f *= 1.03)
               {
                  double ana2 = 1.0;
                  for (int st = 0; st < EqDsp::StageCount(c.type); st++)
                     ana2 *= MatchedBiquad::AnalogMag2(
                        MatchedBiquad::MakeProto(kindOf(c.type), EqDsp::StageQ(c.type, st, c.q), c.gain), f / c.freq);
                  const double ana = 10.0 * std::log10(std::max(ana2, 1e-30));
                  if (ana < -30.0)
                     continue;
                  const double err = std::fabs(Db(MagAt(l, f, rr)) - ana);
                  if (err > worst)
                  {
                     worst = err;
                     worstName = EqDsp::TypeName(c.type);
                     worstF = c.freq;
                     worstAt = (float)f;
                  }
               }
            }
            printf("FXAUDIT SPEC EQ vs analog prototype @ %.1f kHz -> worst %.2f dB (%s %.0f Hz, at %.0f Hz)\n",
                   rr / 1000.0, worst, worstName, worstF, worstAt);
            if (worst > 1.5)
               flag(fx, "analog", "%.2f dB off the analog prototype", worst);
         }
      }

      if (fx == "Resonator Bank")
         for (double rr : { 48000.0, 96000.0 })
         {
            auto rig = MakeRig(def, rr, { { "rootFreq", 110.0f }, { "poles", 1.0f }, { "decay", 2.0f } });
            std::vector<float> l, r;
            impulse(*rig, 5.0, l, r);
            std::vector<float> head(l.begin(), l.begin() + (size_t)rr);
            double best = 0, bestF = 0;
            for (double f = 80.0; f < 140.0; f += 0.25)
            {
               const double m = MagAt(head, f, rr);
               if (m > best)
               {
                  best = m;
                  bestF = f;
               }
            }
            const double rt = Rt60(l, r, rr);
            printf("FXAUDIT SPEC Resonator Bank root 110 Hz decay 2 s @ %.0f kHz -> peak %.2f Hz, RT60 %.2f s\n",
                   rr / 1000.0, bestF, rt);
            if (std::fabs(bestF - 110.0) > 1.0)
               flag(fx, "pitch", "%.2f Hz for a 110 Hz root", bestF);
            if (rt < 0.0 || std::fabs(rt / 2.0 - 1.0) > 0.25)
               flag(fx, "decay", "RT60 %.2f s for a 2 s setting", rt);
         }
   }
}

bool RunFxAudit()
{
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
   _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ | DAZ, as the audio thread runs
#endif
   const char* only = getenv("INFINITE_FXAUDIT_ONLY");
   bool ok = true;
   Flagger flag;
   int audited = 0;
   for (const EffectDef& def : GetEffectDefs())
   {
      if (only && *only && def.name.find(only) == std::string::npos)
         continue;
      ok = AuditOne(def, flag) && ok;
      Specific(def, flag);
      audited++;
      fflush(stdout);
   }
   printf("FXAUDIT %d effect(s), %d flag(s) %s\n", audited, flag.count, ok ? "OK" : "FAIL");
   return ok;
}
}

// DSP self-test fixtures, part 4: the effect wrapper's dry/wet contract and
// the Limiter's ceiling. Same rig as DelayTest (AppShared.h): a real
// AudioEffectNode -> AudioEffectRuntime -> kernel pipeline, one sample per
// block, so what is measured is what a patch hears.
#include "app/AppShared.h"
#include <algorithm>
#include <climits>
#include <limits>

namespace app
{
namespace
{
   const EffectDef* FindEffectDef(const char* name)
   {
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == name)
            return &d;
      return nullptr;
   }

   std::unique_ptr<AudioEffectNode> MakeEffect(const EffectDef& def, float mix, double sampleRate)
   {
      auto node = std::make_unique<AudioEffectNode>(def);
      node->mix = mix;
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   // Deterministic white noise in [-1, 1].
   struct Lcg
   {
      uint32_t s = 12345u;
      float Next()
      {
         s = s * 1664525u + 1013904223u;
         return (float)((s >> 8) & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
      }
   };

   // Reference true peak: 16x band-limited interpolation with a long
   // (64-tap-per-side) Blackman-windowed sinc - much finer than the
   // limiter's own 4x detector, so it measures the detector rather than
   // agreeing with it.
   float ReferenceTruePeak(const std::vector<float>& x, size_t from)
   {
      constexpr int kUp = 16;
      constexpr int kHalf = 64;
      float peak = 0.0f;
      for (size_t k = from + kHalf; k + kHalf + 1 < x.size(); k++)
      {
         peak = std::max(peak, std::fabs(x[k]));
         for (int p = 1; p < kUp; p++)
         {
            const double frac = (double)p / kUp;
            double acc = 0.0;
            for (int j = -kHalf + 1; j <= kHalf; j++)
            {
               const double d = (double)j - frac;
               const double sinc = std::sin(M_PI * d) / (M_PI * d);
               const double r = (d + kHalf) / (2.0 * kHalf);
               const double win = 0.42 - 0.5 * std::cos(2.0 * M_PI * r) + 0.08 * std::cos(4.0 * M_PI * r);
               acc += x[k + j] * sinc * win;
            }
            peak = std::max(peak, (float)std::fabs(acc));
         }
      }
      return peak;
   }

   float Db(float lin) { return 20.0f * std::log10(std::max(lin, 1.0e-12f)); }
}

bool RunEffectMixAndLimiterFixture()
{
   bool all = true;
   const double sampleRate = 44100.0; // AudioEffectNode::CookIfNeeded's no-device fallback rate

   // 1) Dry path is latency-aligned. At mix 0 the output is the dry signal
   //    alone; the wrapper reports the kernel's latency to PDC, so that dry
   //    signal must come out exactly LatencySamples() late - otherwise any
   //    mix below 100% sums an early dry copy with the late wet one (a comb
   //    for the Limiter, a ~46 ms slapback for the 2048-point spectral
   //    effects). Checked for every effect that reports latency.
   {
      int checked = 0;
      bool ok = true;
      for (const EffectDef& def : GetEffectDefs())
      {
         if (def.forceFullyWet)
            continue;
         auto node = MakeEffect(def, 0.0f, sampleRate);
         const int latency = node->GetAudioNode()->LatencySamples();
         if (latency <= 0)
            continue;
         // Let the mix smoother settle on 0 before the impulse.
         const int warm = (int)(0.1 * sampleRate);
         const int tail = latency + 2048;
         std::vector<std::vector<float>> out;
         DelayTest::RunSamples(*node, warm + tail, 1, [warm](int i, int) { return i == warm ? 0.5f : 0.0f; }, &out);
         // A dry path that is a plain delay returns the impulse whole, at
         // `latency`. One that goes through the kernel's own filters with
         // the processing taken out (IEffectKernel::ProcessDry: Drive and
         // Wavetable Shaper's minimum-phase oversampler) smears it, so the
         // check is the one both designs owe PDC: unity gain at DC, and a
         // DC group delay sum(n h[n]) / sum(h[n]) equal to the reported
         // latency (exactly `latency` for a plain delay).
         double gain = 0.0, moment = 0.0;
         for (size_t i = (size_t)warm; i < out[0].size(); i++)
         {
            const double h = out[0][i] / 0.5;
            gain += h;
            moment += h * (double)((int)i - warm);
         }
         const double delay = std::fabs(gain) > 1.0e-9 ? moment / gain : -1.0;
         const bool thisOk = std::fabs(gain - 1.0) < 1.0e-3 && std::fabs(delay - (double)latency) < 0.5;
         if (!thisOk)
            printf("DSPTEST   %s: latency %d, dry DC gain %.5f, DC group delay %.3f samples\n", def.name.c_str(),
                   latency, gain, delay);
         ok &= thisOk;
         checked++;
      }
      ok &= checked > 0;
      printf("DSPTEST effect dry path delayed by reported latency: %d latent effects checked  %s\n", checked,
             ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 1b) A NaN or inf block does not stick. Recursive kernels (filters,
   //     delay lines, reverb tails, oversamplers) kept the first non-finite
   //     input forever, so one bad block silenced the chain until reload
   //     (B-K-023). After 2048 frames of NaN then inf, a clean sine must come
   //     out finite for every effect.
   {
      int stuck = 0, checked = 0;
      for (const EffectDef& def : GetEffectDefs())
      {
         auto node = MakeEffect(def, 1.0f, sampleRate);
         const int burst = 2048, total = (int)(1.5 * sampleRate);
         std::vector<std::vector<float>> out;
         DelayTest::RunSamples(*node, total, 2,
                               [&](int i, int) {
                                  if (i < burst)
                                     return std::numeric_limits<float>::quiet_NaN();
                                  if (i < 2 * burst)
                                     return std::numeric_limits<float>::infinity();
                                  return 0.25f * std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sampleRate);
                               },
                               &out);
         long long bad = 0;
         for (const auto& ch : out)
            for (size_t i = (size_t)(0.5 * sampleRate); i < ch.size(); i++)
               bad += !std::isfinite(ch[i]);
         if (bad > 0)
         {
            printf("DSPTEST   %s: %lld non-finite samples a second after the burst\n", def.name.c_str(), bad);
            stuck++;
         }
         checked++;
      }
      const bool ok = stuck == 0 && checked > 0;
      printf("DSPTEST effect recovers from a NaN/inf block: %d effects checked, %d stuck  %s\n", checked, stuck,
             ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 1c) A patch value past int range is dropped like NaN. Enums and counts
   //     are floats read back with (int)v, which was undefined for 1e30
   //     (B-K-026: 20+ effect bodies, kernels and shader uniforms). Every
   //     param keeps its default, an ordinary value still loads, and an int
   //     past range saturates instead of overflowing atoi.
   {
      int changed = 0, checked = 0;
      for (const EffectDef& def : GetEffectDefs())
      {
         auto node = MakeEffect(def, 1.0f, sampleRate);
         std::vector<std::pair<std::string, std::string>> before, after;
         Patch::SaveParams(node.get(), before);
         for (const char* big : { "1e30", "-1e30", "3e9", "-2147483648" })
         {
            auto hostile = before;
            for (auto& kv : hostile)
               if (kv.first.rfind("f ", 0) == 0)
                  kv.second = big;
            Patch::LoadParams(node.get(), hostile);
            Patch::SaveParams(node.get(), after);
            changed += after != before;
            checked++;
         }
      }
      auto eq = MakeEffect(*FindEffectDef("EQ"), 1.0f, sampleRate);
      Patch::LoadParams(eq.get(), { { "f mix", "0.25" } });
      const bool plainOk = std::fabs(eq->mix - 0.25f) < 1e-6f;
      MolderNode molder;
      Patch::LoadParams(&molder, { { "i seed", "99999999999" } });
      const bool intOk = molder.Seed() == (uint32_t)INT_MAX;
      const bool ok = changed == 0 && checked > 0 && plainOk && intOk;
      printf("DSPTEST patch values past int range are dropped: %d loads, %d changed a param, plain=%d int=%d  %s\n",
             checked, changed, plainOk, intOk, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 1d) A patch value in float range but past the def's range is clamped to
   //     it. 1e9 passes 1c; the knobs bind only the UI, and Stereo scaled its
   //     side signal by a width of 1e9 (B-K-031). Every param stores inside
   //     its range and a sine comes out finite; Stereo stays near unity.
   {
      int outOfRange = 0, nonFinite = 0, checked = 0;
      for (const EffectDef& def : GetEffectDefs())
      {
         for (const char* big : { "1e9", "-1e9" })
         {
            auto node = MakeEffect(def, 1.0f, sampleRate);
            std::vector<std::pair<std::string, std::string>> hostile;
            Patch::SaveParams(node.get(), hostile);
            for (auto& kv : hostile)
               if (kv.first.rfind("f ", 0) == 0)
                  kv.second = big;
            Patch::LoadParams(node.get(), hostile);
            bool inRange = node->mix >= 0.0f && node->mix <= 1.0f;
            for (size_t i = 0; i < def.params.size(); i++)
               inRange &= node->Param(i) >= def.params[i].minVal && node->Param(i) <= def.params[i].maxVal;
            node->CookIfNeeded(2);
            std::vector<std::vector<float>> out;
            DelayTest::RunSamples(*node, (int)(0.25 * sampleRate), 2,
                                  [&](int i, int) { return 0.25f * std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sampleRate); },
                                  &out);
            long long bad = 0;
            for (const auto& ch : out)
               for (float s : ch)
                  bad += !std::isfinite(s);
            if (!inRange || bad > 0)
               printf("DSPTEST   %s at %s: %s, %lld non-finite samples\n", def.name.c_str(), big,
                      inRange ? "in range" : "OUT OF RANGE", bad);
            outOfRange += !inRange;
            nonFinite += bad > 0;
            checked++;
         }
      }
      auto stereo = MakeEffect(*FindEffectDef("Stereo"), 1.0f, sampleRate);
      Patch::LoadParams(stereo.get(), { { "f width", "1e9" } });
      stereo->CookIfNeeded(2);
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(*stereo, (int)(0.25 * sampleRate), 2,
                            [&](int i, int ch) { return (ch == 0 ? 0.25f : -0.25f) * std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sampleRate); },
                            &out);
      float stereoPeak = 0.0f;
      for (const auto& ch : out)
         for (float s : ch)
            stereoPeak = std::max(stereoPeak, std::fabs(s));
      const bool ok = outOfRange == 0 && nonFinite == 0 && checked > 0 && stereoPeak < 1.0f;
      printf("DSPTEST patch values past a param's range are clamped: %d loads, %d out of range, %d non-finite, "
             "Stereo width 1e9 peak %.3g  %s\n",
             checked, outOfRange, nonFinite, stereoPeak, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 1e) The same for image filters. Shaders take the def's range on trust:
   //     dither loops uMode + 1 times, and uMode 1e9 hung the GPU (B-K-035).
   //     Every filter param but colours stores inside its range at ±1e9.
   //     Load only, no GL: the uniforms are the stored values.
   {
      int outOfRange = 0, checked = 0;
      for (const FilterDef& def : GetFilterDefs())
      {
         for (const char* big : { "1e9", "-1e9" })
         {
            FilterNode node(def);
            std::vector<std::pair<std::string, std::string>> hostile;
            Patch::SaveParams(&node, hostile);
            for (auto& kv : hostile)
               if (kv.first.rfind("f ", 0) == 0)
                  kv.second = big;
            Patch::LoadParams(&node, hostile);
            for (size_t i = 0; i < def.params.size(); i++)
            {
               const FilterParamDef& p = def.params[i];
               if (p.type == FilterParamDef::Type::Color || !(p.minVal < p.maxVal))
                  continue;
               const float v = node.GetParamValue(i);
               if (v < p.minVal || v > p.maxVal)
               {
                  printf("DSPTEST   %s.%s at %s: %g OUT OF RANGE\n", def.name.c_str(), p.uniformName.c_str(), big, v);
                  outOfRange++;
               }
            }
            checked++;
         }
      }
      const bool ok = outOfRange == 0 && checked > 0;
      printf("DSPTEST filter params past the def's range are clamped: %d loads, %d out of range  %s\n",
             checked, outOfRange, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 1f) Sample players: volume 1e9 from a patch played ~5e8 loud, and pitch or
   //     speed 1e9 overflowed a float-to-int read position (B-K-036, found by
   //     INFINITE_PARAMFUZZ). Each listed key stores inside its knob range.
   {
      struct Spec { const char* key; float lo, hi; };
      struct Player { const char* name; std::function<INode*()> make; std::vector<Spec> specs; };
      std::vector<std::string> drumKeys;
      for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
         for (int step = 0; step < DrumSequencerNode::kMaxSteps; step++)
            drumKeys.push_back("lane" + std::to_string(lane) + "_step" + std::to_string(step));
      std::vector<Spec> drum = { { "volume", 0.0f, 1.0f } };
      for (const std::string& k : drumKeys)
         drum.push_back({ k.c_str(), 0.0f, 1.0f }); // step velocity, as the grid sets it
      const std::vector<Player> players = {
         { "Audio File", [] { return (INode*)new AudioFileNode(); },
           { { "volume", 0.0f, 1.0f }, { "gain", 0.1f, 16.0f }, { "pitch", -24.0f, 24.0f } } },
         { "Slicer", [] { return (INode*)new SlicerNode(); },
           { { "pitch", -24.0f, 24.0f }, { "finetune", -100.0f, 100.0f }, { "speed", 0.25f, 4.0f }, { "volume", 0.0f, 1.0f } } },
         { "PaulStretch", [] { return (INode*)new PaulStretchNode(); },
           { { "volume", 0.0f, 2.0f }, { "pitch_shift", -24.0f, 24.0f }, { "fine_tune", -100.0f, 100.0f },
             { "freq_shift", -1000.0f, 1000.0f } } },
         { "Molder", [] { return (INode*)new MolderNode(); },
           { { "level", 0.0f, 2.0f }, { "start", 0.0f, 1.0f }, { "end", 0.0f, 1.0f }, { "pitch", -24.0f, 24.0f } } },
         { "Granular", [] { return (INode*)new GranularNode(); },
           { { "volume", 0.0f, 2.0f }, { "pitchShift", -24.0f, 24.0f }, { "fineTune", -100.0f, 100.0f },
             { "start", 0.0f, 1.0f }, { "end", 0.0f, 1.0f } } },
         { "Sampler", [] { return (INode*)new SamplerNode(); },
           { { "pitch", -24.0f, 24.0f }, { "finetune", -50.0f, 50.0f }, { "speed", -2.0f, 2.0f }, { "volume", 0.0f, 1.0f } } },
         { "Drum Sequencer", [] { return (INode*)new DrumSequencerNode(); }, drum },
      };
      int outOfRange = 0, checked = 0;
      for (const Player& pl : players)
      {
         for (const char* big : { "1e9", "-1e9" })
         {
            std::unique_ptr<INode> node(pl.make());
            std::vector<std::pair<std::string, std::string>> hostile;
            Patch::SaveParams(node.get(), hostile);
            for (auto& kv : hostile)
               if (kv.first.rfind("f ", 0) == 0)
                  kv.second = big;
            Patch::LoadParams(node.get(), hostile);
            std::vector<std::pair<std::string, std::string>> after;
            Patch::SaveParams(node.get(), after);
            for (const Spec& sp : pl.specs)
            {
               const std::string key = std::string("f ") + sp.key;
               bool found = false;
               for (const auto& kv : after)
               {
                  if (kv.first != key)
                     continue;
                  found = true;
                  const float v = std::strtof(kv.second.c_str(), nullptr);
                  if (!(v >= sp.lo && v <= sp.hi))
                  {
                     printf("DSPTEST   %s.%s at %s: %g OUT OF RANGE\n", pl.name, sp.key, big, v);
                     outOfRange++;
                  }
               }
               if (!found)
               {
                  printf("DSPTEST   %s.%s: no such key\n", pl.name, sp.key);
                  outOfRange++;
               }
               checked++;
            }
         }
      }
      const bool ok = outOfRange == 0 && checked > 0;
      printf("DSPTEST sample player params past the knob range are clamped: %d values, %d out of range  %s\n",
             checked, outOfRange, ok ? "OK" : "FAIL");
      all &= ok;
   }

   const EffectDef* limiterDef = FindEffectDef("Limiter");
   if (limiterDef == nullptr)
   {
      printf("DSPTEST limiter: no Limiter EffectDef registered  FAIL\n");
      return false;
   }

   auto makeLimiter = [&](float thresholdDb, float mix) {
      auto node = std::make_unique<AudioEffectNode>(*limiterDef);
      node->mix = mix;
      *node->ParamPtr("threshold") = thresholdDb;
      *node->ParamPtr("release") = 50.0f;
      *node->ParamPtr("inGain") = 0.0f;
      *node->ParamPtr("outGain") = 0.0f;
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      // Settle the mailbox smoothers on the pushed values before measuring.
      DelayTest::RunSamples(*node, (int)(0.2 * sampleRate), 1, [](int, int) { return 0.0f; }, nullptr);
      return node;
   };

   const float thresholdDb = -6.0f;
   const float ceiling = std::pow(10.0f, thresholdDb / 20.0f);

   // 2) Sample-peak ceiling holds on the signals that broke the old one-pole
   //    attack: a sudden +12 dB tone burst, hot noise, and isolated spikes.
   {
      struct Case
      {
         const char* label;
         std::function<float(int)> fn;
      };
      const int n = (int)(0.5 * sampleRate);
      Lcg noise;
      std::vector<float> noiseBuf((size_t)n);
      for (float& v : noiseBuf)
         v = noise.Next() * 2.0f; // ~ +12 dB over the -6 dB ceiling at its peaks
      const Case cases[] = {
         { "tone burst", [&](int i) {
              const float a = (i > n / 4 && i < n / 2) ? 2.0f : 0.1f;
              return a * std::sin(2.0f * (float)M_PI * 220.0f * (float)i / (float)sampleRate);
           } },
         { "hot noise", [&](int i) { return noiseBuf[(size_t)i]; } },
         { "spikes", [&](int i) { return (i % 2000) == 1000 ? 4.0f : 0.0f; } },
      };
      bool ok = true;
      for (const Case& c : cases)
      {
         auto node = makeLimiter(thresholdDb, 1.0f);
         std::vector<std::vector<float>> out;
         DelayTest::RunSamples(*node, n, 1, [&](int i, int) { return c.fn(i); }, &out);
         float peak = 0.0f;
         for (float v : out[0])
            peak = std::max(peak, std::fabs(v));
         const bool thisOk = peak <= ceiling * (1.0f + 1.0e-5f);
         printf("DSPTEST limiter sample-peak ceiling (%s): peak %+.3f dB vs ceiling %+.1f dB  %s\n", c.label,
                Db(peak), thresholdDb, thisOk ? "OK" : "FAIL");
         ok &= thisOk;
      }
      all &= ok;
   }

   // 3) True peak. A tone at fs/4 sampled 45 degrees off its crest has every
   //    sample at 0.707 of its real peak: sample peaks sit under the ceiling
   //    while the reconstructed wave goes +3 dB over it. The limiter must
   //    catch the inter-sample peak.
   {
      auto node = makeLimiter(thresholdDb, 1.0f);
      const int n = (int)(0.25 * sampleRate);
      const float amp = ceiling * 0.98f / 0.70710678f; // sample peaks 0.98 x ceiling
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(
         *node, n, 1,
         [amp](int i, int) { return amp * std::sin(0.5f * (float)M_PI * (float)i + 0.25f * (float)M_PI); }, &out);
      const float tp = ReferenceTruePeak(out[0], (size_t)(0.1 * sampleRate));
      const bool ok = Db(tp) <= thresholdDb + 0.1f;
      printf("DSPTEST limiter true-peak ceiling (fs/4 tone, +2.8 dB inter-sample): true peak %+.2f dB vs ceiling "
             "%+.1f dB  %s\n",
             Db(tp), thresholdDb, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 4) Below the ceiling the limiter is a pure delay of exactly
   //    LatencySamples() - no gain change, no smearing.
   {
      auto node = makeLimiter(thresholdDb, 1.0f);
      const int latency = node->GetAudioNode()->LatencySamples();
      const int n = 4096;
      auto sig = [](int i) { return 0.25f * std::sin(2.0f * (float)M_PI * 997.0f * (float)i / 44100.0f); };
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(*node, n, 1, [&](int i, int) { return sig(i); }, &out);
      float maxErr = 0.0f;
      for (int i = latency; i < n; i++)
         maxErr = std::max(maxErr, std::fabs(out[0][(size_t)i] - sig(i - latency)));
      const bool ok = latency > 0 && maxErr < 1.0e-5f;
      printf("DSPTEST limiter transparent below ceiling: latency %d, max |out - delayed in| %.2e  %s\n", latency,
             maxErr, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 5) Mix law. The Limiter's wet signal below the ceiling is the dry signal
   //    itself, so at mix 0.5 the two must sum back to unity - an
   //    equal-power law (cos + sin = 1.414) makes a half-mixed limiter +3 dB
   //    louder than either end of the knob.
   {
      auto node = makeLimiter(thresholdDb, 0.5f);
      const int latency = node->GetAudioNode()->LatencySamples();
      const int n = 4096;
      auto sig = [](int i) { return 0.25f * std::sin(2.0f * (float)M_PI * 997.0f * (float)i / 44100.0f); };
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(*node, n, 1, [&](int i, int) { return sig(i); }, &out);
      double num = 0.0, den = 0.0;
      for (int i = latency + 1024; i < n; i++)
      {
         num += (double)out[0][(size_t)i] * out[0][(size_t)i];
         den += (double)sig(i - latency) * sig(i - latency);
      }
      const float gainDb = 10.0f * (float)std::log10(num / std::max(den, 1.0e-30));
      const bool ok = std::fabs(gainDb) < 0.05f;
      printf("DSPTEST limiter mix 0.5 below ceiling: level %+.2f dB (want 0.00)  %s\n", gainDb, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // 6) Drive keeps the bass. Its always-on DC blocker was a fixed
   //    R = 0.995 one-pole, a 35 Hz high-pass at 44.1 kHz (76 Hz at
   //    96 kHz); it is now 8 Hz at every rate. At 0 dB drive and a small
   //    level the curve is nearly linear, so a 40 Hz tone must come out
   //    within 0.3 dB of a 1 kHz one - at 44.1 and 96 kHz.
   {
      const EffectDef* driveDef = FindEffectDef("Drive");
      bool ok = driveDef != nullptr;
      for (double rate : { 44100.0, 96000.0 })
      {
         if (!ok)
            break;
         auto levelAt = [&](double hz) {
            auto node = std::make_unique<AudioEffectNode>(*driveDef);
            node->mix = 1.0f;
            *node->ParamPtr("drive") = 0.0f;
            node->GetAudioNode()->PrepareToPlay(rate, 512);
            node->CookIfNeeded(1);
            const int n = (int)(1.5 * rate);
            std::vector<std::vector<float>> out;
            DelayTest::RunSamples(
               *node, n, 1,
               [hz, rate](int i, int) { return 0.05f * (float)std::sin(2.0 * M_PI * hz * (double)i / rate); }, &out);
            double sum = 0.0;
            const int from = n / 2;
            for (int i = from; i < n; i++)
               sum += (double)out[0][(size_t)i] * out[0][(size_t)i];
            return 10.0 * std::log10(sum / (double)(n - from));
         };
         const double diff = levelAt(40.0) - levelAt(1000.0);
         const bool thisOk = std::fabs(diff) < 0.3;
         printf("DSPTEST drive bass response @ %.0f Hz: 40 Hz vs 1 kHz %+.2f dB (want ~0)  %s\n", rate, diff,
                thisOk ? "OK" : "FAIL");
         ok &= thisOk;
      }
      all &= ok;
   }

   // 7) Bitcrush's `bits` is the bit depth it says: an N-bit converter has
   //    2^N levels. (It used to deliver 2^(N+1) - 1 - a bit more than shown.)
   //    And TPDF dither does what dither is for: a quiet tone through an
   //    8-bit crush has its quantisation distortion (harmonics) replaced by
   //    a noise floor.
   {
      const EffectDef* bcDef = FindEffectDef("Bitcrush");
      bool ok = bcDef != nullptr;
      auto makeCrush = [&](float bits, float dither) {
         auto node = std::make_unique<AudioEffectNode>(*bcDef);
         node->mix = 1.0f;
         *node->ParamPtr("rate") = 44100.0f; // no hold at the fixture's rate: quantiser only
         *node->ParamPtr("bits") = bits;
         *node->ParamPtr("dither") = dither;
         node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
         node->CookIfNeeded(1);
         DelayTest::RunSamples(*node, 4410, 1, [](int, int) { return 0.0f; }, nullptr);
         return node;
      };
      if (ok)
      {
         for (float bits : { 1.0f, 4.0f, 8.0f })
         {
            auto node = makeCrush(bits, 0.0f);
            const int n = 1 << 16;
            std::vector<std::vector<float>> out;
            DelayTest::RunSamples(*node, n, 1, [n](int i, int) { return -1.0f + 2.0f * (float)i / (float)n; }, &out);
            std::vector<float> v = out[0];
            std::sort(v.begin(), v.end());
            const int levels = (int)(std::unique(v.begin(), v.end()) - v.begin());
            const int want = 1 << (int)bits;
            const bool thisOk = levels == want;
            printf("DSPTEST bitcrush %.0f bits: %d output levels on a full-scale ramp (want %d)  %s\n", bits, levels,
                   want, thisOk ? "OK" : "FAIL");
            ok &= thisOk;
         }

         // A 3-LSB, 1 kHz tone at 8 bits. Harmonics 2..10 re the fundamental,
         // by quadrature correlation over a whole number of cycles.
         auto harmonicsDb = [&](float dither) {
            auto node = makeCrush(8.0f, dither);
            const double f = 1000.0;
            const int n = 44100; // 1000 whole cycles
            const float amp = 3.0f * (2.0f / 256.0f);
            std::vector<std::vector<float>> out;
            DelayTest::RunSamples(
               *node, n, 1, [&](int i, int) { return amp * (float)std::sin(2.0 * M_PI * f * i / sampleRate); }, &out);
            auto power = [&](int h) {
               double c = 0.0, s = 0.0;
               for (int i = 0; i < n; i++)
               {
                  const double w = 2.0 * M_PI * f * h * i / sampleRate;
                  c += out[0][(size_t)i] * std::cos(w);
                  s += out[0][(size_t)i] * std::sin(w);
               }
               return c * c + s * s;
            };
            double harm = 0.0;
            for (int h = 2; h <= 10; h++)
               harm += power(h);
            return 10.0 * std::log10(std::max(harm, 1e-30) / power(1));
         };
         // Dithered, what is left in the nine harmonic bins is the dither's
         // own noise: TPDF + rounding is 0.25 LSB^2 against the tone's
         // 4.5 LSB^2, spread over 22 kHz, i.e. about -46 dB in nine 1 Hz
         // bins. So "at the noise floor" is the bar, not zero.
         const double plain = harmonicsDb(0.0f), dithered = harmonicsDb(1.0f);
         const bool ditherOk = plain > -30.0 && dithered < -40.0 && dithered < plain - 15.0;
         printf("DSPTEST bitcrush dither: harmonics of a 3-LSB tone %.1f dB undithered, %.1f dB dithered  %s\n", plain,
                dithered, ditherOk ? "OK" : "FAIL");
         ok &= ditherOk;
      }
      all &= ok;
   }

   // 8) Dynamics' `scHpf` takes bass out of the detector only: with it on, a
   //    loud 40 Hz tone barely moves the gain, while the audio path itself is
   //    unchanged (same output for a tone well above the cutoff).
   {
      const EffectDef* dynDef = FindEffectDef("Dynamics");
      bool ok = dynDef != nullptr;
      auto grDb = [&](float scHpf, double hz) {
         auto node = std::make_unique<AudioEffectNode>(*dynDef);
         node->mix = 1.0f;
         *node->ParamPtr("threshold") = -30.0f;
         *node->ParamPtr("ratio") = 8.0f;
         *node->ParamPtr("makeup") = 0.0f;
         *node->ParamPtr("scHpf") = scHpf;
         node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
         node->CookIfNeeded(1);
         const int n = (int)(1.0 * sampleRate);
         std::vector<std::vector<float>> out;
         DelayTest::RunSamples(
            *node, n, 1, [&](int i, int) { return 0.5f * (float)std::sin(2.0 * M_PI * hz * i / sampleRate); }, &out);
         double in2 = 0.0, out2 = 0.0;
         for (int i = n / 2; i < n; i++)
         {
            const double x = 0.5 * std::sin(2.0 * M_PI * hz * i / sampleRate);
            in2 += x * x;
            out2 += (double)out[0][(size_t)i] * out[0][(size_t)i];
         }
         return 10.0 * std::log10(in2 / std::max(out2, 1e-30)); // gain reduction, dB
      };
      if (ok)
      {
         const double bassOff = grDb(0.0f, 40.0), bassOn = grDb(1.0f, 40.0);
         const double midOff = grDb(0.0f, 2000.0), midOn = grDb(1.0f, 2000.0);
         ok = bassOff > 15.0 && bassOn < bassOff - 10.0 && std::fabs(midOn - midOff) < 0.5;
         printf("DSPTEST dynamics sc hpf: 40 Hz reduction %.1f -> %.1f dB, 2 kHz %.1f -> %.1f dB  %s\n", bassOff,
                bassOn, midOff, midOn, ok ? "OK" : "FAIL");
      }
      all &= ok;
   }

   // 9) Stereo coherence of the spectral effects. Real stereo is mostly the
   //    same sources at different pans, so L and R are different mixes of
   //    the same partials (here two note groups panned apart, input L/R
   //    correlation ~0.9). The output must stay about as correlated: a phase
   //    vocoder that accumulates each channel's output phases on its own
   //    lets them random-walk apart, and the result cancels in mono.
   {
      struct Case
      {
         const char* effect;
         std::vector<std::pair<const char*, float>> params;
      };
      const Case cases[] = {
         { "Spectrum Slide", { { "slide", 0.5f } } },
         { "Key-Snap", {} },
         { "Spec Blur", { { "diffusion", 0.0f } } },
         { "Pitch Shifter", { { "pitch", 7.0f }, { "spectral", 1.0f } } },
      };
      bool ok = true;
      for (const Case& c : cases)
      {
         const EffectDef* def = FindEffectDef(c.effect);
         if (def == nullptr)
         {
            ok = false;
            continue;
         }
         auto node = std::make_unique<AudioEffectNode>(*def);
         node->mix = 1.0f;
         for (const auto& [k, v] : c.params)
            if (float* p = node->ParamPtr(k))
               *p = v;
         node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
         node->CookIfNeeded(1);
         const int n = (int)(3.0 * sampleRate);
         Lcg rng;
         std::vector<float> noiseR((size_t)n);
         for (float& v : noiseR)
            v = 0.004f * rng.Next();
         // Group 1 (A3, E4, E6) panned left of centre, group 2 (C#4 + B4)
         // right of centre.
         auto groupA = [&](int i) {
            const double t = (double)i / sampleRate;
            return 0.2 * std::sin(2 * M_PI * 220.0 * t) + 0.12 * std::sin(2 * M_PI * 329.6 * t + 0.4) +
                   0.05 * std::sin(2 * M_PI * 1318.5 * t);
         };
         auto groupB = [&](int i) {
            const double t = (double)i / sampleRate;
            return 0.15 * std::sin(2 * M_PI * 277.2 * t) + 0.1 * std::sin(2 * M_PI * 493.9 * t + 1.1);
         };
         auto music = [&](int i, int ch) {
            return (float)(ch == 0 ? groupA(i) + 0.6 * groupB(i) : 0.6 * groupA(i) + groupB(i));
         };
         // Two-input effects (Spectrum Slide's "to") get a second near-mono
         // signal there - with no cable, Spectrum Slide mirrors A into B and
         // would hide the problem.
         auto other = [&](int i) {
            const double t = (double)i / sampleRate;
            return (float)(0.25 * std::sin(2 * M_PI * 440.0 * t) + 0.1 * std::sin(2 * M_PI * 660.0 * t + 1.0));
         };
         std::vector<std::vector<float>> out(2);
         {
            AudioNode* an = node->GetAudioNode();
            float inA[2], inB[2], o[2];
            float* pa[2] = { &inA[0], &inA[1] };
            float* pb[2] = { &inB[0], &inB[1] };
            float* po[2] = { &o[0], &o[1] };
            AudioBuffer ba, bb, bo;
            ba.channels = pa;
            bb.channels = pb;
            bo.channels = po;
            ba.numChannels = bb.numChannels = bo.numChannels = 2;
            ba.numFrames = bb.numFrames = bo.numFrames = 1;
            const AudioBuffer* inputs[2] = { &ba, &bb };
            const int numInputs = def->hasSidechain ? 2 : 1;
            for (int i = 0; i < n; i++)
            {
               for (int ch = 0; ch < 2; ch++)
               {
                  inA[ch] = music(i, ch) + (ch == 1 ? noiseR[(size_t)i] : 0.0f);
                  inB[ch] = other(i) + (ch == 1 ? 0.5f * noiseR[(size_t)i] : 0.0f);
               }
               an->ProcessBlock(inputs, numInputs, bo);
               out[0].push_back(o[0]);
               out[1].push_back(o[1]);
            }
         }
         auto correlation = [&](auto&& l, auto&& r) {
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (int i = n / 3; i < n; i++)
            {
               lr += (double)l(i) * r(i);
               ll += (double)l(i) * l(i);
               rr += (double)r(i) * r(i);
            }
            return lr / std::sqrt(std::max(ll * rr, 1e-30));
         };
         const double inCorr = correlation([&](int i) { return music(i, 0); }, [&](int i) { return music(i, 1); });
         const double corr =
            correlation([&](int i) { return out[0][(size_t)i]; }, [&](int i) { return out[1][(size_t)i]; });
         const bool thisOk = corr > inCorr - 0.1;
         printf("DSPTEST %s stereo coherence: input L/R correlation %.3f, output %.3f (want within 0.1)  %s\n",
                c.effect, inCorr, corr, thisOk ? "OK" : "FAIL");
         ok &= thisOk;
      }
      all &= ok;
   }

   // A gain past the knob's range is clamped on load (B-K-032). 1e9 dB passes
   // Patch's int-range drop; 10^(1e9/20) is inf, and silence times inf is
   // NaN out of Gain. Mixer and Audio In share the -60..12 dB range; Velocity
   // Curve's curve of -1 sent 1/velocity downstream.
   {
      int bad = 0;
      auto expect = [&](const char* what, float got, float lo, float hi) {
         if (!(got >= lo && got <= hi))
         {
            printf("DSPTEST   %s = %g, outside %g..%g\n", what, got, lo, hi);
            bad++;
         }
      };
      long long nonFinite = 0;
      for (const char* big : { "1e9", "-1e9" })
      {
         GainNode gain;
         Patch::LoadParams(&gain, { { "f gainDb", big } });
         expect("Gain gainDb", gain.gainDb, -60.0f, 12.0f);
         AudioNode* an = gain.GetAudioNode();
         gain.CookIfNeeded(1);
         an->PrepareToPlay(sampleRate, 64);
         float l[64] = {}, r[64] = {};
         float* po[2] = { l, r };
         AudioBuffer bo;
         bo.channels = po;
         bo.numChannels = 2;
         bo.numFrames = 64;
         an->ProcessBlock(nullptr, 0, bo);
         for (int i = 0; i < 64; i++)
            nonFinite += !std::isfinite(l[i]) + !std::isfinite(r[i]);

         MixerNode mixer;
         Patch::LoadParams(&mixer, { { "f gain0", big }, { "f pan0", big } });
         expect("Mixer gain0", mixer.gainDb[0], -60.0f, 12.0f);
         expect("Mixer pan0", mixer.pan[0], -1.0f, 1.0f);

         AudioInputNode in;
         Patch::LoadParams(&in, { { "f gainDb", big } });
         expect("Audio In gainDb", in.gainDb, -60.0f, 12.0f);

         std::unique_ptr<INode> vc(VelocityCurveNode::Create());
         Patch::LoadParams(vc.get(), { { "f curve", big } });
         expect("Velocity Curve curve", static_cast<VelocityCurveNode*>(vc.get())->curve, 0.25f, 4.0f);
      }
      std::unique_ptr<INode> vc(VelocityCurveNode::Create());
      Patch::LoadParams(vc.get(), { { "f curve", "-1" } });
      expect("Velocity Curve curve -1", static_cast<VelocityCurveNode*>(vc.get())->curve, 0.25f, 4.0f);
      const bool ok = bad == 0 && nonFinite == 0;
      printf("DSPTEST gain and curve past the knob range are clamped: %d out of range, Gain 1e9 dB %lld non-finite  %s\n",
             bad, nonFinite, ok ? "OK" : "FAIL");
      all &= ok;
   }

   // Set Color's palette offset is an int any patch, OSC or RPC set can hold.
   // index + offset overflowed int at INT_MAX (UB); the swatch must stay in range.
   {
      int bad = 0;
      const int offsets[] = { 0, 3, -5, INT_MAX, INT_MIN, INT_MAX - 1 };
      const size_t indices[] = { 0, 1, 7, 199999 };
      for (int off : offsets)
         for (size_t i : indices)
         {
            const int s = PaletteSwatchIndex(i, off, 5);
            if (s < 0 || s >= 5)
               bad++;
         }
      const bool ok = bad == 0 && PaletteSwatchIndex(1, 3, 5) == 4 && PaletteSwatchIndex(0, -1, 5) == 4;
      printf("DSPTEST palette offset at the int limits stays in range: %d bad  %s\n", bad, ok ? "OK" : "FAIL");
      all &= ok;
   }

   return all;
}
}

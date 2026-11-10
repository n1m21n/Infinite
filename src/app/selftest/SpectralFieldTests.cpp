// ============================================================ INFINITE_SPECTRALFIELDTEST
// Headless checks for the Spectral Field node (docs/plans/spectral-field/
// README.md section 7): every preset on a fixed 48 kHz test signal, the
// identity round trip, zero allocation in ProcessBlock, a failed compile
// mid-render, latency and Mix, and microseconds per block at 2048 / 8192.
#include "app/AppShared.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"

#include <chrono>
#include <cstdarg>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

namespace
{
   constexpr double kSr = 48000.0;
   constexpr int kBlock = 512;
   constexpr double kPi = 3.14159265358979323846;

   using Signal = std::vector<float>;

   // ---------------------------------------------------------- signals
   struct Lcg
   {
      uint32_t s = 12345u;
      float Next() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) * (1.0 / 16777216.0)) * 2.0f - 1.0f; }
   };

   Signal Noise(int n, float amp, uint32_t seed = 12345u)
   {
      Lcg r; r.s = seed;
      Signal x((size_t)n);
      for (float& v : x) v = amp * r.Next();
      return x;
   }

   Signal Sine(int n, double hz, float amp)
   {
      Signal x((size_t)n);
      for (int i = 0; i < n; i++) x[(size_t)i] = amp * (float)std::sin(2.0 * kPi * hz * i / kSr);
      return x;
   }

   // Band-limited sawtooth (additive), peak about amp.
   Signal Saw(int n, double hz, float amp)
   {
      Signal x((size_t)n, 0.0f);
      const int harmonics = (int)(kSr * 0.5 / hz) - 1;
      for (int k = 1; k <= harmonics; k++)
      {
         const double g = amp * (2.0 / kPi) * ((k & 1) ? 1.0 : -1.0) / k;
         const double w = 2.0 * kPi * hz * k / kSr;
         for (int i = 0; i < n; i++) x[(size_t)i] += (float)(g * std::sin(w * i));
      }
      return x;
   }

   // Pink-ish noise (Paul Kellet's economy filter) through four one-poles at 4 kHz.
   Signal PinkLowpassed(int n)
   {
      Lcg r; r.s = 777u;
      Signal x((size_t)n);
      double b0 = 0, b1 = 0, b2 = 0, l[4] = {};
      const double a = std::exp(-2.0 * kPi * 4000.0 / kSr);
      for (int i = 0; i < n; i++)
      {
         const double w = r.Next();
         b0 = 0.99765 * b0 + w * 0.0990460;
         b1 = 0.96300 * b1 + w * 0.2965164;
         b2 = 0.57000 * b2 + w * 1.0526913;
         double v = (b0 + b1 + b2 + w * 0.1848) * 0.1;
         for (double& s : l) { s = (1.0 - a) * v + a * s; v = s; }
         x[(size_t)i] = (float)v;
      }
      return x;
   }

   // ---------------------------------------------------------- analysis
   void Fft(std::vector<std::complex<double>>& a)
   {
      const size_t n = a.size();
      for (size_t i = 1, j = 0; i < n; i++)
      {
         size_t bit = n >> 1;
         for (; j & bit; bit >>= 1) j ^= bit;
         j ^= bit;
         if (i < j) std::swap(a[i], a[j]);
      }
      for (size_t len = 2; len <= n; len <<= 1)
      {
         const double ang = -2.0 * kPi / (double)len;
         const std::complex<double> wl(std::cos(ang), std::sin(ang));
         for (size_t i = 0; i < n; i += len)
         {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; k++)
            {
               const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
               a[i + k] = u + v;
               a[i + k + len / 2] = u - v;
               w *= wl;
            }
         }
      }
   }

   // Welch-averaged Hann power spectrum, n/2 + 1 bins, of x[start, start + len).
   std::vector<double> Power(const Signal& x, size_t start, size_t len, int n)
   {
      std::vector<double> p((size_t)n / 2 + 1, 0.0);
      int frames = 0;
      std::vector<std::complex<double>> a((size_t)n);
      for (size_t off = start; off + (size_t)n <= start + len && off + (size_t)n <= x.size(); off += (size_t)n / 2)
      {
         for (int i = 0; i < n; i++)
         {
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
            a[(size_t)i] = { x[off + (size_t)i] * w, 0.0 };
         }
         Fft(a);
         for (size_t k = 0; k < p.size(); k++) p[k] += std::norm(a[k]);
         frames++;
      }
      for (double& v : p) v /= std::max(1, frames);
      return p;
   }

   double BinHz(int n) { return kSr / n; }

   double PeakHz(const std::vector<double>& p, int n, double loHz, double hiHz)
   {
      const size_t lo = (size_t)std::max(1.0, loHz / BinHz(n)), hi = std::min(p.size() - 2, (size_t)(hiHz / BinHz(n)));
      size_t best = lo;
      for (size_t k = lo; k <= hi; k++) if (p[k] > p[best]) best = k;
      const double y0 = std::log(p[best - 1] + 1e-30), y1 = std::log(p[best] + 1e-30), y2 = std::log(p[best + 1] + 1e-30);
      const double den = y0 - 2.0 * y1 + y2;
      const double d = (std::fabs(den) > 1e-12) ? 0.5 * (y0 - y2) / den : 0.0;
      return ((double)best + d) * BinHz(n);
   }

   double BandPower(const std::vector<double>& p, int n, double loHz, double hiHz)
   {
      double s = 0.0;
      for (size_t k = 0; k < p.size(); k++)
      {
         const double f = k * BinHz(n);
         if (f >= loHz && f < hiHz) s += p[k];
      }
      return s;
   }

   double Rms(const Signal& x, size_t start, size_t len)
   {
      double s = 0.0;
      for (size_t i = start; i < start + len && i < x.size(); i++) s += (double)x[i] * x[i];
      return std::sqrt(s / std::max<size_t>(1, len));
   }

   double Db(double v) { return 20.0 * std::log10(std::max(v, 1e-30)); }

   // ---------------------------------------------------------- rig
   struct Rig
   {
      SpectralFieldNode node;
      int frame = 0;
      AudioNode* an = nullptr;

      bool Load(const std::string& code, int sizeIndex = 2)
      {
         node.code = code;
         node.sizeIndex = sizeIndex;
         if (!node.Apply())
         {
            printf("  compile failed: %s\n", node.LastError().c_str());
            return false;
         }
         an = node.GetAudioNode();
         node.CookIfNeeded(frame++); // size reaches the audio node before Prepare
         an->PrepareToPlay(kSr, kBlock);
         return true;
      }

      bool Preset(const char* name, int sizeIndex = 2)
      {
         for (const auto& p : SpectralFieldNode::Presets())
            if (std::string(p.name) == name)
               return Load(p.code, sizeIndex);
         printf("  no preset '%s'\n", name);
         return false;
      }

      void Set(const char* param, float v)
      {
         if (Field::ParamEntry* e = node.GetParamTable().Find(param)) e->value = v;
         else printf("  no param '%s'\n", param);
      }

      // Renders `in` (and optional bin input `b`), `channels` wide. perBlock
      // runs before each block with the block's first sample index.
      Signal Render(const Signal& in, const Signal* b = nullptr, int channels = 1,
                    const std::function<void(int)>& perBlock = nullptr)
      {
         Signal out(in.size(), 0.0f);
         std::vector<float> inCh[2], bCh[2], outCh[2];
         for (int c = 0; c < 2; c++) { inCh[c].assign(kBlock, 0.0f); bCh[c].assign(kBlock, 0.0f); outCh[c].assign(kBlock, 0.0f); }
         float* inPtr[2] = { inCh[0].data(), inCh[1].data() };
         float* bPtr[2] = { bCh[0].data(), bCh[1].data() };
         float* outPtr[2] = { outCh[0].data(), outCh[1].data() };
         AudioBuffer inBuf, bBuf, outBuf;
         inBuf.channels = inPtr; inBuf.numChannels = channels;
         bBuf.channels = bPtr; bBuf.numChannels = channels;
         outBuf.channels = outPtr; outBuf.numChannels = channels;
         const AudioBuffer* inputs[2] = { &inBuf, &bBuf };
         for (size_t pos = 0; pos < in.size(); pos += kBlock)
         {
            const int n = (int)std::min<size_t>(kBlock, in.size() - pos);
            inBuf.numFrames = bBuf.numFrames = outBuf.numFrames = n;
            if (perBlock) perBlock((int)pos);
            node.CookIfNeeded(frame++);
            for (int c = 0; c < channels; c++)
               for (int i = 0; i < n; i++)
               {
                  inCh[c][(size_t)i] = in[pos + (size_t)i];
                  bCh[c][(size_t)i] = b ? (*b)[pos + (size_t)i] : 0.0f;
               }
            an->ProcessBlock(inputs, b ? 2 : 1, outBuf);
            for (int i = 0; i < n; i++) out[pos + (size_t)i] = outCh[0][(size_t)i];
         }
         return out;
      }
   };

   // Error of out against in delayed by `lat`, in dB re in's rms, skipping the first `skip`.
   double DelayedErrorDb(const Signal& in, const Signal& out, int lat, size_t skip)
   {
      double e = 0.0, r = 0.0;
      for (size_t i = skip; i < out.size(); i++)
      {
         const double d = out[i] - in[i - (size_t)lat];
         e += d * d;
         r += (double)in[i - (size_t)lat] * in[i - (size_t)lat];
      }
      return 10.0 * std::log10(std::max(e, 1e-300) / std::max(r, 1e-300));
   }

   int gFails = 0;
   void Report(const char* name, bool ok, const char* fmt, ...)
   {
      char buf[512];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof(buf), fmt, ap);
      va_end(ap);
      printf("  %-22s %s  %s\n", name, ok ? "OK  " : "FAIL", buf);
      if (!ok) gFails++;
   }
}

namespace app
{
int RunSpectralFieldTest()
{
   printf("[SPECTRALFIELDTEST] Spectral Field, 48 kHz, block %d\n", kBlock);
   const int sec = (int)kSr;

   // 0 / 0b: identity, raw and vocoder, at every size end.
   {
      const Signal in = Noise(2 * sec, 0.25f);
      const int sizes[3] = { 0, 2, 4 };
      for (int si : sizes)
      {
         for (int mode = 0; mode < 2; mode++)
         {
            Rig rig;
            if (!rig.Load(mode == 0 ? "mag = mag\nphase = phase\n" : "mag = mag\n", si)) { gFails++; continue; }
            const int lat = rig.an->LatencySamples();
            const Signal out = rig.Render(in);
            const double err = DelayedErrorDb(in, out, lat, (size_t)lat + 8192);
            char name[64];
            snprintf(name, sizeof(name), "0%s identity %d", mode == 0 ? "" : "b", 512 << si);
            Report(name, err < -90.0 && lat == (512 << si), "error %.1f dB, latency %d", err, lat);
         }
      }
   }

   // 1: low-pass at 2 kHz on white noise.
   {
      Rig rig;
      if (rig.Preset("Low-pass"))
      {
         rig.Set("cutoff", 2000.0f);
         const Signal in = Noise(3 * sec, 0.25f);
         const Signal out = rig.Render(in);
         const int lat = rig.an->LatencySamples();
         const int n = 16384;
         const auto pin = Power(in, (size_t)sec - (size_t)lat, (size_t)(2 * sec) - 16384, n);
         const auto pout = Power(out, (size_t)sec, (size_t)(2 * sec) - 16384, n);
         const double rel = 10.0 * std::log10(BandPower(pout, n, 2500.0, kSr / 2) / BandPower(pin, n, 2500.0, kSr / 2));
         Report("1 low-pass", rel < -60.0, ">=2.5k %.1f dB re input", rel);
      }
      else gFails++;
   }

   // 2: gate at -40 dB, 1 kHz sine at -6 dB plus noise at -60 dB.
   {
      Rig rig;
      if (rig.Preset("Gate"))
      {
         rig.Set("thresh", -40.0f);
         const Signal sine = Sine(3 * sec, 1000.0, 0.5f);
         Signal in = Noise(3 * sec, 0.001f * std::sqrt(3.0f), 99u); // uniform rms = amp / sqrt(3)
         for (size_t i = 0; i < in.size(); i++) in[i] += sine[i];
         const Signal out = rig.Render(in);
         const int lat = rig.an->LatencySamples();
         Signal resid(out.size(), 0.0f);
         for (size_t i = (size_t)lat; i < out.size(); i++) resid[i] = out[i] - sine[i - (size_t)lat];
         const double r = Db(Rms(resid, (size_t)sec, (size_t)sec));
         // Diagnostic split: the same gate on the sine alone. out - sineOnly
         // is the noise that leaked; sineOnly - sine is the gate cutting the
         // sine's own window skirt.
         Rig clean;
         clean.Preset("Gate");
         clean.Set("thresh", -40.0f);
         const Signal sineOnly = clean.Render(sine);
         Signal leak(out.size(), 0.0f), skirt(out.size(), 0.0f);
         for (size_t i = (size_t)lat; i < out.size(); i++)
         {
            leak[i] = out[i] - sineOnly[i];
            skirt[i] = sineOnly[i] - sine[i - (size_t)lat];
         }
         const double leakDb = Db(Rms(leak, (size_t)sec, (size_t)sec));
         Report("2 gate", leakDb < -80.0, "noise leak %.1f dBFS (noise in %.1f); sine skirt trimmed %.1f, total residual %.1f",
                leakDb, Db(0.001), Db(Rms(skirt, (size_t)sec, (size_t)sec)), r);
      }
      else gFails++;
   }

   // 3: freeze a 440 Hz saw at 0.5 s, input stops at 1 s.
   {
      Rig rig;
      if (rig.Preset("Freeze"))
      {
         Signal in = Saw((int)(3.3 * sec), 440.0, 0.3f);
         for (size_t i = (size_t)sec; i < in.size(); i++) in[i] = 0.0f;
         rig.Set("hold", 0.0f);
         const Signal out = rig.Render(in, nullptr, 1, [&](int pos) { if (pos >= sec / 2) rig.Set("hold", 1.0f); });
         const int lat = rig.an->LatencySamples();
         double worst = 0.0, prev = 0.0;
         int frames = 0;
         for (size_t s = (size_t)sec + (size_t)lat; s + 2048 <= (size_t)(3 * sec) + (size_t)lat; s += 512, frames++)
         {
            const double db = Db(Rms(out, s, 2048));
            if (frames > 0) worst = std::max(worst, std::fabs(db - prev));
            prev = db;
         }
         const int n = 16384;
         const double pk = PeakHz(Power(out, (size_t)(2 * sec), (size_t)sec, n), n, 200.0, 1000.0);
         Report("3 freeze", worst <= 0.5 && std::fabs(pk - 440.0) < kSr / 2048 && prev > -60.0,
                "frame-to-frame max %.3f dB over %d frames, level %.1f dB, pitch %.1f Hz", worst, frames, prev, pk);
      }
      else gFails++;
   }

   // 4: octave up, 440 Hz sine.
   {
      Rig rig;
      if (rig.Preset("Octave up"))
      {
         const Signal in = Sine(3 * sec, 440.0, 0.5f);
         const Signal out = rig.Render(in);
         const int n = 16384;
         const auto p = Power(out, (size_t)sec, (size_t)sec, n);
         const double pk = PeakHz(p, n, 100.0, 4000.0);
         const double down = 10.0 * std::log10(BandPower(p, n, 420.0, 460.0) / BandPower(p, n, 860.0, 900.0));
         Report("4 octave up", std::fabs(pk - 880.0) < kSr / 2048 && down < -40.0, "peak %.1f Hz, 440 Hz at %.1f dB", pk, down);
      }
      else gFails++;
   }

   // 5: frequency shift +200 Hz, 1 kHz sine.
   {
      Rig rig;
      if (rig.Preset("Freq shift"))
      {
         rig.Set("shift", 200.0f);
         const Signal in = Sine(3 * sec, 1000.0, 0.5f);
         const Signal out = rig.Render(in);
         const int n = 16384;
         const double pk = PeakHz(Power(out, (size_t)sec, (size_t)sec, n), n, 100.0, 4000.0);
         Report("5 freq shift", std::fabs(pk - 1200.0) < kSr / 2048, "peak %.1f Hz", pk);
      }
      else gFails++;
   }

   // 6: flip, pink noise low-passed at 4 kHz.
   {
      Rig rig;
      if (rig.Preset("Flip"))
      {
         const Signal in = PinkLowpassed(3 * sec);
         const Signal out = rig.Render(in);
         const int lat = rig.an->LatencySamples();
         const int n = 8192;
         auto centroid = [&](const std::vector<double>& p) {
            double s = 0.0, w = 0.0;
            for (size_t k = 0; k < p.size(); k++) { s += p[k] * k * BinHz(n); w += p[k]; }
            return s / w;
         };
         const double cin = centroid(Power(in, (size_t)sec - (size_t)lat, (size_t)sec, n));
         const double cout = centroid(Power(out, (size_t)sec, (size_t)sec, n));
         const double want = kSr / 2 - cin;
         Report("6 flip", std::fabs(cout - want) <= 0.05 * want, "centroid in %.0f, out %.0f, want %.0f Hz", cin, cout, want);
      }
      else gFails++;
   }

   // 7: robot (autocorrelation at lag hop) and whisper (flatness).
   {
      const Signal in = Saw(3 * sec, 220.0, 0.3f);
      Rig robot;
      Rig whisper;
      if (robot.Preset("Robot / whisper") && whisper.Preset("Robot / whisper"))
      {
         robot.Set("whisper", 0.0f);
         const Signal ro = robot.Render(in);
         const int hop = 2048 / 4;
         double xy = 0, xx = 0, yy = 0;
         for (size_t i = (size_t)sec; i < (size_t)(2 * sec); i++)
         {
            xy += (double)ro[i] * ro[i + (size_t)hop];
            xx += (double)ro[i] * ro[i];
            yy += (double)ro[i + (size_t)hop] * ro[i + (size_t)hop];
         }
         const double ac = xy / std::sqrt(xx * yy + 1e-300);

         whisper.Set("whisper", 1.0f);
         const Signal wo = whisper.Render(in);
         const int n = 8192;
         auto flatness = [&](const std::vector<double>& p) {
            double lg = 0.0, ar = 0.0;
            int cnt = 0;
            for (size_t k = 0; k < p.size(); k++)
            {
               const double f = k * BinHz(n);
               if (f < 20.0 || f > 20000.0) continue;
               lg += std::log(p[k] + 1e-30);
               ar += p[k];
               cnt++;
            }
            return std::exp(lg / cnt) / (ar / cnt);
         };
         const double fin = flatness(Power(in, (size_t)sec, (size_t)sec, n));
         const double fout = flatness(Power(wo, (size_t)sec, (size_t)sec, n));
         Report("7 robot", ac > 0.8, "autocorr at lag %d = %.3f", hop, ac);
         Report("7 whisper", fout >= 3.0 * fin, "flatness %.2e -> %.2e (x%.1f)", fin, fout, fout / fin);
      }
      else gFails += 2;
   }

   // 8: cross-synthesis, in = noise with a 4 Hz envelope, B = 220 Hz saw.
   {
      Rig rig;
      if (rig.Preset("Cross-synth"))
      {
         Signal in = Noise(3 * sec, 0.5f, 4242u);
         for (size_t i = 0; i < in.size(); i++) in[i] *= (float)(0.5 + 0.5 * std::sin(2.0 * kPi * 4.0 * i / kSr));
         const Signal b = Saw(3 * sec, 220.0, 0.3f);
         const Signal out = rig.Render(in, &b);
         const int lat = rig.an->LatencySamples();
         const size_t win = 480; // 10 ms
         std::vector<double> ei, eo;
         for (size_t s = (size_t)sec; s + win <= (size_t)(2 * sec); s += win)
         {
            ei.push_back(Rms(in, s - (size_t)lat, win));
            eo.push_back(Rms(out, s, win));
         }
         double mi = 0, mo = 0;
         for (size_t i = 0; i < ei.size(); i++) { mi += ei[i]; mo += eo[i]; }
         mi /= ei.size(); mo /= eo.size();
         double sxy = 0, sxx = 0, syy = 0;
         for (size_t i = 0; i < ei.size(); i++)
         {
            sxy += (ei[i] - mi) * (eo[i] - mo);
            sxx += (ei[i] - mi) * (ei[i] - mi);
            syy += (eo[i] - mo) * (eo[i] - mo);
         }
         const double r = sxy / std::sqrt(sxx * syy + 1e-300);
         // Pitch: strongest normalised autocorrelation lag for 150..400 Hz.
         int bestLag = 0;
         double best = -2.0;
         for (int lag = (int)(kSr / 400); lag <= (int)(kSr / 150); lag++)
         {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = (size_t)sec; i < (size_t)(2 * sec); i++)
            {
               xy += (double)out[i] * out[i + (size_t)lag];
               xx += (double)out[i] * out[i];
               yy += (double)out[i + (size_t)lag] * out[i + (size_t)lag];
            }
            const double c = xy / std::sqrt(xx * yy + 1e-300);
            if (c > best) { best = c; bestLag = lag; }
         }
         const double pitch = kSr / bestLag;
         Report("8 cross-synth", r > 0.9 && std::fabs(pitch - 220.0) <= 0.03 * 220.0,
                "envelope r %.3f, pitch %.1f Hz (ac %.2f)", r, pitch, best);
      }
      else gFails++;
   }

   // Mix 0 is the dry signal delayed by the latency.
   {
      Rig rig;
      if (rig.Preset("Octave up"))
      {
         rig.node.mix = 0.0f;
         const Signal in = Noise(sec, 0.25f);
         const Signal out = rig.Render(in);
         const int lat = rig.an->LatencySamples();
         const double err = DelayedErrorDb(in, out, lat, (size_t)lat + 4096);
         Report("mix 0 = dry", err < -120.0, "error %.1f dB", err);
      }
      else gFails++;
   }

   // Size change re-reports latency.
   {
      Rig rig;
      if (rig.Preset("Low-pass"))
      {
         const Signal in = Noise(kBlock * 4, 0.25f);
         rig.Render(in);
         const int before = rig.an->LatencySamples();
         rig.node.sizeIndex = 4;
         rig.Render(in);
         const int after = rig.an->LatencySamples();
         Report("size -> latency", before == 2048 && after == 8192, "%d -> %d", before, after);
      }
      else gFails++;
   }

   // A bad compile mid-render leaves the sound unchanged.
   {
      Rig a, b;
      if (a.Preset("Low-pass") && b.Preset("Low-pass"))
      {
         const Signal in = Noise(sec, 0.25f);
         bool refused = false;
         const Signal oa = a.Render(in, nullptr, 1, [&](int pos) {
            if (pos == sec / 2 - (sec / 2) % kBlock) { a.node.code = "mag = (\n"; refused = !a.node.Apply(); }
         });
         const Signal ob = b.Render(in);
         double diff = 0.0;
         for (size_t i = 0; i < oa.size(); i++) diff = std::max(diff, (double)std::fabs(oa[i] - ob[i]));
         Report("bad compile", refused && diff == 0.0 && !a.node.LastError().empty(), "refused %d, max diff %g", (int)refused, diff);
      }
      else gFails++;
   }

   // No allocation in steady-state ProcessBlock (stereo, bin input, state).
   {
      bool ok = true;
      for (const char* name : { "Cross-synth", "Freeze", "Freq shift" })
      {
         Rig rig;
         if (!rig.Preset(name)) { ok = false; continue; }
         const Signal in = Noise(kBlock * 16, 0.25f);
         const Signal b = Saw(kBlock * 16, 220.0, 0.3f);
         rig.Render(in, &b, 2); // warm up
#if defined(__APPLE__)
         malloc_statistics_t before{}, after{};
         malloc_zone_statistics(malloc_default_zone(), &before);
         std::vector<float> l(kBlock), r(kBlock), bl(kBlock), br(kBlock), ol(kBlock), orr(kBlock);
         float* ip[2] = { l.data(), r.data() };
         float* bp[2] = { bl.data(), br.data() };
         float* op[2] = { ol.data(), orr.data() };
         AudioBuffer ib, bb, ob;
         ib.channels = ip; bb.channels = bp; ob.channels = op;
         ib.numChannels = bb.numChannels = ob.numChannels = 2;
         ib.numFrames = bb.numFrames = ob.numFrames = kBlock;
         const AudioBuffer* inputs[2] = { &ib, &bb };
         malloc_zone_statistics(malloc_default_zone(), &before);
         for (int i = 0; i < 200; i++)
         {
            rig.node.CookIfNeeded(rig.frame++);
            rig.an->ProcessBlock(inputs, 2, ob);
         }
         malloc_zone_statistics(malloc_default_zone(), &after);
         const long grew = (long)after.size_allocated - (long)before.size_allocated;
         if (grew != 0) { printf("  %s: size_allocated grew %ld bytes\n", name, grew); ok = false; }
#endif
      }
      Report("no allocation", ok, "200 stereo blocks x 3 presets");
   }

   // CPU: us per stereo block for every preset at 2048 and 8192.
   {
      printf("  CPU, stereo, us per %d-sample block (%.2f ms budget):\n", kBlock, 1000.0 * kBlock / kSr);
      double lowpass2048 = 0.0;
      const Signal in = Noise(4 * sec, 0.25f);
      const Signal b = Saw(4 * sec, 220.0, 0.3f);
      for (const auto& p : SpectralFieldNode::Presets())
      {
         double us[2] = {};
         for (int s = 0; s < 2; s++)
         {
            Rig rig;
            if (!rig.Load(p.code, s == 0 ? 2 : 4)) { gFails++; continue; }
            rig.Render(Signal(in.begin(), in.begin() + sec / 2), &b, 2); // warm up
            const auto t0 = std::chrono::steady_clock::now();
            rig.Render(in, &b, 2);
            const auto t1 = std::chrono::steady_clock::now();
            us[s] = std::chrono::duration<double, std::micro>(t1 - t0).count() / (double)(in.size() / kBlock);
         }
         printf("    %-18s 2048: %7.1f us (%.2f%%)   8192: %7.1f us (%.2f%%)\n", p.name,
                us[0], 100.0 * us[0] / (1e6 * kBlock / kSr), us[1], 100.0 * us[1] / (1e6 * kBlock / kSr));
         if (std::string(p.name) == "Low-pass") lowpass2048 = us[0];
      }
      const double pct = 100.0 * lowpass2048 / (1e6 * kBlock / kSr);
#if defined(NDEBUG)
      Report("cpu low-pass 2048", pct < 3.0, "%.2f%% of one core, stereo", pct);
#else
      printf("  cpu low-pass 2048       skip  %.2f%% of one core, stereo (debug build: the 3%% bar applies to optimised builds)\n", pct);
#endif
   }

   printf("INFINITE_SPECTRALFIELDTEST: %s (%d failure%s)\n", gFails == 0 ? "OK" : "FAIL", gFails, gFails == 1 ? "" : "s");
   return gFails == 0 ? 0 : 1;
}
} // namespace app

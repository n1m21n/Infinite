#include "AudioSummary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace AudioSummary
{
   namespace
   {
      const double kPi = 3.14159265358979323846;
      const double kNaN = std::numeric_limits<double>::quiet_NaN();

      constexpr int kFftLog2 = 12;
      constexpr int kFftSize = 1 << kFftLog2; // 85 ms at 48 kHz
      constexpr int kHop = kFftSize / 4;
      constexpr int kSpecBins = kFftSize / 2 + 1;

      constexpr int kOversample = 4;
      constexpr int kPhaseTaps = 12;

      struct Biquad
      {
         double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
         double z1 = 0.0, z2 = 0.0;
         double Run(double x)
         {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
         }
      };

      // BS.1770 K-weighting, re-derived for any sample rate from the analog
      // prototypes behind the standard's 48 kHz coefficient table: a +4 dB
      // high shelf at 1682 Hz followed by a 38 Hz high-pass.
      void KWeighting(double fs, Biquad& shelf, Biquad& highpass)
      {
         {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / fs);
            const double Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
         }
         {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highpass.b0 = 1.0;
            highpass.b1 = -2.0;
            highpass.b2 = 1.0;
            highpass.a1 = 2.0 * (K * K - 1.0) / a0;
            highpass.a2 = (1.0 - K / Q + K * K) / a0;
         }
      }

      double BesselI0(double x)
      {
         double sum = 1.0, term = 1.0;
         for (int k = 1; k < 40; k++)
         {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
         }
         return sum;
      }

      // In-place radix-2 FFT, double precision. Runs a few thousand times
      // per minute of audio, offline - no need for anything faster.
      void Fft(double* re, double* im, int log2N, const std::vector<double>& cosT, const std::vector<double>& sinT)
      {
         const int n = 1 << log2N;
         for (int i = 1, j = 0; i < n; i++)
         {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
               j ^= bit;
            j ^= bit;
            if (i < j)
            {
               std::swap(re[i], re[j]);
               std::swap(im[i], im[j]);
            }
         }
         for (int len = 2; len <= n; len <<= 1)
         {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
               for (int k = 0; k < len / 2; k++)
               {
                  const double wr = cosT[(size_t)k * step], wi = sinT[(size_t)k * step];
                  const int a = i + k, b = i + k + len / 2;
                  const double tr = re[b] * wr - im[b] * wi;
                  const double ti = re[b] * wi + im[b] * wr;
                  re[b] = re[a] - tr;
                  im[b] = im[a] - ti;
                  re[a] += tr;
                  im[a] += ti;
               }
         }
      }

      double Lufs(double weightedMeanSquare)
      {
         return weightedMeanSquare > 1e-12 ? -0.691 + 10.0 * std::log10(weightedMeanSquare) : kNaN;
      }

      std::string Num(double v, const char* fmt = "%.2f")
      {
         if (!std::isfinite(v))
            return "null";
         char buf[48];
         std::snprintf(buf, sizeof(buf), fmt, v);
         // "-0.00" reads as a sign error to anything diffing two summaries.
         double back = 0.0;
         if (std::sscanf(buf, "%lf", &back) == 1 && back == 0.0)
            return "0";
         return buf;
      }
   }

   struct Analyzer::State
   {
      double fs = 48000.0;
      long long frames = 0;

      // Per channel
      Biquad shelf[2], highpass[2];
      double sum[2] = { 0.0, 0.0 };
      double sumSq[2] = { 0.0, 0.0 };
      double sumLR = 0.0;
      double peak = 0.0;
      double truePeak = 0.0;
      long long clipped = 0;

      // 100 ms sub-blocks: K-weighted mean square summed over channels (the
      // gating blocks are four of these) and the louder channel's raw mean
      // square (silence ratio).
      int subLen = 4800;
      int subCount = 0;
      double subK = 0.0;
      double subRaw[2] = { 0.0, 0.0 };
      std::vector<double> subKs;
      std::vector<double> subRaws;
      double totalK = 0.0;

      // True peak: polyphase windowed-sinc interpolator, 12 taps per phase.
      double taps[kOversample][kPhaseTaps];
      double hist[2][kPhaseTaps] = {};
      int histPos = 0;

      // Spectrum and flux: Hann frames of kFftSize every kHop samples.
      std::vector<double> window, cosT, sinT;
      std::vector<float> ring[2]; // last kFftSize samples per channel
      int ringPos = 0;
      int sinceHop = 0;
      long long ringFilled = 0;
      double windowPower = 0.0;
      std::vector<double> binPower; // accumulated, mean of L and R
      long long specFrames = 0;
      std::vector<double> prevLogMag;
      std::vector<float> flux;
      std::vector<double> re, im, framePower;

      void SpectrumFrame()
      {
         std::fill(framePower.begin(), framePower.end(), 0.0);
         for (int ch = 0; ch < 2; ch++)
         {
            for (int i = 0; i < kFftSize; i++)
            {
               re[i] = (double)ring[ch][(size_t)((ringPos + i) & (kFftSize - 1))] * window[i];
               im[i] = 0.0;
            }
            Fft(re.data(), im.data(), kFftLog2, cosT, sinT);
            for (int k = 0; k < kSpecBins; k++)
               framePower[k] += 0.5 * (re[k] * re[k] + im[k] * im[k]);
         }
         // Scaled so the bins of a sine of amplitude A sum to A^2 / 2.
         const double scale = 2.0 / ((double)kFftSize * windowPower);
         double f = 0.0;
         for (int k = 0; k < kSpecBins; k++)
         {
            const double p = framePower[k] * scale;
            binPower[k] += p;
            const double logMag = std::log1p(1000.0 * std::sqrt(p));
            if (logMag > prevLogMag[k])
               f += logMag - prevLogMag[k];
            prevLogMag[k] = logMag;
         }
         flux.push_back((float)(f / (double)kSpecBins));
         specFrames++;
      }
   };

   Analyzer::Analyzer(double sampleRate)
      : mState(new State())
   {
      State& s = *mState;
      s.fs = sampleRate > 0.0 ? sampleRate : 48000.0;
      for (int ch = 0; ch < 2; ch++)
         KWeighting(s.fs, s.shelf[ch], s.highpass[ch]);
      s.subLen = std::max(1, (int)std::llround(s.fs * 0.1));

      // Kaiser-windowed sinc, cutoff at the original Nyquist, centred between
      // taps so all four phases land between samples (the sample itself is
      // already covered by the sample peak).
      const int total = kOversample * kPhaseTaps;
      const double centre = (double)(total - 1) * 0.5, beta = 8.0;
      for (int p = 0; p < kOversample; p++)
      {
         double gain = 0.0;
         for (int k = 0; k < kPhaseTaps; k++)
         {
            const int n = kOversample * k + p;
            const double x = ((double)n - centre) / (double)kOversample;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double r = ((double)n - centre) / centre;
            const double w = BesselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / BesselI0(beta);
            s.taps[p][k] = sinc * w;
            gain += s.taps[p][k];
         }
         for (int k = 0; k < kPhaseTaps; k++)
            s.taps[p][k] /= gain;
      }

      s.window.resize(kFftSize);
      for (int i = 0; i < kFftSize; i++)
      {
         s.window[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * (double)i / (double)kFftSize));
         s.windowPower += s.window[i] * s.window[i];
      }
      s.cosT.resize(kFftSize / 2);
      s.sinT.resize(kFftSize / 2);
      for (int i = 0; i < kFftSize / 2; i++)
      {
         s.cosT[i] = std::cos(-2.0 * kPi * (double)i / (double)kFftSize);
         s.sinT[i] = std::sin(-2.0 * kPi * (double)i / (double)kFftSize);
      }
      s.ring[0].assign(kFftSize, 0.0f);
      s.ring[1].assign(kFftSize, 0.0f);
      s.binPower.assign(kSpecBins, 0.0);
      s.prevLogMag.assign(kSpecBins, 0.0);
      s.framePower.assign(kSpecBins, 0.0);
      s.re.assign(kFftSize, 0.0);
      s.im.assign(kFftSize, 0.0);
   }

   Analyzer::~Analyzer()
   {
      delete mState;
   }

   void Analyzer::Push(const float* left, const float* right, int frames)
   {
      State& s = *mState;
      for (int i = 0; i < frames; i++)
      {
         const double x[2] = { (double)left[i], (double)right[i] };
         bool clip = false;
         for (int ch = 0; ch < 2; ch++)
         {
            const double v = x[ch];
            const double a = std::abs(v);
            s.sum[ch] += v;
            s.sumSq[ch] += v * v;
            s.subRaw[ch] += v * v;
            s.peak = std::max(s.peak, a);
            clip = clip || a >= 1.0;

            const double k = s.highpass[ch].Run(s.shelf[ch].Run(v));
            s.subK += k * k;

            s.hist[ch][s.histPos] = v;
            for (int p = 0; p < kOversample; p++)
            {
               double y = 0.0;
               for (int t = 0; t < kPhaseTaps; t++)
                  y += s.taps[p][t] * s.hist[ch][(s.histPos - t + kPhaseTaps) % kPhaseTaps];
               s.truePeak = std::max(s.truePeak, std::abs(y));
            }
            s.ring[ch][(size_t)s.ringPos] = (float)v;
         }
         s.histPos = (s.histPos + 1) % kPhaseTaps;
         s.ringPos = (s.ringPos + 1) & (kFftSize - 1);
         s.sumLR += x[0] * x[1];
         if (clip)
            s.clipped++;
         s.frames++;

         if (++s.subCount == s.subLen)
         {
            s.subKs.push_back(s.subK / (double)s.subLen);
            s.subRaws.push_back(std::max(s.subRaw[0], s.subRaw[1]) / (double)s.subLen);
            s.totalK += s.subK;
            s.subK = 0.0;
            s.subRaw[0] = s.subRaw[1] = 0.0;
            s.subCount = 0;
         }

         s.ringFilled++;
         if (++s.sinceHop >= kHop && s.ringFilled >= kFftSize)
         {
            s.sinceHop = 0;
            s.SpectrumFrame();
         }
      }
   }

   Result Analyzer::Finish()
   {
      State& s = *mState;
      Result r;
      r.sampleRate = s.fs;
      r.frames = s.frames;
      r.seconds = (double)s.frames / s.fs;
      r.integratedLufs = kNaN;
      r.correlation = kNaN;
      r.bpmEstimate = kNaN;
      if (s.frames == 0)
      {
         r.silenceRatio = 1.0;
         return r;
      }

      const double n = (double)s.frames;
      for (int ch = 0; ch < 2; ch++)
      {
         r.rms[ch] = std::sqrt(s.sumSq[ch] / n);
         r.dcOffset[ch] = s.sum[ch] / n;
      }
      r.samplePeak = s.peak;
      r.truePeak = std::max(s.truePeak, s.peak);
      r.clippedFrames = s.clipped;
      r.clippedPercent = 100.0 * (double)s.clipped / n;
      if (s.sumSq[0] > 1e-18 && s.sumSq[1] > 1e-18)
         r.correlation = std::clamp(s.sumLR / std::sqrt(s.sumSq[0] * s.sumSq[1]), -1.0, 1.0);

      // A signal shorter than one 100 ms block still gets an answer, from
      // whatever arrived.
      if (s.subKs.empty() && s.subCount > 0)
      {
         s.subKs.push_back(s.subK / (double)s.subCount);
         s.subRaws.push_back(std::max(s.subRaw[0], s.subRaw[1]) / (double)s.subCount);
      }

      // Integrated loudness: 400 ms blocks at 75 % overlap, absolute gate at
      // -70 LUFS, then a relative gate 10 LU under the mean of what is left.
      {
         std::vector<double> blocks;
         if (s.subKs.size() >= 4)
            for (size_t j = 0; j + 4 <= s.subKs.size(); j++)
               blocks.push_back(0.25 * (s.subKs[j] + s.subKs[j + 1] + s.subKs[j + 2] + s.subKs[j + 3]));
         else
         {
            double m = 0.0;
            for (double v : s.subKs)
               m += v;
            blocks.push_back(m / (double)s.subKs.size());
         }
         const double absGate = std::pow(10.0, (-70.0 + 0.691) / 10.0);
         double sum = 0.0;
         int count = 0;
         for (double z : blocks)
            if (z > absGate)
            {
               sum += z;
               count++;
            }
         if (count > 0)
         {
            const double relGate = (sum / (double)count) * 0.1;
            double gated = 0.0;
            int gatedCount = 0;
            for (double z : blocks)
               if (z > absGate && z > relGate)
               {
                  gated += z;
                  gatedCount++;
               }
            if (gatedCount > 0)
               r.integratedLufs = Lufs(gated / (double)gatedCount);
         }
      }

      for (size_t sec = 0; sec * 10 < s.subKs.size(); sec++)
      {
         double m = 0.0;
         int c = 0;
         for (size_t j = sec * 10; j < std::min(s.subKs.size(), sec * 10 + 10); j++, c++)
            m += s.subKs[j];
         r.loudnessPerSecond.push_back(Lufs(m / (double)c));
      }

      {
         int silent = 0;
         for (double v : s.subRaws)
            if (v < 1e-6) // -60 dBFS RMS
               silent++;
         r.silenceRatio = (double)silent / (double)s.subRaws.size();
      }

      // Anything shorter than one FFT frame: analyse what is in the ring,
      // zero-padded.
      if (s.specFrames == 0)
         s.SpectrumFrame();

      const double nyquist = s.fs * 0.5;
      const double lo = 20.0, hi = std::min(20000.0, nyquist);
      const double df = s.fs / (double)kFftSize;
      double best = 1e-24; // nothing louder than this: no loudest band
      r.loudestBand = -1;
      for (int b = 0; b < kBands; b++)
      {
         const double e0 = lo * std::pow(hi / lo, (double)b / (double)kBands);
         const double e1 = lo * std::pow(hi / lo, (double)(b + 1) / (double)kBands);
         r.bandHz[b] = std::sqrt(e0 * e1);
         // Each bin is a df-wide slice of the axis; a band takes the share
         // of every bin it overlaps, so narrow low bands and wide high ones
         // add up to the same total.
         double p = 0.0;
         const int k0 = std::max(0, (int)std::floor(e0 / df - 0.5));
         const int k1 = std::min(kSpecBins - 1, (int)std::ceil(e1 / df + 0.5));
         for (int k = k0; k <= k1; k++)
         {
            const double b0 = ((double)k - 0.5) * df, b1 = ((double)k + 0.5) * df;
            const double overlap = std::min(b1, e1) - std::max(b0, e0);
            if (overlap > 0.0)
               p += s.binPower[(size_t)k] * overlap / df;
         }
         r.bandPower[b] = p / (double)s.specFrames;
         if (r.bandPower[b] > best)
         {
            best = r.bandPower[b];
            r.loudestBand = b;
         }
      }

      // Onsets: peaks of the spectral flux that stand clear of their
      // neighbourhood. The floor is relative to the biggest jump in the
      // signal, so a steady tone or noise bed reports its start and nothing
      // after it.
      {
         const std::vector<float>& f = s.flux;
         const int count = (int)f.size();
         float peak = 0.0f;
         for (float v : f)
            peak = std::max(peak, v);
         const float floor = std::max(1e-4f, 0.1f * peak);
         const double frameRate = s.fs / (double)kHop;
         const int reach = std::max(1, (int)std::lround(0.06 * frameRate)); // +-60 ms: local maximum
         const int span = std::max(2, (int)std::lround(0.2 * frameRate));   // +-200 ms: local mean
         int last = -1000000;
         for (int t = 0; t < count; t++)
         {
            if (f[t] < floor)
               continue;
            bool isPeak = true;
            for (int u = std::max(0, t - reach); u <= std::min(count - 1, t + reach); u++)
               if (f[u] > f[t] || (f[u] == f[t] && u < t))
                  isPeak = false;
            if (!isPeak)
               continue;
            double mean = 0.0;
            int c = 0;
            for (int u = std::max(0, t - span); u <= std::min(count - 1, t + span); u++, c++)
               mean += f[u];
            mean /= (double)c;
            if ((double)f[t] < 1.5 * mean || t - last <= reach)
               continue;
            r.onsetCount++;
            last = t;
         }

         // Tempo: the lag at which the flux best repeats itself, searched
         // over 50..220 BPM with a mild preference for the middle of the
         // range (a 120 BPM pattern repeats just as well at 60 and 240).
         if (r.onsetCount >= 4 && count > 8)
         {
            double mean = 0.0;
            for (float v : f)
               mean += v;
            mean /= (double)count;
            auto ac = [&](int lag)
            {
               double a = 0.0;
               for (int t = 0; t + lag < count; t++)
                  a += ((double)f[t] - mean) * ((double)f[t + lag] - mean);
               return a / (double)std::max(1, count - lag);
            };
            const double ac0 = ac(0);
            const int lagMin = std::max(1, (int)std::floor(frameRate * 60.0 / 220.0));
            const int lagMax = std::min(count / 2, (int)std::ceil(frameRate * 60.0 / 50.0));
            double bestScore = 0.0;
            int bestLag = 0;
            std::vector<double> acs((size_t)std::max(0, lagMax + 2), 0.0);
            for (int lag = std::max(1, lagMin - 1); lag <= lagMax + 1 && lag < count; lag++)
               acs[(size_t)lag] = ac(lag);
            for (int lag = lagMin; lag <= lagMax; lag++)
            {
               const double a = acs[(size_t)lag];
               if (a <= acs[(size_t)lag - 1] || a < acs[(size_t)lag + 1])
                  continue; // not a local maximum
               const double bpm = 60.0 * frameRate / (double)lag;
               const double octaves = std::log2(bpm / 120.0);
               const double score = a * std::exp(-0.5 * octaves * octaves / (0.9 * 0.9));
               if (score > bestScore)
               {
                  bestScore = score;
                  bestLag = lag;
               }
            }
            if (bestLag > 0 && ac0 > 0.0)
            {
               // Parabola through the peak and its neighbours: the flux is
               // sampled every ~21 ms, far coarser than a tempo needs.
               const double y0 = acs[(size_t)bestLag - 1], y1 = acs[(size_t)bestLag], y2 = acs[(size_t)bestLag + 1];
               const double denom = y0 - 2.0 * y1 + y2;
               const double shift = std::abs(denom) > 1e-18 ? std::clamp(0.5 * (y0 - y2) / denom, -0.5, 0.5) : 0.0;
               r.bpmConfidence = std::clamp(y1 / ac0, 0.0, 1.0);
               if (r.bpmConfidence >= 0.1)
                  r.bpmEstimate = 60.0 * frameRate / ((double)bestLag + shift);
            }
         }
      }
      return r;
   }

   double Db(double linear)
   {
      return linear > 1e-12 ? 20.0 * std::log10(linear) : kNaN;
   }

   std::string ToJson(const Result& r, bool full)
   {
      std::string s = "{";
      s += "\"sample_rate\":" + Num(r.sampleRate, "%.0f");
      s += ",\"frames\":" + std::to_string(r.frames);
      s += ",\"seconds\":" + Num(r.seconds, "%.3f");
      s += ",\"integrated_lufs\":" + Num(r.integratedLufs);
      s += ",\"true_peak_dbtp\":" + Num(Db(r.truePeak));
      s += ",\"sample_peak_dbfs\":" + Num(Db(r.samplePeak));
      s += ",\"sample_peak\":" + Num(r.samplePeak, "%.6f");
      s += ",\"rms_dbfs\":[" + Num(Db(r.rms[0])) + "," + Num(Db(r.rms[1])) + "]";
      s += ",\"dc_offset\":[" + Num(r.dcOffset[0], "%.6f") + "," + Num(r.dcOffset[1], "%.6f") + "]";
      s += ",\"clipped_frames\":" + std::to_string(r.clippedFrames);
      s += ",\"clipped_percent\":" + Num(r.clippedPercent, "%.4f");
      s += ",\"silence_ratio\":" + Num(r.silenceRatio, "%.4f");
      s += ",\"stereo_correlation\":" + Num(r.correlation, "%.3f");
      s += ",\"onset_count_estimate\":" + std::to_string(r.onsetCount);
      s += ",\"bpm_estimate\":" + Num(r.bpmEstimate, "%.1f");
      s += ",\"bpm_confidence\":" + Num(r.bpmConfidence, "%.2f");
      s += ",\"loudest_band_hz\":" + Num(r.loudestBand >= 0 ? r.bandHz[r.loudestBand] : kNaN, "%.0f");
      if (full)
      {
         s += ",\"spectrum\":{\"band_hz\":[";
         for (int b = 0; b < kBands; b++)
            s += (b ? "," : "") + Num(r.bandHz[b], "%.1f");
         s += "],\"mean_db\":[";
         for (int b = 0; b < kBands; b++)
            s += (b ? "," : "") + Num(r.bandPower[b] > 1e-24 ? 10.0 * std::log10(r.bandPower[b]) : kNaN);
         s += "]}";
         s += ",\"loudness_per_second\":[";
         for (size_t i = 0; i < r.loudnessPerSecond.size(); i++)
            s += (i ? "," : "") + Num(r.loudnessPerSecond[i]);
         s += "]";
      }
      s += "}";
      return s;
   }

   bool SelfTest(std::string& report)
   {
      bool allOk = true;
      auto check = [&](const char* name, bool ok, double got)
      {
         char line[160];
         std::snprintf(line, sizeof(line), "[%s] %s (%.3f)\n", ok ? "pass" : "FAIL", name, got);
         report += line;
         allOk = allOk && ok;
      };
      auto run = [](double fs, const std::vector<float>& l, const std::vector<float>& r)
      {
         Analyzer a(fs);
         // Odd block size on purpose: nothing may depend on the caller's blocks.
         for (size_t pos = 0; pos < l.size(); pos += 509)
            a.Push(l.data() + pos, r.data() + pos, (int)std::min<size_t>(509, l.size() - pos));
         return a.Finish();
      };

      for (double fs : { 48000.0, 44100.0 })
      {
         // 1 kHz at -20 dBFS peak on both channels: -23 dB RMS per channel,
         // +3 dB for two channels, K-weighting is ~0 dB at 1 kHz.
         std::vector<float> x((size_t)(fs * 5.0));
         for (size_t i = 0; i < x.size(); i++)
            x[i] = 0.1f * (float)std::sin(2.0 * kPi * 1000.0 * (double)i / fs);
         const Result r = run(fs, x, x);
         check("sine: -20 LUFS", std::abs(r.integratedLufs + 20.0) < 0.1, r.integratedLufs);
         check("sine: true peak -20 dBTP", std::abs(Db(r.truePeak) + 20.0) < 0.1, Db(r.truePeak));
         check("sine: rms -23 dBFS", std::abs(Db(r.rms[0]) + 23.01) < 0.05, Db(r.rms[0]));
         const int loud = std::max(0, r.loudestBand);
         check("sine: 1 kHz is the loudest band", r.loudestBand >= 0 && r.bandHz[loud] > 890.0 && r.bandHz[loud] < 1120.0,
               r.bandHz[loud]);
         check("sine: band reads -23 dB", std::abs(10.0 * std::log10(r.bandPower[loud]) + 23.01) < 0.5,
               10.0 * std::log10(r.bandPower[loud]));
         check("sine: correlation 1", r.correlation > 0.999, r.correlation);
         check("sine: no clipping", r.clippedFrames == 0, (double)r.clippedFrames);
         check("sine: no silence", r.silenceRatio == 0.0, r.silenceRatio);
         check("sine: no dc", std::abs(r.dcOffset[0]) < 1e-4, r.dcOffset[0]);
         check("sine: five seconds of loudness", r.loudnessPerSecond.size() == 5 &&
                                                    std::abs(r.loudnessPerSecond[2] + 20.0) < 0.1,
               (double)r.loudnessPerSecond.size());
         check("sine: at most its own start as an onset", r.onsetCount <= 1, (double)r.onsetCount);
      }

      {
         // fs/4 at 45 degrees: every sample sits at 0.707 of the real peak.
         const double fs = 48000.0;
         std::vector<float> x((size_t)fs);
         for (size_t i = 0; i < x.size(); i++)
            x[i] = (float)std::sin(kPi * 0.5 * (double)i + kPi * 0.25);
         const Result r = run(fs, x, x);
         check("inter-sample peak: sample peak -3 dBFS", std::abs(Db(r.samplePeak) + 3.01) < 0.05, Db(r.samplePeak));
         check("inter-sample peak: true peak ~0 dBTP", std::abs(Db(r.truePeak)) < 0.3, Db(r.truePeak));
      }

      {
         // Silence, then a clipped, offset, out-of-phase second half.
         const double fs = 48000.0;
         std::vector<float> l((size_t)(fs * 2.0), 0.0f), rr(l.size(), 0.0f);
         for (size_t i = l.size() / 2; i < l.size(); i++)
         {
            const double v = 1.5 * std::sin(2.0 * kPi * 220.0 * (double)i / fs);
            l[i] = (float)v;
            rr[i] = (float)-v;
         }
         const Result r = run(fs, l, rr);
         check("clip: over full scale is counted", r.clippedPercent > 20.0 && r.clippedPercent < 30.0, r.clippedPercent);
         check("clip: sample peak +3.5 dBFS", std::abs(Db(r.samplePeak) - 3.52) < 0.05, Db(r.samplePeak));
         check("clip: half silent", std::abs(r.silenceRatio - 0.5) < 0.06, r.silenceRatio);
         check("clip: correlation -1", r.correlation < -0.999, r.correlation);
      }

      {
         const double fs = 48000.0;
         std::vector<float> x((size_t)(fs * 1.0), 0.0f);
         const Result r = run(fs, x, x);
         check("silence: no loudness", !std::isfinite(r.integratedLufs), 0.0);
         check("silence: ratio 1", r.silenceRatio == 1.0, r.silenceRatio);
         check("silence: no loudest band", r.loudestBand < 0, (double)r.loudestBand);
         check("silence: json says null", ToJson(r, true).find("\"integrated_lufs\":null") != std::string::npos, 0.0);
      }

      {
         // 120 BPM: a 10 ms noise burst every half second for ten seconds.
         const double fs = 48000.0;
         std::vector<float> x((size_t)(fs * 10.0), 0.0f);
         unsigned seed = 12345u;
         for (int beat = 0; beat < 20; beat++)
            for (int i = 0; i < 480; i++)
            {
               seed = seed * 1664525u + 1013904223u;
               const float noise = (float)((seed >> 8) & 0xffff) / 32768.0f - 1.0f;
               x[(size_t)(beat * 24000 + 2400 + i)] = 0.5f * noise * (1.0f - (float)i / 480.0f);
            }
         const Result r = run(fs, x, x);
         check("clicks: twenty onsets", r.onsetCount >= 19 && r.onsetCount <= 21, (double)r.onsetCount);
         check("clicks: 120 bpm", std::abs(r.bpmEstimate - 120.0) < 2.0, r.bpmEstimate);
      }
      return allOk;
   }
}

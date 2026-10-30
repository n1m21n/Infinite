#include "app/graph/RoleCritic.h"

#include "app/graph/RoleCriticWeights.inc"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <numeric>

namespace RoleCritic
{
namespace
{
   constexpr int kBands = 32, kEnv = 40;
   constexpr double kPi = 3.14159265358979323846; // M_PI is not in MSVC's <cmath> by default
   using cd = std::complex<double>;

   // In-place radix-2 FFT (size a power of two); inverse leaves the 1/N to the caller.
   void Fft(std::vector<cd>& a, bool inverse)
   {
      const size_t n = a.size();
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
         const double ang = 2.0 * kPi / (double)len * (inverse ? 1.0 : -1.0);
         const cd wl(std::cos(ang), std::sin(ang));
         for (size_t i = 0; i < n; i += len)
         {
            cd w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; k++)
            {
               const cd u = a[i + k], v = a[i + k + len / 2] * w;
               a[i + k] = u + v;
               a[i + k + len / 2] = u - v;
               w *= wl;
            }
         }
      }
   }

   // numpy's default (linear) percentile.
   double Percentile(std::vector<double> v, double q)
   {
      if (v.empty())
         return 0.0;
      std::sort(v.begin(), v.end());
      const double at = q / 100.0 * (double)(v.size() - 1);
      const size_t lo = (size_t)std::floor(at), hi = std::min(v.size() - 1, lo + 1);
      return v[lo] + (v[hi] - v[lo]) * (at - (double)lo);
   }

   double Median(const std::vector<double>& v) { return Percentile(v, 50.0); }

   double Mean(const std::vector<double>& v)
   {
      return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / (double)v.size();
   }

   double Std(const std::vector<double>& v)
   {
      if (v.empty())
         return 0.0;
      const double m = Mean(v);
      double s = 0.0;
      for (double x : v)
         s += (x - m) * (x - m);
      return std::sqrt(s / (double)v.size());
   }

   // frame_features._notes on the active frames' power spectra.
   void Notes(const std::vector<std::vector<double>>& pw, const std::vector<double>& freqs, double rate, double* out)
   {
      std::fill(out, out + 8, 0.0);
      const size_t nf = pw.size(), nb = freqs.size();
      if (nf < 2)
         return;
      std::vector<int> pcOf(nb, -1);
      for (size_t k = 0; k < nb; k++)
         if (freqs[k] >= 55.0 && freqs[k] <= 4000.0)
            pcOf[k] = (int)(((long)std::nearbyint(12.0 * std::log2(freqs[k] / 440.0)) % 12 + 12) % 12);
      std::vector<std::array<double, 12>> C(nf);
      for (size_t t = 0; t < nf; t++)
      {
         const std::vector<double>& p = pw[t];
         const double top = *std::max_element(p.begin(), p.end());
         std::array<double, 12> c {};
         for (size_t k = 1; k + 1 < nb; k++)
            if (pcOf[k] >= 0 && p[k] > p[k - 1] && p[k] >= p[k + 1] && p[k] > 1e-3 * top)
               c[pcOf[k]] += p[k];
         const double m = *std::max_element(c.begin(), c.end()) + 1e-20;
         for (double& x : c)
            x /= m;
         C[t] = c;
      }
      if (nf >= 3)
      {
         std::vector<std::array<double, 12>> S(nf);
         for (size_t t = 0; t < nf; t++)
            for (int j = 0; j < 12; j++)
               S[t][j] = (C[t][j] + C[t == 0 ? 0 : t - 1][j] + C[t + 1 < nf ? t + 1 : nf - 1][j]) / 3.0;
         C = S;
      }
      std::vector<double> poly(nf);
      std::vector<int> top(nf);
      for (size_t t = 0; t < nf; t++)
      {
         const double m = *std::max_element(C[t].begin(), C[t].end()) + 1e-20;
         for (double& x : C[t])
            x /= m;
         top[t] = (int)(std::max_element(C[t].begin(), C[t].end()) - C[t].begin());
         poly[t] = (double)std::count_if(C[t].begin(), C[t].end(), [](double x) { return x > 0.3; });
      }
      std::vector<double> runs;
      int r = 1;
      for (size_t i = 1; i < nf; i++)
      {
         if (top[i] == top[i - 1] || C[i][top[i - 1]] > 0.6)
            r++;
         else
         {
            runs.push_back(r);
            r = 1;
         }
      }
      runs.push_back(r);
      std::vector<double> steady;
      for (size_t i = 1; i < nf; i++)
      {
         double d = 0.0, a = 0.0, b = 0.0;
         for (int j = 0; j < 12; j++)
         {
            d += C[i][j] * C[i - 1][j];
            a += C[i][j] * C[i][j];
            b += C[i - 1][j] * C[i - 1][j];
         }
         steady.push_back(d / ((std::sqrt(a) + 1e-12) * (std::sqrt(b) + 1e-12)));
      }
      std::array<double, 12> p {};
      for (size_t t = 0; t < nf; t++)
         for (int j = 0; j < 12; j++)
            p[j] += C[t][j] / (double)nf;
      const double ps = std::accumulate(p.begin(), p.end(), 0.0);
      double ent = 0.0;
      for (double x : p)
         ent -= (x / ps) * std::log(x / ps + 1e-12);
      bool used[12] = {};
      for (int t : top)
         used[t] = true;
      out[0] = Mean(poly);
      out[1] = Percentile(poly, 90);
      out[2] = Mean(runs) / rate;
      out[3] = Percentile(runs, 90) / rate;
      out[4] = Mean(steady);
      out[5] = Std(steady);
      out[6] = ent / std::log(12.0);
      out[7] = (double)std::count(used, used + 12, true) / 12.0;
   }
}

std::vector<double> Measure(const float* L, const float* R, int frames, double fsIn)
{
   const int fs = (int)std::lround(fsIn);
   const int fft = fs > 30000 ? 2048 : 1024, hop = fft / 4, nb = fft / 2 + 1;
   const double df = (double)fs / fft;
   // 32 log bands 20 Hz - 20 kHz, each bin weighted by its overlap with the band.
   std::vector<std::vector<std::pair<int, double>>> bm(kBands);
   {
      const double lo = 20.0, hi = std::min(20000.0, fs / 2.0);
      for (int b = 0; b < kBands; b++)
      {
         const double e0 = lo * std::pow(hi / lo, (double)b / kBands), e1 = lo * std::pow(hi / lo, (double)(b + 1) / kBands);
         for (int k = std::max(0, (int)std::floor(e0 / df - 0.5)); k <= std::min(fft / 2, (int)std::ceil(e1 / df + 0.5)); k++)
         {
            const double ov = std::min((k + 0.5) * df, e1) - std::max((k - 0.5) * df, e0);
            if (ov > 0)
               bm[b].push_back({ k, ov / df });
         }
      }
   }
   std::vector<double> win(fft), freqs(nb);
   for (int i = 0; i < fft; i++)
      win[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / fft));
   for (int k = 0; k < nb; k++)
      freqs[k] = k * df;

   std::vector<double> m(std::max(frames, fft * 2), 0.0);
   for (int i = 0; i < frames; i++)
      m[i] = 0.5 * ((double)L[i] + (double)R[i]);
   const int n = (int)m.size();

   std::vector<std::vector<double>> fr, pw;
   std::vector<cd> buf(fft);
   for (int s = 0; s + fft <= n; s += hop)
   {
      std::vector<double> x(fft);
      for (int i = 0; i < fft; i++)
         buf[i] = x[i] = m[s + i] * win[i];
      Fft(buf, false);
      std::vector<double> p(nb);
      for (int k = 0; k < nb; k++)
         p[k] = std::norm(buf[k]) + 1e-20;
      fr.push_back(std::move(x));
      pw.push_back(std::move(p));
   }
   const int T = (int)pw.size();
   std::vector<std::array<double, kBands>> shape(T);
   std::vector<double> loud(T), cen(T), flat(T), tf(T, 0.0), flux(T, 0.0);
   for (int t = 0; t < T; t++)
   {
      const std::vector<double>& p = pw[t];
      double sum = 0.0, wsum = 0.0, lsum = 0.0, rest = 0.0;
      for (int k = 0; k < nb; k++)
      {
         sum += p[k];
         wsum += p[k] * freqs[k];
         if (k)
         {
            lsum += std::log(p[k]);
            rest += p[k];
         }
      }
      loud[t] = std::log10(sum);
      cen[t] = std::log2(wsum / sum + 1.0);
      flat[t] = std::exp(lsum / (nb - 1)) / (rest / (nb - 1));
      double B[kBands], tot = 0.0;
      for (int b = 0; b < kBands; b++)
      {
         double e = 0.0;
         for (const auto& [k, w] : bm[b])
            e += p[k] * w;
         B[b] = std::log10(e + 1e-20);
         tot += std::pow(10.0, B[b]);
      }
      for (int b = 0; b < kBands; b++)
         shape[t][b] = B[b] - std::log10(tot);
      if (t)
      {
         double d = 0.0, f = 0.0;
         for (int b = 0; b < kBands; b++)
            d += (shape[t][b] - shape[t - 1][b]) * (shape[t][b] - shape[t - 1][b]);
         tf[t] = std::sqrt(d);
         for (int k = 0; k < nb; k++)
            f += std::max(0.0, std::log1p(1000.0 * std::sqrt(p[k])) - std::log1p(1000.0 * std::sqrt(pw[t - 1][k])));
         flux[t] = f / nb;
      }
   }
   const double lmax = *std::max_element(loud.begin(), loud.end());
   std::vector<char> act(T);
   int nAct = 0;
   for (int t = 0; t < T; t++)
      nAct += act[t] = loud[t] > lmax - 4.0;
   if (nAct < 2)
      std::fill(act.begin(), act.end(), 1), nAct = T;

   double shMean[kBands] = {}, shSd[kBands] = {}, wTot = 0.0;
   for (int t = 0; t < T; t++)
      if (act[t])
      {
         const double w = std::pow(10.0, loud[t] - lmax);
         wTot += w;
         for (int b = 0; b < kBands; b++)
            shMean[b] += w * shape[t][b];
      }
   for (int b = 0; b < kBands; b++)
      shMean[b] /= wTot;
   for (int t = 0; t < T; t++)
      if (act[t])
      {
         const double w = std::pow(10.0, loud[t] - lmax);
         for (int b = 0; b < kBands; b++)
            shSd[b] += w * (shape[t][b] - shMean[b]) * (shape[t][b] - shMean[b]);
      }
   for (int b = 0; b < kBands; b++)
      shSd[b] = std::sqrt(shSd[b] / wTot);

   // Onsets: flux peaks over 15% of the largest, the largest within 50 ms, 1.5x the local mean.
   const double rate = (double)fs / hop;
   const int reach = std::max(1, (int)std::nearbyint(0.05 * rate));
   const double fl = std::max(1e-4, 0.15 * *std::max_element(flux.begin(), flux.end()));
   std::vector<int> on0, on;
   for (int t = 1; t < T; t++)
   {
      if (flux[t] < fl)
         continue;
      const int a = std::max(0, t - reach), z = std::min(T, t + reach + 1);
      if (flux[t] != *std::max_element(flux.begin() + a, flux.begin() + z))
         continue;
      const int a4 = std::max(0, t - 4 * reach), z4 = std::min(T, t + 4 * reach + 1);
      if (flux[t] >= 1.5 * std::accumulate(flux.begin() + a4, flux.begin() + z4, 0.0) / (z4 - a4))
         on0.push_back(t);
   }
   for (size_t i = 0; i < on0.size(); i++)
      if (i == 0 || on0[i] - on0[i - 1] > reach)
         on.push_back(on0[i]);
   const double dur = (double)n / fs;

   // 10 ms RMS envelope; the median curve after each onset, normalised to its own peak.
   const int hs = fs / 100, ne = n / hs;
   std::vector<double> e(ne);
   for (int i = 0; i < ne; i++)
   {
      double s = 0.0;
      for (int j = 0; j < hs; j++)
         s += m[(size_t)i * hs + j] * m[(size_t)i * hs + j];
      e[i] = std::sqrt(s / hs + 1e-20);
   }
   std::vector<std::array<double, kEnv>> curves;
   std::vector<double> att, dec, sus;
   std::vector<int> starts = on.empty() ? std::vector<int> { 0 } : on;
   for (int t : starts)
   {
      const int s = (int)((double)t * hop / hs);
      const int segN = std::max(0, std::min(ne, s + kEnv + 20) - s);
      if (segN < 4)
         continue;
      const int pk = (int)(std::max_element(e.begin() + s, e.begin() + s + std::min(15, segN)) - (e.begin() + s));
      const double peak = e[s + pk];
      std::array<double, kEnv> c;
      for (int i = 0; i < kEnv; i++)
         c[i] = std::log10((pk + i < segN ? e[s + pk + i] / peak : 0.0) + 1e-4);
      curves.push_back(c);
      att.push_back(pk * 10.0);
      int below = -1;
      for (int i = s + pk; i < ne; i++)
         if (e[i] < peak * 0.1)
         {
            below = i - (s + pk);
            break;
         }
      dec.push_back(below >= 0 ? std::min(2000.0, 10.0 * below) : 2000.0);
      sus.push_back(s + pk + 20 < ne ? e[s + pk + 20] / peak : 0.0);
   }
   std::vector<double> env(kEnv, -4.0);
   if (!curves.empty())
      for (int i = 0; i < kEnv; i++)
      {
         std::vector<double> col;
         for (const auto& c : curves)
            col.push_back(c[i]);
         env[i] = Median(col);
      }

   // Pitchedness: the normalised autocorrelation peak (lags 30 Hz - 2 kHz) on the 16 loudest frames.
   std::vector<int> order(T);
   std::iota(order.begin(), order.end(), 0);
   std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return loud[a] < loud[b]; });
   std::vector<double> hv;
   std::vector<cd> ab(2 * fft);
   for (int i = std::max(0, T - 16); i < T; i++)
   {
      const std::vector<double>& x = fr[order[i]];
      std::fill(ab.begin(), ab.end(), cd(0.0, 0.0));
      for (int j = 0; j < fft; j++)
         ab[j] = x[j];
      Fft(ab, false);
      for (cd& v : ab)
         v = std::norm(v);
      Fft(ab, true);
      const double a0 = ab[0].real();
      if (a0 <= 0)
         continue;
      const int lo = fs / 2000, hi = std::min(fft - 1, fs / 30);
      double best = -1e300;
      for (int j = lo; j < hi; j++)
         best = std::max(best, ab[j].real());
      hv.push_back(best / a0);
   }

   std::vector<double> ioi;
   if (on.size() > 2)
      for (size_t i = 1; i < on.size(); i++)
         ioi.push_back((on[i] - on[i - 1]) / rate);
   else
      ioi.push_back(dur);
   double pkAbs = 0.0, ms = 0.0, ll = 0.0, rr = 0.0, lr = 0.0;
   for (int i = 0; i < frames; i++)
   {
      pkAbs = std::max({ pkAbs, (double)std::fabs(L[i]), (double)std::fabs(R[i]) });
      ll += (double)L[i] * L[i];
      rr += (double)R[i] * R[i];
      lr += (double)L[i] * R[i];
   }
   for (double v : m)
      ms += v * v;
   const double rms = std::sqrt(ms / n) + 1e-12;
   const double den = std::sqrt(ll * rr), corr = den > 0 ? lr / den : 1.0;
   double tot = 0.0, low = 0.0, mid = 0.0, high = 0.0;
   for (int b = 0; b < kBands; b++)
   {
      const double v = std::pow(10.0, shMean[b]);
      tot += v;
      (b < 11 ? low : b < 22 ? mid : high) += v;
   }
   auto actOnly = [&](const std::vector<double>& v) {
      std::vector<double> o;
      for (int t = 0; t < T; t++)
         if (act[t])
            o.push_back(v[t]);
      return o;
   };
   const std::vector<double> loudA = actOnly(loud), cenA = actOnly(cen), flatA = actOnly(flat), tfA = actOnly(tf);
   const double scalars[20] = { on.size() / dur, att.empty() ? 0 : Median(att), dec.empty() ? 0 : Median(dec), sus.empty() ? 0 : Median(sus),
                                (double)nAct / T, Std(loudA), hv.empty() ? 0 : Median(hv), Mean(cenA), Std(cenA), Mean(flatA), Std(flatA), Mean(tfA),
                                Mean(flux), Std(flux), Std(ioi) / (Mean(ioi) + 1e-9), std::log10(pkAbs / rms + 1e-9), corr,
                                low / tot, mid / tot, high / tot };
   std::vector<std::vector<double>> pwAct;
   for (int t = 0; t < T; t++)
      if (act[t])
         pwAct.push_back(pw[t]);
   double notes[8];
   Notes(pwAct, freqs, rate, notes);

   std::vector<double> v;
   v.reserve(kFeatures);
   v.insert(v.end(), shMean, shMean + kBands);
   v.insert(v.end(), shSd, shSd + kBands);
   v.insert(v.end(), env.begin(), env.end());
   v.insert(v.end(), scalars, scalars + 20);
   v.insert(v.end(), notes, notes + 8);
   for (double& x : v)
      if (!std::isfinite(x))
         x = 0.0;
   return v;
}

int RoleIndex(const char* partName)
{
   for (int i = 0; i < RoleCriticData::kRoles; i++)
      if (std::strcmp(partName, RoleCriticData::kRoleNames[i]) == 0)
         return i;
   return -1;
}

std::vector<float> Probabilities(const std::vector<double>& x)
{
   using namespace RoleCriticData;
   double z[kIn], h1[kH1], h2[kH2], o[kRoles];
   for (int i = 0; i < kIn; i++)
      z[i] = (x[i] - kMean[i]) / kScale[i];
   for (int j = 0; j < kH1; j++)
   {
      double s = kB1[j];
      for (int i = 0; i < kIn; i++)
         s += z[i] * kW1[i * kH1 + j];
      h1[j] = std::max(0.0, s);
   }
   for (int j = 0; j < kH2; j++)
   {
      double s = kB2[j];
      for (int i = 0; i < kH1; i++)
         s += h1[i] * kW2[i * kH2 + j];
      h2[j] = std::max(0.0, s);
   }
   double top = -1e300;
   for (int j = 0; j < kRoles; j++)
   {
      double s = kB3[j];
      for (int i = 0; i < kH2; i++)
         s += h2[i] * kW3[i * kRoles + j];
      top = std::max(top, o[j] = s);
   }
   double sum = 0.0;
   for (double& v : o)
      sum += v = std::exp(v - top);
   std::vector<float> p(kRoles);
   for (int j = 0; j < kRoles; j++)
      p[j] = (float)(o[j] / sum);
   return p;
}

bool Accept(const char* partName, const float* l, const float* r, int frames, double fs, float* p)
{
   const int role = RoleIndex(partName);
   if (p)
      *p = -1.0f;
   if (role < 0 || frames <= 0)
      return true;
   const float pr = Probabilities(Measure(l, r, frames, fs))[role];
   if (p)
      *p = pr;
   return pr >= std::max(RoleCriticData::kCut[role], 0.5f); // better than a coin toss, or it is redrawn
}
}

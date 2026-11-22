#include "AliasAnalysis.h"
#include "SampleRuntime.h"

#include <algorithm>
#include <cmath>
#include <complex>

// See AliasAnalysis.h for the method. Main-thread only; allocates freely.
namespace Field
{
   namespace
   {
      using cd = std::complex<double>;

      // In-place radix-2 complex FFT; inverse is unscaled.
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
            const double ang = 2.0 * M_PI / (double)len * (inverse ? 1.0 : -1.0);
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

      // Signed harmonic number of FFT bin b on an n-point period grid.
      inline int Harm(size_t b, size_t n) { return b <= n / 2 ? (int)b : (int)b - (int)n; }

      // x(theta - phi per harmonic): exact delay of the grid's band-limited
      // interpolant. phi = 2 pi f0 / fs is one sample at fs.
      void DelayPeriodic(const std::vector<double>& x, double phi, std::vector<double>& out)
      {
         const size_t n = x.size();
         std::vector<cd> a(n);
         for (size_t i = 0; i < n; i++)
            a[i] = x[i];
         Fft(a, false);
         for (size_t b = 0; b < n; b++)
         {
            if (b == n / 2)
            {
               a[b] *= std::cos(phi * (double)(n / 2));
               continue;
            }
            const double h = (double)Harm(b, n);
            a[b] *= cd(std::cos(h * phi), -std::sin(h * phi));
         }
         Fft(a, true);
         out.resize(n);
         for (size_t i = 0; i < n; i++)
            out[i] = a[i].real() / (double)n;
      }

      bool Unsupported(const SampleProgram& prog, std::string& why)
      {
         if (!prog.tables.empty())
            return why = "state tables", true;
         for (const SampleInstr& ins : prog.code)
            switch (ins.op)
            {
               case SampleOp::LoadN: return why = "sample counter n", true;
               case SampleOp::Rand: return why = "rand()", true;
               case SampleOp::Tick:
               case SampleOp::LoadBeat: return why = "beat clock", true;
               case SampleOp::LoadBrain: return why = "brain input", true;
               case SampleOp::EmitNote: return why = "note emit", true;
               case SampleOp::LoadDeclaredIn: return why = "declared audio inputs (more than one source)", true;
               case SampleOp::LoadBin:
               case SampleOp::LoadBins:
               case SampleOp::LoadBinHz:
               case SampleOp::LoadHop:
               case SampleOp::BinAt: return why = "spectral program", true;
               default: break;
            }
         return false;
      }

      // One program run per grid point, the way FieldSampleNode runs it
      // (params at defaults, freq/gate 0, output NaN->0 and clamped).
      struct Runner
      {
         const SampleProgram& prog;
         float params[kSampleMaxParams] = {};
         float regs[kSampleMaxRegs] = {};
         float cur[kSampleMaxStateCells] = {};
         float next[kSampleMaxStateCells] = {};
         float sr = 48000.0f;
         long long evals = 0;

         explicit Runner(const SampleProgram& p) : prog(p)
         {
            for (size_t i = 0; i < p.params.size() && i < (size_t)kSampleMaxParams; i++)
               params[i] = p.params[i].defaultValue;
         }

         const AliasContext* ctx = nullptr;

         // Grid point j; stateDelayed: K values; nextOut: K values. Returns output.
         double Run(size_t j, float in, const double* stateDelayed, double* nextOut)
         {
            const int k = (int)prog.state.size();
            for (int i = 0; i < k; i++)
               cur[i] = next[i] = (float)stateDelayed[i];
            if (ctx != nullptr)
               for (size_t i = 0; i < ctx->inputRegs.size(); i++)
                  regs[ctx->inputRegs[i]] = (float)ctx->inputWaves[i][j];
            SampleRuntimeInput rin;
            rin.in = in;
            rin.sr = sr;
            rin.paramVals = params;
            rin.stateCur = cur;
            rin.stateNext = next;
            float y = RunSampleProgram(prog, rin, regs);
            evals++;
            for (int i = 0; i < k; i++)
               nextOut[i] = next[i];
            if (std::isnan(y) || std::isinf(y))
               y = 0.0f;
            return std::clamp(y, -4.0f, 4.0f); // FieldSampleNode's kOutClamp
         }
      };

      double Rms(const std::vector<double>& v)
      {
         double s = 0.0;
         for (double x : v)
            s += x * x;
         return v.empty() ? 0.0 : std::sqrt(s / (double)v.size());
      }

      // Distance on the unit circle of phase values (accumulators wrap).
      double WrapDist(double a, double b)
      {
         double d = std::fabs(a - b);
         d -= std::floor(d);
         return std::min(d, 1.0 - d);
      }

      // Spectral radius estimate of a small real matrix: ||M^64||^(1/64).
      double SpectralRadius(std::vector<double> m, int k)
      {
         if (k == 0)
            return 0.0;
         double logScale = 0.0;
         for (int sq = 0; sq < 6; sq++)
         {
            std::vector<double> r((size_t)(k * k), 0.0);
            for (int i = 0; i < k; i++)
               for (int j = 0; j < k; j++)
                  for (int l = 0; l < k; l++)
                     r[(size_t)(i * k + j)] += m[(size_t)(i * k + l)] * m[(size_t)(l * k + j)];
            double mx = 0.0;
            for (double v : r)
               mx = std::max(mx, std::fabs(v));
            logScale *= 2.0;
            if (mx > 0.0)
            {
               for (double& v : r)
                  v /= mx;
               logScale += std::log(mx);
            }
            else
               return 0.0;
            m = r;
         }
         double norm = 0.0;
         for (int i = 0; i < k; i++)
         {
            double row = 0.0;
            for (int j = 0; j < k; j++)
               row += std::fabs(m[(size_t)(i * k + j)]);
            norm = std::max(norm, row);
         }
         return std::exp((logScale + std::log(std::max(norm, 1e-300))) / 64.0);
      }

      // Solve (I - M D) x = r for a k x k system in place, D = diag(rot): column
      // l is shifted by its own cell's lag, e^{-i h lag_l phi} (phase 5).
      bool SolveShifted(const std::vector<double>& m, int k, const std::vector<cd>& rot, std::vector<cd>& x)
      {
         std::vector<cd> a((size_t)(k * k));
         for (int i = 0; i < k; i++)
            for (int j = 0; j < k; j++)
               a[(size_t)(i * k + j)] = (i == j ? cd(1.0, 0.0) : cd(0.0, 0.0)) - m[(size_t)(i * k + j)] * rot[(size_t)j];
         for (int c = 0; c < k; c++)
         {
            int piv = c;
            for (int r = c + 1; r < k; r++)
               if (std::abs(a[(size_t)(r * k + c)]) > std::abs(a[(size_t)(piv * k + c)]))
                  piv = r;
            if (std::abs(a[(size_t)(piv * k + c)]) < 1e-9)
               return false;
            if (piv != c)
            {
               for (int j = 0; j < k; j++)
                  std::swap(a[(size_t)(c * k + j)], a[(size_t)(piv * k + j)]);
               std::swap(x[(size_t)c], x[(size_t)piv]);
            }
            for (int r = c + 1; r < k; r++)
            {
               const cd f = a[(size_t)(r * k + c)] / a[(size_t)(c * k + c)];
               for (int j = c; j < k; j++)
                  a[(size_t)(r * k + j)] -= f * a[(size_t)(c * k + j)];
               x[(size_t)r] -= f * x[(size_t)c];
            }
         }
         for (int c = k - 1; c >= 0; c--)
         {
            for (int j = c + 1; j < k; j++)
               x[(size_t)c] -= a[(size_t)(c * k + j)] * x[(size_t)j];
            x[(size_t)c] /= a[(size_t)(c * k + c)];
         }
         return true;
      }
   }

   namespace
   {
      // Phase 5 (README section 15): a delay line is one more periodic cell with
      // lag N. Each Delay op becomes StoreState(cell, src) then LoadState(dst,
      // cell); the store writes next and the load reads the delayed value, so
      // dst == src is safe. lag gets 1 per state cell and N per delay line.
      bool LowerDelays(const SampleProgram& prog, SampleProgram& low, std::vector<int>& lag, std::string& why)
      {
         const int k0 = (int)prog.state.size();
         if (k0 + (int)prog.delays.size() > kSampleMaxStateCells)
            return why = "too many state cells and delay lines for the analysis", false;
         low = prog;
         low.delays.clear();
         lag.assign((size_t)k0, 1);
         for (const SampleDelayLine& dl : prog.delays)
         {
            SampleStateInit init;
            init.name = "__delay";
            low.state.push_back(init);
            lag.push_back(std::max(1, dl.length));
         }
         low.code.clear();
         for (const SampleInstr& ins : prog.code)
         {
            if (ins.op != SampleOp::Delay)
            {
               low.code.push_back(ins);
               continue;
            }
            if ((int)ins.a >= (int)prog.delays.size())
               return why = "bad delay line index", false;
            SampleInstr st;
            st.op = SampleOp::StoreState;
            st.a = (uint8_t)(k0 + ins.a);
            st.b = ins.b;
            SampleInstr ld;
            ld.op = SampleOp::LoadState;
            ld.dst = ins.dst;
            ld.a = (uint8_t)(k0 + ins.a);
            low.code.push_back(st);
            low.code.push_back(ld);
         }
         return true;
      }

      bool AnalyzeCells(const SampleProgram& prog, const std::vector<int>& lag, const AliasContext& ctx,
                        double sampleRate, AliasSpectrum& out);
   }

   bool AnalyzePeriodicSpectrum(const SampleProgram& prog, const AliasContext& ctx, double sampleRate,
                                AliasSpectrum& out)
   {
      out = AliasSpectrum();
      out.sampleRate = sampleRate;
      if (!prog.valid)
         return out.status = "program not compiled", false;
      if (Unsupported(prog, out.status))
         return false;
      if (prog.delays.empty())
         return AnalyzeCells(prog, std::vector<int>((size_t)prog.state.size(), 1), ctx, sampleRate, out);
      SampleProgram low;
      std::vector<int> lag;
      if (!LowerDelays(prog, low, lag, out.status))
         return false;
      return AnalyzeCells(low, lag, ctx, sampleRate, out);
   }

   namespace
   {
   bool AnalyzeCells(const SampleProgram& prog, const std::vector<int>& lag, const AliasContext& ctx,
                     double sampleRate, AliasSpectrum& out)
   {

      const size_t n = (size_t)1 << ctx.gridLog2;
      const int k = (int)prog.state.size();
      const double phi = 2.0 * M_PI * ctx.f0Hz / sampleRate; // one sample, in radians of f0
      const double cyclesPerSample = ctx.f0Hz / sampleRate;

      Runner run(prog);
      run.sr = (float)sampleRate;
      run.ctx = &ctx;
      for (size_t i = 0; i < ctx.inputRegs.size(); i++)
         if (ctx.inputRegs[i] < 0 || ctx.inputRegs[i] >= kSampleMaxRegs || ctx.inputWaves.size() <= i ||
             ctx.inputWaves[i].size() != ((size_t)1 << ctx.gridLog2))
            return out.status = "bad segment input", false;
      if (!ctx.inWave.empty() && ctx.inWave.size() != n)
         return out.status = "bad input wave", false;
      out.captured.assign(ctx.captureRegs.size(), std::vector<double>((size_t)1 << ctx.gridLog2));

      std::vector<float> in(n);
      for (size_t j = 0; j < n; j++)
         in[j] = !ctx.usesInput ? 0.0f
                 : !ctx.inWave.empty() ? (float)ctx.inWave[j]
                                       : ctx.inAmplitude * (float)std::sin(2.0 * M_PI * (double)j / (double)n);

      // State on the grid: s[c][j]. Delayed copies sd, next values g.
      std::vector<std::vector<double>> s((size_t)k, std::vector<double>(n));
      for (int c = 0; c < k; c++)
         std::fill(s[(size_t)c].begin(), s[(size_t)c].end(), prog.state[(size_t)c].initialValue);
      std::vector<std::vector<double>> sd = s, g = s;
      std::vector<char> isAcc((size_t)k, 0);
      std::vector<double> y(n);

      auto delayAll = [&]() {
         for (int c = 0; c < k; c++)
         {
            if (isAcc[(size_t)c])
            {
               // A ramp is delayed exactly, not through its (jumping) spectrum.
               for (size_t j = 0; j < n; j++)
               {
                  double v = (double)j / (double)n - cyclesPerSample;
                  sd[(size_t)c][j] = v - std::floor(v);
               }
            }
            else
               DelayPeriodic(s[(size_t)c], phi * (double)lag[(size_t)c], sd[(size_t)c]);
         }
      };
      auto evalAll = [&]() {
         std::vector<double> st((size_t)std::max(k, 1)), nx((size_t)std::max(k, 1));
         for (size_t j = 0; j < n; j++)
         {
            for (int c = 0; c < k; c++)
               st[(size_t)c] = sd[(size_t)c][j];
            y[j] = run.Run(j, in[j], st.data(), nx.data());
            for (int c = 0; c < k; c++)
               g[(size_t)c][j] = nx[(size_t)c];
            for (size_t r = 0; r < ctx.captureRegs.size(); r++)
               out.captured[r][j] = run.regs[ctx.captureRegs[r]];
         }
      };
      // Grid-mean Jacobian d next_c / d delayed_l over a subset of points.
      auto meanJacobian = [&](std::vector<double>& m) {
         m.assign((size_t)(k * k), 0.0);
         std::vector<double> st((size_t)k), nx0((size_t)k), nx1((size_t)k);
         int count = 0;
         for (size_t j = 0; j < n; j += 16, count++)
         {
            for (int c = 0; c < k; c++)
               st[(size_t)c] = sd[(size_t)c][j];
            run.Run(j, in[j], st.data(), nx0.data());
            for (int l = 0; l < k; l++)
            {
               const double d = 1e-3 * (1.0 + std::fabs(st[(size_t)l]));
               st[(size_t)l] += d;
               run.Run(j, in[j], st.data(), nx1.data());
               st[(size_t)l] -= d;
               for (int c = 0; c < k; c++)
                  m[(size_t)(c * k + l)] += std::clamp((nx1[(size_t)c] - nx0[(size_t)c]) / d, -10.0, 10.0);
            }
         }
         for (double& v : m)
            v /= (double)std::max(count, 1);
      };

      if (k > 0)
      {
         // Accumulators: self-Jacobian 1, no other dependence. Try the ramp
         // locked to f0 as their steady state.
         std::vector<double> m;
         delayAll();
         meanJacobian(m);
         for (int c = 0; c < k; c++)
         {
            bool acc = lag[(size_t)c] == 1 && std::fabs(m[(size_t)(c * k + c)] - 1.0) < 1e-2;
            for (int l = 0; l < k && acc; l++)
               if (l != c && std::fabs(m[(size_t)(c * k + l)]) > 1e-3)
                  acc = false;
            if (acc)
            {
               isAcc[(size_t)c] = 1;
               out.accumulators++;
               for (size_t j = 0; j < n; j++)
                  s[(size_t)c][j] = (double)j / (double)n;
            }
         }
         // A slow one-pole also has a self-Jacobian near 1: keep a candidate
         // only if the ramp really is its fixed point.
         if (out.accumulators > 0)
         {
            delayAll();
            evalAll();
            for (int c = 0; c < k; c++)
            {
               if (!isAcc[(size_t)c])
                  continue;
               double worst = 0.0;
               for (size_t j = 0; j < n; j++)
                  worst = std::max(worst, WrapDist(g[(size_t)c][j], s[(size_t)c][j]));
               if (worst > 1e-3)
               {
                  isAcc[(size_t)c] = 0;
                  out.accumulators--;
                  std::fill(s[(size_t)c].begin(), s[(size_t)c].end(), prog.state[(size_t)c].initialValue);
               }
            }
         }

         std::vector<int> freeIdx;
         for (int c = 0; c < k; c++)
            if (!isAcc[(size_t)c])
               freeIdx.push_back(c);
         const int kf = (int)freeIdx.size();

         auto residual = [&]() {
            double r2 = 0.0, s2 = 0.0;
            for (int c : freeIdx)
               for (size_t j = 0; j < n; j++)
               {
                  const double r = g[(size_t)c][j] - s[(size_t)c][j];
                  r2 += r * r;
                  s2 += s[(size_t)c][j] * s[(size_t)c][j];
               }
            const double cnt = (double)std::max<size_t>(1, (size_t)kf * n);
            return std::make_pair(std::sqrt(r2 / cnt), std::sqrt(s2 / cnt));
         };

         delayAll();
         evalAll();
         auto [res, mag] = residual();
         const int kMaxIter = 40;
         bool converged = kf == 0 || res < 1e-6 * (1.0 + mag);
         int it = 0;
         std::vector<double> mf((size_t)(kf * kf));
         for (; !converged && it < kMaxIter; it++)
         {
            meanJacobian(m);
            for (int a = 0; a < kf; a++)
               for (int b = 0; b < kf; b++)
                  mf[(size_t)(a * kf + b)] = m[(size_t)(freeIdx[(size_t)a] * k + freeIdx[(size_t)b])];
            out.loopRadius = SpectralRadius(mf, kf);

            // Newton step with the mean Jacobian: per harmonic h,
            // (I - M e^{-i h phi}) delta_h = r_h.
            std::vector<std::vector<cd>> rh((size_t)kf, std::vector<cd>(n));
            for (int a = 0; a < kf; a++)
            {
               const int c = freeIdx[(size_t)a];
               for (size_t j = 0; j < n; j++)
                  rh[(size_t)a][j] = g[(size_t)c][j] - s[(size_t)c][j];
               Fft(rh[(size_t)a], false);
            }
            std::vector<cd> x((size_t)kf), rot((size_t)kf);
            for (size_t b = 0; b < n; b++)
            {
               const double h = (double)Harm(b, n);
               for (int a = 0; a < kf; a++)
               {
                  const double hp = h * (phi * (double)lag[(size_t)freeIdx[(size_t)a]]);
                  rot[(size_t)a] = cd(std::cos(hp), -std::sin(hp));
               }
               for (int a = 0; a < kf; a++)
                  x[(size_t)a] = rh[(size_t)a][b];
               if (!SolveShifted(mf, kf, rot, x))
                  continue; // keep the plain fixed-point step for this harmonic
               for (int a = 0; a < kf; a++)
                  rh[(size_t)a][b] = x[(size_t)a];
            }
            std::vector<std::vector<double>> step((size_t)kf, std::vector<double>(n));
            for (int a = 0; a < kf; a++)
            {
               Fft(rh[(size_t)a], true);
               for (size_t j = 0; j < n; j++)
                  step[(size_t)a][j] = rh[(size_t)a][j].real() / (double)n;
            }

            // Backtracking: halve the step until the residual drops.
            const std::vector<std::vector<double>> s0 = s;
            double lambda = 1.0;
            for (int bt = 0; bt < 6; bt++, lambda *= 0.5)
            {
               for (int a = 0; a < kf; a++)
                  for (size_t j = 0; j < n; j++)
                     s[(size_t)freeIdx[(size_t)a]][j] = s0[(size_t)freeIdx[(size_t)a]][j] + lambda * step[(size_t)a][j];
               delayAll();
               evalAll();
               auto [r1, m1] = residual();
               if (r1 < res || bt == 5)
               {
                  res = r1;
                  mag = m1;
                  break;
               }
            }
            converged = res < 1e-6 * (1.0 + mag);
         }
         out.iterations = it;
         if (!converged)
         {
            char b[160];
            snprintf(b, sizeof(b), "no periodic steady state after %d iterations (residual %.2g, loop radius %.3f)",
                     it, res, out.loopRadius);
            out.status = b;
            out.evaluations = run.evals;
            return false;
         }
         if (out.loopRadius >= 1.0)
         {
            char b[120];
            snprintf(b, sizeof(b), "periodic solution is unstable (loop radius %.3f): self-oscillation",
                     out.loopRadius);
            out.status = b;
            out.evaluations = run.evals;
            return false;
         }
         for (int c = 0; c < k; c++)
            if (isAcc[(size_t)c])
            {
               double worst = 0.0;
               for (size_t j = 0; j < n; j++)
                  worst = std::max(worst, WrapDist(g[(size_t)c][j], s[(size_t)c][j]));
               if (worst > 1e-3)
               {
                  out.status = "phase accumulator not locked to f0";
                  out.evaluations = run.evals;
                  return false;
               }
            }
      }
      else
      {
         evalAll();
      }

      out.evaluations = run.evals;
      SpectrumFromWave(y, out);
      return true;
   }
   }

   void SpectrumFromWave(const std::vector<double>& y, AliasSpectrum& out)
   {
      const size_t n = y.size();
      std::vector<cd> a(n);
      for (size_t j = 0; j < n; j++)
         a[j] = y[j];
      Fft(a, false);
      const size_t hTop = n / 4; // above this the grid's own folding biases the lines
      out.power.assign(hTop + 1, 0.0);
      out.power[0] = std::norm(a[0] / (double)n);
      for (size_t h = 1; h <= hTop; h++)
         out.power[h] = std::norm(2.0 * a[h] / (double)n);

      // Tail past hTop: P(h) ~ h^-q from the octave bands [H/8,H/4) and [H/2,H).
      out.tailDensity = 0.0;
      out.tailExp = 0.0;
      out.tailRef = 1.0;
      double e1 = 0.0, e2 = 0.0;
      for (size_t h = hTop / 8; h < hTop / 4; h++)
         e1 += out.power[h];
      for (size_t h = hTop / 2; h < hTop; h++)
         e2 += out.power[h];
      const double fund = std::max(out.power.size() > 1 ? out.power[1] : 0.0, 1e-300);
      if (e1 > 0.0 && e2 > 1e-15 * fund)
      {
         const double q = 1.0 - std::log(e2 / e1) / std::log(4.0);
         out.tailExp = std::max(q, 1.1);
         out.tailDensity = e2 / (double)(hTop / 2);
         out.tailRef = 0.75 * (double)hTop;
      }
      out.ok = true;
      out.status = "ok";
   }

   void BandLimitWave(std::vector<double>& y, double maxHarmonic)
   {
      const size_t n = y.size();
      std::vector<cd> a(n);
      for (size_t j = 0; j < n; j++)
         a[j] = y[j];
      Fft(a, false);
      for (size_t b = 0; b < n; b++)
         if (std::abs((double)Harm(b, n)) > maxHarmonic)
            a[b] = 0.0;
      Fft(a, true);
      for (size_t j = 0; j < n; j++)
         y[j] = a[j].real() / (double)n;
   }

   double PredictAliasDb(const AliasSpectrum& s, double f0Hz, double baseRate, int factor, double audibleHz,
                         double harmonicTolHz)
   {
      if (!s.ok || s.power.size() < 2)
         return 0.0;
      const double fs = baseRate * (double)factor;
      const double fund = std::max(s.power[1], 1e-300);
      double alias = 0.0;
      auto add = [&](double h, double p) {
         double r = std::fmod(h * f0Hz, fs);
         if (r > 0.5 * fs)
            r = fs - r;
         if (r > audibleHz)
            return;
         const double j = std::round(r / f0Hz);
         if (std::fabs(r - j * f0Hz) <= harmonicTolHz)
            return;
         alias += p;
      };
      const size_t hTop = s.power.size() - 1;
      for (size_t h = 1; h <= hTop; h++)
         add((double)h, s.power[h]);
      if (s.tailDensity > 0.0)
      {
         const double hMax = std::min(4194304.0, std::ceil(512.0 * fs / f0Hz));
         for (double h = (double)hTop + 1.0; h <= hMax; h += 1.0)
         {
            const double p = s.tailDensity * std::pow(h / s.tailRef, -s.tailExp);
            if (p < 1e-20 * fund)
               break;
            add(h, p);
         }
      }
      return 10.0 * std::log10(std::max(alias, 1e-30) / fund);
   }

   double PredictFoldedDb(const AliasSpectrum& s, double f0Hz, double baseRate, int factor, double audibleHz)
   {
      if (!s.ok || s.power.size() < 2)
         return 0.0;
      const double fs = baseRate * (double)factor;
      double alias = 0.0, ref = 0.0;
      auto add = [&](double h, double p) {
         const double f = h * f0Hz;
         if (f <= audibleHz)
         {
            ref += p;
            return;
         }
         if (f <= 0.5 * fs)
            return; // in band at fs, above the audible band: low-passed away
         double r = std::fmod(f, fs);
         if (r > 0.5 * fs)
            r = fs - r;
         if (r <= audibleHz)
            alias += p;
      };
      const size_t hTop = s.power.size() - 1;
      for (size_t h = 0; h <= hTop; h++)
         add((double)h, s.power[h]);
      if (s.tailDensity > 0.0)
      {
         const double hMax = std::min(4194304.0, std::ceil(512.0 * fs / f0Hz));
         for (double h = (double)hTop + 1.0; h <= hMax; h += 1.0)
         {
            const double p = s.tailDensity * std::pow(h / s.tailRef, -s.tailExp);
            if (p < 1e-20 * std::max(ref, 1e-300))
               break;
            add(h, p);
         }
      }
      return 10.0 * std::log10(std::max(alias, 1e-30) / std::max(ref, 1e-30));
   }
}

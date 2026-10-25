#include "CircuitSynthDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace CircuitDsp
{
   namespace
   {
      constexpr double kPi = 3.14159265358979323846;
      constexpr double kThermalV = 0.025852; // kT/q at 300 K
      constexpr double kGmin = 1e-9;         // every node to ground, as in SPICE
      constexpr double kNeonOff = 1e-10;     // an unlit lamp's leakage
      constexpr double kNeonHold = 1e-4;     // A: below this a lit lamp goes out
      constexpr double kChuaOuter = 4e-3;    // S: beyond E2 the Chua diode turns passive
      constexpr double kBlowUp = 1e5;        // V: a state past this is reset

      bool InMatrix(PartKind k) { return k == kResistor || k == kCapacitor || k == kInductor; }

      bool IsNonlinear(PartKind k) { return k == kDiode || k == kNeon || k == kChua || k == kNegRes || k == kOpAmp; }

      // Element current a -> b and its slope, for a voltage a - b.
      void ElementCurrent(const Circuit::Part& p, double v, double& i, double& g)
      {
         switch (p.kind)
         {
         case kDiode:
         {
            const double nvt = std::max(0.2, p.p1) * kThermalV;
            const double x = v / nvt;
            if (x > 40.0)
            {
               // Linear past e^40 so Newton never overflows; the root is far below.
               const double e40 = 2.3538526683702e17;
               i = p.value * (e40 * (1.0 + x - 40.0) - 1.0);
               g = p.value * e40 / nvt;
            }
            else
            {
               const double e = std::exp(x);
               i = p.value * (e - 1.0);
               g = p.value * e / nvt;
            }
            g += 1e-12;
            break;
         }
         case kNeon:
         {
            const double av = std::fabs(v);
            if (p.lit && av > p.p1)
            {
               const double glit = 1.0 / std::max(1.0, p.p2);
               i = (v > 0.0 ? 1.0 : -1.0) * (av - p.p1) * glit + kNeonOff * v;
               g = glit + kNeonOff;
            }
            else
            {
               i = kNeonOff * v;
               g = kNeonOff;
            }
            break;
         }
         case kChua:
         {
            const double ga = p.value, gb = p.p1, e1 = p.p2, e2 = std::max(p.p2 + 1e-3, p.p3);
            i = kChuaOuter * v + 0.5 * (ga - gb) * (std::fabs(v + e1) - std::fabs(v - e1)) +
                0.5 * (gb - kChuaOuter) * (std::fabs(v + e2) - std::fabs(v - e2));
            const double av = std::fabs(v);
            g = av < e1 ? ga : (av < e2 ? gb : kChuaOuter);
            break;
         }
         case kNegRes:
         {
            const double vs2 = std::max(1e-6, p.p1 * p.p1);
            i = p.value * (v * v * v / vs2 - v);
            g = p.value * (3.0 * v * v / vs2 - 1.0);
            break;
         }
         default:
            i = 0.0;
            g = 0.0;
            break;
         }
      }

      uint32_t Hash(uint32_t x)
      {
         x ^= x >> 16;
         x *= 0x7feb352dU;
         x ^= x >> 15;
         x *= 0x846ca68bU;
         x ^= x >> 16;
         return x;
      }

      double Blep(double t, double dt)
      {
         if (t < dt)
         {
            t /= dt;
            return t + t - t * t - 1.0;
         }
         if (t > 1.0 - dt)
         {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
         }
         return 0.0;
      }
   }

   // ---------------------------------------------------------------- parts

   double KnobScale(PartKind kind, double t)
   {
      t = std::clamp(t, -1.0, 1.0);
      switch (kind)
      {
      case kDiode: return std::pow(2.0, t);   // emission n: clip level x0.5 .. x2
      case kNeon: return std::pow(1.4, t);    // strike voltage
      case kChua: return std::pow(1.5, t);    // inner slope
      case kNegRes: return std::pow(4.0, t);  // how hard it pushes
      case kOpAmp: return std::pow(10.0, t);  // open-loop gain
      default: return std::pow(8.0, t);       // R, C, L, generator volts: three octaves each way
      }
   }

   void FormatPartValue(const PartDef& part, double scale, char* out, int size)
   {
      auto eng = [&](double v, const char* unit)
      {
         struct Prefix
         {
            double f;
            const char* p;
         };
         static const Prefix kPrefixes[] = { { 1e6, "M" }, { 1e3, "k" }, { 1.0, "" }, { 1e-3, "m" },
                                             { 1e-6, "u" }, { 1e-9, "n" }, { 1e-12, "p" } };
         const double av = std::fabs(v);
         for (const Prefix& p : kPrefixes)
            if (av >= p.f * 0.9995 || p.f == 1e-12)
            {
               const double s = v / p.f;
               const char* fmt = std::fabs(s) < 10.0 ? "%.2f %s%s" : (std::fabs(s) < 100.0 ? "%.1f %s%s" : "%.0f %s%s");
               std::snprintf(out, (size_t)size, fmt, s, p.p, unit);
               return;
            }
      };
      switch (part.kind)
      {
      case kResistor: eng(part.value * scale, "ohm"); break;
      case kCapacitor: eng(part.value * scale, "F"); break;
      case kInductor: eng(part.value * scale, "H"); break;
      case kGenerator: std::snprintf(out, (size_t)size, "%.2f V", part.value * scale); break;
      case kDiode: std::snprintf(out, (size_t)size, "n %.2f", part.p1 * scale); break;
      case kNeon: std::snprintf(out, (size_t)size, "strike %.0f V", part.value * scale); break;
      case kChua: eng(part.value * scale, "S"); break;
      case kNegRes: eng(-part.value * scale, "S"); break;
      case kOpAmp: std::snprintf(out, (size_t)size, "gain %.0f", part.value * scale); break;
      }
   }

   // ---------------------------------------------------------------- circuit

   void Circuit::Load(const Preset& p)
   {
      n = std::clamp(p.numNodes, 1, kMaxNodes);
      numParts = std::clamp(p.numParts, 0, kMaxParts);
      numPorts = numReactive = numGens = numNeons = 0;
      for (int k = 0; k < numParts; k++)
      {
         const PartDef& d = p.parts[k];
         Part& q = parts[k];
         if (d.kind == kCapacitor || d.kind == kInductor)
            reactive[numReactive++] = (int8_t)k;
         else if (d.kind == kGenerator)
            gens[numGens++] = (int8_t)k;
         else if (d.kind == kNeon)
            neons[numNeons++] = (int8_t)k;
         q.kind = d.kind;
         q.a = std::clamp((int)d.a, 0, n);
         q.b = std::clamp((int)d.b, 0, n);
         q.c = std::clamp((int)d.c, 0, n);
         q.value = d.value;
         q.p1 = d.p1;
         q.p2 = d.p2;
         q.p3 = d.p3;
         if (!IsNonlinear(d.kind))
            continue;
         if (d.kind == kOpAmp)
         {
            if (numPorts >= kMaxPorts)
               continue;
            Port& port = ports[numPorts++];
            port = Port();
            port.ca = q.a;
            port.cb = q.b;
            port.ia = q.c;
            port.ib = 0;
            port.opamp = true;
            port.count = 1;
            port.part[0] = k;
            port.sign[0] = 1.0;
            continue;
         }
         // Two-terminal parts across the same pair share a port (antiparallel diodes).
         int found = -1;
         double sign = 1.0;
         for (int j = 0; j < numPorts; j++)
         {
            const Port& port = ports[j];
            if (port.opamp || port.count >= kMaxPortParts)
               continue;
            if (port.ca == q.a && port.cb == q.b)
            {
               found = j;
               sign = 1.0;
               break;
            }
            if (port.ca == q.b && port.cb == q.a)
            {
               found = j;
               sign = -1.0;
               break;
            }
         }
         if (found < 0)
         {
            if (numPorts >= kMaxPorts)
               continue;
            found = numPorts++;
            ports[found] = Port();
            ports[found].ca = ports[found].ia = q.a;
            ports[found].cb = ports[found].ib = q.b;
         }
         Port& port = ports[found];
         port.part[port.count] = k;
         port.sign[port.count] = sign;
         port.count++;
      }
      dirty = true;
      Reset();
   }

   void Circuit::Reset()
   {
      for (int k = 0; k < numParts; k++)
      {
         parts[k].v = 0.0;
         parts[k].i = 0.0;
         parts[k].lit = false;
      }
      for (int j = 0; j < numPorts; j++)
         ports[j].v = ports[j].vPrev = 0.0;
      for (int k = 0; k <= kMaxNodes; k++)
         x[k] = 0.0;
      if (contact != 0.0)
      {
         contact = 0.0;
         dirty = true;
      }
   }

   void Circuit::SetValue(int part, double value)
   {
      Part& p = parts[part];
      if (p.kind == kDiode)
      {
         p.p1 = value; // a diode's knob is its emission coefficient
         return;
      }
      if (std::fabs(value - p.value) <= 1e-12 * std::fabs(value))
         return;
      p.value = value;
      if (InMatrix(p.kind))
         dirty = true;
   }

   void Circuit::SetContact(double c)
   {
      double q;
      if (c < 1e-4)
         q = 0.0;
      else if (c > 0.9999)
         q = 1.0;
      else
         q = std::exp2(std::round(std::log2(c) * 4.0) * 0.25);
      if (q != contact)
      {
         contact = q;
         dirty = true;
      }
   }

   void Circuit::Factor()
   {
      const int N = n;
      double* A = lu;
      std::fill(A, A + N * N, 0.0);
      auto stamp = [&](int a, int b, double g)
      {
         if (a)
            A[(a - 1) * N + (a - 1)] += g;
         if (b)
            A[(b - 1) * N + (b - 1)] += g;
         if (a && b)
         {
            A[(a - 1) * N + (b - 1)] -= g;
            A[(b - 1) * N + (a - 1)] -= g;
         }
      };
      for (int i = 0; i < N; i++)
         A[i * N + i] += kGmin;
      for (int k = 0; k < numParts; k++)
      {
         Part& p = parts[k];
         switch (p.kind)
         {
         case kResistor: stamp(p.a, p.b, 1.0 / std::max(1e-3, p.value)); break;
         case kCapacitor: p.g = 2.0 * std::max(1e-18, p.value) / dt; stamp(p.a, p.b, p.g); break;
         case kInductor: p.g = dt / (2.0 * std::max(1e-12, p.value)); stamp(p.a, p.b, p.g); break;
         case kGenerator: p.g = contact / std::max(1e-3, p.p1); stamp(p.a, p.b, p.g); break;
         case kOpAmp: stamp(p.c, 0, 1.0 / std::max(1e-3, p.p1)); break;
         default: break;
         }
      }
      // LU with partial pivoting, in place.
      for (int k = 0; k < N; k++)
      {
         int pr = k;
         double best = std::fabs(A[k * N + k]);
         for (int i = k + 1; i < N; i++)
            if (std::fabs(A[i * N + k]) > best)
            {
               best = std::fabs(A[i * N + k]);
               pr = i;
            }
         piv[k] = pr;
         if (pr != k)
            for (int j = 0; j < N; j++)
               std::swap(A[k * N + j], A[pr * N + j]);
         double pivot = A[k * N + k];
         if (std::fabs(pivot) < 1e-300)
            pivot = A[k * N + k] = 1e-300;
         const double inv = 1.0 / pivot;
         for (int i = k + 1; i < N; i++)
         {
            const double f = A[i * N + k] * inv;
            A[i * N + k] = f;
            if (f != 0.0)
               for (int j = k + 1; j < N; j++)
                  A[i * N + j] -= f * A[k * N + j];
         }
      }
      // Each port's unit-injection response, and the port-to-port impedances.
      for (int j = 0; j < numPorts; j++)
      {
         double* z = zcol[j];
         std::fill(z, z + kMaxNodes + 1, 0.0);
         if (ports[j].ia)
            z[ports[j].ia] += 1.0;
         if (ports[j].ib)
            z[ports[j].ib] -= 1.0;
         SolveLu(z);
         z[0] = 0.0;
      }
      for (int p = 0; p < numPorts; p++)
         for (int j = 0; j < numPorts; j++)
            Z[p][j] = zcol[j][ports[p].ca] - zcol[j][ports[p].cb];
      dirty = false;
   }

   void Circuit::SolveLu(double* b) const
   {
      const int N = n;
      double* y = b + 1;
      for (int k = 0; k < N; k++)
         if (piv[k] != k)
            std::swap(y[k], y[piv[k]]);
      for (int i = 1; i < N; i++)
      {
         double s = y[i];
         const double* row = lu + i * N;
         for (int j = 0; j < i; j++)
            s -= row[j] * y[j];
         y[i] = s;
      }
      for (int i = N - 1; i >= 0; i--)
      {
         double s = y[i];
         const double* row = lu + i * N;
         for (int j = i + 1; j < N; j++)
            s -= row[j] * y[j];
         y[i] = s / row[i];
      }
   }

   void Circuit::PortEval(const Port& port, double v, double& inj, double& dinj) const
   {
      if (port.opamp)
      {
         const Part& p = parts[port.part[0]];
         const double rail = std::max(0.1, p.p2), ro = std::max(1e-3, p.p1);
         const double t = std::tanh(p.value * v / rail);
         inj = rail / ro * t;
         dinj = p.value / ro * (1.0 - t * t);
         return;
      }
      double ie = 0.0, ge = 0.0;
      if (port.count == 2 && port.sign[0] != port.sign[1])
      {
         // Antiparallel diodes of one emission coefficient: one exp for both.
         const Part& d0 = parts[port.part[0]];
         const Part& d1 = parts[port.part[1]];
         if (d0.kind == kDiode && d1.kind == kDiode && d0.p1 == d1.p1)
         {
            const double nvt = std::max(0.2, d0.p1) * kThermalV;
            const double x = port.sign[0] * v / nvt;
            if (std::fabs(x) < 40.0)
            {
               const double e = std::exp(x), ei = 1.0 / e;
               const double i = d0.value * (e - 1.0) - d1.value * (ei - 1.0); // along part 0's direction
               inj = -port.sign[0] * i;
               dinj = -((d0.value * e + d1.value * ei) / nvt + 2e-12);
               return;
            }
         }
      }
      for (int k = 0; k < port.count; k++)
      {
         const double s = port.sign[k];
         double i, g;
         ElementCurrent(parts[port.part[k]], s * v, i, g);
         ie += s * i;
         ge += g;
      }
      inj = -ie; // the element draws its current out of the injection node
      dinj = -ge;
   }

   bool Circuit::Step(double source)
   {
      if (dirty)
         Factor();
      for (int k = 0; k <= n; k++)
         rhs[k] = 0.0;
      for (int r = 0; r < numReactive; r++)
      {
         const Part& p = parts[reactive[r]];
         // Trapezoidal companions as current sources. C: i(n+1) = g v(n+1) -
         // (g v(n) + i(n)); L: i(n+1) = g v(n+1) + (i(n) + g v(n)).
         const double cur = p.kind == kCapacitor ? p.g * p.v + p.i : -(p.i + p.g * p.v);
         rhs[p.a] += cur;
         rhs[p.b] -= cur;
      }
      for (int r = 0; r < numGens; r++)
      {
         const Part& p = parts[gens[r]];
         const double cur = p.value * source * p.g; // Norton: V behind Rs / contact
         rhs[p.a] += cur;
         rhs[p.b] -= cur;
      }
      rhs[0] = 0.0;
      SolveLu(rhs);
      for (int k = 1; k <= n; k++)
         x[k] = rhs[k];
      x[0] = 0.0;

      newtonIters = 0;
      if (numPorts == 1)
      {
         // Scalar Newton, safeguarded by a bracket: F(v) = v - v0 - Z inj(v).
         Port& port = ports[0];
         const double v0 = x[port.ca] - x[port.cb];
         const double z = Z[0][0];
         double v = 2.0 * port.v - port.vPrev, inj = 0.0, dinj = 0.0;
         double lo = -1e300, hi = 1e300;
         for (int it = 0; it < 50; it++)
         {
            PortEval(port, v, inj, dinj);
            const double F = v - v0 - z * inj;
            const double dF = 1.0 - z * dinj;
            newtonIters = it + 1;
            if (std::fabs(F) < 1e-9 * (1.0 + std::fabs(v)))
               break;
            if (F > 0.0)
               hi = std::min(hi, v);
            else
               lo = std::max(lo, v);
            double vn = dF > 1e-30 ? v - F / dF : v;
            if (!(vn > lo && vn < hi) || !std::isfinite(vn) || vn == v)
            {
               if (lo > -1e299 && hi < 1e299)
                  vn = 0.5 * (lo + hi);
               else
               {
                  const double step = std::max(0.5, std::fabs(v));
                  vn = F > 0.0 ? v - step : v + step;
               }
            }
            if (std::fabs(vn - v) < 1e-13 * (1.0 + std::fabs(v)))
            {
               v = vn;
               PortEval(port, v, inj, dinj);
               break;
            }
            v = vn;
         }
         port.vPrev = port.v;
         port.v = v;
         const double* z0 = zcol[0];
         for (int k = 1; k <= n; k++)
            x[k] += z0[k] * inj;
      }
      else if (numPorts > 1)
      {
         const int K = numPorts;
         double v[kMaxPorts], v0[kMaxPorts], inj[kMaxPorts], dinj[kMaxPorts];
         for (int p = 0; p < K; p++)
         {
            v0[p] = x[ports[p].ca] - x[ports[p].cb];
            v[p] = 2.0 * ports[p].v - ports[p].vPrev;
         }
         for (int it = 0; it < 40; it++)
         {
            double J[kMaxPorts][kMaxPorts + 1];
            for (int p = 0; p < K; p++)
               PortEval(ports[p], v[p], inj[p], dinj[p]);
            double worst = 0.0;
            for (int p = 0; p < K; p++)
            {
               double s = v[p] - v0[p];
               for (int j = 0; j < K; j++)
               {
                  s -= Z[p][j] * inj[j];
                  J[p][j] = (p == j ? 1.0 : 0.0) - Z[p][j] * dinj[j];
               }
               J[p][K] = -s;
               worst = std::max(worst, std::fabs(s));
            }
            newtonIters = it + 1;
            if (worst < 1e-9)
               break;
            // Gauss with partial pivoting on the K x K Jacobian.
            for (int c = 0; c < K; c++)
            {
               int pr = c;
               for (int r = c + 1; r < K; r++)
                  if (std::fabs(J[r][c]) > std::fabs(J[pr][c]))
                     pr = r;
               if (pr != c)
                  for (int j = 0; j <= K; j++)
                     std::swap(J[c][j], J[pr][j]);
               if (std::fabs(J[c][c]) < 1e-300)
                  J[c][c] = 1e-300;
               for (int r = c + 1; r < K; r++)
               {
                  const double f = J[r][c] / J[c][c];
                  for (int j = c; j <= K; j++)
                     J[r][j] -= f * J[c][j];
               }
            }
            double d[kMaxPorts];
            double moved = 0.0;
            for (int c = K - 1; c >= 0; c--)
            {
               double s = J[c][K];
               for (int j = c + 1; j < K; j++)
                  s -= J[c][j] * d[j];
               d[c] = std::clamp(s / J[c][c], -0.5, 0.5);
               if (!std::isfinite(d[c]))
                  d[c] = 0.0;
               moved = std::max(moved, std::fabs(d[c]));
            }
            for (int p = 0; p < K; p++)
               v[p] += d[p];
            if (moved < 1e-12)
               break;
         }
         for (int p = 0; p < K; p++)
         {
            PortEval(ports[p], v[p], inj[p], dinj[p]);
            ports[p].vPrev = ports[p].v;
            ports[p].v = v[p];
         }
         for (int j = 0; j < K; j++)
         {
            const double* zj = zcol[j];
            for (int k = 1; k <= n; k++)
               x[k] += zj[k] * inj[j];
         }
      }

      // Commit the companion states.
      double worst = 0.0;
      for (int k = 1; k <= n; k++)
         worst = std::max(worst, std::fabs(x[k]));
      if (!(worst < kBlowUp))
      {
         Reset();
         return false;
      }
      for (int r = 0; r < numReactive; r++)
      {
         Part& p = parts[reactive[r]];
         double vab = x[p.a] - x[p.b];
         double i = p.kind == kCapacitor ? p.g * (vab - p.v) - p.i : p.g * (vab + p.v) + p.i;
         if (std::fabs(i) < 1e-30)
            i = 0.0; // decaying tails never reach denormals
         if (std::fabs(vab) < 1e-30)
            vab = 0.0;
         p.i = i;
         p.v = vab;
      }
      for (int r = 0; r < numNeons; r++)
      {
         Part& p = parts[neons[r]];
         const double av = std::fabs(x[p.a] - x[p.b]);
         if (!p.lit && av >= p.value)
            p.lit = true;
         else if (p.lit && av < p.p1 + kNeonHold * std::max(1.0, p.p2))
            p.lit = false;
      }
      return true;
   }

   double Circuit::Probe(const ProbeDef& probe) const
   {
      if (probe.current)
      {
         if (probe.part < 0 || probe.part >= numParts)
            return 0.0;
         const Part& p = parts[probe.part];
         const double i = p.kind == kResistor ? (x[p.a] - x[p.b]) / std::max(1e-3, p.value) : p.i;
         return i * probe.gain;
      }
      double sum = 0.0;
      for (int k = 0; k < 4; k++)
         sum += probe.weight[k] * x[std::clamp((int)probe.node[k], 0, n)];
      return sum * probe.gain;
   }

   // ---------------------------------------------------------------- engine

   void Engine::Prepare(double sampleRate)
   {
      mRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
      const int preset = mPreset < 0 ? DefaultPreset() : mPreset;
      mPreset = -1;
      LoadPreset(preset);
      Reset();
   }

   void Engine::DesignDecimator()
   {
      const double fs = mRate * mOver;
      const double fc = std::min(20000.0, 0.45 * mRate) / fs;
      double sum = 0.0;
      double h[kTaps];
      for (int k = 0; k < kTaps; k++)
      {
         const double m = k - (kTaps - 1) * 0.5;
         const double s = m == 0.0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * m) / (kPi * m);
         const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * k / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * k / (kTaps - 1));
         h[k] = s * w;
         sum += h[k];
      }
      for (int k = 0; k < kTaps; k++)
         mFir[k] = (float)(h[k] / sum);
      mDcK = std::exp(-2.0 * kPi * 8.0 / mRate);
      std::fill(std::begin(mHistL), std::end(mHistL), 0.0f);
      std::fill(std::begin(mHistR), std::end(mHistR), 0.0f);
      mHistPos = 0;
   }

   void Engine::Reset()
   {
      for (Voice& v : mVoices)
      {
         v.active = v.gate = false;
         v.env = v.contact = 0.0;
         v.tw[0].Reset();
         v.tw[1].Reset();
      }
      mStackSize = 0;
      std::fill(std::begin(mHistL), std::end(mHistL), 0.0f);
      std::fill(std::begin(mHistR), std::end(mHistR), 0.0f);
      mHistPos = 0;
      mDcXL = mDcYL = mDcXR = mDcYR = 0.0;
   }

   void Engine::LoadPreset(int index)
   {
      index = std::clamp(index, 0, NumPresets() - 1);
      if (index == mPreset)
         return;
      mPreset = index;
      const Preset& p = GetPreset(index);
      // Nonlinear circuits run near 192 kHz (diodes, lamps and op-amp rails
      // make harmonics far above the audio band, which must not fold back);
      // linear ones near 96 kHz, which keeps the trapezoidal warp under 0.2 %
      // below 2 kHz and the generator's own aliasing far down.
      int ports = 0;
      for (int k = 0; k < p.numParts; k++)
         ports += IsNonlinear(p.parts[k].kind) ? 1 : 0;
      mOver = std::clamp((int)std::lround((ports > 0 ? 192000.0 : 96000.0) / mRate), 1, kMaxOver);
      DesignDecimator();
      const double dt = 1.0 / (mRate * mOver);
      for (int vi = 0; vi < kMaxVoices; vi++)
      {
         Voice& v = mVoices[vi];
         for (int t = 0; t < 2; t++)
         {
            v.tw[t].Load(p);
            v.tw[t].dt = dt;
            v.tw[t].dirty = true;
            for (int k = 0; k < kMaxParts; k++)
               v.offset[t][k] = (float)((Hash((uint32_t)(vi * 977 + t * 131 + k * 7919 + 1)) & 0xFFFF) / 32767.5 - 1.0);
         }
         v.active = v.gate = false;
         v.twinLive = false;
         v.env = v.contact = 0.0;
      }
      mStackSize = 0;
      // CPU cap: a rough count of the multiply-adds one twin costs per step
      // (one triangular solve, the companions, Newton), against a budget that
      // keeps 8 voices of the light circuits and fewer of the big meshes.
      // Calibrated on the INFINITE_CIRCUITSYNTHTEST CPU report: percent of
      // one core per stereo voice ~ (over / 4) * (0.6 + 0.02 * cost); the
      // voices of one node stay under ~30 % of a core.
      const double cost = 2.0 * (p.numNodes * p.numNodes + 6.0 * p.numParts + 40.0 * ports);
      const double percent = mOver * mRate / 192000.0 * (0.6 + 0.02 * cost);
      mMaxVoices = std::clamp((int)(30.0 / std::max(0.1, percent)), 1, kMaxVoices);
   }

   double Engine::NoteHz(int note, float pitch) const
   {
      return 440.0 * std::pow(2.0, (note + pitch - 69.0) / 12.0);
   }

   int Engine::AllocateVoice()
   {
      for (int v = 0; v < mMaxVoices; v++)
         if (!mVoices[v].active)
            return v;
      int best = -1;
      uint64_t oldest = UINT64_MAX;
      for (int v = 0; v < mMaxVoices; v++) // a released voice first
         if (!mVoices[v].gate && mVoices[v].age < oldest)
         {
            oldest = mVoices[v].age;
            best = v;
         }
      if (best >= 0)
         return best;
      for (int v = 0; v < mMaxVoices; v++)
         if (mVoices[v].age < oldest)
         {
            oldest = mVoices[v].age;
            best = v;
         }
      return std::max(0, best);
   }

   int Engine::TapSamples(double hz) const
   {
      // A tap is a short touch of the contact: at most a quarter period of the
      // note (a touch as long as a period would cancel the fundamental it is
      // meant to strike), at most 3 ms, plus the contact's own edges.
      const double touch = std::min(0.003, 0.25 / std::max(1.0, hz));
      return (int)((touch + 2e-3 * mContactMs) * mRate * mOver);
   }

   void Engine::Trigger(Voice& v, int note, float velocity, int voiceId, bool fresh)
   {
      const double target = NoteHz(note, mPitch);
      if (fresh)
      {
         v.tw[0].Reset();
         v.tw[1].Reset();
         v.env = v.contact = 0.0;
         v.phase = 0.0;
         v.twinLive = false;
         v.hz = (mMode == kPoly && mGlide > 0.0f) ? mLastHz : target;
      }
      else if (mGlide <= 0.0f)
         v.hz = target;
      v.note = note;
      v.voiceId = voiceId;
      v.velocity = std::clamp(velocity, 0.0f, 1.0f);
      v.targetHz = target;
      v.gate = true;
      v.active = true;
      v.age = mNextAge++;
      if (mTrigger == kTap)
         v.tapRemaining = TapSamples(target);
      mLastHz = target;
   }

   void Engine::NoteOn(int note, float velocity, int voiceId)
   {
      if (mMode == kMono)
      {
         int w = 0;
         for (int r = 0; r < mStackSize; r++)
            if (mStack[r] != note)
            {
               mStack[w] = mStack[r];
               mStackIds[w] = mStackIds[r];
               mStackVel[w] = mStackVel[r];
               w++;
            }
         mStackSize = std::min(w, 15);
         mStack[mStackSize] = note;
         mStackIds[mStackSize] = voiceId;
         mStackVel[mStackSize] = velocity;
         mStackSize++;
         Voice& v = mVoices[0];
         if (v.active && v.gate)
         {
            // Legato: the circuit keeps running, only the key moves.
            v.note = note;
            v.voiceId = voiceId;
            v.targetHz = NoteHz(note, mPitch);
            if (mGlide <= 0.0f)
               v.hz = v.targetHz;
            if (mTrigger == kTap)
               v.tapRemaining = TapSamples(v.targetHz);
            mLastHz = v.targetHz;
         }
         else
            Trigger(v, note, velocity, voiceId, !v.active);
         return;
      }
      Voice& v = mVoices[AllocateVoice()];
      static const float kPan[8] = { 0.0f, -0.6f, 0.6f, -0.3f, 0.3f, -0.85f, 0.85f, 0.15f };
      v.pan = kPan[mPanIndex++ & 7];
      Trigger(v, note, velocity, voiceId, true);
   }

   void Engine::NoteOff(int note, int voiceId)
   {
      if (mMode == kMono)
      {
         int w = 0;
         for (int r = 0; r < mStackSize; r++)
            if (mStack[r] != note)
            {
               mStack[w] = mStack[r];
               mStackIds[w] = mStackIds[r];
               mStackVel[w] = mStackVel[r];
               w++;
            }
         mStackSize = w;
         Voice& v = mVoices[0];
         if (!v.active || v.note != note)
            return;
         if (mStackSize > 0)
         {
            v.note = mStack[mStackSize - 1];
            v.voiceId = mStackIds[mStackSize - 1];
            v.targetHz = NoteHz(v.note, mPitch);
            if (mGlide <= 0.0f)
               v.hz = v.targetHz;
         }
         else
            v.gate = false;
         return;
      }
      for (int i = 0; i < kMaxVoices; i++)
      {
         Voice& v = mVoices[i];
         if (v.active && v.gate && ((voiceId != 0 && v.voiceId == voiceId) || v.note == note))
            v.gate = false;
      }
   }

   void Engine::AllNotesOff()
   {
      for (Voice& v : mVoices)
         v.gate = false;
      mStackSize = 0;
   }

   int Engine::ActiveVoices() const
   {
      int count = 0;
      for (const Voice& v : mVoices)
         count += v.active ? 1 : 0;
      return count;
   }

   float Engine::LfoValue(int shape)
   {
      const double ph = mLfoPhase;
      switch (shape)
      {
      case kLfoTriangle: return (float)(1.0 - 4.0 * std::fabs(ph - 0.5));
      case kLfoSquare: return ph < 0.5 ? 1.0f : -1.0f;
      case kLfoRandom: return mLfoHeld;
      default: return (float)std::sin(2.0 * kPi * ph);
      }
   }

   void Engine::UpdateValues(Voice& v, const Params& p, double tuneScale, double dampAdd)
   {
      const Preset& pr = GetPreset(mPreset);
      const double key = pr.refHz / std::max(1.0, v.hz);
      const int twins = v.twinLive ? 2 : 1;
      const double spread = 0.01 * std::clamp((double)p.spread, 0.0, 10.0);
      for (int t = 0; t < twins; t++)
      {
         Circuit& c = v.tw[t];
         for (int k = 0; k < pr.numParts; k++)
         {
            const PartDef& d = pr.parts[k];
            double trim = d.knob >= 0 ? p.knob[d.knob] : 0.0;
            if (d.knob >= 0 && d.knob == pr.dampKnob)
               trim += dampAdd;
            const double scale = KnobScale(d.kind, trim);
            double val = (d.kind == kDiode ? d.p1 : d.value) * scale;
            if (d.kind == kCapacitor && d.tracks)
               val *= (p.cFollow ? key : 1.0) * tuneScale;
            else if (d.kind == kInductor && d.tracks)
               val *= (p.lFollow ? key : 1.0) * tuneScale;
            if (InMatrix(d.kind))
               val *= 1.0 + spread * v.offset[t][k];
            c.SetValue(k, val);
         }
      }
   }

   void Engine::Apply(const Params& p)
   {
      if (p.preset != mPreset)
         LoadPreset(p.preset);
      if (p.mode != mMode)
      {
         for (Voice& v : mVoices)
            v.gate = false;
         mStackSize = 0;
         mMode = p.mode;
      }
      mTrigger = p.trigger;
      mPitch = p.pitch;
      mGlide = p.glide;
      mContactMs = p.contactMs;
   }

   void Engine::Render(float* outL, float* outR, int frames, const Params& p)
   {
      frames = std::clamp(frames, 0, kMaxChunk);
      if (frames == 0)
         return;
      Apply(p);
      const Preset& pr = GetPreset(mPreset);

      // LFO, one per node, read once per chunk.
      mLfoPhase += std::clamp((double)p.lfoRate, 0.0, 50.0) * frames / mRate;
      if (mLfoPhase >= 1.0)
      {
         mLfoPhase -= std::floor(mLfoPhase);
         mLfoSeed = mLfoSeed * 1664525u + 1013904223u;
         mLfoHeld = (float)((mLfoSeed >> 8) / 8388607.5 - 1.0);
      }
      const double depth = std::clamp((double)p.lfoDepth, 0.0, 1.0);
      const double lfo = depth > 0.0 ? LfoValue(p.lfoShape) : 0.0;
      const double tuneScale = p.lfoTarget == kToTune ? std::pow(2.0, -depth * lfo) : 1.0;
      const double power = p.lfoTarget == kToPower ? std::max(0.0, 1.0 + depth * lfo) : 1.0;
      const double dampAdd = p.lfoTarget == kToDamp ? 0.5 * depth * lfo * pr.dampSign : 0.0;
      const double levelMod = p.lfoTarget == kToLevel ? 1.0 - 0.5 * depth * (1.0 - lfo) : 1.0;

      const double fs = mRate * mOver;
      const int N = frames * mOver;
      std::fill(mBufL, mBufL + N, 0.0f);
      std::fill(mBufR, mBufR + N, 0.0f);
      const ProbeDef& pa = pr.probes[std::clamp(p.probeA, 0, std::max(0, pr.numProbes - 1))];
      const ProbeDef& pb = pr.probes[std::clamp(p.probeB, 0, std::max(0, pr.numProbes - 1))];
      const bool wantTwin = p.spread > 0.001f;
      const double spread = 0.01 * std::clamp((double)p.spread, 0.0, 10.0);
      const double kc = 1.0 - std::exp(-1.0 / (std::max(0.02, (double)p.contactMs) * 1e-3 * fs));
      const double ka = 1.0 - std::exp(-3.0 / (std::max(1e-4, (double)p.attack) * fs));
      const double kr = 1.0 - std::exp(-6.9 / (std::max(1e-3, (double)p.release) * fs));
      const double glideK = p.glide > 0.0f ? std::exp(-frames / (std::max(1e-4, (double)p.glide) * mRate / 3.0)) : 0.0;

      for (int vi = 0; vi < kMaxVoices; vi++)
      {
         Voice& v = mVoices[vi];
         if (!v.active)
            continue;
         if (glideK > 0.0)
            v.hz = std::exp(std::log(v.targetHz) + (std::log(std::max(1.0, v.hz)) - std::log(v.targetHz)) * glideK);
         else
            v.hz = v.targetHz;
         if (wantTwin && !v.twinLive)
            v.tw[1] = v.tw[0];
         v.twinLive = wantTwin;
         UpdateValues(v, p, tuneScale, dampAdd);

         // The generator is one of the twin's parts too. Two real oscillators
         // never sit exactly in phase: twin R's generator wanders around twin
         // L's, slowly (0.23 and 0.37 Hz) and bounded to 3 % of a cycle per
         // percent of spread - the two sides move against each other like a
         // pair of analog circuits, at every pitch, and the mono sum never
         // loses the fundamental (a free detune would, once per beat).
         const double inc = std::min(0.45, v.hz / fs);
         const double driftT = mTime;
         const double drift = 0.03 * (100.0 * spread) *
            (0.6 * std::sin(kPi * (2.0 * 0.23 * driftT + v.offset[1][kMaxParts - 1])) +
             0.4 * std::sin(kPi * (2.0 * 0.37 * driftT + v.offset[1][kMaxParts - 2])));
         const double drive = (0.3 + 0.7 * v.velocity) * power;
         const double ang = (std::clamp((double)v.pan, -1.0, 1.0) + 1.0) * kPi * 0.25;
         const float gL = (float)(std::cos(ang) * 1.41421356), gR = (float)(std::sin(ang) * 1.41421356);
         const double outGain = pr.gain;
         Circuit& c0 = v.tw[0];
         Circuit& c1 = v.tw[1];
         const int wave = p.wave;
         auto shape = [wave, inc](double phase, double noise)
         {
            switch (wave)
            {
            case kNoise: return noise;
            case kStep: return 1.0;
            case kSine: return std::sin(2.0 * kPi * phase);
            case kPulse:
            {
               double h = phase + 0.5;
               if (h >= 1.0)
                  h -= 1.0;
               return (phase < 0.5 ? 1.0 : -1.0) + Blep(phase, inc) - Blep(h, inc);
            }
            default: return 2.0 * phase - 1.0 - Blep(phase, inc);
            }
         };
         for (int s = 0; s < N; s++)
         {
            double noise = 0.0;
            if (wave == kNoise)
            {
               v.noise ^= v.noise << 13;
               v.noise ^= v.noise >> 17;
               v.noise ^= v.noise << 5;
               noise = (v.noise >> 8) / 8388607.5 - 1.0;
            }
            v.phase += inc;
            if (v.phase >= 1.0)
               v.phase -= 1.0;
            const bool closed = mTrigger == kTap ? v.tapRemaining > 0 : v.gate;
            if (v.tapRemaining > 0)
               v.tapRemaining--;
            v.contact += ((closed ? 1.0 : 0.0) - v.contact) * kc;
            v.env += ((v.gate ? 1.0 : 0.0) - v.env) * (v.gate ? ka : kr);
            c0.SetContact(v.contact);
            c0.Step(shape(v.phase, noise) * drive);
            double r;
            if (v.twinLive)
            {
               double ph1 = v.phase + drift;
               ph1 -= std::floor(ph1);
               c1.SetContact(v.contact);
               c1.Step(shape(ph1, noise) * drive);
               r = c1.Probe(pb);
            }
            else
               r = c0.Probe(pb);
            const double l = c0.Probe(pa);
            const double g = v.env * outGain;
            mBufL[s] += (float)(l * g) * gL;
            mBufR[s] += (float)(r * g) * gR;
         }
         if (!v.gate && v.env < 1e-3)
         {
            v.active = false;
            v.tw[0].Reset();
            v.tw[1].Reset();
         }
      }

      mTime += frames / mRate;
      if (mTime > 1e6)
         mTime = 0.0;

      // Back to the device rate, DC out (the circuits' own bias is real, the
      // speakers do not want it), then width and level.
      const double width = std::clamp((double)p.width, 0.0, 1.5);
      const double level = std::clamp((double)p.level, 0.0, 4.0) * levelMod;
      for (int f = 0; f < frames; f++)
      {
         double l, r;
         if (mOver == 1)
         {
            l = mBufL[f];
            r = mBufR[f];
         }
         else
         {
            for (int o = 0; o < mOver; o++)
            {
               const int s = f * mOver + o;
               mHistL[mHistPos] = mHistL[mHistPos + kTaps] = mBufL[s];
               mHistR[mHistPos] = mHistR[mHistPos + kTaps] = mBufR[s];
               mHistPos = mHistPos + 1 == kTaps ? 0 : mHistPos + 1;
            }
            const float* hl = mHistL + mHistPos;
            const float* hr = mHistR + mHistPos;
            float al = 0.0f, ar = 0.0f;
            for (int k = 0; k < kTaps; k++)
            {
               al += mFir[k] * hl[k];
               ar += mFir[k] * hr[k];
            }
            l = al;
            r = ar;
         }
         const double yl = l - mDcXL + mDcK * mDcYL;
         const double yr = r - mDcXR + mDcK * mDcYR;
         mDcXL = l;
         mDcXR = r;
         mDcYL = std::fabs(yl) < 1e-20 ? 0.0 : yl;
         mDcYR = std::fabs(yr) < 1e-20 ? 0.0 : yr;
         const double mid = 0.5 * (mDcYL + mDcYR), side = 0.5 * (mDcYL - mDcYR) * width;
         double ol = (mid + side) * level, orr = (mid - side) * level;
         if (!std::isfinite(ol))
            ol = 0.0;
         if (!std::isfinite(orr))
            orr = 0.0;
         outL[f] = (float)std::clamp(ol, -4.0, 4.0);
         outR[f] = (float)std::clamp(orr, -4.0, 4.0);
      }
   }
}

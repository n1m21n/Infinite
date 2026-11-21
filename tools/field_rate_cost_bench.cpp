// Field rate-plan cost model at -O2 (docs/research/aliasing-aware-field, phase 2a).
// The self-test calibrates in whatever build runs it (Debug in dev), where the
// min-phase resampler is ~10x slower than shipped but the interpreter only ~2x,
// which over-charges every cut. This times the same two things optimised:
//   clang++ -std=c++20 -O2 -Isrc -Isrc/core -o /tmp/rate_cost_bench tools/field_rate_cost_bench.cpp
//   /tmp/rate_cost_bench
// and prints the INFINITE_FIELDRATE_COST value the rig accepts.
#include "core/field/SampleRuntime.h"
#include "audio/dsp/MinPhaseOversampler.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
using namespace Field;
// Best of 5 trials: other load on the machine only ever adds time.
static double Ns(const std::function<void()>& f, int n)
{
   for (int i = 0; i < n / 10; i++) f();
   double best = 1e30;
   for (int trial = 0; trial < 5; trial++)
   {
      auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < n; i++) f();
      best = std::min(best, 1e9 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / n);
   }
   return best;
}
static SampleInstr I(SampleOp op, int d, int a = 0, int b = 0, float imm = 0) { SampleInstr x; x.op = op; x.dst = (uint8_t)d; x.a = (uint8_t)a; x.b = (uint8_t)b; x.imm = imm; return x; }
// x = in * 0.5, then `reps` times x = x - x*x*x/3 (what the rig compiles).
static SampleProgram Chain(int reps)
{
   SampleProgram p; int r = 0;
   p.code.push_back(I(SampleOp::LoadIn, r++));
   p.code.push_back(I(SampleOp::LoadImm, r++, 0, 0, 0.5f));
   p.code.push_back(I(SampleOp::Mul, r, 0, 1)); int x = r++;
   for (int k = 0; k < reps; k++)
   {
      p.code.push_back(I(SampleOp::Mul, r, x, x)); int a = r++;
      p.code.push_back(I(SampleOp::Mul, r, a, x)); int b = r++;
      p.code.push_back(I(SampleOp::LoadImm, r, 0, 0, 3.0f)); int c = r++;
      p.code.push_back(I(SampleOp::Div, r, b, c)); int d = r++;
      p.code.push_back(I(SampleOp::Sub, r, x, d)); x = r++;
      if (r > 120) r = 3; // stay inside kSampleMaxRegs
   }
   p.outReg = x; p.numRegs = 128; p.valid = true; return p;
}
int main()
{
   float regs[kSampleMaxRegs] = {}; float st[1] = {}, pv[1] = {};
   SampleRuntimeInput rin; rin.sr = 48000; rin.stateCur = st; rin.stateNext = st; rin.paramVals = pv;
   volatile float sink = 0;
   SampleProgram s = Chain(0), l = Chain(24);
   auto t = [&](const SampleProgram& p) { return Ns([&] { rin.in += 1e-6f; if (rin.in > 0.5f) rin.in = -0.5f; sink = sink + RunSampleProgram(p, rin, regs); }, 2000000); };
   double ts = t(s), tl = t(l);
   double instr = (tl - ts) / (double)(l.code.size() - s.code.size()), run = ts - instr * s.code.size();
   printf("run %.2f ns, per instr %.3f ns (short %zu instr %.2f ns, long %zu instr %.2f ns)\n", run, instr, s.code.size(), ts, l.code.size(), tl);
   double up[4] = {}, down[4] = {};
   for (int fi = 1; fi <= 3; fi++)
   {
      MinPhaseOversampler os; os.Prepare(1 << fi, 48000.0);
      float hi[8] = {}; float x = 0.1f;
      up[fi] = Ns([&] { x = -x; os.Up(0, x, hi); }, 1000000);
      down[fi] = Ns([&] { sink = sink + os.Down(0, hi); }, 1000000);
   }
   printf("up 2/4/8/16/32 %.1f/%.1f/%.1f/%.1f/%.1f  down %.1f/%.1f/%.1f/%.1f/%.1f\n", up[1], up[2], up[3], up[3] + 8 * up[1], up[3] + 8 * up[2],
          down[1], down[2], down[3], down[3] + 8 * down[1], down[3] + 8 * down[2]);
   printf("INFINITE_FIELDRATE_COST=%.2f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n", run, instr, up[1], up[2], up[3], down[1], down[2], down[3]);
}
// The interpreter reads brain.* globals through this; the bench never does.
namespace Bci { float ReadBrainGlobal(int) { return 0.0f; } }

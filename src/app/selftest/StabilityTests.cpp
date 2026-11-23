// INFINITE_FIELDSTABDUMP: state matrices of Field sample kernels, for
// docs/research/stable-modulation (phase 2). Test code only, report only.
//
// A kernel whose state cells update linearly, next = A(theta) * state +
// B(theta) * in, is a linear parameter-varying system. This dumps A(theta)
// at the knob settings a Python certifier asks for, measured on the real
// interpreter (RunSampleProgram), so every op keeps its runtime meaning.
//
//   INFINITE_FIELDSTABDUMP=<dir>      sets of kernels: <dir>/<set>/<name>.field
//                                     with <name>.grid beside it: one knob
//                                     setting per line, values in the
//                                     kernel's param declaration order
//   INFINITE_FIELDSTABDUMP_OUT=<dir>  writes <out>/<set>/<name>.amat
//
// Column j of A is next(state = e_j, in = 0) - next(state = 0, in = 0). Two
// gates run before a kernel is dumped, at every grid point:
//   - linear in state: next(a x + b y) - next(0) = a (next(x) - next(0)) +
//     b (next(y) - next(0)) on random pairs (tolerance 1e-4, Float32);
//   - coefficients don't depend on `in`: the same columns with in = 0.37.
// A kernel that fails either gate is out of the study's scope; the .amat
// says so and holds no matrices. Delay lines, tables and inputs with no
// fixed value (n, rand, beat, notes, voice freq/gate, brain,
// declared inputs, spectral ops) are declined.
//
// .amat format: line 1 "status <word> n <cells> p <params> gate <max err>
// [reason...]", then one line per grid point: the knob values, then the n*n
// entries of A row-major (A[i][j] = next_i for state e_j).
#include "field/BackendRegister.h"
#include "field/SampleRuntime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace app
{
namespace
{
   std::string ReadText(const std::filesystem::path& p)
   {
      std::string s;
      FILE* fp = fopen(p.string().c_str(), "rb");
      if (fp == nullptr)
         return s;
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
         s.append(buf, n);
      fclose(fp);
      return s;
   }

   const char* UnsupportedOp(const Field::SampleProgram& prog)
   {
      using Field::SampleOp;
      if (!prog.delays.empty())
         return "delay line";
      if (!prog.tables.empty())
         return "table";
      for (const Field::SampleInstr& ins : prog.code)
         switch (ins.op)
         {
            case SampleOp::LoadN: return "sample counter n";
            case SampleOp::Rand: return "rand()";
            case SampleOp::Tick:
            case SampleOp::LoadBeat: return "beat clock";
            case SampleOp::LoadBrain: return "brain input";
            case SampleOp::EmitNote:
            case SampleOp::LoadNoteOn:
            case SampleOp::LoadNotePitch:
            case SampleOp::LoadNoteVel:
            case SampleOp::LoadNoteNum: return "note input";
            case SampleOp::LoadDeclaredIn: return "declared audio input";
            case SampleOp::LoadFreq:
            case SampleOp::LoadGate: return "voice freq/gate";
            case SampleOp::LoadBin:
            case SampleOp::LoadBins:
            case SampleOp::LoadBinHz:
            case SampleOp::LoadHop:
            case SampleOp::BinAt: return "spectral op";
            default: break;
         }
      return nullptr;
   }

   // One step of the kernel: state in, next state out (double in, Float32 inside).
   struct Stepper
   {
      const Field::SampleProgram& prog;
      float params[Field::kSampleMaxParams] = {};
      float regs[Field::kSampleMaxRegs] = {};
      float cur[Field::kSampleMaxStateCells] = {};
      float next[Field::kSampleMaxStateCells] = {};

      explicit Stepper(const Field::SampleProgram& p) : prog(p) {}

      std::vector<double> Step(const std::vector<double>& x, float in)
      {
         const int k = (int)prog.state.size();
         for (int i = 0; i < k; i++)
            cur[i] = next[i] = (float)x[(size_t)i];
         Field::SampleRuntimeInput rin;
         rin.in = in;
         rin.sr = 48000.0f;
         rin.paramVals = params;
         rin.stateCur = cur;
         rin.stateNext = next;
         (void)Field::RunSampleProgram(prog, rin, regs);
         std::vector<double> y((size_t)k);
         for (int i = 0; i < k; i++)
            y[(size_t)i] = next[i];
         return y;
      }

      // Columns next(e_j) - next(0) at input `in`, row-major A.
      std::vector<double> Matrix(float in)
      {
         const int k = (int)prog.state.size();
         const std::vector<double> zero((size_t)k, 0.0);
         const std::vector<double> f0 = Step(zero, in);
         std::vector<double> a((size_t)(k * k));
         for (int j = 0; j < k; j++)
         {
            std::vector<double> e = zero;
            e[(size_t)j] = 1.0;
            const std::vector<double> fj = Step(e, in);
            for (int i = 0; i < k; i++)
               a[(size_t)(i * k + j)] = fj[(size_t)i] - f0[(size_t)i];
         }
         return a;
      }
   };

   double Norm(const std::vector<double>& v)
   {
      double s = 0.0;
      for (double x : v)
         s += x * x;
      return std::sqrt(s);
   }
}

bool RunFieldStabDumpTest()
{
   const char* dir = getenv("INFINITE_FIELDSTABDUMP");
   const char* outDir = getenv("INFINITE_FIELDSTABDUMP_OUT");
   if (dir == nullptr || outDir == nullptr)
   {
      printf("FIELDSTABDUMP needs INFINITE_FIELDSTABDUMP=<dir> and INFINITE_FIELDSTABDUMP_OUT=<dir>\n");
      return false;
   }
   std::vector<std::filesystem::path> files;
   std::error_code ec;
   for (const auto& set : std::filesystem::directory_iterator(dir, ec))
      if (set.is_directory())
         for (const auto& f : std::filesystem::directory_iterator(set.path(), ec))
            if (f.path().extension() == ".field")
               files.push_back(f.path());
   std::sort(files.begin(), files.end());
   std::mt19937 rng(20261123u);
   std::uniform_real_distribution<double> uni(-1.0, 1.0);
   int dumped = 0, outOfScope = 0, failed = 0;
   for (const auto& path : files)
   {
      const std::string set = path.parent_path().filename().string(), name = path.stem().string();
      const std::filesystem::path outSet = std::filesystem::path(outDir) / set;
      std::filesystem::create_directories(outSet, ec);
      FILE* out = fopen((outSet / (name + ".amat")).string().c_str(), "w");
      if (out == nullptr)
      {
         failed++;
         continue;
      }
      Field::SampleProgram prog;
      Field::FieldError ferr;
      if (!Field::CompileSampleProgram(ReadText(path), nullptr, prog, ferr))
      {
         fprintf(out, "status compile n 0 p 0 gate 0 %s\n", ferr.message.c_str());
         printf("FIELDSTABDUMP %s/%-28s does not compile: %s\n", set.c_str(), name.c_str(), ferr.message.c_str());
         fclose(out);
         failed++;
         continue;
      }
      const int k = (int)prog.state.size(), np = (int)prog.params.size();
      if (const char* why = UnsupportedOp(prog))
      {
         fprintf(out, "status unsupported n %d p %d gate 0 %s\n", k, np, why);
         printf("FIELDSTABDUMP %s/%-28s declined: %s\n", set.c_str(), name.c_str(), why);
         fclose(out);
         outOfScope++;
         continue;
      }
      std::vector<std::vector<double>> grid;
      {
         std::istringstream gs(ReadText(path.parent_path() / (name + ".grid")));
         std::string line;
         while (std::getline(gs, line))
         {
            std::istringstream ls(line);
            std::vector<double> v;
            double x;
            while (ls >> x)
               v.push_back(x);
            if ((int)v.size() == np)
               grid.push_back(v);
         }
      }
      Stepper st(prog);
      std::vector<std::vector<double>> mats;
      double gateErr = 0.0;
      std::string reason;
      for (const std::vector<double>& g : grid)
      {
         for (int i = 0; i < np; i++)
            st.params[i] = (float)g[(size_t)i];
         const std::vector<double> a = st.Matrix(0.0f);
         // linear in state
         const std::vector<double> zero((size_t)k, 0.0);
         const std::vector<double> f0 = st.Step(zero, 0.0f);
         for (int t = 0; t < 2; t++)
         {
            std::vector<double> x((size_t)k), y((size_t)k), xy((size_t)k);
            const double ca = uni(rng), cb = uni(rng);
            for (int i = 0; i < k; i++)
            {
               x[(size_t)i] = uni(rng);
               y[(size_t)i] = uni(rng);
               xy[(size_t)i] = ca * x[(size_t)i] + cb * y[(size_t)i];
            }
            const std::vector<double> fx = st.Step(x, 0.0f), fy = st.Step(y, 0.0f), fxy = st.Step(xy, 0.0f);
            std::vector<double> d((size_t)k), ref((size_t)k);
            for (int i = 0; i < k; i++)
            {
               ref[(size_t)i] = ca * (fx[(size_t)i] - f0[(size_t)i]) + cb * (fy[(size_t)i] - f0[(size_t)i]);
               d[(size_t)i] = (fxy[(size_t)i] - f0[(size_t)i]) - ref[(size_t)i];
            }
            const double e = Norm(d) / std::max(1.0, Norm(ref));
            gateErr = std::max(gateErr, e);
            if (e > 1e-4 && reason.empty())
               reason = "not linear in state";
         }
         // coefficients independent of `in`
         const std::vector<double> ai = st.Matrix(0.37f);
         std::vector<double> d(a.size());
         for (size_t i = 0; i < a.size(); i++)
            d[i] = ai[i] - a[i];
         const double e = Norm(d) / std::max(1.0, Norm(a));
         gateErr = std::max(gateErr, e);
         if (e > 1e-4 && reason.empty())
            reason = "coefficients depend on in";
         mats.push_back(a);
      }
      if (grid.empty())
         reason = "no .grid file or no line with one value per param";
      if (!reason.empty())
      {
         fprintf(out, "status out-of-scope n %d p %d gate %.3g %s\n", k, np, gateErr, reason.c_str());
         printf("FIELDSTABDUMP %s/%-28s out of scope: %s (gate err %.3g)\n", set.c_str(), name.c_str(),
                reason.c_str(), gateErr);
         fclose(out);
         outOfScope++;
         continue;
      }
      fprintf(out, "status ok n %d p %d gate %.3g\n", k, np, gateErr);
      for (size_t r = 0; r < grid.size(); r++)
      {
         for (double v : grid[r])
            fprintf(out, "%.9g ", v);
         for (size_t i = 0; i < mats[r].size(); i++)
            fprintf(out, i + 1 < mats[r].size() ? "%.9g " : "%.9g\n", mats[r][i]);
      }
      fclose(out);
      dumped++;
   }
   printf("FIELDSTABDUMP %zu kernels: %d dumped, %d out of scope, %d failed\n", files.size(), dumped, outOfScope,
          failed);
   return failed == 0;
}
}

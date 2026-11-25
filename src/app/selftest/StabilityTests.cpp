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
//   INFINITE_FIELDSTABDUMP_SAT=1      Lur'e form for saturating kernels instead
//                                     (docs/research/saturating-loops): <name>.lmat,
//                                     see DumpLure below
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

   // ---- Saturating loops (docs/research/saturating-loops, phase 2) ---------------------------
   // INFINITE_FIELDSTABDUMP_SAT=1 switches the dump to Lur'e form, x+ = A x + B w, y = C x + D w,
   // w_i = phi_i(y_i). Each recognised saturator whose argument depends on state is cut: its result
   // becomes an injected value w_i (an extra param slot) and its argument y_i is stored to an extra
   // state cell. Shapes (README section 8): clamp(y, -c, c); min(y, c) / max(y, -c), c > 0;
   // y / (1 + abs(y)); (e - 1) / (e + 1) with e = exp(2 * y); sin(y); abs(y). Anything nonlinear the
   // matcher misses fails the linearity gate on the cut kernel. Writes <name>.lmat:
   //   "status <word> n <k> m <m> p <np> gate <err> shapes <s1,s2,...>", then per grid point the knob
   //   values and A (k*k), B (k*m), C (m*k), D (m*m), each row-major.
   struct Saturator
   {
      int root = 0;      // instruction whose result is phi(y)
      uint8_t y = 0;     // register holding the argument
      std::string shape;
   };

   std::vector<Saturator> FindSaturators(const Field::SampleProgram& prog)
   {
      using Field::SampleOp;
      const int nr = Field::kSampleMaxRegs;
      std::vector<int> def((size_t)nr, -1);
      std::vector<char> isConst((size_t)nr, 0), tainted((size_t)nr, 0);
      std::vector<float> val((size_t)nr, 0.0f);
      for (int i = 0; i < (int)prog.code.size(); i++)
      {
         const Field::SampleInstr& ins = prog.code[(size_t)i];
         if (ins.op == SampleOp::StoreState || ins.op == SampleOp::Nop)
            continue;
         def[ins.dst] = i;
         const uint8_t a = ins.a, b = ins.b, c = ins.c;
         switch (ins.op)
         {
            case SampleOp::LoadImm: isConst[ins.dst] = 1; val[ins.dst] = ins.imm; break;
            case SampleOp::LoadState: tainted[ins.dst] = 1; break;
            case SampleOp::LoadIn: case SampleOp::LoadSr: case SampleOp::LoadParam: break;
            case SampleOp::Move: case SampleOp::Neg: case SampleOp::Sin: case SampleOp::Cos: case SampleOp::Tan:
            case SampleOp::Sqrt: case SampleOp::Abs: case SampleOp::Floor: case SampleOp::Ceil: case SampleOp::Exp:
            case SampleOp::Log: case SampleOp::LogNot:
               tainted[ins.dst] = tainted[a];
               if (isConst[a] && (ins.op == SampleOp::Neg || ins.op == SampleOp::Move))
               {
                  isConst[ins.dst] = 1;
                  val[ins.dst] = ins.op == SampleOp::Neg ? -val[a] : val[a];
               }
               break;
            case SampleOp::Select: case SampleOp::Clamp:
               tainted[ins.dst] = tainted[a] || tainted[b] || tainted[c];
               break;
            default:
               tainted[ins.dst] = tainted[a] || tainted[b];
               if (isConst[a] && isConst[b])
               {
                  float r = 0.0f;
                  bool ok = true;
                  switch (ins.op)
                  {
                     case SampleOp::Add: r = val[a] + val[b]; break;
                     case SampleOp::Sub: r = val[a] - val[b]; break;
                     case SampleOp::Mul: r = val[a] * val[b]; break;
                     case SampleOp::Div: r = val[b] != 0.0f ? val[a] / val[b] : 0.0f; break;
                     default: ok = false; break;
                  }
                  if (ok)
                  {
                     isConst[ins.dst] = 1;
                     val[ins.dst] = r;
                  }
               }
               break;
         }
      }
      auto opOf = [&](uint8_t r) { return def[r] >= 0 ? prog.code[(size_t)def[r]].op : SampleOp::Nop; };
      auto insOf = [&](uint8_t r) -> const Field::SampleInstr& { return prog.code[(size_t)def[r]]; };
      auto constIs = [&](uint8_t r, float v) { return isConst[r] && val[r] == v; };
      // the other operand of a commutative op when one operand is the constant v, else -1
      auto otherThan = [&](const Field::SampleInstr& ins, float v) -> int {
         if (constIs(ins.a, v)) return ins.b;
         if (constIs(ins.b, v)) return ins.a;
         return -1;
      };
      std::vector<Saturator> sats;
      std::vector<char> consumed(prog.code.size(), 0);
      // pass 1: the division forms (soft clip, tanh); their abs is part of the shape
      for (int i = 0; i < (int)prog.code.size(); i++)
      {
         const Field::SampleInstr& ins = prog.code[(size_t)i];
         if (ins.op != SampleOp::Div || opOf(ins.b) != SampleOp::Add)
            continue;
         const Field::SampleInstr& den = insOf(ins.b);
         const int dx = otherThan(den, 1.0f);
         if (dx < 0)
            continue;
         // soft: y / (1 + abs(y))
         if (opOf((uint8_t)dx) == SampleOp::Abs && insOf((uint8_t)dx).a == ins.a)
         {
            if (tainted[ins.a])
            {
               sats.push_back({ i, ins.a, "soft" });
               consumed[(size_t)def[(uint8_t)dx]] = 1;
            }
            continue;
         }
         // tanh: (e - 1) / (e + 1), e = exp(2 * y)
         if (opOf(ins.a) != SampleOp::Sub || opOf((uint8_t)dx) != SampleOp::Exp)
            continue;
         const Field::SampleInstr& num = insOf(ins.a);
         if (num.a != (uint8_t)dx || !constIs(num.b, 1.0f))
            continue;
         const uint8_t z = insOf((uint8_t)dx).a;
         if (opOf(z) != SampleOp::Mul)
            continue;
         const int y = otherThan(insOf(z), 2.0f);
         if (y >= 0 && tainted[(uint8_t)y])
            sats.push_back({ i, (uint8_t)y, "tanh" });
      }
      // pass 2: single-op shapes
      for (int i = 0; i < (int)prog.code.size(); i++)
      {
         const Field::SampleInstr& ins = prog.code[(size_t)i];
         if (consumed[(size_t)i])
            continue;
         switch (ins.op)
         {
            case SampleOp::Sin:
               if (tainted[ins.a]) sats.push_back({ i, ins.a, "sin" });
               break;
            case SampleOp::Abs:
               if (tainted[ins.a]) sats.push_back({ i, ins.a, "abs" });
               break;
            case SampleOp::Clamp:
               if (tainted[ins.a] && !tainted[ins.b] && !tainted[ins.c] && isConst[ins.b] && isConst[ins.c] &&
                   val[ins.c] > 0.0f && val[ins.b] == -val[ins.c])
                  sats.push_back({ i, ins.a, "clamp" });
               break;
            case SampleOp::Min:
            case SampleOp::Max:
            {
               const float sign = ins.op == SampleOp::Min ? 1.0f : -1.0f;
               for (int side = 0; side < 2; side++)
               {
                  const uint8_t x = side == 0 ? ins.a : ins.b, k = side == 0 ? ins.b : ins.a;
                  if (tainted[x] && isConst[k] && sign * val[k] > 0.0f)
                  {
                     sats.push_back({ i, x, "clip1" });
                     break;
                  }
               }
               break;
            }
            default: break;
         }
      }
      std::sort(sats.begin(), sats.end(), [](const Saturator& p, const Saturator& q) { return p.root < q.root; });
      return sats;
   }

   // The kernel with every saturator cut: result -> param slot np + i, argument -> state cell k + i.
   Field::SampleProgram CutProgram(const Field::SampleProgram& prog, const std::vector<Saturator>& sats)
   {
      Field::SampleProgram cut = prog;
      cut.code.clear();
      const int k = (int)prog.state.size(), np = (int)prog.params.size();
      for (int i = 0; i < (int)prog.code.size(); i++)
      {
         int s = -1;
         for (int j = 0; j < (int)sats.size(); j++)
            if (sats[(size_t)j].root == i)
               s = j;
         if (s < 0)
         {
            cut.code.push_back(prog.code[(size_t)i]);
            continue;
         }
         Field::SampleInstr ld;
         ld.op = Field::SampleOp::LoadParam;
         ld.dst = prog.code[(size_t)i].dst;
         ld.a = (uint8_t)(np + s);
         cut.code.push_back(ld);
         Field::SampleInstr st;
         st.op = Field::SampleOp::StoreState;
         st.a = (uint8_t)(k + s);
         st.b = sats[(size_t)s].y;
         cut.code.push_back(st);
      }
      for (size_t s = 0; s < sats.size(); s++)
      {
         cut.params.push_back(Field::SampleParamSlot{});
         cut.state.push_back(Field::SampleStateInit{});
      }
      return cut;
   }

   // One step of the cut kernel: (x, w) in, (x+, y) out.
   struct CutStepper
   {
      const Field::SampleProgram& prog;
      int k, m, np;
      float params[Field::kSampleMaxParams] = {};
      float regs[Field::kSampleMaxRegs] = {};
      float cur[Field::kSampleMaxStateCells] = {};
      float next[Field::kSampleMaxStateCells] = {};

      CutStepper(const Field::SampleProgram& p, int k_, int m_, int np_) : prog(p), k(k_), m(m_), np(np_) {}

      // u = [x (k), w (m)] -> [x+ (k), y (m)]
      std::vector<double> Step(const std::vector<double>& u, float in)
      {
         for (int i = 0; i < k + m; i++)
            cur[i] = next[i] = 0.0f;
         for (int i = 0; i < k; i++)
            cur[i] = next[i] = (float)u[(size_t)i];
         for (int s = 0; s < m; s++)
            params[np + s] = (float)u[(size_t)(k + s)];
         Field::SampleRuntimeInput rin;
         rin.in = in;
         rin.sr = 48000.0f;
         rin.paramVals = params;
         rin.stateCur = cur;
         rin.stateNext = next;
         (void)Field::RunSampleProgram(prog, rin, regs);
         std::vector<double> v((size_t)(k + m));
         for (int i = 0; i < k + m; i++)
            v[(size_t)i] = next[i];
         return v;
      }

      // Full (k+m) x (k+m) map [[A, B], [C, D]], row-major, columns v(e_j) - v(0).
      std::vector<double> Matrix(float in)
      {
         const int t = k + m;
         const std::vector<double> zero((size_t)t, 0.0);
         const std::vector<double> f0 = Step(zero, in);
         std::vector<double> a((size_t)(t * t));
         for (int j = 0; j < t; j++)
         {
            std::vector<double> e = zero;
            e[(size_t)j] = 1.0;
            const std::vector<double> fj = Step(e, in);
            for (int i = 0; i < t; i++)
               a[(size_t)(i * t + j)] = fj[(size_t)i] - f0[(size_t)i];
         }
         return a;
      }
   };

   // Returns "" when dumped, else the out-of-scope reason; fills the .lmat text.
   std::string DumpLure(const Field::SampleProgram& prog, const std::vector<std::vector<double>>& grid,
                        std::mt19937& rng, std::string& text, double& gateErr)
   {
      const std::vector<Saturator> sats = FindSaturators(prog);
      const int k = (int)prog.state.size(), np = (int)prog.params.size(), m = (int)sats.size();
      std::string shapes;
      for (const Saturator& s : sats)
         shapes += (shapes.empty() ? "" : ",") + s.shape;
      if (shapes.empty())
         shapes = "-";
      char head[256];
      if (k + m > Field::kSampleMaxStateCells || np + m > Field::kSampleMaxParams)
      {
         snprintf(head, sizeof(head), "n %d m %d p %d gate 0 shapes %s too many cells", k, m, np, shapes.c_str());
         text = head;
         return "too many cells";
      }
      const Field::SampleProgram cut = CutProgram(prog, sats);
      CutStepper st(cut, k, m, np);
      std::uniform_real_distribution<double> uni(-1.0, 1.0);
      const int t = k + m;
      std::string reason, body;
      gateErr = 0.0;
      for (const std::vector<double>& g : grid)
      {
         for (int i = 0; i < np; i++)
            st.params[i] = (float)g[(size_t)i];
         const std::vector<double> a = st.Matrix(0.0f);
         const std::vector<double> zero((size_t)t, 0.0);
         const std::vector<double> f0 = st.Step(zero, 0.0f);
         for (int r = 0; r < 8; r++)  // linearity gate on the cut kernel, jointly in (x, w)
         {
            std::vector<double> x((size_t)t), y((size_t)t), xy((size_t)t);
            const double ca = uni(rng), cb = uni(rng);
            for (int i = 0; i < t; i++)
            {
               x[(size_t)i] = uni(rng);
               y[(size_t)i] = uni(rng);
               xy[(size_t)i] = ca * x[(size_t)i] + cb * y[(size_t)i];
            }
            const std::vector<double> fx = st.Step(x, 0.0f), fy = st.Step(y, 0.0f), fxy = st.Step(xy, 0.0f);
            std::vector<double> d((size_t)t), ref((size_t)t);
            for (int i = 0; i < t; i++)
            {
               ref[(size_t)i] = ca * (fx[(size_t)i] - f0[(size_t)i]) + cb * (fy[(size_t)i] - f0[(size_t)i]);
               d[(size_t)i] = (fxy[(size_t)i] - f0[(size_t)i]) - ref[(size_t)i];
            }
            const double e = Norm(d) / std::max(1.0, Norm(ref));
            gateErr = std::max(gateErr, e);
            if (e > 1e-5 && reason.empty())
               reason = "not linear once cut (unrecognised saturator?)";
         }
         const std::vector<double> ai = st.Matrix(0.37f);
         std::vector<double> d(a.size());
         for (size_t i = 0; i < a.size(); i++)
            d[i] = ai[i] - a[i];
         const double e = Norm(d) / std::max(1.0, Norm(a));
         gateErr = std::max(gateErr, e);
         if (e > 1e-5 && reason.empty())
            reason = "coefficients depend on in";
         char num[32];
         for (double v : g)
         {
            snprintf(num, sizeof(num), "%.9g ", v);
            body += num;
         }
         // blocks A, B, C, D out of [[A, B], [C, D]]
         const int r0[4] = { 0, 0, k, k }, c0[4] = { 0, k, 0, k }, rn[4] = { k, k, m, m }, cn[4] = { k, m, k, m };
         std::string row;
         for (int b = 0; b < 4; b++)
            for (int i = 0; i < rn[b]; i++)
               for (int j = 0; j < cn[b]; j++)
               {
                  snprintf(num, sizeof(num), "%.9g ", a[(size_t)((r0[b] + i) * t + c0[b] + j)]);
                  row += num;
               }
         if (!row.empty())
            row.pop_back();
         body += row + "\n";
      }
      if (grid.empty())
         reason = "no .grid file or no line with one value per param";
      snprintf(head, sizeof(head), "n %d m %d p %d gate %.3g shapes %s", k, m, np, gateErr, shapes.c_str());
      text = std::string(head) + (reason.empty() ? "\n" + body : " " + reason);
      return reason;
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
   const bool satMode = getenv("INFINITE_FIELDSTABDUMP_SAT") != nullptr;  // Lur'e form, .lmat
   int dumped = 0, outOfScope = 0, failed = 0;
   for (const auto& path : files)
   {
      const std::string set = path.parent_path().filename().string(), name = path.stem().string();
      const std::filesystem::path outSet = std::filesystem::path(outDir) / set;
      std::filesystem::create_directories(outSet, ec);
      FILE* out = fopen((outSet / (name + (satMode ? ".lmat" : ".amat"))).string().c_str(), "w");
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
      if (satMode)
      {
         std::string text;
         double err = 0.0;
         const std::string why = DumpLure(prog, grid, rng, text, err);
         fprintf(out, "status %s %s\n", why.empty() ? "ok" : "out-of-scope", text.c_str());
         printf("FIELDSTABDUMP %s/%-28s %s\n", set.c_str(), name.c_str(), why.empty() ? "ok" : why.c_str());
         fclose(out);
         (why.empty() ? dumped : outOfScope)++;
         continue;
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

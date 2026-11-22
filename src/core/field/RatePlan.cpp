#include "RatePlan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>

// See RatePlan.h. Main thread only; allocates freely.
namespace Field
{
   namespace
   {
      // Register operands an instruction reads.
      int Reads(const SampleInstr& i, int r[4])
      {
         switch (i.op)
         {
            case SampleOp::Tick:
            case SampleOp::Deg:
            case SampleOp::Move:
            case SampleOp::Neg:
            case SampleOp::LogNot:
            case SampleOp::Sin:
            case SampleOp::Cos:
            case SampleOp::Tan:
            case SampleOp::Sqrt:
            case SampleOp::Abs:
            case SampleOp::Floor:
            case SampleOp::Ceil:
            case SampleOp::Exp:
            case SampleOp::Log:
               r[0] = i.a;
               return 1;
            case SampleOp::StoreState:
            case SampleOp::Delay:
            case SampleOp::LoadTable:
            case SampleOp::BinAt:
               r[0] = i.b;
               return 1;
            case SampleOp::StoreTable:
               r[0] = i.b;
               r[1] = i.c;
               return 2;
            case SampleOp::EmitNote:
               r[0] = i.a;
               r[1] = i.b;
               r[2] = i.c;
               r[3] = i.dst;
               return 4;
            case SampleOp::PhaseShape:
            case SampleOp::Add:
            case SampleOp::Sub:
            case SampleOp::Mul:
            case SampleOp::Div:
            case SampleOp::Mod:
            case SampleOp::Pow:
            case SampleOp::Lt:
            case SampleOp::Le:
            case SampleOp::Gt:
            case SampleOp::Ge:
            case SampleOp::Eq:
            case SampleOp::Ne:
            case SampleOp::LogAnd:
            case SampleOp::LogOr:
            case SampleOp::Min:
            case SampleOp::Max:
               r[0] = i.a;
               r[1] = i.b;
               return 2;
            case SampleOp::Select:
            case SampleOp::Clamp:
               r[0] = i.a;
               r[1] = i.b;
               r[2] = i.c;
               return 3;
            default:
               return 0;
         }
      }

      bool Writes(const SampleInstr& i)
      {
         return i.op != SampleOp::Nop && i.op != SampleOp::StoreState && i.op != SampleOp::StoreTable &&
                i.op != SampleOp::EmitNote;
      }

      // Same result on every sample of a block when its operands are.
      bool PureOp(SampleOp op)
      {
         switch (op)
         {
            case SampleOp::LoadImm:
            case SampleOp::LoadParam:
            case SampleOp::LoadSr:
            case SampleOp::Move:
            case SampleOp::Add:
            case SampleOp::Sub:
            case SampleOp::Mul:
            case SampleOp::Div:
            case SampleOp::Mod:
            case SampleOp::Pow:
            case SampleOp::Neg:
            case SampleOp::Lt:
            case SampleOp::Le:
            case SampleOp::Gt:
            case SampleOp::Ge:
            case SampleOp::Eq:
            case SampleOp::Ne:
            case SampleOp::LogAnd:
            case SampleOp::LogOr:
            case SampleOp::LogNot:
            case SampleOp::Select:
            case SampleOp::Sin:
            case SampleOp::Cos:
            case SampleOp::Tan:
            case SampleOp::Sqrt:
            case SampleOp::Abs:
            case SampleOp::Floor:
            case SampleOp::Ceil:
            case SampleOp::Exp:
            case SampleOp::Log:
            case SampleOp::Min:
            case SampleOp::Max:
            case SampleOp::Clamp:
            case SampleOp::PhaseShape:
               return true;
            default:
               return false;
         }
      }

      // Data-flow facts of a prepared program.
      struct Facts
      {
         int n = 0;
         std::vector<std::array<int, 4>> reg;    // register of each read operand
         std::vector<std::array<int, 4>> writer; // its writer (-1: never written, reads 0)
         std::vector<int> nReads;
         std::vector<char> isConst;
         int outWriter = -1;

         explicit Facts(const SampleProgram& p)
         {
            n = (int)p.code.size();
            reg.resize((size_t)n);
            writer.resize((size_t)n);
            nReads.resize((size_t)n);
            isConst.resize((size_t)n);
            std::vector<int> last((size_t)kSampleMaxRegs, -1);
            for (int i = 0; i < n; i++)
            {
               int r[4] = {};
               const int k = Reads(p.code[(size_t)i], r);
               nReads[(size_t)i] = k;
               bool c = PureOp(p.code[(size_t)i].op);
               for (int j = 0; j < k; j++)
               {
                  reg[(size_t)i][(size_t)j] = r[j];
                  const int w = last[(size_t)r[j]];
                  writer[(size_t)i][(size_t)j] = w;
                  if (w >= 0 && !isConst[(size_t)w])
                     c = false;
               }
               isConst[(size_t)i] = c;
               if (Writes(p.code[(size_t)i]))
                  last[p.code[(size_t)i].dst] = i;
            }
            if (p.outReg >= 0)
               outWriter = last[(size_t)p.outReg];
         }

         bool ConstWriter(int w) const { return w < 0 || isConst[(size_t)w]; }
      };

      bool Nonlinear(const SampleProgram& p, const Facts& f, int i)
      {
         if (i < 0 || i >= f.n || f.isConst[(size_t)i])
            return false;
         switch (p.code[(size_t)i].op)
         {
            case SampleOp::Sin:
            case SampleOp::Cos:
            case SampleOp::Tan:
            case SampleOp::Exp:
            case SampleOp::Log:
            case SampleOp::Pow:
            case SampleOp::Sqrt:
            case SampleOp::Abs:
            case SampleOp::Floor:
            case SampleOp::Ceil:
            case SampleOp::Min:
            case SampleOp::Max:
            case SampleOp::Clamp:
            case SampleOp::Mod:
            case SampleOp::Select:
            case SampleOp::Lt:
            case SampleOp::Le:
            case SampleOp::Gt:
            case SampleOp::Ge:
            case SampleOp::Eq:
            case SampleOp::Ne:
            case SampleOp::LogAnd:
            case SampleOp::LogOr:
            case SampleOp::LogNot:
            case SampleOp::PhaseShape:
               return true;
            case SampleOp::Mul:
               return !f.ConstWriter(f.writer[(size_t)i][0]) && !f.ConstWriter(f.writer[(size_t)i][1]);
            case SampleOp::Div:
               return !f.ConstWriter(f.writer[(size_t)i][1]);
            default:
               return false;
         }
      }

      // Per state cell: the span of instruction positions its loop covers
      // (its loads and the writer of the value it stores).
      std::vector<std::pair<int, int>> StateSpans(const SampleProgram& p, const Facts& f)
      {
         std::vector<std::pair<int, int>> span(p.state.size(), { 1 << 30, -1 });
         for (int i = 0; i < f.n; i++)
         {
            const SampleInstr& ins = p.code[(size_t)i];
            int pos = -1;
            if (ins.op == SampleOp::LoadState)
               pos = i;
            else if (ins.op == SampleOp::StoreState && !f.ConstWriter(f.writer[(size_t)i][0]))
               pos = f.writer[(size_t)i][0];
            if (pos < 0 || ins.a >= p.state.size())
               continue;
            span[ins.a].first = std::min(span[ins.a].first, pos);
            span[ins.a].second = std::max(span[ins.a].second, pos);
         }
         return span;
      }

      int CrossingAt(const SampleProgram& p, const Facts& f, int cut)
      {
         std::set<int> ws;
         for (int i = cut; i < f.n; i++)
         {
            if (p.code[(size_t)i].op == SampleOp::StoreState)
               continue; // moves to the segment of its writer
            for (int j = 0; j < f.nReads[(size_t)i]; j++)
            {
               const int w = f.writer[(size_t)i][(size_t)j];
               if (w >= 0 && w < cut && !f.isConst[(size_t)w])
                  ws.insert(w);
            }
         }
         if (f.outWriter >= 0 && f.outWriter < cut && !f.isConst[(size_t)f.outWriter])
            ws.insert(f.outWriter);
         return (int)ws.size();
      }

      bool FactorDependent(const SampleProgram& p)
      {
         if (!p.state.empty())
            return true;
         for (const SampleInstr& i : p.code)
            if (i.op == SampleOp::LoadSr)
               return true;
         return false;
      }

      constexpr int kPlanFactors[] = { 1, 2, 4, 8, 16, 32 };
      constexpr int kNumPlanFactors = 6;
   }

   int FactorIndex(int factor)
   {
      switch (factor)
      {
         case 1: return 0;
         case 2: return 1;
         case 4: return 2;
         case 8: return 3;
         case 16: return 4;
         case 32: return 5;
         default: return -1;
      }
   }

   bool PrepareForSplit(const SampleProgram& prog, SampleProgram& out, int& inReg, std::string& why)
   {
      out = prog;
      inReg = -1;
      bool readsIn = false;
      for (const SampleInstr& i : prog.code)
         readsIn |= i.op == SampleOp::LoadIn;
      if (!readsIn)
         return true;
      if (prog.numRegs >= kSampleMaxRegs || (int)prog.code.size() >= kSampleMaxInstr)
         return why = "no room for the input register", false;
      inReg = prog.numRegs;
      out.numRegs = prog.numRegs + 1;
      out.code.clear();
      SampleInstr load;
      load.op = SampleOp::LoadIn;
      load.dst = (uint8_t)inReg;
      out.code.push_back(load);
      for (SampleInstr i : prog.code)
      {
         if (i.op == SampleOp::LoadIn)
         {
            i.op = SampleOp::Move;
            i.a = (uint8_t)inReg;
         }
         out.code.push_back(i);
      }
      return true;
   }

   std::vector<int> CandidateCuts(const SampleProgram& p, int maxCrossing, int maxCandidates)
   {
      const Facts f(p);
      const auto spans = StateSpans(p, f);
      int firstStore = f.n;
      for (int i = 0; i < f.n; i++)
         if (p.code[(size_t)i].op == SampleOp::StoreState)
         {
            firstStore = i;
            break;
         }
      std::vector<std::pair<int, int>> found; // (cut, crossing)
      for (int cut = 1; cut < firstStore; cut++)
      {
         if (!Nonlinear(p, f, cut - 1) && !Nonlinear(p, f, cut))
            continue;
         bool ok = true;
         for (const auto& s : spans)
            if (s.second >= 0 && s.first < cut && cut <= s.second)
               ok = false;
         if (!ok)
            continue;
         const int x = CrossingAt(p, f, cut);
         if (x < 1 || x > maxCrossing)
            continue;
         found.push_back({ cut, x });
      }
      if ((int)found.size() > maxCandidates)
      {
         std::vector<std::pair<int, int>> ones;
         for (const auto& c : found)
            if (c.second == 1)
               ones.push_back(c);
         if ((int)ones.size() >= maxCandidates || ones.size() >= found.size() / 2)
            found = ones;
      }
      std::vector<int> cuts;
      if ((int)found.size() > maxCandidates)
      {
         for (int k = 0; k < maxCandidates; k++)
            cuts.push_back(found[(size_t)((double)k * (double)(found.size() - 1) / (double)(maxCandidates - 1) + 0.5)].first);
         cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
      }
      else
         for (const auto& c : found)
            cuts.push_back(c.first);
      return cuts;
   }

   bool SplitProgram(const SampleProgram& p, const std::vector<int>& cuts, std::vector<RateSegment>& segs,
                     std::string& why)
   {
      const Facts f(p);
      const int m = (int)cuts.size() + 1;
      std::vector<int> bound;
      bound.push_back(0);
      for (int c : cuts)
         bound.push_back(c);
      bound.push_back(f.n);
      for (int s = 0; s < m; s++)
         if (bound[(size_t)s + 1] <= bound[(size_t)s])
            return why = "empty segment", false;

      auto posSeg = [&](int i) {
         int s = 0;
         while (s + 1 < m && i >= bound[(size_t)s + 1])
            s++;
         return s;
      };
      // First LoadState of each cell, for StoreStates of a constant.
      std::vector<int> firstLoad(p.state.size(), -1);
      for (int i = 0; i < f.n; i++)
         if (p.code[(size_t)i].op == SampleOp::LoadState && p.code[(size_t)i].a < p.state.size() &&
             firstLoad[p.code[(size_t)i].a] < 0)
            firstLoad[p.code[(size_t)i].a] = i;
      std::vector<int> segOf((size_t)f.n);
      for (int i = 0; i < f.n; i++)
      {
         const SampleInstr& ins = p.code[(size_t)i];
         if (ins.op == SampleOp::StoreState)
         {
            const int w = f.writer[(size_t)i][0];
            if (!f.ConstWriter(w))
               segOf[(size_t)i] = posSeg(w);
            else if (ins.a < p.state.size() && firstLoad[ins.a] >= 0)
               segOf[(size_t)i] = posSeg(firstLoad[ins.a]);
            else
               segOf[(size_t)i] = posSeg(i);
         }
         else
            segOf[(size_t)i] = posSeg(i);
      }
      // A cell's loads and its store live in one segment.
      std::vector<int> cellSeg(p.state.size(), -1);
      for (int i = 0; i < f.n; i++)
      {
         const SampleInstr& ins = p.code[(size_t)i];
         if ((ins.op == SampleOp::LoadState || ins.op == SampleOp::StoreState) && ins.a < p.state.size())
         {
            int& cs = cellSeg[ins.a];
            if (cs >= 0 && cs != segOf[(size_t)i])
               return why = "state loop across a cut", false;
            cs = segOf[(size_t)i];
         }
      }

      // Needs: per segment, values from earlier segments and constants to recompute.
      std::vector<std::set<int>> inSet((size_t)m), outSet((size_t)m);
      std::vector<std::set<int>> remat((size_t)m);
      std::function<void(int, int)> addRemat = [&](int s, int w) {
         if (w < 0 || !remat[(size_t)s].insert(w).second)
            return;
         for (int j = 0; j < f.nReads[(size_t)w]; j++)
            addRemat(s, f.writer[(size_t)w][(size_t)j]);
      };
      auto need = [&](int s, int r, int w) -> bool {
         if (w < 0)
            return true; // never written: reads 0 in a fresh register file too
         if (f.isConst[(size_t)w])
         {
            if (segOf[(size_t)w] != s)
               addRemat(s, w);
            return true;
         }
         const int sw = segOf[(size_t)w];
         if (sw == s)
            return true;
         if (sw > s)
            return false;
         outSet[(size_t)sw].insert(r);
         for (int t = sw + 1; t < s; t++)
         {
            inSet[(size_t)t].insert(r);
            outSet[(size_t)t].insert(r);
         }
         inSet[(size_t)s].insert(r);
         return true;
      };
      for (int i = 0; i < f.n; i++)
         for (int j = 0; j < f.nReads[(size_t)i]; j++)
            if (!need(segOf[(size_t)i], f.reg[(size_t)i][(size_t)j], f.writer[(size_t)i][(size_t)j]))
               return why = "value read before it is computed", false;
      if (p.outReg >= 0 && !need(m - 1, p.outReg, f.outWriter))
         return why = "output read before it is computed", false;
      if (!inSet[0].empty())
         return why = "first segment has inputs", false;

      segs.assign((size_t)m, RateSegment());
      for (int s = 0; s < m; s++)
      {
         RateSegment& seg = segs[(size_t)s];
         seg.first = bound[(size_t)s];
         seg.end = bound[(size_t)s + 1];
         seg.inRegs.assign(inSet[(size_t)s].begin(), inSet[(size_t)s].end());
         seg.outRegs.assign(outSet[(size_t)s].begin(), outSet[(size_t)s].end());
         SampleProgram& sp = seg.prog;
         sp.numRegs = p.numRegs;
         sp.params = p.params;
         sp.outReg = (s == m - 1) ? p.outReg : -1;
         sp.valid = true;
         std::map<int, int> cellLocal;
         auto local = [&](int cell) {
            auto it = cellLocal.find(cell);
            if (it != cellLocal.end())
               return it->second;
            const int id = (int)seg.stateMap.size();
            cellLocal[cell] = id;
            seg.stateMap.push_back(cell);
            sp.state.push_back(p.state[(size_t)cell]);
            return id;
         };
         for (int w : remat[(size_t)s])
            sp.code.push_back(p.code[(size_t)w]);
         for (int i = 0; i < f.n; i++)
         {
            if (segOf[(size_t)i] != s)
               continue;
            SampleInstr ins = p.code[(size_t)i];
            if (ins.op == SampleOp::LoadState || ins.op == SampleOp::StoreState)
               ins.a = (uint8_t)local(ins.a);
            if (ins.op == SampleOp::LoadIn)
               seg.readsIn = true;
            sp.code.push_back(ins);
         }
      }
      return true;
   }

   double PlanCost(const std::vector<RateSegment>& segs, const RateCostModel& m)
   {
      double c = 0.0;
      for (size_t s = 0; s < segs.size(); s++)
      {
         const int k = segs[s].factor;
         const int fi = std::max(0, FactorIndex(k));
         c += (double)k * (m.run + m.instr * (double)segs[s].prog.code.size());
         if (segs[s].readsIn)
            c += m.up[fi];
         if (s + 1 < segs.size())
            c += (double)segs[s].outRegs.size() * (m.down[fi] + m.up[std::max(0, FactorIndex(segs[s + 1].factor))]);
         else
            c += m.down[fi];
      }
      return c;
   }

   double WholeCost(const SampleProgram& prog, int factor, const RateCostModel& m)
   {
      const int fi = std::max(0, FactorIndex(factor));
      bool readsIn = false;
      for (const SampleInstr& i : prog.code)
         readsIn |= i.op == SampleOp::LoadIn;
      return (double)factor * (m.run + m.instr * (double)prog.code.size()) + (readsIn ? m.up[fi] : 0.0) + m.down[fi];
   }

   bool ChooseRatePlan(const SampleProgram& prog, const RatePlanOptions& opt, RatePlan& best, RatePlanReport& rep)
   {
      rep = RatePlanReport();
      best = RatePlan();
      SampleProgram prepared;
      int inReg = -1;
      if (!PrepareForSplit(prog, prepared, inReg, rep.status))
         return false;
      const std::vector<int> cands = CandidateCuts(prepared, opt.maxCrossing, opt.maxCandidates);
      rep.candidates = (int)cands.size();
      const double maxHarm = 0.5 * opt.baseRate / opt.f0Hz;

      // Alias is scored where it is heard: a value crossing a cut carries
      // its alias (absolute power) into the rest of the program, scaled by
      // the output's mean-square sensitivity to that value at the cut, and
      // compared with the output's fundamental. Sensitivity is linearised
      // on the unsplit program at the base rate (central difference on the
      // periodic steady state; static gain, so filters after a cut are
      // taken at their DC gain).
      AliasContext baseCtx;
      baseCtx.f0Hz = opt.f0Hz;
      baseCtx.inAmplitude = opt.inAmplitude;
      baseCtx.usesInput = opt.usesInput;
      baseCtx.gridLog2 = opt.gridLog2;
      double outFund = 0.0;
      {
         AliasSpectrum sp;
         rep.analyses++;
         if (!AnalyzePeriodicSpectrum(prepared, baseCtx, opt.baseRate, sp))
            return rep.status = sp.status, false;
         rep.evaluations += sp.evaluations;
         outFund = sp.power.size() > 1 ? sp.power[1] : 0.0;
         if (!(outFund > 1e-24))
            return rep.status = "output has no fundamental", false;
      }
      std::map<std::pair<int, int>, double> gainMemo;
      auto sensitivity = [&](int cut, int reg, const std::vector<double>& wave) -> double {
         const auto key = std::make_pair(cut, reg);
         auto it = gainMemo.find(key);
         if (it != gainMemo.end())
            return it->second;
         double ms = 1e30; // unknown: no plan through this cut meets a target
         double rms = 0.0;
         for (double v : wave)
            rms += v * v / (double)wave.size();
         rms = std::sqrt(rms);
         const double eps = std::max(1e-6, 1e-3 * rms);
         if (prepared.numRegs < kSampleMaxRegs && (int)prepared.code.size() + 2 <= kSampleMaxInstr &&
             prepared.outReg >= 0)
         {
            std::vector<double> y[2];
            bool ok = true;
            for (int side = 0; side < 2 && ok; side++)
            {
               SampleProgram q = prepared;
               const int rE = q.numRegs++;
               SampleInstr li, add;
               li.op = SampleOp::LoadImm;
               li.dst = (uint8_t)rE;
               li.imm = (float)(side == 0 ? eps : -eps);
               add.op = SampleOp::Add;
               add.dst = (uint8_t)reg;
               add.a = (uint8_t)reg;
               add.b = (uint8_t)rE;
               q.code.insert(q.code.begin() + cut, { li, add });
               AliasContext ctx = baseCtx;
               ctx.captureRegs = { q.outReg };
               AliasSpectrum sp;
               rep.analyses++;
               ok = AnalyzePeriodicSpectrum(q, ctx, opt.baseRate, sp);
               rep.evaluations += sp.evaluations;
               if (ok)
                  y[side] = sp.captured[0];
            }
            if (ok)
            {
               ms = 0.0;
               for (size_t j = 0; j < y[0].size(); j++)
               {
                  const double g = (y[0][j] - y[1][j]) / (2.0 * eps);
                  ms += g * g / (double)y[0].size();
               }
            }
         }
         gainMemo[key] = ms;
         return ms;
      };
      // Alias power that folds in band at a factor, re 1 (absolute).
      auto absAlias = [&](AliasSpectrum a, int factor) {
         if (a.power.size() < 2)
            return 0.0;
         a.power[1] = 1.0;
         return std::pow(10.0, PredictAliasDb(a, opt.f0Hz, opt.baseRate, factor, opt.audibleHz, opt.harmonicTolHz) / 10.0);
      };

      // Cut sets: every subset of the candidates with up to maxCuts cuts.
      std::vector<std::vector<int>> sets;
      std::vector<int> cur;
      std::function<void(size_t)> gen = [&](size_t from) {
         sets.push_back(cur);
         if ((int)cur.size() >= opt.maxCuts)
            return;
         for (size_t i = from; i < cands.size(); i++)
         {
            cur.push_back(cands[i]);
            gen(i + 1);
            cur.pop_back();
         }
      };
      gen(0);

      struct SegResult
      {
         bool ok = false;
         std::string status;
         double db[kNumPlanFactors] = {};
         std::vector<std::vector<double>> waves; // outRegs, at the lowest factor
      };
      std::map<std::string, SegResult> memo;

      bool anyMeets = false, anyPlan = false;
      double bestCost = 1e300, bestDb = 1e300;
      for (const std::vector<int>& cuts : sets)
      {
         std::vector<RateSegment> segs;
         std::string why;
         if (!SplitProgram(prepared, cuts, segs, why))
            continue;
         rep.cutSets++;
         const int m = (int)segs.size();
         std::vector<const SegResult*> res((size_t)m, nullptr);
         bool ok = true;
         std::string key;
         for (int s = 0; s < m && ok; s++)
         {
            key += std::to_string(segs[(size_t)s].first) + "-" + std::to_string(segs[(size_t)s].end) + ";";
            auto it = memo.find(key);
            if (it == memo.end())
            {
               SegResult r;
               r.ok = true;
               const RateSegment& seg = segs[(size_t)s];
               AliasContext ctx;
               ctx.f0Hz = opt.f0Hz;
               ctx.inAmplitude = opt.inAmplitude;
               ctx.usesInput = opt.usesInput;
               ctx.gridLog2 = opt.gridLog2;
               ctx.inputRegs = seg.inRegs;
               ctx.captureRegs = seg.outRegs;
               if (s > 0)
               {
                  const RateSegment& prev = segs[(size_t)s - 1];
                  const SegResult& pr = *res[(size_t)s - 1];
                  for (int reg : seg.inRegs)
                  {
                     const auto at = std::find(prev.outRegs.begin(), prev.outRegs.end(), reg);
                     std::vector<double> w = pr.waves[(size_t)(at - prev.outRegs.begin())];
                     const auto [lo, hi] = std::minmax_element(w.begin(), w.end());
                     const double wLo = *lo, wHi = *hi;
                     BandLimitWave(w, maxHarm);
                     // A one-signed value (exp, abs, a square) that rings
                     // across zero once band-limited can land on a pole or a
                     // branch cut downstream ((e-1)/(e+1) at e = -1, log,
                     // sqrt) between grid points: no prediction holds there.
                     const auto [blo, bhi] = std::minmax_element(w.begin(), w.end());
                     if ((wLo >= 0.0 && *blo < 0.0) || (wHi <= 0.0 && *bhi > 0.0))
                     {
                        r.ok = false;
                        r.status = "band-limiting a crossing value changes its sign";
                     }
                     ctx.inputWaves.push_back(std::move(w));
                  }
               }
               const bool dep = FactorDependent(seg.prog);
               for (int fi = 0; fi < kNumPlanFactors && r.ok; fi++)
               {
                  if (!dep && fi > 0)
                     break;
                  AliasSpectrum sp;
                  rep.analyses++;
                  if (!AnalyzePeriodicSpectrum(seg.prog, ctx, opt.baseRate * kPlanFactors[fi], sp))
                  {
                     r.ok = false;
                     r.status = sp.status;
                  }
                  rep.evaluations += sp.evaluations;
                  if (!r.ok)
                     break;
                  if (fi == 0)
                     r.waves = sp.captured;
                  // This segment's alias at the output, at each factor it may run at.
                  std::vector<AliasSpectrum> specs;
                  std::vector<double> gains;
                  if (s == m - 1)
                  {
                     specs.push_back(sp);
                     gains.push_back(1.0);
                  }
                  for (size_t w = 0; s < m - 1 && w < sp.captured.size(); w++)
                  {
                     AliasSpectrum ws;
                     SpectrumFromWave(sp.captured[w], ws);
                     specs.push_back(ws);
                     gains.push_back(sensitivity(seg.end, seg.outRegs[w], r.waves.empty() ? sp.captured[w] : r.waves[w]));
                  }
                  for (int fj = (dep ? fi : 0); fj < (dep ? fi + 1 : kNumPlanFactors); fj++)
                  {
                     double lin = 0.0;
                     for (size_t a = 0; a < specs.size(); a++)
                        lin += absAlias(specs[a], kPlanFactors[fj]) * gains[a];
                     r.db[fj] = 10.0 * std::log10(std::max(lin / outFund, 1e-30));
                  }
               }
               it = memo.emplace(key, std::move(r)).first;
            }
            if (!it->second.ok)
            {
               ok = false;
               if (cuts.empty())
                  rep.status = it->second.status;
            }
            res[(size_t)s] = &it->second;
         }
         if (!ok)
            continue;

         // Every factor assignment for this cut set.
         int combos = 1;
         for (int s = 0; s < m; s++)
            combos *= kNumPlanFactors;
         for (int c = 0; c < combos; c++)
         {
            int code = c;
            double lin = 0.0;
            std::vector<double> segDb((size_t)m);
            for (int s = 0; s < m; s++)
            {
               const int fi = code % kNumPlanFactors;
               code /= kNumPlanFactors;
               segs[(size_t)s].factor = kPlanFactors[fi];
               segDb[(size_t)s] = res[(size_t)s]->db[fi];
               lin += std::pow(10.0, segDb[(size_t)s] / 10.0);
            }
            const double db = 10.0 * std::log10(std::max(lin, 1e-30));
            const double cost = PlanCost(segs, opt.cost);
            const bool meets = db <= opt.targetDb;
            bool take = false;
            if (meets)
               take = !anyMeets || cost < bestCost - 1e-9 ||
                      (std::fabs(cost - bestCost) <= 1e-9 && m < (int)best.segs.size());
            else if (!anyMeets)
               take = !anyPlan || db < bestDb;
            if (take)
            {
               anyMeets |= meets;
               anyPlan = true;
               bestCost = cost;
               bestDb = db;
               best.cuts = cuts;
               best.segs = segs;
               best.segDb = segDb;
               best.predictedDb = db;
               best.cost = cost;
               best.meetsTarget = meets;
            }
         }
      }
      if (!anyPlan)
      {
         if (rep.status.empty())
            rep.status = "no plan";
         return false;
      }
      rep.status = anyMeets ? "ok" : "no plan within 8x meets the target";
      return true;
   }
}

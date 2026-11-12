// Headless checks for Field Patcher Block A (internal/plans/field-patcher/README.md §10, §12).
//
//   field-patcher-check            run every case and every shipped preset
//   field-patcher-check <substr>   also print the round-tripped code of presets whose name matches
//
// Every preset is taken from the node sources by tools/field_patcher_presets.py at build time,
// parsed into boxes, printed back, and compiled both ways. The compiled programs must match
// (M4), printing must be idempotent, and Notes / Primitive / Element presets should open with
// no code box (M3). Exit is 0 only when every case passes, M4 is 100% and M3 is >= 70%.
#include "field/FieldBoxGraph.h"
#include "field/FieldBoxRegistry.h"
#include "field/FieldIR.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "field/GlslBackend.h"
#include "field/BackendRegister.h"
#include "field/ElementBackend.h"

#include "field_patcher_presets.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ExprGlobals
{
   struct Global { std::string name; std::string expr; float value = 0; std::string error; };
   std::vector<Global>& All() { static std::vector<Global> g; return g; }
}

namespace
{
   using namespace Field;

   // ---- compiled-program comparison -----------------------------------------------------

   bool NodeEq(const IRNodePtr& a, const IRNodePtr& b)
   {
      if (!a && !b) return true;
      if (!a || !b) return false;
      if (a->kind != b->kind || a->type != b->type || a->domain != b->domain ||
          a->transferKind != b->transferKind || a->divisor != b->divisor ||
          a->isOffsetRead != b->isOffsetRead || a->isNeighbourRead != b->isNeighbourRead ||
          a->isHandle != b->isHandle || a->reduceNeedsPostLoop != b->reduceNeedsPostLoop ||
          a->numberValue != b->numberValue || a->varName != b->varName ||
          a->op != b->op || a->callee != b->callee || a->field != b->field ||
          a->children.size() != b->children.size())
         return false;
      for (int i = 0; i < 4; ++i)
         if (a->vecValues[i] != b->vecValues[i] || a->swizzleIndices[i] != b->swizzleIndices[i])
            return false;
      for (size_t i = 0; i < a->children.size(); ++i)
         if (!NodeEq(a->children[i], b->children[i]))
            return false;
      return true;
   }

   bool StmtListEq(const std::vector<IRStmtPtr>& a, const std::vector<IRStmtPtr>& b);

   bool StmtEq(const IRStmtPtr& a, const IRStmtPtr& b)
   {
      if (!a && !b) return true;
      if (!a || !b) return false;
      if (a->kind != b->kind || a->domain != b->domain ||
          a->assignTarget != b->assignTarget || a->assignField != b->assignField ||
          a->assignOp != b->assignOp || a->swizzleCount != b->swizzleCount ||
          a->attribName != b->attribName || a->attribType != b->attribType ||
          a->stateName != b->stateName || a->stateTypeName != b->stateTypeName ||
          a->stateType != b->stateType || a->stateLanes != b->stateLanes ||
          a->pinName != b->pinName || a->pinTypeName != b->pinTypeName ||
          a->pinIsStructural != b->pinIsStructural || a->pinType != b->pinType ||
          a->pinLanes != b->pinLanes || a->pinDomain != b->pinDomain ||
          a->constantTripCount != b->constantTripCount ||
          a->emitTargetName != b->emitTargetName || a->emitTypeName != b->emitTypeName ||
          a->setParamName != b->setParamName)
         return false;
      if (!NodeEq(a->lvalueExpr, b->lvalueExpr) || !NodeEq(a->rvalueExpr, b->rvalueExpr) ||
          !NodeEq(a->attribInitExpr, b->attribInitExpr) || !NodeEq(a->pinInitExpr, b->pinInitExpr) ||
          !NodeEq(a->ifCond, b->ifCond) || !NodeEq(a->forCond, b->forCond) ||
          !NodeEq(a->expr, b->expr) || !NodeEq(a->connectSrc, b->connectSrc) ||
          !NodeEq(a->connectSrcSlot, b->connectSrcSlot) || !NodeEq(a->connectDst, b->connectDst) ||
          !NodeEq(a->connectDstSlot, b->connectDstSlot) || !NodeEq(a->setTarget, b->setTarget) ||
          !NodeEq(a->setValue, b->setValue) || !NodeEq(a->placeTarget, b->placeTarget) ||
          !NodeEq(a->placeX, b->placeX) || !NodeEq(a->placeY, b->placeY))
         return false;
      if (a->emitKeyArgs.size() != b->emitKeyArgs.size())
         return false;
      for (size_t i = 0; i < a->emitKeyArgs.size(); ++i)
         if (!NodeEq(a->emitKeyArgs[i], b->emitKeyArgs[i])) return false;
      return StmtListEq(a->thenStmts, b->thenStmts) && StmtListEq(a->elseStmts, b->elseStmts) &&
             StmtListEq(a->forBody, b->forBody);
   }

   bool StmtListEq(const std::vector<IRStmtPtr>& a, const std::vector<IRStmtPtr>& b)
   {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i)
         if (!StmtEq(a[i], b[i])) return false;
      return true;
   }

   bool ParamsEq(const std::vector<DeclaredParam>& a, const std::vector<DeclaredParam>& b, std::string& why)
   {
      if (a.size() != b.size()) { why = "param count"; return false; }
      for (size_t i = 0; i < a.size(); ++i)
         if (a[i].name != b[i].name || a[i].typeName != b[i].typeName || a[i].defaultValue != b[i].defaultValue ||
             a[i].minValue != b[i].minValue || a[i].maxValue != b[i].maxValue)
         { why = "param '" + a[i].name + "' default/min/max"; return false; }
      return true;
   }

   bool StatesEq(const std::vector<DeclaredState>& a, const std::vector<DeclaredState>& b, std::string& why)
   {
      if (a.size() != b.size()) { why = "state count"; return false; }
      for (size_t i = 0; i < a.size(); ++i)
         if (a[i].name != b[i].name || a[i].typeName != b[i].typeName || a[i].lanes != b[i].lanes ||
             a[i].initialValues != b[i].initialValues || a[i].domain != b[i].domain || a[i].boundary != b[i].boundary)
         { why = "state '" + a[i].name + "' type/initial value"; return false; }
      return true;
   }

   template <class Pin>
   bool PinsEq(const std::vector<Pin>& a, const std::vector<Pin>& b)
   {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i)
         if (a[i].name != b[i].name || a[i].typeName != b[i].typeName || a[i].isStructural != b[i].isStructural ||
             a[i].lanes != b[i].lanes || a[i].domain != b[i].domain)
            return false;
      return true;
   }

   bool SampleEq(const SampleProgram& a, const SampleProgram& b, std::string& why)
   {
      if (a.code.size() != b.code.size() || a.numRegs != b.numRegs || a.outReg != b.outReg)
      { why = "bytecode size / registers"; return false; }
      for (size_t i = 0; i < a.code.size(); ++i)
      {
         const auto& x = a.code[i];
         const auto& y = b.code[i];
         if (x.op != y.op || x.dst != y.dst || x.a != y.a || x.b != y.b || x.c != y.c || x.imm != y.imm)
         { why = "bytecode at " + std::to_string(i); return false; }
      }
      if (a.params.size() != b.params.size()) { why = "param count"; return false; }
      for (size_t i = 0; i < a.params.size(); ++i)
         if (a.params[i].name != b.params[i].name || a.params[i].defaultValue != b.params[i].defaultValue ||
             a.params[i].minValue != b.params[i].minValue || a.params[i].maxValue != b.params[i].maxValue)
         { why = "param '" + a.params[i].name + "' default/min/max"; return false; }
      if (a.state.size() != b.state.size()) { why = "state count"; return false; }
      for (size_t i = 0; i < a.state.size(); ++i)
         if (a.state[i].name != b.state[i].name || a.state[i].typeName != b.state[i].typeName ||
             a.state[i].initialValue != b.state[i].initialValue)
         { why = "state '" + a.state[i].name + "' type/initial value"; return false; }
      if (a.delays.size() != b.delays.size()) { why = "delay count"; return false; }
      for (size_t i = 0; i < a.delays.size(); ++i)
         if (a.delays[i].bufferOffset != b.delays[i].bufferOffset || a.delays[i].length != b.delays[i].length ||
             a.delays[i].cursorIndex != b.delays[i].cursorIndex)
         { why = "delay line " + std::to_string(i); return false; }
      if (a.tables.size() != b.tables.size()) { why = "table count"; return false; }
      for (size_t i = 0; i < a.tables.size(); ++i)
         if (a.tables[i].name != b.tables[i].name || a.tables[i].length != b.tables[i].length ||
             a.tables[i].bufferOffset != b.tables[i].bufferOffset || a.tables[i].initialValue != b.tables[i].initialValue)
         { why = "table '" + a.tables[i].name + "'"; return false; }
      if (a.hasNoteEmit != b.hasNoteEmit || a.hasReduceRms != b.hasReduceRms ||
          a.reduceLoHz != b.reduceLoHz || a.reduceHiHz != b.reduceHiHz)
      { why = "note emit / reduce.rms band"; return false; }
      auto pinsEq = [](const std::vector<SampleDeclaredPin>& x, const std::vector<SampleDeclaredPin>& y) {
         if (x.size() != y.size()) return false;
         for (size_t i = 0; i < x.size(); ++i)
            if (x[i].name != y[i].name || x[i].typeName != y[i].typeName || x[i].domainName != y[i].domainName)
               return false;
         return true;
      };
      if (!pinsEq(a.declaredOutputs, b.declaredOutputs) || !pinsEq(a.declaredInputs, b.declaredInputs))
      { why = "declared pins"; return false; }
      return true;
   }

   bool ParseAst(const std::string& src, AstNodePtr& out, FieldError& err)
   {
      std::vector<Token> toks;
      return Lex(src, toks, err) && ParseProgram(toks, out, err);
   }

   // Compiles `a` and `b` for `domain` and compares. `why` gets the reason on failure.
   bool SameProgram(const std::string& domain, const std::string& a, const std::string& b, std::string& why)
   {
      FieldError err;
      if (domain == "notes" || domain == "sample")
      {
         const bool notesHost = domain == "notes";
         SampleProgram p1, p2;
         if (!CompileSampleProgram(a, nullptr, p1, err, notesHost)) { why = "original does not compile: " + err.message; return false; }
         if (!CompileSampleProgram(b, nullptr, p2, err, notesHost)) { why = "round trip does not compile: " + err.message; return false; }
         return SampleEq(p1, p2, why);
      }
      AstNodePtr a1, a2;
      if (!ParseAst(a, a1, err)) { why = "original does not parse: " + err.message; return false; }
      if (!ParseAst(b, a2, err)) { why = "round trip does not parse: " + err.message; return false; }
      if (domain == "element")
      {
         ElementIRProgram i1, i2;
         if (!LowerElementProgramToIR(a1, i1, err)) { why = "original does not lower: " + err.message; return false; }
         if (!LowerElementProgramToIR(a2, i2, err)) { why = "round trip does not lower: " + err.message; return false; }
         if (!StmtListEq(i1.prologue, i2.prologue)) { why = "prologue"; return false; }
         if (!StmtListEq(i1.elementLoop, i2.elementLoop)) { why = "element loop"; return false; }
         if (!StmtListEq(i1.postLoop, i2.postLoop)) { why = "post loop"; return false; }
         if (!ParamsEq(i1.declaredParams, i2.declaredParams, why)) return false;
         if (!StatesEq(i1.declaredStates, i2.declaredStates, why)) return false;
         if (!PinsEq(i1.declaredOutputs, i2.declaredOutputs) || !PinsEq(i1.declaredInputs, i2.declaredInputs))
         { why = "declared pins"; return false; }
         return true;
      }
      if (domain == "pixel")
      {
         PixelIRProgram i1, i2;
         if (!LowerPixelProgramToIR(a1, i1, err)) { why = "original does not lower: " + err.message; return false; }
         if (!LowerPixelProgramToIR(a2, i2, err)) { why = "round trip does not lower: " + err.message; return false; }
         if (!ParamsEq(i1.declaredParams, i2.declaredParams, why)) return false;
         if (!StatesEq(i1.declaredStates, i2.declaredStates, why)) return false;
         if (EmitGlsl(i1).source != EmitGlsl(i2).source) { why = "GLSL differs"; return false; }
         return true;
      }
      if (domain == "graph")
      {
         GraphIRProgram i1, i2;
         if (!LowerGraphProgramToIR(a1, i1, err)) { why = "original does not lower: " + err.message; return false; }
         if (!LowerGraphProgramToIR(a2, i2, err)) { why = "round trip does not lower: " + err.message; return false; }
         if (!StmtListEq(i1.statements, i2.statements)) { why = "graph statements"; return false; }
         return ParamsEq(i1.declaredParams, i2.declaredParams, why);
      }
      why = "unknown domain " + domain;
      return false;
   }

   // ---- round trip ------------------------------------------------------------------------

   struct RoundTrip
   {
      bool ok = false;
      int codeBoxes = 0;
      std::string printed;
      std::string why;
   };

   RoundTrip Run(const std::string& domain, const std::string& code)
   {
      RoundTrip r;
      FieldError err;
      BoxGraph g;
      if (!ParseCodeToBoxGraph(code, HostAll, g, err)) { r.why = "ParseCodeToBoxGraph: " + err.message; return r; }
      r.codeBoxes = g.CountCodeBoxes();
      r.printed = PrintBoxGraphToCode(g, true);
      if (!SameProgram(domain, code, r.printed, r.why)) return r;

      BoxGraph g2;
      if (!ParseCodeToBoxGraph(r.printed, HostAll, g2, err)) { r.why = "re-parse: " + err.message; return r; }
      const std::string again = PrintBoxGraphToCode(g2, true);
      if (again != r.printed) { r.why = "not idempotent; second print:\n" + again; return r; }
      r.ok = true;
      return r;
   }

   int g_failures = 0;

   void Fail(const std::string& name, const std::string& why, const std::string& printed = "")
   {
      ++g_failures;
      std::cerr << "[FAIL] " << name << ": " << why << "\n";
      if (!printed.empty())
         std::cerr << "---- round trip ----\n" << printed << "\n--------------------\n";
   }

   // A hand-written case: must round-trip; `expectCodeBoxes` < 0 means don't care.
   void Case(const std::string& name, const std::string& domain, const std::string& code, int expectCodeBoxes = 0)
   {
      RoundTrip r = Run(domain, code);
      if (!r.ok) { Fail(name, r.why, r.printed); return; }
      if (expectCodeBoxes >= 0 && r.codeBoxes != expectCodeBoxes)
         Fail(name, "expected " + std::to_string(expectCodeBoxes) + " code boxes, got " + std::to_string(r.codeBoxes), r.printed);
   }

   void Presets(const char* filter, int& m4Pass, int& m4Total, int& m3Pass, int& m3Total)
   {
      for (const auto& p : FieldPatcherPresets::kAll)
      {
         const std::string name = std::string(p.host) + " / " + p.name;
         RoundTrip r = Run(p.domain, p.code);
         ++m4Total;
         if (p.countsForM3) ++m3Total;
         if (r.ok)
         {
            ++m4Pass;
            if (p.countsForM3 && r.codeBoxes == 0) ++m3Pass;
         }
         else
            Fail(name, r.why, r.printed);
         if (filter && std::strstr(p.name, filter))
            std::cout << "==== " << name << " (" << r.codeBoxes << " code boxes)\n" << r.printed << "\n";
      }
   }
}

int main(int argc, char** argv)
{
   const char* filter = argc > 1 ? argv[1] : nullptr;

   // ---- registry ----
   if (AllBoxSpecs().size() < 75)
      Fail("registry", "too few boxes: " + std::to_string(AllBoxSpecs().size()));
   if (FindBoxSpecsByQuery("either").empty() || FindBoxSpecsByQuery("xor").empty() || FindBoxSpecsByQuery("!=").empty())
      Fail("registry", "alias search for either / xor / != found nothing");

   // ---- header ----
   {
      BoxHeaderData h;
      ParseBoxesHeader("# @boxes v1 view 12,34,1.5 | k_rate 120,40 | group \"rhythm\" k_rate b1 [folded]\nparam float rate = 0.25 [0.0625, 1]\n", h);
      if (!h.view.valid || h.view.panX != 12.0f || h.view.panY != 34.0f || h.positions.size() != 1 ||
          h.groups.size() != 1 || !h.groups[0].folded)
         Fail("header", "'# @boxes v1' header did not parse");
      BoxHeaderData h2;
      ParseBoxesHeader("// @boxes v1 view 1,2,1 | k_rate 1,2\n", h2);
      if (!h2.view.valid || h2.positions.size() != 1)
         Fail("header", "'// @boxes v1' header no longer accepted");
      BoxHeaderData h3;
      ParseBoxesHeader("# @boxes v1 broken_junk |||\nparam float x = 1 [0, 2]\n", h3);
      if (h3.view.valid)
         Fail("header", "broken header should leave the view invalid");
   }

   // ---- sugar ----
   {
      BoxGraph g;
      FieldError err;
      if (!ParseCodeToBoxGraph("a = t\nb = sin(t)\nc = (a > 0.5) != (b > 0.5)\npublish = c\n", HostElement, g, err))
         Fail("sugar xor", "parse failed: " + err.message);
      else
      {
         bool found = false;
         for (const auto& box : g.boxes)
            if (box.specId == "xor") found = true;
         if (!found || g.CountCodeBoxes() != 0)
            Fail("sugar xor", "(a > 0.5) != (b > 0.5) did not open as one either box");
      }
   }

   // ---- hand-written round trips ----
   Case("Notes Harmoniser", "notes",
        "if (noteOn > 0.5) {\n   note(noteNum, noteVel, 0)\n   note(noteNum + 7, noteVel * 0.7, 0)\n}\n");
   Case("Notes Arp Up", "notes",
        "param float steps = 4 [2, 8]\nstate float step = 0\nif (tick(1/8) > 0.5) {\n   cnt = floor(steps + 0.5)\n"
        "   note(deg(step * 2), 0.8, 1/8)\n   step = (step + 1) % cnt\n}\n", -1);
   Case("Element Wave Displace", "element",
        "param float amp = 0.3 [0.0, 2.0]\nparam float freq = 3.5 [0.5, 10.0]\nparam float speed = 2.0 [0.0, 8.0]\n"
        "P.y += sin(P.x * freq + t * speed) * amp\npublish = sin(t * speed)\n");
   Case("Pixel Levels", "pixel",
        "input pixel image img;\nparam float inBlack = 0.0 [0.0, 1.0];\nparam float inWhite = 1.0 [0.0, 1.0];\n"
        "c = clamp((img.rgb - inBlack) / max(inWhite - inBlack, 0.0001), 0.0, 1.0);\ncol = c;\nalpha = img.a;\n", -1);
   Case("Sample Tube Overdrive", "sample",
        "param float drive = 3.0 [1.0, 10.0]\nparam float tone = 4000.0 [400.0, 12000.0]\nstate float lp = 0\n"
        "x = in * drive\nsat = x / (1.0 + abs(x))\nf = clamp((tone / sr) * 3.14159, 0.001, 0.95)\n"
        "lp = lp + f * (sat - lp)\nout = lp\n", -1);
   Case("Graph Pipeline", "graph",
        "lfo = emit(\"LFO\", 0)\nosc = emit(\"Wavetable\", 1)\nconnect(lfo, 0, osc, 0)\nplace(lfo, -260, 0)\nplace(osc, 80, 0)\n", -1);

   // ---- every shipped preset ----
   int m4Pass = 0, m4Total = 0, m3Pass = 0, m3Total = 0;
   Presets(filter, m4Pass, m4Total, m3Pass, m3Total);

   std::cout << "M4 " << m4Pass << "/" << m4Total << " (compiled program unchanged, idempotent)\n";
   std::cout << "M3 " << m3Pass << "/" << m3Total << " (Notes + Primitive + Element, no code box, also pass M4)\n";

   const bool m4ok = m4Pass == m4Total;
   const bool m3ok = m3Pass * 10 >= m3Total * 7;
   if (g_failures == 0 && m4ok && m3ok)
   {
      std::cout << "field-patcher-check: ALL PASSED\n";
      return 0;
   }
   std::cerr << "field-patcher-check: " << g_failures << " failure(s)" << (m3ok ? "" : ", M3 below 70%") << "\n";
   return 1;
}

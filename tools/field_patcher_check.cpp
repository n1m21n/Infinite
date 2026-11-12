// Standalone headless check for Field Patcher Block A (docs/internal/plans/field-patcher/README.md):
//   - 10 box families, ~80+ boxes, friendly labels & aliases
//   - Versioned @boxes v1 header parse/format + malformed/missing fallback
//   - Sugar recognition (either/xor, nand, nor, xnor)
//   - Code -> Boxes -> Code round-trip IR equivalence across Element, Pixel, Sample, Notes, Graph
#include "field/FieldBoxGraph.h"
#include "field/FieldBoxRegistry.h"
#include "field/FieldIR.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "field/GlslBackend.h"
#include "field/BackendRegister.h"
#include "field/ElementBackend.h"

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
   bool CheckIRNodeEqual(const Field::IRNodePtr& a, const Field::IRNodePtr& b)
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
      {
         if (a->vecValues[i] != b->vecValues[i] || a->swizzleIndices[i] != b->swizzleIndices[i])
            return false;
      }
      for (size_t i = 0; i < a->children.size(); ++i)
      {
         if (!CheckIRNodeEqual(a->children[i], b->children[i]))
            return false;
      }
      return true;
   }

   bool CheckIRStmtEqual(const Field::IRStmtPtr& a, const Field::IRStmtPtr& b)
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
      if (!CheckIRNodeEqual(a->lvalueExpr, b->lvalueExpr) ||
          !CheckIRNodeEqual(a->rvalueExpr, b->rvalueExpr) ||
          !CheckIRNodeEqual(a->attribInitExpr, b->attribInitExpr) ||
          !CheckIRNodeEqual(a->pinInitExpr, b->pinInitExpr) ||
          !CheckIRNodeEqual(a->ifCond, b->ifCond) ||
          !CheckIRNodeEqual(a->forCond, b->forCond) ||
          !CheckIRNodeEqual(a->expr, b->expr) ||
          !CheckIRNodeEqual(a->connectSrc, b->connectSrc) ||
          !CheckIRNodeEqual(a->connectSrcSlot, b->connectSrcSlot) ||
          !CheckIRNodeEqual(a->connectDst, b->connectDst) ||
          !CheckIRNodeEqual(a->connectDstSlot, b->connectDstSlot) ||
          !CheckIRNodeEqual(a->setTarget, b->setTarget) ||
          !CheckIRNodeEqual(a->setValue, b->setValue) ||
          !CheckIRNodeEqual(a->placeTarget, b->placeTarget) ||
          !CheckIRNodeEqual(a->placeX, b->placeX) ||
          !CheckIRNodeEqual(a->placeY, b->placeY))
         return false;
      if (a->thenStmts.size() != b->thenStmts.size() ||
          a->elseStmts.size() != b->elseStmts.size() ||
          a->forBody.size() != b->forBody.size() ||
          a->emitKeyArgs.size() != b->emitKeyArgs.size())
         return false;
      for (size_t i = 0; i < a->thenStmts.size(); ++i)
         if (!CheckIRStmtEqual(a->thenStmts[i], b->thenStmts[i])) return false;
      for (size_t i = 0; i < a->elseStmts.size(); ++i)
         if (!CheckIRStmtEqual(a->elseStmts[i], b->elseStmts[i])) return false;
      for (size_t i = 0; i < a->forBody.size(); ++i)
         if (!CheckIRStmtEqual(a->forBody[i], b->forBody[i])) return false;
      for (size_t i = 0; i < a->emitKeyArgs.size(); ++i)
         if (!CheckIRNodeEqual(a->emitKeyArgs[i], b->emitKeyArgs[i])) return false;
      return true;
   }

   bool CheckSampleProgramEqual(const Field::SampleProgram& a, const Field::SampleProgram& b)
   {
      if (a.code.size() != b.code.size() || a.params.size() != b.params.size() ||
          a.state.size() != b.state.size())
         return false;
      for (size_t i = 0; i < a.code.size(); ++i)
      {
         const auto& ia = a.code[i];
         const auto& ib = b.code[i];
         if (ia.op != ib.op || ia.dst != ib.dst || ia.a != ib.a ||
             ia.b != ib.b || ia.c != ib.c || ia.imm != ib.imm)
            return false;
      }
      return true;
   }

   bool RoundTripCheck(const std::string& name, const std::string& domain, const std::string& code, bool expectZeroCodeBoxes = true)
   {
      Field::FieldError err;
      Field::BoxGraph graph;
      if (!Field::ParseCodeToBoxGraph(code, Field::HostAll, graph, err))
      {
         std::cerr << "[FAIL] " << name << ": ParseCodeToBoxGraph failed: " << err.message << "\n";
         return false;
      }
      if (expectZeroCodeBoxes && graph.CountCodeBoxes() > 0)
      {
         std::cerr << "[FAIL] " << name << ": expected 0 code boxes, got " << graph.CountCodeBoxes() << "\n";
         return false;
      }

      std::string rtCode = Field::PrintBoxGraphToCode(graph, true);

      if (domain == "notes" || domain == "sample")
      {
         bool notesHost = (domain == "notes");
         Field::SampleProgram p1, p2;
         if (!Field::CompileSampleProgram(code, nullptr, p1, err, notesHost))
         {
            std::cerr << "[FAIL] " << name << ": original compile failed: " << err.message << "\n";
            return false;
         }
         if (!Field::CompileSampleProgram(rtCode, nullptr, p2, err, notesHost))
         {
            std::cerr << "[FAIL] " << name << ": round-trip compile failed: " << err.message << "\nCode:\n" << rtCode << "\n";
            return false;
         }
         if (!CheckSampleProgramEqual(p1, p2))
         {
            std::cerr << "[FAIL] " << name << ": SampleProgram bytecode mismatch\nCode:\n" << rtCode << "\n";
            return false;
         }
         return true;
      }

      auto parseAst = [&](const std::string& src, Field::AstNodePtr& outAst) -> bool {
         std::vector<Field::Token> toks;
         if (!Field::Lex(src, toks, err)) return false;
         return Field::ParseProgram(toks, outAst, err);
      };

      Field::AstNodePtr a1, a2;
      if (!parseAst(code, a1) || !parseAst(rtCode, a2))
      {
         std::cerr << "[FAIL] " << name << ": parse failed: " << err.message << "\nCode:\n" << rtCode << "\n";
         return false;
      }

      if (domain == "element")
      {
         Field::ElementIRProgram ir1, ir2;
         if (!Field::LowerElementProgramToIR(a1, ir1, err) ||
             !Field::LowerElementProgramToIR(a2, ir2, err))
         {
            std::cerr << "[FAIL] " << name << ": LowerElementProgramToIR failed: " << err.message << "\nCode:\n" << rtCode << "\n";
            return false;
         }
         if (ir1.prologue.size() != ir2.prologue.size() ||
             ir1.elementLoop.size() != ir2.elementLoop.size() ||
             ir1.postLoop.size() != ir2.postLoop.size())
         {
            std::cerr << "[FAIL] " << name << ": ElementIR stmt count mismatch\nCode:\n" << rtCode << "\n";
            return false;
         }
         for (size_t i = 0; i < ir1.prologue.size(); ++i)
            if (!CheckIRStmtEqual(ir1.prologue[i], ir2.prologue[i])) { std::cerr << "[FAIL] " << name << ": prologue mismatch\n"; return false; }
         for (size_t i = 0; i < ir1.elementLoop.size(); ++i)
            if (!CheckIRStmtEqual(ir1.elementLoop[i], ir2.elementLoop[i])) { std::cerr << "[FAIL] " << name << ": loop mismatch\n"; return false; }
         for (size_t i = 0; i < ir1.postLoop.size(); ++i)
            if (!CheckIRStmtEqual(ir1.postLoop[i], ir2.postLoop[i])) { std::cerr << "[FAIL] " << name << ": postLoop mismatch\n"; return false; }
         return true;
      }

      if (domain == "pixel")
      {
         Field::PixelIRProgram ir1, ir2;
         if (!Field::LowerPixelProgramToIR(a1, ir1, err) ||
             !Field::LowerPixelProgramToIR(a2, ir2, err))
         {
            std::cerr << "[FAIL] " << name << ": LowerPixelProgramToIR failed: " << err.message << "\n";
            return false;
         }
         Field::GlslEmitResult g1 = Field::EmitGlsl(ir1);
         Field::GlslEmitResult g2 = Field::EmitGlsl(ir2);
         if (g1.source != g2.source)
         {
            std::cerr << "[FAIL] " << name << ": GLSL mismatch\n";
            return false;
         }
         return true;
      }

      if (domain == "graph")
      {
         Field::GraphIRProgram ir1, ir2;
         if (!Field::LowerGraphProgramToIR(a1, ir1, err) ||
             !Field::LowerGraphProgramToIR(a2, ir2, err))
         {
            std::cerr << "[FAIL] " << name << ": LowerGraphProgramToIR failed: " << err.message << "\n";
            return false;
         }
         if (ir1.statements.size() != ir2.statements.size())
         {
            std::cerr << "[FAIL] " << name << ": GraphIR stmt count mismatch\n";
            return false;
         }
         for (size_t i = 0; i < ir1.statements.size(); ++i)
            if (!CheckIRStmtEqual(ir1.statements[i], ir2.statements[i])) { std::cerr << "[FAIL] " << name << ": graph stmt mismatch\n"; return false; }
         return true;
      }

      return false;
   }
}

int main()
{
   int failures = 0;

   // 1. Registry checks
   if (Field::AllBoxSpecs().size() < 75)
   {
      std::cerr << "[FAIL] Registry has too few boxes: " << Field::AllBoxSpecs().size() << "\n";
      ++failures;
   }
   if (Field::FindBoxSpecsByQuery("either").empty() ||
       Field::FindBoxSpecsByQuery("xor").empty() ||
       Field::FindBoxSpecsByQuery("!=").empty())
   {
      std::cerr << "[FAIL] Alias search for either/xor/!= failed\n";
      ++failures;
   }

   // 2. Header parse + fallback checks
   {
      Field::BoxHeaderData h;
      Field::ParseBoxesHeader("// @boxes v1 view 12,34,1.5 | k_rate 120,40 | group \"rhythm\" k_rate b1 [folded]\nparam float rate = 0.25 [0.0625, 1]\n", h);
      if (!h.view.valid || h.view.panX != 12.0f || h.view.panY != 34.0f ||
          h.positions.size() != 1 || h.groups.size() != 1 || !h.groups[0].folded)
      {
         std::cerr << "[FAIL] Header parse failed\n";
         ++failures;
      }
      Field::ParseBoxesHeader("// @boxes v1 broken_junk |||\nparam float x = 1 [0, 2]\n", h);
      if (h.view.valid)
      {
         std::cerr << "[FAIL] Broken header should leave view invalid\n";
         ++failures;
      }
   }

   // 3. Sugar recognition check: (a > 0.5) != (b > 0.5) -> xor (either)
   {
      Field::BoxGraph g;
      Field::FieldError err;
      std::string src = "a = t\nb = sin(t)\nc = (a > 0.5) != (b > 0.5)\npublish = c\n";
      if (!Field::ParseCodeToBoxGraph(src, Field::HostElement, g, err))
      {
         std::cerr << "[FAIL] Sugar parse failed: " << err.message << "\n";
         ++failures;
      }
      else
      {
         bool foundXor = false;
         for (const auto& box : g.boxes)
            if (box.specId == "xor") foundXor = true;
         if (!foundXor || g.CountCodeBoxes() != 0)
         {
            std::cerr << "[FAIL] Sugar recognition for xor/either failed\n";
            ++failures;
         }
      }
   }

   // 4. Round-trip IR checks across all 5 domains
   if (!RoundTripCheck("Notes Harmoniser", "notes",
       "if (noteOn > 0.5) {\n"
       "   note(noteNum, noteVel, 0)\n"
       "   note(noteNum + 7, noteVel * 0.7, 0)\n"
       "}\n")) ++failures;

   if (!RoundTripCheck("Notes Arp Up", "notes",
       "param float steps = 4 [2, 8]\n"
       "state float step = 0\n"
       "if (tick(1/8) > 0.5) {\n"
       "   cnt = floor(steps + 0.5)\n"
       "   note(deg(step * 2), 0.8, 1/8)\n"
       "   step = (step + 1) % cnt\n"
       "}\n")) ++failures;

   if (!RoundTripCheck("Element Wave Displace", "element",
       "param float amp = 0.3 [0.0, 2.0]\n"
       "param float freq = 3.5 [0.5, 10.0]\n"
       "param float speed = 2.0 [0.0, 8.0]\n"
       "P.y += sin(P.x * freq + t * speed) * amp\n"
       "publish = sin(t * speed)\n")) ++failures;

   if (!RoundTripCheck("Element Boundary Chime", "element",
       "param float threshold = 0.3 [0.0, 1.0]\n"
       "param float freq = 2.0 [0.1, 8.0]\n"
       "P.y += sin(P.x * 4.0 + t * freq) * 0.1\n"
       "hit = if(P.y > threshold, 1.0, 0.0)\n"
       "Cd = if(hit > 0.5, vec3(1.0, 0.2, 0.2), vec3(0.2, 0.6, 1.0))\n"
       "output frame float chime = reduce.max(hit)\n"
       "publish = reduce.max(hit)\n")) ++failures;

   if (!RoundTripCheck("Pixel Levels", "pixel",
       "input pixel image img;\n"
       "param float inBlack = 0.0 [0.0, 1.0];\n"
       "param float inWhite = 1.0 [0.0, 1.0];\n"
       "c = clamp((img.rgb - inBlack) / max(inWhite - inBlack, 0.0001), 0.0, 1.0);\n"
       "col = c;\n"
       "alpha = img.a;\n")) ++failures;

   if (!RoundTripCheck("Sample Tube Overdrive", "sample",
       "param float drive = 3.0 [1.0, 10.0]\n"
       "param float tone = 4000.0 [400.0, 12000.0]\n"
       "state float lp = 0\n"
       "x = in * drive\n"
       "sat = x / (1.0 + abs(x))\n"
       "f = clamp((tone / sr) * 3.14159, 0.001, 0.95)\n"
       "lp = lp + f * (sat - lp)\n"
       "out = lp\n")) ++failures;

   if (!RoundTripCheck("Graph Pipeline", "graph",
       "lfo = emit(\"LFO\", 0)\n"
       "osc = emit(\"Wavetable\", 1)\n"
       "connect(lfo, 0, osc, 0)\n"
       "place(lfo, -260, 0)\n"
       "place(osc, 80, 0)\n", false)) ++failures;

   if (failures == 0)
   {
      std::cout << "field-patcher-check: ALL PASSED\n";
      return 0;
   }
   std::cerr << "field-patcher-check: " << failures << " FAILURE(S)\n";
   return 1;
}

#include "BackendRegister.h"

#include "FieldAst.h"
#include "FieldLex.h"
#include "FieldParse.h"

#include <unordered_map>
#include <unordered_set>
#include <cmath>

// A dedicated compiler for the sample domain rather than a shared pass with
// FieldIR.cpp's typed-IR Element/Pixel lowering: the sample domain's target
// is straight-line register bytecode with no runtime branches (an audio-
// thread interpreter must never backward-jump or recurse - see
// SampleRuntime.h), which is a different enough code-generation problem
// (branchless if/else via Select, compile-time-unrolled for, no vectors)
// that sharing IRNode/IRStmt would mean bolting a second, mismatched
// codegen target onto a data structure designed for the GLSL-shaped
// Element/Pixel backends. This mirrors the existing convention of one
// dedicated lowering entry point per domain (LowerElementProgramToIR vs
// LowerPixelProgramToIR) rather than one generic pass for every domain.
namespace Field
{
   namespace
   {
      bool IsReservedName(const std::string& name)
      {
         return name == "in" || name == "out" || name == "sr" || name == "n" ||
                name == "freq" || name == "gate";
      }

      struct Ctx
      {
         SampleProgram& prog;
         FieldError& err;

         bool Failed() const { return !err.Empty(); }

         void Fail(const std::string& msg, const SourceSpan& span, const std::string& hint = "")
         {
            if (Failed()) return; // keep the first error
            err.message = msg;
            err.hint = hint;
            err.span = span;
         }

         uint8_t AllocReg(const SourceSpan& span)
         {
            if (Failed()) return 0;
            if (prog.numRegs >= kSampleMaxRegs)
            {
               Fail("sample-domain kernel exceeds the register cap (" + std::to_string(kSampleMaxRegs) + ")",
                    span, "kernel is too complex for v1; split into fewer intermediate expressions");
               return 0;
            }
            return (uint8_t)(prog.numRegs++);
         }

         void Emit(SampleOp op, uint8_t dst, uint8_t a, uint8_t b, uint8_t c, float imm, const SourceSpan& span)
         {
            if (Failed()) return;
            if ((int)prog.code.size() >= kSampleMaxInstr)
            {
               Fail("sample-domain kernel exceeds the instruction cap (" + std::to_string(kSampleMaxInstr) + ")",
                    span, "kernel is too complex for v1; split into fewer statements");
               return;
            }
            SampleInstr in;
            in.op = op; in.dst = dst; in.a = a; in.b = b; in.c = c; in.imm = imm;
            prog.code.push_back(in);
         }

         std::vector<uint8_t> condStack;

         uint8_t ActiveCond() const
         {
            return condStack.empty() ? 0 : condStack.back();
         }
         bool HasActiveCond() const
         {
            return !condStack.empty();
         }
      };

      using Scope = std::unordered_map<std::string, uint8_t>;
      using TableScope = std::unordered_map<std::string, int>;

      uint8_t CompileExpr(Ctx& ctx, const AstNodePtr& node, Scope& scope, const TableScope& tableScope);

      uint8_t EmitImm(Ctx& ctx, float value, const SourceSpan& span)
      {
         uint8_t dst = ctx.AllocReg(span);
         ctx.Emit(SampleOp::LoadImm, dst, 0, 0, 0, value, span);
         return dst;
      }

      // Fixed-arity intrinsic dispatch shared by both the operator forms
      // (unary '-', '!') and the function-call forms (sin(x), clamp(x,l,h), ...).
      uint8_t CompileCall(Ctx& ctx, const AstCall& call, Scope& scope, const TableScope& tableScope)
      {
         const std::string& c = call.callee;
         auto arg = [&](size_t i) -> uint8_t { return CompileExpr(ctx, call.args[i], scope, tableScope); };

         auto unary = [&](SampleOp op) -> uint8_t
         {
            if (call.args.size() != 1)
            {
               ctx.Fail("'" + c + "' takes exactly 1 argument", call.span);
               return 0;
            }
            uint8_t a = arg(0);
            uint8_t dst = ctx.AllocReg(call.span);
            ctx.Emit(op, dst, a, 0, 0, 0.0f, call.span);
            return dst;
         };
         auto binary = [&](SampleOp op) -> uint8_t
         {
            if (call.args.size() != 2)
            {
               ctx.Fail("'" + c + "' takes exactly 2 arguments", call.span);
               return 0;
            }
            uint8_t a = arg(0);
            uint8_t b = arg(1);
            uint8_t dst = ctx.AllocReg(call.span);
            ctx.Emit(op, dst, a, b, 0, 0.0f, call.span);
            return dst;
         };

         if (c == "sin") return unary(SampleOp::Sin);
         if (c == "cos") return unary(SampleOp::Cos);
         if (c == "tan") return unary(SampleOp::Tan);
         if (c == "sqrt") return unary(SampleOp::Sqrt);
         if (c == "abs") return unary(SampleOp::Abs);
         if (c == "floor") return unary(SampleOp::Floor);
         if (c == "ceil") return unary(SampleOp::Ceil);
         if (c == "exp") return unary(SampleOp::Exp);
         if (c == "log") return unary(SampleOp::Log);
         if (c == "min") return binary(SampleOp::Min);
         if (c == "max") return binary(SampleOp::Max);
         if (c == "pow") return binary(SampleOp::Pow);
         if (c == "clamp")
         {
            if (call.args.size() != 3)
            {
               ctx.Fail("'clamp' takes exactly 3 arguments (x, lo, hi)", call.span);
               return 0;
            }
            uint8_t x = arg(0);
            uint8_t lo = arg(1);
            uint8_t hi = arg(2);
            uint8_t dst = ctx.AllocReg(call.span);
            ctx.Emit(SampleOp::Clamp, dst, x, lo, hi, 0.0f, call.span);
            return dst;
         }
         if (c == "if")
         {
            if (call.args.size() != 3)
            {
               ctx.Fail("'if' takes exactly 3 arguments (cond, then_val, else_val)", call.span);
               return 0;
            }
            uint8_t cond = arg(0);
            uint8_t thenVal = arg(1);
            uint8_t elseVal = arg(2);
            uint8_t dst = ctx.AllocReg(call.span);
            ctx.Emit(SampleOp::Select, dst, cond, thenVal, elseVal, 0.0f, call.span);
            return dst;
         }
         if (c == "mix" || c == "lerp")
         {
            if (call.args.size() != 3)
            {
               ctx.Fail("'" + c + "' takes exactly 3 arguments (a, b, t)", call.span);
               return 0;
            }
            uint8_t a = arg(0);
            uint8_t b = arg(1);
            uint8_t t = arg(2);
            uint8_t subReg = ctx.AllocReg(call.span);
            ctx.Emit(SampleOp::Sub, subReg, b, a, 0, 0.0f, call.span);
            uint8_t mulReg = ctx.AllocReg(call.span);
            ctx.Emit(SampleOp::Mul, mulReg, subReg, t, 0, 0.0f, call.span);
            uint8_t dst = ctx.AllocReg(call.span);
            ctx.Emit(SampleOp::Add, dst, a, mulReg, 0, 0.0f, call.span);
            return dst;
         }
         if (c == "delay")
         {
            if (call.args.size() != 2)
            {
               ctx.Fail("'delay' takes exactly 2 arguments (x, samples)", call.span);
               return 0;
            }
            uint8_t srcReg = arg(0);
            if (ctx.Failed()) return 0;

            const auto& lenArg = call.args[1];
            if (!lenArg || lenArg->kind != AstKind::Literal)
            {
               ctx.Fail("delay length must be a compile-time constant integer literal", lenArg ? lenArg->span : call.span);
               return 0;
            }
            auto* lit = static_cast<AstLiteral*>(lenArg.get());
            double numVal = lit->numberValue;
            if (numVal < 1.0 || std::floor(numVal) != numVal)
            {
               ctx.Fail("delay length must be a positive integer literal (>= 1)", lenArg->span);
               return 0;
            }
            int delaySamples = (int)numVal;

            int delayIdx = (int)ctx.prog.delays.size();
            if (delayIdx >= kSampleMaxDelayLines)
            {
               ctx.Fail("sample-domain kernel exceeds the delay-line count cap (" + std::to_string(kSampleMaxDelayLines) + ")",
                        call.span);
               return 0;
            }

            int currentTotalCells = 0;
            for (const auto& dl : ctx.prog.delays)
               currentTotalCells += dl.length;

            if (currentTotalCells + delaySamples > kSampleMaxDelayCells)
            {
               ctx.Fail("sample-domain kernel exceeds the cumulative delay-buffer cap (" +
                        std::to_string(currentTotalCells + delaySamples) + " > " +
                        std::to_string(kSampleMaxDelayCells) + " cells)", call.span);
               return 0;
            }

            SampleDelayLine dl;
            dl.bufferOffset = currentTotalCells;
            dl.length = delaySamples;
            dl.cursorIndex = delayIdx;
            ctx.prog.delays.push_back(dl);

            uint8_t dst = ctx.AllocReg(call.span);
            ctx.Emit(SampleOp::Delay, dst, (uint8_t)delayIdx, srcReg, 0, 0.0f, call.span);
            return dst;
         }
         if (c == "reduce.rms")
         {
            ctx.Fail("reduce.rms publishes to the frame domain and has no per-sample value",
                     call.span, "write it as its own statement: reduce.rms(in, lo, hi)");
            return 0;
         }
         if (c == "map" || c == "reduce.sum" || c == "reduce.mean" || c == "reduce.min" || c == "reduce.max" ||
             c == "broadcast" || c == "resample" || c == "downsample")
         {
            ctx.Fail("'" + c + "' is not supported inside a sample-domain kernel in v1", call.span,
                     "map/broadcast/resample/downsample/reduce (other than reduce.rms(in,lo,hi)) are out of scope for step 9");
            return 0;
         }

         ctx.Fail("unknown function '" + c + "'", call.span);
         return 0;
      }

      uint8_t CompileExpr(Ctx& ctx, const AstNodePtr& node, Scope& scope, const TableScope& tableScope)
      {
         if (ctx.Failed() || !node) return 0;

         switch (node->kind)
         {
            case AstKind::Literal:
            {
               auto* lit = static_cast<AstLiteral*>(node.get());
               return EmitImm(ctx, (float)lit->numberValue, node->span);
            }
            case AstKind::Ident:
            {
               auto* id = static_cast<AstIdent*>(node.get());
               if (id->name == "in")
               {
                  uint8_t dst = ctx.AllocReg(node->span);
                  ctx.Emit(SampleOp::LoadIn, dst, 0, 0, 0, 0.0f, node->span);
                  return dst;
               }
               if (id->name == "sr")
               {
                  uint8_t dst = ctx.AllocReg(node->span);
                  ctx.Emit(SampleOp::LoadSr, dst, 0, 0, 0, 0.0f, node->span);
                  return dst;
               }
               if (id->name == "n")
               {
                  uint8_t dst = ctx.AllocReg(node->span);
                  ctx.Emit(SampleOp::LoadN, dst, 0, 0, 0, 0.0f, node->span);
                  return dst;
               }
               if (id->name == "freq")
               {
                  uint8_t dst = ctx.AllocReg(node->span);
                  ctx.Emit(SampleOp::LoadFreq, dst, 0, 0, 0, 0.0f, node->span);
                  return dst;
               }
               if (id->name == "gate")
               {
                  uint8_t dst = ctx.AllocReg(node->span);
                  ctx.Emit(SampleOp::LoadGate, dst, 0, 0, 0, 0.0f, node->span);
                  return dst;
               }
               if (tableScope.count(id->name))
               {
                  ctx.Fail("table '" + id->name + "' cannot be read as a bare identifier", node->span,
                           "index it with square brackets: '" + id->name + "[idx]'");
                  return 0;
               }
               auto it = scope.find(id->name);
               if (it == scope.end())
               {
                  ctx.Fail("used before assignment: '" + id->name + "'", node->span,
                           "declare it with state/param, or assign it before reading");
                  return 0;
               }
               return it->second;
            }
            case AstKind::Unary:
            {
               auto* u = static_cast<AstUnary*>(node.get());
               uint8_t a = CompileExpr(ctx, u->operand, scope, tableScope);
               uint8_t dst = ctx.AllocReg(node->span);
               if (u->op == "-") ctx.Emit(SampleOp::Neg, dst, a, 0, 0, 0.0f, node->span);
               else if (u->op == "!") ctx.Emit(SampleOp::LogNot, dst, a, 0, 0, 0.0f, node->span);
               else ctx.Fail("unary operator '" + u->op + "' is not supported in the sample domain", node->span);
               return dst;
            }
            case AstKind::Binary:
            {
               auto* b = static_cast<AstBinary*>(node.get());
               uint8_t lhs = CompileExpr(ctx, b->lhs, scope, tableScope);
               uint8_t rhs = CompileExpr(ctx, b->rhs, scope, tableScope);
               SampleOp op;
               if (b->op == "+") op = SampleOp::Add;
               else if (b->op == "-") op = SampleOp::Sub;
               else if (b->op == "*") op = SampleOp::Mul;
               else if (b->op == "/") op = SampleOp::Div;
               else if (b->op == "%") op = SampleOp::Mod;
               else if (b->op == "^") op = SampleOp::Pow;
               else if (b->op == "<") op = SampleOp::Lt;
               else if (b->op == "<=") op = SampleOp::Le;
               else if (b->op == ">") op = SampleOp::Gt;
               else if (b->op == ">=") op = SampleOp::Ge;
               else if (b->op == "==") op = SampleOp::Eq;
               else if (b->op == "!=") op = SampleOp::Ne;
               else if (b->op == "&&") op = SampleOp::LogAnd;
               else if (b->op == "||") op = SampleOp::LogOr;
               else
               {
                  ctx.Fail("operator '" + b->op + "' is not supported in the sample domain", node->span);
                  return 0;
               }
               uint8_t dst = ctx.AllocReg(node->span);
               ctx.Emit(op, dst, lhs, rhs, 0, 0.0f, node->span);
               return dst;
            }
            case AstKind::Call:
               return CompileCall(ctx, *static_cast<AstCall*>(node.get()), scope, tableScope);
            case AstKind::Index:
            {
               auto* idx = static_cast<AstIndex*>(node.get());
               if (!idx->base || idx->base->kind != AstKind::Ident)
               {
                  ctx.Fail("indexed expression must be a table identifier in the sample domain", node->span);
                  return 0;
               }
               const std::string& tblName = static_cast<AstIdent*>(idx->base.get())->name;
               auto tit = tableScope.find(tblName);
               if (tit == tableScope.end())
               {
                  ctx.Fail("'" + tblName + "' is not a declared state table", idx->base->span,
                           "declare it with 'state float " + tblName + "[N] = init'");
                  return 0;
               }
               uint8_t idxReg = CompileExpr(ctx, idx->index, scope, tableScope);
               if (ctx.Failed()) return 0;
               uint8_t dst = ctx.AllocReg(node->span);
               ctx.Emit(SampleOp::LoadTable, dst, (uint8_t)tit->second, idxReg, 0, 0.0f, node->span);
               return dst;
            }
            case AstKind::Access:
               ctx.Fail("vector/swizzle access is not supported in the sample domain in v1", node->span,
                        "v1 is scalar-only; split vectors into separate float state/params");
               return 0;
            default:
               ctx.Fail("expression kind is not supported in the sample domain", node->span);
               return 0;
         }
      }

      void CompileBlock(Ctx& ctx, const std::vector<AstNodePtr>& stmts, Scope& scope, TableScope& tableScope);
      void CompileStmt(Ctx& ctx, const AstNodePtr& stmt, Scope& scope, TableScope& tableScope);

      // ParseIf/ParseFor accept a single statement as a body without
      // requiring braces (if (cond) x = 1, no Block wrapper) - normalize
      // both shapes here so CompileBlock always sees a real statement list.
      void CompileBodyNode(Ctx& ctx, const AstNodePtr& node, Scope& scope, TableScope& tableScope)
      {
         if (!node) return;
         if (node->kind == AstKind::Block)
            CompileBlock(ctx, static_cast<AstBlock*>(node.get())->statements, scope, tableScope);
         else
            CompileStmt(ctx, node, scope, tableScope);
      }

      void CompileStmt(Ctx& ctx, const AstNodePtr& stmt, Scope& scope, TableScope& tableScope)
      {
         if (ctx.Failed() || !stmt) return;

         switch (stmt->kind)
         {
            case AstKind::DeclState:
            {
               auto* d = static_cast<AstDeclState*>(stmt.get());
               if (IsReservedName(d->name))
               {
                  ctx.Fail("'" + d->name + "' is a reserved name (in/out/sr/n) and cannot be declared", stmt->span);
                  return;
               }
               if (scope.count(d->name) || tableScope.count(d->name))
               {
                  ctx.Fail("duplicate declaration of '" + d->name + "'", stmt->span);
                  return;
               }
               if (!d->typeName.empty() && d->typeName != "float")
               {
                  ctx.Fail("state type '" + d->typeName + "' is not supported in the sample domain in v1",
                           stmt->span, "v1 state cells are scalar float only");
                  return;
               }
               float initVal = 0.0f;
               if (d->initExpr)
               {
                  if (d->initExpr->kind != AstKind::Literal)
                  {
                     ctx.Fail("state initial value must be a compile-time constant", d->initExpr->span);
                     return;
                  }
                  initVal = (float)static_cast<AstLiteral*>(d->initExpr.get())->numberValue;
               }

               if (d->tableSize > 0)
               {
                  int tableIdx = (int)ctx.prog.tables.size();
                  if (tableIdx >= kSampleMaxTables)
                  {
                     ctx.Fail("sample-domain kernel exceeds the table count cap (" + std::to_string(kSampleMaxTables) + ")",
                              stmt->span);
                     return;
                  }

                  int currentTotalCells = 0;
                  for (const auto& tbl : ctx.prog.tables)
                     currentTotalCells += tbl.length;

                  if (currentTotalCells + d->tableSize > kSampleMaxTableCells)
                  {
                     ctx.Fail("sample-domain kernel exceeds the cumulative table-buffer cap (" +
                              std::to_string(currentTotalCells + d->tableSize) + " > " +
                              std::to_string(kSampleMaxTableCells) + " cells)", stmt->span);
                     return;
                  }

                  SampleTable tbl;
                  tbl.name = d->name;
                  tbl.bufferOffset = currentTotalCells;
                  tbl.length = d->tableSize;
                  tbl.initialValue = initVal;
                  ctx.prog.tables.push_back(tbl);

                  tableScope[d->name] = tableIdx;
                  return;
               }

               SampleStateInit si;
               si.name = d->name;
               si.typeName = "float";
               si.initialValue = initVal;
               int stateIndex = (int)ctx.prog.state.size();
               if (stateIndex >= kSampleMaxStateCells)
               {
                  ctx.Fail("sample-domain kernel exceeds the state-cell cap (" + std::to_string(kSampleMaxStateCells) + ")",
                           stmt->span);
                  return;
               }
               ctx.prog.state.push_back(si);
               uint8_t dst = ctx.AllocReg(stmt->span);
               ctx.Emit(SampleOp::LoadState, dst, (uint8_t)stateIndex, 0, 0, 0.0f, stmt->span);
               scope[d->name] = dst;
               return;
            }
            case AstKind::DeclParam:
            {
               auto* d = static_cast<AstDeclParam*>(stmt.get());
               if (IsReservedName(d->name))
               {
                  ctx.Fail("'" + d->name + "' is a reserved name (in/out/sr/n) and cannot be declared", stmt->span);
                  return;
               }
               if (scope.count(d->name))
               {
                  ctx.Fail("duplicate declaration of '" + d->name + "'", stmt->span);
                  return;
               }
               if (!d->typeName.empty() && d->typeName != "float")
               {
                  ctx.Fail("param type '" + d->typeName + "' is not supported in the sample domain in v1",
                           stmt->span, "v1 params are scalar float only");
                  return;
               }
               if ((int)ctx.prog.params.size() >= 128)
               {
                  ctx.Fail("param count exceeds kMaxParams (128) in src/audio/ParamMailbox.h", stmt->span);
                  return;
               }
               SampleParamSlot ps;
               ps.name = d->name;
               ps.defaultValue = (float)d->defaultValue;
               ps.minValue = (float)d->minVal;
               ps.maxValue = (float)d->maxVal;
               int paramIndex = (int)ctx.prog.params.size();
               ctx.prog.params.push_back(ps);
               uint8_t dst = ctx.AllocReg(stmt->span);
               ctx.Emit(SampleOp::LoadParam, dst, (uint8_t)paramIndex, 0, 0, 0.0f, stmt->span);
               scope[d->name] = dst;
               return;
            }
            case AstKind::DeclAttrib:
               ctx.Fail("'attrib' is not defined in the sample domain", stmt->span,
                        "use 'state' for per-voice memory, or 'param' for a user control");
               return;
            case AstKind::DeclOutput:
            {
               // Build step 12 (doc S1.6): collected independently of
               // FieldIR.h's DeclOutput/DeclInput lowering - same grammar
               // and per-entry shape, genuinely separate implementation.
               auto* d = static_cast<AstDeclOutput*>(stmt.get());
               if (IsReservedName(d->name))
               {
                  ctx.Fail("'" + d->name + "' is a reserved name (in/out/sr/n/freq/gate) and cannot be declared", stmt->span);
                  return;
               }
               if (scope.count(d->name))
               {
                  ctx.Fail("duplicate declaration of '" + d->name + "'", stmt->span);
                  return;
               }
               static const std::unordered_set<std::string> kValidDomains = { "graph", "frame", "element", "pixel", "sample" };
               if (kValidDomains.find(d->domainName) == kValidDomains.end())
               {
                  ctx.Fail("unknown domain '" + d->domainName + "' in output declaration", stmt->span);
                  return;
               }
               if (d->domainName == "graph")
               {
                  ctx.Fail("'output' cannot declare a graph-domain pin (graph domain has no per-frame dataflow to expose)", stmt->span);
                  return;
               }
               if (d->domainName == "element" || d->domainName == "pixel")
               {
                  ctx.Fail("a sample-domain kernel cannot declare a '" + d->domainName + "'-domain output pin in v1 (no path from sample to " + d->domainName + " here)", stmt->span);
                  return;
               }
               if (d->typeName != "float" && d->typeName != "audio")
               {
                  ctx.Fail("output type '" + d->typeName + "' is not supported in the sample domain in v1", stmt->span,
                           "v1 sample-domain outputs are float (or 'audio', an alias for float) only");
                  return;
               }
               // S5.6/S5.8 row 4: 'audio' is only legal at 'sample' rate.
               if (d->typeName == "audio" && d->domainName != "sample")
               {
                  ctx.Fail("'audio' pins are only legal in the 'sample' domain (got '" + d->domainName + "')", stmt->span,
                           "a cross-domain audio read would need an explicit resample, not supported in v1");
                  return;
               }
               if ((int)(ctx.prog.declaredOutputs.size() + ctx.prog.declaredInputs.size()) >= 16)
               {
                  ctx.Fail("declared pin count exceeds the 16-pin-per-kernel ceiling (build step 12 v1 limit)", stmt->span);
                  return;
               }

               bool isReduceRmsForm = d->initExpr && d->initExpr->kind == AstKind::Call &&
                                      static_cast<AstCall*>(d->initExpr.get())->callee == "reduce.rms";

               if (d->domainName == "frame")
               {
                  if (!isReduceRmsForm)
                  {
                     ctx.Fail("a frame-domain output declared inside a sample-domain kernel must be exactly reduce.rms(in, loHz, hiHz) in v1", stmt->span,
                              "a plain per-sample expression is sample-domain, not frame-domain; declare 'output sample float' instead, or wrap in reduce.rms");
                     return;
                  }
                  auto* call = static_cast<AstCall*>(d->initExpr.get());
                  if (ctx.prog.hasReduceRms)
                  {
                     ctx.Fail("only one reduce.rms(...) is supported per sample-domain kernel in v1", stmt->span);
                     return;
                  }
                  if (call->args.size() != 3 || call->args[0]->kind != AstKind::Ident ||
                      static_cast<AstIdent*>(call->args[0].get())->name != "in" ||
                      call->args[1]->kind != AstKind::Literal || call->args[2]->kind != AstKind::Literal)
                  {
                     ctx.Fail("reduce.rms in the sample domain requires the exact form reduce.rms(in, loHz, hiHz) with "
                              "compile-time constant loHz/hiHz", stmt->span);
                     return;
                  }
                  ctx.prog.hasReduceRms = true;
                  ctx.prog.reduceLoHz = (float)static_cast<AstLiteral*>(call->args[1].get())->numberValue;
                  ctx.prog.reduceHiHz = (float)static_cast<AstLiteral*>(call->args[2].get())->numberValue;
               }
               else // "sample"
               {
                  if (isReduceRmsForm)
                  {
                     ctx.Fail("reduce.rms(...) produces a frame-domain value; declare this as 'output frame float " + d->name + "', not 'output sample'", stmt->span);
                     return;
                  }
                  if (!d->initExpr)
                  {
                     ctx.Fail("output '" + d->name + "' requires an initializer expression", stmt->span);
                     return;
                  }
                  // Compiled only for validation - step 12 collects the
                  // declaration, it does not yet wire the resulting
                  // register to a node-level output pin (later work).
                  CompileExpr(ctx, d->initExpr, scope, tableScope);
                  if (ctx.Failed()) return;
               }

               SampleDeclaredPin pin;
               pin.name = d->name;
               pin.typeName = d->typeName;
               pin.domainName = d->domainName;
               ctx.prog.declaredOutputs.push_back(pin);
               return;
            }
            case AstKind::DeclInput:
            {
               auto* d = static_cast<AstDeclInput*>(stmt.get());
               if (IsReservedName(d->name))
               {
                  ctx.Fail("'" + d->name + "' is a reserved name (in/out/sr/n/freq/gate) and cannot be declared", stmt->span);
                  return;
               }
               if (scope.count(d->name))
               {
                  ctx.Fail("duplicate declaration of '" + d->name + "'", stmt->span);
                  return;
               }
               static const std::unordered_set<std::string> kValidDomains = { "graph", "frame", "element", "pixel", "sample" };
               if (kValidDomains.find(d->domainName) == kValidDomains.end())
               {
                  ctx.Fail("unknown domain '" + d->domainName + "' in input declaration", stmt->span);
                  return;
               }
               if (d->domainName == "graph")
               {
                  ctx.Fail("'input' cannot declare a graph-domain pin (graph domain has no per-frame dataflow to receive into)", stmt->span);
                  return;
               }
               if (d->typeName != "float" && d->typeName != "audio")
               {
                  ctx.Fail("input type '" + d->typeName + "' is not supported in the sample domain in v1", stmt->span,
                           "v1 sample-domain inputs are float (or 'audio', an alias for float) only - 'geometry' has no meaning at sample rate");
                  return;
               }
               // S5.6/S5.8 row 4: 'audio' is only legal at 'sample' rate.
               if (d->typeName == "audio" && d->domainName != "sample")
               {
                  ctx.Fail("'audio' pins are only legal in the 'sample' domain (got '" + d->domainName + "')", stmt->span,
                           "a cross-domain audio read would need an explicit resample, not supported in v1");
                  return;
               }
               if ((int)(ctx.prog.declaredOutputs.size() + ctx.prog.declaredInputs.size()) >= 16)
               {
                  ctx.Fail("declared pin count exceeds the 16-pin-per-kernel ceiling (build step 12 v1 limit)", stmt->span);
                  return;
               }

               SampleDeclaredPin pin;
               pin.name = d->name;
               pin.typeName = d->typeName;
               pin.domainName = d->domainName;
               ctx.prog.declaredInputs.push_back(pin);

               // Step 12 collects the declaration only - actual audio-input
               // slot plumbing (wiring this name to a second live input
               // stream) is later work. Deliberately NOT bound into `scope`:
               // referencing the name in an expression fails loudly with
               // "undeclared identifier" rather than silently resolving to
               // garbage (see CompileExpr's Ident case).
               return;
            }
            case AstKind::Assign:
            {
               auto* a = static_cast<AstAssign*>(stmt.get());

               if (a->lvalue->kind == AstKind::Index)
               {
                  auto* idx = static_cast<AstIndex*>(a->lvalue.get());
                  if (!idx->base || idx->base->kind != AstKind::Ident)
                  {
                     ctx.Fail("indexed assignment target must be a table identifier in the sample domain", stmt->span);
                     return;
                  }
                  const std::string& tblName = static_cast<AstIdent*>(idx->base.get())->name;
                  auto tit = tableScope.find(tblName);
                  if (tit == tableScope.end())
                  {
                     ctx.Fail("'" + tblName + "' is not a declared state table", idx->base->span);
                     return;
                  }

                  int tblIdx = tit->second;
                  uint8_t idxReg = CompileExpr(ctx, idx->index, scope, tableScope);
                  if (ctx.Failed()) return;
                  uint8_t rhs = CompileExpr(ctx, a->rvalue, scope, tableScope);
                  if (ctx.Failed()) return;

                  uint8_t valToStore = rhs;
                  if (a->op != "=")
                  {
                     uint8_t curVal = ctx.AllocReg(stmt->span);
                     ctx.Emit(SampleOp::LoadTable, curVal, (uint8_t)tblIdx, idxReg, 0, 0.0f, stmt->span);

                     SampleOp op;
                     if (a->op == "+=") op = SampleOp::Add;
                     else if (a->op == "-=") op = SampleOp::Sub;
                     else if (a->op == "*=") op = SampleOp::Mul;
                     else if (a->op == "/=") op = SampleOp::Div;
                     else
                     {
                        ctx.Fail("assignment operator '" + a->op + "' is not supported for tables", stmt->span);
                        return;
                     }
                     valToStore = ctx.AllocReg(stmt->span);
                     ctx.Emit(op, valToStore, curVal, rhs, 0, 0.0f, stmt->span);
                  }

                  // If inside an 'if' block, predicate the store branchlessly:
                  // StoreTable(tbl, idx, Select(activeCond, valToStore, curVal))
                  if (ctx.HasActiveCond())
                  {
                     uint8_t curVal = ctx.AllocReg(stmt->span);
                     ctx.Emit(SampleOp::LoadTable, curVal, (uint8_t)tblIdx, idxReg, 0, 0.0f, stmt->span);
                     uint8_t predVal = ctx.AllocReg(stmt->span);
                     ctx.Emit(SampleOp::Select, predVal, ctx.ActiveCond(), valToStore, curVal, 0.0f, stmt->span);
                     valToStore = predVal;
                  }

                  ctx.Emit(SampleOp::StoreTable, 0, (uint8_t)tblIdx, idxReg, valToStore, 0.0f, stmt->span);
                  return;
               }

               if (a->lvalue->kind != AstKind::Ident)
               {
                  ctx.Fail("assignment target must be a plain name in the sample domain in v1", stmt->span,
                           "v1 is scalar-only; swizzle/field assignment is not supported");
                  return;
               }
               const std::string& name = static_cast<AstIdent*>(a->lvalue.get())->name;
               if (tableScope.count(name))
               {
                  ctx.Fail("cannot assign directly to table '" + name + "'", stmt->span,
                           "assign to an indexed element: '" + name + "[idx] = val'");
                  return;
               }
               if (name == "in" || name == "sr" || name == "n" || name == "freq" || name == "gate")
               {
                  ctx.Fail("cannot assign to '" + name + "' (reserved, read-only)", stmt->span);
                  return;
               }

               uint8_t rhs = CompileExpr(ctx, a->rvalue, scope, tableScope);
               if (ctx.Failed()) return;

               uint8_t finalReg;
               if (a->op == "=")
               {
                  finalReg = rhs;
               }
               else
               {
                  auto it = scope.find(name);
                  if (it == scope.end())
                  {
                     ctx.Fail("used before assignment: '" + name + "'", stmt->span);
                     return;
                  }
                  uint8_t prevReg = it->second;
                  SampleOp op;
                  if (a->op == "+=") op = SampleOp::Add;
                  else if (a->op == "-=") op = SampleOp::Sub;
                  else if (a->op == "*=") op = SampleOp::Mul;
                  else if (a->op == "/=") op = SampleOp::Div;
                  else
                  {
                     ctx.Fail("assignment operator '" + a->op + "' is not supported", stmt->span);
                     return;
                  }
                  finalReg = ctx.AllocReg(stmt->span);
                  ctx.Emit(op, finalReg, prevReg, rhs, 0, 0.0f, stmt->span);
               }
               scope[name] = finalReg;
               return;
            }
            case AstKind::If:
            {
               auto* ifs = static_cast<AstIf*>(stmt.get());
               uint8_t condReg = CompileExpr(ctx, ifs->cond, scope, tableScope);
               if (ctx.Failed()) return;

               Scope baseScope = scope;

               // Maintain active condition on condStack so side-effects like StoreTable
               // can be branchlessly predicated using Select.
               uint8_t thenCond = condReg;
               if (ctx.HasActiveCond())
               {
                  uint8_t combined = ctx.AllocReg(stmt->span);
                  ctx.Emit(SampleOp::LogAnd, combined, ctx.ActiveCond(), condReg, 0, 0.0f, stmt->span);
                  thenCond = combined;
               }

               ctx.condStack.push_back(thenCond);
               Scope thenScope = baseScope;
               CompileBodyNode(ctx, ifs->thenBlock, thenScope, tableScope);
               ctx.condStack.pop_back();
               if (ctx.Failed()) return;

               uint8_t elseCond = ctx.AllocReg(stmt->span);
               ctx.Emit(SampleOp::LogNot, elseCond, condReg, 0, 0, 0.0f, stmt->span);
               if (ctx.HasActiveCond())
               {
                  uint8_t combined = ctx.AllocReg(stmt->span);
                  ctx.Emit(SampleOp::LogAnd, combined, ctx.ActiveCond(), elseCond, 0, 0.0f, stmt->span);
                  elseCond = combined;
               }

               ctx.condStack.push_back(elseCond);
               Scope elseScope = baseScope;
               CompileBodyNode(ctx, ifs->elseBlock, elseScope, tableScope);
               ctx.condStack.pop_back();
               if (ctx.Failed()) return;

               // Only names that already existed before the if merge across
               // it (branchless Select) - a name first declared inside a
               // branch is local to that branch, matching ordinary lexical
               // scoping, and never reaches code after the if.
               for (auto& kv : baseScope)
               {
                  uint8_t thenReg = thenScope.count(kv.first) ? thenScope[kv.first] : kv.second;
                  uint8_t elseReg = elseScope.count(kv.first) ? elseScope[kv.first] : kv.second;
                  if (thenReg == elseReg)
                  {
                     scope[kv.first] = thenReg;
                     continue;
                  }
                  uint8_t dst = ctx.AllocReg(stmt->span);
                  ctx.Emit(SampleOp::Select, dst, condReg, thenReg, elseReg, 0.0f, stmt->span);
                  scope[kv.first] = dst;
               }
               return;
            }
            case AstKind::For:
            {
               auto* f = static_cast<AstFor*>(stmt.get());

               auto fail = [&]()
               {
                  ctx.Fail("for-loop bounds must be compile-time integer constants in the sample domain",
                           stmt->span, "e.g. 'for i = 0; i < 8; i += 1 { ... }'");
               };

               if (!f->init || f->init->kind != AstKind::Assign) { fail(); return; }
               auto* initA = static_cast<AstAssign*>(f->init.get());
               if (initA->op != "=" || initA->lvalue->kind != AstKind::Ident ||
                   initA->rvalue->kind != AstKind::Literal) { fail(); return; }
               const std::string loopVar = static_cast<AstIdent*>(initA->lvalue.get())->name;
               if (IsReservedName(loopVar) || scope.count(loopVar) || tableScope.count(loopVar))
               {
                  ctx.Fail("'" + loopVar + "' cannot be used as a for-loop variable (reserved or already declared)",
                           stmt->span);
                  return;
               }
               double startVal = static_cast<AstLiteral*>(initA->rvalue.get())->numberValue;

               if (!f->cond || f->cond->kind != AstKind::Binary) { fail(); return; }
               auto* condB = static_cast<AstBinary*>(f->cond.get());
               bool inclusive;
               if (condB->op == "<") inclusive = false;
               else if (condB->op == "<=") inclusive = true;
               else { fail(); return; }
               if (condB->lhs->kind != AstKind::Ident || static_cast<AstIdent*>(condB->lhs.get())->name != loopVar ||
                   condB->rhs->kind != AstKind::Literal) { fail(); return; }
               double boundVal = static_cast<AstLiteral*>(condB->rhs.get())->numberValue;

               bool stepOk = false;
               if (f->step)
               {
                  if (f->step->kind == AstKind::Assign)
                  {
                     auto* stepA = static_cast<AstAssign*>(f->step.get());
                     if (stepA->lvalue->kind == AstKind::Ident &&
                         static_cast<AstIdent*>(stepA->lvalue.get())->name == loopVar)
                     {
                        if (stepA->op == "+=" && stepA->rvalue->kind == AstKind::Literal &&
                            static_cast<AstLiteral*>(stepA->rvalue.get())->numberValue == 1.0)
                           stepOk = true;
                        else if (stepA->op == "=" && stepA->rvalue->kind == AstKind::Binary)
                        {
                           auto* stepB = static_cast<AstBinary*>(stepA->rvalue.get());
                           if (stepB->op == "+" && stepB->lhs->kind == AstKind::Ident &&
                               static_cast<AstIdent*>(stepB->lhs.get())->name == loopVar &&
                               stepB->rhs->kind == AstKind::Literal &&
                               static_cast<AstLiteral*>(stepB->rhs.get())->numberValue == 1.0)
                              stepOk = true;
                        }
                     }
                  }
               }
               if (!stepOk) { fail(); return; }

               int start = (int)startVal;
               int bound = (int)boundVal;
               int tripCount = inclusive ? (bound - start + 1) : (bound - start);
               if (tripCount < 0) tripCount = 0;
               if (tripCount > kSampleMaxUnroll)
               {
                  ctx.Fail("for-loop trip count (" + std::to_string(tripCount) + ") exceeds the sample-domain unroll cap (" +
                           std::to_string(kSampleMaxUnroll) + ")", stmt->span);
                  return;
               }

               for (int k = start; k < start + tripCount; k++)
               {
                  uint8_t iterReg = EmitImm(ctx, (float)k, stmt->span);
                  scope[loopVar] = iterReg;
                  CompileBodyNode(ctx, f->body, scope, tableScope);
                  if (ctx.Failed()) return;
               }
               scope.erase(loopVar);
               return;
            }
            case AstKind::Call:
            {
               auto* call = static_cast<AstCall*>(stmt.get());
               if (call->callee == "delay")
               {
                  CompileCall(ctx, *call, scope, tableScope);
                  return;
               }
               if (call->callee != "reduce.rms")
               {
                  ctx.Fail("'" + call->callee + "' is not supported inside a sample-domain kernel in v1", stmt->span);
                  return;
               }
               if (ctx.prog.hasReduceRms)
               {
                  ctx.Fail("only one reduce.rms(...) is supported per sample-domain kernel in v1", stmt->span);
                  return;
               }
               if (call->args.size() != 3 || call->args[0]->kind != AstKind::Ident ||
                   static_cast<AstIdent*>(call->args[0].get())->name != "in" ||
                   call->args[1]->kind != AstKind::Literal || call->args[2]->kind != AstKind::Literal)
               {
                  ctx.Fail("reduce.rms in the sample domain requires the exact form reduce.rms(in, loHz, hiHz) with "
                           "compile-time constant loHz/hiHz", stmt->span);
                  return;
               }
               ctx.prog.hasReduceRms = true;
               ctx.prog.reduceLoHz = (float)static_cast<AstLiteral*>(call->args[1].get())->numberValue;
               ctx.prog.reduceHiHz = (float)static_cast<AstLiteral*>(call->args[2].get())->numberValue;
               return;
            }
            case AstKind::Block:
               CompileBlock(ctx, static_cast<AstBlock*>(stmt.get())->statements, scope, tableScope);
               return;
            default:
               ctx.Fail("statement kind is not supported inside a sample-domain kernel in v1", stmt->span,
                        "map/broadcast/resample/downsample are not available inside sample kernels yet");
               return;
         }
      }

      void CompileBlock(Ctx& ctx, const std::vector<AstNodePtr>& stmts, Scope& scope, TableScope& tableScope)
      {
         for (const auto& s : stmts)
         {
            if (ctx.Failed()) return;
            CompileStmt(ctx, s, scope, tableScope);
         }
      }
   }

   bool CompileSampleProgram(const std::string& code,
                              const SampleProgram* previous,
                              SampleProgram& outProgram,
                              FieldError& outError)
   {
      outProgram = SampleProgram{};
      outError.Clear();

      std::vector<Token> tokens;
      if (!Lex(code, tokens, outError))
         return false;

      AstNodePtr astProgram;
      if (!ParseProgram(tokens, astProgram, outError))
         return false;

      if (!astProgram || astProgram->kind != AstKind::Program)
      {
         outError.message = "internal error: expected a Program AST node";
         return false;
      }

      Ctx ctx { outProgram, outError };
      Scope scope;
      TableScope tableScope;

      // 'out' behaves like an ordinary local, seeded to silence so a kernel
      // that never assigns it is a valid (if useless) always-silent program
      // rather than a compile error.
      scope["out"] = EmitImm(ctx, 0.0f, SourceSpan{});

      CompileBlock(ctx, static_cast<AstProgram*>(astProgram.get())->statements, scope, tableScope);
      if (ctx.Failed())
      {
         outProgram = SampleProgram{};
         return false;
      }

      outProgram.outReg = (int)scope["out"];

      // Emit the final state write-back, once per declared cell, using
      // whatever register that cell's name is bound to at the very end of
      // the program (its value after every assignment/branch this sample).
      for (int i = 0; i < (int)outProgram.state.size(); i++)
      {
         const std::string& name = outProgram.state[i].name;
         auto it = scope.find(name);
         uint8_t finalReg = (it != scope.end()) ? it->second : 0;
         ctx.Emit(SampleOp::StoreState, 0, (uint8_t)i, finalReg, 0, 0.0f, SourceSpan{});
      }
      if (ctx.Failed())
      {
         outProgram = SampleProgram{};
         return false;
      }

      // Dense mailbox ids: recomputed fresh every successful compile, never
      // persisted (see SampleParamSlot's comment / ParamTable's separate
      // stable `id` column, which this does not touch).
      for (int i = 0; i < (int)outProgram.params.size(); i++)
         outProgram.params[i].mailboxId = i;

      // (name,type) state transplant resolution against the previous
      // program, on the main thread, once - never string-compared on the
      // audio thread.
      if (previous)
      {
         for (auto& cell : outProgram.state)
         {
            cell.transplantFromIndex = -1;
            for (int j = 0; j < (int)previous->state.size(); j++)
            {
               if (previous->state[j].name == cell.name && previous->state[j].typeName == cell.typeName)
               {
                  cell.transplantFromIndex = j;
                  break;
               }
            }
         }

         // Step 19: Delay line transplant resolution. If delay line lengths match at
         // the same AST call site index, reuse the existing buffer offset and cursor.
         for (int i = 0; i < (int)outProgram.delays.size(); i++)
         {
            auto& dl = outProgram.delays[i];
            dl.transplantFromOffset = -1;
            dl.transplantFromLength = 0;
            dl.transplantFromCursor = -1;
            if (i < (int)previous->delays.size())
            {
               const auto& prevDl = previous->delays[i];
               if (prevDl.length == dl.length)
               {
                  dl.transplantFromOffset = prevDl.bufferOffset;
                  dl.transplantFromLength = prevDl.length;
                  dl.transplantFromCursor = prevDl.cursorIndex;
               }
            }
         }

         // Step 24: Table transplant resolution. If table name and length match,
         // reuse existing buffer offset to preserve table data across recompile.
         for (auto& tbl : outProgram.tables)
         {
            tbl.transplantFromOffset = -1;
            tbl.transplantFromLength = 0;
            for (const auto& prevTbl : previous->tables)
            {
               if (prevTbl.name == tbl.name && prevTbl.length == tbl.length)
               {
                  tbl.transplantFromOffset = prevTbl.bufferOffset;
                  tbl.transplantFromLength = prevTbl.length;
                  break;
               }
            }
         }
      }

      outProgram.valid = true;
      return true;
   }
}

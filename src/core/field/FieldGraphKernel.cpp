#include "FieldGraphKernel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <unordered_map>

// Direct recursive tree-walking interpreter over the shared IRNode/IRStmt
// types (I7: an interpreter over the existing IR, not a fourth IR/bytecode
// dialect - graph domain runs once, top to bottom, at edit time, so there is
// no per-sample/per-element hot loop to justify a compiled bytecode form the
// way Element/Pixel/Sample have).
namespace Field
{
   namespace
   {
      static constexpr int kMaxGraphLoopIterations = 100000;

      struct LocalValue
      {
         bool isHandle = false;
         std::string handleKey;

         double lanes[4] = { 0.0, 0.0, 0.0, 0.0 };
         int laneCount = 1;
         bool isBool = false;

         static LocalValue Scalar(double v)
         {
            LocalValue r;
            r.lanes[0] = v;
            r.laneCount = 1;
            return r;
         }

         static LocalValue Bool(bool b)
         {
            LocalValue r;
            r.lanes[0] = b ? 1.0 : 0.0;
            r.laneCount = 1;
            r.isBool = true;
            return r;
         }

         double Scalar() const { return lanes[0]; }
         bool AsBool() const { return lanes[0] != 0.0; }
      };

      std::string FormatKeyToken(const LocalValue& v)
      {
         if (v.isBool)
            return v.AsBool() ? "true" : "false";

         char buf[256];
         if (v.laneCount == 1)
         {
            std::snprintf(buf, sizeof(buf), "%.6g", v.lanes[0]);
            return buf;
         }
         std::string out;
         for (int i = 0; i < v.laneCount; ++i)
         {
            if (i > 0) out += "_";
            std::snprintf(buf, sizeof(buf), "%.6g", v.lanes[i]);
            out += buf;
         }
         return out;
      }

      class GraphInterpreter
      {
      public:
         GraphInterpreter(const std::map<std::string, float>& params,
                           const std::map<std::string, float>& globals,
                           GraphPlan& plan,
                           FieldError& error)
            : mParams(params), mGlobals(globals), mPlan(plan), mError(error) {}

         bool Run(const GraphIRProgram& program)
         {
            for (const auto& s : program.statements)
            {
               if (!ExecStmt(s)) return false;
            }
            return CheckDuplicateKeys();
         }

      private:
         const std::map<std::string, float>& mParams;
         const std::map<std::string, float>& mGlobals;
         GraphPlan& mPlan;
         FieldError& mError;
         std::unordered_map<std::string, LocalValue> mEnv;

         bool Fail(const SourceSpan& span, const std::string& msg)
         {
            mError.severity = Severity::Error;
            mError.span = span;
            mError.message = msg;
            return false;
         }

         bool CheckDuplicateKeys()
         {
            std::set<std::string> seen;
            for (const auto& e : mPlan.emits)
            {
               if (!seen.insert(e.key).second)
               {
                  return Fail(e.span, "duplicate emit() identity key '" + e.key +
                                      "' - two emit() calls produced the same key; give them distinct k0/k1 arguments");
               }
            }
            return true;
         }

         bool ResolveVariable(const IRNodePtr& node, LocalValue& out)
         {
            auto envIt = mEnv.find(node->varName);
            if (envIt != mEnv.end())
            {
               if (node->isHandle && !envIt->second.isHandle)
                  return Fail(node->span, "'" + node->varName + "' is not a handle");
               out = envIt->second;
               return true;
            }
            if (node->isHandle)
            {
               return Fail(node->span, "handle '" + node->varName + "' was never bound by emit() on this pass "
                                        "(it was assigned inside a branch or loop iteration that didn't run)");
            }
            auto pIt = mParams.find(node->varName);
            if (pIt != mParams.end())
            {
               out = LocalValue::Scalar(pIt->second);
               return true;
            }
            auto gIt = mGlobals.find(node->varName);
            if (gIt != mGlobals.end())
            {
               out = LocalValue::Scalar(gIt->second);
               return true;
            }
            return Fail(node->span, "unknown identifier '" + node->varName + "'");
         }

         bool Eval(const IRNodePtr& node, LocalValue& out)
         {
            if (!node) return Fail({}, "internal error: null expression node");

            switch (node->kind)
            {
               case IRKind::Literal:
               {
                  out.isBool = (node->type.kind == DataType::Bool);
                  out.laneCount = node->type.lanes > 0 ? node->type.lanes : 1;
                  for (int i = 0; i < out.laneCount; ++i)
                     out.lanes[i] = node->vecValues[i];
                  return true;
               }

               case IRKind::Variable:
                  return ResolveVariable(node, out);

               case IRKind::StateRead:
                  return Fail(node->span, "internal error: state read in graph domain (should have been rejected at lowering)");

               case IRKind::Access:
               {
                  LocalValue base;
                  if (!Eval(node->children[0], base)) return false;
                  out.laneCount = node->type.lanes > 0 ? node->type.lanes : 1;
                  out.isBool = (node->type.kind == DataType::Bool);
                  for (int i = 0; i < out.laneCount; ++i)
                     out.lanes[i] = base.lanes[node->swizzleIndices[i]];
                  return true;
               }

               case IRKind::Unary:
               {
                  LocalValue operand;
                  if (!Eval(node->children[0], operand)) return false;
                  if (node->op == "!")
                  {
                     out = LocalValue::Bool(!operand.AsBool());
                     return true;
                  }
                  out.laneCount = operand.laneCount;
                  for (int i = 0; i < out.laneCount; ++i)
                     out.lanes[i] = -operand.lanes[i];
                  return true;
               }

               case IRKind::Binary:
                  return EvalBinary(node, out);

               case IRKind::Call:
                  return EvalCall(node, out);

               default:
                  return Fail(node->span, "unsupported expression in graph kernel");
            }
         }

         bool EvalBinary(const IRNodePtr& node, LocalValue& out)
         {
            LocalValue lhs, rhs;
            if (!Eval(node->children[0], lhs)) return false;
            if (!Eval(node->children[1], rhs)) return false;

            const std::string& op = node->op;
            if (op == "&&") { out = LocalValue::Bool(lhs.AsBool() && rhs.AsBool()); return true; }
            if (op == "||") { out = LocalValue::Bool(lhs.AsBool() || rhs.AsBool()); return true; }

            if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=")
            {
               double a = lhs.Scalar(), b = rhs.Scalar();
               bool r = false;
               if (op == "<") r = a < b;
               else if (op == "<=") r = a <= b;
               else if (op == ">") r = a > b;
               else if (op == ">=") r = a >= b;
               else if (op == "==") r = a == b;
               else r = a != b;
               out = LocalValue::Bool(r);
               return true;
            }

            out.laneCount = std::max(lhs.laneCount, rhs.laneCount);
            for (int i = 0; i < out.laneCount; ++i)
            {
               double a = lhs.lanes[lhs.laneCount == 1 ? 0 : i];
               double b = rhs.lanes[rhs.laneCount == 1 ? 0 : i];
               double r = 0.0;
               if (op == "+") r = a + b;
               else if (op == "-") r = a - b;
               else if (op == "*") r = a * b;
               else if (op == "/") r = (b != 0.0) ? (a / b) : 0.0;
               else if (op == "%") r = (b != 0.0) ? std::fmod(a, b) : 0.0;
               else if (op == "^") r = std::pow(a, b);
               else return Fail(node->span, "unsupported binary operator '" + op + "' in graph kernel");
               out.lanes[i] = r;
            }
            return true;
         }

         bool EvalCall(const IRNodePtr& node, LocalValue& out)
         {
            const std::string& fn = node->callee;

            if (fn == "vec2" || fn == "vec3" || fn == "vec4")
            {
               int targetLanes = fn == "vec2" ? 2 : (fn == "vec3" ? 3 : 4);
               std::vector<LocalValue> args;
               for (const auto& c : node->children)
               {
                  LocalValue v;
                  if (!Eval(c, v)) return false;
                  args.push_back(v);
               }
               out.laneCount = targetLanes;
               if (args.size() == 1 && args[0].laneCount == 1)
               {
                  for (int i = 0; i < targetLanes; ++i) out.lanes[i] = args[0].lanes[0];
               }
               else
               {
                  int idx = 0;
                  for (const auto& a : args)
                     for (int i = 0; i < a.laneCount && idx < targetLanes; ++i)
                        out.lanes[idx++] = a.lanes[i];
               }
               return true;
            }

            std::vector<LocalValue> args;
            for (const auto& c : node->children)
            {
               LocalValue v;
               if (!Eval(c, v)) return false;
               args.push_back(v);
            }

            auto unary = [&](double (*f)(double)) {
               out.laneCount = args[0].laneCount;
               for (int i = 0; i < out.laneCount; ++i) out.lanes[i] = f(args[0].lanes[i]);
               return true;
            };

            if (fn == "sin") return unary([](double x){ return std::sin(x); });
            if (fn == "cos") return unary([](double x){ return std::cos(x); });
            if (fn == "tan") return unary([](double x){ return std::tan(x); });
            if (fn == "abs") return unary([](double x){ return std::fabs(x); });
            if (fn == "floor") return unary([](double x){ return std::floor(x); });
            if (fn == "ceil") return unary([](double x){ return std::ceil(x); });
            if (fn == "round") return unary([](double x){ return std::round(x); });
            if (fn == "sign") return unary([](double x){ return (double)((x > 0.0) - (x < 0.0)); });
            if (fn == "exp") return unary([](double x){ return std::exp(x); });
            if (fn == "sqrt") return unary([](double x){ return x > 0.0 ? std::sqrt(x) : 0.0; });
            if (fn == "log") return unary([](double x){ return x > 0.0 ? std::log(x) : 0.0; });
            if (fn == "fract") return unary([](double x){ return x - std::floor(x); });

            if (fn == "length")
            {
               double sum = 0.0;
               for (int i = 0; i < args[0].laneCount; ++i) sum += args[0].lanes[i] * args[0].lanes[i];
               out = LocalValue::Scalar(std::sqrt(sum));
               return true;
            }
            if (fn == "normalize")
            {
               double sum = 0.0;
               for (int i = 0; i < args[0].laneCount; ++i) sum += args[0].lanes[i] * args[0].lanes[i];
               double len = std::sqrt(sum);
               out.laneCount = args[0].laneCount;
               for (int i = 0; i < out.laneCount; ++i) out.lanes[i] = len > 0.0 ? args[0].lanes[i] / len : 0.0;
               return true;
            }

            if (fn == "min" || fn == "max" || fn == "mod" || fn == "fmod" || fn == "pow" || fn == "step")
            {
               out.laneCount = std::max(args[0].laneCount, args[1].laneCount);
               for (int i = 0; i < out.laneCount; ++i)
               {
                  double a = args[0].lanes[args[0].laneCount == 1 ? 0 : i];
                  double b = args[1].lanes[args[1].laneCount == 1 ? 0 : i];
                  double r = 0.0;
                  if (fn == "min") r = std::min(a, b);
                  else if (fn == "max") r = std::max(a, b);
                  else if (fn == "mod" || fn == "fmod") r = (b != 0.0) ? std::fmod(a, b) : 0.0;
                  else if (fn == "pow") r = std::pow(a, b);
                  else r = (a >= b) ? 1.0 : 0.0; // step(edge, x)
                  out.lanes[i] = r;
               }
               return true;
            }
            if (fn == "distance")
            {
               double sum = 0.0;
               int lanes = std::max(args[0].laneCount, args[1].laneCount);
               for (int i = 0; i < lanes; ++i)
               {
                  double d = args[0].lanes[i] - args[1].lanes[i];
                  sum += d * d;
               }
               out = LocalValue::Scalar(std::sqrt(sum));
               return true;
            }
            if (fn == "dot")
            {
               double sum = 0.0;
               for (int i = 0; i < args[0].laneCount; ++i) sum += args[0].lanes[i] * args[1].lanes[i];
               out = LocalValue::Scalar(sum);
               return true;
            }
            if (fn == "cross")
            {
               out.laneCount = 3;
               out.lanes[0] = args[0].lanes[1] * args[1].lanes[2] - args[0].lanes[2] * args[1].lanes[1];
               out.lanes[1] = args[0].lanes[2] * args[1].lanes[0] - args[0].lanes[0] * args[1].lanes[2];
               out.lanes[2] = args[0].lanes[0] * args[1].lanes[1] - args[0].lanes[1] * args[1].lanes[0];
               return true;
            }
            if (fn == "atan2")
            {
               out = LocalValue::Scalar(std::atan2(args[0].lanes[0], args[1].lanes[0]));
               return true;
            }

            if (fn == "clamp")
            {
               out.laneCount = args[0].laneCount;
               for (int i = 0; i < out.laneCount; ++i)
               {
                  double lo = args[1].lanes[args[1].laneCount == 1 ? 0 : i];
                  double hi = args[2].lanes[args[2].laneCount == 1 ? 0 : i];
                  out.lanes[i] = std::min(std::max(args[0].lanes[i], lo), hi);
               }
               return true;
            }
            if (fn == "lerp" || fn == "mix")
            {
               out.laneCount = std::max(args[0].laneCount, args[1].laneCount);
               double t = args[2].lanes[0];
               for (int i = 0; i < out.laneCount; ++i)
               {
                  double a = args[0].lanes[args[0].laneCount == 1 ? 0 : i];
                  double b = args[1].lanes[args[1].laneCount == 1 ? 0 : i];
                  out.lanes[i] = a + (b - a) * t;
               }
               return true;
            }
            if (fn == "smoothstep")
            {
               double lo = args[0].lanes[0], hi = args[1].lanes[0], x = args[2].lanes[0];
               double t = (hi != lo) ? std::min(std::max((x - lo) / (hi - lo), 0.0), 1.0) : 0.0;
               out = LocalValue::Scalar(t * t * (3.0 - 2.0 * t));
               return true;
            }
            if (fn == "if")
            {
               out = args[0].AsBool() ? args[1] : args[2];
               return true;
            }

            return Fail(node->span, "'" + fn + "()' is not available in the graph domain");
         }

         bool ExecStmt(const IRStmtPtr& s)
         {
            if (!s) return true;

            switch (s->kind)
            {
               case IRStmtKind::Assign: return ExecAssign(s);
               case IRStmtKind::Emit: return ExecEmit(s);
               case IRStmtKind::Connect: return ExecConnect(s);
               case IRStmtKind::SetParam: return ExecSetParam(s);
               case IRStmtKind::Place: return ExecPlace(s);
               case IRStmtKind::If: return ExecIf(s);
               case IRStmtKind::For: return ExecFor(s);
               case IRStmtKind::Expr:
               {
                  LocalValue v;
                  return Eval(s->expr, v);
               }
               default:
                  return Fail(s->span, "unsupported statement in graph kernel interpreter");
            }
         }

         bool ExecAssign(const IRStmtPtr& s)
         {
            LocalValue rhs;
            if (!Eval(s->rvalueExpr, rhs)) return false;

            if (s->assignOp == "=")
            {
               mEnv[s->assignTarget] = rhs;
               return true;
            }

            auto it = mEnv.find(s->assignTarget);
            if (it == mEnv.end())
               return Fail(s->span, "use of undeclared identifier '" + s->assignTarget + "'");

            LocalValue& cur = it->second;
            int lanes = std::max(cur.laneCount, rhs.laneCount);
            for (int i = 0; i < lanes; ++i)
            {
               double a = cur.lanes[cur.laneCount == 1 ? 0 : i];
               double b = rhs.lanes[rhs.laneCount == 1 ? 0 : i];
               double r = a;
               if (s->assignOp == "+=") r = a + b;
               else if (s->assignOp == "-=") r = a - b;
               else if (s->assignOp == "*=") r = a * b;
               else if (s->assignOp == "/=") r = (b != 0.0) ? (a / b) : 0.0;
               cur.lanes[i] = r;
            }
            cur.laneCount = lanes;
            return true;
         }

         bool ExecEmit(const IRStmtPtr& s)
         {
            std::string key = s->emitTargetName + "#";
            for (size_t i = 0; i < s->emitKeyArgs.size(); ++i)
            {
               LocalValue v;
               if (!Eval(s->emitKeyArgs[i], v)) return false;
               if (i > 0) key += ".";
               key += FormatKeyToken(v);
            }

            EmitSpec spec;
            spec.key = key;
            spec.typeName = s->emitTypeName;
            spec.span = s->span;
            mPlan.emits.push_back(spec);

            LocalValue handle;
            handle.isHandle = true;
            handle.handleKey = key;
            mEnv[s->emitTargetName] = handle;
            return true;
         }

         bool ResolveHandleArg(const IRNodePtr& node, std::string& outKey)
         {
            LocalValue v;
            if (!Eval(node, v)) return false;
            if (!v.isHandle)
               return Fail(node->span, "expected a handle returned by emit()");
            outKey = v.handleKey;
            return true;
         }

         bool ExecConnect(const IRStmtPtr& s)
         {
            std::string srcKey, dstKey;
            if (!ResolveHandleArg(s->connectSrc, srcKey)) return false;
            if (!ResolveHandleArg(s->connectDst, dstKey)) return false;
            LocalValue srcSlot, dstSlot;
            if (!Eval(s->connectSrcSlot, srcSlot)) return false;
            if (!Eval(s->connectDstSlot, dstSlot)) return false;

            ConnectSpec spec;
            spec.srcKey = srcKey;
            spec.srcSlot = (int)std::lround(srcSlot.Scalar());
            spec.dstKey = dstKey;
            spec.dstSlot = (int)std::lround(dstSlot.Scalar());
            spec.span = s->span;
            mPlan.connects.push_back(spec);
            return true;
         }

         bool ExecSetParam(const IRStmtPtr& s)
         {
            std::string targetKey;
            if (!ResolveHandleArg(s->setTarget, targetKey)) return false;
            LocalValue val;
            if (!Eval(s->setValue, val)) return false;

            SetSpec spec;
            spec.targetKey = targetKey;
            spec.paramName = s->setParamName;
            spec.value = (float)val.Scalar();
            spec.span = s->span;
            mPlan.sets.push_back(spec);
            return true;
         }

         bool ExecPlace(const IRStmtPtr& s)
         {
            std::string targetKey;
            if (!ResolveHandleArg(s->placeTarget, targetKey)) return false;
            LocalValue x, y;
            if (!Eval(s->placeX, x)) return false;
            if (!Eval(s->placeY, y)) return false;

            PlaceSpec spec;
            spec.targetKey = targetKey;
            spec.x = (float)x.Scalar();
            spec.y = (float)y.Scalar();
            spec.span = s->span;
            mPlan.places.push_back(spec);
            return true;
         }

         bool ExecIf(const IRStmtPtr& s)
         {
            LocalValue cond;
            if (!Eval(s->ifCond, cond)) return false;
            const auto& branch = cond.AsBool() ? s->thenStmts : s->elseStmts;
            for (const auto& st : branch)
               if (!ExecStmt(st)) return false;
            return true;
         }

         bool ExecFor(const IRStmtPtr& s)
         {
            if (!ExecStmt(s->forInit)) return false;
            int iterations = 0;
            while (true)
            {
               if (s->forCond)
               {
                  LocalValue cond;
                  if (!Eval(s->forCond, cond)) return false;
                  if (!cond.AsBool()) break;
               }
               else if (iterations > 0)
               {
                  break; // no condition: run exactly once, matching a bare `for (init;;step)` body guard
               }

               for (const auto& st : s->forBody)
                  if (!ExecStmt(st)) return false;

               if (!ExecStmt(s->forStep)) return false;

               if (++iterations > kMaxGraphLoopIterations)
                  return Fail(s->span, "for loop exceeded " + std::to_string(kMaxGraphLoopIterations) +
                                       " iterations - check your loop bound (graph kernels must terminate deterministically)");
            }
            return true;
         }
      };
   }

   bool InterpretGraphProgram(const GraphIRProgram& program,
                               const std::map<std::string, float>& params,
                               const std::map<std::string, float>& globals,
                               GraphPlan& outPlan,
                               FieldError& outError)
   {
      outError.Clear();
      outPlan = GraphPlan{};

      GraphInterpreter interp(params, globals, outPlan, outError);
      if (!interp.Run(program))
      {
         outPlan = GraphPlan{};
         return false;
      }

      outPlan.valid = true;
      return true;
   }
}

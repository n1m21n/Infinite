#include "ElementGlslBackend.h"
#include "FieldRandom.h"
#include "../../audio/PhaseShape.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Field
{
   namespace
   {
      // The element VM's maths where GLSL's differs. Scalar first, then the
      // vec2..4 overloads component by component.
      const char* const kScalarHelpers =
         "float fld_ediv(float a, float b) { return b != 0.0 ? a / b : 0.0; }\n"
         "float fld_kdiv(float a, float b) { return b != 0.0 ? a / b : a; }\n"
         "float fld_emod(float a, float b) { return b != 0.0 ? a - b * trunc(a / b) : 0.0; }\n"
         "float fld_epow(float a, float b) {\n"
         "   if (b == 0.0) return 1.0;\n"
         "   if (a == 0.0) return b > 0.0 ? 0.0 : 3.0e38;\n"
         "   if (a < 0.0) {\n"
         "      if (b != floor(b)) return 0.0;\n"
         "      float r = pow(-a, b);\n"
         "      return mod(b, 2.0) == 1.0 ? -r : r;\n"
         "   }\n"
         "   return pow(a, b);\n"
         "}\n"
         "float fld_eatan2(float y, float x) { return (x == 0.0 && y == 0.0) ? 0.0 : atan(y, x); }\n"
         "float fld_esmooth(float e0, float e1, float x) {\n"
         "   if (e1 == e0) return x < e0 ? 0.0 : 1.0;\n"
         "   float k = clamp((x - e0) / (e1 - e0), 0.0, 1.0);\n"
         "   return k * k * (3.0 - 2.0 * k);\n"
         "}\n";

      std::string VecOverloads()
      {
         struct Fn { const char* name; int args; };
         const Fn fns[] = { { "fld_ediv", 2 }, { "fld_kdiv", 2 }, { "fld_emod", 2 }, { "fld_epow", 2 },
                            { "fld_eatan2", 2 }, { "fld_esmooth", 3 } };
         const char* comps = "xyzw";
         std::string out;
         for (int n = 2; n <= 4; ++n)
         {
            const std::string t = "vec" + std::to_string(n);
            for (const Fn& f : fns)
            {
               out += t + " " + f.name + "(";
               for (int a = 0; a < f.args; ++a)
                  out += std::string(a ? ", " : "") + t + " a" + std::to_string(a);
               out += ") { return " + t + "(";
               for (int c = 0; c < n; ++c)
               {
                  out += std::string(c ? ", " : "") + f.name + "(";
                  for (int a = 0; a < f.args; ++a)
                     out += std::string(a ? ", " : "") + "a" + std::to_string(a) + "." + comps[c];
                  out += ")";
               }
               out += "); }\n";
            }
            out += t + " fld_enorm(" + t + " a) { float l = length(a); return l > 1e-20 ? a / l : " + t + "(0.0); }\n";
         }
         out += "float fld_enorm(float a) { float l = abs(a); return l > 1e-20 ? a / l : 0.0; }\n";
         return out;
      }

      // FieldRandom's Xorwise on a 64-bit word held as (low, high), and the
      // seed half of TimeToRand; the time half arrives as `hTime`.
      const char* const kRandHelpers =
         "uvec2 fld_xw(uvec2 x) {\n"
         "   uvec2 a = uvec2(x.x << 13u, (x.y << 13u) | (x.x >> 19u)) ^ x;\n"
         "   uvec2 b = uvec2((a.x >> 17u) | (a.y << 15u), a.y >> 17u) ^ a;\n"
         "   return uvec2(b.x << 5u, (b.y << 5u) | (b.x >> 27u)) ^ b;\n"
         "}\n"
         "float fld_rand(float lo, float hi, uvec2 hTime, float seed) {\n"
         "   int s = (isnan(seed) || isinf(seed)) ? 0 : int(seed);\n"
         "   uvec2 sw = uvec2(uint(s), s < 0 ? 0xFFFFFFFFu : 0u);\n"
         "   uvec2 m = fld_xw(hTime ^ fld_xw(sw));\n"
         "   return lo + (hi - lo) * (float(m.x & 0x1FFFFFFFu) / 536870912.0);\n"
         "}\n";

      std::string TypeName(int lanes)
      {
         return lanes <= 1 ? "float" : "vec" + std::to_string(std::min(lanes, 4));
      }

      bool FormatFloat(double val, std::string& out)
      {
         if (!std::isfinite(val)) return false;
         std::ostringstream ss;
         ss << std::setprecision(9) << val;
         out = ss.str();
         if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
             out.find('E') == std::string::npos)
            out += ".0";
         return true;
      }

      struct Val
      {
         std::string expr;
         int lanes = 1;
      };

      struct Emitter
      {
         ElementGlslResult& res;
         std::ostringstream body;
         std::ostringstream decls;
         std::string indent = "   ";
         int temps = 0;
         int loops = 0;
         // Names that are locals at this point of the walk, in the order the
         // VM's emitter meets them (a name first assigned inside an `if` is a
         // local for every statement after it), and the GLSL variable each is.
         std::unordered_map<std::string, int> localLanes;
         std::unordered_map<std::string, std::string> localVar;
         std::unordered_map<std::string, int> uniformIndex;
         // Prologue (frame) assignments and their widths. The VM keeps each in
         // a register the element loop can reassign; here it is a uniform
         // until the loop first assigns it, then a local that starts from it.
         std::unordered_map<std::string, int> prologueLanes;
         std::string sizeVar;

         explicit Emitter(ElementGlslResult& r) : res(r) {}

         bool Fail(const std::string& why)
         {
            if (res.error.empty()) res.error = why;
            return false;
         }
         bool Ok() const { return res.error.empty(); }

         void Line(const std::string& s) { body << indent << s << "\n"; }

         Val Temp(const std::string& expr, int lanes)
         {
            std::string name = "fld_t" + std::to_string(temps++);
            Line(TypeName(lanes) + " " + name + " = " + expr + ";");
            return { name, lanes };
         }

         std::string DeclareLocal(const std::string& name, int lanes, const std::string& init)
         {
            std::string var = "fld_l" + std::to_string(localVar.size());
            decls << "   " << TypeName(lanes) << " " << var << " = " << init << ";\n";
            localVar[name] = var;
            localLanes[name] = lanes;
            return var;
         }

         // A value at another width the way the VM reads it: a scalar
         // broadcasts, a wider value gives its first lanes, a narrower vector
         // pads with 0.
         static std::string Fit(const Val& v, int to)
         {
            if (v.lanes == to) return v.expr;
            if (v.lanes == 1) return TypeName(to) + "(" + v.expr + ")";
            if (v.lanes > to)
               return v.expr + "." + std::string("xyzw").substr(0, (size_t)to);
            std::string s = TypeName(to) + "(" + v.expr;
            for (int l = v.lanes; l < to; ++l) s += ", 0.0";
            return s + ")";
         }
         static std::string Lane0(const Val& v) { return v.lanes == 1 ? v.expr : v.expr + ".x"; }
         static std::string Comp(const Val& v, int k) { return v.lanes == 1 ? v.expr : v.expr + "." + "xyzw"[std::min(k, 3)]; }

         Val Uniform(const std::string& name, int lanes)
         {
            lanes = std::max(1, std::min(4, lanes));
            auto it = uniformIndex.find(name);
            if (it == uniformIndex.end())
            {
               ElementGlslUniform u;
               u.glslName = "fld_u" + std::to_string(res.uniforms.size());
               u.varName = name;
               u.lanes = lanes;
               uniformIndex[name] = (int)res.uniforms.size();
               res.uniforms.push_back(u);
               return { u.glslName, lanes };
            }
            const ElementGlslUniform& u = res.uniforms[(size_t)it->second];
            if (u.lanes == lanes) return { u.glslName, lanes };
            return Temp(Fit({ u.glslName, u.lanes }, lanes), lanes);
         }

         // A name whose value is the same for every element at this point.
         bool IsFrameName(const std::string& name) const
         {
            return name != "P" && name != "N" && name != "uv" && name != "Cd" && name != "i" &&
                   localLanes.find(name) == localLanes.end();
         }

         Val Expr(const IRNodePtr& n)
         {
            if (!Ok()) return { "0.0", 1 };
            if (!n) return { "0.0", 1 };
            if (n->isNeighbourRead)
            {
               Fail("neighbour read (.at)");
               return { "0.0", 1 };
            }
            const int lanes = std::max(1, std::min(4, n->type.lanes));

            switch (n->kind)
            {
               case IRKind::Literal:
               {
                  std::string comps[4];
                  for (int l = 0; l < lanes; ++l)
                  {
                     if (!FormatFloat(n->vecValues[l], comps[l]))
                     {
                        Fail("non-finite literal");
                        return { "0.0", 1 };
                     }
                  }
                  if (lanes == 1) return { comps[0], 1 };
                  std::string s = TypeName(lanes) + "(";
                  for (int l = 0; l < lanes; ++l) s += (l ? ", " : "") + comps[l];
                  return Temp(s + ")", lanes);
               }

               case IRKind::StateRead:
                  Fail("state cell");
                  return { "0.0", 1 };

               case IRKind::ImageRead:
                  Fail("image read");
                  return { "0.0", 1 };

               case IRKind::Variable:
               {
                  const std::string& name = n->varName;
                  if (name == "P") return Temp("P", 3);
                  if (name == "Cd") return Temp("Cd", 3);
                  if (name == "uv") return Temp("vec2(0.0)", 2);
                  if (name == "N")
                  {
                     Fail("N (GPU points carry no normal)");
                     return { "0.0", 1 };
                  }
                  if (name == "i") return Temp("float(index)", 1);
                  auto it = localVar.find(name);
                  if (it != localVar.end()) return { it->second, localLanes[name] };
                  return Uniform(name, lanes);
               }

               case IRKind::Access:
               {
                  if (n->children.empty()) return { "0.0", 1 };
                  Val base = Expr(n->children[0]);
                  if (!Ok()) return { "0.0", 1 };
                  std::string comps[4];
                  for (int l = 0; l < lanes; ++l)
                  {
                     const int idx = n->swizzleIndices[l];
                     if (base.lanes == 1) comps[l] = base.expr;
                     else if (idx < base.lanes) comps[l] = base.expr + "." + "xyzw"[idx];
                     else comps[l] = "0.0";
                  }
                  if (lanes == 1) return Temp(comps[0], 1);
                  std::string s = TypeName(lanes) + "(";
                  for (int l = 0; l < lanes; ++l) s += (l ? ", " : "") + comps[l];
                  return Temp(s + ")", lanes);
               }

               case IRKind::Unary:
               {
                  Val a = Expr(n->children.empty() ? nullptr : n->children[0]);
                  if (n->op == "!") return Temp("(" + Lane0(a) + " == 0.0 ? 1.0 : 0.0)", 1);
                  return Temp("-" + Fit(a, lanes), lanes);
               }

               case IRKind::Binary:
               {
                  if (n->children.size() < 2) return { "0.0", 1 };
                  Val a = Expr(n->children[0]);
                  Val b = Expr(n->children[1]);
                  if (!Ok()) return { "0.0", 1 };
                  const std::string& op = n->op;
                  if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=")
                     return Temp("(" + Lane0(a) + " " + op + " " + Lane0(b) + " ? 1.0 : 0.0)", 1);
                  if (op == "&&" || op == "||")
                     return Temp("((" + Lane0(a) + " != 0.0 " + op + " " + Lane0(b) + " != 0.0) ? 1.0 : 0.0)", 1);
                  const std::string fa = Fit(a, lanes), fb = Fit(b, lanes);
                  if (op == "+" || op == "-" || op == "*") return Temp(fa + " " + op + " " + fb, lanes);
                  if (op == "/") return Temp("fld_ediv(" + fa + ", " + fb + ")", lanes);
                  if (op == "%") return Temp("fld_emod(" + fa + ", " + fb + ")", lanes);
                  if (op == "^") return Temp("fld_epow(" + fa + ", " + fb + ")", lanes);
                  Fail("operator " + op);
                  return { "0.0", 1 };
               }

               case IRKind::Call:
                  return Call(n, lanes);
            }
            return { "0.0", 1 };
         }

         Val Call(const IRNodePtr& n, int lanes)
         {
            if (n->transferKind == TransferKind::Reduce)
            {
               Fail("reduce");
               return { "0.0", 1 };
            }
            if (n->transferKind == TransferKind::Resample || n->transferKind == TransferKind::Downsample)
               return n->children.empty() ? Val{ "0.0", 1 } : Expr(n->children[0]);

            const std::string& fn = n->callee;

            if (fn == "rand" || fn == "noise" || fn == "sh")
               return Rand(n, lanes, fn == "sh");

            std::vector<Val> args;
            for (const auto& c : n->children)
               args.push_back(Expr(c));
            if (!Ok()) return { "0.0", 1 };
            auto need = [&](size_t k) -> bool
            {
               if (args.size() >= k) return true;
               return Fail(fn + " with too few arguments");
            };
            auto lane = [&](size_t k) { return Fit(args[k], lanes); };

            if (fn == "vec2" || fn == "vec3" || fn == "vec4")
            {
               if (args.size() == 1 && args[0].lanes == 1)
                  return Temp(TypeName(lanes) + "(" + args[0].expr + ")", lanes);
               std::vector<std::string> comps;
               for (const Val& a : args)
                  for (int l = 0; l < a.lanes && (int)comps.size() < lanes; ++l)
                     comps.push_back(Comp(a, l));
               while ((int)comps.size() < lanes) comps.push_back("0.0");
               std::string s = TypeName(lanes) + "(";
               for (size_t k = 0; k < comps.size(); ++k) s += (k ? ", " : "") + comps[k];
               return Temp(s + ")", lanes);
            }

            if (fn == "if")
            {
               if (!need(3)) return { "0.0", 1 };
               return Temp("(" + Lane0(args[0]) + " != 0.0 ? " + lane(1) + " : " + lane(2) + ")", lanes);
            }

            if (fn == "length")
            {
               if (!need(1)) return { "0.0", 1 };
               return Temp("length(" + args[0].expr + ")", 1);
            }
            if (fn == "normalize")
            {
               if (!need(1)) return { "0.0", 1 };
               if (args[0].lanes != lanes) { Fail("normalize to another width"); return { "0.0", 1 }; }
               return Temp("fld_enorm(" + args[0].expr + ")", lanes);
            }
            if (fn == "distance" || fn == "dot" || fn == "cross")
            {
               if (!need(2)) return { "0.0", 1 };
               if (args[0].lanes != args[1].lanes) { Fail(fn + " of two widths"); return { "0.0", 1 }; }
               if (fn == "cross")
               {
                  if (args[0].lanes != 3) { Fail("cross of non-vec3"); return { "0.0", 1 }; }
                  return Temp("cross(" + args[0].expr + ", " + args[1].expr + ")", 3);
               }
               return Temp(fn + "(" + args[0].expr + ", " + args[1].expr + ")", 1);
            }

            static const std::unordered_map<std::string, std::string> kSame = {
               { "sin", "sin" }, { "cos", "cos" }, { "tan", "tan" }, { "abs", "abs" },
               { "floor", "floor" }, { "ceil", "ceil" }, { "exp", "exp" }, { "sign", "sign" },
               { "fract", "fract" },
            };
            auto same = kSame.find(fn);
            if (same != kSame.end())
            {
               if (!need(1)) return { "0.0", 1 };
               return Temp(same->second + "(" + lane(0) + ")", lanes);
            }
            const std::string zero = TypeName(lanes) + "(0.0)";
            if (fn == "round") { if (!need(1)) return { "0.0", 1 }; return Temp("floor(" + lane(0) + " + 0.5)", lanes); }
            if (fn == "sqrt") { if (!need(1)) return { "0.0", 1 }; return Temp("sqrt(max(" + lane(0) + ", " + zero + "))", lanes); }
            if (fn == "log") { if (!need(1)) return { "0.0", 1 }; return Temp("log(max(" + lane(0) + ", " + TypeName(lanes) + "(1e-15)))", lanes); }
            if (fn == "min" || fn == "max")
            {
               if (!need(2)) return { "0.0", 1 };
               return Temp(fn + "(" + lane(0) + ", " + lane(1) + ")", lanes);
            }
            if (fn == "pow") { if (!need(2)) return { "0.0", 1 }; return Temp("fld_epow(" + lane(0) + ", " + lane(1) + ")", lanes); }
            if (fn == "mod" || fn == "fmod") { if (!need(2)) return { "0.0", 1 }; return Temp("fld_emod(" + lane(0) + ", " + lane(1) + ")", lanes); }
            if (fn == "step") { if (!need(2)) return { "0.0", 1 }; return Temp("step(" + lane(0) + ", " + lane(1) + ")", lanes); }
            if (fn == "atan2") { if (!need(2)) return { "0.0", 1 }; return Temp("fld_eatan2(" + lane(0) + ", " + lane(1) + ")", lanes); }
            if (fn == "clamp")
            {
               if (!need(3)) return { "0.0", 1 };
               return Temp("max(" + lane(1) + ", min(" + lane(2) + ", " + lane(0) + "))", lanes);
            }
            if (fn == "lerp" || fn == "mix")
            {
               if (!need(3)) return { "0.0", 1 };
               Val a = Temp(lane(0), lanes);
               return Temp(a.expr + " + " + lane(2) + " * (" + lane(1) + " - " + a.expr + ")", lanes);
            }
            if (fn == "smoothstep")
            {
               if (!need(3)) return { "0.0", 1 };
               return Temp("fld_esmooth(" + lane(0) + ", " + lane(1) + ", " + lane(2) + ")", lanes);
            }
            PhaseShape::Kind shape;
            if (PhaseShape::FieldBuiltin(fn.c_str(), shape))
            {
               if (!need(2)) return { "0.0", 1 };
               return Temp("fld_" + fn + "(" + lane(0) + ", " + lane(1) + ")", lanes);
            }
            Fail("builtin " + fn);
            return { "0.0", 1 };
         }

         // rand(speed) / rand(min, max[, speed[, seed]]), as EvalElementBuiltin
         // decodes them. The speed has to be known before the pass runs: the
         // CPU folds it with t into the call site's time word.
         Val Rand(const IRNodePtr& n, int lanes, bool isSh)
         {
            const size_t nArgs = n->children.size();
            IRNodePtr speedNode;
            std::string lo = "0.0", hi = "1.0", seed = "0.0";
            if (nArgs == 1) speedNode = n->children[0];
            else if (nArgs >= 2)
            {
               lo = Lane0(Expr(n->children[0]));
               hi = Lane0(Expr(n->children[1]));
               if (nArgs >= 3) speedNode = n->children[2];
               if (nArgs >= 4) seed = Lane0(Expr(n->children[3]));
            }
            if (!Ok()) return { "0.0", 1 };

            ElementGlslRand r;
            r.glslName = "fld_r" + std::to_string(res.rands.size());
            r.isSh = isSh;
            if (speedNode)
            {
               if (speedNode->kind == IRKind::Literal)
                  r.speedLiteral = speedNode->vecValues[0];
               else if (speedNode->kind == IRKind::Variable && !speedNode->isNeighbourRead &&
                        IsFrameName(speedNode->varName))
               {
                  r.speedIsLiteral = false;
                  r.speedName = speedNode->varName;
               }
               else
               {
                  Fail("rand speed that is not a literal or a frame value");
                  return { "0.0", 1 };
               }
            }
            res.rands.push_back(r);
            Val v = Temp("fld_rand(" + lo + ", " + hi + ", " + r.glslName + ", " + seed + ")", 1);
            return lanes == 1 ? v : Temp(Fit(v, lanes), lanes);
         }

         // P or Cd: the VM's ApplyCompound per lane, `/=` by 0 keeping it.
         void StoreLane(const std::string& lhs, const std::string& op, const std::string& v)
         {
            if (op == "=") Line(lhs + " = " + v + ";");
            else if (op == "+=" || op == "-=" || op == "*=") Line(lhs + " " + op + " " + v + ";");
            else if (op == "/=") Line(lhs + " = fld_kdiv(" + lhs + ", " + v + ");");
            else Line(lhs + " = " + v + ";");
         }

         // A local: the VM's register arithmetic, where `/=` by 0 gives 0.
         std::string LocalOp(const std::string& lhs, const std::string& op, const std::string& v)
         {
            if (op == "+=") return lhs + " + " + v;
            if (op == "-=") return lhs + " - " + v;
            if (op == "*=") return lhs + " * " + v;
            return "fld_ediv(" + lhs + ", " + v + ")";
         }

         void Stmt(const IRStmtPtr& s)
         {
            if (!Ok() || !s) return;
            switch (s->kind)
            {
               case IRStmtKind::DeclAttrib:
               case IRStmtKind::DeclOutput:
               case IRStmtKind::DeclInput:
               case IRStmtKind::Expr:
                  return;
               case IRStmtKind::DeclState:
               case IRStmtKind::StateWrite:
                  Fail("state cell");
                  return;
               case IRStmtKind::Emit:
               case IRStmtKind::Connect:
               case IRStmtKind::SetParam:
               case IRStmtKind::Place:
                  Fail("graph statement");
                  return;
               case IRStmtKind::Assign:
                  Assign(s);
                  return;
               case IRStmtKind::If:
               {
                  Val c = Expr(s->ifCond);
                  if (!Ok()) return;
                  Line("if (" + Lane0(c) + " != 0.0) {");
                  const std::string outer = indent;
                  indent += "   ";
                  for (const auto& t : s->thenStmts) Stmt(t);
                  indent = outer;
                  if (!s->elseStmts.empty())
                  {
                     Line("} else {");
                     indent += "   ";
                     for (const auto& t : s->elseStmts) Stmt(t);
                     indent = outer;
                  }
                  Line("}");
                  return;
               }
               case IRStmtKind::For:
               {
                  // The VM's loop-entry binding (ElementBackend's For): every
                  // name the loop assigns that is no local yet starts from its
                  // by-name value, so the body updates one variable each trip.
                  std::vector<std::pair<std::string, int>> targets;
                  CollectLoopTargets(s, targets);
                  for (const auto& tg : targets)
                  {
                     if (localVar.count(tg.first)) continue;
                     Uniform(tg.first, tg.second);
                     const ElementGlslUniform& u = res.uniforms[(size_t)uniformIndex[tg.first]];
                     DeclareLocal(tg.first, tg.second, Fit({ u.glslName, u.lanes }, tg.second));
                  }
                  if (s->forInit) Stmt(s->forInit);
                  const std::string counter = "fld_n" + std::to_string(loops++);
                  Line("for (int " + counter + " = 0; " + counter + " < " + std::to_string(kMaxLoopTrips) +
                       "; ++" + counter + ") {");
                  const std::string outer = indent;
                  indent += "   ";
                  Val c = Expr(s->forCond);
                  if (!Ok()) return;
                  Line("if (" + Lane0(c) + " == 0.0) break;");
                  for (const auto& t : s->forBody) Stmt(t);
                  if (s->forStep) Stmt(s->forStep);
                  indent = outer;
                  Line("}");
                  return;
               }
            }
         }

         void Assign(const IRStmtPtr& s)
         {
            const std::string& target = s->assignTarget;
            const std::string& op = s->assignOp;
            if (target == "N")
            {
               Fail("N (GPU points carry no normal)");
               return;
            }
            Val rhs = Expr(s->rvalueExpr);
            if (!Ok()) return;
            if (target == "uv")
               return; // points have no UVs; the CPU path drops the write too
            const bool comp = !s->assignField.empty();

            if (target == "P" || target == "Cd")
            {
               if (!comp)
               {
                  StoreLane(target, op, Fit(rhs, 3));
                  return;
               }
               for (int k = 0; k < s->swizzleCount; ++k)
               {
                  const int idx = s->swizzleIndices[k];
                  if (idx < 0 || idx > 2) continue;
                  StoreLane(target + "." + "xyz"[idx], op, s->swizzleCount == 1 ? Lane0(rhs) : Comp(rhs, k));
               }
               return;
            }

            auto it = localVar.find(target);
            auto pro = prologueLanes.find(target);
            if (it == localVar.end() && pro != prologueLanes.end())
            {
               // The init sits at the top of main, so it names the uniform
               // itself, never a temp from the middle of the body.
               Uniform(target, pro->second);
               const ElementGlslUniform& u = res.uniforms[(size_t)uniformIndex[target]];
               DeclareLocal(target, pro->second, Fit({ u.glslName, u.lanes }, pro->second));
               it = localVar.find(target);
            }
            if (it == localVar.end())
            {
               // First assignment of a plain local: it takes the value, the
               // operator is not applied (the VM has nothing to apply it to).
               if (comp)
               {
                  Fail("component assignment to a new local");
                  return;
               }
               const std::string var = DeclareLocal(target, rhs.lanes, TypeName(rhs.lanes) + "(0.0)");
               Line(var + " = " + rhs.expr + ";");
               return;
            }

            const std::string var = it->second;
            const int have = localLanes[target];
            if (!comp)
            {
               if (op == "=")
               {
                  if (rhs.lanes != have) { Fail("local '" + target + "' changes width"); return; }
                  Line(var + " = " + rhs.expr + ";");
                  return;
               }
               if (std::max(have, rhs.lanes) != have) { Fail("local '" + target + "' changes width"); return; }
               Line(var + " = " + LocalOp(var, op, Fit(rhs, have)) + ";");
               return;
            }
            // The VM rebuilds a component-assigned local as a vec3.
            if (have != 3) { Fail("component assignment to a non-vec3 local"); return; }
            Val old = Temp(var, 3);
            for (int k = 0; k < s->swizzleCount; ++k)
            {
               const int idx = s->swizzleIndices[k];
               if (idx < 0 || idx > 2) continue;
               const std::string c = std::string(".") + "xyz"[idx];
               const std::string v = s->swizzleCount == 1 ? Lane0(rhs) : Comp(rhs, k);
               Line(var + c + " = " + (op == "=" ? v : LocalOp(old.expr + c, op, v)) + ";");
            }
         }

         static void CollectLoopTargets(const IRStmtPtr& st, std::vector<std::pair<std::string, int>>& out)
         {
            if (!st) return;
            if (st->kind == IRStmtKind::Assign)
            {
               const std::string& n = st->assignTarget;
               if (n == "P" || n == "N" || n == "uv" || n == "Cd") return;
               for (const auto& o : out)
                  if (o.first == n) return;
               const int lanes = !st->assignField.empty() ? 3 : (st->rvalueExpr ? std::max(1, std::min(4, st->rvalueExpr->type.lanes)) : 1);
               out.push_back({ n, lanes });
               return;
            }
            CollectLoopTargets(st->forInit, out);
            for (const auto* list : { &st->thenStmts, &st->elseStmts, &st->forBody })
               for (const auto& c : *list)
                  CollectLoopTargets(c, out);
            CollectLoopTargets(st->forStep, out);
         }

         bool ScanFrame(const IRNodePtr& n)
         {
            if (!n) return true;
            if (n->transferKind == TransferKind::Reduce) return Fail("reduce");
            for (const auto& c : n->children)
               if (!ScanFrame(c)) return false;
            return true;
         }
         bool ScanFrame(const IRStmtPtr& s)
         {
            if (!s) return true;
            for (const IRNodePtr& n : { s->rvalueExpr, s->ifCond, s->forCond, s->expr, s->pinInitExpr, s->attribInitExpr })
               if (!ScanFrame(n)) return false;
            for (const auto* list : { &s->thenStmts, &s->elseStmts, &s->forBody })
               for (const auto& t : *list)
                  if (!ScanFrame(t)) return false;
            return ScanFrame(s->forInit) && ScanFrame(s->forStep);
         }
      };
   }

   ElementGlslResult EmitElementPointGlsl(const ElementIRProgram& program)
   {
      ElementGlslResult res;
      Emitter e(res);

      if (!program.declaredStates.empty()) e.Fail("state cell");
      if (!program.postLoop.empty()) e.Fail("post-loop pass");
      if (program.writeMask.wroteN) e.Fail("N (GPU points carry no normal)");
      for (const auto& s : program.prologue)
      {
         if (e.Ok()) e.ScanFrame(s);
         if (s && s->kind == IRStmtKind::Assign && s->assignField.empty() && s->rvalueExpr)
            e.prologueLanes[s->assignTarget] = std::max(1, std::min(4, s->rvalueExpr->type.lanes));
      }
      if (!e.Ok()) return res;

      // Attribs are locals from the top: 0, except `size`, which starts at
      // the point's own size (the CPU path's ElementStore::FromParticles).
      for (const auto& a : program.declaredAttribs)
      {
         const int lanes = FieldType::GetLanesForType(a.second);
         if (lanes <= 0) continue;
         std::string init = TypeName(lanes) + "(0.0)";
         const bool isSize = a.first == ElementStore::kPointSizeAttrib && lanes == 1;
         if (isSize) init = "(fld_baseSize != 0.0 ? W / fld_baseSize : 0.0)";
         const std::string var = e.DeclareLocal(a.first, lanes, init);
         if (isSize) e.sizeVar = var;
      }

      for (const auto& s : program.elementLoop)
         e.Stmt(s);
      if (!e.Ok()) return res;

      res.writesP = program.writeMask.wroteP;
      res.writesCd = program.writeMask.wroteCd;
      res.writesSize = !e.sizeVar.empty() && program.writeMask.WroteAttrib(ElementStore::kPointSizeAttrib);

      std::ostringstream src;
      src << kScalarHelpers << VecOverloads() << kRandHelpers << PhaseShape::kGlslPrelude;
      src << "uniform float fld_baseSize;\n";
      for (const auto& u : res.uniforms)
         src << "uniform " << TypeName(u.lanes) << " " << u.glslName << ";\n";
      for (const auto& r : res.rands)
         src << "uniform uvec2 " << r.glslName << ";\n";
      src << "void main() {\n";
      src << "   LoadPoint();\n";
      src << e.decls.str();
      src << e.body.str();
      if (res.writesSize)
         src << "   float fld_w = W == 0.0 ? 0.0 : max(" << e.sizeVar << ", 0.0) * fld_baseSize;\n";
      else
         src << "   float fld_w = W;\n";
      src << "   outPos = vec4(P, fld_w);\n";
      src << "   outColor = vec4(Cd, 1.0);\n";
      src << "}\n";
      res.source = src.str();
      return res;
   }

   void ElementRandTimeWord(bool isSh, double speed, double t, unsigned int& lo, unsigned int& hi)
   {
      const double tt = isSh ? std::floor(t * speed) : t * speed;
      double frac = std::fmod(tt / 300.0, 1.0);
      if (frac < 0.0)
         frac += 1.0;
      const uint64_t h = Xorwise((uint64_t)(frac * 536870912.0));
      lo = (unsigned int)(h & 0xFFFFFFFFull);
      hi = (unsigned int)(h >> 32);
   }
}

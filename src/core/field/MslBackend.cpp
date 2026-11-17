#include "MslBackend.h"
#include "FieldNoise.h"
#include "../../audio/PhaseShape.h"
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// GLSL -> MSL, every difference this emitter makes (the walk itself mirrors GlslBackend.cpp line for line):
//
//   GLSL                              MSL
//   vecN / bvecN                      floatN / boolN
//   uniform T x                       float4 slot in `constant float4* fld_U`, unpacked to a local at the top of main
//   mix(a, b, bvecN sel)              select(a, b, sel)
//   lessThan / equal / ... (a, b)     a < b, a == b, ... (vector compare gives boolN)
//   atan(y, x)                        atan2(y, x)
//   fld_mod = x - y * trunc(x / y)    same (Field's mod is C fmod, not GLSL mod; kept in the prelude)
//   texture(t, c)                     t.sample(fld_smp, c), fld_smp = linear, clamp to edge (GL's FBO default)
//   textureSize(t, 0)                 float2(t.get_width(), t.get_height())
//   texelFetch(t, ivec2(p), 0)        t.read(uint2(p))
//   gl_FragCoord.xy                   fld_in.position.xy (pixel centre; row 0 = uv.y near 0, same memory as GL)
//   vUv                               position.xy / res
//   min/max/step(vec, float)          scalar broadcast to the vector first (MSL has no mixed overloads)
//   0.0 / 0.0 (NaN)                   as_type<float>(0x7fc00000u)
// Compile with fast math ON (the Metal default). Apple's GL runs on Metal with fast math, and the presets' hashes,
// fract(sin(x) * 43758), amplify the last bits of sin: with precise math, Voronoi, Nebula and Reaction Diffusion
// differ from GL by up to 255/255; with fast math every preset matches to 0.02/255 (row 1.1 self-test).

namespace Field
{
   namespace
   {
      constexpr const char* kMslHelperPrelude =
         "// ---- helper prelude (always emitted; Expression.cpp semantics) ----\n"
         "static inline float fld_nan() { return as_type<float>(0x7fc00000u); }\n"
         "static inline float  fld_mod(float x, float y) { return x - y * trunc(x / y); }\n"
         "static inline float2 fld_mod(float2 x, float2 y) { return x - y * trunc(x / y); }\n"
         "static inline float3 fld_mod(float3 x, float3 y) { return x - y * trunc(x / y); }\n"
         "static inline float4 fld_mod(float4 x, float4 y) { return x - y * trunc(x / y); }\n"
         "static inline float  fld_clamp(float x, float a, float b) { return min(max(x, a), b); }\n"
         "static inline float2 fld_clamp(float2 x, float2 a, float2 b) { return min(max(x, a), b); }\n"
         "static inline float3 fld_clamp(float3 x, float3 a, float3 b) { return min(max(x, a), b); }\n"
         "static inline float4 fld_clamp(float4 x, float4 a, float4 b) { return min(max(x, a), b); }\n"
         "static inline float  fld_lerp(float a, float b, float t) { return a + (b - a) * t; }\n"
         "static inline float2 fld_lerp(float2 a, float2 b, float2 t) { return a + (b - a) * t; }\n"
         "static inline float3 fld_lerp(float3 a, float3 b, float3 t) { return a + (b - a) * t; }\n"
         "static inline float4 fld_lerp(float4 a, float4 b, float4 t) { return a + (b - a) * t; }\n"
         "static inline float fld_smoothstep(float e0, float e1, float x) {\n"
         "   if (e0 == e1) return (x < e0) ? 0.0 : 1.0;\n"
         "   float u = fld_clamp((x - e0) / (e1 - e0), 0.0, 1.0);\n"
         "   return u * u * (3.0 - 2.0 * u);\n"
         "}\n"
#define FLD_MSL_SMOOTH(T) \
         "static inline " T " fld_smoothstep(" T " e0, " T " e1, " T " x) {\n" \
         "   " T " u = fld_clamp((x - e0) / select(e1 - e0, " T "(1.0), e0 == e1), " T "(0.0), " T "(1.0));\n" \
         "   " T " h = u * u * (" T "(3.0) - " T "(2.0) * u);\n" \
         "   return select(h, select(" T "(1.0), " T "(0.0), x < e0), e0 == e1);\n" \
         "}\n"
         FLD_MSL_SMOOTH("float2")
         FLD_MSL_SMOOTH("float3")
         FLD_MSL_SMOOTH("float4")
#undef FLD_MSL_SMOOTH
         "static inline float fld_pow(float b, float e) {\n"
         "   if (b >= 0.0) return pow(b, e);\n"
         "   if (e == trunc(e)) return ((e - 2.0 * trunc(e / 2.0)) == 0.0) ? pow(-b, e) : -pow(-b, e);\n"
         "   return fld_nan();\n"
         "}\n"
         "static inline float2 fld_pow(float2 b, float2 e) { return float2(fld_pow(b.x, e.x), fld_pow(b.y, e.y)); }\n"
         "static inline float3 fld_pow(float3 b, float3 e) { return float3(fld_pow(b.x, e.x), fld_pow(b.y, e.y), fld_pow(b.z, e.z)); }\n"
         "static inline float4 fld_pow(float4 b, float4 e) { return float4(fld_pow(b.x, e.x), fld_pow(b.y, e.y), fld_pow(b.z, e.z), fld_pow(b.w, e.w)); }\n";

      // The phase-shape prelude is shared with GLSL; its only GLSL-only spelling is the vector type names.
      std::string ToMslTypes(const std::string& src)
      {
         std::string out;
         out.reserve(src.size() + 64);
         for (size_t i = 0; i < src.size(); i++)
         {
            const bool wordStart = i == 0 || !(std::isalnum((unsigned char)src[i - 1]) || src[i - 1] == '_');
            if (wordStart && src.compare(i, 3, "vec") == 0 && i + 3 < src.size() && src[i + 3] >= '2' && src[i + 3] <= '4' &&
                (i + 4 >= src.size() || !(std::isalnum((unsigned char)src[i + 4]) || src[i + 4] == '_')))
            {
               out += "float";
               out += src[i + 3];
               i += 3;
               continue;
            }
            out += src[i];
         }
         return out;
      }

      bool IsPhaseShapeName(const std::string& name)
      {
         PhaseShape::Kind k;
         return PhaseShape::FieldBuiltin(name.c_str(), k);
      }

      std::string FormatFloat(double val)
      {
         std::ostringstream ss;
         ss << std::setprecision(9) << val;
         std::string s = ss.str();
         if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos)
            s += ".0";
         if (s == "inf" || s == "-inf" || s == "nan")
            return s == "nan" ? "fld_nan()" : (s[0] == '-' ? "(-INFINITY)" : "INFINITY");
         return s;
      }

      std::string MslType(const FieldType& t)
      {
         if (t.lanes == 1)
            return t.kind == DataType::Int ? "int" : "float"; // Bool lowers to float, as in GLSL
         if (t.lanes == 2) return "float2";
         if (t.lanes == 3) return "float3";
         if (t.lanes == 4) return "float4";
         return "float";
      }

      std::string Broadcast(const std::string& arg, int argLanes, int resLanes)
      {
         if (argLanes == 1 && resLanes > 1)
            return "float" + std::to_string(resLanes) + "(" + arg + ")";
         return arg;
      }

      struct Ctx
      {
         std::ostringstream out;
         int tempCounter = 0;
         int branchCount = 0;
         std::unordered_set<std::string> declaredLocals;
         std::unordered_map<std::string, int> hoistedMap;
         std::unordered_set<std::string> stateNames;
         std::unordered_map<std::string, int> stateChannel;
         std::unordered_map<std::string, BoundaryMode> stateBoundary;
         std::unordered_map<std::string, std::vector<float>> stateInit;
         std::unordered_set<std::string> paramNames;
         std::unordered_set<std::string> imageInputNames;
         int offsetReadCount = 0;
         int imageReadCount = 0;
         std::string error;

         void Line(const std::string& l) { out << l << "\n"; }
         std::string Temp() { return "fld_t" + std::to_string(tempCounter++); }
      };

      std::string BoundaryCoord(const std::string& coord, BoundaryMode mode, const std::string& half)
      {
         if (mode == BoundaryMode::Wrap) return "fract(" + coord + ")";
         return "clamp(" + coord + ", " + half + ", float2(1.0) - " + half + ")";
      }

      std::string Emit(const IRNodePtr& node, Ctx& ctx)
      {
         if (!ctx.error.empty()) return "";
         if (!node) return "0.0";

         switch (node->kind)
         {
            case IRKind::Literal:
            {
               if (node->type.lanes == 1)
               {
                  if (node->type.kind == DataType::Bool) return node->numberValue != 0.0 ? "1.0" : "0.0";
                  if (node->type.kind == DataType::Int) return std::to_string((int)node->numberValue);
                  return FormatFloat(node->numberValue);
               }
               std::string args;
               for (int i = 0; i < node->type.lanes; i++)
                  args += (i ? ", " : "") + FormatFloat(node->vecValues[i]);
               return MslType(node->type) + "(" + args + ")";
            }

            case IRKind::ImageRead:
            {
               std::string coord = Emit(node->children[0], ctx);
               if (!ctx.error.empty()) return "";
               std::string c = BoundaryCoord(coord, BoundaryMode::Clamp,
                                             "(0.5 / float2(fld_srcTex.get_width(), fld_srcTex.get_height()))");
               ctx.offsetReadCount++;
               ctx.imageReadCount++;
               return "fld_srcTex.sample(fld_smp, " + c + ")";
            }

            case IRKind::Variable:
            case IRKind::StateRead:
            {
               const std::string& name = node->varName;
               if (node->kind == IRKind::StateRead && node->isOffsetRead && !node->children.empty())
               {
                  std::string coord = Emit(node->children[0], ctx);
                  if (!ctx.error.empty()) return "";
                  BoundaryMode bm = BoundaryMode::Clamp;
                  auto bIt = ctx.stateBoundary.find(name);
                  if (bIt != ctx.stateBoundary.end()) bm = bIt->second;
                  int chan = 0;
                  auto cIt = ctx.stateChannel.find(name);
                  if (cIt != ctx.stateChannel.end()) chan = cIt->second;
                  const char* chanNames[] = { "r", "g", "b", "a" };
                  std::string c = BoundaryCoord(coord, bm, "(0.5 / fld_res)");
                  std::string fetch = "fld_s_bank0.sample(fld_smp, " + c + ")." + chanNames[chan];
                  if (bm == BoundaryMode::Border)
                  {
                     float initV = 0.0f;
                     auto iIt = ctx.stateInit.find(name);
                     if (iIt != ctx.stateInit.end() && !iIt->second.empty()) initV = iIt->second[0];
                     std::string inside = "float(all(" + coord + " >= float2(0.0)) && all(" + coord + " <= float2(1.0)))";
                     fetch = "mix(" + FormatFloat((double)initV) + ", " + fetch + ", " + inside + ")";
                  }
                  ctx.offsetReadCount++;
                  return "(" + fetch + ")";
               }

               if (name == "uv" || name == "xy" || name == "res" || name == "aspect" || name == "col" ||
                   name == "alpha" || name == "age")
                  return name;
               if (name == "t") return "fld_t";
               if (name == "dt") return "fld_dt";
               if (name == "frame") return "fld_frame";
               if (name == "noteOn" || name == "notePitch" || name == "noteVel") return "0.0";
               auto hit = ctx.hoistedMap.find(name);
               if (hit != ctx.hoistedMap.end()) return "fld_h" + std::to_string(hit->second);
               if (ctx.paramNames.count(name)) return "fld_p_" + name;
               if (ctx.imageInputNames.count(name)) return "src";
               return "fld_v_" + name;
            }

            case IRKind::Access:
            {
               if (node->children.empty()) return "0.0";
               std::string base = Emit(node->children[0], ctx);
               std::string r = ctx.Temp();
               ctx.Line("   " + MslType(node->type) + " " + r + " = " + base + "." + node->field + ";");
               return r;
            }

            case IRKind::Unary:
            {
               if (node->children.empty()) return "0.0";
               std::string o = Emit(node->children[0], ctx);
               std::string r = ctx.Temp();
               const std::string ty = MslType(node->type);
               if (node->op == "!")
                  ctx.Line("   float " + r + " = (" + o + " == 0.0) ? 1.0 : 0.0;");
               else
                  ctx.Line("   " + ty + " " + r + " = " + node->op + o + ";");
               return r;
            }

            case IRKind::Binary:
            {
               if (node->children.size() < 2) return "0.0";
               std::string lhs = Emit(node->children[0], ctx);
               std::string rhs = Emit(node->children[1], ctx);
               std::string r = ctx.Temp();
               const std::string ty = MslType(node->type);
               const std::string& op = node->op;
               const int resLanes = node->type.lanes;
               const int lLanes = node->children[0]->type.lanes;
               const int rLanes = node->children[1]->type.lanes;
               if (op == "%" || op == "^")
               {
                  ctx.Line("   " + ty + " " + r + " = " + (op == "%" ? "fld_mod(" : "fld_pow(") +
                           Broadcast(lhs, lLanes, resLanes) + ", " + Broadcast(rhs, rLanes, resLanes) + ");");
               }
               else if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=")
               {
                  if (lLanes == 1)
                     ctx.Line("   float " + r + " = (" + lhs + " " + op + " " + rhs + ") ? 1.0 : 0.0;");
                  else
                  {
                     const std::string v = "float" + std::to_string(lLanes);
                     ctx.Line("   " + v + " " + r + " = select(" + v + "(0.0), " + v + "(1.0), " + lhs + " " + op + " " + rhs + ");");
                  }
               }
               else if (op == "&&" || op == "||")
                  ctx.Line("   float " + r + " = (" + lhs + " != 0.0 " + op + " " + rhs + " != 0.0) ? 1.0 : 0.0;");
               else
                  ctx.Line("   " + ty + " " + r + " = " + lhs + " " + op + " " + rhs + ";");
               return r;
            }

            case IRKind::Call:
            {
               const std::string& name = node->callee;
               std::vector<std::string> args;
               for (const auto& c : node->children)
                  args.push_back(Emit(c, ctx));
               std::string r = ctx.Temp();
               const std::string ty = MslType(node->type);
               const int resLanes = node->type.lanes;
               auto bc = [&](size_t i) -> std::string {
                  if (i >= args.size() || i >= node->children.size()) return i < args.size() ? args[i] : "0.0";
                  return Broadcast(args[i], node->children[i]->type.lanes, resLanes);
               };
               auto joined = [&](bool broadcast) {
                  std::string s;
                  for (size_t i = 0; i < args.size(); i++)
                     s += (i ? ", " : "") + (broadcast ? bc(i) : args[i]);
                  return s;
               };

               if (name == "vec2" || name == "vec3" || name == "vec4")
                  ctx.Line("   " + ty + " " + r + " = " + ty + "(" + joined(false) + ");");
               else if (IsImageBuiltin(name))
                  ctx.Line("   " + ty + " " + r + " = fld_" + name + "(fm, fi, flod, " + args[0] + ", " + args[1] + ");");
               else if (IsNoiseBuiltin(name))
                  ctx.Line("   " + ty + " " + r + " = fld_" + name + "(" + joined(false) + ");");
               else if (name == "mod" || name == "fmod")
                  ctx.Line("   " + ty + " " + r + " = fld_mod(" + bc(0) + ", " + bc(1) + ");");
               else if (name == "pow")
                  ctx.Line("   " + ty + " " + r + " = fld_pow(" + bc(0) + ", " + bc(1) + ");");
               else if (name == "clamp")
                  ctx.Line("   " + ty + " " + r + " = fld_clamp(" + bc(0) + ", " + bc(1) + ", " + bc(2) + ");");
               else if (name == "lerp" || name == "mix")
                  ctx.Line("   " + ty + " " + r + " = fld_lerp(" + bc(0) + ", " + bc(1) + ", " + bc(2) + ");");
               else if (IsPhaseShapeName(name))
                  ctx.Line("   " + ty + " " + r + " = fld_" + name + "(" + bc(0) + ", " + bc(1) + ");");
               else if (name == "smoothstep")
                  ctx.Line("   " + ty + " " + r + " = fld_smoothstep(" + bc(0) + ", " + bc(1) + ", " + bc(2) + ");");
               else if (name == "round")
                  ctx.Line("   " + ty + " " + r + " = floor(" + args[0] + " + " + (resLanes == 1 ? std::string("0.5") : ty + "(0.5)") + ");");
               else if (name == "atan2")
                  ctx.Line("   " + ty + " " + r + " = atan2(" + args[0] + ", " + args[1] + ");");
               else if (name == "if")
               {
                  // Bool-selector select, as GLSL's mix(b, a, bvec): an inf/NaN in the branch not taken stays out.
                  ctx.branchCount++;
                  const std::string sel = "(" + args[0] + " != 0.0)";
                  ctx.Line("   " + ty + " " + r + " = select(" + bc(2) + ", " + bc(1) + ", " +
                           (resLanes == 1 ? sel : "bool" + std::to_string(resLanes) + sel) + ");");
               }
               else if (name.rfind("reduce.", 0) == 0 || node->transferKind == TransferKind::Reduce)
               {
                  ctx.error = "reduce is not lowerable in MSL; it must arrive as a uniform from CPU reduction";
                  return "";
               }
               else if (name == "min" || name == "max" || name == "step")
                  ctx.Line("   " + ty + " " + r + " = " + name + "(" + joined(true) + ");");
               else
                  ctx.Line("   " + ty + " " + r + " = " + name + "(" + joined(false) + ");");
               return r;
            }
         }
         return "0.0";
      }

      void EmitStmt(const IRStmtPtr& stmt, Ctx& ctx)
      {
         if (!ctx.error.empty() || !stmt) return;
         switch (stmt->kind)
         {
            case IRStmtKind::Assign:
            {
               std::string v = Emit(stmt->rvalueExpr, ctx);
               const std::string& target = stmt->assignTarget;
               if (target == "col")
               {
                  if (!stmt->assignField.empty())
                     ctx.Line("   col." + stmt->assignField + " " + stmt->assignOp + " " + v + ";");
                  else if (stmt->rvalueExpr && stmt->rvalueExpr->type.lanes == 1)
                     ctx.Line("   col = float3(" + v + ");");
                  else
                     ctx.Line("   col = " + v + ";");
               }
               else if (target == "alpha")
                  ctx.Line("   alpha " + stmt->assignOp + " " + v + ";");
               else
               {
                  const std::string var = "fld_v_" + target;
                  if (!stmt->assignField.empty())
                     ctx.Line("   " + var + "." + stmt->assignField + " " + stmt->assignOp + " " + v + ";");
                  else if (!ctx.declaredLocals.count(target) && !ctx.stateNames.count(target))
                  {
                     ctx.Line("   " + MslType(stmt->rvalueExpr ? stmt->rvalueExpr->type : FieldType(DataType::Float, 1)) + " " +
                              var + " = " + v + ";");
                     ctx.declaredLocals.insert(target);
                  }
                  else
                     ctx.Line("   " + var + " " + stmt->assignOp + " " + v + ";");
               }
               break;
            }
            case IRStmtKind::StateWrite:
               break;
            case IRStmtKind::Expr:
               if (stmt->expr) Emit(stmt->expr, ctx);
               break;
            case IRStmtKind::If:
            {
               std::string cond = Emit(stmt->ifCond, ctx);
               ctx.Line("   if (" + cond + " != 0.0) {");
               for (const auto& s : stmt->thenStmts) EmitStmt(s, ctx);
               if (!stmt->elseStmts.empty())
               {
                  ctx.Line("   } else {");
                  for (const auto& s : stmt->elseStmts) EmitStmt(s, ctx);
               }
               ctx.Line("   }");
               break;
            }
            case IRStmtKind::For:
            {
               if (stmt->forInit) EmitStmt(stmt->forInit, ctx);
               std::string cond = stmt->forCond ? Emit(stmt->forCond, ctx) : "1.0";
               ctx.Line("   while (" + cond + " != 0.0) {");
               for (const auto& b : stmt->forBody) EmitStmt(b, ctx);
               if (stmt->forStep) EmitStmt(stmt->forStep, ctx);
               ctx.Line("   }");
               break;
            }
            case IRStmtKind::DeclAttrib:
            case IRStmtKind::DeclState:
            case IRStmtKind::DeclOutput:
            case IRStmtKind::DeclInput:
            case IRStmtKind::Emit:
            case IRStmtKind::Connect:
            case IRStmtKind::SetParam:
            case IRStmtKind::Place:
               break;
         }
      }

      bool HasReduce(const IRNodePtr& n)
      {
         if (!n) return false;
         if (n->transferKind == TransferKind::Reduce || n->callee.rfind("reduce.", 0) == 0) return true;
         for (const auto& c : n->children)
            if (HasReduce(c)) return true;
         return false;
      }

      std::string Swz(int lanes)
      {
         return lanes == 2 ? ".xy" : lanes == 3 ? ".xyz" : lanes == 4 ? "" : ".x";
      }
   }

   MslEmitResult EmitMsl(const PixelIRProgram& program)
   {
      MslEmitResult result;
      for (const auto& s : program.pixelBody)
         if (s->rvalueExpr && HasReduce(s->rvalueExpr))
         {
            result.error = "reduce is not lowerable in MSL; a reduction must arrive as a uniform";
            return result;
         }

      Ctx ctx;
      int imageInputs = 0;
      for (const auto& in : program.declaredInputs)
      {
         if (in.typeName != "image") continue;
         if (++imageInputs > 1)
         {
            result.error = "only one `input pixel image` is supported per Field Pixel kernel right now";
            return result;
         }
         ctx.imageInputNames.insert(in.name);
      }

      // Uniform slots and their unpacking lines (emitted at the top of fld_fs).
      std::vector<std::string> unpack;
      int slot = kMslFixedSlots;
      for (size_t i = 0; i < program.prologue.size(); i++)
      {
         const auto& s = program.prologue[i];
         FieldType ft = s->rvalueExpr ? s->rvalueExpr->type : FieldType(DataType::Float, 1);
         UniformSlot u;
         u.name = "fld_h" + std::to_string(i);
         u.varName = s->assignTarget;
         u.type = ft;
         u.domain = Domain::Frame;
         u.hoistedIndex = (int)i;
         u.location = slot;
         result.uniforms.push_back(u);
         ctx.hoistedMap[s->assignTarget] = (int)i;
         const std::string ty = MslType(ft);
         unpack.push_back("   " + ty + " " + u.name + " = " + (ft.kind == DataType::Int && ft.lanes == 1 ? "int(" : ty + "(") +
                          "fld_U[" + std::to_string(slot) + "]" + Swz(ft.lanes) + ");");
         slot++;
      }
      for (size_t i = 0; i < program.declaredParams.size(); i++)
      {
         const auto& p = program.declaredParams[i];
         UniformSlot u;
         u.name = "fld_p_" + p.name;
         u.varName = p.name;
         u.type = FieldType(DataType::Float, 1);
         u.domain = Domain::Graph;
         u.paramIndex = (int)i;
         u.location = slot;
         result.uniforms.push_back(u);
         ctx.paramNames.insert(p.name);
         unpack.push_back("   float " + u.name + " = fld_U[" + std::to_string(slot) + "].x;");
         slot++;
      }
      result.uniformSlotCount = slot;

      const bool hasState = !program.declaredStates.empty();
      for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
      {
         const auto& st = program.declaredStates[i];
         StateSlot ss;
         ss.name = st.name;
         ss.type = st.type;
         ss.lanes = st.lanes;
         ss.channel = (int)i;
         ss.initialValues = st.initialValues;
         result.state.push_back(ss);
         ctx.stateNames.insert(st.name);
         ctx.stateChannel[st.name] = (int)i;
         ctx.stateBoundary[st.name] = st.boundary;
         ctx.stateInit[st.name] = st.initialValues;
      }

      // The body first, into its own stream, so the header can follow what it used.
      ctx.Line("   // ---- SSA body ----");
      for (const auto& s : program.pixelBody)
      {
         EmitStmt(s, ctx);
         if (!ctx.error.empty())
         {
            result.error = ctx.error;
            return result;
         }
      }
      const std::string body = ctx.out.str();

      std::ostringstream o;
      o << "#include <metal_stdlib>\nusing namespace metal;\n\n"
        << kMslHelperPrelude << ToMslTypes(PhaseShape::kGlslPrelude) << NoiseMsl() << "\n"
        << "constexpr sampler fld_smp(filter::linear, address::clamp_to_edge);\n\n"
        << "struct FldVOut { float4 position [[position]]; };\n"
        << "vertex FldVOut fld_vs(uint vid [[vertex_id]])\n{\n"
        << "   float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));\n"
        << "   FldVOut o; o.position = float4(p * 2.0 - 1.0, 0.0, 1.0); return o;\n}\n\n"
        << "fragment float4 fld_fs(FldVOut fld_in [[stage_in]], constant float4* fld_U [[buffer(0)]],\n"
        << "                       texture2d<float> fld_srcTex [[texture(0)]]"
        << (hasState ? ",\n                       texture2d<float> fld_s_bank0 [[texture(1)]]" : "") << ")\n{\n"
        << "   // ---- uniforms ----\n"
        << "   float2 fld_res = fld_U[0].xy;\n"
        << "   float  fld_t = fld_U[0].z;\n"
        << "   float  fld_dt = fld_U[0].w;\n"
        << "   int    fld_frame = int(fld_U[1].x);\n"
        << "   float  fld_age = fld_U[1].y;\n"
        << "   float  fld_srcAlpha = fld_U[1].z;\n"
        << "   int    fld_outMode = int(fld_U[1].w);\n";
      for (const auto& l : unpack) o << l << "\n";
      o << "   // ---- reserved-name bindings ----\n"
        << "   float2 uv = fld_in.position.xy / fld_res;\n"
        << "   float2 xy = fld_in.position.xy;\n"
        << "   float2 res = fld_res;\n"
        << "   float  age = fld_age;\n"
        << "   float  aspect = fld_res.x / fld_res.y;\n"
        << "   float4 src = fld_srcTex.sample(fld_smp, uv);\n"
        << "   float3 col = src.rgb;\n"
        << "   float  alpha = mix(1.0, src.a, fld_srcAlpha);\n"
        << "   (void)fld_t; (void)fld_dt; (void)fld_frame; (void)xy; (void)res; (void)age; (void)aspect; (void)fld_outMode;\n";
      if (hasState)
      {
         const char* chanNames[] = { "r", "g", "b", "a" };
         o << "   float4 fld_st0 = fld_s_bank0.read(uint2(fld_in.position.xy));\n";
         for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
         {
            const auto& st = program.declaredStates[i];
            o << "   " << MslType(FieldType(st.type, st.lanes)) << " fld_v_" << st.name << " = fld_st0." << chanNames[i] << ";\n";
         }
      }
      o << body << "   // ---- writes ----\n";
      if (hasState)
      {
         std::string ch[4] = { "0.0", "0.0", "0.0", "0.0" };
         for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
            ch[i] = "float(fld_v_" + program.declaredStates[i].name + ")";
         o << "   float4 fld_stateOut = float4(" << ch[0] << ", " << ch[1] << ", " << ch[2] << ", " << ch[3] << ");\n"
           << "   return (fld_outMode == 0) ? fld_stateOut : float4(col, alpha);\n";
      }
      else
         o << "   return float4(col, alpha);\n";
      o << "}\n";

      result.source = o.str();
      result.branchCount = ctx.branchCount;
      result.offsetReadCount = ctx.offsetReadCount;
      result.usesOffsetReads = ctx.offsetReadCount > ctx.imageReadCount;
      return result;
   }

   std::string MslMaterialPrelude()
   {
      std::string s = kMslHelperPrelude;
      s += ToMslTypes(PhaseShape::kGlslPrelude);
      s += NoiseMsl();
      // Row 2.4 field images: fm is the host's map array, fi the four slots as index + 1 (0 none), flod the mip level
      // (below 0: the raster's own screen derivatives).
      s += "\nconstexpr sampler kFldImgS(filter::linear, mip_filter::linear, address::repeat, max_anisotropy(8));\n"
           "static float4 fld_img(array<texture2d<float>, 16> fm, float4 fi, float flod, float slot, float2 uv)\n{\n"
           "   const int t = int(fi[clamp(int(slot + 0.5), 0, 3)] + 0.5) - 1;\n"
           "   if (t < 0 || t > 15) return float4(1.0);\n"
           "   if (flod < 0.0) return fm[t].sample(kFldImgS, uv);\n"
           "   return fm[t].sample(kFldImgS, uv, level(flod));\n}\n"
           "static float4 fld_imgc(array<texture2d<float>, 16> fm, float4 fi, float flod, float slot, float2 uv)\n{\n"
           "   const float4 c = fld_img(fm, fi, flod, slot, uv);\n"
           "   return float4(select(pow((c.rgb + 0.055) / 1.055, float3(2.4)), c.rgb / 12.92, c.rgb < float3(0.04045)), c.a);\n}\n";
      s += "\nstruct FldMat { float3 base; float roughness; float metallic; float3 normal; float height; float3 emission; "
           "float opacity; float mask; float4 prm0; float4 prm1; float4 prm2; float4 prm3; };\n";
      return s;
   }

   MaterialFnResult EmitMslMaterialFn(const PixelIRProgram& program, const std::string& fnName,
                                      const std::vector<float>& paramValues)
   {
      MaterialFnResult r;
      if (program.kind != PixelProgramKind::Material)
         r.error = "not a Material-domain program";
      else if (!program.declaredStates.empty())
         r.error = "state is not available in the Material domain (a surface hit has no previous frame)";
      else if (!program.declaredInputs.empty())
         r.error = "input pins are not available in the Material domain";
      if (!r.error.empty()) return r;
      for (const auto& s : program.pixelBody)
         if (s->rvalueExpr && HasReduce(s->rvalueExpr)) { r.error = "reduce is not available in the Material domain"; return r; }
      Ctx ctx;
      ctx.Line("static void " + fnName + "(thread FldMat& o, float3 P, float3 N, float2 UV, float fld_t, float3 W, float3 V, float4 G, float4 A, "
               "array<texture2d<float>, 16> fm, float4 fi, float flod)");
      ctx.Line("{");
      ctx.Line("   float fld_dt = 0.0; int fld_frame = 0; (void)fld_dt; (void)fld_frame; (void)fld_t;");
      for (int i = 0; i < kMaterialInputCount; i++)
      {
         const MaterialChannel& c = kMaterialInputs[i];
         const char* gl = std::string(c.name) == "backfacing" ? "G.x" : std::string(c.name) == "objrand" ? "G.y"
                          : std::string(c.name) == "curvature" ? "G.z" : std::string(c.name) == "attr" ? "A.x" : "G.w";
         const std::string v = (c.lanes == 1) ? gl : c.name; // G = (backfacing, objrand, curvature, cavity)
         ctx.Line("   " + MslType(FieldType(c.type, c.lanes)) + " fld_v_" + c.name + " = " + v + "; (void)fld_v_" + c.name + ";");
         ctx.declaredLocals.insert(c.name);
      }
      for (int i = 0; i < kMaterialOutputCount; i++)
      {
         const MaterialChannel& c = kMaterialOutputs[i];
         ctx.Line("   " + MslType(FieldType(c.type, c.lanes)) + " fld_v_" + c.name + " = o." + c.name + ";");
         ctx.declaredLocals.insert(c.name);
      }
      for (size_t i = 0; i < program.declaredParams.size(); i++)
      {
         const auto& p = program.declaredParams[i];
         const float v = i < paramValues.size() ? paramValues[i] : (float)p.defaultValue;
         // Row 3.4: live knobs come from the host's record (o.prm*), the rest are baked (GlslBackend.cpp, same rule).
         if (i < (size_t)kMaterialLiveParams)
            ctx.Line("   float fld_p_" + p.name + " = o.prm" + std::to_string(i / 4) + "." + "xyzw"[i % 4] + ";");
         else
            ctx.Line("   float fld_p_" + p.name + " = " + FormatFloat(v) + ";");
         ctx.paramNames.insert(p.name);
      }
      for (const auto& s : program.prologue) EmitStmt(s, ctx);
      for (const auto& s : program.pixelBody) EmitStmt(s, ctx);
      if (!ctx.error.empty()) { r.error = ctx.error; return r; }
      for (int i = 0; i < kMaterialOutputCount; i++)
         ctx.Line(std::string("   o.") + kMaterialOutputs[i].name + " = fld_v_" + kMaterialOutputs[i].name + ";");
      ctx.Line("}");
      r.source = ctx.out.str();
      return r;
   }
}

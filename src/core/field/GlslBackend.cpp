#include "GlslBackend.h"
#include "GlslHelpers.h"
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Field
{
   namespace
   {
      std::string FormatFloat(double val)
      {
         std::ostringstream ss;
         ss << std::setprecision(9) << val;
         std::string s = ss.str();
         if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos)
         {
            s += ".0";
         }
         return s;
      }

      std::string GlslTypeString(const FieldType& t)
      {
         if (t.lanes == 1)
         {
            if (t.kind == DataType::Int) return "int";
            // Bool lowers to float: every comparison / ! / && / || this backend
            // emits produces a 0.0-or-1.0 float, and GLSL has no implicit
            // bool <-> float conversion, so a `bool` declaration cannot be
            // initialised from one.
            return "float";
         }
         if (t.lanes == 2) return "vec2";
         if (t.lanes == 3) return "vec3";
         if (t.lanes == 4) return "vec4";
         return "float";
      }

      std::string GlslBoolVecType(int lanes)
      {
         if (lanes == 1) return "bool";
         if (lanes == 2) return "bvec2";
         if (lanes == 3) return "bvec3";
         if (lanes == 4) return "bvec4";
         return "bool";
      }

      // GLSL performs no implicit scalar->vector conversion on function
      // arguments, and the fld_ helpers replace builtins that DO carry
      // (genType, float, float) overloads - clamp, mix, smoothstep, mod.
      // Promote every scalar argument to the call's result rank so
      // `clamp(col, 0.0, 1.0)` keeps working.
      std::string Broadcast(const std::string& arg, int argLanes, int resLanes)
      {
         if (argLanes == 1 && resLanes > 1)
            return "vec" + std::to_string(resLanes) + "(" + arg + ")";
         return arg;
      }

      struct EmitterContext
      {
         std::ostringstream out;
         int tempCounter = 0;
         int branchCount = 0;
         std::unordered_set<std::string> declaredLocals;
         std::unordered_map<std::string, int> hoistedMap; // varName -> uniform index
         std::unordered_set<std::string> stateNames;
         // Build step 22 (OPEN-C): per-cell channel + boundary mode, so an
         // offset read knows which channel of the bank to take and what to do
         // outside [0,1].
         std::unordered_map<std::string, int> stateChannel;
         std::unordered_map<std::string, BoundaryMode> stateBoundary;
         std::unordered_map<std::string, std::vector<float>> stateInit;
         int offsetReadCount = 0;
         std::unordered_set<std::string> paramNames;
         std::vector<int> lineToIrNode;
         int currentLine = 1;
         std::string error;

         void EmitLine(const std::string& line, int nodeIdx = -1)
         {
            out << line << "\n";
            lineToIrNode.push_back(nodeIdx);
            currentLine++;
         }

         std::string FreshTemp()
         {
            return "fld_t" + std::to_string(tempCounter++);
         }
      };

      std::string EmitNode(const IRNodePtr& node, EmitterContext& ctx)
      {
         if (!ctx.error.empty()) return "";
         if (!node) return "0.0";

         switch (node->kind)
         {
            case IRKind::Literal:
            {
               if (node->type.lanes == 1)
               {
                  if (node->type.kind == DataType::Bool)
                     return node->numberValue != 0.0 ? "1.0" : "0.0";
                  if (node->type.kind == DataType::Int)
                     return std::to_string((int)node->numberValue);
                  return FormatFloat(node->numberValue);
               }
               else
               {
                  std::string typeStr = GlslTypeString(node->type);
                  std::string args;
                  for (int i = 0; i < node->type.lanes; i++)
                  {
                     if (i > 0) args += ", ";
                     args += FormatFloat(node->vecValues[i]);
                  }
                  return typeStr + "(" + args + ")";
               }
            }

            case IRKind::Variable:
            case IRKind::StateRead:
            {
               const std::string& name = node->varName;

               // Build step 22 (OPEN-C): `A(uv + d)` - one texture() fetch of
               // the PRE-SWAP state texture, which is the same texture the
               // ordinary state loads read, so the two can never disagree.
               // The boundary rule is applied to the coordinate, not the
               // result, so a wrapped fetch stays a single fetch.
               if (node->kind == IRKind::StateRead && node->isOffsetRead && !node->children.empty())
               {
                  std::string coord = EmitNode(node->children[0], ctx);
                  if (!ctx.error.empty()) return "";

                  BoundaryMode bm = BoundaryMode::Clamp;
                  auto bIt = ctx.stateBoundary.find(name);
                  if (bIt != ctx.stateBoundary.end()) bm = bIt->second;

                  int chan = 0;
                  auto cIt = ctx.stateChannel.find(name);
                  if (cIt != ctx.stateChannel.end()) chan = cIt->second;
                  const char* chanNames[] = { "r", "g", "b", "a" };

                  // Half a texel in, so a clamped fetch lands on the edge
                  // texel's centre rather than blending with whatever the
                  // sampler decides lives past it.
                  std::string half = "(0.5 / fld_res)";
                  std::string c;
                  if (bm == BoundaryMode::Wrap)
                     c = "fract(" + coord + ")";
                  else
                     c = "clamp(" + coord + ", " + half + ", vec2(1.0) - " + half + ")";

                  std::string fetch = "texture(fld_s_bank0, " + c + ")." + chanNames[chan];

                  if (bm == BoundaryMode::Border)
                  {
                     // Outside the frame the cell reads its declared initial
                     // value - the rule that makes a diffusion kernel decay
                     // into its background instead of smearing the edge.
                     float initV = 0.0f;
                     auto iIt = ctx.stateInit.find(name);
                     if (iIt != ctx.stateInit.end() && !iIt->second.empty()) initV = iIt->second[0];

                     std::string inside = "float(all(greaterThanEqual(" + coord + ", vec2(0.0))) && all(lessThanEqual(" + coord + ", vec2(1.0))))";
                     fetch = "mix(" + FormatFloat((double)initV) + ", " + fetch + ", " + inside + ")";
                  }

                  ctx.offsetReadCount++;
                  return "(" + fetch + ")";
               }

               if (name == "uv" || name == "xy" || name == "res" || name == "aspect" ||
                   name == "col" || name == "alpha" || name == "age")
               {
                  return name;
               }
               if (name == "t") return "fld_t";
               if (name == "dt") return "fld_dt";
               if (name == "frame") return "fld_frame";

               auto hit = ctx.hoistedMap.find(name);
               if (hit != ctx.hoistedMap.end())
               {
                  return "fld_h" + std::to_string(hit->second);
               }

               // A declared param is a uniform in its own namespace. Without
               // this the body referenced fld_v_<name> while the declaration
               // emitted fld_p_<name>, so every kernel using a param failed to
               // link with "undeclared identifier".
               if (ctx.paramNames.find(name) != ctx.paramNames.end())
               {
                  return "fld_p_" + name;
               }

               return "fld_v_" + name;
            }

            case IRKind::Access:
            {
               if (node->children.empty()) return "0.0";
               std::string base = EmitNode(node->children[0], ctx);
               std::string resTemp = ctx.FreshTemp();
               std::string typeStr = GlslTypeString(node->type);
               ctx.EmitLine("   " + typeStr + " " + resTemp + " = " + base + "." + node->field + ";");
               return resTemp;
            }

            case IRKind::Unary:
            {
               if (node->children.empty()) return "0.0";
               std::string opnd = EmitNode(node->children[0], ctx);
               std::string resTemp = ctx.FreshTemp();
               std::string typeStr = GlslTypeString(node->type);
               if (node->op == "-")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = -" + opnd + ";");
               }
               else if (node->op == "!")
               {
                  ctx.EmitLine("   float " + resTemp + " = (" + opnd + " == 0.0) ? 1.0 : 0.0;");
               }
               else
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = " + node->op + opnd + ";");
               }
               return resTemp;
            }

            case IRKind::Binary:
            {
               if (node->children.size() < 2) return "0.0";
               std::string lhs = EmitNode(node->children[0], ctx);
               std::string rhs = EmitNode(node->children[1], ctx);
               std::string resTemp = ctx.FreshTemp();
               std::string typeStr = GlslTypeString(node->type);

               const std::string& op = node->op;
               const int resLanes = node->type.lanes;
               const int lLanes = node->children[0]->type.lanes;
               const int rLanes = node->children[1]->type.lanes;
               if (op == "%")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_mod(" +
                               Broadcast(lhs, lLanes, resLanes) + ", " +
                               Broadcast(rhs, rLanes, resLanes) + ");");
               }
               else if (op == "^")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_pow(" +
                               Broadcast(lhs, lLanes, resLanes) + ", " +
                               Broadcast(rhs, rLanes, resLanes) + ");");
               }
               else if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=")
               {
                  int lhsLanes = node->children[0]->type.lanes;
                  if (lhsLanes == 1)
                  {
                     ctx.EmitLine("   float " + resTemp + " = (" + lhs + " " + op + " " + rhs + ") ? 1.0 : 0.0;");
                  }
                  else
                  {
                     std::string glslFunc;
                     if (op == "<") glslFunc = "lessThan";
                     else if (op == "<=") glslFunc = "lessThanEqual";
                     else if (op == ">") glslFunc = "greaterThan";
                     else if (op == ">=") glslFunc = "greaterThanEqual";
                     else if (op == "==") glslFunc = "equal";
                     else if (op == "!=") glslFunc = "notEqual";

                     std::string vType = "vec" + std::to_string(lhsLanes);
                     ctx.EmitLine("   " + vType + " " + resTemp + " = mix(" + vType + "(0.0), " + vType + "(1.0), " + glslFunc + "(" + lhs + ", " + rhs + "));");
                  }
               }
               else if (op == "&&")
               {
                  ctx.EmitLine("   float " + resTemp + " = (" + lhs + " != 0.0 && " + rhs + " != 0.0) ? 1.0 : 0.0;");
               }
               else if (op == "||")
               {
                  ctx.EmitLine("   float " + resTemp + " = (" + lhs + " != 0.0 || " + rhs + " != 0.0) ? 1.0 : 0.0;");
               }
               else
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = " + lhs + " " + op + " " + rhs + ";");
               }
               return resTemp;
            }

            case IRKind::Call:
            {
               const std::string& name = node->callee;
               std::vector<std::string> args;
               for (const auto& c : node->children)
               {
                  args.push_back(EmitNode(c, ctx));
               }

               std::string resTemp = ctx.FreshTemp();
               std::string typeStr = GlslTypeString(node->type);
               const int resLanes = node->type.lanes;

               // Every fld_ helper is declared only at matched rank, so a scalar
               // argument to a vector-valued call must be broadcast explicitly.
               auto bc = [&](size_t i) -> std::string {
                  if (i >= args.size() || i >= node->children.size()) return i < args.size() ? args[i] : "0.0";
                  return Broadcast(args[i], node->children[i]->type.lanes, resLanes);
               };

               if (name == "vec2" || name == "vec3" || name == "vec4")
               {
                  std::string argStr;
                  for (size_t i = 0; i < args.size(); i++)
                  {
                     if (i > 0) argStr += ", ";
                     argStr += args[i];
                  }
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = " + name + "(" + argStr + ");");
               }
               else if (name == "mod" || name == "fmod")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_mod(" + bc(0) + ", " + bc(1) + ");");
               }
               else if (name == "pow")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_pow(" + bc(0) + ", " + bc(1) + ");");
               }
               else if (name == "clamp")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_clamp(" + bc(0) + ", " + bc(1) + ", " + bc(2) + ");");
               }
               else if (name == "lerp" || name == "mix")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_lerp(" + bc(0) + ", " + bc(1) + ", " + bc(2) + ");");
               }
               else if (name == "smoothstep")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_smoothstep(" + bc(0) + ", " + bc(1) + ", " + bc(2) + ");");
               }
               else if (name == "round")
               {
                  if (node->type.lanes == 1)
                     ctx.EmitLine("   " + typeStr + " " + resTemp + " = floor(" + args[0] + " + 0.5);");
                  else
                     ctx.EmitLine("   " + typeStr + " " + resTemp + " = floor(" + args[0] + " + " + typeStr + "(0.5));");
               }
               else if (name == "atan2")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = atan(" + args[0] + ", " + args[1] + ");");
               }
               else if (name == "if")
               {
                  ctx.branchCount++;
                  // if(c, a, b) in Field -> mix(b, a, c != 0.0) in GLSL, using
                  // the BOOL-SELECTOR overload genType mix(genType, genType,
                  // genBType). The float overload is an arithmetic blend, so an
                  // inf/NaN in the unselected branch reaches the result as
                  // x * 1.0 + inf * 0.0 = NaN. Field's truth rule is "any
                  // non-zero is true", hence `!= 0.0` and not step(0.5, c).
                  // Argument order: mix returns its FIRST operand when the
                  // selector is false, so the false branch (args[2]) comes first.
                  const std::string sel = "(" + args[0] + " != 0.0)";
                  if (resLanes == 1)
                  {
                     ctx.EmitLine("   " + typeStr + " " + resTemp + " = mix(" + bc(2) + ", " + bc(1) + ", " + sel + ");");
                  }
                  else
                  {
                     ctx.EmitLine("   " + typeStr + " " + resTemp + " = mix(" + bc(2) + ", " + bc(1) + ", " +
                                  GlslBoolVecType(resLanes) + "(" + sel + "));");
                  }
               }
                else if (name.rfind("reduce.", 0) == 0 || node->transferKind == TransferKind::Reduce)
                {
                   ctx.error = "reduce is not lowerable in GLSL — it must arrive as a uniform from CPU reduction";
                   return "";
                }
               else
               {
                  // Standard direct built-ins: sin, cos, tan, abs, floor, ceil, fract, min, max, sign, step, sqrt, length, dot, cross, normalize, distance, exp, log
                  std::string argStr;
                  for (size_t i = 0; i < args.size(); i++)
                  {
                     if (i > 0) argStr += ", ";
                     argStr += args[i];
                  }
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = " + name + "(" + argStr + ");");
               }

               return resTemp;
            }
         }

         return "0.0";
      }

      void EmitStmt(const IRStmtPtr& stmt, EmitterContext& ctx)
      {
         if (!ctx.error.empty() || !stmt) return;

         switch (stmt->kind)
         {
            case IRStmtKind::Assign:
            {
               std::string rVal = EmitNode(stmt->rvalueExpr, ctx);
               const std::string& target = stmt->assignTarget;

               if (target == "col")
               {
                  if (stmt->assignField.empty())
                  {
                     if (stmt->rvalueExpr && stmt->rvalueExpr->type.lanes == 1)
                        ctx.EmitLine("   col = vec3(" + rVal + ");");
                     else
                        ctx.EmitLine("   col = " + rVal + ";");
                  }
                  else
                  {
                     ctx.EmitLine("   col." + stmt->assignField + " " + stmt->assignOp + " " + rVal + ";");
                  }
               }
               else if (target == "alpha")
               {
                  ctx.EmitLine("   alpha " + stmt->assignOp + " " + rVal + ";");
               }
               else
               {
                  std::string varName = (ctx.stateNames.find(target) != ctx.stateNames.end()) ? ("fld_v_" + target) : ("fld_v_" + target);
                  if (stmt->assignField.empty())
                  {
                     if (ctx.declaredLocals.find(target) == ctx.declaredLocals.end() && ctx.stateNames.find(target) == ctx.stateNames.end())
                     {
                        std::string typeStr = GlslTypeString(stmt->rvalueExpr ? stmt->rvalueExpr->type : FieldType(DataType::Float, 1));
                        ctx.EmitLine("   " + typeStr + " " + varName + " = " + rVal + ";");
                        ctx.declaredLocals.insert(target);
                     }
                     else
                     {
                        ctx.EmitLine("   " + varName + " " + stmt->assignOp + " " + rVal + ";");
                     }
                  }
                  else
                  {
                     ctx.EmitLine("   " + varName + "." + stmt->assignField + " " + stmt->assignOp + " " + rVal + ";");
                  }
               }
               break;
            }

            case IRStmtKind::StateWrite:
            {
               // State cell final value is already in fld_v_<name>
               break;
            }

            case IRStmtKind::Expr:
            {
               if (stmt->expr)
               {
                  EmitNode(stmt->expr, ctx);
               }
               break;
            }

            case IRStmtKind::If:
            {
               std::string cond = EmitNode(stmt->ifCond, ctx);
               ctx.EmitLine("   if (" + cond + " != 0.0) {");
               for (const auto& s : stmt->thenStmts) EmitStmt(s, ctx);
               if (!stmt->elseStmts.empty())
               {
                  ctx.EmitLine("   } else {");
                  for (const auto& s : stmt->elseStmts) EmitStmt(s, ctx);
               }
               ctx.EmitLine("   }");
               break;
            }

            case IRStmtKind::For:
            {
               if (stmt->forInit) EmitStmt(stmt->forInit, ctx);
               std::string cond = stmt->forCond ? EmitNode(stmt->forCond, ctx) : "true";
               ctx.EmitLine("   while (" + cond + " != 0.0) {");
               for (const auto& b : stmt->forBody) EmitStmt(b, ctx);
               if (stmt->forStep) EmitStmt(stmt->forStep, ctx);
               ctx.EmitLine("   }");
               break;
            }

            case IRStmtKind::DeclAttrib:
            case IRStmtKind::DeclState:
               break;
         }
      }
   }

   GlslEmitResult EmitGlsl(const PixelIRProgram& program)
   {
      GlslEmitResult result;

      auto hasReduce = [](const auto& self, const IRNodePtr& node) -> bool {
         if (!node) return false;
         if (node->transferKind == TransferKind::Reduce || node->callee.rfind("reduce.", 0) == 0)
            return true;
         for (const auto& c : node->children)
            if (self(self, c)) return true;
         return false;
      };

      for (const auto& s : program.pixelBody)
      {
         if (s->rvalueExpr && hasReduce(hasReduce, s->rvalueExpr))
         {
            result.error = "reduce is not lowerable in GLSL; a reduction must arrive as a uniform";
            return result;
         }
      }

      EmitterContext ctx;

      // 1. Version header
      ctx.EmitLine("#version 150\n");

      // 2. Vertex inputs & fragment outputs
      ctx.EmitLine("in  vec2 vUv;");
      ctx.EmitLine("out vec4 fragColor;\n");

      // 3. Fixed internals
      ctx.EmitLine("// ---- fixed internals (always emitted, even if unused; the compiler DCEs them)");
      ctx.EmitLine("uniform vec2      fld_res;      // output resolution in pixels");
      ctx.EmitLine("uniform float     fld_t;        // transport seconds, from the frame domain");
      ctx.EmitLine("uniform float     fld_dt;       // seconds since previous cook");
      ctx.EmitLine("uniform int       fld_frame;    // monotonic frame id");
      // Build step 22: cooks since this node's state was last cleared. A
      // simulation needs a one-shot seed, and `frame` cannot give it - that
      // counter is global and is already in the thousands by the time a node
      // is spawned. `age` is 0 on the first cook after a clear or a transport
      // reset, so `step(0.5, age)` is "not the first cook".
      ctx.EmitLine("uniform float     fld_age;      // cooks since this node's state was cleared");
      ctx.EmitLine("uniform sampler2D fld_srcTex;   // input image, or a 1x1 black texture when unconnected");
      ctx.EmitLine("uniform float     fld_srcAlpha; // 1.0 when connected, 0.0 when not\n");

      // 4. Divergences policy comment block
      ctx.EmitLine("// ---- divergences policy vs CPU VM ----");
      ctx.EmitLine("// - Division by zero yields ±inf");
      ctx.EmitLine("// - fld_mod(x, 0.0) yields NaN");
      ctx.EmitLine("// - sqrt(x) for x < 0 yields NaN");
      ctx.EmitLine("// - log(x) for x <= 0 is undefined");
      ctx.EmitLine("// - fld_pow(neg, non-integer) returns 0.0 / 0.0 (NaN)\n");

      // 5. Helper prelude
      ctx.EmitLine(kFieldHelperPrelude);

      // 6. Hoisted uniforms from prologue
      if (!program.prologue.empty())
      {
         ctx.EmitLine("// ---- hoisted frame-domain values, as uniforms ----");
         for (size_t i = 0; i < program.prologue.size(); i++)
         {
            const auto& s = program.prologue[i];
            std::string uName = "fld_h" + std::to_string(i);
            FieldType ft = s->rvalueExpr ? s->rvalueExpr->type : FieldType(DataType::Float, 1);
            std::string typeStr = GlslTypeString(ft);
            ctx.EmitLine("uniform " + typeStr + " " + uName + ";");

            UniformSlot slot;
            slot.name = uName;
            slot.varName = s->assignTarget;
            slot.type = ft;
            slot.domain = Domain::Frame;
            slot.hoistedIndex = (int)i;
            result.uniforms.push_back(slot);

            ctx.hoistedMap[s->assignTarget] = (int)i;
         }
         ctx.EmitLine("");
      }

      // 7. Step 5 Param uniforms
      if (!program.declaredParams.empty())
      {
         ctx.EmitLine("// ---- step-5 params ----");
         for (size_t i = 0; i < program.declaredParams.size(); i++)
         {
            const auto& p = program.declaredParams[i];
            std::string uName = "fld_p_" + p.name;
            ctx.EmitLine("uniform float " + uName + ";");

            UniformSlot slot;
            slot.name = uName;
            slot.varName = p.name;
            slot.type = FieldType(DataType::Float, 1);
            slot.domain = Domain::Graph;
            slot.paramIndex = (int)i;
            result.uniforms.push_back(slot);

            ctx.paramNames.insert(p.name);
         }
         ctx.EmitLine("");
      }

      // 8. Pixel state samplers
      if (!program.declaredStates.empty())
      {
         ctx.EmitLine("// ---- pixel state read samplers ----");
         ctx.EmitLine("uniform sampler2D fld_s_bank0;");
         // One color attachment per pass (T12), so a state kernel needs two
         // passes: one writing the cells into the ping-pong target, one writing
         // `col` into the display FBO. Both read the same pre-swap state
         // texture, so they never disagree. The selector is frame-uniform, so
         // the branch costs nothing on the GPU.
         ctx.EmitLine("uniform int       fld_outMode;   // 0 = write state cells, 1 = write display colour\n");

         for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
         {
            const auto& st = program.declaredStates[i];
            StateSlot sslot;
            sslot.name = st.name;
            sslot.type = st.type;
            sslot.lanes = st.lanes;
            sslot.bankIndex = 0;
            sslot.channel = (int)i;
            sslot.initialValues = st.initialValues;
            result.state.push_back(sslot);
            ctx.stateNames.insert(st.name);
            ctx.stateChannel[st.name] = (int)i;
            ctx.stateBoundary[st.name] = st.boundary;
            ctx.stateInit[st.name] = st.initialValues;
         }
      }

      // 9. main function
      ctx.EmitLine("void main()");
      ctx.EmitLine("{");
      ctx.EmitLine("   // ---- reserved-name bindings ----");
      ctx.EmitLine("   vec2  uv  = vUv;");
      ctx.EmitLine("   vec2  xy  = gl_FragCoord.xy;");
      ctx.EmitLine("   vec2  res = fld_res;");
      ctx.EmitLine("   float age = fld_age;");
      ctx.EmitLine("   float aspect = fld_res.x / fld_res.y;");
      ctx.EmitLine("   vec4  src = texture(fld_srcTex, vUv);");
      ctx.EmitLine("   vec3  col = src.rgb;");
      // Unconnected src is a 1x1 black texture with alpha 0 (fld_srcAlpha
      // tracks that), so `alpha = src.a` alone made every standalone
      // FieldPixelNode (no upstream input) render fully transparent by
      // default - a kernel that only ever writes `col` (every built-in
      // preset does) produced a correctly-computed image nobody could see.
      // Default to opaque when unconnected; a real upstream still passes
      // its own alpha through untouched.
      ctx.EmitLine("   float alpha = mix(1.0, src.a, fld_srcAlpha);\n");

      // 10. State loads
      if (!program.declaredStates.empty())
      {
         ctx.EmitLine("   // ---- state loads ----");
         ctx.EmitLine("   vec4  fld_st0 = texelFetch(fld_s_bank0, ivec2(gl_FragCoord.xy), 0);");
         const char* chanNames[] = { "r", "g", "b", "a" };
         for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
         {
            const auto& st = program.declaredStates[i];
            std::string typeStr = GlslTypeString(FieldType(st.type, st.lanes));
            ctx.EmitLine("   " + typeStr + " fld_v_" + st.name + " = fld_st0." + chanNames[i] + ";");
         }
         ctx.EmitLine("");
      }

      // 11. Statements in pixel body
      ctx.EmitLine("   // ---- SSA body ----");
      for (const auto& s : program.pixelBody)
      {
         EmitStmt(s, ctx);
         if (!ctx.error.empty())
         {
            result.error = ctx.error;
            return result;
         }
      }

      // 12. Writes
      ctx.EmitLine("\n   // ---- writes ----");
      if (!program.declaredStates.empty())
      {
         std::string chans[4] = { "0.0", "0.0", "0.0", "0.0" };
         for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
         {
            chans[i] = "fld_v_" + program.declaredStates[i].name;
         }
         ctx.EmitLine("   // cell -> channel by declaration order: 0=.r 1=.g 2=.b 3=.a");
         ctx.EmitLine("   vec4 fld_stateOut = vec4(" + chans[0] + ", " + chans[1] + ", " + chans[2] + ", " + chans[3] + ");");
         ctx.EmitLine("   vec4 fld_colorOut = vec4(col, alpha);");
         ctx.EmitLine("   fragColor = (fld_outMode == 0) ? fld_stateOut : fld_colorOut;");
      }
      else
      {
         ctx.EmitLine("   fragColor = vec4(col, alpha);");
      }

      ctx.EmitLine("}");

      result.offsetReadCount = ctx.offsetReadCount;
      result.usesOffsetReads = ctx.offsetReadCount > 0;

      result.source = ctx.out.str();
      result.branchCount = ctx.branchCount;
      result.lineToIrNode = ctx.lineToIrNode;
      return result;
   }
}

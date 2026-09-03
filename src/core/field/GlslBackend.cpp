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
            if (t.kind == DataType::Bool) return "bool";
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

      struct EmitterContext
      {
         std::ostringstream out;
         int tempCounter = 0;
         int branchCount = 0;
         std::unordered_set<std::string> declaredLocals;
         std::unordered_map<std::string, int> hoistedMap; // varName -> uniform index
         std::unordered_set<std::string> stateNames;
         std::vector<int> lineToIrNode;
         int currentLine = 1;

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
         if (!node) return "0.0";

         switch (node->kind)
         {
            case IRKind::Literal:
            {
               if (node->type.lanes == 1)
               {
                  if (node->type.kind == DataType::Bool)
                     return node->numberValue != 0.0 ? "true" : "false";
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
               if (name == "uv" || name == "xy" || name == "res" || name == "aspect" ||
                   name == "col" || name == "alpha")
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

               // Check if state cell
               if (ctx.stateNames.find(name) != ctx.stateNames.end())
               {
                  return "fld_v_" + name;
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
               if (op == "%")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_mod(" + lhs + ", " + rhs + ");");
               }
               else if (op == "^")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_pow(" + lhs + ", " + rhs + ");");
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
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_mod(" + args[0] + ", " + args[1] + ");");
               }
               else if (name == "pow")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_pow(" + args[0] + ", " + args[1] + ");");
               }
               else if (name == "clamp")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_clamp(" + args[0] + ", " + args[1] + ", " + args[2] + ");");
               }
               else if (name == "lerp" || name == "mix")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_lerp(" + args[0] + ", " + args[1] + ", " + args[2] + ");");
               }
               else if (name == "smoothstep")
               {
                  ctx.EmitLine("   " + typeStr + " " + resTemp + " = fld_smoothstep(" + args[0] + ", " + args[1] + ", " + args[2] + ");");
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
                  // if(c, a, b) in Field -> mix(b, a, cond) in GLSL
                  int lanes = node->type.lanes;
                  if (lanes == 1)
                  {
                     ctx.EmitLine("   " + typeStr + " " + resTemp + " = mix(" + args[2] + ", " + args[1] + ", (" + args[0] + " != 0.0) ? 1.0 : 0.0);");
                  }
                  else
                  {
                     ctx.EmitLine("   " + typeStr + " " + resTemp + " = mix(" + args[2] + ", " + args[1] + ", " + typeStr + "((" + args[0] + " != 0.0) ? 1.0 : 0.0));");
                  }
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
         if (!stmt) return;

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
         }
         ctx.EmitLine("");
      }

      // 8. Pixel state samplers
      if (!program.declaredStates.empty())
      {
         ctx.EmitLine("// ---- pixel state read samplers ----");
         ctx.EmitLine("uniform sampler2D fld_s_bank0;\n");

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
         }
      }

      // 9. main function
      ctx.EmitLine("void main()");
      ctx.EmitLine("{");
      ctx.EmitLine("   // ---- reserved-name bindings ----");
      ctx.EmitLine("   vec2  uv  = vUv;");
      ctx.EmitLine("   vec2  xy  = gl_FragCoord.xy;");
      ctx.EmitLine("   vec2  res = fld_res;");
      ctx.EmitLine("   float aspect = fld_res.x / fld_res.y;");
      ctx.EmitLine("   vec4  src = texture(fld_srcTex, vUv);");
      ctx.EmitLine("   vec3  col = src.rgb;");
      ctx.EmitLine("   float alpha = src.a;\n");

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
      }

      // 12. Writes
      ctx.EmitLine("\n   // ---- writes ----");
      if (!program.declaredStates.empty())
      {
         const char* chanNames[] = { "fld_v_cell0", "fld_v_cell1", "fld_v_cell2", "fld_v_cell3" };
         std::string chans[4] = { "0.0", "0.0", "0.0", "1.0" };
         for (size_t i = 0; i < program.declaredStates.size() && i < 4; i++)
         {
            chans[i] = "fld_v_" + program.declaredStates[i].name;
         }
         // If unused channels exist, let col / alpha pass
         if (program.declaredStates.size() == 1)
         {
            chans[1] = "col.g";
            chans[2] = "col.b";
            chans[3] = "alpha * fld_srcAlpha";
         }
         else if (program.declaredStates.size() == 2)
         {
            chans[2] = "col.b";
            chans[3] = "alpha * fld_srcAlpha";
         }
         else if (program.declaredStates.size() == 3)
         {
            chans[3] = "alpha * fld_srcAlpha";
         }

         ctx.EmitLine("   fragColor = vec4(" + chans[0] + ", " + chans[1] + ", " + chans[2] + ", " + chans[3] + ");");
      }
      else
      {
         ctx.EmitLine("   fragColor = vec4(col, alpha * fld_srcAlpha);");
      }

      ctx.EmitLine("}");

      result.source = ctx.out.str();
      result.branchCount = ctx.branchCount;
      result.lineToIrNode = ctx.lineToIrNode;
      return result;
   }
}

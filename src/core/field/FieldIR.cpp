#include "FieldIR.h"
#include "FieldSwizzle.h"
#include "FieldCycles.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Field
{
   Domain JoinDomains(Domain a, Domain b, bool& outCompatible)
   {
      outCompatible = true;
      if (a == b) return a;
      if (a == Domain::Graph) return b;
      if (b == Domain::Graph) return a;
      if (a == Domain::Frame) return b; // Frame is coarse subtype of Element/Pixel/Sample
      if (b == Domain::Frame) return a;

      // Element, Pixel, Sample are mutually incomparable
      outCompatible = false;
      return a;
   }

   namespace
   {
      bool IsVecConstructor(const std::string& name)
      {
         return name == "vec2" || name == "vec3" || name == "vec4";
      }

      int GetCtorLanes(const std::string& name)
      {
         if (name == "vec2") return 2;
         if (name == "vec3") return 3;
         if (name == "vec4") return 4;
         return 1;
      }

      bool ValidateFunction(const std::string& name, size_t argCount, FieldError& error, SourceSpan span)
      {
         auto checkExact = [&](size_t n) -> bool {
            if (argCount != n)
            {
               error.severity = Severity::Error;
               error.span = span;
               error.message = name + "() expects " + std::to_string(n) + " argument(s)";
               return false;
            }
            return true;
         };

         if (name == "sin" || name == "cos" || name == "tan" || name == "abs" ||
             name == "floor" || name == "ceil" || name == "round" || name == "sign" ||
             name == "exp" || name == "sqrt" || name == "log" || name == "fract" ||
             name == "length" || name == "normalize")
         {
            return checkExact(1);
         }
         if (name == "min" || name == "max" || name == "mod" || name == "fmod" ||
             name == "pow" || name == "step" || name == "distance" || name == "dot" ||
             name == "cross" || name == "atan2")
         {
            return checkExact(2);
         }
         if (name == "clamp" || name == "lerp" || name == "mix" || name == "smoothstep" || name == "if")
         {
            return checkExact(3);
         }
         if (name == "rand" || name == "noise" || name == "sh")
         {
            if (argCount > 4)
            {
               error.severity = Severity::Error;
               error.span = span;
               if (name == "sh")
                  error.message = "sh() expects 0 to 4 arguments (min, max, speed, seed)";
               else
                  error.message = name + "() expects 0 to 4 arguments (e.g. rand(speed) or rand(min, max, speed, seed))";
               return false;
            }
            return true;
         }

         error.severity = Severity::Error;
         error.span = span;
         error.message = "unknown function '" + name + "'";
         return false;
      }

      struct VarSymbol
      {
         std::string name;
         FieldType type;
         Domain domain = Domain::Graph;
         bool isAttrib = false;
         bool isParam = false;
         bool isState = false;
         bool isReserved = false;
         bool isReadOnly = false;
         // Seeded by LowerElementProgramToIR's pass-0 pre-scan purely so a forward
         // reference to a name defined later in the body still resolves and reaches
         // the cycle checker. A provisional symbol carries NO committed type and NO
         // committed domain: pinning them here was silently forcing every local to
         // (float, element), which killed both the prologue hoist and local type
         // inference (`c = vec3(1,0,0)` was rejected as "cannot assign vec3 to float").
         // The Assign lowering back-patches the symbol from the RHS and clears this.
         bool isProvisional = false;
      };

      struct ElementScope
      {
         std::unordered_map<std::string, VarSymbol> symbols;
         std::unordered_set<std::string> writtenStates;
         Domain targetDomain = Domain::Element;
         bool enforceDeclaration = false;

         bool Has(const std::string& name) const
         {
            return symbols.find(name) != symbols.end();
         }

         const VarSymbol* Find(const std::string& name) const
         {
            auto it = symbols.find(name);
            return it != symbols.end() ? &it->second : nullptr;
         }

         void Add(const VarSymbol& sym)
         {
            symbols[sym.name] = sym;
         }

         bool HasWrittenState(const std::string& name) const
         {
            return writtenStates.find(name) != writtenStates.end();
         }

         void MarkWrittenState(const std::string& name)
         {
            writtenStates.insert(name);
         }
      };

      IRNodePtr LowerAstExpr(const AstNodePtr& ast, ElementScope& scope, FieldError& error)
      {
         if (!ast || !error.Empty()) return nullptr;

         switch (ast->kind)
         {
            case AstKind::Literal:
            {
               auto lit = std::static_pointer_cast<AstLiteral>(ast);
               auto ir = std::make_shared<IRNode>(IRKind::Literal, lit->span);
               ir->type = lit->isBool ? FieldType(DataType::Bool, 1) : FieldType(DataType::Float, 1);
               ir->domain = Domain::Graph;
               ir->numberValue = lit->numberValue;
               ir->vecValues[0] = lit->numberValue;
               ir->vecValues[1] = 0.0;
               ir->vecValues[2] = 0.0;
               ir->vecValues[3] = 0.0;
               return ir;
            }

            case AstKind::Ident:
            {
               auto id = std::static_pointer_cast<AstIdent>(ast);
               const VarSymbol* sym = scope.Find(id->name);
               if (sym)
               {
                  if (sym->isState)
                  {
                     // If written earlier in this body, reads current SSA value;
                     // otherwise, reads StateRead (unit delay entry value)
                     IRKind k = scope.HasWrittenState(id->name) ? IRKind::Variable : IRKind::StateRead;
                     auto ir = std::make_shared<IRNode>(k, id->span);
                     ir->varName = id->name;
                     ir->type = sym->type;
                     ir->domain = sym->domain;
                     return ir;
                  }

                  auto ir = std::make_shared<IRNode>(IRKind::Variable, id->span);
                  ir->varName = id->name;
                  ir->type = sym->type;
                  ir->domain = sym->domain;
                  return ir;
               }
               else
               {
                  if (scope.enforceDeclaration)
                  {
                     error.severity = Severity::Error;
                     error.span = id->span;
                     error.message = "undeclared identifier '" + id->name + "' (hint: declare with 'attrib float " + id->name + " = 0')";
                     return nullptr;
                  }

                  // Default single-expression fallback
                  auto ir = std::make_shared<IRNode>(IRKind::Variable, id->span);
                  ir->varName = id->name;
                  ir->type = FieldType(DataType::Float, 1);
                  if (id->name == "t" || id->name == "dt" || id->name == "frame")
                     ir->domain = Domain::Frame;
                  else
                     ir->domain = Domain::Graph;
                  return ir;
               }
            }

            case AstKind::Access:
            {
               auto acc = std::static_pointer_cast<AstAccess>(ast);
               auto baseIR = LowerAstExpr(acc->base, scope, error);
               if (!baseIR) return nullptr;

               std::string baseName;
               if (baseIR->kind == IRKind::Variable)
                  baseName = baseIR->varName;

               SwizzleInfo swizzleInfo;
               std::string swizzleError;
               if (!ParseAndValidateSwizzle(acc->field, baseIR->type, baseName, swizzleInfo, swizzleError))
               {
                  error.severity = Severity::Error;
                  error.span = acc->span;
                  error.message = swizzleError;
                  return nullptr;
               }

               auto ir = std::make_shared<IRNode>(IRKind::Access, acc->span);
               ir->type = swizzleInfo.resultType;
               ir->domain = baseIR->domain;
               ir->field = acc->field;
               for (int i = 0; i < 4; ++i)
                  ir->swizzleIndices[i] = swizzleInfo.indices[i];

               // Constant fold swizzle if base is literal
               if (baseIR->kind == IRKind::Literal)
               {
                  ir->kind = IRKind::Literal;
                  for (int i = 0; i < swizzleInfo.numComponents; ++i)
                     ir->vecValues[i] = baseIR->vecValues[swizzleInfo.indices[i]];
                  ir->numberValue = ir->vecValues[0];
                  return ir;
               }

               ir->children.push_back(baseIR);
               return ir;
            }

            case AstKind::Unary:
            {
               auto un = std::static_pointer_cast<AstUnary>(ast);
               auto operandIR = LowerAstExpr(un->operand, scope, error);
               if (!operandIR) return nullptr;

               if (un->op == "!")
               {
                  if (operandIR->type.lanes > 1)
                  {
                     error.severity = Severity::Error;
                     error.span = un->span;
                     error.message = "operator '!' requires scalar argument (got " + std::string(operandIR->type.ToString()) + ")";
                     return nullptr;
                  }

                  auto ir = std::make_shared<IRNode>(IRKind::Unary, un->span);
                  ir->op = "!";
                  ir->type = FieldType(DataType::Bool, 1);
                  ir->domain = operandIR->domain;
                  ir->children.push_back(operandIR);

                  if (operandIR->kind == IRKind::Literal)
                  {
                     ir->kind = IRKind::Literal;
                     ir->numberValue = (operandIR->numberValue != 0.0) ? 0.0 : 1.0;
                     ir->vecValues[0] = ir->numberValue;
                     ir->children.clear();
                  }
                  return ir;
               }

               // Component-wise unary minus: -T -> T
               auto ir = std::make_shared<IRNode>(IRKind::Unary, un->span);
               ir->op = un->op;
               ir->type = operandIR->type;
               ir->domain = operandIR->domain;
               ir->children.push_back(operandIR);

               if (operandIR->kind == IRKind::Literal)
               {
                  ir->kind = IRKind::Literal;
                  for (int i = 0; i < ir->type.lanes; ++i)
                     ir->vecValues[i] = -operandIR->vecValues[i];
                  ir->numberValue = ir->vecValues[0];
                  ir->children.clear();
               }
               return ir;
            }

            case AstKind::Binary:
            {
               auto bin = std::static_pointer_cast<AstBinary>(ast);
               auto lhsIR = LowerAstExpr(bin->lhs, scope, error);
               if (!lhsIR) return nullptr;
               auto rhsIR = LowerAstExpr(bin->rhs, scope, error);
               if (!rhsIR) return nullptr;

               bool compatible = true;
               Domain joinedDomain = JoinDomains(lhsIR->domain, rhsIR->domain, compatible);
               if (!compatible)
               {
                  error.severity = Severity::Error;
                  error.span = bin->span;
                  error.message = "incomparable domains in binary operation";
                  return nullptr;
               }

               bool isComp = (bin->op == "<" || bin->op == "<=" || bin->op == ">" ||
                              bin->op == ">=" || bin->op == "==" || bin->op == "!=");
               bool isLogical = (bin->op == "&&" || bin->op == "||");

               if (isComp)
               {
                  if (lhsIR->type.lanes > 1 || rhsIR->type.lanes > 1)
                  {
                     error.severity = Severity::Error;
                     error.span = bin->span;
                     error.message = "operator '" + bin->op + "' requires scalar arguments (got " +
                                     std::string(lhsIR->type.ToString()) + " and " +
                                     std::string(rhsIR->type.ToString()) +
                                     "; hint: take a component, e.g. P.x > 0.5)";
                     return nullptr;
                  }

                  auto ir = std::make_shared<IRNode>(IRKind::Binary, bin->span);
                  ir->op = bin->op;
                  ir->type = FieldType(DataType::Bool, 1);
                  ir->domain = joinedDomain;
                  ir->children.push_back(lhsIR);
                  ir->children.push_back(rhsIR);
                  return ir;
               }

               if (isLogical)
               {
                  if (lhsIR->type.lanes > 1 || rhsIR->type.lanes > 1)
                  {
                     error.severity = Severity::Error;
                     error.span = bin->span;
                     error.message = "operator '" + bin->op + "' requires scalar arguments";
                     return nullptr;
                  }

                  auto ir = std::make_shared<IRNode>(IRKind::Binary, bin->span);
                  ir->op = bin->op;
                  ir->type = FieldType(DataType::Bool, 1);
                  ir->domain = joinedDomain;
                  ir->children.push_back(lhsIR);
                  ir->children.push_back(rhsIR);
                  return ir;
               }

               // Binary arithmetic (+, -, *, /, %, ^)
               FieldType resultType;
               std::string rankError;
               if (!JoinRank(lhsIR->type, rhsIR->type, resultType, rankError))
               {
                  error.severity = Severity::Error;
                  error.span = bin->span;
                  error.message = rankError;
                  return nullptr;
               }

               auto ir = std::make_shared<IRNode>(IRKind::Binary, bin->span);
               ir->op = bin->op;
               ir->type = resultType;
               ir->domain = joinedDomain;
               ir->children.push_back(lhsIR);
               ir->children.push_back(rhsIR);
               return ir;
            }

            case AstKind::Call:
            {
               auto call = std::static_pointer_cast<AstCall>(ast);

               // Vector constructors: vec2(...), vec3(...), vec4(...)
               if (IsVecConstructor(call->callee))
               {
                  int targetLanes = GetCtorLanes(call->callee);
                  if (call->args.empty())
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = call->callee + "() needs 1 or " + std::to_string(targetLanes) + " components";
                     return nullptr;
                  }

                  std::vector<IRNodePtr> argIRs;
                  Domain ctorDomain = Domain::Graph;
                  int totalLanes = 0;
                  bool allLiterals = true;

                  for (const auto& arg : call->args)
                  {
                     auto aIR = LowerAstExpr(arg, scope, error);
                     if (!aIR) return nullptr;

                     bool compatible = true;
                     ctorDomain = JoinDomains(ctorDomain, aIR->domain, compatible);
                     if (!compatible)
                     {
                        error.severity = Severity::Error;
                        error.span = call->span;
                        error.message = "incomparable domain arguments in vector constructor";
                        return nullptr;
                     }

                     totalLanes += aIR->type.lanes;
                     if (aIR->kind != IRKind::Literal)
                        allLiterals = false;

                     argIRs.push_back(aIR);
                  }

                  if (call->args.size() == 1)
                  {
                     if (argIRs[0]->type.lanes != 1)
                     {
                        error.severity = Severity::Error;
                        error.span = call->span;
                        error.message = "cannot construct " + call->callee + " from " +
                                        std::string(argIRs[0]->type.ToString()) +
                                        " (expected scalar splat or arguments summing to " +
                                        std::to_string(targetLanes) + " lanes)";
                        return nullptr;
                     }
                  }
                  else
                  {
                     if (totalLanes != targetLanes)
                     {
                        error.severity = Severity::Error;
                        error.span = call->span;
                        error.message = "cannot construct " + call->callee + ": argument components sum to " +
                                        std::to_string(totalLanes) + " (expected " +
                                        std::to_string(targetLanes) + ")";
                        return nullptr;
                     }
                  }

                  FieldType resType(FieldType::FromLanes(targetLanes), targetLanes);
                  auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
                  ir->callee = call->callee;
                  ir->type = resType;
                  ir->domain = ctorDomain;
                  ir->children = argIRs;

                  if (allLiterals)
                  {
                     ir->kind = IRKind::Literal;
                     if (call->args.size() == 1)
                     {
                        double splatVal = argIRs[0]->vecValues[0];
                        for (int l = 0; l < targetLanes; ++l)
                           ir->vecValues[l] = splatVal;
                     }
                     else
                     {
                        int outIdx = 0;
                        for (const auto& a : argIRs)
                        {
                           for (int l = 0; l < a->type.lanes && outIdx < targetLanes; ++l)
                              ir->vecValues[outIdx++] = a->vecValues[l];
                        }
                     }
                     ir->numberValue = ir->vecValues[0];
                     ir->children.clear();
                  }

                  return ir;
               }

               // Built-in functions
               if (!ValidateFunction(call->callee, call->args.size(), error, call->span))
               {
                  return nullptr;
               }

               std::vector<IRNodePtr> argIRs;
               Domain fnDomain = Domain::Graph;
               if (call->callee == "rand" || call->callee == "noise" || call->callee == "sh")
                  fnDomain = Domain::Frame;

               for (const auto& arg : call->args)
               {
                  auto aIR = LowerAstExpr(arg, scope, error);
                  if (!aIR) return nullptr;

                  bool compatible = true;
                  fnDomain = JoinDomains(fnDomain, aIR->domain, compatible);
                  if (!compatible)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "incomparable domain arguments in function call";
                     return nullptr;
                  }
                  argIRs.push_back(aIR);
               }

               FieldType fnResultType = FieldType(DataType::Float, 1);

               if (call->callee == "sin" || call->callee == "cos" || call->callee == "tan" ||
                   call->callee == "abs" || call->callee == "floor" || call->callee == "ceil" ||
                   call->callee == "round" || call->callee == "sign" || call->callee == "exp" ||
                   call->callee == "sqrt" || call->callee == "log" || call->callee == "fract" ||
                   call->callee == "normalize")
               {
                  fnResultType = argIRs[0]->type;
               }
               else if (call->callee == "length")
               {
                  fnResultType = FieldType(DataType::Float, 1);
               }
               else if (call->callee == "distance" || call->callee == "dot")
               {
                  fnResultType = FieldType(DataType::Float, 1);
               }
               else if (call->callee == "cross")
               {
                  fnResultType = FieldType(DataType::Vec3, 3);
               }
               else if (call->callee == "min" || call->callee == "max" ||
                        call->callee == "mod" || call->callee == "fmod" ||
                        call->callee == "pow" || call->callee == "step" ||
                        call->callee == "atan2")
               {
                  std::string rankErr;
                  if (!JoinRank(argIRs[0]->type, argIRs[1]->type, fnResultType, rankErr))
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = rankErr;
                     return nullptr;
                  }
               }
               else if (call->callee == "clamp" || call->callee == "smoothstep")
               {
                  FieldType j1, j2;
                  std::string rankErr;
                  if (!JoinRank(argIRs[0]->type, argIRs[1]->type, j1, rankErr) ||
                      !JoinRank(j1, argIRs[2]->type, j2, rankErr))
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = rankErr;
                     return nullptr;
                  }
                  fnResultType = j2;
               }
               else if (call->callee == "lerp" || call->callee == "mix")
               {
                  FieldType abType;
                  std::string rankErr;
                  if (!JoinRank(argIRs[0]->type, argIRs[1]->type, abType, rankErr) ||
                      !JoinRank(abType, argIRs[2]->type, fnResultType, rankErr))
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = rankErr;
                     return nullptr;
                  }
               }
               else if (call->callee == "if")
               {
                  if (argIRs[0]->type.lanes > 1)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "if() condition must be a scalar (got " + std::string(argIRs[0]->type.ToString()) + ")";
                     return nullptr;
                  }
                  std::string rankErr;
                  if (!JoinRank(argIRs[1]->type, argIRs[2]->type, fnResultType, rankErr))
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = rankErr;
                     return nullptr;
                  }
               }
               else if (call->callee == "rand" || call->callee == "noise" || call->callee == "sh")
               {
                  for (const auto& a : argIRs)
                  {
                     if (a->type.lanes > 1)
                     {
                        error.severity = Severity::Error;
                        error.span = call->span;
                        error.message = call->callee + "() arguments must be scalars (got " + std::string(a->type.ToString()) + ")";
                        return nullptr;
                     }
                  }
                  fnResultType = FieldType(DataType::Float, 1);
               }

               auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
               ir->callee = call->callee;
               ir->type = fnResultType;
               ir->domain = fnDomain;
               ir->children = argIRs;
               return ir;
            }

            case AstKind::Program:
            {
               auto prog = std::static_pointer_cast<AstProgram>(ast);
               if (prog->statements.empty())
               {
                  error.severity = Severity::Error;
                  error.message = "empty expression";
                  return nullptr;
               }
               return LowerAstExpr(prog->statements.front(), scope, error);
            }

            default:
               error.severity = Severity::Error;
               error.message = "unsupported AST node in expression evaluation";
               return nullptr;
         }
      }

      DataType ParseDataType(const std::string& name)
      {
         if (name == "float") return DataType::Float;
         if (name == "int") return DataType::Int;
         if (name == "bool") return DataType::Bool;
         if (name == "vec2") return DataType::Vec2;
         if (name == "vec3") return DataType::Vec3;
         if (name == "vec4") return DataType::Vec4;
         return DataType::Void;
      }

      IRStmtPtr LowerAstStmt(const AstNodePtr& ast, ElementScope& scope, ElementIRProgram& prog, FieldError& error)
      {
         if (!ast || !error.Empty()) return nullptr;

         switch (ast->kind)
         {
            case AstKind::DeclAttrib:
            {
               auto decl = std::static_pointer_cast<AstDeclAttrib>(ast);

               // Check reserved name collision
               if (decl->name == "P" || decl->name == "N" || decl->name == "uv" ||
                   decl->name == "Cd" || decl->name == "i" || decl->name == "count")
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'" + decl->name + "' is a reserved word of the element domain";
                  return nullptr;
               }
               if (decl->name == "t" || decl->name == "dt" || decl->name == "frame")
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'" + decl->name + "' is a reserved word of the frame domain";
                  return nullptr;
               }

               if (scope.Has(decl->name))
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "duplicate declaration of attribute '" + decl->name + "'";
                  return nullptr;
               }

               DataType dt = ParseDataType(decl->typeName);
               if (dt == DataType::Void)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "unknown type '" + decl->typeName + "' in attribute declaration";
                  return nullptr;
               }

               FieldType ft(dt, FieldType::GetLanesForType(dt));
               IRNodePtr initIR = nullptr;
               std::vector<float> initVals(ft.lanes, 0.0f);

               if (decl->initExpr)
               {
                  initIR = LowerAstExpr(decl->initExpr, scope, error);
                  if (!initIR) return nullptr;

                  if (initIR->domain != Domain::Graph && initIR->kind != IRKind::Literal)
                  {
                     error.severity = Severity::Error;
                     error.span = decl->initExpr->span;
                     error.message = "attribute initial value must be a constant expression";
                     return nullptr;
                  }

                  if (initIR->kind == IRKind::Literal)
                  {
                     if (initIR->type.lanes == 1)
                     {
                        for (int l = 0; l < ft.lanes; ++l)
                           initVals[l] = (float)initIR->vecValues[0];
                     }
                     else
                     {
                        for (int l = 0; l < ft.lanes && l < initIR->type.lanes; ++l)
                           initVals[l] = (float)initIR->vecValues[l];
                     }
                  }
               }

               VarSymbol sym;
               sym.name = decl->name;
               sym.type = ft;
               sym.domain = Domain::Element;
               sym.isAttrib = true;
               scope.Add(sym);

               prog.declaredAttribs.push_back({ decl->name, dt });

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::DeclAttrib, decl->span);
               stmt->domain = Domain::Element;
               stmt->attribName = decl->name;
               stmt->attribType = dt;
               stmt->attribInitValues = initVals;
               stmt->attribInitExpr = initIR;
               return stmt;
            }

            case AstKind::DeclParam:
            {
               auto decl = std::static_pointer_cast<AstDeclParam>(ast);

               // Check reserved name collision
               if (decl->name == "P" || decl->name == "N" || decl->name == "uv" ||
                   decl->name == "Cd" || decl->name == "i" || decl->name == "count")
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'" + decl->name + "' is a reserved word of the element domain";
                  return nullptr;
               }
               if (decl->name == "t" || decl->name == "dt" || decl->name == "frame")
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'" + decl->name + "' is a reserved word of the frame domain";
                  return nullptr;
               }

               if (scope.Has(decl->name))
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "duplicate declaration of param '" + decl->name + "'";
                  return nullptr;
               }

               if (prog.declaredParams.size() >= 128)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "param count exceeds kMaxParams (128) in src/audio/ParamMailbox.h";
                  return nullptr;
               }

               DeclaredParam dp;
               dp.name = decl->name;
               dp.typeName = "float";
               dp.defaultValue = decl->defaultValue;
               dp.minValue = decl->minVal;
               dp.maxValue = decl->maxVal;
               prog.declaredParams.push_back(dp);

               VarSymbol sym;
               sym.name = decl->name;
               sym.type = FieldType(DataType::Float, 1);
               sym.domain = Domain::Graph; // Seeds graph domain
               sym.isParam = true;
               sym.isReadOnly = true;
               scope.Add(sym);

               return nullptr;
            }

            case AstKind::DeclState:
            {
               auto decl = std::static_pointer_cast<AstDeclState>(ast);

               if (scope.Has(decl->name))
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "duplicate declaration of '" + decl->name + "'";
                  return nullptr;
               }

               DataType dt = DataType::Float;
               int lanes = 1;
               if (decl->typeName == "float") { dt = DataType::Float; lanes = 1; }
               else if (decl->typeName == "int") { dt = DataType::Int; lanes = 1; }
               else if (decl->typeName == "bool") { dt = DataType::Bool; lanes = 1; }
               else if (decl->typeName == "vec2") { dt = DataType::Vec2; lanes = 2; }
               else if (decl->typeName == "vec3") { dt = DataType::Vec3; lanes = 3; }
               else if (decl->typeName == "vec4") { dt = DataType::Vec4; lanes = 4; }

               std::vector<float> initVals(lanes, 0.0f);
               if (decl->initExpr)
               {
                  auto initIR = LowerAstExpr(decl->initExpr, scope, error);
                  if (!initIR) return nullptr;
                  if (initIR->kind == IRKind::Literal)
                  {
                     for (int i = 0; i < lanes; ++i)
                        initVals[i] = (float)initIR->vecValues[i];
                     if (lanes == 1) initVals[0] = (float)initIR->numberValue;
                  }
               }

               DeclaredState ds;
               ds.name = decl->name;
               ds.typeName = decl->typeName;
               ds.type = dt;
               ds.lanes = lanes;
               ds.initialValues = initVals;
               ds.domain = scope.targetDomain;
               ds.span = decl->span;
               prog.declaredStates.push_back(ds);

               VarSymbol sym;
               sym.name = decl->name;
               sym.type = FieldType(dt, lanes);
               sym.domain = scope.targetDomain;
               sym.isState = true;
               scope.Add(sym);

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::DeclState, decl->span);
               stmt->domain = scope.targetDomain;
               stmt->stateName = decl->name;
               stmt->stateTypeName = decl->typeName;
               stmt->stateType = dt;
               stmt->stateLanes = lanes;
               stmt->stateInitValues = initVals;
               return stmt;
            }

            case AstKind::Assign:
            {
               auto assign = std::static_pointer_cast<AstAssign>(ast);

               // Evaluate LValue target
               std::string targetName;
               std::string targetField;
               uint8_t swizzleIndices[4] = { 0, 0, 0, 0 };
               int swizzleCount = 0;
               FieldType lvalType;
               Domain lvalDomain = Domain::Graph;

               if (assign->lvalue->kind == AstKind::Ident)
               {
                  auto id = std::static_pointer_cast<AstIdent>(assign->lvalue);
                  targetName = id->name;

                  // Reserved word shadowing check for newly created locals:
                  // If id is reserved in another domain or read-only
                  if (targetName == "i" || targetName == "count" ||
                      targetName == "t" || targetName == "dt" || targetName == "frame")
                  {
                     error.severity = Severity::Error;
                     error.span = id->span;
                     error.message = "cannot assign to read-only variable '" + targetName + "'";
                     return nullptr;
                  }

                  const VarSymbol* sym = scope.Find(targetName);
                  // A provisional symbol is a pass-0 placeholder, not a definition. Falling
                  // into the inference branch below is what lets the local take its type and
                  // domain from the RHS - and what makes `heat += 0.5` with no declaration
                  // the compile error it is supposed to be instead of a silent zero read.
                  if (sym && sym->isProvisional)
                     sym = nullptr;
                  if (sym)
                  {
                     if (sym->isReadOnly)
                     {
                        error.severity = Severity::Error;
                        error.span = id->span;
                        error.message = "cannot assign to read-only variable '" + targetName + "'";
                        return nullptr;
                     }
                     lvalType = sym->type;
                     lvalDomain = sym->domain;
                  }
                  else
                  {
                     if (assign->op != "=")
                     {
                        error.severity = Severity::Error;
                        error.span = id->span;
                        error.message = "use of undeclared identifier '" + targetName + "'";
                        return nullptr;
                     }
                     // Local variable declaration via assignment
                     lvalType = FieldType(DataType::Void, 0); // inferred from RHS
                     lvalDomain = Domain::Graph;
                  }
               }
               else if (assign->lvalue->kind == AstKind::Access)
               {
                  auto acc = std::static_pointer_cast<AstAccess>(assign->lvalue);
                  if (acc->base->kind != AstKind::Ident)
                  {
                     error.severity = Severity::Error;
                     error.span = acc->span;
                     error.message = "invalid assignment target";
                     return nullptr;
                  }
                  auto baseId = std::static_pointer_cast<AstIdent>(acc->base);
                  targetName = baseId->name;
                  targetField = acc->field;

                  if (targetName == "i" || targetName == "count" ||
                      targetName == "t" || targetName == "dt" || targetName == "frame")
                  {
                     error.severity = Severity::Error;
                     error.span = baseId->span;
                     error.message = "cannot assign to read-only variable '" + targetName + "'";
                     return nullptr;
                  }

                  const VarSymbol* sym = scope.Find(targetName);
                  if (!sym)
                  {
                     error.severity = Severity::Error;
                     error.span = baseId->span;
                     error.message = "undeclared identifier '" + targetName + "'";
                     return nullptr;
                  }
                  if (sym->isReadOnly)
                  {
                     error.severity = Severity::Error;
                     error.span = baseId->span;
                     error.message = "cannot assign to read-only variable '" + targetName + "'";
                     return nullptr;
                  }

                  SwizzleInfo sw;
                  std::string swErr;
                  if (!ParseAndValidateSwizzle(targetField, sym->type, targetName, sw, swErr))
                  {
                     error.severity = Severity::Error;
                     error.span = acc->span;
                     error.message = swErr;
                     return nullptr;
                  }

                  if (HasDuplicateSwizzleComponents(targetField))
                  {
                     error.severity = Severity::Error;
                     error.span = acc->span;
                     error.message = "cannot write to swizzle with duplicate components '" + targetField + "'";
                     return nullptr;
                  }

                  lvalType = sw.resultType;
                  lvalDomain = sym->domain;
                  swizzleCount = sw.numComponents;
                  for (int i = 0; i < 4; ++i)
                     swizzleIndices[i] = sw.indices[i];
               }
               else
               {
                  error.severity = Severity::Error;
                  error.span = assign->span;
                  error.message = "invalid assignment target";
                  return nullptr;
               }

               // Evaluate RHS
               auto rhsIR = LowerAstExpr(assign->rvalue, scope, error);
               if (!rhsIR) return nullptr;

               // Infer local variable type/domain if new local
               if (lvalType.kind == DataType::Void)
               {
                  lvalType = rhsIR->type;
                  lvalDomain = rhsIR->domain;

                  VarSymbol newSym;
                  newSym.name = targetName;
                  newSym.type = lvalType;
                  newSym.domain = lvalDomain;
                  scope.Add(newSym);
               }
               else
               {
                  // Check assignment type compatibility
                  if (assign->op == "=")
                  {
                     if (lvalType != rhsIR->type && rhsIR->type.lanes != 1)
                     {
                        error.severity = Severity::Error;
                        error.span = assign->span;
                        error.message = "cannot assign " + std::string(rhsIR->type.ToString()) +
                                        " to " + std::string(lvalType.ToString()) +
                                        " (hint: broadcast goes scalar to vector only)";
                        return nullptr;
                     }
                  }
                  else
                  {
                     // Compound assign: +=, -=, *=, /=
                     FieldType resType;
                     std::string rErr;
                     if (!JoinRank(lvalType, rhsIR->type, resType, rErr))
                     {
                        error.severity = Severity::Error;
                        error.span = assign->span;
                        error.message = rErr;
                        return nullptr;
                     }
                  }
               }

               bool compatible = true;
               Domain stmtDomain = JoinDomains(lvalDomain, rhsIR->domain, compatible);
               if (!compatible)
               {
                  error.severity = Severity::Error;
                  error.span = assign->span;
                  error.message = "incomparable domains in assignment";
                  return nullptr;
               }

               // Track write mask
               if (targetName == "P") prog.writeMask.wroteP = true;
               else if (targetName == "N") prog.writeMask.wroteN = true;
               else if (targetName == "uv") prog.writeMask.wroteUv = true;
               else if (targetName == "Cd") prog.writeMask.wroteCd = true;
               else
               {
                  const VarSymbol* sym = scope.Find(targetName);
                  if (sym)
                  {
                     if (sym->isAttrib)
                     {
                        if (!prog.writeMask.WroteAttrib(targetName))
                           prog.writeMask.wroteAttribs.push_back(targetName);
                     }
                     if (sym->isState)
                     {
                        scope.MarkWrittenState(targetName);
                     }
                  }
               }

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::Assign, assign->span);
               stmt->domain = stmtDomain;
               stmt->assignTarget = targetName;
               stmt->assignField = targetField;
               stmt->assignOp = assign->op;
               stmt->swizzleCount = swizzleCount;
               for (int i = 0; i < 4; ++i)
                  stmt->swizzleIndices[i] = swizzleIndices[i];
               stmt->rvalueExpr = rhsIR;
               return stmt;
            }

            case AstKind::If:
            {
               auto ifAst = std::static_pointer_cast<AstIf>(ast);
               auto condIR = LowerAstExpr(ifAst->cond, scope, error);
               if (!condIR) return nullptr;

               if (condIR->type.lanes > 1)
               {
                  error.severity = Severity::Error;
                  error.span = ifAst->cond->span;
                  error.message = "if condition must be a scalar";
                  return nullptr;
               }

               std::vector<IRStmtPtr> thenStmts;
               if (ifAst->thenBlock)
               {
                  if (ifAst->thenBlock->kind == AstKind::Block)
                  {
                     auto blk = std::static_pointer_cast<AstBlock>(ifAst->thenBlock);
                     for (const auto& s : blk->statements)
                     {
                        auto irS = LowerAstStmt(s, scope, prog, error);
                        if (!irS) return nullptr;
                        thenStmts.push_back(irS);
                     }
                  }
                  else
                  {
                     auto irS = LowerAstStmt(ifAst->thenBlock, scope, prog, error);
                     if (!irS) return nullptr;
                     thenStmts.push_back(irS);
                  }
               }

               std::vector<IRStmtPtr> elseStmts;
               if (ifAst->elseBlock)
               {
                  if (ifAst->elseBlock->kind == AstKind::Block)
                  {
                     auto blk = std::static_pointer_cast<AstBlock>(ifAst->elseBlock);
                     for (const auto& s : blk->statements)
                     {
                        auto irS = LowerAstStmt(s, scope, prog, error);
                        if (!irS) return nullptr;
                        elseStmts.push_back(irS);
                     }
                  }
                  else
                  {
                     auto irS = LowerAstStmt(ifAst->elseBlock, scope, prog, error);
                     if (!irS) return nullptr;
                     elseStmts.push_back(irS);
                  }
               }

               Domain ifDomain = condIR->domain;
               for (const auto& s : thenStmts)
               {
                  bool comp = true;
                  ifDomain = JoinDomains(ifDomain, s->domain, comp);
                  if (!comp)
                  {
                     error.severity = Severity::Error;
                     error.span = ifAst->span;
                     error.message = "incomparable domains in if branch";
                     return nullptr;
                  }
               }
               for (const auto& s : elseStmts)
               {
                  bool comp = true;
                  ifDomain = JoinDomains(ifDomain, s->domain, comp);
                  if (!comp)
                  {
                     error.severity = Severity::Error;
                     error.span = ifAst->span;
                     error.message = "incomparable domains in else branch";
                     return nullptr;
                  }
               }

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::If, ifAst->span);
               stmt->domain = ifDomain;
               stmt->ifCond = condIR;
               stmt->thenStmts = std::move(thenStmts);
               stmt->elseStmts = std::move(elseStmts);
               return stmt;
            }

            case AstKind::For:
            {
               auto forAst = std::static_pointer_cast<AstFor>(ast);

               IRStmtPtr initStmt = nullptr;
               if (forAst->init)
               {
                  initStmt = LowerAstStmt(forAst->init, scope, prog, error);
                  if (!initStmt) return nullptr;
               }

               IRNodePtr condIR = nullptr;
               if (forAst->cond)
               {
                  condIR = LowerAstExpr(forAst->cond, scope, error);
                  if (!condIR) return nullptr;

                  // Rule §5.6 & §8: Loop bounds must be a compile-time constant
                  // If condition mentions runtime variables like count or i, refuse it
                  if (condIR->domain != Domain::Graph && condIR->kind != IRKind::Literal)
                  {
                     error.severity = Severity::Error;
                     error.span = forAst->cond->span;
                     error.message = "loop bound must be a compile-time constant";
                     return nullptr;
                  }
               }

               IRStmtPtr stepStmt = nullptr;
               if (forAst->step)
               {
                  stepStmt = LowerAstStmt(forAst->step, scope, prog, error);
                  if (!stepStmt) return nullptr;
               }

               std::vector<IRStmtPtr> bodyStmts;
               if (forAst->body)
               {
                  if (forAst->body->kind == AstKind::Block)
                  {
                     auto blk = std::static_pointer_cast<AstBlock>(forAst->body);
                     for (const auto& s : blk->statements)
                     {
                        auto irS = LowerAstStmt(s, scope, prog, error);
                        if (!irS) return nullptr;
                        bodyStmts.push_back(irS);
                     }
                  }
                  else
                  {
                     auto irS = LowerAstStmt(forAst->body, scope, prog, error);
                     if (!irS) return nullptr;
                     bodyStmts.push_back(irS);
                  }
               }

               Domain forDomain = Domain::Graph;
               if (initStmt) { bool c = true; forDomain = JoinDomains(forDomain, initStmt->domain, c); }
               if (condIR)   { bool c = true; forDomain = JoinDomains(forDomain, condIR->domain, c); }
               if (stepStmt) { bool c = true; forDomain = JoinDomains(forDomain, stepStmt->domain, c); }
               for (const auto& s : bodyStmts)
               {
                  bool c = true;
                  forDomain = JoinDomains(forDomain, s->domain, c);
               }

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::For, forAst->span);
               stmt->domain = forDomain;
               stmt->forInit = initStmt;
               stmt->forCond = condIR;
               stmt->forStep = stepStmt;
               stmt->forBody = std::move(bodyStmts);
               return stmt;
            }

            default:
            {
               // Bare expression statement
               auto exprIR = LowerAstExpr(ast, scope, error);
               if (!exprIR) return nullptr;

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::Expr, ast->span);
               stmt->domain = exprIR->domain;
               stmt->expr = exprIR;
               return stmt;
            }
         }
      }
   }

   bool LowerAstToIR(const AstNodePtr& ast, IRNodePtr& outIR, FieldError& outError)
   {
      outError.Clear();
      ElementScope defaultScope;
      defaultScope.enforceDeclaration = false;
      outIR = LowerAstExpr(ast, defaultScope, outError);
      return outIR != nullptr && outError.Empty();
   }

   bool LowerElementProgramToIR(const AstNodePtr& ast, ElementIRProgram& outProgram, FieldError& outError)
   {
      outError.Clear();
      outProgram = ElementIRProgram{};

      if (!ast)
      {
         outError.severity = Severity::Error;
         outError.message = "empty program";
         return false;
      }

      ElementScope scope;
      scope.targetDomain = Domain::Element;
      scope.enforceDeclaration = true;

      // Seed reserved words of Element and Frame domains
      {
         VarSymbol sym;
         sym.name = "P"; sym.type = FieldType(DataType::Vec3, 3); sym.domain = Domain::Element; sym.isReserved = true; scope.Add(sym);
         sym.name = "N"; sym.type = FieldType(DataType::Vec3, 3); sym.domain = Domain::Element; sym.isReserved = true; scope.Add(sym);
         sym.name = "uv"; sym.type = FieldType(DataType::Vec2, 2); sym.domain = Domain::Element; sym.isReserved = true; scope.Add(sym);
         sym.name = "Cd"; sym.type = FieldType(DataType::Vec3, 3); sym.domain = Domain::Element; sym.isReserved = true; scope.Add(sym);
         sym.name = "i"; sym.type = FieldType(DataType::Int, 1); sym.domain = Domain::Element; sym.isReserved = true; sym.isReadOnly = true; scope.Add(sym);
         sym.name = "count"; sym.type = FieldType(DataType::Int, 1); sym.domain = Domain::Frame; sym.isReserved = true; sym.isReadOnly = true; scope.Add(sym);
         sym.name = "t"; sym.type = FieldType(DataType::Float, 1); sym.domain = Domain::Frame; sym.isReserved = true; sym.isReadOnly = true; scope.Add(sym);
         sym.name = "dt"; sym.type = FieldType(DataType::Float, 1); sym.domain = Domain::Frame; sym.isReserved = true; sym.isReadOnly = true; scope.Add(sym);
         sym.name = "frame"; sym.type = FieldType(DataType::Float, 1); sym.domain = Domain::Frame; sym.isReserved = true; sym.isReadOnly = true; scope.Add(sym);
      }

      std::vector<AstNodePtr> stmts;
      if (ast->kind == AstKind::Program)
      {
         auto progAst = std::static_pointer_cast<AstProgram>(ast);
         stmts = progAst->statements;
      }
      else
      {
         stmts.push_back(ast);
      }

      // Pass 0: Pre-scan top-level assignment targets so mutual references in dataflow cycles can be resolved and analyzed
      std::unordered_set<std::string> declaredNames;
      for (const auto& s : stmts)
      {
         if (s->kind == AstKind::DeclAttrib)
            declaredNames.insert(std::static_pointer_cast<AstDeclAttrib>(s)->name);
         else if (s->kind == AstKind::DeclParam)
            declaredNames.insert(std::static_pointer_cast<AstDeclParam>(s)->name);
         else if (s->kind == AstKind::DeclState)
            declaredNames.insert(std::static_pointer_cast<AstDeclState>(s)->name);
      }

      for (const auto& s : stmts)
      {
         if (s->kind == AstKind::Assign)
         {
            auto assign = std::static_pointer_cast<AstAssign>(s);
            if (assign->lvalue->kind == AstKind::Ident)
            {
               auto id = std::static_pointer_cast<AstIdent>(assign->lvalue);
               if (declaredNames.find(id->name) == declaredNames.end() && !scope.Has(id->name))
               {
                  VarSymbol sym;
                  sym.name = id->name;
                  sym.type = FieldType(DataType::Float, 1);
                  sym.domain = Domain::Element;
                  sym.isProvisional = true;
                  scope.Add(sym);
               }
            }
         }
      }

      // First pass: lower all statements and assign domains
      std::vector<IRStmtPtr> irStmts;
      for (const auto& s : stmts)
      {
         auto irS = LowerAstStmt(s, scope, outProgram, outError);
         if (!outError.Empty())
         {
            return false;
         }
         if (irS)
         {
            irStmts.push_back(irS);
         }
      }

      // Add StateWrite statements for all declared states at the end of body
      for (const auto& ds : outProgram.declaredStates)
      {
         auto writeStmt = std::make_shared<IRStmt>(IRStmtKind::StateWrite, ds.span);
         writeStmt->domain = ds.domain;
         writeStmt->assignTarget = ds.name;
         writeStmt->stateName = ds.name;
         writeStmt->stateType = ds.type;
         writeStmt->stateLanes = ds.lanes;

         auto rVal = std::make_shared<IRNode>(IRKind::Variable, ds.span);
         rVal->varName = ds.name;
         rVal->type = FieldType(ds.type, ds.lanes);
         rVal->domain = ds.domain;
         writeStmt->rvalueExpr = rVal;

         irStmts.push_back(writeStmt);
      }

      // Constant folding pass
      FoldConstantsInStmts(irStmts);

      // Dataflow cycle legality check pass (§5.2)
      if (!CheckDataflowCycles(irStmts, outProgram.declaredStates, outError))
      {
         return false;
      }

      // Second pass: Partition into prologue (Frame/Graph domain) and element loop (Element domain)
      // This is the rate inference hoist! (§5.5)
      for (const auto& s : irStmts)
      {
         if (s->domain == Domain::Graph || s->domain == Domain::Frame)
         {
            outProgram.prologue.push_back(s);
         }
         else
         {
            outProgram.elementLoop.push_back(s);
         }
      }

      // Check time dependency
      auto checkNodeTime = [](const IRNodePtr& n, auto& self) -> bool {
         if (!n) return false;
         if (n->kind == IRKind::Variable && (n->varName == "t" || n->varName == "dt" || n->varName == "frame"))
            return true;
         if (n->kind == IRKind::Call && (n->callee == "rand" || n->callee == "noise" || n->callee == "sh"))
            return true;
         for (const auto& c : n->children)
         {
            if (self(c, self)) return true;
         }
         return false;
      };

      auto checkStmtTime = [&](const IRStmtPtr& s, auto& self) -> bool {
         if (!s) return false;
         if (s->rvalueExpr && checkNodeTime(s->rvalueExpr, checkNodeTime)) return true;
         if (s->attribInitExpr && checkNodeTime(s->attribInitExpr, checkNodeTime)) return true;
         if (s->expr && checkNodeTime(s->expr, checkNodeTime)) return true;
         if (s->ifCond && checkNodeTime(s->ifCond, checkNodeTime)) return true;
         for (const auto& t : s->thenStmts) if (self(t, self)) return true;
         for (const auto& e : s->elseStmts) if (self(e, self)) return true;
         for (const auto& b : s->forBody) if (self(b, self)) return true;
         return false;
      };

      for (const auto& s : irStmts)
      {
         if (checkStmtTime(s, checkStmtTime))
         {
            outProgram.isTimeDependent = true;
            break;
         }
      }

      return true;
   }

   bool LowerPixelProgramToIR(const AstNodePtr& ast, PixelIRProgram& outProgram, FieldError& outError)
   {
      outError.Clear();
      outProgram = PixelIRProgram{};

      if (!ast)
      {
         outError.severity = Severity::Error;
         outError.message = "empty program";
         return false;
      }

      ElementScope scope;
      scope.targetDomain = Domain::Pixel;
      scope.enforceDeclaration = false;

      // Seed reserved words of Pixel and Frame domains
      {
         auto addSym = [&](const std::string& name, FieldType type, Domain dom, bool readOnly) {
            VarSymbol sym;
            sym.name = name;
            sym.type = type;
            sym.domain = dom;
            sym.isReserved = true;
            sym.isReadOnly = readOnly;
            scope.Add(sym);
         };
         addSym("uv", FieldType(DataType::Vec2, 2), Domain::Pixel, true);
         addSym("xy", FieldType(DataType::Vec2, 2), Domain::Pixel, true);
         addSym("res", FieldType(DataType::Vec2, 2), Domain::Frame, true);
         addSym("aspect", FieldType(DataType::Float, 1), Domain::Frame, true);
         addSym("t", FieldType(DataType::Float, 1), Domain::Frame, true);
         addSym("dt", FieldType(DataType::Float, 1), Domain::Frame, true);
         addSym("frame", FieldType(DataType::Float, 1), Domain::Frame, true);
         addSym("col", FieldType(DataType::Vec3, 3), Domain::Pixel, false);
         addSym("alpha", FieldType(DataType::Float, 1), Domain::Pixel, false);
      }

      std::vector<AstNodePtr> stmts;
      if (ast->kind == AstKind::Program)
      {
         auto progAst = std::static_pointer_cast<AstProgram>(ast);
         stmts = progAst->statements;
      }
      else
      {
         stmts.push_back(ast);
      }

      // Pre-scan declarations
      std::unordered_set<std::string> declaredNames;
      for (const auto& s : stmts)
      {
         if (s->kind == AstKind::DeclParam)
            declaredNames.insert(std::static_pointer_cast<AstDeclParam>(s)->name);
         else if (s->kind == AstKind::DeclState)
            declaredNames.insert(std::static_pointer_cast<AstDeclState>(s)->name);
      }

      // Pass 0: pre-scan assignments
      for (const auto& s : stmts)
      {
         if (s->kind == AstKind::Assign)
         {
            auto assign = std::static_pointer_cast<AstAssign>(s);
            if (assign->lvalue->kind == AstKind::Ident)
            {
               auto id = std::static_pointer_cast<AstIdent>(assign->lvalue);
               if (declaredNames.find(id->name) == declaredNames.end() && !scope.Has(id->name))
               {
                  VarSymbol sym;
                  sym.name = id->name;
                  sym.type = FieldType(DataType::Float, 1);
                  sym.domain = Domain::Pixel;
                  sym.isProvisional = true;
                  scope.Add(sym);
               }
            }
         }
      }

      // First pass: lower statements
      ElementIRProgram dummyElemProg;
      std::vector<IRStmtPtr> irStmts;
      for (const auto& s : stmts)
      {
         if (s->kind == AstKind::DeclParam)
         {
            auto dp = std::static_pointer_cast<AstDeclParam>(s);
            if (scope.Has(dp->name))
            {
               const VarSymbol* existing = scope.Find(dp->name);
               if (existing && !existing->isProvisional)
               {
                  outError.severity = Severity::Error;
                  outError.span = dp->span;
                  outError.message = "duplicate declaration of param '" + dp->name + "'";
                  return false;
               }
            }
            DeclaredParam p;
            p.name = dp->name;
            p.typeName = dp->typeName.empty() ? "float" : dp->typeName;
            p.defaultValue = dp->defaultValue;
            p.minValue = dp->minVal;
            p.maxValue = dp->maxVal;
            outProgram.declaredParams.push_back(p);

            VarSymbol sym;
            sym.name = dp->name;
            sym.type = FieldType(DataType::Float, 1);
            sym.domain = Domain::Graph;
            sym.isParam = true;
            sym.isReadOnly = true;
            scope.Add(sym);
            continue;
         }
         else if (s->kind == AstKind::DeclState)
         {
            auto ds = std::static_pointer_cast<AstDeclState>(s);
            if (outProgram.declaredStates.size() >= 4)
            {
               outError.severity = Severity::Error;
               outError.span = ds->span;
               outError.message = "pixel state: 4 cells max in this build (one RGBA16F ping-pong pair); GLUtil::RunShaderPass binds a single color attachment";
               return false;
            }
            DataType dt = DataType::Float;
            int lanes = 1;
            if (ds->typeName == "float") { dt = DataType::Float; lanes = 1; }
            else if (ds->typeName == "int") { dt = DataType::Int; lanes = 1; }
            else if (ds->typeName == "bool") { dt = DataType::Bool; lanes = 1; }
            else if (ds->typeName == "vec2") { dt = DataType::Vec2; lanes = 2; }
            else if (ds->typeName == "vec3") { dt = DataType::Vec3; lanes = 3; }
            else if (ds->typeName == "vec4") { dt = DataType::Vec4; lanes = 4; }

            std::vector<float> initVals(lanes, 0.0f);
            if (ds->initExpr)
            {
               auto initIR = LowerAstExpr(ds->initExpr, scope, outError);
               if (!initIR) return false;
               if (initIR->kind == IRKind::Literal)
               {
                  for (int i = 0; i < lanes; ++i)
                     initVals[i] = (float)initIR->vecValues[i];
                  if (lanes == 1) initVals[0] = (float)initIR->numberValue;
               }
            }

            DeclaredState st;
            st.name = ds->name;
            st.typeName = ds->typeName;
            st.type = dt;
            st.lanes = lanes;
            st.initialValues = initVals;
            st.domain = Domain::Pixel;
            st.span = ds->span;
            outProgram.declaredStates.push_back(st);

            VarSymbol sym;
            sym.name = ds->name;
            sym.type = FieldType(dt, lanes);
            sym.domain = Domain::Pixel;
            sym.isState = true;
            scope.Add(sym);

            auto stmt = std::make_shared<IRStmt>(IRStmtKind::DeclState, ds->span);
            stmt->domain = Domain::Pixel;
            stmt->stateName = ds->name;
            stmt->stateTypeName = ds->typeName;
            stmt->stateType = dt;
            stmt->stateLanes = lanes;
            stmt->stateInitValues = initVals;
            irStmts.push_back(stmt);
            continue;
         }
         else if (s->kind != AstKind::Assign && s->kind != AstKind::If && s->kind != AstKind::For)
         {
            // Top-level expression statement (e.g. `col = expr` or bare `vec3(...)`)
            auto exprIR = LowerAstExpr(s, scope, outError);
            if (!outError.Empty()) return false;
            if (exprIR)
            {
               auto stmt = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
               stmt->domain = exprIR->domain;
               stmt->assignTarget = "col";
               stmt->assignOp = "=";
               stmt->rvalueExpr = exprIR;
               irStmts.push_back(stmt);
            }
            continue;
         }

         auto irS = LowerAstStmt(s, scope, dummyElemProg, outError);
         if (!outError.Empty())
         {
            return false;
         }
         if (irS)
         {
            irStmts.push_back(irS);
         }
      }

      // Add StateWrite statements for all declared states at end of body
      for (const auto& ds : outProgram.declaredStates)
      {
         auto writeStmt = std::make_shared<IRStmt>(IRStmtKind::StateWrite, ds.span);
         writeStmt->domain = ds.domain;
         writeStmt->assignTarget = ds.name;
         writeStmt->stateName = ds.name;
         writeStmt->stateType = ds.type;
         writeStmt->stateLanes = ds.lanes;

         auto rVal = std::make_shared<IRNode>(IRKind::Variable, ds.span);
         rVal->varName = ds.name;
         rVal->type = FieldType(ds.type, ds.lanes);
         rVal->domain = ds.domain;
         writeStmt->rvalueExpr = rVal;

         irStmts.push_back(writeStmt);
      }

      // Check for pixel-varying rand / noise / sh (OPEN-3)
      auto checkPixelRand = [](const IRNodePtr& n, auto& self, FieldError& err) -> bool {
         if (!n) return true;
         if (n->kind == IRKind::Call && (n->callee == "rand" || n->callee == "noise" || n->callee == "sh"))
         {
            if (n->domain == Domain::Pixel)
            {
               err.severity = Severity::Error;
               err.span = n->span;
               err.message = "rand/noise/sh cannot vary per pixel in this build: the reference generator uses int64 arithmetic, which GLSL 150 cannot express (64-bit integers are GLSL 4.00)";
               return false;
            }
         }
         for (const auto& c : n->children)
         {
            if (!self(c, self, err)) return false;
         }
         return true;
      };

      for (const auto& s : irStmts)
      {
         if (s->rvalueExpr && !checkPixelRand(s->rvalueExpr, checkPixelRand, outError)) return false;
         if (s->expr && !checkPixelRand(s->expr, checkPixelRand, outError)) return false;
         if (s->ifCond && !checkPixelRand(s->ifCond, checkPixelRand, outError)) return false;
      }

      // Constant folding pass
      FoldConstantsInStmts(irStmts);

      // Dataflow cycle check pass
      if (!CheckDataflowCycles(irStmts, outProgram.declaredStates, outError))
      {
         return false;
      }

      // Second pass: Partition into prologue (Frame/Graph domain) and pixelBody (Pixel domain)
      for (const auto& s : irStmts)
      {
         if (s->domain == Domain::Graph || s->domain == Domain::Frame)
         {
            outProgram.prologue.push_back(s);
         }
         else
         {
            outProgram.pixelBody.push_back(s);
         }
      }

      // Check time dependency
      auto checkNodeTime = [](const IRNodePtr& n, auto& self) -> bool {
         if (!n) return false;
         if (n->kind == IRKind::Variable && (n->varName == "t" || n->varName == "dt" || n->varName == "frame"))
            return true;
         if (n->kind == IRKind::Call && (n->callee == "rand" || n->callee == "noise" || n->callee == "sh"))
            return true;
         for (const auto& c : n->children)
         {
            if (self(c, self)) return true;
         }
         return false;
      };

      auto checkStmtTime = [&](const IRStmtPtr& s, auto& self) -> bool {
         if (!s) return false;
         if (s->rvalueExpr && checkNodeTime(s->rvalueExpr, checkNodeTime)) return true;
         if (s->expr && checkNodeTime(s->expr, checkNodeTime)) return true;
         if (s->ifCond && checkNodeTime(s->ifCond, checkNodeTime)) return true;
         for (const auto& t : s->thenStmts) if (self(t, self)) return true;
         for (const auto& e : s->elseStmts) if (self(e, self)) return true;
         for (const auto& b : s->forBody) if (self(b, self)) return true;
         return false;
      };

      for (const auto& s : irStmts)
      {
         if (checkStmtTime(s, checkStmtTime))
         {
            outProgram.isTimeDependent = true;
            break;
         }
      }

      return true;
   }
}


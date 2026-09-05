#include "FieldIR.h"
#include "FieldSwizzle.h"
#include "FieldCycles.h"
#include "ExprGlobals.h"

#include <cctype>
#include <cmath>
#include <functional>
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
      bool IsLiteralConstant(const AstNodePtr& node, std::string& outNonConst)
      {
         if (!node) return true;
         if (node->kind == AstKind::Literal) return true;
         if (node->kind == AstKind::Ident)
         {
            outNonConst = std::static_pointer_cast<AstIdent>(node)->name;
            return false;
         }
         if (node->kind == AstKind::Binary)
         {
            auto bin = std::static_pointer_cast<AstBinary>(node);
            return IsLiteralConstant(bin->lhs, outNonConst) && IsLiteralConstant(bin->rhs, outNonConst);
         }
         if (node->kind == AstKind::Unary)
         {
            auto un = std::static_pointer_cast<AstUnary>(node);
            return IsLiteralConstant(un->operand, outNonConst);
         }
         outNonConst = "expression";
         return false;
      }

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
         // graph domain (step 10): bound by `name = emit(...)`. Handles carry
         // no FieldType - `type` is unused when this is set.
         bool isHandle = false;
         // graph domain (step 10, doc §5.5.2/5.5.3): seeded from ExprGlobals
         // rather than declared in the kernel. Only ever set inside
         // LowerGraphProgramToIR, so a read-only-assignment hit against a
         // symbol with this set can only happen in a graph kernel - safe to
         // give it the globals-specific error message everywhere that check
         // is made, without touching Element/Pixel/Sample lowering at all.
         bool isGlobal = false;
         // Build step 12: declared via `output`/`input`. isStructuralGeometry
         // marks a `geometry`-typed input pin (whole-mesh, no FieldType) -
         // only legal as the base of a `.P`/`.N`/`.uv`/`.Cd` field access.
         bool isOutputPin = false;
         bool isInputPin = false;
         bool isStructuralGeometry = false;
      };

      struct ElementScope
      {
         std::unordered_map<std::string, VarSymbol> symbols;
         std::unordered_set<std::string> writtenStates;
         Domain targetDomain = Domain::Element;
         bool enforceDeclaration = false;
         // graph domain (step 10, doc §5.3.2): incremented while lowering a
         // for-loop body, so an Emit statement inside can require a key path
         // - otherwise every iteration would collapse onto the same identity
         // key. Domain-generic ElementScope carries it for free; every other
         // domain's lowering never touches it.
         int graphLoopDepth = 0;
         // Build step 12: incremented while lowering the body of an if/for/map
         // so DeclOutput/DeclInput can refuse a nested occurrence with a
         // clear "must be declared at top level" message.
         int declDepth = 0;

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
               if (lit->isString)
               {
                  error.severity = Severity::Error;
                  error.span = lit->span;
                  error.message = "a string literal is only valid as emit()'s first argument or set()'s second argument";
                  return nullptr;
               }
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
                  if (sym->isStructuralGeometry)
                  {
                     error.severity = Severity::Error;
                     error.span = id->span;
                     error.message = "geometry input '" + id->name + "' cannot be used as a value directly; access a field, e.g. '" + id->name + ".P'";
                     return nullptr;
                  }
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
                  ir->isHandle = sym->isHandle;
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

               // Build step 12: `<name>.P` / `.N` / `.uv` / `.Cd` on a
               // declared `geometry`-typed input pin is a structural field
               // read, not a swizzle (geometry has no FieldType to swizzle).
               if (acc->base->kind == AstKind::Ident)
               {
                  const std::string& baseName = std::static_pointer_cast<AstIdent>(acc->base)->name;
                  const VarSymbol* baseSym = scope.Find(baseName);
                  if (baseSym && baseSym->isStructuralGeometry)
                  {
                     FieldType fieldType;
                     if (acc->field == "P" || acc->field == "N" || acc->field == "Cd")
                        fieldType = FieldType(DataType::Vec3, 3);
                     else if (acc->field == "uv")
                        fieldType = FieldType(DataType::Vec2, 2);
                     else
                     {
                        error.severity = Severity::Error;
                        error.span = acc->span;
                        error.message = "geometry input '" + baseName + "' has no field '" + acc->field +
                                          "' (supported: P, N, uv, Cd)";
                        return nullptr;
                     }

                     auto ir = std::make_shared<IRNode>(IRKind::Access, acc->span);
                     ir->type = fieldType;
                     ir->domain = baseSym->domain;
                     ir->field = baseName + "." + acc->field;
                     auto baseVar = std::make_shared<IRNode>(IRKind::Variable, acc->base->span);
                     baseVar->varName = baseName;
                     baseVar->type = fieldType;
                     baseVar->domain = baseSym->domain;
                     ir->children.push_back(baseVar);
                     return ir;
                  }
               }

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
                  error = MakeIncomparableDomainError(lhsIR->domain, lhsIR->span, rhsIR->domain, rhsIR->span, "binary operation '" + bin->op + "'");
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

               // 0a. Build step 23 (OPEN-B): `X.at(k)` is a NEIGHBOUR read of
               // element k's X, not a method call. It reads the cook's input
               // buffer - for a mesh attribute that is the incoming mesh, for
               // an element state cell the previous cook's value - so element
               // j's result never depends on whether element j-1 has already
               // run. That is the whole reason the loop stays vectorizable.
               if (call->callee.size() > 3 &&
                   call->callee.compare(call->callee.size() - 3, 3, ".at") == 0)
               {
                  const std::string baseName = call->callee.substr(0, call->callee.size() - 3);

                  if (scope.targetDomain != Domain::Element)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "'.at()' is an element-domain form; this kernel is " +
                                     std::string(DomainToString(scope.targetDomain)) + "-domain";
                     if (scope.targetDomain == Domain::Pixel)
                        error.hint = "a pixel kernel reads its neighbours as '" + baseName + "(uv + d)'";
                     return nullptr;
                  }

                  const VarSymbol* baseSym = scope.Find(baseName);
                  if (!baseSym)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "'" + baseName + "' is not declared, so '" + baseName +
                                     ".at()' has nothing to read";
                     return nullptr;
                  }

                  if (baseSym->domain != Domain::Element)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "'" + baseName + "' is " + std::string(DomainToString(baseSym->domain)) +
                                     "-domain - it holds one value for the whole mesh, not one per element, " +
                                     "so it cannot be read at an element index";
                     error.hint = "write '" + baseName + "' on its own to read it";
                     return nullptr;
                  }

                  if (call->args.size() != 1)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "'" + baseName + ".at()' takes exactly 1 argument: " +
                                     baseName + ".at(i - 1)";
                     return nullptr;
                  }

                  auto idxIR = LowerAstExpr(call->args[0], scope, error);
                  if (!idxIR) return nullptr;

                  if (idxIR->type.lanes != 1)
                  {
                     error.severity = Severity::Error;
                     error.span = call->args[0]->span;
                     error.message = "'" + baseName + ".at()' needs a single element index (got " +
                                     idxIR->type.ToString() + ")";
                     error.hint = "e.g. " + baseName + ".at(i - 1)";
                     return nullptr;
                  }

                  auto ir = std::make_shared<IRNode>(baseSym->isState ? IRKind::StateRead : IRKind::Variable,
                                                     call->span);
                  ir->varName = baseName;
                  ir->isNeighbourRead = true;
                  ir->type = baseSym->type;
                  ir->domain = Domain::Element;
                  ir->children.push_back(idxIR);
                  return ir;
               }

               // 0. Build step 22 (OPEN-C): a call whose callee names a state
               // cell is an offset read of that cell - `A(uv + d)` - not a
               // function call. Bare `A` stays sugar for `A(uv)`, so this is
               // the only new spelling. Checked before every builtin so a
               // cell can never be shadowed by one, and after the scope
               // lookup so an ordinary function name is untouched.
               if (const VarSymbol* stSym = scope.Find(call->callee))
               {
                  if (stSym->isState)
                  {
                     if (stSym->domain != Domain::Pixel)
                     {
                        error.severity = Severity::Error;
                        error.span = call->span;
                        error.message = "'" + call->callee + "' is a " + std::string(DomainToString(stSym->domain)) +
                                        "-domain state cell and has no spatial extent, so it cannot be read at a coordinate";
                        error.hint = "offset reads are a pixel-domain form; write '" + call->callee + "' on its own to read the cell";
                        return nullptr;
                     }
                     if (call->args.size() != 1)
                     {
                        error.severity = Severity::Error;
                        error.span = call->span;
                        error.message = "an offset read of state cell '" + call->callee + "' takes exactly 1 argument: " +
                                        call->callee + "(uv + d)";
                        return nullptr;
                     }

                     auto coordIR = LowerAstExpr(call->args[0], scope, error);
                     if (!coordIR) return nullptr;

                     if (coordIR->type.lanes != 2)
                     {
                        error.severity = Severity::Error;
                        error.span = call->args[0]->span;
                        error.message = "an offset read of '" + call->callee + "' needs a vec2 coordinate (got " +
                                        coordIR->type.ToString() + ")";
                        error.hint = "e.g. " + call->callee + "(uv + vec2(1.0 / res.x, 0))";
                        return nullptr;
                     }

                     auto ir = std::make_shared<IRNode>(IRKind::StateRead, call->span);
                     ir->varName = call->callee;
                     ir->isOffsetRead = true;
                     ir->type = stSym->type;
                     ir->domain = Domain::Pixel;
                     ir->children.push_back(coordIR);
                     return ir;
                  }
               }

               // 1. broadcast is implicit and must never be written
               if (call->callee == "broadcast")
               {
                  error.severity = Severity::Error;
                  error.span = call->span;
                  error.message = "broadcast is implicit; write 'P.y += amount'";
                  error.hint = "coarse-to-fine domain transitions happen automatically via rate inference";
                  return nullptr;
               }

               // 2. resample(x, D)
               if (call->callee == "resample")
               {
                  if (call->args.size() != 2)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "resample() expects 2 arguments: resample(x, Domain)";
                     return nullptr;
                  }

                  auto xIR = LowerAstExpr(call->args[0], scope, error);
                  if (!xIR) return nullptr;

                  std::string domainStr;
                  if (call->args[1]->kind == AstKind::Ident)
                  {
                     domainStr = std::static_pointer_cast<AstIdent>(call->args[1])->name;
                  }
                  else
                  {
                     error.severity = Severity::Error;
                     error.span = call->args[1]->span;
                     error.message = "resample() target domain must be an identifier (frame, element, pixel, sample)";
                     return nullptr;
                  }

                  Domain targetDomain;
                  if (!DomainFromString(domainStr, targetDomain))
                  {
                     error.severity = Severity::Error;
                     error.span = call->args[1]->span;
                     error.message = "unknown target domain '" + domainStr + "' in resample() (expected: frame, element, pixel, sample)";
                     return nullptr;
                  }

                  if (!ValidateResample(xIR->domain, targetDomain, call->span, error))
                  {
                     return nullptr;
                  }

                  auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
                  ir->callee = "resample";
                  ir->transferKind = TransferKind::Resample;
                  ir->type = xIR->type;
                  ir->domain = targetDomain;
                  ir->children.push_back(xIR);
                  return ir;
               }

               // 3. downsample(x, k)
               if (call->callee == "downsample")
               {
                  if (call->args.size() != 2)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "downsample() expects 2 arguments: downsample(x, k)";
                     return nullptr;
                  }

                  auto xIR = LowerAstExpr(call->args[0], scope, error);
                  if (!xIR) return nullptr;

                  if (call->args[1]->kind != AstKind::Literal)
                  {
                     std::string nonConst;
                     if (call->args[1]->kind == AstKind::Ident)
                        nonConst = std::static_pointer_cast<AstIdent>(call->args[1])->name;
                     else
                        nonConst = "expression";

                     error.severity = Severity::Error;
                     error.span = call->args[1]->span;
                     error.message = "downsample factor k must be a compile-time constant integer literal >= 1 (got non-constant '" + nonConst + "')";
                     return nullptr;
                  }

                  auto lit = std::static_pointer_cast<AstLiteral>(call->args[1]);
                  int k = (int)lit->numberValue;
                  if (k < 1)
                  {
                     error.severity = Severity::Error;
                     error.span = call->args[1]->span;
                     error.message = "downsample factor k must be a compile-time constant integer >= 1 (got " + std::to_string(k) + ")";
                     return nullptr;
                  }

                  auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
                  ir->callee = "downsample";
                  ir->transferKind = TransferKind::Downsample;
                  ir->divisor = k;
                  ir->type = xIR->type;
                  ir->domain = xIR->domain;
                  ir->children.push_back(xIR);
                  return ir;
               }

               // 4. reduce.<op>(...)
               if (call->callee.rfind("reduce.", 0) == 0)
               {
                  std::string reduceOp = call->callee.substr(7);
                  if (call->args.empty())
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = call->callee + "() expects at least 1 argument";
                     return nullptr;
                  }

                  if (call->args.size() == 1)
                  {
                     const auto& argAst = call->args[0];
                     bool isBareVar = false;
                     if (argAst->kind == AstKind::Ident)
                     {
                        isBareVar = true;
                     }
                     else if (argAst->kind == AstKind::Access)
                     {
                        auto acc = std::static_pointer_cast<AstAccess>(argAst);
                        if (acc->base && acc->base->kind == AstKind::Ident)
                           isBareVar = true;
                     }

                     auto xIR = LowerAstExpr(argAst, scope, error);
                     if (!xIR) return nullptr;

                     if (xIR->domain == Domain::Element && !isBareVar)
                     {
                        error.severity = Severity::Error;
                        error.span = argAst->span;
                        error.message = "reduction argument must be a bare variable or attribute name (got expression; hint: bind the expression to an attribute or variable first, e.g. 'heat = length(P) * 0.1; avg = reduce.mean(heat)')";
                        return nullptr;
                     }

                     if (!ValidateReduce(reduceOp, 1, xIR->domain, call->span, error))
                     {
                        return nullptr;
                     }

                     auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
                     ir->callee = call->callee;
                     ir->transferKind = TransferKind::Reduce;
                     ir->type = xIR->type;
                     ir->domain = DomainCoarsen(xIR->domain);
                     ir->children.push_back(xIR);
                     return ir;
                  }
                  else if (call->args.size() == 3)
                  {
                     auto inIR = LowerAstExpr(call->args[0], scope, error);
                     if (!inIR) return nullptr;
                     auto loIR = LowerAstExpr(call->args[1], scope, error);
                     if (!loIR) return nullptr;
                     auto hiIR = LowerAstExpr(call->args[2], scope, error);
                     if (!hiIR) return nullptr;

                     if (!ValidateReduce(reduceOp, 3, inIR->domain, call->span, error))
                     {
                        return nullptr;
                     }

                     auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
                     ir->callee = call->callee;
                     ir->transferKind = TransferKind::Reduce;
                     ir->type = FieldType(DataType::Float, 1);
                     ir->domain = Domain::Frame;
                     ir->children.push_back(inIR);
                     ir->children.push_back(loIR);
                     ir->children.push_back(hiIR);
                     return ir;
                  }
                  else
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = call->callee + "() expects 1 argument" +
                                     (reduceOp == "rms" ? " (or 3 for band-limited sample reduce: reduce.rms(in, lo, hi))" : "");
                     return nullptr;
                  }
               }

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
                        error = MakeIncomparableDomainError(ctorDomain, call->span, aIR->domain, aIR->span, "vector constructor " + call->callee);
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
                     error = MakeIncomparableDomainError(fnDomain, call->span, aIR->domain, aIR->span, "call to " + call->callee + "()");
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

      // Build step 12: v1 ceiling on the combined number of output+input
      // pins a single kernel may declare (mirrors PinTable::kMaxDeclaredPins).
      static constexpr size_t kFieldMaxDeclaredPinsPerProgram = 16;

      // Build step 12 (doc S5.3 "domain-join check"): is it legal for an
      // `output <declaredDomain> ... = <expr>` initializer whose lowered
      // domain is `exprDomain` to feed a pin declared at `declaredDomain`?
      // Legal iff exprDomain == declaredDomain, or exprDomain is Graph
      // (a constant broadcasts to anything), or exprDomain is Frame and
      // declaredDomain isn't Graph (frame broadcasts down to any finer
      // domain). Anything else is illegal - either exprDomain is strictly
      // finer than declaredDomain (needs an explicit reduce/resample), or
      // the two domains are mutually incomparable peers (Element/Pixel/Sample).
      bool CheckPinDomainOk(Domain exprDomain, Domain declaredDomain, bool& outIsFinerThanDeclared)
      {
         outIsFinerThanDeclared = false;
         if (exprDomain == declaredDomain) return true;
         if (exprDomain == Domain::Graph) return true;
         if (exprDomain == Domain::Frame && declaredDomain != Domain::Graph) return true;

         // Walk exprDomain's coarsening chain (Element/Pixel/Sample -> Frame
         // -> Graph) to see whether declaredDomain lies on it - if so,
         // exprDomain is strictly finer than declaredDomain.
         Domain cur = exprDomain;
         for (int i = 0; i < 3; ++i)
         {
            cur = DomainCoarsen(cur);
            if (cur == declaredDomain) { outIsFinerThanDeclared = true; return false; }
            if (cur == Domain::Graph) break;
         }
         return false;
      }

      static AstNodePtr CloneAstForMap(const AstNodePtr& node,
                                       const std::unordered_map<std::string, std::string>& stateRenames,
                                       int mapIdx)
      {
         if (!node) return nullptr;
         switch (node->kind)
         {
            case AstKind::Literal:
            {
               auto lit = std::static_pointer_cast<AstLiteral>(node);
               if (lit->isBool)
                  return std::make_shared<AstLiteral>(lit->boolValue, lit->span);
               return std::make_shared<AstLiteral>(lit->numberValue, lit->span);
            }
            case AstKind::Ident:
            {
               auto id = std::static_pointer_cast<AstIdent>(node);
               if (id->name == "map_index" || id->name == "_map_i")
                  return std::make_shared<AstLiteral>((double)mapIdx, id->span);
               auto it = stateRenames.find(id->name);
               if (it != stateRenames.end())
                  return std::make_shared<AstIdent>(it->second, id->span);
               return std::make_shared<AstIdent>(id->name, id->span);
            }
            case AstKind::Access:
            {
               auto acc = std::static_pointer_cast<AstAccess>(node);
               return std::make_shared<AstAccess>(CloneAstForMap(acc->base, stateRenames, mapIdx), acc->field, acc->span);
            }
            case AstKind::Unary:
            {
               auto un = std::static_pointer_cast<AstUnary>(node);
               return std::make_shared<AstUnary>(un->op, CloneAstForMap(un->operand, stateRenames, mapIdx), un->span);
            }
            case AstKind::Binary:
            {
               auto bin = std::static_pointer_cast<AstBinary>(node);
               return std::make_shared<AstBinary>(bin->op, CloneAstForMap(bin->lhs, stateRenames, mapIdx), CloneAstForMap(bin->rhs, stateRenames, mapIdx), bin->span);
            }
            case AstKind::Call:
            {
               auto call = std::static_pointer_cast<AstCall>(node);
               std::vector<AstNodePtr> args;
               for (const auto& a : call->args)
                  args.push_back(CloneAstForMap(a, stateRenames, mapIdx));
               return std::make_shared<AstCall>(call->callee, args, call->span);
            }
            case AstKind::Assign:
            {
               auto assign = std::static_pointer_cast<AstAssign>(node);
               return std::make_shared<AstAssign>(assign->op, CloneAstForMap(assign->lvalue, stateRenames, mapIdx), CloneAstForMap(assign->rvalue, stateRenames, mapIdx), assign->span);
            }
            case AstKind::Block:
            {
               auto blk = std::static_pointer_cast<AstBlock>(node);
               std::vector<AstNodePtr> stmts;
               for (const auto& s : blk->statements)
                  stmts.push_back(CloneAstForMap(s, stateRenames, mapIdx));
               return std::make_shared<AstBlock>(stmts, blk->span);
            }
            case AstKind::If:
            {
               auto ifNode = std::static_pointer_cast<AstIf>(node);
               return std::make_shared<AstIf>(CloneAstForMap(ifNode->cond, stateRenames, mapIdx),
                                             CloneAstForMap(ifNode->thenBlock, stateRenames, mapIdx),
                                             CloneAstForMap(ifNode->elseBlock, stateRenames, mapIdx),
                                             ifNode->span);
            }
            case AstKind::For:
            {
               auto forNode = std::static_pointer_cast<AstFor>(node);
               return std::make_shared<AstFor>(CloneAstForMap(forNode->init, stateRenames, mapIdx),
                                              CloneAstForMap(forNode->cond, stateRenames, mapIdx),
                                              CloneAstForMap(forNode->step, stateRenames, mapIdx),
                                              CloneAstForMap(forNode->body, stateRenames, mapIdx),
                                              forNode->span);
            }
            case AstKind::DeclState:
            {
               auto ds = std::static_pointer_cast<AstDeclState>(node);
               auto it = stateRenames.find(ds->name);
               std::string ren = (it != stateRenames.end()) ? it->second : ds->name;
               return std::make_shared<AstDeclState>(ds->typeName, ren, CloneAstForMap(ds->initExpr, stateRenames, mapIdx), ds->span);
            }
            default:
               return node;
         }
      }

      static IRStmtPtr LowerAstStmt(const AstNodePtr& ast, ElementScope& scope, ElementIRProgram& prog, FieldError& error);

      static bool LowerMapStatement(const std::shared_ptr<AstMap>& mapAst,
                                    ElementScope& scope,
                                    ElementIRProgram& outProgram,
                                    std::vector<IRStmtPtr>& outStmts,
                                    FieldError& outError)
      {
         if (!mapAst->countExpr)
         {
            outError.severity = Severity::Error;
            outError.span = mapAst->span;
            outError.message = "map() requires an element count expression, e.g. map(8) { ... }";
            return false;
         }

         std::string nonConst;
         if (!IsLiteralConstant(mapAst->countExpr, nonConst))
         {
            outError.severity = Severity::Error;
            outError.span = mapAst->countExpr->span;
            outError.message = "map() element count must be a compile-time constant integer (got non-constant '" + nonConst + "')";
            return false;
         }

         int count = (int)std::static_pointer_cast<AstLiteral>(mapAst->countExpr)->numberValue;
         if (count < 1 || count > 64)
         {
            outError.severity = Severity::Error;
            outError.span = mapAst->countExpr->span;
            outError.message = "map() element count must be between 1 and 64 (got " + std::to_string(count) + ")";
            return false;
         }

         if (!mapAst->body) return true;

         // Collect statements in map body
         std::vector<AstNodePtr> bodyStmts;
         if (mapAst->body->kind == AstKind::Block)
            bodyStmts = std::static_pointer_cast<AstBlock>(mapAst->body)->statements;
         else
            bodyStmts.push_back(mapAst->body);

         // Find state declarations in body
         std::vector<std::shared_ptr<AstDeclState>> declaredStatesInMap;
         for (const auto& bs : bodyStmts)
         {
            if (bs->kind == AstKind::DeclState)
               declaredStatesInMap.push_back(std::static_pointer_cast<AstDeclState>(bs));
         }

         // Register N distinct state cells for each declared state
         for (const auto& ds : declaredStatesInMap)
         {
            DataType dt = ParseDataType(ds->typeName);
            int lanes = FieldType::GetLanesForType(dt);

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

            for (int k = 0; k < count; ++k)
            {
               std::string instName = ds->name + "__m" + std::to_string(k);
               DeclaredState st;
               st.name = instName;
               st.typeName = ds->typeName;
               st.type = dt;
               st.lanes = lanes;
               st.initialValues = initVals;
               st.domain = scope.targetDomain;
               st.span = ds->span;
               outProgram.declaredStates.push_back(st);

               VarSymbol sym;
               sym.name = instName;
               sym.type = FieldType(dt, lanes);
               sym.domain = scope.targetDomain;
               sym.isState = true;
               scope.Add(sym);
            }
         }

         // Unroll N instances
         scope.declDepth++;
         for (int k = 0; k < count; ++k)
         {
            std::unordered_map<std::string, std::string> renames;
            for (const auto& ds : declaredStatesInMap)
               renames[ds->name] = ds->name + "__m" + std::to_string(k);

            for (const auto& bs : bodyStmts)
            {
               // The state decl itself was already registered into scope and
               // outProgram.declaredStates above, once per instance; re-lowering
               // the (renamed) decl statement here would re-declare the same
               // name and fail with "duplicate declaration".
               if (bs->kind == AstKind::DeclState) continue;

               auto cloned = CloneAstForMap(bs, renames, k);
               auto irS = LowerAstStmt(cloned, scope, outProgram, outError);
               if (!outError.Empty()) { scope.declDepth--; return false; }
               if (irS)
               {
                  if (!ValidateMap(scope.targetDomain, irS->domain, mapAst->span, outError))
                     { scope.declDepth--; return false; }
                  outStmts.push_back(irS);
               }
            }
         }
         scope.declDepth--;

         return true;
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

               if (decl->tableSize > 0)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "table state declarations ('state float " + decl->name + "[N]') are supported in frame and sample domains only (got " +
                                  std::string(DomainToString(scope.targetDomain)) + ")";
                  return nullptr;
               }

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

            case AstKind::DeclOutput:
            {
               auto decl = std::static_pointer_cast<AstDeclOutput>(ast);

               if (scope.declDepth > 0)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'output' must be declared at top level (not inside if/for/map)";
                  return nullptr;
               }

               Domain declaredDomain;
               if (!DomainFromString(decl->domainName, declaredDomain))
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "unknown domain '" + decl->domainName + "' in output declaration";
                  return nullptr;
               }
               if (declaredDomain == Domain::Graph)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'output' cannot declare a graph-domain pin (graph domain has no per-frame dataflow to expose)";
                  return nullptr;
               }

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
                  error.message = "duplicate declaration of '" + decl->name + "'";
                  return nullptr;
               }

               if (prog.declaredOutputs.size() + prog.declaredInputs.size() >= kFieldMaxDeclaredPinsPerProgram)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "declared pin count exceeds the " + std::to_string(kFieldMaxDeclaredPinsPerProgram) + "-pin-per-kernel ceiling (build step 12 v1 limit)";
                  return nullptr;
               }

               if (decl->typeName == "geometry")
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'geometry' is an input-only structural type; use it with 'input', not 'output'";
                  return nullptr;
               }
               // S5.6/S5.8 row 4: each structural pin type has exactly one
               // legal domain - 'audio' only makes sense at 'sample' rate,
               // 'image' only at 'pixel' rate. A cross-domain structural pin
               // is a resample, out of scope for this step (S5.6).
               if (decl->typeName == "audio" && declaredDomain != Domain::Sample)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'audio' pins are only legal in the 'sample' domain (got '" + decl->domainName +
                                    "'); a cross-domain audio read would need an explicit resample, not supported in v1";
                  return nullptr;
               }
               if (decl->typeName == "image" && declaredDomain != Domain::Pixel)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'image' pins are only legal in the 'pixel' domain (got '" + decl->domainName +
                                    "'); a cross-domain image read would need an explicit resample, not supported in v1";
                  return nullptr;
               }

               bool isStructural = (decl->typeName == "audio" || decl->typeName == "image");
               DataType dt = DataType::Float;
               int lanes = 1;
               if (isStructural)
               {
                  if (decl->typeName == "audio") { dt = DataType::Float; lanes = 1; }
                  else { dt = DataType::Vec4; lanes = 4; } // "image"
               }
               else
               {
                  dt = ParseDataType(decl->typeName);
                  if (dt == DataType::Void)
                  {
                     error.severity = Severity::Error;
                     error.span = decl->span;
                     error.message = "unknown type '" + decl->typeName + "' in output declaration";
                     return nullptr;
                  }
                  lanes = FieldType::GetLanesForType(dt);
               }

               if (!decl->initExpr)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "output '" + decl->name + "' requires an initializer expression";
                  return nullptr;
               }
               IRNodePtr initIR = LowerAstExpr(decl->initExpr, scope, error);
               if (!initIR) return nullptr;

               bool isFiner = false;
               if (!CheckPinDomainOk(initIR->domain, declaredDomain, isFiner))
               {
                  if (isFiner)
                  {
                     error.severity = Severity::Error;
                     error.span = decl->initExpr->span;
                     error.message = "output '" + decl->name + "' is declared " + std::string(DomainToString(declaredDomain)) +
                                       "-domain but its expression is " + std::string(DomainToString(initIR->domain)) +
                                       "-domain (finer); wrap it in an explicit reduce/resample to coarsen it first";
                     error.hint = "e.g. reduce.mean(x) or reduce.rms(x)";
                     return nullptr;
                  }
                  error = MakeIncomparableDomainError(initIR->domain, decl->initExpr->span, declaredDomain, decl->span,
                                                      "output '" + decl->name + "' declaration");
                  return nullptr;
               }

               DeclaredOutput out;
               out.name = decl->name;
               out.typeName = decl->typeName;
               out.isStructural = isStructural;
               out.type = dt;
               out.lanes = lanes;
               out.domain = declaredDomain;
               out.span = decl->span;
               prog.declaredOutputs.push_back(out);

               VarSymbol sym;
               sym.name = decl->name;
               sym.type = FieldType(dt, lanes);
               sym.domain = declaredDomain;
               sym.isOutputPin = true;
               sym.isReadOnly = true;
               scope.Add(sym);

               // Frame-domain, non-structural declared outputs (`chime`,
               // `glow`, ...) get a real runtime value: emit a synthetic
               // top-level Assign identical in shape to an ordinary
               // `<name> = <expr>` statement (the exact mechanism `publish`
               // already uses, unwired to the compiler at all). Codegen and
               // prologue/loop placement then handle it exactly like any
               // other frame-domain assignment - see ElementVM::mFrameVars/
               // ReadFrameVar and the declared-output-wiring plan.
               // Structural types (audio/image) and non-Frame domains
               // (element/pixel/sample) have no such name-keyed runtime
               // channel yet, so they stay a declaration-only collection -
               // nothing in the kernel body reads or writes them as a
               // runtime value (mirrors DeclParam, which also returns
               // nullptr here).
               if (declaredDomain == Domain::Frame && !isStructural)
               {
                  auto stmt = std::make_shared<IRStmt>(IRStmtKind::Assign, decl->span);
                  stmt->domain = Domain::Frame;
                  stmt->assignTarget = decl->name;
                  stmt->assignOp = "=";
                  stmt->rvalueExpr = initIR;
                  return stmt;
               }
               return nullptr;
            }

            case AstKind::DeclInput:
            {
               auto decl = std::static_pointer_cast<AstDeclInput>(ast);

               if (scope.declDepth > 0)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'input' must be declared at top level (not inside if/for/map)";
                  return nullptr;
               }

               Domain declaredDomain;
               if (!DomainFromString(decl->domainName, declaredDomain))
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "unknown domain '" + decl->domainName + "' in input declaration";
                  return nullptr;
               }
               if (declaredDomain == Domain::Graph)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'input' cannot declare a graph-domain pin (graph domain has no per-frame dataflow to receive into)";
                  return nullptr;
               }

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
                  error.message = "duplicate declaration of '" + decl->name + "'";
                  return nullptr;
               }

               if (prog.declaredOutputs.size() + prog.declaredInputs.size() >= kFieldMaxDeclaredPinsPerProgram)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "declared pin count exceeds the " + std::to_string(kFieldMaxDeclaredPinsPerProgram) + "-pin-per-kernel ceiling (build step 12 v1 limit)";
                  return nullptr;
               }

               // S5.6/S5.8 row 4: each structural pin type has exactly one
               // legal domain.
               if (decl->typeName == "geometry" && declaredDomain != Domain::Element)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'geometry' pins are only legal in the 'element' domain (got '" + decl->domainName +
                                    "'); a cross-domain geometry read would need an explicit resample, not supported in v1";
                  return nullptr;
               }
               if (decl->typeName == "audio" && declaredDomain != Domain::Sample)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'audio' pins are only legal in the 'sample' domain (got '" + decl->domainName +
                                    "'); a cross-domain audio read would need an explicit resample, not supported in v1";
                  return nullptr;
               }
               if (decl->typeName == "image" && declaredDomain != Domain::Pixel)
               {
                  error.severity = Severity::Error;
                  error.span = decl->span;
                  error.message = "'image' pins are only legal in the 'pixel' domain (got '" + decl->domainName +
                                    "'); a cross-domain image read would need an explicit resample, not supported in v1";
                  return nullptr;
               }

               bool isStructural = (decl->typeName == "geometry" || decl->typeName == "audio" || decl->typeName == "image");
               DataType dt = DataType::Float;
               int lanes = 1;
               if (isStructural)
               {
                  if (decl->typeName == "audio") { dt = DataType::Float; lanes = 1; }
                  else if (decl->typeName == "image") { dt = DataType::Vec4; lanes = 4; }
                  // "geometry" has no FieldType - dt/lanes stay at defaults and are unused.
               }
               else
               {
                  dt = ParseDataType(decl->typeName);
                  if (dt == DataType::Void)
                  {
                     error.severity = Severity::Error;
                     error.span = decl->span;
                     error.message = "unknown type '" + decl->typeName + "' in input declaration";
                     return nullptr;
                  }
                  lanes = FieldType::GetLanesForType(dt);
               }

               DeclaredInput in;
               in.name = decl->name;
               in.typeName = decl->typeName;
               in.isStructural = isStructural;
               in.type = dt;
               in.lanes = lanes;
               in.domain = declaredDomain;
               in.span = decl->span;
               prog.declaredInputs.push_back(in);

               VarSymbol sym;
               sym.name = decl->name;
               sym.type = FieldType(dt, lanes);
               sym.domain = declaredDomain;
               sym.isInputPin = true;
               sym.isReadOnly = true;
               sym.isStructuralGeometry = (decl->typeName == "geometry");
               scope.Add(sym);

               return nullptr;
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

                  const VarSymbol* sym = scope.Find(targetName);
                  // A provisional symbol is a pass-0 placeholder, not a definition. Falling
                  // into the inference branch below is what lets the local take its type and
                  // domain from the RHS - and what makes `heat += 0.5` with no declaration
                  // the compile error it is supposed to be instead of a silent zero read.
                  if (sym && sym->isProvisional)
                     sym = nullptr;
                  // A name reserved by an *unrelated* domain (graph scope pre-seeds every
                  // other domain's reserved names purely to give a clean cross-domain-leak
                  // error on read - see LowerGraphProgramToIR) is not a real conflict for a
                  // fresh local declaration: `for (i = 0; ...)` in graph scope is the
                  // language's own canonical idiom (step-10 doc S5.1) and must be allowed
                  // to shadow the placeholder. Only a name reserved by the domain currently
                  // being lowered is a genuine read-only builtin.
                  if (sym && sym->isReserved && sym->domain != scope.targetDomain && assign->op == "=")
                     sym = nullptr;
                  else if (sym && sym->isReserved && sym->domain == scope.targetDomain &&
                           (targetName == "i" || targetName == "count" ||
                            targetName == "t" || targetName == "dt" || targetName == "frame"))
                  {
                     error.severity = Severity::Error;
                     error.span = id->span;
                     error.message = "cannot assign to read-only variable '" + targetName + "'";
                     return nullptr;
                  }
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
                  error = MakeIncomparableDomainError(lvalDomain, assign->lvalue->span, rhsIR->domain, assign->rvalue->span, "assignment to '" + targetName + "'");
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

            case AstKind::Map:
            {
               auto mapAst = std::static_pointer_cast<AstMap>(ast);
               std::vector<IRStmtPtr> unrolled;
               if (!LowerMapStatement(mapAst, scope, prog, unrolled, error))
                  return nullptr;
               if (unrolled.empty()) return nullptr;
               return unrolled.front();
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

               scope.declDepth++;
               std::vector<IRStmtPtr> thenStmts;
               if (ifAst->thenBlock)
               {
                  if (ifAst->thenBlock->kind == AstKind::Block)
                  {
                     auto blk = std::static_pointer_cast<AstBlock>(ifAst->thenBlock);
                     for (const auto& s : blk->statements)
                     {
                        auto irS = LowerAstStmt(s, scope, prog, error);
                        if (!irS) { scope.declDepth--; return nullptr; }
                        thenStmts.push_back(irS);
                     }
                  }
                  else
                  {
                     auto irS = LowerAstStmt(ifAst->thenBlock, scope, prog, error);
                     if (!irS) { scope.declDepth--; return nullptr; }
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
                        if (!irS) { scope.declDepth--; return nullptr; }
                        elseStmts.push_back(irS);
                     }
                  }
                  else
                  {
                     auto irS = LowerAstStmt(ifAst->elseBlock, scope, prog, error);
                     if (!irS) { scope.declDepth--; return nullptr; }
                     elseStmts.push_back(irS);
                  }
               }
               scope.declDepth--;

               Domain ifDomain = condIR->domain;
               for (const auto& s : thenStmts)
               {
                  bool comp = true;
                  Domain nextD = JoinDomains(ifDomain, s->domain, comp);
                  if (!comp)
                  {
                     error = MakeIncomparableDomainError(ifDomain, ifAst->span, s->domain, s->span, "if branch");
                     return nullptr;
                  }
                  ifDomain = nextD;
               }
               for (const auto& s : elseStmts)
               {
                  bool comp = true;
                  Domain nextD = JoinDomains(ifDomain, s->domain, comp);
                  if (!comp)
                  {
                     error = MakeIncomparableDomainError(ifDomain, ifAst->span, s->domain, s->span, "else branch");
                     return nullptr;
                  }
                  ifDomain = nextD;
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
               scope.declDepth++;
               if (forAst->body)
               {
                  if (forAst->body->kind == AstKind::Block)
                  {
                     auto blk = std::static_pointer_cast<AstBlock>(forAst->body);
                     for (const auto& s : blk->statements)
                     {
                        auto irS = LowerAstStmt(s, scope, prog, error);
                        if (!irS) { scope.declDepth--; return nullptr; }
                        bodyStmts.push_back(irS);
                     }
                  }
                  else
                  {
                     auto irS = LowerAstStmt(forAst->body, scope, prog, error);
                     if (!irS) { scope.declDepth--; return nullptr; }
                     bodyStmts.push_back(irS);
                  }
               }
               scope.declDepth--;

               Domain forDomain = Domain::Graph;
               if (initStmt) { bool c = true; Domain d = JoinDomains(forDomain, initStmt->domain, c); if (!c) { error = MakeIncomparableDomainError(forDomain, forAst->span, initStmt->domain, initStmt->span, "for loop init"); return nullptr; } forDomain = d; }
               if (condIR)   { bool c = true; Domain d = JoinDomains(forDomain, condIR->domain, c); if (!c) { error = MakeIncomparableDomainError(forDomain, forAst->span, condIR->domain, condIR->span, "for loop condition"); return nullptr; } forDomain = d; }
               if (stepStmt) { bool c = true; Domain d = JoinDomains(forDomain, stepStmt->domain, c); if (!c) { error = MakeIncomparableDomainError(forDomain, forAst->span, stepStmt->domain, stepStmt->span, "for loop step"); return nullptr; } forDomain = d; }
               for (const auto& s : bodyStmts)
               {
                  bool c = true;
                  Domain d = JoinDomains(forDomain, s->domain, c);
                  if (!c) { error = MakeIncomparableDomainError(forDomain, forAst->span, s->domain, s->span, "for loop body"); return nullptr; }
                  forDomain = d;
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

      // graph domain (step 10): recursive statement lowering, structurally
      // parallel to LowerAstStmt above but recursing into itself (not
      // LowerAstStmt) so emit/connect/set/place are recognized at any
      // nesting depth inside if/for. Reuses LowerAstExpr for ordinary
      // expressions and ElementScope for locals - both are already
      // domain-generic (Pixel reuses them the same way).
      IRStmtPtr LowerGraphStmt(const AstNodePtr& ast, ElementScope& scope, FieldError& error)
      {
         if (!ast || !error.Empty()) return nullptr;

         auto requireHandle = [&](const AstNodePtr& argAst, const char* who) -> IRNodePtr {
            auto ir = LowerAstExpr(argAst, scope, error);
            if (!ir) return nullptr;
            if (!ir->isHandle)
            {
               error.severity = Severity::Error;
               error.span = argAst->span;
               error.message = std::string(who) + " expects a handle returned by emit() (hint: pass the variable emit()'s result was assigned to)";
               return nullptr;
            }
            return ir;
         };

         auto requireLiteralString = [&](const AstNodePtr& argAst, const char* who) -> const std::string* {
            if (argAst->kind != AstKind::Literal)
            {
               error.severity = Severity::Error;
               error.span = argAst->span;
               error.message = std::string(who) + " must be a literal string. Fix: pass a string literal like \"Wavetable\" directly.";
               return nullptr;
            }
            auto lit = std::static_pointer_cast<AstLiteral>(argAst);
            if (!lit->isString)
            {
               error.severity = Severity::Error;
               error.span = argAst->span;
               error.message = std::string(who) + " must be a literal string (got a number). Fix: pass a string literal like \"Wavetable\" directly.";
               return nullptr;
            }
            return &lit->stringValue;
         };

         if (ast->kind == AstKind::Assign)
         {
            auto assign = std::static_pointer_cast<AstAssign>(ast);

            // emit(): `name = emit("Type", k0, k1, ...)`
            if (assign->op == "=" && assign->rvalue && assign->rvalue->kind == AstKind::Call &&
                std::static_pointer_cast<AstCall>(assign->rvalue)->callee == "emit")
            {
               if (assign->lvalue->kind != AstKind::Ident)
               {
                  error.severity = Severity::Error;
                  error.span = assign->lvalue->span;
                  error.message = "emit()'s result must be assigned to a plain name, e.g. 'n = emit(\"Oscillator\", 0)'";
                  return nullptr;
               }
               auto call = std::static_pointer_cast<AstCall>(assign->rvalue);
               if (call->args.empty())
               {
                  error.severity = Severity::Error;
                  error.span = call->span;
                  error.message = "emit() expects at least 1 argument: emit(\"Type Name\", k0, k1, ...)";
                  return nullptr;
               }
               const std::string* typeName = requireLiteralString(call->args[0], "emit()'s first argument (node type name)");
               if (!typeName) return nullptr;

               if (*typeName == "Group" || *typeName == "Field Graph")
               {
                  error.severity = Severity::Error;
                  error.span = call->args[0]->span;
                  error.message = "emit(\"" + *typeName + "\") is not allowed (doc §5.7.2/RATETEST: a graph kernel cannot emit a Group or another Field Graph node). Fix: emit ordinary leaf node types only.";
                  return nullptr;
               }

               std::vector<IRNodePtr> keyArgs;
               for (size_t i = 1; i < call->args.size(); ++i)
               {
                  auto kIR = LowerAstExpr(call->args[i], scope, error);
                  if (!kIR) return nullptr;
                  keyArgs.push_back(kIR);
               }

               if (scope.graphLoopDepth > 0 && keyArgs.empty())
               {
                  error.severity = Severity::Error;
                  error.span = call->span;
                  error.message = "emit(\"" + *typeName + "\") is inside a loop and has no key, so every iteration would name the same node. Fix: add the loop variable as a key, e.g. emit(\"" + *typeName + "\", i);";
                  return nullptr;
               }

               std::string targetName = std::static_pointer_cast<AstIdent>(assign->lvalue)->name;
               const VarSymbol* existing = scope.Find(targetName);
               // doc §5.3.2: two distinct emit() statements must not target the
               // same name, whether or not the earlier one was itself a handle -
               // a second textually-distinct `osc = emit(...)` shadowing the
               // first would silently orphan the first emit's identity key.
               // (A single emit() statement re-run per loop iteration at
               // *runtime* only ever reaches this compile-time check once, since
               // loops aren't unrolled during lowering - so this can't fire on
               // the doc's own keyed-loop pattern.)
               if (existing && !existing->isProvisional)
               {
                  error.severity = Severity::Error;
                  error.span = assign->lvalue->span;
                  if (existing->isHandle)
                     error.message = "two emit targets are both named '" + targetName + "'; each emit target must have its own name. Fix: give each emit target a unique variable name.";
                  else
                     error.message = "'" + targetName + "' is already declared as a non-handle value; emit()'s result needs its own name. Fix: give emit's target a distinct name.";
                  return nullptr;
               }

               VarSymbol sym;
               sym.name = targetName;
               sym.domain = Domain::Graph;
               sym.isHandle = true;
               scope.Add(sym);

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::Emit, assign->span);
               stmt->domain = Domain::Graph;
               stmt->emitTargetName = targetName;
               stmt->emitTypeName = *typeName;
               stmt->emitKeyArgs = std::move(keyArgs);
               return stmt;
            }

            // Ordinary local assignment (graph domain has no P/N/uv/Cd/attrib/state)
            if (assign->lvalue->kind != AstKind::Ident)
            {
               error.severity = Severity::Error;
               error.span = assign->lvalue->span;
               error.message = "a graph kernel local must be a plain name (no swizzle targets)";
               return nullptr;
            }
            auto id = std::static_pointer_cast<AstIdent>(assign->lvalue);
            std::string targetName = id->name;
            const VarSymbol* sym = scope.Find(targetName);
            if (sym && sym->isProvisional) sym = nullptr;

            FieldType lvalType;
            Domain lvalDomain = Domain::Graph;
            if (sym)
            {
               if (sym->isHandle)
               {
                  error.severity = Severity::Error;
                  error.span = id->span;
                  error.message = "'" + targetName + "' is a handle bound by emit(); it cannot be reassigned as an ordinary value";
                  return nullptr;
               }
               if (sym->isReadOnly)
               {
                  error.severity = Severity::Error;
                  error.span = id->span;
                  // doc §5.5.3: writing a global would break ExprGlobals'
                  // "a global can only reference globals above it" guarantee,
                  // which is what makes a value cycle among globals
                  // structurally impossible rather than something to detect
                  // at runtime.
                  if (sym->isGlobal)
                     error.message = "a graph kernel cannot assign to the global '" + targetName +
                        "'. Globals are evaluated top-down so that a cycle is structurally impossible; "
                        "letting a kernel rewrite the list would break that. Fix: emit a node and set "
                        "its param instead, or edit the global in the Globals window.";
                  else
                     error.message = "cannot assign to read-only variable '" + targetName + "'";
                  return nullptr;
               }
               lvalType = sym->type;
               lvalDomain = sym->domain;
            }
            else if (assign->op != "=")
            {
               error.severity = Severity::Error;
               error.span = id->span;
               error.message = "use of undeclared identifier '" + targetName + "'";
               return nullptr;
            }
            else
            {
               lvalType = FieldType(DataType::Void, 0);
            }

            auto rhsIR = LowerAstExpr(assign->rvalue, scope, error);
            if (!rhsIR) return nullptr;
            if (rhsIR->isHandle)
            {
               error.severity = Severity::Error;
               error.span = assign->rvalue->span;
               error.message = "a handle can only be bound directly by 'name = emit(...)', not copied to another name";
               return nullptr;
            }

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
            else if (assign->op == "=")
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

            bool compatible = true;
            Domain stmtDomain = JoinDomains(lvalDomain, rhsIR->domain, compatible);
            if (!compatible)
            {
               error = MakeIncomparableDomainError(lvalDomain, assign->lvalue->span, rhsIR->domain, assign->rvalue->span, "assignment to '" + targetName + "'");
               return nullptr;
            }

            auto stmt = std::make_shared<IRStmt>(IRStmtKind::Assign, assign->span);
            stmt->domain = stmtDomain;
            stmt->assignTarget = targetName;
            stmt->assignOp = assign->op;
            stmt->rvalueExpr = rhsIR;
            return stmt;
         }

         if (ast->kind == AstKind::Call)
         {
            auto call = std::static_pointer_cast<AstCall>(ast);

            if (call->callee == "connect")
            {
               if (call->args.size() != 4)
               {
                  error.severity = Severity::Error;
                  error.span = call->span;
                  error.message = "connect() expects 4 arguments: connect(srcHandle, srcSlot, dstHandle, dstSlot)";
                  return nullptr;
               }
               auto srcIR = requireHandle(call->args[0], "connect()'s source argument");
               if (!srcIR) return nullptr;
               auto srcSlotIR = LowerAstExpr(call->args[1], scope, error);
               if (!srcSlotIR) return nullptr;
               auto dstIR = requireHandle(call->args[2], "connect()'s destination argument");
               if (!dstIR) return nullptr;
               auto dstSlotIR = LowerAstExpr(call->args[3], scope, error);
               if (!dstSlotIR) return nullptr;

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::Connect, call->span);
               stmt->domain = Domain::Graph;
               stmt->connectSrc = srcIR;
               stmt->connectSrcSlot = srcSlotIR;
               stmt->connectDst = dstIR;
               stmt->connectDstSlot = dstSlotIR;
               return stmt;
            }

            if (call->callee == "set")
            {
               if (call->args.size() != 3)
               {
                  error.severity = Severity::Error;
                  error.span = call->span;
                  error.message = "set() expects 3 arguments: set(handle, \"paramName\", value)";
                  return nullptr;
               }
               auto targetIR = requireHandle(call->args[0], "set()'s target argument");
               if (!targetIR) return nullptr;
               const std::string* paramName = requireLiteralString(call->args[1], "set()'s second argument (param name)");
               if (!paramName) return nullptr;
               auto valueIR = LowerAstExpr(call->args[2], scope, error);
               if (!valueIR) return nullptr;

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::SetParam, call->span);
               stmt->domain = Domain::Graph;
               stmt->setTarget = targetIR;
               stmt->setParamName = *paramName;
               stmt->setValue = valueIR;
               return stmt;
            }

            if (call->callee == "place")
            {
               if (call->args.size() != 3)
               {
                  error.severity = Severity::Error;
                  error.span = call->span;
                  error.message = "place() expects 3 arguments: place(handle, x, y)";
                  return nullptr;
               }
               auto targetIR = requireHandle(call->args[0], "place()'s target argument");
               if (!targetIR) return nullptr;
               auto xIR = LowerAstExpr(call->args[1], scope, error);
               if (!xIR) return nullptr;
               auto yIR = LowerAstExpr(call->args[2], scope, error);
               if (!yIR) return nullptr;

               auto stmt = std::make_shared<IRStmt>(IRStmtKind::Place, call->span);
               stmt->domain = Domain::Graph;
               stmt->placeTarget = targetIR;
               stmt->placeX = xIR;
               stmt->placeY = yIR;
               return stmt;
            }
         }

         if (ast->kind == AstKind::If)
         {
            auto ifAst = std::static_pointer_cast<AstIf>(ast);
            auto condIR = LowerAstExpr(ifAst->cond, scope, error);
            if (!condIR) return nullptr;
            if (condIR->type.kind != DataType::Bool)
            {
               error.severity = Severity::Error;
               error.span = ifAst->cond->span;
               error.message = "if condition must be a bool expression (got " + std::string(condIR->type.ToString()) + ")";
               return nullptr;
            }
            if (condIR->domain != Domain::Graph)
            {
               error.severity = Severity::Error;
               error.span = ifAst->cond->span;
               error.message = "if condition must be a compile-time (graph-domain) value (graph domain has no coarser domain to fall back to)";
               return nullptr;
            }

            auto lowerBlock = [&](const AstNodePtr& blockAst, std::vector<IRStmtPtr>& out) -> bool {
               if (!blockAst) return true;
               if (blockAst->kind == AstKind::Block)
               {
                  auto blk = std::static_pointer_cast<AstBlock>(blockAst);
                  for (const auto& s : blk->statements)
                  {
                     auto irS = LowerGraphStmt(s, scope, error);
                     if (!irS) return false;
                     out.push_back(irS);
                  }
               }
               else
               {
                  auto irS = LowerGraphStmt(blockAst, scope, error);
                  if (!irS) return false;
                  out.push_back(irS);
               }
               return true;
            };

            std::vector<IRStmtPtr> thenStmts, elseStmts;
            if (!lowerBlock(ifAst->thenBlock, thenStmts)) return nullptr;
            if (!lowerBlock(ifAst->elseBlock, elseStmts)) return nullptr;

            auto stmt = std::make_shared<IRStmt>(IRStmtKind::If, ifAst->span);
            stmt->domain = condIR->domain;
            stmt->ifCond = condIR;
            stmt->thenStmts = std::move(thenStmts);
            stmt->elseStmts = std::move(elseStmts);

            // emit() IS allowed inside if - it is the sanctioned way to make
            // a kernel's node count depend on a param (doc §5.1/T5: a
            // param-dependent *loop bound* is refused because it makes the
            // plan unbounded, but "literal bound + if guard" is the doc's
            // own prescribed fix, used throughout its worked examples). The
            // if's condition is already required to be graph-domain (checked
            // above), and each emit's key path is still the loop variable,
            // so the if does not make any key non-statically-enumerable -
            // it only decides, per key, whether that iteration's emit runs.
            return stmt;
         }

         if (ast->kind == AstKind::For)
         {
            auto forAst = std::static_pointer_cast<AstFor>(ast);

            IRStmtPtr initStmt = nullptr;
            if (forAst->init)
            {
               initStmt = LowerGraphStmt(forAst->init, scope, error);
               if (!initStmt) return nullptr;
            }

            IRNodePtr condIR = nullptr;
            if (forAst->cond)
            {
               condIR = LowerAstExpr(forAst->cond, scope, error);
               if (!condIR) return nullptr;
            }

            IRStmtPtr stepStmt = nullptr;
            if (forAst->step)
            {
               stepStmt = LowerGraphStmt(forAst->step, scope, error);
               if (!stepStmt) return nullptr;
            }

            std::vector<IRStmtPtr> bodyStmts;
            if (forAst->body)
            {
               struct LoopDepthGuard
               {
                  int& depth;
                  LoopDepthGuard(int& d) : depth(d) { depth++; }
                  ~LoopDepthGuard() { depth--; }
               } guard(scope.graphLoopDepth);

               if (forAst->body->kind == AstKind::Block)
               {
                  auto blk = std::static_pointer_cast<AstBlock>(forAst->body);
                  for (const auto& s : blk->statements)
                  {
                     auto irS = LowerGraphStmt(s, scope, error);
                     if (!irS) return nullptr;
                     bodyStmts.push_back(irS);
                  }
               }
               else
               {
                  auto irS = LowerGraphStmt(forAst->body, scope, error);
                  if (!irS) return nullptr;
                  bodyStmts.push_back(irS);
               }
            }

            auto stmt = std::make_shared<IRStmt>(IRStmtKind::For, forAst->span);
            stmt->domain = Domain::Graph;
            stmt->forInit = initStmt;
            stmt->forCond = condIR;
            stmt->forStep = stepStmt;
            stmt->forBody = std::move(bodyStmts);

            // emit() inside for IS the doc's canonical pattern (§5.1's worked
            // example: `for (i = 0; i < 8; i++) { if (i < voices) { osc =
            // emit("Wavetable", i) ... } } }`), not something to refuse. What
            // §5.3.2 actually refuses is narrower and already enforced at
            // the Emit case itself: an emit with no key path inside a loop,
            // or a key path that is not the loop variable (both would make
            // every iteration collapse onto the same key). The loop bound
            // being a literal constant (checked above) is what keeps the
            // key set statically enumerable - the loop body's shape doesn't
            // need any further restriction.
            return stmt;
         }

         error.severity = Severity::Error;
         error.span = ast->span;
         error.message = "unsupported statement in graph kernel (only assignment, if, for, emit()/connect()/set()/place() are allowed - no attrib/state/map)";
         return nullptr;
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
         // Build step 23: cooks since this node's state bank was cleared, so a
         // simulation can seed itself exactly once. `frame` cannot do that job -
         // it is the global cook counter and is already in the thousands by the
         // time a node is spawned.
         sym.name = "age"; sym.type = FieldType(DataType::Float, 1); sym.domain = Domain::Frame; sym.isReserved = true; sym.isReadOnly = true; scope.Add(sym);
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
         else if (s->kind == AstKind::DeclOutput)
            declaredNames.insert(std::static_pointer_cast<AstDeclOutput>(s)->name);
         else if (s->kind == AstKind::DeclInput)
            declaredNames.insert(std::static_pointer_cast<AstDeclInput>(s)->name);
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
         if (s->kind == AstKind::Map)
         {
            auto mapAst = std::static_pointer_cast<AstMap>(s);
            if (!LowerMapStatement(mapAst, scope, outProgram, irStmts, outError))
               return false;
            continue;
         }

         if (s->kind == AstKind::Assign)
         {
            auto assign = std::static_pointer_cast<AstAssign>(s);
            if (assign->rvalue && assign->rvalue->kind == AstKind::Call)
            {
               auto call = std::static_pointer_cast<AstCall>(assign->rvalue);
               if (call->callee == "downsample" && call->args.size() == 2)
               {
                  auto xIR = LowerAstExpr(call->args[0], scope, outError);
                  if (!outError.Empty() || !xIR) return false;

                  if (call->args[1]->kind != AstKind::Literal)
                  {
                     std::string nonConst;
                     if (call->args[1]->kind == AstKind::Ident)
                        nonConst = std::static_pointer_cast<AstIdent>(call->args[1])->name;
                     else
                        nonConst = "expression";

                     outError.severity = Severity::Error;
                     outError.span = call->args[1]->span;
                     outError.message = "downsample factor k must be a compile-time constant integer literal >= 1 (got non-constant '" + nonConst + "')";
                     return false;
                  }

                  int k = (int)std::static_pointer_cast<AstLiteral>(call->args[1])->numberValue;
                  if (!ValidateDownsample(k, call->args[1]->span, outError))
                     return false;

                  std::string targetVar = "ds";
                  if (assign->lvalue->kind == AstKind::Ident)
                     targetVar = std::static_pointer_cast<AstIdent>(assign->lvalue)->name;
                  std::string holdName = "__ds_hold_" + targetVar + "_L" + std::to_string(s->span.line) + "_C" + std::to_string(s->span.col);

                  DeclaredState ds;
                  ds.name = holdName;
                  ds.typeName = xIR->type.ToString();
                  ds.type = xIR->type.kind;
                  ds.lanes = xIR->type.lanes;
                  ds.domain = xIR->domain;
                  ds.span = s->span;
                  ds.initialValues.resize(xIR->type.lanes, 0.0f);
                  outProgram.declaredStates.push_back(ds);

                  VarSymbol stateSym;
                  stateSym.name = holdName;
                  stateSym.type = xIR->type;
                  stateSym.domain = xIR->domain;
                  stateSym.isState = true;
                  scope.Add(stateSym);

                  auto timeVarNode = std::make_shared<IRNode>(IRKind::Variable, s->span);
                  timeVarNode->varName = "frame";
                  timeVarNode->type = FieldType(DataType::Float, 1);
                  timeVarNode->domain = xIR->domain;

                  auto kLitNode = std::make_shared<IRNode>(IRKind::Literal, call->args[1]->span);
                  kLitNode->type = FieldType(DataType::Float, 1);
                  kLitNode->domain = Domain::Graph;
                  kLitNode->numberValue = (double)k;
                  kLitNode->vecValues[0] = (double)k;

                  auto modCall = std::make_shared<IRNode>(IRKind::Call, s->span);
                  modCall->callee = "mod";
                  modCall->type = FieldType(DataType::Float, 1);
                  modCall->domain = xIR->domain;
                  modCall->children.push_back(timeVarNode);
                  modCall->children.push_back(kLitNode);

                  auto zeroLit = std::make_shared<IRNode>(IRKind::Literal, s->span);
                  zeroLit->type = FieldType(DataType::Float, 1);
                  zeroLit->domain = Domain::Graph;
                  zeroLit->numberValue = 0.0;
                  zeroLit->vecValues[0] = 0.0;

                  auto gateCond = std::make_shared<IRNode>(IRKind::Binary, s->span);
                  gateCond->op = "==";
                  gateCond->type = FieldType(DataType::Bool, 1);
                  gateCond->domain = xIR->domain;
                  gateCond->children.push_back(modCall);
                  gateCond->children.push_back(zeroLit);

                  auto initRead = std::make_shared<IRNode>(IRKind::StateRead, s->span);
                  initRead->varName = holdName;
                  initRead->type = xIR->type;
                  initRead->domain = xIR->domain;

                  auto initAssign = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
                  initAssign->domain = xIR->domain;
                  initAssign->assignTarget = holdName;
                  initAssign->assignOp = "=";
                  initAssign->rvalueExpr = initRead;
                  irStmts.push_back(initAssign);

                  auto holdAssign = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
                  holdAssign->domain = xIR->domain;
                  holdAssign->assignTarget = holdName;
                  holdAssign->assignOp = "=";
                  holdAssign->rvalueExpr = xIR;

                  auto gateIf = std::make_shared<IRStmt>(IRStmtKind::If, s->span);
                  gateIf->domain = xIR->domain;
                  gateIf->ifCond = gateCond;
                  gateIf->thenStmts.push_back(holdAssign);
                  irStmts.push_back(gateIf);

                  auto stateRead = std::make_shared<IRNode>(IRKind::StateRead, s->span);
                  stateRead->varName = holdName;
                  stateRead->type = xIR->type;
                  stateRead->domain = xIR->domain;

                  std::string targetName;
                  if (assign->lvalue->kind == AstKind::Ident)
                     targetName = std::static_pointer_cast<AstIdent>(assign->lvalue)->name;

                  auto finalAssign = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
                  finalAssign->domain = xIR->domain;
                  finalAssign->assignTarget = targetName;
                  finalAssign->assignOp = assign->op;
                  finalAssign->rvalueExpr = stateRead;

                  if (targetName == "P") outProgram.writeMask.wroteP = true;
                  else if (targetName == "N") outProgram.writeMask.wroteN = true;
                  else if (targetName == "uv") outProgram.writeMask.wroteUv = true;
                  else if (targetName == "Cd") outProgram.writeMask.wroteCd = true;
                  else
                  {
                     const VarSymbol* sym = scope.Find(targetName);
                     if (sym && sym->isAttrib && !outProgram.writeMask.WroteAttrib(targetName))
                        outProgram.writeMask.wroteAttribs.push_back(targetName);
                  }

                  irStmts.push_back(finalAssign);
                  continue;
               }
            }
         }

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
         // res and aspect are Pixel-domain, not Frame. They are frame-constant
         // in value, but marking them Frame let rate inference hoist any
         // expression mentioning them - `d = 1.0 / res` - into the CPU-side
         // prologue, and the prologue evaluator only knows t/dt/frame/params.
         // So `res` evaluated to 0 there and every kernel that measured a
         // texel silently got a texel size of zero (a hoisted vec2 was also
         // being uploaded with glUniform1f, which cannot carry two lanes).
         // In GLSL both are bound from fld_res inside main() at no cost, so
         // keeping them per-pixel is free and correct.
         addSym("res", FieldType(DataType::Vec2, 2), Domain::Pixel, true);
         addSym("aspect", FieldType(DataType::Float, 1), Domain::Pixel, true);
         // Pixel-domain like res: bound from a uniform inside main(), never
         // hoisted, because the CPU prologue evaluator does not know it.
         addSym("age", FieldType(DataType::Float, 1), Domain::Pixel, true);
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
         else if (s->kind == AstKind::DeclOutput)
            declaredNames.insert(std::static_pointer_cast<AstDeclOutput>(s)->name);
         else if (s->kind == AstKind::DeclInput)
            declaredNames.insert(std::static_pointer_cast<AstDeclInput>(s)->name);
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
            st.boundary = BoundaryModeFromString(ds->boundary);
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
         else if (s->kind == AstKind::DeclOutput || s->kind == AstKind::DeclInput)
         {
            bool isOutput = (s->kind == AstKind::DeclOutput);
            std::string domainName = isOutput ? std::static_pointer_cast<AstDeclOutput>(s)->domainName
                                               : std::static_pointer_cast<AstDeclInput>(s)->domainName;
            std::string typeName = isOutput ? std::static_pointer_cast<AstDeclOutput>(s)->typeName
                                             : std::static_pointer_cast<AstDeclInput>(s)->typeName;
            std::string pinName = isOutput ? std::static_pointer_cast<AstDeclOutput>(s)->name
                                            : std::static_pointer_cast<AstDeclInput>(s)->name;
            AstNodePtr initExpr = isOutput ? std::static_pointer_cast<AstDeclOutput>(s)->initExpr : nullptr;

            Domain declaredDomain;
            if (!DomainFromString(domainName, declaredDomain))
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "unknown domain '" + domainName + "' in " + std::string(isOutput ? "output" : "input") + " declaration";
               return false;
            }
            if (declaredDomain == Domain::Graph)
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = std::string("'") + (isOutput ? "output" : "input") + "' cannot declare a graph-domain pin (graph domain has no per-frame dataflow)";
               return false;
            }
            if (isOutput && typeName == "geometry")
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "'geometry' is an input-only structural type; use it with 'input', not 'output'";
               return false;
            }
            if (scope.Has(pinName))
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "duplicate declaration of '" + pinName + "'";
               return false;
            }
            if (outProgram.declaredOutputs.size() + outProgram.declaredInputs.size() >= kFieldMaxDeclaredPinsPerProgram)
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "declared pin count exceeds the " + std::to_string(kFieldMaxDeclaredPinsPerProgram) + "-pin-per-kernel ceiling (build step 12 v1 limit)";
               return false;
            }

            // S5.6/S5.8 row 4: each structural pin type has exactly one
            // legal domain.
            if (!isOutput && typeName == "geometry" && declaredDomain != Domain::Element)
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "'geometry' pins are only legal in the 'element' domain (got '" + domainName +
                                    "'); a cross-domain geometry read would need an explicit resample, not supported in v1";
               return false;
            }
            if (typeName == "audio" && declaredDomain != Domain::Sample)
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "'audio' pins are only legal in the 'sample' domain (got '" + domainName +
                                    "'); a cross-domain audio read would need an explicit resample, not supported in v1";
               return false;
            }
            if (typeName == "image" && declaredDomain != Domain::Pixel)
            {
               outError.severity = Severity::Error;
               outError.span = s->span;
               outError.message = "'image' pins are only legal in the 'pixel' domain (got '" + domainName +
                                    "'); a cross-domain image read would need an explicit resample, not supported in v1";
               return false;
            }

            bool isStructural = (typeName == "audio" || typeName == "image" || (!isOutput && typeName == "geometry"));
            DataType dt = DataType::Float;
            int lanes = 1;
            if (isStructural)
            {
               if (typeName == "audio") { dt = DataType::Float; lanes = 1; }
               else if (typeName == "image") { dt = DataType::Vec4; lanes = 4; }
               // "geometry" (input-only): no FieldType, dt/lanes unused.
            }
            else
            {
               dt = ParseDataType(typeName);
               if (dt == DataType::Void)
               {
                  outError.severity = Severity::Error;
                  outError.span = s->span;
                  outError.message = "unknown type '" + typeName + "' in " + std::string(isOutput ? "output" : "input") + " declaration";
                  return false;
               }
               lanes = FieldType::GetLanesForType(dt);
            }

            if (isOutput)
            {
               if (!initExpr)
               {
                  outError.severity = Severity::Error;
                  outError.span = s->span;
                  outError.message = "output '" + pinName + "' requires an initializer expression";
                  return false;
               }
               IRNodePtr initIR = LowerAstExpr(initExpr, scope, outError);
               if (!initIR) return false;

               bool isFiner = false;
               if (!CheckPinDomainOk(initIR->domain, declaredDomain, isFiner))
               {
                  if (isFiner)
                  {
                     outError.severity = Severity::Error;
                     outError.span = initExpr->span;
                     outError.message = "output '" + pinName + "' is declared " + std::string(DomainToString(declaredDomain)) +
                                          "-domain but its expression is " + std::string(DomainToString(initIR->domain)) +
                                          "-domain (finer); wrap it in an explicit reduce/resample to coarsen it first";
                     outError.hint = "e.g. reduce.mean(x) or reduce.rms(x)";
                     return false;
                  }
                  outError = MakeIncomparableDomainError(initIR->domain, initExpr->span, declaredDomain, s->span,
                                                         "output '" + pinName + "' declaration");
                  return false;
               }

               DeclaredOutput out;
               out.name = pinName; out.typeName = typeName; out.isStructural = isStructural;
               out.type = dt; out.lanes = lanes; out.domain = declaredDomain; out.span = s->span;
               outProgram.declaredOutputs.push_back(out);
            }
            else
            {
               DeclaredInput in;
               in.name = pinName; in.typeName = typeName; in.isStructural = isStructural;
               in.type = dt; in.lanes = lanes; in.domain = declaredDomain; in.span = s->span;
               outProgram.declaredInputs.push_back(in);
            }

            VarSymbol sym;
            sym.name = pinName;
            sym.type = FieldType(dt, lanes);
            sym.domain = declaredDomain;
            sym.isOutputPin = isOutput;
            sym.isInputPin = !isOutput;
            sym.isReadOnly = true;
            sym.isStructuralGeometry = (!isOutput && typeName == "geometry");
            scope.Add(sym);
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

         if (s->kind == AstKind::Map)
         {
            auto mapAst = std::static_pointer_cast<AstMap>(s);
            if (!mapAst->countExpr)
            {
               outError.severity = Severity::Error;
               outError.span = mapAst->span;
               outError.message = "map() requires an element count expression, e.g. map(8) { ... }";
               return false;
            }
            std::string nonConst;
            if (!IsLiteralConstant(mapAst->countExpr, nonConst))
            {
               outError.severity = Severity::Error;
               outError.span = mapAst->countExpr->span;
               outError.message = "map() element count must be a compile-time constant integer (got non-constant '" + nonConst + "')";
               return false;
            }
            int count = (int)std::static_pointer_cast<AstLiteral>(mapAst->countExpr)->numberValue;
            if (count < 1 || count > 64)
            {
               outError.severity = Severity::Error;
               outError.span = mapAst->countExpr->span;
               outError.message = "map() element count must be between 1 and 64 (got " + std::to_string(count) + ")";
               return false;
            }
            if (mapAst->body)
            {
               std::vector<AstNodePtr> bodyStmts;
               if (mapAst->body->kind == AstKind::Block)
                  bodyStmts = std::static_pointer_cast<AstBlock>(mapAst->body)->statements;
               else
                  bodyStmts.push_back(mapAst->body);

               std::vector<std::shared_ptr<AstDeclState>> declaredStatesInMap;
               for (const auto& bs : bodyStmts)
                  if (bs->kind == AstKind::DeclState)
                     declaredStatesInMap.push_back(std::static_pointer_cast<AstDeclState>(bs));

               for (const auto& ds : declaredStatesInMap)
               {
                  DataType dt = ParseDataType(ds->typeName);
                  int lanes = FieldType::GetLanesForType(dt);
                  std::vector<float> initVals(lanes, 0.0f);
                  for (int k = 0; k < count; ++k)
                  {
                     std::string instName = ds->name + "__m" + std::to_string(k);
                     DeclaredState st;
                     st.name = instName;
                     st.typeName = ds->typeName;
                     st.type = dt;
                     st.lanes = lanes;
                     st.initialValues = initVals;
                     st.domain = Domain::Pixel;
                     st.span = ds->span;
                     outProgram.declaredStates.push_back(st);

                     VarSymbol sym;
                     sym.name = instName;
                     sym.type = FieldType(dt, lanes);
                     sym.domain = Domain::Pixel;
                     sym.isState = true;
                     scope.Add(sym);
                  }
               }

               for (int k = 0; k < count; ++k)
               {
                  std::unordered_map<std::string, std::string> renames;
                  for (const auto& ds : declaredStatesInMap)
                     renames[ds->name] = ds->name + "__m" + std::to_string(k);

                  for (const auto& bs : bodyStmts)
                  {
                     // Already registered into scope and outProgram.declaredStates
                     // above, once per instance; re-lowering it here would re-declare
                     // the same name and fail with "duplicate declaration".
                     if (bs->kind == AstKind::DeclState) continue;

                     auto cloned = CloneAstForMap(bs, renames, k);
                     auto irS = LowerAstStmt(cloned, scope, dummyElemProg, outError);
                     if (!outError.Empty()) return false;
                     if (irS)
                     {
                        if (!ValidateMap(Domain::Pixel, irS->domain, mapAst->span, outError))
                           return false;
                        irStmts.push_back(irS);
                     }
                  }
               }
            }
            continue;
         }

         if (s->kind == AstKind::Assign)
         {
            auto assign = std::static_pointer_cast<AstAssign>(s);
            if (assign->rvalue && assign->rvalue->kind == AstKind::Call)
            {
               auto call = std::static_pointer_cast<AstCall>(assign->rvalue);
               if (call->callee == "downsample" && call->args.size() == 2)
               {
                  auto xIR = LowerAstExpr(call->args[0], scope, outError);
                  if (!outError.Empty() || !xIR) return false;

                  if (call->args[1]->kind != AstKind::Literal)
                  {
                     std::string nonConst;
                     if (call->args[1]->kind == AstKind::Ident)
                        nonConst = std::static_pointer_cast<AstIdent>(call->args[1])->name;
                     else
                        nonConst = "expression";

                     outError.severity = Severity::Error;
                     outError.span = call->args[1]->span;
                     outError.message = "downsample factor k must be a compile-time constant integer literal >= 1 (got non-constant '" + nonConst + "')";
                     return false;
                  }

                  int k = (int)std::static_pointer_cast<AstLiteral>(call->args[1])->numberValue;
                  if (!ValidateDownsample(k, call->args[1]->span, outError))
                     return false;

                  std::string targetVar = "ds";
                  if (assign->lvalue->kind == AstKind::Ident)
                     targetVar = std::static_pointer_cast<AstIdent>(assign->lvalue)->name;
                  std::string holdName = "__ds_hold_" + targetVar + "_L" + std::to_string(s->span.line) + "_C" + std::to_string(s->span.col);

                  DeclaredState ds;
                  ds.name = holdName;
                  ds.typeName = xIR->type.ToString();
                  ds.type = xIR->type.kind;
                  ds.lanes = xIR->type.lanes;
                  ds.domain = xIR->domain;
                  ds.span = s->span;
                  ds.initialValues.resize(xIR->type.lanes, 0.0f);
                  outProgram.declaredStates.push_back(ds);

                  VarSymbol stateSym;
                  stateSym.name = holdName;
                  stateSym.type = xIR->type;
                  stateSym.domain = xIR->domain;
                  stateSym.isState = true;
                  scope.Add(stateSym);

                  auto timeVarNode = std::make_shared<IRNode>(IRKind::Variable, s->span);
                  timeVarNode->varName = "frame";
                  timeVarNode->type = FieldType(DataType::Float, 1);
                  timeVarNode->domain = Domain::Frame;

                  auto kLitNode = std::make_shared<IRNode>(IRKind::Literal, call->args[1]->span);
                  kLitNode->type = FieldType(DataType::Float, 1);
                  kLitNode->domain = Domain::Graph;
                  kLitNode->numberValue = (double)k;
                  kLitNode->vecValues[0] = (double)k;

                  auto modCall = std::make_shared<IRNode>(IRKind::Call, s->span);
                  modCall->callee = "mod";
                  modCall->type = FieldType(DataType::Float, 1);
                  modCall->domain = Domain::Frame;
                  modCall->children.push_back(timeVarNode);
                  modCall->children.push_back(kLitNode);

                  auto zeroLit = std::make_shared<IRNode>(IRKind::Literal, s->span);
                  zeroLit->type = FieldType(DataType::Float, 1);
                  zeroLit->domain = Domain::Graph;
                  zeroLit->numberValue = 0.0;
                  zeroLit->vecValues[0] = 0.0;

                  auto gateCond = std::make_shared<IRNode>(IRKind::Binary, s->span);
                  gateCond->op = "==";
                  gateCond->type = FieldType(DataType::Bool, 1);
                  gateCond->domain = Domain::Frame;
                  gateCond->children.push_back(modCall);
                  gateCond->children.push_back(zeroLit);

                  auto initRead = std::make_shared<IRNode>(IRKind::StateRead, s->span);
                  initRead->varName = holdName;
                  initRead->type = xIR->type;
                  initRead->domain = xIR->domain;

                  auto initAssign = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
                  initAssign->domain = xIR->domain;
                  initAssign->assignTarget = holdName;
                  initAssign->assignOp = "=";
                  initAssign->rvalueExpr = initRead;
                  irStmts.push_back(initAssign);

                  auto holdAssign = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
                  holdAssign->domain = xIR->domain;
                  holdAssign->assignTarget = holdName;
                  holdAssign->assignOp = "=";
                  holdAssign->rvalueExpr = xIR;

                  auto gateIf = std::make_shared<IRStmt>(IRStmtKind::If, s->span);
                  gateIf->domain = xIR->domain;
                  gateIf->ifCond = gateCond;
                  gateIf->thenStmts.push_back(holdAssign);
                  irStmts.push_back(gateIf);

                  auto stateRead = std::make_shared<IRNode>(IRKind::StateRead, s->span);
                  stateRead->varName = holdName;
                  stateRead->type = xIR->type;
                  stateRead->domain = xIR->domain;

                  std::string targetName;
                  if (assign->lvalue->kind == AstKind::Ident)
                     targetName = std::static_pointer_cast<AstIdent>(assign->lvalue)->name;

                  auto finalAssign = std::make_shared<IRStmt>(IRStmtKind::Assign, s->span);
                  finalAssign->domain = xIR->domain;
                  finalAssign->assignTarget = targetName;
                  finalAssign->assignOp = assign->op;
                  finalAssign->rvalueExpr = stateRead;
                  irStmts.push_back(finalAssign);
                  continue;
               }
            }
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
      //
      // Only SCALARS are hoisted. A hoisted value crosses into the shader as
      // one `uniform float` uploaded with glUniform1f, so a hoisted vec2/3/4
      // arrived with lane 0 set and every other lane left at zero - which is
      // why `e = vec2(0.22 * cos(t), 0.22 * sin(t))` sat at the origin
      // instead of orbiting. Keeping multi-lane frame-domain values in the
      // pixel body costs a few ALU per pixel and is always correct; making
      // them a real vec2/3/4 uniform is the alternative and needs the CPU
      // prologue evaluator to return vectors, which it does not.
      for (const auto& s : irStmts)
      {
         const bool coarse = (s->domain == Domain::Graph || s->domain == Domain::Frame);
         const bool scalar = !s->rvalueExpr || s->rvalueExpr->type.lanes == 1;
         if (coarse && scalar)
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

   namespace
   {
      // Phase 2 (step 10 doc §5.2): walks down from a non-Graph-domain node
      // to the leaf (Variable/StateRead) responsible, so the error names the
      // actual offending identifier rather than "some expression somewhere".
      bool FindNonGraphLeaf(const IRNodePtr& node, std::string& outName, Domain& outDomain)
      {
         if (!node || node->domain == Domain::Graph) return false;

         if (node->kind == IRKind::Variable || node->kind == IRKind::StateRead)
         {
            outName = node->varName.empty() ? "<value>" : node->varName;
            outDomain = node->domain;
            return true;
         }
         for (const auto& c : node->children)
         {
            if (FindNonGraphLeaf(c, outName, outDomain)) return true;
         }
         if (node->kind == IRKind::Call)
         {
            outName = node->callee.empty() ? "<call>" : (node->callee + "()");
            outDomain = node->domain;
            return true;
         }
         outName = "<expression>";
         outDomain = node->domain;
         return true;
      }

      bool CheckArgIsRateZero(const IRNodePtr& node, const std::string& where, FieldError& outError)
      {
         if (!node || node->domain == Domain::Graph) return true;

         std::string leafName;
         Domain leafDomain = Domain::Graph;
         FindNonGraphLeaf(node, leafName, leafDomain);

         outError.severity = Severity::Error;
         outError.span = node->span;
         outError.message = where + " must be a compile-time (graph-domain) value, but '" + leafName +
                             "' is " + DomainToString(leafDomain) + "-domain. Fix: the graph kernel runs once "
                             "at edit time and cannot read a per-frame/per-element/per-pixel/per-sample value - "
                             "hoist a graph-domain param or literal instead.";
         return false;
      }

      bool CheckGraphStmtRateZero(const IRStmtPtr& s, FieldError& outError)
      {
         if (!s) return true;

         switch (s->kind)
         {
            case IRStmtKind::Emit:
               for (const auto& k : s->emitKeyArgs)
                  if (!CheckArgIsRateZero(k, "emit()'s identity-key argument", outError)) return false;
               return true;
            case IRStmtKind::Connect:
               if (!CheckArgIsRateZero(s->connectSrcSlot, "connect()'s source slot", outError)) return false;
               if (!CheckArgIsRateZero(s->connectDstSlot, "connect()'s destination slot", outError)) return false;
               return true;
            case IRStmtKind::SetParam:
               if (!CheckArgIsRateZero(s->setValue, "set()'s value argument", outError)) return false;
               return true;
            case IRStmtKind::Place:
               if (!CheckArgIsRateZero(s->placeX, "place()'s x argument", outError)) return false;
               if (!CheckArgIsRateZero(s->placeY, "place()'s y argument", outError)) return false;
               return true;
            case IRStmtKind::If:
               if (!CheckArgIsRateZero(s->ifCond, "if condition", outError)) return false;
               for (const auto& t : s->thenStmts) if (!CheckGraphStmtRateZero(t, outError)) return false;
               for (const auto& e : s->elseStmts) if (!CheckGraphStmtRateZero(e, outError)) return false;
               return true;
            case IRStmtKind::For:
               if (!CheckArgIsRateZero(s->forCond, "loop bound", outError)) return false;
               for (const auto& b : s->forBody) if (!CheckGraphStmtRateZero(b, outError)) return false;
               return true;
            case IRStmtKind::Assign:
               // Deviation from the doc's "only rate-critical uses are
               // checked" design: every local in a graph kernel is required
               // to be Graph-domain, full stop. The kernel is edit-time-only
               // interpreted code with no per-frame/element/pixel/sample
               // value available to read, so a local that picked up a finer
               // domain (e.g. `x = t`) can never be given a real value at
               // interpret time - better to reject it here, at the point of
               // assignment, than defer to a confusing failure (or a silent
               // placeholder, which rule 0.4 forbids) if it's ever read.
               if (!CheckArgIsRateZero(s->rvalueExpr, "a graph kernel local", outError)) return false;
               return true;
            case IRStmtKind::Expr:
               if (!CheckArgIsRateZero(s->expr, "a graph kernel expression", outError)) return false;
               return true;
            default:
               return true;
         }
      }
   }

   bool LowerGraphProgramToIR(const AstNodePtr& ast, GraphIRProgram& outProgram, FieldError& outError)
   {
      outError.Clear();
      outProgram = GraphIRProgram{};

      if (!ast)
      {
         outError.severity = Severity::Error;
         outError.message = "empty program";
         return false;
      }

      ElementScope scope;
      scope.targetDomain = Domain::Graph;
      scope.enforceDeclaration = false;

      // Seed reserved words of every other domain, purely so a graph kernel
      // that references them gets the leaf-naming rate-zero error from the
      // Phase 2 walk below instead of a confusing "undeclared identifier".
      {
         auto addSym = [&](const std::string& name, FieldType type, Domain dom) {
            VarSymbol sym;
            sym.name = name;
            sym.type = type;
            sym.domain = dom;
            sym.isReserved = true;
            sym.isReadOnly = true;
            scope.Add(sym);
         };
         addSym("t", FieldType(DataType::Float, 1), Domain::Frame);
         addSym("dt", FieldType(DataType::Float, 1), Domain::Frame);
         addSym("frame", FieldType(DataType::Float, 1), Domain::Frame);
         addSym("count", FieldType(DataType::Int, 1), Domain::Frame);
         addSym("P", FieldType(DataType::Vec3, 3), Domain::Element);
         addSym("N", FieldType(DataType::Vec3, 3), Domain::Element);
         addSym("Cd", FieldType(DataType::Vec3, 3), Domain::Element);
         addSym("i", FieldType(DataType::Int, 1), Domain::Element);
         addSym("uv", FieldType(DataType::Vec2, 2), Domain::Pixel);
         addSym("col", FieldType(DataType::Vec3, 3), Domain::Pixel);
         addSym("in", FieldType(DataType::Float, 1), Domain::Sample);
         addSym("out", FieldType(DataType::Float, 1), Domain::Sample);
         addSym("sr", FieldType(DataType::Float, 1), Domain::Sample);
         addSym("n", FieldType(DataType::Float, 1), Domain::Sample);
         addSym("freq", FieldType(DataType::Float, 1), Domain::Sample);
         addSym("gate", FieldType(DataType::Float, 1), Domain::Sample);
      }

      // doc §5.5.2: a graph kernel may read a global, but only one whose
      // expression is constant-foldable (no t/rand/noise/sh, and every
      // global it references is itself graph-domain) - otherwise it is
      // frame-domain and reading it from a rate-zero kernel is the same
      // violation as reading `t` directly, caught by the same Phase 2 walk
      // below. ExprGlobals' own evaluation order guarantees a global can
      // only reference globals *above* it in the list, so one forward pass
      // is a complete fixpoint - no cycle handling needed.
      {
         std::unordered_set<std::string> frameDomainGlobals;
         for (const auto& g : ExprGlobals::All())
         {
            bool isFrameDomain = false;
            size_t i = 0;
            while (i < g.expr.size())
            {
               if (std::isalpha((unsigned char)g.expr[i]) || g.expr[i] == '_')
               {
                  size_t start = i;
                  while (i < g.expr.size() && (std::isalnum((unsigned char)g.expr[i]) || g.expr[i] == '_')) i++;
                  std::string token = g.expr.substr(start, i - start);
                  if (token == "t" || token == "rand" || token == "noise" || token == "sh" ||
                      frameDomainGlobals.count(token))
                  {
                     isFrameDomain = true;
                     break;
                  }
               }
               else
               {
                  i++;
               }
            }
            if (isFrameDomain)
               frameDomainGlobals.insert(g.name);

            VarSymbol sym;
            sym.name = g.name;
            sym.type = FieldType(DataType::Float, 1);
            sym.domain = isFrameDomain ? Domain::Frame : Domain::Graph;
            sym.isReadOnly = true;
            sym.isGlobal = true;
            scope.Add(sym);
         }
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

      std::vector<IRStmtPtr> irStmts;
      for (const auto& s : stmts)
      {
         if (s->kind == AstKind::DeclParam)
         {
            auto dp = std::static_pointer_cast<AstDeclParam>(s);
            if (scope.Has(dp->name) && scope.Find(dp->name) && !scope.Find(dp->name)->isReserved)
            {
               outError.severity = Severity::Error;
               outError.span = dp->span;
               outError.message = "duplicate declaration of param '" + dp->name + "'";
               return false;
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

         if (s->kind == AstKind::DeclAttrib || s->kind == AstKind::DeclState)
         {
            outError.severity = Severity::Error;
            outError.span = s->span;
            outError.message = "attrib/state declarations are not allowed in the graph domain (the kernel runs once at edit time - there is no per-frame or per-element loop to hold state across)";
            return false;
         }

         if (s->kind == AstKind::DeclOutput || s->kind == AstKind::DeclInput)
         {
            outError.severity = Severity::Error;
            outError.span = s->span;
            outError.message = "output/input declarations are not allowed in the graph domain (the kernel runs once at edit time - use emit()/connect()/set()/place() for graph-domain dataflow instead)";
            return false;
         }

         if (s->kind == AstKind::Map)
         {
            outError.severity = Severity::Error;
            outError.span = s->span;
            outError.message = "map() is not allowed in the graph domain; use 'for' with a compile-time-constant bound instead";
            return false;
         }

         auto irS = LowerGraphStmt(s, scope, outError);
         if (!irS) return false;
         irStmts.push_back(irS);
      }

      // Phase 2 (doc §5.2): rate-zero enforcement, one walk after the whole
      // program's domains are settled.
      for (const auto& s : irStmts)
      {
         if (!CheckGraphStmtRateZero(s, outError))
            return false;
      }

      outProgram.statements = std::move(irStmts);
      return true;
   }
}


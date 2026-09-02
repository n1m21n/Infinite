#include "FieldIR.h"

#include <cmath>
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
      bool ValidateAndTypeFunction(const std::string& name, size_t argCount, FieldError& error, SourceSpan span)
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
             name == "exp" || name == "sqrt" || name == "log")
         {
            return checkExact(1);
         }
         if (name == "min" || name == "max" || name == "mod" || name == "pow" || name == "step")
         {
            return checkExact(2);
         }
         if (name == "clamp" || name == "lerp" || name == "smoothstep" || name == "if")
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

      IRNodePtr LowerAst(const AstNodePtr& ast, FieldError& error)
      {
         if (!ast || !error.Empty()) return nullptr;

         switch (ast->kind)
         {
            case AstKind::Literal:
            {
               auto lit = std::static_pointer_cast<AstLiteral>(ast);
               auto ir = std::make_shared<IRNode>(IRKind::Literal, lit->span);
               ir->type = lit->isBool ? DataType::Bool : DataType::Float;
               ir->domain = Domain::Graph;
               ir->numberValue = lit->numberValue;
               return ir;
            }

            case AstKind::Ident:
            {
               auto id = std::static_pointer_cast<AstIdent>(ast);
               auto ir = std::make_shared<IRNode>(IRKind::Variable, id->span);
               ir->type = DataType::Float;
               ir->varName = id->name;
               if (id->name == "t" || id->name == "dt" || id->name == "frame")
                  ir->domain = Domain::Frame;
               else
                  ir->domain = Domain::Graph;
               return ir;
            }

            case AstKind::Unary:
            {
               auto un = std::static_pointer_cast<AstUnary>(ast);
               auto operandIR = LowerAst(un->operand, error);
               if (!operandIR) return nullptr;

               auto ir = std::make_shared<IRNode>(IRKind::Unary, un->span);
               ir->op = un->op;
               ir->type = (un->op == "!") ? DataType::Bool : operandIR->type;
               ir->domain = operandIR->domain;
               ir->children.push_back(operandIR);

               // Constant fold
               if (operandIR->kind == IRKind::Literal)
               {
                  if (un->op == "-")
                  {
                     ir->kind = IRKind::Literal;
                     ir->numberValue = -operandIR->numberValue;
                     ir->children.clear();
                  }
                  else if (un->op == "!")
                  {
                     ir->kind = IRKind::Literal;
                     ir->numberValue = (operandIR->numberValue != 0.0) ? 0.0 : 1.0;
                     ir->children.clear();
                  }
               }
               return ir;
            }

            case AstKind::Binary:
            {
               auto bin = std::static_pointer_cast<AstBinary>(ast);
               auto lhsIR = LowerAst(bin->lhs, error);
               if (!lhsIR) return nullptr;
               auto rhsIR = LowerAst(bin->rhs, error);
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

               auto ir = std::make_shared<IRNode>(IRKind::Binary, bin->span);
               ir->op = bin->op;
               ir->domain = joinedDomain;
               ir->children.push_back(lhsIR);
               ir->children.push_back(rhsIR);

               bool isComp = (bin->op == "<" || bin->op == "<=" || bin->op == ">" ||
                              bin->op == ">=" || bin->op == "==" || bin->op == "!=" ||
                              bin->op == "&&" || bin->op == "||");
               ir->type = isComp ? DataType::Bool : DataType::Float;

               return ir;
            }

            case AstKind::Call:
            {
               auto call = std::static_pointer_cast<AstCall>(ast);
               if (!ValidateAndTypeFunction(call->callee, call->args.size(), error, call->span))
               {
                  return nullptr;
               }

               auto ir = std::make_shared<IRNode>(IRKind::Call, call->span);
               ir->callee = call->callee;
               ir->type = DataType::Float;
               ir->domain = Domain::Graph;

               if (call->callee == "rand" || call->callee == "noise" || call->callee == "sh")
               {
                  ir->domain = Domain::Frame; // time-dependent
               }

               for (const auto& arg : call->args)
               {
                  auto argIR = LowerAst(arg, error);
                  if (!argIR) return nullptr;

                  bool compatible = true;
                  ir->domain = JoinDomains(ir->domain, argIR->domain, compatible);
                  if (!compatible)
                  {
                     error.severity = Severity::Error;
                     error.span = call->span;
                     error.message = "incomparable domain arguments in function call";
                     return nullptr;
                  }
                  ir->children.push_back(argIR);
               }
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
               return LowerAst(prog->statements.front(), error);
            }

            default:
               error.severity = Severity::Error;
               error.message = "unsupported AST node in expression evaluation";
               return nullptr;
         }
      }
   }

   bool LowerAstToIR(const AstNodePtr& ast, IRNodePtr& outIR, FieldError& outError)
   {
      outError.Clear();
      outIR = LowerAst(ast, outError);
      return outIR != nullptr && outError.Empty();
   }
}

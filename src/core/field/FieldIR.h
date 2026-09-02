#pragma once

#include "FieldAst.h"
#include "FieldError.h"
#include <memory>
#include <string>
#include <vector>

namespace Field
{
   enum class DataType
   {
      Void,
      Float,
      Int,
      Bool,
      Vec2,
      Vec3,
      Vec4
   };

   enum class Domain
   {
      Graph,   // Edit-time / Constants (Rate 0)
      Frame,   // 60 Hz
      Element, // 60 * N
      Pixel,   // 60 * W * H
      Sample   // 48 kHz
   };

   enum class IRKind
   {
      Literal,
      Variable,
      Unary,
      Binary,
      Call
   };

   struct IRNode;
   using IRNodePtr = std::shared_ptr<IRNode>;

   struct IRNode
   {
      IRKind kind;
      DataType type = DataType::Float;
      Domain domain = Domain::Graph;
      SourceSpan span;

      double numberValue = 0.0;
      std::string varName;
      std::string op;
      std::string callee;
      std::vector<IRNodePtr> children;

      IRNode(IRKind k, SourceSpan sp = {})
         : kind(k), span(sp) {}
   };

   Domain JoinDomains(Domain a, Domain b, bool& outCompatible);

   bool LowerAstToIR(const AstNodePtr& ast, IRNodePtr& outIR, FieldError& outError);
}

#pragma once

#include "FieldError.h"
#include <memory>
#include <string>
#include <vector>

namespace Field
{
   enum class AstKind
   {
      Program,
      DeclAttrib,
      DeclParam,
      DeclState,
      Assign,
      If,
      For,
      Block,
      Binary,
      Unary,
      Call,
      Access,
      Ident,
      Literal
   };

   struct AstNode
   {
      AstKind kind;
      SourceSpan span;

      explicit AstNode(AstKind k, SourceSpan sp = {}) : kind(k), span(sp) {}
      virtual ~AstNode() = default;
   };

   using AstNodePtr = std::shared_ptr<AstNode>;

   struct AstLiteral : public AstNode
   {
      double numberValue = 0.0;
      bool isBool = false;
      bool boolValue = false;

      AstLiteral(double val, SourceSpan sp = {})
         : AstNode(AstKind::Literal, sp), numberValue(val) {}
      AstLiteral(bool b, SourceSpan sp = {})
         : AstNode(AstKind::Literal, sp), isBool(true), boolValue(b), numberValue(b ? 1.0 : 0.0) {}
   };

   struct AstIdent : public AstNode
   {
      std::string name;

      AstIdent(std::string n, SourceSpan sp = {})
         : AstNode(AstKind::Ident, sp), name(std::move(n)) {}
   };

   struct AstAccess : public AstNode
   {
      AstNodePtr base;
      std::string field;

      AstAccess(AstNodePtr b, std::string f, SourceSpan sp = {})
         : AstNode(AstKind::Access, sp), base(std::move(b)), field(std::move(f)) {}
   };

   struct AstUnary : public AstNode
   {
      std::string op;
      AstNodePtr operand;

      AstUnary(std::string o, AstNodePtr opnd, SourceSpan sp = {})
         : AstNode(AstKind::Unary, sp), op(std::move(o)), operand(std::move(opnd)) {}
   };

   struct AstBinary : public AstNode
   {
      std::string op;
      AstNodePtr lhs;
      AstNodePtr rhs;

      AstBinary(std::string o, AstNodePtr l, AstNodePtr r, SourceSpan sp = {})
         : AstNode(AstKind::Binary, sp), op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {}
   };

   struct AstCall : public AstNode
   {
      std::string callee;
      std::vector<AstNodePtr> args;

      AstCall(std::string c, std::vector<AstNodePtr> a, SourceSpan sp = {})
         : AstNode(AstKind::Call, sp), callee(std::move(c)), args(std::move(a)) {}
   };

   struct AstAssign : public AstNode
   {
      std::string op; // "=", "+=", "-=", "*=", "/="
      AstNodePtr lvalue;
      AstNodePtr rvalue;

      AstAssign(std::string o, AstNodePtr l, AstNodePtr r, SourceSpan sp = {})
         : AstNode(AstKind::Assign, sp), op(std::move(o)), lvalue(std::move(l)), rvalue(std::move(r)) {}
   };

   struct AstBlock : public AstNode
   {
      std::vector<AstNodePtr> statements;

      AstBlock(std::vector<AstNodePtr> stmts = {}, SourceSpan sp = {})
         : AstNode(AstKind::Block, sp), statements(std::move(stmts)) {}
   };

   struct AstIf : public AstNode
   {
      AstNodePtr cond;
      AstNodePtr thenBlock;
      AstNodePtr elseBlock;

      AstIf(AstNodePtr c, AstNodePtr t, AstNodePtr e = nullptr, SourceSpan sp = {})
         : AstNode(AstKind::If, sp), cond(std::move(c)), thenBlock(std::move(t)), elseBlock(std::move(e)) {}
   };

   struct AstFor : public AstNode
   {
      AstNodePtr init;
      AstNodePtr cond;
      AstNodePtr step;
      AstNodePtr body;

      AstFor(AstNodePtr i, AstNodePtr c, AstNodePtr s, AstNodePtr b, SourceSpan sp = {})
         : AstNode(AstKind::For, sp), init(std::move(i)), cond(std::move(c)), step(std::move(s)), body(std::move(b)) {}
   };

   struct AstDeclAttrib : public AstNode
   {
      std::string typeName;
      std::string name;
      AstNodePtr initExpr;

      AstDeclAttrib(std::string t, std::string n, AstNodePtr init = nullptr, SourceSpan sp = {})
         : AstNode(AstKind::DeclAttrib, sp), typeName(std::move(t)), name(std::move(n)), initExpr(std::move(init)) {}
   };

   struct AstDeclParam : public AstNode
   {
      std::string typeName;
      std::string name;
      double defaultValue = 0.0;
      double minVal = 0.0;
      double maxVal = 1.0;
      AstNodePtr initExpr;

      AstDeclParam(std::string t, std::string n, double defVal, double mn, double mx, AstNodePtr init = nullptr, SourceSpan sp = {})
         : AstNode(AstKind::DeclParam, sp), typeName(std::move(t)), name(std::move(n)), defaultValue(defVal), minVal(mn), maxVal(mx), initExpr(std::move(init)) {}
   };

   struct AstDeclState : public AstNode
   {
      std::string typeName;
      std::string name;
      AstNodePtr initExpr;

      AstDeclState(std::string t, std::string n, AstNodePtr init = nullptr, SourceSpan sp = {})
         : AstNode(AstKind::DeclState, sp), typeName(std::move(t)), name(std::move(n)), initExpr(std::move(init)) {}
   };

   struct AstProgram : public AstNode
   {
      std::vector<AstNodePtr> statements;

      AstProgram(std::vector<AstNodePtr> stmts = {}, SourceSpan sp = {})
         : AstNode(AstKind::Program, sp), statements(std::move(stmts)) {}
   };
}

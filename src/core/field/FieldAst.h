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
      DeclOutput,
      DeclInput,
      Assign,
      If,
      For,
      Map,
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
      // D7 (step 10, graph domain): a quoted string literal. Only legal as
      // emit()'s first argument or set()'s second argument - enforced during
      // graph-program lowering, not at parse time (mirrors how e.g.
      // downsample()'s literal-only factor argument is enforced).
      bool isString = false;
      std::string stringValue;

      AstLiteral(double val, SourceSpan sp = {})
         : AstNode(AstKind::Literal, sp), numberValue(val) {}
      AstLiteral(bool b, SourceSpan sp = {})
         : AstNode(AstKind::Literal, sp), isBool(true), boolValue(b), numberValue(b ? 1.0 : 0.0) {}
      AstLiteral(std::string s, SourceSpan sp, bool /*stringTag*/)
         : AstNode(AstKind::Literal, sp), isString(true), stringValue(std::move(s)) {}
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

   struct AstMap : public AstNode
   {
      AstNodePtr countExpr;
      AstNodePtr body;

      AstMap(AstNodePtr cnt, AstNodePtr b, SourceSpan sp = {})
         : AstNode(AstKind::Map, sp), countExpr(std::move(cnt)), body(std::move(b)) {}
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
      // Build step 22 (OPEN-C): optional boundary mode written after the
      // initial value - `state float A = 1 [wrap]`. Empty means the default,
      // which is clamp. Only meaningful for a pixel-domain cell, because it
      // decides what an offset read A(uv + d) returns outside [0,1].
      std::string boundary;

      AstDeclState(std::string t, std::string n, AstNodePtr init = nullptr, SourceSpan sp = {}, std::string bnd = std::string())
         : AstNode(AstKind::DeclState, sp), typeName(std::move(t)), name(std::move(n)), initExpr(std::move(init)), boundary(std::move(bnd)) {}
   };

   // Build step 12: `output <domain> <type> <name> = <expr>` - declares a
   // stable, dynamically-listed pin exposing a value computed by this kernel.
   struct AstDeclOutput : public AstNode
   {
      std::string domainName;
      std::string typeName;
      std::string name;
      AstNodePtr initExpr;

      AstDeclOutput(std::string d, std::string t, std::string n, AstNodePtr init, SourceSpan sp = {})
         : AstNode(AstKind::DeclOutput, sp), domainName(std::move(d)), typeName(std::move(t)), name(std::move(n)), initExpr(std::move(init)) {}
   };

   // Build step 12: `input <domain> <type> <name>` - declares a stable,
   // dynamically-listed pin that other nodes may feed a value into.
   struct AstDeclInput : public AstNode
   {
      std::string domainName;
      std::string typeName;
      std::string name;

      AstDeclInput(std::string d, std::string t, std::string n, SourceSpan sp = {})
         : AstNode(AstKind::DeclInput, sp), domainName(std::move(d)), typeName(std::move(t)), name(std::move(n)) {}
   };

   struct AstProgram : public AstNode
   {
      std::vector<AstNodePtr> statements;

      AstProgram(std::vector<AstNodePtr> stmts = {}, SourceSpan sp = {})
         : AstNode(AstKind::Program, sp), statements(std::move(stmts)) {}
   };
}

#pragma once

#include "FieldAst.h"
#include "FieldError.h"
#include "FieldTypes.h"
#include "ElementStore.h"
#include "ParamTable.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Field
{
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
      Call,
      Access,
      StateRead
   };

   struct IRNode;
   using IRNodePtr = std::shared_ptr<IRNode>;

   struct IRNode
   {
      IRKind kind;
      FieldType type = FieldType(DataType::Float, 1);
      Domain domain = Domain::Graph;
      SourceSpan span;

      double numberValue = 0.0;
      double vecValues[4] = { 0.0, 0.0, 0.0, 0.0 };

      std::string varName;
      std::string op;
      std::string callee;
      std::string field;
      uint8_t swizzleIndices[4] = { 0, 0, 0, 0 };

      std::vector<IRNodePtr> children;

      IRNode(IRKind k, SourceSpan sp = {})
         : kind(k), span(sp) {}
   };

   enum class IRStmtKind
   {
      Assign,
      DeclAttrib,
      DeclState,
      StateWrite,
      If,
      For,
      Expr
   };

   struct IRStmt;
   using IRStmtPtr = std::shared_ptr<IRStmt>;

   struct DeclaredState
   {
      std::string name;
      std::string typeName;
      DataType type = DataType::Float;
      int lanes = 1;
      std::vector<float> initialValues;
      Domain domain = Domain::Element;
      SourceSpan span;
   };

   struct IRStmt
   {
      IRStmtKind kind = IRStmtKind::Expr;
      SourceSpan span;
      Domain domain = Domain::Graph;

      // Assign
      std::string assignTarget; // variable/attrib name
      std::string assignField;  // swizzle/field name if any
      std::string assignOp;     // "=", "+=", "-=", "*=", "/="
      uint8_t swizzleIndices[4] = { 0, 0, 0, 0 };
      int swizzleCount = 0;
      IRNodePtr lvalueExpr;
      IRNodePtr rvalueExpr;

      // DeclAttrib
      std::string attribName;
      DataType attribType = DataType::Float;
      std::vector<float> attribInitValues;
      IRNodePtr attribInitExpr;

      // DeclState / StateWrite
      std::string stateName;
      std::string stateTypeName;
      DataType stateType = DataType::Float;
      int stateLanes = 1;
      std::vector<float> stateInitValues;

      // If
      IRNodePtr ifCond;
      std::vector<IRStmtPtr> thenStmts;
      std::vector<IRStmtPtr> elseStmts;

      // For
      IRStmtPtr forInit;
      IRNodePtr forCond;
      IRStmtPtr forStep;
      std::vector<IRStmtPtr> forBody;
      int constantTripCount = 0;

      // Expr
      IRNodePtr expr;

      explicit IRStmt(IRStmtKind k, SourceSpan sp = {})
         : kind(k), span(sp) {}
   };

   struct ElementIRProgram
   {
      std::vector<IRStmtPtr> prologue;    // Hoisted frame/graph statements
      std::vector<IRStmtPtr> elementLoop; // Per-element statements
      MeshWriteMask writeMask;
      std::vector<std::pair<std::string, DataType>> declaredAttribs;
      std::vector<DeclaredParam> declaredParams;
      std::vector<DeclaredState> declaredStates;
      bool isTimeDependent = false;
   };

   struct PixelIRProgram
   {
      std::vector<IRStmtPtr> prologue;    // Hoisted frame/graph statements
      std::vector<IRStmtPtr> pixelBody;   // Per-pixel statements
      std::vector<DeclaredParam> declaredParams;
      std::vector<DeclaredState> declaredStates;
      bool isTimeDependent = false;
   };

   Domain JoinDomains(Domain a, Domain b, bool& outCompatible);

   // Lowers a single expression AST to typed IR
   bool LowerAstToIR(const AstNodePtr& ast, IRNodePtr& outIR, FieldError& outError);

   // Lowers an Element program AST to typed Element IR with domain inference and hoisting
   bool LowerElementProgramToIR(const AstNodePtr& ast, ElementIRProgram& outProgram, FieldError& outError);

   // Lowers a Pixel program AST to typed Pixel IR with domain inference and hoisting
   bool LowerPixelProgramToIR(const AstNodePtr& ast, PixelIRProgram& outProgram, FieldError& outError);
}


#pragma once

#include "FieldAst.h"
#include "FieldError.h"
#include "FieldTypes.h"
#include "ElementStore.h"
#include "ParamTable.h"
#include "Transfer.h"
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

      TransferKind transferKind = TransferKind::None;
      int divisor = 1;

      // Build step 22 (OPEN-C): a StateRead carrying one child is an OFFSET
      // read - `A(uv + d)` - and the child is the coordinate expression. A
      // StateRead with no children is the ordinary current-position read.
      // The offset form always reads the previous cook's cell, never a value
      // written earlier in this body, which is what keeps the pass
      // order-independent.
      bool isOffsetRead = false;

      // Build step 23 (OPEN-B): a Variable or StateRead carrying one child and
      // this flag is a NEIGHBOUR read - `P.at(i - 1)` - and the child is the
      // element-index expression, clamped to [0, count-1]. It reads the cook's
      // INPUT buffer, never a value written earlier in this loop, which is what
      // keeps the element kernel pure and data-parallel: element j's result can
      // never depend on whether element j-1 has already run.
      bool isNeighbourRead = false;

      // graph domain (step 10): true when this Variable node refers to a
      // handle bound by emit() rather than an ordinary typed value. Handles
      // have no FieldType - `type` is left at its default and unused.
      bool isHandle = false;

      // Bugfix (reduce-local-variable-storage): true for a reduce() IRNode
      // (transferKind == TransferKind::Reduce) whose bare-variable argument is
      // Element-domain and NOT one of the four reserved mesh attributes
      // (P/N/uv/Cd). Those four are already fully populated before the
      // element loop begins (ElementStore::FromMesh loads them from the input
      // mesh up front), so reducing them can run in the ordinary prologue
      // exactly as it always has. Any other bare name - a plain local, or one
      // declared with `attrib` - only gets its real per-element data as a
      // side effect of the element loop itself running THIS cook, so any
      // statement using this reduce's result must be deferred to
      // ElementIRProgram::postLoop, which runs once, after the loop finishes.
      // Set only at reduce-lowering time in LowerAstExpr; read only by
      // LowerElementProgramToIR's prologue/postLoop partitioning.
      bool reduceNeedsPostLoop = false;

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
      DeclOutput,
      DeclInput,
      StateWrite,
      If,
      For,
      Expr,
      // graph domain (step 10) intrinsics - see FieldGraphKernel.h. Kept as
      // IRStmt variants rather than a fifth IR node type (I7).
      Emit,
      Connect,
      SetParam,
      Place
   };

   struct IRStmt;
   using IRStmtPtr = std::shared_ptr<IRStmt>;

   // Build step 12: `output <domain> <type> <name> = <expr>` /
   // `input <domain> <type> <name>`. `typeName` is the raw source-level type
   // word, e.g. "float", "geometry", "audio", "image"; `isStructural` is
   // true for the three reserved structural type-names, which have no
   // DataType (type/lanes are left at their defaults and unused in that
   // case) - see step-12-dynamic-pins-ir.md S1.5/S5.6.
   struct DeclaredOutput
   {
      std::string name;
      std::string typeName;
      bool isStructural = false;
      DataType type = DataType::Float;
      int lanes = 1;
      Domain domain = Domain::Element;
      SourceSpan span;
   };

   struct DeclaredInput
   {
      std::string name;
      std::string typeName;
      bool isStructural = false;
      DataType type = DataType::Float;
      int lanes = 1;
      Domain domain = Domain::Element;
      SourceSpan span;
   };

   // Build step 22 (OPEN-C): what an offset read of a pixel state cell
   // returns outside [0,1]. Per-cell, not global - reaction-diffusion under
   // Wrap tiles seamlessly and under Clamp piles up at the border, and those
   // are two different pictures, not two spellings of one.
   enum class BoundaryMode : int
   {
      Clamp,  // default; matches every shipping Feedback/Trails node
      Wrap,
      Border  // outside reads the cell's declared initial value
   };

   struct DeclaredState
   {
      std::string name;
      std::string typeName;
      DataType type = DataType::Float;
      int lanes = 1;
      std::vector<float> initialValues;
      Domain domain = Domain::Element;
      BoundaryMode boundary = BoundaryMode::Clamp;
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

      // DeclOutput / DeclInput (build step 12)
      std::string pinName;
      std::string pinTypeName;
      bool pinIsStructural = false;
      DataType pinType = DataType::Float;
      int pinLanes = 1;
      Domain pinDomain = Domain::Element;
      IRNodePtr pinInitExpr; // DeclOutput only

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

      // Emit: `<emitTargetName> = emit("<emitTypeName>", k0, k1, ...)`.
      // emitKeyArgs are the identity-key arguments (must all be Domain::Graph,
      // enforced by the Phase 2 rate-zero walk); emitTypeName is the literal
      // string, validated to be a spawnable node type at interpret time
      // (FieldGraphKernel.cpp), not at lowering time.
      std::string emitTargetName;
      std::string emitTypeName;
      std::vector<IRNodePtr> emitKeyArgs;

      // Connect: connect(<connectSrc>, <connectSrcSlot>, <connectDst>, <connectDstSlot>)
      IRNodePtr connectSrc;
      IRNodePtr connectSrcSlot;
      IRNodePtr connectDst;
      IRNodePtr connectDstSlot;

      // SetParam: set(<setTarget>, "<setParamName>", <setValue>)
      IRNodePtr setTarget;
      std::string setParamName;
      IRNodePtr setValue;

      // Place: place(<placeTarget>, <placeX>, <placeY>)
      IRNodePtr placeTarget;
      IRNodePtr placeX;
      IRNodePtr placeY;

      explicit IRStmt(IRStmtKind k, SourceSpan sp = {})
         : kind(k), span(sp) {}
   };

   // Graph domain (step 10): a flat statement list, no prologue/loop split -
   // the kernel runs once, top to bottom, at edit time. See
   // FieldGraphKernel.h for the interpreter that walks this into a GraphPlan.
   struct GraphIRProgram
   {
      std::vector<IRStmtPtr> statements;
      std::vector<DeclaredParam> declaredParams;
   };

   struct ElementIRProgram
   {
      std::vector<IRStmtPtr> prologue;    // Hoisted frame/graph statements
      std::vector<IRStmtPtr> elementLoop; // Per-element statements
      // Bugfix (reduce-local-variable-storage): frame/graph-domain statements
      // that are only frame-domain because they coarsen a reduce() over an
      // Element-domain value (e.g. `output frame float wobble =
      // reduce.max(wave)` where `wave` is an ordinary per-element local).
      // These cannot run in `prologue` - `prologue` executes once, BEFORE
      // the element loop populates `wave`'s per-element buffer for this
      // cook - so they run in their own pass, once, AFTER the element loop.
      // See LowerElementProgramToIR's partitioning and ElementVM::Execute.
      std::vector<IRStmtPtr> postLoop;
      MeshWriteMask writeMask;
      std::vector<std::pair<std::string, DataType>> declaredAttribs;
      std::vector<DeclaredParam> declaredParams;
      std::vector<DeclaredState> declaredStates;
      std::vector<DeclaredOutput> declaredOutputs;
      std::vector<DeclaredInput> declaredInputs;
      bool isTimeDependent = false;
   };

   struct PixelIRProgram
   {
      std::vector<IRStmtPtr> prologue;    // Hoisted frame/graph statements
      std::vector<IRStmtPtr> pixelBody;   // Per-pixel statements
      std::vector<DeclaredParam> declaredParams;
      std::vector<DeclaredState> declaredStates;
      std::vector<DeclaredOutput> declaredOutputs;
      std::vector<DeclaredInput> declaredInputs;
      bool isTimeDependent = false;
   };

   Domain JoinDomains(Domain a, Domain b, bool& outCompatible);

   // Lowers a single expression AST to typed IR
   bool LowerAstToIR(const AstNodePtr& ast, IRNodePtr& outIR, FieldError& outError);

   // Lowers an Element program AST to typed Element IR with domain inference and hoisting
   bool LowerElementProgramToIR(const AstNodePtr& ast, ElementIRProgram& outProgram, FieldError& outError);

   // Lowers a Pixel program AST to typed Pixel IR with domain inference and hoisting
   bool LowerPixelProgramToIR(const AstNodePtr& ast, PixelIRProgram& outProgram, FieldError& outError);

   // Lowers a Graph program AST to typed Graph IR: domain inference, then a
   // rate-zero enforcement walk (Phase 2, step 10 doc §5.2) rejecting any
   // value reachable from an emit/connect/set/place argument whose domain
   // is not Domain::Graph.
   bool LowerGraphProgramToIR(const AstNodePtr& ast, GraphIRProgram& outProgram, FieldError& outError);
}


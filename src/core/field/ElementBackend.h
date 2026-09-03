#pragma once

#include "ElementStore.h"
#include "FieldBytecode.h"
#include "FieldError.h"
#include "FieldIR.h"
#include "FieldVM.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Field
{
   enum class ElemOpcode
   {
      OpLoadConst,
      OpLoadVar,
      OpAdd,
      OpSub,
      OpMul,
      OpDiv,
      OpMod,
      OpPow,
      OpLess,
      OpLessEqual,
      OpGreater,
      OpGreaterEqual,
      OpEqual,
      OpNotEqual,
      OpLogicalAnd,
      OpLogicalOr,
      OpNeg,
      OpNot,
      OpCallBuiltin,
      OpSwizzle,
      OpVecCtor,

      // Element-specific opcodes
      OpLoadP,
      OpStoreP,
      OpStorePComp,

      OpLoadN,
      OpStoreN,
      OpStoreNComp,

      OpLoadUv,
      OpStoreUv,
      OpStoreUvComp,

      OpLoadCd,
      OpStoreCd,
      OpStoreCdComp,

      OpLoadIndex,
      OpLoadCount,

      OpLoadAttrib,
      OpStoreAttrib,
      OpStoreAttribComp,

      OpLoadState,
      OpStoreState,
      OpStoreStateComp,

      OpReduceElementAttrib,

      OpLoadFrameVar, // reads variable computed in prologue
      OpStoreFrameVar,

      OpJumpIfFalse,
      OpJump,
      OpReturn
   };

   struct ElemInstruction
   {
      ElemOpcode op = ElemOpcode::OpReturn;
      int dst = 0;
      int src1 = 0;
      int src2 = 0;
      int src3 = 0;
      int lanes = 1;
      int src1Lanes = 1;
      int src2Lanes = 1;
      uint8_t swizzleIndices[4] = { 0, 1, 2, 3 };
      int swizzleCount = 0;
      std::string stringData;
      std::string stringData2;
      std::vector<int> argRegs;
      std::vector<int> argLanes;
      int jumpTarget = -1;
   };

   struct ElementCompiledCode
   {
      std::vector<ElemInstruction> code;
      std::vector<ConstantVector> constants;
      int numRegisters = 0;
   };

   class ElementProgram
   {
   public:
      ElementCompiledCode prologue;
      ElementCompiledCode loop;
      MeshWriteMask writeMask;
      std::vector<std::pair<std::string, DataType>> declaredAttribs;
      std::vector<DeclaredParam> declaredParams;
      std::vector<DeclaredState> declaredStates;
      bool isTimeDependent = false;

      // Verification / profiling counters (§5.8)
      mutable uint64_t prologueEvalCount = 0;
      mutable uint64_t elementEvalCount = 0;

      const MeshWriteMask& WriteMask() const { return writeMask; }
      void ResetCounters() const { prologueEvalCount = 0; elementEvalCount = 0; }
   };

   bool EmitElementBytecode(const ElementIRProgram& ir, ElementProgram& outProgram, FieldError& outError);

   // What the element-domain opcodes need in order to touch an element. The frame
   // prologue runs with inElementLoop == false and null lane pointers, so a genuinely
   // element-only opcode reaching it is reported rather than silently ignored.
   struct ElementExecContext
   {
      bool inElementLoop = false;
      size_t index = 0;
      size_t count = 0;
      ElementStore* store = nullptr;

      float* px = nullptr;
      float* py = nullptr;
      float* pz = nullptr;
      float* nx = nullptr;
      float* ny = nullptr;
      float* nz = nullptr;
      float* u = nullptr;
      float* v = nullptr;
      float* cr = nullptr;
      float* cg = nullptr;
      float* cb = nullptr;
   };

   class ElementVM
   {
   public:
      ElementVM() = default;

      bool Execute(const ElementProgram& prog,
                   ElementStore& store,
                   const ExecutionEnv& env,
                   std::string& outError);

      // Reads a Frame-domain-hoisted assignment (OpStoreFrameVar) from the
      // most recent Execute() call - e.g. `publish = ...` written at
      // prologue scope. An element-loop (per-element) assignment to the
      // same name never populates mFrameVars (it becomes an ordinary mesh
      // attribute instead via OpStoreAttrib), so this deliberately returns
      // false for that case rather than reading something else. Used by
      // FieldElementNode's "publish scalar" second output (build step 11,
      // §5.1) - reads register lane 0 only, since a publish output is a
      // scalar float.
      bool ReadFrameVar(const std::string& name, float& out) const
      {
         auto it = mFrameVars.find(name);
         if (it == mFrameVars.end())
            return false;
         out = (float)it->second.v[0];
         return true;
      }

   private:
      // ONE interpreter for both banks. The prologue and the element loop used to be
      // two near-duplicate switches over the same ElemOpcode set, and they had already
      // drifted: the prologue implemented no comparison, logical, unary or branch
      // opcode at all, so `k = t > 1.0` hoisted to frame rate silently evaluated to 0.
      bool ExecuteBlock(const ElementCompiledCode& block,
                        std::vector<VectorResult>& regs,
                        const ExecutionEnv& env,
                        const ElementExecContext& ctx,
                        bool& outRanAnInstruction,
                        std::string& outError);

      std::vector<VectorResult> mFrameRegisters;
      std::vector<VectorResult> mLoopRegisters;
      std::unordered_map<std::string, VectorResult> mFrameVars;
   };
}

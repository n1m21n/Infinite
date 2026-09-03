#pragma once

#include "FieldIR.h"
#include "FieldError.h"
#include "FieldTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Field
{
   enum class Opcode
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
      OpReturn
   };

   struct ConstantVector
   {
      double v[4] = { 0.0, 0.0, 0.0, 0.0 };
      int lanes = 1;
   };

   struct Instruction
   {
      Opcode op = Opcode::OpReturn;
      int dst = 0;
      int src1 = 0;
      int src2 = 0;
      int lanes = 1;
      int src1Lanes = 1;
      int src2Lanes = 1;
      uint8_t swizzleIndices[4] = { 0, 1, 2, 3 };
      std::string stringData;
      std::vector<int> argRegs;
      std::vector<int> argLanes;
   };

   struct BytecodeProgram
   {
      std::vector<Instruction> code;
      std::vector<ConstantVector> constants;
      FieldType resultType = FieldType(DataType::Float, 1);
      int numRegisters = 0;
      int resultRegister = 0;
   };

   bool EmitBytecode(const IRNodePtr& ir, BytecodeProgram& outProgram, FieldError& outError);
}

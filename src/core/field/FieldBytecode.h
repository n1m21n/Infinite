#pragma once

#include "FieldIR.h"
#include "FieldError.h"
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
      OpReturn
   };

   struct Instruction
   {
      Opcode op = Opcode::OpReturn;
      int dst = 0;
      int src1 = 0;
      int src2 = 0;
      int extra = 0;
      std::string stringData;
      std::vector<int> argRegs;
   };

   struct BytecodeProgram
   {
      std::vector<Instruction> code;
      std::vector<double> constants;
      int numRegisters = 0;
      int resultRegister = 0;
   };

   bool EmitBytecode(const IRNodePtr& ir, BytecodeProgram& outProgram, FieldError& outError);
}

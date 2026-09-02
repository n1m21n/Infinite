#include "FieldBytecode.h"

namespace Field
{
   namespace
   {
      struct EmitterState
      {
         BytecodeProgram& prog;
         int nextReg = 0;
         FieldError& error;

         EmitterState(BytecodeProgram& p, FieldError& err)
            : prog(p), error(err) {}

         int AllocReg()
         {
            int r = nextReg++;
            if (nextReg > prog.numRegisters)
               prog.numRegisters = nextReg;
            return r;
         }

         int AddConstant(double val)
         {
            for (size_t i = 0; i < prog.constants.size(); ++i)
            {
               if (prog.constants[i] == val)
                  return (int)i;
            }
            int idx = (int)prog.constants.size();
            prog.constants.push_back(val);
            return idx;
         }

         int EmitNode(const IRNodePtr& node)
         {
            if (!node || !error.Empty()) return -1;

            switch (node->kind)
            {
               case IRKind::Literal:
               {
                  int r = AllocReg();
                  int cIdx = AddConstant(node->numberValue);
                  Instruction inst;
                  inst.op = Opcode::OpLoadConst;
                  inst.dst = r;
                  inst.src1 = cIdx;
                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Variable:
               {
                  int r = AllocReg();
                  Instruction inst;
                  inst.op = Opcode::OpLoadVar;
                  inst.dst = r;
                  inst.stringData = node->varName;
                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Unary:
               {
                  int operandReg = EmitNode(node->children[0]);
                  int r = AllocReg();
                  Instruction inst;
                  if (node->op == "-") inst.op = Opcode::OpNeg;
                  else if (node->op == "!") inst.op = Opcode::OpNot;
                  else inst.op = Opcode::OpNeg; // fallback
                  inst.dst = r;
                  inst.src1 = operandReg;
                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Binary:
               {
                  int lhsReg = EmitNode(node->children[0]);
                  int rhsReg = EmitNode(node->children[1]);
                  int r = AllocReg();
                  Instruction inst;
                  inst.dst = r;
                  inst.src1 = lhsReg;
                  inst.src2 = rhsReg;

                  if (node->op == "+") inst.op = Opcode::OpAdd;
                  else if (node->op == "-") inst.op = Opcode::OpSub;
                  else if (node->op == "*") inst.op = Opcode::OpMul;
                  else if (node->op == "/") inst.op = Opcode::OpDiv;
                  else if (node->op == "%") inst.op = Opcode::OpMod;
                  else if (node->op == "^") inst.op = Opcode::OpPow;
                  else if (node->op == "<") inst.op = Opcode::OpLess;
                  else if (node->op == "<=") inst.op = Opcode::OpLessEqual;
                  else if (node->op == ">") inst.op = Opcode::OpGreater;
                  else if (node->op == ">=") inst.op = Opcode::OpGreaterEqual;
                  else if (node->op == "==") inst.op = Opcode::OpEqual;
                  else if (node->op == "!=") inst.op = Opcode::OpNotEqual;
                  else if (node->op == "&&") inst.op = Opcode::OpLogicalAnd;
                  else if (node->op == "||") inst.op = Opcode::OpLogicalOr;
                  else inst.op = Opcode::OpAdd;

                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Call:
               {
                  std::vector<int> argRegs;
                  for (const auto& child : node->children)
                  {
                     argRegs.push_back(EmitNode(child));
                  }

                  int r = AllocReg();
                  Instruction inst;
                  inst.op = Opcode::OpCallBuiltin;
                  inst.dst = r;
                  inst.stringData = node->callee;
                  inst.src1 = argRegs.empty() ? -1 : argRegs.front();
                  inst.src2 = (int)argRegs.size();
                  inst.extra = argRegs.empty() ? -1 : argRegs.front();
                  inst.argRegs = argRegs;
                  prog.code.push_back(inst);
                  return r;
               }

               default:
                  return -1;
            }
         }
      };
   }

   bool EmitBytecode(const IRNodePtr& ir, BytecodeProgram& outProgram, FieldError& outError)
   {
      outProgram.code.clear();
      outProgram.constants.clear();
      outProgram.numRegisters = 0;
      outProgram.resultRegister = 0;
      outError.Clear();

      if (!ir)
      {
         outError.severity = Severity::Error;
         outError.message = "null IR node";
         return false;
      }

      EmitterState state(outProgram, outError);
      int resReg = state.EmitNode(ir);
      if (resReg < 0 || !outError.Empty())
         return false;

      outProgram.resultRegister = resReg;

      Instruction retInst;
      retInst.op = Opcode::OpReturn;
      retInst.src1 = resReg;
      outProgram.code.push_back(retInst);

      return true;
   }
}

#include "FieldBytecode.h"

namespace Field
{
   namespace
   {
      bool IsVecConstructor(const std::string& name)
      {
         return name == "vec2" || name == "vec3" || name == "vec4";
      }

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

         int AddConstant(const double* vals, int lanes)
         {
            for (size_t i = 0; i < prog.constants.size(); ++i)
            {
               if (prog.constants[i].lanes == lanes)
               {
                  bool match = true;
                  for (int l = 0; l < lanes; ++l)
                  {
                     if (prog.constants[i].v[l] != vals[l])
                     {
                        match = false;
                        break;
                     }
                  }
                  if (match) return (int)i;
               }
            }

            ConstantVector cv;
            cv.lanes = lanes;
            for (int l = 0; l < lanes; ++l)
               cv.v[l] = vals[l];
            int idx = (int)prog.constants.size();
            prog.constants.push_back(cv);
            return idx;
         }

         int EmitNode(const IRNodePtr& node, int& outLanes)
         {
            if (!node || !error.Empty()) return -1;

            outLanes = node->type.lanes;

            switch (node->kind)
            {
               case IRKind::Literal:
               {
                  int r = AllocReg();
                  int cIdx = AddConstant(node->vecValues, node->type.lanes);
                  Instruction inst;
                  inst.op = Opcode::OpLoadConst;
                  inst.dst = r;
                  inst.src1 = cIdx;
                  inst.lanes = node->type.lanes;
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
                  inst.lanes = 1;
                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Access:
               {
                  int baseLanes = 1;
                  int baseReg = EmitNode(node->children[0], baseLanes);
                  int r = AllocReg();
                  Instruction inst;
                  inst.op = Opcode::OpSwizzle;
                  inst.dst = r;
                  inst.src1 = baseReg;
                  inst.lanes = node->type.lanes;
                  inst.src1Lanes = baseLanes;
                  for (int i = 0; i < 4; ++i)
                     inst.swizzleIndices[i] = node->swizzleIndices[i];
                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Unary:
               {
                  int operandLanes = 1;
                  int operandReg = EmitNode(node->children[0], operandLanes);
                  int r = AllocReg();
                  Instruction inst;
                  if (node->op == "-") inst.op = Opcode::OpNeg;
                  else if (node->op == "!") inst.op = Opcode::OpNot;
                  else inst.op = Opcode::OpNeg;
                  inst.dst = r;
                  inst.src1 = operandReg;
                  inst.lanes = node->type.lanes;
                  inst.src1Lanes = operandLanes;
                  prog.code.push_back(inst);
                  return r;
               }

               case IRKind::Binary:
               {
                  int lhsLanes = 1;
                  int lhsReg = EmitNode(node->children[0], lhsLanes);
                  int rhsLanes = 1;
                  int rhsReg = EmitNode(node->children[1], rhsLanes);
                  int r = AllocReg();
                  Instruction inst;
                  inst.dst = r;
                  inst.src1 = lhsReg;
                  inst.src2 = rhsReg;
                  inst.lanes = node->type.lanes;
                  inst.src1Lanes = lhsLanes;
                  inst.src2Lanes = rhsLanes;

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
                  if (IsVecConstructor(node->callee))
                  {
                     std::vector<int> argRegs;
                     std::vector<int> argLanes;
                     for (const auto& child : node->children)
                     {
                        int aL = 1;
                        int aR = EmitNode(child, aL);
                        argRegs.push_back(aR);
                        argLanes.push_back(aL);
                     }

                     int r = AllocReg();
                     Instruction inst;
                     inst.op = Opcode::OpVecCtor;
                     inst.dst = r;
                     inst.lanes = node->type.lanes;
                     inst.argRegs = argRegs;
                     inst.argLanes = argLanes;
                     prog.code.push_back(inst);
                     return r;
                  }

                  std::vector<int> argRegs;
                  std::vector<int> argLanes;
                  for (const auto& child : node->children)
                  {
                     int aL = 1;
                     int aR = EmitNode(child, aL);
                     argRegs.push_back(aR);
                     argLanes.push_back(aL);
                  }

                  int r = AllocReg();
                  Instruction inst;
                  inst.op = Opcode::OpCallBuiltin;
                  inst.dst = r;
                  inst.stringData = node->callee;
                  inst.lanes = node->type.lanes;
                  inst.argRegs = argRegs;
                  inst.argLanes = argLanes;
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
      outProgram.resultType = FieldType(DataType::Float, 1);
      outError.Clear();

      if (!ir)
      {
         outError.severity = Severity::Error;
         outError.message = "null IR node";
         return false;
      }

      outProgram.resultType = ir->type;

      EmitterState state(outProgram, outError);
      int resLanes = 1;
      int resReg = state.EmitNode(ir, resLanes);
      if (resReg < 0 || !outError.Empty())
         return false;

      outProgram.resultRegister = resReg;

      Instruction retInst;
      retInst.op = Opcode::OpReturn;
      retInst.src1 = resReg;
      retInst.lanes = resLanes;
      outProgram.code.push_back(retInst);

      return true;
   }
}

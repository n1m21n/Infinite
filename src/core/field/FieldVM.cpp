#include "FieldVM.h"
#include "FieldRandom.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Field
{
   namespace
   {
      struct Reg
      {
         double v[4] = { 0.0, 0.0, 0.0, 0.0 };
      };

      constexpr int kMaxRegisters = 256;

      double CallBuiltin(const std::string& name,
                         const double* args,
                         size_t numArgs,
                         double t,
                         std::string& outError)
      {
         if (name == "sin") return numArgs >= 1 ? sin(args[0]) : 0.0;
         if (name == "cos") return numArgs >= 1 ? cos(args[0]) : 0.0;
         if (name == "tan") return numArgs >= 1 ? tan(args[0]) : 0.0;
         if (name == "abs") return numArgs >= 1 ? fabs(args[0]) : 0.0;
         if (name == "floor") return numArgs >= 1 ? floor(args[0]) : 0.0;
         if (name == "ceil") return numArgs >= 1 ? ceil(args[0]) : 0.0;
         if (name == "round") return numArgs >= 1 ? floor(args[0] + 0.5) : 0.0;
         if (name == "sign") return numArgs >= 1 ? (args[0] > 0.0 ? 1.0 : (args[0] < 0.0 ? -1.0 : 0.0)) : 0.0;
         if (name == "exp") return numArgs >= 1 ? exp(args[0]) : 0.0;
         if (name == "pow") return numArgs >= 2 ? pow(args[0], args[1]) : 0.0;
         if (name == "min") return numArgs >= 2 ? std::min(args[0], args[1]) : 0.0;
         if (name == "max") return numArgs >= 2 ? std::max(args[0], args[1]) : 0.0;
         if (name == "mod")
         {
            if (numArgs >= 2)
            {
               if (args[1] == 0.0)
               {
                  outError = "division by zero";
                  return 0.0;
               }
               return fmod(args[0], args[1]);
            }
            return 0.0;
         }
         if (name == "clamp") return numArgs >= 3 ? std::min(std::max(args[0], args[1]), args[2]) : 0.0;
         if (name == "lerp" || name == "mix") return numArgs >= 3 ? args[0] + (args[1] - args[0]) * args[2] : 0.0;
         if (name == "sqrt")
         {
            if (numArgs < 1) return 0.0;
            if (args[0] < 0.0)
            {
               outError = "sqrt() of a negative number";
               return 0.0;
            }
            return sqrt(args[0]);
         }
         if (name == "log")
         {
            if (numArgs < 1) return 0.0;
            if (args[0] <= 0.0)
            {
               outError = "log() needs a positive argument";
               return 0.0;
            }
            return log(args[0]);
         }
         if (name == "step")
         {
            if (numArgs >= 2) return args[1] < args[0] ? 0.0 : 1.0;
            return 0.0;
         }
         if (name == "smoothstep")
         {
            if (numArgs >= 3)
            {
               if (args[1] == args[0]) return args[2] < args[0] ? 0.0 : 1.0;
               const double x = std::min(std::max((args[2] - args[0]) / (args[1] - args[0]), 0.0), 1.0);
               return x * x * (3.0 - 2.0 * x);
            }
            return 0.0;
         }
         if (name == "rand" || name == "noise")
         {
            double minVal = 0.0;
            double maxVal = 1.0;
            double speed = 1.0;
            double seed = 0.0;
            if (numArgs == 1)
            {
               speed = args[0];
            }
            else if (numArgs == 2)
            {
               minVal = args[0];
               maxVal = args[1];
            }
            else if (numArgs == 3)
            {
               minVal = args[0];
               maxVal = args[1];
               speed = args[2];
            }
            else if (numArgs >= 4)
            {
               minVal = args[0];
               maxVal = args[1];
               speed = args[2];
               seed = args[3];
            }
            return Rand(minVal, maxVal, speed, seed, t);
         }
         if (name == "sh")
         {
            double minVal = 0.0;
            double maxVal = 1.0;
            double speed = 1.0;
            double seed = 0.0;
            if (numArgs == 1)
            {
               speed = args[0];
            }
            else if (numArgs == 2)
            {
               minVal = args[0];
               maxVal = args[1];
            }
            else if (numArgs == 3)
            {
               minVal = args[0];
               maxVal = args[1];
               speed = args[2];
            }
            else if (numArgs >= 4)
            {
               minVal = args[0];
               maxVal = args[1];
               speed = args[2];
               seed = args[3];
            }
            return Sh(minVal, maxVal, speed, seed, t);
         }
         if (name == "if")
         {
            if (numArgs >= 3)
            {
               return (args[0] != 0.0) ? args[1] : args[2];
            }
            return 0.0;
         }

         outError = "unknown function '" + name + "'";
         return 0.0;
      }
   }

   bool FieldVM::Execute(const BytecodeProgram& prog,
                         const ExecutionEnv& env,
                         double& outResult,
                         std::string& outError)
   {
      VectorResult vr;
      if (!ExecuteVector(prog, env, vr, outError))
         return false;

      outResult = vr.v[0];
      return true;
   }

   bool FieldVM::ExecuteVector(const BytecodeProgram& prog,
                               const ExecutionEnv& env,
                               VectorResult& outResult,
                               std::string& outError)
   {
      outError.clear();
      outResult = VectorResult{};

      if (prog.numRegisters > kMaxRegisters)
      {
         outError = "register limit exceeded (" + std::to_string(kMaxRegisters) + ")";
         return false;
      }

      Reg regs[kMaxRegisters];

      for (const auto& inst : prog.code)
      {
         int lanes = inst.lanes > 0 ? inst.lanes : 1;

         switch (inst.op)
         {
            case Opcode::OpLoadConst:
            {
               const auto& cv = prog.constants[inst.src1];
               for (int l = 0; l < lanes; ++l)
                  regs[inst.dst].v[l] = cv.v[l];
               break;
            }

            case Opcode::OpLoadVar:
            {
               const std::string& name = inst.stringData;
               if (name == "t")
               {
                  regs[inst.dst].v[0] = env.t;
               }
               else if (name == "pi")
               {
                  regs[inst.dst].v[0] = M_PI;
               }
               else
               {
                  bool found = false;
                  if (env.params != nullptr)
                  {
                     auto it = env.params->find(name);
                     if (it != env.params->end())
                     {
                        regs[inst.dst].v[0] = (double)it->second;
                        found = true;
                     }
                  }
                  if (!found && env.siblings != nullptr)
                  {
                     auto it = env.siblings->find(name);
                     if (it != env.siblings->end())
                     {
                        regs[inst.dst].v[0] = (double)it->second;
                        found = true;
                     }
                  }
                  if (!found && env.globals != nullptr)
                  {
                     auto it = env.globals->find(name);
                     if (it != env.globals->end())
                     {
                        regs[inst.dst].v[0] = (double)it->second;
                        found = true;
                     }
                  }
                  if (!found)
                  {
                     outError = "unknown identifier '" + name + "'";
                     return false;
                  }
               }
               break;
            }

            case Opcode::OpSwizzle:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  uint8_t srcIdx = inst.swizzleIndices[l];
                  regs[inst.dst].v[l] = regs[inst.src1].v[srcIdx];
               }
               break;
            }

            case Opcode::OpVecCtor:
            {
               if (inst.argRegs.size() == 1 && lanes > 1 && inst.argLanes[0] == 1)
               {
                  double splatVal = regs[inst.argRegs[0]].v[0];
                  for (int l = 0; l < lanes; ++l)
                     regs[inst.dst].v[l] = splatVal;
               }
               else
               {
                  int outIdx = 0;
                  for (size_t a = 0; a < inst.argRegs.size(); ++a)
                  {
                     int aReg = inst.argRegs[a];
                     int aLanes = inst.argLanes[a];
                     for (int l = 0; l < aLanes && outIdx < lanes; ++l)
                     {
                        regs[inst.dst].v[outIdx++] = regs[aReg].v[l];
                     }
                  }
               }
               break;
            }

            case Opcode::OpAdd:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  double b = regs[inst.src2].v[inst.src2Lanes == 1 ? 0 : l];
                  regs[inst.dst].v[l] = a + b;
               }
               break;
            }

            case Opcode::OpSub:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  double b = regs[inst.src2].v[inst.src2Lanes == 1 ? 0 : l];
                  regs[inst.dst].v[l] = a - b;
               }
               break;
            }

            case Opcode::OpMul:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  double b = regs[inst.src2].v[inst.src2Lanes == 1 ? 0 : l];
                  regs[inst.dst].v[l] = a * b;
               }
               break;
            }

            case Opcode::OpDiv:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  double b = regs[inst.src2].v[inst.src2Lanes == 1 ? 0 : l];
                  if (b == 0.0)
                  {
                     outError = "division by zero";
                     return false;
                  }
                  regs[inst.dst].v[l] = a / b;
               }
               break;
            }

            case Opcode::OpMod:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  double b = regs[inst.src2].v[inst.src2Lanes == 1 ? 0 : l];
                  if (b == 0.0)
                  {
                     outError = "division by zero";
                     return false;
                  }
                  regs[inst.dst].v[l] = fmod(a, b);
               }
               break;
            }

            case Opcode::OpPow:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  double b = regs[inst.src2].v[inst.src2Lanes == 1 ? 0 : l];
                  regs[inst.dst].v[l] = pow(a, b);
               }
               break;
            }

            case Opcode::OpLess:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] < regs[inst.src2].v[0]) ? 1.0 : 0.0;
               break;

            case Opcode::OpLessEqual:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] <= regs[inst.src2].v[0]) ? 1.0 : 0.0;
               break;

            case Opcode::OpGreater:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] > regs[inst.src2].v[0]) ? 1.0 : 0.0;
               break;

            case Opcode::OpGreaterEqual:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] >= regs[inst.src2].v[0]) ? 1.0 : 0.0;
               break;

            case Opcode::OpEqual:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] == regs[inst.src2].v[0]) ? 1.0 : 0.0;
               break;

            case Opcode::OpNotEqual:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] != regs[inst.src2].v[0]) ? 1.0 : 0.0;
               break;

            case Opcode::OpLogicalAnd:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] != 0.0 && regs[inst.src2].v[0] != 0.0) ? 1.0 : 0.0;
               break;

            case Opcode::OpLogicalOr:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] != 0.0 || regs[inst.src2].v[0] != 0.0) ? 1.0 : 0.0;
               break;

            case Opcode::OpNeg:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double a = regs[inst.src1].v[inst.src1Lanes == 1 ? 0 : l];
                  regs[inst.dst].v[l] = -a;
               }
               break;
            }

            case Opcode::OpNot:
               regs[inst.dst].v[0] = (regs[inst.src1].v[0] != 0.0) ? 0.0 : 1.0;
               break;

            case Opcode::OpCallBuiltin:
            {
               for (int l = 0; l < lanes; ++l)
               {
                  double argBuf[8];
                  size_t count = std::min(inst.argRegs.size(), (size_t)8);
                  for (size_t a = 0; a < count; ++a)
                  {
                     int aReg = inst.argRegs[a];
                     int aLanes = inst.argLanes[a];
                     argBuf[a] = regs[aReg].v[aLanes == 1 ? 0 : l];
                  }
                  double val = CallBuiltin(inst.stringData, argBuf, inst.argRegs.size(), env.t, outError);
                  if (!outError.empty())
                     return false;
                  regs[inst.dst].v[l] = val;
               }
               break;
            }

            case Opcode::OpReturn:
            {
               outResult.lanes = lanes;
               for (int l = 0; l < lanes; ++l)
                  outResult.v[l] = regs[inst.src1].v[l];
               return true;
            }
         }
      }

      int resLanes = prog.resultType.lanes > 0 ? prog.resultType.lanes : 1;
      outResult.lanes = resLanes;
      if (prog.resultRegister < kMaxRegisters)
      {
         for (int l = 0; l < resLanes; ++l)
            outResult.v[l] = regs[prog.resultRegister].v[l];
      }
      return true;
   }
}

#include "FieldVM.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Field
{
   namespace
   {
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
         if (name == "lerp") return numArgs >= 3 ? args[0] + (args[1] - args[0]) * args[2] : 0.0;
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
            if (numArgs == 1)
            {
               speed = args[0];
            }
            else if (numArgs == 2)
            {
               minVal = args[0];
               maxVal = args[1];
            }
            else if (numArgs >= 3)
            {
               minVal = args[0];
               maxVal = args[1];
               speed = args[2];
            }
            const double tau_t = t * speed;
            const double n = (sin(tau_t) + sin(tau_t * 1.618033988749895) + sin(tau_t * 2.718281828459045)) / 6.0 + 0.5;
            return minVal + (maxVal - minVal) * n;
         }
         if (name == "sh")
         {
            double minVal = 0.0;
            double maxVal = 1.0;
            double speed = 1.0;
            if (numArgs == 1)
            {
               speed = args[0];
            }
            else if (numArgs == 2)
            {
               minVal = args[0];
               maxVal = args[1];
            }
            else if (numArgs >= 3)
            {
               minVal = args[0];
               maxVal = args[1];
               speed = args[2];
            }
            const double seed = floor(t * speed) * 123.456;
            const double frac = fabs(fmod(sin(seed) * 43758.5453123, 1.0));
            return minVal + (maxVal - minVal) * frac;
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
      outError.clear();

      // Stack-allocated register buffer up to 128 registers, dynamic fallback if needed
      constexpr int kMaxStackRegs = 128;
      double stackRegs[kMaxStackRegs];
      std::vector<double> heapRegs;
      double* regs = stackRegs;

      if (prog.numRegisters > kMaxStackRegs)
      {
         heapRegs.resize(prog.numRegisters, 0.0);
         regs = heapRegs.data();
      }
      else
      {
         for (int i = 0; i < prog.numRegisters; ++i)
            regs[i] = 0.0;
      }

      for (const auto& inst : prog.code)
      {
         switch (inst.op)
         {
            case Opcode::OpLoadConst:
               regs[inst.dst] = prog.constants[inst.src1];
               break;

            case Opcode::OpLoadVar:
            {
               const std::string& name = inst.stringData;
               if (name == "t")
               {
                  regs[inst.dst] = env.t;
               }
               else if (name == "pi")
               {
                  regs[inst.dst] = M_PI;
               }
               else
               {
                  bool found = false;
                  if (env.siblings != nullptr)
                  {
                     auto it = env.siblings->find(name);
                     if (it != env.siblings->end())
                     {
                        regs[inst.dst] = (double)it->second;
                        found = true;
                     }
                  }
                  if (!found && env.globals != nullptr)
                  {
                     auto it = env.globals->find(name);
                     if (it != env.globals->end())
                     {
                        regs[inst.dst] = (double)it->second;
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

            case Opcode::OpAdd:
               regs[inst.dst] = regs[inst.src1] + regs[inst.src2];
               break;

            case Opcode::OpSub:
               regs[inst.dst] = regs[inst.src1] - regs[inst.src2];
               break;

            case Opcode::OpMul:
               regs[inst.dst] = regs[inst.src1] * regs[inst.src2];
               break;

            case Opcode::OpDiv:
            {
               double denom = regs[inst.src2];
               if (denom == 0.0)
               {
                  outError = "division by zero";
                  return false;
               }
               regs[inst.dst] = regs[inst.src1] / denom;
               break;
            }

            case Opcode::OpMod:
            {
               double denom = regs[inst.src2];
               if (denom == 0.0)
               {
                  outError = "division by zero";
                  return false;
               }
               regs[inst.dst] = fmod(regs[inst.src1], denom);
               break;
            }

            case Opcode::OpPow:
               regs[inst.dst] = pow(regs[inst.src1], regs[inst.src2]);
               break;

            case Opcode::OpLess:
               regs[inst.dst] = (regs[inst.src1] < regs[inst.src2]) ? 1.0 : 0.0;
               break;

            case Opcode::OpLessEqual:
               regs[inst.dst] = (regs[inst.src1] <= regs[inst.src2]) ? 1.0 : 0.0;
               break;

            case Opcode::OpGreater:
               regs[inst.dst] = (regs[inst.src1] > regs[inst.src2]) ? 1.0 : 0.0;
               break;

            case Opcode::OpGreaterEqual:
               regs[inst.dst] = (regs[inst.src1] >= regs[inst.src2]) ? 1.0 : 0.0;
               break;

            case Opcode::OpEqual:
               regs[inst.dst] = (regs[inst.src1] == regs[inst.src2]) ? 1.0 : 0.0;
               break;

            case Opcode::OpNotEqual:
               regs[inst.dst] = (regs[inst.src1] != regs[inst.src2]) ? 1.0 : 0.0;
               break;

            case Opcode::OpLogicalAnd:
               regs[inst.dst] = (regs[inst.src1] != 0.0 && regs[inst.src2] != 0.0) ? 1.0 : 0.0;
               break;

            case Opcode::OpLogicalOr:
               regs[inst.dst] = (regs[inst.src1] != 0.0 || regs[inst.src2] != 0.0) ? 1.0 : 0.0;
               break;

            case Opcode::OpNeg:
               regs[inst.dst] = -regs[inst.src1];
               break;

            case Opcode::OpNot:
               regs[inst.dst] = (regs[inst.src1] != 0.0) ? 0.0 : 1.0;
               break;

            case Opcode::OpCallBuiltin:
            {
               int argStart = inst.src1;
               int argCount = inst.src2;
               double argBuf[8];
               for (int i = 0; i < argCount && i < 8; ++i)
               {
                  argBuf[i] = regs[argStart + i];
               }
               regs[inst.dst] = CallBuiltin(inst.stringData, argBuf, argCount, env.t, outError);
               if (!outError.empty())
                  return false;
               break;
            }

            case Opcode::OpReturn:
               outResult = regs[inst.src1];
               return true;
         }
      }

      outResult = (prog.resultRegister < prog.numRegisters) ? regs[prog.resultRegister] : 0.0;
      return true;
   }
}

#pragma once

#include "SampleProgram.h"
#include "../../audio/DspMath.h"

#include <cassert>
#include <cmath>

// Header-only, allocation-free interpreter for the Field 'sample' domain
// register machine. Callable from ProcessBlock, on the real-time audio
// thread: no allocation, no locks, no syscalls, no unbounded loops (the
// program's own instruction count is fixed at compile time and bounded by
// kSampleMaxInstr) - see SampleProgram.h and BackendRegister.cpp for how a
// SampleProgram gets built. This file only reads it.
namespace Field
{
   // One sample's worth of externally-supplied inputs. `in`/`sr`/`n`/
   // `paramVals` are fetched once per sample, ABOVE the voice loop, by the
   // caller (see FieldSampleNode.cpp) - fetching them per-voice would let
   // the effective param smoothing time constant drift with voice count.
   // `stateCur`/`stateNext` are per-voice: the caller passes that voice's
   // own state banks.
   struct SampleRuntimeInput
   {
      float in = 0.0f;
      float sr = 0.0f;
      float n = 0.0f;
      const float* paramVals = nullptr; // indexed by SampleProgram::params[i] order
      const float* stateCur = nullptr;  // indexed by SampleProgram::state[i] order
      float* stateNext = nullptr;       // write-only; same indexing as stateCur
   };

   // `regs` must have at least prog.numRegs entries (kSampleMaxRegs is
   // always enough - callers keep a fixed-size scratch array, never
   // allocate one per call). Returns the raw (unclamped, un-NaN-checked)
   // value of the program's 'out' register; FieldSampleNode.cpp applies the
   // output clamp and the once-per-block NaN sweep.
   inline float RunSampleProgram(const SampleProgram& prog, const SampleRuntimeInput& in, float* regs)
   {
      for (int i = 0; i < (int)prog.code.size(); i++)
      {
         const SampleInstr& ins = prog.code[i];
         switch (ins.op)
         {
            case SampleOp::Nop: break;
            case SampleOp::LoadImm: regs[ins.dst] = ins.imm; break;
            case SampleOp::LoadIn: regs[ins.dst] = in.in; break;
            case SampleOp::LoadSr: regs[ins.dst] = in.sr; break;
            case SampleOp::LoadN: regs[ins.dst] = in.n; break;
            case SampleOp::LoadParam: regs[ins.dst] = in.paramVals[ins.a]; break;
            case SampleOp::LoadState: regs[ins.dst] = in.stateCur[ins.a]; break;
            case SampleOp::StoreState: in.stateNext[ins.a] = DspMath::FlushDenormal(regs[ins.b]); break;
            case SampleOp::Move: regs[ins.dst] = regs[ins.a]; break;
            case SampleOp::Add: regs[ins.dst] = regs[ins.a] + regs[ins.b]; break;
            case SampleOp::Sub: regs[ins.dst] = regs[ins.a] - regs[ins.b]; break;
            case SampleOp::Mul: regs[ins.dst] = regs[ins.a] * regs[ins.b]; break;
            case SampleOp::Div: regs[ins.dst] = regs[ins.b] != 0.0f ? regs[ins.a] / regs[ins.b] : 0.0f; break;
            case SampleOp::Mod: regs[ins.dst] = regs[ins.b] != 0.0f ? fmodf(regs[ins.a], regs[ins.b]) : 0.0f; break;
            case SampleOp::Pow: regs[ins.dst] = powf(regs[ins.a], regs[ins.b]); break;
            case SampleOp::Neg: regs[ins.dst] = -regs[ins.a]; break;
            case SampleOp::Lt: regs[ins.dst] = regs[ins.a] < regs[ins.b] ? 1.0f : 0.0f; break;
            case SampleOp::Le: regs[ins.dst] = regs[ins.a] <= regs[ins.b] ? 1.0f : 0.0f; break;
            case SampleOp::Gt: regs[ins.dst] = regs[ins.a] > regs[ins.b] ? 1.0f : 0.0f; break;
            case SampleOp::Ge: regs[ins.dst] = regs[ins.a] >= regs[ins.b] ? 1.0f : 0.0f; break;
            case SampleOp::Eq: regs[ins.dst] = regs[ins.a] == regs[ins.b] ? 1.0f : 0.0f; break;
            case SampleOp::Ne: regs[ins.dst] = regs[ins.a] != regs[ins.b] ? 1.0f : 0.0f; break;
            case SampleOp::LogAnd: regs[ins.dst] = (regs[ins.a] != 0.0f && regs[ins.b] != 0.0f) ? 1.0f : 0.0f; break;
            case SampleOp::LogOr: regs[ins.dst] = (regs[ins.a] != 0.0f || regs[ins.b] != 0.0f) ? 1.0f : 0.0f; break;
            case SampleOp::LogNot: regs[ins.dst] = regs[ins.a] == 0.0f ? 1.0f : 0.0f; break;
            case SampleOp::Select: regs[ins.dst] = regs[ins.a] != 0.0f ? regs[ins.b] : regs[ins.c]; break;
            case SampleOp::Sin: regs[ins.dst] = sinf(regs[ins.a]); break;
            case SampleOp::Cos: regs[ins.dst] = cosf(regs[ins.a]); break;
            case SampleOp::Tan: regs[ins.dst] = tanf(regs[ins.a]); break;
            case SampleOp::Sqrt: regs[ins.dst] = sqrtf(regs[ins.a] > 0.0f ? regs[ins.a] : 0.0f); break;
            case SampleOp::Abs: regs[ins.dst] = fabsf(regs[ins.a]); break;
            case SampleOp::Floor: regs[ins.dst] = floorf(regs[ins.a]); break;
            case SampleOp::Ceil: regs[ins.dst] = ceilf(regs[ins.a]); break;
            case SampleOp::Exp: regs[ins.dst] = expf(regs[ins.a]); break;
            case SampleOp::Log: regs[ins.dst] = logf(regs[ins.a] > 1e-9f ? regs[ins.a] : 1e-9f); break;
            case SampleOp::Min: regs[ins.dst] = fminf(regs[ins.a], regs[ins.b]); break;
            case SampleOp::Max: regs[ins.dst] = fmaxf(regs[ins.a], regs[ins.b]); break;
            case SampleOp::Clamp: regs[ins.dst] = fminf(fmaxf(regs[ins.a], regs[ins.b]), regs[ins.c]); break;
            default:
               assert(false && "SampleRuntime: unhandled opcode");
               regs[ins.dst] = 0.0f;
               break;
         }
      }
      return (prog.outReg >= 0) ? regs[prog.outReg] : 0.0f;
   }
}

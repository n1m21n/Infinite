#pragma once

#include <cstdint>
#include <string>
#include <vector>

// POD-ish register-machine program for the Field 'sample' domain (build step
// 9). Built on the main thread by BackendRegister::Compile, consumed only by
// SampleRuntime::Run on the audio thread via a read-only pointer handed over
// through SampleSlotT<SampleProgram> (compile-swap.h - see FieldSampleNode).
//
// Every array below is fixed-capacity and filled once at compile time; the
// audio thread only ever indexes into it (read-only), never resizes or
// allocates from it. `code`/`state`/`params` use std::vector rather than raw
// arrays for compile-time convenience, but nothing on the audio thread calls
// push_back/insert/erase on them - see SampleRuntime.h.
namespace Field
{
   static constexpr int kSampleMaxInstr = 512;
   static constexpr int kSampleMaxRegs = 128;
   static constexpr int kSampleMaxStateCells = 64;
   static constexpr int kSampleMaxParams = 128; // mirrors ParamMailbox::kMaxParams
   static constexpr int kSampleMaxUnroll = 64;  // for/map-style unroll cap, matches step 8's convention

   enum class SampleOp : uint8_t
   {
      Nop = 0,
      LoadImm,     // dst = imm
      LoadIn,      // dst = in  (per-sample shared input)
      LoadSr,      // dst = sr  (per-sample shared sample rate)
      LoadN,       // dst = n   (per-sample shared running sample counter)
      LoadParam,   // dst = paramVals[a]  (per-sample shared, hoisted above the voice loop)
      LoadState,   // dst = stateCur[a]   (per-voice)
      StoreState,  // stateNext[a] = <src in b>  (per-voice; emitted once per cell at program end)
      Move,        // dst = a
      Add, Sub, Mul, Div, Mod, Pow, Neg,
      Lt, Le, Gt, Ge, Eq, Ne,
      LogAnd, LogOr, LogNot,
      Select,      // dst = (a != 0) ? b : c  (branchless if/else merge - see BackendRegister)
      Sin, Cos, Tan, Sqrt, Abs, Floor, Ceil, Exp, Log,
      Min, Max, Clamp,
      Count
   };

   // Fixed instruction: one opcode + up to three register operands + one
   // float immediate. No operand stack. `dst`/`a`/`b`/`c` are register
   // indices (0..kSampleMaxRegs-1); for LoadParam/LoadState/StoreState, `a`
   // is instead a param-slot / state-cell index (not a register). `c` is
   // used only by Select (dst = (a != 0) ? b : c).
   struct SampleInstr
   {
      SampleOp op = SampleOp::Nop;
      uint8_t dst = 0;
      uint8_t a = 0;
      uint8_t b = 0;
      uint8_t c = 0;
      float imm = 0.0f;
   };

   struct SampleStateInit
   {
      std::string name;
      std::string typeName = "float";
      float initialValue = 0.0f;
      // Resolved on the main thread at compile time by (name,type) match
      // against the *previous* program's declared state; -1 = no match
      // (freshly declared cell, or a type change - starts at initialValue).
      int transplantFromIndex = -1;
   };

   struct SampleParamSlot
   {
      std::string name;
      // Dense id into ParamMailbox (0..127), recomputed fresh every
      // successful compile - never persisted. See ParamTable's separate
      // stable `id` (paramIndex) column for the persisted identity.
      int mailboxId = -1;
      float defaultValue = 0.0f;
      float minValue = 0.0f;
      float maxValue = 1.0f;
   };

   struct SampleProgram
   {
      std::vector<SampleInstr> code; // fixed-size after Compile(); audio thread only indexes it
      int numRegs = 0;

      int outReg = -1; // final register bound to 'out'; duplicated to both channels (mono kernel, v1)

      std::vector<SampleStateInit> state; // one entry per declared state cell
      std::vector<SampleParamSlot> params; // one entry per declared param

      bool hasReduceRms = false;
      float reduceLoHz = 20.0f;
      float reduceHiHz = 20000.0f;

      bool valid = false;
   };
}

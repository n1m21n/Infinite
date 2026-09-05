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

   // Delay line capacity (Step 19): 65536 cells (~1.48s at 44.1kHz, ~1.36s at 48kHz)
   // cumulative budget across all delay() calls in a kernel. Sized to allow genuine
   // multi-tap reverbs (multiple comb/allpass filters), chorus, flangers, phasers,
   // and slapback/tempo-synced delays while fitting comfortably in L2 cache (256 KB)
   // without dynamic allocation or heap overhead on the real-time audio thread.
   static constexpr int kSampleMaxDelayCells = 65536;
   static constexpr int kSampleMaxDelayLines = 16;

   // Table capacity (Step 24): 16384 cells (64 KB) cumulative budget across
   // all state float name[N] table declarations in a kernel. Fits comfortably
   // in L1/L2 cache without dynamic allocation on the audio thread.
   static constexpr int kSampleMaxTableCells = 16384;
   static constexpr int kSampleMaxTables = 16;

   // Step 25 (OPEN-D): a kernel may declare more than one live audio-rate
   // input (`input sample audio <name>`) on top of the native "in" pin.
   // Matches Field::PinTable::kMaxDeclaredPins (16, the per-kernel combined
   // output+input declared-pin ceiling already enforced in
   // BackendRegister.cpp) - kept as its own constant here rather than
   // including PinTable.h, since SampleProgram.h is a leaf header consumed
   // from the audio thread.
   static constexpr int kSampleMaxDeclaredAudioInputs = 16;

   enum class SampleOp : uint8_t
   {
      Nop = 0,
      LoadImm,     // dst = imm
      LoadIn,      // dst = in  (per-sample shared input)
      LoadSr,      // dst = sr  (per-sample shared sample rate)
      LoadN,       // dst = n   (per-sample shared running sample counter)
      LoadFreq,    // dst = freq  (per-voice: the voice's current note frequency in Hz)
      LoadGate,    // dst = gate  (per-voice: 1.0 while the voice's note is held, 0.0 after note-off)
      LoadNoteOn,    // dst = noteOn  (Step 26: 1.0 only on the sample a note-on was registered, else 0.0 - an edge, not held)
      LoadNotePitch, // dst = notePitch  (Step 26: most-recently-registered note's frequency in Hz, MidiNoteToHz convention)
      LoadNoteVel,   // dst = noteVel  (Step 26: most-recently-registered note's velocity, 0..1)
      LoadDeclaredIn, // dst = declaredIns[a]  (Step 25: a declared 'input sample audio <name>', per-sample shared, indexed by declared-audio-input ordinal)
      LoadParam,   // dst = paramVals[a]  (per-sample shared, hoisted above the voice loop)
      LoadState,   // dst = stateCur[a]   (per-voice)
      StoreState,  // stateNext[a] = <src in b>  (per-voice; emitted once per cell at program end)
      Delay,       // dst = delay(src:b, line:a)  (Step 19: read delayed sample, write src, advance ring)
      LoadTable,   // dst = table[idx:b] (Step 24: table a, clamped index in reg b)
      StoreTable,  // table[idx:b] = src:c (Step 24: table a, clamped index in reg b, val in reg c)
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

   // Build step 12: `output <domain> <type> <name> = <expr>` /
   // `input <domain> <type> <name>` declared inside a sample-domain kernel.
   // Collected independently of FieldIR.h's DeclaredOutput/DeclaredInput
   // (S1.6 - the sample backend does not share the typed IR with
   // Element/Pixel) but mirrors its shape. `domainName` is stored as a
   // plain string rather than Field::Domain to keep this header free of a
   // FieldIR.h dependency.
   struct SampleDeclaredPin
   {
      std::string name;
      std::string typeName = "float";
      std::string domainName = "sample";
   };

   // Step 19: Delay line layout and transplant metadata for delay(x, samples) intrinsic.
   struct SampleDelayLine
   {
      int bufferOffset = 0;   // start index in delay buffer (0..kSampleMaxDelayCells-1)
      int length = 0;         // length in samples (N)
      int cursorIndex = 0;    // index into delayCursors (0..kSampleMaxDelayLines-1)
      int transplantFromOffset = -1; // -1 = fresh buffer (zeroed); >= 0 = copy from previous
      int transplantFromLength = 0;
      int transplantFromCursor = -1;
   };

   // Step 24: Table layout and transplant metadata for state float name[N] = init.
   struct SampleTable
   {
      std::string name;
      int bufferOffset = 0;   // start index in table buffer (0..kSampleMaxTableCells-1)
      int length = 0;         // length in floats (N)
      float initialValue = 0.0f;
      int transplantFromOffset = -1; // -1 = fresh buffer (zeroed/initVal); >= 0 = copy from previous
      int transplantFromLength = 0;
   };

   struct SampleProgram
   {
      std::vector<SampleInstr> code; // fixed-size after Compile(); audio thread only indexes it
      int numRegs = 0;

      int outReg = -1; // final register bound to 'out'; duplicated to both channels (mono kernel, v1)

      std::vector<SampleStateInit> state; // one entry per declared state cell
      std::vector<SampleParamSlot> params; // one entry per declared param
      std::vector<SampleDelayLine> delays; // Step 19: delay line allocations in AST order
      std::vector<SampleTable> tables;     // Step 24: table allocations in AST order

      bool hasReduceRms = false;
      float reduceLoHz = 20.0f;
      float reduceHiHz = 20000.0f;

      std::vector<SampleDeclaredPin> declaredOutputs; // build step 12
      std::vector<SampleDeclaredPin> declaredInputs;  // build step 12

      bool valid = false;
   };
}

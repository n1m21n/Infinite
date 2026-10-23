#pragma once

#include "FieldIR.h"
#include <string>
#include <vector>

namespace Field
{
   // An element kernel (Field Modifier) as one GLSL fragment pass over GPU
   // points: one fragment per point, the element loop run on it. The CPU
   // still runs the prologue (ElementVM::RunPrologue) and hands its results
   // in as uniforms, so only the per-element maths moves.
   //
   // The emitted text is a body, not a whole shader. It expects the caller's
   // prelude (GpuPointsGL::kPointPassHeader) to have declared `vec3 P`,
   // `float W` (sprite half-size, 0 = hidden), `vec3 Cd`, `int index`,
   // `void LoadPoint()` and the outputs `outPos` / `outColor`; it defines
   // main(), which loads the point, runs the loop and writes both outputs.
   //
   // It copies the element VM's semantics, not GLSL's: `/` and `%` by zero
   // give 0, comparisons and `if()` read lane 0, sqrt/log clamp their
   // argument, normalize of a zero vector is zero, `P /= 0` keeps P, and
   // rand/noise/sh hash the same 29 bits (the time half on the CPU, per call
   // site, the seed half here in 64-bit integer maths). Differences left:
   // float instead of double, and a `for` loop stops after kMaxLoopTrips.
   //
   // Anything it cannot copy exactly sets `error`, and the caller keeps the
   // kernel on the CPU: state cells, reduce, `.at()` neighbour reads, a
   // post-loop pass, reading or writing N (GPU points carry no normal), a
   // rand speed that is not a literal or a plain frame value, a local that
   // changes type.
   struct ElementGlslUniform
   {
      std::string glslName; // fld_u<k>
      std::string varName;  // the name ElementVM::ResolveName resolves
      int lanes = 1;
   };

   // One rand/noise/sh call site: a uvec2 uniform holding Xorwise(timeWord)
   // as (low, high) 32 bits, from TimeToRand's time half for this cook.
   struct ElementGlslRand
   {
      std::string glslName; // fld_r<k>
      bool isSh = false;
      bool speedIsLiteral = true;
      double speedLiteral = 1.0;
      std::string speedName; // resolved like a uniform when !speedIsLiteral
   };

   struct ElementGlslResult
   {
      std::string source;
      std::vector<ElementGlslUniform> uniforms;
      std::vector<ElementGlslRand> rands;
      // `uniform float fld_baseSize` is declared when this is set: the kernel
      // writes the `size` attrib, and W becomes size * fld_baseSize.
      bool writesSize = false;
      bool writesP = false;
      bool writesCd = false;
      std::string error;
   };

   constexpr int kMaxLoopTrips = 4096;

   ElementGlslResult EmitElementPointGlsl(const ElementIRProgram& program);

   // TimeToRand's time half: Xorwise((uint64)(frac(t / 300) * 2^29)), with
   // Sh's floor(t * speed) step when isSh. Low and high 32 bits.
   void ElementRandTimeWord(bool isSh, double speed, double t, unsigned int& lo, unsigned int& hi);
}

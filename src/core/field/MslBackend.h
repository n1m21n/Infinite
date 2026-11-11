#pragma once

// Field Pixel IR -> Metal Shading Language (OPUS_PLAN row 1.1). The same IR walk as GlslBackend, so one kernel
// gives the same picture on GL and on the Metal engine; the GLSL -> MSL differences live in one table at the top of
// MslBackend.cpp.
//
// The emitted library has two entry points:
//   vertex   fld_vs   full-screen triangle from vertex_id (no vertex buffer)
//   fragment fld_fs   the kernel
// Bindings:
//   buffer(0)  constant float4 fld_U[]   uniform slots, see below
//   texture(0) fld_srcTex                input image (a 1x1 black texture when unconnected)
//   texture(1) fld_s_bank0               state bank (only when the kernel declares state)
// Uniform slots (one float4 each):
//   0  res.x, res.y, t, dt
//   1  frame, age, srcAlpha, outMode (0 = write state cells, 1 = write display colour)
//   2+ one per hoisted value, then one per param; UniformSlot::location holds the slot index
// Memory layout matches GL: pixel row 0 is uv.y near 0, so a GL readback and a Metal readback compare row by row.

#include "GlslBackend.h"

namespace Field
{
   constexpr int kMslFixedSlots = 2;

   struct MslEmitResult
   {
      std::string source;
      std::vector<UniformSlot> uniforms; // location = float4 slot index in fld_U
      std::vector<StateSlot> state;
      std::string error;
      int branchCount = 0;
      int uniformSlotCount = kMslFixedSlots;
      int offsetReadCount = 0;
      bool usesOffsetReads = false;
   };

   MslEmitResult EmitMsl(const PixelIRProgram& program);

   // Row 1.3, the MSL twin of EmitGlslMaterialFn: void <fn>(thread FldMat& o, float3 P, float3 N, float2 UV, float fld_t)
   std::string MslMaterialPrelude();
   MaterialFnResult EmitMslMaterialFn(const PixelIRProgram& program, const std::string& fnName,
                                      const std::vector<float>& paramValues);
}

#pragma once

// GPU side of the PointZ family: per-point kernels run as a vertex shader with
// transform feedback (OpenGL 3.2 core, no compute shaders, so macOS works),
// plus two small helpers built on the same idea: a bounds reduction done by
// MAX-blending every point into one pixel, and the per-instance matrices
// Render 3D draws PointZ-driven instances with.

#include <string>
#include <vector>

#include "PopData.h"

namespace pop
{
   struct KernelIO
   {
      std::string name; // attribute name, also the GLSL local variable
      int comps = 1;    // 1..4 -> float, vec2, vec3, vec4
   };

   struct KernelUniform
   {
      enum Type { kFloat = 0, kInt, kVec2, kVec3, kVec4, kMat4 };
      std::string name;
      Type type = kFloat;
      float v[16] = { 0 };
      int i = 0;
   };

   // A kernel: GLSL statements run once per point. Every attribute in `reads`
   // and `writes` is a local variable of its own name, initialised from the
   // input (or its default when the input lacks it); `writes` are stored back
   // as new buffers (at most four per kernel, the GL 3.2 guarantee).
   // `lookups` adds random access: `fetch_<name>(int index)` reads that
   // attribute of any point (of input `lookupInput`, 0 or 1).
   // Built-ins: id (point index), uCount, uTime, uBeat, uDelta, uFrame, uSeed,
   // and when `usesImage` a sampler2D uImage with sampleImage(vec2 uv).
   struct KernelSpec
   {
      std::string decls;
      std::string body;
      std::vector<KernelIO> reads;
      std::vector<KernelIO> writes;
      std::vector<KernelIO> lookups;
      int lookupInput = 0;
      bool usesImage = false;
   };

   struct KernelEnv
   {
      double time = 0.0;  // transport seconds
      double beat = 0.0;
      float delta = 1.0f / 60.0f;
      int frame = 0;
      float seed = 0.0f;
      unsigned int imageTex = 0;
      const PopData* lookupData[2] = { nullptr, nullptr };
   };

   // GLSL type name for a component count.
   const char* GlslType(int comps);
   // True when `name` can be a GLSL identifier and is not reserved.
   bool ValidAttribName(const std::string& name);

   // The complete vertex shader for a kernel, plus the attribute names (bound
   // to locations 1..) and transform feedback varyings it uses.
   std::string BuildKernelSource(const KernelSpec& spec, const std::vector<KernelUniform>& uniforms,
                                 std::vector<std::string>& attribNames, std::vector<std::string>& varyings);

   // Runs `spec` over every point of `in`. On success `out` holds one new
   // buffer per entry of spec.writes, in order.
   bool RunKernel(const KernelSpec& spec, const PopData& in, const std::vector<KernelUniform>& uniforms,
                  const KernelEnv& env, std::vector<BufferPtr>& out, std::string& error);

   // Bounds of a GPU-resident position buffer without downloading it. Waits
   // for the GPU (two 1x1 reads), so not for per-frame use on a live cloud.
   bool GpuBounds(unsigned int buffer, int count, int comps, float lo[3], float hi[3]);

   // Per-consumer state for GpuBoundsAsync (one per Render 3D slot).
   struct AsyncBounds
   {
      unsigned int pbo = 0;
      void* fence = nullptr;
      bool have = false;
      int comps = 3;
      float lo[3] = { 0, 0, 0 };
      float hi[3] = { 0, 0, 0 };
      AsyncBounds() = default;
      AsyncBounds(const AsyncBounds&) = delete;
      AsyncBounds& operator=(const AsyncBounds&) = delete;
      ~AsyncBounds();
   };
   // The same reduction without stalling: returns the newest finished result
   // (about a frame old for an animated cloud) and queues the next one. Only
   // the very first call waits.
   bool GpuBoundsAsync(AsyncBounds& state, unsigned int buffer, int count, int comps, float lo[3], float hi[3]);

   // History shift on the GPU (Trail): a new buffer of `blocks` blocks of
   // `blockFloats` floats holding `newest` first, then the first blocks - 1
   // blocks of `history`. With no usable history every block is `newest`.
   BufferPtr GpuShiftIn(const BufferPtr& newest, const BufferPtr& history, int blockFloats, int blocks, int comps);

   // Per-instance transforms for points: four vec4 column buffers (M0..M3).
   // T(P + offset) * R(align) * R(Rotate + rotate) * S(PScale * scale * Scale *
   // scaleXYZ), all premultiplied by `model`; offset / rotate / scaleXYZ come
   // from points.instanceLook. alignMode: 0 none, 1 rotate +Z to N, 2 rotate
   // +Y to N. Points whose Active attribute is 0 collapse to zero scale.
   bool InstanceMatrices(const PopData& points, const float model[16], float scale, int alignMode,
                         std::vector<BufferPtr>& cols, std::string& error);

   // Shared GLSL helpers every kernel can call: hashi(id, seed) / hash3i,
   // hash11 / hash31 / hash33, gnoise(vec3) gradient noise -1..1, vnoise3
   // (three decorrelated gnoise), fbm(p, octaves, roughness), curl(p)
   // (divergence-free), rotX/rotY/rotZ (mat3, radians), eulerDeg (degrees
   // XYZ), alignVec(from, to), remap, PI, TAU.
   const char* KernelPreamble();
}

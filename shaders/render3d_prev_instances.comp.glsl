#version 430
// Turbo 0.70: each copy's matrix last frame, for Render 3D's motion blur.
// Pass 0 writes prevOut[i]: found by PartId in the ring of last frame's
// matrices (births and deaths reorder the copies), else last frame's matrix at
// the same index while the count held, else the current one (no motion).
// Pass 1 then stores this frame's matrices (ring by id, and by index) for the
// next frame. Same ring hash as the RTX mode (RtxShared.h RtxRingSlot).
layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer Col0 { float col0[]; };
layout(std430, binding = 1) readonly buffer Col1 { float col1[]; };
layout(std430, binding = 2) readonly buffer Col2 { float col2[]; };
layout(std430, binding = 3) readonly buffer Col3 { float col3[]; };
layout(std430, binding = 4) readonly buffer Ids { float partId[]; };
// 20 words per slot: 16 floats (matrix columns), id + 1 (0 = empty), key, 2 pad.
layout(std430, binding = 5) buffer Ring { uint ring[]; };
layout(std430, binding = 6) buffer PrevOut { float prevOut[]; };
layout(std430, binding = 7) buffer ByIndex { float byIndex[]; };

uniform int uPass;
uniform int uCount;
uniform ivec4 uStride;
uniform ivec4 uOffset;
uniform int uHasId;
uniform int uIdStride;
uniform int uByIndexValid;
uniform uint uKey;
uniform uint uMask;

vec4 Column(int c, uint i)
{
   uint b = uint(uOffset[c]) + i * uint(uStride[c]);
   if (c == 0) return vec4(col0[b], col0[b + 1u], col0[b + 2u], col0[b + 3u]);
   if (c == 1) return vec4(col1[b], col1[b + 1u], col1[b + 2u], col1[b + 3u]);
   if (c == 2) return vec4(col2[b], col2[b + 1u], col2[b + 2u], col2[b + 3u]);
   return vec4(col3[b], col3[b + 1u], col3[b + 2u], col3[b + 3u]);
}

uint Slot(uint id)
{
   return ((id * 2654435761u) ^ (uKey * 40503u + 0x9e3779b9u)) & uMask;
}

void main()
{
   uint i = gl_GlobalInvocationID.x;
   if (i >= uint(uCount))
      return;
   uint id = uHasId == 1 ? uint(max(partId[i * uint(uIdStride)] + 0.5, 0.0)) + 1u : 0u;
   if (uPass == 0)
   {
      bool found = false;
      if (uHasId == 1)
      {
         uint s = Slot(id) * 20u;
         if (ring[s + 16u] == id && ring[s + 17u] == uKey)
         {
            for (uint k = 0u; k < 16u; k++)
               prevOut[i * 16u + k] = uintBitsToFloat(ring[s + k]);
            found = true;
         }
      }
      else if (uByIndexValid == 1)
      {
         for (uint k = 0u; k < 16u; k++)
            prevOut[i * 16u + k] = byIndex[i * 16u + k];
         found = true;
      }
      if (!found)
         for (int c = 0; c < 4; c++)
         {
            vec4 v = Column(c, i);
            prevOut[i * 16u + uint(c) * 4u + 0u] = v.x;
            prevOut[i * 16u + uint(c) * 4u + 1u] = v.y;
            prevOut[i * 16u + uint(c) * 4u + 2u] = v.z;
            prevOut[i * 16u + uint(c) * 4u + 3u] = v.w;
         }
      return;
   }
   // Pass 1: this frame's, for the next.
   for (int c = 0; c < 4; c++)
   {
      vec4 v = Column(c, i);
      byIndex[i * 16u + uint(c) * 4u + 0u] = v.x;
      byIndex[i * 16u + uint(c) * 4u + 1u] = v.y;
      byIndex[i * 16u + uint(c) * 4u + 2u] = v.z;
      byIndex[i * 16u + uint(c) * 4u + 3u] = v.w;
      if (uHasId == 1)
      {
         uint s = Slot(id) * 20u + uint(c) * 4u;
         ring[s + 0u] = floatBitsToUint(v.x);
         ring[s + 1u] = floatBitsToUint(v.y);
         ring[s + 2u] = floatBitsToUint(v.z);
         ring[s + 3u] = floatBitsToUint(v.w);
      }
   }
   if (uHasId == 1)
   {
      uint s = Slot(id) * 20u;
      ring[s + 16u] = id;
      ring[s + 17u] = uKey;
   }
}

#pragma once

#include "Mesh.h"

// Turbo 0.70 (R1): GPU culling and LOD of one Render 3D instanced slot.
//
// A compute pass (shaders/render3d_instance_cull.comp.glsl) tests every
// instance's bounding sphere against the camera's frustum and, with a LOD
// size, splits off the instances that cover fewer pixels than that. Stable
// compaction and gathers (pop::gpu) then build the two lists' instance
// attributes, and the draws read their counts from the GPU
// (glDrawElementsIndirect) - nothing comes back to the CPU. The full list is
// drawn with the slot's own mesh, the small one as camera-facing quads.
// The instances keep their order, and aInstanceIndex (location 10) carries
// each one's number from before culling (the per-instance texture choice
// uses it), so the culled full list draws exactly what the plain draw did.
class Render3DCull
{
public:
   // One per-instance vertex attribute as the slot's VAO reads it: GL buffer,
   // components read, row stride and offset in floats. buf 0 = not enabled.
   struct Stream
   {
      unsigned int buf = 0;
      int size = 0;
      int stride = 0;
      int offset = 0;
   };
   enum { kCol0 = 0, kCol1, kCol2, kCol3, kColor, kTexIndex, kStreams };
   static const unsigned int kStreamLoc[kStreams]; // 3, 4, 5, 6, 7, 9
   static constexpr unsigned int kIndexLoc = 10;   // aInstanceIndex

   struct Request
   {
      Stream streams[kStreams];
      int count = 0;
      float localSphere[4] = { 0, 0, 0, 0 }; // the mesh's bounding sphere, object space
      Mat4 viewProj;
      float planes[6][4] = {};
      float projScaleY = 0.0f;               // proj[1][1] * viewport height / 2
      bool frustum = true;
      float lodPixels = 0.0f;                // 0 = no small list
      int indexCount = 0;                    // the mesh's indices (the full list's command)
   };

   ~Render3DCull();

   // Compute shaders and the pop::gpu primitives are there (GL 4.3).
   static bool Available();
   // Frustum planes of viewProj (Gribb-Hartmann), normalised, inside >= 0.
   static void FrustumPlanes(const Mat4& viewProj, float planes[6][4]);
   // About how much of the world box lo..hi is inside the six planes, 0..1
   // (4 x 4 x 4 points tested on the CPU).
   static float InsideFraction(const float planes[6][4], const float lo[3], const float hi[3]);

   // Sorts the instances of `r` into the two lists. False (the matrix
   // columns are not all there, or no compute): draw the plain way. Leaves no
   // program bound - the caller binds its own again.
   bool Run(const Request& r);
   // Points the bound VAO's instance attributes (and aInstanceIndex) at list
   // 0 (full) or 1 (small).
   void Bind(int list) const;
   // glDrawElementsIndirect of that list from the bound VAO.
   void Draw(int list, unsigned int prim) const;
   // Points the bound VAO's instance attributes back at the request's streams
   // and switches aInstanceIndex off.
   void Restore() const;
   // A unit quad (aPos -1..1, aNormal +Z, aUv) for the small list, with its
   // own copy of the instance attributes (Bind(1) after binding it).
   unsigned int QuadVao();
   void Release();

private:
   bool EnsureProgram();
   void EnsureCapacity(int count);

   Request mReq;
   int mCapacity = 0;
   unsigned int mProgram = 0;
   bool mProgramTried = false;
   unsigned int mFullFlags = 0, mSmallFlags = 0;
   unsigned int mIndices[2] = { 0, 0 }, mCounts[2] = { 0, 0 }, mCommands = 0;
   // Distinct (buffer, stride) sources of the streams, each gathered per list.
   unsigned int mSource[kStreams] = {};
   int mSourceStride[kStreams] = {};
   int mSources = 0;
   int mStreamSource[kStreams] = {};
   unsigned int mDst[2][kStreams] = {};
   int mDstFloats[2][kStreams] = {};
   unsigned int mQuadVao = 0, mQuadVbo = 0, mQuadIbo = 0;
};

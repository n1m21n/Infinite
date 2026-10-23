#pragma once

#include "Mesh.h"
#include "Render3DCull.h"

// Turbo 0.70: Render 3D's motion of every pixel in raster mode (the RTX mode
// has its own motion vectors, RtxOptix.cpp). Only while the TAA or a node
// after the render asks for it and the scene moves:
//  1. per Instance PointZ slot, each copy's matrix last frame - by PartId
//     through a ring of last frame's matrices (particles are born and die, so
//     the index is no identity), else by index while the count holds
//     (shaders/render3d_prev_instances.comp.glsl);
//  2. a velocity pass: the opaque meshes again, now and last frame, into a
//     velocity target (pixels; B = 1 where geometry was drawn) - the TAA's
//     motion for what moves on its own, and Render 3D FX's motion blur.
class Render3DMotion
{
public:
   // What one draw slot keeps across frames.
   struct Slot
   {
      const void* source = nullptr;
      unsigned int prevMat = 0; // this frame's copies' matrices last frame (mat4 each)
      unsigned int byIndex = 0; // this frame's matrices, for next frame's by-index lookup
      int capacity = 0;
      int lastCount = -1;       // copies byIndex holds (-1 = nothing)
      Mat4 prevModel;
      bool havePrevModel = false;
      void Release();
   };
   ~Render3DMotion();

   // Fills s.prevMat for `count` copies whose matrices the slot's VAO reads
   // from `cols`, and keeps this frame's for the next. key: tells slots apart
   // in the ring. False: no compute (the copies then have no motion of their own).
   bool PrevInstances(Slot& s, unsigned int key, const Render3DCull::Stream cols[4], unsigned int partIdBuf,
                      int partIdStride, int count);

   // The velocity target, bound and cleared, its program in use (unjittered
   // cameras: now and last frame; the camera's axes for LOD quads).
   bool BeginVelocity(int w, int h, const Mat4& viewProj, const Mat4& prevViewProj, const float camRight[3],
                      const float camUp[3]);
   // The next draw's placement: instanced (matrices at locations 3..6, last
   // frame's from prevMat by the copy's number - aInstanceIndex when `remap`,
   // a culled list) or model / prevModel; `lod` = Render3DCull's quads for
   // the mesh's box (centre, half extents). The caller then draws.
   void SetVelocityDraw(bool instanced, unsigned int prevMat, bool remap, bool lod, const float lodCentre[3],
                        const float lodHalf[3], const Mat4& model, const Mat4& prevModel);
   // A plain draw of `vao` (SetVelocityDraw without remap / LOD, then the draw).
   void DrawVelocity(unsigned int vao, unsigned int prim, int indexCount, int instances, bool instanced,
                     unsigned int prevMat, const Mat4& model, const Mat4& prevModel);
   void EndVelocity();
   unsigned int VelocityTexture() const { return mVelTex; }
   int VelocityWidth() const { return mW; }
   int VelocityHeight() const { return mH; }

   void Release();

private:
   bool EnsurePrograms();
   void EnsureTargets(int w, int h);
   unsigned int DummyBuffer(); // for an SSBO binding a draw does not read
   unsigned int mDummy = 0;

   unsigned int mPrevProgram = 0, mVelProgram = 0;
   bool mTried = false;
   int mW = 0, mH = 0;
   unsigned int mFbo = 0, mVelTex = 0, mDepth = 0;
   unsigned int mRing = 0;
   unsigned int mRingSlots = 0;
};

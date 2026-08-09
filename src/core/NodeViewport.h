#pragma once

#include "Geometry3DNodes.h"

// A tiny, cheap solo-render of one geometry-producing node's own mesh -
// independent of whatever Render 3D shows downstream. Deliberately far
// simpler than Render3DNode: one fixed directional light, no shadows, no
// HDRI, no MSAA, no instancing. The point is a fast glance at "what did this
// node actually produce" (including its selection, if any), not a final
// image.
//
// One instance is owned per opted-in GraphNode (see gNodeViewports in
// main.cpp), so its camera and GPU buffers persist across frames. The
// compiled shader programs are process-wide statics shared read-only across
// every instance - only the mesh buffers and FBO are per-instance.
class NodeViewport
{
public:
   ~NodeViewport();

   // Re-renders into an internally-owned FBO at (w,h) only if something
   // actually changed since the last call (mesh revision, camera, or size) -
   // otherwise reuses last frame's texture at the cost of a few comparisons.
   // Returns the color texture, or 0 if geo is null or its mesh is empty.
   unsigned int Render(IGeometrySource* geo, int w, int h, bool forceRedraw = false);

   // Drag-to-orbit / scroll-to-zoom, matched to DrawPreview's feel for
   // Render3DNode's embedded viewport in main.cpp.
   void Orbit(float dAzimuth, float dElevation);
   void Zoom(float wheelDelta);

private:
   bool EnsureFbo(int w, int h);
   void ReleaseFbo();
   void UploadMesh(const Mesh& mesh, unsigned long long revision);
   void UpdateSelectionBuffer(const Mesh& mesh, unsigned long long revision);

   unsigned int mFbo = 0, mColorTex = 0, mDepthBuffer = 0;
   int mWidth = 0, mHeight = 0;

   unsigned int mVao = 0, mVbo = 0, mIbo = 0;
   int mIndexCount = 0;
   unsigned long long mMeshRevision = 0;
   float mLo[3] = { 0, 0, 0 };
   float mHi[3] = { 0, 0, 0 };
   bool mHasBounds = false;

   // Instanced draw, mirroring Render3DNode::GpuMesh's instance fields
   // (Geometry3DNodes.h) - see FindInstancer in NodeViewport.cpp.
   unsigned int mInstanceVbo = 0;
   int mInstanceCount = 0;
   bool mInstanceAttribsOn = false;
   unsigned long long mInstanceRevision = (unsigned long long)-1;
   Mat4 mInstanceGroupMatrix;

   // Selected faces as their own index buffer, same technique as
   // Render3DNode's GpuMesh::selVao/selIbo (Geometry3DNodes.h).
   unsigned int mSelVao = 0, mSelIbo = 0;
   int mSelIndexCount = 0;
   unsigned long long mSelRevision = (unsigned long long)-1;

   // Orbit camera, auto-framed to the mesh bounds the first time a mesh
   // arrives, then left alone so a user's own drag/zoom sticks across frames.
   float mAzimuth = 0.7f, mElevation = 0.45f, mDistance = 3.0f;
   float mTarget[3] = { 0, 0, 0 };
   bool mFramed = false;

   unsigned long long mLastRenderedRevision = (unsigned long long)-1;
   float mLastAzimuth = 0, mLastElevation = 0, mLastDistance = 0;
   // The mesh's revision stamp only bumps when its vertices/indices actually
   // rebuild - a pos/rot/scale slider moves GetModelMatrix() without
   // rebuilding the mesh at all, so the change gate has to watch the model
   // matrix and material separately or the viewport freezes on transform-only
   // edits.
   Mat4 mLastModel;
   Material mLastMaterial;
   unsigned int mLastSurfaceTexture = 0;
   // Content stamp for the surface texture's pixels, distinct from the GL
   // handle above: a texture-producing node (Formula, Filter, ...) reuses the
   // same FBO texture object across recooks, so the handle alone can't tell a
   // preset switch from a no-op. See IGeometrySource::SurfaceTextureRevision().
   unsigned long long mLastSurfaceTextureRevision = 0;
   bool mHasRendered = false;
};

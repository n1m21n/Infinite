#pragma once

#include "Geometry3DNodes.h"
#include "SharedViewportCamera.h"

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
   // `cam` is owned by the caller (main.cpp keys one per GraphNode, shared
   // between a node's inline mini-viewport and its viewport-panel card so
   // the two agree, but never shared across different nodes) - this instance
   // only reads it and tracks the value it last rendered.
   unsigned int Render(IGeometrySource* geo, const SharedViewportCamera& cam, int w, int h,
                       bool forceRedraw = false);

   // Drag-to-orbit / scroll-to-zoom, matched to DrawPreview's feel for
   // Render3DNode's embedded viewport in main.cpp. Orbit takes degree deltas
   // (same convention as DrawPreview's drag math) and mutates the caller-owned
   // `cam` directly. Zoom mutates this instance's own mDistance - see
   // SharedViewportCamera.h for why that stays out of the shared struct.
   void Orbit(SharedViewportCamera& cam, float dAzimuth, float dElevation);
   void Zoom(float wheelDelta);

private:
   bool EnsureFbo(int w, int h);
   void ReleaseFbo();
   void UploadMesh(const Mesh& mesh, unsigned long long revision);
   void UpdateSelectionBuffer(const Mesh& mesh, unsigned long long revision);
   void UploadPoints(const std::vector<Particle>& points, unsigned long long revision);

   unsigned int mFbo = 0, mColorTex = 0, mDepthBuffer = 0;
   int mWidth = 0, mHeight = 0;

   unsigned int mVao = 0, mVbo = 0, mIbo = 0;
   int mIndexCount = 0;
   // Vertex count, used instead of mIndexCount to preview a vertices-only
   // mesh (Points to Vertices' output - no edges, no faces) as GL_POINTS
   // rather than the blank "no geometry" placeholder.
   int mVertexCount = 0;
   unsigned long long mMeshRevision = 0;
   // Per-vertex colour from a Set Color upstream, same technique as
   // Render3DNode::GpuMesh::vertexColorVbo (Geometry3DNodes.h/.cpp) - a mesh
   // with no Set Color leaves this disabled and the shader falls back to
   // vec3(1.0) via uHasVertexColor.
   unsigned int mVertexColorVbo = 0;
   bool mHasVertexColor = false;
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
   // Mirrors Render3DNode::GpuMesh::instanceOverrideRevision - the mesh
   // revision last seen when the instance buffer was uploaded, so a
   // selectionOnly Delete/Transform's InstanceTransformOverride() changing
   // (without touching the raw instancer's own revision or group matrix)
   // still triggers a re-upload. See ResolveInstanceTransforms in
   // NodeViewport.cpp.
   unsigned long long mInstanceOverrideRevision = (unsigned long long)-1;

   // Selected faces as their own index buffer, same technique as
   // Render3DNode's GpuMesh::selVao/selIbo (Geometry3DNodes.h).
   unsigned int mSelVao = 0, mSelIbo = 0;
   int mSelIndexCount = 0;
   unsigned long long mSelRevision = (unsigned long long)-1;
   // Whether mSelVao's aInstance attribs (2-5) currently point at mInstanceVbo.
   // Configured in the draw path alongside mInstanceAttribsOn, never inside
   // UpdateSelectionBuffer - mInstanceVbo may still be 0 (ungenerated) the
   // first time UpdateSelectionBuffer runs, and binding it there would wire
   // mSelVao's attribs to buffer 0 permanently.
   bool mSelInstanceAttribsOn = false;

   // Instance-domain selection fallback (Part 1 step 5 of
   // local-prompts/instance-selection.md): the stamp mesh has no faceMask to
   // build mSelVao/mSelIbo from (Select operated on instance placements, not
   // faces), so instead mSelIbo holds the *whole* stamp's indices and this
   // holds just the selected instances' composed transforms, drawn instanced
   // over only that subset - whole instances tint, not a per-face highlight.
   // Rebuilt whenever the instance-selection draw block runs at all (which
   // only happens on an actual redraw already gated above), so it carries no
   // revision cache of its own.
   unsigned int mSelInstanceOverrideVbo = 0;
   int mSelInstanceOverrideCount = 0;

   // A pure point-cloud source (Particle System) has no mesh at all - GetMesh()
   // is a permanently-empty stub, see ParticleSystemNode::GetMesh
   // (SimulationNodes.h). Render3DNode draws that case as camera-facing
   // billboard sprites (Geometry3DNodes.cpp's drawCloudSlot); this thumbnail
   // takes the much cheaper route of plain round GL_POINTS, which is enough to
   // tell a preview apart from the "no geometry" placeholder without a second
   // sprite/instancing pipeline.
   unsigned int mPointVao = 0, mPointVbo = 0;
   int mPointCount = 0;
   unsigned long long mLastPointRevision = (unsigned long long)-1;

   // Orbit camera. Distance/target are auto-framed to the mesh bounds the
   // first time a mesh arrives, then left alone so a user's own zoom sticks
   // across frames - both stay per-instance since they depend on this node's
   // own mesh scale. Azimuth/elevation are NOT stored here - they live in the
   // caller-owned SharedViewportCamera passed into Render()/Orbit(), so a
   // node's own camera survives independently of which NodeViewport instance
   // (inline vs panel) happens to be rendering it right now.
   float mDistance = 3.0f;
   float mTarget[3] = { 0, 0, 0 };
   bool mFramed = false;
   // A growing point cloud (Particle System, Cloth) keeps expanding well past
   // the tiny bounds it had on its first cook, so the one-shot mFramed latch
   // above leaves the camera locked inside the emitter forever. For point
   // clouds only, Render() re-runs the framing block whenever the bounds grow
   // well past what was last framed - see mFramedRadius - but only until the
   // user actually touches the camera themselves (Orbit/Zoom), tracked here so
   // their manual framing always wins and never gets silently overridden.
   bool mUserAdjusted = false;
   float mFramedRadius = 0.0f;

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

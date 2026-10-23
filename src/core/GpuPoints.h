#pragma once

#include "Mesh.h"

#include <functional>

// A point cloud that lives on the GPU: two textures, one point per texel,
// point i at texel (i % texW, i / texW). Render 3D and the node mini viewport
// draw it without the CPU ever seeing the points (docs/plans/gpu-points).
//
// A source offers this through IGeometrySource::GetGpuPoints() *beside* its
// CPU cloud, never instead of it: every other consumer keeps reading
// GetPointCloud(), and a node that does not forward GetGpuPoints() makes the
// consumers below it fall back to that CPU cloud, which is correct, only slower.
struct GpuPoints
{
   // RGBA32F: xyz = position in point space, w = sprite half-size in world
   // units (p.scale * PointBaseSize() on the CPU path). w == 0 hides the point:
   // a cell under the threshold, a dead particle.
   unsigned int posTex = 0;
   // RGBA32F: rgb = point colour (full float, so a tint reads back exactly as
   // the CPU path's float colour does); a = how far through its life the
   // point is (PointLifeFraction: age / life, 0 for anything not a mortal
   // particle), which Field Modifier reads as its `life` attrib.
   unsigned int colorTex = 0;
   int texW = 0;
   int texH = 0;
   // Texels to draw, from 0; hidden ones among them cost a vertex, not a pixel.
   int count = 0;
   // From NextMeshRevision(); changes iff the texels do. Same stamp the
   // source's PointCloudRevision() reports for the matching CPU cloud.
   unsigned long long revision = 0;
   // Applied to each position before the source's GetModelMatrix(): how a
   // Transform downstream moves the points without copying them. Sprite size
   // does not follow it, matching the CPU path (Transform bakes positions only).
   Mat4 pointMatrix;
   // Point-space bounds of the positions (before pointMatrix, sprite radius
   // not included) and the largest w, for camera fitting and shadow volumes.
   float lo[3] = { 0, 0, 0 };
   float hi[3] = { 0, 0, 0 };
   float maxHalfSize = 0.0f;
   bool hasBounds = false;
   // Points likely visible (w > 0), for Render 3D's fill estimate; may lag a
   // frame (occlusion query). -1 = unknown, use count.
   int liveCount = -1;
};

namespace GpuPointsGL
{
   // False when INFINITE_GPU_POINTS=0: producers then offer no GpuPoints and
   // everything draws through the CPU cloud, as before (A/B checks, fallback).
   bool Enabled();
   // GPUPOINTSTEST renders the same chain both ways in one process.
   void SetEnabledForTest(bool enabled);

   // A producer's storage: the two textures as colour attachments 0 (position)
   // and 1 (colour) of one framebuffer, so a single fragment pass writes both.
   struct Target
   {
      unsigned int fbo = 0;
      unsigned int posTex = 0;
      unsigned int colorTex = 0;
      int w = 0;
      int h = 0;
   };
   bool EnsureTarget(Target& t, int w, int h, const char* label);
   void DestroyTarget(Target& t);
   // Binds a program's two outputs to the target's attachments: `posOut` to
   // attachment 0, `colorOut` to 1. Call with the target's FBO bound and the
   // program linked (GLSL 150 has no layout qualifiers to fix them).
   void BindOutputs(const Target& t, unsigned int program, const char* posOut, const char* colorOut);

   // For a source whose points are made on the CPU (Mesh to Points, a .ply):
   // writes `points` into `t` (resized to fit, 1024 texels a row) and fills
   // `out` with the texels, count and bounds the way Render 3D's CPU cloud
   // draw would read them: w = p.scale * baseSize, 0 for a dead particle.
   // The caller sets out.revision. False (out untouched) for an empty cloud
   // or a missing target.
   bool Upload(Target& t, GpuPoints& out, const std::vector<Particle>& points, float baseSize, const char* label);

   // A per-point operator's pass (Twist, Displacement, Field Modifier): one
   // fragment per texel of `src`, written into `t` (resized to match). The
   // program reads the source through kPointPassHeader's uPointPos and
   // uPointColor (units 0 and 1, bound here) and uPointMatrix (src's), so it
   // works in the space the CPU cloud is in; `setup` binds everything else,
   // on units 2 and up. Fills `out`'s textures, size and count, with an
   // identity matrix; bounds, live count and revision are the caller's.
   bool RunPointPass(Target& t, GpuPoints& out, const GpuPoints& src, unsigned int program,
                     const std::function<void()>& setup, const char* label);
   // GLSL 150 prelude for RunPointPass programs. Declares the inputs and the
   // two outputs, and defines `P` (position after uPointMatrix), `W` (sprite
   // half-size, 0 = hidden), `Cd` (colour), `Life` (life fraction, colour
   // alpha) and `index` for the point this fragment owns. The program's main()
   // ends with `outPos = vec4(P, W); outColor = vec4(Cd, Life);`.
   extern const char* const kPointPassHeader;
}

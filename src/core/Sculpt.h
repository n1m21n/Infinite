#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mesh.h"

// Sculpting as data: an offset per welded point of a mesh, painted by brushes
// (Blender's sculpt brushes, the maths written fresh) and blended back in by
// an amount, like a shape key. Vertices split at UV or normal seams share one
// point, so a stroke never opens a crack.
namespace Sculpt
{
   enum Brush { kDraw = 0, kSmooth, kGrab, kInflate, kPinch, kFlatten, kCrease, kBrushCount };
   const char* BrushName(int brush);

   // Which vertices make one point, who neighbours whom, and which faces
   // shade each vertex (faces whose corner normal agrees with it, so hard
   // edges stay hard and smooth seams stay smooth).
   struct Topology
   {
      std::vector<unsigned int> point;        // per vertex
      int points = 0;
      std::vector<unsigned int> nbrStart, nbr;   // per point, CSR
      std::vector<unsigned int> shadeStart, shadeFace;   // per vertex, CSR
      std::vector<unsigned char> mask;        // per point: 1 when a selected face touches it
      std::vector<unsigned int> pointVertStart, pointVert;   // per point: its vertices, CSR
      std::vector<unsigned int> pointFaceStart, pointFace;   // per point: faces with a corner there, CSR
      std::vector<unsigned int> indices;      // what it was built from
      size_t vertexCount = 0;
   };
   // Rebuilds only when the triangles changed.
   void Build(const Mesh& base, Topology& t);

   // base + amount * offsets, with normals recomputed (base unchanged when
   // every offset is zero or amount is 0).
   void Apply(const Mesh& base, const Topology& t, const std::vector<float>& offsets, float amount, Mesh& out);

   // Apply for a few points only (a dab's): their vertices move, and every
   // vertex shading from a face they touch gets a new normal. `out` must hold
   // Apply's result from before the change. `changed` gets each point whose
   // position or normal moved. False when `out` does not fit (call Apply).
   bool ApplyPoints(const Mesh& base, const Topology& t, const std::vector<float>& offsets, float amount,
                    const std::vector<unsigned int>& points, Mesh& out, std::vector<unsigned int>& changed);

   // One point of the shaped mesh: its position and smooth normal.
   void PointFrame(const Mesh& shaped, const Topology& t, std::vector<float>& pos, std::vector<float>& nrm);
   // The same for some points only.
   void PointFrameAt(const Mesh& shaped, const Topology& t, const std::vector<unsigned int>& points,
                     std::vector<float>& pos, std::vector<float>& nrm);

   // Points bucketed by position, so a brush or a ray looks at its
   // neighbourhood only. Moved points are re-filed with MoveInGrid.
   struct Grid
   {
      float cell = 0.0f;
      float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };   // bounds at the build, one cell wider
      std::vector<float> at;   // positions at the build
      std::unordered_map<uint64_t, std::vector<unsigned int>> cells;
   };
   // Cells must be at least two of the mesh's longest edges wide for Raycast.
   void BuildGrid(const std::vector<float>& pos, float cell, Grid& g);
   // Re-files points that moved (pos is their new place).
   void MoveInGrid(const std::vector<float>& pos, const std::vector<unsigned int>& points, Grid& g);

   // Nearest hit of a ray (d need not be unit) on the mesh; t in units of d.
   // With a grid (built on this mesh's points), only faces near the ray.
   bool Raycast(const Mesh& m, const float o[3], const float d[3], float& t, const Topology* topo = nullptr,
                const Grid* grid = nullptr);

   // A brush image: brightness per pixel, rows from the bottom.
   struct Alpha
   {
      int w = 0, h = 0;
      std::vector<float> lum;
      // Bilinear at (u, v) in 0-1; 0 outside.
      float Sample(float u, float v) const;
   };

   struct Dab
   {
      int brush = kDraw;
      float center[3] = { 0, 0, 0 };
      float radius = 0.1f;
      float strength = 0.5f;
      bool invert = false;
      int symmetry = 0;   // bits: 1 X, 2 Y, 4 Z (mirrored through the mesh's origin)
      // An image laid on the view plane over the brush (Blender's texture
      // brush, View mapping): each point's touch is scaled by the image's
      // brightness under it. right/up: the view's axes in mesh space.
      const Alpha* alpha = nullptr;
      float right[3] = { 1, 0, 0 }, up[3] = { 0, 1, 0 };
      // Normals (per point) the brush's area normal is taken from, when not the shown surface's: Blender
      // keeps a stroke's normals as it began, so a ridge does not tip its own brush forward.
      const std::vector<float>* areaNormals = nullptr;
      // Which way the brush moved since its last dab, in mesh space (zero at a stroke's first dab). Pinch
      // pulls across it and waits for it, as Blender's does.
      float dir[3] = { 0, 0, 0 };
   };
   // Adds one dab to `offsets` (per point, xyz), measured on the shaped
   // mesh's points. Offsets are in the sculpt's own units: a dab moves the
   // shown surface by its amount, so they are divided by `amount`.
   // `touched` (optional) gets the points it moved; `grid` limits the search.
   void Stroke(const Dab& d, const std::vector<float>& pos, const std::vector<float>& nrm, const Topology& t,
               float amount, std::vector<float>& offsets, std::vector<unsigned int>* touched = nullptr,
               const Grid* grid = nullptr);

   // Grab: the points under the brush at the press, and how much each follows.
   struct GrabHold
   {
      std::vector<unsigned int> idx;
      std::vector<float> weight;
      std::vector<int> flip;        // symmetry pass each entry belongs to (bits of axes mirrored)
      std::vector<float> start;     // offsets at the press, all points
   };
   void GrabBegin(const Dab& d, const std::vector<float>& pos, const Topology& t, const std::vector<float>& offsets,
                  GrabHold& hold);
   // move: how far the mouse has dragged the surface since the press, in mesh space.
   void GrabMove(const GrabHold& hold, const float move[3], float amount, std::vector<float>& offsets);

   // Only the moved points, quantised, as base64 for a Text param.
   std::string Encode(const std::vector<float>& offsets, int points);
   // False on a blob it cannot read. points = how many the mesh had.
   bool Decode(const std::string& s, std::vector<float>& offsets, int& points);
}

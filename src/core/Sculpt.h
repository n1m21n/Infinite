#pragma once

#include <string>
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
      std::vector<unsigned int> indices;      // what it was built from
      size_t vertexCount = 0;
   };
   // Rebuilds only when the triangles changed.
   void Build(const Mesh& base, Topology& t);

   // base + amount * offsets, with normals recomputed (base unchanged when
   // every offset is zero or amount is 0).
   void Apply(const Mesh& base, const Topology& t, const std::vector<float>& offsets, float amount, Mesh& out);

   // One point of the shaped mesh: its position and smooth normal.
   void PointFrame(const Mesh& shaped, const Topology& t, std::vector<float>& pos, std::vector<float>& nrm);

   struct Dab
   {
      int brush = kDraw;
      float center[3] = { 0, 0, 0 };
      float radius = 0.1f;
      float strength = 0.5f;
      bool invert = false;
      int symmetry = 0;   // bits: 1 X, 2 Y, 4 Z (mirrored through the mesh's origin)
   };
   // Adds one dab to `offsets` (per point, xyz), measured on the shaped
   // mesh's points. Offsets are in the sculpt's own units: a dab moves the
   // shown surface by its amount, so they are divided by `amount`.
   void Stroke(const Dab& d, const std::vector<float>& pos, const std::vector<float>& nrm, const Topology& t,
               float amount, std::vector<float>& offsets);

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

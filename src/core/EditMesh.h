#pragma once

#include <string>
#include <vector>

#include "Mesh.h"

// Edit Mode as data: a list of ops replayed on the input mesh, one per line.
//
//   sel f cx cy cz nx ny nz ...   faces (triangles) by centre and normal
//   sel v px py pz ...            points by position
//   sel e ax ay az bx by bz ...   edges by their two ends
//   sel all
//   move dx dy dz                 the selected points
//   rotate ax ay az               degrees, X then Y then Z, about their median
//   scale sx sy sz                about their median
//   extrude d                     the selected region, along its normal; the cap stays selected
//   inset w                       the selected faces; the inner faces stay selected
//   delete                        the selected faces
//
// Elements are named by where they are, not by index, so the list still means
// the same thing after an upstream change that keeps the shape roughly in
// place. A selector with nothing near it is skipped and counted in `missed`.
namespace EditMesh
{
   Mesh Apply(const Mesh& in, const std::string& ops, int* missed = nullptr);

   // Selector text for one element of `m`, for the 3D View to append to a sel line.
   std::string FaceSelector(const Mesh& m, size_t face);
   std::string PointSelector(const Mesh& m, unsigned int vertex);

   // The triangles of the flat patch around `face` (within `angleDeg`, sharing
   // points), so a click selects the whole quad or n-gon as Blender does.
   std::vector<size_t> FlatPatch(const Mesh& m, size_t face, float angleDeg = 1.0f);

   // Points (vertices) and faces the selection of the last sel line in `ops`
   // covers on `m`, for the highlight; `m` is the mesh before that line's ops ran.
   struct Selection
   {
      std::vector<unsigned char> point;   // per vertex
      std::vector<unsigned char> face;    // per triangle
   };
   Selection Select(const Mesh& m, const std::string& selLine, int* missed = nullptr);

   // Region extrude: the selected faces are lifted by `distance` along their
   // area-weighted normal and walled back to the region's outline. The input's
   // other faces pass through; the cap is selected in the output.
   Mesh ExtrudeRegion(const Mesh& in, float distance);
}

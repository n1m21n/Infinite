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
//   rotate x y z deg              about the axis (x y z) through their median
//   scale sx sy sz                about their median
//   extrude d                     the selected region, along its normal; the cap stays selected
//   inset w                       the selected faces; the inner faces stay selected
//   delete                        the selected faces; after sel v / sel e, the faces (whole quads or
//                                 n-gons) that use the points or edges, as Blender's Delete does
//
// Elements are named by where they are, not by index, so the list still means
// the same thing after an upstream change that keeps the shape roughly in
// place. A selector with nothing near it is skipped and counted in `missed`.
//
// Optional trailing data keeps an edit meaning the same when the upstream mesh is refined
// (Cube detail 4 -> 96). Without it, a line replays by nearest match as it always did:
//   sel f c n c n ... t  a b c  a b c ...   `t`: the three corners of each face. A face selector then picks
//                                           every face of the mesh whose centre lies inside that triangle
//                                           (same side, within 0.15 of its longest edge), so a face made on a
//                                           coarse mesh is the whole footprint on a refined one. No hit: nearest.
//   sel v p p ... r  R  R ...               `r`: how far each point reached (its shortest edge). When the mesh
//                                           now has edges under 0.6 R there, move / rotate / scale reach the
//                                           points within R as a linear tent (sum capped at 1), so a moved
//                                           corner is a smooth bump of the same footprint, never a spike.
//                                           Delete after sel v still takes only the faces on the exact point.
// A line with no marker, or a mesh whose topology is unchanged, replays exactly as before.
// A trailing sel line is the selection Edit Mode shows; `selection` gets it.
namespace EditMesh
{
   struct Selection
   {
      std::vector<unsigned char> point;   // per vertex
      std::vector<unsigned char> face;    // per triangle
      std::vector<float> soft;            // per vertex, only for refined points: how much of a move reaches it (0..1)
      char kind = 'f';                    // what the sel line named: f faces, v points, e edges, a all
      std::vector<std::pair<unsigned int, unsigned int>> edges;   // for 'e': the two points of each edge
   };

   Mesh Apply(const Mesh& in, const std::string& ops, int* missed = nullptr, Selection* selection = nullptr);

   // Selector text for one element of `m`, for the 3D View to append to a sel line.
   std::string FaceSelector(const Mesh& m, size_t face);
   std::string PointSelector(const Mesh& m, unsigned int vertex);

   // The whole sel line tail for the picked faces / points, with the optional footprint data (see above).
   std::string FaceSelectors(const Mesh& m, const std::vector<unsigned char>& face);
   std::string PointSelectors(const Mesh& m, const std::vector<unsigned int>& vertices);

   // The triangles of the flat patch around `face` (within `angleDeg`, sharing
   // points), so a click selects the whole quad or n-gon as Blender does.
   std::vector<size_t> FlatPatch(const Mesh& m, size_t face, float angleDeg = 1.0f);

   // A point id per vertex, shared by coincident vertices (a cube's split corners).
   std::vector<int> PointIds(const Mesh& m, int* count = nullptr);

   // The points and faces one sel line picks on `m`.
   Selection Select(const Mesh& m, const std::string& selLine, int* missed = nullptr);

   // Region extrude: the selected faces are lifted by `distance` along their
   // area-weighted normal and walled back to the region's outline. The input's
   // other faces pass through; the cap is selected in the output.
   Mesh ExtrudeRegion(const Mesh& in, float distance);
}

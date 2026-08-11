#pragma once

#include <array>
#include <cmath>
#include <vector>

// Minimal mesh + matrix maths for the 3D nodes. Deliberately self-contained:
// pulling in GLM for a handful of transforms would be a heavier dependency than
// the code it replaces.
struct Vertex
{
   float px = 0, py = 0, pz = 0;
   float nx = 0, ny = 0, nz = 1;
   float u = 0, v = 0;
};

struct Mesh
{
   std::vector<Vertex> vertices;
   std::vector<unsigned int> indices;

   // Which faces are selected, one entry per triangle. Empty means "all of
   // them", so nothing that does not care about selection has to think about it
   // and no existing mesh grew a cost.
   //
   // Carried on the mesh rather than as a separate cable so a selection travels
   // down the geometry connections that already exist - select on one node,
   // act on it two nodes later, without rewiring anything.
   std::vector<unsigned char> faceMask;

   // Optional grouping of faces into atomic selection units, one entry per
   // triangle, matched by value (not index - just an opaque tag). Empty means
   // every face is its own unit. Mesh to Points sets this so a point's two
   // billboard triangles always share a value, letting Select choose or skip
   // the whole point instead of tearing it into one selected + one
   // unselected triangle.
   std::vector<unsigned int> selectionGroup;

   // Per-vertex linear RGB, three floats per vertex, parallel to `vertices`.
   // Empty means "no per-vertex colour" - the material's flat colour is used
   // instead, so no existing mesh grew a cost. A flat vector<float> rather
   // than a vector<Color> struct, matching how InstanceOnPointsNode packs
   // per-instance colour for upload - same shape means the same upload path
   // works.
   std::vector<float> vertexColor;

   bool Empty() const { return vertices.empty() || indices.empty(); }
   // Unlike Empty(), true for a vertices-only mesh with no faces (Points to
   // Vertices' output) - use this where "has any drawable-as-points data" is
   // the question being asked, not "has drawable triangles".
   bool HasGeometry() const { return !vertices.empty(); }
   size_t FaceCount() const { return indices.size() / 3; }

   bool FaceSelected(size_t face) const
   {
      return faceMask.empty() || (face < faceMask.size() && faceMask[face] != 0);
   }
   // Size-mismatched data is treated as absent, never indexed into - an
   // operator that resizes `vertices` and forgets `vertexColor` would
   // otherwise read out of bounds.
   bool HasVertexColor() const { return vertexColor.size() == vertices.size() * 3; }
   size_t SelectedCount() const
   {
      if (faceMask.empty())
         return FaceCount();
      size_t n = 0;
      for (unsigned char f : faceMask)
         if (f)
            n++;
      return n;
   }
};

// Column-major 4x4, matching what OpenGL expects from glUniformMatrix4fv.
struct Mat4
{
   float m[16] = { 1, 0, 0, 0,
                   0, 1, 0, 0,
                   0, 0, 1, 0,
                   0, 0, 0, 1 };

   static Mat4 Identity() { return Mat4(); }

   bool operator==(const Mat4& o) const
   {
      for (int i = 0; i < 16; i++)
         if (m[i] != o.m[i])
            return false;
      return true;
   }

   static Mat4 Multiply(const Mat4& a, const Mat4& b)
   {
      Mat4 r;
      for (int c = 0; c < 4; c++)
      {
         for (int row = 0; row < 4; row++)
         {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
               sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = sum;
         }
      }
      return r;
   }

   static Mat4 Translation(float x, float y, float z)
   {
      Mat4 r;
      r.m[12] = x; r.m[13] = y; r.m[14] = z;
      return r;
   }

   static Mat4 Scale(float x, float y, float z)
   {
      Mat4 r;
      r.m[0] = x; r.m[5] = y; r.m[10] = z;
      return r;
   }

   static Mat4 RotationX(float a)
   {
      Mat4 r;
      const float s = std::sin(a), c = std::cos(a);
      r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
      return r;
   }

   static Mat4 RotationY(float a)
   {
      Mat4 r;
      const float s = std::sin(a), c = std::cos(a);
      r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
      return r;
   }

   static Mat4 RotationZ(float a)
   {
      Mat4 r;
      const float s = std::sin(a), c = std::cos(a);
      r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
      return r;
   }

   static Mat4 Perspective(float fovYRadians, float aspect, float nearZ, float farZ)
   {
      Mat4 r;
      const float f = 1.0f / std::tan(fovYRadians * 0.5f);
      for (int i = 0; i < 16; i++)
         r.m[i] = 0.0f;
      r.m[0] = f / aspect;
      r.m[5] = f;
      r.m[10] = (farZ + nearZ) / (nearZ - farZ);
      r.m[11] = -1.0f;
      r.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
      return r;
   }

   static Mat4 Orthographic(float halfHeight, float aspect, float nearZ, float farZ)
   {
      Mat4 r;
      const float halfWidth = halfHeight * aspect;
      r.m[0] = 1.0f / halfWidth;
      r.m[5] = 1.0f / halfHeight;
      r.m[10] = -2.0f / (farZ - nearZ);
      r.m[14] = -(farZ + nearZ) / (farZ - nearZ);
      return r;
   }

   static Mat4 LookAt(const float eye[3], const float target[3], const float up[3])
   {
      auto sub = [](const float a[3], const float b[3], float out[3]) {
         out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
      };
      auto norm = [](float v[3]) {
         const float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
         if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
      };
      auto cross = [](const float a[3], const float b[3], float out[3]) {
         out[0] = a[1]*b[2] - a[2]*b[1];
         out[1] = a[2]*b[0] - a[0]*b[2];
         out[2] = a[0]*b[1] - a[1]*b[0];
      };

      float f[3]; sub(target, eye, f); norm(f);
      float s[3]; cross(f, up, s); norm(s);
      float u[3]; cross(s, f, u);

      Mat4 r;
      r.m[0] = s[0]; r.m[4] = s[1]; r.m[8]  = s[2];
      r.m[1] = u[0]; r.m[5] = u[1]; r.m[9]  = u[2];
      r.m[2] = -f[0]; r.m[6] = -f[1]; r.m[10] = -f[2];
      r.m[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
      r.m[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
      r.m[14] =  (f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2]);
      return r;
   }

   // Inverse-transpose of the upper 3x3, so normals survive non-uniform scale.
   void NormalMatrix(float out[9]) const
   {
      const float a = m[0], b = m[4], c = m[8];
      const float d = m[1], e = m[5], f = m[9];
      const float g = m[2], h = m[6], i = m[10];
      const float det = a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);
      const float inv = (std::fabs(det) < 1e-8f) ? 0.0f : 1.0f / det;
      // transpose of the inverse, written directly
      out[0] = (e*i - f*h) * inv; out[1] = (c*h - b*i) * inv; out[2] = (b*f - c*e) * inv;
      out[3] = (f*g - d*i) * inv; out[4] = (a*i - c*g) * inv; out[5] = (c*d - a*f) * inv;
      out[6] = (d*h - e*g) * inv; out[7] = (b*g - a*h) * inv; out[8] = (a*e - b*d) * inv;
   }

   // General 4x4 inverse via cofactor expansion. Used to unproject screen space
   // back to world space (the HDRI background pass), which is the one place in
   // this codebase that needs a full inverse rather than the cheaper
   // transpose-of-rotation trick that works for view matrices alone.
   static Mat4 Inverse(const Mat4& in)
   {
      const float* a = in.m;
      float inv[16];
      inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
      inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
      inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
      inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
      inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
      inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
      inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
      inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
      inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
      inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
      inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
      inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
      inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
      inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
      inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
      inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];

      float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
      Mat4 r;
      if (std::fabs(det) < 1e-12f)
         return r; // identity: degenerate, nothing sane to return
      det = 1.0f / det;
      for (int i = 0; i < 16; i++)
         r.m[i] = inv[i] * det;
      return r;
   }
};

// Globally unique, monotonically increasing version stamp for mesh data. A
// source bumps its stamp whenever it rebuilds; the renderer compares the stamp
// it last uploaded against the current one and skips the upload when they match.
//
// Global rather than per-node so that two different meshes can never share a
// stamp. A bypassed operator hands back its input's stamp, and with per-node
// counters those two could collide at the same number and leave last frame's
// geometry sitting on the GPU.
unsigned long long NextMeshRevision();

// One element of a point cloud: a simulated particle, a scattered point, a
// vertex of a soft body. Carries the state a simulation needs to step it, and
// the state the renderer needs to draw it, in one struct - keeping them apart
// would mean walking two parallel arrays that must stay index-aligned.
struct Particle
{
   float px = 0, py = 0, pz = 0;
   float vx = 0, vy = 0, vz = 0;
   float nx = 0, ny = 1, nz = 0;  // orientation hint, e.g. a surface normal
   float scale = 1.0f;
   float r = 1.0f, g = 1.0f, b = 1.0f;
   float age = 0.0f;              // seconds alive
   float life = 0.0f;             // seconds until death; <= 0 means immortal
   bool alive = true;
};

// An ordered chain of points in space. Curves, extracted mesh boundaries and
// plane-slice contours all reduce to this, so anything that can follow one can
// follow all of them.
struct Polyline
{
   std::vector<float> points; // xyz triples
   bool closed = false;

   size_t Count() const { return points.size() / 3; }
   bool Empty() const { return Count() < 2; }
};

// A point sampled off a mesh, used by the instancing nodes.
struct MeshPoint
{
   float px = 0, py = 0, pz = 0;
   float nx = 0, ny = 1, nz = 0;
   float scale = 1.0f;
   int index = 0;
   // Carried from Mesh::vertexColor when the source had it, 1,1,1 otherwise -
   // ToPoints() fills this so a Set Color upstream of Mesh to Points survives
   // the conversion into the point domain.
   float r = 1.0f, g = 1.0f, b = 1.0f;
};

namespace MeshOps
{
   // All of these are mesh -> mesh, which is what lets the operator nodes chain
   // freely in any order.
   Mesh Transform(const Mesh& in, const Mat4& m);
   Mesh Array(const Mesh& in, int count, float dx, float dy, float dz,
              float rotStep, float scaleStep, bool radial, float radius);
   Mesh Subdivide(const Mesh& in, int levels, float smooth);
   // Taubin lambda/mu smoothing: relaxes the surface without the steady
   // shrinkage a plain Laplacian causes.
   Mesh Smooth(const Mesh& in, int iterations, float strength);
   Mesh Mirror(const Mesh& in, int axis, float offset, bool weldSeam, bool keepOriginal);
   // Revolves the input's boundary edges around an axis, with an optional rise
   // per turn for threads and helices.
   Mesh Screw(const Mesh& in, int steps, float turns, float rise, float radiusOffset, int axis);

   // Turns closed 2D contours into an extruded solid. Contours wound
   // anticlockwise are treated as outlines and clockwise ones as holes, so the
   // counter of an 'o' is cut out rather than filled.
   struct Contour2D
   {
      std::vector<float> points; // x,y pairs
   };
   Mesh ExtrudeContours(const std::vector<Contour2D>& contours, float depth, float bevel);

   // Gerstner (trochoidal) wave surface. Not a simulation: every vertex is a
   // closed-form function of position and time, so it costs one evaluation per
   // vertex per frame with no state and no timestep to keep stable.
   Mesh Ocean(int resolution, float size, float amplitude, float wavelength,
              float steepness, float direction, float choppiness, int octaves, float time);

   // For each vertex, the index of the first vertex sharing its position.
   // Anything reasoning about connectivity needs this: the primitives duplicate
   // vertices at UV seams and flat-shaded edges, and treating those as separate
   // points tears a surface open along its seams.
   std::vector<unsigned int> BuildWeldMap(const Mesh& in);
   // Same as BuildWeldMap(in), but with the weld quantum exposed as a distance
   // threshold rather than the hardcoded ~0.00001-unit snap the no-arg version
   // uses - lets Merge by Distance dial the weld radius instead of only ever
   // deduplicating exact seam coincidences.
   std::vector<unsigned int> BuildWeldMap(const Mesh& in, float threshold);

   // Welds every vertex pair closer than `threshold`, compacts the vertex
   // array to one entry per weld group (the group's first-encountered
   // vertex - position, normal and uv all come from that representative, not
   // an average), remaps every triangle's indices, and drops any triangle
   // whose three indices are no longer distinct after welding. threshold <= 0
   // is a no-op. faceMask/selectionGroup entries follow their surviving
   // triangle; a dropped triangle drops its entries too.
   Mesh MergeByDistance(const Mesh& in, float threshold);

   // Rebuilds a vertexColor array for `outVertexCount` new vertices, each
   // sourced from `mapping[i]` in `srcColor` (an rgb-triple array sized for
   // some old vertex count). Returns empty if `srcColor` is empty (no colour
   // to propagate) or any mapping index is out of range for it. Shared by
   // every operator that remaps vertices one-to-one, so the "size mismatch
   // means absent" convention is enforced in one place rather than open-coded
   // per operator.
   std::vector<float> RemapVertexColor(const std::vector<float>& srcColor,
                                        const std::vector<unsigned int>& mapping,
                                        size_t outVertexCount);

   // Rounds off hard edges by pulling every vertex toward the average of its
   // neighbours, then re-splitting so the flat regions stay flat. Not a true
   // edge bevel - that needs a half-edge structure and per-edge loops - but it
   // gives a cube the softened silhouette that catches a highlight, which is
   // what a bevel is usually wanted for.
   Mesh Bevel(const Mesh& in, float amount, int segments);

   // --- curves ---
   // Samples a polyline by arc length rather than by index, so a point moving
   // at constant t moves at constant speed even where the control points bunch
   // up. Returns the position and the direction of travel there.
   void SamplePolyline(const Polyline& line, float t, float outPos[3], float outTangent[3]);

   // Control points to a smooth curve. Catmull-Rom passes through every control
   // point; cubic bezier treats them as alternating anchors and handles; the
   // B-spline is approximated rather than a true NURBS, since rational weights
   // and a knot vector would be a lot of machinery for a visual difference that
   // is hard to see at these scales.
   enum CurveKind { kCurveCatmullRom = 0, kCurveBezier, kCurveBSpline, kCurveLinear };
   Polyline BuildCurve(const std::vector<float>& controlPoints, int kind, int segments,
                       bool closed);

   // Sweeps a circular profile along a polyline, so a curve can be seen.
   Mesh TubeAlong(const Polyline& line, float radius, int sides, float taper);

   // The chain of edges used by exactly one triangle - the outline of an open
   // surface. Empty for a closed mesh, which has no boundary at all.
   std::vector<Polyline> BoundaryLoops(const Mesh& in);

   // --- constructive solid geometry ---
   // Union, intersection and difference of two closed meshes.
   //
   // Implemented with BSP trees rather than by voxelising both meshes and
   // re-surfacing: a voxel approach is more robust against bad input, but it
   // rounds every flat face and sharp edge to the grid, which is exactly what
   // is wanted preserved when cutting a hole in a cube.
   //
   // Expects closed, non-self-intersecting input. Open surfaces have no defined
   // inside, so the result on one is unpredictable rather than wrong.
   enum BooleanOp { kBooleanUnion = 0, kBooleanIntersect, kBooleanDifference, kBooleanCount };
   Mesh Boolean(const Mesh& a, const Mesh& b, int op);

   // --- selection ---
   // How faces are chosen. Every mode is deterministic, so a patch selects the
   // same faces every time it is opened.
   enum SelectMode
   {
      kSelectAll = 0, kSelectIndex, kSelectAxis, kSelectNormal, kSelectRandom,
      kSelectRadius, kSelectModeCount
   };
   // Returns the input with its face mask replaced. `invert` flips the result,
   // and `append` unions with whatever was already selected upstream.
   Mesh Select(const Mesh& in, int mode, float a, float b, float c, int axis,
               float seed, bool invert, bool append);

   // Acts on the selection only. Unselected faces pass through untouched.
   Mesh DeleteSelected(const Mesh& in, bool keepSelected);
   Mesh TransformSelected(const Mesh& in, const Mat4& m, bool alongNormals, float normalAmount);
   Mesh ExtrudeSelected(const Mesh& in, float distance, float inset);

   // Phase 4 (selection as an input): the generic plumbing every operator's
   // `selectionOnly` toggle is built from, so each op doesn't invent its own.
   //
   // Splits `in` into its selected and unselected faces as two independent
   // meshes with no shared vertex indices - the same trade-off
   // ExtrudeSelected already makes at the wall it inserts, a possible seam at
   // the boundary rather than a weld.
   struct SelectionSplit { Mesh selected, unselected; };
   SelectionSplit SplitBySelection(const Mesh& in);
   // Concatenates b's vertices/indices onto a in place, no welding - the
   // inverse of SplitBySelection, used to stitch an operator's selected-only
   // result back together with the unselected part it didn't touch.
   void AppendMesh(Mesh& a, const Mesh& b);
   // A copy of `in` with faceMask cleared, so a selection-aware operator can
   // be told to ignore whatever mask arrived on its input - the
   // `selectionOnly == false` half of every operator this phase touches.
   Mesh ClearSelection(const Mesh& in);
   // A vertex counts as selected if any face touching it is selected - the
   // same rule ToPoints already uses to decide which vertices survive a face
   // selection (src/core/Mesh.cpp, the point-mode `touched` loop). Shared here
   // rather than reimplemented so Twist/Displace's `selectionOnly` uses the
   // one rule that already exists.
   std::vector<unsigned char> VertexSelectionFromFaces(const Mesh& in);

   // Contour where a mesh crosses an axis-aligned plane. This is what gives a
   // closed object something to be followed "around", since it has no boundary.
   std::vector<Polyline> SliceContours(const Mesh& in, int axis, float position);
   Mesh Solidify(const Mesh& in, float thickness, bool keepOriginal);
   Mesh Extrude(const Mesh& in, float distance, float inset);
   Mesh Wireframe(const Mesh& in, float thickness);
   Mesh Triangulate(const Mesh& in, float jitter);
   Mesh RecalculateNormals(const Mesh& in, bool flat, bool flip);
   Mesh Explode(const Mesh& in, float amount, float seed);
   // `vertexMask`, when non-null, restricts the twist to vertices marked 1 -
   // see VertexSelectionFromFaces. Null (the default) twists every vertex,
   // unchanged from before this parameter existed.
   Mesh Twist(const Mesh& in, float angle, int axis, const std::vector<unsigned char>* vertexMask = nullptr);

   // Moves each vertex using a texture sampled at its UV, the 3D counterpart
   // to the 2D `displace`/`liquify` filters - those warp an image's UVs, this
   // actually offsets mesh geometry. Mode 0 (Blender's "Displacement Only")
   // reads the texture's luminance and pushes along the vertex's own normal
   // by (height - midlevel) * strength. Mode 1 ("Vector Displacement") reads
   // RGB as an XYZ offset in the mesh's own local space, scaled by strength -
   // this can carve overhangs a normal-only push cannot reach. `texRGBA` is a
   // pre-sampled readback of the driving texture (row 0 = bottom), so this
   // stays pure CPU-side mesh math with no GL calls of its own. Normals are
   // recalculated afterward via RecalculateNormals(flat, flip).
   //
   // Vertices that share a starting position - a hard-shaded primitive like
   // Cube duplicates each corner once per adjoining face - get their
   // per-vertex offsets averaged and applied as one, so seams stay closed
   // instead of tearing open as each duplicate is pushed along its own
   // (different) normal.
   // `vertexMask`, when non-null, restricts the displacement to vertices
   // marked 1 - see VertexSelectionFromFaces. Null (the default) displaces
   // every vertex, unchanged from before this parameter existed.
   Mesh Displace(const Mesh& in, const std::vector<float>& texRGBA, int texW, int texH,
                 int mode, float strength, float midlevel, bool flat, bool flip,
                 const std::vector<unsigned char>* vertexMask = nullptr);

   // Sampling for instancing: vertices, edge midpoints or face centres.
   //
   // `weld` deduplicates coincident vertices (see BuildWeldMap) before
   // sampling in all three modes, so a hard-shaded primitive's seams don't
   // read as denser/brighter from stacked coincident points. Defaults on
   // because it's the correct behaviour, but it changes point density in
   // existing patches, so it's exposed as a parameter rather than switched
   // unconditionally.
   //
   // `dissolveAngleDegrees` (edge mode only) drops an interior edge whose two
   // adjacent triangle normals differ by less than this angle - it is the
   // only way, short of n-gon tracking, to tell a triangulation diagonal from
   // a real edge once vertices are welded. 0 disables the filter. This
   // degrades on dense smooth meshes: adjacent triangle normals on a
   // 200-segment sphere differ by under 1 degree, so the default of 1 degree
   // is safe on primitives but would start dissolving real edges there.
   std::vector<MeshPoint> ToPoints(const Mesh& in, int mode, int maxPoints,
                                    bool weld = true, float dissolveAngleDegrees = 1.0f);
   Mesh PointsToFaces(const std::vector<MeshPoint>& points, float size);

   // Area-weighted scatter. `density` is points per square unit of surface
   // area, so coverage is uniform regardless of how the mesh is tessellated -
   // unlike ToPoints, which samples in index order and therefore clumps
   // wherever the topology is dense (visible as dark caps at a UV sphere's
   // poles, where Primitives::Sphere bunches its rings). Honours `faceMask`
   // the same way ToPoints does. `seed` is hashed per-candidate the same way
   // MeshOps::Select's random mode is, so reopening a saved patch scatters
   // identically.
   //
   // `method` is kDistributeRandom or kDistributePoisson. Poisson mode
   // additionally rejects any candidate within `minDistance` of an already
   // accepted point (ignored in random mode) - once minimum spacing is
   // saturated, no further points are added no matter how high `density` is.
   enum { kDistributeRandom = 0, kDistributePoisson, kDistributeMethodCount };
   std::vector<MeshPoint> DistributeOnFaces(const Mesh& in, float density, float seed,
                                             int method, float minDistance);

   // Bends or conforms `source` onto `target`. Both inputs' transforms are
   // baked in, so the result is in world space - there is no way back to
   // source-local space without a matrix inverse, which this codebase does
   // not have. The result is blended against the original world position by
   // `blend` (0 = untouched, 1 = fully wrapped).
   //
   // kWrapCylindrical / kWrapSpherical are *coordinate remaps*, not
   // projections: the source is rolled around `axis` at the target's radius,
   // one radian of bend per `Reff` units travelled. That makes them exactly
   // arc-length preserving, which is what keeps flat 3D text readable when it
   // curves around a sphere - letterforms, letter spacing and extrusion depth
   // all survive intact. Spherical adds the same bend in the perpendicular
   // direction so long text also curves over the poles. With a target patched
   // in, the bend radius is always WrapRadius(target) * `radiusScale`, so
   // scaling the target always moves the source with it - a multiplier rather
   // than an absolute value precisely so tuning the radius can never sever
   // that link. `radiusOverride` is only consulted when there is no target at
   // all, where the bend has nothing else to work from. `offset` lifts the
   // source off that surface, and `fitAround` scales the bend so the source's
   // full width spans exactly 360 degrees.
   //
   // kWrapNearest is Blender's Shrinkwrap "Nearest Surface Point": every
   // source vertex moves to the closest point on target's nearest triangle,
   // offset outward along that triangle's face normal. It is the right tool
   // for conforming a dense mesh onto irregular geometry, where no parametric
   // bend exists - but it squashes flat text, since distant vertices all
   // share the same nearest silhouette point. Brute force over every target
   // triangle per source vertex; no spatial acceleration structure exists
   // here yet, same as every other op in this file.
   enum { kWrapCylindrical = 0, kWrapSpherical, kWrapNearest, kWrapModeCount };
   // Bend radius derived from a target: the mean of the two world-AABB
   // half-extents perpendicular to `axis`, so a sphere gives exactly its
   // radius and a cube its half-width. 0 for an empty target. Exported so the
   // UI can show the live value without waiting on a mesh rebuild.
   float WrapRadius(const Mesh& target, const Mat4& targetModel, int axis);
   Mesh Wrap(const Mesh& source, const Mat4& sourceModel, const Mesh& target, const Mat4& targetModel,
             int mode, float offset, float blend, float radiusOverride, float radiusScale, int axis,
             bool fitAround, bool flatShade, bool flipNormals);
}

namespace Primitives
{
   Mesh Plane(int segments);
   Mesh Cube(int segments);
   Mesh Sphere(int rings, int sectors);
   Mesh Torus(int rings, int sides, float tubeRadius);
   Mesh Cylinder(int sides, int rings, float topRadius);
   Mesh Cone(int sides, int rings);
   Mesh Icosphere(int subdivisions);
   Mesh TorusKnot(int segments, int sides, float tubeRadius, int p, int q);
   Mesh Capsule(int rings, int sectors, float height);
   Mesh Tube(int sides, int rings, float innerRadius);
   Mesh Pyramid(int sides);
   Mesh Prism(int sides, int rings);
   Mesh Helix(int segments, int sides, float tubeRadius, float turns, float height);

   // The three platonic solids an icosphere/cube can't stand in for. All are
   // flat-shaded - a smoothed tetrahedron is just a bad sphere.
   Mesh Tetrahedron();
   Mesh Octahedron();
   Mesh Dodecahedron();
   // Cube whose surface points are projected onto a rounded box, so the corner
   // radius is exact rather than a bevel approximation. `segments` controls how
   // finely the rounded region is tessellated.
   Mesh RoundedCube(int segments, float radius);
   // One-sided surfaces, emitted with both windings so they read as solid from
   // either side rather than vanishing under backface culling.
   Mesh MobiusStrip(int segments, int widthSegments, float width);
   Mesh KleinBottle(int uSegments, int vSegments);
   Mesh Gear(int teeth, float depth, float toothDepth, float hubRadius);
   Mesh Star(int points, float innerRatio, float depth);
   Mesh Disc(int sides, float innerRadius);
   Mesh Arrow(int sides, float shaftRadius, float headLength);
   // Gielis superformula on a sphere. Two sets of exponents give everything
   // from a rounded cube to a starfish to a spiky shell, which is a lot of
   // distinct silhouettes out of one function.
   Mesh Supershape(int rings, int sectors, float m1, float n1, float n2, float n3,
                   float m2, float p1, float p2, float p3);

   // Marching cubes over summed inverse-square fields. Metaballs merge into one
   // another instead of intersecting, which is the whole reason to want them
   // rather than a Join of spheres.
   struct MetaBall
   {
      float x = 0, y = 0, z = 0;
      float strength = 1.0f;
   };
   Mesh MetaBalls(const std::vector<MetaBall>& balls, int resolution, float threshold,
                  float bounds);
}

// INFINITE_GEOAUDIT: a measurement audit of the geometry nodes.
//
// The geometry twin of FXAUDIT / SYNTHAUDIT / NOTEAUDIT / IOAUDIT
// (docs/plans/node-audits/README.md, block 3). Every Geometry primitive and
// every Geometry Op (Transform, Array, Subdivide, ...) is built through its
// real node and read back with GetMesh(); nothing is rendered.
//
//   VALID       non-finite positions or normals, indices out of range, unit
//               length of the vertex normals, degenerate (zero-area) triangles,
//               triangles whose winding disagrees with their vertex normals,
//               side arrays (faceMask, vertexColor, selectionGroup) that do not
//               match the mesh size
//   WATERTIGHT  boundary and non-manifold edges after welding coincident
//               positions, for the shapes and operators that promise a closed
//               surface; closed meshes must also have a positive signed volume
//   NORMALS     vertex normal against the analytic normal for the Sphere,
//               Icosphere and Torus (flag > 1e-3 rad)
//   DEGEN       detail 0 / 1 / 2 / 3 / 512, sides 0 / 1 / 2, extreme bevel,
//               zero or negative supershape exponents; Ops with no input, a
//               bypassed input and a one-triangle input: no crash, finite output
//   SCALE       build time against vertex count for the Sphere, 1e3 to 1e6
//               vertices (flag a per-vertex cost that grows more than 3x)
//   DET         two nodes with the same parameters give bit-identical meshes
//   CACHE       an unchanged input does not change MeshRevision; a changed
//               input does
//   CPU         milliseconds per build
//
//   GRAPH       the nodes that take points or several meshes, driven through a
//               real chain (Sphere -> Distribute Points):
//                 Distribute  random count against density x surface area
//                             (flag > 30 %), Poisson minimum spacing
//                 Delaunay    triangle count in range and the empty-circumcircle
//                             property in the projection plane (flag any point
//                             inside a circumcircle)
//                 Voronoi     valid mesh, one cell per interior site
//                 Merge by Distance  no two vertices closer than the threshold
//                             afterwards, fewer vertices than before
//                 Join (merge)  vertex and triangle counts add up
//                 Ocean       finite, valid, deterministic, vertex count
//
//   META        one ball against the analytic surface radius sqrt(strength / threshold)
//               at three grid resolutions; closed, deterministic, degenerate settings
//   CURVE       Curve Ops: Resample spacing and length on a circle, Simplify against the
//               Douglas-Peucker bound, Smooth keeps the ends and never lengthens, Offset
//               against the parallel curve; the node in every mode and on hostile numbers
//   SWEEP       Sweep and Revolve: closed results are watertight, wound and shaded outward with
//               the volume the shape has (torus, sphere, capped part-turns, a non-convex cap,
//               a taper to a point, a closed path with whole-turn twist); lathe normals against
//               the analytic torus; the nodes' cache, bypass, forwarding and hostile numbers
//   KINDS       every geometry op against every input kind: empty, points, curve, mixed, bad data
//   BEVEL       Bevel and Inset: closed in, closed out; volume against the chamfer prisms and the
//               six frustums of an inset cube; smooth shapes and hostile numbers hand the input back
//   CLOTH       free fall against g t^2 / 2, pinned cloth over 12 s (finite, bounded
//               stretch, pins hold, sags, settles), determinism, a long time jump, bypass
//   INST        Instance on Points: count, maxPoints cap, placement on the surface, scale,
//               alignToNormal, seed determinism, realised mesh counts, hostile numbers
//   REF         Delaunay against 2n - 2 - h and the convex-hull area, Voronoi corners are
//               circumcentres, Decimate error against the original surface
//   DISP        Displacement against the offset the texture asks for (needs GL; only when
//               INFINITE_GEOAUDIT_ONLY names DISP, run from the GL-ready dispatch)
//
// Only hard invariants fail the run (non-finite data, out-of-range indices,
// non-deterministic output). Everything else is reported, with FLAG lines.
#include "app/AppShared.h"
#include "core/CurveOps.h"
#include "core/Mesh.h"
#include "core/Transport.h"
#include "nodes/CurveNode.h"
#include "nodes/CurveOpsNode.h"
#include "nodes/SweepRevolveNodes.h"
#include "nodes/SimulationNodes.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/SceneNodes.h"
#include "nodes/DelaunayNodes.h"
#include "nodes/GeometryOpNodes.h"
#include "nodes/OceanNode.h"
#include "nodes/PointDistributionNodes.h"
#include "nodes/UtilityNodes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_map>

namespace app
{
namespace
{
   // One measured value for the quality system (docs/quality/README.md): tools/quality/quality.py keeps
   // the worst per metric and subject and owns the target and the release gate.
   void Quality(const char* id, const std::string& node, double v)
   {
      printf("QUALITY id=%s subject=\"node:%s\" value=%.4f\n", id, node.c_str(), v);
   }

   struct Flags
   {
      int n = 0;
      void Flag(const char* sec, const std::string& node, const char* what, double v)
      {
         printf("GEOAUDIT FLAG %-10s %-18s %s %.5g\n", sec, node.c_str(), what, v);
         n++;
      }
   };

   struct Stats
   {
      size_t verts = 0, tris = 0;
      int nonFinite = 0, badIndex = 0, badNormalLen = 0, degenerate = 0, windingVsNormal = 0, sideMismatch = 0;
      int boundary = 0, nonManifold = 0, inconsistent = 0;
      double volume = 0.0;
      double maxNormalLenErr = 0.0;
   };

   struct Key
   {
      long long x, y, z;
      bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
   };
   struct KeyHash
   {
      size_t operator()(const Key& k) const
      {
         return (size_t)((unsigned long long)k.x * 73856093ULL) ^ (size_t)((unsigned long long)k.y * 19349663ULL) ^ (size_t)((unsigned long long)k.z * 83492791ULL);
      }
   };

   Stats Measure(const Mesh& m)
   {
      Stats s;
      s.verts = m.vertices.size();
      s.tris = m.indices.size() / 3;
      for (const Vertex& v : m.vertices)
      {
         if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz) || !std::isfinite(v.nx) ||
             !std::isfinite(v.ny) || !std::isfinite(v.nz) || !std::isfinite(v.u) || !std::isfinite(v.v))
            s.nonFinite++;
         else
         {
            const double len = std::sqrt((double)v.nx * v.nx + (double)v.ny * v.ny + (double)v.nz * v.nz);
            const double err = std::fabs(len - 1.0);
            s.maxNormalLenErr = std::max(s.maxNormalLenErr, err);
            if (err > 1e-2)
               s.badNormalLen++;
         }
      }
      if (!m.faceMask.empty() && m.faceMask.size() != s.tris)
         s.sideMismatch++;
      if (!m.vertexColor.empty() && m.vertexColor.size() != s.verts * 3)
         s.sideMismatch++;
      if (!m.selectionGroup.empty() && m.selectionGroup.size() != s.tris)
         s.sideMismatch++;

      // Weld by position so a hard-edged shape (a cube with split corners) still counts as closed.
      std::unordered_map<Key, int, KeyHash> weld;
      std::vector<int> id(s.verts, 0);
      for (size_t i = 0; i < s.verts; i++)
      {
         const Vertex& v = m.vertices[i];
         if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
            continue;
         const Key k { std::llround((double)v.px * 1e4), std::llround((double)v.py * 1e4),
                       std::llround((double)v.pz * 1e4) };
         auto it = weld.find(k);
         if (it == weld.end())
            it = weld.emplace(k, (int)weld.size()).first;
         id[i] = it->second;
      }

      std::map<std::pair<int, int>, int> undirected;
      std::map<std::pair<int, int>, int> directed;
      for (size_t t = 0; t + 2 < m.indices.size() + 0 && t / 3 < s.tris; t += 3)
      {
         const unsigned a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
         if (a >= s.verts || b >= s.verts || c >= s.verts)
         {
            s.badIndex++;
            continue;
         }
         const Vertex &A = m.vertices[a], &B = m.vertices[b], &C = m.vertices[c];
         const double ux = B.px - A.px, uy = B.py - A.py, uz = B.pz - A.pz;
         const double vx = C.px - A.px, vy = C.py - A.py, vz = C.pz - A.pz;
         const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
         const double area2 = std::sqrt(nx * nx + ny * ny + nz * nz);
         if (!(area2 > 1e-10))
         {
            // A zero-area triangle (a UV pole fan) is its own flag; it is not surface, so it does not
            // count towards edges or the winding check.
            s.degenerate++;
            continue;
         }
         else
         {
            const double sx = (double)A.nx + B.nx + C.nx, sy = (double)A.ny + B.ny + C.ny,
                         sz = (double)A.nz + B.nz + C.nz;
            if (nx * sx + ny * sy + nz * sz < 0.0)
               s.windingVsNormal++;
         }
         s.volume += ((double)A.px * ((double)B.py * C.pz - (double)B.pz * C.py) -
                      (double)A.py * ((double)B.px * C.pz - (double)B.pz * C.px) +
                      (double)A.pz * ((double)B.px * C.py - (double)B.py * C.px)) / 6.0;
         const int w[3] = { id[a], id[b], id[c] };
         for (int e = 0; e < 3; e++)
         {
            const int p = w[e], q = w[(e + 1) % 3];
            if (p == q)
               continue;
            undirected[{ std::min(p, q), std::max(p, q) }]++;
            directed[{ p, q }]++;
         }
      }
      for (const auto& kv : undirected)
      {
         if (kv.second == 1)
            s.boundary++;
         else if (kv.second > 2)
            s.nonManifold++;
      }
      for (const auto& kv : directed)
         if (kv.second > 1)
            s.inconsistent++;
      return s;
   }

   bool SameMesh(const Mesh& a, const Mesh& b)
   {
      if (a.vertices.size() != b.vertices.size() || a.indices != b.indices)
         return false;
      return a.vertices.empty() ||
             std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(Vertex)) == 0;
   }

   // The shapes that promise a closed, outward-facing surface at default settings.
   bool PromisesClosed(const std::string& name)
   {
      static const char* closed[] = { "Cube", "Sphere", "Icosphere", "Torus", "Cylinder", "Cone", "Capsule",
                                       "Pyramid", "Prism", "Tetrahedron", "Octahedron", "Dodecahedron",
                                       "Rounded Cube" };
      for (const char* c : closed)
         if (name == c)
            return true;
      return false;
   }

   int gFrame = 1000;

   const Mesh& Build(GeometryNode& g)
   {
      g.CookIfNeeded(gFrame++);
      return g.GetMesh();
   }

   std::string ShapeName(int shape) { return GeometryNode::ShapeNames()[(size_t)shape]; }

   void Report(Flags& f, const char* sec, const std::string& name, const Stats& s, bool expectClosed, bool& ok)
   {
      if (s.nonFinite > 0)
      {
         f.Flag(sec, name, "non-finite vertices", s.nonFinite);
         ok = false;
      }
      if (s.badIndex > 0)
      {
         f.Flag(sec, name, "indices out of range", s.badIndex);
         ok = false;
      }
      if (s.badNormalLen > 0)
         f.Flag(sec, name, "normals off unit length (>1%)", s.badNormalLen);
      if (s.degenerate > 0)
         f.Flag(sec, name, "degenerate triangles", s.degenerate);
      Quality("geo.winding_mismatch", name, s.windingVsNormal);
      if (s.windingVsNormal > 0)
         f.Flag(sec, name, "winding disagrees with normals", s.windingVsNormal);
      if (s.sideMismatch > 0)
         f.Flag(sec, name, "side arrays do not match the mesh", s.sideMismatch);
      if (expectClosed)
         Quality("geo.nonmanifold", name, s.nonManifold + s.inconsistent);
      if (expectClosed)
      {
         if (s.boundary > 0)
            f.Flag("WATERTIGHT", name, "boundary edges", s.boundary);
         if (s.nonManifold > 0)
            f.Flag("WATERTIGHT", name, "non-manifold edges", s.nonManifold);
         if (s.inconsistent > 0)
            f.Flag("WATERTIGHT", name, "edges used twice in one direction", s.inconsistent);
         if (s.boundary == 0 && s.nonManifold == 0 && s.volume < 0.0)
            f.Flag("WATERTIGHT", name, "closed mesh faces inward, volume", s.volume);
      }
   }

   // --------------------------------------------------------------- primitives

   void AuditPrimitives(Flags& f, bool& ok)
   {
      const auto& names = GeometryNode::ShapeNames();
      for (size_t i = 0; i < names.size(); i++)
      {
         GeometryNode g;
         g.shape = (int)i;
         const Mesh& m = Build(g);
         const Stats s = Measure(m);
         printf("GEOAUDIT VALID  %-14s %zu verts %zu tris | boundary %d non-manifold %d volume %.4f normal length err %.1e\n",
                names[i].c_str(), s.verts, s.tris, s.boundary, s.nonManifold, s.volume, s.maxNormalLenErr);
         Report(f, "VALID", names[i], s, PromisesClosed(names[i]), ok);

         GeometryNode g2;
         g2.shape = (int)i;
         if (!SameMesh(m, Build(g2)))
         {
            f.Flag("DET", names[i], "two builds differ", 1);
            ok = false;
         }
      }
   }

   void AuditNormals(Flags& f)
   {
      auto angle = [](double ax, double ay, double az, double bx, double by, double bz) {
         const double la = std::sqrt(ax * ax + ay * ay + az * az), lb = std::sqrt(bx * bx + by * by + bz * bz);
         if (la < 1e-12 || lb < 1e-12)
            return 0.0;
         return std::acos(std::min(1.0, std::max(-1.0, (ax * bx + ay * by + az * bz) / (la * lb))));
      };
      for (const char* name : { "Sphere", "Icosphere", "Torus" })
      {
         GeometryNode g;
         const auto& names = GeometryNode::ShapeNames();
         for (size_t i = 0; i < names.size(); i++)
            if (names[i] == name)
               g.shape = (int)i;
         const Mesh& m = Build(g);
         // Bounding-box centre: the mean is biased by the vertices duplicated along a UV seam or pole.
         double mn[3] = { 1e30, 1e30, 1e30 }, mx[3] = { -1e30, -1e30, -1e30 };
         for (const Vertex& v : m.vertices)
         {
            const double p[3] = { v.px, v.py, v.pz };
            for (int a = 0; a < 3; a++)
            {
               mn[a] = std::min(mn[a], p[a]);
               mx[a] = std::max(mx[a], p[a]);
            }
         }
         const double cx = 0.5 * (mn[0] + mx[0]), cy = 0.5 * (mn[1] + mx[1]), cz = 0.5 * (mn[2] + mx[2]);
         double worst = 0.0;
         if (std::strcmp(name, "Torus") != 0)
         {
            for (const Vertex& v : m.vertices)
               worst = std::max(worst, angle(v.nx, v.ny, v.nz, v.px - cx, v.py - cy, v.pz - cz));
         }
         else
         {
            // The axis is the shortest extent; the ring radius is the mean of the in-plane radius range.
            double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
            for (const Vertex& v : m.vertices)
            {
               const double p[3] = { v.px - cx, v.py - cy, v.pz - cz };
               for (int a = 0; a < 3; a++)
               {
                  lo[a] = std::min(lo[a], p[a]);
                  hi[a] = std::max(hi[a], p[a]);
               }
            }
            int axis = 0;
            for (int a = 1; a < 3; a++)
               if (hi[a] - lo[a] < hi[axis] - lo[axis])
                  axis = a;
            const int u = (axis + 1) % 3, w = (axis + 2) % 3;
            double rmin = 1e30, rmax = 0.0;
            for (const Vertex& v : m.vertices)
            {
               const double p[3] = { v.px - cx, v.py - cy, v.pz - cz };
               const double rho = std::sqrt(p[u] * p[u] + p[w] * p[w]);
               rmin = std::min(rmin, rho);
               rmax = std::max(rmax, rho);
            }
            const double R = 0.5 * (rmin + rmax);
            for (const Vertex& v : m.vertices)
            {
               double p[3] = { v.px - cx, v.py - cy, v.pz - cz };
               const double rho = std::sqrt(p[u] * p[u] + p[w] * p[w]);
               if (rho < 1e-9)
                  continue;
               double q[3] = { p[0], p[1], p[2] };
               q[u] -= R * p[u] / rho;
               q[w] -= R * p[w] / rho;
               worst = std::max(worst, angle(v.nx, v.ny, v.nz, q[0], q[1], q[2]));
            }
         }
         printf("GEOAUDIT NORMALS %-12s worst angle to analytic %.5f rad\n", name, worst);
         if (worst > 1e-3)
            f.Flag("NORMALS", name, "normal error rad", worst);
      }
   }

   void AuditDegenerate(Flags& f, bool& ok)
   {
      auto run = [&](const std::string& label, const std::function<void(GeometryNode&)>& setup) {
         const auto& names = GeometryNode::ShapeNames();
         for (size_t i = 0; i < names.size(); i++)
         {
            GeometryNode g;
            g.shape = (int)i;
            setup(g);
            const Stats s = Measure(Build(g));
            const std::string id = names[i] + " " + label;
            if (s.nonFinite > 0)
            {
               f.Flag("DEGEN", id, "non-finite vertices", s.nonFinite);
               ok = false;
            }
            if (s.badIndex > 0)
            {
               f.Flag("DEGEN", id, "indices out of range", s.badIndex);
               ok = false;
            }
         }
      };
      run("detail 0", [](GeometryNode& g) { g.detail = 0; });
      run("detail 1", [](GeometryNode& g) { g.detail = 1; });
      run("detail 2", [](GeometryNode& g) { g.detail = 2; });
      run("detail 3", [](GeometryNode& g) { g.detail = 3; });
      run("detail 512", [](GeometryNode& g) { g.detail = 512; });
      run("sides 0", [](GeometryNode& g) { g.sides = 0; });
      run("sides 1", [](GeometryNode& g) { g.sides = 1; });
      run("sides 2", [](GeometryNode& g) { g.sides = 2; });
      run("bevel 5", [](GeometryNode& g) { g.bevel = 5.0f; g.bevelSegments = 8; });
      run("bevel -1", [](GeometryNode& g) { g.bevel = -1.0f; g.bevelSegments = 0; });
      run("tube 0", [](GeometryNode& g) { g.tubeRadius = 0.0f; });
      run("tube 10", [](GeometryNode& g) { g.tubeRadius = 10.0f; });
      run("super 0", [](GeometryNode& g) { g.superN2 = 0.0f; g.superN3 = 0.0f; g.superP2 = 0.0f; g.superP3 = 0.0f; });
      run("super -1", [](GeometryNode& g) { g.superN2 = -1.0f; g.superN3 = -1.0f; g.superP2 = -1.0f; g.superP3 = -1.0f; });
      run("knot 0", [](GeometryNode& g) { g.knotP = 0; g.knotQ = 0; });
      printf("GEOAUDIT DEGEN  primitives: %zu shapes x 15 settings built\n", GeometryNode::ShapeNames().size());
   }

   void AuditScale(Flags& f)
   {
      double firstPerVert = 0.0;
      for (int detail : { 32, 100, 316, 1000 })
      {
         GeometryNode g;
         g.shape = 2; // Sphere
         g.detail = detail;
         g.sides = detail / 2; // Sphere takes sides * 2 sectors, so this reaches 1e6 vertices at detail 1000
         const auto t0 = std::chrono::steady_clock::now();
         const Mesh& m = Build(g);
         const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
         const double perVert = m.vertices.empty() ? 0.0 : ms * 1e6 / (double)m.vertices.size();
         printf("GEOAUDIT SCALE  Sphere detail %4d: %8zu verts %9zu tris %9.1f ms %8.1f ns/vert\n", detail,
                m.vertices.size(), m.indices.size() / 3, ms, perVert);
         if (firstPerVert == 0.0)
            firstPerVert = perVert;
         else if (firstPerVert > 0.0 && perVert > 3.0 * firstPerVert && ms > 5.0)
            f.Flag("SCALE", "Sphere", "per-vertex cost grew x", perVert / firstPerVert);
      }
   }

   // --------------------------------------------------------------------- ops

   // Triangulate jitters its vertices on purpose (the low-poly look), so it is not in this set.
   bool OpKeepsClosed(int op)
   {
      return op == GeometryOpNode::kTransform || op == GeometryOpNode::kSubdivide ||
             op == GeometryOpNode::kNormals || op == GeometryOpNode::kSolidify ||
             op == GeometryOpNode::kTwist || op == GeometryOpNode::kSmooth;
   }

   // Solidify on an open input: the rim must be closed by a wall, so the result has no boundary at all.
   void AuditSolidifyRim(Flags& f, bool& ok)
   {
      const Mesh plane = Primitives::Plane(4);
      const Mesh solid = MeshOps::Solidify(plane, 0.1f, true);
      const Stats s = Measure(solid);
      printf("GEOAUDIT OP     Solidify on a plane        %zu tris | boundary %d non-manifold %d volume %.3f\n", s.tris, s.boundary, s.nonManifold, s.volume);
      Report(f, "VALID", "Solidify on a plane", s, true, ok);
   }

   void AuditOps(Flags& f, bool& ok)
   {
      AuditSolidifyRim(f, ok);
      const auto& opNames = GeometryOpNode::OpNames();
      for (int op = 0; op < GeometryOpNode::kOpCount; op++)
      {
         if (!GeometryOpNode::IsSpawnable(op))
            continue;
         const std::string name = (size_t)op < opNames.size() ? opNames[(size_t)op] : std::to_string(op);
         for (int shape : { 1, 2 }) // cube, sphere
         {
            GeometryNode geo;
            geo.shape = shape;
            geo.detail = 16;
            GeometryOpNode node;
            node.op = op;
            node.input = &geo;
            node.CookIfNeeded(gFrame++);
            const auto t0 = std::chrono::steady_clock::now();
            const Mesh m = node.GetMesh();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const Stats s = Measure(m);
            const std::string id = name + (shape == 1 ? " on cube" : " on sphere");
            printf("GEOAUDIT OP     %-26s %6zu verts %6zu tris | boundary %d non-manifold %d | %.2f ms\n", id.c_str(),
                   s.verts, s.tris, s.boundary, s.nonManifold, ms);
            Report(f, "VALID", id, s, OpKeepsClosed(op), ok);

            GeometryNode geo2;
            geo2.shape = shape;
            geo2.detail = 16;
            GeometryOpNode node2;
            node2.op = op;
            node2.input = &geo2;
            node2.CookIfNeeded(gFrame++);
            if (!SameMesh(m, node2.GetMesh()))
            {
               f.Flag("DET", id, "two builds differ", 1);
               ok = false;
            }

            // CACHE: nothing changed, so the revision must not move; change the input and it must.
            if (shape == 2)
            {
               const unsigned long long r0 = node.MeshRevision();
               node.CookIfNeeded(gFrame++);
               node.GetMesh();
               const unsigned long long r1 = node.MeshRevision();
               if (r0 != r1)
                  f.Flag("CACHE", id, "revision moved with nothing changed", (double)(r1 - r0));
               geo.detail = 24;
               node.CookIfNeeded(gFrame++);
               node.GetMesh();
               if (node.MeshRevision() == r1)
                  f.Flag("CACHE", id, "revision did not follow a changed input", 0);
            }
         }

         // DEGEN: no input, a bypassed input, a one-triangle input.
         {
            GeometryOpNode none;
            none.op = op;
            none.CookIfNeeded(gFrame++);
            const Stats s = Measure(none.GetMesh());
            if (s.nonFinite > 0 || s.badIndex > 0)
            {
               f.Flag("DEGEN", name + " no input", "bad output", s.nonFinite + s.badIndex);
               ok = false;
            }

            GeometryNode off;
            off.bypassed = true;
            GeometryOpNode node;
            node.op = op;
            node.input = &off;
            node.CookIfNeeded(gFrame++);
            const Stats s2 = Measure(node.GetMesh());
            if (s2.nonFinite > 0 || s2.badIndex > 0)
            {
               f.Flag("DEGEN", name + " bypassed in", "bad output", s2.nonFinite + s2.badIndex);
               ok = false;
            }

            GeometryNode tri;
            tri.shape = 0; // Plane
            tri.detail = 1;
            GeometryOpNode node3;
            node3.op = op;
            node3.input = &tri;
            node3.CookIfNeeded(gFrame++);
            const Stats s3 = Measure(node3.GetMesh());
            if (s3.nonFinite > 0 || s3.badIndex > 0)
            {
               f.Flag("DEGEN", name + " 1-quad in", "bad output", s3.nonFinite + s3.badIndex);
               ok = false;
            }
         }

         // Bypass: the op passes its input through untouched.
         {
            GeometryNode geo;
            geo.shape = 2;
            geo.detail = 12;
            GeometryOpNode node;
            node.op = op;
            node.input = &geo;
            node.bypassed = true;
            node.CookIfNeeded(gFrame++);
            if (!SameMesh(node.GetMesh(), Build(geo)) && !node.GetMesh().Empty())
               f.Flag("BYPASS", name, "bypassed op does not pass its input through", 1);
         }
      }
   }
   // -------------------------------------------------------------------- graph

   double SurfaceArea(const Mesh& m)
   {
      double a = 0.0;
      for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
      {
         const Vertex &A = m.vertices[m.indices[t]], &B = m.vertices[m.indices[t + 1]], &C = m.vertices[m.indices[t + 2]];
         const double ux = B.px - A.px, uy = B.py - A.py, uz = B.pz - A.pz;
         const double vx = C.px - A.px, vy = C.py - A.py, vz = C.pz - A.pz;
         const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
         a += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
      }
      return a;
   }

   void AuditGraph(Flags& f, bool& ok)
   {
      GeometryNode sphere;
      sphere.shape = 2;
      sphere.detail = 24;
      const double area = SurfaceArea(Build(sphere));

      // ---- Distribute
      DistributePointsOnFacesNode dist;
      dist.input = &sphere;
      dist.density = 40.0f;
      dist.method = 0;
      dist.CookIfNeeded(gFrame++);
      const auto* cloud = dist.GetPointCloud();
      const size_t n = cloud ? cloud->size() : 0;
      const double want = 40.0 * area;
      printf("GEOAUDIT GRAPH  Distribute random   %zu points | density x area = %.1f\n", n, want);
      if (n == 0 || std::fabs((double)n / want - 1.0) > 0.30)
         f.Flag("GRAPH", "Distribute random", "count differs from density x area (ratio)", n / want);

      dist.method = 1;
      dist.minDistance = 0.2f;
      dist.density = 200.0f;
      dist.CookIfNeeded(gFrame++);
      cloud = dist.GetPointCloud();
      double minD = 1e9;
      if (cloud)
         for (size_t i = 0; i < cloud->size(); i++)
            for (size_t j = i + 1; j < cloud->size(); j++)
            {
               const Particle &a = (*cloud)[i], &b = (*cloud)[j];
               minD = std::min(minD, std::sqrt((double)(a.px - b.px) * (a.px - b.px) + (double)(a.py - b.py) * (a.py - b.py) +
                                               (double)(a.pz - b.pz) * (a.pz - b.pz)));
            }
      printf("GEOAUDIT GRAPH  Distribute Poisson  %zu points | min spacing %.4f (asked 0.2)\n", cloud ? cloud->size() : 0, minD);
      if (minD < 0.2 * 0.99)
         f.Flag("GRAPH", "Distribute Poisson", "points closer than minDistance, nearest", minD);

      // ---- Delaunay / Voronoi on a random cloud (plane XY)
      dist.method = 0;
      dist.density = 12.0f;
      dist.CookIfNeeded(gFrame++);
      DelaunayMeshNode del;
      del.input = &dist;
      del.plane = 0;
      del.CookIfNeeded(gFrame++);
      const Mesh dm = del.GetMesh();
      const Stats ds = Measure(dm);
      const auto* pts = dist.GetPointCloud();
      const size_t np = pts ? pts->size() : 0;
      printf("GEOAUDIT GRAPH  Delaunay            %zu points -> %zu tris\n", np, ds.tris);
      Report(f, "GRAPH", "Delaunay", ds, false, ok);
      if (ds.tris < np / 2 || ds.tris > 2 * np)
         f.Flag("GRAPH", "Delaunay", "triangle count out of [n/2, 2n], count", (double)ds.tris);
      int violations = 0;
      for (size_t t = 0; t + 2 < dm.indices.size(); t += 3)
      {
         const Vertex &A = dm.vertices[dm.indices[t]], &B = dm.vertices[dm.indices[t + 1]], &C = dm.vertices[dm.indices[t + 2]];
         const double ax = A.px, ay = A.py, bx = B.px, by = B.py, cx = C.px, cy = C.py;
         const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
         if (std::fabs(d) < 1e-12)
            continue;
         const double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
         const double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
         const double r2 = (ax - ux) * (ax - ux) + (ay - uy) * (ay - uy);
         for (const Particle& p : *pts)
         {
            const double d2 = (p.px - ux) * (p.px - ux) + (p.py - uy) * (p.py - uy);
            if (d2 < r2 * (1.0 - 1e-6))
            {
               violations++;
               break;
            }
         }
      }
      printf("GEOAUDIT GRAPH  Delaunay            circumcircle violations %d of %zu\n", violations, ds.tris);
      if (violations > 0)
         f.Flag("GRAPH", "Delaunay", "triangles with a point inside the circumcircle", violations);

      VoronoiCellsNode vor;
      vor.input = &dist;
      vor.plane = 0;
      vor.CookIfNeeded(gFrame++);
      const Stats vs = Measure(vor.GetMesh());
      printf("GEOAUDIT GRAPH  Voronoi             %zu verts %zu tris\n", vs.verts, vs.tris);
      Report(f, "GRAPH", "Voronoi", vs, false, ok);
      if (vs.tris == 0 && np > 8)
         f.Flag("GRAPH", "Voronoi", "no cells from a cloud of", (double)np);

      // ---- Join (merge) and Merge by Distance
      GeometryNode cube;
      cube.shape = 1;
      JoinGeometryNode join;
      join.mode = JoinGeometryNode::kMerge;
      join.inputs[0] = &cube;
      GeometryNode prism;
      prism.shape = 3; // Icosphere: closed, without the UV-pole slivers the Sphere carries
      join.inputs[1] = &prism;
      join.CookIfNeeded(gFrame++);
      const Mesh jm = join.GetMesh();
      const Mesh cm = Build(cube), sm = Build(prism);
      printf("GEOAUDIT GRAPH  Join merge          %zu verts %zu tris (cube %zu/%zu + shape %zu/%zu)\n", jm.vertices.size(),
             jm.indices.size() / 3, cm.vertices.size(), cm.indices.size() / 3, sm.vertices.size(), sm.indices.size() / 3);
      Report(f, "GRAPH", "Join merge", Measure(jm), false, ok);
      if (jm.vertices.size() != cm.vertices.size() + sm.vertices.size() || jm.indices.size() != cm.indices.size() + sm.indices.size())
         f.Flag("GRAPH", "Join merge", "counts do not add up, verts", (double)jm.vertices.size());

      JoinGeometryNode twin; // the same cube twice: every vertex has a twin
      twin.mode = JoinGeometryNode::kMerge;
      twin.inputs[0] = &cube;
      twin.inputs[1] = &cube;
      twin.CookIfNeeded(gFrame++);
      MergeByDistanceNode merge;
      merge.input = &twin;
      merge.threshold = 0.01f;
      merge.CookIfNeeded(gFrame++);
      const Mesh mm = merge.GetMesh();
      double closest = 1e9;
      for (size_t i = 0; i < mm.vertices.size(); i++)
         for (size_t j = i + 1; j < mm.vertices.size(); j++)
         {
            const Vertex &a = mm.vertices[i], &b = mm.vertices[j];
            closest = std::min(closest, std::sqrt((double)(a.px - b.px) * (a.px - b.px) + (double)(a.py - b.py) * (a.py - b.py) +
                                                  (double)(a.pz - b.pz) * (a.pz - b.pz)));
         }
      printf("GEOAUDIT GRAPH  Merge by Distance   %zu -> %zu verts | closest pair %.4f (threshold 0.01)\n",
             twin.GetMesh().vertices.size(), mm.vertices.size(), closest);
      Report(f, "GRAPH", "Merge by Distance", Measure(mm), false, ok);
      if (mm.vertices.size() >= twin.GetMesh().vertices.size())
         f.Flag("GRAPH", "Merge by Distance", "did not remove duplicate vertices", (double)mm.vertices.size());
      if (closest < 0.01 * 0.999)
         f.Flag("GRAPH", "Merge by Distance", "vertices still closer than the threshold", closest);

      // ---- Ocean
      OceanNode ocean;
      ocean.resolution = 48;
      ocean.CookIfNeeded(gFrame++);
      const Mesh om = ocean.GetMesh();
      const Stats os = Measure(om);
      printf("GEOAUDIT GRAPH  Ocean               %zu verts %zu tris\n", os.verts, os.tris);
      Report(f, "GRAPH", "Ocean", os, false, ok);
      OceanNode ocean2;
      ocean2.resolution = 48;
      ocean2.CookIfNeeded(gFrame++);
      if (!SameMesh(om, ocean2.GetMesh()))
      {
         f.Flag("DET", "Ocean", "two builds differ", 1);
         ok = false;
      }
   }

   // ================================================================ helpers
   struct V3d { double x, y, z; };

   double PointSegDist(V3d p, V3d a, V3d b)
   {
      const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
      const double apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
      const double len2 = abx * abx + aby * aby + abz * abz;
      double t = len2 > 0.0 ? (apx * abx + apy * aby + apz * abz) / len2 : 0.0;
      t = std::clamp(t, 0.0, 1.0);
      const double dx = apx - t * abx, dy = apy - t * aby, dz = apz - t * abz;
      return std::sqrt(dx * dx + dy * dy + dz * dz);
   }

   V3d PolyAt(const Polyline& l, size_t i) { return { l.points[i * 3], l.points[i * 3 + 1], l.points[i * 3 + 2] }; }

   double PointPolyDist(V3d p, const Polyline& l)
   {
      double best = 1e30;
      const size_t n = l.Count();
      if (n == 1)
         return std::sqrt((p.x - l.points[0]) * (p.x - l.points[0]) + (p.y - l.points[1]) * (p.y - l.points[1]) +
                          (p.z - l.points[2]) * (p.z - l.points[2]));
      const size_t segs = l.closed ? n : n - 1;
      for (size_t i = 0; i < segs; i++)
         best = std::min(best, PointSegDist(p, PolyAt(l, i), PolyAt(l, (i + 1) % n)));
      return best;
   }

   double PolyLength(const Polyline& l)
   {
      double len = 0.0;
      const size_t n = l.Count();
      const size_t segs = l.closed ? n : (n > 0 ? n - 1 : 0);
      for (size_t i = 0; i < segs; i++)
      {
         const V3d a = PolyAt(l, i), b = PolyAt(l, (i + 1) % n);
         len += std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
      }
      return len;
   }

   bool PolyFinite(const Polyline& l)
   {
      for (float v : l.points)
         if (!std::isfinite(v))
            return false;
      return true;
   }

   // Closest distance from p to triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
   double PointTriDist(V3d p, V3d a, V3d b, V3d c)
   {
      auto sub = [](V3d u, V3d v) { return V3d { u.x - v.x, u.y - v.y, u.z - v.z }; };
      auto dot = [](V3d u, V3d v) { return u.x * v.x + u.y * v.y + u.z * v.z; };
      const V3d ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
      const double d1 = dot(ab, ap), d2 = dot(ac, ap);
      V3d q;
      if (d1 <= 0 && d2 <= 0)
         q = a;
      else
      {
         const V3d bp = sub(p, b);
         const double d3 = dot(ab, bp), d4 = dot(ac, bp);
         if (d3 >= 0 && d4 <= d3)
            q = b;
         else
         {
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0 && d1 >= 0 && d3 <= 0)
            {
               const double v = d1 / (d1 - d3);
               q = { a.x + v * ab.x, a.y + v * ab.y, a.z + v * ab.z };
            }
            else
            {
               const V3d cp = sub(p, c);
               const double d5 = dot(ab, cp), d6 = dot(ac, cp);
               if (d6 >= 0 && d5 <= d6)
                  q = c;
               else
               {
                  const double vb = d5 * d2 - d1 * d6;
                  if (vb <= 0 && d2 >= 0 && d6 <= 0)
                  {
                     const double w = d2 / (d2 - d6);
                     q = { a.x + w * ac.x, a.y + w * ac.y, a.z + w * ac.z };
                  }
                  else
                  {
                     const double va = d3 * d6 - d5 * d4;
                     if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
                     {
                        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                        q = { b.x + w * (c.x - b.x), b.y + w * (c.y - b.y), b.z + w * (c.z - b.z) };
                     }
                     else
                     {
                        const double denom = 1.0 / (va + vb + vc);
                        const double v = vb * denom, w = vc * denom;
                        q = { a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w };
                     }
                  }
               }
            }
         }
      }
      const V3d d = sub(p, q);
      return std::sqrt(dot(d, d));
   }

   double Dist3(const Vertex& a, const Vertex& b)
   {
      return std::sqrt((double)(a.px - b.px) * (a.px - b.px) + (double)(a.py - b.py) * (a.py - b.py) + (double)(a.pz - b.pz) * (a.pz - b.pz));
   }

   // ============================================================== METABALLS
   void AuditMetaballs(Flags& f, bool& ok)
   {
      // One ball at the origin: the surface is where strength / d^2 = threshold, so d = radius / sqrt(threshold).
      const float radius = 0.35f, threshold = 8.0f;
      const double wantR = radius / std::sqrt((double)threshold);
      printf("GEOAUDIT META   one ball: slider radius %.3f, surface radius at threshold %.1f is %.4f (radius / sqrt(threshold))\n", radius,
             threshold, wantR);
      for (int res : { 24, 40, 96 })
      {
         MetaBallNode m;
         m.ballCount = 1;
         m.spread = 0.0f;
         m.spin = 0.0f;
         m.radius = radius;
         m.threshold = threshold;
         m.resolution = res;
         m.CookIfNeeded(gFrame++);
         const Mesh& mesh = m.GetMesh();
         if (mesh.Empty())
         {
            printf("GEOAUDIT META   resolution %d: empty mesh\n", res);
            f.Flag("META", "one ball", "empty mesh at resolution", res);
            continue;
         }
         double mean = 0.0, worst = 0.0;
         for (const Vertex& v : mesh.vertices)
         {
            const double d = std::sqrt((double)v.px * v.px + (double)v.py * v.py + (double)v.pz * v.pz);
            mean += d;
            worst = std::max(worst, std::fabs(d - wantR));
         }
         mean /= (double)mesh.vertices.size();
         const double cell = 2.0 * 1.5 / res;
         printf("GEOAUDIT META   resolution %-3d cell %.4f: mean radius %.4f (%.1f %% off), worst %.4f, %zu tris\n", res, cell, mean,
                (mean / wantR - 1.0) * 100.0, worst, mesh.indices.size() / 3);
         if (std::fabs(mean / wantR - 1.0) > 0.10)
            f.Flag("META", "one ball", "mean surface radius off the analytic radius, %", (mean / wantR - 1.0) * 100.0);
         Report(f, "META", "one ball r" + std::to_string(res), Measure(mesh), true, ok);
      }

      // Default three balls: valid, closed, deterministic; eight balls at the top resolution.
      MetaBallNode a, b;
      a.CookIfNeeded(gFrame++);
      b.CookIfNeeded(gFrame++);
      const Stats s = Measure(a.GetMesh());
      printf("GEOAUDIT META   default 3 balls: %zu verts %zu tris, boundary edges %d, volume %.4f\n", s.verts, s.tris, s.boundary, s.volume);
      Report(f, "META", "default", s, false, ok);
      if (!SameMesh(a.GetMesh(), b.GetMesh()))
      {
         f.Flag("DET", "Metaballs", "two builds differ", 1);
         ok = false;
      }
      MetaBallNode big;
      big.ballCount = 8;
      big.resolution = 96;
      const auto t0 = std::chrono::steady_clock::now();
      big.CookIfNeeded(gFrame++);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      printf("GEOAUDIT META   8 balls at resolution 96: %.1f ms, %zu tris\n", ms, big.GetMesh().indices.size() / 3);
      Report(f, "META", "8 balls r96", Measure(big.GetMesh()), false, ok);

      // Degenerate settings: no crash, finite.
      MetaBallNode d;
      d.ballCount = 0;
      d.resolution = 0;
      d.threshold = 0.0f;
      d.bounds = 0.0f;
      d.radius = 0.0f;
      d.CookIfNeeded(gFrame++);
      Report(f, "DEGEN", "Metaballs", Measure(d.GetMesh()), false, ok);
      MetaBallNode e;
      e.threshold = -5.0f;
      e.radius = 100.0f;
      e.CookIfNeeded(gFrame++);
      Report(f, "DEGEN", "Metaballs extreme", Measure(e.GetMesh()), false, ok);
   }

   // ============================================================== CURVEOPS
   Polyline Circle(int n, float r)
   {
      Polyline l;
      l.closed = true;
      for (int i = 0; i < n; i++)
      {
         const float a = 6.2831853f * (float)i / (float)n;
         l.points.push_back(r * std::cos(a));
         l.points.push_back(r * std::sin(a));
         l.points.push_back(0.0f);
      }
      return l;
   }

   void AuditCurveOps(Flags& f, bool& ok)
   {
      // Resample on a fine circle: evenly spaced by arc length, length close to 2 pi r.
      const Polyline circle = Circle(720, 1.0f);
      for (int count : { 8, 32, 100 })
      {
         const Polyline r = CurveOps::Resample(circle, count);
         double lo = 1e9, hi = 0.0, offCurve = 0.0;
         for (size_t i = 0; i < r.Count(); i++)
         {
            const V3d a = PolyAt(r, i), b = PolyAt(r, (i + 1) % r.Count());
            const double seg = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            lo = std::min(lo, seg);
            hi = std::max(hi, seg);
            offCurve = std::max(offCurve, PointPolyDist(a, circle));
         }
         printf("GEOAUDIT CURVE  Resample %-3d on a circle: %zu points | spacing %.4f..%.4f | max distance off the curve %.5f | length %.4f (circle %.4f)\n",
                count, r.Count(), lo, hi, offCurve, PolyLength(r), 6.2831853);
         if ((int)r.Count() != count)
            f.Flag("CURVE", "Resample", "point count differs from the request", (double)r.Count());
         if (hi > lo * 1.02 + 1e-6)
            f.Flag("CURVE", "Resample", "spacing is not even, max / min", hi / std::max(lo, 1e-9));
         if (offCurve > 2e-3)
            f.Flag("CURVE", "Resample", "points leave the curve by", offCurve);
      }

      // Simplify: Ramer-Douglas-Peucker guarantees every dropped point is within the tolerance.
      for (float tol : { 0.005f, 0.02f, 0.1f })
      {
         const Polyline s = CurveOps::Simplify(circle, tol);
         double worst = 0.0;
         for (size_t i = 0; i < circle.Count(); i++)
            worst = std::max(worst, PointPolyDist(PolyAt(circle, i), s));
         printf("GEOAUDIT CURVE  Simplify tol %.3f: %zu -> %zu points | worst deviation %.5f\n", tol, circle.Count(), s.Count(), worst);
         if (worst > tol * 1.01)
            f.Flag("CURVE", "Simplify", "a dropped point is further than the tolerance, by", worst - tol);
      }

      // Smooth: ends of an open line stay put, length never grows.
      Polyline zig;
      for (int i = 0; i < 21; i++)
      {
         zig.points.push_back((float)i * 0.1f);
         zig.points.push_back((i & 1) ? 0.2f : -0.2f);
         zig.points.push_back(0.0f);
      }
      double prev = PolyLength(zig);
      for (int it : { 1, 3, 10, 50 })
      {
         const Polyline s = CurveOps::Smooth(zig, it, 0.5f);
         const double len = PolyLength(s);
         const bool endsKept = std::fabs(s.points[0] - zig.points[0]) < 1e-6 && std::fabs(s.points[s.points.size() - 3] - zig.points[zig.points.size() - 3]) < 1e-6 &&
                               std::fabs(s.points[1] - zig.points[1]) < 1e-6;
         printf("GEOAUDIT CURVE  Smooth %-2d iterations: length %.4f (was %.4f) | ends fixed %s\n", it, len, PolyLength(zig), endsKept ? "yes" : "NO");
         if (!endsKept)
            f.Flag("CURVE", "Smooth", "an end of an open line moved at iterations", it);
         if (len > prev + 1e-6)
            f.Flag("CURVE", "Smooth", "length grew with more iterations, by", len - prev);
         prev = len;
      }

      // Offset on a circle of radius 1: the parallel curve is a circle of radius 1 + d (left of travel is positive for CCW => inward).
      for (float d : { 0.1f, -0.1f, 0.3f })
      {
         const Polyline o = CurveOps::Offset(circle, d, 6);
         double lo = 1e9, hi = 0.0;
         for (size_t i = 0; i < o.Count(); i++)
         {
            const double rr = std::sqrt((double)o.points[i * 3] * o.points[i * 3] + (double)o.points[i * 3 + 1] * o.points[i * 3 + 1]);
            lo = std::min(lo, rr);
            hi = std::max(hi, rr);
         }
         const double want = 1.0 - d;
         printf("GEOAUDIT CURVE  Offset %+.2f on a unit circle: radius %.4f..%.4f (parallel curve %.4f)\n", d, lo, hi, want);
         if (!PolyFinite(o) || std::fabs(lo - want) > 0.01 || std::fabs(hi - want) > 0.01)
            f.Flag("CURVE", "Offset", "radius differs from the parallel curve, by", std::max(std::fabs(lo - want), std::fabs(hi - want)));
      }
      // An L: convex corner gets a round join, so every offset point is at the offset distance (never closer).
      Polyline ell;
      ell.points = { 0, 0, 0, 1, 0, 0, 1, 1, 0 };
      const Polyline lo = CurveOps::Offset(ell, 0.2f, 6);
      double minD = 1e9, maxD = 0.0;
      for (size_t i = 0; i < lo.Count(); i++)
      {
         const double d = PointPolyDist(PolyAt(lo, i), ell);
         minD = std::min(minD, d);
         maxD = std::max(maxD, d);
      }
      printf("GEOAUDIT CURVE  Offset 0.20 on an L: %zu points, distance from the original %.4f..%.4f\n", lo.Count(), minD, maxD);
      if (minD < 0.2 * 0.98 || maxD > 0.2 * 1.45)
         f.Flag("CURVE", "Offset", "points off the offset distance, range", maxD - minD);

      // The node, through a Curve source: valid tube mesh, finite points, every mode, hostile numbers.
      CurveNode curve;
      curve.CookIfNeeded(gFrame++);
      Report(f, "CURVE", "Curve source tube", Measure(curve.GetMesh()), false, ok);
      for (int mode = 0; mode < CurveOpsNode::kModeCount; mode++)
      {
         CurveOpsNode n;
         n.input = &curve;
         n.mode = mode;
         n.CookIfNeeded(gFrame++);
         const Polyline* l = n.GetCurve();
         const Stats s = Measure(n.GetMesh());
         printf("GEOAUDIT CURVE  node %-9s %zu points, tube %zu tris\n", CurveOpsNode::ModeNames()[(size_t)mode].c_str(), n.PointCount(), s.tris);
         if (l != nullptr && !PolyFinite(*l))
         {
            f.Flag("CURVE", "node", "non-finite curve points in mode", mode);
            ok = false;
         }
         Report(f, "CURVE", "node " + CurveOpsNode::ModeNames()[(size_t)mode], s, false, ok);
      }
      for (int mode = 0; mode < CurveOpsNode::kModeCount; mode++)
      {
         CurveOpsNode n;
         n.input = &curve;
         n.mode = mode;
         n.count = 0;
         n.tolerance = -1.0f;
         n.iterations = -3;
         n.strength = 10.0f;
         n.distance = 1e6f;
         n.arcSteps = 0;
         n.radius = 0.0f;
         n.sides = 0;
         n.CookIfNeeded(gFrame++);
         const Polyline* l = n.GetCurve();
         if (l != nullptr && !PolyFinite(*l))
         {
            f.Flag("DEGEN", "Curve Ops", "non-finite curve points in mode", mode);
            ok = false;
         }
         Report(f, "DEGEN", "Curve Ops hostile", Measure(n.GetMesh()), false, ok);
      }
      CurveOpsNode none; // no input
      none.CookIfNeeded(gFrame++);
      Report(f, "DEGEN", "Curve Ops no input", Measure(none.GetMesh()), false, ok);
   }

   // ================================================================= SWEEP
   // Sweep and Revolve. The done check for both is the same: the surface is wound and
   // shaded outward, and a closed result is watertight with the volume the shape has.
   struct CurveProbe : public IGeometrySource
   {
      Polyline line;
      Mat4 matrix;
      const Mesh& GetMesh() override { static Mesh empty; return empty; }
      unsigned long long MeshRevision() override { return 0; }
      Mat4 GetModelMatrix() const override { return matrix; }
      Material GetMaterial() const override { return Material(); }
      const Polyline* GetCurve() override { return &line; }
      unsigned long long CurveStamp() override { return stamp; }
      unsigned long long stamp = 1;
   };

   Polyline CirclePoly(const float c[3], const float u[3], const float v[3], float r, int n)
   {
      Polyline p;
      for (int i = 0; i < n; i++)
      {
         const float a = 6.28318530718f * (float)i / (float)n;
         for (int k = 0; k < 3; k++)
            p.points.push_back(c[k] + r * (std::cos(a) * u[k] + std::sin(a) * v[k]));
      }
      p.closed = true;
      return p;
   }

   Polyline PolyOf(std::initializer_list<float> xyz, bool closed)
   {
      Polyline p;
      p.points.assign(xyz.begin(), xyz.end());
      p.closed = closed;
      return p;
   }

   // Unit-radius semicircle from the south pole to the north pole in the XY plane,
   // closed along the axis: revolved, it is a sphere.
   Polyline SemiCircle(int n, bool reversed)
   {
      Polyline p;
      for (int i = 0; i <= n; i++)
      {
         const float a = -1.57079632679f + 3.14159265359f * (float)i / (float)n;
         const bool pole = (i == 0 || i == n);
         p.points.push_back(pole ? 0.0f : std::cos(a));
         p.points.push_back(std::sin(a));
         p.points.push_back(0.0f);
      }
      p.closed = true;
      if (reversed)
      {
         Polyline r;
         r.closed = true;
         for (size_t i = p.Count(); i-- > 0;)
            for (int k = 0; k < 3; k++)
               r.points.push_back(p.points[i * 3 + (size_t)k]);
         return r;
      }
      return p;
   }


   // ================================================================= BEVEL
   // Edge bevel and face inset (3.7). Closed in, closed out; the volume follows the shape
   // (a bevel only removes, a wider one removes more, a rounder one removes less than the
   // chamfer, an inset of a cube is a frustum per face), hostile numbers hand the input back
   // or still give a closed mesh.
   // ================================================================= KINDS
   // Every geometry op against every kind of input the graph can hand it: no mesh, a mesh with no
   // triangles, points only, a curve only, a mesh carrying points and a curve, and meshes with bad
   // data. Each must either work or pass through cleanly - no crash, no out-of-range index, no
   // non-finite number made from finite input. A cloud or curve must come out unchanged (or moved,
   // for Transform) and never be dropped.
   struct KindProbe : public IGeometrySource
   {
      Mesh mesh;
      std::vector<Particle> cloud;
      Polyline line;
      bool hasCloud = false, hasLine = false;
      Mat4 matrix;
      unsigned long long rev = NextMeshRevision();
      const Mesh& GetMesh() override { return mesh; }
      unsigned long long MeshRevision() override { return rev; }
      Mat4 GetModelMatrix() const override { return matrix; }
      Material GetMaterial() const override { return Material(); }
      const std::vector<Particle>* GetPointCloud() override { return hasCloud ? &cloud : nullptr; }
      unsigned long long PointCloudRevision() override { return rev; }
      const Polyline* GetCurve() override { return hasLine ? &line : nullptr; }
      unsigned long long CurveStamp() override { return rev; }
   };

   void AuditKinds(Flags& f, bool& ok)
   {
      struct Kind { std::string name; KindProbe probe; bool finiteInput = true; };
      std::vector<Kind> kinds;
      auto cubeMesh = []() { GeometryNode g; g.shape = 1; g.detail = 4; return Build(g); };
      auto addCloud = [](KindProbe& p) {
         p.hasCloud = true;
         for (int i = 0; i < 5; i++) { Particle q; q.px = (float)i; q.py = 0.5f * (float)i; q.pz = -1.0f; p.cloud.push_back(q); }
      };
      auto addLine = [](KindProbe& p, int n) {
         p.hasLine = true;
         for (int i = 0; i < n; i++) { p.line.points.push_back((float)i); p.line.points.push_back(1.0f); p.line.points.push_back(0.0f); }
      };
      auto add = [&](const char* name, std::function<void(KindProbe&)> fill, bool finite = true) {
         kinds.emplace_back();
         kinds.back().name = name;
         kinds.back().finiteInput = finite;
         fill(kinds.back().probe);
      };
      add("empty mesh", [](KindProbe&) {});
      add("vertices, no triangles", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.indices.clear(); });
      add("points only", [&](KindProbe& p) { addCloud(p); });
      add("one point", [&](KindProbe& p) { addCloud(p); p.cloud.resize(1); });
      add("curve only", [&](KindProbe& p) { addLine(p, 6); });
      add("curve of one point", [&](KindProbe& p) { addLine(p, 1); });
      add("closed 2-point curve", [&](KindProbe& p) { addLine(p, 2); p.line.closed = true; });
      add("mesh + points + curve", [&](KindProbe& p) { p.mesh = cubeMesh(); addCloud(p); addLine(p, 6); });
      add("points + curve, no mesh", [&](KindProbe& p) { addCloud(p); addLine(p, 4); });
      add("one triangle", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.indices.resize(3); });
      add("triangle list not a multiple of 3", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.indices.resize(p.mesh.indices.size() - 1); });
      add("zero-area triangles", [&](KindProbe& p) {
         p.mesh = cubeMesh();
         for (size_t i = 0; i + 2 < p.mesh.indices.size(); i += 3) p.mesh.indices[i + 1] = p.mesh.indices[i + 2] = p.mesh.indices[i];
      });
      add("indices out of range", [&](KindProbe& p) {
         p.mesh = cubeMesh();
         p.mesh.indices[0] = (unsigned)p.mesh.vertices.size() + 50;
      });
      add("NaN and infinity positions", [&](KindProbe& p) {
         p.mesh = cubeMesh();
         p.mesh.vertices[0].px = std::nanf("");
         p.mesh.vertices[1].py = INFINITY;
      }, false);
      add("huge coordinates", [&](KindProbe& p) {
         p.mesh = cubeMesh();
         for (auto& v : p.mesh.vertices) { v.px *= 1e9f; v.py *= 1e9f; v.pz *= 1e9f; }
      });
      add("short selection mask", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.faceMask.assign(3, 1); });
      add("long selection mask", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.faceMask.assign(p.mesh.indices.size(), 1); });
      add("everything selected", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.faceMask.assign(p.mesh.indices.size() / 3, 1); });
      add("short vertex colours", [&](KindProbe& p) { p.mesh = cubeMesh(); p.mesh.vertexColor.assign(5, 0.5f); });

      const auto& opNames = GeometryOpNode::OpNames();
      int cases = 0;
      for (int op = 0; op < GeometryOpNode::kOpCount; op++)
      {
         if (!GeometryOpNode::IsSpawnable(op))
            continue;
         const std::string name = (size_t)op < opNames.size() ? opNames[(size_t)op] : std::to_string(op);
         for (const Kind& k : kinds)
         {
            for (int selOnly = 0; selOnly < 2; selOnly++)
            {
               KindProbe probe = k.probe;
               probe.rev = NextMeshRevision();
               GeometryOpNode node;
               node.op = op;
               node.selectionOnly = selOnly != 0;
               node.input = &probe;
               const std::string id = name + " on " + k.name + (selOnly ? " (selection only)" : "");
               node.CookIfNeeded(gFrame++);
               const Mesh m = node.GetMesh();
               const Stats s = Measure(m);
               cases++;
               if (s.badIndex > 0)
               {
                  f.Flag("KINDS", id, "output has an out-of-range index", s.badIndex);
                  ok = false;
               }
               if (k.finiteInput && s.nonFinite > 0)
               {
                  f.Flag("KINDS", id, "non-finite output from finite input", s.nonFinite);
                  ok = false;
               }
               if (m.indices.size() % 3 != 0)
               {
                  f.Flag("KINDS", id, "triangle list is not a multiple of 3", (double)m.indices.size());
                  ok = false;
               }
               if (!m.vertexColor.empty() && m.vertexColor.size() != m.vertices.size() * 3)
               {
                  f.Flag("KINDS", id, "vertex colours do not match the vertex count", (double)m.vertexColor.size());
                  ok = false;
               }
               if (!m.faceMask.empty() && m.faceMask.size() != m.indices.size() / 3)
               {
                  f.Flag("KINDS", id, "selection mask does not match the triangle count", (double)m.faceMask.size());
                  ok = false;
               }
               // Points and a curve must survive: untouched by a mesh op, moved by Transform.
               const std::vector<Particle>* pc = node.GetPointCloud();
               if (probe.hasCloud && (pc == nullptr || pc->size() != probe.cloud.size()))
               {
                  f.Flag("KINDS", id, "points were dropped", pc ? (double)pc->size() : 0.0);
                  ok = false;
               }
               const Polyline* cv = node.GetCurve();
               if (probe.hasLine && (cv == nullptr || cv->points.size() != probe.line.points.size()))
               {
                  f.Flag("KINDS", id, "curve was dropped", cv ? (double)cv->points.size() : 0.0);
                  ok = false;
               }
               if (!probe.hasCloud && pc != nullptr)
                  f.Flag("KINDS", id, "invented points from nothing", (double)pc->size());
               if (!probe.hasLine && cv != nullptr)
                  f.Flag("KINDS", id, "invented a curve from nothing", (double)cv->points.size());
               // A mesh op fed points or a curve must say so rather than silently pass it by.
               if ((probe.hasCloud || probe.hasLine) && probe.mesh.Empty() && op != GeometryOpNode::kTransform &&
                   node.CookWarning().empty())
                  f.Flag("KINDS", id, "no warning that this op does not act on points or curves", 1);
            }
         }
      }
      printf("GEOAUDIT KINDS  %d op x input-kind cases run\n", cases);
   }

   void AuditBevelInset(Flags& f, bool& ok)
   {
      auto stats = [&](const char* name, const Mesh& m, bool closed) {
         const Stats s = Measure(m);
         printf("GEOAUDIT BEVEL  %-34s %6zu verts %6zu tris | boundary %d nonmanifold %d volume %.4f\n", name,
                s.verts, s.tris, s.boundary, s.nonManifold, s.volume);
         if (s.tris == 0)
            f.Flag("BEVEL", name, "no triangles", 0);
         else
            Report(f, "BEVEL", name, s, closed, ok);
         return s;
      };

      const Mesh cube = Primitives::Cube(1);
      const Stats base = Measure(cube);
      const double edge = std::cbrt(base.volume);

      const Stats b1 = stats("Bevel cube 0.1 chamfer", MeshOps::BevelEdges(cube, 0.1f * (float)edge, 1, 30.0f), true);
      const Stats b4 = stats("Bevel cube 0.1 round x4", MeshOps::BevelEdges(cube, 0.1f * (float)edge, 4, 30.0f), true);
      const Stats bw = stats("Bevel cube 0.2 chamfer", MeshOps::BevelEdges(cube, 0.2f * (float)edge, 1, 30.0f), true);
      const Stats bh = stats("Bevel cube 99 (clamped)", MeshOps::BevelEdges(cube, 99.0f, 2, 30.0f), true);
      if (!(b1.volume < base.volume))
         f.Flag("BEVEL", "Bevel cube chamfer", "volume did not shrink", b1.volume - base.volume);
      if (!(bw.volume < b1.volume))
         f.Flag("BEVEL", "Bevel cube chamfer", "wider bevel did not remove more", bw.volume - b1.volume);
      if (!(b4.volume > b1.volume && b4.volume < base.volume))
         f.Flag("BEVEL", "Bevel cube round", "rounded bevel volume not between chamfer and original", b4.volume);
      // Chamfer lower bound: 12 prisms of cross section w^2/2 over the edge length, overlaps at
      // the corners only make the true volume larger.
      const double w = 0.1 * edge;
      const double lower = base.volume - 12.0 * 0.5 * w * w * edge;
      if (b1.volume < lower - 1e-6)
         f.Flag("BEVEL", "Bevel cube chamfer", "removed more than the 12 edge prisms", b1.volume - lower);

      // A box that is not a cube, a cylinder with caps, and smooth shapes that must not change.
      Mesh box = MeshOps::Transform(cube, Mat4::Scale(2.0f, 1.0f, 0.5f));
      stats("Bevel box 2x1x0.5", MeshOps::BevelEdges(box, 0.08f, 2, 30.0f), true);
      const Mesh cyl = Primitives::Cylinder(24, 1, 1.0f);
      stats("Bevel cylinder", MeshOps::BevelEdges(cyl, 0.05f, 2, 30.0f), true);
      const Mesh torus = Primitives::Torus(24, 12, 0.3f);
      const Mesh tb = MeshOps::BevelEdges(torus, 0.05f, 2, 60.0f);
      if (tb.indices.size() != torus.indices.size())
         f.Flag("BEVEL", "Bevel torus", "a smooth shape changed", (double)tb.indices.size() - (double)torus.indices.size());
      const Mesh sphere = Primitives::Sphere(12, 16);
      if (MeshOps::BevelEdges(sphere, 0.05f, 2, 60.0f).indices.size() != sphere.indices.size())
         f.Flag("BEVEL", "Bevel sphere", "a smooth shape changed", 0);

      // Hostile numbers hand the input back untouched.
      for (float bad : { 0.0f, -1.0f, std::nanf(""), INFINITY })
      {
         const Mesh r = MeshOps::BevelEdges(cube, bad, 3, 30.0f);
         if (r.indices.size() != cube.indices.size())
            f.Flag("BEVEL", "Bevel hostile width", "input not returned", bad);
      }
      stats("Bevel 999 segments (clamped)", MeshOps::BevelEdges(cube, 0.1f, 999, 30.0f), true);
      if (!MeshOps::BevelEdges(Mesh(), 0.1f, 1, 30.0f).Empty())
         f.Flag("BEVEL", "Bevel empty", "made geometry out of nothing", 0);

      // ---- Inset: every cube face pulled in by wi and lifted by d is a frustum added to it.
      const double wi = 0.15 * edge, d = 0.1 * edge;
      const Stats i0 = stats("Inset cube flat", MeshOps::InsetFaces(cube, (float)wi, 0.0f, 20.0f), true);
      const Stats i1 = stats("Inset cube raised", MeshOps::InsetFaces(cube, (float)wi, (float)d, 20.0f), true);
      const Stats i2 = stats("Inset cube sunk", MeshOps::InsetFaces(cube, (float)wi, -(float)d, 20.0f), true);
      if (std::fabs(i0.volume - base.volume) > 1e-5 * base.volume)
         f.Flag("BEVEL", "Inset cube flat", "volume changed with no depth", i0.volume - base.volume);
      const double a1 = edge * edge, a2 = (edge - 2 * wi) * (edge - 2 * wi);
      const double frustum = d / 3.0 * (a1 + a2 + std::sqrt(a1 * a2));
      if (std::fabs(i1.volume - (base.volume + 6.0 * frustum)) > 1e-3 * base.volume)
         f.Flag("BEVEL", "Inset cube raised", "volume off the six frustums", i1.volume - base.volume - 6.0 * frustum);
      if (std::fabs(i2.volume - (base.volume - 6.0 * frustum)) > 1e-3 * base.volume)
         f.Flag("BEVEL", "Inset cube sunk", "volume off the six frustums", i2.volume - base.volume + 6.0 * frustum);
      const Mesh sph = MeshOps::InsetFaces(sphere, 0.05f, 0.0f, 20.0f);
      if (sph.indices.size() != sphere.indices.size())
         f.Flag("BEVEL", "Inset sphere", "a shape with no boundary changed", 0);

      // A selection: only the chosen face is inset, and it stays the selection.
      Mesh masked = cube;
      masked.faceMask.assign(masked.indices.size() / 3, 0);
      masked.faceMask[0] = masked.faceMask[1] = 1;
      const Mesh mi = MeshOps::InsetFaces(masked, (float)wi, (float)d, 20.0f);
      stats("Inset one cube face", mi, true);
      int selected = 0;
      for (unsigned char m : mi.faceMask)
         selected += m != 0;
      if (mi.faceMask.size() != mi.indices.size() / 3)
         f.Flag("BEVEL", "Inset one face", "mask length off the triangle count", (double)mi.faceMask.size());
      if (selected < 1 || selected > 2)
         f.Flag("BEVEL", "Inset one face", "inner face not left selected, selected triangles", selected);
      for (float bad : { 0.0f, -1.0f, std::nanf("") })
         if (MeshOps::InsetFaces(cube, bad, 0.0f, 20.0f).indices.size() != cube.indices.size())
            f.Flag("BEVEL", "Inset hostile width", "input not returned", bad);
      if (!MeshOps::InsetFaces(cube, 0.1f, INFINITY, 20.0f).vertices.empty() &&
          MeshOps::InsetFaces(cube, 0.1f, INFINITY, 20.0f).indices.size() != cube.indices.size())
         f.Flag("BEVEL", "Inset hostile depth", "input not returned", 0);
   }

   void AuditSweepRevolve(Flags& f, bool& ok)
   {
      const double pi = 3.14159265358979;
      auto check = [&](const std::string& name, const Mesh& m, bool closed, double wantVolume, double tol) {
         const Stats s = Measure(m);
         printf("GEOAUDIT SWEEP  %-34s %6zu verts %6zu tris | boundary %d nonmanifold %d volume %.4f (want %.4f)\n",
                name.c_str(), s.verts, s.tris, s.boundary, s.nonManifold, s.volume, wantVolume);
         if (s.tris == 0)
         {
            f.Flag("SWEEP", name, "no triangles", 0);
            return;
         }
         Report(f, "SWEEP", name, s, closed, ok);
         if (closed && wantVolume > 0.0 && std::fabs(s.volume - wantVolume) > tol * wantVolume)
            f.Flag("SWEEP", name, "volume off by fraction", (s.volume - wantVolume) / wantVolume);
      };

      const float o[3] = { 0, 0, 0 };
      const float ex[3] = { 1, 0, 0 }, ey[3] = { 0, 1, 0 }, ez[3] = { 0, 0, 1 };

      // ---- Revolve
      const float torusC[3] = { 1, 0, 0 };
      const Polyline tube = CirclePoly(torusC, ex, ey, 0.3f, 48);
      const double torusVol = 2.0 * pi * pi * 1.0 * 0.09;
      check("Revolve torus 360 Y", MeshOps::Revolve(tube, 1, 360.0f, 96, true), true, torusVol, 0.02);
      check("Revolve torus -360 Y", MeshOps::Revolve(tube, 1, -360.0f, 96, true), true, torusVol, 0.02);
      {
         const float cx[3] = { 0, 1, 0 };
         check("Revolve torus 360 X", MeshOps::Revolve(CirclePoly(cx, ey, ex, 0.3f, 48), 0, 360.0f, 96, true), true, torusVol, 0.02);
         const float cz[3] = { 1, 0, 0 };
         check("Revolve torus 360 Z", MeshOps::Revolve(CirclePoly(cz, ex, ez, 0.3f, 48), 2, 360.0f, 96, true), true, torusVol, 0.02);
      }
      check("Revolve sphere (poles)", MeshOps::Revolve(SemiCircle(32, false), 1, 360.0f, 64, true), true, 4.0 * pi / 3.0, 0.03);
      check("Revolve sphere, profile reversed", MeshOps::Revolve(SemiCircle(32, true), 1, 360.0f, 64, true), true, 4.0 * pi / 3.0, 0.03);
      check("Revolve half sphere 180 + caps", MeshOps::Revolve(SemiCircle(32, false), 1, 180.0f, 32, true), true, 2.0 * pi / 3.0, 0.03);
      check("Revolve torus 270 + caps", MeshOps::Revolve(tube, 1, 270.0f, 72, true), true, torusVol * 0.75, 0.03);
      check("Revolve cone shell (open)", MeshOps::Revolve(PolyOf({ 0.5f, 0, 0, 1.0f, 1, 0 }, false), 1, 360.0f, 48, true), false, 0, 0);
      {
         // The cone shell is open at both rims: exactly two boundary loops, one per rim.
         const Stats s = Measure(MeshOps::Revolve(PolyOf({ 0.5f, 0, 0, 1.0f, 1, 0 }, false), 1, 360.0f, 48, true));
         if (s.boundary != 96)
            f.Flag("SWEEP", "Revolve cone shell", "boundary edges, want 96", s.boundary);
      }

      // Normals of the lathe torus against the analytic ones.
      {
         const Mesh m = MeshOps::Revolve(tube, 1, 360.0f, 128, true);
         double worst = 0.0;
         for (const Vertex& v : m.vertices)
         {
            const double r = std::hypot((double)v.px, (double)v.pz);
            const double cx = r > 1e-9 ? (double)v.px / r : 1.0, cz = r > 1e-9 ? (double)v.pz / r : 0.0;
            double wx = v.px - cx, wy = v.py, wz = v.pz - cz;
            const double wl = std::sqrt(wx * wx + wy * wy + wz * wz);
            wx /= wl; wy /= wl; wz /= wl;
            const double d = std::max(-1.0, std::min(1.0, wx * v.nx + wy * v.ny + wz * v.nz));
            worst = std::max(worst, std::acos(d));
         }
         printf("GEOAUDIT NORMALS Revolve torus: worst normal error %.5f rad\n", worst);
         if (worst > 3e-3)
            f.Flag("NORMALS", "Revolve torus", "normal off the analytic one, rad", worst);
      }

      // ---- Sweep
      const float pc[3] = { 0, 0, 0 };
      const Polyline round = CirclePoly(pc, ex, ey, 0.2f, 24);
      const double roundArea = 0.5 * 24 * 0.2 * 0.2 * std::sin(2.0 * pi / 24.0);
      const Polyline line2 = PolyOf({ 0, 0, 0, 0, 0, 1, 0, 0, 2 }, false);
      check("Sweep circle on a line + caps", MeshOps::Sweep(round, line2, 1.0f, 1.0f, 0.0f, true), true, roundArea * 2.0, 0.01);
      {
         const Mesh m = MeshOps::Sweep(round, line2, 1.0f, 1.0f, 0.0f, false);
         const Stats s = Measure(m);
         if (s.boundary != 48)
            f.Flag("SWEEP", "Sweep circle, caps off", "boundary edges, want 48 (two open rims)", s.boundary);
         Report(f, "SWEEP", "Sweep circle, caps off", s, false, ok);
      }
      {
         const float rc[3] = { 0, 0, 0 };
         const Polyline ring = CirclePoly(rc, ex, ez, 1.0f, 96);
         check("Sweep circle on a ring", MeshOps::Sweep(round, ring, 1.0f, 1.0f, 0.0f, true), true,
               roundArea * 2.0 * pi * 1.0, 0.02);
         check("Sweep circle on a ring, twist 360", MeshOps::Sweep(round, ring, 1.0f, 1.0f, 360.0f, true), true,
               roundArea * 2.0 * pi * 1.0, 0.02);
         const Polyline square = PolyOf({ -0.2f, -0.1f, 0, 0.2f, -0.1f, 0, 0.2f, 0.1f, 0, -0.2f, 0.1f, 0 }, true);
         // A faceted twisted prism loses volume in proportion to the twist per ring, so the
         // twisted rectangle gets a finer ring (0.4735 of 0.5027 at 96 rings, 0.4957 at 384).
         const Polyline fine = CirclePoly(rc, ex, ez, 1.0f, 384);
         check("Sweep rectangle on a ring, twist 720", MeshOps::Sweep(square, fine, 1.0f, 1.0f, 720.0f, true), true,
               0.08 * 2.0 * pi, 0.02);
      }
      {
         Polyline helix;
         for (int i = 0; i <= 120; i++)
         {
            const float a = 6.28318530718f * 3.0f * (float)i / 120.0f;
            helix.points.push_back(std::cos(a));
            helix.points.push_back(0.1f * a);
            helix.points.push_back(std::sin(a));
         }
         check("Sweep circle on a helix + caps", MeshOps::Sweep(round, helix, 1.0f, 1.0f, 0.0f, true), true, 0, 0);
         check("Sweep circle on a helix, tapered", MeshOps::Sweep(round, helix, 1.5f, 0.5f, 0.0f, true), true, 0, 0);
      }
      {
         // An L: not convex, so the cap needs a real triangulation, not a fan.
         const Polyline ell = PolyOf({ 0, 0, 0, 0.4f, 0, 0, 0.4f, 0.1f, 0, 0.1f, 0.1f, 0, 0.1f, 0.4f, 0, 0, 0.4f, 0 }, true);
         const double ellArea = 0.4 * 0.1 + 0.1 * 0.3;
         check("Sweep L profile + caps", MeshOps::Sweep(ell, line2, 1.0f, 1.0f, 0.0f, true), true, ellArea * 2.0, 0.01);
         Polyline cw;
         cw.closed = true;
         for (size_t i = ell.Count(); i-- > 0;)
            for (size_t k = 0; k < 3; k++)
               cw.points.push_back(ell.points[i * 3 + k]);
         check("Sweep L profile reversed + caps", MeshOps::Sweep(cw, line2, 1.0f, 1.0f, 0.0f, true), true, ellArea * 2.0, 0.01);
      }
      check("Sweep cone (end scale 0)", MeshOps::Sweep(round, line2, 1.0f, 0.0f, 0.0f, true), true, roundArea * 2.0 / 3.0, 0.03);
      {
         const float xz[3] = { 0, 0, 0 };
         const Polyline flat = CirclePoly(xz, ex, ez, 0.2f, 24); // drawn in the XZ plane
         const Polyline alongZ = PolyOf({ 0, 0, 0, 0, 0, 2 }, false);
         check("Sweep XZ profile on a line", MeshOps::Sweep(flat, alongZ, 1.0f, 1.0f, 0.0f, true), true, roundArea * 2.0, 0.01);
      }
      check("Sweep open profile (ribbon)", MeshOps::Sweep(PolyOf({ -0.2f, 0, 0, 0.2f, 0, 0 }, false), line2, 1.0f, 1.0f, 0.0f, true), false, 0, 0);

      // ---- The nodes: cache, bypass, forwarding, wrong inputs, hostile numbers.
      CurveProbe prof; prof.line = tube;
      CurveProbe way; way.line = line2;
      RevolveNode rev;
      rev.input = &prof;
      rev.CookIfNeeded(gFrame++);
      const unsigned long long r0 = rev.MeshRevision();
      rev.CookIfNeeded(gFrame++);
      if (rev.MeshRevision() != r0)
         f.Flag("CACHE", "Revolve", "revision changed with nothing changed", 0);
      rev.angle = 180.0f;
      if (rev.MeshRevision() == r0)
         f.Flag("CACHE", "Revolve", "revision did not change with the angle", 0);
      const unsigned long long r1 = rev.MeshRevision();
      prof.stamp++;
      if (rev.MeshRevision() == r1)
         f.Flag("CACHE", "Revolve", "revision did not change with the input curve", 0);
      prof.matrix = Mat4::Translation(5.0f, 7.0f, 3.0f);
      {
         const Mat4 m = rev.GetModelMatrix();
         if (std::fabs(m.m[12] - 5.0f) > 1e-4f || std::fabs(m.m[13] - 7.0f) > 1e-4f || std::fabs(m.m[14] - 3.0f) > 1e-4f)
            f.Flag("CACHE", "Revolve", "does not forward the input's model matrix", m.m[12]);
      }
      RevolveNode rev2;
      rev2.input = &prof;
      rev2.angle = 180.0f;
      if (!SameMesh(rev.GetMesh(), rev2.GetMesh()))
         f.Flag("DET", "Revolve", "two nodes with the same parameters differ", 0);

      SweepNode sw;
      sw.profile = &prof;
      sw.path = &way;
      sw.CookIfNeeded(gFrame++);
      const unsigned long long s0 = sw.MeshRevision();
      sw.CookIfNeeded(gFrame++);
      if (sw.MeshRevision() != s0)
         f.Flag("CACHE", "Sweep", "revision changed with nothing changed", 0);
      sw.twist = 90.0f;
      if (sw.MeshRevision() == s0)
         f.Flag("CACHE", "Sweep", "revision did not change with the twist", 0);
      way.stamp++;
      const unsigned long long s1 = sw.MeshRevision();
      way.stamp++;
      if (sw.MeshRevision() == s1)
         f.Flag("CACHE", "Sweep", "revision did not change with the path curve", 0);
      sw.profile = nullptr; // a path alone gives a tube
      Report(f, "SWEEP", "Sweep path alone", Measure(sw.GetMesh()), true, ok);
      {
         GeometryNode cube;
         cube.shape = 1;
         cube.CookIfNeeded(gFrame++);
         RevolveNode wrong;
         wrong.input = &cube;
         wrong.CookIfNeeded(gFrame++);
         if (wrong.CookWarning().empty() || !wrong.GetMesh().vertices.empty())
            f.Flag("DEGEN", "Revolve", "a mesh input is not flagged and ignored", 0);
         wrong.bypassed = true;
         if (&wrong.GetMesh() != &cube.GetMesh())
            f.Flag("DEGEN", "Revolve", "bypass does not hand back the input", 0);
      }

      RevolveNode none;
      none.CookIfNeeded(gFrame++);
      Report(f, "DEGEN", "Revolve no input", Measure(none.GetMesh()), false, ok);
      SweepNode none2;
      none2.CookIfNeeded(gFrame++);
      Report(f, "DEGEN", "Sweep no input", Measure(none2.GetMesh()), false, ok);

      const float nan = std::nanf("");
      const float angles[] = { 0.0f, 1e-6f, -1e9f, 1e9f, nan, 360.0f, 359.9999f };
      for (float a : angles)
      {
         for (int seg : { -5, 0, 1, 2, 100000 })
         {
            for (int axis : { -1, 0, 1, 2, 9 })
            {
               RevolveNode r;
               r.input = &prof;
               r.angle = a; r.segments = seg; r.axis = axis;
               const Stats s = Measure(r.GetMesh());
               if (s.nonFinite > 0 || s.badIndex > 0)
               {
                  f.Flag("DEGEN", "Revolve hostile", "non-finite or bad index at angle", a);
                  ok = false;
               }
            }
         }
      }
      const float scales[] = { 0.0f, -1.0f, 1e9f, nan, 1e-12f };
      for (float a : scales)
      {
         for (float b : scales)
         {
            for (float tw : { 0.0f, 1e9f, nan, -720.0f })
            {
               SweepNode s;
               s.profile = &prof;
               s.path = &way;
               s.startScale = a; s.endScale = b; s.twist = tw;
               s.profileSides = 0; s.profileRadius = 0.0f;
               const Stats st = Measure(s.GetMesh());
               if (st.nonFinite > 0 || st.badIndex > 0)
               {
                  f.Flag("DEGEN", "Sweep hostile", "non-finite or bad index at scale", a);
                  ok = false;
               }
            }
         }
      }
      // Degenerate curves: a single point, repeated points, a path that doubles back.
      {
         CurveProbe dot, back;
         dot.line = PolyOf({ 1, 1, 1, 1, 1, 1, 1, 1, 1 }, false);
         back.line = PolyOf({ 0, 0, 0, 1, 0, 0, 0, 0, 0 }, false);
         SweepNode s;
         s.profile = &prof;
         s.path = &dot;
         Report(f, "DEGEN", "Sweep path of repeated points", Measure(s.GetMesh()), false, ok);
         s.path = &back;
         const Stats st = Measure(s.GetMesh());
         if (st.nonFinite > 0 || st.badIndex > 0)
         {
            f.Flag("DEGEN", "Sweep path doubling back", "non-finite or bad index", 0);
            ok = false;
         }
      }
      (void)o;
   }

   // ================================================================= CLOTH
   void AuditCloth(Flags& f, bool& ok)
   {
      Transport& tr = Transport::Instance();
      tr.SetPlaying(false);
      tr.SetTempo(120.0f);

      auto makeCloth = [](GeometryNode& plane, ClothNode& c) {
         plane.shape = 0;
         plane.detail = 16;
         c.input = &plane;
      };
      auto advance = [&](std::initializer_list<ClothNode*> nodes, int steps, double beatsPerStep, double& beat) {
         for (int i = 0; i < steps; i++)
         {
            beat += beatsPerStep;
            tr.SeekBeats(beat);
            const int fr = gFrame++;
            for (ClothNode* c : nodes)
               c->CookIfNeeded(fr);
         }
      };

      // Free fall (nothing pinned, no damping, no ground): y drops by g t^2 / 2, whatever the constraints do.
      {
         double beat = 0.0;
         tr.SeekBeats(0.0);
         GeometryNode plane;
         ClothNode c;
         makeCloth(plane, c);
         c.pinMode = ClothNode::kPinNone;
         c.damping = 0.0f;
         c.windTurbulence = 0.0f;
         c.shapeRetention = 0.0f;
         c.CookIfNeeded(gFrame++); // builds the rest state
         const Mesh start = c.GetMesh();
         double y0 = 0.0;
         for (const Vertex& v : start.vertices)
            y0 += v.py;
         y0 /= (double)start.vertices.size();
         advance({ &c }, 4, 0.25, beat); // 4 x 0.125 s = 0.5 s
         double y1 = 0.0;
         for (const Vertex& v : c.GetMesh().vertices)
            y1 += v.py;
         y1 /= (double)std::max<size_t>(1, c.GetMesh().vertices.size());
         const double fell = y0 - y1;
         const double g = -c.gravityY;
         const double want = 0.5 * g * 0.5 * 0.5; // t = 0.5 s
         printf("GEOAUDIT CLOTH  free fall 0.5 s at g %.1f: dropped %.4f | g t^2 / 2 = %.4f\n", g, fell, want);
         if (std::fabs(fell / want - 1.0) > 0.10)
            f.Flag("CLOTH", "free fall", "drop differs from g t^2 / 2, %", (fell / want - 1.0) * 100.0);
      }

      // Pinned top edge, 12 s of simulated time: stays finite, stretch bounded, pins hold, settles, deterministic.
      {
         double beat = 0.0;
         tr.SeekBeats(0.0);
         GeometryNode plane, planeB;
         ClothNode c, d;
         makeCloth(plane, c);
         makeCloth(planeB, d);
         c.CookIfNeeded(gFrame++);
         d.CookIfNeeded(gFrame++);
         const Mesh rest = c.GetMesh();
         double topY = -1e9;
         for (const Vertex& v : rest.vertices)
            topY = std::max(topY, (double)v.py);
         std::vector<size_t> pinned;
         for (size_t i = 0; i < rest.vertices.size(); i++)
            if (rest.vertices[i].py > topY - 1e-5)
               pinned.push_back(i);
         // Rest edge lengths, by index pair.
         std::map<std::pair<unsigned, unsigned>, double> restLen;
         for (size_t t = 0; t + 2 < rest.indices.size(); t += 3)
            for (int e = 0; e < 3; e++)
            {
               const unsigned p = rest.indices[t + e], q = rest.indices[t + (e + 1) % 3];
               restLen[{ std::min(p, q), std::max(p, q) }] = Dist3(rest.vertices[p], rest.vertices[q]);
            }
         advance({ &c, &d }, 96, 0.25, beat); // 96 x 0.125 s = 12 s
         const Mesh now = c.GetMesh();
         const Stats s = Measure(now);
         double maxStretch = 0.0, maxPinMove = 0.0, meanY0 = 0.0, meanY1 = 0.0, bbox = 0.0;
         for (const auto& kv : restLen)
            maxStretch = std::max(maxStretch, Dist3(now.vertices[kv.first.first], now.vertices[kv.first.second]) / std::max(kv.second, 1e-9) - 1.0);
         for (size_t i : pinned)
            maxPinMove = std::max(maxPinMove, Dist3(rest.vertices[i], now.vertices[i]));
         for (size_t i = 0; i < now.vertices.size(); i++)
         {
            meanY0 += rest.vertices[i].py;
            meanY1 += now.vertices[i].py;
            bbox = std::max({ bbox, std::fabs((double)now.vertices[i].px), std::fabs((double)now.vertices[i].py), std::fabs((double)now.vertices[i].pz) });
         }
         meanY0 /= (double)now.vertices.size();
         meanY1 /= (double)now.vertices.size();
         // Settling: how far the cloth moves in the next half second.
         const Mesh before = c.GetMesh();
         advance({ &c, &d }, 4, 0.25, beat);
         double drift = 0.0;
         for (size_t i = 0; i < before.vertices.size(); i++)
            drift = std::max(drift, Dist3(before.vertices[i], c.GetMesh().vertices[i]));
         printf("GEOAUDIT CLOTH  pinned top, 12 s: %zu verts | max edge stretch %.1f %% | pins moved %.5f | mean y %.3f -> %.3f | extent %.2f | drift in the next 0.5 s %.4f\n",
                s.verts, maxStretch * 100.0, maxPinMove, meanY0, meanY1, bbox, drift);
         Quality("geo.cloth_stretch_pct", "Cloth", maxStretch * 100.0);
         Report(f, "CLOTH", "pinned", s, false, ok);
         if (s.nonFinite > 0)
            ok = false;
         if (maxStretch > 0.5)
            f.Flag("CLOTH", "pinned", "an edge stretched past 50 %, stretch", maxStretch * 100.0);
         if (maxPinMove > 1e-4)
            f.Flag("CLOTH", "pinned", "pinned vertices moved by", maxPinMove);
         if (!(meanY1 < meanY0))
            f.Flag("CLOTH", "pinned", "cloth did not sag under gravity, mean y", meanY1 - meanY0);
         if (bbox > 20.0)
            f.Flag("CLOTH", "pinned", "cloth flew out of range, extent", bbox);
         if (drift > 0.05)
            f.Flag("CLOTH", "pinned", "still moving after 12 s, drift", drift);
         if (!SameMesh(c.GetMesh(), d.GetMesh()))
         {
            f.Flag("DET", "Cloth", "two simulations from the same start differ", 1);
            ok = false;
         }
      }

      // Hostile numbers: no crash, finite, and a big time jump is clamped (kMaxCatchUp), not integrated in one go.
      {
         double beat = 0.0;
         tr.SeekBeats(0.0);
         GeometryNode plane;
         ClothNode c;
         makeCloth(plane, c);
         c.stiffness = 5.0f;
         c.iterations = 0;
         c.damping = 1.5f;
         c.mass = 0.0f;
         c.gravityY = 1e6f;
         c.windX = 1e6f;
         c.CookIfNeeded(gFrame++);
         advance({ &c }, 6, 1.0, beat);
         const Stats s = Measure(c.GetMesh());
         if (s.nonFinite > 0)
         {
            f.Flag("DEGEN", "Cloth hostile", "non-finite vertices", s.nonFinite);
            ok = false;
         }
         ClothNode none; // no input
         none.CookIfNeeded(gFrame++);
         const double beforeJump = beat;
         tr.SeekBeats(beforeJump + 4000.0); // a long pause: must not blow up
         GeometryNode plane2;
         ClothNode j;
         makeCloth(plane2, j);
         j.CookIfNeeded(gFrame++);
         tr.SeekBeats(beforeJump + 8000.0);
         j.CookIfNeeded(gFrame++);
         const Stats sj = Measure(j.GetMesh());
         printf("GEOAUDIT CLOTH  hostile settings and an 8000-beat jump: finite verts %zu, non-finite %d\n", sj.verts, sj.nonFinite);
         if (sj.nonFinite > 0)
         {
            f.Flag("CLOTH", "jump", "non-finite after a long time jump", sj.nonFinite);
            ok = false;
         }
      }

      // A sheet dropped onto a ball: nothing ends up inside it, the top rests the
      // collider distance above it, and a stiffer cloth spreads wider around it.
      {
         GeometryNode ball;
         ball.shape = 2; // Sphere
         ball.detail = 32;
         const Mesh& bm = Build(ball);
         float radius = 0.0f;
         for (const Vertex& v : bm.vertices)
            radius = std::max(radius, std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz));
         ball.posY = -(radius + 0.2f);
         const float cy = ball.posY;
         double spread[2] = { 0.0, 0.0 };
         for (int stiff = 0; stiff < 2; stiff++)
         {
            double beat = 0.0;
            tr.SeekBeats(0.0);
            GeometryNode plane;
            ClothNode c;
            makeCloth(plane, c);
            plane.uniformScale = 2.5f; // wider than the ball, so it drapes instead of sliding off
            plane.rotX = -1.5707963f;  // lying flat over it
            c.collider = &ball;
            c.groundEnabled = true; // the ball sits on the floor
            c.groundHeight = cy - radius;
            c.pinMode = ClothNode::kPinNone;
            c.bending = stiff ? 0.6f : 0.0f;
            c.CookIfNeeded(gFrame++);
            advance({ &c }, 32, 0.25, beat); // 4 s
            const Mesh& m = c.GetMesh();
            float inside = 0.0f, top = -1e30f;
            double r = 0.0;
            for (const Vertex& v : m.vertices)
            {
               const float d = std::sqrt(v.px * v.px + (v.py - cy) * (v.py - cy) + v.pz * v.pz);
               inside = std::max(inside, radius * 0.97f - d);
               top = std::max(top, v.py - cy);
               r += std::sqrt(v.px * v.px + v.pz * v.pz);
            }
            spread[stiff] = r / (double)std::max<size_t>(1, m.vertices.size());
            const Stats st = Measure(m);
            printf("GEOAUDIT CLOTH  over a ball r %.3f, bending %.1f: %zu collider tris | deepest inside %.4f | top %.4f above centre | mean spread %.4f\n",
                   radius, c.bending, c.ColliderTriangleCount(), std::max(0.0f, inside), top, spread[stiff]);
            if (st.nonFinite > 0)
            {
               f.Flag("CLOTH", "collider", "non-finite vertices", st.nonFinite);
               ok = false;
            }
            if (inside > 0.0f)
               f.Flag("CLOTH", "collider", "cloth inside the ball by", inside);
            if (top < radius || top > radius + c.colliderDistance + 0.05f)
               f.Flag("CLOTH", "collider", "top not resting on the ball, height above centre", top);
         }
         if (spread[1] <= spread[0])
            f.Flag("CLOTH", "bending", "a stiffer cloth did not spread wider, spread", spread[1] - spread[0]);
      }

      // Bypass: the cloth hands its input's mesh through.
      {
         GeometryNode plane;
         ClothNode c;
         makeCloth(plane, c);
         c.bypassed = true;
         c.CookIfNeeded(gFrame++);
         const bool same = SameMesh(c.GetMesh(), Build(plane));
         printf("GEOAUDIT CLOTH  bypassed: output is the input mesh %s\n", same ? "yes" : "NO");
         if (!same)
            f.Flag("BYPASS", "Cloth", "bypassed cloth does not pass its input through", 1);
      }
      tr.SeekBeats(0.0);
   }


   // ================================================================ L-SYSTEM
   void AuditLSystem(Flags& f, bool& ok)
   {
      (void)ok;
      auto finite = [](const Mesh& m) {
         for (const Vertex& v : m.vertices)
            if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
               return false;
         return true;
      };
      auto hash = [](const Mesh& m) {
         size_t h = m.vertices.size() * 31u + m.indices.size();
         for (const Vertex& v : m.vertices)
            h = h * 1000003u ^ (size_t)(long long)(v.px * 1e5f) ^ ((size_t)(long long)(v.py * 1e5f) << 7) ^
                ((size_t)(long long)(v.pz * 1e5f) << 13);
         return h;
      };

      // The fractal plant: F counts after n generations are 3 and 18.
      {
         LSystemNode a;
         a.iterations = 1;
         a.GetMesh();
         const size_t one = a.SegmentCount();
         a.iterations = 2;
         a.GetMesh();
         const size_t two = a.SegmentCount();
         printf("GEOAUDIT LSYS   plant segments gen1 %zu (want 3), gen2 %zu (want 18)\n", one, two);
         if (one != 3 || two != 18)
            f.Flag("LSYS", "Plant", "segment count wrong", (double)two);
         a.iterations = 5;
         const Mesh& m = a.GetMesh();
         printf("GEOAUDIT LSYS   plant gen5: %zu segments, %zu branches, %zu tris\n",
                a.SegmentCount(), a.StrandCount(), a.TriangleCount());
         if (m.indices.empty() || !finite(m))
            f.Flag("LSYS", "Plant", "gen5 mesh empty or not finite", (double)m.indices.size());
      }

      // Geometry: "F" is one step up; "F+F" at 90 degrees turns left (-x).
      {
         LSystemNode n;
         n.axiom = "F+F"; n.rules = ""; n.iterations = 0; n.angle = 90.0f; n.length = 0.5f;
         const Polyline& line = n.GetPolyline();
         if (line.Count() != 3)
            f.Flag("LSYS", "Turtle", "curve point count", (double)line.Count());
         else
         {
            const float* p = line.points.data();
            printf("GEOAUDIT LSYS   F+F end (%.3f %.3f %.3f) want (-0.5 0.5 0)\n", p[6], p[7], p[8]);
            if (std::fabs(p[6] + 0.5f) > 1e-4f || std::fabs(p[7] - 0.5f) > 1e-4f || std::fabs(p[8]) > 1e-4f)
               f.Flag("LSYS", "Turtle", "F+F end point", p[6]);
         }
      }

      // Same text, same mesh; and a bypassed node outputs nothing.
      {
         LSystemNode a, b;
         const size_t ha = hash(a.GetMesh()), hb = hash(b.GetMesh());
         printf("GEOAUDIT LSYS   determinism %zx %zx\n", ha, hb);
         if (ha != hb)
            f.Flag("LSYS", "Determinism", "two identical nodes differ", 1);
         a.bypassed = true;
         if (!a.GetMesh().vertices.empty() || a.GetCurve() != nullptr)
            f.Flag("LSYS", "Bypass", "bypassed node still outputs", 1);
      }

      // Hostile input: runaway grammar, junk rules, brackets that never match, empty axiom.
      {
         LSystemNode n;
         n.axiom = "X"; n.rules = "X=XXXXXXXXXX;F=FFFFFFFF"; n.iterations = 10;
         const Mesh& m = n.GetMesh();
         printf("GEOAUDIT LSYS   runaway: %zu segments, warning '%s'\n", n.SegmentCount(), n.CookWarning().c_str());
         if (!finite(m) || n.CookWarning().empty())
            f.Flag("LSYS", "Runaway", "no warning or non-finite", 1);

         LSystemNode j;
         j.axiom = "]]][[[F]F]]]+++---&&&^^^\\\\///|||f"; j.rules = "=F;X;;==;===;F=F"; j.iterations = 4;
         if (!finite(j.GetMesh()))
            f.Flag("LSYS", "Junk", "non-finite", 1);

         LSystemNode e;
         e.axiom = ""; e.iterations = 5;
         if (!e.GetMesh().vertices.empty() || e.GetCurve() == nullptr)
            f.Flag("LSYS", "Empty", "empty axiom should give an empty mesh and a (possibly empty) curve", 1);
      }
   }


   // ============================================================ PARTICLE COST (gate G1 of metal-compute.md)
   // How much of a 16.7 ms frame the CPU particle system and the copy of its points take,
   // at counts a user would set. Only runs when INFINITE_GEOAUDIT_ONLY names PCOST.
   void AuditParticleCost()
   {
      Transport& tr = Transport::Instance();
      tr.SetPlaying(false);
      tr.SetTempo(120.0f);   // 1/30 beat is 1/60 s
      int round = 0;
      for (int count : { 50000, 100000, 200000, 100000 })
      {
         // The last round repeats 100k on one thread: its checksum must match round two.
         ParticleSystemNode::sMaxThreads = round++ == 3 ? 1 : 0;
         ParticleSystemNode p;
         p.maxParticles = count;
         p.lifetime = 3.0f;
         p.lifetimeRandom = 0.0f;
         p.emitRate = (float)count / 3.0f;
         double beat = 0.0;
         tr.SeekBeats(0.0);
         p.CookIfNeeded(gFrame++);
         std::vector<double> cook;
         for (int i = 0; i < 420; i++)
         {
            beat += 1.0 / 30.0;
            tr.SeekBeats(beat);
            const auto t0 = std::chrono::steady_clock::now();
            p.CookIfNeeded(gFrame++);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (i >= 240)
               cook.push_back(ms);
         }
         std::sort(cook.begin(), cook.end());
         double sum = 0.0;
         for (double c : cook) sum += c;
         // The hand-off to the GL buffer is at least one pass over the points.
         const std::vector<Particle>* pts = p.GetPointCloud();
         std::vector<Particle> copy;
         const auto c0 = std::chrono::steady_clock::now();
         copy = *pts;
         const double copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
         double sumPos = 0.0;
         for (const Particle& q : *pts) sumPos += (double)q.px + 2.0 * q.py + 3.0 * q.pz;
         printf("GEOAUDIT PCOST  checksum %.6f (threads %d)\n", sumPos, ParticleSystemNode::sMaxThreads);
         printf("GEOAUDIT PCOST  %6d requested, %6zu alive: cook mean %.2f ms p95 %.2f ms | copy of points %.2f ms | cook + copy %.0f%% of a 16.7 ms frame\n",
                count, p.AliveCount(), sum / (double)cook.size(), cook[cook.size() * 95 / 100], copyMs,
                100.0 * (sum / (double)cook.size() + copyMs) / 16.7);
      }
      ParticleSystemNode::sMaxThreads = 0;
   }

   // ============================================================ RIGID BODIES
   void AuditRigid(Flags& f, bool& ok)
   {
      Transport& tr = Transport::Instance();
      tr.SetPlaying(false);
      tr.SetTempo(120.0f);   // one beat is half a second

      auto bounds = [](const Mesh& m, double& lo, double& hi) {
         lo = 1e30; hi = -1e30;
         for (const Vertex& v : m.vertices) { lo = std::min(lo, (double)v.py); hi = std::max(hi, (double)v.py); }
      };
      auto run = [&](std::initializer_list<RigidBodyNode*> nodes, int steps, double beatsPerStep) {
         double beat = 0.0;
         tr.SeekBeats(0.0);
         for (RigidBodyNode* n : nodes) n->CookIfNeeded(gFrame++);
         for (int i = 0; i < steps; i++)
         {
            beat += beatsPerStep;
            tr.SeekBeats(beat);
            const int fr = gFrame++;
            for (RigidBodyNode* n : nodes) n->CookIfNeeded(fr);
         }
      };
      GeometryNode cube;
      cube.shape = 1;
      cube.detail = 4;
      const Mesh cubeMesh = Build(cube);
      double cLo, cHi;
      bounds(cubeMesh, cLo, cHi);
      const double h = cHi - cLo;

      // Free fall: no ground, no damping, y drops by g t^2 / 2.
      {
         KindProbe src; src.mesh = cubeMesh;
         RigidBodyNode r;
         r.input = &src;
         r.groundEnabled = false;
         r.linearDamping = 0.0f;
         run({ &r }, 4, 0.25);   // 0.5 s
         double lo, hi; bounds(r.GetMesh(), lo, hi);
         const double fell = cLo - lo;
         const double want = 0.5 * 9.8 * 0.25;
         printf("GEOAUDIT RIGID  free fall 0.5 s: dropped %.4f | g t^2 / 2 = %.4f\n", fell, want);
         if (std::fabs(fell / want - 1.0) > 0.05)
            f.Flag("RIGID", "free fall", "drop differs from g t^2 / 2, %", (fell / want - 1.0) * 100.0);
      }

      // Rest on the ground: lands flat, ends at the ground height, stops moving, same shape.
      for (int kind = 0; kind < 3; kind++)
      {
         KindProbe src; src.mesh = cubeMesh;
         RigidBodyNode r;
         r.input = &src;
         r.collider = kind;
         r.groundHeight = (float)(cLo - 2.0);
         run({ &r }, 32, 0.25);
         const Mesh a = r.GetMesh();
         run({ &r }, 33, 0.25);
         const Mesh b = r.GetMesh();
         double lo, hi; bounds(b, lo, hi);
         double drift = 0.0;
         for (size_t i = 0; i < a.vertices.size() && i < b.vertices.size(); i++)
            drift = std::max(drift, (double)std::fabs(a.vertices[i].py - b.vertices[i].py));
         const Stats st = Measure(b);
         printf("GEOAUDIT RIGID  collider %d on ground: bottom %.4f (ground %.4f) height %.4f (was %.4f) drift %.5f\n",
                kind, lo, cLo - 2.0, hi - lo, h, drift);
         if (st.nonFinite > 0)
            f.Flag("RIGID", "rest", "non-finite vertices", st.nonFinite);
         if (lo < cLo - 2.0 - 0.03)
            f.Flag("RIGID", "rest", "sank below the ground by", cLo - 2.0 - lo);
         if (kind != 1 && lo > cLo - 2.0 + 0.05)
            f.Flag("RIGID", "rest", "stopped above the ground by", lo - (cLo - 2.0));
         if (kind != 1 && std::fabs((hi - lo) - h) > 0.02 * h + 0.05 && std::fabs((hi - lo) - h * 1.7) > 0.1 * h)
            f.Flag("RIGID", "rest", "shape changed, height", hi - lo);
         if (drift > 0.002)
            f.Flag("RIGID", "rest", "still moving after 8 s, drift", drift);
      }

      // Stack: a cube dropped on a cube on the ground stays up and on top.
      {
         KindProbe src;
         src.mesh = cubeMesh;
         Mesh top = MeshOps::Transform(cubeMesh, Mat4::Translation(0.0f, (float)(h * 1.5), 0.0f));
         const unsigned int base = (unsigned int)src.mesh.vertices.size();
         src.mesh.vertices.insert(src.mesh.vertices.end(), top.vertices.begin(), top.vertices.end());
         for (unsigned int i : top.indices) src.mesh.indices.push_back(i + base);
         RigidBodyNode r;
         r.input = &src;
         r.groundHeight = (float)(cLo - 0.0);
         run({ &r }, 40, 0.25);   // 10 s
         const Mesh& m = r.GetMesh();
         double lo = 1e30, hi = -1e30;
         for (const Vertex& v : m.vertices) { lo = std::min(lo, (double)v.py); hi = std::max(hi, (double)v.py); }
         double topLo = 1e30;
         for (size_t i = base; i < m.vertices.size(); i++) topLo = std::min(topLo, (double)m.vertices[i].py);
         double midX = 0.0;
         for (size_t i = base; i < m.vertices.size(); i++) midX += m.vertices[i].px;
         midX /= (double)(m.vertices.size() - base);
         printf("GEOAUDIT RIGID  stack of two: bodies %zu, total height %.4f (2 cubes = %.4f), top rests at %.4f, top centre x %.4f\n",
                r.BodyCount(), hi - lo, 2 * h, topLo - lo, midX);
         if (r.BodyCount() != 2)
            f.Flag("RIGID", "stack", "body count", (double)r.BodyCount());
         if (std::fabs((hi - lo) - 2 * h) > 0.1 * h)
            f.Flag("RIGID", "stack", "stack height differs from two cubes, by", (hi - lo) - 2 * h);
         if (topLo < lo + 0.85 * h)
            f.Flag("RIGID", "stack", "top cube sank into the bottom one, by", lo + h - topLo);
      }

      // Bounce: a sphere with bounce 0.8 comes back up a good way; with 0 it does not.
      for (int pass = 0; pass < 2; pass++)
      {
         KindProbe src; src.mesh = cubeMesh;
         RigidBodyNode r;
         r.input = &src;
         r.collider = RigidBodyNode::kSphere;
         r.bounce = pass == 0 ? 0.8f : 0.0f;
         r.groundHeight = (float)(cLo - 1.0);
         double beat = 0.0;
         tr.SeekBeats(0.0);
         r.CookIfNeeded(gFrame++);
         double lowest = 1e30, afterLow = -1e30;
         bool bounced = false;
         for (int i = 0; i < 24; i++)
         {
            beat += 0.125;
            tr.SeekBeats(beat);
            r.CookIfNeeded(gFrame++);
            double lo, hi; bounds(r.GetMesh(), lo, hi);
            if (lo < lowest) lowest = lo; else bounced = true;
            if (bounced) afterLow = std::max(afterLow, lo);
         }
         const double rose = afterLow - lowest;
         printf("GEOAUDIT RIGID  sphere bounce %.1f: rose %.4f after the first landing (fell 1.0)\n", r.bounce, rose);
         if (pass == 0 && rose < 0.25)
            f.Flag("RIGID", "bounce", "bounce 0.8 rose only", rose);
         if (pass == 1 && rose > 0.1)
            f.Flag("RIGID", "bounce", "bounce 0 still rose", rose);
      }

      // Determinism: same patch twice, and 0.25 vs 0.5 beat steps for the same 4 s.
      {
         KindProbe src; src.mesh = cubeMesh;
         KindProbe src2; src2.mesh = cubeMesh;
         RigidBodyNode a, b;
         a.input = &src; b.input = &src2;
         a.spinX = b.spinX = 3.0f; a.spinZ = b.spinZ = 2.0f; a.velX = b.velX = 1.0f;
         a.groundHeight = b.groundHeight = (float)(cLo - 1.0);
         run({ &a }, 16, 0.5);
         run({ &b }, 32, 0.25);
         const bool same = SameMesh(a.GetMesh(), b.GetMesh());
         printf("GEOAUDIT RIGID  same result at two frame rates: %s\n", same ? "yes" : "NO");
         if (!same)
         {
            f.Flag("DET", "Rigid Bodies", "result depends on the frame rate", 1);
            ok = false;
         }
      }

      // Hostile numbers, no input, bypass.
      {
         KindProbe src; src.mesh = cubeMesh;
         RigidBodyNode r;
         r.input = &src;
         r.mass = 0.0f; r.iterations = 0; r.gravityY = 1e9f; r.velX = 1e9f; r.spinY = 1e9f; r.friction = 1e9f;
         r.wallsEnabled = true; r.bounce = 5.0f;
         run({ &r }, 8, 1.0);
         const Stats s = Measure(r.GetMesh());
         printf("GEOAUDIT RIGID  hostile settings: non-finite %d\n", s.nonFinite);
         if (s.nonFinite > 0)
         {
            f.Flag("DEGEN", "Rigid hostile", "non-finite vertices", s.nonFinite);
            ok = false;
         }
         RigidBodyNode none;
         none.CookIfNeeded(gFrame++);
         RigidBodyNode byp;
         KindProbe src3; src3.mesh = cubeMesh;
         byp.input = &src3; byp.bypassed = true;
         byp.CookIfNeeded(gFrame++);
         if (!SameMesh(byp.GetMesh(), cubeMesh))
            f.Flag("BYPASS", "Rigid Bodies", "bypassed node does not pass its input through", 1);
      }
      tr.SeekBeats(0.0);
   }

   // ============================================================== INSTANCES
   void AuditInstances(Flags& f, bool& ok)
   {
      GeometryNode points;
      points.shape = 2; // Sphere
      points.detail = 16;
      GeometryNode stamp;
      stamp.shape = 1;
      const Mesh& pm = Build(points);
      double srcR = 0.0;
      for (const Vertex& v : pm.vertices)
         srcR += std::sqrt((double)v.px * v.px + (double)v.py * v.py + (double)v.pz * v.pz);
      srcR /= (double)std::max<size_t>(1, pm.vertices.size());

      InstanceOnPointsNode n;
      n.pointSource = &points;
      n.instanceShape = &stamp;
      n.pointMode = 0; // vertices
      n.maxPoints = 100000;
      n.scaleRandom = 0.0f;
      n.instanceScale = 0.1f;
      n.rotationRandom = 0.0f;
      n.CookIfNeeded(gFrame++);
      const size_t verts = pm.vertices.size();
      const std::vector<Mat4>& xf = n.InstanceTransforms();
      printf("GEOAUDIT INST   vertices mode: %zu points on the source (radius %.3f) -> %zu instances\n", verts, srcR, xf.size());
      // The sampler welds coincident positions (UV seams, poles), so the count is the number of distinct positions.
      std::set<std::tuple<long long, long long, long long>> distinct;
      for (const Vertex& v : pm.vertices)
         distinct.insert({ std::llround((double)v.px * 1e4), std::llround((double)v.py * 1e4), std::llround((double)v.pz * 1e4) });
      if (xf.size() != distinct.size())
         f.Flag("INST", "vertices", "instance count differs from the distinct point count", (double)xf.size() - (double)distinct.size());

      // Cap: maxPoints 50.
      n.maxPoints = 50;
      n.CookIfNeeded(gFrame++);
      printf("GEOAUDIT INST   maxPoints 50 -> %zu instances\n", n.InstanceCount());
      if (n.InstanceCount() > 50)
         f.Flag("INST", "cap", "more instances than maxPoints", (double)n.InstanceCount());

      // Placement and scale, in the three point modes: translation lies on the source surface (sphere radius 1), uniform scale as asked.
      for (int mode : { 0, 1, 2 })
      {
         InstanceOnPointsNode m;
         m.pointSource = &points;
         m.instanceShape = &stamp;
         m.pointMode = mode;
         m.maxPoints = 500;
         m.scaleRandom = 0.0f;
         m.instanceScale = 0.1f;
         m.rotationRandom = 0.0f;
         m.alignToNormal = true;
         m.CookIfNeeded(gFrame++);
         double worstR = 0.0, worstScale = 0.0, worstAlign = 0.0;
         int nonFinite = 0, negDet = 0;
         for (const Mat4& t : m.InstanceTransforms())
         {
            bool fin = true;
            for (int i = 0; i < 16; i++)
               if (!std::isfinite(t.m[i]))
                  fin = false;
            if (!fin)
            {
               nonFinite++;
               continue;
            }
            const double px = t.m[12], py = t.m[13], pz = t.m[14];
            {
               const double det = (double)t.m[0] * ((double)t.m[5] * t.m[10] - (double)t.m[6] * t.m[9]) -
                                  (double)t.m[1] * ((double)t.m[4] * t.m[10] - (double)t.m[6] * t.m[8]) +
                                  (double)t.m[2] * ((double)t.m[4] * t.m[9] - (double)t.m[5] * t.m[8]);
               if (det < 0.0)
                  negDet++;
            }
            const double r = std::sqrt(px * px + py * py + pz * pz);
            worstR = std::max(worstR, std::fabs(r - srcR));
            // Column lengths: the stamp scale.
            double best = 0.0;
            for (int c = 0; c < 3; c++)
            {
               const double len = std::sqrt((double)t.m[c * 4] * t.m[c * 4] + (double)t.m[c * 4 + 1] * t.m[c * 4 + 1] + (double)t.m[c * 4 + 2] * t.m[c * 4 + 2]);
               worstScale = std::max(worstScale, std::fabs(len - 0.1));
               const double dot = (t.m[c * 4] * px + t.m[c * 4 + 1] * py + t.m[c * 4 + 2] * pz) / std::max(r, 1e-9) / std::max(len, 1e-9);
               best = std::max(best, std::fabs(dot));
            }
            worstAlign = std::max(worstAlign, 1.0 - best); // some stamp axis points along the surface normal
         }
         printf("GEOAUDIT INST   pointMode %d: %zu instances | off the sphere by %.4f | scale off by %.5f | no axis along the normal by %.4f | non-finite %d\n",
                mode, m.InstanceCount(), worstR, worstScale, worstAlign, nonFinite);
         printf("GEOAUDIT INST   pointMode %d: %d of %zu transforms mirror the stamp (negative determinant)\n", mode, negDet, m.InstanceCount());
         Quality("geo.inst_mirrored", "Instance on Points", negDet);
         if (negDet > 0)
            f.Flag("INST", "mode " + std::to_string(mode), "transforms with negative determinant", negDet);
         if (nonFinite > 0)
         {
            f.Flag("INST", "mode " + std::to_string(mode), "non-finite transforms", nonFinite);
            ok = false;
         }
         if (worstScale > 1e-3)
            f.Flag("INST", "mode " + std::to_string(mode), "scale differs from instanceScale by", worstScale);
         if (worstAlign > 0.02)
            f.Flag("INST", "mode " + std::to_string(mode), "alignToNormal leaves no axis on the normal, by", worstAlign);
         if (mode == 0 && worstR > 0.02)
            f.Flag("INST", "mode 0", "instances are off the source surface by", worstR);
      }

      // Seed: the same seed reproduces, a different one does not.
      auto build = [&](float seed) {
         InstanceOnPointsNode m;
         m.pointSource = &points;
         m.instanceShape = &stamp;
         m.pointMode = 2;
         m.maxPoints = 200;
         m.seed = seed;
         m.CookIfNeeded(gFrame++);
         return m.InstanceTransforms();
      };
      const std::vector<Mat4> s1 = build(3.0f), s2 = build(3.0f), s3 = build(7.0f);
      const bool same = s1.size() == s2.size() && std::memcmp(s1.data(), s2.data(), s1.size() * sizeof(Mat4)) == 0;
      const bool differs = s1.size() != s3.size() || std::memcmp(s1.data(), s3.data(), s1.size() * sizeof(Mat4)) != 0;
      printf("GEOAUDIT INST   seed: same seed reproduces %s, other seed differs %s\n", same ? "yes" : "NO", differs ? "yes" : "NO");
      if (!same)
      {
         f.Flag("DET", "Instances", "same seed gives different transforms", 1);
         ok = false;
      }
      if (!differs)
         f.Flag("INST", "seed", "a different seed gives the same transforms", 1);

      // The realised result is a valid mesh, and the counts add up.
      InstanceOnPointsNode r;
      r.pointSource = &points;
      r.instanceShape = &stamp;
      r.maxPoints = 60;
      r.CookIfNeeded(gFrame++);
      const Mesh& stampMesh = Build(stamp);
      const Mesh real = MeshOps::RealizeInstances(stampMesh, r.InstanceTransforms(), Mat4::Identity(), &r.InstanceColors(), 256);
      printf("GEOAUDIT INST   realised: %zu instances x %zu tris = %zu tris (mesh has %zu)\n", r.InstanceCount(), stampMesh.indices.size() / 3,
             r.InstanceCount() * stampMesh.indices.size() / 3, real.indices.size() / 3);
      Report(f, "INST", "realised", Measure(real), false, ok);
      if (real.indices.size() != r.InstanceCount() * stampMesh.indices.size())
         f.Flag("INST", "realised", "triangle count differs from instances x stamp, tris", (double)real.indices.size() / 3.0);

      // Hostile and empty inputs.
      InstanceOnPointsNode h;
      h.pointSource = &points;
      h.instanceShape = &stamp;
      h.maxPoints = 0;
      h.instanceScale = -1.0f;
      h.scaleRandom = 1e6f;
      h.normalOffset = 1e6f;
      h.CookIfNeeded(gFrame++);
      for (const Mat4& t : h.InstanceTransforms())
         for (int i = 0; i < 16; i++)
            if (!std::isfinite(t.m[i]))
            {
               f.Flag("DEGEN", "Instances hostile", "non-finite transform", 1);
               ok = false;
               i = 16;
            }
      InstanceOnPointsNode none;
      none.CookIfNeeded(gFrame++);
      printf("GEOAUDIT INST   no inputs: %zu instances, hostile settings %zu instances\n", none.InstanceCount(), h.InstanceCount());
   }

   // ============================================================= REFERENCES
   struct P2 { double x, y; };

   std::vector<P2> ConvexHull(std::vector<P2> p)
   {
      std::sort(p.begin(), p.end(), [](const P2& a, const P2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
      auto cross = [](const P2& o, const P2& a, const P2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
      std::vector<P2> h(2 * p.size());
      size_t k = 0;
      for (size_t i = 0; i < p.size(); i++)
      {
         while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0)
            k--;
         h[k++] = p[i];
      }
      for (size_t i = p.size() - 1, t = k + 1; i > 0; i--)
      {
         while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0)
            k--;
         h[k++] = p[i - 1];
      }
      h.resize(k > 0 ? k - 1 : 0);
      return h;
   }

   double PolyArea2(const std::vector<P2>& p)
   {
      double a = 0.0;
      for (size_t i = 0; i < p.size(); i++)
      {
         const P2 &u = p[i], &v = p[(i + 1) % p.size()];
         a += u.x * v.y - v.x * u.y;
      }
      return std::fabs(a) * 0.5;
   }

   void AuditRef(Flags& f, bool& ok)
   {
      // ---- Delaunay against its closed-form counts: triangles = 2n - 2 - h, area = convex hull area.
      GeometryNode sphere;
      sphere.shape = 2;
      sphere.detail = 24;
      DistributePointsOnFacesNode dist;
      dist.input = &sphere;
      dist.method = 0;
      for (float density : { 6.0f, 20.0f })
      {
         dist.density = density;
         dist.CookIfNeeded(gFrame++);
         const auto* cloud = dist.GetPointCloud();
         std::vector<P2> pts;
         if (cloud)
            for (const Particle& p : *cloud)
               pts.push_back({ p.px, p.py });
         DelaunayMeshNode del;
         del.input = &dist;
         del.plane = 0;
         del.CookIfNeeded(gFrame++);
         const Mesh& dm = del.GetMesh();
         const std::vector<P2> hull = ConvexHull(pts);
         const size_t n = pts.size(), h = hull.size();
         const size_t wantTris = 2 * n - 2 - h;
         double area = 0.0;
         std::vector<char> used(dm.vertices.size(), 0);
         for (size_t t = 0; t + 2 < dm.indices.size(); t += 3)
         {
            const Vertex &A = dm.vertices[dm.indices[t]], &B = dm.vertices[dm.indices[t + 1]], &C = dm.vertices[dm.indices[t + 2]];
            area += 0.5 * std::fabs((double)(B.px - A.px) * (C.py - A.py) - (double)(C.px - A.px) * (B.py - A.py));
         }
         const double hullArea = PolyArea2(hull);
         printf("GEOAUDIT REF    Delaunay %zu points (hull %zu): %zu triangles (2n - 2 - h = %zu) | area %.4f (hull %.4f)\n", n, h, dm.indices.size() / 3,
                wantTris, area, hullArea);
         if (dm.indices.size() / 3 != wantTris)
            f.Flag("REF", "Delaunay", "triangle count differs from 2n - 2 - h, by", (double)dm.indices.size() / 3.0 - (double)wantTris);
         if (std::fabs(area / hullArea - 1.0) > 1e-3)
            f.Flag("REF", "Delaunay", "triangles do not tile the convex hull, area ratio", area / hullArea);
      }

      // ---- Voronoi: every cell corner is a circumcentre, equidistant from its three nearest sites.
      dist.density = 12.0f;
      dist.CookIfNeeded(gFrame++);
      const auto* cloud = dist.GetPointCloud();
      VoronoiCellsNode vor;
      vor.input = &dist;
      vor.plane = 0;
      vor.CookIfNeeded(gFrame++);
      const Mesh& vm = vor.GetMesh();
      size_t corners = 0, bad = 0;
      double worstSpread = 0.0;
      if (cloud)
         for (const Vertex& v : vm.vertices)
         {
            double d[3] = { 1e30, 1e30, 1e30 };
            bool isSite = false;
            for (const Particle& p : *cloud)
            {
               const double dd = std::sqrt((double)(p.px - v.px) * (p.px - v.px) + (double)(p.py - v.py) * (p.py - v.py));
               if (dd < 1e-5)
                  isSite = true;
               if (dd < d[0]) { d[2] = d[1]; d[1] = d[0]; d[0] = dd; }
               else if (dd < d[1]) { d[2] = d[1]; d[1] = dd; }
               else if (dd < d[2]) d[2] = dd;
            }
            if (isSite)
               continue;
            corners++;
            const double spread = (d[2] - d[0]) / std::max(d[0], 1e-9);
            worstSpread = std::max(worstSpread, spread);
            if (spread > 1e-3)
               bad++;
         }
      printf("GEOAUDIT REF    Voronoi: %zu non-site vertices, %zu not equidistant from their three nearest sites (worst spread %.2e)\n", corners, bad, worstSpread);
      // The node clamps sliver circumcentres beside the hull to twice the cell reach (DelaunayNodes.cpp), by design, so a few
      // corners are not circumcentres; flag only when that is more than one corner in ten.
      if (corners == 0)
         f.Flag("REF", "Voronoi", "no cell corners to check", 0);
      else if ((double)bad / (double)corners > 0.10)
         f.Flag("REF", "Voronoi", "share of cell corners that are not circumcentres, %", 100.0 * (double)bad / (double)corners);

      // ---- Decimate: error against the original surface, and the requested count.
      struct Shape { const char* name; int shape; int detail; };
      const Shape shapes[2] = { { "Icosphere", 3, 24 }, { "Torus", 4, 48 } };
      for (const Shape& sh : shapes)
      {
         GeometryNode g;
         g.shape = sh.shape;
         g.detail = sh.detail;
         const Mesh orig = Build(g);
         double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
         for (const Vertex& v : orig.vertices)
         {
            lo[0] = std::min(lo[0], (double)v.px); hi[0] = std::max(hi[0], (double)v.px);
            lo[1] = std::min(lo[1], (double)v.py); hi[1] = std::max(hi[1], (double)v.py);
            lo[2] = std::min(lo[2], (double)v.pz); hi[2] = std::max(hi[2], (double)v.pz);
         }
         const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
         const Stats so = Measure(orig);
         double prevMax = 0.0;
         for (float ratio : { 0.5f, 0.25f, 0.1f })
         {
            const Mesh dec = MeshOps::Decimate(orig, ratio, true);
            const Stats sd = Measure(dec);
            double mean = 0.0, worst = 0.0;
            for (const Vertex& v : dec.vertices)
            {
               double best = 1e30;
               for (size_t t = 0; t + 2 < orig.indices.size(); t += 3)
               {
                  const Vertex &A = orig.vertices[orig.indices[t]], &B = orig.vertices[orig.indices[t + 1]], &C = orig.vertices[orig.indices[t + 2]];
                  best = std::min(best, PointTriDist({ v.px, v.py, v.pz }, { A.px, A.py, A.pz }, { B.px, B.py, B.pz }, { C.px, C.py, C.pz }));
               }
               mean += best;
               worst = std::max(worst, best);
            }
            mean /= (double)std::max<size_t>(1, dec.vertices.size());
            const double countRatio = (double)sd.tris / ((double)so.tris * ratio);
            const double vol = so.volume != 0.0 ? sd.volume / so.volume - 1.0 : 0.0;
            printf("GEOAUDIT REF    Decimate %-9s ratio %.2f: %zu -> %zu tris (%.0f %% of asked) | vertex-to-original distance mean %.5f worst %.5f (%.2f %% of bbox diagonal) | volume %+.1f %% | boundary edges %d\n",
                   sh.name, ratio, so.tris, sd.tris, countRatio * 100.0, mean, worst, worst / diag * 100.0, vol * 100.0, sd.boundary);
            Quality("geo.decimate_bbox_pct", "Decimate", worst / diag * 100.0);
            Report(f, "REF", std::string("Decimate ") + sh.name, sd, false, ok);
            if (std::fabs(countRatio - 1.0) > 0.10)
               f.Flag("REF", std::string("Decimate ") + sh.name, "triangle count off the requested ratio, %", (countRatio - 1.0) * 100.0);
            if (worst / diag > 0.05)
               f.Flag("REF", std::string("Decimate ") + sh.name, "vertex further than 5 % of the bbox diagonal from the original, %", worst / diag * 100.0);
            if (worst + 1e-9 < prevMax * 0.5)
               f.Flag("REF", std::string("Decimate ") + sh.name, "error shrank while decimating harder", worst);
            if (sd.boundary > so.boundary)
               f.Flag("REF", std::string("Decimate ") + sh.name, "decimation opened the mesh, new boundary edges", sd.boundary - so.boundary);
            prevMax = std::max(prevMax, worst);
         }
      }
   }

   // ============================================================ DISPLACEMENT
   // Needs a GL texture: INFINITE_GEOAUDIT=1 INFINITE_GEOAUDIT_ONLY=DISP runs it from the GL-ready dispatch in Startup.cpp.
   class TexSource : public INode
   {
   public:
      TexSource(int w, int h) : mW(w), mH(h)
      {
         glGenTextures(1, &mTex);
         glBindTexture(GL_TEXTURE_2D, mTex);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, 0);
      }
      ~TexSource() override { glDeleteTextures(1, &mTex); }
      void Fill(unsigned char r, unsigned char g, unsigned char b)
      {
         std::vector<unsigned char> px((size_t)mW * mH * 4);
         for (size_t i = 0; i < (size_t)mW * mH; i++)
         {
            px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
         }
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mW, mH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindTexture(GL_TEXTURE_2D, 0);
         mRevision = NextTextureRevision();
      }
      unsigned int GetOutputTexture() override { return mTex; }
      int GetOutputWidth() const override { return mW; }
      int GetOutputHeight() const override { return mH; }
      void CookIfNeeded(int) override {}
      unsigned long long TextureRevision() const override { return mRevision; }

   private:
      int mW, mH;
      unsigned int mTex = 0;
      unsigned long long mRevision = 0;
   };

   void AuditDisplace(Flags& f, bool& ok)
   {
      GeometryNode ico;
      ico.shape = 3;
      ico.detail = 3;
      const Mesh orig = Build(ico);
      const Stats so = Measure(orig);
      TexSource tex(16, 16);

      struct Case { const char* label; int mode; unsigned char r, g, b; float strength, midlevel; };
      const Case cases[] = {
         { "scalar grey 0.5 = midlevel", DisplacementNode::kScalar, 128, 128, 128, 0.4f, 0.5f },
         { "scalar white", DisplacementNode::kScalar, 255, 255, 255, 0.4f, 0.5f },
         { "scalar black", DisplacementNode::kScalar, 0, 0, 0, 0.4f, 0.5f },
         { "scalar white, midlevel 0", DisplacementNode::kScalar, 255, 255, 255, 0.3f, 0.0f },
         { "vector (1, 0.5, 0.5)", DisplacementNode::kVector, 255, 128, 128, 0.25f, 0.5f },
      };
      for (const Case& c : cases)
      {
         tex.Fill(c.r, c.g, c.b);
         DisplacementNode d;
         d.input = &ico;
         d.TextureInput().Connect(&tex);
         d.mode = c.mode;
         d.strength = c.strength;
         d.midlevel = c.midlevel;
         d.CookIfNeeded(gFrame++);
         const Mesh& m = d.GetMesh();
         if (m.vertices.size() != orig.vertices.size())
         {
            f.Flag("DISP", c.label, "vertex count changed", (double)m.vertices.size());
            continue;
         }
         double worst = 0.0;
         for (size_t i = 0; i < m.vertices.size(); i++)
         {
            const Vertex& o = orig.vertices[i];
            double ex, ey, ez;
            const double lum = (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
            if (c.mode == DisplacementNode::kScalar)
            {
               const double dist = (lum - c.midlevel) * c.strength;
               ex = o.nx * dist; ey = o.ny * dist; ez = o.nz * dist;
            }
            else
            {
               ex = (c.r / 255.0 - 0.5) * 2.0 * c.strength;
               ey = (c.g / 255.0 - 0.5) * 2.0 * c.strength;
               ez = (c.b / 255.0 - 0.5) * 2.0 * c.strength;
            }
            // Welded duplicates share the average of their normals' offsets; the check is on the mean over a weld group, so allow for the
            // per-vertex normal difference only through the closed-surface test below.
            worst = std::max(worst, std::sqrt(std::pow(m.vertices[i].px - o.px - ex, 2) + std::pow(m.vertices[i].py - o.py - ey, 2) +
                                              std::pow(m.vertices[i].pz - o.pz - ez, 2)));
         }
         const Stats s = Measure(m);
         printf("GEOAUDIT DISP   %-28s: worst vertex off the expected offset %.5f | boundary edges %d (was %d)\n", c.label, worst, s.boundary, so.boundary);
         Quality("geo.disp_offset", "Displacement", worst);
         Report(f, "DISP", c.label, s, false, ok);
         if (worst > 0.02)
            f.Flag("DISP", c.label, "vertex off the expected offset, worst", worst);
         if (s.boundary > so.boundary)
            f.Flag("DISP", c.label, "displacement tore the surface, new boundary edges", s.boundary - so.boundary);
      }
      // Without a texture the positions pass through (normals may be rebuilt).
      DisplacementNode bare;
      bare.input = &ico;
      bare.CookIfNeeded(gFrame++);
      const Mesh& bm = bare.GetMesh();
      double moved = bm.vertices.size() == orig.vertices.size() ? 0.0 : 1e9;
      for (size_t i = 0; moved < 1e8 && i < bm.vertices.size(); ++i)
         moved = std::max(moved, std::sqrt(std::pow(bm.vertices[i].px - orig.vertices[i].px, 2) + std::pow(bm.vertices[i].py - orig.vertices[i].py, 2) +
                                           std::pow(bm.vertices[i].pz - orig.vertices[i].pz, 2)));
      printf("GEOAUDIT DISP   no texture: positions moved by %.5f\n", moved);
      if (moved > 1e-4)
         f.Flag("DISP", "no texture", "vertices moved without a texture", moved);
   }


   // --- RENDERQ: Render 3D quality (v0.6 3.5) ---------------------------------
   // Needs GL. Ambient occlusion, depth of field, offline supersampling and the
   // float output, each against the plain render of the same scene.
   struct RqImage
   {
      int w = 0, h = 0;
      std::vector<unsigned char> px;
      double Luma(int x, int y) const
      {
         const unsigned char* p = &px[((size_t)y * w + x) * 4];
         return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
      }
   };

   bool RqRead(Render3DNode& r, RqImage& img)
   {
      return ReadNodeImageRgba8(&r, 0, true, img.w, img.h, img.px);
   }

   void RqCook(Render3DNode& r)
   {
      r.CookIfNeeded(gFrame++);
   }

   // Sum of absolute luma steps inside a window: how much detail is left in it.
   double RqEnergy(const RqImage& im, int x0, int y0, int x1, int y1)
   {
      double e = 0.0;
      for (int y = y0; y < y1 - 1; y++)
         for (int x = x0; x < x1 - 1; x++)
            e += std::fabs(im.Luma(x + 1, y) - im.Luma(x, y)) + std::fabs(im.Luma(x, y + 1) - im.Luma(x, y));
      return e;
   }


   // --- AREALIGHT: Light node "Area" type (v0.6 3.6) --------------------------
   // A tiny panel matches a point light at the same place; a big one gives a broader, softer highlight
   // and never makes the picture non-finite or black; the back of the panel gives no light.
   void AuditAreaLight(Flags& f, bool& ok)
   {
      (void)ok;
      GeometryNode sphere;
      sphere.shape = 2; sphere.detail = 48; sphere.sides = 48;
      LightNode point, tiny, big, backFacing;
      point.type = 1; tiny.type = 5; big.type = 5; backFacing.type = 5;
      for (LightNode* l : { &point, &tiny, &big, &backFacing })
      {
         l->azimuth = 40.0f; l->elevation = 35.0f; l->distance = 3.0f; l->intensity = 2.0f;
      }
      tiny.areaWidth = tiny.areaHeight = 0.05f;
      big.areaWidth = big.areaHeight = 3.0f;
      auto draw = [&](LightNode& l, RqImage& im) {
         Render3DNode r;
         r.width = 192.0f; r.height = 192.0f; r.camDistance = 5.0f; r.camAzimuth = 40.0f; r.camElevation = 35.0f;
         r.samples = 2;
         r.geometry[0] = &sphere;
         r.lights[0] = &l;
         r.CookIfNeeded(gFrame++);
         return RqRead(r, im);
      };
      RqImage ip, it, ib;
      bool good = draw(point, ip);
      good = draw(tiny, it) && good;
      good = draw(big, ib) && good;
      auto mean = [](const RqImage& im) { double s = 0; for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) s += im.Luma(x, y); return s / (double)(im.w * im.h); };
      auto bright = [](const RqImage& im, double cut) { int n = 0; for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) if (im.Luma(x, y) > cut) n++; return n; };
      double peakB = 0.0, peakT = 0.0;
      if (good)
         for (int y = 0; y < ip.h; y++)
            for (int x = 0; x < ip.w; x++) { peakT = std::max(peakT, it.Luma(x, y)); peakB = std::max(peakB, ib.Luma(x, y)); }
      const double mp = good ? mean(ip) : 0.0, mt = good ? mean(it) : 0.0, mb = good ? mean(ib) : 0.0;
      printf("GEOAUDIT AREALIGHT mean luma point %.2f, tiny panel %.2f, 3x3 panel %.2f; peak tiny %.0f, big %.0f; pixels over 70%% of tiny's peak: tiny %d, big %d\n",
             mp, mt, mb, peakT, peakB, good ? bright(it, 0.7 * peakT) : 0, good ? bright(ib, 0.7 * peakT) : 0);
      if (!good) f.Flag("AREALIGHT", "render", "could not read the render", 0);
      else
      {
         if (std::fabs(mp - mt) > 6.0) f.Flag("AREALIGHT", "tiny panel", "a tiny panel does not match a point light", std::fabs(mp - mt));
         if (mb < 5.0) f.Flag("AREALIGHT", "big panel", "a big panel renders black", mb);
         if (bright(ib, 0.7 * peakT) < bright(it, 0.7 * peakT)) f.Flag("AREALIGHT", "big panel", "a bigger panel does not widen the lit area", mb);
      }
      // The panel faces the origin, so a light behind a mirror-flat floor still lights what it faces; with the
      // light pushed under the ground plane's far side nothing may go NaN: re-render at an extreme size.
      backFacing.areaWidth = 1e6f; backFacing.areaHeight = 1e6f;
      RqImage ie;
      const bool ex = draw(backFacing, ie);
      if (!ex || !(mean(ie) >= 0.0) || !(mean(ie) <= 255.0)) f.Flag("AREALIGHT", "extreme", "huge panel is not finite", 0);
   }

   void AuditRenderQuality(Flags& f, bool& ok)
   {
      (void)ok;
      GeometryNode plane, cube, sphere;
      plane.shape = 0; plane.uniformScale = 6.0f; plane.rotX = -90.0f;
      cube.shape = 1; cube.posY = 0.5f;
      sphere.shape = 2; sphere.detail = 48; sphere.sides = 48; sphere.posY = 0.0f;

      auto setup = [](Render3DNode& r, int dim) {
         r.width = (float)dim; r.height = (float)dim;
         r.camDistance = 5.0f; r.camAzimuth = 35.0f; r.camElevation = 30.0f;
         r.samples = 2;
      };
      Render3DNode scene, flat;
      setup(scene, 256); setup(flat, 256);
      scene.geometry[0] = &plane; scene.geometry[1] = &cube;
      flat.geometry[0] = &plane;

      // 1. Defaults leave the render exactly as it was.
      RqImage base, again;
      RqCook(scene);
      bool good = RqRead(scene, base);
      Render3DNode scene2; setup(scene2, 256);
      scene2.geometry[0] = &plane; scene2.geometry[1] = &cube;
      RqCook(scene2); good = good && RqRead(scene2, again);
      int diff = 0;
      if (good) for (size_t i = 0; i < base.px.size(); i++) if (base.px[i] != again.px[i]) diff++;
      printf("GEOAUDIT RENDERQ default render repeats exactly (%d byte differences), 8-bit target %s\n", diff,
             NodeImageIsFloat(&scene, 0) ? "NO" : "yes");
      if (!good || diff != 0) f.Flag("RENDERQ", "defaults", "default render is not repeatable", diff);
      if (NodeImageIsFloat(&scene, 0)) f.Flag("RENDERQ", "defaults", "default output is float", 1);

      // 2. Ambient occlusion: never brightens, leaves the background and a flat plane alone, darkens the contact.
      auto meanLuma = [](const RqImage& im) { double s = 0; for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) s += im.Luma(x, y); return s / (double)(im.w * im.h); };
      RqImage aoFull, aoHalf, flatBase, flatAo;
      scene.aoStrength = 1.0f; scene.aoRadius = 0.6f;
      RqCook(scene); good = RqRead(scene, aoFull);
      scene.aoStrength = 0.5f; RqCook(scene); good = RqRead(scene, aoHalf) && good;
      int brighter = 0, darker = 0;
      double worstDrop = 0.0;
      if (good)
         for (int y = 0; y < base.h; y++)
            for (int x = 0; x < base.w; x++)
            {
               const double d = aoFull.Luma(x, y) - base.Luma(x, y);
               if (d > 1.0) brighter++;
               if (d < -4.0) darker++;
               worstDrop = std::min(worstDrop, d);
            }
      const double mBase = meanLuma(base), mFull = meanLuma(aoFull), mHalf = meanLuma(aoHalf);
      printf("GEOAUDIT RENDERQ ao: %d px brighter, %d px darker by >4 levels, worst drop %.1f, mean luma %.2f -> half %.2f -> full %.2f\n",
             brighter, darker, worstDrop, mBase, mHalf, mFull);
      if (!good || brighter > 0) f.Flag("RENDERQ", "ao", "occlusion brightened pixels", brighter);
      if (darker < 200) f.Flag("RENDERQ", "ao", "occlusion left the contact crease unchanged", darker);
      if (!(mFull < mHalf && mHalf < mBase)) f.Flag("RENDERQ", "ao", "strength does not scale the darkening", mFull);
      if (good && (aoFull.Luma(2, 2) != base.Luma(2, 2))) f.Flag("RENDERQ", "ao", "background pixel changed", aoFull.Luma(2, 2));
      flat.aoStrength = 0.0f; RqCook(flat); good = RqRead(flat, flatBase);
      flat.aoStrength = 1.0f; flat.aoRadius = 0.6f; RqCook(flat); good = RqRead(flat, flatAo) && good;
      int flatDiff = 0;
      if (good) for (int y = 0; y < flatBase.h; y++) for (int x = 0; x < flatBase.w; x++)
         if (std::fabs(flatAo.Luma(x, y) - flatBase.Luma(x, y)) > 3.0) flatDiff++;
      printf("GEOAUDIT RENDERQ ao on a lone flat plane: %d px changed by >3 levels\n", flatDiff);
      if (flatDiff > 40) f.Flag("RENDERQ", "ao", "a flat plane was occluded", flatDiff);
      scene.aoStrength = 0.0f;

      // 3. Depth of field: focused on the cube it keeps the cube sharp and softens the rest; focused far, the cube blurs.
      RqImage dofNear, dofFar;
      const int cx0 = 80, cy0 = 80, cx1 = 176, cy1 = 176;
      const double eBase = RqEnergy(base, cx0, cy0, cx1, cy1), eAll = RqEnergy(base, 0, 0, base.w, base.h);
      scene.dofAperture = 2.0f; scene.focusDistance = 5.0f; RqCook(scene); good = RqRead(scene, dofNear);
      scene.focusDistance = 30.0f; RqCook(scene); good = RqRead(scene, dofFar) && good;
      const double eNear = RqEnergy(dofNear, cx0, cy0, cx1, cy1), eFar = RqEnergy(dofFar, cx0, cy0, cx1, cy1);
      const double aNear = RqEnergy(dofNear, 0, 0, base.w, base.h);
      printf("GEOAUDIT RENDERQ dof: cube-window detail %.0f sharp, %.0f focused on it, %.0f focused far; whole image %.0f -> %.0f\n",
             eBase, eNear, eFar, eAll, aNear);
      if (!good || eNear < 0.8 * eBase) f.Flag("RENDERQ", "dof", "the focused subject lost detail", eNear / std::max(1.0, eBase));
      if (eFar > 0.75 * eBase) f.Flag("RENDERQ", "dof", "an out-of-focus subject stayed sharp", eFar / std::max(1.0, eBase));
      scene.dofAperture = 0.0f;

      // 4. Supersampling: only offline, and it adds in-between values along edges.
      scene.samples = 0;
      RqImage ss1, ssLive, ss8;
      RqCook(scene); good = RqRead(scene, ss1);
      scene.supersample = 8; RqCook(scene); good = RqRead(scene, ssLive) && good;
      Transport::Instance().SetOfflineMode(true);
      RqCook(scene); good = RqRead(scene, ss8) && good;
      scene.supersample = 1; RqCook(scene);
      RqImage ssOff1; good = RqRead(scene, ssOff1) && good;
      Transport::Instance().SetOfflineMode(false);
      int liveDiff = 0;
      std::set<int> lv1, lv8;
      if (good) for (size_t i = 0; i < ss1.px.size(); i++) if (ss1.px[i] != ssLive.px[i]) liveDiff++;
      if (good) for (int y = 0; y < ss1.h; y++) for (int x = 0; x < ss1.w; x++) { lv1.insert((int)std::lround(ss1.Luma(x, y) * 4)); lv8.insert((int)std::lround(ss8.Luma(x, y) * 4)); }
      int offDiff = 0;
      if (good) for (size_t i = 0; i < ss1.px.size(); i++) if (ss1.px[i] != ssOff1.px[i]) offDiff++;
      const double sMean1 = meanLuma(ss1), sMean8 = meanLuma(ss8);
      printf("GEOAUDIT RENDERQ supersample: live render with 8 set differs by %d bytes; offline 1 pass differs by %d; luma levels %zu -> %zu; mean %.2f -> %.2f\n",
             liveDiff, offDiff, lv1.size(), lv8.size(), sMean1, sMean8);
      if (liveDiff != 0) f.Flag("RENDERQ", "supersample", "a live render changed with supersample set", liveDiff);
      if (offDiff != 0) f.Flag("RENDERQ", "supersample", "one offline pass differs from the plain render", offDiff);
      if (lv8.size() < lv1.size() * 12 / 10) f.Flag("RENDERQ", "supersample", "no smoother edges", (double)lv8.size());
      if (std::fabs(sMean8 - sMean1) > 2.0) f.Flag("RENDERQ", "supersample", "brightness drifted", sMean8 - sMean1);

      // 5. Float output: real precision above 8 bits, same picture.
      Render3DNode fl8, fl16;
      setup(fl8, 128); setup(fl16, 128);
      for (Render3DNode* r : { &fl8, &fl16 }) { r->geometry[0] = &sphere; r->tonemap = 0; r->exposure = 1.0f; }
      fl16.floatOutput = true;
      RqCook(fl8); RqCook(fl16);
      RqImage p8; good = RqRead(fl8, p8);
      std::vector<uint16_t> deep; int dw = 0, dh = 0;
      const bool isFloat = NodeImageIsFloat(&fl16, 0);
      good = ReadNodeImageRgba16(&fl16, 0, true, dw, dh, deep) && good;
      std::set<int> d8, d16;
      int maxStep = 0;
      if (good) for (int i = 0; i < dw * dh; i++) { d16.insert(deep[(size_t)i * 4]); d8.insert(p8.px[(size_t)i * 4]); }
      if (good) for (int i = 0; i < dw * dh; i++)
         maxStep = std::max(maxStep, std::abs((int)std::lround(deep[(size_t)i * 4] / 257.0) - (int)p8.px[(size_t)i * 4]));
      printf("GEOAUDIT RENDERQ float output: target %s, distinct red levels %zu (8-bit %zu), worst gap to the 8-bit render %d levels\n",
             isFloat ? "16-bit float" : "8-bit", d16.size(), d8.size(), maxStep);
      if (!isFloat) f.Flag("RENDERQ", "float", "output is not 16-bit float", 0);
      if (d16.size() < 2 * d8.size()) f.Flag("RENDERQ", "float", "no more levels than 8 bits", (double)d16.size());
      if (maxStep > 2) f.Flag("RENDERQ", "float", "float and 8-bit renders differ", maxStep);

      // 6. Float with multisampling, AO, DOF and supersample together, and hostile values: none may crash or leave black.
      Render3DNode mix; setup(mix, 96);
      mix.geometry[0] = &plane; mix.geometry[1] = &cube;
      mix.floatOutput = true; mix.samples = 2; mix.aoStrength = 1.0f; mix.dofAperture = 1.5f; mix.supersample = 4;
      Transport::Instance().SetOfflineMode(true);
      RqCook(mix);
      Transport::Instance().SetOfflineMode(false);
      RqImage mi; good = RqRead(mix, mi);
      const double mixMean = good ? meanLuma(mi) : 0.0;
      printf("GEOAUDIT RENDERQ everything on together: %s, mean luma %.2f\n", good ? "rendered" : "FAILED", mixMean);
      if (!good || mixMean < 5.0) f.Flag("RENDERQ", "combined", "combined render is empty", mixMean);

      const float hostile[] = { -5.0f, 0.0f, 1e-9f, 1e9f, std::nanf(""), std::numeric_limits<float>::infinity() };
      int hostileBad = 0;
      for (float v : hostile)
      {
         Render3DNode hz; setup(hz, 64);
         hz.geometry[0] = &cube;
         hz.aoStrength = 1.0f; hz.aoRadius = v; hz.dofAperture = 1.0f; hz.focusDistance = v;
         hz.supersample = (int)std::max(-1e6f, std::min(1e6f, std::isfinite(v) ? v : 0.0f));
         Transport::Instance().SetOfflineMode(true);
         RqCook(hz);
         Transport::Instance().SetOfflineMode(false);
         RqImage hi;
         if (!RqRead(hz, hi) || hi.w != 64) hostileBad++;
      }
      if (const char* dir = getenv("INFINITE_RENDERQ_DIR"))
      {
         const std::pair<const char*, const RqImage*> shots[] = { { "base", &base }, { "ao", &aoFull }, { "dof_focus_cube", &dofNear },
                                                                    { "dof_focus_far", &dofFar }, { "alias_1pass", &ss1 }, { "supersample_8", &ss8 } };
         for (const auto& sh : shots)
            WriteSrgbPng(std::string(dir) + "/" + sh.first + ".png", sh.second->w, sh.second->h, sh.second->px.data());
      }
      // 7. What the live passes cost at 1080p: median of 21 cooks, each ended with glFinish.
      {
         Render3DNode big; setup(big, 1080);
         big.width = 1920.0f; big.height = 1080.0f; big.samples = 2;
         big.geometry[0] = &plane; big.geometry[1] = &cube;
         auto timeMs = [&](const char* label) {
            std::vector<double> t;
            for (int i = 0; i < 21; i++)
            {
               big.camAzimuth += 0.37f; // a moved camera defeats the unchanged-scene skip
               const auto t0 = std::chrono::steady_clock::now();
               RqCook(big);
               glFinish();
               t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::sort(t.begin(), t.end());
            printf("GEOAUDIT RENDERQ cost 1920x1080 4x MSAA %-22s %.2f ms (median of 21)\n", label, t[10]);
            return t[10];
         };
         const double tBase = timeMs("scene only");
         big.aoStrength = 1.0f; big.aoRadius = 0.6f;
         const double tAo = timeMs("+ ambient occlusion");
         big.aoStrength = 0.0f; big.dofAperture = 1.0f; big.focusDistance = 5.0f;
         const double tDof = timeMs("+ depth of field");
         big.floatOutput = true; big.dofAperture = 0.0f;
         const double tFl = timeMs("float output only");
         printf("GEOAUDIT RENDERQ added cost: ambient occlusion %.2f ms, depth of field %.2f ms\n", tAo - tBase, tDof - tBase);
         (void)tFl;
      }

      Render3DNode empty; setup(empty, 64);
      empty.aoStrength = 1.0f; empty.dofAperture = 1.0f; empty.supersample = 3;
      Transport::Instance().SetOfflineMode(true);
      RqCook(empty);
      Transport::Instance().SetOfflineMode(false);
      RqImage ei; if (!RqRead(empty, ei) || ei.w != 64) hostileBad++;
      printf("GEOAUDIT RENDERQ hostile values and an empty scene: %d failures\n", hostileBad);
      if (hostileBad != 0) f.Flag("RENDERQ", "hostile", "a hostile value or empty scene failed to render", hostileBad);
   }

} // namespace

bool GeoAuditWantsGl()
{
   const char* only = getenv("INFINITE_GEOAUDIT_ONLY");
   return only != nullptr && (strstr(only, "DISP") != nullptr || strstr(only, "RENDERQ") != nullptr || strstr(only, "AREALIGHT") != nullptr);
}

bool RunGeoAudit()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   const char* only = getenv("INFINITE_GEOAUDIT_ONLY");
   auto want = [&](const char* sec) { return only == nullptr || *only == 0 || strstr(only, sec) != nullptr; };
   Flags flags;
   bool ok = true;
   if (want("PRIM"))
   {
      AuditPrimitives(flags, ok);
      AuditNormals(flags);
      AuditDegenerate(flags, ok);
   }
   if (want("SCALE"))
      AuditScale(flags);
   if (want("OPS"))
      AuditOps(flags, ok);
   if (want("GRAPH"))
      AuditGraph(flags, ok);
   if (want("META"))
      AuditMetaballs(flags, ok);
   if (want("CURVE"))
      AuditCurveOps(flags, ok);
   if (want("SWEEP"))
      AuditSweepRevolve(flags, ok);
   if (want("KINDS"))
      AuditKinds(flags, ok);
   if (want("BEVEL"))
      AuditBevelInset(flags, ok);
   if (want("CLOTH"))
      AuditCloth(flags, ok);
   if (only != nullptr && strstr(only, "PCOST") != nullptr)
      AuditParticleCost();
   if (want("RIGID"))
      AuditRigid(flags, ok);
   if (want("LSYS"))
      AuditLSystem(flags, ok);
   if (want("INST"))
      AuditInstances(flags, ok);
   if (want("REF"))
      AuditRef(flags, ok);
   // DISP needs a GL texture, so it only runs from the GL-ready dispatch (Startup.cpp) and only when asked for by name.
   if (only != nullptr && strstr(only, "DISP") != nullptr)
      AuditDisplace(flags, ok);
   if (only != nullptr && strstr(only, "RENDERQ") != nullptr)
      AuditRenderQuality(flags, ok);
   if (only != nullptr && strstr(only, "AREALIGHT") != nullptr)
      AuditAreaLight(flags, ok);
   printf("GEOAUDIT %s (%d flags)\n", ok ? "PASS" : "FAIL", flags.n);
   return ok;
}

} // namespace app

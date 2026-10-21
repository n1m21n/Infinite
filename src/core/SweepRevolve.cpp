#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Sweep and Revolve. Both build the same thing: a grid of positions with one axis
// running along the path (or round the axis) and the other along the profile. One
// builder turns that grid into a mesh, so the winding, the normals, the seams and the
// caps are decided in one place.

namespace
{
   const double kTwoPi = 6.283185307179586;

   struct V3
   {
      double x = 0, y = 0, z = 0;
   };

   V3 operator+(const V3& a, const V3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
   V3 operator-(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
   V3 operator*(const V3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
   double Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   V3 Cross(const V3& a, const V3& b)
   {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
   }
   double Length(const V3& a) { return std::sqrt(Dot(a, a)); }
   V3 Normalized(const V3& a, const V3& fallback)
   {
      const double l = Length(a);
      return l > 1e-12 ? a * (1.0 / l) : fallback;
   }

   V3 PointAt(const Polyline& l, size_t i)
   {
      return { (double)l.points[i * 3], (double)l.points[i * 3 + 1], (double)l.points[i * 3 + 2] };
   }

   bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

   // The curve's points with non-finite ones, consecutive repeats and a closing repeat of
   // the first point removed: a repeat has no direction, and a closing repeat would put a
   // zero-length edge in the seam.
   std::vector<V3> CleanPoints(const Polyline& l, bool& closed)
   {
      std::vector<V3> out;
      for (size_t i = 0; i < l.Count(); i++)
      {
         const V3 p = PointAt(l, i);
         if (!Finite(p))
            continue;
         if (!out.empty() && Length(p - out.back()) < 1e-9)
            continue;
         out.push_back(p);
      }
      closed = l.closed;
      if (closed && out.size() > 1 && Length(out.back() - out.front()) < 1e-9)
         out.pop_back();
      if (out.size() < 3)
         closed = false;
      return out;
   }

   // Triangulates a planar polygon by ear clipping. The triangles are wound in the
   // direction the polygon's points are listed, whichever way that is.
   std::vector<int> TriangulatePolygon(const std::vector<V3>& pts, std::vector<float>& uv)
   {
      std::vector<int> tris;
      const int n = (int)pts.size();
      if (n < 3)
         return tris;

      V3 newell;
      for (int i = 0; i < n; i++)
      {
         const V3& a = pts[(size_t)i];
         const V3& b = pts[(size_t)((i + 1) % n)];
         newell.x += (a.y - b.y) * (a.z + b.z);
         newell.y += (a.z - b.z) * (a.x + b.x);
         newell.z += (a.x - b.x) * (a.y + b.y);
      }
      // Drop the axis the polygon faces most, keeping the other two in cyclic order so the
      // projection does not mirror it.
      const double ax = std::fabs(newell.x), ay = std::fabs(newell.y), az = std::fabs(newell.z);
      int drop = 2;
      if (ax >= ay && ax >= az) drop = 0;
      else if (ay >= ax && ay >= az) drop = 1;
      std::vector<double> u(n), v(n);
      for (int i = 0; i < n; i++)
      {
         const V3& p = pts[(size_t)i];
         if (drop == 0)      { u[i] = p.y; v[i] = p.z; }
         else if (drop == 1) { u[i] = p.z; v[i] = p.x; }
         else                { u[i] = p.x; v[i] = p.y; }
      }
      double area2 = 0.0;
      for (int i = 0; i < n; i++)
      {
         const int j = (i + 1) % n;
         area2 += u[i] * v[j] - u[j] * v[i];
      }
      if (std::fabs(area2) < 1e-14)
         return tris;

      double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
      for (int i = 0; i < n; i++)
      {
         lo[0] = std::min(lo[0], u[i]); hi[0] = std::max(hi[0], u[i]);
         lo[1] = std::min(lo[1], v[i]); hi[1] = std::max(hi[1], v[i]);
      }
      uv.resize((size_t)n * 2);
      for (int i = 0; i < n; i++)
      {
         uv[(size_t)i * 2] = (float)((u[i] - lo[0]) / std::max(hi[0] - lo[0], 1e-12));
         uv[(size_t)i * 2 + 1] = (float)((v[i] - lo[1]) / std::max(hi[1] - lo[1], 1e-12));
      }

      const bool clockwise = area2 < 0.0;
      std::vector<int> order((size_t)n);
      for (int i = 0; i < n; i++)
         order[(size_t)i] = clockwise ? n - 1 - i : i;

      auto cross2 = [&](int a, int b, int c) {
         return (u[b] - u[a]) * (v[c] - v[a]) - (v[b] - v[a]) * (u[c] - u[a]);
      };
      auto inside = [&](int a, int b, int c, int p) {
         if ((u[p] == u[a] && v[p] == v[a]) || (u[p] == u[b] && v[p] == v[b]) ||
             (u[p] == u[c] && v[p] == v[c]))
            return false;
         return cross2(a, b, p) >= 0.0 && cross2(b, c, p) >= 0.0 && cross2(c, a, p) >= 0.0;
      };

      while (order.size() > 3)
      {
         const size_t m = order.size();
         size_t pick = m;
         for (size_t i = 0; i < m && pick == m; i++)
         {
            const int a = order[(i + m - 1) % m], b = order[i], c = order[(i + 1) % m];
            if (cross2(a, b, c) <= 0.0)
               continue;
            bool ear = true;
            for (size_t j = 0; j < m && ear; j++)
            {
               const int p = order[j];
               if (p != a && p != b && p != c && inside(a, b, c, p))
                  ear = false;
            }
            if (ear)
               pick = i;
         }
         if (pick == m)
            pick = 0; // no clean ear (a pinched polygon): clip anyway so it terminates
         const int a = order[(pick + m - 1) % m], b = order[pick], c = order[(pick + 1) % m];
         if (clockwise) { tris.push_back(a); tris.push_back(c); tris.push_back(b); }
         else           { tris.push_back(a); tris.push_back(b); tris.push_back(c); }
         order.erase(order.begin() + (long)pick);
      }
      if (clockwise) { tris.push_back(order[0]); tris.push_back(order[2]); tris.push_back(order[1]); }
      else           { tris.push_back(order[0]); tris.push_back(order[1]); tris.push_back(order[2]); }
      return tris;
   }

   // A grid of positions: `rows` along the path (or round the axis) by `cols` along the
   // profile. Either direction can wrap, in which case the first row/column is reused to
   // close it instead of being repeated in the grid.
   struct Grid
   {
      int rows = 0, cols = 0;
      bool rowWrap = false, colWrap = false;
      std::vector<V3> p;
      V3& At(int i, int k) { return p[(size_t)i * (size_t)cols + (size_t)k]; }
      const V3& At(int i, int k) const { return p[(size_t)i * (size_t)cols + (size_t)k]; }
   };

   Mesh BuildMesh(const Grid& g, bool caps)
   {
      Mesh mesh;
      const int R = g.rows, C = g.cols;
      if (R < 2 || C < 2)
         return mesh;

      auto rowNeighbour = [&](int i, int d) {
         int j = i + d;
         if (g.rowWrap) return ((j % R) + R) % R;
         return std::max(0, std::min(R - 1, j));
      };
      auto colNeighbour = [&](int k, int d) {
         int j = k + d;
         if (g.colWrap) return ((j % C) + C) % C;
         return std::max(0, std::min(C - 1, j));
      };

      // Normals from the position grid itself: the cross of the two central differences is
      // the same orientation as the faces below, so the vertex normals agree with the
      // winding by construction.
      std::vector<V3> normal((size_t)R * (size_t)C);
      std::vector<unsigned char> state((size_t)R * (size_t)C, 0); // 0 ok, 1 collapsed along a row, 2 along a column
      for (int i = 0; i < R; i++)
      {
         for (int k = 0; k < C; k++)
         {
            const V3 dr = g.At(rowNeighbour(i, 1), k) - g.At(rowNeighbour(i, -1), k);
            const V3 dc = g.At(i, colNeighbour(k, 1)) - g.At(i, colNeighbour(k, -1));
            const V3 n = Cross(dr, dc);
            const size_t at = (size_t)i * (size_t)C + (size_t)k;
            if (Length(n) > 1e-12)
               normal[at] = n * (1.0 / Length(n));
            else
               state[at] = Length(dr) <= 1e-12 ? 1 : 2;
         }
      }
      // A collapsed point (the pole of a lathe, a swept ring scaled to nothing) has no
      // direction of its own: borrow the average of the line of points beside it, which
      // for a pole cancels the sideways part and leaves the axis.
      for (int i = 0; i < R; i++)
      {
         for (int k = 0; k < C; k++)
         {
            const size_t at = (size_t)i * (size_t)C + (size_t)k;
            if (state[at] == 0)
               continue;
            V3 sum;
            if (state[at] == 1)
            {
               for (int d = -1; d <= 1; d += 2)
               {
                  const int kk = colNeighbour(k, d);
                  for (int ii = 0; ii < R; ii++)
                     if (state[(size_t)ii * (size_t)C + (size_t)kk] == 0)
                        sum = sum + normal[(size_t)ii * (size_t)C + (size_t)kk];
               }
            }
            else
            {
               for (int d = -1; d <= 1; d += 2)
               {
                  const int ii = rowNeighbour(i, d);
                  for (int kk = 0; kk < C; kk++)
                     if (state[(size_t)ii * (size_t)C + (size_t)kk] == 0)
                        sum = sum + normal[(size_t)ii * (size_t)C + (size_t)kk];
               }
            }
            normal[at] = Normalized(sum, V3{ 0.0, 1.0, 0.0 });
         }
      }

      const int outRows = R + (g.rowWrap ? 1 : 0);
      const int outCols = C + (g.colWrap ? 1 : 0);
      mesh.vertices.reserve((size_t)outRows * (size_t)outCols);
      for (int i = 0; i < outRows; i++)
      {
         for (int k = 0; k < outCols; k++)
         {
            const size_t at = (size_t)(i % R) * (size_t)C + (size_t)(k % C);
            Vertex v;
            v.px = (float)g.p[at].x; v.py = (float)g.p[at].y; v.pz = (float)g.p[at].z;
            v.nx = (float)normal[at].x; v.ny = (float)normal[at].y; v.nz = (float)normal[at].z;
            v.u = (float)i / (float)(outRows - 1);
            v.v = (float)k / (float)(outCols - 1);
            mesh.vertices.push_back(v);
         }
      }

      // A triangle is kept only if it has area. Where a profile touches the axis two
      // corners of a quad coincide, and the zero-area half would be a degenerate face.
      auto pushTriangle = [&](unsigned int a, unsigned int b, unsigned int c) {
         const Vertex &A = mesh.vertices[a], &B = mesh.vertices[b], &Cc = mesh.vertices[c];
         const V3 e1 = { (double)B.px - A.px, (double)B.py - A.py, (double)B.pz - A.pz };
         const V3 e2 = { (double)Cc.px - A.px, (double)Cc.py - A.py, (double)Cc.pz - A.pz };
         if (!(Length(Cross(e1, e2)) > 2e-10))
            return;
         mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c);
      };
      for (int i = 0; i + 1 < outRows; i++)
      {
         for (int k = 0; k + 1 < outCols; k++)
         {
            const unsigned int a = (unsigned int)(i * outCols + k);
            const unsigned int b = (unsigned int)((i + 1) * outCols + k);
            const unsigned int c = (unsigned int)((i + 1) * outCols + k + 1);
            const unsigned int d = (unsigned int)(i * outCols + k + 1);
            pushTriangle(a, b, c);
            pushTriangle(a, c, d);
         }
      }

      // Flat caps for a closed profile on an open run. The side surface leaves row 0 going
      // down the columns and row R-1 going up them, so the first cap follows the profile
      // order and the last one runs against it.
      bool closedSolid = g.rowWrap && g.colWrap;
      if (caps && !g.rowWrap && g.colWrap)
      {
         for (int end = 0; end < 2; end++)
         {
            const int row = end == 0 ? 0 : R - 1;
            std::vector<V3> ring((size_t)C);
            for (int k = 0; k < C; k++)
               ring[(size_t)k] = g.At(row, k);
            std::vector<float> uv;
            const std::vector<int> tris = TriangulatePolygon(ring, uv);
            if (tris.empty())
               continue;
            V3 sum;
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
            {
               const V3 &a = ring[(size_t)tris[t]], &b = ring[(size_t)tris[t + 1]], &c = ring[(size_t)tris[t + 2]];
               sum = sum + (end == 0 ? Cross(b - a, c - a) : Cross(c - a, b - a));
            }
            const V3 n = Normalized(sum, V3{ 0.0, 1.0, 0.0 });
            const unsigned int base = (unsigned int)mesh.vertices.size();
            for (int k = 0; k < C; k++)
            {
               Vertex v;
               v.px = (float)ring[(size_t)k].x; v.py = (float)ring[(size_t)k].y; v.pz = (float)ring[(size_t)k].z;
               v.nx = (float)n.x; v.ny = (float)n.y; v.nz = (float)n.z;
               v.u = uv[(size_t)k * 2]; v.v = uv[(size_t)k * 2 + 1];
               mesh.vertices.push_back(v);
            }
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
            {
               if (end == 0)
                  pushTriangle(base + (unsigned)tris[t], base + (unsigned)tris[t + 1], base + (unsigned)tris[t + 2]);
               else
                  pushTriangle(base + (unsigned)tris[t], base + (unsigned)tris[t + 2], base + (unsigned)tris[t + 1]);
            }
            closedSolid = true;
         }
      }

      // A closed result faces outward: if the signed volume came out negative the whole
      // thing is wound inside out, so flip the faces and the normals together.
      if (closedSolid)
      {
         double volume = 0.0;
         for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
         {
            const Vertex &A = mesh.vertices[mesh.indices[t]], &B = mesh.vertices[mesh.indices[t + 1]],
                         &Cc = mesh.vertices[mesh.indices[t + 2]];
            volume += (double)A.px * ((double)B.py * Cc.pz - (double)B.pz * Cc.py)
                    - (double)A.py * ((double)B.px * Cc.pz - (double)B.pz * Cc.px)
                    + (double)A.pz * ((double)B.px * Cc.py - (double)B.py * Cc.px);
         }
         if (volume < 0.0)
         {
            for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
               std::swap(mesh.indices[t + 1], mesh.indices[t + 2]);
            for (Vertex& v : mesh.vertices)
            {
               v.nx = -v.nx; v.ny = -v.ny; v.nz = -v.nz;
            }
         }
      }
      return mesh;
   }

   V3 RotateAbout(const V3& p, int axis, double angle)
   {
      const double s = std::sin(angle), c = std::cos(angle);
      if (axis == 1) return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
      if (axis == 0) return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
      return { p.x * c - p.y * s, p.x * s + p.y * c, p.z };
   }

   V3 RotateFrame(const V3& n, const V3& b, double angle, bool second)
   {
      const double c = std::cos(angle), s = std::sin(angle);
      return second ? n * (-s) + b * c : n * c + b * s;
   }
}

namespace MeshOps
{
   Mesh Revolve(const Polyline& profile, int axis, float angleDegrees, int segments, bool caps)
   {
      // A profile that was meant to touch the axis rarely lands on it exactly (a semicircle
      // computed with cos(pi/2) misses by a few 1e-8). Snap those points onto the axis so
      // the pole closes cleanly instead of leaving a ring of slivers.
      Polyline snapped = profile;
      const int snapAxis = std::max(0, std::min(axis, 2));
      for (size_t i = 0; i + 2 < snapped.points.size(); i += 3)
      {
         const int u = (snapAxis + 1) % 3, w = (snapAxis + 2) % 3;
         if (std::fabs(snapped.points[i + (size_t)u]) < 1e-6f && std::fabs(snapped.points[i + (size_t)w]) < 1e-6f)
            snapped.points[i + (size_t)u] = snapped.points[i + (size_t)w] = 0.0f;
      }
      bool closed = false;
      const std::vector<V3> pts = CleanPoints(snapped, closed);
      if (pts.size() < 2 || !std::isfinite(angleDegrees))
         return Mesh();

      const double degrees = std::max(-360.0, std::min(360.0, (double)angleDegrees));
      if (std::fabs(degrees) < 1e-3)
         return Mesh();
      const bool full = std::fabs(degrees) >= 359.999;
      const double angle = (full ? (degrees < 0 ? -360.0 : 360.0) : degrees) * kTwoPi / 360.0;
      const int steps = std::max(full ? 3 : 1, std::min(segments, 512));
      const int a = std::max(0, std::min(axis, 2));

      Grid g;
      g.cols = (int)pts.size();
      g.rows = full ? steps : steps + 1;
      g.rowWrap = full;
      g.colWrap = closed;
      g.p.resize((size_t)g.rows * (size_t)g.cols);
      for (int i = 0; i < g.rows; i++)
         for (int k = 0; k < g.cols; k++)
            g.At(i, k) = RotateAbout(pts[(size_t)k], a, angle * (double)i / (double)steps);
      return BuildMesh(g, caps);
   }

   Mesh Sweep(const Polyline& profile, const Polyline& path, float startScale, float endScale,
              float twistDegrees, bool caps)
   {
      bool profileClosed = false, pathClosed = false;
      const std::vector<V3> prof = CleanPoints(profile, profileClosed);
      const std::vector<V3> way = CleanPoints(path, pathClosed);
      if (prof.size() < 2 || way.size() < 2 || !std::isfinite(startScale) || !std::isfinite(endScale) ||
          !std::isfinite(twistDegrees))
         return Mesh();

      const int R = (int)way.size(), C = (int)prof.size();

      // The profile's own plane: drop the axis it faces most and keep the other two as its
      // local x and y, so a profile drawn flat in any world plane sweeps the same way.
      V3 newell;
      for (int i = 0; i < C; i++)
      {
         const V3& p = prof[(size_t)i];
         const V3& q = prof[(size_t)((i + 1) % C)];
         newell.x += (p.y - q.y) * (p.z + q.z);
         newell.y += (p.z - q.z) * (p.x + q.x);
         newell.z += (p.x - q.x) * (p.y + q.y);
      }
      const double nx = std::fabs(newell.x), ny = std::fabs(newell.y), nz = std::fabs(newell.z);
      int drop = 2;
      if (nx >= ny && nx >= nz && nx > 1e-12) drop = 0;
      else if (ny >= nx && ny >= nz && ny > 1e-12) drop = 1;
      std::vector<double> lx((size_t)C), ly((size_t)C);
      for (int k = 0; k < C; k++)
      {
         const V3& p = prof[(size_t)k];
         if (drop == 0)      { lx[(size_t)k] = p.y; ly[(size_t)k] = p.z; }
         else if (drop == 1) { lx[(size_t)k] = p.z; ly[(size_t)k] = p.x; }
         else                { lx[(size_t)k] = p.x; ly[(size_t)k] = p.y; }
      }

      // Tangents, then a frame carried along the path (parallel transport) so the profile
      // does not spin where the path's curvature changes sign.
      std::vector<V3> tangent((size_t)R), n((size_t)R);
      for (int i = 0; i < R; i++)
      {
         const int next = pathClosed ? (i + 1) % R : std::min(i + 1, R - 1);
         const int prev = pathClosed ? (i + R - 1) % R : std::max(i - 1, 0);
         tangent[(size_t)i] = Normalized(way[(size_t)next] - way[(size_t)prev], V3{ 0.0, 0.0, 1.0 });
      }
      auto perpendicularTo = [](const V3& t) {
         const V3 hint = std::fabs(t.y) > 0.9 ? V3{ 1.0, 0.0, 0.0 } : V3{ 0.0, 1.0, 0.0 };
         return Normalized(hint - t * Dot(hint, t), V3{ 1.0, 0.0, 0.0 });
      };
      n[0] = perpendicularTo(tangent[0]);
      for (int i = 1; i < R; i++)
      {
         const V3& t = tangent[(size_t)i];
         const V3 carried = n[(size_t)i - 1] - t * Dot(n[(size_t)i - 1], t);
         n[(size_t)i] = Length(carried) > 1e-6 ? Normalized(carried, perpendicularTo(t)) : perpendicularTo(t);
      }

      // A closed path brings the carried frame back turned by some angle; spread that
      // angle over the loop so the last ring meets the first one.
      double holonomy = 0.0;
      if (pathClosed)
      {
         const V3& t0 = tangent[0];
         const V3 last = n[(size_t)R - 1] - t0 * Dot(n[(size_t)R - 1], t0);
         const V3 end = Normalized(last, n[0]);
         holonomy = std::atan2(Dot(Cross(n[0], end), t0), Dot(n[0], end));
      }

      double twist = (double)twistDegrees * kTwoPi / 360.0;
      if (pathClosed)
         twist = std::floor(twist / kTwoPi + 0.5) * kTwoPi; // a seam can only close on whole turns

      std::vector<double> along((size_t)R, 0.0);
      for (int i = 1; i < R; i++)
         along[(size_t)i] = along[(size_t)i - 1] + Length(way[(size_t)i] - way[(size_t)i - 1]);
      const double total = std::max(along[(size_t)R - 1], 1e-12);

      Grid g;
      g.rows = R;
      g.cols = C;
      g.rowWrap = pathClosed;
      g.colWrap = profileClosed;
      g.p.resize((size_t)R * (size_t)C);
      for (int i = 0; i < R; i++)
      {
         const double f = pathClosed ? (double)i / (double)R : (double)i / (double)(R - 1);
         const double scale = pathClosed ? (double)startScale
                                         : (double)startScale + ((double)endScale - (double)startScale) * (along[(size_t)i] / total);
         const double turn = twist * f - holonomy * f;
         const V3& t = tangent[(size_t)i];
         const V3 b0 = Cross(t, n[(size_t)i]);
         const V3 nn = RotateFrame(n[(size_t)i], b0, turn, false);
         const V3 bb = RotateFrame(n[(size_t)i], b0, turn, true);
         for (int k = 0; k < C; k++)
            g.At(i, k) = way[(size_t)i] + (nn * lx[(size_t)k] + bb * ly[(size_t)k]) * scale;
      }
      return BuildMesh(g, caps);
   }
}

#include "EditMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace EditMesh
{
std::vector<int> PointIds(const Mesh& m, int* count);

namespace
{
   using Key = std::tuple<long long, long long, long long>;

   Key KeyOf(const Vertex& v)
   {
      // Welded by position: a cube's split corners are one point.
      return Key(std::llround(v.px * 1e5), std::llround(v.py * 1e5), std::llround(v.pz * 1e5));
   }

   void FaceData(const Mesh& m, size_t f, float c[3], float n[3], float* area = nullptr)
   {
      const Vertex& a = m.vertices[m.indices[f * 3]];
      const Vertex& b = m.vertices[m.indices[f * 3 + 1]];
      const Vertex& d = m.vertices[m.indices[f * 3 + 2]];
      c[0] = (a.px + b.px + d.px) / 3.0f;
      c[1] = (a.py + b.py + d.py) / 3.0f;
      c[2] = (a.pz + b.pz + d.pz) / 3.0f;
      const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
      const float vx = d.px - a.px, vy = d.py - a.py, vz = d.pz - a.pz;
      n[0] = uy * vz - uz * vy;
      n[1] = uz * vx - ux * vz;
      n[2] = ux * vy - uy * vx;
      const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (area)
         *area = len * 0.5f;
      if (len > 1e-12f)
         for (int k = 0; k < 3; k++)
            n[k] /= len;
   }

   // Normals through the mesh's own indices: shared vertices smooth, split
   // ones (a cube's corners, an extrude's walls) stay hard, as in Blender.
   // MeshOps::RecalculateNormals welds first and would round every edge.
   void IndexNormals(Mesh& m)
   {
      std::vector<float> acc(m.vertices.size() * 3, 0.0f);
      for (size_t f = 0; f < m.FaceCount(); f++)
      {
         float c[3], n[3], area;
         FaceData(m, f, c, n, &area);
         for (int k = 0; k < 3; k++)
            for (int j = 0; j < 3; j++)
               acc[m.indices[f * 3 + k] * 3 + j] += n[j] * area;
      }
      for (size_t i = 0; i < m.vertices.size(); i++)
      {
         const float* a = &acc[i * 3];
         const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
         if (len < 1e-12f)
            continue;
         m.vertices[i].nx = a[0] / len;
         m.vertices[i].ny = a[1] / len;
         m.vertices[i].nz = a[2] / len;
      }
   }

   float Diagonal(const Mesh& m)
   {
      if (m.vertices.empty())
         return 1.0f;
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      for (const Vertex& v : m.vertices)
      {
         const float p[3] = { v.px, v.py, v.pz };
         for (int k = 0; k < 3; k++)
         {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
         }
      }
      const float d = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                (hi[2] - lo[2]) * (hi[2] - lo[2]));
      return std::max(d, 1e-6f);
   }

   std::string Fmt(const float* x, int n)
   {
      std::string s;
      char buf[32];
      for (int i = 0; i < n; i++)
      {
         snprintf(buf, sizeof(buf), " %.5g", std::fabs(x[i]) < 1e-6f ? 0.0f : x[i]);
         s += buf;
      }
      return s;
   }

   // The nearest vertex to p within tol, or -1.
   int NearestVertex(const Mesh& m, const float p[3], float tol)
   {
      int best = -1;
      float bestD = tol * tol;
      for (size_t i = 0; i < m.vertices.size(); i++)
      {
         const Vertex& v = m.vertices[i];
         const float d = (v.px - p[0]) * (v.px - p[0]) + (v.py - p[1]) * (v.py - p[1]) + (v.pz - p[2]) * (v.pz - p[2]);
         if (d <= bestD)
         {
            bestD = d;
            best = (int)i;
         }
      }
      return best;
   }

   // Every vertex of a selected point, so split corners move together.
   void WeldExpand(const Mesh& m, Selection& s)
   {
      const std::vector<int> ids = PointIds(m);
      std::set<int> on;
      for (size_t i = 0; i < s.point.size(); i++)
         if (s.point[i])
            on.insert(ids[i]);
      for (size_t i = 0; i < s.point.size(); i++)
         if (on.count(ids[i]))
            s.point[i] = 1;
   }

   Selection FromFaceMask(const Mesh& m)
   {
      Selection s;
      s.point.assign(m.vertices.size(), 0);
      s.face.assign(m.FaceCount(), 0);
      if (m.faceMask.empty())
         return s;
      for (size_t f = 0; f < s.face.size(); f++)
         if (m.FaceSelected(f))
         {
            s.face[f] = 1;
            for (int k = 0; k < 3; k++)
               s.point[m.indices[f * 3 + k]] = 1;
         }
      WeldExpand(m, s);
      return s;
   }

   bool Any(const std::vector<unsigned char>& v)
   {
      return std::find(v.begin(), v.end(), 1) != v.end();
   }

   void Median(const Mesh& m, const Selection& s, float c[3])
   {
      // Each point counts once, however many vertices share it.
      const std::vector<int> ids = PointIds(m);
      std::set<int> seen;
      double sum[3] = { 0, 0, 0 };
      int n = 0;
      for (size_t i = 0; i < s.point.size(); i++)
         if (s.point[i] && seen.insert(ids[i]).second)
         {
            sum[0] += m.vertices[i].px;
            sum[1] += m.vertices[i].py;
            sum[2] += m.vertices[i].pz;
            n++;
         }
      for (int k = 0; k < 3; k++)
         c[k] = n ? (float)(sum[k] / n) : 0.0f;
   }
}

std::vector<int> PointIds(const Mesh& m, int* count)
{
   std::map<Key, int> ids;
   std::vector<int> out(m.vertices.size());
   for (size_t i = 0; i < m.vertices.size(); i++)
      out[i] = ids.emplace(KeyOf(m.vertices[i]), (int)ids.size()).first->second;
   if (count)
      *count = (int)ids.size();
   return out;
}

std::string FaceSelector(const Mesh& m, size_t face)
{
   float c[3], n[3];
   FaceData(m, face, c, n);
   return Fmt(c, 3) + Fmt(n, 3);
}

std::string PointSelector(const Mesh& m, unsigned int vertex)
{
   const Vertex& v = m.vertices[vertex];
   const float p[3] = { v.px, v.py, v.pz };
   return Fmt(p, 3);
}

std::vector<size_t> FlatPatch(const Mesh& m, size_t face, float angleDeg)
{
   std::vector<size_t> out;
   const size_t faces = m.FaceCount();
   if (face >= faces)
      return out;
   const std::vector<int> ids = PointIds(m);
   std::map<std::pair<int, int>, std::vector<size_t>> edges;
   for (size_t f = 0; f < faces; f++)
      for (int k = 0; k < 3; k++)
      {
         int a = ids[m.indices[f * 3 + k]], b = ids[m.indices[f * 3 + (k + 1) % 3]];
         edges[{ std::min(a, b), std::max(a, b) }].push_back(f);
      }
   const float cosLimit = std::cos(angleDeg * 3.14159265f / 180.0f);
   float c0[3], n0[3];
   FaceData(m, face, c0, n0);
   std::vector<unsigned char> seen(faces, 0);
   std::vector<size_t> todo = { face };
   seen[face] = 1;
   while (!todo.empty())
   {
      const size_t f = todo.back();
      todo.pop_back();
      out.push_back(f);
      for (int k = 0; k < 3; k++)
      {
         int a = ids[m.indices[f * 3 + k]], b = ids[m.indices[f * 3 + (k + 1) % 3]];
         for (size_t g : edges[{ std::min(a, b), std::max(a, b) }])
         {
            if (seen[g])
               continue;
            float c[3], n[3];
            FaceData(m, g, c, n);
            if (n[0] * n0[0] + n[1] * n0[1] + n[2] * n0[2] < cosLimit)
               continue;
            seen[g] = 1;
            todo.push_back(g);
         }
      }
   }
   std::sort(out.begin(), out.end());
   return out;
}

Selection Select(const Mesh& m, const std::string& selLine, int* missed)
{
   Selection s;
   s.point.assign(m.vertices.size(), 0);
   s.face.assign(m.FaceCount(), 0);
   std::istringstream in(selLine);
   std::string word, mode;
   in >> word >> mode;
   if (mode == "all")
   {
      std::fill(s.point.begin(), s.point.end(), 1);
      std::fill(s.face.begin(), s.face.end(), 1);
      return s;
   }
   std::vector<float> nums;
   float x;
   while (in >> x)
      nums.push_back(x);
   // Near enough to follow an upstream change that keeps the shape in place.
   const float tol = Diagonal(m) * 0.05f;
   int miss = 0;
   if (mode == "f")
   {
      for (size_t i = 0; i + 5 < nums.size(); i += 6)
      {
         int best = -1;
         float bestD = tol * tol;
         for (size_t f = 0; f < s.face.size(); f++)
         {
            float c[3], n[3];
            FaceData(m, f, c, n);
            if (n[0] * nums[i + 3] + n[1] * nums[i + 4] + n[2] * nums[i + 5] < 0.9f)
               continue;
            const float d = (c[0] - nums[i]) * (c[0] - nums[i]) + (c[1] - nums[i + 1]) * (c[1] - nums[i + 1]) +
                            (c[2] - nums[i + 2]) * (c[2] - nums[i + 2]);
            if (d <= bestD)
            {
               bestD = d;
               best = (int)f;
            }
         }
         if (best < 0)
         {
            miss++;
            continue;
         }
         s.face[best] = 1;
         for (int k = 0; k < 3; k++)
            s.point[m.indices[best * 3 + k]] = 1;
      }
      WeldExpand(m, s);
   }
   else if (mode == "v" || mode == "e")
   {
      const size_t group = mode == "v" ? 3 : 6;
      for (size_t i = 0; i + group - 1 < nums.size(); i += group)
         for (size_t j = 0; j < group; j += 3)
         {
            const int v = NearestVertex(m, &nums[i + j], tol);
            if (v < 0)
               miss++;
            else
               s.point[v] = 1;
         }
      WeldExpand(m, s);
      // A face is selected when all its points are, as in Blender.
      for (size_t f = 0; f < s.face.size(); f++)
         s.face[f] = s.point[m.indices[f * 3]] && s.point[m.indices[f * 3 + 1]] && s.point[m.indices[f * 3 + 2]];
   }
   if (missed)
      *missed += miss;
   return s;
}

Mesh ExtrudeRegion(const Mesh& in, float distance)
{
   const size_t faces = in.FaceCount();
   float N[3] = { 0, 0, 0 };
   for (size_t f = 0; f < faces; f++)
      if (in.FaceSelected(f))
      {
         float c[3], n[3], area;
         FaceData(in, f, c, n, &area);
         for (int k = 0; k < 3; k++)
            N[k] += n[k] * area;
      }
   const float len = std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
   if (len < 1e-12f)
      return in;
   for (int k = 0; k < 3; k++)
      N[k] = N[k] / len * distance;

   const bool colours = in.HasVertexColor();
   Mesh out;
   out.vertices = in.vertices;
   if (colours)
      out.vertexColor = in.vertexColor;
   auto colourOf = [&](unsigned int v) {
      if (colours)
         for (int k = 0; k < 3; k++)
            out.vertexColor.push_back(in.vertexColor[v * 3 + k]);
   };
   auto lifted = [&](const Vertex& v) {
      Vertex w = v;
      w.px += N[0];
      w.py += N[1];
      w.pz += N[2];
      return w;
   };

   for (size_t f = 0; f < faces; f++)
      if (!in.FaceSelected(f))
         for (int k = 0; k < 3; k++)
            out.indices.push_back(in.indices[f * 3 + k]);
   const size_t kept = out.FaceCount();

   // The cap: a lifted copy of every vertex the region uses.
   std::unordered_map<unsigned int, unsigned int> cap;
   for (size_t f = 0; f < faces; f++)
      if (in.FaceSelected(f))
         for (int k = 0; k < 3; k++)
         {
            const unsigned int v = in.indices[f * 3 + k];
            auto it = cap.find(v);
            if (it == cap.end())
            {
               it = cap.emplace(v, (unsigned int)out.vertices.size()).first;
               out.vertices.push_back(lifted(in.vertices[v]));
               colourOf(v);
            }
            out.indices.push_back(it->second);
         }
   const size_t capEnd = out.FaceCount();

   // The outline: directed edges whose reverse no selected face has.
   const std::vector<int> ids = PointIds(in);
   std::set<std::pair<int, int>> directed;
   for (size_t f = 0; f < faces; f++)
      if (in.FaceSelected(f))
         for (int k = 0; k < 3; k++)
            directed.insert({ ids[in.indices[f * 3 + k]], ids[in.indices[f * 3 + (k + 1) % 3]] });
   for (size_t f = 0; f < faces; f++)
   {
      if (!in.FaceSelected(f))
         continue;
      for (int k = 0; k < 3; k++)
      {
         const unsigned int a = in.indices[f * 3 + k], b = in.indices[f * 3 + (k + 1) % 3];
         if (directed.count({ ids[b], ids[a] }))
            continue;
         // Its own four vertices, so the wall shades flat against the cap.
         const unsigned int base = (unsigned int)out.vertices.size();
         Vertex wa = in.vertices[a], wb = in.vertices[b];
         wa.u = 0; wa.v = 0;
         wb.u = 1; wb.v = 0;
         Vertex ta = lifted(wa), tb = lifted(wb);
         ta.v = 1;
         tb.v = 1;
         out.vertices.push_back(wa);
         out.vertices.push_back(wb);
         out.vertices.push_back(tb);
         out.vertices.push_back(ta);
         colourOf(a);
         colourOf(b);
         colourOf(b);
         colourOf(a);
         const unsigned int tri[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
         out.indices.insert(out.indices.end(), tri, tri + 6);
      }
   }
   out.faceMask.assign(out.FaceCount(), 0);
   for (size_t f = kept; f < capEnd; f++)
      out.faceMask[f] = 1;
   IndexNormals(out);
   return out;
}

Mesh Apply(const Mesh& in, const std::string& ops, int* missed, Selection* selection)
{
   Mesh cur = in;
   if (ops.empty())
      return cur;
   Selection sel;
   sel.point.assign(cur.vertices.size(), 0);
   sel.face.assign(cur.FaceCount(), 0);
   bool moved = false;
   std::istringstream lines(ops);
   std::string line;
   while (std::getline(lines, line))
   {
      std::istringstream in(line);
      std::string op;
      if (!(in >> op))
         continue;
      if (op == "sel")
      {
         sel = Select(cur, line, missed);
         continue;
      }
      float a[3] = { 0, 0, 0 };
      in >> a[0] >> a[1] >> a[2];
      if (op == "move" || op == "rotate" || op == "scale")
      {
         if (!Any(sel.point))
            continue;
         float c[3];
         Median(cur, sel, c);
         float deg = 0.0f;
         in >> deg;
         // Rodrigues: the turn about the unit axis.
         float ax[3] = { a[0], a[1], a[2] };
         const float alen = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
         if (op == "rotate" && alen < 1e-9f)
            continue;
         for (int k = 0; k < 3 && op == "rotate"; k++)
            ax[k] /= alen;
         const float th = deg * 3.14159265f / 180.0f, ct = std::cos(th), st = std::sin(th);
         for (size_t i = 0; i < cur.vertices.size(); i++)
         {
            if (!sel.point[i])
               continue;
            Vertex& v = cur.vertices[i];
            if (op == "move")
            {
               v.px += a[0];
               v.py += a[1];
               v.pz += a[2];
               continue;
            }
            float p[3] = { v.px - c[0], v.py - c[1], v.pz - c[2] };
            if (op == "scale")
               for (int k = 0; k < 3; k++)
                  p[k] *= a[k];
            else
            {
               const float d = ax[0] * p[0] + ax[1] * p[1] + ax[2] * p[2];
               const float x[3] = { ax[1] * p[2] - ax[2] * p[1], ax[2] * p[0] - ax[0] * p[2], ax[0] * p[1] - ax[1] * p[0] };
               for (int k = 0; k < 3; k++)
                  p[k] = p[k] * ct + x[k] * st + ax[k] * d * (1.0f - ct);
            }
            v.px = p[0] + c[0];
            v.py = p[1] + c[1];
            v.pz = p[2] + c[2];
         }
         moved = true;
         continue;
      }
      if (op != "extrude" && op != "inset" && op != "delete")
         continue;
      if (!Any(sel.face))
         continue;
      if (moved)
         IndexNormals(cur);
      cur.faceMask = sel.face;
      if (op == "extrude")
         cur = ExtrudeRegion(cur, a[0]);
      else if (op == "inset")
      {
         cur = MeshOps::InsetFaces(cur, a[0], 0.0f, 30.0f);
         IndexNormals(cur);
      }
      else
      {
         cur = MeshOps::DeleteSelected(cur, false);
         cur.faceMask.clear();
      }
      sel = FromFaceMask(cur);
      moved = false;
   }
   if (moved)
      IndexNormals(cur);
   cur.faceMask.clear();
   if (selection)
      *selection = std::move(sel);
   return cur;
}
}

#include "DelaunayNodes.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "Delaunay.h"

namespace
{
   const Mesh kEmptyMesh;

   struct Site { float x, y, z; };

   // Plane axes: u,v are the projected coordinates, chosen so (u, v, normal)
   // is right-handed and counter-clockwise triangles face along `normal`.
   void PlaneAxes(int plane, int& iu, int& iv, float n[3])
   {
      n[0] = n[1] = n[2] = 0.0f;
      if (plane == 1)      { iu = 2; iv = 0; n[1] = 1.0f; }
      else if (plane == 2) { iu = 1; iv = 2; n[0] = 1.0f; }
      else                 { iu = 0; iv = 1; n[2] = 1.0f; }
   }
}

const std::vector<std::string>& DelaunayMeshNode::PlaneNames()
{
   static const std::vector<std::string> names = { "XY", "XZ", "YZ" };
   return names;
}

void DelaunayMeshNode::RebuildIfNeeded()
{
   if (input == nullptr)
   {
      if (!mCache.vertices.empty())
      {
         mCache = Mesh();
         mMeshRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      mCookWarning.clear();
      return;
   }

   // Points only: a mesh on this pin is reported (CookWarning) and produces
   // nothing, rather than silently triangulating its vertices.
   const std::vector<Particle>* cloud = input->GetPointCloud();
   mCookWarning = DescribeGeometryMismatch(input, GeometryRequirement::kCloud);
   const unsigned long long upstream = cloud ? input->PointCloudRevision() : 0;
   if (mBuiltInput == input && mBuiltUpstream == upstream && mBuiltPlane == plane &&
       mBuiltAliveOnly == aliveOnly && mBuiltMaxPoints == maxPoints && mBuiltInset == inset)
      return;

   std::vector<Site> sites;
   if (cloud != nullptr)
   {
      sites.reserve(cloud->size());
      for (const Particle& p : *cloud)
      {
         if (aliveOnly && !p.alive) continue;
         sites.push_back({ p.px, p.py, p.pz });
      }
   }
   const size_t cap = (size_t)std::max(3, maxPoints);
   if (sites.size() > cap)
   {
      std::vector<Site> thin;
      thin.reserve(cap);
      for (size_t i = 0; i < cap; ++i)
         thin.push_back(sites[i * sites.size() / cap]);
      sites.swap(thin);
   }

   int iu = 0, iv = 1;
   float nrm[3];
   PlaneAxes(plane, iu, iv, nrm);
   auto coord = [&](const Site& s, int axis) { return axis == 0 ? s.x : axis == 1 ? s.y : s.z; };

   std::vector<double> xy;
   xy.reserve(sites.size() * 2);
   float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
   for (const Site& s : sites)
   {
      const float u = coord(s, iu), v = coord(s, iv);
      xy.push_back(u); xy.push_back(v);
      u0 = std::min(u0, u); u1 = std::max(u1, u);
      v0 = std::min(v0, v); v1 = std::max(v1, v);
   }
   const float du = std::max(u1 - u0, 1e-6f), dv = std::max(v1 - v0, 1e-6f);

   const std::vector<unsigned int> tri = Delaunay::Triangulate(xy);

   Mesh out;
   auto addVertex = [&](float x, float y, float z, float u, float v)
   {
      Vertex w;
      w.px = x; w.py = y; w.pz = z;
      w.nx = nrm[0]; w.ny = nrm[1]; w.nz = nrm[2];
      w.u = (u - u0) / du; w.v = (v - v0) / dv;
      out.vertices.push_back(w);
      return (unsigned)(out.vertices.size() - 1);
   };

   if (!IsVoronoi())
   {
      out.vertices.reserve(sites.size());
      for (const Site& s : sites)
         addVertex(s.x, s.y, s.z, coord(s, iu), coord(s, iv));
      out.indices = tri;
   }
   else
   {
      // Edge use counts identify hull vertices; per-vertex fans give the cells.
      std::map<std::pair<unsigned, unsigned>, int> edgeUse;
      std::vector<std::vector<size_t>> fan(sites.size());
      for (size_t t = 0; t + 2 < tri.size(); t += 3)
         for (int k = 0; k < 3; ++k)
         {
            const unsigned a = tri[t + k], b = tri[t + (k + 1) % 3];
            ++edgeUse[{ std::min(a, b), std::max(a, b) }];
            fan[a].push_back(t / 3);
         }
      std::vector<char> hull(sites.size(), 0);
      for (auto& e : edgeUse)
         if (e.second == 1) hull[e.first.first] = hull[e.first.second] = 1;

      auto circum = [&](size_t t, double& cx, double& cy)
      {
         const double ax = xy[tri[t * 3] * 2], ay = xy[tri[t * 3] * 2 + 1];
         const double bx = xy[tri[t * 3 + 1] * 2] - ax, by = xy[tri[t * 3 + 1] * 2 + 1] - ay;
         const double qx = xy[tri[t * 3 + 2] * 2] - ax, qy = xy[tri[t * 3 + 2] * 2 + 1] - ay;
         const double d = 2.0 * (bx * qy - by * qx);
         if (std::fabs(d) < 1e-300) { cx = ax; cy = ay; return; }
         const double b2 = bx * bx + by * by, q2 = qx * qx + qy * qy;
         cx = ax + (qy * b2 - by * q2) / d;
         cy = ay + (bx * q2 - qx * b2) / d;
      };

      const float pull = std::min(std::max(inset, 0.0f), 0.95f);
      const int other = 3 - iu - iv; // the unprojected axis
      for (size_t s = 0; s < sites.size(); ++s)
      {
         if (hull[s] || fan[s].size() < 3) continue;
         const double sx = xy[s * 2], sy = xy[s * 2 + 1];
         struct Ring { double ang, x, y; };
         std::vector<Ring> ring;
         for (size_t t : fan[s])
         {
            double cx, cy;
            circum(t, cx, cy);
            // Slivers beside the hull have circumcentres far outside the
            // cloud; cap each at twice the distance to the farthest site of
            // its triangle so one flat triangle cannot spike a cell.
            double reach = 0.0;
            for (int k = 0; k < 3; ++k)
            {
               const unsigned v = tri[t * 3 + k];
               reach = std::max(reach, std::hypot(xy[v * 2] - sx, xy[v * 2 + 1] - sy));
            }
            const double dist = std::hypot(cx - sx, cy - sy);
            if (dist > reach * 2.0 && dist > 0.0)
            {
               cx = sx + (cx - sx) * reach * 2.0 / dist;
               cy = sy + (cy - sy) * reach * 2.0 / dist;
            }
            ring.push_back({ std::atan2(cy - sy, cx - sx), cx, cy });
         }
         std::sort(ring.begin(), ring.end(), [](const Ring& a, const Ring& b) { return a.ang < b.ang; });

         const float h = coord(sites[s], other);
         auto lift = [&](double u, double v)
         {
            float p[3];
            p[iu] = (float)u; p[iv] = (float)v; p[other] = h;
            return addVertex(p[0], p[1], p[2], (float)u, (float)v);
         };
         const unsigned centre = lift(sx, sy);
         const unsigned first = (unsigned)out.vertices.size();
         for (const Ring& r : ring)
            lift(sx + (r.x - sx) * (1.0 - pull), sy + (r.y - sy) * (1.0 - pull));
         const unsigned count = (unsigned)ring.size();
         for (unsigned k = 0; k < count; ++k)
         {
            out.indices.push_back(centre);
            out.indices.push_back(first + k);
            out.indices.push_back(first + (k + 1) % count);
         }
      }
   }

   mCache = std::move(out);
   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltPlane = plane;
   mBuiltAliveOnly = aliveOnly;
   mBuiltMaxPoints = maxPoints;
   mBuiltInset = inset;
   mMeshRevision = NextMeshRevision();
}

const Mesh& DelaunayMeshNode::GetMesh()
{
   if (bypassed)
      return input ? input->GetMesh() : kEmptyMesh;
   RebuildIfNeeded();
   return mCache;
}

unsigned long long DelaunayMeshNode::MeshRevision()
{
   if (bypassed)
      return input ? input->MeshRevision() : 0;
   RebuildIfNeeded();
   return mMeshRevision;
}

unsigned long long DelaunayMeshNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

void DelaunayMeshNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

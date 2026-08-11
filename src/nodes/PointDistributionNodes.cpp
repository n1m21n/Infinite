#include "PointDistributionNodes.h"

#include <algorithm>
#include <cmath>

namespace
{
   // Same shape as MeshOps::Select's SelectHash / GeometryOpNodes' Rand01,
   // with a channel argument so a single grid cell can draw independent x/y
   // jitter without the two correlating. Same seed + same (index, channel)
   // always gives the same draw, which is what lets a reopened patch jitter
   // identically.
   float PointHash01(float seed, int index, int channel)
   {
      const float x = std::sin((seed + 1.0f) * ((float)(index + 1) * 12.9898f +
                                                  (float)(channel + 1) * 78.233f)) * 43758.5453f;
      return x - std::floor(x);
   }
}

// =============================================================== Distribute Points on Faces

const std::vector<std::string>& DistributePointsOnFacesNode::MethodNames()
{
   static const std::vector<std::string> names = { "Random", "Poisson Disk" };
   return names;
}

void DistributePointsOnFacesNode::RebuildIfNeeded()
{
   if (input == nullptr)
   {
      if (!mCache.vertices.empty() || !mPoints.empty())
      {
         mCache = Mesh();
         mPoints.clear();
         mMeshRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      return;
   }

   const unsigned long long upstream = input->MeshRevision();
   if (mBuiltInput == input && mBuiltUpstream == upstream && mBuiltDensity == density &&
       mBuiltMethod == method && mBuiltMinDistance == minDistance &&
       mBuiltPointSize == pointSize && mBuiltSeed == seed)
      return;

   const Mesh& src = input->GetMesh();
   const std::vector<MeshPoint> points =
      MeshOps::DistributeOnFaces(src, density, seed, method, minDistance);
   mCache = MeshOps::PointsToFaces(points, pointSize);

   float tint[3];
   if (inheritMaterial)
   {
      const Material m = input->GetMaterial();
      tint[0] = m.color[0]; tint[1] = m.color[1]; tint[2] = m.color[2];
   }
   else
   {
      tint[0] = color[0]; tint[1] = color[1]; tint[2] = color[2];
   }

   mPoints.clear();
   mPoints.reserve(points.size());
   for (const MeshPoint& p : points)
   {
      Particle particle;
      particle.px = p.px; particle.py = p.py; particle.pz = p.pz;
      particle.nx = p.nx; particle.ny = p.ny; particle.nz = p.nz;
      // Half-extent pointSize * 0.5 * p.scale, matching the quad
      // MeshOps::PointsToFaces just baked into mCache (h = size * 0.5).
      particle.scale = pointSize * 0.5f * p.scale;
      particle.r = tint[0] * p.r; particle.g = tint[1] * p.g; particle.b = tint[2] * p.b;
      mPoints.push_back(particle);
   }

   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltDensity = density;
   mBuiltMethod = method;
   mBuiltMinDistance = minDistance;
   mBuiltPointSize = pointSize;
   mBuiltSeed = seed;
   mMeshRevision = NextMeshRevision();
}

const Mesh& DistributePointsOnFacesNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long DistributePointsOnFacesNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

const std::vector<Particle>& DistributePointsOnFacesNode::GetPoints()
{
   RebuildIfNeeded();
   return mPoints;
}

unsigned long long DistributePointsOnFacesNode::PointRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Material DistributePointsOnFacesNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   m.ior = ior;
   m.transmission = transmission;
   m.transmissionRoughness = transmissionRoughness;
   m.specular = specular;
   m.clearcoat = clearcoat;
   m.clearcoatRoughness = clearcoatRoughness;
   m.subsurface = subsurface;
   m.subsurfaceColor[0] = subsurfaceColor[0];
   m.subsurfaceColor[1] = subsurfaceColor[1];
   m.subsurfaceColor[2] = subsurfaceColor[2];
   m.subsurfaceRadius = subsurfaceRadius;
   return m;
}

void DistributePointsOnFacesNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

// =============================================================== Points to Vertices

void PointsToVerticesNode::RebuildIfNeeded()
{
   if (input == nullptr)
   {
      if (!mCache.vertices.empty())
      {
         mCache = Mesh();
         mMeshRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      return;
   }

   // A cloud, when the upstream carries one, wins over its mesh - the same
   // "cloud is the more specific instruction" rule InstanceOnPointsNode uses.
   const std::vector<Particle>* cloud = input->GetPointCloud();
   const unsigned long long upstream = cloud ? input->PointCloudRevision() : input->MeshRevision();
   if (mBuiltInput == input && mBuiltUpstream == upstream && mBuiltAliveOnly == aliveOnly)
      return;

   Mesh out;
   if (cloud != nullptr)
   {
      out.vertices.reserve(cloud->size());
      out.vertexColor.reserve(cloud->size() * 3);
      for (const Particle& p : *cloud)
      {
         if (aliveOnly && !p.alive)
            continue;
         Vertex v;
         v.px = p.px; v.py = p.py; v.pz = p.pz;
         v.nx = p.nx; v.ny = p.ny; v.nz = p.nz;
         out.vertices.push_back(v);
         out.vertexColor.push_back(p.r);
         out.vertexColor.push_back(p.g);
         out.vertexColor.push_back(p.b);
      }
   }
   else
   {
      // No cloud upstream - fall back to the mesh's own vertices, so wiring a
      // plain mesh node into this input is a harmless pass-through of its
      // point positions rather than producing nothing.
      const Mesh& src = input->GetMesh();
      out.vertices = src.vertices;
      out.vertexColor = src.vertexColor;
   }

   mCache = out;
   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltAliveOnly = aliveOnly;
   mMeshRevision = NextMeshRevision();
}

const Mesh& PointsToVerticesNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long PointsToVerticesNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Material PointsToVerticesNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   m.ior = ior;
   m.transmission = transmission;
   m.transmissionRoughness = transmissionRoughness;
   m.specular = specular;
   m.clearcoat = clearcoat;
   m.clearcoatRoughness = clearcoatRoughness;
   m.subsurface = subsurface;
   m.subsurfaceColor[0] = subsurfaceColor[0];
   m.subsurfaceColor[1] = subsurfaceColor[1];
   m.subsurfaceColor[2] = subsurfaceColor[2];
   m.subsurfaceRadius = subsurfaceRadius;
   return m;
}

void PointsToVerticesNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

// =============================================================== Distribute Points in Grid

void DistributePointsInGridNode::RebuildIfNeeded()
{
   if (mBuiltCountX == countX && mBuiltCountY == countY && mBuiltSpacingX == spacingX &&
       mBuiltSpacingY == spacingY && mBuiltJitter == jitter && mBuiltPointSize == pointSize &&
       mBuiltSeed == seed)
      return;

   const int nx = std::max(1, countX);
   const int ny = std::max(1, countY);
   const float width = (float)nx * spacingX;
   const float height = (float)ny * spacingY;

   mPoints.clear();
   mPoints.reserve((size_t)nx * (size_t)ny);
   // Row-major (gy outer, gx inner) with cell-centre UV, matching
   // ImageToPointsNode's grid ordering exactly (see GenerativeNodes.cpp) so a
   // grid and an Image to Points of the same counts/spacing correspond
   // point-for-point.
   for (int gy = 0; gy < ny; gy++)
   {
      for (int gx = 0; gx < nx; gx++)
      {
         const int idx = gy * nx + gx;
         const float u = ((float)gx + 0.5f) / (float)nx;
         const float v = ((float)gy + 0.5f) / (float)ny;
         const float jx = (PointHash01(seed, idx, 0) - 0.5f) * jitter * spacingX;
         const float jy = (PointHash01(seed, idx, 1) - 0.5f) * jitter * spacingY;

         Particle p;
         p.px = (u - 0.5f) * width + jx;
         p.py = (v - 0.5f) * height + jy;
         p.pz = 0.0f;
         p.nx = 0.0f; p.ny = 0.0f; p.nz = 1.0f;
         // Render3D's cloud sprite path scales its unit quad by Particle::scale
         // directly (drawCloudSlot: s = p.scale * scaleX) - half pointSize,
         // matching the convention MeshToPointsNode/DistributePointsOnFacesNode
         // use, or every sprite renders at a fixed 1-unit size regardless of
         // this node's own pointSize param.
         p.scale = pointSize * 0.5f;
         p.r = tint[0]; p.g = tint[1]; p.b = tint[2];
         mPoints.push_back(p);
      }
   }

   std::vector<MeshPoint> asPoints;
   asPoints.reserve(mPoints.size());
   for (const Particle& p : mPoints)
      asPoints.push_back({ p.px, p.py, p.pz, p.nx, p.ny, p.nz, 1.0f, 0, p.r, p.g, p.b });
   mCache = MeshOps::PointsToFaces(asPoints, pointSize);

   mBuiltCountX = countX; mBuiltCountY = countY;
   mBuiltSpacingX = spacingX; mBuiltSpacingY = spacingY;
   mBuiltJitter = jitter; mBuiltPointSize = pointSize; mBuiltSeed = seed;
   mMeshRevision = NextMeshRevision();
}

const Mesh& DistributePointsInGridNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long DistributePointsInGridNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

const std::vector<Particle>& DistributePointsInGridNode::GetPoints()
{
   RebuildIfNeeded();
   return mPoints;
}

unsigned long long DistributePointsInGridNode::PointRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Material DistributePointsInGridNode::GetMaterial() const
{
   Material m;
   m.color[0] = tint[0]; m.color[1] = tint[1]; m.color[2] = tint[2];
   return m;
}

void DistributePointsInGridNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   RebuildIfNeeded();
}

// =============================================================== Merge by Distance

void MergeByDistanceNode::RebuildIfNeeded()
{
   if (input == nullptr)
   {
      if (!mCache.vertices.empty())
      {
         mCache = Mesh();
         mMeshRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      return;
   }

   const unsigned long long upstream = input->MeshRevision();
   if (mBuiltInput == input && mBuiltUpstream == upstream && mBuiltThreshold == threshold)
      return;

   mCache = MeshOps::MergeByDistance(input->GetMesh(), threshold);
   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltThreshold = threshold;
   mMeshRevision = NextMeshRevision();
}

const Mesh& MergeByDistanceNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long MergeByDistanceNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

void MergeByDistanceNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

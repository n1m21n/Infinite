#include "UtilityNodes.h"

#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const Mesh kEmptyMesh;
   const std::vector<std::string> kModeNames = { "Vertices", "Edges", "Faces" };
   const std::vector<std::string> kJoinModeNames = { "Merge", "Union", "Intersect", "Difference" };
}

// ===================================================================== Null 3D

const Mesh& Null3DNode::GetMesh()
{
   return input ? input->GetMesh() : mEmpty;
}

size_t Null3DNode::TriangleCount() const
{
   if (input == nullptr)
      return 0;
   return const_cast<IGeometrySource*>(input)->GetMesh().indices.size() / 3;
}

void Null3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
}

// ==================================================================== Material

const Mesh& MaterialNode::GetMesh()
{
   return input ? input->GetMesh() : mEmpty;
}

size_t MaterialNode::TriangleCount() const
{
   if (input == nullptr)
      return 0;
   return const_cast<IGeometrySource*>(input)->GetMesh().indices.size() / 3;
}

Material MaterialNode::GetMaterial() const
{
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

unsigned int MaterialNode::GetSurfaceTexture()
{
   return GetMaterialTexture(kMapAlbedo);
}

unsigned int MaterialNode::GetMaterialTexture(int map)
{
   if (map < 0 || map >= kMapCount)
      return 0;
   // Its own map wins when one is patched in; otherwise whatever the upstream
   // shape already carried passes through untouched.
   if (mMaps[map].IsConnected() && mMaps[map].GetSource())
      return mMaps[map].GetSource()->GetOutputTexture();
   return input ? input->GetMaterialTexture(map) : 0;
}

unsigned long long MaterialNode::SurfaceTextureRevision() const
{
   if (mMaps[kMapAlbedo].IsConnected() && mMaps[kMapAlbedo].GetSource())
      return mMaps[kMapAlbedo].Revision();
   return input ? input->SurfaceTextureRevision() : 0;
}

void MaterialNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   for (int map = 0; map < kMapCount; map++)
      if (mMaps[map].IsConnected())
         mMaps[map].Pull(frameId);
}

// ==================================================================== Mapping

namespace
{
   const std::vector<std::string> kMapSpaceNames = { "UV", "Generated", "Object" };
}

const std::vector<std::string>& MappingNode::SpaceNames() { return kMapSpaceNames; }

const Mesh& MappingNode::GetMesh()
{
   return input ? input->GetMesh() : mEmpty;
}

size_t MappingNode::TriangleCount() const
{
   if (input == nullptr)
      return 0;
   return const_cast<IGeometrySource*>(input)->GetMesh().indices.size() / 3;
}

MappingTransform MappingNode::GetMappingTransform() const
{
   MappingTransform t;
   t.space = space;
   t.translate[0] = translateX; t.translate[1] = translateY; t.translate[2] = translateZ;
   t.rotate[0] = rotateX; t.rotate[1] = rotateY; t.rotate[2] = rotateZ;
   t.scale[0] = scaleX; t.scale[1] = scaleY; t.scale[2] = scaleZ;
   return t;
}

void MappingNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
}

// =============================================================== Join Geometry

const std::vector<std::string>& JoinGeometryNode::ModeNames() { return kJoinModeNames; }

int JoinGeometryNode::ConnectedCount() const
{
   int count = 0;
   for (int i = 0; i < kSlots; i++)
      if (inputs[i] != nullptr)
         count++;
   return count;
}

void JoinGeometryNode::RebuildIfNeeded()
{
   auto sameMatrix = [](const Mat4& a, const Mat4& b)
   {
      for (int k = 0; k < 16; k++)
         if (a.m[k] != b.m[k])
            return false;
      return true;
   };

   bool dirty = false;
   for (int i = 0; i < kSlots; i++)
   {
      const unsigned long long rev = inputs[i] ? inputs[i]->MeshRevision() : 0;
      const Mat4 matrix = inputs[i] ? inputs[i]->GetModelMatrix() : Mat4::Identity();
      if (mBuiltInputs[i] != (const void*)inputs[i] || mBuiltRevisions[i] != rev ||
          !sameMatrix(mBuiltMatrices[i], matrix))
         dirty = true;
   }
   if (mBuiltMode != mode)
      dirty = true;
   if (!dirty)
      return;

   mCache = Mesh();
   bool haveFirst = false;
   for (int i = 0; i < kSlots; i++)
   {
      mBuiltInputs[i] = inputs[i];
      mBuiltRevisions[i] = inputs[i] ? inputs[i]->MeshRevision() : 0;
      mBuiltMatrices[i] = inputs[i] ? inputs[i]->GetModelMatrix() : Mat4::Identity();
      if (inputs[i] == nullptr)
         continue;

      const Mesh& src = inputs[i]->GetMesh();
      if (src.Empty())
         continue;

      // Each part's own transform is baked in here. The merged mesh carries a
      // single model matrix, so anything not baked would lose its placement.
      const Mesh placed = MeshOps::Transform(src, inputs[i]->GetModelMatrix());

      if (mode == kMerge)
      {
         const unsigned int base = (unsigned int)mCache.vertices.size();
         mCache.vertices.insert(mCache.vertices.end(), placed.vertices.begin(), placed.vertices.end());
         for (unsigned int idx : placed.indices)
            mCache.indices.push_back(base + idx);
      }
      else if (!haveFirst)
      {
         // The first input is the base; later ones are applied to it in turn,
         // so A minus B minus C means what it reads like.
         mCache = placed;
         haveFirst = true;
      }
      else
      {
         const int op = (mode == kUnion) ? MeshOps::kBooleanUnion
                      : (mode == kIntersect) ? MeshOps::kBooleanIntersect
                                             : MeshOps::kBooleanDifference;
         mCache = MeshOps::Boolean(mCache, placed, op);
      }
   }
   mBuiltMode = mode;
   mMeshRevision = NextMeshRevision();
}

const Mesh& JoinGeometryNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long JoinGeometryNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Mat4 JoinGeometryNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material JoinGeometryNode::GetMaterial() const
{
   if (inheritMaterial)
   {
      const int pick = std::max(0, std::min(materialFrom, kSlots - 1));
      if (inputs[pick] != nullptr)
         return inputs[pick]->GetMaterial();
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return inputs[i]->GetMaterial();
   }

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

unsigned int JoinGeometryNode::GetSurfaceTexture()
{
   const int pick = std::max(0, std::min(materialFrom, kSlots - 1));
   if (inputs[pick] != nullptr)
      return inputs[pick]->GetSurfaceTexture();
   for (int i = 0; i < kSlots; i++)
      if (inputs[i] != nullptr)
         return inputs[i]->GetSurfaceTexture();
   return 0;
}

unsigned long long JoinGeometryNode::SurfaceTextureRevision() const
{
   const int pick = std::max(0, std::min(materialFrom, kSlots - 1));
   if (inputs[pick] != nullptr)
      return inputs[pick]->SurfaceTextureRevision();
   for (int i = 0; i < kSlots; i++)
      if (inputs[i] != nullptr)
         return inputs[i]->SurfaceTextureRevision();
   return 0;
}

MappingTransform JoinGeometryNode::GetMappingTransform() const
{
   // Same "which input wins" choice already exposed for material/texture,
   // rather than always identity - a merged mesh draws with one Mapping
   // setting, so it may as well be the one the user already picked.
   const int pick = std::max(0, std::min(materialFrom, kSlots - 1));
   if (inputs[pick] != nullptr)
      return inputs[pick]->GetMappingTransform();
   for (int i = 0; i < kSlots; i++)
      if (inputs[i] != nullptr)
         return inputs[i]->GetMappingTransform();
   return MappingTransform();
}

void JoinGeometryNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   for (int i = 0; i < kSlots; i++)
      if (auto* upstream = dynamic_cast<INode*>(inputs[i]))
         upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

// =============================================================== Mesh to Points

const std::vector<std::string>& MeshToPointsNode::ModeNames() { return kModeNames; }

void MeshToPointsNode::RebuildIfNeeded()
{
   if (input == nullptr)
   {
      if (!mCache.vertices.empty())
      {
         mCache = Mesh();
         mPointCount = 0;
         mMeshRevision = NextMeshRevision();
      }
      return;
   }

   // Keyed on the upstream stamp rather than on a triangle count: an operator
   // can change a mesh's shape without changing how many triangles it has.
   const unsigned long long upstream = input->MeshRevision();
   if (mBuiltInput == input && mBuiltUpstream == upstream && mBuiltMode == mode &&
       mBuiltMax == maxPoints && mBuiltSize == pointSize)
      return;

   const Mesh& src = input->GetMesh();
   const std::vector<MeshPoint> points = MeshOps::ToPoints(src, mode, maxPoints);
   mPointCount = points.size();
   mCache = MeshOps::PointsToFaces(points, pointSize);

   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltMode = mode;
   mBuiltMax = maxPoints;
   mBuiltSize = pointSize;
   mMeshRevision = NextMeshRevision();
}

const Mesh& MeshToPointsNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long MeshToPointsNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Material MeshToPointsNode::GetMaterial() const
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

unsigned int MeshToPointsNode::GetSurfaceTexture()
{
   return input ? input->GetSurfaceTexture() : 0;
}

void MeshToPointsNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

// =================================================================== Metaballs

void MetaBallNode::RebuildIfNeeded()
{
   // Marching cubes over a 40-cubed grid is far too expensive to redo for a
   // sub-pixel change, so the orbit is quantised and every parameter is checked.
   const double beat = Transport::Instance().Beats() * (double)spin;
   const double quantised = std::floor(beat * 60.0) / 60.0;
   const unsigned long long cloudRevision = cloudSource ? cloudSource->PointRevision() : 0;

   if (mBuiltCount == ballCount && mBuiltRes == resolution &&
       mBuiltThreshold == threshold && mBuiltBounds == bounds &&
       mBuiltRadius == radius && mBuiltSpread == spread && mBuiltMax == maxFromCloud &&
       mBuiltBeat == quantised && mBuiltCloud == (const void*)cloudSource &&
       mBuiltCloudRevision == cloudRevision && !mCache.Empty())
      return;

   std::vector<Primitives::MetaBall> balls;
   if (cloudSource != nullptr)
   {
      // Surfacing a particle system. Capped, because the field cost is the ball
      // count times the whole grid - a thousand particles would be unusable.
      const std::vector<Particle>& cloud = cloudSource->GetPoints();
      const int cap = std::max(1, std::min(maxFromCloud, 64));
      int taken = 0;
      for (const Particle& p : cloud)
      {
         if (!p.alive)
            continue;
         if (taken >= cap)
            break;
         Primitives::MetaBall b;
         b.x = p.px; b.y = p.py; b.z = p.pz;
         b.strength = radius * radius * std::max(0.05f, p.scale);
         balls.push_back(b);
         taken++;
      }
   }
   else
   {
      const int count = std::max(1, std::min(ballCount, kMaxBalls));
      for (int i = 0; i < count; i++)
      {
         // Arranged on a ring at irrational angular offsets, so they drift in
         // and out of contact rather than pulsing in unison.
         const float t = (float)i / (float)count;
         const float angle = t * 6.28318530718f + (float)quantised * 6.28318530718f;
         Primitives::MetaBall b;
         b.x = std::cos(angle) * spread;
         b.y = std::sin(angle * 1.618f) * spread * 0.6f;
         b.z = std::sin(angle) * spread;
         b.strength = radius * radius;
         balls.push_back(b);
      }
   }

   mBallCount = balls.size();
   mCache = Primitives::MetaBalls(balls, resolution, threshold, bounds);

   mBuiltCount = ballCount;
   mBuiltRes = resolution;
   mBuiltThreshold = threshold;
   mBuiltBounds = bounds;
   mBuiltRadius = radius;
   mBuiltSpread = spread;
   mBuiltMax = maxFromCloud;
   mBuiltBeat = quantised;
   mBuiltCloud = cloudSource;
   mBuiltCloudRevision = cloudRevision;
   mMeshRevision = NextMeshRevision();
}

const Mesh& MetaBallNode::GetMesh()
{
   RebuildIfNeeded();
   return mCache;
}

unsigned long long MetaBallNode::MeshRevision()
{
   RebuildIfNeeded();
   return mMeshRevision;
}

Mat4 MetaBallNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material MetaBallNode::GetMaterial() const
{
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

void MetaBallNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* c = dynamic_cast<INode*>(cloudSource))
      c->CookIfNeeded(frameId);
   RebuildIfNeeded();
}

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
   if (bypassed)
      return input ? input->GetMaterial() : Material();

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
   m.sheen = sheen;
   m.sheenColor[0] = sheenColor[0];
   m.sheenColor[1] = sheenColor[1];
   m.sheenColor[2] = sheenColor[2];
   m.sheenRoughness = sheenRoughness;
   m.iridescence = iridescence;
   m.iridescenceIor = iridescenceIor;
   m.iridescenceThickness = iridescenceThickness;
   m.anisotropy = anisotropy;
   m.anisotropyRotation = anisotropyRotation;
   m.dispersion = dispersion;
   m.alphaCutoff = alphaCutoff;
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
   const float d2r = 3.14159265f / 180.0f;
   t.rotate[0] = rotateX * d2r; t.rotate[1] = rotateY * d2r; t.rotate[2] = rotateZ * d2r;
   t.scale[0] = scaleX; t.scale[1] = scaleY; t.scale[2] = scaleZ;
   t.triplanarBlend = triplanarBlend;
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
      InstanceOnPointsNode* instancer = inputs[i] ? FindInstancer(inputs[i]) : nullptr;
      const unsigned long long instRev = instancer ? instancer->InstanceRevision() : 0;
      const Mat4 groupMatrix = instancer ? inputs[i]->GetInstanceGroupMatrix() : Mat4::Identity();
      const std::vector<Mat4>* xformsPtr = instancer ? &ResolveInstanceTransforms(inputs[i], instancer) : nullptr;
      const size_t instCount = xformsPtr ? xformsPtr->size() : 0;

      if (mBuiltInputs[i] != (const void*)inputs[i] || mBuiltRevisions[i] != rev ||
          mBuiltInstancers[i] != (const void*)instancer || mBuiltInstRevisions[i] != instRev ||
          !(mBuiltGroupMatrices[i] == groupMatrix) || mBuiltInstanceCounts[i] != instCount ||
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
      InstanceOnPointsNode* instancer = inputs[i] ? FindInstancer(inputs[i]) : nullptr;
      mBuiltInstancers[i] = instancer;
      mBuiltInstRevisions[i] = instancer ? instancer->InstanceRevision() : 0;
      mBuiltGroupMatrices[i] = instancer ? inputs[i]->GetInstanceGroupMatrix() : Mat4::Identity();
      const std::vector<Mat4>* xformsPtr = instancer ? &ResolveInstanceTransforms(inputs[i], instancer) : nullptr;
      mBuiltInstanceCounts[i] = xformsPtr ? xformsPtr->size() : 0;

      if (inputs[i] == nullptr)
         continue;

      const Mesh& src = inputs[i]->GetMesh();
      if (src.Empty())
         continue;

      Mesh placed;
      if (instancer != nullptr && xformsPtr != nullptr && !xformsPtr->empty())
      {
         placed = MeshOps::RealizeInstances(src, *xformsPtr, mBuiltGroupMatrices[i], &instancer->InstanceColors(), 256);
      }
      else
      {
         placed = src;
      }
      if (inputs[i]->GetModelMatrix() != Mat4::Identity())
         placed = MeshOps::Transform(placed, inputs[i]->GetModelMatrix());

      if (mode == kMerge)
      {
         const unsigned int base = (unsigned int)mCache.vertices.size();
         mCache.vertices.insert(mCache.vertices.end(), placed.vertices.begin(), placed.vertices.end());
         for (unsigned int idx : placed.indices)
            mCache.indices.push_back(base + idx);
         if (!placed.vertexColor.empty())
            mCache.vertexColor.insert(mCache.vertexColor.end(), placed.vertexColor.begin(), placed.vertexColor.end());
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
   if (bypassed)
   {
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return inputs[i]->GetMesh();
      return mCache;
   }
   RebuildIfNeeded();
   return mCache;
}

unsigned long long JoinGeometryNode::MeshRevision()
{
   if (bypassed)
   {
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return inputs[i]->MeshRevision();
      return 0;
   }
   RebuildIfNeeded();
   return mMeshRevision;
}

Mat4 JoinGeometryNode::GetModelMatrix() const
{
   if (bypassed)
   {
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return inputs[i]->GetModelMatrix();
   }
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material JoinGeometryNode::GetMaterial() const
{
   if (bypassed)
   {
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return inputs[i]->GetMaterial();
   }
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
      if (!mCache.vertices.empty() || !mPoints.empty())
      {
         mCache = Mesh();
         mPoints.clear();
         mPointCount = 0;
         mMeshRevision = NextMeshRevision();
      }
      mBuiltInput = nullptr;
      return;
   }

   const unsigned long long upstream = input->MeshRevision();
   InstanceOnPointsNode* instancer = FindInstancer(input);
   const unsigned long long instRev = instancer ? instancer->InstanceRevision() : 0;
   const Mat4 groupMatrix = instancer ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   const std::vector<Mat4>* xformsPtr = instancer ? &ResolveInstanceTransforms(input, instancer) : nullptr;
   const size_t instCount = xformsPtr ? xformsPtr->size() : 0;

   const bool sameColor = (mBuiltColor[0] == color[0] && mBuiltColor[1] == color[1] && mBuiltColor[2] == color[2]);
   if (mBuiltInput == input && mBuiltUpstream == upstream &&
       mBuiltInstancer == (const void*)instancer && mBuiltInstRevision == instRev &&
       mBuiltGroupMatrix == groupMatrix && mBuiltInstanceCount == instCount &&
       mBuiltMode == mode && mBuiltMax == maxPoints && mBuiltSize == pointSize &&
       mBuiltWeld == weld && mBuiltDissolve == dissolveAngleDegrees &&
       mBuiltInherit == inheritMaterial && sameColor)
      return;

   const Mesh& src = input->GetMesh();
   float tint[3];
   if (inheritMaterial && input != nullptr)
   {
      Material m = input->GetMaterial();
      tint[0] = m.color[0]; tint[1] = m.color[1]; tint[2] = m.color[2];
   }
   else
   {
      tint[0] = color[0]; tint[1] = color[1]; tint[2] = color[2];
   }

   mPoints.clear();
   std::vector<MeshPoint> allPoints;

   if (instancer != nullptr && xformsPtr != nullptr && !xformsPtr->empty())
   {
      const std::vector<Mat4>& xforms = *xformsPtr;
      const std::vector<float>& instColors = instancer->InstanceColors();
      const int n = std::min((int)xforms.size(), 256);
      const bool isGroupIdent = (groupMatrix == Mat4::Identity());

      // Sample stamp once
      const std::vector<MeshPoint> basePoints = MeshOps::ToPoints(src, mode, maxPoints, weld, dissolveAngleDegrees);

      for (int i = 0; i < n; i++)
      {
         const Mat4 m = isGroupIdent ? xforms[i] : Mat4::Multiply(groupMatrix, xforms[i]);
         float nMat[9];
         m.NormalMatrix(nMat);

         float instTint[3] = { tint[0], tint[1], tint[2] };
         if ((size_t)i * 3 + 2 < instColors.size())
         {
            instTint[0] *= instColors[(size_t)i * 3 + 0];
            instTint[1] *= instColors[(size_t)i * 3 + 1];
            instTint[2] *= instColors[(size_t)i * 3 + 2];
         }

         for (const MeshPoint& p : basePoints)
         {
            MeshPoint wp;
            wp.px = m.m[0]*p.px + m.m[4]*p.py + m.m[8]*p.pz + m.m[12];
            wp.py = m.m[1]*p.px + m.m[5]*p.py + m.m[9]*p.pz + m.m[13];
            wp.pz = m.m[2]*p.px + m.m[6]*p.py + m.m[10]*p.pz + m.m[14];

            float nx = nMat[0]*p.nx + nMat[3]*p.ny + nMat[6]*p.nz;
            float ny = nMat[1]*p.nx + nMat[4]*p.ny + nMat[7]*p.nz;
            float nz = nMat[2]*p.nx + nMat[5]*p.ny + nMat[8]*p.nz;
            const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            wp.nx = nx; wp.ny = ny; wp.nz = nz;
            wp.scale = p.scale;
            wp.r = p.r; wp.g = p.g; wp.b = p.b;
            allPoints.push_back(wp);

            Particle particle;
            particle.px = wp.px; particle.py = wp.py; particle.pz = wp.pz;
            particle.nx = wp.nx; particle.ny = wp.ny; particle.nz = wp.nz;
            particle.scale = pointSize * 0.5f * p.scale;
            particle.r = instTint[0] * p.r; particle.g = instTint[1] * p.g; particle.b = instTint[2] * p.b;
            mPoints.push_back(particle);
         }
      }
      mPointCount = allPoints.size();
      mCache = MeshOps::PointsToFaces(allPoints, pointSize);
   }
   else
   {
      const std::vector<MeshPoint> points = MeshOps::ToPoints(src, mode, maxPoints, weld, dissolveAngleDegrees);
      mPointCount = points.size();
      mCache = MeshOps::PointsToFaces(points, pointSize);

      mPoints.reserve(points.size());
      for (const MeshPoint& p : points)
      {
         Particle particle;
         particle.px = p.px; particle.py = p.py; particle.pz = p.pz;
         particle.nx = p.nx; particle.ny = p.ny; particle.nz = p.nz;
         particle.scale = pointSize * 0.5f * p.scale;
         particle.r = tint[0] * p.r; particle.g = tint[1] * p.g; particle.b = tint[2] * p.b;
         mPoints.push_back(particle);
      }
   }

   mBuiltInput = input;
   mBuiltUpstream = upstream;
   mBuiltInstancer = instancer;
   mBuiltInstRevision = instRev;
   mBuiltGroupMatrix = groupMatrix;
   mBuiltInstanceCount = instCount;
   mBuiltMode = mode;
   mBuiltMax = maxPoints;
   mBuiltSize = pointSize;
   mBuiltWeld = weld;
   mBuiltDissolve = dissolveAngleDegrees;
   mBuiltInherit = inheritMaterial;
   mBuiltColor[0] = color[0]; mBuiltColor[1] = color[1]; mBuiltColor[2] = color[2];
   mMeshRevision = NextMeshRevision();
}

const Mesh& MeshToPointsNode::GetMesh()
{
   if (bypassed)
      return input ? input->GetMesh() : kEmptyMesh;
   RebuildIfNeeded();
   return mCache;
}

unsigned long long MeshToPointsNode::MeshRevision()
{
   if (bypassed)
      return input ? input->MeshRevision() : 0;
   RebuildIfNeeded();
   return mMeshRevision;
}

const std::vector<Particle>& MeshToPointsNode::GetPoints()
{
   static const std::vector<Particle> kEmptyPoints;
   if (bypassed)
      return kEmptyPoints;
   RebuildIfNeeded();
   return mPoints;
}

unsigned long long MeshToPointsNode::PointRevision()
{
   if (bypassed)
      return 0;
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
   const std::vector<Particle>* cloud = cloudSource ? cloudSource->GetPointCloud() : nullptr;
   const unsigned long long cloudRevision = cloudSource ? cloudSource->PointCloudRevision() : 0;

   if (mBuiltCount == ballCount && mBuiltRes == resolution &&
       mBuiltThreshold == threshold && mBuiltBounds == bounds &&
       mBuiltRadius == radius && mBuiltSpread == spread && mBuiltMax == maxFromCloud &&
       mBuiltBeat == quantised && mBuiltCloud == (const void*)cloudSource &&
       mBuiltCloudRevision == cloudRevision && !mCache.Empty())
      return;

   std::vector<Primitives::MetaBall> balls;
   if (cloud != nullptr)
   {
      // Surfacing a particle system. Capped, because the field cost is the ball
      // count times the whole grid - a thousand particles would be unusable.
      const int cap = std::max(1, std::min(maxFromCloud, 64));
      int taken = 0;
      for (const Particle& p : *cloud)
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
   if (bypassed)
      return kEmptyMesh;
   RebuildIfNeeded();
   return mCache;
}

unsigned long long MetaBallNode::MeshRevision()
{
   if (bypassed)
      return 0;
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

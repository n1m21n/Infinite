#include "UtilityNodes.h"

#include <algorithm>

namespace
{
   const Mesh kEmptyMesh;
   const std::vector<std::string> kModeNames = { "Vertices", "Edges", "Faces" };
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
   return m;
}

unsigned int MaterialNode::GetSurfaceTexture()
{
   // Its own texture wins when one is patched in; otherwise whatever the
   // upstream shape already carried is left alone.
   if (mTextureInput.IsConnected() && mTextureInput.GetSource())
      return mTextureInput.GetSource()->GetOutputTexture();
   return input ? input->GetSurfaceTexture() : 0;
}

void MaterialNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
   if (mTextureInput.IsConnected())
      mTextureInput.Pull(frameId);
}

// =============================================================== Join Geometry

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
   if (!dirty)
      return;

   mCache = Mesh();
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
      const unsigned int base = (unsigned int)mCache.vertices.size();
      mCache.vertices.insert(mCache.vertices.end(), placed.vertices.begin(), placed.vertices.end());
      for (unsigned int idx : placed.indices)
         mCache.indices.push_back(base + idx);
   }
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

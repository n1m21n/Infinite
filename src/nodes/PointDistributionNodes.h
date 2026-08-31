#pragma once

#include <string>
#include <vector>

#include "GeometryOpNodes.h"

// Phase 6: real point distribution. Four small, unrelated node types that
// happen to all live in the point/vertex domain - kept in their own file
// pair rather than folded into GeometryOpNodes' table, the same call
// InstanceOnPointsNode/SetColorNode/WrapNode already made for "doesn't fit
// the single-geo-input operator shape".
// See docs/plans/phase6-point-distribution.md.

// --- Distribute Points on Faces --------------------------------------------
// Area-weighted scatter (MeshOps::DistributeOnFaces), the uniform-coverage
// counterpart to Mesh to Points' index-stride sampling - see the function's
// doc comment in Mesh.h for why that distinction matters (UV sphere pole
// clumping).
class DistributePointsOnFacesNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new DistributePointsOnFacesNode(); }
   static const std::vector<std::string>& MethodNames();

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   // A billboard-quad mesh (MeshOps::PointsToFaces), same fallback shape as
   // MeshToPointsNode - anything that only understands GetMesh() still sees
   // something, and Render 3D's drawCloudSlot draws GetPointCloud() instead
   // when the consumer understands clouds.
   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   const std::vector<Particle>* GetPointCloud() override { return &GetPoints(); }
   unsigned long long PointCloudRevision() override { return PointRevision(); }
   const std::vector<Particle>& GetPoints();
   unsigned long long PointRevision();

   Mat4 GetModelMatrix() const override
   {
      if (input == nullptr) return Mat4::Identity();
      if (FindInstancer(input) != nullptr) return Mat4::Identity();
      return input->GetModelMatrix();
   }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   size_t PointCount() const { return mPoints.size(); }

   float density = 40.0f;
   int method = 0;             // MeshOps::kDistributeRandom / kDistributePoisson
   float minDistance = 0.1f;
   float pointSize = 0.05f;
   float seed = 0.0f;
   bool inheritMaterial = true;

   float color[3] = { 0.9f, 0.75f, 0.5f };
   float metallic = 0.2f;
   float roughness = 0.4f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;
   float ior = 1.5f;
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   float specular = 0.5f;
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("density", density); v.Int("method", method);
      v.Float("minDistance", minDistance); v.Float("pointSize", pointSize);
      v.Float("seed", seed); v.Bool("inheritMaterial", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
   }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   std::vector<Particle> mPoints;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   const void* mBuiltInstancer = nullptr;
   unsigned long long mBuiltInstRevision = 0;
   Mat4 mBuiltGroupMatrix;
   size_t mBuiltInstanceCount = 0;
   float mBuiltDensity = -1.0f;
   int mBuiltMethod = -1;
   float mBuiltMinDistance = -1.0f, mBuiltPointSize = -1.0f, mBuiltSeed = 0.0f;
   bool mBuiltInherit = true;
   float mBuiltColor[3] = { -1.0f, -1.0f, -1.0f };
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

// --- Points to Vertices -----------------------------------------------------
// Point cloud in, mesh out: one vertex per point, no edges, no faces. Lets a
// cloud (Particle System, a Distribute node, Image to Points) re-enter mesh
// operators that only know how to walk a Mesh. The output trips
// Mesh::Empty() (indices is empty) on purpose - see Mesh::HasGeometry() and
// the callers that were updated to check it instead.
class PointsToVerticesNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new PointsToVerticesNode(); }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override
   {
      if (input == nullptr) return Mat4::Identity();
      if (FindInstancer(input) != nullptr) return Mat4::Identity();
      return input->GetModelMatrix();
   }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "points"; }
   size_t VertexCount() const { return mCache.vertices.size(); }

   bool aliveOnly = true;      // drop dead particles (Particle::alive == false)
   bool inheritMaterial = true;

   float color[3] = { 0.9f, 0.9f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;
   float ior = 1.5f;
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   float specular = 0.5f;
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Bool("aliveOnly", aliveOnly); v.Bool("inheritMaterial", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
   }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   const void* mBuiltInstancer = nullptr;
   unsigned long long mBuiltInstRevision = 0;
   Mat4 mBuiltGroupMatrix;
   size_t mBuiltInstanceCount = 0;
   bool mBuiltAliveOnly = true;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

// --- Distribute Points in Grid ---------------------------------------------
// The honest version of "a grid": a rectangular lattice of points, no new
// data type - a source node like ImageToPointsNode, not an operator. Row-
// major (y outer, x inner) with cell-centre UV, matching ImageToPointsNode's
// ordering convention exactly so a grid and an Image to Points of the same
// counts/size line up point-for-point.
class DistributePointsInGridNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new DistributePointsInGridNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   const std::vector<Particle>* GetPointCloud() override { return &GetPoints(); }
   unsigned long long PointCloudRevision() override { return PointRevision(); }
   const std::vector<Particle>& GetPoints();
   unsigned long long PointRevision();

   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override { return 0; }

   IGeometrySource** GeometryInputSlot(int) override { return nullptr; }
   size_t PointCount() const { return mPoints.size(); }

   int countX = 10, countY = 10;
   float spacingX = 0.2f, spacingY = 0.2f;
   float jitter = 0.0f;
   float pointSize = 0.05f;
   float seed = 0.0f;
   float tint[3] = { 1.0f, 1.0f, 1.0f };

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("countX", countX); v.Int("countY", countY);
      v.Float("spacingX", spacingX); v.Float("spacingY", spacingY);
      v.Float("jitter", jitter); v.Float("pointSize", pointSize);
      v.Float("seed", seed); v.Color("tint", tint);
   }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   std::vector<Particle> mPoints;
   int mBuiltCountX = -1, mBuiltCountY = -1;
   float mBuiltSpacingX = -1.0f, mBuiltSpacingY = -1.0f;
   float mBuiltJitter = -1.0f, mBuiltPointSize = -1.0f, mBuiltSeed = 0.0f;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

// --- Merge by Distance ------------------------------------------------------
// MeshOps::MergeByDistance with a tunable threshold, wrapped as a node - the
// cleanup step that makes scatter/convert results usable: weld the seams a
// conversion left behind, or collapse geometry outright at a large radius.
class MergeByDistanceNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new MergeByDistanceNode(); }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   IGeometrySource* PassthroughSource() const override { return input; }
   // Forwarded alongside PassthroughSource - see MaterialNode for why the two
   // have to travel together.
   Mat4 GetInstanceGroupMatrix() const override
   {
      return input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return input ? input->InstanceTransformOverride() : nullptr;
   }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }

   float threshold = 0.001f;

   void VisitParams(ParamVisitor& v) override { v.Float("threshold", threshold); }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   float mBuiltThreshold = -1.0f;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

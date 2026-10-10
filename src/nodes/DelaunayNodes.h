#pragma once

#include "GeometryOpNodes.h"

// Delaunay Mesh: a point cloud in (only - a mesh on the pin is flagged and ignored),
// a triangulated surface out. The points are
// projected onto the plane picked by `plane`, triangulated there, and the
// triangles are lifted back onto the original 3D positions (a terrain-style
// height field when the cloud has depth).
//
// Voronoi Cells: the dual - one polygon per interior site, built from the
// circumcentres of the surrounding Delaunay triangles. Sites on the convex hull
// have unbounded cells and are skipped.
class DelaunayMeshNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new DelaunayMeshNode(); }
   static const std::vector<std::string>& PlaneNames();

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return input ? input->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override;
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return input ? input->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override
   {
      return ComputeContentRevision(GetMappingTransform(), mMappingRevision, mLastMappingHash);
   }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "points"; }
   const std::string& CookWarning() const override { return mCookWarning; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }

   int plane = 0;            // 0 XY, 1 XZ, 2 YZ
   bool aliveOnly = true;
   int maxPoints = 2000;     // O(n^2) triangulation; excess points are dropped evenly
   float inset = 0.0f;       // Voronoi only: pull each cell toward its site

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("plane", plane); v.Bool("aliveOnly", aliveOnly);
      v.Int("maxPoints", maxPoints); v.Float("inset", inset);
   }

protected:
   virtual bool IsVoronoi() const { return false; }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   std::string mCookWarning;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   int mBuiltPlane = -1;
   bool mBuiltAliveOnly = true;
   int mBuiltMaxPoints = 0;
   float mBuiltInset = 0.0f;
   unsigned long long mMeshRevision = 0;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   mutable unsigned long long mMappingRevision = 0;
   mutable size_t mLastMappingHash = 0;
   int mLastCookFrame = -1;
};

class VoronoiCellsNode : public DelaunayMeshNode
{
public:
   static INode* Create() { return new VoronoiCellsNode(); }

protected:
   bool IsVoronoi() const override { return true; }
};

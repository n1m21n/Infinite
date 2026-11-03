#pragma once

#include <string>

#include "Geometry3DNodes.h"

// Holds an Edit Mode session: a list of ops (select, move, rotate, scale,
// extrude, inset, delete) made in the 3D View with Tab and replayed on the
// input mesh (core/EditMesh). Elements are named by where they are, so the
// edit survives an upstream change that keeps the shape in place; a selection
// with nothing near it is skipped and the node warns.
class EditMeshNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new EditMeshNode(); }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return input ? input->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override { return input ? input->MaterialRevision() : 0; }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return input ? input->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override { return input ? input->SurfaceTextureRevision() : 0; }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override { return input ? input->MappingRevision() : 0; }
   IGeometrySource* PassthroughSource() const override { return input; }
   Mat4 GetInstanceGroupMatrix() const override { return input ? input->GetInstanceGroupMatrix() : Mat4::Identity(); }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return input ? input->InstanceTransformOverride() : nullptr;
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Particle>* GetPointCloud() override { return input ? input->GetPointCloud() : nullptr; }
   unsigned long long PointCloudRevision() override { return input ? input->PointCloudRevision() : 0; }
   float PointBaseSize() const override { return input ? input->PointBaseSize() : 1.0f; }
   const GpuPoints* GetGpuPoints() override { return input ? input->GetGpuPoints() : nullptr; }
   const Polyline* GetCurve() override { return input ? input->GetCurve() : nullptr; }
   unsigned long long CurveStamp() override { return input ? input->CurveStamp() : 0; }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   const std::string& CookWarning() const override { return mCookWarning; }

   std::string ops;   // one op per line, see core/EditMesh.h

   void VisitParams(ParamVisitor& v) override { v.Text("ops", ops); }

   // The input as it is before the ops, for the 3D View to pick on.
   const Mesh& InputMesh();
   int OpCount() const;

private:
   void Update();

   std::string mCookWarning;
   int mLastCookFrame = -1;
   Mesh mMesh;
   const void* mKeyInput = nullptr;
   unsigned long long mKeyRevision = 0, mMeshRevision = 0;
   std::string mKeyOps;
   bool mBuilt = false;
};

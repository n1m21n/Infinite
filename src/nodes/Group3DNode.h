#pragma once

#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "Geometry3DNodes.h"
#include "GeometryOpNodes.h"
#include "Mesh.h"

// Carries several geometry sources through one pin into Render 3D, so a scene
// is not capped at Render 3D's four geometry inputs. Unlike Join Geometry it
// does not merge anything: every child keeps its own mesh, material, texture,
// mapping and transform, and Render 3D draws each one as if it had its own
// pin. Groups can be patched into groups.
//
// To any other consumer (Transform, Join Geometry, Material, ...) it hands
// out its children baked into one mesh, each placed where Render 3D would
// draw it. That merged copy loses per-child materials (tints survive as
// vertex colour), so Render 3D keeps drawing the children individually.
//
// The group's own move / rotate / scale (about a pivot) applies to every child
// as it is drawn - the whole set sways as one and keeps its materials, which
// a Transform after the group could not do (it gets the bake). Children are
// then handed out through a thin proxy that multiplies the group matrix in;
// at the defaults they are handed out as they are.
class GroupPlacedChild : public IGeometrySource
{
public:
   IGeometrySource* src = nullptr;
   Mat4 place;

   const Mesh& GetMesh() override { return src->GetMesh(); }
   unsigned long long MeshRevision() override { return src->MeshRevision(); }
   Mat4 GetModelMatrix() const override { return Mat4::Multiply(place, src->GetModelMatrix()); }
   Material GetMaterial() const override { return src->GetMaterial(); }
   unsigned long long MaterialRevision() const override { return src->MaterialRevision(); }
   unsigned int GetSurfaceTexture() override { return src->GetSurfaceTexture(); }
   unsigned int GetMaterialTexture(int map) override { return src->GetMaterialTexture(map); }
   unsigned long long SurfaceTextureRevision() const override { return src->SurfaceTextureRevision(); }
   MappingTransform GetMappingTransform() const override { return src->GetMappingTransform(); }
   unsigned long long MappingRevision() const override { return src->MappingRevision(); }
   IGeometrySource* PassthroughSource() const override { return src; }
   int GroupChildCount() const override { return src->GroupChildCount(); }
   IGeometrySource* GroupChild(int index) const override
   {
      IGeometrySource* child = src->GroupChild(index);
      if (child == nullptr)
         return nullptr;
      if ((int)mChildren.size() <= index)
         mChildren.resize((size_t)index + 1);
      if (!mChildren[(size_t)index])
         mChildren[(size_t)index] = std::make_unique<GroupPlacedChild>();
      mChildren[(size_t)index]->src = child;
      mChildren[(size_t)index]->place = place;
      return mChildren[(size_t)index].get();
   }
   unsigned int LightMask() const override { return src->LightMask(); }
   unsigned int ShadowMask() const override { return src->ShadowMask(); }
   int PassIndex() const override { return src->PassIndex(); }
   void ObjectColor(float c[3]) const override { src->ObjectColor(c); }
   bool CastsShadow() const override { return src->CastsShadow(); }
   Mat4 GetInstanceGroupMatrix() const override { return Mat4::Multiply(place, src->GetInstanceGroupMatrix()); }
   const std::vector<unsigned char>* InstanceSelection() const override { return src->InstanceSelection(); }
   unsigned long long InstanceSelectionRevision() const override { return src->InstanceSelectionRevision(); }
   const std::vector<Mat4>* InstanceTransformOverride() const override { return src->InstanceTransformOverride(); }
   const std::vector<Particle>* GetPointCloud() override { return src->GetPointCloud(); }
   unsigned long long PointCloudRevision() override { return src->PointCloudRevision(); }
   float PointBaseSize() const override { return src->PointBaseSize(); }
   const GpuPoints* GetGpuPoints() override { return src->GetGpuPoints(); }
   const Polyline* GetCurve() override { return src->GetCurve(); }
   unsigned long long CurveStamp() override { return src->CurveStamp(); }

private:
   mutable std::vector<std::unique_ptr<GroupPlacedChild>> mChildren;
};

class Group3DNode : public INode, public IGeometrySource
{
public:
   static const int kSlots = 8;

   static INode* Create() { return new Group3DNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   // Render 3D cooks the children it flattens itself; anything else reaching
   // them through this group needs the group to pass the cook on.
   void CookIfNeeded(int frameId) override
   {
      if (mLastCookFrame == frameId)
         return; // also ends a group patched into itself
      mLastCookFrame = frameId;
      for (IGeometrySource* in : inputs)
         if (auto* upstream = dynamic_cast<INode*>(in))
            upstream->CookIfNeeded(frameId);
   }

   // Baked lazily: a group that only feeds Render 3D is never asked.
   const Mesh& GetMesh() override
   {
      RefreshBake();
      return mBaked;
   }
   unsigned long long MeshRevision() override
   {
      RefreshBake();
      return mRevision;
   }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }

   // The connected inputs, in pin order.
   int GroupChildCount() const override
   {
      int count = 0;
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            count++;
      return count;
   }
   IGeometrySource* GroupChild(int index) const override
   {
      for (int i = 0; i < kSlots; i++)
      {
         if (inputs[i] == nullptr)
            continue;
         if (index-- != 0)
            continue;
         if (IsIdentity())
            return inputs[i];
         if (!mPlaced[i])
            mPlaced[i] = std::make_unique<GroupPlacedChild>();
         mPlaced[i]->src = inputs[i];
         mPlaced[i]->place = GroupMatrix();
         return mPlaced[i].get();
      }
      return nullptr;
   }

   // Same order as Transform: scale, rotate Z/Y/X (degrees) about the pivot, then move.
   float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float scale = 1.0f;
   float pivotX = 0.0f, pivotY = 0.0f, pivotZ = 0.0f;

   bool IsIdentity() const
   {
      return offsetX == 0.0f && offsetY == 0.0f && offsetZ == 0.0f && rotX == 0.0f && rotY == 0.0f &&
             rotZ == 0.0f && scale == 1.0f;
   }
   Mat4 GroupMatrix() const
   {
      const float d2r = 3.14159265f / 180.0f;
      Mat4 m = Mat4::Scale(scale, scale, scale);
      m = Mat4::Multiply(Mat4::RotationZ(rotZ * d2r), m);
      m = Mat4::Multiply(Mat4::RotationY(rotY * d2r), m);
      m = Mat4::Multiply(Mat4::RotationX(rotX * d2r), m);
      m = Mat4::Multiply(m, Mat4::Translation(-pivotX, -pivotY, -pivotZ));
      m = Mat4::Multiply(Mat4::Translation(pivotX, pivotY, pivotZ), m);
      return Mat4::Multiply(Mat4::Translation(offsetX, offsetY, offsetZ), m);
   }

   IGeometrySource* inputs[kSlots] = {};
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo A", "geo B", "geo C", "geo D",
                                      "geo E", "geo F", "geo G", "geo H" };
      return (slot >= 0 && slot < kSlots) ? kNames[slot] : nullptr;
   }

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("offsetX", offsetX); v.Float("offsetY", offsetY); v.Float("offsetZ", offsetZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scale", scale);
      v.Float("pivotX", pivotX); v.Float("pivotY", pivotY); v.Float("pivotZ", pivotZ);
   }

private:
   void RefreshBake()
   {
      if (mBaking)
         return; // groups patched into each other: the inner ask sees the last bake
      mBaking = true;
      if (BakeGroupMesh(this, mSignature, mBaked) || mRevision == 0)
         mRevision = NextMeshRevision();
      mBaking = false;
   }

   mutable std::unique_ptr<GroupPlacedChild> mPlaced[kSlots];
   Mesh mBaked;
   unsigned long long mSignature = 0;
   unsigned long long mRevision = 0;
   int mLastCookFrame = -1;
   bool mBaking = false;
};

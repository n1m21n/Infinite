#pragma once

#include <string>

#include "Geometry3DNodes.h"

// Curves in, surface out. Both sit on one base that does the plumbing every curve-to-
// mesh node needs (bypass, forwarding the input's transform, material and mapping, the
// cache check); each subclass only says what it builds.
class CurveSurfaceNode : public INode, public IGeometrySource
{
public:
   INode* BypassSource() override { return dynamic_cast<INode*>(Primary()); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return Primary() ? Primary()->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return Primary() ? Primary()->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override;
   unsigned int GetSurfaceTexture() override { return Primary() ? Primary()->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return Primary() ? Primary()->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override
   {
      return Primary() ? Primary()->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return Primary() ? Primary()->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override
   {
      return ComputeContentRevision(GetMappingTransform(), mMappingRevision, mLastMappingHash);
   }
   const std::string& CookWarning() const override { return mCookWarning; }

protected:
   // The input whose transform, material and textures the result carries.
   virtual IGeometrySource* Primary() const = 0;
   // Every curve input, in slot order (null when empty).
   virtual int Inputs(IGeometrySource** out) const = 0;
   // Every parameter the mesh depends on, packed so the cache can compare them.
   virtual int Key(double* out) const = 0;
   virtual Mesh Build() = 0;


private:
   void RebuildIfNeeded();

   Mesh mMesh;
   std::string mCookWarning;
   unsigned long long mRevision = 0;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   mutable unsigned long long mMappingRevision = 0;
   mutable size_t mLastMappingHash = 0;

   static constexpr int kMaxKey = 16;
   bool mBuilt = false;
   int mBuiltInputs = 0;
   const void* mBuiltInput[2] = { nullptr, nullptr };
   unsigned long long mBuiltStamp[2] = { 0, 0 };
   int mBuiltKeyCount = 0;
   double mBuiltKey[kMaxKey] = {};
   int mLastCookFrame = -1;
};

// Lathe: revolves a curve about the X, Y or Z axis. A closed profile all the way round
// is a watertight solid; a part-turn is capped when `caps` is on; an open profile gives
// an open surface (a vase wall, a cone shell).
class RevolveNode : public CurveSurfaceNode
{
public:
   static INode* Create() { return new RevolveNode(); }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "profile"; }

   int axis = 1;            // 0 X, 1 Y, 2 Z
   float angle = 360.0f;    // degrees
   int segments = 32;
   bool caps = true;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("axis", axis); v.Float("angle", angle); v.Int("segments", segments); v.Bool("caps", caps);
   }

protected:
   IGeometrySource* Primary() const override { return input; }
   int Inputs(IGeometrySource** out) const override { out[0] = input; return 1; }
   int Key(double* out) const override
   {
      out[0] = axis; out[1] = angle; out[2] = segments; out[3] = caps ? 1.0 : 0.0;
      return 4;
   }
   Mesh Build() override;
};

// Sweep: carries a profile curve along a path curve. With no profile connected it
// sweeps a round one of `profileRadius`, so a path alone gives a tube.
class SweepNode : public CurveSurfaceNode
{
public:
   static INode* Create() { return new SweepNode(); }

   IGeometrySource* profile = nullptr;
   IGeometrySource* path = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return slot == 0 ? &profile : (slot == 1 ? &path : nullptr);
   }
   const char* InputLabel(int slot) const override { return slot == 0 ? "profile" : "path"; }

   float startScale = 1.0f;
   float endScale = 1.0f;
   float twist = 0.0f;        // degrees over the whole path
   bool caps = true;
   float profileRadius = 0.1f;
   int profileSides = 12;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("startScale", startScale); v.Float("endScale", endScale); v.Float("twist", twist);
      v.Bool("caps", caps); v.Float("profileRadius", profileRadius); v.Int("profileSides", profileSides);
   }

protected:
   IGeometrySource* Primary() const override { return path ? path : profile; }
   int Inputs(IGeometrySource** out) const override { out[0] = profile; out[1] = path; return 2; }
   int Key(double* out) const override
   {
      out[0] = startScale; out[1] = endScale; out[2] = twist; out[3] = caps ? 1.0 : 0.0;
      out[4] = profileRadius; out[5] = profileSides;
      return 6;
   }
   Mesh Build() override;
};

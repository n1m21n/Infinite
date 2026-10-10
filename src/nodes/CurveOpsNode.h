#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// Curve in, curve out: Resample / Simplify / Smooth / Offset (see CurveOps.h).
// The result is both followable (GetCurve, so a Path can ride it) and visible
// (a thin tube through GetMesh), the same dual role CurveNode has.
class CurveOpsNode : public INode, public IGeometrySource
{
public:
   enum Mode { kResample = 0, kSimplify, kSmooth, kOffset, kModeCount };

   static INode* Create() { return new CurveOpsNode(); }
   static const std::vector<std::string>& ModeNames();

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

   const Polyline* GetCurve() override;
   unsigned long long CurveStamp() override;

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "curve"; }
   const std::string& CookWarning() const override { return mCookWarning; }
   size_t PointCount() const { return mLine.Count(); }

   int mode = kResample;
   int count = 32;           // Resample
   float tolerance = 0.02f;  // Simplify
   int iterations = 3;       // Smooth
   float strength = 0.5f;    // Smooth
   float distance = 0.2f;    // Offset
   int arcSteps = 6;         // Offset: points per round join
   float radius = 0.02f;     // preview tube
   int sides = 8;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("count", count); v.Float("tolerance", tolerance);
      v.Int("iterations", iterations); v.Float("strength", strength);
      v.Float("distance", distance); v.Int("arcSteps", arcSteps);
      v.Float("radius", radius); v.Int("sides", sides);
   }

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   std::string mCookWarning;
   Polyline mLine;
   unsigned long long mRevision = 0;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   mutable unsigned long long mMappingRevision = 0;
   mutable size_t mLastMappingHash = 0;

   bool mBuilt = false;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   int mBuiltMode = -1, mBuiltCount = 0, mBuiltIterations = 0, mBuiltArc = 0, mBuiltSides = 0;
   float mBuiltTolerance = 0, mBuiltStrength = 0, mBuiltDistance = 0, mBuiltRadius = 0;
   int mLastCookFrame = -1;
};

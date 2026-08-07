#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// A curve through space, as both something to look at and something to follow.
//
// It is an IGeometrySource so it can be seen - swept into a tube and rendered
// like any mesh - and an ICurveSource so a Path can travel along it. Those are
// genuinely different questions: a mesh cannot answer "where am I at t and
// which way am I heading", and a curve has no surface until one is built.
class CurveNode : public INode, public IGeometrySource, public ICurveSource
{
public:
   static const int kMaxPoints = 8;

   static INode* Create() { return new CurveNode(); }
   static const std::vector<std::string>& KindNames();
   static const std::vector<std::string>& PresetNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;

   const Polyline& GetPolyline() override;
   unsigned long long CurveRevision() override;

   size_t PointCount() const { return mLine.Count(); }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   int kind = 0;          // catmull-rom / bezier / b-spline / linear
   int preset = 0;        // starting arrangement of the control points
   int pointCount = 5;
   int segments = 16;
   bool closed = false;

   float spread = 1.0f;
   float height = 0.5f;
   float twist = 0.0f;
   float seed = 3.0f;

   float radius = 0.06f;
   int sides = 12;
   float taper = 0.0f;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float uniformScale = 1.0f;

   float color[3] = { 0.9f, 0.7f, 0.35f };
   float metallic = 0.2f;
   float roughness = 0.35f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("kind", kind); v.Int("preset", preset); v.Int("pointCount", pointCount);
      v.Int("segments", segments); v.Bool("closed", closed);
      v.Float("spread", spread); v.Float("height", height);
      v.Float("twist", twist); v.Float("seed", seed);
      v.Float("radius", radius); v.Int("sides", sides); v.Float("taper", taper);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("scale", uniformScale);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   Polyline mLine;
   unsigned long long mRevision = 0;

   int mBuiltKind = -1, mBuiltPreset = -1, mBuiltCount = -1, mBuiltSegments = -1;
   int mBuiltSides = -1;
   bool mBuiltClosed = false;
   float mBuiltSpread = -1, mBuiltHeight = -999, mBuiltTwist = -999, mBuiltSeed = -999;
   float mBuiltRadius = -1, mBuiltTaper = -1;
   int mLastCookFrame = -1;
};

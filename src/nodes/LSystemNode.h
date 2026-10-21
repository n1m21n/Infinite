#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// --- L-System -----------------------------------------------------------
// Grows a plant, tree or fractal from a few lines of text: an axiom, some
// rewrite rules, an angle and a number of generations. The rewritten string
// is walked by a 3D turtle: F draws forward, f moves without drawing, + and -
// turn, & and ^ pitch, \ and / roll, | turns around, [ and ] start and end a
// branch. Every other letter is only a placeholder for the rules to rewrite.
//
// The result is both a mesh (every branch swept into a tube) and a curve (one
// continuous walk that retraces each branch), so a Path can follow a plant.
// The output depends only on the text and the numbers: no random numbers.
class LSystemNode : public INode, public IGeometrySource
{
public:
   static const int kMaxIterations = 10;
   static const size_t kMaxSymbols = 200000;   // rewritten string length cap
   static const size_t kMaxStrands = 6000;     // branches turned into tubes

   static INode* Create() { return new LSystemNode(); }
   const std::string& CookWarning() const override { return mWarning; }
   const std::string& Status() const { return mStatus; }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned long long MaterialRevision() const override;

   const Polyline& GetPolyline();
   const Polyline* GetCurve() override { return bypassed ? nullptr : &GetPolyline(); }
   unsigned long long CurveStamp() override { return bypassed ? 0 : MeshRevision(); }

   size_t SegmentCount() const { return mSegments; }
   size_t StrandCount() const { return mStrands; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   std::string axiom = "X";
   std::string rules = "X=F+[[X]-X]-F[-FX]+X;F=FF";
   int iterations = 5;
   float angle = 25.0f;
   float length = 0.1f;
   float lengthScale = 1.0f;     // per-branch length multiplier
   float radius = 0.02f;
   float radiusScale = 0.8f;     // per-branch radius multiplier
   int sides = 6;
   float taper = 0.3f;

   float posX = 0.0f, posY = -1.0f, posZ = 0.0f;
   float uniformScale = 1.0f;

   float color[3] = { 0.35f, 0.65f, 0.25f };
   float metallic = 0.0f;
   float roughness = 0.6f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 0.6f, 1.0f, 0.5f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("axiom", axiom); v.Text("rules", rules);
      v.Int("iterations", iterations); v.Float("angle", angle);
      v.Float("length", length); v.Float("lengthScale", lengthScale);
      v.Float("radius", radius); v.Float("radiusScale", radiusScale);
      v.Int("sides", sides); v.Float("taper", taper);
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
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   std::string mWarning;
   std::string mStatus;
   size_t mSegments = 0;
   size_t mStrands = 0;

   std::string mBuiltAxiom, mBuiltRules;
   int mBuiltIterations = -1, mBuiltSides = -1;
   float mBuiltAngle = -999, mBuiltLength = -1, mBuiltLengthScale = -1;
   float mBuiltRadius = -1, mBuiltRadiusScale = -1, mBuiltTaper = -1;
   bool mHasBuilt = false;
   int mLastCookFrame = -1;
};

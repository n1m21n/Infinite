#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// One class for every mesh -> mesh operator, chosen by a dropdown - the same
// table-driven approach FilterNode uses for image effects, so adding an
// operator is a case in a switch rather than a new node type.
//
// Meshes are cached and only rebuilt when a parameter actually changes; a
// Subdivide feeding an Array can otherwise re-run hundreds of thousands of
// triangles every single frame.
class GeometryOpNode : public INode, public IGeometrySource
{
public:
   enum Op
   {
      kTransform = 0, kArray, kSubdivide, kSolidify, kExtrude,
      kWireframe, kTriangulate, kNormals, kExplode, kTwist, kOpCount
   };

   static INode* Create() { return new GeometryOpNode(); }
   static const std::vector<std::string>& OpNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   void GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                    float& outOpacity, int& outShading) const override;
   unsigned int GetSurfaceTexture() override;

   IGeometrySource* input = nullptr;
   size_t TriangleCount() const { return mCache.indices.size() / 3; }

   int op = kArray;
   bool inheritMaterial = true;

   // shared
   float amount = 1.0f;
   int count = 5;
   float offsetX = 0.6f, offsetY = 0.0f, offsetZ = 0.0f;
   float rotStep = 0.0f, scaleStep = 1.0f;
   bool radial = false;
   float radius = 1.0f;
   int levels = 1;
   float smooth = 1.0f;
   float thickness = 0.05f;
   bool keepOriginal = true;
   float inset = 0.0f;
   bool flatShade = false, flipNormals = false;
   float seed = 0.0f;
   int axis = 1;

   // material used when not inheriting
   float color[3] = { 0.8f, 0.82f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;

private:
   struct Signature
   {
      int op = -1, count = 0, levels = 0, axis = 0;
      float a = 0, ox = 0, oy = 0, oz = 0, rs = 0, ss = 0, rad = 0;
      float sm = 0, th = 0, ins = 0, sd = 0;
      bool radial = false, keep = false, flat = false, flip = false;
      const void* upstream = nullptr;
      size_t upstreamTris = 0;
      bool operator==(const Signature& o) const
      {
         return op == o.op && count == o.count && levels == o.levels && axis == o.axis &&
                a == o.a && ox == o.ox && oy == o.oy && oz == o.oz && rs == o.rs &&
                ss == o.ss && rad == o.rad && sm == o.sm && th == o.th && ins == o.ins &&
                sd == o.sd && radial == o.radial && keep == o.keep && flat == o.flat &&
                flip == o.flip && upstream == o.upstream && upstreamTris == o.upstreamTris;
      }
   };

   Signature CurrentSignature() const;

   Mesh mCache;
   Signature mBuilt;
   bool mHasBuilt = false;
   int mLastCookFrame = -1;
};

// --- Mesh to Points / Instance on Points --------------------------------
// Instancing is drawn with glDrawElementsInstanced, so ten thousand copies are
// still a single draw call.
class InstanceOnPointsNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new InstanceOnPointsNode(); }
   static const std::vector<std::string>& SourceNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   void GetMaterial(float outColor[3], float& outMetallic, float& outRoughness,
                    float& outOpacity, int& outShading) const override;
   unsigned int GetSurfaceTexture() override;

   // Slot 0 supplies the points, slot 1 the shape stamped on them.
   IGeometrySource* pointSource = nullptr;
   IGeometrySource* instanceShape = nullptr;

   // Instanced draw data, consumed by Render3DNode.
   const std::vector<Mat4>& InstanceTransforms() const { return mTransforms; }
   size_t InstanceCount() const { return mTransforms.size(); }
   size_t TriangleCount() const;

   int pointMode = 2;      // vertices / edges / faces
   int maxPoints = 2000;
   float instanceScale = 0.12f;
   float scaleRandom = 0.4f;
   float rotationRandom = 1.0f;
   bool alignToNormal = true;
   float normalOffset = 0.0f;
   float seed = 0.0f;

   float color[3] = { 0.9f, 0.75f, 0.5f };
   float metallic = 0.2f;
   float roughness = 0.4f;
   float opacity = 1.0f;
   int shading = 0;

private:
   void Rebuild();

   std::vector<Mat4> mTransforms;
   Mesh mEmpty;
   const void* mBuiltPointSource = nullptr;
   const void* mBuiltShape = nullptr;
   size_t mBuiltPointTris = 0;
   int mBuiltMode = -1, mBuiltMax = -1;
   float mBuiltScale = -1, mBuiltScaleRand = -1, mBuiltRotRand = -1, mBuiltSeed = -1, mBuiltOffset = -1;
   bool mBuiltAlign = false;
   int mLastCookFrame = -1;
};

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
      kWireframe, kTriangulate, kNormals, kExplode, kTwist,
      kSmooth, kMirror, kScrew,
      // Selection: one node chooses faces, the rest act only on what is chosen.
      kSelect, kDeleteSelected, kTransformSelected, kExtrudeSelected,
      kOpCount
   };

   static INode* Create() { return new GeometryOpNode(); }
   // Each operator is registered as its own spawnable node, all sharing this
   // class - the spawn menu shows ten named nodes, and the dropdown still lets
   // you switch operation without rewiring.
   static INode* CreateFor(int operation)
   {
      auto* node = new GeometryOpNode();
      node->op = operation;
      return node;
   }
   static const std::vector<std::string>& OpNames();

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   // Forwarded, not identity. An operator changes the shape, not where it sits,
   // so dropping the input's transform here made moving the upstream Geometry
   // node - or modulating its position with a Path - have no visible effect at
   // all once anything was chained after it.
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   IGeometrySource* input = nullptr;
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   size_t SelectedCount() const { return mCache.SelectedCount(); }

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

   // Smooth
   int iterations = 2;
   // Mirror
   float mirrorOffset = 0.0f;
   bool weldSeam = true;
   // Screw
   int screwSteps = 32;
   float turns = 1.0f;
   float rise = 0.0f;
   float radiusOffset = 0.5f;

   // Selection
   int selectMode = 3;          // normal, which is the most useful default
   float selectA = 0.5f;        // threshold / start / min / probability / x
   float selectB = 0.0f;        // end / max / y
   float selectC = 1.0f;        // stride / sign / z
   bool selectInvert = false;
   bool selectAppend = false;
   float selectSeed = 1.0f;
   bool keepSelected = false;   // delete: keep the selection instead of dropping it
   bool moveAlongNormals = true;
   float normalAmount = 0.2f;

   // material used when not inheriting
   float color[3] = { 0.8f, 0.82f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op); v.Bool("inherit", inheritMaterial);
      v.Float("amount", amount); v.Int("count", count);
      v.Float("offsetX", offsetX); v.Float("offsetY", offsetY); v.Float("offsetZ", offsetZ);
      v.Float("rotStep", rotStep); v.Float("scaleStep", scaleStep);
      v.Bool("radial", radial); v.Float("radius", radius);
      v.Int("levels", levels); v.Float("smooth", smooth);
      v.Float("thickness", thickness); v.Bool("keepOriginal", keepOriginal);
      v.Float("inset", inset); v.Bool("flat", flatShade); v.Bool("flip", flipNormals);
      v.Float("seed", seed); v.Int("axis", axis);
      v.Int("iterations", iterations); v.Float("mirrorOffset", mirrorOffset);
      v.Bool("weldSeam", weldSeam); v.Int("screwSteps", screwSteps);
      v.Float("turns", turns); v.Float("rise", rise); v.Float("radiusOffset", radiusOffset);
      v.Int("selectMode", selectMode); v.Float("selectA", selectA);
      v.Float("selectB", selectB); v.Float("selectC", selectC);
      v.Bool("selectInvert", selectInvert); v.Bool("selectAppend", selectAppend);
      v.Float("selectSeed", selectSeed); v.Bool("keepSelected", keepSelected);
      v.Bool("moveAlongNormals", moveAlongNormals); v.Float("normalAmount", normalAmount);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   struct Signature
   {
      int op = -1, count = 0, levels = 0, axis = 0;
      float a = 0, ox = 0, oy = 0, oz = 0, rs = 0, ss = 0, rad = 0;
      float sm = 0, th = 0, ins = 0, sd = 0;
      bool radial = false, keep = false, flat = false, flip = false;
      int iter = 0, screwSteps = 0;
      float mirrorOffset = 0, turns = 0, rise = 0, radiusOffset = 0;
      bool weldSeam = false;
      int selectMode = -1;
      float selectA = 0, selectB = 0, selectC = 0, selectSeed = 0, normalAmount = 0;
      bool selectInvert = false, selectAppend = false, keepSelected = false;
      bool moveAlongNormals = false;
      const void* upstream = nullptr;
      // The global mesh revision stamp, not a triangle count: Select changes
      // only a face mask, leaving the vertex/index count identical, so a
      // triangle-count proxy cannot see a reselection and a downstream
      // Delete/Transform/Extrude Selected kept its stale cached output.
      unsigned long long upstreamRevision = 0;
      bool operator==(const Signature& o) const
      {
         return op == o.op && count == o.count && levels == o.levels && axis == o.axis &&
                a == o.a && ox == o.ox && oy == o.oy && oz == o.oz && rs == o.rs &&
                ss == o.ss && rad == o.rad && sm == o.sm && th == o.th && ins == o.ins &&
                sd == o.sd && radial == o.radial && keep == o.keep && flat == o.flat &&
                flip == o.flip && iter == o.iter && screwSteps == o.screwSteps &&
                mirrorOffset == o.mirrorOffset && turns == o.turns && rise == o.rise &&
                radiusOffset == o.radiusOffset && weldSeam == o.weldSeam &&
                selectMode == o.selectMode && selectA == o.selectA &&
                selectB == o.selectB && selectC == o.selectC &&
                selectSeed == o.selectSeed && normalAmount == o.normalAmount &&
                selectInvert == o.selectInvert && selectAppend == o.selectAppend &&
                keepSelected == o.keepSelected &&
                moveAlongNormals == o.moveAlongNormals &&
                upstream == o.upstream && upstreamRevision == o.upstreamRevision;
      }
   };

   Signature CurrentSignature() const;

   Mesh mCache;
   Signature mBuilt;
   bool mHasBuilt = false;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

// --- Displacement ---------------------------------------------------------
// Offsets vertex positions using a texture - the true-3D counterpart to the
// `displace`/`liquify` filters in FilterDefs.cpp, which only warp a flat
// image's UVs and never move any geometry. Modelled on Blender's
// Displacement node: a raw pointer + a single ImageCable rather than the
// GeometryOpNode table, since none of the other operators need a texture
// input and giving every one of them an unused pin would be a worse fit than
// its own small class (see MaterialNode for the same geometry+texture shape).
class DisplacementNode : public INode, public IGeometrySource
{
public:
   enum Mode { kScalar = 0, kVector };

   static INode* Create() { return new DisplacementNode(); }
   ~DisplacementNode() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   // Forwarded, not identity - see GeometryOpNode::GetModelMatrix for why.
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   IGeometrySource* input = nullptr;
   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo", "texture" };
      return (slot >= 0 && slot < 2) ? kNames[slot] : nullptr;
   }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }

   int mode = kScalar;
   float strength = 1.0f;
   float midlevel = 0.5f;
   bool flatShade = false, flipNormals = false;
   bool inheritMaterial = true;

   // material used when not inheriting
   float color[3] = { 0.8f, 0.82f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Float("strength", strength); v.Float("midlevel", midlevel);
      v.Bool("flat", flatShade); v.Bool("flip", flipNormals);
      v.Bool("inherit", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   struct Signature
   {
      int mode = -1;
      float strength = 0, midlevel = 0;
      bool flat = false, flip = false;
      const void* upstream = nullptr;
      unsigned long long upstreamRevision = 0;
      // Bumped every frame the texture is patched in (see CookIfNeeded) so a
      // Noise/Voronoi feeding this keeps animating the mesh; there is no
      // per-texture revision counter to compare against instead.
      unsigned long long texGeneration = 0;
      bool operator==(const Signature& o) const
      {
         return mode == o.mode && strength == o.strength && midlevel == o.midlevel &&
                flat == o.flat && flip == o.flip &&
                upstream == o.upstream && upstreamRevision == o.upstreamRevision &&
                texGeneration == o.texGeneration;
      }
   };

   Signature CurrentSignature() const;

   Mesh mCache;
   Signature mBuilt;
   bool mHasBuilt = false;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;

   ImageCable mTextureInput;
   std::vector<float> mTexPixels;
   int mTexW = 0, mTexH = 0;
   unsigned int mReadFbo = 0;
   unsigned long long mTexGeneration = 0;
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
   unsigned long long MeshRevision() override;
   // Instances are positioned by their own transforms, so this stays identity -
   // applying the point source's transform on top would move them twice.
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;

   // Slot 0 supplies the points, slot 1 the shape stamped on them, and slot 2
   // an optional point cloud that replaces the mesh sampling entirely. A cloud
   // wins when both are patched: it is the more specific instruction.
   IGeometrySource* pointSource = nullptr;
   IGeometrySource* instanceShape = nullptr;
   IPointCloudSource* cloudSource = nullptr;

   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "points", "shape", "cloud" };
      return (slot >= 0 && slot < 3) ? kNames[slot] : nullptr;
   }

   // Per-instance colours, parallel to InstanceTransforms(). Empty when the
   // instances all share the node's own material, which is the mesh-sampling
   // case; a point cloud fills it so particles can vary.
   const std::vector<float>& InstanceColors() const { return mColors; }

   // Instanced draw data, consumed by Render3DNode. The transforms carry their
   // own stamp, separate from the mesh one: nudging the scatter re-uploads a few
   // hundred matrices without touching the instanced mesh itself.
   const std::vector<Mat4>& InstanceTransforms() const { return mTransforms; }
   size_t InstanceCount() const { return mTransforms.size(); }
   unsigned long long InstanceRevision() const { return mInstanceRevision; }
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
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("pointMode", pointMode); v.Int("maxPoints", maxPoints);
      v.Float("instanceScale", instanceScale); v.Float("scaleRandom", scaleRandom);
      v.Float("rotationRandom", rotationRandom); v.Bool("alignToNormal", alignToNormal);
      v.Float("normalOffset", normalOffset); v.Float("seed", seed);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void Rebuild();

   std::vector<Mat4> mTransforms;
   std::vector<float> mColors; // rgb triples, or empty for a uniform material
   unsigned long long mInstanceRevision = 0;
   Mesh mEmpty;
   const void* mBuiltPointSource = nullptr;
   const void* mBuiltShape = nullptr;
   const void* mBuiltCloud = nullptr;
   unsigned long long mBuiltCloudRevision = 0;
   unsigned long long mBuiltPointRevision = 0;
   unsigned long long mBuiltShapeRevision = 0;
   int mBuiltMode = -1, mBuiltMax = -1;
   float mBuiltScale = -1, mBuiltScaleRand = -1, mBuiltRotRand = -1, mBuiltSeed = -1, mBuiltOffset = -1;
   bool mBuiltAlign = false;
   int mLastCookFrame = -1;
};

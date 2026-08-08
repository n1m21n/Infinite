#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// --- Group ------------------------------------------------------------------
// A resizable backdrop that maps onto the node-editor library's own notion of
// a "group" (see ax::NodeEditor::Group()): any node whose bounds currently sit
// inside its rectangle is dragged along when the group's header is dragged,
// and it renders with its own highlight color so a cluster of nodes reads as
// one unit. The drag-together behavior is purely geometric and lives entirely
// in the library; what main.cpp adds on top is an explicit member list the box
// auto-fits itself to, so dragging a node towards the edge stretches the box
// and dragging it back shrinks it again, and a node can never quietly fall out
// of a group by being moved. That bookkeeping (which node indices belong, the
// last measured header height) lives in main.cpp next to the drawing code, not
// here, since it is keyed by GraphNode::index, which this header knows nothing
// about.
class GroupNode : public INode
{
public:
   static INode* Create() { return new GroupNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   std::string label = "Group";
   float width = 320.0f;
   float height = 220.0f;
   float color[3] = { 0.45f, 0.65f, 0.95f };

   // Rename-in-place state and the last measured header height. Neither is
   // saveable data - VisitParams deliberately leaves them out - they are UI
   // state that starts fresh every time a patch loads.
   bool renaming = false;
   bool renameJustStarted = false;
   float headerH = 28.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("label", label);
      v.Float("width", width); v.Float("height", height);
      v.Color("color", color);
   }
};

// --- Null (2D) ----------------------------------------------------------
// Reroute point for tidying cable runs. It owns no framebuffer and does no
// work: it simply reports its input's texture as its own, so inserting one
// costs nothing at all rather than costing a full-screen copy.
class NullNode : public INode
{
public:
   static INode* Create() { return new NullNode(); }
   virtual ~NullNode() {}

   unsigned int GetOutputTexture() override
   {
      INode* src = mInput.Resolved();
      return src ? src->GetOutputTexture() : 0;
   }
   int GetOutputWidth() const override { return mInput.Width(); }
   int GetOutputHeight() const override { return mInput.Height(); }

   void CookIfNeeded(int frameId) override
   {
      if (mLastCookFrame == frameId)
         return;
      mLastCookFrame = frameId;
      mInput.Pull(frameId);
   }

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int) const override { return "in"; }

private:
   ImageCable mInput;
   int mLastCookFrame = -1;
};

// --- Viewport -----------------------------------------------------------
// A Null that draws big. Same zero-cost pass-through, but the editor gives it a
// large canvas so a stage of a patch can actually be inspected mid-graph
// instead of being judged from a thumbnail.
class ViewportNode : public NullNode
{
public:
   static INode* Create() { return new ViewportNode(); }
};

// --- Null 3D ------------------------------------------------------------
// The same idea for geometry cables. Everything is forwarded, including the
// mesh revision stamp: inventing a stamp of its own would make the renderer
// re-upload the mesh every frame and quietly undo the upload cache.
class Null3DNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new Null3DNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override { return input ? input->MeshRevision() : 0; }
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override
   {
      return input ? input->GetMaterial() : Material();
   }
   unsigned int GetSurfaceTexture() override
   {
      return input ? input->GetSurfaceTexture() : 0;
   }
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const;

private:
   Mesh mEmpty;
   int mLastCookFrame = -1;
};

// --- Material -----------------------------------------------------------
// Overrides the material of whatever geometry passes through it, so a surface
// can be authored once and shared by several shapes.
//
// Modelled as a pass-through in the geometry chain rather than as a material
// cable patched into each shape. That needs no new cable type and no new input
// slot on every geometry node, and it composes: a Material placed after a
// Geometry Op restyles everything upstream of it in one move.
class MaterialNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new MaterialNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override { return input ? input->MeshRevision() : 0; }
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;

   unsigned int GetMaterialTexture(int map) override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   ImageCable& TextureInput() { return mMaps[kMapAlbedo]; }
   // Slot 0 is geometry; slots 1..5 are the material channels, in MaterialMap
   // order, so the existing 2D generators can author a whole surface.
   ImageCable& MapInput(int map) { return mMaps[map]; }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo", "albedo", "roughness", "metallic",
                                      "normal", "ao" };
      return (slot >= 0 && slot < 6) ? kNames[slot] : nullptr;
   }
   size_t TriangleCount() const;

   float normalStrength = 1.0f;

   float color[3] = { 0.85f, 0.86f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;


   void VisitParams(ParamVisitor& v) override
   {
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("normalStrength", normalStrength);
   }
private:
   ImageCable mMaps[kMapCount];
   Mesh mEmpty;
   int mLastCookFrame = -1;
};

// --- Join Geometry ------------------------------------------------------
// Merges several meshes into one, so an assembly can be smoothed, scattered or
// materialled as a single thing rather than one piece at a time.
//
// Each input's model matrix is baked into the merged vertices. Without that the
// parts would all collapse onto the origin, since the combined mesh can only
// carry one transform of its own.
class JoinGeometryNode : public INode, public IGeometrySource
{
public:
   static const int kSlots = 4;

   // Merge is a plain concatenation; the rest are real CSG. Registered as
   // separate nodes too, since "union" and "difference" are what someone
   // searches for, not "join with the mode dropdown set to difference".
   enum Mode { kMerge = 0, kUnion, kIntersect, kDifference, kModeCount };
   static const std::vector<std::string>& ModeNames();

   static INode* Create() { return new JoinGeometryNode(); }
   static INode* CreateFor(int mode)
   {
      auto* node = new JoinGeometryNode();
      node->mode = mode;
      return node;
   }

   int mode = kMerge;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;

   IGeometrySource* inputs[kSlots] = { nullptr, nullptr, nullptr, nullptr };
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo A", "geo B", "geo C", "geo D" };
      return (slot >= 0 && slot < kSlots) ? kNames[slot] : nullptr;
   }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   int ConnectedCount() const;

   // Which input's material the merged mesh wears. A merged mesh is one draw
   // call, so it can only have one material; this picks which.
   int materialFrom = 0;
   bool inheritMaterial = true;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float uniformScale = 1.0f;

   float color[3] = { 0.85f, 0.86f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode);
      v.Int("materialFrom", materialFrom); v.Bool("inherit", inheritMaterial);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("scale", uniformScale);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   unsigned long long mMeshRevision = 0;
   const void* mBuiltInputs[kSlots] = { nullptr, nullptr, nullptr, nullptr };
   unsigned long long mBuiltRevisions[kSlots] = { 0, 0, 0, 0 };
   int mBuiltMode = -1;
   // The transforms are baked into the merged vertices, so a change to one has
   // to trigger a rebuild exactly like a change to a mesh would. Keying only on
   // the mesh stamp meant moving or scaling an input did nothing at all.
   Mat4 mBuiltMatrices[kSlots];
   int mLastCookFrame = -1;
};

// --- Metaballs ----------------------------------------------------------
// Blobs that merge into one another rather than intersecting, surfaced with
// marching cubes over a summed field. Optionally takes a point cloud, so a
// particle system can be surfaced as liquid.
class MetaBallNode : public INode, public IGeometrySource
{
public:
   static constexpr int kMaxBalls = 8;

   static INode* Create() { return new MetaBallNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;

   // A cloud drives the balls when patched, so particles can be surfaced.
   IPointCloudSource* cloudSource = nullptr;
   const char* InputLabel(int) const override { return "cloud"; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   size_t BallCount() const { return mBallCount; }

   int ballCount = 3;
   int resolution = 40;
   float threshold = 8.0f;
   float bounds = 1.5f;
   float radius = 0.35f;
   float spread = 0.5f;
   float spin = 0.15f;      // orbit per beat, so they move without modulation
   int maxFromCloud = 16;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float uniformScale = 1.0f;

   float color[3] = { 0.55f, 0.75f, 0.95f };
   float metallic = 0.1f;
   float roughness = 0.2f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("ballCount", ballCount); v.Int("resolution", resolution);
      v.Float("threshold", threshold); v.Float("bounds", bounds);
      v.Float("radius", radius); v.Float("spread", spread); v.Float("spin", spin);
      v.Int("maxFromCloud", maxFromCloud);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("scale", uniformScale);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void RebuildIfNeeded();

   Mesh mCache;
   size_t mBallCount = 0;
   unsigned long long mMeshRevision = 0;
   int mBuiltCount = -1, mBuiltRes = -1, mBuiltMax = -1;
   float mBuiltThreshold = -1, mBuiltBounds = -1, mBuiltRadius = -1, mBuiltSpread = -1;
   double mBuiltBeat = -1.0;
   const void* mBuiltCloud = nullptr;
   unsigned long long mBuiltCloudRevision = 0;
   int mLastCookFrame = -1;
};

// --- Mesh to Points -----------------------------------------------------
// Samples a mesh at its vertices, edge midpoints or face centres and emits a
// small quad at each. The sampling itself already existed inside Instance on
// Points; this exposes it as geometry in its own right, so a point set can be
// looked at, operated on, or fed onwards.
class MeshToPointsNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new MeshToPointsNode(); }
   // Registered three times under Points/Edges/Faces names sharing one class,
   // the same way the geometry operators are.
   static INode* CreateFor(int sampleMode)
   {
      auto* node = new MeshToPointsNode();
      node->mode = sampleMode;
      return node;
   }
   static const std::vector<std::string>& ModeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   // Forwarded for the same reason the geometry operators forward it: sampling
   // a mesh does not relocate it.
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

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   size_t PointCount() const { return mPointCount; }

   int mode = 0;          // vertices / edges / faces
   int maxPoints = 4000;
   float pointSize = 0.03f;

   bool inheritMaterial = true;
   float color[3] = { 0.95f, 0.8f, 0.45f };
   float metallic = 0.1f;
   float roughness = 0.5f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;


   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("maxPoints", maxPoints);
      v.Float("pointSize", pointSize); v.Bool("inherit", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }
private:
   void RebuildIfNeeded();

   Mesh mCache;
   size_t mPointCount = 0;
   unsigned long long mMeshRevision = 0;

   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   int mBuiltMode = -1, mBuiltMax = -1;
   float mBuiltSize = -1.0f;
   int mLastCookFrame = -1;
};

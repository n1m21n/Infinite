#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// --- Comment --------------------------------------------------------------
// A free-floating annotation. Not part of the signal graph at all - no
// inputs, no outputs, CookIfNeeded does nothing - so it can be dropped
// anywhere on the canvas to label a section of a patch without affecting
// anything downstream.
class CommentNode : public INode
{
public:
   static INode* Create() { return new CommentNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   // Empty on purpose: a fresh comment shows the prompt to write one rather
   // than the word "Comment", which the node's own title already says.
   std::string text;
   float width = 260.0f;
   float height = 140.0f;
   float color[3] = { 0.95f, 0.85f, 0.45f };

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("text", text);
      v.Float("width", width); v.Float("height", height);
      v.Color("color", color);
   }
};

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
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   unsigned int GetSurfaceTexture() override
   {
      return input ? input->GetSurfaceTexture() : 0;
   }
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   // A null is a no-op on the cable: forwarding these two keeps an upstream
   // InstanceOnPoints (and a Transform wrapping it) visible to the consumers
   // that walk the passthrough chain, instead of collapsing the scatter to a
   // single stamp mesh.
   IGeometrySource* PassthroughSource() const override { return input; }
   Mat4 GetInstanceGroupMatrix() const override
   {
      return input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return input ? input->InstanceTransformOverride() : nullptr;
   }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
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
   unsigned long long SurfaceTextureRevision() const override;
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* PassthroughSource() const override { return input; }
   // Forwarded alongside PassthroughSource: a node that claims to pass an
   // instancer through has to pass the wrapping Transform's group matrix
   // through with it, or the scatter draws at the origin.
   Mat4 GetInstanceGroupMatrix() const override
   {
      return input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return input ? input->InstanceTransformOverride() : nullptr;
   }
   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   ImageCable& TextureInput() { return mMaps[kMapAlbedo]; }
   // Slot 0 is geometry; slots 1..5 are the material channels, in MaterialMap
   // order, so the existing 2D generators can author a whole surface.
   ImageCable& MapInput(int map) { return mMaps[map]; }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo", "albedo", "roughness", "metallic",
                                      "normal", "ao", "emission", "clearcoat", "sheen" };
      return (slot >= 0 && slot < (1 + kMapCount)) ? kNames[slot] : nullptr;
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
   // Fresnel/dielectric response, independent of metallic/roughness - 1.5
   // matches glass and Blender's Principled BSDF default.
   float ior = 1.5f;
   // See-through / refractive path. Distinct from `opacity`, which stays a
   // cutout/dither alpha - transmission is the actual glass-like blend.
   // Screen-space approximated (see Render3D's transmission pass), not
   // raytraced: it will not look correct through thick or overlapping
   // transmissive geometry.
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   // Dielectric specular reflectance at normal incidence, independent of
   // metallic - Blender's "Specular" / "Specular IOR Level" slider.
   float specular = 0.5f;
   // Second, untinted Fresnel-weighted specular lobe layered on top - car
   // paint / lacquer.
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   // Fake subsurface scattering via wrap lighting (N.L extended past the
   // terminator, tinted by subsurfaceColor) - not true multi-scatter.
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;
   float sheen = 0.0f;
   float sheenColor[3] = { 1.0f, 1.0f, 1.0f };
   float sheenRoughness = 0.5f;
   float iridescence = 0.0f;
   float iridescenceIor = 1.33f;
   float iridescenceThickness = 400.0f;
   float anisotropy = 0.0f;
   float anisotropyRotation = 0.0f;
   float dispersion = 0.0f;
   float alphaCutoff = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
      v.Float("sheen", sheen); v.Color("sheenColor", sheenColor);
      v.Float("sheenRoughness", sheenRoughness);
      v.Float("iridescence", iridescence); v.Float("iridescenceIor", iridescenceIor);
      v.Float("iridescenceThickness", iridescenceThickness);
      v.Float("anisotropy", anisotropy); v.Float("anisotropyRotation", anisotropyRotation);
      v.Float("dispersion", dispersion); v.Float("alphaCutoff", alphaCutoff);
      v.Float("normalStrength", normalStrength);
   }
private:
   ImageCable mMaps[kMapCount];
   Mesh mEmpty;
   int mLastCookFrame = -1;
};

// --- Mapping --------------------------------------------------------------
// Chooses which coordinates a material's maps sample - the mesh's own UV, or
// a 3D coordinate generated from the surface itself - and offsets, rotates
// and scales them before lookup. The Blender equivalent is a Texture
// Coordinate node feeding a Mapping node feeding an Image Texture; here that
// is one node, since every material map already samples through the same
// IGeometrySource chain rather than through a separate texture-node graph.
//
// A pass-through in the geometry chain, like Null 3D and Material: it forwards
// the mesh and material untouched and only changes how MaterialNode's maps -
// or the plain GetSurfaceTexture() path - are looked up on the surface. Placed
// anywhere upstream of a Material or Render 3D, so an image or a Noise/Ramp/
// Formula texture can be tiled, offset or object-space-projected without
// re-authoring the geometry's own UVs.
class MappingNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new MappingNode(); }

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
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const;

   int space = kMapSpaceUv;
   float translateX = 0.0f, translateY = 0.0f, translateZ = 0.0f;
   float rotateX = 0.0f, rotateY = 0.0f, rotateZ = 0.0f;
   float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
   float triplanarBlend = 0.0f;

   static const std::vector<std::string>& SpaceNames();

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("space", space);
      v.Float("translateX", translateX); v.Float("translateY", translateY); v.Float("translateZ", translateZ);
      v.Float("rotateX", rotateX); v.Float("rotateY", rotateY); v.Float("rotateZ", rotateZ);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY); v.Float("scaleZ", scaleZ);
      v.Float("triplanarBlend", triplanarBlend);
   }

private:
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
   unsigned long long SurfaceTextureRevision() const override;
   MappingTransform GetMappingTransform() const override;

   INode* BypassSource() override
   {
      for (int i = 0; i < kSlots; i++)
         if (inputs[i] != nullptr)
            return dynamic_cast<INode*>(inputs[i]);
      return nullptr;
   }

   IGeometrySource* inputs[kSlots] = { nullptr, nullptr, nullptr, nullptr };
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &inputs[slot] : nullptr;
   }
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
   // Fresnel/dielectric response, independent of metallic/roughness - 1.5
   // matches glass and Blender's Principled BSDF default.
   float ior = 1.5f;
   // See-through / refractive path. Distinct from `opacity`, which stays a
   // cutout/dither alpha - transmission is the actual glass-like blend.
   // Screen-space approximated (see Render3D's transmission pass), not
   // raytraced: it will not look correct through thick or overlapping
   // transmissive geometry.
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   // Dielectric specular reflectance at normal incidence, independent of
   // metallic - Blender's "Specular" / "Specular IOR Level" slider.
   float specular = 0.5f;
   // Second, untinted Fresnel-weighted specular lobe layered on top - car
   // paint / lacquer.
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   // Fake subsurface scattering via wrap lighting (N.L extended past the
   // terminator, tinted by subsurfaceColor) - not true multi-scatter.
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;

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
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
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
   // Read via GetPointCloud() - a plain mesh source with no point cloud of its
   // own returns nullptr, same as an unconnected pin.
   IGeometrySource* cloudSource = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &cloudSource : nullptr; }
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
   // Fresnel/dielectric response, independent of metallic/roughness - 1.5
   // matches glass and Blender's Principled BSDF default.
   float ior = 1.5f;
   // See-through / refractive path. Distinct from `opacity`, which stays a
   // cutout/dither alpha - transmission is the actual glass-like blend.
   // Screen-space approximated (see Render3D's transmission pass), not
   // raytraced: it will not look correct through thick or overlapping
   // transmissive geometry.
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   // Dielectric specular reflectance at normal incidence, independent of
   // metallic - Blender's "Specular" / "Specular IOR Level" slider.
   float specular = 0.5f;
   // Second, untinted Fresnel-weighted specular lobe layered on top - car
   // paint / lacquer.
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   // Fake subsurface scattering via wrap lighting (N.L extended past the
   // terminator, tinted by subsurfaceColor) - not true multi-scatter.
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;

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
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
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

   // The same samples GetMesh() bakes into billboard quads, as particles a
   // renderer can draw as camera-facing sprites instead. Built together in
   // RebuildIfNeeded so the two never disagree.
   const std::vector<Particle>& GetPoints();
   unsigned long long PointRevision();
   const std::vector<Particle>* GetPointCloud() override { return &GetPoints(); }
   unsigned long long PointCloudRevision() override { return PointRevision(); }

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
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   size_t PointCount() const { return mPointCount; }

   int mode = 0;          // vertices / edges / faces
   int maxPoints = 4000;
   float pointSize = 0.03f;
   bool weld = true;
   // Edge mode only - see MeshOps::ToPoints. 0 disables the dissolve filter.
   float dissolveAngleDegrees = 1.0f;

   bool inheritMaterial = true;
   float color[3] = { 0.95f, 0.8f, 0.45f };
   float metallic = 0.1f;
   float roughness = 0.5f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;
   // Fresnel/dielectric response, independent of metallic/roughness - 1.5
   // matches glass and Blender's Principled BSDF default.
   float ior = 1.5f;
   // See-through / refractive path. Distinct from `opacity`, which stays a
   // cutout/dither alpha - transmission is the actual glass-like blend.
   // Screen-space approximated (see Render3D's transmission pass), not
   // raytraced: it will not look correct through thick or overlapping
   // transmissive geometry.
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   // Dielectric specular reflectance at normal incidence, independent of
   // metallic - Blender's "Specular" / "Specular IOR Level" slider.
   float specular = 0.5f;
   // Second, untinted Fresnel-weighted specular lobe layered on top - car
   // paint / lacquer.
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   // Fake subsurface scattering via wrap lighting (N.L extended past the
   // terminator, tinted by subsurfaceColor) - not true multi-scatter.
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;


   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("maxPoints", maxPoints);
      v.Float("pointSize", pointSize); v.Bool("weld", weld);
      v.Float("dissolveAngleDegrees", dissolveAngleDegrees);
      v.Bool("inherit", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
   }
private:
   void RebuildIfNeeded();

   Mesh mCache;
   std::vector<Particle> mPoints;
   size_t mPointCount = 0;
   unsigned long long mMeshRevision = 0;

   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltUpstream = 0;
   int mBuiltMode = -1, mBuiltMax = -1;
   float mBuiltSize = -1.0f;
   bool mBuiltWeld = true;
   float mBuiltDissolve = -1.0f;
   int mLastCookFrame = -1;
};

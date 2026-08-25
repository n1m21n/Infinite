#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "Palette.h"

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
      //
      // kDeleteSelected/kTransformSelected/kExtrudeSelected are deprecated -
      // kept at their saved-patch indices only so old `op` integers keep
      // meaning something; ReloadDerivedState remaps them to
      // (kDelete/kTransform/kExtrude, selectionOnly=true) on load and they no
      // longer appear in the spawn menu or the operation dropdown. Do not
      // reuse or reorder these four indices - see docs/plans/phase4-selection-as-input.md.
      kSelect, kDeleteSelected, kTransformSelected, kExtrudeSelected,
      // General form of kDeleteSelected: with `selectionOnly` off it deletes
      // every face, which is a legitimate (if unexciting) way to empty a
      // mesh; turn `selectionOnly` on to restrict it to a Select node's mask.
      kDelete,
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
   // False for the three deprecated `*Selected` ops (see the Op enum comment)
   // - they stay registered under NodeFactory for old patches to resolve by
   // name, but shouldn't appear as spawnable choices going forward.
   static bool IsSpawnable(int op);
   // Rewrites a deprecated `*Selected` op to its general form + selectionOnly,
   // in place. A no-op for any other op. Called from ReloadDerivedState after
   // both patch load and copy/paste, so a saved integer never has to be
   // reinterpreted anywhere else.
   void MigrateDeprecatedOp();

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
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   IGeometrySource* PassthroughSource() const override { return input; }
   // When this node (or the chain of GeometryOpNodes it's wired through) sits
   // downstream of an InstanceOnPoints and op is kTransform, GetMesh() leaves
   // the stamp mesh alone and this returns the move/rotate/scale as a matrix
   // instead - see the kTransform comment in GetMesh() for why.
   Mat4 GetInstanceGroupMatrix() const override;
   // Shared by kTransform and kTransformSelected: move/rotate/scale built
   // from the offsetX/Y/Z, rotX/Y/Z, scaleX/Y/Z fields the UI exposes as
   // "move x/y/z", "rotate x/y/z", "scale x/y/z" for those two operations.
   Mat4 TransformMatrix() const;

   // True when this op sits downstream of an Instance on Points, i.e.
   // GetMesh() is operating on the shared stamp mesh rather than on a
   // realized copy per instance (every op except kTransform, which moves the
   // whole instanced group instead - see the kTransform case in GetMesh()).
   // Lets the UI state which frame of reference is in effect.
   bool ActsOnInstanceStamp() const;
   // Instance count of the upstream InstanceOnPoints when
   // ActsOnInstanceStamp() is true, so the UI can quote it; 0 otherwise.
   size_t UpstreamInstanceCount() const;

   // Non-null only when op == kSelect and this node sits downstream of an
   // InstanceOnPoints: the per-instance mask built in GetMesh() (parallel to
   // the instancer's InstanceTransforms()), instead of the usual per-face
   // Mesh::faceMask. Any other op forwards whatever it received from input,
   // same as PassthroughSource. Reflects whatever GetMesh() last built, so
   // callers need to have called GetMesh()/MeshRevision() on this node first
   // this frame - same ordering every other passthrough accessor here relies on.
   const std::vector<unsigned char>* InstanceSelection() const override;
   unsigned long long InstanceSelectionRevision() const override;
   // Non-null only when op is kDelete or kTransform with selectionOnly on and
   // WrapsInstancer(input): the instance transform list after this node's
   // edit (deleted/moved entries), replacing what the instancer would
   // otherwise hand back. See GetMesh()'s kDelete/kTransform cases.
   const std::vector<Mat4>* InstanceTransformOverride() const override;

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   size_t SelectedCount() const { return mCache.SelectedCount(); }

   int op = kArray;
   bool inheritMaterial = true;
   // When set and the incoming mesh carries a selection (Mesh::faceMask
   // non-empty), this operator only affects selected faces and passes the
   // rest through untouched. Ignored when the input has no selection, so
   // nothing changes for a chain with no Select node in it. Hidden in the
   // params panel for operators where "only these faces" isn't well defined
   // - see DrawGeometryOpParams and docs/plans/phase4-selection-as-input.md.
   bool selectionOnly = false;

   // shared
   float amount = 1.0f;
   int count = 5;
   float offsetX = 0.6f, offsetY = 0.0f, offsetZ = 0.0f;
   float rotStep = 0.0f, scaleStep = 1.0f;
   // Transform / Transform Selected only: per-axis rotate and scale, used by
   // TransformMatrix() instead of the single-axis rotStep/scaleStep above
   // (those stay Array-only, where a per-step Y rotation and uniform scale
   // are what "rot / step" and "scale / step" mean).
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
   // Transform / Transform Selected only: extra Y-axis rotation per beat, on
   // top of rotY, so a shape can spin on its own - same idea as GeometryNode's
   // spinY. TransformMatrix() bakes this into the mesh cache when not
   // downstream of an instancer, so CurrentSignature() has to fold the live
   // beat count in whenever spin != 0, or the baked rotation would freeze at
   // whatever beat it happened to be built on.
   float spin = 0.0f;
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
      v.Int("op", op); v.Bool("inherit", inheritMaterial);
      v.Bool("selectionOnly", selectionOnly);
      v.Float("amount", amount); v.Int("count", count);
      v.Float("offsetX", offsetX); v.Float("offsetY", offsetY); v.Float("offsetZ", offsetZ);
      v.Float("rotStep", rotStep); v.Float("scaleStep", scaleStep);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY); v.Float("scaleZ", scaleZ);
      v.Float("spin", spin);
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
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
   }

private:
   struct Signature
   {
      int op = -1, count = 0, levels = 0, axis = 0;
      float a = 0, ox = 0, oy = 0, oz = 0, rs = 0, ss = 0, rad = 0;
      float sm = 0, th = 0, ins = 0, sd = 0;
      float rx = 0, ry = 0, rz = 0, sx = 0, sy = 0, sz = 0;
      // Only meaningful (and only ever set) while spin != 0 - see the comment
      // on GeometryOpNode::spin. Zero the rest of the time so two builds with
      // spin == 0 still compare equal regardless of when each ran.
      float spinBeats = 0;
      bool radial = false, keep = false, flat = false, flip = false;
      bool selectionOnly = false;
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
      // The raw instancer's InstanceRevision() when WrapsInstancer(input) -
      // needed because InstanceOnPointsNode::MeshRevision() only reflects the
      // stamp shape's revision, not a scatter/instance-transform change, so
      // upstreamRevision alone is blind to "the points moved" and a
      // downstream instance-domain Select/Delete/Transform would keep a mask
      // or override built against the old placements.
      unsigned long long upstreamInstanceRevision = 0;
      bool operator==(const Signature& o) const
      {
         return op == o.op && count == o.count && levels == o.levels && axis == o.axis &&
                a == o.a && ox == o.ox && oy == o.oy && oz == o.oz && rs == o.rs &&
                ss == o.ss && rad == o.rad && sm == o.sm && th == o.th && ins == o.ins &&
                sd == o.sd && radial == o.radial && keep == o.keep && flat == o.flat &&
                selectionOnly == o.selectionOnly &&
                flip == o.flip && iter == o.iter && screwSteps == o.screwSteps &&
                mirrorOffset == o.mirrorOffset && turns == o.turns && rise == o.rise &&
                radiusOffset == o.radiusOffset && weldSeam == o.weldSeam &&
                selectMode == o.selectMode && selectA == o.selectA &&
                selectB == o.selectB && selectC == o.selectC &&
                selectSeed == o.selectSeed && normalAmount == o.normalAmount &&
                selectInvert == o.selectInvert && selectAppend == o.selectAppend &&
                keepSelected == o.keepSelected &&
                moveAlongNormals == o.moveAlongNormals &&
                rx == o.rx && ry == o.ry && rz == o.rz &&
                sx == o.sx && sy == o.sy && sz == o.sz && spinBeats == o.spinBeats &&
                upstream == o.upstream && upstreamRevision == o.upstreamRevision &&
                upstreamInstanceRevision == o.upstreamInstanceRevision;
      }
   };

   Signature CurrentSignature() const;

   Mesh mCache;
   Signature mBuilt;
   bool mHasBuilt = false;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;

   // Populated by GetMesh() only for op == kSelect downstream of an
   // instancer; empty otherwise (InstanceSelection() forwards input's in
   // that case instead of pointing at this, so an always-empty-but-non-null
   // vector here can't be mistaken for a real all-unselected mask).
   std::vector<unsigned char> mInstanceSelection;
   unsigned long long mInstanceSelectionRevision = 0;
   // Populated by GetMesh() only for kDelete/kTransform with selectionOnly
   // downstream of an instancer - see InstanceTransformOverride().
   std::vector<Mat4> mInstanceTransformOverride;
   bool mHasInstanceTransformOverride = false;
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
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   // Displacement moves the stamp's vertices, it doesn't build the mesh from
   // scratch - so an upstream InstanceOnPoints stays visible to the chain
   // walk (displace the stamp, then scatter it), group matrix included.
   IGeometrySource* PassthroughSource() const override { return input; }
   Mat4 GetInstanceGroupMatrix() const override
   {
      return input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   }
   // Forwarded alongside PassthroughSource/GetInstanceGroupMatrix above -
   // Displacement never selects or edits instances itself.
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
   // See GeometryOpNode::selectionOnly - same convention, restricted here to
   // vertices touching a selected face (MeshOps::VertexSelectionFromFaces),
   // since displacement is inherently per-vertex.
   bool selectionOnly = false;

   // material used when not inheriting
   float color[3] = { 0.8f, 0.82f, 0.9f };
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
      v.Int("mode", mode); v.Float("strength", strength); v.Float("midlevel", midlevel);
      v.Bool("flat", flatShade); v.Bool("flip", flipNormals);
      v.Bool("inherit", inheritMaterial);
      v.Bool("selectionOnly", selectionOnly);
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
   struct Signature
   {
      int mode = -1;
      float strength = 0, midlevel = 0;
      bool flat = false, flip = false;
      bool selectionOnly = false;
      const void* upstream = nullptr;
      unsigned long long upstreamRevision = 0;
      // Bumped when the texture's pixel content actually changes (see
      // CookIfNeeded), so a static texture doesn't fake a change every frame
      // while a Noise/Voronoi with real animation still keeps driving the mesh.
      unsigned long long texGeneration = 0;
      bool operator==(const Signature& o) const
      {
         return mode == o.mode && strength == o.strength && midlevel == o.midlevel &&
                flat == o.flat && flip == o.flip && selectionOnly == o.selectionOnly &&
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
   unsigned long long mTexHash = 0;
   bool mHasTexHash = false;
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
   unsigned int GetMaterialTexture(int map) override
   {
      return instanceShape ? instanceShape->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return instanceShape ? instanceShape->SurfaceTextureRevision() : 0;
   }

   // Slot 0 supplies the points, slot 1 the shape stamped on them, and slot 2
   // an optional point cloud that replaces the mesh sampling entirely. A cloud
   // wins when both are patched: it is the more specific instruction.
   IGeometrySource* pointSource = nullptr;
   IGeometrySource* instanceShape = nullptr;
   IGeometrySource* cloudSource = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      if (slot == 0) return &pointSource;
      if (slot == 1) return &instanceShape;
      if (slot == 2) return &cloudSource;
      return nullptr;
   }

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

   bool inheritMaterial = true;

   float color[3] = { 0.9f, 0.75f, 0.5f };
   float metallic = 0.2f;
   float roughness = 0.4f;
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
      v.Int("pointMode", pointMode); v.Int("maxPoints", maxPoints);
      v.Float("instanceScale", instanceScale); v.Float("scaleRandom", scaleRandom);
      v.Float("rotationRandom", rotationRandom); v.Bool("alignToNormal", alignToNormal);
      v.Float("normalOffset", normalOffset); v.Float("seed", seed);
      v.Bool("inheritMaterial", inheritMaterial);
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
   // Baked into every instance transform in Rebuild(), so a pure transform edit
   // (no revision bump - see GeometryNode::GetModelMatrix) still has to be
   // caught here rather than falling through the revision checks above.
   Mat4 mBuiltPointModel;
   Mat4 mBuiltShapeModel;
   int mBuiltMode = -1, mBuiltMax = -1;
   float mBuiltScale = -1, mBuiltScaleRand = -1, mBuiltRotRand = -1, mBuiltSeed = -1, mBuiltOffset = -1;
   bool mBuiltAlign = false;
   int mLastCookFrame = -1;
};

// --- Set Color -------------------------------------------------------------
// Writes per-element colour: Mesh::vertexColor for a mesh, Particle::r/g/b for
// a point cloud, whichever the input actually carries (both, if it carries
// both - see MeshToPointsNode). Its own class rather than a GeometryOpNode
// row, same reasoning as DisplacementNode/WrapNode: it needs two extra pins
// (texture, palette) the table's single geo input doesn't have room for.
//
// Deliberately NOT a generic named-attribute writer - see docs/plans -
// "source" is a fixed dropdown, not free text.
class SetColorNode : public INode, public IGeometrySource
{
public:
   enum Source { kFlat = 0, kPosition, kNormal, kIndex, kRandom, kPalette, kTexture, kSourceCount };

   static INode* Create() { return new SetColorNode(); }
   ~SetColorNode() override;
   static const std::vector<std::string>& SourceNames();

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   const std::vector<Particle>* GetPointCloud() override;
   unsigned long long PointCloudRevision() override;
   // Forwarded, not identity - see DisplacementNode for why.
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
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
   IGeometrySource* PassthroughSource() const override { return input; }
   // Forwarded alongside PassthroughSource - see MaterialNode for why the two
   // have to travel together.
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
   IPaletteSource* paletteInput = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo", "texture", "palette" };
      return (slot >= 0 && slot < 3) ? kNames[slot] : nullptr;
   }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }

   int source = kFlat;
   float flatColor[3] = { 1.0f, 1.0f, 1.0f };
   // Ramp endpoints for Position/Normal/Index.
   float rampA[3] = { 0.0f, 0.0f, 0.0f };
   float rampB[3] = { 1.0f, 1.0f, 1.0f };
   float seed = 0.0f;                  // Random
   int paletteOffset = 0;              // Palette: rotates which swatch element 0 gets

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("source", source);
      v.Color("flatColor", flatColor);
      v.Color("rampA", rampA); v.Color("rampB", rampB);
      v.Float("seed", seed);
      v.Int("paletteOffset", paletteOffset);
   }

private:
   struct Signature
   {
      int source = -1;
      float flatColor[3] = { 0, 0, 0 };
      float rampA[3] = { 0, 0, 0 };
      float rampB[3] = { 0, 0, 0 };
      float seed = 0;
      int paletteOffset = 0;
      const void* upstream = nullptr;
      unsigned long long upstreamMeshRevision = 0;
      unsigned long long upstreamCloudRevision = 0;
      unsigned long long texGeneration = 0;
      unsigned long long paletteHash = 0;
      bool operator==(const Signature& o) const
      {
         return source == o.source && seed == o.seed && paletteOffset == o.paletteOffset &&
                flatColor[0] == o.flatColor[0] && flatColor[1] == o.flatColor[1] && flatColor[2] == o.flatColor[2] &&
                rampA[0] == o.rampA[0] && rampA[1] == o.rampA[1] && rampA[2] == o.rampA[2] &&
                rampB[0] == o.rampB[0] && rampB[1] == o.rampB[1] && rampB[2] == o.rampB[2] &&
                upstream == o.upstream && upstreamMeshRevision == o.upstreamMeshRevision &&
                upstreamCloudRevision == o.upstreamCloudRevision &&
                texGeneration == o.texGeneration && paletteHash == o.paletteHash;
      }
   };

   Signature CurrentSignature() const;
   void Rebuild();

   Mesh mCache;
   std::vector<Particle> mPointCache;
   bool mHasPointCache = false;
   Signature mBuilt;
   bool mHasBuilt = false;
   unsigned long long mMeshRevision = 0;
   unsigned long long mCloudRevision = 0;
   int mLastCookFrame = -1;

   ImageCable mTextureInput;
   std::vector<float> mTexPixels;
   int mTexW = 0, mTexH = 0;
   unsigned int mReadFbo = 0;
   unsigned long long mTexGeneration = 0;
   unsigned long long mTexHash = 0;
   bool mHasTexHash = false;
};

// --- Wrap (Shrinkwrap-style "Nearest Surface Point") ---------------------
// Conforms one mesh onto the surface of another: every source vertex moves to
// the closest point on the target's surface. Its own class, not a row in
// GeometryOpNode's table, because it needs two geometry inputs rather than
// the table's single geo (+ optional scalar) shape - the same reason
// DisplacementNode got its own class for a texture input.
class WrapNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new WrapNode(); }
   INode* BypassSource() override { return dynamic_cast<INode*>(sourceInput); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   // World transforms are baked into the output mesh (see MeshOps::Wrap), so
   // this stays identity - same reasoning as InstanceOnPointsNode.
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned int GetMaterialTexture(int map) override { return sourceInput ? sourceInput->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override
   {
      return sourceInput ? sourceInput->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override { return sourceInput ? sourceInput->GetMappingTransform() : MappingTransform(); }
   // The source mesh is the stamp; wrapping it onto the target is a stamp-level
   // op like every other one, so an upstream InstanceOnPoints keeps scattering
   // the wrapped mesh. Never targetInput - that's the thing being wrapped onto,
   // not where this node's mesh comes from.
   IGeometrySource* PassthroughSource() const override { return sourceInput; }
   Mat4 GetInstanceGroupMatrix() const override
   {
      return sourceInput ? sourceInput->GetInstanceGroupMatrix() : Mat4::Identity();
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return sourceInput ? sourceInput->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return sourceInput ? sourceInput->InstanceSelectionRevision() : 0;
   }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return sourceInput ? sourceInput->InstanceTransformOverride() : nullptr;
   }

   IGeometrySource* sourceInput = nullptr;
   IGeometrySource* targetInput = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      if (slot == 0) return &sourceInput;
      if (slot == 1) return &targetInput;
      return nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "source", "target" };
      return (slot >= 0 && slot < 2) ? kNames[slot] : nullptr;
   }
   size_t TriangleCount() const { return mCache.indices.size() / 3; }
   // The bend radius the current settings resolve to, live - the UI shows it
   // so the link to the target's size is visible.
   float ResolvedRadius() const;
   static const std::vector<std::string>& ModeNames();

   int mode = 0;                 // MeshOps::kWrapCylindrical
   int axis = 1;                 // Y: text bends around the equator, upright
   // With a target connected the bend radius is the target's derived radius
   // times `radiusScale`, so it always tracks the target. `radiusOverride` is
   // only used when no target is connected.
   float radiusOverride = 1.0f;
   float radiusScale = 1.0f;
   bool fitAround = false;
   // 0 sits exactly on the target's surface in the bend modes.
   float offset = 0.0f;
   float blend = 1.0f;
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
      v.Int("mode", mode); v.Int("axis", axis);
      v.Float("radius", radiusOverride); v.Float("radiusScale", radiusScale);
      v.Bool("fitAround", fitAround);
      v.Float("offset", offset); v.Float("blend", blend);
      v.Bool("flat", flatShade); v.Bool("flip", flipNormals);
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
   struct Signature
   {
      int mode = 0, axis = 0;
      float radiusOverride = 0, radiusScale = 0;
      bool fitAround = false;
      float offset = 0, blend = 0;
      bool flat = false, flip = false;
      const void* source = nullptr;
      const void* target = nullptr;
      unsigned long long sourceRevision = 0;
      unsigned long long targetRevision = 0;
      // Baked in world-space (see MeshOps::Wrap), and GetModelMatrix() is a
      // live per-frame value with no revision stamp of its own, so both
      // matrices are compared directly - same reasoning as
      // InstanceOnPointsNode::CookIfNeeded.
      Mat4 sourceModel;
      Mat4 targetModel;
      bool operator==(const Signature& o) const
      {
         return mode == o.mode && axis == o.axis && radiusOverride == o.radiusOverride &&
                radiusScale == o.radiusScale && fitAround == o.fitAround &&
                offset == o.offset && blend == o.blend && flat == o.flat && flip == o.flip &&
                source == o.source && target == o.target &&
                sourceRevision == o.sourceRevision && targetRevision == o.targetRevision &&
                sourceModel == o.sourceModel && targetModel == o.targetModel;
      }
   };

   Signature CurrentSignature() const;

   Mesh mCache;
   Signature mBuilt;
   bool mHasBuilt = false;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

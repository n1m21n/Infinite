#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Mesh.h"
#include "SplatIO.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Surface properties handed to the renderer. A struct rather than a row of
// out-params: this already carried five, and every material feature after this
// one (emission here, AO and clearcoat later) would otherwise change the
// signature of a virtual with three implementers.
struct Material
{
   float color[3] = { 0.85f, 0.86f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0; // lit / normals / uv / flat

   // Light the surface emits on its own. Added after shading and before
   // tonemapping, so a strength above 1 rolls off into a highlight rather than
   // simply clipping.
   float emissionColor[3] = { 1.0f, 1.0f, 1.0f };
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

   // Sheen / cloth / velvet microfacet backscattering lobe (Charlie distribution)
   float sheen = 0.0f;
   float sheenColor[3] = { 1.0f, 1.0f, 1.0f };
   float sheenRoughness = 0.5f;

   // Optical thin-film interference / iridescence (soap bubble / oil slick / pearl)
   float iridescence = 0.0f;
   float iridescenceIor = 1.33f;
   float iridescenceThickness = 400.0f; // nanometres

   // Anisotropic specular reflection (brushed metals, grooves, vinyl)
   float anisotropy = 0.0f; // -1.0 to 1.0
   float anisotropyRotation = 0.0f; // 0.0 to 1.0

   // Prismatic chromatic dispersion for transmission
   float dispersion = 0.0f;

   // Alpha test / cutout threshold (0 = smooth alpha blend, >0 = mask cutoff)
   float alphaCutoff = 0.0f;
};

// A node that supplies geometry to a Render node. Geometry travels down its own
// kind of cable - it is neither an image nor a control value - so Render's
// inputs hold IGeometrySource pointers rather than ImageCables, the same
// pattern Math uses for modulators.
// Which channel a texture drives. Albedo was the only one for a long time; the
// rest let the existing 2D generators - Noise, Ramp, Formula - author a whole
// material rather than only its colour.
enum MaterialMap
{
   kMapAlbedo = 0,
   kMapRoughness,
   kMapMetallic,
   kMapNormal,
   kMapAmbientOcclusion,
   kMapEmission,
   kMapClearcoat,
   kMapSheen,
   kMapCount
};

// Which coordinates a Mapping node's offset/rotation/scale are applied to,
// mirroring Blender's Texture Coordinate node. UV is the mesh's own baked
// per-vertex UV (what every material map samples with today). Generated is
// the vertex position normalised into its mesh's own object-space bounding
// box, so a texture tiles consistently across an object no matter how it was
// unwrapped. Object is the raw, unnormalised local-space position.
enum MappingSpace
{
   kMapSpaceUv = 0,
   kMapSpaceGenerated,
   kMapSpaceObject
};

// Offset/rotation/scale applied to whichever coordinate space a Mapping node
// picked, before a material map samples it. Identity by default so a source
// with no Mapping patched in front of it renders exactly as it always did.
struct MappingTransform
{
   int space = kMapSpaceUv;
   float translate[3] = { 0.0f, 0.0f, 0.0f };
   float rotate[3] = { 0.0f, 0.0f, 0.0f }; // radians
   float scale[3] = { 1.0f, 1.0f, 1.0f };
   float triplanarBlend = 0.0f; // 0 = sharp pick, >0 = smooth blended triplanar
};

class IGeometrySource
{
public:
   virtual ~IGeometrySource() {}

   // Whether output pin `index` on this node actually carries GetMesh()'s
   // geometry. Defaults to "only the primary output (index 0)" so a
   // multi-output node (e.g. a Field node with scalar outputs alongside its
   // geo output) doesn't let a non-geometry pin silently act as if it were
   // the mesh output just because the node happens to implement this
   // interface at all. Mirrors IAudioSource::IsAudioOutputIndex (INode.h).
   virtual bool IsGeometryOutputIndex(int index) const { return index == 0; }

   // Mesh in object space, plus the model matrix that places it in the scene.
   virtual const Mesh& GetMesh() = 0;

   // Version stamp for whatever GetMesh() is currently returning, from
   // NextMeshRevision(). Render 3D keeps the mesh on the GPU between frames and
   // only re-uploads when this changes. Pure virtual on purpose: a source that
   // silently returned a constant would freeze its geometry at the first frame.
   virtual unsigned long long MeshRevision() = 0;

   virtual Mat4 GetModelMatrix() const = 0;
   virtual Material GetMaterial() const = 0;
   // Optional texture applied to the surface (0 when none is patched in).
   virtual unsigned int GetSurfaceTexture() { return 0; }
   // Per-channel maps. Defaults to routing slot 0 to the albedo texture, so a
   // source that only ever had one texture keeps working untouched.
   virtual unsigned int GetMaterialTexture(int map)
   {
      return map == kMapAlbedo ? GetSurfaceTexture() : 0;
   }
   // Content version stamp for whatever GetSurfaceTexture()/GetMaterialTexture()
   // currently return - unlike the GL handle, this changes when a texture
   // producer recooks new pixels into the same persistent FBO texture. 0 by
   // default (matches GetSurfaceTexture()'s "no texture" default); a source
   // with a real texture input should forward its ImageCable::Revision(), and
   // a passthrough wrapper (Transform, Array, Twist, Mapping, ...) should
   // forward its upstream source's SurfaceTextureRevision().
   virtual unsigned long long SurfaceTextureRevision() const { return 0; }
   // How material maps are looked up on the surface. Identity/UV by default,
   // so nothing changes for a chain with no Mapping node in it.
   virtual MappingTransform GetMappingTransform() const { return MappingTransform(); }

   // The upstream source this node derives its mesh from, for nodes whose
   // GetMesh() is built by transforming a single upstream mesh (Transform,
   // Array, Subdivide, ...). Render3D walks this chain to find an
   // InstanceOnPoints further upstream, so e.g. instance on points ->
   // transform -> render still draws every instance instead of falling back
   // to a single un-instanced copy. Nullptr by default, meaning "this is
   // where the mesh actually comes from".
   virtual IGeometrySource* PassthroughSource() const { return nullptr; }

   // An extra rigid transform to compose onto each of an upstream instancer's
   // per-instance matrices, for a wrapper (Transform) that sits between an
   // InstanceOnPoints and whatever's drawing it - see GeometryOpNode's
   // override. Identity by default: only a source that actually redirects a
   // transform this way needs to return anything else.
   virtual Mat4 GetInstanceGroupMatrix() const { return Mat4::Identity(); }

   // Per-instance selection produced by a Select node downstream of an
   // InstanceOnPoints. Parallel to InstanceOnPointsNode::InstanceTransforms() -
   // index i here masks InstanceTransforms()[i] (or the override below, if
   // any). nullptr when nothing in the chain has selected instances.
   virtual const std::vector<unsigned char>* InstanceSelection() const { return nullptr; }
   virtual unsigned long long InstanceSelectionRevision() const { return 0; }

   // Instance transforms after this node's edit, replacing the instancer's
   // own list - e.g. Delete(selectionOnly) dropping masked entries, or
   // Transform(selectionOnly) moving only the masked ones. nullptr when this
   // node doesn't modify them, meaning "read InstanceTransforms() from the
   // instancer unchanged".
   virtual const std::vector<Mat4>* InstanceTransformOverride() const { return nullptr; }

   // Optional components alongside (or instead of) the mesh above. A source
   // with no point cloud / curve of its own returns nullptr, which callers
   // treat identically to "not connected to the right kind of thing" - the
   // same guard they already used for a plain null pointer before every
   // geometry-ish source shared one interface.
   virtual const std::vector<Particle>* GetPointCloud() { return nullptr; }
   virtual unsigned long long PointCloudRevision() { return 0; }
   // World-space half-extent that Particle::scale (a relative multiplier, see
   // Mesh.h) is relative to for this source's points. Render3D's sprite draw
   // multiplies p.scale by this to get an actual world size - a source whose
   // p.scale is already absolute (ParticleSystemNode, Distribute*, Mesh to
   // Points) returns 1.0f; a source that documents p.scale as a 0-1-ish
   // multiplier of its own cell size (Image to Points) returns that cell size.
   virtual float PointBaseSize() const { return 1.0f; }
   virtual const Polyline* GetCurve() { return nullptr; }
   virtual unsigned long long CurveStamp() { return 0; }

   // Gaussian splat cloud, alongside (or instead of) the mesh/point cloud
   // above. nullptr by default, matching GetPointCloud()'s convention, so
   // every existing IGeometrySource implementer is unaffected. Render3D
   // gives a splat cloud precedence over a point cloud or mesh triangles
   // when a source offers more than one (see drawSlot's precedence check).
   // A passthrough wrapper MUST forward this and SplatCloudRevision() from
   // its input, the same way it forwards GetPointCloud()/PointCloudRevision
   // - skipping it makes a splat source vanish behind a Transform/Material/
   // Mapping/etc node, the same passthrough-forwarding bug class that has
   // already happened here for real (env-light cache invalidation).
   virtual const SplatIO::SplatCloud* GetSplatCloud() { return nullptr; }
   virtual unsigned long long SplatCloudRevision() { return 0; }
};

// --- Geometry -----------------------------------------------------------
class GeometryNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new GeometryNode(); }
   // Each primitive is also registered under its own name, all sharing this
   // class. The dropdown still works and still switches shape without
   // rewiring - this just means "cube" is findable by typing "cube" rather
   // than by knowing it lives inside a node called Geometry.
   static INode* CreateFor(int shapeIndex)
   {
      auto* node = new GeometryNode();
      node->shape = shapeIndex;
      return node;
   }
   static const std::vector<std::string>& ShapeNames();
   static const std::vector<std::string>& ShadingNames();

   ~GeometryNode() override;

   // Geometry nodes have no image output; the preview shows a small solo render.
   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mPreview.w; }
   int GetOutputHeight() const override { return mPreview.h; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned long long SurfaceTextureRevision() const override { return mTextureInput.Revision(); }

   ImageCable& TextureInput() { return mTextureInput; }
   const char* InputLabel(int) const override { return "texture"; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   int shape = 1;        // cube by default
   int detail = 24;      // segments / rings
   int sides = 16;
   float tubeRadius = 0.4f;
   int knotP = 2;
   int knotQ = 3;
   float bevel = 0.0f;
   int bevelSegments = 1;
   // Supershape exponents. knotP and knotQ double as its two m values, since
   // both are "how many lobes" parameters and reusing them keeps the panel from
   // growing a second set of near-identical sliders.
   float superN2 = 1.0f, superN3 = 1.0f;
   // Disc's hole. Kept separate from superN2 because that defaults to 1.0 (it
   // is a supershape exponent) and a node called Disc should spawn solid.
   float discInner = 0.0f;
   float superP2 = 1.0f, superP3 = 1.0f;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
   float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
   float uniformScale = 1.0f;
   float spinY = 0.0f;   // extra rotation per beat, so shapes can turn on their own

   float color[3] = { 0.85f, 0.86f, 0.9f };
   float metallic = 0.1f;
   float roughness = 0.45f;
   float opacity = 1.0f;
   int shading = 0;      // lit / normals / uv / wireframe-ish flat
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
      v.Int("shape", shape); v.Int("detail", detail); v.Int("sides", sides);
      v.Float("tube", tubeRadius); v.Int("knotP", knotP); v.Int("knotQ", knotQ);
      v.Float("bevel", bevel); v.Int("bevelSegments", bevelSegments);
      v.Float("superN2", superN2); v.Float("superN3", superN3);
      v.Float("discInner", discInner);
      v.Float("superP2", superP2); v.Float("superP3", superP3);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY); v.Float("scaleZ", scaleZ);
      v.Float("scale", uniformScale); v.Float("spin", spinY);
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
   }

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   int mBuiltShape = -1, mBuiltDetail = -1, mBuiltSides = -1, mBuiltP = -1, mBuiltQ = -1;
   float mBuiltTube = -1.0f;
   float mBuiltBevel = -1.0f;
   int mBuiltBevelSegments = -1;
   float mBuiltN2 = -999.0f, mBuiltN3 = -999.0f, mBuiltP2 = -999.0f, mBuiltP3 = -999.0f;
   float mBuiltDiscInner = -999.0f;
   unsigned long long mMeshRevision = 0;

   ImageCable mTextureInput;
   GLUtil::Fbo mPreview;
   int mLastCookFrame = -1;
   bool mPreviewFailed = false;
};

// --- Render 3D ----------------------------------------------------------
// Rasterises up to four geometry inputs into a texture with a depth buffer,
// a camera and two lights. This is the bridge from 3D back into the 2D graph:
// its output is an ordinary image cable.
class Render3DNode : public INode
{
public:
   static const int kSlots = 4;

   static INode* Create() { return new Render3DNode(); }
   static const std::vector<std::string>& ProjectionNames();

   ~Render3DNode() override;

   unsigned int GetOutputTexture() override { return mColorTex; }
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   IGeometrySource* geometry[kSlots] = { nullptr, nullptr, nullptr, nullptr };
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &geometry[slot] : nullptr;
   }

   // Optional scene nodes. When null, the built-in camera/light values below
   // are used, so a Render node works on its own.
   static const int kLightSlots = 3;
   class CameraNode* camera = nullptr;
   class LightNode* lights[kLightSlots] = { nullptr, nullptr, nullptr };

   // An HDRI node feeds this. Unlike geometry/camera/light, this is an
   // ordinary image cable rather than a raw pointer - an Environment node is
   // just another texture source as far as connection plumbing is concerned.
   static const int kEnvSlot = kSlots + 1 + kLightSlots;
   ImageCable envInput;
   ImageCable& EnvironmentInput() { return envInput; }
   // When an HDRI is patched in, draw it as the background too. Off leaves
   // the flat bgColor clear while still using the HDRI for lighting and
   // reflections - useful when compositing the render over something else.
   bool envAsBackground = true;

   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo A", "geo B", "geo C", "geo D",
                                      "camera", "light 1", "light 2", "light 3",
                                      "env" };
      return (slot >= 0 && slot < 9) ? kNames[slot] : nullptr;
   }

   size_t LastTriangleCount() const { return mLastTriangles; }
   size_t LastDrawCalls() const { return mLastDrawCalls; }
   // Slots that had to re-upload this frame; 0 means everything came from cache.
   size_t LastUploads() const { return mLastUploads; }

   float width = 1024.0f;
   float height = 1024.0f;
   int projection = 0;      // perspective / orthographic
   float fov = 45.0f;
   float orthoHeight = 1.5f;
   float camDistance = 3.0f;
   float camAzimuth = 34.3775f;
   float camElevation = 22.9183f;
   float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
   float nearPlane = 0.05f;
   float farPlane = 100.0f;

   float lightAzimuth = 51.5662f;
   float lightElevation = 51.5662f;
   float lightColor[3] = { 1.0f, 0.98f, 0.94f };
   float lightIntensity = 1.2f;
   float ambientColor[3] = { 0.28f, 0.32f, 0.42f };
   float rimIntensity = 0.35f;

   // Procedural surround, sampled for both reflections and ambient. Not an
   // HDRI: a three-stop vertical gradient, which is enough to stop metal
   // reading as flat grey.
   float envSky[3] = { 0.42f, 0.53f, 0.75f };
   float envHorizon[3] = { 0.55f, 0.56f, 0.60f };
   float envGround[3] = { 0.16f, 0.14f, 0.13f };
   float envIntensity = 1.0f;

   float bgColor[3] = { 0.04f, 0.04f, 0.06f };
   float bgOpacity = 1.0f;
   bool depthTest = true;
   bool backfaceCull = true;

   // How a connected point cloud slot draws (when GetPointCloud() returns
   // non-null for that slot). Applies to every cloud slot uniformly, since
   // it's a property of how this node renders points, not of any one source.
   static const std::vector<std::string>& SpriteShapeNames();
   int spriteShape = 0;      // circle / square
   static const std::vector<std::string>& SpriteSizeModeNames();
   int spriteSizeMode = 0;   // world / screen

   // Shadows. Rendered from the first directional or sun light, or from the
   // built-in light when none is patched in. Point lights are deliberately not
   // supported: an omnidirectional light needs a depth cube map and six passes,
   // which is a different piece of work from this one.
   static const std::vector<std::string>& ShadowQualityNames();
   bool shadowsEnabled = false;
   int shadowQuality = 1;      // index into ShadowQualityNames: 1024/2048/4096
   float shadowBias = 0.0025f;
   float shadowSoftness = 1.0f;
   float shadowStrength = 0.8f;
   int ActiveShadowSize() const { return mShadowSize; }

   // Multisample count for edge antialiasing: 0 is off, otherwise 2/4/8. The
   // scene is drawn into a multisampled buffer and resolved into the ordinary
   // output texture, so the 2D graph downstream sees a normal image either way.
   // Clamped to the driver's GL_MAX_SAMPLES at resource-creation time.
   static const std::vector<std::string>& SampleNames();
   int samples = 2; // index into SampleNames(), so 4x

   // Lighting is computed in linear space and tonemapped on the way out. The
   // clear colour is left alone: it is written straight to the target by
   // glClear, so it is already in the same encoded space the shader outputs.
   static const std::vector<std::string>& TonemapNames();
   float exposure = 1.0f;
   int tonemap = 1; // none / ACES / Reinhard
   int ActiveSamples() const { return mActiveSamples; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("width", width); v.Float("height", height);
      v.Int("projection", projection); v.Float("fov", fov);
      v.Float("orthoHeight", orthoHeight); v.Float("camDistance", camDistance);
      v.Float("camAzimuth", camAzimuth); v.Float("camElevation", camElevation);
      v.Float("targetX", targetX); v.Float("targetY", targetY); v.Float("targetZ", targetZ);
      v.Float("near", nearPlane); v.Float("far", farPlane);
      v.Float("lightAzimuth", lightAzimuth); v.Float("lightElevation", lightElevation);
      v.Color("lightColor", lightColor); v.Float("lightIntensity", lightIntensity);
      v.Color("ambient", ambientColor); v.Float("rim", rimIntensity);
      v.Color("envSky", envSky); v.Color("envHorizon", envHorizon);
      v.Color("envGround", envGround); v.Float("envIntensity", envIntensity);
      v.Bool("envAsBackground", envAsBackground);
      v.Color("bg", bgColor); v.Float("bgOpacity", bgOpacity);
      v.Bool("depthTest", depthTest); v.Bool("cull", backfaceCull);
      v.Int("spriteShape", spriteShape); v.Int("spriteSizeMode", spriteSizeMode);
      v.Int("samples", samples); v.Int("tonemap", tonemap); v.Float("exposure", exposure);
      v.Bool("shadows", shadowsEnabled); v.Int("shadowQuality", shadowQuality);
      v.Float("shadowBias", shadowBias); v.Float("shadowSoftness", shadowSoftness);
      v.Float("shadowStrength", shadowStrength);
   }

private:
   // One set of GPU buffers per input slot, kept across frames. Re-uploading a
   // subdivided mesh every frame was costing more than drawing it; now the
   // upload only happens when the source's mesh stamp actually moves.
   //
   // Keyed per slot rather than by source pointer so there is no map to sweep
   // and no chance of a freed node leaving a dangling key behind: a slot that
   // changes source simply invalidates and re-uploads.
   struct GpuMesh
   {
      unsigned int vao = 0, vbo = 0, ibo = 0, instanceVbo = 0, instanceColorVbo = 0, vertexColorVbo = 0;
      // Object-space bounds, computed once per upload. The shadow volume has to
      // be fitted to the scene every frame, and walking every vertex of a
      // hundred-thousand-triangle mesh to do that would cost more than the
      // shadow pass itself.
      float lo[3] = { 0, 0, 0 };
      float hi[3] = { 0, 0, 0 };
      bool hasBounds = false;
      const void* source = nullptr;
      unsigned long long meshRevision = 0;
      unsigned long long instanceRevision = 0;
      int indexCount = 0;
      // Vertex count, used instead of indexCount to draw a vertices-only mesh
      // (Points to Vertices' output - no edges, no faces) as GL_POINTS rather
      // than silently drawing nothing.
      int vertexCount = 0;
      int instanceCount = 0;
      bool instanceAttribsOn = false;
      bool instanceColored = false;
      bool hasVertexColor = false;
      // The GetInstanceGroupMatrix() baked into the uploaded instance buffer
      // last time it was rebuilt, so a Transform node's own params changing -
      // which doesn't bump the instancer's own InstanceRevision() - still
      // triggers a re-upload.
      Mat4 instanceGroupMatrix;
      // A selectionOnly Delete/Transform downstream of the instancer publishes
      // its own transform list via InstanceTransformOverride() instead of
      // touching the instancer, so neither instanceRevision nor
      // instanceGroupMatrix above catch it changing on its own. The owning
      // node bumps its own MeshRevision() on every real rebuild of that list
      // (GetMesh() always stamps a fresh revision, override or not), so this
      // mirrors `revision`/`meshRevision` at the point the instance buffer was
      // last uploaded.
      unsigned long long instanceOverrideRevision = 0;
   };

   bool EnsureResources(int w, int h, int sampleCount);
   bool EnsureShader();
   bool EnsureShadowResources(int size);
   bool EnsureShadowShader();
   bool EnsureEnvBgShader();
   bool EnsureSplatShader();
   void ReleaseGpuMesh(GpuMesh& gpu);
   void ReleaseTargets();
   void ReleaseShadowTargets();
   // ReleaseGpuSplat is declared further below, after GpuSplat itself is
   // defined - a nested type used as a parameter type isn't visible to an
   // earlier member declaration in the same class, unlike names only used
   // in a function body.

   // Everything the actual draw (shadow + opaque + transmissive passes)
   // depends on. When this is identical to the last cook, the previous
   // mColorTex/mSceneColorTex contents are still correct and the whole
   // draw can be skipped - mirrors GeometryOpNode::Signature on the mesh
   // side, just gathered from more places (own params, an optional camera
   // node, up to kLightSlots optional light nodes, each geometry slot's
   // mesh revision + material + world transform + instancing state, and
   // the optional env map).
   //
   // Deliberately conservative in one place rather than tracking it exactly:
   // a camera/light with orbitPerBeat != 0 moves continuously from Transport
   // beats alone, with no param edit to detect, so that always forces a
   // redraw - a correctness-safe no-op for the non-orbiting case this cache
   // actually targets. The surface/albedo texture used to need the same
   // treatment (IGeometrySource had no revision stamp for a texture's
   // *contents*, only GetSurfaceTexture()'s GL handle, which a persistent-FBO
   // producer like Formula/Filter keeps unchanged across recooks), but
   // SurfaceTextureRevision() now provides that stamp, so it's tracked like
   // meshRev/cloudRev/curveRev below instead of forcing a redraw. The
   // remaining per-channel material maps (roughness/metallic/normal/ao)
   // still have no revision stamp of their own, so any of those being bound
   // still forces a redraw.
   //
   // meshRev/cloudRev/curveRev are kept as three SEPARATE fields rather than
   // folded into one via XOR (or any other combiner). Several
   // IGeometrySource implementations deliberately return the same counter
   // from two of MeshRevision()/PointCloudRevision()/CurveStamp() - e.g.
   // MeshToPointsNode, DistributePointsOnFacesNode, DistributePointsInGridNode
   // (mesh cache and point cache rebuilt together, sharing mMeshRevision) and
   // CurveNode (mesh and curve share mRevision). That sharing is correct on
   // the producer side. But XOR-folding those three counters into one
   // geomRev means a ^ a == 0 for any node aliasing two of them, so the fold
   // was a constant 0 forever no matter how much the geometry changed - the
   // signature never moved and the viewport froze on the first frame. Keeping
   // the three stamps separate here makes that cancellation impossible: a
   // change to any one component always shows up in its own field.
   // RENDER3DCACHESWEEPTEST checks this generically.
   //
   // modelMatrix/instanceGroupMatrix cover a Geometry node's own
   // spin/orbit-driven transform (evaluated live off the transport clock,
   // with no revision bump of its own - see GeometryNode::GetModelMatrix)
   // and a wrapping Transform node sitting between an instancer and this
   // node (which likewise doesn't bump the instancer's InstanceRevision()).
   // instanceRev/instanceCount cover a Particle System -> Instance on Points
   // chain: the instanced mesh's own MeshRevision()/PointCloudRevision()/
   // CurveStamp() never change (the instance shape is constant, and the
   // instancer doesn't override the point-cloud/curve stamps), so without
   // these the moving instance transforms would never invalidate the cache.
   struct SceneSignature
   {
      std::vector<float> own;
      bool hasCamera = false;
      std::vector<float> camera;
      bool hasLight[kLightSlots] = { false, false, false };
      std::vector<float> light[kLightSlots];
      bool hasGeom[kSlots] = { false, false, false, false };
      unsigned long long meshRev[kSlots] = { 0, 0, 0, 0 };
      unsigned long long cloudRev[kSlots] = { 0, 0, 0, 0 };
      unsigned long long curveRev[kSlots] = { 0, 0, 0, 0 };
      // Splat cloud content stamp - kept as its own field for the same
      // reason meshRev/cloudRev/curveRev are separate (see the comment
      // above): folding it into an existing counter would silently break
      // invalidation for any source that happens to alias two of them.
      unsigned long long splatRev[kSlots] = { 0, 0, 0, 0 };
      unsigned long long surfaceTexRev[kSlots] = { 0, 0, 0, 0 };
      Material material[kSlots];
      Mat4 modelMatrix[kSlots];
      unsigned long long instanceRev[kSlots] = { 0, 0, 0, 0 };
      size_t instanceCount[kSlots] = { 0, 0, 0, 0 };
      Mat4 instanceGroupMatrix[kSlots];
      bool envConnected = false;
      unsigned long long envRev = 0;
      float envRotation = 0.0f;
      float envIntensity = 1.0f;
      bool animated = false;
      bool texturedMaterial = false;

      bool operator==(const SceneSignature& o) const
      {
         if (animated || o.animated || texturedMaterial || o.texturedMaterial)
            return false; // never treated as a cache hit
         if (own != o.own || hasCamera != o.hasCamera || camera != o.camera)
            return false;
         for (int i = 0; i < kLightSlots; i++)
            if (hasLight[i] != o.hasLight[i] || light[i] != o.light[i])
               return false;
         for (int i = 0; i < kSlots; i++)
         {
            if (hasGeom[i] != o.hasGeom[i] || meshRev[i] != o.meshRev[i] ||
                cloudRev[i] != o.cloudRev[i] || curveRev[i] != o.curveRev[i] ||
                splatRev[i] != o.splatRev[i] ||
                surfaceTexRev[i] != o.surfaceTexRev[i] || instanceRev[i] != o.instanceRev[i] ||
                instanceCount[i] != o.instanceCount[i])
               return false;
            if (hasGeom[i] && memcmp(&material[i], &o.material[i], sizeof(Material)) != 0)
               return false;
            if (hasGeom[i] && !(modelMatrix[i] == o.modelMatrix[i]))
               return false;
            if (hasGeom[i] && !(instanceGroupMatrix[i] == o.instanceGroupMatrix[i]))
               return false;
         }
         return envConnected == o.envConnected && envRev == o.envRev && envRotation == o.envRotation &&
                envIntensity == o.envIntensity;
      }
   };
   SceneSignature BuildSceneSignature();
   // World-space bounds of everything patched in, from the cached per-slot
   // object-space boxes pushed through each source's model matrix.
   bool SceneBounds(float outLo[3], float outHi[3]);

   // Resolve target: a plain texture, and what the rest of the graph reads.
   unsigned int mFbo = 0;
   unsigned int mColorTex = 0;
   unsigned int mDepthBuffer = 0;
   // Multisampled draw target, blitted into the above. Unused when samples == 0.
   unsigned int mMsFbo = 0;
   unsigned int mMsColor = 0;
   unsigned int mMsDepth = 0;
   int mActiveSamples = 0;
   int mWidth = 0, mHeight = 0;

   // Snapshot of the opaque pass, sampled by the transmissive pass below for
   // screen-space refraction. Mipmapped so transmissionRoughness can blur the
   // sample the same way sampleEnv() blurs a reflection - a cheap stand-in for
   // a real rough-refraction filter. This is a screen-space approximation, not
   // raytraced glass: geometry hidden behind other transmissive geometry, or
   // seen only through it, is not captured and will look wrong for thick or
   // overlapping transmissive objects.
   unsigned int mSceneColorFbo = 0;
   unsigned int mSceneColorTex = 0;
   float mSceneMaxLod = 0.0f;

   unsigned int mProgram = 0;
   bool mShaderTried = false;
   unsigned int mEnvBgProgram = 0;
   bool mEnvBgShaderTried = false;
   // Empty VAO for the background's vertexless "big triangle" - core profile
   // requires one bound to draw at all, even when no attribute is read.
   unsigned int mEnvBgVao = 0;
   unsigned int mShadowProgram = 0;
   bool mShadowShaderTried = false;
   unsigned int mShadowFbo = 0;
   unsigned int mShadowTex = 0;
   int mShadowSize = 0;
   Mat4 mLightViewProj;
   // Background depth-sort worker for one splat slot. GL 3.3 core has no
   // compute shader / SSBO / GPU sort available (see docs/plans/
   // gaussian-splat-node.md S0), so the back-to-front order is produced on
   // the CPU, off the render thread, and the render thread never blocks on
   // it: it uploads whatever order is newest-complete and simply keeps
   // drawing the previous order while a sort is still in flight. One
   // persistent thread per slot (up to kSlots of them), started once and
   // parked on a condition variable between jobs - not spun up per sort.
   class SplatSorter
   {
   public:
      SplatSorter();
      ~SplatSorter();
      SplatSorter(const SplatSorter&) = delete;
      SplatSorter& operator=(const SplatSorter&) = delete;

      // Copies the splats' positions (cheap relative to a full sort) and
      // hands the job to the worker thread. Non-blocking. A job already in
      // flight is superseded - the worker only ever finishes the most
      // recently requested one.
      void RequestSort(const SplatIO::SplatCloud* cloud, const Mat4& modelView);
      // Non-blocking poll: true (and outIndices filled) only when a NEW
      // completed order is ready since the last call. False leaves
      // outIndices untouched, meaning "keep drawing whatever you already
      // uploaded".
      bool TakeCompletedOrder(std::vector<unsigned int>& outIndices);

   private:
      void ThreadMain();

      std::thread mThread;
      std::mutex mMutex;
      std::condition_variable mCv;
      bool mStop = false;
      bool mHasJob = false;
      bool mResultReady = false;
      std::vector<float> mPendingPositions; // xyz per splat
      Mat4 mPendingModelView;
      std::vector<unsigned int> mResult;
   };

   // Per-slot GPU state for a splat cloud, entirely separate from GpuMesh:
   // a splat cloud draws instanced screen-facing quads from a static data
   // texture plus a per-frame index buffer, never triangles/VAO-attribute
   // layout a mesh uses. See docs/plans/gaussian-splat-node.md S6 for the
   // GPU layout this mirrors (2048-wide GL_RGBA32F, 4 texels/splat).
   struct GpuSplat
   {
      unsigned int vao = 0;
      unsigned int quadVbo = 0;     // static unit-quad corners, 4 verts
      unsigned int indexVbo = 0;    // GL_R32UI, divisor 1, re-uploaded per sort
      unsigned int tex = 0;         // GL_RGBA32F splat data texture
      int texWidth = 0, texHeight = 0;
      unsigned long long texRevision = 0; // SplatCloudRevision() baked into `tex`
      const void* source = nullptr;
      int splatCount = 0;
      int uploadedIndexCount = 0;
      // Average per-splat world-space radius (sqrt of the covariance
      // trace/3), computed once when the texture is rebuilt - used only for
      // the VRAM/fill-rate budget estimate, mirroring the point-cloud sprite
      // guard; never read by the shader.
      float avgWorldRadius = 0.0f;
      bool hasSorted = false;
      float lastSortFwd[3] = { 0.0f, 0.0f, 1.0f };
      float lastSortEye[3] = { 0.0f, 0.0f, 0.0f };
      SplatSorter sorter;
   };

   void ReleaseGpuSplat(GpuSplat& gpu);

   GpuMesh mGpu[kSlots];
   GpuSplat mGpuSplat[kSlots];
   unsigned int mSplatProgram = 0;
   bool mSplatShaderTried = false;
   // Last fill-rate clamp factor reported for the splat pass, gated the same
   // way mLastFillClamp is (only print when it actually moves).
   float mLastSplatFillClamp = 1.0f;
   int mLastCookFrame = -1;
   // Last sprite-fill clamp factor this node reported (1 = not clamping).
   // The clamp is evaluated on every instance-buffer rebuild, which for an
   // animating cloud is every frame, so the warning is only printed when the
   // factor actually moves - on Windows stderr is the rolling log file
   // (CrashHandlerWin.cpp), and a per-frame line would bury everything else.
   float mLastFillClamp = 1.0f;
   size_t mLastTriangles = 0;
   size_t mLastDrawCalls = 0;
   size_t mLastUploads = 0;
   bool mHasSceneBuilt = false;
   SceneSignature mSceneBuilt;
   unsigned long long mRevision = 0;
};

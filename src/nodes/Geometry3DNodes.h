#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Mesh.h"

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
};

// A node that supplies geometry to a Render node. Geometry travels down its own
// kind of cable - it is neither an image nor a control value - so Render's
// inputs hold IGeometrySource pointers rather than ImageCables, the same
// pattern Math uses for modulators.
class IGeometrySource
{
public:
   virtual ~IGeometrySource() {}

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

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shape", shape); v.Int("detail", detail); v.Int("sides", sides);
      v.Float("tube", tubeRadius); v.Int("knotP", knotP); v.Int("knotQ", knotQ);
      v.Float("bevel", bevel); v.Int("bevelSegments", bevelSegments);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("rotX", rotX); v.Float("rotY", rotY); v.Float("rotZ", rotZ);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY); v.Float("scaleZ", scaleZ);
      v.Float("scale", uniformScale); v.Float("spin", spinY);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
   }

private:
   void RebuildIfNeeded();

   Mesh mMesh;
   int mBuiltShape = -1, mBuiltDetail = -1, mBuiltSides = -1, mBuiltP = -1, mBuiltQ = -1;
   float mBuiltTube = -1.0f;
   float mBuiltBevel = -1.0f;
   int mBuiltBevelSegments = -1;
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

   IGeometrySource* geometry[kSlots] = { nullptr, nullptr, nullptr, nullptr };

   // Optional scene nodes. When null, the built-in camera/light values below
   // are used, so a Render node works on its own.
   static const int kLightSlots = 3;
   class CameraNode* camera = nullptr;
   class LightNode* lights[kLightSlots] = { nullptr, nullptr, nullptr };

   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "geo A", "geo B", "geo C", "geo D",
                                      "camera", "light 1", "light 2", "light 3" };
      return (slot >= 0 && slot < 8) ? kNames[slot] : nullptr;
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
   float camAzimuth = 0.6f;
   float camElevation = 0.4f;
   float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
   float nearPlane = 0.05f;
   float farPlane = 100.0f;

   float lightAzimuth = 0.9f;
   float lightElevation = 0.9f;
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
      v.Color("bg", bgColor); v.Float("bgOpacity", bgOpacity);
      v.Bool("depthTest", depthTest); v.Bool("cull", backfaceCull);
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
      unsigned int vao = 0, vbo = 0, ibo = 0, instanceVbo = 0, instanceColorVbo = 0;
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
      int instanceCount = 0;
      bool instanceAttribsOn = false;
      bool instanceColored = false;
   };

   bool EnsureResources(int w, int h, int sampleCount);
   bool EnsureShader();
   bool EnsureShadowResources(int size);
   bool EnsureShadowShader();
   void ReleaseGpuMesh(GpuMesh& gpu);
   void ReleaseTargets();
   void ReleaseShadowTargets();
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

   unsigned int mProgram = 0;
   bool mShaderTried = false;
   unsigned int mShadowProgram = 0;
   bool mShadowShaderTried = false;
   unsigned int mShadowFbo = 0;
   unsigned int mShadowTex = 0;
   int mShadowSize = 0;
   Mat4 mLightViewProj;
   GpuMesh mGpu[kSlots];
   int mLastCookFrame = -1;
   size_t mLastTriangles = 0;
   size_t mLastDrawCalls = 0;
   size_t mLastUploads = 0;
};

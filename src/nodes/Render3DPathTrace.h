#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Bvh.h"
#include "GLUtil.h"
#include "Geometry3DNodes.h"
#include "Mesh.h"
#include "SceneNodes.h"

// Standalone progressive OpenGL 4.1 / 3.3 core fragment-shader path tracer
// (3D Realism Specification section G.3, Option A).
//
// Uploads flattened Bvh::Tlas and Bvh::Blas arrays plus triangle normals and
// materials into RGBA32F samplerBuffer textures, traces one sample per pass
// into ping-pong RGBA32F accumulation targets, and resolves to display sRGB
// using the same IEC 61966-2-1 transfer curve and tonemap operators as
// Render 3D.
class Render3DPathTrace
{
public:
   struct PtMaterial
   {
      float baseColor[3] = { 0.8f, 0.8f, 0.8f }; // linear RGB
      float metallic = 0.0f;
      float roughness = 0.5f;
      float emission[3] = { 0.0f, 0.0f, 0.0f };  // linear RGB radiance
      float ior = 1.5f;
      float specular = 0.5f;          // dielectric reflectance level, as Material's
      float transmission = 0.0f;      // glass share (metallic takes it back)
      float transmissionRoughness = 0.0f;
      float clearcoat = 0.0f;
      float clearcoatRoughness = 0.03f;
      float sheen = 0.0f;
      float sheenRoughness = 0.5f;
      float sheenColor[3] = { 1.0f, 1.0f, 1.0f }; // linear RGB

      static PtMaterial FromLinear(float r, float g, float b, float rough = 0.5f, float metal = 0.0f,
                                   float emR = 0.0f, float emG = 0.0f, float emB = 0.0f);
      static PtMaterial FromMaterial(const Material& m);
   };

   struct PtLight
   {
      int type = 0; // 0 directional, 1 point, 2 sun, 3 ambient, 4 spot, 5 area
      float posOrDir[3] = { 0.0f, 1.0f, 0.0f };
      float aim[3] = { 0.0f, -1.0f, 0.0f };
      float color[3] = { 1.0f, 1.0f, 1.0f }; // linear RGB scaled by radiometric power term
      float areaW = 1.0f;
      float areaH = 1.0f;
      float spotCosOuter = 0.70710678f;
      float spotSmoothW = 0.05f;
      float cutoff = 0.0f;

      static PtLight FromRigLight(const Render3DNode::RigLight& rl);
      static PtLight FromLightNode(const LightNode& n);
      static PtLight AreaWatts(const float pos[3], const float aimDir[3], float width, float height,
                               float powerWatts, const float linearRgb[3] = nullptr);
      static PtLight PointWatts(const float pos[3], float powerWatts, const float linearRgb[3] = nullptr);
   };

   Render3DPathTrace() = default;
   ~Render3DPathTrace();

   Render3DPathTrace(const Render3DPathTrace&) = delete;
   Render3DPathTrace& operator=(const Render3DPathTrace&) = delete;

   void Release();

   // Builds BLASes (if not already provided) and TLAS, packs triangle smooth
   // normals and materials, and uploads all scene buffers to GL_TEXTURE_BUFFER
   // (GL_RGBA32F) textures. Resets the progressive accumulation counter.
   bool UploadScene(const std::vector<Mesh>& meshes,
                    const std::vector<Bvh::Instance>& instances,
                    const std::vector<PtMaterial>& materials);

   void SetLights(const std::vector<PtLight>& lights);
   void SetWorldColorLinear(float r, float g, float b);
   void SetExposure(float exp) { exposure = exp; }
   void SetTonemap(int mode) { tonemap = mode; }

   void SetCameraLookAt(const float eye[3], const float target[3], const float up[3],
                        float fovDegrees, float aspect);
   void SetCameraFromNode(const CameraNode& cam, float aspect);
   // A camera given as its world basis (Render 3D's own view matrix rows). orthoHalfH > 0 makes it
   // orthographic with that half height; tanHalfFovY is then unused.
   void SetCameraBasis(const float eye[3], const float right[3], const float up[3], const float forward[3],
                       float tanHalfFovY, float aspect, float orthoHalfH);
   // The world rays see when they leave the scene: an equirect HDRI (Environment node's texture), or
   // with equirectTex 0 Render 3D's sky/horizon/ground gradient (linear colours). Replaces the
   // uniform world colour until the next SetWorldColorLinear.
   void SetEnvironment(unsigned int equirectTex, float rotationRad, float intensity,
                       const float skyLin[3], const float horizonLin[3], const float groundLin[3]);
   // worldAsBackground false: a camera ray that misses shows `srgb` at `opacity`, as Render 3D's
   // clear colour does, instead of the world. Display only; the samples are kept.
   void SetBackground(bool worldAsBackground, const float srgb[3], float opacity);
   // A thin lens: radius in world units (0 a pinhole), the distance that stays sharp, and the
   // aperture as Render 3D's bokeh: blades (below 3 round), their turn in radians, the tall/wide ratio.
   void SetLens(float radius, float focusDistance, int blades, float rotationRad, float ratio);

   // Resets progressive accumulation without re-uploading the scene BVH.
   void ResetAccumulation();

   // Live refinement: traces horizontal strips of the next sample, sized from the last strips'
   // speed so a call stays within `frameBudgetMs` (12 ms, so the UI never blocks > 16 ms on an
   // 8 GB M2), and at most `sampleBudgetPerFrame` whole samples. Resolves the running average into
   // the display FBO. Returns 0 once `targetSpp` samples are in (nothing traced), else 1.
   int StepFrame(int width, int height, int targetSpp = 0);

   // Renders `numSamples` additional samples into the accumulation target and
   // resolves to the display FBO. rows > 0 traces only rows y0 .. y0 + rows - 1 of each, and the
   // sample counts once a strip reaches the top row.
   bool StepSamples(int width, int height, int numSamples, bool resolveDisplay = true, int y0 = 0, int rows = 0);

   // Renders from 0 up to `totalSpp` samples and resolves the final image.
   bool RenderToSpp(int width, int height, int totalSpp);

   // Reads the resolved sRGB display image back as top-to-bottom RGBA8 pixels.
   bool ReadDisplayRGBA8(std::vector<unsigned char>& outRgba, int& outW, int& outH) const;

   // Reads the linear-light accumulated buffer back as top-to-bottom RGBA32F pixels.
   bool ReadLinearRGBA32F(std::vector<float>& outRgba, int& outW, int& outH) const;

   unsigned int GetDisplayTexture() const { return mDisplay.tex; }
   unsigned int GetDisplayFbo() const { return mDisplay.fbo; }
   unsigned int GetAccumTexture() const { return mAccum[mAccumReadIdx].tex; }
   int SampleCount() const { return mSampleIndex; }
   // Rows of the display that hold traced samples: all of them once the first sample is whole.
   int CoveredRows() const { return mSampleIndex > 0 ? mHeight : mStripY; }

   // Live frame budget controls (Spec section G.3).
   int sampleBudgetPerFrame = 4;
   float frameBudgetMs = 12.0f;
   int maxBounces = 4;
   float exposure = 1.0f;
   int tonemap = 0; // 0 None, 1 ACES, 2 Reinhard, 3 AgX, 4 Neutral

private:
   struct BufferTex
   {
      unsigned int vbo = 0;
      unsigned int tex = 0;
      size_t floatCount = 0;
   };

   bool EnsureShaders();
   bool EnsureTargets(int w, int h);
   void UploadBufferTex(BufferTex& bt, const std::vector<float>& data, const char* label);
   void DestroyBufferTex(BufferTex& bt);
   void ResolveDisplay();

   BufferTex mTlasNodesBuf;
   BufferTex mTlasInstBuf;
   BufferTex mBlasNodesBuf;
   BufferTex mBlasTrisBuf;
   BufferTex mBlasNormalsBuf;
   BufferTex mMaterialsBuf;

   GLUtil::Fbo mAccum[2];
   GLUtil::Fbo mDisplay;
   int mAccumReadIdx = 0;
   int mSampleIndex = 0;
   int mStripY = 0;      // the next row StepFrame traces
   int mStripRows = 0;   // strip height that fits the frame budget, learned as it runs
   int mWidth = 0;
   int mHeight = 0;

   unsigned int mProgTrace = 0;
   unsigned int mProgDisplay = 0;
   bool mShadersTried = false;
   bool mSceneUploaded = false;

   float mCamEye[3] = { 0.0f, 1.0f, 3.8f };
   float mCamForward[3] = { 0.0f, 0.0f, -1.0f };
   float mCamRight[3] = { 1.0f, 0.0f, 0.0f };
   float mCamUp[3] = { 0.0f, 1.0f, 0.0f };
   float mTanHalfFovY = 0.36397023f;
   float mAspect = 1.0f;
   float mOrthoHalfH = 0.0f;
   float mLensRadius = 0.0f;
   float mFocusDist = 4.0f;
   float mBokeh[3] = { 0.0f, 0.0f, 1.0f };
   float mWorldColor[3] = { 0.0f, 0.0f, 0.0f };
   unsigned int mEnvTex = 0;
   bool mEnvGradient = false;
   float mEnvRotation = 0.0f;
   float mEnvIntensity = 1.0f;
   float mEnvSky[3] = {};
   float mEnvHorizon[3] = {};
   float mEnvGround[3] = {};
   bool mEnvAsBg = true;
   float mBgColor[3] = {};
   float mBgOpacity = 1.0f;

   std::vector<PtLight> mLights;
};

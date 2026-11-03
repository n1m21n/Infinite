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

   // Resets progressive accumulation without re-uploading the scene BVH.
   void ResetAccumulation();

   // Renders up to `sampleBudgetPerFrame` progressive samples into the
   // ping-pong RGBA32F target while staying within `frameBudgetMs` (default
   // <= 12 ms so the UI never blocks > 16 ms on an 8 GB M2). Resolves the
   // running average into the display FBO and returns the number of samples
   // added this call.
   int StepFrame(int width, int height, int targetSpp = 0);

   // Renders `numSamples` additional samples into the accumulation target and
   // resolves to the display FBO.
   bool StepSamples(int width, int height, int numSamples, bool resolveDisplay = true);

   // Renders from 0 up to `totalSpp` samples and resolves the final image.
   bool RenderToSpp(int width, int height, int totalSpp);

   // Reads the resolved sRGB display image back as top-to-bottom RGBA8 pixels.
   bool ReadDisplayRGBA8(std::vector<unsigned char>& outRgba, int& outW, int& outH) const;

   // Reads the linear-light accumulated buffer back as top-to-bottom RGBA32F pixels.
   bool ReadLinearRGBA32F(std::vector<float>& outRgba, int& outW, int& outH) const;

   unsigned int GetDisplayTexture() const { return mDisplay.tex; }
   unsigned int GetAccumTexture() const { return mAccum[mAccumReadIdx].tex; }
   int SampleCount() const { return mSampleIndex; }

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
   float mWorldColor[3] = { 0.0f, 0.0f, 0.0f };

   std::vector<PtLight> mLights;
};

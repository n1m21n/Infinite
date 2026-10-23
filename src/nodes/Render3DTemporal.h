#pragma once

#include <string>

#include "Mesh.h"

// Turbo 0.70 (M4): Render 3D's temporal pipeline and its upscaler.
//
// MV:  camera motion vectors (RG16F, uv units) from the depth buffer and the
//      last frame's camera - the base the TAA and, later, the RTX temporal
//      denoiser (X1) share. Surfaces that move on their own are not in it
//      yet (no per-vertex history): the TAA's history clipping covers them.
// R4:  TAA on the linear HDR image, with a 16-step Halton(2,3) camera jitter.
// R5:  AMD FidelityFX Super Resolution 1 (MIT): EASU upscale + RCAS sharpen
//      from the internal resolution to the output size, after the tonemap.
//
// Compute shaders (GL 4.3) from shaders/*.comp.glsl (EmbeddedFile). All GL
// work on the main context; every pass leaves programs, texture units and
// image units unbound.
class Render3DTemporal
{
public:
   ~Render3DTemporal();

   // The jitter of step `index` in pixels, each axis in -0.5..0.5.
   static void JitterOffset(int index, float& x, float& y);
   // `proj` moved by (x, y) pixels of a w x h target (works for perspective
   // and orthographic: the shift is applied in clip space).
   static Mat4 JitteredProjection(const Mat4& proj, float x, float y, int w, int h);
   // Step of the jitter sequence for the next frame.
   int JitterIndex() const { return mJitterIndex; }

   // Camera motion of every pixel since the last call (its viewProj is kept as
   // "last frame"). viewProjJittered: what the depth was drawn with;
   // viewProj: the same camera without jitter. Returns the RG16F texture.
   // Turbo 0.70: velocityTex (Render3DMotion's velocity target, 0 = none)
   // gives surfaces that move on their own their motion.
   unsigned int MotionVectors(unsigned int depthTex, int w, int h, const Mat4& viewProjJittered, const Mat4& viewProj,
                              float jitterX, float jitterY, unsigned int velocityTex = 0);
   unsigned int MotionTexture() const { return mMotion; }

   // TAA: blends `current` (HDR, w x h, jittered) into the history and returns
   // the result (RGBA16F, also next frame's history). Call after
   // MotionVectors() for the same frame. Advances the jitter.
   unsigned int ResolveTaa(unsigned int current, unsigned int depthTex, int w, int h);
   // The geometry's share of each pixel (R16F, 0 = background), averaged with
   // the same history as the colour - the latest ResolveTaa's.
   unsigned int CoverageTexture() const { return mCoverage[mCurrent]; }
   // Frames blended into the history since the last reset.
   int HistoryFrames() const { return mHistoryFrames; }
   // Forget the history (cut, size change, TAA switched on).
   void ResetHistory();

   // FSR 1: RGBA8 sRGB `src` (iw x ih) -> RGBA8 (ow x oh), EASU then RCAS.
   // sharpenStops: RCAS strength in stops (0 = strongest). Returns 0 when the
   // shaders are not available (the caller keeps the internal-size image).
   unsigned int Upscale(unsigned int src, int iw, int ih, int ow, int oh, float sharpenStops);

   void Release();

   // A compute program from shaders/<name> (EmbeddedFile, //@include lines
   // resolved), labelled; 0 with the log on stderr when it does not build.
   static unsigned int BuildComputeShader(const char* name);
   // Turbo 0.71 (S1): shaders/<name> as text, //@include lines resolved
   // (empty when it is not in this build) - Render 3D HQ's raster shaders.
   static std::string LoadShaderSource(const char* name);

private:
   bool EnsurePrograms();
   void EnsureTaaTargets(int w, int h);
   void EnsureUpscaleTargets(int w, int h);

   unsigned int mMotionProgram = 0, mTaaProgram = 0, mEasuProgram = 0, mRcasProgram = 0;
   bool mProgramsTried = false;

   int mTaaW = 0, mTaaH = 0;
   unsigned int mMotion = 0;
   unsigned int mHistory[2] = { 0, 0 };
   unsigned int mCoverage[2] = { 0, 0 };
   int mCurrent = 0;             // mHistory[mCurrent] is the latest result
   int mHistoryFrames = 0;
   int mJitterIndex = 0;
   Mat4 mPrevViewProj;
   bool mHavePrev = false;

   int mUpW = 0, mUpH = 0;
   unsigned int mEasuOut = 0, mRcasOut = 0;
};

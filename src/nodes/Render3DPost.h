#pragma once

// Turbo 0.61: Render 3D's image-based lighting and its HDR finish.
//
// Ibl: from an equirectangular HDRI, the three textures the split-sum
// approximation needs (Karis, "Real Shading in Unreal Engine 4"; the same
// scheme as the Khronos glTF-Sample-Viewer): a GGX-prefiltered copy with one
// roughness per mip, a cosine-convolved irradiance map, and the BRDF
// integration table. Rebuilt only when the HDRI's texture or revision moves.
//
// Post: the scene is drawn in linear HDR (RGBA16F); this pass adds bloom from
// what is brighter than the threshold, tonemaps (ACES, Reinhard, AgX, Khronos
// PBR Neutral), encodes to sRGB with a dither and hands an RGBA8 image to the
// 2D graph. Data passes (depth, normals, position) go through untouched.
class Render3DPost
{
public:
   ~Render3DPost();

   // Prefiltered / irradiance maps for `envTex` (w x h, mips 0..maxLod).
   // Returns false when it cannot build them (the caller keeps the old path).
   bool EnsureIbl(unsigned int envTex, int envW, int envH, float envMaxLod, unsigned long long revision);
   unsigned int PrefilteredTexture() const { return mPrefTex; }
   unsigned int IrradianceTexture() const { return mIrrTex; }
   unsigned int BrdfLut();
   float PrefilteredMaxLod() const { return (float)(mPrefLevels - 1); }

   // HDR `src` (w x h) -> RGBA8 output. passthrough: copy only (data passes).
   // depthTex + bg: where nothing was drawn (depth 1) the output is the plain
   // background colour, as it always was (glClear, never tonemapped), with the
   // bloom halo screened over it. depthTex 0 = tonemap everything (HDRI bg).
   unsigned int Finish(unsigned int src, int w, int h, int tonemap, float bloom, float bloomThreshold,
                       float bloomRadius, bool passthrough, unsigned int depthTex, const float bg[3]);
   unsigned int Output() const { return mOutTex; }

private:
   bool EnsurePrograms();
   void EnsureTargets(int w, int h);
   void DrawFullscreen();

   unsigned int mVao = 0;
   unsigned int mPrefProgram = 0, mIrrProgram = 0, mLutProgram = 0;
   unsigned int mBrightProgram = 0, mBlurProgram = 0, mFinishProgram = 0;
   bool mProgramsFailed = false;

   unsigned int mPrefTex = 0, mIrrTex = 0, mLutTex = 0, mIblFbo = 0;
   int mPrefLevels = 0;
   unsigned int mIblSource = 0;
   unsigned long long mIblRevision = 0;

   static constexpr int kBloomLevels = 6;
   unsigned int mOutTex = 0, mOutFbo = 0;
   unsigned int mBloomTex[kBloomLevels][2] = {};
   unsigned int mBloomFbo[kBloomLevels][2] = {};
   int mBloomW[kBloomLevels] = {}, mBloomH[kBloomLevels] = {};
   int mWidth = 0, mHeight = 0;
};

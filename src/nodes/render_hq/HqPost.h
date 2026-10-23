#pragma once
// Turbo 0.71 (S8): Render 3D HQ's own finish - Filament's bloom (a threshold
// pass to about 384 rows, a mip chain of `levels` with the 9 / 13-tap
// filters, a 3x3-tent upsample added back level by level) composited in
// linear HDR before the tonemap, then Infinite's tonemaps or Filament's ACES,
// sRGB and dither into RGBA8. Until S7 the HQ used Render 3D's Render3DPost;
// that one stays as it was (Render 3D is frozen). Part of the optional module
// (INFINITE_ENABLE_RENDER_HQ). See docs/RENDER-3D-HQ-FILAMENT.md section 12.

struct HqPostSettings
{
   int tonemap = 1;           // Render 3D's 0-4, 5 Filament ACES legacy (its default), 6 Filament ACES
   float bloom = 0.0f;        // Filament's strength (0 = off)
   float threshold = 1.0f;    // subtracted before the bloom (Filament: 1.0)
   int levels = 6;            // Filament's levels (1..)
   bool fireflies = true;     // Karis average in the first pass (Filament: off under TAA)
};

class HqPost
{
public:
   ~HqPost() { Release(); }

   // HDR `src` (w x h) -> RGBA8. keyMode 0: tonemap everything (an HDRI
   // behind); 1: `key` is the depth, the plain background `bg` shows where it
   // is 1 (bloom screened over it); 4: `key` is the TAA coverage. Returns 0
   // when its shaders did not build.
   unsigned int Finish(unsigned int src, int w, int h, const HqPostSettings& s, int keyMode, unsigned int key,
                       const float bg[3], float bgOpacity);
   // Levels the last Finish() drew (0: no bloom).
   int LastBloomLevels() const { return mLastLevels; }
   void Release();

   static const int kMaxLevels = 11; // Filament's BloomOptions.levels range

private:
   bool EnsurePrograms();
   void EnsureTargets(int w, int h);
   void Draw(unsigned int fbo, int w, int h);

   struct DownProgram
   {
      unsigned int id = 0;
      int mode = -1, threshold = -1, invHighlight = -1, fireflies = -1;
   } mDown;
   struct UpProgram
   {
      unsigned int id = 0;
      int dstTexel = -1, base = -1, source = -1;
   } mUp;
   struct FinishProgram
   {
      unsigned int id = 0;
      int bloomStrength = -1, keyMode = -1, bg = -1, bgOpacity = -1, tonemap = -1;
   } mFin;
   bool mTried = false;
   unsigned int mVao = 0;

   int mW = 0, mH = 0;
   unsigned int mOut = 0, mOutFbo = 0;
   // Full size -> about 2 x 384 rows: the pre-downscale steps (the first one
   // thresholds), then the bloom chain itself, one texture per level.
   static const int kMaxPre = 4;
   unsigned int mPre[kMaxPre] = {}, mPreFbo[kMaxPre] = {};
   int mPreW[kMaxPre] = {}, mPreH[kMaxPre] = {};
   int mPreCount = 0;
   unsigned int mChain[kMaxLevels] = {}, mChainFbo[kMaxLevels] = {};
   // Level i plus everything below it upsampled (mSum[0] is the bloom).
   unsigned int mSum[kMaxLevels] = {}, mSumFbo[kMaxLevels] = {};
   int mChainW[kMaxLevels] = {}, mChainH[kMaxLevels] = {};
   int mChainCount = 0;
   int mLastLevels = 0;
};

// HqRenderer.cpp: a vertex + fragment program from shaders/ (//@include
// resolved), labelled; 0 with the log on stderr.
unsigned int HqBuildProgram(const char* vertName, const char* fragName);

#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// --- Feedback -----------------------------------------------------------
// Outputs what its input produced on the PREVIOUS frame. That one-frame delay
// is what makes cycles legal: patch a node's output back into something
// upstream through a Feedback and the graph can loop without the cook
// recursing forever. This is the primitive the whole trails/echo/growth family
// is built on.
class FeedbackNode : public INode
{
public:
   static INode* Create() { return new FeedbackNode(); }

   ~FeedbackNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mBuffers[1 - mWrite]); }
   int GetOutputWidth() const override { return mBuffers[1 - mWrite].w; }
   int GetOutputHeight() const override { return mBuffers[1 - mWrite].h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   void Clear();

   float delayFrames = 1.0f; // held for information; the delay is always one frame

   void VisitParams(ParamVisitor& v) override { v.Float("delayFrames", delayFrames); }

private:
   bool EnsureShader();

   ImageCable mInput;
   GLUtil::Fbo mBuffers[2];
   int mWrite = 0;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

// --- Trails -------------------------------------------------------------
// Self-contained feedback: keeps a decaying accumulation of everything that has
// passed through it, with optional drift, zoom and rotation applied to the
// history each frame. Echo/motion-trail effects without wiring a cycle by hand.
class TrailsNode : public INode
{
public:
   static INode* Create() { return new TrailsNode(); }

   ~TrailsNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mBuffers[mFront]); }
   int GetOutputWidth() const override { return mBuffers[mFront].w; }
   int GetOutputHeight() const override { return mBuffers[mFront].h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   void Clear() { mNeedsClear = true; }

   float decay = 0.92f;
   float zoom = 1.0f;
   float rotate = 0.0f;
   float driftX = 0.0f;
   float driftY = 0.0f;
   float hueShift = 0.0f;
   int blendMode = 0; // 0 = max, 1 = add, 2 = screen

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("decay", decay); v.Float("zoom", zoom); v.Float("rotate", rotate);
      v.Float("driftX", driftX); v.Float("driftY", driftY);
      v.Float("hueShift", hueShift); v.Int("blendMode", blendMode);
   }

private:
   bool EnsureShader();

   ImageCable mInput;
   GLUtil::Fbo mBuffers[2];
   int mFront = 0;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   bool mNeedsClear = true;
   int mLastCookFrame = -1;
};

// --- Reaction-Diffusion -------------------------------------------------
// Gray-Scott, ping-ponged. A pure generator when nothing is patched in; with an
// input it uses the image's luminance to vary the feed rate, so the pattern
// grows differently through light and dark areas of the source.
class ReactionDiffusionNode : public INode
{
public:
   static INode* Create() { return new ReactionDiffusionNode(); }
   static const std::vector<std::string>& PresetNames();

   ~ReactionDiffusionNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mDisplay); }
   int GetOutputWidth() const override { return mDisplay.w; }
   int GetOutputHeight() const override { return mDisplay.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   void Reseed() { mNeedsSeed = true; }
   void ApplyPreset(int index);

   int preset = 0;
   float width = 512.0f;
   float height = 512.0f;
   float feed = 0.037f;
   float kill = 0.06f;
   float diffuseA = 1.0f;
   float diffuseB = 0.5f;
   float stepsPerFrame = 8.0f;
   float sourceInfluence = 0.0f;
   float lowColor[3] = { 0.02f, 0.02f, 0.06f };
   float highColor[3] = { 0.95f, 0.85f, 0.6f };

   // The simulated chemical field itself is not persisted - like DrawNode's
   // canvas, it is runtime state that reseeds from these params rather than a
   // value to round-trip.
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("preset", preset); v.Float("width", width); v.Float("height", height);
      v.Float("feed", feed); v.Float("kill", kill);
      v.Float("diffuseA", diffuseA); v.Float("diffuseB", diffuseB);
      v.Float("stepsPerFrame", stepsPerFrame); v.Float("sourceInfluence", sourceInfluence);
      v.Color("lowColor", lowColor); v.Color("highColor", highColor);
   }

private:
   bool EnsureShaders();
   void Seed(int w, int h);

   ImageCable mInput;
   GLUtil::Fbo mState[2];   // RG = chemical A/B
   GLUtil::Fbo mDisplay;
   int mFront = 0;
   unsigned int mSimProgram = 0;
   unsigned int mSeedProgram = 0;
   unsigned int mDrawProgram = 0;
   bool mShaderTried = false;
   bool mNeedsSeed = true;
   int mLastCookFrame = -1;
};

// --- Datamosh (Turbo 0.51) ----------------------------------------------
// Codec-style datamosh: blocks that are "held" keep sampling the previous output,
// displaced by a motion vector estimated between the previous and current frame
// of the motion source (the image itself when no motion source is patched).
// Refreshing ("I-frames", on a tempo grid or by trigger) copies the live image
// back into a share of the blocks. Renders every frame (state lives in the node).
class DatamoshNode : public INode
{
public:
   static INode* Create() { return new DatamoshNode(); }
   ~DatamoshNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut[mFront]); }
   int GetOutputWidth() const override { return mOut[mFront].w; }
   int GetOutputHeight() const override { return mOut[mFront].h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   ImageCable& MotionInput() { return mMotion; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "image" : (slot == 1 ? "motion" : nullptr); }
   void Refresh() { mRefreshPending = true; }

   static const std::vector<std::string>& BlockNames();
   static const std::vector<std::string>& RefreshNames();

   float mosh = 0.6f;        // share of blocks held (moshed)
   float gain = 1.0f;        // motion vector multiplier
   float bloom = 0.3f;       // how much of the last vector keeps accumulating
   float threshold = 0.1f;   // motion below this (0..1 = 0..8 px) is ignored
   int block = 1;            // 0 = 8, 1 = 16, 2 = 32 px
   float blockVar = 0.0f;    // 0 = uniform grid, 1 = widely varying block sizes
   float leak = 0.0f;        // mosh bleeding into refreshed blocks
   int refresh = 0;          // 0 off, 1 bar, 2 beat, 3 1/2, 4 1/4, 5 1/8
   float refreshChance = 1.0f; // share of blocks reset on each refresh
   float seed = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("mosh", mosh); v.Float("gain", gain); v.Float("bloom", bloom);
      v.Float("threshold", threshold); v.Int("block", block); v.Float("blockVar", blockVar);
      v.Float("leak", leak); v.Int("refresh", refresh); v.Float("refreshChance", refreshChance);
      v.Float("seed", seed);
   }

private:
   bool EnsureShaders();

   ImageCable mInput;
   ImageCable mMotion;
   GLUtil::Fbo mOut[2];   // previous / current output (RGBA16F)
   GLUtil::Fbo mMv[2];    // one texel per 16 px block: accumulated motion vector (RG16F)
   GLUtil::Fbo mPrevIn;   // previous frame of the motion source
   int mFront = 0;        // index of the latest output
   int mMvFront = 0;
   unsigned int mMeProgram = 0;
   unsigned int mMoshProgram = 0;
   unsigned int mCopyProgram = 0;
   bool mShaderTried = false;
   bool mNeedsInit = true;
   bool mHavePrevIn = false;
   bool mRefreshPending = false;
   long long mLastTick = -1;
   unsigned int mEpoch = 0;
   int mLastCookFrame = -1;
};

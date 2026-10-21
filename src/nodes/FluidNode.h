#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// --- Fluid --------------------------------------------------------------
// A 2D fluid on the GPU (stable fluids, Stam 1999), and the picture rides in
// it. The input image is the dye: it is carried along by the flow, so a video
// smears, swirls and folds as the fluid moves. Each frame: stir (forces and
// the built-in stirrer), move the velocity along itself, add back the swirl
// that smoothing takes out (vorticity), then make the flow divergence-free
// with a pressure solve, and move the dye along it.
//
// Inputs: image (the dye), force (a motion image, e.g. Optical Flow's raw
// output: R,G are pixels per frame of that image), obstacles (a picture whose
// bright areas are solid; the fluid flows around them).
//
// The simulation runs at the input divided by `resolution`; the dye keeps the
// input's full size. Velocity is in simulation texels per frame. State
// persists across frames, so the node steps once per cook and Reset clears it.
class FluidNode : public INode
{
public:
   static INode* Create() { return new FluidNode(); }
   static const std::vector<std::string>& StirrerNames();
   static const std::vector<std::string>& ViewNames();
   static constexpr int kMaxPressureIterations = 120;

   ~FluidNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   ImageCable& Force() { return mForce; }
   ImageCable& Obstacles() { return mObstacles; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "image" : (slot == 1 ? "force" : "obstacles");
   }
   void Reset() { mReset = true; }

   // Largest |divergence| of the velocity field, in simulation texels per
   // frame per texel, over cells away from obstacles. Reads the GPU back, so
   // for tests only. Negative if there is nothing to measure yet.
   float MeasureDivergence();

   int resolution = 1;        // 0 full, 1 half, 2 quarter
   int pressureIterations = 20;
   float viscosity = 0.0f;    // 0..0.2, spreads velocity into its neighbours
   float velocityDamping = 0.005f;
   float vorticity = 0.3f;    // swirl kept in the flow
   float dyeFade = 0.0f;      // dye drains toward black per frame
   float refresh = 0.01f;     // how fast the input image replaces the dye
   float forceGain = 1.0f;    // scales the force input
   int stirrer = 1;           // built-in stirrer: off / orbit / pulse / spin
   float stirrerStrength = 1.0f;
   float stirrerSize = 0.06f; // as a fraction of the image height
   int view = 0;              // dye / velocity / pressure / obstacles

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("resolution", resolution); v.Int("pressureIterations", pressureIterations);
      v.Float("viscosity", viscosity); v.Float("velocityDamping", velocityDamping);
      v.Float("vorticity", vorticity); v.Float("dyeFade", dyeFade); v.Float("refresh", refresh);
      v.Float("forceGain", forceGain); v.Int("stirrer", stirrer);
      v.Float("stirrerStrength", stirrerStrength); v.Float("stirrerSize", stirrerSize);
      v.Int("view", view);
   }

private:
   bool EnsureShaders();
   void DestroyTargets();

   ImageCable mInput, mForce, mObstacles;
   GLUtil::Fbo mVel[2], mPressure[2], mDiv, mCurl, mObs, mDye[2], mOut;
   // The pressure solve is a multigrid V-cycle: level 0 is the simulation grid
   // (mPressure, mDiv, mObs, with the residual in mCurl's place), levels 1.. are
   // each half the size of the one before.
   static constexpr int kMaxLevels = 5;
   GLUtil::Fbo mLvP[kMaxLevels][2], mLvRhs[kMaxLevels], mLvRes[kMaxLevels], mLvObs[kMaxLevels];
   int mLvW[kMaxLevels] = {}, mLvH[kMaxLevels] = {}, mLevels = 1;
   int mVelCur = 0, mPressCur = 0, mDyeCur = 0;
   int mSimW = 0, mSimH = 0, mDyeW = 0, mDyeH = 0;
   bool mReset = true;
   bool mHaveVelocity = false;
   int mStep = 0;
   unsigned int mProgForce = 0, mProgAdvectVel = 0, mProgCurl = 0, mProgVort = 0, mProgDiv = 0;
   unsigned int mProgVortApply = 0, mProgResid = 0, mProgRestrict = 0, mProgProlong = 0, mProgObsDown = 0;
   unsigned int mProgJacobi = 0, mProgGrad = 0, mProgObs = 0, mProgAdvectDye = 0, mProgOut = 0, mProgZero = 0;
   bool mShadersTried = false;
   int mLastCookFrame = -1;
};

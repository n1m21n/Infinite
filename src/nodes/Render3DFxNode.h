#pragma once

#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Mesh.h"

class Render3DNode;

// --- Render 3D FX -----------------------------------------------------------
// Turbo 0.70: screen-space finishing for a Render 3D, as a node after it (the
// way upstream's Volume reads the render's camera and depth), so Render 3D
// itself stays close to upstream:
//   ambient occlusion  - contact shadows where surfaces close in on each other
//                        (from the render's depth);
//   reflections        - screen-space: what the picture already shows,
//                        mirrored in its surfaces;
//   depth of field     - blur away from a focus distance;
//   motion blur        - along each pixel's motion since last frame, for a
//                        shutter of up to several frames (from the render's
//                        velocity pass: camera, moved objects, every copy).
// With any other image, or a Render 3D in RTX mode (no depth), the picture
// passes through. Compute shaders (GL 4.3): shaders/render3d_fx_*.comp.glsl and
// render3d_motion_blur.comp.glsl.
class Render3DFxNode : public INode
{
public:
   static INode* Create() { return new Render3DFxNode(); }
   ~Render3DFxNode() override;

   unsigned int GetOutputTexture() override { return mActive ? mOutTex : mPassTex; }
   int GetOutputWidth() const override { return mW; }
   int GetOutputHeight() const override { return mH; }
   unsigned int GetOutputFormat() const override { return mActive ? 0x881A : mPassFormat; } // GL_RGBA16F
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int) const override { return "render"; }

   // What the last cook found upstream, for the panel.
   bool HasRender() const { return mHasRender; }
   bool HasDepth() const { return mHasDepth; }

   float aoAmount = 0.8f;      // 0 = off
   float aoRadius = 0.4f;      // world units
   float reflections = 0.0f;   // 0 = off
   float reflectDistance = 4.0f;
   float dofBlur = 0.0f;       // largest blur, pixels; 0 = off
   float focusDistance = 3.0f; // world units from the camera
   float focusRange = 0.5f;    // sharp band around it, +- world units
   float shutter = 0.0f;       // motion blur, in frames; 0 = off

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("aoAmount", aoAmount); v.Float("aoRadius", aoRadius);
      v.Float("reflections", reflections); v.Float("reflectDistance", reflectDistance);
      v.Float("dofBlur", dofBlur); v.Float("focusDistance", focusDistance); v.Float("focusRange", focusRange);
      v.Float("shutter", shutter);
   }

private:
   bool EnsurePrograms();
   void EnsureTargets(int w, int h);

   ImageCable mInput;
   unsigned int mAoProgram = 0, mCompositeProgram = 0, mDofProgram = 0, mBlurProgram = 0;
   bool mTried = false;
   unsigned int mAoTex = 0, mTex[2] = { 0, 0 };
   unsigned int mTileTex = 0;     // motion blur: each 16x16 tile's longest motion
   unsigned int mNeighborTex = 0; // and the longest that reaches each tile
   unsigned int mOutTex = 0;     // the last pass's target
   unsigned int mPassTex = 0;    // the input, when nothing applies
   unsigned int mPassFormat = 0x8058;
   int mW = 0, mH = 0, mTexW = 0, mTexH = 0;
   bool mActive = false, mHasRender = false, mHasDepth = false;
   int mLastCookFrame = -1;
   unsigned long long mRevision = 0;
   // The last cook's inputs (cook again only when one changed).
   unsigned long long mLastInputRevision = ~0ull;
   std::vector<float> mLastParams;
   unsigned int mLastVelocity = 0;
};

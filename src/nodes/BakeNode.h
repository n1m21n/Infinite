#pragma once

#include <future>
#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// Geometry in, image out: the mesh rendered in its own UV space (Blender's
// bake, written fresh). AO, curvature, position, normal or thickness, so the
// image can go through any 2D chain and back into a Material map.
//
// Baked on the CPU by ray casts against a BVH of the mesh, in object space.
// The first bake runs in the cook so the image is there at once; later ones
// (the mesh or a param changed) run on a worker thread and the last image
// stays up until the new one lands.
class BakeNode : public INode, public ICookWarningSource
{
public:
   static INode* Create() { return new BakeNode(); }
   ~BakeNode() override;

   enum Mode { kAO = 0, kCurvature, kPosition, kNormal, kThickness };
   static const std::vector<std::string>& ModeNames();
   static const std::vector<std::string>& SizeNames();

   unsigned int GetOutputTexture() override { return mTex; }
   int GetOutputWidth() const override { return mTexSize; }
   int GetOutputHeight() const override { return mTexSize; }
   unsigned long long TextureRevision() const override { return mTexRevision; }
   void CookIfNeeded(int frameId) override;

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   const std::string& CookWarning() const override { return mCookWarning; }
   bool Baking() const { return mJob.valid(); }

   int mode = kAO;
   int size = 1;              // 256, 512, 1024, 2048
   int samples = 16;          // rays per texel (AO, thickness)
   float distance = 0.25f;    // ray reach, in mesh sizes (AO, thickness)

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode); v.Int("size", size);
      v.Int("samples", samples); v.Float("distance", distance);
   }

   struct Request
   {
      Mesh mesh;
      int mode = 0, res = 512, samples = 16;
      float distance = 0.25f;
   };
   // RGBA float, res x res, rows from v = 0. Public for the self-test.
   static std::vector<float> Bake(const Request& r);

private:
   void Upload(const std::vector<float>& pixels, int res);

   std::string mCookWarning;
   int mLastCookFrame = -1;
   unsigned int mTex = 0;
   int mTexSize = 0;
   unsigned long long mTexRevision = 0;

   // What the image (or the running job) was baked from.
   unsigned long long mKeyMesh = 0;
   int mKeyMode = -1, mKeySize = -1, mKeySamples = -1;
   float mKeyDistance = -1.0f;
   const void* mKeyInput = nullptr;
   bool mDirty = false;   // changed while a job was running: bake again after it
   std::future<std::vector<float>> mJob;
   int mJobRes = 0;
};

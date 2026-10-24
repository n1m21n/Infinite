#pragma once

#include <vector>

#include "INode.h"
#include "Geometry3DNodes.h"
#include "GpuPoints.h"

// --- Feedback 3D --------------------------------------------------------
// The 3D twin of Feedback (FeedbackNodes.h): a one-frame delay that makes a
// geometry cycle legal, so the end of a point chain can be patched back into
// its start - growth, flocking, custom particle sims, with no new mental
// model. Two pins:
//   seed - where the loop starts: output on the first cook, on Reset, and
//          whenever the seed cable changes; with nothing on `loop` the node is
//          a pass-through of the seed.
//   loop - the end of the chain that reads this node. Each cook the node
//          captures what `loop` made from the previous capture, and outputs it.
// GetMesh/GetPointCloud/GetGpuPoints only ever hand out the stored capture and
// never pull `loop`, which is what stops the cycle recursing. The capture is
// baked into world space (the source's model matrix applied), so the node's
// own matrix is identity and a Transform inside the loop accumulates.
//
// Points stay on the GPU when the loop offers GPU points: the capture is a
// copy pass into one of two targets (ping-pong, so a loop that forwards this
// node's own texels never reads the texture being written). The CPU cloud is
// read back only when a CPU consumer asks.
class GeometryFeedbackNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new GeometryFeedbackNode(); }
   ~GeometryFeedbackNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   enum Slot { kSeedSlot = 0, kLoopSlot = 1 };
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return slot == kSeedSlot ? &seed : (slot == kLoopSlot ? &loop : nullptr);
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kNames[] = { "seed", "loop" };
      return (slot >= 0 && slot < 2) ? kNames[slot] : nullptr;
   }

   const Mesh& GetMesh() override { return mMesh; }
   unsigned long long MeshRevision() override { return mMeshRevision; }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   // The look comes from the seed, never from `loop`: forwarding from the loop
   // would walk the cycle back into this node.
   Material GetMaterial() const override { return seed ? seed->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override { return seed ? seed->MaterialRevision() : 0; }
   unsigned int GetSurfaceTexture() override { return seed ? seed->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return seed ? seed->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override { return seed ? seed->SurfaceTextureRevision() : 0; }
   MappingTransform GetMappingTransform() const override
   {
      return seed ? seed->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override { return seed ? seed->MappingRevision() : 0; }
   // PassthroughSource stays nullptr: the capture originates here, and a walk
   // that went on would go round the cycle.

   const std::vector<Particle>* GetPointCloud() override;
   unsigned long long PointCloudRevision() override { return mHasCloud ? mCloudRevision : 0; }
   float PointBaseSize() const override { return mBaseSize; }
   const GpuPoints* GetGpuPoints() override;

   // Start over from the seed on the next cook.
   void Reset() { mNeedsSeed = true; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }
   size_t PointCount() const { return mHasCloud ? (size_t)(mCloudOnGpu ? mGpu.count : (int)mCloud.size()) : 0; }
   // True while `loop` drives the output (not a pass-through of the seed).
   bool Looping() const { return mLooping; }
   // Captures since the last seed, for the node face.
   int Generation() const { return mGeneration; }

   IGeometrySource* seed = nullptr;
   IGeometrySource* loop = nullptr;

private:
   void Capture(IGeometrySource* src, bool force);
   bool CopyGpu(const GpuPoints& src, const Mat4& bake);
   void ReadBack();

   Mesh mMesh;
   unsigned long long mMeshRevision = 0;
   std::vector<Particle> mCloud;
   bool mHasCloud = false;
   unsigned long long mCloudRevision = 0;
   float mBaseSize = 1.0f;

   // What the current capture was taken from, so a source that has not
   // changed is not copied again (and a loop that settles stops moving).
   IGeometrySource* mMeshFrom = nullptr;
   unsigned long long mMeshFromRev = 0;
   Mat4 mMeshFromMatrix;
   IGeometrySource* mCloudFrom = nullptr;
   unsigned long long mCloudFromRev = 0;
   Mat4 mCloudFromMatrix;

   bool mNeedsSeed = true;
   IGeometrySource* mSeededFrom = nullptr;
   bool mLooping = false;
   int mGeneration = 0;
   int mLastCookFrame = -1;

   // GPU state. mCloudOnGpu: the capture is in mTargets[mFront] and mCloud is
   // filled from it on demand (mCpuStale). Otherwise mCloud is the capture and
   // GetGpuPoints uploads it once per revision.
   GpuPointsGL::Target mTargets[2];
   int mFront = 0;
   GpuPoints mGpu;
   bool mCloudOnGpu = false;
   bool mCpuStale = false;
   bool mGpuValid = false;
   unsigned long long mGpuUploadedRev = 0;
   unsigned int mCopyProgram = 0;
   bool mCopyTried = false;
};

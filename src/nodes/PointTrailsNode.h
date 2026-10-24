#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Geometry3DNodes.h"

// --- Point Trails -------------------------------------------------------
// Draws where each point has been: points in, a mesh of tails out, so a
// Particle System gives comet tails the camera can fly through. Main's 2D
// Trails smears pixels; this makes geometry.
//
// Each point keeps a ring of its last `length` positions (and colours), taken
// in world space so a moving or spinning source leaves its tails behind. A new
// sample is taken when the input's points or matrix change, every
// `sampleEvery`-th change, so a paused transport freezes the tails instead of
// collapsing them. The tails come out as three-sided prisms, the same lines
// Proximity Lines draws, so they render and take materials like any mesh:
// `width` is the head radius, `taper` thins the tail, `fade` darkens it.
//
// Matching: point i is tracked as slot i. By Particle ID, a slot whose
// particle died or was reborn (its age went back) starts a new tail, so a
// recycled particle does not draw a line from where the old one died. By
// Index, slot i is followed whatever happens to it (static clouds, Image to
// Points). Hidden points (dead, or size 0) break the tail either way.
//
// Guards: at most `pointLimit` points are tracked, and at most kMaxSamples
// samples in total (points x length), so a long tail on a big cloud tracks
// fewer points rather than running out of memory.
class PointTrailsNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new PointTrailsNode(); }

   static constexpr int kMaxSamples = 1000000;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   // Bypassed, the input's points go straight through.
   const Mesh& GetMesh() override { return bypassed && input ? input->GetMesh() : mMesh; }
   unsigned long long MeshRevision() override { return bypassed && input ? input->MeshRevision() : mMeshRevision; }
   const std::vector<Particle>* GetPointCloud() override { return bypassed && input ? input->GetPointCloud() : nullptr; }
   unsigned long long PointCloudRevision() override { return bypassed && input ? input->PointCloudRevision() : 0; }
   const GpuPoints* GetGpuPoints() override { return bypassed && input ? input->GetGpuPoints() : nullptr; }
   const Polyline* GetCurve() override { return bypassed && input ? input->GetCurve() : nullptr; }
   // The tails are baked into world space.
   Mat4 GetModelMatrix() const override { return bypassed && input ? input->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override { return input ? input->MaterialRevision() : 0; }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return input ? input->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override { return input ? input->SurfaceTextureRevision() : 0; }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   unsigned long long MappingRevision() const override { return input ? input->MappingRevision() : 0; }
   // PassthroughSource stays nullptr: the tail mesh originates here.

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "points"; }

   enum ColorMode { kColorFromPoints = 0, kColorGradient = 1 };
   enum MatchMode { kMatchParticleId = 0, kMatchIndex = 1 };
   static const std::vector<std::string>& ColorModeNames()
   {
      static const std::vector<std::string> k = { "From points", "Gradient over age" };
      return k;
   }
   static const std::vector<std::string>& MatchModeNames()
   {
      static const std::vector<std::string> k = { "Particle ID", "Index" };
      return k;
   }

   int length = 30;          // samples per tail, 2..240
   int sampleEvery = 1;      // take every Nth input frame, 1..10
   float width = 0.01f;      // head radius, 0.0005..0.2
   float fade = 1.0f;        // tail darkens with age, 0..1
   float taper = 0.5f;       // tail thins with age, 0..1
   int colorMode = kColorFromPoints;
   float headColor[3] = { 1.0f, 0.95f, 0.85f };
   float tailColor[3] = { 0.95f, 0.42f, 0.33f };
   int matchMode = kMatchParticleId;
   int pointLimit = 10000;   // points tracked, 1..100000

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("length", length); v.Int("sampleEvery", sampleEvery); v.Float("width", width);
      v.Float("fade", fade); v.Float("taper", taper); v.Int("colorMode", colorMode);
      v.Color("headColor", headColor); v.Color("tailColor", tailColor); v.Int("matchMode", matchMode);
      v.Int("pointLimit", pointLimit);
   }

   // Start every tail over on the next sample.
   void Clear() { mClear = true; }
   // Points that currently draw a tail (two samples or more), for the node face.
   size_t TrailCount() const { return mTrails; }
   // Points tracked after the point limit and the sample budget.
   int TrackedPoints() const { return mSlots; }
   // Samples taken since the last clear.
   int SamplesTaken() const { return mSamples; }

private:
   void Sample(const std::vector<Particle>* cloud, const Mesh* mesh, const Mat4& model);
   void BuildMesh();

   struct Slot
   {
      int head = 0;          // ring index of the newest sample
      int count = 0;         // valid samples, newest back
      float lastAge = 0.0f;
      bool lastAlive = false;
   };

   // Ring storage: slot i's sample k lives at (i * mLength + k), xyz and rgb.
   std::vector<float> mPos;
   std::vector<float> mCol;
   std::vector<Slot> mSlotState;
   int mSlots = 0;
   int mLength = 0;
   int mSamples = 0;
   size_t mTrails = 0;
   bool mClear = true;

   // What the last sample was taken from.
   IGeometrySource* mFrom = nullptr;
   unsigned long long mFromRev = 0;
   Mat4 mFromMatrix;
   int mFramesSinceSample = 0;

   // What the mesh was last built with (look-only params rebuild without sampling).
   float mBuiltWidth = -1.0f, mBuiltFade = -1.0f, mBuiltTaper = -1.0f;
   int mBuiltColorMode = -1;
   float mBuiltHead[3] = { -1, -1, -1 }, mBuiltTail[3] = { -1, -1, -1 };
   bool mDirty = false;

   Mesh mMesh;
   unsigned long long mMeshRevision = 0;
   int mLastCookFrame = -1;
};

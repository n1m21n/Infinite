#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Mesh.h"
#include "Geometry3DNodes.h"

// The 3D counterpart of ResynthNode: an iterative generative mutator. Each
// generation reads the *previous generation* rather than the input, so the mesh
// drifts away from its source over time instead of applying a fixed effect.
//
// Determinism is the point. The mutation for generation N is a pure function of
// (seed, N, weights), so the same patch replays the same evolution - you can
// find a shape you like at generation 40 and it will still be there tomorrow.
class MeshResynthNode : public INode, public IGeometrySource
{
public:
   // The eight mutation operators. Each has a weight; every generation applies
   // all of them scaled by weight * chaos, so the character comes from the mix
   // rather than from picking one.
   enum Op
   {
      kDisplace = 0,   // push along normals by value noise
      kJitter,         // uncorrelated per-vertex offset
      kSmooth,         // Taubin relax - the only operator that *removes* detail
      kTwist,          // rotate about Y proportional to height
      kBulge,          // radial scale by noise
      kExtrudeFaces,   // extrude a random subset of faces
      kSubdivide,      // Loop subdivision, budget-capped
      kSquash,         // per-axis non-uniform wobble
      kOpCount
   };
   static const std::vector<std::string>& OpNames();

   static INode* Create() { return new MeshResynthNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override
   {
      if (bypassed)
      {
         static const Mesh kEmptyMesh;
         return input ? input->GetMesh() : kEmptyMesh;
      }
      return mMesh;
   }
   unsigned long long MeshRevision() override
   {
      return bypassed ? (input ? input->MeshRevision() : 0) : mRevision;
   }
   // Mutating a mesh does not relocate it, the same reasoning the geometry
   // operators use for forwarding their input's placement.
   Mat4 GetModelMatrix() const override
   {
      return input ? input->GetModelMatrix() : Mat4::Identity();
   }
   Material GetMaterial() const override
   {
      return input ? input->GetMaterial() : Material();
   }
   unsigned int GetSurfaceTexture() override
   {
      return input ? input->GetSurfaceTexture() : 0;
   }
   unsigned int GetMaterialTexture(int map) override
   {
      return input ? input->GetMaterialTexture(map) : 0;
   }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   IGeometrySource* PassthroughSource() const override { return input; }
   // MeshResynth mutates mesh vertices only - a splat cloud upstream has
   // nothing to mutate, so it forwards untouched.
   const SplatIO::SplatCloud* GetSplatCloud() override
   {
      return input ? input->GetSplatCloud() : nullptr;
   }
   unsigned long long SplatCloudRevision() override
   {
      return input ? input->SplatCloudRevision() : 0;
   }
   Mat4 GetInstanceGroupMatrix() const override
   {
      return input ? input->GetInstanceGroupMatrix() : Mat4::Identity();
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return input ? input->InstanceTransformOverride() : nullptr;
   }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }

   // --- generation control, mirroring ResynthNode's transport ---
   void StepOnce() { mPendingSteps++; }
   void Reset() { mNeedsReset = true; }
   void Randomise();
   int Generation() const { return mGeneration; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }

   float weight[kOpCount] = { 1.0f, 0.3f, 0.5f, 0.2f, 0.4f, 0.15f, 0.0f, 0.2f };
   float chaos = 0.35f;
   float seed = 0.0f;
   bool autoStep = false;
   float stepsPerBeat = 1.0f;
   // Loop subdivision quadruples the triangle count, so an unattended auto-step
   // patch would climb into the millions within a minute. This is the stop.
   int triangleBudget = 120000;

   void VisitParams(ParamVisitor& v) override
   {
      static const char* kKeys[kOpCount] = {
         "wDisplace", "wJitter", "wSmooth", "wTwist",
         "wBulge", "wExtrude", "wSubdivide", "wSquash"
      };
      for (int i = 0; i < kOpCount; i++)
         v.Float(kKeys[i], weight[i]);
      v.Float("chaos", chaos);
      v.Float("seed", seed);
      v.Bool("autoStep", autoStep);
      v.Float("stepsPerBeat", stepsPerBeat);
      v.Int("triangleBudget", triangleBudget);
   }

private:
   void ApplyGeneration(int generation);

   Mesh mMesh;
   unsigned long long mRevision = 0;
   int mGeneration = 0;
   int mPendingSteps = 0;
   bool mNeedsReset = true;

   int mLastCookFrame = -1;
   const void* mBuiltInput = nullptr;
   unsigned long long mBuiltInputRevision = 0;
   double mLastStepBeat = 0.0;
};

// Turns an image into a point cloud: one particle per sampled pixel, positioned
// on a plane with luminance pushing it out of that plane. The result feeds
// Instance on Points, so an image becomes a field of shapes.
class ImageToPointsNode : public INode, public IGeometrySource
{
public:
   enum DepthSource { kLuminance = 0, kRed, kAlpha, kFlat, kDepthSourceCount };
   static const std::vector<std::string>& DepthSourceNames();

   static INode* Create() { return new ImageToPointsNode(); }
   ~ImageToPointsNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const std::vector<Particle>& GetPoints() { return mPoints; }
   unsigned long long PointRevision() { return mRevision; }
   const std::vector<Particle>* GetPointCloud() override
   {
      if (bypassed)
         return nullptr;
      return &mPoints;
   }
   unsigned long long PointCloudRevision() override { return bypassed ? 0 : mRevision; }
   // Mirrors RebuildMeshIfNeeded()'s baseHalf (GenerativeNodes.cpp) so
   // Render3D's sprite draw and this node's own mini-viewport agree on what
   // p.scale = 1.0 actually measures.
   float PointBaseSize() const override
   {
      const int n = std::max(2, std::min(density, 512));
      const float cell = std::min(width, height) / (float)n;
      return cell * 0.45f;
   }

   // IGeometrySource: a swatch quad per point, each sampling its own texel of
   // the downsampled source image rather than the whole image tiled per-quad
   // (which is what MeshOps::PointsToFaces's 0..1 corner UVs would give).
   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override;
   // Never expose a surface texture here: CookIfNeeded (GenerativeNodes.cpp) already
   // bakes this same downsampled source image into each point's own instance color
   // (p.r/g/b) whenever useImageColor is on. Returning mSmall.tex here as well made
   // Geometry3DNodes.cpp's point-cloud shader multiply that same image data in twice
   // (once via vInstanceColor, again via the texture sample), crushing colored points
   // toward black while the white-tint (useImageColor off) path stayed at full brightness.
   unsigned int GetSurfaceTexture() override { return 0; }
   unsigned long long SurfaceTextureRevision() const override { return mRevision; }

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   size_t PointCount() const { return mPoints.size(); }

   // Sampling grid, not source resolution: a 4000px image at density 128 still
   // yields 128x128 points, so the cloud size is predictable from the parameter
   // rather than from whatever happens to be plugged in.
   int density = 96;
   float width = 2.0f;
   float height = 2.0f;
   int depthSource = kLuminance;
   float depthScale = 0.5f;
   float pointSize = 1.0f;
   // Pixels dimmer than this are dropped entirely, which is what makes a cutout
   // read as a shape rather than a solid rectangle with a dark region.
   float threshold = 0.05f;
   bool useImageColor = true;
   float tint[3] = { 1.0f, 1.0f, 1.0f };
   float sizeFromLuma = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("density", density);
      v.Float("width", width); v.Float("height", height);
      v.Int("depthSource", depthSource); v.Float("depthScale", depthScale);
      v.Float("pointSize", pointSize); v.Float("threshold", threshold);
      v.Bool("useImageColor", useImageColor); v.Color("tint", tint);
      v.Float("sizeFromLuma", sizeFromLuma);
   }

private:
   // Resolves the source into a density x density target before reading back,
   // so the CPU never sees the full-resolution image.
   bool EnsureDownsampler(int n);
   void RebuildMeshIfNeeded();

   ImageCable mInput;
   std::vector<Particle> mPoints;
   // Grid-cell UV for each entry in mPoints, in the same order, so the mini-
   // viewport mesh can give each swatch quad its own single texel to sample
   // instead of spreading the whole image across it.
   std::vector<std::pair<float, float>> mPointUv;
   std::vector<unsigned char> mPixels;
   unsigned long long mRevision = 0;

   GLUtil::Fbo mSmall;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   Mesh mCookedMesh;
   unsigned long long mCookedMeshRevision = 0;
   unsigned long long mBuiltMeshRevision = (unsigned long long)-1;
};

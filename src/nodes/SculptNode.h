#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "Sculpt.h"

// Holds a sculpt: an offset per point of the input mesh, painted with brushes
// in the 3D View's Sculpt mode (core/Sculpt). Amount blends it in like a shape
// key, so audio or an LFO can drive it; chain Sculpt nodes to layer sculpts.
// Level subdivides the input first so a coarse mesh has points to move.
// Any 2D node patched into "brush" shapes each dab: its brightness, laid on
// the view over the brush, scales how much each point moves.
//
// The offsets follow point numbers. If the input's points change (another
// subdivide, a different model), they stop applying, stay saved, and the node
// warns until the topology comes back or the sculpt is cleared.
class SculptNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new SculptNode(); }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return input ? input->GetModelMatrix() : Mat4::Identity(); }
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
   IGeometrySource* PassthroughSource() const override { return input; }
   Mat4 GetInstanceGroupMatrix() const override { return input ? input->GetInstanceGroupMatrix() : Mat4::Identity(); }
   const std::vector<Mat4>* InstanceTransformOverride() const override
   {
      return input ? input->InstanceTransformOverride() : nullptr;
   }
   const std::vector<unsigned char>* InstanceSelection() const override
   {
      return input ? input->InstanceSelection() : nullptr;
   }
   unsigned long long InstanceSelectionRevision() const override
   {
      return input ? input->InstanceSelectionRevision() : 0;
   }
   const std::vector<Particle>* GetPointCloud() override { return input ? input->GetPointCloud() : nullptr; }
   unsigned long long PointCloudRevision() override { return input ? input->PointCloudRevision() : 0; }
   float PointBaseSize() const override { return input ? input->PointBaseSize() : 1.0f; }
   const GpuPoints* GetGpuPoints() override { return input ? input->GetGpuPoints() : nullptr; }
   const Polyline* GetCurve() override { return input ? input->GetCurve() : nullptr; }
   unsigned long long CurveStamp() override { return input ? input->CurveStamp() : 0; }

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "geo" : "brush"; }
   ImageCable& BrushInput() { return mBrush; }
   // The brush image as brightness (read back when it changes), or null.
   const Sculpt::Alpha* BrushAlpha();
   const std::string& CookWarning() const override { return mCookWarning; }

   float amount = 1.0f;   // 0..2
   int level = 0;         // subdivide the input first, 0..3

   void VisitParams(ParamVisitor& v) override
   {
      // Encoded only when something reads or writes it; a load replaces the
      // text, and the next update decodes it.
      if (!mEncoded)
      {
         mData = Sculpt::Encode(mOffsets, mPoints);
         mDecodedFrom = mData;
         mEncoded = true;
      }
      v.Float("amount", amount);
      v.Int("level", level);
      v.Text("data", mData);
   }

   // For Sculpt mode. Sculptable: the offsets fit the mesh.
   bool Sculptable();
   const Mesh& Shaped() { Update(); return mMesh; }
   const Mesh& Base() { Update(); return mBase; }   // the input, after level
   const Sculpt::Topology& Topology() const { return mTopo; }
   // The shaped mesh's points (position, normal), kept up to date by
   // OffsetsChanged, and a grid over them for a brush of this radius.
   const std::vector<float>& PointPos();
   const std::vector<float>& PointNrm();
   const Sculpt::Grid* PointGrid(float radius);
   // The nearest hit on the shaped mesh (mesh space, d need not be unit).
   // During a stroke, on the mesh as the stroke began, as Blender lands a
   // drag's dabs: a ridge rises out of the brush and levels off instead of
   // climbing after it.
   bool Raycast(const float o[3], const float d[3], float& t);
   void BeginStroke();
   // The points' normals as the stroke began (null outside a stroke).
   const std::vector<float>* StrokeNormals() const { return mStrokeNrm.empty() ? nullptr : &mStrokeNrm; }
   void EndStroke();
   std::vector<float>& Offsets() { return mOffsets; }
   // After editing Offsets(). With the points a dab touched, only the mesh
   // around them is re-shaped.
   void OffsetsChanged(const std::vector<unsigned int>* touched = nullptr);
   bool HasSculpt() const;
   void Clear();

private:
   void Update();

   std::string mCookWarning;
   int mLastCookFrame = -1;
   ImageCable mBrush;
   Sculpt::Alpha mAlpha;
   unsigned long long mAlphaRevision = 0;
   unsigned int mAlphaTexture = 0, mReadFbo = 0;
   int mAlphaFrame = -1;
   std::string mData, mDecodedFrom;
   bool mEncoded = true;

   Mesh mBase, mMesh;
   Sculpt::Topology mTopo;
   std::vector<float> mOffsets;
   int mPoints = 0;                 // how many points the offsets were made for (0: none yet)
   float mMaxEdge = 0.0f;           // the base's longest edge (sizes the grid)
   const void* mKeyInput = nullptr;
   unsigned long long mKeyRevision = 0, mBaseRevision = 0;
   int mKeyLevel = -1;
   // What mMesh was shaped from; mReshaped: its normals were rebuilt (not
   // the base's), so a dab can re-shape just its neighbourhood.
   unsigned long long mShapeBase = ~0ull, mShapeOffsets = ~0ull;
   float mShapeAmount = -1.0f;
   bool mFits = false, mReshaped = false;
   unsigned long long mOffsetsRevision = 1, mMeshRevision = 0;
   std::vector<float> mPos, mNrm;
   bool mFrameStale = true;
   Sculpt::Grid mGrid;
   std::vector<unsigned int> mChanged;
   Mesh mStrokeMesh;
   std::vector<float> mStrokeNrm;
   Sculpt::Grid mStrokeGrid;
};

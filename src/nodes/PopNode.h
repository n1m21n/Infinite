#pragma once

// One node class for the whole PointZ family: it runs whichever pop::PopDef it
// was created for (see src/pop/PopOps.h). Geometry inputs accept any
// IGeometrySource; a non-PointZ source (Geometry, Model 3D, a Particle System,
// a Curve) is converted on the way in, so PointZ nodes mix freely with the existing
// 3D nodes. The output is a PointZ that Render 3D draws straight from the GPU,
// and that every other 3D node can still read as a mesh, cloud or curve.

#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "Geometry3DNodes.h"
#include "pop/PopOps.h"

class PopNode : public INode, public IGeometrySource
{
public:
   static const int kMaxInputs = 4;

   explicit PopNode(const pop::PopDef* def);
   ~PopNode() override;
   // The PointZ whose 'tag' param is `tag` (Null PointZ, Cache PointZ): Particle and
   // Feedback loops and Cache Select find their target this way, like
   // TouchDesigner's target-PointZ parameter. Cooks it for `frameId` first.
   static PopNode* FindTagged(const std::string& tag, const PopNode* exclude, int frameId);
   const std::string& Tag() const;
   pop::OpState* State() { return mState.get(); }
   static INode* CreateFor(const std::string& defName);

   const pop::PopDef& Def() const { return *mDef; }
   pop::ParamValues& Values() { return mValues; }
   // Restart a stateful operator (Particle, Feedback, Trail, Cache).
   void RequestReset() { mResetRequested = true; }
   const std::string& CookError() const { return mError; }
   int PointCount() const { return mOut ? mOut->pointCount : 0; }
   size_t TriangleCount() const { return mOut ? mOut->Topo().TriangleCount() : 0; }
   size_t StripCount() const { return mOut ? mOut->Topo().StripCount() : 0; }
   double LastCookMs() const { return mLastCookMs; }

   // INode
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   INode* BypassSource() override { return dynamic_cast<INode*>(inputs[0]); }
   const char* InputLabel(int slot) const override;
   void VisitParams(ParamVisitor& v) override;
   IGeometrySource** GeometryInputSlot(int slot) override;
   ImageCable* ImageInputSlot(int slot) override;
   int InputPinCount() const override;
   int ImageCount() const;

   // IGeometrySource
   const pop::PopData* GetPop() override;
   const pop::PopData* GetPopInstances() override { return bypassed ? nullptr : mInstances.get(); }
   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }
   const std::vector<Particle>* GetPointCloud() override;
   unsigned long long PointCloudRevision() override;
   const Polyline* GetCurve() override;
   unsigned long long CurveStamp() override;
   const std::string& CookWarning() const override { return mWarning; }

   IGeometrySource* inputs[kMaxInputs] = { nullptr, nullptr, nullptr, nullptr };

private:
   bool IsCloudOut() const;
   bool IsCurveOut() const;
   pop::DataPtr InputData(int slot, int frameId);
   const pop::PopData* ActiveOut();

   const pop::PopDef* mDef = nullptr;
   pop::ParamValues mValues;
   ImageCable mImages[4];
   std::unique_ptr<pop::OpState> mState;
   bool mResetRequested = false;

   std::shared_ptr<const pop::PopData> mOut;
   std::shared_ptr<const pop::PopData> mInstances;
   unsigned long long mOutRevision = 0;
   int mLastCookFrame = -1;
   std::vector<float> mLastSigF;
   std::vector<std::string> mLastSigS;
   std::vector<unsigned long long> mLastSigInputs;
   unsigned long long mLastImageRev = 0;
   std::string mWarning, mError;
   double mLastCookMs = 0.0;
   double mLastTime = -1.0;

   // Non-PointZ inputs converted once per upstream revision.
   struct Converted
   {
      const void* source = nullptr;
      unsigned long long rev[3] = { 0, 0, 0 };
      pop::DataPtr data;
   };
   Converted mConverted[kMaxInputs];

   // Lazy views of the output for non-PointZ consumers.
   Mesh mMeshView;
   unsigned long long mMeshViewRev = ~0ull;
   std::vector<Particle> mCloudView;
   unsigned long long mCloudViewRev = ~0ull;
   Polyline mCurveView;
   unsigned long long mCurveViewRev = ~0ull;
};

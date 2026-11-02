#include "SculptNode.h"

#include <algorithm>

namespace
{
   const Mesh kEmpty;
}

void SculptNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   Update();
}

void SculptNode::Update()
{
   if (mData != mDecodedFrom)
   {
      // Loaded or undone: the text wins.
      int points = 0;
      std::vector<float> offsets;
      if (Sculpt::Decode(mData, offsets, points))
      {
         mOffsets.swap(offsets);
         mPoints = points;
      }
      mDecodedFrom = mData;
      mOffsetsRevision++;
   }
   if (input == nullptr)
   {
      mCookWarning.clear();
      mMesh = Mesh();
      mKeyInput = nullptr;
      return;
   }
   const int lv = std::clamp(level, 0, 3);
   const unsigned long long rev = input->MeshRevision();
   if (input != mKeyInput || rev != mKeyRevision || lv != mKeyLevel)
   {
      mKeyInput = input;
      mKeyRevision = rev;
      mKeyLevel = lv;
      mBase = lv > 0 ? MeshOps::Subdivide(input->GetMesh(), lv, 1.0f) : input->GetMesh();
      Sculpt::Build(mBase, mTopo);
      mBaseRevision++;
   }
   if (mPoints == 0 && mTopo.points > 0)
   {
      mPoints = mTopo.points;
      mOffsets.assign((size_t)mPoints * 3, 0.0f);
   }
   const bool fits = mPoints == mTopo.points && mOffsets.size() == (size_t)mPoints * 3;
   mCookWarning = fits || mBase.Empty() ? std::string() : std::string("Topology changed: sculpt not applied");
   const unsigned long long shapeOf = mBaseRevision * 1000003ull + mOffsetsRevision * 7919ull + (fits ? 1 : 0);
   const float a = std::clamp(amount, 0.0f, 2.0f);
   if (shapeOf != mShapeOf || a != mShapeAmount)
   {
      mShapeOf = shapeOf;
      mShapeAmount = a;
      if (fits)
         Sculpt::Apply(mBase, mTopo, mOffsets, a, mMesh);
      else
         mMesh = mBase;
      mMeshRevision = NextMeshRevision();
   }
}

const Mesh& SculptNode::GetMesh()
{
   if (input == nullptr)
      return kEmpty;
   Update();
   return mMesh;
}

unsigned long long SculptNode::MeshRevision()
{
   if (input == nullptr)
      return 0;
   Update();
   return mMeshRevision;
}

bool SculptNode::Sculptable()
{
   Update();
   return input != nullptr && mTopo.points > 0 && mPoints == mTopo.points && mOffsets.size() == (size_t)mPoints * 3;
}

void SculptNode::OffsetsChanged()
{
   mOffsetsRevision++;
   mEncoded = false;
}

bool SculptNode::HasSculpt() const
{
   return std::any_of(mOffsets.begin(), mOffsets.end(), [](float v) { return v != 0.0f; });
}

void SculptNode::Clear()
{
   mPoints = mTopo.points;
   mOffsets.assign((size_t)mPoints * 3, 0.0f);
   OffsetsChanged();
}

#include "SculptNode.h"

#include <algorithm>
#include <cmath>

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
      const size_t oldVerts = mBase.vertices.size();
      std::vector<unsigned int> oldIndices = std::move(mBase.indices);
      mBase = lv > 0 ? MeshOps::Subdivide(input->GetMesh(), lv, 1.0f) : input->GetMesh();
      // Only the points moved (a pose, a clip): the topology still holds.
      if (mTopo.points == 0 || mBase.vertices.size() != oldVerts || mBase.indices != oldIndices)
         Sculpt::Build(mBase, mTopo);
      mMaxEdge = 0.0f;
      const size_t nv = mBase.vertices.size();
      for (size_t f = 0; f * 3 + 2 < mBase.indices.size(); f++)
         for (int c = 0; c < 3; c++)
         {
            const unsigned int i = mBase.indices[f * 3 + c], j = mBase.indices[f * 3 + (c + 1) % 3];
            if (i >= nv || j >= nv)
               continue;
            const Vertex &a = mBase.vertices[i], &b = mBase.vertices[j];
            mMaxEdge = std::max(mMaxEdge, std::sqrt((a.px - b.px) * (a.px - b.px) + (a.py - b.py) * (a.py - b.py) +
                                                    (a.pz - b.pz) * (a.pz - b.pz)));
         }
      mBaseRevision++;
   }
   if (mPoints == 0 && mTopo.points > 0)
   {
      mPoints = mTopo.points;
      mOffsets.assign((size_t)mPoints * 3, 0.0f);
   }
   const bool fits = mPoints == mTopo.points && mOffsets.size() == (size_t)mPoints * 3;
   mCookWarning = fits || mBase.Empty() ? std::string() : std::string("Topology changed: sculpt not applied");
   const float a = std::clamp(amount, 0.0f, 2.0f);
   if (mShapeBase != mBaseRevision || mShapeOffsets != mOffsetsRevision || fits != mFits || a != mShapeAmount)
   {
      mShapeBase = mBaseRevision;
      mShapeOffsets = mOffsetsRevision;
      mFits = fits;
      mShapeAmount = a;
      mReshaped = fits && a != 0.0f && HasSculpt();
      if (fits)
         Sculpt::Apply(mBase, mTopo, mOffsets, a, mMesh);
      else
         mMesh = mBase;
      mMeshRevision = NextMeshRevision();
      mFrameStale = true;
      mGrid = Sculpt::Grid();
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

const std::vector<float>& SculptNode::PointPos()
{
   Update();
   if (mFrameStale)
   {
      Sculpt::PointFrame(mMesh, mTopo, mPos, mNrm);
      mFrameStale = false;
   }
   return mPos;
}

const std::vector<float>& SculptNode::PointNrm()
{
   PointPos();
   return mNrm;
}

const Sculpt::Grid* SculptNode::PointGrid(float radius)
{
   const std::vector<float>& pos = PointPos();
   if (radius <= 0.0f && mGrid.cell <= 0.0f)
   {
      // No brush yet (the hover ray): guess a tenth of the model, a usual
      // brush, so the first dab does not rebuild.
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      for (size_t i = 0; i < pos.size(); i++)
      {
         lo[i % 3] = std::min(lo[i % 3], pos[i]);
         hi[i % 3] = std::max(hi[i % 3], pos[i]);
      }
      if (!pos.empty())
         radius = 0.1f * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                   (hi[2] - lo[2]) * (hi[2] - lo[2]));
   }
   // At least two edges wide, so Raycast can walk it; a brush spans about a
   // dozen cells, so it looks at a few thousand at most.
   const float cell = std::max(radius / 6.0f, mMaxEdge * 2.0f);
   if (mGrid.cell <= 0.0f || mGrid.cell < cell * 0.5f || mGrid.cell > cell * 2.0f ||
       mGrid.at.size() != pos.size())
      Sculpt::BuildGrid(pos, cell, mGrid);
   return &mGrid;
}

bool SculptNode::Raycast(const float o[3], const float d[3], float& t)
{
   Update();
   if (mTopo.point.size() != mMesh.vertices.size())
      return Sculpt::Raycast(mMesh, o, d, t);
   // The walk needs faces no wider than the grid allows; a stretched sculpt
   // can break that, so a miss falls back to testing every face.
   const float radius = mGrid.cell > 0.0f ? mGrid.cell * 6.0f : 0.0f;
   return Sculpt::Raycast(mMesh, o, d, t, &mTopo, PointGrid(radius)) || Sculpt::Raycast(mMesh, o, d, t);
}

void SculptNode::OffsetsChanged(const std::vector<unsigned int>* touched)
{
   mEncoded = false;
   Update();   // in step with everything but this change
   const bool inStep = touched != nullptr && mFits && mReshaped && mShapeBase == mBaseRevision &&
                       mShapeOffsets == mOffsetsRevision && mShapeAmount == std::clamp(amount, 0.0f, 2.0f);
   mOffsetsRevision++;
   if (touched != nullptr && !inStep && !mFrameStale && mGrid.cell > 0.0f && mShapeBase == mBaseRevision)
   {
      // The stroke's first dab re-shapes in full (every normal is re-made),
      // but only the touched points moved: the grid stays, moved.
      Sculpt::Grid grid = std::move(mGrid);
      Update();
      PointPos();
      mGrid = std::move(grid);
      Sculpt::MoveInGrid(mPos, *touched, mGrid);
      return;
   }
   if (!inStep || !Sculpt::ApplyPoints(mBase, mTopo, mOffsets, mShapeAmount, *touched, mMesh, mChanged))
      return;
   mShapeOffsets = mOffsetsRevision;
   mMeshRevision = NextMeshRevision();
   if (mFrameStale)
      return;
   Sculpt::PointFrameAt(mMesh, mTopo, mChanged, mPos, mNrm);
   // mChanged holds every moved point (and the neighbours whose normals turned).
   Sculpt::MoveInGrid(mPos, mChanged, mGrid);
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

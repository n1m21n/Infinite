#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "Modulation.h"

// Samples N points off whatever geometry is patched in and publishes them as
// an ordinary bank of XYZ modulator outputs - one pin per (row, axis), plus
// four aggregates (centroid + spread) that stay meaningful even when the row
// count changes or the source's point count is unstable frame to frame.
//
// Unlike PathNode (one animated point travelling a contour), this is N
// static points forming a table: every row patches into a different
// parameter at once, so a single mesh can drive a whole bank of sliders.
class GeometryTableNode : public INode, public IModulator
{
public:
   enum SampleMode { kVertex = 0, kScatter, kContour, kSampleModeCount };
   enum SortMode { kSortNone = 0, kSortX, kSortY, kSortZ, kSortDistance, kSortAngle, kSortModeCount };
   enum Space { kSpaceFixed = 0, kSpaceBounds, kSpaceCount };

   static INode* Create() { return new GeometryTableNode(); }
   static const std::vector<std::string>& SampleModeNames();
   static const std::vector<std::string>& SortModeNames();
   static const std::vector<std::string>& SpaceNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   IGeometrySource* geometrySource = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      return slot == 0 ? &geometrySource : nullptr;
   }
   const char* InputLabel(int) const override { return "geo"; }

   // Aggregates first (cx, cy, cz, spread), then rows*3. See the layout note in
   // 09-geometry-table-node.md §2 - this ordering is a saved-patch-file
   // stability requirement, not an aesthetic choice: growing `rows` must only
   // ever append new pins, never renumber existing ones.
   int OutputCount() const override { return 4 + 3 * RowCount(); }
   const char* OutputLabel(int index) const override;

   float Value01() override; // aggregate 0 (cx)
   IModulator* ModulatorOutput(int index) override;

   int RowCount() const { return std::max(1, std::min(16, rows)); }
   bool HasSamples() const { return mSampleCount > 0; }
   int SampleCount() const { return mSampleCount; }

   // Pre-normalisation world-space position of row `row`, for the transform
   // sweep test - the analogue of PathNode::CurrentPoint.
   void SampleRow(int row, float outWorld[3]) const;

   int sampleMode = kVertex;
   int rows = 4;
   int sortMode = kSortNone;
   float offset = 0.0f;
   int space = kSpaceFixed;
   float extent = 1.0f;
   float smooth = 0.25f;
   int sliceAxis = 1;
   float slicePosition = 0.0f;
   float seed = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("sampleMode", sampleMode); v.Int("rows", rows); v.Int("sortMode", sortMode);
      v.Float("offset", offset); v.Int("space", space); v.Float("extent", extent);
      v.Float("smooth", smooth); v.Int("sliceAxis", sliceAxis);
      v.Float("slicePosition", slicePosition); v.Float("seed", seed);
   }

private:
   // Sibling outputs: each just reads the slot its parent already computed, so
   // the table is evaluated once per frame however many outputs are patched.
   class Tap : public IModulator
   {
   public:
      GeometryTableNode* owner = nullptr;
      int index = 0; // 0..3 aggregates, 4.. row*3+axis
      float Value01() override;
   };

   void Rebuild();
   void Evaluate();
   float RawRow(int row, int axis) const;
   float Normalize(float p, float sharedDivisor) const;

   // The dense sample cache built at rebuild time (world space, pre-offset,
   // pre-normalisation), and the offset/sort applied per frame without
   // re-sampling. See §4.2 - offset scrubs a read head across this cache.
   std::vector<float> mSamplePoints; // xyz triples, mSampleCount entries
   int mSampleCount = 0;
   float mCentroid[3] = { 0, 0, 0 };
   float mSpread = 0.0f;
   float mBoundsHalfSpan = 1.0f; // largest half-extent of the sampled set, for kSpaceBounds

   float mSmoothed[52] = { 0.0f }; // one per possible output (4 + 3*16)
   bool mSmoothedInit = false;

   const void* mBuiltSource = nullptr;
   unsigned long long mBuiltRevision = 0;
   int mBuiltSampleMode = -1, mBuiltRows = -1, mBuiltSortMode = -1, mBuiltSliceAxis = -1;
   float mBuiltSlicePosition = -999.0f, mBuiltSeed = -999.0f;
   Mat4 mBuiltModel;

   int mLastCookFrame = -1;
   Tap mTaps[52];
   bool mOutputsBound = false;
};

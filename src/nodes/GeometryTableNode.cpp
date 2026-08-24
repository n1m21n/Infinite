#include "GeometryTableNode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
   const std::vector<std::string> kSampleModeNames = { "Vertex", "Scatter", "Contour" };
   const std::vector<std::string> kSortModeNames = { "None", "X", "Y", "Z", "Distance", "Angle" };
   const std::vector<std::string> kSpaceNames = { "Fixed", "Bounds" };

   void TransformPoint(const Mat4& m, float x, float y, float z, float out[3])
   {
      out[0] = m.m[0] * x + m.m[4] * y + m.m[8]  * z + m.m[12];
      out[1] = m.m[1] * x + m.m[5] * y + m.m[9]  * z + m.m[13];
      out[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
   }
}

const std::vector<std::string>& GeometryTableNode::SampleModeNames() { return kSampleModeNames; }
const std::vector<std::string>& GeometryTableNode::SortModeNames() { return kSortModeNames; }
const std::vector<std::string>& GeometryTableNode::SpaceNames() { return kSpaceNames; }

void GeometryTableNode::Rebuild()
{
   const int rowCount = RowCount();

   if (geometrySource == nullptr)
   {
      if (mSampleCount != 0)
      {
         mSamplePoints.clear();
         mSampleCount = 0;
         mCentroid[0] = mCentroid[1] = mCentroid[2] = 0.0f;
         mSpread = 0.0f;
         mBoundsHalfSpan = 1.0f;
      }
      mBuiltSource = nullptr;
      return;
   }

   // Sample more points than `rows` so `offset` scrubs a read head across a
   // dense sampling of the geometry rather than cyclically permuting the same
   // `rows` values - see §4.2.
   const int sampleTarget = std::max(rowCount, std::min(512, rowCount * 8));
   const Mat4 model = geometrySource->GetModelMatrix();

   // Revision tracks whichever component §3.1 will actually resolve to, so a
   // point-cloud-only change doesn't get missed because only MeshRevision()
   // was compared.
   unsigned long long revision = geometrySource->MeshRevision();
   const std::vector<Particle>* cloudForRevision =
      (sampleMode != kContour) ? geometrySource->GetPointCloud() : nullptr;
   if (cloudForRevision != nullptr && !cloudForRevision->empty())
      revision = geometrySource->PointCloudRevision();
   else if (sampleMode == kContour && geometrySource->GetCurve() != nullptr)
      revision = geometrySource->CurveStamp();

   if (mBuiltSource == (const void*)geometrySource && mBuiltRevision == revision &&
       mBuiltSampleMode == sampleMode && mBuiltRows == rowCount && mBuiltSortMode == sortMode &&
       mBuiltSliceAxis == sliceAxis && mBuiltSlicePosition == slicePosition &&
       mBuiltSeed == seed && mBuiltModel == model)
      return;

   std::vector<float> points; // xyz triples, world space

   if (sampleMode == kVertex || sampleMode == kScatter)
   {
      // Cloud wins over mesh: every point-cloud publisher except
      // ParticleSystemNode also publishes a billboard-quad mesh built by
      // MeshOps::PointsToFaces, which is quad corners, not point centres -
      // see §3.1. ParticleSystemNode's own GetMesh() is a static empty mesh,
      // so a mesh-first read would silently see nothing there either way.
      const std::vector<Particle>* cloud = geometrySource->GetPointCloud();
      if (cloud != nullptr && !cloud->empty())
      {
         points.reserve(cloud->size() * 3);
         for (const Particle& p : *cloud)
         {
            if (!p.alive)
               continue;
            float w[3];
            TransformPoint(model, p.px, p.py, p.pz, w);
            points.push_back(w[0]);
            points.push_back(w[1]);
            points.push_back(w[2]);
         }
      }
      else
      {
         const Mesh mesh = MeshOps::Transform(geometrySource->GetMesh(), model);
         if (sampleMode == kVertex)
         {
            // ToPoints welds by default, which is what's wanted here: a Cube
            // emits 24 vertices for 8 corners, and sampling the raw index
            // space would over-count every seam.
            std::vector<MeshPoint> mp = MeshOps::ToPoints(mesh, 0, sampleTarget);
            points.reserve(mp.size() * 3);
            for (const MeshPoint& p : mp)
            {
               points.push_back(p.px);
               points.push_back(p.py);
               points.push_back(p.pz);
            }
         }
         else // kScatter
         {
            // No raw density knob is exposed (§4.3): derive one from the
            // sample target and the mesh's own surface area, then retry at
            // higher density if the (not-perfectly-uniform) rejection
            // sampling under-filled the target.
            float area = 0.0f;
            for (size_t f = 0; f < mesh.FaceCount(); f++)
            {
               if (!mesh.FaceSelected(f))
                  continue;
               const unsigned int i0 = mesh.indices[f * 3 + 0];
               const unsigned int i1 = mesh.indices[f * 3 + 1];
               const unsigned int i2 = mesh.indices[f * 3 + 2];
               const Vertex& a = mesh.vertices[i0];
               const Vertex& b = mesh.vertices[i1];
               const Vertex& c = mesh.vertices[i2];
               const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
               const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
               const float cx = uy * vz - uz * vy;
               const float cy = uz * vx - ux * vz;
               const float cz = ux * vy - uy * vx;
               area += 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
            }
            float density = (area > 1e-6f) ? (float)sampleTarget / area : (float)sampleTarget;
            std::vector<MeshPoint> mp;
            for (int attempt = 0; attempt < 4 && (int)mp.size() < sampleTarget; attempt++)
            {
               mp = MeshOps::DistributeOnFaces(mesh, density, seed, MeshOps::kDistributeRandom, 0.0f);
               density *= 2.0f;
            }
            const int n = std::min((int)mp.size(), sampleTarget);
            points.reserve((size_t)n * 3);
            for (int i = 0; i < n; i++)
            {
               points.push_back(mp[i].px);
               points.push_back(mp[i].py);
               points.push_back(mp[i].pz);
            }
         }
      }
   }
   else // kContour
   {
      Polyline line;
      const Polyline* curve = geometrySource->GetCurve();
      if (curve != nullptr)
      {
         line = *curve;
         for (size_t i = 0; i < line.Count(); i++)
         {
            float w[3];
            TransformPoint(model, line.points[i * 3 + 0], line.points[i * 3 + 1],
                           line.points[i * 3 + 2], w);
            line.points[i * 3 + 0] = w[0];
            line.points[i * 3 + 1] = w[1];
            line.points[i * 3 + 2] = w[2];
         }
      }
      else
      {
         const Mesh mesh = MeshOps::Transform(geometrySource->GetMesh(), model);
         // A closed mesh has no boundary at all, so fall back to a slice -
         // same reduction and same fallback order as PathNode's follow mode.
         std::vector<Polyline> loops = MeshOps::BoundaryLoops(mesh);
         if (loops.empty())
            loops = MeshOps::SliceContours(mesh, sliceAxis, slicePosition);
         if (!loops.empty())
            line = loops[0];
      }

      if (!line.Empty())
      {
         const int n = std::max(rowCount, sampleTarget);
         points.reserve((size_t)n * 3);
         for (int i = 0; i < n; i++)
         {
            const float t = (n > 1) ? (float)i / (float)n : 0.0f;
            float pos[3], tangent[3];
            MeshOps::SamplePolyline(line, t, pos, tangent);
            points.push_back(pos[0]);
            points.push_back(pos[1]);
            points.push_back(pos[2]);
         }
      }
   }

   // Sorting happens once, here, not per frame - see §4.1. `None` preserves
   // native index order, which for a contour is already the walk order along
   // it and is usually what's wanted there.
   const int n = (int)(points.size() / 3);
   if (n > 1 && sortMode != kSortNone)
   {
      float cx = 0, cy = 0, cz = 0;
      for (int i = 0; i < n; i++)
      {
         cx += points[i * 3 + 0];
         cy += points[i * 3 + 1];
         cz += points[i * 3 + 2];
      }
      cx /= (float)n; cy /= (float)n; cz /= (float)n;

      std::vector<int> order(n);
      for (int i = 0; i < n; i++)
         order[i] = i;
      const int mode = sortMode;
      auto key = [&](int i) -> float {
         const float px = points[i * 3 + 0], py = points[i * 3 + 1], pz = points[i * 3 + 2];
         switch (mode)
         {
            case kSortX: return px;
            case kSortY: return py;
            case kSortZ: return pz;
            case kSortDistance:
            {
               const float dx = px - cx, dy = py - cy, dz = pz - cz;
               return std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            case kSortAngle: return std::atan2(pz - cz, px - cx); // around the centroid in XZ
            default: return 0.0f;
         }
      };
      std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return key(a) < key(b); });

      std::vector<float> sorted(points.size());
      for (int i = 0; i < n; i++)
      {
         sorted[i * 3 + 0] = points[order[i] * 3 + 0];
         sorted[i * 3 + 1] = points[order[i] * 3 + 1];
         sorted[i * 3 + 2] = points[order[i] * 3 + 2];
      }
      points.swap(sorted);
   }

   mSamplePoints = points;
   mSampleCount = n;

   // Aggregates over the full sampled set, not just the exposed rows - this
   // is what keeps cx/cy/cz/spread meaningful when `rows` shrinks or the
   // source's point count is unstable (particles being born/dying).
   if (n > 0)
   {
      float cx = 0, cy = 0, cz = 0;
      for (int i = 0; i < n; i++)
      {
         cx += points[i * 3 + 0];
         cy += points[i * 3 + 1];
         cz += points[i * 3 + 2];
      }
      cx /= (float)n; cy /= (float)n; cz /= (float)n;
      mCentroid[0] = cx; mCentroid[1] = cy; mCentroid[2] = cz;

      float sumSq = 0.0f;
      float mn[3] = { cx, cy, cz }, mx[3] = { cx, cy, cz };
      for (int i = 0; i < n; i++)
      {
         const float dx = points[i * 3 + 0] - cx, dy = points[i * 3 + 1] - cy, dz = points[i * 3 + 2] - cz;
         sumSq += dx * dx + dy * dy + dz * dz;
         for (int a = 0; a < 3; a++)
         {
            const float v = points[i * 3 + a];
            mn[a] = std::min(mn[a], v);
            mx[a] = std::max(mx[a], v);
         }
      }
      mSpread = std::sqrt(sumSq / (float)n);

      float maxHalf = 0.0f;
      for (int a = 0; a < 3; a++)
         maxHalf = std::max(maxHalf, (mx[a] - mn[a]) * 0.5f);
      mBoundsHalfSpan = std::max(0.001f, maxHalf);
   }
   else
   {
      mCentroid[0] = mCentroid[1] = mCentroid[2] = 0.0f;
      mSpread = 0.0f;
      mBoundsHalfSpan = 1.0f;
   }

   mBuiltSource = geometrySource;
   mBuiltRevision = revision;
   mBuiltSampleMode = sampleMode;
   mBuiltRows = rowCount;
   mBuiltSortMode = sortMode;
   mBuiltSliceAxis = sliceAxis;
   mBuiltSlicePosition = slicePosition;
   mBuiltSeed = seed;
   mBuiltModel = model;
}

void GeometryTableNode::SampleRow(int row, float outWorld[3]) const
{
   outWorld[0] = outWorld[1] = outWorld[2] = 0.0f;
   if (mSampleCount <= 0)
      return;
   // `offset` rotates which sample lands in row 0, read from the cached array
   // - never by re-sampling. See §4.2.
   const int baseIndex = (int)std::floor(offset * (float)mSampleCount);
   const int idx = ((baseIndex + row) % mSampleCount + mSampleCount) % mSampleCount;
   outWorld[0] = mSamplePoints[idx * 3 + 0];
   outWorld[1] = mSamplePoints[idx * 3 + 1];
   outWorld[2] = mSamplePoints[idx * 3 + 2];
}

float GeometryTableNode::RawRow(int row, int axis) const
{
   float w[3];
   SampleRow(row, w);
   return w[axis];
}

float GeometryTableNode::Normalize(float p, float sharedDivisor) const
{
   // PathNode::AxisOutput::Value01()'s formula, verbatim - one shared divisor
   // across all three axes, not a per-axis one, so a flat object isn't
   // stretched back out to full range on every axis.
   return std::max(0.0f, std::min(1.0f, p / (2.0f * sharedDivisor) + 0.5f));
}

void GeometryTableNode::Evaluate()
{
   const int rowCount = RowCount();
   const int total = 4 + 3 * rowCount;
   float raw[52];

   if (mSampleCount <= 0)
   {
      // Unplugged (or a source yielding no points): hold every output at the
      // neutral midpoint rather than 0, so this doesn't slam every
      // destination it drives to the bottom of its range.
      for (int i = 0; i < total; i++)
         raw[i] = 0.5f;
   }
   else
   {
      // Fixed uses `extent`; Bounds self-scales off the sampled set's own
      // bounding box. Both use one shared divisor across axes (§4.3).
      const float sharedDivisor = std::max(0.001f, (space == kSpaceBounds) ? mBoundsHalfSpan : extent);
      raw[0] = Normalize(mCentroid[0], sharedDivisor);
      raw[1] = Normalize(mCentroid[1], sharedDivisor);
      raw[2] = Normalize(mCentroid[2], sharedDivisor);
      raw[3] = std::max(0.0f, std::min(1.0f, mSpread / sharedDivisor));
      for (int r = 0; r < rowCount; r++)
         for (int a = 0; a < 3; a++)
            raw[4 + r * 3 + a] = Normalize(RawRow(r, a), sharedDivisor);
   }

   // One-pole per output, stepped once per cook - not once per Value01() call,
   // since several destinations can read the same output in one frame and it
   // must not decay faster the more things are patched to it (§4.4).
   if (!mSmoothedInit)
   {
      for (int i = 0; i < total; i++)
         mSmoothed[i] = raw[i];
      mSmoothedInit = true;
   }
   const float k = std::max(0.0f, std::min(0.999f, smooth));
   for (int i = 0; i < total; i++)
      mSmoothed[i] += (raw[i] - mSmoothed[i]) * (1.0f - k);
}

void GeometryTableNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // ModulatorOutput() can be called by the link-rebuild pass before the node
   // has cooked a single frame, so the sibling outputs are bound lazily in
   // both places - see PathNode.
   if (!mOutputsBound)
   {
      for (int i = 0; i < 52; i++)
      {
         mTaps[i].owner = this;
         mTaps[i].index = i;
      }
      mOutputsBound = true;
   }

   Rebuild();
   Evaluate();
}

const char* GeometryTableNode::OutputLabel(int index) const
{
   static thread_local char buf[16];
   if (index == 0) return "cx";
   if (index == 1) return "cy";
   if (index == 2) return "cz";
   if (index == 3) return "spread";
   const int r = (index - 4) / 3;
   const int a = (index - 4) % 3;
   const char axis = (a == 0) ? 'x' : (a == 1) ? 'y' : 'z';
   snprintf(buf, sizeof(buf), "%c%d", axis, r + 1);
   return buf;
}

float GeometryTableNode::Value01()
{
   // Modulators can be read before the node has cooked this frame, so fall
   // back to the neutral midpoint rather than a stale/garbage value.
   return mSmoothedInit ? mSmoothed[0] : 0.5f;
}

IModulator* GeometryTableNode::ModulatorOutput(int index)
{
   if (!mOutputsBound)
   {
      for (int i = 0; i < 52; i++)
      {
         mTaps[i].owner = this;
         mTaps[i].index = i;
      }
      mOutputsBound = true;
   }
   const int total = 4 + 3 * RowCount();
   if (index < 0 || index >= total)
      return nullptr;
   return &mTaps[index];
}

float GeometryTableNode::Tap::Value01()
{
   if (owner == nullptr)
      return 0.5f;
   const int total = 4 + 3 * owner->RowCount();
   if (index < 0 || index >= total)
      return 0.5f;
   return owner->mSmoothedInit ? owner->mSmoothed[index] : 0.5f;
}

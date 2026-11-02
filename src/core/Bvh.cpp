#include "Bvh.h"

#include <algorithm>
#include <cmath>

namespace Bvh
{
namespace
{
   struct Bounds3
   {
      float lo[3] = { 1e30f, 1e30f, 1e30f };
      float hi[3] = { -1e30f, -1e30f, -1e30f };

      void Expand(const float p[3])
      {
         for (int k = 0; k < 3; k++)
         {
            if (p[k] < lo[k]) lo[k] = p[k];
            if (p[k] > hi[k]) hi[k] = p[k];
         }
      }

      void Expand(const Bounds3& b)
      {
         for (int k = 0; k < 3; k++)
         {
            if (b.lo[k] < lo[k]) lo[k] = b.lo[k];
            if (b.hi[k] > hi[k]) hi[k] = b.hi[k];
         }
      }

      float Area() const
      {
         const float dx = std::max(0.0f, hi[0] - lo[0]);
         const float dy = std::max(0.0f, hi[1] - lo[1]);
         const float dz = std::max(0.0f, hi[2] - lo[2]);
         return 2.0f * (dx * dy + dy * dz + dz * dx);
      }
   };

   struct PrimBuildInfo
   {
      Bounds3 bounds;
      float centroid[3] = { 0.0f, 0.0f, 0.0f };
      int index = 0;
   };

   inline bool IntersectAabb(const float minB[3], const float maxB[3],
                             const float o[3], const float invD[3],
                             float tMin, float tMax, float& outNear)
   {
      const float eps = 1e-5f;
      float t0 = tMin;
      float t1 = tMax;
      for (int a = 0; a < 3; a++)
      {
         const float b0 = (minB[a] - eps - o[a]) * invD[a];
         const float b1 = (maxB[a] + eps - o[a]) * invD[a];
         const float nearT = b0 < b1 ? b0 : b1;
         const float farT = b0 > b1 ? b0 : b1;
         if (nearT > t0) t0 = nearT;
         if (farT < t1) t1 = farT;
         if (t0 > t1)
            return false;
      }
      outNear = t0;
      return true;
   }

   int BuildSahRecursive(std::vector<PrimBuildInfo>& prims, int start, int end,
                         int maxLeafPrims, float costTrav, float costIsect,
                         std::vector<float>& nodeTexels)
   {
      const int nodeIdx = (int)(nodeTexels.size() / 8);
      nodeTexels.resize(nodeTexels.size() + 8, 0.0f);

      Bounds3 nodeBounds;
      Bounds3 centBounds;
      for (int i = start; i < end; i++)
      {
         nodeBounds.Expand(prims[i].bounds);
         centBounds.Expand(prims[i].centroid);
      }

      const int count = end - start;
      int bestAxis = -1;
      int bestBin = -1;
      float bestCost = (float)count * costIsect;

      if (count > maxLeafPrims)
      {
         const float parentArea = std::max(nodeBounds.Area(), 1e-20f);
         for (int axis = 0; axis < 3; axis++)
         {
            const float extent = centBounds.hi[axis] - centBounds.lo[axis];
            if (extent <= 1e-7f)
               continue;

            Bounds3 binBounds[Blas::kBins];
            int binCount[Blas::kBins] = {};
            const float scale = (float)Blas::kBins / extent;
            const float base = centBounds.lo[axis];

            for (int i = start; i < end; i++)
            {
               int b = (int)((prims[i].centroid[axis] - base) * scale);
               if (b < 0) b = 0;
               if (b >= Blas::kBins) b = Blas::kBins - 1;
               binCount[b]++;
               binBounds[b].Expand(prims[i].bounds);
            }

            Bounds3 leftB[Blas::kBins - 1];
            Bounds3 rightB[Blas::kBins - 1];
            int leftN[Blas::kBins - 1] = {};
            int rightN[Blas::kBins - 1] = {};

            Bounds3 curL;
            int sumL = 0;
            for (int b = 0; b < Blas::kBins - 1; b++)
            {
               sumL += binCount[b];
               if (binCount[b] > 0)
                  curL.Expand(binBounds[b]);
               leftN[b] = sumL;
               leftB[b] = curL;
            }

            Bounds3 curR;
            int sumR = 0;
            for (int b = Blas::kBins - 1; b > 0; b--)
            {
               sumR += binCount[b];
               if (binCount[b] > 0)
                  curR.Expand(binBounds[b]);
               rightN[b - 1] = sumR;
               rightB[b - 1] = curR;
            }

            for (int b = 0; b < Blas::kBins - 1; b++)
            {
               if (leftN[b] == 0 || rightN[b] == 0)
                  continue;
               const float cost = costTrav +
                  (leftB[b].Area() * (float)leftN[b] + rightB[b].Area() * (float)rightN[b]) * (costIsect / parentArea);
               if (cost < bestCost || bestAxis < 0)
               {
                  bestCost = cost;
                  bestAxis = axis;
                  bestBin = b;
               }
            }
         }
      }

      if (count <= maxLeafPrims)
      {
         float* dst = &nodeTexels[(size_t)nodeIdx * 8];
         dst[0] = nodeBounds.lo[0];
         dst[1] = nodeBounds.lo[1];
         dst[2] = nodeBounds.lo[2];
         dst[3] = IntAsFloat(start);
         dst[4] = nodeBounds.hi[0];
         dst[5] = nodeBounds.hi[1];
         dst[6] = nodeBounds.hi[2];
         dst[7] = IntAsFloat(count);
         return nodeIdx;
      }

      int mid = (start + end) / 2;
      if (bestAxis >= 0)
      {
         const float extent = centBounds.hi[bestAxis] - centBounds.lo[bestAxis];
         const float scale = (float)Blas::kBins / extent;
         const float base = centBounds.lo[bestAxis];
         auto it = std::partition(prims.begin() + start, prims.begin() + end,
            [&](const PrimBuildInfo& p)
            {
               int b = (int)((p.centroid[bestAxis] - base) * scale);
               if (b < 0) b = 0;
               if (b >= Blas::kBins) b = Blas::kBins - 1;
               return b <= bestBin;
            });
         mid = (int)(it - prims.begin());
         if (mid == start || mid == end)
            mid = (start + end) / 2;
      }

      const int leftChild = BuildSahRecursive(prims, start, mid, maxLeafPrims, costTrav, costIsect, nodeTexels);
      const int rightChild = BuildSahRecursive(prims, mid, end, maxLeafPrims, costTrav, costIsect, nodeTexels);

      float* dst = &nodeTexels[(size_t)nodeIdx * 8];
      dst[0] = nodeBounds.lo[0];
      dst[1] = nodeBounds.lo[1];
      dst[2] = nodeBounds.lo[2];
      dst[3] = IntAsFloat(leftChild);
      dst[4] = nodeBounds.hi[0];
      dst[5] = nodeBounds.hi[1];
      dst[6] = nodeBounds.hi[2];
      dst[7] = IntAsFloat(-rightChild);
      return nodeIdx;
   }
}

void Blas::Build(const Mesh& mesh, int materialId)
{
   nodeTexels.clear();
   triTexels.clear();

   const int numTris = (int)(mesh.indices.size() / 3);
   if (numTris <= 0 || mesh.vertices.empty())
      return;

   std::vector<PrimBuildInfo> prims((size_t)numTris);
   for (int t = 0; t < numTris; t++)
   {
      const Vertex& v0 = mesh.vertices[mesh.indices[(size_t)t * 3 + 0]];
      const Vertex& v1 = mesh.vertices[mesh.indices[(size_t)t * 3 + 1]];
      const Vertex& v2 = mesh.vertices[mesh.indices[(size_t)t * 3 + 2]];
      const float p0[3] = { v0.px, v0.py, v0.pz };
      const float p1[3] = { v1.px, v1.py, v1.pz };
      const float p2[3] = { v2.px, v2.py, v2.pz };

      PrimBuildInfo& info = prims[(size_t)t];
      info.index = t;
      info.bounds.Expand(p0);
      info.bounds.Expand(p1);
      info.bounds.Expand(p2);
      info.centroid[0] = (p0[0] + p1[0] + p2[0]) * (1.0f / 3.0f);
      info.centroid[1] = (p0[1] + p1[1] + p2[1]) * (1.0f / 3.0f);
      info.centroid[2] = (p0[2] + p1[2] + p2[2]) * (1.0f / 3.0f);
   }

   nodeTexels.reserve((size_t)numTris * 2 * 8);
   BuildSahRecursive(prims, 0, numTris, kMaxLeafPrims, kCostTrav, kCostIsect, nodeTexels);

   triTexels.resize((size_t)numTris * 12);
   for (int i = 0; i < numTris; i++)
   {
      const int t = prims[(size_t)i].index;
      const Vertex& v0 = mesh.vertices[mesh.indices[(size_t)t * 3 + 0]];
      const Vertex& v1 = mesh.vertices[mesh.indices[(size_t)t * 3 + 1]];
      const Vertex& v2 = mesh.vertices[mesh.indices[(size_t)t * 3 + 2]];
      float* dst = &triTexels[(size_t)i * 12];
      dst[0] = v0.px;
      dst[1] = v0.py;
      dst[2] = v0.pz;
      dst[3] = IntAsFloat(t);

      dst[4] = v1.px - v0.px;
      dst[5] = v1.py - v0.py;
      dst[6] = v1.pz - v0.pz;
      dst[7] = IntAsFloat(0);

      dst[8] = v2.px - v0.px;
      dst[9] = v2.py - v0.py;
      dst[10] = v2.pz - v0.pz;
      dst[11] = IntAsFloat(materialId);
   }
}

void Blas::Refit(const Mesh& deformedMesh)
{
   const int numTris = TriCount();
   if (numTris <= 0 || (int)(deformedMesh.indices.size() / 3) < numTris)
      return;

   for (int i = 0; i < numTris; i++)
   {
      float* dst = &triTexels[(size_t)i * 12];
      const int t = FloatAsInt(dst[3]);
      const Vertex& v0 = deformedMesh.vertices[deformedMesh.indices[(size_t)t * 3 + 0]];
      const Vertex& v1 = deformedMesh.vertices[deformedMesh.indices[(size_t)t * 3 + 1]];
      const Vertex& v2 = deformedMesh.vertices[deformedMesh.indices[(size_t)t * 3 + 2]];

      dst[0] = v0.px;
      dst[1] = v0.py;
      dst[2] = v0.pz;

      dst[4] = v1.px - v0.px;
      dst[5] = v1.py - v0.py;
      dst[6] = v1.pz - v0.pz;

      dst[8] = v2.px - v0.px;
      dst[9] = v2.py - v0.py;
      dst[10] = v2.pz - v0.pz;
   }

   // Pre-order numbering guarantees parent index < child index, so a reverse
   // linear scan visits every child before its parent.
   const int numNodes = NodeCount();
   for (int n = numNodes - 1; n >= 0; n--)
   {
      float* node = &nodeTexels[(size_t)n * 8];
      const int32_t w0 = FloatAsInt(node[3]);
      const int32_t w1 = FloatAsInt(node[7]);
      if (w1 > 0)
      {
         Bounds3 b;
         for (int k = 0; k < w1; k++)
         {
            const float* tri = &triTexels[(size_t)(w0 + k) * 12];
            const float p0[3] = { tri[0], tri[1], tri[2] };
            const float p1[3] = { tri[0] + tri[4], tri[1] + tri[5], tri[2] + tri[6] };
            const float p2[3] = { tri[0] + tri[8], tri[1] + tri[9], tri[2] + tri[10] };
            b.Expand(p0);
            b.Expand(p1);
            b.Expand(p2);
         }
         node[0] = b.lo[0]; node[1] = b.lo[1]; node[2] = b.lo[2];
         node[4] = b.hi[0]; node[5] = b.hi[1]; node[6] = b.hi[2];
      }
      else
      {
         const float* left = &nodeTexels[(size_t)w0 * 8];
         const float* right = &nodeTexels[(size_t)(-w1) * 8];
         node[0] = std::min(left[0], right[0]);
         node[1] = std::min(left[1], right[1]);
         node[2] = std::min(left[2], right[2]);
         node[4] = std::max(left[4], right[4]);
         node[5] = std::max(left[5], right[5]);
         node[6] = std::max(left[6], right[6]);
      }
   }
}

Hit Blas::Intersect(const Ray& ray) const
{
   Hit hit;
   if (Empty())
      return hit;

   hit.t = ray.tMax;
   const float invD[3] = {
      1.0f / (std::fabs(ray.d[0]) > 1e-20f ? ray.d[0] : (ray.d[0] >= 0.0f ? 1e-20f : -1e-20f)),
      1.0f / (std::fabs(ray.d[1]) > 1e-20f ? ray.d[1] : (ray.d[1] >= 0.0f ? 1e-20f : -1e-20f)),
      1.0f / (std::fabs(ray.d[2]) > 1e-20f ? ray.d[2] : (ray.d[2] >= 0.0f ? 1e-20f : -1e-20f))
   };

   const float* nodes = nodeTexels.data();
   const float* tris = triTexels.data();

   float rootNear = 0.0f;
   if (!IntersectAabb(&nodes[0], &nodes[4], ray.o, invD, ray.tMin, hit.t, rootNear))
      return hit;

   int stack[64];
   int top = 0;
   int cur = 0;

   for (;;)
   {
      const float* node = &nodes[(size_t)cur * 8];
      const int32_t w0 = FloatAsInt(node[3]);
      const int32_t w1 = FloatAsInt(node[7]);

      if (w1 > 0)
      {
         for (int k = 0; k < w1; k++)
         {
            const float* tri = &tris[(size_t)(w0 + k) * 12];
            float t, u, v;
            if (IntersectTriangle(ray.o, ray.d, ray.tMin, hit.t, &tri[0], &tri[4], &tri[8], t, u, v))
            {
               hit.t = t;
               hit.triIndex = FloatAsInt(tri[3]);
               hit.materialId = FloatAsInt(tri[11]);
               hit.u = u;
               hit.v = v;
            }
         }
         if (top == 0)
            break;
         cur = stack[--top];
      }
      else
      {
         const int leftIdx = w0;
         const int rightIdx = -w1;
         const float* leftNode = &nodes[(size_t)leftIdx * 8];
         const float* rightNode = &nodes[(size_t)rightIdx * 8];

         float tNearL = 0.0f, tNearR = 0.0f;
         const bool hitL = IntersectAabb(&leftNode[0], &leftNode[4], ray.o, invD, ray.tMin, hit.t, tNearL);
         const bool hitR = IntersectAabb(&rightNode[0], &rightNode[4], ray.o, invD, ray.tMin, hit.t, tNearR);

         if (hitL && hitR)
         {
            if (tNearL <= tNearR)
            {
               stack[top++] = rightIdx;
               cur = leftIdx;
            }
            else
            {
               stack[top++] = leftIdx;
               cur = rightIdx;
            }
         }
         else if (hitL)
         {
            cur = leftIdx;
         }
         else if (hitR)
         {
            cur = rightIdx;
         }
         else
         {
            if (top == 0)
               break;
            cur = stack[--top];
         }
      }
   }

   if (!hit.Valid())
      hit.t = 1e30f;
   return hit;
}

void Tlas::Build(const std::vector<Instance>& inInstances, const std::vector<const Blas*>& blases)
{
   nodeTexels.clear();
   instances = inInstances;
   const int numInst = (int)instances.size();
   if (numInst <= 0)
      return;

   std::vector<PrimBuildInfo> prims;
   prims.reserve((size_t)numInst);

   for (int i = 0; i < numInst; i++)
   {
      Instance& inst = instances[(size_t)i];
      if (inst.blasIndex < 0 || inst.blasIndex >= (int)blases.size())
         continue;
      const Blas* blas = blases[(size_t)inst.blasIndex];
      if (blas == nullptr || blas->Empty())
         continue;

      // Compute world-to-local 3x4 affine inverse in double precision so
      // non-uniform scale + rotation does not lose precision in cofactor products.
      double a[16];
      for (int k = 0; k < 16; k++)
         a[k] = (double)inst.transform.m[k];
      double inv[16];
      inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
      inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
      inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
      inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
      inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
      inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
      inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
      inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
      inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
      inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
      inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
      inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
      const double det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
      const double invDet = std::fabs(det) > 1e-18 ? 1.0 / det : 0.0;
      for (int row = 0; row < 3; row++)
         for (int col = 0; col < 4; col++)
            inst.invTransform[row * 4 + col] = (float)(inv[col * 4 + row] * invDet);

      const float* root = blas->nodeTexels.data();
      const float lo[3] = { root[0], root[1], root[2] };
      const float hi[3] = { root[4], root[5], root[6] };

      Bounds3 wb;
      for (int mask = 0; mask < 8; mask++)
      {
         const float lx = (mask & 1) ? hi[0] : lo[0];
         const float ly = (mask & 2) ? hi[1] : lo[1];
         const float lz = (mask & 4) ? hi[2] : lo[2];
         const float* m = inst.transform.m;
         const float wx = m[0] * lx + m[4] * ly + m[8] * lz + m[12];
         const float wy = m[1] * lx + m[5] * ly + m[9] * lz + m[13];
         const float wz = m[2] * lx + m[6] * ly + m[10] * lz + m[14];
         const float wp[3] = { wx, wy, wz };
         wb.Expand(wp);
      }

      for (int k = 0; k < 3; k++)
      {
         inst.worldMin[k] = wb.lo[k];
         inst.worldMax[k] = wb.hi[k];
      }

      PrimBuildInfo info;
      info.index = i;
      info.bounds = wb;
      for (int k = 0; k < 3; k++)
         info.centroid[k] = 0.5f * (wb.lo[k] + wb.hi[k]);
      prims.push_back(info);
   }

   if (prims.empty())
      return;

   nodeTexels.reserve(prims.size() * 2 * 8);
   // Each TLAS leaf holds 1 instance so w0 can be rewritten directly to its
   // original instance index without permuting `instances`.
   BuildSahRecursive(prims, 0, (int)prims.size(), 1, kCostTrav, kCostIsect, nodeTexels);

   const int numNodes = NodeCount();
   for (int n = 0; n < numNodes; n++)
   {
      float* node = &nodeTexels[(size_t)n * 8];
      const int32_t w0 = FloatAsInt(node[3]);
      const int32_t w1 = FloatAsInt(node[7]);
      if (w1 > 0)
         node[3] = IntAsFloat(prims[(size_t)w0].index);
   }
}

void Tlas::Build(const std::vector<Instance>& inInstances, const std::vector<Blas>& blases)
{
   std::vector<const Blas*> ptrs(blases.size());
   for (size_t i = 0; i < blases.size(); i++)
      ptrs[i] = &blases[i];
   Build(inInstances, ptrs);
}

Hit Tlas::Intersect(const Ray& ray, const std::vector<const Blas*>& blases) const
{
   Hit hit;
   if (Empty())
      return hit;

   hit.t = ray.tMax;
   const float invD[3] = {
      1.0f / (std::fabs(ray.d[0]) > 1e-20f ? ray.d[0] : (ray.d[0] >= 0.0f ? 1e-20f : -1e-20f)),
      1.0f / (std::fabs(ray.d[1]) > 1e-20f ? ray.d[1] : (ray.d[1] >= 0.0f ? 1e-20f : -1e-20f)),
      1.0f / (std::fabs(ray.d[2]) > 1e-20f ? ray.d[2] : (ray.d[2] >= 0.0f ? 1e-20f : -1e-20f))
   };

   const float* nodes = nodeTexels.data();
   float rootNear = 0.0f;
   if (!IntersectAabb(&nodes[0], &nodes[4], ray.o, invD, ray.tMin, hit.t, rootNear))
      return hit;

   int stack[64];
   int top = 0;
   int cur = 0;

   for (;;)
   {
      const float* node = &nodes[(size_t)cur * 8];
      const int32_t w0 = FloatAsInt(node[3]);
      const int32_t w1 = FloatAsInt(node[7]);

      if (w1 > 0)
      {
         const int instIdx = w0;
         const Instance& inst = instances[(size_t)instIdx];
         const Blas* blas = blases[(size_t)inst.blasIndex];
         if (blas != nullptr)
         {
            const float* m = inst.invTransform;
            Ray localRay;
            localRay.o[0] = m[0] * ray.o[0] + m[1] * ray.o[1] + m[2] * ray.o[2] + m[3];
            localRay.o[1] = m[4] * ray.o[0] + m[5] * ray.o[1] + m[6] * ray.o[2] + m[7];
            localRay.o[2] = m[8] * ray.o[0] + m[9] * ray.o[1] + m[10] * ray.o[2] + m[11];

            localRay.d[0] = m[0] * ray.d[0] + m[1] * ray.d[1] + m[2] * ray.d[2];
            localRay.d[1] = m[4] * ray.d[0] + m[5] * ray.d[1] + m[6] * ray.d[2];
            localRay.d[2] = m[8] * ray.d[0] + m[9] * ray.d[1] + m[10] * ray.d[2];

            localRay.tMin = ray.tMin;
            localRay.tMax = hit.t;

            const Hit localHit = blas->Intersect(localRay);
            if (localHit.Valid() && localHit.t < hit.t)
            {
               hit = localHit;
               hit.instanceIndex = instIdx;
               if (inst.materialId >= 0)
                  hit.materialId = inst.materialId;
            }
         }
         if (top == 0)
            break;
         cur = stack[--top];
      }
      else
      {
         const int leftIdx = w0;
         const int rightIdx = -w1;
         const float* leftNode = &nodes[(size_t)leftIdx * 8];
         const float* rightNode = &nodes[(size_t)rightIdx * 8];

         float tNearL = 0.0f, tNearR = 0.0f;
         const bool hitL = IntersectAabb(&leftNode[0], &leftNode[4], ray.o, invD, ray.tMin, hit.t, tNearL);
         const bool hitR = IntersectAabb(&rightNode[0], &rightNode[4], ray.o, invD, ray.tMin, hit.t, tNearR);

         if (hitL && hitR)
         {
            if (tNearL <= tNearR)
            {
               stack[top++] = rightIdx;
               cur = leftIdx;
            }
            else
            {
               stack[top++] = leftIdx;
               cur = rightIdx;
            }
         }
         else if (hitL)
         {
            cur = leftIdx;
         }
         else if (hitR)
         {
            cur = rightIdx;
         }
         else
         {
            if (top == 0)
               break;
            cur = stack[--top];
         }
      }
   }

   if (!hit.Valid())
      hit.t = 1e30f;
   return hit;
}

Hit Tlas::Intersect(const Ray& ray, const std::vector<Blas>& blases) const
{
   std::vector<const Blas*> ptrs(blases.size());
   for (size_t i = 0; i < blases.size(); i++)
      ptrs[i] = &blases[i];
   return Intersect(ray, ptrs);
}
}

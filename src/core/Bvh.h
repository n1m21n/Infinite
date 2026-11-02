#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "Mesh.h"

namespace Bvh
{
   inline float IntAsFloat(int32_t v)
   {
      float f;
      std::memcpy(&f, &v, sizeof(float));
      return f;
   }

   inline int32_t FloatAsInt(float f)
   {
      int32_t v;
      std::memcpy(&v, &f, sizeof(int32_t));
      return v;
   }

   struct Ray
   {
      float o[3] = { 0.0f, 0.0f, 0.0f };
      float d[3] = { 0.0f, 0.0f, -1.0f };
      float tMin = 1e-4f;
      float tMax = 1e30f;
   };

   struct Hit
   {
      float t = 1e30f;
      int triIndex = -1;
      int instanceIndex = -1;
      int materialId = 0;
      float u = 0.0f;
      float v = 0.0f;

      bool Valid() const { return triIndex >= 0; }
   };

   // Möller-Trumbore ray-triangle test matching the shader kernel.
   inline bool IntersectTriangle(const float o[3], const float d[3], float tMin, float tMax,
                                 const float v0[3], const float e1[3], const float e2[3],
                                 float& outT, float& outU, float& outV)
   {
      const float h[3] = {
         d[1] * e2[2] - d[2] * e2[1],
         d[2] * e2[0] - d[0] * e2[2],
         d[0] * e2[1] - d[1] * e2[0]
      };
      const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
      if (det > -1e-8f && det < 1e-8f)
         return false;
      const float invDet = 1.0f / det;
      const float s[3] = { o[0] - v0[0], o[1] - v0[1], o[2] - v0[2] };
      const float u = (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]) * invDet;
      if (u < 0.0f || u > 1.0f)
         return false;
      const float q[3] = {
         s[1] * e1[2] - s[2] * e1[1],
         s[2] * e1[0] - s[0] * e1[2],
         s[0] * e1[1] - s[1] * e1[0]
      };
      const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * invDet;
      if (v < 0.0f || u + v > 1.0f)
         return false;
      const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * invDet;
      if (t <= tMin || t >= tMax)
         return false;
      outT = t;
      outU = u;
      outV = v;
      return true;
   }

   // Bottom-Level Acceleration Structure (one per mesh in object space).
   // Flattened for OpenGL 4.1 samplerBuffer (RGBA32F):
   // - nodeTexels: 2 vec4 texels (8 floats) per node:
   //     texel 2i+0: (min.x, min.y, min.z, IntAsFloat(left_or_first_prim))
   //     texel 2i+1: (max.x, max.y, max.z, IntAsFloat(right_or_count))
   //   For a leaf node, right_or_count > 0 (1..4) and left_or_first_prim is the first triangle slot.
   //   For an interior node, right_or_count <= 0 (-right_child_idx) and left_or_first_prim is left_child_idx.
   // - triTexels: 3 vec4 texels (12 floats) per triangle:
   //     texel 3t+0: (v0.x, v0.y, v0.z, IntAsFloat(tri_index))
   //     texel 3t+1: (e1.x, e1.y, e1.z, IntAsFloat(0))
   //     texel 3t+2: (e2.x, e2.y, e2.z, IntAsFloat(material_id))
   struct Blas
   {
      static constexpr int kBins = 16;
      static constexpr int kMaxLeafPrims = 4;
      static constexpr float kCostTrav = 1.0f;
      static constexpr float kCostIsect = 1.5f;

      std::vector<float> nodeTexels;
      std::vector<float> triTexels;

      int NodeCount() const { return (int)(nodeTexels.size() / 8); }
      int TriCount() const { return (int)(triTexels.size() / 12); }
      bool Empty() const { return nodeTexels.empty() || triTexels.empty(); }

      void Build(const Mesh& mesh, int materialId = 0);
      void Refit(const Mesh& deformedMesh);
      Hit Intersect(const Ray& ray) const;
   };

   struct Instance
   {
      int blasIndex = 0;
      int blasRootIndex = 0;
      int materialId = -1; // < 0 keeps the BLAS triangle's material id
      Mat4 transform = Mat4::Identity();

      // Populated by Tlas::Build:
      float worldMin[3] = { 0.0f, 0.0f, 0.0f };
      float worldMax[3] = { 0.0f, 0.0f, 0.0f };
      float invTransform[12] = { 1, 0, 0, 0,
                                 0, 1, 0, 0,
                                 0, 0, 1, 0 }; // row-major 3x4 world-to-local matrix
   };

   // Top-Level Acceleration Structure over world-space instances.
   // nodeTexels uses the same 2-texel-per-node RGBA32F layout as Blas.
   struct Tlas
   {
      static constexpr int kBins = 16;
      static constexpr int kMaxLeafPrims = 4;
      static constexpr float kCostTrav = 1.0f;
      static constexpr float kCostIsect = 1.5f;

      std::vector<float> nodeTexels;
      std::vector<Instance> instances;

      int NodeCount() const { return (int)(nodeTexels.size() / 8); }
      bool Empty() const { return nodeTexels.empty() || instances.empty(); }

      void Build(const std::vector<Instance>& inInstances, const std::vector<const Blas*>& blases);
      void Build(const std::vector<Instance>& inInstances, const std::vector<Blas>& blases);
      Hit Intersect(const Ray& ray, const std::vector<const Blas*>& blases) const;
      Hit Intersect(const Ray& ray, const std::vector<Blas>& blases) const;
   };
}

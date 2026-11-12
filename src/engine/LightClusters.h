#pragma once
// Row 5.6 (FINISH_PLAN 6.10): more than 8 lights. Every frame the CPU bins each light's sphere of reach into a
// 16 x 9 x 24 froxel grid (screen tiles x exponential depth slices, near 0.1 to far 1000); suns, directional and
// ambient lights go in every cell. The shader finds its cell from its world position through the same viewProj,
// so the CPU and GPU never disagree about a y flip. Depth is the clip w (view depth for a perspective camera).
//
// Words: [0..2] grid x, y, z, [3] cell count, [4] near (float bits), [5] log(far / near) (float bits), [6..11] 0,
// then one word per cell (index offset << 8 | count), then the light indices.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Scene.h"

namespace engine
{
   constexpr int kClusterX = 16, kClusterY = 9, kClusterZ = 24, kClusterHeader = 12;
   constexpr float kClusterNear = 0.1f, kClusterFar = 1000.0f;

   // How far a light reaches: its cutoff, or where it falls under 1/256 of exposure white. < 0 = everywhere.
   inline float LightReach(const Light& l, float exposure)
   {
      if (l.type == LightType::Directional || l.type == LightType::Sun || l.type == LightType::Ambient)
         return -1.0f;
      const float peak = std::max({ l.colorSrgb[0], l.colorSrgb[1], l.colorSrgb[2] });
      const float lin = peak <= 0.04045f ? peak / 12.92f : std::pow((peak + 0.055f) / 1.055f, 2.4f);
      const float I = lin * std::max(l.intensity, 0.0f) * std::max(exposure, 1e-3f) * 256.0f;
      float r = -1.0f;
      if (l.falloff > 0.0f) r = std::sqrt(std::max(I - 1.0f, 0.0f) / l.falloff);
      else if (l.falloff < 0.0f) r = std::sqrt(I / -l.falloff);
      if (l.type == LightType::Area) r = r < 0.0f ? r : r + 0.5f * std::hypot(l.areaW, l.areaH);
      if (l.cutoff > 0.0f) r = r < 0.0f ? l.cutoff : std::min(r, l.cutoff);
      return r;
   }

   inline int ClusterSlice(float z)
   {
      const float k = std::log(std::max(z, kClusterNear) / kClusterNear) / std::log(kClusterFar / kClusterNear);
      return std::clamp((int)std::floor(k * (float)kClusterZ), 0, kClusterZ - 1);
   }

   inline void BuildLightClusters(const Frame& fr, int n, std::vector<uint32_t>& out)
   {
      const int cells = kClusterX * kClusterY * kClusterZ;
      struct Box { int x0, x1, y0, y1, z0, z1, i; };
      Box boxes[kMaxLights];
      int nb = 0;
      const float* m = fr.viewProj; // column-major: clip[r] = sum_c m[c * 4 + r] * v[c]
      auto clip = [&](float x, float y, float z, float* o) {
         for (int r = 0; r < 4; r++) o[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
      };
      const float wLen = std::max(std::sqrt(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]), 1e-6f);
      for (int i = 0; i < n; i++)
      {
         const Light& l = fr.lights[(size_t)i];
         const float r = LightReach(l, fr.exposure);
         int x0 = 0, x1 = kClusterX - 1, y0 = 0, y1 = kClusterY - 1, z0 = 0, z1 = kClusterZ - 1;
         if (r >= 0.0f)
         {
            float c[4];
            clip(l.vec[0], l.vec[1], l.vec[2], c);
            const float zc = c[3], zr = r * wLen;
            if (zc + zr < kClusterNear) continue; // wholly behind the camera
            z0 = ClusterSlice(zc - zr); z1 = ClusterSlice(zc + zr);
            bool crossesNear = false;
            float nx0 = 1e9f, nx1 = -1e9f, ny0 = 1e9f, ny1 = -1e9f;
            for (int k = 0; k < 8 && !crossesNear; k++)
            {
               float q[4];
               clip(l.vec[0] + ((k & 1) ? r : -r), l.vec[1] + ((k & 2) ? r : -r), l.vec[2] + ((k & 4) ? r : -r), q);
               if (q[3] <= kClusterNear) { crossesNear = true; break; }
               nx0 = std::min(nx0, q[0] / q[3]); nx1 = std::max(nx1, q[0] / q[3]);
               ny0 = std::min(ny0, q[1] / q[3]); ny1 = std::max(ny1, q[1] / q[3]);
            }
            if (!crossesNear)
            {
               if (nx1 < -1.0f || nx0 > 1.0f || ny1 < -1.0f || ny0 > 1.0f) continue; // off screen
               auto tile = [](float ndc, int g) { return std::clamp((int)std::floor((ndc * 0.5f + 0.5f) * (float)g), 0, g - 1); };
               x0 = tile(nx0, kClusterX); x1 = tile(nx1, kClusterX);
               y0 = tile(ny0, kClusterY); y1 = tile(ny1, kClusterY);
            }
         }
         boxes[nb++] = { x0, x1, y0, y1, z0, z1, i };
      }
      // Two passes over the boxes: count per cell, then place the indices after a prefix sum (no per-cell lists).
      out.assign((size_t)(kClusterHeader + cells), 0u);
      uint32_t* cnt = &out[kClusterHeader];
      for (int b = 0; b < nb; b++)
         for (int z = boxes[b].z0; z <= boxes[b].z1; z++)
            for (int y = boxes[b].y0; y <= boxes[b].y1; y++)
               for (int x = boxes[b].x0; x <= boxes[b].x1; x++)
                  cnt[(z * kClusterY + y) * kClusterX + x]++;
      out[0] = kClusterX; out[1] = kClusterY; out[2] = kClusterZ; out[3] = (uint32_t)cells;
      const float nearF = kClusterNear, logR = std::log(kClusterFar / kClusterNear);
      std::memcpy(&out[4], &nearF, 4); std::memcpy(&out[5], &logR, 4);
      uint32_t off = 0;
      for (int c = 0; c < cells; c++)
      {
         const uint32_t k = out[(size_t)(kClusterHeader + c)];
         out[(size_t)(kClusterHeader + c)] = off << 8; // count refilled below
         off += k;
      }
      out.resize(out.size() + off, 0u);
      const size_t base = (size_t)(kClusterHeader + cells);
      for (int b = 0; b < nb; b++)
         for (int z = boxes[b].z0; z <= boxes[b].z1; z++)
            for (int y = boxes[b].y0; y <= boxes[b].y1; y++)
               for (int x = boxes[b].x0; x <= boxes[b].x1; x++)
               {
                  uint32_t& w = out[(size_t)(kClusterHeader + (z * kClusterY + y) * kClusterX + x)];
                  out[base + (w >> 8) + (w & 255u)] = (uint32_t)boxes[b].i;
                  w++;
               }
   }
}

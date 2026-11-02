#include "Skinning.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
   using GltfImport::GltfChannel;

   void Mul(const float a[16], const float b[16], float out[16])
   {
      float o[16];
      for (int c = 0; c < 4; c++)
         for (int r = 0; r < 4; r++)
            o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
      std::memcpy(out, o, sizeof(o));
   }

   void Trs(const float t[3], const float q[4], const float s[3], float m[16])
   {
      const float x = q[0], y = q[1], z = q[2], w = q[3];
      const float r[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                           2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) };
      for (int c = 0; c < 3; c++)
      {
         for (int k = 0; k < 3; k++)
            m[c * 4 + k] = r[c * 3 + k] * s[c];
         m[c * 4 + 3] = 0.0f;
      }
      m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1.0f;
   }

   // Samples one channel at `time` into `out` (width floats). Rotations take
   // the shorter arc and are renormalised (nlerp is within a fraction of a
   // degree of slerp at animation key spacing).
   void Sample(const GltfChannel& ch, float time, float* out)
   {
      const size_t keys = ch.times.size();
      const int w = ch.width;
      if (keys == 0)
         return;
      if (keys == 1 || time <= ch.times.front())
      {
         std::memcpy(out, &ch.values[0], w * sizeof(float));
         return;
      }
      if (time >= ch.times.back())
      {
         std::memcpy(out, &ch.values[(keys - 1) * w], w * sizeof(float));
         return;
      }
      const size_t hi = (size_t)(std::upper_bound(ch.times.begin(), ch.times.end(), time) - ch.times.begin());
      const size_t lo = hi - 1;
      const float* a = &ch.values[lo * w];
      const float* b = &ch.values[hi * w];
      if (ch.step)
      {
         std::memcpy(out, a, w * sizeof(float));
         return;
      }
      const float span = ch.times[hi] - ch.times[lo];
      const float f = span > 1e-9f ? (time - ch.times[lo]) / span : 0.0f;
      if (ch.path == GltfChannel::kRotation)
      {
         const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
         const float sign = d < 0.0f ? -1.0f : 1.0f;
         float len = 0.0f;
         for (int k = 0; k < 4; k++)
         {
            out[k] = a[k] + (b[k] * sign - a[k]) * f;
            len += out[k] * out[k];
         }
         len = len > 1e-12f ? 1.0f / std::sqrt(len) : 0.0f;
         for (int k = 0; k < 4; k++)
            out[k] *= len;
         return;
      }
      for (int k = 0; k < w; k++)
         out[k] = a[k] + (b[k] - a[k]) * f;
   }
}

namespace Skinning
{
   bool HasRig(const GltfImport::GltfDecodePackage& pkg)
   {
      return !pkg.joints.empty() || !pkg.morphDeltas.empty();
   }

   void Evaluate(const GltfImport::GltfDecodePackage& pkg, int clip, float time,
                 const std::vector<float>& morphWeights,
                 std::vector<Platform::ModelVertex>& out,
                 std::vector<float>* jointWorld)
   {
      const size_t vertexCount = pkg.vertices.size();
      const size_t jointCount = pkg.joints.size();
      out = pkg.vertices;

      struct Local { float t[3], r[4], s[3]; };
      std::vector<Local> local(jointCount);
      for (size_t j = 0; j < jointCount; j++)
      {
         std::memcpy(local[j].t, pkg.joints[j].t, sizeof(local[j].t));
         std::memcpy(local[j].r, pkg.joints[j].r, sizeof(local[j].r));
         std::memcpy(local[j].s, pkg.joints[j].s, sizeof(local[j].s));
      }

      std::vector<float> weights(pkg.morphDeltas.size(), 0.0f);
      for (size_t m = 0; m < weights.size() && m < morphWeights.size(); m++)
         weights[m] = morphWeights[m];

      if (clip >= 0 && clip < (int)pkg.clips.size())
      {
         const GltfImport::GltfClip& c = pkg.clips[clip];
         if (c.duration > 0.0f)
         {
            time = std::fmod(time, c.duration);
            if (time < 0.0f)
               time += c.duration;
         }
         std::vector<float> tmp;
         for (const GltfChannel& ch : c.channels)
         {
            tmp.assign(ch.width, 0.0f);
            Sample(ch, time, tmp.data());
            if (ch.path == GltfChannel::kWeights)
            {
               for (size_t m = 0; m < weights.size() && m < tmp.size(); m++)
                  weights[m] += tmp[m];
               continue;
            }
            if (ch.joint < 0 || ch.joint >= (int)jointCount)
               continue;
            float* dst = ch.path == GltfChannel::kTranslation ? local[ch.joint].t
                       : ch.path == GltfChannel::kRotation    ? local[ch.joint].r
                                                              : local[ch.joint].s;
            std::memcpy(dst, tmp.data(), ch.width * sizeof(float));
         }
      }

      for (size_t m = 0; m < weights.size(); m++)
      {
         const float wgt = weights[m];
         if (wgt == 0.0f)
            continue;
         const std::vector<float>& d = pkg.morphDeltas[m];
         for (size_t v = 0; v < vertexCount; v++)
         {
            out[v].px += d[v * 3 + 0] * wgt;
            out[v].py += d[v * 3 + 1] * wgt;
            out[v].pz += d[v * 3 + 2] * wgt;
         }
      }

      if (jointCount == 0)
         return;

      // Joints are parent-first, so one pass builds every world matrix.
      std::vector<float> world(jointCount * 16);
      std::vector<float> skin(jointCount * 16);
      for (size_t j = 0; j < jointCount; j++)
      {
         const GltfImport::GltfJoint& jt = pkg.joints[j];
         float m[16];
         Trs(local[j].t, local[j].r, local[j].s, m);
         Mul(jt.between, m, m);
         if (jt.parent >= 0)
            Mul(&world[jt.parent * 16], m, m);
         std::memcpy(&world[j * 16], m, sizeof(m));
         Mul(m, jt.inverseBind, &skin[j * 16]);
      }
      if (jointWorld != nullptr)
         *jointWorld = world;

      if (pkg.skinJoints.size() != vertexCount * 4 || pkg.skinWeights.size() != vertexCount * 4)
         return;

      for (size_t v = 0; v < vertexCount; v++)
      {
         const Platform::ModelVertex src = out[v];
         float p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
         float total = 0.0f;
         for (int k = 0; k < 4; k++)
         {
            const float wgt = pkg.skinWeights[v * 4 + k];
            const unsigned short j = pkg.skinJoints[v * 4 + k];
            if (wgt <= 0.0f || j >= jointCount)
               continue;
            const float* m = &skin[j * 16];
            p[0] += wgt * (m[0] * src.px + m[4] * src.py + m[8] * src.pz + m[12]);
            p[1] += wgt * (m[1] * src.px + m[5] * src.py + m[9] * src.pz + m[13]);
            p[2] += wgt * (m[2] * src.px + m[6] * src.py + m[10] * src.pz + m[14]);
            n[0] += wgt * (m[0] * src.nx + m[4] * src.ny + m[8] * src.nz);
            n[1] += wgt * (m[1] * src.nx + m[5] * src.ny + m[9] * src.nz);
            n[2] += wgt * (m[2] * src.nx + m[6] * src.ny + m[10] * src.nz);
            total += wgt;
         }
         if (total < 1e-4f)
            continue;
         const float inv = 1.0f / total;
         out[v].px = p[0] * inv; out[v].py = p[1] * inv; out[v].pz = p[2] * inv;
         const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
         if (len > 1e-12f)
         {
            out[v].nx = n[0] / len; out[v].ny = n[1] / len; out[v].nz = n[2] / len;
         }
      }
   }
}

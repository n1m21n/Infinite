#include "Skinning.h"

#include <algorithm>
#include <array>
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

   // Rotation part of a column-major matrix (columns normalised) as a quaternion.
   void MatQuat(const float m[16], float q[4])
   {
      float a[3][3]; // a[row][col]
      for (int c = 0; c < 3; c++)
      {
         const float len = std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
         for (int r = 0; r < 3; r++)
            a[r][c] = len > 1e-12f ? m[c * 4 + r] / len : 0.0f;
      }
      const float tr = a[0][0] + a[1][1] + a[2][2];
      if (tr > 0.0f)
      {
         const float k = std::sqrt(tr + 1.0f) * 2.0f;
         q[3] = 0.25f * k; q[0] = (a[2][1] - a[1][2]) / k; q[1] = (a[0][2] - a[2][0]) / k; q[2] = (a[1][0] - a[0][1]) / k;
      }
      else if (a[0][0] > a[1][1] && a[0][0] > a[2][2])
      {
         const float k = std::sqrt(1.0f + a[0][0] - a[1][1] - a[2][2]) * 2.0f;
         q[3] = (a[2][1] - a[1][2]) / k; q[0] = 0.25f * k; q[1] = (a[0][1] + a[1][0]) / k; q[2] = (a[0][2] + a[2][0]) / k;
      }
      else if (a[1][1] > a[2][2])
      {
         const float k = std::sqrt(1.0f + a[1][1] - a[0][0] - a[2][2]) * 2.0f;
         q[3] = (a[0][2] - a[2][0]) / k; q[0] = (a[0][1] + a[1][0]) / k; q[1] = 0.25f * k; q[2] = (a[1][2] + a[2][1]) / k;
      }
      else
      {
         const float k = std::sqrt(1.0f + a[2][2] - a[0][0] - a[1][1]) * 2.0f;
         q[3] = (a[1][0] - a[0][1]) / k; q[0] = (a[0][2] + a[2][0]) / k; q[1] = (a[1][2] + a[2][1]) / k; q[2] = 0.25f * k;
      }
   }

   float Len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

   // The shortest turn taking direction a onto direction b.
   void QuatBetween(const float a[3], const float b[3], float q[4])
   {
      const float la = Len(a), lb = Len(b);
      q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f;
      if (la < 1e-9f || lb < 1e-9f)
         return;
      const float u[3] = { a[0] / la, a[1] / la, a[2] / la }, v[3] = { b[0] / lb, b[1] / lb, b[2] / lb };
      const float d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
      if (d < -0.999999f)
      {
         // Opposite: any axis square to u.
         float ax[3] = { 0.0f, -u[2], u[1] };
         if (Len(ax) < 1e-6f) { ax[0] = u[2]; ax[1] = 0.0f; ax[2] = -u[0]; }
         const float l = Len(ax);
         q[0] = ax[0] / l; q[1] = ax[1] / l; q[2] = ax[2] / l; q[3] = 0.0f;
         return;
      }
      q[0] = u[1] * v[2] - u[2] * v[1];
      q[1] = u[2] * v[0] - u[0] * v[2];
      q[2] = u[0] * v[1] - u[1] * v[0];
      q[3] = 1.0f + d;
      const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
      for (int k = 0; k < 4; k++)
         q[k] /= l;
   }

   // FABRIK (Aristidou and Lasenby): pull the chain to the target from the
   // tip, then back to the fixed root, until the tip is close. A pole then
   // swings each middle joint about the line through its neighbours toward
   // the pole, which keeps the reach (Blender's pole angle, without the
   // extra roll).
   void Fabrik(std::vector<std::array<float, 3>>& p, const float target[3], const float* pole)
   {
      const size_t n = p.size();
      std::vector<float> len(n - 1);
      float total = 0.0f;
      for (size_t i = 0; i + 1 < n; i++)
      {
         const float d[3] = { p[i + 1][0] - p[i][0], p[i + 1][1] - p[i][1], p[i + 1][2] - p[i][2] };
         len[i] = Len(d);
         total += len[i];
      }
      const std::array<float, 3> root = p[0];
      auto place = [](std::array<float, 3>& moved, const std::array<float, 3>& anchor, float l)
      {
         const float d[3] = { moved[0] - anchor[0], moved[1] - anchor[1], moved[2] - anchor[2] };
         const float dl = Len(d);
         const float f = dl > 1e-9f ? l / dl : 0.0f;
         for (int k = 0; k < 3; k++)
            moved[k] = anchor[k] + d[k] * f;
      };
      for (int iter = 0; iter < 16; iter++)
      {
         const float e[3] = { p[n - 1][0] - target[0], p[n - 1][1] - target[1], p[n - 1][2] - target[2] };
         if (Len(e) < total * 1e-4f)
            break;
         p[n - 1] = { target[0], target[1], target[2] };
         for (size_t i = n - 1; i-- > 0;)
            place(p[i], p[i + 1], len[i]);
         p[0] = root;
         for (size_t i = 1; i < n; i++)
            place(p[i], p[i - 1], len[i - 1]);
      }
      if (pole == nullptr)
         return;
      for (size_t i = 1; i + 1 < n; i++)
      {
         const float* a = p[i - 1].data();
         const float* b = p[i + 1].data();
         float axis[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
         const float al = Len(axis);
         if (al < 1e-9f)
            continue;
         for (float& v : axis) v /= al;
         auto flat = [&](const float* x, float out[3])
         {
            const float d[3] = { x[0] - a[0], x[1] - a[1], x[2] - a[2] };
            const float along = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
            for (int k = 0; k < 3; k++)
               out[k] = d[k] - axis[k] * along;
         };
         float j[3], pl[3];
         flat(p[i].data(), j);
         flat(pole, pl);
         if (Len(j) < 1e-9f || Len(pl) < 1e-9f)
            continue;
         // Turn the joint about the a-b line by the signed angle from j to
         // pl (Rodrigues), which also covers the pole straight opposite.
         const float cr[3] = { j[1] * pl[2] - j[2] * pl[1], j[2] * pl[0] - j[0] * pl[2], j[0] * pl[1] - j[1] * pl[0] };
         const float angle = std::atan2(cr[0] * axis[0] + cr[1] * axis[1] + cr[2] * axis[2],
                                        j[0] * pl[0] + j[1] * pl[1] + j[2] * pl[2]);
         const float cs = std::cos(angle), sn = std::sin(angle);
         const float d[3] = { p[i][0] - a[0], p[i][1] - a[1], p[i][2] - a[2] };
         const float along = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
         const float ad[3] = { axis[1] * d[2] - axis[2] * d[1], axis[2] * d[0] - axis[0] * d[2], axis[0] * d[1] - axis[1] * d[0] };
         for (int k = 0; k < 3; k++)
            p[i][k] = a[k] + d[k] * cs + ad[k] * sn + axis[k] * along * (1.0f - cs);
      }
   }
}

namespace Skinning
{
   void QuatMul(const float a[4], const float b[4], float out[4])
   {
      const float o[4] = { a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                           a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                           a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                           a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] };
      std::memcpy(out, o, sizeof(o));
   }

   void QuatFromEuler(const float deg[3], float q[4])
   {
      const float k = 3.14159265f / 360.0f;
      const float qx[4] = { std::sin(deg[0] * k), 0, 0, std::cos(deg[0] * k) };
      const float qy[4] = { 0, std::sin(deg[1] * k), 0, std::cos(deg[1] * k) };
      const float qz[4] = { 0, 0, std::sin(deg[2] * k), std::cos(deg[2] * k) };
      float zy[4];
      QuatMul(qz, qy, zy);
      QuatMul(zy, qx, q);
   }

   void EulerFromQuat(const float q[4], float deg[3])
   {
      const float x = q[0], y = q[1], z = q[2], w = q[3];
      const float k = 180.0f / 3.14159265f;
      // R = Rz * Ry * Rx: r20 = -sin(y), r21 = cos(y) sin(x), r22 = cos(y) cos(x), r10, r00.
      const float r20 = 2 * (x * z - w * y);
      const float r21 = 2 * (y * z + w * x);
      const float r22 = 1 - 2 * (x * x + y * y);
      const float r10 = 2 * (x * y + w * z);
      const float r00 = 1 - 2 * (y * y + z * z);
      deg[1] = std::asin(std::clamp(-r20, -1.0f, 1.0f)) * k;
      if (std::fabs(r20) < 0.9999f)
      {
         deg[0] = std::atan2(r21, r22) * k;
         deg[2] = std::atan2(r10, r00) * k;
      }
      else
      {
         // Gimbal lock: put the whole turn into Z.
         const float r01 = 2 * (x * y - w * z);
         const float r11 = 1 - 2 * (x * x + z * z);
         deg[0] = 0.0f;
         deg[2] = std::atan2(-r01, r11) * k;
      }
   }

   bool HasRig(const GltfImport::GltfDecodePackage& pkg)
   {
      return !pkg.joints.empty() || !pkg.morphDeltas.empty();
   }

   void Evaluate(const GltfImport::GltfDecodePackage& pkg, int clip, float time,
                 const std::vector<float>& morphWeights,
                 std::vector<Platform::ModelVertex>& out,
                 std::vector<float>* jointWorld,
                 const std::vector<float>* poseOffsets,
                 const IkGoal* ik)
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

      if (poseOffsets != nullptr && poseOffsets->size() == jointCount * 4)
         for (size_t j = 0; j < jointCount; j++)
            QuatMul(local[j].r, &(*poseOffsets)[j * 4], local[j].r);

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
      auto build = [&]()
      {
         for (size_t j = 0; j < jointCount; j++)
         {
            const GltfImport::GltfJoint& jt = pkg.joints[j];
            float m[16];
            Trs(local[j].t, local[j].r, local[j].s, m);
            Mul(jt.between, m, m);
            if (jt.parent >= 0)
               Mul(&world[jt.parent * 16], m, m);
            std::memcpy(&world[j * 16], m, sizeof(m));
         }
      };
      build();

      // IK: solve the chain's head positions, then turn each joint so its
      // child's head lands on the solved spot, root first.
      if (ik != nullptr && ik->joint >= 0 && ik->joint < (int)jointCount && ik->weight > 0.0f)
      {
         std::vector<int> chain{ ik->joint };
         while ((int)chain.size() <= std::max(1, ik->chain) && pkg.joints[chain.back()].parent >= 0)
            chain.push_back(pkg.joints[chain.back()].parent);
         std::reverse(chain.begin(), chain.end());
         if (chain.size() >= 2)
         {
            auto head = [&](int j) { return std::array<float, 3>{ world[j * 16 + 12], world[j * 16 + 13], world[j * 16 + 14] }; };
            std::vector<std::array<float, 3>> pos;
            for (int j : chain)
               pos.push_back(head(j));
            // A dead-straight chain has no bend for FABRIK to grow, so its
            // middle joints start a hair off the line, toward the pole if any.
            for (size_t i = 1; i + 1 < pos.size(); i++)
            {
               float off[3];
               for (int k = 0; k < 3; k++)
                  off[k] = ik->usePole ? ik->pole[k] - pos[i][k] : world[chain[i] * 16 + k];   // else the joint's own X
               const float l = Len(off);
               if (l > 1e-9f)
                  for (int k = 0; k < 3; k++) pos[i][k] += off[k] / l * 1e-3f;
            }
            Fabrik(pos, ik->target, ik->usePole ? ik->pole : nullptr);
            for (size_t i = 0; i + 1 < chain.size(); i++)
            {
               const int j = chain[i];
               const std::array<float, 3> h = head(j), c = head(chain[i + 1]);
               const float from[3] = { c[0] - h[0], c[1] - h[1], c[2] - h[2] };
               const float to[3] = { pos[i + 1][0] - h[0], pos[i + 1][1] - h[1], pos[i + 1][2] - h[2] };
               float dq[4];
               QuatBetween(from, to, dq);
               if (ik->weight < 1.0f)
               {
                  const float wgt = ik->weight;
                  float l = 0.0f;
                  for (int k = 0; k < 4; k++)
                  {
                     dq[k] = (k == 3 ? 1.0f - wgt : 0.0f) + dq[k] * wgt;
                     l += dq[k] * dq[k];
                  }
                  l = std::sqrt(l);
                  for (float& v : dq) v /= l;
               }
               // A world turn about the head, moved into the joint's local
               // frame: r' = P^-1 dq P r, P = the parent side's rotation.
               float pm[16];
               std::memcpy(pm, pkg.joints[j].between, sizeof(pm));
               if (pkg.joints[j].parent >= 0)
                  Mul(&world[pkg.joints[j].parent * 16], pm, pm);
               float qp[4], qpi[4], t1[4], t2[4];
               MatQuat(pm, qp);
               qpi[0] = -qp[0]; qpi[1] = -qp[1]; qpi[2] = -qp[2]; qpi[3] = qp[3];
               QuatMul(qpi, dq, t1);
               QuatMul(t1, qp, t2);
               QuatMul(t2, local[j].r, local[j].r);
               build();
            }
         }
      }

      std::vector<float> skin(jointCount * 16);
      for (size_t j = 0; j < jointCount; j++)
         Mul(&world[j * 16], pkg.joints[j].inverseBind, &skin[j * 16]);
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

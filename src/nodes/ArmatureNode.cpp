#include "ArmatureNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Base64.h"
#include "CurveOps.h"
#include "GeometryOpNodes.h"
#include "Skinning.h"
#include "Transport.h"

namespace
{
   const Mesh kEmptyMesh;
   const float kUp[3] = { 0.0f, 1.0f, 0.0f };

   // [R | p] from a quaternion and a position, column-major.
   Mat4 Frame(const float q[4], const float p[3])
   {
      const float x = q[0], y = q[1], z = q[2], w = q[3];
      Mat4 m;
      m.m[0] = 1 - 2 * (y * y + z * z); m.m[1] = 2 * (x * y + z * w);     m.m[2] = 2 * (x * z - y * w);
      m.m[4] = 2 * (x * y - z * w);     m.m[5] = 1 - 2 * (x * x + z * z); m.m[6] = 2 * (y * z + x * w);
      m.m[8] = 2 * (x * z + y * w);     m.m[9] = 2 * (y * z - x * w);     m.m[10] = 1 - 2 * (x * x + y * y);
      m.m[12] = p[0]; m.m[13] = p[1]; m.m[14] = p[2];
      return m;
   }

   void AxisAngle(const float axis[3], float degrees, float q[4])
   {
      const float h = degrees * 3.14159265f / 360.0f;
      const float s = std::sin(h);
      q[0] = axis[0] * s; q[1] = axis[1] * s; q[2] = axis[2] * s; q[3] = std::cos(h);
   }

   float SegmentDistance(const float* p, const float* a, const float* b)
   {
      const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
      const float ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
      const float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
      float t = len2 > 0.0f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2 : 0.0f;
      t = std::clamp(t, 0.0f, 1.0f);
      const float d[3] = { ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t };
      return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
   }
}

void ArmatureNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   Update();
}

void ArmatureNode::Update()
{
   if (input == nullptr)
   {
      mCookWarning.clear();
      mStatus = "no input";
      mPkg.joints.clear();
      return;
   }
   if (mPaintData != mPaintDecodedFrom)
   {
      // A load or an undo replaced the text.
      mPaintDecodedFrom = mPaintData;
      DecodePaint();
   }

   // What the rest chain and the weights depend on.
   InstanceOnPointsNode* instancer = FindInstancer(input);
   const Polyline* pathCurve = path ? path->GetCurve() : nullptr;
   const int n = std::clamp(bones, 2, 32);
   std::vector<double> restKey = { (double)(uintptr_t)input, (double)(uintptr_t)path, (double)axis, (double)n, envelope,
                                   (double)(instancer ? 0 : input->MeshRevision()),
                                   (double)input->PointCloudRevision(), (double)input->CurveStamp(),
                                   (double)(pathCurve ? path->CurveStamp() : 0) };
   const std::vector<Mat4>* instances = instancer ? &ResolveInstanceTransforms(input, instancer) : nullptr;
   if (instances != nullptr)
   {
      // Copies move without a revision of their own, so their origins are the key.
      restKey.push_back((double)instances->size());
      for (const Mat4& t : *instances)
         restKey.insert(restKey.end(), { t.m[12], t.m[13], t.m[14] });
   }
   const bool rebuild = restKey != mRestKey;
   if (rebuild)
   {
      mRestKey = std::move(restKey);
      BuildRest();
   }
   else if (mPaintRevision != mWeightedRevision)
      Reweight();
   mWeightedRevision = mPaintRevision;

   // What the pose depends on. The wave only reads the clock while it moves.
   const int jointCount = (int)mPkg.joints.size();
   std::vector<double> poseKey = { curl, curlDirection, twist, waveAmount, waveLength, waveSpeed, wavePhase,
                                   (double)mPaintRevision, (double)paintBone };
   const bool waving = waveAmount != 0.0f && waveSpeed != 0.0f;
   const float seconds = waving ? (float)Transport::Instance().Seconds() : 0.0f;
   poseKey.push_back(seconds);
   std::vector<float> slots;
   pose.AppendKey(slots, jointCount);
   poseKey.insert(poseKey.end(), slots.begin(), slots.end());
   if (!rebuild && poseKey == mPoseKey)
      return;
   mPoseKey = std::move(poseKey);
   if (jointCount < 2)
      return;

   // Curl, twist and wave turn the inner joints (1..n-1), so the base bone
   // stays where it is. Each joint's turn is in its own frame: the bend axis
   // lies across the bone, picked by curlDirection; twist turns about it.
   std::vector<float> offsets(jointCount * 4, 0.0f);
   for (int j = 0; j < jointCount; j++)
      offsets[j * 4 + 3] = 1.0f;
   const float dir = curlDirection * 3.14159265f / 180.0f;
   const float bendAxis[3] = { std::cos(dir), 0.0f, std::sin(dir) };
   const float inner = (float)(n - 1);
   const float length = std::max(0.01f, waveLength);
   for (int j = 1; j < n; j++)
   {
      const float wave = waveAmount * std::sin(6.2831853f * ((float)j / (float)n / length - seconds * waveSpeed - wavePhase));
      float bend[4], turn[4];
      AxisAngle(bendAxis, curl / inner + wave, bend);
      AxisAngle(kUp, twist / inner, turn);
      Skinning::QuatMul(bend, turn, &offsets[j * 4]);
   }
   pose.ApplySlots(offsets, jointCount);
   const float centre[3] = { 0.0f, 0.0f, 0.0f };
   Skinning::IkGoal goal;
   const bool ik = pose.Goal(jointCount, centre, 1.0f, goal);
   std::vector<Platform::ModelVertex> none;
   Skinning::Evaluate(mPkg, -1, 0.0f, {}, none, &mJointWorld, &offsets, ik ? &goal : nullptr);

   std::vector<float> skin(jointCount * 16);
   for (int j = 0; j < jointCount; j++)
   {
      Mat4 world;
      std::copy(&mJointWorld[j * 16], &mJointWorld[j * 16] + 16, world.m);
      Mat4 bind;
      std::copy(mPkg.joints[j].inverseBind, mPkg.joints[j].inverseBind + 16, bind.m);
      const Mat4 s = Mat4::Multiply(world, bind);
      std::copy(s.m, s.m + 16, &skin[j * 16]);
   }
   ApplySkin(skin);
}

void ArmatureNode::BuildRest()
{
   mPkg = GltfImport::GltfDecodePackage();
   mJointWorld.clear();
   mJoints.clear();
   mWeights.clear();
   mEnv.clear();
   mNearest.clear();
   mEnvBones = 0;
   mMeshCount = mCloudCount = mCurveCount = mInstanceCount = 0;
   mHasInstances = false;
   mCookWarning.clear();

   // Every point that bends, in order: mesh, cloud, curve, instance origins.
   std::vector<float> pts;
   InstanceOnPointsNode* instancer = FindInstancer(input);
   if (instancer == nullptr)
   {
      const Mesh& mesh = input->GetMesh();
      mMeshCount = mesh.vertices.size();
      for (const Vertex& v : mesh.vertices)
         pts.insert(pts.end(), { v.px, v.py, v.pz });
   }
   if (const std::vector<Particle>* cloud = input->GetPointCloud())
   {
      mCloudCount = cloud->size();
      for (const Particle& p : *cloud)
         pts.insert(pts.end(), { p.px, p.py, p.pz });
   }
   else if (input->GetGpuPoints() != nullptr)
      mCookWarning = "GPU particles can't be bent: bend the emitter or the points before it";
   if (const Polyline* curve = input->GetCurve())
   {
      mCurveCount = curve->Count();
      pts.insert(pts.end(), curve->points.begin(), curve->points.end());
   }
   if (instancer != nullptr)
   {
      const std::vector<Mat4>& inst = ResolveInstanceTransforms(input, instancer);
      mInstanceCount = inst.size();
      for (const Mat4& t : inst)
         pts.insert(pts.end(), { t.m[12], t.m[13], t.m[14] });
   }
   const size_t count = pts.size() / 3;

   // The chain's joints: along the path, else along the axis through the
   // bounds' middle.
   const int n = std::clamp(bones, 2, 32);
   std::vector<float> chain;
   const Polyline* pathCurve = path ? path->GetCurve() : nullptr;
   if (path != nullptr && pathCurve == nullptr)
      mCookWarning = "path needs a curve: the bones run along the axis";
   if (pathCurve != nullptr && !pathCurve->Empty())
      chain = CurveOps::Resample(*pathCurve, n + 1).points;
   if (chain.size() != (size_t)(n + 1) * 3)
   {
      if (count == 0)
      {
         mStatus = "nothing to bend";
         return;
      }
      float lo[3] = { pts[0], pts[1], pts[2] }, hi[3] = { pts[0], pts[1], pts[2] };
      for (size_t i = 1; i < count; i++)
         for (int k = 0; k < 3; k++)
         {
            lo[k] = std::min(lo[k], pts[i * 3 + k]);
            hi[k] = std::max(hi[k], pts[i * 3 + k]);
         }
      const int a = std::clamp(axis, 0, 2);
      if (hi[a] - lo[a] < 1e-5f)
      {
         lo[a] -= 0.5f;
         hi[a] += 0.5f;
      }
      chain.clear();
      for (int j = 0; j <= n; j++)
         for (int k = 0; k < 3; k++)
            chain.push_back(k == a ? lo[k] + (hi[k] - lo[k]) * (float)j / (float)n : 0.5f * (lo[k] + hi[k]));
   }

   // Rest frames: each joint's Y runs down its bone. R_j is the smallest
   // turn from the previous bone's frame, so the chain doesn't spin.
   std::vector<float> rot((n + 1) * 4), len(n);
   float prev[4] = { 0, 0, 0, 1 };
   for (int j = 0; j < n; j++)
   {
      const float* a = &chain[j * 3];
      const float* b = &chain[(j + 1) * 3];
      float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
      len[j] = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      float y[3], step[4];
      Skinning::QuatRotate(prev, kUp, y);
      if (len[j] > 1e-6f)
         Skinning::QuatFromTo(y, d, step);
      else
         step[0] = step[1] = step[2] = 0.0f, step[3] = 1.0f;
      Skinning::QuatMul(step, prev, &rot[j * 4]);
      std::copy(&rot[j * 4], &rot[j * 4] + 4, prev);
   }
   std::copy(prev, prev + 4, &rot[n * 4]);

   mPkg.joints.resize(n + 1);
   for (int j = 0; j <= n; j++)
   {
      GltfImport::GltfJoint& jt = mPkg.joints[j];
      jt.name = j < n ? "bone " + std::to_string(j + 1) : "tip";
      jt.parent = j - 1;
      if (j == 0)
      {
         std::copy(chain.begin(), chain.begin() + 3, jt.t);
         std::copy(&rot[0], &rot[0] + 4, jt.r);
      }
      else
      {
         jt.t[0] = 0.0f; jt.t[1] = len[j - 1]; jt.t[2] = 0.0f;
         const float* p = &rot[(j - 1) * 4];
         const float inv[4] = { -p[0], -p[1], -p[2], p[3] };
         Skinning::QuatMul(inv, &rot[j * 4], jt.r);
      }
      const Mat4 bind = Mat4::Inverse(Frame(&rot[j * 4], &chain[j * 3]));
      std::copy(bind.m, bind.m + 16, jt.inverseBind);
   }

   // Envelope weights: a smooth falloff with distance to each bone, the four
   // strongest kept and normalised. A point no envelope reaches follows its
   // nearest bone.
   float total = 0.0f;
   for (float l : len)
      total += l;
   const float radius = std::max(1e-5f, std::max(0.05f, envelope) * total / (float)n);
   mEnv.assign(count * n, 0.0f);
   mNearest.assign(count, 0);
   mEnvBones = n;
   for (size_t i = 0; i < count; i++)
   {
      float nearestD = 1e30f;
      for (int b = 0; b < n; b++)
      {
         const float d = SegmentDistance(&pts[i * 3], &chain[b * 3], &chain[(b + 1) * 3]);
         if (d < nearestD)
         {
            nearestD = d;
            mNearest[i] = (unsigned short)b;
         }
         const float x = std::clamp(1.0f - d / radius, 0.0f, 1.0f);
         mEnv[i * n + b] = x * x * (3.0f - 2.0f * x);
      }
   }
   mHasInstances = instancer != nullptr;
   mStatus = std::to_string(n) + " bones, " + std::to_string(count) + " points";
   Reweight();
}

bool ArmatureNode::Paintable()
{
   Update();
   const size_t count = mNearest.size();
   return count > 0 && (mPaintPoints == 0 || ((size_t)mPaintPoints == count && mPaintBones == mEnvBones));
}

void ArmatureNode::Reweight()
{
   // The envelope plus the paint, the four strongest kept and normalised. A
   // point no weight reaches follows its nearest bone.
   const int n = mEnvBones;
   const size_t count = mNearest.size();
   const bool paint = mPaintPoints > 0 && (size_t)mPaintPoints == count && mPaintBones == n;
   if (mPaintPoints > 0 && !paint && mCookWarning.empty())
      mCookWarning = "weight paint was made for " + std::to_string(mPaintPoints) + " points and " +
                     std::to_string(mPaintBones) + " bones: put them back or clear the paint";
   else if ((paint || mPaintPoints == 0) && mCookWarning.rfind("weight paint", 0) == 0)
      mCookWarning.clear();
   mJoints.assign(count * 4, 0);
   mWeights.assign(count * 4, 0.0f);
   std::vector<std::pair<float, int>> w(n);
   const int keep = std::min(4, n);
   for (size_t i = 0; i < count; i++)
   {
      for (int b = 0; b < n; b++)
      {
         const float x = mEnv[i * n + b] + (paint ? mPaint[i * n + b] : 0.0f);
         w[b] = { std::clamp(x, 0.0f, 1.0f), b };
      }
      std::partial_sort(w.begin(), w.begin() + keep, w.end(),
                        [](const auto& l, const auto& r) { return l.first > r.first; });
      float sum = 0.0f;
      for (int k = 0; k < keep; k++)
         sum += w[k].first;
      if (sum <= 0.0f)
      {
         mJoints[i * 4] = mNearest[i];
         mWeights[i * 4] = 1.0f;
         continue;
      }
      for (int k = 0; k < keep; k++)
      {
         mJoints[i * 4 + k] = (unsigned short)w[k].second;
         mWeights[i * 4 + k] = w[k].first / sum;
      }
   }
}

void ArmatureNode::Paint(const std::vector<std::pair<unsigned int, float>>& hits, int bone, float target)
{
   if (!Paintable() || bone < 0 || bone >= mEnvBones || hits.empty())
      return;
   const int n = mEnvBones;
   const size_t count = mNearest.size();
   if (mPaintPoints == 0)
   {
      mPaint.assign(count * n, 0.0f);
      mPaintPoints = (int)count;
      mPaintBones = n;
   }
   for (const auto& h : hits)
   {
      if (h.first >= count)
         continue;
      const size_t at = (size_t)h.first * n + bone;
      const float cur = std::clamp(mEnv[at] + mPaint[at], 0.0f, 1.0f);
      const float next = cur + (target - cur) * std::clamp(h.second, 0.0f, 1.0f);
      mPaint[at] = next - mEnv[at];
   }
   mPaintRevision++;
   mPaintEncoded = false;
}

bool ArmatureNode::HasPaint() const
{
   return mPaintPoints > 0;
}

void ArmatureNode::ClearPaint()
{
   mPaint.clear();
   mPaintPoints = mPaintBones = 0;
   mPaintRevision++;
   mPaintEncoded = false;
}

// "W1", points, bones, scale, how many, then (point * bones + bone, int16) for
// each painted entry, base64: a few painted joints stay a short param.
std::string ArmatureNode::EncodePaint() const
{
   if (mPaintPoints == 0)
      return std::string();
   const size_t total = (size_t)mPaintPoints * mPaintBones;
   float big = 0.0f;
   for (size_t i = 0; i < total && i < mPaint.size(); i++)
      big = std::max(big, std::fabs(mPaint[i]));
   const float scale = std::max(big, 1e-6f) / 32767.0f;
   std::vector<uint8_t> b;
   auto put = [&](const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
   put("W1", 2);
   const uint32_t np = (uint32_t)mPaintPoints, nb = (uint32_t)mPaintBones;
   put(&np, 4);
   put(&nb, 4);
   put(&scale, 4);
   const size_t countAt = b.size();
   uint32_t kept = 0;
   put(&kept, 4);
   for (size_t i = 0; i < total && i < mPaint.size(); i++)
   {
      const int16_t q = (int16_t)std::clamp(std::lround(mPaint[i] / scale), -32767L, 32767L);
      if (q == 0)
         continue;
      const uint32_t idx = (uint32_t)i;
      put(&idx, 4);
      put(&q, 2);
      kept++;
   }
   std::memcpy(&b[countAt], &kept, 4);
   return Base64::Encode(b.data(), b.size());
}

void ArmatureNode::DecodePaint()
{
   mPaint.clear();
   mPaintPoints = mPaintBones = 0;
   mPaintRevision++;
   std::vector<uint8_t> b;
   if (mPaintData.empty() || !Base64::Decode(mPaintData, b) || b.size() < 18 || b[0] != 'W' || b[1] != '1')
      return;
   uint32_t np, nb, count;
   float scale;
   std::memcpy(&np, &b[2], 4);
   std::memcpy(&nb, &b[6], 4);
   std::memcpy(&scale, &b[10], 4);
   std::memcpy(&count, &b[14], 4);
   if (np == 0 || nb == 0 || nb > 32 || np > 50000000u || b.size() < 18 + (size_t)count * 6)
      return;
   const size_t total = (size_t)np * nb;
   std::vector<float> paint(total, 0.0f);
   for (uint32_t e = 0; e < count; e++)
   {
      const uint8_t* r = &b[18 + (size_t)e * 6];
      uint32_t idx;
      int16_t q;
      std::memcpy(&idx, r, 4);
      std::memcpy(&q, r + 4, 2);
      if (idx >= total)
         return;
      paint[idx] = q * scale;
   }
   mPaint = std::move(paint);
   mPaintPoints = (int)np;
   mPaintBones = (int)nb;
}

void ArmatureNode::ApplySkin(const std::vector<float>& skin)
{
   // Linear blend: each point's matrix is its weighted sum of bone matrices.
   size_t at = 0;
   auto blend = [&](size_t i, Mat4& m)
   {
      std::fill(m.m, m.m + 16, 0.0f);
      for (int k = 0; k < 4; k++)
      {
         const float wgt = mWeights[i * 4 + k];
         if (wgt <= 0.0f)
            continue;
         const float* s = &skin[mJoints[i * 4 + k] * 16];
         for (int e = 0; e < 16; e++)
            m.m[e] += wgt * s[e];
      }
   };
   auto point = [](const Mat4& m, float& x, float& y, float& z)
   {
      const float p[3] = { x, y, z };
      x = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2] + m.m[12];
      y = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2] + m.m[13];
      z = m.m[2] * p[0] + m.m[6] * p[1] + m.m[10] * p[2] + m.m[14];
   };
   auto vector = [](const Mat4& m, float& x, float& y, float& z, bool normalise)
   {
      const float p[3] = { x, y, z };
      x = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2];
      y = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2];
      z = m.m[2] * p[0] + m.m[6] * p[1] + m.m[10] * p[2];
      const float l = std::sqrt(x * x + y * y + z * z);
      if (normalise && l > 0.0f)
         x /= l, y /= l, z /= l;
   };
   Mat4 m;
   const size_t total = mMeshCount + mCloudCount + mCurveCount + mInstanceCount;
   mPosed.assign(total * 3, 0.0f);
   mPosedN.assign(total * 3, 0.0f);
   auto keep = [&](size_t i, float x, float y, float z, float nx, float ny, float nz)
   {
      float* p = &mPosed[i * 3];
      float* q = &mPosedN[i * 3];
      p[0] = x; p[1] = y; p[2] = z;
      q[0] = nx; q[1] = ny; q[2] = nz;
   };

   if (mMeshCount > 0)
   {
      mMesh = input->GetMesh();
      const bool tint = paintBone >= 0 && paintBone < mEnvBones;
      if (tint)
         mMesh.vertexColor.assign(mMesh.vertices.size() * 3, 0.0f);
      for (size_t i = 0; i < mMeshCount && i < mMesh.vertices.size(); i++)
      {
         Vertex& v = mMesh.vertices[i];
         blend(at + i, m);
         point(m, v.px, v.py, v.pz);
         vector(m, v.nx, v.ny, v.nz, true);
         keep(at + i, v.px, v.py, v.pz, v.nx, v.ny, v.nz);
         if (tint)
         {
            // Blender's weight colours: blue at none, green halfway, red at all.
            float wgt = 0.0f;
            for (int k = 0; k < 4; k++)
               if (mJoints[(at + i) * 4 + k] == paintBone)
                  wgt += mWeights[(at + i) * 4 + k];
            float* c = &mMesh.vertexColor[i * 3];
            c[0] = std::clamp(wgt * 2.0f - 1.0f, 0.0f, 1.0f);
            c[1] = 1.0f - std::fabs(wgt * 2.0f - 1.0f);
            c[2] = std::clamp(1.0f - wgt * 2.0f, 0.0f, 1.0f);
         }
      }
      mMeshRevision = NextMeshRevision();
   }
   at += mMeshCount;
   if (mCloudCount > 0)
   {
      if (const std::vector<Particle>* cloud = input->GetPointCloud())
         mCloud = *cloud;
      for (size_t i = 0; i < mCloudCount && i < mCloud.size(); i++)
      {
         Particle& p = mCloud[i];
         blend(at + i, m);
         point(m, p.px, p.py, p.pz);
         vector(m, p.nx, p.ny, p.nz, true);
         vector(m, p.vx, p.vy, p.vz, false);
         keep(at + i, p.px, p.py, p.pz, p.nx, p.ny, p.nz);
      }
      mCloudRevision = NextMeshRevision();
   }
   at += mCloudCount;
   if (mCurveCount > 0)
   {
      if (const Polyline* curve = input->GetCurve())
         mCurve = *curve;
      for (size_t i = 0; i < mCurveCount && i < mCurve.Count(); i++)
      {
         blend(at + i, m);
         point(m, mCurve.points[i * 3], mCurve.points[i * 3 + 1], mCurve.points[i * 3 + 2]);
         keep(at + i, mCurve.points[i * 3], mCurve.points[i * 3 + 1], mCurve.points[i * 3 + 2], 0.0f, 0.0f, 0.0f);
      }
      mCurveRevision = NextMeshRevision();
   }
   at += mCurveCount;
   if (mHasInstances)
   {
      const std::vector<Mat4>& inst = ResolveInstanceTransforms(input, FindInstancer(input));
      mInstances.resize(std::min(inst.size(), mInstanceCount));
      for (size_t i = 0; i < mInstances.size(); i++)
      {
         blend(at + i, m);
         mInstances[i] = Mat4::Multiply(m, inst[i]);
         keep(at + i, mInstances[i].m[12], mInstances[i].m[13], mInstances[i].m[14], 0.0f, 0.0f, 0.0f);
      }
   }
}

bool ArmatureNode::JointMatrix(int j, float out[16])
{
   Update();
   if (j < 0 || (size_t)(j + 1) * 16 > mJointWorld.size())
      return false;
   std::copy(&mJointWorld[j * 16], &mJointWorld[j * 16] + 16, out);
   return true;
}

const Mesh& ArmatureNode::GetMesh()
{
   if (input == nullptr)
      return kEmptyMesh;
   Update();
   return mMeshCount > 0 ? mMesh : input->GetMesh();
}

unsigned long long ArmatureNode::MeshRevision()
{
   if (input == nullptr)
      return 0;
   Update();
   return mMeshCount > 0 ? mMeshRevision : input->MeshRevision();
}

const std::vector<Particle>* ArmatureNode::GetPointCloud()
{
   if (input == nullptr)
      return nullptr;
   Update();
   return mCloudCount > 0 ? &mCloud : input->GetPointCloud();
}

unsigned long long ArmatureNode::PointCloudRevision()
{
   if (input == nullptr)
      return 0;
   Update();
   return mCloudCount > 0 ? mCloudRevision : input->PointCloudRevision();
}

const GpuPoints* ArmatureNode::GetGpuPoints()
{
   // A CPU cloud is bent here, so its GPU copy upstream would be the
   // unbent one; only GPU-only particles pass (unbent, with the warning).
   if (input == nullptr || input->GetPointCloud() != nullptr)
      return nullptr;
   return input->GetGpuPoints();
}

const Polyline* ArmatureNode::GetCurve()
{
   if (input == nullptr)
      return nullptr;
   Update();
   return mCurveCount > 0 ? &mCurve : input->GetCurve();
}

unsigned long long ArmatureNode::CurveStamp()
{
   if (input == nullptr)
      return 0;
   Update();
   return mCurveCount > 0 ? mCurveRevision : input->CurveStamp();
}

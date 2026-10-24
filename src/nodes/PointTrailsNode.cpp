#include "PointTrailsNode.h"
#include <algorithm>
#include <cmath>

namespace
{
   void TransformPoint(const Mat4& m, float& x, float& y, float& z)
   {
      const float px = x, py = y, pz = z;
      x = m.m[0] * px + m.m[4] * py + m.m[8] * pz + m.m[12];
      y = m.m[1] * px + m.m[5] * py + m.m[9] * pz + m.m[13];
      z = m.m[2] * px + m.m[6] * py + m.m[10] * pz + m.m[14];
   }

   float Normalize(float* v)
   {
      const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
      if (l > 1e-12f)
      {
         v[0] /= l; v[1] /= l; v[2] /= l;
      }
      return l;
   }
}

void PointTrailsNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   if (bypassed)   // the outputs pass the input through; do not sample or read back
   {
      mClear = true;
      return;
   }

   if (input == nullptr)
   {
      if (!mMesh.vertices.empty())
      {
         mMesh = Mesh();
         mMeshRevision = NextMeshRevision();
      }
      mTrails = 0;
      mFrom = nullptr;
      mClear = true;
      return;
   }

   const std::vector<Particle>* cloud = input->GetPointCloud();
   const Mesh* mesh = cloud ? nullptr : &input->GetMesh();
   const unsigned long long rev = cloud ? input->PointCloudRevision() : input->MeshRevision();
   const Mat4 model = input->GetModelMatrix();

   // The ring is sized from length and the point limit; changing either starts over.
   const int len = std::clamp(length, 2, 240);
   const int slots = std::min(std::clamp(pointLimit, 1, 100000), kMaxSamples / len);
   if (input != mFrom || len != mLength || slots != mSlots)
      mClear = true;
   if (mClear)
   {
      mLength = len;
      mSlots = slots;
      mPos.assign((size_t)slots * len * 3, 0.0f);
      mCol.assign((size_t)slots * len * 3, 0.0f);
      mSlotState.assign((size_t)slots, Slot());
      mSamples = 0;
      mFramesSinceSample = 0;
      mDirty = true;
   }

   const bool changed = mClear || input != mFrom || rev != mFromRev || model != mFromMatrix;
   if (changed)
   {
      mFrom = input;
      mFromRev = rev;
      mFromMatrix = model;
      if (mClear || ++mFramesSinceSample >= std::clamp(sampleEvery, 1, 10))
      {
         Sample(cloud, mesh, model);
         mFramesSinceSample = 0;
      }
   }
   mClear = false;

   const bool lookChanged = width != mBuiltWidth || fade != mBuiltFade || taper != mBuiltTaper ||
                            colorMode != mBuiltColorMode ||
                            !std::equal(headColor, headColor + 3, mBuiltHead) ||
                            !std::equal(tailColor, tailColor + 3, mBuiltTail);
   if (mDirty || lookChanged)
      BuildMesh();
}

void PointTrailsNode::Sample(const std::vector<Particle>* cloud, const Mesh* mesh, const Mat4& model)
{
   const size_t n = cloud ? cloud->size() : (mesh ? mesh->vertices.size() : 0);
   const bool meshColor = mesh && mesh->vertexColor.size() == mesh->vertices.size() * 3;
   const bool byId = matchMode == kMatchParticleId && cloud != nullptr;
   for (int i = 0; i < mSlots; i++)
   {
      Slot& s = mSlotState[(size_t)i];
      if ((size_t)i >= n)
      {
         s.count = 0;
         s.lastAlive = false;
         continue;
      }
      float x, y, z, r = 1.0f, g = 1.0f, b = 1.0f, age = 0.0f;
      bool alive = true;
      if (cloud)
      {
         const Particle& p = (*cloud)[(size_t)i];
         x = p.px; y = p.py; z = p.pz;
         r = p.r; g = p.g; b = p.b;
         age = p.age;
         alive = p.alive && p.scale > 0.0f;
      }
      else
      {
         const Vertex& v = mesh->vertices[(size_t)i];
         x = v.px; y = v.py; z = v.pz;
         if (meshColor)
         {
            r = mesh->vertexColor[(size_t)i * 3];
            g = mesh->vertexColor[(size_t)i * 3 + 1];
            b = mesh->vertexColor[(size_t)i * 3 + 2];
         }
      }
      if (!alive)
      {
         s.count = 0;
         s.lastAlive = false;
         continue;
      }
      // A recycled particle slot: the age went back, so this is a new particle.
      if (byId && s.count > 0 && age < s.lastAge)
         s.count = 0;
      TransformPoint(model, x, y, z);
      s.head = (s.head + 1) % mLength;
      const size_t at = ((size_t)i * mLength + (size_t)s.head) * 3;
      mPos[at] = x; mPos[at + 1] = y; mPos[at + 2] = z;
      mCol[at] = r; mCol[at + 1] = g; mCol[at + 2] = b;
      s.count = std::min(s.count + 1, mLength);
      s.lastAge = age;
      s.lastAlive = true;
   }
   mSamples++;
   mDirty = true;
}

void PointTrailsNode::BuildMesh()
{
   Mesh out;
   size_t trails = 0;
   const float w = std::clamp(width, 0.0005f, 0.2f);
   const float fd = std::clamp(fade, 0.0f, 1.0f);
   const float tp = std::clamp(taper, 0.0f, 1.0f);
   const float denom = (float)std::max(1, mLength - 1);

   for (int i = 0; i < mSlots; i++)
   {
      const Slot& s = mSlotState[(size_t)i];
      if (s.count < 2)
         continue;
      trails++;
      const unsigned int base = (unsigned int)out.vertices.size();
      auto at = [&](int k) { return ((size_t)i * mLength + (size_t)((s.head - k + mLength) % mLength)) * 3; };

      float side[3] = { 0, 0, 0 };
      float prevT[3] = { 0, 1, 0 };
      for (int k = 0; k < s.count; k++)
      {
         // Tangent from the neighbours (one-sided at the ends); a point that
         // did not move keeps the last direction.
         const float* p = &mPos[at(k)];
         const float* a = &mPos[at(std::max(0, k - 1))];
         const float* b = &mPos[at(std::min(s.count - 1, k + 1))];
         float t[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
         if (Normalize(t) < 1e-8f)
            std::copy(prevT, prevT + 3, t);
         std::copy(t, t + 3, prevT);

         // Side vector carried along the tail (parallel transport), so the
         // prism does not twist where the tail bends.
         float d = side[0] * t[0] + side[1] * t[1] + side[2] * t[2];
         float sv[3] = { side[0] - t[0] * d, side[1] - t[1] * d, side[2] - t[2] * d };
         if (k == 0 || Normalize(sv) < 1e-4f)
         {
            float up[3] = { 0, 1, 0 };
            if (std::fabs(t[1]) > 0.9f) { up[0] = 1; up[1] = 0; }
            sv[0] = t[1] * up[2] - t[2] * up[1];
            sv[1] = t[2] * up[0] - t[0] * up[2];
            sv[2] = t[0] * up[1] - t[1] * up[0];
            Normalize(sv);
         }
         std::copy(sv, sv + 3, side);
         const float o[3] = { t[1] * sv[2] - t[2] * sv[1], t[2] * sv[0] - t[0] * sv[2], t[0] * sv[1] - t[1] * sv[0] };

         const float age = (float)k / denom;
         const float radius = w * (1.0f - tp * age);
         const float dim = 1.0f - fd * age;
         float c[3];
         if (colorMode == kColorGradient)
            for (int j = 0; j < 3; j++)
               c[j] = headColor[j] + (tailColor[j] - headColor[j]) * age;
         else
            std::copy(&mCol[at(k)], &mCol[at(k)] + 3, c);

         for (int j = 0; j < 3; j++)
         {
            const float ang = 6.2831853f * (float)j / 3.0f;
            const float cs = std::cos(ang), sn = std::sin(ang);
            Vertex v;
            v.nx = sv[0] * cs + o[0] * sn; v.ny = sv[1] * cs + o[1] * sn; v.nz = sv[2] * cs + o[2] * sn;
            v.px = p[0] + v.nx * radius; v.py = p[1] + v.ny * radius; v.pz = p[2] + v.nz * radius;
            v.u = age; v.v = (float)j / 3.0f;
            out.vertices.push_back(v);
            out.vertexColor.push_back(c[0] * dim);
            out.vertexColor.push_back(c[1] * dim);
            out.vertexColor.push_back(c[2] * dim);
         }
      }
      for (int k = 0; k + 1 < s.count; k++)
      {
         const unsigned int r0 = base + (unsigned int)k * 3, r1 = r0 + 3;
         for (unsigned int j = 0; j < 3; j++)
         {
            const unsigned int j1 = (j + 1) % 3;
            out.indices.insert(out.indices.end(), { r0 + j, r0 + j1, r1 + j1, r0 + j, r1 + j1, r1 + j });
         }
      }
   }

   mMesh = std::move(out);
   mTrails = trails;
   mMeshRevision = NextMeshRevision();
   mDirty = false;
   mBuiltWidth = width;
   mBuiltFade = fade;
   mBuiltTaper = taper;
   mBuiltColorMode = colorMode;
   std::copy(headColor, headColor + 3, mBuiltHead);
   std::copy(tailColor, tailColor + 3, mBuiltTail);
}

#include "GeometryFeedbackNode.h"
#include "BenchReport.h"
#include "gl3.h"
#include <algorithm>
#include <cfloat>
#include "GLUtil.h"

namespace
{
   void TransformPoint(const Mat4& m, float& x, float& y, float& z)
   {
      const float px = x, py = y, pz = z;
      x = m.m[0] * px + m.m[4] * py + m.m[8] * pz + m.m[12];
      y = m.m[1] * px + m.m[5] * py + m.m[9] * pz + m.m[13];
      z = m.m[2] * px + m.m[6] * py + m.m[10] * pz + m.m[14];
   }
}

GeometryFeedbackNode::~GeometryFeedbackNode()
{
   GpuPointsGL::DestroyTarget(mTargets[0]);
   GpuPointsGL::DestroyTarget(mTargets[1]);
   if (mCopyProgram != 0)
      glDeleteProgram(mCopyProgram);
}

void GeometryFeedbackNode::CookIfNeeded(int frameId)
{
   // Stamped before anything upstream cooks: the loop's nodes cook their
   // input, which is this node, and must find it already cooked.
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* n = dynamic_cast<INode*>(seed))
      n->CookIfNeeded(frameId);
   IGeometrySource* loopSrc = (loop != this && !bypassed) ? loop : nullptr;
   if (auto* n = dynamic_cast<INode*>(loopSrc))
      n->CookIfNeeded(frameId);

   // Seed on the first cook, on Reset, when the seed cable moves, and every
   // cook while nothing closes the loop (a pass-through of the seed).
   const bool reseed = mNeedsSeed || seed != mSeededFrom || loopSrc == nullptr || seed == nullptr;
   if (reseed)
   {
      const bool force = mNeedsSeed || seed != mSeededFrom;
      mNeedsSeed = false;
      mSeededFrom = seed;
      mLooping = false;
      if (force)
         mGeneration = 0;
      Capture(seed, force);
      return;
   }
   mLooping = true;
   const unsigned long long meshRev = mMeshRevision, cloudRev = mCloudRevision;
   Capture(loopSrc, false);
   if (mMeshRevision != meshRev || mCloudRevision != cloudRev)
      mGeneration++;
}

void GeometryFeedbackNode::Capture(IGeometrySource* src, bool force)
{
   if (src == nullptr)
   {
      if (!mMesh.vertices.empty() || !mMesh.indices.empty())
      {
         mMesh = Mesh();
         mMeshRevision = NextMeshRevision();
      }
      if (mHasCloud)
      {
         mHasCloud = false;
         mCloud.clear();
         mCloudOnGpu = false;
         mCloudRevision = NextMeshRevision();
      }
      mMeshFrom = mCloudFrom = nullptr;
      return;
   }

   const Mat4 m = src->GetModelMatrix();
   const bool identity = m == Mat4::Identity();

   const unsigned long long meshRev = src->MeshRevision();
   if (force || src != mMeshFrom || meshRev != mMeshFromRev || m != mMeshFromMatrix)
   {
      mMeshFrom = src;
      mMeshFromRev = meshRev;
      mMeshFromMatrix = m;
      const Mesh& in = src->GetMesh();
      // Copied before assigning: `in` can be this node's own mesh when the
      // loop forwards it untouched.
      Mesh next = identity ? in : MeshOps::Transform(in, m);
      mMesh = std::move(next);
      mMeshRevision = NextMeshRevision();
   }

   const unsigned long long cloudRev = src->PointCloudRevision();
   if (force || src != mCloudFrom || cloudRev != mCloudFromRev || m != mCloudFromMatrix)
   {
      mCloudFrom = src;
      mCloudFromRev = cloudRev;
      mCloudFromMatrix = m;
      mBaseSize = src->PointBaseSize();
      // GPU first: asking a GPU-only source for its CPU cloud would read it
      // back every cook.
      const GpuPoints* g = GpuPointsGL::Enabled() ? src->GetGpuPoints() : nullptr;
      if (g != nullptr && CopyGpu(*g, m))
      {
         mHasCloud = true;
         mCloudOnGpu = true;
         mCpuStale = true;
      }
      else if (const std::vector<Particle>* c = src->GetPointCloud())
      {
         std::vector<Particle> next = *c; // `c` can be this node's own cloud
         if (!identity)
            for (Particle& p : next)
               TransformPoint(m, p.px, p.py, p.pz);
         mCloud = std::move(next);
         mHasCloud = true;
         mCloudOnGpu = false;
         mCpuStale = false;
      }
      else
      {
         mCloud.clear();
         mHasCloud = false;
         mCloudOnGpu = false;
      }
      mCloudRevision = NextMeshRevision();
      if (mCloudOnGpu)
         mGpu.revision = mCloudRevision;
   }
}

bool GeometryFeedbackNode::CopyGpu(const GpuPoints& src, const Mat4& bake)
{
   if (mCopyProgram == 0 && !mCopyTried)
   {
      mCopyTried = true;
      const std::string fs = std::string(GpuPointsGL::kPointPassHeader) +
         "uniform mat4 uBake;\n"
         "void main() {\n"
         "   LoadPoint();\n"
         "   P = (uBake * vec4(P, 1.0)).xyz;\n"
         "   outPos = vec4(P, W);\n"
         "   outColor = vec4(Cd, Life);\n"
         "}\n";
      mCopyProgram = GLUtil::CompileProgram(fs.c_str());
   }
   if (mCopyProgram == 0)
      return false;
   // Into the target not holding the current state: the loop's texels may
   // have been made from it, or be it.
   const int back = 1 - mFront;
   GpuPoints out;
   const bool ok = GpuPointsGL::RunPointPass(mTargets[back], out, src, mCopyProgram, [&]()
   {
      glUniformMatrix4fv(glGetUniformLocation(mCopyProgram, "uBake"), 1, GL_FALSE, bake.m);
   }, "Feedback3D_GpuPoints");
   if (!ok)
      return false;
   mFront = back;
   out.liveCount = src.liveCount;
   out.maxHalfSize = src.maxHalfSize;
   out.hasBounds = src.hasBounds;
   if (src.hasBounds)
   {
      // The source's bounds are before its pointMatrix; ours after it and the
      // bake, so carry the eight corners through both.
      const Mat4 full = Mat4::Multiply(bake, src.pointMatrix);
      out.lo[0] = out.lo[1] = out.lo[2] = FLT_MAX;
      out.hi[0] = out.hi[1] = out.hi[2] = -FLT_MAX;
      for (int c = 0; c < 8; c++)
      {
         float x = (c & 1) ? src.hi[0] : src.lo[0];
         float y = (c & 2) ? src.hi[1] : src.lo[1];
         float z = (c & 4) ? src.hi[2] : src.lo[2];
         TransformPoint(full, x, y, z);
         out.lo[0] = std::min(out.lo[0], x); out.hi[0] = std::max(out.hi[0], x);
         out.lo[1] = std::min(out.lo[1], y); out.hi[1] = std::max(out.hi[1], y);
         out.lo[2] = std::min(out.lo[2], z); out.hi[2] = std::max(out.hi[2], z);
      }
   }
   mGpu = out;
   mGpuValid = true;
   return true;
}

void GeometryFeedbackNode::ReadBack()
{
   mCpuStale = false;
   mCloud.clear();
   const int n = mGpu.count;
   if (n <= 0 || mGpu.posTex == 0)
      return;
   std::vector<float> pos((size_t)mGpu.texW * (size_t)mGpu.texH * 4, 0.0f);
   std::vector<float> col(pos.size(), 0.0f);
   glBindTexture(GL_TEXTURE_2D, mGpu.posTex);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pos.data());
   glBindTexture(GL_TEXTURE_2D, mGpu.colorTex);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, col.data());
   glBindTexture(GL_TEXTURE_2D, 0);
   Bench::GpuMem::NoteReadback((pos.size() + col.size()) * sizeof(float));
   mCloud.resize((size_t)n);
   const float base = mBaseSize > 0.0f ? mBaseSize : 1.0f;
   for (int k = 0; k < n; k++)
   {
      const float* q = &pos[(size_t)k * 4];
      const float* c = &col[(size_t)k * 4];
      Particle& p = mCloud[(size_t)k];
      p.px = q[0]; p.py = q[1]; p.pz = q[2];
      p.scale = q[3] / base;
      p.alive = q[3] > 0.0f;
      p.r = c[0]; p.g = c[1]; p.b = c[2];
      p.hasColor = true;
      // The texels keep the life fraction, not age and life: a fraction of a
      // one-second life reads back the same through PointLifeFraction.
      p.life = c[3] > 0.0f ? 1.0f : 0.0f;
      p.age = c[3];
   }
}

const std::vector<Particle>* GeometryFeedbackNode::GetPointCloud()
{
   if (!mHasCloud)
      return nullptr;
   if (mCloudOnGpu && mCpuStale)
      ReadBack();
   return &mCloud;
}

const GpuPoints* GeometryFeedbackNode::GetGpuPoints()
{
   if (!GpuPointsGL::Enabled() || !mHasCloud)
      return nullptr;
   if (mCloudOnGpu)
      return mGpuValid ? &mGpu : nullptr;
   if (mGpuUploadedRev != mCloudRevision)
   {
      mGpuUploadedRev = mCloudRevision;
      mGpuValid = GpuPointsGL::Upload(mTargets[mFront], mGpu, mCloud, mBaseSize, "Feedback3D_Upload");
      mGpu.revision = mCloudRevision;
   }
   return mGpuValid ? &mGpu : nullptr;
}

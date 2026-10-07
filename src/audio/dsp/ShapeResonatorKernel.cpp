#include "ShapeResonatorKernel.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "nodes/AudioEffectNode.h"
#include "nodes/Geometry3DNodes.h"

namespace
{
   // Plate shown until a geometry is cabled: a 1.5 x 1 rectangle. Not a square -
   // a square's modes come in degenerate pairs, which the solver may return as
   // any mix of the pair, so its mode shapes would depend on solver noise.
   void BuildDefaultPlate(std::vector<float>& pos, std::vector<uint32_t>& idx)
   {
      const int nx = 30, ny = 20;
      for (int j = 0; j <= ny; j++)
         for (int i = 0; i <= nx; i++)
         {
            pos.push_back(1.5f * (float)i / nx);
            pos.push_back((float)j / ny);
            pos.push_back(0.0f);
         }
      for (int j = 0; j < ny; j++)
         for (int i = 0; i < nx; i++)
         {
            const uint32_t v = (uint32_t)(j * (nx + 1) + i);
            idx.insert(idx.end(), { v, v + 1, v + nx + 2, v, v + nx + 2, v + nx + 1 });
         }
   }
}

ShapeResonatorKernel::ShapeResonatorKernel() = default;

ShapeResonatorKernel::~ShapeResonatorKernel()
{
   if (mWorker.joinable())
      mWorker.join();
}

void ShapeResonatorKernel::PrepareToPlay(double sampleRate, int)
{
   mSampleRate = sampleRate;
   Reset();
}

void ShapeResonatorKernel::Reset()
{
   for (int c = 0; c < kMaxChannels; c++)
      for (int m = 0; m < kMaxModes; m++)
         mY1[c][m] = mY2[c][m] = 0.0f;
}

void ShapeResonatorKernel::StartSolve(Kind kind, bool closed, std::vector<float> positions, std::vector<uint32_t> indices)
{
   mJobDone.store(false, std::memory_order_relaxed);
   mJobRunning = true;
   mLastSolveStart = std::chrono::steady_clock::now();
   mWorker = std::thread([this, kind, closed, p = std::move(positions), i = std::move(indices)]() {
      const int nv = (int)(p.size() / 3);
      switch (kind)
      {
         case Kind::kCloud:
            mJobResult = MeshModalSolver::SolveCloud(p.data(), nv, kMaxModes);
            break;
         case Kind::kCurve:
            mJobResult = MeshModalSolver::SolveCurve(p.data(), nv, closed, kMaxModes);
            break;
         default:
            mJobResult = MeshModalSolver::Solve(p.data(), nv, i.data(), (int)i.size(), kMaxModes);
            break;
      }
      mJobDone.store(true, std::memory_order_release);
   });
}

void ShapeResonatorKernel::CollectSolve()
{
   if (!mJobRunning || !mJobDone.load(std::memory_order_acquire))
      return;
   mWorker.join();
   mJobRunning = false;
   if (mJobResult.valid)
   {
      mModes = mJobResult;
      mSolvedGeo = mJobGeo;
      mSolvedRev = mJobRev;
   }
   else
   {
      // Unusable input (empty, degenerate, NaN): fall silent rather than keep ringing the previous shape, and
      // remember the revision so we do not retry every cook.
      mModes = MeshModalSolver::Modes();
      mSolvedGeo = mJobGeo;
      mSolvedRev = mJobRev;
   }
}

void ShapeResonatorKernel::Publish(const Coeffs& c)
{
   mBuf[mWriteIdx] = c;
   mWriteIdx = mMailbox.exchange(mWriteIdx | 4, std::memory_order_acq_rel) & 3;
}

void ShapeResonatorKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   CollectSolve();

   IGeometrySource* geo = node.geometry;
   const void* geoKey = geo;
   unsigned long long rev = 0ull;
   if (geo)
      rev = geo->MeshRevision() * 1000003ull ^ geo->PointCloudRevision() * 998244353ull ^ geo->CurveStamp();
   const bool stale = !mHaveRequest || geoKey != mSolvedGeo || rev != mSolvedRev;
   const bool alreadyQueued = mJobRunning && geoKey == mJobGeo && rev == mJobRev;
   // A new source (or the first solve) goes straight away; only a revision change on the source we already
   // solved waits out the throttle.
   const bool sameSource = mHaveRequest && geoKey == mSolvedGeo;
   const bool throttled = sameSource &&
      std::chrono::steady_clock::now() - mLastSolveStart < std::chrono::milliseconds(400);
   if (stale && !mJobRunning && !alreadyQueued && !throttled)
   {
      std::vector<float> pos;
      std::vector<uint32_t> idx;
      Kind kind = Kind::kMesh;
      bool closed = false;
      if (geo)
      {
         const Mesh& mesh = geo->GetMesh();
         const std::vector<Particle>* cloud = geo->GetPointCloud();
         const Polyline* curve = geo->GetCurve();
         if (!mesh.indices.empty())
         {
            pos.reserve(mesh.vertices.size() * 3);
            for (const Vertex& v : mesh.vertices)
               pos.insert(pos.end(), { v.px, v.py, v.pz });
            idx.assign(mesh.indices.begin(), mesh.indices.end());
         }
         else if (cloud && !cloud->empty())
         {
            kind = Kind::kCloud;
            for (const Particle& pt : *cloud)
               if (pt.alive)
                  pos.insert(pos.end(), { pt.px, pt.py, pt.pz });
         }
         else if (curve && !curve->Empty())
         {
            kind = Kind::kCurve;
            closed = curve->closed;
            pos = curve->points;
         }
         else if (!mesh.vertices.empty())
         {
            kind = Kind::kCloud; // vertices with no faces (Points to Vertices)
            for (const Vertex& v : mesh.vertices)
               pos.insert(pos.end(), { v.px, v.py, v.pz });
         }
      }
      else
         BuildDefaultPlate(pos, idx);
      mHaveRequest = true;
      mJobGeo = geoKey;
      mJobRev = rev;
      StartSolve(kind, closed, std::move(pos), std::move(idx));
   }

   // Turn modes + params into resonator coefficients (main thread, cheap).
   const float tune = std::clamp(node.Param("tune"), 20.0f, 2000.0f);
   const float decay = std::clamp(node.Param("decay"), 0.02f, 20.0f);
   const float damping = std::clamp(node.Param("damping"), 0.0f, 1.0f);
   const float pos = std::clamp(node.Param("pos"), 0.0f, 1.0f);
   const int wantModes = std::clamp((int)std::lround(node.Param("modes")), 1, kMaxModes);

   Coeffs c;
   mFreqCount = 0;
   if (mModes.valid && mModes.count > 0)
   {
      const float lambda0 = mModes.lambda[0];
      const float nyq = (float)(0.45 * sampleRate);
      const int pickIn = std::clamp((int)(pos * MeshModalSolver::kPickPoints), 0, MeshModalSolver::kPickPoints - 1);
      const int pickOut = MeshModalSolver::kPickPoints - 1 - pickIn;
      float norm = 0.0f;
      for (int i = 0; i < std::min(wantModes, mModes.count); i++)
      {
         const float f = tune * std::sqrt(mModes.lambda[i] / lambda0);
         if (f >= nyq)
            break;
         const float w = 6.28318530718f * f / (float)sampleRate;
         const float t60 = decay * std::pow(tune / f, damping * 1.5f);
         const float r = std::exp(-6.9078f / (t60 * (float)sampleRate));
         const int k = c.n++;
         c.a1[k] = 2.0f * r * std::cos(w);
         c.a2[k] = r * r;
         // sqrt(1-r): impulse and noise excitation ring at a level that does
         // not collapse as decay grows (a flat (1-r) gain is silent for a hit).
         c.bIn[k] = std::sqrt(1.0f - r) * mModes.shape[i][pickIn];
         c.gOut[k] = mModes.shape[i][pickOut];
         mFreqs[k] = f;
         norm += c.gOut[k] * c.gOut[k];
      }
      mFreqCount = c.n;
      if (c.n > 0)
      {
         const float g = 1.0f / std::sqrt(std::max(norm, 1e-6f));
         for (int k = 0; k < c.n; k++)
            c.gOut[k] *= g;
      }
   }
   Publish(c);
}

int ShapeResonatorKernel::ModeFrequencies(float* out, int maxCount) const
{
   const int n = std::min(mFreqCount, maxCount);
   for (int i = 0; i < n; i++)
      out[i] = mFreqs[i];
   return n;
}

void ShapeResonatorKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer*, AudioBuffer& out)
{
   if (mMailbox.load(std::memory_order_relaxed) & 4)
   {
      mReadIdx = mMailbox.exchange(mReadIdx, std::memory_order_acq_rel) & 3;
      mTarget = mBuf[mReadIdx];
      if (mCur.n == 0)
         mCur = mTarget; // nothing ringing yet: start at the target, no glide
   }

   // Glide the live coefficients toward the target once per block so a
   // tune/pos sweep or a new mesh does not click.
   const int n = std::max(mCur.n, mTarget.n);
   for (int k = 0; k < n; k++)
   {
      const bool tIn = k < mTarget.n;
      const float tb = tIn ? mTarget.bIn[k] : 0.0f;
      const float ta1 = tIn ? mTarget.a1[k] : mCur.a1[k];
      const float ta2 = tIn ? mTarget.a2[k] : mCur.a2[k];
      const float tg = tIn ? mTarget.gOut[k] : 0.0f;
      mCur.bIn[k] += (tb - mCur.bIn[k]) * 0.35f;
      mCur.a1[k] += (ta1 - mCur.a1[k]) * 0.35f;
      mCur.a2[k] += (ta2 - mCur.a2[k]) * 0.35f;
      mCur.gOut[k] += (tg - mCur.gOut[k]) * 0.35f;
   }
   mCur.n = n;
   if (mCur.n > 0 && mTarget.n < mCur.n && mCur.gOut[mCur.n - 1] * mCur.gOut[mCur.n - 1] < 1e-10f)
      mCur.n = std::max(mTarget.n, mCur.n - 1);

   const int channels = std::min(in.numChannels, kMaxChannels);
   for (int ch = 0; ch < out.numChannels; ch++)
   {
      const int src = std::min(ch, channels - 1);
      const float* x = in.channels[src];
      float* y = out.channels[ch];
      const int cs = std::min(ch, kMaxChannels - 1);
      float* y1 = mY1[cs];
      float* y2 = mY2[cs];
      for (int s = 0; s < out.numFrames; s++)
      {
         const float xs = x[s];
         float sum = 0.0f;
         for (int k = 0; k < mCur.n; k++)
         {
            const float v = mCur.bIn[k] * xs + mCur.a1[k] * y1[k] - mCur.a2[k] * y2[k];
            y2[k] = y1[k];
            y1[k] = v;
            sum += mCur.gOut[k] * v;
         }
         // A tone sitting on a long-ringing mode can build large; keep the wet
         // path bounded (unity gain for small signals).
         y[s] = std::tanh(sum);
      }
   }
}

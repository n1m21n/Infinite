#include "ShapeResonatorKernel.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioBuffer.h"
#include "audio/DspMath.h"
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
         mRe[c][m] = mIm[c][m] = 0.0f;
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
      const float rateRef = std::sqrt(48000.0f / (float)sampleRate);
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
         c.c[k] = r * std::cos(w);
         c.s[k] = r * std::sin(w);
         // The impulse response is bIn r^(n+1) sin((n+1) w), whose sum of h^2
         // is about bIn^2 / (4 (1-r)); 2 sqrt(1-r) makes it unity white-noise
         // power gain per mode. So every mode answers a
         // broadband hit equally (no 1/f tilt), a hit carries the same energy
         // whatever the decay (longer decay = quieter but longer ring), and with
         // unit-RMS shapes and sum(gOut^2) = 1 broadband in ~= broadband out.
         // Referenced to 48 kHz: real input has a fixed spectral density, not a
         // fixed per-sample power, so without rateRef a mode would ring 3 dB
         // louder each time the rate doubles.
         c.bIn[k] = 2.0f * std::sqrt(1.0f - r) * rateRef * mModes.shape[i][pickIn];
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

   // Glide the live coefficients toward the target with a ~20 ms one-pole,
   // ramped per sample inside the block, so a tune/pos sweep or a new mesh
   // never steps. With the coupled form a moving tune bends a ringing mode's
   // pitch without rescaling it; a per-block step (the old 0.35 per block)
   // still read as a +15..19 dB transient in FXAUDIT's click test.
   const int frames = std::max(1, out.numFrames);
   const float k = 1.0f - std::exp(-(float)frames / (0.02f * (float)mSampleRate));
   const Coeffs from = mCur;
   const int n = std::max(mCur.n, mTarget.n);
   for (int m = 0; m < n; m++)
   {
      const bool tIn = m < mTarget.n;
      const float tb = tIn ? mTarget.bIn[m] : 0.0f;
      const float tc = tIn ? mTarget.c[m] : mCur.c[m];
      const float ts = tIn ? mTarget.s[m] : mCur.s[m];
      const float tg = tIn ? mTarget.gOut[m] : 0.0f;
      if (m >= from.n)
      {
         // A mode joining: at its pitch from the start, fading in.
         mCur.c[m] = tc;
         mCur.s[m] = ts;
         mCur.bIn[m] = mCur.gOut[m] = 0.0f;
      }
      mCur.bIn[m] += (tb - mCur.bIn[m]) * k;
      // A chord between two points of the circle is inside it, so the glide
      // only ever damps slightly: always stable.
      mCur.c[m] += (tc - mCur.c[m]) * k;
      mCur.s[m] += (ts - mCur.s[m]) * k;
      mCur.gOut[m] += (tg - mCur.gOut[m]) * k;
   }
   mCur.n = n;
   const float invFrames = 1.0f / (float)frames;

   const int channels = std::min(in.numChannels, kMaxChannels);
   for (int ch = 0; ch < out.numChannels; ch++)
   {
      const int src = std::min(ch, channels - 1);
      const float* x = in.channels[src];
      float* y = out.channels[ch];
      const int cs = std::min(ch, kMaxChannels - 1);
      float* re = mRe[cs];
      float* im = mIm[cs];
      Coeffs cur = from, step;
      for (int m = 0; m < n; m++)
      {
         if (m >= from.n)
         {
            cur.c[m] = mCur.c[m];
            cur.s[m] = mCur.s[m];
            cur.bIn[m] = cur.gOut[m] = 0.0f;
         }
         step.bIn[m] = (mCur.bIn[m] - cur.bIn[m]) * invFrames;
         step.c[m] = (mCur.c[m] - cur.c[m]) * invFrames;
         step.s[m] = (mCur.s[m] - cur.s[m]) * invFrames;
         step.gOut[m] = (mCur.gOut[m] - cur.gOut[m]) * invFrames;
      }
      for (int s = 0; s < out.numFrames; s++)
      {
         const float xs = x[s];
         float sum = 0.0f;
         for (int m = 0; m < n; m++)
         {
            cur.bIn[m] += step.bIn[m];
            cur.c[m] += step.c[m];
            cur.s[m] += step.s[m];
            cur.gOut[m] += step.gOut[m];
            const float t = re[m] + cur.bIn[m] * xs;
            re[m] = cur.c[m] * t - cur.s[m] * im[m];
            im[m] = cur.s[m] * t + cur.c[m] * im[m];
            sum += cur.gOut[m] * im[m];
         }
         // Safety clip only: linear below the knee, so normal hits pass clean;
         // a sustained tone parked on a long-ringing mode still builds (a
         // resonator's steady-state gain is ~1/sqrt(1-r)) and is bounded here.
         y[s] = DspMath::SoftCeiling(sum);
      }
   }

   // Drop trailing modes that have faded out after the mode count fell.
   while (mCur.n > mTarget.n && mCur.gOut[mCur.n - 1] * mCur.gOut[mCur.n - 1] < 1e-10f)
   {
      mCur.n--;
      for (int c = 0; c < kMaxChannels; c++)
         mRe[c][mCur.n] = mIm[c][mCur.n] = 0.0f;
   }
}

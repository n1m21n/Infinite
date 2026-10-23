#include "SimulationNodes.h"

#include "gl3.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <thread>

#include "Transport.h"
#include "GeometryOpNodes.h"
#include "BenchReport.h"
#include "GLUtil.h"

namespace
{
   const std::vector<std::string> kEmitShapeNames = { "Point", "Sphere", "Box", "Disc" };

   // 120Hz. Fast enough that gravity and drag integrate cleanly, slow enough
   // that a heavy patch is not spending all its time here.
   const float kFixedStep = 1.0f / 120.0f;

   // A frame that stalls (window drag, file dialog, a slow first compile) would
   // otherwise hand the accumulator a huge dt and make the simulation run a
   // thousand steps at once to catch up. Time is dropped instead.
   const float kMaxCatchUp = 0.25f;
}

const std::vector<std::string>& ParticleSystemNode::EmitShapeNames() { return kEmitShapeNames; }

namespace
{
   // xorshift rather than rand(): deterministic, self-contained, and it does not
   // disturb any other caller's global random sequence.
   inline float NextFloat(unsigned int& state)
   {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      return (float)(state & 0xFFFFFF) / (float)0xFFFFFF;
   }

   inline float NextSigned(unsigned int& state)
   {
      return NextFloat(state) * 2.0f - 1.0f;
   }

   // Cheap value noise, used for turbulence. Not smooth enough to look like
   // curl noise up close, but at particle scale the difference does not read.
   inline float Noise3(float x, float y, float z)
   {
      const float v = std::sin(x * 12.9898f + y * 78.233f + z * 37.719f) * 43758.5453f;
      return (v - std::floor(v)) * 2.0f - 1.0f;
   }
}

void ParticleSystemNode::Reset()
{
   mParticles.clear();
   mAliveCount = 0;
   mAccumulator = 0.0f;
   mEmitCarry = 0.0f;
   mLastBeats = -1.0;
   // Reseeded from the parameter so a rewind reproduces the same run exactly.
   // fmod keeps every non-negative seed under 4.29M on its old stream; a
   // negative or huge seed used to be an undefined float->unsigned cast.
   mRandState = (unsigned int)std::fmod(std::fabs((double)seed) * 1000.0, 4294967296.0) + 1u;
   // The GPU side restarts at the next cook (Reset may run off the GL thread).
   mBirths = 0;
   mGpuClearPending = true;
   mLiveCount = 0;
   mGpuPoints.liveCount = 0;
   mBoundsValid = false;
   mBoundsPending = false;   // a reduction still in flight is from the old run
   mSimSeconds = 0.0;
   mResets++;                // ... and so is a count in flight
   mCountStale = true;
   mRevision = NextMeshRevision();
   mCpuRevision = mRevision;
   mGpuPoints.revision = mRevision;
}

int ParticleSystemNode::Cap() const
{
   return std::max(0, std::min(maxParticles, mGpuActive ? kGpuMaxParticles : kCpuMaxParticles));
}

size_t ParticleSystemNode::AliveCount() const
{
   return mGpuActive ? (size_t)std::max(0LL, mLiveCount) : mAliveCount;
}

const GpuPoints* ParticleSystemNode::GetGpuPoints()
{
   return (bypassed || !mGpuActive) ? nullptr : &mGpuPoints;
}

void ParticleSystemNode::BirthShape(unsigned int& rng, float pos[3], float nrm[3]) const
{
   pos[0] = pos[1] = pos[2] = 0.0f;
   nrm[0] = nrm[1] = nrm[2] = 0.0f;
   switch (emitShape)
   {
      case kSphere:
      {
         // Rejection-free direction, then a cube-root radius so the interior
         // fills evenly instead of clumping at the centre.
         const float u = NextSigned(rng);
         const float theta = NextFloat(rng) * 6.28318530718f;
         const float r = std::cbrt(NextFloat(rng)) * emitRadius;
         const float s = std::sqrt(std::max(0.0f, 1.0f - u * u));
         pos[0] = r * s * std::cos(theta);
         pos[1] = r * u;
         pos[2] = r * s * std::sin(theta);
         nrm[0] = s * std::cos(theta); nrm[1] = u; nrm[2] = s * std::sin(theta);
         break;
      }
      case kBox:
      {
         pos[0] = NextSigned(rng) * emitRadius;
         pos[1] = NextSigned(rng) * emitRadius;
         pos[2] = NextSigned(rng) * emitRadius;
         const float len = std::sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);
         if (len > 1e-6f)
            for (int k = 0; k < 3; k++)
               nrm[k] = pos[k] / len;
         break;
      }
      case kDisc:
      {
         const float theta = NextFloat(rng) * 6.28318530718f;
         const float r = std::sqrt(NextFloat(rng)) * emitRadius;
         pos[0] = r * std::cos(theta);
         pos[1] = 0.0f;
         pos[2] = r * std::sin(theta);
         nrm[1] = 1.0f;
         break;
      }
      case kPoint:
      default:
         break; // the launch direction stands in (Emit)
   }
}

void ParticleSystemNode::Emit(int count)
{
   const int cap = Cap();
   const bool fromInput = EmittingFromInput();
   // Slots below the cursor are known to be alive, so each particle takes the
   // lowest dead slot exactly as a scan from zero would, without rescanning.
   size_t freeHint = 0;
   for (int i = 0; i < count; i++)
   {
      Particle p;

      // Spawn position by emitter shape, or a birth point off the emit pin.
      // The shape path draws from the generator exactly as it always has, so
      // a patch saved before the emit pin keeps its run.
      float nrm[3];
      const EmitSample* sample = nullptr;
      if (fromInput)
      {
         const size_t n = mEmitSamples.size();
         sample = &mEmitSamples[std::min(n - 1, (size_t)(NextFloat(mRandState) * (float)n))];
         p.px = sample->p[0]; p.py = sample->p[1]; p.pz = sample->p[2];
         nrm[0] = sample->n[0]; nrm[1] = sample->n[1]; nrm[2] = sample->n[2];
      }
      else
      {
         float pos[3];
         BirthShape(mRandState, pos, nrm);
         p.px = pos[0]; p.py = pos[1]; p.pz = pos[2];
      }

      // Velocity: the emitter direction, blended toward a random direction by
      // spread, so spread 0 is a beam and 1 is omnidirectional.
      float dir[3] = { dirX, dirY, dirZ };
      const float dirLen = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
      if (dirLen > 1e-5f) { dir[0] /= dirLen; dir[1] /= dirLen; dir[2] /= dirLen; }
      else { dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f; }
      if (!fromInput && emitShape == kPoint)
         for (int k = 0; k < 3; k++)
            nrm[k] = dir[k];

      float rnd[3] = { NextSigned(mRandState), NextSigned(mRandState), NextSigned(mRandState) };
      const float rndLen = std::sqrt(rnd[0]*rnd[0] + rnd[1]*rnd[1] + rnd[2]*rnd[2]);
      if (rndLen > 1e-5f) { rnd[0] /= rndLen; rnd[1] /= rndLen; rnd[2] /= rndLen; }

      const float blend = std::max(0.0f, std::min(spread, 1.0f));
      float vel[3] = {
         dir[0] * (1.0f - blend) + rnd[0] * blend,
         dir[1] * (1.0f - blend) + rnd[1] * blend,
         dir[2] * (1.0f - blend) + rnd[2] * blend
      };
      const float velLen = std::sqrt(vel[0]*vel[0] + vel[1]*vel[1] + vel[2]*vel[2]);
      if (velLen > 1e-5f) { vel[0] /= velLen; vel[1] /= velLen; vel[2] /= velLen; }

      const float speed = initialSpeed * (1.0f + NextSigned(mRandState) * speedRandom);
      p.vx = vel[0] * speed + nrm[0] * speedAlongNormal;
      p.vy = vel[1] * speed + nrm[1] * speedAlongNormal;
      p.vz = vel[2] * speed + nrm[2] * speedAlongNormal;

      p.life = std::max(0.05f, lifetime * (1.0f + NextSigned(mRandState) * lifetimeRandom));
      p.age = 0.0f;
      p.scale = startSize;
      if (sample != nullptr && copySourceColor)
      {
         p.r = sample->c[0]; p.g = sample->c[1]; p.b = sample->c[2];
      }
      else
      {
         p.r = startColor[0]; p.g = startColor[1]; p.b = startColor[2];
      }
      p.hasColor = true;
      p.alive = true;

      // Reuse a dead slot before growing: at steady state the array stops
      // reallocating entirely.
      bool placed = false;
      for (size_t s = freeHint; s < mParticles.size(); s++)
      {
         if (!mParticles[s].alive)
         {
            mParticles[s] = p;
            placed = true;
            freeHint = s + 1;
            break;
         }
      }
      if (!placed)
         freeHint = mParticles.size();
      if (!placed)
      {
         if ((int)mParticles.size() >= cap)
            return; // at capacity and nothing recyclable; drop the rest
         mParticles.push_back(p);
      }
   }
}

int ParticleSystemNode::sMaxThreads = 0;

void ParticleSystemNode::StepRange(size_t begin, size_t end, float dt)
{
   const float invMass = 1.0f / std::max(0.1f, mass);
   const float dragFactor = std::max(0.0f, 1.0f - drag * invMass * dt);
   const float turb = turbulence * invMass;
   // Emitting from geometry with its colour: the colour set at birth stays.
   const bool keepColor = copySourceColor && EmittingFromInput();
   const float sCurve = std::max(0.05f, sizeCurve);
   const float cCurve = std::max(0.05f, colorCurve);

   for (size_t i = begin; i < end; i++)
   {
      Particle& p = mParticles[i];
      if (!p.alive)
         continue;

      p.age += dt;
      if (p.life > 0.0f && p.age >= p.life)
      {
         p.alive = false;
         continue;
      }

      p.vx += gravityX * dt;
      p.vy += gravityY * dt;
      p.vz += gravityZ * dt;

      if (turb > 0.0f)
      {
         const float s = turbulenceScale;
         p.vx += Noise3(p.px * s, p.py * s, p.pz * s) * turb * dt;
         p.vy += Noise3(p.py * s + 31.4f, p.pz * s, p.px * s) * turb * dt;
         p.vz += Noise3(p.pz * s, p.px * s + 17.1f, p.py * s) * turb * dt;
      }

      p.vx *= dragFactor;
      p.vy *= dragFactor;
      p.vz *= dragFactor;

      p.px += p.vx * dt;
      p.py += p.vy * dt;
      p.pz += p.vz * dt;

      const float t = (p.life > 0.0f) ? std::min(1.0f, p.age / p.life) : 0.0f;
      const float ts = sCurve == 1.0f ? t : std::pow(t, sCurve);
      p.scale = startSize + (endSize - startSize) * ts;
      if (!keepColor)
      {
         const float tc = cCurve == 1.0f ? t : std::pow(t, cCurve);
         p.r = startColor[0] + (endColor[0] - startColor[0]) * tc;
         p.g = startColor[1] + (endColor[1] - startColor[1]) * tc;
         p.b = startColor[2] + (endColor[2] - startColor[2]) * tc;
      }
   }
}

// Every particle moves on its own (the turbulence reads only that particle's
// position), so slices of the array can step on separate threads and the result
// is the same as one pass.
void ParticleSystemNode::Step(float dt)
{
   const size_t n = mParticles.size();
   constexpr size_t kMinPerThread = 16384;
   const unsigned hw = sMaxThreads > 0 ? (unsigned)sMaxThreads : std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
   const size_t threads = std::min<size_t>(hw, n / kMinPerThread);
   if (threads < 2)
   {
      StepRange(0, n, dt);
      return;
   }
   const size_t chunk = (n + threads - 1) / threads;
   std::vector<std::thread> workers;
   workers.reserve(threads - 1);
   for (size_t t = 1; t < threads; t++)
      workers.emplace_back([this, t, chunk, n, dt]() { StepRange(t * chunk, std::min(n, (t + 1) * chunk), dt); });
   StepRange(0, std::min(n, chunk), dt);
   for (std::thread& w : workers)
      w.join();
}

// Birth points from the emit pin. GPU points are sampled in the shader where
// they are; anything else becomes a table of world-space samples, rebuilt only
// when the geometry, its revision or its placement changes.
void ParticleSystemNode::PullEmitSamples()
{
   mEmitGpu = nullptr;
   mCookWarning.clear();
   if (!emitFromInput || emitSource == nullptr)
      return;

   if (mGpuActive)
   {
      if (const GpuPoints* gp = emitSource->GetGpuPoints())
      {
         if (gp->count > 0)
         {
            mEmitGpu = gp;
            mEmitGpuModel = Mat4::Multiply(emitSource->GetModelMatrix(), gp->pointMatrix);
            if (gp->hasBounds)
            {
               for (int k = 0; k < 3; k++) { mEmitLo[k] = 1e30f; mEmitHi[k] = -1e30f; }
               for (int corner = 0; corner < 8; corner++)
               {
                  const float c[3] = { (corner & 1) ? gp->hi[0] : gp->lo[0],
                                       (corner & 2) ? gp->hi[1] : gp->lo[1],
                                       (corner & 4) ? gp->hi[2] : gp->lo[2] };
                  for (int k = 0; k < 3; k++)
                  {
                     const float w = mEmitGpuModel.m[k] * c[0] + mEmitGpuModel.m[4 + k] * c[1] +
                                     mEmitGpuModel.m[8 + k] * c[2] + mEmitGpuModel.m[12 + k];
                     mEmitLo[k] = std::min(mEmitLo[k], w);
                     mEmitHi[k] = std::max(mEmitHi[k], w);
                  }
               }
               mEmitHasBounds = true;
            }
            else
            {
               mEmitHasBounds = false;
            }
            return;
         }
      }
   }

   const std::vector<Particle>* cloud = emitSource->GetPointCloud();
   const bool useCloud = cloud != nullptr && !cloud->empty();
   InstanceOnPointsNode* instancer = useCloud ? nullptr : FindInstancer(emitSource);
   const unsigned long long revision = useCloud ? emitSource->PointCloudRevision() : emitSource->MeshRevision();
   const Mat4 model = emitSource->GetModelMatrix();
   if (mEmitSamplesFrom == (const void*)emitSource && mEmitSamplesRevision == revision &&
       mEmitSamplesModel == model && mEmitSamplesCloud == useCloud &&
       (!instancer || mEmitSamplesInstances == ResolveInstanceTransforms(emitSource, instancer).size()))
   {
      if (mEmitSamples.empty())
         mCookWarning = "Nothing to emit from: the emit pin's geometry is empty";
      return;
   }
   mEmitSamplesFrom = emitSource;
   mEmitSamplesRevision = revision;
   mEmitSamplesModel = model;
   mEmitSamplesCloud = useCloud;
   mEmitSamplesInstances = instancer ? ResolveInstanceTransforms(emitSource, instancer).size() : 0;
   mEmitSamplesStamp++;
   mEmitSamples.clear();

   const Material material = emitSource->GetMaterial();
   auto toWorld = [&model](const float in[3], float out[3])
   {
      for (int k = 0; k < 3; k++)
         out[k] = model.m[k] * in[0] + model.m[4 + k] * in[1] + model.m[8 + k] * in[2] + model.m[12 + k];
   };
   auto toWorldNormal = [&model](const float in[3], float out[3])
   {
      float len = 0.0f;
      for (int k = 0; k < 3; k++)
      {
         out[k] = model.m[k] * in[0] + model.m[4 + k] * in[1] + model.m[8 + k] * in[2];
         len += out[k] * out[k];
      }
      len = std::sqrt(len);
      if (len > 1e-8f)
         for (int k = 0; k < 3; k++)
            out[k] /= len;
   };

   if (useCloud)
   {
      // Every alive point, thinned evenly past the table's size.
      constexpr size_t kMaxCloudSamples = 262144;
      size_t alive = 0;
      for (const Particle& q : *cloud)
         if (q.alive)
            alive++;
      const size_t stride = std::max<size_t>(1, (alive + kMaxCloudSamples - 1) / kMaxCloudSamples);
      mEmitSamples.reserve(std::min(alive, kMaxCloudSamples));
      size_t seen = 0;
      for (const Particle& q : *cloud)
      {
         if (!q.alive || (seen++ % stride) != 0)
            continue;
         EmitSample s;
         const float p[3] = { q.px, q.py, q.pz };
         const float n[3] = { q.nx, q.ny, q.nz };
         toWorld(p, s.p);
         toWorldNormal(n, s.n);
         s.c[0] = q.hasColor ? q.r : material.color[0];
         s.c[1] = q.hasColor ? q.g : material.color[1];
         s.c[2] = q.hasColor ? q.b : material.color[2];
         mEmitSamples.push_back(s);
      }
   }
   else
   {
      Mesh realized;
      if (instancer != nullptr)
         realized = MeshOps::RealizeInstances(emitSource->GetMesh(), ResolveInstanceTransforms(emitSource, instancer),
                                              emitSource->GetInstanceGroupMatrix(), &instancer->InstanceColors(), 256);
      const Mesh& mesh = instancer != nullptr ? realized : emitSource->GetMesh();
      if (mesh.indices.size() >= 3)
      {
         // Area-weighted points on the surface: a low-poly mesh is covered,
         // not just its corners. A fixed generator, so the same mesh always
         // gives the same table and a rewind replays.
         std::vector<double> cumulative;
         cumulative.reserve(mesh.indices.size() / 3);
         double total = 0.0;
         for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
         {
            const Vertex& a = mesh.vertices[mesh.indices[t]];
            const Vertex& b = mesh.vertices[mesh.indices[t + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[t + 2]];
            const float e1[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz };
            const float e2[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
            const float cx = e1[1] * e2[2] - e1[2] * e2[1];
            const float cy = e1[2] * e2[0] - e1[0] * e2[2];
            const float cz = e1[0] * e2[1] - e1[1] * e2[0];
            total += 0.5 * std::sqrt((double)cx * cx + (double)cy * cy + (double)cz * cz);
            cumulative.push_back(total);
         }
         if (total > 0.0)
         {
            constexpr int kMeshSamples = 65536;
            unsigned int rng = 0x9E3779B9u;
            mEmitSamples.reserve(kMeshSamples);
            for (int i = 0; i < kMeshSamples; i++)
            {
               const double pick = (double)NextFloat(rng) * total;
               const size_t tri = std::min(cumulative.size() - 1,
                                           (size_t)(std::lower_bound(cumulative.begin(), cumulative.end(), pick) - cumulative.begin()));
               float u = NextFloat(rng), v = NextFloat(rng);
               if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
               const float w0 = 1.0f - u - v;
               const Vertex& a = mesh.vertices[mesh.indices[tri * 3]];
               const Vertex& b = mesh.vertices[mesh.indices[tri * 3 + 1]];
               const Vertex& c = mesh.vertices[mesh.indices[tri * 3 + 2]];
               const float p[3] = { w0 * a.px + u * b.px + v * c.px, w0 * a.py + u * b.py + v * c.py,
                                    w0 * a.pz + u * b.pz + v * c.pz };
               const float n[3] = { w0 * a.nx + u * b.nx + v * c.nx, w0 * a.ny + u * b.ny + v * c.ny,
                                    w0 * a.nz + u * b.nz + v * c.nz };
               EmitSample s;
               toWorld(p, s.p);
               toWorldNormal(n, s.n);
               s.c[0] = material.color[0]; s.c[1] = material.color[1]; s.c[2] = material.color[2];
               mEmitSamples.push_back(s);
            }
         }
      }
      else
      {
         for (const Vertex& vtx : mesh.vertices)
         {
            EmitSample s;
            const float p[3] = { vtx.px, vtx.py, vtx.pz };
            const float n[3] = { vtx.nx, vtx.ny, vtx.nz };
            toWorld(p, s.p);
            toWorldNormal(n, s.n);
            s.c[0] = material.color[0]; s.c[1] = material.color[1]; s.c[2] = material.color[2];
            mEmitSamples.push_back(s);
         }
      }
   }

   mEmitHasBounds = !mEmitSamples.empty();
   for (int k = 0; k < 3; k++) { mEmitLo[k] = 1e30f; mEmitHi[k] = -1e30f; }
   for (const EmitSample& s : mEmitSamples)
      for (int k = 0; k < 3; k++)
      {
         mEmitLo[k] = std::min(mEmitLo[k], s.p[k]);
         mEmitHi[k] = std::max(mEmitHi[k], s.p[k]);
      }
   if (mEmitSamples.empty())
      mCookWarning = "Nothing to emit from: the emit pin's geometry is empty";
}

// ------------------------------------------------------------ GPU path
//
// Particle i lives in texel (i % texW, i / texW) of three state textures, a
// ping-pong pair each: A = position + age, B = velocity + life (0 = dead or
// never born), C = birth colour. One fullscreen pass per fixed step births the
// step's particles into a ring of slots (cursor = births so far mod the cap)
// and integrates the rest; a present pass writes the GpuPoints textures every
// consumer reads, with an occlusion query for the alive count. Births draw
// their random numbers from a hash of their birth number rather than a running
// generator, so the GPU run is reproducible after a rewind; it matches the CPU
// run statistically, not number for number. At the cap the ring replaces the
// oldest particles where the CPU path drops new births.

namespace
{
   const char* const kParticleCommonGlsl =
      "uint pcg(uint v) {\n"
      "   uint state = v * 747796405u + 2891336453u;\n"
      "   uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;\n"
      "   return (word >> 22u) ^ word;\n"
      "}\n"
      "float rnd(uint id, uint k) { return float(pcg(pcg(id + uSeed) + k) >> 8) / 16777215.0; }\n"
      "float rnds(uint id, uint k) { return rnd(id, k) * 2.0 - 1.0; }\n"
      // The CPU path's value noise (Noise3), term for term.
      "float noise3(float x, float y, float z) {\n"
      "   float v = sin(x * 12.9898 + y * 78.233 + z * 37.719) * 43758.5453;\n"
      "   return fract(v) * 2.0 - 1.0;\n"
      "}\n";

   std::string ParticleSimSource()
   {
      return std::string(
         "#version 150\n"
         "uniform sampler2D uStateA;\n"
         "uniform sampler2D uStateB;\n"
         "uniform sampler2D uStateC;\n"
         "uniform sampler2D uEmitTable;\n"   // CPU birth points, 3 texels each (pos, normal, colour)
         "uniform sampler2D uEmitPos;\n"     // or the emit pin's GPU points
         "uniform sampler2D uEmitColor;\n"
         "uniform int uEmitMode;\n"          // 0 shape, 1 table, 2 GPU points
         "uniform int uEmitCount;\n"
         "uniform int uEmitTexW;\n"
         "uniform mat4 uEmitModel;\n"
         "uniform int uTexW;\n"
         "uniform int uCap;\n"
         "uniform int uCursor;\n"
         "uniform int uBirths;\n"
         "uniform uint uBirthStart;\n"
         "uniform uint uSeed;\n"
         "uniform int uShape;\n"
         "uniform float uRadius;\n"
         "uniform vec3 uDir;\n"
         "uniform float uSpread;\n"
         "uniform float uSpeed;\n"
         "uniform float uSpeedRandom;\n"
         "uniform float uSpeedAlongNormal;\n"
         "uniform float uLifetime;\n"
         "uniform float uLifetimeRandom;\n"
         "uniform vec3 uStartColor;\n"
         "uniform int uCopyColor;\n"
         "uniform vec3 uGravity;\n"
         "uniform float uDragFactor;\n"
         "uniform float uTurb;\n"
         "uniform float uTurbScale;\n"
         "uniform float uDt;\n"
         "out vec4 outA;\n"
         "out vec4 outB;\n"
         "out vec4 outC;\n") +
         kParticleCommonGlsl +
         "vec4 tableTexel(int j) { return texelFetch(uEmitTable, ivec2(j % uEmitTexW, j / uEmitTexW), 0); }\n"
         "void main() {\n"
         "   ivec2 cell = ivec2(gl_FragCoord.xy);\n"
         "   int i = cell.y * uTexW + cell.x;\n"
         "   if (i >= uCap) { outA = vec4(0.0); outB = vec4(0.0); outC = vec4(0.0); return; }\n"
         "   vec4 a = texelFetch(uStateA, cell, 0);\n"
         "   vec4 b = texelFetch(uStateB, cell, 0);\n"
         "   vec4 c = texelFetch(uStateC, cell, 0);\n"
         "   int d = i - uCursor;\n"
         "   if (d < 0) d += uCap;\n"
         "   if (d < uBirths) {\n"
         "      uint id = uBirthStart + uint(d);\n"
         "      vec3 pos = vec3(0.0), nrm = vec3(0.0), col = uStartColor;\n"
         "      bool born = true;\n"
         "      if (uEmitMode == 1) {\n"
         "         int s = int(pcg(id ^ 0x68E31DA4u) % uint(uEmitCount));\n"
         "         pos = tableTexel(s * 3).xyz; nrm = tableTexel(s * 3 + 1).xyz;\n"
         "         if (uCopyColor == 1) col = tableTexel(s * 3 + 2).rgb;\n"
         "      } else if (uEmitMode == 2) {\n"
         // A hidden source point (w == 0) is no birth place: try a few others.
         "         born = false;\n"
         "         for (uint t = 0u; t < 8u && !born; t++) {\n"
         "            int s = int(pcg(id ^ (0x68E31DA4u + t * 0x1B56C4E9u)) % uint(uEmitCount));\n"
         "            ivec2 sc = ivec2(s % uEmitTexW, s / uEmitTexW);\n"
         "            vec4 sp = texelFetch(uEmitPos, sc, 0);\n"
         "            if (sp.w > 0.0) {\n"
         "               born = true;\n"
         "               pos = (uEmitModel * vec4(sp.xyz, 1.0)).xyz;\n"
         "               nrm = normalize(mat3(uEmitModel) * vec3(0.0, 0.0, 1.0));\n"
         "               if (uCopyColor == 1) col = texelFetch(uEmitColor, sc, 0).rgb;\n"
         "            }\n"
         "         }\n"
         "      } else if (uShape == 1) {\n"
         "         float u = rnds(id, 0u);\n"
         "         float theta = rnd(id, 1u) * 6.28318530718;\n"
         "         float r = pow(rnd(id, 2u), 1.0 / 3.0) * uRadius;\n"
         "         float s = sqrt(max(0.0, 1.0 - u * u));\n"
         "         nrm = vec3(s * cos(theta), u, s * sin(theta));\n"
         "         pos = r * nrm;\n"
         "      } else if (uShape == 2) {\n"
         "         pos = vec3(rnds(id, 0u), rnds(id, 1u), rnds(id, 2u)) * uRadius;\n"
         "         if (length(pos) > 1e-6) nrm = normalize(pos);\n"
         "      } else if (uShape == 3) {\n"
         "         float theta = rnd(id, 0u) * 6.28318530718;\n"
         "         float r = sqrt(rnd(id, 1u)) * uRadius;\n"
         "         pos = vec3(r * cos(theta), 0.0, r * sin(theta));\n"
         "         nrm = vec3(0.0, 1.0, 0.0);\n"
         "      } else {\n"
         "         nrm = uDir;\n"
         "      }\n"
         "      vec3 rv = vec3(rnds(id, 3u), rnds(id, 4u), rnds(id, 5u));\n"
         "      if (length(rv) > 1e-5) rv = normalize(rv);\n"
         "      float blend = clamp(uSpread, 0.0, 1.0);\n"
         "      vec3 v = uDir * (1.0 - blend) + rv * blend;\n"
         "      if (length(v) > 1e-5) v = normalize(v);\n"
         "      float speed = uSpeed * (1.0 + rnds(id, 6u) * uSpeedRandom);\n"
         "      float life = max(0.05, uLifetime * (1.0 + rnds(id, 7u) * uLifetimeRandom));\n"
         "      a = vec4(pos, 0.0);\n"
         "      b = vec4(v * speed + nrm * uSpeedAlongNormal, born ? life : 0.0);\n"
         "      c = vec4(col, 1.0);\n"
         "   }\n"
         "   if (b.w > 0.0) {\n"
         "      a.w += uDt;\n"
         "      if (a.w >= b.w) {\n"
         "         b.w = 0.0;\n"
         "      } else {\n"
         "         vec3 vel = b.xyz + uGravity * uDt;\n"
         "         if (uTurb > 0.0) {\n"
         "            float s = uTurbScale;\n"
         "            vec3 p = a.xyz;\n"
         "            vel.x += noise3(p.x * s, p.y * s, p.z * s) * uTurb * uDt;\n"
         "            vel.y += noise3(p.y * s + 31.4, p.z * s, p.x * s) * uTurb * uDt;\n"
         "            vel.z += noise3(p.z * s, p.x * s + 17.1, p.y * s) * uTurb * uDt;\n"
         "         }\n"
         "         vel *= uDragFactor;\n"
         "         a.xyz += vel * uDt;\n"
         "         b.xyz = vel;\n"
         "      }\n"
         "   }\n"
         "   outA = a;\n"
         "   outB = b;\n"
         "   outC = c;\n"
         "}\n";
   }

   const char* const kParticlePresentSource =
      "#version 150\n"
      "uniform sampler2D uStateA;\n"
      "uniform sampler2D uStateB;\n"
      "uniform sampler2D uStateC;\n"
      "uniform int uCopyColor;\n"
      "uniform vec3 uStartColor;\n"
      "uniform vec3 uEndColor;\n"
      "uniform float uStartSize;\n"
      "uniform float uEndSize;\n"
      "uniform float uSizeCurve;\n"
      "uniform float uColorCurve;\n"
      "uniform float uBaseSize;\n"
      "out vec4 outPos;\n"
      "out vec4 outColor;\n"
      "void main() {\n"
      "   ivec2 cell = ivec2(gl_FragCoord.xy);\n"
      "   vec4 a = texelFetch(uStateA, cell, 0);\n"
      "   vec4 b = texelFetch(uStateB, cell, 0);\n"
      // Dead and unborn slots keep the clear (w = 0 hides a point) and pass no
      // sample, so the occlusion query counts the living.
      "   if (b.w <= 0.0) discard;\n"
      "   float t = clamp(a.w / b.w, 0.0, 1.0);\n"
      "   float size = mix(uStartSize, uEndSize, pow(t, uSizeCurve));\n"
      "   outPos = vec4(a.xyz, size * uBaseSize);\n"
      "   outColor = vec4(uCopyColor == 1 ? texelFetch(uStateC, cell, 0).rgb\n"
      "                                   : mix(uStartColor, uEndColor, pow(t, uColorCurve)), t);\n"
      "}\n";

   // Min/max of the living particles' positions (and their top speed), 16 x 16
   // texels per output: three levels take 1024 x 1024 down to one texel.
   const char* const kParticleBoundsSource =
      "#version 150\n"
      "uniform sampler2D uSrcA;\n"
      "uniform sampler2D uSrcB;\n"
      "uniform ivec2 uSrcSize;\n"
      "uniform int uFirst;\n"
      "out vec4 outMin;\n"
      "out vec4 outMax;\n"
      "void main() {\n"
      "   ivec2 base = ivec2(gl_FragCoord.xy) * 16;\n"
      "   vec4 mn = vec4(1e30);\n"
      "   vec4 mx = vec4(-1e30);\n"
      "   for (int y = 0; y < 16; y++) {\n"
      "      for (int x = 0; x < 16; x++) {\n"
      "         ivec2 c = base + ivec2(x, y);\n"
      "         if (c.x >= uSrcSize.x || c.y >= uSrcSize.y) continue;\n"
      "         vec4 a = texelFetch(uSrcA, c, 0);\n"
      "         vec4 b = texelFetch(uSrcB, c, 0);\n"
      "         if (uFirst == 1) {\n"
      "            if (b.w <= 0.0) continue;\n"
      "            mn.xyz = min(mn.xyz, a.xyz);\n"
      "            mx.xyz = max(mx.xyz, a.xyz);\n"
      "            mx.w = max(mx.w, length(b.xyz));\n"
      "         } else {\n"
      "            mn = min(mn, a);\n"
      "            mx = max(mx, b);\n"
      "         }\n"
      "      }\n"
      "   }\n"
      "   outMin = mn;\n"
      "   outMax = mx;\n"
      "}\n";

   unsigned int MakeStateTexture(int w, int h, GLenum internalFormat, const char* label)
   {
      unsigned int tex = 0;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      Bench::GpuMem::RecordTexture(tex, Bench::GpuMemCategory::RenderTargets, w, h, internalFormat, false, label);
      return tex;
   }

   void DeleteStateTexture(unsigned int& tex)
   {
      if (tex == 0)
         return;
      Bench::GpuMem::ReleaseTexture(tex);
      glDeleteTextures(1, &tex);
      tex = 0;
   }

   // An FBO over `count` textures, each output of `program` routed to its
   // attachment by name (GLSL 150 has no layout qualifiers to fix them).
   unsigned int MakeMrtFbo(const unsigned int* tex, int count, unsigned int program, const char* const* outputs)
   {
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      unsigned int fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      GLenum bufs[4] = { GL_NONE, GL_NONE, GL_NONE, GL_NONE };
      bool ok = count <= 4;
      for (int k = 0; k < count && ok; k++)
      {
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + k, GL_TEXTURE_2D, tex[k], 0);
         const GLint loc = glGetFragDataLocation(program, outputs[k]);
         if (loc < 0 || loc >= count || bufs[loc] != GL_NONE)
            ok = false;
         else
            bufs[loc] = GL_COLOR_ATTACHMENT0 + k;
      }
      if (ok)
      {
         glDrawBuffers(count, bufs);
         ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
      }
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      if (!ok)
      {
         glDeleteFramebuffers(1, &fbo);
         return 0;
      }
      return fbo;
   }

   void BindSampler(unsigned int program, const char* name, int unit, unsigned int tex)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(program, name), unit);
   }
}

ParticleSystemNode::~ParticleSystemNode()
{
   ReleaseGpu();
   if (mSimProgram != 0)
      glDeleteProgram(mSimProgram);
   if (mPresentProgram != 0)
      glDeleteProgram(mPresentProgram);
   if (mBoundsProgram != 0)
      glDeleteProgram(mBoundsProgram);
}

void ParticleSystemNode::ReleaseGpu()
{
   for (int s = 0; s < 2; s++)
   {
      if (mSimFbo[s] != 0)
         glDeleteFramebuffers(1, &mSimFbo[s]);
      mSimFbo[s] = 0;
      for (int k = 0; k < 3; k++)
         DeleteStateTexture(mSimTex[s][k]);
   }
   for (int l = 0; l < 3; l++)
   {
      if (mBoundsFbo[l] != 0)
         glDeleteFramebuffers(1, &mBoundsFbo[l]);
      mBoundsFbo[l] = 0;
      DeleteStateTexture(mBoundsTex[l][0]);
      DeleteStateTexture(mBoundsTex[l][1]);
   }
   if (mBoundsPbo != 0)
      glDeleteBuffers(1, &mBoundsPbo);
   mBoundsPbo = 0;
   if (mBoundsSync != nullptr)
      glDeleteSync((GLsync)mBoundsSync);
   mBoundsSync = nullptr;
   mBoundsPending = false;
   DeleteStateTexture(mEmitTex);
   mEmitTexCount = 0;
   mEmitTexStamp = 0;
   GpuPointsGL::DestroyTarget(mOut);
   if (mCountQuery != 0)
      glDeleteQueries(1, &mCountQuery);
   mCountQuery = 0;
   mCountPending = false;
   mGpuCap = 0;
   mGpuPoints = GpuPoints();
}

bool ParticleSystemNode::EnsureGpu(int cap)
{
   if (mSimProgram == 0 || mPresentProgram == 0 || mBoundsProgram == 0)
   {
      if (mSimProgram == 0)
         mSimProgram = GLUtil::CompileProgram(ParticleSimSource().c_str());
      if (mPresentProgram == 0)
         mPresentProgram = GLUtil::CompileProgram(kParticlePresentSource);
      if (mBoundsProgram == 0)
         mBoundsProgram = GLUtil::CompileProgram(kParticleBoundsSource);
      if (mSimProgram == 0 || mPresentProgram == 0 || mBoundsProgram == 0)
         return false;
   }
   if (mGpuCap == cap && mSimFbo[0] != 0)
      return true;
   ReleaseGpu();

   const int slots = std::max(1, cap);
   const int texW = std::min(1024, slots);
   const int texH = (slots + texW - 1) / texW;
   static const char* const kSimOutputs[3] = { "outA", "outB", "outC" };
   for (int s = 0; s < 2; s++)
   {
      mSimTex[s][0] = MakeStateTexture(texW, texH, GL_RGBA32F, "ParticleSystem_State");
      mSimTex[s][1] = MakeStateTexture(texW, texH, GL_RGBA32F, "ParticleSystem_State");
      // Birth colour: half floats are plenty for a colour.
      mSimTex[s][2] = MakeStateTexture(texW, texH, GL_RGBA16F, "ParticleSystem_State");
      mSimFbo[s] = MakeMrtFbo(mSimTex[s], 3, mSimProgram, kSimOutputs);
      if (mSimFbo[s] == 0)
      {
         ReleaseGpu();
         return false;
      }
   }
   if (!GpuPointsGL::EnsureTarget(mOut, texW, texH, "ParticleSystem_GpuPoints"))
   {
      ReleaseGpu();
      return false;
   }
   GpuPointsGL::BindOutputs(mOut, mPresentProgram, "outPos", "outColor");

   static const char* const kBoundsOutputs[2] = { "outMin", "outMax" };
   int w = texW, h = texH;
   for (int l = 0; l < 3; l++)
   {
      w = (w + 15) / 16;
      h = (h + 15) / 16;
      mBoundsW[l] = w;
      mBoundsH[l] = h;
      mBoundsTex[l][0] = MakeStateTexture(w, h, GL_RGBA32F, "ParticleSystem_Bounds");
      mBoundsTex[l][1] = MakeStateTexture(w, h, GL_RGBA32F, "ParticleSystem_Bounds");
      mBoundsFbo[l] = MakeMrtFbo(mBoundsTex[l], 2, mBoundsProgram, kBoundsOutputs);
      if (mBoundsFbo[l] == 0)
      {
         ReleaseGpu();
         return false;
      }
   }
   glGenBuffers(1, &mBoundsPbo);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, mBoundsPbo);
   glBufferData(GL_PIXEL_PACK_BUFFER, 8 * sizeof(float), nullptr, GL_STREAM_READ);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
   glGenQueries(1, &mCountQuery);

   mGpuCap = cap;
   mGpuTexH = texH;
   mGpuPoints = GpuPoints();
   mGpuPoints.posTex = mOut.posTex;
   mGpuPoints.colorTex = mOut.colorTex;
   mGpuPoints.texW = texW;
   mGpuPoints.texH = texH;
   mGpuPoints.count = cap;
   mGpuPoints.pointMatrix = Mat4::Identity();
   mGpuPoints.liveCount = 0;
   return true;
}

void ParticleSystemNode::ClearGpuState()
{
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glClearColor(0, 0, 0, 0);
   for (int s = 0; s < 2; s++)
   {
      glBindFramebuffer(GL_FRAMEBUFFER, mSimFbo[s]);
      glClear(GL_COLOR_BUFFER_BIT);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, mOut.fbo);
   glClear(GL_COLOR_BUFFER_BIT);
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   mSimRead = 0;
   mGpuPoints.hasBounds = false;
   mGpuPoints.liveCount = 0;
}

void ParticleSystemNode::GpuStep(float dt, int births)
{
   const int cap = mGpuCap;
   births = std::max(0, std::min(births, cap));

   // The emit pin's birth points, uploaded when they change.
   int emitMode = 0;
   if (EmittingFromInput())
   {
      if (mEmitGpu != nullptr)
      {
         emitMode = 2;
      }
      else
      {
         emitMode = 1;
         if (mEmitTexStamp != mEmitSamplesStamp || mEmitTex == 0)
         {
            const int texels = (int)mEmitSamples.size() * 3;
            const int rows = (texels + 1023) / 1024;
            std::vector<float> data((size_t)rows * 1024 * 4, 0.0f);
            for (size_t s = 0; s < mEmitSamples.size(); s++)
            {
               const EmitSample& e = mEmitSamples[s];
               float* t = &data[s * 12];
               t[0] = e.p[0]; t[1] = e.p[1]; t[2] = e.p[2];
               t[4] = e.n[0]; t[5] = e.n[1]; t[6] = e.n[2];
               t[8] = e.c[0]; t[9] = e.c[1]; t[10] = e.c[2];
            }
            DeleteStateTexture(mEmitTex);
            mEmitTex = MakeStateTexture(1024, rows, GL_RGBA32F, "ParticleSystem_Emit");
            glBindTexture(GL_TEXTURE_2D, mEmitTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, rows, GL_RGBA, GL_FLOAT, data.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            mEmitTexCount = (int)mEmitSamples.size();
            mEmitTexStamp = mEmitSamplesStamp;
         }
      }
   }

   float dir[3] = { dirX, dirY, dirZ };
   const float dirLen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
   if (dirLen > 1e-5f) { dir[0] /= dirLen; dir[1] /= dirLen; dir[2] /= dirLen; }
   else { dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f; }
   const float invMass = 1.0f / std::max(0.1f, mass);
   const unsigned int seedHash = (unsigned int)std::fmod(std::fabs((double)seed) * 1000.0, 4294967296.0);

   const int write = 1 - mSimRead;
   GLUtil::Fbo target;
   target.fbo = mSimFbo[write];
   target.tex = mSimTex[write][0];
   target.w = mGpuPoints.texW;
   target.h = mGpuTexH;
   target.internalFormat = GL_RGBA32F;
   const unsigned int prog = mSimProgram;
   GLUtil::RunShaderPass(target, prog, [&]()
   {
      BindSampler(prog, "uStateA", 0, mSimTex[mSimRead][0]);
      BindSampler(prog, "uStateB", 1, mSimTex[mSimRead][1]);
      BindSampler(prog, "uStateC", 2, mSimTex[mSimRead][2]);
      BindSampler(prog, "uEmitTable", 3, emitMode == 1 ? mEmitTex : 0);
      BindSampler(prog, "uEmitPos", 4, emitMode == 2 ? mEmitGpu->posTex : 0);
      BindSampler(prog, "uEmitColor", 5, emitMode == 2 ? mEmitGpu->colorTex : 0);
      glUniform1i(glGetUniformLocation(prog, "uEmitMode"), emitMode);
      glUniform1i(glGetUniformLocation(prog, "uEmitCount"),
                  emitMode == 1 ? std::max(1, mEmitTexCount) : emitMode == 2 ? std::max(1, mEmitGpu->count) : 1);
      glUniform1i(glGetUniformLocation(prog, "uEmitTexW"), emitMode == 2 ? std::max(1, mEmitGpu->texW) : 1024);
      glUniformMatrix4fv(glGetUniformLocation(prog, "uEmitModel"), 1, GL_FALSE, mEmitGpuModel.m);
      glUniform1i(glGetUniformLocation(prog, "uTexW"), mGpuPoints.texW);
      glUniform1i(glGetUniformLocation(prog, "uCap"), cap);
      glUniform1i(glGetUniformLocation(prog, "uCursor"), cap > 0 ? (int)(mBirths % (unsigned long long)cap) : 0);
      glUniform1i(glGetUniformLocation(prog, "uBirths"), births);
      glUniform1ui(glGetUniformLocation(prog, "uBirthStart"), (unsigned int)mBirths);
      glUniform1ui(glGetUniformLocation(prog, "uSeed"), seedHash);
      glUniform1i(glGetUniformLocation(prog, "uShape"), emitShape);
      glUniform1f(glGetUniformLocation(prog, "uRadius"), emitRadius);
      glUniform3f(glGetUniformLocation(prog, "uDir"), dir[0], dir[1], dir[2]);
      glUniform1f(glGetUniformLocation(prog, "uSpread"), spread);
      glUniform1f(glGetUniformLocation(prog, "uSpeed"), initialSpeed);
      glUniform1f(glGetUniformLocation(prog, "uSpeedRandom"), speedRandom);
      glUniform1f(glGetUniformLocation(prog, "uSpeedAlongNormal"), speedAlongNormal);
      glUniform1f(glGetUniformLocation(prog, "uLifetime"), lifetime);
      glUniform1f(glGetUniformLocation(prog, "uLifetimeRandom"), lifetimeRandom);
      glUniform3f(glGetUniformLocation(prog, "uStartColor"), startColor[0], startColor[1], startColor[2]);
      glUniform1i(glGetUniformLocation(prog, "uCopyColor"), (emitMode != 0 && copySourceColor) ? 1 : 0);
      glUniform3f(glGetUniformLocation(prog, "uGravity"), gravityX, gravityY, gravityZ);
      glUniform1f(glGetUniformLocation(prog, "uDragFactor"), std::max(0.0f, 1.0f - drag * invMass * dt));
      glUniform1f(glGetUniformLocation(prog, "uTurb"), turbulence * invMass);
      glUniform1f(glGetUniformLocation(prog, "uTurbScale"), turbulenceScale);
      glUniform1f(glGetUniformLocation(prog, "uDt"), dt);
   });
   for (int unit = 5; unit >= 0; unit--)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   mSimRead = write;
   mBirths += (unsigned long long)births;
   mSimSeconds += dt;
}

void ParticleSystemNode::GpuPresent()
{
   GLUtil::Fbo target;
   target.fbo = mOut.fbo;
   target.tex = mOut.posTex;
   target.w = mOut.w;
   target.h = mOut.h;
   target.internalFormat = GL_RGBA32F;

   // One count in flight at a time; a present that cannot start one leaves
   // the count stale, and a later cook with no step recounts.
   const bool query = !mCountPending;
   mCountStale = !query;
   if (query)
      glBeginQuery(GL_SAMPLES_PASSED, mCountQuery);
   const unsigned int prog = mPresentProgram;
   const bool keepColor = copySourceColor && EmittingFromInput();
   GLUtil::RunShaderPass(target, prog, [&]()
   {
      BindSampler(prog, "uStateA", 0, mSimTex[mSimRead][0]);
      BindSampler(prog, "uStateB", 1, mSimTex[mSimRead][1]);
      BindSampler(prog, "uStateC", 2, mSimTex[mSimRead][2]);
      glUniform1i(glGetUniformLocation(prog, "uCopyColor"), keepColor ? 1 : 0);
      glUniform3f(glGetUniformLocation(prog, "uStartColor"), startColor[0], startColor[1], startColor[2]);
      glUniform3f(glGetUniformLocation(prog, "uEndColor"), endColor[0], endColor[1], endColor[2]);
      glUniform1f(glGetUniformLocation(prog, "uStartSize"), startSize);
      glUniform1f(glGetUniformLocation(prog, "uEndSize"), endSize);
      glUniform1f(glGetUniformLocation(prog, "uSizeCurve"), std::max(0.05f, sizeCurve));
      glUniform1f(glGetUniformLocation(prog, "uColorCurve"), std::max(0.05f, colorCurve));
      glUniform1f(glGetUniformLocation(prog, "uBaseSize"), PointBaseSize());
   });
   if (query)
   {
      glEndQuery(GL_SAMPLES_PASSED);
      mCountPending = true;
      mCountResets = mResets;
   }

   // Bounds: reduce now, read the one texel back next cook or later.
   if (!mBoundsPending)
   {
      for (int l = 0; l < 3; l++)
      {
         GLUtil::Fbo level;
         level.fbo = mBoundsFbo[l];
         level.tex = mBoundsTex[l][0];
         level.w = mBoundsW[l];
         level.h = mBoundsH[l];
         level.internalFormat = GL_RGBA32F;
         const unsigned int bp = mBoundsProgram;
         const unsigned int srcA = l == 0 ? mSimTex[mSimRead][0] : mBoundsTex[l - 1][0];
         const unsigned int srcB = l == 0 ? mSimTex[mSimRead][1] : mBoundsTex[l - 1][1];
         const int srcW = l == 0 ? mOut.w : mBoundsW[l - 1];
         const int srcH = l == 0 ? mOut.h : mBoundsH[l - 1];
         GLUtil::RunShaderPass(level, bp, [&]()
         {
            BindSampler(bp, "uSrcA", 0, srcA);
            BindSampler(bp, "uSrcB", 1, srcB);
            glUniform2i(glGetUniformLocation(bp, "uSrcSize"), srcW, srcH);
            glUniform1i(glGetUniformLocation(bp, "uFirst"), l == 0 ? 1 : 0);
         });
      }
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mBoundsFbo[2]);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, mBoundsPbo);
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, (void*)0);
      glReadBuffer(GL_COLOR_ATTACHMENT1);
      glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, (void*)(4 * sizeof(float)));
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      if (mBoundsSync != nullptr) // one dropped by a reset
         glDeleteSync((GLsync)mBoundsSync);
      mBoundsSync = (void*)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      mBoundsPending = true;
      mBoundsIssuedAt = mSimSeconds;
   }
   for (int unit = 2; unit >= 0; unit--)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   UpdateGpuBounds();
}

void ParticleSystemNode::ReadCountQuery()
{
   if (!mCountPending)
      return;
   GLuint available = 0;
   glGetQueryObjectuiv(mCountQuery, GL_QUERY_RESULT_AVAILABLE, &available);
   if (!available)
      return;
   GLuint samples = 0;
   glGetQueryObjectuiv(mCountQuery, GL_QUERY_RESULT, &samples);
   mCountPending = false;
   if (mCountResets != mResets)
      return; // counted before a reset: mCountStale recounts
   mLiveCount = (long long)samples;
   mGpuPoints.liveCount = (int)samples;
}

void ParticleSystemNode::ReadBoundsIfReady()
{
   if (!mBoundsPending || mBoundsSync == nullptr)
      return;
   const GLenum state = glClientWaitSync((GLsync)mBoundsSync, 0, 0);
   if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED)
      return;
   glDeleteSync((GLsync)mBoundsSync);
   mBoundsSync = nullptr;
   mBoundsPending = false;
   float v[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
   glBindBuffer(GL_PIXEL_PACK_BUFFER, mBoundsPbo);
   glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, sizeof(v), v);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
   mBoundsValid = v[0] <= v[4] && std::isfinite(v[0]) && std::isfinite(v[4]);
   for (int k = 0; k < 3; k++)
   {
      mBoundsLo[k] = v[k];
      mBoundsHi[k] = v[4 + k];
   }
   mBoundsMaxSpeed = std::isfinite(v[7]) ? std::max(0.0f, v[7]) : 0.0f;
   mBoundsAt = mBoundsIssuedAt;
}

// GpuPoints bounds: the last read-back box, grown by how far the fastest
// particle could have gone since (plus gravity and turbulence), joined with where new ones
// are born.
void ParticleSystemNode::UpdateGpuBounds()
{
   float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
   bool any = false;
   if (mBoundsValid)
   {
      const float lag = (float)std::max(0.0, mSimSeconds - mBoundsAt);
      const float g[3] = { gravityX, gravityY, gravityZ };
      for (int k = 0; k < 3; k++)
      {
         // Turbulence pushes up to turbulence / mass on each axis.
         const float accel = std::fabs(g[k]) + std::fabs(turbulence) / std::max(0.1f, mass);
         const float pad = mBoundsMaxSpeed * lag + 0.5f * accel * lag * lag;
         lo[k] = mBoundsLo[k] - pad;
         hi[k] = mBoundsHi[k] + pad;
      }
      any = true;
   }
   if (mLiveCount > 0 || mBirths > 0)
   {
      float elo[3], ehi[3];
      bool have = false;
      if (EmittingFromInput())
      {
         if (mEmitHasBounds)
         {
            for (int k = 0; k < 3; k++) { elo[k] = mEmitLo[k]; ehi[k] = mEmitHi[k]; }
            have = true;
         }
      }
      else
      {
         const float r = std::fabs(emitRadius);
         for (int k = 0; k < 3; k++) { elo[k] = -r; ehi[k] = r; }
         if (emitShape == kDisc) { elo[1] = 0.0f; ehi[1] = 0.0f; }
         have = true;
      }
      if (have)
      {
         for (int k = 0; k < 3; k++)
         {
            lo[k] = std::min(lo[k], elo[k]);
            hi[k] = std::max(hi[k], ehi[k]);
         }
         any = true;
      }
   }
   mGpuPoints.hasBounds = any && lo[0] <= hi[0];
   for (int k = 0; k < 3; k++)
   {
      mGpuPoints.lo[k] = lo[k];
      mGpuPoints.hi[k] = hi[k];
   }
   mGpuPoints.maxHalfSize = std::max(std::fabs(startSize), std::fabs(endSize)) * PointBaseSize();
}

// A CPU consumer (MetaBall, Proximity Lines, a test) asked: read the GPU state
// back once per revision, slot for slot, dead slots kept as dead particles the
// way the CPU path keeps them.
void ParticleSystemNode::EnsureCpuParticles()
{
   if (!mGpuActive || mCpuRevision == mRevision || mGpuCap <= 0)
      return;
   mCpuRevision = mRevision;
   const int texW = mGpuPoints.texW;
   const size_t used = (size_t)std::min<unsigned long long>(mBirths, (unsigned long long)mGpuCap);
   mParticles.clear();
   if (used == 0)
      return;
   const int rows = (int)((used + (size_t)texW - 1) / (size_t)texW);
   const size_t texels = (size_t)rows * (size_t)texW;
   std::vector<float> a(texels * 4), b(texels * 4), pos(texels * 4), col(texels * 4);

   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 4);
   glBindFramebuffer(GL_FRAMEBUFFER, mSimFbo[mSimRead]);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   glReadPixels(0, 0, texW, rows, GL_RGBA, GL_FLOAT, a.data());
   glReadBuffer(GL_COLOR_ATTACHMENT1);
   glReadPixels(0, 0, texW, rows, GL_RGBA, GL_FLOAT, b.data());
   glBindFramebuffer(GL_FRAMEBUFFER, mOut.fbo);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   glReadPixels(0, 0, texW, rows, GL_RGBA, GL_FLOAT, pos.data());
   glReadBuffer(GL_COLOR_ATTACHMENT1);
   glReadPixels(0, 0, texW, rows, GL_RGBA, GL_FLOAT, col.data());
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

   const float base = std::max(1e-8f, PointBaseSize());
   mParticles.resize(used);
   for (size_t i = 0; i < used; i++)
   {
      Particle& p = mParticles[i];
      const float* sa = &a[i * 4];
      const float* sb = &b[i * 4];
      const float* sp = &pos[i * 4];
      const float* sc = &col[i * 4];
      p.px = sa[0]; p.py = sa[1]; p.pz = sa[2];
      p.age = sa[3];
      p.vx = sb[0]; p.vy = sb[1]; p.vz = sb[2];
      p.life = sb[3];
      p.alive = sb[3] > 0.0f;
      p.scale = p.alive ? sp[3] / base : 0.0f;
      p.r = sc[0]; p.g = sc[1]; p.b = sc[2];
      p.hasColor = true;
   }
}

void ParticleSystemNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* upstream = dynamic_cast<INode*>(emitSource))
      upstream->CookIfNeeded(frameId);

   // Bypassed: GetPointCloud() reports nothing, so stepping the emitter would
   // just burn CPU on particles nobody reads. mLastBeats is left frozen; the
   // dt clamp below already bounds the catch-up jump on resume.
   if (bypassed)
      return;

   // GPU or CPU for this run. Switching (the kill switch, a failed shader, a
   // new cap the textures do not hold) starts the run over.
   const bool wantGpu = GpuPointsGL::Enabled() && !mGpuFailed;
   const int wantCap = std::max(0, std::min(maxParticles, wantGpu ? kGpuMaxParticles : kCpuMaxParticles));
   if (wantGpu != mGpuActive || (wantGpu && wantCap != mGpuCap))
   {
      mGpuActive = wantGpu && EnsureGpu(wantCap);
      if (wantGpu && !mGpuActive)
      {
         mGpuFailed = true;
         ReleaseGpu();
      }
      else if (!wantGpu)
      {
         ReleaseGpu();
      }
      Reset();
   }
   if (mGpuActive)
   {
      if (mGpuClearPending)
         ClearGpuState();
      mGpuClearPending = false;
      ReadCountQuery();
      ReadBoundsIfReady();
   }

   PullEmitSamples();

   const double beats = Transport::Instance().Beats();
   const float bpm = std::max(1.0f, Transport::Instance().Tempo());

   // Rewinding must put the simulation back to nothing, or a patch's look
   // depends on how long it was left running before the rewind.
   if (mLastBeats < 0.0 || beats < mLastBeats)
   {
      Reset();
      if (mGpuActive)
      {
         ClearGpuState();
         mGpuClearPending = false;
      }
      mLastBeats = beats;
      return;
   }

   // Beats to seconds, so tempo changes retime the simulation the same way they
   // retime every other time-based node.
   const double deltaBeats = beats - mLastBeats;
   mLastBeats = beats;
   float dt = (float)(deltaBeats * 60.0 / (double)bpm) * std::max(0.0f, timeScale);
   if (dt <= 0.0f && !(mGpuActive && mCountStale && !mCountPending))
      return; // paused (or time scale 0): the clock did not move, so neither does the simulation
   dt = std::max(0.0f, std::min(dt, kMaxCatchUp));

   mAccumulator += dt;
   int steps = 0;
   const int maxSteps = (int)(kMaxCatchUp / kFixedStep) + 1;
   GLboolean prevBlend = GL_FALSE;
   if (mGpuActive)
   {
      prevBlend = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND); // state is data: no blending into the clear
   }
   while (mAccumulator >= kFixedStep && steps < maxSteps)
   {
      mEmitCarry += emitRate * kFixedStep;
      const int toEmit = (int)mEmitCarry;
      if (toEmit > 0)
         mEmitCarry -= (float)toEmit;
      if (mGpuActive)
      {
         GpuStep(kFixedStep, toEmit);
      }
      else
      {
         if (toEmit > 0)
            Emit(toEmit);
         Step(kFixedStep);
      }
      mAccumulator -= kFixedStep;
      steps++;
   }

   if (mGpuActive)
   {
      if (steps > 0 || (mCountStale && !mCountPending))
         GpuPresent();
      if (prevBlend)
         glEnable(GL_BLEND);
      mAliveCount = (size_t)std::max(0LL, mLiveCount);
   }
   else
   {
      mAliveCount = 0;
      for (const Particle& p : mParticles)
         if (p.alive)
            mAliveCount++;
   }

   if (steps > 0)
   {
      mRevision = NextMeshRevision();
      if (mGpuActive)
         mGpuPoints.revision = mRevision;
      else
         mCpuRevision = mRevision;
   }
}

// ================================================================== Cloth

namespace
{
   const std::vector<std::string> kPinModeNames = { "None", "Top edge", "Corners", "Border" };
}

const std::vector<std::string>& ClothNode::PinModeNames() { return kPinModeNames; }

void ClothNode::RebuildFromInput()
{
   mMesh = Mesh();
   mRest = Mesh();
   mConstraints.clear();
   mWeld.clear();
   mPos.clear();
   mPrev.clear();
   mRestPos.clear();
   mPinned.clear();

   if (input == nullptr)
      return;
   Mesh srcLocal;
   if (InstanceOnPointsNode* instancer = FindInstancer(input))
   {
      srcLocal = MeshOps::RealizeInstances(input->GetMesh(), ResolveInstanceTransforms(input, instancer), input->GetInstanceGroupMatrix(), &instancer->InstanceColors(), 256);
   }
   else
   {
      srcLocal = input->GetMesh();
   }
   if (srcLocal.Empty())
      return;

   // The input's own transform sets the rest pose the cloth drapes from - a
   // draped cube that gets rotated upstream should re-drape rotated, not
   // stay pinned to its old, now-stale, world-space shape.
   const Mesh src = MeshOps::Transform(srcLocal, input->GetModelMatrix());

   mRest = src;
   mMesh = src;
   mWeld = MeshOps::BuildWeldMap(src);

   // Simulated points are the welded representatives. The map is compacted to a
   // dense range so the solver walks contiguous arrays rather than a sparse set.
   std::map<unsigned int, unsigned int> compact;
   for (size_t i = 0; i < mWeld.size(); i++)
   {
      const unsigned int rep = mWeld[i];
      if (compact.find(rep) == compact.end())
      {
         const unsigned int slot = (unsigned int)compact.size();
         compact[rep] = slot;
      }
   }
   for (size_t i = 0; i < mWeld.size(); i++)
      mWeld[i] = compact[mWeld[i]];

   const size_t pointCount = compact.size();
   mPos.resize(pointCount * 3);
   mPrev.resize(pointCount * 3);
   mRestPos.resize(pointCount * 3);
   mPinned.assign(pointCount, 0);

   for (size_t i = 0; i < src.vertices.size(); i++)
   {
      const unsigned int p = mWeld[i];
      mPos[p * 3 + 0] = src.vertices[i].px;
      mPos[p * 3 + 1] = src.vertices[i].py;
      mPos[p * 3 + 2] = src.vertices[i].pz;
   }
   mPrev = mPos;
   mRestPos = mPos;

   // One distance constraint per unique edge, at its rest length.
   std::set<std::pair<unsigned int, unsigned int>> seen;
   for (size_t t = 0; t + 2 < src.indices.size(); t += 3)
   {
      const unsigned int v[3] = { mWeld[src.indices[t]], mWeld[src.indices[t + 1]],
                                  mWeld[src.indices[t + 2]] };
      for (int e = 0; e < 3; e++)
      {
         const unsigned int a = v[e];
         const unsigned int b = v[(e + 1) % 3];
         if (a == b)
            continue;
         const auto key = std::minmax(a, b);
         if (!seen.insert({ key.first, key.second }).second)
            continue;
         const float dx = mPos[a * 3 + 0] - mPos[b * 3 + 0];
         const float dy = mPos[a * 3 + 1] - mPos[b * 3 + 1];
         const float dz = mPos[a * 3 + 2] - mPos[b * 3 + 2];
         mConstraints.push_back({ a, b, std::sqrt(dx*dx + dy*dy + dz*dz) });
      }
   }

   // Pinning is expressed against the rest bounding box, so it means the same
   // thing whatever mesh is patched in.
   float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
   for (size_t p = 0; p < pointCount; p++)
   {
      for (int k = 0; k < 3; k++)
      {
         lo[k] = std::min(lo[k], mRestPos[p * 3 + k]);
         hi[k] = std::max(hi[k], mRestPos[p * 3 + k]);
      }
   }
   const float epsX = std::max(1e-4f, (hi[0] - lo[0]) * 0.02f);
   const float epsY = std::max(1e-4f, (hi[1] - lo[1]) * 0.02f);
   const float epsZ = std::max(1e-4f, (hi[2] - lo[2]) * 0.02f);

   for (size_t p = 0; p < pointCount; p++)
   {
      const float x = mRestPos[p * 3 + 0];
      const float y = mRestPos[p * 3 + 1];
      const float z = mRestPos[p * 3 + 2];
      switch (pinMode)
      {
         case kPinTop:
            if (y >= hi[1] - epsY)
               mPinned[p] = 1;
            break;
         case kPinCorners:
            if ((x <= lo[0] + epsX || x >= hi[0] - epsX) &&
                (z <= lo[2] + epsZ || z >= hi[2] - epsZ) &&
                (y >= hi[1] - epsY || (hi[1] - lo[1]) < epsY))
               mPinned[p] = 1;
            break;
         case kPinEdges:
            if (x <= lo[0] + epsX || x >= hi[0] - epsX ||
                z <= lo[2] + epsZ || z >= hi[2] - epsZ)
               mPinned[p] = 1;
            break;
         case kPinNone:
         default:
            break;
      }
   }

   mRevision = NextMeshRevision();
}

void ClothNode::Reset()
{
   mPos = mRestPos;
   mPrev = mRestPos;
   mAccumulator = 0.0f;
   mElapsed = 0.0f;
   mLastBeats = -1.0;

   for (size_t i = 0; i < mMesh.vertices.size() && i < mRest.vertices.size(); i++)
      mMesh.vertices[i] = mRest.vertices[i];
   mRevision = NextMeshRevision();
}

void ClothNode::Step(float dt, float elapsed)
{
   const size_t pointCount = mPinned.size();
   if (pointCount == 0)
      return;

   const float invMass = 1.0f / std::max(0.01f, mass);
   // Verlet: velocity is implied by the gap between this position and the last,
   // which is what lets a constraint projection double as a velocity change.
   const float retain = std::max(0.0f, 1.0f - damping);

   for (size_t p = 0; p < pointCount; p++)
   {
      if (mPinned[p])
      {
         mPrev[p * 3 + 0] = mPos[p * 3 + 0];
         mPrev[p * 3 + 1] = mPos[p * 3 + 1];
         mPrev[p * 3 + 2] = mPos[p * 3 + 2];
         continue;
      }

      float ax = gravityX + windX;
      float ay = gravityY + windY;
      float az = gravityZ + windZ;

      if (windTurbulence > 0.0f)
      {
         const float x = mPos[p * 3 + 0], y = mPos[p * 3 + 1], z = mPos[p * 3 + 2];
         ax += Noise3(x + elapsed, y, z) * windTurbulence;
         ay += Noise3(y, z + elapsed, x) * windTurbulence;
         az += Noise3(z, x, y + elapsed) * windTurbulence;
      }

      for (int k = 0; k < 3; k++)
      {
         const float accel = (k == 0) ? ax : (k == 1) ? ay : az;
         const float current = mPos[p * 3 + k];
         const float velocity = (current - mPrev[p * 3 + k]) * retain;
         mPrev[p * 3 + k] = current;
         mPos[p * 3 + k] = current + velocity + accel * invMass * dt * dt;
      }
   }

   // Gauss-Seidel constraint projection. More iterations means a stiffer,
   // less stretchy cloth; this is the knob that actually controls stiffness.
   const int passes = std::max(1, std::min(iterations, 40));
   const float k = std::max(0.0f, std::min(stiffness, 1.0f));
   for (int pass = 0; pass < passes; pass++)
   {
      for (const Constraint& c : mConstraints)
      {
         float dx = mPos[c.b * 3 + 0] - mPos[c.a * 3 + 0];
         float dy = mPos[c.b * 3 + 1] - mPos[c.a * 3 + 1];
         float dz = mPos[c.b * 3 + 2] - mPos[c.a * 3 + 2];
         const float len = std::sqrt(dx*dx + dy*dy + dz*dz);
         if (len < 1e-6f)
            continue;

         const float correction = (len - c.rest) / len * k;
         const bool aFixed = mPinned[c.a] != 0;
         const bool bFixed = mPinned[c.b] != 0;
         if (aFixed && bFixed)
            continue;

         // A pinned endpoint takes none of the correction, so the free end
         // absorbs all of it rather than dragging the pin.
         const float shareA = aFixed ? 0.0f : (bFixed ? 1.0f : 0.5f);
         const float shareB = bFixed ? 0.0f : (aFixed ? 1.0f : 0.5f);

         mPos[c.a * 3 + 0] += dx * correction * shareA;
         mPos[c.a * 3 + 1] += dy * correction * shareA;
         mPos[c.a * 3 + 2] += dz * correction * shareA;
         mPos[c.b * 3 + 0] -= dx * correction * shareB;
         mPos[c.b * 3 + 1] -= dy * correction * shareB;
         mPos[c.b * 3 + 2] -= dz * correction * shareB;
      }

      if (shapeRetention > 0.0f)
      {
         const float pull = std::min(1.0f, shapeRetention) * 0.5f;
         for (size_t p = 0; p < pointCount; p++)
         {
            if (mPinned[p])
               continue;
            for (int c = 0; c < 3; c++)
               mPos[p * 3 + c] += (mRestPos[p * 3 + c] - mPos[p * 3 + c]) * pull * dt;
         }
      }
   }

   if (groundEnabled)
   {
      for (size_t p = 0; p < pointCount; p++)
      {
         if (mPinned[p] || mPos[p * 3 + 1] >= groundHeight)
            continue;
         const float penetration = groundHeight - mPos[p * 3 + 1];
         mPos[p * 3 + 1] = groundHeight;
         // Reflecting the previous position is what turns a position fix into
         // a bounce, since velocity here is just the gap between the two.
         const float vy = mPrev[p * 3 + 1] - mPos[p * 3 + 1];
         mPrev[p * 3 + 1] = mPos[p * 3 + 1] - vy * std::max(0.0f, std::min(bounce, 1.0f));
         const float slide = std::max(0.0f, 1.0f - friction);
         mPrev[p * 3 + 0] = mPos[p * 3 + 0] - (mPos[p * 3 + 0] - mPrev[p * 3 + 0]) * slide;
         mPrev[p * 3 + 2] = mPos[p * 3 + 2] - (mPos[p * 3 + 2] - mPrev[p * 3 + 2]) * slide;
         (void)penetration;
      }
   }
}

Mat4 ClothNode::GetModelMatrix() const
{
   if (bypassed)
      return input ? input->GetModelMatrix() : Mat4::Identity();
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material ClothNode::GetMaterial() const
{
   if (bypassed)
      return input ? input->GetMaterial() : Material();
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   m.ior = ior;
   m.transmission = transmission;
   m.transmissionRoughness = transmissionRoughness;
   m.specular = specular;
   m.clearcoat = clearcoat;
   m.clearcoatRoughness = clearcoatRoughness;
   m.subsurface = subsurface;
   m.subsurfaceColor[0] = subsurfaceColor[0];
   m.subsurfaceColor[1] = subsurfaceColor[1];
   m.subsurfaceColor[2] = subsurfaceColor[2];
   m.subsurfaceRadius = subsurfaceRadius;
   return m;
}

unsigned long long ClothNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

unsigned int ClothNode::GetSurfaceTexture()
{
   return input ? input->GetSurfaceTexture() : 0;
}

void ClothNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   // Bypassed: forward the upstream mesh untouched (see GetMesh()) and do not
   // step the solver - no point spending the CPU on a simulation nothing is
   // reading, and kMaxCatchUp already bounds the dt jump on resume the same
   // way a long-paused transport does.
   if (bypassed)
   {
      mCookWarning.clear();
      return;
   }
   mCookWarning = DescribeGeometryMismatch(input, GeometryRequirement::kMeshSurface);

   // The rest state is rebuilt when the incoming *topology* changes: a
   // different vertex/index count invalidates every constraint index. A mesh
   // revision bump with the same counts (the input just moved its vertices -
   // a Transform, a Displace, an animated texture driving one) is left alone,
   // so the simulation keeps draping instead of snapping back to rest every
   // time its upstream re-cooks.
   const unsigned long long upstreamRevision = input ? input->MeshRevision() : 0;
   const Mat4 inputModel = input ? input->GetModelMatrix() : Mat4::Identity();
   size_t vertexCount = 0, indexCount = 0;
   InstanceOnPointsNode* instancer = input ? FindInstancer(input) : nullptr;
   if (instancer != nullptr)
   {
      const std::vector<Mat4>& xforms = ResolveInstanceTransforms(input, instancer);
      const size_t instCount = std::min((size_t)256, xforms.size());
      const Mesh& peek = input->GetMesh();
      vertexCount = peek.vertices.size() * instCount;
      indexCount = peek.indices.size() * instCount;
   }
   else if (input != nullptr)
   {
      const Mesh& peek = input->GetMesh();
      vertexCount = peek.vertices.size();
      indexCount = peek.indices.size();
   }
   const bool topologyChanged = vertexCount != mBuiltVertexCount || indexCount != mBuiltIndexCount;
   if (!mHasBuilt || mBuiltInput != (const void*)input ||
       (mBuiltUpstream != upstreamRevision && topologyChanged) ||
       mBuiltPinMode != pinMode || !(mBuiltInputModel == inputModel))
   {
      RebuildFromInput();
      mBuiltInput = input;
      mBuiltUpstream = upstreamRevision;
      mBuiltPinMode = pinMode;
      mBuiltInputModel = inputModel;
      mBuiltVertexCount = vertexCount;
      mBuiltIndexCount = indexCount;
      mHasBuilt = true;
      mLastBeats = -1.0;
      mElapsed = 0.0f;
      mAccumulator = 0.0f;
   }
   else
   {
      // Topology held but the upstream still re-cooked (new positions, or just
      // a re-evaluation with nothing to report) - track its revision so the
      // next real topology change is still detected correctly.
      mBuiltUpstream = upstreamRevision;
   }

   if (mPinned.empty())
      return;

   const double beats = Transport::Instance().Beats();
   const float bpm = std::max(1.0f, Transport::Instance().Tempo());

   if (mLastBeats < 0.0 || beats < mLastBeats)
   {
      Reset();
      mLastBeats = beats;
      return;
   }

   const double deltaBeats = beats - mLastBeats;
   mLastBeats = beats;
   float dt = (float)(deltaBeats * 60.0 / (double)bpm);
   if (dt <= 0.0f)
      return; // paused
   dt = std::min(dt, kMaxCatchUp);

   mAccumulator += dt;
   int steps = 0;
   const int maxSteps = (int)(kMaxCatchUp / kFixedStep) + 1;
   while (mAccumulator >= kFixedStep && steps < maxSteps)
   {
      mElapsed += kFixedStep;
      Step(kFixedStep, mElapsed);
      mAccumulator -= kFixedStep;
      steps++;
   }

   if (steps == 0)
      return;

   // Write the simulated points back out through the weld map, so split
   // vertices move together and the seams stay closed.
   for (size_t i = 0; i < mMesh.vertices.size(); i++)
   {
      const unsigned int p = mWeld[i];
      mMesh.vertices[i].px = mPos[p * 3 + 0];
      mMesh.vertices[i].py = mPos[p * 3 + 1];
      mMesh.vertices[i].pz = mPos[p * 3 + 2];
   }
   mMesh = MeshOps::RecalculateNormals(mMesh, false, false);
   mRevision = NextMeshRevision();
}

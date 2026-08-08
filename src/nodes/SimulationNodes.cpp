#include "SimulationNodes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "Transport.h"

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
   mRandState = (unsigned int)(seed * 1000.0f) + 1u;
   mRevision = NextMeshRevision();
}

void ParticleSystemNode::Emit(int count)
{
   const int cap = std::max(0, std::min(maxParticles, 200000));
   for (int i = 0; i < count; i++)
   {
      Particle p;

      // Spawn position by emitter shape.
      switch (emitShape)
      {
         case kSphere:
         {
            // Rejection-free direction, then a cube-root radius so the interior
            // fills evenly instead of clumping at the centre.
            const float u = NextSigned(mRandState);
            const float theta = NextFloat(mRandState) * 6.28318530718f;
            const float r = std::cbrt(NextFloat(mRandState)) * emitRadius;
            const float s = std::sqrt(std::max(0.0f, 1.0f - u * u));
            p.px = r * s * std::cos(theta);
            p.py = r * u;
            p.pz = r * s * std::sin(theta);
            break;
         }
         case kBox:
            p.px = NextSigned(mRandState) * emitRadius;
            p.py = NextSigned(mRandState) * emitRadius;
            p.pz = NextSigned(mRandState) * emitRadius;
            break;
         case kDisc:
         {
            const float theta = NextFloat(mRandState) * 6.28318530718f;
            const float r = std::sqrt(NextFloat(mRandState)) * emitRadius;
            p.px = r * std::cos(theta);
            p.py = 0.0f;
            p.pz = r * std::sin(theta);
            break;
         }
         case kPoint:
         default:
            break;
      }

      // Velocity: the emitter direction, blended toward a random direction by
      // spread, so spread 0 is a beam and 1 is omnidirectional.
      float dir[3] = { dirX, dirY, dirZ };
      const float dirLen = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
      if (dirLen > 1e-5f) { dir[0] /= dirLen; dir[1] /= dirLen; dir[2] /= dirLen; }
      else { dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f; }

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
      p.vx = vel[0] * speed;
      p.vy = vel[1] * speed;
      p.vz = vel[2] * speed;

      p.life = std::max(0.05f, lifetime * (1.0f + NextSigned(mRandState) * lifetimeRandom));
      p.age = 0.0f;
      p.scale = startSize;
      p.r = startColor[0]; p.g = startColor[1]; p.b = startColor[2];
      p.alive = true;

      // Reuse a dead slot before growing: at steady state the array stops
      // reallocating entirely.
      bool placed = false;
      for (size_t s = 0; s < mParticles.size(); s++)
      {
         if (!mParticles[s].alive)
         {
            mParticles[s] = p;
            placed = true;
            break;
         }
      }
      if (!placed)
      {
         if ((int)mParticles.size() >= cap)
            return; // at capacity and nothing recyclable; drop the rest
         mParticles.push_back(p);
      }
   }
}

void ParticleSystemNode::Step(float dt)
{
   const float dragFactor = std::max(0.0f, 1.0f - drag * dt);

   for (Particle& p : mParticles)
   {
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

      if (turbulence > 0.0f)
      {
         const float s = turbulenceScale;
         p.vx += Noise3(p.px * s, p.py * s, p.pz * s) * turbulence * dt;
         p.vy += Noise3(p.py * s + 31.4f, p.pz * s, p.px * s) * turbulence * dt;
         p.vz += Noise3(p.pz * s, p.px * s + 17.1f, p.py * s) * turbulence * dt;
      }

      p.vx *= dragFactor;
      p.vy *= dragFactor;
      p.vz *= dragFactor;

      p.px += p.vx * dt;
      p.py += p.vy * dt;
      p.pz += p.vz * dt;

      const float t = (p.life > 0.0f) ? std::min(1.0f, p.age / p.life) : 0.0f;
      p.scale = startSize + (endSize - startSize) * t;
      p.r = startColor[0] + (endColor[0] - startColor[0]) * t;
      p.g = startColor[1] + (endColor[1] - startColor[1]) * t;
      p.b = startColor[2] + (endColor[2] - startColor[2]) * t;
   }
}

void ParticleSystemNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const double beats = Transport::Instance().Beats();
   const float bpm = std::max(1.0f, Transport::Instance().Tempo());

   // Rewinding must put the simulation back to nothing, or a patch's look
   // depends on how long it was left running before the rewind.
   if (mLastBeats < 0.0 || beats < mLastBeats)
   {
      Reset();
      mLastBeats = beats;
      return;
   }

   // Beats to seconds, so tempo changes retime the simulation the same way they
   // retime every other time-based node.
   const double deltaBeats = beats - mLastBeats;
   mLastBeats = beats;
   float dt = (float)(deltaBeats * 60.0 / (double)bpm);
   if (dt <= 0.0f)
      return; // paused: the clock did not move, so neither does the simulation
   dt = std::min(dt, kMaxCatchUp);

   mAccumulator += dt;
   int steps = 0;
   const int maxSteps = (int)(kMaxCatchUp / kFixedStep) + 1;
   while (mAccumulator >= kFixedStep && steps < maxSteps)
   {
      mEmitCarry += emitRate * kFixedStep;
      const int toEmit = (int)mEmitCarry;
      if (toEmit > 0)
      {
         mEmitCarry -= (float)toEmit;
         Emit(toEmit);
      }
      Step(kFixedStep);
      mAccumulator -= kFixedStep;
      steps++;
   }

   mAliveCount = 0;
   for (const Particle& p : mParticles)
      if (p.alive)
         mAliveCount++;

   if (steps > 0)
      mRevision = NextMeshRevision();
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
   const Mesh& srcLocal = input->GetMesh();
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
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material ClothNode::GetMaterial() const
{
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
   return m;
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

   // The rest state is rebuilt when the incoming mesh changes at all: a
   // different topology invalidates every constraint index.
   const unsigned long long upstreamRevision = input ? input->MeshRevision() : 0;
   const Mat4 inputModel = input ? input->GetModelMatrix() : Mat4::Identity();
   if (mBuiltInput != (const void*)input || mBuiltUpstream != upstreamRevision ||
       mBuiltPinMode != pinMode || !(mBuiltInputModel == inputModel))
   {
      RebuildFromInput();
      mBuiltInput = input;
      mBuiltUpstream = upstreamRevision;
      mBuiltPinMode = pinMode;
      mBuiltInputModel = inputModel;
      mLastBeats = -1.0;
      mElapsed = 0.0f;
      mAccumulator = 0.0f;
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

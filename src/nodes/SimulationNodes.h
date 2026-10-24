#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// Simulations: nodes that carry state forward between frames rather than being
// a pure function of their parameters.
//
// This is the same shape the Feedback family already uses in 2D. The rules that
// keep it predictable, and which every node here follows:
//
//   - Time comes from the Transport, never the wall clock, so pausing freezes
//     the simulation and does not merely stop drawing it.
//   - Stepping uses a fixed timestep with an accumulator. A variable step makes
//     a spring solver behave differently at 30fps than at 120, and a long frame
//     (a file dialog, a window drag) can blow it up outright.
//   - Rewinding the transport resets the state, so a patch is reproducible
//     rather than depending on how long it happened to be left running.

// --- Particle System ----------------------------------------------------
// Emits a point cloud. Patch it into the "cloud" pin of Instance on Points to
// stamp a shape at every particle - the renderer draws all of them in a single
// instanced call, so the practical limit is the simulation, not the drawing.
class ParticleSystemNode : public INode, public IGeometrySource
{
public:
   enum EmitShape { kPoint = 0, kSphere, kBox, kDisc, kEmitShapeCount };

   // The GPU simulation's ceiling (one 1024 x 1024 state texture); the CPU
   // fallback (INFINITE_GPU_POINTS=0, or no float render targets) keeps its
   // old 200k.
   static constexpr int kGpuMaxParticles = 1024 * 1024;
   static constexpr int kCpuMaxParticles = 200000;

   static INode* Create() { return new ParticleSystemNode(); }
   static const std::vector<std::string>& EmitShapeNames();
   ~ParticleSystemNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   const std::string& CookWarning() const override { return mCookWarning; }

   // No mesh of its own - a pure point cloud. See GetPointCloud() below for
   // the component that actually carries its data.
   // Bypassed with an emit input wired, that input goes straight through.
   INode* BypassSource() override { return dynamic_cast<INode*>(emitSource); }
   const Mesh& GetMesh() override
   {
      static Mesh empty;
      return bypassed && emitSource ? emitSource->GetMesh() : empty;
   }
   unsigned long long MeshRevision() override { return bypassed && emitSource ? emitSource->MeshRevision() : 0; }
   Mat4 GetModelMatrix() const override { return bypassed && emitSource ? emitSource->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return bypassed && emitSource ? emitSource->GetMaterial() : Material(); }
   unsigned int GetSurfaceTexture() override { return bypassed && emitSource ? emitSource->GetSurfaceTexture() : 0; }
   const Polyline* GetCurve() override { return bypassed && emitSource ? emitSource->GetCurve() : nullptr; }

   // On the GPU path the particles live in textures (GetGpuPoints()); these
   // read them back the first time a CPU consumer asks in a cook.
   const std::vector<Particle>& GetPoints() { EnsureCpuParticles(); return mParticles; }
   unsigned long long PointRevision() { return mRevision; }
   const std::vector<Particle>* GetPointCloud() override
   {
      if (bypassed)
         return emitSource ? emitSource->GetPointCloud() : nullptr;
      EnsureCpuParticles();
      return &mParticles;
   }
   unsigned long long PointCloudRevision() override
   {
      return bypassed ? (emitSource ? emitSource->PointCloudRevision() : 0) : mRevision;
   }
   const GpuPoints* GetGpuPoints() override;

   // Alive particles. On the GPU path it comes from an occlusion query and
   // lags the simulation by a cook.
   size_t AliveCount() const;
   bool OnGpu() const { return mGpuActive; }
   // Threads used to step the particles: 0 picks from the machine (up to 8). Tests set it to compare.
   static int sMaxThreads;
   void Reset();

   // Optional geometry to be born on: a mesh's surface, or a cloud's points.
   IGeometrySource* emitSource = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &emitSource : nullptr; }
   const char* InputLabel(int) const override { return "emit"; }

   int maxParticles = 4000;
   float emitRate = 200.0f;      // particles per second
   int emitShape = kSphere;
   float emitRadius = 0.2f;
   float lifetime = 3.0f;
   float lifetimeRandom = 0.4f;

   float initialSpeed = 1.0f;
   float speedRandom = 0.5f;
   float spread = 0.4f;          // 0 = straight along the direction, 1 = all ways
   float dirX = 0.0f, dirY = 1.0f, dirZ = 0.0f;

   float gravityX = 0.0f, gravityY = -1.5f, gravityZ = 0.0f;
   float drag = 0.1f;
   float turbulence = 0.0f;
   float turbulenceScale = 1.0f;

   float startSize = 0.06f;
   float endSize = 0.02f;
   float startColor[3] = { 1.0f, 0.75f, 0.3f };
   float endColor[3] = { 0.6f, 0.1f, 0.05f };

   float seed = 0.0f;

   // Born on the "emit" pin's geometry instead of the shape (when connected).
   bool emitFromInput = false;
   // Added along the birth point's normal: a surface's normal, the radial
   // direction of the sphere and box shapes, up for the disc, the launch
   // direction for a point.
   float speedAlongNormal = 0.0f;
   // Emitting from geometry: each particle keeps the colour of the point or
   // surface it was born on for its whole life (start/end colour unused).
   bool copySourceColor = true;
   // Drag and turbulence are divided by it; gravity is not.
   float mass = 1.0f;
   // Multiplies the simulation clock: 0 freezes, 4 runs four times as fast.
   float timeScale = 1.0f;
   // Shape of the start->end size and colour blends over a life:
   // t^curve, so 1 is linear and both endpoints are always reached.
   float sizeCurve = 1.0f;
   float colorCurve = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("maxParticles", maxParticles); v.Float("emitRate", emitRate);
      v.Int("emitShape", emitShape); v.Float("emitRadius", emitRadius);
      v.Float("lifetime", lifetime); v.Float("lifetimeRandom", lifetimeRandom);
      v.Float("initialSpeed", initialSpeed); v.Float("speedRandom", speedRandom);
      v.Float("spread", spread);
      v.Float("dirX", dirX); v.Float("dirY", dirY); v.Float("dirZ", dirZ);
      v.Float("gravityX", gravityX); v.Float("gravityY", gravityY); v.Float("gravityZ", gravityZ);
      v.Float("drag", drag); v.Float("turbulence", turbulence);
      v.Float("turbulenceScale", turbulenceScale);
      v.Float("startSize", startSize); v.Float("endSize", endSize);
      v.Color("startColor", startColor); v.Color("endColor", endColor);
      v.Float("seed", seed);
      v.Bool("emitFromInput", emitFromInput); v.Float("speedAlongNormal", speedAlongNormal);
      v.Bool("copySourceColor", copySourceColor); v.Float("mass", mass);
      v.Float("timeScale", timeScale);
      v.Float("sizeCurve", sizeCurve); v.Float("colorCurve", colorCurve);
   }

private:
   // A birth point: world-space position, normal and colour.
   struct EmitSample
   {
      float p[3];
      float n[3];
      float c[3];
   };

   void Step(float dt);
   void Emit(int count);
   void StepRange(size_t begin, size_t end, float dt);
   int Cap() const;
   void PullEmitSamples();
   void BirthShape(unsigned int& rng, float pos[3], float nrm[3]) const;

   // GPU path (fragment-shader ping-pong; GL 4.1 has no compute).
   bool EnsureGpu(int cap);
   void ReleaseGpu();
   void ClearGpuState();
   void GpuStep(float dt, int births);
   void GpuPresent();
   void ReadCountQuery();
   void ReadBoundsIfReady();
   void UpdateGpuBounds();
   void EnsureCpuParticles();

   std::vector<Particle> mParticles;
   size_t mAliveCount = 0;
   unsigned long long mRevision = 0;
   std::string mCookWarning;

   // Leftover simulation time not yet consumed by a whole fixed step.
   float mAccumulator = 0.0f;
   double mLastBeats = -1.0;
   float mEmitCarry = 0.0f;   // fractional particles owed from the last step
   unsigned int mRandState = 1;
   int mLastCookFrame = -1;

   // Birth points from the emit pin, rebuilt when its geometry changes.
   std::vector<EmitSample> mEmitSamples;
   const void* mEmitSamplesFrom = nullptr;
   unsigned long long mEmitSamplesRevision = 0;
   Mat4 mEmitSamplesModel;
   bool mEmitSamplesCloud = false;
   size_t mEmitSamplesInstances = 0;
   unsigned long long mEmitSamplesStamp = 0;   // bumps on every rebuild (the GPU table follows it)
   bool mEmitHasBounds = false;
   float mEmitLo[3] = { 0, 0, 0 };
   float mEmitHi[3] = { 0, 0, 0 };
   // The emit pin's GPU points, when it has them (sampled in the shader).
   const GpuPoints* mEmitGpu = nullptr;
   Mat4 mEmitGpuModel;
   bool EmittingFromInput() const { return emitFromInput && (mEmitGpu != nullptr || !mEmitSamples.empty()); }

   bool mGpuActive = false;        // this run is simulated on the GPU
   bool mGpuFailed = false;        // shaders or targets unavailable: CPU from now on
   bool mGpuClearPending = true;   // Reset() ran: clear the state textures at the next cook
   int mGpuCap = 0;                // slots in the state textures
   int mGpuTexH = 0;
   unsigned int mSimFbo[2] = { 0, 0 };
   unsigned int mSimTex[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };   // pos+age, vel+life, colour
   int mSimRead = 0;               // which of the pair holds the current state
   unsigned int mSimProgram = 0;
   unsigned int mPresentProgram = 0;
   unsigned int mBoundsProgram = 0;
   unsigned int mEmitTex = 0;      // mEmitSamples as texels (3 per sample)
   int mEmitTexCount = 0;
   unsigned long long mEmitTexStamp = 0;
   GpuPointsGL::Target mOut;
   GpuPoints mGpuPoints;
   unsigned long long mBirths = 0; // since the last reset: the ring cursor and each birth's id
   unsigned int mCountQuery = 0;
   bool mCountPending = false;
   bool mCountStale = false;       // the last present ran with a count already in flight
   unsigned int mResets = 0;       // Reset() calls, so a count from before one is dropped
   unsigned int mCountResets = 0;
   long long mLiveCount = 0;
   // Bounds: a min/max reduction read back through a PBO a cook late.
   unsigned int mBoundsFbo[3] = { 0, 0, 0 };
   unsigned int mBoundsTex[3][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
   int mBoundsW[3] = { 0, 0, 0 };
   int mBoundsH[3] = { 0, 0, 0 };
   unsigned int mBoundsPbo = 0;
   bool mBoundsPending = false;
   void* mBoundsSync = nullptr;    // GLsync fence after the PBO read
   bool mBoundsValid = false;
   float mBoundsLo[3] = { 0, 0, 0 };
   float mBoundsHi[3] = { 0, 0, 0 };
   float mBoundsMaxSpeed = 0.0f;
   double mSimSeconds = 0.0;       // simulated time since the reset
   double mBoundsIssuedAt = 0.0;   // mSimSeconds when the pending reduction ran
   double mBoundsAt = 0.0;         // ... and when the read-back one ran
   unsigned long long mCpuRevision = 0;   // mParticles holds this revision's readback
};

// --- Cloth / Soft Body --------------------------------------------------
// Takes a mesh and simulates it as a sheet or a squashy solid, emitting the
// deformed result as ordinary geometry so it can be shaded and operated on like
// anything else.
//
// The solver is position-based dynamics rather than a force integrator: it
// moves points to satisfy constraints directly instead of accumulating spring
// forces. That is what makes it stable at large stiffness values - a mass-
// spring model needs progressively smaller timesteps as springs get stiffer,
// and explodes when it does not get them.
class ClothNode : public INode, public IGeometrySource
{
public:
   enum PinMode { kPinNone = 0, kPinTop, kPinCorners, kPinEdges, kPinModeCount };

   static INode* Create() { return new ClothNode(); }
   const std::string& CookWarning() const override { return mCookWarning; }
   static const std::vector<std::string>& PinModeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override
   {
      if (bypassed)
      {
         static const Mesh kEmptyMesh;
         return input ? input->GetMesh() : kEmptyMesh;
      }
      return mMesh;
   }
   unsigned long long MeshRevision() override { return bypassed ? (input ? input->MeshRevision() : 0) : mRevision; }
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned long long MaterialRevision() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   // Component passthrough (geometry-domains audit, Phase 4, Blender's rule):
   // Cloth only simulates GetMesh() and has no opinion on a cloud/curve
   // riding alongside it, so both forward untouched instead of being swallowed.
   const std::vector<Particle>* GetPointCloud() override
   {
      return input ? input->GetPointCloud() : nullptr;
   }
   unsigned long long PointCloudRevision() override
   {
      return input ? input->PointCloudRevision() : 0;
   }
   const Polyline* GetCurve() override { return input ? input->GetCurve() : nullptr; }
   unsigned long long CurveStamp() override { return input ? input->CurveStamp() : 0; }
   float PointBaseSize() const override { return input ? input->PointBaseSize() : 1.0f; }
   unsigned long long MappingRevision() const override
   {
      return ComputeContentRevision(GetMappingTransform(), mMappingRevision, mLastMappingHash);
   }

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
   size_t TriangleCount() const { return mMesh.indices.size() / 3; }
   size_t ConstraintCount() const { return mConstraints.size(); }
   void Reset();

   int pinMode = kPinTop;
   float stiffness = 0.9f;
   int iterations = 6;
   float damping = 0.02f;
   float mass = 1.0f;

   float gravityX = 0.0f, gravityY = -4.0f, gravityZ = 0.0f;
   float windX = 0.0f, windY = 0.0f, windZ = 0.0f;
   float windTurbulence = 0.0f;

   bool groundEnabled = false;
   float groundHeight = -1.0f;
   float bounce = 0.0f;
   float friction = 0.5f;

   // Pulls every point back toward where it started. At 0 the mesh drapes
   // freely; turned up it behaves like a soft solid that remembers its shape.
   float shapeRetention = 0.0f;

   float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
   float uniformScale = 1.0f;

   bool inheritMaterial = true;
   float color[3] = { 0.8f, 0.4f, 0.45f };
   float metallic = 0.0f;
   float roughness = 0.7f;
   float opacity = 1.0f;
   int shading = 0;
   float emissionColor[3] = { 1.0f, 0.85f, 0.6f };
   float emission = 0.0f;
   // Fresnel/dielectric response, independent of metallic/roughness - 1.5
   // matches glass and Blender's Principled BSDF default.
   float ior = 1.5f;
   // See-through / refractive path. Distinct from `opacity`, which stays a
   // cutout/dither alpha - transmission is the actual glass-like blend.
   // Screen-space approximated (see Render3D's transmission pass), not
   // raytraced: it will not look correct through thick or overlapping
   // transmissive geometry.
   float transmission = 0.0f;
   float transmissionRoughness = 0.0f;
   // Dielectric specular reflectance at normal incidence, independent of
   // metallic - Blender's "Specular" / "Specular IOR Level" slider.
   float specular = 0.5f;
   // Second, untinted Fresnel-weighted specular lobe layered on top - car
   // paint / lacquer.
   float clearcoat = 0.0f;
   float clearcoatRoughness = 0.03f;
   // Fake subsurface scattering via wrap lighting (N.L extended past the
   // terminator, tinted by subsurfaceColor) - not true multi-scatter.
   float subsurface = 0.0f;
   float subsurfaceColor[3] = { 1.0f, 0.2f, 0.1f };
   float subsurfaceRadius = 0.5f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("pinMode", pinMode); v.Float("stiffness", stiffness);
      v.Int("iterations", iterations); v.Float("damping", damping); v.Float("mass", mass);
      v.Float("gravityX", gravityX); v.Float("gravityY", gravityY); v.Float("gravityZ", gravityZ);
      v.Float("windX", windX); v.Float("windY", windY); v.Float("windZ", windZ);
      v.Float("windTurbulence", windTurbulence);
      v.Bool("groundEnabled", groundEnabled); v.Float("groundHeight", groundHeight);
      v.Float("bounce", bounce); v.Float("friction", friction);
      v.Float("shapeRetention", shapeRetention);
      v.Float("posX", posX); v.Float("posY", posY); v.Float("posZ", posZ);
      v.Float("scale", uniformScale);
      v.Bool("inherit", inheritMaterial);
      v.Color("color", color); v.Float("metallic", metallic);
      v.Float("roughness", roughness); v.Float("opacity", opacity);
      v.Int("shading", shading);
      v.Color("emissionColor", emissionColor); v.Float("emission", emission);
      v.Float("ior", ior); v.Float("transmission", transmission);
      v.Float("transmissionRoughness", transmissionRoughness);
      v.Float("specular", specular);
      v.Float("clearcoat", clearcoat); v.Float("clearcoatRoughness", clearcoatRoughness);
      v.Float("subsurface", subsurface); v.Color("subsurfaceColor", subsurfaceColor);
      v.Float("subsurfaceRadius", subsurfaceRadius);
   }

private:
   std::string mCookWarning;
   struct Constraint
   {
      unsigned int a = 0, b = 0;
      float rest = 0.0f;
   };

   void RebuildFromInput();
   void Step(float dt, float elapsed);

   Mesh mMesh;               // the deformed mesh handed downstream
   Mesh mRest;               // the input as it arrived, for reset and retention
   std::vector<Constraint> mConstraints;

   // Simulation state lives on welded points, not mesh vertices: a UV seam
   // duplicates vertices, and simulating those independently tears the surface
   // open along the seam.
   std::vector<unsigned int> mWeld;      // vertex -> simulated point
   std::vector<float> mPos, mPrev, mRestPos;
   std::vector<unsigned char> mPinned;

   unsigned long long mRevision = 0;
   mutable unsigned long long mMaterialRevision = 0;
   mutable size_t mLastMaterialHash = 0;
   mutable unsigned long long mMappingRevision = 0;
   mutable size_t mLastMappingHash = 0;
   unsigned long long mBuiltUpstream = 0;
   const void* mBuiltInput = nullptr;
   int mBuiltPinMode = -1;
   // Topology fingerprint the constraints were last built from. A revision
   // bump alone does not justify wiping the running simulation back to rest -
   // an input that only moved its vertices (a Transform, a Displace, an
   // animated texture driving one) should keep draping, not snap back every
   // time it re-cooks. Only a real vertex/index count change means the old
   // constraint indices are no longer valid.
   size_t mBuiltVertexCount = 0;
   size_t mBuiltIndexCount = 0;
   bool mHasBuilt = false;
   // The input's transform is baked into the rest state (see RebuildFromInput),
   // and it never bumps mBuiltUpstream on its own - GetModelMatrix() is a live
   // value, not a revision - so it has to be compared directly to catch a
   // pure transform edit on the upstream source.
   Mat4 mBuiltInputModel;

   float mAccumulator = 0.0f;
   double mLastBeats = -1.0;
   float mElapsed = 0.0f;
   int mLastCookFrame = -1;
};

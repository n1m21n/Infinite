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
class ParticleSystemNode : public INode, public IPointCloudSource
{
public:
   enum EmitShape { kPoint = 0, kSphere, kBox, kDisc, kEmitShapeCount };

   static INode* Create() { return new ParticleSystemNode(); }
   static const std::vector<std::string>& EmitShapeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const std::vector<Particle>& GetPoints() override { return mParticles; }
   unsigned long long PointRevision() override { return mRevision; }

   size_t AliveCount() const { return mAliveCount; }
   void Reset();

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

   float startSize = 1.0f;
   float endSize = 0.2f;
   float startColor[3] = { 1.0f, 0.75f, 0.3f };
   float endColor[3] = { 0.6f, 0.1f, 0.05f };

   float seed = 0.0f;

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
   }

private:
   void Step(float dt);
   void Emit(int count);

   std::vector<Particle> mParticles;
   size_t mAliveCount = 0;
   unsigned long long mRevision = 0;

   // Leftover simulation time not yet consumed by a whole fixed step.
   float mAccumulator = 0.0f;
   double mLastBeats = -1.0;
   float mEmitCarry = 0.0f;   // fractional particles owed from the last step
   unsigned int mRandState = 1;
   int mLastCookFrame = -1;
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
   static const std::vector<std::string>& PinModeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override { return mMesh; }
   unsigned long long MeshRevision() override { return mRevision; }
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;

   INode* BypassSource() override { return dynamic_cast<INode*>(input); }
   IGeometrySource* input = nullptr;
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
   }

private:
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
   unsigned long long mBuiltUpstream = 0;
   const void* mBuiltInput = nullptr;
   int mBuiltPinMode = -1;

   float mAccumulator = 0.0f;
   double mLastBeats = -1.0;
   float mElapsed = 0.0f;
   int mLastCookFrame = -1;
};

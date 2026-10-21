#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"

// --- Rigid Bodies -------------------------------------------------------
// Takes a mesh and drops every connected piece of it as its own rigid body:
// boxes, spheres or convex hulls that fall, collide, stack and slide on a
// ground plane (and optionally four walls). The result is ordinary geometry,
// so it can be shaded and operated on like anything else.
//
// The solver is sequential impulses at a fixed 1/120 s step. Bodies are
// ordered by their lowest vertex index and contacts are generated in a fixed
// order with no threads and no random numbers, so one patch plays back the
// same way every time, at any frame rate, on every platform.
class RigidBodyNode : public INode, public IGeometrySource
{
public:
   enum Collider { kBox = 0, kSphere, kHull, kColliderCount };

   static INode* Create() { return new RigidBodyNode(); }
   const std::string& CookWarning() const override { return mCookWarning; }
   static const std::vector<std::string>& ColliderNames();

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
   // The input's transform is baked into the simulated vertices.
   Mat4 GetModelMatrix() const override
   {
      if (bypassed)
         return input ? input->GetModelMatrix() : Mat4::Identity();
      return Mat4::Identity();
   }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned long long MaterialRevision() const override { return input ? input->MaterialRevision() : 0; }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned long long SurfaceTextureRevision() const override
   {
      return input ? input->SurfaceTextureRevision() : 0;
   }
   MappingTransform GetMappingTransform() const override
   {
      return input ? input->GetMappingTransform() : MappingTransform();
   }
   // Same component passthrough as Cloth: a cloud or curve riding alongside
   // the mesh is not this node's business and goes on untouched.
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
   size_t BodyCount() const { return mBodies.size(); }
   void Reset();

   int collider = kBox;
   float mass = 1.0f;            // per body
   int iterations = 12;
   float friction = 0.5f;
   float bounce = 0.2f;
   float linearDamping = 0.02f;
   float angularDamping = 0.05f;

   float gravityX = 0.0f, gravityY = -9.8f, gravityZ = 0.0f;

   bool groundEnabled = true;
   float groundHeight = -1.0f;
   bool wallsEnabled = false;
   float wallSize = 4.0f;        // walls stand this far from the origin on x and z

   float velX = 0.0f, velY = 0.0f, velZ = 0.0f;
   float spinX = 0.0f, spinY = 0.0f, spinZ = 0.0f;

   // A one-off kick: at "kick time" (seconds after the reset) every body's
   // velocity changes by this vector.
   float kickTime = 0.0f;
   float kickX = 0.0f, kickY = 0.0f, kickZ = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("collider", collider); v.Float("mass", mass);
      v.Int("iterations", iterations); v.Float("friction", friction); v.Float("bounce", bounce);
      v.Float("linearDamping", linearDamping); v.Float("angularDamping", angularDamping);
      v.Float("gravityX", gravityX); v.Float("gravityY", gravityY); v.Float("gravityZ", gravityZ);
      v.Bool("groundEnabled", groundEnabled); v.Float("groundHeight", groundHeight);
      v.Bool("wallsEnabled", wallsEnabled); v.Float("wallSize", wallSize);
      v.Float("velX", velX); v.Float("velY", velY); v.Float("velZ", velZ);
      v.Float("spinX", spinX); v.Float("spinY", spinY); v.Float("spinZ", spinZ);
      v.Float("kickTime", kickTime);
      v.Float("kickX", kickX); v.Float("kickY", kickY); v.Float("kickZ", kickZ);
   }

   // Solver types; public only so the .cpp's helpers can name them.
   struct Vec3 { float x = 0, y = 0, z = 0; };
   struct Quat { float w = 1, x = 0, y = 0, z = 0; };
   struct Plane { Vec3 n; float d = 0; };   // inside the body: dot(n, p) <= d
   struct Body
   {
      Vec3 pos, vel, spin;
      Quat rot;
      Vec3 half;                 // box half extents (also the inertia box)
      float radius = 0.0f;       // sphere collider radius
      bool sphere = false;
      float boundRadius = 0.0f;
      std::vector<Vec3> points;  // collision sample points, body space
      std::vector<Plane> planes; // convex shape, body space (empty for a sphere)
      std::vector<unsigned int> verts;   // mesh vertices that belong to this body
      std::vector<Vec3> local;           // those vertices, body space
      std::vector<Vec3> localNormal;
   };

private:
   void RebuildFromInput();
   void Step(float dt);

   std::string mCookWarning;
   Mesh mMesh;
   std::vector<Body> mBodies;
   unsigned long long mRevision = 0;
   mutable unsigned long long mMappingRevision = 0;
   mutable size_t mLastMappingHash = 0;
   unsigned long long mBuiltUpstream = 0;
   const void* mBuiltInput = nullptr;
   int mBuiltCollider = -1;
   size_t mBuiltVertexCount = 0;
   size_t mBuiltIndexCount = 0;
   bool mHasBuilt = false;
   Mat4 mBuiltInputModel;

   float mAccumulator = 0.0f;
   double mLastBeats = -1.0;
   float mElapsed = 0.0f;
   bool mKickDone = false;
   int mLastCookFrame = -1;
};

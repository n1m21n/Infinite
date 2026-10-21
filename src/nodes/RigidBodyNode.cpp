#include "RigidBodyNode.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

#include "Transport.h"
#include "GeometryOpNodes.h"

namespace
{
   const std::vector<std::string> kColliderNames = { "Box", "Sphere", "Convex hull" };

   // 120 Hz, the same step as Cloth and Particle System.
   const float kFixedStep = 1.0f / 120.0f;
   const float kMaxCatchUp = 0.25f;
   const size_t kMaxBodies = 256;

   using Vec3 = RigidBodyNode::Vec3;
   using Quat = RigidBodyNode::Quat;
   using Plane = RigidBodyNode::Plane;
   using Body = RigidBodyNode::Body;

   struct Mat3 { float m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; };   // row major

   Vec3 V(float x, float y, float z) { Vec3 r; r.x = x; r.y = y; r.z = z; return r; }
   Vec3 operator+(Vec3 a, Vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
   Vec3 operator-(Vec3 a, Vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
   Vec3 operator*(Vec3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
   float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   Vec3 Cross(Vec3 a, Vec3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
   float Len(Vec3 a) { return std::sqrt(Dot(a, a)); }
   Vec3 Norm(Vec3 a) { const float l = Len(a); return l > 1e-12f ? a * (1.0f / l) : V(0, 1, 0); }

   Mat3 ToMat(const Quat& q)
   {
      Mat3 r;
      const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
      const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
      const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
      r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy - wz);     r.m[2] = 2 * (xz + wy);
      r.m[3] = 2 * (xy + wz);     r.m[4] = 1 - 2 * (xx + zz); r.m[5] = 2 * (yz - wx);
      r.m[6] = 2 * (xz - wy);     r.m[7] = 2 * (yz + wx);     r.m[8] = 1 - 2 * (xx + yy);
      return r;
   }
   Vec3 Mul(const Mat3& a, Vec3 v)
   {
      return V(a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z,
               a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
               a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z);
   }
   Vec3 MulT(const Mat3& a, Vec3 v)   // transpose, i.e. the inverse of a rotation
   {
      return V(a.m[0] * v.x + a.m[3] * v.y + a.m[6] * v.z,
               a.m[1] * v.x + a.m[4] * v.y + a.m[7] * v.z,
               a.m[2] * v.x + a.m[5] * v.y + a.m[8] * v.z);
   }

   // The inertia of a box or sphere, per unit mass, in the body frame (diagonal).
   Vec3 UnitInertia(const Body& b)
   {
      if (b.sphere)
      {
         const float i = 0.4f * b.radius * b.radius;
         return V(i, i, i);
      }
      const float x = b.half.x, y = b.half.y, z = b.half.z;
      return V((y * y + z * z) / 3.0f, (x * x + z * z) / 3.0f, (x * x + y * y) / 3.0f);
   }

   struct Pose { Vec3 pos; Mat3 rot; };

   struct Contact
   {
      int a = -1, b = -1;       // b = -1: the world (ground or a wall)
      Vec3 p, n;                // n pushes a away; b gets the opposite push
      float depth = 0.0f;
      float vn0 = 0.0f;         // closing speed when the step began
      float jn = 0.0f, jt1 = 0.0f, jt2 = 0.0f;
      Vec3 t1, t2;
   };

   // A convex hull as inward-bounded planes, by brute force over the triples
   // of a small point set. Only run when the input changes.
   void BuildHull(const std::vector<Vec3>& src, Body& body)
   {
      // Extreme points along a fixed set of directions keep the set small and
      // the choice deterministic.
      std::vector<Vec3> dirs;
      for (int x = -1; x <= 1; x++)
         for (int y = -1; y <= 1; y++)
            for (int z = -1; z <= 1; z++)
               if (x != 0 || y != 0 || z != 0)
                  dirs.push_back(Norm(V((float)x, (float)y, (float)z)));
      std::vector<Vec3> pts;
      for (const Vec3& d : dirs)
      {
         size_t best = 0;
         float bestDot = -1e30f;
         for (size_t i = 0; i < src.size(); i++)
         {
            const float t = Dot(src[i], d);
            if (t > bestDot + 1e-7f) { bestDot = t; best = i; }
         }
         bool dup = false;
         for (const Vec3& q : pts)
            if (Len(q - src[best]) < 1e-5f) { dup = true; break; }
         if (!dup)
            pts.push_back(src[best]);
      }

      Vec3 cent;
      for (const Vec3& p : pts) cent = cent + p;
      if (!pts.empty()) cent = cent * (1.0f / (float)pts.size());
      float scale = 1e-3f;
      for (const Vec3& p : pts) scale = std::max(scale, Len(p - cent));
      const float eps = scale * 1e-3f;

      body.planes.clear();
      for (size_t i = 0; i < pts.size(); i++)
         for (size_t j = i + 1; j < pts.size(); j++)
            for (size_t k = j + 1; k < pts.size(); k++)
            {
               Vec3 n = Cross(pts[j] - pts[i], pts[k] - pts[i]);
               const float l = Len(n);
               if (l < scale * scale * 1e-4f)
                  continue;
               n = n * (1.0f / l);
               if (Dot(n, cent - pts[i]) > 0.0f)
                  n = n * -1.0f;
               const float d = Dot(n, pts[i]);
               bool ok = true;
               for (const Vec3& p : pts)
                  if (Dot(n, p) > d + eps) { ok = false; break; }
               if (!ok)
                  continue;
               bool dup = false;
               for (const Plane& q : body.planes)
                  if (Dot(q.n, n) > 0.9995f && std::fabs(q.d - d) < eps * 4.0f) { dup = true; break; }
               if (!dup)
               {
                  Plane pl; pl.n = n; pl.d = d;
                  body.planes.push_back(pl);
               }
            }

      body.points = pts;
      // The centre of each flat face rests better than its corners alone.
      for (const Plane& pl : body.planes)
      {
         Vec3 c; int cnt = 0;
         for (const Vec3& p : pts)
            if (std::fabs(Dot(pl.n, p) - pl.d) < eps * 4.0f) { c = c + p; cnt++; }
         if (cnt >= 3)
            body.points.push_back(c * (1.0f / (float)cnt));
      }
   }

   void BuildBox(Body& body)
   {
      const Vec3 h = body.half;
      body.planes.clear();
      const Vec3 axes[3] = { V(1, 0, 0), V(0, 1, 0), V(0, 0, 1) };
      const float ext[3] = { h.x, h.y, h.z };
      for (int a = 0; a < 3; a++)
      {
         Plane p; p.n = axes[a]; p.d = ext[a];
         body.planes.push_back(p);
         p.n = axes[a] * -1.0f;
         body.planes.push_back(p);
      }
      // Corners, edge midpoints and face centres.
      body.points.clear();
      for (int x = -1; x <= 1; x++)
         for (int y = -1; y <= 1; y++)
            for (int z = -1; z <= 1; z++)
               if (x != 0 || y != 0 || z != 0)
                  body.points.push_back(V(x * h.x, y * h.y, z * h.z));
   }

   // Everything below works on a pose snapshot taken at the start of a step.
   struct World
   {
      std::vector<Pose> poses;
   };

   // Does `p` (world space) sit inside the convex body? On a hit, pushes the
   // point out along the face it is closest to leaving through.
   bool PointInside(const Body& b, const Pose& pose, Vec3 p, Vec3& outN, float& depth)
   {
      const Vec3 l = MulT(pose.rot, p - pose.pos);
      float best = -1e30f;
      int bestPlane = -1;
      for (size_t i = 0; i < b.planes.size(); i++)
      {
         const float d = Dot(b.planes[i].n, l) - b.planes[i].d;
         if (d > best) { best = d; bestPlane = (int)i; }
      }
      if (bestPlane < 0 || best > 0.0f)
         return false;
      outN = Mul(pose.rot, b.planes[(size_t)bestPlane].n);
      depth = -best;
      return true;
   }

   void CollidePair(const std::vector<Body>& bodies, const std::vector<Pose>& poses, int ia, int ib,
                    std::vector<Contact>& out)
   {
      const Body& A = bodies[(size_t)ia];
      const Body& B = bodies[(size_t)ib];
      const Pose& pa = poses[(size_t)ia];
      const Pose& pb = poses[(size_t)ib];
      const float reach = A.boundRadius + B.boundRadius;
      if (Len(pa.pos - pb.pos) > reach)
         return;

      auto add = [&](int a, int b, Vec3 p, Vec3 n, float depth)
      {
         Contact c; c.a = a; c.b = b; c.p = p; c.n = n; c.depth = depth;
         out.push_back(c);
      };

      if (A.sphere && B.sphere)
      {
         const Vec3 d = pa.pos - pb.pos;
         const float l = Len(d);
         const float pen = A.radius + B.radius - l;
         if (pen > 0.0f)
         {
            const Vec3 n = l > 1e-8f ? d * (1.0f / l) : V(0, 1, 0);
            add(ia, ib, pb.pos + n * B.radius, n, pen);
         }
         return;
      }

      // A sphere against a convex body: the centre against the faces, and the
      // body's points against the sphere (which is what catches its corners).
      auto sphereVsPoly = [&](int is, const Body& S, const Pose& ps, int ip, const Body& P, const Pose& pp)
      {
         const Vec3 l = MulT(pp.rot, ps.pos - pp.pos);
         float best = -1e30f;
         int bestPlane = -1;
         for (size_t i = 0; i < P.planes.size(); i++)
         {
            const float d = Dot(P.planes[i].n, l) - P.planes[i].d;
            if (d > best) { best = d; bestPlane = (int)i; }
         }
         if (bestPlane >= 0 && best < S.radius)
         {
            const Vec3 n = Mul(pp.rot, P.planes[(size_t)bestPlane].n);
            add(is, ip, ps.pos - n * S.radius, n, S.radius - best);
         }
         for (const Vec3& q : P.points)
         {
            const Vec3 w = pp.pos + Mul(pp.rot, q);
            const Vec3 d = w - ps.pos;
            const float dl = Len(d);
            if (dl < S.radius && dl > 1e-8f)
               add(ip, is, w, d * (1.0f / dl), S.radius - dl);
         }
      };
      if (A.sphere) { sphereVsPoly(ia, A, pa, ib, B, pb); return; }
      if (B.sphere) { sphereVsPoly(ib, B, pb, ia, A, pa); return; }

      for (const Vec3& q : A.points)
      {
         const Vec3 w = pa.pos + Mul(pa.rot, q);
         Vec3 n; float depth;
         if (PointInside(B, pb, w, n, depth))
            add(ia, ib, w, n, depth);
      }
      for (const Vec3& q : B.points)
      {
         const Vec3 w = pb.pos + Mul(pb.rot, q);
         Vec3 n; float depth;
         if (PointInside(A, pa, w, n, depth))
            add(ib, ia, w, n, depth);
      }
   }
}

const std::vector<std::string>& RigidBodyNode::ColliderNames() { return kColliderNames; }

void RigidBodyNode::RebuildFromInput()
{
   mMesh = Mesh();
   mBodies.clear();
   if (input == nullptr)
      return;

   Mesh srcLocal;
   if (InstanceOnPointsNode* instancer = FindInstancer(input))
      srcLocal = MeshOps::RealizeInstances(input->GetMesh(), ResolveInstanceTransforms(input, instancer),
                                           input->GetInstanceGroupMatrix(), &instancer->InstanceColors(), 256);
   else
      srcLocal = input->GetMesh();
   if (srcLocal.Empty())
      return;

   const Mesh src = MeshOps::Transform(srcLocal, input->GetModelMatrix());
   mMesh = src;

   // Pieces: vertices that share a position are one point, and triangles join
   // the points they touch. Each connected piece is a body.
   const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(src);
   std::vector<unsigned int> parent(src.vertices.size());
   std::iota(parent.begin(), parent.end(), 0u);
   auto find = [&](unsigned int x)
   {
      while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
      return x;
   };
   auto join = [&](unsigned int a, unsigned int b)
   {
      a = find(a); b = find(b);
      if (a != b) parent[std::max(a, b)] = std::min(a, b);   // lowest index wins
   };
   for (size_t i = 0; i < weld.size(); i++)
      join((unsigned int)i, weld[i]);
   for (size_t t = 0; t + 2 < src.indices.size(); t += 3)
   {
      join(src.indices[t], src.indices[t + 1]);
      join(src.indices[t], src.indices[t + 2]);
   }

   std::map<unsigned int, size_t> bodyOf;   // root -> body, in order of lowest vertex
   for (size_t i = 0; i < src.vertices.size(); i++)
   {
      const unsigned int root = find((unsigned int)i);
      auto it = bodyOf.find(root);
      size_t index;
      if (it == bodyOf.end())
      {
         index = std::min(mBodies.size(), kMaxBodies - 1);
         if (index == mBodies.size())
            mBodies.emplace_back();
         bodyOf[root] = index;
      }
      else
         index = it->second;
      mBodies[index].verts.push_back((unsigned int)i);
   }

   for (Body& b : mBodies)
   {
      Vec3 lo = V(1e30f, 1e30f, 1e30f), hi = V(-1e30f, -1e30f, -1e30f);
      for (unsigned int vi : b.verts)
      {
         const Vertex& v = src.vertices[vi];
         lo = V(std::min(lo.x, v.px), std::min(lo.y, v.py), std::min(lo.z, v.pz));
         hi = V(std::max(hi.x, v.px), std::max(hi.y, v.py), std::max(hi.z, v.pz));
      }
      b.pos = (lo + hi) * 0.5f;
      b.rot = Quat();
      b.half = V(std::max(0.005f, (hi.x - lo.x) * 0.5f), std::max(0.005f, (hi.y - lo.y) * 0.5f),
                 std::max(0.005f, (hi.z - lo.z) * 0.5f));
      b.local.reserve(b.verts.size());
      b.localNormal.reserve(b.verts.size());
      for (unsigned int vi : b.verts)
      {
         const Vertex& v = src.vertices[vi];
         b.local.push_back(V(v.px, v.py, v.pz) - b.pos);
         b.localNormal.push_back(V(v.nx, v.ny, v.nz));
      }

      b.sphere = (collider == kSphere);
      if (b.sphere)
      {
         b.radius = std::max(b.half.x, std::max(b.half.y, b.half.z));
         b.boundRadius = b.radius;
         continue;
      }
      if (collider == kHull)
      {
         BuildHull(b.local, b);
         if (b.planes.size() < 4)
            BuildBox(b);
      }
      else
         BuildBox(b);
      b.boundRadius = 0.0f;
      for (const Vec3& p : b.local)
         b.boundRadius = std::max(b.boundRadius, Len(p));
   }
}

void RigidBodyNode::Reset()
{
   // Back to the rest pose: the bodies are rebuilt from the input, then given
   // their starting velocity and spin.
   RebuildFromInput();
   for (Body& b : mBodies)
   {
      b.vel = V(velX, velY, velZ);
      b.spin = V(spinX, spinY, spinZ);
   }
   mAccumulator = 0.0f;
   mElapsed = 0.0f;
   mLastBeats = -1.0;
   mKickDone = false;
   mRevision = NextMeshRevision();
}

void RigidBodyNode::Step(float dt)
{
   const size_t n = mBodies.size();
   if (n == 0)
      return;

   const float m = std::max(0.001f, mass);
   const float invMass = 1.0f / m;
   const Vec3 g = V(gravityX, gravityY, gravityZ);

   if (!mKickDone && mElapsed >= kickTime)
   {
      mKickDone = true;
      for (Body& b : mBodies)
         b.vel = b.vel + V(kickX, kickY, kickZ);
   }

   // Velocities first, then contacts from the current positions, then the
   // positions move with the corrected velocities.
   const float ld = 1.0f / (1.0f + linearDamping * dt * 10.0f);
   const float ad = 1.0f / (1.0f + angularDamping * dt * 10.0f);
   for (Body& b : mBodies)
   {
      b.vel = (b.vel + g * dt) * ld;
      b.spin = b.spin * ad;
   }

   std::vector<Pose> poses(n);
   std::vector<Vec3> invI(n);
   for (size_t i = 0; i < n; i++)
   {
      poses[i].pos = mBodies[i].pos;
      poses[i].rot = ToMat(mBodies[i].rot);
      const Vec3 ui = UnitInertia(mBodies[i]);
      invI[i] = V(1.0f / (ui.x * m), 1.0f / (ui.y * m), 1.0f / (ui.z * m));
   }
   auto applyInvI = [&](size_t i, Vec3 v)
   {
      const Vec3 l = MulT(poses[i].rot, v);
      return Mul(poses[i].rot, V(l.x * invI[i].x, l.y * invI[i].y, l.z * invI[i].z));
   };

   std::vector<Contact> contacts;
   // The world: ground and walls are half-spaces; a point is in contact when
   // it is past the plane.
   struct HalfSpace { Vec3 n; float s; };
   std::vector<HalfSpace> world;
   if (groundEnabled)
      world.push_back({ V(0, 1, 0), groundHeight });
   if (wallsEnabled)
   {
      world.push_back({ V(1, 0, 0), -wallSize });
      world.push_back({ V(-1, 0, 0), -wallSize });
      world.push_back({ V(0, 0, 1), -wallSize });
      world.push_back({ V(0, 0, -1), -wallSize });
   }
   for (size_t i = 0; i < n; i++)
   {
      const Body& b = mBodies[i];
      for (const HalfSpace& h : world)
      {
         if (b.sphere)
         {
            const float pen = h.s - (Dot(h.n, b.pos) - b.radius);
            if (pen > 0.0f)
            {
               Contact c; c.a = (int)i; c.b = -1; c.n = h.n; c.depth = pen;
               c.p = b.pos - h.n * b.radius;
               contacts.push_back(c);
            }
            continue;
         }
         if (Dot(h.n, b.pos) - b.boundRadius > h.s)
            continue;
         for (const Vec3& q : b.points)
         {
            const Vec3 w = b.pos + Mul(poses[i].rot, q);
            const float pen = h.s - Dot(h.n, w);
            if (pen > 0.0f)
            {
               Contact c; c.a = (int)i; c.b = -1; c.n = h.n; c.depth = pen; c.p = w;
               contacts.push_back(c);
            }
         }
      }
   }
   for (size_t i = 0; i < n; i++)
      for (size_t j = i + 1; j < n; j++)
         CollidePair(mBodies, poses, (int)i, (int)j, contacts);

   for (Contact& c : contacts)
   {
      const Body& a = mBodies[(size_t)c.a];
      Vec3 va = a.vel + Cross(a.spin, c.p - a.pos);
      if (c.b >= 0)
      {
         const Body& b = mBodies[(size_t)c.b];
         va = va - (b.vel + Cross(b.spin, c.p - b.pos));
      }
      c.vn0 = Dot(va, c.n);
      const Vec3 ref = std::fabs(c.n.y) < 0.9f ? V(0, 1, 0) : V(1, 0, 0);
      c.t1 = Norm(Cross(c.n, ref));
      c.t2 = Cross(c.n, c.t1);
   }

   const float slop = 0.005f;
   const float beta = 0.2f / dt;
   const int iters = std::max(1, std::min(100, iterations));
   const float mu = std::max(0.0f, friction);
   const float e = std::max(0.0f, std::min(1.0f, bounce));

   auto effMass = [&](const Contact& c, Vec3 dir)
   {
      const size_t ia = (size_t)c.a;
      const Vec3 ra = c.p - mBodies[ia].pos;
      const Vec3 rna = Cross(ra, dir);
      float k = invMass + Dot(Cross(applyInvI(ia, rna), ra), dir);
      if (c.b >= 0)
      {
         const size_t ib = (size_t)c.b;
         const Vec3 rb = c.p - mBodies[ib].pos;
         const Vec3 rnb = Cross(rb, dir);
         k += invMass + Dot(Cross(applyInvI(ib, rnb), rb), dir);
      }
      return k > 1e-12f ? 1.0f / k : 0.0f;
   };
   auto relVel = [&](const Contact& c)
   {
      const Body& a = mBodies[(size_t)c.a];
      Vec3 v = a.vel + Cross(a.spin, c.p - a.pos);
      if (c.b >= 0)
      {
         const Body& b = mBodies[(size_t)c.b];
         v = v - (b.vel + Cross(b.spin, c.p - b.pos));
      }
      return v;
   };
   auto apply = [&](const Contact& c, Vec3 impulse)
   {
      Body& a = mBodies[(size_t)c.a];
      a.vel = a.vel + impulse * invMass;
      a.spin = a.spin + applyInvI((size_t)c.a, Cross(c.p - a.pos, impulse));
      if (c.b >= 0)
      {
         Body& b = mBodies[(size_t)c.b];
         b.vel = b.vel - impulse * invMass;
         b.spin = b.spin - applyInvI((size_t)c.b, Cross(c.p - b.pos, impulse));
      }
   };

   for (int it = 0; it < iters; it++)
   {
      for (Contact& c : contacts)
      {
         // Friction first, limited by the normal impulse built up so far, so
         // the normal constraint always gets the last word.
         const Vec3 dirs[2] = { c.t1, c.t2 };
         float* acc[2] = { &c.jt1, &c.jt2 };
         for (int k = 0; k < 2; k++)
         {
            const float vt = Dot(relVel(c), dirs[k]);
            float j = -vt * effMass(c, dirs[k]);
            const float limit = mu * c.jn;
            const float old = *acc[k];
            *acc[k] = std::max(-limit, std::min(limit, old + j));
            apply(c, dirs[k] * (*acc[k] - old));
         }
         const float vn = Dot(relVel(c), c.n);
         float target = beta * std::max(0.0f, c.depth - slop);
         if (c.vn0 < -1.0f)
            target = std::max(target, -e * c.vn0);
         const float j = (target - vn) * effMass(c, c.n);
         const float old = c.jn;
         c.jn = std::max(0.0f, old + j);
         apply(c, c.n * (c.jn - old));
      }
   }

   for (size_t i = 0; i < n; i++)
   {
      Body& b = mBodies[i];
      b.pos = b.pos + b.vel * dt;
      const Quat q = b.rot;
      const Vec3 w = b.spin;
      Quat dq;   // 0.5 * (0, w) * q
      dq.w = 0.5f * (-w.x * q.x - w.y * q.y - w.z * q.z);
      dq.x = 0.5f * (w.x * q.w + w.y * q.z - w.z * q.y);
      dq.y = 0.5f * (w.y * q.w + w.z * q.x - w.x * q.z);
      dq.z = 0.5f * (w.z * q.w + w.x * q.y - w.y * q.x);
      Quat r;
      r.w = q.w + dq.w * dt; r.x = q.x + dq.x * dt; r.y = q.y + dq.y * dt; r.z = q.z + dq.z * dt;
      const float l = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
      if (l > 1e-12f)
      {
         r.w /= l; r.x /= l; r.y /= l; r.z /= l;
         b.rot = r;
      }
   }

   // A body that is touching something and has all but stopped is put to rest,
   // so a stack settles instead of shimmering.
   std::vector<char> touching(n, 0);
   for (const Contact& c : contacts)
   {
      touching[(size_t)c.a] = 1;
      if (c.b >= 0) touching[(size_t)c.b] = 1;
   }
   for (size_t i = 0; i < n; i++)
   {
      Body& b = mBodies[i];
      if (touching[i] && Len(b.vel) < 0.05f && Len(b.spin) < 0.1f)
      {
         b.vel = b.vel * 0.9f;
         b.spin = b.spin * 0.9f;
      }
   }
}

void RigidBodyNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   if (bypassed)
   {
      mCookWarning.clear();
      return;
   }
   mCookWarning = DescribeGeometryMismatch(input, GeometryRequirement::kMeshSurface);

   // Rebuilt only when the incoming topology, the input transform or the
   // collider changes; an input that merely moved its vertices keeps the
   // running simulation.
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
       mBuiltCollider != collider || !(mBuiltInputModel == inputModel))
   {
      Reset();
      mBuiltInput = input;
      mBuiltUpstream = upstreamRevision;
      mBuiltCollider = collider;
      mBuiltInputModel = inputModel;
      mBuiltVertexCount = vertexCount;
      mBuiltIndexCount = indexCount;
      mHasBuilt = true;
   }
   else
      mBuiltUpstream = upstreamRevision;

   if (mBodies.empty())
      return;

   const double beats = Transport::Instance().Beats();
   const float bpm = std::max(1.0f, Transport::Instance().Tempo());

   if (mLastBeats < 0.0 || beats < mLastBeats)
   {
      const double keep = beats;
      Reset();
      mLastBeats = keep;
      return;
   }

   const double deltaBeats = beats - mLastBeats;
   mLastBeats = beats;
   float dt = (float)(deltaBeats * 60.0 / (double)bpm);
   if (dt <= 0.0f)
      return;
   dt = std::min(dt, kMaxCatchUp);

   mAccumulator += dt;
   int steps = 0;
   const int maxSteps = (int)(kMaxCatchUp / kFixedStep) + 1;
   while (mAccumulator >= kFixedStep && steps < maxSteps)
   {
      Step(kFixedStep);
      mElapsed += kFixedStep;
      mAccumulator -= kFixedStep;
      steps++;
   }
   if (steps == 0)
      return;

   for (const Body& b : mBodies)
   {
      const Mat3 r = ToMat(b.rot);
      for (size_t k = 0; k < b.verts.size(); k++)
      {
         Vertex& v = mMesh.vertices[b.verts[k]];
         const Vec3 p = b.pos + Mul(r, b.local[k]);
         const Vec3 nn = Mul(r, b.localNormal[k]);
         v.px = p.x; v.py = p.y; v.pz = p.z;
         v.nx = nn.x; v.ny = nn.y; v.nz = nn.z;
      }
   }
   mRevision = NextMeshRevision();
}

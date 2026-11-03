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

   Quat FromMat(const Mat3& r)
   {
      Quat q;
      const float* m = r.m;
      const float tr = m[0] + m[4] + m[8];
      if (tr > 0.0f)
      {
         const float s = std::sqrt(tr + 1.0f) * 2.0f;
         q.w = 0.25f * s; q.x = (m[7] - m[5]) / s; q.y = (m[2] - m[6]) / s; q.z = (m[3] - m[1]) / s;
      }
      else if (m[0] > m[4] && m[0] > m[8])
      {
         const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2.0f;
         q.w = (m[7] - m[5]) / s; q.x = 0.25f * s; q.y = (m[1] + m[3]) / s; q.z = (m[2] + m[6]) / s;
      }
      else if (m[4] > m[8])
      {
         const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2.0f;
         q.w = (m[2] - m[6]) / s; q.x = (m[1] + m[3]) / s; q.y = 0.25f * s; q.z = (m[5] + m[7]) / s;
      }
      else
      {
         const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2.0f;
         q.w = (m[3] - m[1]) / s; q.x = (m[2] + m[6]) / s; q.y = (m[5] + m[7]) / s; q.z = 0.25f * s;
      }
      const float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
      q.w /= l; q.x /= l; q.y /= l; q.z /= l;
      return q;
   }

   // The piece's own axes, from its faces: the direction with the most face
   // area, then the most-area direction at right angles to it. Columns of the
   // result are those axes in world space, ordered and signed to sit closest to
   // x, y and z, so a piece that is already square to the world keeps the
   // identity. False when the faces give no two perpendicular directions.
   bool FaceFrame(const std::vector<Vec3>& p, const std::vector<unsigned int>& tris, Mat3& out)
   {
      struct Dir { Vec3 n; float area; };
      std::vector<Dir> dirs;
      for (size_t t = 0; t + 2 < tris.size(); t += 3)
      {
         Vec3 n = Cross(p[tris[t + 1]] - p[tris[t]], p[tris[t + 2]] - p[tris[t]]);
         const float l = Len(n);
         if (l < 1e-12f)
            continue;
         n = n * (1.0f / l);
         bool merged = false;
         for (Dir& d : dirs)
         {
            const float c = Dot(d.n, n);
            if (std::fabs(c) > 0.995f)
            {
               d.area += l;
               merged = true;
               break;
            }
         }
         if (!merged)
            dirs.push_back({ n, l });
      }
      if (dirs.size() < 2)
         return false;
      std::stable_sort(dirs.begin(), dirs.end(), [](const Dir& a, const Dir& b) { return a.area > b.area; });
      const Vec3 a1 = dirs[0].n;
      int second = -1;
      for (size_t i = 1; i < dirs.size(); i++)
         if (std::fabs(Dot(dirs[i].n, a1)) < 0.05f) { second = (int)i; break; }
      if (second < 0)
         return false;
      const Vec3 a3 = Norm(Cross(a1, dirs[(size_t)second].n));
      const Vec3 a2 = Cross(a3, a1);
      Vec3 cols[3] = { a1, a2, a3 };
      // Greedy: world x, y, z each take the free axis most parallel to it.
      const Vec3 world[3] = { V(1, 0, 0), V(0, 1, 0), V(0, 0, 1) };
      bool used[3] = { false, false, false };
      Vec3 picked[3];
      for (int w = 0; w < 3; w++)
      {
         int best = -1;
         float bestDot = -1.0f;
         for (int c = 0; c < 3; c++)
            if (!used[c] && std::fabs(Dot(cols[c], world[w])) > bestDot) { bestDot = std::fabs(Dot(cols[c], world[w])); best = c; }
         used[best] = true;
         picked[w] = Dot(cols[best], world[w]) < 0.0f ? cols[best] * -1.0f : cols[best];
      }
      if (Dot(Cross(picked[0], picked[1]), picked[2]) < 0.0f)
         picked[2] = picked[2] * -1.0f;
      for (int r = 0; r < 3; r++)
      {
         out.m[r * 3 + 0] = r == 0 ? picked[0].x : r == 1 ? picked[0].y : picked[0].z;
         out.m[r * 3 + 1] = r == 0 ? picked[1].x : r == 1 ? picked[1].y : picked[1].z;
         out.m[r * 3 + 2] = r == 0 ? picked[2].x : r == 1 ? picked[2].y : picked[2].z;
      }
      return true;
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
      unsigned long long key = 0;   // the same body pair and feature next step
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

   // Body a, body b (-1 the world, with the plane in the feature's high bits)
   // and the sample point: what makes a contact the same one from step to step.
   unsigned long long ContactKey(int a, int b, unsigned int feature)
   {
      return ((unsigned long long)(unsigned int)a << 48) | ((unsigned long long)(unsigned int)(b + 1) << 32) | feature;
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

      auto add = [&](int a, int b, Vec3 p, Vec3 n, float depth, unsigned int feature)
      {
         Contact c; c.a = a; c.b = b; c.p = p; c.n = n; c.depth = depth;
         c.key = ContactKey(a, b, feature);
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
            add(ia, ib, pb.pos + n * B.radius, n, pen, 0);
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
            add(is, ip, ps.pos - n * S.radius, n, S.radius - best, 0);
         }
         for (size_t k = 0; k < P.points.size(); k++)
         {
            const Vec3 w = pp.pos + Mul(pp.rot, P.points[k]);
            const Vec3 d = w - ps.pos;
            const float dl = Len(d);
            if (dl < S.radius && dl > 1e-8f)
               add(ip, is, w, d * (1.0f / dl), S.radius - dl, (unsigned int)k);
         }
      };
      if (A.sphere) { sphereVsPoly(ia, A, pa, ib, B, pb); return; }
      if (B.sphere) { sphereVsPoly(ib, B, pb, ia, A, pa); return; }

      for (size_t k = 0; k < A.points.size(); k++)
      {
         const Vec3 w = pa.pos + Mul(pa.rot, A.points[k]);
         Vec3 n; float depth;
         if (PointInside(B, pb, w, n, depth))
            add(ia, ib, w, n, depth, (unsigned int)k);
      }
      for (size_t k = 0; k < B.points.size(); k++)
      {
         const Vec3 w = pb.pos + Mul(pb.rot, B.points[k]);
         Vec3 n; float depth;
         if (PointInside(A, pa, w, n, depth))
            add(ib, ia, w, n, depth, (unsigned int)k);
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

   // Each body's triangles, for its face frame.
   std::vector<int> bodyOfVertex(src.vertices.size(), -1);
   for (size_t bi = 0; bi < mBodies.size(); bi++)
      for (unsigned int vi : mBodies[bi].verts)
         bodyOfVertex[vi] = (int)bi;
   std::vector<std::vector<unsigned int>> bodyTris(mBodies.size());
   for (size_t t = 0; t + 2 < src.indices.size(); t += 3)
   {
      const int bi = bodyOfVertex[src.indices[t]];
      if (bi >= 0)
         for (int k = 0; k < 3; k++)
            bodyTris[(size_t)bi].push_back(src.indices[t + (size_t)k]);
   }
   std::vector<Vec3> world(src.vertices.size());
   for (size_t i = 0; i < src.vertices.size(); i++)
      world[i] = V(src.vertices[i].px, src.vertices[i].py, src.vertices[i].pz);

   for (size_t bi = 0; bi < mBodies.size(); bi++)
   {
      Body& b = mBodies[bi];
      // The box (and the hull's frame and inertia) sits on the piece's own
      // axes when that box is tighter than the world-aligned one, so a turned
      // cube is a turned cube and not its larger axis-aligned bounds.
      auto bounds = [&](const Mat3& r, Vec3& lo, Vec3& hi)
      {
         lo = V(1e30f, 1e30f, 1e30f);
         hi = V(-1e30f, -1e30f, -1e30f);
         for (unsigned int vi : b.verts)
         {
            const Vec3 l = MulT(r, world[vi]);
            lo = V(std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z));
            hi = V(std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z));
         }
      };
      Mat3 frame;
      Vec3 lo, hi;
      bounds(frame, lo, hi);
      Mat3 faces;
      if (collider != kSphere && FaceFrame(world, bodyTris[bi], faces))
      {
         Vec3 flo, fhi;
         bounds(faces, flo, fhi);
         const Vec3 e = hi - lo, fe = fhi - flo;
         if (fe.x * fe.y * fe.z < e.x * e.y * e.z * 0.99f)
         {
            frame = faces;
            lo = flo;
            hi = fhi;
         }
      }
      b.pos = Mul(frame, (lo + hi) * 0.5f);
      b.rot = FromMat(frame);
      b.half = V(std::max(0.005f, (hi.x - lo.x) * 0.5f), std::max(0.005f, (hi.y - lo.y) * 0.5f),
                 std::max(0.005f, (hi.z - lo.z) * 0.5f));
      b.local.reserve(b.verts.size());
      b.localNormal.reserve(b.verts.size());
      for (unsigned int vi : b.verts)
      {
         const Vertex& v = src.vertices[vi];
         b.local.push_back(MulT(frame, world[vi] - b.pos));
         b.localNormal.push_back(MulT(frame, V(v.nx, v.ny, v.nz)));
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
   mWarm.clear();
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
      for (size_t hi = 0; hi < world.size(); hi++)
      {
         const HalfSpace& h = world[hi];
         if (b.sphere)
         {
            const float pen = h.s - (Dot(h.n, b.pos) - b.radius);
            if (pen > 0.0f)
            {
               Contact c; c.a = (int)i; c.b = -1; c.n = h.n; c.depth = pen;
               c.p = b.pos - h.n * b.radius;
               c.key = ContactKey((int)i, -1, (unsigned int)hi << 16);
               contacts.push_back(c);
            }
            continue;
         }
         if (Dot(h.n, b.pos) - b.boundRadius > h.s)
            continue;
         for (size_t k = 0; k < b.points.size(); k++)
         {
            const Vec3 w = b.pos + Mul(poses[i].rot, b.points[k]);
            const float pen = h.s - Dot(h.n, w);
            if (pen > 0.0f)
            {
               Contact c; c.a = (int)i; c.b = -1; c.n = h.n; c.depth = pen; c.p = w;
               c.key = ContactKey((int)i, -1, ((unsigned int)hi << 16) | (unsigned int)k);
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

   const float slop = 0.002f;
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

   // Warm start: a contact that was there last step begins with last step's
   // impulses, so a resting stack carries its weight from the first iteration
   // instead of sinking a little and sliding a little every step.
   for (Contact& c : contacts)
   {
      const auto it = mWarm.find(c.key);
      if (it == mWarm.end())
         continue;
      c.jn = it->second.x;
      const float limit = mu * c.jn;
      c.jt1 = std::max(-limit, std::min(limit, it->second.y));
      c.jt2 = std::max(-limit, std::min(limit, it->second.z));
      apply(c, c.n * c.jn + c.t1 * c.jt1 + c.t2 * c.jt2);
   }

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
   mWarm.clear();
   for (const Contact& c : contacts)
      mWarm[c.key] = V(c.jn, c.jt1, c.jt2);

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

// CPU BVH self-test (INFINITE_BVHTEST) and benchmark (INFINITE_BVHBENCH)
// for Section G.2 of the 3D Realism Specification.
#include "core/Bvh.h"
#include "core/GltfImport.h"
#include "core/Mesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace app
{
namespace
{
   struct Rng
   {
      uint32_t state = 0x9E3779B9u;
      explicit Rng(uint32_t seed = 0x9E3779B9u) : state(seed ? seed : 1u) {}

      uint32_t NextU32()
      {
         uint32_t x = state;
         x ^= x << 13;
         x ^= x >> 17;
         x ^= x << 5;
         state = x;
         return x;
      }

      float NextFloat()
      {
         return (float)(NextU32() >> 8) * (1.0f / 16777216.0f);
      }

      float Range(float lo, float hi)
      {
         return lo + (hi - lo) * NextFloat();
      }
   };

   void MeshBounds(const Mesh& m, float lo[3], float hi[3])
   {
      lo[0] = lo[1] = lo[2] = 1e30f;
      hi[0] = hi[1] = hi[2] = -1e30f;
      for (const Vertex& v : m.vertices)
      {
         lo[0] = std::min(lo[0], v.px);
         lo[1] = std::min(lo[1], v.py);
         lo[2] = std::min(lo[2], v.pz);
         hi[0] = std::max(hi[0], v.px);
         hi[1] = std::max(hi[1], v.py);
         hi[2] = std::max(hi[2], v.pz);
      }
   }

   std::vector<Bvh::Ray> MakeRandomRays(int count, const float lo[3], const float hi[3], uint32_t seed)
   {
      Rng rng(seed);
      const float cx = 0.5f * (lo[0] + hi[0]);
      const float cy = 0.5f * (lo[1] + hi[1]);
      const float cz = 0.5f * (lo[2] + hi[2]);
      const float rx = std::max(0.5f, (hi[0] - lo[0]) * 0.6f);
      const float ry = std::max(0.5f, (hi[1] - lo[1]) * 0.6f);
      const float rz = std::max(0.5f, (hi[2] - lo[2]) * 0.6f);

      std::vector<Bvh::Ray> rays((size_t)count);
      for (int i = 0; i < count; i++)
      {
         Bvh::Ray r;
         r.o[0] = cx + rng.Range(-2.5f * rx, 2.5f * rx);
         r.o[1] = cy + rng.Range(-2.5f * ry, 2.5f * ry);
         r.o[2] = cz + rng.Range(-2.5f * rz, 2.5f * rz);
         const float tx = cx + rng.Range(-1.1f * rx, 1.1f * rx);
         const float ty = cy + rng.Range(-1.1f * ry, 1.1f * ry);
         const float tz = cz + rng.Range(-1.1f * rz, 1.1f * rz);
         float dx = tx - r.o[0];
         float dy = ty - r.o[1];
         float dz = tz - r.o[2];
         const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
         if (len > 1e-6f)
         {
            dx /= len;
            dy /= len;
            dz /= len;
         }
         else
         {
            dx = 0.0f; dy = 0.0f; dz = -1.0f;
         }
         r.d[0] = dx;
         r.d[1] = dy;
         r.d[2] = dz;
         r.tMin = 1e-4f;
         r.tMax = 1e30f;
         rays[(size_t)i] = r;
      }
      return rays;
   }

   struct PrecompTri
   {
      float v0[3];
      float e1[3];
      float e2[3];
   };

   std::vector<PrecompTri> PrecomputeTris(const Mesh& m)
   {
      const size_t n = m.indices.size() / 3;
      std::vector<PrecompTri> out(n);
      for (size_t t = 0; t < n; t++)
      {
         const Vertex& a = m.vertices[m.indices[t * 3 + 0]];
         const Vertex& b = m.vertices[m.indices[t * 3 + 1]];
         const Vertex& c = m.vertices[m.indices[t * 3 + 2]];
         out[t].v0[0] = a.px; out[t].v0[1] = a.py; out[t].v0[2] = a.pz;
         out[t].e1[0] = b.px - a.px; out[t].e1[1] = b.py - a.py; out[t].e1[2] = b.pz - a.pz;
         out[t].e2[0] = c.px - a.px; out[t].e2[1] = c.py - a.py; out[t].e2[2] = c.pz - a.pz;
      }
      return out;
   }

   Bvh::Hit BruteForceIntersect(const Bvh::Ray& ray, const std::vector<PrecompTri>& tris)
   {
      Bvh::Hit hit;
      hit.t = ray.tMax;
      const int n = (int)tris.size();
      for (int t = 0; t < n; t++)
      {
         float ht, hu, hv;
         if (Bvh::IntersectTriangle(ray.o, ray.d, ray.tMin, hit.t,
                                    tris[(size_t)t].v0, tris[(size_t)t].e1, tris[(size_t)t].e2,
                                    ht, hu, hv))
         {
            hit.t = ht;
            hit.triIndex = t;
            hit.u = hu;
            hit.v = hv;
         }
      }
      if (!hit.Valid())
         hit.t = 1e30f;
      return hit;
   }

   // Builds a subdivided sphere with `4 * baseRings * baseSectors * 2` triangles
   // by splitting each triangle of Primitives::Sphere(baseRings, baseSectors) into 4
   // and projecting vertices onto the sphere of radius 0.5.
   Mesh MakeSubdividedSphere1M()
   {
      const Mesh base = Primitives::Sphere(256, 512); // 262,144 triangles
      const size_t baseTris = base.indices.size() / 3;
      Mesh out;
      out.vertices.reserve(base.vertices.size() + baseTris * 3);
      out.indices.resize(baseTris * 12);
      out.vertices = base.vertices;

      auto addMid = [&](const Vertex& a, const Vertex& b) -> unsigned int
      {
         Vertex m;
         m.px = 0.5f * (a.px + b.px);
         m.py = 0.5f * (a.py + b.py);
         m.pz = 0.5f * (a.pz + b.pz);
         const float len = std::sqrt(m.px * m.px + m.py * m.py + m.pz * m.pz);
         if (len > 1e-8f)
         {
            m.nx = m.px / len;
            m.ny = m.py / len;
            m.nz = m.pz / len;
            m.px = m.nx * 0.5f;
            m.py = m.ny * 0.5f;
            m.pz = m.nz * 0.5f;
         }
         const unsigned int idx = (unsigned int)out.vertices.size();
         out.vertices.push_back(m);
         return idx;
      };

      for (size_t t = 0; t < baseTris; t++)
      {
         const unsigned int i0 = base.indices[t * 3 + 0];
         const unsigned int i1 = base.indices[t * 3 + 1];
         const unsigned int i2 = base.indices[t * 3 + 2];
         const Vertex v0 = out.vertices[i0];
         const Vertex v1 = out.vertices[i1];
         const Vertex v2 = out.vertices[i2];
         const unsigned int m01 = addMid(v0, v1);
         const unsigned int m12 = addMid(v1, v2);
         const unsigned int m20 = addMid(v2, v0);

         unsigned int* dst = &out.indices[t * 12];
         dst[0] = i0;  dst[1] = m01; dst[2] = m20;
         dst[3] = m01; dst[4] = i1;  dst[5] = m12;
         dst[6] = m20; dst[7] = m12; dst[8] = i2;
         dst[9] = m01; dst[10] = m12; dst[11] = m20;
      }
      return out;
   }

   Mesh LoadFoxOrFallback(std::string& usedPath)
   {
      std::vector<std::string> candidates;
      if (const char* env = getenv("INFINITE_3DBENCH_FOX"))
         candidates.push_back(env);
      candidates.push_back("/private/tmp/claude-501/-Users-namansoni-Infinite/373e47b9-cb26-4fd8-a885-8490241b53e6/scratchpad/assets/Fox.glb");
      const std::string self = __FILE__;
      const size_t at = self.rfind("src/app/selftest/");
      if (at != std::string::npos)
         candidates.push_back(self.substr(0, at) + "tests/render3d/skinned_two_bone.glb");

      for (const std::string& p : candidates)
      {
         if (p.empty())
            continue;
         std::string err;
         const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(p, err);
         if (pkg != nullptr && !pkg->vertices.empty() && !pkg->indices.empty())
         {
            Mesh m;
            m.vertices.resize(pkg->vertices.size());
            for (size_t i = 0; i < pkg->vertices.size(); i++)
            {
               m.vertices[i].px = pkg->vertices[i].px;
               m.vertices[i].py = pkg->vertices[i].py;
               m.vertices[i].pz = pkg->vertices[i].pz;
               m.vertices[i].nx = pkg->vertices[i].nx;
               m.vertices[i].ny = pkg->vertices[i].ny;
               m.vertices[i].nz = pkg->vertices[i].nz;
               m.vertices[i].u = pkg->vertices[i].u;
               m.vertices[i].v = pkg->vertices[i].v;
            }
            m.indices = pkg->indices;
            usedPath = p;
            return m;
         }
      }
      return Primitives::TorusKnot(64, 16, 0.2f, 2, 3);
   }

   int VerifyMeshRays(const Mesh& mesh, int numRays, uint32_t seed, const Bvh::Blas& blas)
   {
      float lo[3], hi[3];
      MeshBounds(mesh, lo, hi);
      const std::vector<Bvh::Ray> rays = MakeRandomRays(numRays, lo, hi, seed);
      const std::vector<PrecompTri> tris = PrecomputeTris(mesh);

      const unsigned int nThreads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
      std::vector<int> threadMismatches(nThreads, 0);
      std::vector<std::thread> pool;
      for (unsigned int tid = 0; tid < nThreads; tid++)
      {
         pool.emplace_back([&, tid]()
         {
            const int begin = (int)((uint64_t)tid * (uint64_t)numRays / nThreads);
            const int end = (int)((uint64_t)(tid + 1) * (uint64_t)numRays / nThreads);
            int bad = 0;
            for (int i = begin; i < end; i++)
            {
               const Bvh::Hit bvhHit = blas.Intersect(rays[(size_t)i]);
               const Bvh::Hit bfHit = BruteForceIntersect(rays[(size_t)i], tris);
               if (bvhHit.Valid() != bfHit.Valid())
               {
                  bad++;
                  continue;
               }
               if (bvhHit.Valid())
               {
                  const bool sameTri = (bvhHit.triIndex == bfHit.triIndex);
                  const bool sameT = (std::fabs(bvhHit.t - bfHit.t) <= 1e-5f * std::max(1.0f, bfHit.t));
                  if (!sameTri && !sameT)
                     bad++;
               }
            }
            threadMismatches[tid] = bad;
         });
      }
      for (std::thread& th : pool)
         th.join();

      int totalBad = 0;
      for (int b : threadMismatches)
         totalBad += b;
      return totalBad;
   }

   double MsSince(std::chrono::steady_clock::time_point t0)
   {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   }

   double Median(std::vector<double> v)
   {
      if (v.empty())
         return 0.0;
      std::sort(v.begin(), v.end());
      return v[v.size() / 2];
   }
}

// ======================================================== INFINITE_BVHTEST
bool RunBvhTest()
{
   printf("[BVH TEST] Binned-SAH BLAS, TLAS, Refit and samplerBuffer layout\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };

   // 1. UV Sphere (10,000 rays)
   const Mesh uvSphere = Primitives::Sphere(24, 48);
   Bvh::Blas sphereBlas;
   sphereBlas.Build(uvSphere, 3);
   check((int)sphereBlas.nodeTexels.size() == sphereBlas.NodeCount() * 8 &&
         (int)sphereBlas.triTexels.size() == (int)uvSphere.FaceCount() * 12,
         "samplerBuffer layout: 2 texels/node, 3 texels/tri",
         (double)sphereBlas.TriCount(), (double)uvSphere.FaceCount());

   int maxLeafCount = 0;
   for (int n = 0; n < sphereBlas.NodeCount(); n++)
   {
      const int32_t w1 = Bvh::FloatAsInt(sphereBlas.nodeTexels[(size_t)n * 8 + 7]);
      if (w1 > maxLeafCount)
         maxLeafCount = w1;
   }
   check(maxLeafCount >= 1 && maxLeafCount <= Bvh::Blas::kMaxLeafPrims,
         "BLAS leaf size <= 4 triangles", (double)maxLeafCount, (double)Bvh::Blas::kMaxLeafPrims);

   const int sphereBad = VerifyMeshRays(uvSphere, 10000, 0x1001u, sphereBlas);
   check(sphereBad == 0, "UV sphere: 10,000 random rays match brute force", (double)sphereBad, 0.0);

   // 2. Fox glTF (10,000 rays)
   std::string foxPath;
   const Mesh foxMesh = LoadFoxOrFallback(foxPath);
   Bvh::Blas foxBlas;
   foxBlas.Build(foxMesh, 1);
   const int foxBad = VerifyMeshRays(foxMesh, 10000, 0x2002u, foxBlas);
   check(foxBad == 0, "Fox mesh: 10,000 random rays match brute force", (double)foxBad, 0.0);

   // 3. Refit after moving every vertex (10,000 rays)
   Mesh deformedSphere = uvSphere;
   for (size_t i = 0; i < deformedSphere.vertices.size(); i++)
   {
      Vertex& v = deformedSphere.vertices[i];
      const float wave = 0.15f * std::sin(v.py * 6.0f + (float)i * 0.01f);
      v.px = v.px * 1.25f + wave;
      v.py = v.py * 0.85f - 0.5f * wave;
      v.pz = v.pz * 1.15f + 0.3f * std::cos(v.px * 5.0f);
   }
   sphereBlas.Refit(deformedSphere);
   const int refitBad = VerifyMeshRays(deformedSphere, 10000, 0x3003u, sphereBlas);
   check(refitBad == 0, "Refit after moving every vertex: 10,000 rays match brute force", (double)refitBad, 0.0);

   // 4. TLAS: 3 instances with non-uniform scale and rotation (10,000 rays)
   const Mesh instBaseMesh = Primitives::Sphere(16, 32);
   Bvh::Blas instBlas;
   instBlas.Build(instBaseMesh, 0);

   std::vector<Bvh::Instance> instances(3);
   instances[0].blasIndex = 0;
   instances[0].materialId = 10;
   instances[0].transform = Mat4::Multiply(
      Mat4::Translation(-1.2f, 0.3f, 0.1f),
      Mat4::Multiply(Mat4::RotationZ(0.6f), Mat4::Multiply(Mat4::RotationX(0.4f), Mat4::Scale(1.5f, 0.6f, 1.2f))));

   instances[1].blasIndex = 0;
   instances[1].materialId = 20;
   instances[1].transform = Mat4::Multiply(
      Mat4::Translation(1.1f, -0.4f, -0.2f),
      Mat4::Multiply(Mat4::RotationY(-0.8f), Mat4::Multiply(Mat4::RotationZ(0.5f), Mat4::Scale(0.7f, 1.6f, 0.9f))));

   instances[2].blasIndex = 0;
   instances[2].materialId = 30;
   instances[2].transform = Mat4::Multiply(
      Mat4::Translation(0.1f, 1.2f, -0.8f),
      Mat4::Multiply(Mat4::RotationX(1.1f), Mat4::Multiply(Mat4::RotationY(0.3f), Mat4::Scale(1.3f, 0.8f, 1.7f))));

   std::vector<Bvh::Blas> blasVec = { instBlas };
   Bvh::Tlas tlas;
   tlas.Build(instances, blasVec);

   std::vector<PrecompTri> worldInstTris[3];
   float tlasLo[3] = { 1e30f, 1e30f, 1e30f };
   float tlasHi[3] = { -1e30f, -1e30f, -1e30f };
   for (int k = 0; k < 3; k++)
   {
      const Mesh wm = MeshOps::Transform(instBaseMesh, instances[(size_t)k].transform);
      worldInstTris[k] = PrecomputeTris(wm);
      float mlo[3], mhi[3];
      MeshBounds(wm, mlo, mhi);
      for (int a = 0; a < 3; a++)
      {
         tlasLo[a] = std::min(tlasLo[a], mlo[a]);
         tlasHi[a] = std::max(tlasHi[a], mhi[a]);
      }
   }

   const std::vector<Bvh::Ray> tlasRays = MakeRandomRays(10000, tlasLo, tlasHi, 0x4004u);
   const std::vector<PrecompTri> baseTris = PrecomputeTris(instBaseMesh);
   int tlasBad = 0;
   for (int i = 0; i < 10000; i++)
   {
      const Bvh::Hit th = tlas.Intersect(tlasRays[(size_t)i], blasVec);

      // 4a. Exact brute-force across all instances in local space
      Bvh::Hit localBfHit;
      for (int k = 0; k < 3; k++)
      {
         const Mat4 invM = Mat4::Inverse(instances[(size_t)k].transform);
         const Bvh::Ray& wr = tlasRays[(size_t)i];
         Bvh::Ray lr;
         lr.o[0] = invM.m[0] * wr.o[0] + invM.m[4] * wr.o[1] + invM.m[8] * wr.o[2] + invM.m[12];
         lr.o[1] = invM.m[1] * wr.o[0] + invM.m[5] * wr.o[1] + invM.m[9] * wr.o[2] + invM.m[13];
         lr.o[2] = invM.m[2] * wr.o[0] + invM.m[6] * wr.o[1] + invM.m[10] * wr.o[2] + invM.m[14];
         lr.d[0] = invM.m[0] * wr.d[0] + invM.m[4] * wr.d[1] + invM.m[8] * wr.d[2];
         lr.d[1] = invM.m[1] * wr.d[0] + invM.m[5] * wr.d[1] + invM.m[9] * wr.d[2];
         lr.d[2] = invM.m[2] * wr.d[0] + invM.m[6] * wr.d[1] + invM.m[10] * wr.d[2];
         lr.tMin = wr.tMin;
         lr.tMax = localBfHit.t;
         const Bvh::Hit kh = BruteForceIntersect(lr, baseTris);
         if (kh.Valid() && kh.t < localBfHit.t)
         {
            localBfHit = kh;
            localBfHit.instanceIndex = k;
            localBfHit.materialId = instances[(size_t)k].materialId;
         }
      }

      // 4b. World-space transformed-vertex brute-force
      Bvh::Hit bh;
      for (int k = 0; k < 3; k++)
      {
         Bvh::Ray r = tlasRays[(size_t)i];
         r.tMax = bh.t;
         const Bvh::Hit kh = BruteForceIntersect(r, worldInstTris[k]);
         if (kh.Valid() && kh.t < bh.t)
         {
            bh = kh;
            bh.instanceIndex = k;
            bh.materialId = instances[(size_t)k].materialId;
         }
      }

      auto nearEdge = [](const Bvh::Hit& h)
      {
         const float w = 1.0f - h.u - h.v;
         return h.u < 1e-3f || h.v < 1e-3f || w < 1e-3f;
      };

      if (th.Valid() != localBfHit.Valid())
      {
         const Bvh::Hit& h = th.Valid() ? th : localBfHit;
         if (!nearEdge(h))
            tlasBad++;
         continue;
      }
      if (th.Valid())
      {
         if (th.instanceIndex != localBfHit.instanceIndex ||
             (th.triIndex != localBfHit.triIndex && std::fabs(th.t - localBfHit.t) > 1e-5f * std::max(1.0f, localBfHit.t)))
         {
            if (!nearEdge(th) && !nearEdge(localBfHit))
               tlasBad++;
            continue;
         }
      }

      if (th.Valid() != bh.Valid())
      {
         const Bvh::Hit& h = th.Valid() ? th : bh;
         if (!nearEdge(h))
            tlasBad++;
         continue;
      }
      if (th.Valid())
      {
         const bool sameInst = (th.instanceIndex == bh.instanceIndex);
         const bool sameTri = (th.triIndex == bh.triIndex);
         const bool nearT = (std::fabs(th.t - bh.t) <= 1e-3f);
         if (!nearT || (!sameInst) || (!sameTri && std::fabs(th.t - bh.t) > 1e-4f))
         {
            if (!nearEdge(th) && !nearEdge(bh))
               tlasBad++;
         }
      }
   }
   check(tlasBad == 0, "TLAS (3 instances, non-uniform scale + rotation): 10,000 rays match brute force",
         (double)tlasBad, 0.0);

   // 5. 1M-triangle subdivided sphere (10,000 rays)
   const Mesh sphere1M = MakeSubdividedSphere1M();
   Bvh::Blas blas1M;
   blas1M.Build(sphere1M, 0);
   const int bad1M = VerifyMeshRays(sphere1M, 10000, 0x5005u, blas1M);
   check(bad1M == 0, "1M-triangle subdivided sphere: 10,000 random rays match brute force",
         (double)bad1M, (double)sphere1M.FaceCount());

   printf("BVH TEST: %s (%d failure%s)\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
   return failures == 0;
}

// ======================================================== INFINITE_BVHBENCH
bool RunBvhBench()
{
   // 100k-triangle sphere (200 x 250 quads = 100,000 triangles)
   const Mesh sphere100k = Primitives::Sphere(200, 250);
   const long tris100k = (long)sphere100k.FaceCount();

   std::vector<double> build100kSamples;
   Bvh::Blas blas100k;
   for (int i = 0; i < 7; i++)
   {
      auto t0 = std::chrono::steady_clock::now();
      blas100k.Build(sphere100k, 0);
      build100kSamples.push_back(MsSince(t0));
   }
   printf("{\"bench\":\"bvh_build_100k\",\"triangles\":%ld,\"ms\":%.3f,\"spec_ms\":8.3}\n",
          tris100k, Median(build100kSamples));

   // Refit at 100k triangles
   Mesh deformed100k = sphere100k;
   for (Vertex& v : deformed100k.vertices)
   {
      v.px *= 1.05f;
      v.py *= 0.95f;
      v.pz += 0.02f;
   }
   std::vector<double> refit100kSamples;
   for (int i = 0; i < 15; i++)
   {
      auto t0 = std::chrono::steady_clock::now();
      blas100k.Refit(deformed100k);
      refit100kSamples.push_back(MsSince(t0));
   }
   printf("{\"bench\":\"bvh_refit_100k\",\"triangles\":%ld,\"ms\":%.3f,\"spec_ms\":0.25}\n",
          tris100k, Median(refit100kSamples));

   // 1M-triangle subdivided sphere
   const Mesh sphere1M = MakeSubdividedSphere1M();
   const long tris1M = (long)sphere1M.FaceCount();
   std::vector<double> build1MSamples;
   Bvh::Blas blas1M;
   for (int i = 0; i < 3; i++)
   {
      auto t0 = std::chrono::steady_clock::now();
      blas1M.Build(sphere1M, 0);
      build1MSamples.push_back(MsSince(t0));
   }
   printf("{\"bench\":\"bvh_build_1m\",\"triangles\":%ld,\"ms\":%.3f}\n",
          tris1M, Median(build1MSamples));

   // TLAS rebuild at 1,000 instances
   std::vector<Bvh::Blas> blasList = { blas100k };
   std::vector<Bvh::Instance> inst1000(1000);
   Rng rng(0x7777u);
   for (int i = 0; i < 1000; i++)
   {
      inst1000[(size_t)i].blasIndex = 0;
      inst1000[(size_t)i].materialId = i % 8;
      const float tx = rng.Range(-20.0f, 20.0f);
      const float ty = rng.Range(-20.0f, 20.0f);
      const float tz = rng.Range(-20.0f, 20.0f);
      const float rx = rng.Range(-3.14f, 3.14f);
      const float sx = rng.Range(0.5f, 1.5f);
      const float sy = rng.Range(0.5f, 1.5f);
      const float sz = rng.Range(0.5f, 1.5f);
      inst1000[(size_t)i].transform = Mat4::Multiply(
         Mat4::Translation(tx, ty, tz),
         Mat4::Multiply(Mat4::RotationY(rx), Mat4::Scale(sx, sy, sz)));
   }
   Bvh::Tlas tlas1000;
   std::vector<double> tlasSamples;
   for (int i = 0; i < 15; i++)
   {
      auto t0 = std::chrono::steady_clock::now();
      tlas1000.Build(inst1000, blasList);
      tlasSamples.push_back(MsSince(t0));
   }
   printf("{\"bench\":\"tlas_rebuild_1000\",\"instances\":1000,\"ms\":%.3f,\"spec_ms\":0.08}\n",
          Median(tlasSamples));

   // Single-thread ray throughput (rays/s) on 100k and 1M spheres
   float lo[3] = { -0.5f, -0.5f, -0.5f }, hi[3] = { 0.5f, 0.5f, 0.5f };
   const int benchRayCount = 100000;
   const std::vector<Bvh::Ray> benchRays = MakeRandomRays(benchRayCount, lo, hi, 0x8888u);

   volatile float sink = 0.0f;
   auto r0 = std::chrono::steady_clock::now();
   int hits100k = 0;
   for (int i = 0; i < benchRayCount; i++)
   {
      const Bvh::Hit h = blas100k.Intersect(benchRays[(size_t)i]);
      if (h.Valid())
      {
         hits100k++;
         sink += h.t;
      }
   }
   const double msRays100k = MsSince(r0);
   const double mraysPerSec100k = ((double)benchRayCount / (msRays100k * 1e-3)) / 1e6;
   printf("{\"bench\":\"bvh_rays_single_thread_100k\",\"rays\":%d,\"hits\":%d,\"ms\":%.3f,\"mrays_per_s\":%.3f,\"rays_per_s\":%.0f}\n",
          benchRayCount, hits100k, msRays100k, mraysPerSec100k, (double)benchRayCount / (msRays100k * 1e-3));

   auto r1 = std::chrono::steady_clock::now();
   int hits1M = 0;
   for (int i = 0; i < benchRayCount; i++)
   {
      const Bvh::Hit h = blas1M.Intersect(benchRays[(size_t)i]);
      if (h.Valid())
      {
         hits1M++;
         sink += h.t;
      }
   }
   const double msRays1M = MsSince(r1);
   const double mraysPerSec1M = ((double)benchRayCount / (msRays1M * 1e-3)) / 1e6;
   printf("{\"bench\":\"bvh_rays_single_thread_1m\",\"rays\":%d,\"hits\":%d,\"ms\":%.3f,\"mrays_per_s\":%.3f,\"rays_per_s\":%.0f}\n",
          benchRayCount, hits1M, msRays1M, mraysPerSec1M, (double)benchRayCount / (msRays1M * 1e-3));
   (void)sink;
   return true;
}

namespace
{
   struct BvhAutoDispatch
   {
      BvhAutoDispatch()
      {
         if (getenv("INFINITE_BVHTEST") != nullptr)
         {
            const bool ok = RunBvhTest();
            std::fflush(stdout);
            std::_Exit(ok ? 0 : 1);
         }
         if (getenv("INFINITE_BVHBENCH") != nullptr)
         {
            const bool ok = RunBvhBench();
            std::fflush(stdout);
            std::_Exit(ok ? 0 : 1);
         }
      }
   } sBvhAutoDispatch;
}
}

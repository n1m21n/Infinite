// Turbo 0.62: OptiX device programs for Render 3D's RTX mode. One ray type:
// radiance rays fill an RtxHit through the payload pointer; shadow rays skip
// the closest-hit program and only learn whether something was in the way
// (the miss program clears `hit`). All shading happens in the ray generation
// program, the same model as the raster path's PBR: GGX + Lambert, one shadow
// ray per light, glass by transmission, the HDRI or the sky gradient on a miss.

#include <optix.h>

#include "RtxShared.h"

extern "C" __constant__ RtxParams params;

namespace
{
   constexpr float kPi = 3.14159265358979f;

   __device__ __forceinline__ float3 operator+(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
   __device__ __forceinline__ float3 operator-(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
   __device__ __forceinline__ float3 operator-(float3 a) { return make_float3(-a.x, -a.y, -a.z); }
   __device__ __forceinline__ float3 operator*(float3 a, float3 b) { return make_float3(a.x * b.x, a.y * b.y, a.z * b.z); }
   __device__ __forceinline__ float3 operator*(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
   __device__ __forceinline__ float3 operator/(float3 a, float s) { return a * (1.0f / s); }
   __device__ __forceinline__ float3& operator+=(float3& a, float3 b) { a = a + b; return a; }
   __device__ __forceinline__ float3& operator*=(float3& a, float3 b) { a = a * b; return a; }
   __device__ __forceinline__ float3& operator*=(float3& a, float s) { a = a * s; return a; }
   __device__ __forceinline__ float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   __device__ __forceinline__ float3 cross(float3 a, float3 b)
   {
      return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
   }
   __device__ __forceinline__ float3 normalize(float3 a) { return a * rsqrtf(fmaxf(dot(a, a), 1e-20f)); }
   __device__ __forceinline__ float clamp01(float x) { return fminf(fmaxf(x, 0.0f), 1.0f); }
   __device__ __forceinline__ float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
   __device__ __forceinline__ float maxc(float3 a) { return fmaxf(a.x, fmaxf(a.y, a.z)); }
   __device__ __forceinline__ float toLin(float c) { return powf(fmaxf(c, 0.0f), 2.2f); }
   __device__ __forceinline__ float3 toLin(float3 c) { return make_float3(toLin(c.x), toLin(c.y), toLin(c.z)); }
   __device__ __forceinline__ float smoothstep(float a, float b, float x)
   {
      const float t = clamp01((x - a) / (b - a));
      return t * t * (3.0f - 2.0f * t);
   }

   // PCG hash random numbers.
   __device__ __forceinline__ unsigned int pcg(unsigned int& s)
   {
      s = s * 747796405u + 2891336453u;
      unsigned int w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
      return (w >> 22u) ^ w;
   }
   __device__ __forceinline__ float rnd(unsigned int& s) { return (pcg(s) & 0x00FFFFFFu) / 16777216.0f; }

   __device__ __forceinline__ void* unpack(unsigned int a, unsigned int b)
   {
      return reinterpret_cast<void*>(static_cast<unsigned long long>(a) << 32 | b);
   }
   __device__ __forceinline__ RtxHit* hitPtr() { return reinterpret_cast<RtxHit*>(unpack(optixGetPayload_0(), optixGetPayload_1())); }

   __device__ float3 mulM3(const float* m, float3 v) // column-major 3x3
   {
      return make_float3(m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z,
                         m[2] * v.x + m[5] * v.y + m[8] * v.z);
   }
   __device__ float4 mulM4(const float* m, float4 v)
   {
      return make_float4(m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w, m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                         m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w, m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
   }

   // Render 3D's panorama directions (Render3DPost.cpp kPanoGlsl), camera space.
   __device__ bool panoDir(float u, float v, float3& d)
   {
      if (params.panorama == 3)
      {
         const float px = u * 2.0f - 1.0f, py = v * 2.0f - 1.0f;
         const float r = sqrtf(px * px + py * py);
         if (r > 1.0f)
            return false;
         const float th = r * params.domeAngle * (kPi / 180.0f) * 0.5f;
         const float ax = r > 1e-6f ? px / r : 0.0f, ay = r > 1e-6f ? py / r : -1.0f;
         float3 q = normalize(make_float3(ax * sinf(th), cosf(th), ay * sinf(th)));
         const float tr = params.domeTilt * (kPi / 180.0f);
         const float ct = cosf(tr), st = sinf(tr);
         d = make_float3(q.x, q.y * ct + q.z * st, q.z * ct - q.y * st);
         return true;
      }
      if (params.panorama == 1)
      {
         const float phi = (u - 0.5f) * 2.0f * kPi, th = (1.0f - v) * kPi;
         d = make_float3(sinf(th) * sinf(phi), cosf(th), -sinf(th) * cosf(phi));
         return true;
      }
      // Cubemap 3x2: top row +X -X +Y, bottom row -Y +Z -Z.
      const float3 F[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
      const float3 R[6] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };
      const float3 U[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0, 1, 0 } };
      const float gx = u * 3.0f, gy = v * 2.0f;
      const int cx = min((int)gx, 2), cy = min((int)gy, 1);
      const float fx = (gx - cx) * 2.0f - 1.0f, fy = (gy - cy) * 2.0f - 1.0f;
      const int face = (1 - cy) * 3 + cx;
      d = normalize(F[face] + R[face] * fx + U[face] * fy);
      return true;
   }

   __device__ float3 envColor(float3 d)
   {
      if (params.envTex != 0)
      {
         const float phi = atan2f(d.z, d.x) + params.envRotation;
         const float theta = acosf(fminf(fmaxf(d.y, -1.0f), 1.0f));
         // Turbo 0.70: v flipped like the raster's dirToEquirect (the same GL
         // texture): theta / pi showed the HDRI upside down, so the mirrors
         // reflected the ground where the raster shows the sky.
         const float4 c = tex2D<float4>(params.envTex, phi / (2.0f * kPi) + 0.5f, 1.0f - theta / kPi);
         return make_float3(c.x, c.y, c.z) * params.envIntensity;
      }
      const float t = clamp01(d.y * 0.5f + 0.5f);
      const float3 lower = lerp(params.ground, params.horizon, smoothstep(0.0f, 0.5f, t));
      return lerp(lower, params.sky, smoothstep(0.5f, 1.0f, t)) * params.envIntensity;
   }

   __device__ float ggxD(float nh, float a)
   {
      const float a2 = a * a;
      const float d = nh * nh * (a2 - 1.0f) + 1.0f;
      return a2 / (kPi * d * d + 1e-7f);
   }
   __device__ float smithG(float nv, float nl, float a)
   {
      const float k = a * 0.5f;
      return (nv / (nv * (1.0f - k) + k)) * (nl / (nl * (1.0f - k) + k));
   }
   __device__ float3 fres(float c, float3 f0)
   {
      const float p = powf(clamp01(1.0f - c), 5.0f);
      return f0 + (make_float3(1, 1, 1) - f0) * p;
   }
   __device__ void basis(float3 n, float3& t, float3& b)
   {
      const float3 up = fabsf(n.y) < 0.999f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
      t = normalize(cross(up, n));
      b = cross(n, t);
   }
   __device__ float3 sampleGgxH(float3 n, float a, unsigned int& s)
   {
      const float r1 = rnd(s), r2 = rnd(s);
      const float phi = 2.0f * kPi * r1;
      const float ct = sqrtf((1.0f - r2) / (1.0f + (a * a - 1.0f) * r2)), st = sqrtf(fmaxf(1.0f - ct * ct, 0.0f));
      float3 t, b;
      basis(n, t, b);
      return normalize(t * (cosf(phi) * st) + b * (sinf(phi) * st) + n * ct);
   }
   __device__ float3 evalBsdf(float3 n, float3 v, float3 l, float3 base, float metal, float a, float3 f0)
   {
      const float nl = dot(n, l), nv = fmaxf(dot(n, v), 1e-4f);
      if (nl <= 0.0f)
         return make_float3(0, 0, 0);
      const float3 h = normalize(v + l);
      const float3 F = fres(fmaxf(dot(v, h), 0.0f), f0);
      const float3 spec = F * (ggxD(fmaxf(dot(n, h), 0.0f), a) * smithG(nv, nl, a) / (4.0f * nv * nl + 1e-5f));
      const float3 diff = (make_float3(1, 1, 1) - F) * (1.0f - metal) * base / kPi;
      return (diff + spec) * nl;
   }

   // Turbo 0.70 (X1): a world point (w = 1) or direction (w = 0) through last
   // frame's camera, in this frame's pixel units; x < 0 = behind it.
   // `sample`: where this sample sits in the pixel grid now; the result is
   // moved by the same motion from the pixel's centre, so the sub-pixel jitter
   // does not land in the history fetch (it blurred everything over frames).
   __device__ float2 prevPixel(float4 p, float2 sample, int x, int y)
   {
      const float4 c = mulM4(params.prevViewProj, p);
      if (c.w <= 1e-6f)
         return make_float2(-1.0f, -1.0f);
      const float px = (c.x / c.w * 0.5f + 0.5f) * params.width, py = (c.y / c.w * 0.5f + 0.5f) * params.height;
      return make_float2((float)x + 0.5f + (px - sample.x), (float)y + 0.5f + (py - sample.y));
   }

   // Turbo 0.70: light reaching the camera after a diffuse bounce, capped (one
   // sample of a small bright source otherwise lands as a white speck the
   // denoisers smear into blotches). What the first hit shows, and a mirror's
   // reflection of it (the sun in a near-mirror metal is a real highlight),
   // are left alone.
   __device__ void addLight(float3& L, float3 v, bool capped)
   {
      constexpr float kIndirectMax = 8.0f;
      if (capped)
      {
         const float m = maxc(v);
         if (m > kIndirectMax)
            v *= kIndirectMax / m;
      }
      L += v;
   }

   __device__ void traceRadiance(float3 ro, float3 rd, RtxHit& h)
   {
      unsigned long long p = reinterpret_cast<unsigned long long>(&h);
      unsigned int p0 = (unsigned int)(p >> 32), p1 = (unsigned int)p;
      h.hit = 0;
      optixTrace(params.scene, ro, rd, 0.0f, 1e16f, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1,
                 0, p0, p1);
   }
   __device__ bool occluded(float3 ro, float3 rd, float tmax)
   {
      RtxHit h;
      h.hit = 1;
      unsigned long long p = reinterpret_cast<unsigned long long>(&h);
      unsigned int p0 = (unsigned int)(p >> 32), p1 = (unsigned int)p;
      optixTrace(params.scene, ro, rd, 0.0f, tmax, 0.0f, OptixVisibilityMask(255),
                 OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
                 0, 1, 0, p0, p1);
      return h.hit != 0;
   }

   // Turbo 0.70 (X1): where the point `world` on the current hit was last
   // frame. A particle (PartId) is found by its id in the ring of last frame's
   // places, so births and deaths do not matter; otherwise through the
   // instance's previous transform while the list kept its order (same
   // count); else where it is now (no motion of its own).
   __device__ float3 previousPoint(const RtxGeomData& g, float3 world, int prim, bool sphere, float3 centre)
   {
      if (params.prevRing != nullptr && g.partId != nullptr)
      {
         const unsigned int at = sphere ? (unsigned int)prim : optixGetInstanceId();
         const unsigned int id = (unsigned int)fmaxf(g.partId[(size_t)at * g.partIdStride] + 0.5f, 0.0f) + 1u;
         const RtxPrevSlot& s = params.prevRing[RtxRingSlot(id, g.ringKey, params.ringMask)];
         if (s.id != id || s.key != g.ringKey)
            return world; // born this frame
         if (sphere)
            return world + make_float3(s.t[3], s.t[7], s.t[11]) - centre;
         const float3 o = optixTransformPointFromWorldToObjectSpace(world);
         return make_float3(s.t[0] * o.x + s.t[1] * o.y + s.t[2] * o.z + s.t[3],
                            s.t[4] * o.x + s.t[5] * o.y + s.t[6] * o.z + s.t[7],
                            s.t[8] * o.x + s.t[9] * o.y + s.t[10] * o.z + s.t[11]);
      }
      const unsigned int idx = optixGetInstanceIndex();
      if (params.prevInst == nullptr || idx >= params.prevInstCount)
         return world;
      const float3 o = optixTransformPointFromWorldToObjectSpace(world);
      const float* t = params.prevInst[idx].transform;
      return make_float3(t[0] * o.x + t[1] * o.y + t[2] * o.z + t[3], t[4] * o.x + t[5] * o.y + t[6] * o.z + t[7],
                         t[8] * o.x + t[9] * o.y + t[10] * o.z + t[11]);
   }

   // Colour, texture, material of the hit primitive into `h`.
   __device__ void fillMaterial(const RtxGeomData& g, int i0, int i1, int i2, float b1, float b2, RtxHit& h)
   {
      const float b0 = 1.0f - b1 - b2;
      float3 base = g.base;
      if (g.color != nullptr)
      {
         const float* c0 = g.color + (size_t)i0 * g.colorStride;
         const float* c1 = g.color + (size_t)i1 * g.colorStride;
         const float* c2 = g.color + (size_t)i2 * g.colorStride;
         // Turbo 0.70: as the raster (vVertexColor), not linearised - the
         // same colours came out darker and redder here.
         base *= make_float3(c0[0] * b0 + c1[0] * b1 + c2[0] * b2, c0[1] * b0 + c1[1] * b1 + c2[1] * b2,
                             c0[2] * b0 + c1[2] * b1 + c2[2] * b2);
      }
      if (g.instColor != nullptr)
      {
         const float* c = g.instColor + (size_t)optixGetInstanceId() * g.instColorStride;
         base *= make_float3(c[0], c[1], c[2]); // Turbo 0.70: as the raster's vInstanceColor
      }
      float u = 0.0f, v = 0.0f;
      if (g.uv != nullptr)
      {
         const float* t0 = g.uv + (size_t)i0 * g.uvStride;
         const float* t1 = g.uv + (size_t)i1 * g.uvStride;
         const float* t2 = g.uv + (size_t)i2 * g.uvStride;
         u = t0[0] * b0 + t1[0] * b1 + t2[0] * b2;
         v = t0[1] * b0 + t1[1] * b1 + t2[1] * b2;
      }
      if (g.instTexMode != 0)
      {
         // Turbo 0.70: the raster's instanceTexel(), per copy.
         const unsigned int id = optixGetInstanceId();
         float t = 0.0f;
         if (g.instTexFrom == 1)
            t = (float)id;
         else if (g.instTexFrom == 2)
         {
            unsigned int hh = id * 2654435761u;
            hh ^= hh >> 15; hh *= 0x846ca68bu; hh ^= hh >> 16;
            t = (float)(hh & 0xffffu);
         }
         else if (g.instTexIdx != nullptr)
            t = g.instTexIdx[(size_t)id * g.instTexIdxStride];
         int idx = (int)floorf(t + 0.5f);
         float4 tc;
         if (g.instTexMode == 2)
         {
            const int cols = max(1, g.atlasCols), rows = max(1, g.atlasRows), cells = cols * rows;
            idx = ((idx % cells) + cells) % cells;
            const float cx = (float)(idx % cols), cy = (float)(rows - 1 - idx / cols);
            const float fu = fminf(fmaxf(u - floorf(u), 0.001f), 0.999f);
            const float fv = fminf(fmaxf(v - floorf(v), 0.001f), 0.999f);
            tc = tex2D<float4>(g.instTex[0], (cx + fu) / (float)cols, (cy + fv) / (float)rows);
         }
         else
         {
            const int n = max(1, g.instTexCount);
            idx = ((idx % n) + n) % n;
            tc = tex2D<float4>(g.instTex[idx], u, v);
         }
         base *= toLin(make_float3(tc.x, tc.y, tc.z));
      }
      else if (g.tex != 0 && g.uv != nullptr)
      {
         const float4 tc = tex2D<float4>(g.tex, u, v);
         base *= toLin(make_float3(tc.x, tc.y, tc.z));
      }
      h.base = base;
      h.metal = g.metal;
      h.rough = g.rough;
      h.opacity = g.opacity;
      h.ior = g.ior;
      h.trans = g.trans;
      h.emission = g.emission;
   }
}

extern "C" __global__ void __closesthit__tri()
{
   const RtxGeomData& g = *reinterpret_cast<const RtxGeomData*>(optixGetSbtDataPointer());
   RtxHit& h = *hitPtr();
   const int prim = optixGetPrimitiveIndex();
   const float2 bc = optixGetTriangleBarycentrics();
   const unsigned int i0 = g.idx[prim * 3], i1 = g.idx[prim * 3 + 1], i2 = g.idx[prim * 3 + 2];
   const float* p0 = g.pos + (size_t)i0 * g.posStride;
   const float* p1 = g.pos + (size_t)i1 * g.posStride;
   const float* p2 = g.pos + (size_t)i2 * g.posStride;
   const float3 a = make_float3(p0[0], p0[1], p0[2]), b = make_float3(p1[0], p1[1], p1[2]), c = make_float3(p2[0], p2[1], p2[2]);
   float3 ng = normalize(optixTransformNormalFromObjectToWorldSpace(cross(b - a, c - a)));
   float3 n = ng;
   if (g.normal != nullptr)
   {
      const float* n0 = g.normal + (size_t)i0 * g.normalStride;
      const float* n1 = g.normal + (size_t)i1 * g.normalStride;
      const float* n2 = g.normal + (size_t)i2 * g.normalStride;
      const float b0 = 1.0f - bc.x - bc.y;
      const float3 ns = make_float3(n0[0] * b0 + n1[0] * bc.x + n2[0] * bc.y, n0[1] * b0 + n1[1] * bc.x + n2[1] * bc.y,
                                    n0[2] * b0 + n1[2] * bc.x + n2[2] * bc.y);
      if (dot(ns, ns) > 1e-12f)
      {
         n = normalize(optixTransformNormalFromObjectToWorldSpace(ns));
         if (dot(n, ng) < 0.0f)
            ng = -ng; // follow the authored normals' side
      }
   }
   h.hit = 1;
   h.t = optixGetRayTmax();
   h.n = n;
   h.ng = ng;
   h.prevP = previousPoint(g, optixGetWorldRayOrigin() + optixGetWorldRayDirection() * h.t, prim, false,
                           make_float3(0, 0, 0));
   fillMaterial(g, (int)i0, (int)i1, (int)i2, bc.x, bc.y, h);
}

extern "C" __global__ void __closesthit__sphere()
{
   const RtxGeomData& g = *reinterpret_cast<const RtxGeomData*>(optixGetSbtDataPointer());
   RtxHit& h = *hitPtr();
   const int prim = optixGetPrimitiveIndex();
   const float* p = g.pos + (size_t)prim * g.posStride;
   const float3 centre = optixTransformPointFromObjectToWorldSpace(make_float3(p[0], p[1], p[2]));
   const float3 hitP = optixGetWorldRayOrigin() + optixGetWorldRayDirection() * optixGetRayTmax();
   const float3 n = normalize(hitP - centre);
   h.hit = 1;
   h.t = optixGetRayTmax();
   h.n = n;
   h.ng = n;
   h.prevP = previousPoint(g, hitP, prim, true, centre);
   fillMaterial(g, prim, prim, prim, 0.0f, 0.0f, h);
}

extern "C" __global__ void __miss__radiance()
{
   hitPtr()->hit = 0;
}

extern "C" __global__ void __raygen__render()
{
   const uint3 li = optixGetLaunchIndex();
   const int x = (int)li.x, y = (int)li.y;
   const int pix = y * params.width + x;
   unsigned int s = (unsigned int)(x * 1973 + y * 9277) ^ (params.seed * 26699u);
   pcg(s);

   float3 sum = make_float3(0, 0, 0), albedo = make_float3(0, 0, 0), nrm = make_float3(0, 0, 0);
   float cover = 0.0f;
   float2 mv = make_float2(-1.0f, -1.0f); // Turbo 0.70 (X1): first sample's first hit
   float4 gb = make_float4(0.0f, 0.0f, 0.0f, -1.0f);
   for (int sp = 0; sp < params.spp; sp++)
   {
      const float u = (x + rnd(s)) / params.width, v = (y + rnd(s)) / params.height;
      float3 ro, rd;
      if (params.panorama > 0)
      {
         float3 d;
         if (!panoDir(u, v, d))
         {
            cover += 1.0f; // outside the dome: opaque black
            continue;
         }
         ro = params.eye;
         rd = normalize(mulM3(params.invViewRot, d));
      }
      else
      {
         const float nx = u * 2.0f - 1.0f, ny = v * 2.0f - 1.0f;
         const float4 a = mulM4(params.invViewProj, make_float4(nx, ny, -1.0f, 1.0f));
         const float4 b = mulM4(params.invViewProj, make_float4(nx, ny, 1.0f, 1.0f));
         ro = make_float3(a.x / a.w, a.y / a.w, a.z / a.w);
         rd = normalize(make_float3(b.x / b.w, b.y / b.w, b.z / b.w) - ro);
      }

      float3 L = make_float3(0, 0, 0), thr = make_float3(1, 1, 1);
      int passes = 0;
      bool viaDiffuse = false; // Turbo 0.70: the last bounce took the diffuse lobe
      bool diffused = false;   // ... or any bounce before it (addLight caps)
      for (int bounce = 0; bounce <= params.bounces; bounce++)
      {
         RtxHit h;
         traceRadiance(ro, rd, h);
         if (!h.hit)
         {
            if (bounce == 0 && sp == 0 && params.hasPrev)
               mv = prevPixel(make_float4(rd.x, rd.y, rd.z, 0.0f), make_float2(u * params.width, v * params.height), x, y);
            // Turbo 0.70: diffuse light from the environment tinted like the
            // raster's image-based diffuse (ambient * 3).
            addLight(L, thr * (viaDiffuse ? envColor(rd) * params.ambientTint : envColor(rd)), diffused);
            break;
         }
         const float3 pos = ro + rd * h.t;
         // Cut-out opacity: pass straight through with the matching probability.
         if (h.opacity < 1.0f && rnd(s) > h.opacity && passes < 8)
         {
            ro = pos + rd * 1e-3f;
            bounce--;
            passes++;
            continue;
         }
         if (bounce == 0)
         {
            cover += 1.0f;
            albedo += h.base;
            nrm += mulM3(params.viewRot, h.n);
            if (sp == 0)
            {
               // The g-buffer every frame (next frame compares against it).
               gb = make_float4(h.n.x, h.n.y, h.n.z, h.t);
               if (params.hasPrev)
                  mv = prevPixel(make_float4(h.prevP.x, h.prevP.y, h.prevP.z, 1.0f),
                                 make_float2(u * params.width, v * params.height), x, y);
            }
         }
         const bool inside = dot(h.ng, rd) > 0.0f;
         float3 nf = dot(h.n, rd) > 0.0f ? -h.n : h.n;
         const float3 ngf = inside ? -h.ng : h.ng;
         if (dot(nf, ngf) < 0.0f)
            nf = ngf;
         const float3 vv = -rd;
         // Turbo 0.70: the raster's roughness floor (0.045), same highlights.
         const float metal = h.metal, a = fmaxf(h.rough, 0.045f) * fmaxf(h.rough, 0.045f);
         const float3 base = h.base;
         addLight(L, thr * h.emission, diffused);
         const float3 f0 = lerp(make_float3(0.04f, 0.04f, 0.04f), base, metal);
         if (bounce == 0 && params.rim > 0.0f) // Turbo 0.70: the raster's rim
         {
            const float edge = 1.0f - clamp01(dot(nf, vv));
            L += thr * base * (edge * edge * edge * params.rim);
         }

         // Next-event estimation: one shadow ray per light (glass lets it through dimmed).
         addLight(L, thr * base * (1.0f - metal) * params.ambient * (1.0f - h.trans), diffused);
         for (int i = 0; i < params.lightCount; i++)
         {
            const RtxLight& lt = params.lights[i];
            float3 ldir;
            float dist = 1e16f, att = 1.0f;
            if (lt.type == 1 || lt.type == 4)
            {
               const float3 tl = lt.dir - pos;
               dist = sqrtf(dot(tl, tl));
               ldir = tl / fmaxf(dist, 1e-4f);
               att = 1.0f / (1.0f + dist * dist * 0.25f);
               if (lt.type == 4)
               {
                  const float c = dot(-ldir, normalize(-lt.dir));
                  att *= smoothstep(lt.spotCos0, lt.spotCos1, c);
               }
            }
            else
               ldir = normalize(lt.dir);
            if (dot(nf, ldir) <= 0.0f || att <= 0.0f)
               continue;
            if (occluded(pos + ngf * 1e-3f, ldir, dist - 2e-3f))
               continue;
            addLight(L, thr * evalBsdf(nf, vv, ldir, base, metal, a, f0) * lt.color * (att * (1.0f - h.trans * (1.0f - metal))),
                     diffused);
         }

         // Sample the next direction.
         float3 nd;
         if (h.trans > 0.0f && rnd(s) < h.trans * (1.0f - metal))
         {
            const float3 hm = sampleGgxH(nf, a, s);
            viaDiffuse = false;
            const float eta = inside ? h.ior : 1.0f / h.ior;
            const float c = clamp01(dot(vv, hm));
            float r0 = (1.0f - h.ior) / (1.0f + h.ior);
            r0 *= r0;
            const float F = r0 + (1.0f - r0) * powf(1.0f - c, 5.0f);
            // refract(rd, hm, eta)
            const float cosi = -dot(rd, hm);
            const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
            if (rnd(s) < F || k < 0.0f)
            {
               nd = rd - hm * (2.0f * dot(rd, hm));
               ro = pos + ngf * 1e-3f;
            }
            else
            {
               nd = normalize(rd * eta + hm * (eta * cosi - sqrtf(k)));
               ro = pos - ngf * 1e-3f;
               thr *= base;
            }
         }
         else
         {
            const float nv = fmaxf(dot(nf, vv), 1e-4f);
            const float3 fv = fres(nv, f0);
            // Turbo 0.70: lobe chosen by its actual weight. The old 10% floor
            // on the diffuse lobe sent a near-mirror metal (0.97) one ray in
            // ten into a lobe worth ~2%, carried at 10x - most of its grain.
            const float specW = maxc(fv);
            const float diffW = (1.0f - specW) * (1.0f - metal) * fmaxf(maxc(base), 1e-3f);
            const float pSpec = metal > 0.99f ? 1.0f : fminf(fmaxf(specW / fmaxf(specW + diffW, 1e-6f), 0.05f), 0.98f);
            if (rnd(s) < pSpec)
            {
               const float3 hm = sampleGgxH(nf, a, s);
               nd = rd - hm * (2.0f * dot(rd, hm));
               viaDiffuse = false;
               const float nl = dot(nf, nd);
               if (nl <= 0.0f)
                  break;
               const float vh = fmaxf(dot(vv, hm), 1e-4f), nh = fmaxf(dot(nf, hm), 1e-4f);
               thr *= fres(vh, f0) * (smithG(nv, nl, a) * vh / (nh * nv) / pSpec);
            }
            else
            {
               const float r1 = rnd(s), r2 = rnd(s), r = sqrtf(r1), phi = 2.0f * kPi * r2;
               float3 t, b;
               basis(nf, t, b);
               nd = normalize(t * (r * cosf(phi)) + b * (r * sinf(phi)) + nf * sqrtf(fmaxf(1.0f - r1, 0.0f)));
               viaDiffuse = true;
               diffused = true;
               thr *= (make_float3(1, 1, 1) - fv) * (1.0f - metal) * base / (1.0f - pSpec);
            }
            ro = pos + ngf * 1e-3f;
         }
         rd = nd;
         // Russian roulette past the third bounce.
         if (bounce >= 3)
         {
            const float q = fminf(fmaxf(maxc(thr), 0.05f), 0.95f);
            if (rnd(s) > q)
               break;
            thr *= 1.0f / q;
         }
         if (isnan(thr.x) || isnan(thr.y) || isnan(thr.z))
            break;
      }
      if (!(isnan(L.x) || isnan(L.y) || isnan(L.z) || isinf(L.x) || isinf(L.y) || isinf(L.z)))
         sum += make_float3(fminf(L.x, 64.0f), fminf(L.y, 64.0f), fminf(L.z, 64.0f)); // firefly clamp
   }

   float4 acc = params.samples > 0 ? params.accum[pix] : make_float4(0, 0, 0, 0);
   acc = make_float4(acc.x + sum.x, acc.y + sum.y, acc.z + sum.z, acc.w + cover);
   params.accum[pix] = acc;
   const float n = (float)(params.samples + params.spp);
   params.average[pix] = make_float4(acc.x / n, acc.y / n, acc.z / n, acc.w / n);
   // Guides for the denoiser: a running average too.
   const float4 al = params.samples > 0 ? params.albedo[pix] : make_float4(0, 0, 0, 0);
   const float4 no = params.samples > 0 ? params.normal[pix] : make_float4(0, 0, 0, 0);
   const float wOld = (float)params.samples / n, wNew = 1.0f / n;
   params.albedo[pix] = make_float4(al.x * wOld + albedo.x * wNew, al.y * wOld + albedo.y * wNew,
                                    al.z * wOld + albedo.z * wNew, 1.0f);
   params.normal[pix] = make_float4(no.x * wOld + nrm.x * wNew, no.y * wOld + nrm.y * wNew,
                                    no.z * wOld + nrm.z * wNew, 0.0f);
   if (params.motion != nullptr)
   {
      params.motion[pix] = mv;
      params.gbuf[pix] = gb;
   }
}

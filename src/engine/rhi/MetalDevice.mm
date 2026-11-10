// Metal backend of the engine RHI. See Rhi.h.
#include "engine/rhi/Rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>

namespace rhi
{
namespace
{
   NSString* const kTestShader = @R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; };
vertex VOut tri_vs(uint vid [[vertex_id]])
{
   const float2 p[3] = { float2(-0.6, -0.6), float2(0.6, -0.6), float2(0.0, 0.6) };
   VOut o; o.pos = float4(p[vid], 0.0, 1.0); return o;
}
fragment float4 tri_fs(VOut in [[stage_in]], constant float3& c [[buffer(0)]]) { return float4(c, 1.0); }
)";


   NSString* const kRasterShader = @R"(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;
struct LightU { float4 vec; float4 aim; float4 color; float4 terms; float4 spot; }; // terms: diffuse, specular, falloff, cutoff; spot: angle, penumbra, area w, h
struct FrameU
{
   float4x4 viewProj;
   float4 eye;      // xyz, w = rim
   float4 ambient;  // xyz linear
   float4 sky, horizon, ground; // xyz linear
   float4 params;   // exposure, tonemap, envIntensity, lightCount
   float4x4 lightVP;
   float4 shP;      // enabled, light index, bias (depth units), strength
   float4 shQ;      // texel, softness
   float4 gi;       // enabled, strength, frame index, rays
   float4 envm;     // x: 1 = HDRI map, +2 = HDRI background; y rotation (rad); z max lod (GL levels); w the map's base lod
   LightU lights[8]; // vec.w = type, aim.w unused
};
struct InstU { float4x4 model; float4x4 normalM; float4 base; float4 mr; }; // base linear rgb; mr = metallic, roughness, transmission, ior
// INV_POS: the second, invariant compile used by the depth prepass and the shaded runs after it (exact same depth);
// plain raster keeps the faster non-invariant build.
#ifdef INV_POS
#define VPOS [[position, invariant]]
#else
#define VPOS [[position]]
#endif
struct VOut { float4 pos VPOS; float3 wp; float3 n; };

vertex VOut r_vs(uint vid [[vertex_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                 constant InstU& m [[buffer(2)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4 w = m.model * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5; // GL clip depth -1..1 to Metal 0..1
   c.y = -c.y;              // the shared image is read by GL, whose row 0 is the bottom
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m.normalM * float4(n, 0.0)).xyz; return o;
}

float3 sampleEnv(constant FrameU& f, texture2d<float> et, float3 dir, float rough)
{
   if ((int(f.envm.x) & 1) != 0)
   {
      // Render 3D's equirect lookup: u from atan(z, x) + rotation, v = 0 at the zenith; lod = rough * max lod.
      constexpr sampler es(s_address::repeat, t_address::clamp_to_edge, filter::linear, mip_filter::linear);
      const float2 uv = float2((atan2(dir.z, dir.x) + f.envm.y) / 6.28318530718 + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265359);
      return et.sample(es, uv, level(max(clamp(rough, 0.0, 1.0) * f.envm.z - f.envm.w, 0.0))).rgb * f.params.z;
   }
   const float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
   const float3 lower = mix(f.ground.xyz, f.horizon.xyz, smoothstep(0.0, 0.5, t));
   const float3 env = mix(lower, f.sky.xyz, smoothstep(0.5, 1.0, t));
   return mix(env, f.horizon.xyz, rough * rough) * f.params.z;
}
float distGGX(float nDotH, float rough) { const float a = rough * rough, a2 = a * a; const float d = nDotH * nDotH * (a2 - 1.0) + 1.0; return a2 / max(3.14159265 * d * d, 1e-7); }
float geomSchlick(float nDotV, float rough) { const float r = rough + 1.0, k = (r * r) / 8.0; return nDotV / (nDotV * (1.0 - k) + k); }
float3 fresnel(float cosT, float3 f0) { return f0 + (1.0 - f0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0); }
float3 fresnelRough(float cosT, float3 f0, float rough) { const float3 ceil = max(float3(1.0 - rough), f0); return f0 + (ceil - f0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0); }
float lightFalloff(float dist, float k) { if (k >= 0.0) return 1.0 / (1.0 + dist * dist * k); return min(1.0 / max(dist * dist * -k, 1e-4), 20.0); }
float shadowFactor(constant FrameU& f, depth2d<float> sm, float3 wp, float3 n, float3 ld)
{
   const float4 lp = f.lightVP * float4(wp, 1.0);
   const float3 pr = lp.xyz / max(lp.w, 1e-6);
   const float2 uv = float2(pr.x * 0.5 + 0.5, 0.5 - pr.y * 0.5);
   const float z = pr.z * 0.5 + 0.5;
   if (z > 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
   const float cosL = clamp(dot(n, ld), 0.05, 1.0);
   const float bias = f.shP.z * (1.0 + min(sqrt(1.0 - cosL * cosL) / cosL, 10.0));
   constexpr sampler cs(coord::normalized, filter::linear, address::clamp_to_edge, compare_func::less_equal);
   float lit = 0.0;
   for (int x = -1; x <= 1; x++)
      for (int y = -1; y <= 1; y++)
         lit += sm.sample_compare(cs, uv + float2(float(x), float(y)) * f.shQ.x * f.shQ.y, z - bias);
   return mix(1.0, lit / 9.0, f.shP.w);
}
struct SkyOut { float4 pos [[position]]; };
struct RTArgs { instance_acceleration_structure tlas; };
bool occluded(instance_acceleration_structure tlas, float3 o, float3 d, float tmax)
{
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   it.accept_any_intersection(true);
   ray r(o, d, 0.0, tmax);
   intersection_result<triangle_data, instancing> h = it.intersect(r, tlas);
   return h.type == intersection_type::triangle;
}
struct RInst { float4 bm; float4 rg; ulong vb; ulong ib; float4x4 normalM; };
// One hit: closest hit -> vertex normal, Lambert from every light (RT shadowed), ambient, env spec. Miss -> env.
// hp/hn/alb return the hit (alb = diffuse albedo, 0 on a miss) so GI can take one more bounce from it.
float3 shadeHit(constant FrameU& f, texture2d<float> et, instance_acceleration_structure tlas, constant RInst* inst, float3 o, float3 d, float rough,
                bool giMode, thread float3& hp, thread float3& hn, thread float3& alb, thread float3& mir, bool withEnv = true, int pick = -1,
                bool withSpec = true)
{
   alb = float3(0.0); mir = float3(0.0);
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   ray r(o, d, 0.0, 1e4);
   intersection_result<triangle_data, instancing> h = it.intersect(r, tlas);
   if (h.type != intersection_type::triangle) return giMode ? sampleEnv(f, et, d, 1.0) * f.ambient.xyz * 3.0 : sampleEnv(f, et, d, rough);
   constant RInst& I = inst[h.instance_id];
   const device uint* ib = reinterpret_cast<const device uint*>(I.ib);
   const device float* vb = reinterpret_cast<const device float*>(I.vb);
   const uint3 ix = uint3(ib[h.primitive_id * 3], ib[h.primitive_id * 3 + 1], ib[h.primitive_id * 3 + 2]);
   const float2 bc = h.triangle_barycentric_coord;
   const float3 w = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
   float3 nl = float3(vb[ix.x * 6 + 3], vb[ix.x * 6 + 4], vb[ix.x * 6 + 5]) * w.x
             + float3(vb[ix.y * 6 + 3], vb[ix.y * 6 + 4], vb[ix.y * 6 + 5]) * w.y
             + float3(vb[ix.z * 6 + 3], vb[ix.z * 6 + 4], vb[ix.z * 6 + 5]) * w.z;
   float3 n = normalize((I.normalM * float4(nl, 0.0)).xyz);
   if (dot(n, d) > 0.0) n = -n;
   const float3 p = o + d * h.distance;
   const float3 base = I.bm.xyz; const float metal = I.bm.w;
   const float3 envIrr = sampleEnv(f, et, n, 1.0) * f.ambient.xyz * 3.0; // same ambient convention as r_fs
   float3 diff = float3(0.0);
   const int count = int(f.params.w);
   // pick >= 0: one light chosen by the caller, weighted by the light count (unbiased, one shadow ray per bounce).
   const int i0 = pick >= 0 ? min(pick, max(count - 1, 0)) : 0;
   const int i1 = pick >= 0 ? i0 + 1 : count;
   const float lw = pick >= 0 ? float(count) : 1.0;
   for (int i = i0; i < 8; i++)
   {
      if (i >= i1 || i >= count) break;
      constant LightU& L = f.lights[i];
      const int type = int(L.vec.w);
      if (type == 3) { diff += L.color.xyz * L.terms.x * 3.14159265 * lw; continue; }
      float3 ld; float atten = 1.0;
      if (type == 1 || type == 4 || type == 5)
      {
         const float3 tl = L.vec.xyz - p; const float dist = length(tl);
         ld = tl / max(dist, 1e-4);
         atten = lightFalloff(dist, L.terms.z);
         if (L.terms.w > 0.0 && dist > L.terms.w) atten = 0.0;
         if (type == 4)
         {
            const float cosCur = dot(-ld, L.aim.xyz);
            const float cosOuter = cos(L.spot.x * 0.017453292);
            atten *= cosCur > cosOuter ? 1.0 : 0.0;
         }
         if (type == 5) atten *= max(dot(-ld, L.aim.xyz), 0.0);
      }
      else ld = normalize(L.vec.xyz);
      if (atten > 0.0)
      {
         const float tm = (type == 1 || type == 4 || type == 5) ? max(length(L.vec.xyz - p) - 0.01, 0.0) : 1e4;
         if (occluded(tlas, p + n * f.shQ.w, ld, tm)) atten *= (1.0 - f.shP.w);
      }
      diff += L.color.xyz * atten * max(dot(n, ld), 0.0) * L.terms.x * lw;
   }
   const float3 spec = sampleEnv(f, et, reflect(d, n), I.rg.x) * metal * base;
   hp = p; hn = n; alb = base * (1.0 - metal); mir = base * metal;
   // Lambert radiance: base/pi * light irradiance (r_fs's direct term), env as r_fs's ambient.
   // withEnv false: the caller's next bounce carries the env; withSpec false: the caller follows the mirror direction.
   return alb * ((withEnv ? envIrr : float3(0.0)) + diff / 3.14159265) + (withSpec ? spec : float3(0.0));
}
float3 cosineDir(float3 n, float u1, float u2);
float rnd(thread uint& s);
float3 ggxSampleH(float3 n, float a, float u1, float u2);
// Screen-space radiance feedback: a hit that is on screen (same distance and facing as the guide there) reads
// last frame's accumulated GI irradiance at that pixel. alb * it is the hit's indirect light with every bounce,
// converging frame by frame; off-screen hits fall back to an explicit bounce.
bool histAt(constant FrameU& f, texture2d<float> nz, texture2d<float> hist, float3 p, float3 n, thread float3& irr)
{
   const float4 c = f.viewProj * float4(p, 1.0);
   if (c.w <= 1e-4) return false;
   const float2 ndc = c.xy / c.w;
   if (abs(ndc.x) >= 1.0 || abs(ndc.y) >= 1.0) return false;
   const float2 sz = float2(nz.get_width(), nz.get_height());
   const uint2 q = uint2(min((ndc * 0.5 + 0.5) * sz, sz - 1.0)); // row 0 is GL's bottom, as in tr_fs
   const float4 g = nz.read(q);
   const float dist = length(p - f.eye.xyz);
   if (g.w <= 0.0 || abs(g.w - dist) > 0.02 * dist + 0.01 || dot(g.xyz, n) < 0.8) return false;
   irr = hist.read(q).rgb;
   return true;
}
// Reflection: the hit plus its indirect light (walls lit only by bounce light must not mirror black).
// Noisy per frame; res_fs accumulates reflections over time.
float3 traceReflection(constant FrameU& f, texture2d<float> et, instance_acceleration_structure tlas, constant RInst* inst, float3 o, float3 d, float rough, thread uint& seed,
                       texture2d<float> nz, texture2d<float> hist, bool hv)
{
   float3 hp, hn, alb, mir;
   const bool bounce = f.gi.x > 0.5;
   float3 L = shadeHit(f, et, tlas, inst, o, d, rough, false, hp, hn, alb, mir, !bounce);
   float3 irr;
   if (any(alb > 0.0) && bounce && hv && histAt(f, nz, hist, hp, hn, irr))
      L += alb * irr * f.gi.y;
   else if (any(alb > 0.0) && bounce)
   {
      float3 hp2, hn2, alb2, mir2;
      const float3 d2 = cosineDir(hn, rnd(seed), rnd(seed));
      const int pk = int(rnd(seed) * f.params.w);
      const float3 L2 = shadeHit(f, et, tlas, inst, hp + hn * f.shQ.w, d2, 1.0, true, hp2, hn2, alb2, mir2, true, pk);
      L += alb * L2 * f.gi.y / (1.0 - clamp(dot(alb2, float3(0.3333)), 0.0, 0.8)); // later bounces as GI's geometric tail
   }
   return L;
}
uint pcg(uint v) { v = v * 747796405u + 2891336453u; uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u; return (w >> 22u) ^ w; }
float rnd(thread uint& s) { s = pcg(s); return float(s) * (1.0 / 4294967296.0); }
float3 cosineDir(float3 n, float u1, float u2)
{
   const float r = sqrt(u1), phi = 6.2831853 * u2;
   const float3 up = abs(n.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
   const float3 t = normalize(cross(up, n)); const float3 b = cross(n, t);
   return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u1, 0.0)));
}
constant bool kMrt [[function_constant(0)]];
struct FOut { float4 c0 [[color(0)]]; float4 c1 [[color(1), function_constant(kMrt)]]; float4 c2 [[color(2), function_constant(kMrt)]];
             float4 c3 [[color(3), function_constant(kMrt)]]; }; // c3: normal, distance to the eye (denoiser guides)
float3 aces(float3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
float3 toSrgb(float3 c) { c = max(c, float3(0.0)); return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c < float3(0.0031308)); }

// ---- Half-res RT shadows (C18): vis_fs traces one shadow ray per light (aim.w = channel) at full-res pixel 2q
// from the depth prepass; r_fs takes the 2x2 texels around it that lie on its surface (distance within 2%), and
// traces its own ray where none does (silhouettes, thin parts) or where they disagree (shadow edges stay full-res).
struct VisU { float4x4 invVP; float4 info; }; // info: full width, height
struct VisOut { float4 v [[color(0)]]; float d [[color(1)]]; };
float3 visWp(constant VisU& u, depth2d<float> dt, int2 px)
{
   const float z = dt.read(uint2(px));
   const float2 ndc = float2(2.0 * (float(px.x) + 0.5) / u.info.x - 1.0, 2.0 * (float(px.y) + 0.5) / u.info.y - 1.0);
   const float4 pw = u.invVP * float4(ndc, z * 2.0 - 1.0, 1.0);
   return pw.xyz / pw.w;
}
fragment VisOut vis_fs(SkyOut in [[stage_in]], constant VisU& u [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                       constant RTArgs& rtArgs [[buffer(3)]], depth2d<float> dt [[texture(0)]])
{
   VisOut o; o.v = float4(1.0); o.d = -1.0;
   const int W = int(u.info.x), H = int(u.info.y);
   const int2 px = int2(in.pos.xy) * 2;
   if (px.x >= W || px.y >= H) return o;
   if (dt.read(uint2(px)) >= 1.0) return o;
   const float3 p = visWp(u, dt, px);
   const int2 ax = int2(min(px.x + 1, W - 1), px.y), bx = int2(max(px.x - 1, 0), px.y);
   const int2 ay = int2(px.x, min(px.y + 1, H - 1)), by = int2(px.x, max(px.y - 1, 0));
   const float3 pax = visWp(u, dt, ax), pbx = visWp(u, dt, bx), pay = visWp(u, dt, ay), pby = visWp(u, dt, by);
   const float3 dx = distance(pax, p) < distance(pbx, p) ? pax - p : p - pbx;
   const float3 dy = distance(pay, p) < distance(pby, p) ? pay - p : p - pby;
   float3 n = cross(dx, dy);
   n = dot(n, n) > 1e-20 ? normalize(n) : normalize(f.eye.xyz - p);
   if (dot(n, f.eye.xyz - p) < 0.0) n = -n;
   o.d = distance(p, f.eye.xyz);
   const int count = int(f.params.w);
   for (int i = 0; i < 8; i++)
   {
      if (i >= count) break;
      constant LightU& L = f.lights[i];
      if (L.aim.w < -0.5) continue;
      const int ch = min(int(L.aim.w + 0.5), 3);
      const int type = int(L.vec.w);
      float3 ld; float tmax = 1e4;
      if (type == 1 || type == 4) { const float3 tl = L.vec.xyz - p; const float dist = length(tl); ld = tl / max(dist, 1e-4); tmax = max(dist - 0.01, 0.0); }
      else ld = normalize(L.vec.xyz);
      if (occluded(rtArgs.tlas, p + n * f.shQ.w, ld, tmax)) o.v[ch] = 0.0;
   }
   return o;
}
float upVis(texture2d<float> vt, texture2d<float> vd, float2 pos, float dist, int ch)
{
   const int2 p = int2(pos);
   const int2 qb = p / 2;
   const float2 fw = float2(p - qb * 2) * 0.5;
   const int2 sz = int2(vt.get_width(), vt.get_height());
   float acc = 0.0, ws = 0.0, lo = 1.0, hi = 0.0;
   for (int j = 0; j < 2; j++)
      for (int i = 0; i < 2; i++)
      {
         const float bw = (i == 1 ? fw.x : 1.0 - fw.x) * (j == 1 ? fw.y : 1.0 - fw.y);
         if (bw <= 0.0) continue;
         const uint2 q = uint2(min(qb + int2(i, j), sz - 1));
         const float dq = vd.read(q).x;
         if (dq < 0.0 || abs(dq - dist) > 0.02 * dist) continue;
         const float v = vt.read(q)[ch];
         acc += bw * v; ws += bw; lo = min(lo, v); hi = max(hi, v);
      }
   return (ws < 0.2 || hi - lo > 0.5) ? -1.0 : acc / ws; // a shadow edge between the texels: trace this pixel

}
fragment FOut r_fs(VOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant InstU& m [[buffer(2)]],
                     depth2d<float> sm [[texture(0)]], constant RTArgs& rtArgs [[buffer(3)]],
                     constant RInst* instInfo [[buffer(4)]], texture2d<float> et [[texture(9)]],
                     texture2d<float> visT [[texture(7)]], texture2d<float> visD [[texture(8)]])
{
   const float3 n = normalize(in.n);
   const float3 base = m.base.xyz;
   const float metal = clamp(m.mr.x, 0.0, 1.0);
   const float rough = clamp(m.mr.y, 0.045, 1.0);
   const float3 viewDir = normalize(f.eye.xyz - in.wp);
   const float nDotV = max(dot(n, viewDir), 1e-4);
   const float3 f0 = mix(float3(0.04), base, metal); // ior 1.5, specular 1
   float3 col = float3(0.0);
   float3 floorBounce = float3(0.0);
   const int count = int(f.params.w);
   for (int i = 0; i < 8; i++)
   {
      if (i >= count) break;
      constant LightU& L = f.lights[i];
      const int type = int(L.vec.w);
      const float3 radiance0 = L.color.xyz; // linear * intensity
      if (type == 3) { col += base * (1.0 - metal) * radiance0 * L.terms.x; continue; }
      // Area lights: a small visibility loop over 8 stratified points on the rectangle (rotated per pixel and
      // frame) gives E = mean(falloff * cos at light * cos at surface * visible); the BRDF is evaluated once
      // toward the centre. Diffuse energy, shape and soft RT shadows match the tracer; kept compact for occupancy.
      float areaE = -1.0;
      if (type == 5)
      {
         float3 aR = cross(float3(0.0, 1.0, 0.0), L.aim.xyz);
         aR = dot(aR, aR) < 1e-4 ? float3(1.0, 0.0, 0.0) : normalize(aR);
         const float3 aU = cross(L.aim.xyz, aR);
         uint ars = pcg(uint(in.pos.x) * 7919u + 104729u * uint(in.pos.y)) ^ pcg(uint(f.gi.z) * 31337u + uint(i));
         const bool rtSh = (int(f.shQ.z + 0.5) & 1) != 0;
         areaE = 0.0;
         for (int si = 0; si < 8; si++)
         {
            const float jx = (float(si & 3) + rnd(ars)) * 0.25, jy = (float(si >> 2) + rnd(ars)) * 0.5;
            const float3 tl = L.vec.xyz + aR * ((jx - 0.5) * L.spot.z) + aU * ((jy - 0.5) * L.spot.w) - in.wp;
            const float dist = length(tl); const float3 ld = tl / max(dist, 1e-4);
            float e = lightFalloff(dist, L.terms.z) * max(dot(-ld, L.aim.xyz), 0.0) * max(dot(n, ld), 0.0);
            if (L.terms.w > 0.0 && dist > L.terms.w) e = 0.0;
            if (e > 0.0 && rtSh && occluded(rtArgs.tlas, in.wp + n * f.shQ.w, ld, max(dist - 0.01, 0.0))) e *= 1.0 - f.shP.w;
            areaE += e * 0.125;
         }
         if (areaE <= 0.0) continue;
      }
      float3 lightDir; float atten = 1.0;
      if (type == 1 || type == 4 || type == 5)
      {
         const float3 toLight = L.vec.xyz - in.wp;
         const float dist = length(toLight);
         lightDir = toLight / max(dist, 1e-4);
         atten = lightFalloff(dist, L.terms.z);
         if (L.terms.w > 0.0 && dist > L.terms.w) atten = 0.0;
         if (type == 4)
         {
            const float cosCur = dot(-lightDir, L.aim.xyz);
            const float cosOuter = cos(L.spot.x * 0.017453292);
            const float smoothW = (1.0 - cosOuter) * L.spot.y;
            float sa = cosCur > cosOuter ? cosCur : 0.0;
            if (smoothW > 0.0) sa *= smoothstep(0.0, smoothW, cosCur - cosOuter);
            atten *= sa;
         }
      }
      else lightDir = normalize(L.vec.xyz);
      const float3 halfway = normalize(lightDir + viewDir);
      float nDotL;
      if (type == 2) { const float wrap = 0.35; nDotL = max((dot(n, lightDir) + wrap) / (1.0 + wrap), 0.0); }
      else nDotL = max(dot(n, lightDir), 0.0);
      if (type == 5) nDotL = max(nDotL, 1e-3); // the centre may face away while part of the rectangle is seen
      if (nDotL <= 0.0) continue;
      const float nDotH = max(dot(n, halfway), 0.0);
      const float d = distGGX(nDotH, rough);
      const float g = geomSchlick(nDotV, rough) * geomSchlick(nDotL, rough);
      const float3 fr = fresnel(max(dot(halfway, viewDir), 0.0), f0);
      const float3 spec = (d * g * fr) / max(4.0 * nDotV * nDotL, 1e-7);
      const float3 kD = (float3(1.0) - fr) * (1.0 - metal);
      const float3 radiance = radiance0 * atten;
      floorBounce += radiance * max(lightDir.y, 0.0) * L.terms.x;
      float shadow = 1.0;
      if (type == 5) shadow = areaE / max(atten * nDotL, 1e-6); // radiance * nDotL * shadow == radiance0 * E
      else
      {
         if (f.shP.x > 0.5 && i == int(f.shP.y)) shadow = shadowFactor(f, sm, in.wp, n, lightDir);
         if ((int(f.shQ.z + 0.5) & 1) != 0)
         {
            float vis = -1.0;
            if ((int(f.shQ.z + 0.5) & 4) != 0 && L.aim.w > -0.5)
               vis = upVis(visT, visD, in.pos.xy, distance(in.wp, f.eye.xyz), int(L.aim.w + 0.5));
            if (vis < 0.0)
            {
               const float tmax = (type == 1 || type == 4) ? max(length(L.vec.xyz - in.wp) - 0.01, 0.0) : 1e4;
               vis = occluded(rtArgs.tlas, in.wp + n * f.shQ.w, lightDir, tmax) ? 0.0 : 1.0;
            }
            shadow = mix(1.0 - f.shP.w, 1.0, vis);
         }
      }
      col += (kD * base / 3.14159265 * L.terms.x + spec * L.terms.y) * radiance * nDotL * shadow;
   }
   if (!(f.gi.x > 0.5)) col += base / 3.14159265 * floorBounce * (0.25 * (1.0 - n.y)) * (1.0 - metal); // fake bounce only without real GI
   // GI and traced reflections run in tr_fs at half res; this pass writes their demodulated weights instead.
   const float3 irradiance = sampleEnv(f, et, n, 1.0) * f.ambient.xyz * 3.0;
   const float3 fAmb = fresnelRough(nDotV, f0, rough);
   const float3 envRefl = sampleEnv(f, et, reflect(-viewDir, n), rough) * fAmb;
   const bool reflOn = (int(f.shQ.z + 0.5) & 2) != 0 && rough < 0.55;
   const float envKeep = reflOn ? smoothstep(0.3, 0.55, rough) : 1.0; // the env share of the reflection; the rest is traced
   const bool giOn = f.gi.x > 0.5;
   if (!giOn) col += (float3(1.0) - fAmb) * (1.0 - metal) * base * irradiance;
   col += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
   // Glass (transmission): refract in, find the far side of the hit object, refract out (or reflect on total internal
   // reflection), shade what lies beyond. Tint and Fresnel as Render 3D's: beyond * mix(1, base, 0.4) * (1 - F).
   // Needs the TLAS; without it the glass shows the environment along one refracted ray.
   const float trans = clamp(m.mr.z, 0.0, 1.0);
   const bool rtOn = (int(f.shQ.z + 0.5) & 3) != 0 || f.gi.x > 0.5;
   if (trans > 0.0 && rtOn)
   {
      const float eta = 1.0 / max(m.mr.w, 1.0001);
      float3 rd = refract(-viewDir, n, eta);
      if (dot(rd, rd) < 1e-6) rd = reflect(-viewDir, n);
      float3 o2 = in.wp - n * f.shQ.w, d2 = rd;
      intersector<triangle_data, instancing> gt;
      gt.assume_geometry_type(geometry_type::triangle);
      ray gr(o2, d2, 0.0, 1e4);
      intersection_result<triangle_data, instancing> gh = gt.intersect(gr, rtArgs.tlas);
      if (gh.type == intersection_type::triangle)
      {
         constant RInst& I = instInfo[gh.instance_id];
         const device uint* ib = reinterpret_cast<const device uint*>(I.ib);
         const device float* vb = reinterpret_cast<const device float*>(I.vb);
         const uint3 ix = uint3(ib[gh.primitive_id * 3], ib[gh.primitive_id * 3 + 1], ib[gh.primitive_id * 3 + 2]);
         const float2 bc = gh.triangle_barycentric_coord;
         const float3 w3 = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
         const float3 nl = float3(vb[ix.x * 6 + 3], vb[ix.x * 6 + 4], vb[ix.x * 6 + 5]) * w3.x
                         + float3(vb[ix.y * 6 + 3], vb[ix.y * 6 + 4], vb[ix.y * 6 + 5]) * w3.y
                         + float3(vb[ix.z * 6 + 3], vb[ix.z * 6 + 4], vb[ix.z * 6 + 5]) * w3.z;
         float3 ne = normalize((I.normalM * float4(nl, 0.0)).xyz);
         if (dot(ne, d2) < 0.0) ne = -ne; // outward, along the ray leaving the object
         const float3 pe = o2 + d2 * gh.distance;
         float3 od = refract(d2, -ne, 1.0 / eta);
         if (dot(od, od) < 1e-6) od = reflect(d2, -ne);
         o2 = pe + ne * f.shQ.w; d2 = od;
      }
      float3 hp, hn, alb, mir;
      const float3 beyond = shadeHit(f, et, rtArgs.tlas, instInfo, o2, d2, 0.0, false, hp, hn, alb, mir, true, -1, true);
      col = mix(col, beyond * mix(float3(1.0), base, 0.4) * (float3(1.0) - fAmb), trans);
   }
   else if (trans > 0.0)
   {
      // No rays this frame: see the environment through the glass along the refracted view, tinted the same way.
      float3 rd = refract(-viewDir, n, 1.0 / max(m.mr.w, 1.0001));
      if (dot(rd, rd) < 1e-6) rd = reflect(-viewDir, n);
      col = mix(col, sampleEnv(f, et, rd, 0.0) * mix(float3(1.0), base, 0.4) * (float3(1.0) - fAmb), trans);
   }
   const float opaque = (trans > 0.0 && rtOn) ? 1.0 - trans : 1.0; // glass takes no GI of its own
   FOut o;
   if (!kMrt)
   {
      float3 fin = (col + envRefl) * f.params.x;
      if (int(f.params.y) == 1) fin = aces(fin);
      o.c0 = float4(toSrgb(fin), 1.0);
      return o;
   }
   // With GI, the rough share of the reflection sees the room, not only the env: its lobe is wide enough that the
   // cosine-weighted GI radiance stands in for it (both carry the env on a miss, ambient * 3 == 1 in linear terms).
   o.c0 = float4((col + (giOn ? float3(0.0) : envRefl * envKeep)) * f.params.x, 1.0);
   o.c1 = float4(giOn ? ((float3(1.0) - fAmb) * (1.0 - metal) * base * opaque + fAmb * envKeep) * f.gi.y * f.params.x : float3(0.0), rough); // GI weight
   o.c2 = float4(fAmb * (1.0 - envKeep) * f.params.x, reflOn ? 1.0 : 0.0); // traced-reflection weight
   o.c3 = float4(n, length(in.wp - f.eye.xyz));
   return o;
}

// ---- Half-res trace (R3): each half-res texel traces at one full-res pixel 2q + off, off rotating over the 2x2
// each frame; every lane traces (no idle quad lanes). Position from the eye ray and the guide distance.
struct TrU { float4x4 invVP; float4 info; float4 hist; }; // info: offset x, y, full width, height; hist.x: GI history valid
struct TrOut { float4 g [[color(0)]]; float4 r [[color(1)]]; }; // raw GI irradiance / reflection radiance, a = valid
fragment TrOut tr_fs(SkyOut in [[stage_in]], constant TrU& u [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                     constant RTArgs& rtArgs [[buffer(3)]], constant RInst* instInfo [[buffer(4)]],
                     texture2d<float> nz [[texture(0)]], texture2d<float> gw [[texture(1)]], texture2d<float> rw [[texture(2)]],
                     texture2d<float> hist [[texture(3)]], texture2d<float> et [[texture(9)]])
{
   const bool hv = u.hist.x > 0.5;
   TrOut o; o.g = float4(0.0); o.r = float4(0.0);
   const uint2 px = uint2(in.pos.xy) * 2u + uint2(u.info.xy);
   if (px.x >= uint(u.info.z) || px.y >= uint(u.info.w)) return o;
   const float4 c = nz.read(px);
   if (c.w <= 0.0) return o;
   const float3 n = normalize(c.xyz);
   const float2 ndc = float2(2.0 * (float(px.x) + 0.5) / u.info.z - 1.0, 2.0 * (float(px.y) + 0.5) / u.info.w - 1.0);
   const float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   const float3 d = normalize(pw.xyz / pw.w - f.eye.xyz);
   const float3 org = f.eye.xyz + d * c.w + n * f.shQ.w;
   const float4 g = gw.read(px);
   const float rough = g.w;
   if (any(g.rgb > 0.0))
   {
      uint seed = pcg(px.x + 8192u * px.y) ^ pcg(uint(f.gi.z) * 9781u + 1u);
      const int rays = max(int(f.gi.w), 1);
      float3 acc = float3(0.0);
      for (int k = 0; k < rays; k++)
      {
         const float3 dir = cosineDir(n, rnd(seed), rnd(seed));
         float3 hp, hn, alb, mir;
         float3 L1 = shadeHit(f, et, rtArgs.tlas, instInfo, org, dir, 1.0, true, hp, hn, alb, mir, false, int(rnd(seed) * f.params.w), false);
         float3 irr;
         if (any(alb > 0.0) && hv && histAt(f, nz, hist, hp, hn, irr))
            L1 += alb * irr; // every bounce through last frame's GI at that point
         else if (any(mir > 0.0))
         {
            // Metal hit: follow its mirror direction every frame (an env-only answer leaves what a mirror lights dark).
            float3 hp2, hn2, alb2, mir2;
            const float3 L2 = shadeHit(f, et, rtArgs.tlas, instInfo, hp + hn * f.shQ.w, reflect(dir, hn), 0.3, false, hp2, hn2, alb2, mir2, true, int(rnd(seed) * f.params.w));
            float3 irr2;
            L1 += mir * (hv && any(alb2 > 0.0) && histAt(f, nz, hist, hp2, hn2, irr2) ? L2 + alb2 * irr2
                                                                                  : L2 / (1.0 - clamp(dot(alb2, float3(0.3333)), 0.0, 0.8)));
         }
         else if (any(alb > 0.0) && ((uint(f.gi.z) >> 2) & 1u) == 0u)
         {
            // Second bounce every other 4-frame cycle at twice the weight (unbiased; the history blends it);
            // later bounces as the geometric tail of that hit's albedo.
            float3 hp2, hn2, alb2, mir2;
            const float3 d2 = cosineDir(hn, rnd(seed), rnd(seed));
            const float3 L2 = shadeHit(f, et, rtArgs.tlas, instInfo, hp + hn * f.shQ.w, d2, 1.0, true, hp2, hn2, alb2, mir2, true, int(rnd(seed) * f.params.w));
            const float a = clamp(dot(alb2, float3(0.3333)), 0.0, 0.8);
            L1 += alb * L2 * 2.0 / (1.0 - a);
         }
         acc += L1;
      }
      o.g = float4(acc / float(rays), 1.0);
   }
   if (rw.read(px).w > 0.5)
   {
      uint rs = pcg(px.x * 1973u + 9277u * px.y) ^ pcg(uint(f.gi.z) * 26699u + 7u);
      float3 jd = reflect(d, n);
      if (rough > 0.04)
      {
         jd = reflect(d, ggxSampleH(n, rough * rough, rnd(rs), rnd(rs))); // the GGX lobe the tracer samples
         if (dot(jd, n) < 0.02) jd = normalize(jd + n * (0.02 - dot(jd, n)));
      }
      o.r = float4(traceReflection(f, et, rtArgs.tlas, instInfo, org, jd, rough, rs, nz, hist, hv), 1.0);
   }
   return o;
}

struct ResU { float4 p; float4 clear; float4 q; }; // p: blend alpha, tonemap, history valid, refl blend; q: half-res offset x, y, GI on, HDRI background
struct ResOut { float4 c0 [[color(0)]]; float4 c1 [[color(1)]]; float4 c2 [[color(2)]]; };
// Joint-bilateral upsample of the half-res trace (3x3 texels, spatial x normal x distance weights, a small floor so
// thin features fall back to their spatial neighbours), temporal accumulation, then remodulation by the weights.
fragment ResOut res_fs(SkyOut in [[stage_in]], constant ResU& u [[buffer(0)]], texture2d<float> hdr [[texture(0)]], texture2d<float> gw [[texture(1)]],
                       texture2d<float> hist [[texture(2)]], texture2d<float> rw [[texture(3)]], texture2d<float> rhist [[texture(4)]],
                       texture2d<float> trG [[texture(5)]], texture2d<float> trR [[texture(6)]], texture2d<float> nz [[texture(7)]])
{
   const uint2 px = uint2(in.pos.xy);
   const float4 c = hdr.read(px);
   ResOut o;
   if (c.a < 0.5)
   {
      float3 b = c.rgb; if (int(u.p.y) == 1) b = aces(b);
      o.c0 = u.q.w > 0.5 ? float4(toSrgb(b), 1.0) : float4(u.clear.xyz, 1.0); o.c1 = float4(0.0); o.c2 = float4(0.0); return o;
   }
   const float4 cz = nz.read(px);
   const float4 wg = gw.read(px), wr = rw.read(px);
   const int2 off = int2(u.q.xy);
   const int2 q0 = (int2(px) - off + 2) / 2 - 1;
   const int W = int(hdr.get_width()), H = int(hdr.get_height()), HW = int(trG.get_width()), HH = int(trG.get_height());
   float3 g = float3(0.0), r = float3(0.0); float gs = 0.0, rsum = 0.0;
   for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++)
   {
      const int2 q = q0 + int2(x, y);
      if (q.x < 0 || q.y < 0 || q.x >= HW || q.y >= HH) continue;
      const int2 s = q * 2 + off;
      if (s.x >= W || s.y >= H) continue;
      const float4 cs = nz.read(uint2(s));
      if (cs.w <= 0.0) continue;
      const float2 dd = float2(s - int2(px));
      const float wn = pow(max(dot(cz.xyz, cs.xyz), 0.0), 32.0);
      const float wz = exp(-abs(cs.w - cz.w) / (0.02 * cz.w + 1e-3));
      const float w = exp(-dot(dd, dd) * 0.35) * (wn * wz + 1e-3);
      const float4 tg = trG.read(uint2(q)); if (tg.a > 0.5) { g += tg.rgb * w; gs += w; }
      const float4 tr = trR.read(uint2(q)); if (tr.a > 0.5) { r += tr.rgb * w; rsum += w; }
   }
   const bool haveG = gs > 0.0, haveR = rsum > 0.0;
   g /= max(gs, 1e-8); r /= max(rsum, 1e-8);
   const float3 h = hist.read(px).rgb;
   const bool hv = u.p.z > 0.5;
   const float3 acc = u.q.z < 0.5 ? float3(0.0) : (haveG ? (hv ? mix(h, g, u.p.x) : g) : (hv ? h : float3(0.0))); // GI off -> no GI, never stale history
   const float3 rh = rhist.read(px).rgb;
   const bool rv = u.clear.w > 0.5;
   const float3 racc = wr.a < 0.5 ? float3(0.0) : (haveR ? (rv ? mix(rh, r, u.p.w) : r) : (rv ? rh : float3(0.0)));
   float3 col = c.rgb + wg.rgb * acc + wr.rgb * racc;
   if (int(u.p.y) == 1) col = aces(col);
   o.c0 = float4(toSrgb(col), 1.0);
   o.c1 = float4(acc, 1.0);
   o.c2 = float4(racc, 1.0);
   return o;
}


// ---- GI denoiser (R3): edge-aware a-trous over the temporally accumulated GI, 5x5 B3 kernel at growing steps,
// weights stopped by normal and distance so light does not bleed across edges; then the composite.
struct AtU { float4 p; }; // x step
fragment float4 at_fs(SkyOut in [[stage_in]], constant AtU& u [[buffer(0)]], texture2d<float> gi [[texture(0)]], texture2d<float> nz [[texture(1)]])
{
   const int2 px = int2(in.pos.xy);
   const float4 c = nz.read(uint2(px));
   if (c.w <= 0.0) return float4(0.0);
   const int step = int(u.p.x);
   const float k[3] = { 0.375, 0.25, 0.0625 };
   const int W = int(gi.get_width()), H = int(gi.get_height());
   float3 sum = float3(0.0); float wsum = 0.0;
   for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++)
   {
      const int2 q = px + int2(x, y) * step;
      if (q.x < 0 || q.y < 0 || q.x >= W || q.y >= H) continue;
      const float4 cq = nz.read(uint2(q));
      if (cq.w <= 0.0) continue;
      const float wn = pow(max(dot(c.xyz, cq.xyz), 0.0), 64.0);
      const float wz = exp(-abs(cq.w - c.w) / max(0.01 * c.w * float(step) * length(float2(x, y)) + 1e-3, 1e-3));
      const float w = k[abs(x)] * k[abs(y)] * wn * wz;
      sum += gi.read(uint2(q)).rgb * w; wsum += w;
   }
   return float4(sum / max(wsum, 1e-6), 1.0);
}
struct CompU { float4 p; float4 clear; }; // p.y tonemap, p.z HDRI background
fragment float4 comp_fs(SkyOut in [[stage_in]], constant CompU& u [[buffer(0)]], texture2d<float> hdr [[texture(0)]], texture2d<float> gi [[texture(1)]], texture2d<float> rfl [[texture(2)]],
                        texture2d<float> gw [[texture(3)]], texture2d<float> rw [[texture(4)]])
{
   const uint2 px = uint2(in.pos.xy);
   const float4 c = hdr.read(px);
   if (c.a < 0.5)
   {
      float3 b = c.rgb; if (int(u.p.y) == 1) b = aces(b);
      return u.p.z > 0.5 ? float4(toSrgb(b), 1.0) : float4(u.clear.xyz, 1.0);
   }
   float3 col = c.rgb + gw.read(px).rgb * gi.read(px).rgb + rw.read(px).rgb * rfl.read(px).rgb;
   if (int(u.p.y) == 1) col = aces(col);
   return float4(toSrgb(col), 1.0);
}

// ---- path tracer: progressive, one sample per pass, accumulated in RGBA32F (rgb = radiance sum, a = coverage sum)
struct PtU { float4x4 invVP; float4 info; }; // x prior samples, y max bounces, z frame, w width
float3 ggxSampleH(float3 n, float a, float u1, float u2)
{
   const float cosT = sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1));
   const float sinT = sqrt(max(1.0 - cosT * cosT, 0.0)), phi = 6.2831853 * u2;
   const float3 up = abs(n.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
   const float3 t = normalize(cross(up, n)); const float3 b = cross(n, t);
   return normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + n * cosT);
}
// The path tracer's BSDF, ported from Render 3D's GL tracer (evalOpaqueTerms, lobeProbs) so the two agree:
// height-correlated Smith visibility, Fresnel at the half vector, diffuse takes what the specular leaves.
float ptS5(float c) { const float m = clamp(1.0 - c, 0.0, 1.0), m2 = m * m; return m2 * m2 * m; }
float ptVis(float nV, float nL, float a)
{
   const float a2 = a * a;
   const float gV = nL * sqrt(a2 + (1.0 - a2) * nV * nV), gL = nV * sqrt(a2 + (1.0 - a2) * nL * nL);
   return 0.5 / max(gV + gL, 1e-7);
}
float ptD(float nH, float a) { const float a2 = a * a, d = nH * nH * (a2 - 1.0) + 1.0; return a2 / max(3.14159265 * d * d, 1e-7); }
float2 ptProbs(float3 f0, float metal, float nV)
{
   const float f0s = max(f0.x, max(f0.y, f0.z)), fV = f0s + (1.0 - f0s) * ptS5(nV);
   float spec = mix(fV, 1.0, metal);
   const float diff = (1.0 - metal) * (1.0 - fV);
   spec = max(spec, 0.05 * (spec + diff));
   return float2(spec, diff) / max(spec + diff, 1e-6);
}
// f(V, L) * cos(L), and the pdf of drawing L by ptProbs. w: a light's diffuse and specular terms.
float3 ptEval(float3 n, float3 V, float3 L, float3 base, float metal, float rough, float3 f0, float2 w, thread float& pdf)
{
   const float nL = dot(n, L);
   if (nL <= 1e-6) { pdf = 0.0; return float3(0.0); }
   const float nV = max(dot(n, V), 1e-4);
   const float3 H = normalize(V + L);
   const float nH = max(dot(n, H), 0.0), vH = max(dot(V, H), 1e-4);
   const float a = max(0.002, rough * rough);
   const float D = ptD(nH, a);
   const float3 F = f0 + (float3(1.0) - f0) * ptS5(vH);
   const float3 fr = D * ptVis(nV, nL, a) * F * w.y + (float3(1.0) - F) * (1.0 - metal) * base * (w.x / 3.14159265);
   const float2 p = ptProbs(f0, metal, nV);
   pdf = p.x * D * nH / max(4.0 * vH, 1e-6) + p.y * nL / 3.14159265;
   return fr * nL;
}
fragment float4 pt_fs(SkyOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant PtU& u [[buffer(0)]],
                      constant RTArgs& rtArgs [[buffer(3)]], constant RInst* inst [[buffer(4)]], texture2d<float> prev [[texture(0)]],
                      texture2d<float> et [[texture(9)]])
{
   const uint2 px = uint2(in.pos.xy);
   uint seed = pcg(px.x + 8192u * px.y) ^ pcg(uint(u.info.z) * 2654435761u + 17u);
   const float H = float(prev.get_height());
   const float2 ndc = float2(2.0 * (float(px.x) + rnd(seed)) / u.info.w - 1.0, 2.0 * (float(px.y) + rnd(seed)) / H - 1.0);
   float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   float3 o = f.eye.xyz;
   float3 d = normalize(pw.xyz / pw.w - o);
   float3 L = float3(0.0), T = float3(1.0);
   float cov = 0.0;
   const int maxB = int(u.info.y);
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   for (int b = 0; b < maxB; b++)
   {
      ray r(o, d, 0.0, 1e4);
      intersection_result<triangle_data, instancing> h = it.intersect(r, rtArgs.tlas);
      if (h.type != intersection_type::triangle)
      {
         if (b > 0) L += T * sampleEnv(f, et, d, 0.0); // the sky itself, as the GL tracer; the cosine is in the sampling
         else if ((int(f.envm.x) & 2) != 0) { L = sampleEnv(f, et, d, 0.0); cov = 1.0; } // the HDRI as the background
         break;
      }
      if (b == 0) cov = 1.0;
      constant RInst& I = inst[h.instance_id];
      const device uint* ib = reinterpret_cast<const device uint*>(I.ib);
      const device float* vb = reinterpret_cast<const device float*>(I.vb);
      const uint3 ix = uint3(ib[h.primitive_id * 3], ib[h.primitive_id * 3 + 1], ib[h.primitive_id * 3 + 2]);
      const float2 bc = h.triangle_barycentric_coord;
      const float3 w3 = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
      const float3 nl = float3(vb[ix.x * 6 + 3], vb[ix.x * 6 + 4], vb[ix.x * 6 + 5]) * w3.x
                      + float3(vb[ix.y * 6 + 3], vb[ix.y * 6 + 4], vb[ix.y * 6 + 5]) * w3.y
                      + float3(vb[ix.z * 6 + 3], vb[ix.z * 6 + 4], vb[ix.z * 6 + 5]) * w3.z;
      float3 n = normalize((I.normalM * float4(nl, 0.0)).xyz);
      if (dot(n, d) > 0.0) n = -n;
      const float3 p = o + d * h.distance;
      const float3 base = I.bm.xyz; const float metal = clamp(I.bm.w, 0.0, 1.0); const float rough = clamp(I.rg.x, 0.045, 1.0);
      const float3 V = -d; const float nDotV = max(dot(n, V), 1e-4);
      const float3 f0 = mix(float3(0.04), base, metal);
      // direct light, the same model as the raster
      const int count = int(f.params.w);
      // next-event estimation: one light per bounce, picked uniformly, weighted by the count (unbiased)
      const int pick = min(int(rnd(seed) * float(max(count, 1))), max(count - 1, 0));
      for (int i = pick; i < pick + 1 && count > 0; i++)
      {
         constant LightU& Lt = f.lights[i];
         const int type = int(Lt.vec.w);
         if (type == 3) continue; // ambient arrives through the bounces
         const float lw = float(count);
         float3 ld; float atten = 1.0; float tmax = 1e4;
         if (type == 1 || type == 4 || type == 5)
         {
            float3 lp = Lt.vec.xyz;
            if (type == 5)
            {
               // Area: a uniform point on the rectangle (Render 3D's basis), cosine at the light below.
               float3 rgt = cross(float3(0.0, 1.0, 0.0), Lt.aim.xyz);
               rgt = dot(rgt, rgt) < 1e-4 ? float3(1.0, 0.0, 0.0) : normalize(rgt);
               const float3 upv = cross(Lt.aim.xyz, rgt);
               lp += rgt * ((rnd(seed) - 0.5) * Lt.spot.z) + upv * ((rnd(seed) - 0.5) * Lt.spot.w);
            }
            const float3 tl = lp - p; const float dist = length(tl);
            ld = tl / max(dist, 1e-4); tmax = max(dist - 0.01, 0.0);
            atten = lightFalloff(dist, Lt.terms.z);
            if (Lt.terms.w > 0.0 && dist > Lt.terms.w) atten = 0.0;
            if (type == 4)
            {
               const float cosCur = dot(-ld, Lt.aim.xyz), cosOuter = cos(Lt.spot.x * 0.017453292);
               const float smoothW = (1.0 - cosOuter) * Lt.spot.y;
               float sa = cosCur > cosOuter ? cosCur : 0.0;
               if (smoothW > 0.0) sa *= smoothstep(0.0, smoothW, cosCur - cosOuter);
               atten *= sa;
            }
            if (type == 5) atten *= max(dot(-ld, Lt.aim.xyz), 0.0);
         }
         else ld = normalize(Lt.vec.xyz);
         if (dot(n, ld) <= 1e-5 || atten <= 0.0) continue;
         if (occluded(rtArgs.tlas, p + n * f.shQ.w, ld, tmax)) continue;
         float pdfL = 0.0;
         L += T * ptEval(n, V, ld, base, metal, rough, f0, Lt.terms.xy, pdfL) * Lt.color.xyz * atten * lw;
      }
      // next direction: one lobe picked by Render 3D's lobe weights, weighted by the full BSDF over the mixture pdf
      const float2 lp = ptProbs(f0, metal, nDotV);
      const float a = max(0.002, rough * rough);
      float3 nd;
      if (rnd(seed) < lp.x) { nd = reflect(d, ggxSampleH(n, a, rnd(seed), rnd(seed))); }
      else { nd = cosineDir(n, rnd(seed), rnd(seed)); }
      float pdfB = 0.0;
      const float3 fB = ptEval(n, V, nd, base, metal, rough, f0, float2(1.0), pdfB);
      if (pdfB <= 1e-6) break;
      T *= fB / pdfB;
      if (b >= 2)
      {
         const float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
         if (rnd(seed) > q) break;
         T /= q;
      }
      o = p + n * f.shQ.w; d = nd;
   }
   L = min(L, float3(30.0)); // firefly guard
   const float4 pv = u.info.x > 0.5 ? prev.read(px) : float4(0.0);
   return pv + float4(L, cov);
}
struct PtResU { float4 p; float4 clear; }; // p: sample count, tonemap, exposure
fragment float4 ptres_fs(SkyOut in [[stage_in]], constant PtResU& u [[buffer(0)]], texture2d<float> acc [[texture(0)]])
{
   const float4 a = acc.read(uint2(in.pos.xy));
   const float cov = a.a / u.p.x;
   float3 col = a.rgb / max(a.a, 1e-4) * u.p.z; // mean radiance of the samples that hit
   if (int(u.p.y) == 1) col = aces(col);
   return float4(mix(u.clear.xyz, toSrgb(col), cov), 1.0);
}


// R5 culling + LOD: one thread per copy. Outside the view: dropped. Under ptPx pixels across: a point. Otherwise: the mesh.
struct CullU { float4x4 viewProj; float4 info; }; // x count, y mesh radius, z width px, w point threshold px
kernel void cull_k(uint i [[thread_position_in_grid]], constant CullU& u [[buffer(0)]], const device float4x4* models [[buffer(1)]],
                   device uint* visMesh [[buffer(2)]], device uint* visPt [[buffer(3)]], device atomic_uint* cnt [[buffer(4)]])
{
   if (i >= uint(u.info.x)) return;
   const float4x4 m = models[i];
   const float3 c = m[3].xyz;
   const float scale = max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));
   const float r = u.info.y * scale;
   const float4 cl = u.viewProj * float4(c, 1.0);
   const float rx = r * length(float3(u.viewProj[0][0], u.viewProj[1][0], u.viewProj[2][0]));
   const float ry = r * length(float3(u.viewProj[0][1], u.viewProj[1][1], u.viewProj[2][1]));
   const float rw = r * length(float3(u.viewProj[0][3], u.viewProj[1][3], u.viewProj[2][3]));
   if (cl.w + rw < 0.0) return;
   if (abs(cl.x) - rx > cl.w + rw || abs(cl.y) - ry > cl.w + rw || cl.z - r * 0.0 > cl.w * 1.0 + rw * 2.0) return;
   const float px = rx / max(cl.w, 1e-4) * u.info.z;
   if (cl.w > 0.0 && px < u.info.w) visPt[atomic_fetch_add_explicit(&cnt[1], 1u, memory_order_relaxed)] = i;
   else visMesh[atomic_fetch_add_explicit(&cnt[0], 1u, memory_order_relaxed)] = i;
}
// Builds the two indirect draws from the counters, then clears them for the next frame.
kernel void args_k(device atomic_uint* cnt [[buffer(0)]], device uint* argsMesh [[buffer(1)]], device uint* argsPt [[buffer(2)]], constant uint& indexCount [[buffer(3)]])
{
   const uint m = atomic_load_explicit(&cnt[0], memory_order_relaxed), p = atomic_load_explicit(&cnt[1], memory_order_relaxed);
   argsMesh[0] = indexCount; argsMesh[1] = m; argsMesh[2] = 0; argsMesh[3] = 0; argsMesh[4] = 0;
   argsPt[0] = 1; argsPt[1] = p; argsPt[2] = 0; argsPt[3] = 0;
   atomic_store_explicit(&cnt[0], 0u, memory_order_relaxed); atomic_store_explicit(&cnt[1], 0u, memory_order_relaxed);
}
struct VOutP { float4 pos [[position]]; float3 wp; float3 n; float ps [[point_size]]; };
// Points are sub-detail copies: lit by the first light and the ambient only, no PBR, no shadows.
fragment float4 rp_fs(VOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant InstU& m [[buffer(2)]])
{
   const float3 n = normalize(in.n);
   float3 ld = float3(0.0, 1.0, 0.0), lc = float3(0.0);
   if (int(f.params.w) > 0)
   {
      const LightU L = f.lights[0];
      const int type = int(L.vec.w);
      ld = (type == 1 || type == 4 || type == 5) ? normalize(L.vec.xyz - in.wp) : normalize(L.vec.xyz);
      lc = L.color.xyz * L.terms.x;
   }
   float3 c = m.base.xyz * (f.ambient.xyz * 1.4 + lc * max(dot(n, ld), 0.0)) * f.params.x;
   if (int(f.params.y) == 1) c = aces(c);
   return float4(toSrgb(c), 1.0);
}
vertex VOutP rp_vs(uint iid [[instance_id]], constant FrameU& f [[buffer(1)]], const device float4x4* models [[buffer(5)]], const device uint* vis [[buffer(6)]],
                   constant float& pxK [[buffer(7)]])
{
   const float4x4 m = models[vis[iid]];
   const float4 w = float4(m[3].xyz, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   c.y = -c.y;
   VOutP o; o.pos = c; o.wp = w.xyz; o.n = normalize(f.eye.xyz - w.xyz);
   o.ps = clamp(length(m[0].xyz) * pxK / max(c.w, 1e-4), 1.5, 16.0); return o;
}
vertex VOut ri_vs(uint vid [[vertex_id]], uint iid [[instance_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                  const device float4x4* models [[buffer(5)]], const device uint* vis [[buffer(6)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4x4 m = models[vis[iid]];
   const float4 w = m * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   c.y = -c.y;
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m * float4(n, 0.0)).xyz; return o;
}
vertex VOut ri_vs_old(uint vid [[vertex_id]], uint iid [[instance_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                  const device float4x4* models [[buffer(5)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4x4 m = models[iid];
   const float4 w = m * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   c.y = -c.y;
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m * float4(n, 0.0)).xyz; return o; // uniform scale: the model's 3x3 turns normals
}
// Runs: consecutive instances sharing a mesh and material drawn in one instanced call; per-copy data from a buffer.
vertex VOut rr_vs(uint vid [[vertex_id]], uint iid [[instance_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                  const device InstU* ms [[buffer(10)]], constant uint& first [[buffer(11)]])
{
   const device InstU& m = ms[first + iid];
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4 w = m.model * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   c.y = -c.y;
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m.normalM * float4(n, 0.0)).xyz; return o;
}
struct SOut { float4 pos [[position]]; };
vertex SOut shr_vs(uint vid [[vertex_id]], uint iid [[instance_id]], const device float* v [[buffer(0)]], constant float4x4& lvp [[buffer(1)]],
                   const device InstU* ms [[buffer(10)]], constant uint& first [[buffer(11)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   float4 c = lvp * (ms[first + iid].model * float4(p, 1.0));
   c.z = (c.z + c.w) * 0.5;
   SOut o; o.pos = c; return o;
}
vertex SOut sh_vs(uint vid [[vertex_id]], const device float* v [[buffer(0)]], constant float4x4& lvp [[buffer(1)]], constant InstU& m [[buffer(2)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   float4 c = lvp * (m.model * float4(p, 1.0));
   c.z = (c.z + c.w) * 0.5;
   SOut o; o.pos = c; return o;
}

vertex SkyOut sky_vs(uint vid [[vertex_id]]) { const float2 p[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) }; SkyOut o; o.pos = float4(p[vid], 1.0, 1.0); return o; }
fragment float4 sky_fs(SkyOut in [[stage_in]], constant float4& clearSrgb [[buffer(0)]]) { return float4(clearSrgb.xyz, 1.0); }
// HDRI background, drawn first under the scene. MRT: linear * exposure with a = 0 (the resolve tonemaps it); direct: display.
struct BgU { float4x4 invVP; float4 p; }; // p: width, height, MRT
fragment float4 bg_fs(SkyOut in [[stage_in]], constant BgU& u [[buffer(0)]], constant FrameU& f [[buffer(1)]], texture2d<float> et [[texture(9)]])
{
   const float2 ndc = float2(2.0 * in.pos.x / u.p.x - 1.0, 2.0 * in.pos.y / u.p.y - 1.0); // row 0 is GL's bottom
   const float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   float3 c = sampleEnv(f, et, normalize(pw.xyz / pw.w - f.eye.xyz), 0.0) * f.params.x;
   if (u.p.z > 0.5) return float4(c, 0.0);
   if (int(f.params.y) == 1) c = aces(c);
   return float4(toSrgb(c), 1.0);
}
)";

   class MetalImage final : public SharedImage
   {
   public:
      MetalImage(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLRenderPipelineState> pso, int w, int h, Format fmt)
          : mQueue(q), mPso(pso), mW(w), mH(h)
      {
         const bool half = fmt == Format::RGBA16F;
         mHalf = half;
         const int bpe = half ? 8 : 4;
         NSDictionary* props = @{
            (id)kIOSurfaceWidth : @(w), (id)kIOSurfaceHeight : @(h), (id)kIOSurfaceBytesPerElement : @(bpe),
            (id)kIOSurfacePixelFormat : @(half ? 'RGhA' : 'BGRA'),
         };
         mSurface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
         if (!mSurface)
            return;
         MTLTextureDescriptor* d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(half ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm)
                                                               width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
         d.storageMode = MTLStorageModeShared;
         mTex = [dev newTextureWithDescriptor:d iosurface:mSurface plane:0];
         CGLContextObj ctx = CGLGetCurrentContext();
         if (ctx && mTex)
         {
            glGenTextures(1, &mGl);
            glBindTexture(GL_TEXTURE_RECTANGLE, mGl);
            const CGLError err = half ? CGLTexImageIOSurface2D(ctx, GL_TEXTURE_RECTANGLE, GL_RGBA16F, w, h, GL_RGBA, GL_HALF_FLOAT, mSurface, 0)
                                      : CGLTexImageIOSurface2D(ctx, GL_TEXTURE_RECTANGLE, GL_RGBA, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mSurface, 0);
            if (err != kCGLNoError)
            {
               glDeleteTextures(1, &mGl);
               mGl = 0;
            }
            glBindTexture(GL_TEXTURE_RECTANGLE, 0);
         }
      }
      ~MetalImage() override
      {
         if (mGl)
            glDeleteTextures(1, &mGl);
         if (mSurface)
            CFRelease(mSurface);
      }
      bool Ok() const { return mTex != nil && mGl != 0; }
      id<MTLTexture> Tex() const { return mTex; }
      bool Half() const { return mHalf; }
      int Width() const override { return mW; }
      int Height() const override { return mH; }
      unsigned int GlTexture() const override { return mGl; }

      bool DrawTestTriangle(const float clear[4], const float tri[3]) override
      {
         MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
         pass.colorAttachments[0].texture = mTex;
         pass.colorAttachments[0].loadAction = MTLLoadActionClear;
         pass.colorAttachments[0].storeAction = MTLStoreActionStore;
         pass.colorAttachments[0].clearColor = MTLClearColorMake(clear[0], clear[1], clear[2], clear[3]);
         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
         [enc setRenderPipelineState:mPso];
         [enc setFragmentBytes:tri length:12 atIndex:0];
         [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
         [enc endEncoding];
         [cb commit];
         [cb waitUntilCompleted];
         return cb.status == MTLCommandBufferStatusCompleted;
      }

   private:
      id<MTLCommandQueue> mQueue;
      id<MTLRenderPipelineState> mPso;
      IOSurfaceRef mSurface = nullptr;
      id<MTLTexture> mTex = nil;
      unsigned int mGl = 0;
      int mW, mH;
      bool mHalf = false;
   };


   struct LightU { float vec[4], aim[4], color[4], terms[4], spot[4]; };
   struct FrameU
   {
      float viewProj[16], eye[4], ambient[4], sky[4], horizon[4], ground[4], params[4], lightVP[16], shP[4], shQ[4], gi[4], envm[4];
      LightU lights[8];
   };
   struct InstU { float model[16], normalM[16], base[4], mr[4]; };

   float ToLin(float c)
   {
      c = c < 0.0f ? 0.0f : c;
      return c < 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
   }
   void Lin3(const float* c, float* o, float scale = 1.0f)
   {
      for (int i = 0; i < 3; i++)
         o[i] = ToLin(c[i]) * scale;
      o[3] = 0;
   }

   class MetalRaster final : public RasterRenderer
   {
   public:
      MetalRaster(id<MTLDevice> dev, id<MTLCommandQueue> q) : mDev(dev), mQueue(q)
      {
         NSError* err = nil;
         MTLCompileOptions* co = [MTLCompileOptions new];
         co.languageVersion = MTLLanguageVersion2_3;
         mLib = [dev newLibraryWithSource:kRasterShader options:co error:&err];
         if (!mLib)
            NSLog(@"raster shader: %@", err);
      }
      bool Ok() const { return mLib != nil; }
   private:
      struct Gpu { id<MTLBuffer> vb = nil, ib = nil; NSUInteger count = 0; float lo[3], hi[3]; };
   public:
      bool UploadMeshes(const std::vector<engine::MeshData>& meshes) override
      {
         mMeshes.clear();
         for (const engine::MeshData& m : meshes)
         {
            Gpu g;
            if (!MakeGpu(m, g))
               return false;
            mMeshes.push_back(g);
         }
         return BuildBlas();
      }
      bool BuildBlas()
      {
         mBlas = [NSMutableArray array];
         if (mMeshes.empty())
            return true;
         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         id<MTLAccelerationStructureCommandEncoder> ae = [cb accelerationStructureCommandEncoder];
         NSMutableArray* keep = [NSMutableArray array];
         mBlasDirty.assign(mMeshes.size(), 0);
         for (const Gpu& g : mMeshes)
         {
            MTLPrimitiveAccelerationStructureDescriptor* ad = BlasDesc(g);
            const MTLAccelerationStructureSizes sz = [mDev accelerationStructureSizesWithDescriptor:ad];
            id<MTLAccelerationStructure> as = [mDev newAccelerationStructureWithSize:sz.accelerationStructureSize];
            id<MTLBuffer> scratch = [mDev newBufferWithLength:sz.buildScratchBufferSize options:MTLResourceStorageModePrivate];
            if (!as || !scratch)
               return false;
            [ae buildAccelerationStructure:as descriptor:ad scratchBuffer:scratch scratchBufferOffset:0];
            [mBlas addObject:as];
            [keep addObject:scratch];
         }
         [ae endEncoding];
         [cb commit];
         [cb waitUntilCompleted];
         return cb.status == MTLCommandBufferStatusCompleted;
      }
      // Instances change every frame; the instance structure is rebuilt in the frame's command buffer.
      bool BuildTlas(id<MTLCommandBuffer> cb, const engine::Frame& fr)
      {
         const NSUInteger n = fr.instances.size();
         if (n == 0 || mBlas.count == 0)
            return false;
         bool anyDirty = false;
         for (char c : mBlasDirty) anyDirty |= c != 0;
         if (anyDirty)
         {
            // Each refit gets its own slice of one scratch buffer, so refits in one encoder never share scratch.
            std::vector<std::pair<size_t, NSUInteger>> jobs;
            NSUInteger total = 0;
            for (size_t i = 0; i < mBlasDirty.size() && i < mBlas.count; i++)
               if (mBlasDirty[i])
               {
                  const MTLAccelerationStructureSizes sz = [mDev accelerationStructureSizesWithDescriptor:BlasDesc(mMeshes[i])];
                  jobs.emplace_back(i, total);
                  total += (std::max<NSUInteger>(sz.refitScratchBufferSize, 16) + 255) & ~(NSUInteger)255;
               }
            if (!mRefitScratch || mRefitScratch.length < total)
               mRefitScratch = [mDev newBufferWithLength:total options:MTLResourceStorageModePrivate];
            id<MTLAccelerationStructureCommandEncoder> re = [cb accelerationStructureCommandEncoder];
            for (const auto& j : jobs)
            {
               [re refitAccelerationStructure:mBlas[j.first] descriptor:BlasDesc(mMeshes[j.first]) destination:nil
                                scratchBuffer:mRefitScratch scratchBufferOffset:j.second];
               mBlasDirty[j.first] = 0;
            }
            [re endEncoding];
         }
         std::vector<MTLAccelerationStructureInstanceDescriptor> d(n);
         for (NSUInteger i = 0; i < n; i++)
         {
            const engine::Instance& in = fr.instances[i];
            MTLAccelerationStructureInstanceDescriptor& o = d[i];
            for (int c = 0; c < 4; c++)
               o.transformationMatrix.columns[c] = MTLPackedFloat3Make(in.model[c * 4 + 0], in.model[c * 4 + 1], in.model[c * 4 + 2]);
            o.options = MTLAccelerationStructureInstanceOptionOpaque;
            o.mask = 0xFF;
            o.intersectionFunctionTableOffset = 0;
            o.accelerationStructureIndex = (uint32_t)std::max(0, std::min(in.mesh, (int)mBlas.count - 1));
         }
         id<MTLBuffer> db = [mDev newBufferWithBytes:d.data() length:n * sizeof(d[0]) options:MTLResourceStorageModeShared];
         MTLInstanceAccelerationStructureDescriptor* td = [MTLInstanceAccelerationStructureDescriptor descriptor];
         td.instanceCount = n;
         td.instanceDescriptorBuffer = db;
         td.instancedAccelerationStructures = mBlas;
         const MTLAccelerationStructureSizes sz = [mDev accelerationStructureSizesWithDescriptor:td];
         if (!mTlas || mTlas.size < sz.accelerationStructureSize)
            mTlas = [mDev newAccelerationStructureWithSize:sz.accelerationStructureSize];
         if (!mTlasScratch || mTlasScratch.length < sz.buildScratchBufferSize)
            mTlasScratch = [mDev newBufferWithLength:sz.buildScratchBufferSize options:MTLResourceStorageModePrivate];
         if (!mTlas || !mTlasScratch)
            return false;
         id<MTLAccelerationStructureCommandEncoder> ae = [cb accelerationStructureCommandEncoder];
         [ae buildAccelerationStructure:mTlas descriptor:td scratchBuffer:mTlasScratch scratchBufferOffset:0];
         [ae endEncoding];
         return true;
      }
      // The HDRI as a mipmapped half-float texture, re-uploaded only when its revision changes; a 1x1 stand-in
      // otherwise so the shaders always have a texture bound at 9.
      bool EnsureEnv(const engine::Frame& fr)
      {
         const bool want = fr.envPixels != nullptr && fr.envW > 0 && fr.envH > 0;
         if (want && (!mEnvTex || mEnvRev != fr.envRev || (int)mEnvTex.width != fr.envW || (int)mEnvTex.height != fr.envH))
         {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                         width:fr.envW height:fr.envH mipmapped:YES];
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            mEnvTex = [mDev newTextureWithDescriptor:d];
            if (!mEnvTex)
               return false;
            std::vector<__fp16> h((size_t)fr.envW * fr.envH * 4);
            for (size_t i = 0, n = (size_t)fr.envW * fr.envH; i < n; i++)
            {
               for (int k = 0; k < 3; k++)
               {
                  const float v = fr.envPixels[i * 3 + k];
                  h[i * 4 + k] = (__fp16)(std::isfinite(v) ? std::min(std::max(v, 0.0f), 65504.0f) : 0.0f);
               }
               h[i * 4 + 3] = (__fp16)1.0f;
            }
            [mEnvTex replaceRegion:MTLRegionMake2D(0, 0, fr.envW, fr.envH) mipmapLevel:0 withBytes:h.data() bytesPerRow:(NSUInteger)fr.envW * 8];
            id<MTLCommandBuffer> cb = [mQueue commandBuffer];
            id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
            [be generateMipmapsForTexture:mEnvTex];
            [be endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            mEnvRev = fr.envRev;
         }
         if (!want && !mEnvDummy)
         {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:1 height:1 mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            mEnvDummy = [mDev newTextureWithDescriptor:d];
            const __fp16 z[4] = { 0, 0, 0, 1 };
            [mEnvDummy replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:z bytesPerRow:8];
         }
         mEnvBound = want ? mEnvTex : mEnvDummy;
         return want && mEnvTex != nil;
      }
      // Per-instance hit data for the ray passes. A buffer, not inline bytes: inline is capped at 4 KB (~36 objects).
      template <typename T> id<MTLBuffer> InfoBuffer(const std::vector<T>& v)
      {
         const NSUInteger len = std::max<NSUInteger>(v.size() * sizeof(T), 16);
         if (!mInfoBuf || mInfoBuf.length < len)
            mInfoBuf = [mDev newBufferWithLength:len * 2 options:MTLResourceStorageModeShared];
         if (mInfoBuf && !v.empty())
            std::memcpy(mInfoBuf.contents, v.data(), v.size() * sizeof(T));
         return mInfoBuf;
      }
   public:
      bool UpdateMesh(int index, const engine::MeshData& m) override
      {
         if (index < 0 || index >= (int)mMeshes.size() || index >= (int)mBlas.count)
            return false;
         Gpu& cur = mMeshes[(size_t)index];
         if (cur.count == m.indices.size() && cur.vb.length == std::max<size_t>(m.posNormal.size() * 4, 4) &&
             std::memcmp(cur.ib.contents, m.indices.data(), m.indices.size() * 4) == 0)
         {
            // Same topology: write the vertices in place (the last frame has completed) and refit next frame.
            std::memcpy(cur.vb.contents, m.posNormal.data(), m.posNormal.size() * 4);
            for (int k = 0; k < 3; k++) { cur.lo[k] = 1e30f; cur.hi[k] = -1e30f; }
            for (size_t i = 0; i + 5 < m.posNormal.size(); i += 6)
               for (int k = 0; k < 3; k++)
               {
                  cur.lo[k] = std::min(cur.lo[k], m.posNormal[i + k]);
                  cur.hi[k] = std::max(cur.hi[k], m.posNormal[i + k]);
               }
            if (mBlasDirty.size() != mMeshes.size())
               mBlasDirty.assign(mMeshes.size(), 0);
            mBlasDirty[(size_t)index] = 1;
            return true;
         }
         Gpu g;
         if (!MakeGpu(m, g))
            return false;
         id<MTLAccelerationStructure> as = BuildOneBlas(g);
         if (!as)
            return false;
         mMeshes[index] = g;
         [mBlas replaceObjectAtIndex:(NSUInteger)index withObject:as];
         if ((size_t)index < mBlasDirty.size())
            mBlasDirty[(size_t)index] = 0;
         return true;
      }
   private:
      bool MakeGpu(const engine::MeshData& m, Gpu& g)
      {
         g.vb = [mDev newBufferWithBytes:m.posNormal.data() length:std::max<size_t>(m.posNormal.size() * 4, 4) options:MTLResourceStorageModeShared];
         g.ib = [mDev newBufferWithBytes:m.indices.data() length:std::max<size_t>(m.indices.size() * 4, 4) options:MTLResourceStorageModeShared];
         g.count = (NSUInteger)m.indices.size();
         for (int k = 0; k < 3; k++) { g.lo[k] = 1e30f; g.hi[k] = -1e30f; }
         for (size_t i = 0; i + 5 < m.posNormal.size(); i += 6)
            for (int k = 0; k < 3; k++)
            {
               g.lo[k] = std::min(g.lo[k], m.posNormal[i + k]);
               g.hi[k] = std::max(g.hi[k], m.posNormal[i + k]);
            }
         return g.vb && g.ib;
      }
      // Refit-capable, so a mesh whose vertices move but whose topology holds (an animated ocean) is refitted in
      // the frame's own command buffer instead of rebuilt and waited on.
      static MTLPrimitiveAccelerationStructureDescriptor* BlasDesc(const Gpu& g)
      {
         MTLAccelerationStructureTriangleGeometryDescriptor* gd = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
         gd.vertexBuffer = g.vb; gd.vertexStride = 24;
         gd.indexBuffer = g.ib; gd.indexType = MTLIndexTypeUInt32;
         gd.triangleCount = g.count / 3;
         gd.opaque = YES;
         MTLPrimitiveAccelerationStructureDescriptor* ad = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
         ad.geometryDescriptors = @[ gd ];
         ad.usage = MTLAccelerationStructureUsageRefit;
         return ad;
      }
      id<MTLAccelerationStructure> BuildOneBlas(const Gpu& g)
      {
         MTLPrimitiveAccelerationStructureDescriptor* ad = BlasDesc(g);
         const MTLAccelerationStructureSizes sz = [mDev accelerationStructureSizesWithDescriptor:ad];
         id<MTLAccelerationStructure> as = [mDev newAccelerationStructureWithSize:sz.accelerationStructureSize];
         id<MTLBuffer> scratch = [mDev newBufferWithLength:std::max<NSUInteger>(sz.buildScratchBufferSize, 16) options:MTLResourceStorageModePrivate];
         if (!as || !scratch)
            return nil;
         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         id<MTLAccelerationStructureCommandEncoder> ae = [cb accelerationStructureCommandEncoder];
         [ae buildAccelerationStructure:as descriptor:ad scratchBuffer:scratch scratchBufferOffset:0];
         [ae endEncoding];
         [cb commit];
         [cb waitUntilCompleted];
         return as;
      }
      id<MTLTexture> mEnvTex = nil, mEnvDummy = nil, mEnvBound = nil;
      unsigned long long mEnvRev = ~0ull;
      id<MTLBuffer> mInfoBuf = nil;
      id<MTLRenderPipelineState> mBgPsoMrt = nil, mBgPsoDir = nil;
      id<MTLDepthStencilState> mDsBg = nil;
      void FillFrame(FrameU& f, const engine::Frame& fr)
      {
         std::memcpy(f.viewProj, fr.viewProj, 64);
         std::memcpy(f.eye, fr.eye, 12);
         f.eye[3] = fr.rim;
         Lin3(fr.ambientSrgb, f.ambient);
         Lin3(fr.envSkySrgb, f.sky);
         Lin3(fr.envHorizonSrgb, f.horizon);
         Lin3(fr.envGroundSrgb, f.ground);
         const int n = (int)std::min<size_t>(fr.lights.size(), engine::kMaxLights);
         f.params[0] = fr.exposure; f.params[1] = (float)fr.tonemap; f.params[2] = fr.envIntensity; f.params[3] = (float)n;
         const bool envMap = EnsureEnv(fr);
         f.envm[0] = (envMap ? 1.0f : 0.0f) + (envMap && fr.envBackground ? 2.0f : 0.0f);
         f.envm[1] = fr.envRotationRad; f.envm[2] = fr.envMaxLod; f.envm[3] = fr.envBaseLod;
         int visCh = 0;
         for (int i = 0; i < n; i++)
         {
            const engine::Light& l = fr.lights[i];
            LightU& o = f.lights[i];
            std::memcpy(o.vec, l.vec, 12); o.vec[3] = (float)(int)l.type;
            std::memcpy(o.aim, l.aim, 12);
            // aim.w: this light's channel in the half-res shadow mask (directional, point, sun, spot; first four), else -1
            o.aim[3] = -1.0f;
            if (l.type == engine::LightType::Directional || l.type == engine::LightType::Point || l.type == engine::LightType::Sun || l.type == engine::LightType::Spot)
               if (visCh < 4) o.aim[3] = (float)visCh++;
            Lin3(l.colorSrgb, o.color, l.intensity);
            o.terms[0] = l.diffuse; o.terms[1] = l.specular; o.terms[2] = l.falloff; o.terms[3] = l.cutoff;
            o.spot[0] = l.spotAngleDeg; o.spot[1] = l.spotPenumbra; o.spot[2] = l.areaW; o.spot[3] = l.areaH;
         }

      }
      bool Render(SharedImage& target, const engine::Frame& fr, double* gpuMs) override
      {
         MetalImage& img = static_cast<MetalImage&>(target);
         const MTLPixelFormat fmt = img.Half() ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
         if (!Pipeline(fmt) || !Depth(img.Width(), img.Height()) || !Targets(img.Width(), img.Height()))
            return false;

         FrameU f = {};
         FillFrame(f, fr);
         BuildRuns(fr);

         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         // Pure raster needs no ray structure; skipping it keeps a large instanced scene's raster cost raster-only.
         const bool haveTlas = (fr.rtShadows || fr.rtReflections || fr.gi) && BuildTlas(cb, fr);
         CullBatches(cb, fr, img.Width());
         const bool rt = fr.rtShadows && haveTlas;
         const bool rtRefl = fr.rtReflections && haveTlas;
         if (rt || !ShadowPass(cb, fr, f))
            EnsureShadowTex(4);
         const bool giOn = fr.gi && haveTlas;
         f.gi[0] = giOn ? 1.0f : 0.0f; f.gi[1] = fr.giStrength; f.gi[2] = (float)fr.frameIndex; f.gi[3] = (float)fr.giRays;
         f.shQ[2] = (rt ? 1.0f : 0.0f) + (rtRefl ? 2.0f : 0.0f); f.shQ[3] = fr.rayBias;
         if (rt) { f.shP[1] = 0; f.shP[0] = 0; f.shP[3] = fr.shadow.strength; }
         // RT shadows: a depth-only prepass (the shaded pass then runs once per pixel), then the half-res shadow mask
         // traced from that depth. INFINITE_ENGINE_HALFSHADOW=0 keeps the prepass and traces every pixel instead.
         static const int sHalfSh = [] { const char* e = getenv("INFINITE_ENGINE_HALFSHADOW"); return e ? atoi(e) : 1; }(); // 0 prepass only, -1 neither
         const bool prepass = rt && sHalfSh >= 0 && mDsEq && EnsureInv();
         float invVP[16];
         const bool mask = prepass && sHalfSh > 0 && mVisPso && Inv4(fr.viewProj, invVP);
         if (prepass)
         {
            MTLRenderPassDescriptor* zp = [MTLRenderPassDescriptor renderPassDescriptor];
            zp.depthAttachment.texture = mDepth;
            zp.depthAttachment.loadAction = MTLLoadActionClear;
            zp.depthAttachment.storeAction = MTLStoreActionStore;
            zp.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> ze = [cb renderCommandEncoderWithDescriptor:zp];
            [ze setRenderPipelineState:mPrePso];
            [ze setDepthStencilState:mDs];
            [ze setCullMode:MTLCullModeBack];
            [ze setFrontFacingWinding:MTLWindingClockwise];
            [ze setVertexBytes:&f length:sizeof(f) atIndex:1];
            [ze setVertexBuffer:mInstBuf offset:0 atIndex:10];
            for (const auto& r : mRuns)
            {
               const Gpu& g = mMeshes[fr.instances[r.first].mesh];
               [ze setVertexBuffer:g.vb offset:0 atIndex:0];
               [ze setVertexBytes:&r.first length:4 atIndex:11];
               [ze drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib
                           indexBufferOffset:0 instanceCount:r.second];
            }
            [ze endEncoding];
         }
         if (mask)
         {
            f.shQ[2] += 4.0f;
            if (!mArgEnc)
               mArgEnc = [mFsFn newArgumentEncoderWithBufferIndex:3];
            if (mArgTlas != mTlas)
            {
               mArgBuf = [mDev newBufferWithLength:mArgEnc.encodedLength options:MTLResourceStorageModeShared];
               [mArgEnc setArgumentBuffer:mArgBuf offset:0];
               [mArgEnc setAccelerationStructure:mTlas atIndex:0];
               mArgTlas = mTlas;
            }
            MTLRenderPassDescriptor* vp = [MTLRenderPassDescriptor renderPassDescriptor];
            vp.colorAttachments[0].texture = mVis;
            vp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            vp.colorAttachments[0].storeAction = MTLStoreActionStore;
            vp.colorAttachments[1].texture = mVisD;
            vp.colorAttachments[1].loadAction = MTLLoadActionDontCare;
            vp.colorAttachments[1].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> ve = [cb renderCommandEncoderWithDescriptor:vp];
            struct { float invVP[16]; float info[4]; } vu = {};
            std::memcpy(vu.invVP, invVP, 64);
            vu.info[0] = (float)img.Width(); vu.info[1] = (float)img.Height();
            [ve setRenderPipelineState:mVisPso];
            [ve setFragmentBytes:&vu length:sizeof(vu) atIndex:0];
            [ve setFragmentBytes:&f length:sizeof(f) atIndex:1];
            [ve setFragmentBuffer:mArgBuf offset:0 atIndex:3];
            [ve setFragmentTexture:mDepth atIndex:0];
            [ve useResource:mTlas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            for (id<MTLAccelerationStructure> b in mBlas) [ve useResource:b usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            [ve drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [ve endEncoding];
         }
         MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
         const bool mrt = giOn || rtRefl;
         pass.colorAttachments[0].texture = mrt ? mHdr : img.Tex();
         pass.colorAttachments[0].loadAction = MTLLoadActionClear;
         pass.colorAttachments[0].storeAction = MTLStoreActionStore;
         pass.colorAttachments[0].clearColor = mrt ? MTLClearColorMake(0, 0, 0, 0) : MTLClearColorMake(fr.clearSrgb[0], fr.clearSrgb[1], fr.clearSrgb[2], 1.0);
         if (mrt) {
         pass.colorAttachments[1].texture = mGiT;
         pass.colorAttachments[1].loadAction = MTLLoadActionClear;
         pass.colorAttachments[1].storeAction = MTLStoreActionStore;
         pass.colorAttachments[1].clearColor = MTLClearColorMake(0, 0, 0, 0);
         pass.colorAttachments[2].texture = mRefl;
         pass.colorAttachments[2].loadAction = MTLLoadActionClear;
         pass.colorAttachments[2].storeAction = MTLStoreActionStore;
         pass.colorAttachments[2].clearColor = MTLClearColorMake(0, 0, 0, 0);
         if (mrt)
         {
            pass.colorAttachments[3].texture = mNz;
            pass.colorAttachments[3].loadAction = MTLLoadActionClear;
            pass.colorAttachments[3].storeAction = MTLStoreActionStore;
            pass.colorAttachments[3].clearColor = MTLClearColorMake(0, 0, 0, 0);
         }
         }
         pass.depthAttachment.texture = mDepth;
         pass.depthAttachment.loadAction = prepass ? MTLLoadActionLoad : MTLLoadActionClear;
         pass.depthAttachment.storeAction = MTLStoreActionDontCare;
         pass.depthAttachment.clearDepth = 1.0;
         id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
         [enc setFragmentTexture:mShadowTex atIndex:0];
         if (haveTlas)
         {
            if (mArgTlas != mTlas)
            {
               if (!mArgEnc)
                  mArgEnc = [mFsFn newArgumentEncoderWithBufferIndex:3];
               mArgBuf = [mDev newBufferWithLength:mArgEnc.encodedLength options:MTLResourceStorageModeShared];
               [mArgEnc setArgumentBuffer:mArgBuf offset:0];
               [mArgEnc setAccelerationStructure:mTlas atIndex:0];
               mArgTlas = mTlas;
            }
            [enc setFragmentBuffer:mArgBuf offset:0 atIndex:3];
            [enc useResource:mTlas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            for (const Gpu& g : mMeshes)
            {
               [enc useResource:g.vb usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
               [enc useResource:g.ib usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            }
            for (id<MTLAccelerationStructure> b in mBlas)
               [enc useResource:b usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
         }
         [enc setRenderPipelineState:mrt ? mPso : mPsoDir];
         [enc setDepthStencilState:mDs];
         [enc setCullMode:MTLCullModeBack];
         [enc setFrontFacingWinding:MTLWindingClockwise]; // GL counter-clockwise, mirrored by the y flip
         [enc setVertexBytes:&f length:sizeof(f) atIndex:1];
         [enc setFragmentBytes:&f length:sizeof(f) atIndex:1];
         struct RInstH { float bm[4], rg[4]; uint64_t vb, ib; float normalM[16]; };
         std::vector<RInstH> info(fr.instances.size() + 1);
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            RInstH& o = info[i]; o = {};
            Lin3(in.baseSrgb, o.bm); o.bm[3] = in.metallic; o.rg[0] = in.roughness;
            std::memcpy(o.normalM, in.normalMatrix, 64);
            if (in.mesh >= 0 && in.mesh < (int)mMeshes.size())
            {
               o.vb = mMeshes[in.mesh].vb.gpuAddress; o.ib = mMeshes[in.mesh].ib.gpuAddress;
            }
         }
         id<MTLBuffer> infoBuf = InfoBuffer(info);
         [enc setFragmentBuffer:infoBuf offset:0 atIndex:4];
         [enc setFragmentTexture:mEnvBound atIndex:9];
         [enc setFragmentTexture:mVis atIndex:7];
         [enc setFragmentTexture:mVisD atIndex:8];
         if (fr.envBackground && (int(f.envm[0]) & 2) != 0 && mBgPsoMrt && mBgPsoDir)
         {
            struct { float invVP[16]; float p[4]; } bu = {};
            if (Inv4(fr.viewProj, bu.invVP))
            {
               bu.p[0] = (float)img.Width(); bu.p[1] = (float)img.Height(); bu.p[2] = mrt ? 1.0f : 0.0f;
               [enc setRenderPipelineState:mrt ? mBgPsoMrt : mBgPsoDir];
               [enc setDepthStencilState:mDsBg];
               [enc setCullMode:MTLCullModeNone];   // the full-screen triangle winds as a back face here
               [enc setFragmentBytes:&bu length:sizeof(bu) atIndex:0];
               [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
               [enc setCullMode:MTLCullModeBack];
               [enc setRenderPipelineState:mrt ? mPso : mPsoDir];
               [enc setDepthStencilState:mDs];
            }
         }
         [enc setRenderPipelineState:prepass ? (mrt ? mPsoRunInv : mPsoRunDirInv) : (mrt ? mPsoRun : mPsoRunDir)];
         [enc setVertexBuffer:mInstBuf offset:0 atIndex:10];
         if (prepass) [enc setDepthStencilState:mDsEq];
         const InstU* runU = (const InstU*)mInstBuf.contents;
         for (const auto& r : mRuns)
         {
            const Gpu& g = mMeshes[fr.instances[r.first].mesh];
            [enc setFragmentBytes:&runU[r.first] length:sizeof(InstU) atIndex:2];   // the run's material
            [enc setVertexBuffer:g.vb offset:0 atIndex:0];
            [enc setVertexBytes:&r.first length:4 atIndex:11];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib
                         indexBufferOffset:0 instanceCount:r.second];
         }
         [enc setRenderPipelineState:mrt ? mPso : mPsoDir];
         [enc setDepthStencilState:mDs];
         for (size_t bi = 0; bi < fr.batches.size() && bi < mBatchGpu.size(); bi++)
         {
            const engine::InstanceBatch& b = fr.batches[bi];
            BatchGpu& bg = mBatchGpu[bi];
            if (b.mesh < 0 || b.mesh >= (int)mMeshes.size() || b.Count() == 0 || !bg.models)
               continue;
            InstU u = {};
            Lin3(b.baseSrgb, u.base);
            u.mr[0] = b.metallic; u.mr[1] = b.roughness;
            [enc setFragmentBytes:&u length:sizeof(u) atIndex:2];
            [enc setVertexBuffer:bg.models offset:0 atIndex:5];
            const Gpu& g = mMeshes[b.mesh];
            [enc setRenderPipelineState:mrt ? mPsoBatchMrt : mPsoBatch];
            [enc setVertexBuffer:g.vb offset:0 atIndex:0];
            [enc setVertexBuffer:bg.visMesh offset:0 atIndex:6];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt32 indexBuffer:g.ib indexBufferOffset:0 indirectBuffer:bg.argsMesh indirectBufferOffset:0];
            [enc setRenderPipelineState:mrt ? mPsoPtMrt : mPsoPt];
            [enc setVertexBuffer:bg.visPt offset:0 atIndex:6];
            {
               const float rd = std::sqrt(std::max(g.lo[0]*g.lo[0], g.hi[0]*g.hi[0]) + std::max(g.lo[1]*g.lo[1], g.hi[1]*g.hi[1]) + std::max(g.lo[2]*g.lo[2], g.hi[2]*g.hi[2]));
               const float k = rd * std::sqrt(fr.viewProj[0]*fr.viewProj[0] + fr.viewProj[4]*fr.viewProj[4] + fr.viewProj[8]*fr.viewProj[8]) * (float)img.Width();
               [enc setVertexBytes:&k length:4 atIndex:7];
            }
            [enc drawPrimitives:MTLPrimitiveTypePoint indirectBuffer:bg.argsPt indirectBufferOffset:0];
         }
         [enc endEncoding];
         if (!mrt) { mHistValid = false; mRValid = false; }
         else
         {
            const bool moved = std::memcmp(mLastVP, fr.viewProj, 64) != 0;
            std::memcpy(mLastVP, fr.viewProj, 64);
            const int prev = mHistCur, cur = 1 - mHistCur;
            const float offX = (float)(fr.frameIndex & 1u), offY = (float)((fr.frameIndex >> 1) & 1u);
            float inv[16];
            if (mTrPso && Inv4(fr.viewProj, inv))
            {
               // half-res trace: GI + reflection rays, one full-res pixel of every 2x2 per frame
               MTLRenderPassDescriptor* tp = [MTLRenderPassDescriptor renderPassDescriptor];
               for (int k = 0; k < 2; k++)
               {
                  tp.colorAttachments[k].texture = mTr[k];
                  tp.colorAttachments[k].loadAction = MTLLoadActionDontCare;
                  tp.colorAttachments[k].storeAction = MTLStoreActionStore;
               }
               id<MTLRenderCommandEncoder> te = [cb renderCommandEncoderWithDescriptor:tp];
               struct { float invVP[16]; float info[4]; float hist[4]; } tu = {};
               std::memcpy(tu.invVP, inv, 64);
               tu.info[0] = offX; tu.info[1] = offY; tu.info[2] = (float)img.Width(); tu.info[3] = (float)img.Height();
               tu.hist[0] = (mHistValid && giOn) ? 1.0f : 0.0f;
               [te setRenderPipelineState:mTrPso];
               [te setFragmentBytes:&tu length:sizeof(tu) atIndex:0];
               [te setFragmentBytes:&f length:sizeof(f) atIndex:1];
               [te setFragmentBuffer:mArgBuf offset:0 atIndex:3];
               [te setFragmentBuffer:infoBuf offset:0 atIndex:4];
               [te setFragmentTexture:mEnvBound atIndex:9];
               [te setFragmentTexture:mNz atIndex:0];
               [te setFragmentTexture:mGiT atIndex:1];
               [te setFragmentTexture:mRefl atIndex:2];
               [te setFragmentTexture:mHist[prev] atIndex:3];
               [te useResource:mTlas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
               for (const Gpu& g : mMeshes) { [te useResource:g.vb usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; [te useResource:g.ib usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; }
               for (id<MTLAccelerationStructure> b in mBlas) [te useResource:b usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
               [te drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
               [te endEncoding];
            }
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = img.Tex();
            rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            rp.colorAttachments[1].texture = mHist[cur];
            rp.colorAttachments[1].loadAction = MTLLoadActionDontCare;
            rp.colorAttachments[1].storeAction = MTLStoreActionStore;
            rp.colorAttachments[2].texture = mRHist[cur];
            rp.colorAttachments[2].loadAction = MTLLoadActionDontCare;
            rp.colorAttachments[2].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
            float ru[12] = { moved ? 0.6f : fr.giBlend, (float)fr.tonemap, mHistValid ? 1.0f : 0.0f, moved ? 0.7f : 0.12f, 0, 0, 0, 0, offX, offY, giOn ? 1.0f : 0.0f,
                             (int(f.envm[0]) & 2) != 0 ? 1.0f : 0.0f };
            Lin3(fr.clearSrgb, ru + 4); // clear colour is stored sRGB-encoded in the target, so undo Lin3
            for (int i = 0; i < 3; i++) ru[4 + i] = fr.clearSrgb[i];
            ru[7] = mRValid ? 1.0f : 0.0f;
            [re setRenderPipelineState:mResPso];
            [re setFragmentBytes:ru length:sizeof(ru) atIndex:0];
            [re setFragmentTexture:mHdr atIndex:0];
            [re setFragmentTexture:mGiT atIndex:1];
            [re setFragmentTexture:mHist[prev] atIndex:2];
            [re setFragmentTexture:mRefl atIndex:3];
            [re setFragmentTexture:mRHist[prev] atIndex:4];
            [re setFragmentTexture:mTr[0] atIndex:5];
            [re setFragmentTexture:mTr[1] atIndex:6];
            [re setFragmentTexture:mNz atIndex:7];
            [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [re endEncoding];
            if (giOn && fr.giDenoise > 0 && mAtPso && mCompPso)
            {
               // a-trous: history -> At0 -> At1 -> ...; the first filtered pass becomes next frame's history
               id<MTLTexture> src = mHist[cur];
               for (int it = 0; it < fr.giDenoise; it++)
               {
                  id<MTLTexture> dst = mAt[it & 1];
                  MTLRenderPassDescriptor* ap = [MTLRenderPassDescriptor renderPassDescriptor];
                  ap.colorAttachments[0].texture = dst;
                  ap.colorAttachments[0].loadAction = MTLLoadActionDontCare;
                  ap.colorAttachments[0].storeAction = MTLStoreActionStore;
                  id<MTLRenderCommandEncoder> ae = [cb renderCommandEncoderWithDescriptor:ap];
                  const float au[4] = { (float)(1 << it), 0, 0, 0 };
                  [ae setRenderPipelineState:mAtPso];
                  [ae setFragmentBytes:au length:sizeof(au) atIndex:0];
                  [ae setFragmentTexture:src atIndex:0];
                  [ae setFragmentTexture:mNz atIndex:1];
                  [ae drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                  [ae endEncoding];
                  if (it == 0)
                  {
                     id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
                     [be copyFromTexture:dst toTexture:mHist[cur]];
                     [be endEncoding];
                  }
                  src = dst;
               }
               MTLRenderPassDescriptor* cp = [MTLRenderPassDescriptor renderPassDescriptor];
               cp.colorAttachments[0].texture = img.Tex();
               cp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
               cp.colorAttachments[0].storeAction = MTLStoreActionStore;
               id<MTLRenderCommandEncoder> ce = [cb renderCommandEncoderWithDescriptor:cp];
               float cu[8] = { 0, (float)fr.tonemap, (int(f.envm[0]) & 2) != 0 ? 1.0f : 0.0f, 0, fr.clearSrgb[0], fr.clearSrgb[1], fr.clearSrgb[2], 0 };
               [ce setRenderPipelineState:mCompPso];
               [ce setFragmentBytes:cu length:sizeof(cu) atIndex:0];
               [ce setFragmentTexture:mHdr atIndex:0];
               [ce setFragmentTexture:src atIndex:1];
               [ce setFragmentTexture:mRHist[cur] atIndex:2];
               [ce setFragmentTexture:mGiT atIndex:3];
               [ce setFragmentTexture:mRefl atIndex:4];
               [ce drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
               [ce endEncoding];
            }
            mHistCur = cur; mHistValid = giOn; mRValid = true;
         }
         [cb commit];
         [cb waitUntilCompleted];
         if (gpuMs)
            *gpuMs = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
         return cb.status == MTLCommandBufferStatusCompleted;
      }

      static bool Inv4(const float* m, float* o)
      {
         double a[16], inv[16];
         for (int i = 0; i < 16; i++) a[i] = m[i];
         inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
         inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
         inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
         inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
         inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
         inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
         inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
         inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
         inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
         inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
         inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
         inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
         inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
         inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
         inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
         inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
         const double det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
         if (std::fabs(det) < 1e-30) return false;
         for (int i = 0; i < 16; i++) o[i] = (float)(inv[i] / det);
         return true;
      }
      bool PathTrace(SharedImage& target, const engine::Frame& fr, int spp, int bounces, bool reset, double* gpuMs) override
      {
         MetalImage& img = static_cast<MetalImage&>(target);
         const MTLPixelFormat fmt = img.Half() ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
         if (!Pipeline(fmt) || !PtPipeline(fmt) || !PtTargets(img.Width(), img.Height()))
            return false;
         FrameU f = {};
         FillFrame(f, fr);
         f.shQ[3] = fr.rayBias;
         float inv[16];
         if (!Inv4(fr.viewProj, inv))
            return false;
         if (reset) mPtN = 0;
         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         if (!BuildTlas(cb, fr))
            return false;
         if (mArgTlas != mTlas)
         {
            if (!mArgEnc) mArgEnc = [mFsFn newArgumentEncoderWithBufferIndex:3];
            mArgBuf = [mDev newBufferWithLength:mArgEnc.encodedLength options:MTLResourceStorageModeShared];
            [mArgEnc setArgumentBuffer:mArgBuf offset:0];
            [mArgEnc setAccelerationStructure:mTlas atIndex:0];
            mArgTlas = mTlas;
         }
         struct RInstH { float bm[4], rg[4]; uint64_t vb, ib; float normalM[16]; };
         std::vector<RInstH> info(fr.instances.size() + 1);
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            RInstH& o = info[i]; o = {};
            Lin3(in.baseSrgb, o.bm); o.bm[3] = in.metallic; o.rg[0] = in.roughness;
            std::memcpy(o.normalM, in.normalMatrix, 64);
            if (in.mesh >= 0 && in.mesh < (int)mMeshes.size()) { o.vb = mMeshes[in.mesh].vb.gpuAddress; o.ib = mMeshes[in.mesh].ib.gpuAddress; }
         }
         id<MTLBuffer> infoBuf = InfoBuffer(info);
         for (int s = 0; s < std::max(1, spp); s++)
         {
            const int cur = 1 - mPtCur;
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = mPtAcc[cur];
            rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
            [e setRenderPipelineState:mPtPso];
            struct { float invVP[16]; float info[4]; } pu;
            std::memcpy(pu.invVP, inv, 64);
            pu.info[0] = (float)mPtN; pu.info[1] = (float)std::max(1, bounces); pu.info[2] = (float)(mPtFrame++); pu.info[3] = (float)img.Width();
            [e setFragmentBytes:&pu length:sizeof(pu) atIndex:0];
            [e setFragmentBytes:&f length:sizeof(f) atIndex:1];
            [e setFragmentBuffer:mArgBuf offset:0 atIndex:3];
            [e setFragmentBuffer:infoBuf offset:0 atIndex:4];
            [e setFragmentTexture:mEnvBound atIndex:9];
            [e setFragmentTexture:mPtAcc[mPtCur] atIndex:0];
            [e useResource:mTlas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            for (const Gpu& g : mMeshes) { [e useResource:g.vb usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; [e useResource:g.ib usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; }
            for (id<MTLAccelerationStructure> b in mBlas) [e useResource:b usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [e endEncoding];
            mPtCur = cur; mPtN++;
         }
         MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
         rp.colorAttachments[0].texture = img.Tex();
         rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
         rp.colorAttachments[0].storeAction = MTLStoreActionStore;
         id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
         float ru[8] = { (float)mPtN, (float)fr.tonemap, fr.exposure, 0, fr.clearSrgb[0], fr.clearSrgb[1], fr.clearSrgb[2], 0 };
         [e setRenderPipelineState:mPtResPso];
         [e setFragmentBytes:ru length:sizeof(ru) atIndex:0];
         [e setFragmentTexture:mPtAcc[mPtCur] atIndex:0];
         [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
         [e endEncoding];
         [cb commit];
         [cb waitUntilCompleted];
         if (gpuMs) *gpuMs = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
         return cb.status == MTLCommandBufferStatusCompleted;
      }

   private:
      void CullBatches(id<MTLCommandBuffer> cb, const engine::Frame& fr, int widthPx)
      {
         if (fr.batches.empty() || !mCullPso)
            return;
         mBatchGpu.resize(fr.batches.size());
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         for (size_t bi = 0; bi < fr.batches.size(); bi++)
         {
            const engine::InstanceBatch& b = fr.batches[bi];
            BatchGpu& bg = mBatchGpu[bi];
            if (b.mesh < 0 || b.mesh >= (int)mMeshes.size() || b.Count() == 0)
               continue;
            const NSUInteger n = b.Count();
            if (!bg.models || bg.cap < n)
            {
               bg.models = [mDev newBufferWithLength:n * 64 options:MTLResourceStorageModeShared];
               bg.visMesh = [mDev newBufferWithLength:n * 4 options:MTLResourceStorageModePrivate];
               bg.visPt = [mDev newBufferWithLength:n * 4 options:MTLResourceStorageModePrivate];
               bg.cnt = [mDev newBufferWithLength:8 options:MTLResourceStorageModeShared];
               std::memset(bg.cnt.contents, 0, 8);
               bg.argsMesh = [mDev newBufferWithLength:20 options:MTLResourceStorageModePrivate];
               bg.argsPt = [mDev newBufferWithLength:16 options:MTLResourceStorageModePrivate];
               bg.cap = n;
            }
            std::memcpy(bg.models.contents, b.models.data(), n * 64);
            const Gpu& g = mMeshes[b.mesh];
            const float rad = std::sqrt(std::max({ g.lo[0]*g.lo[0], g.hi[0]*g.hi[0] }) + std::max({ g.lo[1]*g.lo[1], g.hi[1]*g.hi[1] }) + std::max({ g.lo[2]*g.lo[2], g.hi[2]*g.hi[2] }));
            struct { float vp[16]; float info[4]; } cu;
            std::memcpy(cu.vp, fr.viewProj, 64);
            cu.info[0] = (float)n; cu.info[1] = rad; cu.info[2] = (float)widthPx; cu.info[3] = 10.0f;
            [ce setComputePipelineState:mCullPso];
            [ce setBytes:&cu length:sizeof(cu) atIndex:0];
            [ce setBuffer:bg.models offset:0 atIndex:1];
            [ce setBuffer:bg.visMesh offset:0 atIndex:2];
            [ce setBuffer:bg.visPt offset:0 atIndex:3];
            [ce setBuffer:bg.cnt offset:0 atIndex:4];
            [ce dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [ce memoryBarrierWithScope:MTLBarrierScopeBuffers];
            [ce setComputePipelineState:mArgsPso];
            [ce setBuffer:bg.cnt offset:0 atIndex:0];
            [ce setBuffer:bg.argsMesh offset:0 atIndex:1];
            [ce setBuffer:bg.argsPt offset:0 atIndex:2];
            const uint32_t ic = (uint32_t)g.count;
            [ce setBytes:&ic length:4 atIndex:3];
            [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
            [ce memoryBarrierWithScope:MTLBarrierScopeBuffers];
         }
         [ce endEncoding];
      }
      bool PtPipeline(MTLPixelFormat fmt)
      {
         if (mPtPso && mPtResPso && mPtFmt == fmt)
            return true;
         NSError* err = nil;
         MTLRenderPipelineDescriptor* a = [MTLRenderPipelineDescriptor new];
         a.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
         a.fragmentFunction = [mLib newFunctionWithName:@"pt_fs"];
         a.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
         mPtPso = [mDev newRenderPipelineStateWithDescriptor:a error:&err];
         if (!mPtPso) NSLog(@"pt pipeline: %@", err);
         MTLRenderPipelineDescriptor* b = [MTLRenderPipelineDescriptor new];
         b.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
         b.fragmentFunction = [mLib newFunctionWithName:@"ptres_fs"];
         b.colorAttachments[0].pixelFormat = fmt;
         mPtResPso = [mDev newRenderPipelineStateWithDescriptor:b error:&err];
         if (!mPtResPso) NSLog(@"pt resolve pipeline: %@", err);
         mPtFmt = fmt;
         return mPtPso && mPtResPso;
      }
      bool PtTargets(int w, int h)
      {
         if (mPtAcc[0] && (int)mPtAcc[0].width == w && (int)mPtAcc[0].height == h)
            return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
         d.storageMode = MTLStorageModePrivate;
         mPtAcc[0] = [mDev newTextureWithDescriptor:d]; mPtAcc[1] = [mDev newTextureWithDescriptor:d];
         mPtN = 0;
         return mPtAcc[0] && mPtAcc[1];
      }
      id<MTLRenderPipelineState> mPtPso = nil, mPtResPso = nil;
      id<MTLTexture> mPtAcc[2] = { nil, nil };
      int mPtCur = 0, mPtN = 0, mPtFrame = 0; MTLPixelFormat mPtFmt = MTLPixelFormatInvalid;
   private:

      static void Norm(float* v) { const float l = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); if (l > 1e-5f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
      bool EnsureShadowTex(int size)
      {
         if (mShadowTex && (int)mShadowTex.width == size)
            return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:size height:size mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
         d.storageMode = MTLStorageModePrivate;
         mShadowTex = [mDev newTextureWithDescriptor:d];
         return mShadowTex != nil;
      }
      // Fits an orthographic depth map to the scene's bounding sphere, as Render 3D does, and draws it.
      bool ShadowPass(id<MTLCommandBuffer> cb, const engine::Frame& fr, FrameU& f)
      {
         const engine::Shadow& sh = fr.shadow;
         if (!sh.enabled || fr.lights.empty() || fr.lights[0].type == engine::LightType::Point || fr.lights[0].type == engine::LightType::Area ||
             fr.lights[0].type == engine::LightType::Ambient || !EnsureShadowTex(sh.size))
            return false;
         float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
         bool any = false;
         for (const engine::Instance& in : fr.instances)
         {
            if (in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            const Gpu& g = mMeshes[in.mesh];
            for (int c = 0; c < 8; c++)
            {
               const float p[3] = { (c & 1) ? g.hi[0] : g.lo[0], (c & 2) ? g.hi[1] : g.lo[1], (c & 4) ? g.hi[2] : g.lo[2] };
               for (int k = 0; k < 3; k++)
               {
                  const float w = in.model[k] * p[0] + in.model[4 + k] * p[1] + in.model[8 + k] * p[2] + in.model[12 + k];
                  lo[k] = std::min(lo[k], w);
                  hi[k] = std::max(hi[k], w);
               }
            }
            any = true;
         }
         if (!any)
            return false;
         const engine::Light& L = fr.lights[0];
         const float centre[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
         const float radius = std::max(0.01f, 0.5f * std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0]) + (hi[1]-lo[1])*(hi[1]-lo[1]) + (hi[2]-lo[2])*(hi[2]-lo[2])));
         float dir[3] = { L.vec[0], L.vec[1], L.vec[2] };
         const float dl = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
         if (dl > 1e-5f) Norm(dir); else { dir[0] = 0; dir[1] = 1; dir[2] = 0; }
         const float eye[3] = { centre[0] + dir[0] * radius * 2.5f, centre[1] + dir[1] * radius * 2.5f, centre[2] + dir[2] * radius * 2.5f };
         // LookAt, GL conventions
         const float upv[3] = { 0, std::fabs(dir[1]) > 0.99f ? 0.0f : 1.0f, std::fabs(dir[1]) > 0.99f ? 1.0f : 0.0f };
         float fw[3] = { centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2] };
         Norm(fw);
         float sd[3] = { fw[1]*upv[2] - fw[2]*upv[1], fw[2]*upv[0] - fw[0]*upv[2], fw[0]*upv[1] - fw[1]*upv[0] };
         Norm(sd);
         const float u[3] = { sd[1]*fw[2] - sd[2]*fw[1], sd[2]*fw[0] - sd[0]*fw[2], sd[0]*fw[1] - sd[1]*fw[0] };
         float V[16] = { sd[0], u[0], -fw[0], 0, sd[1], u[1], -fw[1], 0, sd[2], u[2], -fw[2], 0,
                         -(sd[0]*eye[0] + sd[1]*eye[1] + sd[2]*eye[2]), -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]),
                         (fw[0]*eye[0] + fw[1]*eye[1] + fw[2]*eye[2]), 1 };
         float clipNear = sh.clipNear;
         if (L.type == engine::LightType::Spot)
         {
            const float d[3] = { L.vec[0] - centre[0], L.vec[1] - centre[1], L.vec[2] - centre[2] };
            clipNear += radius * 2.5f - std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
         }
         clipNear = std::min(std::max(clipNear, 0.05f), radius * 4.0f);
         const float farZ = radius * 5.0f, hh = radius * 1.2f;
         float P[16] = {};
         P[0] = 1.0f / hh; P[5] = 1.0f / hh; P[10] = -2.0f / (farZ - clipNear); P[14] = -(farZ + clipNear) / (farZ - clipNear); P[15] = 1;
         float VP[16];
         for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
            {
               float a = 0;
               for (int k = 0; k < 4; k++) a += P[k * 4 + r] * V[c * 4 + k];
               VP[c * 4 + r] = a;
            }
         const float texelWorld = 2.4f * radius / (float)sh.size;
         const float biasDepth = (sh.bias + 2.5f * texelWorld * std::max(1.0f, std::min(sh.softness, 2.0f))) / (farZ - clipNear);
         std::memcpy(f.lightVP, VP, 64);
         f.shP[0] = 1; f.shP[1] = 0; f.shP[2] = biasDepth; f.shP[3] = sh.strength;
         f.shQ[0] = 1.0f / (float)sh.size; f.shQ[1] = sh.softness;

         MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
         pass.depthAttachment.texture = mShadowTex;
         pass.depthAttachment.loadAction = MTLLoadActionClear;
         pass.depthAttachment.storeAction = MTLStoreActionStore;
         pass.depthAttachment.clearDepth = 1.0;
         id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
         [enc setRenderPipelineState:mShPso];
         [enc setDepthStencilState:mDs];
         [enc setCullMode:MTLCullModeNone];
         [enc setVertexBytes:VP length:64 atIndex:1];
         [enc setRenderPipelineState:mShPsoRun];
         [enc setVertexBuffer:mInstBuf offset:0 atIndex:10];
         for (const auto& r : mRuns)
         {
            const Gpu& g = mMeshes[fr.instances[r.first].mesh];
            [enc setVertexBuffer:g.vb offset:0 atIndex:0];
            [enc setVertexBytes:&r.first length:4 atIndex:11];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib
                         indexBufferOffset:0 instanceCount:r.second];
         }
         [enc endEncoding];
         return true;
      }
      bool Pipeline(MTLPixelFormat fmt)
      {
         if (mPso && mFmt == fmt)
            return true;
         MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
         pd.vertexFunction = [mLib newFunctionWithName:@"r_vs"];
         NSError* fe = nil;
         MTLFunctionConstantValues* cvM = [MTLFunctionConstantValues new]; bool yes = true, no = false;
         [cvM setConstantValue:&yes type:MTLDataTypeBool atIndex:0];
         MTLFunctionConstantValues* cvD = [MTLFunctionConstantValues new];
         [cvD setConstantValue:&no type:MTLDataTypeBool atIndex:0];
         mFsFn = [mLib newFunctionWithName:@"r_fs" constantValues:cvM error:&fe];
         id<MTLFunction> fsDir = [mLib newFunctionWithName:@"r_fs" constantValues:cvD error:&fe];
         if (!mFsFn || !fsDir) NSLog(@"r_fs specialisation: %@", fe);
         pd.fragmentFunction = mFsFn;
         pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
         pd.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
         pd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
         pd.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA32Float;
         pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         NSError* err = nil;
         mPso = [mDev newRenderPipelineStateWithDescriptor:pd error:&err];
         MTLRenderPipelineDescriptor* rd = [MTLRenderPipelineDescriptor new];
         rd.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
         rd.fragmentFunction = [mLib newFunctionWithName:@"res_fs"];
         rd.colorAttachments[0].pixelFormat = fmt;
         rd.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
         rd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
         mResPso = [mDev newRenderPipelineStateWithDescriptor:rd error:&err];
         {
            MTLRenderPipelineDescriptor* ad = [MTLRenderPipelineDescriptor new];
            ad.vertexFunction = rd.vertexFunction;
            ad.fragmentFunction = [mLib newFunctionWithName:@"at_fs"];
            ad.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            mAtPso = [mDev newRenderPipelineStateWithDescriptor:ad error:&err];
            ad.fragmentFunction = [mLib newFunctionWithName:@"comp_fs"];
            ad.colorAttachments[0].pixelFormat = fmt;
            mCompPso = [mDev newRenderPipelineStateWithDescriptor:ad error:&err];
            if (!mAtPso || !mCompPso) NSLog(@"denoise pipeline: %@", err);
            ad.fragmentFunction = [mLib newFunctionWithName:@"tr_fs"];
            ad.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            ad.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
            mTrPso = [mDev newRenderPipelineStateWithDescriptor:ad error:&err];
            if (!mTrPso) NSLog(@"trace pipeline: %@", err);
         }
         MTLRenderPipelineDescriptor* dd2 = [MTLRenderPipelineDescriptor new];
         dd2.vertexFunction = pd.vertexFunction;
         dd2.fragmentFunction = fsDir;
         dd2.colorAttachments[0].pixelFormat = fmt;
         dd2.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mPsoDir = [mDev newRenderPipelineStateWithDescriptor:dd2 error:&err];
         {
            id<MTLFunction> rr = [mLib newFunctionWithName:@"rr_vs"];
            dd2.vertexFunction = rr;
            mPsoRunDir = [mDev newRenderPipelineStateWithDescriptor:dd2 error:&err];
            dd2.vertexFunction = pd.vertexFunction;
            id<MTLFunction> keep = pd.vertexFunction;
            pd.vertexFunction = rr;
            mPsoRun = [mDev newRenderPipelineStateWithDescriptor:pd error:&err];
            // Depth prepass: the same vertex function, no fragment work, so the shaded pass (and its rays) runs once per pixel.
            {
               mFsDirFn = fsDir;
               MTLRenderPipelineDescriptor* vd = [MTLRenderPipelineDescriptor new];
               vd.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
               vd.fragmentFunction = [mLib newFunctionWithName:@"vis_fs"];
               vd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
               vd.colorAttachments[1].pixelFormat = MTLPixelFormatR32Float;
               mVisPso = [mDev newRenderPipelineStateWithDescriptor:vd error:&err];
               if (!mVisPso) NSLog(@"shadow mask pipeline: %@", err);
            }
            pd.vertexFunction = keep;
            if (!mPsoRun || !mPsoRunDir) NSLog(@"run pipeline: %@", err);
         }
         {
            MTLRenderPipelineDescriptor* bd = [MTLRenderPipelineDescriptor new];
            bd.vertexFunction = [mLib newFunctionWithName:@"ri_vs"];
            bd.fragmentFunction = fsDir;
            bd.colorAttachments[0].pixelFormat = fmt;
            bd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            mPsoBatch = [mDev newRenderPipelineStateWithDescriptor:bd error:&err];
            {
               MTLRenderPipelineDescriptor* pp = [MTLRenderPipelineDescriptor new];
               pp.vertexFunction = [mLib newFunctionWithName:@"rp_vs"];
               pp.fragmentFunction = [mLib newFunctionWithName:@"rp_fs"];
               pp.colorAttachments[0].pixelFormat = fmt;
               pp.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
               mPsoPt = [mDev newRenderPipelineStateWithDescriptor:pp error:&err];
               pp.fragmentFunction = mFsFn;
               pp.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
               pp.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
               pp.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
               pp.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA32Float;
               mPsoPtMrt = [mDev newRenderPipelineStateWithDescriptor:pp error:&err];
               mCullPso = [mDev newComputePipelineStateWithFunction:[mLib newFunctionWithName:@"cull_k"] error:&err];
               mArgsPso = [mDev newComputePipelineStateWithFunction:[mLib newFunctionWithName:@"args_k"] error:&err];
               if (!mPsoPt || !mPsoPtMrt || !mCullPso || !mArgsPso) NSLog(@"point/cull pipeline: %@", err);
            }
            bd.fragmentFunction = mFsFn;
            bd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            bd.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
            bd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
            bd.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA32Float;
            mPsoBatchMrt = [mDev newRenderPipelineStateWithDescriptor:bd error:&err];
            if (!mPsoBatch || !mPsoBatchMrt) NSLog(@"batch pipeline: %@", err);
         }
         if (!mPsoDir) NSLog(@"direct pipeline: %@", err);
         if (!mResPso) NSLog(@"resolve pipeline: %@", err);
         MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
         dd.depthCompareFunction = MTLCompareFunctionLess;
         dd.depthWriteEnabled = YES;
         mDs = [mDev newDepthStencilStateWithDescriptor:dd];
         dd.depthCompareFunction = MTLCompareFunctionLessEqual;
         dd.depthWriteEnabled = NO;
         mDsEq = [mDev newDepthStencilStateWithDescriptor:dd];
         mFmt = fmt;
         mInvTried = false; mPrePso = nil; mPsoRunInv = nil; mPsoRunDirInv = nil; // rebuilt for the new format on first use
         {
            MTLRenderPipelineDescriptor* gd = [MTLRenderPipelineDescriptor new];
            gd.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
            gd.fragmentFunction = [mLib newFunctionWithName:@"bg_fs"];
            gd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            gd.colorAttachments[0].pixelFormat = fmt;
            mBgPsoDir = [mDev newRenderPipelineStateWithDescriptor:gd error:&err];
            gd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            gd.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
            gd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
            gd.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA32Float;
            for (int k = 1; k < 4; k++) gd.colorAttachments[k].writeMask = MTLColorWriteMaskNone;
            mBgPsoMrt = [mDev newRenderPipelineStateWithDescriptor:gd error:&err];
            if (!mBgPsoDir || !mBgPsoMrt) NSLog(@"background pipeline: %@", err);
            MTLDepthStencilDescriptor* bd = [MTLDepthStencilDescriptor new];
            bd.depthCompareFunction = MTLCompareFunctionAlways;
            bd.depthWriteEnabled = NO;
            mDsBg = [mDev newDepthStencilStateWithDescriptor:bd];
         }
         MTLRenderPipelineDescriptor* sd = [MTLRenderPipelineDescriptor new];
         sd.vertexFunction = [mLib newFunctionWithName:@"sh_vs"];
         sd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mShPso = [mDev newRenderPipelineStateWithDescriptor:sd error:&err];
         sd.vertexFunction = [mLib newFunctionWithName:@"shr_vs"];
         mShPsoRun = [mDev newRenderPipelineStateWithDescriptor:sd error:&err];
         return mPso != nil && mPsoDir != nil && mDs != nil && mShPso != nil && mResPso != nil && mPsoRun != nil && mPsoRunDir != nil && mShPsoRun != nil;
      }
      bool Depth(int w, int h)
      {
         if (mDepth && (int)mDepth.width == w && (int)mDepth.height == h)
            return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; // the shadow mask pass reads the prepass depth
         d.storageMode = MTLStorageModePrivate;
         mDepth = [mDev newTextureWithDescriptor:d];
         return mDepth != nil;
      }
      id<MTLDevice> mDev;
      id<MTLCommandQueue> mQueue;
      id<MTLLibrary> mLib = nil;
      id<MTLRenderPipelineState> mPso = nil;
      id<MTLDepthStencilState> mDs = nil, mDsEq = nil;
      id<MTLRenderPipelineState> mPrePso = nil, mVisPso = nil, mPsoRunInv = nil, mPsoRunDirInv = nil;
      id<MTLFunction> mFsDirFn = nil;
      bool mInvTried = false;
      // The invariant prepass + run pipelines, built the first time RT shadows are on (a second compile of the shader).
      bool EnsureInv()
      {
         if (mInvTried) return mPrePso && mPsoRunInv && mPsoRunDirInv;
         mInvTried = true;
         NSError* err = nil;
         MTLCompileOptions* co = [MTLCompileOptions new];
         co.languageVersion = MTLLanguageVersion2_3;
         co.preserveInvariance = YES;
         co.preprocessorMacros = @{ @"INV_POS" : @1 };
         id<MTLLibrary> lib = [mDev newLibraryWithSource:kRasterShader options:co error:&err];
         id<MTLFunction> rr = lib ? [lib newFunctionWithName:@"rr_vs"] : nil;
         if (!rr) { NSLog(@"invariant shader: %@", err); return false; }
         MTLRenderPipelineDescriptor* zp = [MTLRenderPipelineDescriptor new];
         zp.vertexFunction = rr;
         zp.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mPrePso = [mDev newRenderPipelineStateWithDescriptor:zp error:&err];
         MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
         pd.vertexFunction = rr; pd.fragmentFunction = mFsFn;
         pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
         pd.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
         pd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
         pd.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA32Float;
         pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mPsoRunInv = [mDev newRenderPipelineStateWithDescriptor:pd error:&err];
         MTLRenderPipelineDescriptor* dd = [MTLRenderPipelineDescriptor new];
         dd.vertexFunction = rr; dd.fragmentFunction = mFsDirFn;
         dd.colorAttachments[0].pixelFormat = mFmt;
         dd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mPsoRunDirInv = [mDev newRenderPipelineStateWithDescriptor:dd error:&err];
         if (!mPrePso || !mPsoRunInv || !mPsoRunDirInv) NSLog(@"invariant pipelines: %@", err);
         return mPrePso && mPsoRunInv && mPsoRunDirInv;
      }
      id<MTLTexture> mVis = nil, mVisD = nil; // half-res RT shadow mask (4 lights) and its distance guide
      id<MTLTexture> mDepth = nil;
      id<MTLTexture> mShadowTex = nil;
      NSMutableArray<id<MTLAccelerationStructure>>* mBlas = nil;
      id<MTLAccelerationStructure> mTlas = nil;
      id<MTLBuffer> mTlasScratch = nil, mRefitScratch = nil;
      std::vector<char> mBlasDirty;   // vertices moved in place; refit before the next instance build
      id<MTLFunction> mFsFn = nil;
      id<MTLArgumentEncoder> mArgEnc = nil;
      id<MTLBuffer> mArgBuf = nil;
      id<MTLAccelerationStructure> mArgTlas = nil;
      id<MTLRenderPipelineState> mShPso = nil, mShPsoRun = nil, mPsoRun = nil, mPsoRunDir = nil;
      id<MTLBuffer> mInstBuf = nil;
      std::vector<std::pair<uint32_t, uint32_t>> mRuns;   // (first, count) into mInstBuf: same mesh, same material

      // Every instance's InstU into one buffer, cut into runs of one mesh and one material (instanced copies come in
      // as consecutive instances), so 20,000 copies are a handful of draws instead of 20,000.
      void BuildRuns(const engine::Frame& fr)
      {
         mRuns.clear();
         const size_t n = fr.instances.size();
         const NSUInteger len = std::max<NSUInteger>(n * sizeof(InstU), 16);
         if (!mInstBuf || mInstBuf.length < len)
            mInstBuf = [mDev newBufferWithLength:len * 2 options:MTLResourceStorageModeShared];
         InstU* dst = (InstU*)mInstBuf.contents;
         for (size_t i = 0; i < n; i++)
         {
            const engine::Instance& in = fr.instances[i];
            InstU& u = dst[i]; u = {};
            std::memcpy(u.model, in.model, 64);
            std::memcpy(u.normalM, in.normalMatrix, 64);
            Lin3(in.baseSrgb, u.base);
            u.mr[0] = in.metallic; u.mr[1] = in.roughness; u.mr[2] = in.transmission; u.mr[3] = in.ior;
            if (in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            if (!mRuns.empty())
            {
               auto& r = mRuns.back();
               const engine::Instance& a = fr.instances[r.first];
               if (r.first + r.second == i && a.mesh == in.mesh && std::memcmp(dst[r.first].base, u.base, 32) == 0)
               {
                  r.second++;
                  continue;
               }
            }
            mRuns.emplace_back((uint32_t)i, 1u);
         }
      }
      id<MTLRenderPipelineState> mResPso = nil, mPsoDir = nil, mPsoBatch = nil, mPsoBatchMrt = nil, mPsoPt = nil, mPsoPtMrt = nil;
      id<MTLComputePipelineState> mCullPso = nil, mArgsPso = nil;
      struct BatchGpu { id<MTLBuffer> models = nil, visMesh = nil, visPt = nil, cnt = nil, argsMesh = nil, argsPt = nil; NSUInteger cap = 0; };
      std::vector<BatchGpu> mBatchGpu;
      id<MTLTexture> mHdr = nil, mGiT = nil, mRefl = nil, mHist[2] = { nil, nil }, mRHist[2] = { nil, nil }, mNz = nil, mAt[2] = { nil, nil };
      id<MTLRenderPipelineState> mAtPso = nil, mCompPso = nil, mTrPso = nil;
      id<MTLTexture> mTr[2] = { nil, nil }; // half-res trace: GI irradiance, reflection radiance
      int mHistCur = 0; bool mHistValid = false, mRValid = false; float mLastVP[16] = {};
      bool Targets(int w, int h)
      {
         if (mHdr && (int)mHdr.width == w && (int)mHdr.height == h)
            return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
         d.storageMode = MTLStorageModePrivate;
         mHdr = [mDev newTextureWithDescriptor:d]; mGiT = [mDev newTextureWithDescriptor:d];
         mHist[0] = [mDev newTextureWithDescriptor:d]; mHist[1] = [mDev newTextureWithDescriptor:d];
         mRefl = [mDev newTextureWithDescriptor:d]; mRHist[0] = [mDev newTextureWithDescriptor:d]; mRHist[1] = [mDev newTextureWithDescriptor:d];
         mAt[0] = [mDev newTextureWithDescriptor:d]; mAt[1] = [mDev newTextureWithDescriptor:d];
         d.pixelFormat = MTLPixelFormatRGBA32Float; // guides at full float: tr_fs rebuilds the position from the distance
         mNz = [mDev newTextureWithDescriptor:d];
         d.pixelFormat = MTLPixelFormatRGBA16Float; d.width = (w + 1) / 2; d.height = (h + 1) / 2;
         mTr[0] = [mDev newTextureWithDescriptor:d]; mTr[1] = [mDev newTextureWithDescriptor:d];
         d.pixelFormat = MTLPixelFormatRGBA8Unorm; mVis = [mDev newTextureWithDescriptor:d];
         d.pixelFormat = MTLPixelFormatR32Float; mVisD = [mDev newTextureWithDescriptor:d];
         mHistValid = false; mRValid = false;
         return mHdr && mGiT && mHist[0] && mHist[1] && mRefl && mRHist[0] && mRHist[1] && mNz && mAt[0] && mAt[1] && mTr[0] && mTr[1] && mVis && mVisD;
      }
      MTLPixelFormat mFmt = MTLPixelFormatInvalid;
      std::vector<Gpu> mMeshes;
   };

   class MetalDevice final : public Device
   {
   public:
      MetalDevice()
      {
         mDev = MTLCreateSystemDefaultDevice();
         if (mDev)
            mQueue = [mDev newCommandQueue];
      }
      bool Ok() const { return mDev != nil && mQueue != nil; }
      std::string Name() const override { return std::string("Metal: ") + [mDev.name UTF8String]; }
      std::unique_ptr<SharedImage> CreateSharedImage(int w, int h, Format fmt) override
      {
         NSError* err = nil;
         id<MTLLibrary> lib = [mDev newLibraryWithSource:kTestShader options:nil error:&err];
         if (!lib)
            return nullptr;
         MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
         pd.vertexFunction = [lib newFunctionWithName:@"tri_vs"];
         pd.fragmentFunction = [lib newFunctionWithName:@"tri_fs"];
         pd.colorAttachments[0].pixelFormat = fmt == Format::RGBA16F ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
         id<MTLRenderPipelineState> pso = [mDev newRenderPipelineStateWithDescriptor:pd error:&err];
         if (!pso)
            return nullptr;
         auto img = std::make_unique<MetalImage>(mDev, mQueue, pso, w, h, fmt);
         return img->Ok() ? std::unique_ptr<SharedImage>(std::move(img)) : nullptr;
      }

      std::unique_ptr<RasterRenderer> CreateRasterRenderer() override
      {
         auto r = std::make_unique<MetalRaster>(mDev, mQueue);
         return r->Ok() ? std::unique_ptr<RasterRenderer>(std::move(r)) : nullptr;
      }

   private:
      id<MTLDevice> mDev = nil;
      id<MTLCommandQueue> mQueue = nil;
   };
}

std::unique_ptr<Device> CreateDevice()
{
   auto d = std::make_unique<MetalDevice>();
   return d->Ok() ? std::unique_ptr<Device>(std::move(d)) : nullptr;
}
}

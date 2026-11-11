// Metal backend of the engine RHI. See Rhi.h.
#include "engine/rhi/Rhi.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
struct InstU { float4x4 model; float4x4 normalM; float4 base; float4 mr; float4 em; float4 tx; float4 sh; float4 shc; float4 tx2; float4 tx3; float4 mp0; float4 msc; float4 mro; float4 mtr; float4 olo; float4 ohi; float4 dt; }; // base linear rgb; mr = metallic, roughness, transmission, ior; em = linear emission * strength;
// tx = map index + 1 (albedo, roughness, metallic, normal; 0 = none); sh = sheen, sheen roughness, normal strength, wrap + 4 * bump; shc = sheen colour
// tx2 = ao, emission, sheen, opacity maps; tx3 = clearcoat, transmission maps; mp0 = mapping space, triplanar, alpha cutoff,
// 1 - opacity; msc/mro/mtr = mapping scale / rotation / offset, w = clearcoat, coat roughness, dielectric F0 - 0.04;
// olo/ohi = the mesh box, w = breakup, detail scale; dt.x = detail strength. Zero everywhere = the plain material.
// INV_POS: the second, invariant compile used by the depth prepass and the shaded runs after it (exact same depth);
// plain raster keeps the faster non-invariant build.
#ifdef INV_POS
#define VPOS [[position, invariant]]
#else
#define VPOS [[position]]
#endif
struct VOut { float4 pos VPOS; float3 wp; float3 n; float2 uv; float3 lp; };

vertex VOut r_vs(uint vid [[vertex_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                 constant InstU& m [[buffer(2)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4 w = m.model * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5; // GL clip depth -1..1 to Metal 0..1
   c.y = -c.y;              // the shared image is read by GL, whose row 0 is the bottom
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m.normalM * float4(n, 0.0)).xyz; o.uv = float2(0.0); o.lp = p; return o;
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
// A sun's shadow as its disc (angleDeg across, as settle's cone and Cycles' sun angle): four probe rays first; only
// where they disagree (the penumbra) twelve more on a golden-angle spiral, turned and jittered per
// pixel by a hash so the thin sampling reads as fine grain, not bands. 0 = fully blocked, 1 = open.
uint pcg(uint v);
float rnd(thread uint& s);
float sunVis(instance_acceleration_structure tlas, float3 o, float3 ld, float angleDeg, float2 pix)
{
   if (angleDeg <= 0.0) return occluded(tlas, o, ld, 1e4) ? 0.0 : 1.0;
   const float tr = tan(angleDeg * 0.5 * 0.017453292);
   const float3 ax = abs(ld.y) < 0.99 ? normalize(cross(ld, float3(0.0, 1.0, 0.0))) : float3(1.0, 0.0, 0.0);
   const float3 ay = cross(ld, ax);
   uint s = pcg(uint(pix.x) * 7919u + 104729u * uint(pix.y) + 31u);
   const float rot = 6.2831853 * rnd(s);
   // Four probes in a cross at 0.9 of the disc: one pair always spans the edge whichever way it runs, so the
   // penumbra never snaps to 0/1 (two opposite probes agree whenever they lie along the edge -> dither).
   float v = 0.0;
   for (int i = 0; i < 4; i++)
   {
      const float an = rot + float(i) * 1.5707963;
      v += occluded(tlas, o, normalize(ld + (ax * cos(an) + ay * sin(an)) * (0.9 * tr)), 1e4) ? 0.0 : 1.0;
   }
   if (v == 0.0 || v == 4.0) return v * 0.25;
   for (int i = 0; i < 12; i++)
   {
      const float an = rot + 0.7853982 + float(i) * 2.399963, r = sqrt((float(i) + rnd(s)) / 12.0) * tr;
      v += occluded(tlas, o, normalize(ld + (ax * cos(an) + ay * sin(an)) * r), 1e4) ? 0.0 : 1.0;
   }
   return v / 16.0;
}
struct RInst { float4 bm; float4 rg; float4 em; ulong vb; ulong ib; float4x4 normalM; ulong uvb; ulong pad0; float4 tx; float4 sh; float4 shc; float4 tx2; float4 tx3; float4 mp0; float4 msc; float4 mro; float4 mtr; float4 olo; float4 ohi; float4 dt; }; // em: linear emission * strength; uvb/tx/sh: the maps, as InstU
float3 cosineDir(float3 n, float u1, float u2);
// One hit: closest hit -> vertex normal, Lambert from every light (RT shadowed), ambient, env spec. Miss -> env.
// hp/hn/alb return the hit (alb = diffuse albedo, 0 on a miss) so GI can take one more bounce from it.
float3 shadeHit(constant FrameU& f, texture2d<float> et, instance_acceleration_structure tlas, constant RInst* inst, float3 o, float3 d, float rough,
                bool giMode, thread float3& hp, thread float3& hn, thread float3& alb, thread float3& mir, bool withEnv = true, int pick = -1,
                bool withSpec = true, float2 envU = float2(-1.0), float3 missDir = float3(0.0))
{
   alb = float3(0.0); mir = float3(0.0);
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   ray r(o, d, 0.0, 1e4);
   intersection_result<triangle_data, instancing> h = it.intersect(r, tlas);
   if (h.type != intersection_type::triangle) return giMode ? sampleEnv(f, et, d, 1.0) * f.ambient.xyz * 3.0 : sampleEnv(f, et, any(missDir != 0.0) ? missDir : d, rough);
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
   float3 envIrr = sampleEnv(f, et, n, 1.0) * f.ambient.xyz * 3.0; // same ambient convention as r_fs
   if (envU.x >= 0.0 && withEnv)
   {
      // A bounce's sky light, occluded: one cosine ray, the env along it when it escapes. The unoccluded form above lit
      // every off-screen wall of a room with the whole sky (interior case 7x Cycles' brightness).
      const float3 sd = cosineDir(n, envU.x, envU.y);
      envIrr = occluded(tlas, p + n * f.shQ.w, sd, 1e4) ? float3(0.0) : sampleEnv(f, et, sd, 1.0) * f.ambient.xyz * 3.0;
   }
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
   return alb * ((withEnv ? envIrr : float3(0.0)) + diff / 3.14159265) + (withSpec ? spec : float3(0.0)) + I.em.xyz;
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
// A ray that leaves the scene reads the sky prefiltered over the lobe along the mirror direction (its expected value),
// not along its own jittered direction: one sample of a sun disc in the sky made a speckled blob on the glitter path.
float3 traceReflection(constant FrameU& f, texture2d<float> et, instance_acceleration_structure tlas, constant RInst* inst, float3 o, float3 d, float rough, thread uint& seed,
                       texture2d<float> nz, texture2d<float> hist, bool hv, float3 dMirror = float3(0.0))
{
   float3 hp, hn, alb, mir;
   const bool bounce = f.gi.x > 0.5;
   float3 L = shadeHit(f, et, tlas, inst, o, d, rough, false, hp, hn, alb, mir, !bounce, -1, true, float2(-1.0), dMirror);
   float3 irr;
   if (any(alb > 0.0) && bounce && hv && histAt(f, nz, hist, hp, hn, irr))
      L += alb * irr * f.gi.y;
   else if (any(alb > 0.0) && bounce)
   {
      float3 hp2, hn2, alb2, mir2;
      const float3 d2 = cosineDir(hn, rnd(seed), rnd(seed));
      const int pk = int(rnd(seed) * f.params.w);
      const float3 L2 = shadeHit(f, et, tlas, inst, hp + hn * f.shQ.w, d2, 1.0, true, hp2, hn2, alb2, mir2, true, pk, true, float2(rnd(seed), rnd(seed)));
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
// Render 3D's tonemaps (Render3DTonemap.h), same order as the node: 1 ACES, 2 Reinhard, 3 AgX, 4 Neutral.
float3 agxTone(float3 c)
{
   const float3x3 inset = float3x3(float3(0.842479062253094, 0.0423282422610123, 0.0423756549057051),
                                   float3(0.0784335999999992, 0.878468636469772, 0.0784336),
                                   float3(0.0792237451477643, 0.0791661274605434, 0.879142973793104));
   const float3x3 outset = float3x3(float3(1.19687900512017, -0.0528968517574562, -0.0529716355144438),
                                    float3(-0.0980208811401368, 1.15190312990417, -0.0980434501171241),
                                    float3(-0.0990297440797205, -0.0989611768448433, 1.15107367264116));
   float3 x = clamp((log2(max(inset * c, float3(1e-10))) + 12.47393) / 16.5, 0.0, 1.0);
   x = clamp(((((((((-246.535442) * x + 984.731367) * x - 1568.41656) * x + 1268.06241) * x - 553.234808) * x + 130.459845) * x - 14.9498445) * x + 0.887512984) * x - 0.00843336545, 0.0, 1.0); // fit to Blender's AgX (Render3DTonemap.h)
   x = max(outset * x, float3(0.0));
   return pow(x, float3(2.4));
}
float3 neutralTone(float3 c)
{
   const float x = min(c.r, min(c.g, c.b));
   c -= (x < 0.08) ? x - 6.25 * x * x : 0.04;
   const float peak = max(c.r, max(c.g, c.b));
   if (peak < 0.76) return c;
   const float np = 1.0 - 0.0576 / (peak - 0.52);
   c *= np / peak;
   return mix(c, float3(np), 1.0 - 1.0 / (0.15 * (peak - np) + 1.0));
}
float3 toneMap(float3 c, int t)
{
   if (t == 1) return aces(c);
   if (t == 2) return c / (c + 1.0);
   if (t == 3) return agxTone(c);
   if (t == 4) return neutralTone(c);
   return c;
}
float3 toSrgb(float3 c) { c = max(c, float3(0.0)); return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c < float3(0.0031308)); }
// Material maps as Render 3D's GL path samples them: trilinear, anisotropy 8, the Material's wrap (0 clamp, 1 repeat, 2 mirror).
constexpr sampler kMapC(filter::linear, mip_filter::linear, address::clamp_to_edge, max_anisotropy(8));
constexpr sampler kMapR(filter::linear, mip_filter::linear, address::repeat, max_anisotropy(8));
constexpr sampler kMapM(filter::linear, mip_filter::linear, address::mirrored_repeat, max_anisotropy(8));
float4 mapS(texture2d<float> t, int wrap, float2 uv) { return wrap == 1 ? t.sample(kMapR, uv) : wrap == 2 ? t.sample(kMapM, uv) : t.sample(kMapC, uv); }
float4 mapL(texture2d<float> t, int wrap, float2 uv, float lod) { return wrap == 1 ? t.sample(kMapR, uv, level(lod)) : wrap == 2 ? t.sample(kMapM, uv, level(lod)) : t.sample(kMapC, uv, level(lod)); }
float3 mapLin(float3 c) { return select(pow((c + 0.055) / 1.055, float3(2.4)), c / 12.92, c < float3(0.04045)); }
// Mapping (the Mapping node, as GL's computeMapUv / sampleMap): uv, generated (the mesh box) or object coordinates,
// scaled, rotated and offset; outside uv, the dominant axis picks the plane or triplanar blends all three.
struct MapX { int space; float tri; float3 sc, ro, tr, lo, hi; float brk; int wrap; };
MapX mapX(float4 mp0, float4 msc, float4 mro, float4 mtr, float4 olo, float4 ohi, int wrap)
{
   MapX x; x.space = int(mp0.x + 0.5); x.tri = mp0.y; x.sc = msc.xyz; x.ro = mro.xyz; x.tr = mtr.xyz; x.lo = olo.xyz; x.hi = ohi.xyz;
   x.brk = olo.w; x.wrap = wrap; return x;
}
float3 mapRot(float3 p, float3 r)
{
   const float cx = cos(r.x), sx = sin(r.x), cy = cos(r.y), sy = sin(r.y), cz = cos(r.z), sz = sin(r.z);
   p = float3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
   p = float3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
   return float3(cz * p.x - sz * p.y, sz * p.x + cz * p.y, p.z);
}
float3 mapBase(MapX x, float2 uv, float3 lp)
{
   const float3 b = x.space == 1 ? (lp - x.lo) / max(x.hi - x.lo, float3(1e-5)) : x.space == 2 ? lp : float3(uv, 0.0);
   return mapRot(b * x.sc, x.ro) + x.tr;
}
// Linear in b, so it also projects the screen derivatives of the base.
float2 mapUv(MapX x, float3 b, float3 n)
{
   if (x.space == 0) return b.xy;
   const float3 an = abs(n);
   if (an.x >= an.y && an.x >= an.z) return b.yz;
   if (an.y >= an.x && an.y >= an.z) return b.xz;
   return b.xy;
}
float4 tapG(texture2d<float> t, int wrap, float2 uv, float2 gx, float2 gy, float lod)
{
   if (lod >= 0.0) return mapL(t, wrap, uv, lod);
   const gradient2d g(gx, gy);
   return wrap == 1 ? t.sample(kMapR, uv, g) : wrap == 2 ? t.sample(kMapM, uv, g) : t.sample(kMapC, uv, g);
}
float brkHash(float2 p) { return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float brkNoise(float2 p)
{
   const float2 i = floor(p); float2 f = fract(p); f = f * f * (3.0 - 2.0 * f);
   return mix(mix(brkHash(i), brkHash(i + float2(1.0, 0.0)), f.x), mix(brkHash(i + float2(0.0, 1.0)), brkHash(i + float2(1.0, 1.0)), f.x), f.y);
}
// Breakup (texture-repetition hiding after Quilez): a slow noise picks one of 8 offsets of the tile per area, and two
// neighbouring picks blend where the noise crosses between them, so no two tiles repeat side by side.
float4 mapTap(texture2d<float> t, MapX x, float2 uv, float2 gx, float2 gy, float lod)
{
   if (x.brk <= 0.001) return tapG(t, x.wrap, uv, gx, gy, lod);
   const float k = brkNoise(uv * 0.6) * 8.0;
   const float i = floor(k), fk = k - i;
   const float2 oa = sin(float2(3.0, 7.0) * i), ob = sin(float2(3.0, 7.0) * (i + 1.0));
   // A broken-up map tiles by nature: the offsets read past 0..1, so it always repeats.
   const float4 ca = tapG(t, 1, uv + oa, gx, gy, lod), cb = tapG(t, 1, uv + ob, gx, gy, lod);
   const float4 u = mix(ca, cb, smoothstep(0.2, 0.8, fk - 0.1 * dot(ca.rgb - cb.rgb, float3(1.0))));
   return x.brk >= 0.999 ? u : mix(tapG(t, 1, uv, gx, gy, lod), u, x.brk);
}
float2 prj(float3 v, int a) { return a == 0 ? v.yz : a == 1 ? v.xz : v.xy; }
// lod < 0: the raster's screen gradients (bx, by: derivatives of the base); else an explicit level (the tracer).
float4 mapSample(texture2d<float> t, MapX x, float3 b, float3 n, float3 bx, float3 by, float lod)
{
   if (x.space == 0 || x.tri <= 0.001) return mapTap(t, x, mapUv(x, b, n), mapUv(x, bx, n), mapUv(x, by, n), lod);
   const float sharp = 1.0 + (1.0 - x.tri) * 7.0;
   float3 w = pow(max(abs(n), float3(1e-4)), float3(sharp));
   w /= (w.x + w.y + w.z);
   float4 c = float4(0.0);
   for (int a = 0; a < 3; a++) c += mapTap(t, x, prj(b, a), prj(bx, a), prj(by, a), lod) * w[a];
   return c;
}
// The tangent-space sample (GL's applyNormalMap): a normal map, or with bump a grey height map's central differences.
float3 mapTangent(texture2d<float> t, MapX x, bool bump, float strength, float2 uv, float2 gx, float2 gy, float lod)
{
   float3 s;
   if (bump)
   {
      const float2 px = exp2(max(lod, 0.0)) / float2(t.get_width(), t.get_height());
      const float3 lum = float3(0.2126, 0.7152, 0.0722);
      const float hl = dot(mapTap(t, x, uv - float2(px.x, 0.0), gx, gy, lod).rgb, lum), hr = dot(mapTap(t, x, uv + float2(px.x, 0.0), gx, gy, lod).rgb, lum);
      const float hd = dot(mapTap(t, x, uv - float2(0.0, px.y), gx, gy, lod).rgb, lum), hu = dot(mapTap(t, x, uv + float2(0.0, px.y), gx, gy, lod).rgb, lum);
      s = normalize(float3((hl - hr) * 2.0, (hd - hu) * 2.0, 1.0));
   }
   else s = mapTap(t, x, uv, gx, gy, lod).rgb * 2.0 - 1.0;
   s.xy *= strength;
   s.z = max(s.z, 1e-3); // a texel with z <= 0 would flip the surface
   return s;
}
// The normal map and its detail read (the same map at uv * detail scale), whiteout-blended.
float3 mapNormalTS(texture2d<float> t, MapX x, bool bump, float strength, float2 uv, float2 gx, float2 gy, float lod, float dScale, float dStrength)
{
   float3 s = mapTangent(t, x, bump, strength, uv, gx, gy, lod);
   if (dScale > 0.0)
   {
      MapX xd = x; xd.wrap = 1; // the detail always tiles
      const float3 d = mapTangent(t, xd, bump, dStrength, uv * dScale, gx * dScale, gy * dScale, lod);
      s = normalize(float3(s.xy + d.xy, s.z * d.z));
   }
   return s;
}
float coatSpec(float nDotH, float nDotV, float nDotL, float vDotH, float r)
{
   return distGGX(nDotH, r) * geomSchlick(nDotV, r) * geomSchlick(nDotL, r) * (0.04 + 0.96 * pow(1.0 - vDotH, 5.0)) / max(4.0 * nDotV * nDotL, 1e-7);
}
// Sheen: Cycles' microfiber lobe, an LTC fitted to Blender measurements; see Render3DSheen.h for the tables.
// Rows roughness 0.1..1.0 step 0.15, columns cos(view) 0..1 in 11 steps.
constant float kSheenE[84] = { 0.1027, 0.0530, 0.0033, 0.0003, 0.0001, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.5498, 0.3736, 0.1973, 0.0804, 0.0304, 0.0117, 0.0049, 0.0023, 0.0012, 0.0007, 0.0004, 0.0003, 0.6310, 0.5032, 0.3755, 0.2530, 0.1616, 0.0997, 0.0601, 0.0357, 0.0214, 0.0129, 0.0078, 0.0052, 0.6578, 0.5674, 0.4771, 0.3802, 0.2955, 0.2242, 0.1681, 0.1236, 0.0898, 0.0648, 0.0459, 0.0330, 0.6782, 0.6124, 0.5467, 0.4733, 0.4033, 0.3394, 0.2834, 0.2353, 0.1939, 0.1593, 0.1296, 0.1068, 0.7049, 0.6549, 0.6049, 0.5473, 0.4907, 0.4377, 0.3893, 0.3464, 0.3080, 0.2738, 0.2433, 0.2191, 0.7385, 0.6970, 0.6555, 0.6077, 0.5618, 0.5185, 0.4800, 0.4453, 0.4144, 0.3874, 0.3636, 0.3441 };
constant float kSheenA[84] = { 0.05, 0.05, 0.05, 0.0748, 0.0996, 0.1492, 0.1739, 0.1987, 0.22, 0.24, 0.26, 0.28, 0.1243, 0.1491, 0.1739, 0.1987, 0.2235, 0.2235, 0.2483, 0.2731, 0.2979, 0.2979, 0.3475, 0.3971, 0.2483, 0.2731, 0.2979, 0.3227, 0.3475, 0.3723, 0.3971, 0.3971, 0.4218, 0.4466, 0.4714, 0.4962, 0.3722, 0.397, 0.4218, 0.4466, 0.4962, 0.521, 0.521, 0.5458, 0.5458, 0.5706, 0.5954, 0.5954, 0.521, 0.5458, 0.5706, 0.5954, 0.6202, 0.645, 0.6697, 0.6697, 0.6945, 0.6945, 0.7193, 0.7193, 0.6697, 0.6945, 0.7193, 0.7441, 0.7689, 0.7689, 0.7689, 0.7937, 0.7937, 0.7937, 0.8185, 0.8185, 0.7689, 0.7937, 0.8185, 0.8433, 0.8433, 0.8681, 0.8681, 0.8681, 0.8681, 0.8681, 0.8681, 0.8681 };
constant float kSheenB[84] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -0.05, -0.05, -0.05, -0.025, -0.025, -0.025, -0.025, -0.05, -0.05, -0.05, -0.05, -0.025, -0.1, -0.1, -0.075, -0.05, -0.05, -0.05, -0.025, -0.025, -0.025, -0.05, -0.05, -0.025, -0.15, -0.15, -0.125, -0.1, -0.075, -0.075, -0.05, -0.05, -0.05, -0.05, -0.025, -0.025, -0.15, -0.15, -0.125, -0.1, -0.1, -0.075, -0.075, -0.075, -0.05, -0.05, -0.025, -0.025, -0.075, -0.075, -0.075, -0.075, -0.075, -0.05, -0.05, -0.05, -0.05, -0.025, -0.025, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
float sheenLut(constant float* t, float mu, float r)
{
   const float x = clamp(mu, 0.0, 1.0) * 11.0, y = clamp((r - 0.1) / 0.15, 0.0, 6.0);
   const int x0 = min(int(x), 10), y0 = min(int(y), 5);
   const float fx = x - float(x0), fy = y - float(y0);
   const float a = mix(t[y0 * 12 + x0], t[y0 * 12 + x0 + 1], fx);
   const float b = mix(t[(y0 + 1) * 12 + x0], t[(y0 + 1) * 12 + x0 + 1], fx);
   return mix(a, b, fy);
}
float sheenE(float mu, float r) { return sheenLut(kSheenE, mu, r); }
float sheenLtc(float nL, float nV, float lv, float r)
{
   const float a = sheenLut(kSheenA, nV, r), b = sheenLut(kSheenB, nV, r);
   const float st = sqrt(max(1.0 - nL * nL, 0.0));
   const float x = clamp((lv - nL * nV) / max(sqrt(max(1.0 - nV * nV, 0.0)), 1e-4), -st, st);
   const float wx = a * x + b * nL, q = wx * wx + a * a * max(st * st - x * x, 0.0) + nL * nL;
   return sheenE(nV, r) * a * a / (3.14159265 * max(q * q, 1e-8));
}
float maxc(float3 c) { return max(c.x, max(c.y, c.z)); }

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
      if (type == 1 || type == 4) { if (occluded(rtArgs.tlas, p + n * f.shQ.w, ld, tmax)) o.v[ch] = 0.0; }
      else o.v[ch] = sunVis(rtArgs.tlas, p + n * f.shQ.w, ld, (type == 0 || type == 2) ? L.spot.x : 0.0, float2(px));
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
                     texture2d<float> visT [[texture(7)]], texture2d<float> visD [[texture(8)]],
                     array<texture2d<float>, 16> maps [[texture(10)]])
{
   float3 n = normalize(in.n);
   float3 base = m.base.xyz;
   float metalIn = m.mr.x, roughIn = m.mr.y;
   const int wrapM = int(m.sh.w + 0.5) & 3;
   // Every derivative first, in uniform control flow, before any map read or cutout.
   const MapX mx = mapX(m.mp0, m.msc, m.mro, m.mtr, m.olo, m.ohi, wrapM);
   const float3 mb = mapBase(mx, in.uv, in.lp);
   const float3 mbx = dfdx(mb), mby = dfdy(mb);
   const float3 dp1 = dfdx(in.wp), dp2 = dfdy(in.wp);
   const float3 dnx = dfdx(in.n), dny = dfdy(in.n);
   const float3 nP = n; // the projection's normal (triplanar weights, dominant plane)
#define MAPR(slot) mapSample(maps[int(slot) - 1], mx, mb, nP, mbx, mby, -1.0)
   if (m.tx.x > 0.5) base *= mapLin(MAPR(m.tx.x).rgb);
   if (m.tx.y > 0.5) roughIn *= MAPR(m.tx.y).r;
   if (m.tx.z > 0.5) metalIn *= MAPR(m.tx.z).r;
   const float aoV = m.tx2.x > 0.5 ? MAPR(m.tx2.x).r : 1.0;
   const float3 emMap = m.tx2.y > 0.5 ? mapLin(MAPR(m.tx2.y).rgb) : float3(1.0);
   const float shW = m.sh.x * (m.tx2.z > 0.5 ? MAPR(m.tx2.z).r : 1.0);
   const float shMax = shW * maxc(m.shc.xyz);
   float alpha = 1.0 - m.mp0.w;
   if (m.tx2.w > 0.5) alpha *= MAPR(m.tx2.w).r;
   const float ccW = m.msc.w * (m.tx3.x > 0.5 ? MAPR(m.tx3.x).r : 1.0);
   const float transMap = m.tx3.y > 0.5 ? MAPR(m.tx3.y).r : 1.0;
#undef MAPR
   if (alpha < 1.0)
   {
      // A cutoff cuts below it; without one, opacity is a coverage the frames average (a per-pixel threshold).
      uint as = pcg(uint(in.pos.x) * 7919u + 104729u * uint(in.pos.y)) ^ pcg(uint(f.gi.z) * 977u + 5u);
      if (alpha < (m.mp0.z > 0.0 ? m.mp0.z : rnd(as))) discard_fragment();
   }
   const float metal = clamp(metalIn, 0.0, 1.0);
   // Specular anti-aliasing (Kaplanyan / Tokuyoshi): where the normal turns faster than a pixel (waves at the horizon),
   // the lobe widens by that variance, so a glitter path reads as one column instead of flickering sparkles.
   const float nVar = min(2.0 * 0.25 * (dot(dnx, dnx) + dot(dny, dny)), 0.18);
   const float rough = sqrt(clamp(sqrt(clamp(roughIn * roughIn * roughIn * roughIn + nVar, 0.0, 1.0)), 0.045 * 0.045, 1.0));
   const float3 viewDir = normalize(f.eye.xyz - in.wp);
   // Two-sided: the back of an open surface (a sail, a leaf) shades with the normal on the side we see, as Cycles
   // does; else it takes the far side's light and its shadow rays start on that side. The geometric normal decides,
   // so smooth normals that tip away at a silhouette are left alone.
   float3 gN = cross(dp1, dp2);
   if (dot(gN, gN) > 1e-20) { gN = normalize(gN); if (dot(gN, viewDir) < 0.0) gN = -gN; if (dot(n, gN) < 0.0) n = -n; }
   if (m.tx.w > 0.5)
   {
      // GL's cotangent frame from screen derivatives of position and the mapped uv (Schuler), then the tangent-space sample.
      const float2 muv = mapUv(mx, mb, nP), duv1 = mapUv(mx, mbx, nP), duv2 = mapUv(mx, mby, nP);
      const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      float3 t = dp2perp * duv1.x + dp1perp * duv2.x, bt = dp2perp * duv1.y + dp1perp * duv2.y;
      if (max(dot(t, t), dot(bt, bt)) > 1e-12) { t = normalize(t); bt = normalize(bt); }
      else { t = abs(n.z) < 0.999 ? normalize(cross(n, float3(0.0, 0.0, 1.0))) : normalize(cross(n, float3(1.0, 0.0, 0.0))); bt = normalize(cross(n, t)); }
      const float3 s = mapNormalTS(maps[int(m.tx.w) - 1], mx, (int(m.sh.w + 0.5) & 4) != 0, m.sh.z, muv, duv1, duv2, -1.0, m.ohi.w, m.dt.x);
      n = normalize(t * s.x + bt * s.y + n * s.z);
   }
   const float nDotV = max(dot(n, viewDir), 1e-4);
   const float shEv = shMax > 0.0 ? sheenE(nDotV, m.sh.y) : 0.0;
   const float shV = 1.0 - shMax * shEv;          // the base layer's share under the sheen
   const float3 shAmb = m.shc.xyz * (shW * shEv); // the sheen's own share of ambient light
   const float3 f0 = mix(float3(0.04 + m.mtr.w), base, metal); // GL's dielectric F0 from ior and specular
   // Clearcoat (GL's): a layer on top; what it reflects at this angle never reaches the base below.
   const float ccR = clamp(m.mro.w, 0.02, 1.0);
   const float ccFv = 0.04 + 0.96 * pow(1.0 - nDotV, 5.0);
   const float ccAt = 1.0 - ccW * ccFv;
   float3 coatCol = float3(0.0);
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
      nDotL = max(dot(n, lightDir), 0.0); // no GL sun wrap: the tracer and Cycles have none, so live and settled agree
      if (type == 5) nDotL = max(nDotL, 1e-3); // the centre may face away while part of the rectangle is seen
      if (nDotL <= 0.0) continue;
      const float nDotH = max(dot(n, halfway), 0.0);
      const float d = distGGX(nDotH, rough);
      const float g = geomSchlick(nDotV, rough) * geomSchlick(nDotL, rough);
      const float3 fr = fresnel(max(dot(halfway, viewDir), 0.0), f0);
      float3 spec = (d * g * fr) / max(4.0 * nDotV * nDotL, 1e-7);
      if ((type == 0 || type == 2) && L.spot.x > 0.0)
      {
         // A sun disc, not a point: the highlight takes the disc direction nearest the mirror ray, on a lobe widened
         // by the disc and renormalised (representative point, Karis 2013), so a glitter path is as wide as Cycles'.
         const float sr = tan(L.spot.x * 0.5 * 0.017453292);
         const float3 R = reflect(-viewDir, n);
         const float3 ctr = dot(lightDir, R) * R - lightDir;
         const float3 sd = normalize(lightDir + ctr * clamp(sr / max(length(ctr), 1e-6), 0.0, 1.0));
         const float a = rough * rough, a2 = clamp(a + 0.5 * sr, 0.0, 1.0);
         const float nls = max(dot(n, sd), 0.0);
         const float3 hs = normalize(sd + viewDir);
         const float ds = distGGX(max(dot(n, hs), 0.0), sqrt(a2)) * (a / a2) * (a / a2);
         const float gs = geomSchlick(nDotV, rough) * geomSchlick(max(nls, 1e-4), rough);
         spec = ds * gs * fresnel(max(dot(hs, viewDir), 0.0), f0) / max(4.0 * nDotV, 1e-7) * (nls / max(nDotL, 1e-4)); // * nDotL below
      }
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
               if (type == 1 || type == 4)
                  vis = occluded(rtArgs.tlas, in.wp + n * f.shQ.w, lightDir, max(length(L.vec.xyz - in.wp) - 0.01, 0.0)) ? 0.0 : 1.0;
               else
                  vis = sunVis(rtArgs.tlas, in.wp + n * f.shQ.w, lightDir, (type == 0 || type == 2) ? L.spot.x : 0.0, in.pos.xy);
            }
            shadow = mix(1.0 - f.shP.w, 1.0, vis);
         }
      }
      float3 shS = float3(0.0);
      float shK = 1.0;
      if (shW > 0.0) // sheen: Cycles' microfiber lobe, over a base scaled by its albedo
      {
         shS = m.shc.xyz * sheenLtc(nDotL, nDotV, dot(lightDir, viewDir), m.sh.y) * shW;
         shK = shV; // Cycles scales the base by the sheen albedo at the view angle only
      }
      col += ((kD * base / 3.14159265 * L.terms.x + spec * L.terms.y) * shK + shS * L.terms.y) * radiance * nDotL * shadow;
      if (ccW > 0.0) coatCol += ccW * coatSpec(nDotH, nDotV, nDotL, max(dot(halfway, viewDir), 0.0), ccR) * L.terms.y * radiance * nDotL * shadow;
   }
   if (!(f.gi.x > 0.5)) col += base / 3.14159265 * floorBounce * (0.25 * (1.0 - n.y)) * (1.0 - metal); // fake bounce only without real GI
   // GI and traced reflections run in tr_fs at half res; this pass writes their demodulated weights instead.
   const float3 irradiance = sampleEnv(f, et, n, 1.0) * f.ambient.xyz * 3.0;
   const float3 fAmb = fresnelRough(nDotV, f0, rough);
   const float3 envRefl = sampleEnv(f, et, reflect(-viewDir, n), rough) * fAmb * (aoV * ccAt * shV);
   const bool reflOn = (int(f.shQ.z + 0.5) & 2) != 0 && rough < 0.55;
   const float envKeep = reflOn ? smoothstep(0.3, 0.55, rough) : 1.0; // the env share of the reflection; the rest is traced
   const bool giOn = f.gi.x > 0.5;
   if (!giOn) col += ((float3(1.0) - fAmb) * (1.0 - metal) * base * shV + shAmb) * irradiance * aoV;
   col += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
   // Glass (transmission): refract in, find the far side of the hit object, refract out (or reflect on total internal
   // reflection), shade what lies beyond. Tint and Fresnel as Render 3D's: beyond * mix(1, base, 0.4) * (1 - F).
   // Needs the TLAS; without it the glass shows the environment along one refracted ray.
   const float trans = clamp(m.mr.z * transMap, 0.0, 1.0);
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
   col = col * ccAt + coatCol + (ccW > 0.0 ? sampleEnv(f, et, reflect(-viewDir, n), ccR) * (ccW * ccFv) : float3(0.0));
   col += m.em.xyz * emMap;
   const float opaque = (trans > 0.0 && rtOn) ? 1.0 - trans : 1.0; // glass takes no GI of its own
   FOut o;
   if (!kMrt)
   {
      float3 fin = (col + envRefl) * f.params.x;
      fin = toneMap(fin, int(f.params.y));
      o.c0 = float4(toSrgb(fin), 1.0);
      return o;
   }
   // With GI, the rough share of the reflection sees the room, not only the env: its lobe is wide enough that the
   // cosine-weighted GI radiance stands in for it (both carry the env on a miss, ambient * 3 == 1 in linear terms).
   o.c0 = float4((col + (giOn ? float3(0.0) : envRefl * envKeep)) * f.params.x, 1.0);
   o.c1 = float4(giOn ? (((float3(1.0) - fAmb) * (1.0 - metal) * base * opaque + fAmb * envKeep) * shV + shAmb * opaque) * f.gi.y * f.params.x * (aoV * ccAt) : float3(0.0), rough); // GI weight
   o.c2 = float4(fAmb * (1.0 - envKeep) * f.params.x * (aoV * ccAt * shV), reflOn ? 1.0 : 0.0); // traced-reflection weight
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
            const float3 L2 = shadeHit(f, et, rtArgs.tlas, instInfo, hp + hn * f.shQ.w, reflect(dir, hn), 0.3, false, hp2, hn2, alb2, mir2, true, int(rnd(seed) * f.params.w), true, float2(rnd(seed), rnd(seed)));
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
            const float3 L2 = shadeHit(f, et, rtArgs.tlas, instInfo, hp + hn * f.shQ.w, d2, 1.0, true, hp2, hn2, alb2, mir2, true, int(rnd(seed) * f.params.w), true, float2(rnd(seed), rnd(seed)));
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
      o.r = float4(traceReflection(f, et, rtArgs.tlas, instInfo, org, jd, rough, rs, nz, hist, hv, reflect(d, n)), 1.0);
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
      float3 b = c.rgb; b = toneMap(b, int(u.p.y));
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
   col = toneMap(col, int(u.p.y));
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
// Live depth of field: Render 3D's post blur (Render3DPost.cpp) on the finished picture. The circle of confusion is
// aperture px x |d - focus| / d from the depth buffer; taps on a golden-angle spiral turned per pixel, each weighted by
// whether its own circle reaches back here, so a sharp subject does not bleed onto a blurred background.
struct DofU { float4x4 invVP; float4 info; float4 lens; float4 eye; }; // info: width, height, aperture px, max coc; lens: focus, blades, turn, ratio
float dofCoc(constant DofU& u, depth2d<float> dt, int2 p)
{
   p = clamp(p, int2(0), int2(u.info.xy) - 1);
   const float z = dt.read(uint2(p));
   float dist = 1.0e4;
   if (z < 0.999999)
   {
      const float2 ndc = float2(2.0 * (float(p.x) + 0.5) / u.info.x - 1.0, 2.0 * (float(p.y) + 0.5) / u.info.y - 1.0);
      const float4 pw = u.invVP * float4(ndc, z * 2.0 - 1.0, 1.0);
      dist = length(pw.xyz / pw.w - u.eye.xyz);
   }
   return clamp(u.info.z * abs(dist - u.lens.x) / max(dist, 1.0e-3), 0.0, u.info.w);
}
fragment float4 dof_fs(SkyOut in [[stage_in]], constant DofU& u [[buffer(0)]], texture2d<float> col [[texture(0)]], depth2d<float> dt [[texture(1)]])
{
   const int2 px = int2(in.pos.xy);
   const float4 c0 = col.read(uint2(px));
   const float coc0 = dofCoc(u, dt, px);
   if (coc0 < 0.5) return c0;
   float4 sum = c0; float wsum = 1.0;
   const int taps = int(clamp(12.0 + coc0 * 2.0, 24.0, 48.0));
   const float rot = 6.2831853 * fract(52.9829189 * fract(dot(in.pos.xy, float2(0.06711056, 0.00583715))));
   for (int i = 0; i < taps; i++)
   {
      const float a = rot + float(i) * 2.399963;
      float r = sqrt((float(i) + 0.5) / float(taps)) * coc0;
      if (u.lens.y >= 3.0)
      {
         const float seg = 6.2831853 / u.lens.y;
         r *= cos(0.5 * seg) / cos(fmod(fmod(a - u.lens.z, seg) + seg, seg) - 0.5 * seg);
      }
      float2 off = float2(cos(a), sin(a)) * r;
      off *= u.lens.w >= 1.0 ? float2(1.0 / u.lens.w, 1.0) : float2(1.0, u.lens.w);
      r = length(off);
      const int2 q = clamp(px + int2(round(off)), int2(0), int2(u.info.xy) - 1);
      const float w = clamp(dofCoc(u, dt, q) - r + 1.0, 0.0, 1.0);
      if (w <= 0.0) continue;
      sum += col.read(uint2(q)) * w; wsum += w;
   }
   return sum / wsum;
}
struct CompU { float4 p; float4 clear; }; // p.y tonemap, p.z HDRI background
fragment float4 comp_fs(SkyOut in [[stage_in]], constant CompU& u [[buffer(0)]], texture2d<float> hdr [[texture(0)]], texture2d<float> gi [[texture(1)]], texture2d<float> rfl [[texture(2)]],
                        texture2d<float> gw [[texture(3)]], texture2d<float> rw [[texture(4)]])
{
   const uint2 px = uint2(in.pos.xy);
   const float4 c = hdr.read(px);
   if (c.a < 0.5)
   {
      float3 b = c.rgb; b = toneMap(b, int(u.p.y));
      return u.p.z > 0.5 ? float4(toSrgb(b), 1.0) : float4(u.clear.xyz, 1.0);
   }
   float3 col = c.rgb + gw.read(px).rgb * gi.read(px).rgb + rw.read(px).rgb * rfl.read(px).rgb;
   col = toneMap(col, int(u.p.y));
   return float4(toSrgb(col), 1.0);
}

// ---- path tracer: progressive, one sample per pass, accumulated in RGBA32F (rgb = radiance sum, a = coverage sum)
struct PtU { float4x4 invVP; float4 info; float4 em; float4 lens; float4 lr; float4 lu; float4 lf; }; // info: x prior samples, y max bounces, z frame, w width; em: x emitter triangles, y chance to sample one
// lens: thin-lens radius, focus distance, blades (below 3 round), blade turn; lr/lu/lf: camera right (w = bokeh ratio), up, forward
// One emissive triangle in world space. p0.w: cumulative selection chance (by power); e1.w: selection chance / area.
struct EmTri { float4 p0; float4 e1; float4 e2; float4 em; };
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
// Settle's shadow ray: it passes cut-outs and see-through surfaces as the camera ray does (opacity and the opacity
// map against the cutoff, or by chance below 1), so a leaf's holes let light through as in Cycles.
bool occludedAlpha(instance_acceleration_structure tlas, constant RInst* inst, array<texture2d<float>, 16> maps,
                   float3 o, float3 d, float tmax, float eps, thread uint& seed)
{
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   for (int k = 0; k < 16; k++)
   {
      ray r(o, d, 0.0, tmax);
      intersection_result<triangle_data, instancing> h = it.intersect(r, tlas);
      if (h.type != intersection_type::triangle) return false;
      constant RInst& I = inst[h.instance_id];
      float alpha = 1.0 - I.mp0.w;
      if (I.tx2.w > 0.5)
      {
         const device uint* ib = reinterpret_cast<const device uint*>(I.ib);
         const device float* vb = reinterpret_cast<const device float*>(I.vb);
         const uint3 ix = uint3(ib[h.primitive_id * 3], ib[h.primitive_id * 3 + 1], ib[h.primitive_id * 3 + 2]);
         const float2 bc = h.triangle_barycentric_coord;
         const float3 w3 = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
         float2 uv = float2(0.0);
         if (I.uvb != 0)
         {
            const device float* uvp = reinterpret_cast<const device float*>(I.uvb);
            uv = float2(uvp[ix.x * 2], uvp[ix.x * 2 + 1]) * w3.x + float2(uvp[ix.y * 2], uvp[ix.y * 2 + 1]) * w3.y
               + float2(uvp[ix.z * 2], uvp[ix.z * 2 + 1]) * w3.z;
         }
         const float3 lp = float3(vb[ix.x * 6], vb[ix.x * 6 + 1], vb[ix.x * 6 + 2]) * w3.x
                         + float3(vb[ix.y * 6], vb[ix.y * 6 + 1], vb[ix.y * 6 + 2]) * w3.y
                         + float3(vb[ix.z * 6], vb[ix.z * 6 + 1], vb[ix.z * 6 + 2]) * w3.z;
         const float3 nl = float3(vb[ix.x * 6 + 3], vb[ix.x * 6 + 4], vb[ix.x * 6 + 5]) * w3.x
                         + float3(vb[ix.y * 6 + 3], vb[ix.y * 6 + 4], vb[ix.y * 6 + 5]) * w3.y
                         + float3(vb[ix.z * 6 + 3], vb[ix.z * 6 + 4], vb[ix.z * 6 + 5]) * w3.z;
         const float3 n = normalize((I.normalM * float4(nl, 0.0)).xyz);
         const MapX mx = mapX(I.mp0, I.msc, I.mro, I.mtr, I.olo, I.ohi, int(I.sh.w + 0.5) & 3);
         alpha *= mapSample(maps[int(I.tx2.w) - 1], mx, mapBase(mx, uv, lp), n, float3(0.0), float3(0.0), 0.0).r;
      }
      if (!(alpha < 1.0 && alpha < (I.mp0.z > 0.0 ? I.mp0.z : rnd(seed)))) return true;
      o += d * (h.distance + eps); tmax -= h.distance + eps;
      if (tmax <= 0.0) return false;
   }
   return true;
}
fragment float4 pt_fs(SkyOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant PtU& u [[buffer(0)]],
                      constant RTArgs& rtArgs [[buffer(3)]], constant RInst* inst [[buffer(4)]], const device EmTri* ems [[buffer(5)]],
                      texture2d<float> prev [[texture(0)]], texture2d<float> et [[texture(9)]],
                      array<texture2d<float>, 16> maps [[texture(10)]])
{
   const uint2 px = uint2(in.pos.xy);
   uint seed = pcg(px.x + 8192u * px.y) ^ pcg(uint(u.info.z) * 2654435761u + 17u);
   const float H = float(prev.get_height());
   const float2 ndc = float2(2.0 * (float(px.x) + rnd(seed)) / u.info.w - 1.0, 2.0 * (float(px.y) + rnd(seed)) / H - 1.0);
   float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   float3 o = f.eye.xyz;
   float3 d = normalize(pw.xyz / pw.w - o);
   if (u.lens.x > 0.0)
   {
      // Depth of field: a point on the aperture (round, or a polygon of lens.z blades turned by lens.w, stretched by
      // the ratio as Render 3D's bokeh); the ray still meets the focus plane, so it stays sharp there.
      const float3 fp = o + d * (u.lens.y / max(dot(d, u.lf.xyz), 1e-4));
      const float2 lu = float2(rnd(seed), rnd(seed));
      float2 off;
      if (u.lens.z >= 3.0)
      {
         const float seg = 6.2831853 / u.lens.z, a0 = u.lens.w + floor(rnd(seed) * u.lens.z) * seg;
         off = sqrt(lu.x) * mix(float2(cos(a0), sin(a0)), float2(cos(a0 + seg), sin(a0 + seg)), lu.y);
      }
      else
         off = sqrt(lu.x) * float2(cos(6.2831853 * lu.y), sin(6.2831853 * lu.y));
      off *= u.lr.w >= 1.0 ? float2(1.0 / u.lr.w, 1.0) : float2(1.0, u.lr.w);
      o += (off.x * u.lr.xyz + off.y * u.lu.xyz) * u.lens.x;
      d = normalize(fp - o);
   }
   float3 L = float3(0.0), T = float3(1.0);
   float cov = 0.0;
   const int maxB = int(u.info.y);
   const int nEm = int(u.em.x); const float pEm = u.em.y;
   float lastPdfB = 0.0; // pdf of the bounce that led here (MIS against emitter sampling)
   float3 skyK = float3(1.0); // sky scale for the bounce that led here: Render 3D's ambient on the diffuse lobe, 1 on the glossy one (as the raster)
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   int skips = 0; // see-through hits passed this sample
   for (int b = 0; b < maxB; b++)
   {
      ray r(o, d, 0.0, 1e4);
      intersection_result<triangle_data, instancing> h = it.intersect(r, rtArgs.tlas);
      if (h.type != intersection_type::triangle)
      {
         if (b > 0) L += T * sampleEnv(f, et, d, 0.0) * skyK; // the sky itself; the cosine is in the sampling
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
      const bool backHit = dot(n, d) > 0.0;
      if (backHit) n = -n;
      const float3 p = o + d * h.distance;
      float3 base = I.bm.xyz; float metal = clamp(I.bm.w, 0.0, 1.0); float rough = clamp(I.rg.x, 0.045, 1.0);
      const float3 ns = n; // the surface's own side: ray origins and the light / bounce gates use it, not the mapped normal
      float3 emMap = float3(1.0); float shW = I.sh.x, ccW = I.msc.w;
      if (any(I.tx > 0.5) || any(I.tx2 > 0.5) || any(I.tx3 > 0.5))
      {
         // Maps at the hit, through the mapping: full detail on the camera hit (the jittered samples filter it), a
         // blurred level after a bounce. Normal maps take the triangle's uv tangent frame.
         float2 t0 = float2(0.0), t1 = float2(0.0), t2 = float2(0.0);
         if (I.uvb != 0)
         {
            const device float* uvp = reinterpret_cast<const device float*>(I.uvb);
            t0 = float2(uvp[ix.x * 2], uvp[ix.x * 2 + 1]); t1 = float2(uvp[ix.y * 2], uvp[ix.y * 2 + 1]); t2 = float2(uvp[ix.z * 2], uvp[ix.z * 2 + 1]);
         }
         const float2 uv = t0 * w3.x + t1 * w3.y + t2 * w3.z;
         const float3 p0 = float3(vb[ix.x * 6], vb[ix.x * 6 + 1], vb[ix.x * 6 + 2]);
         const float3 q1 = float3(vb[ix.y * 6], vb[ix.y * 6 + 1], vb[ix.y * 6 + 2]), q2 = float3(vb[ix.z * 6], vb[ix.z * 6 + 1], vb[ix.z * 6 + 2]);
         const float3 lp = p0 * w3.x + q1 * w3.y + q2 * w3.z;
         const float lod = b == 0 ? 0.0 : 3.0;
         const int wrapM = int(I.sh.w + 0.5) & 3;
         const MapX mx = mapX(I.mp0, I.msc, I.mro, I.mtr, I.olo, I.ohi, wrapM);
         const float3 mb = mapBase(mx, uv, lp);
#define MAPT(slot) mapSample(maps[int(slot) - 1], mx, mb, n, float3(0.0), float3(0.0), lod)
         if (I.tx.x > 0.5) base *= mapLin(MAPT(I.tx.x).rgb);
         if (I.tx.y > 0.5) rough = clamp(I.rg.x * MAPT(I.tx.y).r, 0.045, 1.0);
         if (I.tx.z > 0.5) metal = clamp(I.bm.w * MAPT(I.tx.z).r, 0.0, 1.0);
         if (I.tx2.y > 0.5) emMap = mapLin(MAPT(I.tx2.y).rgb);
         if (I.tx2.z > 0.5) shW *= MAPT(I.tx2.z).r;
         if (I.tx3.x > 0.5) ccW *= MAPT(I.tx3.x).r;
         float alpha = 1.0 - I.mp0.w;
         if (I.tx2.w > 0.5) alpha *= MAPT(I.tx2.w).r;
#undef MAPT
         if (alpha < 1.0 && skips < 16 && alpha < (I.mp0.z > 0.0 ? I.mp0.z : rnd(seed)))
         {
            // Cut out or not covered: the ray goes on through, the same bounce.
            skips++; if (b == 0) cov = 0.0;
            o = p + d * f.shQ.w; b--; continue;
         }
         if (I.tx.w > 0.5)
         {
            const float3 e1 = q1 - p0, e2 = q2 - p0;
            const float2 mu0 = mapUv(mx, mapBase(mx, t0, p0), n), mu1 = mapUv(mx, mapBase(mx, t1, q1), n), mu2 = mapUv(mx, mapBase(mx, t2, q2), n);
            const float2 d1 = mu1 - mu0, d2 = mu2 - mu0;
            const float det = d1.x * d2.y - d2.x * d1.y;
            if (abs(det) > 1e-12)
            {
               float3 tg = normalize((I.normalM * float4((e1 * d2.y - e2 * d1.y) / det, 0.0)).xyz);
               const float3 bg = (I.normalM * float4((e2 * d1.x - e1 * d2.x) / det, 0.0)).xyz;
               tg = normalize(tg - n * dot(n, tg));
               float3 bt = cross(n, tg);
               if (dot(bt, bg) < 0.0) bt = -bt;
               if (backHit) { tg = -tg; bt = -bt; } // the flipped side mirrors the frame, as the raster's cotangent frame does
               const float3 s = mapNormalTS(maps[int(I.tx.w) - 1], mx, (int(I.sh.w + 0.5) & 4) != 0, I.sh.z, mapUv(mx, mb, n),
                                            float2(0.0), float2(0.0), lod, I.ohi.w, I.dt.x);
               n = normalize(tg * s.x + bt * s.y + n * s.z);
            }
         }
      }
      if (any(I.em.xyz > 0.0))
      {
         // Emission seen along the path. After a bounce, emitter sampling below could have found it too: power-heuristic MIS.
         float wE = 1.0;
         if (b > 0 && nEm > 0 && I.em.w >= 0.0)
         {
            const EmTri t = ems[int(I.em.w) + int(h.primitive_id)];
            const float3 gn = cross(t.e1.xyz, t.e2.xyz);
            const float cosL = abs(dot(gn, d)) / max(length(gn), 1e-12);
            const float pL = pEm * t.e1.w * h.distance * h.distance / max(cosL, 1e-6);
            wE = lastPdfB * lastPdfB / max(lastPdfB * lastPdfB + pL * pL, 1e-20);
         }
         L += T * I.em.xyz * emMap * wE;
      }
      const float3 V = -d; const float nDotV = max(dot(n, V), 1e-4);
      const float shMax = shW * maxc(I.shc.xyz), shV = shMax > 0.0 ? 1.0 - shMax * sheenE(nDotV, I.sh.y) : 1.0;
      const float3 f0 = mix(float3(0.04 + I.mtr.w), base, metal);
      if (b == 0) L += base * pow(1.0 - nDotV, 3.0) * f.eye.w; // Render 3D's rim, as the raster, so settling keeps the look
      for (int i = 0; i < int(f.params.w) && i < 8; i++) // ambient lights: the raster's flat, unshadowed fill, so settling keeps it
         if (int(f.lights[i].vec.w) == 3) L += T * base * (1.0 - metal) * f.lights[i].color.xyz * f.lights[i].terms.x;
      // direct light, the same model as the raster
      const int count = int(f.params.w);
      // next-event estimation: an emissive triangle (chance pEm, picked by power) or one light, picked uniformly and
      // weighted by the count and the chance of taking this branch (unbiased). Both branches only set up the sample;
      // one shadow ray and one BSDF evaluation below serve either (keeps the kernel small).
      float3 ld = float3(0.0, 1.0, 0.0), Le = float3(0.0); float tmax = 0.0, pL = -1.0; float2 lterms = float2(1.0);
      if (nEm > 0 && rnd(seed) < pEm)
      {
         const float r = rnd(seed);
         int lo = 0, hi = nEm - 1;
         while (lo < hi) { const int mid = (lo + hi) / 2; if (ems[mid].p0.w < r) lo = mid + 1; else hi = mid; }
         const EmTri t = ems[lo];
         const float su = sqrt(rnd(seed)), u2 = rnd(seed);
         const float3 tl = t.p0.xyz + t.e1.xyz * (su * (1.0 - u2)) + t.e2.xyz * (su * u2) - p;
         const float dist = length(tl); ld = tl / max(dist, 1e-6);
         const float3 gn = cross(t.e1.xyz, t.e2.xyz);
         const float cosL = abs(dot(gn, ld)) / max(length(gn), 1e-12);
         if (cosL > 1e-4 && t.e1.w > 0.0)
         {
            pL = pEm * t.e1.w * dist * dist / cosL; // solid-angle pdf of this sample
            Le = t.em.xyz; tmax = max(dist * (1.0 - 1e-3) - f.shQ.w, 0.0);
         }
      }
      else if (count > 0)
      {
         constant LightU& Lt = f.lights[min(int(rnd(seed) * float(count)), count - 1)];
         const int type = int(Lt.vec.w);
         float atten = type == 3 ? 0.0 : 1.0; // ambient arrives through the bounces
         tmax = 1e4;
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
            atten *= lightFalloff(dist, Lt.terms.z);
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
         else
         {
            ld = normalize(Lt.vec.xyz);
            if ((type == 0 || type == 2) && Lt.spot.x > 0.0)
            {
               // A sun disc: a uniform direction in its cone (radiance spread so the irradiance stays the light's own).
               const float cm = cos(Lt.spot.x * 0.5 * 0.017453292);
               const float ct = 1.0 - rnd(seed) * (1.0 - cm), st = sqrt(max(1.0 - ct * ct, 0.0)), ph = 6.28318530718 * rnd(seed);
               const float3 ax = abs(ld.y) < 0.99 ? normalize(cross(ld, float3(0.0, 1.0, 0.0))) : float3(1.0, 0.0, 0.0);
               ld = normalize(ld * ct + (ax * cos(ph) + cross(ld, ax) * sin(ph)) * st);
            }
         }
         Le = Lt.color.xyz * atten * (float(count) / max(1.0 - pEm, 1e-3));
         lterms = Lt.terms.xy;
      }
      if (dot(n, ld) > 1e-5 && dot(ns, ld) > 0.0 && any(Le > 0.0))
      {
         // Lights keep the live picture's shadow strength, so settling does not darken shadows the user already sees;
         // emitters are surfaces, fully blocked.
         if (occludedAlpha(rtArgs.tlas, inst, maps, p + ns * f.shQ.w, ld, tmax, f.shQ.w, seed)) Le *= pL > 0.0 ? 0.0 : 1.0 - f.shP.w;
         float pB = 0.0;
         float3 fe = ptEval(n, V, ld, base, metal, rough, f0, lterms, pB);
         if (shW > 0.0 || ccW > 0.0)
         {
            // Sheen and clearcoat on the light sample (the raster's lobes); bounces see the base BSDF.
            const float nL = max(dot(n, ld), 0.0), nH = max(dot(n, normalize(V + ld)), 0.0), vH = max(dot(V, normalize(V + ld)), 0.0);
            const float cF = 0.04 + 0.96 * pow(1.0 - nDotV, 5.0);
            fe *= 1.0 - ccW * cF;
            if (shW > 0.0)
               fe = fe * shV
                  + I.shc.xyz * sheenLtc(nL, nDotV, dot(ld, V), I.sh.y) * shW * nL * lterms.y;
            if (ccW > 0.0) fe += ccW * coatSpec(nH, nDotV, max(nL, 1e-4), vH, clamp(I.mro.w, 0.02, 1.0)) * nL * lterms.y;
         }
         L += T * fe * Le * (pL > 0.0 ? pL / max(pL * pL + pB * pB, 1e-20) : 1.0); // emitters: power-heuristic MIS
      }
      // next direction: one lobe picked by Render 3D's lobe weights, weighted by the full BSDF over the mixture pdf
      const float2 lp = ptProbs(f0, metal, nDotV);
      const float a = max(0.002, rough * rough);
      // The clearcoat is a layer on top: it takes the bounce with its Fresnel share (pC), the base the rest; the
      // base's (1 - pC) attenuation and that choice cancel, so the base branch weighs as before.
      const float ccR = clamp(I.mro.w, 0.02, 1.0);
      const float pC = ccW > 0.0 ? clamp(ccW * (0.04 + 0.96 * pow(1.0 - nDotV, 5.0)), 0.0, 0.95) : 0.0;
      const bool coatB = pC > 0.0 && rnd(seed) < pC;
      float3 nd;
      if (coatB) { nd = reflect(d, ggxSampleH(n, ccR * ccR, rnd(seed), rnd(seed))); skyK = float3(1.0); }
      else if (rnd(seed) < lp.x) { nd = reflect(d, ggxSampleH(n, a, rnd(seed), rnd(seed))); skyK = float3(1.0); }
      else { nd = cosineDir(n, rnd(seed), rnd(seed)); skyK = f.ambient.xyz * 3.0; }
      float pdfB = 0.0;
      float3 fB = ptEval(n, V, nd, base, metal, rough, f0, float2(1.0), pdfB);
      float pdfC = 0.0;
      const float cnL = dot(n, nd);
      if (pC > 0.0 && cnL > 0.0)
      {
         const float3 hc = normalize(V + nd);
         const float cnH = max(dot(n, hc), 0.0), cvH = max(dot(V, hc), 1e-4);
         pdfC = distGGX(cnH, ccR) * cnH / (4.0 * cvH);
      }
      if (coatB)
      {
         if (pdfC <= 1e-6 || dot(ns, nd) <= 0.0) break;
         const float3 hc = normalize(V + nd);
         const float cw = ccW * coatSpec(max(dot(n, hc), 0.0), nDotV, max(cnL, 1e-4), max(dot(V, hc), 1e-4), ccR) * cnL / (pC * pdfC);
         T *= min(cw, 4.0);
         lastPdfB = pC * pdfC + (1.0 - pC) * pdfB;
      }
      else
      {
         if (pdfB <= 1e-6 || dot(ns, nd) <= 0.0) break; // a mapped normal can send the bounce under the surface
         if (shW > 0.0) // the bounce sees the sheen too (the cosine half of the mixture covers its wide lobe)
         {
            const float nL = max(dot(n, nd), 0.0);
            fB = fB * shV
               + I.shc.xyz * sheenLtc(nL, nDotV, dot(nd, V), I.sh.y) * shW * nL;
         }
         T *= fB / pdfB;
         lastPdfB = pC * pdfC + (1.0 - pC) * pdfB;
      }
      if (b >= 2)
      {
         const float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
         if (rnd(seed) > q) break;
         T /= q;
      }
      o = p + ns * f.shQ.w; d = nd;
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
   col = toneMap(col, int(u.p.y));
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
   float3 c = (m.base.xyz * (f.ambient.xyz * 1.4 + lc * max(dot(n, ld), 0.0)) + m.em.xyz) * f.params.x;
   c = toneMap(c, int(f.params.y));
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
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m * float4(n, 0.0)).xyz; o.uv = float2(0.0); o.lp = p; return o;
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
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m * float4(n, 0.0)).xyz; o.uv = float2(0.0); o.lp = p; return o; // uniform scale: the model's 3x3 turns normals
}
// Runs: consecutive instances sharing a mesh and material drawn in one instanced call; per-copy data from a buffer.
vertex VOut rr_vs(uint vid [[vertex_id]], uint iid [[instance_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                  const device InstU* ms [[buffer(10)]], constant uint& first [[buffer(11)]], const device float* uvs [[buffer(12)]])
{
   const device InstU& m = ms[first + iid];
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4 w = m.model * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   c.y = -c.y;
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m.normalM * float4(n, 0.0)).xyz; o.uv = float2(uvs[vid * 2], uvs[vid * 2 + 1]); o.lp = p; return o;
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
   c = toneMap(c, int(f.params.y));
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
   // The rest of the material (InstU / RInst tx2 .. dt in the shader).
   struct MapExtH { float tx2[4], tx3[4], mp0[4], msc[4], mro[4], mtr[4], olo[4], ohi[4], dt[4]; };
   struct InstU { float model[16], normalM[16], base[4], mr[4], em[4], tx[4], sh[4], shc[4]; MapExtH x; };
   struct RInstH { float bm[4], rg[4], em[4]; uint64_t vb, ib; float normalM[16]; uint64_t uvb, pad0; float tx[4], sh[4], shc[4]; MapExtH x; };
   static_assert(sizeof(InstU) == 368 && sizeof(RInstH) == 336, "InstU / RInst layout as the shader's");
   // Encoded so all zeros is the plain material (batches leave it empty): opacity as 1 - opacity, F0 less 0.04.
   void MapExt(const engine::Instance& in, MapExtH& x)
   {
      x.tx2[0] = (float)(in.mapAo + 1); x.tx2[1] = (float)(in.mapEmission + 1); x.tx2[2] = (float)(in.mapSheen + 1); x.tx2[3] = (float)(in.mapOpacity + 1);
      x.tx3[0] = (float)(in.mapClearcoat + 1); x.tx3[1] = (float)(in.mapTransmission + 1);
      x.mp0[0] = (float)in.mapSpace; x.mp0[1] = in.triplanar; x.mp0[2] = in.alphaCutoff; x.mp0[3] = 1.0f - std::min(std::max(in.opacity, 0.0f), 1.0f);
      const float r = (in.ior - 1.0f) / (in.ior + 1.0f);
      const float f0 = std::min(std::max(r * r * 2.0f * in.specular, 0.0f), 1.0f);
      for (int k = 0; k < 3; k++)
      {
         x.msc[k] = in.mapScale[k]; x.mro[k] = in.mapRotate[k]; x.mtr[k] = in.mapTranslate[k];
         x.olo[k] = in.objLo[k]; x.ohi[k] = in.objHi[k];
      }
      x.msc[3] = in.clearcoat; x.mro[3] = in.clearcoatRoughness; x.mtr[3] = f0 - 0.04f;
      x.olo[3] = in.breakup; x.ohi[3] = in.detailScale; x.dt[0] = in.detailStrength;
   }
   // A run the depth prepass leaves out: it cuts pixels away, so its depth is known only once it is shaded.
   bool SeeThrough(const InstU& u) { return u.x.mp0[3] > 0.0f || u.x.tx2[3] > 0.5f || u.x.mp0[2] > 0.0f; }
   // The maps of an instance as InstU / RInst carry them (index + 1, 0 = none).
   void MapFields(const engine::Instance& in, float tx[4], float sh[4])
   {
      tx[0] = (float)(in.mapAlbedo + 1); tx[1] = (float)(in.mapRoughness + 1); tx[2] = (float)(in.mapMetallic + 1); tx[3] = (float)(in.mapNormal + 1);
      sh[0] = in.sheen; sh[1] = in.sheenRoughness; sh[2] = in.normalStrength; sh[3] = (float)((in.wrap & 3) + (in.normalBump ? 4 : 0));
   }

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
      struct Gpu { id<MTLBuffer> vb = nil, ib = nil, uvb = nil; NSUInteger count = 0; float lo[3], hi[3]; };
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
            cur.uvb = UvBuffer(m);
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
      // Two floats per vertex (zeros when the mesh has none), read by rr_vs and the path tracer.
      id<MTLBuffer> UvBuffer(const engine::MeshData& m)
      {
         const size_t nv = m.posNormal.size() / 6;
         const size_t len = std::max<size_t>(nv * 8, 8);
         if (m.uv.size() == nv * 2 && nv > 0)
            return [mDev newBufferWithBytes:m.uv.data() length:len options:MTLResourceStorageModeShared];
         id<MTLBuffer> b = [mDev newBufferWithLength:len options:MTLResourceStorageModeShared];
         std::memset(b.contents, 0, len);
         return b;
      }
      // Material maps: RGBA8 with a full mip chain, cached by TextureData::key; the frame's 16 slots (texture 10..25),
      // unused ones a 1x1 white. mMapMean: each slot's mean linear colour, for the ray passes' hit albedo.
      struct MapTex { unsigned long long key = 0; id<MTLTexture> tex = nil; float mean[3] = { 1, 1, 1 }; bool used = false; };
      std::vector<MapTex> mMapCache;
      id<MTLTexture> mMapSlot[engine::kMaxTextures] = {};
      float mMapMean[engine::kMaxTextures][3] = {};
      id<MTLTexture> mWhite = nil;
      void PrepareMaps(id<MTLCommandBuffer> cb, const engine::Frame& fr)
      {
         if (!mWhite)
         {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
            mWhite = [mDev newTextureWithDescriptor:d];
            const uint8_t w[4] = { 255, 255, 255, 255 };
            [mWhite replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:w bytesPerRow:4];
         }
         for (auto& c : mMapCache) c.used = false;
         id<MTLBlitCommandEncoder> blit = nil;
         for (int i = 0; i < engine::kMaxTextures; i++)
         {
            mMapSlot[i] = mWhite;
            mMapMean[i][0] = mMapMean[i][1] = mMapMean[i][2] = 1.0f;
            if (i >= (int)fr.textures.size())
               continue;
            const engine::TextureData& t = fr.textures[(size_t)i];
            if (!t.rgba || t.w <= 0 || t.h <= 0)
               continue;
            MapTex* c = nullptr;
            for (auto& e : mMapCache)
               if (e.key == t.key) { c = &e; break; }
            if (!c)
            {
               MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:(NSUInteger)t.w height:(NSUInteger)t.h mipmapped:YES];
               d.usage = MTLTextureUsageShaderRead;
               id<MTLTexture> tex = [mDev newTextureWithDescriptor:d];
               if (!tex)
                  continue;
               [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)t.w, (NSUInteger)t.h) mipmapLevel:0 withBytes:t.rgba bytesPerRow:(NSUInteger)t.w * 4];
               if (!blit) blit = [cb blitCommandEncoder];
               [blit generateMipmapsForTexture:tex];
               MapTex e;
               e.key = t.key; e.tex = tex;
               double acc[3] = { 0, 0, 0 }; size_t n = 0;
               const size_t step = std::max<size_t>(1, ((size_t)t.w * t.h) / 65536);
               for (size_t k = 0; k < (size_t)t.w * t.h; k += step, n++)
                  for (int ch = 0; ch < 3; ch++)
                     acc[ch] += ToLin(t.rgba[k * 4 + ch] / 255.0f);
               for (int ch = 0; ch < 3; ch++)
                  e.mean[ch] = n ? (float)(acc[ch] / n) : 1.0f;
               mMapCache.push_back(e);
               c = &mMapCache.back();
            }
            c->used = true;
            mMapSlot[i] = c->tex;
            std::memcpy(mMapMean[i], c->mean, 12);
         }
         if (blit) [blit endEncoding];
         mMapCache.erase(std::remove_if(mMapCache.begin(), mMapCache.end(), [](const MapTex& e) { return !e.used; }), mMapCache.end());
      }
      void BindMaps(id<MTLRenderCommandEncoder> e)
      {
         [e setFragmentTextures:mMapSlot withRange:NSMakeRange(10, engine::kMaxTextures)];
      }
      bool MakeGpu(const engine::MeshData& m, Gpu& g)
      {
         g.vb = [mDev newBufferWithBytes:m.posNormal.data() length:std::max<size_t>(m.posNormal.size() * 4, 4) options:MTLResourceStorageModeShared];
         g.ib = [mDev newBufferWithBytes:m.indices.data() length:std::max<size_t>(m.indices.size() * 4, 4) options:MTLResourceStorageModeShared];
         g.uvb = UvBuffer(m);
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
      id<MTLBuffer> mInfoBuf = nil, mEmBuf = nil;
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
            if (l.type == engine::LightType::Directional || l.type == engine::LightType::Sun) o.spot[0] = l.sunAngleDeg; // the disc, for these
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
         PrepareMaps(cb, fr);
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
            const InstU* preU = (const InstU*)mInstBuf.contents;
            for (const auto& r : mRuns)
            {
               if (SeeThrough(preU[r.first]))
                  continue;
               const Gpu& g = mMeshes[fr.instances[r.first].mesh];
               [ze setVertexBuffer:g.vb offset:0 atIndex:0];
               [ze setVertexBuffer:g.uvb offset:0 atIndex:12];
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
         pass.depthAttachment.storeAction = fr.dofAperture > 0.0f ? MTLStoreActionStore : MTLStoreActionDontCare; // the DOF pass reads it
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
         std::vector<RInstH> info(fr.instances.size() + 1);
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            RInstH& o = info[i]; o = {};
            Lin3(in.baseSrgb, o.bm); o.bm[3] = in.metallic; o.rg[0] = in.roughness; std::memcpy(o.em, in.emission, 12);
            std::memcpy(o.normalM, in.normalMatrix, 64);
            if (in.mapAlbedo >= 0 && in.mapAlbedo < engine::kMaxTextures) // the rays see the map's mean colour
               for (int k = 0; k < 3; k++) o.bm[k] *= mMapMean[in.mapAlbedo][k];
            if (in.mesh >= 0 && in.mesh < (int)mMeshes.size())
            {
               o.vb = mMeshes[in.mesh].vb.gpuAddress; o.ib = mMeshes[in.mesh].ib.gpuAddress;
            }
         }
         id<MTLBuffer> infoBuf = InfoBuffer(info);
         [enc setFragmentBuffer:infoBuf offset:0 atIndex:4];
         [enc setFragmentTexture:mEnvBound atIndex:9];
         BindMaps(enc);
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
         auto drawRun = [&](const std::pair<uint32_t, uint32_t>& r)
         {
            const Gpu& g = mMeshes[fr.instances[r.first].mesh];
            [enc setFragmentBytes:&runU[r.first] length:sizeof(InstU) atIndex:2];   // the run's material
            [enc setVertexBuffer:g.vb offset:0 atIndex:0];
            [enc setVertexBuffer:g.uvb offset:0 atIndex:12];
            [enc setVertexBytes:&r.first length:4 atIndex:11];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib
                         indexBufferOffset:0 instanceCount:r.second];
         };
         for (const auto& r : mRuns)
            if (!prepass || !SeeThrough(runU[r.first]))
               drawRun(r);
         if (prepass)
         {
            // See-through runs were left out of the prepass: drawn after it with the plain depth test.
            [enc setRenderPipelineState:mrt ? mPsoRun : mPsoRunDir];
            [enc setDepthStencilState:mDs];
            for (const auto& r : mRuns)
               if (SeeThrough(runU[r.first]))
                  drawRun(r);
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
         DepthOfField(cb, img, fr, fmt);
         [cb commit];
         [cb waitUntilCompleted];
         if (gpuMs)
            *gpuMs = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
         return cb.status == MTLCommandBufferStatusCompleted;
      }

      // Live depth of field on the finished picture (dof_fs): copy it, then blur the copy back into it by depth.
      void DepthOfField(id<MTLCommandBuffer> cb, MetalImage& img, const engine::Frame& fr, MTLPixelFormat fmt)
      {
         float inv[16];
         if (fr.dofAperture <= 0.0f || !Inv4(fr.viewProj, inv))
            return;
         if (!mDofPso || mDofFmt != fmt)
         {
            MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
            d.fragmentFunction = [mLib newFunctionWithName:@"dof_fs"];
            d.colorAttachments[0].pixelFormat = fmt;
            NSError* err = nil;
            mDofPso = [mDev newRenderPipelineStateWithDescriptor:d error:&err];
            mDofFmt = fmt;
            if (!mDofPso) { NSLog(@"dof pipeline: %@", err); return; }
         }
         const int w = img.Width(), h = img.Height();
         if (!mDofSrc || (int)mDofSrc.width != w || (int)mDofSrc.height != h || mDofSrc.pixelFormat != fmt)
         {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead; d.storageMode = MTLStorageModePrivate;
            mDofSrc = [mDev newTextureWithDescriptor:d];
            if (!mDofSrc) return;
         }
         id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
         [be copyFromTexture:img.Tex() toTexture:mDofSrc];
         [be endEncoding];
         MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
         rp.colorAttachments[0].texture = img.Tex();
         rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
         rp.colorAttachments[0].storeAction = MTLStoreActionStore;
         id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
         struct { float invVP[16]; float info[4]; float lens[4]; float eye[4]; } u;
         std::memcpy(u.invVP, inv, 64);
         u.info[0] = (float)w; u.info[1] = (float)h; u.info[2] = fr.dofAperture * 12.0f * (float)h / 1080.0f; // 24 px is the circle's diameter
         u.info[3] = std::max(2.0f, 0.03f * (float)h);
         u.lens[0] = fr.focusDist; u.lens[1] = fr.bokeh[0]; u.lens[2] = fr.bokeh[1]; u.lens[3] = fr.bokeh[2];
         u.eye[0] = fr.eye[0]; u.eye[1] = fr.eye[1]; u.eye[2] = fr.eye[2]; u.eye[3] = 0.0f;
         [e setRenderPipelineState:mDofPso];
         [e setFragmentBytes:&u length:sizeof(u) atIndex:0];
         [e setFragmentTexture:mDofSrc atIndex:0];
         [e setFragmentTexture:mDepth atIndex:1];
         [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
         [e endEncoding];
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
         f.shP[3] = fr.shadow.strength; // the live picture's shadow strength, so settling keeps the same shadows
         float inv[16];
         if (!Inv4(fr.viewProj, inv))
            return false;
         if (reset) mPtN = 0;
         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         PrepareMaps(cb, fr);
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
         std::vector<RInstH> info(fr.instances.size() + 1);
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            RInstH& o = info[i]; o = {};
            Lin3(in.baseSrgb, o.bm); o.bm[3] = in.metallic; o.rg[0] = in.roughness; std::memcpy(o.em, in.emission, 12);
            std::memcpy(o.normalM, in.normalMatrix, 64);
            MapFields(in, o.tx, o.sh);
            std::memcpy(o.shc, in.sheenColor, 12);
            MapExt(in, o.x);
            if (in.mesh >= 0 && in.mesh < (int)mMeshes.size())
            {
               o.vb = mMeshes[in.mesh].vb.gpuAddress; o.ib = mMeshes[in.mesh].ib.gpuAddress; o.uvb = mMeshes[in.mesh].uvb.gpuAddress;
            }
         }
         // Emissive triangles in world space for next-event estimation, chosen by power (luminance * area). The
         // instance's em[3] points at its first triangle; primitive_id indexes from there. Capped so a huge glowing
         // mesh does not stall the frame (past the cap it is still found by bounces).
         struct EmTriH { float p0[4], e1[4], e2[4], em[4]; };
         std::vector<EmTriH> ems;
         double emTotal = 0.0;
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            info[i].em[3] = -1.0f;
            const float lum = 0.2126f * in.emission[0] + 0.7152f * in.emission[1] + 0.0722f * in.emission[2];
            if (lum <= 0.0f || in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            const Gpu& g = mMeshes[in.mesh];
            const size_t tris = g.count / 3;
            if (tris == 0 || ems.size() + tris > 65536)
               continue;
            info[i].em[3] = (float)ems.size();
            const float* vb = (const float*)g.vb.contents;
            const uint32_t* ib = (const uint32_t*)g.ib.contents;
            const float* m = in.model;
            auto world = [&](uint32_t v, float* o) {
               const float* a = vb + (size_t)v * 6;
               for (int k = 0; k < 3; k++) o[k] = m[k] * a[0] + m[4 + k] * a[1] + m[8 + k] * a[2] + m[12 + k];
            };
            for (size_t t = 0; t < tris; t++)
            {
               EmTriH e = {};
               float a[3], b[3], c[3];
               world(ib[t * 3], a); world(ib[t * 3 + 1], b); world(ib[t * 3 + 2], c);
               for (int k = 0; k < 3; k++) { e.p0[k] = a[k]; e.e1[k] = b[k] - a[k]; e.e2[k] = c[k] - a[k]; e.em[k] = in.emission[k]; }
               const float cx = e.e1[1] * e.e2[2] - e.e1[2] * e.e2[1], cy = e.e1[2] * e.e2[0] - e.e1[0] * e.e2[2], cz = e.e1[0] * e.e2[1] - e.e1[1] * e.e2[0];
               const float area = 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
               e.e2[3] = area;
               emTotal += (double)area * lum;
               e.p0[3] = (float)emTotal; // cumulative, normalised below
               ems.push_back(e);
            }
         }
         if (emTotal > 0.0)
            for (size_t k = 0; k < ems.size(); k++)
            {
               const double prevC = k == 0 ? 0.0 : ems[k - 1].p0[3] * emTotal; // ems[k-1] already normalised
               const double pw = (double)ems[k].p0[3] - prevC;
               ems[k].p0[3] = (float)(ems[k].p0[3] / emTotal);
               ems[k].e1[3] = ems[k].e2[3] > 0.0f ? (float)(pw / emTotal / ems[k].e2[3]) : 0.0f;
            }
         else
            ems.clear();
         if (!ems.empty()) ems.back().p0[3] = 1.0f;
         int analytic = 0;
         for (const engine::Light& l : fr.lights) analytic += l.type != engine::LightType::Ambient ? 1 : 0;
         const float pEm = ems.empty() ? 0.0f : (analytic > 0 ? 0.5f : 1.0f);
         const NSUInteger emLen = std::max<NSUInteger>(ems.size() * sizeof(EmTriH), 64);
         if (!mEmBuf || mEmBuf.length < emLen)
            mEmBuf = [mDev newBufferWithLength:emLen * 2 options:MTLResourceStorageModeShared];
         if (!ems.empty()) std::memcpy(mEmBuf.contents, ems.data(), ems.size() * sizeof(EmTriH));
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
            struct { float invVP[16]; float info[4]; float em[4]; float lens[4], lr[4], lu[4], lf[4]; } pu;
            pu.lens[0] = fr.lensRadius; pu.lens[1] = fr.focusDist; pu.lens[2] = fr.bokeh[0]; pu.lens[3] = fr.bokeh[1];
            for (int k = 0; k < 3; k++) { pu.lr[k] = fr.camRight[k]; pu.lu[k] = fr.camUp[k]; pu.lf[k] = fr.camFwd[k]; }
            pu.lr[3] = fr.bokeh[2]; pu.lu[3] = pu.lf[3] = 0.0f;
            std::memcpy(pu.invVP, inv, 64);
            pu.info[0] = (float)mPtN; pu.info[1] = (float)std::max(1, bounces); pu.info[2] = (float)(mPtFrame++); pu.info[3] = (float)img.Width();
            pu.em[0] = (float)ems.size(); pu.em[1] = pEm; pu.em[2] = 0.0f; pu.em[3] = 0.0f;
            [e setFragmentBytes:&pu length:sizeof(pu) atIndex:0];
            [e setFragmentBytes:&f length:sizeof(f) atIndex:1];
            [e setFragmentBuffer:mArgBuf offset:0 atIndex:3];
            [e setFragmentBuffer:infoBuf offset:0 atIndex:4];
            [e setFragmentBuffer:mEmBuf offset:0 atIndex:5];
            [e setFragmentTexture:mEnvBound atIndex:9];
            [e setFragmentTexture:mPtAcc[mPtCur] atIndex:0];
            BindMaps(e);
            [e useResource:mTlas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            for (const Gpu& g : mMeshes) { [e useResource:g.vb usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; [e useResource:g.ib usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; if (g.uvb) [e useResource:g.uvb usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; }
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
      id<MTLRenderPipelineState> mDofPso = nil;
      MTLPixelFormat mDofFmt = MTLPixelFormatInvalid;
      id<MTLTexture> mDofSrc = nil;
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
            std::memcpy(u.em, in.emission, 12);
            MapFields(in, u.tx, u.sh);
            std::memcpy(u.shc, in.sheenColor, 12);
            MapExt(in, u.x);
            if (in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            if (!mRuns.empty())
            {
               auto& r = mRuns.back();
               const engine::Instance& a = fr.instances[r.first];
               if (r.first + r.second == i && a.mesh == in.mesh && std::memcmp(dst[r.first].base, u.base, sizeof(InstU) - offsetof(InstU, base)) == 0)
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

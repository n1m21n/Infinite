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
struct LightU { float4 vec; float4 aim; float4 color; float4 terms; float4 spot; }; // terms: diffuse, specular, falloff, cutoff
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
   LightU lights[8]; // vec.w = type, aim.w unused
};
struct InstU { float4x4 model; float4x4 normalM; float4 base; float4 mr; }; // base linear rgb; mr = metallic, roughness
struct VOut { float4 pos [[position]]; float3 wp; float3 n; };

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

float3 sampleEnv(constant FrameU& f, float3 dir, float rough)
{
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
// One-bounce reflection: closest hit -> vertex normal, Lambert from every light (no shadows), ambient, env spec. Miss -> env.
float3 traceReflection(constant FrameU& f, instance_acceleration_structure tlas, constant RInst* inst, float3 o, float3 d, float rough, bool giMode = false)
{
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   ray r(o, d, 0.0, 1e4);
   intersection_result<triangle_data, instancing> h = it.intersect(r, tlas);
   if (h.type != intersection_type::triangle) return giMode ? sampleEnv(f, d, 1.0) * f.ambient.xyz * 3.0 : sampleEnv(f, d, rough);
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
   float3 diff = sampleEnv(f, n, 1.0) * f.ambient.xyz * 3.0;
   const int count = int(f.params.w);
   for (int i = 0; i < 8; i++)
   {
      if (i >= count) break;
      constant LightU& L = f.lights[i];
      const int type = int(L.vec.w);
      if (type == 3) { diff += L.color.xyz * L.terms.x; continue; }
      float3 ld; float atten = 1.0;
      if (type == 1 || type == 4)
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
      }
      else ld = normalize(L.vec.xyz);
      if (atten > 0.0 && !giMode)
      {
         const float tm = (type == 1 || type == 4) ? max(length(L.vec.xyz - p) - 0.01, 0.0) : 1e4;
         if (occluded(tlas, p + n * f.shQ.w, ld, tm)) atten *= (1.0 - f.shP.w);
      }
      diff += L.color.xyz * atten * max(dot(n, ld), 0.0) * L.terms.x;
   }
   const float3 spec = sampleEnv(f, reflect(d, n), I.rg.x) * metal * base;
   return base * (1.0 - metal) * diff / 3.14159265 * 3.0 + spec;
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
struct FOut { float4 c0 [[color(0)]]; float4 c1 [[color(1), function_constant(kMrt)]]; float4 c2 [[color(2), function_constant(kMrt)]]; };
float3 aces(float3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
float3 toSrgb(float3 c) { c = max(c, float3(0.0)); return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c < float3(0.0031308)); }

fragment FOut r_fs(VOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant InstU& m [[buffer(2)]],
                     depth2d<float> sm [[texture(0)]], constant RTArgs& rtArgs [[buffer(3)]],
                     constant RInst* instInfo [[buffer(4)]])
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
      float3 lightDir; float atten = 1.0;
      if (type == 1 || type == 4)
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
      if (nDotL <= 0.0) continue;
      const float nDotH = max(dot(n, halfway), 0.0);
      const float d = distGGX(nDotH, rough);
      const float g = geomSchlick(nDotV, rough) * geomSchlick(nDotL, rough);
      const float3 fr = fresnel(max(dot(halfway, viewDir), 0.0), f0);
      const float3 spec = (d * g * fr) / max(4.0 * nDotV * nDotL, 1e-7);
      const float3 kD = (float3(1.0) - fr) * (1.0 - metal);
      const float3 radiance = radiance0 * atten;
      floorBounce += radiance * max(lightDir.y, 0.0) * L.terms.x;
      float shadow = (f.shP.x > 0.5 && i == int(f.shP.y)) ? shadowFactor(f, sm, in.wp, n, lightDir) : 1.0;
      if ((int(f.shQ.z + 0.5) & 1) != 0)
      {
         const float tmax = (type == 1 || type == 4) ? max(length(L.vec.xyz - in.wp) - 0.01, 0.0) : 1e4;
         if (occluded(rtArgs.tlas, in.wp + n * f.shQ.w, lightDir, tmax)) shadow = 1.0 - f.shP.w;
      }
      col += (kD * base / 3.14159265 * L.terms.x + spec * L.terms.y) * radiance * nDotL * shadow;
   }
   col += base / 3.14159265 * floorBounce * (0.25 * (1.0 - n.y)) * (1.0 - metal);
   const float3 reflectDir = reflect(-viewDir, n);
   const float3 irradiance = sampleEnv(f, n, 1.0) * f.ambient.xyz * 3.0;
   float3 reflection = sampleEnv(f, reflectDir, rough);
   float reflTraced = 0.0;
   if ((int(f.shQ.z + 0.5) & 2) != 0 && rough < 0.55)
   {
      uint rs = pcg(uint(in.pos.x) * 1973u + 9277u * uint(in.pos.y)) ^ pcg(uint(f.gi.z) * 26699u + 7u);
      float3 jd = reflectDir;
      if (rough > 0.04)
      {
         const float3 v = cosineDir(reflectDir, rnd(rs), rnd(rs)); // lobe around the mirror direction
         jd = normalize(mix(reflectDir, v, clamp(rough * 1.4, 0.0, 1.0)));
         if (dot(jd, n) < 0.02) jd = normalize(jd + n * (0.02 - dot(jd, n)));
      }
      const float3 rc = traceReflection(f, rtArgs.tlas, instInfo, in.wp + n * f.shQ.w, jd, rough);
      reflection = mix(rc, reflection, smoothstep(0.3, 0.55, rough));
      reflTraced = 1.0;
   }
   const float3 fAmb = fresnelRough(nDotV, f0, rough);
   float3 giOut = float3(0.0);
   const bool giOn = f.gi.x > 0.5 && (int(f.shQ.z + 0.5) >= 0);
   const bool giSample = giOn && ((uint(in.pos.x) & 1u) + 2u * (uint(in.pos.y) & 1u)) == (uint(f.gi.z) & 3u); // 1 of every 2x2 per frame
   if (giSample)
   {
      uint seed = pcg(uint(in.pos.x) + 8192u * uint(in.pos.y)) ^ pcg(uint(f.gi.z) * 9781u + 1u);
      const int rays = max(int(f.gi.w), 1);
      float3 acc = float3(0.0);
      for (int k = 0; k < rays; k++)
      {
         const float3 dir = cosineDir(n, rnd(seed), rnd(seed));
         acc += traceReflection(f, rtArgs.tlas, instInfo, in.wp + n * f.shQ.w, dir, 1.0, true);
      }
      giOut = (float3(1.0) - fAmb) * (1.0 - metal) * base * acc / float(rays) * f.gi.y;
   }
   else if (!giOn) col += (float3(1.0) - fAmb) * (1.0 - metal) * base * irradiance;
   const float3 reflOut = reflection * fAmb;
   col += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
   FOut o;
   if (!kMrt)
   {
      float3 fin = (col + reflOut) * f.params.x;
      if (int(f.params.y) == 1) fin = aces(fin);
      o.c0 = float4(toSrgb(fin), 1.0);
      return o;
   }
   o.c2 = float4(reflOut * f.params.x, reflTraced);
   o.c0 = float4(col * f.params.x, 1.0); o.c1 = float4(giOut * f.params.x, giSample ? 1.0 : 0.0);
   return o;
}

struct ResU { float4 p; float4 clear; }; // p: blend alpha, tonemap, history valid
struct ResOut { float4 c0 [[color(0)]]; float4 c1 [[color(1)]]; float4 c2 [[color(2)]]; };
fragment ResOut res_fs(SkyOut in [[stage_in]], constant ResU& u [[buffer(0)]], texture2d<float> hdr [[texture(0)]], texture2d<float> giT [[texture(1)]], texture2d<float> hist [[texture(2)]], texture2d<float> rfl [[texture(3)]], texture2d<float> rhist [[texture(4)]])
{
   const uint2 px = uint2(in.pos.xy);
   const float4 c = hdr.read(px);
   ResOut o;
   if (c.a < 0.5) { o.c0 = float4(u.clear.xyz, 1.0); o.c1 = float4(0.0); o.c2 = float4(0.0); return o; }
   float3 g = float3(0.0); float wsum = 0.0;
   for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++)
   {
      const int2 q = int2(px) + int2(x, y);
      if (q.x < 0 || q.y < 0 || q.x >= int(hdr.get_width()) || q.y >= int(hdr.get_height())) continue;
      if (hdr.read(uint2(q)).a < 0.5) continue;
      const float4 gs = giT.read(uint2(q)); if (gs.a < 0.5) continue;
      g += gs.rgb; wsum += 1.0;
   }
   g /= max(wsum, 1.0);
   const float3 h = hist.read(px).rgb;
   const float3 acc = wsum > 0.5 ? (u.p.z > 0.5 ? mix(h, g, u.p.x) : g) : float3(0.0); // no sampled neighbour (GI off) -> no GI, never stale history
   const float4 rc = rfl.read(px);
   const float3 rh = rhist.read(px).rgb;
   const float3 racc = (rc.a > 0.5 && u.clear.w > 0.5) ? mix(rh, rc.rgb, u.p.w) : rc.rgb;
   float3 col = c.rgb + acc + racc;
   if (int(u.p.y) == 1) col = aces(col);
   o.c0 = float4(toSrgb(col), 1.0);
   o.c1 = float4(acc, 1.0);
   o.c2 = float4(racc, 1.0);
   return o;
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
fragment float4 pt_fs(SkyOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant PtU& u [[buffer(0)]],
                      constant RTArgs& rtArgs [[buffer(3)]], constant RInst* inst [[buffer(4)]], texture2d<float> prev [[texture(0)]])
{
   const uint2 px = uint2(in.pos.xy);
   uint seed = pcg(px.x + 8192u * px.y) ^ pcg(uint(u.info.z) * 2654435761u + 17u);
   const float H = float(prev.get_height());
   const float2 ndc = float2(2.0 * (float(px.x) + rnd(seed)) / u.info.w - 1.0, 2.0 * (float(px.y) + rnd(seed)) / H - 1.0);
   float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   float3 o = f.eye.xyz;
   float3 d = normalize(pw.xyz / pw.w - o);
   float3 L = float3(0.0), T = float3(1.0);
   float cov = 0.0; bool specBounce = true;
   const int maxB = int(u.info.y);
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   for (int b = 0; b < maxB; b++)
   {
      ray r(o, d, 0.0, 1e4);
      intersection_result<triangle_data, instancing> h = it.intersect(r, rtArgs.tlas);
      if (h.type != intersection_type::triangle)
      {
         if (b > 0) L += T * (specBounce ? sampleEnv(f, d, 0.0) : sampleEnv(f, d, 1.0) * f.ambient.xyz * 3.0);
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
         if (type == 1 || type == 4)
         {
            const float3 tl = Lt.vec.xyz - p; const float dist = length(tl);
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
         }
         else ld = normalize(Lt.vec.xyz);
         float nDotL = max(dot(n, ld), 0.0);
         if (type == 2) nDotL = max((dot(n, ld) + 0.35) / 1.35, 0.0);
         if (nDotL <= 0.0 || atten <= 0.0) continue;
         if (occluded(rtArgs.tlas, p + n * f.shQ.w, ld, tmax)) continue;
         const float3 hv = normalize(ld + V);
         const float nDotH = max(dot(n, hv), 0.0);
         const float g = geomSchlick(nDotV, rough) * geomSchlick(nDotL, rough);
         const float3 fr = fresnel(max(dot(hv, V), 0.0), f0);
         const float3 spec = (distGGX(nDotH, rough) * g * fr) / max(4.0 * nDotV * nDotL, 1e-7);
         const float3 kD = (float3(1.0) - fr) * (1.0 - metal);
         L += T * (kD * base / 3.14159265 * Lt.terms.x + spec * Lt.terms.y) * Lt.color.xyz * atten * nDotL * lw;
      }
      // next direction
      const float3 F = fresnel(nDotV, f0);
      const float ps = metal > 0.99 ? 1.0 : clamp(mix(dot(F, float3(0.3333)), 1.0, metal), 0.1, 0.9);
      float3 nd;
      if (rnd(seed) < ps)
      {
         const float a = rough * rough;
         const float3 hv = ggxSampleH(n, a, rnd(seed), rnd(seed));
         nd = reflect(d, hv);
         const float nDotL = dot(n, nd);
         if (nDotL <= 1e-4) break;
         const float vDotH = max(dot(V, hv), 1e-4), nDotH = max(dot(n, hv), 1e-4);
         const float g = geomSchlick(nDotV, rough) * geomSchlick(nDotL, rough);
         T *= fresnel(vDotH, f0) * g * vDotH / (nDotV * nDotH) / ps;
         specBounce = true;
      }
      else
      {
         nd = cosineDir(n, rnd(seed), rnd(seed));
         T *= (float3(1.0) - F) * (1.0 - metal) * base / (1.0 - ps);
         specBounce = false;
      }
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

struct SOut { float4 pos [[position]]; };
vertex SOut sh_vs(uint vid [[vertex_id]], const device float* v [[buffer(0)]], constant float4x4& lvp [[buffer(1)]], constant InstU& m [[buffer(2)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   float4 c = lvp * (m.model * float4(p, 1.0));
   c.z = (c.z + c.w) * 0.5;
   SOut o; o.pos = c; return o;
}

vertex SkyOut sky_vs(uint vid [[vertex_id]]) { const float2 p[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) }; SkyOut o; o.pos = float4(p[vid], 1.0, 1.0); return o; }
fragment float4 sky_fs(SkyOut in [[stage_in]], constant float4& clearSrgb [[buffer(0)]]) { return float4(clearSrgb.xyz, 1.0); }
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
      float viewProj[16], eye[4], ambient[4], sky[4], horizon[4], ground[4], params[4], lightVP[16], shP[4], shQ[4], gi[4];
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
      bool UploadMeshes(const std::vector<engine::MeshData>& meshes) override
      {
         mMeshes.clear();
         for (const engine::MeshData& m : meshes)
         {
            Gpu g;
            g.vb = [mDev newBufferWithBytes:m.posNormal.data() length:m.posNormal.size() * 4 options:MTLResourceStorageModeShared];
            g.ib = [mDev newBufferWithBytes:m.indices.data() length:m.indices.size() * 4 options:MTLResourceStorageModeShared];
            g.count = (NSUInteger)m.indices.size();
            for (int k = 0; k < 3; k++) { g.lo[k] = 1e30f; g.hi[k] = -1e30f; }
            for (size_t i = 0; i + 5 < m.posNormal.size(); i += 6)
               for (int k = 0; k < 3; k++)
               {
                  g.lo[k] = std::min(g.lo[k], m.posNormal[i + k]);
                  g.hi[k] = std::max(g.hi[k], m.posNormal[i + k]);
               }
            if (!g.vb || !g.ib)
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
         for (const Gpu& g : mMeshes)
         {
            MTLAccelerationStructureTriangleGeometryDescriptor* gd = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
            gd.vertexBuffer = g.vb; gd.vertexStride = 24;
            gd.indexBuffer = g.ib; gd.indexType = MTLIndexTypeUInt32;
            gd.triangleCount = g.count / 3;
            gd.opaque = YES;
            MTLPrimitiveAccelerationStructureDescriptor* ad = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
            ad.geometryDescriptors = @[ gd ];
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
         for (int i = 0; i < n; i++)
         {
            const engine::Light& l = fr.lights[i];
            LightU& o = f.lights[i];
            std::memcpy(o.vec, l.vec, 12); o.vec[3] = (float)(int)l.type;
            std::memcpy(o.aim, l.aim, 12);
            Lin3(l.colorSrgb, o.color, l.intensity);
            o.terms[0] = l.diffuse; o.terms[1] = l.specular; o.terms[2] = l.falloff; o.terms[3] = l.cutoff;
            o.spot[0] = l.spotAngleDeg; o.spot[1] = l.spotPenumbra;
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

         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         const bool haveTlas = BuildTlas(cb, fr);
         const bool rt = fr.rtShadows && haveTlas;
         const bool rtRefl = fr.rtReflections && haveTlas;
         if (rt || !ShadowPass(cb, fr, f))
            EnsureShadowTex(4);
         const bool giOn = fr.gi && haveTlas;
         f.gi[0] = giOn ? 1.0f : 0.0f; f.gi[1] = fr.giStrength; f.gi[2] = (float)fr.frameIndex; f.gi[3] = (float)fr.giRays;
         f.shQ[2] = (rt ? 1.0f : 0.0f) + (rtRefl ? 2.0f : 0.0f); f.shQ[3] = fr.rayBias;
         if (rt) { f.shP[1] = 0; f.shP[0] = 0; f.shP[3] = fr.shadow.strength; }
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
         }
         pass.depthAttachment.texture = mDepth;
         pass.depthAttachment.loadAction = MTLLoadActionClear;
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
         {
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
            [enc setFragmentBytes:info.data() length:info.size() * sizeof(RInstH) atIndex:4];
         }
         for (const engine::Instance& in : fr.instances)
         {
            if (in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            InstU u = {};
            std::memcpy(u.model, in.model, 64);
            std::memcpy(u.normalM, in.normalMatrix, 64);
            Lin3(in.baseSrgb, u.base);
            u.mr[0] = in.metallic; u.mr[1] = in.roughness;
            [enc setVertexBytes:&u length:sizeof(u) atIndex:2];
            [enc setFragmentBytes:&u length:sizeof(u) atIndex:2];
            const Gpu& g = mMeshes[in.mesh];
            [enc setVertexBuffer:g.vb offset:0 atIndex:0];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib indexBufferOffset:0];
         }
         [enc endEncoding];
         if (!mrt) { mHistValid = false; mRValid = false; }
         else
         {
            const bool moved = std::memcmp(mLastVP, fr.viewProj, 64) != 0;
            std::memcpy(mLastVP, fr.viewProj, 64);
            const int prev = mHistCur, cur = 1 - mHistCur;
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
            float ru[8] = { moved ? 0.6f : fr.giBlend, (float)fr.tonemap, mHistValid ? 1.0f : 0.0f, moved ? 0.7f : 0.12f, 0, 0, 0, 0 };
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
            [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [re endEncoding];
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
            [e setFragmentBytes:info.data() length:info.size() * sizeof(RInstH) atIndex:4];
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
   public:
   private:
      struct Gpu { id<MTLBuffer> vb = nil, ib = nil; NSUInteger count = 0; float lo[3], hi[3]; };

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
         if (!sh.enabled || fr.lights.empty() || fr.lights[0].type == engine::LightType::Point ||
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
         for (const engine::Instance& in : fr.instances)
         {
            if (in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            InstU u2 = {};
            std::memcpy(u2.model, in.model, 64);
            [enc setVertexBytes:&u2 length:sizeof(u2) atIndex:2];
            const Gpu& g = mMeshes[in.mesh];
            [enc setVertexBuffer:g.vb offset:0 atIndex:0];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib indexBufferOffset:0];
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
         MTLRenderPipelineDescriptor* dd2 = [MTLRenderPipelineDescriptor new];
         dd2.vertexFunction = pd.vertexFunction;
         dd2.fragmentFunction = fsDir;
         dd2.colorAttachments[0].pixelFormat = fmt;
         dd2.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mPsoDir = [mDev newRenderPipelineStateWithDescriptor:dd2 error:&err];
         if (!mPsoDir) NSLog(@"direct pipeline: %@", err);
         if (!mResPso) NSLog(@"resolve pipeline: %@", err);
         MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
         dd.depthCompareFunction = MTLCompareFunctionLess;
         dd.depthWriteEnabled = YES;
         mDs = [mDev newDepthStencilStateWithDescriptor:dd];
         mFmt = fmt;
         MTLRenderPipelineDescriptor* sd = [MTLRenderPipelineDescriptor new];
         sd.vertexFunction = [mLib newFunctionWithName:@"sh_vs"];
         sd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mShPso = [mDev newRenderPipelineStateWithDescriptor:sd error:&err];
         return mPso != nil && mPsoDir != nil && mDs != nil && mShPso != nil && mResPso != nil;
      }
      bool Depth(int w, int h)
      {
         if (mDepth && (int)mDepth.width == w && (int)mDepth.height == h)
            return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget;
         d.storageMode = MTLStorageModePrivate;
         mDepth = [mDev newTextureWithDescriptor:d];
         return mDepth != nil;
      }
      id<MTLDevice> mDev;
      id<MTLCommandQueue> mQueue;
      id<MTLLibrary> mLib = nil;
      id<MTLRenderPipelineState> mPso = nil;
      id<MTLDepthStencilState> mDs = nil;
      id<MTLTexture> mDepth = nil;
      id<MTLTexture> mShadowTex = nil;
      NSMutableArray<id<MTLAccelerationStructure>>* mBlas = nil;
      id<MTLAccelerationStructure> mTlas = nil;
      id<MTLBuffer> mTlasScratch = nil;
      id<MTLFunction> mFsFn = nil;
      id<MTLArgumentEncoder> mArgEnc = nil;
      id<MTLBuffer> mArgBuf = nil;
      id<MTLAccelerationStructure> mArgTlas = nil;
      id<MTLRenderPipelineState> mShPso = nil;
      id<MTLRenderPipelineState> mResPso = nil, mPsoDir = nil;
      id<MTLTexture> mHdr = nil, mGiT = nil, mRefl = nil, mHist[2] = { nil, nil }, mRHist[2] = { nil, nil };
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
         mHistValid = false; mRValid = false;
         return mHdr && mGiT && mHist[0] && mHist[1] && mRefl && mRHist[0] && mRHist[1];
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

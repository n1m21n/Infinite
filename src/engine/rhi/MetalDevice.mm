// Metal backend of the engine RHI. See Rhi.h.
#include "engine/rhi/Rhi.h"
#include "nodes/Render3DHair.h"
#include "nodes/Render3DSheen.h"
#include "nodes/Render3DThinFilm.h"
#include "nodes/Render3DTonemap.h"
#include "nodes/Render3DMaterial.h"
#include "nodes/Render3DDenoise.h"
#include "nodes/Render3DBsdf.h"
#include "nodes/Render3DFresnel.h"
#include "nodes/Render3DAb.h"
#include "field/MaterialRegistry.h"
#include "engine/LightClusters.h"

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
      std::string gLastError;
      // Compile macros plus the active A/B switches (Render3DAb.h).
      NSDictionary* AbMacros(NSDictionary* base)
      {
         NSMutableDictionary* d = [NSMutableDictionary dictionaryWithDictionary:base ?: @{}];
         for (const std::string& m : R3dAbMacros())
            d[[NSString stringWithUTF8String:m.c_str()]] = @1;
         return d;
      }
      // Logs a failed shader or pipeline and keeps the first one for the node: what failed plus the compiler's
      // first error line (the full text goes to the log).
      // The compiler's first error line, at most 160 chars.
      std::string FirstError(NSError* err)
      {
         std::string msg = err ? std::string([[err localizedDescription] UTF8String] ?: "") : std::string("not built");
         const size_t e = msg.find("error:");
         if (e != std::string::npos)
         {
            const size_t ls = msg.rfind('\n', e);
            msg = msg.substr(ls == std::string::npos ? 0 : ls + 1);
         }
         msg = msg.substr(0, msg.find('\n'));
         if (msg.size() > 160)
            msg = msg.substr(0, 157) + "...";
         return msg;
      }
      void EngineFail(NSString* what, NSError* err)
      {
         NSLog(@"%@: %@", what, err);
         if (!gLastError.empty())
            return;
         gLastError = std::string([what UTF8String]) + ": " + FirstError(err);
      }
   }
   const std::string& LastError() { return gLastError; }
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
// Hair: Render3DHair.h's Chiang fibre BSDF (shared with GL).
typedef float3 vec3;
typedef float4 vec4;
typedef float3x3 mat3;
#define HAIR_ATAN2(y, x) atan2(y, x)
#define HAIR_OUT thread float&
)" R3D_HAIR_SRC R"(
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
   float4 fld;      // x: the Field material programs' t
};
)" R3D_MAT_MSL R"(
//@FLD_MAT_LIB@
#ifndef FLD_MAT_ANY
#define FLD_MAT_ANY 0
#endif
struct InstU { float4x4 model; float4x4 normalM; MatRec mat; }; // the material: Render3DMaterial.h (all zeros = plain)
// INV_POS: the second, invariant compile used by the depth prepass and the shaded runs after it (exact same depth);
// plain raster keeps the faster non-invariant build.
#ifdef INV_POS
#define VPOS [[position, invariant]]
#else
#define VPOS [[position]]
#endif
struct VOut { float4 pos VPOS; float3 wp; float3 n; float2 uv; float3 lp; float3 cw; }; // cw: curvature, cavity (row 4.4), attr (row 3.3)

vertex VOut r_vs(uint vid [[vertex_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                 constant InstU& m [[buffer(2)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4 w = m.model * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5; // GL clip depth -1..1 to Metal 0..1
   c.y = -c.y;              // the shared image is read by GL, whose row 0 is the bottom
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m.normalM * float4(n, 0.0)).xyz; o.uv = float2(0.0); o.lp = p; o.cw = float3(0.0); return o;
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
)" R3D_BSDF_SRC R3D_FRESNEL_SRC R3D_THINFILM_SRC R"(
// Thin film over the specular lobes (Render3DThinFilm.h, shared with GL): a gain on the lobe, at the view angle.
float3 iridGain(float w, float ior, float thick, float nV, float3 f0)
{
   return w > 0.0 ? thinFilmGain(nV, ior, thick, f0, w) : float3(1.0);
}
#ifdef R3D_AB_GGX
#define R3D_G2(nv, nl, r) ggxG2(nv, nl, r)
#else
#define R3D_G2(nv, nl, r) (geomSchlick(nv, r) * geomSchlick(nl, r))
#endif
float3 fresnel(float cosT, float3 f0) { return fresnelExact(cosT, f0); }
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
   intersection_result<triangle_data, instancing> h = it.intersect(r, tlas, 1); // bit 0: shadow casters
   return h.type == intersection_type::triangle;
}
// A sun's shadow as its disc (angleDeg across, as settle's cone and Cycles' sun angle): four probe rays first; only
// where they disagree (the penumbra) twelve more on a golden-angle spiral, turned and jittered per
// pixel by a hash so the thin sampling reads as fine grain, not bands. 0 = fully blocked, 1 = open.
uint pcg(uint v);
float rnd(thread uint& s);
struct RInst;
float shadowT(instance_acceleration_structure tlas, constant RInst* inst, bool thru, float3 o, float3 d, float tmax);
// thru (row 5.2): the receiver is a strand, so other strands pass light (see shadowT); inst may be null when false.
float sunVis(instance_acceleration_structure tlas, constant RInst* inst, bool thru, float3 o, float3 ld, float angleDeg, float2 pix)
{
   if (angleDeg <= 0.0) return shadowT(tlas, inst, thru, o, ld, 1e4);
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
      v += shadowT(tlas, inst, thru, o, normalize(ld + (ax * cos(an) + ay * sin(an)) * (0.9 * tr)), 1e4);
   }
   if (v == 0.0 || v == 4.0) return v * 0.25;
   for (int i = 0; i < 12; i++)
   {
      const float an = rot + 0.7853982 + float(i) * 2.399963, r = sqrt((float(i) + rnd(s)) / 12.0) * tr;
      v += shadowT(tlas, inst, thru, o, normalize(ld + (ax * cos(an) + ay * sin(an)) * r), 1e4);
   }
   return v / 16.0;
}
struct RInst { ulong vb; ulong ib; ulong uvb; ulong wr; float4x4 normalM; MatRec mat; }; // the rays' view: mesh buffers + the same record (base times the albedo map's mean on GI rays)
// Row 5.2: a shadow ray's visibility. Light reaching a strand passes the other strands by exp(-0.7) each, re-traced
// from every strand hit up to 4 (FINISH_PLAN 6.12; the forward scatter of Zinke's dual scattering, which Cycles gets
// from many bounces through bright fibres); a strand still blocks fully what lies under it (skin, floor), as in Cycles.
float shadowT(instance_acceleration_structure tlas, constant RInst* inst, bool thru, float3 o, float3 d, float tmax)
{
   if (!occluded(tlas, o, d, tmax)) return 1.0;
   if (!thru) return 0.0;
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   float T = 1.0, t0 = 0.0;
   for (int k = 0; k < 4; k++)
   {
      ray r(o, d, t0, tmax);
      intersection_result<triangle_data, instancing> h = it.intersect(r, tlas, 1);
      if (h.type != intersection_type::triangle) return T;
      constant RInst& I = inst[h.instance_id];
      if (I.mat.strandFrom < 0.5 || float(h.primitive_id) < I.mat.strandFrom - 1.0) return 0.0;
      T *= 0.49658530; // exp(-0.7)
      t0 = h.distance + 1e-4;
   }
   return T;
}
// A strand's sun: one ray per pixel in the disc (hashed per pixel and frame), through shadowT; the 16-ray penumbra
// search times a 4-hit re-trace over every hair pixel is what the live frame cannot afford (FINISH_PLAN 6.12).
float sunVisStrand(instance_acceleration_structure tlas, constant RInst* inst, float3 o, float3 ld, float angleDeg, float2 pix, float frame)
{
   if (angleDeg <= 0.0) return shadowT(tlas, inst, true, o, ld, 1e4);
   const float tr = tan(angleDeg * 0.5 * 0.017453292);
   const float3 ax = abs(ld.y) < 0.99 ? normalize(cross(ld, float3(0.0, 1.0, 0.0))) : float3(1.0, 0.0, 0.0);
   const float3 ay = cross(ld, ax);
   uint sd = pcg(uint(pix.x) * 7919u + 104729u * uint(pix.y) + 31u * uint(frame));
   const float an = 6.2831853 * rnd(sd), r = sqrt(rnd(sd)) * tr;
   return shadowT(tlas, inst, true, o, normalize(ld + (ax * cos(an) + ay * sin(an)) * r), 1e4);
}
float3 cosineDir(float3 n, float u1, float u2);
// One hit: closest hit -> vertex normal, Lambert from every light (RT shadowed), ambient, env spec. Miss -> env.
// hp/hn/alb return the hit (alb = diffuse albedo, 0 on a miss) so GI can take one more bounce from it.
float3 shadeHit(constant FrameU& f, texture2d<float> et, instance_acceleration_structure tlas, constant RInst* inst, array<texture2d<float>, 16> maps, float3 o, float3 d, float rough,
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
   const bool hairH = I.mat.hairRadialNeg < 0.0 && float(h.primitive_id) >= I.mat.hairFrom;
   float3 base = hairH ? hairAlbedo(float3(I.mat.hairSigma), -I.mat.hairRadialNeg) : float3(I.mat.base); float metal = hairH ? 0.0 : I.mat.metallic;
   float hitRough = I.mat.roughness; float3 hitEm = float3(I.mat.emission);
#if FLD_MAT_ANY
   // Field material program (row 1.4): reflection and GI rays see what the camera sees. Base, metallic, roughness and
   // emission at the hit's local position and mesh uv; its normal, height and cut stay with the camera hit.
   if (abs(I.mat.program) > 0.5 && !hairH)
   {
      float2 uv = float2(0.0);
      if (I.uvb != 0)
      {
         const device float* uvp = reinterpret_cast<const device float*>(I.uvb);
         uv = float2(uvp[ix.x * 2], uvp[ix.x * 2 + 1]) * w.x + float2(uvp[ix.y * 2], uvp[ix.y * 2 + 1]) * w.y + float2(uvp[ix.z * 2], uvp[ix.z * 2 + 1]) * w.z;
      }
      const float3 lp = float3(vb[ix.x * 6], vb[ix.x * 6 + 1], vb[ix.x * 6 + 2]) * w.x + float3(vb[ix.y * 6], vb[ix.y * 6 + 1], vb[ix.y * 6 + 2]) * w.y
                      + float3(vb[ix.z * 6], vb[ix.z * 6 + 1], vb[ix.z * 6 + 2]) * w.z;
      FldMat fo;
      fo.base = base; fo.roughness = hitRough; fo.metallic = metal; fo.normal = float3(0.0, 0.0, 1.0); fo.height = 0.0;
      fo.emission = hitEm; fo.opacity = 1.0; fo.mask = 1.0;
      fo.prm0 = float4(I.mat.p0, I.mat.p1, I.mat.p2, I.mat.p3); fo.prm1 = float4(I.mat.p4, I.mat.p5, I.mat.p6, I.mat.p7); fo.prm2 = float4(I.mat.p8, I.mat.p9, I.mat.p10, I.mat.p11); fo.prm3 = float4(I.mat.p12, I.mat.p13, I.mat.p14, I.mat.p15);
      float3 fw = float3(0.0);
      if (I.wr != 0)
      {
         const device float* wp2 = reinterpret_cast<const device float*>(I.wr);
         fw = float3(wp2[ix.x * 3], wp2[ix.x * 3 + 1], wp2[ix.x * 3 + 2]) * w.x + float3(wp2[ix.y * 3], wp2[ix.y * 3 + 1], wp2[ix.y * 3 + 2]) * w.y + float3(wp2[ix.z * 3], wp2[ix.z * 3 + 1], wp2[ix.z * 3 + 2]) * w.z;
      }
      fldMatDispatch(int(abs(I.mat.program) + 0.5), fo, lp, n, uv, f.fld.x, p, -d, float4(0.0, 0.0, fw.x, fw.y), float4(fw.z, 0.0, 0.0, 0.0), maps, float4(I.mat.fimg0, I.mat.fimg1, I.mat.fimg2, I.mat.fimg3), 2.0);
      base = max(fo.base, float3(0.0)); metal = clamp(fo.metallic, 0.0, 1.0); hitRough = clamp(fo.roughness, 0.045, 1.0); hitEm = fo.emission;
   }
#endif
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
         if (L.color.w < 0.5 && occluded(tlas, p + n * f.shQ.w, ld, tm)) atten *= (1.0 - f.shP.w);
      }
      diff += L.color.xyz * atten * max(dot(n, ld), 0.0) * L.terms.x * lw;
   }
   const float3 spec = sampleEnv(f, et, reflect(d, n), hitRough) * metal * base;
   hp = p; hn = n; alb = base * (1.0 - metal); mir = base * metal;
   // Lambert radiance: base/pi * light irradiance (r_fs's direct term), env as r_fs's ambient.
   // withEnv false: the caller's next bounce carries the env; withSpec false: the caller follows the mirror direction.
   return alb * ((withEnv ? envIrr : float3(0.0)) + diff / 3.14159265) + (withSpec ? spec : float3(0.0)) + hitEm;
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
float3 traceReflection(constant FrameU& f, texture2d<float> et, instance_acceleration_structure tlas, constant RInst* inst, array<texture2d<float>, 16> maps, float3 o, float3 d, float rough, thread uint& seed,
                       texture2d<float> nz, texture2d<float> hist, bool hv, float3 dMirror = float3(0.0))
{
   float3 hp, hn, alb, mir;
   const bool bounce = f.gi.x > 0.5;
   float3 L = shadeHit(f, et, tlas, inst, maps, o, d, rough, false, hp, hn, alb, mir, !bounce, -1, true, float2(-1.0), dMirror);
   float3 irr;
   if (any(alb > 0.0) && bounce && hv && histAt(f, nz, hist, hp, hn, irr))
      L += alb * irr * f.gi.y;
   else if (any(alb > 0.0) && bounce)
   {
      float3 hp2, hn2, alb2, mir2;
      const float3 d2 = cosineDir(hn, rnd(seed), rnd(seed));
      const int pk = int(rnd(seed) * f.params.w);
      const float3 L2 = shadeHit(f, et, tlas, inst, maps, hp + hn * f.shQ.w, d2, 1.0, true, hp2, hn2, alb2, mir2, true, pk, true, float2(rnd(seed), rnd(seed)));
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
// Tonemaps and sRGB (Render3DTonemap.h, shared with GL), same order as the node: 1 ACES, 2 Reinhard, 3 AgX, 4 Neutral.
)" R3D_TONEMAP_SRC R3D_SRGB_SRC R"(
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
MapX mapX(MatRec m)
{
   MapX x; x.space = int(m.mapSpace + 0.5); x.tri = m.triplanar; x.sc = float3(m.mapScale); x.ro = float3(m.mapRotate); x.tr = float3(m.mapTranslate);
   x.lo = float3(m.boxLo); x.hi = float3(m.boxHi); x.brk = m.breakup; x.wrap = int(m.wrapBump + 0.5) & 3; return x;
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
// Row 4.2: parallax occlusion on a bump (grey height) map. Height 1 is the surface, 0 the deepest; a full step is
// 4 x strength texels deep, the same relief the bump normal reads, so the two agree. The gradients are the
// unshifted uv's, so mips do not jump at the steps.
float pomH(texture2d<float> t, MapX x, float2 uv, float2 gx, float2 gy)
{
   return dot(mapTap(t, x, uv, gx, gy, -1.0).rgb, float3(0.2126, 0.7152, 0.0722));
}
float2 pomMarch(texture2d<float> t, MapX x, float2 uv, float2 gx, float2 gy, float3 vTS, float scale, thread float& hOut)
{
   const float ns = mix(32.0, 8.0, abs(vTS.z)), dl = 1.0 / ns; // more steps at grazing
   const float2 dUv = -vTS.xy / max(vTS.z, 0.1) * scale / ns;
   float layer = 1.0, hc = pomH(t, x, uv, gx, gy), hp = hc;
   float2 c = uv, cp = uv;
   for (int i = 0; i < 32 && hc < layer; i++) { cp = c; hp = hc; c += dUv; layer -= dl; hc = pomH(t, x, c, gx, gy); }
   const float a = hc - layer, b = hp - (layer + dl); // secant between the last two layers
   const float w = a - b > 1e-6 ? a / (a - b) : 0.0;
   hOut = layer + w * dl;
   return mix(c, cp, w);
}
// Light blocked by the relief between the hit and the top: 8 steps toward the light.
float pomShadow(texture2d<float> t, MapX x, float2 c, float h, float2 gx, float2 gy, float3 lTS, float scale)
{
   if (lTS.z <= 0.0 || h >= 0.999) return 1.0;
   const float2 dUv = lTS.xy / max(lTS.z, 0.1) * scale * (1.0 - h) / 8.0;
   float m = 0.0;
   for (int i = 1; i <= 8; i++) m = max(m, pomH(t, x, c + dUv * float(i), gx, gy) - (h + (1.0 - h) * float(i) / 8.0));
   return 1.0 - smoothstep(0.0, 0.05, m);
}
float coatSpec(float nDotH, float nDotV, float nDotL, float vDotH, float r)
{
   return distGGX(nDotH, r) * R3D_G2(nDotV, nDotL, r) * (0.04 + 0.96 * pow(1.0 - vDotH, 5.0)) / max(4.0 * nDotV * nDotL, 1e-7);
}
// Sheen: Cycles' microfiber lobe (Render3DSheen.h, shared with GL).
)" R3D_SHEEN_MSL R3D_DENOISE_MSL R"(
float maxc(float3 c) { return max(c.x, max(c.y, c.z)); }
// Hair: the settle sampler for Render3DHair.h's Chiang fibre BSDF (spliced in at the top).
// Draws wi for hairBsdf at h: a lobe by its share of Ap, its tilted longitudinal lobe, then its azimuthal logistic.
float3 hairSample(float3 wo, float3 T, float3 sa, float bm, float bn, float alpha, float eta, float h, float4 u)
{
   const float sto = clamp(dot(wo, T), -1.0, 1.0), cto = sqrt(max(1.0 - sto * sto, 0.0));
   const float3 X = hairX(wo, T, sto), Y = cross(T, X);
   const float stt = sto / eta, ctt = sqrt(max(1.0 - stt * stt, 1e-4));
   const float etap = sqrt(max(eta * eta - sto * sto, 0.0)) / max(cto, 1e-4);
   const float sgt = clamp(h / etap, -1.0, 1.0), cgt = sqrt(max(1.0 - sgt * sgt, 0.0));
   const float go = asin(clamp(h, -1.0, 1.0)), gt = asin(sgt);
   const float3 tr = exp(-sa * (2.0 * cgt / ctt));
   const float fr = hairFresnel(cto * sqrt(max(1.0 - h * h, 0.0)), eta);
   const float3 a0 = float3(fr), a1 = (1.0 - fr) * (1.0 - fr) * tr, a2 = a1 * tr * fr;
   const float3 a3 = a2 * tr * fr / max(float3(1.0) - tr * fr, float3(1e-4));
   const float3 lw = float3(0.2126, 0.7152, 0.0722);
   float4 ap = float4(dot(a0, lw), dot(a1, lw), dot(a2, lw), dot(a3, lw)); ap /= max(ap.x + ap.y + ap.z + ap.w, 1e-6);
   const int p = u.x < ap.x ? 0 : u.x < ap.x + ap.y ? 1 : u.x < ap.x + ap.y + ap.z ? 2 : 3;
   const float v0 = hairV0(bm), s = hairS(bn);
   float v = p == 0 ? v0 : p == 1 ? 0.25 * v0 : 4.0 * v0;
   const float ta = p == 0 ? -2.0 * alpha : p == 1 ? alpha : p == 2 ? 4.0 * alpha : 0.0;
   const float stp = sto * cos(ta) + cto * sin(ta), ctp = abs(cto * cos(ta) - sto * sin(ta));
   const float u1 = max(u.y, 1e-5);
   const float ct = 1.0 + v * log(u1 + (1.0 - u1) * exp(-2.0 / v)), st = sqrt(max(1.0 - ct * ct, 0.0));
   const float sti = clamp(-ct * stp + st * cos(6.2831853 * u.z) * ctp, -1.0, 1.0), cti = sqrt(max(1.0 - sti * sti, 0.0));
   float phi;
   if (p < 3)
   {
      const float lo = hairCdf(-3.14159265, s), k = hairCdf(3.14159265, s) - lo;
      const float x = -s * log(1.0 / max(u.w * k + lo, 1e-7) - 1.0);
      phi = hairPhi(float(p), go, gt) + clamp(x, -3.14159265, 3.14159265);
   }
   else phi = 6.2831853 * u.w;
   return normalize(T * sti + (X * cos(phi) + Y * sin(phi)) * cti);
}

// ---- Half-res RT shadows (C18): vis_fs traces one shadow ray per light (aim.w = channel) at full-res pixel 2q
// from the depth prepass; r_fs takes the 2x2 texels around it that lie on its surface (distance within 2%), and
// traces its own ray where none does (silhouettes, thin parts) or where they disagree (shadow edges stay full-res).
// Fog (rows 5.3, 5.4): the part of o + d t, t in [0, tmax], inside the box lo..hi, and the Henyey-Greenstein phase
// (c the cosine between the light's travel before and after the scatter).
float2 boxSpan(float3 lo, float3 hi, float3 o, float3 d, float tmax)
{
   const float3 inv = 1.0 / select(d, float3(1e-12), abs(d) < 1e-12);
   const float3 a = (lo - o) * inv, c = (hi - o) * inv;
   const float3 l = min(a, c), h = max(a, c);
   return float2(max(max(l.x, l.y), max(l.z, 0.0)), min(min(h.x, h.y), min(h.z, tmax)));
}
float hgPhase(float c, float g) { const float k = max(1.0 + g * g - 2.0 * g * c, 1e-6); return (1.0 - g * g) / (12.566370614 * k * sqrt(k)); }
struct VisU { float4x4 invVP; float4 info; float4 fog; float4 fogC; float4 fogLo; float4 fogHi; }; // info: full width, height; fog as PtU's
struct VisOut { float4 v [[color(0)]]; float d [[color(1)]]; float4 fg [[color(2)]]; }; // fg: fog in-scatter (rgb) and transmittance (a) along the view
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
   VisOut o; o.v = float4(1.0); o.d = -1.0; o.fg = float4(0.0, 0.0, 0.0, 1.0);
   const int W = int(u.info.x), H = int(u.info.y);
   const int2 px = int2(in.pos.xy) * 2;
   if (px.x >= W || px.y >= H) return o;
   if (u.fog.x > 0.0)
   {
      // Row 5.4: single scattering of the sun along the view through the fog box, 8 jittered steps, one RT shadow
      // ray each (the shaft); the sun's own path through the fog dims it. r_fs blends this as col * a + rgb.
      const float z = dt.read(uint2(px));
      const float3 e = f.eye.xyz;
      const float2 ndc = float2(2.0 * (float(px.x) + 0.5) / u.info.x - 1.0, 2.0 * (float(px.y) + 0.5) / u.info.y - 1.0);
      const float4 pf = u.invVP * float4(ndc, 1.0, 1.0);
      const float3 vd = normalize(pf.xyz / pf.w - e);
      const float2 sp = boxSpan(u.fogLo.xyz, u.fogHi.xyz, e, vd, z >= 1.0 ? 1e4 : distance(visWp(u, dt, px), e));
      if (sp.y > sp.x)
      {
         const float sig = u.fog.x, st = (sp.y - sp.x) / 8.0;
         const float jit = fract(52.9829189 * fract(0.06711056 * float(px.x) + 0.00583715 * float(px.y)) + 0.61803399 * f.gi.z);
         float3 In = float3(0.0);
         for (int k = 0; k < 8; k++)
         {
            const float t = sp.x + (float(k) + jit) * st;
            const float3 p = e + vd * t;
            const float Tv = exp(-sig * (t - sp.x));
            for (int i = 0; i < 8 && i < int(f.params.w); i++)
            {
               constant LightU& L = f.lights[i];
               const int type = int(L.vec.w);
               if (type != 0 && type != 2) continue;
               const float3 ld = normalize(L.vec.xyz);
               if (L.color.w < 0.5 && occluded(rtArgs.tlas, p, ld, 1e4)) continue;
               const float2 ss = boxSpan(u.fogLo.xyz, u.fogHi.xyz, p, ld, 1e4);
               In += Tv * exp(-sig * max(ss.y - ss.x, 0.0)) * hgPhase(dot(ld, vd), u.fog.y) * L.color.xyz * L.terms.x;
            }
         }
         o.fg = float4(In * (sig * st) * u.fogC.xyz, exp(-sig * (sp.y - sp.x)));
      }
   }
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
      if (L.aim.w < -0.5 || L.color.w > 0.5) continue;
      const int ch = min(int(L.aim.w + 0.5), 3);
      const int type = int(L.vec.w);
      float3 ld; float tmax = 1e4;
      if (type == 1 || type == 4) { const float3 tl = L.vec.xyz - p; const float dist = length(tl); ld = tl / max(dist, 1e-4); tmax = max(dist - 0.01, 0.0); }
      else ld = normalize(L.vec.xyz);
      if (type == 1 || type == 4) { if (occluded(rtArgs.tlas, p + n * f.shQ.w, ld, tmax)) o.v[ch] = 0.0; }
      else o.v[ch] = sunVis(rtArgs.tlas, nullptr, false, p + n * f.shQ.w, ld, (type == 0 || type == 2) ? L.spot.x : 0.0, float2(px));
      if (u.fog.x > 0.0) { const float2 ss = boxSpan(u.fogLo.xyz, u.fogHi.xyz, p, ld, tmax); o.v[ch] *= exp(-u.fog.x * max(ss.y - ss.x, 0.0)); } // row 5.4: the light crosses the fog
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
#ifndef SSS_K
#define SSS_K 4.0
#endif
#ifndef SSS_D
#define SSS_D 1.0
#endif
fragment FOut r_fs(VOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant InstU& m [[buffer(2)]],
                     depth2d<float> sm [[texture(0)]], constant RTArgs& rtArgs [[buffer(3)]],
                     constant RInst* instInfo [[buffer(4)]], texture2d<float> et [[texture(9)]],
                     texture2d<float> visT [[texture(7)]], texture2d<float> visD [[texture(8)]], texture2d<float> visF [[texture(6)]],
                     array<texture2d<float>, 16> maps [[texture(10)]], uint prim [[primitive_id]],
                     constant LightU* LL [[buffer(6)]], constant uint* CL [[buffer(7)]])
{
   float3 n = normalize(in.n);
   float3 base = float3(m.mat.base);
   float metalIn = m.mat.metallic, roughIn = m.mat.roughness;
   // Hair: from triangle shc.w on (a Fur mesh's strands; the skin keeps the material). The vertex normal is the strand.
   const bool hairM = m.mat.hairRadialNeg < 0.0 && float(prim) >= m.mat.hairFrom;
   const bool strandPx = m.mat.strandFrom > 0.5 && float(prim) >= m.mat.strandFrom - 1.0; // row 5.2
   if (hairM) { base = hairAlbedo(float3(m.mat.hairSigma), -m.mat.hairRadialNeg); metalIn = 0.0; }
   const int wrapM = int(m.mat.wrapBump + 0.5) & 3;
   // Every derivative first, in uniform control flow, before any map read or cutout.
   const MapX mx = mapX(m.mat);
   float3 mb = mapBase(mx, in.uv, in.lp);
   const float3 mbx = dfdx(mb), mby = dfdy(mb);
   const float3 dp1 = dfdx(in.wp), dp2 = dfdy(in.wp);
   const float2 shDu1 = dfdx(in.uv), shDu2 = dfdy(in.uv);
   const float3 dnx = dfdx(in.n), dny = dfdy(in.n);
   const float3 nP = n; // the projection's normal (triplanar weights, dominant plane)
   // Row 4.2: a bump map on uv marches the view ray through its relief; every map then reads at the hit.
   float pomHt = 1.0, pomScale = 0.0;
   float3 pomT = float3(0.0), pomB = float3(0.0);
#ifndef R3D_AB_NOPOM
   if (m.mat.mapNormal > 0.5 && (int(m.mat.wrapBump + 0.5) & 4) != 0 && mx.space == 0 && mx.brk <= 0.001 && !hairM)
   {
      texture2d<float> ht = maps[int(m.mat.mapNormal) - 1];
      const float2 tsz = float2(ht.get_width(), ht.get_height());
      const float rho = max(length(mbx.xy * tsz), length(mby.xy * tsz));
      pomScale = 4.0 * m.mat.normalStrength / tsz.x * (1.0 - smoothstep(4.0, 8.0, rho)); // far: the bump normal carries it
      const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      float3 t = dp2perp * mbx.x + dp1perp * mby.x, bt = dp2perp * mbx.y + dp1perp * mby.y;
      const float3 V = normalize(f.eye.xyz - in.wp);
      if (pomScale > 1e-6 && max(dot(t, t), dot(bt, bt)) > 1e-12 && dot(V, n) > 0.0)
      {
         t = normalize(t); bt = normalize(bt);
         mb.xy = pomMarch(ht, mx, mb.xy, mbx.xy, mby.xy, float3(dot(V, t), dot(V, bt), dot(V, n)), pomScale, pomHt);
         pomT = t; pomB = bt;
      }
      else pomScale = 0.0;
   }
#endif
#define MAPR(slot) mapSample(maps[int(slot) - 1], mx, mb, nP, mbx, mby, -1.0)
   if (m.mat.mapAlbedo > 0.5) base *= mapLin(MAPR(m.mat.mapAlbedo).rgb);
   if (m.mat.mapRoughness > 0.5) roughIn *= MAPR(m.mat.mapRoughness).r;
   if (m.mat.mapMetallic > 0.5) metalIn *= MAPR(m.mat.mapMetallic).r;
   const float aoV = m.mat.mapAo > 0.5 ? MAPR(m.mat.mapAo).r : 1.0;
   const float3 emMap = m.mat.mapEmission > 0.5 ? mapLin(MAPR(m.mat.mapEmission).rgb) : float3(1.0);
   const float shW = m.mat.sheen * (m.mat.mapSheen > 0.5 ? MAPR(m.mat.mapSheen).r : 1.0);
   const float shMax = shW * maxc(float3(m.mat.sheenColor));
   float alpha = 1.0 - m.mat.clear;
   if (m.mat.mapOpacity > 0.5) alpha *= MAPR(m.mat.mapOpacity).r;
   const float ccW = m.mat.coat * (m.mat.mapCoat > 0.5 ? MAPR(m.mat.mapCoat).r : 1.0);
   const float transMap = m.mat.mapTransmission > 0.5 ? MAPR(m.mat.mapTransmission).r : 1.0;
#undef MAPR
#if FLD_MAT_ANY
   // Field material program (row 1.3): after the maps, before the cut; it replaces what it writes. Its normal and
   // height bend n after the normal map; height derivatives are taken here, before any discard.
   float3 fldEmD = float3(0.0), fldNrm = float3(0.0, 0.0, 1.0);
   float2 fldDh = float2(0.0);
   const float2 fldDuv1 = dfdx(in.uv), fldDuv2 = dfdy(in.uv);
   if (abs(m.mat.program) > 0.5)
   {
      FldMat o;
      const float3 em0 = float3(m.mat.emission) * emMap;
      o.base = base; o.roughness = roughIn; o.metallic = metalIn; o.normal = float3(0.0, 0.0, 1.0); o.height = 0.0;
      o.emission = em0; o.opacity = alpha; o.mask = 1.0;
      o.prm0 = float4(m.mat.p0, m.mat.p1, m.mat.p2, m.mat.p3); o.prm1 = float4(m.mat.p4, m.mat.p5, m.mat.p6, m.mat.p7); o.prm2 = float4(m.mat.p8, m.mat.p9, m.mat.p10, m.mat.p11); o.prm3 = float4(m.mat.p12, m.mat.p13, m.mat.p14, m.mat.p15);
      fldMatDispatch(int(abs(m.mat.program) + 0.5), o, in.lp, normalize(in.n), in.uv, f.fld.x, in.wp, normalize(f.eye.xyz - in.wp), float4(dot(in.n, f.eye.xyz - in.wp) < 0.0 ? 1.0 : 0.0, 0.0, in.cw.x, in.cw.y), float4(in.cw.z, 0.0, 0.0, 0.0), maps, float4(m.mat.fimg0, m.mat.fimg1, m.mat.fimg2, m.mat.fimg3), -1.0);
      fldDh = float2(dfdx(o.height), dfdy(o.height));
      base = o.base; roughIn = o.roughness; metalIn = o.metallic; fldEmD = o.emission - em0; alpha = o.opacity;
      fldNrm = o.normal;
      if (o.mask < 0.5) discard_fragment();
   }
#endif
   if (alpha < 1.0)
   {
      // A cutoff cuts below it; without one, opacity is a coverage the frames average (a per-pixel threshold).
      uint as = pcg(uint(in.pos.x) * 7919u + 104729u * uint(in.pos.y)) ^ pcg(uint(f.gi.z) * 977u + 5u);
      if (alpha < (m.mat.alphaCutoff > 0.0 ? m.mat.alphaCutoff : rnd(as))) discard_fragment();
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
   // The ribbon's face toward us carries what is not along the strand.
   const float3 hT = normalize(in.n);
   if (hairM && dot(gN, gN) > 0.5) n = gN;
   if (m.mat.mapNormal > 0.5 && !hairM)
   {
      // GL's cotangent frame from screen derivatives of position and the mapped uv (Schuler), then the tangent-space sample.
      const float2 muv = mapUv(mx, mb, nP), duv1 = mapUv(mx, mbx, nP), duv2 = mapUv(mx, mby, nP);
      const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      float3 t = dp2perp * duv1.x + dp1perp * duv2.x, bt = dp2perp * duv1.y + dp1perp * duv2.y;
      if (max(dot(t, t), dot(bt, bt)) > 1e-12) { t = normalize(t); bt = normalize(bt); }
      else { t = abs(n.z) < 0.999 ? normalize(cross(n, float3(0.0, 0.0, 1.0))) : normalize(cross(n, float3(1.0, 0.0, 0.0))); bt = normalize(cross(n, t)); }
      const float3 s = mapNormalTS(maps[int(m.mat.mapNormal) - 1], mx, (int(m.mat.wrapBump + 0.5) & 4) != 0, m.mat.normalStrength, muv, duv1, duv2, -1.0, m.mat.detailScale, m.mat.detailStrength);
      n = normalize(t * s.x + bt * s.y + n * s.z);
   }
#if FLD_MAT_ANY
   if (abs(m.mat.program) > 0.5 && !hairM)
   {
      // The program's tangent-space normal through the cotangent frame of the mesh uv, then its height as a bump
      // (surface gradient: n' = |det| n - sign(det) (dh/dx r1 + dh/dy r2)).
      const float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      float3 t = dp2perp * fldDuv1.x + dp1perp * fldDuv2.x, bt = dp2perp * fldDuv1.y + dp1perp * fldDuv2.y;
      if (max(dot(t, t), dot(bt, bt)) > 1e-12) { t = normalize(t); bt = normalize(bt); }
      else { t = abs(n.z) < 0.999 ? normalize(cross(n, float3(0.0, 0.0, 1.0))) : normalize(cross(n, float3(1.0, 0.0, 0.0))); bt = normalize(cross(n, t)); }
      n = normalize(t * fldNrm.x + bt * fldNrm.y + n * fldNrm.z);
      const float3 r1 = cross(dp2, n), r2 = cross(n, dp1);
      const float det = dot(dp1, r1);
      const float3 nb = abs(det) * n - sign(det) * (fldDh.x * r1 + fldDh.y * r2);
      if (dot(nb, nb) > 1e-30) n = normalize(nb);
   }
#endif
   const float nDotV = max(dot(n, viewDir), 1e-4);
   float3 shN = n; // row 5.1: the sheen lobe leans along the mesh-uv tangent (screen-derivative frame, as the normal map's)
   if (m.mat.sheenDir != 0.0) shN = sheenTilt(n, cross(dp2, n) * shDu1.x + cross(n, dp1) * shDu2.x, m.mat.sheenDir);
   const float shNV = max(dot(shN, viewDir), 1e-4);
   const float shEv = shMax > 0.0 ? sheenE(shNV, m.mat.sheenRoughness) : 0.0;
   const float shV = 1.0 - shMax * shEv;          // the base layer's share under the sheen
   const float3 shAmb = float3(m.mat.sheenColor) * (shW * shEv); // the sheen's own share of ambient light
   const float3 f0 = mix(float3(0.04 + m.mat.f0Extra), base, metal); // GL's dielectric F0 from ior and specular
   const float3 iridK = iridGain(m.mat.iridescence, m.mat.iridIor, m.mat.iridThickness, nDotV, f0);
   // Clearcoat (GL's): a layer on top; what it reflects at this angle never reaches the base below.
   const float ccR = clamp(m.mat.coatRoughness, 0.02, 1.0);
   const float ccFv = 0.04 + 0.96 * pow(1.0 - nDotV, 5.0);
   const float ccAt = 1.0 - ccW * ccFv;
   float3 coatCol = float3(0.0);
   float3 col = float3(0.0);
   float3 floorBounce = float3(0.0);
   // Subsurface (C9), as GL: per channel the light bleeds past the terminator by scatter radius against curvature,
   // w = K r kappa, diffuse (N.L + w) / (1 + w)^2; translucency passes a share of the diffuse to the far side.
   const float sssW = hairM ? 0.0 : clamp(m.mat.subsurface, 0.0, 1.0), tlW = hairM ? 0.0 : clamp(m.mat.translucency, 0.0, 1.0);
   float3 ssW = float3(0.0);
   if (sssW > 0.0)
   {
      const float kap = sqrt((dot(dnx, dnx) + dot(dny, dny)) / max(dot(dp1, dp1) + dot(dp2, dp2), 1e-12));
      ssW = clamp(SSS_K * float3(m.mat.sssRadius) * kap, 0.0, 1.0);
   }
   const float frontD = (1.0 - sssW) * (1.0 - tlW);
   const int count = int(f.params.w);
   // Row 5.6: past 8 lights, this pixel's froxel lists the lights that reach it (LightClusters.h); else the first 8.
   const bool clu = f.fld.y > 8.5;
   uint cBase = 0u; int iters = count;
   if (clu)
   {
      const float4 cp = f.viewProj * float4(in.wp, 1.0);
      const uint gx = CL[0], gy = CL[1], gz = CL[2];
      const uint cx = uint(clamp((cp.x / cp.w * 0.5 + 0.5) * float(gx), 0.0, float(gx - 1)));
      const uint cy = uint(clamp((cp.y / cp.w * 0.5 + 0.5) * float(gy), 0.0, float(gy - 1)));
      const float zk = log(max(cp.w, as_type<float>(CL[4])) / as_type<float>(CL[4])) / as_type<float>(CL[5]);
      const uint cz = uint(clamp(floor(zk * float(gz)), 0.0, float(gz - 1)));
      const uint cell = CL[12u + (cz * gy + cy) * gx + cx];
      iters = int(cell & 255u); cBase = 12u + CL[3] + (cell >> 8);
   }
   for (int k = 0; k < 64; k++)
   {
      if (k >= iters) break;
      const int i = clu ? int(CL[cBase + uint(k)]) : k;
      constant LightU& L = clu ? LL[i] : f.lights[i];
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
            float e = lightFalloff(dist, L.terms.z) * max(dot(-ld, L.aim.xyz), 0.0) * (hairM ? 1.0 : max(dot(n, ld), 0.0));
            if (L.terms.w > 0.0 && dist > L.terms.w) e = 0.0;
            if (e > 0.0 && rtSh && L.color.w < 0.5 && occluded(rtArgs.tlas, in.wp + (dot(n, ld) < 0.0 ? -n : n) * f.shQ.w, ld, max(dist - 0.01, 0.0))) e *= 1.0 - f.shP.w;
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
         if (atten <= 0.0 && type != 5) continue; // out of reach (cutoff, cone): every term below is scaled by it
      }
      else lightDir = normalize(L.vec.xyz);
      const float3 halfway = normalize(lightDir + viewDir);
      float nDotL;
      nDotL = max(dot(n, lightDir), 0.0); // no GL sun wrap: the tracer and Cycles have none, so live and settled agree
      if (type == 5) nDotL = max(nDotL, 1e-3); // the centre may face away while part of the rectangle is seen
      if (hairM) nDotL = 1.0; // a fibre takes light from every side; the BSDF holds the cosine
      const float nlS = dot(n, lightDir);
      if (nDotL <= 0.0 && !((sssW > 0.0 && any(nlS + ssW > 0.0)) || (tlW > 0.0 && type != 5))) continue;
      const float3 nS = dot(n, lightDir) < 0.0 ? -n : n; // shadow rays leave on the light's side
      const float nDotH = max(dot(n, halfway), 0.0);
      const float d = distGGX(nDotH, rough);
      const float g = R3D_G2(nDotV, nDotL, rough);
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
         const float gs = R3D_G2(nDotV, max(nls, 1e-4), rough);
         spec = ds * gs * fresnel(max(dot(hs, viewDir), 0.0), f0) / max(4.0 * nDotV, 1e-7) * (nls / max(nDotL, 1e-4)); // * nDotL below
      }
      spec *= ggxMs(f0, rough, nDotV) * iridK;
      const float3 kD = (float3(1.0) - fr) * (1.0 - metal);
      const float3 radiance = radiance0 * atten;
      floorBounce += radiance * max(lightDir.y, 0.0) * L.terms.x;
      float shadow = 1.0;
      if (type == 5) shadow = areaE / max(atten * nDotL, 1e-6); // radiance * nDotL * shadow == radiance0 * E
      else
      {
         if (f.shP.x > 0.5 && i == int(f.shP.y)) shadow = shadowFactor(f, sm, in.wp, nS, lightDir);
         if ((int(f.shQ.z + 0.5) & 1) != 0 && L.color.w < 0.5)
         {
            float vis = -1.0;
            if ((int(f.shQ.z + 0.5) & 4) != 0 && L.aim.w > -0.5 && !strandPx) // a strand traces its own (row 5.2)
               vis = upVis(visT, visD, in.pos.xy, distance(in.wp, f.eye.xyz), int(L.aim.w + 0.5));
            if (vis < 0.0)
            {
               if (type == 1 || type == 4)
                  vis = shadowT(rtArgs.tlas, instInfo, strandPx, in.wp + nS * f.shQ.w, lightDir, max(length(L.vec.xyz - in.wp) - 0.01, 0.0));
               else
                  vis = strandPx ? sunVisStrand(rtArgs.tlas, instInfo, in.wp + nS * f.shQ.w, lightDir, (type == 0 || type == 2) ? L.spot.x : 0.0, in.pos.xy, f.gi.z)
                                 : sunVis(rtArgs.tlas, instInfo, false, in.wp + nS * f.shQ.w, lightDir, (type == 0 || type == 2) ? L.spot.x : 0.0, in.pos.xy);
            }
            shadow = mix(1.0 - f.shP.w, 1.0, vis);
         }
      }
      if (pomScale > 0.0)
         shadow *= pomShadow(maps[int(m.mat.mapNormal) - 1], mx, mb.xy, pomHt, mbx.xy, mby.xy,
                             float3(dot(lightDir, pomT), dot(lightDir, pomB), dot(lightDir, normalize(in.n))), pomScale);
      if (hairM)
      {
         col += hairAvg(viewDir, lightDir, hT, float3(m.mat.hairSigma), clamp(roughIn, 0.02, 1.0), -m.mat.hairRadialNeg, m.mat.hairShift, m.mat.ior) * radiance * shadow * L.terms.x;
         continue;
      }
      float3 shS = float3(0.0);
      float shK = 1.0;
      if (shW > 0.0) // sheen: Cycles' microfiber lobe, over a base scaled by its albedo
      {
         const float shL = m.mat.sheenDir != 0.0 ? max(dot(shN, lightDir), 0.0) : nDotL;
         shS = float3(m.mat.sheenColor) * sheenLtc(shL, shNV, dot(lightDir, viewDir), m.mat.sheenRoughness) * shW * (m.mat.sheenDir != 0.0 ? shL / max(nDotL, 1e-4) : 1.0);
         shK = shV; // Cycles scales the base by the sheen albedo at the view angle only
      }
      // Diffuse: the Lambert share, the subsurface wrap (unshadowed past the terminator, where a shadow ray from the
      // surface meets the object itself) and the back side's share (its shadow ray leaves from behind).
      float3 dif = float3(frontD * nDotL * shadow);
      if (sssW > 0.0) dif += (1.0 - tlW) * sssW * max(nlS + ssW, 0.0) / ((1.0 + ssW) * (1.0 + ssW)) * (nlS > 0.0 ? shadow : 1.0);
      if (tlW > 0.0) dif += tlW * max(-nlS, 0.0) * shadow;
      col += ((kD * base / 3.14159265 * dif * L.terms.x + spec * nDotL * shadow * L.terms.y) * shK + shS * nDotL * shadow * L.terms.y) * radiance;
      if (ccW > 0.0) coatCol += ccW * coatSpec(nDotH, nDotV, nDotL, max(dot(halfway, viewDir), 0.0), ccR) * L.terms.y * radiance * nDotL * shadow;
   }
   if (!(f.gi.x > 0.5)) col += base / 3.14159265 * floorBounce * (0.25 * (1.0 - n.y)) * (1.0 - metal); // fake bounce only without real GI
   // GI and traced reflections run in tr_fs at half res; this pass writes their demodulated weights instead.
   const float3 irradiance = sampleEnv(f, et, n, 1.0) * f.ambient.xyz * 3.0;
   const float3 fAmb = fresnelRough(nDotV, f0, rough) * iridK;
   const float3 envRefl = sampleEnv(f, et, reflect(-viewDir, n), rough) * fAmb * (aoV * ccAt * shV);
   const bool reflOn = (int(f.shQ.z + 0.5) & 2) != 0 && rough < 0.55;
   const float envKeep = reflOn ? smoothstep(0.3, 0.55, rough) : 1.0; // the env share of the reflection; the rest is traced
   const bool giOn = f.gi.x > 0.5;
   if (!giOn) col += ((float3(1.0) - fAmb) * (1.0 - metal) * base * shV + shAmb) * irradiance * aoV;
   col += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
   // Glass (transmission): refract in, find the far side of the hit object, refract out (or reflect on total internal
   // reflection), shade what lies beyond. Tint and Fresnel as Render 3D's: beyond * mix(1, base, 0.4) * (1 - F).
   // Needs the TLAS; without it the glass shows the environment along one refracted ray.
   const float trans = clamp(m.mat.transmission * transMap, 0.0, 1.0);
   const bool rtOn = (int(f.shQ.z + 0.5) & 3) != 0 || f.gi.x > 0.5;
   if (trans > 0.0 && rtOn)
   {
      const float eta = 1.0 / max(m.mat.ior, 1.0001);
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
      const float3 beyond = shadeHit(f, et, rtArgs.tlas, instInfo, maps, o2, d2, 0.0, false, hp, hn, alb, mir, true, -1, true);
      col = mix(col, beyond * mix(float3(1.0), base, 0.4) * (float3(1.0) - fAmb), trans);
   }
   else if (trans > 0.0)
   {
      // No rays this frame: see the environment through the glass along the refracted view, tinted the same way.
      float3 rd = refract(-viewDir, n, 1.0 / max(m.mat.ior, 1.0001));
      if (dot(rd, rd) < 1e-6) rd = reflect(-viewDir, n);
      col = mix(col, sampleEnv(f, et, rd, 0.0) * mix(float3(1.0), base, 0.4) * (float3(1.0) - fAmb), trans);
   }
   col = col * ccAt + coatCol + (ccW > 0.0 ? sampleEnv(f, et, reflect(-viewDir, n), ccR) * (ccW * ccFv) : float3(0.0));
   col += float3(m.mat.emission) * emMap;
#if FLD_MAT_ANY
   col += fldEmD;
#endif
   const float opaque = (trans > 0.0 && rtOn) ? 1.0 - trans : 1.0; // glass takes no GI of its own
   // Row 5.4: the fog between the eye and this surface (vis_fs, half res), a 3x3 box of texels against its noise.
   float3 fogIn = float3(0.0); float fogT = 1.0;
   if (visF.get_width() > 1)
   {
      const int2 q = int2(in.pos.xy) / 2, fs = int2(visF.get_width(), visF.get_height());
      float4 a = float4(0.0);
      for (int j = -1; j <= 1; j++)
         for (int i = -1; i <= 1; i++) a += visF.read(uint2(clamp(q + int2(i, j), int2(0), fs - 1)));
      a /= 9.0; fogIn = a.rgb; fogT = a.a;
   }
   FOut o;
   if (!kMrt)
   {
      float3 fin = ((col + envRefl) * fogT + fogIn) * f.params.x;
      fin = toneMap(fin, int(f.params.y));
      o.c0 = float4(toSrgb(fin), 1.0);
      return o;
   }
   // With GI, the rough share of the reflection sees the room, not only the env: its lobe is wide enough that the
   // cosine-weighted GI radiance stands in for it (both carry the env on a miss, ambient * 3 == 1 in linear terms).
   o.c0 = float4(((col + (giOn ? float3(0.0) : envRefl * envKeep)) * fogT + fogIn) * f.params.x, 1.0);
   o.c1 = float4(giOn ? (((float3(1.0) - fAmb) * (1.0 - metal) * base * opaque + fAmb * envKeep) * shV + shAmb * opaque) * f.gi.y * f.params.x * (aoV * ccAt) : float3(0.0), rough); // GI weight
   o.c2 = float4(fAmb * (1.0 - envKeep) * f.params.x * (aoV * ccAt * shV), reflOn ? 1.0 : 0.0); // traced-reflection weight
   o.c1.rgb *= fogT; o.c2.rgb *= fogT;
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
                     texture2d<float> hist [[texture(3)]], texture2d<float> et [[texture(9)]],
                     array<texture2d<float>, 16> maps [[texture(10)]])
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
         float3 L1 = shadeHit(f, et, rtArgs.tlas, instInfo, maps, org, dir, 1.0, true, hp, hn, alb, mir, false, int(rnd(seed) * f.params.w), false);
         float3 irr;
         if (any(alb > 0.0) && hv && histAt(f, nz, hist, hp, hn, irr))
            L1 += alb * irr; // every bounce through last frame's GI at that point
         else if (any(mir > 0.0))
         {
            // Metal hit: follow its mirror direction every frame (an env-only answer leaves what a mirror lights dark).
            float3 hp2, hn2, alb2, mir2;
            const float3 L2 = shadeHit(f, et, rtArgs.tlas, instInfo, maps, hp + hn * f.shQ.w, reflect(dir, hn), 0.3, false, hp2, hn2, alb2, mir2, true, int(rnd(seed) * f.params.w), true, float2(rnd(seed), rnd(seed)));
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
            const float3 L2 = shadeHit(f, et, rtArgs.tlas, instInfo, maps, hp + hn * f.shQ.w, d2, 1.0, true, hp2, hn2, alb2, mir2, true, int(rnd(seed) * f.params.w), true, float2(rnd(seed), rnd(seed)));
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
      o.r = float4(traceReflection(f, et, rtArgs.tlas, instInfo, maps, org, jd, rough, rs, nz, hist, hv, reflect(d, n)), 1.0);
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
struct PtU { float4x4 invVP; float4 info; float4 em; float4 lens; float4 lr; float4 lu; float4 lf; float4 fog; float4 fogC; float4 fogLo; float4 fogHi; }; // info: x prior samples, y max bounces, z frame, w width; em: x emitter triangles, y chance to sample one
// lens: thin-lens radius, focus distance, blades (below 3 round), blade turn; lr/lu/lf: camera right (w = bokeh ratio), up, forward
// fog (row 5.3): x extinction per unit (0 off), y HG g; fogC the scattered share; fogLo/fogHi the medium's box
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
#ifdef R3D_AB_PTFRESNEL
float3 ptF(float c, float3 f0) { return fresnelExact(c, f0); }
#else
float3 ptF(float c, float3 f0) { return f0 + (float3(1.0) - f0) * ptS5(c); }
#endif
// Multiple-scattering gain for the tracer's own lobe (Kulla and Conty 2017, Turquin 2019): E is that lobe's
// directional albedo at F = 1, baked by quadrature into a 16x16 table (x = nV, y = roughness) at start-up.
constexpr sampler kELut(filter::linear, address::clamp_to_edge);
float3 ptMs(float3 f0, float rough, float nV, texture2d<float> eLut)
{
   const float E = clamp(eLut.sample(kELut, float2(nV, rough), level(0.0)).r, 0.05, 1.0);
   return float3(1.0) + f0 * (1.0 / E - 1.0);
}
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
// Glass: the exact unpolarised dielectric Fresnel (eta = n_i / n_t, 1 past the critical angle) and Smith G1 for GGX.
float ptFd(float c, float eta)
{
   const float s2 = eta * eta * (1.0 - c * c);
   if (s2 >= 1.0) return 1.0;
   const float ct = sqrt(1.0 - s2);
   const float rs = (eta * c - ct) / (eta * c + ct), rp = (c - eta * ct) / (c + eta * ct);
   return 0.5 * (rs * rs + rp * rp);
}
float ptG1(float x, float a) { return 2.0 * x / (x + sqrt(a * a + (1.0 - a * a) * x * x)); }
// f(V, L) * cos(L), and the pdf of drawing L by ptProbs. w: a light's diffuse and specular terms.
float3 ptEval(float3 n, float3 V, float3 L, float3 base, float metal, float rough, float3 f0, float2 w, float3 ms, thread float& pdf)
{
   const float nL = dot(n, L);
   if (nL <= 1e-6) { pdf = 0.0; return float3(0.0); }
   const float nV = max(dot(n, V), 1e-4);
   const float3 H = normalize(V + L);
   const float nH = max(dot(n, H), 0.0), vH = max(dot(V, H), 1e-4);
   const float a = max(0.002, rough * rough);
   const float D = ptD(nH, a);
   const float3 F = ptF(vH, f0);
   const float3 fr = D * ptVis(nV, nL, a) * F * ms * w.y + (float3(1.0) - F) * (1.0 - metal) * base * (w.x / 3.14159265);
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
      intersection_result<triangle_data, instancing> h = it.intersect(r, tlas, 1); // bit 0: shadow casters
      if (h.type != intersection_type::triangle) return false;
      constant RInst& I = inst[h.instance_id];
      float alpha = 1.0 - I.mat.clear;
      if (I.mat.mapOpacity > 0.5)
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
         const MapX mx = mapX(I.mat);
         alpha *= mapSample(maps[int(I.mat.mapOpacity) - 1], mx, mapBase(mx, uv, lp), n, float3(0.0), float3(0.0), 0.0).r;
      }
      if (!(alpha < 1.0 && alpha < (I.mat.alphaCutoff > 0.0 ? I.mat.alphaCutoff : rnd(seed)))) return true;
      o += d * (h.distance + eps); tmax -= h.distance + eps;
      if (tmax <= 0.0) return false;
   }
   return true;
}
// Settle's next-event pick: an emissive triangle (chance pEm, picked by power) or one light, picked uniformly and
// weighted by the count and the chance of taking this branch. pL > 0: an emitter, its solid-angle pdf.
void ptPickLight(constant FrameU& f, constant LightU* LL, const device EmTri* ems, int nEm, float pEm, int count, float3 p, thread uint& seed,
                 thread float3& ld, thread float3& Le, thread float& tmax, thread float& pL, thread float2& lterms)
{
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
      constant LightU& Lt = LL[min(int(rnd(seed) * float(count)), count - 1)]; // row 5.6: all N lights
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
      if (Lt.color.w > 0.5) pL = -2.0; // a light set to cast no shadow
   }
}
// Translucency's lobes: the specular on the lit side, the diffuse share passed through to the far side (cosine there).
float3 ptEvalT(float3 n, float3 V, float3 L, float3 base, float metal, float rough, float3 f0, float2 w, float3 ms, thread float& pdf)
{
   const float nL = dot(n, L), nV = max(dot(n, V), 1e-4);
   const float2 p = ptProbs(f0, metal, nV);
   if (nL > 1e-6)
   {
      const float3 fs = ptEval(n, V, L, base, metal, rough, f0, float2(0.0, w.y), ms, pdf);
      pdf -= p.y * nL / 3.14159265; // the diffuse half of the mixture lives on the far side here
      return fs;
   }
   const float3 Fv = ptF(nV, f0);
   pdf = p.y * (-nL) / 3.14159265;
   return (float3(1.0) - Fv) * (1.0 - metal) * base * (w.x / 3.14159265) * (-nL);
}
// Three running sums (the denoiser's inputs, Render3DDenoise.h): radiance + hits, first-hit albedo + luminance^2,
// first-hit normal + distance.
struct PtOut { float4 acc [[color(0)]]; float4 aux [[color(1)]]; float4 gd [[color(2)]]; };
// The HDRI as a light (PBRT's piecewise-constant 2D distribution; Veach 1997 for the MIS). `cdf` is built on the CPU
// from the map's luminance times sin(theta) on an NX x NY grid: rows 0..NY-1 hold each row's cumulative chance
// over x, row NY the cumulative chance over rows, rows NY+1.. each cell's density over the unit uv square.
// envPick draws a direction in proportion to the sky's light; envPdf is the chance (per solid angle) envPick
// would give a direction, for weighting a bounce that reaches the sky by itself.
float3 envPick(constant FrameU& f, texture2d<float> cdf, float2 u, thread float& pdf)
{
   const int NX = int(cdf.get_width()), NY = (int(cdf.get_height()) - 1) / 2;
   int lo = 0, hi = NY - 1;
   for (int i = 0; i < 12 && lo < hi; i++) { const int mid = (lo + hi) / 2; if (cdf.read(uint2(mid, NY)).r < u.y) lo = mid + 1; else hi = mid; }
   const int y = lo;
   const float m0 = y > 0 ? cdf.read(uint2(y - 1, NY)).r : 0.0, m1 = cdf.read(uint2(y, NY)).r;
   lo = 0; hi = NX - 1;
   for (int i = 0; i < 12 && lo < hi; i++) { const int mid = (lo + hi) / 2; if (cdf.read(uint2(mid, y)).r < u.x) lo = mid + 1; else hi = mid; }
   const int x = lo;
   const float c0 = x > 0 ? cdf.read(uint2(x - 1, y)).r : 0.0, c1 = cdf.read(uint2(x, y)).r;
   // the leftover of each random number places the sample inside its cell
   const float2 uv = float2((float(x) + clamp((u.x - c0) / max(c1 - c0, 1e-12), 0.0, 1.0)) / float(NX),
                            (float(y) + clamp((u.y - m0) / max(m1 - m0, 1e-12), 0.0, 1.0)) / float(NY));
   const float th = uv.y * 3.14159265359, ph = (uv.x - 0.5) * 6.28318530718 - f.envm.y, st = sin(th);
   pdf = cdf.read(uint2(x, NY + 1 + y)).r / (19.7392088 * max(st, 1e-4)); // 2 pi^2 sin(theta): uv square to solid angle
   return float3(st * cos(ph), cos(th), st * sin(ph)); // sampleEnv's lookup, inverted
}
float envPdf(constant FrameU& f, texture2d<float> cdf, float3 dir)
{
   const int NX = int(cdf.get_width()), NY = (int(cdf.get_height()) - 1) / 2;
   const float2 uv = float2(fract((atan2(dir.z, dir.x) + f.envm.y) / 6.28318530718 + 0.5), acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265359);
   const int x = min(int(uv.x * float(NX)), NX - 1), y = min(int(uv.y * float(NY)), NY - 1);
   return cdf.read(uint2(x, NY + 1 + y)).r / (19.7392088 * max(sqrt(max(1.0 - dir.y * dir.y, 0.0)), 1e-4));
}
// Row 5.3: one homogeneous medium in a box. fogSpan is the part of o + d t, t in [0, tmax], inside it.
float2 fogSpan(constant PtU& u, float3 o, float3 d, float tmax)
{
   return boxSpan(u.fogLo.xyz, u.fogHi.xyz, o, d, tmax);
}
float fogTr(constant PtU& u, float3 o, float3 d, float tmax)
{
   if (u.fog.x <= 0.0) return 1.0;
   const float2 s = fogSpan(u, o, d, tmax);
   return exp(-u.fog.x * max(s.y - s.x, 0.0));
}
float3 hgSample(float3 w, float g, float u1, float u2)
{
   float c = 1.0 - 2.0 * u1;
   if (abs(g) > 1e-3) { const float q = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1); c = (1.0 + g * g - q * q) / (2.0 * g); }
   c = clamp(c, -1.0, 1.0);
   const float st = sqrt(max(0.0, 1.0 - c * c)), ph = 6.28318530718 * u2;
   const float3 t = normalize(cross(w, abs(w.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0)));
   return normalize(t * (st * cos(ph)) + cross(w, t) * (st * sin(ph)) + w * c);
}
fragment PtOut pt_fs(SkyOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant PtU& u [[buffer(0)]],
                      constant RTArgs& rtArgs [[buffer(3)]], constant RInst* inst [[buffer(4)]], const device EmTri* ems [[buffer(5)]],
                      constant LightU* LL [[buffer(6)]],
                      texture2d<float> prev [[texture(0)]], texture2d<float> prevAux [[texture(1)]], texture2d<float> prevGd [[texture(2)]],
                      texture2d<float> ecdf [[texture(3)]],
                      texture2d<float> eLut [[texture(8)]], texture2d<float> et [[texture(9)]],
                      array<texture2d<float>, 16> maps [[texture(10)]])
{
   const uint2 px = uint2(in.pos.xy);
   uint seed = pcg(px.x + 8192u * px.y) ^ pcg(uint(u.info.z) * 2654435761u + 17u);
   const float H = float(prev.get_height());
   float2 ndc = float2(2.0 * (float(px.x) + rnd(seed)) / u.info.w - 1.0, 2.0 * (float(px.y) + rnd(seed)) / H - 1.0);
   const float2 ndc0 = ndc;
   // Row 6.2 lens: radial distortion k r^2 (lf.w, + pincushion, - barrel) and lateral chromatic
   // aberration (fogC.w): each sample sees one channel through its own scale and counts 3x in that channel.
   float3 chan = float3(1.0);
   if (u.lf.w != 0.0) ndc *= 1.0 + u.lf.w * dot(ndc, ndc);
   if (u.fogC.w != 0.0)
   {
      const int c = min(int(rnd(seed) * 3.0), 2);
      ndc *= 1.0 + u.fogC.w * float(c - 1);
      chan = float3(c == 0 ? 3.0 : 0.0, c == 1 ? 3.0 : 0.0, c == 2 ? 3.0 : 0.0);
   }
   float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   float3 o = f.eye.xyz;
   float3 d = normalize(pw.xyz / pw.w - o);
   if (u.lens.x > 0.0)
   {
      // Depth of field: a point on the aperture (round, or a polygon of lens.z blades turned by lens.w, stretched by
      // the ratio as Render 3D's bokeh); the ray still meets the focus plane, so it stays sharp there.
      const float3 fp = o + d * (u.lens.y / max(dot(d, u.lf.xyz), 1e-4));
      float2 off;
      // Cat-eye (lu.w): toward the frame's edge the lens barrel clips the aperture by a second disc shifted outward,
      // so out-of-focus highlights turn to lemons; samples outside it are drawn again (up to 8 tries).
      const float2 eye = u.lu.w * ndc0 * float2(u.info.w / H, 1.0) * 0.5;
      for (int tr = 0; tr < 8; tr++)
      {
         const float2 lu = float2(rnd(seed), rnd(seed));
         if (u.lens.z >= 3.0)
         {
            const float seg = 6.2831853 / u.lens.z, a0 = u.lens.w + floor(rnd(seed) * u.lens.z) * seg;
            off = sqrt(lu.x) * mix(float2(cos(a0), sin(a0)), float2(cos(a0 + seg), sin(a0 + seg)), lu.y);
         }
         else
            off = sqrt(lu.x) * float2(cos(6.2831853 * lu.y), sin(6.2831853 * lu.y));
         if (u.lu.w <= 0.0 || length_squared(off - eye) <= 1.0) break;
      }
      off *= u.lr.w >= 1.0 ? float2(1.0 / u.lr.w, 1.0) : float2(1.0, u.lr.w);
      o += (off.x * u.lr.xyz + off.y * u.lu.xyz) * u.lens.x;
      d = normalize(fp - o);
   }
   float3 L = float3(0.0), T = float3(1.0), L0 = float3(0.0);
   float cov = 0.0;
   float3 alb0 = float3(0.0), n0 = float3(0.0); float dist0 = 0.0; // first hit, for the denoiser's guides
   const int maxB = int(u.info.y);
   const int nEm = int(u.em.x); const float pEm = u.em.y;
   float lastPdfB = 0.0; // pdf of the bounce that led here (MIS against emitter sampling)
   float envMis = 0.0; // 1: the hit that sent this bounce also sampled the HDRI as a light, so a miss is weighted against it
   float3 skyK = float3(1.0); // sky scale for the bounce that led here: Render 3D's ambient on the diffuse lobe, 1 on the glossy one (as the raster)
   intersector<triangle_data, instancing> it;
   it.assume_geometry_type(geometry_type::triangle);
   int skips = 0; // see-through hits passed this sample
   for (int b = 0; b < maxB; b++)
   {
      ray r(o, d, 0.0, 1e4);
      intersection_result<triangle_data, instancing> h = it.intersect(r, rtArgs.tlas);
      if (u.fog.x > 0.0)
      {
         // Row 5.3: free flight through the medium (one extinction for all channels, so the weight is the albedo).
         // A scatter takes one light sample with the phase function, then the path goes on along a phase sample.
         const float2 sp = fogSpan(u, o, d, h.type == intersection_type::triangle ? h.distance : 1e4);
         const float s = sp.x - log(max(1.0 - rnd(seed), 1e-12)) / u.fog.x;
         if (sp.y > sp.x && s < sp.y)
         {
            const float3 pm = o + d * s;
            if (b == 0) { cov = 1.0; alb0 = u.fogC.xyz; n0 = -d; dist0 = s; }
            T *= u.fogC.xyz;
            float3 ld = float3(0.0, 1.0, 0.0), Le = float3(0.0); float tmax = 0.0, pL = -1.0; float2 lterms = float2(1.0);
            ptPickLight(f, LL, ems, nEm, pEm, int(f.fld.y + 0.5), pm, seed, ld, Le, tmax, pL, lterms);
            if (any(Le > 0.0))
            {
               if (pL > -1.5 && occludedAlpha(rtArgs.tlas, inst, maps, pm, ld, tmax, f.shQ.w, seed)) Le *= pL > 0.0 ? 0.0 : 1.0 - f.shP.w;
               const float ph = hgPhase(dot(ld, d), u.fog.y);
               L += T * ph * Le * lterms.x * fogTr(u, pm, ld, tmax) * (pL > 0.0 ? pL / max(pL * pL + ph * ph, 1e-20) : 1.0);
            }
            const float3 nd = hgSample(d, u.fog.y, rnd(seed), rnd(seed));
            lastPdfB = hgPhase(dot(nd, d), u.fog.y); envMis = 0.0; skyK = float3(1.0);
            o = pm; d = nd;
            if (b >= 2)
            {
               const float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
               if (rnd(seed) > q) break;
               T /= q;
            }
            continue;
         }
      }
      if (h.type != intersection_type::triangle)
      {
         float wS = 1.0;
         if (envMis > 0.5) { const float pe = envPdf(f, ecdf, d); wS = lastPdfB * lastPdfB / max(lastPdfB * lastPdfB + pe * pe, 1e-20); }
         if (b > 0) L += T * sampleEnv(f, et, d, 0.0) * skyK * wS; // the sky itself; the cosine is in the sampling
         else if ((int(f.envm.x) & 2) != 0) { L = sampleEnv(f, et, d, 0.0); cov = 1.0; } // the HDRI as the background
         break;
      }
      if (b == 0) cov = 1.0;
      envMis = 0.0;
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
      const float3 hT = n; // hair: the strand axis
      const bool backHit = dot(n, d) > 0.0;
      if (backHit) n = -n;
      const bool hairM = I.mat.hairRadialNeg < 0.0 && float(h.primitive_id) >= I.mat.hairFrom;
      if (hairM)
      {
         // The ribbon's face toward the ray (the vertex normal is along the strand).
         const float3 p0 = float3(vb[ix.x * 6], vb[ix.x * 6 + 1], vb[ix.x * 6 + 2]);
         const float3 q1 = float3(vb[ix.y * 6], vb[ix.y * 6 + 1], vb[ix.y * 6 + 2]), q2 = float3(vb[ix.z * 6], vb[ix.z * 6 + 1], vb[ix.z * 6 + 2]);
         float3 gn = (I.normalM * float4(cross(q1 - p0, q2 - p0), 0.0)).xyz;
         if (dot(gn, gn) > 1e-30) { gn = normalize(gn); n = dot(gn, d) > 0.0 ? -gn : gn; }
      }
      const float3 p = o + d * h.distance;
      float3 base = hairM ? hairAlbedo(float3(I.mat.hairSigma), -I.mat.hairRadialNeg) : float3(I.mat.base); float metal = hairM ? 0.0 : clamp(I.mat.metallic, 0.0, 1.0); float rough = clamp(I.mat.roughness, 0.045, 1.0);
      const float3 ns = n; // the surface's own side: ray origins and the light / bounce gates use it, not the mapped normal
      float3 emMap = float3(1.0); float shW = hairM ? 0.0 : I.mat.sheen, ccW = hairM ? 0.0 : I.mat.coat;
#if FLD_MAT_ANY
      float3 fldEm = float3(0.0); bool fldEmSet = false;
      const bool fldOn = abs(I.mat.program) > 0.5;
#else
      const bool fldOn = false;
#endif
      if (I.mat.mapAlbedo > 0.5 || I.mat.mapRoughness > 0.5 || I.mat.mapMetallic > 0.5 || I.mat.mapNormal > 0.5 || I.mat.mapAo > 0.5 || I.mat.mapEmission > 0.5 || I.mat.mapSheen > 0.5 || I.mat.mapOpacity > 0.5 || I.mat.mapCoat > 0.5 || I.mat.mapTransmission > 0.5 || I.mat.hairShift > 0.5 || fldOn)
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
         const int wrapM = int(I.mat.wrapBump + 0.5) & 3;
         const MapX mx = mapX(I.mat);
         const float3 mb = mapBase(mx, uv, lp);
#define MAPT(slot) mapSample(maps[int(slot) - 1], mx, mb, n, float3(0.0), float3(0.0), lod)
         if (I.mat.mapAlbedo > 0.5) base *= mapLin(MAPT(I.mat.mapAlbedo).rgb);
         if (I.mat.mapRoughness > 0.5) rough = clamp(I.mat.roughness * MAPT(I.mat.mapRoughness).r, 0.045, 1.0);
         if (I.mat.mapMetallic > 0.5) metal = clamp(I.mat.metallic * MAPT(I.mat.mapMetallic).r, 0.0, 1.0);
         if (I.mat.mapEmission > 0.5) emMap = mapLin(MAPT(I.mat.mapEmission).rgb);
         if (I.mat.mapSheen > 0.5) shW *= MAPT(I.mat.mapSheen).r;
         if (I.mat.mapCoat > 0.5) ccW *= MAPT(I.mat.mapCoat).r;
         float alpha = 1.0 - I.mat.clear;
         if (I.mat.mapOpacity > 0.5) alpha *= MAPT(I.mat.mapOpacity).r;
#undef MAPT
#if FLD_MAT_ANY
         // Field material program (row 1.3): after the maps, before the cut; it replaces what it writes. Height is not
         // traced (no screen derivatives at a ray hit); the normal goes through the triangle's uv frame below.
         float3 fldNrm = float3(0.0, 0.0, 1.0);
         float2 fldDh = float2(0.0); float3 fldE1 = float3(0.0), fldE2 = float3(0.0);
         if (fldOn)
         {
            FldMat fo;
            fo.base = base; fo.roughness = rough; fo.metallic = metal; fo.normal = float3(0.0, 0.0, 1.0); fo.height = 0.0;
            fo.emission = float3(I.mat.emission) * emMap; fo.opacity = alpha; fo.mask = 1.0;
            fo.prm0 = float4(I.mat.p0, I.mat.p1, I.mat.p2, I.mat.p3); fo.prm1 = float4(I.mat.p4, I.mat.p5, I.mat.p6, I.mat.p7); fo.prm2 = float4(I.mat.p8, I.mat.p9, I.mat.p10, I.mat.p11); fo.prm3 = float4(I.mat.p12, I.mat.p13, I.mat.p14, I.mat.p15);
            float3 fldW = float3(0.0);
            if (I.wr != 0)
            {
               const device float* wp2 = reinterpret_cast<const device float*>(I.wr);
               fldW = float3(wp2[ix.x * 3], wp2[ix.x * 3 + 1], wp2[ix.x * 3 + 2]) * w3.x + float3(wp2[ix.y * 3], wp2[ix.y * 3 + 1], wp2[ix.y * 3 + 2]) * w3.y + float3(wp2[ix.z * 3], wp2[ix.z * 3 + 1], wp2[ix.z * 3 + 2]) * w3.z;
            }
            const float4 fldG = float4(backHit ? 1.0 : 0.0, 0.0, fldW.x, fldW.y);
            const float4 fldA = float4(fldW.z, 0.0, 0.0, 0.0);
            const float4 fldI = float4(I.mat.fimg0, I.mat.fimg1, I.mat.fimg2, I.mat.fimg3);
            fldMatDispatch(int(abs(I.mat.program) + 0.5), fo, lp, n, uv, f.fld.x, p, -d, fldG, fldA, maps, fldI, lod);
            if (b == 0 && !hairM && fldMatHasHeight(int(abs(I.mat.program) + 0.5)))
            {
               // Row 2.6 bump: no screen derivatives at a ray hit, so the height is read twice more, a step along
               // each triangle edge (about a pixel wide at this distance); the two differences are the gradient.
               const float3 le1 = q1 - p0, le2 = q2 - p0;
               const float3 we1 = (I.normalM * float4(le1, 0.0)).xyz, we2 = (I.normalM * float4(le2, 0.0)).xyz;
               const float stp = max(h.distance, 1e-3) * 1e-3;
               const float k1 = stp / max(length(we1), 1e-12), k2 = stp / max(length(we2), 1e-12);
               FldMat fa = fo, fb = fo;
               fldMatDispatch(int(abs(I.mat.program) + 0.5), fa, lp + le1 * k1, n, uv + (t1 - t0) * k1, f.fld.x, p + we1 * k1, -d, fldG, fldA, maps, fldI, lod);
               fldMatDispatch(int(abs(I.mat.program) + 0.5), fb, lp + le2 * k2, n, uv + (t2 - t0) * k2, f.fld.x, p + we2 * k2, -d, fldG, fldA, maps, fldI, lod);
               fldDh = float2(fa.height - fo.height, fb.height - fo.height);
               fldE1 = we1 * k1; fldE2 = we2 * k2;
            }
            base = fo.base; rough = clamp(fo.roughness, 0.045, 1.0); metal = clamp(fo.metallic, 0.0, 1.0);
            fldEm = fo.emission; fldEmSet = true; alpha = fo.mask < 0.5 ? 0.0 : fo.opacity; fldNrm = fo.normal;
         }
#endif
         if (alpha < 1.0 && skips < 16 && alpha < (I.mat.alphaCutoff > 0.0 ? I.mat.alphaCutoff : rnd(seed)))
         {
            // Cut out or not covered: the ray goes on through, the same bounce.
            skips++; if (b == 0) cov = 0.0;
            o = p + d * f.shQ.w; b--; continue;
         }
         if (I.mat.mapNormal > 0.5 && !hairM)
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
               const float3 s = mapNormalTS(maps[int(I.mat.mapNormal) - 1], mx, (int(I.mat.wrapBump + 0.5) & 4) != 0, I.mat.normalStrength, mapUv(mx, mb, n),
                                            float2(0.0), float2(0.0), lod, I.mat.detailScale, I.mat.detailStrength);
               n = normalize(tg * s.x + bt * s.y + n * s.z);
            }
         }
#if FLD_MAT_ANY
         if (fldOn && !hairM && any(fldNrm != float3(0.0, 0.0, 1.0)))
         {
            // The program's tangent-space normal in the triangle's mesh-uv frame (as the normal map's, unmapped uv).
            const float3 e1 = q1 - p0, e2 = q2 - p0;
            const float2 d1 = t1 - t0, d2 = t2 - t0;
            const float det = d1.x * d2.y - d2.x * d1.y;
            if (abs(det) > 1e-12)
            {
               float3 tg = normalize((I.normalM * float4((e1 * d2.y - e2 * d1.y) / det, 0.0)).xyz);
               const float3 bg = (I.normalM * float4((e2 * d1.x - e1 * d2.x) / det, 0.0)).xyz;
               tg = normalize(tg - n * dot(n, tg));
               float3 bt = cross(n, tg);
               if (dot(bt, bg) < 0.0) bt = -bt;
               if (backHit) { tg = -tg; bt = -bt; }
               n = normalize(tg * fldNrm.x + bt * fldNrm.y + n * fldNrm.z);
            }
         }
         if (any(fldDh != float2(0.0)))
         {
            // The same surface gradient as the raster: n' = |det| n - sign(det) (dh1 r1 + dh2 r2).
            const float3 r1 = cross(fldE2, n), r2 = cross(n, fldE1);
            const float det = dot(fldE1, r1);
            const float3 nb = abs(det) * n - sign(det) * (fldDh.x * r1 + fldDh.y * r2);
            if (dot(nb, nb) > 1e-30) n = normalize(nb);
         }
#endif
      }
      if (b == 0) { alb0 = base; n0 = n; dist0 = h.distance; }
#if FLD_MAT_ANY
      if (!fldEmSet) fldEm = float3(I.mat.emission) * emMap;
      if (any(fldEm > 0.0))
#else
      if (any(float3(I.mat.emission) > 0.0))
#endif
      {
         // Emission seen along the path. After a bounce, emitter sampling below could have found it too: power-heuristic MIS.
         float wE = 1.0;
         if (b > 0 && nEm > 0 && I.mat.emTri >= 0.0)
         {
            const EmTri t = ems[int(I.mat.emTri) + int(h.primitive_id)];
            const float3 gn = cross(t.e1.xyz, t.e2.xyz);
            const float cosL = abs(dot(gn, d)) / max(length(gn), 1e-12);
            const float pL = pEm * t.e1.w * h.distance * h.distance / max(cosL, 1e-6);
            wE = lastPdfB * lastPdfB / max(lastPdfB * lastPdfB + pL * pL, 1e-20);
         }
#if FLD_MAT_ANY
         const float3 eHit = T * fldEm * wE;
#else
         const float3 eHit = T * float3(I.mat.emission) * emMap * wE;
#endif
         if (b == 0) L0 += eHit; else L += eHit; // an emitter seen straight from the lens is never a firefly
      }
      const float3 V = -d; const float nDotV = max(dot(n, V), 1e-4);
      float3 shN = n; // row 5.1: the sheen lobe leans along the triangle's mesh-uv tangent
      if (shW > 0.0 && I.mat.sheenDir != 0.0 && I.uvb != 0)
      {
         const device float* uvs = reinterpret_cast<const device float*>(I.uvb);
         const float2 u0 = float2(uvs[ix.x * 2], uvs[ix.x * 2 + 1]);
         const float2 d1 = float2(uvs[ix.y * 2], uvs[ix.y * 2 + 1]) - u0, d2 = float2(uvs[ix.z * 2], uvs[ix.z * 2 + 1]) - u0;
         const float3 a0 = float3(vb[ix.x * 6], vb[ix.x * 6 + 1], vb[ix.x * 6 + 2]);
         const float3 e1 = float3(vb[ix.y * 6], vb[ix.y * 6 + 1], vb[ix.y * 6 + 2]) - a0, e2 = float3(vb[ix.z * 6], vb[ix.z * 6 + 1], vb[ix.z * 6 + 2]) - a0;
         const float det = d1.x * d2.y - d2.x * d1.y;
         if (abs(det) > 1e-12) shN = sheenTilt(n, (I.normalM * float4((e1 * d2.y - e2 * d1.y) / det, 0.0)).xyz, I.mat.sheenDir);
      }
      const float shNV = max(dot(shN, V), 1e-4);
      const float shMax = shW * maxc(float3(I.mat.sheenColor)), shV = shMax > 0.0 ? 1.0 - shMax * sheenE(shNV, I.mat.sheenRoughness) : 1.0;
      const float3 f0 = mix(float3(0.04 + I.mat.f0Extra), base, metal);
      const float3 msK = ptMs(f0, rough, nDotV, eLut) * iridGain(I.mat.iridescence, I.mat.iridIor, I.mat.iridThickness, nDotV, f0);
      // Subsurface and translucency (C9) take the diffuse share by chance: mode 1 walks the volume, mode 2 passes the
      // diffuse to the far side; otherwise the plain diffuse.
      int dm = 0;
      if (!hairM && metal < 1.0 && (I.mat.subsurface > 0.0 || I.mat.translucency > 0.0))
      {
         const float tlW = clamp(I.mat.translucency, 0.0, 1.0), ssW = clamp(I.mat.subsurface, 0.0, 1.0) * (1.0 - tlW);
         const float um = rnd(seed);
         dm = um < tlW ? 2 : (um < tlW + ssW && max(float3(I.mat.sssRadius).x, max(float3(I.mat.sssRadius).y, float3(I.mat.sssRadius).z)) > 1e-4 ? 1 : 0);
      }
      const float glW = hairM ? 0.0 : clamp(I.mat.transmission, 0.0, 1.0) * (1.0 - metal);
      if (glW > 0.0 && rnd(seed) < glW)
      {
         // Glass by its share, as the GL tracer: a GGX microfacet, then reflect or refract by the exact Fresnel; the
         // weight is Walter et al. 2007's for a microfacet drawn from D * nDotH. Transmission takes the base colour.
         const float ta = max(0.002, rough * rough);
         const float3 Hm = ggxSampleH(n, ta, rnd(seed), rnd(seed));
         const float cosI = dot(V, Hm);
         if (cosI <= 1e-5) break;
         const float ior = max(I.mat.ior, 1.0001), eta = backHit ? ior : 1.0 / ior;
         float3 nd; bool thru = false;
         if (rnd(seed) < ptFd(cosI, eta)) { nd = reflect(d, Hm); if (dot(ns, nd) <= 0.0) break; }
         else { nd = refract(d, Hm, eta); if (dot(nd, nd) < 1e-6 || dot(ns, nd) >= 0.0) break; T *= base; thru = true; }
         const float gV = max(abs(dot(n, V)), 1e-4), gL = max(abs(dot(n, nd)), 1e-4), gH = max(dot(n, Hm), 1e-4);
         T *= min(cosI * ptG1(gV, ta) * ptG1(gL, ta) / (gV * gH), 4.0);
         lastPdfB = 1e8; skyK = float3(1.0); // a sharp lobe: an emitter it finds counts in full
         if (b >= 2)
         {
            const float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
            if (rnd(seed) > q) break;
            T /= q;
         }
         o = p + (thru ? -ns : ns) * f.shQ.w; d = normalize(nd);
         continue;
      }
      if (b == 0) L += base * pow(1.0 - nDotV, 3.0) * f.eye.w; // Render 3D's rim, as the raster, so settling keeps the look
      for (int i = 0; i < int(f.fld.y + 0.5); i++) // ambient lights: the raster's flat, unshadowed fill, so settling keeps it
         if (int(LL[i].vec.w) == 3) L += T * base * (1.0 - metal) * LL[i].color.xyz * LL[i].terms.x;
      // direct light, the same model as the raster
      const int count = int(f.fld.y + 0.5);
      // next-event estimation: an emissive triangle (chance pEm, picked by power) or one light, picked uniformly and
      // weighted by the count and the chance of taking this branch (unbiased). Both branches only set up the sample;
      // one shadow ray and one BSDF evaluation below serve either (keeps the kernel small).
      float3 ld = float3(0.0, 1.0, 0.0), Le = float3(0.0); float tmax = 0.0, pL = -1.0; float2 lterms = float2(1.0);
      ptPickLight(f, LL, ems, nEm, pEm, count, p, seed, ld, Le, tmax, pL, lterms);
      // Hair: one place across the fibre per hit (the paper's integral over h), the same for the light and the bounce.
      const float hH = hairM ? rnd(seed) * 2.0 - 1.0 : 0.0;
      const float hBm = clamp(I.mat.roughness, 0.02, 1.0), hBn = -I.mat.hairRadialNeg;
      if ((hairM || (dot(n, ld) > 1e-5 && dot(ns, ld) > 0.0) || (dm == 2 && dot(n, ld) < -1e-5 && dot(ns, ld) < 0.0)) && any(Le > 0.0))
      {
         // Lights keep the live picture's shadow strength, so settling does not darken shadows the user already sees;
         // emitters are surfaces, fully blocked.
         if (pL > -1.5 && occludedAlpha(rtArgs.tlas, inst, maps, p + (dot(ns, ld) < 0.0 ? -ns : ns) * f.shQ.w, ld, tmax, f.shQ.w, seed)) Le *= pL > 0.0 ? 0.0 : 1.0 - f.shP.w;
         Le *= fogTr(u, p, ld, tmax);
         float pB = 0.0;
         float3 fe = hairM ? hairBsdf(V, ld, hT, float3(I.mat.hairSigma), hBm, hBn, I.mat.hairShift, I.mat.ior, hH, pB) * lterms.x
                  : dm == 2 ? ptEvalT(n, V, ld, base, metal, rough, f0, lterms, msK, pB)
                           : ptEval(n, V, ld, base, metal, rough, f0, dm == 1 ? float2(0.0, lterms.y) : lterms, msK, pB);
         if (shW > 0.0 || ccW > 0.0)
         {
            // Sheen and clearcoat on the light sample (the raster's lobes); bounces see the base BSDF.
            const float nL = max(dot(n, ld), 0.0), nH = max(dot(n, normalize(V + ld)), 0.0), vH = max(dot(V, normalize(V + ld)), 0.0);
            const float cF = 0.04 + 0.96 * pow(1.0 - nDotV, 5.0);
            fe *= 1.0 - ccW * cF;
            if (shW > 0.0)
               fe = fe * shV
                  + float3(I.mat.sheenColor) * (I.mat.sheenDir != 0.0 ? sheenLtc(max(dot(shN, ld), 0.0), shNV, dot(ld, V), I.mat.sheenRoughness) * max(dot(shN, ld), 0.0) : sheenLtc(nL, nDotV, dot(ld, V), I.mat.sheenRoughness) * nL) * shW * lterms.y;
            if (ccW > 0.0) fe += ccW * coatSpec(nH, nDotV, max(nL, 1e-4), vH, clamp(I.mat.coatRoughness, 0.02, 1.0)) * nL * lterms.y;
         }
         L += T * fe * Le * (pL > 0.0 ? pL / max(pL * pL + pB * pB, 1e-20) : 1.0); // emitters: power-heuristic MIS
      }
      // The HDRI as a light: one direction drawn in proportion to the sky's light, weighted against the bounce
      // (power heuristic), so a small bright sun is found at every hit instead of by a lucky bounce. The sample
      // carries what the bounce would: sheen, the coat's share, and the lobe's sky scale (skyK).
      if (!hairM && dm != 1 && u.em.z > 0.5)
      {
         envMis = 1.0;
         float pE = 0.0;
         const float3 ed = envPick(f, ecdf, float2(rnd(seed), rnd(seed)), pE);
         const float eL = dot(n, ed);
         const bool under = dm == 2 && eL < 0.0;
         if (pE > 0.0 && abs(eL) > 1e-5 && (dot(ns, ed) > 0.0) != under)
         {
            float pB = 0.0;
            float3 fe = dm == 2 ? ptEvalT(n, V, ed, base, metal, rough, f0, float2(1.0), msK, pB) : ptEval(n, V, ed, base, metal, rough, f0, float2(1.0), msK, pB);
            const float nLe = max(eL, 0.0);
            if (shW > 0.0) fe = fe * shV + float3(I.mat.sheenColor) * (I.mat.sheenDir != 0.0 ? sheenLtc(max(dot(shN, ed), 0.0), shNV, dot(ed, V), I.mat.sheenRoughness) * max(dot(shN, ed), 0.0) : sheenLtc(nLe, nDotV, dot(ed, V), I.mat.sheenRoughness) * nLe) * shW;
            fe *= mix(float3(1.0), f.ambient.xyz * 3.0, clamp((1.0 - ptProbs(f0, metal, nDotV).x) * abs(eL) / 3.14159265 / max(pB, 1e-6), 0.0, 1.0));
            float pBm = pB;
            if (ccW > 0.0)
            {
               const float cR = clamp(I.mat.coatRoughness, 0.02, 1.0), cP = clamp(ccW * (0.04 + 0.96 * pow(1.0 - nDotV, 5.0)), 0.0, 0.95);
               const float3 hc = normalize(V + ed);
               const float cnH = max(dot(n, hc), 0.0), cvH = max(dot(V, hc), 1e-4);
               fe = fe * (1.0 - cP) + (eL > 0.0 ? float3(ccW * coatSpec(cnH, nDotV, max(eL, 1e-4), cvH, cR) * eL) : float3(0.0));
               pBm = cP * (eL > 0.0 ? distGGX(cnH, cR) * cnH / (4.0 * cvH) : 0.0) + (1.0 - cP) * pB;
            }
            if (any(fe > 0.0) && !occludedAlpha(rtArgs.tlas, inst, maps, p + (dot(ns, ed) < 0.0 ? -ns : ns) * f.shQ.w, ed, 1e4, f.shQ.w, seed))
               L += T * fe * sampleEnv(f, et, ed, 0.0) * fogTr(u, p, ed, 1e4) * (pE / max(pE * pE + pBm * pBm, 1e-20));
         }
      }
      if (hairM)
      {
         const float3 nd = hairSample(V, hT, float3(I.mat.hairSigma), hBm, hBn, I.mat.hairShift, I.mat.ior, hH, float4(rnd(seed), rnd(seed), rnd(seed), rnd(seed)));
         float pdH = 0.0;
         const float3 fH = hairBsdf(V, nd, hT, float3(I.mat.hairSigma), hBm, hBn, I.mat.hairShift, I.mat.ior, hH, pdH);
         if (pdH <= 1e-6) break;
         T *= min(fH / pdH, float3(4.0));
         lastPdfB = pdH; skyK = float3(1.0);
         if (b >= 2)
         {
            const float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
            if (rnd(seed) > q) break;
            T /= q;
         }
         o = p + (dot(ns, nd) < 0.0 ? -ns : ns) * f.shQ.w; d = nd;
         continue;
      }
      if (dm == 1)
      {
         const float2 lq = ptProbs(f0, metal, nDotV);
         if (rnd(seed) < lq.x)
         {
            // The specular lobe on its own (the walk takes the diffuse share).
            const float3 nd = reflect(d, ggxSampleH(n, max(0.002, rough * rough), rnd(seed), rnd(seed)));
            float pd = 0.0;
            const float3 fs = ptEval(n, V, nd, base, metal, rough, f0, float2(0.0, 1.0), msK, pd);
            const float ps = pd - lq.y * max(dot(n, nd), 0.0) / 3.14159265;
            if (ps <= 1e-6 || dot(ns, nd) <= 0.0) break;
            T *= fs / ps; lastPdfB = pd; skyK = float3(1.0);
            o = p + ns * f.shQ.w; d = nd;
         }
         else
         {
            // Random walk: in through a cosine below the surface, free flights of mean free path ss.xyz (one channel
            // drawn per step, weighted by the mixture of the three pdfs), isotropic scattering at a single-scatter albedo
            // inverted from the base colour (so the walk's total albedo is close to it), out where a flight meets a
            // surface. There the light leaves as a white Lambert surface (the colour is in the throughput).
            const float3 Fv = ptF(nDotV, f0);
            float3 Tw = (float3(1.0) - Fv) * (1.0 - metal) / max(lq.y, 1e-3);
            const float3 sg = 1.0 / max(float3(I.mat.sssRadius) * SSS_D, float3(1e-4));
            const float3 A = clamp(base, 0.0, 0.999);
            const float3 qa = 4.09712 + 4.20863 * A - sqrt(9.59217 + 41.6808 * A + 17.7126 * A * A);
            const float3 alb = 1.0 - qa * qa;
            float3 wp = p - ns * f.shQ.w, wd = cosineDir(-ns, rnd(seed), rnd(seed));
            bool out = false; float3 q = p, no = ns;
            for (int s = 0; s < 256 && !out; s++)
            {
               // The channel is drawn in proportion to the walk's throughput so far, so a channel that has kept its
               // weight (the long red path in skin) is not starved by draws from the short ones.
               const float3 cw = max(Tw, float3(1e-6)) / max(Tw.x + Tw.y + Tw.z, 3e-6);
               const float u0 = rnd(seed);
               const float sc = u0 < cw.x ? sg.x : (u0 < cw.x + cw.y ? sg.y : sg.z);
               const float tt = -log(max(1.0 - rnd(seed), 1e-7)) / sc;
               ray wr(wp, wd, 0.0, tt);
               intersection_result<triangle_data, instancing> wh = it.intersect(wr, rtArgs.tlas);
               if (wh.type == intersection_type::triangle)
               {
                  const float3 tr = exp(-sg * wh.distance);
                  Tw *= tr / max(dot(cw, tr), 1e-20);
                  q = wp + wd * wh.distance;
                  constant RInst& J = inst[wh.instance_id];
                  const device uint* jb = reinterpret_cast<const device uint*>(J.ib);
                  const device float* jv = reinterpret_cast<const device float*>(J.vb);
                  const uint3 jx = uint3(jb[wh.primitive_id * 3], jb[wh.primitive_id * 3 + 1], jb[wh.primitive_id * 3 + 2]);
                  const float2 jc = wh.triangle_barycentric_coord;
                  const float3 jw = float3(1.0 - jc.x - jc.y, jc.x, jc.y);
                  const float3 jn = float3(jv[jx.x * 6 + 3], jv[jx.x * 6 + 4], jv[jx.x * 6 + 5]) * jw.x
                                  + float3(jv[jx.y * 6 + 3], jv[jx.y * 6 + 4], jv[jx.y * 6 + 5]) * jw.y
                                  + float3(jv[jx.z * 6 + 3], jv[jx.z * 6 + 4], jv[jx.z * 6 + 5]) * jw.z;
                  const float3 gn = normalize((J.normalM * float4(jn, 0.0)).xyz);
                  no = dot(gn, wd) > 0.0 ? gn : -gn;
                  out = true;
               }
               else
               {
                  const float3 tr = exp(-sg * tt);
                  Tw *= alb * sg * tr / max(dot(cw, sg * tr), 1e-20);
                  wp += wd * tt;
                  const float cz = 1.0 - 2.0 * rnd(seed), ph = 6.28318530718 * rnd(seed), sz = sqrt(max(1.0 - cz * cz, 0.0));
                  wd = float3(sz * cos(ph), sz * sin(ph), cz);
               }
            }
            if (!out) break;
            T *= min(Tw, float3(8.0));
            float3 eld = float3(0.0, 1.0, 0.0), eLe = float3(0.0); float etm = 0.0, epL = -1.0; float2 elt = float2(1.0);
            ptPickLight(f, LL, ems, nEm, pEm, count, q, seed, eld, eLe, etm, epL, elt);
            const float ec = dot(no, eld);
            if (ec > 1e-5 && any(eLe > 0.0))
            {
               if (epL > -1.5 && occludedAlpha(rtArgs.tlas, inst, maps, q + no * f.shQ.w, eld, etm, f.shQ.w, seed)) eLe *= epL > 0.0 ? 0.0 : 1.0 - f.shP.w;
               const float epB = ec / 3.14159265;
               L += T * (epB * elt.x) * eLe * (epL > 0.0 ? epL / max(epL * epL + epB * epB, 1e-20) : 1.0);
            }
            d = cosineDir(no, rnd(seed), rnd(seed)); o = q + no * f.shQ.w;
            lastPdfB = max(dot(no, d), 1e-4) / 3.14159265; skyK = f.ambient.xyz * 3.0;
         }
         if (b >= 2)
         {
            const float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
            if (rnd(seed) > q) break;
            T /= q;
         }
         continue;
      }
      // next direction: one lobe picked by Render 3D's lobe weights, weighted by the full BSDF over the mixture pdf
      const float2 lp = ptProbs(f0, metal, nDotV);
      const float a = max(0.002, rough * rough);
      // The clearcoat is a layer on top: it takes the bounce with its Fresnel share (pC), the base the rest; the
      // base's (1 - pC) attenuation and that choice cancel, so the base branch weighs as before.
      const float ccR = clamp(I.mat.coatRoughness, 0.02, 1.0);
      const float pC = ccW > 0.0 ? clamp(ccW * (0.04 + 0.96 * pow(1.0 - nDotV, 5.0)), 0.0, 0.95) : 0.0;
      const bool coatB = pC > 0.0 && rnd(seed) < pC;
      float3 nd;
      if (coatB) { nd = reflect(d, ggxSampleH(n, ccR * ccR, rnd(seed), rnd(seed))); skyK = float3(1.0); }
      else if (rnd(seed) < lp.x) { nd = reflect(d, ggxSampleH(n, a, rnd(seed), rnd(seed))); skyK = float3(1.0); }
      else { nd = cosineDir(dm == 2 ? -n : n, rnd(seed), rnd(seed)); skyK = f.ambient.xyz * 3.0; }
      float pdfB = 0.0;
      float3 fB = dm == 2 ? ptEvalT(n, V, nd, base, metal, rough, f0, float2(1.0), msK, pdfB) : ptEval(n, V, nd, base, metal, rough, f0, float2(1.0), msK, pdfB);
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
         if (pdfB <= 1e-6 || (dot(ns, nd) <= 0.0) != (dm == 2 && dot(n, nd) < 0.0)) break; // a mapped normal can send the bounce under the surface
         if (shW > 0.0) // the bounce sees the sheen too (the cosine half of the mixture covers its wide lobe)
         {
            const float nL = max(dot(n, nd), 0.0);
            fB = fB * shV
               + float3(I.mat.sheenColor) * (I.mat.sheenDir != 0.0 ? sheenLtc(max(dot(shN, nd), 0.0), shNV, dot(nd, V), I.mat.sheenRoughness) * max(dot(shN, nd), 0.0) : sheenLtc(nL, nDotV, dot(nd, V), I.mat.sheenRoughness) * nL) * shW;
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
      o = p + (dot(ns, nd) < 0.0 ? -ns : ns) * f.shQ.w; d = nd;
   }
   L = (min(L, float3(30.0)) + L0) * chan; // firefly guard; chan weights a chromatic-aberration sample's one channel
   const bool add = u.info.x > 0.5;
   const float lum = dot(L, float3(0.2126, 0.7152, 0.0722));
   PtOut out;
   out.acc = (add ? prev.read(px) : float4(0.0)) + float4(L, cov);
   out.aux = (add ? prevAux.read(px) : float4(0.0)) + float4(alb0 * cov, cov > 0.0 ? lum * lum : 0.0);
   out.gd = (add ? prevGd.read(px) : float4(0.0)) + float4(n0 * cov, dist0 * cov);
   return out;
}
struct PtResU { float4 p; float4 clear; }; // p: sample count, tonemap, exposure
fragment float4 ptres_fs(SkyOut in [[stage_in]], constant PtResU& u [[buffer(0)]], texture2d<float> acc [[texture(0)]],
                         texture2d<float> aux [[texture(1)]], texture2d<float> dn [[texture(2)]], texture2d<float> gd [[texture(3)]])
{
   const uint2 px = uint2(in.pos.xy);
   const float4 a = acc.read(px);
   const float cov = a.a / u.p.x;
   float3 col = a.rgb / max(a.a, 1e-4); // mean radiance of the samples that hit
   if (u.p.w > 0.5 && gd.read(px).w > 0.0) // denoised lighting times the first-hit albedo (dn_prep_k's split)
      col = dn.read(px).rgb * max(aux.read(px).rgb / max(a.a, 1.0), float3(0.001));
   col *= u.p.z;
   // Blend hits with the background in linear light, as a camera does (a 60x highlight on a third of the lens
   // still reads white), keeping a pixel no sample hit exactly the clear colour and a fully hit one unchanged.
   const float3 cl = toLinear(u.clear.xyz);
   const float3 bg = toSrgb(toneMap(cl, int(u.p.y)));
   return float4(max(toSrgb(toneMap(cov * col + (1.0 - cov) * cl, int(u.p.y))) - (1.0 - cov) * bg, float3(0.0)) + (1.0 - cov) * u.clear.xyz, 1.0);
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
// Same varyings as VOut (uv, lp zero) so rp_fs and the MRT fragment link with it; Metal refuses a fragment input the
// vertex stage does not write.
struct VOutP { float4 pos [[position]]; float3 wp; float3 n; float2 uv; float3 lp; float ps [[point_size]]; };
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
   float3 c = (float3(m.mat.base) * (f.ambient.xyz * 1.4 + lc * max(dot(n, ld), 0.0)) + float3(m.mat.emission)) * f.params.x;
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
   VOutP o; o.pos = c; o.wp = w.xyz; o.n = normalize(f.eye.xyz - w.xyz); o.uv = float2(0.0); o.lp = float3(0.0);
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
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m * float4(n, 0.0)).xyz; o.uv = float2(0.0); o.lp = p; o.cw = float3(0.0); return o;
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
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m * float4(n, 0.0)).xyz; o.uv = float2(0.0); o.lp = p; o.cw = float3(0.0); return o; // uniform scale: the model's 3x3 turns normals
}
// Runs: consecutive instances sharing a mesh and material drawn in one instanced call; per-copy data from a buffer.
vertex VOut rr_vs(uint vid [[vertex_id]], uint iid [[instance_id]], const device float* v [[buffer(0)]], constant FrameU& f [[buffer(1)]],
                  const device InstU* ms [[buffer(10)]], constant uint& first [[buffer(11)]], const device float* uvs [[buffer(12)]],
                  const device float* wr [[buffer(13)]])
{
   const device InstU& m = ms[first + iid];
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   const float3 n = float3(v[vid * 6 + 3], v[vid * 6 + 4], v[vid * 6 + 5]);
   const float4 w = m.model * float4(p, 1.0);
   float4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   c.y = -c.y;
   VOut o; o.pos = c; o.wp = w.xyz; o.n = (m.normalM * float4(n, 0.0)).xyz; o.uv = float2(uvs[vid * 2], uvs[vid * 2 + 1]); o.lp = p; o.cw = float3(wr[vid * 3], wr[vid * 3 + 1], wr[vid * 3 + 2]); return o;
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
struct BgU { float4x4 invVP; float4 p; float4 clr; }; // p: width, height, MRT, 1 = plain clear colour under fog (row 5.4); clr linear
fragment float4 bg_fs(SkyOut in [[stage_in]], constant BgU& u [[buffer(0)]], constant FrameU& f [[buffer(1)]], texture2d<float> et [[texture(9)]],
                      texture2d<float> visF [[texture(6)]])
{
   const float2 ndc = float2(2.0 * in.pos.x / u.p.x - 1.0, 2.0 * in.pos.y / u.p.y - 1.0); // row 0 is GL's bottom
   const float4 pw = u.invVP * float4(ndc, 1.0, 1.0);
   if (u.p.w > 0.5)
   {
      // Row 5.4: no HDRI behind, but the fog still lies in front of the clear colour; where it is thin keep the clear.
      const float4 a = visF.read(uint2(min(int2(in.pos.xy) / 2, int2(visF.get_width(), visF.get_height()) - 1)));
      const float3 c = u.clr.rgb * a.a + a.rgb * f.params.x;
      if (u.p.z > 0.5) return float4(c, a.a < 0.999 ? 1.0 : 0.0);
      return float4(toSrgb(toneMap(c, int(f.params.y))), 1.0);
   }
   float3 c = sampleEnv(f, et, normalize(pw.xyz / pw.w - f.eye.xyz), 0.0);
   if (visF.get_width() > 1) { const float4 a = visF.read(uint2(min(int2(in.pos.xy) / 2, int2(visF.get_width(), visF.get_height()) - 1))); c = c * a.a + a.rgb; } // row 5.4
   c *= f.params.x;
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
      float fld[4];
   };
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

   // The material record (Render3DMaterial.h) as the shader's InstU / RInst carry it.
   struct InstU { float model[16], normalM[16]; R3dMatH mat; };
   struct RInstH { uint64_t vb, ib, uvb, wr; float normalM[16]; R3dMatH mat; };
   static_assert(sizeof(InstU) == 496 && sizeof(RInstH) == 464, "InstU / RInst layout as the shader's");
   void Lin3x(const float* c, float* o) { for (int i = 0; i < 3; i++) o[i] = ToLin(c[i]); }
   // The surface the GI and reflection rays see: base, metal, roughness, ior, emission (no maps, sheen or hair).
   void FillMatRays(const engine::Instance& in, R3dMatH& m)
   {
      Lin3x(in.baseSrgb, m.base); m.metallic = in.metallic; m.roughness = in.roughness; m.ior = in.ior;
      std::memcpy(m.emission, in.emission, 12);
      m.program = (float)(in.fieldSeeThrough ? -in.fieldProgram : in.fieldProgram); // the rays run it too (row 1.4)
      m.fimg0 = (float)(in.fieldImage[0] + 1); m.fimg1 = (float)(in.fieldImage[1] + 1);
      m.fimg2 = (float)(in.fieldImage[2] + 1); m.fimg3 = (float)(in.fieldImage[3] + 1);
      std::memcpy(&m.p0, in.fieldParams, sizeof(in.fieldParams)); // row 3.4: knobs live, no relink
      m.strandFrom = (float)(in.strandFrom + 1); // row 5.2: the shadow rays tell strands from solids
   }
   // The whole material, encoded so all zeros is the plain material (see Render3DMaterial.h).
   void FillMat(const engine::Instance& in, R3dMatH& m)
   {
      FillMatRays(in, m);
      m.transmission = in.transmission;
      m.mapAlbedo = (float)(in.mapAlbedo + 1); m.mapRoughness = (float)(in.mapRoughness + 1);
      m.mapMetallic = (float)(in.mapMetallic + 1); m.mapNormal = (float)(in.mapNormal + 1);
      m.mapAo = (float)(in.mapAo + 1); m.mapEmission = (float)(in.mapEmission + 1); m.mapSheen = (float)(in.mapSheen + 1);
      m.mapOpacity = (float)(in.mapOpacity + 1); m.mapCoat = (float)(in.mapClearcoat + 1); m.mapTransmission = (float)(in.mapTransmission + 1);
      m.sheen = in.sheen; m.sheenRoughness = in.sheenRoughness; m.sheenDir = in.sheenDir; std::memcpy(m.sheenColor, in.sheenColor, 12);
      m.normalStrength = in.normalStrength; m.wrapBump = (float)((in.wrap & 3) + (in.normalBump ? 4 : 0));
      m.mapSpace = (float)in.mapSpace; m.triplanar = in.triplanar; m.alphaCutoff = in.alphaCutoff;
      m.clear = 1.0f - std::min(std::max(in.opacity, 0.0f), 1.0f);
      const float r = (in.ior - 1.0f) / (in.ior + 1.0f);
      const float f0 = std::min(std::max(r * r * 2.0f * in.specular, 0.0f), 1.0f);
      for (int k = 0; k < 3; k++)
      {
         m.mapScale[k] = in.mapScale[k]; m.mapRotate[k] = in.mapRotate[k]; m.mapTranslate[k] = in.mapTranslate[k];
         m.boxLo[k] = in.objLo[k]; m.boxHi[k] = in.objHi[k];
         m.hairSigma[k] = in.hairSigma[k]; m.sssRadius[k] = in.sssRadius[k];
      }
      m.coat = in.clearcoat; m.coatRoughness = in.clearcoatRoughness; m.f0Extra = f0 - 0.04f;
      m.breakup = in.breakup; m.detailScale = in.detailScale; m.detailStrength = in.detailStrength;
      m.hairFrom = (float)in.hairFrom; m.hairRadialNeg = -std::max(in.hairRadial, 0.0f); m.hairShift = in.hairShift;
      m.subsurface = in.subsurface; m.translucency = in.translucency;
      m.iridescence = in.iridescence; m.iridIor = in.iridescenceIor; m.iridThickness = in.iridescenceThickness;
   }
   // A run the depth prepass leaves out: it cuts pixels away, so its depth is known only once it is shaded.
   bool SeeThrough(const InstU& u) { return u.mat.clear > 0.0f || u.mat.mapOpacity > 0.5f || u.mat.alphaCutoff > 0.0f || u.mat.program < -0.5f; }

   class MetalRaster final : public RasterRenderer
   {
   public:
      MetalRaster(id<MTLDevice> dev, id<MTLCommandQueue> q) : mDev(dev), mQueue(q)
      {
         NSError* err = nil;
         MTLCompileOptions* co = [MTLCompileOptions new];
         co.languageVersion = MTLLanguageVersion2_3;
         co.preprocessorMacros = AbMacros(nil);
         mLib = [dev newLibraryWithSource:kRasterShader options:co error:&err];
         if (!mLib)
            EngineFail(@"raster shader", err);
      }
      bool Ok() const { return mLib != nil; }
   private:
      // Field material programs (row 1.3): their library spliced in at the marker; a new set recompiles the shader and
      // drops every pipeline, which the next Pipeline / PtPipeline / DepthOfField / EnsureInv rebuild from it. A
      // library that fails to compile (row 1.5): each program is compiled alone, the ones that fail are marked in the
      // registry (their error shows on the Field Pixel node, their materials draw the knobs) and the rest is built
      // once. mFieldKey becomes the key of what was built, which is the key the engine sends next frame.
      NSString* mSrc = nil;
      unsigned long long mFieldKey = 0;
      NSString* Src() const { return mSrc ? mSrc : kRasterShader; }
      static NSString* WithLib(const std::string& msl)
      {
         if (msl.empty()) return kRasterShader;
         return [kRasterShader stringByReplacingOccurrencesOfString:@"//@FLD_MAT_LIB@"
                                                         withString:[NSString stringWithUTF8String:msl.c_str()]];
      }
      void FieldLib(const engine::Frame& fr)
      {
         if (fr.fieldMslKey == mFieldKey)
            return;
         mFieldKey = fr.fieldMslKey;
         NSString* src = WithLib(fr.fieldMsl);
         NSError* err = nil;
         MTLCompileOptions* co = [MTLCompileOptions new];
         co.languageVersion = MTLLanguageVersion2_3;
         co.preprocessorMacros = AbMacros(nil);
         id<MTLLibrary> lib = [mDev newLibraryWithSource:src options:co error:&err];
         if (!lib)
         {
            // a user's program, not an engine failure: logged, its error goes to its node (registry), and
            // rhi::LastError stays free for the engine's own shaders
            NSLog(@"field material library: %@", err);
            std::vector<int> ids;
            for (const engine::Instance& in : fr.instances)
               if (in.fieldProgram != 0) ids.push_back(in.fieldProgram);
            Field::IsolateFailingPrograms(ids, Field::MaterialLang::Msl, [&](const std::string& one) -> std::string {
               NSError* e1 = nil;
               return [mDev newLibraryWithSource:WithLib(one) options:co error:&e1] ? std::string() : "MSL: " + FirstError(e1);
            });
            const std::string rest = Field::BuildMaterialLibrary(ids, Field::MaterialLang::Msl, mFieldKey);
            if (rest.empty() && mSrc == nil) return; // already the plain shader
            src = WithLib(rest);
            lib = [mDev newLibraryWithSource:src options:co error:&err];
            if (!lib && !rest.empty()) { src = kRasterShader; lib = [mDev newLibraryWithSource:src options:co error:&err]; }
            if (!lib) return;
         }
         mLib = lib;
         mSrc = src;
         mFieldLibBuilds++;
         mPso = nil; mFmt = MTLPixelFormatInvalid;
         mPtPso = nil; mPtResPso = nil; mPtFmt = MTLPixelFormatInvalid;
         mDofPso = nil;
         mInvTried = false; mPrePso = nil; mPsoRunInv = nil; mPsoRunDirInv = nil;
      }
      int mFieldLibBuilds = 0;
   public:
      int FieldLibraryBuilds() const override { return mFieldLibBuilds; }
   private:
      // tvb/tib/tuvb/tcount: the rays' mesh (row 4.3, a displaced copy), nil when the rays see the drawn one.
      struct Gpu
      {
         id<MTLBuffer> vb = nil, ib = nil, uvb = nil; NSUInteger count = 0; float lo[3], hi[3];
         id<MTLBuffer> tvb = nil, tib = nil, tuvb = nil; NSUInteger tcount = 0;
         id<MTLBuffer> wb = nil, twb = nil; // row 4.4: curvature, cavity per vertex (zeros when not baked)
         id<MTLBuffer> RW() const { return tvb ? twb : wb; }
         id<MTLBuffer> RV() const { return tvb ? tvb : vb; }
         id<MTLBuffer> RI() const { return tvb ? tib : ib; }
         id<MTLBuffer> RU() const { return tvb ? tuvb : uvb; }
         NSUInteger RC() const { return tvb ? tcount : count; }
      };
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
            o.mask = in.castShadow ? 0xFF : 0xFE; // bit 0 is what shadow rays test
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
      // E(mu, r): directional albedo of the settle tracer's specular lobe (GGX, height-correlated Smith) at F = 1,
      // by quadrature over half vectors drawn from the NDF. 16x16, x = nV, y = roughness; ptMs reads it.
      id<MTLTexture> ELut()
      {
         if (mELut)
            return mELut;
         const int kN = 16;
         std::vector<uint16_t> e((size_t)kN * kN);
         for (int y = 0; y < kN; y++)
            for (int x = 0; x < kN; x++)
            {
               const float mu = (x + 0.5f) / kN, r = (y + 0.5f) / kN;
               const float a = std::max(0.002f, r * r), a2 = a * a;
               const int n1 = 64, n2 = 128;
               double sum = 0.0;
               for (int i = 0; i < n1; i++)
                  for (int j = 0; j < n2; j++)
                  {
                     const float u1 = (i + 0.5f) / n1, ph = 6.2831853f * (j + 0.5f) / n2;
                     const float ct = std::sqrt((1.0f - u1) / (1.0f + (a2 - 1.0f) * u1)), st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
                     const float vh = std::sqrt(std::max(0.0f, 1.0f - mu * mu)) * st * std::cos(ph) + mu * ct;
                     const float nl = 2.0f * vh * ct - mu;
                     if (vh <= 0.0f || nl <= 0.0f)
                        continue;
                     const float gV = nl * std::sqrt(a2 + (1.0f - a2) * mu * mu), gL = mu * std::sqrt(a2 + (1.0f - a2) * nl * nl);
                     sum += (0.5f / (gV + gL)) * nl * 4.0f * vh / std::max(ct, 1e-6f); // Vis * nL / pdf, pdf = D nH / (4 vH)
                  }
               e[(size_t)y * kN + x] = (uint16_t)std::lround(std::min(1.0, std::max(0.0, sum / (n1 * n2))) * 65535.0);
            }
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Unorm width:kN height:kN mipmapped:NO];
         d.usage = MTLTextureUsageShaderRead;
         mELut = [mDev newTextureWithDescriptor:d];
         [mELut replaceRegion:MTLRegionMake2D(0, 0, kN, kN) mipmapLevel:0 withBytes:e.data() bytesPerRow:kN * sizeof(uint16_t)];
         return mELut;
      }
      id<MTLTexture> mELut = nil;
      // The HDRI's sampling table for the path tracer (envPick / envPdf in the shader): luminance times sin(theta)
      // on a 256 x 128 grid, as cumulative chances per row, over rows, and each cell's density.
      void BuildEnvCdf(const engine::Frame& fr)
      {
         const int NX = 256, NY = 128;
         std::vector<double> w((size_t)NX * NY, 0.0);
         double total = 0.0;
         for (int cy = 0; cy < NY; cy++)
         {
            const int y0 = cy * fr.envH / NY, y1 = std::max(y0 + 1, (cy + 1) * fr.envH / NY);
            const double st = std::sin(((double)cy + 0.5) / NY * 3.14159265358979);
            for (int cx = 0; cx < NX; cx++)
            {
               const int x0 = cx * fr.envW / NX, x1 = std::max(x0 + 1, (cx + 1) * fr.envW / NX);
               double sum = 0.0;
               for (int y = y0; y < y1 && y < fr.envH; y++)
                  for (int x = x0; x < x1 && x < fr.envW; x++)
                  {
                     const float* px = fr.envPixels + ((size_t)y * fr.envW + x) * 3;
                     const float l = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
                     sum += std::isfinite(l) ? std::min(std::max(l, 0.0f), 65504.0f) : 0.0f;
                  }
               w[(size_t)cy * NX + cx] = sum / ((double)(y1 - y0) * (x1 - x0)) * st;
               total += w[(size_t)cy * NX + cx];
            }
         }
         mEnvCdf = nil;
         if (!(total > 0.0))
            return;
         const double floorW = total / ((double)NX * NY) * 1e-3; // no direction has zero chance
         total += floorW * NX * NY;
         std::vector<float> t((size_t)NX * (2 * NY + 1), 0.0f);
         double rows = 0.0;
         for (int cy = 0; cy < NY; cy++)
         {
            double row = 0.0;
            for (int cx = 0; cx < NX; cx++) row += w[(size_t)cy * NX + cx] + floorW;
            double run = 0.0;
            for (int cx = 0; cx < NX; cx++)
            {
               const double c = w[(size_t)cy * NX + cx] + floorW;
               run += c;
               t[(size_t)cy * NX + cx] = cx == NX - 1 ? 1.0f : (float)(run / row);
               t[(size_t)(NY + 1 + cy) * NX + cx] = (float)(c / total * NX * NY);
            }
            rows += row;
            t[(size_t)NY * NX + cy] = cy == NY - 1 ? 1.0f : (float)(rows / total);
         }
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:NX height:2 * NY + 1 mipmapped:NO];
         d.usage = MTLTextureUsageShaderRead;
         d.storageMode = MTLStorageModeShared;
         mEnvCdf = [mDev newTextureWithDescriptor:d];
         [mEnvCdf replaceRegion:MTLRegionMake2D(0, 0, NX, 2 * NY + 1) mipmapLevel:0 withBytes:t.data() bytesPerRow:(NSUInteger)NX * 4];
      }
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
            BuildEnvCdf(fr);
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
         if (!cur.tvb && m.tracePosNormal.empty() && cur.count == m.indices.size() && cur.vb.length == std::max<size_t>(m.posNormal.size() * 4, 4) &&
             std::memcmp(cur.ib.contents, m.indices.data(), m.indices.size() * 4) == 0)
         {
            // Same topology: write the vertices in place (the last frame has completed) and refit next frame.
            std::memcpy(cur.vb.contents, m.posNormal.data(), m.posNormal.size() * 4);
            cur.uvb = UvBuffer(m);
            cur.wb = WearBuffer(m.wear, m.posNormal.size() / 6);
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
      // Three floats per vertex (curvature, cavity, attr; zeros when the mesh has none), read by rr_vs and the ray hits.
      id<MTLBuffer> WearBuffer(const std::vector<float>& w, size_t nv)
      {
         const size_t len = std::max<size_t>(nv * 12, 12);
         if (w.size() == nv * 3 && nv > 0)
            return [mDev newBufferWithBytes:w.data() length:len options:MTLResourceStorageModeShared];
         id<MTLBuffer> b = [mDev newBufferWithLength:len options:MTLResourceStorageModeShared];
         std::memset(b.contents, 0, len);
         return b;
      }
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
         g.wb = WearBuffer(m.wear, m.posNormal.size() / 6);
         g.count = (NSUInteger)m.indices.size();
         if (!m.tracePosNormal.empty() && !m.traceIndices.empty())
         {
            engine::MeshData t;
            t.posNormal = m.tracePosNormal; t.uv = m.traceUv;
            g.tvb = [mDev newBufferWithBytes:m.tracePosNormal.data() length:m.tracePosNormal.size() * 4 options:MTLResourceStorageModeShared];
            g.tib = [mDev newBufferWithBytes:m.traceIndices.data() length:m.traceIndices.size() * 4 options:MTLResourceStorageModeShared];
            g.tuvb = UvBuffer(t);
            g.twb = WearBuffer(m.traceWear, m.tracePosNormal.size() / 6);
            g.tcount = (NSUInteger)m.traceIndices.size();
         }
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
         gd.vertexBuffer = g.RV(); gd.vertexStride = 24;
         gd.indexBuffer = g.RI(); gd.indexType = MTLIndexTypeUInt32;
         gd.triangleCount = g.RC() / 3;
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
      id<MTLTexture> mEnvTex = nil, mEnvDummy = nil, mEnvBound = nil, mEnvCdf = nil;
      unsigned long long mEnvRev = ~0ull;
      id<MTLBuffer> mInfoBuf = nil, mEmBuf = nil;
      id<MTLRenderPipelineState> mBgPsoMrt = nil, mBgPsoDir = nil;
      id<MTLDepthStencilState> mDsBg = nil;
      void FillFrame(FrameU& f, const engine::Frame& fr)
      {
         f.fld[0] = fr.fieldTime;
         std::memcpy(f.viewProj, fr.viewProj, 64);
         std::memcpy(f.eye, fr.eye, 12);
         f.eye[3] = fr.rim;
         Lin3(fr.ambientSrgb, f.ambient);
         Lin3(fr.envSkySrgb, f.sky);
         Lin3(fr.envHorizonSrgb, f.horizon);
         Lin3(fr.envGroundSrgb, f.ground);
         const int n = (int)std::min<size_t>(fr.lights.size(), engine::kMaxLights);
         // Row 5.6: params.w keeps the first 8 (every per-pixel loop that walks f.lights); fld.y is all of them, in the
         // light buffer (mLightAll) the live froxel loop and settle's pick read.
         f.params[0] = fr.exposure; f.params[1] = (float)fr.tonemap; f.params[2] = fr.envIntensity; f.params[3] = (float)std::min(n, 8);
         f.fld[1] = (float)n;
         mLightAll.assign((size_t)std::max(n, 1), LightU{});
         const bool envMap = EnsureEnv(fr);
         f.envm[0] = (envMap ? 1.0f : 0.0f) + (envMap && fr.envBackground ? 2.0f : 0.0f);
         f.envm[1] = fr.envRotationRad; f.envm[2] = fr.envMaxLod; f.envm[3] = fr.envBaseLod;
         int visCh = 0;
         for (int i = 0; i < n; i++)
         {
            const engine::Light& l = fr.lights[i];
            LightU& o = mLightAll[(size_t)i];
            std::memcpy(o.vec, l.vec, 12); o.vec[3] = (float)(int)l.type;
            std::memcpy(o.aim, l.aim, 12);
            // aim.w: this light's channel in the half-res shadow mask (directional, point, sun, spot; first four), else -1
            o.aim[3] = -1.0f;
            if (l.type == engine::LightType::Directional || l.type == engine::LightType::Point || l.type == engine::LightType::Sun || l.type == engine::LightType::Spot)
               if (visCh < 4 && i < 8) o.aim[3] = (float)visCh++;
            Lin3(l.colorSrgb, o.color, l.intensity);
            o.color[3] = l.castShadow ? 0.0f : 1.0f; // 1: this light is never blocked
            o.terms[0] = l.diffuse; o.terms[1] = l.specular; o.terms[2] = l.falloff; o.terms[3] = l.cutoff;
            o.spot[0] = l.spotAngleDeg; o.spot[1] = l.spotPenumbra; o.spot[2] = l.areaW; o.spot[3] = l.areaH;
            if (l.type == engine::LightType::Directional || l.type == engine::LightType::Sun) o.spot[0] = l.sunAngleDeg; // the disc, for these
            if (i < 8) f.lights[i] = o;
         }
         mLightBuf = [mDev newBufferWithBytes:mLightAll.data() length:mLightAll.size() * sizeof(LightU) options:MTLResourceStorageModeShared];
         mClusters.assign(16, 0u);
         if (n > 8) engine::BuildLightClusters(fr, n, mClusters);
         mClusterBuf = [mDev newBufferWithBytes:mClusters.data() length:mClusters.size() * sizeof(uint32_t) options:MTLResourceStorageModeShared];
      }
      std::vector<LightU> mLightAll;
      std::vector<uint32_t> mClusters;
      id<MTLBuffer> mLightBuf = nil, mClusterBuf = nil;
      bool Render(SharedImage& target, const engine::Frame& fr, double* gpuMs) override
      {
         MetalImage& img = static_cast<MetalImage&>(target);
         const MTLPixelFormat fmt = img.Half() ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
         FieldLib(fr);
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
               [ze setVertexBuffer:g.wb offset:0 atIndex:13];
               [ze setVertexBytes:&r.first length:4 atIndex:11];
               [ze drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.count indexType:MTLIndexTypeUInt32 indexBuffer:g.ib
                           indexBufferOffset:0 instanceCount:r.second];
            }
            [ze endEncoding];
         }
         // The same frame again (export warm-ups): the mask pass would trace the same rays to the same answer.
         const bool maskReuse = mask && fr.reuseShadowMask && mVisValid && mVisW == img.Width() && mVisH == img.Height();
         if (mask) f.shQ[2] += 4.0f;
         mVisValid = mask; mVisW = img.Width(); mVisH = img.Height();
         if (mask && !maskReuse)
         {
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
            vp.colorAttachments[2].texture = mVisF;
            vp.colorAttachments[2].loadAction = MTLLoadActionDontCare;
            vp.colorAttachments[2].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> ve = [cb renderCommandEncoderWithDescriptor:vp];
            struct { float invVP[16]; float info[4]; float fog[4], fogC[4], fogLo[4], fogHi[4]; } vu = {};
            std::memcpy(vu.invVP, invVP, 64);
            vu.info[0] = (float)img.Width(); vu.info[1] = (float)img.Height();
            vu.fog[0] = fr.fogDensity; vu.fog[1] = fr.fogAnisotropy;
            for (int k = 0; k < 3; k++) { vu.fogC[k] = fr.fogColor[k]; vu.fogLo[k] = fr.fogMin[k]; vu.fogHi[k] = fr.fogMax[k]; }
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
               [enc useResource:g.RV() usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
               [enc useResource:g.RI() usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
               if (g.RU()) [enc useResource:g.RU() usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
               if (g.RW()) [enc useResource:g.RW() usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
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
         [enc setFragmentBuffer:mLightBuf offset:0 atIndex:6];
         [enc setFragmentBuffer:mClusterBuf offset:0 atIndex:7];
         std::vector<RInstH> info(fr.instances.size() + 1);
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            RInstH& o = info[i]; o = {};
            FillMatRays(in, o.mat);
            std::memcpy(o.normalM, in.normalMatrix, 64);
            if (in.mapAlbedo >= 0 && in.mapAlbedo < engine::kMaxTextures) // the rays see the map's mean colour
               for (int k = 0; k < 3; k++) o.mat.base[k] *= mMapMean[in.mapAlbedo][k];
            if (in.mesh >= 0 && in.mesh < (int)mMeshes.size())
            {
               const Gpu& g = mMeshes[in.mesh];
               o.vb = g.RV().gpuAddress; o.ib = g.RI().gpuAddress; o.uvb = g.RU().gpuAddress; o.wr = g.RW() ? g.RW().gpuAddress : 0;
               if (g.tvb) o.mat.mapNormal = 0.0f; // row 4.3: the relief is geometry now; its bump would count twice
            }
         }
         id<MTLBuffer> infoBuf = InfoBuffer(info);
         [enc setFragmentBuffer:infoBuf offset:0 atIndex:4];
         [enc setFragmentTexture:mEnvBound atIndex:9];
         BindMaps(enc);
         [enc setFragmentTexture:mVis atIndex:7];
         [enc setFragmentTexture:mVisD atIndex:8];
         [enc setFragmentTexture:(mask && fr.fogDensity > 0.0f) ? mVisF : FogNone() atIndex:6]; // row 5.4: 1x1 = no fog
         const bool envBg = fr.envBackground && (int(f.envm[0]) & 2) != 0, fogBg = !envBg && mask && fr.fogDensity > 0.0f;
         if ((envBg || fogBg) && mBgPsoMrt && mBgPsoDir)
         {
            struct { float invVP[16]; float p[4]; float clr[4]; } bu = {};
            if (Inv4(fr.viewProj, bu.invVP))
            {
               bu.p[0] = (float)img.Width(); bu.p[1] = (float)img.Height(); bu.p[2] = mrt ? 1.0f : 0.0f; bu.p[3] = fogBg ? 1.0f : 0.0f;
               Lin3(fr.clearSrgb, bu.clr);
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
            [enc setVertexBuffer:g.wb offset:0 atIndex:13];
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
            Lin3x(b.baseSrgb, u.mat.base);
            u.mat.metallic = b.metallic; u.mat.roughness = b.roughness;
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
               [te setFragmentTextures:mMapSlot withRange:NSMakeRange(10, engine::kMaxTextures)]; // row 2.4: field images at a traced hit
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
               for (const Gpu& g : mMeshes) { [te useResource:g.RV() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; [te useResource:g.RI() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; if (g.RU()) [te useResource:g.RU() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; if (g.RW()) [te useResource:g.RW() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; }
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
            if (!mDofPso) { EngineFail(@"dof pipeline", err); return; }
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
         FieldLib(fr);
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
            FillMat(in, o.mat);
            std::memcpy(o.normalM, in.normalMatrix, 64);
            if (in.mesh >= 0 && in.mesh < (int)mMeshes.size())
            {
               const Gpu& g = mMeshes[in.mesh];
               o.vb = g.RV().gpuAddress; o.ib = g.RI().gpuAddress; o.uvb = g.RU().gpuAddress; o.wr = g.RW() ? g.RW().gpuAddress : 0;
               if (g.tvb) o.mat.mapNormal = 0.0f; // row 4.3: the relief is geometry now; its bump would count twice
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
            info[i].mat.emTri = -1.0f;
            const float lum = 0.2126f * in.emission[0] + 0.7152f * in.emission[1] + 0.0722f * in.emission[2];
            if (lum <= 0.0f || in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            const Gpu& g = mMeshes[in.mesh];
            const size_t tris = g.count / 3;
            if (tris == 0 || ems.size() + tris > 65536)
               continue;
            info[i].mat.emTri = (float)ems.size();
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
            id<MTLTexture> outs[3] = { mPtAcc[cur], mPtAux[cur], mPtGd[cur] };
            for (int k = 0; k < 3; k++)
            {
               rp.colorAttachments[k].texture = outs[k];
               rp.colorAttachments[k].loadAction = MTLLoadActionDontCare;
               rp.colorAttachments[k].storeAction = MTLStoreActionStore;
            }
            id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
            [e setRenderPipelineState:mPtPso];
            struct { float invVP[16]; float info[4]; float em[4]; float lens[4], lr[4], lu[4], lf[4], fog[4], fogC[4], fogLo[4], fogHi[4]; } pu;
            pu.fog[0] = fr.fogDensity; pu.fog[1] = fr.fogAnisotropy; pu.fog[2] = pu.fog[3] = 0.0f;
            for (int k = 0; k < 3; k++) { pu.fogC[k] = fr.fogColor[k]; pu.fogLo[k] = fr.fogMin[k]; pu.fogHi[k] = fr.fogMax[k]; }
            pu.fogLo[3] = pu.fogHi[3] = 0.0f;
            pu.lens[0] = fr.lensRadius; pu.lens[1] = fr.focusDist; pu.lens[2] = fr.bokeh[0]; pu.lens[3] = fr.bokeh[1];
            for (int k = 0; k < 3; k++) { pu.lr[k] = fr.camRight[k]; pu.lu[k] = fr.camUp[k]; pu.lf[k] = fr.camFwd[k]; }
            pu.lr[3] = fr.bokeh[2]; pu.lu[3] = fr.lensCatEye; pu.lf[3] = fr.lensDistortion; pu.fogC[3] = fr.lensCA;
            std::memcpy(pu.invVP, inv, 64);
            pu.info[0] = (float)mPtN; pu.info[1] = (float)std::max(1, bounces); pu.info[2] = (float)(mPtFrame++); pu.info[3] = (float)img.Width();
            const bool envLight = mEnvCdf && mEnvBound == mEnvTex; // the HDRI can be sampled as a light
            pu.em[0] = (float)ems.size(); pu.em[1] = pEm; pu.em[2] = envLight ? 1.0f : 0.0f; pu.em[3] = 0.0f;
            [e setFragmentBytes:&pu length:sizeof(pu) atIndex:0];
            [e setFragmentBytes:&f length:sizeof(f) atIndex:1];
            [e setFragmentBuffer:mArgBuf offset:0 atIndex:3];
            [e setFragmentBuffer:infoBuf offset:0 atIndex:4];
            [e setFragmentBuffer:mEmBuf offset:0 atIndex:5];
            [e setFragmentBuffer:mLightBuf offset:0 atIndex:6];
            [e setFragmentTexture:ELut() atIndex:8];
            [e setFragmentTexture:mEnvBound atIndex:9];
            [e setFragmentTexture:mPtAcc[mPtCur] atIndex:0];
            [e setFragmentTexture:mPtAux[mPtCur] atIndex:1];
            [e setFragmentTexture:mPtGd[mPtCur] atIndex:2];
            [e setFragmentTexture:envLight ? mEnvCdf : mEnvBound atIndex:3];
            BindMaps(e);
            [e useResource:mTlas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            for (const Gpu& g : mMeshes) { [e useResource:g.RV() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; [e useResource:g.RI() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; if (g.RU()) [e useResource:g.RU() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; if (g.RW()) [e useResource:g.RW() usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; }
            for (id<MTLAccelerationStructure> b in mBlas) [e useResource:b usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [e endEncoding];
            mPtCur = cur; mPtN++;
         }
         id<MTLTexture> dn = mDenoise ? Denoise(cb, img.Width(), img.Height()) : nil;
         MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
         rp.colorAttachments[0].texture = img.Tex();
         rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
         rp.colorAttachments[0].storeAction = MTLStoreActionStore;
         id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
         float ru[8] = { (float)mPtN, (float)fr.tonemap, fr.exposure, dn ? 1.0f : 0.0f, fr.clearSrgb[0], fr.clearSrgb[1], fr.clearSrgb[2], 0 };
         [e setRenderPipelineState:mPtResPso];
         [e setFragmentBytes:ru length:sizeof(ru) atIndex:0];
         [e setFragmentTexture:mPtAcc[mPtCur] atIndex:0];
         [e setFragmentTexture:mPtAux[mPtCur] atIndex:1];
         [e setFragmentTexture:dn ? dn : mPtAcc[mPtCur] atIndex:2];
         [e setFragmentTexture:dn ? mDnG : mPtGd[mPtCur] atIndex:3];
         [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
         [e endEncoding];
         [cb commit];
         [cb waitUntilCompleted];
         if (gpuMs) *gpuMs = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
         mPtNoise = -1.0f;
         if (dn && mDnHist && cb.status == MTLCommandBufferStatusCompleted)
         {
            const uint32_t* h = (const uint32_t*)mDnHist.contents;
            uint64_t total = 0, run = 0;
            for (int i = 0; i < 64; i++) total += h[i];
            for (int i = 0; i < 64 && total > 0; i++)
            {
               run += h[i];
               if (run * 100 >= total * 95) { mPtNoise = (float)(i + 1) / 400.0f; break; }
            }
         }
         return cb.status == MTLCommandBufferStatusCompleted;
      }
      float PathTraceNoise() const override { return mPtNoise; }

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
         a.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA32Float;
         a.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA32Float;
         mPtPso = [mDev newRenderPipelineStateWithDescriptor:a error:&err];
         if (!mPtPso) EngineFail(@"pt pipeline", err);
         MTLRenderPipelineDescriptor* b = [MTLRenderPipelineDescriptor new];
         b.vertexFunction = [mLib newFunctionWithName:@"sky_vs"];
         b.fragmentFunction = [mLib newFunctionWithName:@"ptres_fs"];
         b.colorAttachments[0].pixelFormat = fmt;
         mPtResPso = [mDev newRenderPipelineStateWithDescriptor:b error:&err];
         if (!mPtResPso) EngineFail(@"pt resolve pipeline", err);
         mDnPrepPso = [mDev newComputePipelineStateWithFunction:[mLib newFunctionWithName:@"dn_prep_k"] error:&err];
         mDnAtrousPso = [mDev newComputePipelineStateWithFunction:[mLib newFunctionWithName:@"dn_atrous_k"] error:&err];
         if (!mDnPrepPso || !mDnAtrousPso) EngineFail(@"settle denoiser pipeline", err);
         mPtFmt = fmt;
         return mPtPso && mPtResPso && mDnPrepPso && mDnAtrousPso;
      }
      bool PtTargets(int w, int h)
      {
         if (mPtAcc[0] && (int)mPtAcc[0].width == w && (int)mPtAcc[0].height == h)
            return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
         d.storageMode = MTLStorageModePrivate;
         for (int k = 0; k < 2; k++)
         {
            mPtAcc[k] = [mDev newTextureWithDescriptor:d]; mPtAux[k] = [mDev newTextureWithDescriptor:d]; mPtGd[k] = [mDev newTextureWithDescriptor:d];
         }
         mDnC[0] = mDnC[1] = mDnG = mDnA = nil; // made on the first denoised resolve at this size
         mPtN = 0;
         return mPtAcc[0] && mPtAcc[1] && mPtAux[0] && mPtAux[1] && mPtGd[0] && mPtGd[1];
      }
      bool DnTargets(int w, int h)
      {
         if (mDnG) return true;
         MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:w height:h mipmapped:NO];
         d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
         d.storageMode = MTLStorageModePrivate;
         mDnC[0] = [mDev newTextureWithDescriptor:d]; mDnC[1] = [mDev newTextureWithDescriptor:d]; mDnA = [mDev newTextureWithDescriptor:d];
         mDnG = [mDev newTextureWithDescriptor:d];
         return mDnC[0] && mDnC[1] && mDnA && mDnG;
      }
      // Settle denoiser (PLAN 1.5): demodulate, five a-trous steps over the lighting; returns the filtered texture.
      id<MTLTexture> Denoise(id<MTLCommandBuffer> cb, int w, int h)
      {
         if (!DnTargets(w, h)) return nil;
         if (!mDnHist) mDnHist = [mDev newBufferWithLength:64 * sizeof(uint32_t) options:MTLResourceStorageModeShared];
         std::memset(mDnHist.contents, 0, 64 * sizeof(uint32_t)); // the caller waits for this command buffer before the next one
         id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
         const MTLSize tg = MTLSizeMake(16, 16, 1), grid = MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1);
         struct { int step; float n; float sigL; float pad; } du = { 1, 0.0f, mDnSigL, 0.0f };
         [ce setComputePipelineState:mDnPrepPso];
         [ce setTexture:mPtAcc[mPtCur] atIndex:0]; [ce setTexture:mPtAux[mPtCur] atIndex:1]; [ce setTexture:mPtGd[mPtCur] atIndex:2];
         [ce setTexture:mDnC[0] atIndex:3]; [ce setTexture:mDnG atIndex:4]; [ce setTexture:mDnA atIndex:5];
         [ce setBytes:&du length:sizeof(du) atIndex:0];
         [ce setBuffer:mDnHist offset:0 atIndex:1];
         [ce dispatchThreads:grid threadsPerThreadgroup:tg];
         [ce setComputePipelineState:mDnAtrousPso];
         int c = 0;
         for (int i = 0; i < mDnSteps; i++)
         {
            du.step = 1 << i;
            [ce setTexture:mDnC[c] atIndex:0]; [ce setTexture:mDnG atIndex:1]; [ce setTexture:mDnC[1 - c] atIndex:2]; [ce setTexture:mDnA atIndex:3];
            [ce setBytes:&du length:sizeof(du) atIndex:0];
            [ce dispatchThreads:grid threadsPerThreadgroup:tg];
            c = 1 - c;
         }
         [ce endEncoding];
         return mDnC[c];
      }
      id<MTLRenderPipelineState> mPtPso = nil, mPtResPso = nil;
      id<MTLComputePipelineState> mDnPrepPso = nil, mDnAtrousPso = nil;
      id<MTLBuffer> mDnHist = nil; // dn_prep_k's 64-bin relative-error histogram
      float mPtNoise = -1.0f;
      id<MTLTexture> mPtAcc[2] = { nil, nil }, mPtAux[2] = { nil, nil }, mPtGd[2] = { nil, nil };
      id<MTLTexture> mDnC[2] = { nil, nil }, mDnG = nil, mDnA = nil;
      // On by default; INFINITE_ENGINE_DENOISE=0 turns it off, _DNSTEPS (5) and _DNSIGL (4) are A/B knobs.
      const bool mDenoise = [] { const char* e = getenv("INFINITE_ENGINE_DENOISE"); return !e || atoi(e) > 0; }();
      const int mDnSteps = [] { const char* e = getenv("INFINITE_ENGINE_DNSTEPS"); return e ? atoi(e) : 5; }();
      const float mDnSigL = [] { const char* e = getenv("INFINITE_ENGINE_DNSIGL"); return e ? (float)atof(e) : 4.0f; }();
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
         if (!sh.enabled || fr.lights.empty() || !fr.lights[0].castShadow || fr.lights[0].type == engine::LightType::Point || fr.lights[0].type == engine::LightType::Area ||
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
            if (!fr.instances[r.first].castShadow)
               continue;
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
         if (!mFsFn || !fsDir) EngineFail(@"r_fs specialisation", fe);
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
            if (!mAtPso || !mCompPso) EngineFail(@"denoise pipeline", err);
            ad.fragmentFunction = [mLib newFunctionWithName:@"tr_fs"];
            ad.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            ad.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
            mTrPso = [mDev newRenderPipelineStateWithDescriptor:ad error:&err];
            if (!mTrPso) EngineFail(@"trace pipeline", err);
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
               vd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
               mVisPso = [mDev newRenderPipelineStateWithDescriptor:vd error:&err];
               if (!mVisPso) EngineFail(@"shadow mask pipeline", err);
            }
            pd.vertexFunction = keep;
            if (!mPsoRun || !mPsoRunDir) EngineFail(@"run pipeline", err);
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
               if (!mPsoPt || !mPsoPtMrt || !mCullPso || !mArgsPso) EngineFail(@"point/cull pipeline", err);
            }
            bd.fragmentFunction = mFsFn;
            bd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            bd.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
            bd.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
            bd.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA32Float;
            mPsoBatchMrt = [mDev newRenderPipelineStateWithDescriptor:bd error:&err];
            if (!mPsoBatch || !mPsoBatchMrt) EngineFail(@"batch pipeline", err);
         }
         if (!mPsoDir) EngineFail(@"direct pipeline", err);
         if (!mResPso) EngineFail(@"resolve pipeline", err);
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
            if (!mBgPsoDir || !mBgPsoMrt) EngineFail(@"background pipeline", err);
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
         co.preprocessorMacros = AbMacros(@{ @"INV_POS" : @1 });
         id<MTLLibrary> lib = [mDev newLibraryWithSource:Src() options:co error:&err];
         id<MTLFunction> rr = lib ? [lib newFunctionWithName:@"rr_vs"] : nil;
         if (!rr) { EngineFail(@"invariant shader", err); return false; }
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
         if (!mPrePso || !mPsoRunInv || !mPsoRunDirInv) EngineFail(@"invariant pipelines", err);
         return mPrePso && mPsoRunInv && mPsoRunDirInv;
      }
      bool mVisValid = false; int mVisW = 0, mVisH = 0; // mVis holds the last Render's mask at this size
      id<MTLTexture> mVis = nil, mVisD = nil; // half-res RT shadow mask (4 lights) and its distance guide
      id<MTLTexture> mVisF = nil, mFogNone = nil; // row 5.4: half-res fog (in-scatter, transmittance); 1x1 stand-in when off
      id<MTLTexture> FogNone()
      {
         if (!mFogNone)
         {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:1 height:1 mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            mFogNone = [mDev newTextureWithDescriptor:d];
         }
         return mFogNone;
      }
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
            FillMat(in, u.mat);
            if (in.mesh < 0 || in.mesh >= (int)mMeshes.size())
               continue;
            if (!mRuns.empty())
            {
               auto& r = mRuns.back();
               const engine::Instance& a = fr.instances[r.first];
               if (r.first + r.second == i && a.mesh == in.mesh && a.castShadow == in.castShadow && std::memcmp(&dst[r.first].mat, &u.mat, sizeof(R3dMatH)) == 0)
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
         d.pixelFormat = MTLPixelFormatRGBA16Float; mVisF = [mDev newTextureWithDescriptor:d];
         mHistValid = false; mRValid = false;
         return mHdr && mGiT && mHist[0] && mHist[1] && mRefl && mRHist[0] && mRHist[1] && mNz && mAt[0] && mAt[1] && mTr[0] && mTr[1] && mVis && mVisD && mVisF;
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

      bool RunFieldPixel(const std::string& msl, int w, int h, const std::vector<float>& slots, const float* bankInit,
                         std::vector<float>& px, std::string& err) override
      {
         @autoreleasepool
         {
            MTLCompileOptions* co = [MTLCompileOptions new];
            co.fastMathEnabled = YES; // as GL on macOS: precise sin turns the fract(sin(x) * 43758) hashes into other noise
            NSError* e = nil;
            id<MTLLibrary> lib = [mDev newLibraryWithSource:[NSString stringWithUTF8String:msl.c_str()] options:co error:&e];
            if (!lib)
            {
               err = e ? [[e localizedDescription] UTF8String] : "Metal compile failed";
               return false;
            }
            if (w <= 0 || h <= 0)
               return true;
            MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [lib newFunctionWithName:@"fld_vs"];
            pd.fragmentFunction = [lib newFunctionWithName:@"fld_fs"];
            pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
            id<MTLRenderPipelineState> pso = [mDev newRenderPipelineStateWithDescriptor:pd error:&e];
            if (!pso)
            {
               err = e ? [[e localizedDescription] UTF8String] : "Metal pipeline failed";
               return false;
            }
            MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                                          width:w height:h mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModeShared;
            id<MTLTexture> out = [mDev newTextureWithDescriptor:td];
            id<MTLTexture> bank = [mDev newTextureWithDescriptor:td];
            std::vector<float> fill((size_t)w * h * 4);
            for (size_t i = 0; i < fill.size(); i++)
               fill[i] = bankInit ? bankInit[i & 3] : 0.0f;
            [bank replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:fill.data() bytesPerRow:(NSUInteger)w * 16];
            MTLTextureDescriptor* sd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                          width:1 height:1 mipmapped:NO];
            id<MTLTexture> black = [mDev newTextureWithDescriptor:sd];
            const uint8_t zero[4] = { 0, 0, 0, 0 };
            [black replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:zero bytesPerRow:4];

            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = out;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            id<MTLCommandBuffer> cb = [mQueue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            [enc setRenderPipelineState:pso];
            std::vector<float> u = slots;
            if (u.empty())
               u.assign(4, 0.0f);
            [enc setFragmentBytes:u.data() length:u.size() * sizeof(float) atIndex:0];
            [enc setFragmentTexture:black atIndex:0];
            [enc setFragmentTexture:bank atIndex:1];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status != MTLCommandBufferStatusCompleted)
            {
               err = "Metal command buffer failed";
               return false;
            }
            px.resize((size_t)w * h * 4);
            [out getBytes:px.data() bytesPerRow:(NSUInteger)w * 16 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
            return true;
         }
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

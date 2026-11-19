// OPUS_PLAN row 9.3: writes the Vulkan backend's GLSL 450 (a build-time host tool; glslang turns each file into a
// SPIR-V header that VulkanDevice.cpp includes). The shading maths is the one shared source (Render3DShared.h and the
// other Render3D*.h macros, written in the vec3 dialect); this file holds only the bindings and entry points, ported
// from MetalDevice.mm's r_vs / shr_vs / r_fs (its no-ray variant) / sky_vs / bg_fs.
//   usage: VkShaderGen <outdir>   -> raster.vert raster.frag shadow.vert bg.vert bg.frag tri.vert tri.frag
#include <cstdio>
#include <string>

#include "platform/AppPaths.h"

#include "nodes/Render3DBsdf.h"
#include "nodes/Render3DFresnel.h"
#include "nodes/Render3DMaterial.h"
#include "nodes/Render3DShared.h"
#include "nodes/Render3DSheen.h"
#include "nodes/Render3DThinFilm.h"
#include "nodes/Render3DTonemap.h"
#include "engine/rhi/vk/VkPtShaders.h"

#define R3D_MAT_GLSL_F3(n) "vec3 " #n "; "
#define R3D_MAT_GLSL_F1(n) "float " #n "; "

namespace
{
// Shared by every stage: the frame and instance records in scalar layout (the C++ structs' packing, no padding).
const std::string kHead = std::string(R"(#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_samplerless_texture_functions : require
struct LightU { vec4 vec; vec4 aim; vec4 color; vec4 terms; vec4 spot; };
struct FrameU
{
   mat4 viewProj; vec4 eye; vec4 ambient; vec4 sky; vec4 horizon; vec4 ground; vec4 params;
   mat4 lightVP; vec4 shP; vec4 shQ; vec4 gi; vec4 envm; LightU lights[8]; vec4 fld;
};
struct MatRec { )") + R3D_MAT_FIELDS(R3D_MAT_GLSL_F3, R3D_MAT_GLSL_F1) + R"(};
struct InstU { mat4 model; mat4 normalM; MatRec mat; };
layout(scalar, set = 0, binding = 0) readonly buffer FrameB { FrameU f; };
layout(scalar, set = 0, binding = 1) readonly buffer InstB { InstU inst[]; };
layout(push_constant) uniform PC { mat4 lvp; vec4 bg; vec4 clr; mat4 invVP; } pc;
)";

const char* kRasterVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec3 aWear;
layout(location = 0) out vec3 vWp;
layout(location = 1) out vec3 vN;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec3 vLp;
layout(location = 4) out vec3 vCw;
layout(location = 5) flat out float vOrand;
layout(location = 6) flat out uint vInst;
void main()
{
   // rr_vs: the instance is firstInstance + the copy. No y flip: Vulkan's row 0 is already GL's bottom row.
   uint i = uint(gl_InstanceIndex);
   vec4 w = inst[i].model * vec4(aPos, 1.0);
   vec4 c = f.viewProj * w;
   c.z = (c.z + c.w) * 0.5;
   gl_Position = c;
   vWp = w.xyz; vN = (inst[i].normalM * vec4(aNrm, 0.0)).xyz; vUv = aUv; vLp = aPos; vCw = aWear;
   vOrand = inst[i].mat.objRand; vInst = i;
}
)";

const char* kShadowVert = R"(
layout(location = 0) in vec3 aPos;
void main()
{
   vec4 c = pc.lvp * (inst[gl_InstanceIndex].model * vec4(aPos, 1.0));
   c.z = (c.z + c.w) * 0.5;
   gl_Position = c;
}
)";

const char* kSkyVert = R"(
void main()
{
   vec2 p = vec2(gl_VertexIndex == 1 ? 3.0 : -1.0, gl_VertexIndex == 2 ? 3.0 : -1.0);
   gl_Position = vec4(p, 1.0, 1.0);
}
)";

// Environment lookup and the material maps, as MetalDevice.mm's (texture objects are indexed, not passed).
const char* kFragLib = R"(
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;
layout(set = 0, binding = 3) uniform sampler2D envTex;
layout(set = 0, binding = 4) uniform texture2D maps[16];
layout(set = 0, binding = 5) uniform sampler sC;
layout(set = 0, binding = 6) uniform sampler sR;
layout(set = 0, binding = 7) uniform sampler sM;
vec3 sampleEnv(vec3 dir, float rough)
{
   if ((int(f.envm.x) & 1) != 0)
   {
      vec2 uv = vec2((atan(dir.z, dir.x) + f.envm.y) / 6.28318530718 + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265359);
      return textureLod(envTex, uv, max(clamp(rough, 0.0, 1.0) * f.envm.z - f.envm.w, 0.0)).rgb * f.params.z;
   }
   float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
   vec3 lower = mix(f.ground.xyz, f.horizon.xyz, smoothstep(0.0, 0.5, t));
   vec3 env = mix(lower, f.sky.xyz, smoothstep(0.5, 1.0, t));
   return mix(env, f.horizon.xyz, rough * rough) * f.params.z;
}
)" R3D_SHARED_SRC R3D_AREA_SRC R3D_BSDF_SRC R3D_FRESNEL_SRC R3D_THINFILM_SRC R3D_TONEMAP_SRC R3D_SRGB_SRC R3D_SHEEN_GLSL R"(
#define SSS_K 4.0
vec3 iridGain(float w, float ior, float thick, float nV, vec3 f0) { return w > 0.0 ? thinFilmGain(nV, ior, thick, f0, w) : vec3(1.0); }
#define R3D_G2(nv, nl, r) ggxG2(nv, nl, r)
vec3 fresnel(float cosT, vec3 f0) { return fresnelExact(cosT, f0); }
float shadowFactor(vec3 wp, vec3 n, vec3 ld)
{
   vec4 lp = f.lightVP * vec4(wp, 1.0);
   vec3 pr = lp.xyz / max(lp.w, 1e-6);
   vec2 uv = vec2(pr.x * 0.5 + 0.5, pr.y * 0.5 + 0.5); // the light's NDC +y is the map's last row here
   float z = pr.z * 0.5 + 0.5;
   if (z > 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
   float cosL = clamp(dot(n, ld), 0.05, 1.0);
   float bias = f.shP.z * (1.0 + min(sqrt(1.0 - cosL * cosL) / cosL, 10.0));
   float lit = 0.0;
   for (int x = -1; x <= 1; x++)
      for (int y = -1; y <= 1; y++)
         lit += texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * f.shQ.x * f.shQ.y, z - bias));
   return mix(1.0, lit / 9.0, f.shP.w);
}
float rnd(inout uint s) { s = pcg(s); return float(s) * (1.0 / 4294967296.0); }
vec3 mapLin(vec3 c) { return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThan(c, vec3(0.04045))); }
struct MapX { int space; float tri; vec3 sc; vec3 ro; vec3 tr; vec3 lo; vec3 hi; float brk; int wrap; };
MapX mapX(MatRec m)
{
   MapX x; x.space = int(m.mapSpace + 0.5); x.tri = m.triplanar; x.sc = m.mapScale; x.ro = m.mapRotate; x.tr = m.mapTranslate;
   x.lo = m.boxLo; x.hi = m.boxHi; x.brk = m.breakup; x.wrap = int(m.wrapBump + 0.5) & 3; return x;
}
vec3 mapRot(vec3 p, vec3 r)
{
   float cx = cos(r.x), sx = sin(r.x), cy = cos(r.y), sy = sin(r.y), cz = cos(r.z), sz = sin(r.z);
   p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
   p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
   return vec3(cz * p.x - sz * p.y, sz * p.x + cz * p.y, p.z);
}
vec3 mapBase(MapX x, vec2 uv, vec3 lp)
{
   vec3 b = x.space == 1 ? (lp - x.lo) / max(x.hi - x.lo, vec3(1e-5)) : x.space == 2 ? lp : vec3(uv, 0.0);
   return mapRot(b * x.sc, x.ro) + x.tr;
}
vec2 mapUv(MapX x, vec3 b, vec3 n)
{
   if (x.space == 0) return b.xy;
   vec3 an = abs(n);
   if (an.x >= an.y && an.x >= an.z) return b.yz;
   if (an.y >= an.x && an.y >= an.z) return b.xz;
   return b.xy;
}
vec4 tapG(int s, int wrap, vec2 uv, vec2 gx, vec2 gy, float lod)
{
   if (lod >= 0.0)
      return wrap == 1 ? textureLod(sampler2D(maps[s], sR), uv, lod) : wrap == 2 ? textureLod(sampler2D(maps[s], sM), uv, lod) : textureLod(sampler2D(maps[s], sC), uv, lod);
   return wrap == 1 ? textureGrad(sampler2D(maps[s], sR), uv, gx, gy) : wrap == 2 ? textureGrad(sampler2D(maps[s], sM), uv, gx, gy) : textureGrad(sampler2D(maps[s], sC), uv, gx, gy);
}
float brkHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float brkNoise(vec2 p)
{
   vec2 i = floor(p); vec2 q = fract(p); q = q * q * (3.0 - 2.0 * q);
   return mix(mix(brkHash(i), brkHash(i + vec2(1.0, 0.0)), q.x), mix(brkHash(i + vec2(0.0, 1.0)), brkHash(i + vec2(1.0, 1.0)), q.x), q.y);
}
vec4 mapTap(int s, MapX x, vec2 uv, vec2 gx, vec2 gy, float lod)
{
   if (x.brk <= 0.001) return tapG(s, x.wrap, uv, gx, gy, lod);
   float k = brkNoise(uv * 0.6) * 8.0;
   float i = floor(k), fk = k - i;
   vec2 oa = sin(vec2(3.0, 7.0) * i), ob = sin(vec2(3.0, 7.0) * (i + 1.0));
   vec4 ca = tapG(s, 1, uv + oa, gx, gy, lod), cb = tapG(s, 1, uv + ob, gx, gy, lod);
   vec4 u = mix(ca, cb, smoothstep(0.2, 0.8, fk - 0.1 * dot(ca.rgb - cb.rgb, vec3(1.0))));
   return x.brk >= 0.999 ? u : mix(tapG(s, 1, uv, gx, gy, lod), u, x.brk);
}
vec2 prj(vec3 v, int a) { return a == 0 ? v.yz : a == 1 ? v.xz : v.xy; }
vec4 mapSample(int s, MapX x, vec3 b, vec3 n, vec3 bx, vec3 by, float lod)
{
   if (x.space == 0 || x.tri <= 0.001) return mapTap(s, x, mapUv(x, b, n), mapUv(x, bx, n), mapUv(x, by, n), lod);
   float sharp = 1.0 + (1.0 - x.tri) * 7.0;
   vec3 w = pow(max(abs(n), vec3(1e-4)), vec3(sharp));
   w /= (w.x + w.y + w.z);
   vec4 c = vec4(0.0);
   for (int a = 0; a < 3; a++) c += mapTap(s, x, prj(b, a), prj(bx, a), prj(by, a), lod) * w[a];
   return c;
}
vec3 mapTangent(int s, MapX x, bool bump, float strength, vec2 uv, vec2 gx, vec2 gy, float lod)
{
   vec3 r;
   if (bump)
   {
      vec2 px = exp2(max(lod, 0.0)) / vec2(textureSize(maps[s], 0));
      vec3 lum = vec3(0.2126, 0.7152, 0.0722);
      float hl = dot(mapTap(s, x, uv - vec2(px.x, 0.0), gx, gy, lod).rgb, lum), hr = dot(mapTap(s, x, uv + vec2(px.x, 0.0), gx, gy, lod).rgb, lum);
      float hd = dot(mapTap(s, x, uv - vec2(0.0, px.y), gx, gy, lod).rgb, lum), hu = dot(mapTap(s, x, uv + vec2(0.0, px.y), gx, gy, lod).rgb, lum);
      r = normalize(vec3((hl - hr) * 2.0, (hd - hu) * 2.0, 1.0));
   }
   else r = mapTap(s, x, uv, gx, gy, lod).rgb * 2.0 - 1.0;
   r.xy *= strength;
   r.z = max(r.z, 1e-3);
   return r;
}
vec3 mapNormalTS(int s, MapX x, bool bump, float strength, vec2 uv, vec2 gx, vec2 gy, float lod, float dScale, float dStrength)
{
   vec3 r = mapTangent(s, x, bump, strength, uv, gx, gy, lod);
   if (dScale > 0.0)
   {
      MapX xd = x; xd.wrap = 1;
      vec3 d = mapTangent(s, xd, bump, dStrength, uv * dScale, gx * dScale, gy * dScale, lod);
      r = normalize(vec3(r.xy + d.xy, r.z * d.z));
   }
   return r;
}
float pomH(int s, MapX x, vec2 uv, vec2 gx, vec2 gy) { return dot(mapTap(s, x, uv, gx, gy, -1.0).rgb, vec3(0.2126, 0.7152, 0.0722)); }
vec2 pomMarch(int s, MapX x, vec2 uv, vec2 gx, vec2 gy, vec3 vTS, float scale, out float hOut)
{
   float ns = mix(32.0, 8.0, abs(vTS.z)), dl = 1.0 / ns;
   vec2 dUv = -vTS.xy / max(vTS.z, 0.1) * scale / ns;
   float layer = 1.0, hc = pomH(s, x, uv, gx, gy), hp = hc;
   vec2 c = uv, cp = uv;
   for (int i = 0; i < 32 && hc < layer; i++) { cp = c; hp = hc; c += dUv; layer -= dl; hc = pomH(s, x, c, gx, gy); }
   float a = hc - layer, b = hp - (layer + dl);
   float w = a - b > 1e-6 ? a / (a - b) : 0.0;
   hOut = layer + w * dl;
   return mix(c, cp, w);
}
float pomShadow(int s, MapX x, vec2 c, float h, vec2 gx, vec2 gy, vec3 lTS, float scale)
{
   if (lTS.z <= 0.0 || h >= 0.999) return 1.0;
   vec2 dUv = lTS.xy / max(lTS.z, 0.1) * scale * (1.0 - h) / 8.0;
   float m = 0.0;
   for (int i = 1; i <= 8; i++) m = max(m, pomH(s, x, c + dUv * float(i), gx, gy) - (h + (1.0 - h) * float(i) / 8.0));
   return 1.0 - smoothstep(0.0, 0.05, m);
}
float coatSpec(float nDotH, float nDotV, float nDotL, float vDotH, float r)
{
   return distGGX(nDotH, r) * R3D_G2(nDotV, nDotL, r) * (0.04 + 0.96 * pow(1.0 - vDotH, 5.0)) / max(4.0 * nDotV * nDotL, 1e-7);
}
)";

// r_fs, the no-ray build (Metal function constant 1 false, kMrt false): direct, tonemapped, sRGB-encoded. Not here
// yet: hair (needs the fragment primitive id), Field material programs, the light froxels past 8 lights, fog.
const char* kRasterFrag = R"(
layout(location = 0) in vec3 vWp;
layout(location = 1) in vec3 vN;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec3 vLp;
layout(location = 4) in vec3 vCw;
layout(location = 5) flat in float vOrand;
layout(location = 6) flat in uint vInst;
layout(location = 0) out vec4 oC;
void main()
{
   MatRec m = inst[vInst].mat;
   vec3 n = normalize(vN);
   if (!gl_FrontFacing) n = -n;
   vec3 base = m.base;
   float metalIn = m.metallic, roughIn = m.roughness;
   MapX mx = mapX(m);
   vec3 mb = mapBase(mx, vUv, vLp);
   vec3 mbx = dFdx(mb), mby = dFdy(mb);
   vec3 dp1 = dFdx(vWp), dp2 = dFdy(vWp);
   vec2 shDu1 = dFdx(vUv), shDu2 = dFdy(vUv);
   vec3 dnx = dFdx(vN), dny = dFdy(vN);
   vec3 nP = n;
   float pomHt = 1.0, pomScale = 0.0;
   vec3 pomT = vec3(0.0), pomB = vec3(0.0);
   if (m.mapNormal > 0.5 && (int(m.wrapBump + 0.5) & 4) != 0 && mx.space == 0 && mx.brk <= 0.001)
   {
      int hs = int(m.mapNormal) - 1;
      vec2 tsz = vec2(textureSize(maps[hs], 0));
      float rho = max(length(mbx.xy * tsz), length(mby.xy * tsz));
      pomScale = 4.0 * m.normalStrength / tsz.x * (1.0 - smoothstep(4.0, 8.0, rho));
      vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      vec3 t = dp2perp * mbx.x + dp1perp * mby.x, bt = dp2perp * mbx.y + dp1perp * mby.y;
      vec3 V = normalize(f.eye.xyz - vWp);
      if (pomScale > 1e-6 && max(dot(t, t), dot(bt, bt)) > 1e-12 && dot(V, n) > 0.0)
      {
         t = normalize(t); bt = normalize(bt);
         mb.xy = pomMarch(hs, mx, mb.xy, mbx.xy, mby.xy, vec3(dot(V, t), dot(V, bt), dot(V, n)), pomScale, pomHt);
         pomT = t; pomB = bt;
      }
      else pomScale = 0.0;
   }
#define MAPR(slot) mapSample(int(slot) - 1, mx, mb, nP, mbx, mby, -1.0)
   if (m.mapAlbedo > 0.5) base *= mapLin(MAPR(m.mapAlbedo).rgb);
   if (m.mapRoughness > 0.5) roughIn *= MAPR(m.mapRoughness).r;
   if (m.mapMetallic > 0.5) metalIn *= MAPR(m.mapMetallic).r;
   float aoV = m.mapAo > 0.5 ? MAPR(m.mapAo).r : 1.0;
   vec3 emMap = m.mapEmission > 0.5 ? mapLin(MAPR(m.mapEmission).rgb) : vec3(1.0);
   float shW = m.sheen * (m.mapSheen > 0.5 ? MAPR(m.mapSheen).r : 1.0);
   float shMax = shW * maxc(m.sheenColor);
   float alpha = 1.0 - m.clear;
   if (m.mapOpacity > 0.5) alpha *= MAPR(m.mapOpacity).r;
   float ccW = m.coat * (m.mapCoat > 0.5 ? MAPR(m.mapCoat).r : 1.0);
   float transMap = m.mapTransmission > 0.5 ? MAPR(m.mapTransmission).r : 1.0;
#undef MAPR
   if (alpha < 1.0)
   {
      uint as = pcg(uint(gl_FragCoord.x) * 7919u + 104729u * uint(gl_FragCoord.y)) ^ pcg(uint(f.gi.z) * 977u + 5u);
      if (alpha < (m.alphaCutoff > 0.0 ? m.alphaCutoff : rnd(as))) discard;
   }
   float metal = clamp(metalIn, 0.0, 1.0);
   float nVar = min(2.0 * 0.25 * (dot(dnx, dnx) + dot(dny, dny)), 0.18);
   float rough = sqrt(clamp(sqrt(clamp(roughIn * roughIn * roughIn * roughIn + nVar, 0.0, 1.0)), 0.045 * 0.045, 1.0));
   vec3 viewDir = normalize(f.eye.xyz - vWp);
   vec3 gN = cross(dp1, dp2);
   if (dot(gN, gN) > 1e-20) { gN = normalize(gN); if (dot(gN, viewDir) < 0.0) gN = -gN; if (dot(n, gN) < 0.0) n = -n; }
   if (m.mapNormal > 0.5)
   {
      vec2 muv = mapUv(mx, mb, nP), duv1 = mapUv(mx, mbx, nP), duv2 = mapUv(mx, mby, nP);
      vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      vec3 t = dp2perp * duv1.x + dp1perp * duv2.x, bt = dp2perp * duv1.y + dp1perp * duv2.y;
      if (max(dot(t, t), dot(bt, bt)) > 1e-12) { t = normalize(t); bt = normalize(bt); }
      else { t = abs(n.z) < 0.999 ? normalize(cross(n, vec3(0.0, 0.0, 1.0))) : normalize(cross(n, vec3(1.0, 0.0, 0.0))); bt = normalize(cross(n, t)); }
      vec3 s = mapNormalTS(int(m.mapNormal) - 1, mx, (int(m.wrapBump + 0.5) & 4) != 0, m.normalStrength, muv, duv1, duv2, -1.0, m.detailScale, m.detailStrength);
      n = normalize(t * s.x + bt * s.y + n * s.z);
   }
   float nDotV = max(dot(n, viewDir), 1e-4);
   vec3 shN = n;
   if (m.sheenDir != 0.0) shN = sheenTilt(n, cross(dp2, n) * shDu1.x + cross(n, dp1) * shDu2.x, m.sheenDir);
   float shNV = max(dot(shN, viewDir), 1e-4);
   float shEv = shMax > 0.0 ? sheenE(shNV, m.sheenRoughness) : 0.0;
   float shV = 1.0 - shMax * shEv;
   vec3 shAmb = m.sheenColor * (shW * shEv);
   vec3 f0 = mix(vec3(0.04 + m.f0Extra), base, metal);
   vec3 iridK = iridGain(m.iridescence, m.iridIor, m.iridThickness, nDotV, f0);
   float ccR = clamp(m.coatRoughness, 0.02, 1.0);
   float ccFv = 0.04 + 0.96 * pow(1.0 - nDotV, 5.0);
   float ccAt = 1.0 - ccW * ccFv;
   vec3 coatCol = vec3(0.0);
   vec3 col = vec3(0.0);
   vec3 floorBounce = vec3(0.0);
   float sssW = clamp(m.subsurface, 0.0, 1.0), tlW = clamp(m.translucency, 0.0, 1.0);
   vec3 ssW = vec3(0.0);
   if (sssW > 0.0)
   {
      float kap = sqrt((dot(dnx, dnx) + dot(dny, dny)) / max(dot(dp1, dp1) + dot(dp2, dp2), 1e-12));
      ssW = clamp(SSS_K * m.sssRadius * kap, 0.0, 1.0);
   }
   float frontD = (1.0 - sssW) * (1.0 - tlW);
   int count = int(f.params.w);
   for (int i = 0; i < 8; i++)
   {
      if (i >= count) break;
      LightU L = f.lights[i];
      int type = int(L.vec.w);
      vec3 radiance0 = L.color.xyz;
      if (type == 3) { col += base * (1.0 - metal) * radiance0 * L.terms.x; continue; }
      float areaE = -1.0;
      if (type == 5)
      {
         vec3 aR = cross(vec3(0.0, 1.0, 0.0), L.aim.xyz);
         aR = dot(aR, aR) < 1e-4 ? vec3(1.0, 0.0, 0.0) : normalize(aR);
         vec3 aU = cross(L.aim.xyz, aR);
         uint ars = pcg(uint(gl_FragCoord.x) * 7919u + 104729u * uint(gl_FragCoord.y)) ^ pcg(uint(f.gi.z) * 31337u + uint(i));
         areaE = 0.0;
         for (int si = 0; si < 8; si++)
         {
            float jx = (float(si & 3) + rnd(ars)) * 0.25, jy = (float(si >> 2) + rnd(ars)) * 0.5;
            vec3 ao = areaOffset(L.spot.x, jx, jy);
            vec3 tl = L.vec.xyz + aR * (ao.x * L.spot.z) + aU * (ao.y * L.spot.w) - vWp;
            float dist = length(tl); vec3 ld = tl / max(dist, 1e-4);
            float cl = dot(-ld, L.aim.xyz);
            float e = lightFalloff(dist, L.terms.z) * max(cl, 0.0) * areaSpreadK(L.spot.y, cl) * max(dot(n, ld), 0.0);
            if (L.terms.w > 0.0 && dist > L.terms.w) e = 0.0;
            areaE += e * 0.125;
         }
         if (areaE <= 0.0) continue;
      }
      vec3 lightDir; float atten = 1.0;
      if (type == 1 || type == 4 || type == 5)
      {
         vec3 toLight = L.vec.xyz - vWp;
         float dist = length(toLight);
         lightDir = toLight / max(dist, 1e-4);
         atten = lightFalloff(dist, L.terms.z);
         if (L.terms.w > 0.0 && dist > L.terms.w) atten = 0.0;
         if (type == 4)
         {
            float cosCur = dot(-lightDir, L.aim.xyz);
            float cosOuter = cos(L.spot.x * 0.017453292);
            float smoothW = (1.0 - cosOuter) * L.spot.y;
            float sa = cosCur > cosOuter ? cosCur : 0.0;
            if (smoothW > 0.0) sa *= smoothstep(0.0, smoothW, cosCur - cosOuter);
            atten *= sa;
         }
         if (atten <= 0.0 && type != 5) continue;
      }
      else lightDir = normalize(L.vec.xyz);
      vec3 halfway = normalize(lightDir + viewDir);
      float nDotL = max(dot(n, lightDir), 0.0);
      if (type == 5) nDotL = max(nDotL, 1e-3);
      float nlS = dot(n, lightDir);
      if (nDotL <= 0.0 && !((sssW > 0.0 && any(greaterThan(nlS + ssW, vec3(0.0)))) || (tlW > 0.0 && type != 5))) continue;
      vec3 nS = dot(n, lightDir) < 0.0 ? -n : n;
      float nDotH = max(dot(n, halfway), 0.0);
      float d = distGGX(nDotH, rough);
      float g = R3D_G2(nDotV, nDotL, rough);
      vec3 fr = fresnel(max(dot(halfway, viewDir), 0.0), f0);
      vec3 spec = (d * g * fr) / max(4.0 * nDotV * nDotL, 1e-7);
      spec *= ggxMs(f0, rough, nDotV) * iridK;
      vec3 kD = (vec3(1.0) - fr) * (1.0 - metal);
      vec3 radiance = radiance0 * atten;
      floorBounce += radiance * max(lightDir.y, 0.0) * L.terms.x;
      float shadow = 1.0;
      if (type == 5) shadow = areaE / max(atten * nDotL, 1e-6);
      else if (f.shP.x > 0.5 && i == int(f.shP.y)) shadow = shadowFactor(vWp, nS, lightDir);
      if (pomScale > 0.0)
         shadow *= pomShadow(int(m.mapNormal) - 1, mx, mb.xy, pomHt, mbx.xy, mby.xy,
                             vec3(dot(lightDir, pomT), dot(lightDir, pomB), dot(lightDir, normalize(vN))), pomScale);
      vec3 shS = vec3(0.0);
      float shK = 1.0;
      if (shW > 0.0)
      {
         float shL = m.sheenDir != 0.0 ? max(dot(shN, lightDir), 0.0) : nDotL;
         shS = m.sheenColor * sheenLtc(shL, shNV, dot(lightDir, viewDir), m.sheenRoughness) * shW * (m.sheenDir != 0.0 ? shL / max(nDotL, 1e-4) : 1.0);
         shK = shV;
      }
      vec3 dif = vec3(frontD * nDotL * shadow);
      if (sssW > 0.0) dif += (1.0 - tlW) * sssW * max(nlS + ssW, 0.0) / ((1.0 + ssW) * (1.0 + ssW)) * (nlS > 0.0 ? shadow : 1.0);
      if (tlW > 0.0) dif += tlW * max(-nlS, 0.0) * shadow;
      col += ((kD * base / 3.14159265 * dif * L.terms.x + spec * nDotL * shadow * L.terms.y) * shK + shS * nDotL * shadow * L.terms.y) * radiance;
      if (ccW > 0.0) coatCol += ccW * coatSpec(nDotH, nDotV, nDotL, max(dot(halfway, viewDir), 0.0), ccR) * L.terms.y * radiance * nDotL * shadow;
   }
   if (!(f.gi.x > 0.5)) col += base / 3.14159265 * floorBounce * (0.25 * (1.0 - n.y)) * (1.0 - metal);
   vec3 irradiance = sampleEnv(n, 1.0) * f.ambient.xyz * 3.0;
   vec3 fAmb = fresnelRough(nDotV, f0, rough) * iridK;
   vec3 envRefl = sampleEnv(reflect(-viewDir, n), rough) * fAmb * (aoV * ccAt * shV);
   col += ((vec3(1.0) - fAmb) * (1.0 - metal) * base * shV + shAmb) * irradiance * aoV;
   col += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
   float trans = clamp(m.transmission * transMap, 0.0, 1.0);
   if (trans > 0.0)
   {
      vec3 rd = refract(-viewDir, n, 1.0 / max(m.ior, 1.0001));
      if (dot(rd, rd) < 1e-6) rd = reflect(-viewDir, n);
      col = mix(col, sampleEnv(rd, 0.0) * mix(vec3(1.0), base, 0.4) * (vec3(1.0) - fAmb), trans);
   }
   col = col * ccAt + coatCol + (ccW > 0.0 ? sampleEnv(reflect(-viewDir, n), ccR) * (ccW * ccFv) : vec3(0.0));
   col += m.emission * emMap;
   vec3 fin = (col + envRefl) * f.params.x;
   fin = toneMap(fin, int(f.params.y));
   oC = vec4(toSrgb(fin), 1.0);
}
)";

// bg_fs without fog: the HDRI behind the scene (pc.bg: width, height; pc.invVP).
const char* kBgFrag = R"(
layout(location = 0) out vec4 oC;
void main()
{
   vec2 ndc = vec2(2.0 * gl_FragCoord.x / pc.bg.x - 1.0, 2.0 * gl_FragCoord.y / pc.bg.y - 1.0);
   vec4 pw = pc.invVP * vec4(ndc, 1.0, 1.0);
   vec3 c = sampleEnv(normalize(pw.xyz / pw.w - f.eye.xyz), 0.0) * f.params.x;
   oC = vec4(toSrgb(toneMap(c, int(f.params.y))), 1.0);
}
)";

const char* kTriVert = R"(#version 450
void main()
{
   vec2 p = gl_VertexIndex == 0 ? vec2(-0.6, -0.6) : gl_VertexIndex == 1 ? vec2(0.6, -0.6) : vec2(0.0, 0.6);
   gl_Position = vec4(p, 0.0, 1.0);
}
)";
const char* kTriFrag = R"(#version 450
layout(push_constant) uniform PC { vec4 c; } pc;
layout(location = 0) out vec4 oC;
void main() { oC = vec4(pc.c.rgb, 1.0); }
)";

bool Put(const std::string& dir, const char* name, const std::string& text)
{
   const std::string p = dir + "/" + name;
   FILE* f = AppPaths::OpenFile(p, "wb");
   if (!f)
      return false;
   std::fwrite(text.data(), 1, text.size(), f);
   std::fclose(f);
   return true;
}
}

int main(int argc, char** argv)
{
   if (argc < 2)
   {
      std::fprintf(stderr, "usage: VkShaderGen <outdir>\n");
      return 1;
   }
   const std::string d = argv[1];
   const std::string frag = kHead + kFragLib;
   const bool ok = Put(d, "raster.vert", kHead + kRasterVert) && Put(d, "shadow.vert", kHead + kShadowVert) &&
                   Put(d, "bg.vert", kHead + kSkyVert) && Put(d, "raster.frag", frag + kRasterFrag) &&
                   Put(d, "bg.frag", frag + kBgFrag) && Put(d, "tri.vert", kTriVert) && Put(d, "tri.frag", kTriFrag);
   // Row 9.4: the path tracer (compute, with the fragment library for the maps and the environment).
   const std::string comp = kHead + kFragLib + vkpt::kBind;
   const bool okPt = Put(d, "pt.comp", comp + vkpt::kTrace + vkpt::kPtMain) && Put(d, "dnprep.comp", comp + vkpt::kDnPrep) &&
                     Put(d, "dnatrous.comp", comp + vkpt::kDnAtrous) && Put(d, "ptres.frag", frag + vkpt::kBind + vkpt::kPtRes);
   return ok && okPt ? 0 : 1;
}

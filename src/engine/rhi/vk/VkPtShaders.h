#pragma once

// OPUS_PLAN row 9.4: the settle path tracer on Vulkan, ported from MetalDevice.mm's pt_fs / ptres_fs and
// Render3DDenoise.h's dn_prep_k / dn_atrous_k (included by VkShaderGen.cpp after its kHead and kFragLib).
// MoltenVK has no ray query, so rays walk a binned-SAH BVH over world-space triangles that VulkanDevice.cpp builds
// on the CPU (stack traversal, Moller-Trumbore). The tracer is a compute shader writing three RGBA32F running sums in
// place; ptres.frag (drawn with bg.vert) resolves them into the target.
// Set 1 (compute + fragment): 0 PtU, 1 lights, 2 emissive triangles, 3 per instance {first index, has uv},
// 4 BVH triangles (3 vec4: v0 + instance, e1 + primitive, e2 + flags), 5 BVH nodes (2 vec4: lo + left / first,
// hi + count, 0 = interior with children left, left + 1), 6 vertices (pos, normal), 7 indices (already global),
// 8 uvs (per global vertex), 9 env CDF, 10 E LUT, 11-13 acc / aux / gd, 14-17 denoiser images, 18 histogram.
// Push constants (kHead's PC): pt: bg = (prior samples, frame); denoise: bg = (step, sigL, source); resolve:
// bg = (samples, tonemap, exposure, denoised), clr = (clear sRGB, denoised image index).
// Not ported (logged in AUTORUN.md): Field material programs, motion blur, instance clusters / batches, bloom.

#include "nodes/Render3DHair.h"

namespace vkpt
{
const char* kBind = R"(
struct EmTri { vec4 p0; vec4 e1; vec4 e2; vec4 em; };
struct PtU { mat4 invVP; vec4 info; vec4 em; vec4 lens; vec4 lr; vec4 lu; vec4 lf; vec4 fog; vec4 fogC; vec4 fogLo; vec4 fogHi; };
layout(scalar, set = 1, binding = 0) readonly buffer PtB { PtU pu; };
layout(scalar, set = 1, binding = 1) readonly buffer LightB { LightU LL[]; };
layout(scalar, set = 1, binding = 2) readonly buffer EmB { EmTri ems[]; };
layout(scalar, set = 1, binding = 3) readonly buffer BaseB { uvec4 ibase[]; };
// The integer fields are uint, not bits inside a float: small integers are denormal floats and a GPU may flush them to 0.
struct BTri { vec3 a; uint inst; vec3 e1; uint prim; vec3 e2; uint flags; };
struct BNode { vec3 lo; uint first; vec3 hi; uint cnt; };
layout(scalar, set = 1, binding = 4) readonly buffer TriB { BTri btri[]; };
layout(scalar, set = 1, binding = 5) readonly buffer NodeB { BNode bnode[]; };
layout(scalar, set = 1, binding = 6) readonly buffer VtxB { float vbuf[]; };
layout(scalar, set = 1, binding = 7) readonly buffer IdxB { uint ibuf[]; };
layout(scalar, set = 1, binding = 8) readonly buffer UvB { float ubuf[]; };
layout(set = 1, binding = 9) uniform texture2D ecdf;
layout(set = 1, binding = 10) uniform texture2D eLut;
layout(set = 1, binding = 11, rgba32f) uniform image2D accI;
layout(set = 1, binding = 12, rgba32f) uniform image2D auxI;
layout(set = 1, binding = 13, rgba32f) uniform image2D gdI;
layout(set = 1, binding = 14, rgba32f) uniform image2D dnC0;
layout(set = 1, binding = 15, rgba32f) uniform image2D dnC1;
layout(set = 1, binding = 16, rgba32f) uniform image2D dnG;
layout(set = 1, binding = 17, rgba32f) uniform image2D dnA;
layout(std430, set = 1, binding = 18) buffer HistB { uint hist[64]; };
)";

const char* kTrace = R"(
#define HAIR_ATAN2(y, x) atan(y, x)
#define HAIR_OUT out float
)" R3D_HAIR_SRC R3D_AREA_SRC R"(
#define SSS_D 1.0
struct Hit { float t; uint inst; uint prim; vec2 bc; };
vec2 slab(vec3 lo, vec3 hi, vec3 o, vec3 inv, float tmax)
{
   vec3 a = (lo - o) * inv, c = (hi - o) * inv;
   vec3 l = min(a, c), h = max(a, c);
   return vec2(max(max(l.x, l.y), max(l.z, 0.0)), min(min(h.x, h.y), min(h.z, tmax)));
}
// Closest hit along o + d t, t in (0, tmax). casters: only triangles that cast shadows (Metal's mask bit 0).
bool trace(vec3 o, vec3 d, float tmax, bool casters, out Hit hit)
{
   hit.t = tmax; hit.inst = 0u; hit.prim = 0u; hit.bc = vec2(0.0);
   bool found = false;
   vec3 sg = mix(vec3(1.0), vec3(-1.0), lessThan(d, vec3(0.0)));
   vec3 inv = sg / max(abs(d), vec3(1e-12));
   vec2 r0 = slab(bnode[0].lo, bnode[0].hi, o, inv, tmax);
   if (r0.x > r0.y) return false;
   uint stack[64]; int sp = 0;
   uint node = 0u;
   for (int step = 0; step < 16384; step++) // bounded: a bad node can never hang the GPU
   {
      uint first = bnode[node].first, cnt = bnode[node].cnt;
      if (cnt > 0u)
      {
         for (uint k = first; k < first + cnt; k++)
         {
            if (casters && (btri[k].flags & 1u) == 0u) continue;
            vec3 a = btri[k].a, e1 = btri[k].e1, e2 = btri[k].e2;
            vec3 p = cross(d, e2);
            float det = dot(e1, p);
            if (abs(det) < 1e-24) continue;
            float id = 1.0 / det;
            vec3 s = o - a;
            float bu = dot(s, p) * id;
            if (bu < 0.0 || bu > 1.0) continue;
            vec3 q = cross(s, e1);
            float bv = dot(d, q) * id;
            if (bv < 0.0 || bu + bv > 1.0) continue;
            float t = dot(e2, q) * id;
            if (t > 0.0 && t < hit.t) { hit.t = t; hit.inst = btri[k].inst; hit.prim = btri[k].prim; hit.bc = vec2(bu, bv); found = true; }
         }
      }
      else
      {
         uint l = first, r = first + 1u;
         vec2 tl = slab(bnode[l].lo, bnode[l].hi, o, inv, hit.t);
         vec2 tr = slab(bnode[r].lo, bnode[r].hi, o, inv, hit.t);
         bool hl = tl.x <= tl.y, hr = tr.x <= tr.y;
         if (hl && hr)
         {
            bool lf = tl.x <= tr.x;
            if (sp < 64) stack[sp++] = lf ? r : l;
            node = lf ? l : r;
            continue;
         }
         if (hl) { node = l; continue; }
         if (hr) { node = r; continue; }
      }
      if (sp == 0) break;
      node = stack[--sp];
   }
   return found;
}
uvec3 triIx(uint iid, uint prim) { uint b = ibase[iid].x + prim * 3u; return uvec3(ibuf[b], ibuf[b + 1u], ibuf[b + 2u]); }
vec3 vP(uint v) { return vec3(vbuf[v * 6u], vbuf[v * 6u + 1u], vbuf[v * 6u + 2u]); }
vec3 vN(uint v) { return vec3(vbuf[v * 6u + 3u], vbuf[v * 6u + 4u], vbuf[v * 6u + 5u]); }
vec2 vT(uint v) { return vec2(ubuf[v * 2u], ubuf[v * 2u + 1u]); }
bool hasUv(uint iid) { return ibase[iid].y != 0u; }
vec3 ptF(float c, vec3 f0) { return f0 + (vec3(1.0) - f0) * ptS5(c); }
vec3 ptMs(vec3 f0, float rough, float nV)
{
   float E = clamp(textureLod(sampler2D(eLut, sC), vec2(nV, rough), 0.0).r, 0.05, 1.0);
   return vec3(1.0) + f0 * (1.0 / E - 1.0);
}
vec2 ptProbs(vec3 f0, float metal, float nV)
{
   float f0s = max(f0.x, max(f0.y, f0.z)), fV = f0s + (1.0 - f0s) * ptS5(nV);
   float spec = mix(fV, 1.0, metal);
   float diff = (1.0 - metal) * (1.0 - fV);
   spec = max(spec, 0.05 * (spec + diff));
   return vec2(spec, diff) / max(spec + diff, 1e-6);
}
vec3 ptEval(vec3 n, vec3 V, vec3 L, vec3 base, float metal, float rough, vec3 f0, vec2 w, vec3 ms, out float pdf)
{
   float nL = dot(n, L);
   if (nL <= 1e-6) { pdf = 0.0; return vec3(0.0); }
   float nV = max(dot(n, V), 1e-4);
   vec3 H = normalize(V + L);
   float nH = max(dot(n, H), 0.0), vH = max(dot(V, H), 1e-4);
   float a = max(0.002, rough * rough);
   float D = ptD(nH, a);
   vec3 F = ptF(vH, f0);
   vec3 fr = D * ptVis(nV, nL, a) * F * ms * w.y + (vec3(1.0) - F) * (1.0 - metal) * base * (w.x / 3.14159265);
   vec2 p = ptProbs(f0, metal, nV);
   pdf = p.x * D * nH / max(4.0 * vH, 1e-6) + p.y * nL / 3.14159265;
   return fr * nL;
}
vec3 ptEvalT(vec3 n, vec3 V, vec3 L, vec3 base, float metal, float rough, vec3 f0, vec2 w, vec3 ms, out float pdf)
{
   float nL = dot(n, L), nV = max(dot(n, V), 1e-4);
   vec2 p = ptProbs(f0, metal, nV);
   if (nL > 1e-6)
   {
      vec3 fs = ptEval(n, V, L, base, metal, rough, f0, vec2(0.0, w.y), ms, pdf);
      pdf -= p.y * nL / 3.14159265;
      return fs;
   }
   vec3 Fv = ptF(nV, f0);
   pdf = p.y * (-nL) / 3.14159265;
   return (vec3(1.0) - Fv) * (1.0 - metal) * base * (w.x / 3.14159265) * (-nL);
}
bool occludedAlpha(vec3 o, vec3 d, float tmax, float eps, inout uint seed)
{
   for (int k = 0; k < 16; k++)
   {
      Hit h;
      if (!trace(o, d, tmax, true, h)) return false;
      float alpha = 1.0 - inst[h.inst].mat.clear;
      float mo = inst[h.inst].mat.mapOpacity;
      if (mo > 0.5)
      {
         uvec3 ix = triIx(h.inst, h.prim);
         vec3 w3 = vec3(1.0 - h.bc.x - h.bc.y, h.bc.x, h.bc.y);
         vec2 uv = hasUv(h.inst) ? vT(ix.x) * w3.x + vT(ix.y) * w3.y + vT(ix.z) * w3.z : vec2(0.0);
         vec3 lp = vP(ix.x) * w3.x + vP(ix.y) * w3.y + vP(ix.z) * w3.z;
         vec3 nl = vN(ix.x) * w3.x + vN(ix.y) * w3.y + vN(ix.z) * w3.z;
         vec3 n = normalize((inst[h.inst].normalM * vec4(nl, 0.0)).xyz);
         MapX mx = mapX(inst[h.inst].mat);
         alpha *= mapSample(int(mo) - 1, mx, mapBase(mx, uv, lp), n, vec3(0.0), vec3(0.0), 0.0).r;
      }
      float cut = inst[h.inst].mat.alphaCutoff;
      if (!(alpha < 1.0 && alpha < (cut > 0.0 ? cut : rnd(seed)))) return true;
      o += d * (h.t + eps); tmax -= h.t + eps;
      if (tmax <= 0.0) return false;
   }
   return true;
}
void ptPickLight(int nEm, float pEm, int count, vec3 p, inout uint seed, inout vec3 ld, inout vec3 Le, inout float tmax, inout float pL, inout vec2 lterms)
{
   if (nEm > 0 && rnd(seed) < pEm)
   {
      float r = rnd(seed);
      int lo = 0, hi = nEm - 1;
      while (lo < hi) { int mid = (lo + hi) / 2; if (ems[mid].p0.w < r) lo = mid + 1; else hi = mid; }
      EmTri t = ems[lo];
      float su = sqrt(rnd(seed)), u2 = rnd(seed);
      vec3 tl = t.p0.xyz + t.e1.xyz * (su * (1.0 - u2)) + t.e2.xyz * (su * u2) - p;
      float dist = length(tl); ld = tl / max(dist, 1e-6);
      vec3 gn = cross(t.e1.xyz, t.e2.xyz);
      float cosL = abs(dot(gn, ld)) / max(length(gn), 1e-12);
      if (cosL > 1e-4 && t.e1.w > 0.0)
      {
         pL = pEm * t.e1.w * dist * dist / cosL;
         Le = t.em.xyz; tmax = max(dist * (1.0 - 1e-3) - f.shQ.w, 0.0);
      }
   }
   else if (count > 0)
   {
      LightU Lt = LL[min(int(rnd(seed) * float(count)), count - 1)];
      int type = int(Lt.vec.w);
      float atten = type == 3 ? 0.0 : 1.0;
      tmax = 1e4;
      if (type == 1 || type == 4 || type == 5)
      {
         vec3 lp = Lt.vec.xyz;
         if (type == 5)
         {
            vec3 rgt = cross(vec3(0.0, 1.0, 0.0), Lt.aim.xyz);
            rgt = dot(rgt, rgt) < 1e-4 ? vec3(1.0, 0.0, 0.0) : normalize(rgt);
            vec3 upv = cross(Lt.aim.xyz, rgt);
            float u1 = rnd(seed), u2 = rnd(seed);
            vec3 ao = areaOffset(Lt.spot.x, u1, u2);
            lp += rgt * (ao.x * Lt.spot.z) + upv * (ao.y * Lt.spot.w);
         }
         vec3 tl = lp - p; float dist = length(tl);
         ld = tl / max(dist, 1e-4); tmax = max(dist - 0.01, 0.0);
         atten *= lightFalloff(dist, Lt.terms.z);
         if (Lt.terms.w > 0.0 && dist > Lt.terms.w) atten = 0.0;
         if (type == 4)
         {
            float cosCur = dot(-ld, Lt.aim.xyz), cosOuter = cos(Lt.spot.x * 0.017453292);
            float smoothW = (1.0 - cosOuter) * Lt.spot.y;
            float sa = cosCur > cosOuter ? cosCur : 0.0;
            if (smoothW > 0.0) sa *= smoothstep(0.0, smoothW, cosCur - cosOuter);
            atten *= sa;
         }
         if (type == 5) { float cl = dot(-ld, Lt.aim.xyz); atten *= max(cl, 0.0) * areaSpreadK(Lt.spot.y, cl); }
      }
      else
      {
         ld = normalize(Lt.vec.xyz);
         if ((type == 0 || type == 2) && Lt.spot.x > 0.0)
         {
            float cm = cos(Lt.spot.x * 0.5 * 0.017453292);
            float ct = 1.0 - rnd(seed) * (1.0 - cm), st = sqrt(max(1.0 - ct * ct, 0.0)), ph = 6.28318530718 * rnd(seed);
            vec3 ax = abs(ld.y) < 0.99 ? normalize(cross(ld, vec3(0.0, 1.0, 0.0))) : vec3(1.0, 0.0, 0.0);
            ld = normalize(ld * ct + (ax * cos(ph) + cross(ld, ax) * sin(ph)) * st);
         }
      }
      Le = Lt.color.xyz * atten * (float(count) / max(1.0 - pEm, 1e-3));
      lterms = Lt.terms.xy;
      if (Lt.color.w > 0.5) pL = -2.0;
   }
}
vec3 envPick(vec2 u, out float pdf)
{
   ivec2 sz = textureSize(ecdf, 0);
   int NX = sz.x, NY = (sz.y - 1) / 2;
   int lo = 0, hi = NY - 1;
   for (int i = 0; i < 12 && lo < hi; i++) { int mid = (lo + hi) / 2; if (texelFetch(ecdf, ivec2(mid, NY), 0).r < u.y) lo = mid + 1; else hi = mid; }
   int y = lo;
   float m0 = y > 0 ? texelFetch(ecdf, ivec2(y - 1, NY), 0).r : 0.0, m1 = texelFetch(ecdf, ivec2(y, NY), 0).r;
   lo = 0; hi = NX - 1;
   for (int i = 0; i < 12 && lo < hi; i++) { int mid = (lo + hi) / 2; if (texelFetch(ecdf, ivec2(mid, y), 0).r < u.x) lo = mid + 1; else hi = mid; }
   int x = lo;
   float c0 = x > 0 ? texelFetch(ecdf, ivec2(x - 1, y), 0).r : 0.0, c1 = texelFetch(ecdf, ivec2(x, y), 0).r;
   vec2 uv = vec2((float(x) + clamp((u.x - c0) / max(c1 - c0, 1e-12), 0.0, 1.0)) / float(NX),
                  (float(y) + clamp((u.y - m0) / max(m1 - m0, 1e-12), 0.0, 1.0)) / float(NY));
   float th = uv.y * 3.14159265359, ph = (uv.x - 0.5) * 6.28318530718 - f.envm.y, st = sin(th);
   pdf = texelFetch(ecdf, ivec2(x, NY + 1 + y), 0).r / (19.7392088 * max(st, 1e-4));
   return vec3(st * cos(ph), cos(th), st * sin(ph));
}
float envPdf(vec3 dir)
{
   ivec2 sz = textureSize(ecdf, 0);
   int NX = sz.x, NY = (sz.y - 1) / 2;
   vec2 uv = vec2(fract((atan(dir.z, dir.x) + f.envm.y) / 6.28318530718 + 0.5), acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265359);
   int x = min(int(uv.x * float(NX)), NX - 1), y = min(int(uv.y * float(NY)), NY - 1);
   return texelFetch(ecdf, ivec2(x, NY + 1 + y), 0).r / (19.7392088 * max(sqrt(max(1.0 - dir.y * dir.y, 0.0)), 1e-4));
}
vec2 boxSpan(vec3 lo, vec3 hi, vec3 o, vec3 d, float tmax)
{
   vec3 inv = 1.0 / mix(d, vec3(1e-12), lessThan(abs(d), vec3(1e-12)));
   vec3 a = (lo - o) * inv, c = (hi - o) * inv;
   vec3 l = min(a, c), h = max(a, c);
   return vec2(max(max(l.x, l.y), max(l.z, 0.0)), min(min(h.x, h.y), min(h.z, tmax)));
}
vec2 fogSpan(vec3 o, vec3 d, float tmax) { return boxSpan(pu.fogLo.xyz, pu.fogHi.xyz, o, d, tmax); }
float fogTr(vec3 o, vec3 d, float tmax)
{
   if (pu.fog.x <= 0.0) return 1.0;
   vec2 s = fogSpan(o, d, tmax);
   return exp(-pu.fog.x * max(s.y - s.x, 0.0));
}
vec3 hgSample(vec3 w, float g, float u1, float u2)
{
   float c = 1.0 - 2.0 * u1;
   if (abs(g) > 1e-3) { float q = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1); c = (1.0 + g * g - q * q) / (2.0 * g); }
   c = clamp(c, -1.0, 1.0);
   float st = sqrt(max(0.0, 1.0 - c * c)), ph = 6.28318530718 * u2;
   vec3 t = normalize(cross(w, abs(w.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
   return normalize(t * (st * cos(ph)) + cross(w, t) * (st * sin(ph)) + w * c);
}
vec3 hairSample(vec3 wo, vec3 T, vec3 sa, float bm, float bn, float alpha, float eta, float h, vec4 u)
{
   float sto = clamp(dot(wo, T), -1.0, 1.0), cto = sqrt(max(1.0 - sto * sto, 0.0));
   vec3 X = hairX(wo, T, sto), Y = cross(T, X);
   float stt = sto / eta, ctt = sqrt(max(1.0 - stt * stt, 1e-4));
   float etap = sqrt(max(eta * eta - sto * sto, 0.0)) / max(cto, 1e-4);
   float sgt = clamp(h / etap, -1.0, 1.0), cgt = sqrt(max(1.0 - sgt * sgt, 0.0));
   float go = asin(clamp(h, -1.0, 1.0)), gt = asin(sgt);
   vec3 tr = exp(-sa * (2.0 * cgt / ctt));
   float fr = hairFresnel(cto * sqrt(max(1.0 - h * h, 0.0)), eta);
   vec3 a0 = vec3(fr), a1 = (1.0 - fr) * (1.0 - fr) * tr, a2 = a1 * tr * fr;
   vec3 a3 = a2 * tr * fr / max(vec3(1.0) - tr * fr, vec3(1e-4));
   vec3 lw = vec3(0.2126, 0.7152, 0.0722);
   vec4 ap = vec4(dot(a0, lw), dot(a1, lw), dot(a2, lw), dot(a3, lw)); ap /= max(ap.x + ap.y + ap.z + ap.w, 1e-6);
   int p = u.x < ap.x ? 0 : u.x < ap.x + ap.y ? 1 : u.x < ap.x + ap.y + ap.z ? 2 : 3;
   float v0 = hairV0(bm), s = hairS(bn);
   float v = p == 0 ? v0 : p == 1 ? 0.25 * v0 : 4.0 * v0;
   float ta = p == 0 ? -2.0 * alpha : p == 1 ? alpha : p == 2 ? 4.0 * alpha : 0.0;
   float stp = sto * cos(ta) + cto * sin(ta), ctp = abs(cto * cos(ta) - sto * sin(ta));
   float u1 = max(u.y, 1e-5);
   float ct = 1.0 + v * log(u1 + (1.0 - u1) * exp(-2.0 / v)), st = sqrt(max(1.0 - ct * ct, 0.0));
   float sti = clamp(-ct * stp + st * cos(6.2831853 * u.z) * ctp, -1.0, 1.0), cti = sqrt(max(1.0 - sti * sti, 0.0));
   float phi;
   if (p < 3)
   {
      float lo = hairCdf(-3.14159265, s), k = hairCdf(3.14159265, s) - lo;
      float x = -s * log(1.0 / max(u.w * k + lo, 1e-7) - 1.0);
      phi = hairPhi(float(p), go, gt) + clamp(x, -3.14159265, 3.14159265);
   }
   else phi = 6.2831853 * u.w;
   return normalize(T * sti + (X * cos(phi) + Y * sin(phi)) * cti);
}
bool anyPos(vec3 v) { return any(greaterThan(v, vec3(0.0))); }
void roulette(int b, inout vec3 T, inout uint seed, inout bool dead)
{
   if (b < 2) return;
   float q = clamp(max(T.x, max(T.y, T.z)), 0.05, 0.95);
   if (rnd(seed) > q) { dead = true; return; }
   T /= q;
}
)";

// pt_fs as a compute kernel: one sample per pixel per dispatch, added to the running sums in place.
const char* kPtMain = R"(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
   ivec2 sz = imageSize(accI);
   ivec2 px = ivec2(gl_GlobalInvocationID.xy);
   if (px.x >= sz.x || px.y >= sz.y) return;
   uint seed = pcg(uint(px.x) + 8192u * uint(px.y)) ^ pcg(uint(pc.bg.y) * 2654435761u + 17u);
   float W = float(sz.x), H = float(sz.y);
   vec2 ndc = vec2(2.0 * (float(px.x) + rnd(seed)) / W - 1.0, 2.0 * (float(px.y) + rnd(seed)) / H - 1.0);
   vec2 ndc0 = ndc;
   vec3 chan = vec3(1.0);
   if (pu.lf.w != 0.0) ndc *= 1.0 + pu.lf.w * dot(ndc, ndc);
   if (pu.fogC.w != 0.0)
   {
      int c = min(int(rnd(seed) * 3.0), 2);
      ndc *= 1.0 + pu.fogC.w * float(c - 1);
      chan = vec3(c == 0 ? 3.0 : 0.0, c == 1 ? 3.0 : 0.0, c == 2 ? 3.0 : 0.0);
   }
   vec4 pw = pu.invVP * vec4(ndc, 1.0, 1.0);
   vec3 o = f.eye.xyz;
   vec3 d = normalize(pw.xyz / pw.w - o);
   if (pu.lens.x > 0.0)
   {
      vec3 fp = o + d * (pu.lens.y / max(dot(d, pu.lf.xyz), 1e-4));
      vec2 off = vec2(0.0);
      vec2 eye = pu.lu.w * ndc0 * vec2(W / H, 1.0) * 0.5;
      for (int tr = 0; tr < 8; tr++)
      {
         vec2 lu = vec2(rnd(seed), rnd(seed));
         if (pu.lens.z >= 3.0)
         {
            float seg = 6.2831853 / pu.lens.z, a0 = pu.lens.w + floor(rnd(seed) * pu.lens.z) * seg;
            off = sqrt(lu.x) * mix(vec2(cos(a0), sin(a0)), vec2(cos(a0 + seg), sin(a0 + seg)), lu.y);
         }
         else
            off = sqrt(lu.x) * vec2(cos(6.2831853 * lu.y), sin(6.2831853 * lu.y));
         if (pu.lu.w <= 0.0 || dot(off - eye, off - eye) <= 1.0) break;
      }
      off *= pu.lr.w >= 1.0 ? vec2(1.0 / pu.lr.w, 1.0) : vec2(1.0, pu.lr.w);
      o += (off.x * pu.lr.xyz + off.y * pu.lu.xyz) * pu.lens.x;
      d = normalize(fp - o);
   }
   vec3 L = vec3(0.0), T = vec3(1.0), L0 = vec3(0.0);
   float cov = 0.0;
   vec3 alb0 = vec3(0.0), n0 = vec3(0.0); float dist0 = 0.0;
   int maxB = int(pu.info.y);
   int nEm = int(pu.em.x); float pEm = pu.em.y;
   float lastPdfB = 0.0;
   float envMis = 0.0;
   vec3 skyK = vec3(1.0);
   int skips = 0;
   bool dead = false;
   int count = int(f.fld.y + 0.5);
   for (int b = 0; b < maxB; b++)
   {
      Hit h;
      bool hitAny = trace(o, d, 1e4, false, h);
      if (pu.fog.x > 0.0)
      {
         vec2 sp = fogSpan(o, d, hitAny ? h.t : 1e4);
         float s = sp.x - log(max(1.0 - rnd(seed), 1e-12)) / pu.fog.x;
         if (sp.y > sp.x && s < sp.y)
         {
            vec3 pm = o + d * s;
            if (b == 0) { cov = 1.0; alb0 = pu.fogC.xyz; n0 = -d; dist0 = s; }
            T *= pu.fogC.xyz;
            vec3 ld = vec3(0.0, 1.0, 0.0), Le = vec3(0.0); float tmax = 0.0, pL = -1.0; vec2 lterms = vec2(1.0);
            ptPickLight(nEm, pEm, count, pm, seed, ld, Le, tmax, pL, lterms);
            if (anyPos(Le))
            {
               if (pL > -1.5 && occludedAlpha(pm, ld, tmax, f.shQ.w, seed)) Le *= pL > 0.0 ? 0.0 : 1.0 - f.shP.w;
               float ph = hgPhase(dot(ld, d), pu.fog.y);
               L += T * ph * Le * lterms.x * fogTr(pm, ld, tmax) * (pL > 0.0 ? pL / max(pL * pL + ph * ph, 1e-20) : 1.0);
            }
            vec3 nd = hgSample(d, pu.fog.y, rnd(seed), rnd(seed));
            lastPdfB = hgPhase(dot(nd, d), pu.fog.y); envMis = 0.0; skyK = vec3(1.0);
            o = pm; d = nd;
            roulette(b, T, seed, dead); if (dead) break;
            continue;
         }
      }
      if (!hitAny)
      {
         float wS = 1.0;
         if (envMis > 0.5) { float pe = envPdf(d); wS = lastPdfB * lastPdfB / max(lastPdfB * lastPdfB + pe * pe, 1e-20); }
         if (b > 0) L += T * sampleEnv(d, 0.0) * skyK * wS;
         else if ((int(f.envm.x) & 2) != 0) { L = sampleEnv(d, 0.0); cov = 1.0; }
         break;
      }
      if (b == 0) cov = 1.0;
      envMis = 0.0;
      uint iid = h.inst;
      mat4 NM = inst[iid].normalM;
      uvec3 ix = triIx(iid, h.prim);
      vec3 w3 = vec3(1.0 - h.bc.x - h.bc.y, h.bc.x, h.bc.y);
      vec3 nl = vN(ix.x) * w3.x + vN(ix.y) * w3.y + vN(ix.z) * w3.z;
      vec3 n = normalize((NM * vec4(nl, 0.0)).xyz);
      vec3 hT = n;
      bool backHit = dot(n, d) > 0.0;
      if (backHit) n = -n;
      bool hairM = inst[iid].mat.hairRadialNeg < 0.0 && float(h.prim) >= inst[iid].mat.hairFrom;
      vec3 p0 = vP(ix.x), q1 = vP(ix.y), q2 = vP(ix.z);
      if (hairM)
      {
         vec3 gn = (NM * vec4(cross(q1 - p0, q2 - p0), 0.0)).xyz;
         if (dot(gn, gn) > 1e-30) { gn = normalize(gn); n = dot(gn, d) > 0.0 ? -gn : gn; }
      }
      vec3 p = o + d * h.t;
      vec3 base = hairM ? hairAlbedo(inst[iid].mat.hairSigma, -inst[iid].mat.hairRadialNeg) : inst[iid].mat.base;
      float metal = hairM ? 0.0 : clamp(inst[iid].mat.metallic, 0.0, 1.0);
      float rough = clamp(inst[iid].mat.roughness, 0.045, 1.0);
      vec3 ns = n;
      vec3 emMap = vec3(1.0); float shW = hairM ? 0.0 : inst[iid].mat.sheen, ccW = hairM ? 0.0 : inst[iid].mat.coat;
      bool uvOk = hasUv(iid);
      vec2 t0 = uvOk ? vT(ix.x) : vec2(0.0), t1 = uvOk ? vT(ix.y) : vec2(0.0), t2 = uvOk ? vT(ix.z) : vec2(0.0);
      if (inst[iid].mat.mapAlbedo > 0.5 || inst[iid].mat.mapRoughness > 0.5 || inst[iid].mat.mapMetallic > 0.5 || inst[iid].mat.mapNormal > 0.5 ||
          inst[iid].mat.mapAo > 0.5 || inst[iid].mat.mapEmission > 0.5 || inst[iid].mat.mapSheen > 0.5 || inst[iid].mat.mapOpacity > 0.5 ||
          inst[iid].mat.mapCoat > 0.5 || inst[iid].mat.mapTransmission > 0.5 || inst[iid].mat.hairShift > 0.5)
      {
         vec2 uv = t0 * w3.x + t1 * w3.y + t2 * w3.z;
         vec3 lp = p0 * w3.x + q1 * w3.y + q2 * w3.z;
         float lod = b == 0 ? 0.0 : 3.0;
         MapX mx = mapX(inst[iid].mat);
         vec3 mb = mapBase(mx, uv, lp);
#define MAPT(slot) mapSample(int(slot) - 1, mx, mb, n, vec3(0.0), vec3(0.0), lod)
         if (inst[iid].mat.mapAlbedo > 0.5) base *= mapLin(MAPT(inst[iid].mat.mapAlbedo).rgb);
         if (inst[iid].mat.mapRoughness > 0.5) rough = clamp(inst[iid].mat.roughness * MAPT(inst[iid].mat.mapRoughness).r, 0.045, 1.0);
         if (inst[iid].mat.mapMetallic > 0.5) metal = clamp(inst[iid].mat.metallic * MAPT(inst[iid].mat.mapMetallic).r, 0.0, 1.0);
         if (inst[iid].mat.mapEmission > 0.5) emMap = mapLin(MAPT(inst[iid].mat.mapEmission).rgb);
         if (inst[iid].mat.mapSheen > 0.5) shW *= MAPT(inst[iid].mat.mapSheen).r;
         if (inst[iid].mat.mapCoat > 0.5) ccW *= MAPT(inst[iid].mat.mapCoat).r;
         float alpha = 1.0 - inst[iid].mat.clear;
         if (inst[iid].mat.mapOpacity > 0.5) alpha *= MAPT(inst[iid].mat.mapOpacity).r;
#undef MAPT
         float cut = inst[iid].mat.alphaCutoff;
         if (alpha < 1.0 && skips < 16 && alpha < (cut > 0.0 ? cut : rnd(seed)))
         {
            skips++; if (b == 0) cov = 0.0;
            o = p + d * f.shQ.w; b--; continue;
         }
         if (inst[iid].mat.mapNormal > 0.5 && !hairM)
         {
            vec3 e1 = q1 - p0, e2 = q2 - p0;
            vec2 mu0 = mapUv(mx, mapBase(mx, t0, p0), n), mu1 = mapUv(mx, mapBase(mx, t1, q1), n), mu2 = mapUv(mx, mapBase(mx, t2, q2), n);
            vec2 d1 = mu1 - mu0, d2 = mu2 - mu0;
            float det = d1.x * d2.y - d2.x * d1.y;
            if (abs(det) > 1e-12)
            {
               vec3 tg = normalize((NM * vec4((e1 * d2.y - e2 * d1.y) / det, 0.0)).xyz);
               vec3 bgv = (NM * vec4((e2 * d1.x - e1 * d2.x) / det, 0.0)).xyz;
               tg = normalize(tg - n * dot(n, tg));
               vec3 bt = cross(n, tg);
               if (dot(bt, bgv) < 0.0) bt = -bt;
               if (backHit) { tg = -tg; bt = -bt; }
               vec3 s = mapNormalTS(int(inst[iid].mat.mapNormal) - 1, mx, (int(inst[iid].mat.wrapBump + 0.5) & 4) != 0, inst[iid].mat.normalStrength,
                                    mapUv(mx, mb, n), vec2(0.0), vec2(0.0), lod, inst[iid].mat.detailScale, inst[iid].mat.detailStrength);
               n = normalize(tg * s.x + bt * s.y + n * s.z);
            }
         }
      }
      if (b == 0) { alb0 = base; n0 = n; dist0 = h.t; }
      vec3 emis = inst[iid].mat.emission;
      if (anyPos(emis))
      {
         float wE = 1.0;
         if (b > 0 && nEm > 0 && inst[iid].mat.emTri >= 0.0)
         {
            EmTri t = ems[int(inst[iid].mat.emTri) + int(h.prim)];
            vec3 gn = cross(t.e1.xyz, t.e2.xyz);
            float cosL = abs(dot(gn, d)) / max(length(gn), 1e-12);
            float pL = pEm * t.e1.w * h.t * h.t / max(cosL, 1e-6);
            wE = lastPdfB * lastPdfB / max(lastPdfB * lastPdfB + pL * pL, 1e-20);
         }
         vec3 eHit = T * emis * emMap * wE;
         if (b == 0) L0 += eHit; else L += eHit;
      }
      vec3 V = -d; float nDotV = max(dot(n, V), 1e-4);
      vec3 shN = n;
      float sheenDir = inst[iid].mat.sheenDir, sheenR = inst[iid].mat.sheenRoughness;
      vec3 sheenC = inst[iid].mat.sheenColor;
      if (shW > 0.0 && sheenDir != 0.0 && uvOk)
      {
         vec2 d1 = t1 - t0, d2 = t2 - t0;
         vec3 e1 = q1 - p0, e2 = q2 - p0;
         float det = d1.x * d2.y - d2.x * d1.y;
         if (abs(det) > 1e-12) shN = sheenTilt(n, (NM * vec4((e1 * d2.y - e2 * d1.y) / det, 0.0)).xyz, sheenDir);
      }
      float shNV = max(dot(shN, V), 1e-4);
      float shMax = shW * maxc(sheenC), shV = shMax > 0.0 ? 1.0 - shMax * sheenE(shNV, sheenR) : 1.0;
      vec3 f0 = mix(vec3(0.04 + inst[iid].mat.f0Extra), base, metal);
      vec3 msK = ptMs(f0, rough, nDotV) * iridGain(inst[iid].mat.iridescence, inst[iid].mat.iridIor, inst[iid].mat.iridThickness, nDotV, f0);
      int dm = 0;
      vec3 sssR = inst[iid].mat.sssRadius;
      if (!hairM && metal < 1.0 && (inst[iid].mat.subsurface > 0.0 || inst[iid].mat.translucency > 0.0))
      {
         float tlW = clamp(inst[iid].mat.translucency, 0.0, 1.0), ssW = clamp(inst[iid].mat.subsurface, 0.0, 1.0) * (1.0 - tlW);
         float um = rnd(seed);
         dm = um < tlW ? 2 : (um < tlW + ssW && max(sssR.x, max(sssR.y, sssR.z)) > 1e-4 ? 1 : 0);
      }
      float glW = hairM ? 0.0 : clamp(inst[iid].mat.transmission, 0.0, 1.0) * (1.0 - metal);
      if (glW > 0.0 && rnd(seed) < glW)
      {
         float ta = max(0.002, rough * rough);
         vec3 Hm = ggxSampleH(n, ta, rnd(seed), rnd(seed));
         float cosI = dot(V, Hm);
         if (cosI <= 1e-5) break;
         float ior = max(inst[iid].mat.ior, 1.0001), eta = backHit ? ior : 1.0 / ior;
         vec3 nd; bool thru = false;
         if (rnd(seed) < ptFd(cosI, eta)) { nd = reflect(d, Hm); if (dot(ns, nd) <= 0.0) break; }
         else { nd = refract(d, Hm, eta); if (dot(nd, nd) < 1e-6 || dot(ns, nd) >= 0.0) break; T *= base; thru = true; }
         float gV = max(abs(dot(n, V)), 1e-4), gL = max(abs(dot(n, nd)), 1e-4), gH = max(dot(n, Hm), 1e-4);
         T *= min(cosI * ptG1(gV, ta) * ptG1(gL, ta) / (gV * gH), 4.0);
         lastPdfB = 1e8; skyK = vec3(1.0);
         roulette(b, T, seed, dead); if (dead) break;
         o = p + (thru ? -ns : ns) * f.shQ.w; d = normalize(nd);
         continue;
      }
      if (b == 0) L += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
      for (int i = 0; i < count; i++)
         if (int(LL[i].vec.w) == 3) L += T * base * (1.0 - metal) * LL[i].color.xyz * LL[i].terms.x;
      vec3 ld = vec3(0.0, 1.0, 0.0), Le = vec3(0.0); float tmax = 0.0, pL = -1.0; vec2 lterms = vec2(1.0);
      ptPickLight(nEm, pEm, count, p, seed, ld, Le, tmax, pL, lterms);
      float hH = hairM ? rnd(seed) * 2.0 - 1.0 : 0.0;
      float hBm = clamp(inst[iid].mat.roughness, 0.02, 1.0), hBn = -inst[iid].mat.hairRadialNeg;
      vec3 hSig = inst[iid].mat.hairSigma; float hShift = inst[iid].mat.hairShift, mIor = inst[iid].mat.ior;
      float ccRough = clamp(inst[iid].mat.coatRoughness, 0.02, 1.0);
      if ((hairM || (dot(n, ld) > 1e-5 && dot(ns, ld) > 0.0) || (dm == 2 && dot(n, ld) < -1e-5 && dot(ns, ld) < 0.0)) && anyPos(Le))
      {
         if (pL > -1.5 && occludedAlpha(p + (dot(ns, ld) < 0.0 ? -ns : ns) * f.shQ.w, ld, tmax, f.shQ.w, seed)) Le *= pL > 0.0 ? 0.0 : 1.0 - f.shP.w;
         Le *= fogTr(p, ld, tmax);
         float pB = 0.0;
         vec3 fe;
         if (hairM) fe = hairBsdf(V, ld, hT, hSig, hBm, hBn, hShift, mIor, hH, pB) * lterms.x;
         else if (dm == 2) fe = ptEvalT(n, V, ld, base, metal, rough, f0, lterms, msK, pB);
         else fe = ptEval(n, V, ld, base, metal, rough, f0, dm == 1 ? vec2(0.0, lterms.y) : lterms, msK, pB);
         if (shW > 0.0 || ccW > 0.0)
         {
            float nL = max(dot(n, ld), 0.0), nH = max(dot(n, normalize(V + ld)), 0.0), vH = max(dot(V, normalize(V + ld)), 0.0);
            float cF = 0.04 + 0.96 * pow(1.0 - nDotV, 5.0);
            fe *= 1.0 - ccW * cF;
            if (shW > 0.0)
               fe = fe * shV + sheenC * (sheenDir != 0.0 ? sheenLtc(max(dot(shN, ld), 0.0), shNV, dot(ld, V), sheenR) * max(dot(shN, ld), 0.0)
                                                          : sheenLtc(nL, nDotV, dot(ld, V), sheenR) * nL) * shW * lterms.y;
            if (ccW > 0.0) fe += ccW * coatSpec(nH, nDotV, max(nL, 1e-4), vH, ccRough) * nL * lterms.y;
         }
         L += T * fe * Le * (pL > 0.0 ? pL / max(pL * pL + pB * pB, 1e-20) : 1.0);
      }
      if (!hairM && dm != 1 && pu.em.z > 0.5)
      {
         envMis = 1.0;
         float pE = 0.0;
         vec3 ed = envPick(vec2(rnd(seed), rnd(seed)), pE);
         float eL = dot(n, ed);
         bool under = dm == 2 && eL < 0.0;
         if (pE > 0.0 && abs(eL) > 1e-5 && (dot(ns, ed) > 0.0) != under)
         {
            float pB = 0.0;
            vec3 fe = dm == 2 ? ptEvalT(n, V, ed, base, metal, rough, f0, vec2(1.0), msK, pB) : ptEval(n, V, ed, base, metal, rough, f0, vec2(1.0), msK, pB);
            float nLe = max(eL, 0.0);
            if (shW > 0.0) fe = fe * shV + sheenC * (sheenDir != 0.0 ? sheenLtc(max(dot(shN, ed), 0.0), shNV, dot(ed, V), sheenR) * max(dot(shN, ed), 0.0)
                                                                       : sheenLtc(nLe, nDotV, dot(ed, V), sheenR) * nLe) * shW;
            fe *= mix(vec3(1.0), f.ambient.xyz * 3.0, clamp((1.0 - ptProbs(f0, metal, nDotV).x) * abs(eL) / 3.14159265 / max(pB, 1e-6), 0.0, 1.0));
            float pBm = pB;
            if (ccW > 0.0)
            {
               float cP = clamp(ccW * (0.04 + 0.96 * pow(1.0 - nDotV, 5.0)), 0.0, 0.95);
               vec3 hc = normalize(V + ed);
               float cnH = max(dot(n, hc), 0.0), cvH = max(dot(V, hc), 1e-4);
               fe = fe * (1.0 - cP) + (eL > 0.0 ? vec3(ccW * coatSpec(cnH, nDotV, max(eL, 1e-4), cvH, ccRough) * eL) : vec3(0.0));
               pBm = cP * (eL > 0.0 ? distGGX(cnH, ccRough) * cnH / (4.0 * cvH) : 0.0) + (1.0 - cP) * pB;
            }
            if (anyPos(fe) && !occludedAlpha(p + (dot(ns, ed) < 0.0 ? -ns : ns) * f.shQ.w, ed, 1e4, f.shQ.w, seed))
               L += T * fe * sampleEnv(ed, 0.0) * fogTr(p, ed, 1e4) * (pE / max(pE * pE + pBm * pBm, 1e-20));
         }
      }
      if (hairM)
      {
         vec3 nd = hairSample(V, hT, hSig, hBm, hBn, hShift, mIor, hH, vec4(rnd(seed), rnd(seed), rnd(seed), rnd(seed)));
         float pdH = 0.0;
         vec3 fH = hairBsdf(V, nd, hT, hSig, hBm, hBn, hShift, mIor, hH, pdH);
         if (pdH <= 1e-6) break;
         T *= min(fH / pdH, vec3(4.0));
         lastPdfB = pdH; skyK = vec3(1.0);
         roulette(b, T, seed, dead); if (dead) break;
         o = p + (dot(ns, nd) < 0.0 ? -ns : ns) * f.shQ.w; d = nd;
         continue;
      }
      if (dm == 1)
      {
         vec2 lq = ptProbs(f0, metal, nDotV);
         if (rnd(seed) < lq.x)
         {
            vec3 nd = reflect(d, ggxSampleH(n, max(0.002, rough * rough), rnd(seed), rnd(seed)));
            float pd = 0.0;
            vec3 fs = ptEval(n, V, nd, base, metal, rough, f0, vec2(0.0, 1.0), msK, pd);
            float ps = pd - lq.y * max(dot(n, nd), 0.0) / 3.14159265;
            if (ps <= 1e-6 || dot(ns, nd) <= 0.0) break;
            T *= fs / ps; lastPdfB = pd; skyK = vec3(1.0);
            o = p + ns * f.shQ.w; d = nd;
         }
         else
         {
            vec3 Fv = ptF(nDotV, f0);
            vec3 Tw = (vec3(1.0) - Fv) * (1.0 - metal) / max(lq.y, 1e-3);
            vec3 sg = 1.0 / max(sssR * SSS_D, vec3(1e-4));
            vec3 A = clamp(base, 0.0, 0.999);
            vec3 qa = 4.09712 + 4.20863 * A - sqrt(9.59217 + 41.6808 * A + 17.7126 * A * A);
            vec3 alb = 1.0 - qa * qa;
            vec3 wp = p - ns * f.shQ.w, wd = cosineDir(-ns, rnd(seed), rnd(seed));
            bool wout = false; vec3 q = p, no = ns;
            for (int s = 0; s < 256 && !wout; s++)
            {
               vec3 cw = max(Tw, vec3(1e-6)) / max(Tw.x + Tw.y + Tw.z, 3e-6);
               float u0 = rnd(seed);
               float sc = u0 < cw.x ? sg.x : (u0 < cw.x + cw.y ? sg.y : sg.z);
               float tt = -log(max(1.0 - rnd(seed), 1e-7)) / sc;
               Hit wh;
               if (trace(wp, wd, tt, false, wh))
               {
                  vec3 tr = exp(-sg * wh.t);
                  Tw *= tr / max(dot(cw, tr), 1e-20);
                  q = wp + wd * wh.t;
                  uvec3 jx = triIx(wh.inst, wh.prim);
                  vec3 jw = vec3(1.0 - wh.bc.x - wh.bc.y, wh.bc.x, wh.bc.y);
                  vec3 jn = vN(jx.x) * jw.x + vN(jx.y) * jw.y + vN(jx.z) * jw.z;
                  vec3 gn = normalize((inst[wh.inst].normalM * vec4(jn, 0.0)).xyz);
                  no = dot(gn, wd) > 0.0 ? gn : -gn;
                  wout = true;
               }
               else
               {
                  vec3 tr = exp(-sg * tt);
                  Tw *= alb * sg * tr / max(dot(cw, sg * tr), 1e-20);
                  wp += wd * tt;
                  float cz = 1.0 - 2.0 * rnd(seed), ph = 6.28318530718 * rnd(seed), sz2 = sqrt(max(1.0 - cz * cz, 0.0));
                  wd = vec3(sz2 * cos(ph), sz2 * sin(ph), cz);
               }
            }
            if (!wout) break;
            T *= min(Tw, vec3(8.0));
            vec3 eld = vec3(0.0, 1.0, 0.0), eLe = vec3(0.0); float etm = 0.0, epL = -1.0; vec2 elt = vec2(1.0);
            ptPickLight(nEm, pEm, count, q, seed, eld, eLe, etm, epL, elt);
            float ec = dot(no, eld);
            if (ec > 1e-5 && anyPos(eLe))
            {
               if (epL > -1.5 && occludedAlpha(q + no * f.shQ.w, eld, etm, f.shQ.w, seed)) eLe *= epL > 0.0 ? 0.0 : 1.0 - f.shP.w;
               float epB = ec / 3.14159265;
               L += T * (epB * elt.x) * eLe * (epL > 0.0 ? epL / max(epL * epL + epB * epB, 1e-20) : 1.0);
            }
            d = cosineDir(no, rnd(seed), rnd(seed)); o = q + no * f.shQ.w;
            lastPdfB = max(dot(no, d), 1e-4) / 3.14159265; skyK = f.ambient.xyz * 3.0;
         }
         roulette(b, T, seed, dead); if (dead) break;
         continue;
      }
      vec2 lp = ptProbs(f0, metal, nDotV);
      float a = max(0.002, rough * rough);
      float pC = ccW > 0.0 ? clamp(ccW * (0.04 + 0.96 * pow(1.0 - nDotV, 5.0)), 0.0, 0.95) : 0.0;
      bool coatB = pC > 0.0 && rnd(seed) < pC;
      vec3 nd;
      if (coatB) { nd = reflect(d, ggxSampleH(n, ccRough * ccRough, rnd(seed), rnd(seed))); skyK = vec3(1.0); }
      else if (rnd(seed) < lp.x) { nd = reflect(d, ggxSampleH(n, a, rnd(seed), rnd(seed))); skyK = vec3(1.0); }
      else { nd = cosineDir(dm == 2 ? -n : n, rnd(seed), rnd(seed)); skyK = f.ambient.xyz * 3.0; }
      float pdfB = 0.0;
      vec3 fB = dm == 2 ? ptEvalT(n, V, nd, base, metal, rough, f0, vec2(1.0), msK, pdfB) : ptEval(n, V, nd, base, metal, rough, f0, vec2(1.0), msK, pdfB);
      float pdfC = 0.0;
      float cnL = dot(n, nd);
      if (pC > 0.0 && cnL > 0.0)
      {
         vec3 hc = normalize(V + nd);
         float cnH = max(dot(n, hc), 0.0), cvH = max(dot(V, hc), 1e-4);
         pdfC = distGGX(cnH, ccRough) * cnH / (4.0 * cvH);
      }
      if (coatB)
      {
         if (pdfC <= 1e-6 || dot(ns, nd) <= 0.0) break;
         vec3 hc = normalize(V + nd);
         float cwt = ccW * coatSpec(max(dot(n, hc), 0.0), nDotV, max(cnL, 1e-4), max(dot(V, hc), 1e-4), ccRough) * cnL / (pC * pdfC);
         T *= min(cwt, 4.0);
         lastPdfB = pC * pdfC + (1.0 - pC) * pdfB;
      }
      else
      {
         if (pdfB <= 1e-6 || (dot(ns, nd) <= 0.0) != (dm == 2 && dot(n, nd) < 0.0)) break;
         if (shW > 0.0)
         {
            float nL = max(dot(n, nd), 0.0);
            fB = fB * shV + sheenC * (sheenDir != 0.0 ? sheenLtc(max(dot(shN, nd), 0.0), shNV, dot(nd, V), sheenR) * max(dot(shN, nd), 0.0)
                                                       : sheenLtc(nL, nDotV, dot(nd, V), sheenR) * nL) * shW;
         }
         T *= fB / pdfB;
         lastPdfB = pC * pdfC + (1.0 - pC) * pdfB;
      }
      roulette(b, T, seed, dead); if (dead) break;
      o = p + (dot(ns, nd) < 0.0 ? -ns : ns) * f.shQ.w; d = nd;
   }
   L = (min(L, vec3(30.0)) + L0) * chan;
   bool add = pc.bg.x > 0.5;
   float lum = dot(L, vec3(0.2126, 0.7152, 0.0722));
   imageStore(accI, px, (add ? imageLoad(accI, px) : vec4(0.0)) + vec4(L, cov));
   imageStore(auxI, px, (add ? imageLoad(auxI, px) : vec4(0.0)) + vec4(alb0 * cov, cov > 0.0 ? lum * lum : 0.0));
   imageStore(gdI, px, (add ? imageLoad(gdI, px) : vec4(0.0)) + vec4(n0 * cov, dist0 * cov));
}
)";

// dn_prep_k: demodulated radiance + variance of the mean, the guides, and the relative-error histogram.
const char* kDnPrep = R"(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
   ivec2 sz = imageSize(accI);
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (p.x >= sz.x || p.y >= sz.y) return;
   vec4 a = imageLoad(accI, p), x = imageLoad(auxI, p), g = imageLoad(gdI, p);
   float hits = max(a.a, 1.0);
   vec3 alb = max(x.rgb / hits, vec3(0.001));
   vec3 dm = a.rgb / hits / alb;
   float l = dot(a.rgb / hits, vec3(0.2126, 0.7152, 0.0722));
   float var = max(x.a / hits - l * l, 0.0) / hits;
   float la = dot(alb, vec3(0.2126, 0.7152, 0.0722));
   float rel = g.w > 0.0 ? sqrt(var) / max(l, 0.05) : 0.0;
   atomicAdd(hist[min(int(rel * 400.0), 63)], 1u);
   imageStore(dnC0, p, vec4(dm, var / max(la * la, 1e-4)));
   imageStore(dnA, p, vec4(alb, 0.0));
   float nl = length(g.xyz);
   imageStore(dnG, p, vec4(nl > 0.0 ? g.xyz / nl : vec3(0.0), g.w / hits));
}
)";

// dn_atrous_k, one step (pc.bg: x step, y sigL, z 0 = dnC0 -> dnC1, 1 = dnC1 -> dnC0).
const char* kDnAtrous = R"(
layout(local_size_x = 8, local_size_y = 8) in;
vec4 rdC(ivec2 q) { return pc.bg.z > 0.5 ? imageLoad(dnC1, q) : imageLoad(dnC0, q); }
void wrC(ivec2 q, vec4 v) { if (pc.bg.z > 0.5) imageStore(dnC0, q, v); else imageStore(dnC1, q, v); }
void main()
{
   ivec2 sz = imageSize(dnG);
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (p.x >= sz.x || p.y >= sz.y) return;
   int stp = int(pc.bg.x + 0.5); float sigL = pc.bg.y;
   vec4 c0 = rdC(p), g0 = imageLoad(dnG, p);
   vec3 a0 = imageLoad(dnA, p).rgb;
   if (g0.w <= 0.0) { wrC(p, c0); return; }
   float hk[3] = float[3](0.375, 0.25, 0.0625);
   float l0 = dot(c0.rgb, vec3(0.2126, 0.7152, 0.0722));
   vec3 sum = vec3(0.0); float vs = 0.0, ws = 0.0;
   for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++)
      {
         ivec2 q = p + ivec2(dx, dy) * stp;
         if (q.x < 0 || q.y < 0 || q.x >= sz.x || q.y >= sz.y) continue;
         vec4 c = rdC(q), g = imageLoad(dnG, q);
         if (g.w <= 0.0) continue;
         float wn = pow(max(dot(g0.xyz, g.xyz), 0.0), 64.0);
         float wz = exp(-abs(g.w - g0.w) / (0.02 * g0.w * float(max(abs(dx), abs(dy)) * stp) + 1e-4));
         float wl = exp(-abs(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)) - l0) / (sigL * sqrt(max(c0.a + c.a, 1e-8)) + 1e-4));
         float wa = exp(-length(imageLoad(dnA, q).rgb - a0) / 0.03);
         float w = hk[abs(dx)] * hk[abs(dy)] * wn * wz * wl * wa;
         sum += c.rgb * w; vs += c.a * w * w; ws += w;
      }
   wrC(p, ws > 0.0 ? vec4(sum / ws, vs / (ws * ws)) : c0);
}
)";

// ptres_fs: mean radiance (or the denoised lighting times the first-hit albedo), exposure, tonemap, and the blend
// with the clear colour in linear light.
const char* kPtRes = R"(
layout(location = 0) out vec4 oC;
void main()
{
   ivec2 px = ivec2(gl_FragCoord.xy);
   vec4 a = imageLoad(accI, px);
   float cov = a.a / pc.bg.x;
   vec3 col = a.rgb / max(a.a, 1e-4);
   if (pc.bg.w > 0.5 && imageLoad(dnG, px).w > 0.0)
      col = (pc.clr.w > 0.5 ? imageLoad(dnC1, px) : imageLoad(dnC0, px)).rgb * max(imageLoad(auxI, px).rgb / max(a.a, 1.0), vec3(0.001));
   col *= pc.bg.z;
   int tm = int(pc.bg.y);
   vec3 cl = toLinear(pc.clr.xyz);
   vec3 bg = toSrgb(toneMap(cl, tm));
   oC = vec4(max(toSrgb(toneMap(cov * col + (1.0 - cov) * cl, tm)) - (1.0 - cov) * bg, vec3(0.0)) + (1.0 - cov) * pc.clr.xyz, 1.0);
}
)";
}

#pragma once

// Settle denoiser (PLAN 1.5): edge-avoiding a-trous wavelet filter (Dammertz et al. 2010) with SVGF's
// variance-guided luminance weight (Schied et al. 2017), written from the papers. MSL only (settle is Metal).
//
// Settle accumulates three sums per pixel (pt_fs):
//   acc : radiance rgb, hit count            aux : first-hit albedo rgb, luminance^2
//   gd  : first-hit normal xyz, distance
// dn_prep_k turns them into demodulated radiance (radiance / albedo) + its variance of the mean, and the guide
// (normal, distance); dn_atrous_k runs once per step 1, 2, 4, 8, 16; ptres_fs then multiplies the albedo back.
// Demodulating keeps texture detail out of the blur: only lighting is filtered.
// dn_prep_k also bins every pixel's relative error of the mean (sqrt(variance) / luminance, 0.0025 per bin, 64
// bins) for the export's adaptive stop: the CPU reads 64 counts, never the image.
#define R3D_DENOISE_MSL \
   "struct DnU { int step; float n; float sigL; float pad; };\n" \
   "kernel void dn_prep_k(uint2 p [[thread_position_in_grid]], texture2d<float, access::read> acc [[texture(0)]],\n" \
   "      texture2d<float, access::read> aux [[texture(1)]], texture2d<float, access::read> gd [[texture(2)]],\n" \
   "      texture2d<float, access::write> col [[texture(3)]], texture2d<float, access::write> gOut [[texture(4)]],\n" \
   "      texture2d<float, access::write> aOut [[texture(5)]], constant DnU& u [[buffer(0)]],\n" \
   "      device atomic_uint* hist [[buffer(1)]]) {\n" \
   "   if (p.x >= acc.get_width() || p.y >= acc.get_height()) return;\n" \
   "   const float4 a = acc.read(p), x = aux.read(p), g = gd.read(p);\n" \
   "   const float hits = max(a.a, 1.0);\n" \
   "   const float3 alb = max(x.rgb / hits, float3(0.001));\n" \
   "   const float3 dm = a.rgb / hits / alb;\n" \
   "   const float l = dot(a.rgb / hits, float3(0.2126, 0.7152, 0.0722));\n" \
   "   const float var = max(x.a / hits - l * l, 0.0) / hits;\n" \
   "   const float la = dot(alb, float3(0.2126, 0.7152, 0.0722));\n" \
   "   const float rel = g.w > 0.0 ? sqrt(var) / max(l, 0.05) : 0.0;\n" \
   "   atomic_fetch_add_explicit(&hist[min(int(rel * 400.0), 63)], 1u, memory_order_relaxed);\n" \
   "   col.write(float4(dm, var / max(la * la, 1e-4)), p);\n" \
   "   aOut.write(float4(alb, 0.0), p);\n" \
   "   const float nl = length(g.xyz);\n" \
   "   gOut.write(float4(nl > 0.0 ? g.xyz / nl : float3(0.0), g.w / hits), p);\n" \
   "}\n" \
   "kernel void dn_atrous_k(uint2 p [[thread_position_in_grid]], texture2d<float, access::read> col [[texture(0)]],\n" \
   "      texture2d<float, access::read> gd [[texture(1)]], texture2d<float, access::write> out [[texture(2)]],\n" \
   "      texture2d<float, access::read> at [[texture(3)]],\n" \
   "      constant DnU& u [[buffer(0)]]) {\n" \
   "   const int W = int(col.get_width()), H = int(col.get_height());\n" \
   "   if (int(p.x) >= W || int(p.y) >= H) return;\n" \
   "   const float4 c0 = col.read(p), g0 = gd.read(p);\n" \
   "   const float3 a0 = at.read(p).rgb;\n" \
   "   if (g0.w <= 0.0) { out.write(c0, p); return; }\n" \
   "   const float h[3] = { 0.375, 0.25, 0.0625 };\n" \
   "   const float l0 = dot(c0.rgb, float3(0.2126, 0.7152, 0.0722));\n" \
   "   float3 sum = float3(0.0); float vs = 0.0, ws = 0.0;\n" \
   "   for (int dy = -2; dy <= 2; dy++)\n" \
   "      for (int dx = -2; dx <= 2; dx++) {\n" \
   "         const int2 q = int2(p) + int2(dx, dy) * u.step;\n" \
   "         if (q.x < 0 || q.y < 0 || q.x >= W || q.y >= H) continue;\n" \
   "         const float4 c = col.read(uint2(q)), g = gd.read(uint2(q));\n" \
   "         if (g.w <= 0.0) continue;\n" \
   "         const float wn = pow(max(dot(g0.xyz, g.xyz), 0.0), 64.0);\n" \
   "         const float wz = exp(-abs(g.w - g0.w) / (0.02 * g0.w * float(max(abs(dx), abs(dy)) * u.step) + 1e-4));\n" \
   "         // both pixels' noise: a one-sided stop lets bright noisy pixels take dark neighbours and not back (light lost)\n" \
   "         const float wl = exp(-abs(dot(c.rgb, float3(0.2126, 0.7152, 0.0722)) - l0) / (u.sigL * sqrt(max(c0.a + c.a, 1e-8)) + 1e-4));\n" \
   "         // albedo stop: specular is not scaled by albedo, so demodulated light still carries the texture\n" \
   "         const float wa = exp(-length(at.read(uint2(q)).rgb - a0) / 0.03);\n" \
   "         const float w = h[abs(dx)] * h[abs(dy)] * wn * wz * wl * wa;\n" \
   "         sum += c.rgb * w; vs += c.a * w * w; ws += w;\n" \
   "      }\n" \
   "   out.write(ws > 0.0 ? float4(sum / ws, vs / (ws * ws)) : c0, p);\n" \
   "}\n"

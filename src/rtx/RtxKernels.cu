// Turbo 0.62: small CUDA kernels the RTX module runs before a trace (built
// to PTX, loaded with the driver API): instance records from PointZ matrix
// columns, and sphere radii from PScale.

#include <optix_types.h>

#include "RtxShared.h"

// Columns M0..M3 (vec4 each, column-major 4x4 per instance, as
// pop::InstanceMatrices writes them) -> OptixInstance (row-major 3x4).
extern "C" __global__ void rtxBuildInstances(OptixInstance* out, const float* c0, const float* c1, const float* c2,
                                              const float* c3, int count, unsigned long long gas, unsigned int sbtOffset)
{
   const int i = blockIdx.x * blockDim.x + threadIdx.x;
   if (i >= count)
      return;
   const float* col[4] = { c0 + i * 4, c1 + i * 4, c2 + i * 4, c3 + i * 4 };
   OptixInstance& o = out[i];
   for (int r = 0; r < 3; r++)
      for (int c = 0; c < 4; c++)
         o.transform[r * 4 + c] = col[c][r];
   o.instanceId = (unsigned int)i;
   o.sbtOffset = sbtOffset;
   o.visibilityMask = 255;
   o.flags = OPTIX_INSTANCE_FLAG_NONE;
   o.traversableHandle = gas;
   o.pad[0] = o.pad[1] = 0;
}

// radius[i] = scale * (pscale ? pscale[i * stride] : 1).
extern "C" __global__ void rtxRadii(float* out, const float* pscale, int stride, float scale, int count)
{
   const int i = blockIdx.x * blockDim.x + threadIdx.x;
   if (i >= count)
      return;
   const float r = pscale != nullptr ? pscale[(size_t)i * stride] : 1.0f;
   out[i] = fmaxf(scale * r, 1e-6f);
}

// Fast denoiser (edge-avoiding a-trous wavelet, as in SVGF without the
// temporal part): the image is divided by the first-hit albedo, blurred in
// growing steps that stop at normal, depth-of-colour and albedo edges, then
// multiplied back - a millisecond or two at 1080p, for moving scenes where
// the AI denoiser's ~20 ms would set the frame rate.
extern "C" __global__ void rtxDemod(const float4* in, const float4* albedo, float4* out, int n)
{
   const int i = blockIdx.x * blockDim.x + threadIdx.x;
   if (i >= n)
      return;
   const float4 c = in[i], a = albedo[i];
   out[i] = make_float4(c.x / fmaxf(a.x, 0.02f), c.y / fmaxf(a.y, 0.02f), c.z / fmaxf(a.z, 0.02f), c.w);
}

extern "C" __global__ void rtxAtrous(const float4* in, float4* out, const float4* albedo, const float4* normal, int w,
                                      int h, int step, float sigmaL)
{
   const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
   if (x >= w || y >= h)
      return;
   const float k[3] = { 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };
   const int i = y * w + x;
   const float4 c0 = in[i], n0 = normal[i], a0 = albedo[i];
   // Turbo 0.70: the background (no first hit) is the environment itself, not
   // noise: left as it is (blurring it smeared the HDRI behind the scene).
   if (n0.x * n0.x + n0.y * n0.y + n0.z * n0.z < 1e-4f)
   {
      out[i] = c0;
      return;
   }
   const float l0 = 0.2126f * c0.x + 0.7152f * c0.y + 0.0722f * c0.z;
   float4 sum = make_float4(0, 0, 0, 0);
   float wsum = 0.0f;
   for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++)
      {
         const int xx = min(max(x + dx * step, 0), w - 1), yy = min(max(y + dy * step, 0), h - 1);
         const int j = yy * w + xx;
         const float4 c = in[j], n = normal[j], a = albedo[j];
         const float l = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
         const float nd = n0.x * n.x + n0.y * n.y + n0.z * n.z;
         float wn = fmaxf(0.0f, nd); // ^32 by squaring (powf was most of the cost)
         wn *= wn; wn *= wn; wn *= wn; wn *= wn; wn *= wn;
         const float wl = __expf(-fabsf(l - l0) / (sigmaL * (1.0f + l0)));
         const float da = fabsf(a.x - a0.x) + fabsf(a.y - a0.y) + fabsf(a.z - a0.z);
         const float wa = __expf(-da * 8.0f);
         const float wt = k[abs(dx)] * k[abs(dy)] * wn * wl * wa;
         sum.x += c.x * wt;
         sum.y += c.y * wt;
         sum.z += c.z * wt;
         wsum += wt;
      }
   const float inv = 1.0f / fmaxf(wsum, 1e-6f);
   out[i] = make_float4(sum.x * inv, sum.y * inv, sum.z * inv, c0.w);
}

// Turbo 0.70 (X1): temporal accumulation of a moving scene (the accumulation
// restarts every frame, a few samples per pixel), SVGF's temporal half. Each
// pixel's history is fetched where its first hit was last frame (motion: the
// camera and each copy's own previous transform), bilinearly, from the taps
// whose surface matches (both background, or distance within 10% and normals
// within ~37 degrees); a disocclusion starts over. The history is clamped to
// the current 3x3 neighbourhood's mean +- 2.5 sigma, for shading that changed
// in place (moving reflections), then blended at 1 / n up to maxN frames.
// `hist` / `histN` are last frame's output, never filtered (the spatial
// filter and the denoisers run on a copy).
extern "C" __global__ void rtxReproject(const float4* cur, const float2* motion, const float4* gbuf,
                                         const float4* prevGbuf, const float4* hist, const float* histN, float4* out,
                                         float* outN, int w, int h, float maxN, int useHist)
{
   const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
   if (x >= w || y >= h)
      return;
   const int i = y * w + x;
   const float4 c = cur[i], g = gbuf[i];
   const float2 m = motion[i];
   float4 prev = make_float4(0, 0, 0, 0);
   float wsum = 0.0f, nsum = 0.0f;
   if (useHist && m.x >= 0.0f && m.y >= 0.0f)
   {
      const float px = m.x - 0.5f, py = m.y - 0.5f;
      const int x0 = (int)floorf(px), y0 = (int)floorf(py);
      const float fx = px - (float)x0, fy = py - (float)y0;
      for (int dy = 0; dy <= 1; dy++)
         for (int dx = 0; dx <= 1; dx++)
         {
            const int xx = x0 + dx, yy = y0 + dy;
            if (xx < 0 || yy < 0 || xx >= w || yy >= h)
               continue;
            const int j = yy * w + xx;
            const float4 pg = prevGbuf[j];
            bool same;
            if (g.w < 0.0f || pg.w < 0.0f)
               same = g.w < 0.0f && pg.w < 0.0f;
            else
               same = fabsf(pg.w - g.w) < 0.1f * g.w + 1e-3f && (pg.x * g.x + pg.y * g.y + pg.z * g.z) > 0.8f;
            if (!same)
               continue;
            const float wt = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
            const float4 hc = hist[j];
            prev.x += hc.x * wt;
            prev.y += hc.y * wt;
            prev.z += hc.z * wt;
            prev.w += hc.w * wt;
            nsum += histN[j] * wt;
            wsum += wt;
         }
   }
   float n = 0.0f;
   if (wsum > 0.05f)
   {
      const float inv = 1.0f / wsum;
      prev = make_float4(prev.x * inv, prev.y * inv, prev.z * inv, prev.w * inv);
      n = nsum * inv;
      float4 mean = make_float4(0, 0, 0, 0), sq = make_float4(0, 0, 0, 0);
      for (int dy = -1; dy <= 1; dy++)
         for (int dx = -1; dx <= 1; dx++)
         {
            const int xx = min(max(x + dx, 0), w - 1), yy = min(max(y + dy, 0), h - 1);
            const float4 s = cur[yy * w + xx];
            mean.x += s.x; mean.y += s.y; mean.z += s.z; mean.w += s.w;
            sq.x += s.x * s.x; sq.y += s.y * s.y; sq.z += s.z * s.z; sq.w += s.w * s.w;
         }
      const float k = 1.0f / 9.0f;
      const float mv[4] = { mean.x * k, mean.y * k, mean.z * k, mean.w * k };
      const float sv[4] = { sq.x * k, sq.y * k, sq.z * k, sq.w * k };
      float pv[4] = { prev.x, prev.y, prev.z, prev.w };
      for (int q = 0; q < 4; q++)
      {
         const float sigma = sqrtf(fmaxf(sv[q] - mv[q] * mv[q], 0.0f));
         pv[q] = fminf(fmaxf(pv[q], mv[q] - 2.5f * sigma), mv[q] + 2.5f * sigma);
      }
      prev = make_float4(pv[0], pv[1], pv[2], pv[3]);
   }
   n = fminf(n + 1.0f, maxN);
   const float a = 1.0f / n;
   out[i] = make_float4(prev.x + (c.x - prev.x) * a, prev.y + (c.y - prev.y) * a, prev.z + (c.z - prev.z) * a,
                        prev.w + (c.w - prev.w) * a);
   outN[i] = n;
}

// Turbo 0.70 (X1): this frame's place of every particle of one geometry, by
// PartId, for next frame's motion vectors (RtxDevice.cu previousPoint): an
// instance's transform, or a sphere's centre through the geometry's model
// (row-major 3x4, `model`). Run after the trace that read last frame's ring.
struct RtxModel34
{
   float m[12];
};
extern "C" __global__ void rtxRingStore(RtxPrevSlot* ring, unsigned int mask, const OptixInstance* inst,
                                         const float* centres, int centreStride, RtxModel34 model, const float* partId,
                                         int partIdStride, int count, unsigned int key)
{
   const int i = blockIdx.x * blockDim.x + threadIdx.x;
   if (i >= count)
      return;
   const unsigned int id = (unsigned int)fmaxf(partId[(size_t)i * partIdStride] + 0.5f, 0.0f) + 1u;
   RtxPrevSlot s;
   if (inst != nullptr)
      for (int k = 0; k < 12; k++)
         s.t[k] = inst[i].transform[k];
   else
   {
      const float* c = centres + (size_t)i * centreStride;
      const float* m = model.m;
      for (int k = 0; k < 12; k++)
         s.t[k] = (k % 5 == 0) ? 1.0f : 0.0f; // identity rotation
      s.t[3] = m[0] * c[0] + m[1] * c[1] + m[2] * c[2] + m[3];
      s.t[7] = m[4] * c[0] + m[5] * c[1] + m[6] * c[2] + m[7];
      s.t[11] = m[8] * c[0] + m[9] * c[1] + m[10] * c[2] + m[11];
   }
   s.id = id;
   s.key = key;
   s.pad[0] = s.pad[1] = 0;
   ring[RtxRingSlot(id, key, mask)] = s;
}

// Turbo 0.70: OptiX's temporal denoisers want the flow from last frame to this
// one, in pixels (this pixel came from pixel - flow); no motion = none.
extern "C" __global__ void rtxFlow(const float2* motion, float2* flow, int w, int h)
{
   const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
   if (x >= w || y >= h)
      return;
   const int i = y * w + x;
   const float2 m = motion[i];
   flow[i] = (m.x >= 0.0f && m.y >= 0.0f) ? make_float2((float)x + 0.5f - m.x, (float)y + 0.5f - m.y)
                                          : make_float2(0.0f, 0.0f);
}

// Turbo 0.70: motion blur - each output pixel averaged along its own motion
// since last frame (motion at the input size, `scale` times smaller than the
// output), `shutter` frames long and centred on the pixel. A pixel with no
// motion (a cut, or nothing under it last frame) stays sharp.
extern "C" __global__ void rtxMotionBlur(const float4* in, float4* out, const float2* motion, int iw, int ih,
                                          int scale, float shutter)
{
   const int ow = iw * scale, oh = ih * scale;
   const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
   if (x >= ow || y >= oh)
      return;
   const int ix = min(x / scale, iw - 1), iy = min(y / scale, ih - 1);
   const float2 m = motion[iy * iw + ix];
   const int i = y * ow + x;
   if (m.x < 0.0f || m.y < 0.0f)
   {
      out[i] = in[i];
      return;
   }
   float vx = ((float)ix + 0.5f - m.x) * (float)scale * shutter;
   float vy = ((float)iy + 0.5f - m.y) * (float)scale * shutter;
   const float len = sqrtf(vx * vx + vy * vy);
   const float kMaxLen = 96.0f;
   if (len > kMaxLen)
   {
      vx *= kMaxLen / len;
      vy *= kMaxLen / len;
   }
   const int taps = min(max((int)ceilf(fminf(len, kMaxLen)), 1), 24);
   if (taps <= 1)
   {
      out[i] = in[i];
      return;
   }
   float4 sum = make_float4(0, 0, 0, 0);
   for (int k = 0; k < taps; k++)
   {
      const float t = ((float)k + 0.5f) / (float)taps - 0.5f;
      const int sx = min(max((int)((float)x + vx * t + 0.5f), 0), ow - 1);
      const int sy = min(max((int)((float)y + vy * t + 0.5f), 0), oh - 1);
      const float4 s = in[sy * ow + sx];
      sum.x += s.x; sum.y += s.y; sum.z += s.z; sum.w += s.w;
   }
   const float inv = 1.0f / (float)taps;
   out[i] = make_float4(sum.x * inv, sum.y * inv, sum.z * inv, sum.w * inv);
}

// Turbo 0.70: a bilinear 2x copy - the output of a frame OptiX's 2x model
// could not denoise.
extern "C" __global__ void rtxUpscale2x(const float4* in, float4* out, int w, int h)
{
   const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
   if (x >= 2 * w || y >= 2 * h)
      return;
   const float sx = fminf(fmaxf(((float)x + 0.5f) * 0.5f - 0.5f, 0.0f), (float)(w - 1));
   const float sy = fminf(fmaxf(((float)y + 0.5f) * 0.5f - 0.5f, 0.0f), (float)(h - 1));
   const int x0 = (int)sx, y0 = (int)sy, x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
   const float fx = sx - (float)x0, fy = sy - (float)y0;
   const float4 a = in[y0 * w + x0], b = in[y0 * w + x1], c = in[y1 * w + x0], d = in[y1 * w + x1];
   const float w00 = (1.0f - fx) * (1.0f - fy), w10 = fx * (1.0f - fy), w01 = (1.0f - fx) * fy, w11 = fx * fy;
   out[y * 2 * w + x] = make_float4(a.x * w00 + b.x * w10 + c.x * w01 + d.x * w11, a.y * w00 + b.y * w10 + c.y * w01 + d.y * w11,
                                    a.z * w00 + b.z * w10 + c.z * w01 + d.z * w11, a.w * w00 + b.w * w10 + c.w * w01 + d.w * w11);
}

extern "C" __global__ void rtxRemod(const float4* in, const float4* albedo, float4* out, int n)
{
   const int i = blockIdx.x * blockDim.x + threadIdx.x;
   if (i >= n)
      return;
   const float4 c = in[i], a = albedo[i];
   out[i] = make_float4(c.x * fmaxf(a.x, 0.02f), c.y * fmaxf(a.y, 0.02f), c.z * fmaxf(a.z, 0.02f), c.w);
}

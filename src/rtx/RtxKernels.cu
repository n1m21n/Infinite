// Turbo 0.62: small CUDA kernels the RTX module runs before a trace (built
// to PTX, loaded with the driver API): instance records from PointZ matrix
// columns, and sphere radii from PScale.

#include <optix_types.h>

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
         const float wn = powf(fmaxf(0.0f, nd), 32.0f);
         const float wl = __expf(-fabsf(l - l0) / (sigmaL * (1.0f + l0)));
         const float da = fabsf(a.x - a0.x) + fabsf(a.y - a0.y) + fabsf(a.z - a0.z);
         const float wa = __expf(-da * 8.0f);
         const float wc = (n0.x * n0.x + n0.y * n0.y + n0.z * n0.z) < 1e-4f ? 1.0f : wn; // background: plain blur
         const float wt = k[abs(dx)] * k[abs(dy)] * wc * wl * wa;
         sum.x += c.x * wt;
         sum.y += c.y * wt;
         sum.z += c.z * wt;
         wsum += wt;
      }
   const float inv = 1.0f / fmaxf(wsum, 1e-6f);
   out[i] = make_float4(sum.x * inv, sum.y * inv, sum.z * inv, c0.w);
}

extern "C" __global__ void rtxRemod(const float4* in, const float4* albedo, float4* out, int n)
{
   const int i = blockIdx.x * blockDim.x + threadIdx.x;
   if (i >= n)
      return;
   const float4 c = in[i], a = albedo[i];
   out[i] = make_float4(c.x * fmaxf(a.x, 0.02f), c.y * fmaxf(a.y, 0.02f), c.z * fmaxf(a.z, 0.02f), c.w);
}

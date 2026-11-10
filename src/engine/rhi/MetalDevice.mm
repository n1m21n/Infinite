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
using namespace metal;
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
float3 aces(float3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
float3 toSrgb(float3 c) { c = max(c, float3(0.0)); return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c < float3(0.0031308)); }

fragment float4 r_fs(VOut in [[stage_in]], constant FrameU& f [[buffer(1)]], constant InstU& m [[buffer(2)]],
                     depth2d<float> sm [[texture(0)]])
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
      const float shadow = (f.shP.x > 0.5 && i == int(f.shP.y)) ? shadowFactor(f, sm, in.wp, n, lightDir) : 1.0;
      col += (kD * base / 3.14159265 * L.terms.x + spec * L.terms.y) * radiance * nDotL * shadow;
   }
   col += base / 3.14159265 * floorBounce * (0.25 * (1.0 - n.y)) * (1.0 - metal);
   const float3 reflectDir = reflect(-viewDir, n);
   const float3 irradiance = sampleEnv(f, n, 1.0) * f.ambient.xyz * 3.0;
   const float3 reflection = sampleEnv(f, reflectDir, rough);
   const float3 fAmb = fresnelRough(nDotV, f0, rough);
   col += (float3(1.0) - fAmb) * (1.0 - metal) * base * irradiance;
   col += reflection * fAmb;
   col += base * pow(1.0 - nDotV, 3.0) * f.eye.w;
   col *= f.params.x;
   if (int(f.params.y) == 1) col = aces(col);
   return float4(toSrgb(col), 1.0);
}

struct SOut { float4 pos [[position]]; };
vertex SOut sh_vs(uint vid [[vertex_id]], const device float* v [[buffer(0)]], constant float4x4& lvp [[buffer(1)]], constant InstU& m [[buffer(2)]])
{
   const float3 p = float3(v[vid * 6 + 0], v[vid * 6 + 1], v[vid * 6 + 2]);
   float4 c = lvp * (m.model * float4(p, 1.0));
   c.z = (c.z + c.w) * 0.5;
   SOut o; o.pos = c; return o;
}

struct SkyOut { float4 pos [[position]]; };
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
      float viewProj[16], eye[4], ambient[4], sky[4], horizon[4], ground[4], params[4], lightVP[16], shP[4], shQ[4];
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
         mLib = [dev newLibraryWithSource:kRasterShader options:nil error:&err];
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
         return true;
      }
      bool Render(SharedImage& target, const engine::Frame& fr, double* gpuMs) override
      {
         MetalImage& img = static_cast<MetalImage&>(target);
         const MTLPixelFormat fmt = img.Half() ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
         if (!Pipeline(fmt) || !Depth(img.Width(), img.Height()))
            return false;

         FrameU f = {};
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

         id<MTLCommandBuffer> cb = [mQueue commandBuffer];
         if (!ShadowPass(cb, fr, f))
            EnsureShadowTex(4);
         MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
         pass.colorAttachments[0].texture = img.Tex();
         pass.colorAttachments[0].loadAction = MTLLoadActionClear;
         pass.colorAttachments[0].storeAction = MTLStoreActionStore;
         pass.colorAttachments[0].clearColor = MTLClearColorMake(fr.clearSrgb[0], fr.clearSrgb[1], fr.clearSrgb[2], 1.0);
         pass.depthAttachment.texture = mDepth;
         pass.depthAttachment.loadAction = MTLLoadActionClear;
         pass.depthAttachment.storeAction = MTLStoreActionDontCare;
         pass.depthAttachment.clearDepth = 1.0;
         id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
         [enc setFragmentTexture:mShadowTex atIndex:0];
         [enc setRenderPipelineState:mPso];
         [enc setDepthStencilState:mDs];
         [enc setCullMode:MTLCullModeBack];
         [enc setFrontFacingWinding:MTLWindingClockwise]; // GL counter-clockwise, mirrored by the y flip
         [enc setVertexBytes:&f length:sizeof(f) atIndex:1];
         [enc setFragmentBytes:&f length:sizeof(f) atIndex:1];
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
         [cb commit];
         [cb waitUntilCompleted];
         if (gpuMs)
            *gpuMs = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
         return cb.status == MTLCommandBufferStatusCompleted;
      }

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
         pd.fragmentFunction = [mLib newFunctionWithName:@"r_fs"];
         pd.colorAttachments[0].pixelFormat = fmt;
         pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         NSError* err = nil;
         mPso = [mDev newRenderPipelineStateWithDescriptor:pd error:&err];
         MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
         dd.depthCompareFunction = MTLCompareFunctionLess;
         dd.depthWriteEnabled = YES;
         mDs = [mDev newDepthStencilStateWithDescriptor:dd];
         mFmt = fmt;
         MTLRenderPipelineDescriptor* sd = [MTLRenderPipelineDescriptor new];
         sd.vertexFunction = [mLib newFunctionWithName:@"sh_vs"];
         sd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
         mShPso = [mDev newRenderPipelineStateWithDescriptor:sd error:&err];
         return mPso != nil && mDs != nil && mShPso != nil;
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
      id<MTLRenderPipelineState> mShPso = nil;
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

// Metal backend of the engine RHI. See Rhi.h.
#include "engine/rhi/Rhi.h"

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

   class MetalImage final : public SharedImage
   {
   public:
      MetalImage(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLRenderPipelineState> pso, int w, int h, Format fmt)
          : mQueue(q), mPso(pso), mW(w), mH(h)
      {
         const bool half = fmt == Format::RGBA16F;
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

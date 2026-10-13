#include "VideoTexture.h"

#include "core/GLUtil.h"
#include "core/RuntimeLog.h"
#include "platform/OpenGLHeaders.h"

#include <cstring>

namespace
{
   using Platform::VideoPixelFormat;

   enum Mode
   {
      kModeRgba = 0,     // sample plane 0 as RGBA (BGRA/RGBA uploads, BC1/BC3/BC7)
      kModeBgr24 = 1,    // plane 0 is an R8 texture 3*w wide holding packed B,G,R
      kModeNv12 = 2,     // plane 0 Y (R8), plane 1 UV (RG8)
      kModeYuv420 = 3,   // planes 0/1/2 Y/U/V (R8)
      kModeYCoCg = 4,    // plane 0 HAP Q scaled YCoCg DXT5
      kModeYCoCgA = 5    // + plane 1 BC4 alpha
   };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uP0;\n"
      "uniform sampler2D uP1;\n"
      "uniform sampler2D uP2;\n"
      "uniform int uMode;\n"
      "uniform ivec2 uSize;\n"     // picture size
      "uniform vec2 uUvScale;\n"   // picture / allocated (BC padding)
      "uniform bool uTopDown;\n"
      "uniform bool uMirror;\n"
      "uniform bool uOpaque;\n"
      "uniform bool u709;\n"
      "uniform bool uFull;\n"
      "vec3 yuv2rgb(float y, float u, float v) {\n"
      "   if (uFull) { u -= 0.5; v -= 0.5; }\n"
      "   else { y = (y - 16.0/255.0) * (255.0/219.0); u = (u - 128.0/255.0) * (255.0/224.0); v = (v - 128.0/255.0) * (255.0/224.0); }\n"
      "   if (u709) return vec3(y + 1.5748*v, y - 0.1873*u - 0.4681*v, y + 1.8556*u);\n"
      "   return vec3(y + 1.402*v, y - 0.344136*u - 0.714136*v, y + 1.772*u);\n"
      "}\n"
      "void main() {\n"
      "   vec2 uv = vUv;\n"
      "   if (uMirror) uv.x = 1.0 - uv.x;\n"
      "   if (uTopDown) uv.y = 1.0 - uv.y;\n"
      "   vec4 c;\n"
      "   if (uMode == 1) {\n"
      "      int x = int(uv.x * float(uSize.x)); x = clamp(x, 0, uSize.x - 1);\n"
      "      int y = int(uv.y * float(uSize.y)); y = clamp(y, 0, uSize.y - 1);\n"
      "      float b = texelFetch(uP0, ivec2(x*3, y), 0).r;\n"
      "      float g = texelFetch(uP0, ivec2(x*3+1, y), 0).r;\n"
      "      float r = texelFetch(uP0, ivec2(x*3+2, y), 0).r;\n"
      "      c = vec4(r, g, b, 1.0);\n"
      "   } else if (uMode == 2) {\n"
      "      vec2 cuv = texture(uP1, uv).rg;\n"
      "      c = vec4(yuv2rgb(texture(uP0, uv).r, cuv.r, cuv.g), 1.0);\n"
      "   } else if (uMode == 3) {\n"
      "      c = vec4(yuv2rgb(texture(uP0, uv).r, texture(uP1, uv).r, texture(uP2, uv).r), 1.0);\n"
      "   } else if (uMode == 4 || uMode == 5) {\n"
      "      vec4 t = texture(uP0, uv * uUvScale);\n"
      "      float s = t.b * (255.0/8.0) + 1.0;\n"
      "      float co = (t.r - 0.5*256.0/255.0) / s;\n"
      "      float cg = (t.g - 0.5*256.0/255.0) / s;\n"
      "      c = vec4(t.a + co - cg, t.a + cg, t.a - co - cg, uMode == 5 ? texture(uP1, uv * uUvScale).r : 1.0);\n"
      "   } else {\n"
      "      c = texture(uP0, uv * uUvScale);\n"
      "   }\n"
      "   if (uOpaque) c.a = 1.0;\n"
      "   fragColor = vec4(clamp(c.rgb, 0.0, 1.0), c.a);\n"
      "}\n";

   unsigned int Program()
   {
      static unsigned int sProgram = 0;
      static bool sTried = false;
      if (!sTried)
      {
         sTried = true;
         std::string err;
         sProgram = GLUtil::CompileProgram(kFragSrc, &err);
         if (sProgram == 0)
            RuntimeLog::Write("VideoTexture shader failed: %s", err.c_str());
      }
      return sProgram;
   }

   bool HasBptc()
   {
      static const bool sHas = GLEW_ARB_texture_compression_bptc || GLEW_VERSION_4_2;
      return sHas;
   }

   int Pad4(int v) { return (v + 3) & ~3; }
   size_t BcBytes(int w, int h, size_t blockBytes) { return (size_t)(Pad4(w) / 4) * (size_t)(Pad4(h) / 4) * blockBytes; }
}

VideoTexture::~VideoTexture()
{
   Release();
}

void VideoTexture::Release()
{
   for (Plane& p : mPlanes)
   {
      if (p.tex != 0)
         glDeleteTextures(1, &p.tex);
      if (p.pbo[0] != 0)
         glDeleteBuffers(2, p.pbo);
      p = Plane();
   }
   if (mFbo != 0)
      glDeleteFramebuffers(1, &mFbo);
   mFbo = 0;
}

bool VideoTexture::UploadPlane(Plane& p, int w, int h, unsigned int internalFormat, unsigned int format,
                               unsigned int type, bool compressed, const unsigned char* data, size_t bytes,
                               int alignment, bool linear)
{
   if (data == nullptr || bytes == 0 || w <= 0 || h <= 0)
      return false;
   if (p.tex == 0)
      glGenTextures(1, &p.tex);
   if (p.pbo[0] == 0)
      glGenBuffers(2, p.pbo);

   glBindTexture(GL_TEXTURE_2D, p.tex);
   if (p.w != w || p.h != h || p.internalFormat != internalFormat)
   {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      if (compressed)
         glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, (GLsizei)bytes, nullptr);
      else
         glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
      p.w = w;
      p.h = h;
      p.internalFormat = internalFormat;
   }

   // Orphan-and-fill: the driver hands back fresh storage while the GPU may
   // still be reading last frame's, and the copy into the texture runs as an
   // asynchronous DMA instead of stalling this thread.
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER, p.pbo[p.pboIndex]);
   glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_DRAW);
   void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, (GLsizeiptr)bytes,
                                GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
   if (dst != nullptr)
   {
      std::memcpy(dst, data, bytes);
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
   }
   else
      glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, (GLsizeiptr)bytes, data);

   glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
   if (compressed)
      glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, internalFormat, (GLsizei)bytes, nullptr);
   else
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, nullptr);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   p.pboIndex ^= 1;
   return true;
}

bool VideoTexture::Upload(const Platform::VideoFrame& f, unsigned int& dstTex, int& dstW, int& dstH)
{
   const int w = f.width;
   const int h = f.height;
   if (f.empty() || w <= 0 || h <= 0)
      return false;
   const unsigned int program = Program();
   if (program == 0)
      return false;

   const size_t px = (size_t)w * (size_t)h;
   int mode = kModeRgba;
   int planes = 1;
   float uvScale[2] = { 1.0f, 1.0f };
   bool opaque = f.opaque;
   bool ok = false;
   auto bcScale = [&]() { uvScale[0] = (float)w / (float)Pad4(w); uvScale[1] = (float)h / (float)Pad4(h); };

   switch (f.format)
   {
      case VideoPixelFormat::BGRA8:
      case VideoPixelFormat::RGBA8:
         if (f.plane[0].size() < px * 4) return false;
         ok = UploadPlane(mPlanes[0], w, h, GL_RGBA8,
                          f.format == VideoPixelFormat::BGRA8 ? GL_BGRA : GL_RGBA,
                          f.format == VideoPixelFormat::BGRA8 ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_BYTE,
                          false, f.plane[0].data(), px * 4, 4, true);
         break;
      case VideoPixelFormat::BGR8:
         if (f.plane[0].size() < px * 3) return false;
         mode = kModeBgr24;
         ok = UploadPlane(mPlanes[0], w * 3, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false,
                          f.plane[0].data(), px * 3, 1, false);
         break;
      case VideoPixelFormat::NV12:
      {
         const int cw = (w + 1) / 2, ch = (h + 1) / 2;
         if (f.plane[0].size() < px || f.plane[1].size() < (size_t)cw * ch * 2) return false;
         mode = kModeNv12;
         planes = 2;
         ok = UploadPlane(mPlanes[0], w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, f.plane[0].data(), px, 1, true) &&
              UploadPlane(mPlanes[1], cw, ch, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false, f.plane[1].data(), (size_t)cw * ch * 2, 1, true);
         break;
      }
      case VideoPixelFormat::YUV420P:
      {
         const int cw = (w + 1) / 2, ch = (h + 1) / 2;
         const size_t cpx = (size_t)cw * ch;
         if (f.plane[0].size() < px || f.plane[1].size() < cpx || f.plane[2].size() < cpx) return false;
         mode = kModeYuv420;
         planes = 3;
         ok = UploadPlane(mPlanes[0], w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, f.plane[0].data(), px, 1, true) &&
              UploadPlane(mPlanes[1], cw, ch, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, f.plane[1].data(), cpx, 1, true) &&
              UploadPlane(mPlanes[2], cw, ch, GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, f.plane[2].data(), cpx, 1, true);
         break;
      }
      case VideoPixelFormat::BC1:
      {
         const size_t bytes = BcBytes(w, h, 8);
         if (f.plane[0].size() < bytes) return false;
         bcScale();
         opaque = true;
         ok = UploadPlane(mPlanes[0], Pad4(w), Pad4(h), GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 0, 0, true,
                          f.plane[0].data(), bytes, 4, true);
         break;
      }
      case VideoPixelFormat::BC3:
      case VideoPixelFormat::BC3YCoCg:
      case VideoPixelFormat::BC3YCoCgAlpha:
      {
         const size_t bytes = BcBytes(w, h, 16);
         if (f.plane[0].size() < bytes) return false;
         bcScale();
         ok = UploadPlane(mPlanes[0], Pad4(w), Pad4(h), GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, true,
                          f.plane[0].data(), bytes, 4, true);
         if (f.format == VideoPixelFormat::BC3YCoCg)
            mode = kModeYCoCg;
         else if (f.format == VideoPixelFormat::BC3YCoCgAlpha)
         {
            const size_t abytes = BcBytes(w, h, 8);
            if (f.plane[1].size() < abytes) return false;
            mode = kModeYCoCgA;
            planes = 2;
            ok = ok && UploadPlane(mPlanes[1], Pad4(w), Pad4(h), GL_COMPRESSED_RED_RGTC1, 0, 0, true,
                                   f.plane[1].data(), abytes, 4, true);
         }
         break;
      }
      case VideoPixelFormat::BC7:
      {
         const size_t bytes = BcBytes(w, h, 16);
         if (!HasBptc() || f.plane[0].size() < bytes) return false;
         bcScale();
         ok = UploadPlane(mPlanes[0], Pad4(w), Pad4(h), GL_COMPRESSED_RGBA_BPTC_UNORM_ARB, 0, 0, true,
                          f.plane[0].data(), bytes, 4, true);
         break;
      }
      default:
         return false;
   }
   if (!ok)
      return false;

   // Destination: the caller's texture, (re)allocated at the picture size.
   if (dstTex == 0)
      glGenTextures(1, &dstTex);
   if (dstW != w || dstH != h)
   {
      glBindTexture(GL_TEXTURE_2D, dstTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      dstW = w;
      dstH = h;
   }
   // Attached every time: the caller may have recreated its texture under
   // the same name, and re-attaching is cheaper than chasing that.
   {
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      if (mFbo == 0)
         glGenFramebuffers(1, &mFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   }

   GLUtil::Fbo target;
   target.fbo = mFbo;
   target.tex = dstTex;
   target.w = w;
   target.h = h;
   GLUtil::RunShaderPass(target, program, [&]()
   {
      for (int i = 0; i < planes; ++i)
      {
         glActiveTexture(GL_TEXTURE0 + i);
         glBindTexture(GL_TEXTURE_2D, mPlanes[i].tex);
      }
      glUniform1i(glGetUniformLocation(program, "uP0"), 0);
      glUniform1i(glGetUniformLocation(program, "uP1"), 1);
      glUniform1i(glGetUniformLocation(program, "uP2"), 2);
      glUniform1i(glGetUniformLocation(program, "uMode"), mode);
      glUniform2i(glGetUniformLocation(program, "uSize"), w, h);
      glUniform2f(glGetUniformLocation(program, "uUvScale"), uvScale[0], uvScale[1]);
      glUniform1i(glGetUniformLocation(program, "uTopDown"), f.topDown ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "uMirror"), f.mirrorX ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "uOpaque"), opaque ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "u709"), f.yuv709 ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "uFull"), f.fullRange ? 1 : 0);
   });
   for (int i = planes - 1; i >= 0; --i)
   {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   return true;
}

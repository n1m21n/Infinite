#include "OpticalFlowNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
   const char* kLumaFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() {\n"
      "   vec4 c = texture(uSrc, vUv);\n"
      "   float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
      "   fragColor = vec4(l * 16.0, 0.0, 0.0, 1.0); // x16 keeps the squared gradients inside half-float range\n"
      "}\n";

   // One level down: bilinear at the centre of each 2x2 block is the block mean.
   const char* kDownFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   // Gradient products of the previous frame (central differences), per pixel.
   const char* kGradFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uPrev;\n"
      "uniform vec2 uTexel;\n"
      "void main() {\n"
      "   float ix = 0.5 * (texture(uPrev, vUv + vec2(uTexel.x, 0.0)).r - texture(uPrev, vUv - vec2(uTexel.x, 0.0)).r);\n"
      "   float iy = 0.5 * (texture(uPrev, vUv + vec2(0.0, uTexel.y)).r - texture(uPrev, vUv - vec2(0.0, uTexel.y)).r);\n"
      "   fragColor = vec4(ix * ix, ix * iy, iy * iy, 0.0);\n"
      "}\n";

   // Sum over a (2R+1) window along one axis, edges clamped.
   const char* kBoxFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform ivec2 uStep;\n"
      "uniform int uR;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uSrc, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   vec4 s = vec4(0.0);\n"
      "   for (int i = -uR; i <= uR; i++)\n"
      "      s += texelFetch(uSrc, clamp(p + uStep * i, ivec2(0), sz - 1), 0);\n"
      "   fragColor = s;\n"
      "}\n";

   // Image mismatch under the current flow guess, times the previous frame's gradient.
   // The guess is the coarser level's flow (bilinear, scaled by the size ratio) on a
   // level's first pass, this level's own flow after that, and zero at the top level.
   const char* kResidFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uPrev;\n"
      "uniform sampler2D uCur;\n"
      "uniform sampler2D uGuess;\n"
      "uniform vec2 uGuessScale;\n"
      "uniform int uUseGuess;\n"
      "uniform vec2 uTexel;\n"
      "void main() {\n"
      "   vec2 g = uUseGuess != 0 ? texture(uGuess, vUv).rg * uGuessScale : vec2(0.0);\n"
      "   float i0 = texture(uPrev, vUv).r;\n"
      "   float ix = 0.5 * (texture(uPrev, vUv + vec2(uTexel.x, 0.0)).r - texture(uPrev, vUv - vec2(uTexel.x, 0.0)).r);\n"
      "   float iy = 0.5 * (texture(uPrev, vUv + vec2(0.0, uTexel.y)).r - texture(uPrev, vUv - vec2(0.0, uTexel.y)).r);\n"
      "   float it = texture(uCur, vUv + g * uTexel).r - i0;\n"
      "   fragColor = vec4(ix * it, iy * it, 0.0, 0.0);\n"
      "}\n";

   const char* kSolveFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uTensor;\n"
      "uniform sampler2D uB;\n"
      "uniform sampler2D uGuess;\n"
      "uniform vec2 uGuessScale;\n"
      "uniform int uUseGuess;\n"
      "uniform float uEps;\n"
      "void main() {\n"
      "   vec2 g = uUseGuess != 0 ? texture(uGuess, vUv).rg * uGuessScale : vec2(0.0);\n"
      "   vec3 t = texture(uTensor, vUv).rgb;\n"
      "   vec2 b = texture(uB, vUv).rg;\n"
      "   float a = t.x + uEps, c = t.z + uEps, m = t.y;\n"
      "   float det = a * c - m * m;\n"
      "   vec2 d = vec2(0.0);\n"
      "   if (det > 1e-12)\n"
      "      d = -vec2(c * b.x - m * b.y, a * b.y - m * b.x) / det;\n"
      "   float len = length(d);\n"
      "   if (len > 1.5) d *= 1.5 / len;\n"
      "   fragColor = vec4(g + d, 0.0, 1.0);\n"
      "}\n";

   const char* kOutFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uFlow;\n"
      "uniform float uPix;\n"
      "uniform int uView;\n"
      "vec3 hsv(float h, float s, float v) {\n"
      "   vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
      "   return v * mix(vec3(1.0), k, s);\n"
      "}\n"
      "void main() {\n"
      "   vec2 f = texture(uFlow, vUv).rg * uPix;\n"
      "   float sp = length(f);\n"
      "   if (uView == 0) { fragColor = vec4(f, sp, 1.0); return; }\n"
      "   float ang = atan(f.y, f.x) / 6.2831853;\n"
      "   fragColor = vec4(hsv(ang, 1.0, 1.0 - exp(-sp * 0.5)), 1.0);\n"
      "}\n";

   const char* kZeroFrag =
      "#version 150\n"
      "out vec4 fragColor;\n"
      "void main() { fragColor = vec4(0.0, 0.0, 0.0, 1.0); }\n";

   void Bind(unsigned int prog, int unit, unsigned int tex, const char* name)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(prog, name), unit);
   }
}

OpticalFlowNode::~OpticalFlowNode()
{
   for (int l = 0; l < kMaxLevels; l++)
   {
      GLUtil::DestroyFbo(mLuma[0][l]);
      GLUtil::DestroyFbo(mLuma[1][l]);
      GLUtil::DestroyFbo(mTensor[l]);
      GLUtil::DestroyFbo(mWorkX[l]);
      GLUtil::DestroyFbo(mWorkY[l]);
      GLUtil::DestroyFbo(mFlow[l][0]);
      GLUtil::DestroyFbo(mFlow[l][1]);
   }
   GLUtil::DestroyFbo(mOut);
   for (unsigned int p : { mProgLuma, mProgDown, mProgGrad, mProgBox, mProgResid, mProgSolve, mProgOut, mProgZero })
      if (p != 0)
         glDeleteProgram(p);
}

bool OpticalFlowNode::EnsureShaders()
{
   if (mShadersTried)
      return mProgOut != 0;
   mShadersTried = true;
   mProgLuma = GLUtil::CompileProgram(kLumaFrag);
   mProgDown = GLUtil::CompileProgram(kDownFrag);
   mProgGrad = GLUtil::CompileProgram(kGradFrag);
   mProgBox = GLUtil::CompileProgram(kBoxFrag);
   mProgResid = GLUtil::CompileProgram(kResidFrag);
   mProgSolve = GLUtil::CompileProgram(kSolveFrag);
   mProgOut = GLUtil::CompileProgram(kOutFrag);
   mProgZero = GLUtil::CompileProgram(kZeroFrag);
   if (!(mProgLuma && mProgDown && mProgGrad && mProgBox && mProgResid && mProgSolve && mProgOut && mProgZero))
   {
      mProgOut = 0;
      return false;
   }
   return true;
}

void OpticalFlowNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0 || !EnsureShaders())
      return;

   const int inW = std::max(1, mInput.Width());
   const int inH = std::max(1, mInput.Height());
   const int div = resolution <= 0 ? 1 : (resolution == 1 ? 2 : 4);
   const int workW = std::max(16, inW / div);
   const int workH = std::max(16, inH / div);
   int want = std::clamp(levels, 1, kMaxLevels);
   int lw[kMaxLevels], lh[kMaxLevels];
   lw[0] = workW;
   lh[0] = workH;
   int used = 1;
   while (used < want && std::min(lw[used - 1], lh[used - 1]) >= 32)
   {
      lw[used] = std::max(1, (lw[used - 1] + 1) / 2);
      lh[used] = std::max(1, (lh[used - 1] + 1) / 2);
      used++;
   }
   // Gradient energy below this per window tap (luma x16 units) is noise: the matrix is
   // pulled toward a flat answer there instead of amplifying it. Measured: 2.6e-4 gave
   // 0.6 px mean error at a 3.2 px move, 0.5 gives 0.29 px.
   constexpr float epsPerTap = 0.5f;
   const int R = std::clamp(window, 1, 6);
   const int iters = std::clamp(iterations, 1, 6);

   if (workW != mWorkW || workH != mWorkH || used != mUsedLevels)
   {
      mHavePrev = false;
      mWorkW = workW;
      mWorkH = workH;
      mUsedLevels = used;
   }

   bool ok = GLUtil::EnsureFbo(mOut, workW, workH, GL_RGBA16F, "OpticalFlow");
   for (int l = 0; l < used && ok; l++)
   {
      ok = GLUtil::EnsureFbo(mLuma[0][l], lw[l], lh[l], GL_RGBA16F, "OpticalFlow")
         && GLUtil::EnsureFbo(mLuma[1][l], lw[l], lh[l], GL_RGBA16F, "OpticalFlow")
         && GLUtil::EnsureFbo(mTensor[l], lw[l], lh[l], GL_RGBA16F, "OpticalFlow")
         && GLUtil::EnsureFbo(mWorkX[l], lw[l], lh[l], GL_RGBA16F, "OpticalFlow")
         && GLUtil::EnsureFbo(mWorkY[l], lw[l], lh[l], GL_RGBA16F, "OpticalFlow")
         && GLUtil::EnsureFbo(mFlow[l][0], lw[l], lh[l], GL_RGBA16F, "OpticalFlow")
         && GLUtil::EnsureFbo(mFlow[l][1], lw[l], lh[l], GL_RGBA16F, "OpticalFlow");
   }
   if (!ok)
      return;

   // Luma pyramid of this frame.
   GLUtil::RunShaderPass(mLuma[mCur][0], mProgLuma, [&]() { Bind(mProgLuma, 0, srcTex, "uSrc"); });
   for (int l = 1; l < used; l++)
   {
      const unsigned int fine = GLUtil::FboTexture(mLuma[mCur][l - 1]);
      GLUtil::RunShaderPass(mLuma[mCur][l], mProgDown, [&]() { Bind(mProgDown, 0, fine, "uSrc"); });
   }

   unsigned int flowTex = 0; // the finest level's final flow
   if (mHavePrev)
   {
      const int prev = 1 - mCur;
      int coarseFlow = -1;  // which of mFlow[l+1] holds the final flow of the level above
      for (int l = used - 1; l >= 0; l--)
      {
         const float tx = 1.0f / (float)lw[l], ty = 1.0f / (float)lh[l];
         const unsigned int prevTex = GLUtil::FboTexture(mLuma[prev][l]);
         const unsigned int curTex = GLUtil::FboTexture(mLuma[mCur][l]);

         auto box = [&](GLUtil::Fbo& dst, const GLUtil::Fbo& src, int sx, int sy)
         {
            GLUtil::RunShaderPass(dst, mProgBox, [&]()
            {
               Bind(mProgBox, 0, GLUtil::FboTexture(src), "uSrc");
               glUniform2i(glGetUniformLocation(mProgBox, "uStep"), sx, sy);
               glUniform1i(glGetUniformLocation(mProgBox, "uR"), R);
            });
         };

         // The gradient matrix of the previous frame does not depend on the flow guess:
         // once per level.
         GLUtil::RunShaderPass(mWorkX[l], mProgGrad, [&]()
         {
            Bind(mProgGrad, 0, prevTex, "uPrev");
            glUniform2f(glGetUniformLocation(mProgGrad, "uTexel"), tx, ty);
         });
         box(mWorkY[l], mWorkX[l], 1, 0);
         box(mTensor[l], mWorkY[l], 0, 1);

         int cur = 0;
         for (int it = 0; it < iters; it++)
         {
            const bool first = it == 0;
            const bool hasCoarse = first && l < used - 1;
            const bool useGuess = !first || hasCoarse;
            const unsigned int guessTex = !useGuess ? 0
               : (first ? GLUtil::FboTexture(mFlow[l + 1][coarseFlow]) : GLUtil::FboTexture(mFlow[l][cur]));
            // A move of one coarse pixel is lw[l] / lw[l + 1] (2 for even sizes) pixels here.
            const float gsx = hasCoarse ? (float)lw[l] / (float)lw[l + 1] : 1.0f;
            const float gsy = hasCoarse ? (float)lh[l] / (float)lh[l + 1] : 1.0f;

            GLUtil::RunShaderPass(mWorkX[l], mProgResid, [&]()
            {
               Bind(mProgResid, 0, prevTex, "uPrev");
               Bind(mProgResid, 1, curTex, "uCur");
               Bind(mProgResid, 2, guessTex, "uGuess");
               glUniform2f(glGetUniformLocation(mProgResid, "uGuessScale"), gsx, gsy);
               glUniform1i(glGetUniformLocation(mProgResid, "uUseGuess"), useGuess ? 1 : 0);
               glUniform2f(glGetUniformLocation(mProgResid, "uTexel"), tx, ty);
            });
            box(mWorkY[l], mWorkX[l], 1, 0);
            box(mWorkX[l], mWorkY[l], 0, 1);

            const int dst = first ? 0 : 1 - cur;
            GLUtil::RunShaderPass(mFlow[l][dst], mProgSolve, [&]()
            {
               Bind(mProgSolve, 0, GLUtil::FboTexture(mTensor[l]), "uTensor");
               Bind(mProgSolve, 1, GLUtil::FboTexture(mWorkX[l]), "uB");
               Bind(mProgSolve, 2, guessTex, "uGuess");
               glUniform2f(glGetUniformLocation(mProgSolve, "uGuessScale"), gsx, gsy);
               glUniform1i(glGetUniformLocation(mProgSolve, "uUseGuess"), useGuess ? 1 : 0);
               glUniform1f(glGetUniformLocation(mProgSolve, "uEps"), epsPerTap * (float)((2 * R + 1) * (2 * R + 1)));
            });
            cur = dst;
         }
         coarseFlow = cur;
         if (l == 0)
            flowTex = GLUtil::FboTexture(mFlow[0][cur]);
      }
   }

   // Output: the finest flow, converted to input pixels. Without a previous frame, zero.
   if (flowTex == 0)
   {
      GLUtil::RunShaderPass(mFlow[0][0], mProgZero, []() {});
      flowTex = GLUtil::FboTexture(mFlow[0][0]);
   }
   const float pix = (float)inW / (float)workW * gain;
   GLUtil::RunShaderPass(mOut, mProgOut, [&]()
   {
      Bind(mProgOut, 0, flowTex, "uFlow");
      glUniform1f(glGetUniformLocation(mProgOut, "uPix"), pix);
      glUniform1i(glGetUniformLocation(mProgOut, "uView"), view == 1 ? 1 : 0);
   });

   mCur = 1 - mCur;
   mHavePrev = true;
}

// Render 3D HDR finish: with `hdr` on, the forward shaders write exposed but
// untonemapped light (sRGB-encoded, unclamped) into the 16-bit float target.
// This stage takes bloom from the light above the threshold and runs the
// tonemap once over scene + bloom, writing back into the same texture, so the
// 2D graph downstream sees an ordinary image.
//
// Bloom is the standard mip chain (Jimenez, "Next Generation Post Processing in
// Call of Duty: Advanced Warfare", SIGGRAPH 2014): a 13-tap downsample to each
// half-size level - the first one thresholded and Karis-averaged so a single
// hot pixel cannot flicker - then a 3x3 tent upsample back up, each level
// blended with the one below it by the radius.
//
// The background (the clear colour, where nothing was drawn and no HDRI is the
// background) keeps its exact colour: it is not tonemapped and does not bloom
// itself, though bloom from the scene spills over it.
#include "Geometry3DNodes.h"
#include "Render3DTonemap.h"

#include "gl3.h"
#include <algorithm>

namespace
{
   const char* kHead =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }\n"
      "vec3 toSrgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }\n"
      "float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n";

   // 13 taps around vUv, `t` = one source texel. Shared by the prefilter
   // (reads the encoded scene, gated by depth) and the plain downsample.
   const char* kDown13 =
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uTexel;\n"
      "vec3 tap(vec2 o);\n"
      "void taps13(out vec3 a, out vec3 b, out vec3 c, out vec3 d, out vec3 e, out vec3 f, out vec3 g,\n"
      "            out vec3 h, out vec3 i, out vec3 j, out vec3 k, out vec3 l, out vec3 m) {\n"
      "   a = tap(vec2(-2.0, 2.0)); b = tap(vec2(0.0, 2.0)); c = tap(vec2(2.0, 2.0));\n"
      "   d = tap(vec2(-2.0, 0.0)); e = tap(vec2(0.0, 0.0)); f = tap(vec2(2.0, 0.0));\n"
      "   g = tap(vec2(-2.0, -2.0)); h = tap(vec2(0.0, -2.0)); i = tap(vec2(2.0, -2.0));\n"
      "   j = tap(vec2(-1.0, 1.0)); k = tap(vec2(1.0, 1.0)); l = tap(vec2(-1.0, -1.0)); m = tap(vec2(1.0, -1.0));\n"
      "}\n";

   // First level: scene light above the threshold (soft knee), Karis-weighted.
   const char* kPrefilterBody =
      "uniform sampler2D uDepth;\n"
      "uniform int uKeepBg;\n"
      "uniform float uThreshold;\n"
      "vec3 tap(vec2 o) {\n"
      "   vec2 uv = vUv + o * uTexel;\n"
      "   if (uKeepBg == 1 && texture(uDepth, uv).r >= 0.999999) return vec3(0.0);\n"
      "   return min(toLinear(texture(uSrc, uv).rgb), vec3(64.0));\n"
      "}\n"
      "vec3 karis(vec3 s, float w, inout float wsum) { float k = w / (1.0 + luma(s)); wsum += k; return s * k; }\n"
      "void main() {\n"
      "   vec3 a, b, c, d, e, f, g, h, i, j, k, l, m;\n"
      "   taps13(a, b, c, d, e, f, g, h, i, j, k, l, m);\n"
      "   float ws = 0.0;\n"
      "   vec3 s = karis((j + k + l + m) * 0.25, 0.5, ws);\n"
      "   s += karis((a + b + d + e) * 0.25, 0.125, ws);\n"
      "   s += karis((b + c + e + f) * 0.25, 0.125, ws);\n"
      "   s += karis((d + e + g + h) * 0.25, 0.125, ws);\n"
      "   s += karis((e + f + h + i) * 0.25, 0.125, ws);\n"
      "   s /= max(ws, 1e-6);\n"
      "   float br = max(s.r, max(s.g, s.b));\n"
      "   float knee = uThreshold * 0.5;\n"
      "   float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);\n"
      "   soft = soft * soft / (4.0 * knee + 1e-5);\n"
      "   fragColor = vec4(s * (max(soft, br - uThreshold) / max(br, 1e-5)), 1.0);\n"
      "}\n";

   const char* kDownBody =
      "vec3 tap(vec2 o) { return texture(uSrc, vUv + o * uTexel).rgb; }\n"
      "void main() {\n"
      "   vec3 a, b, c, d, e, f, g, h, i, j, k, l, m;\n"
      "   taps13(a, b, c, d, e, f, g, h, i, j, k, l, m);\n"
      "   vec3 s = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;\n"
      "   fragColor = vec4(s, 1.0);\n"
      "}\n";

   // This level, blended with the tent-filtered level below it by the radius.
   const char* kUpBody =
      "uniform sampler2D uCur;\n"
      "uniform sampler2D uBelow;\n"
      "uniform vec2 uTexel;\n" // one texel of uBelow
      "uniform float uMix;\n"
      "vec3 t(float x, float y) { return texture(uBelow, vUv + vec2(x, y) * uTexel).rgb; }\n"
      "void main() {\n"
      "   vec3 tent = (t(0.0, 0.0) * 4.0 + (t(0.0, 1.0) + t(-1.0, 0.0) + t(1.0, 0.0) + t(0.0, -1.0)) * 2.0\n"
      "             + t(-1.0, 1.0) + t(1.0, 1.0) + t(-1.0, -1.0) + t(1.0, -1.0)) / 16.0;\n"
      "   fragColor = vec4(mix(texture(uCur, vUv).rgb, tent, uMix), 1.0);\n"
      "}\n";

   const char* kFinishBody =
      "uniform sampler2D uColor;\n"
      "uniform sampler2D uDepth;\n"
      "uniform sampler2D uBloom;\n"
      "uniform float uBloomAmount;\n"
      "uniform int uTonemap;\n"
      "uniform int uKeepBg;\n"
      "vec3 acesFilm(vec3 x) {\n"
      "   return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n"
      "}\n"
      R3D_TONEMAP_GLSL
      "vec3 tone(vec3 c) {\n"
      "   if (uTonemap == 1) return acesFilm(c);\n"
      "   if (uTonemap == 2) return c / (c + vec3(1.0));\n"
      "   if (uTonemap == 3) return agxTone(c);\n"
      "   if (uTonemap == 4) return neutralTone(c);\n"
      "   return c;\n"
      "}\n"
      "void main() {\n"
      "   vec4 c = texture(uColor, vUv);\n"
      "   vec3 b = uBloomAmount > 0.0 ? texture(uBloom, vUv).rgb * uBloomAmount : vec3(0.0);\n"
      "   if (uKeepBg == 1 && texture(uDepth, vUv).r >= 0.999999) {\n"
      "      vec3 glow = clamp(tone(b), 0.0, 1.0);\n"
      "      fragColor = vec4(toSrgb(toLinear(c.rgb) + glow), max(c.a, max(glow.r, max(glow.g, glow.b))));\n"
      "      return;\n"
      "   }\n"
      "   fragColor = vec4(toSrgb(tone(toLinear(c.rgb) + b)), c.a);\n"
      "}\n";

   void Bind(unsigned int prog, int unit, unsigned int tex, const char* name)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(prog, name), unit);
   }
}

bool Render3DNode::EnsureHdrShaders()
{
   if (mHdrShadersTried)
      return mProgHdr != 0;
   mHdrShadersTried = true;
   const std::string head = kHead;
   mProgBloomPre = GLUtil::CompileProgram((head + kDown13 + kPrefilterBody).c_str());
   mProgBloomDown = GLUtil::CompileProgram((head + kDown13 + kDownBody).c_str());
   mProgBloomUp = GLUtil::CompileProgram((head + kUpBody).c_str());
   mProgHdr = GLUtil::CompileProgram((head + kFinishBody).c_str());
   if (!(mProgBloomPre && mProgBloomDown && mProgBloomUp && mProgHdr))
      mProgHdr = 0;
   return mProgHdr != 0;
}

void Render3DNode::ReleaseHdr()
{
   for (int i = 0; i < kBloomLevels; i++)
   {
      GLUtil::DestroyFbo(mBloomDown[i]);
      GLUtil::DestroyFbo(mBloomUp[i]);
   }
   GLUtil::DestroyFbo(mHdrOut);
   for (unsigned int* p : { &mProgBloomPre, &mProgBloomDown, &mProgBloomUp, &mProgHdr })
      if (*p != 0) { glDeleteProgram(*p); *p = 0; }
}

void Render3DNode::RunHdr(int w, int h)
{
   if (!EnsureHdrShaders() || mColorTex == 0 || mDepthTex == 0)
      return;
   if (!GLUtil::EnsureFbo(mHdrOut, w, h, GL_RGBA16F, "Render3D_HDR"))
      return;

   const GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
   const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_BLEND);
   glDepthMask(GL_FALSE);

   const int keepBg = mEnvBgDrawn ? 0 : 1;
   const float amount = std::clamp(bloom, 0.0f, 1.0f);
   unsigned int bloomTex = mColorTex; // a real texture even when unused
   if (amount > 0.0f)
   {
      // Half size down to the last level at least 8 px on its short side.
      int levels = 0;
      int lw = w, lh = h;
      bool ok = true;
      while (levels < kBloomLevels && std::min(lw, lh) >= 16)
      {
         lw = std::max(1, lw / 2);
         lh = std::max(1, lh / 2);
         ok = ok && GLUtil::EnsureFbo(mBloomDown[levels], lw, lh, GL_RGBA16F, "Render3D_Bloom") &&
              GLUtil::EnsureFbo(mBloomUp[levels], lw, lh, GL_RGBA16F, "Render3D_Bloom");
         levels++;
      }
      if (ok && levels > 0)
      {
         const float threshold = std::clamp(bloomThreshold, 0.5f, 8.0f);
         GLUtil::RunShaderPass(mBloomDown[0], mProgBloomPre, [&]() {
            Bind(mProgBloomPre, 0, mColorTex, "uSrc");
            Bind(mProgBloomPre, 1, mDepthTex, "uDepth");
            glUniform2f(glGetUniformLocation(mProgBloomPre, "uTexel"), 1.0f / (float)w, 1.0f / (float)h);
            glUniform1i(glGetUniformLocation(mProgBloomPre, "uKeepBg"), keepBg);
            glUniform1f(glGetUniformLocation(mProgBloomPre, "uThreshold"), threshold);
         });
         for (int i = 1; i < levels; i++)
         {
            const GLUtil::Fbo& src = mBloomDown[i - 1];
            GLUtil::RunShaderPass(mBloomDown[i], mProgBloomDown, [&]() {
               Bind(mProgBloomDown, 0, GLUtil::FboTexture(src), "uSrc");
               glUniform2f(glGetUniformLocation(mProgBloomDown, "uTexel"), 1.0f / (float)src.w, 1.0f / (float)src.h);
            });
         }
         // Radius 0 keeps the finest level (a tight glow), 1 lets the widest through.
         const float mixAmt = 0.15f + 0.8f * std::clamp(bloomRadius, 0.0f, 1.0f);
         unsigned int below = GLUtil::FboTexture(mBloomDown[levels - 1]);
         int belowW = mBloomDown[levels - 1].w, belowH = mBloomDown[levels - 1].h;
         for (int i = levels - 2; i >= 0; i--)
         {
            GLUtil::RunShaderPass(mBloomUp[i], mProgBloomUp, [&]() {
               Bind(mProgBloomUp, 0, GLUtil::FboTexture(mBloomDown[i]), "uCur");
               Bind(mProgBloomUp, 1, below, "uBelow");
               glUniform2f(glGetUniformLocation(mProgBloomUp, "uTexel"), 1.0f / (float)belowW, 1.0f / (float)belowH);
               glUniform1f(glGetUniformLocation(mProgBloomUp, "uMix"), mixAmt);
            });
            below = GLUtil::FboTexture(mBloomUp[i]);
            belowW = mBloomUp[i].w;
            belowH = mBloomUp[i].h;
         }
         bloomTex = below;
      }
   }

   const float bloomAmount = bloomTex != mColorTex ? amount : 0.0f;
   GLUtil::RunShaderPass(mHdrOut, mProgHdr, [&]() {
      Bind(mProgHdr, 0, mColorTex, "uColor");
      Bind(mProgHdr, 1, mDepthTex, "uDepth");
      Bind(mProgHdr, 2, bloomTex, "uBloom");
      glUniform1f(glGetUniformLocation(mProgHdr, "uBloomAmount"), bloomAmount);
      glUniform1i(glGetUniformLocation(mProgHdr, "uTonemap"), std::clamp(tonemap, 0, (int)TonemapNames().size() - 1));
      glUniform1i(glGetUniformLocation(mProgHdr, "uKeepBg"), keepBg);
   });
   glActiveTexture(GL_TEXTURE0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, mHdrOut.fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glDepthMask(GL_TRUE);
   if (wasDepth) glEnable(GL_DEPTH_TEST);
   if (wasCull) glEnable(GL_CULL_FACE);
}

// Render 3D quality stage (v0.6 3.5): ambient occlusion, depth of field and the
// offline supersample accumulator. Everything here runs after the scene is
// drawn and resolved, reads the depth texture, and writes back into the same
// output texture, so the 2D graph downstream sees an ordinary image.
#include "Geometry3DNodes.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include "Transport.h"

namespace
{
   // View-space position from the depth texture. uProj = (m0, m5, m10, m14) of the
   // projection matrix; perspective divides by -z, orthographic does not.
   const char* kViewSpaceGlsl =
      "uniform sampler2D uDepth;\n"
      "uniform vec4 uProj;\n"
      "uniform int uOrtho;\n"
      "float viewDist(float d) {\n"
      "   float n = d * 2.0 - 1.0;\n"
      "   float z = (uOrtho == 1) ? (n - uProj.w) / uProj.z : -uProj.w / (n + uProj.z);\n"
      "   return -z;\n"
      "}\n"
      "// Snapped to the texel the depth came from: depth from one pixel with x and y from\n"
      "// another lifts a sloped floor off its own plane, in stripes.\n"
      "vec3 viewPos(vec2 uv) {\n"
      "   ivec2 depthSize = textureSize(uDepth, 0);\n"
      "   ivec2 texel = clamp(ivec2(uv * vec2(depthSize)), ivec2(0), depthSize - 1);\n"
      "   float d = texelFetch(uDepth, texel, 0).r;\n"
      "   uv = (vec2(texel) + 0.5) / vec2(depthSize);\n"
      "   float dist = viewDist(d);\n"
      "   vec2 ndc = uv * 2.0 - 1.0;\n"
      "   vec2 xy = (uOrtho == 1) ? vec2(ndc.x / uProj.x, ndc.y / uProj.y) : vec2(ndc.x * dist / uProj.x, ndc.y * dist / uProj.y);\n"
      "   return vec3(xy, -dist);\n"
      "}\n";

   // Pass 1: visibility in R, view distance in G (so the blur needs no second
   // depth read). Sixteen taps on a golden-angle spiral, rotated per pixel by
   // interleaved gradient noise; the blur pass averages the rotation away.
   const char* kAoBody =
      "uniform vec2 uSize;\n"
      "uniform float uRadius;\n"
      "void main() {\n"
      "   float d0 = texture(uDepth, vUv).r;\n"
      "   if (d0 >= 0.999999) { fragColor = vec4(1.0, 1.0e5, 0.0, 1.0); return; }\n"
      "   vec3 P = viewPos(vUv);\n"
      "   vec2 t = 1.0 / uSize;\n"
      "   vec3 pl = viewPos(vUv - vec2(t.x, 0.0)), pr = viewPos(vUv + vec2(t.x, 0.0));\n"
      "   vec3 pd = viewPos(vUv - vec2(0.0, t.y)), pu = viewPos(vUv + vec2(0.0, t.y));\n"
      "   vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;\n"
      "   vec3 dy = abs(pu.z - P.z) < abs(P.z - pd.z) ? pu - P : P - pd;\n"
      "   vec3 N = normalize(cross(dx, dy));\n"
      "   float dist = -P.z;\n"
      "   float pxPerUnit = uProj.y * uSize.y * 0.5 / ((uOrtho == 1) ? 1.0 : max(dist, 1.0e-3));\n"
      "   float rPx = min(uRadius * pxPerUnit, 0.25 * uSize.y);\n"
      "   float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
      "   float occ = 0.0;\n"
      "   for (int i = 0; i < 16; i++) {\n"
      "      float a = rot + float(i) * 2.399963;\n"
      "      float rho = sqrt((float(i) + 0.5) / 16.0);\n"
      "      vec2 uv = vUv + vec2(cos(a), sin(a)) * rho * rPx * t;\n"
      "      if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) continue;\n"
      "      if (texture(uDepth, uv).r >= 0.999999) continue;\n"
      "      vec3 v = viewPos(uv) - P;\n"
      "      float len = length(v);\n"
      "      if (len < 1.0e-5) continue;\n"
      "      float ndv = dot(N, v) / len;\n"
      "      occ += clamp((ndv - 0.15) / 0.85, 0.0, 1.0) * (1.0 - clamp(len / uRadius, 0.0, 1.0));\n"
      "   }\n"
      "   float vis = 1.0 - clamp(2.0 * occ / 16.0, 0.0, 1.0);\n"
      "   fragColor = vec4(vis, dist, 0.0, 1.0);\n"
      "}\n";

   // Pass 2/3: separable depth-aware blur of the visibility.
   const char* kBlurBody =
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uStep;\n"
      "uniform float uRange;\n"
      "void main() {\n"
      "   vec4 c = texture(uSrc, vUv);\n"
      "   float sum = c.r, wsum = 1.0;\n"
      "   for (int i = 1; i <= 4; i++) {\n"
      "      float g = exp(-float(i * i) / 8.0);\n"
      "      vec4 a = texture(uSrc, vUv + uStep * float(i));\n"
      "      vec4 b = texture(uSrc, vUv - uStep * float(i));\n"
      "      float wa = g * exp(-abs(a.g - c.g) / uRange);\n"
      "      float wb = g * exp(-abs(b.g - c.g) / uRange);\n"
      "      sum += a.r * wa + b.r * wb;\n"
      "      wsum += wa + wb;\n"
      "   }\n"
      "   fragColor = vec4(sum / wsum, c.g, 0.0, 1.0);\n"
      "}\n";

   // Composite: occlusion multiplies the colour, then a thin-lens gather blurs by
   // the circle of confusion each pixel gets from its distance.
   const char* kPostBody =
      "uniform sampler2D uColor;\n"
      "uniform sampler2D uAo;\n"
      "uniform vec2 uSize;\n"
      "uniform int uAoOn;\n"
      "uniform float uAoStrength;\n"
      "uniform float uAoRange;\n"
      "uniform int uDofOn;\n"
      "uniform float uFocus;\n"
      "uniform float uAperturePx;\n"
      "uniform float uMaxCoc;\n"
      "float cocOf(vec2 uv) {\n"
      "   float d = texture(uDepth, uv).r;\n"
      "   float dist = d >= 0.999999 ? 1.0e4 : viewDist(d);\n"
      "   return clamp(uAperturePx * abs(dist - uFocus) / max(dist, 1.0e-3), 0.0, uMaxCoc);\n"
      "}\n"
      "// The occlusion is half size: take the four nearest texels, weighted by how close\n"
      "// their distance is to this pixel's, so it does not smear across an edge.\n"
      "float aoAt(vec2 uv) {\n"
      "   ivec2 lowSize = textureSize(uAo, 0);\n"
      "   vec2 p = uv * vec2(lowSize) - 0.5;\n"
      "   ivec2 i0 = ivec2(floor(p));\n"
      "   vec2 f = p - vec2(i0);\n"
      "   float d = texture(uDepth, uv).r;\n"
      "   float dist = d >= 0.999999 ? 1.0e5 : viewDist(d);\n"
      "   float sum = 0.0, wsum = 0.0;\n"
      "   for (int k = 0; k < 4; k++) {\n"
      "      ivec2 o = ivec2(k & 1, k >> 1);\n"
      "      vec4 t = texelFetch(uAo, clamp(i0 + o, ivec2(0), lowSize - 1), 0);\n"
      "      float bw = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);\n"
      "      float w = bw * exp(-abs(t.g - dist) / uAoRange) + 1.0e-5;\n"
      "      sum += t.r * w; wsum += w;\n"
      "   }\n"
      "   return sum / wsum;\n"
      "}\n"
      "vec4 shade(vec2 uv) {\n"
      "   vec4 c = texture(uColor, uv);\n"
      "   if (uAoOn == 1) c.rgb *= mix(1.0, aoAt(uv), uAoStrength);\n"
      "   return c;\n"
      "}\n"
      "void main() {\n"
      "   vec4 c0 = shade(vUv);\n"
      "   if (uDofOn == 0) { fragColor = c0; return; }\n"
      "   float coc0 = cocOf(vUv);\n"
      "   if (coc0 < 0.5) { fragColor = c0; return; }\n"
      "   vec2 t = 1.0 / uSize;\n"
      "   vec4 sum = c0;\n"
      "   float wsum = 1.0;\n"
      "   // Taps follow the blur size; a per-pixel rotation turns the thin sampling into noise, not rings.\n"
      "   int taps = int(clamp(12.0 + coc0 * 2.0, 24.0, 48.0));\n"
      "   float rot = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
      "   for (int i = 0; i < taps; i++) {\n"
      "      float a = rot + float(i) * 2.399963;\n"
      "      float r = sqrt((float(i) + 0.5) / float(taps)) * coc0;\n"
      "      vec2 uv = vUv + vec2(cos(a), sin(a)) * r * t;\n"
      "      float w = clamp(cocOf(uv) - r + 1.0, 0.0, 1.0);\n"
      "      if (w <= 0.0) continue;\n"
      "      sum += shade(uv) * w;\n"
      "      wsum += w;\n"
      "   }\n"
      "   fragColor = sum / wsum;\n"
      "}\n";

   // Accumulation: premultiplied running sum, then the resolve back to straight alpha.
   const char* kAccumFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uPrev;\n"
      "uniform sampler2D uCur;\n"
      "uniform int uFirst;\n"
      "void main() {\n"
      "   vec4 c = texture(uCur, vUv);\n"
      "   vec4 p = uFirst == 1 ? vec4(0.0) : texture(uPrev, vUv);\n"
      "   fragColor = p + vec4(c.rgb * c.a, c.a);\n"
      "}\n";
   const char* kAccumDoneFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSum;\n"
      "uniform float uInvN;\n"
      "void main() {\n"
      "   vec4 s = texture(uSum, vUv);\n"
      "   fragColor = vec4(s.a > 1.0e-6 ? s.rgb / s.a : vec3(0.0), s.a * uInvN);\n"
      "}\n";

   void Bind(unsigned int prog, int unit, unsigned int tex, const char* name)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(prog, name), unit);
   }
}

bool Render3DNode::EnsurePostShaders()
{
   if (mPostShadersTried)
      return mProgPost != 0;
   mPostShadersTried = true;
   const std::string head = "#version 150\nin vec2 vUv;\nout vec4 fragColor;\n";
   const std::string ao = head + kViewSpaceGlsl + kAoBody;
   const std::string blur = head + kBlurBody;
   const std::string post = head + kViewSpaceGlsl + kPostBody;
   mProgAo = GLUtil::CompileProgram(ao.c_str());
   mProgAoBlur = GLUtil::CompileProgram(blur.c_str());
   mProgPost = GLUtil::CompileProgram(post.c_str());
   mProgAccum = GLUtil::CompileProgram(kAccumFrag);
   mProgAccumDone = GLUtil::CompileProgram(kAccumDoneFrag);
   if (!(mProgAo && mProgAoBlur && mProgPost && mProgAccum && mProgAccumDone))
      mProgPost = 0;
   return mProgPost != 0;
}

void Render3DNode::ReleasePost()
{
   for (GLUtil::Fbo* f : { &mPostAo[0], &mPostAo[1], &mPostOut, &mAccum[0], &mAccum[1] })
      GLUtil::DestroyFbo(*f);
   for (unsigned int* p : { &mProgAo, &mProgAoBlur, &mProgPost, &mProgAccum, &mProgAccumDone })
      if (*p != 0) { glDeleteProgram(*p); *p = 0; }
}

void Render3DNode::RunPost(const Mat4& proj, bool ortho, int w, int h)
{
   if (!EnsurePostShaders() || mDepthTex == 0)
      return;
   const bool aoOn = aoStrength > 0.0f;
   const bool dofOn = dofAperture > 0.0f;
   const GLenum outFmt = mFloatTarget ? GL_RGBA16F : GL_RGBA8;
   bool ok = GLUtil::EnsureFbo(mPostOut, w, h, outFmt, "Render3D_Post");
   if (aoOn)
      ok = ok && GLUtil::EnsureFbo(mPostAo[0], (w + 1) / 2, (h + 1) / 2, GL_RGBA16F, "Render3D_AO") &&
           GLUtil::EnsureFbo(mPostAo[1], (w + 1) / 2, (h + 1) / 2, GL_RGBA16F, "Render3D_AO");
   if (!ok)
      return;

   const GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
   const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_BLEND);
   glDepthMask(GL_FALSE);

   const float projVec[4] = { proj.m[0], proj.m[5], proj.m[10], proj.m[14] };
   const int orthoI = ortho ? 1 : 0;
   const float sz[2] = { (float)w, (float)h };
   auto setProj = [&](unsigned int prog) {
      glUniform4fv(glGetUniformLocation(prog, "uProj"), 1, projVec);
      glUniform1i(glGetUniformLocation(prog, "uOrtho"), orthoI);
   };
   const float radius = std::max(0.001f, aoRadius);

   if (aoOn)
   {
      GLUtil::RunShaderPass(mPostAo[0], mProgAo, [&]() {
         Bind(mProgAo, 0, mDepthTex, "uDepth");
         setProj(mProgAo);
         glUniform2fv(glGetUniformLocation(mProgAo, "uSize"), 1, sz);
         glUniform1f(glGetUniformLocation(mProgAo, "uRadius"), radius);
      });
      for (int pass = 0; pass < 2; pass++)
      {
         const GLUtil::Fbo& src = mPostAo[pass];
         const GLUtil::Fbo& dst = mPostAo[1 - pass];
         const float step[2] = { pass == 0 ? 1.0f / (float)mPostAo[0].w : 0.0f, pass == 0 ? 0.0f : 1.0f / (float)mPostAo[0].h };
         GLUtil::RunShaderPass(dst, mProgAoBlur, [&]() {
            Bind(mProgAoBlur, 0, GLUtil::FboTexture(src), "uSrc");
            glUniform2fv(glGetUniformLocation(mProgAoBlur, "uStep"), 1, step);
            glUniform1f(glGetUniformLocation(mProgAoBlur, "uRange"), radius * 0.3f);
         });
      }
   }

   const float hScale = (float)h / 1080.0f;
   GLUtil::RunShaderPass(mPostOut, mProgPost, [&]() {
      Bind(mProgPost, 0, mColorTex, "uColor");
      Bind(mProgPost, 1, mDepthTex, "uDepth");
      Bind(mProgPost, 2, aoOn ? GLUtil::FboTexture(mPostAo[0]) : mColorTex, "uAo"); // a real texture even when unused
      setProj(mProgPost);
      glUniform2fv(glGetUniformLocation(mProgPost, "uSize"), 1, sz);
      glUniform1i(glGetUniformLocation(mProgPost, "uAoOn"), aoOn ? 1 : 0);
      glUniform1f(glGetUniformLocation(mProgPost, "uAoStrength"), std::min(1.0f, aoStrength));
      glUniform1f(glGetUniformLocation(mProgPost, "uAoRange"), radius * 0.3f);
      glUniform1i(glGetUniformLocation(mProgPost, "uDofOn"), dofOn ? 1 : 0);
      glUniform1f(glGetUniformLocation(mProgPost, "uFocus"), focusDistance);
      glUniform1f(glGetUniformLocation(mProgPost, "uAperturePx"), dofAperture * 24.0f * hScale);
      glUniform1f(glGetUniformLocation(mProgPost, "uMaxCoc"), std::max(2.0f, 0.03f * (float)h));
   });
   glActiveTexture(GL_TEXTURE0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, mPostOut.fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glDepthMask(GL_TRUE);
   if (wasDepth) glEnable(GL_DEPTH_TEST);
   if (wasCull) glEnable(GL_CULL_FACE);
}

// Offline supersampling: cook the scene `supersample` times with the camera
// nudged by a sub-pixel amount, and average. Returns false when the targets
// cannot be made, so the caller falls back to one plain pass.
bool Render3DNode::Accumulate(int frameId)
{
   const int n = std::max(2, std::min(supersample, 64));
   const int w = GLUtil::TargetSide(width, 16);
   const int h = GLUtil::TargetSide(height, 16);
   if (!EnsurePostShaders())
      return false;
   if (!GLUtil::EnsureFbo(mAccum[0], w, h, GL_RGBA32F, "Render3D_Accum") ||
       !GLUtil::EnsureFbo(mAccum[1], w, h, GL_RGBA32F, "Render3D_Accum"))
      return false;

   GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
   GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
   GLboolean wasBlend = glIsEnabled(GL_BLEND);
   int cur = 0;
   mInAccumulate = true;
   for (int i = 0; i < n; i++)
   {
      mJitterIndex = i;
      CookOnce(frameId);
      if (mColorTex == 0 || mWidth != w || mHeight != h)
         break; // the single pass failed; leave whatever it left
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      glDisable(GL_BLEND);
      const int prev = cur;
      cur = 1 - cur;
      GLUtil::RunShaderPass(mAccum[cur], mProgAccum, [&]() {
         Bind(mProgAccum, 0, GLUtil::FboTexture(mAccum[prev]), "uPrev");
         Bind(mProgAccum, 1, mColorTex, "uCur");
         glUniform1i(glGetUniformLocation(mProgAccum, "uFirst"), i == 0 ? 1 : 0);
      });
      glActiveTexture(GL_TEXTURE0);
   }
   mJitterIndex = -1;
   mInAccumulate = false;

   GLUtil::Fbo target;
   target.fbo = mFbo; target.tex = mColorTex; target.w = w; target.h = h;
   target.internalFormat = mFloatTarget ? GL_RGBA16F : GL_RGBA8;
   GLUtil::RunShaderPass(target, mProgAccumDone, [&]() {
      Bind(mProgAccumDone, 0, GLUtil::FboTexture(mAccum[cur]), "uSum");
      glUniform1f(glGetUniformLocation(mProgAccumDone, "uInvN"), 1.0f / (float)n);
   });
   glActiveTexture(GL_TEXTURE0);
   if (wasDepth) glEnable(GL_DEPTH_TEST);
   if (wasCull) glEnable(GL_CULL_FACE);
   if (wasBlend) glEnable(GL_BLEND);
   mRevision = NextTextureRevision();
   return true;
}

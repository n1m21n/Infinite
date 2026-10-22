#include "Render3DPost.h"

#include "core/GLUtil.h"
#include "platform/OpenGLHeaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
   // Shared by the IBL passes: direction <-> equirect uv, matching Render 3D's
   // own dirToEquirect without the rotation (the rotation is applied at lookup).
   const char* kEquirectCommon =
      "const float PI = 3.14159265359;\n"
      "vec3 uvToDir(vec2 uv) {\n"
      "   float phi = (uv.x - 0.5) * 2.0 * PI;\n"
      "   float theta = (1.0 - uv.y) * PI;\n"
      "   return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));\n"
      "}\n"
      "vec2 dirToUv(vec3 d) {\n"
      "   float phi = atan(d.z, d.x);\n"
      "   float theta = acos(clamp(d.y, -1.0, 1.0));\n"
      "   return vec2(phi / (2.0 * PI) + 0.5, 1.0 - theta / PI);\n"
      "}\n"
      "float radicalInverse(uint b) {\n"
      "   b = (b << 16u) | (b >> 16u);\n"
      "   b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);\n"
      "   b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);\n"
      "   b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);\n"
      "   b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);\n"
      "   return float(b) * 2.3283064365386963e-10;\n"
      "}\n"
      "vec2 hammersley(uint i, uint n) { return vec2(float(i) / float(n), radicalInverse(i)); }\n"
      "vec3 importanceGGX(vec2 xi, vec3 n, float a) {\n"
      "   float phi = 2.0 * PI * xi.x;\n"
      "   float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));\n"
      "   float sinT = sqrt(1.0 - cosT * cosT);\n"
      "   vec3 h = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);\n"
      "   vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
      "   vec3 t = normalize(cross(up, n));\n"
      "   vec3 b = cross(n, t);\n"
      "   return normalize(t * h.x + b * h.y + n * h.z);\n"
      "}\n";

   // One mip of the prefiltered map: GGX lobe at uRough, importance sampled,
   // reading a blurrier source mip for low-probability samples (filtered
   // importance sampling, GPU Gems 3 ch. 20) so 64 samples do not sparkle.
   std::string PrefilterSrc()
   {
      return std::string("#version 330 core\n"
                         "in vec2 vUv;\n"
                         "out vec4 fragColor;\n"
                         "uniform sampler2D uEnv;\n"
                         "uniform float uRough;\n"
                         "uniform float uSrcTexels;\n"
                         "uniform float uSrcMaxLod;\n") +
             kEquirectCommon +
             "void main() {\n"
             "   vec3 n = uvToDir(vUv);\n"
             "   if (uRough < 0.001) { fragColor = vec4(textureLod(uEnv, vUv, 0.0).rgb, 1.0); return; }\n"
             "   float a = uRough * uRough;\n"
             "   const uint N = 64u;\n"
             "   vec3 acc = vec3(0.0); float w = 0.0;\n"
             "   for (uint i = 0u; i < N; i++) {\n"
             "      vec3 h = importanceGGX(hammersley(i, N), n, a);\n"
             "      vec3 l = normalize(2.0 * dot(n, h) * h - n);\n"
             "      float nl = dot(n, l);\n"
             "      if (nl <= 0.0) continue;\n"
             "      float nh = max(dot(n, h), 0.0);\n"
             "      float d = (nh * nh * (a * a - 1.0) + 1.0);\n"
             "      float D = a * a / (PI * d * d);\n"
             "      float pdf = D / 4.0 + 1e-4;\n"
             "      float saSample = 1.0 / (float(N) * pdf);\n"
             "      float saTexel = 4.0 * PI / uSrcTexels;\n"
             "      float lod = clamp(0.5 * log2(saSample / saTexel) + 1.0, 0.0, uSrcMaxLod);\n"
             "      acc += textureLod(uEnv, dirToUv(l), lod).rgb * nl;\n"
             "      w += nl;\n"
             "   }\n"
             "   fragColor = vec4(acc / max(w, 1e-4), 1.0);\n"
             "}\n";
   }

   // Cosine-weighted hemisphere integral of the environment: the diffuse light
   // a surface facing each direction receives. Read from a blurry mip.
   std::string IrradianceSrc()
   {
      return std::string("#version 330 core\n"
                         "in vec2 vUv;\n"
                         "out vec4 fragColor;\n"
                         "uniform sampler2D uEnv;\n"
                         "uniform float uLod;\n") +
             kEquirectCommon +
             "void main() {\n"
             "   vec3 n = uvToDir(vUv);\n"
             "   vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
             "   vec3 t = normalize(cross(up, n));\n"
             "   vec3 b = cross(n, t);\n"
             "   const uint N = 256u;\n"
             "   vec3 acc = vec3(0.0);\n"
             "   for (uint i = 0u; i < N; i++) {\n"
             "      vec2 xi = hammersley(i, N);\n"
             "      float r = sqrt(xi.x), phi = 2.0 * PI * xi.y;\n"
             "      vec3 l = t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - xi.x, 0.0));\n"
             "      acc += textureLod(uEnv, dirToUv(l), uLod).rgb;\n"
             "   }\n"
             "   fragColor = vec4(acc / float(N), 1.0);\n"
             "}\n";
   }

   // Split-sum BRDF table: x = NdotV, y = roughness -> (scale, bias) on F0.
   std::string LutSrc()
   {
      return std::string("#version 330 core\n"
                         "in vec2 vUv;\n"
                         "out vec4 fragColor;\n") +
             kEquirectCommon +
             "void main() {\n"
             "   float nv = max(vUv.x, 1e-3), rough = vUv.y;\n"
             "   float a = rough * rough;\n"
             "   vec3 v = vec3(sqrt(1.0 - nv * nv), 0.0, nv);\n"
             "   float A = 0.0, B = 0.0;\n"
             "   const uint N = 512u;\n"
             "   float k = a / 2.0;\n"
             "   for (uint i = 0u; i < N; i++) {\n"
             "      vec2 xi = hammersley(i, N);\n"
             "      float phi = 2.0 * PI * xi.x;\n"
             "      float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));\n"
             "      float sinT = sqrt(1.0 - cosT * cosT);\n"
             "      vec3 h = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);\n"
             "      vec3 l = normalize(2.0 * dot(v, h) * h - v);\n"
             "      float nl = max(l.z, 0.0), nh = max(h.z, 0.0), vh = max(dot(v, h), 0.0);\n"
             "      if (nl > 0.0) {\n"
             "         float g = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));\n"
             "         float gv = g * vh / max(nh * nv, 1e-4);\n"
             "         float fc = pow(1.0 - vh, 5.0);\n"
             "         A += (1.0 - fc) * gv; B += fc * gv;\n"
             "      }\n"
             "   }\n"
             "   fragColor = vec4(A / float(N), B / float(N), 0.0, 1.0);\n"
             "}\n";
   }

   // Bloom: a soft-knee bright pass, then a separable 9-tap blur per level.
   const char* kBrightSrc =
      "#version 330 core\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform float uThreshold;\n"
      "void main() {\n"
      "   vec3 c = texture(uSrc, vUv).rgb;\n"
      "   float br = max(c.r, max(c.g, c.b));\n"
      "   float knee = uThreshold * 0.5;\n"
      "   float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);\n"
      "   soft = soft * soft / (4.0 * knee + 1e-4);\n"
      "   float contrib = max(soft, br - uThreshold) / max(br, 1e-4);\n"
      "   fragColor = vec4(c * contrib, 1.0);\n"
      "}\n";

   const char* kBlurSrc =
      "#version 330 core\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform vec2 uStep;\n"
      "void main() {\n"
      "   const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
      "   vec3 c = texture(uSrc, vUv).rgb * w[0];\n"
      "   for (int i = 1; i < 5; i++) {\n"
      "      c += texture(uSrc, vUv + uStep * float(i)).rgb * w[i];\n"
      "      c += texture(uSrc, vUv - uStep * float(i)).rgb * w[i];\n"
      "   }\n"
      "   fragColor = vec4(c, 1.0);\n"
      "}\n";

   // Tonemaps: 1 ACES (Narkowicz fit, as before), 2 Reinhard, 3 AgX (Troy
   // Sobotka's, the minimal fit by Benjamin Wrensch), 4 Khronos PBR Neutral.
   const char* kFinishSrc =
      "#version 330 core\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uBloom0, uBloom1, uBloom2, uBloom3, uBloom4, uBloom5;\n"
      "uniform int uLevels;\n"
      "uniform float uBloom;\n"
      "uniform float uRadius;\n"
      "uniform int uTonemap;\n"
      "uniform int uPassthrough;\n"
      "uniform sampler2D uDepth;\n"
      "uniform int uBgKey;\n"
      "uniform vec3 uBg;\n"
      "uniform float uBgOpacity;\n"
      "vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n"
      "vec3 agxContrast(vec3 x) {\n"
      "   vec3 x2 = x * x, x4 = x2 * x2;\n"
      "   return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;\n"
      "}\n"
      "vec3 agx(vec3 c) {\n"
      "   const mat3 inMat = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,\n"
      "                           0.0784335999999992, 0.878468636469772, 0.0784336,\n"
      "                           0.0792237451477643, 0.0791661274605434, 0.879142973793104);\n"
      "   const mat3 outMat = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,\n"
      "                            -0.0980208811401368, 1.15190312990417, -0.0980434501171241,\n"
      "                            -0.0990297440797205, -0.0989611768448433, 1.15107367264116);\n"
      "   c = inMat * max(c, vec3(1e-10));\n"
      "   c = clamp((log2(c) + 12.47393) / 16.5, 0.0, 1.0);\n"
      "   c = agxContrast(c);\n"
      "   c = outMat * c;\n"
      "   return pow(max(c, vec3(0.0)), vec3(2.2));\n" // back to linear for the shared sRGB step
      "}\n"
      "vec3 neutral(vec3 c) {\n"
      "   const float start = 0.8 - 0.04, desat = 0.15;\n"
      "   float x = min(c.r, min(c.g, c.b));\n"
      "   float off = x < 0.08 ? x - 6.25 * x * x : 0.04;\n"
      "   c -= off;\n"
      "   float peak = max(c.r, max(c.g, c.b));\n"
      "   if (peak < start) return c;\n"
      "   float d = 1.0 - start;\n"
      "   float np = 1.0 - d * d / (peak + d - start);\n"
      "   c *= np / peak;\n"
      "   float g = 1.0 - 1.0 / (desat * (peak - np) + 1.0);\n"
      "   return mix(c, vec3(np), g);\n"
      "}\n"
      "vec3 bloomLevel(int i) {\n"
      "   if (i == 0) return texture(uBloom0, vUv).rgb;\n"
      "   if (i == 1) return texture(uBloom1, vUv).rgb;\n"
      "   if (i == 2) return texture(uBloom2, vUv).rgb;\n"
      "   if (i == 3) return texture(uBloom3, vUv).rgb;\n"
      "   if (i == 4) return texture(uBloom4, vUv).rgb;\n"
      "   return texture(uBloom5, vUv).rgb;\n"
      "}\n"
      "void main() {\n"
      "   vec4 src = texture(uSrc, vUv);\n"
      "   if (uPassthrough == 1) { fragColor = src; return; }\n"
      "   vec3 c = src.rgb;\n"
      "   vec3 glow = vec3(0.0);\n"
      "   if (uBloom > 0.0) {\n"
      "      vec3 b = vec3(0.0); float wsum = 0.0;\n"
      "      for (int i = 0; i < 6; i++) {\n"
      "         if (i >= uLevels) break;\n"
      "         float w = pow(max(uRadius, 0.05), float(i));\n"
      "         b += bloomLevel(i) * w; wsum += w;\n"
      "      }\n"
      "      glow = b / max(wsum, 1e-4) * uBloom;\n"
      "   }\n"
      "   bool bg = uBgKey == 1 && texture(uDepth, vUv).r >= 0.99999;\n"
      "   if (bg) c = glow;\n"
      "   else c += glow;\n"
      "   if (uTonemap == 1) c = aces(c);\n"
      "   else if (uTonemap == 2) c = c / (c + vec3(1.0));\n"
      "   else if (uTonemap == 3) c = agx(c);\n"
      "   else if (uTonemap == 4) c = neutral(c);\n"
      "   c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));\n"
      "   if (bg) c = 1.0 - (1.0 - uBg) * (1.0 - c);\n"
      "   float alpha = src.a;\n"
      "   if (uBgKey == 2) {\n" // path traced: alpha is coverage
      "      vec3 g = glow;\n"
      "      if (uTonemap == 1) g = aces(g); else if (uTonemap == 2) g = g / (g + vec3(1.0));\n"
      "      else if (uTonemap == 3) g = agx(g); else if (uTonemap == 4) g = neutral(g);\n"
      "      vec3 bgOut = 1.0 - (1.0 - uBg) * (1.0 - pow(clamp(g, 0.0, 1.0), vec3(1.0 / 2.2)));\n"
      "      c = mix(bgOut, c, clamp(src.a, 0.0, 1.0));\n"
      "      alpha = mix(uBgOpacity, 1.0, clamp(src.a, 0.0, 1.0));\n"
      "   } else if (uBgKey == 3) {\n" // path traced over its own environment
      "      alpha = mix(uBgOpacity, 1.0, clamp(src.a, 0.0, 1.0));\n"
      "   }\n"
      "   vec2 p = floor(gl_FragCoord.xy);\n"
      "   c += (fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;\n"
      "   fragColor = vec4(c, alpha);\n"
      "}\n";

   // Cube faces in camera space (-Z forward): forward, right, up. Face i is
   // rendered looking down `forward`; a direction d lands on it at
   // (dot(d, right), dot(d, up)) / dot(d, forward).
   const float kPanoFaces[6][9] = {
      { 1, 0, 0, 0, 0, 1, 0, 1, 0 },   // +X
      { -1, 0, 0, 0, 0, -1, 0, 1, 0 }, // -X
      { 0, 1, 0, 1, 0, 0, 0, 0, 1 },   // +Y
      { 0, -1, 0, 1, 0, 0, 0, 0, -1 }, // -Y
      { 0, 0, 1, -1, 0, 0, 0, 1, 0 },  // +Z (behind the camera)
      { 0, 0, -1, 1, 0, 0, 0, 1, 0 },  // -Z (where the camera looks)
   };

   const char* kPanoGlsl = R"GLSL(
const vec3 PANO_F[6] = vec3[](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));
const vec3 PANO_R[6] = vec3[](vec3(0,0,1), vec3(0,0,-1), vec3(1,0,0), vec3(1,0,0), vec3(-1,0,0), vec3(1,0,0));
const vec3 PANO_U[6] = vec3[](vec3(0,1,0), vec3(0,1,0), vec3(0,0,1), vec3(0,0,-1), vec3(0,1,0), vec3(0,1,0));
// 1 equirect: centre column looks down -Z, top row is +Y. 2 cubemap 3x2:
// top row +X -X +Y, bottom row -Y +Z -Z.
uniform float uDomeAngle; // mode 3: fisheye angle in degrees (180 = hemisphere)
uniform float uDomeTilt;  // mode 3: degrees the zenith leans toward the camera's forward
// 3 dome (domemaster fisheye): zenith = camera up at the centre, the camera's
// forward at the bottom edge, +X to the right; outside the circle -> vec3(0).
vec3 panoDir(vec2 uv, int mode) {
   if (mode == 3) {
      vec2 p = uv * 2.0 - 1.0;
      float r = length(p);
      if (r > 1.0) return vec3(0.0);
      float th = r * radians(uDomeAngle) * 0.5;
      vec2 a = r > 1e-6 ? p / r : vec2(0.0, -1.0);
      vec3 d = normalize(vec3(0.0, cos(th), 0.0) + vec3(a.x, 0.0, a.y) * sin(th));
      float ct = cos(radians(uDomeTilt)), st = sin(radians(uDomeTilt));
      return vec3(d.x, d.y * ct + d.z * st, d.z * ct - d.y * st);
   }
   if (mode == 1) {
      float phi = (uv.x - 0.5) * 6.28318530718, th = (1.0 - uv.y) * 3.14159265359;
      return vec3(sin(th) * sin(phi), cos(th), -sin(th) * cos(phi));
   }
   vec2 g = uv * vec2(3.0, 2.0);
   int cx = int(min(floor(g.x), 2.0)), cy = int(min(floor(g.y), 1.0));
   vec2 f = fract(g) * 2.0 - 1.0;
   int face = (1 - cy) * 3 + cx;
   return normalize(PANO_F[face] + f.x * PANO_R[face] + f.y * PANO_U[face]);
}
)GLSL";

   unsigned int MakeTex(int w, int h, unsigned int internalFormat)
   {
      unsigned int t = 0;
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, w, h);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      return t;
   }

   unsigned int MakeFbo(unsigned int tex)
   {
      unsigned int f = 0;
      glGenFramebuffers(1, &f);
      glBindFramebuffer(GL_FRAMEBUFFER, f);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
      return f;
   }

   struct SavedState
   {
      GLint fbo = 0, viewport[4] = {};
      GLboolean blend = GL_FALSE, depth = GL_FALSE, scissor = GL_FALSE, cull = GL_FALSE;
      SavedState()
      {
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
         glGetIntegerv(GL_VIEWPORT, viewport);
         blend = glIsEnabled(GL_BLEND);
         depth = glIsEnabled(GL_DEPTH_TEST);
         scissor = glIsEnabled(GL_SCISSOR_TEST);
         cull = glIsEnabled(GL_CULL_FACE);
         glDisable(GL_BLEND);
         glDisable(GL_DEPTH_TEST);
         glDisable(GL_SCISSOR_TEST);
         glDisable(GL_CULL_FACE);
         glActiveTexture(GL_TEXTURE0);
      }
      ~SavedState()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, 0);
         glUseProgram(0);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
         if (blend) glEnable(GL_BLEND);
         if (depth) glEnable(GL_DEPTH_TEST);
         if (scissor) glEnable(GL_SCISSOR_TEST);
         if (cull) glEnable(GL_CULL_FACE);
      }
   };
}

void Render3DPost::PanoFace(int i, float forward[3], float right[3], float up[3])
{
   i = std::clamp(i, 0, 5);
   for (int k = 0; k < 3; k++)
   {
      forward[k] = kPanoFaces[i][k];
      right[k] = kPanoFaces[i][3 + k];
      up[k] = kPanoFaces[i][6 + k];
   }
}

const char* Render3DPost::PanoGlsl()
{
   return kPanoGlsl;
}

unsigned int Render3DPost::Panorama(unsigned int faceArray, int w, int h, int mode, float domeAngle, float domeTilt)
{
   if (mPanoProgram == 0)
   {
      const std::string src = std::string("#version 330 core\n"
                                          "in vec2 vUv;\n"
                                          "out vec4 fragColor;\n"
                                          "uniform sampler2DArray uFaces;\n"
                                          "uniform int uMode;\n") +
                              kPanoGlsl +
                              "void main() {\n"
                              "   vec3 d = panoDir(vUv, uMode);\n"
                              "   if (dot(d, d) < 0.5) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
                              "   int best = 0; float bd = -2.0;\n"
                              "   for (int i = 0; i < 6; i++) { float k = dot(d, PANO_F[i]); if (k > bd) { bd = k; best = i; } }\n"
                              "   vec2 st = vec2(dot(d, PANO_R[best]), dot(d, PANO_U[best])) / max(bd, 1e-4);\n"
                              "   fragColor = texture(uFaces, vec3(clamp(st * 0.5 + 0.5, 0.0, 1.0), float(best)));\n"
                              "}\n";
      mPanoProgram = GLUtil::CompileProgram(src.c_str());
      if (mPanoProgram == 0)
         return 0;
   }
   if (mPanoTex == 0 || w != mPanoW || h != mPanoH)
   {
      if (mPanoTex != 0) glDeleteTextures(1, &mPanoTex);
      if (mPanoFbo != 0) glDeleteFramebuffers(1, &mPanoFbo);
      GLint prev = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
      mPanoTex = MakeTex(w, h, GL_RGBA8);
      mPanoFbo = MakeFbo(mPanoTex);
      glBindFramebuffer(GL_FRAMEBUFFER, prev);
      mPanoW = w;
      mPanoH = h;
   }
   SavedState saved;
   glBindFramebuffer(GL_FRAMEBUFFER, mPanoFbo);
   glViewport(0, 0, w, h);
   glUseProgram(mPanoProgram);
   glBindTexture(GL_TEXTURE_2D_ARRAY, faceArray);
   glUniform1i(glGetUniformLocation(mPanoProgram, "uFaces"), 0);
   glUniform1i(glGetUniformLocation(mPanoProgram, "uMode"), mode);
   glUniform1f(glGetUniformLocation(mPanoProgram, "uDomeAngle"), domeAngle);
   glUniform1f(glGetUniformLocation(mPanoProgram, "uDomeTilt"), domeTilt);
   DrawFullscreen();
   glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
   return mPanoTex;
}

Render3DPost::~Render3DPost()
{
   if (mPanoProgram != 0) glDeleteProgram(mPanoProgram);
   if (mPanoTex != 0) glDeleteTextures(1, &mPanoTex);
   if (mPanoFbo != 0) glDeleteFramebuffers(1, &mPanoFbo);
   unsigned int progs[] = { mPrefProgram, mIrrProgram, mLutProgram, mBrightProgram, mBlurProgram, mFinishProgram };
   for (unsigned int p : progs)
      if (p != 0)
         glDeleteProgram(p);
   unsigned int texs[] = { mPrefTex, mIrrTex, mLutTex, mOutTex };
   for (unsigned int t : texs)
      if (t != 0)
         glDeleteTextures(1, &t);
   if (mIblFbo != 0) glDeleteFramebuffers(1, &mIblFbo);
   if (mOutFbo != 0) glDeleteFramebuffers(1, &mOutFbo);
   for (int i = 0; i < kBloomLevels; i++)
      for (int j = 0; j < 2; j++)
      {
         if (mBloomTex[i][j] != 0) glDeleteTextures(1, &mBloomTex[i][j]);
         if (mBloomFbo[i][j] != 0) glDeleteFramebuffers(1, &mBloomFbo[i][j]);
      }
}

bool Render3DPost::EnsurePrograms()
{
   if (mFinishProgram != 0)
      return true;
   if (mProgramsFailed)
      return false;
   mPrefProgram = GLUtil::CompileProgram(PrefilterSrc().c_str());
   mIrrProgram = GLUtil::CompileProgram(IrradianceSrc().c_str());
   mLutProgram = GLUtil::CompileProgram(LutSrc().c_str());
   mBrightProgram = GLUtil::CompileProgram(kBrightSrc);
   mBlurProgram = GLUtil::CompileProgram(kBlurSrc);
   const unsigned int finish = GLUtil::CompileProgram(kFinishSrc);
   if (mPrefProgram == 0 || mIrrProgram == 0 || mLutProgram == 0 || mBrightProgram == 0 || mBlurProgram == 0 ||
       finish == 0)
   {
      fprintf(stderr, "Render3DPost: shader compile failed, HDR finish off\n");
      mProgramsFailed = true;
      if (finish != 0)
         glDeleteProgram(finish);
      return false;
   }
   mFinishProgram = finish;
   return true;
}

void Render3DPost::DrawFullscreen()
{
   GLUtil::DrawFullscreenQuad();
}

unsigned int Render3DPost::BrdfLut()
{
   if (mLutTex != 0 || !EnsurePrograms())
      return mLutTex;
   SavedState saved;
   mLutTex = MakeTex(128, 128, GL_RG16F);
   unsigned int fbo = MakeFbo(mLutTex);
   glViewport(0, 0, 128, 128);
   glUseProgram(mLutProgram);
   DrawFullscreen();
   glDeleteFramebuffers(1, &fbo);
   return mLutTex;
}

bool Render3DPost::EnsureIbl(unsigned int envTex, int envW, int envH, float envMaxLod, unsigned long long revision)
{
   if (envTex == 0 || envW <= 0 || envH <= 0 || !EnsurePrograms())
      return false;
   if (envTex == mIblSource && revision == mIblRevision && mPrefTex != 0)
      return true;
   BrdfLut();
   SavedState saved;

   if (mPrefTex != 0) glDeleteTextures(1, &mPrefTex);
   if (mIrrTex != 0) glDeleteTextures(1, &mIrrTex);
   if (mIblFbo == 0) glGenFramebuffers(1, &mIblFbo);

   // 512 x 256 base, one roughness per mip (0, 0.2 ... 1.0).
   const int pw = 512, ph = 256;
   mPrefLevels = 6;
   glGenTextures(1, &mPrefTex);
   glBindTexture(GL_TEXTURE_2D, mPrefTex);
   glTexStorage2D(GL_TEXTURE_2D, mPrefLevels, GL_RGBA16F, pw, ph);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mPrefLevels - 1);

   glUseProgram(mPrefProgram);
   glUniform1i(glGetUniformLocation(mPrefProgram, "uEnv"), 0);
   glUniform1f(glGetUniformLocation(mPrefProgram, "uSrcTexels"), (float)envW * (float)envH);
   glUniform1f(glGetUniformLocation(mPrefProgram, "uSrcMaxLod"), envMaxLod);
   glBindFramebuffer(GL_FRAMEBUFFER, mIblFbo);
   glBindTexture(GL_TEXTURE_2D, envTex);
   for (int level = 0; level < mPrefLevels; level++)
   {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mPrefTex, level);
      glViewport(0, 0, std::max(1, pw >> level), std::max(1, ph >> level));
      glUniform1f(glGetUniformLocation(mPrefProgram, "uRough"), (float)level / (float)(mPrefLevels - 1));
      DrawFullscreen();
   }

   mIrrTex = MakeTex(64, 32, GL_RGBA16F);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glBindFramebuffer(GL_FRAMEBUFFER, mIblFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mIrrTex, 0);
   glViewport(0, 0, 64, 32);
   glUseProgram(mIrrProgram);
   glBindTexture(GL_TEXTURE_2D, envTex);
   glUniform1i(glGetUniformLocation(mIrrProgram, "uEnv"), 0);
   glUniform1f(glGetUniformLocation(mIrrProgram, "uLod"), std::max(0.0f, envMaxLod - 4.0f));
   DrawFullscreen();

   mIblSource = envTex;
   mIblRevision = revision;
   return true;
}

void Render3DPost::EnsureTargets(int w, int h)
{
   if (mOutTex != 0 && w == mWidth && h == mHeight)
      return;
   if (mOutTex != 0) glDeleteTextures(1, &mOutTex);
   if (mOutFbo != 0) glDeleteFramebuffers(1, &mOutFbo);
   for (int i = 0; i < kBloomLevels; i++)
      for (int j = 0; j < 2; j++)
      {
         if (mBloomTex[i][j] != 0) { glDeleteTextures(1, &mBloomTex[i][j]); mBloomTex[i][j] = 0; }
         if (mBloomFbo[i][j] != 0) { glDeleteFramebuffers(1, &mBloomFbo[i][j]); mBloomFbo[i][j] = 0; }
      }
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   mWidth = w;
   mHeight = h;
   mOutTex = MakeTex(w, h, GL_RGBA8);
   mOutFbo = MakeFbo(mOutTex);
   int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
   for (int i = 0; i < kBloomLevels; i++)
   {
      mBloomW[i] = bw;
      mBloomH[i] = bh;
      for (int j = 0; j < 2; j++)
      {
         mBloomTex[i][j] = MakeTex(bw, bh, GL_R11F_G11F_B10F);
         mBloomFbo[i][j] = MakeFbo(mBloomTex[i][j]);
      }
      bw = std::max(1, bw / 2);
      bh = std::max(1, bh / 2);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int Render3DPost::Finish(unsigned int src, int w, int h, int tonemap, float bloom, float bloomThreshold,
                                  float bloomRadius, bool passthrough, unsigned int depthTex, const float bg[3])
{
   return Run(src, w, h, tonemap, bloom, bloomThreshold, bloomRadius, passthrough, depthTex, bg,
              depthTex != 0 ? 1 : 0, 1.0f);
}

unsigned int Render3DPost::FinishCoverage(unsigned int src, int w, int h, int tonemap, float bloom,
                                          float bloomThreshold, float bloomRadius, const float bg[3], float bgOpacity,
                                          bool envBackground)
{
   return Run(src, w, h, tonemap, bloom, bloomThreshold, bloomRadius, false, 0, bg, envBackground ? 3 : 2, bgOpacity);
}

unsigned int Render3DPost::Run(unsigned int src, int w, int h, int tonemap, float bloom, float bloomThreshold,
                               float bloomRadius, bool passthrough, unsigned int depthTex, const float bg[3],
                               int keyMode, float bgOpacity)
{
   if (!EnsurePrograms())
      return 0;
   EnsureTargets(w, h);
   SavedState saved;

   const bool doBloom = !passthrough && bloom > 0.0f;
   int levels = 0;
   if (doBloom)
   {
      // Bright pass into level 0, then downsample + blur down the chain.
      glUseProgram(mBrightProgram);
      glUniform1i(glGetUniformLocation(mBrightProgram, "uSrc"), 0);
      glUniform1f(glGetUniformLocation(mBrightProgram, "uThreshold"), std::max(0.0f, bloomThreshold));
      glBindFramebuffer(GL_FRAMEBUFFER, mBloomFbo[0][0]);
      glViewport(0, 0, mBloomW[0], mBloomH[0]);
      glBindTexture(GL_TEXTURE_2D, src);
      DrawFullscreen();
      glUseProgram(mBlurProgram);
      glUniform1i(glGetUniformLocation(mBlurProgram, "uSrc"), 0);
      const int stepLoc = glGetUniformLocation(mBlurProgram, "uStep");
      for (int i = 0; i < kBloomLevels; i++)
      {
         if (mBloomW[i] < 4 || mBloomH[i] < 4)
            break;
         glViewport(0, 0, mBloomW[i], mBloomH[i]);
         if (i > 0)
         {
            // Downsample the previous level's result into this one (bilinear).
            glBindFramebuffer(GL_FRAMEBUFFER, mBloomFbo[i][0]);
            glUniform2f(stepLoc, 0.0f, 0.0f);
            glBindTexture(GL_TEXTURE_2D, mBloomTex[i - 1][0]);
            DrawFullscreen();
         }
         glBindFramebuffer(GL_FRAMEBUFFER, mBloomFbo[i][1]);
         glUniform2f(stepLoc, 1.0f / (float)mBloomW[i], 0.0f);
         glBindTexture(GL_TEXTURE_2D, mBloomTex[i][0]);
         DrawFullscreen();
         glBindFramebuffer(GL_FRAMEBUFFER, mBloomFbo[i][0]);
         glUniform2f(stepLoc, 0.0f, 1.0f / (float)mBloomH[i]);
         glBindTexture(GL_TEXTURE_2D, mBloomTex[i][1]);
         DrawFullscreen();
         levels = i + 1;
      }
   }

   glBindFramebuffer(GL_FRAMEBUFFER, mOutFbo);
   glViewport(0, 0, w, h);
   glUseProgram(mFinishProgram);
   glBindTexture(GL_TEXTURE_2D, src);
   glUniform1i(glGetUniformLocation(mFinishProgram, "uSrc"), 0);
   for (int i = 0; i < kBloomLevels; i++)
   {
      glActiveTexture(GL_TEXTURE1 + i);
      glBindTexture(GL_TEXTURE_2D, mBloomTex[i][0]);
      char name[16];
      snprintf(name, sizeof(name), "uBloom%d", i);
      glUniform1i(glGetUniformLocation(mFinishProgram, name), 1 + i);
   }
   glActiveTexture(GL_TEXTURE7);
   glBindTexture(GL_TEXTURE_2D, depthTex);
   glUniform1i(glGetUniformLocation(mFinishProgram, "uDepth"), 7);
   glUniform1i(glGetUniformLocation(mFinishProgram, "uBgKey"), keyMode);
   glUniform1f(glGetUniformLocation(mFinishProgram, "uBgOpacity"), bgOpacity);
   glUniform3f(glGetUniformLocation(mFinishProgram, "uBg"), bg[0], bg[1], bg[2]);
   glActiveTexture(GL_TEXTURE0);
   glUniform1i(glGetUniformLocation(mFinishProgram, "uLevels"), levels);
   glUniform1f(glGetUniformLocation(mFinishProgram, "uBloom"), doBloom ? bloom : 0.0f);
   glUniform1f(glGetUniformLocation(mFinishProgram, "uRadius"), std::clamp(bloomRadius, 0.0f, 1.0f));
   glUniform1i(glGetUniformLocation(mFinishProgram, "uTonemap"), tonemap);
   glUniform1i(glGetUniformLocation(mFinishProgram, "uPassthrough"), passthrough ? 1 : 0);
   DrawFullscreen();

   for (int i = 0; i < kBloomLevels + 1; i++)
   {
      glActiveTexture(GL_TEXTURE1 + i);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   return mOutTex;
}

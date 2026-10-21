#include "VolumeNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>

#include "Geometry3DNodes.h"
#include "Transport.h"

namespace
{
   const std::vector<std::string> kShapeNames = { "Fog", "Smoke", "Cloud" };

   // One pass over the whole picture. Each pixel's ray starts at the camera and
   // stops at the first solid surface (from the depth buffer) or the maximum
   // distance. The ray start is jittered by a fixed per-pixel pattern, so a low
   // step count shows fine grain rather than bands, and the same frame always
   // gives the same picture.
   const char* kVolumeFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uDepth;\n"
      "uniform int uHasSrc;\n"
      "uniform int uHasDepth;\n"
      "uniform vec3 uEye, uRight, uUp, uFwd;\n"
      "uniform float uTanY, uAspectX, uOrthoHalf, uNear, uFar;\n"
      "uniform int uOrtho;\n"
      "uniform int uShape;\n"
      "uniform float uDensity, uSize, uHeight, uNoiseScale, uNoiseAmount, uSeed, uTime;\n"
      "uniform vec3 uCenter, uWind, uColor, uLightColor, uLightDir;\n"
      "uniform float uLightIntensity, uAmbient, uScatter, uG, uMaxDist;\n"
      "uniform int uSteps, uLightSteps;\n"
      "float hash(vec3 p) {\n"
      "   p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));\n"
      "   p *= 17.0;\n"
      "   return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"
      "}\n"
      "float vnoise(vec3 x) {\n"
      "   vec3 i = floor(x), f = fract(x);\n"
      "   f = f * f * (3.0 - 2.0 * f);\n"
      "   return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),\n"
      "              mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);\n"
      "}\n"
      "float fbm(vec3 p) {\n"
      "   float a = 0.5, s = 0.0;\n"
      "   for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.02 + vec3(5.2, 1.3, 3.7); a *= 0.5; }\n"
      "   return s / 0.9375;\n"
      "}\n"
      "float density(vec3 p) {\n"
      "   vec3 q = p - uCenter;\n"
      "   vec3 np = p * uNoiseScale - uWind * uTime + vec3(uSeed * 37.1, uSeed * 11.3, uSeed * 23.7);\n"
      "   float base;\n"
      "   if (uShape == 0) {\n"
      "      base = exp(-max(q.y, 0.0) / max(uSize, 0.01)) * smoothstep(-max(uHeight, 0.01), 0.0, q.y);\n"
      "   } else if (uShape == 1) {\n"
      "      float t = clamp(q.y / max(uHeight, 0.01), 0.0, 1.0);\n"
      "      float inside = q.y >= 0.0 ? 1.0 : smoothstep(-0.2 * uSize, 0.0, q.y);\n"
      "      float radius = max(uSize, 0.01) * (0.35 + 0.9 * t);\n"
      "      vec2 off = (vec2(vnoise(np * 0.7 + 3.0), vnoise(np * 0.7 + 9.0)) - 0.5) * uSize * 1.5 * t;\n"
      "      float r = length(q.xz + off);\n"
      "      base = exp(-(r * r) / (radius * radius)) * (1.0 - t * t) * inside;\n"
      "   } else {\n"
      "      float r = length(q / vec3(1.0, 0.65, 1.0));\n"
      "      base = smoothstep(uSize, uSize * 0.3, r);\n"
      "   }\n"
      "   if (base <= 0.0005) return 0.0;\n"
      "   float lump = smoothstep(0.25, 0.75, fbm(np));\n"
      "   return base * mix(1.0, lump * 1.4, uNoiseAmount) * uDensity;\n"
      "}\n"
      "float phase(float c) {\n"
      "   float g2 = uG * uG;\n"
      "   return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * uG * c, 1e-3), 1.5));\n"
      "}\n"
      "void main() {\n"
      "   vec4 src = uHasSrc != 0 ? texture(uSrc, vUv) : vec4(0.0, 0.0, 0.0, 1.0);\n"
      "   vec2 ndc = vUv * 2.0 - 1.0;\n"
      "   vec3 ro, dir;\n"
      "   if (uOrtho != 0) {\n"
      "      ro = uEye + uRight * ndc.x * uOrthoHalf * uAspectX + uUp * ndc.y * uOrthoHalf;\n"
      "      dir = uFwd;\n"
      "   } else {\n"
      "      ro = uEye;\n"
      "      dir = uFwd + uRight * ndc.x * uTanY * uAspectX + uUp * ndc.y * uTanY;\n"
      "   }\n"
      "   float dlen = length(dir);\n"
      "   vec3 rd = dir / dlen;\n"
      "   float tEnd = uMaxDist;\n"
      "   if (uHasDepth != 0) {\n"
      "      float d = texture(uDepth, vUv).r;\n"
      "      if (d < 0.99999) {\n"
      "         float zn = d * 2.0 - 1.0;\n"
      "         float zv = uOrtho != 0 ? uNear + d * (uFar - uNear) : 2.0 * uNear * uFar / (uFar + uNear - zn * (uFar - uNear));\n"
      "         tEnd = min(tEnd, uOrtho != 0 ? zv : zv * dlen);\n"
      "      }\n"
      "   }\n"
      "   float t0 = 0.0;\n"
      "   float dt = (tEnd - t0) / float(uSteps);\n"
      "   float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
      "   float t = t0 + dt * jit;\n"
      "   float T = 1.0;\n"
      "   vec3 L = vec3(0.0);\n"
      "   float cosT = dot(rd, uLightDir);\n"
      "   float ph = phase(cosT);\n"
      "   for (int i = 0; i < uSteps; i++) {\n"
      "      vec3 p = ro + rd * t;\n"
      "      float d = density(p);\n"
      "      if (d > 0.002) {\n"
      "         float stepT = exp(-d * dt);\n"
      "         float lightT = 1.0;\n"
      "         if (uLightSteps > 0) {\n"
      "            float ldt = max(uSize, 0.3) * 1.5 / float(uLightSteps);\n"
      "            float od = 0.0;\n"
      "            for (int k = 1; k <= uLightSteps; k++) od += density(p + uLightDir * ldt * (float(k) - 0.5)) * ldt;\n"
      "            lightT = exp(-od);\n"
      "         }\n"
      "         vec3 inscatter = (uLightColor * uLightIntensity * lightT * ph * 4.0 * 3.14159265 * 0.25 + vec3(uAmbient)) * uColor * uScatter;\n"
      "         L += T * (1.0 - stepT) * inscatter;\n"
      "         T *= stepT;\n"
      "         if (T < 0.01) break;\n"
      "      }\n"
      "      t += dt;\n"
      "   }\n"
      "   fragColor = vec4(src.rgb * T + L, 1.0 - T * (1.0 - src.a));\n"
      "}\n";

   void Bind(unsigned int prog, int unit, unsigned int tex, const char* name)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(prog, name), unit);
   }
   void F1(unsigned int prog, const char* name, float v) { glUniform1f(glGetUniformLocation(prog, name), v); }
   void I1(unsigned int prog, const char* name, int v) { glUniform1i(glGetUniformLocation(prog, name), v); }
   void F3(unsigned int prog, const char* name, float a, float b, float c) { glUniform3f(glGetUniformLocation(prog, name), a, b, c); }
}

const std::vector<std::string>& VolumeNode::ShapeNames() { return kShapeNames; }

VolumeNode::~VolumeNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProg != 0)
      glDeleteProgram(mProg);
}

bool VolumeNode::EnsureShader()
{
   if (mShaderTried)
      return mProg != 0;
   mShaderTried = true;
   mProg = GLUtil::CompileProgram(kVolumeFrag);
   return mProg != 0;
}

void VolumeNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const unsigned int srcTex = mInput.Pull(frameId);
   if (!EnsureShader())
      return;

   const bool hasSrc = srcTex != 0;
   const int w = hasSrc ? std::max(16, mInput.Width()) : 1024;
   const int h = hasSrc ? std::max(16, mInput.Height()) : 576;
   if (!GLUtil::EnsureFbo(mOut, w, h, GL_RGBA16F, "Volume"))
      return;

   // Camera: the Render 3D that made the picture, or this node's own.
   const float aspect = (float)w / (float)h;
   Mat4 view, proj;
   float eye[3] = { 0, 0, 0 };
   unsigned int depthTex = 0;
   mUsedScene = false;
   if (auto* render = dynamic_cast<Render3DNode*>(mInput.GetSource()))
   {
      render->RequestDepth();
      const Render3DNode::SceneCamera& cam = render->LastCamera();
      if (cam.valid)
      {
         view = cam.view;
         proj = cam.proj;
         eye[0] = cam.eye[0]; eye[1] = cam.eye[1]; eye[2] = cam.eye[2];
         mUsedScene = true;
         if (render->DepthValid())
            depthTex = render->DepthTexture();
      }
   }
   if (!mUsedScene)
   {
      const float az = camAzimuth * 3.14159265f / 180.0f, el = camElevation * 3.14159265f / 180.0f;
      const float ce = std::cos(el);
      eye[0] = targetX + camDistance * ce * std::cos(az);
      eye[1] = targetY + camDistance * std::sin(el);
      eye[2] = targetZ + camDistance * ce * std::sin(az);
      const float target[3] = { targetX, targetY, targetZ };
      const float up[3] = { 0.0f, 1.0f, 0.0f };
      view = Mat4::LookAt(eye, target, up);
      proj = Mat4::Perspective(std::clamp(fov, 5.0f, 150.0f) * 3.14159265f / 180.0f, aspect, 0.05f, 100.0f);
   }
   const bool ortho = proj.m[11] == 0.0f;
   float nearZ, farZ;
   if (ortho)
   {
      const float range = -2.0f / proj.m[10];
      const float sum = 2.0f * proj.m[14] / proj.m[10];
      nearZ = (sum - range) * 0.5f;
      farZ = nearZ + range;
   }
   else
   {
      nearZ = proj.m[14] / (proj.m[10] - 1.0f);
      farZ = proj.m[14] / (proj.m[10] + 1.0f);
   }

   const float tanY = ortho ? 0.0f : 1.0f / proj.m[5];
   const float orthoHalf = ortho ? 1.0f / proj.m[5] : 0.0f;
   const float aspectX = proj.m[5] / proj.m[0];

   // Light direction points toward the light, same angles as Render 3D's own light.
   const float la = lightAzimuth * 3.14159265f / 180.0f, le = lightElevation * 3.14159265f / 180.0f;
   const float lx = std::cos(le) * std::cos(la), ly = std::sin(le), lz = std::cos(le) * std::sin(la);

   Transport& tr = Transport::Instance();
   const float time = (float)(tr.Beats() * 60.0 / (double)std::max(1.0f, tr.Tempo())) * speed;
   const int stepCount = std::clamp(tr.IsOfflineMode() ? renderSteps : steps, 4, 512);

   GLUtil::RunShaderPass(mOut, mProg, [&]()
   {
      Bind(mProg, 0, srcTex, "uSrc");
      Bind(mProg, 1, depthTex, "uDepth");
      I1(mProg, "uHasSrc", hasSrc ? 1 : 0);
      I1(mProg, "uHasDepth", depthTex != 0 ? 1 : 0);
      F3(mProg, "uEye", eye[0], eye[1], eye[2]);
      F3(mProg, "uRight", view.m[0], view.m[4], view.m[8]);
      F3(mProg, "uUp", view.m[1], view.m[5], view.m[9]);
      F3(mProg, "uFwd", -view.m[2], -view.m[6], -view.m[10]);
      F1(mProg, "uTanY", tanY);
      F1(mProg, "uAspectX", aspectX);
      F1(mProg, "uOrthoHalf", orthoHalf);
      F1(mProg, "uNear", nearZ);
      F1(mProg, "uFar", farZ);
      I1(mProg, "uOrtho", ortho ? 1 : 0);
      I1(mProg, "uShape", std::clamp(shape, 0, (int)kShapeCount - 1));
      F1(mProg, "uDensity", std::max(0.0f, density));
      F1(mProg, "uSize", std::max(0.01f, size));
      F1(mProg, "uHeight", std::max(0.01f, height));
      F3(mProg, "uCenter", centerX, centerY, centerZ);
      F1(mProg, "uNoiseScale", std::max(0.01f, noiseScale));
      F1(mProg, "uNoiseAmount", std::clamp(noiseAmount, 0.0f, 1.0f));
      F1(mProg, "uSeed", seed);
      F1(mProg, "uTime", time);
      F3(mProg, "uWind", windX, windY, windZ);
      F3(mProg, "uColor", color[0], color[1], color[2]);
      F3(mProg, "uLightColor", lightColor[0], lightColor[1], lightColor[2]);
      F3(mProg, "uLightDir", lx, ly, lz);
      F1(mProg, "uLightIntensity", std::max(0.0f, lightIntensity));
      F1(mProg, "uAmbient", std::max(0.0f, ambient));
      F1(mProg, "uScatter", std::clamp(scatter, 0.0f, 1.0f));
      F1(mProg, "uG", std::clamp(anisotropy, -0.95f, 0.95f));
      F1(mProg, "uMaxDist", std::max(0.1f, maxDistance));
      I1(mProg, "uSteps", stepCount);
      I1(mProg, "uLightSteps", std::clamp(lightSteps, 0, 16));
   });
}

#include "RampNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

namespace
{
   const std::vector<std::string> kTypeNames = {
      "Linear", "Radial", "Angular", "Diamond", "Spiral"
   };
   const std::vector<std::string> kRepeatNames = { "Clamp", "Repeat", "Mirror" };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform int uType;\n"
      "uniform int uRepeat;\n"
      "uniform float uAngle;\n"
      "uniform vec2 uCenter;\n"
      "uniform float uScale;\n"
      "uniform float uOffset;\n"
      "uniform float uGamma;\n"
      "uniform float uDither;\n"
      "uniform int uStopCount;\n"
      "uniform float uStopPos[5];\n"
      "uniform vec3 uStopColor[5];\n"
      "uniform float uAspect;\n"
      "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
      "void main() {\n"
      "   vec2 p = vUv - uCenter;\n"
      "   p.x *= uAspect;\n"
      "   float s = sin(-uAngle), c = cos(-uAngle);\n"
      "   p = vec2(c*p.x - s*p.y, s*p.x + c*p.y);\n"
      "\n"
      "   float t;\n"
      "   if (uType == 0) t = p.x + 0.5;\n"
      "   else if (uType == 1) t = length(p) * 2.0;\n"
      "   else if (uType == 2) t = (atan(p.y, p.x) + 3.14159265) / 6.28318530;\n"
      "   else if (uType == 3) t = (abs(p.x) + abs(p.y)) * 2.0;\n"
      "   else t = fract((atan(p.y, p.x) + 3.14159265) / 6.28318530 + length(p) * 2.0);\n"
      "\n"
      "   t = (t - uOffset) * max(uScale, 1e-4);\n"
      "   if (uRepeat == 1) t = fract(t);\n"
      "   else if (uRepeat == 2) { t = fract(t * 0.5) * 2.0; t = t > 1.0 ? 2.0 - t : t; }\n"
      "   else t = clamp(t, 0.0, 1.0);\n"
      "   t = pow(clamp(t, 0.0, 1.0), max(uGamma, 1e-3));\n"
      "\n"
      "   // walk the stops; they are kept sorted by the UI\n"
      "   int count = max(2, uStopCount);\n"
      "   vec3 col = uStopColor[0];\n"
      "   if (t <= uStopPos[0]) col = uStopColor[0];\n"
      "   else if (t >= uStopPos[count - 1]) col = uStopColor[count - 1];\n"
      "   else {\n"
      "      for (int i = 0; i < 4; i++) {\n"
      "         if (i + 1 >= count) break;\n"
      "         if (t >= uStopPos[i] && t <= uStopPos[i + 1]) {\n"
      "            float span = max(uStopPos[i + 1] - uStopPos[i], 1e-5);\n"
      "            col = mix(uStopColor[i], uStopColor[i + 1], (t - uStopPos[i]) / span);\n"
      "            break;\n"
      "         }\n"
      "      }\n"
      "   }\n"
      "\n"
      "   // a touch of noise breaks up banding in wide, shallow gradients\n"
      "   if (uDither > 0.0) col += (hash(vUv * 1024.0) - 0.5) * uDither * 0.02;\n"
      "   fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);\n"
      "}\n";
}

const std::vector<std::string>& RampNode::TypeNames() { return kTypeNames; }
const std::vector<std::string>& RampNode::RepeatNames() { return kRepeatNames; }

RampNode::~RampNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool RampNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void RampNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShader())
      return;
   const int w = std::max(4, (int)width);
   const int h = std::max(4, (int)height);
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   // Sort a copy of the stops so the shader can assume increasing positions
   // without the UI having to police slider order.
   int order[kStops] = { 0, 1, 2, 3, 4 };
   const int count = std::max(2, std::min(stopCount, kStops));
   for (int i = 0; i < count; i++)
      for (int j = i + 1; j < count; j++)
         if (stopPos[order[j]] < stopPos[order[i]])
            std::swap(order[i], order[j]);

   float pos[kStops];
   float col[kStops * 3];
   for (int i = 0; i < kStops; i++)
   {
      const int src = (i < count) ? order[i] : order[count - 1];
      pos[i] = stopPos[src];
      col[i * 3 + 0] = stopColor[src][0];
      col[i * 3 + 1] = stopColor[src][1];
      col[i * 3 + 2] = stopColor[src][2];
   }

   GLUtil::RunShaderPass(mOut, mProgram, [&]()
   {
      glUniform1i(glGetUniformLocation(mProgram, "uType"), type);
      glUniform1i(glGetUniformLocation(mProgram, "uRepeat"), repeat);
      glUniform1f(glGetUniformLocation(mProgram, "uAngle"), angle * (float)M_PI / 180.0f);
      glUniform2f(glGetUniformLocation(mProgram, "uCenter"), centerX, centerY);
      glUniform1f(glGetUniformLocation(mProgram, "uScale"), scale);
      glUniform1f(glGetUniformLocation(mProgram, "uOffset"), offset);
      glUniform1f(glGetUniformLocation(mProgram, "uGamma"), gamma);
      glUniform1f(glGetUniformLocation(mProgram, "uDither"), dither);
      glUniform1i(glGetUniformLocation(mProgram, "uStopCount"), count);
      glUniform1fv(glGetUniformLocation(mProgram, "uStopPos"), kStops, pos);
      glUniform3fv(glGetUniformLocation(mProgram, "uStopColor"), kStops, col);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);
   });
}

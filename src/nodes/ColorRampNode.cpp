#include "ColorRampNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

namespace
{
   const std::vector<std::string> kInterpNames = { "Linear", "Constant" };
   const int kLutSize = 256;

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uLut;\n"
      "uniform float uMix;\n"
      "void main() {\n"
      "   vec4 c = texture(uSrc, vUv);\n"
      "   float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
      "   vec3 ramped = texture(uLut, vec2(clamp(lum, 0.0, 1.0), 0.5)).rgb;\n"
      "   fragColor = vec4(mix(c.rgb, ramped, uMix), c.a);\n"
      "}\n";
}

const std::vector<std::string>& ColorRampNode::InterpNames() { return kInterpNames; }

ColorRampNode::ColorRampNode() {}

ColorRampNode::~ColorRampNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mLutTex != 0)
      glDeleteTextures(1, &mLutTex);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

int ColorRampNode::AddStop(float x, const float rgb[3])
{
   if (stopCount >= kMaxStops)
      return -1;
   const int index = stopCount++;
   stopPos[index] = std::min(1.0f, std::max(0.0f, x));
   stopColor[index][0] = rgb[0];
   stopColor[index][1] = rgb[1];
   stopColor[index][2] = rgb[2];
   mLutDirty = true;
   return index;
}

void ColorRampNode::RemoveStop(int index)
{
   if (index < 0 || index >= stopCount || stopCount <= 2)
      return;
   const int last = stopCount - 1;
   if (index != last)
   {
      stopPos[index] = stopPos[last];
      stopColor[index][0] = stopColor[last][0];
      stopColor[index][1] = stopColor[last][1];
      stopColor[index][2] = stopColor[last][2];
   }
   stopCount--;
   mLutDirty = true;
}

void ColorRampNode::MoveStop(int index, float x)
{
   if (index < 0 || index >= stopCount)
      return;
   stopPos[index] = std::min(1.0f, std::max(0.0f, x));
   mLutDirty = true;
}

void ColorRampNode::SortedOrder(int order[kMaxStops], int count) const
{
   for (int i = 0; i < count; i++)
      order[i] = i;
   for (int i = 0; i < count; i++)
      for (int j = i + 1; j < count; j++)
         if (stopPos[order[j]] < stopPos[order[i]])
            std::swap(order[i], order[j]);
}

void ColorRampNode::Evaluate(float t, float outRgb[3]) const
{
   const int count = std::max(1, std::min(stopCount, kMaxStops));
   int order[kMaxStops];
   SortedOrder(order, count);

   t = std::min(1.0f, std::max(0.0f, t));

   auto copyStop = [&](int slot) {
      outRgb[0] = stopColor[slot][0];
      outRgb[1] = stopColor[slot][1];
      outRgb[2] = stopColor[slot][2];
   };

   if (count == 1 || t <= stopPos[order[0]])
   {
      copyStop(order[0]);
      return;
   }
   if (t >= stopPos[order[count - 1]])
   {
      copyStop(order[count - 1]);
      return;
   }

   for (int i = 0; i < count - 1; i++)
   {
      const int a = order[i];
      const int b = order[i + 1];
      if (t >= stopPos[a] && t <= stopPos[b])
      {
         if (interpMode == ColorRampNode::kConstant)
         {
            copyStop(a);
         }
         else
         {
            const float span = std::max(1e-5f, stopPos[b] - stopPos[a]);
            const float f = (t - stopPos[a]) / span;
            outRgb[0] = stopColor[a][0] + (stopColor[b][0] - stopColor[a][0]) * f;
            outRgb[1] = stopColor[a][1] + (stopColor[b][1] - stopColor[a][1]) * f;
            outRgb[2] = stopColor[a][2] + (stopColor[b][2] - stopColor[a][2]) * f;
         }
         return;
      }
   }
   copyStop(order[count - 1]);
}

void ColorRampNode::RebuildLut()
{
   std::vector<unsigned char> lut(kLutSize * 3, 0);
   for (int i = 0; i < kLutSize; i++)
   {
      const float t = (float)i / (float)(kLutSize - 1);
      float rgb[3];
      Evaluate(t, rgb);
      lut[i * 3 + 0] = (unsigned char)(std::min(1.0f, std::max(0.0f, rgb[0])) * 255.0f + 0.5f);
      lut[i * 3 + 1] = (unsigned char)(std::min(1.0f, std::max(0.0f, rgb[1])) * 255.0f + 0.5f);
      lut[i * 3 + 2] = (unsigned char)(std::min(1.0f, std::max(0.0f, rgb[2])) * 255.0f + 0.5f);
   }

   if (mLutTex == 0)
      glGenTextures(1, &mLutTex);
   glBindTexture(GL_TEXTURE_2D, mLutTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kLutSize, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, lut.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mLutDirty = false;
}

bool ColorRampNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void ColorRampNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }
   if (!EnsureShader())
      return;
   if (mLutDirty || mLutTex == 0)
      RebuildLut();
   if (!GLUtil::EnsureFbo(mOut, mInput.Width(), mInput.Height()))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, srcTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, srcTex);
      glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mLutTex);
      glUniform1i(glGetUniformLocation(mProgram, "uLut"), 1);
      glUniform1f(glGetUniformLocation(mProgram, "uMix"), mix);
   });
}

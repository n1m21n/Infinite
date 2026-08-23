#include "CurvesNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
   const std::vector<std::string> kChannelNames = { "RGB", "Red", "Green", "Blue" };
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
      "   // row 0 is the master curve, rows 1-3 are per-channel\n"
      "   float r = texture(uLut, vec2(c.r, 0.125)).r;\n"
      "   float g = texture(uLut, vec2(c.g, 0.375)).r;\n"
      "   float b = texture(uLut, vec2(c.b, 0.625)).r;\n"
      "   r = texture(uLut, vec2(r, 0.875)).r;\n"
      "   g = texture(uLut, vec2(g, 0.875)).r;\n"
      "   b = texture(uLut, vec2(b, 0.875)).r;\n"
      "   fragColor = vec4(mix(c.rgb, vec3(r, g, b), uMix), c.a);\n"
      "}\n";
}

const std::vector<std::string>& CurvesNode::ChannelNames()
{
   return kChannelNames;
}

CurvesNode::CurvesNode()
{
   for (int c = 0; c < kChannelCount; c++)
      ResetChannel(c);
}

CurvesNode::~CurvesNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mLutTex != 0)
      glDeleteTextures(1, &mLutTex);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

void CurvesNode::ResetChannel(int channel)
{
   if (channel < 0 || channel >= kChannelCount)
      return;
   mShapes[channel].Reset();
   mLutDirty = true;
}

int CurvesNode::AddPoint(int channel, float x, float y)
{
   if (channel < 0 || channel >= kChannelCount)
      return -1;
   int index = mShapes[channel].AddPoint(x, y);
   mLutDirty = true;
   return index;
}

void CurvesNode::MovePoint(int channel, int index, float x, float y)
{
   if (channel < 0 || channel >= kChannelCount)
      return;
   mShapes[channel].MovePoint(index, x, y);
   mLutDirty = true;
}

void CurvesNode::RemovePoint(int channel, int index)
{
   if (channel < 0 || channel >= kChannelCount)
      return;
   mShapes[channel].RemovePoint(index);
   mLutDirty = true;
}

float CurvesNode::Evaluate(int channel, float x) const
{
   if (channel < 0 || channel >= kChannelCount)
      return x;
   return mShapes[channel].Evaluate(x);
}

void CurvesNode::RebuildLut()
{
   // one row per channel: R, G, B, then the master curve applied last
   std::vector<unsigned char> lut(kLutSize * 4, 0);
   const int rowChannel[4] = { kRed, kGreen, kBlue, kRGB };
   for (int row = 0; row < 4; row++)
   {
      for (int i = 0; i < kLutSize; i++)
      {
         const float x = (float)i / (float)(kLutSize - 1);
         const float y = Evaluate(rowChannel[row], x);
         lut[row * kLutSize + i] = (unsigned char)(std::min(1.0f, std::max(0.0f, y)) * 255.0f + 0.5f);
      }
   }

   if (mLutTex == 0)
      glGenTextures(1, &mLutTex);
   glBindTexture(GL_TEXTURE_2D, mLutTex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kLutSize, 4, 0, GL_RED, GL_UNSIGNED_BYTE, lut.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   mLutDirty = false;
}

bool CurvesNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void CurvesNode::CookIfNeeded(int frameId)
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

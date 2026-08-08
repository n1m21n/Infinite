#include "CurvesNode.h"

#include <OpenGL/gl3.h>
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

std::string CurvesNode::EncodePoints(const std::vector<Point>& pts)
{
   std::string out;
   for (size_t i = 0; i < pts.size(); i++)
   {
      if (i > 0)
         out += ";";
      char buf[48];
      snprintf(buf, sizeof(buf), "%.6g,%.6g", (double)pts[i].x, (double)pts[i].y);
      out += buf;
   }
   return out;
}

std::vector<CurvesNode::Point> CurvesNode::DecodePoints(const std::string& s)
{
   std::vector<Point> pts;
   size_t start = 0;
   while (start < s.size())
   {
      size_t sep = s.find(';', start);
      std::string term = s.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
      const size_t comma = term.find(',');
      if (comma != std::string::npos)
      {
         Point p;
         p.x = (float)atof(term.substr(0, comma).c_str());
         p.y = (float)atof(term.substr(comma + 1).c_str());
         pts.push_back(p);
      }
      if (sep == std::string::npos)
         break;
      start = sep + 1;
   }
   return pts;
}

void CurvesNode::ResetChannel(int channel)
{
   if (channel < 0 || channel >= kChannelCount)
      return;
   mPoints[channel].clear();
   mPoints[channel].push_back({ 0.0f, 0.0f });
   mPoints[channel].push_back({ 1.0f, 1.0f });
   mLutDirty = true;
}

int CurvesNode::AddPoint(int channel, float x, float y)
{
   if (channel < 0 || channel >= kChannelCount)
      return -1;
   x = std::min(1.0f, std::max(0.0f, x));
   y = std::min(1.0f, std::max(0.0f, y));

   std::vector<Point>& pts = mPoints[channel];
   size_t insertAt = pts.size();
   for (size_t i = 0; i < pts.size(); i++)
   {
      if (pts[i].x > x)
      {
         insertAt = i;
         break;
      }
   }
   pts.insert(pts.begin() + insertAt, { x, y });
   mLutDirty = true;
   return (int)insertAt;
}

void CurvesNode::MovePoint(int channel, int index, float x, float y)
{
   if (channel < 0 || channel >= kChannelCount)
      return;
   std::vector<Point>& pts = mPoints[channel];
   if (index < 0 || index >= (int)pts.size())
      return;

   y = std::min(1.0f, std::max(0.0f, y));

   if (index == 0)
      x = 0.0f; // endpoints stay pinned to the edges
   else if (index == (int)pts.size() - 1)
      x = 1.0f;
   else
   {
      // keep the ordering intact so evaluation stays monotonic in x
      const float lo = pts[index - 1].x + 0.002f;
      const float hi = pts[index + 1].x - 0.002f;
      x = std::min(hi, std::max(lo, x));
   }

   pts[index].x = x;
   pts[index].y = y;
   mLutDirty = true;
}

void CurvesNode::RemovePoint(int channel, int index)
{
   if (channel < 0 || channel >= kChannelCount)
      return;
   std::vector<Point>& pts = mPoints[channel];
   if (index <= 0 || index >= (int)pts.size() - 1)
      return; // endpoints are permanent
   pts.erase(pts.begin() + index);
   mLutDirty = true;
}

float CurvesNode::Evaluate(int channel, float x) const
{
   if (channel < 0 || channel >= kChannelCount)
      return x;
   const std::vector<Point>& pts = mPoints[channel];
   if (pts.size() < 2)
      return x;

   x = std::min(1.0f, std::max(0.0f, x));
   if (x <= pts.front().x)
      return pts.front().y;
   if (x >= pts.back().x)
      return pts.back().y;

   size_t i = 0;
   while (i + 1 < pts.size() && pts[i + 1].x < x)
      i++;

   const Point& p1 = pts[i];
   const Point& p2 = pts[i + 1];
   const float span = std::max(1e-5f, p2.x - p1.x);
   const float t = (x - p1.x) / span;

   // Catmull-Rom through the neighbours, so the curve is smooth rather than
   // faceted, with the ends duplicated to avoid overshoot at the edges.
   const Point& p0 = (i > 0) ? pts[i - 1] : p1;
   const Point& p3 = (i + 2 < pts.size()) ? pts[i + 2] : p2;

   const float t2 = t * t;
   const float t3 = t2 * t;
   float y = 0.5f * ((2.0f * p1.y) +
                     (-p0.y + p2.y) * t +
                     (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                     (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
   return std::min(1.0f, std::max(0.0f, y));
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

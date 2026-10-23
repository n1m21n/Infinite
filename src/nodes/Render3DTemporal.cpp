#include "Render3DTemporal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "platform/OpenGLHeaders.h"
#include "EmbeddedFiles.h"
#include "GLUtil.h"

// AMD FidelityFX FSR 1 (MIT), CPU side: the constants EASU and RCAS read.
#pragma warning(push, 0)
#define A_CPU 1
#include "ffx_a.h"
#include "ffx_fsr1.h"
#pragma warning(pop)

namespace
{
   constexpr int kJitterSteps = 16;

   float Halton(int index, int base)
   {
      float f = 1.0f, r = 0.0f;
      for (int i = index; i > 0; i /= base)
      {
         f /= (float)base;
         r += f * (float)(i % base);
      }
      return r;
   }

   // shaders/<name> with its //@include lines replaced by the embedded file.
   std::string ShaderSource(const char* name)
   {
      const char* text = EmbeddedFile(name);
      if (text == nullptr)
         return std::string();
      std::string src = text;
      // Only at the start of a line (a comment may mention the directive).
      const std::string tag = "\n//@include ";
      for (size_t at = src.find(tag); at != std::string::npos; at = src.find(tag, at))
      {
         at++; // keep the newline
         size_t end = src.find('\n', at);
         if (end == std::string::npos)
            end = src.size();
         std::string file = src.substr(at + tag.size() - 1, end - at - (tag.size() - 1));
         while (!file.empty() && (file.back() == '\r' || file.back() == ' '))
            file.pop_back();
         const char* inc = EmbeddedFile(file.c_str());
         if (inc == nullptr)
            return std::string();
         src.replace(at, end - at, inc);
         at += std::char_traits<char>::length(inc);
      }
      return src;
   }

   unsigned int BuildCompute(const char* name)
   {
      const std::string src = ShaderSource(name);
      if (src.empty())
      {
         fprintf(stderr, "Render 3D: shader %s is not in this build\n", name);
         return 0;
      }
      const char* p = src.c_str();
      GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
      glShaderSource(cs, 1, &p, nullptr);
      glCompileShader(cs);
      GLint ok = 0;
      glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[2048];
         glGetShaderInfoLog(cs, sizeof(log), nullptr, log);
         fprintf(stderr, "Render 3D: %s did not compile: %s\n", name, log);
         glDeleteShader(cs);
         return 0;
      }
      GLuint prog = glCreateProgram();
      glAttachShader(prog, cs);
      glLinkProgram(prog);
      glDeleteShader(cs);
      glGetProgramiv(prog, GL_LINK_STATUS, &ok);
      if (!ok)
      {
         char log[2048];
         glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
         fprintf(stderr, "Render 3D: %s did not link: %s\n", name, log);
         glDeleteProgram(prog);
         return 0;
      }
      GLUtil::LabelObject(GL_PROGRAM, prog, name);
      return prog;
   }

   unsigned int MakeTexture(int w, int h, unsigned int format, const char* label)
   {
      GLuint t = 0;
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      GLUtil::LabelObject(GL_TEXTURE, t, label);
      return t;
   }

   void DeleteTexture(unsigned int& t)
   {
      if (t != 0)
         glDeleteTextures(1, &t);
      t = 0;
   }

   GLuint Groups(int size, int tile)
   {
      return (GLuint)((size + tile - 1) / tile);
   }

   // Leaves the units a pass used as the rest of Render 3D expects them.
   void Unbind(int textureUnits)
   {
      for (int i = textureUnits - 1; i >= 0; i--)
      {
         glActiveTexture(GL_TEXTURE0 + i);
         glBindTexture(GL_TEXTURE_2D, 0);
      }
      glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
      glUseProgram(0);
   }
}

Render3DTemporal::~Render3DTemporal()
{
   Release();
}

unsigned int Render3DTemporal::BuildComputeShader(const char* name)
{
   return BuildCompute(name);
}

std::string Render3DTemporal::LoadShaderSource(const char* name)
{
   return ShaderSource(name);
}

void Render3DTemporal::Release()
{
   DeleteTexture(mMotion);
   DeleteTexture(mHistory[0]);
   DeleteTexture(mHistory[1]);
   DeleteTexture(mCoverage[0]);
   DeleteTexture(mCoverage[1]);
   DeleteTexture(mEasuOut);
   DeleteTexture(mRcasOut);
   unsigned int* programs[] = { &mMotionProgram, &mTaaProgram, &mEasuProgram, &mRcasProgram };
   for (unsigned int* p : programs)
   {
      if (*p != 0)
         glDeleteProgram(*p);
      *p = 0;
   }
   mProgramsTried = false;
   mTaaW = mTaaH = mUpW = mUpH = 0;
   mHistoryFrames = 0;
   mHavePrev = false;
}

void Render3DTemporal::JitterOffset(int index, float& x, float& y)
{
   // Halton(2, 3) from 1, so step 0 is not (0, 0) twice in a row.
   const int i = (index % kJitterSteps) + 1;
   x = Halton(i, 2) - 0.5f;
   y = Halton(i, 3) - 0.5f;
}

Mat4 Render3DTemporal::JitteredProjection(const Mat4& proj, float x, float y, int w, int h)
{
   // Clip-space translation by (2x/w, 2y/h) * w_clip: an NDC shift for both
   // perspective (w = -z) and orthographic (w = 1) projections.
   Mat4 shift = Mat4::Identity();
   shift.m[12] = 2.0f * x / (float)std::max(1, w);
   shift.m[13] = 2.0f * y / (float)std::max(1, h);
   return Mat4::Multiply(shift, proj);
}

bool Render3DTemporal::EnsurePrograms()
{
   if (!mProgramsTried)
   {
      mProgramsTried = true;
      mMotionProgram = BuildCompute("render3d_motion.comp.glsl");
      mTaaProgram = BuildCompute("render3d_taa.comp.glsl");
      mEasuProgram = BuildCompute("fsr1_easu.comp.glsl");
      mRcasProgram = BuildCompute("fsr1_rcas.comp.glsl");
   }
   return mMotionProgram != 0 && mTaaProgram != 0;
}

void Render3DTemporal::EnsureTaaTargets(int w, int h)
{
   if (mMotion != 0 && w == mTaaW && h == mTaaH)
      return;
   DeleteTexture(mMotion);
   DeleteTexture(mHistory[0]);
   DeleteTexture(mHistory[1]);
   DeleteTexture(mCoverage[0]);
   DeleteTexture(mCoverage[1]);
   mMotion = MakeTexture(w, h, GL_RG16F, "Render 3D motion vectors");
   mHistory[0] = MakeTexture(w, h, GL_RGBA16F, "Render 3D TAA history A");
   mHistory[1] = MakeTexture(w, h, GL_RGBA16F, "Render 3D TAA history B");
   mCoverage[0] = MakeTexture(w, h, GL_R16F, "Render 3D TAA coverage A");
   mCoverage[1] = MakeTexture(w, h, GL_R16F, "Render 3D TAA coverage B");
   mTaaW = w;
   mTaaH = h;
   ResetHistory();
}

void Render3DTemporal::EnsureUpscaleTargets(int w, int h)
{
   if (mEasuOut != 0 && w == mUpW && h == mUpH)
      return;
   DeleteTexture(mEasuOut);
   DeleteTexture(mRcasOut);
   mEasuOut = MakeTexture(w, h, GL_RGBA8, "Render 3D FSR EASU");
   mRcasOut = MakeTexture(w, h, GL_RGBA8, "Render 3D FSR output");
   mUpW = w;
   mUpH = h;
}

void Render3DTemporal::ResetHistory()
{
   mHistoryFrames = 0;
   mHavePrev = false;
}

unsigned int Render3DTemporal::MotionVectors(unsigned int depthTex, int w, int h, const Mat4& viewProjJittered,
                                             const Mat4& viewProj, float jitterX, float jitterY, unsigned int velocityTex)
{
   if (!EnsurePrograms() || depthTex == 0)
      return 0;
   EnsureTaaTargets(w, h);
   GLUtil::DebugScope group("Render 3D: motion vectors");
   if (!mHavePrev)
   {
      mPrevViewProj = viewProj;
      mHavePrev = true;
   }
   const Mat4 inv = Mat4::Inverse(viewProjJittered);
   glUseProgram(mMotionProgram);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, depthTex);
   glUniform1i(glGetUniformLocation(mMotionProgram, "uDepth"), 0);
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, velocityTex);
   glActiveTexture(GL_TEXTURE0);
   glUniform1i(glGetUniformLocation(mMotionProgram, "uVelocity"), 1);
   glUniform1i(glGetUniformLocation(mMotionProgram, "uHasVelocity"), velocityTex != 0 ? 1 : 0);
   glBindImageTexture(0, mMotion, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
   glUniformMatrix4fv(glGetUniformLocation(mMotionProgram, "uInvViewProj"), 1, GL_FALSE, inv.m);
   glUniformMatrix4fv(glGetUniformLocation(mMotionProgram, "uPrevViewProj"), 1, GL_FALSE, mPrevViewProj.m);
   glUniform2f(glGetUniformLocation(mMotionProgram, "uJitterUv"), jitterX / (float)w, jitterY / (float)h);
   glUniform2i(glGetUniformLocation(mMotionProgram, "uSize"), w, h);
   glDispatchCompute(Groups(w, 8), Groups(h, 8), 1);
   glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
   glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
   Unbind(2);
   mPrevViewProj = viewProj;
   return mMotion;
}

unsigned int Render3DTemporal::ResolveTaa(unsigned int current, unsigned int depthTex, int w, int h)
{
   if (!EnsurePrograms() || current == 0 || depthTex == 0 || mMotion == 0 || w != mTaaW || h != mTaaH)
      return 0;
   GLUtil::DebugScope group("Render 3D: TAA");
   const int prev = mCurrent, next = 1 - mCurrent;
   glUseProgram(mTaaProgram);
   const unsigned int inputs[5] = { current, mHistory[prev], mMotion, depthTex, mCoverage[prev] };
   const char* names[5] = { "uCurrent", "uHistory", "uMotion", "uDepth", "uCoverageHistory" };
   for (int i = 0; i < 5; i++)
   {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, inputs[i]);
      glUniform1i(glGetUniformLocation(mTaaProgram, names[i]), i);
   }
   glBindImageTexture(0, mHistory[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
   glBindImageTexture(1, mCoverage[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
   glUniform2i(glGetUniformLocation(mTaaProgram, "uSize"), w, h);
   // 1/n while the history fills (a plain average of the first frames), then
   // a 10% exponential blend.
   const float blend = std::max(1.0f / (float)(mHistoryFrames + 1), 0.1f);
   glUniform1f(glGetUniformLocation(mTaaProgram, "uBlend"), blend);
   glUniform1i(glGetUniformLocation(mTaaProgram, "uHistoryValid"), mHistoryFrames > 0 ? 1 : 0);
   glDispatchCompute(Groups(w, 8), Groups(h, 8), 1);
   glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
   glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
   glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
   Unbind(5);
   mCurrent = next;
   mHistoryFrames++;
   mJitterIndex = (mJitterIndex + 1) % kJitterSteps;
   return mHistory[mCurrent];
}

unsigned int Render3DTemporal::Upscale(unsigned int src, int iw, int ih, int ow, int oh, float sharpenStops)
{
   EnsurePrograms();
   if (mEasuProgram == 0 || mRcasProgram == 0 || src == 0 || iw <= 0 || ih <= 0 || ow <= 0 || oh <= 0)
      return 0;
   EnsureUpscaleTargets(ow, oh);
   GLUtil::DebugScope group("Render 3D: FSR upscale");

   varAU4(con0);
   varAU4(con1);
   varAU4(con2);
   varAU4(con3);
   FsrEasuCon(con0, con1, con2, con3, (AF1)iw, (AF1)ih, (AF1)iw, (AF1)ih, (AF1)ow, (AF1)oh);
   glUseProgram(mEasuProgram);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, src);
   glUniform1i(glGetUniformLocation(mEasuProgram, "uInput"), 0);
   glUniform4uiv(glGetUniformLocation(mEasuProgram, "uCon0"), 1, con0);
   glUniform4uiv(glGetUniformLocation(mEasuProgram, "uCon1"), 1, con1);
   glUniform4uiv(glGetUniformLocation(mEasuProgram, "uCon2"), 1, con2);
   glUniform4uiv(glGetUniformLocation(mEasuProgram, "uCon3"), 1, con3);
   glUniform2i(glGetUniformLocation(mEasuProgram, "uOutSize"), ow, oh);
   glBindImageTexture(0, mEasuOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
   glDispatchCompute(Groups(ow, 16), Groups(oh, 16), 1);
   glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

   varAU4(rcas);
   FsrRcasCon(rcas, (AF1)std::max(0.0f, sharpenStops));
   glUseProgram(mRcasProgram);
   glBindTexture(GL_TEXTURE_2D, mEasuOut);
   glUniform1i(glGetUniformLocation(mRcasProgram, "uInput"), 0);
   glUniform4uiv(glGetUniformLocation(mRcasProgram, "uCon0"), 1, rcas);
   glUniform2i(glGetUniformLocation(mRcasProgram, "uOutSize"), ow, oh);
   glBindImageTexture(0, mRcasOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
   glDispatchCompute(Groups(ow, 16), Groups(oh, 16), 1);
   glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
   Unbind(1);
   return mRcasOut;
}

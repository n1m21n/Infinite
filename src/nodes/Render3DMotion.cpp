#include "Render3DMotion.h"

#include <algorithm>
#include <cstdio>

#include "platform/OpenGLHeaders.h"
#include "GLUtil.h"
#include "Render3DTemporal.h"

namespace
{
   // The velocity pass: positions now and last frame, through the cameras now
   // and last frame; the difference in pixels.
   // The copies' matrices last frame come from an SSBO by the copy's number -
   // aInstanceIndex in a culled draw (Render3DCull's lists), gl_InstanceID in
   // a plain one. uLod: Render3DCull's small list, one camera-facing quad per
   // copy, about its box's size (velocity, not looks).
   const char* kVelVert =
      "#version 430 core\n"
      "layout(location = 0) in vec3 aPos;\n"
      "layout(location = 3) in mat4 aInstance;\n" // 3..6
      "layout(location = 10) in uint aInstanceIndex;\n"
      "layout(std430, binding = 0) readonly buffer PrevMats { mat4 prevMat[]; };\n"
      "uniform int uInstanced;\n"
      "uniform int uHasPrev;\n"
      "uniform int uRemap;\n"
      "uniform int uLod;\n"
      "uniform vec3 uLodCentre;\n"
      "uniform vec3 uLodHalf;\n"
      "uniform vec3 uCamRight;\n"
      "uniform vec3 uCamUp;\n"
      "uniform mat4 uModel;\n"
      "uniform mat4 uPrevModel;\n"
      "uniform mat4 uViewProj;\n"
      "uniform mat4 uPrevViewProj;\n"
      "out vec4 vCur;\n"
      "out vec4 vPrev;\n"
      "void main() {\n"
      "   uint idx = uRemap == 1 ? aInstanceIndex : uint(gl_InstanceID);\n"
      "   mat4 m = uInstanced == 1 ? aInstance : uModel;\n"
      "   mat4 pm = uInstanced == 1 ? (uHasPrev == 1 ? prevMat[idx] : aInstance) : uPrevModel;\n"
      "   vec4 w, pw;\n"
      "   if (uLod == 1) {\n"
      "      float s = max(length(m[0].xyz) * uLodHalf.x, max(length(m[1].xyz) * uLodHalf.y, length(m[2].xyz) * uLodHalf.z));\n"
      "      vec3 off = (uCamRight * aPos.x + uCamUp * aPos.y) * s;\n"
      "      w = vec4((m * vec4(uLodCentre, 1.0)).xyz + off, 1.0);\n"
      "      pw = vec4((pm * vec4(uLodCentre, 1.0)).xyz + off, 1.0);\n"
      "   } else {\n"
      "      w = m * vec4(aPos, 1.0);\n"
      "      pw = pm * vec4(aPos, 1.0);\n"
      "   }\n"
      "   vCur = uViewProj * w;\n"
      "   vPrev = uPrevViewProj * pw;\n"
      "   gl_Position = vCur;\n"
      "}\n";
   const char* kVelFrag =
      "#version 430 core\n"
      "in vec4 vCur;\n"
      "in vec4 vPrev;\n"
      "uniform vec2 uSize;\n"
      "out vec4 fragColor;\n"
      "void main() {\n"
      "   vec2 a = vCur.xy / vCur.w;\n"
      "   vec2 b = vPrev.xy / max(vPrev.w, 1e-5);\n"
      "   fragColor = vec4((a - b) * 0.5 * uSize, 1.0, 1.0);\n"
      "}\n";

   GLuint CompileStage(GLenum type, const char* src)
   {
      GLuint s = glCreateShader(type);
      glShaderSource(s, 1, &src, nullptr);
      glCompileShader(s);
      GLint ok = 0;
      glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
      if (!ok)
      {
         char log[2048];
         glGetShaderInfoLog(s, sizeof(log), nullptr, log);
         fprintf(stderr, "Render 3D: motion blur shader did not compile: %s\n", log);
         glDeleteShader(s);
         return 0;
      }
      return s;
   }

   GLuint BuildGraphics(const char* vs, const char* fs)
   {
      const GLuint v = CompileStage(GL_VERTEX_SHADER, vs), f = CompileStage(GL_FRAGMENT_SHADER, fs);
      if (v == 0 || f == 0)
      {
         if (v) glDeleteShader(v);
         if (f) glDeleteShader(f);
         return 0;
      }
      GLuint p = glCreateProgram();
      glAttachShader(p, v);
      glAttachShader(p, f);
      glLinkProgram(p);
      glDeleteShader(v);
      glDeleteShader(f);
      GLint ok = 0;
      glGetProgramiv(p, GL_LINK_STATUS, &ok);
      if (!ok)
      {
         char log[2048];
         glGetProgramInfoLog(p, sizeof(log), nullptr, log);
         fprintf(stderr, "Render 3D: motion blur program did not link: %s\n", log);
         glDeleteProgram(p);
         return 0;
      }
      GLUtil::LabelObject(GL_PROGRAM, p, "Render 3D: velocity");
      return p;
   }

   constexpr unsigned int kRingWords = 20; // shaders/render3d_prev_instances.comp.glsl
}

void Render3DMotion::Slot::Release()
{
   if (prevMat) glDeleteBuffers(1, &prevMat);
   if (byIndex) glDeleteBuffers(1, &byIndex);
   prevMat = byIndex = 0;
   capacity = 0;
   lastCount = -1;
   havePrevModel = false;
   source = nullptr;
}

Render3DMotion::~Render3DMotion()
{
   Release();
}

bool Render3DMotion::EnsurePrograms()
{
   if (!mTried)
   {
      mTried = true;
      mPrevProgram = Render3DTemporal::BuildComputeShader("render3d_prev_instances.comp.glsl");
      mVelProgram = BuildGraphics(kVelVert, kVelFrag);
   }
   return mPrevProgram != 0 && mVelProgram != 0;
}

void Render3DMotion::EnsureTargets(int w, int h)
{
   if (w == mW && h == mH && mFbo != 0)
      return;
   if (mFbo) glDeleteFramebuffers(1, &mFbo);
   if (mVelTex) glDeleteTextures(1, &mVelTex);
   if (mDepth) glDeleteRenderbuffers(1, &mDepth);
   auto tex = [](int tw, int th, const char* label) {
      GLuint t = 0;
      glGenTextures(1, &t);
      glBindTexture(GL_TEXTURE_2D, t);
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, tw, th);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      GLUtil::LabelObject(GL_TEXTURE, t, label);
      return t;
   };
   mVelTex = tex(w, h, "Render 3D: velocity");
   glGenRenderbuffers(1, &mDepth);
   glBindRenderbuffer(GL_RENDERBUFFER, mDepth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glGenFramebuffers(1, &mFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mVelTex, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepth);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   mW = w;
   mH = h;
}

bool Render3DMotion::PrevInstances(Slot& s, unsigned int key, const Render3DCull::Stream cols[4], unsigned int partIdBuf,
                                   int partIdStride, int count)
{
   if (count <= 0 || !EnsurePrograms())
      return false;
   for (int c = 0; c < 4; c++)
      if (cols[c].buf == 0 || cols[c].size != 4)
         return false;
   if (count > s.capacity)
   {
      const int cap = std::max(count, s.capacity + s.capacity / 2);
      if (s.prevMat == 0) glGenBuffers(1, &s.prevMat);
      if (s.byIndex == 0) glGenBuffers(1, &s.byIndex);
      const GLsizeiptr bytes = (GLsizeiptr)cap * 16 * (GLsizeiptr)sizeof(float);
      glBindBuffer(GL_COPY_WRITE_BUFFER, s.prevMat);
      glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
      glBindBuffer(GL_COPY_WRITE_BUFFER, s.byIndex);
      glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
      s.capacity = cap;
      s.lastCount = -1; // reallocated: nothing kept
   }
   const bool hasId = partIdBuf != 0;
   if (hasId)
   {
      unsigned int need = 1024;
      while (need < (unsigned int)count * 4u)
         need <<= 1;
      if (need > mRingSlots)
      {
         if (mRing == 0) glGenBuffers(1, &mRing);
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, mRing);
         glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)need * kRingWords * sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
         glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
         mRingSlots = need;
      }
   }
   glUseProgram(mPrevProgram);
   GLint stride[4], offset[4];
   for (int c = 0; c < 4; c++)
   {
      stride[c] = cols[c].stride;
      offset[c] = cols[c].offset;
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)c, cols[c].buf);
   }
   // Unused bindings still point at a real buffer.
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, hasId ? partIdBuf : s.prevMat);
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, hasId ? mRing : s.byIndex);
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, s.prevMat);
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, s.byIndex);
   glUniform1i(glGetUniformLocation(mPrevProgram, "uCount"), count);
   glUniform4iv(glGetUniformLocation(mPrevProgram, "uStride"), 1, stride);
   glUniform4iv(glGetUniformLocation(mPrevProgram, "uOffset"), 1, offset);
   glUniform1i(glGetUniformLocation(mPrevProgram, "uHasId"), hasId ? 1 : 0);
   glUniform1i(glGetUniformLocation(mPrevProgram, "uIdStride"), std::max(1, partIdStride));
   glUniform1i(glGetUniformLocation(mPrevProgram, "uByIndexValid"), s.lastCount == count ? 1 : 0);
   glUniform1ui(glGetUniformLocation(mPrevProgram, "uKey"), key);
   glUniform1ui(glGetUniformLocation(mPrevProgram, "uMask"), hasId ? mRingSlots - 1 : 0u);
   const GLuint groups = (GLuint)((count + 255) / 256);
   glUniform1i(glGetUniformLocation(mPrevProgram, "uPass"), 0);
   glDispatchCompute(groups, 1, 1);
   glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
   glUniform1i(glGetUniformLocation(mPrevProgram, "uPass"), 1);
   glDispatchCompute(groups, 1, 1);
   glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
   for (GLuint b = 0; b < 8; b++)
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, b, 0);
   glUseProgram(0);
   s.lastCount = count;
   return true;
}

bool Render3DMotion::BeginVelocity(int w, int h, const Mat4& viewProj, const Mat4& prevViewProj, const float camRight[3],
                                   const float camUp[3])
{
   if (w <= 0 || h <= 0 || !EnsurePrograms())
      return false;
   EnsureTargets(w, h);
   glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
   glViewport(0, 0, w, h);
   glDisable(GL_BLEND);
   glDisable(GL_CULL_FACE);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   glDepthMask(GL_TRUE);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClearDepth(1.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glUseProgram(mVelProgram);
   glUniformMatrix4fv(glGetUniformLocation(mVelProgram, "uViewProj"), 1, GL_FALSE, viewProj.m);
   glUniformMatrix4fv(glGetUniformLocation(mVelProgram, "uPrevViewProj"), 1, GL_FALSE, prevViewProj.m);
   glUniform2f(glGetUniformLocation(mVelProgram, "uSize"), (float)w, (float)h);
   glUniform3fv(glGetUniformLocation(mVelProgram, "uCamRight"), 1, camRight);
   glUniform3fv(glGetUniformLocation(mVelProgram, "uCamUp"), 1, camUp);
   return true;
}

void Render3DMotion::SetVelocityDraw(bool instanced, unsigned int prevMat, bool remap, bool lod, const float lodCentre[3],
                                     const float lodHalf[3], const Mat4& model, const Mat4& prevModel)
{
   static const float kZero[3] = { 0.0f, 0.0f, 0.0f };
   glUniform1i(glGetUniformLocation(mVelProgram, "uInstanced"), instanced ? 1 : 0);
   glUniform1i(glGetUniformLocation(mVelProgram, "uHasPrev"), instanced && prevMat != 0 ? 1 : 0);
   glUniform1i(glGetUniformLocation(mVelProgram, "uRemap"), remap ? 1 : 0);
   glUniform1i(glGetUniformLocation(mVelProgram, "uLod"), lod ? 1 : 0);
   glUniform3fv(glGetUniformLocation(mVelProgram, "uLodCentre"), 1, lodCentre ? lodCentre : kZero);
   glUniform3fv(glGetUniformLocation(mVelProgram, "uLodHalf"), 1, lodHalf ? lodHalf : kZero);
   glUniformMatrix4fv(glGetUniformLocation(mVelProgram, "uModel"), 1, GL_FALSE, model.m);
   glUniformMatrix4fv(glGetUniformLocation(mVelProgram, "uPrevModel"), 1, GL_FALSE, prevModel.m);
   // An unused binding still points at a real buffer.
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, prevMat != 0 ? prevMat : DummyBuffer());
}

void Render3DMotion::DrawVelocity(unsigned int vao, unsigned int prim, int indexCount, int instances, bool instanced,
                                  unsigned int prevMat, const Mat4& model, const Mat4& prevModel)
{
   if (vao == 0 || indexCount <= 0)
      return;
   glBindVertexArray(vao);
   SetVelocityDraw(instanced, prevMat, false, false, nullptr, nullptr, model, prevModel);
   if (instanced)
      glDrawElementsInstanced(prim, indexCount, GL_UNSIGNED_INT, nullptr, (GLsizei)instances);
   else
      glDrawElements(prim, indexCount, GL_UNSIGNED_INT, nullptr);
}

void Render3DMotion::EndVelocity()
{
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
   glBindVertexArray(0);
   glUseProgram(0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int Render3DMotion::DummyBuffer()
{
   if (mDummy == 0)
   {
      glGenBuffers(1, &mDummy);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, mDummy);
      glBufferData(GL_SHADER_STORAGE_BUFFER, 64, nullptr, GL_STATIC_DRAW);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
   }
   return mDummy;
}

void Render3DMotion::Release()
{
   if (mPrevProgram) glDeleteProgram(mPrevProgram);
   if (mVelProgram) glDeleteProgram(mVelProgram);
   if (mFbo) glDeleteFramebuffers(1, &mFbo);
   if (mVelTex) glDeleteTextures(1, &mVelTex);
   if (mDepth) glDeleteRenderbuffers(1, &mDepth);
   if (mRing) glDeleteBuffers(1, &mRing);
   if (mDummy) glDeleteBuffers(1, &mDummy);
   mDummy = 0;
   mPrevProgram = mVelProgram = 0;
   mTried = false;
   mFbo = mVelTex = mDepth = mRing = 0;
   mRingSlots = 0;
   mW = mH = 0;
}

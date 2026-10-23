#include "Render3DFxNode.h"

#include <algorithm>

#include "platform/OpenGLHeaders.h"
#include "GLUtil.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/Render3DTemporal.h"

namespace
{
   GLuint MakeTexture(GLenum format, int w, int h, const char* label)
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

   void BindTex(GLuint unit, GLuint tex)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
   }

   void Finish(int w, int h, int units)
   {
      glDispatchCompute((GLuint)((w + 15) / 16), (GLuint)((h + 15) / 16), 1);
      glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
      for (int u = units - 1; u >= 0; u--)
         BindTex((GLuint)u, 0);
      glUseProgram(0);
   }
}

Render3DFxNode::~Render3DFxNode()
{
   const GLuint programs[] = { mAoProgram, mCompositeProgram, mDofProgram, mBlurProgram };
   for (GLuint p : programs)
      if (p) glDeleteProgram(p);
   const GLuint texs[] = { mAoTex, mTex[0], mTex[1], mTileTex, mNeighborTex };
   for (GLuint t : texs)
      if (t) glDeleteTextures(1, &t);
}

bool Render3DFxNode::EnsurePrograms()
{
   if (!mTried)
   {
      mTried = true;
      mAoProgram = Render3DTemporal::BuildComputeShader("render3d_fx_ao.comp.glsl");
      mCompositeProgram = Render3DTemporal::BuildComputeShader("render3d_fx_composite.comp.glsl");
      mDofProgram = Render3DTemporal::BuildComputeShader("render3d_fx_dof.comp.glsl");
      mBlurProgram = Render3DTemporal::BuildComputeShader("render3d_motion_blur.comp.glsl");
   }
   return mAoProgram && mCompositeProgram && mDofProgram && mBlurProgram;
}

void Render3DFxNode::EnsureTargets(int w, int h)
{
   if (w == mTexW && h == mTexH && mTex[0] != 0)
      return;
   const GLuint texs[] = { mAoTex, mTex[0], mTex[1], mTileTex, mNeighborTex };
   for (GLuint t : texs)
      if (t) glDeleteTextures(1, &t);
   mAoTex = MakeTexture(GL_R16F, w, h, "Render 3D FX: occlusion");
   mTex[0] = MakeTexture(GL_RGBA16F, w, h, "Render 3D FX: a");
   mTex[1] = MakeTexture(GL_RGBA16F, w, h, "Render 3D FX: b");
   mTileTex = MakeTexture(GL_RG16F, (w + 15) / 16, (h + 15) / 16, "Render 3D FX: motion tiles");
   mNeighborTex = MakeTexture(GL_RG16F, (w + 15) / 16, (h + 15) / 16, "Render 3D FX: motion neighbour max");
   mTexW = w;
   mTexH = h;
}

void Render3DFxNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const unsigned int src = mInput.Pull(frameId);
   INode* upstream = mInput.GetSource();
   mPassTex = src;
   mPassFormat = upstream != nullptr ? upstream->GetOutputFormat() : 0x8058;
   mW = mInput.Width();
   mH = mInput.Height();

   const bool wantAo = aoAmount > 0.001f, wantSsr = reflections > 0.001f, wantDof = dofBlur >= 0.5f,
              wantBlur = shutter > 0.001f;
   auto* render = dynamic_cast<Render3DNode*>(upstream);
   mHasRender = render != nullptr;
   if (render != nullptr && (wantAo || wantSsr || wantDof || wantBlur))
      render->RequestDepth();
   if (render != nullptr && wantBlur)
      render->RequestVelocity();
   mHasDepth = render != nullptr && render->LastCamera().valid && render->DepthValid() && render->DepthTexture() != 0;
   const unsigned int velocity = render != nullptr && wantBlur ? render->VelocityTexture() : 0u;
   const bool active = src != 0 && mW > 0 && mH > 0 && mHasDepth &&
                       (wantAo || wantSsr || wantDof || (wantBlur && velocity != 0)) && EnsurePrograms();

   // Cook again only when the picture or a param changed (or what applies did).
   const unsigned long long inRev = mInput.Revision();
   std::vector<float> params;
   {
      struct Collect : ParamVisitor
      {
         std::vector<float>& out;
         explicit Collect(std::vector<float>& o) : out(o) {}
         void Float(const char*, float& v) override { out.push_back(v); }
         void Int(const char*, int& v) override { out.push_back((float)v); }
         void Bool(const char*, bool& v) override { out.push_back(v ? 1.0f : 0.0f); }
         void Text(const char*, std::string&) override {}
         void Color(const char*, float rgb[3]) override { out.insert(out.end(), rgb, rgb + 3); }
      } collect(params);
      VisitParams(collect);
   }
   const bool changed = inRev != mLastInputRevision || params != mLastParams || velocity != mLastVelocity ||
                        active != mActive;
   mLastInputRevision = inRev;
   mLastParams = params;
   mLastVelocity = velocity;
   if (!changed)
      return;
   mActive = active;
   mRevision = NextTextureRevision();
   if (!active)
      return; // the picture passes through

   GLUtil::DebugScope group("Render 3D FX");
   EnsureTargets(mW, mH);
   const unsigned int depth = render->DepthTexture();
   const Render3DNode::SceneCamera& cam = render->LastCamera();
   const Mat4 invProj = Mat4::Inverse(cam.proj);
   const bool ortho = cam.proj.m[11] == 0.0f;

   if (wantAo)
   {
      glUseProgram(mAoProgram);
      BindTex(0, depth);
      glBindImageTexture(0, mAoTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
      glUniform1i(glGetUniformLocation(mAoProgram, "uDepth"), 0);
      glUniform2i(glGetUniformLocation(mAoProgram, "uSize"), mW, mH);
      glUniformMatrix4fv(glGetUniformLocation(mAoProgram, "uInvProj"), 1, GL_FALSE, invProj.m);
      glUniform1f(glGetUniformLocation(mAoProgram, "uProjScale"), cam.proj.m[5] * (float)mH * 0.5f);
      glUniform1i(glGetUniformLocation(mAoProgram, "uOrtho"), ortho ? 1 : 0);
      glUniform1f(glGetUniformLocation(mAoProgram, "uRadius"), std::max(aoRadius, 0.001f));
      glUniform1f(glGetUniformLocation(mAoProgram, "uAmount"), aoAmount);
      Finish(mW, mH, 1);
   }

   unsigned int cur = src;
   int next = 0;
   if (wantAo || wantSsr)
   {
      glUseProgram(mCompositeProgram);
      BindTex(0, cur);
      BindTex(1, depth);
      BindTex(2, mAoTex);
      glBindImageTexture(0, mTex[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uColor"), 0);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uDepth"), 1);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uAo"), 2);
      glUniform2i(glGetUniformLocation(mCompositeProgram, "uSize"), mW, mH);
      glUniformMatrix4fv(glGetUniformLocation(mCompositeProgram, "uProj"), 1, GL_FALSE, cam.proj.m);
      glUniformMatrix4fv(glGetUniformLocation(mCompositeProgram, "uInvProj"), 1, GL_FALSE, invProj.m);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uUseAo"), wantAo ? 1 : 0);
      glUniform1f(glGetUniformLocation(mCompositeProgram, "uSsr"), wantSsr ? reflections : 0.0f);
      glUniform1f(glGetUniformLocation(mCompositeProgram, "uSsrDistance"), std::max(reflectDistance, 0.01f));
      Finish(mW, mH, 3);
      cur = mTex[next];
      next = 1 - next;
   }
   if (wantDof)
   {
      glUseProgram(mDofProgram);
      BindTex(0, cur);
      BindTex(1, depth);
      glBindImageTexture(0, mTex[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
      glUniform1i(glGetUniformLocation(mDofProgram, "uColor"), 0);
      glUniform1i(glGetUniformLocation(mDofProgram, "uDepth"), 1);
      glUniform2i(glGetUniformLocation(mDofProgram, "uSize"), mW, mH);
      glUniformMatrix4fv(glGetUniformLocation(mDofProgram, "uInvProj"), 1, GL_FALSE, invProj.m);
      glUniform1f(glGetUniformLocation(mDofProgram, "uFocus"), std::max(focusDistance, 0.01f));
      glUniform1f(glGetUniformLocation(mDofProgram, "uRange"), std::max(focusRange, 0.0f));
      glUniform1f(glGetUniformLocation(mDofProgram, "uMaxBlur"), std::min(dofBlur, 48.0f));
      Finish(mW, mH, 2);
      cur = mTex[next];
      next = 1 - next;
   }
   if (wantBlur && velocity != 0)
   {
      // Three passes of one program: each tile's longest motion, the longest
      // that reaches each tile, then the gather (see the shader's comment).
      glUseProgram(mBlurProgram);
      BindTex(0, cur);
      BindTex(1, velocity);
      BindTex(2, mTileTex);
      BindTex(3, depth);
      glUniform1i(glGetUniformLocation(mBlurProgram, "uScene"), 0);
      glUniform1i(glGetUniformLocation(mBlurProgram, "uVelocity"), 1);
      glUniform1i(glGetUniformLocation(mBlurProgram, "uTileMax"), 2);
      glUniform1i(glGetUniformLocation(mBlurProgram, "uDepth"), 3);
      glUniformMatrix4fv(glGetUniformLocation(mBlurProgram, "uInvProj"), 1, GL_FALSE, invProj.m);
      glUniform2i(glGetUniformLocation(mBlurProgram, "uSize"), mW, mH);
      glUniform2f(glGetUniformLocation(mBlurProgram, "uVelScale"), (float)mW / (float)std::max(1, render->VelocityWidth()),
                  (float)mH / (float)std::max(1, render->VelocityHeight()));
      glUniform1f(glGetUniformLocation(mBlurProgram, "uShutter"), std::min(shutter, 8.0f));
      const Mat4 invViewProj = Mat4::Inverse(render->VelocityViewProj());
      glUniformMatrix4fv(glGetUniformLocation(mBlurProgram, "uInvViewProj"), 1, GL_FALSE, invViewProj.m);
      glUniformMatrix4fv(glGetUniformLocation(mBlurProgram, "uPrevViewProj"), 1, GL_FALSE,
                         render->VelocityPrevViewProj().m);
      const int tilesW = (mW + 15) / 16, tilesH = (mH + 15) / 16;
      auto tilePass = [&](int pass, GLuint readTex, GLuint writeTex) {
         BindTex(2, readTex);
         glUniform1i(glGetUniformLocation(mBlurProgram, "uPass"), pass);
         glBindImageTexture(1, writeTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
         glDispatchCompute((GLuint)((tilesW + 15) / 16), (GLuint)((tilesH + 15) / 16), 1);
         glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
         glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
      };
      tilePass(0, mNeighborTex, mTileTex); // reads nothing from unit 2
      tilePass(1, mTileTex, mNeighborTex);
      BindTex(2, mNeighborTex);
      glUniform1i(glGetUniformLocation(mBlurProgram, "uPass"), 2);
      glBindImageTexture(0, mTex[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
      Finish(mW, mH, 4);
      cur = mTex[next];
      next = 1 - next;
   }
   mOutTex = cur;
   if (mOutTex == src)
      mActive = false; // nothing ran after all
}

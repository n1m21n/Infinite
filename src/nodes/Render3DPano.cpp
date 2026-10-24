// Render 3D 360 / dome output. The scene is drawn six times from the camera's
// eye, one 90 degree face per draw (CookOnce with mFaceOverride), each copied
// into a cube map; one resample pass then unwraps the cube into the chosen
// layout:
//   Equirect 360  - longitude across, latitude up; the camera's forward is the
//                   middle of the picture. 2:1 is what VR players expect.
//   Cubemap 3x2   - top row right, left, up; bottom row down, front, back
//                   (the Facebook 360 3x2 order), each cell a 90 degree view.
//   Fisheye dome  - equidistant fisheye in a circle the height of the output.
//                   Tilt 0 is a planetarium dome (centre straight up, front at
//                   the bottom of the circle); 90 looks straight ahead.
// With HDR on, the faces are drawn untonemapped and the bloom/tonemap finish
// runs once on the unwrapped picture, so it has no seams at the cube's edges.
#include "Geometry3DNodes.h"

#include "core/Transport.h"
#include "gl3.h"
#include <algorithm>
#include <cmath>

namespace
{
   const char* kPanoHead =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n";

   const char* kCopyBody =
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   const char* kPanoBody =
      "uniform samplerCube uCube;\n"
      "uniform int uLayout;\n" // 0 equirect, 1 cubemap 3x2, 2 fisheye
      "uniform int uDepth;\n"  // 1: the cube holds depth, outside the dome is far
      "uniform vec2 uSize;\n"
      "uniform float uDome;\n" // degrees across the dome circle
      "uniform float uTilt;\n" // degrees
      "const float PI = 3.14159265;\n"
      // Directions are in the camera's level frame: x right, y up, -z forward.
      "bool dirFor(vec2 uv, out vec3 d) {\n"
      "   d = vec3(0.0, 0.0, -1.0);\n"
      "   if (uLayout == 0) {\n"
      "      float lon = (uv.x - 0.5) * 2.0 * PI;\n"
      "      float lat = (uv.y - 0.5) * PI;\n"
      "      d = vec3(sin(lon) * cos(lat), sin(lat), -cos(lon) * cos(lat));\n"
      "      return true;\n"
      "   }\n"
      "   if (uLayout == 1) {\n"
      "      vec2 g = uv * vec2(3.0, 2.0);\n"
      "      vec2 cell = min(floor(g), vec2(2.0, 1.0));\n"
      "      vec2 st = (g - cell) * 2.0 - 1.0;\n"
      "      int idx = int(cell.x) + (cell.y > 0.5 ? 0 : 3);\n" // uv.y = 1 is the top row
      "      vec3 f = vec3(0.0, 0.0, 1.0), r = vec3(-1.0, 0.0, 0.0), u = vec3(0.0, 1.0, 0.0);\n" // back
      "      if (idx == 0) { f = vec3(1.0, 0.0, 0.0); r = vec3(0.0, 0.0, 1.0); }\n"   // right
      "      else if (idx == 1) { f = vec3(-1.0, 0.0, 0.0); r = vec3(0.0, 0.0, -1.0); }\n" // left
      "      else if (idx == 2) { f = vec3(0.0, 1.0, 0.0); r = vec3(1.0, 0.0, 0.0); u = vec3(0.0, 0.0, 1.0); }\n" // up
      "      else if (idx == 3) { f = vec3(0.0, -1.0, 0.0); r = vec3(1.0, 0.0, 0.0); u = vec3(0.0, 0.0, -1.0); }\n" // down
      "      else if (idx == 4) { f = vec3(0.0, 0.0, -1.0); r = vec3(1.0, 0.0, 0.0); }\n" // front
      "      d = f + st.x * r + st.y * u;\n"
      "      return true;\n"
      "   }\n"
      "   float m = min(uSize.x, uSize.y);\n"
      "   vec2 p = (uv * 2.0 - 1.0) * uSize / m;\n"
      "   float rr = length(p);\n"
      "   if (rr > 1.0) return false;\n"
      "   float th = rr * radians(uDome) * 0.5;\n"
      "   float t = radians(uTilt);\n"
      "   vec3 c = vec3(0.0, cos(t), -sin(t));\n" // the dome's centre
      "   vec3 y = vec3(0.0, sin(t), cos(t));\n"  // up in the picture
      "   vec2 q = rr > 1e-6 ? p / rr : vec2(0.0);\n"
      "   d = c * cos(th) + (vec3(1.0, 0.0, 0.0) * q.x + y * q.y) * sin(th);\n"
      "   return true;\n"
      "}\n"
      "void main() {\n"
      "   vec3 d;\n"
      "   if (!dirFor(vUv, d)) { fragColor = uDepth == 1 ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
      "   vec4 s = texture(uCube, d);\n"
      "   fragColor = uDepth == 1 ? vec4(s.r) : s;\n"
      "}\n";

   void AllocCube(unsigned int& tex, int side, GLenum internalFormat, GLenum format)
   {
      if (tex == 0)
         glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
      for (int f = 0; f < 6; f++)
         glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, internalFormat, side, side, 0, format, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
   }
}

bool Render3DNode::EnsurePanoShaders()
{
   if (mPanoShadersTried)
      return mProgPano != 0;
   mPanoShadersTried = true;
   const std::string head = kPanoHead;
   mProgCubeCopy = GLUtil::CompileProgram((head + kCopyBody).c_str());
   mProgPano = GLUtil::CompileProgram((head + kPanoBody).c_str());
   if (mProgCubeCopy == 0)
      mProgPano = 0;
   return mProgPano != 0;
}

void Render3DNode::ReleasePano()
{
   GLUtil::DestroyFbo(mPano);
   GLUtil::DestroyFbo(mPanoDepth);
   if (mCubeColor != 0) { glDeleteTextures(1, &mCubeColor); mCubeColor = 0; }
   if (mCubeDepth != 0) { glDeleteTextures(1, &mCubeDepth); mCubeDepth = 0; }
   if (mCubeFbo != 0) { glDeleteFramebuffers(1, &mCubeFbo); mCubeFbo = 0; }
   if (mProgCubeCopy != 0) { glDeleteProgram(mProgCubeCopy); mProgCubeCopy = 0; }
   if (mProgPano != 0) { glDeleteProgram(mProgPano); mProgPano = 0; }
   mCubeSide = 0;
   mPanoValid = false;
}

void Render3DNode::ReleasePanoTargets()
{
   GLUtil::DestroyFbo(mPano);
   GLUtil::DestroyFbo(mPanoDepth);
   if (mCubeColor != 0) { glDeleteTextures(1, &mCubeColor); mCubeColor = 0; }
   if (mCubeDepth != 0) { glDeleteTextures(1, &mCubeDepth); mCubeDepth = 0; }
   if (mCubeFbo != 0) { glDeleteFramebuffers(1, &mCubeFbo); mCubeFbo = 0; }
   mCubeSide = 0;
   mPanoValid = false;
   mHasPanoSig = false;
}

void Render3DNode::CookPano(int frameId)
{
   if (!EnsurePanoShaders())
   {
      mPanoValid = false;
      return;
   }
   const int w = GLUtil::TargetSide(width, 16);
   const int h = GLUtil::TargetSide(height, 16);
   const float dome = std::clamp(domeAngle, 180.0f, 220.0f);
   // A face side that matches the output's pixel density at the centre of view.
   int side = 0;
   if (projection == kEquirect)
      side = w / 4;
   else if (projection == kCubemap3x2)
      side = std::max(w / 3, h / 2);
   else
      side = (int)std::lround((float)std::min(w, h) * 90.0f / dome);
   side = std::clamp(side, 64, 4096);

   // Same cooks CookOnce does first, so the signature sees this frame's revisions.
   CollectDraw();
   for (IGeometrySource* src : mDraw)
      if (auto* node = dynamic_cast<INode*>(src))
         node->CookIfNeeded(frameId);
   if (envInput.IsConnected())
      if (INode* env = envInput.Resolved())
         env->CookIfNeeded(frameId);
   SceneSignature sig = BuildSceneSignature();
   if (mPanoValid && mHasPanoSig && sig == mPanoSig && mPano.w == w && mPano.h == h &&
       !Transport::Instance().IsOfflineMode())
      return;

   const bool hdrOn = HdrActive();
   if (!hdrOn && mCubeDepth != 0)   // the depth cube is only read by the HDR finish
   {
      glDeleteTextures(1, &mCubeDepth);
      mCubeDepth = 0;
   }
   if (mCubeSide != side || mCubeColor == 0 || (hdrOn && mCubeDepth == 0))
   {
      AllocCube(mCubeColor, side, GL_RGBA16F, GL_RGBA);
      if (hdrOn)
         AllocCube(mCubeDepth, side, GL_R32F, GL_RED);
      mCubeSide = side;
   }
   if (mCubeFbo == 0)
      glGenFramebuffers(1, &mCubeFbo);

   const GLenum outFmt = (floatOutput || hdrOn) ? GL_RGBA16F : GL_RGBA8;
   if (!GLUtil::EnsureFbo(mPano, w, h, outFmt, "Render3D_Pano") ||
       (hdrOn && !GLUtil::EnsureFbo(mPanoDepth, w, h, GL_RGBA32F, "Render3D_Pano")))
   {
      mPanoValid = false;
      return;
   }

   const GLboolean wasBlend = glIsEnabled(GL_BLEND);
   const GLboolean wasScissor = glIsEnabled(GL_SCISSOR_TEST);
   const GLboolean wasSeamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);

   // Depth of field has no meaning on a panorama; ambient occlusion still runs per face.
   const float savedDof = dofAperture;
   dofAperture = 0.0f;
   GLUtil::Fbo face;
   face.fbo = mCubeFbo;
   face.w = face.h = side;
   for (int f = 0; f < 6; f++) // -Z (front) last, so the preview camera is the forward view
   {
      mFaceOverride = f;
      mFaceSide = side;
      CookOnce(frameId);
      mFaceOverride = -1;
      if (mColorTex == 0)
         break;

      glDisable(GL_BLEND);
      glDisable(GL_SCISSOR_TEST);
      glBindFramebuffer(GL_FRAMEBUFFER, mCubeFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, mCubeColor, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      GLUtil::RunShaderPass(face, mProgCubeCopy, [&]() {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, mColorTex);
         glUniform1i(glGetUniformLocation(mProgCubeCopy, "uSrc"), 0);
      });
      if (hdrOn && mDepthTex != 0)
      {
         glBindFramebuffer(GL_FRAMEBUFFER, mCubeFbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, mCubeDepth, 0);
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         GLUtil::RunShaderPass(face, mProgCubeCopy, [&]() {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mDepthTex);
            glUniform1i(glGetUniformLocation(mProgCubeCopy, "uSrc"), 0);
         });
      }
   }
   dofAperture = savedDof;

   glDisable(GL_BLEND);
   glDisable(GL_SCISSOR_TEST);
   glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   const int layout = projection == kEquirect ? 0 : (projection == kCubemap3x2 ? 1 : 2);
   auto resample = [&](const GLUtil::Fbo& out, unsigned int cube, int depthMode) {
      GLUtil::RunShaderPass(out, mProgPano, [&]() {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
         glUniform1i(glGetUniformLocation(mProgPano, "uCube"), 0);
         glUniform1i(glGetUniformLocation(mProgPano, "uLayout"), layout);
         glUniform1i(glGetUniformLocation(mProgPano, "uDepth"), depthMode);
         glUniform2f(glGetUniformLocation(mProgPano, "uSize"), (float)w, (float)h);
         glUniform1f(glGetUniformLocation(mProgPano, "uDome"), dome);
         glUniform1f(glGetUniformLocation(mProgPano, "uTilt"), std::clamp(domeTilt, 0.0f, 90.0f));
      });
      glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
   };
   resample(mPano, mCubeColor, 0);
   if (hdrOn)
   {
      resample(mPanoDepth, mCubeDepth, 1);
      RunHdr(mPano.tex, mPanoDepth.tex, w, h, mPano.fbo);
   }

   if (!wasSeamless) glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   if (wasBlend) glEnable(GL_BLEND);
   if (wasScissor) glEnable(GL_SCISSOR_TEST);

   mDepthResolved = false; // the depth texture holds one face, not this picture
   mPanoSig = sig;
   mHasPanoSig = true;
   mPanoValid = true;
}

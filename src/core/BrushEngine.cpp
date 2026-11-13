#include "BrushEngine.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

namespace BrushEngine
{
   namespace
   {
      // One dab, drawn scissored to its own square and blended into the
      // coverage buffer (ONE, ONE_MINUS_SRC_ALPHA): cov = a + cov (1 - a).
      const char* kDabFrag =
         "#version 150\n"
         "out vec4 fragColor;\n"
         "uniform vec2 uCenter;\n"
         "uniform float uRadius;\n"
         "uniform float uHard;\n"
         "uniform float uFlow;\n"
         "uniform int uTip;\n"
         "uniform float uSeed;\n"
         "uniform float uAngle;\n"
         "uniform float uRound;\n"
         "uniform int uPencil;\n"
         "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + uSeed) * 43758.5453); }\n"
         "void main() {\n"
         "   vec2 p = gl_FragCoord.xy - uCenter;\n"
         "   float c = cos(uAngle), s = sin(uAngle);\n"
         "   p = vec2(c * p.x + s * p.y, -s * p.x + c * p.y);\n"
         "   p.y /= max(uRound, 0.05);\n"
         "   float rad = max(uRadius, 0.5);\n"
         "   float r = length(p) / rad;\n"
         "   float aa = min(1.0, 1.0 / rad);\n"
         "   float a = 0.0;\n"
         "   if (uPencil == 1) {\n"
         "      a = (uTip == 3) ? step(max(abs(p.x), abs(p.y)), rad) : step(length(p), rad);\n"
         "   } else if (uTip == 0 || uTip == 1) {\n"
         "      a = 1.0 - smoothstep(min(uHard, 1.0 - aa), 1.0, r);\n"
         "   } else if (uTip == 2) {\n"
         "      a = 1.0 - smoothstep(min(0.94, 1.0 - aa), 1.0, r);\n"
         "   } else if (uTip == 3) {\n"
         "      float m = max(abs(p.x), abs(p.y)) / rad;\n"
         "      a = 1.0 - smoothstep(min(uHard, 1.0 - aa), 1.0, m);\n"
         "   } else if (uTip == 4) {\n"
         "      a = (1.0 - smoothstep(0.0, 1.0, r)) * step(0.55, hash(floor(gl_FragCoord.xy)));\n"
         "   } else if (uTip == 5) {\n"
         "      float n = hash(floor(gl_FragCoord.xy * 0.5)) * 0.55 + 0.45;\n"
         "      a = (1.0 - smoothstep(uHard * 0.6, 1.0, r)) * n;\n"
         "   } else {\n"
         "      float n = hash(floor(gl_FragCoord.xy * 0.125)) * 0.35;\n"
         "      a = 1.0 - smoothstep(uHard - n, 1.0 + n, r);\n"
         "   }\n"
         "   a = clamp(a, 0.0, 1.0) * uFlow;\n"
         "   fragColor = vec4(a);\n"
         "}\n";

      // The surface with the stroke's coverage applied, straight alpha.
      const char* kMergeFrag =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uSurf;\n"
         "uniform sampler2D uCov;\n"
         "uniform sampler2D uSel;\n"
         "uniform int uHasSel;\n"
         "uniform vec3 uColor;\n"
         "uniform float uOpacity;\n"
         "uniform int uMode;\n"
         "uniform int uLock;\n"
         "void main() {\n"
         "   vec4 d = texture(uSurf, vUv);\n"
         "   float a = clamp(texture(uCov, vUv).a * uOpacity, 0.0, 1.0);\n"
         "   if (uHasSel == 1) a *= texture(uSel, vUv).r;\n"
         "   if (a <= 0.0) { fragColor = d; return; }\n"
         "   if (uMode == 3) { fragColor = vec4(d.rgb, d.a * (1.0 - a)); return; }\n"
         "   if (uMode == 1) { fragColor = uLock == 1 ? d : vec4(d.rgb, d.a * (1.0 - a)); return; }\n"
         "   if (uMode == 2) {\n"
         "      if (uLock == 1) { fragColor = d; return; }\n"
         "      float oa = d.a + a * (1.0 - d.a);\n"
         "      vec3 rgb = (d.rgb * d.a + uColor * a * (1.0 - d.a)) / max(oa, 1e-4);\n"
         "      fragColor = vec4(rgb, oa);\n"
         "      return;\n"
         "   }\n"
         "   vec3 src = uColor;\n"
         "   if (uMode == 4) src = mix(uColor, uColor * d.rgb, d.a);\n"
         "   if (uMode == 5) src = mix(uColor, 1.0 - (1.0 - uColor) * (1.0 - d.rgb), d.a);\n"
         "   if (uLock == 1) { fragColor = vec4(mix(d.rgb, src, a), d.a); return; }\n"
         "   float oa = a + d.a * (1.0 - a);\n"
         "   vec3 rgb = (src * a + d.rgb * d.a * (1.0 - a)) / max(oa, 1e-4);\n"
         "   fragColor = vec4(rgb, oa);\n"
         "}\n";

      unsigned sDab = 0, sMerge = 0;
      bool sTried = false;
      struct DabLoc
      {
         int center, radius, hard, flow, tip, seed, angle, round, pencil;
      } sDabLoc;
      uint32_t sStrokeSeed = 0x9E3779B9u;

      float Rand(uint32_t& s)
      {
         s ^= s << 13;
         s ^= s >> 17;
         s ^= s << 5;
         return (float)(s & 0xFFFFFF) / (float)0x1000000;
      }

      Brush Make(int tip, float size, float hard, float opacity, float flow, float spacing)
      {
         Brush b;
         b.tip = tip;
         b.size = size;
         b.hardness = hard;
         b.opacity = opacity;
         b.flow = flow;
         b.spacing = spacing;
         return b;
      }
   }

   const std::vector<std::string>& TipNames()
   {
      static const std::vector<std::string> names = { "Round", "Soft Round", "Hard Round", "Square", "Spray", "Chalk", "Ink Bleed" };
      return names;
   }

   const std::vector<Preset>& Presets()
   {
      static const std::vector<Preset> presets = []()
      {
         std::vector<Preset> p;
         Brush b = Make(kRound, 3, 1, 1, 1, 0.05f);
         b.pencil = true;
         b.pressureSize = false;
         p.push_back({ "Pencil", b });
         b = Make(kRound, 6, 0.9f, 1, 1, 0.08f);
         b.smoothing = 0.3f;
         p.push_back({ "Ink", b });
         b = Make(kHardRound, 2, 1, 1, 1, 0.08f);
         b.pressureSize = false;
         b.smoothing = 0.2f;
         p.push_back({ "Fine liner", b });
         b = Make(kRound, 24, 0.7f, 0.6f, 1, 0.1f);
         b.pressureSize = false;
         p.push_back({ "Marker", b });
         b = Make(kSoftRound, 120, 0, 1, 0.08f, 0.1f);
         b.pressureSize = false;
         b.pressureOpacity = true;
         p.push_back({ "Soft airbrush", b });
         p.push_back({ "Hard round", Make(kRound, 30, 1, 1, 1, 0.12f) });
         b = Make(kChalk, 40, 0.5f, 1, 0.6f, 0.15f);
         p.push_back({ "Chalk", b });
         b = Make(kSpray, 80, 0, 1, 0.5f, 0.2f);
         b.jitter = 0.1f;
         p.push_back({ "Spray", b });
         b = Make(kInkBleed, 18, 0.4f, 0.9f, 0.7f, 0.12f);
         b.jitter = 0.05f;
         p.push_back({ "Charcoal", b });
         b = Make(kRound, 40, 0.9f, 1, 1, 0.06f);
         b.roundness = 0.2f;
         b.angle = 45.0f;
         p.push_back({ "Flat", b });
         b = Make(kRound, 14, 0.9f, 1, 1, 1.5f);
         b.jitter = 2.0f;
         b.pressureSize = false;
         p.push_back({ "Splatter", b });
         b = Make(kSoftRound, 60, 0, 1, 0.5f, 0.1f);
         b.mode = kErase;
         p.push_back({ "Eraser soft", b });
         b = Make(kRound, 30, 1, 1, 1, 0.1f);
         b.mode = kErase;
         b.pressureSize = false;
         p.push_back({ "Eraser hard", b });
         return p;
      }();
      return presets;
   }

   Stroke::~Stroke()
   {
      GLUtil::DestroyFbo(mCoverage);
      GLUtil::DestroyFbo(mDisplay);
      GLUtil::DestroyFbo(mMerge);
   }

   bool Stroke::EnsurePrograms()
   {
      if (!sTried)
      {
         sTried = true;
         sDab = GLUtil::CompileProgram(kDabFrag);
         sMerge = GLUtil::CompileProgram(kMergeFrag);
         if (sDab != 0)
         {
            sDabLoc.center = glGetUniformLocation(sDab, "uCenter");
            sDabLoc.radius = glGetUniformLocation(sDab, "uRadius");
            sDabLoc.hard = glGetUniformLocation(sDab, "uHard");
            sDabLoc.flow = glGetUniformLocation(sDab, "uFlow");
            sDabLoc.tip = glGetUniformLocation(sDab, "uTip");
            sDabLoc.seed = glGetUniformLocation(sDab, "uSeed");
            sDabLoc.angle = glGetUniformLocation(sDab, "uAngle");
            sDabLoc.round = glGetUniformLocation(sDab, "uRound");
            sDabLoc.pencil = glGetUniformLocation(sDab, "uPencil");
         }
      }
      return sDab != 0 && sMerge != 0;
   }

   void Stroke::Begin(const GLUtil::Fbo& surface, const Brush& b, float x, float y, float pressure, unsigned selection)
   {
      mActive = false;
      if (surface.fbo == 0 || !EnsurePrograms())
         return;
      mW = surface.w;
      mH = surface.h;
      if (!GLUtil::EnsureFbo(mCoverage, mW, mH, GL_RGBA16F, "BrushCoverage") ||
          !GLUtil::EnsureFbo(mDisplay, mW, mH, surface.internalFormat, "BrushDisplay") ||
          !GLUtil::EnsureFbo(mMerge, mW, mH, surface.internalFormat, "BrushMerge"))
         return;
      GLUtil::ClearFbo(mCoverage);
      mBrush = b;
      mBrush.size = std::max(0.5f, b.size);
      mSelection = selection;
      mActive = true;
      mAny = false;
      mDabCount = 0;
      mQueue.clear();
      sStrokeSeed = sStrokeSeed * 1664525u + 1013904223u;
      mRng = sStrokeSeed | 1u;
      const float p = pressure < 0.0f ? 1.0f : pressure;
      mLastX = mPenX = x;
      mLastY = mPenY = y;
      mLastP = mPenP = p;
      mCarry = 0.0f;
      Dab(x, y, p, Rand(mRng) * 100.0f);
      mDisplayStale = true;
   }

   void Stroke::Dab(float x, float y, float pressure, float seed)
   {
      if (!mActive)
         return;
      if (mBrush.jitter > 0.0f)
      {
         x += (Rand(mRng) - 0.5f) * mBrush.jitter * mBrush.size;
         y += (Rand(mRng) - 0.5f) * mBrush.jitter * mBrush.size;
      }
      const float p = pressure < 0.0f ? 1.0f : std::clamp(pressure, 0.0f, 1.0f);
      Queue(x, y, p, seed);
      for (const auto& m : mMirrors)
         Queue(m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5], p, seed);
   }

   void Stroke::Queue(float x, float y, float p, float seed)
   {
      mQueue.push_back({ x, y, p, seed });
      const float r = 0.5f * mBrush.size + 2.0f;
      if (!mAny)
      {
         mDirty[0] = x - r;
         mDirty[1] = y - r;
         mDirty[2] = x + r;
         mDirty[3] = y + r;
         mAny = true;
      }
      else
      {
         mDirty[0] = std::min(mDirty[0], x - r);
         mDirty[1] = std::min(mDirty[1], y - r);
         mDirty[2] = std::max(mDirty[2], x + r);
         mDirty[3] = std::max(mDirty[3], y + r);
      }
   }

   void Stroke::Walk(float x, float y, float pressure)
   {
      const float dx = x - mLastX, dy = y - mLastY;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d <= 1e-4f)
         return;
      const float scale = mBrush.pressureSize ? std::max(0.05f, std::max(mLastP, pressure)) : 1.0f;
      const float step = std::max(mBrush.pencil ? 1.0f : 0.5f, mBrush.size * scale * std::max(0.01f, mBrush.spacing));
      float t = step - mCarry;
      int guard = 0;
      while (t <= d && guard++ < 20000)
      {
         const float f = t / d;
         Dab(mLastX + dx * f, mLastY + dy * f, mLastP + (pressure - mLastP) * f, Rand(mRng) * 100.0f);
         t += step;
      }
      mCarry = d - (t - step);
      mLastX = x;
      mLastY = y;
      mLastP = pressure;
   }

   void Stroke::MoveTo(float x, float y, float pressure)
   {
      if (!mActive)
         return;
      const float p = pressure < 0.0f ? 1.0f : pressure;
      mPenX = x;
      mPenY = y;
      mPenP = p;
      if (mBrush.smoothing > 0.0f)
      {
         // The brush hangs on a string behind the pen and only moves once the string is taut.
         const float len = mBrush.smoothing * std::max(8.0f, mBrush.size) * 1.5f;
         const float dx = x - mLastX, dy = y - mLastY;
         const float d = std::sqrt(dx * dx + dy * dy);
         if (d <= len)
            return;
         const float f = (d - len) / d;
         Walk(mLastX + dx * f, mLastY + dy * f, p);
         return;
      }
      Walk(x, y, p);
   }

   void Stroke::Flush()
   {
      if (!mActive || mQueue.empty())
         return;
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      GLint srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0;
      glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
      glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
      glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
      glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      const Brush& b = mBrush;
      const float angle = b.angle * 3.14159265f / 180.0f;
      for (const Pending& q : mQueue)
      {
         const float scale = b.pressureSize ? std::max(0.05f, q.pressure) : 1.0f;
         float r = 0.5f * b.size * scale;
         float cx = q.x, cy = q.y;
         if (b.pencil)
         {
            r = std::max(0.5f, std::round(r * 2.0f) * 0.5f);
            cx = std::floor(cx) + 0.5f;
            cy = std::floor(cy) + 0.5f;
         }
         const float flow = b.flow * (b.pressureOpacity ? q.pressure : 1.0f);
         int sc[4] = { (int)std::floor(cx - r - 2), (int)std::floor(cy - r - 2), 0, 0 };
         sc[2] = (int)std::ceil(cx + r + 2) - sc[0];
         sc[3] = (int)std::ceil(cy + r + 2) - sc[1];
         const int x0 = std::max(0, sc[0]), y0 = std::max(0, sc[1]);
         const int x1 = std::min(mW, sc[0] + sc[2]), y1 = std::min(mH, sc[1] + sc[3]);
         if (x1 <= x0 || y1 <= y0)
            continue;
         const int clip[4] = { x0, y0, x1 - x0, y1 - y0 };
         GLUtil::DrawPass(mCoverage, sDab, [&]()
         {
            glUniform2f(sDabLoc.center, cx, cy);
            glUniform1f(sDabLoc.radius, r);
            glUniform1f(sDabLoc.hard, std::clamp(b.hardness, 0.0f, 0.98f));
            glUniform1f(sDabLoc.flow, std::clamp(flow, 0.0f, 1.0f));
            glUniform1i(sDabLoc.tip, b.tip);
            glUniform1f(sDabLoc.seed, q.seed);
            glUniform1f(sDabLoc.angle, angle);
            glUniform1f(sDabLoc.round, std::clamp(b.roundness, 0.05f, 1.0f));
            glUniform1i(sDabLoc.pencil, b.pencil ? 1 : 0);
         }, clip);
         mDabCount++;
      }
      mQueue.clear();
      glBlendFuncSeparate(srcRgb, dstRgb, srcA, dstA);
      if (!blendOn)
         glDisable(GL_BLEND);
      mDisplayStale = true;
   }

   void Stroke::Merge(const GLUtil::Fbo& surface, const GLUtil::Fbo& out)
   {
      GLboolean blendOn = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND);
      GLUtil::RunShaderPass(out, sMerge, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, surface.tex);
         glUniform1i(glGetUniformLocation(sMerge, "uSurf"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mCoverage.tex);
         glUniform1i(glGetUniformLocation(sMerge, "uCov"), 1);
         glActiveTexture(GL_TEXTURE2);
         glBindTexture(GL_TEXTURE_2D, mSelection);
         glUniform1i(glGetUniformLocation(sMerge, "uSel"), 2);
         glUniform1i(glGetUniformLocation(sMerge, "uHasSel"), mSelection != 0 ? 1 : 0);
         glUniform3f(glGetUniformLocation(sMerge, "uColor"), mBrush.color[0], mBrush.color[1], mBrush.color[2]);
         glUniform1f(glGetUniformLocation(sMerge, "uOpacity"), std::clamp(mBrush.opacity, 0.0f, 1.0f));
         glUniform1i(glGetUniformLocation(sMerge, "uMode"), mBrush.mode);
         glUniform1i(glGetUniformLocation(sMerge, "uLock"), mBrush.lockAlpha ? 1 : 0);
         glActiveTexture(GL_TEXTURE0);
      });
      if (blendOn)
         glEnable(GL_BLEND);
   }

   unsigned Stroke::DisplayTexture(const GLUtil::Fbo& surface)
   {
      if (!mActive)
         return surface.tex;
      Flush();
      if (mDisplayStale)
      {
         Merge(surface, mDisplay);
         mDisplayStale = false;
      }
      return mDisplay.tex;
   }

   void Stroke::End(GLUtil::Fbo& surface, int dirty[4])
   {
      dirty[0] = dirty[1] = dirty[2] = dirty[3] = 0;
      if (!mActive)
         return;
      // A smoothed stroke catches up to where the pen let go.
      if (mBrush.smoothing > 0.0f)
         Walk(mPenX, mPenY, mPenP);
      Flush();
      mActive = false;
      if (!mAny)
         return;
      const int x0 = std::clamp((int)std::floor(mDirty[0]), 0, mW), y0 = std::clamp((int)std::floor(mDirty[1]), 0, mH);
      const int x1 = std::clamp((int)std::ceil(mDirty[2]), 0, mW), y1 = std::clamp((int)std::ceil(mDirty[3]), 0, mH);
      if (x1 <= x0 || y1 <= y0)
         return;
      Merge(surface, mMerge);
      // Only the touched rectangle goes back, so the surface keeps its handle.
      GLint prevRead = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, mMerge.fbo);
      glBindTexture(GL_TEXTURE_2D, surface.tex);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x0, y0, x1 - x0, y1 - y0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
      dirty[0] = x0;
      dirty[1] = y0;
      dirty[2] = x1;
      dirty[3] = y1;
   }

   void Stroke::Cancel()
   {
      mActive = false;
      mQueue.clear();
   }
}

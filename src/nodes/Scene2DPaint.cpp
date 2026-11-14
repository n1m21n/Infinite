// Scene 2D paint: paint-layer surfaces and their tiles, pixel masks, the
// document selection, bucket, gradient and pick. Plan: internal/plans/2d-view (Block B).
#include "Scene2DNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "TileStore.h"

namespace
{
   constexpr int kT = TileStore::kTile;

   int Cols(int w) { return (w + kT - 1) / kT; }

   // Maps each texel of a surface to the document and either paints a fill
   // (solid or gradient, through a coverage and the selection) over the
   // surface (uOp 0), or writes the selection there as grey (uOp 1).
   const char* kFillFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSurf;\n"
      "uniform sampler2D uCov;\n"
      "uniform sampler2D uSel;\n"
      "uniform int uHasCov;\n"
      "uniform int uHasSel;\n"
      "uniform int uOp;\n"
      "uniform vec2 uSurfSize;\n"
      "uniform vec2 uBox;\n"
      "uniform vec2 uDoc;\n"
      "uniform vec3 uMA;\n"
      "uniform vec3 uMB;\n"
      "uniform vec4 uColA;\n"
      "uniform vec4 uColB;\n"
      "uniform int uKind;\n"
      "uniform vec2 uP0;\n"
      "uniform vec2 uP1;\n"
      "uniform float uOpacity;\n"
      "uniform int uLock;\n"
      "void main() {\n"
      "   vec2 sp = vUv * uSurfSize;\n"
      "   vec2 bp = vec2(sp.x * uBox.x / uSurfSize.x, (uSurfSize.y - sp.y) * uBox.y / uSurfSize.y);\n"
      "   vec2 dp = vec2(dot(uMA, vec3(bp, 1.0)), dot(uMB, vec3(bp, 1.0)));\n"
      "   vec2 duv = vec2(dp.x / uDoc.x, 1.0 - dp.y / uDoc.y);\n"
      "   bool inDoc = duv.x >= 0.0 && duv.y >= 0.0 && duv.x <= 1.0 && duv.y <= 1.0;\n"
      "   float sel = uHasSel == 1 ? (inDoc ? texture(uSel, duv).r : 0.0) : 1.0;\n"
      "   if (uOp == 1) { fragColor = vec4(vec3(sel), 1.0); return; }\n"
      "   vec4 d = texture(uSurf, vUv);\n"
      "   float cov = uHasCov == 1 ? (inDoc ? texture(uCov, duv).r : 0.0) : 1.0;\n"
      "   vec4 c = uColA;\n"
      "   if (uKind == 1) {\n"
      "      vec2 ab = uP1 - uP0;\n"
      "      c = mix(uColA, uColB, clamp(dot(dp - uP0, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0));\n"
      "   } else if (uKind == 2) {\n"
      "      c = mix(uColA, uColB, clamp(length(dp - uP0) / max(length(uP1 - uP0), 1e-3), 0.0, 1.0));\n"
      "   }\n"
      "   float a = clamp(c.a * cov * sel * uOpacity, 0.0, 1.0);\n"
      "   if (uLock == 1) { fragColor = vec4(mix(d.rgb, c.rgb, a), d.a); return; }\n"
      "   float oa = a + d.a * (1.0 - a);\n"
      "   fragColor = vec4((c.rgb * a + d.rgb * d.a * (1.0 - a)) / max(oa, 1e-4), oa);\n"
      "}\n";

   // Marching ants on the selection's edge, or the quick mask's red tint.
   const char* kAntsFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSel;\n"
      "uniform vec2 uPx;\n"
      "uniform float uTime;\n"
      "uniform int uQuick;\n"
      "void main() {\n"
      "   float v = texture(uSel, vUv).r;\n"
      "   if (uQuick == 1) { fragColor = vec4(1.0, 0.0, 0.0, (1.0 - v) * 0.5); return; }\n"
      "   bool inside = v >= 0.5;\n"
      "   bool edge = false;\n"
      "   for (int i = 0; i < 4; i++) {\n"
      "      vec2 o = i == 0 ? vec2(uPx.x, 0.0) : i == 1 ? vec2(-uPx.x, 0.0) : i == 2 ? vec2(0.0, uPx.y) : vec2(0.0, -uPx.y);\n"
      "      edge = edge || ((texture(uSel, vUv + o).r >= 0.5) != inside);\n"
      "   }\n"
      "   if (!edge) { fragColor = vec4(0.0); return; }\n"
      "   float s = mod(floor((gl_FragCoord.x + gl_FragCoord.y) / 4.0 + uTime * 8.0), 2.0);\n"
      "   fragColor = vec4(vec3(s), 1.0);\n"
      "}\n";

   void ReadRect(const GLUtil::Fbo& f, int x, int y, int w, int h, std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * (size_t)h * 4, 0);
      GLint prev = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, f.fbo);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
   }

   void WriteRect(const GLUtil::Fbo& f, int x, int y, int w, int h, const uint8_t* px)
   {
      glBindTexture(GL_TEXTURE_2D, f.tex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glBindTexture(GL_TEXTURE_2D, 0);
   }

   // Box blur, three times: close to a gaussian of the same radius.
   void Feather(std::vector<uint8_t>& a, int w, int h, float radius)
   {
      const int r = (int)std::lround(radius / 1.7f);
      if (r < 1)
         return;
      std::vector<float> buf(a.begin(), a.end()), tmp(buf.size());
      for (int pass = 0; pass < 3; pass++)
      {
         for (int y = 0; y < h; y++)
         {
            float sum = 0;
            const float* row = &buf[(size_t)y * w];
            for (int x = -r; x <= r; x++)
               sum += row[std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; x++)
            {
               tmp[(size_t)y * w + x] = sum / (2 * r + 1);
               sum += row[std::min(w - 1, x + r + 1)] - row[std::max(0, x - r)];
            }
         }
         for (int x = 0; x < w; x++)
         {
            float sum = 0;
            for (int y = -r; y <= r; y++)
               sum += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
            for (int y = 0; y < h; y++)
            {
               buf[(size_t)y * w + x] = sum / (2 * r + 1);
               sum += tmp[(size_t)std::min(h - 1, y + r + 1) * w + x] - tmp[(size_t)std::max(0, y - r) * w + x];
            }
         }
      }
      for (size_t i = 0; i < a.size(); i++)
         a[i] = (uint8_t)std::clamp((int)std::lround(buf[i]), 0, 255);
   }

   bool Near(const uint8_t* a, const uint8_t* b, int tol)
   {
      for (int c = 0; c < 4; c++)
         if (std::abs((int)a[c] - (int)b[c]) > tol)
            return false;
      return true;
   }

   // Which pixels (document size, bottom row first) match the seed: flood or everywhere.
   void MatchRegion(const std::vector<uint8_t>& px, int w, int h, int sx, int sy, int tol, bool contiguous,
                    std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * h, 0);
      if (sx < 0 || sy < 0 || sx >= w || sy >= h)
         return;
      const uint8_t* seed = &px[((size_t)sy * w + sx) * 4];
      uint8_t ref[4];
      std::memcpy(ref, seed, 4);
      if (!contiguous)
      {
         for (size_t i = 0; i < out.size(); i++)
            out[i] = Near(&px[i * 4], ref, tol) ? 255 : 0;
         return;
      }
      std::vector<int> stack;
      stack.push_back(sy * w + sx);
      out[(size_t)sy * w + sx] = 255;
      while (!stack.empty())
      {
         const int i = stack.back();
         stack.pop_back();
         const int x = i % w, y = i / w;
         const int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
         for (int n : nb)
            if (n >= 0 && out[n] == 0 && Near(&px[(size_t)n * 4], ref, tol))
            {
               out[n] = 255;
               stack.push_back(n);
            }
      }
   }
}

// ---- layers --------------------------------------------------------------

int Scene2DNode::AddPaintLayer(int above)
{
   return AddLayer(Kind::Paint, 0, above).id;
}

bool Scene2DNode::AddPixelMask(int id)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind == Kind::Group)
      return false;
   if (!l->HasPixelMask())
   {
      float bw = 1, bh = 1;
      LayerBox(*l, bw, bh);
      l->mw = std::clamp((int)std::lround(bw), 1, GLUtil::kMaxTargetSide);
      l->mh = std::clamp((int)std::lround(bh), 1, GLUtil::kMaxTargetSide);
      l->mtiles.assign((size_t)Cols(l->mw) * Cols(l->mh), "-");
      l->pmaskOn = true;
   }
   Commit();
   return true;
}

void Scene2DNode::RemovePixelMask(int id)
{
   if (Layer* l = FindLayer(id))
   {
      l->mw = l->mh = 0;
      l->mtiles.clear();
      Commit();
   }
}

bool Scene2DNode::MaskFromSelection(int id)
{
   if (!AddPixelMask(id) || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   Layer* l = FindLayer(id);
   auto it = mMasks.find(id);
   if (l == nullptr || it == mMasks.end())
      return false;
   if (mSelAny)
   {
      UploadSelection();
      ApplyFill(it->second, l->mw, l->mh, *l, 0, nullptr, nullptr, -1, nullptr, nullptr, 1.0f);
   }
   const int all[4] = { 0, 0, l->mw, l->mh };
   StoreTiles(it->second, l->mtiles, all);
   l = FindLayer(id);
   Commit();
   return true;
}

// ---- surfaces ------------------------------------------------------------

Scene2DNode::Surface* Scene2DNode::SyncSurface(std::map<int, Surface>& in, int id, int w, int h,
                                               const std::vector<std::string>& keys)
{
   if (w <= 0 || h <= 0)
      return nullptr;
   const bool mask = &in == &mMasks;
   Surface& s = in[id];
   if (s.fbo.w != w || s.fbo.h != h || s.fbo.tex == 0)
   {
      if (!GLUtil::EnsureFbo(s.fbo, w, h, GL_RGBA8, mask ? "Scene2DMask" : "Scene2DPaint"))
         return nullptr;
      s.loaded.assign(keys.size(), "?");
   }
   if (s.loaded.size() != keys.size())
      s.loaded.assign(keys.size(), "?");
   const int cols = Cols(w);
   std::vector<uint8_t> px;
   for (size_t i = 0; i < keys.size(); i++)
   {
      if (keys[i] == s.loaded[i])
         continue;
      const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
      const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
      if (tw <= 0 || th <= 0)
         continue;
      int gw = 0, gh = 0;
      if (keys[i] == "-" || !TileStore::Get(keys[i], px, gw, gh) || gw != tw || gh != th)
         px.assign((size_t)tw * th * 4, mask ? 255 : 0);
      WriteRect(s.fbo, x0, y0, tw, th, px.data());
      s.loaded[i] = keys[i];
      mPaintEdits++;
   }
   return &s;
}

void Scene2DNode::SyncSurfaces()
{
   std::vector<int> paintIds, maskIds;
   for (const Layer& l : mLayers)
   {
      if ((l.kind == Kind::Paint || l.HasSkin()) && SyncSurface(mSurfaces, l.id, l.pw, l.ph, l.tiles) != nullptr)
         paintIds.push_back(l.id);
      if (l.HasPixelMask() && SyncSurface(mMasks, l.id, l.mw, l.mh, l.mtiles) != nullptr)
         maskIds.push_back(l.id);
   }
   for (auto* m : { &mSurfaces, &mMasks })
   {
      const std::vector<int>& keep = m == &mSurfaces ? paintIds : maskIds;
      for (auto it = m->begin(); it != m->end();)
      {
         if (std::find(keep.begin(), keep.end(), it->first) == keep.end() && !(mStroke.Active() && it->first == mPaintId))
         {
            GLUtil::DestroyFbo(it->second.fbo);
            it = m->erase(it);
         }
         else
            ++it;
      }
   }
}

void Scene2DNode::StoreTiles(Surface& s, std::vector<std::string>& keys, const int dirty[4])
{
   const int w = s.fbo.w, h = s.fbo.h, cols = Cols(w);
   const bool mask = !mMasks.empty() && std::any_of(mMasks.begin(), mMasks.end(), [&](const auto& kv) { return &kv.second == &s; });
   std::vector<uint8_t> px;
   for (size_t i = 0; i < keys.size(); i++)
   {
      const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
      const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
      if (tw <= 0 || th <= 0 || x0 >= dirty[2] || y0 >= dirty[3] || x0 + tw <= dirty[0] || y0 + th <= dirty[1])
         continue;
      ReadRect(s.fbo, x0, y0, tw, th, px);
      bool white = mask;
      for (size_t k = 0; white && k < px.size(); k++)
         white = px[k] == 255;
      keys[i] = white ? "-" : TileStore::Put(px.data(), tw, th);
      s.loaded[i] = keys[i];
   }
   mPaintEdits++;
}

bool Scene2DNode::DocToSurface(const Layer& l, int target, float x, float y, float& sx, float& sy, float& scale)
{
   if (target == 2)
   {
      sx = x;
      sy = (float)mDoc.h - y;
      scale = 1.0f;
      return true;
   }
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   const float sw = (float)(target == 0 ? l.pw : l.mw), sh = (float)(target == 0 ? l.ph : l.mh);
   if (sw <= 0 || sh <= 0)
      return false;
   // Through the card's homography; the brush scale is the local one (a 3D card shrinks with distance).
   float bx = 0, by = 0, ux = 0, uy = 0, vx = 0, vy = 0;
   DocToBox(l, x, y, bx, by);
   DocToBox(l, x + 1.0f, y, ux, uy);
   DocToBox(l, x, y + 1.0f, vx, vy);
   sx = bx * sw / bw;
   sy = sh - by * sh / bh;
   scale = std::sqrt(std::fabs((ux - bx) * (vy - by) - (uy - by) * (vx - bx))) * sw / bw;
   return true;
}

bool Scene2DNode::EnsurePaintShaders()
{
   if (!mPaintTried)
   {
      mPaintTried = true;
      mFillProgram = GLUtil::CompileProgram(kFillFrag);
      mAntsProgram = GLUtil::CompileProgram(kAntsFrag);
   }
   return mFillProgram != 0 && mAntsProgram != 0;
}

void Scene2DNode::ApplyFill(Surface& s, int sw, int sh, const Layer& l, unsigned coverage, const float colA[4],
                            const float colB[4], int kind, const float p0[2], const float p1[2], float opacity)
{
   // kind -1: write the selection as grey (mask from selection).
   GLUtil::Fbo out;
   if (!GLUtil::EnsureFbo(out, sw, sh, GL_RGBA8, "Scene2DFill"))
      return;
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   float m[6];
   LayerMatrix(l, m);
   const unsigned prog = mFillProgram;
   const unsigned sel = mSelAny ? mSelTex.tex : 0;
   const float zero[4] = { 0, 0, 0, 0 };
   GLUtil::RunShaderPass(out, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, s.fbo.tex);
      glUniform1i(glGetUniformLocation(prog, "uSurf"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, coverage);
      glUniform1i(glGetUniformLocation(prog, "uCov"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, sel);
      glUniform1i(glGetUniformLocation(prog, "uSel"), 2);
      glActiveTexture(GL_TEXTURE0);
      glUniform1i(glGetUniformLocation(prog, "uHasCov"), coverage != 0 ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uHasSel"), sel != 0 ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uOp"), kind < 0 ? 1 : 0);
      glUniform2f(glGetUniformLocation(prog, "uSurfSize"), (float)sw, (float)sh);
      glUniform2f(glGetUniformLocation(prog, "uBox"), bw, bh);
      glUniform2f(glGetUniformLocation(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform3f(glGetUniformLocation(prog, "uMA"), m[0], m[1], m[2]);
      glUniform3f(glGetUniformLocation(prog, "uMB"), m[3], m[4], m[5]);
      glUniform4fv(glGetUniformLocation(prog, "uColA"), 1, colA != nullptr ? colA : zero);
      glUniform4fv(glGetUniformLocation(prog, "uColB"), 1, colB != nullptr ? colB : zero);
      glUniform1i(glGetUniformLocation(prog, "uKind"), std::max(0, kind));
      glUniform2f(glGetUniformLocation(prog, "uP0"), p0 != nullptr ? p0[0] : 0.0f, p0 != nullptr ? p0[1] : 0.0f);
      glUniform2f(glGetUniformLocation(prog, "uP1"), p1 != nullptr ? p1[0] : 1.0f, p1 != nullptr ? p1[1] : 1.0f);
      glUniform1f(glGetUniformLocation(prog, "uOpacity"), std::clamp(opacity, 0.0f, 1.0f));
      glUniform1i(glGetUniformLocation(prog, "uLock"), (l.lock & kLockAlpha) != 0 ? 1 : 0);
   });
   GLint prevRead = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, out.fbo);
   glBindTexture(GL_TEXTURE_2D, s.fbo.tex);
   glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, sw, sh);
   glBindTexture(GL_TEXTURE_2D, 0);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
   GLUtil::DestroyFbo(out);
}

// ---- painting --------------------------------------------------------------

bool Scene2DNode::BeginPaint(int id, int target, const BrushEngine::Brush& b, float x, float y, float pressure)
{
   Layers();
   if (mStroke.Active())
      EndPaint();
   BrushEngine::Brush brush = b;
   GLUtil::Fbo* surf = nullptr;
   const Layer* l = FindConst(id);
   if (target == 2)
   {
      if (mSel.size() != (size_t)mDoc.w * mDoc.h)
      {
         mSel.assign((size_t)mDoc.w * mDoc.h, 0);
         mSelDirty = true;
      }
      UploadSelection();
      surf = &mSelTex;
   }
   else
   {
      if (l == nullptr || (l->lock & (kLockPixels | kLockAll)) != 0)
         return false;
      if (target == 0 && l->kind != Kind::Paint)
         return false;
      if (target == 1 && !l->HasPixelMask())
         return false;
      SyncSurfaces();
      auto& m = target == 0 ? mSurfaces : mMasks;
      auto it = m.find(id);
      if (it == m.end())
         return false;
      surf = &it->second.fbo;
      if (target == 0)
         brush.lockAlpha = brush.lockAlpha || (l->lock & kLockAlpha) != 0;
   }
   if (target != 0)
   {
      // Masks hold grey: the eraser shows (or, on the quick mask, takes away).
      if (brush.mode == BrushEngine::kErase)
      {
         const float v = target == 2 ? 0.0f : 1.0f;
         brush.color[0] = brush.color[1] = brush.color[2] = v;
      }
      brush.mode = BrushEngine::kNormal;
      brush.lockAlpha = false;
      const float g = (brush.color[0] + brush.color[1] + brush.color[2]) / 3.0f;
      brush.color[0] = brush.color[1] = brush.color[2] = g;
   }
   float sx = 0, sy = 0, scale = 1;
   if (!DocToSurface(target == 2 ? Layer() : *l, target, x, y, sx, sy, scale))
      return false;
   brush.size *= scale;
   if (brush.mode == BrushEngine::kClone || brush.mode == BrushEngine::kHeal)
   {
      // The source offset arrives in document pixels; the shader wants it on this surface.
      float ox = 0, oy = 0, s2 = 1;
      if (DocToSurface(target == 2 ? Layer() : *l, target, x + b.sourceOffset[0], y + b.sourceOffset[1], ox, oy, s2))
      {
         brush.sourceOffset[0] = ox - sx;
         brush.sourceOffset[1] = oy - sy;
      }
   }
   // Paint lands only inside the selection, resampled to the surface when it is placed differently.
   unsigned sel = 0;
   if (target != 2 && mSelAny && EnsurePaintShaders())
   {
      UploadSelection();
      if (GLUtil::EnsureFbo(mSelScratch, surf->w, surf->h, GL_RGBA8, "Scene2DSelResample"))
      {
         Surface tmp;
         tmp.fbo = mSelScratch;
         ApplyFill(tmp, surf->w, surf->h, *l, 0, nullptr, nullptr, -1, nullptr, nullptr, 1.0f);
         sel = mSelScratch.tex;
      }
   }
   mPaintId = id;
   mPaintTarget = target;
   mStroke.SetMirrors(SurfaceMirrors(target == 2 ? Layer() : *l, target));
   mStroke.Begin(*surf, brush, sx, sy, pressure, sel);
   return mStroke.Active();
}

void Scene2DNode::PaintTo(float x, float y, float pressure)
{
   if (!mStroke.Active())
      return;
   const Layer* l = FindConst(mPaintId);
   if (mPaintTarget != 2 && l == nullptr)
      return;
   float sx = 0, sy = 0, scale = 1;
   if (DocToSurface(mPaintTarget == 2 ? Layer() : *l, mPaintTarget, x, y, sx, sy, scale))
      mStroke.MoveTo(sx, sy, pressure);
}

void Scene2DNode::PaintFollow(const BrushEngine::Brush& b)
{
   if (!mStroke.Active() || mPaintTarget == 2)
      return;
   const Layer* l = FindConst(mPaintId);
   const BrushEngine::Brush& cur = mStroke.CurrentBrush();
   float sx = 0, sy = 0, scale = 1;
   if (l == nullptr || !DocToSurface(*l, mPaintTarget, 0.0f, 0.0f, sx, sy, scale))
      return;
   if (cur.size != b.size * scale || cur.opacity != b.opacity || cur.flow != b.flow)
      mStroke.Follow(b.size * scale, b.opacity, b.flow);
}

void Scene2DNode::EndPaint()
{
   mSkinStroke.on = false;
   if (!mStroke.Active())
      return;
   Layers();
   int dirty[4];
   if (mPaintTarget == 2)
   {
      mStroke.End(mSelTex, dirty);
      std::vector<uint8_t> px;
      ReadRect(mSelTex, 0, 0, mDoc.w, mDoc.h, px);
      mSel.resize((size_t)mDoc.w * mDoc.h);
      mSelAny = false;
      for (size_t i = 0; i < mSel.size(); i++)
      {
         mSel[i] = px[i * 4];
         mSelAny = mSelAny || mSel[i] != 0;
      }
      mPaintId = 0;
      mPaintEdits++;
      return;
   }
   Layer* l = FindLayer(mPaintId);
   auto& m = mPaintTarget == 0 ? mSurfaces : mMasks;
   auto it = m.find(mPaintId);
   if (l == nullptr || it == m.end())
   {
      mStroke.Cancel();
      mPaintId = 0;
      return;
   }
   mStroke.End(it->second.fbo, dirty);
   if (dirty[2] > dirty[0] && dirty[3] > dirty[1])
   {
      StoreTiles(it->second, mPaintTarget == 0 ? l->tiles : l->mtiles, dirty);
      Commit();
   }
   mPaintId = 0;
}

bool Scene2DNode::Bucket(int id, float x, float y, const float rgb[3], float opacity, int tolerance, bool contiguous, bool sampleAll)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (l->lock & (kLockPixels | kLockAll)) != 0 || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end() || mOut.fbo == 0)
      return false;
   const int W = mDoc.w, H = mDoc.h;
   // The region is found in the document: from the picture, or from the layer drawn alone.
   std::vector<uint8_t> px;
   float m[6];
   LayerMatrix(*l, m);
   const bool aligned = std::fabs(m[0] - 1) < 1e-4f && std::fabs(m[4] - 1) < 1e-4f && std::fabs(m[1]) < 1e-4f &&
                        std::fabs(m[3]) < 1e-4f && std::fabs(m[2]) < 1e-3f && std::fabs(m[5]) < 1e-3f && l->pw == W && l->ph == H;
   if (sampleAll || !aligned)
      ReadRect(mOut, 0, 0, W, H, px);
   else
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   std::vector<uint8_t> region;
   MatchRegion(px, W, H, (int)std::floor(x), H - 1 - (int)std::floor(y), tolerance, contiguous, region);
   if (std::none_of(region.begin(), region.end(), [](uint8_t v) { return v != 0; }))
      return false;
   GLUtil::Fbo cov;
   if (!GLUtil::EnsureFbo(cov, W, H, GL_RGBA8, "Scene2DBucket"))
      return false;
   std::vector<uint8_t> rgba((size_t)W * H * 4);
   for (size_t i = 0; i < region.size(); i++)
      rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = region[i];
   WriteRect(cov, 0, 0, W, H, rgba.data());
   UploadSelection();
   const float col[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
   ApplyFill(it->second, l->pw, l->ph, *l, cov.tex, col, col, 0, nullptr, nullptr, opacity);
   GLUtil::DestroyFbo(cov);
   const int all[4] = { 0, 0, l->pw, l->ph };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::Gradient(int id, float ax, float ay, float bx, float by, const float a[4], const float b[4], bool radial)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Paint || (l->lock & (kLockPixels | kLockAll)) != 0 || !EnsurePaintShaders())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   UploadSelection();
   const float p0[2] = { ax, ay }, p1[2] = { bx, by };
   ApplyFill(it->second, l->pw, l->ph, *l, 0, a, b, radial ? 2 : 1, p0, p1, 1.0f);
   const int all[4] = { 0, 0, l->pw, l->ph };
   StoreTiles(it->second, l->tiles, all);
   Commit();
   return true;
}

bool Scene2DNode::Pick(float x, float y, float rgb[3])
{
   const int px = (int)std::floor(x), py = mDoc.h - 1 - (int)std::floor(y);
   if (mOut.fbo == 0 || px < 0 || py < 0 || px >= mOut.w || py >= mOut.h)
      return false;
   float v[4] = { 0, 0, 0, 0 };
   GLint prev = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, mOut.fbo);
   glReadPixels(px, py, 1, 1, GL_RGBA, GL_FLOAT, v);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
   for (int i = 0; i < 3; i++)
      rgb[i] = std::clamp(v[i], 0.0f, 1.0f);
   return true;
}

bool Scene2DNode::ReadOutput(std::vector<uint8_t>& rgba, int& w, int& h)
{
   if (mOut.fbo == 0)
      return false;
   w = mOut.w;
   h = mOut.h;
   ReadRect(mOut, 0, 0, w, h, rgba);
   return true;
}

bool Scene2DNode::ReadPaint(int id, std::vector<uint8_t>& rgba, int& w, int& h)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Paint)
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   w = it->second.fbo.w;
   h = it->second.fbo.h;
   ReadRect(it->second.fbo, 0, 0, w, h, rgba);
   return true;
}

// ---- selection -------------------------------------------------------------

namespace
{
   // Even-odd scanline fill of a polygon given in document px (y down), into a bottom-row-first buffer.
   void FillPolygon(const std::vector<float>& xy, int w, int h, std::vector<uint8_t>& out)
   {
      out.assign((size_t)w * h, 0);
      const size_t n = xy.size() / 2;
      if (n < 3)
         return;
      std::vector<float> xs;
      for (int y = 0; y < h; y++)
      {
         const float cy = (float)y + 0.5f;
         xs.clear();
         for (size_t i = 0; i < n; i++)
         {
            const float x0 = xy[i * 2], y0 = xy[i * 2 + 1];
            const float x1 = xy[((i + 1) % n) * 2], y1 = xy[((i + 1) % n) * 2 + 1];
            if ((y0 <= cy && y1 > cy) || (y1 <= cy && y0 > cy))
               xs.push_back(x0 + (cy - y0) / (y1 - y0) * (x1 - x0));
         }
         std::sort(xs.begin(), xs.end());
         uint8_t* row = &out[(size_t)(h - 1 - y) * w];
         for (size_t k = 0; k + 1 < xs.size(); k += 2)
         {
            const int a = std::clamp((int)std::lround(xs[k]), 0, w), b = std::clamp((int)std::lround(xs[k + 1]), 0, w);
            if (b > a)
               std::memset(row + a, 255, (size_t)(b - a));
         }
      }
   }
}

void Scene2DNode::SelectPolygon(const std::vector<float>& xy, int op, float feather)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> t;
   FillPolygon(xy, W, H, t);
   if (feather > 0.5f)
      Feather(t, W, H, feather);
   if (mSel.size() != t.size())
      mSel.assign(t.size(), 0);
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(t.size(), 0) : t;
   else
      for (size_t i = 0; i < t.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], t[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - t[i]) / 255);
         else mSel[i] = std::min(mSel[i], t[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
}

void Scene2DNode::SelectRect(float x0, float y0, float x1, float y1, int op, float feather, bool ellipse)
{
   if (x1 < x0) std::swap(x0, x1);
   if (y1 < y0) std::swap(y0, y1);
   std::vector<float> xy;
   if (ellipse)
   {
      const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, rx = (x1 - x0) * 0.5f, ry = (y1 - y0) * 0.5f;
      for (int i = 0; i < 128; i++)
      {
         const float a = (float)i / 128.0f * 6.2831853f;
         xy.push_back(cx + std::cos(a) * rx);
         xy.push_back(cy + std::sin(a) * ry);
      }
   }
   else
      xy = { x0, y0, x1, y0, x1, y1, x0, y1 };
   SelectPolygon(xy, op, feather);
}

void Scene2DNode::SelectWand(float x, float y, int tolerance, bool contiguous, int op, bool sampleAll, int layerId)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   std::vector<uint8_t> px;
   const Layer* l = FindConst(layerId);
   auto it = l != nullptr && l->kind == Kind::Paint ? mSurfaces.find(layerId) : mSurfaces.end();
   if (!sampleAll && it != mSurfaces.end() && l->pw == W && l->ph == H)
      ReadRect(it->second.fbo, 0, 0, W, H, px);
   else if (mOut.fbo != 0 && mOut.w == W && mOut.h == H)
      ReadRect(mOut, 0, 0, W, H, px);
   else
      return;
   std::vector<uint8_t> region;
   MatchRegion(px, W, H, (int)std::floor(x), H - 1 - (int)std::floor(y), tolerance, contiguous, region);
   if (mSel.size() != region.size())
      mSel.assign(region.size(), 0);
   if (op == kSelNew || !mSelAny)
      mSel = op == kSelSub || op == kSelIntersect ? std::vector<uint8_t>(region.size(), 0) : region;
   else
      for (size_t i = 0; i < region.size(); i++)
      {
         if (op == kSelAdd) mSel[i] = std::max(mSel[i], region[i]);
         else if (op == kSelSub) mSel[i] = (uint8_t)(mSel[i] * (255 - region[i]) / 255);
         else mSel[i] = std::min(mSel[i], region[i]);
      }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
}

void Scene2DNode::SelectAll()
{
   Layers();
   mSel.assign((size_t)mDoc.w * mDoc.h, 255);
   mSelAny = true;
   mSelDirty = true;
}

void Scene2DNode::Deselect()
{
   mSel.clear();
   mSelAny = false;
   mSelDirty = true;
}

void Scene2DNode::InvertSelection()
{
   Layers();
   if (mSel.size() != (size_t)mDoc.w * mDoc.h)
      mSel.assign((size_t)mDoc.w * mDoc.h, 0);
   for (uint8_t& v : mSel)
      v = (uint8_t)(255 - v);
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
}

void Scene2DNode::ModifySelection(int op, int px)
{
   const int w = mDoc.w, h = mDoc.h, r = std::clamp(px, 1, 100);
   if (!mSelAny || mSel.size() != (size_t)w * h)
      return;
   // Separable max (grow) or min (shrink) over a (2rr+1) square, a pass per axis.
   auto morph = [&](const std::vector<uint8_t>& src, bool grow, int rr) {
      std::vector<uint8_t> tmp(src.size()), dst(src.size());
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            uint8_t v = grow ? 0 : 255;
            for (int k = std::max(0, x - rr); k <= std::min(w - 1, x + rr); k++)
               v = grow ? std::max(v, src[(size_t)y * w + k]) : std::min(v, src[(size_t)y * w + k]);
            tmp[(size_t)y * w + x] = v;
         }
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
         {
            uint8_t v = grow ? 0 : 255;
            for (int k = std::max(0, y - rr); k <= std::min(h - 1, y + rr); k++)
               v = grow ? std::max(v, tmp[(size_t)k * w + x]) : std::min(v, tmp[(size_t)k * w + x]);
            dst[(size_t)y * w + x] = v;
         }
      return dst;
   };
   if (op == 0)
      mSel = morph(mSel, true, r);
   else if (op == 1)
      mSel = morph(mSel, false, r);
   else if (op == 2)
   {
      // Box blur (running sums, a pass per axis) then a hard cut at half: corners round, specks under r go.
      std::vector<int> tmp((size_t)w * h);
      for (int y = 0; y < h; y++)
      {
         int sum = 0;
         for (int x = -r; x <= r; x++)
            sum += mSel[(size_t)y * w + std::clamp(x, 0, w - 1)];
         for (int x = 0; x < w; x++)
         {
            tmp[(size_t)y * w + x] = sum;
            sum += mSel[(size_t)y * w + std::min(w - 1, x + r + 1)] - mSel[(size_t)y * w + std::max(0, x - r)];
         }
      }
      const long long n = (long long)(2 * r + 1) * (2 * r + 1);
      for (int x = 0; x < w; x++)
      {
         long long sum = 0;
         for (int y = -r; y <= r; y++)
            sum += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
         for (int y = 0; y < h; y++)
         {
            mSel[(size_t)y * w + x] = sum * 2 >= n * 255 ? 255 : 0;
            sum += tmp[(size_t)std::min(h - 1, y + r + 1) * w + x] - tmp[(size_t)std::max(0, y - r) * w + x];
         }
      }
   }
   else
   {
      // A ring r wide straddling the edge: grown by half, minus shrunk by half.
      const int half = std::max(1, r / 2);
      const std::vector<uint8_t> out = morph(mSel, true, half), in = morph(mSel, false, half);
      for (size_t i = 0; i < mSel.size(); i++)
         mSel[i] = (uint8_t)(out[i] * (255 - in[i]) / 255);
   }
   mSelAny = std::any_of(mSel.begin(), mSel.end(), [](uint8_t v) { return v != 0; });
   mSelDirty = true;
}

float Scene2DNode::SelectionAt(int x, int y) const
{
   if (!mSelAny || x < 0 || y < 0 || x >= mDoc.w || y >= mDoc.h || mSel.size() != (size_t)mDoc.w * mDoc.h)
      return 0.0f;
   return mSel[(size_t)(mDoc.h - 1 - y) * mDoc.w + x] / 255.0f;
}

bool Scene2DNode::SelectionBounds(float lo[2], float hi[2]) const
{
   if (!mSelAny || mSel.size() != (size_t)mDoc.w * mDoc.h)
      return false;
   int x0 = mDoc.w, y0 = mDoc.h, x1 = -1, y1 = -1;
   for (int r = 0; r < mDoc.h; r++)
   {
      const uint8_t* row = &mSel[(size_t)r * mDoc.w];
      for (int x = 0; x < mDoc.w; x++)
         if (row[x] >= 128)
         {
            const int y = mDoc.h - 1 - r;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
         }
   }
   if (x1 < 0)
      return false;
   lo[0] = (float)x0;
   lo[1] = (float)y0;
   hi[0] = (float)x1 + 1;
   hi[1] = (float)y1 + 1;
   return true;
}

void Scene2DNode::UploadSelection()
{
   const int W = mDoc.w, H = mDoc.h;
   if (mSelTex.w != W || mSelTex.h != H)
      mSelDirty = true;
   if (!GLUtil::EnsureFbo(mSelTex, W, H, GL_RGBA8, "Scene2DSelection"))
      return;
   if (!mSelDirty)
      return;
   mSelDirty = false;
   std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
   if (mSel.size() == (size_t)W * H)
      for (size_t i = 0; i < mSel.size(); i++)
      {
         rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = mSel[i];
         rgba[i * 4 + 3] = 255;
      }
   WriteRect(mSelTex, 0, 0, W, H, rgba.data());
}

unsigned Scene2DNode::SelectionTexture()
{
   if (!mSelAny && !quickMask)
      return 0;
   UploadSelection();
   return mSelTex.tex;
}

unsigned Scene2DNode::SelectionOverlay(float time, bool quick)
{
   if ((!mSelAny && !quick) || !EnsurePaintShaders())
      return 0;
   if (mSel.size() != (size_t)mDoc.w * mDoc.h)
   {
      mSel.assign((size_t)mDoc.w * mDoc.h, 0);
      mSelDirty = true;
   }
   UploadSelection();
   if (!GLUtil::EnsureFbo(mSelOverlay, mDoc.w, mDoc.h, GL_RGBA8, "Scene2DAnts"))
      return 0;
   const bool live = mStroke.Active() && mPaintTarget == 2;
   const unsigned sel = live ? mStroke.DisplayTexture(mSelTex) : mSelTex.tex;
   const unsigned prog = mAntsProgram;
   GLUtil::RunShaderPass(mSelOverlay, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sel);
      glUniform1i(glGetUniformLocation(prog, "uSel"), 0);
      glUniform2f(glGetUniformLocation(prog, "uPx"), 1.0f / (float)mDoc.w, 1.0f / (float)mDoc.h);
      glUniform1f(glGetUniformLocation(prog, "uTime"), time);
      glUniform1i(glGetUniformLocation(prog, "uQuick"), quick ? 1 : 0);
   });
   return mSelOverlay.tex;
}

unsigned Scene2DNode::PaintTexture(int id)
{
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   return it == mSurfaces.end() ? 0u : it->second.fbo.tex;
}

#include "SketchNode.h"

#include "core/gl3.h"
#include "core/sketch/SketchPresets.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
std::string gFontPath;

} // namespace

const std::vector<SketchNode::Preset>& SketchNode::Presets()
{
   static const std::vector<Preset> p = [] {
      std::vector<Preset> v;
      int n = 0;
      const SketchPresets::Entry* e = SketchPresets::All(n);
      for (int i = 0; i < n; ++i) v.push_back({e[i].name, e[i].code});
      return v;
   }();
   return p;
}

const std::vector<std::string>& SketchNode::PresetNames()
{
   static const std::vector<std::string> n = [] {
      std::vector<std::string> v;
      for (const auto& p : Presets()) v.push_back(p.name);
      return v;
   }();
   return n;
}

void SketchNode::SetFontPath(const std::string& path) { gFontPath = path; }

SketchNode::SketchNode() { code = Presets()[0].code; }

SketchNode::~SketchNode()
{
   if (mTex != 0) glDeleteTextures(1, &mTex);
}

void SketchNode::LoadPreset(int index)
{
   const auto& p = Presets();
   if (index >= 0 && index < (int)p.size())
   {
      code = p[index].code;
      Apply();
   }
}

bool SketchNode::Apply()
{
   mCompiledOnce = true;
   mCompiledCode = code;
   SketchError err;
   mEngine.SetFontFile(gFontPath);
   if (!mEngine.Compile(code, err))
   {
      mLastError = err.line > 0 ? "line " + std::to_string(err.line) + ": " + err.message : err.message;
      return false;
   }
   mLastError.clear();
   std::vector<Field::DeclaredParam> decl;
   for (const auto& p : mEngine.Params())
   {
      Field::DeclaredParam d;
      d.name = p.name; d.typeName = "float";
      d.defaultValue = p.def; d.minValue = p.lo; d.maxValue = p.hi;
      decl.push_back(d);
   }
   mParamTable.Reconcile(decl, mNodeIndex, mNotice);
   mHasRun = false;   // new program: redraw even if time and params match
   return true;
}

void SketchNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;
   mLastCookFrame = frameId;

   if (!mCompiledOnce || code != mCompiledCode) Apply();
   if (!mEngine.HasProgram()) return;

   const int w = std::max(16, std::min(4096, (int)width));
   const int h = std::max(16, std::min(4096, (int)height));
   const double t = animate ? Transport::Instance().Seconds() : 0.0;
   std::map<std::string, float> pm = mParamTable.ValueMap();

   if (mHasRun && w == mRunW && h == mRunH && t == mRunT && pm == mRunParams) return;

   SketchEngine::Frame f;
   f.width = w; f.height = h;
   f.t = t;
   f.beat = animate ? Transport::Instance().Beats() : 0.0;
   f.frame = (int)std::floor(t * 60.0);
   f.seed = (uint32_t)(mNodeIndex + 1);
   f.params = &pm;

   SketchError err;
   const bool ok = mEngine.Run(f, mPixels, err);
   mHasRun = true; mRunW = w; mRunH = h; mRunT = t; mRunParams = pm;
   if (!ok)
   {
      // Keep the last good frame on screen (README S9).
      mLastError = err.line > 0 ? "line " + std::to_string(err.line) + ": " + err.message : err.message;
      return;
   }
   mLastError.clear();

   // plutovg writes the top row first; GL row 0 is the bottom (same as TextNode).
   const int stride = w * 4;
   mFlipped.resize(mPixels.size());
   for (int y = 0; y < h; y++)
      std::memcpy(&mFlipped[(size_t)y * stride], &mPixels[(size_t)(h - 1 - y) * stride], stride);

   if (mTex == 0) glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   if (mUploadedW == w && mUploadedH == h)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, mFlipped.data());
   else
   {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mFlipped.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      mUploadedW = w; mUploadedH = h;
   }
   glBindTexture(GL_TEXTURE_2D, 0);
   mW = w; mH = h;
   mRevision = NextTextureRevision();
}

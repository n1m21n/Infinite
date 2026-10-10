#include "SketchNode.h"

#include "core/gl3.h"
#include "platform/AppPaths.h"
#include "core/sketch/SketchPresets.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

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
      for (int i = 0; i < n; ++i) v.push_back({e[i].name, e[i].code, e[i].svg});
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
      svg = p[index].svg ? p[index].svg : "";
      svgName = svg.empty() ? "" : "example";
      Apply();
   }
}

bool SketchNode::LoadSvgFile(const std::string& path)
{
   std::ifstream in(AppPaths::FsPath(path), std::ios::binary); // wide path on Windows
   if (!in) { mLastError = "could not open " + path; return false; }
   std::ostringstream ss;
   ss << in.rdbuf();
   std::string text = ss.str();
   if (text.size() > (size_t)8 * 1024 * 1024) { mLastError = "SVG is larger than 8 MB"; return false; }
   SketchError err;
   SketchEngine probe;
   if (!probe.SetSvg(text, err)) { mLastError = "SVG: " + err.message; return false; }

   svg = std::move(text);
   const size_t slash = path.find_last_of("/\\");
   svgName = slash == std::string::npos ? path : path.substr(slash + 1);

   // A fresh sketch still showing one of the built-in presets has nothing to lose:
   // swap in a script that just draws the SVG. Anything the user wrote is left alone.
   bool isPreset = false;
   for (const auto& p : Presets()) if (code == p.code) isPreset = true;
   if (isPreset || code.empty())
      code = "// Your SVG, drawn fitted into the frame. Animate it by id:\n"
             "//   svgSet(\"#id\", \"attr\", value)   svgText(\"#id\", \"text\")   svgBox(\"#id\")\n"
             "// svgWidth / svgHeight give the SVG's own size. Add param() knobs to drive it.\n"
             "function draw(t) {\n  background(0.07);\n  svgDraw();\n}\n";
   Apply();
   return true;
}

bool SketchNode::Apply()
{
   mCompiledOnce = true;
   mCompiledCode = code;
   SketchError err;
   mEngine.SetFontFile(gFontPath);
   mCompiledSvg = svg;
   if (!mEngine.SetSvg(svg, err))
   {
      mLastError = "SVG: " + err.message;
      return false;
   }
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

   if (!mCompiledOnce || code != mCompiledCode || svg != mCompiledSvg) Apply();
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

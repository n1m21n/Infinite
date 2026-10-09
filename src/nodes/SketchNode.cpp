#include "SketchNode.h"

#include "core/gl3.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
std::string gFontPath;

const char* kBurst = R"(// Radial burst. Every param() is a knob you can modulate.
param("count", 12, 1, 64);
param("spin", 0.2, 0, 2);
param("size", 30, 4, 120);

function draw(t) {
  background(0.05);
  translate(width / 2, height / 2);
  noStroke();
  const r = min(width, height) * 0.2;
  for (let i = 0; i < count; i++) {
    rotate(TAU / count + spin * t * 0.1);
    fill(hsl(i / count, 0.7, 0.6));
    circle(r * 1.2 + r * 0.4 * sin(t + i), 0, size);
  }
}
)";

const char* kFlow = R"(// Flow-field lines.
param("lines", 220, 20, 600);
param("scale", 2.5, 0.5, 8);
param("weight", 1.5, 0.5, 6);

function draw(t) {
  background(0.04, 0.05, 0.08);
  noFill();
  strokeWeight(weight);
  for (let i = 0; i < lines; i++) {
    let x = ((i * 0.6180339) % 1) * width;
    let y = ((i * 0.7548776) % 1) * height;
    stroke(hsl(0.55 + 0.2 * (i / lines), 0.6, 0.65), 0.7);
    beginShape();
    for (let s = 0; s < 40; s++) {
      vertex(x, y);
      const a = noise(x / width * scale, y / height * scale, t * 0.2) * TAU * 2;
      x += cos(a) * 10;
      y += sin(a) * 10;
    }
    endShape();
  }
}
)";

const char* kTree = R"(// L-system style recursive tree.
param("depth", 9, 2, 12);
param("angle", 0.45, 0.1, 1.2);
param("sway", 0.08, 0, 0.4);

function branch(len, d) {
  strokeWeight(max(1, d * 0.9));
  stroke(0.35 + 0.05 * (12 - d), 0.55, 0.3 + 0.04 * (12 - d));
  line(0, 0, 0, -len);
  translate(0, -len);
  if (d <= 0) return;
  push(); rotate(angle + sway * sin(t * 1.3 + d)); branch(len * 0.74, d - 1); pop();
  push(); rotate(-angle + sway * sin(t * 1.1 + d)); branch(len * 0.74, d - 1); pop();
}

function draw(tt) {
  background(0.93, 0.92, 0.88);
  translate(width / 2, height * 0.95);
  branch(height * 0.2, depth);
}
)";
} // namespace

const std::vector<SketchNode::Preset>& SketchNode::Presets()
{
   static const std::vector<Preset> p = {
      {"Radial burst", kBurst},
      {"Flow field", kFlow},
      {"Tree", kTree},
   };
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

#include "Scene2DNode.h"
#include <map>
#include <set>

#include "gl3.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <sstream>
#include <unordered_set>

#include "BlendModes.h"
#include "ImageCable.h"
#include "Geometry3DNodes.h"
#include "Modulation.h"
#include "NodeViewport.h"
#include "TileStore.h"
#include "core/ml/AiSelect.h"

std::function<INode*(uint64_t)> Scene2DNode::ResolveUid;
std::function<bool(uint64_t, float&, float&)> Scene2DNode::NodeViewOf;

const char* Scene2DNode::AdjName(int type)
{
   static const char* kNames[kAdjCount] = { "Pixelate", "Halftone", "Dither", "Threshold", "Gradient Map", "Posterize",
                                                 "Brightness", "Contrast", "Hue", "Saturation", "Exposure", "Invert",
                                                 "Levels", "Curves", "Colour Balance", "Vibrance", "Selective Colour",
                                                 "Channel Mixer", "Black & White", "Photo Filter", "Shadows / Highlights",
                                                 "Colour Lookup", "White Balance", "Split Toning", "Recolour", "Normals",
                                                 "Tone Compression", "Tone Stretch", "Noise & Grain", "Procedural Texture",
                                                 "Lens Blur" };
   return kNames[std::clamp(type, 0, kAdjCount - 1)];
}

const char* Scene2DNode::AdjGroup(int type)
{
   switch (type)
   {
   case kAdjPixelate: case kAdjHalftone: case kAdjDither: case kAdjThreshold: case kAdjPosterize:
   case kAdjNoise: case kAdjTexture: case kAdjLensBlur: return "Stylize";
   case kAdjGradientMap: case kAdjHue: case kAdjSaturation: case kAdjColorBalance: case kAdjVibrance: case kAdjSelective:
   case kAdjMixer: case kAdjBlackWhite: case kAdjPhotoFilter: case kAdjLookup: case kAdjInvert: case kAdjWhiteBalance:
   case kAdjSplitToning: case kAdjRecolour: return "Colour";
   default: return "Adjust";
   }
}

int Scene2DNode::AdjParamCount(int type)
{
   switch (type)
   {
   case kAdjHalftone: return 3;
   case kAdjDither: return 1;
   case kAdjThreshold: return 1;
   case kAdjPosterize: return 1;
   case kAdjBrightness: return 1;
   case kAdjHue: return 3 + 6 * kHueRangeStride;
   case kAdjExposure: return 2;
   case kAdjLevels: return 3;
   case kAdjCurves: return 1;
   case kAdjColorBalance: return 10;
   case kAdjSelective: return 37;
   case kAdjMixer: return 13;
   case kAdjBlackWhite: return 7;
   case kAdjPhotoFilter: return 1;
   case kAdjShadowsHighlights: return 3;
   case kAdjWhiteBalance: return 2;
   case kAdjSplitToning: return 5;
   case kAdjRecolour: return 3;
   case kAdjNormals: return 4;
   case kAdjToneCompression: return 3;
   case kAdjToneStretch: return 3;
   case kAdjNoise: return 6;
   case kAdjTexture: return 8;
   case kAdjLensBlur: return 3;
   default: return 0;
   }
}

// Hue/Saturation ranges (PhotoCraft doc/adjust.rs HueRange, MIT/Apache-2.0): how much a hue (deg) belongs to a range,
// measured from the range's start so the wrap-around reds work.
float Scene2DNode::HueRangeWeight(const float* b, float deg)
{
   if (!(std::isfinite(b[0]) && std::isfinite(deg)))
      return 0.0f;
   auto rel = [&](float x) { return std::isfinite(x) ? x - b[0] - 360.0f * std::floor((x - b[0]) / 360.0f) : 0.0f; };
   const float lo = rel(b[1]), hi = std::max(rel(b[2]), lo), end = std::max(rel(b[3]), hi), h = rel(deg);
   if (h <= lo)
      return lo <= 0.0f ? 1.0f : h / lo;
   if (h <= hi)
      return 1.0f;
   if (h <= end)
      return end <= hi ? 1.0f : (end - h) / (end - hi);
   return 0.0f;
}

// Four range sliders made increasing, within one turn, centre in -30..330 (the default reds are -45 -15 15 45).
void Scene2DNode::CanonicalHueBounds(float* b)
{
   if (!(std::isfinite(b[0]) && std::isfinite(b[1]) && std::isfinite(b[2]) && std::isfinite(b[3])))
   {
      b[0] = -45.0f; b[1] = -15.0f; b[2] = 15.0f; b[3] = 45.0f;
      return;
   }
   auto wrap = [](float x) { return x - 360.0f * std::floor(x / 360.0f); };
   float o[4] = { b[0], 0, 0, 0 };
   for (int k = 1; k < 4; k++)
      o[k] = std::min(o[k - 1] + wrap(b[k] - o[k - 1]), o[0] + 359.0f);
   const float centre = (o[1] + o[2]) * 0.5f, shift = wrap(centre + 30.0f) - 30.0f - centre;
   for (int k = 0; k < 4; k++)
      b[k] = o[k] + shift;
}

// The six ranges as three 256-tap hue tables (hue shift deg, saturation, lightness), sampled at hue k / 255 turns;
// ranges add up where they overlap. False when every range is neutral.
bool Scene2DNode::HueRangeTables(const std::vector<float>& p, float* out)
{
   bool any = false;
   std::fill(out, out + 768, 0.0f);
   for (int r = 0; r < 6; r++)
   {
      const size_t at = (size_t)(kHueRangeBase + r * kHueRangeStride);
      if (at + kHueRangeStride > p.size())
         break;
      const float hue = p[at], sat = p[at + 1], light = p[at + 2];
      if (hue == 0.0f && sat == 0.0f && light == 0.0f)
         continue;
      any = true;
      for (int k = 0; k < 256; k++)
      {
         const float w = HueRangeWeight(&p[at + 3], k / 255.0f * 360.0f);
         out[k] += w * hue;
         out[256 + k] += w * sat;
         out[512 + k] += w * light;
      }
   }
   return any;
}

std::vector<float> Scene2DNode::AdjParamDefault(int type)
{
   std::vector<float> p((size_t)AdjParamCount(type), 0.0f);
   if (type == kAdjHalftone)
      p = { 45.0f, 0.0f, 0.0f };   // angle (deg), shape (0 dot, 1 line, 2 diamond), colour halftone
   else if (type == kAdjDither || type == kAdjThreshold || type == kAdjPosterize || type == kAdjCurves)
      p = { 0.0f };
   else if (type == kAdjBrightness)
      p = { 0.0f };                // 0 = modern non-clipping curve, 1 = legacy linear
   else if (type == kAdjHue)
   {
      // saturation delta, lightness delta, legacy hue range (0 master; 1..6 R/Y/G/C/B/M kept for old documents), then
      // Photoshop's six ranges, each hue (deg), saturation, lightness (-1..1) and four bounds (deg), fully on for 30
      // degrees about the centre, fading over 30 each side.
      p.assign((size_t)AdjParamCount(type), 0.0f);
      for (int r = 0; r < 6; r++)
      {
         const float c = 60.0f * r;
         float* b = &p[(size_t)(kHueRangeBase + r * kHueRangeStride + 3)];
         b[0] = c - 45.0f; b[1] = c - 15.0f; b[2] = c + 15.0f; b[3] = c + 45.0f;
      }
   }
   else if (type == kAdjExposure)
      p = { 0.0f, 1.0f };          // offset, gamma
   else if (type == kAdjLevels)
      p = { 1.0f, 1.0f, 1.0f };    // per-channel gamma R, G, B
   else if (type == kAdjColorBalance)
      p[9] = 1.0f;                 // preserve luminosity on by default
   else if (type == kAdjSelective)
      p[36] = 1.0f;                // relative mode on by default (Photoshop default)
   else if (type == kAdjMixer)
      p = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0 };   // 3x3 + mono + constant R,G,B
   else if (type == kAdjBlackWhite)
      p = { 0.4f, 0.6f, 0.4f, 0.6f, 0.2f, 0.8f, 0.0f };   // Photoshop's default weights + tint toggle
   else if (type == kAdjPhotoFilter)
      p = { 1.0f };
   else if (type == kAdjShadowsHighlights)
      p = { 0.35f, 0.0f, 0.0f };   // shadows, highlights, midtone contrast
   else if (type == kAdjSplitToning)
      p = { 0.0f, 0.0f, 0.0f, 0.0f, 0.5f };
   else if (type == kAdjRecolour)
      p = { 0.0f, 1.0f, 0.0f };
   else if (type == kAdjNormals)
      p = { 0.0f, 1.0f, 0.0f, 0.0f };   // rotation (degrees), scale, flip x, flip y
   else if (type == kAdjToneCompression)
      p = { 1.0f, 1.0f, 0.5f };   // exposure, gamma, colour
   else if (type == kAdjToneStretch)
      p = { 0.125f, 1.0f, 0.5f };   // stretch, gamma, compression
   else if (type == kAdjNoise)
      p = { 2.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.25f };   // mode (Film Grain), size, mono, blend (Soft), seed, clump
   else if (type == kAdjTexture)
      p = { 2.0f, 8.0f, 4.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f };   // pattern (fBm Clouds), scale, octaves, detail, warp, seed, blend (Overlay), invert
   else if (type == kAdjLensBlur)
      p = { 0.85f, 0.1f, 0.0f };   // focus depth (0 far .. 1 near), in-focus band, iris (0 disc, 1 hexagon)
   return p;
}

float Scene2DNode::AdjDefault(int type)
{
   static const float kDef[kAdjCount] = { 16.0f, 10.0f, 3.0f, 0.5f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                                          1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.25f, 30.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                          0.25f, 0.5f, 12.0f };
   return kDef[std::clamp(type, 0, kAdjCount - 1)];
}

void Scene2DNode::AdjRange(int type, float& lo, float& hi)
{
   static const float kLo[kAdjCount] = { 1.0f, 2.0f, 2.0f, 0.0f, 0.0f, 2.0f, -1.0f, -1.0f, -180.0f, 0.0f, -4.0f, 0.0f, 0.1f, 0.0f,
                                         0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 0.0f };
   static const float kHi[kAdjCount] = { 256.0f, 128.0f, 16.0f, 1.0f, 1.0f, 32.0f, 1.0f, 1.0f, 180.0f, 2.0f, 4.0f, 1.0f, 4.0f, 1.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 200.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                         1.0f, 1.0f, 100.0f };
   const int t = std::clamp(type, 0, kAdjCount - 1);
   lo = kLo[t];
   hi = kHi[t];
}

// Affinity's White Balance, measured to 0.3/255 (tools/re-survey/affinity_probe.py adjust, wbs_ / tns_ sweeps): a
// Bradford adaptation in linear sRGB from a source white to D65. Temperature walks the source white along a measured
// curve in CIE 1960 uv (with its luminance); tint moves it across that curve, square to it.
void Scene2DNode::WhiteBalanceMatrix(float temperature, float tint, float m[9])
{
   static const float kUv[17][2] = { {0.24507f, 0.34668f}, {0.23471f, 0.34202f}, {0.22570f, 0.33696f}, {0.21894f, 0.33242f},
                                     {0.21293f, 0.32771f}, {0.20833f, 0.32360f}, {0.20416f, 0.31941f}, {0.20094f, 0.31576f},
                                     {0.19784f, 0.31222f}, {0.18881f, 0.29910f}, {0.18405f, 0.29037f}, {0.18086f, 0.28334f},
                                     {0.17892f, 0.27839f}, {0.17746f, 0.27419f}, {0.17649f, 0.27108f}, {0.17571f, 0.26831f},
                                     {0.17516f, 0.26620f} };
   static const float kY[17] = { 0.9851f, 0.9877f, 0.9902f, 0.9921f, 0.9942f, 0.9956f, 0.9969f, 0.9973f, 1.0f,
                                 1.0044f, 1.0077f, 1.0103f, 1.0120f, 1.0135f, 1.0146f, 1.0156f, 1.0164f };
   const auto at = [&](float v, float out[3])
   {
      const float f = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 8.0f;
      const int i = std::min((int)f, 15);
      const float t = f - (float)i;
      out[0] = kUv[i][0] + (kUv[i + 1][0] - kUv[i][0]) * t;
      out[1] = kUv[i][1] + (kUv[i + 1][1] - kUv[i][1]) * t;
      out[2] = kY[i] + (kY[i + 1] - kY[i]) * t;
   };
   float w[3], a[3], b[3];
   at(temperature, w);
   at(temperature + 0.0625f, a);
   at(temperature - 0.0625f, b);
   float tu = a[0] - b[0], tv = a[1] - b[1];
   const float len = std::max(std::sqrt(tu * tu + tv * tv), 1e-9f);
   tu /= len;
   tv /= len;
   const float u = w[0] + tint * (0.0053f * tu - 0.0202f * tv), v = w[1] + tint * (0.0053f * tv + 0.0202f * tu);
   const float d = 2.0f * u - 8.0f * v + 4.0f, x = 3.0f * u / d, y = 2.0f * v / d;
   const float src[3] = { x / y * w[2], w[2], (1.0f - x - y) / y * w[2] };
   static const float kToXyz[9] = { 0.4124f, 0.3576f, 0.1805f, 0.2126f, 0.7152f, 0.0722f, 0.0193f, 0.1192f, 0.9505f };
   static const float kBrad[9] = { 0.8951f, 0.2664f, -0.1614f, -0.7502f, 1.7135f, 0.0367f, 0.0389f, -0.0685f, 1.0296f };
   float A[9] = {}, Ai[9];
   for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
         for (int k = 0; k < 3; k++)
            A[r * 3 + c] += kBrad[r * 3 + k] * kToXyz[k * 3 + c];
   {   // inverse by the adjugate
      const float det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
      const float id = 1.0f / det;
      Ai[0] = (A[4] * A[8] - A[5] * A[7]) * id; Ai[1] = (A[2] * A[7] - A[1] * A[8]) * id; Ai[2] = (A[1] * A[5] - A[2] * A[4]) * id;
      Ai[3] = (A[5] * A[6] - A[3] * A[8]) * id; Ai[4] = (A[0] * A[8] - A[2] * A[6]) * id; Ai[5] = (A[2] * A[3] - A[0] * A[5]) * id;
      Ai[6] = (A[3] * A[7] - A[4] * A[6]) * id; Ai[7] = (A[1] * A[6] - A[0] * A[7]) * id; Ai[8] = (A[0] * A[4] - A[1] * A[3]) * id;
   }
   float g[3];
   for (int r = 0; r < 3; r++)
   {
      float d65 = 0.0f, s = 0.0f;   // cone response of D65 (linear white 1,1,1) and of the source white
      for (int k = 0; k < 3; k++)
      {
         d65 += A[r * 3 + k];
         s += kBrad[r * 3 + k] * src[k];
      }
      g[r] = d65 / std::max(s, 1e-6f);
   }
   for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
      {
         float sum = 0.0f;
         for (int k = 0; k < 3; k++)
            sum += Ai[r * 3 + k] * g[k] * A[k * 3 + c];
         m[r * 3 + c] = sum;
      }
}

void Scene2DNode::SortStops(std::vector<GradStop>& st)
{
   if (st.size() > (size_t)kMaxStops)
      st.resize(kMaxStops);
   for (GradStop& g : st)
   {
      g.t = std::clamp(g.t, 0.0f, 1.0f);
      for (float& v : g.c)
         v = std::clamp(v, 0.0f, 1.0f);
   }
   std::stable_sort(st.begin(), st.end(), [](const GradStop& a, const GradStop& b) { return a.t < b.t; });
}

void Scene2DNode::GradAt(const std::vector<GradStop>& st, const float a[4], const float b[4], float t, float out[4])
{
   t = std::clamp(t, 0.0f, 1.0f);
   if (st.size() < 2)
   {
      for (int k = 0; k < 4; k++)
         out[k] = a[k] + (b[k] - a[k]) * t;
      return;
   }
   if (t <= st.front().t)
   {
      std::copy(st.front().c, st.front().c + 4, out);
      return;
   }
   for (size_t i = 1; i < st.size(); i++)
      if (t <= st[i].t)
      {
         const float u = (t - st[i - 1].t) / std::max(1e-5f, st[i].t - st[i - 1].t);
         for (int k = 0; k < 4; k++)
            out[k] = st[i - 1].c[k] + (st[i].c[k] - st[i - 1].c[k]) * u;
         return;
      }
   std::copy(st.back().c, st.back().c + 4, out);
}

void Scene2DNode::WriteStops(std::ostream& o, const std::vector<GradStop>& st)
{
   o << " " << st.size();
   for (const GradStop& g : st)
   {
      o << " " << g.t;
      for (float v : g.c) o << " " << v;
   }
}

void Scene2DNode::ReadStops(std::istream& s, std::vector<GradStop>& st)
{
   st.clear();
   size_t n = 0;
   if (!(s >> n))
      return;
   for (size_t i = 0; i < std::min<size_t>(n, kMaxStops); i++)
   {
      GradStop g;
      if (!(s >> g.t >> g.c[0] >> g.c[1] >> g.c[2] >> g.c[3]))
         break;
      st.push_back(g);
   }
   if (st.size() < 2)
      st.clear();
   SortStops(st);
}

const char* Scene2DNode::kStopGlsl =
   "uniform int uStopN;\n"
   "uniform float uStopT[8];\n"
   "uniform vec4 uStopC[8];\n"
   "vec4 gradAt(float t, vec4 a, vec4 b) {\n"
   "   t = clamp(t, 0.0, 1.0);\n"
   "   if (uStopN < 2) return mix(a, b, t);\n"
   "   if (t <= uStopT[0]) return uStopC[0];\n"
   "   for (int i = 1; i < 8; i++) {\n"
   "      if (i >= uStopN) break;\n"
   "      if (t <= uStopT[i]) return mix(uStopC[i - 1], uStopC[i], (t - uStopT[i - 1]) / max(uStopT[i] - uStopT[i - 1], 1e-5));\n"
   "   }\n"
   "   return uStopC[uStopN - 1];\n"
   "}\n";

void Scene2DNode::StopUniforms(unsigned prog, const std::vector<GradStop>& st, float (*conv)(float, bool), bool lin)
{
   const int n = st.size() >= 2 ? (int)std::min<size_t>(st.size(), kMaxStops) : 0;
   float t[kMaxStops] = {}, c[kMaxStops * 4] = {};
   for (int i = 0; i < n; i++)
   {
      t[i] = st[(size_t)i].t;
      for (int k = 0; k < 4; k++)
         c[i * 4 + k] = conv != nullptr && k < 3 ? conv(st[(size_t)i].c[k], lin) : st[(size_t)i].c[k];
   }
   glUniform1i(GLUtil::UniformLoc(prog, "uStopN"), n);
   glUniform1fv(GLUtil::UniformLoc(prog, "uStopT"), kMaxStops, t);
   glUniform4fv(GLUtil::UniformLoc(prog, "uStopC"), kMaxStops, c);
}

void Scene2DNode::CurveLut(const std::vector<float>& pts, float* lut, int n)
{
   // Fritsch-Carlson: Hermite pieces with slopes cut back so no piece rises above or dips below its ends.
   std::vector<float> x, y;
   for (size_t i = 0; i + 1 < pts.size(); i += 2)
   {
      x.push_back(std::clamp(pts[i], 0.0f, 1.0f));
      y.push_back(std::clamp(pts[i + 1], 0.0f, 1.0f));
   }
   if (x.size() < 2)
   {
      for (int i = 0; i < n; i++)
         lut[i] = (float)i / (float)std::max(1, n - 1);
      return;
   }
   const size_t k = x.size();
   std::vector<float> d(k - 1), m(k);
   for (size_t i = 0; i + 1 < k; i++)
      d[i] = (y[i + 1] - y[i]) / std::max(1e-5f, x[i + 1] - x[i]);
   m[0] = d[0];
   m[k - 1] = d[k - 2];
   for (size_t i = 1; i + 1 < k; i++)
      m[i] = d[i - 1] * d[i] <= 0.0f ? 0.0f : (d[i - 1] + d[i]) * 0.5f;
   for (size_t i = 0; i + 1 < k; i++)
   {
      if (d[i] == 0.0f)
      {
         m[i] = m[i + 1] = 0.0f;
         continue;
      }
      const float a = m[i] / d[i], b = m[i + 1] / d[i], r = a * a + b * b;
      if (r > 9.0f)
      {
         const float t = 3.0f / std::sqrt(r);
         m[i] = t * a * d[i];
         m[i + 1] = t * b * d[i];
      }
   }
   for (int i = 0; i < n; i++)
   {
      const float t = (float)i / (float)std::max(1, n - 1);
      if (t <= x[0]) { lut[i] = y[0]; continue; }
      if (t >= x[k - 1]) { lut[i] = y[k - 1]; continue; }
      size_t j = 0;
      while (j + 2 < k && t > x[j + 1])
         j++;
      const float h = std::max(1e-5f, x[j + 1] - x[j]), u = (t - x[j]) / h;
      const float u2 = u * u, u3 = u2 * u;
      const float v = (2 * u3 - 3 * u2 + 1) * y[j] + (u3 - 2 * u2 + u) * h * m[j] + (-2 * u3 + 3 * u2) * y[j + 1] +
                      (u3 - u2) * h * m[j + 1];
      lut[i] = std::clamp(v, 0.0f, 1.0f);
   }
}

namespace
{
   // One layer style's settings after "<key> <id> <type>": on, colour, angle, distance, size, spread, position, second
   // colour, pattern, material, contour. Older docs stop after position or after the second colour.
   void WriteFxFields(std::ostream& o, const Scene2DNode::Fx& f)
   {
      o << +f.on;
      for (float v : f.c) o << " " << v;
      o << " " << f.angle << " " << f.dist << " " << f.size << " " << f.spread << " " << f.pos;
      for (float v : f.c2) o << " " << v;
      o << " " << f.pattern << " " << f.material;
      for (float v : f.contour) o << " " << v;
      Scene2DNode::WriteStops(o, f.stops);
      o << " " << f.blend << " " << f.style;
   }
   void ReadFxFields(std::istream& s, Scene2DNode::Fx& f)
   {
      int on = 0;
      s >> on >> f.c[0] >> f.c[1] >> f.c[2] >> f.c[3] >> f.angle >> f.dist >> f.size >> f.spread >> f.pos;
      f.on = on != 0;
      float c2[4];
      if (s >> c2[0] >> c2[1] >> c2[2] >> c2[3])
         std::copy(c2, c2 + 4, f.c2);
      int pat = 0, mat = -1;
      float ct[4];
      if (s >> pat >> mat)
      {
         f.pattern = std::clamp(pat, 0, Scene2DNode::kFxPatternCount - 1);
         f.material = std::clamp(mat, -1, 3);
         if (s >> ct[0] >> ct[1] >> ct[2] >> ct[3])
            for (int k = 0; k < 4; k++)
               f.contour[k] = std::clamp(ct[k], 0.0f, 1.0f);
         Scene2DNode::ReadStops(s, f.stops);
         int bm = 0, st = 0;
         if (s >> bm) f.blend = std::clamp(bm, 0, 6);
         if (s >> st) f.style = std::clamp(st, 0, 3);
      }
   }
}

std::string Scene2DNode::StyleText(const Layer& l)
{
   std::ostringstream o;
   o << "inf2dstyle 1\n";
   for (int t = 0; t < kFxCount; t++)
      if (!FxSame(l.fx[t], FxDefault(t)))
      {
         o << "fx " << t << " ";
         WriteFxFields(o, l.fx[t]);
         o << "\n";
      }
   for (const auto& m : l.fxMore)
   {
      o << "fx2 " << m.first << " ";
      WriteFxFields(o, m.second);
      o << "\n";
   }
   return o.str();
}

bool Scene2DNode::ParseStyle(const std::string& text, Layer& l)
{
   std::istringstream in(text);
   std::string line;
   if (!std::getline(in, line) || line.rfind("inf2dstyle", 0) != 0)
      return false;
   for (int t = 0; t < kFxCount; t++)
      l.fx[t] = FxDefault(t);
   l.fxMore.clear();
   while (std::getline(in, line))
   {
      std::istringstream s(line);
      std::string key;
      int t = -1;
      if (!(s >> key >> t) || t < 0 || t >= kFxCount || (key != "fx" && key != "fx2"))
         continue;
      Fx f = FxDefault(t);
      ReadFxFields(s, f);
      if (key == "fx")
         l.fx[t] = f;
      else
         l.fxMore.emplace_back(t, f);
   }
   return true;
}

const char* Scene2DNode::LayerFieldName(int f)
{
   static const char* kNames[kLfFxBase] = { "Opacity", "Fill", "X", "Y", "Scale X", "Scale Y", "Angle", "Depth",
                                            "Rotate X", "Rotate Y", "Amount", "Text size", "Tracking", "Leading",
                                            "Mask density", "Fill angle" };
   static const char* kFx[kLfCount - kLfFxBase] = {
      "Shadow size", "Shadow distance", "Shadow angle", "Shadow opacity",
      "Inner shadow size", "Inner shadow distance", "Inner shadow angle", "Inner shadow opacity",
      "Glow size", "Glow distance", "Glow angle", "Glow opacity",
      "Stroke width", "Stroke distance", "Stroke angle", "Stroke opacity",
      "Overlay size", "Overlay distance", "Overlay angle", "Overlay opacity",
      "Inner glow size", "Inner glow distance", "Inner glow angle", "Inner glow opacity",
      "Bevel size", "Bevel depth", "Bevel angle", "Bevel opacity",
      "Gradient size", "Gradient distance", "Gradient angle", "Gradient opacity",
      "Satin size", "Satin distance", "Satin angle", "Satin opacity",
      "Pattern scale", "Pattern distance", "Pattern angle", "Pattern opacity" };
   if (f < 0 || f >= kLfCount)
      return "";
   return f < kLfFxBase ? kNames[f] : kFx[f - kLfFxBase];
}

float* Scene2DNode::LayerFieldPtr(Layer& l, int f)
{
   switch (f)
   {
      case kLfOpacity: return &l.opacity;
      case kLfFill: return &l.fill;
      case kLfX: return &l.x;
      case kLfY: return &l.y;
      case kLfScaleX: return &l.sx;
      case kLfScaleY: return &l.sy;
      case kLfAngle: return &l.angle;
      case kLfZ: return &l.z;
      case kLfRotX: return &l.rx;
      case kLfRotY: return &l.ry;
      case kLfAdjust: return &l.adjAmt;
      case kLfFontSize: return &l.fontSize;
      case kLfTracking: return &l.tracking;
      case kLfLeading: return &l.leading;
      case kLfMaskDensity: return &l.maskDensity;
      case kLfFillAngle: return &l.fillAngle;
      default: break;
   }
   if (f < kLfFxBase || f >= kLfCount)
      return nullptr;
   Fx& x = l.fx[(f - kLfFxBase) / kLfFxFields];
   switch ((f - kLfFxBase) % kLfFxFields)
   {
      case 0: return &x.size;
      case 1: return &x.dist;
      case 2: return &x.angle;
      default: return &x.c[3];
   }
}

bool Scene2DNode::LayerFieldApplies(const Layer& l, int f)
{
   switch (f)
   {
      case kLfFill: return l.kind != Kind::Group;
      case kLfZ: case kLfRotX: case kLfRotY: return l.is3d;
      case kLfAdjust: return l.kind == Kind::Adjust;
      case kLfFontSize: case kLfTracking: case kLfLeading: return l.kind == Kind::Text;
      case kLfMaskDensity: return l.maskUid != 0;
      case kLfFillAngle: return l.kind == Kind::Fill;
      default: break;
   }
   if (f >= kLfFxBase && f < kLfCount)
      return l.kind != Kind::Group && l.kind != Kind::Adjust;
   return f >= 0 && f < kLfCount;
}

void Scene2DNode::LayerFieldRange(const Layer& l, int f, float& lo, float& hi)
{
   const float w = (float)Doc().w, h = (float)Doc().h;
   lo = 0.0f;
   hi = 1.0f;
   switch (f)
   {
      case kLfX: lo = -w; hi = 2.0f * w; return;
      case kLfY: lo = -h; hi = 2.0f * h; return;
      case kLfScaleX: case kLfScaleY: hi = 4.0f; return;
      case kLfAngle: case kLfFillAngle: case kLfRotX: case kLfRotY: lo = -180.0f; hi = 180.0f; return;
      case kLfZ: lo = -2000.0f; hi = 2000.0f; return;
      case kLfAdjust: AdjRange(l.adjType, lo, hi); return;
      case kLfFontSize: lo = 4.0f; hi = 800.0f; return;
      case kLfTracking: lo = -100.0f; hi = 400.0f; return;
      case kLfLeading: lo = 0.5f; hi = 3.0f; return;
      default: break;
   }
   if (f >= kLfFxBase && f < kLfCount)
   {
      const int k = (f - kLfFxBase) % kLfFxFields;
      if (k == 0 || k == 1) hi = 200.0f;
      else if (k == 2) { lo = -180.0f; hi = 180.0f; }
   }
}

int Scene2DNode::SlotFor(int layer, int field, bool make)
{
   Document& d = Doc();
   int next = kSlotBase;
   for (const ModSlot& m : d.slots)
   {
      if (m.layer == layer && m.field == field)
         return m.slot;
      next = std::max(next, m.slot + 1);
   }
   if (!make || next > kSlotMax || FindLayer(layer) == nullptr || field < 0 || field >= kLfCount)
      return -1;
   d.slots.push_back({ layer, field, next });
   Commit();
   return next;
}

float Scene2DNode::AdjAmount(const Layer& l) const
{
   if (l.adjDrive < 1 || l.adjDrive > kDrives)
      return l.adjAmt;
   const float k = std::clamp(drive[l.adjDrive - 1], 0.0f, 1.0f);
   return l.adjAmt + (l.adjTo - l.adjAmt) * k;
}

namespace
{
   const std::string& PassSrc()
   {
      static const std::string src =
         std::string(
            "#version 150\n"
            "in vec2 vUv;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uAcc;\n"
            "uniform sampler2D uTex;\n"
            "uniform sampler2D uMask;\n"
            "uniform vec2 uDoc;\n"
            "uniform vec3 uInvA;\n"
            "uniform vec3 uInvB;\n"
            "uniform vec3 uInvC;\n"
            "out float fragDepth;\n"
            "uniform sampler2D uDepthIn;\n"
            "uniform int uDepthMode;\n"
            "uniform int uBif;\n"
            "uniform vec4 uBifT;\n"
            "uniform vec4 uBifU;\n"
            "uniform vec2 uBox;\n"
            "uniform vec4 uCrop;\n"
            "uniform vec2 uTexSize;\n"
            "uniform int uKind;\n"
            "uniform int uMode;\n"
            "uniform int uClip;\n"
            "uniform int uNearest;\n"
            "uniform int uLinear;\n"
            "uniform int uSrgb;\n"
            "uniform int uHasMask;\n"
            "uniform int uMaskChan;\n"
            "uniform int uMaskInv;\n"
            "uniform int uHead;\n"
            "uniform float uOpacity;\n"
            "uniform float uMaskDensity;\n"
            "uniform vec4 uColA;\n"
            "uniform vec4 uColB;\n"
            "uniform float uFillAngle;\n"
            "uniform float uFillNoise;\n"
            "uniform float uGamma;\n"
            + std::string(Scene2DNode::kStopGlsl) +
            "uniform sampler2D uPMask;\n"
            "uniform int uHasPMask;\n"
            "uniform sampler2D uVMask;\n"
            "uniform int uHasVMask;\n"
            "uniform int uVMaskInv;\n"
            "uniform float uVMaskDensity;\n"
            "uniform sampler2D uGeoDepth;\n"
            "uniform int uHasGeoDepth;\n"
            "uniform vec2 uGeoNF;\n"
            "uniform vec4 uGeoUv;\n"
            "uniform int uDirect;\n")
         + GLUtil::kSrgbGlsl
         + BlendModes::kBlendGLSL
         + "float ramp(float lo, float hi, float v) { return hi > lo + 1e-4 ? clamp((v - lo) / (hi - lo), 0.0, 1.0) : step(lo, v); }\n"
           "float bifW(vec4 t, float l) { return ramp(t.x, t.y, l) * (1.0 - ramp(t.z, t.w, l)); }\n"
           "float gLum(vec3 c) { c = clamp(c, 0.0, 1.0); return Lum(uLinear == 1 ? SrgbEncode(c) : c); }\n"
           "float grain(vec2 px) { uvec2 q = uvec2(max(px, vec2(0.0)));\n"
           "   uint n = q.x * 374761393u + q.y * 668265263u; n = (n ^ (n >> 13u)) * 1274126177u;\n"
           "   return float((n ^ (n >> 16u)) & 65535u) / 65535.0; }\n"
           "vec2 gDu = vec2(0.0), gDv = vec2(0.0);\n"
           "// Downscale prefilter: an area average over the output pixel's footprint in the texture (du, dv = the uv step\n"
           "// per document pixel), so a layer shrunk far comes out its average, not point-sampled moire (PhotoCraft).\n"
           "vec4 areaTex(sampler2D t, vec2 uv, vec2 du, vec2 dv, vec2 ts) {\n"
           "   float nx = clamp(ceil(length(du * ts) - 0.25), 1.0, 8.0), ny = clamp(ceil(length(dv * ts) - 0.25), 1.0, 8.0);\n"
           "   if (nx * ny < 2.0) return texture(t, uv);\n"
           "   vec4 s = vec4(0.0);\n"
           "   for (int j = 0; j < 8; j++) { if (float(j) >= ny) break;\n"
           "      for (int i = 0; i < 8; i++) { if (float(i) >= nx) break;\n"
           "         vec2 o = (vec2(float(i), float(j)) + 0.5) / vec2(nx, ny) - 0.5;\n"
           "         s += texture(t, uv + du * o.x + dv * o.y); } }\n"
           "   return s / (nx * ny);\n"
           "}\n"
           "vec2 uvAt(vec2 pp) {\n"
           "   vec3 q = vec3(dot(uInvA, vec3(pp, 1.0)), dot(uInvB, vec3(pp, 1.0)), dot(uInvC, vec3(pp, 1.0)));\n"
           "   vec2 b = q.xy / max(q.z, 1e-12) / uBox;\n"
           "   return vec2(b.x, 1.0 - b.y);\n"
           "}\n"
           "vec4 fillAt(vec2 uvb) {\n"
           "   if (uKind == 1) return uColA;\n"
           "   if (uKind == 2) {\n"
           "      float a = radians(uFillAngle);\n"
           "      vec2 d = vec2(cos(a), sin(a));\n"
           "      float t = clamp(dot(uvb - 0.5, d) / (abs(d.x) + abs(d.y)) * 2.0 * 0.5 + 0.5, 0.0, 1.0);\n"
           "      return gradAt(t, uColA, uColB);\n"
           "   }\n"
           "   if (uKind == 3) return gradAt(length(uvb - 0.5) * 2.0, uColA, uColB);\n"
           "   if (uKind == 7) {\n"   // conical: once round the centre, from the angle, clockwise on screen
           "      vec2 p = (uvb - 0.5) * uBox;\n"
           "      float t = (atan(p.y, p.x) - radians(uFillAngle)) / 6.2831853;\n"
           "      return gradAt(t - floor(t), uColA, uColB);\n"
           "   }\n"
           "   if (uKind == 8) return texture(uTex, uvb);\n"   // bitmap, stretched over the document (rows top first)
           "   float a = radians(uFillAngle);\n"   // reflected: from the centre line out; diamond: to a turned square
           "   vec2 d = vec2(cos(a), sin(a)), e = vec2(-d.y, d.x), p = uvb - 0.5;\n"
           "   float t = uKind == 5 ? abs(dot(p, d)) / (abs(d.x) + abs(d.y)) * 2.0 : (abs(dot(p, d)) + abs(dot(p, e))) * 2.0;\n"
           "   return gradAt(t, uColA, uColB);\n"
           "}\n"
           "vec4 layerAt(vec2 uvb, vec2 uv) {\n"
           "   if (uvb.x < uCrop.x || uvb.x > uCrop.z || uvb.y < uCrop.y || uvb.y > uCrop.w) return vec4(0.0);\n"
           "   if (uKind >= 1 && uKind != 4) {\n"
           "      vec4 c = fillAt(uvb);\n"
           "      if (uFillNoise > 0.0) c.a *= 1.0 - uFillNoise * grain(floor(uvb * uBox));\n"
           "      return c;\n"
           "   }\n"
           "   if (uNearest == 1) {\n"
           "      ivec2 px = ivec2(clamp(floor(uv * uTexSize), vec2(0.0), uTexSize - 1.0));\n"
           "      return texelFetch(uTex, px, 0);\n"
           "   }\n"
           "   return areaTex(uTex, uv, gDu, gDv, uTexSize);\n"
           "}\n"
           "void main() {\n"
           "   vec4 acc = uDirect == 1 ? vec4(0.0, 0.0, 0.0, 1.0) : texture(uAcc, vUv);\n"
           "   float dIn = uDepthMode != 0 ? texture(uDepthIn, vUv).r : 1e30;\n"
           "   fragDepth = dIn;\n"
           "   vec2 p = vec2(vUv.x * uDoc.x, (1.0 - vUv.y) * uDoc.y);\n"
           "   vec3 q = vec3(dot(uInvA, vec3(p, 1.0)), dot(uInvB, vec3(p, 1.0)), dot(uInvC, vec3(p, 1.0)));\n"
           "   if (q.z <= 1e-12 && uKind != 4) { fragColor = uDirect == 1 ? vec4(0.0) : acc; return; }\n"
           "   vec2 b = q.xy / max(q.z, 1e-12);\n"
           "   vec2 uvb = b / uBox;\n"
           "   vec2 uv = vec2(uvb.x, 1.0 - uvb.y);\n"
           "   gDu = uvAt(p + vec2(1.0, 0.0)) - uv;\n"
           "   gDv = uvAt(p + vec2(0.0, 1.0)) - uv;\n"
           "   vec4 layer;\n"
           "   vec2 gUv = (vUv - uGeoUv.xy) / max(uGeoUv.zw - uGeoUv.xy, vec2(1e-6));\n"
           "   if (uKind == 4) {\n"
           "      bool inBox = all(greaterThanEqual(gUv, vec2(0.0))) && all(lessThanEqual(gUv, vec2(1.0)));\n"
           "      layer = inBox ? texture(uTex, gUv) : vec4(0.0);\n"
           "      if (layer.a > 1e-5) layer.rgb /= layer.a;\n"
           "   } else layer = layerAt(uvb, uv);\n"
           "   if (uLinear == 1 && uSrgb == 1) layer.rgb = SrgbDecode(layer.rgb);\n"
           "   if (uHasMask == 1) {\n"
           "      vec4 m = texture(uMask, uv);\n"
           "      float v = uMaskChan == 0 ? Lum(m.rgb) * m.a : m.a;\n"
           "      if (uMaskInv == 1) v = 1.0 - v;\n"
           "      layer.a *= mix(1.0, clamp(v, 0.0, 1.0), uMaskDensity);\n"
           "   }\n"
           "   if (uHasPMask == 1) layer.a *= texture(uPMask, uv).r;\n"
           "   if (uHasVMask == 1) { float va = texture(uVMask, uv).a; if (uVMaskInv == 1) va = 1.0 - va; va = 1.0 - uVMaskDensity * (1.0 - va); layer.a *= clamp(va, 0.0, 1.0); }\n"
           "   float amt = uOpacity * layer.a;\n"
           "   if (uBif == 1) amt *= bifW(uBifT, gLum(layer.rgb)) * bifW(uBifU, gLum(acc.rgb));\n"
           // Normal over an opaque picture: the hardware blends it (src alpha, one minus src alpha; alpha kept at 1).
           "   if (uDirect == 1) { fragColor = vec4(layer.rgb, clamp(amt, 0.0, 1.0)); return; }\n"
           "   if (uDepthMode == 1) {\n"
           "      float d = 1.0 / q.z;\n"
           "      if (uKind == 4) {\n"
           "         float n = uGeoNF.x, f = uGeoNF.y;\n"
           "         float zn = (uHasGeoDepth == 1 ? texture(uGeoDepth, gUv).r : 0.5) * 2.0 - 1.0;\n"
           "         d = 2.0 * f * n / max((f + n) - zn * (f - n), 1e-6);\n"
           "      }\n"
           "      if (d > dIn * 1.0005 + 0.01) { fragColor = acc; return; }\n"
           "      if (layer.a > 0.5) fragDepth = min(dIn, d);\n"
           "   }\n"
           "   if (uMode == " + std::to_string(BlendModes::kEraseMode) + ") { fragColor = vec4(acc.rgb, acc.a * (1.0 - amt)); return; }\n"
           "   if (uMode == " + std::to_string(BlendModes::kAntiEraseMode) + ") { fragColor = vec4(acc.rgb, acc.a * (1.0 - (1.0 - layer.a) * uOpacity)); return; }\n"
           // Blend gamma: both colours raised to blendGamma / 2.2 while they mix, then back.
           "   vec3 A = acc.rgb, L = layer.rgb;\n"
           "   if (uGamma != 1.0) { A = pow(max(A, vec3(0.0)), vec3(uGamma)); L = pow(max(L, vec3(0.0)), vec3(uGamma)); }\n"
           "   vec3 blended = blendModeImage(uMode, A, L, 1, uHead);\n"
           "   vec3 cs = mix(L, blended, acc.a);\n"
           "   float ar = amt + acc.a * (1.0 - amt);\n"
           "   vec3 cr = uClip == 1 ? mix(A, cs, amt) : (ar > 1e-5) ? (cs * amt + A * acc.a * (1.0 - amt)) / ar : vec3(0.0);\n"
           "   if (uGamma != 1.0) cr = pow(max(cr, vec3(0.0)), vec3(1.0 / uGamma));\n"
           "   fragColor = uClip == 1 ? vec4(cr, acc.a) : vec4(cr, ar);\n"
           "}\n";
      return src;
   }

   const std::string& FinishSrc()
   {
      static const std::string src = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uAcc;\n"
         "uniform int uLinear;\n")
         + GLUtil::kSrgbGlsl +
         "void main() { fragColor = texture(uAcc, vUv); }\n";
      return src;
   }

   // Adjustment layers: the picture below with one effect, in display values.
   const std::string& AdjustSrc()
   {
      static const std::string src = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uSrc;\n"
         "uniform vec2 uDoc;\n"
         "uniform int uType;\n"
         "uniform float uAmt;\n"
         "uniform int uMap;\n"
         "uniform int uGmDither;\n"
         "uniform vec4 uColA;\n"
         "uniform vec4 uColB;\n"
         "uniform int uLinear;\n"
         "uniform int uHead;\n"   // headroom on: light above 1.0 survives the adjustment (Photoshop 32-bit)
         "uniform vec4 uLevels;\n"
         "uniform vec4 uLut4[64];\n"     // 256 taps packed 4 per vec4: Apple GL may spend a whole vector slot per float element
         "uniform vec4 uLutC4[192];\n"
         "uniform vec4 uLvCh[3];\n"
         "uniform vec4 uP4[10];\n"
         "float uP[40];\n"
         "uniform sampler3D uCube;\n"
         "uniform float uCubeN;\n"
         "uniform sampler2D uBlurL;\n"
         "uniform sampler2D uDepth;\n"
         "uniform int uHasDepth;\n")
         + std::string(Scene2DNode::kStopGlsl)
         + GLUtil::kSrgbGlsl +
         "float Lut(float v) { float f = clamp(v, 0.0, 1.0) * 255.0; int i = min(int(f), 254); return mix(uLut4[i >> 2][i & 3], uLut4[(i + 1) >> 2][(i + 1) & 3], f - float(i)); }\n"
         "vec3 Disp(vec4 c) { return uLinear == 1 ? SrgbEncode(max(c.rgb, 0.0)) : c.rgb; }\n"
         "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
         // Photoshop's Threshold and Gradient Map grey: 0.30 R + 0.59 G + 0.11 B
         "float LumaPs(vec3 c) { return dot(c, vec3(0.30, 0.59, 0.11)); }\n"
         // Levels' midtone gamma as Photoshop (PhotoCraft adjust/tone.rs levels_gamma, MIT/Apache-2.0): u^(1/g), but
         // above 1 a toe a*u - b*u^2.2 of slope 2^g that meets the power curve with the same value and slope.
         "float LevelsGamma1(float u, float g) {\n"
         "   float c = 1.0 / g;\n"
         "   if (u <= 0.0) return 0.0;\n"
         "   if (g <= 1.0) return pow(u, c);\n"
         "   float a = exp2(g), q = 2.2;\n"
         "   float u1 = pow((1.0 - c / q) / (a * (1.0 - 1.0 / q)), 1.0 / (1.0 - c));\n"
         "   if (u >= u1 || u1 <= 0.0) return pow(u, c);\n"
         "   float b = (a * u1 - pow(u1, c)) / pow(u1, q);\n"
         "   if (isinf(b) || isnan(b)) return pow(u, c);\n"
         "   return a * u - b * pow(u, q);\n"
         "}\n"
         // Photoshop's modern Brightness (PhotoCraft adjust/tone.rs Brightness, MIT/Apache-2.0, within ~1.5 levels of
         // every measurement), b in -150..150: raised, a line of slope 1.375^(b/50) up to a knee at 126/255, then a
         // Hermite (or, past ~+110, a power roll-off) to white; lowered, the inverse of the curve raised as much.
         "float BrightRaised(float x, float s, float e, float x0, float al, bool herm) {\n"
         "   const float y0 = 126.0 / 255.0;\n"
         "   if (x <= x0) return min(y0, s * x);\n"
         "   float h = max(1.0 - x0, 1e-6), t = clamp((x - x0) / h, 0.0, 1.0), t2 = t * t, t3 = t2 * t;\n"
         "   float y = herm ? (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * h * s + (-2.0 * t3 + 3.0 * t2) + (t3 - t2) * h * e\n"
         "                  : 1.0 - (1.0 - y0) * pow(1.0 - t, al);\n"
         "   return clamp(y, 0.0, 1.0);\n"
         "}\n"
         "float ModernBright(float v, float br) {\n"
         "   if (br == 0.0) return v;\n"
         "   float b = abs(br), s = pow(1.375, b / 50.0), e = 1.0 / (1.0 + 0.084 * b), x0 = min((126.0 / 255.0) / s, 1.0);\n"
         "   float sec = (1.0 - 126.0 / 255.0) / max(1.0 - x0, 1e-6), al = s / sec, be = e / sec;\n"
         "   bool herm = al * al + be * be <= 9.0;\n"
         "   v = clamp(v, 0.0, 1.0);\n"
         "   if (br > 0.0) return BrightRaised(v, s, e, x0, al, herm);\n"
         "   if (v <= 0.0 || v >= 1.0) return v;\n"
         "   float lo = 0.0, hi = 1.0;\n"
         "   for (int i = 0; i < 24; i++) { float m = 0.5 * (lo + hi); if (BrightRaised(m, s, e, x0, al, herm) < v) lo = m; else hi = m; }\n"
         "   return 0.5 * (lo + hi);\n"
         "}\n"
         // Photoshop's Vibrance on a linear pixel (PhotoCraft adjust/color.rs vibrance_lin, MIT/Apache-2.0): an HSV
         // change that keeps the hue; above 0 a fitted saturation boost, damped on reds and skin.
         "vec3 VibranceLin(vec3 c, float v) {\n"
         "   float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));\n"
         "   if (mx <= 0.0 || mx - mn <= 1e-9 || v == 0.0) return c;\n"
         "   float t = 1.0 - mn / mx;\n"
         "   float shape = t * (1.0 - t) * (2.0 - t + t * t) * mx * max(1.0 - mx, 0.0);\n"
         "   float t2, v2;\n"
         "   if (v < 0.0) {\n"
         "      float a = -v;\n"
         "      t2 = t * (1.0 - a / 4.0) * (1.0 - a + a * t * (1.0 + t) / 2.0); v2 = mx - a * shape;\n"
         "   } else {\n"
         "      float d = mx - mn, h;\n"
         "      if (mx == c.r) h = mod((c.g - c.b) / d, 6.0); else if (mx == c.g) h = (c.b - c.r) / d + 2.0; else h = (c.r - c.g) / d + 4.0;\n"
         "      h *= 60.0;\n"
         "      float w = h <= 30.0 ? 1.0 : h < 45.0 ? (45.0 - h) / 15.0 : h < 300.0 ? 0.0 : (h - 300.0) / 60.0;\n"
         "      float a = v * (1.0 - w) + 0.64 * pow(v, 1.6) * w;\n"
         "      float boost = (0.5001 * a + 0.0887 * a * a) * pow(t, 0.4374) * pow(1.0 - t, 0.7573)\n"
         "                  * exp((-0.8113 + 0.5154 * a + 2.8026 * (1.0 - t)) * max(1.0 - mx, 0.0));\n"
         "      t2 = min(t * (1.0 + boost), 1.0); v2 = mx + v / 4.0 * shape;\n"
         "   }\n"
         "   float m2 = v2 * (1.0 - t2), k = (v2 - m2) / (mx - mn);\n"
         "   return m2 + (c - mn) * k;\n"
         "}\n"
         "vec3 LevelsGamma(vec3 u, float g) { return vec3(LevelsGamma1(u.r, g), LevelsGamma1(u.g, g), LevelsGamma1(u.b, g)); }\n"
         "float LutC(int ch, float v) { float f = clamp(v, 0.0, 1.0) * 255.0; int i = min(int(f), 254); int a = ch * 256 + i, b = a + 1; return mix(uLutC4[a >> 2][a & 3], uLutC4[b >> 2][b & 3], f - float(i)); }\n"
         "float Hue(vec3 c) { float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;\n"
         "   if (d < 1e-5) return 0.0;\n"
         "   float h = mx == c.r ? mod((c.g - c.b) / d, 6.0) : mx == c.g ? (c.b - c.r) / d + 2.0 : (c.r - c.g) / d + 4.0;\n"
         "   return h / 6.0; }\n"
         "float Sat(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }\n"
         // Weight of the hue sector centred on k/6: 1 there, 0 one sector away.
         "float HueW(float h, int k) { float d = abs(fract(h - float(k) / 6.0 + 0.5) - 0.5) * 6.0; return clamp(1.0 - d, 0.0, 1.0); }\n"
         "vec3 HueRgb(float deg) { return clamp(abs(fract(deg / 360.0 + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0); }\n"
         // Photoshop's soft light, the one Affinity's Split Toning matches
         "vec3 SoftPs(vec3 b, vec3 t) { return mix(2.0 * b * (1.0 - t) + sqrt(max(b, 0.0)) * (2.0 * t - 1.0), 2.0 * b * t + b * b * (1.0 - 2.0 * t), step(t, vec3(0.5))); }\n"
         "const int kBayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n"
         "float AdjHash12(vec2 p) {\n"
         "   vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
         "   p3 += dot(p3, p3.yzx + 33.33);\n"
         "   return fract((p3.x + p3.y) * p3.z);\n"
         "}\n"
         "vec2 AdjHash22(vec2 p) {\n"
         "   vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
         "   p3 += dot(p3, p3.yzx + 33.33);\n"
         "   return fract((p3.xx + p3.yz) * p3.zy);\n"
         "}\n"
         "float AdjGauss(vec2 p, float seed) {\n"
         "   float u1 = max(AdjHash12(p + vec2(seed * 17.3, seed * 31.7)), 1e-4);\n"
         "   float u2 = AdjHash12(p + vec2(seed * 59.1 + 13.7, seed * 11.3 + 7.1));\n"
         "   return clamp(sqrt(-2.0 * log(u1)) * cos(6.2831853 * u2) * 0.34, -1.0, 1.0);\n"
         "}\n"
         "float AdjValueNoise(vec2 p) {\n"
         "   vec2 i = floor(p), f = fract(p);\n"
         "   vec2 u = f * f * (3.0 - 2.0 * f);\n"
         "   return mix(mix(AdjHash12(i), AdjHash12(i + vec2(1.0, 0.0)), u.x),\n"
         "              mix(AdjHash12(i + vec2(0.0, 1.0)), AdjHash12(i + vec2(1.0, 1.0)), u.x), u.y);\n"
         "}\n"
         "float AdjPerlin(vec2 p) {\n"
         "   vec2 i = floor(p), f = fract(p), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
         "   vec2 g00 = normalize(AdjHash22(i) - 0.5 + vec2(1e-4));\n"
         "   vec2 g10 = normalize(AdjHash22(i + vec2(1.0, 0.0)) - 0.5 + vec2(1e-4));\n"
         "   vec2 g01 = normalize(AdjHash22(i + vec2(0.0, 1.0)) - 0.5 + vec2(1e-4));\n"
         "   vec2 g11 = normalize(AdjHash22(i + vec2(1.0, 1.0)) - 0.5 + vec2(1e-4));\n"
         "   float a = mix(dot(g00, f), dot(g10, f - vec2(1.0, 0.0)), u.x);\n"
         "   float b = mix(dot(g01, f - vec2(0.0, 1.0)), dot(g11, f - vec2(1.0, 1.0)), u.x);\n"
         "   return mix(a, b, u.y) * 0.7 + 0.5;\n"
         "}\n"
         "float AdjSimplex(vec2 p) {\n"
         "   const float K1 = 0.366025404, K2 = 0.211324865;\n"
         "   vec2 i = floor(p + (p.x + p.y) * K1);\n"
         "   vec2 a = p - i + (i.x + i.y) * K2;\n"
         "   vec2 o = step(a.yx, a.xy);\n"
         "   vec2 b = a - o + K2, cc = a - 1.0 + 2.0 * K2;\n"
         "   vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(cc, cc)), 0.0);\n"
         "   vec2 ga = normalize(AdjHash22(i) - 0.5 + vec2(1e-4));\n"
         "   vec2 gb = normalize(AdjHash22(i + o) - 0.5 + vec2(1e-4));\n"
         "   vec2 gc = normalize(AdjHash22(i + 1.0) - 0.5 + vec2(1e-4));\n"
         "   vec3 n = h * h * h * h * vec3(dot(a, ga), dot(b, gb), dot(cc, gc));\n"
         "   return dot(n, vec3(70.0)) * 0.5 + 0.5;\n"
         "}\n"
         "float AdjFbm(vec2 p, int oct, int mode) {\n"
         "   float sum = 0.0, amp = 0.5, norm = 0.0, freq = 1.0;\n"
         "   for (int k = 0; k < 8; k++) {\n"
         "      if (k >= oct) break;\n"
         "      float v = AdjPerlin(p * freq);\n"
         "      if (mode == 1) { float r = 1.0 - abs(v * 2.0 - 1.0); v = r * r; }\n"
         "      else if (mode == 2) v = abs(v * 2.0 - 1.0);\n"
         "      sum += v * amp; norm += amp; amp *= 0.5; freq *= 2.03;\n"
         "   }\n"
         "   return sum / max(norm, 1e-4);\n"
         "}\n"
         "vec2 AdjVoronoi(vec2 p) {\n"
         "   vec2 n = floor(p), f = fract(p);\n"
         "   float d1 = 8.0, d2 = 8.0;\n"
         "   for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {\n"
         "      vec2 g = vec2(float(i), float(j));\n"
         "      vec2 o = AdjHash22(n + g);\n"
         "      float d = length(g + o - f);\n"
         "      if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;\n"
         "   }\n"
         "   return vec2(d1, d2);\n"
         "}\n"
         "void main() {\n"
         "   for (int k = 0; k < 10; k++) { uP[k * 4] = uP4[k].x; uP[k * 4 + 1] = uP4[k].y; uP[k * 4 + 2] = uP4[k].z; uP[k * 4 + 3] = uP4[k].w; }\n"
         "   vec2 px = gl_FragCoord.xy;\n"
         "   vec4 src = texture(uSrc, vUv);\n"
         "   vec3 c = Disp(src);\n"
         "   if (uType == 0) {\n"   // pixelate: 4x4 box-averaged mosaic cell (no thin-line flicker)
         "      float b = max(1.0, uAmt);\n"
         "      vec2 base = floor(px / b) * b;\n"
         "      vec3 acc = vec3(0.0);\n"
         "      for (int jy = 0; jy < 4; jy++) for (int jx = 0; jx < 4; jx++) {\n"
         "         vec2 q = base + (vec2(float(jx), float(jy)) + 0.5) * (b * 0.25);\n"
         "         acc += Disp(texture(uSrc, clamp(q / uDoc, vec2(0.0), vec2(1.0))));\n"
         "      }\n"
         "      c = acc * 0.0625;\n"
         "   } else if (uType == 1) {\n"   // halftone: custom angle, Dot/Line/Diamond, mono or CMY colour screens
         "      float cell = max(2.0, uAmt), ang0 = radians(uP[0]);\n"
         "      int shape = int(clamp(uP[1] + 0.5, 0.0, 2.0));\n"
         "      if (uP[2] > 0.5) {\n"
         "         vec3 cmy = vec3(0.0);\n"
         "         float angs[3] = float[3](ang0 + 0.2618, ang0 + 1.3090, ang0);\n"
         "         for (int ch = 0; ch < 3; ch++) {\n"
         "            float ca = cos(angs[ch]), sa = sin(angs[ch]);\n"
         "            mat2 r = mat2(ca, sa, -sa, ca);\n"
         "            vec2 p = r * px, ctr = (floor(p / cell) + 0.5) * cell, cpx = transpose(r) * ctr;\n"
         "            vec3 sc = Disp(texture(uSrc, clamp(cpx / uDoc, vec2(0.0), vec2(1.0))));\n"
         "            float ink = clamp(1.0 - sc[ch], 0.0, 1.0);\n"
         "            vec2 d = (p - ctr) / (cell * 0.5);\n"
         "            float dist = shape == 1 ? abs(d.y) : (shape == 2 ? (abs(d.x) + abs(d.y)) * 0.7071 : length(d));\n"
         "            float rad = sqrt(ink) * 1.15;\n"
         "            cmy[ch] = 1.0 - smoothstep(rad - 0.12, rad + 0.12, dist);\n"
         "         }\n"
         "         c = clamp(1.0 - cmy, 0.0, 1.0);\n"
         "      } else {\n"
         "         float ca = cos(ang0), sa = sin(ang0);\n"
         "         mat2 r = mat2(ca, sa, -sa, ca);\n"
         "         vec2 p = r * px, ctr = (floor(p / cell) + 0.5) * cell, cpx = transpose(r) * ctr;\n"
         "         float L = Luma(Disp(texture(uSrc, clamp(cpx / uDoc, vec2(0.0), vec2(1.0)))));\n"
         "         vec2 d = (p - ctr) / (cell * 0.5);\n"
         "         float dist = shape == 1 ? abs(d.y) : (shape == 2 ? (abs(d.x) + abs(d.y)) * 0.7071 : length(d));\n"
         "         float rad = sqrt(clamp(1.0 - L, 0.0, 1.0)) * 1.15;\n"
         "         c = vec3(smoothstep(rad - 0.12, rad + 0.12, dist));\n"
         "      }\n"
         "   } else if (uType == 2) {\n"   // dither: 0=4x4 Bayer, 1=8x8 Bayer, 2=Blue/IGN, 3=Random
         "      float n = max(2.0, floor(uAmt + 0.5));\n"
         "      int dm = int(clamp(uP[0] + 0.5, 0.0, 3.0));\n"
         "      float t = 0.5;\n"
         "      if (dm == 0) {\n"
         "         ivec2 ip = ivec2(mod(px, 4.0));\n"
         "         t = (float(kBayer[ip.y * 4 + ip.x]) + 0.5) / 16.0;\n"
         "      } else if (dm == 1) {\n"
         "         ivec2 ip0 = ivec2(mod(px, 4.0)), ip1 = ivec2(mod(floor(px * 0.25), 2.0));\n"
         "         int q = (ip1.y * 2 + ip1.x); int qv = q == 0 ? 0 : (q == 1 ? 2 : (q == 2 ? 3 : 1));\n"
         "         t = (float(kBayer[ip0.y * 4 + ip0.x] * 4 + qv) + 0.5) / 64.0;\n"
         "      } else if (dm == 2) {\n"
         "         t = fract(52.9829189 * fract(dot(floor(px), vec2(0.06711056, 0.00583715))));\n"
         "      } else {\n"
         "         t = AdjHash12(floor(px));\n"
         "      }\n"
         "      c = floor(c * (n - 1.0) + t) / (n - 1.0);\n"
         "   } else if (uType == 3) {\n"   // threshold (+ optional smooth anti-aliased softness)
         "      float lum = LumaPs(c), soft = max(uP[0], 0.0) * 0.25;\n"
         "      c = vec3(soft > 1e-4 ? smoothstep(uAmt - soft, uAmt + soft, lum) : step(uAmt, lum));\n"
         "   } else if (uType == 5) {\n"   // posterize: 0=RGB, 1=Luma-only, 2=Dithered
         "      float n = max(2.0, floor(uAmt + 0.5));\n"
         "      int pm = int(clamp(uP[0] + 0.5, 0.0, 2.0));\n"
         "      if (pm == 1) {\n"
         "         float l0 = max(Luma(c), 1e-5), l1 = min(floor(l0 * n) / (n - 1.0), 1.0);\n"
         "         c = clamp(c * (l1 / l0), 0.0, 1.0);\n"
         "      } else if (pm == 2) {\n"
         "         float ign = fract(52.9829189 * fract(dot(floor(px), vec2(0.06711056, 0.00583715))));\n"
         "         c = clamp(floor(c * (n - 1.0) + ign) / (n - 1.0), 0.0, 1.0);\n"
         "      } else {\n"
         "         c = min(floor(c * n) / (n - 1.0), vec3(1.0));\n"
         "      }\n"
         "   } else if (uType == 6) {\n"   // brightness: modern non-clipping midtone curve (blacks/whites anchored), or legacy linear
         "      if (uP[0] > 0.5) c = c + uAmt;\n"
         "      else c = vec3(ModernBright(c.r, uAmt * 150.0), ModernBright(c.g, uAmt * 150.0), ModernBright(c.b, uAmt * 150.0));\n"
         "   } else if (uType == 7) {\n"
         // Photoshop's modern Contrast (PhotoCraft adjust/tone.rs modern_contrast, MIT/Apache-2.0, measured within
         // 0.5/255): a symmetric Hermite S-curve through 0, 0.5 and 1, end slope 1 - c/128, centre slope 1 + c/128.
         // The amount -1..1 is Photoshop's -50..100.
         "      float k = (uAmt >= 0.0 ? uAmt * 100.0 : uAmt * 50.0) / 128.0, ed = 1.0 - k, md = 1.0 + k;\n"
         "      vec3 v = clamp(c, 0.0, 1.0), lo = step(v, vec3(0.5)), t = mix((1.0 - v) * 2.0, v * 2.0, lo);\n"
         "      vec3 t2 = t * t, t3 = t2 * t;\n"
         "      vec3 h = ed * 0.5 * (t3 - 2.0 * t2 + t) + (3.0 * t2 - 2.0 * t3) * 0.5 + md * 0.5 * (t3 - t2);\n"
         "      c = mix(1.0 - h, h, lo);\n"
         "   } else if (uType == 8) {\n"
         // HSL Hue / Saturation / Lightness with optional per-hue-family targeting (0 Master, 1..6 R/Y/G/C/B/M)
         "      float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));\n"
         "      int rng = int(clamp(uP[2] + 0.5, 0.0, 6.0));\n"
         // Photoshop's order (PhotoCraft adjust/color.rs hue_saturation_in + range_lightness, MIT/Apache-2.0):
         // Lightness first, then the hue turned and the HSL saturation scaled (+50 % doubles it), then each range's
         // Lightness towards the brightest or darkest channel. Ranges follow the original hue, in full however dull.
         "      float h0 = Hue(c), dh = 0.0, ds = 0.0, dl = 0.0;\n"
         "      if (uP[3] > 0.5) { dh = LutC(0, h0); ds = LutC(1, h0); dl = LutC(2, h0); }\n"
         "      if (rng == 0 && uHead == 0) {\n"
         "         float l = clamp(uP[1], -1.0, 1.0), s = clamp(uP[0] + ds, -1.0, 1.0);\n"
         "         vec3 lc = l > 0.0 ? c + (1.0 - c) * l : c * (1.0 + l);\n"
         "         float lmx = max(lc.r, max(lc.g, lc.b)), lmn = min(lc.r, min(lc.g, lc.b)), L = (lmx + lmn) * 0.5, d = lmx - lmn;\n"
         "         float S = d <= 0.0 ? 0.0 : (L > 0.5 ? d / max(2.0 - lmx - lmn, 1e-6) : d / max(lmx + lmn, 1e-6));\n"
         "         S = clamp(s > 0.0 ? S / max(1.0 - s, 1.0 / 512.0) : S * (1.0 + s), 0.0, 1.0);\n"
         "         float C = (1.0 - abs(2.0 * L - 1.0)) * S;\n"
         "         vec3 o = (L - C * 0.5) + C * HueRgb(Hue(lc) * 360.0 + uAmt + dh);\n"
         "         float tg = dl > 0.0 ? max(o.r, max(o.g, o.b)) : min(o.r, min(o.g, o.b));\n"
         "         c = clamp(o + (tg - o) * min(abs(dl), 1.0), 0.0, 1.0);\n"
         "      } else {\n"
         "      float hw = rng == 0 ? 1.0 : HueW(Hue(c), rng - 1) * smoothstep(0.02, 0.15, mx - mn);\n"
         "      vec3 hOut = mn + (mx - mn) * HueRgb(Hue(c) * 360.0 + uAmt);\n"
         "      hOut = mix(vec3(Luma(hOut)), hOut, clamp(1.0 + uP[0], 0.0, 2.0));\n"
         "      hOut = uP[1] >= 0.0 ? mix(hOut, vec3(1.0), uP[1]) : mix(hOut, vec3(0.0), -uP[1]);\n"
         "      if (uHead == 1) {\n"
         // 32-bit (PhotoCraft adjust/color.rs hue_saturation_in + hdr_saturation_factor, MIT/Apache-2.0): Lightness
         // first, then the hue turned and the chroma scaled about the HSL lightness by Photoshop's measured factor,
         // unclipped, so colours at or above 1 keep their hue.
         "         float l = clamp(uP[1], -1.0, 1.0), s = clamp(uP[0] + (rng == 0 ? ds : 0.0), -1.0, 1.0);\n"
         "         vec3 v = uLinear == 1 ? max(src.rgb, 0.0) : c;\n"   // a 32-bit document's own (linear) values
         "         vec3 lc = l > 0.0 ? v + (1.0 - v) * l : v * (1.0 + l);\n"
         "         float lmx = max(lc.r, max(lc.g, lc.b)), lmn = min(lc.r, min(lc.g, lc.b)), ll = (lmx + lmn) * 0.5;\n"
         "         float f = s <= 0.0 ? 1.0 - floor(-s * 256.0 + 1e-4) / 256.0\n"
         "                            : min(floor(65280.0 / max(255.0 - floor(s * 254.0 + 1e-4), 1.0)), 32767.0) / 256.0;\n"
         "         float d = (lmx - lmn) * f;\n"
         "         hOut = ll - d * 0.5 + d * HueRgb(Hue(lc) * 360.0 + uAmt + (rng == 0 ? dh : 0.0));\n"
         "         if (rng == 0 && dl != 0.0) { float tg = dl > 0.0 ? max(hOut.r, max(hOut.g, hOut.b)) : min(hOut.r, min(hOut.g, hOut.b));\n"
         "            hOut += (tg - hOut) * min(abs(dl), 1.0); }\n"
         "         c = mix(c, uLinear == 1 ? SrgbEncode(max(hOut, 0.0)) : hOut, hw);\n"
         "      } else\n"
         "         c = mix(c, clamp(hOut, 0.0, 1.0), hw);\n"
         "      }\n"
         "   } else if (uType == 9) {\n"
         "      c = mix(vec3(Luma(c)), c, uAmt);\n"
         "   } else if (uType == 10) {\n"   // exposure + offset + gamma correction in linear light
         "      vec3 l = uLinear == 1 ? max(src.rgb, 0.0) : SrgbDecode(c);\n"
         "      l = max(l * exp2(uAmt) + uP[0], 0.0);\n"
         "      l = pow(l, vec3(1.0 / max(uP[1], 0.05)));\n"
         "      c = SrgbEncode(uHead == 1 ? l : clamp(l, 0.0, 1.0));\n"
         "   } else if (uType == 11) {\n"
         "      c = mix(c, 1.0 - c, uAmt);\n"
         "   } else if (uType == 12) {\n"
         "      c = (c - uLevels.x) / max(uLevels.y - uLevels.x, 1e-3);\n"
         "      c = (uHead == 1 ? max(c, 0.0) : clamp(c, 0.0, 1.0));\n"
         "      c = LevelsGamma(c, max(uAmt, 0.01));\n"
         "      c = mix(vec3(uLevels.z), vec3(uLevels.w), c);\n"
         "      for (int i = 0; i < 3; i++) {\n"
         "         vec4 L = uLvCh[i];\n"
         "         float ci = (c[i] - L.x) / max(L.y - L.x, 1e-3);\n"
         "         ci = (uHead == 1 ? max(ci, 0.0) : clamp(ci, 0.0, 1.0));\n"
         "         ci = LevelsGamma1(ci, max(uP[i], 0.01));\n"
         "         c[i] = mix(L.z, L.w, ci);\n"
         "      }\n"
         "   } else if (uType == 13) {\n"
         "      vec3 m;\n"
         "      if (uP[0] > 0.5) {\n"
         "         float l0 = max(Luma(c), 1e-5), l1 = Lut(l0);\n"
         "         m = clamp(c * (l1 / l0), 0.0, 1.0);\n"
         "      } else {\n"
         "         m = vec3(Lut(c.r), Lut(c.g), Lut(c.b));\n"
         "         m = vec3(LutC(0, m.r), LutC(1, m.g), LutC(2, m.b));\n"
         "      }\n"
         "      c = mix(c, m, uAmt);\n"
         "   } else if (uType == 14) {\n"   // colour balance: Photoshop's, Preserve Luminosity on (PhotoCraft color_balance_channel):
         "      vec3 S = vec3(uP[0], uP[1], uP[2]) * 100.0, M = vec3(uP[3], uP[4], uP[5]) * 100.0, T = vec3(uP[6], uP[7], uP[8]) * 100.0;\n"
         "      float smax = max(S.r, max(S.g, S.b)), tmin = min(T.r, min(T.g, T.b)), mmid = (max(M.r, max(M.g, M.b)) + min(M.r, min(M.g, M.b))) * 0.5;\n"
         "      vec3 o = c;\n"   // the three tones fold into one Levels per channel: black, white, midtone gamma
         "      bool pl = uP[9] > 0.5;\n"   // Preserve Luminosity off: a slider moves only its own channel
         "      for (int i = 0; i < 3; i++) {\n"
         "         float b = (pl ? smax - S[i] : max(-S[i], 0.0)) / 255.0, w = 1.0 - (pl ? T[i] - tmin : max(T[i], 0.0)) / 255.0;\n"
         "         float g = exp2((pl ? M[i] - mmid : M[i] + (S[i] + T[i]) * 0.5) / 100.0), e = 1.0 / max(g, 0.01), v = clamp(c[i], 0.0, 1.0);\n"
         "         if (v >= w) { o[i] = 1.0; continue; }\n"
         "         if (v <= b) { o[i] = 0.0; continue; }\n"
         "         float u = (v - b) / (w - b);\n"
         "         o[i] = pow(u, e);\n"
         "         if (g > 1.0) {\n"   // a gamma over 1 starts at slope 2^gamma and joins the power curve in a Hermite
         "            float s0 = exp2(g), u0 = min(2.0 * pow(s0, 1.0 / (e - 1.0)), 1.0);\n"
         "            if (u0 > 0.0 && u < u0) {\n"
         "               float t = u / u0, t2 = t * t, t3 = t2 * t, p = pow(u0, e), d = e * pow(u0, e - 1.0);\n"
         "               o[i] = clamp((t3 - 2.0 * t2 + t) * u0 * s0 + (3.0 * t2 - 2.0 * t3) * p + (t3 - t2) * u0 * d, 0.0, 1.0);\n"
         "            }\n"
         "         }\n"
         "      }\n"
         "      c = mix(c, o, uAmt);\n"
         "   } else if (uType == 15) {\n"   // vibrance: Photoshop's, measured (PhotoCraft vibrance_lin, MIT/Apache-2.0)
         "      c = SrgbEncode(VibranceLin(SrgbDecode(clamp(c, 0.0, 1.0)), clamp(uAmt, -1.0, 1.0)));\n"
         "   } else if (uType == 16) {\n"   // selective colour: Photoshop's, measured (PhotoCraft selective_color, MIT/Apache-2.0)
         "      float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), md = c.r + c.g + c.b - mx - mn;\n"
         "      float w[9];\n"   // reds, yellows, greens, cyans, blues, magentas by chroma in the sector; whites, neutrals, blacks
         "      w[0] = c.r >= mx ? mx - md : 0.0; w[1] = c.b <= mn ? md - mn : 0.0;\n"
         "      w[2] = c.g >= mx ? mx - md : 0.0; w[3] = c.r <= mn ? md - mn : 0.0;\n"
         "      w[4] = c.b >= mx ? mx - md : 0.0; w[5] = c.g <= mn ? md - mn : 0.0;\n"
         "      w[6] = max((mn - 0.5) * 2.0, 0.0);\n"
         "      w[7] = clamp(1.0 - abs(mx - 0.5) - abs(mn - 0.5), 0.0, 1.0);\n"
         "      w[8] = max((0.5 - mx) * 2.0, 0.0);\n"
         "      vec3 ink = 1.0 - clamp(c, 0.0, 1.0), delta = vec3(0.0);\n"
         "      for (int k = 0; k < 9; k++) {\n"
         "         if (w[k] <= 0.0) continue;\n"
         "         vec3 d = vec3(uP[k * 4], uP[k * 4 + 1], uP[k * 4 + 2]) + vec3(uP[k * 4 + 3]);\n"
         // relative: scaled by the ink already there (pure white can't be tinted); absolute: by the room the ink has
         "         d *= uP[36] > 0.5 ? ink : mix(min(vec3(1.0), 2.0 * ink), min(vec3(1.0), 2.0 * (1.0 - ink)), step(vec3(0.0), d));\n"
         "         delta += d * w[k];\n"
         "      }\n"
         "      c = clamp(c - delta * uAmt, 0.0, 1.0);\n"
         "   } else if (uType == 17) {\n"   // channel mixer (+ per-channel constant offset)
         "      vec3 m = vec3(dot(c, vec3(uP[0], uP[1], uP[2])) + uP[10],\n"
         "                    dot(c, vec3(uP[3], uP[4], uP[5])) + uP[11],\n"
         "                    dot(c, vec3(uP[6], uP[7], uP[8])) + uP[12]);\n"
         "      if (uP[9] > 0.5) m = vec3(m.r);\n"
         "      c = mix(c, m, uAmt);\n"
         "   } else if (uType == 18) {\n"   // black & white: each hue family's weight sets how light it turns + optional tint
         "      float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), md = c.r + c.g + c.b - mx - mn;\n"
         "      int pr = (c.r >= c.g && c.r >= c.b) ? 0 : (c.g >= c.b ? 2 : 4);\n"
         "      int se = (c.b <= c.r && c.b <= c.g) ? 1 : (c.r <= c.g ? 3 : 5);\n"
         "      float g = mn + (md - mn) * uP[se] + (mx - md) * uP[pr];\n"
         "      g = (uHead == 1 ? max(g, 0.0) : clamp(g, 0.0, 1.0));\n"
         "      vec3 bw = vec3(g);\n"
         // Photoshop tints like the Color blend mode: the tint's hue and saturation at the grey's W3C luminosity, then
         // ClipColor (PhotoCraft compose/adjust.rs BlackWhite, Apache 2.0); a headroom document keeps values over 1 only for a grey already over 1.
         "      if (uP[6] > 0.5) {\n"
         "         const vec3 W3 = vec3(0.3, 0.59, 0.11);\n"
         "         vec3 t = uColA.rgb + (g - dot(uColA.rgb, W3));\n"
         "         float L = dot(t, W3), n = min(t.r, min(t.g, t.b)), x = max(t.r, max(t.g, t.b));\n"
         "         if (n < 0.0) t = L + (t - L) * L / max(L - n, 1e-6);\n"
         "         if (x > 1.0 && (uHead != 1 || L <= 1.0)) t = L + (t - L) * (1.0 - L) / max(x - L, 1e-6);\n"
         "         bw = (uHead == 1 ? max(t, 0.0) : clamp(t, 0.0, 1.0));\n"
         "      }\n"
         "      c = mix(c, bw, uAmt);\n"
         "   } else if (uType == 19) {\n"   // photo filter: through a coloured glass, lightness kept if asked
         // Photoshop's (PhotoCraft photo_filter_matrix): the pixel's D50 XYZ times the filter's, relative to white,
         // in linear light; Preserve Luminosity puts the encoded luminosity back as the Luminosity blend does.
         "      const mat3 toX = mat3(0.4360747, 0.2225045, 0.0139322, 0.3850649, 0.7168786, 0.0971045, 0.1430804, 0.0606169, 0.7141733);\n"
         "      const mat3 toR = mat3(3.1338561, -0.9787684, 0.0719453, -1.6168667, 1.9161415, -0.2289914, -0.4906146, 0.0334540, 1.4052427);\n"
         "      vec3 fx = toX * SrgbDecode(clamp(uColA.rgb, 0.0, 1.0));\n"
         "      vec3 s = 1.0 - uAmt + uAmt * fx / vec3(0.9642, 1.0, 0.8251);\n"
         "      vec3 n = SrgbEncode(clamp(toR * (s * (toX * SrgbDecode(clamp(c, 0.0, 1.0)))), 0.0, 1.0));\n"
         "      if (uP[0] > 0.5) {\n"
         "         n += LumaPs(c) - LumaPs(n);\n"
         "         float L = LumaPs(n), lo = min(n.r, min(n.g, n.b)), hi = max(n.r, max(n.g, n.b));\n"
         "         if (lo < 0.0) n = L + (n - L) * L / max(L - lo, 1e-6);\n"
         "         if (hi > 1.0) n = L + (n - L) * (1.0 - L) / max(hi - L, 1e-6);\n"
         "      }\n"
         "      c = n;\n"
         "   } else if (uType == 20) {\n"   // shadows / highlights: edge-aware bilateral luma lift/cut + midtone contrast
         "      float b = texture(uBlurL, vUv).r, l0 = Luma(c);\n"
         "      float edgeW = exp(-abs(l0 - b) * 5.0);\n"
         "      float eb = mix(l0, b, edgeW);\n"
         "      float lift = uP[0] * (1.0 - smoothstep(0.0, 0.5, eb));\n"
         "      float cut = uP[1] * smoothstep(0.5, 1.0, eb);\n"
         "      c = c + lift * (1.0 - c) * c * 2.2;\n"
         "      c = 1.0 - (1.0 - clamp(c, 0.0, 1.0)) * (1.0 + 1.8 * cut);\n"
         "      if (abs(uP[2]) > 1e-3) {\n"
         "         vec3 v = clamp(c, 0.0, 1.0);\n"
         "         c = clamp(v + uP[2] * 0.4 * (v - 0.5) * 4.0 * v * (1.0 - v), 0.0, 1.0);\n"
         "      }\n"
         "   } else if (uType == 21) {\n"   // colour lookup (.cube)
         "      if (uCubeN > 1.0) {\n"
         "         vec3 q = clamp(c, 0.0, 1.0) * (uCubeN - 1.0) / uCubeN + 0.5 / uCubeN;\n"
         "         c = mix(c, texture(uCube, q).rgb, uAmt);\n"
         "      }\n"
         "   } else if (uType == 22) {\n"   // white balance (Affinity's, measured): a 3x3 in linear light, from uP[2..10]
         "      vec3 l = SrgbDecode(c);\n"
         "      vec3 n = vec3(dot(l, vec3(uP[2], uP[3], uP[4])), dot(l, vec3(uP[5], uP[6], uP[7])), dot(l, vec3(uP[8], uP[9], uP[10])));\n"
         "      c = mix(c, SrgbEncode(clamp(n, 0.0, 1.0)), uAmt);\n"
         "   } else if (uType == 23) {\n"   // split toning (Affinity's, measured): soft-light a hue in, Rec.601 luma kept
         "      const vec3 k = vec3(0.299, 0.587, 0.114);\n"
         "      float l = dot(c, k), p = uP[4];\n"
         "      float wh = l > p ? sin(1.5707963 * clamp((l - p) / max(1.0 - p, 1e-6), 0.0, 1.0)) : 0.0;\n"
         "      float ws = l < p ? sin(1.5707963 * clamp((p - l) / max(p, 1e-6), 0.0, 1.0)) : 0.0;\n"
         "      vec3 eh = SoftPs(c, HueRgb(uP[0])) - c, es = SoftPs(c, HueRgb(uP[2])) - c;\n"
         "      vec3 o = c + wh * uP[1] * (eh - dot(eh, k)) + ws * uP[3] * (es - dot(es, k));\n"
         "      float lo = dot(o, k); vec3 ch = o - lo; float s = 1.0;\n"   // out of gamut: pull towards the luma, not clamp
         "      for (int i = 0; i < 3; i++) s = ch[i] > 1e-6 ? min(s, (1.0 - lo) / ch[i]) : ch[i] < -1e-6 ? min(s, -lo / ch[i]) : s;\n"
         "      c = mix(c, clamp(lo + ch * max(s, 0.0), 0.0, 1.0), uAmt);\n"
         "   } else if (uType == 24) {\n"   // recolour (Affinity's, measured): HSL colourize, its lightness kept
         "      float l = (max(c.r, max(c.g, c.b)) + min(c.r, min(c.g, c.b))) * 0.5;\n"
         "      l = uP[2] < 0.0 ? l * (1.0 + uP[2]) : l + (1.0 - l) * uP[2];\n"
         "      float ch = (1.0 - abs(2.0 * l - 1.0)) * clamp(uP[1], 0.0, 1.0);\n"
         "      c = mix(c, l + ch * (HueRgb(uP[0]) - 0.5), uAmt);\n"
         "   } else if (uType == 25) {\n"   // normals (Affinity's, measured): xy scaled, flipped, turned, z rebuilt
         "      vec2 v = (c.rg * 2.0 - 1.0) * uP[1]; float a = radians(uP[0]);\n"   // flips come before the turn
         "      if (uP[2] > 0.5) v.x = -v.x;\n"
         "      if (uP[3] > 0.5) v.y = -v.y;\n"
         "      v = vec2(v.x * cos(a) - v.y * sin(a), v.x * sin(a) + v.y * cos(a));\n"
         "      float vl = length(v); if (vl > 1.0) v /= vl;\n"
         "      c = mix(c, vec3(v, sqrt(max(0.0, 1.0 - dot(v, v)))) * 0.5 + 0.5, uAmt);\n"
         "   } else if (uType == 26) {\n"   // tone compression (Affinity's, measured): a Reinhard curve per channel or on luma
         "      float e = max(uP[0], 0.01);\n"
         "      vec3 lv = c * e; vec3 pc = lv * (1.0 + 0.006 * lv) / (1.0 + lv);\n"
         "      float l = dot(c, vec3(0.2126, 0.7152, 0.0722)), ll = l * e;\n"
         "      vec3 pl = c * ((ll * (1.0 + 0.006 * ll) / (1.0 + ll)) / max(l, 1e-6));\n"
         "      c = mix(c, pow(clamp(mix(pc, pl, uP[2]), 0.0, 1.0), vec3(max(uP[1], 0.01))), uAmt);\n"
         "   } else if (uType == 27) {\n"   // tone stretch (Affinity's, fitted; approximate on saturated colour above 0 compression)
         "      float k = clamp(uP[2], 0.0, 1.0), pw = 1.32 - 0.31 * k, ap = pow(0.00194 * exp(0.31 * k), pw);\n"
         "      vec3 u = pow(max(c * uP[0], 0.0), vec3(pw));\n"
         "      vec3 h = (204.0 - 73.0 * k) / 255.0 * u / (u + ap);\n"
         "      c = mix(c, pow(clamp(h, 0.0, 1.0), vec3(max(uP[1], 0.01))), uAmt);\n"
         "   } else if (uType == 28) {\n"   // noise & grain: Gaussian, Uniform, Film Grain, Salt & Pepper, Fine Blue
         "      int mode = int(clamp(uP[0] + 0.5, 0.0, 4.0));\n"
         "      float sz = max(uP[1], 1.0), mono = uP[2], bmode = uP[3], seed = uP[4], clump = clamp(uP[5], 0.0, 1.0);\n"
         "      vec2 gp = floor(px / sz);\n"
         "      vec3 n = vec3(0.0);\n"
         "      if (mode == 0) {\n"
         "         n = mono > 0.5 ? vec3(AdjGauss(gp, seed)) : vec3(AdjGauss(gp, seed), AdjGauss(gp, seed + 17.1), AdjGauss(gp, seed + 41.3));\n"
         "      } else if (mode == 1) {\n"
         "         n = mono > 0.5 ? vec3(AdjHash12(gp + seed) * 2.0 - 1.0)\n"
         "                        : vec3(AdjHash12(gp + seed), AdjHash12(gp + seed + 17.1), AdjHash12(gp + seed + 41.3)) * 2.0 - 1.0;\n"
         "      } else if (mode == 2) {\n"
         "         float fg = mono > 0.5 ? AdjGauss(gp, seed) : 0.0;\n"
         "         vec2 gp2 = gp / 2.2;\n"
         "         float cg = AdjValueNoise(gp2 + seed * 7.3) * 2.0 - 1.0;\n"
         "         vec3 gvec = mono > 0.5 ? vec3(fg) : vec3(AdjGauss(gp, seed), AdjGauss(gp, seed + 17.1), AdjGauss(gp, seed + 41.3));\n"
         "         n = mix(gvec, gvec * 0.65 + vec3(cg) * 0.55, clump);\n"
         "         float lum = clamp(Luma(c), 0.0, 1.0);\n"
         "         n *= mix(0.4, 1.0, 4.0 * lum * (1.0 - lum));\n"
         "      } else if (mode == 3) {\n"
         "         float dens = mix(0.02, 0.45, clamp(uAmt, 0.0, 1.0));\n"
         "         float h = AdjHash12(gp + seed);\n"
         "         if (h < dens * 0.5) n = vec3(-1.0); else if (h < dens) n = vec3(1.0); else n = vec3(0.0);\n"
         "         if (mono < 0.5 && h < dens) n *= vec3(AdjHash12(gp + seed + 3.0), AdjHash12(gp + seed + 9.0), AdjHash12(gp + seed + 27.0)) * 1.4;\n"
         "      } else {\n"
         "         float c0 = AdjHash12(gp + seed);\n"
         "         float nb = 0.25 * (AdjHash12(gp + vec2(1.0, 0.0) + seed) + AdjHash12(gp - vec2(1.0, 0.0) + seed)\n"
         "                          + AdjHash12(gp + vec2(0.0, 1.0) + seed) + AdjHash12(gp - vec2(0.0, 1.0) + seed));\n"
         "         float b = clamp((c0 - nb) * 1.35, -1.0, 1.0);\n"
         "         n = mono > 0.5 ? vec3(b) : vec3(b, clamp((AdjHash12(gp + seed + 13.0) - nb) * 1.35, -1.0, 1.0),\n"
         "                                            clamp((AdjHash12(gp + seed + 29.0) - nb) * 1.35, -1.0, 1.0));\n"
         "      }\n"
         "      if (mode == 3) {\n"
         "         vec3 sp = mix(c, step(0.0, n), step(0.01, abs(n)));\n"
         "         c = mix(c, sp, clamp(uAmt, 0.0, 1.0));\n"
         "      } else if (bmode < 0.5) {\n"
         "         c = clamp(c + n * uAmt * 0.45 * (1.0 - abs(2.0 * c - 1.0) * 0.45), 0.0, 1.0);\n"
         "      } else if (bmode < 1.5) {\n"
         "         c = clamp(c + n * uAmt * 0.5, 0.0, 1.0);\n"
         "      } else {\n"
         "         c = clamp(c * (1.0 + n * uAmt * 0.75), 0.0, 1.0);\n"
         "      }\n"
         "   } else if (uType == 29) {\n"   // procedural texture: Perlin, Simplex, fBm Clouds, Ridged Marble, Turbulence, Voronoi, Worley Cracks, Weave, Scanlines
         "      int pat = int(clamp(uP[0] + 0.5, 0.0, 8.0));\n"
         "      float scale = max(uP[1], 0.25), warp = max(uP[4], 0.0), seed = uP[5];\n"
         "      int oct = int(clamp(uP[2] + 0.5, 1.0, 8.0)), bmode = int(clamp(uP[6] + 0.5, 0.0, 4.0));\n"
         "      float detail = max(uP[3], 0.05), inv = uP[7];\n"
         "      vec2 asp = uDoc / max(min(uDoc.x, uDoc.y), 1.0);\n"
         "      vec2 tp = (vUv * asp) * scale + vec2(seed * 3.7, seed * 7.1);\n"
         "      if (warp > 0.001) {\n"
         "         vec2 wv = vec2(AdjFbm(tp + vec2(1.7, 9.2), min(oct, 4), 0) - 0.5,\n"
         "                        AdjFbm(tp + vec2(8.3, 2.8), min(oct, 4), 0) - 0.5);\n"
         "         tp += wv * warp * 2.5;\n"
         "      }\n"
         "      float tv = 0.5;\n"
         "      if (pat == 0) tv = AdjPerlin(tp);\n"
         "      else if (pat == 1) tv = AdjSimplex(tp);\n"
         "      else if (pat == 2) tv = AdjFbm(tp, oct, 0);\n"
         "      else if (pat == 3) tv = AdjFbm(tp, oct, 1);\n"
         "      else if (pat == 4) tv = AdjFbm(tp, oct, 2);\n"
         "      else if (pat == 5) tv = clamp(AdjVoronoi(tp).x, 0.0, 1.0);\n"
         "      else if (pat == 6) { vec2 v = AdjVoronoi(tp); tv = clamp(1.0 - (v.y - v.x) * 2.5, 0.0, 1.0); }\n"
         "      else if (pat == 7) {\n"
         "         float wx = 0.5 + 0.5 * sin(tp.x * 6.2831853 + 0.4 * AdjPerlin(tp * 0.5));\n"
         "         float wy = 0.5 + 0.5 * sin(tp.y * 6.2831853 + 0.4 * AdjPerlin(tp * 0.5 + 9.1));\n"
         "         float fib = AdjFbm(tp * 2.0, min(oct, 4), 0);\n"
         "         tv = clamp(mix(wx, wy, 0.5 + 0.35 * sin((tp.x + tp.y) * 3.14159)) * 0.7 + fib * 0.3, 0.0, 1.0);\n"
         "      } else {\n"
         "         float s0 = 0.5 + 0.5 * sin(tp.y * 6.2831853);\n"
         "         float s1 = 0.5 + 0.5 * cos(tp.x * 6.2831853 * 0.5);\n"
         "         tv = clamp(mix(s0, s0 * s1, clamp(warp * 0.5 + 0.15, 0.0, 1.0)), 0.0, 1.0);\n"
         "      }\n"
         "      tv = clamp((tv - 0.5) * detail + 0.5, 0.0, 1.0);\n"
         "      if (inv > 0.5) tv = 1.0 - tv;\n"
         "      vec3 tc = uMap == 1 ? mix(uColA.rgb, uColB.rgb, tv) : vec3(tv);\n"
         "      vec3 outc = tc;\n"
         "      if (bmode == 1) outc = mix(2.0 * c * tc, 1.0 - 2.0 * (1.0 - c) * (1.0 - tc), step(vec3(0.5), c));\n"
         "      else if (bmode == 2) outc = c * tc;\n"
         "      else if (bmode == 3) outc = 1.0 - (1.0 - c) * (1.0 - tc);\n"
         "      else if (bmode == 4) outc = SoftPs(c, tc);\n"
         "      c = mix(c, outc, clamp(uAmt, 0.0, 1.0));\n"
         "   } else if (uType == 30) {\n"   // lens blur: a depth-driven iris blur, gathered (scatter-as-gather) in linear light
         "      float focus = uP[0], band = uP[1];\n"
         "      bool hex = uP[2] > 0.5;\n"
         "      float dc = uHasDepth == 1 ? texture(uDepth, vUv).r : 0.0;\n"
         "      float cc = uHasDepth == 1 ? uAmt * clamp((abs(dc - focus) - band) * 2.0, 0.0, 1.0) : uAmt;\n"
         "      vec3 lc = uLinear == 1 ? max(src.rgb, 0.0) : SrgbDecode(clamp(src.rgb, 0.0, 1.0));\n"
         "      float w0 = 1.0 / max(cc * cc, 1.0);\n"
         "      vec3 acc = lc * w0; float wsum = w0;\n"
         "      if (uAmt >= 0.5) for (int k = 0; k < 96; k++) {\n"
         "         float th = float(k) * 2.39996323, rr = sqrt((float(k) + 0.5) / 96.0) * uAmt;\n"
         "         if (hex) { float a = mod(th, 1.0471976) - 0.5235988; rr *= 0.8660254 / cos(a); }\n"
         "         vec2 o = vec2(cos(th), sin(th)) * rr, q = (px + o) / uDoc;\n"
         "         if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0) continue;\n"
         "         float ds = uHasDepth == 1 ? texture(uDepth, q).r : 0.0;\n"
         "         float cs = uHasDepth == 1 ? uAmt * clamp((abs(ds - focus) - band) * 2.0, 0.0, 1.0) : uAmt;\n"
         "         if (ds < dc) cs = min(cs, cc);\n"   // what lies behind never spills over what is in front
         "         float w = smoothstep(rr - 1.0, rr + 1.0, cs) / max(cs * cs, 1.0);\n"
         "         vec4 sv = texture(uSrc, q);\n"
         "         acc += (uLinear == 1 ? max(sv.rgb, 0.0) : SrgbDecode(clamp(sv.rgb, 0.0, 1.0))) * w; wsum += w;\n"
         "      }\n"
         "      c = SrgbEncode(max(acc / wsum, 0.0));\n"
         "   }\n"
         "   if (uType == 4 || (uMap == 1 && uType != 29)) c = gradAt(clamp(LumaPs(c), 0.0, 1.0), uColA, uColB).rgb;\n"
         "   if (uType == 4 && uGmDither == 1) c += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;\n"
         "   c = uHead == 1 && uLinear == 1 ? max(c, 0.0) : clamp(c, 0.0, 1.0);\n"
         "   fragColor = vec4(uLinear == 1 ? SrgbDecode(c) : c, src.a);\n"
         "}\n";
      return src;
   }

   float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

   // 3x3 inverse by the adjugate; the sign is kept so w stays positive in front of the camera.
   void Invert3(const float m[9], float o[9])
   {
      o[0] = m[4] * m[8] - m[5] * m[7];
      o[1] = m[2] * m[7] - m[1] * m[8];
      o[2] = m[1] * m[5] - m[2] * m[4];
      o[3] = m[5] * m[6] - m[3] * m[8];
      o[4] = m[0] * m[8] - m[2] * m[6];
      o[5] = m[2] * m[3] - m[0] * m[5];
      o[6] = m[3] * m[7] - m[4] * m[6];
      o[7] = m[1] * m[6] - m[0] * m[7];
      o[8] = m[0] * m[4] - m[1] * m[3];
      const float det = m[0] * o[0] + m[1] * o[3] + m[2] * o[6];
      const float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
      for (int i = 0; i < 9; i++)
         o[i] *= id;
   }

   unsigned long long Mix(unsigned long long h, unsigned long long v) { return (h ^ v) * 1099511628211ull; }
}

const std::vector<std::string>& Scene2DNode::FitNames()
{
   static const std::vector<std::string> names = { "None", "Fit", "Fill", "Stretch" };
   return names;
}

Scene2DNode::~Scene2DNode()
{
   for (Pair& p : mPool)
   {
      GLUtil::DestroyFbo(p.f[0]);
      GLUtil::DestroyFbo(p.f[1]);
   }
   GLUtil::DestroyFbo(mOut);
   for (auto& kv : mLayerOut)
      GLUtil::DestroyFbo(kv.second);
   GLUtil::DestroyFbo(mAdjFbo);
   for (auto& kv : mFillPics)
      if (kv.second.tex != 0)
         glDeleteTextures(1, &kv.second.tex);
   for (auto& kv : mRelief)
      GLUtil::DestroyFbo(kv.second);
   GLUtil::DestroyFbo(mBelow);
   for (auto& kv : mSurfaces)
      GLUtil::DestroyFbo(kv.second.fbo);
   for (auto& kv : mMasks)
      GLUtil::DestroyFbo(kv.second.fbo);
   for (auto& kv : mFxMasks)
      GLUtil::DestroyFbo(kv.second.fbo);
   for (auto* m : { &mVec, &mVMasks })
      for (auto& kv : *m)
         if (kv.second.tex != 0)
            glDeleteTextures(1, &kv.second.tex);
   GLUtil::DestroyFbo(mSelTex);
   GLUtil::DestroyFbo(mSelOverlay);
   GLUtil::DestroyFbo(mMaskView);
   GLUtil::DestroyFbo(mSelScratch);
   GLUtil::DestroyFbo(mCloneSrc);
   GLUtil::DestroyFbo(mWarpSel);
   for (auto& kv : mWarpMaskFrom)
      GLUtil::DestroyFbo(kv.second.fbo);
   mWarpMaskFrom.clear();
   for (auto& kv : mUndoSurf)
      GLUtil::DestroyFbo(kv.second.fbo);
   mUndoSurf.clear();
   if (mWarpProgram != 0)
      glDeleteProgram(mWarpProgram);
   mWarpProgram = 0;
   for (GLUtil::Fbo& f : mBrushFrozen)
      GLUtil::DestroyFbo(f);
   for (GLUtil::Fbo* f : { &mEff[0], &mEff[1], &mShBlur[0], &mShBlur[1] })
      GLUtil::DestroyFbo(*f);
   if (mLutTex != 0)
      glDeleteTextures(1, &mLutTex);
   for (auto& kv : mLensDepth)
      if (kv.second.tex != 0)
         glDeleteTextures(1, &kv.second.tex);
   for (unsigned p : { mFillProgram, mAntsProgram, mResampleProgram, mAdjProgram, mReliefProgram, mEffMixProgram, mLumaBlurProgram })
      if (p != 0)
         glDeleteProgram(p);
   if (mPassProgram != 0)
      glDeleteProgram(mPassProgram);
   if (mBatchProgram != 0)
      glDeleteProgram(mBatchProgram);
   if (mFinishProgram != 0)
      glDeleteProgram(mFinishProgram);
   for (auto& kv : mDepth)
      for (unsigned t : kv.second.tex)
         if (t != 0)
            glDeleteTextures(1, &t);
   GLUtil::DestroyFbo(mFxBlur[0]);
   GLUtil::DestroyFbo(mFxBlur[1]);
   GLUtil::DestroyFbo(mFxBlur2[0]);
   GLUtil::DestroyFbo(mFxBlur2[1]);
   GLUtil::DestroyFbo(mFxOut);
   GLUtil::DestroyFbo(mFxExtra);
   GLUtil::DestroyFbo(mFxMore[0]);
   GLUtil::DestroyFbo(mFxMore[1]);
   for (unsigned p : { mBlurProgram, mFxProgram, mFxCombineProgram })
      if (p != 0)
         glDeleteProgram(p);
}

int Scene2DNode::LiveTextures() const
{
   int n = 0;
   for (const auto& kv : mLayerOut) n += kv.second.tex != 0;
   for (const auto& kv : mRelief) n += kv.second.tex != 0;
   for (const auto& kv : mSurfaces) n += kv.second.fbo.tex != 0;
   for (const auto& kv : mMasks) n += kv.second.fbo.tex != 0;
   for (const auto& kv : mFxMasks) n += kv.second.fbo.tex != 0;
   for (const auto* m : { &mVec, &mVMasks })
      for (const auto& kv : *m) n += kv.second.tex != 0;
   for (const auto& kv : mDepth)
      for (unsigned t : kv.second.tex) n += t != 0;
   return n;
}

// ---- model --------------------------------------------------------------

const Scene2DNode::Layer* Scene2DNode::FindConst(int id) const
{
   for (const Layer& l : mLayers)
      if (l.id == id)
         return &l;
   return nullptr;
}

Scene2DNode::Layer* Scene2DNode::FindLayer(int id)
{
   Layers();
   for (Layer& l : mLayers)
      if (l.id == id)
         return &l;
   return nullptr;
}

Scene2DNode::Layer& Scene2DNode::AddLayer(Kind kind, uint64_t uid, int above)
{
   Layers();
   Layer l;
   l.id = mNextId++;
   l.kind = kind;
   l.uid = uid;
   l.x = mDoc.w * 0.5f;
   l.y = mDoc.h * 0.5f;
   if (kind == Kind::Group)
      l.blend = kPassThrough;
   if (kind == Kind::Adjust)
   {
      const float black[4] = { 0, 0, 0, 1 }, white[4] = { 1, 1, 1, 1 };
      std::copy(black, black + 4, l.colorA);
      std::copy(white, white + 4, l.colorB);
   }
   if (kind == Kind::Vector)
   {
      l.pw = mDoc.w;
      l.ph = mDoc.h;
   }
   if (kind == Kind::Paint)
   {
      l.pw = mDoc.w;
      l.ph = mDoc.h;
      const int n = ((l.pw + TileStore::kTile - 1) / TileStore::kTile) * ((l.ph + TileStore::kTile - 1) / TileStore::kTile);
      l.tiles.assign(n, "-");
   }
   int at = (int)mLayers.size();
   // A new group one level past the nesting cap goes beside the enclosing group instead.
   while (kind == Kind::Group && FindConst(above) != nullptr && FindConst(above)->parent != 0 && Depth(above) + 1 > kMaxGroupDepth)
   {
      above = FindConst(above)->parent;
      std::fprintf(stderr, "PROBE new group: too deep, placed beside group %d\n", above);
   }
   if (const Layer* ref = FindConst(above))
   {
      l.parent = ref->parent;
      for (int i = 0; i < (int)mLayers.size(); i++)
         if (mLayers[i].id == above)
            at = i + 1;
      // Past the reference's own children when it is a group.
      while (at < (int)mLayers.size() && IsInside(mLayers[at].id, above))
         at++;
   }
   mLayers.insert(mLayers.begin() + at, l);
   Commit();
   return *FindLayer(l.id);
}

// Clipped layers clip to the nearest unclipped layer below them in the same parent; one with none is let go.
void Scene2DNode::ReleaseOrphanClips()
{
   std::vector<int> based;   // parents whose children have a base so far, walking bottom up
   for (Layer& l : mLayers)
   {
      const bool hasBase = std::find(based.begin(), based.end(), l.parent) != based.end();
      if (l.clip && !hasBase)
         l.clip = false;
      if (!l.clip && !hasBase)
         based.push_back(l.parent);
   }
}

std::vector<int> Scene2DNode::ClipGroupOf(int id)
{
   const Layer* l = FindConst(id);
   if (l == nullptr)
      return {};
   const std::vector<int> kids = ChildrenOf(l->parent);
   const long idx = std::find(kids.begin(), kids.end(), id) - kids.begin();
   long base = -1;
   for (long i = idx; i >= 0 && base < 0; i--)
      if (!FindConst(kids[i])->clip)
         base = i;
   if (base < 0)
      return {};
   std::vector<int> g{ kids[base] };
   for (long i = base + 1; i < (long)kids.size() && FindConst(kids[i])->clip; i++)
      g.push_back(kids[i]);
   return g.size() > 1 ? g : std::vector<int>{};
}

Scene2DNode::ClipsBefore Scene2DNode::ReadClips(const std::vector<int>& moving)
{
   ClipsBefore b;
   b.moving = moving;
   for (int id : moving)
   {
      const Layer* l = FindConst(id);
      if (l == nullptr)
         continue;
      if (l->clip)
         b.clipped.push_back(id);
      std::vector<int> g = ClipGroupOf(id);
      if (g.empty())
         continue;
      if (g.front() == id)   // a base: the layers clipped to it lose it unless they move too
         for (size_t k = 1; k < g.size(); k++)
            if (std::find(moving.begin(), moving.end(), g[k]) == moving.end())
               b.left.push_back(g[k]);
      b.groups.emplace_back(id, std::move(g));
   }
   return b;
}

void Scene2DNode::SettleClips(const std::vector<int>& placed, const ClipsBefore& before)
{
   // Topmost first, so a run of placed layers joins together.
   std::vector<std::pair<int, int>> order;   // stack index, id
   for (int id : placed)
      for (int i = 0; i < (int)mLayers.size(); i++)
         if (mLayers[i].id == id)
            order.emplace_back(i, id);
   std::sort(order.rbegin(), order.rend());
   for (const auto& [at, id] : order)
   {
      (void)at;
      static const std::vector<int> kNone;
      const std::vector<int>* group = &kNone;
      for (const auto& g : before.groups)
         if (g.first == id)
            group = &g.second;
      auto in = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
      const std::vector<int> kids = ChildrenOf(FindConst(id)->parent);
      const long k = std::find(kids.begin(), kids.end(), id) - kids.begin();
      const Layer* hi = k + 1 < (long)kids.size() ? FindConst(kids[k + 1]) : nullptr;
      const Layer* lo = k > 0 ? FindConst(kids[k - 1]) : nullptr;
      // 1. It joins the clip group it lands in: the layer above its slot is clipped (and not travelling with it).
      const bool joins = hi != nullptr && hi->clip && !(in(before.moving, hi->id) && in(*group, hi->id));
      // 2. A clipped layer stays clipped while it still sits on a clip group.
      const bool stays = in(before.clipped, id) && lo != nullptr && (lo->clip || in(*group, lo->id));
      FindLayer(id)->clip = joins || stays;
   }
   // 3. The layers that lost their base are let go; 4. so is a clipped run with nothing below it.
   for (int id : before.left)
      if (std::find(placed.begin(), placed.end(), id) == placed.end())
         if (Layer* l = FindLayer(id))
            l->clip = false;
   ReleaseOrphanClips();
}

void Scene2DNode::RemoveLayer(int id, bool settleClips)
{
   Layers();
   const ClipsBefore before = settleClips ? ReadClips({ id }) : ClipsBefore();
   for (int c : before.left)
      if (Layer* l = FindLayer(c))
         l->clip = false;   // deleting a base lets its clipped layers go (Photoshop)
   mLayers.erase(std::remove_if(mLayers.begin(), mLayers.end(), [&](const Layer& l) { return IsInside(l.id, id); }),
                 mLayers.end());
   // Modulator slots of removed layers go too.
   mDoc.slots.erase(std::remove_if(mDoc.slots.begin(), mDoc.slots.end(), [&](const ModSlot& m) { return FindConst(m.layer) == nullptr; }),
                    mDoc.slots.end());
   if (settleClips)
      ReleaseOrphanClips();
   Commit();
}

bool Scene2DNode::ReverseLayers(const std::vector<int>& ids)
{
   Layers();
   const Layer* first = ids.empty() ? nullptr : FindConst(ids[0]);
   if (first == nullptr)
      return false;
   const int parent = first->parent;
   std::vector<size_t> at;
   for (size_t i = 0; i < mLayers.size(); i++)
      if (mLayers[i].parent == parent && std::find(ids.begin(), ids.end(), mLayers[i].id) != ids.end())
         at.push_back(i);
   if (at.size() < 2)
      return false;
   for (size_t i = 0, j = at.size() - 1; i < j; i++, j--)
      std::swap(mLayers[at[i]], mLayers[at[j]]);
   Commit();
   return true;
}

std::vector<int> Scene2DNode::ChildrenOf(int parent)
{
   std::vector<int> out;
   for (const Layer& l : Layers())
      if (l.parent == parent)
         out.push_back(l.id);
   return out;
}

bool Scene2DNode::IsInside(int id, int group)
{
   for (int hops = 0; id != 0 && hops < 64; hops++)
   {
      if (id == group)
         return true;
      const Layer* l = FindConst(id);
      id = l != nullptr ? l->parent : 0;
   }
   return false;
}

int Scene2DNode::Depth(int id)
{
   int d = 0;
   const Layer* l = FindConst(id);
   while (l != nullptr && l->parent != 0 && d < 64)
   {
      d++;
      l = FindConst(l->parent);
   }
   return d;
}

int Scene2DNode::GroupHeight(int id)
{
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Group)
      return 0;
   const int base = Depth(id);
   int deepest = 0;
   for (const Layer& c : Layers())
      if (c.id != id && IsInside(c.id, id) && c.kind == Kind::Group)
         deepest = std::max(deepest, Depth(c.id) - base);
   return 1 + deepest;
}

int Scene2DNode::MaxGroupDepth()
{
   int d = 0;
   for (const Layer& l : Layers())
      if (l.kind == Kind::Group)
         d = std::max(d, Depth(l.id) + 1);
   return d;
}

bool Scene2DNode::CanGroup(const std::vector<int>& ids)
{
   Layers();
   int top = 0, topIndex = -1;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (std::find(ids.begin(), ids.end(), mLayers[i].id) != ids.end())
      {
         topIndex = i;
         top = mLayers[i].id;
      }
   if (topIndex < 0)
      return false;
   // The new group sits where the topmost picked layer is; every picked layer goes one level inside it.
   const int groupLevel = Depth(top) + 1;
   int levels = groupLevel;
   for (int id : ids)
      if (FindConst(id) != nullptr)
         levels = std::max(levels, groupLevel + GroupHeight(id));
   return levels <= kMaxGroupDepth;
}

void Scene2DNode::MoveLayer(int id, int target, bool below, bool into, bool settleClips)
{
   Layers();
   if (id == target || FindConst(id) == nullptr || FindConst(target) == nullptr || IsInside(target, id))
      return;
   if ((into ? Depth(target) + 1 : Depth(target)) + GroupHeight(id) > kMaxGroupDepth)
   {
      std::fprintf(stderr, "PROBE move layer %d: refused, groups would nest deeper than %d\n", id, kMaxGroupDepth);
      return;
   }
   const ClipsBefore before = settleClips ? ReadClips({ id }) : ClipsBefore();
   // Pull out the moving layer and everything under it, in order.
   std::vector<Layer> moving, rest;
   for (const Layer& l : mLayers)
      (IsInside(l.id, id) ? moving : rest).push_back(l);
   mLayers.swap(rest);
   const Layer* t = FindConst(target);
   const int newParent = into ? target : t->parent;
   int at = 0;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (mLayers[i].id == target)
         at = i;
   if (into || !below)
   {
      // Above the target: past it and its children.
      at++;
      while (at < (int)mLayers.size() && IsInside(mLayers[at].id, target))
         at++;
   }
   else
   {
      // Below the target: before its first child.
      while (at > 0 && IsInside(mLayers[at - 1].id, target))
         at--;
   }
   moving[0].parent = newParent;
   mLayers.insert(mLayers.begin() + at, moving.begin(), moving.end());
   if (settleClips)
      SettleClips({ id }, before);
   Commit();
}

int Scene2DNode::GroupLayersWith(const std::vector<int>& ids, const GroupOptions& o)
{
   const int g = GroupLayers(ids);
   Layer* gl = g != 0 ? FindLayer(g) : nullptr;
   if (gl == nullptr)
      return 0;
   if (!o.name.empty())
      gl->name = o.name;
   gl->label = std::clamp(o.label, 0, 7);
   gl->blend = o.blend;
   gl->opacity = std::isfinite(o.opacity) ? std::clamp(o.opacity, 0.0f, 1.0f) : 1.0f;
   gl->open = false;
   if (o.labelLayers && gl->label != 0)
      for (int id : ids)
         if (Layer* l = FindLayer(id))
            l->label = gl->label;
   std::fprintf(stderr, "PROBE group from layers %d: name '%s' label %d blend %d opacity %.2f\n", g, gl->name.c_str(), gl->label,
                gl->blend, gl->opacity);
   Commit();
   return g;
}

int Scene2DNode::GroupLayers(const std::vector<int>& ids)
{
   Layers();
   if (ids.empty())
      return 0;
   if (!CanGroup(ids))
   {
      std::fprintf(stderr, "PROBE group layers: refused, groups would nest deeper than %d\n", kMaxGroupDepth);
      return 0;
   }
   // The group takes the place of the topmost picked layer.
   int top = ids[0], topIndex = -1;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (std::find(ids.begin(), ids.end(), mLayers[i].id) != ids.end() && i > topIndex)
      {
         topIndex = i;
         top = mLayers[i].id;
      }
   const int gid = AddLayer(Kind::Group, 0, top).id;
   Layer* g = FindLayer(gid);
   g->name = "Group " + std::to_string(gid);
   // Move each picked layer (bottom first) into the group's top, keeping their order.
   std::vector<int> order;
   for (const Layer& l : mLayers)
      if (std::find(ids.begin(), ids.end(), l.id) != ids.end())
         order.push_back(l.id);
   std::vector<int> tops;
   for (int id : order)
   {
      bool parentPicked = false;
      for (int other : order)
         if (other != id && IsInside(id, other))
            parentPicked = true;
      if (!parentPicked)
         tops.push_back(id);
   }
   const ClipsBefore before = ReadClips(tops);
   for (int id : tops)
      MoveLayer(id, gid, false, true, false);
   SettleClips(tops, before);
   g = FindLayer(gid);
   g->clip = false;
   g->open = false;   // Group from Layers starts collapsed (Photoshop)
   Commit();
   return gid;
}

void Scene2DNode::Ungroup(int group)
{
   Layers();
   const Layer* g = FindConst(group);
   if (g == nullptr || g->kind != Kind::Group)
      return;
   const int parent = g->parent;
   for (Layer& l : mLayers)
      if (l.parent == group)
         l.parent = parent;
   mLayers.erase(std::remove_if(mLayers.begin(), mLayers.end(), [&](const Layer& l) { return l.id == group; }),
                 mLayers.end());
   Commit();
}

int Scene2DNode::Duplicate(int id)
{
   Layers();
   if (FindConst(id) == nullptr)
      return 0;
   std::vector<Layer> copies;
   std::vector<std::pair<int, int>> remap;
   int last = -1;
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (IsInside(mLayers[i].id, id))
      {
         Layer c = mLayers[i];
         remap.push_back({ c.id, mNextId });
         c.id = mNextId++;
         copies.push_back(c);
         last = i;
      }
   for (Layer& c : copies)
      for (auto& r : remap)
         if (c.parent == r.first)
            c.parent = r.second;
   if (!copies.empty() && !copies[0].name.empty() && sCopySuffix)
      copies[0].name = CopyName(copies[0].name);
   mLayers.insert(mLayers.begin() + last + 1, copies.begin(), copies.end());
   Commit();
   return copies.empty() ? 0 : copies[0].id;
}

uint64_t Scene2DNode::UnusedLinkGroup()
{
   std::vector<uint64_t> used;
   for (const Layer& l : Layers())
      if (l.link != 0)
         used.push_back(l.link);
   std::sort(used.begin(), used.end());
   used.erase(std::unique(used.begin(), used.end()), used.end());
   if (used.empty())
      return 1;
   if (used.back() < UINT64_MAX)
      return used.back() + 1;
   // A loaded file can store any id up to the top, so max + 1 must not wrap into 0 (no link) or an existing group.
   uint64_t free = 1;
   for (uint64_t g : used)
   {
      if (g > free)
         break;
      if (g == free)
         free++;
   }
   return free;
}

bool Scene2DNode::LinkLayers(const std::vector<int>& ids, int want, bool* changed)
{
   if (changed != nullptr)
      *changed = false;
   std::vector<Layer*> sel;
   for (int id : ids)
      if (Layer* l = FindLayer(id))
         sel.push_back(l);
   if (sel.empty())
      return false;
   const uint64_t first = sel[0]->link;
   bool same = first != 0, none = true;
   for (const Layer* l : sel)
   {
      same = same && l->link == first;
      none = none && l->link == 0;
   }
   int members = 0;
   for (const Layer& l : mLayers)
      members += same && l.link == first ? 1 : 0;
   if ((want == 1 && same && members >= 2) || (want == 0 && none))
      return want == 1;
   const bool unlink = (want < 0 ? same : want == 0) || sel.size() < 2;
   const uint64_t next = unlink ? 0 : UnusedLinkGroup();
   for (Layer* l : sel)
      l->link = next;
   std::map<uint64_t, int> count;
   for (const Layer& l : mLayers)
      if (l.link != 0)
         count[l.link]++;
   for (Layer& l : mLayers)
      if (l.link != 0 && count[l.link] < 2)
         l.link = 0;
   if (changed != nullptr)
      *changed = true;
   Commit();
   return !unlink;
}

std::vector<int> Scene2DNode::LinkedWith(const std::vector<int>& ids)
{
   std::set<uint64_t> groups;
   for (int id : ids)
      if (const Layer* l = FindConst(id); l != nullptr && l->link != 0)
         groups.insert(l->link);
   std::vector<int> out;
   if (!groups.empty())
      for (const Layer& l : Layers())
         if (groups.count(l.link) != 0)
            out.push_back(l.id);
   return out;
}

void Scene2DNode::RelinkCopies(const std::vector<std::pair<int, int>>& copies)
{
   std::map<uint64_t, int> count;
   for (const auto& [from, to] : copies)
      if (const Layer* o = FindConst(from); o != nullptr && o->link != 0)
         count[o->link]++;
   std::map<uint64_t, uint64_t> fresh;
   for (const auto& [from, to] : copies)
   {
      const Layer* o = FindConst(from);
      Layer* c = FindLayer(to);
      if (o == nullptr || c == nullptr)
         continue;
      if (o->link == 0 || count[o->link] < 2)
      {
         c->link = 0;
         continue;
      }
      if (fresh.count(o->link) == 0)
         fresh[o->link] = UnusedLinkGroup();
      c->link = fresh[o->link];
   }
   Commit();
}

std::vector<Scene2DNode::Layer> Scene2DNode::CopyLayers(const std::vector<int>& ids)
{
   Layers();
   std::vector<Layer> out;
   for (const Layer& l : mLayers)
      for (int id : ids)
         if (IsInside(l.id, id))
         {
            out.push_back(l);
            break;
         }
   return out;
}

std::vector<int> Scene2DNode::PasteLayers(const std::vector<Layer>& clip, int above, float dx, float dy)
{
   Layers();
   std::vector<int> tops;
   if (clip.empty())
      return tops;
   std::vector<std::pair<int, int>> remap;
   std::vector<Layer> copies;
   for (const Layer& src : clip)
   {
      Layer c = src;
      remap.push_back({ c.id, mNextId });
      c.id = mNextId++;
      copies.push_back(c);
   }
   int parentOfPaste = 0;
   if (const Layer* a = FindConst(above))
      parentOfPaste = (a->kind == Kind::Group && a->open) ? a->id : a->parent;   // into an open group, on top
   // The pasted groups' own nesting (levels under the paste point); too deep and they go out beside the enclosing group.
   int pasteLevels = 0;
   for (const Layer& c : clip)
      if (c.kind == Kind::Group)
      {
         int d = 1;
         for (int p = c.parent, hops = 0; p != 0 && hops < 64; hops++)
         {
            const Layer* up = nullptr;
            for (const Layer& o : clip)
               if (o.id == p)
                  up = &o;
            if (up == nullptr)
               break;
            d++;
            p = up->parent;
         }
         pasteLevels = std::max(pasteLevels, d);
      }
   while (parentOfPaste != 0 && Depth(parentOfPaste) + 1 + pasteLevels > kMaxGroupDepth)
   {
      above = parentOfPaste;
      parentOfPaste = FindConst(parentOfPaste)->parent;
      std::fprintf(stderr, "PROBE paste: groups too deep, placed beside group %d\n", above);
   }
   for (Layer& c : copies)
   {
      bool inside = false;
      for (auto& r : remap)
         if (c.parent == r.first)
         {
            c.parent = r.second;
            inside = true;
            break;
         }
      if (!inside)
      {
         c.parent = parentOfPaste;
         c.x += dx;
         c.y += dy;
         tops.push_back(c.id);
      }
   }
   int at = (int)mLayers.size();
   for (int i = 0; i < (int)mLayers.size(); i++)
      if (mLayers[i].id == above || IsInside(mLayers[i].id, above))
         at = i + 1;
   mLayers.insert(mLayers.begin() + at, copies.begin(), copies.end());
   Commit();
   return tops;
}

INode* Scene2DNode::Source(const Layer& l) const
{
   if (l.kind != Kind::Node || l.uid == 0 || !ResolveUid)
      return nullptr;
   INode* node = ResolveUid(l.uid);
   // Bypass walks the same way a cable does.
   for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      node = node->BypassSource();
   return (node != nullptr && !node->bypassed && node != this) ? node : nullptr;
}

INode* Scene2DNode::BrushSource(int slot) const
{
   if (slot < 0 || slot > 2 || mDoc.brushSrc[slot] == 0 || !ResolveUid)
      return nullptr;
   INode* node = ResolveUid(mDoc.brushSrc[slot]);
   for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      node = node->BypassSource();
   return (node != nullptr && !node->bypassed && node != this) ? node : nullptr;
}

INode* Scene2DNode::MaskSource(const Layer& l) const
{
   if (l.maskUid == 0 || !ResolveUid)
      return nullptr;
   INode* node = ResolveUid(l.maskUid);
   for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      node = node->BypassSource();
   return (node != nullptr && !node->bypassed && node != this) ? node : nullptr;
}

bool Scene2DNode::LayerBox(const Layer& l, float& w, float& h)
{
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   if (l.kind == Kind::Paint || l.kind == Kind::Vector)
   {
      w = (float)std::max(1, l.pw);
      h = (float)std::max(1, l.ph);
      return true;
   }
   if (l.kind == Kind::Text)
   {
      int tw = 0, th = 0;
      const bool ok = TextSize(l, tw, th);
      w = (float)std::max(1, tw);
      h = (float)std::max(1, th);
      return ok;
   }
   if (l.kind != Kind::Node)
   {
      w = W;
      h = H;
      return true;
   }
   INode* src = Source(l);
   float sw = src != nullptr ? (float)src->GetOutputWidth() : 0.0f;
   float sh = src != nullptr ? (float)src->GetOutputHeight() : 0.0f;
   if (dynamic_cast<IGeometrySource*>(src) != nullptr)
      sw = sh = (float)std::clamp(std::min(mDoc.w, mDoc.h), 64, 2048);   // a solo render, square

   if (sw <= 0.0f || sh <= 0.0f)
   {
      w = W;
      h = H;
      return false;
   }
   if (l.fit == kFitStretch)
   {
      w = W;
      h = H;
   }
   else if (l.fit == kFitNone)
   {
      w = sw;
      h = sh;
   }
   else
   {
      const float k = l.fit == kFitFit ? std::min(W / sw, H / sh) : std::max(W / sw, H / sh);
      w = sw * k;
      h = sh * k;
   }
   return true;
}

void Scene2DNode::LayerMatrix(const Layer& l, float m[6])
{
   if (l.kind == Kind::Fill || l.kind == Kind::Group || l.kind == Kind::Adjust)
   {
      // Fills, groups and adjustments are the document itself.
      const float id[6] = { 1, 0, 0, 0, 1, 0 };
      std::copy(id, id + 6, m);
      return;
   }
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   const float a = l.angle * 3.14159265f / 180.0f;
   const float c = std::cos(a), s = std::sin(a);
   const float kx = l.sx * (l.flipX ? -1.0f : 1.0f), ky = l.sy * (l.flipY ? -1.0f : 1.0f);
   // R * diag(kx, ky) * [[1, tan(shear)], [0, 1]]: the box leans before it scales and turns.
   const float t = l.is3d ? 0.0f : std::tan(std::clamp(l.shear, -80.0f, 80.0f) * 3.14159265f / 180.0f);
   m[0] = c * kx;
   m[1] = c * kx * t - s * ky;
   m[3] = s * kx;
   m[4] = s * kx * t + c * ky;
   // The anchor point of the box lands on (x, y); a flip turns about the anchor.
   const float px = l.ax * bw, py = l.ay * bh;
   m[2] = l.x - (m[0] * px + m[1] * py);
   m[5] = l.y - (m[3] * px + m[4] * py);
}

bool Scene2DNode::Corners(const Layer& l, float out[8])
{
   // A turned 3D object: its real outline, not the card it used to be.
   if (l.is3d && l.kind == Kind::Node && !l.warpOn && dynamic_cast<IGeometrySource*>(Source(l)) != nullptr && GeometryCorners(l, out))
      return true;
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   float h[9];
   const bool front = LayerHomography(l, h);
   float x0 = 0, y0 = 0, x1 = bw, y1 = bh;
   if (l.cropOn)
   {
      x0 = std::min(bw, l.crop[0]);
      y0 = std::min(bh, l.crop[1]);
      x1 = std::max(x0, bw - l.crop[2]);
      y1 = std::max(y0, bh - l.crop[3]);
   }
   const float pts[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
   for (int i = 0; i < 4; i++)
   {
      // A corner behind the camera is held just in front of it, so handles stay finite.
      const float w = std::max(1e-3f, h[6] * pts[i * 2] + h[7] * pts[i * 2 + 1] + h[8]);
      out[i * 2] = (h[0] * pts[i * 2] + h[1] * pts[i * 2 + 1] + h[2]) / w;
      out[i * 2 + 1] = (h[3] * pts[i * 2] + h[4] * pts[i * 2 + 1] + h[5]) / w;
   }
   return front;
}

bool Scene2DNode::Bounds(int id, float lo[2], float hi[2])
{
   const Layer* l = FindLayer(id);
   if (l == nullptr)
      return false;
   lo[0] = lo[1] = 1e30f;
   hi[0] = hi[1] = -1e30f;
   bool any = false;
   auto add = [&](const Layer& one)
   {
      float c[8];
      Corners(one, c);
      for (int i = 0; i < 4; i++)
      {
         lo[0] = std::min(lo[0], c[i * 2]);
         lo[1] = std::min(lo[1], c[i * 2 + 1]);
         hi[0] = std::max(hi[0], c[i * 2]);
         hi[1] = std::max(hi[1], c[i * 2 + 1]);
      }
      any = true;
   };
   if (l->kind == Kind::Group)
   {
      for (const Layer& one : mLayers)
         if (one.kind != Kind::Group && one.id != id && IsInside(one.id, id))
            add(one);
   }
   else
      add(*l);
   return any;
}

bool Scene2DNode::Shown(const Layer& l)
{
   if (soloLayer != 0 && FindConst(soloLayer) != nullptr && !IsInside(l.id, soloLayer) && !IsInside(soloLayer, l.id))
      return false;
   const Layer* p = &l;
   for (int hops = 0; p != nullptr && hops < 64; hops++)
   {
      if (!p->visible || (sExport && p->noExport))
         return false;
      p = p->parent != 0 ? FindConst(p->parent) : nullptr;
   }
   return true;
}

// ---- document edits -----------------------------------------------------

namespace
{
   // A Fit / Fill / Stretch layer is sized off the document, so before the document
   // changes size each one is turned into the plain scale it shows at now.
   void BakeFit(Scene2DNode& doc, Scene2DNode::Layer& l)
   {
      if (l.kind != Scene2DNode::Kind::Node || l.fit == Scene2DNode::kFitNone)
         return;
      INode* src = doc.Source(l);
      if (src == nullptr || src->GetOutputWidth() <= 0 || src->GetOutputHeight() <= 0)
         return;
      float bw = 0, bh = 0;
      doc.LayerBox(l, bw, bh);
      const float kx = bw / (float)src->GetOutputWidth(), ky = bh / (float)src->GetOutputHeight();
      l.fit = Scene2DNode::kFitNone;
      l.sx *= kx;
      l.sy *= ky;
      for (int i = 0; i < 4; i++)
         l.crop[i] /= (i % 2 == 0) ? kx : ky;
   }
}

void Scene2DNode::CanvasSize(int w, int h, int anchor)
{
   Layers();
   w = std::clamp(w, 1, GLUtil::kMaxTargetSide);
   h = std::clamp(h, 1, GLUtil::kMaxTargetSide);
   const float dx = (float)(w - mDoc.w) * (float)(anchor % 3) * 0.5f;
   const float dy = (float)(h - mDoc.h) * (float)(anchor / 3) * 0.5f;
   for (Layer& l : mLayers)
   {
      BakeFit(*this, l);
      l.x += dx;
      l.y += dy;
   }
   for (float& g : mDoc.guidesX) g += dx;
   for (float& g : mDoc.guidesY) g += dy;
   ReframeSelection(0, 0, 0, 0);   // a new canvas drops the selection (Photoshop, PhotoCraft)
   mDoc.w = w;
   mDoc.h = h;
   StoreSelection();   // commits
}

void Scene2DNode::ImageSize(int w, int h)
{
   Layers();
   w = std::clamp(w, 1, GLUtil::kMaxTargetSide);
   h = std::clamp(h, 1, GLUtil::kMaxTargetSide);
   const float kx = (float)w / (float)mDoc.w, ky = (float)h / (float)mDoc.h;
   for (Layer& l : mLayers)
   {
      BakeFit(*this, l);
      l.x *= kx;
      l.y *= ky;
      l.sx *= kx;
      l.sy *= ky;
   }
   for (float& g : mDoc.guidesX) g *= kx;
   for (float& g : mDoc.guidesY) g *= ky;
   ReframeSelection(0, 0, 0, 0);   // resampling drops the selection (Photoshop, PhotoCraft)
   mDoc.w = w;
   mDoc.h = h;
   StoreSelection();   // commits
}

void Scene2DNode::ImageSizeDpi(int w, int h, float dpi, bool keepPrintSize, bool nearest)
{
   Layers();
   if (nearest)
      for (Layer& l : mLayers)
         if (l.kind == Kind::Paint)
            l.nearest = true;
   const float was = mDoc.dpi > 0 ? mDoc.dpi : 72.0f, now = dpi > 0 ? dpi : was;
   if (w <= 0 && h <= 0 && keepPrintSize)
   {
      w = (int)std::lround(mDoc.w * now / was);
      h = (int)std::lround(mDoc.h * now / was);
   }
   else if (w > 0 && h <= 0)
      h = (int)std::lround((double)w * mDoc.h / mDoc.w);
   else if (h > 0 && w <= 0)
      w = (int)std::lround((double)h * mDoc.w / mDoc.h);
   mDoc.dpi = now;
   if (w > 0 && h > 0 && (w != mDoc.w || h != mDoc.h))
      ImageSize(w, h);   // commits
   else
      Commit();
}

void Scene2DNode::CropToTarget(float x0, float y0, float x1, float y1, float deg, int tw, int th, float dpi)
{
   if (deg != 0.0f)
      CropRotated((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, std::fabs(x1 - x0), std::fabs(y1 - y0), deg);
   else
      CropTo(x0, y0, x1, y1);
   if (dpi > 0)
      mDoc.dpi = dpi;
   if (tw <= 0 && th <= 0)
   {
      Commit();
      return;
   }
   if (tw > 0 && th <= 0)
      th = (int)std::lround((double)tw * mDoc.h / mDoc.w);
   else if (th > 0 && tw <= 0)
      tw = (int)std::lround((double)th * mDoc.w / mDoc.h);
   tw = std::clamp(tw, 1, GLUtil::kMaxTargetSide);
   th = std::clamp(th, 1, GLUtil::kMaxTargetSide);
   // The crop tool's one step (Photoshop's Delete Cropped Pixels, on by default there): a plain paint layer's canvas
   // part is resampled on its own, Bicubic with its edges held, so a fill reaching the frame fills the result to its
   // corners. Layers that are turned, scaled, warped or masked keep the transform route below.
   struct Baked { int id; float x, y; };
   std::vector<Baked> baked;
   const int DW = mDoc.w, DH = mDoc.h, kT = TileStore::kTile;
   Layers();
   SyncSurfaces();
   for (Layer& l : mLayers)
   {
      if (l.kind != Kind::Paint || l.pw <= 0 || l.ph <= 0 || l.sx != 1.0f || l.sy != 1.0f || l.angle != 0.0f || l.shear != 0.0f ||
          l.flipX || l.flipY || l.warpOn || l.is3d || l.HasPixelMask() || l.fmw > 0)
         continue;
      float bw = 1, bh = 1;
      LayerBox(l, bw, bh);
      const float fx = l.x - l.ax * bw, fy = l.y - l.ay * bh;
      const int ox = (int)std::lround(fx), oy = (int)std::lround(fy);
      if (bw != (float)l.pw || bh != (float)l.ph || std::fabs(fx - ox) > 1e-3f || std::fabs(fy - oy) > 1e-3f)
         continue;
      std::vector<uint8_t> src;
      int sw = 0, sh = 0;
      if (!ReadPaint(l.id, src, sw, sh) || sw != l.pw || sh != l.ph)
         continue;
      // The canvas part, rows top first; what lies off the surface is clear.
      std::vector<uint8_t> region((size_t)DW * DH * 4, 0);
      for (int y = 0; y < DH; y++)
      {
         const int sy = y - oy;
         if (sy < 0 || sy >= sh)
            continue;
         const int srow = sh - 1 - sy;   // surface rows bottom first
         for (int x = 0; x < DW; x++)
         {
            const int sx = x - ox;
            if (sx >= 0 && sx < sw)
               std::memcpy(&region[((size_t)y * DW + x) * 4], &src[((size_t)srow * sw + sx) * 4], 4);
         }
      }
      const std::vector<uint8_t> up = ResampleRgba(region, DW, DH, tw, th, l.nearest ? 0 : 2);
      const int cols = (tw + kT - 1) / kT, rows = (th + kT - 1) / kT;
      std::vector<std::string> keys((size_t)cols * rows, "-");
      std::vector<uint8_t> tile;
      for (size_t i = 0; i < keys.size(); i++)
      {
         const int tx0 = (int)(i % cols) * kT, ty0 = (int)(i / cols) * kT;
         const int tww = std::min(kT, tw - tx0), thh = std::min(kT, th - ty0);
         tile.assign((size_t)tww * thh * 4, 0);
         bool any = false;
         for (int y = 0; y < thh; y++)
         {
            const int row = th - 1 - (ty0 + y);   // tile rows bottom first, from the top-first result
            std::memcpy(&tile[(size_t)y * tww * 4], &up[((size_t)row * tw + tx0) * 4], (size_t)tww * 4);
            for (int x = 0; x < tww && !any; x++)
               any = tile[((size_t)y * tww + x) * 4 + 3] != 0;
         }
         if (any)
            keys[i] = TileStore::Put(tile.data(), tww, thh);
      }
      l.pw = tw;
      l.ph = th;
      l.tiles = std::move(keys);
      baked.push_back({ l.id, l.ax * (float)tw, l.ay * (float)th });
   }
   if (tw != mDoc.w || th != mDoc.h)
      ImageSize(tw, th);   // commits; it scales every layer, so the baked ones are put back after
   for (const Baked& b : baked)
      if (Layer* l = FindLayer(b.id))
      {
         l->x = b.x;
         l->y = b.y;
         l->sx = l->sy = 1.0f;
      }
   Commit();
}

void Scene2DNode::CropTo(float x0, float y0, float x1, float y1, bool deletePixels)
{
   Layers();
   if (x1 < x0) std::swap(x0, x1);
   if (y1 < y0) std::swap(y0, y1);
   const int w = std::clamp((int)std::lround(x1 - x0), 1, GLUtil::kMaxTargetSide);
   const int h = std::clamp((int)std::lround(y1 - y0), 1, GLUtil::kMaxTargetSide);
   for (Layer& l : mLayers)
   {
      BakeFit(*this, l);
      l.x -= x0;
      l.y -= y0;
   }
   for (float& g : mDoc.guidesX) g -= x0;
   for (float& g : mDoc.guidesY) g -= y0;
   ReframeSelection(0, 0, 0, 0);   // cropping drops the selection (Photoshop, PhotoCraft)
   mDoc.w = w;
   mDoc.h = h;
   if (deletePixels)
      ClearPaintOffCanvas();
   StoreSelection();   // commits
}

void Scene2DNode::CropRotated(float cx, float cy, float w, float h, float deg, bool deletePixels)
{
   Layers();
   const float turn = -deg;
   if (std::fabs(std::remainder(turn, 360.0f)) > 1e-4f)
   {
      const float r = turn * 3.14159265358979f / 180.0f, cs = std::cos(r), sn = std::sin(r);
      for (Layer& l : mLayers)
      {
         // Fills, groups and adjustments are the document itself: nothing to turn.
         if (l.kind == Kind::Fill || l.kind == Kind::Group || l.kind == Kind::Adjust)
            continue;
         BakeFit(*this, l);
         // The layer's box centre turns about (cx, cy), and the layer turns with it.
         float bw = 1, bh = 1, m0[6];
         LayerBox(l, bw, bh);
         LayerMatrix(l, m0);
         const float px = m0[0] * bw * 0.5f + m0[1] * bh * 0.5f + m0[2], py = m0[3] * bw * 0.5f + m0[4] * bh * 0.5f + m0[5];
         const float qx = cx + (px - cx) * cs - (py - cy) * sn, qy = cy + (px - cx) * sn + (py - cy) * cs;
         l.angle = std::remainder(l.angle + turn, 360.0f);
         float m1[6];
         LayerMatrix(l, m1);
         l.x += qx - (m1[0] * bw * 0.5f + m1[1] * bh * 0.5f + m1[2]);
         l.y += qy - (m1[3] * bw * 0.5f + m1[4] * bh * 0.5f + m1[5]);
         if (l.warpOn)
            for (int k = 0; k < 4; k++)
            {
               // Pulled corners are document offsets: they turn with the layer.
               const float wx = l.warp[k * 2], wy = l.warp[k * 2 + 1];
               l.warp[k * 2] = wx * cs - wy * sn;
               l.warp[k * 2 + 1] = wx * sn + wy * cs;
            }
      }
      // Guides do not turn with the picture: they go, as a turned crop leaves none in place.
      mDoc.guidesX.clear();
      mDoc.guidesY.clear();
   }
   CropTo(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, deletePixels);
}

void Scene2DNode::RotateCanvas(float degCw)
{
   Layers();
   const float d = std::remainder(degCw, 360.0f);
   if (std::fabs(d) < 1e-4f)
      return;
   const float r = d * 3.14159265358979f / 180.0f;
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   const float nw = std::fabs(W * std::cos(r)) + std::fabs(H * std::sin(r));
   const float nh = std::fabs(W * std::sin(r)) + std::fabs(H * std::cos(r));
   // Right angles carry the guides: (x, y) about the centre turned clockwise on screen (y down).
   const std::vector<float> gx = mDoc.guidesX, gy = mDoc.guidesY;
   const int quarter = (int)std::lround(d / 90.0f);
   const bool right = std::fabs(d - quarter * 90.0f) < 1e-3f;
   CropRotated(W * 0.5f, H * 0.5f, std::round(nw), std::round(nh), -d);
   if (!right)
      return;
   std::vector<float> ox, oy;
   switch ((quarter % 4 + 4) % 4)
   {
      case 1:   // 90 clockwise: x' = H - y, y' = x
         for (float g : gx) oy.push_back(g);
         for (float g : gy) ox.push_back(H - g);
         break;
      case 2:   // 180
         for (float g : gx) ox.push_back(W - g);
         for (float g : gy) oy.push_back(H - g);
         break;
      case 3:   // 90 counter clockwise: x' = y, y' = W - x
         for (float g : gx) oy.push_back(W - g);
         for (float g : gy) ox.push_back(g);
         break;
      default: ox = gx; oy = gy; break;
   }
   mDoc.guidesX = ox;
   mDoc.guidesY = oy;
   Commit();
}

void Scene2DNode::FlipCanvas(bool vertical)
{
   Layers();
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   for (Layer& l : mLayers)
   {
      if (l.kind == Kind::Fill || l.kind == Kind::Group || l.kind == Kind::Adjust)
         continue;
      BakeFit(*this, l);
      float bw = 1, bh = 1, m[6];
      LayerBox(l, bw, bh);
      LayerMatrix(l, m);
      const float cx = m[0] * bw * 0.5f + m[1] * bh * 0.5f + m[2], cy = m[3] * bw * 0.5f + m[4] * bh * 0.5f + m[5];
      if (vertical)
         l.flipY = !l.flipY;
      else
         l.flipX = !l.flipX;
      l.angle = l.angle == 0.0f ? 0.0f : -l.angle;
      if (l.warpOn)
         for (int k = 0; k < 4; k++)
            l.warp[k * 2 + (vertical ? 1 : 0)] = -l.warp[k * 2 + (vertical ? 1 : 0)];
      float m1[6];
      LayerMatrix(l, m1);
      const float nx = m1[0] * bw * 0.5f + m1[1] * bh * 0.5f + m1[2], ny = m1[3] * bw * 0.5f + m1[4] * bh * 0.5f + m1[5];
      l.x += (vertical ? cx : W - cx) - nx;
      l.y += (vertical ? H - cy : cy) - ny;
   }
   for (float& g : vertical ? mDoc.guidesY : mDoc.guidesX)
      g = (vertical ? H : W) - g;
   ReframeSelection(0, 0, 0, 0);   // the outline no longer matches the picture: it goes, as after a crop
   StoreSelection();               // commits
}

bool Scene2DNode::RevealAll()
{
   Layers();
   float lo[2] = { 0.0f, 0.0f }, hi[2] = { (float)mDoc.w, (float)mDoc.h };
   for (const Layer& l : mLayers)
   {
      if (l.kind == Kind::Fill || l.kind == Kind::Group || l.kind == Kind::Adjust || !Shown(l))
         continue;
      float a[2], b[2];
      if (!Bounds(l.id, a, b))
         continue;
      lo[0] = std::min(lo[0], std::floor(a[0]));
      lo[1] = std::min(lo[1], std::floor(a[1]));
      hi[0] = std::max(hi[0], std::ceil(b[0]));
      hi[1] = std::max(hi[1], std::ceil(b[1]));
   }
   if (lo[0] >= 0.0f && lo[1] >= 0.0f && hi[0] <= (float)mDoc.w && hi[1] <= (float)mDoc.h)
      return false;
   CropTo(lo[0], lo[1], hi[0], hi[1], false);
   return true;
}

namespace
{
   // Row-major 3x3 homographies in double: the quad maths needs more than float near a steep horizon.
   using H3 = std::array<double, 9>;

   // The unit square onto quad q (TL TR BR BL), Heckbert's closed form. False when degenerate.
   bool UnitToQuad(const double q[8], H3& m)
   {
      const double x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], x2 = q[4], y2 = q[5], x3 = q[6], y3 = q[7];
      const double sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3;
      double g = 0, k = 0;
      if (std::fabs(sx) > 1e-12 || std::fabs(sy) > 1e-12)
      {
         const double dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
         const double den = dx1 * dy2 - dx2 * dy1;
         if (std::fabs(den) < 1e-12)
            return false;
         g = (sx * dy2 - dx2 * sy) / den;
         k = (dx1 * sy - sx * dy1) / den;
      }
      m = { x1 - x0 + g * x1, x3 - x0 + k * x3, x0, y1 - y0 + g * y1, y3 - y0 + k * y3, y0, g, k, 1.0 };
      return true;
   }

   bool Invert3(const H3& m, H3& o)
   {
      const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
      const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
      const double det = a * A + b * B + c * C;
      if (!std::isfinite(det) || std::fabs(det) < 1e-18)
         return false;
      o = { A / det, -(b * i - c * h) / det, (b * f - c * e) / det, B / det, (a * i - c * g) / det, -(a * f - c * d) / det,
            C / det, -(a * h - b * g) / det, (a * e - b * d) / det };
      return true;
   }

   // Every turn the same way, and not a straight angle (PhotoCraft convex_quad, either winding).
   bool ConvexQuad(const double q[8])
   {
      int sign = 0;
      double area2 = 0;
      for (int i = 0; i < 4; i++)
      {
         const double* a = q + i * 2;
         const double* b = q + (i + 1) % 4 * 2;
         const double* c = q + (i + 2) % 4 * 2;
         const double e1x = b[0] - a[0], e1y = b[1] - a[1], e2x = c[0] - b[0], e2y = c[1] - b[1];
         const double cross = e1x * e2y - e1y * e2x;
         if (!std::isfinite(cross) || std::fabs(cross) <= 1e-3 * std::hypot(e1x, e1y) * std::hypot(e2x, e2y))
            return false;
         const int s = cross > 0 ? 1 : -1;
         if (sign != 0 && s != sign)
            return false;
         sign = s;
         area2 += a[0] * b[1] - b[0] * a[1];
      }
      return std::fabs(area2) >= 2.0;
   }

   // The width and aspect ratio of the rectangle q shows, the principal point at (cx, cy) (Zhang and He,
   // "Whiteboard scanning and image enhancement", 2007, as PhotoCraft's perspective_estimate). False when two
   // sides are too near parallel to fix the focal length.
   bool PerspectiveEstimate(const double q[8], double cx, double cy, double& width, double& aspect)
   {
      H3 m;
      if (!UnitToQuad(q, m))
         return false;
      const double a = m[0], b = m[1], d = m[3], e = m[4], g = m[6], h = m[7];
      constexpr double kMinForeshortening = 0.005;
      if (std::min(std::fabs(g), std::fabs(h)) < kMinForeshortening)
         return false;
      const double ux = a - g * cx, uy = d - g * cy, vx = b - h * cx, vy = e - h * cy;
      const double focal2 = -(ux * vx + uy * vy) / (g * h);
      if (!std::isfinite(focal2) || focal2 <= 0)
         return false;
      const double focal = std::sqrt(focal2);
      aspect = std::hypot(std::hypot(ux, uy), g * focal) / std::hypot(std::hypot(vx, vy), h * focal);
      // The widest horizontal scale at a corner, so the nearest part keeps its detail.
      const double st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
      width = 0;
      for (int i = 0; i < 4; i++)
      {
         const double x = q[i * 2], y = q[i * 2 + 1];
         width = std::max(width, std::hypot(a - x * g, d - y * g) / (1.0 + g * st[i][0] + h * st[i][1]));
      }
      return std::isfinite(width) && std::isfinite(aspect) && aspect > 0;
   }

   // The estimate, or false when a pixel of click error on any corner swings its proportions over 10 %.
   bool PerspectiveSize(const double q[8], double cx, double cy, double& w, double& h)
   {
      double width = 0, aspect = 1;
      if (!PerspectiveEstimate(q, cx, cy, width, aspect))
         return false;
      constexpr double kMaxAspectSwing = 0.1;
      for (int c = 0; c < 8; c++)
         for (double step : { -1.0, 1.0 })
         {
            double n[8];
            std::copy(q, q + 8, n);
            n[c] += step;
            double w2 = 0, a2 = 1;
            if (!PerspectiveEstimate(n, cx, cy, w2, a2) || std::fabs(a2 / aspect - 1.0) > kMaxAspectSwing)
               return false;
         }
      w = width;
      h = width / aspect;
      return true;
   }
}

bool Scene2DNode::PerspectiveCropSize(const float qf[8], float cx, float cy, int w, int h, int& outW, int& outH)
{
   double q[8];
   std::copy(qf, qf + 8, q);
   if (!ConvexQuad(q))
      return false;
   const auto len = [&](int i, int j) { return std::hypot(q[j * 2] - q[i * 2], q[j * 2 + 1] - q[i * 2 + 1]); };
   double fw = 0, fh = 0;
   if (w > 0 && h > 0)
   {
      fw = w;
      fh = h;
   }
   else if (w > 0 || h > 0)
   {
      // One side given: the other keeps the mean top/bottom by mean left/right proportions.
      const double meanW = (len(0, 1) + len(3, 2)) * 0.5, meanH = (len(0, 3) + len(1, 2)) * 0.5;
      if (meanW <= 0 || meanH <= 0)
         return false;
      fw = w > 0 ? w : h * meanW / meanH;
      fh = h > 0 ? h : w * meanH / meanW;
   }
   else
   {
      // Free: the rectangle shown in perspective when that is recoverable and plausible, else the longest edges.
      const double edgeW = std::max(len(0, 1), len(3, 2)), edgeH = std::max(len(0, 3), len(1, 2));
      constexpr double kMaxEstimateRatio = 4.0;
      const auto plausible = [&](double v, double edge) { return std::isfinite(v) && v >= edge / kMaxEstimateRatio && v <= edge * kMaxEstimateRatio; };
      double pw = 0, ph = 0;
      if (PerspectiveSize(q, cx, cy, pw, ph) && plausible(pw, edgeW) && plausible(ph, edgeH))
      {
         fw = pw;
         fh = ph;
      }
      else
      {
         fw = edgeW;
         fh = edgeH;
      }
   }
   outW = std::clamp((int)std::lround(fw), 1, GLUtil::kMaxTargetSide);
   outH = std::clamp((int)std::lround(fh), 1, GLUtil::kMaxTargetSide);
   return true;
}

bool Scene2DNode::PerspectiveCrop(const float qf[8], int w, int h)
{
   Layers();
   int W = 0, Hh = 0;
   if (!PerspectiveCropSize(qf, mDoc.w * 0.5f, mDoc.h * 0.5f, w, h, W, Hh))
      return false;
   // Document to output: the inverse of the output rectangle onto the quad.
   double q[8];
   std::copy(qf, qf + 8, q);
   H3 sq, toDoc, toOut;
   if (!UnitToQuad(q, sq))
      return false;
   toDoc = sq;
   for (int r = 0; r < 3; r++)
   {
      toDoc[r * 3] /= W;
      toDoc[r * 3 + 1] /= Hh;
   }
   if (!Invert3(toDoc, toOut))
      return false;
   // Points inside the quad come out with a positive w.
   const double qcx = (q[0] + q[2] + q[4] + q[6]) * 0.25, qcy = (q[1] + q[3] + q[5] + q[7]) * 0.25;
   if (toOut[6] * qcx + toOut[7] * qcy + toOut[8] < 0)
      for (double& v : toOut)
         v = -v;
   const auto map = [&](double x, double y, float& ox, float& oy) {
      const double z = toOut[6] * x + toOut[7] * y + toOut[8];
      if (!(z > 1e-9))
         return false;
      ox = (float)((toOut[0] * x + toOut[1] * y + toOut[2]) / z);
      oy = (float)((toOut[3] * x + toOut[4] * y + toOut[5]) / z);
      return std::isfinite(ox) && std::isfinite(oy);
   };
   // Every layer's new corner warp first, so a layer past the horizon (or a 3D card, whose map moves with the
   // document size) leaves the document untouched.
   struct Plan { int index; float warp[8]; };
   struct Bake { int id; std::vector<uint8_t> px; };
   std::vector<Plan> plans;
   std::vector<Bake> bakes;
   for (int i = 0; i < (int)mLayers.size(); i++)
   {
      Layer& l = mLayers[i];
      if (l.kind == Kind::Fill || l.kind == Kind::Group || l.kind == Kind::Adjust)
         continue;   // the document itself: they fill the new size as they are
      if (l.is3d)
         return false;
      BakeFit(*this, l);
      float bw = 1, bh = 1, hNow[9], m[6];
      LayerBox(l, bw, bh);
      if (!LayerHomography(l, hNow))
         return false;
      LayerMatrix(l, m);
      Plan p{ i, {} };
      bool past = false;
      const float box[8] = { 0, 0, bw, 0, bw, bh, 0, bh };
      for (int k = 0; k < 4 && !past; k++)
      {
         const float bx = box[k * 2], by = box[k * 2 + 1];
         const float z = hNow[6] * bx + hNow[7] * by + hNow[8];
         if (!(z > 1e-6f))
            return false;
         const double dx = (hNow[0] * bx + hNow[1] * by + hNow[2]) / z, dy = (hNow[3] * bx + hNow[4] * by + hNow[5]) / z;
         float ox = 0, oy = 0;
         if (!map(dx, dy, ox, oy))
         {
            past = true;   // this corner lies beyond the quad's horizon: no corner warp can carry the layer
            break;
         }
         // The plain (unwarped) corner, and the pull that puts it where the map sends it.
         p.warp[k * 2] = ox - (m[0] * bx + m[1] * by + m[2]);
         p.warp[k * 2 + 1] = oy - (m[3] * bx + m[4] * by + m[5]);
      }
      if (!past)
      {
         plans.push_back(p);
         continue;
      }
      // PhotoCraft #7557: a layer reaching past the horizon is resampled through the quad (bilinear, premultiplied)
      // into a paint layer of the new size, as Photoshop resamples, instead of refusing the crop.
      const int bakeId = l.id;   // RenderLayerAlone puts the layer list back: l may not survive it
      std::vector<uint8_t> pic;
      if (!RenderLayerAlone(bakeId, 0, pic) || pic.size() != (size_t)mDoc.w * mDoc.h * 4)
         return false;
      Bake b{ bakeId, std::vector<uint8_t>((size_t)W * Hh * 4, 0) };
      const int DW = mDoc.w, DH = mDoc.h;
      for (int oy = 0; oy < Hh; oy++)
         for (int ox = 0; ox < W; ox++)
         {
            const double u = ox + 0.5, v = oy + 0.5, z = toDoc[6] * u + toDoc[7] * v + toDoc[8];
            if (!(std::fabs(z) > 1e-12))
               continue;
            const double sx = (toDoc[0] * u + toDoc[1] * v + toDoc[2]) / z - 0.5, sy = (toDoc[3] * u + toDoc[4] * v + toDoc[5]) / z - 0.5;
            if (!(sx > -1.0 && sy > -1.0 && sx < DW && sy < DH))
               continue;
            const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
            const double fx = sx - x0, fy = sy - y0;
            double acc[4] = { 0, 0, 0, 0 };
            for (int t = 0; t < 4; t++)
            {
               const int xx = x0 + (t & 1), yy = y0 + (t >> 1);
               if (xx < 0 || yy < 0 || xx >= DW || yy >= DH)
                  continue;
               const double wgt = ((t & 1) ? fx : 1.0 - fx) * ((t >> 1) ? fy : 1.0 - fy);
               const uint8_t* s = &pic[((size_t)yy * DW + xx) * 4];
               const double a = s[3] / 255.0;
               for (int c = 0; c < 3; c++)
                  acc[c] += wgt * s[c] * a;
               acc[3] += wgt * a;
            }
            if (acc[3] <= 0.0)
               continue;
            uint8_t* d = &b.px[((size_t)oy * W + ox) * 4];
            for (int c = 0; c < 3; c++)
               d[c] = (uint8_t)std::clamp((int)std::lround(acc[c] / acc[3]), 0, 255);
            d[3] = (uint8_t)std::clamp((int)std::lround(acc[3] * 255.0), 0, 255);
         }
      bakes.push_back(std::move(b));
   }
   for (const Plan& p : plans)
   {
      Layer& l = mLayers[p.index];
      l.warpOn = true;
      std::copy(p.warp, p.warp + 8, l.warp);
   }
   for (Bake& b : bakes)
   {
      const Layer* old = FindConst(b.id);
      if (old == nullptr)
         continue;
      const std::string name = old->name;
      const float opacity = old->opacity;
      const int blend = old->blend;
      const bool visible = old->visible, clip = old->clip;
      const int nid = AddFileLayer(old->parent, name, 0, 0, W, Hh, b.px, b.id);
      if (Layer* nl = FindLayer(nid))
      {
         nl->opacity = opacity;
         nl->blend = blend;
         nl->visible = visible;
         nl->clip = clip;
      }
      RemoveLayer(b.id);
      std::fprintf(stderr, "PROBE perspective crop resampled layer %d past the horizon into %d\n", b.id, nid);
   }
   mDoc.guidesX.clear();
   mDoc.guidesY.clear();
   CropTo(0, 0, (float)W, (float)Hh);
   return true;
}

// ---- saved text ---------------------------------------------------------

void Scene2DNode::Commit()
{
   if (mText != mParsedText)
      Parse();   // never write unparsed state over newer text (a node Undo just rebuilt holds an empty list)
   Serialize();
}

// One line per fact, like the 3D View, so a patch diff reads and an agent can write it by hand.
void Scene2DNode::Serialize()
{
   for (Layer& l : mLayers)
      if (l.kind == Kind::Text)
         SyncTextRuns(l);   // fold summary-field and text edits into the runs before they are written
   std::ostringstream o;
   const Document& d = mDoc;
   o << "next " << mNextId << "\n";
   if (panelOnly)
      o << "panel 1\n";
   o << "doc " << d.w << " " << d.h << " " << d.background << " " << d.bgColor[0] << " " << d.bgColor[1] << " "
     << d.bgColor[2] << " " << +d.ratioLock << " " << +d.rulers << " " << +d.grid << " " << +d.snap << " "
     << +d.guidesOn << " " << d.gridSize << " " << d.gridSubdiv << " " << d.fps << " " << d.seconds << " "
     << +d.linear << " " << +d.headroom << "\n";
   if (d.dpi != 72.0f)
      o << "dpi " << d.dpi << "\n";
   if (d.guidesLocked)
      o << "guideslock 1\n";
   if (!d.guidesX.empty())
   {
      o << "guidesx";
      for (float g : d.guidesX) o << " " << g;
      o << "\n";
   }
   if (!d.guidesY.empty())
   {
      o << "guidesy";
      for (float g : d.guidesY) o << " " << g;
      o << "\n";
   }
   if (d.camUid != 0)
      o << "cam " << d.camUid << "\n";
   if (d.symmetry != 0 || d.symN != 6)
      o << "sym " << d.symmetry << " " << d.symN << "\n";
   for (int k = 0; k < 3; k++)
      if (d.brushSrc[k] != 0)
         o << "brushsrc " << k << " " << d.brushSrc[k] << "\n";
   if (d.brushLive)
      o << "brushlive 1\n";
   for (const ModSlot& m : d.slots)
      o << "mslot " << m.layer << " " << m.field << " " << m.slot << "\n";
   // Named type styles: "cstyle id name attrs", "pstyle id name para chr", "pbasic para chr" (attrs as k=v;k=v).
   if (!d.textStyles.basic.para.empty() || !d.textStyles.basic.chr.empty())
      o << "pbasic " << EscapeToken(AttrsText(d.textStyles.basic.para)) << " " << EscapeToken(AttrsText(d.textStyles.basic.chr)) << "\n";
   for (const CharStyleDef& c : d.textStyles.character)
      o << "cstyle " << c.id << " " << EscapeToken(c.name) << " " << EscapeToken(AttrsText(c.attrs)) << "\n";
   for (const ParaStyleDef& p : d.textStyles.paragraph)
      o << "pstyle " << p.id << " " << EscapeToken(p.name) << " " << EscapeToken(AttrsText(p.para)) << " "
        << EscapeToken(AttrsText(p.chr)) << "\n";
   if (quickMask)
      o << "qmask 1\n";
   auto selLine = [&](const char* key, const SelStore& st)
   {
      o << key;
      if (!st.name.empty())
         o << " " << EscapeToken(st.name);
      o << " " << st.w << " " << st.h;
      for (const std::string& k : st.keys) o << " " << k;
      o << "\n";
   };
   if (mSelStore.w == d.w && mSelStore.h == d.h && !mSelStore.keys.empty())
      selLine("sel", mSelStore);
   if (!mSelApronStore.keys.empty())
   {
      const ApronStore& as = mSelApronStore;
      o << "selapron " << as.x0 << " " << as.y0 << " " << as.w << " " << as.h;
      for (const std::string& k : as.keys) o << " " << k;
      o << "\n";
   }
   for (const SelStore& st : mSelSaved)
      selLine("selsave", st);
   // Paths: one line per item, "dpath name op closed n x y inX inY outX outY ..."
   for (const DocPath& p : mPaths)
      for (const Shape& sh : p.subs)
      {
         o << "dpath " << EscapeToken(p.name) << " " << sh.op << " " << +sh.closed << " " << sh.pts.size() / 6;
         for (float v : sh.pts) o << " " << v;
         o << "\n";
      }
   if (!mClipPath.empty())
      o << "clippath " << EscapeToken(mClipPath) << "\n";
   for (const Layer& l : mLayers)
   {
      o << "layer " << l.id << " " << (int)l.kind << " " << l.parent << " " << l.uid << " " << +l.visible << " "
        << l.blend << " " << l.opacity << " " << l.fill << " " << l.lock << " " << l.label << " " << +l.clip << " "
        << +l.open << "\n";
      if (l.link != 0)
         o << "link " << l.id << " " << l.link << "\n";
      o << "xf " << l.id << " " << l.x << " " << l.y << " " << l.sx << " " << l.sy << " " << l.angle << " " << l.ax
        << " " << l.ay << " " << +l.flipX << " " << +l.flipY << " " << l.fit << " " << +l.nearest << "\n";
      if (l.shear != 0.0f || l.blendGamma != 2.2f || l.aaMode != 0 || l.noExport)
         o << "lx " << l.id << " " << l.shear << " " << l.blendGamma << " " << l.aaMode << " " << +l.noExport << "\n";
      if (l.typeAA != 0)
         o << "taa " << l.id << " " << l.typeAA << "\n";
      if (!l.name.empty())
         o << "name " << l.id << " " << l.name << "\n";
      if (l.cropOn || l.crop[0] != 0 || l.crop[1] != 0 || l.crop[2] != 0 || l.crop[3] != 0)
         o << "crop " << l.id << " " << +l.cropOn << " " << l.crop[0] << " " << l.crop[1] << " " << l.crop[2] << " "
           << l.crop[3] << "\n";
      if (l.maskUid != 0)
         o << "mask " << l.id << " " << l.maskUid << " " << +l.maskOn << " " << +l.maskInvert << " " << l.maskChannel
           << " " << l.maskDensity << "\n";
      if (l.kind == Kind::Adjust)
         o << "adj " << l.id << " " << l.adjType << " " << l.adjAmt << " " << l.adjTo << " " << l.adjDrive << " " << +l.adjMap
           << "\n";
      if (l.kind == Kind::Adjust && l.adjDither)
         o << "gmdither " << l.id << " 1\n";
      if (l.kind == Kind::Adjust && l.adjType == kAdjLevels)
         o << "levels " << l.id << " " << l.levels[0] << " " << l.levels[1] << " " << l.levels[2] << " " << l.levels[3] << "\n";
      if (l.kind == Kind::Adjust && l.adjType == kAdjCurves)
      {
         o << "curve " << l.id << " " << l.curve.size() / 2;
         for (float v : l.curve) o << " " << v;
         o << "\n";
      }
      if (l.kind == Kind::Fill || l.kind == Kind::Adjust)
      {
         o << "fillc " << l.id << " " << l.fillType;
         for (float v : l.colorA) o << " " << v;
         for (float v : l.colorB) o << " " << v;
         o << " " << l.fillAngle << "\n";
         if (!l.stops.empty())
         {
            o << "stops " << l.id;
            WriteStops(o, l.stops);
            o << "\n";
         }
         if (l.kind == Kind::Fill && (l.fillNoise > 0.0f || !l.fillImage.empty()))
            o << "fillx " << l.id << " " << l.fillNoise << " " << EscapeToken(l.fillImage) << "\n";
      }
      if (l.kind == Kind::Paint && l.relief)
         o << "relief " << l.id << " " << l.reliefMat << " " << l.reliefLight << " " << l.reliefElev << " " << l.reliefDepth
           << "\n";
      if (l.kind == Kind::Paint || l.HasSkin())
      {
         o << "paint " << l.id << " " << l.pw << " " << l.ph;
         for (const std::string& k : l.tiles) o << " " << k;
         o << "\n";
      }
      WriteEffectLines(o, l, l.id);
      auto shapeLine = [&](const char* key, const Shape& sh)
      {
         o << key << " " << l.id << " " << sh.type << " " << sh.x0 << " " << sh.y0 << " " << sh.x1 << " " << sh.y1 << " "
           << sh.radius << " " << sh.sides << " " << sh.inner << " " << +sh.closed << " " << +sh.fillOn;
         for (float v : sh.fillC) o << " " << v;
         o << " " << +sh.strokeOn;
         for (float v : sh.strokeC) o << " " << v;
         o << " " << sh.width << " " << sh.cap << " " << sh.join << " " << sh.align << " " << sh.dash << " " << sh.gap << " "
           << sh.op << " " << EscapeToken(sh.svg) << " " << sh.pts.size();
         for (float v : sh.pts) o << " " << v;
         o << " " << sh.fillType << " " << sh.fillAngle;
         WriteStops(o, sh.fillStops);
         // Later settings, read back only when present.
         o << " " << EscapeToken(sh.fillImage) << " " << sh.imageMode;
         for (const auto& c : sh.meshC) for (float v : c) o << " " << v;
         o << " " << sh.noise;
         for (int i = 0; i < 4; i++) o << " " << sh.cornerR[i] << " " << sh.cornerType[i];
         o << " " << sh.miter << " " << sh.dashPhase << " " << +sh.balanced << " " << sh.arrowS << " " << sh.arrowE << " "
           << sh.arrowSize << " " << sh.taperS << " " << sh.taperE << " " << sh.pressMin << " " << +sh.behind << " "
           << +sh.scaleStroke << " " << sh.arrowConcave;
         o << " " << sh.strokeType << " " << sh.strokeAngle;
         WriteStops(o, sh.strokeStops);
         o << "\n";
      };
      if (l.kind == Kind::Vector)
      {
         o << "vsize " << l.id << " " << l.pw << " " << l.ph << "\n";
         if (l.SoftShape())
            o << "vsoft " << l.id << " " << l.shapeFeather << " " << l.shapeDensity << "\n";
         for (const Shape& sh : l.shapes)
            shapeLine("shape", sh);
      }
      if (l.hasVMask)
      {
         o << "vmask " << l.id << " " << +l.vmaskOn << " " << +l.vmaskInvert << " " << l.vmaskFeather << " " << l.vmaskDensity << "\n";
         for (const Shape& sh : l.vmask)
            shapeLine("vshape", sh);
      }
      if (l.kind == Kind::Text)
      {
         o << "text " << l.id << " " << l.fontSize << " " << l.leading << " " << l.tracking << " " << l.align;
         for (float v : l.textC) o << " " << v;
         o << " " << l.boxW << " " << EscapeToken(l.font) << " " << EscapeToken(l.text) << "\n";
         if (l.bold || l.italic || l.underline || l.caps)
            o << "tstyle " << l.id << " " << +l.bold << " " << +l.italic << " " << +l.underline << " " << +l.caps << "\n";
         if (l.strike || l.highlight || !l.kerning || l.script != 0 || l.baseShift != 0 || l.textSX != 1 || l.textSY != 1)
         {
            o << "tchar " << l.id << " " << +l.strike << " " << +l.highlight << " " << +l.kerning << " " << l.script << " "
              << l.baseShift << " " << l.textSX << " " << l.textSY;
            for (float v : l.highlightC) o << " " << v;
            o << "\n";
         }
         if (l.ulOwn || l.stOwn || l.noBreak || l.optical || l.outline || l.offsetX != 0 || !l.lang.empty())
         {
            o << "tchar2 " << l.id << " " << +l.ulOwn << " " << +l.stOwn << " " << +l.noBreak << " " << +l.optical << " "
              << +l.outline << " " << l.offsetX << " " << l.outlineW;
            for (float v : l.ulC) o << " " << v;
            for (float v : l.stC) o << " " << v;
            for (float v : l.outlineC) o << " " << v;
            o << " " << (l.lang.empty() ? std::string("-") : l.lang) << "\n";
         }
         if (l.indentL != 0 || l.indentR != 0 || l.indentFirst != 0 || l.spaceBefore != 0 || l.spaceAfter != 0)
            o << "tpara " << l.id << " " << l.indentL << " " << l.indentR << " " << l.indentFirst << " " << l.spaceBefore << " "
              << l.spaceAfter << "\n";
         if (l.typeMode != 0 || l.vertical || l.pathOffset != 0 || l.pathFlip)
            o << "ttype " << l.id << " " << l.typeMode << " " << l.pathOffset << " " << +l.pathFlip << " " << +l.vertical << "\n";
         // The end point only when set, so files without it stay as they were (and load as "the path's end").
         if (l.pathEnd >= 0.0f)
            o << "tpend " << l.id << " " << l.pathEnd << "\n";
         for (const Shape& sh : l.typePath)
         {
            o << "tpath " << l.id << " " << sh.op << " " << +sh.closed << " " << sh.pts.size() / 6;
            for (float v : sh.pts) o << " " << v;
            o << "\n";
         }
         if (l.warpStyle != 0)
            o << "twarp " << l.id << " " << l.warpStyle << " " << l.warpBend << " " << l.warpH << " " << l.warpV << " "
              << +l.warpVert << "\n";
         // Styled runs, only when the text is not one plain run: "trun layer len charStyle font size tracking r g b a b i u".
         // Tail (optional, newer files): features (- = none) kernMode kern caps.
         if (l.runs.size() > 1 || (l.runs.size() == 1 && !l.runs[0].st.Plain()))
            for (const TextRun& r : l.runs)
            {
               o << "trun " << l.id << " " << r.len << " " << r.st.charStyle << " " << EscapeToken(r.st.font) << " " << r.st.size
                 << " " << r.st.tracking;
               for (float v : r.st.c) o << " " << v;
               o << " " << +r.st.bold << " " << +r.st.italic << " " << +r.st.underline;
               if (!r.st.features.empty() || r.st.kernMode != 0 || r.st.kern != 0.0f || r.st.caps)
                  o << " " << (r.st.features.empty() ? std::string("-") : EscapeToken(r.st.features)) << " " << r.st.kernMode << " "
                    << r.st.kern << " " << +r.st.caps;
               o << "\n";
            }
         if (l.paraStyle != 0)
            o << "tpstyle " << l.id << " " << l.paraStyle << "\n";
      }
      if (l.is3d || l.z != 0 || l.az != 0 || l.rx != 0 || l.ry != 0 || l.orient[0] != 0 || l.orient[1] != 0 ||
          l.orient[2] != 0 || l.collapse)
         o << "d3 " << l.id << " " << +l.is3d << " " << l.z << " " << l.az << " " << l.rx << " " << l.ry << " "
           << l.orient[0] << " " << l.orient[1] << " " << l.orient[2] << " " << +l.collapse << "\n";
      if (l.sz != 1.0f)
         o << "sz " << l.id << " " << l.sz << "\n";
      if (l.output)
         o << "out " << l.id << "\n";
      if (l.below)
         o << "below " << l.id << "\n";
      if (l.skinMirror != 0)
         o << "skinm " << l.id << " " << l.skinMirror << "\n";
      if (l.warpOn || std::any_of(l.warp, l.warp + 8, [](float v) { return v != 0.0f; }))
      {
         o << "warp " << l.id << " " << +l.warpOn;
         for (float v : l.warp) o << " " << v;
         o << "\n";
      }
      auto fxLine = [&](const char* key, int t, const Fx& f)
      {
         o << key << " " << l.id << " " << t << " ";
         WriteFxFields(o, f);
         o << "\n";
      };
      for (int t = 0; t < kFxCount; t++)
         if (!FxSame(l.fx[t], FxDefault(t)))
            fxLine("fx", t, l.fx[t]);
      for (const auto& m : l.fxMore)
         fxLine("fx2", m.first, m.second);
      if (l.HasBlendIf())
      {
         o << "bif " << l.id;
         for (float v : l.bifT) o << " " << v;
         for (float v : l.bifU) o << " " << v;
         o << "\n";
      }
      if (l.HasPixelMask())
      {
         o << "pmask " << l.id << " " << +l.pmaskOn << " " << l.mw << " " << l.mh;
         for (const std::string& k : l.mtiles) o << " " << k;
         o << "\n";
      }
      if (l.HasFilterMask())
      {
         o << "fmask " << l.id << " " << l.fmw << " " << l.fmh;
         for (const std::string& k : l.fmtiles) o << " " << k;
         o << "\n";
      }
   }
   mText = o.str();
   mParsedText = mText;
}

void Scene2DNode::Parse()
{
   mParsedText = mText;
   mLayers.clear();
   mDoc = Document();
   panelOnly = false;
   mNextId = 1;
   mSelStore = SelStore();
   mSelApronStore = ApronStore();
   mSelSaved.clear();
   mPaths.clear();
   mClipPath.clear();
   quickMask = false;
   std::map<int, int> alias;   // a repeated layer id: the later layer's new id, for the lines that follow it
   std::istringstream in(mText);
   std::string line;
   auto unit = [](float v) { return v >= 0.0f ? std::min(v, 1.0f) : 0.0f; };   // NaN -> 0
   auto find = [&](int id) -> Layer*
   {
      for (Layer& l : mLayers)
         if (l.id == id)
            return &l;
      return nullptr;
   };
   while (std::getline(in, line))
   {
      std::istringstream s(line);
      std::string key;
      s >> key;
      if (key == "next")
         s >> mNextId;
      else if (key == "panel")
         panelOnly = true;
      else if (key == "doc")
      {
         Document& d = mDoc;
         int ratio = 0, rulers = 0, grid = 0, snap = 1, guides = 1, lin = 1, head = 1;
         s >> d.w >> d.h >> d.background >> d.bgColor[0] >> d.bgColor[1] >> d.bgColor[2] >> ratio >> rulers >> grid >>
            snap >> guides >> d.gridSize >> d.gridSubdiv >> d.fps >> d.seconds >> lin >> head;
         d.ratioLock = ratio != 0;
         d.rulers = rulers != 0;
         d.grid = grid != 0;
         d.snap = snap != 0;
         d.guidesOn = guides != 0;
         d.linear = lin != 0;
         d.headroom = head != 0;
         d.w = std::clamp(d.w, 1, GLUtil::kMaxTargetSide);
         d.h = std::clamp(d.h, 1, GLUtil::kMaxTargetSide);
         d.gridSubdiv = std::clamp(d.gridSubdiv, 1, 16);
         d.gridSize = d.gridSize >= 1.0f ? std::min(d.gridSize, 100000.0f) : 1.0f;
         d.background = std::clamp(d.background, 0, 3);
      }
      else if (key == "qmask")
         quickMask = true;
      else if (key == "selapron")
      {
         ApronStore as;
         std::string k;
         s >> as.x0 >> as.y0 >> as.w >> as.h;
         while (s >> k)
            as.keys.push_back(k == "+" || TileStore::IsKey(k.data(), k.size()) ? k : "-");
         const int lim = 3 * GLUtil::kMaxTargetSide;
         if (as.w <= 0 || as.h <= 0 || as.w > lim || as.h > lim || std::abs(as.x0) > lim || std::abs(as.y0) > lim)
            continue;
         as.keys.resize((size_t)((as.w + TileStore::kTile - 1) / TileStore::kTile) * (size_t)((as.h + TileStore::kTile - 1) / TileStore::kTile), "-");
         mSelApronStore = as;
      }
      else if (key == "sel" || key == "selsave")
      {
         SelStore st;
         std::string name, k;
         if (key == "selsave" && s >> name)
            st.name = UnescapeToken(name);
         s >> st.w >> st.h;
         while (s >> k)
            st.keys.push_back(k == "+" || TileStore::IsKey(k.data(), k.size()) ? k : "-");
         if (st.w <= 0 || st.h <= 0 || st.w > GLUtil::kMaxTargetSide || st.h > GLUtil::kMaxTargetSide)
            continue;
         st.keys.resize((size_t)((st.w + TileStore::kTile - 1) / TileStore::kTile) * (size_t)((st.h + TileStore::kTile - 1) / TileStore::kTile), "-");
         if (key == "sel")
            mSelStore = st;
         else if (!st.name.empty())
            mSelSaved.push_back(st);
      }
      else if (key == "dpath")
      {
         std::string name;
         Shape sh;
         int op = 0, closed = 0;
         size_t n = 0;
         if (!(s >> name >> op >> closed >> n) || n == 0 || n > (size_t)kTraceShapesMax * 64)
            continue;
         sh.type = Shape::kPath;
         sh.op = std::clamp(op, 0, (int)Shape::kXor);
         sh.closed = closed != 0;
         sh.pts.resize(n * 6);
         bool ok = true;
         for (float& v : sh.pts)
            if (!(s >> v) || !std::isfinite(v) || std::fabs(v) > kPathCoordMax)
               ok = false;
         if (!ok)
            continue;
         name = UnescapeToken(name);
         auto it = std::find_if(mPaths.begin(), mPaths.end(), [&](const DocPath& p) { return p.name == name; });
         if (it == mPaths.end())
            it = mPaths.insert(mPaths.end(), DocPath{ name, {} });
         it->subs.push_back(std::move(sh));
      }
      else if (key == "clippath")
      {
         std::string name;
         if (s >> name)
            mClipPath = UnescapeToken(name);
      }
      else if (key == "guideslock")
      {
         int v = 0;
         s >> v;
         mDoc.guidesLocked = v != 0;
      }
      else if (key == "dpi")
      {
         float v = 72.0f;
         if (s >> v && v > 0 && std::isfinite(v))
            mDoc.dpi = v;
      }
      else if (key == "guidesx" || key == "guidesy")
      {
         float g = 0;
         while (s >> g)
            (key == "guidesx" ? mDoc.guidesX : mDoc.guidesY).push_back(g);
      }
      else if (key == "cstyle" || key == "pstyle" || key == "pbasic")
      {
         TextStyles& ts = mDoc.textStyles;
         uint32_t id = 0;
         std::string name, pa, ca;
         if (key == "pbasic")
         {
            StyleAttrs a, b;
            ParaStyle pp;
            RunStyle cp;
            if (s >> pa >> ca && ParseAttrsText(UnescapeToken(pa), a) && ParseAttrsText(UnescapeToken(ca), b) &&
                ApplyParaAttrs(pp, a) && ApplyCharAttrs(cp, b))
            {
               ts.basic.para = a;
               ts.basic.chr = b;
            }
         }
         else if (key == "cstyle")
         {
            CharStyleDef c;
            if (s >> id >> name >> ca && id != 0 && ts.Char(id) == nullptr && ParseAttrsText(UnescapeToken(ca), c.attrs))
            {
               RunStyle cp;
               c.id = id;
               c.name = UnescapeToken(name);
               if (!c.name.empty() && ApplyCharAttrs(cp, c.attrs))
                  ts.character.push_back(c);
            }
         }
         else
         {
            ParaStyleDef p;
            if (s >> id >> name >> pa >> ca && id != 0 && ts.Para(id) == nullptr && ParseAttrsText(UnescapeToken(pa), p.para) &&
                ParseAttrsText(UnescapeToken(ca), p.chr))
            {
               ParaStyle pp;
               RunStyle cp;
               p.id = id;
               p.name = UnescapeToken(name);
               if (!p.name.empty() && ApplyParaAttrs(pp, p.para) && ApplyCharAttrs(cp, p.chr))
                  ts.paragraph.push_back(p);
            }
         }
      }
      else if (key == "mslot")
      {
         ModSlot m;
         if (s >> m.layer >> m.field >> m.slot && m.field >= 0 && m.field < kLfCount && m.slot >= kSlotBase && m.slot <= kSlotMax)
            mDoc.slots.push_back(m);
      }
      else if (key == "cam")
         s >> mDoc.camUid;
      else if (key == "brushsrc")
      {
         int k = -1;
         uint64_t uid = 0;
         if (s >> k >> uid && k >= 0 && k < 3)
            mDoc.brushSrc[k] = uid;
      }
      else if (key == "brushlive")
      {
         int on = 0;
         s >> on;
         mDoc.brushLive = on != 0;
      }
      else if (key == "sym")
      {
         s >> mDoc.symmetry >> mDoc.symN;
         mDoc.symmetry = std::clamp(mDoc.symmetry, 0, 4);
         mDoc.symN = std::clamp(mDoc.symN, 2, 32);
      }
      else if (key == "layer")
      {
         Layer l;
         int kind = 0, vis = 1, clip = 0, open = 1;
         s >> l.id >> kind >> l.parent >> l.uid >> vis >> l.blend >> l.opacity >> l.fill >> l.lock >> l.label >> clip >> open;
         l.kind = (Kind)std::clamp(kind, 0, kKindCount - 1);
         l.visible = vis != 0;
         l.clip = clip != 0;
         l.open = open != 0;
         l.blend = std::clamp(l.blend, kPassThrough, 34);
         l.opacity = unit(l.opacity);
         l.fill = unit(l.fill);
         if (l.id <= 0 || find(l.id) != nullptr)
         {
            int nid = mNextId;
            for (const Layer& o : mLayers) nid = std::max(nid, o.id + 1);
            alias[l.id] = nid;
            l.id = nid;
         }
         else
            alias.erase(l.id);
         mNextId = std::max(mNextId, l.id + 1);
         mLayers.push_back(l);
      }
      else
      {
         int id = 0;
         s >> id;
         if (auto a = alias.find(id); a != alias.end())
            id = a->second;
         Layer* l = find(id);
         if (l == nullptr)
            continue;
         if (ParseEffectLine(key, s, *l))
         {
         }
         else if (key == "xf")
         {
            int fx = 0, fy = 0, nearest = 0;
            s >> l->x >> l->y >> l->sx >> l->sy >> l->angle >> l->ax >> l->ay >> fx >> fy >> l->fit >> nearest;
            l->flipX = fx != 0;
            l->flipY = fy != 0;
            l->nearest = nearest != 0;
         }
         else if (key == "link")
            s >> l->link;
         else if (key == "lx")
         {
            int ne = 0;
            s >> l->shear >> l->blendGamma >> l->aaMode >> ne;
            l->shear = std::clamp(l->shear, -80.0f, 80.0f);
            l->blendGamma = std::isfinite(l->blendGamma) ? std::clamp(l->blendGamma, 0.1f, 10.0f) : 2.2f;
            l->aaMode = std::clamp(l->aaMode, 0, 2);
            l->noExport = ne != 0;
         }
         else if (key == "taa")
         {
            s >> l->typeAA;
            l->typeAA = std::clamp(l->typeAA, 0, (int)kTypeAACount - 1);
         }
         else if (key == "fillx")
         {
            std::string img;
            s >> l->fillNoise >> img;
            l->fillNoise = std::clamp(l->fillNoise, 0.0f, 1.0f);
            l->fillImage = UnescapeToken(img);
         }
         else if (key == "name")
         {
            s >> std::ws;
            std::getline(s, l->name);
         }
         else if (key == "d3")
         {
            int on = 0, col = 0;
            s >> on >> l->z >> l->az >> l->rx >> l->ry >> l->orient[0] >> l->orient[1] >> l->orient[2] >> col;
            l->is3d = on != 0;
            l->collapse = col != 0;
         }
         else if (key == "out")
            l->output = true;
         else if (key == "below")
            l->below = true;
         else if (key == "sz")
         {
            s >> l->sz;
            if (!(std::fabs(l->sz) >= 0.01f))
               l->sz = 1.0f;
         }
         else if (key == "skinm")
         {
            s >> l->skinMirror;
            l->skinMirror = std::clamp(l->skinMirror, 0, 7);
         }
         else if (key == "warp")
         {
            int on = 0;
            s >> on;
            for (float& v : l->warp) s >> v;
            l->warpOn = on != 0;
         }
         else if (key == "fx" || key == "fx2")
         {
            int t = -1;
            s >> t;
            if (t >= 0 && t < kFxCount)
            {
               Fx got = FxDefault(t);
               ReadFxFields(s, key == "fx" ? l->fx[t] : got);
               if (key == "fx2")
                  l->fxMore.emplace_back(t, got);
            }
         }
         else if (key == "bif")
         {
            for (float& v : l->bifT) s >> v;
            for (float& v : l->bifU) s >> v;
         }
         else if (key == "crop")
         {
            int on = 0;
            s >> on >> l->crop[0] >> l->crop[1] >> l->crop[2] >> l->crop[3];
            l->cropOn = on != 0;
         }
         else if (key == "mask")
         {
            int on = 1, inv = 0;
            s >> l->maskUid >> on >> inv >> l->maskChannel >> l->maskDensity;
            l->maskOn = on != 0;
            l->maskInvert = inv != 0;
         }
         else if (key == "adj")
         {
            int map = 0;
            s >> l->adjType >> l->adjAmt >> l->adjTo >> l->adjDrive >> map;
            l->adjType = std::clamp(l->adjType, 0, kAdjCount - 1);
            l->adjDrive = std::clamp(l->adjDrive, 0, kDrives);
            l->adjMap = map != 0;
         }
         else if (key == "gmdither")
         {
            int on = 0;
            s >> on;
            l->adjDither = on != 0;
         }
         else if (key == "relief")
         {
            s >> l->reliefMat >> l->reliefLight >> l->reliefElev >> l->reliefDepth;
            l->relief = true;
            l->reliefMat = std::clamp(l->reliefMat, 0, 3);
         }
         else if (key == "levels")
         {
            for (float& v : l->levels)
            {
               s >> v;
               v = unit(v);
            }
         }
         else if (key == "curve")
         {
            int n = 0;
            s >> n;
            n = std::clamp(n, 0, 32);
            std::vector<float> pts(n * 2);
            for (float& v : pts) s >> v;
            if (s && n >= 2)
            {
               // One limit with the editor (16 points), each in 0..1, in x order.
               std::vector<std::pair<float, float>> xy;
               for (int i = 0; i < std::min(n, 16); i++)
                  xy.push_back({ unit(pts[i * 2]), unit(pts[i * 2 + 1]) });
               std::stable_sort(xy.begin(), xy.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
               l->curve.clear();
               for (const auto& q : xy)
               {
                  l->curve.push_back(q.first);
                  l->curve.push_back(q.second);
               }
            }
         }
         else if (key == "fillc")
         {
            s >> l->fillType;
            l->fillType = std::clamp(l->fillType, 0, 6);
            for (float& v : l->colorA) s >> v;
            for (float& v : l->colorB) s >> v;
            s >> l->fillAngle;
         }
         else if (key == "stops")
            ReadStops(s, l->stops);
         else if (key == "vsize")
         {
            s >> l->pw >> l->ph;
            l->pw = std::clamp(l->pw, 1, GLUtil::kMaxTargetSide);
            l->ph = std::clamp(l->ph, 1, GLUtil::kMaxTargetSide);
         }
         else if (key == "vsoft")
         {
            float feather = 0.0f, density = 1.0f;
            s >> feather >> density;
            // Hostile values render as the nearest valid ones (PhotoCraft hostile_values_render).
            l->shapeFeather = std::isfinite(feather) ? std::clamp(feather, 0.0f, 128.0f) : 0.0f;
            l->shapeDensity = std::isfinite(density) ? std::clamp(density, 0.0f, 1.0f) : 1.0f;
         }
         else if (key == "vmask")
         {
            int on = 1, inv = 0;
            float feather = 0.0f, density = 1.0f;
            s >> on;
            if (s >> inv)
               l->vmaskInvert = inv != 0;
            if (s >> feather)   // a hostile value renders as the nearest valid one (PhotoCraft hostile_values_render)
               l->vmaskFeather = std::isfinite(feather) ? std::clamp(feather, 0.0f, 128.0f) : 0.0f;
            if (s >> density)
               l->vmaskDensity = std::isfinite(density) ? std::clamp(density, 0.0f, 1.0f) : 1.0f;
            l->hasVMask = true;
            l->vmaskOn = on != 0;
         }
         else if (key == "shape" || key == "vshape")
         {
            Shape sh;
            int closed = 0, fillOn = 1, strokeOn = 0;
            size_t n = 0;
            std::string svg;
            s >> sh.type >> sh.x0 >> sh.y0 >> sh.x1 >> sh.y1 >> sh.radius >> sh.sides >> sh.inner >> closed >> fillOn;
            for (float& v : sh.fillC) s >> v;
            s >> strokeOn;
            for (float& v : sh.strokeC) s >> v;
            s >> sh.width >> sh.cap >> sh.join >> sh.align >> sh.dash >> sh.gap >> sh.op >> svg >> n;
            if (!s)
               continue;
            sh.type = std::clamp(sh.type, 0, (int)Shape::kTypeCount - 1);
            sh.sides = std::clamp(sh.sides, 3, 64);
            sh.op = std::clamp(sh.op, 0, 3);
            sh.closed = closed != 0;
            sh.fillOn = fillOn != 0;
            sh.strokeOn = strokeOn != 0;
            sh.svg = UnescapeToken(svg);
            n = std::min<size_t>(n, 6 * 100000) / 6 * 6;
            sh.pts.reserve(n);
            float v = 0;
            while (sh.pts.size() < n && s >> v)
               sh.pts.push_back(v);
            sh.pts.resize(sh.pts.size() / 6 * 6);
            if (s >> sh.fillType >> sh.fillAngle)
            {
               sh.fillType = std::clamp(sh.fillType, 0, 6);
               ReadStops(s, sh.fillStops);
               std::string img;
               if (s >> img >> sh.imageMode)
               {
                  sh.fillImage = UnescapeToken(img);
                  sh.imageMode = std::clamp(sh.imageMode, 0, 3);
                  for (auto& c : sh.meshC) for (float& v : c) s >> v;
                  s >> sh.noise;
                  for (int i = 0; i < 4; i++)
                  {
                     s >> sh.cornerR[i] >> sh.cornerType[i];
                     sh.cornerType[i] = std::clamp(sh.cornerType[i], 0, 3);
                  }
                  int bal = 0, beh = 0, sc = 1;
                  s >> sh.miter >> sh.dashPhase >> bal >> sh.arrowS >> sh.arrowE >> sh.arrowSize >> sh.taperS >> sh.taperE >>
                     sh.pressMin >> beh >> sc;
                  sh.balanced = bal != 0;
                  sh.behind = beh != 0;
                  sh.scaleStroke = sc != 0;
                  sh.arrowS = std::clamp(sh.arrowS, 0, 5);
                  sh.arrowE = std::clamp(sh.arrowE, 0, 5);
                  float cc = 0.0f;
                  if (s >> cc && std::isfinite(cc))   // whole percents; an old 0..100 value is clamped, not refused
                     sh.arrowConcave = std::clamp(std::round(cc), -Shape::kArrowConcaveMax, Shape::kArrowConcaveMax);
                  sh.noise = std::clamp(sh.noise, 0.0f, 1.0f);
                  sh.miter = std::clamp(sh.miter, 1.0f, 100.0f);
                  int stype = 0;
                  float sangle = 90.0f;
                  if (s >> stype >> sangle && std::isfinite(sangle))
                  {
                     sh.strokeType = std::clamp(stype, 0, 4);
                     sh.strokeAngle = sangle;
                     ReadStops(s, sh.strokeStops);
                  }
               }
            }
            (key == "shape" ? l->shapes : l->vmask).push_back(sh);
         }
         else if (key == "text")
         {
            std::string font, text;
            s >> l->fontSize >> l->leading >> l->tracking >> l->align;
            for (float& v : l->textC) s >> v;
            s >> l->boxW >> font >> text;
            l->font = UnescapeToken(font);
            l->text = UnescapeToken(text);
            l->fontSize = std::clamp(l->fontSize, 1.0f, 2000.0f);
            l->align = std::clamp(l->align, 0, 3);
         }
         else if (key == "tstyle")
         {
            int b = 0, i = 0, u = 0, c = 0;
            s >> b >> i >> u >> c;
            l->bold = b != 0;
            l->italic = i != 0;
            l->underline = u != 0;
            l->caps = c != 0;
         }
         else if (key == "tchar")
         {
            int st = 0, hi = 0, ke = 1;
            if (s >> st >> hi >> ke >> l->script >> l->baseShift >> l->textSX >> l->textSY)
            {
               l->strike = st != 0;
               l->highlight = hi != 0;
               l->kerning = ke != 0;
               l->script = std::clamp(l->script, 0, 2);
               l->textSX = std::clamp(l->textSX, 0.01f, 100.0f);
               l->textSY = std::clamp(l->textSY, 0.01f, 100.0f);
               for (float& v : l->highlightC)
                  s >> v;
            }
         }
         else if (key == "tchar2")
         {
            int uo = 0, so = 0, nb = 0, op = 0, ol = 0;
            if (s >> uo >> so >> nb >> op >> ol >> l->offsetX >> l->outlineW)
            {
               l->ulOwn = uo != 0;
               l->stOwn = so != 0;
               l->noBreak = nb != 0;
               l->optical = op != 0;
               l->outline = ol != 0;
               l->outlineW = std::clamp(l->outlineW, 0.0f, 200.0f);
               for (float& v : l->ulC) s >> v;
               for (float& v : l->stC) s >> v;
               for (float& v : l->outlineC) s >> v;
               std::string lg;
               if (s >> lg && lg != "-")
                  l->lang = lg;
            }
         }
         else if (key == "tpara")
            s >> l->indentL >> l->indentR >> l->indentFirst >> l->spaceBefore >> l->spaceAfter;
         else if (key == "trun")
         {
            TextRun r;
            std::string font;
            int b = 0, i = 0, u = 0;
            if (s >> r.len >> r.st.charStyle >> font >> r.st.size >> r.st.tracking >> r.st.c[0] >> r.st.c[1] >> r.st.c[2] >>
                    r.st.c[3] >> b >> i >> u &&
                std::isfinite(r.st.size) && std::isfinite(r.st.tracking) && r.len <= l->text.size() + 4)
            {
               r.st.font = UnescapeToken(font);
               r.st.size = std::clamp(r.st.size, 1.0f, 2000.0f);
               for (float& v : r.st.c)
                  v = unit(v);
               r.st.bold = b != 0;
               r.st.italic = i != 0;
               r.st.underline = u != 0;
               std::string feats;
               int km = 0, cp = 0;
               float kv = 0.0f;
               if (s >> feats >> km >> kv >> cp)
               {
                  r.st.features = feats == "-" ? std::string() : UnescapeToken(feats);
                  r.st.kernMode = std::clamp(km, 0, 2);
                  r.st.kern = std::isfinite(kv) ? std::clamp(kv, -1000.0f, 10000.0f) : 0.0f;
                  r.st.caps = cp != 0;
               }
               l->runs.push_back(r);
            }
         }
         else if (key == "tpstyle")
            s >> l->paraStyle;
         else if (key == "ttype")
         {
            int mode = 0, flip = 0, vert = 0;
            float off = 0.0f;
            if (s >> mode >> off >> flip >> vert)
            {
               l->typeMode = std::clamp(mode, 0, 2);
               l->pathOffset = std::isfinite(off) ? off : 0.0f;
               l->pathFlip = flip != 0;
               l->vertical = vert != 0;
            }
         }
         else if (key == "tpend")
         {
            float e = -1.0f;
            if (s >> e)
               l->pathEnd = std::isfinite(e) && e >= 0.0f ? std::min(e, 1.0f) : -1.0f;
         }
         else if (key == "tpath")
         {
            Shape sh;
            int op = 0, closed = 0;
            size_t n = 0;
            if (s >> op >> closed >> n && n > 0 && n <= (size_t)kTypePathAnchorsMax)
            {
               sh.type = Shape::kPath;
               sh.op = std::clamp(op, 0, (int)Shape::kXor);
               sh.closed = closed != 0;
               sh.pts.resize(n * 6);
               bool ok = true;
               for (float& v : sh.pts)
                  if (!(s >> v) || !std::isfinite(v) || std::fabs(v) > kPathCoordMax)
                     ok = false;
               if (ok)
                  l->typePath.push_back(std::move(sh));
            }
         }
         else if (key == "twarp")
         {
            int style = 0, vert = 0;
            float b = 50.0f, h = 0.0f, v = 0.0f;
            if (s >> style >> b >> h >> v >> vert && std::isfinite(b) && std::isfinite(h) && std::isfinite(v))
            {
               l->warpStyle = std::clamp(style, 0, kWarpStyleCount - 1);
               l->warpBend = std::clamp(b, -100.0f, 100.0f);
               l->warpH = std::clamp(h, -100.0f, 100.0f);
               l->warpV = std::clamp(v, -100.0f, 100.0f);
               l->warpVert = vert != 0;
            }
         }
         else if (key == "paint" || key == "pmask" || key == "fmask")
         {
            const bool mask = key == "pmask", fx = key == "fmask";
            int on = 1, w = 0, h = 0;
            if (mask)
               s >> on;
            s >> w >> h;
            w = std::clamp(w, 0, GLUtil::kMaxTargetSide);
            h = std::clamp(h, 0, GLUtil::kMaxTargetSide);
            const size_t n = (size_t)((w + TileStore::kTile - 1) / TileStore::kTile) * (size_t)((h + TileStore::kTile - 1) / TileStore::kTile);
            std::vector<std::string> keys;
            std::string k;
            while (keys.size() < n && s >> k)
               keys.push_back(TileStore::IsKey(k.data(), k.size()) ? k : "-");
            keys.resize(n, "-");
            if (fx)
            {
               l->fmw = w;
               l->fmh = h;
               l->fmtiles = keys;
            }
            else if (mask)
            {
               l->pmaskOn = on != 0;
               l->mw = w;
               l->mh = h;
               l->mtiles = keys;
            }
            else
            {
               l->pw = w;
               l->ph = h;
               l->tiles = keys;
            }
         }
      }
   }
   // Runs: a style id that names no style falls back (None / Basic Paragraph); the runs then cover the text and lead
   // the summary fields. No runs: one run from the fields, made on the next sync.
   for (Layer& l : mLayers)
   {
      if (l.kind != Kind::Text)
      {
         l.runs.clear();
         continue;
      }
      if (l.paraStyle != 0 && mDoc.textStyles.Para(l.paraStyle) == nullptr)
         l.paraStyle = 0;
      for (TextRun& r : l.runs)
         if (r.st.charStyle != 0 && mDoc.textStyles.Char(r.st.charStyle) == nullptr)
            r.st.charStyle = 0;
      if (!l.runs.empty())
         NormalizeRuns(l);
   }
   RestoreSelection();
   // A parent that is not a group (hand-written text) puts the layer back at the top level.
   for (Layer& l : mLayers)
   {
      const Layer* p = l.parent != 0 ? find(l.parent) : nullptr;
      if (l.parent != 0 && (p == nullptr || p->kind != Kind::Group || p == &l))
         l.parent = 0;
   }
   // A hand-written file nested past the group cap: each layer too deep moves up to the deepest group it may sit in.
   for (Layer& l : mLayers)
   {
      int chain[kMaxGroupDepth + 2], n = 0;
      for (int q = l.parent; q != 0 && n < kMaxGroupDepth + 1; )
      {
         chain[n++] = q;
         const Layer* g = find(q);
         q = g != nullptr ? g->parent : 0;
      }
      const int cap = l.kind == Kind::Group ? kMaxGroupDepth - 1 : kMaxGroupDepth;
      if (n > cap)
      {
         l.parent = cap > 0 ? chain[n - cap] : 0;
         std::fprintf(stderr, "PROBE load: layer %d nested past %d groups, moved up to %d\n", l.id, kMaxGroupDepth, l.parent);
      }
   }
}

// ---- cook ---------------------------------------------------------------

bool Scene2DNode::EnsureShaders()
{
   if (mShaderTried)
      return mPassProgram != 0 && mFinishProgram != 0;
   mShaderTried = true;
   mPassProgram = GLUtil::CompileProgram(PassSrc().c_str());
   mFinishProgram = GLUtil::CompileProgram(
      GLUtil::WithClampedOutput(FinishSrc(), "if (uLinear == 1) fragColor.rgb = SrgbEncode(fragColor.rgb);").c_str());
   return mPassProgram != 0 && mFinishProgram != 0;
}

Scene2DNode::Pair& Scene2DNode::PairAt(int depth)
{
   if (!mQueue.empty() && depth == mQueueDepth)
      FlushQueue();
   while ((int)mPool.size() <= depth)
      mPool.emplace_back();
   Pair& p = mPool[depth];
   for (GLUtil::Fbo& f : p.f)
      GLUtil::EnsureFbo(f, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D");
   return p;
}

void Scene2DNode::Clear(Pair& p, const float rgba[4])
{
   GLint prev = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
   glBindFramebuffer(GL_FRAMEBUFFER, p.f[p.cur].fbo);
   glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
   glClear(GL_COLOR_BUFFER_BIT);
   glBindFramebuffer(GL_FRAMEBUFFER, prev);
}

void Scene2DNode::Pass(int depth, const PassArgs& a)
{
   if (Batchable(depth, a))
   {
      if (!mQueue.empty() && mQueueDepth != depth)
         FlushQueue();
      mQueueDepth = depth;
      mQueue.push_back(a);
      if ((int)mQueue.size() >= kBatch)
         FlushQueue();
      return;
   }
   if (!mQueue.empty())
      FlushQueue();
   mPasses++;
   Pair& p = PairAt(depth);
   const unsigned int acc = p.Tex();
   const int next = 1 - p.cur;
   const unsigned int prog = mPassProgram;
   const int depthMode = depth == mDepthLevel ? (a.card ? 1 : 2) : 0;
   // A layer that covers part of the frame: draw only that part into the other buffer and copy it back, so the
   // rest of the picture is never read or written.
   extern bool gScene2DBounds;
   const bool part = gScene2DBounds && depthMode == 0 && a.rect[2] > a.rect[0] && a.rect[3] > a.rect[1];
   // Normal over an opaque top-level picture: blend straight into the current buffer (no read of the picture, no
   // second buffer, no copy back).
   const bool direct = gScene2DBounds && depth == 0 && depthMode == 0 && mAccOpaque && a.blend == 0 && !a.clipMode && a.bif == nullptr &&
                       a.gamma == 1.0f;
   const auto setup = [&]()
   {
      glUniform1i(GLUtil::UniformLoc(prog, "uDirect"), direct ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, direct ? 0u : acc);
      glUniform1i(GLUtil::UniformLoc(prog, "uAcc"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, a.tex);
      glUniform1i(GLUtil::UniformLoc(prog, "uTex"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, a.mask);
      glUniform1i(GLUtil::UniformLoc(prog, "uMask"), 2);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, a.pmask);
      glUniform1i(GLUtil::UniformLoc(prog, "uPMask"), 3);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasPMask"), a.pmask != 0 ? 1 : 0);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, a.vmask);
      glUniform1i(GLUtil::UniformLoc(prog, "uVMask"), 4);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasVMask"), a.vmask != 0 ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uVMaskInv"), a.vmaskInvert ? 1 : 0);
      glUniform1f(GLUtil::UniformLoc(prog, "uVMaskDensity"), a.vmaskDensity);
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(GLUtil::UniformLoc(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform3f(GLUtil::UniformLoc(prog, "uInvA"), a.inv[0], a.inv[1], a.inv[2]);
      glUniform3f(GLUtil::UniformLoc(prog, "uInvB"), a.inv[3], a.inv[4], a.inv[5]);
      glUniform3f(GLUtil::UniformLoc(prog, "uInvC"), a.inv[6], a.inv[7], a.inv[8]);
      glUniform1i(GLUtil::UniformLoc(prog, "uDepthMode"), depthMode);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D, a.geoDepth);
      glUniform1i(GLUtil::UniformLoc(prog, "uGeoDepth"), 6);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasGeoDepth"), a.geoDepth != 0 ? 1 : 0);
      glUniform2f(GLUtil::UniformLoc(prog, "uGeoNF"), a.geoNF[0], a.geoNF[1]);
      glUniform4f(GLUtil::UniformLoc(prog, "uGeoUv"), a.geoUv[0], a.geoUv[1], a.geoUv[2], a.geoUv[3]);
      glActiveTexture(GL_TEXTURE0);
      if (depthMode != 0)
      {
         glActiveTexture(GL_TEXTURE5);
         glBindTexture(GL_TEXTURE_2D, mDepth[depth].tex[p.cur]);
         glUniform1i(GLUtil::UniformLoc(prog, "uDepthIn"), 5);
         glActiveTexture(GL_TEXTURE0);
         const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
         glDrawBuffers(2, bufs);
      }
      glUniform1i(GLUtil::UniformLoc(prog, "uBif"), a.bif != nullptr ? 1 : 0);
      if (a.bif != nullptr)
      {
         glUniform4fv(GLUtil::UniformLoc(prog, "uBifT"), 1, a.bif->bifT);
         glUniform4fv(GLUtil::UniformLoc(prog, "uBifU"), 1, a.bif->bifU);
      }
      glUniform2f(GLUtil::UniformLoc(prog, "uBox"), std::max(1e-3f, a.box[0]), std::max(1e-3f, a.box[1]));
      glUniform4f(GLUtil::UniformLoc(prog, "uCrop"), a.crop[0], a.crop[1], a.crop[2], a.crop[3]);
      glUniform2f(GLUtil::UniformLoc(prog, "uTexSize"), (float)std::max(1, a.texW), (float)std::max(1, a.texH));
      glUniform1i(GLUtil::UniformLoc(prog, "uKind"), a.kind);
      glUniform1i(GLUtil::UniformLoc(prog, "uMode"), a.blend);
      glUniform1i(GLUtil::UniformLoc(prog, "uClip"), a.clipMode ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uNearest"), a.nearest ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), mDoc.linear ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uSrgb"), a.srgb ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasMask"), a.mask != 0 ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uMaskChan"), a.maskChannel);
      glUniform1i(GLUtil::UniformLoc(prog, "uMaskInv"), a.maskInvert ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uHead"), mDoc.headroom ? 1 : 0);
      glUniform1f(GLUtil::UniformLoc(prog, "uOpacity"), std::clamp(a.opacity, 0.0f, 1.0f));
      glUniform1f(GLUtil::UniformLoc(prog, "uMaskDensity"), std::clamp(a.maskDensity, 0.0f, 1.0f));
      glUniform1f(GLUtil::UniformLoc(prog, "uGamma"), std::clamp(a.gamma, 0.05f, 5.0f));
      glUniform1f(GLUtil::UniformLoc(prog, "uFillNoise"), a.fill != nullptr ? std::clamp(a.fill->fillNoise, 0.0f, 1.0f) : 0.0f);
      if (a.fill != nullptr)
      {
         glUniform4fv(GLUtil::UniformLoc(prog, "uColA"), 1, a.fill->colorA);
         glUniform4fv(GLUtil::UniformLoc(prog, "uColB"), 1, a.fill->colorB);
         glUniform1f(GLUtil::UniformLoc(prog, "uFillAngle"), a.fill->fillAngle);
      }
      StopUniforms(prog, a.fill != nullptr ? a.fill->stops : std::vector<GradStop>());
   };
   if (direct)
   {
      const GLboolean wasBlend = glIsEnabled(GL_BLEND);
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
      const int sc[4] = { a.rect[0], a.rect[1], a.rect[2] - a.rect[0], a.rect[3] - a.rect[1] };
      GLUtil::DrawPass(p.f[p.cur], prog, setup, part ? sc : nullptr);
      if (!wasBlend)
         glDisable(GL_BLEND);
      return;
   }
   if (part)
   {
      glEnable(GL_SCISSOR_TEST);
      glScissor(a.rect[0], a.rect[1], a.rect[2] - a.rect[0], a.rect[3] - a.rect[1]);
   }
   GLUtil::RunShaderPass(p.f[next], prog, setup);
   if (part)
   {
      glDisable(GL_SCISSOR_TEST);
      GLint prevR = 0, prevD = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, p.f[next].fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p.f[p.cur].fbo);
      glBlitFramebuffer(a.rect[0], a.rect[1], a.rect[2], a.rect[3], a.rect[0], a.rect[1], a.rect[2], a.rect[3],
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
      return;
   }
   if (depthMode != 0)
   {
      GLint prev = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_FRAMEBUFFER, p.f[next].fbo);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glBindFramebuffer(GL_FRAMEBUFFER, prev);
   }
   p.cur = next;
}

bool Scene2DNode::sBatch = true;
bool Scene2DNode::sExport = false;

namespace
{
   // The single pass's texture-layer path (layerAt kind 0, blend, alpha over) for kBatch layers at once; slot k
   // runs when uCount > k. Samplers are separate uniforms so every index is a constant.
   const std::string& BatchSrc()
   {
      static const std::string src = []()
      {
         const int n = 15;   // Scene2DNode::kBatch
         std::string s = std::string(
                            "#version 150\n"
                            "in vec2 vUv;\n"
                            "out vec4 fragColor;\n"
                            "uniform sampler2D uAcc;\n"
                            "uniform vec2 uDoc;\n"
                            "uniform int uLinear;\n"
                            "uniform int uHead;\n"
                            "uniform int uCount;\n")
                         + "uniform vec3 uInvA[" + std::to_string(n) + "];\n"
                         + "uniform vec3 uInvB[" + std::to_string(n) + "];\n"
                         + "uniform vec3 uInvC[" + std::to_string(n) + "];\n"
                         + "uniform vec2 uBox[" + std::to_string(n) + "];\n"
                         + "uniform vec4 uCrop[" + std::to_string(n) + "];\n"
                         + "uniform vec4 uRect[" + std::to_string(n) + "];\n"
                         + "uniform vec2 uTexSize[" + std::to_string(n) + "];\n"
                         + "uniform int uMode[" + std::to_string(n) + "];\n"
                         + "uniform float uOpacity[" + std::to_string(n) + "];\n"
                         + "uniform int uNearest[" + std::to_string(n) + "];\n"
                         + "uniform int uSrgb[" + std::to_string(n) + "];\n";
         for (int k = 0; k < n; k++)
            s += "uniform sampler2D uT" + std::to_string(k) + ";\n";
         s += GLUtil::kSrgbGlsl;
         s += BlendModes::kBlendGLSL;
         s += "// Downscale prefilter: an area average over the output pixel's footprint in the texture (du, dv = the uv step\n"
              "// per document pixel), so a layer shrunk far comes out its average, not point-sampled moire (PhotoCraft).\n"
              "vec4 areaTex(sampler2D t, vec2 uv, vec2 du, vec2 dv, vec2 ts) {\n"
              "   float nx = clamp(ceil(length(du * ts) - 0.25), 1.0, 8.0), ny = clamp(ceil(length(dv * ts) - 0.25), 1.0, 8.0);\n"
              "   if (nx * ny < 2.0) return texture(t, uv);\n"
              "   vec4 s = vec4(0.0);\n"
              "   for (int j = 0; j < 8; j++) { if (float(j) >= ny) break;\n"
              "      for (int i = 0; i < 8; i++) { if (float(i) >= nx) break;\n"
              "         vec2 o = (vec2(float(i), float(j)) + 0.5) / vec2(nx, ny) - 0.5;\n"
              "         s += texture(t, uv + du * o.x + dv * o.y); } }\n"
              "   return s / (nx * ny);\n"
              "}\n";
         s += "vec2 uvAtK(vec2 pp, int k) {\n"
              "   vec3 q = vec3(dot(uInvA[k], vec3(pp, 1.0)), dot(uInvB[k], vec3(pp, 1.0)), dot(uInvC[k], vec3(pp, 1.0)));\n"
              "   vec2 b = q.xy / max(q.z, 1e-12) / uBox[k];\n"
              "   return vec2(b.x, 1.0 - b.y);\n"
              "}\n";
         s += "vec4 over(vec4 acc, sampler2D t, int k) {\n"
              "   vec4 r = uRect[k];\n"
              "   if (gl_FragCoord.x < r.x || gl_FragCoord.y < r.y || gl_FragCoord.x > r.z || gl_FragCoord.y > r.w) return acc;\n"
              "   vec2 p = vec2(vUv.x * uDoc.x, (1.0 - vUv.y) * uDoc.y);\n"
              "   vec3 q = vec3(dot(uInvA[k], vec3(p, 1.0)), dot(uInvB[k], vec3(p, 1.0)), dot(uInvC[k], vec3(p, 1.0)));\n"
              "   if (q.z <= 1e-12) return acc;\n"
              "   vec2 uvb = (q.xy / max(q.z, 1e-12)) / uBox[k];\n"
              "   vec2 uv = vec2(uvb.x, 1.0 - uvb.y);\n"
              "   vec4 c = uCrop[k];\n"
              "   vec4 layer;\n"
              "   if (uvb.x < c.x || uvb.x > c.z || uvb.y < c.y || uvb.y > c.w) layer = vec4(0.0);\n"
              "   else if (uNearest[k] == 1) layer = texelFetch(t, ivec2(clamp(floor(uv * uTexSize[k]), vec2(0.0), uTexSize[k] - 1.0)), 0);\n"
              "   else layer = areaTex(t, uv, uvAtK(p + vec2(1.0, 0.0), k) - uv, uvAtK(p + vec2(0.0, 1.0), k) - uv, uTexSize[k]);\n"
              "   if (uLinear == 1 && uSrgb[k] == 1) layer.rgb = SrgbDecode(layer.rgb);\n"
              "   float amt = uOpacity[k] * layer.a;\n"
              "   vec3 cs = mix(layer.rgb, blendModeImage(uMode[k], acc.rgb, layer.rgb, 1, uHead), acc.a);\n"
              "   float ar = amt + acc.a * (1.0 - amt);\n"
              "   vec3 cr = (ar > 1e-5) ? (cs * amt + acc.rgb * acc.a * (1.0 - amt)) / ar : vec3(0.0);\n"
              "   return vec4(cr, ar);\n"
              "}\n"
              "void main() {\n"
              "   vec4 acc = texture(uAcc, vUv);\n";
         for (int k = 0; k < n; k++)
            s += "   if (uCount > " + std::to_string(k) + ") acc = over(acc, uT" + std::to_string(k) + ", " + std::to_string(k) + ");\n";
         s += "   fragColor = acc;\n}\n";
         return s;
      }();
      return src;
   }
}

bool Scene2DNode::Batchable(int depth, const PassArgs& a) const
{
   static_assert(kBatch == 15, "BatchSrc builds 15 slots");
   return sBatch && a.stable && a.kind == 0 && a.tex != 0 && a.mask == 0 && a.pmask == 0 && a.vmask == 0 && a.fill == nullptr &&
          a.bif == nullptr && a.gamma == 1.0f && !a.clipMode && !a.card && a.geoDepth == 0 && depth != mDepthLevel &&
          a.blend != BlendModes::kEraseMode && a.blend != BlendModes::kAntiEraseMode;
}

// The batch shader's sampler names, built once (they were a heap string per layer per flush).
static const char* const kBatchSamplerNames[15] = { "uT0", "uT1", "uT2", "uT3", "uT4", "uT5", "uT6", "uT7",
                                                    "uT8", "uT9", "uT10", "uT11", "uT12", "uT13", "uT14" };

void Scene2DNode::FlushQueue()
{
   static_assert(kBatch == 15, "kBatchSamplerNames covers kBatch samplers");
   if (mQueue.empty())
      return;
   std::vector<PassArgs> q;
   q.swap(mQueue);
   const int depth = mQueueDepth;
   mQueueDepth = -1;
   if (!mBatchTried)
   {
      mBatchTried = true;
      mBatchProgram = GLUtil::CompileProgram(BatchSrc().c_str());
   }
   if (q.size() == 1 || mBatchProgram == 0)
   {
      for (PassArgs& a : q)
      {
         a.stable = false;
         Pass(depth, a);
      }
      return;
   }
   Pair& p = PairAt(depth);
   const unsigned int acc = p.Tex();
   const int next = 1 - p.cur;
   const unsigned int prog = mBatchProgram;
   const int n = (int)q.size();
   float inv[3][kBatch * 3], box[kBatch * 2], crop[kBatch * 4], rect[kBatch * 4], ts[kBatch * 2], op[kBatch];
   int mode[kBatch], nearest[kBatch], srgb[kBatch];
   for (int k = 0; k < n; k++)
   {
      const PassArgs& a = q[k];
      for (int r = 0; r < 3; r++)
         for (int c = 0; c < 3; c++)
            inv[r][k * 3 + c] = a.inv[r * 3 + c];
      box[k * 2] = std::max(1e-3f, a.box[0]);
      box[k * 2 + 1] = std::max(1e-3f, a.box[1]);
      for (int c = 0; c < 4; c++)
         crop[k * 4 + c] = a.crop[c];
      const bool bounded = a.rect[2] > a.rect[0] && a.rect[3] > a.rect[1];
      rect[k * 4 + 0] = bounded ? (float)a.rect[0] : -1e9f;
      rect[k * 4 + 1] = bounded ? (float)a.rect[1] : -1e9f;
      rect[k * 4 + 2] = bounded ? (float)a.rect[2] : 1e9f;
      rect[k * 4 + 3] = bounded ? (float)a.rect[3] : 1e9f;
      ts[k * 2] = (float)std::max(1, a.texW);
      ts[k * 2 + 1] = (float)std::max(1, a.texH);
      op[k] = std::clamp(a.opacity, 0.0f, 1.0f);
      mode[k] = a.blend;
      nearest[k] = a.nearest ? 1 : 0;
      srgb[k] = a.srgb ? 1 : 0;
   }
   const auto setup = [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, acc);
      glUniform1i(GLUtil::UniformLoc(prog, "uAcc"), 0);
      for (int k = 0; k < n; k++)
      {
         glActiveTexture(GL_TEXTURE1 + k);
         glBindTexture(GL_TEXTURE_2D, q[k].tex);
         glUniform1i(GLUtil::UniformLoc(prog, kBatchSamplerNames[k]), 1 + k);
      }
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(GLUtil::UniformLoc(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), mDoc.linear ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uHead"), mDoc.headroom ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uCount"), n);
      glUniform3fv(GLUtil::UniformLoc(prog, "uInvA"), n, inv[0]);
      glUniform3fv(GLUtil::UniformLoc(prog, "uInvB"), n, inv[1]);
      glUniform3fv(GLUtil::UniformLoc(prog, "uInvC"), n, inv[2]);
      glUniform2fv(GLUtil::UniformLoc(prog, "uBox"), n, box);
      glUniform4fv(GLUtil::UniformLoc(prog, "uCrop"), n, crop);
      glUniform4fv(GLUtil::UniformLoc(prog, "uRect"), n, rect);
      glUniform2fv(GLUtil::UniformLoc(prog, "uTexSize"), n, ts);
      glUniform1fv(GLUtil::UniformLoc(prog, "uOpacity"), n, op);
      glUniform1iv(GLUtil::UniformLoc(prog, "uMode"), n, mode);
      glUniform1iv(GLUtil::UniformLoc(prog, "uNearest"), n, nearest);
      glUniform1iv(GLUtil::UniformLoc(prog, "uSrgb"), n, srgb);
   };
   // The part of the frame any of them can touch (a layer with no rect touches all of it).
   extern bool gScene2DBounds;
   int u[4] = { mDoc.w, mDoc.h, 0, 0 };
   bool all = !gScene2DBounds;
   for (const PassArgs& a : q)
   {
      if (!(a.rect[2] > a.rect[0] && a.rect[3] > a.rect[1]))
         all = true;
      u[0] = std::min(u[0], a.rect[0]);
      u[1] = std::min(u[1], a.rect[1]);
      u[2] = std::max(u[2], a.rect[2]);
      u[3] = std::max(u[3], a.rect[3]);
   }
   const bool part = !all && (double)(u[2] - u[0]) * (u[3] - u[1]) <= 0.6 * (double)mDoc.w * mDoc.h;
   const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
   if (part)
   {
      glEnable(GL_SCISSOR_TEST);
      glScissor(u[0], u[1], u[2] - u[0], u[3] - u[1]);
   }
   else if (scissor)
      glDisable(GL_SCISSOR_TEST);
   GLUtil::RunShaderPass(p.f[next], prog, setup);
   if (part)
   {
      // Only that part was drawn: copy it back into the current buffer, which holds the rest.
      glDisable(GL_SCISSOR_TEST);
      GLint prevR = 0, prevD = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, p.f[next].fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p.f[p.cur].fbo);
      glBlitFramebuffer(u[0], u[1], u[2], u[3], u[0], u[1], u[2], u[3], GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
   }
   else
      p.cur = next;
   if (scissor)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);
   mBatched += n;
}

// The box's corners through the layer's homography, padded a little for filtering. Outside them the pass leaves
// the picture as it is (the layer reads as clear there), except Anti-Erase, which clears what it does not cover.
bool Scene2DNode::LayerRect(const float h[9], PassArgs& a) const
{
   if (a.blend == BlendModes::kAntiEraseMode)
      return true;
   float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
   for (int c = 0; c < 4; c++)
   {
      const float bx = ((c == 1 || c == 2) ? a.crop[2] : a.crop[0]) * a.box[0], by = (c >= 2 ? a.crop[3] : a.crop[1]) * a.box[1];
      const float w = h[6] * bx + h[7] * by + h[8];
      if (!(w > 1e-6f))
         return true;   // a corner behind the camera: the shape is unbounded on screen
      const float x = (h[0] * bx + h[1] * by + h[2]) / w, y = (h[3] * bx + h[4] * by + h[5]) / w;
      x0 = std::min(x0, x);
      x1 = std::max(x1, x);
      y0 = std::min(y0, y);
      y1 = std::max(y1, y);
   }
   const int W = mDoc.w, H = mDoc.h;
   const int rx0 = (int)std::clamp(std::floor(x0) - 2.0f, 0.0f, (float)W), rx1 = (int)std::clamp(std::ceil(x1) + 2.0f, 0.0f, (float)W);
   const int ry0 = (int)std::clamp(std::floor(H - y1) - 2.0f, 0.0f, (float)H), ry1 = (int)std::clamp(std::ceil(H - y0) + 2.0f, 0.0f, (float)H);
   if (rx1 <= rx0 || ry1 <= ry0)
      return false;
   // Most of the frame anyway: one full pass is cheaper than a pass plus a copy.
   if ((double)(rx1 - rx0) * (ry1 - ry0) > 0.6 * (double)W * H)
      return true;
   a.rect[0] = rx0;
   a.rect[1] = ry0;
   a.rect[2] = rx1;
   a.rect[3] = ry1;
   return true;
}

void Scene2DNode::DrawLayer(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode)
{
   if (l.kind == Kind::Adjust && !mAdjReady)
   {
      // The picture below this layer, changed once; the fx path then draws it like any other layer.
      if (!RunAdjust(l, PairAt(depth).Tex()))
         return;
      mAdjReady = true;
      DrawLayer(l, depth, frameId, opacityMul, clipMode);
      mAdjReady = false;
      return;
   }
   if (l.AnyFx())
   {
      DrawFx(l, depth, frameId, opacityMul, clipMode);
      return;
   }
   if (l.HasEffects())
   {
      DrawEffects(l, depth, frameId, opacityMul, clipMode);
      return;
   }
   PassArgs a;
   a.bif = l.HasBlendIf() ? &l : nullptr;
   a.gamma = BlendGammaOf(l);
   a.opacity = l.opacity * l.fill * opacityMul;
   a.blend = l.kind == Kind::Group && l.blend == kPassThrough ? 0 : std::clamp(l.blend, 0, BlendModes::kContrastInvertMode);
   a.clipMode = clipMode;
   if (INode* mask = l.maskOn ? MaskSource(l) : nullptr)
   {
      mask->CookIfNeeded(frameId);
      a.mask = mask->GetOutputTexture();
      a.maskChannel = l.maskChannel;
      a.maskInvert = l.maskInvert;
      a.maskDensity = l.maskDensity;
   }
   if (l.kind == Kind::Group)
   {
      // Photoshop (PhotoCraft composite.rs): a Pass Through group below 100% fill or with Blend If renders isolated,
      // like Normal; otherwise its children land straight in the backdrop, and opacity or a mask mixes that result
      // back over what was below, so adjustments inside still see the layers beneath the group.
      const bool pass = l.blend == kPassThrough && !clipMode && l.fill >= 0.999f && a.bif == nullptr;
      if (pass && a.mask == 0 && l.opacity * opacityMul >= 0.999f)
      {
         DrawList(l.id, depth, frameId, 1.0f);
         return;
      }
      Pair& below = PairAt(depth);
      Pair& sub = PairAt(depth + 1);
      if (pass)
      {
         GLint prevR = 0, prevD = 0;
         glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
         glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, below.f[below.cur].fbo);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sub.f[sub.cur].fbo);
         glBlitFramebuffer(0, 0, mDoc.w, mDoc.h, 0, 0, mDoc.w, mDoc.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
      }
      else
      {
         const float clear[4] = { 0, 0, 0, 0 };
         Clear(sub, clear);
      }
      DrawList(l.id, depth + 1, frameId, 1.0f);
      a.tex = PairAt(depth + 1).Tex();
      a.texW = mDoc.w;
      a.texH = mDoc.h;
      a.box[0] = (float)mDoc.w;
      a.box[1] = (float)mDoc.h;
      a.srgb = false;   // already in working space
      a.opacity = l.opacity * l.fill * opacityMul;   // a group has no fill of its own: fill dims it like opacity
      Pass(depth, a);
      return;
   }
   if (l.kind == Kind::Fill)
   {
      static const int kFillKinds[7] = { 1, 2, 3, 5, 6, 7, 8 };   // solid, linear, radial, reflected, diamond, conical, bitmap
      a.kind = kFillKinds[std::clamp(l.fillType, 0, 6)];
      a.fill = &l;
      if (a.kind == 8)
      {
         a.tex = FillTexture(l.fillImage, a.texW, a.texH);
         if (a.tex == 0)
            a.kind = 1;   // no picture: the colour
      }
      a.box[0] = (float)mDoc.w;
      a.box[1] = (float)mDoc.h;
      // A fill layer's masks sit on the canvas (its box), like Photoshop's solid / gradient fill with a shape mask.
      if (l.HasPixelMask() && l.pmaskOn)
      {
         auto it = mMasks.find(l.id);
         if (it != mMasks.end())
            a.pmask = (mStroke.Active() && mPaintId == l.id && mPaintTarget == 1) ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
      }
      if (l.hasVMask && l.vmaskOn && !l.vmask.empty())
         if (Raster* r = SyncRaster(mVMasks, l, true))
         {
            a.vmask = r->tex;
            a.vmaskInvert = l.vmaskInvert;
            a.vmaskDensity = l.vmaskDensity;
         }
      Pass(depth, a);
      return;
   }
   if (l.kind == Kind::Paint)
   {
      auto it = mSurfaces.find(l.id);
      if (it == mSurfaces.end())
         return;
      const bool live = mStroke.Active() && mPaintId == l.id && mPaintTarget == 0;
      a.tex = live ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
      a.texW = it->second.fbo.w;
      a.texH = it->second.fbo.h;
      a.stable = !live;   // its surface holds still until the next stroke
      if (l.relief)
         if (const unsigned lit = ShadeRelief(l, a.tex, a.texW, a.texH))
         {
            a.tex = lit;
            a.stable = false;   // a shared scratch target
         }
   }
   else if (l.kind == Kind::Adjust)
   {
      a.tex = mAdjFbo.tex;
      a.texW = mAdjFbo.w;
      a.texH = mAdjFbo.h;
      a.srgb = false;   // already in working space
   }
   else if (l.kind == Kind::Vector || l.kind == Kind::Text)
   {
      Raster* r = SyncRaster(mVec, l, false);
      if (r == nullptr)
         return;
      a.tex = r->tex;
      a.texW = r->w;
      a.texH = r->h;
      a.stable = true;   // the layer's own raster
   }
   else if (dynamic_cast<IGeometrySource*>(Source(l)) != nullptr)
   {
      // Real 3D: the object drawn through the comp camera at document size, with its own depth.
      if (!GeometryRender(l, a.tex, a.geoDepth, a.geoNF, a.geoUv, a.rect))
         return;
      a.kind = 4;
      a.texW = mDoc.w;
      a.texH = mDoc.h;
      a.srgb = true;
      LayerBox(l, a.box[0], a.box[1]);
      float hg[9];
      LayerHomography(l, hg);   // masks still sit on the layer's box
      Invert3(hg, a.inv);
      a.card = l.is3d;
      if (l.HasPixelMask() && l.pmaskOn)
      {
         auto it = mMasks.find(l.id);
         if (it != mMasks.end())
            a.pmask = (mStroke.Active() && mPaintId == l.id && mPaintTarget == 1) ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
      }
      if (l.hasVMask && l.vmaskOn && !l.vmask.empty())
         if (Raster* r = SyncRaster(mVMasks, l, true))
         {
            a.vmask = r->tex;
            a.vmaskInvert = l.vmaskInvert;
            a.vmaskDensity = l.vmaskDensity;
         }
      Pass(depth, a);
      return;
   }
   else
   {
      INode* src = Source(l);
      if (src == nullptr)
         return;
      if (l.below)
      {
         if (!CookOnBelow(l, src, PairAt(depth).Tex()))
            return;
         a.srgb = false;   // it ran on the working-space picture
      }
      else
      {
         src->CookIfNeeded(frameId);
         a.stable = true;
      }
      a.tex = src->GetOutputTexture();
      a.texW = src->GetOutputWidth();
      a.texH = src->GetOutputHeight();
   }
   if (a.tex == 0 || a.texW <= 0 || a.texH <= 0)
      return;
   LayerBox(l, a.box[0], a.box[1]);
   float h[9];
   if (!LayerHomography(l, h))
      return;
   Invert3(h, a.inv);
   a.card = l.is3d && !l.warpOn;
   if (l.cropOn)
   {
      a.crop[0] = std::clamp(l.crop[0] / a.box[0], 0.0f, 1.0f);
      a.crop[1] = std::clamp(l.crop[1] / a.box[1], 0.0f, 1.0f);
      a.crop[2] = std::clamp(1.0f - l.crop[2] / a.box[0], a.crop[0], 1.0f);
      a.crop[3] = std::clamp(1.0f - l.crop[3] / a.box[1], a.crop[1], 1.0f);
   }
   a.nearest = l.nearest || l.aaMode == 2;
   // A paint layer shows only its painted tiles: crop to them (one texel wider, so filtering reads the same), which
   // lets the pass touch only that part of the frame.
   if (l.kind == Kind::Paint && !((mStroke.Active() || mArt.on) && mPaintId == l.id) && l.pw > 0 && l.ph > 0)
   {
      const int kT = TileStore::kTile, cols = (l.pw + kT - 1) / kT;
      int tx0 = l.pw, ty0 = l.ph, tx1 = 0, ty1 = 0;
      for (size_t i = 0; i < l.tiles.size(); i++)
         if (l.tiles[i] != "-")
         {
            const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
            tx0 = std::min(tx0, x0);
            ty0 = std::min(ty0, y0);
            tx1 = std::max(tx1, std::min(l.pw, x0 + kT));
            ty1 = std::max(ty1, std::min(l.ph, y0 + kT));
         }
      if (tx1 <= tx0 || ty1 <= ty0)
         return;   // nothing painted
      // Texture rows count from the bottom; the box's v from the top.
      const float u0 = (tx0 - 1.0f) / l.pw, u1 = (tx1 + 1.0f) / l.pw;
      const float v0 = 1.0f - (ty1 + 1.0f) / l.ph, v1 = 1.0f - (ty0 - 1.0f) / l.ph;
      a.crop[0] = std::max(a.crop[0], u0);
      a.crop[1] = std::max(a.crop[1], v0);
      a.crop[2] = std::min(a.crop[2], u1);
      a.crop[3] = std::min(a.crop[3], v1);
      if (a.crop[2] <= a.crop[0] || a.crop[3] <= a.crop[1])
         return;
   }
   // A vector layer shows only where its items are: crop to their bounds (miter tips and filtering padded),
   // same reason as paint above. A soft shape's fill reaches past its paths (Density shows it over the whole layer).
   if (l.kind == Kind::Vector && !l.shapes.empty() && l.pw > 0 && l.ph > 0 && !l.SoftShape())
   {
      float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
      for (const Shape& s : l.shapes)
      {
         float lo[2], hi[2];
         if (!ShapeBounds(s, lo, hi))
            continue;
         const float pad = (s.strokeOn ? s.width * 2.0f : 0.0f) + 3.0f;
         x0 = std::min(x0, lo[0] - pad);
         y0 = std::min(y0, lo[1] - pad);
         x1 = std::max(x1, hi[0] + pad);
         y1 = std::max(y1, hi[1] + pad);
      }
      if (x1 <= x0)
         return;   // nothing drawn
      a.crop[0] = std::max(a.crop[0], x0 / l.pw);
      a.crop[1] = std::max(a.crop[1], y0 / l.ph);
      a.crop[2] = std::min(a.crop[2], x1 / l.pw);
      a.crop[3] = std::min(a.crop[3], y1 / l.ph);
      if (a.crop[2] <= a.crop[0] || a.crop[3] <= a.crop[1])
         return;
   }
   if (l.HasPixelMask() && l.pmaskOn)
   {
      auto it = mMasks.find(l.id);
      if (it != mMasks.end())
      {
         const bool live = mStroke.Active() && mPaintId == l.id && mPaintTarget == 1;
         a.pmask = live ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
      }
   }
   if (l.hasVMask && l.vmaskOn && !l.vmask.empty())
      if (Raster* r = SyncRaster(mVMasks, l, true))
      {
         a.vmask = r->tex;
         a.vmaskInvert = l.vmaskInvert;
         a.vmaskDensity = l.vmaskDensity;
      }
   if (!LayerRect(h, a))
      return;
   Pass(depth, a);
}

namespace
{
   // Relief: the surface's alpha is height. The normal comes from a smoothed height slope (the surface is 8 bit),
   // the light from reliefLight / reliefElev, the look from the material.
   const char* kReliefFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSurf;\n"
      "uniform vec2 uTexel;\n"
      "uniform vec3 uLight;\n"
      "uniform float uDepth;\n"
      "uniform int uMat;\n"
      "float H(vec2 o) { return texture(uSurf, vUv + o * uTexel).a; }\n"
      "float Hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
      "void main() {\n"
      "   vec4 s = texture(uSurf, vUv);\n"
      "   float dx = (H(vec2(1, 1)) + 2.0 * H(vec2(1, 0)) + H(vec2(1, -1)) - H(vec2(-1, 1)) - 2.0 * H(vec2(-1, 0)) - H(vec2(-1, -1))) / 8.0;\n"
      "   float dy = (H(vec2(1, 1)) + 2.0 * H(vec2(0, 1)) + H(vec2(-1, 1)) - H(vec2(1, -1)) - 2.0 * H(vec2(0, -1)) - H(vec2(-1, -1))) / 8.0;\n"
      "   vec3 n = normalize(vec3(-dx * uDepth, -dy * uDepth, 1.0));\n"
      "   vec3 L = normalize(uLight);\n"
      "   vec3 Hv = normalize(L + vec3(0.0, 0.0, 1.0));\n"
      "   float diff = max(dot(n, L), 0.0), nh = max(dot(n, Hv), 0.0);\n"
      "   float cover = smoothstep(0.0, 0.08, s.a);\n"
      "   vec3 base = s.rgb, c;\n"
      "   if (uMat == 0) {\n"                 // plasticine: soft clay, a broad dull sheen
      "      c = base * (0.35 + 0.75 * diff) + vec3(0.22) * pow(nh, 18.0);\n"
      "   } else if (uMat == 1) {\n"          // metal: little diffuse, a tinted hard sheen
      "      c = base * (0.18 + 0.32 * diff) + mix(base, vec3(1.0), 0.35) * pow(nh, 42.0) * 1.3;\n"
      "   } else if (uMat == 2) {\n"          // chrome: mirrors a sky lit from the light's side
      "      vec3 r = reflect(vec3(0.0, 0.0, -1.0), n);\n"
      "      float t = dot(normalize(r.xy + 1e-4), normalize(L.xy + 1e-4)) * length(r.xy);\n"
      "      float sky = mix(0.06, 1.0, smoothstep(-0.25, 0.55, t)) - 0.35 * smoothstep(0.0, 0.08, abs(t - 0.2)) + 0.35;\n"
      "      c = mix(vec3(sky), base * sky, 0.25) + vec3(1.0) * pow(nh, 160.0);\n"
      "   } else {\n"                         // ink stipple: dots, denser where the light does not reach
      "      float lit = clamp(0.25 + 0.85 * diff, 0.0, 1.0);\n"
      "      vec2 cell = floor(gl_FragCoord.xy / 3.0);\n"
      "      vec2 f = fract(gl_FragCoord.xy / 3.0) - 0.5;\n"
      "      float dot1 = step(lit, Hash(cell)) * (1.0 - smoothstep(0.3, 0.5, length(f)));\n"
      "      fragColor = vec4(base, cover * dot1);\n"
      "      return;\n"
      "   }\n"
      "   fragColor = vec4(clamp(c, 0.0, 1.0), cover);\n"
      "}\n";
}

unsigned Scene2DNode::ShadeRelief(const Layer& l, unsigned tex, int w, int h)
{
   if (!mReliefTried)
   {
      mReliefTried = true;
      mReliefProgram = GLUtil::CompileProgram(kReliefFrag);
   }
   if (mReliefProgram == 0 || tex == 0 || w <= 0 || h <= 0)
      return 0;
   GLUtil::Fbo& out = mRelief[l.id];
   if (!GLUtil::EnsureFbo(out, w, h, GL_RGBA8, "Scene2DRelief"))
      return 0;
   const unsigned prog = mReliefProgram;
   const float az = l.reliefLight * 3.14159265f / 180.0f, el = std::clamp(l.reliefElev, 1.0f, 90.0f) * 3.14159265f / 180.0f;
   GLboolean blendOn = glIsEnabled(GL_BLEND);
   glDisable(GL_BLEND);
   GLUtil::RunShaderPass(out, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(GLUtil::UniformLoc(prog, "uSurf"), 0);
      glUniform2f(GLUtil::UniformLoc(prog, "uTexel"), 1.0f / w, 1.0f / h);
      glUniform3f(GLUtil::UniformLoc(prog, "uLight"), std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el));
      glUniform1f(GLUtil::UniformLoc(prog, "uDepth"), std::max(0.0f, l.reliefDepth));
      glUniform1i(GLUtil::UniformLoc(prog, "uMat"), std::clamp(l.reliefMat, 0, 3));
   });
   if (blendOn)
      glEnable(GL_BLEND);
   return out.tex;
}

bool Scene2DNode::RunAdjust(const Layer& l, unsigned src)
{
   if (!mAdjTried)
   {
      mAdjTried = true;
      mAdjProgram = GLUtil::CompileProgram(AdjustSrc().c_str());
   }
   if (mAdjProgram == 0 || src == 0)
      return false;
   const unsigned cube = l.adjType == kAdjLookup && BindLookup(l.adjFile) ? mLutTex : 0;
   const unsigned blurL = l.adjType == kAdjShadowsHighlights ? BlurLuma(src, AdjAmount(l)) : 0;
   bool lensWait = false;
   const unsigned depthTex = l.adjType == kAdjLensBlur ? LensDepthTex(l, src, lensWait) : 0;
   if (lensWait)
      return false;   // the picture below is being analysed: it shows unblurred until the depth lands
   if (!GLUtil::EnsureFbo(mAdjFbo, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2DAdj"))
      return false;
   const unsigned prog = mAdjProgram;
   GLUtil::RunShaderPass(mAdjFbo, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, src);
      glUniform1i(GLUtil::UniformLoc(prog, "uSrc"), 0);
      glUniform2f(GLUtil::UniformLoc(prog, "uDoc"), (float)mDoc.w, (float)mDoc.h);
      glUniform1i(GLUtil::UniformLoc(prog, "uType"), l.adjType);
      glUniform1f(GLUtil::UniformLoc(prog, "uAmt"), AdjAmount(l));
      glUniform1i(GLUtil::UniformLoc(prog, "uMap"), l.adjMap ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uGmDither"), l.adjDither ? 1 : 0);
      glUniform4fv(GLUtil::UniformLoc(prog, "uColA"), 1, l.colorA);
      glUniform4fv(GLUtil::UniformLoc(prog, "uColB"), 1, l.colorB);
      glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), mDoc.linear ? 1 : 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uHead"), mDoc.headroom ? 1 : 0);
      glUniform4fv(GLUtil::UniformLoc(prog, "uLevels"), 1, l.levels);
      float lut[256];
      CurveLut(l.curve, lut, 256);
      glUniform4fv(GLUtil::UniformLoc(prog, "uLut4"), 64, lut);
      float lutC[768];
      bool hueRanges = false;   // Hue/Saturation has no channel curves: its range tables ride in the same uniform
      if (l.adjType == kAdjHue)
         hueRanges = HueRangeTables(l.adjP, lutC);
      else
         for (int ch = 0; ch < 3; ch++)
            CurveLut(l.curveCh[ch], lutC + ch * 256, 256);
      glUniform4fv(GLUtil::UniformLoc(prog, "uLutC4"), 192, lutC);
      glUniform4fv(GLUtil::UniformLoc(prog, "uLvCh"), 3, &l.levelsCh[0][0]);
      float p[40] = {};
      const std::vector<float> def = AdjParamDefault(l.adjType);
      for (size_t i = 0; i < def.size() && i < 40; i++)
         p[i] = i < l.adjP.size() ? l.adjP[i] : def[i];
      if (l.adjType == kAdjHue)
         p[3] = hueRanges ? 1.0f : 0.0f;   // the shader reads the range tables only when one is set
      if (l.adjType == kAdjWhiteBalance)
         WhiteBalanceMatrix(p[0], p[1], p + 2);
      glUniform4fv(GLUtil::UniformLoc(prog, "uP4"), 10, p);
      StopUniforms(prog, l.stops);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_3D, cube);
      glUniform1i(GLUtil::UniformLoc(prog, "uCube"), 1);
      glUniform1f(GLUtil::UniformLoc(prog, "uCubeN"), cube != 0 ? (float)mLutSize : 0.0f);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, blurL);
      glUniform1i(GLUtil::UniformLoc(prog, "uBlurL"), 2);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, depthTex);
      glUniform1i(GLUtil::UniformLoc(prog, "uDepth"), 3);
      glUniform1i(GLUtil::UniformLoc(prog, "uHasDepth"), depthTex != 0 ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
   });
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_3D, 0);
   glActiveTexture(GL_TEXTURE0);
   return true;
}

bool Scene2DNode::sLensDepthAuto = true;

namespace
{
   unsigned UploadDepth(unsigned tex, const std::vector<float>& topDown, int w, int h)
   {
      std::vector<float> rows((size_t)w * h);   // textures run bottom row first
      for (int y = 0; y < h; y++)
         std::memcpy(&rows[(size_t)y * w], &topDown[(size_t)(h - 1 - y) * w], (size_t)w * sizeof(float));
      if (tex == 0)
         glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, rows.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      return tex;
   }
}

unsigned Scene2DNode::LensDepthTex(const Layer& l, unsigned src, bool& wait)
{
   wait = false;
   const int W = mDoc.w, H = mDoc.h;
   if (l.id <= 0)
      return 0;   // an effect copy: the even blur
   auto it = mLensDepth.find(l.id);
   if (it != mLensDepth.end() && it->second.tex != 0 && it->second.w == W && it->second.h == H)
      return it->second.tex;
   if (!sLensDepthAuto || !AiSelect::DepthAvailable())
      return 0;
   LensDepth& d = mLensDepth[l.id];
   if (d.failed)
      return 0;
   if (!d.pending)
   {
      std::vector<uint8_t> px((size_t)W * H * 4);
      glBindTexture(GL_TEXTURE_2D, src);
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      auto pic = std::make_shared<AiSelect::Picture>();   // the model takes the picture top row first
      pic->w = W;
      pic->h = H;
      pic->rgba.resize(px.size());
      for (int y = 0; y < H; y++)
         std::memcpy(&pic->rgba[(size_t)y * W * 4], &px[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
      d.key = AiSelect::KeyOf(*pic) | 1;
      d.pending = true;
      if (!AiSelect::DepthReady(d.key))
         AiSelect::RequestDepth(pic, d.key);
   }
   if (!AiSelect::DepthReady(d.key))
   {
      if (AiSelect::Busy())
      {
         wait = true;
         return 0;
      }
      d.pending = false;   // the model failed: the even blur, until Re-analyse
      d.failed = true;
      return 0;
   }
   std::vector<float> map;
   int dw = 0, dh = 0;
   d.pending = false;
   if (!AiSelect::Depth(d.key, map, dw, dh) || dw != W || dh != H)
   {
      d.failed = true;
      return 0;
   }
   d.tex = UploadDepth(d.tex, map, W, H);
   d.w = W;
   d.h = H;
   return d.tex;
}

bool Scene2DNode::LensDepthPoll()
{
   bool landed = false;
   for (auto& kv : mLensDepth)
   {
      LensDepth& d = kv.second;
      if (!d.pending)
         continue;
      if (!AiSelect::DepthReady(d.key))
      {
         if (!AiSelect::Busy())
         {
            d.pending = false;
            d.failed = true;
            landed = true;   // redraw with the even blur
         }
         continue;
      }
      std::vector<float> map;
      int dw = 0, dh = 0;
      d.pending = false;
      landed = true;
      if (!AiSelect::Depth(d.key, map, dw, dh) || dw != mDoc.w || dh != mDoc.h)
      {
         d.failed = true;
         continue;
      }
      d.tex = UploadDepth(d.tex, map, dw, dh);
      d.w = dw;
      d.h = dh;
   }
   return landed;
}

void Scene2DNode::LensDepthReset(int layerId)
{
   auto it = mLensDepth.find(layerId);
   if (it == mLensDepth.end())
      return;
   if (it->second.tex != 0)
      glDeleteTextures(1, &it->second.tex);
   mLensDepth.erase(it);
}

int Scene2DNode::LensDepthState(int layerId) const
{
   auto it = mLensDepth.find(layerId);
   if (it != mLensDepth.end() && it->second.tex != 0)
      return 1;
   if (!AiSelect::DepthAvailable() || (it != mLensDepth.end() && it->second.failed))
      return -1;
   return 0;
}

void Scene2DNode::SetLensDepth(int layerId, const std::vector<float>& topDown)
{
   if (topDown.size() != (size_t)mDoc.w * mDoc.h)
      return;
   LensDepth& d = mLensDepth[layerId];
   d.tex = UploadDepth(d.tex, topDown, mDoc.w, mDoc.h);
   d.w = mDoc.w;
   d.h = mDoc.h;
   d.pending = d.failed = false;
}

std::function<ImageCable*(uint64_t)> Scene2DNode::ImageInputOf;

namespace
{
   // The picture under an effect layer, handed to its node as if a cable brought it.
   class BelowPicture : public INode
   {
   public:
      unsigned tex = 0;
      int w = 0, h = 0;
      unsigned long long rev = 0;
      unsigned int GetOutputTexture() override { return tex; }
      int GetOutputWidth() const override { return w; }
      int GetOutputHeight() const override { return h; }
      void CookIfNeeded(int) override {}
      unsigned long long TextureRevision() const override { return rev; }
   };
}

bool Scene2DNode::CookOnBelow(const Layer& l, INode* src, unsigned below)
{
   ImageCable* in = ImageInputOf ? ImageInputOf(l.uid) : nullptr;
   if (in == nullptr || below == 0 || src != (ResolveUid ? ResolveUid(l.uid) : nullptr))
      return false;
   static BelowPicture pic;
   static int fxFrame = -2000000000;   // its own frame ids: the canvas already cooked this node on its own input
   pic.tex = below;
   pic.w = mDoc.w;
   pic.h = mDoc.h;
   pic.rev = NextTextureRevision();
   INode* was = in->GetSource();
   const int wasOut = in->GetSourceOutput();
   in->Connect(&pic, 0);
   src->CookIfNeeded(++fxFrame);
   if (was != nullptr)
      in->Connect(was, wasOut);
   else
      in->Disconnect();
   return src->GetOutputTexture() != 0;
}

bool Scene2DNode::sBelowCache = true;
bool gScene2DBounds = true;   // tests turn it off to compare with full-frame passes

namespace
{
   bool IsCard(const Scene2DNode::Layer& l)
   {
      using K = Scene2DNode::Kind;
      return l.is3d && !l.clip && l.kind != K::Group && l.kind != K::Fill && l.kind != K::Adjust;
   }

   void Blit(unsigned src, unsigned dst, int w, int h)
   {
      GLint prevR = 0, prevD = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevR);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevD);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
      glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevR);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevD);
   }
}

// One stamp per top-level entry (the layer and everything inside it): its saved lines, what its sources hand back,
// and the shared state it reads. `global` takes the lines that touch every layer.
void Scene2DNode::TopHashes(const std::vector<const Layer*>& kids, std::vector<unsigned long long>& out, unsigned long long& global)
{
   if (mTopHashLayerCount != mLayers.size() || mTopHashText != mText)
   {
   std::unordered_map<int, unsigned long long>& lines = mTopHashLines;
   lines.clear();
   global = 1469598103934665603ull;
   size_t pos = 0;
   while (pos < mText.size())
   {
      size_t eol = mText.find('\n', pos);
      if (eol == std::string::npos)
         eol = mText.size();
      const size_t sp = mText.find(' ', pos);
      const std::string key = mText.substr(pos, (sp == std::string::npos || sp > eol ? eol : sp) - pos);
      int id = 0;
      const bool perLayer = key != "doc" && key != "next" && key != "panel" && key != "cam" && key != "sym" && key != "brushsrc" && key != "brushlive" && sp != std::string::npos
                            && sp < eol && std::sscanf(mText.c_str() + sp + 1, "%d", &id) == 1 && FindConst(id) != nullptr;
      if (key != "next" && key != "guidesx" && key != "guidesy")
      {
         unsigned long long& h = perLayer ? lines[id] : global;
         for (size_t k = pos; k < eol; k++)
            h = Mix(h, (unsigned char)mText[k]);
         h = Mix(h, '\n');
      }
      pos = eol + 1;
   }
   mTopHashDoc = global;
   mTopHashText = mText;
   mTopHashLayerCount = mLayers.size();
   }
   global = mTopHashDoc;
   global = Mix(global, (unsigned long long)soloLayer);
   const CompCam cam = Camera();
   out.assign(kids.size(), 0);
   for (size_t i = 0; i < kids.size(); i++)
   {
      unsigned long long h = 1469598103934665603ull;
      for (const Layer& l : mLayers)
      {
         if (!IsInside(l.id, kids[i]->id))
            continue;
         const auto lineIt = mTopHashLines.find(l.id);
         h = Mix(Mix(h, (unsigned long long)l.id), lineIt == mTopHashLines.end() ? 0ull : lineIt->second);
         h = Mix(h, EffectsHash(l, -1));
         for (INode* n : { Source(l), l.maskOn ? MaskSource(l) : nullptr })
            if (n != nullptr)
            {
               h = Mix(h, (unsigned long long)(uintptr_t)n);
               if (auto* geo = dynamic_cast<IGeometrySource*>(n))
                  h = Mix(Mix(h, geo->MeshRevision()), geo->MaterialRevision());
               h = Mix(h, n->TextureRevision());
               h = Mix(h, (unsigned long long)n->GetOutputWidth() * 65536ull + (unsigned long long)n->GetOutputHeight());
            }
         if (l.kind == Kind::Paint || l.kind == Kind::Vector || l.kind == Kind::Text || l.HasPixelMask() || l.HasFilterMask() || l.HasSkin())
            h = Mix(h, mPaintEdits);
         if (mStroke.Active() && mPaintId == l.id)
            h = Mix(h, (unsigned long long)mStroke.DabCount() + 1ull);
         if (l.kind == Kind::Adjust)
            for (float v : drive)
            {
               unsigned bits = 0;
               std::memcpy(&bits, &v, sizeof bits);
               h = Mix(h, bits);
            }
         if (l.is3d || dynamic_cast<IGeometrySource*>(Source(l)) != nullptr)
            for (float v : { cam.zoom, cam.pos[0], cam.pos[1], cam.pos[2], cam.rot[0], cam.rot[1], cam.rot[2], cam.rot[4], cam.rot[8] })
            {
               unsigned bits = 0;
               std::memcpy(&bits, &v, sizeof bits);
               h = Mix(h, bits);
            }
      }
      out[i] = h;
   }
}

// What must hold for a picture saved at the start of entry `at` to be reused: everything below it unchanged, and
// the entry itself still starting a new step of DrawList (clip chains and 3D runs end where they ended).
unsigned long long Scene2DNode::BelowKey(const std::vector<const Layer*>& kids, const std::vector<unsigned long long>& hashes,
                                         size_t at, unsigned long long global)
{
   unsigned long long k = Mix(global, (unsigned long long)at);
   for (size_t i = 0; i < at && i < hashes.size(); i++)
      k = Mix(k, hashes[i]);
   if (at < kids.size())
   {
      k = Mix(k, (unsigned long long)kids[at]->clip * 2ull + (IsCard(*kids[at]) ? 1ull : 0ull));
      k = Mix(k, at + 1 < kids.size() && kids[at + 1]->clip ? 7ull : 3ull);
   }
   return k;
}

void Scene2DNode::DrawList(int parent, int depth, int frameId, float opacityMul)
{
   const std::vector<const Layer*> kids = ListOf(parent);
   auto card = [](const Layer& l) { return IsCard(l); };
   // Top level: resume from the below cache when nothing under the edited entry changed.
   const bool top = parent == 0 && depth == 0 && sBelowCache && mDepthLevel < 0 && !mIsolating && !sExport;
   std::vector<unsigned long long> hashes;
   unsigned long long global = 0;
   size_t start = 0, changed = 0;
   if (top)
   {
      TopHashes(kids, hashes, global);
      if (global == mGlobalHash && hashes.size() == mKidHash.size())
      {
         changed = hashes.size();
         for (size_t i = 0; i < hashes.size(); i++)
            if (hashes[i] != mKidHash[i])
            {
               changed = i;
               break;
            }
         if (changed == hashes.size())
            changed = 0;   // the stamp moved for a reason no entry shows: draw it all, save nothing
      }
      else if (global == mGlobalHash)
      {
         // Layers added or removed: the first entry that differs.
         while (changed < hashes.size() && changed < mKidHash.size() && hashes[changed] == mKidHash[changed])
            changed++;
      }
      const bool sized = GLUtil::EnsureFbo(mBelow, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D below");
      if (!sized)
         mBelowAt = 0;
      if (mBelowAt > 0 && mBelowAt <= changed && mBelowAt <= kids.size() && mBelowKey == BelowKey(kids, hashes, mBelowAt, global))
      {
         Pair& root = PairAt(0);
         Blit(mBelow.fbo, root.f[root.cur].fbo, mDoc.w, mDoc.h);
         start = mBelowAt;
      }
      else
         mBelowAt = 0;
      mGlobalHash = global;
      mKidHash = hashes;
   }
   for (size_t i = start; i < kids.size(); i++)
   {
      // Save the picture so far at each step start up to the edited entry; the last one saved is where the next
      // frame resumes.
      if (top && i > mBelowAt && i <= changed)
      {
         Pair& root = PairAt(0);
         Blit(root.f[root.cur].fbo, mBelow.fbo, mDoc.w, mDoc.h);
         mBelowAt = i;
         mBelowKey = BelowKey(kids, hashes, i, global);
      }
      const Layer& base = *kids[i];
      // A clipped layer with nothing unclipped below it in its list is drawn as it is.
      size_t end = i + 1;
      while (end < kids.size() && kids[end]->clip)
         end++;
      // Neighbouring 3D cards (none with clipped layers) draw as one run: far first, nearer ones hide them.
      if (card(base) && end == i + 1)
      {
         std::vector<const Layer*> run;
         size_t j = i;
         while (j < kids.size() && card(*kids[j]) && !(j + 1 < kids.size() && kids[j + 1]->clip))
         {
            if (Shown(*kids[j]))
               run.push_back(kids[j]);
            j++;
         }
         if (run.size() >= 2)
         {
            std::vector<std::pair<float, const Layer*>> order;
            for (const Layer* r : run)
               order.push_back({ CardDepth(*r), r });
            std::stable_sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
            BeginDepth(depth);
            for (const auto& o : order)
               DrawLayer(*o.second, depth, frameId, opacityMul, false);
            EndDepth(depth);
         }
         else if (!run.empty())
            DrawLayer(*run[0], depth, frameId, opacityMul, false);
         i = j - 1;
         continue;
      }
      if (!Shown(base))
      {
         if (!base.clip)
            i = end - 1;   // its clipped layers go with it
         continue;
      }
      bool anyClipped = false;
      for (size_t j = i + 1; j < end; j++)
         anyClipped = anyClipped || Shown(*kids[j]);
      if (!anyClipped || base.clip)
      {
         DrawLayer(base, depth, frameId, opacityMul, false);
         i = base.clip ? i : end - 1;
         continue;
      }
      // Base and its clipped layers together, then onto what is below with the base's blend and opacity.
      Pair& sub = PairAt(depth + 1);
      const float clear[4] = { 0, 0, 0, 0 };
      Clear(sub, clear);
      Layer plain = base;
      plain.blend = base.kind == Kind::Group ? kPassThrough : 0;
      plain.opacity = 1.0f;
      DrawLayer(plain, depth + 1, frameId, 1.0f, false);
      for (size_t j = i + 1; j < end; j++)
         if (Shown(*kids[j]))
            DrawLayer(*kids[j], depth + 1, frameId, 1.0f, true);
      PassArgs a;
      a.tex = PairAt(depth + 1).Tex();
      a.texW = mDoc.w;
      a.texH = mDoc.h;
      a.box[0] = (float)mDoc.w;
      a.box[1] = (float)mDoc.h;
      a.srgb = false;
      a.blend = base.kind == Kind::Group && base.blend == kPassThrough ? 0 : std::clamp(base.blend, 0, BlendModes::kContrastInvertMode);
      a.opacity = base.opacity * opacityMul;
      Pass(depth, a);
      i = end - 1;
   }
}

std::vector<int> Scene2DNode::OutputLayers() const
{
   std::vector<int> ids;
   for (const Layer& l : mLayers)
      if (l.output)
         ids.push_back(l.id);
   std::sort(ids.begin(), ids.end());
   return ids;
}

int Scene2DNode::OutputCount() const
{
   return 1 + (int)OutputLayers().size();
}

const char* Scene2DNode::OutputLabel(int index) const
{
   const std::vector<int> ids = OutputLayers();
   if (index <= 0 || index > (int)ids.size())
      return "out";
   const Layer* l = FindConst(ids[index - 1]);
   const std::string want = l != nullptr && !l->name.empty() ? l->name : "layer " + std::to_string(ids[index - 1]);
   std::string& slot = mOutLabels[ids[index - 1]];
   if (slot != want)
      slot = want;
   return slot.c_str();
}

unsigned int Scene2DNode::GetOutputTexture(int index)
{
   if (index <= 0)
      return GetOutputTexture();
   const std::vector<int> ids = OutputLayers();
   if (index > (int)ids.size())
      return 0;
   auto it = mLayerOut.find(ids[index - 1]);
   return it != mLayerOut.end() ? GLUtil::FboTexture(it->second) : 0;
}

void Scene2DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   // Re-entrancy guard: if a legacy patch has a cycle through this 2D Document, break it cleanly instead of
   // recursing or re-cooking infinitely.
   static std::unordered_set<const Scene2DNode*> sCooking;
   if (!sCooking.insert(this).second)
      return;
   struct CookGuard
   {
      const Scene2DNode* self;
      ~CookGuard() { sCooking.erase(self); }
   } cookGuard{ this };
   mLastCookFrame = frameId;
   Layers();
   if (bypassed)
      return;
   const auto start = std::chrono::steady_clock::now();

   // Cook every shown source first; what they hand back decides whether the picture changes.
   unsigned long long stamp = 1469598103934665603ull;
   for (char c : mText)
      stamp = Mix(stamp, (unsigned char)c);
   stamp = Mix(stamp, (unsigned long long)soloLayer);
   stamp = Mix(stamp, sExport ? 1ull : 0ull);
   SyncSurfaces();
   SyncRasters();
   // A stroke changes the picture before it changes the text; its queued dabs land now.
   if (mStroke.Active() && mDoc.brushLive)
   {
      BrushEngine::Brush b;
      ApplyBrushSources(b, false);   // live: the node as it is this frame
      mStroke.SetSources(b.tipTex, b.grainTex, b.grainTexW, b.grainTexH, b.colorTex);
   }
   if (mStroke.Active())
      mStroke.Flush();
   stamp = Mix(stamp, mStroke.Active() ? (unsigned long long)mStroke.DabCount() + 1ull : 0ull);
   stamp = Mix(stamp, mPaintEdits);
   for (const Layer& l : mLayers)
   {
      if (!Shown(l) && !l.output)
         continue;
      stamp = Mix(stamp, EffectsHash(l, frameId));
      for (INode* n : { Source(l), l.maskOn ? MaskSource(l) : nullptr })
         if (n != nullptr)
         {
            if (l.below && n == Source(l))
            {
               stamp = Mix(stamp, NextTextureRevision());   // its params live on the node: redraw each frame it shows
               continue;   // cooked in the composite, on the picture under it
            }
            n->CookIfNeeded(frameId);
            stamp = Mix(stamp, (unsigned long long)(uintptr_t)n);
            if (auto* geo = dynamic_cast<IGeometrySource*>(n))
               stamp = Mix(Mix(stamp, geo->MeshRevision()), geo->MaterialRevision());
            stamp = Mix(stamp, n->TextureRevision());
            stamp = Mix(stamp, (unsigned long long)n->GetOutputWidth() * 65536ull + (unsigned long long)n->GetOutputHeight());
         }
   }
   // Bound layer numbers: a modulator writes them straight into the layers, not the text.
   for (const ModSlot& m : mDoc.slots)
      if (Layer* l = FindLayer(m.layer))
         if (const float* v = LayerFieldPtr(*l, m.field))
         {
            unsigned bits = 0;
            std::memcpy(&bits, v, sizeof bits);
            stamp = Mix(stamp, bits);
         }
   // Drive A..D move adjustment layers.
   for (float v : drive)
   {
      unsigned bits = 0;
      std::memcpy(&bits, &v, sizeof bits);
      stamp = Mix(stamp, bits);
   }
   // The comp camera moves 3D cards and turns geometry.
   const CompCam cam = Camera();
   for (float v : { cam.zoom, cam.pos[0], cam.pos[1], cam.pos[2], cam.rot[0], cam.rot[1], cam.rot[2], cam.rot[4], cam.rot[8] })
   {
      unsigned bits = 0;
      std::memcpy(&bits, &v, sizeof bits);
      stamp = Mix(stamp, bits);
   }
   for (auto it = mGeo.begin(); it != mGeo.end();)
   {
      const Layer* l = FindConst(it->first);
      it = l == nullptr || dynamic_cast<IGeometrySource*>(Source(*l)) == nullptr ? mGeo.erase(it) : std::next(it);
   }
   // A relief target lives only while its layer is a relief.
   for (auto it = mRelief.begin(); it != mRelief.end();)
   {
      const Layer* l = FindConst(it->first);
      if (l == nullptr || !l->relief)
      {
         GLUtil::RetireFbo(it->second);
         it = mRelief.erase(it);
      }
      else
         ++it;
   }
   if (stamp == mStamp && mOut.tex != 0 && mOut.w == mDoc.w && mOut.h == mDoc.h)
      return;
   mStamp = stamp;

   if (!EnsureShaders())
      return;
   if (!GLUtil::EnsureFbo(mOut, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D"))
      return;
   Pair& root = PairAt(0);
   float bg[4] = { 0, 0, 0, 0 };
   if (mDoc.background != 3)
   {
      const float white[3] = { 1, 1, 1 }, black[3] = { 0, 0, 0 };
      const float* c = mDoc.background == 0 ? white : mDoc.background == 1 ? black : mDoc.bgColor;
      for (int i = 0; i < 3; i++)
         bg[i] = mDoc.linear ? SrgbToLinear(c[i]) : c[i];
      bg[3] = 1.0f;
   }
   Clear(root, bg);
   mAccOpaque = bg[3] >= 1.0f;
   for (const Layer& l : mLayers)
      if (l.blend == BlendModes::kEraseMode || l.blend == BlendModes::kAntiEraseMode)
         mAccOpaque = false;
   DrawList(0, 0, frameId, 1.0f);

   const unsigned int acc = PairAt(0).Tex();
   const unsigned int prog = mFinishProgram;
   GLUtil::RunShaderPass(mOut, prog, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, acc);
      glUniform1i(GLUtil::UniformLoc(prog, "uAcc"), 0);
      glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), mDoc.linear ? 1 : 0);
      GLUtil::SetImageHeadroom(prog, mDoc.headroom);
   });
   // Output layers: each drawn alone on transparent, shown even when hidden in the comp.
   const std::vector<int> outs = OutputLayers();
   for (auto it = mLayerOut.begin(); it != mLayerOut.end();)
      if (std::find(outs.begin(), outs.end(), it->first) == outs.end())
      {
         GLUtil::RetireFbo(it->second);
         it = mLayerOut.erase(it);
      }
      else
         ++it;
   for (int id : outs)
   {
      Layer* t = FindLayer(id);
      GLUtil::Fbo& f = mLayerOut[id];
      if (t == nullptr || !GLUtil::EnsureFbo(f, mDoc.w, mDoc.h, GLUtil::ImageWorkingFormat(), "Scene2D layer out"))
         continue;
      const int keepSolo = soloLayer;
      const bool keepVis = t->visible;
      soloLayer = id;
      t->visible = true;
      mIsolating = true;
      const bool keepOpaque = mAccOpaque;
      mAccOpaque = false;
      const float none[4] = { 0, 0, 0, 0 };
      Clear(PairAt(0), none);
      DrawList(0, 0, frameId, 1.0f);
      soloLayer = keepSolo;
      if (Layer* back = FindLayer(id))
         back->visible = keepVis;
      mIsolating = false;
      mAccOpaque = keepOpaque;
      const unsigned int one = PairAt(0).Tex();
      GLUtil::RunShaderPass(f, prog, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, one);
         glUniform1i(GLUtil::UniformLoc(prog, "uAcc"), 0);
         glUniform1i(GLUtil::UniformLoc(prog, "uLinear"), mDoc.linear ? 1 : 0);
         GLUtil::SetImageHeadroom(prog, mDoc.headroom);
      });
   }
   mRevision++;
   mCookMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

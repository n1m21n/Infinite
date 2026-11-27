#include "FilterDefs.h"

namespace
{
   using T = FilterParamDef::Type;

   // UI labels are lowercase app-wide; the stored key is uniformName, so this never touches a saved patch.
   std::string Lower(const char* s)
   {
      std::string out(s);
      for (char& c : out)
         if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
      return out;
   }

   FilterParamDef E(const char* label, const char* uniform,
                    std::vector<std::string> options, int defaultIndex = 0)
   {
      FilterParamDef p;
      p.label = Lower(label);
      p.uniformName = uniform;
      p.type = FilterParamDef::Type::Enum;
      p.minVal = 0.0f;
      p.maxVal = (float)(options.size() - 1);
      p.defaultVal[0] = (float)defaultIndex;
      p.options = std::move(options);
      return p;
   }

   FilterParamDef P(const char* label, const char* uniform, T type, float minV, float maxV, float def0, float def1 = 0, float def2 = 0, bool isDegrees = false)
   {
      FilterParamDef p;
      p.label = Lower(label);
      p.uniformName = uniform;
      p.type = type;
      p.minVal = minV;
      p.maxVal = maxV;
      p.defaultVal[0] = def0;
      p.defaultVal[1] = def1;
      p.defaultVal[2] = def2;
      p.isDegrees = isDegrees;
      return p;
   }

   // Display format (carries the unit) and whole-number snapping; storage is untouched.
   // "Linear light": the filter decodes sRGB to linear light before its
   // maths and encodes after (Nuke/Resolve/AE 32-bit). On for a new node;
   // FilterNode::BeforeLoadParams turns it off for patches saved before it
   // existed, so their look is unchanged. Always the LAST param of a def, so
   // existing modulation indices stay put. Bodies sample with ImgSrc().
   // Marks a param added after patches were saved: those patches load it at
   // `legacy` (the old behaviour); a new node gets the param's default.
   FilterParamDef OLD(float legacy, FilterParamDef p)
   {
      p.hasLegacy = true;
      p.legacyVal = legacy;
      return p;
   }

   // Relief filters: read colour premultiplied, so transparent pixels are
   // height 0 instead of whatever colour is hidden under alpha 0.
   FilterParamDef PREMUL()
   {
      return OLD(0.0f, P("Premultiplied", "uPremul", FilterParamDef::Type::Bool, 0.0f, 1.0f, 1.0f));
   }

   // Linear light also makes multi-tap filters average premultiplied colour
   // (ImgTap / ImgUnpremul), so colour hidden under alpha 0 cannot bleed in.
   FilterParamDef LIN()
   {
      return OLD(0.0f, P("Linear light", "uLinear", FilterParamDef::Type::Bool, 0.0f, 1.0f, 1.0f));
   }

   FilterParamDef F(const char* fmt, FilterParamDef p)
   {
      p.format = fmt;
      return p;
   }

   FilterParamDef N(const char* fmt, FilterParamDef p)
   {
      p.format = fmt;
      p.integer = true;
      return p;
   }

   // Tags a param with a section header, drawn above it in the params panel -
   // for filters whose param list is long enough that a flat list stops being
   // readable (e.g. a combined color-adjustments node).
   FilterParamDef S(const char* section, FilterParamDef p)
   {
      p.sectionLabel = Lower(section);
      return p;
   }

   // Shared helpers available to every filter body (hue/sat conversions used by HSL).
   const char* kSharedHelpers =
      "vec3 rgb2hsv(vec3 c) {\n"
      "   vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);\n"
      "   vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n"
      "   vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n"
      "   float d = q.x - min(q.w, q.y);\n"
      "   float e = 1.0e-10;\n"
      "   return vec3(abs(q.z + (q.w - q.y) / (6.0*d+e)), d/(q.x+e), q.x);\n"
      "}\n"
      "vec3 hsv2rgb(vec3 c) {\n"
      "   vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);\n"
      "   vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
      "   return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
      "}\n";
}

const std::vector<FilterDef>& GetFilterDefs()
{
   static const std::vector<FilterDef> kDefs = {
      // ---------------- Effects: blur family ----------------
      // The blur family is separable: exp(-(x²+y²)/k) = exp(-x²/k)·exp(-y²/k),
      // so each 2D kernel runs as a horizontal prePassBody into a 16F
      // intermediate (uPass) and a vertical main pass - 2N texture reads per
      // pixel instead of N², same result up to 16F rounding. Bloom's
      // bright-pass is applied per sample before weighting, so it splits the
      // same way (exact at integer Radius, where every tap is a texel centre).
      // quality: Classic keeps the original nine taps spaced `radius` px apart
      // (saved looks unchanged). Smooth walks every pixel out to 3 sigma (6 x radius, the slider is sigma/2) with
      // the same gaussian, so a large radius blurs instead of ghosting.
      // Smooth (the default for new nodes) walks every pixel, so there are no
      // holes between taps. Its vertical pass reads the linear 16F
      // intermediate in pairs: one bilinear fetch at the weighted midpoint of
      // two texels gives both their weighted sum (exact, since the pass is
      // already linear), which halves the reads. The horizontal pass must
      // read texel by texel: bilinear on sRGB-encoded input would mix before
      // decoding.
      { "gaussianblur", "Effects",
        "uniform float uRadius;\n"
        "uniform int uQuality;\n"
        "void main() {\n"
        "   vec4 sum = vec4(0.0); float total = 0.0;\n"
        "   if (uQuality == 0) {\n"
        "      for (int y = -4; y <= 4; y++) {\n"
        "         float w = exp(-float(y*y) / 8.0);\n"
        "         sum += texture(uPass, vUv + vec2(0.0, float(y) * uTexelSize.y * uRadius)) * w; total += w;\n"
        "      }\n"
        "   } else {\n"
        "      float r = max(uRadius, 0.001);\n"
        "      int n = min(int(ceil(6.0 * r)), 60);\n"
        "      sum = texture(uPass, vUv); total = 1.0;\n"
        "      for (int y = 1; y <= n; y += 2) {\n"
        "         float d1 = float(y) / r, d2 = float(y + 1) / r;\n"
        "         float w1 = exp(-d1 * d1 / 8.0);\n"
        "         float w2 = y + 1 <= n ? exp(-d2 * d2 / 8.0) : 0.0;\n"
        "         float w = w1 + w2;\n"
        "         if (w < 1e-6) break;\n"
        "         vec2 off = vec2(0.0, (float(y) * w1 + float(y + 1) * w2) / w * uTexelSize.y);\n"
        "         sum += (texture(uPass, vUv + off) + texture(uPass, vUv - off)) * w; total += 2.0 * w;\n"
        "      }\n"
        "   }\n"
        "   fragColor = ImgUnpremul(sum / total);\n"
        "}\n",
        { F("%.1f px", P("Radius", "uRadius", T::Float, 0.0f, 10.0f, 2.0f)),
          OLD(0.0f, E("Quality", "uQuality", { "Classic", "Smooth" }, 1)),
          LIN() }, 1,
        "uniform float uRadius;\n"
        "uniform int uQuality;\n"
        "void main() {\n"
        "   vec4 sum = vec4(0.0); float total = 0.0;\n"
        "   if (uConverted == 1) {\n"
        "      float r = max(uRadius, 0.001);\n"
        "      int n = min(int(ceil(6.0 * r)), 60);\n"
        "      sum = texture(uSrc, vUv); total = 1.0;\n"
        "      for (int x = 1; x <= n; x += 2) {\n"
        "         float d1 = float(x) / r, d2 = float(x + 1) / r;\n"
        "         float w1 = exp(-d1 * d1 / 8.0);\n"
        "         float w2 = x + 1 <= n ? exp(-d2 * d2 / 8.0) : 0.0;\n"
        "         float w = w1 + w2;\n"
        "         if (w < 1e-6) break;\n"
        "         vec2 off = vec2((float(x) * w1 + float(x + 1) * w2) / w * uTexelSize.x, 0.0);\n"
        "         sum += (texture(uSrc, vUv + off) + texture(uSrc, vUv - off)) * w; total += 2.0 * w;\n"
        "      }\n"
        "      fragColor = sum / total;\n"
        "      return;\n"
        "   }\n"
        "   if (uQuality == 0) {\n"
        "      for (int x = -4; x <= 4; x++) {\n"
        "         float w = exp(-float(x*x) / 8.0);\n"
        "         sum += ImgTap(vUv + vec2(float(x) * uTexelSize.x * uRadius, 0.0)) * w; total += w;\n"
        "      }\n"
        "   } else {\n"
        "      float r = max(uRadius, 0.001);\n"
        "      int n = min(int(ceil(6.0 * r)), 60);\n"
        "      for (int x = -n; x <= n; x++) {\n"
        "         float d = float(x) / r;\n"
        "         float w = exp(-d * d / 8.0);\n"
        "         sum += ImgTap(vUv + vec2(float(x) * uTexelSize.x, 0.0)) * w; total += w;\n"
        "      }\n"
        "   }\n"
        "   fragColor = sum / total;\n"
        "}\n", true },

      // quality: Classic is the original nine taps `radius` px apart (patches
      // saved before Quality existed load with it). Smooth averages every
      // pixel out to the same reach, 4 x radius, with paired vertical reads.
      { "boxblur", "Effects",
        "uniform float uRadius;\n"
        "uniform int uQuality;\n"
        "void main() {\n"
        "   vec4 sum = vec4(0.0);\n"
        "   if (uQuality == 0) {\n"
        "      for (int y = -4; y <= 4; y++)\n"
        "         sum += texture(uPass, vUv + vec2(0.0, float(y) * uTexelSize.y * uRadius));\n"
        "      fragColor = ImgUnpremul(sum / 9.0);\n"
        "      return;\n"
        "   }\n"
        "   int n = min(int(floor(4.0 * uRadius + 0.5)), 40);\n"
        "   sum = texture(uPass, vUv);\n"
        "   for (int y = 1; y <= n; y += 2) {\n"
        "      float w = y + 1 <= n ? 2.0 : 1.0;\n"
        "      vec2 off = vec2(0.0, (w > 1.5 ? float(y) + 0.5 : float(y)) * uTexelSize.y);\n"
        "      sum += (texture(uPass, vUv + off) + texture(uPass, vUv - off)) * w;\n"
        "   }\n"
        "   fragColor = ImgUnpremul(sum / float(2 * n + 1));\n"
        "}\n",
        { F("%.1f px", P("Radius", "uRadius", T::Float, 0.0f, 10.0f, 2.0f)), LIN(),
          OLD(0.0f, E("Quality", "uQuality", { "Classic", "Smooth" }, 1)) }, 1,
        "uniform float uRadius;\n"
        "uniform int uQuality;\n"
        "void main() {\n"
        "   vec4 sum = vec4(0.0);\n"
        "   if (uConverted == 1) {\n"
        "      int n = min(int(floor(4.0 * uRadius + 0.5)), 40);\n"
        "      sum = texture(uSrc, vUv);\n"
        "      for (int x = 1; x <= n; x += 2) {\n"
        "         float w = x + 1 <= n ? 2.0 : 1.0;\n"
        "         vec2 off = vec2((w > 1.5 ? float(x) + 0.5 : float(x)) * uTexelSize.x, 0.0);\n"
        "         sum += (texture(uSrc, vUv + off) + texture(uSrc, vUv - off)) * w;\n"
        "      }\n"
        "      fragColor = sum / float(2 * n + 1);\n"
        "      return;\n"
        "   }\n"
        "   if (uQuality == 0) {\n"
        "      for (int x = -4; x <= 4; x++)\n"
        "         sum += ImgTap(vUv + vec2(float(x) * uTexelSize.x * uRadius, 0.0));\n"
        "      fragColor = sum / 9.0;\n"
        "      return;\n"
        "   }\n"
        "   int n = min(int(floor(4.0 * uRadius + 0.5)), 40);\n"
        "   for (int x = -n; x <= n; x++)\n"
        "      sum += ImgTap(vUv + vec2(float(x) * uTexelSize.x, 0.0));\n"
        "   fragColor = sum / float(2 * n + 1);\n"
        "}\n", true },

      { "motionblur", "Effects",
        "uniform float uAngle;\n"
        "uniform float uDistance;\n"
        "void main() {\n"
        "   vec2 dir = vec2(cos(uAngle), sin(uAngle)) * uTexelSize * uDistance;\n"
        "   vec4 sum = vec4(0.0); const int N = 16;\n"
        "   for (int i = 0; i < N; i++) {\n"
        "      float t = float(i) / float(N - 1) - 0.5;\n"
        "      sum += ImgPre(vUv + dir * t);\n"
        "   }\n"
        "   fragColor = ImgUnpremul(sum / float(N));\n"
        "}\n",
        { F("%.2f rad", P("Angle", "uAngle", T::Float, 0.0f, 6.2832f, 0.0f)),
          F("%.0f px", P("Distance", "uDistance", T::Float, 0.0f, 60.0f, 15.0f)),
          LIN() } },

      { "radialblur", "Effects",
        "uniform float uAmount;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 center = vec2(uCenterX, uCenterY);\n"
        "   vec2 dir = vUv - center;\n"
        "   vec4 sum = vec4(0.0); const int N = 16;\n"
        "   for (int i = 0; i < N; i++) {\n"
        "      float t = float(i) / float(N - 1);\n"
        "      sum += ImgPre(vUv - dir * uAmount * t * 0.1);\n"
        "   }\n"
        "   fragColor = ImgUnpremul(sum / float(N));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 5.0f, 1.0f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f),
          LIN() } },

      // ---------------- Effects: sharpen ----------------
      // Unsharp Mask, ported from PhotoCraft crates/algo/src/sharpen/reference.rs (MIT/Apache-2.0): a true Gaussian
      // (sigma = Radius, reach ceil(3 sigma)) on premultiplied values, run as a horizontal prePass and a vertical main pass;
      // a channel sharpens only where |pixel - blur| + 1e-6 reaches Threshold / 255; alpha and transparent pixels are kept.
      { "unsharpmask", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "uniform float uThreshold;\n"
        "void main() {\n"
        "   vec4 o = texture(uSrc, vUv);\n"
        "   if (o.a <= 0.0) { fragColor = o; return; }\n"
        "   vec4 b = texture(uPass, vUv);\n"
        "   if (uRadius >= 0.05) {\n"
        "      int r = min(int(ceil(uRadius * 3.0)), 300);\n"
        "      b = vec4(0.0); float t = 0.0;\n"
        "      for (int i = -r; i <= r; i++) {\n"
        "         float w = exp(-float(i * i) / (2.0 * uRadius * uRadius));\n"
        "         b += texture(uPass, vUv + vec2(0.0, float(i) * uTexelSize.y)) * w; t += w;\n"
        "      }\n"
        "      b /= t;\n"
        "   }\n"
        "   vec3 bl = b.a > 0.0 ? b.rgb / b.a : vec3(0.0);\n"
        "   vec3 d = o.rgb - bl;\n"
        "   vec3 on = step(vec3(uThreshold / 255.0), abs(d) + 1e-6);\n"
        "   fragColor = vec4(o.rgb + uAmount * d * on, o.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 5.0f, 0.6f),
          F("%.1f px", P("Radius", "uRadius", T::Float, 0.1f, 100.0f, 1.5f)),
          F("%.0f levels", P("Threshold", "uThreshold", T::Float, 0.0f, 255.0f, 0.0f)) }, 1,
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   if (uRadius < 0.05) { fragColor = vec4(c.rgb * c.a, c.a); return; }\n"
        "   int r = min(int(ceil(uRadius * 3.0)), 300);\n"
        "   vec4 s = vec4(0.0); float t = 0.0;\n"
        "   for (int i = -r; i <= r; i++) {\n"
        "      float w = exp(-float(i * i) / (2.0 * uRadius * uRadius));\n"
        "      vec4 q = texture(uSrc, vUv + vec2(float(i) * uTexelSize.x, 0.0));\n"
        "      s += vec4(q.rgb * q.a, q.a) * w; t += w;\n"
        "   }\n"
        "   fragColor = s / t;\n"
        "}\n" },

      // ---------------- Effects: distortion family ----------------
      { "twirl", "Effects",
        "uniform float uAngle;\n"
        "uniform float uRadius;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 center = vec2(uCenterX, uCenterY);\n"
        "   vec2 d = vUv - center;\n"
        "   float dist = length(d);\n"
        "   float amt = smoothstep(uRadius, 0.0, dist) * uAngle;\n"
        "   float s = sin(amt), c = cos(amt);\n"
        "   vec2 rd = vec2(c*d.x - s*d.y, s*d.x + c*d.y);\n"
        "   fragColor = ImgSample(center + rd);\n"
        "}\n",
        { F("%.2f rad", P("Angle", "uAngle", T::Float, -6.2832f, 6.2832f, 2.0f)),
          P("Radius", "uRadius", T::Float, 0.05f, 1.0f, 0.5f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f),
          LIN() } },

      { "pinchpunch", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 center = vec2(uCenterX, uCenterY);\n"
        "   vec2 d = vUv - center;\n"
        "   float dist = length(d);\n"
        "   vec2 uv = vUv;\n"
        "   if (dist < uRadius && dist > 0.0001) {\n"
        "      float n = dist / uRadius;\n"
        "      float factor = pow(n, uAmount);\n"
        "      uv = center + normalize(d) * factor * uRadius;\n"
        "   }\n"
        "   fragColor = ImgSample(uv);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.1f, 4.0f, 1.5f),
          P("Radius", "uRadius", T::Float, 0.05f, 1.0f, 0.5f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f),
          LIN() } },

      { "ripple", "Effects",
        "uniform float uAmplitude;\n"
        "uniform float uWavelength;\n"
        "uniform float uPhase;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 d = vUv - vec2(uCenterX, uCenterY);\n"
        "   float dist = length(d);\n"
        "   float wave = sin(dist * uWavelength * 6.2832 - uPhase) * uAmplitude;\n"
        "   vec2 uv = vUv + normalize(d + vec2(0.0001)) * wave * uTexelSize * 10.0;\n"
        "   fragColor = ImgSample(uv);\n"
        "}\n",
        { P("Amplitude", "uAmplitude", T::Float, 0.0f, 2.0f, 0.5f),
          P("Wavelength", "uWavelength", T::Float, 1.0f, 40.0f, 10.0f),
          P("Phase", "uPhase", T::Float, 0.0f, 20.0f, 0.0f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f),
          LIN() } },

      { "pixelate", "Effects",
        "uniform float uBlockSize;\n"
        "void main() {\n"
        "   vec2 res = 1.0 / uTexelSize;\n"
        "   vec2 block = max(vec2(1.0), vec2(uBlockSize));\n"
        "   vec2 uv = (floor(vUv * res / block) * block + block * 0.5) / res;\n"
        "   fragColor = texture(uSrc, uv);\n"
        "}\n",
        { N("%.0f px", P("Block Size", "uBlockSize", T::Float, 1.0f, 64.0f, 8.0f)) } },

      // ---------------- Effects: noise / vignette ----------------
      { "addnoise", "Effects",
        "uniform float uAmount;\n"
        "float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453); }\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float n = (rand(vUv * 1000.0 + uTime) - 0.5) * uAmount;\n"
        "   fragColor = vec4(c.rgb + n, c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 1.0f, 0.1f) } },

      { "vignette", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "uniform float uSoftness;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 d = vUv - vec2(uCenterX, uCenterY);\n"
        "   float dist = length(d) / 0.7071;\n"
        "   float vig = smoothstep(uRadius, max(uRadius - uSoftness, 0.0), dist);\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(c.rgb * mix(1.0, vig, uAmount), c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 1.0f, 0.6f),
          P("Radius", "uRadius", T::Float, 0.1f, 1.5f, 0.9f),
          P("Softness", "uSoftness", T::Float, 0.01f, 1.0f, 0.4f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f) } },

      // ---------------- Compositing: transform ----------------
      { "transform", "Compositing",
        "uniform float uTranslateX;\n"
        "uniform float uTranslateY;\n"
        "uniform float uScale;\n"
        "uniform float uScaleX;\n"
        "uniform float uScaleY;\n"
        "uniform float uRotation;\n"
        "uniform float uCropX;\n"
        "uniform float uCropY;\n"
        "uniform int uFlipH;\n"
        "uniform int uFlipV;\n"
        "void main() {\n"
        "   vec2 uv = vUv - vec2(0.5, 0.5);\n"
        "   float s = sin(-uRotation), c = cos(-uRotation);\n"
        "   uv = vec2(c*uv.x - s*uv.y, s*uv.x + c*uv.y);\n"
        "   uv /= max(uScale, 0.0001);\n"
        "   uv /= vec2(max(uScaleX, 0.0001), max(uScaleY, 0.0001));\n"
        "   if (uFlipH != 0) uv.x = -uv.x;\n"
        "   if (uFlipV != 0) uv.y = -uv.y;\n"
        "   uv -= vec2(uTranslateX, uTranslateY);\n"
        "   uv += vec2(0.5, 0.5);\n"
        "   float loX = uCropX * 0.5, hiX = 1.0 - uCropX * 0.5;\n"
        "   float loY = uCropY * 0.5, hiY = 1.0 - uCropY * 0.5;\n"
        "   if (uv.x < max(loX, 0.0) || uv.x > min(hiX, 1.0) || uv.y < max(loY, 0.0) || uv.y > min(hiY, 1.0)) fragColor = vec4(0.0);\n"
        "   else fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Translate X", "uTranslateX", T::Float, -1.0f, 1.0f, 0.0f),
          P("Translate Y", "uTranslateY", T::Float, -1.0f, 1.0f, 0.0f),
          P("Scale", "uScale", T::Float, 0.1f, 4.0f, 1.0f),
          P("Scale X", "uScaleX", T::Float, 0.1f, 4.0f, 1.0f),
          P("Scale Y", "uScaleY", T::Float, 0.1f, 4.0f, 1.0f),
          P("Rotation", "uRotation", T::Float, -180.0f, 180.0f, 0.0f, 0.0f, 0.0f, /*isDegrees=*/true),
          P("Crop X", "uCropX", T::Float, 0.0f, 0.95f, 0.0f),
          P("Crop Y", "uCropY", T::Float, 0.0f, 0.95f, 0.0f),
          P("Flip Horizontal", "uFlipH", T::Bool, 0.0f, 1.0f, 0.0f),
          P("Flip Vertical", "uFlipV", T::Bool, 0.0f, 1.0f, 0.0f) } },

      // ---------------- Color adjustments ----------------
      { "invert", "Compositing",
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(1.0 - c.rgb, c.a);\n"
        "}\n",
        {} },

      { "posterize", "Compositing",
        "uniform float uLevels;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float levels = max(uLevels, 2.0);\n"
        "   vec3 col = floor(c.rgb * levels) / (levels - 1.0);\n"
        "   fragColor = vec4(clamp(col, 0.0, 1.0), c.a);\n"
        "}\n",
        { N("%.0f", P("Levels", "uLevels", T::Float, 2.0f, 32.0f, 6.0f)) } },

      // Quantizes like posterize, but spends the rounding error as a fixed
      // pattern keyed on the output pixel, so gradients keep their tone as
      // texture instead of banding. Bayer thresholds are computed with bit
      // ops (no tables); "noise" is interleaved gradient noise, a hash with no
      // texture lookup that is identical on every GPU.
      { "dither", "Compositing",
        "uniform int uMode;\n"
        "uniform float uLevels;\n"
        "uniform float uMix;\n"
        "float bayer(ivec2 p, int n) {\n"
        "   int r = 0;\n"
        "   for (int i = 0; i < n; i++) {\n"
        "      int xi = (p.x >> i) & 1, yi = (p.y >> i) & 1;\n"
        "      r = (r << 2) | ((xi ^ yi) << 1) | yi;\n"
        "   }\n"
        "   return (float(r) + 0.5) / float(1 << (2 * n));\n"
        "}\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
        "   float thr = uMode == 3\n"
        "      ? fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715))))\n"
        "      : bayer(p, uMode + 1);\n"
        "   float steps = max(floor(uLevels), 2.0) - 1.0;\n"
        "   vec3 v = clamp(c.rgb, 0.0, 1.0) * steps;\n"
        "   vec3 lo = floor(v);\n"
        "   vec3 q = (lo + step(vec3(thr), v - lo)) / steps;\n"
        "   fragColor = vec4(mix(c.rgb, q, uMix), c.a);\n"
        "}\n",
        { E("Mode", "uMode", { "bayer 2", "bayer 4", "bayer 8", "noise" }, 1),
          N("%.0f", P("Levels", "uLevels", T::Float, 2.0f, 32.0f, 4.0f)),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f) } },

      { "threshold", "Compositing",
        "uniform float uThreshold;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   fragColor = vec4(vec3(step(uThreshold, lum)), c.a);\n"
        "}\n",
        { P("Threshold", "uThreshold", T::Float, 0.0f, 1.0f, 0.5f) } },

      { "exposure", "Compositing",
        "uniform float uExposure;\n"
        "void main() {\n"
        "   vec4 c = ImgSrc(vUv);\n"
        "   fragColor = vec4(c.rgb * pow(2.0, uExposure), c.a);\n"
        "}\n",
        { F("%.2f EV", P("Exposure", "uExposure", T::Float, -3.0f, 3.0f, 0.0f)), LIN() } },

      // ---------------- Alpha / opacity operators ----------------
      // Nothing above edits an existing alpha channel directly - chroma/luma
      // key only produce one. "show alpha" is the debugging tool users
      // actually reach for first (visualize the matte inline), so it's kept
      // simplest and first in this block.
      { "show alpha", "Compositing",
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(vec3(c.a), 1.0);\n"
        "}\n",
        {} },

      { "opacity", "Compositing",
        "uniform float uOpacity;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(c.rgb, c.a * uOpacity);\n"
        "}\n",
        { P("Opacity", "uOpacity", T::Float, 0.0f, 1.0f, 1.0f) } },

      { "set alpha", "Compositing",
        // Second input is any grayscale/keyed/bw image (e.g. show alpha's
        // output) whose luminance becomes the new alpha channel.
        "void main() {\n"
        "   vec4 a = texture(uSrc, vUv);\n"
        "   if (uHasSrc2 == 0) { fragColor = vec4(a.rgb, 1.0); return; }\n"
        "   vec3 b = texture(uSrc2, vUv).rgb;\n"
        "   float luma = dot(b, vec3(0.299, 0.587, 0.114));\n"
        "   fragColor = vec4(a.rgb, luma);\n"
        "}\n",
        {}, 2 },

      { "alpha invert", "Compositing",
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(c.rgb, 1.0 - c.a);\n"
        "}\n",
        {} },

      { "alpha from luma", "Compositing",
        "uniform int uInvert;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   if (uInvert == 1) luma = 1.0 - luma;\n"
        "   fragColor = vec4(c.rgb, luma);\n"
        "}\n",
        { P("invert", "uInvert", T::Bool, 0.0f, 1.0f, 0.0f) } },

      // Low/high/gamma remap matches the Black Point / White Point / Gamma
      // convention used by the "color adjustments" Levels section below:
      // clamp-normalize between the two points, then a gamma pow.
      { "alpha levels", "Compositing",
        "uniform float uLow;\n"
        "uniform float uHigh;\n"
        "uniform float uGamma;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float a = clamp((c.a - uLow) / max(uHigh - uLow, 0.0001), 0.0, 1.0);\n"
        "   a = pow(a, 1.0 / max(uGamma, 0.0001));\n"
        "   fragColor = vec4(c.rgb, a);\n"
        "}\n",
        { P("Low", "uLow", T::Float, 0.0f, 1.0f, 0.0f),
          P("High", "uHigh", T::Float, 0.0f, 1.0f, 1.0f),
          P("Gamma", "uGamma", T::Float, 0.1f, 4.0f, 1.0f) } },

      { "premultiply", "Compositing",
        "uniform int uMode;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = (uMode == 0) ? c.rgb * c.a : c.rgb / max(c.a, 1e-4);\n"
        "   fragColor = vec4(col, c.a);\n"
        "}\n",
        { E("mode", "uMode", { "Premultiply", "Unpremultiply" }, 0) } },

      // ---------------- Effects: bloom / glow ----------------
      { "bloom", "Effects",
        "uniform float uThreshold;\n"
        "uniform float uIntensity;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 c = ImgSrc(vUv);\n"
        "   vec3 bloom = vec3(0.0); float total = 0.0;\n"
        "   for (int y = -5; y <= 5; y++) {\n"
        "      float w = exp(-float(y*y) / 12.0);\n"
        "      bloom += texture(uPass, vUv + vec2(0.0, float(y) * uTexelSize.y * uRadius)).rgb * w; total += w;\n"
        "   }\n"
        "   bloom /= max(total, 1e-4);\n"
        "   fragColor = vec4(c.rgb + bloom * uIntensity, c.a);\n"
        "}\n",
        { P("Threshold", "uThreshold", T::Float, 0.0f, 1.0f, 0.6f),
          P("Intensity", "uIntensity", T::Float, 0.0f, 5.0f, 1.4f),
          F("%.1f px", P("Radius", "uRadius", T::Float, 0.5f, 12.0f, 4.0f)), LIN() }, 1,
        "uniform float uThreshold;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        // linear light: the threshold knob keeps its perceptual meaning and
        // luminance uses the Rec.709 weights that apply to linear RGB
        "   float thr = ImgDecode(vec3(uThreshold)).x;\n"
        "   vec3 lw = uLinear == 1 ? vec3(0.2126, 0.7152, 0.0722) : vec3(0.299, 0.587, 0.114);\n"
        "   vec3 bloom = vec3(0.0); float total = 0.0;\n"
        "   for (int x = -5; x <= 5; x++) {\n"
        "      vec2 uv = vUv + vec2(float(x) * uTexelSize.x * uRadius, 0.0);\n"
        "      vec3 bright;\n"
        "      if (uConverted == 1) bright = texture(uSrc, uv).rgb;\n"
        "      else {\n"
        "         vec3 s = ImgTap(uv).rgb;\n"
        "         bright = max(s - vec3(thr), vec3(0.0)) * step(thr, dot(s, lw));\n"
        "      }\n"
        "      float w = exp(-float(x*x) / 12.0);\n"
        "      bloom += bright * w; total += w;\n"
        "   }\n"
        "   fragColor = vec4(bloom / max(total, 1e-4), 1.0);\n"
        "}\n", true,
        // Linear light: the bright-pass runs once per pixel into the float
        // scratch, so the 11 strided taps above are plain fetches. Decoding
        // per tap was the whole of linear light's +26% on Bloom. Identical at
        // integer Radius (taps on texel centres); at fractional Radius the
        // taps now interpolate bright light rather than encoded colour.
        "uniform float uThreshold;\n"
        "void main() {\n"
        "   float thr = ImgDecode(vec3(uThreshold)).x;\n"
        "   vec3 s = ImgTap(vUv).rgb;\n"
        "   float lum = dot(s, vec3(0.2126, 0.7152, 0.0722));\n"
        "   fragColor = vec4(max(s - vec3(thr), vec3(0.0)) * step(thr, lum), 1.0);\n"
        "}\n" },

      { "diffuseglow", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 c = ImgSrc(vUv);\n"
        "   vec3 blur = vec3(0.0); float total = 0.0;\n"
        "   for (int y = -4; y <= 4; y++) {\n"
        "      float w = exp(-float(y*y) / 10.0);\n"
        "      blur += texture(uPass, vUv + vec2(0.0, float(y) * uTexelSize.y * uRadius)).rgb * w; total += w;\n"
        "   }\n"
        "   blur /= max(total, 1e-4);\n"
        "   vec3 screen = 1.0 - (1.0 - c.rgb) * (1.0 - blur);\n"
        "   fragColor = vec4(mix(c.rgb, screen, uAmount), c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 1.0f, 0.6f),
          F("%.1f px", P("Radius", "uRadius", T::Float, 0.5f, 12.0f, 4.0f)), LIN() }, 1,
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec3 blur = vec3(0.0); float total = 0.0;\n"
        "   for (int x = -4; x <= 4; x++) {\n"
        "      float w = exp(-float(x*x) / 10.0);\n"
        "      blur += ImgTap(vUv + vec2(float(x) * uTexelSize.x * uRadius, 0.0)).rgb * w; total += w;\n"
        "   }\n"
        "   fragColor = vec4(blur / max(total, 1e-4), 1.0);\n"
        "}\n" },

      // ---------------- Effects: glitch ----------------
      // One node, six algorithms behind a dropdown, rather than six near-identical
      // nodes cluttering the spawn menu.
      { "glitch", "Effects",
        "uniform int uKind;\n"
        "uniform float uAmount;\n"
        "uniform float uDetail;\n"
        "uniform float uSpeed;\n"
        "uniform float uSeed;\n"
        "float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233)) + uSeed) * 43758.5453); }\n"
        "void main() {\n"
        "   float t = uTime * uSpeed;\n"
        "   vec2 uv = vUv;\n"
        "\n"
        "   if (uKind == 0) {\n"           // Slice shift (the original)
        "      float blockY = floor(uv.y * max(1.0, uDetail * 40.0));\n"
        "      uv.x += (rand(vec2(blockY, floor(t * 10.0))) - 0.5) * uAmount * 0.2;\n"
        "      vec4 c = ImgSample(uv);\n"
        "      vec4 cr = ImgSample(uv + vec2(uAmount * 0.01, 0.0));\n"
        "      vec4 cb = ImgSample(uv - vec2(uAmount * 0.01, 0.0));\n"
        "      fragColor = vec4(cr.r, c.g, cb.b, c.a);\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 1) {\n"           // RGB shift
        "      vec2 dir = vec2(cos(uDetail * 6.2832), sin(uDetail * 6.2832)) * uAmount * 0.02;\n"
        "      fragColor = vec4(ImgSample(uv + dir).r, ImgSample(uv).g,\n"
        "                       ImgSample(uv - dir).b, ImgSample(uv).a);\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 2) {\n"           // Scanlines
        "      vec4 c = ImgSample(uv);\n"
        "      float line = sin((uv.y + t * 0.1) * max(10.0, uDetail * 800.0) * 3.14159);\n"
        "      fragColor = vec4(c.rgb * (1.0 - uAmount * step(0.0, -line)), c.a);\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 3) {\n"           // Blocks
        "      vec2 grid = max(vec2(2.0), vec2(uDetail * 60.0));\n"
        "      vec2 cell = floor(uv * grid);\n"
        "      if (rand(cell + floor(t * 8.0)) > 1.0 - uAmount)\n"
        "         uv += (vec2(rand(cell + 1.0), rand(cell + 2.0)) - 0.5) * 0.15;\n"
        "      fragColor = ImgSample(clamp(uv, 0.0, 1.0));\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 4) {\n"           // Wave
        "      uv.x += sin(uv.y * max(1.0, uDetail * 200.0) + t * 2.0) * uAmount * 0.05;\n"
        "      fragColor = ImgSample(clamp(uv, 0.0, 1.0));\n"
        "      return;\n"
        "   }\n"
        "   // Datamosh\n"
        "   float slice = floor(uv.y * max(2.0, uDetail * 120.0));\n"
        "   float r = rand(vec2(slice, floor(t * 4.0)));\n"
        "   uv.x = fract(uv.x + (r - 0.5) * uAmount * 0.5);\n"
        "   vec4 c = ImgSample(uv);\n"
        "   if (r > 0.85) c.rgb = c.gbr;\n"
        "   fragColor = c;\n"
        "}\n",
        { E("kind", "uKind", { "Slice Shift", "RGB Shift", "Scanlines", "Blocks", "Wave", "Datamosh" }, 0),
          P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 0.6f),
          P("Detail", "uDetail", T::Float, 0.02f, 1.0f, 0.4f),
          P("Speed", "uSpeed", T::Float, 0.0f, 4.0f, 1.0f),
          N("%.0f", P("Seed", "uSeed", T::Float, 0.0f, 100.0f, 0.0f)),
          LIN() } },

      // ---------------- Effects: lens / warp ----------------
      { "lensdistortion", "Effects",
        "uniform float uBarrel;\n"
        "uniform float uChroma;\n"
        "uniform float uZoom;\n"
        "vec2 warp(vec2 uv, float k) {\n"
        "   vec2 c = uv - 0.5;\n"
        "   float r2 = dot(c, c);\n"
        "   return 0.5 + c * (1.0 + k * r2) / max(uZoom, 0.01);\n"
        "}\n"
        "void main() {\n"
        "   vec2 ur = warp(vUv, uBarrel * (1.0 + uChroma * 0.1));\n"
        "   vec2 ug = warp(vUv, uBarrel);\n"
        "   vec2 ub = warp(vUv, uBarrel * (1.0 - uChroma * 0.1));\n"
        "   float a = texture(uSrc, ug).a;\n"
        "   if (ug.x < 0.0 || ug.x > 1.0 || ug.y < 0.0 || ug.y > 1.0) { fragColor = vec4(0.0); return; }\n"
        "   fragColor = vec4(texture(uSrc, ur).r, texture(uSrc, ug).g, texture(uSrc, ub).b, a);\n"
        "}\n",
        { P("Barrel", "uBarrel", T::Float, -1.5f, 1.5f, 0.3f),
          P("Chromatic", "uChroma", T::Float, 0.0f, 5.0f, 0.6f),
          P("Zoom", "uZoom", T::Float, 0.5f, 2.0f, 1.0f) } },

      { "displace", "Effects",
        "uniform float uAmount;\n"
        "uniform float uScale;\n"
        "void main() {\n"
        "   vec2 offset;\n"
        "   if (uHasSrc2 == 1) {\n"
        "      vec3 m = texture(uSrc2, fract(vUv * max(uScale, 0.01))).rgb;\n"
        "      offset = (m.rg - 0.5) * 2.0;\n"
        "   } else {\n"
        "      float n = sin(vUv.x * 20.0 * uScale + uTime) * cos(vUv.y * 20.0 * uScale - uTime);\n"
        "      offset = vec2(n, -n);\n"
        "   }\n"
        "   fragColor = ImgSample(clamp(vUv + offset * uAmount * 0.1, 0.0, 1.0));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 0.3f),
          P("Map Scale", "uScale", T::Float, 0.1f, 8.0f, 1.0f),
          LIN() },
        2 },

      { "liquify", "Effects",
        "uniform float uAmount;\n"
        "uniform float uScale;\n"
        "uniform float uSpeed;\n"
        "vec2 hash2(vec2 p) {\n"
        "   return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);\n"
        "}\n"
        "float noise(vec2 p) {\n"
        "   vec2 i = floor(p), f = fract(p);\n"
        "   vec2 u = f * f * (3.0 - 2.0 * f);\n"
        "   float a = hash2(i).x, b = hash2(i + vec2(1.0, 0.0)).x;\n"
        "   float c = hash2(i + vec2(0.0, 1.0)).x, d = hash2(i + vec2(1.0, 1.0)).x;\n"
        "   return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
        "}\n"
        "void main() {\n"
        "   float t = uTime * uSpeed;\n"
        "   vec2 flow = vec2(noise(vUv * uScale + t), noise(vUv * uScale + 5.2 - t)) - 0.5;\n"
        "   fragColor = ImgSample(clamp(vUv + flow * uAmount * 0.2, 0.0, 1.0));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 0.4f),
          P("Scale", "uScale", T::Float, 0.5f, 20.0f, 4.0f),
          P("Speed", "uSpeed", T::Float, 0.0f, 3.0f, 0.4f),
          LIN() } },

      // ---------------- Effects: symmetry ----------------
      { "symmetry", "Effects",
        "uniform int uAxis;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "uniform int uFlip;\n"
        "void main() {\n"
        "   vec2 uv = vUv;\n"
        "   bool second = false;\n"
        "   if (uAxis == 0) { second = uv.x > uCenterX; if (second) uv.x = 2.0*uCenterX - uv.x; }\n"
        "   else if (uAxis == 1) { second = uv.y > uCenterY; if (second) uv.y = 2.0*uCenterY - uv.y; }\n"
        "   else {\n"
        "      if (uv.x > uCenterX) uv.x = 2.0*uCenterX - uv.x;\n"
        "      if (uv.y > uCenterY) uv.y = 2.0*uCenterY - uv.y;\n"
        "   }\n"
        "   if (uFlip == 1) uv = 1.0 - uv;\n"
        "   fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));\n"
        "}\n",
        { E("axis", "uAxis", { "Mirror X", "Mirror Y", "Both" }, 0),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f),
          P("flip", "uFlip", T::Bool, 0.0f, 1.0f, 0.0f) } },

      { "kaleidoscope", "Effects",
        "uniform float uSegments;\n"
        "uniform float uRotation;\n"
        "uniform float uZoom;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 p = vUv - vec2(uCenterX, uCenterY);\n"
        "   float r = length(p);\n"
        "   float a = atan(p.y, p.x) + uRotation;\n"
        "   float seg = 6.2831853 / max(2.0, uSegments);\n"
        "   a = mod(a, seg);\n"
        "   a = abs(a - seg * 0.5);\n"
        "   vec2 uv = vec2(cos(a), sin(a)) * r * max(uZoom, 0.01) + 0.5;\n"
        "   fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));\n"
        "}\n",
        { N("%.0f", P("Segments", "uSegments", T::Float, 2.0f, 32.0f, 6.0f)),
          F("%.2f rad", P("Rotation", "uRotation", T::Float, 0.0f, 6.2832f, 0.0f)),
          P("Zoom", "uZoom", T::Float, 0.2f, 3.0f, 1.0f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f) } },

      { "mirror tile", "Effects",
        "uniform float uTiles;\n"
        "void main() {\n"
        "   vec2 uv = vUv * max(1.0, uTiles);\n"
        "   vec2 cell = floor(uv);\n"
        "   vec2 f = fract(uv);\n"
        "   if (mod(cell.x, 2.0) > 0.5) f.x = 1.0 - f.x;\n"
        "   if (mod(cell.y, 2.0) > 0.5) f.y = 1.0 - f.y;\n"
        "   fragColor = texture(uSrc, f);\n"
        "}\n",
        { N("%.0f", P("Tiles", "uTiles", T::Float, 1.0f, 12.0f, 2.0f)) } },

      // ---------------- Keying ----------------
      { "chroma key", "Compositing",
        "uniform vec3 uKeyColor;\n"
        "uniform float uTolerance;\n"
        "uniform float uSoftness;\n"
        "uniform float uSpill;\n"
        "uniform int uShowMatte;\n"
        "vec3 rgb2ycbcr(vec3 c) {\n"
        "   float y  = dot(c, vec3(0.299, 0.587, 0.114));\n"
        "   return vec3(y, (c.b - y) * 0.565, (c.r - y) * 0.713);\n"
        "}\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   // Chroma distance in YCbCr, so brightness differences do not break the key\n"
        "   vec3 a = rgb2ycbcr(c.rgb);\n"
        "   vec3 b = rgb2ycbcr(uKeyColor);\n"
        "   float d = distance(a.yz, b.yz);\n"
        "   float alpha = smoothstep(uTolerance, uTolerance + max(uSoftness, 1e-4), d);\n"
        "   if (uShowMatte == 1) { fragColor = vec4(vec3(alpha), 1.0); return; }\n"
        "   vec3 col = c.rgb;\n"
        "   if (uSpill > 0.0) {\n"
        "      // pull residual key colour out of the edges\n"
        "      float m = dot(normalize(uKeyColor + 1e-4), normalize(col + 1e-4));\n"
        "      float grey = dot(col, vec3(0.299, 0.587, 0.114));\n"
        "      col = mix(col, vec3(grey), clamp((m - 0.5) * 2.0, 0.0, 1.0) * uSpill * (1.0 - alpha));\n"
        "   }\n"
        "   fragColor = vec4(col, c.a * alpha);\n"
        "}\n",
        { P("Key Colour", "uKeyColor", T::Color, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f),
          P("Tolerance", "uTolerance", T::Float, 0.0f, 0.5f, 0.08f),
          P("Softness", "uSoftness", T::Float, 0.001f, 0.4f, 0.06f),
          P("Spill Removal", "uSpill", T::Float, 0.0f, 1.0f, 0.5f),
          E("show", "uShowMatte", { "Keyed", "Matte" }, 0) } },

      { "luma key", "Compositing",
        "uniform float uLow;\n"
        "uniform float uHigh;\n"
        "uniform float uSoftness;\n"
        "uniform int uInvert;\n"
        "uniform int uShowMatte;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   float s = max(uSoftness, 1e-4);\n"
        "   float alpha = smoothstep(uLow - s, uLow + s, lum) *\n"
        "                 (1.0 - smoothstep(uHigh - s, uHigh + s, lum));\n"
        "   if (uInvert == 1) alpha = 1.0 - alpha;\n"
        "   if (uShowMatte == 1) { fragColor = vec4(vec3(alpha), 1.0); return; }\n"
        "   fragColor = vec4(c.rgb, c.a * alpha);\n"
        "}\n",
        { P("Low", "uLow", T::Float, 0.0f, 1.0f, 0.1f),
          P("High", "uHigh", T::Float, 0.0f, 1.0f, 1.0f),
          P("Softness", "uSoftness", T::Float, 0.001f, 0.3f, 0.03f),
          P("invert", "uInvert", T::Bool, 0.0f, 1.0f, 0.0f),
          E("show", "uShowMatte", { "Keyed", "Matte" }, 0) } },

      // ---------------- Effects: framing / surface ----------------
      { "crop", "Effects",
        "uniform float uLeft;\n"
        "uniform float uRight;\n"
        "uniform float uTop;\n"
        "uniform float uBottom;\n"
        "uniform int uMode;\n"
        "uniform vec3 uFill;\n"
        "void main() {\n"
        "   float l = uLeft, r = 1.0 - uRight, b = uBottom, t = 1.0 - uTop;\n"
        "   if (uMode == 1) {\n"
        "      // zoom the kept region back out to fill the frame\n"
        "      vec2 uv = vec2(mix(l, r, vUv.x), mix(b, t, vUv.y));\n"
        "      fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));\n"
        "      return;\n"
        "   }\n"
        "   if (vUv.x < l || vUv.x > r || vUv.y < b || vUv.y > t) {\n"
        "      fragColor = vec4(uFill, uMode == 2 ? 1.0 : 0.0);\n"
        "      return;\n"
        "   }\n"
        "   fragColor = texture(uSrc, vUv);\n"
        "}\n",
        { P("Left", "uLeft", T::Float, 0.0f, 0.95f, 0.1f),
          P("Right", "uRight", T::Float, 0.0f, 0.95f, 0.1f),
          P("Top", "uTop", T::Float, 0.0f, 0.95f, 0.1f),
          P("Bottom", "uBottom", T::Float, 0.0f, 0.95f, 0.1f),
          E("outside", "uMode", { "Transparent", "Zoom to fill", "Fill colour" }, 0),
          P("Fill", "uFill", T::Color, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f) } },

      { "emboss", "Effects",
        "uniform float uAmount;\n"
        "uniform float uAngle;\n"
        "uniform float uDistance;\n"
        "uniform int uKeepColor;\n"
        "uniform int uPremul;\n"
        "void main() {\n"
        "   vec2 off = vec2(cos(uAngle), sin(uAngle)) * uTexelSize * uDistance;\n"
        "   vec3 a = uPremul == 1 ? ImgTapPremul(vUv + off).rgb : texture(uSrc, vUv + off).rgb;\n"
        "   vec3 b = uPremul == 1 ? ImgTapPremul(vUv - off).rgb : texture(uSrc, vUv - off).rgb;\n"
        "   vec3 diff = (a - b) * uAmount;\n"
        "   float e = dot(diff, vec3(0.299, 0.587, 0.114));\n"
        "   vec4 src = texture(uSrc, vUv);\n"
        "   if (uKeepColor == 1) fragColor = vec4(ImgClip(src.rgb + diff), src.a);\n"
        "   else fragColor = vec4(vec3(clamp(e + 0.5, 0.0, 1.0)), src.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 8.0f, 2.0f),
          F("%.2f rad", P("Angle", "uAngle", T::Float, 0.0f, 6.2832f, 0.785f)),
          F("%.1f px", P("Distance", "uDistance", T::Float, 0.5f, 12.0f, 1.5f)),
          E("style", "uKeepColor", { "Grey", "Over colour" }, 0),
          PREMUL() } },

      { "normal map", "Effects",
        "uniform float uStrength;\n"
        "uniform int uInvertY;\n"
        "uniform int uOutput;\n"
        "uniform int uPremul;\n"
        // taps sit on texel centres, so multiplying by alpha after the fetch is exact
        "float lum(vec2 uv) { vec4 c = texture(uSrc, uv); return dot(c.rgb, vec3(0.299, 0.587, 0.114)) * (uPremul == 1 ? c.a : 1.0); }\n"
        "void main() {\n"
        "   vec2 t = uTexelSize;\n"
        "   // Sobel gradient of luminance treated as a height field\n"
        "   float tl = lum(vUv + vec2(-t.x,  t.y)), tc = lum(vUv + vec2(0.0,  t.y)), tr = lum(vUv + vec2( t.x,  t.y));\n"
        "   float ml = lum(vUv + vec2(-t.x, 0.0)),                                   mr = lum(vUv + vec2( t.x, 0.0));\n"
        "   float bl = lum(vUv + vec2(-t.x, -t.y)), bc = lum(vUv + vec2(0.0, -t.y)), br = lum(vUv + vec2( t.x, -t.y));\n"
        "   float dx = (tr + 2.0*mr + br) - (tl + 2.0*ml + bl);\n"
        "   float dy = (bl + 2.0*bc + br) - (tl + 2.0*tc + tr);\n"
        "   if (uInvertY == 1) dy = -dy;\n"
        "   vec3 n = normalize(vec3(-dx * uStrength, -dy * uStrength, 1.0));\n"
        "   if (uOutput == 1) { fragColor = vec4(vec3(lum(vUv)), 1.0); return; }\n"
        "   if (uOutput == 2) { fragColor = vec4(vec3(length(vec2(dx, dy)) * uStrength), 1.0); return; }\n"
        "   fragColor = vec4(n * 0.5 + 0.5, 1.0);\n"
        "}\n",
        { P("Strength", "uStrength", T::Float, 0.1f, 12.0f, 3.0f),
          E("flip Y", "uInvertY", { "OpenGL", "DirectX" }, 0),
          E("output", "uOutput", { "Normals", "Height", "Slope" }, 0),
          PREMUL() } },

      { "convolve", "Effects",
        // A user-editable 3x3 kernel. Blur, sharpen, edge and emboss are all just
        // different numbers in this grid.
        "uniform float uK[9];\n"
        "uniform float uDivisor;\n"
        "uniform float uBias;\n"
        "uniform float uSpread;\n"
        "uniform float uMix;\n"
        "void main() {\n"
        "   vec4 src = texture(uSrc, vUv);\n"
        "   vec3 sum = vec3(0.0);\n"
        "   int i = 0;\n"
        "   for (int y = 1; y >= -1; y--) {\n"
        "      for (int x = -1; x <= 1; x++) {\n"
        "         vec2 off = vec2(float(x), float(y)) * uTexelSize * max(uSpread, 0.1);\n"
        "         sum += texture(uSrc, vUv + off).rgb * uK[i];\n"
        "         i++;\n"
        "      }\n"
        "   }\n"
        "   float div = abs(uDivisor) < 1e-4 ? 1.0 : uDivisor;\n"
        "   vec3 outCol = ImgClip(sum / div + uBias);\n"
        "   fragColor = vec4(mix(src.rgb, outCol, uMix), src.a);\n"
        "}\n",
        { P("k11", "uK[0]", T::Float, -8.0f, 8.0f, 0.0f),
          P("k12", "uK[1]", T::Float, -8.0f, 8.0f, -1.0f),
          P("k13", "uK[2]", T::Float, -8.0f, 8.0f, 0.0f),
          P("k21", "uK[3]", T::Float, -8.0f, 8.0f, -1.0f),
          P("k22", "uK[4]", T::Float, -8.0f, 8.0f, 5.0f),
          P("k23", "uK[5]", T::Float, -8.0f, 8.0f, -1.0f),
          P("k31", "uK[6]", T::Float, -8.0f, 8.0f, 0.0f),
          P("k32", "uK[7]", T::Float, -8.0f, 8.0f, -1.0f),
          P("k33", "uK[8]", T::Float, -8.0f, 8.0f, 0.0f),
          P("Divisor", "uDivisor", T::Float, -16.0f, 16.0f, 1.0f),
          P("Bias", "uBias", T::Float, -1.0f, 1.0f, 0.0f),
          P("Spread", "uSpread", T::Float, 0.5f, 8.0f, 1.0f),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f) } },

      { "lookup", "Compositing",
        // Second input is the palette: this pixel's luminance indexes across it.
        "uniform float uMix;\n"
        "uniform int uChannel;\n"
        "uniform float uOffset;\n"
        "uniform int uEnds;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   if (uHasSrc2 == 0) { fragColor = c; return; }\n"
        "   float idx;\n"
        "   if (uChannel == 1) idx = c.r;\n"
        "   else if (uChannel == 2) idx = c.g;\n"
        "   else if (uChannel == 3) idx = c.b;\n"
        "   else if (uChannel == 4) idx = c.a;\n"
        "   else idx = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   vec3 mapped;\n"
        "   if (uEnds == 0) mapped = texture(uSrc2, vec2(fract(idx + uOffset), 0.5)).rgb;\n"
        // Exact: white stays the palette's last colour (offset still cycles),
        // and the index lands on texel centres, so an identity ramp is identity.
        "   else {\n"
        "      float x = clamp(idx, 0.0, 1.0) + uOffset;\n"
        "      x = x > 1.0 ? x - 1.0 : x;\n"
        "      float w = float(textureSize(uSrc2, 0).x);\n"
        "      mapped = texture(uSrc2, vec2((x * (w - 1.0) + 0.5) / w, 0.5)).rgb;\n"
        "   }\n"
        "   fragColor = vec4(mix(c.rgb, mapped, uMix), c.a);\n"
        "}\n",
        { E("index by", "uChannel", { "Luminance", "Red", "Green", "Blue", "Alpha" }, 0),
          P("Offset", "uOffset", T::Float, 0.0f, 1.0f, 0.0f),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f),
          OLD(0.0f, E("ends", "uEnds", { "Wrap", "Exact" }, 1)) },
        2 },

      // ---------------- Effects: halftone / edges ----------------
      { "halftone", "Effects",
        "uniform float uScale;\n"
        "uniform float uAngle;\n"
        "uniform int uColorMode;\n"
        "float dotAt(vec2 uv, float lum, float scale, float angle) {\n"
        "   float s = sin(angle), c = cos(angle);\n"
        "   vec2 r = vec2(c*uv.x - s*uv.y, s*uv.x + c*uv.y) * scale;\n"
        "   vec2 g = fract(r) - 0.5;\n"
        "   return step(length(g), sqrt(1.0 - lum) * 0.75);\n"
        "}\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   if (uColorMode == 1) {\n"
        "      float dr = dotAt(vUv, c.r, uScale, uAngle);\n"
        "      float dg = dotAt(vUv, c.g, uScale, uAngle + 0.4);\n"
        "      float db = dotAt(vUv, c.b, uScale, uAngle + 0.8);\n"
        "      fragColor = vec4(dr, dg, db, c.a);\n"
        "   } else {\n"
        "      float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "      float d = dotAt(vUv, lum, uScale, uAngle);\n"
        "      fragColor = vec4(vec3(d), c.a);\n"
        "   }\n"
        "}\n",
        { N("%.0f", P("Scale", "uScale", T::Float, 10.0f, 300.0f, 80.0f)),
          F("%.2f rad", P("Angle", "uAngle", T::Float, 0.0f, 1.5708f, 0.4f)),
          E("style", "uColorMode", { "Mono", "Colour" }, 0) } },

      { "edge sobel", "Effects",
        "uniform float uAmount;\n"
        "uniform int uInvert;\n"
        "float lum(vec2 uv) { return dot(texture(uSrc, uv).rgb, vec3(0.299, 0.587, 0.114)); }\n"
        "void main() {\n"
        "   vec2 t = uTexelSize;\n"
        "   float gx = lum(vUv + vec2(-t.x, -t.y)) * -1.0 + lum(vUv + vec2(t.x, -t.y)) * 1.0\n"
        "            + lum(vUv + vec2(-t.x, 0.0)) * -2.0 + lum(vUv + vec2(t.x, 0.0)) * 2.0\n"
        "            + lum(vUv + vec2(-t.x, t.y)) * -1.0 + lum(vUv + vec2(t.x, t.y)) * 1.0;\n"
        "   float gy = lum(vUv + vec2(-t.x, -t.y)) * -1.0 + lum(vUv + vec2(-t.x, t.y)) * 1.0\n"
        "            + lum(vUv + vec2(0.0, -t.y)) * -2.0 + lum(vUv + vec2(0.0, t.y)) * 2.0\n"
        "            + lum(vUv + vec2(t.x, -t.y)) * -1.0 + lum(vUv + vec2(t.x, t.y)) * 1.0;\n"
        "   float e = clamp(length(vec2(gx, gy)) * uAmount, 0.0, 1.0);\n"
        "   if (uInvert == 1) e = 1.0 - e;\n"
        "   fragColor = vec4(vec3(e), texture(uSrc, vUv).a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.1f, 8.0f, 1.5f),
          P("invert", "uInvert", T::Bool, 0.0f, 1.0f, 0.0f) } },

      { "edge outline", "Effects",
        "uniform float uThickness;\n"
        "uniform float uThreshold;\n"
        "uniform vec3 uColor;\n"
        "float lum(vec2 uv) { return dot(texture(uSrc, uv).rgb, vec3(0.299, 0.587, 0.114)); }\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec2 t = uTexelSize * max(1.0, uThickness);\n"
        "   float gx = lum(vUv - vec2(t.x, 0.0)) - lum(vUv + vec2(t.x, 0.0));\n"
        "   float gy = lum(vUv - vec2(0.0, t.y)) - lum(vUv + vec2(0.0, t.y));\n"
        "   float e = step(uThreshold, length(vec2(gx, gy)));\n"
        "   fragColor = vec4(mix(c.rgb, uColor, e), max(c.a, e));\n"
        "}\n",
        { N("%.0f px", P("Thickness", "uThickness", T::Float, 1.0f, 10.0f, 2.0f)),
          P("Threshold", "uThreshold", T::Float, 0.01f, 1.0f, 0.15f),
          P("Colour", "uColor", T::Color, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f) } },

      // ---------------- Color: LUT ----------------
      { "lut", "Compositing",
        // Second input is a HALD/strip LUT image: an N*N by N grid of slices.
        // interpolation: Original is the first version (no half-texel inset,
        // so even an identity LUT shifts colours); patches saved before the
        // choice existed keep it. Trilinear and Tetrahedral address texel
        // centres; Tetrahedral (Resolve's default) splits each cube into six
        // tetrahedra, which keeps neutrals neutral.
        "uniform float uSize;\n"
        "uniform float uMix;\n"
        "uniform int uInterp;\n"
        "vec3 Lt(vec3 i, float n) { return texture(uSrc2, vec2((i.b * n + i.r + 0.5) / (n * n), (i.g + 0.5) / n)).rgb; }\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   if (uHasSrc2 == 0) { fragColor = c; return; }\n"
        "   float n = max(2.0, floor(uSize));\n"
        "   vec3 graded;\n"
        "   if (uInterp == 0) {\n"
        "      float blue = clamp(c.b, 0.0, 1.0) * (n - 1.0);\n"
        "      float slice0 = floor(blue);\n"
        "      float slice1 = min(slice0 + 1.0, n - 1.0);\n"
        "      float f = blue - slice0;\n"
        "      vec2 base = vec2(clamp(c.r, 0.0, 1.0) / n, clamp(c.g, 0.0, 1.0));\n"
        "      vec3 s0 = texture(uSrc2, base + vec2(slice0 / n, 0.0)).rgb;\n"
        "      vec3 s1 = texture(uSrc2, base + vec2(slice1 / n, 0.0)).rgb;\n"
        "      graded = mix(s0, s1, f);\n"
        "   } else {\n"
        "      vec3 p = clamp(c.rgb, 0.0, 1.0) * (n - 1.0);\n"
        "      vec3 i = min(floor(p), vec3(n - 2.0));\n"
        "      vec3 f = p - i;\n"
        "      if (uInterp == 1) {\n"
        // red and green blend in hardware inside one slice; blue across two
        "         vec2 rg = vec2(i.r + f.r + 0.5, i.g + f.g + 0.5);\n"
        "         vec3 s0 = texture(uSrc2, vec2((i.b * n + rg.x) / (n * n), rg.y / n)).rgb;\n"
        "         vec3 s1 = texture(uSrc2, vec2(((i.b + 1.0) * n + rg.x) / (n * n), rg.y / n)).rgb;\n"
        "         graded = mix(s0, s1, f.b);\n"
        "      } else {\n"
        "         vec3 c000 = Lt(i, n), c111 = Lt(i + 1.0, n);\n"
        "         if (f.r >= f.g) {\n"
        "            if (f.g >= f.b) { vec3 c100 = Lt(i + vec3(1,0,0), n), c110 = Lt(i + vec3(1,1,0), n);\n"
        "               graded = c000 + f.r * (c100 - c000) + f.g * (c110 - c100) + f.b * (c111 - c110); }\n"
        "            else if (f.r >= f.b) { vec3 c100 = Lt(i + vec3(1,0,0), n), c101 = Lt(i + vec3(1,0,1), n);\n"
        "               graded = c000 + f.r * (c100 - c000) + f.b * (c101 - c100) + f.g * (c111 - c101); }\n"
        "            else { vec3 c001 = Lt(i + vec3(0,0,1), n), c101 = Lt(i + vec3(1,0,1), n);\n"
        "               graded = c000 + f.b * (c001 - c000) + f.r * (c101 - c001) + f.g * (c111 - c101); }\n"
        "         } else {\n"
        "            if (f.b >= f.g) { vec3 c001 = Lt(i + vec3(0,0,1), n), c011 = Lt(i + vec3(0,1,1), n);\n"
        "               graded = c000 + f.b * (c001 - c000) + f.g * (c011 - c001) + f.r * (c111 - c011); }\n"
        "            else if (f.b >= f.r) { vec3 c010 = Lt(i + vec3(0,1,0), n), c011 = Lt(i + vec3(0,1,1), n);\n"
        "               graded = c000 + f.g * (c010 - c000) + f.b * (c011 - c010) + f.r * (c111 - c011); }\n"
        "            else { vec3 c010 = Lt(i + vec3(0,1,0), n), c110 = Lt(i + vec3(1,1,0), n);\n"
        "               graded = c000 + f.g * (c010 - c000) + f.r * (c110 - c010) + f.b * (c111 - c110); }\n"
        "         }\n"
        "      }\n"
        "   }\n"
        "   fragColor = vec4(mix(c.rgb, graded, uMix), c.a);\n"
        "}\n",
        { N("%.0f", P("LUT Size", "uSize", T::Float, 2.0f, 64.0f, 16.0f)),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f),
          OLD(0.0f, E("interpolation", "uInterp", { "Original", "Trilinear", "Tetrahedral" }, 2)) },
        2 },

      { "gradientmap", "Compositing",
        "uniform vec3 uShadowColor;\n"
        "uniform vec3 uHighlightColor;\n"
        "uniform float uMix;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   vec3 mapped = mix(uShadowColor, uHighlightColor, lum);\n"
        "   fragColor = vec4(mix(c.rgb, mapped, uMix), c.a);\n"
        "}\n",
        { P("Shadow", "uShadowColor", T::Color, 0.0f, 1.0f, 0.1f, 0.0f, 0.3f),
          P("Highlight", "uHighlightColor", T::Color, 0.0f, 1.0f, 1.0f, 0.85f, 0.4f),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f) } },

      { "color adjustments", "Compositing",
        // All-in-one grading node consolidating the separate brightness/contrast,
        // levels, color balance, hsl, vibrance, tone shaper, channelmixer and
        // blackandwhite nodes into a single chain, so a common grade doesn't
        // need eight nodes wired in series. Pipeline order follows the usual
        // DaVinci Resolve / Photoshop primaries -> HSL -> channel mixer -> B&W
        // stacking:
        //   1) Brightness/contrast   5) Vibrance (saturation relative to grey)
        //   2) Levels (black/white point + gamma)   6) Tone shaper (S-curve)
        //   3) Color balance (cyan-red/magenta-green/yellow-blue)   7) Channel mixer
        //   4) HSL (hue/sat/lightness)   8) Black & white (optional, always last -
        //      grading after a desaturate step would be pointless, so it only
        //      runs when enabled)
        // Each stage's math is copied verbatim from its former standalone
        // FilterDef - brightnesscontrast, levels, hsl, colorbalance and
        // channelmixer no longer exist standalone (this node fully supersedes
        // them); vibrance and tone shaper never existed standalone (vibrance
        // duplicated HSL's saturation slider, tone shaper duplicated the real
        // per-channel Curves node), and blackandwhite never existed standalone
        // either (it was a special case of Channel Mixer with all three rows
        // set equal).
        std::string(kSharedHelpers) +
        "uniform float uBrightness;\n"
        "uniform float uContrast;\n"
        "uniform float uBlackPoint;\n"
        "uniform float uWhitePoint;\n"
        "uniform float uGamma;\n"
        "uniform float uCyanRed;\n"
        "uniform float uMagentaGreen;\n"
        "uniform float uYellowBlue;\n"
        "uniform float uHueShift;\n"
        "uniform float uSaturation;\n"
        "uniform float uLightness;\n"
        "uniform float uVibrance;\n"
        "uniform float uShadows;\n"
        "uniform float uMidtones;\n"
        "uniform float uHighlights;\n"
        "uniform float uContrastPivot;\n"
        "uniform vec3 uRedRow;\n"
        "uniform vec3 uGreenRow;\n"
        "uniform vec3 uBlueRow;\n"
        "uniform int uBWEnabled;\n"
        "uniform float uRedWeight;\n"
        "uniform float uGreenWeight;\n"
        "uniform float uBlueWeight;\n"
        "uniform int uSatModel;\n"
        "uniform float uSlopeR, uSlopeG, uSlopeB;\n"
        "uniform float uOffsetR, uOffsetG, uOffsetB;\n"
        "uniform float uPowerR, uPowerG, uPowerB;\n"
        "uniform float uCdlSat;\n"
        "float toneCurve(float x) {\n"
        "   float s = mix(x, x*x*(3.0-2.0*x), uContrastPivot);\n"
        "   float lift = uShadows * (1.0 - x) * (1.0 - x);\n"
        "   float gain = uHighlights * x * x;\n"
        "   float mid = uMidtones * 4.0 * x * (1.0 - x);\n"
        "   return clamp(s + lift + gain + mid, 0.0, 1.0);\n"
        "}\n"
        // headroom: the curve shapes 0..1, light above white carries on at unit slope
        "float toneCurveH(float x) { return uImgHeadroom == 1 && x > 1.0 ? toneCurve(1.0) + x - 1.0 : toneCurve(x); }\n"
        // Photoshop's modern Brightness, the 2D View's (PhotoCraft adjust/tone.rs Brightness, MIT/Apache-2.0)
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
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = c.rgb;\n"
        "\n"
        "   // Modern non-clipping brightness + smooth Hermite contrast (matches 2D View)\n"
        "   if (abs(uBrightness) > 1e-4) {\n"
        "      vec3 bv = clamp(col, 0.0, 1.0);\n"
        "      float bb = clamp(uBrightness, -1.0, 1.0) * 150.0;\n"
        "      vec3 curved = vec3(ModernBright(bv.r, bb), ModernBright(bv.g, bb), ModernBright(bv.b, bb));\n"
        "      col = uImgHeadroom == 1 ? mix(curved, col + uBrightness, step(vec3(1.0), col)) : curved;\n"
        "   }\n"
        "   if (abs(uContrast) > 1e-4) {\n"
        "      float k = clamp(uContrast, -0.95, 1.0) * 0.78, ed = 1.0 - k, md = 1.0 + k;\n"
        "      vec3 v = clamp(col, 0.0, 1.0), lo = step(v, vec3(0.5)), t = mix((1.0 - v) * 2.0, v * 2.0, lo);\n"
        "      vec3 t2 = t * t, t3 = t2 * t;\n"
        "      vec3 h = ed * 0.5 * (t3 - 2.0 * t2 + t) + (3.0 * t2 - 2.0 * t3) * 0.5 + md * 0.5 * (t3 - t2);\n"
        "      col = mix(1.0 - h, h, lo);\n"
        "   }\n"
        "\n"
        "   col = ImgClip((col - uBlackPoint) / max(uWhitePoint - uBlackPoint, 0.0001));\n"
        "   col = pow(col, vec3(1.0 / max(uGamma, 0.0001)));\n"
        "\n"
        "   // Midtone-anchored color balance so pure black (0) and pure white (1) stay clean\n"
        "   vec3 cbW = 4.0 * clamp(col, 0.0, 1.0) * (1.0 - clamp(col, 0.0, 1.0));\n"
        "   col = ImgClip(col + vec3(uCyanRed, uMagentaGreen, uYellowBlue) * mix(vec3(0.25), vec3(1.0), cbW));\n"
        "\n"
        "   vec3 hsv = rgb2hsv(col);\n"
        "   hsv.x = fract(hsv.x + uHueShift);\n"
        "   if (uSatModel == 0) hsv.y = clamp(hsv.y * uSaturation, 0.0, 1.0);\n"
        "   col = hsv2rgb(hsv);\n"
        // Rec.709: scale away from luminance in linear light, so saturation 0
        // is the grey of equal brightness (HSV keeps V = max, which is white
        // for a pure primary).
        "   if (uSatModel == 1 && uSaturation != 1.0) {\n"
        "      vec3 lin = SrgbDecode(col);\n"
        "      float y = dot(lin, vec3(0.2126, 0.7152, 0.0722));\n"
        "      col = SrgbEncode(ImgClip(y + (lin - y) * uSaturation));\n"
        "   }\n"
        "   col = ImgClip(col + uLightness);\n"
        "\n"
        "   float avgc = (col.r + col.g + col.b) / 3.0;\n"
        "   col = ImgClip(mix(vec3(avgc), col, 1.0 + uVibrance));\n"
        "\n"
        "   col = vec3(toneCurveH(col.r), toneCurveH(col.g), toneCurveH(col.b));\n"
        "\n"
        "   col = ImgClip(vec3(dot(col, uRedRow), dot(col, uGreenRow), dot(col, uBlueRow)));\n"
        "\n"
        "   if (uBWEnabled == 1) {\n"
        "      float lum = col.r*uRedWeight + col.g*uGreenWeight + col.b*uBlueWeight;\n"
        "      col = vec3(lum);\n"
        "   }\n"
        "\n"
        // ASC CDL v1.2: out = clamp(in * slope + offset)^power, then
        // saturation about Rec.709 luma of the result. Last, so a CDL from
        // set or a colourist reads the same here as in Resolve or Nuke.
        "   vec3 slope = vec3(uSlopeR, uSlopeG, uSlopeB);\n"
        "   vec3 offset = vec3(uOffsetR, uOffsetG, uOffsetB);\n"
        "   vec3 power = vec3(uPowerR, uPowerG, uPowerB);\n"
        "   if (slope != vec3(1.0) || offset != vec3(0.0) || power != vec3(1.0) || uCdlSat != 1.0) {\n"
        "      col = pow(clamp(col * slope + offset, 0.0, 1.0), power);\n"
        "      float l = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
        "      col = clamp(l + (col - l) * uCdlSat, 0.0, 1.0);\n"
        "   }\n"
        "\n"
        "   fragColor = vec4(col, c.a);\n"
        "}\n",
        { S("Brightness / Contrast", P("Brightness", "uBrightness", T::Float, -1.0f, 1.0f, 0.0f)),
          P("Contrast", "uContrast", T::Float, -1.0f, 3.0f, 0.0f),
          S("Levels", P("Black Point", "uBlackPoint", T::Float, 0.0f, 1.0f, 0.0f)),
          P("White Point", "uWhitePoint", T::Float, 0.0f, 1.0f, 1.0f),
          P("Gamma", "uGamma", T::Float, 0.1f, 4.0f, 1.0f),
          S("Color Balance", P("Cyan-Red", "uCyanRed", T::Float, -0.5f, 0.5f, 0.0f)),
          P("Magenta-Green", "uMagentaGreen", T::Float, -0.5f, 0.5f, 0.0f),
          P("Yellow-Blue", "uYellowBlue", T::Float, -0.5f, 0.5f, 0.0f),
          S("HSL", P("Hue Shift", "uHueShift", T::Float, 0.0f, 1.0f, 0.0f)),
          P("Saturation", "uSaturation", T::Float, 0.0f, 3.0f, 1.0f),
          P("Lightness", "uLightness", T::Float, -1.0f, 1.0f, 0.0f),
          S("Vibrance", P("Amount", "uVibrance", T::Float, -1.0f, 2.0f, 0.0f)),
          S("Tone Shaper", P("Shadows", "uShadows", T::Float, -0.5f, 0.5f, 0.0f)),
          P("Midtones", "uMidtones", T::Float, -0.5f, 0.5f, 0.0f),
          P("Highlights", "uHighlights", T::Float, -0.5f, 0.5f, 0.0f),
          P("S-Curve", "uContrastPivot", T::Float, 0.0f, 1.0f, 0.0f),
          S("Channel Mixer", P("Red from RGB", "uRedRow", T::Color, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f)),
          P("Green from RGB", "uGreenRow", T::Color, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f),
          P("Blue from RGB", "uBlueRow", T::Color, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
          S("Black & White", P("Enable", "uBWEnabled", T::Bool, 0.0f, 1.0f, 0.0f)),
          P("Red Weight", "uRedWeight", T::Float, 0.0f, 2.0f, 0.299f),
          P("Green Weight", "uGreenWeight", T::Float, 0.0f, 2.0f, 0.587f),
          P("Blue Weight", "uBlueWeight", T::Float, 0.0f, 2.0f, 0.114f),
          S("Saturation", OLD(0.0f, E("model", "uSatModel", { "HSV", "Rec.709" }, 1))),
          S("ASC CDL", P("Slope R", "uSlopeR", T::Float, 0.0f, 4.0f, 1.0f)),
          P("Slope G", "uSlopeG", T::Float, 0.0f, 4.0f, 1.0f),
          P("Slope B", "uSlopeB", T::Float, 0.0f, 4.0f, 1.0f),
          P("Offset R", "uOffsetR", T::Float, -1.0f, 1.0f, 0.0f),
          P("Offset G", "uOffsetG", T::Float, -1.0f, 1.0f, 0.0f),
          P("Offset B", "uOffsetB", T::Float, -1.0f, 1.0f, 0.0f),
          P("Power R", "uPowerR", T::Float, 0.1f, 4.0f, 1.0f),
          P("Power G", "uPowerG", T::Float, 0.1f, 4.0f, 1.0f),
          P("Power B", "uPowerB", T::Float, 0.1f, 4.0f, 1.0f),
          P("Saturation", "uCdlSat", T::Float, 0.0f, 4.0f, 1.0f) } },

      // ---------------- Compositing: layer-effect style filters ----------------
      { "outerglow", "Compositing",
        "uniform float uAmount;\n"
        "uniform vec3 uColor;\n"
        "void main() {\n"
        "   vec4 c = ImgSrc(vUv);\n"
        "   vec4 blur = vec4(0.0); float total = 0.0;\n"
        "   for (int x = -3; x <= 3; x++) for (int y = -3; y <= 3; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * 4.0;\n"
        "      float w = exp(-float(x*x + y*y) / 6.0);\n"
        "      blur += texture(uSrc, vUv + off) * w; total += w;\n"
        "   }\n"
        "   blur /= total;\n"
        "   vec3 glow = ImgDecode(uColor) * blur.a * uAmount;\n"
        "   fragColor = vec4(c.rgb + glow * (1.0 - c.a), max(c.a, blur.a * uAmount));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 1.0f),
          P("Color", "uColor", T::Color, 0.0f, 1.0f, 1.0f, 0.9f, 0.3f), LIN() } },

      { "coloroverlay", "Compositing",
        "uniform vec3 uColor;\n"
        "uniform float uOpacity;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = mix(c.rgb, uColor, uOpacity);\n"
        "   fragColor = vec4(col, c.a);\n"
        "}\n",
        { P("Color", "uColor", T::Color, 0.0f, 1.0f, 1.0f, 0.2f, 0.2f),
          P("Opacity", "uOpacity", T::Float, 0.0f, 1.0f, 0.5f) } },

      { "dropshadow", "Compositing",
        "uniform float uOffsetX;\n"
        "uniform float uOffsetY;\n"
        "uniform vec3 uColor;\n"
        "uniform float uOpacity;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec2 off = vec2(uOffsetX, uOffsetY) * uTexelSize * 10.0;\n"
        "   vec4 shadowSample = texture(uSrc, vUv - off);\n"
        "   vec3 shadow = uColor * shadowSample.a * uOpacity;\n"
        "   fragColor = vec4(mix(shadow, c.rgb, c.a), max(c.a, shadowSample.a * uOpacity));\n"
        "}\n",
        { F("%.1f px", P("Offset X", "uOffsetX", T::Float, -20.0f, 20.0f, 4.0f)),
          F("%.1f px", P("Offset Y", "uOffsetY", T::Float, -20.0f, 20.0f, 4.0f)),
          P("Color", "uColor", T::Color, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
          P("Opacity", "uOpacity", T::Float, 0.0f, 1.0f, 0.6f) } },

      // ---------------- Effects: the PhotoCraft Filter menu (2D View Add sheet, owner yes 2026-11-27) ----------------
      // Filter > Pixelate > Crystallize: each pixel takes the colour at the nearest of a jittered grid of seeds (Voronoi cells).
      { "crystallize", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "uniform float uCell;\n"
        "void main() {\n"
        "   vec2 px = vUv / uTexelSize; float s = max(uCell, 1.0);\n"
        "   vec2 g = floor(px / s), bp = px; float best = 1e20;\n"
        "   for (int j = -1; j <= 1; j++)\n"
        "      for (int i = -1; i <= 1; i++) {\n"
        "         vec2 c = g + vec2(float(i), float(j)), p = (c + H22(c)) * s;\n"
        "         float d = dot(p - px, p - px);\n"
        "         if (d < best) { best = d; bp = p; }\n"
        "      }\n"
        "   fragColor = ImgSrc(clamp((floor(bp) + 0.5) * uTexelSize, vec2(0.0), vec2(1.0)));\n"
        "}\n",
        { N("%.0f px", P("Cell Size", "uCell", T::Float, 3.0f, 300.0f, 10.0f)), LIN() } },

      // Filter > Pixelate > Facet: flat patches of like colour (a small Kuwahara: the quietest quarter of the neighbourhood wins).
      { "facet", "Effects",
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "vec4 Kuwahara(int r) {\n"
        "   vec4 m[4]; float v[4];\n"
        "   for (int q = 0; q < 4; q++) {\n"
        "      vec2 sg = vec2((q == 1 || q == 3) ? -1.0 : 1.0, q >= 2 ? -1.0 : 1.0);\n"
        "      vec4 s = vec4(0.0); float s2 = 0.0, n = 0.0;\n"
        "      for (int j = 0; j <= 8; j++) {\n"
        "         if (j > r) break;\n"
        "         for (int i = 0; i <= 8; i++) {\n"
        "            if (i > r) break;\n"
        "            vec4 c = ImgPre(vUv + sg * vec2(float(i), float(j)) * uTexelSize);\n"
        "            float l = clamp(Luma(c.rgb), -4.0, 4.0);\n"
        "            s += c; s2 += l * l; n += 1.0;\n"
        "         }\n"
        "      }\n"
        "      m[q] = s / n;\n"
        "      float ml = clamp(Luma(m[q].rgb), -4.0, 4.0);\n"
        "      v[q] = s2 / n - ml * ml;\n"
        "   }\n"
        "   int b = 0;\n"
        "   for (int q = 1; q < 4; q++) if (v[q] < v[b]) b = q;\n"
        "   return m[b];\n"
        "}\n"
        "void main() { fragColor = ImgUnpremul(Kuwahara(2)); }\n",
        { LIN() } },

      // Filter > Pixelate > Fragment: four copies of the picture offset around each pixel and averaged.
      { "fragment", "Effects",
        "uniform float uDistance;\n"
        "void main() {\n"
        "   vec2 d = uDistance * uTexelSize;\n"
        "   vec4 s = ImgPre(vUv + vec2(d.x, 0.0)) + ImgPre(vUv - vec2(d.x, 0.0)) + ImgPre(vUv + vec2(0.0, d.y)) + ImgPre(vUv - vec2(0.0, d.y));\n"
        "   fragColor = ImgUnpremul(s * 0.25);\n"
        "}\n",
        { F("%.1f px", P("Distance", "uDistance", T::Float, 1.0f, 20.0f, 4.0f)), LIN() } },

      // Filter > Pixelate > Mezzotint: each channel becomes full or empty against a random screen of dots, lines or strokes.
      { "mezzotint", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "uniform int uType;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec2 px = floor(vUv / uTexelSize);\n"
        "   vec2 cell = px;\n"
        "   if (uType == 1) cell = floor(px / 2.0);\n"
        "   else if (uType == 2) cell = floor(px / 3.0);\n"
        "   else if (uType == 3) cell = vec2(floor(px.x / 4.0), px.y);\n"
        "   else if (uType == 4) cell = vec2(floor(px.x / 12.0), px.y);\n"
        "   else if (uType == 5) cell = vec2(floor((px.x + px.y) / 6.0), px.x - px.y);\n"
        "   vec3 o;\n"
        "   for (int k = 0; k < 3; k++) o[k] = step(H21(cell + float(k) * 31.7), clamp(c[k], 0.0, 1.0));\n"
        "   fragColor = vec4(o, c.a);\n"
        "}\n",
        { E("Type", "uType", { "Fine Dots", "Medium Dots", "Coarse Dots", "Short Lines", "Long Lines", "Short Strokes" }, 0) } },

      // Filter > Pixelate > Pointillize: round dots of the picture's colour, jittered, on the background colour.
      { "pointillize", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "uniform float uCell;\n"
        "uniform vec3 uBack;\n"
        "void main() {\n"
        "   vec2 px = vUv / uTexelSize; float s = max(uCell, 1.0);\n"
        "   vec2 g = floor(px / s), bp = px; float best = 1e20;\n"
        "   for (int j = -1; j <= 1; j++)\n"
        "      for (int i = -1; i <= 1; i++) {\n"
        "         vec2 c = g + vec2(float(i), float(j)), p = (c + 0.2 + 0.6 * H22(c)) * s;\n"
        "         float d = dot(p - px, p - px);\n"
        "         if (d < best) { best = d; bp = p; }\n"
        "      }\n"
        "   float r = 0.5 * s, d = sqrt(best);\n"
        "   float in_ = clamp(r - d + 0.5, 0.0, 1.0);\n"
        "   vec4 dot_ = ImgSrc(clamp((floor(bp) + 0.5) * uTexelSize, vec2(0.0), vec2(1.0)));\n"
        "   vec4 here = ImgSrc(vUv);\n"
        "   vec4 back = vec4(ImgDecode(uBack), here.a);\n"
        "   float a = mix(back.a, dot_.a, in_);\n"
        "   vec3 pre = mix(back.rgb * back.a, dot_.rgb * dot_.a, in_);\n"
        "   fragColor = vec4(a > 0.0 ? pre / a : vec3(0.0), a);\n"
        "}\n",
        { N("%.0f px", P("Cell Size", "uCell", T::Float, 3.0f, 300.0f, 8.0f)), P("Background", "uBack", T::Color, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f), LIN() } },

      // Filter > Stylize > Diffuse: each pixel swapped with a random neighbour (Darken / Lighten Only keep the swap only when it is darker / lighter).
      { "diffuse", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "uniform int uMode;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec2 px = floor(vUv / uTexelSize);\n"
        "   vec2 o = floor((H22(px) * 2.0 - 1.0) * uRadius + 0.5);\n"
        "   vec4 c = ImgSrc(vUv), n = ImgSrc(vUv + o * uTexelSize);\n"
        "   float lc = Luma(c.rgb) * c.a, ln = Luma(n.rgb) * n.a;\n"
        "   bool take = uMode == 0 || (uMode == 1 && ln < lc) || (uMode == 2 && ln > lc);\n"
        "   fragColor = take ? n : c;\n"
        "}\n",
        { E("Mode", "uMode", { "Normal", "Darken Only", "Lighten Only" }, 0), F("%.0f px", P("Radius", "uRadius", T::Float, 1.0f, 8.0f, 2.0f)), LIN() } },

      // Filter > Stylize > Extrude: the picture cut into square blocks or pyramids, each lit from the top left.
      { "extrude", "Effects",
        "uniform int uType;\n"
        "uniform float uSize;\n"
        "uniform float uDepth;\n"
        "void main() {\n"
        "   vec2 px = vUv / uTexelSize; float s = max(uSize, 2.0);\n"
        "   vec2 cell = floor(px / s), p = (px - (cell + 0.5) * s) / (0.5 * s);\n"
        "   vec4 c = ImgSrc(clamp((floor((cell + 0.5) * s) + 0.5) * uTexelSize, vec2(0.0), vec2(1.0)));\n"
        "   float shade = 1.0;\n"
        "   bool side = uType == 1 || max(abs(p.x), abs(p.y)) > 1.0 - uDepth;\n"
        "   if (side) {\n"
        "      if (abs(p.x) > abs(p.y)) shade = p.x < 0.0 ? 1.25 : 0.7;\n"
        "      else shade = p.y > 0.0 ? 1.15 : 0.55;\n"
        "   }\n"
        "   fragColor = vec4(c.rgb * shade, c.a);\n"
        "}\n",
        { E("Type", "uType", { "Blocks", "Pyramids" }, 0), N("%.0f px", P("Size", "uSize", T::Float, 2.0f, 255.0f, 30.0f)), P("Depth", "uDepth", T::Float, 0.05f, 0.9f, 0.3f), LIN() } },

      // Filter > Stylize > Oil Paint: flat brush patches (Kuwahara, Stylization the radius) with Shine lighting the strokes' relief.
      { "oil paint", "Effects",
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "vec4 Kuwahara(int r) {\n"
        "   vec4 m[4]; float v[4];\n"
        "   for (int q = 0; q < 4; q++) {\n"
        "      vec2 sg = vec2((q == 1 || q == 3) ? -1.0 : 1.0, q >= 2 ? -1.0 : 1.0);\n"
        "      vec4 s = vec4(0.0); float s2 = 0.0, n = 0.0;\n"
        "      for (int j = 0; j <= 8; j++) {\n"
        "         if (j > r) break;\n"
        "         for (int i = 0; i <= 8; i++) {\n"
        "            if (i > r) break;\n"
        "            vec4 c = ImgPre(vUv + sg * vec2(float(i), float(j)) * uTexelSize);\n"
        "            float l = clamp(Luma(c.rgb), -4.0, 4.0);\n"
        "            s += c; s2 += l * l; n += 1.0;\n"
        "         }\n"
        "      }\n"
        "      m[q] = s / n;\n"
        "      float ml = clamp(Luma(m[q].rgb), -4.0, 4.0);\n"
        "      v[q] = s2 / n - ml * ml;\n"
        "   }\n"
        "   int b = 0;\n"
        "   for (int q = 1; q < 4; q++) if (v[q] < v[b]) b = q;\n"
        "   return m[b];\n"
        "}\n"
        "uniform float uStyle;\n"
        "uniform float uShine;\n"
        "uniform float uAngle;\n"
        "void main() {\n"
        "   vec4 k = ImgUnpremul(Kuwahara(int(clamp(uStyle, 1.0, 8.0) + 0.5)));\n"
        "   vec2 t = uTexelSize * 2.0;\n"
        "   float gx = Luma(ImgPre(vUv + vec2(t.x, 0.0)).rgb) - Luma(ImgPre(vUv - vec2(t.x, 0.0)).rgb);\n"
        "   float gy = Luma(ImgPre(vUv + vec2(0.0, t.y)).rgb) - Luma(ImgPre(vUv - vec2(0.0, t.y)).rgb);\n"
        "   float lit = clamp(dot(vec2(gx, gy), vec2(cos(uAngle), sin(uAngle))), -1.0, 1.0);\n"
        "   fragColor = vec4(max(k.rgb * (1.0 + lit * uShine * 2.0), vec3(0.0)), k.a);\n"
        "}\n",
        { N("%.0f px", P("Stylization", "uStyle", T::Float, 1.0f, 8.0f, 4.0f)), P("Shine", "uShine", T::Float, 0.0f, 1.0f, 0.3f), P("Angle", "uAngle", T::Float, -180.0f, 180.0f, 135.0f, 0.0f, 0.0f, true), LIN() } },

      // Filter > Stylize > Tiles: the picture cut into square tiles, each nudged a random way; the gaps take the fill.
      { "tiles", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "uniform float uCount;\n"
        "uniform float uOffset;\n"
        "uniform int uFill;\n"
        "uniform vec3 uBack;\n"
        "void main() {\n"
        "   vec2 px = vUv / uTexelSize; float s = max(1.0 / (uTexelSize.x * max(uCount, 1.0)), 2.0);\n"
        "   vec2 g = floor(px / s), src = vec2(-1.0); bool hit = false;\n"
        "   for (int j = -1; j <= 1; j++)\n"
        "      for (int i = -1; i <= 1; i++) {\n"
        "         vec2 c = g + vec2(float(i), float(j));\n"
        "         vec2 off = (H22(c) * 2.0 - 1.0) * uOffset * s;\n"
        "         vec2 q = px - off;\n"
        "         if (all(greaterThanEqual(q, c * s)) && all(lessThan(q, (c + 1.0) * s))) { src = q; hit = true; }\n"
        "      }\n"
        "   vec4 here = ImgSrc(vUv);\n"
        "   if (hit) fragColor = ImgSrc(clamp((floor(src) + 0.5) * uTexelSize, vec2(0.0), vec2(1.0)));\n"
        "   else if (uFill == 0) fragColor = vec4(ImgDecode(uBack), 1.0);\n"
        "   else if (uFill == 1) fragColor = vec4(ImgDecode(1.0 - ImgEncode(clamp(here.rgb, 0.0, 1.0))), here.a);\n"
        "   else if (uFill == 2) fragColor = here;\n"
        "   else fragColor = vec4(0.0);\n"
        "}\n",
        { N("%.0f", P("Tiles", "uCount", T::Float, 1.0f, 99.0f, 10.0f)), P("Offset", "uOffset", T::Float, 0.0f, 0.5f, 0.1f), E("Fill", "uFill", { "Background Colour", "Inverse", "Unaltered", "Transparent" }, 0), P("Background", "uBack", T::Color, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f), LIN() } },

      // Filter > Stylize > Trace Contour: thin lines, per channel, where the brightness crosses Level (white elsewhere).
      { "trace contour", "Effects",
        "uniform float uLevel;\n"
        "uniform int uEdge;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec4 cr = texture(uSrc, vUv + vec2(uTexelSize.x, 0.0)), cd = texture(uSrc, vUv - vec2(0.0, uTexelSize.y));\n"
        "   vec3 a = clamp(c.rgb * c.a, 0.0, 1.0) * 255.0;\n"
        "   vec3 r = clamp(cr.rgb * cr.a, 0.0, 1.0) * 255.0;\n"
        "   vec3 d = clamp(cd.rgb * cd.a, 0.0, 1.0) * 255.0;\n"
        "   vec3 o;\n"
        "   for (int k = 0; k < 3; k++) {\n"
        "      bool lo = a[k] < uLevel;\n"
        "      bool cross = lo != (r[k] < uLevel) || lo != (d[k] < uLevel);\n"
        "      bool mine = uEdge == 0 ? lo : !lo;\n"
        "      o[k] = cross && mine ? 0.0 : 1.0;\n"
        "   }\n"
        "   fragColor = vec4(o, c.a);\n"
        "}\n",
        { N("%.0f", P("Level", "uLevel", T::Float, 0.0f, 255.0f, 128.0f)), E("Edge", "uEdge", { "Lower", "Upper" }, 0) } },

      // Filter > Stylize > Wind: bright edges streak along the wind (Blast is longer, Stagger varies each row).
      { "wind streaks", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "uniform int uMethod;\n"
        "uniform int uDir;\n"
        "void main() {\n"
        "   float L = uMethod == 1 ? 40.0 : (uMethod == 2 ? 24.0 : 14.0);\n"
        "   float row = floor(vUv.y / uTexelSize.y);\n"
        "   if (uMethod == 2) L *= 0.3 + H21(vec2(row, 7.0));\n"
        "   float dir = uDir == 0 ? 1.0 : -1.0;\n"
        "   vec4 o = ImgPre(vUv);\n"
        "   for (int k = 1; k <= 40; k++) {\n"
        "      float fk = float(k);\n"
        "      if (fk > L) break;\n"
        "      vec4 s = ImgPre(vUv + vec2(dir * fk * uTexelSize.x, 0.0));\n"
        "      float w = 1.0 - fk / L;\n"
        "      if (Luma(s.rgb) * w > Luma(o.rgb)) o = mix(o, s, w * 0.85);\n"
        "   }\n"
        "   fragColor = ImgUnpremul(o);\n"
        "}\n",
        { E("Method", "uMethod", { "Wind", "Blast", "Stagger" }, 0), E("Direction", "uDir", { "From the Right", "From the Left" }, 0), LIN() } },

      // Filter > Blur Gallery > Tilt-Shift: a sharp band across the picture, blurring to Blur outside it (a disc lens blur).
      { "tilt shift", "Effects",
        "vec4 Disc(vec2 uv, float r) {\n"
        "   if (r < 0.5) return ImgPre(uv);\n"
        "   vec4 s = vec4(0.0);\n"
        "   for (int i = 0; i < 48; i++) {\n"
        "      float f = (float(i) + 0.5) / 48.0, a = float(i) * 2.39996;\n"
        "      s += ImgPre(uv + vec2(cos(a), sin(a)) * sqrt(f) * r * uTexelSize);\n"
        "   }\n"
        "   return s / 48.0;\n"
        "}\n"
        "uniform float uBlur;\n"
        "uniform float uCenterY;\n"
        "uniform float uAngle;\n"
        "uniform float uFocus;\n"
        "uniform float uTransition;\n"
        "void main() {\n"
        "   vec2 size = 1.0 / uTexelSize;\n"
        "   vec2 p = (vUv - vec2(0.5, uCenterY)) * size;\n"
        "   vec2 n = vec2(-sin(uAngle), cos(uAngle));\n"
        "   float d = abs(dot(p, n)) / size.y;\n"
        "   float m = smoothstep(uFocus, uFocus + max(uTransition, 1e-4), d);\n"
        "   fragColor = ImgUnpremul(Disc(vUv, uBlur * m));\n"
        "}\n",
        { F("%.0f px", P("Blur", "uBlur", T::Float, 0.0f, 60.0f, 15.0f)), P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f), P("Angle", "uAngle", T::Float, -90.0f, 90.0f, 0.0f, 0.0f, 0.0f, true), P("Focus", "uFocus", T::Float, 0.0f, 0.5f, 0.08f), P("Transition", "uTransition", T::Float, 0.0f, 0.5f, 0.15f), LIN() } },

      // Filter > Blur Gallery > Iris Blur: sharp inside an ellipse, blurring to Blur past its edge (Feather sets how soon).
      { "iris blur", "Effects",
        "vec4 Disc(vec2 uv, float r) {\n"
        "   if (r < 0.5) return ImgPre(uv);\n"
        "   vec4 s = vec4(0.0);\n"
        "   for (int i = 0; i < 48; i++) {\n"
        "      float f = (float(i) + 0.5) / 48.0, a = float(i) * 2.39996;\n"
        "      s += ImgPre(uv + vec2(cos(a), sin(a)) * sqrt(f) * r * uTexelSize);\n"
        "   }\n"
        "   return s / 48.0;\n"
        "}\n"
        "uniform float uBlur;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "uniform float uRadiusX;\n"
        "uniform float uRadiusY;\n"
        "uniform float uFeather;\n"
        "void main() {\n"
        "   float e = length((vUv - vec2(uCenterX, uCenterY)) / max(vec2(uRadiusX, uRadiusY), vec2(1e-3)));\n"
        "   float m = smoothstep(1.0 - uFeather, 1.0, e);\n"
        "   fragColor = ImgUnpremul(Disc(vUv, uBlur * m));\n"
        "}\n",
        { F("%.0f px", P("Blur", "uBlur", T::Float, 0.0f, 60.0f, 15.0f)), P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f), P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f), P("Radius X", "uRadiusX", T::Float, 0.02f, 1.0f, 0.3f), P("Radius Y", "uRadiusY", T::Float, 0.02f, 1.0f, 0.25f), P("Feather", "uFeather", T::Float, 0.0f, 1.0f, 0.5f), LIN() } },

      // Filter > Blur Gallery > Spin Blur: the picture inside an ellipse turned through Angle and averaged, like a spinning wheel.
      { "spin blur", "Effects",
        "uniform float uAngle;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "uniform float uRadius;\n"
        "uniform float uFeather;\n"
        "void main() {\n"
        "   vec2 size = 1.0 / uTexelSize;\n"
        "   vec2 c = vec2(uCenterX, uCenterY) * size, p = vUv * size - c;\n"
        "   float e = length(p) / max(uRadius * size.y, 1.0);\n"
        "   float m = 1.0 - smoothstep(1.0 - uFeather, 1.0, e);\n"
        "   float a = uAngle * m;\n"
        "   vec4 s = vec4(0.0);\n"
        "   for (int i = 0; i < 32; i++) {\n"
        "      float t = (float(i) + 0.5) / 32.0 - 0.5, r = a * t;\n"
        "      vec2 q = vec2(cos(r) * p.x - sin(r) * p.y, sin(r) * p.x + cos(r) * p.y);\n"
        "      s += ImgPre((c + q) * uTexelSize);\n"
        "   }\n"
        "   fragColor = ImgUnpremul(s / 32.0);\n"
        "}\n",
        { P("Angle", "uAngle", T::Float, 0.0f, 360.0f, 15.0f, 0.0f, 0.0f, true), P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f), P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f), P("Radius", "uRadius", T::Float, 0.02f, 1.5f, 0.35f), P("Feather", "uFeather", T::Float, 0.0f, 1.0f, 0.2f), LIN() } },

      // Filter > Blur Gallery > Field Blur: two pins, each with its own blur; the blur runs between them across the picture.
      { "field blur", "Effects",
        "vec4 Disc(vec2 uv, float r) {\n"
        "   if (r < 0.5) return ImgPre(uv);\n"
        "   vec4 s = vec4(0.0);\n"
        "   for (int i = 0; i < 48; i++) {\n"
        "      float f = (float(i) + 0.5) / 48.0, a = float(i) * 2.39996;\n"
        "      s += ImgPre(uv + vec2(cos(a), sin(a)) * sqrt(f) * r * uTexelSize);\n"
        "   }\n"
        "   return s / 48.0;\n"
        "}\n"
        "uniform float uBlurA;\n"
        "uniform float uAX;\n"
        "uniform float uAY;\n"
        "uniform float uBlurB;\n"
        "uniform float uBX;\n"
        "uniform float uBY;\n"
        "void main() {\n"
        "   vec2 size = 1.0 / uTexelSize;\n"
        "   vec2 a = vec2(uAX, uAY) * size, b = vec2(uBX, uBY) * size, p = vUv * size;\n"
        "   vec2 ab = b - a;\n"
        "   float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1.0), 0.0, 1.0);\n"
        "   fragColor = ImgUnpremul(Disc(vUv, mix(uBlurA, uBlurB, t)));\n"
        "}\n",
        { F("%.0f px", P("Blur A", "uBlurA", T::Float, 0.0f, 60.0f, 0.0f)), P("A X", "uAX", T::Float, 0.0f, 1.0f, 0.5f), P("A Y", "uAY", T::Float, 0.0f, 1.0f, 0.3f), F("%.0f px", P("Blur B", "uBlurB", T::Float, 0.0f, 60.0f, 20.0f)), P("B X", "uBX", T::Float, 0.0f, 1.0f, 0.5f), P("B Y", "uBY", T::Float, 0.0f, 1.0f, 0.8f), LIN() } },

      // Filter > Blur Gallery > Path Blur: motion blur along a curved path (Bend bows it, Angle points it).
      { "path blur", "Effects",
        "uniform float uLength;\n"
        "uniform float uAngle;\n"
        "uniform float uBend;\n"
        "void main() {\n"
        "   vec2 d = vec2(cos(uAngle), sin(uAngle)), n = vec2(-d.y, d.x);\n"
        "   vec4 s = vec4(0.0);\n"
        "   for (int i = 0; i < 32; i++) {\n"
        "      float t = (float(i) + 0.5) / 32.0 - 0.5;\n"
        "      vec2 o = d * uLength * t + n * uBend * uLength * (t * t - 1.0 / 12.0);\n"
        "      s += ImgPre(vUv + o * uTexelSize);\n"
        "   }\n"
        "   fragColor = ImgUnpremul(s / 32.0);\n"
        "}\n",
        { F("%.0f px", P("Length", "uLength", T::Float, 0.0f, 200.0f, 40.0f)), P("Angle", "uAngle", T::Float, -180.0f, 180.0f, 0.0f, 0.0f, 0.0f, true), P("Bend", "uBend", T::Float, -1.0f, 1.0f, 0.3f), LIN() } },

      // Filter > Blur > Smart Blur: blurs only neighbours within Threshold of the pixel, so edges stay; Edge Only / Overlay draw the edges.
      { "smart blur", "Effects",
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "uniform float uRadius;\n"
        "uniform float uThreshold;\n"
        "uniform int uMode;\n"
        "void main() {\n"
        "   vec4 c = ImgPre(vUv);\n"
        "   float lc = Luma(c.rgb), st = max(uRadius / 3.0, 1.0);\n"
        "   vec4 s = vec4(0.0); float n = 0.0;\n"
        "   for (int j = -3; j <= 3; j++)\n"
        "      for (int i = -3; i <= 3; i++) {\n"
        "         vec4 t = ImgPre(vUv + vec2(float(i), float(j)) * st * uTexelSize);\n"
        "         if (abs(Luma(t.rgb) - lc) <= uThreshold) { s += t; n += 1.0; }\n"
        "      }\n"
        "   vec4 o = ImgUnpremul(s / max(n, 1.0));\n"
        "   if (uMode > 0) {\n"
        "      vec2 t = uTexelSize;\n"
        "      float gx = Luma(ImgPre(vUv + vec2(t.x, 0.0)).rgb) - Luma(ImgPre(vUv - vec2(t.x, 0.0)).rgb);\n"
        "      float gy = Luma(ImgPre(vUv + vec2(0.0, t.y)).rgb) - Luma(ImgPre(vUv - vec2(0.0, t.y)).rgb);\n"
        "      float edge = step(uThreshold, length(vec2(gx, gy)));\n"
        "      o = uMode == 1 ? vec4(vec3(edge), c.a > 0.0 ? 1.0 : 0.0) : vec4(mix(o.rgb, vec3(1.0), edge), o.a);\n"
        "   }\n"
        "   fragColor = o;\n"
        "}\n",
        { F("%.1f px", P("Radius", "uRadius", T::Float, 1.0f, 24.0f, 3.0f)), P("Threshold", "uThreshold", T::Float, 0.0f, 1.0f, 0.1f), E("Mode", "uMode", { "Normal", "Edge Only", "Edge Overlay" }, 0), LIN() } },

      // Filter > Blur > Shape Blur: the mean over a shape (circle, hexagon, star, heart, square, ring, diamond, triangle,
      // cross), like a lens's bokeh. The kernel is PhotoCraft's (crates/algo/src/blur2.rs, MIT/Apache-2.0): every pixel
      // offset (dx, dy) within ceil(Radius) whose (dx, dy) / Radius is inside the shape, so Circle r3 covers 29 pixels and a
      // fractional Square r2.5 covers 7 x 7. Above a reach of 12 the offsets are stepped (25 x 25 taps) to bound the cost.
      { "shape blur", "Effects",
        "uniform float uRadius;\n"
        "uniform int uShape;\n"
        "bool Star(float u, float v) {\n"
        "   bool inside = false;\n"
        "   for (int i = 0, j = 9; i < 10; j = i++) {\n"
        "      float ri = (i % 2 == 0) ? 1.0 : 0.42, rj = (j % 2 == 0) ? 1.0 : 0.42;\n"
        "      float ai = -1.5707963 + float(i) * 0.62831853, aj = -1.5707963 + float(j) * 0.62831853;\n"
        "      vec2 pi = ri * vec2(cos(ai), sin(ai)), pj = rj * vec2(cos(aj), sin(aj));\n"
        "      if ((pi.y > v) != (pj.y > v) && u < (pj.x - pi.x) * (v - pi.y) / (pj.y - pi.y) + pi.x) inside = !inside;\n"
        "   }\n"
        "   return inside;\n"
        "}\n"
        "bool Inside(float u, float v) {\n"  // u right, v down (PhotoCraft image rows)
        "   float d = u * u + v * v;\n"
        "   if (uShape == 0) return d <= 1.0;\n"
        "   if (uShape == 1) { float a = mod(atan(v, u), 1.0471976) - 0.5235988; return sqrt(d) <= 0.8660254 / cos(a); }\n"
        "   if (uShape == 2) return Star(u, v);\n"
        "   if (uShape == 3) { float x = u * 1.2, y = -v * 1.2 + 0.15; float k = x * x + y * y - 1.0; return k * k * k - x * x * y * y * y <= 0.0; }\n"
        "   if (uShape == 5) return d >= 0.36 && d <= 1.0;\n"
        "   if (uShape == 6) return abs(u) + abs(v) <= 1.0;\n"
        "   if (uShape == 7) return v <= 0.75 && abs(u) <= (v + 1.0) / 1.75;\n"
        "   if (uShape == 8) return abs(u) <= 0.3 || abs(v) <= 0.3;\n"
        "   return true;\n"
        "}\n"
        "void main() {\n"
        "   float r = max(uRadius, 0.5);\n"
        "   int n = int(ceil(r)); float st = 1.0;\n"
        "   if (n > 12) { st = r / 12.0; n = 12; }\n"
        "   vec4 s = vec4(0.0); float c = 0.0;\n"
        "   for (int j = -n; j <= n; j++)\n"
        "      for (int i = -n; i <= n; i++) {\n"
        "         vec2 o = vec2(float(i), float(j)) * st;\n"
        "         if (Inside(o.x / r, -o.y / r)) { s += ImgPre(vUv + o * uTexelSize); c += 1.0; }\n"
        "      }\n"
        "   fragColor = c > 0.0 ? ImgUnpremul(s / c) : ImgSrc(vUv);\n"
        "}\n",
        { F("%.0f px", P("Radius", "uRadius", T::Float, 1.0f, 100.0f, 10.0f)),
          E("Shape", "uShape", { "Circle", "Hexagon", "Star", "Heart", "Square", "Ring", "Diamond", "Triangle", "Cross" }, 0), LIN() } },

      // Filter > Distort > ZigZag: ridges rippling out from the centre, turning pixels around it, pushing them out, or both (pond ripples).
      { "zigzag", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRidges;\n"
        "uniform int uStyle;\n"
        "uniform float uCenterX;\n"
        "uniform float uCenterY;\n"
        "void main() {\n"
        "   vec2 size = 1.0 / uTexelSize;\n"
        "   vec2 c = vec2(uCenterX, uCenterY) * size, d = vUv * size - c;\n"
        "   float r = length(d), rn = r / (0.5 * min(size.x, size.y));\n"
        "   float w = sin(rn * uRidges * 6.28318) * uAmount * max(1.0 - rn, 0.0);\n"
        "   vec2 u = r > 1e-3 ? d / r : vec2(0.0), t = vec2(-u.y, u.x);\n"
        "   vec2 o = uStyle == 0 ? t * w : (uStyle == 1 ? u * w : (u + t) * 0.7071 * w);\n"
        "   fragColor = ImgSample((c + d + o) * uTexelSize);\n"
        "}\n",
        { F("%.0f px", P("Amount", "uAmount", T::Float, -100.0f, 100.0f, 10.0f)), N("%.0f", P("Ridges", "uRidges", T::Float, 1.0f, 20.0f, 5.0f)), E("Style", "uStyle", { "Around Centre", "Out From Centre", "Pond Ripples" }, 2), P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f), P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f), LIN() } },

      // Filter > Noise > Reduce Noise: brightness smoothed only between like pixels (Preserve Details), colour noise averaged away, then a light sharpen.
      { "reduce noise", "Effects",
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "uniform float uStrength;\n"
        "uniform float uPreserve;\n"
        "uniform float uColour;\n"
        "uniform float uSharpen;\n"
        "void main() {\n"
        "   vec4 c = ImgPre(vUv);\n"
        "   float yc = Luma(c.rgb);\n"
        "   float sig = max(mix(0.25, 0.02, uPreserve) * uStrength / 10.0, 1e-4);\n"
        "   float ys = 0.0, ws = 0.0, yb = 0.0; vec3 cs = vec3(0.0); float as = 0.0;\n"
        "   for (int j = -2; j <= 2; j++)\n"
        "      for (int i = -2; i <= 2; i++) {\n"
        "         vec4 t = ImgPre(vUv + vec2(float(i), float(j)) * uTexelSize);\n"
        "         float yt = Luma(t.rgb), dy = clamp((yt - yc) / sig, -20.0, 20.0);\n"
        "         float w = exp(-0.5 * dy * dy) * exp(-float(i * i + j * j) / 8.0);\n"
        "         ys += yt * w; ws += w; yb += yt; cs += t.rgb - yt; as += t.a;\n"
        "      }\n"
        "   float yd = mix(yc, ys / max(ws, 1e-6), clamp(uStrength / 10.0, 0.0, 1.0));\n"
        "   yd += uSharpen * (yd - yb / 25.0);\n"
        "   vec3 chroma = mix(c.rgb - yc, cs / 25.0, uColour);\n"
        "   fragColor = ImgUnpremul(vec4(vec3(yd) + chroma, c.a));\n"
        "}\n",
        { P("Strength", "uStrength", T::Float, 0.0f, 10.0f, 6.0f), P("Preserve Details", "uPreserve", T::Float, 0.0f, 1.0f, 0.6f), P("Reduce Colour Noise", "uColour", T::Float, 0.0f, 1.0f, 0.45f), P("Sharpen Details", "uSharpen", T::Float, 0.0f, 1.0f, 0.25f), LIN() } },

      // Filter > Other > HSB/HSL: the picture's RGB written as hue, saturation and brightness (or lightness) channels, or read back from them.
      { "hsb hsl", "Effects",
        "uniform int uMode;\n"
        "vec3 ToHsb(vec3 c) {\n"
        "   float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn, h = 0.0;\n"
        "   if (d > 1e-6) {\n"
        "      if (mx == c.r) h = mod((c.g - c.b) / d, 6.0);\n"
        "      else if (mx == c.g) h = (c.b - c.r) / d + 2.0;\n"
        "      else h = (c.r - c.g) / d + 4.0;\n"
        "   }\n"
        "   return vec3(h / 6.0, mx > 1e-6 ? d / mx : 0.0, mx);\n"
        "}\n"
        "vec3 FromHsb(vec3 c) {\n"
        "   vec3 k = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);\n"
        "   return c.z * mix(vec3(1.0), k, c.y);\n"
        "}\n"
        "vec3 ToHsl(vec3 c) {\n"
        "   vec3 h = ToHsb(c); float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), l = 0.5 * (mx + mn);\n"
        "   float s = (mx - mn) / max(1.0 - abs(2.0 * l - 1.0), 1e-6);\n"
        "   return vec3(h.x, mx - mn > 1e-6 ? clamp(s, 0.0, 1.0) : 0.0, l);\n"
        "}\n"
        "vec3 FromHsl(vec3 c) {\n"
        "   vec3 k = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);\n"
        "   float ch = (1.0 - abs(2.0 * c.z - 1.0)) * c.y;\n"
        "   return c.z + ch * (k - 0.5);\n"
        "}\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 v = clamp(c.rgb, 0.0, 1.0), o;\n"
        "   if (uMode == 0) o = ToHsb(v);\n"
        "   else if (uMode == 1) o = ToHsl(v);\n"
        "   else if (uMode == 2) o = FromHsb(v);\n"
        "   else o = FromHsl(v);\n"
        "   fragColor = vec4(o, c.a);\n"
        "}\n",
        { E("Mode", "uMode", { "RGB to HSB", "RGB to HSL", "HSB to RGB", "HSL to RGB" }, 0) } },

      // Filter > Render > Fibers: woven fibres from the foreground to the background colour (Variance their spread, Strength their length).
      { "fibers", "Effects",
        "float H21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
        "vec2 H22(vec2 p) { float n = H21(p); return vec2(n, H21(p + n + 17.0)); }\n"
        "uniform float uVariance;\n"
        "uniform float uStrength;\n"
        "uniform float uSeed;\n"
        "uniform vec3 uFore;\n"
        "uniform vec3 uBack;\n"
        "float VNoise(vec2 p) {\n"
        "   vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
        "   return mix(mix(H21(i), H21(i + vec2(1.0, 0.0)), f.x), mix(H21(i + vec2(0.0, 1.0)), H21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
        "}\n"
        "void main() {\n"
        "   vec2 px = vUv / uTexelSize + uSeed * 37.0;\n"
        "   vec2 p = vec2(px.x * 0.05 * uVariance / 16.0, px.y * 0.05 / max(uStrength, 1.0));\n"
        "   float v = 0.0, a = 0.5;\n"
        "   for (int k = 0; k < 5; k++) { v += a * VNoise(p); p = p * 2.03 + 11.7; a *= 0.5; }\n"
        "   v = clamp((v - 0.5) * 1.6 + 0.5, 0.0, 1.0);\n"
        "   fragColor = vec4(mix(ImgDecode(uFore), ImgDecode(uBack), v), 1.0);\n"
        "}\n",
        { P("Variance", "uVariance", T::Float, 1.0f, 64.0f, 16.0f), P("Strength", "uStrength", T::Float, 1.0f, 64.0f, 4.0f), N("%.0f", P("Seed", "uSeed", T::Float, 0.0f, 999.0f, 0.0f)), P("Foreground", "uFore", T::Color, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f), P("Background", "uBack", T::Color, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f), LIN() } },

      // Filter > Render > Lens Flare: light added as a camera flare at a point: a hot core, rays, a halo and ghosts toward the centre.
      { "lens flare", "Effects",
        "uniform float uX;\n"
        "uniform float uY;\n"
        "uniform float uBright;\n"
        "uniform int uLens;\n"
        "void main() {\n"
        "   vec2 size = 1.0 / uTexelSize; float asp = size.x / size.y;\n"
        "   vec2 p = vec2(vUv.x * asp, vUv.y), f = vec2(uX * asp, uY), ctr = vec2(0.5 * asp, 0.5);\n"
        "   vec2 d = p - f; float r = length(d);\n"
        "   float core = exp(-r * r * 900.0) * 3.0 + exp(-r * 18.0) * 0.6;\n"
        "   float ang = atan(d.y, d.x + 1e-6);\n"
        "   float rays = pow(abs(cos(ang * (uLens == 3 ? 3.0 : 6.0))), 24.0) * exp(-r * 6.0) * 0.8;\n"
        "   float halo = exp(-pow((r - (uLens == 1 ? 0.16 : 0.12)) * 60.0, 2.0)) * 0.25;\n"
        "   vec3 o = vec3(1.0, 0.92, 0.8) * (core + rays) + vec3(0.6, 0.8, 1.0) * halo;\n"
        "   int ng = uLens == 0 ? 6 : (uLens == 1 ? 4 : (uLens == 2 ? 5 : 3));\n"
        "   for (int k = 0; k < 6; k++) {\n"
        "      if (k >= ng) break;\n"
        "      float t = 0.35 + 0.33 * float(k), s = 0.015 + 0.02 * float((k * 7) % 4);\n"
        "      vec2 g = mix(f, ctr * 2.0 - f, t);\n"
        "      float gd = length(p - g);\n"
        "      vec3 gc = k % 2 == 0 ? vec3(0.4, 0.9, 0.5) : vec3(0.9, 0.5, 0.3);\n"
        "      o += gc * smoothstep(s, s * 0.6, gd) * 0.12;\n"
        "   }\n"
        "   o *= uBright;\n"
        "   vec4 c = ImgSrc(vUv);\n"
        "   float a = clamp(c.a + dot(o, vec3(0.333)), 0.0, 1.0);\n"
        "   vec3 pre = c.rgb * c.a + o;\n"
        "   fragColor = vec4(a > 0.0 ? pre / a : vec3(0.0), a);\n"
        "}\n",
        { P("Center X", "uX", T::Float, 0.0f, 1.0f, 0.3f), P("Center Y", "uY", T::Float, 0.0f, 1.0f, 0.3f), F("%.2f", P("Brightness", "uBright", T::Float, 0.1f, 3.0f, 1.0f)), E("Lens", "uLens", { "50-300mm Zoom", "35mm Prime", "105mm Prime", "Movie Prime" }, 0), LIN() } },

      // Filter > Render > Lighting Effects: a point, spot or infinite light over the picture, its brightness read as bumps.
      { "lighting effects", "Effects",
        "float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "uniform int uType;\n"
        "uniform float uX;\n"
        "uniform float uY;\n"
        "uniform float uHeight;\n"
        "uniform float uAngle;\n"
        "uniform float uIntensity;\n"
        "uniform vec3 uColor;\n"
        "uniform float uAmbient;\n"
        "uniform float uGloss;\n"
        "uniform float uBump;\n"
        "uniform float uSpot;\n"
        "float Ht(vec2 uv) { vec4 c = ImgSrc(uv); return clamp(Luma(c.rgb), 0.0, 1.0) * c.a; }\n"
        "void main() {\n"
        "   vec2 size = 1.0 / uTexelSize; float asp = size.x / size.y;\n"
        "   vec2 t = uTexelSize;\n"
        "   float gx = Ht(vUv + vec2(t.x, 0.0)) - Ht(vUv - vec2(t.x, 0.0));\n"
        "   float gy = Ht(vUv + vec2(0.0, t.y)) - Ht(vUv - vec2(0.0, t.y));\n"
        "   vec3 N = normalize(vec3(-gx * uBump, -gy * uBump, 1.0));\n"
        "   vec2 p = vec2(vUv.x * asp, vUv.y), lp = vec2(uX * asp, uY);\n"
        "   vec3 L; float att = 1.0;\n"
        "   if (uType == 2) L = normalize(vec3(cos(uAngle), sin(uAngle), max(uHeight, 0.02) * 2.0));\n"
        "   else {\n"
        "      vec3 v = vec3(lp - p, max(uHeight, 0.02));\n"
        "      float dd = dot(v.xy, v.xy);\n"
        "      L = normalize(v);\n"
        "      att = 1.0 / (1.0 + dd * 4.0);\n"
        "      if (uType == 1) att *= smoothstep(uSpot, uSpot * 0.6, sqrt(dd));\n"
        "   }\n"
        "   float diff = max(dot(N, L), 0.0);\n"
        "   vec3 Hh = normalize(L + vec3(0.0, 0.0, 1.0));\n"
        "   float spec = pow(max(dot(N, Hh), 0.0), mix(4.0, 64.0, uGloss)) * uGloss;\n"
        "   vec3 lc = ImgDecode(uColor) * uIntensity * att;\n"
        "   vec4 c = ImgSrc(vUv);\n"
        "   fragColor = vec4(max(c.rgb * (uAmbient + diff * lc) + spec * lc, vec3(0.0)), c.a);\n"
        "}\n",
        { E("Light", "uType", { "Point", "Spot", "Infinite" }, 1), P("Light X", "uX", T::Float, 0.0f, 1.0f, 0.35f), P("Light Y", "uY", T::Float, 0.0f, 1.0f, 0.65f), P("Height", "uHeight", T::Float, 0.0f, 1.0f, 0.3f), P("Angle", "uAngle", T::Float, -180.0f, 180.0f, -45.0f, 0.0f, 0.0f, true), F("%.2f", P("Intensity", "uIntensity", T::Float, 0.0f, 3.0f, 1.2f)), P("Color", "uColor", T::Color, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f), P("Ambience", "uAmbient", T::Float, 0.0f, 1.0f, 0.2f), P("Gloss", "uGloss", T::Float, 0.0f, 1.0f, 0.4f), P("Bumps", "uBump", T::Float, 0.0f, 20.0f, 4.0f), P("Spot Size", "uSpot", T::Float, 0.05f, 1.5f, 0.6f), LIN() } },
   };

   return kDefs;
}

// View > Proof Colours / Gamut Warning (the 2D View's soft proof). Not a node: the View2D panel runs it on the canvas
// texture for display only, so saved and exported pixels never change. uTarget: 0 sRGB, 1 Print (CMYK), 2 Grayscale.
// Print is a simulation, not an ICC transform: paper white and ink black squeeze lightness into 0.22..0.98 OKLab L and
// chroma is held under a coated-stock envelope (widest near mid lightness, more room for yellow, less for deep blue).
const FilterDef& GetProofDef()
{
   static const FilterDef kDef = { "proof colours", "View",
      "uniform int uTarget;\n"
      "uniform int uProof;\n"
      "uniform int uWarn;\n"
      "vec3 ToLab(vec3 c) {\n"
      "   vec3 lms = vec3(dot(c, vec3(0.4122214708, 0.5363325363, 0.0514459929)),\n"
      "                   dot(c, vec3(0.2119034982, 0.6806995451, 0.1073969566)),\n"
      "                   dot(c, vec3(0.0883024619, 0.2817188376, 0.6299787005)));\n"
      "   lms = sign(lms) * pow(abs(lms), vec3(1.0 / 3.0));\n"
      "   return vec3(dot(lms, vec3(0.2104542553, 0.7936177850, -0.0040720468)),\n"
      "               dot(lms, vec3(1.9779984951, -2.4285922050, 0.4505937099)),\n"
      "               dot(lms, vec3(0.0259040371, 0.7827717662, -0.8086757660)));\n"
      "}\n"
      "vec3 FromLab(vec3 l) {\n"
      "   vec3 lms = vec3(l.x + 0.3963377774 * l.y + 0.2158037573 * l.z,\n"
      "                   l.x - 0.1055613458 * l.y - 0.0638541728 * l.z,\n"
      "                   l.x - 0.0894841775 * l.y - 1.2914855480 * l.z);\n"
      "   lms = lms * lms * lms;\n"
      "   return vec3(dot(lms, vec3(4.0767416621, -3.3077115913, 0.2309699292)),\n"
      "               dot(lms, vec3(-1.2684380046, 2.6097574011, -0.3413193965)),\n"
      "               dot(lms, vec3(-0.0041960771, -0.7034186147, 1.7076949383)));\n"
      "}\n"
      "float PrintCmax(float L, float h) {\n"
      "   float t = clamp(L, 0.0, 1.0);\n"
      "   float c = 0.15 * sin(3.14159265 * pow(t, 1.45));\n"
      "   c += 0.06 * t * max(cos(h - 1.92), 0.0);\n"
      "   c -= 0.04 * max(cos(h - 4.6), 0.0);\n"
      "   return max(c, 0.0);\n"
      "}\n"
      "void main() {\n"
      "   vec4 src = texture(uSrc, vUv);\n"
      "   vec3 lin = SrgbDecode(max(src.rgb, vec3(0.0)));\n"
      "   vec3 lab = ToLab(lin);\n"
      "   float C = length(lab.yz), h = atan(lab.z, lab.y);\n"
      "   if (h < 0.0) h += 6.28318531;\n"
      "   bool outside = any(lessThan(src.rgb, vec3(-0.002))) || any(greaterThan(src.rgb, vec3(1.002)));\n"
      "   vec3 proof = clamp(lin, 0.0, 1.0);\n"
      "   if (uTarget == 1) {\n"
      "      float L = mix(0.22, 0.98, clamp(lab.x, 0.0, 1.0));\n"
      "      float cm = PrintCmax(L, h);\n"
      "      outside = outside || C > PrintCmax(lab.x, h) + 0.002;\n"
      "      vec2 ab = C > cm ? lab.yz * (cm / C) : lab.yz;\n"
      "      proof = clamp(FromLab(vec3(L, ab)), 0.0, 1.0);\n"
      "   } else if (uTarget == 2) {\n"
      "      outside = outside || C > 0.02;\n"
      "      proof = vec3(clamp(dot(lin, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0));\n"
      "   }\n"
      "   vec3 rgb = uProof == 1 ? SrgbEncode(proof) : src.rgb;\n"
      "   if (uWarn == 1 && outside && src.a > 0.0) rgb = vec3(0.5);\n"
      "   fragColor = vec4(rgb, src.a);\n"
      "}\n",
      { E("Target", "uTarget", { "sRGB", "Print (CMYK)", "Grayscale" }, 1),
        P("Proof", "uProof", T::Bool, 0.0f, 1.0f, 1.0f),
        P("Warn", "uWarn", T::Bool, 0.0f, 1.0f, 0.0f) } };
   return kDef;
}

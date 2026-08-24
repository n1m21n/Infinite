#include "FilterDefs.h"

namespace
{
   using T = FilterParamDef::Type;

   FilterParamDef E(const char* label, const char* uniform,
                    std::vector<std::string> options, int defaultIndex = 0)
   {
      FilterParamDef p;
      p.label = label;
      p.uniformName = uniform;
      p.type = FilterParamDef::Type::Enum;
      p.minVal = 0.0f;
      p.maxVal = (float)(options.size() - 1);
      p.defaultVal[0] = (float)defaultIndex;
      p.options = std::move(options);
      return p;
   }

   FilterParamDef P(const char* label, const char* uniform, T type, float minV, float maxV, float def0, float def1 = 0, float def2 = 0)
   {
      FilterParamDef p;
      p.label = label;
      p.uniformName = uniform;
      p.type = type;
      p.minVal = minV;
      p.maxVal = maxV;
      p.defaultVal[0] = def0;
      p.defaultVal[1] = def1;
      p.defaultVal[2] = def2;
      return p;
   }

   // Tags a param with a section header, drawn above it in the params panel -
   // for filters whose param list is long enough that a flat list stops being
   // readable (e.g. a combined color-adjustments node).
   FilterParamDef S(const char* section, FilterParamDef p)
   {
      p.sectionLabel = section;
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
      { "gaussianblur", "Effects",
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 sum = vec4(0.0); float total = 0.0;\n"
        "   for (int x = -4; x <= 4; x++) for (int y = -4; y <= 4; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * uRadius;\n"
        "      float w = exp(-float(x*x + y*y) / 8.0);\n"
        "      sum += texture(uSrc, vUv + off) * w; total += w;\n"
        "   }\n"
        "   fragColor = sum / total;\n"
        "}\n",
        { P("Radius", "uRadius", T::Float, 0.0f, 10.0f, 2.0f) } },

      { "boxblur", "Effects",
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 sum = vec4(0.0); float total = 0.0;\n"
        "   for (int x = -4; x <= 4; x++) for (int y = -4; y <= 4; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * uRadius;\n"
        "      sum += texture(uSrc, vUv + off); total += 1.0;\n"
        "   }\n"
        "   fragColor = sum / total;\n"
        "}\n",
        { P("Radius", "uRadius", T::Float, 0.0f, 10.0f, 2.0f) } },

      { "motionblur", "Effects",
        "uniform float uAngle;\n"
        "uniform float uDistance;\n"
        "void main() {\n"
        "   vec2 dir = vec2(cos(uAngle), sin(uAngle)) * uTexelSize * uDistance;\n"
        "   vec4 sum = vec4(0.0); const int N = 16;\n"
        "   for (int i = 0; i < N; i++) {\n"
        "      float t = float(i) / float(N - 1) - 0.5;\n"
        "      sum += texture(uSrc, vUv + dir * t);\n"
        "   }\n"
        "   fragColor = sum / float(N);\n"
        "}\n",
        { P("Angle", "uAngle", T::Float, 0.0f, 6.2832f, 0.0f),
          P("Distance", "uDistance", T::Float, 0.0f, 60.0f, 15.0f) } },

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
        "      sum += texture(uSrc, vUv - dir * uAmount * t * 0.1);\n"
        "   }\n"
        "   fragColor = sum / float(N);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 5.0f, 1.0f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f) } },

      // ---------------- Effects: sharpen ----------------
      { "unsharpmask", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec4 blur = vec4(0.0); float total = 0.0;\n"
        "   for (int x = -2; x <= 2; x++) for (int y = -2; y <= 2; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * uRadius;\n"
        "      float w = exp(-float(x*x + y*y) / 4.0);\n"
        "      blur += texture(uSrc, vUv + off) * w; total += w;\n"
        "   }\n"
        "   blur /= total;\n"
        "   fragColor = vec4(c.rgb + (c.rgb - blur.rgb) * uAmount, c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 3.0f, 0.6f),
          P("Radius", "uRadius", T::Float, 0.1f, 5.0f, 1.5f) } },

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
        "   fragColor = texture(uSrc, center + rd);\n"
        "}\n",
        { P("Angle", "uAngle", T::Float, -6.2832f, 6.2832f, 2.0f),
          P("Radius", "uRadius", T::Float, 0.05f, 1.0f, 0.5f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f) } },

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
        "   fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.1f, 4.0f, 1.5f),
          P("Radius", "uRadius", T::Float, 0.05f, 1.0f, 0.5f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f) } },

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
        "   fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Amplitude", "uAmplitude", T::Float, 0.0f, 2.0f, 0.5f),
          P("Wavelength", "uWavelength", T::Float, 1.0f, 40.0f, 10.0f),
          P("Phase", "uPhase", T::Float, 0.0f, 20.0f, 0.0f),
          P("Center X", "uCenterX", T::Float, 0.0f, 1.0f, 0.5f),
          P("Center Y", "uCenterY", T::Float, 0.0f, 1.0f, 0.5f) } },

      { "pixelate", "Effects",
        "uniform float uBlockSize;\n"
        "void main() {\n"
        "   vec2 res = 1.0 / uTexelSize;\n"
        "   vec2 block = max(vec2(1.0), vec2(uBlockSize));\n"
        "   vec2 uv = (floor(vUv * res / block) * block + block * 0.5) / res;\n"
        "   fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Block Size", "uBlockSize", T::Float, 1.0f, 64.0f, 8.0f) } },

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
        "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) fragColor = vec4(0.0);\n"
        "   else fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Translate X", "uTranslateX", T::Float, -1.0f, 1.0f, 0.0f),
          P("Translate Y", "uTranslateY", T::Float, -1.0f, 1.0f, 0.0f),
          P("Scale", "uScale", T::Float, 0.1f, 4.0f, 1.0f),
          P("Scale X", "uScaleX", T::Float, 0.1f, 4.0f, 1.0f),
          P("Scale Y", "uScaleY", T::Float, 0.1f, 4.0f, 1.0f),
          P("Rotation", "uRotation", T::Float, -6.2832f, 6.2832f, 0.0f),
          P("Flip Horizontal", "uFlipH", T::Bool, 0.0f, 1.0f, 0.0f),
          P("Flip Vertical", "uFlipV", T::Bool, 0.0f, 1.0f, 0.0f) } },

      // ---------------- Color adjustments ----------------
      { "invert", "Color",
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(1.0 - c.rgb, c.a);\n"
        "}\n",
        {} },

      { "posterize", "Color",
        "uniform float uLevels;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float levels = max(uLevels, 2.0);\n"
        "   vec3 col = floor(c.rgb * levels) / (levels - 1.0);\n"
        "   fragColor = vec4(clamp(col, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Levels", "uLevels", T::Float, 2.0f, 32.0f, 6.0f) } },

      { "threshold", "Color",
        "uniform float uThreshold;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   fragColor = vec4(vec3(step(uThreshold, lum)), c.a);\n"
        "}\n",
        { P("Threshold", "uThreshold", T::Float, 0.0f, 1.0f, 0.5f) } },

      { "exposure", "Color",
        "uniform float uExposure;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = c.rgb * pow(2.0, uExposure);\n"
        "   fragColor = vec4(clamp(col, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Exposure", "uExposure", T::Float, -3.0f, 3.0f, 0.0f) } },

      // ---------------- Effects: bloom / glow ----------------
      { "bloom", "Effects",
        "uniform float uThreshold;\n"
        "uniform float uIntensity;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 bloom = vec3(0.0); float total = 0.0;\n"
        "   for (int x = -5; x <= 5; x++) for (int y = -5; y <= 5; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * uRadius;\n"
        "      vec3 s = texture(uSrc, vUv + off).rgb;\n"
        "      float lum = dot(s, vec3(0.299, 0.587, 0.114));\n"
        "      vec3 bright = max(s - vec3(uThreshold), vec3(0.0)) * step(uThreshold, lum);\n"
        "      float w = exp(-float(x*x + y*y) / 12.0);\n"
        "      bloom += bright * w; total += w;\n"
        "   }\n"
        "   bloom /= max(total, 1e-4);\n"
        "   fragColor = vec4(c.rgb + bloom * uIntensity, c.a);\n"
        "}\n",
        { P("Threshold", "uThreshold", T::Float, 0.0f, 1.0f, 0.6f),
          P("Intensity", "uIntensity", T::Float, 0.0f, 5.0f, 1.4f),
          P("Radius", "uRadius", T::Float, 0.5f, 12.0f, 4.0f) } },

      { "diffuseglow", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 blur = vec3(0.0); float total = 0.0;\n"
        "   for (int x = -4; x <= 4; x++) for (int y = -4; y <= 4; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * uRadius;\n"
        "      float w = exp(-float(x*x + y*y) / 10.0);\n"
        "      blur += texture(uSrc, vUv + off).rgb * w; total += w;\n"
        "   }\n"
        "   blur /= max(total, 1e-4);\n"
        "   vec3 screen = 1.0 - (1.0 - c.rgb) * (1.0 - blur);\n"
        "   fragColor = vec4(mix(c.rgb, screen, uAmount), c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 1.0f, 0.6f),
          P("Radius", "uRadius", T::Float, 0.5f, 12.0f, 4.0f) } },

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
        "      vec4 c = texture(uSrc, uv);\n"
        "      vec4 cr = texture(uSrc, uv + vec2(uAmount * 0.01, 0.0));\n"
        "      vec4 cb = texture(uSrc, uv - vec2(uAmount * 0.01, 0.0));\n"
        "      fragColor = vec4(cr.r, c.g, cb.b, c.a);\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 1) {\n"           // RGB shift
        "      vec2 dir = vec2(cos(uDetail * 6.2832), sin(uDetail * 6.2832)) * uAmount * 0.02;\n"
        "      fragColor = vec4(texture(uSrc, uv + dir).r, texture(uSrc, uv).g,\n"
        "                       texture(uSrc, uv - dir).b, texture(uSrc, uv).a);\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 2) {\n"           // Scanlines
        "      vec4 c = texture(uSrc, uv);\n"
        "      float line = sin((uv.y + t * 0.1) * max(10.0, uDetail * 800.0) * 3.14159);\n"
        "      fragColor = vec4(c.rgb * (1.0 - uAmount * step(0.0, -line)), c.a);\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 3) {\n"           // Blocks
        "      vec2 grid = max(vec2(2.0), vec2(uDetail * 60.0));\n"
        "      vec2 cell = floor(uv * grid);\n"
        "      if (rand(cell + floor(t * 8.0)) > 1.0 - uAmount)\n"
        "         uv += (vec2(rand(cell + 1.0), rand(cell + 2.0)) - 0.5) * 0.15;\n"
        "      fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));\n"
        "      return;\n"
        "   }\n"
        "   if (uKind == 4) {\n"           // Wave
        "      uv.x += sin(uv.y * max(1.0, uDetail * 200.0) + t * 2.0) * uAmount * 0.05;\n"
        "      fragColor = texture(uSrc, clamp(uv, 0.0, 1.0));\n"
        "      return;\n"
        "   }\n"
        "   // Datamosh\n"
        "   float slice = floor(uv.y * max(2.0, uDetail * 120.0));\n"
        "   float r = rand(vec2(slice, floor(t * 4.0)));\n"
        "   uv.x = fract(uv.x + (r - 0.5) * uAmount * 0.5);\n"
        "   vec4 c = texture(uSrc, uv);\n"
        "   if (r > 0.85) c.rgb = c.gbr;\n"
        "   fragColor = c;\n"
        "}\n",
        { E("kind", "uKind", { "Slice Shift", "RGB Shift", "Scanlines", "Blocks", "Wave", "Datamosh" }, 0),
          P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 0.6f),
          P("Detail", "uDetail", T::Float, 0.02f, 1.0f, 0.4f),
          P("Speed", "uSpeed", T::Float, 0.0f, 4.0f, 1.0f),
          P("Seed", "uSeed", T::Float, 0.0f, 100.0f, 0.0f) } },

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
        "   fragColor = texture(uSrc, clamp(vUv + offset * uAmount * 0.1, 0.0, 1.0));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 0.3f),
          P("Map Scale", "uScale", T::Float, 0.1f, 8.0f, 1.0f) },
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
        "   float a = hash2(i).x, b = hash2(i + vec2(1,0)).x;\n"
        "   float c = hash2(i + vec2(0,1)).x, d = hash2(i + vec2(1,1)).x;\n"
        "   return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
        "}\n"
        "void main() {\n"
        "   float t = uTime * uSpeed;\n"
        "   vec2 flow = vec2(noise(vUv * uScale + t), noise(vUv * uScale + 5.2 - t)) - 0.5;\n"
        "   fragColor = texture(uSrc, clamp(vUv + flow * uAmount * 0.2, 0.0, 1.0));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 0.4f),
          P("Scale", "uScale", T::Float, 0.5f, 20.0f, 4.0f),
          P("Speed", "uSpeed", T::Float, 0.0f, 3.0f, 0.4f) } },

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
          E("flip", "uFlip", { "Off", "On" }, 0) } },

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
        { P("Segments", "uSegments", T::Float, 2.0f, 32.0f, 6.0f),
          P("Rotation", "uRotation", T::Float, 0.0f, 6.2832f, 0.0f),
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
        { P("Tiles", "uTiles", T::Float, 1.0f, 12.0f, 2.0f) } },

      // ---------------- Keying ----------------
      { "chroma key", "Mask",
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

      { "luma key", "Mask",
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
          E("invert", "uInvert", { "Off", "On" }, 0),
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
        "void main() {\n"
        "   vec2 off = vec2(cos(uAngle), sin(uAngle)) * uTexelSize * uDistance;\n"
        "   vec3 a = texture(uSrc, vUv + off).rgb;\n"
        "   vec3 b = texture(uSrc, vUv - off).rgb;\n"
        "   vec3 diff = (a - b) * uAmount;\n"
        "   float e = dot(diff, vec3(0.299, 0.587, 0.114));\n"
        "   vec4 src = texture(uSrc, vUv);\n"
        "   if (uKeepColor == 1) fragColor = vec4(clamp(src.rgb + diff, 0.0, 1.0), src.a);\n"
        "   else fragColor = vec4(vec3(clamp(e + 0.5, 0.0, 1.0)), src.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 8.0f, 2.0f),
          P("Angle", "uAngle", T::Float, 0.0f, 6.2832f, 0.785f),
          P("Distance", "uDistance", T::Float, 0.5f, 12.0f, 1.5f),
          E("style", "uKeepColor", { "Grey", "Over colour" }, 0) } },

      { "normal map", "Effects",
        "uniform float uStrength;\n"
        "uniform int uInvertY;\n"
        "uniform int uOutput;\n"
        "float lum(vec2 uv) { return dot(texture(uSrc, uv).rgb, vec3(0.299, 0.587, 0.114)); }\n"
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
          E("output", "uOutput", { "Normals", "Height", "Slope" }, 0) } },

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
        "   vec3 outCol = clamp(sum / div + uBias, 0.0, 1.0);\n"
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

      { "lookup", "Color",
        // Second input is the palette: this pixel's luminance indexes across it.
        "uniform float uMix;\n"
        "uniform int uChannel;\n"
        "uniform float uOffset;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   if (uHasSrc2 == 0) { fragColor = c; return; }\n"
        "   float idx;\n"
        "   if (uChannel == 1) idx = c.r;\n"
        "   else if (uChannel == 2) idx = c.g;\n"
        "   else if (uChannel == 3) idx = c.b;\n"
        "   else if (uChannel == 4) idx = c.a;\n"
        "   else idx = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
        "   idx = fract(idx + uOffset);\n"
        "   vec3 mapped = texture(uSrc2, vec2(idx, 0.5)).rgb;\n"
        "   fragColor = vec4(mix(c.rgb, mapped, uMix), c.a);\n"
        "}\n",
        { E("index by", "uChannel", { "Luminance", "Red", "Green", "Blue", "Alpha" }, 0),
          P("Offset", "uOffset", T::Float, 0.0f, 1.0f, 0.0f),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f) },
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
        { P("Scale", "uScale", T::Float, 10.0f, 300.0f, 80.0f),
          P("Angle", "uAngle", T::Float, 0.0f, 1.5708f, 0.4f),
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
          E("invert", "uInvert", { "Off", "On" }, 0) } },

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
        { P("Thickness", "uThickness", T::Float, 1.0f, 10.0f, 2.0f),
          P("Threshold", "uThreshold", T::Float, 0.01f, 1.0f, 0.15f),
          P("Colour", "uColor", T::Color, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f) } },

      // ---------------- Color: LUT ----------------
      { "lut", "Color",
        // Second input is a HALD/strip LUT image: an N*N by N grid of slices.
        "uniform float uSize;\n"
        "uniform float uMix;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   if (uHasSrc2 == 0) { fragColor = c; return; }\n"
        "   float n = max(2.0, floor(uSize));\n"
        "   float blue = clamp(c.b, 0.0, 1.0) * (n - 1.0);\n"
        "   float slice0 = floor(blue);\n"
        "   float slice1 = min(slice0 + 1.0, n - 1.0);\n"
        "   float f = blue - slice0;\n"
        "   vec2 texel = vec2(1.0 / (n * n), 1.0 / n);\n"
        "   vec2 base = vec2(clamp(c.r, 0.0, 1.0) / n, clamp(c.g, 0.0, 1.0));\n"
        "   vec3 s0 = texture(uSrc2, base + vec2(slice0 / n, 0.0) * vec2(1.0, 0.0)).rgb;\n"
        "   vec3 s1 = texture(uSrc2, base + vec2(slice1 / n, 0.0) * vec2(1.0, 0.0)).rgb;\n"
        "   vec3 graded = mix(s0, s1, f);\n"
        "   fragColor = vec4(mix(c.rgb, graded, uMix), c.a);\n"
        "}\n",
        { P("LUT Size", "uSize", T::Float, 2.0f, 64.0f, 16.0f),
          P("Mix", "uMix", T::Float, 0.0f, 1.0f, 1.0f) },
        2 },

      { "gradientmap", "Color",
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

      { "color adjustments", "Color",
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
        "float toneCurve(float x) {\n"
        "   float s = mix(x, x*x*(3.0-2.0*x), uContrastPivot);\n"
        "   float lift = uShadows * (1.0 - x) * (1.0 - x);\n"
        "   float gain = uHighlights * x * x;\n"
        "   float mid = uMidtones * 4.0 * x * (1.0 - x);\n"
        "   return clamp(s + lift + gain + mid, 0.0, 1.0);\n"
        "}\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = c.rgb;\n"
        "\n"
        "   col = clamp((col - 0.5) * (uContrast + 1.0) + 0.5 + uBrightness, 0.0, 1.0);\n"
        "\n"
        "   col = clamp((col - uBlackPoint) / max(uWhitePoint - uBlackPoint, 0.0001), 0.0, 1.0);\n"
        "   col = pow(col, vec3(1.0 / max(uGamma, 0.0001)));\n"
        "\n"
        "   col = clamp(col + vec3(uCyanRed, uMagentaGreen, uYellowBlue), 0.0, 1.0);\n"
        "\n"
        "   vec3 hsv = rgb2hsv(col);\n"
        "   hsv.x = fract(hsv.x + uHueShift);\n"
        "   hsv.y = clamp(hsv.y * uSaturation, 0.0, 1.0);\n"
        "   col = clamp(hsv2rgb(hsv) + uLightness, 0.0, 1.0);\n"
        "\n"
        "   float avgc = (col.r + col.g + col.b) / 3.0;\n"
        "   col = clamp(mix(vec3(avgc), col, 1.0 + uVibrance), 0.0, 1.0);\n"
        "\n"
        "   col = vec3(toneCurve(col.r), toneCurve(col.g), toneCurve(col.b));\n"
        "\n"
        "   col = clamp(vec3(dot(col, uRedRow), dot(col, uGreenRow), dot(col, uBlueRow)), 0.0, 1.0);\n"
        "\n"
        "   if (uBWEnabled == 1) {\n"
        "      float lum = col.r*uRedWeight + col.g*uGreenWeight + col.b*uBlueWeight;\n"
        "      col = vec3(lum);\n"
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
          P("Blue Weight", "uBlueWeight", T::Float, 0.0f, 2.0f, 0.114f) } },

      // ---------------- Compositing: layer-effect style filters ----------------
      { "outerglow", "Compositing",
        "uniform float uAmount;\n"
        "uniform vec3 uColor;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec4 blur = vec4(0.0); float total = 0.0;\n"
        "   for (int x = -3; x <= 3; x++) for (int y = -3; y <= 3; y++) {\n"
        "      vec2 off = vec2(float(x), float(y)) * uTexelSize * 4.0;\n"
        "      float w = exp(-float(x*x + y*y) / 6.0);\n"
        "      blur += texture(uSrc, vUv + off) * w; total += w;\n"
        "   }\n"
        "   blur /= total;\n"
        "   vec3 glow = uColor * blur.a * uAmount;\n"
        "   fragColor = vec4(c.rgb + glow * (1.0 - c.a), max(c.a, blur.a * uAmount));\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 2.0f, 1.0f),
          P("Color", "uColor", T::Color, 0.0f, 1.0f, 1.0f, 0.9f, 0.3f) } },

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
        { P("Offset X", "uOffsetX", T::Float, -20.0f, 20.0f, 4.0f),
          P("Offset Y", "uOffsetY", T::Float, -20.0f, 20.0f, 4.0f),
          P("Color", "uColor", T::Color, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
          P("Opacity", "uOpacity", T::Float, 0.0f, 1.0f, 0.6f) } },
   };

   return kDefs;
}

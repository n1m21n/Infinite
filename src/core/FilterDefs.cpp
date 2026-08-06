#include "FilterDefs.h"

namespace
{
   using T = FilterParamDef::Type;

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
        "void main() {\n"
        "   vec2 center = vec2(0.5, 0.5);\n"
        "   vec2 d = vUv - center;\n"
        "   float dist = length(d);\n"
        "   float amt = smoothstep(uRadius, 0.0, dist) * uAngle;\n"
        "   float s = sin(amt), c = cos(amt);\n"
        "   vec2 rd = vec2(c*d.x - s*d.y, s*d.x + c*d.y);\n"
        "   fragColor = texture(uSrc, center + rd);\n"
        "}\n",
        { P("Angle", "uAngle", T::Float, -6.2832f, 6.2832f, 2.0f),
          P("Radius", "uRadius", T::Float, 0.05f, 1.0f, 0.5f) } },

      { "pinchpunch", "Effects",
        "uniform float uAmount;\n"
        "uniform float uRadius;\n"
        "void main() {\n"
        "   vec2 center = vec2(0.5, 0.5);\n"
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
          P("Radius", "uRadius", T::Float, 0.05f, 1.0f, 0.5f) } },

      { "ripple", "Effects",
        "uniform float uAmplitude;\n"
        "uniform float uWavelength;\n"
        "uniform float uPhase;\n"
        "void main() {\n"
        "   vec2 d = vUv - vec2(0.5, 0.5);\n"
        "   float dist = length(d);\n"
        "   float wave = sin(dist * uWavelength * 6.2832 - uPhase) * uAmplitude;\n"
        "   vec2 uv = vUv + normalize(d + vec2(0.0001)) * wave * uTexelSize * 10.0;\n"
        "   fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Amplitude", "uAmplitude", T::Float, 0.0f, 2.0f, 0.5f),
          P("Wavelength", "uWavelength", T::Float, 1.0f, 40.0f, 10.0f),
          P("Phase", "uPhase", T::Float, 0.0f, 20.0f, 0.0f) } },

      { "pixelate", "Effects",
        "uniform float uBlockSize;\n"
        "void main() {\n"
        "   vec2 res = 1.0 / uTexelSize;\n"
        "   vec2 block = max(vec2(1.0), vec2(uBlockSize));\n"
        "   vec2 uv = (floor(vUv * res / block) * block + block * 0.5) / res;\n"
        "   fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Block Size", "uBlockSize", T::Float, 1.0f, 64.0f, 8.0f) } },

      { "glitch", "Effects",
        "uniform float uAmount;\n"
        "uniform float uBlockiness;\n"
        "float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453); }\n"
        "void main() {\n"
        "   vec2 uv = vUv;\n"
        "   float blockY = floor(uv.y * max(1.0, uBlockiness * 40.0));\n"
        "   float shift = (rand(vec2(blockY, floor(uTime * 10.0))) - 0.5) * uAmount * 0.2;\n"
        "   uv.x += shift;\n"
        "   vec4 c = texture(uSrc, uv);\n"
        "   vec4 cr = texture(uSrc, uv + vec2(uAmount * 0.01, 0.0));\n"
        "   vec4 cb = texture(uSrc, uv - vec2(uAmount * 0.01, 0.0));\n"
        "   fragColor = vec4(cr.r, c.g, cb.b, c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 5.0f, 1.0f),
          P("Blockiness", "uBlockiness", T::Float, 0.0f, 1.0f, 0.4f) } },

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
        "void main() {\n"
        "   vec2 d = vUv - vec2(0.5, 0.5);\n"
        "   float dist = length(d) / 0.7071;\n"
        "   float vig = smoothstep(uRadius, max(uRadius - uSoftness, 0.0), dist);\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   fragColor = vec4(c.rgb * mix(1.0, vig, uAmount), c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, 0.0f, 1.0f, 0.6f),
          P("Radius", "uRadius", T::Float, 0.1f, 1.5f, 0.9f),
          P("Softness", "uSoftness", T::Float, 0.01f, 1.0f, 0.4f) } },

      // ---------------- Effects: transform (folded in per the request) ----------------
      { "transform", "Effects",
        "uniform float uTranslateX;\n"
        "uniform float uTranslateY;\n"
        "uniform float uScale;\n"
        "uniform float uRotation;\n"
        "void main() {\n"
        "   vec2 uv = vUv - vec2(0.5, 0.5);\n"
        "   float s = sin(-uRotation), c = cos(-uRotation);\n"
        "   uv = vec2(c*uv.x - s*uv.y, s*uv.x + c*uv.y);\n"
        "   uv /= max(uScale, 0.0001);\n"
        "   uv -= vec2(uTranslateX, uTranslateY);\n"
        "   uv += vec2(0.5, 0.5);\n"
        "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) fragColor = vec4(0.0);\n"
        "   else fragColor = texture(uSrc, uv);\n"
        "}\n",
        { P("Translate X", "uTranslateX", T::Float, -1.0f, 1.0f, 0.0f),
          P("Translate Y", "uTranslateY", T::Float, -1.0f, 1.0f, 0.0f),
          P("Scale", "uScale", T::Float, 0.1f, 4.0f, 1.0f),
          P("Rotation", "uRotation", T::Float, -6.2832f, 6.2832f, 0.0f) } },

      // ---------------- Color adjustments ----------------
      { "brightnesscontrast", "Color",
        "uniform float uBrightness;\n"
        "uniform float uContrast;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = (c.rgb - 0.5) * (uContrast + 1.0) + 0.5 + uBrightness;\n"
        "   fragColor = vec4(clamp(col, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Brightness", "uBrightness", T::Float, -1.0f, 1.0f, 0.0f),
          P("Contrast", "uContrast", T::Float, -1.0f, 3.0f, 0.0f) } },

      { "levels", "Color",
        "uniform float uBlackPoint;\n"
        "uniform float uWhitePoint;\n"
        "uniform float uGamma;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = clamp((c.rgb - uBlackPoint) / max(uWhitePoint - uBlackPoint, 0.0001), 0.0, 1.0);\n"
        "   col = pow(col, vec3(1.0 / max(uGamma, 0.0001)));\n"
        "   fragColor = vec4(col, c.a);\n"
        "}\n",
        { P("Black Point", "uBlackPoint", T::Float, 0.0f, 1.0f, 0.0f),
          P("White Point", "uWhitePoint", T::Float, 0.0f, 1.0f, 1.0f),
          P("Gamma", "uGamma", T::Float, 0.1f, 4.0f, 1.0f) } },

      { "hsl", "Color",
        std::string(kSharedHelpers) +
        "uniform float uHueShift;\n"
        "uniform float uSaturation;\n"
        "uniform float uLightness;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 hsv = rgb2hsv(c.rgb);\n"
        "   hsv.x = fract(hsv.x + uHueShift);\n"
        "   hsv.y = clamp(hsv.y * uSaturation, 0.0, 1.0);\n"
        "   vec3 rgb = hsv2rgb(hsv) + uLightness;\n"
        "   fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Hue Shift", "uHueShift", T::Float, 0.0f, 1.0f, 0.0f),
          P("Saturation", "uSaturation", T::Float, 0.0f, 3.0f, 1.0f),
          P("Lightness", "uLightness", T::Float, -1.0f, 1.0f, 0.0f) } },

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

      { "vibrance", "Color",
        "uniform float uAmount;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float avgc = (c.r + c.g + c.b) / 3.0;\n"
        "   vec3 boosted = mix(vec3(avgc), c.rgb, 1.0 + uAmount);\n"
        "   fragColor = vec4(clamp(boosted, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Amount", "uAmount", T::Float, -1.0f, 2.0f, 0.3f) } },

      { "blackandwhite", "Color",
        "uniform float uRedWeight;\n"
        "uniform float uGreenWeight;\n"
        "uniform float uBlueWeight;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   float lum = c.r*uRedWeight + c.g*uGreenWeight + c.b*uBlueWeight;\n"
        "   fragColor = vec4(vec3(lum), c.a);\n"
        "}\n",
        { P("Red Weight", "uRedWeight", T::Float, 0.0f, 2.0f, 0.299f),
          P("Green Weight", "uGreenWeight", T::Float, 0.0f, 2.0f, 0.587f),
          P("Blue Weight", "uBlueWeight", T::Float, 0.0f, 2.0f, 0.114f) } },

      { "colorbalance", "Color",
        "uniform float uCyanRed;\n"
        "uniform float uMagentaGreen;\n"
        "uniform float uYellowBlue;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = c.rgb + vec3(uCyanRed, uMagentaGreen, uYellowBlue);\n"
        "   fragColor = vec4(clamp(col, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Cyan-Red", "uCyanRed", T::Float, -0.5f, 0.5f, 0.0f),
          P("Magenta-Green", "uMagentaGreen", T::Float, -0.5f, 0.5f, 0.0f),
          P("Yellow-Blue", "uYellowBlue", T::Float, -0.5f, 0.5f, 0.0f) } },

      { "exposure", "Color",
        "uniform float uExposure;\n"
        "void main() {\n"
        "   vec4 c = texture(uSrc, vUv);\n"
        "   vec3 col = c.rgb * pow(2.0, uExposure);\n"
        "   fragColor = vec4(clamp(col, 0.0, 1.0), c.a);\n"
        "}\n",
        { P("Exposure", "uExposure", T::Float, -3.0f, 3.0f, 0.0f) } },

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

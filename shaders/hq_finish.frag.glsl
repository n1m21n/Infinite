#version 450 core
// Turbo 0.71 (S8): Render 3D HQ - HDR to the RGBA8 image the 2D graph gets:
// bloom added in linear HDR before the tonemap (Filament's colorGrading.mat
// order, ADD blend mode: scene + bloom x strength / levels), tonemap, sRGB
// (2.2, as Render 3D), dither. The background keying and Infinite's own
// tonemaps (1-4) are Render3DPost's (Turbo 0.61 / 0.70), kept the same so an
// HQ patch reads like a Render 3D one.
//
// Tonemaps 5 / 6 ported from Filament filament/src/ToneMapper.cpp
// (ACESLegacyToneMapper, Filament's default, and ACESToneMapper) and the
// matrices of filament/src/details/ColorSpaceUtils.h @ 13be3649 (Apache
// License 2.0, Copyright Google LLC). Changes: GLSL per pixel instead of a
// baked colour-grading LUT; the scene is linear Rec.709, so it goes to
// Rec.2020 and back around the curve as Filament's grading does.
in vec2 vNdc;

uniform sampler2D uSrc;
uniform sampler2D uBloom;      // level 0 of the bloom chain (all levels summed)
uniform float uBloomStrength;  // strength / levels; 0 = no bloom
uniform sampler2D uKey;        // mode 1: depth; mode 4: TAA coverage
uniform int uKeyMode;          // 0 tonemap all (HDRI behind), 1 depth key, 4 coverage key
uniform vec3 uBg;
uniform float uBgOpacity;
uniform int uTonemap;          // 0 none, 1 ACES fit, 2 Reinhard, 3 AgX, 4 Neutral, 5 Filament ACES legacy, 6 Filament ACES

layout(location = 0) out vec4 outColor;

// --- Infinite's tonemaps (Render3DPost) ------------------------------------
vec3 aces(vec3 x)
{
   return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 agxContrast(vec3 x)
{
   vec3 x2 = x * x, x4 = x2 * x2;
   return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 c)
{
   const mat3 inMat = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                           0.0784335999999992, 0.878468636469772, 0.0784336,
                           0.0792237451477643, 0.0791661274605434, 0.879142973793104);
   const mat3 outMat = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                            -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                            -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
   c = inMat * max(c, vec3(1e-10));
   c = clamp((log2(c) + 12.47393) / 16.5, 0.0, 1.0);
   c = agxContrast(c);
   c = outMat * c;
   return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 neutral(vec3 c)
{
   const float start = 0.8 - 0.04, desat = 0.15;
   float x = min(c.r, min(c.g, c.b));
   float off = x < 0.08 ? x - 6.25 * x * x : 0.04;
   c -= off;
   float peak = max(c.r, max(c.g, c.b));
   if (peak < start)
      return c;
   float d = 1.0 - start;
   float np = 1.0 - d * d / (peak + d - start);
   c *= np / peak;
   float g = 1.0 - 1.0 / (desat * (peak - np) + 1.0);
   return mix(c, vec3(np), g);
}

// --- Filament's ACES (ToneMapper.cpp, namespace aces) ----------------------
// Column-major, in the order ColorSpaceUtils.h lists them (mat3f and GLSL
// mat3 both take columns).
const mat3 XYZ_to_sRGB = mat3(3.2404542, -0.9692660, 0.0556434,
                              -1.5371385, 1.8760108, -0.2040259,
                              -0.4985314, 0.0415560, 1.0572252);
const mat3 sRGB_to_XYZ = mat3(0.4124560, 0.2126730, 0.0193339,
                              0.3575760, 0.7151520, 0.1191920,
                              0.1804380, 0.0721750, 0.9503040);
const mat3 Rec2020_to_XYZ = mat3(0.6369530, 0.2626983, 0.0000000,
                                 0.1446169, 0.6780088, 0.0280731,
                                 0.1688558, 0.0592929, 1.0608272);
const mat3 XYZ_to_Rec2020 = mat3(1.7166634, -0.6666738, 0.0176425,
                                 -0.3556733, 1.6164557, -0.0427770,
                                 -0.2533681, 0.0157683, 0.9422433);
const mat3 AP1_to_XYZ = mat3(0.6624541811, 0.2722287168, -0.0055746495,
                             0.1340042065, 0.6740817658, 0.0040607335,
                             0.1561876870, 0.0536895174, 1.0103391003);
const mat3 XYZ_to_AP1 = mat3(1.6410233797, -0.6636628587, 0.0117218943,
                             -0.3248032942, 1.6153315917, -0.0082844420,
                             -0.2364246952, 0.0167563477, 0.9883948585);
const mat3 AP1_to_AP0 = mat3(0.6954522414, 0.0447945634, -0.0055258826,
                             0.1406786965, 0.8596711185, 0.0040252103,
                             0.1638690622, 0.0955343182, 1.0015006723);
const mat3 AP0_to_AP1 = mat3(1.4514393161, -0.0765537734, 0.0083161484,
                             -0.2365107469, 1.1762296998, -0.0060324498,
                             -0.2149285693, -0.0996759264, 0.9977163014);
const vec3 LUMINANCE_AP1 = vec3(0.272229, 0.674082, 0.0536895);
const float HALF_MAX = 65504.0;

float rgb_2_saturation(vec3 rgb)
{
   const float TINY = 1e-5;
   float mi = min(rgb.r, min(rgb.g, rgb.b));
   float ma = max(rgb.r, max(rgb.g, rgb.b));
   return (max(ma, TINY) - max(mi, TINY)) / max(ma, 1e-2);
}

float rgb_2_yc(vec3 rgb)
{
   const float ycRadiusWeight = 1.75;
   float r = rgb.r, g = rgb.g, b = rgb.b;
   float chroma = sqrt(max(b * (b - g) + g * (g - r) + r * (r - b), 0.0));
   return (b + g + r + ycRadiusWeight * chroma) / 3.0;
}

float sigmoid_shaper(float x)
{
   float t = max(1.0 - abs(x / 2.0), 0.0);
   float y = 1.0 + sign(x) * (1.0 - t * t);
   return y / 2.0;
}

float glow_fwd(float ycIn, float glowGainIn, float glowMid)
{
   if (ycIn <= 2.0 / 3.0 * glowMid)
      return glowGainIn;
   if (ycIn >= 2.0 * glowMid)
      return 0.0;
   return glowGainIn * (glowMid / ycIn - 1.0 / 2.0);
}

float rgb_2_hue(vec3 rgb)
{
   float hue = 0.0;
   if (!(rgb.x == rgb.y && rgb.y == rgb.z))
      hue = degrees(atan(sqrt(3.0) * (rgb.y - rgb.z), 2.0 * rgb.x - rgb.y - rgb.z));
   return (hue < 0.0) ? hue + 360.0 : hue;
}

float center_hue(float hue, float centerH)
{
   float hueCentered = hue - centerH;
   if (hueCentered < -180.0)
      hueCentered = hueCentered + 360.0;
   else if (hueCentered > 180.0)
      hueCentered = hueCentered - 360.0;
   return hueCentered;
}

vec3 xyY_to_XYZ(vec3 v)
{
   float a = v.z / max(v.y, 1e-5);
   return vec3(v.x * a, v.z, (1.0 - v.x - v.y) * a);
}

vec3 XYZ_to_xyY(vec3 v)
{
   return vec3(v.xy / max(v.x + v.y + v.z, 1e-5), v.y);
}

vec3 darkSurround_to_dimSurround(vec3 linearCV)
{
   const float DIM_SURROUND_GAMMA = 0.9811;
   vec3 XYZ = AP1_to_XYZ * linearCV;
   vec3 xyY = XYZ_to_xyY(XYZ);
   xyY.z = clamp(xyY.z, 0.0, HALF_MAX);
   xyY.z = pow(xyY.z, DIM_SURROUND_GAMMA);
   XYZ = xyY_to_XYZ(xyY);
   return XYZ_to_AP1 * XYZ;
}

// Rec.2020 in, Rec.2020 out.
vec3 ACES(vec3 color, float brightness)
{
   const float RRT_GLOW_GAIN = 0.05;
   const float RRT_GLOW_MID = 0.08;
   const float RRT_RED_SCALE = 0.82;
   const float RRT_RED_PIVOT = 0.03;
   const float RRT_RED_HUE = 0.0;
   const float RRT_RED_WIDTH = 135.0;
   const float RRT_SAT_FACTOR = 0.96;
   const float ODT_SAT_FACTOR = 0.93;

   vec3 ap0 = (AP1_to_AP0 * XYZ_to_AP1 * Rec2020_to_XYZ) * color;

   float saturation = rgb_2_saturation(ap0);
   float ycIn = rgb_2_yc(ap0);
   float s = sigmoid_shaper((saturation - 0.4) / 0.2);
   float addedGlow = 1.0 + glow_fwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
   ap0 *= addedGlow;

   float hue = rgb_2_hue(ap0);
   float centeredHue = center_hue(hue, RRT_RED_HUE);
   float hueWeight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / RRT_RED_WIDTH));
   hueWeight *= hueWeight;
   ap0.r += hueWeight * saturation * (RRT_RED_PIVOT - ap0.r) * (1.0 - RRT_RED_SCALE);

   vec3 ap1 = clamp(AP0_to_AP1 * ap0, 0.0, HALF_MAX);
   ap1 = mix(vec3(dot(ap1, LUMINANCE_AP1)), ap1, RRT_SAT_FACTOR);
   ap1 *= brightness;

   const float a = 2.785085;
   const float b = 0.107772;
   const float c = 2.936045;
   const float d = 0.887122;
   const float e = 0.806889;
   vec3 rgbPost = (ap1 * (a * ap1 + b)) / (ap1 * (c * ap1 + d) + e);

   vec3 linearCV = darkSurround_to_dimSurround(rgbPost);
   linearCV = mix(vec3(dot(linearCV, LUMINANCE_AP1)), linearCV, ODT_SAT_FACTOR);
   return (XYZ_to_Rec2020 * AP1_to_XYZ) * linearCV;
}

// Linear Rec.709 -> Rec.2020 -> curve -> Rec.709 (Filament's grading).
vec3 filamentAces(vec3 c, float brightness)
{
   vec3 rec2020 = (XYZ_to_Rec2020 * sRGB_to_XYZ) * max(c, vec3(0.0));
   return (XYZ_to_sRGB * Rec2020_to_XYZ) * ACES(rec2020, brightness);
}

vec3 tonemap(vec3 c)
{
   if (uTonemap == 1) return aces(c);
   if (uTonemap == 2) return c / (c + vec3(1.0));
   if (uTonemap == 3) return agx(c);
   if (uTonemap == 4) return neutral(c);
   if (uTonemap == 5) return filamentAces(c, 1.0 / 0.6); // ACESLegacyToneMapper
   if (uTonemap == 6) return filamentAces(c, 1.0);       // ACESToneMapper
   return c;
}

vec3 encode(vec3 c)
{
   return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

void main()
{
   vec2 uv = vNdc * 0.5 + 0.5;
   vec4 src = textureLod(uSrc, uv, 0.0);
   vec3 c = src.rgb;
   // TAA: uKey holds the geometry's share of the pixel; the cleared
   // background is taken back out so it is not tonemapped with the scene.
   float cov = uKeyMode == 4 ? clamp(textureLod(uKey, uv, 0.0).r, 0.0, 1.0) : 1.0;
   float sceneA = src.a;
   if (uKeyMode == 4 && cov > 0.001)
   {
      c = max((src.rgb - (1.0 - cov) * uBg) / cov, vec3(0.0));
      sceneA = (src.a - (1.0 - cov) * uBgOpacity) / cov;
   }
   vec3 glow = uBloomStrength > 0.0 ? textureLod(uBloom, uv, 0.0).rgb * uBloomStrength : vec3(0.0);
   bool bg = uKeyMode == 1 && textureLod(uKey, uv, 0.0).r >= 0.99999;
   c = bg ? glow : c + glow;
   c = encode(tonemap(c));
   if (bg)
      c = 1.0 - (1.0 - uBg) * (1.0 - c);
   float alpha = src.a;
   if (uKeyMode == 4)
   {
      vec3 bgOut = 1.0 - (1.0 - uBg) * (1.0 - encode(tonemap(glow)));
      c = cov > 0.001 ? mix(bgOut, c, cov) : bgOut;
      alpha = mix(uBgOpacity, clamp(sceneA, 0.0, 1.0), cov);
   }
   vec2 p = floor(gl_FragCoord.xy);
   c += (fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
   outColor = vec4(c, alpha);
}

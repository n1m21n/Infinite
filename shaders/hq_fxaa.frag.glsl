#version 450 core
// Turbo 0.71 (S9): Render 3D HQ - FXAA, the anti-aliasing for frames drawn
// without TAA (TAA off, the 360 faces). Filament offers it next to its TAA
// (View::AntiAliasing::FXAA, filament/src/materials/antiAliasing/fxaa @
// 13be3649); written here from Timothy Lottes' FXAA 3.11 algorithm (NVIDIA,
// "PC quality", 12 search steps) - no code copied: luma edge test against the
// 3x3 neighbourhood, the edge's orientation, a search along it for its two
// ends, the blend offset towards the nearer end, and the sub-pixel blend for
// single-pixel features. Runs on the finished sRGB image (perceptual luma),
// as in Filament (after the colour grading). Alpha passes through.
in vec2 vNdc;

uniform sampler2D uSrc; // RGBA8, linear filtering
uniform vec2 uTexel;    // 1 / size

layout(location = 0) out vec4 outColor;

// FXAA 3.11's defaults for quality: edge threshold, its minimum, sub-pixel.
const float kEdgeThreshold = 0.166;
const float kEdgeThresholdMin = 0.0833;
const float kSubpix = 0.75;
const int kSteps = 12;
const float kStep[12] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

float luma(vec3 c)
{
   return dot(c, vec3(0.299, 0.587, 0.114));
}

float lumaAt(vec2 uv)
{
   return luma(textureLod(uSrc, uv, 0.0).rgb);
}

void main()
{
   vec2 uv = gl_FragCoord.xy * uTexel;
   vec4 rgbaM = textureLod(uSrc, uv, 0.0);
   float lM = luma(rgbaM.rgb);
   float lN = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(0, 1)).rgb);
   float lS = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(0, -1)).rgb);
   float lE = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(1, 0)).rgb);
   float lW = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(-1, 0)).rgb);
   float rangeMax = max(lM, max(max(lN, lS), max(lE, lW)));
   float rangeMin = min(lM, min(min(lN, lS), min(lE, lW)));
   float range = rangeMax - rangeMin;
   if (range < max(kEdgeThresholdMin, rangeMax * kEdgeThreshold))
   {
      outColor = rgbaM; // no edge here
      return;
   }
   float lNW = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(-1, 1)).rgb);
   float lNE = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(1, 1)).rgb);
   float lSW = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(-1, -1)).rgb);
   float lSE = luma(textureLodOffset(uSrc, uv, 0.0, ivec2(1, -1)).rgb);

   // Sub-pixel: how far the centre is from its neighbourhood's (weighted) average.
   float average = (2.0 * (lN + lS + lE + lW) + (lNW + lNE + lSW + lSE)) / 12.0;
   float subpix = clamp(abs(average - lM) / range, 0.0, 1.0);
   subpix = smoothstep(0.0, 1.0, subpix);
   subpix = subpix * subpix * kSubpix;

   // Orientation: a horizontal edge changes most from north to south.
   float edgeHorz = abs(0.25 * lNW - 0.5 * lN + 0.25 * lNE) + abs(0.5 * lW - lM + 0.5 * lE) +
                    abs(0.25 * lSW - 0.5 * lS + 0.25 * lSE);
   float edgeVert = abs(0.25 * lNW - 0.5 * lW + 0.25 * lSW) + abs(0.5 * lN - lM + 0.5 * lS) +
                    abs(0.25 * lNE - 0.5 * lE + 0.25 * lSE);
   bool horzSpan = edgeHorz >= edgeVert;
   // The side of the edge with the steeper change (towards it, one half step).
   float lNeg = horzSpan ? lS : lW;
   float lPos = horzSpan ? lN : lE;
   float gradNeg = abs(lNeg - lM), gradPos = abs(lPos - lM);
   bool towardsPos = gradPos >= gradNeg;
   float stepLength = (horzSpan ? uTexel.y : uTexel.x) * (towardsPos ? 1.0 : -1.0);
   float lPair = towardsPos ? lPos : lNeg;
   float gradientScaled = 0.25 * max(gradNeg, gradPos);
   float localAverage = 0.5 * (lPair + lM);

   vec2 posB = uv;
   if (horzSpan)
      posB.y += 0.5 * stepLength;
   else
      posB.x += 0.5 * stepLength;
   vec2 along = horzSpan ? vec2(uTexel.x, 0.0) : vec2(0.0, uTexel.y);

   // Walk both ways along the edge until its luma leaves the pair's average.
   vec2 posN = posB - along * kStep[0];
   vec2 posP = posB + along * kStep[0];
   float endN = lumaAt(posN) - localAverage;
   float endP = lumaAt(posP) - localAverage;
   bool doneN = abs(endN) >= gradientScaled;
   bool doneP = abs(endP) >= gradientScaled;
   for (int i = 1; i < kSteps && !(doneN && doneP); i++)
   {
      if (!doneN)
      {
         posN -= along * kStep[i];
         endN = lumaAt(posN) - localAverage;
         doneN = abs(endN) >= gradientScaled;
      }
      if (!doneP)
      {
         posP += along * kStep[i];
         endP = lumaAt(posP) - localAverage;
         doneP = abs(endP) >= gradientScaled;
      }
   }
   float distN = horzSpan ? uv.x - posN.x : uv.y - posN.y;
   float distP = horzSpan ? posP.x - uv.x : posP.y - uv.y;
   bool nearerN = distN < distP;
   float dist = min(distN, distP);
   float spanLength = distN + distP;
   // Blend only towards an end where the edge really changes side.
   bool centreBelow = lM - localAverage < 0.0;
   bool goodSpan = ((nearerN ? endN : endP) < 0.0) != centreBelow;
   float pixelOffset = goodSpan ? 0.5 - dist / spanLength : 0.0;
   float offset = max(pixelOffset, subpix);

   vec2 finalUv = uv;
   if (horzSpan)
      finalUv.y += offset * stepLength;
   else
      finalUv.x += offset * stepLength;
   outColor = vec4(textureLod(uSrc, finalUv, 0.0).rgb, rgbaM.a);
}

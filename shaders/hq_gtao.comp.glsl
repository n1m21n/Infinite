#version 430
// Turbo 0.71 (S5): Render 3D HQ - ground-truth ambient occlusion (Jimenez et
// al. 2016) at the AO size. Ported from Filament
// filament/src/materials/ssao/gtaoImpl.fs and gtao.mat @ 13be3649 (Apache
// License 2.0, Copyright Google LLC): the slices, steps, horizon update with
// distance falloff and thickness heuristic, the cosine-weighted arc integral,
// the Activision 4x4 noise and the power / intensity epilogue are Filament's.
// Changes: compute instead of a fragment pass; the view normal is read from
// the prepass target instead of being reconstructed from depth; no bent
// normals or visibility bitmasks (Filament's defaults are off too).
// Writes (visibility, view z) for the bilateral blur.
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uZ;      // view z, mip chain (nearest)
layout(binding = 1) uniform sampler2D uNormal; // prepass: view normal (xyz), frame size
layout(rg16f, binding = 0) uniform writeonly image2D uAo;

uniform ivec2 uSize;            // AO size (level 0 of uZ)
uniform vec4 uProjInfo;         // proj m[0], m[5], m[8], m[9]
uniform vec2 uProjOffset;       // proj m[12], m[13] (orthographic)
uniform int uOrtho;
uniform float uFarZ;            // view z of the far plane: nothing drawn there
uniform float uProjectionScale; // min(0.5 m[0] w, 0.5 m[5] h) at the AO size
uniform float uRadius;          // metres
uniform float uInvRadiusSquared;
uniform float uIntensity;
uniform float uPower;
uniform float uMaxLevel;
uniform vec2 uFullTexel;        // 1 / frame size
uniform int uLevel0Shift;       // S8: frame -> AO size, log2 (1 half resolution, 0 full)
// S6: with TAA, the slice directions and the first step turn every frame
// (golden-ratio steps over the spatial 4x4 pattern) so the history averages
// many more directions than 4 - small objects stop flickering. 0 = static.
uniform int uNoiseFrame;

// Filament's GtaoOptions defaults.
const float kSliceCount = 4.0;
const float kStepsPerSlice = 3.0;
const float kThicknessHeuristic = 0.004;
const float kLog2LodRate = 3.0;
const float PI = 3.14159265359;
const float HALF_PI = 1.57079632679;

// The view-space position of the depth sample under uv at a level: texel t
// of level L holds the full-size pixel t * 2^(L+1) (hq_ao_depth; 2^L at full
// AO resolution), so x / y come from that pixel's centre, exactly.
vec3 viewPos(vec2 uv, float level)
{
   int L = int(level);
   ivec2 size = textureSize(uZ, L);
   ivec2 t = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
   float z = texelFetch(uZ, t, L).r;
   vec2 ndc = (vec2(t << (L + uLevel0Shift)) + 0.5) * uFullTexel * 2.0 - 1.0;
   if (uOrtho == 1)
      return vec3((ndc - uProjOffset) / uProjInfo.xy, z);
   return vec3(-z * (ndc + uProjInfo.zw) / uProjInfo.xy, z);
}

float integrateArcCosWeight(float h, float n)
{
   float arc = -cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n);
   return 0.25 * arc;
}

float updateHorizon(float sampleHorizonCos, float currentHorizonCos, float fallOff)
{
   return sampleHorizonCos > currentHorizonCos
      ? mix(sampleHorizonCos, currentHorizonCos, fallOff)
      : mix(currentHorizonCos, sampleHorizonCos, kThicknessHeuristic);
}

float calculateHorizonCos(vec3 sampleDelta, vec3 viewDir, float horizonCos)
{
   float sqSampleDist = dot(sampleDelta, sampleDelta);
   float invSampleDist = inversesqrt(max(sqSampleDist, 1e-12));
   float fallOff = clamp(sqSampleDist * uInvRadiusSquared * 2.0, 0.0, 1.0);
   float shc = dot(sampleDelta, viewDir) * invSampleDist;
   return updateHorizon(shc, horizonCos, fallOff);
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   vec2 uv = (vec2(p) + 0.5) / vec2(uSize);
   vec3 origin = viewPos(uv, 0.0);
   ivec2 np = min(p << uLevel0Shift, textureSize(uNormal, 0) - 1); // the pixel level 0 took its depth from
   vec3 normal = texelFetch(uNormal, np, 0).xyz;
   if (origin.z <= uFarZ * 0.9999 || dot(normal, normal) < 0.25)
   {
      imageStore(uAo, p, vec4(1.0, origin.z, 0.0, 0.0)); // nothing drawn: open
      return;
   }
   normal = normalize(normal);
   vec3 viewDir = uOrtho == 1 ? vec3(0.0, 0.0, 1.0) : normalize(-origin);

   float ssRadius = uOrtho == 1 ? uProjectionScale * uRadius : -(uProjectionScale * uRadius / origin.z);
   float noiseDirection = float((((p.x + p.y) & 3) << 2) | (p.x & 3)) * (1.0 / 16.0);
   float initialRayStep = fract(0.25 * float((p.y - p.x) & 3));
   noiseDirection = fract(noiseDirection + 0.6180340 * float(uNoiseFrame));
   initialRayStep = fract(initialRayStep + 0.7548777 * float(uNoiseFrame));
   float stepRadius = ssRadius / (kStepsPerSlice + 1.0);
   vec2 texel = 1.0 / vec2(uSize);

   float visibility = 0.0;
   for (float i = 0.0; i < kSliceCount; i += 1.0)
   {
      float phi = (i + noiseDirection) / kSliceCount * PI;
      vec2 omega = vec2(cos(phi), sin(phi));
      vec3 direction = vec3(omega, 0.0);
      vec3 orthoDirection = normalize(direction - dot(direction, viewDir) * viewDir);
      vec3 axis = cross(orthoDirection, viewDir);
      vec3 projNormal = normal - axis * dot(normal, axis);
      float signNorm = sign(dot(orthoDirection, projNormal));
      float projNormalLength = length(projNormal);
      float cosNorm = clamp(dot(projNormal, viewDir) / max(projNormalLength, 1e-6), 0.0, 1.0);
      float n = signNorm * acos(cosNorm);

      float horizonCos0 = -1.0;
      float horizonCos1 = -1.0;
      for (float j = 0.0; j < kStepsPerSlice; j += 1.0)
      {
         vec2 sampleOffset = max((j + initialRayStep) * stepRadius, 1.0 + j) * omega;
         float level = clamp(floor(log2(length(sampleOffset))) - kLog2LodRate, 0.0, uMaxLevel);
         vec2 uvSampleOffset = sampleOffset * texel;
         vec3 sampleDelta0 = viewPos(uv + uvSampleOffset, level) - origin;
         vec3 sampleDelta1 = viewPos(uv - uvSampleOffset, level) - origin;
         horizonCos0 = calculateHorizonCos(sampleDelta0, viewDir, horizonCos0);
         horizonCos1 = calculateHorizonCos(sampleDelta1, viewDir, horizonCos1);
      }

      float h0 = -acos(clamp(horizonCos1, -1.0, 1.0));
      float h1 = acos(clamp(horizonCos0, -1.0, 1.0));
      h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
      h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
      visibility += projNormalLength * (integrateArcCosWeight(h0, n) + integrateArcCosWeight(h1, n));
   }
   float occlusion = 1.0 - clamp(visibility / kSliceCount, 0.0, 1.0);
   float aoVisibility = pow(clamp(1.0 - occlusion, 0.0, 1.0), uPower);
   aoVisibility = mix(1.0, aoVisibility, uIntensity);
   imageStore(uAo, p, vec4(aoVisibility, origin.z, 0.0, 0.0));
}

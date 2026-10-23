#version 450 core
// Ported from Filament libs/ibl/src/CubemapIBL.cpp (DFV_Multiscatter,
// hemisphereImportanceSampleDggx) and tools/cmgen/src/cmgen.cpp (level ->
// roughness) @ 13be3649110cfcf1a2424c9fcd19709b3e4c0f8f (Apache License 2.0,
// Copyright Google LLC).
// Changes: one fragment pass per map on the GPU instead of cmgen on the CPU;
// equirectangular maps instead of cubemaps; the irradiance is a cosine-weighted
// map instead of L2 spherical harmonics; the sheen term (dfg.z) is the Charlie
// x Neubelt directional albedo estimated with uniformly sampled directions.
// Turbo 0.71 (S2): Render 3D HQ image-based lighting, built once per HDRI.
//   uMode 0: prefiltered radiance for perceptual roughness uPerceptualRoughness
//   uMode 1: irradiance (cosine-weighted average radiance)
//   uMode 2: DFG table, x = NoV, y = perceptual roughness:
//            r = sum Fc V NoL VoH/NoH, g = sum V NoL VoH/NoH (multiscatter
//            layout, f90 = 1), b = sheen directional albedo
in vec2 vNdc;

uniform int uMode;
uniform sampler2D uEnv;
uniform float uPerceptualRoughness;
uniform float uSrcTexels;  // texels in the HDRI's base level
uniform float uSrcMaxLod;

layout(location = 0) out vec4 outColor;

//@include hq_common.glsl
//@include hq_brdf.glsl

vec2 hammersley(uint i, float invN)
{
   return vec2(float(i) * invN, float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// pdf = D(a) * cosTheta
vec3 hemisphereImportanceSampleDggx(vec2 u, float a)
{
   float phi = 2.0 * PI * u.x;
   float cosTheta2 = (1.0 - u.y) / (1.0 + (a + 1.0) * ((a - 1.0) * u.y));
   float cosTheta = sqrt(cosTheta2);
   float sinTheta = sqrt(1.0 - cosTheta2);
   return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

mat3 frameAround(vec3 n)
{
   vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
   vec3 t = normalize(cross(up, n));
   return mat3(t, cross(n, t), n);
}

vec3 prefilter(vec3 n, float pr)
{
   vec2 uv0 = dirToEquirect(n, 0.0);
   if (pr < 1e-3)
      return textureLod(uEnv, uv0, 0.0).rgb;
   float a = pr * pr;
   mat3 tbn = frameAround(n);
   const uint N = 256u;
   vec3 acc = vec3(0.0);
   float w = 0.0;
   for (uint i = 0u; i < N; i++)
   {
      vec3 h = tbn * hemisphereImportanceSampleDggx(hammersley(i, 1.0 / float(N)), a);
      vec3 l = normalize(2.0 * dot(n, h) * h - n);
      float NoL = dot(n, l);
      if (NoL <= 0.0)
         continue;
      // Filtered importance sampling: read the mip whose texel matches the
      // sample's solid angle (v = n, so pdf(l) = D / 4).
      float NoH = max(dot(n, h), 0.0);
      float pdf = D_GGX(a, NoH) / 4.0 + 1e-4;
      float omegaS = 1.0 / (float(N) * pdf);
      float omegaP = 4.0 * PI / uSrcTexels;
      float lod = clamp(0.5 * log2(omegaS / omegaP) + 1.0, 0.0, uSrcMaxLod);
      acc += textureLod(uEnv, dirToEquirect(l, 0.0), lod).rgb * NoL;
      w += NoL;
   }
   return acc / max(w, 1e-4);
}

vec3 irradiance(vec3 n)
{
   mat3 tbn = frameAround(n);
   const uint N = 512u;
   float lod = max(uSrcMaxLod - 5.0, 0.0); // ~32 x 16: the integral is smooth
   vec3 acc = vec3(0.0);
   for (uint i = 0u; i < N; i++)
   {
      vec2 xi = hammersley(i, 1.0 / float(N));
      float r = sqrt(xi.x), phi = 2.0 * PI * xi.y;
      vec3 l = tbn * vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - xi.x, 0.0)));
      acc += textureLod(uEnv, dirToEquirect(l, 0.0), lod).rgb;
   }
   return acc / float(N);
}

vec3 dfg(float NoV, float pr)
{
   float a = pr * pr; // linear roughness
   vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
   vec2 r = vec2(0.0);
   const uint N = 1024u;
   for (uint i = 0u; i < N; i++)
   {
      vec3 H = hemisphereImportanceSampleDggx(hammersley(i, 1.0 / float(N)), a);
      vec3 L = 2.0 * dot(V, H) * H - V;
      float VoH = clamp(dot(V, H), 0.0, 1.0);
      float NoL = clamp(L.z, 0.0, 1.0);
      float NoH = clamp(H.z, 0.0, 1.0);
      if (NoL > 0.0)
      {
         float v = V_SmithGGXCorrelated(a, NoV, NoL) * NoL * (VoH / NoH);
         float Fc = pow5(1.0 - VoH);
         r.x += v * Fc;
         r.y += v;
      }
   }
   r *= 4.0 / float(N);

   // Sheen: integral of D_Charlie * V_Neubelt * NoL over the hemisphere, with
   // L uniform (pdf 1 / 2pi).
   float sheen = 0.0;
   const uint M = 4096u;
   for (uint i = 0u; i < M; i++)
   {
      vec2 u = hammersley(i, 1.0 / float(M));
      float cosT = u.y, sinT = sqrt(1.0 - cosT * cosT), phi = 2.0 * PI * u.x;
      vec3 L = vec3(sinT * cos(phi), sinT * sin(phi), cosT);
      vec3 H = normalize(V + L);
      float NoL = L.z;
      if (NoL > 0.0)
         sheen += D_Charlie(max(a, 1e-4), clamp(H.z, 0.0, 1.0)) * V_Neubelt(NoV, NoL) * NoL;
   }
   sheen *= 2.0 * PI / float(M);
   return vec3(r, sheen);
}

void main()
{
   vec2 uv = vNdc * 0.5 + 0.5;
   if (uMode == 2)
   {
      outColor = vec4(dfg(max(uv.x, MIN_N_DOT_V), uv.y), 1.0);
      return;
   }
   vec3 n = uvToDir(uv);
   outColor = vec4(uMode == 0 ? prefilter(n, uPerceptualRoughness) : irradiance(n), 1.0);
}

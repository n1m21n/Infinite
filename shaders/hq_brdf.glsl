// Ported from Filament shaders/src/surface_brdf.fs, surface_material.fs,
// surface_shading_lit.fs, surface_shading_model_standard.fs and
// surface_ambient_occlusion.fs @ 13be3649110cfcf1a2424c9fcd19709b3e4c0f8f
// (Apache License 2.0, Copyright Google LLC).
// Changes: GL 4.6 forward pass, desktop (HIGH quality) paths only, no fp16
// guards, plain functions instead of the PixelParams / Light structs.
// Turbo 0.71 (S2): Render 3D HQ, included by hq_surface.frag.glsl
// (docs/filament-notes.md section 2).

#define MIN_PERCEPTUAL_ROUGHNESS 0.045
#define MIN_N_DOT_V 1e-4

float pow5(float x)
{
   float x2 = x * x;
   return x2 * x2 * x;
}

float max3(vec3 v)
{
   return max(v.x, max(v.y, v.z));
}

// --- surface_brdf.fs -------------------------------------------------------
float D_GGX(float roughness, float NoH)
{
   float oneMinusNoHSquared = 1.0 - NoH * NoH;
   float a = NoH * roughness;
   float k = roughness / (oneMinusNoHSquared + a * a);
   return k * k * (1.0 / PI);
}

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
   float a2 = roughness * roughness;
   float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
   float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
   return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

float V_Kelemen(float LoH)
{
   return 0.25 / max(LoH * LoH, 0.0000039);
}

float V_Neubelt(float NoV, float NoL)
{
   return 1.0 / max(4.0 * (NoL + NoV - NoL * NoV), 0.00001532);
}

// Sheen (Charlie): (2 + 1/a) * pow(max(1 - NoH^2, 2^-7), 0.5/a) / (2 PI).
float D_Charlie(float roughness, float NoH)
{
   float invAlpha = 1.0 / roughness;
   float cos2h = NoH * NoH;
   float sin2h = max(1.0 - cos2h, 0.0078125);
   return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

vec3 F_Schlick(vec3 f0, float f90, float VoH)
{
   return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

float F_Schlick(float f0, float f90, float VoH)
{
   return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

vec3 fresnel(vec3 f0, float LoH)
{
   float f90 = clamp(dot(f0, vec3(50.0 * 0.33)), 0.0, 1.0);
   return F_Schlick(f0, f90, LoH);
}

float Fd_Lambert()
{
   return 1.0 / PI;
}

// --- surface_shading_lit.fs: geometric specular anti-aliasing ----------------
// (Tokuyoshi-Kaplanyan 2019; the material builder's defaults 0.15 / 0.2.)
float computeKernelRoughness(vec3 worldNormal, float variance, float threshold)
{
   vec3 du = dFdx(worldNormal);
   vec3 dv = dFdy(worldNormal);
   float v = variance * (dot(du, du) + dot(dv, dv));
   return min(2.0 * v, threshold);
}

float normalFiltering(float kernelRoughness, float perceptualRoughness)
{
   float roughness = perceptualRoughness * perceptualRoughness;
   float squareRoughness = clamp(roughness * roughness + kernelRoughness, 0.0, 1.0);
   return sqrt(sqrt(squareRoughness));
}

// --- surface_ambient_occlusion.fs -------------------------------------------
float SpecularAO_Lagarde(float NoV, float visibility, float roughness)
{
   return clamp(pow(NoV + visibility, exp2(-16.0 * roughness - 1.0)) - 1.0 + visibility, 0.0, 1.0);
}

vec3 gtaoMultiBounce(float visibility, vec3 albedo)
{
   vec3 a = 2.0404 * albedo - 0.3324;
   vec3 b = -4.7951 * albedo + 0.6417;
   vec3 c = 2.7552 * albedo + 0.6903;
   return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

float computeMicroShadowing(float NoL, float visibility)
{
   float aperture = inversesqrt(1.0 - min(visibility, 0.9999));
   float microShadow = clamp(NoL * aperture, 0.0, 1.0);
   return microShadow * microShadow;
}

// --- surface_light_indirect.fs ------------------------------------------------
// LOD of the prefiltered map for a roughness: L * pr * (2 - pr), L = levels - 1
// (cmgen builds level i at pr = 1 - sqrt(1 - i / L); HqRenderer does the same).
float perceptualRoughnessToLod(float perceptualRoughness, float levelOne)
{
   return levelOne * perceptualRoughness * (2.0 - perceptualRoughness);
}

vec3 getSpecularDominantDirection(vec3 n, vec3 r, float roughness)
{
   return mix(r, n, roughness * roughness);
}

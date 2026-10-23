# Filament rendering notes (for the OpenGL 4.6 port)

Source: https://github.com/google/filament (Apache 2.0). All snippets are copied verbatim from the files named
in each section (pure-comment lines and blank lines stripped to save space, `// ...` marks elided code).
Paths are relative to the repo root. Nothing here is from memory; where I could not verify something it is
marked **UNVERIFIED**.

## 1. Snapshot

- Branch `main`, commit **13be3649110cfcf1a2424c9fcd19709b3e4c0f8f** ("gltfio: Fix getEntitiesByPrefix (#10684)")
- Committer date **2026-10-23T04:17:22Z**
- Fetched with `gh api repos/google/filament/commits/main`. (WebFetch on api.github.com returned HTTP 403, and curl on
  raw.githubusercontent.com/.../shaders/src/brdf.fs returned 404 because the file was renamed.) All files were then read from a shallow
  `git fetch --depth 1` of that exact SHA, so every snippet corresponds to that commit.
- File names changed vs older Filament: the lit shader is now `shaders/src/surface_*.fs` (e.g. `surface_brdf.fs`,
  `surface_shading_model_standard.fs`, `surface_light_indirect.fs`, `surface_shading_lit.fs`, `surface_material.fs`,
  `surface_ambient_occlusion.fs`).
- Conventions: `perceptualRoughness` = user "roughness"; `roughness` = `perceptualRoughness^2` (alpha). Reversed-Z, view-space -Z forward.
  `PI` etc. from `common_math.glsl`. `pow5(x) = x2*x2*x` (x2 = x*x). `FILAMENT_QUALITY` defaults to HIGH on the desktop shader model and LOW on mobile (`libs/filamat/src/shaders/CodeGenerator.cpp:1005-1025`); HIGH selects the exact V term.

---

## 2. BRDF, IBL, clear coat, sheen, roughness remap

### 2.1 Roughness remap and clamps - `shaders/src/surface_material.fs`

```glsl
#if defined(TARGET_MOBILE)
    #define MIN_PERCEPTUAL_ROUGHNESS 0.089
    #define MIN_ROUGHNESS            0.007921
#else
    #define MIN_PERCEPTUAL_ROUGHNESS 0.045
    #define MIN_ROUGHNESS            0.002025
#endif
#define MIN_N_DOT_V 1e-4
// ...
vec3 computeF0(const vec4 baseColor, float metallic, float reflectance) {
    return baseColor.rgb * metallic + (reflectance * (1.0 - metallic));
}
float computeDielectricF0(float reflectance) {
    return 0.16 * reflectance * reflectance;
}
// ...
vec3 f0ClearCoatToSurface(const vec3 f0) {
#if FILAMENT_QUALITY == FILAMENT_QUALITY_LOW
    return saturate(f0 * (f0 * 0.526868 + 0.529324) - 0.0482256);
#else
    return saturate(f0 * (f0 * (0.941892 - 0.263008 * f0) + 0.346479) - 0.0285998);
#endif
}
```
Notes: `roughness = perceptualRoughness^2` (`perceptualRoughnessToRoughness`) and the inverse is `sqrt` (same file, lines 37-43). Desktop `MIN_PERCEPTUAL_ROUGHNESS = 0.045` (MIN_ROUGHNESS 0.002025 = 0.045^2); mobile uses 0.089 (fp16 safety). `reflectance`
default 0.5 gives dielectric f0 = 0.16*0.25 = 0.04. `MIN_N_DOT_V = 1e-4` (`clampNoV`).

### 2.2 Pixel parameter setup (clamps, clearcoat/base roughness coupling, energy compensation) - `shaders/src/surface_shading_lit.fs`

```glsl
#if defined(MATERIAL_HAS_CLEAR_COAT) && defined(MATERIAL_HAS_CLEAR_COAT_ROUGHNESS)
    float basePerceptualRoughness = max(perceptualRoughness, pixel.clearCoatPerceptualRoughness);
    perceptualRoughness = mix(perceptualRoughness, basePerceptualRoughness, pixel.clearCoat);
#endif
    pixel.perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    pixel.roughness = perceptualRoughnessToRoughness(pixel.perceptualRoughness);
// ...
void getEnergyCompensationPixelParams(inout PixelParams pixel) {
    pixel.dfg = prefilteredDFG(pixel.perceptualRoughness, shading_NoV);
#if !defined(SHADING_MODEL_CLOTH)
    pixel.energyCompensation = 1.0 + pixel.f0 * (1.0 / pixel.dfg.y - 1.0);
#else
    pixel.energyCompensation = vec3(1.0);
#endif
// ...
#if !defined(SHADING_MODEL_CLOTH)
#if defined(MATERIAL_HAS_SHEEN_COLOR)
    pixel.sheenDFG = prefilteredDFG(pixel.sheenPerceptualRoughness, shading_NoV).z;
    pixel.sheenScaling = 1.0 - max3(pixel.sheenColor) * pixel.sheenDFG;
#endif
#endif
```
Notes: the order is: specular AA (kernelRoughness) -> clearcoat roughness bump of the base -> clamp to [MIN_PERCEPTUAL_ROUGHNESS, 1] -> square.
`pixel.dfg = prefilteredDFG(perceptualRoughness, NoV)` (the LUT, see 2.6) feeds `energyCompensation = 1 + f0 * (1/dfg.y - 1)`, which multiplies
the *direct* specular lobe (`surfaceShading`) and the *IBL* specular. `dfg.z` is the cloth/sheen DFG term (3rd LUT channel).
`f0ClearCoatToSurface` (2.1) is applied to f0 under `CLEAR_COAT_IOR_CHANGE` (I did not trace where that define is set).

### 2.3 Specular anti-aliasing (geometric, normal variance -> roughness) - `shaders/src/surface_shading_lit.fs`

```glsl
#if defined(GEOMETRIC_SPECULAR_AA)
float computeKernelRoughness(const vec3 worldNormal) {
    vec3 du = dFdx(worldNormal);
    vec3 dv = dFdy(worldNormal);
    float variance = materialParams._specularAntiAliasingVariance *
            dot(vec2(dot(du, du), dot(dv, dv)), sq(frameUniforms.derivativesScale));
    return min(2.0 * variance, materialParams._specularAntiAliasingThreshold);
}
float normalFiltering(float kernelRoughness, float perceptualRoughness) {
    float roughness = perceptualRoughnessToRoughness(perceptualRoughness);
    float squareRoughness = saturate(sq(roughness) + kernelRoughness);
    return roughnessToPerceptualRoughness(sqrt(squareRoughness));
}
#endif
```
Defaults: `libs/filamat/include/filamat/MaterialBuilder.h:956-957,977`: `mSpecularAntiAliasingVariance = 0.15f`,
`mSpecularAntiAliasingThreshold = 0.2f`, `mSpecularAntiAliasing = false` (opt-in per material; `GEOMETRIC_SPECULAR_AA` define).
`frameUniforms.derivativesScale` = (1,1) at native res; `1/upscaling` under TAA upscaling (`Renderer.cpp prepareUpscaler`).
The geometric normal (not the normal-mapped one) is differentiated. This is Tokuyoshi-Kaplanyan 2019 (kernel variance added to alpha^2).
Note this is a *geometric* filter only; there is no separate normal-map variance (Toksvig/LEAN) path in this snapshot (searched `shaders/src`).

### 2.4 Specular BRDF terms - `shaders/src/surface_brdf.fs`

```glsl
float D_GGX(float roughness, float NoH, const vec3 h) {
#if defined(TARGET_MOBILE)
    vec3 NxH = cross(shading_normal, h);
    float oneMinusNoHSquared = dot(NxH, NxH);
#else
    float oneMinusNoHSquared = 1.0 - NoH * NoH;
#endif
    float a = NoH * roughness;
    float k = min(roughness / (oneMinusNoHSquared + a * a), 453.5); // 453.5 prevents fp16 overflow
    float d = k * (k * (1.0 / PI));
    return d;
}
// ...
float V_SmithGGXCorrelated(float roughness, float NoV, float NoL) {
    float a2 = roughness * roughness;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    float v = PREVENT_DIV0(0.5, lambdaV + lambdaL, 0.0000077);
    return v;
}
float V_SmithGGXCorrelated_Fast(float roughness, float NoV, float NoL) {
    float v = PREVENT_DIV0(0.5, mix(2.0 * NoL * NoV, NoL + NoV, roughness), 0.0000077);
    return v;
}
// ...
float V_Kelemen(float LoH) {
    return PREVENT_DIV0(0.25, LoH * LoH, 0.0000039);
}
float V_Neubelt(float NoV, float NoL) {
    return PREVENT_DIV0(1.0, 4.0 * (NoL + NoV - NoL * NoV), 0.00001532);
}
// ...
vec3 F_Schlick(const vec3 f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}
// ...
vec3 fresnel(const vec3 f0, float LoH) {
#if BRDF_SPECULAR_F == SPECULAR_F_SCHLICK
#if FILAMENT_QUALITY == FILAMENT_QUALITY_LOW
    return F_Schlick(f0, LoH); // f90 = 1.0
#else
    float f90 = saturate(dot(f0, vec3(50.0 * 0.33)));
    return F_Schlick(f0, f90, LoH);
#endif
#endif
}
// ...
float Fd_Lambert() {
    return 1.0 / PI;
}
```
Notes: scalar `F_Schlick(f0, f90, VoH)` is the same formula (`f0 + (f90 - f0) * pow5(1 - VoH)`); a `F_Schlick(f0, VoH)` overload assumes f90 = 1. `PREVENT_DIV0(n,d,magic) = n / max(d, magic)` (mediump only; `n/d` otherwise, see `common_math.glsl:22-24`). Desktop `D_GGX` uses
`oneMinusNoHSquared = 1 - NoH*NoH` (the cross(N,H) form is `TARGET_MOBILE` only). `V_SmithGGXCorrelated_Fast` is the Hammon 2017 lerp
(used when `FILAMENT_QUALITY < HIGH`). `fresnel()` uses `f90 = saturate(dot(f0, vec3(50*0.33)))` (reflectance-based f90) except at LOW quality.
Diffuse is **Lambert** (`BRDF_DIFFUSE = DIFFUSE_LAMBERT`); Burley exists but is off. The Lambert `1/PI` multiplies `diffuseColor` in `diffuseLobe`.
Anisotropic GGX, Charlie (sheen), Kelemen (clear coat V), Neubelt (cloth V), iridescence are in the same file.

### 2.5 Direct lighting assembly incl. clear coat and sheen lobes - `shaders/src/surface_shading_model_standard.fs`

```glsl
#if defined(MATERIAL_HAS_SHEEN_COLOR)
vec3 sheenLobe(const PixelParams pixel, float NoV, float NoL, float NoH) {
    float D = distributionCloth(pixel.sheenRoughness, NoH);
    float V = visibilityCloth(NoV, NoL);
    return (D * V) * pixel.sheenColor;
}
#endif
#if defined(MATERIAL_HAS_CLEAR_COAT)
float clearCoatLobe(const PixelParams pixel, const vec3 h, float NoH, float LoH, out float Fcc) {
#if defined(MATERIAL_HAS_NORMAL) || defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    float clearCoatNoH = saturate(dot(shading_clearCoatNormal, h));
#else
    float clearCoatNoH = NoH;
#endif
    float D = distributionClearCoat(pixel.clearCoatRoughness, clearCoatNoH, h);
    float V = visibilityClearCoat(LoH);
    float F = F_Schlick(0.04, 1.0, LoH) * pixel.clearCoat; // fix IOR to 1.5
    Fcc = F;
    return D * V * F;
}
#endif
// ...
vec3 surfaceShading(const PixelParams pixel, const Light light, float occlusion) {
    vec3 h = normalize(shading_view + light.l);
    float NoV = shading_NoV;
    float NoL = light.NoL;
    float NoH = saturate(dot(shading_normal, h));
    float LoH = saturate(dot(light.l, h));
    vec3 Fr = specularLobe(pixel, light, h, NoV, NoL, NoH, LoH) * pixel.energyCompensation;
// ...
    vec3 Fd = diffuseLobe(pixel, NoV, NoL, LoH);
// ...
    vec3 color = Fd + Fr;
#if defined(MATERIAL_HAS_SHEEN_COLOR)
    color *= pixel.sheenScaling;
    color += sheenLobe(pixel, NoV, NoL, NoH);
#endif
#if defined(MATERIAL_HAS_CLEAR_COAT)
    float Fcc;
    float clearCoat = clearCoatLobe(pixel, h, NoH, LoH, Fcc);
    float attenuation = 1.0 - Fcc;
#if defined(MATERIAL_HAS_NORMAL) || defined(MATERIAL_HAS_CLEAR_COAT_NORMAL)
    color *= attenuation * NoL;
    float clearCoatNoL = saturate(dot(shading_clearCoatNormal, light.l));
    color += clearCoat * clearCoatNoL;
    return (color * light.colorIntensity.rgb) *
            (light.colorIntensity.w * light.attenuation * occlusion);
#else
    color *= attenuation;
    color += clearCoat;
#endif
#endif
    return (color * light.colorIntensity.rgb) *
            (light.colorIntensity.w * light.attenuation * NoL * occlusion);
}
```
Sheen lobe: `D_Charlie(sheenRoughness, NoH) * V_Neubelt(NoV, NoL) * sheenColor`, base layer scaled by `sheenScaling = 1 - max3(sheenColor) * sheenDFG`
(`sheenDFG = prefilteredDFG(sheenPerceptualRoughness, NoV).z`, lines 274-279 of `surface_shading_lit.fs`).
`D_Charlie`: `(2 + 1/a) * pow(max(1 - NoH^2, 2^-7), 0.5/a) / (2*PI)` with a = sheenRoughness (alpha). `V_Neubelt = 1 / (4*(NoL + NoV - NoL*NoV))`.
Clear coat lobe: IOR fixed 1.5 (f0 = 0.04), `D_GGX(clearCoatRoughness)`, `V_Kelemen = 0.25 / LoH^2`, `F = F_Schlick(0.04, 1, LoH) * clearCoat`; base is attenuated by `1 - Fcc`.
With a normal map the clear coat uses the *geometric* normal (`shading_clearCoatNormal`).
`light.colorIntensity` is already pre-exposed. `occlusion` here is the shadow visibility (not SSAO; see section 4 for where AO is applied).

### 2.6 DFG LUT: how it is built and used

Runtime texture: `filament/src/DFG.cpp` loads a precomputed `RGB16F` table, `DFG_LUT_SIZE` x `DFG_LUT_SIZE` (`filament/CMakeLists.txt:346-352`: 128 desktop, 64 mobile).
Generated at build time by (`filament/CMakeLists.txt:~676`):
`cmgen --size=128 --ibl-dfg-multiscatter --ibl-dfg-cloth --ibl-dfg=dfg.bin` -> `libs/ibl/src/CubemapIBL.cpp` `CubemapIBL::DFG()`.

Build code (verbatim; `hammersley` = radical inverse, see below):

```cpp
static float3 hemisphereImportanceSampleDggx(float2 u, float a) { // pdf = D(a) * cosTheta
    const float phi = 2.0f * (float) F_PI * u.x;
    const float cosTheta2 = (1 - u.y) / (1 + (a + 1) * ((a - 1) * u.y));
    const float cosTheta = std::sqrt(cosTheta2);
    const float sinTheta = std::sqrt(1 - cosTheta2);
    return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
}
// ...
static float2 DFV_Multiscatter(float NoV, float linearRoughness, size_t numSamples) {
    float2 r = 0;
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            const float v = Visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
            const float Fc = pow5(1 - VoH);
            // Fc = (1-VoH)^5, assuming f90 = 1; Visibility() = V_SmithGGXCorrelated with a = linearRoughness (cibl.cpp:169-176)
            r.x += v * Fc;
            r.y += v;
        }
    }
    return r * (4.0f / numSamples);
}
// ...
                    const float h = (float) height;
                    const float coord = saturate((h - y + 0.5f) / h);
                    const float linear_roughness = coord * coord;
                    for (size_t x = 0; x < width; x++, data++) {
                        const float NoV = saturate((x + 0.5f) / width);
                        float3 r = { dfvFunction(NoV, linear_roughness, 1024), 0 };
                        if (cloth) {
                            r.b = float(DFV_Charlie_Uniform(NoV, linear_roughness, 4096));
                        }
                        *data = r;
                    }
```
`hammersley(i, iN)` (`libs/ibl/include/ibl/utilities.h`): `bits = bitreverse32(i); return { i * iN, bits * (0.5f / 0x80000000U) };`
(the standard radical inverse, matches the GLSL `hammersley()` in 2.7).

Layout: **x = NoV** (texel centre `(x+0.5)/width`), **y = perceptualRoughness** (alpha = coord^2). The cmgen code writes
`coord = (h - y + 0.5)/h` per row so row 0 holds roughness ~1; the shader samples `vec2(NoV, perceptualRoughness)` directly
(see 2.7), so the stored row order is flipped relative to texture v. **UNVERIFIED**: I did not read the `.bin` writer, so if you generate your own
table just index it consistently with `v = perceptualRoughness`. Channels (multiscatter mode, `f90 = 1`):
`dfg.x = (4/N) sum Fc * V * NoL * VoH/NoH`, `dfg.y = (4/N) sum V * NoL * VoH/NoH` (Fc = (1-VoH)^5), `dfg.z` = cloth/Charlie directional albedo (`DFV_Charlie_Uniform`, 4096 uniform samples).
1024 GGX importance samples per texel. Reconstruction at runtime:
`E = mix(dfg.xxx, dfg.yyy, f0)` (= f0*dfg.y + (1-f0)*dfg.x) and multiscatter compensation `1 + f0*(1/dfg.y - 1)` (shader comment cites "Multiple-Scattering Microfacet BSDFs with the Smith Model"; see 2.2 and 2.7).
Without `--ibl-dfg-multiscatter` the classic split-sum is stored instead (`DFV`: x = sum (1-Fc)V.., y = sum Fc V..; `Er = f0*x + f90*y`).

### 2.7 Image based lighting - `shaders/src/surface_light_indirect.fs`

DFG lookup, SH irradiance, roughness -> LOD, dominant direction, specular DFG:

```glsl
vec3 PrefilteredDFG_LUT(float lod, float NoV) {
    return textureLod(sampler0_iblDFG, vec2(NoV, lod), 0.0).rgb;
}
// ...
vec3 Irradiance_SphericalHarmonics(const vec3 n) {
    vec3 sphericalHarmonics = frameUniforms.iblSH[0];
    if (CONFIG_SH_BANDS_COUNT >= 2) {
        sphericalHarmonics +=
                  frameUniforms.iblSH[1] * (n.y)
                + frameUniforms.iblSH[2] * (n.z)
                + frameUniforms.iblSH[3] * (n.x);
    }
    if (CONFIG_SH_BANDS_COUNT >= 3) {
        sphericalHarmonics +=
                  frameUniforms.iblSH[4] * (n.y * n.x)
                + frameUniforms.iblSH[5] * (n.y * n.z)
                + frameUniforms.iblSH[6] * (3.0 * n.z * n.z - 1.0)
                + frameUniforms.iblSH[7] * (n.z * n.x)
                + frameUniforms.iblSH[8] * (n.x * n.x - n.y * n.y);
    }
    return max(sphericalHarmonics, 0.0);
}
// ...
float perceptualRoughnessToLod(float perceptualRoughness) {
    return frameUniforms.iblRoughnessOneLevel * perceptualRoughness * (2.0 - perceptualRoughness);
}
vec3 prefilteredRadiance(const vec3 r, float lod) {
    return decodeDataForIBL(textureLod(sampler0_iblSpecular, r, lod));
}
// ...
vec3 getSpecularDominantDirection(const vec3 n, const vec3 r, float roughness) {
    return mix(r, n, roughness * roughness);
}
// ...
vec3 specularDFG(const PixelParams pixel) { return mix(pixel.dfg.xxx, pixel.dfg.yyy, pixel.f0); } // x pixel.specular with KHR specular factor; cloth: pixel.f0 * pixel.dfg.z
```
Final IBL assembly (prefiltered-cubemap path is the default: `IBL_INTEGRATION = IBL_INTEGRATION_PREFILTERED_CUBEMAP`):

```glsl
#if IBL_INTEGRATION == IBL_INTEGRATION_PREFILTERED_CUBEMAP
    vec3 E = specularDFG(pixel);
    if (ssrFr.a < 1.0) { // prevent reading the IBL if possible
        vec3 r = getReflectedVector(pixel, shading_normal);
        Fr = E * prefilteredRadiance(r, perceptualRoughnessToLod(pixel.perceptualRoughness));
        Fr *= pixel.energyCompensation;
// ...
    float ssao = evaluateSSAO(interpolationCache);
    float diffuseAO = min(material.ambientOcclusion, ssao);
    float specularAO = specularAO(shading_NoV, diffuseAO, pixel.roughness, interpolationCache);
    float specularSingleBounceAO = singleBounceAO(specularAO);
    Fr *= specularSingleBounceAO;
#if defined(MATERIAL_HAS_REFLECTIONS)
    ssrFr.rgb *= specularSingleBounceAO;
#endif
    float diffuseBRDF = singleBounceAO(diffuseAO); // Fd_Lambert() is baked in the SH below
    evaluateClothIndirectDiffuseBRDF(pixel, diffuseBRDF);
#if defined(MATERIAL_HAS_BENT_NORMAL)
    vec3 diffuseNormal = shading_bentNormal;
#else
    vec3 diffuseNormal = shading_normal;
#endif
#if IBL_INTEGRATION == IBL_INTEGRATION_PREFILTERED_CUBEMAP
    vec3 diffuseIrradiance = diffuseIrradiance(diffuseNormal);
#elif IBL_INTEGRATION == IBL_INTEGRATION_IMPORTANCE_SAMPLING
    vec3 diffuseIrradiance = isEvaluateDiffuseIBL(pixel, diffuseNormal, shading_view);
#endif
    vec3 Fd = pixel.diffuseColor * diffuseIrradiance * (1.0 - E) * diffuseBRDF;
    evaluateSubsurfaceIBL(pixel, diffuseIrradiance, Fd, Fr);
    multiBounceAO(diffuseAO, pixel.diffuseColor, Fd);
    multiBounceSpecularAO(specularAO, pixel.f0, Fr);
    evaluateSheenIBL(pixel, diffuseAO, interpolationCache, Fd, Fr);
    evaluateClearCoatIBL(pixel, diffuseAO, interpolationCache, Fd, Fr);
    Fr *= frameUniforms.iblLuminance;
    Fd *= frameUniforms.iblLuminance;
```
Layers (prefiltered path, `evaluateSheenIBL` lines 388-403 and `evaluateClearCoatIBL` 405-435): sheen: `Fd,Fr *= sheenScaling; Fr += sheenDFG * sheenColor * specularAO(sheenRoughness) * prefilteredRadiance(shading_reflected, lod(sheenPerceptualRoughness))`.
Clear coat: `Fc = F_Schlick(0.04, 1.0, clearCoatNoV) * clearCoat; Fd *= 1 - Fc; Fr *= 1 - Fc; Fr += prefilteredRadiance(clearCoatR, lod(clearCoatPerceptualRoughness)) * (specularAO(clearCoatNoV, diffuseAO, clearCoatRoughness) * Fc)`
(`clearCoatR` = reflection of the geometric normal when a normal map is present).

Prefiltered cubemap facts:
- `iblRoughnessOneLevel = ibl.getLevelCount() - 1` (`ColorPassDescriptorSet.cpp:373`): the highest mip is roughness 1. LOD = `L * pr * (2 - pr)` where L = that value.
- cmgen inverse (`tools/cmgen/src/cmgen.cpp:908-919`): `lod01 = level/(numLevels-1)`; `perceptualRoughness = (sqrt(a*a + 4*b*lod01) - a) / (2*b)` with a = 2, b = -1, which simplifies to `pr = 1 - sqrt(1 - lod01)` (inverse of `lod = L*pr*(2-pr)`), and `roughness = pr*pr`.
  Each level is built with `CubemapIBL::roughnessFilter(... roughness ...)` (GGX importance sampling, `g_num_samples` default 1024, doubled for each level >= 2). Default base size 256, `IBL_DEFAULT_MIN_LOD_SIZE` 16 (=> 5 levels, 256..16). Texture format R11F_G11F_B10F.
- Dominant direction: `r = mix(reflect(-v,n), n, roughness^2)` (roughness = alpha), see `getSpecularDominantDirection`.
- Irradiance: **L2 SH (9 RGB coefficients)** `frameUniforms.iblSH[9]`, evaluated by the polynomial above; coefficients are pre-multiplied on the CPU
  (`libs/ibl/src/CubemapSH.cpp preprocessSHForShader`: `SH[i] *= A[i] * (1/PI)` with `A = {1/(2sqrtPI), -sqrt3/(2sqrtPI), sqrt3/(2sqrtPI), -sqrt3/(2sqrtPI), sqrt15/(2sqrtPI), -sqrt15/(2sqrtPI), sqrt5/(4sqrtPI), -sqrt15/(2sqrtPI), sqrt15/(4sqrtPI)}`;
  `computeSH(..., irradiance=true)` already folds the cosine-lobe convolution). Fallback when `iblSH[0].x == 65504.0`: sample the roughness-one mip (4 taps at HIGH).
- Diffuse IBL: `Fd = diffuseColor * irradiance * (1 - E) * diffuseBRDF` (`E = specularDFG`), i.e. diffuse is darkened by the specular directional albedo.
- Specular IBL: `Fr = E * prefilteredRadiance(r, lod) * energyCompensation`, then `* iblLuminance` (intensity * exposure).
- Alternative `IBL_INTEGRATION_IMPORTANCE_SAMPLING` (64 Hammersley samples, filtered importance sampling) exists in the same file but is compile-time disabled; its `E` is `0` ("TODO").
- Specular occlusion: see section 4.3 (`specularAO`, `multiBounceSpecularAO`).
- Reflection mips need `textureLod` on a cube with trilinear; sampler0_iblSpecular is a mipmapped cubemap.

---

## 3. Options.h defaults - `filament/include/filament/Options.h`

All values quoted from the struct initialisers (all effects default **disabled** unless noted).

**DynamicResolutionOptions**: `minScale {0.5,0.5}`, `maxScale {1,1}`, `sharpness 0.9` (RCAS, [0,1]), `enabled false`, `homogeneousScaling false`,
`quality LOW`. Quality: LOW bilinear blit, MEDIUM = SGSR 1.0, HIGH = FSR1 mobile-optimised, ULTRA = FSR1 (FSR1/SGSR need a well anti-aliased noise-free input; avoid FXAA/dither). Forced to LOW for TRANSLUCENT views.

**BloomOptions**: `strength 0.10` (0..1), `resolution 384` (vertical axis of the bloom buffer, min 2^levels, silently clamped), `levels 6` (1..11, auto reduced), `blendMode ADD` (ADD | INTERPOLATE),
`threshold true` (threshold at 1.0 on the source), `enabled false`, `highlight 1000.0` (clamp highlights before bloom, [10,+inf]), `quality LOW`, `dirt nullptr`, `dirtStrength 0.2`,
lens flare: `lensFlare false`, `starburst true`, `chromaticAberration 0.005`, `ghostCount 4`, `ghostSpacing 0.6`, `ghostThreshold 10.0`, `haloThickness 0.1`, `haloRadius 0.4`, `haloThreshold 10.0`.

**AmbientOcclusionOptions** (SAO + GTAO + SSCT):
`aoType SAO` (SAO | GTAO), `radius 0.3` (m), `power 1.0`, `bias 0.0005` (no effect for GTAO), `resolution 0.5` (0.5 or 1.0), `intensity 1.0`, `bilateralThreshold 0.05` (depth distance = edge),
`quality LOW` (SAO sample count / blur sigma, see 4.4), `lowPassFilter MEDIUM` ("Recommend HIGH when GTAO"), `upsampling LOW` (HIGH enables the bilinear-bilateral upsample in `surface_ambient_occlusion.fs`),
`enabled false`, `bentNormals false` (also enables specular AO), `minHorizonAngleRad 0` (SAO only).
`Ssct` (ambient shadow from dominant light): `lightConeRad 1.0`, `shadowDistance 0.3`, `contactDistanceMax 1.0`, `intensity 0.8`, `lightDirection {0,-1,0}`, `depthBias 0.01`, `depthSlopeBias 0.01`, `sampleCount 4`, `rayCount 1`, `enabled false`.
`Gtao`: `sampleSliceCount 4` (# of slices; higher = less noise), `sampleStepsPerSlice 3` (higher = less bias), `thicknessHeuristic 0.004` (no effect with bitmasks), `useVisibilityBitmasks false` (no bent normals in that mode; changing at runtime recompiles shaders),
`constThickness 0.5` (world space, only with bitmasks), `linearThickness false` (thickness grows with distance; recompile).

**MultiSampleAntiAliasingOptions**: `enabled false`, `sampleCount 4`, `customResolve false`.

**TemporalAntiAliasingOptions**: `filterWidth 1.0` (deprecated, no effect), `feedback 0.12` (0 = max TAA .. 1 = none; 0.1 ~ 19 accumulated samples), `lodBias -1.0` (texture mip bias while TAA on; extra `-log2(upscaling)`),
`sharpness 0.0` (post-TAA FSR1 RCAS), `enabled false`, `upscaling 1.0` (BETA; disables dynamic res), `filterHistory true` (Catmull-Rom history), `filterInput true` (Lanczos-2 input reconstruction),
`useYCoCg false`, `hdr true`, `boxType AABB` (AABB | AABB_VARIANCE), `boxClipping ACCURATE` (ACCURATE | CLAMP | NONE), `jitterPattern HALTON_23_X16` (RGSS_X4, UNIFORM_HELIX_X4, HALTON_23_X8/X16/X32),
`varianceGamma 0.7` (range [0.6,1.25]; higher = ghosting, lower = jitter), `preventFlickering false` (Lottes luma-weighted alpha), `historyReprojection true`,
`motionAdaptiveHistory false`, `motionDeltaThreshold 1.0` (texels), `minBoxStrength 0.1`, `minAlphaStrength 0.3`, `skyDepthThreshold 0.001`, `motionHoldFrames 12.0`,
`depthDisocclusion false`, `disocclusionDepthThreshold 0.001`, `disocclusionHistoryNeighborhood true`, `exactLanczosSupport false`.
Most of these are shader *constants* (recompile on change); only `feedback`, `filterWidth`, `jitterPattern` are cheap to change at runtime (per the header comment).

**ScreenSpaceReflectionsOptions**: `thickness 0.1` (ray thickness, world units), `bias 0.01`, `maxDistance 3.0`, `stride 2.0` (texels), `enabled false`.
(SSR only applies to materials with `screenSpaceReflections` and roughness < sqrt(0.5); lookup LOD in `evaluateIBL`, lines 726-745.)

**GuardBandOptions**: `enabled false`. **RenderQuality.hdrColorBuffer** `HIGH` (RGB16F/RGBA16F). `enum AntiAliasing {NONE, FXAA}` and `enum Dithering {NONE, TEMPORAL}`: the header comments tag FXAA and TEMPORAL as "(default)".

**ShadowType** (`View::setShadowType`, default **PCF**, `View.h:645 mShadowType = ShadowType::PCF`): PCF | VSM (EVSM) | DPCF (deprecated, falls back to PCSS) | PCSS ("EVSM with soft shadows and contact hardening") | PCFd (debug).

**VsmShadowOptions**: `anisotropy 0`, `mipmapping false`, `msaaSamples 1`, `highPrecision false`, `minVarianceScale 0.5` (deprecated), `lightBleedReduction 0.15`.

**SoftShadowOptions** (PCSS): `penumbraScale 1.0`, `penumbraRatioScale 1.0`, `maxPenumbraRatio 10.0`, `maxSearchRadius 1.0` (m).

**Contact shadows** are per-light in `filament/include/filament/LightManager.h` `ShadowOptions`: `screenSpaceContactShadows false`, `stepCount 8`, `maxShadowDistance 0.3` (m); only the sun/directional light's values are used for all lights.
Other `ShadowOptions` defaults: `mapSize 1024`, `shadowCascades 1`, `cascadeSplitPositions {0.125,0.25,0.5}`, `constantBias 0.001`, `normalBias 1.0`, `shadowFar 0`, `shadowNearHint 1.0`, `shadowFarHint 100.0`,
`stable false`, `lispsm true`, `polygonOffsetConstant 0.5`, `polygonOffsetSlope 2.0`, `vsm.elvsm false`, `vsm.blurWidth 0`, `shadowBulbRadius -1` (PCSS; default per type), `penumbraScale 1`.

---

## 4. GTAO and ambient occlusion

### 4.1 Pipeline (`filament/src/PostProcessManager.cpp screenSpaceAmbientOcclusion`, lines 844-1134)

1. Depth buffer is mipmapped (`materials/ssao/mipmapDepth.mat`: one rotated-grid sample per 2x2 block: `texelFetch(depth, 2*icoord + ivec2(icoord.y&1, icoord.x&1), 0)`), `kLog2LodRate = 3` picks the mip per tap.
2. AO pass at `resolution` (default 0.5 of the colour size) writes `rgb = (aoVisibility, packedLinearDepth.xy)` (R8 + 16-bit depth packed in GB; `pack()` in `ssaoUtils.fs`); with bent normals the `RGB8` array layer 1 holds the packed **world-space** bent normal.
3. Optional separable bilateral Gaussian blur (X then Y) when `lowPassFilter != LOW`; kernel 11 (scale 2, sigma = stddev*0.5) for MEDIUM, 23 (scale 1, sigma = stddev) for HIGH/ULTRA.
   SAO-oriented stddev from `quality` (LOW 8, MEDIUM 8, HIGH 6, ULTRA 4). **GTAO ignores `quality`'s sample counts**; it uses `gtao.sampleSliceCount / sampleStepsPerSlice`.
4. At shading time the AO texture is sampled (optionally with a custom depth-aware bilinear upsample), see 4.3.

CPU parameters (verbatim):

```cpp
                const float projectionScale = std::min(
                        0.5f * cameraInfo.projection[0].x * desc.width,
                        0.5f * cameraInfo.projection[1].y * desc.height);
// ...
                        const auto sliceCount = static_cast<float>(options.gtao.sampleSliceCount);
                        mi->setParameter("stepsPerSlice",
                                static_cast<float>(options.gtao.sampleStepsPerSlice));
                        mi->setParameter("sliceCount",
                                float2{ sliceCount, 1.0f / sliceCount });
// ...
                mi->setParameter("invRadiusSquared", 1.0f / (options.radius * options.radius));
                mi->setParameter("projectionScale", projectionScale);
                mi->setParameter("projectionScaleRadius", projectionScale * options.radius);
```

### 4.2 GTAO kernel - `filament/src/materials/ssao/gtaoImpl.fs` (+ `gtao.mat` fragment)

Noise (gtaoImpl.fs:51-60): `dirNoise = (1/16)*(((x+y)&3)<<2 | (x&3))` and `offsetNoise = 0.25*((y-x)&3)` on the integer pixel position (Activision 4x4 interleave).
Sampling pattern: per pixel, `sliceCount` slices with angle `phi = (i + noiseDirection) / sliceCount * PI` (spatial 4x4 Activision interleave noise for direction `spatialDirectionNoise`
and for step offset `spatialOffsetsNoise`), `stepsPerSlice` steps per half-slice (both sides, `+-sampleOffset`), first step at `max((j + jitter)*stepRadius, 1 + j)` pixels
(always >= 1 px), `stepRadius = ssRadius/(steps+1)`, `ssRadius = -projectionScale*radius / origin.z`. Horizon update with distance falloff and the "thickness heuristic" lerp.
Visibility integrated analytically with the cosine-weighted arc (`integrateArcCosWeight`). XeGTAO-style, cites Jimenez et al. 2016.

```glsl
float integrateArcCosWeight(float h, float n) {
    float arc = -cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n);
    return 0.25 * arc;
}
// ...
float updateHorizon(float sampleHorizonCos, float currentHorizonCos, float fallOff) {
    return sampleHorizonCos > currentHorizonCos
        ? mix(sampleHorizonCos, currentHorizonCos, fallOff)
        : mix(currentHorizonCos, sampleHorizonCos, materialParams.thicknessHeuristic);
}
float calculateHorizonCos(highp vec3 sampleDelta, highp vec3 viewDir, float horizonCos) {
    highp float sqSampleDist = dot(sampleDelta, sampleDelta);
    float invSampleDist = rsqrt(sqSampleDist);
    float fallOff = saturate(sqSampleDist * materialParams.invRadiusSquared * 2.0);
    float shc = dot(sampleDelta, viewDir) * invSampleDist;
    return updateHorizon(shc, horizonCos, fallOff);
}
// ...
    highp float ssRadius = -(materialParams.projectionScaleRadius / origin.z);
    float noiseOffset = spatialOffsetsNoise(uv);
    float noiseDirection = spatialDirectionNoise(uv);
    float initialRayStep = fract(noiseOffset);
    float stepRadius = ssRadius / (materialParams.stepsPerSlice + 1.0);
    float visibility = 0.0;
    for (float i = 0.0; i < materialParams.sliceCount.x; i += 1.0) {
        float slice = (i + noiseDirection) * materialParams.sliceCount.y;
        float phi = slice * PI;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        vec2 omega = vec2(cosPhi, sinPhi);
        vec3 direction = vec3(cosPhi, sinPhi, 0.0);
        vec3 orthoDirection = normalize(direction - (dot(direction, viewDir)*viewDir));
        vec3 axis = cross(orthoDirection, viewDir);
        vec3 projNormal = normal - axis * dot(normal, axis);
        float signNorm = sign(dot(orthoDirection, projNormal));
        float projNormalLength = length(projNormal);
        float cosNorm = saturate(dot(projNormal, viewDir) / projNormalLength);
        float n = signNorm * acosFast(cosNorm);
// ...
        for (float j = 0.0; j < materialParams.stepsPerSlice; j += 1.0) {
            vec2 sampleOffset = max((j + initialRayStep)*stepRadius, 1.0 + j) * omega;
            float sampleOffsetLength = length(sampleOffset);
            float level = clamp(floor(log2(sampleOffsetLength)) - kLog2LodRate, 0.0, float(materialParams.maxLevel));
            vec2 uvSampleOffset = sampleOffset * materialParams.resolution.zw;
            vec2 sampleScreenPos0 = uv + uvSampleOffset;
            vec2 sampleScreenPos1 = uv - uvSampleOffset;
            highp vec3 samplePos0 = getViewSpacePosition(sampleScreenPos0, level);
            highp vec3 samplePos1 = getViewSpacePosition(sampleScreenPos1, level);
            highp vec3 sampleDelta0 = (samplePos0 - origin);
            highp vec3 sampleDelta1 = (samplePos1 - origin);
            if (materialConstants_useVisibilityBitmasks) { /* bitmask path: gtaoImpl.fs:188-191 */ } else {
                horizonCos0 = calculateHorizonCos(sampleDelta0, viewDir, horizonCos0);
                horizonCos1 = calculateHorizonCos(sampleDelta1, viewDir, horizonCos1);
            }
        }
// ...
        if (materialConstants_useVisibilityBitmasks) { /* gtaoImpl.fs:198-203 */ } else {
            float h0 = -acosFast(horizonCos1);
            float h1 = acosFast(horizonCos0);
            h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
            h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
            #if COMPUTE_BENT_NORMAL
                    float angle = 0.5 * (h0 + h1);
                    bentNormal += viewDir * cos(angle) - orthoDirection * sin(angle);
            #endif
            visibility += projNormalLength * (integrateArcCosWeight(h0, n) + integrateArcCosWeight(h1, n));
        }
    }
    obscurance = 1.0 - saturate(visibility * materialParams.sliceCount.y);
#if COMPUTE_BENT_NORMAL
    bentNormal = normalize(bentNormal);
#endif
}
```

Pass epilogue (`gtao.mat` `postProcess`): view-space position/normal reconstructed from depth (`computeViewSpaceNormal` = Wu "accurate normal reconstruction" in
`materials/utils/geometry.fs`), `occlusion` from the kernel (optionally `max()`ed with SSCT dominant-light shadowing), then:

```glsl
        float aoVisibility = pow(saturate(1.0 - occlusion), materialParams.power);
        aoVisibility = mix(1.0, aoVisibility, materialParams.intensity);
        postProcess.color.rgb = vec3(aoVisibility, pack(origin.z * materialParams.invFarPlane));
```
Bilateral blur (`ssao/bilateralBlur.mat`, kernel weights `exp(-i^2/(2 sigma^2))` from the CPU):

```glsl
    float bilateralWeight(in highp float depth, in highp float sampleDepth) {
        float diff = (sampleDepth - depth) * materialParams.farPlaneOverEdgeDistance;
        return max(0.0, 1.0 - diff * diff);
    }
```
`farPlaneOverEdgeDistance = -zf / bilateralThreshold`; weight = `max(0, 1 - ((d_s - d_c) * that)^2)` on the packed 16-bit linear depth (normalised by far plane). Sky pixels (g*b == 1) are skipped.

### 4.3 How AO is applied - `shaders/src/surface_ambient_occlusion.fs` + `surface_light_indirect.fs`

**AO is applied to indirect (IBL) light only**: `diffuseAO = min(material.ambientOcclusion, ssao)`; direct lights never see SSAO (they get a
micro-shadowing term from the material AO map only, `computeMicroShadowing`, `surface_lighting.fs:85`). SSAO is not applied to blended (transparent) materials.
Order in `evaluateIBL` (lines 779-815): specular AO -> `Fr *= singleBounceAO(specularAO)`; diffuse `* singleBounceAO(diffuseAO)`; then colour-aware multi-bounce for diffuse and specular.
`MULTI_BOUNCE_AMBIENT_OCCLUSION` default = 1 on desktop (`ShaderGenerator.cpp:569-571`); `SPECULAR_AMBIENT_OCCLUSION` default = SIMPLE (Lagarde) on desktop, NONE on mobile (`ShaderGenerator.cpp:564-567`).
With multi-bounce on, `singleBounceAO()` returns 1.0 (the multi-bounce polynomial replaces the plain multiply).

```glsl
float SpecularAO_Lagarde(float NoV, float visibility, float roughness) {
    return saturate(pow(NoV + visibility, exp2(-16.0 * roughness - 1.0)) - 1.0 + visibility);
}
// ...
float SpecularAO_Cones(vec3 bentNormal, float visibility, float roughness) {
    float cosAv = sqrt(1.0 - visibility);
    float cosAs = exp2(-3.321928 * sq(roughness));
    float cosB  = dot(bentNormal, shading_reflected);
    float ao = sphericalCapsIntersection(cosAv, cosAs, cosB) / (1.0 - cosAs);
    return mix(1.0, ao, smoothstep(0.01, 0.09, roughness));
}
// ...
vec3 gtaoMultiBounce(float visibility, const vec3 albedo) {
    vec3 a =  2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c =  2.7552 * albedo + 0.6903;
    return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}
// ...
float computeMicroShadowing(float NoL, float visibility) {
    float aperture = inversesqrt(1.0 - min(visibility, 0.9999));
    float microShadow = saturate(NoL * aperture);
    return microShadow * microShadow;
}
```
Bent normals: `aoBentNormals > 0` enables a second AO layer holding the world-space bent normal (GTAO derives it from the mean horizon angle per slice: `bentNormal += viewDir*cos(angle) - orthoDirection*sin(angle)`, 4.2).
It is used **only for specular AO cones** (`SpecularAO_Cones`, Jimenez 2016), min-combined with the Lagarde term; the comment in the source says the diffuse does not use the screen-space bent normal yet
(diffuse uses `material.bentNormal` if the material supplies one: `diffuseNormal = shading_bentNormal`). Bitmask mode (`useVisibilityBitmasks`) cannot output bent normals.
Optional upsampling (`aoSamplingQualityAndEdgeDistance > 0`, i.e. `upsampling >= HIGH`): 4-tap bilinear weights multiplied by `max(MEDIUMP_FLT_MIN, 1 - ((d - depth_i) * aoSamplingQualityAndEdgeDistance)^2)` bilateral weights, normalised (lines 37-75).

---

## 5. TAA - `filament/src/materials/antiAliasing/taa/taa.mat` (+ `PostProcessManager.cpp`)

### 5.1 Jitter sequence and camera (CPU) - `PostProcessManager.cpp`

```cpp
constexpr float halton(unsigned int i, unsigned int const b) noexcept {
    i += 409;
    float f = 1.0f;
    float r = 0.0f;
    while (i > 0u) {
        f /= float(b);
        r += f * float(i % b);
        i /= b;
    }
    return r;
}
// ...
    current.jitter = jitterPosition(previous.frameId);
    float2 jitter = current.jitter;
    float2 const jitterInClipSpace = jitter * (2.0f / float2{ svp.width, svp.height });
    inoutCameraInfo->projection[2].xy -= jitterInClipSpace;
```
`JitterSequence::operator()` returns `positions[i % SIZE] - 0.5f` (`PostProcessManager.h:512`), so jitter is in [-0.5,0.5] pixels. Halton(2,3) with the first 409 entries skipped, default 16 frames.
Jitter is applied to the **projection matrix** (`projection[2].xy -= jitter * 2/viewport`), the *unjittered* `projection * view` is stored as `current.projection` for reprojection.
`frameId` increments each frame; texture mip bias while TAA is on = `lodBias (-1) - log2(upscaling)` (`Renderer.cpp:320-333`), `derivativesScale = 1/upscaling`.
Reprojection matrix passed to the shader (`PostProcessManager::taa`): `mat4f(historyProjection * inverse(current.projection)) * normalizedToClip` (camera-motion only; there is **no per-object motion vector buffer**, moving objects rely on the history clip).
Material params: `alpha = feedback`, `jitter = -current.jitter`, `varianceGamma = varianceGamma * max(1, upscaling)`, `color`/`depth` sampled NEAREST, `history` LINEAR.
Post-TAA sharpen: `if (taaOptions.sharpness > 0) rcas(...)` (FSR1 RCAS). History written = output **before** sharpening.

### 5.2 Shader (verbatim, comments stripped, default-off paths elided)

Helpers:

```glsl
#define kMinBoxHalfWidth 0.002
vec3 tonemap(const vec3 c) {
    return c * rcp(1.0 + max3(c));
}
vec3 untonemap(const vec3 c) {
    const float epsilon = 1.0 / 65504.0;
    vec3 t = clamp(c, 0.0, 1.0 - epsilon);
    return t * rcp(max(epsilon, 1.0 - max3(t)));
}
vec3 tonemapIfHDR(const vec3 c) { return materialConstants_hdr ? tonemap(c) : c; }
vec3 lanczos2(vec3 v) {
    highp vec3 x2 = v * v;
    highp vec3 wA = x2 - 4.0;
    return (wA * wA) * ((x2 - 1.0) * (-0.354377 * x2 + 1.0)) * (-1.0 / 16.0);
}
// ...
vec4 clipToBox(const int quality,
        const vec3 boxmin,  const vec3 boxmax, const vec4 c, const vec4 h) {
    const float epsilon = 0.0001;
    if (quality == BOX_CLIPPING_ACCURATE) {
        if (any(lessThan(h.rgb, boxmin)) || any(greaterThan(h.rgb, boxmax))) {
            vec3 cClamped = clamp(c.rgb, boxmin, boxmax);
            vec4 r = vec4(cClamped, c.a) - h;
            vec3 rSign = step(0.0, r.rgb) * 2.0 - 1.0;
            vec3 ir = 1.0 / (r.rgb + rSign * epsilon);
            vec3 rmax = (boxmax - h.rgb) * ir;
            vec3 rmin = (boxmin - h.rgb) * ir;
            vec3 imin = min(rmax, rmin);
            return h + r * saturate(max3(imin));
        }
    } else if (quality == BOX_CLIPPING_CLAMP) {
        return vec4(clamp(h.rgb, boxmin, boxmax), h.a);
    }
    return h;
}
```
Catmull-Rom history filter (5 taps, bilinear-merged, with deringing clamp) - `sampleTextureCatmullRom`:

```glsl
    vec2 w0 = f2 - 0.5 * (f3 + f);
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w3 = 0.5 * (f3 - f2);
    vec2 w2 = 1.0 - w0 - w1 - w3;
    // 5 bilinear taps: (texPos12.x,texPos0.y) (texPos0.x,texPos12.y) (texPos12) (texPos3.x,texPos12.y) (texPos12.x,texPos3.y); texPos12 = texPos1 + w2/(w1+w2)
    // weights k0=w12.x*w0.y k1=w0.x*w12.y k2=w12.x*w12.y k3=w3.x*w12.y k4=w12.x*w3.y; result=sum(k*s)/sum(k); clamp(result, min(s0..s4), max(s0..s4))
```
Main body (reprojection, history fetch, input reconstruction, box, blend). The `motionAdaptiveHistory` and `depthDisocclusion` blocks (lines 448-500, default off) and the
`exactLanczosSupport` 4th-tap block (lines 600-630, default off) are elided here; see the file.

```glsl
    float historyAlphaStrength = 1.0;
    float disocclusion = 0.0;
    if (materialConstants_historyReprojection) {
        highp float depth = textureLod(materialParams_depth, uv, 0.0).r;
        historyUv = uvToRenderTargetUV(historyUv);
        highp vec4 q = materialParams.reprojection * vec4(historyUv, depth, 1.0);
        highp float invQW = 1.0 / q.w;
        historyUv = (q.xy * invQW) * 0.5 + 0.5;
        historyUv = uvToRenderTargetUV(historyUv);
        highp float reprojectionDeltaLen =
                length((historyUv - uv) * materialParams.colorResolution.xy);
    // ... motionAdaptiveHistory / depthDisocclusion (taa.mat:448-500) ...
    vec4 history;
    if (materialConstants_filterHistory) {
        history = sampleTextureCatmullRom(materialParams_history, historyUv, materialParams.historyResolution);
    } else {
        history = textureLod(materialParams_history, historyUv, 0.0);
    }
    history.rgb = tonemapIfHDR(history.rgb);
    highp vec2 reconstructedFragmentPos = uv * inputSize;
    highp vec2 closestInputSamplePos = floor(reconstructedFragmentPos) + 0.5;
    highp vec2 p = closestInputSamplePos * inputPixelSize;
    highp vec2 samplePos = closestInputSamplePos + materialParams.jitter;
    vec2 dcenter = samplePos - reconstructedFragmentPos;
    vec4 rawColor = textureLod(materialParams_color, p, 0.0);
    vec3 filtered = tonemapIfHDR(rawColor.rgb);
        // s[0..8] = 3x3 input taps around p (textureLodOffset), tonemapped; rgbBoxMin/Max = their min/max
            vec3 vx = dcenter.x + vec3(-1.0, 0.0, 1.0);
            vec3 vy = dcenter.y + vec3(-1.0, 0.0, 1.0);
            if (materialConstants_upscaling) {
                vx = clamp(vx * materialParams.scale, -2.0, 2.0);
                vy = clamp(vy * materialParams.scale, -2.0, 2.0);
            }
            vec3 wx3 = lanczos2(vx);
            vec3 wy3 = lanczos2(vy);
            float w0 = wx3.x, w1 = wx3.y, w2 = wx3.z;
            float w3 = wy3.x, w4 = wy3.y, w5 = wy3.z;
            highp float wx = w0 + w1 + w2;
            highp float wy = w3 + w4 + w5;
            filtered = (s[5] * w0 + s[1] * w1 + s[6] * w2) * w3
                     + (s[2] * w0 + s[0] * w1 + s[3] * w2) * w4
                     + (s[7] * w0 + s[4] * w1 + s[8] * w2) * w5;
            vec3 deringMin = rgbBoxMin;
            vec3 deringMax = rgbBoxMax;
            // ... exactLanczosSupport (taa.mat:600-630) ...
            float sum = wx * wy;
            bool hasReconstruction = abs(sum) > 0.1;
            filtered = hasReconstruction ? (filtered / sum) : s[0];
            filtered = clamp(filtered, deringMin, deringMax);
// ...
            vec3 boxTarget = filtered;
            vec3 box5min = s[0], box5max = s[0];
            for (int i = 1; i < 5; i++) {
                box5min = min(box5min, s[i]);
                box5max = max(box5max, s[i]);
            }
            vec3 box9min = box5min, box9max = box5max;
            for (int i = 5; i < 9; i++) {
                box9min = min(box9min, s[i]);
                box9max = max(box9max, s[i]);
            }
            vec3 boxmin = mix(box5min, box9min, 0.5);
            vec3 boxmax = mix(box5max, box9max, 0.5);
            // AABB_VARIANCE only:
                highp vec3 m0 = s[0];// conversion to highp
                highp vec3 m1 = m0 * m0;
                const int count = 9;
                for (int i = 1; i < count; i++) {
                    highp vec3 c = s[i];// conversion to highp
                    m0 += c;
                    m1 += c * c;
                }
                highp vec3 a0 = m0 * (1.0 / float(count));
                highp vec3 a1 = m1 * (1.0 / float(count));
                highp vec3 stdDev = sqrt(abs(a1 - a0 * a0));
                boxmin = max(boxmin, a0 - materialParams.varianceGamma * stdDev);
                boxmax = min(boxmax, a0 + materialParams.varianceGamma * stdDev);
            vec3 boxCenter = (boxmin + boxmax) * 0.5;
            boxmin = min(boxmin, boxCenter - kMinBoxHalfWidth);
            boxmax = max(boxmax, boxCenter + kMinBoxHalfWidth);
            vec4 target4 = vec4(boxTarget, rawColor.a);
            vec4 clippedHistory = clipToBox(materialConstants_boxClipping, boxmin, boxmax, target4, history);
            history = mix(history, clippedHistory, historyBoxStrength);
// ...
    float alpha = (materialParams.alpha * confidence) * historyAlphaStrength;
    vec4 result = mix(history, vec4(filtered, rawColor.a), alpha);
```
Summary / key behaviours:
- **Reprojection**: `historyUv = (reprojection * vec4(uv, depth, 1)).xy / w * 0.5 + 0.5` using the current-pixel (non-dilated) depth. The shader takes no velocity buffer, so only the colour-box clip (and the optional depth disocclusion test) rejects stale history on moving objects.
- **History filter**: Catmull-Rom (`filterHistory = true`) in tonemapped space (`c / (1 + max3(c))`, inverse `untonemap` clamps to `1 - 1/65504`).
- **Input**: 3x3 neighbourhood around the nearest input texel; Lanczos-2 (polynomial, `lanczos2`) "unjitter" reconstruction of the pixel centre, de-ringed in RGB.
- **Neighbourhood clipping**: box = lerp(5-tap cross box, 9-tap box, 0.5); default `AABB` + `ACCURATE` ray clipping (history -> `filtered`, ray/box intersection, not a plain clamp);
  `AABB_VARIANCE` intersects with mean +- `varianceGamma*sigma` over 9 taps (Salvi); box floored to half-width `0.002`. YCoCg is optional (`useYCoCg` false by default; RGB tonemapped is the default space).
- **Feedback**: `alpha = feedback (0.12) * confidence * historyAlphaStrength`; `result = mix(history, filtered, alpha)`. `preventFlickering` multiplies alpha by `(1 - |lumaC - lumaH| / (0.001 + max))^2`.
- **Disocclusion**: only when `depthDisocclusion` (needs a depth-history texture): compares reprojected depth with the previous-frame depth, evidence beyond the 3x3 jitter-explainable depth spread -> `disocclusion in [0,1]`, raises box strength / alpha to 1 and lerps history toward the current colour.
  Default off; with it off the only disocclusion handling is the colour box clip.
- **Motion adaptive history** (off by default): relaxes box strength/alpha toward `minBoxStrength` (0.1) / `minAlphaStrength` (0.3) when camera motion < `motionDeltaThreshold` texels, held at full strength for `motionHoldFrames` after camera motion (computed on the CPU by comparing unjittered projection matrices).
- Upscaling (`upscaling > 1`) changes the Lanczos kernel scale and uses `confidence = sum` of weights as alpha scale.
- Design doc: `filament/src/materials/antiAliasing/taa/taa.md`.

---

## 6. Bloom

### 6.1 CPU chain - `PostProcessManager.cpp bloom()` (lines 2264-2486)

Order: (a) pre-downscale the HDR colour with `downscalePass` (`bloomDownsample2x.mat`, 4 bilinear taps = 3x3 Gaussian; **threshold + fireflies reduction applied once here**; fireflies reduction skipped if TAA is on)
until the buffer is within 2x of `resolution` (default 384 px tall; aspect-preserving); LOW quality also rounds the size down to a multiple of 16 so the cheap 9-tap kernel can be used for >= 4 levels;
HIGH/ULTRA instead *raise* the bloom buffer size (`levels++`, max 2048). (b) `Bloom Downsample`: mip chain of `levels` (default 6, clamped to the texture's max levels): each level samples the previous with the **9-tap (6x6 kernel)** filter if the source size is even in both axes, else the **13-tap Jimenez** filter.
(c) optional lens flare pass. (d) `Bloom Upsample`: from the smallest level up, each level `i` is rendered **into level `i-1` with additive blending (`ONE, ONE`)**, so mip 0 ends up as the sum of all upsampled levels.

Downscale (threshold) shader - `materials/bloom/bloomDownsample2x.mat`:

```glsl
    void threshold(inout vec3 c) {
        c = max(vec3(0.0), c - 1.0);
        highp float f = max3(c);
        c *= 1.0 / (1.0 + f * materialParams.invHighlight);
    }
    void postProcess(inout PostProcessInputs postProcess) {
        float lod = materialParams.level;
        highp vec2 size = vec2(textureSize(materialParams_source, int(lod)));
        highp vec2 texelSize = vec2(1.0) / size;
        vec2 offset = vec2(0.5);
        highp vec2 uv = (variable_vertex.xy * size) + offset;
        highp vec2 base = (floor(uv) - offset) * texelSize;
        highp vec2 st = fract(uv);
        vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
        vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);
        highp vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * texelSize.x;
        highp vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * texelSize.y;
        vec3 c0 = textureLod(materialParams_source, base + vec2(u.x, v.x), lod).rgb;
        vec3 c1 = textureLod(materialParams_source, base + vec2(u.y, v.x), lod).rgb;
        vec3 c2 = textureLod(materialParams_source, base + vec2(u.x, v.y), lod).rgb;
        vec3 c3 = textureLod(materialParams_source, base + vec2(u.y, v.y), lod).rgb;
        float w0 = uw.x * vw.x * (1.0 / 16.0);
        float w1 = uw.y * vw.x * (1.0 / 16.0);
        float w2 = uw.x * vw.y * (1.0 / 16.0);
        float w3 = uw.y * vw.y * (1.0 / 16.0);
        if (materialParams.fireflies > 0.0) { /* w_i /= (1 + max3(c_i)); then renormalise so w0+w1+w2+w3 = 1 */ }
        vec3 c = c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3;
        if (materialParams.threshold > 0.0) {
            threshold(c);
        }
        postProcess.color.rgb = c;
```
CPU: `threshold` = options.threshold (1.0 if true, 0 else; `c = max(0, c-1)` i.e. subtract 1.0 then soft-crush), `invHighlight = isinf(highlight) ? 0 : 1/highlight` (default highlight 1000 => max ~ 1000 after crush), `fireflies = threshold && !TAA`.
Note: threshold is a *subtractive* threshold at 1.0 (HDR) with a Reinhard-like cap, not a hard knee; there is no soft-knee parameter.

13-tap downsample - `materials/bloom/bloomDownsample.mat` (Jimenez, "Next Generation Post-Processing in COD:AW", 5 overlapping boxes, weights 0.5 / 0.125):

```glsl
    vec3 box4x4(vec3 s0, vec3 s1, vec3 s2, vec3 s3) {
        return (s0 + s1 + s2 + s3) * 0.25;
    }
// taps (source texel offsets): c(0,0); lt rt rb lb = (+-1,+-1); lt2 rt2 rb2 lb2 = (+-2,+-2); l r t b = (+-2,0),(0,+-2)
        vec3 c0, c1;
        c0  = box4x4(lt, rt, rb, lb);
        c1  = box4x4(c, l, t, lt2);
        c1 += box4x4(c, r, t, rt2);
        c1 += box4x4(c, r, b, rb2);
        c1 += box4x4(c, l, b, lb2);
        postProcess.color.rgb = c0 * 0.5 + c1 * 0.125;
```
9-tap (6x6) downsample - `materials/bloom/bloomDownsample9.mat` (shadertoy cslczj): 9 bilinear taps at offsets `(0, +-o)` / `(+-o, +-o)` source texels with `o = 1.5 + 0.261629`;
weights `wa = 7.46602/32`, `wb = 1 - 2*wa`: centre `wb*wb`, the 4 edge taps `wa*wb`, the 4 corner taps `wa*wa`.

Upsample - `materials/bloom/bloomUpsample.mat`: QUALITY < HIGH = 4 bilinear taps at half-texel diagonal offsets, average (`* 0.25`); QUALITY >= HIGH (desktop default) = 3x3 tent (Jimenez), verbatim:

```glsl
        // d = vec4(resolution.zw, -resolution.zw) * 1.0   (resolution = destination level size; source is the next smaller level, bilinear)
        c0  = textureLod(materialParams_source, uv + d.zw, 0.0).rgb;
        c0 += textureLod(materialParams_source, uv + d.xw, 0.0).rgb;
        c0 += textureLod(materialParams_source, uv + d.xy, 0.0).rgb;
        c0 += textureLod(materialParams_source, uv + d.zy, 0.0).rgb;
        c0 += 4.0 * textureLod(materialParams_source, uv, 0.0).rgb;
        c1  = textureLod(materialParams_source, uv + vec2(d.z,  0.0), 0.0).rgb;
        c1 += textureLod(materialParams_source, uv + vec2(0.0,  d.w), 0.0).rgb;
        c1 += textureLod(materialParams_source, uv + vec2(d.x,  0.0), 0.0).rgb;
        c1 += textureLod(materialParams_source, uv + vec2( 0.0, d.y), 0.0).rgb;
        postProcess.color.rgb = (c0 + 2.0 * c1) * (1.0 / 16.0);
```

### 6.2 Composition - `materials/colorGrading/colorGrading.mat` (+ `PostProcessManager.cpp:2827-2838`)

Bloom is composited in **linear HDR before tone mapping / colour grading / vignette / dithering**:

```glsl
vec3 bloom(highp vec2 uv, const vec3 color) {
    vec3 result = vec3(0.0);
    if (materialParams.bloom.x > 0.0) {
        vec3 bloom = textureLod(materialParams_bloomBuffer, uv, 0.0).rgb;
        result += bloom * materialParams.bloom.x;
    }
    if (materialParams.bloom.w > 0.0) {
        float starburstMask = starburst(uv);
        vec3 flare = textureLod(materialParams_flareBuffer, uv, 0.0).rgb;
        result += flare * (materialParams.bloom.w * starburstMask);
    }
    if (materialParams.bloom.z > 0.0) {
        float dirtIntensity = materialParams.bloom.z;
        vec3 dirt = textureLod(materialParams_dirtBuffer, uv, 0.0).rgb;
        result *= dirt * dirtIntensity;
    }
    result += color * materialParams.bloom.y;
    return result;
}
```
```cpp
                float4 bloomParameters{
                    bloomStrength / float(bloomOptions.levels),
                    1.0f,
                    (bloomOptions.enabled && bloomOptions.dirt) ? bloomOptions.dirtStrength : 0.0f,
                    bloomOptions.lensFlare ? bloomStrength : 0.0f
                };
                if (bloomOptions.blendMode == BloomOptions::BlendMode::INTERPOLATE) {
                    bloomParameters.y = 1.0f - bloomParameters.x;
                }
```
So `bloomParameters = (strength/levels, 1, dirtStrength, lensFlare ? strength : 0)`; ADD: `out = scene + bloom * strength/levels`; INTERPOLATE: `y = 1 - strength/levels`, `out = bloom*s + scene*(1-s)`.
The division by `levels` compensates for the additive upsample chain summing `levels` mips. Dirt multiplies only the bloom term (`result *= dirt * dirtStrength` before adding the scene).

---

## 7. Tone mappers and shadow filtering

### 7.1 Tone mappers - `filament/include/filament/ToneMapper.h`, `filament/src/ToneMapper.cpp`

Available: `LinearToneMapper` (clamp), `ACESToneMapper`, `ACESLegacyToneMapper`, `FilmicToneMapper`, `PBRNeutralToneMapper`, `GT7ToneMapper` (Gran Turismo 7, 250 nit paper white),
`AgxToneMapper` (looks NONE/PUNCHY/GOLDEN), `GenericToneMapper(contrast 1.55, midGrayIn 0.18, midGrayOut 0.215, hdrMax 10)`, `DisplayRangeToneMapper` (debug).
**Default** = `ACESLegacyToneMapper` (`ColorGrading.h:220` "The default tone mapping operator is ACESLegacyToneMapper"; `details/ColorGrading.cpp:787` fallback; Engine's `mDefaultColorGrading` is built from a default `ColorGrading::Builder`, so the default View/tonemap path is ACES-legacy).
It is baked into the colour-grading LUT (`ColorGrading.cpp:89` default `dimension = 32`, i.e. 32^3; quality LOW/MEDIUM/HIGH/ULTRA = 16/32/32/64, FLOAT format from HIGH). Linear, Filmic and Generic report `isOneDimensional() = true` (1D LUT path).

ACES-legacy = the full ACES RRT-fit below with an input exposure boost `brightness = 1/0.6` (ACESToneMapper uses 1.0):

```cpp
    constexpr float a = 2.785085f;
    constexpr float b = 0.107772f;
    constexpr float c = 2.936045f;
    constexpr float d = 0.887122f;
    constexpr float e = 0.806889f;
    float3 const rgbPost = (ap1 * (a * ap1 + b)) / (ap1 * (c * ap1 + d) + e);
    float3 linearCV = darkSurround_to_dimSurround(rgbPost);
    linearCV = mix(float3(dot(linearCV, LUMINANCE_AP1)), linearCV, ODT_SAT_FACTOR);
// ...
float3 ACESLegacyToneMapper::operator()(float3 const c) const noexcept {
    return aces::ACES(c, 1.0f / 0.6f);
}
```
Filmic (Narkowicz 2015) and the generic curve (verbatim):

```cpp
float3 FilmicToneMapper::operator()(float3 const x) const noexcept {
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}
// ...
        float const a = pow(midGrayIn, contrast);
        float const b = pow(hdrMax, contrast);
        float const c = a - midGrayOut * b;
        inputScale = (a * b * (midGrayOut - 1.0f)) / c;
        outputScale = midGrayOut * (a - b) / c;
    }
// ...
float3 GenericToneMapper::operator()(float3 x) const noexcept {
    x = pow(x, mOptions->contrast);
    return mOptions->outputScale * x / (x + mOptions->inputScale);
}
```
(`ACES()` takes Rec.2020 linear input and returns Rec.2020: `Rec2020_to_AP0` ... `AP1_to_Rec2020` from `ColorSpaceUtils.h`. The grading code converts sRGB/Rec.709 -> Rec.2020 around the tone mapper (`ColorGrading.cpp:710-724` returns `sRGB_to_Rec2020` / `LUMINANCE_Rec2020`); the exact matrix values are in `ColorSpaceUtils.h`, **not read**. If you port ACES, either convert your Rec.709 input to Rec.2020 first, or substitute the Rec.709<->AP0/AP1 matrices.)
`FilmicToneMapper` = Narkowicz 2015 (the cheap ACES fit). `PBRNeutral` = Khronos PBR Neutral (startCompression = 0.8-0.04, desaturation 0.15; code at `ToneMapper.cpp:446-464`). GT7 and AgX not reproduced here (long tables; see `ToneMapper.cpp`).

### 7.2 Shadow filtering - `shaders/src/surface_shadowing.fs`

- **Default `ShadowType::PCF`** (`View.h:645`). Per-material `ShadowSamplingQuality` (`Material.h:134-137`): `HARD` = 2x2 hardware PCF (1 tap with a comparison sampler),
  `LOW` (**default**, `Material.cpp:93`; `ShadowSample_PCF_Low`, `surface_shadowing.fs:35-86`) = hardware PCF + 3x3 Gaussian = 4 bilinear-PCF taps with weights `(3-2s, 1+2s)` per axis /16, same trick as `bloomDownsample2x` (Castano 2013). Compare reversed-Z, depth array `sampler2DArrayShadow`; bias via `polygonOffset*`, `constantBias`, `normalBias` (light-space normal offset).
- **VSM** (`ShadowType::VSM`): EVSM, moments in RG32F/RG16F (positive exponential only) or RGBA32F/RGBA16F with ELVSM (`ShadowMapManager.cpp:1407-1409`), Chebyshev bound (`chebyshevUpperBound`/`evaluateEVSM`, `surface_shadowing.fs:144-185`: depth warped by `exp(c*(2z-1))`, variance floor `0.002*w^2`, light-bleed reduction `(p - lbr)/(1 - lbr)`, ELVSM takes `min` with the negative-exponent bound); optional separable Gaussian blur (`vsm.blurWidth`) and mipmaps/anisotropy.
- **DPCF**: deprecated -> PCSS. **PCSS** (`ShadowType::PCSS`): actually built on EVSM maps ("EVSSM": blocker search from a mip of the moments, penumbra estimation, then a 5-tap quincunx filtered fetch of a chosen mip LOD), controlled by `SoftShadowOptions`/`shadowBulbRadius`.
- **Contact shadows**: screen-space ray march in the depth buffer along the light direction (`screenSpaceContactShadow`, 8 steps default, interleaved-gradient-noise dithered, fades near screen edges).
- Cascades/LiSPSM/`stable` are CPU-side (ShadowMap*.cpp, not read).


### 7.3 Not found / not verified

- No normal-map-variance specular AA (only the geometric kernel in 2.3).
- No per-object motion vectors for TAA (camera reprojection only).
- Cascade split computation, shadow bias derivation and the full `ShadowSample_EVSSM` (blocker search + penumbra, lines 210-388 of `surface_shadowing.fs`) are summarised, not reproduced here.
- `GT7ToneMapper`, `AgxToneMapper` math not reproduced.
- (S8, 2026-10-23) The ACES matrices of 7.1 were then read from `filament/src/details/ColorSpaceUtils.h`
  and the full `aces::ACES()` from `filament/src/ToneMapper.cpp` at the same commit; they are in
  `shaders/hq_finish.frag.glsl` (column-major, in the header's order).
- SAO (`saoImpl.fs`, the default `aoType`) is not detailed: GTAO is what was asked for. Note `AmbientOcclusionOptions::aoType` defaults to **SAO**, not GTAO.

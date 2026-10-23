#version 450 core
// Turbo 0.71 (S2): Render 3D HQ shading pass for triangles - Filament's
// standard model (hqShade, hq_lighting.glsl; Apache License 2.0, Copyright
// Google LLC). Depth is already laid down by the prepass, so each pixel is
// shaded once. Material maps, the Mapping transform and the bump / normal map
// follow Render 3D's conventions so a Material looks the same kind of thing in
// both.
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec3 vLocalPos;
in vec3 vInstColor; // S7: Instance PointZ colour (1 otherwise), multiplies as in Render 3D

uniform int uShading; // Material: lit / normals / uv / flat

// Material maps (bit i of uMaps = MaterialMap i is bound), fixed units 4-11.
uniform int uMaps;
uniform sampler2D uAlbedoMap;
uniform sampler2D uRoughnessMap;
uniform sampler2D uMetallicMap;
uniform sampler2D uNormalMap;
uniform sampler2D uAoMap;
uniform sampler2D uEmissionMap;
uniform sampler2D uClearcoatMap;
uniform sampler2D uSheenMap;
uniform int uNormalBump;
uniform float uNormalStrength;
uniform int uMapSpace;
uniform vec3 uMapTranslate;
uniform vec3 uMapRotate;
uniform vec3 uMapScale;
uniform float uTriplanarBlend;
uniform vec3 uObjLo;
uniform vec3 uObjHi;
// S8: mip bias of the material maps - Filament's TAA lodBias (-1) and
// -log2 of the render-scale ratio, so the upscaled image keeps their detail.
uniform float uLodBias;

layout(location = 0) out vec4 outColor;

//@include hq_common.glsl
//@include hq_brdf.glsl
//@include hq_shadows.glsl
//@include hq_lighting.glsl

const int kMapAlbedo = 0, kMapRoughness = 1, kMapMetallic = 2, kMapNormal = 3, kMapAo = 4,
          kMapEmission = 5, kMapClearcoat = 6, kMapSheen = 7;

bool hasMap(int map)
{
   return (uMaps & (1 << map)) != 0;
}

// --- Render 3D's material mapping (Geometry3DNodes.cpp) ----------------------
vec3 mapRotate(vec3 p, vec3 r)
{
   float cx = cos(r.x), sx = sin(r.x);
   float cy = cos(r.y), sy = sin(r.y);
   float cz = cos(r.z), sz = sin(r.z);
   p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
   p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
   p = vec3(cz * p.x - sz * p.y, sz * p.x + cz * p.y, p.z);
   return p;
}

vec3 mapBase()
{
   vec3 base;
   if (uMapSpace == 1)
      base = (vLocalPos - uObjLo) / max(uObjHi - uObjLo, vec3(1e-5));
   else if (uMapSpace == 2)
      base = vLocalPos;
   else
      base = vec3(vUv, 0.0);
   return mapRotate(base * uMapScale, uMapRotate) + uMapTranslate;
}

vec2 computeMapUv(vec3 n)
{
   vec3 base = mapBase();
   if (uMapSpace == 0)
      return base.xy;
   vec3 an = abs(n);
   if (an.x >= an.y && an.x >= an.z) return base.yz;
   if (an.y >= an.x && an.y >= an.z) return base.xz;
   return base.xy;
}

vec4 sampleMap(sampler2D tex, vec3 n, vec2 uv)
{
   if (uMapSpace == 0 || uTriplanarBlend <= 0.001)
      return texture(tex, uv, uLodBias);
   vec3 base = mapBase();
   vec3 an = abs(n);
   float sharpness = 1.0 + (1.0 - uTriplanarBlend) * 7.0;
   vec3 w = pow(max(an, vec3(1e-4)), vec3(sharpness));
   w /= (w.x + w.y + w.z);
   return texture(tex, base.yz, uLodBias) * w.x + texture(tex, base.xz, uLodBias) * w.y +
          texture(tex, base.xy, uLodBias) * w.z;
}

// Cotangent frame from screen derivatives (no tangents in the mesh).
mat3 cotangentFrame(vec3 normal, vec3 pos, vec2 uv)
{
   vec3 dp1 = dFdx(pos), dp2 = dFdy(pos);
   vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
   vec3 dp2perp = cross(dp2, normal);
   vec3 dp1perp = cross(normal, dp1);
   vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
   vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
   if (max(dot(t, t), dot(b, b)) > 1e-12)
      return mat3(normalize(t), normalize(b), normal);
   vec3 t2 = abs(normal.z) < 0.999 ? normalize(cross(normal, vec3(0.0, 0.0, 1.0)))
                                   : normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
   return mat3(t2, normalize(cross(normal, t2)), normal);
}

vec3 applyNormalMap(vec3 normal, vec2 uv)
{
   mat3 tbn = cotangentFrame(normal, vWorldPos, uv);
   vec3 sampled;
   if (uNormalBump == 1)
   {
      // Grayscale height: slope from central differences, +Z up.
      vec2 px = 1.0 / vec2(textureSize(uNormalMap, 0));
      const vec3 lum = vec3(0.2126, 0.7152, 0.0722);
      float hl = dot(texture(uNormalMap, uv - vec2(px.x, 0.0), uLodBias).rgb, lum);
      float hr = dot(texture(uNormalMap, uv + vec2(px.x, 0.0), uLodBias).rgb, lum);
      float hd = dot(texture(uNormalMap, uv - vec2(0.0, px.y), uLodBias).rgb, lum);
      float hu = dot(texture(uNormalMap, uv + vec2(0.0, px.y), uLodBias).rgb, lum);
      sampled = normalize(vec3((hl - hr) * 2.0, (hd - hu) * 2.0, 1.0));
   }
   else
      sampled = texture(uNormalMap, uv, uLodBias).rgb * 2.0 - 1.0;
   sampled.xy *= uNormalStrength;
   return normalize(tbn * sampled);
}

void main()
{
   vec3 geoN = normalize(vNormal);
   if (!gl_FrontFacing)
      geoN = -geoN;
   if (uShading == 1) { outColor = vec4(geoN * 0.5 + 0.5, 1.0); return; }
   if (uShading == 2) { outColor = vec4(vUv, 0.0, 1.0); return; }

   vec2 mapUv = computeMapUv(geoN);
   HqSurface s;
   s.pos = vWorldPos;
   s.geoN = geoN;
   s.base = toLinear(uBaseColor) * vInstColor;
   if (hasMap(kMapAlbedo)) s.base *= toLinear(sampleMap(uAlbedoMap, geoN, mapUv).rgb);
   if (uShading == 3) { outColor = vec4(s.base * uExposure, 1.0); return; }

   s.perceptualRoughness = uRoughness;
   if (hasMap(kMapRoughness)) s.perceptualRoughness *= sampleMap(uRoughnessMap, geoN, mapUv).r;
   s.metallic = uMetallic;
   if (hasMap(kMapMetallic)) s.metallic *= sampleMap(uMetallicMap, geoN, mapUv).r;
   s.normalMapped = hasMap(kMapNormal);
   s.n = s.normalMapped ? applyNormalMap(geoN, mapUv) : geoN;
   s.hasAo = hasMap(kMapAo);
   s.ao = s.hasAo ? sampleMap(uAoMap, geoN, mapUv).r : 1.0;
   s.clearCoat = uClearcoat;
   if (hasMap(kMapClearcoat)) s.clearCoat *= sampleMap(uClearcoatMap, geoN, mapUv).r;
   s.sheen = uSheen;
   if (hasMap(kMapSheen)) s.sheen *= sampleMap(uSheenMap, geoN, mapUv).r;
   s.emissive = toLinear(uEmissionColor) * uEmission;
   if (hasMap(kMapEmission)) s.emissive *= toLinear(sampleMap(uEmissionMap, geoN, mapUv).rgb);
   s.kernelRoughness = computeKernelRoughness(geoN, 0.15, 0.2);

   outColor = vec4(hqShade(s) * uExposure, 1.0);
}

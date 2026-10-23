#version 450 core
// Turbo 0.71 (S7): Render 3D HQ - deferred shading of the sphere impostors
// and of Instance PointZ copies without material maps (same G-buffer).
// The prepass already resolved every sphere pixel exactly (depth, view normal
// and roughness, and - S7 - the point's colour with its slot id), so instead
// of rasterising a million quads again and shading their overlap, one
// full-screen pass per sphere slot shades each of its pixels once with the
// same Filament model (hqShade). Pixels of other slots and of triangles are
// left alone (their slot id differs).
in vec2 vNdc;

uniform mat4 uView;
uniform mat4 uInvViewProj; // the jittered camera the depth was drawn with
uniform float uSlotId;
uniform sampler2D uSphereAlbedo; // RGBA8 - rgb: the point's colour, a: slot id (1-based) / 255
uniform sampler2D uSceneNormal;  // xyz: view-space normal, a: roughness

layout(location = 0) out vec4 outColor;

//@include hq_common.glsl
//@include hq_brdf.glsl
//@include hq_shadows.glsl
//@include hq_lighting.glsl

void main()
{
   ivec2 p = ivec2(gl_FragCoord.xy);
   vec4 albedo = texelFetch(uSphereAlbedo, p, 0);
   // World normal (the view matrix is a rotation + translation) and the
   // specular AA kernel, in uniform control flow before any discard.
   vec3 n = normalize(transpose(mat3(uView)) * texelFetch(uSceneNormal, p, 0).xyz);
   float kernelRoughness = computeKernelRoughness(n, 0.15, 0.2);
   if (abs(albedo.a * 255.0 - uSlotId) > 0.5)
      discard;
   float depth = texelFetch(uSceneDepth, p, 0).r;
   vec4 world = uInvViewProj * vec4(vNdc, depth * 2.0 - 1.0, 1.0);

   HqSurface s;
   s.pos = world.xyz / world.w;
   s.geoN = n;
   s.n = n;
   s.base = toLinear(uBaseColor) * albedo.rgb; // Color multiplies, as in Render 3D
   s.perceptualRoughness = uRoughness;
   s.metallic = uMetallic;
   s.ao = 1.0;
   s.hasAo = false;
   s.clearCoat = uClearcoat;
   s.sheen = uSheen;
   s.emissive = toLinear(uEmissionColor) * uEmission;
   s.normalMapped = false;
   s.kernelRoughness = kernelRoughness;
   outColor = vec4(hqShade(s) * uExposure, 1.0);
}

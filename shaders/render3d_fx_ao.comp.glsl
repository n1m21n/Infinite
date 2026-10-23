#version 430
// Turbo 0.70: Render 3D FX - ambient occlusion from the render's depth (a
// screen-space hemisphere estimate in view space, Scalable-AO style): how much
// of the surface around each pixel is closed off by nearby geometry. Writes
// the light that gets through (1 = open) at the output size; the composite
// pass smooths it along the depth.
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uDepth; // hardware depth 0..1, the render's internal size
layout(r16f, binding = 0) uniform writeonly image2D uAo;

uniform ivec2 uSize;     // output
uniform mat4 uInvProj;
uniform float uProjScale; // proj[1][1] * height / 2: world size at view distance 1 -> pixels
uniform int uOrtho;
uniform float uRadius;   // world units
uniform float uAmount;

vec3 ViewPos(vec2 uv)
{
   float d = texture(uDepth, uv).r;
   vec4 v = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
   return v.xyz / v.w;
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (p.x >= uSize.x || p.y >= uSize.y)
      return;
   vec2 texel = 1.0 / vec2(uSize);
   vec2 uv = (vec2(p) + 0.5) * texel;
   if (texture(uDepth, uv).r >= 0.99999)
   {
      imageStore(uAo, p, vec4(1.0));
      return;
   }
   vec3 P = ViewPos(uv);
   // Normal from the neighbours on the side that changes least (edges stay clean).
   vec3 px0 = ViewPos(uv - vec2(texel.x, 0.0)), px1 = ViewPos(uv + vec2(texel.x, 0.0));
   vec3 py0 = ViewPos(uv - vec2(0.0, texel.y)), py1 = ViewPos(uv + vec2(0.0, texel.y));
   vec3 dx = abs(px1.z - P.z) < abs(P.z - px0.z) ? px1 - P : P - px0;
   vec3 dy = abs(py1.z - P.z) < abs(P.z - py0.z) ? py1 - P : P - py0;
   vec3 n = normalize(cross(dx, dy));
   if (dot(n, -P) < 0.0)
      n = -n;

   float pixels = uRadius * uProjScale / (uOrtho == 1 ? 1.0 : max(-P.z, 1e-3));
   pixels = clamp(pixels, 1.0, 96.0);
   // Per-pixel rotation (interleaved gradient noise) - the composite blurs it out.
   float spin = fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715)))) * 6.2831853;
   const int kSamples = 16;
   float occlusion = 0.0;
   for (int i = 0; i < kSamples; i++)
   {
      float t = (float(i) + 0.5) / float(kSamples);
      float a = float(i) * 2.39996323 + spin;
      vec2 suv = uv + vec2(cos(a), sin(a)) * (t * pixels) * texel;
      if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0))))
         continue;
      vec3 v = ViewPos(suv) - P;
      float dist = length(v);
      if (dist < 1e-5)
         continue;
      float cosine = dot(v, n) / dist;
      float falloff = 1.0 - smoothstep(0.0, uRadius, dist);
      occlusion += max(cosine - 0.05, 0.0) * falloff;
   }
   float open = clamp(1.0 - 2.0 * uAmount * occlusion / float(kSamples), 0.0, 1.0);
   imageStore(uAo, p, vec4(open));
}

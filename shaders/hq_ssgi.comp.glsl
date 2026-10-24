#version 450 core
// Turbo 0.71.1: Render 3D HQ - screen-space global illumination (one diffuse
// bounce per frame, more over frames). Filament has no GI: an emissive object
// lit only itself and the bloom. Here every pixel of the GI target (half the
// frame) sends a few cosine-distributed rays over the hemisphere of its normal
// and marches them through this frame's depth (the prepass copy); where a ray
// passes behind the depth buffer it hit that surface, and the light leaving
// it is read from LAST frame's HDR colour, reprojected with last frame's
// camera - lit surfaces and emissive ones alike, including their own GI, so
// bounces build up over frames. Misses add nothing (the HDRI / sky light is
// the IBL's). Output (mean radiance = irradiance / pi, view z) for the
// bilateral blur (hq_ssgi_blur.comp) and the shading passes (hq_lighting.glsl,
// ssgiIrradiance). The noise is new every frame; the GI's own history
// (hq_ssgi_temporal.comp) averages it.
layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba16f, binding = 0) uniform writeonly image2D uDst;
uniform sampler2D uDepth;     // this frame's depth (copy), full size
uniform sampler2D uNormalMap; // xyz: view-space normal, full size
uniform sampler2D uPrevColor; // last frame's HDR colour (exposed), full size
uniform mat4 uViewProj;       // this frame's camera (jittered, as the depth)
uniform mat4 uInvViewProj;
uniform mat4 uPrevViewProj;   // last frame's camera
uniform mat4 uViewMatrix;
uniform ivec2 uSize;          // GI target
uniform ivec2 uFullSize;      // frame
uniform float uRadius;        // world units a ray travels
uniform float uThickness;     // how far behind a surface still counts as a hit
uniform float uInvExposure;   // the colour is exposed; the GI is not
uniform int uFrame;

// 8 directions: with 4 a small bright emitter left a blotchy, cloud-like
// pattern the history and the blur could not average away.
const int kDirections = 8;
const int kSteps = 10;
const float PI2 = 6.28318530718;

// White noise per pixel and frame from an integer hash (lowbias32). Not the
// interleaved gradient noise of the other passes: sampled on every second
// pixel (the half-size GI), its diagonal structure showed as diagonal lines
// in the light; the GI's own history averages white noise out cleanly.
uint hash(uint x)
{
   x ^= x >> 16;
   x *= 0x7feb352du;
   x ^= x >> 15;
   x *= 0x846ca68bu;
   x ^= x >> 16;
   return x;
}

float random01(ivec2 pixel, int frame, uint salt)
{
   uint h = hash(uint(pixel.x) ^ hash(uint(pixel.y) ^ hash(uint(frame) * 2u + salt)));
   return float(h >> 8) * (1.0 / 16777216.0);
}

vec3 worldAt(vec2 uv, float depth)
{
   vec4 w = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
   return w.xyz / w.w;
}

void main()
{
   ivec2 p = ivec2(gl_GlobalInvocationID.xy);
   if (any(greaterThanEqual(p, uSize)))
      return;
   ivec2 fp = min(p * uFullSize / uSize, uFullSize - 1);
   float depth = texelFetch(uDepth, fp, 0).r;
   if (depth >= 1.0)
   {
      imageStore(uDst, p, vec4(0.0, 0.0, 0.0, -65000.0)); // nothing drawn: the blur skips it
      return;
   }
   vec2 fullTexel = 1.0 / vec2(uFullSize);
   vec2 uv = (vec2(fp) + 0.5) * fullTexel;
   vec3 P = worldAt(uv, depth);
   vec3 N = normalize(transpose(mat3(uViewMatrix)) * texelFetch(uNormalMap, fp, 0).xyz);
   vec3 T = normalize(abs(N.y) < 0.99 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
   vec3 B = cross(N, T);
   float viewZ = (uViewMatrix * vec4(P, 1.0)).z;
   float noise = random01(p, uFrame, 0u);
   float jitter = random01(p, uFrame, 1u);

   vec3 sum = vec3(0.0);
   for (int i = 0; i < kDirections; i++)
   {
      // Cosine-weighted directions, stratified in angle, rotated by the noise.
      float u1 = fract(jitter + (float(i) + 0.5) / float(kDirections) * 0.618034);
      float phi = PI2 * (float(i) + noise) / float(kDirections);
      float sinT = sqrt(u1), cosT = sqrt(1.0 - u1);
      vec3 dir = (T * cos(phi) + B * sin(phi)) * sinT + N * cosT;
      vec3 origin = P + N * (0.02 * uRadius);
      for (int s = 0; s < kSteps; s++)
      {
         // Denser near the surface, out to the radius.
         float t = (float(s) + jitter) / float(kSteps);
         vec3 q = origin + dir * (uRadius * t * t + 0.01);
         vec4 clip = uViewProj * vec4(q, 1.0);
         if (clip.w <= 0.0)
            break;
         vec2 quv = clip.xy / clip.w * 0.5 + 0.5;
         if (any(lessThan(quv, vec2(0.0))) || any(greaterThan(quv, vec2(1.0))))
            break;
         float sceneDepth = textureLod(uDepth, quv, 0.0).r;
         if (sceneDepth >= 1.0)
            continue;
         vec3 hit = worldAt(quv, sceneDepth);
         float qz = (uViewMatrix * vec4(q, 1.0)).z;
         float hz = (uViewMatrix * vec4(hit, 1.0)).z;
         // The ray went behind a surface (closer to the camera than the ray
         // point), and not by more than its assumed thickness.
         if (hz > qz && hz - qz < uThickness)
         {
            vec4 prev = uPrevViewProj * vec4(hit, 1.0);
            vec2 puv = prev.xy / prev.w * 0.5 + 0.5;
            if (prev.w > 0.0 && all(greaterThanEqual(puv, vec2(0.0))) && all(lessThanEqual(puv, vec2(1.0))))
               sum += max(textureLod(uPrevColor, puv, 0.0).rgb, vec3(0.0)) * uInvExposure;
            break;
         }
      }
   }
   imageStore(uDst, p, vec4(sum / float(kDirections), viewZ));
}

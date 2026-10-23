#version 430
// Turbo 0.70: Render 3D FX - the ambient occlusion (smoothed over 5x5 along
// the depth, which also hides its per-pixel noise) and screen-space
// reflections (the view ray mirrored at the surface, marched through the depth
// buffer; a hit takes the picture's colour there, weighted by Fresnel and
// faded at the screen's edges) applied to the render's picture, in linear.
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uColor; // the render's picture (display-encoded)
layout(binding = 1) uniform sampler2D uDepth;
layout(binding = 2) uniform sampler2D uAo;    // output size, 1 = open
layout(rgba16f, binding = 0) uniform writeonly image2D uOut;

uniform ivec2 uSize;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform int uUseAo;
uniform float uSsr;      // 0 = off
uniform float uSsrDistance;

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
   vec4 src = texture(uColor, uv);
   vec3 c = pow(max(src.rgb, vec3(0.0)), vec3(2.2));
   float d = texture(uDepth, uv).r;
   bool background = d >= 0.99999;
   if (!background)
   {
      vec3 P = ViewPos(uv);
      if (uUseAo == 1)
      {
         float sum = 0.0, wsum = 0.0;
         for (int y = -2; y <= 2; y++)
            for (int x = -2; x <= 2; x++)
            {
               ivec2 q = clamp(p + ivec2(x, y), ivec2(0), uSize - 1);
               vec2 quv = (vec2(q) + 0.5) * texel;
               float qz = ViewPos(quv).z;
               float w = exp(-abs(qz - P.z) / max(0.02 * abs(P.z), 1e-3));
               sum += texelFetch(uAo, q, 0).r * w;
               wsum += w;
            }
         c *= sum / max(wsum, 1e-5);
      }
      if (uSsr > 0.0)
      {
         vec3 px = ViewPos(uv + vec2(texel.x, 0.0)) - P, py = ViewPos(uv + vec2(0.0, texel.y)) - P;
         vec3 n = normalize(cross(px, py));
         vec3 view = normalize(P);
         if (dot(n, -view) < 0.0)
            n = -n;
         vec3 r = reflect(view, n);
         const int kSteps = 48;
         float stepLen = uSsrDistance / float(kSteps);
         vec3 hit = vec3(0.0);
         float found = 0.0;
         for (int i = 1; i <= kSteps; i++)
         {
            vec3 q = P + r * (stepLen * float(i));
            vec4 clip = uProj * vec4(q, 1.0);
            if (clip.w <= 1e-4)
               break;
            vec2 quv = clip.xy / clip.w * 0.5 + 0.5;
            if (any(lessThan(quv, vec2(0.0))) || any(greaterThan(quv, vec2(1.0))))
               break;
            if (texture(uDepth, quv).r >= 0.99999)
               continue;
            float sceneZ = ViewPos(quv).z;
            float behind = sceneZ - q.z; // > 0: the ray went behind the surface there
            if (behind > 0.0 && behind < stepLen * 2.0 + 0.02 * abs(q.z))
            {
               vec2 edge = smoothstep(vec2(0.0), vec2(0.1), quv) * (1.0 - smoothstep(vec2(0.9), vec2(1.0), quv));
               hit = pow(max(texture(uColor, quv).rgb, vec3(0.0)), vec3(2.2));
               found = edge.x * edge.y * (1.0 - float(i) / float(kSteps));
               break;
            }
         }
         float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(-view, n), 0.0), 5.0);
         c = mix(c, hit, clamp(found * uSsr * (0.3 + 0.7 * fresnel), 0.0, 1.0));
      }
   }
   imageStore(uOut, p, vec4(pow(c, vec3(1.0 / 2.2)), src.a));
}

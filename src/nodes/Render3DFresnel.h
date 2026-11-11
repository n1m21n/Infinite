#pragma once

// Fresnel for the main specular lobe, one source for GL (forward, path tracer) and Metal (vec3 = float3 there).
// Dielectrics (f0 < 0.08) use the exact unpolarised dielectric curve at the ior that gives f0, as Cycles does;
// Schlick's (1 - cos)^5 runs brighter at grazing angles. Coloured, metal-like f0 (> 0.25) keeps Schlick; between
// the two they blend.
#define R3D_FRESNEL_SRC \
   "vec3 fresnelExact(float c, vec3 f0) {\n" \
   "   c = clamp(c, 1e-4, 1.0);\n" \
   "   vec3 s = sqrt(clamp(f0, vec3(0.0), vec3(0.99)));\n" \
   "   vec3 eta = (vec3(1.0) + s) / (vec3(1.0) - s);\n" \
   "   vec3 g = sqrt(eta * eta - vec3(1.0 - c * c));\n" \
   "   vec3 a = (g - vec3(c)) / (g + vec3(c));\n" \
   "   vec3 b = (c * (g + vec3(c)) - vec3(1.0)) / (c * (g - vec3(c)) + vec3(1.0));\n" \
   "   vec3 fd = 0.5 * a * a * (vec3(1.0) + b * b);\n" \
   "   vec3 fs = f0 + (vec3(1.0) - f0) * pow(1.0 - c, 5.0);\n" \
   "   return mix(fd, fs, smoothstep(vec3(0.08), vec3(0.25), f0));\n" \
   "}\n"

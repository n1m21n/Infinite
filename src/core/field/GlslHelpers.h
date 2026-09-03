#pragma once

namespace Field
{
   // kFieldHelperPrelude is emitted into every generated shader.
   // Matches Expression.cpp semantics rather than GLSL defaults.
   constexpr const char* kFieldHelperPrelude =
      "// ---- helper prelude (always emitted) ----\n"
      "float fld_mod(float x, float y) { return x - y * trunc(x / y); } vec2 fld_mod(vec2 x, vec2 y) { return x - y * trunc(x / y); } vec3 fld_mod(vec3 x, vec3 y) { return x - y * trunc(x / y); } vec4 fld_mod(vec4 x, vec4 y) { return x - y * trunc(x / y); }\n"
      "float fld_clamp(float x, float a, float b) { return min(max(x, a), b); }\n"
      "vec2  fld_clamp(vec2 x, vec2 a, vec2 b) { return min(max(x, a), b); }\n"
      "vec3  fld_clamp(vec3 x, vec3 a, vec3 b) { return min(max(x, a), b); }\n"
      "vec4  fld_clamp(vec4 x, vec4 a, vec4 b) { return min(max(x, a), b); }\n"
      "float fld_lerp(float a, float b, float t) { return a + (b - a) * t; }\n"
      "vec2  fld_lerp(vec2 a, vec2 b, vec2 t) { return a + (b - a) * t; }\n"
      "vec3  fld_lerp(vec3 a, vec3 b, vec3 t) { return a + (b - a) * t; }\n"
      "vec4  fld_lerp(vec4 a, vec4 b, vec4 t) { return a + (b - a) * t; }\n"
      "float fld_smoothstep(float e0, float e1, float x) {\n"
      "   if (e0 == e1) return (x < e0) ? 0.0 : 1.0;\n"
      "   float u = fld_clamp((x - e0) / (e1 - e0), 0.0, 1.0);\n"
      "   return u * u * (3.0 - 2.0 * u);\n"
      "}\n"
      "vec2 fld_smoothstep(vec2 e0, vec2 e1, vec2 x) {\n"
      "   vec2 u = fld_clamp((x - e0) / mix(e1 - e0, vec2(1.0), equal(e0, e1)), vec2(0.0), vec2(1.0));\n"
      "   vec2 h = u * u * (vec2(3.0) - vec2(2.0) * u);\n"
      "   return mix(h, mix(vec2(1.0), vec2(0.0), lessThan(x, e0)), equal(e0, e1));\n"
      "}\n"
      "vec3 fld_smoothstep(vec3 e0, vec3 e1, vec3 x) {\n"
      "   vec3 u = fld_clamp((x - e0) / mix(e1 - e0, vec3(1.0), equal(e0, e1)), vec3(0.0), vec3(1.0));\n"
      "   vec3 h = u * u * (vec3(3.0) - vec3(2.0) * u);\n"
      "   return mix(h, mix(vec3(1.0), vec3(0.0), lessThan(x, e0)), equal(e0, e1));\n"
      "}\n"
      "vec4 fld_smoothstep(vec4 e0, vec4 e1, vec4 x) {\n"
      "   vec4 u = fld_clamp((x - e0) / mix(e1 - e0, vec4(1.0), equal(e0, e1)), vec4(0.0), vec4(1.0));\n"
      "   vec4 h = u * u * (vec4(3.0) - vec4(2.0) * u);\n"
      "   return mix(h, mix(vec4(1.0), vec4(0.0), lessThan(x, e0)), equal(e0, e1));\n"
      "}\n"
      "float fld_pow(float b, float e) {\n"
      "   if (b >= 0.0) return pow(b, e);\n"
      "   if (e == trunc(e)) return ((e - 2.0 * trunc(e / 2.0)) == 0.0) ? pow(-b, e) : -pow(-b, e);\n"
      "   return 0.0 / 0.0;\n"
      "}\n"
      "vec2 fld_pow(vec2 b, vec2 e) { return vec2(fld_pow(b.x, e.x), fld_pow(b.y, e.y)); }\n"
      "vec3 fld_pow(vec3 b, vec3 e) { return vec3(fld_pow(b.x, e.x), fld_pow(b.y, e.y), fld_pow(b.z, e.z)); }\n"
      "vec4 fld_pow(vec4 b, vec4 e) { return vec4(fld_pow(b.x, e.x), fld_pow(b.y, e.y), fld_pow(b.z, e.z), fld_pow(b.w, e.w)); }\n";
}

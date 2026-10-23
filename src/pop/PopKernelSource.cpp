#include "PopGpu.h"

// GLSL generation for PointZ kernels, kept free of OpenGL calls so the sources
// can be validated offline (glslangValidator) and unit-tested.

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace pop
{
   namespace
   {
      const char* kPreamble = R"GLSL(
const float PI = 3.14159265358979;
const float TAU = 6.28318530717959;
// GLSL 1.50 has no fbits: quantise instead (1/4096 steps).
uint fbits(float x) { return uint(int(floor(clamp(x, -500000.0, 500000.0) * 4096.0))); }
uint ihash(uint x)
{
   x ^= x >> 16; x *= 0x7feb352du;
   x ^= x >> 15; x *= 0x846ca68bu;
   x ^= x >> 16;
   return x;
}
float hash11(float p) { return float(ihash(fbits(p) ^ 0x9e3779b9u)) / 4294967295.0; }
float hashi(int i, float seed) { return float(ihash(uint(i) * 2654435761u ^ fbits(seed))) / 4294967295.0; }
vec3 hash3i(int i, float seed)
{
   uint h = ihash(uint(i) * 2654435761u ^ fbits(seed));
   uint h2 = ihash(h + 0x68e31da4u);
   uint h3 = ihash(h2 + 0xb5297a4du);
   return vec3(float(h), float(h2), float(h3)) / 4294967295.0;
}
float hash31(vec3 p) { return float(ihash(fbits(p.x) ^ ihash(fbits(p.y) ^ ihash(fbits(p.z))))) / 4294967295.0; }
vec3 hash33(vec3 p)
{
   uint h = ihash(fbits(p.x) ^ ihash(fbits(p.y) ^ ihash(fbits(p.z))));
   uint h2 = ihash(h + 0x68e31da4u);
   uint h3 = ihash(h2 + 0xb5297a4du);
   return vec3(float(h), float(h2), float(h3)) / 4294967295.0;
}
vec3 latticeGrad(ivec3 c)
{
   uint h = ihash(uint(c.x) * 73856093u ^ ihash(uint(c.y) * 19349663u ^ ihash(uint(c.z) * 83492791u)));
   float u = float(h & 0xffffu) / 65535.0 * 2.0 - 1.0;
   float a = float(h >> 16) / 65535.0 * TAU;
   float s = sqrt(max(0.0, 1.0 - u * u));
   return vec3(s * cos(a), s * sin(a), u);
}
float gnoise(vec3 p)
{
   ivec3 i = ivec3(floor(p));
   vec3 f = p - floor(p);
   vec3 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
   float n000 = dot(latticeGrad(i), f);
   float n100 = dot(latticeGrad(i + ivec3(1, 0, 0)), f - vec3(1, 0, 0));
   float n010 = dot(latticeGrad(i + ivec3(0, 1, 0)), f - vec3(0, 1, 0));
   float n110 = dot(latticeGrad(i + ivec3(1, 1, 0)), f - vec3(1, 1, 0));
   float n001 = dot(latticeGrad(i + ivec3(0, 0, 1)), f - vec3(0, 0, 1));
   float n101 = dot(latticeGrad(i + ivec3(1, 0, 1)), f - vec3(1, 0, 1));
   float n011 = dot(latticeGrad(i + ivec3(0, 1, 1)), f - vec3(0, 1, 1));
   float n111 = dot(latticeGrad(i + ivec3(1, 1, 1)), f - vec3(1, 1, 1));
   float x00 = mix(n000, n100, w.x), x10 = mix(n010, n110, w.x);
   float x01 = mix(n001, n101, w.x), x11 = mix(n011, n111, w.x);
   return clamp(mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z) * 1.8, -1.0, 1.0);
}
float fbm(vec3 p, int octaves, float rough)
{
   float sum = 0.0, amp = 1.0, norm = 0.0;
   for (int o = 0; o < 8; o++)
   {
      if (o >= octaves) break;
      sum += gnoise(p) * amp;
      norm += amp;
      amp *= rough;
      p = p * 2.03 + vec3(17.1, 9.7, 3.3);
   }
   return norm > 0.0 ? sum / norm : 0.0;
}
vec3 vnoise3(vec3 p)
{
   return vec3(gnoise(p), gnoise(p + vec3(31.4, 47.9, 11.3)), gnoise(p + vec3(-23.7, 13.1, 61.7)));
}
vec3 curl(vec3 p)
{
   const float e = 0.05;
   vec3 dx = vec3(e, 0.0, 0.0), dy = vec3(0.0, e, 0.0), dz = vec3(0.0, 0.0, e);
   vec3 px0 = vnoise3(p - dx), px1 = vnoise3(p + dx);
   vec3 py0 = vnoise3(p - dy), py1 = vnoise3(p + dy);
   vec3 pz0 = vnoise3(p - dz), pz1 = vnoise3(p + dz);
   float x = (py1.z - py0.z) - (pz1.y - pz0.y);
   float y = (pz1.x - pz0.x) - (px1.z - px0.z);
   float z = (px1.y - px0.y) - (py1.x - py0.x);
   return vec3(x, y, z) / (2.0 * e);
}
// 4D gradient noise (own implementation): a hashed unit gradient per 4D
// lattice corner, quintic fade, two 3D cubes blended along w.
vec4 latticeGrad4(ivec4 c)
{
   uint h = ihash(uint(c.x) * 73856093u ^ ihash(uint(c.y) * 19349663u ^ ihash(uint(c.z) * 83492791u ^ ihash(uint(c.w) * 2654435761u))));
   uint h2 = ihash(h + 0x68e31da4u);
   vec4 g = vec4(float(h & 0xffffu), float(h >> 16), float(h2 & 0xffffu), float(h2 >> 16)) / 65535.0 * 2.0 - 1.0;
   return g * inversesqrt(max(dot(g, g), 1e-6));
}
float gcube4(ivec4 b, vec4 f, vec3 w)
{
   float n000 = dot(latticeGrad4(b), f);
   float n100 = dot(latticeGrad4(b + ivec4(1, 0, 0, 0)), f - vec4(1, 0, 0, 0));
   float n010 = dot(latticeGrad4(b + ivec4(0, 1, 0, 0)), f - vec4(0, 1, 0, 0));
   float n110 = dot(latticeGrad4(b + ivec4(1, 1, 0, 0)), f - vec4(1, 1, 0, 0));
   float n001 = dot(latticeGrad4(b + ivec4(0, 0, 1, 0)), f - vec4(0, 0, 1, 0));
   float n101 = dot(latticeGrad4(b + ivec4(1, 0, 1, 0)), f - vec4(1, 0, 1, 0));
   float n011 = dot(latticeGrad4(b + ivec4(0, 1, 1, 0)), f - vec4(0, 1, 1, 0));
   float n111 = dot(latticeGrad4(b + ivec4(1, 1, 1, 0)), f - vec4(1, 1, 1, 0));
   float x00 = mix(n000, n100, w.x), x10 = mix(n010, n110, w.x);
   float x01 = mix(n001, n101, w.x), x11 = mix(n011, n111, w.x);
   return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z);
}
float gnoise4(vec4 p)
{
   ivec4 i = ivec4(floor(p));
   vec4 f = p - floor(p);
   vec4 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
   float a = gcube4(i, f, w.xyz);
   float b = gcube4(i + ivec4(0, 0, 0, 1), f - vec4(0, 0, 0, 1), w.xyz);
   return clamp(mix(a, b, w.w) * 1.9, -1.0, 1.0);
}
// Value noise -1..1: a hashed value per lattice corner, smoothly blended.
float lval4(ivec4 c) { return float(ihash(uint(c.x) * 73856093u ^ ihash(uint(c.y) * 19349663u ^ ihash(uint(c.z) * 83492791u ^ ihash(uint(c.w) * 2654435761u))))) / 4294967295.0 * 2.0 - 1.0; }
float vcube4(ivec4 b, vec3 w)
{
   float x00 = mix(lval4(b), lval4(b + ivec4(1, 0, 0, 0)), w.x);
   float x10 = mix(lval4(b + ivec4(0, 1, 0, 0)), lval4(b + ivec4(1, 1, 0, 0)), w.x);
   float x01 = mix(lval4(b + ivec4(0, 0, 1, 0)), lval4(b + ivec4(1, 0, 1, 0)), w.x);
   float x11 = mix(lval4(b + ivec4(0, 1, 1, 0)), lval4(b + ivec4(1, 1, 1, 0)), w.x);
   return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z);
}
float valnoise4(vec4 p)
{
   ivec4 i = ivec4(floor(p));
   vec4 f = p - floor(p);
   vec4 w = f * f * (3.0 - 2.0 * f);
   return mix(vcube4(i, w.xyz), vcube4(i + ivec4(0, 0, 0, 1), w.xyz), w.w);
}
// Cellular (Worley F1) noise -1..1. The feature point of each cell orbits
// inside it as w changes, so translating in 4D makes the cells breathe.
float cellnoise4(vec4 p)
{
   ivec3 i = ivec3(floor(p.xyz));
   vec3 f = p.xyz - floor(p.xyz);
   float d = 8.0;
   for (int z = -1; z <= 1; z++)
      for (int y = -1; y <= 1; y++)
         for (int x = -1; x <= 1; x++)
         {
            ivec3 c = i + ivec3(x, y, z);
            uint h = ihash(uint(c.x) * 73856093u ^ ihash(uint(c.y) * 19349663u ^ ihash(uint(c.z) * 83492791u)));
            vec3 r = vec3(float(h & 1023u), float((h >> 10) & 1023u), float((h >> 20) & 1023u)) / 1023.0;
            r = 0.5 + 0.42 * sin(p.w + TAU * r);
            vec3 v = vec3(float(x), float(y), float(z)) + r - f;
            d = min(d, dot(v, v));
         }
   return clamp(sqrt(d) * 1.7 - 1.0, -1.0, 1.0);
}
mat3 rotX(float a) { float c = cos(a), s = sin(a); return mat3(1, 0, 0, 0, c, s, 0, -s, c); }
mat3 rotY(float a) { float c = cos(a), s = sin(a); return mat3(c, 0, -s, 0, 1, 0, s, 0, c); }
mat3 rotZ(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0, -s, c, 0, 0, 0, 1); }
mat3 eulerDeg(vec3 d) { vec3 r = radians(d); return rotZ(r.z) * rotY(r.y) * rotX(r.x); }
mat3 alignVec(vec3 a, vec3 b)
{
   a = normalize(a); b = normalize(b);
   vec3 v = cross(a, b);
   float c = dot(a, b);
   if (c < -0.9999)
   {
      vec3 axis = abs(a.x) < 0.9 ? normalize(cross(a, vec3(1, 0, 0))) : normalize(cross(a, vec3(0, 1, 0)));
      return mat3(2.0 * outerProduct(axis, axis) - mat3(1.0));
   }
   mat3 vx = mat3(0.0, v.z, -v.y, -v.z, 0.0, v.x, v.y, -v.x, 0.0);
   return mat3(1.0) + vx + vx * vx * (1.0 / (1.0 + c));
}
float remap(float x, float a, float b, float c, float d) { return c + (x - a) * (d - c) / ((b - a) == 0.0 ? 1e-9 : (b - a)); }
)GLSL";

      std::string DefaultLiteral(const std::string& name, int comps)
      {
         float d[4];
         DefaultValue(name, d);
         char buf[160];
         if (comps == 1)
            snprintf(buf, sizeof(buf), "float(%.9g)", d[0]);
         else if (comps == 2)
            snprintf(buf, sizeof(buf), "vec2(%.9g, %.9g)", d[0], d[1]);
         else if (comps == 3)
            snprintf(buf, sizeof(buf), "vec3(%.9g, %.9g, %.9g)", d[0], d[1], d[2]);
         else
            snprintf(buf, sizeof(buf), "vec4(%.9g, %.9g, %.9g, %.9g)", d[0], d[1], d[2], d[3]);
         return buf;
      }

      const char* UniformDecl(KernelUniform::Type t)
      {
         switch (t)
         {
            case KernelUniform::kFloat: return "float";
            case KernelUniform::kInt: return "int";
            case KernelUniform::kVec2: return "vec2";
            case KernelUniform::kVec3: return "vec3";
            case KernelUniform::kVec4: return "vec4";
            case KernelUniform::kMat4: return "mat4";
         }
         return "float";
      }
   }

   void DefaultValue(const std::string& name, float out[4])
   {
      out[0] = out[1] = out[2] = 0.0f;
      out[3] = 0.0f;
      if (name == attr::N) { out[2] = 1.0f; }
      else if (name == attr::Color) { out[0] = out[1] = out[2] = out[3] = 1.0f; }
      else if (name == attr::PScale || name == attr::Active || name == attr::PartMass || name == attr::Weight)
         out[0] = 1.0f;
      else if (name == attr::Up) { out[1] = 1.0f; }
      else if (name == attr::Scale) { out[0] = out[1] = out[2] = 1.0f; }
   }

   int DefaultComps(const std::string& name)
   {
      if (name == attr::Color)
         return 4;
      if (name == attr::P || name == attr::N || name == attr::Tex || name == attr::Rotate || name == attr::Scale ||
          name == attr::Up || name == attr::PartVel || name == attr::PartForce)
         return 3;
      return 1;
   }

   const char* KernelPreamble() { return kPreamble; }

   std::string KernelDefaultLiteral(const std::string& name, int comps) { return DefaultLiteral(name, comps); }

   const char* GlslType(int comps)
   {
      static const char* k[] = { "float", "float", "vec2", "vec3", "vec4" };
      return k[std::max(1, std::min(4, comps))];
   }

   bool ValidAttribName(const std::string& name)
   {
      if (name.empty() || name.size() > 48)
         return false;
      if (!(std::isalpha((unsigned char)name[0]) || name[0] == '_'))
         return false;
      for (char c : name)
         if (!(std::isalnum((unsigned char)c) || c == '_'))
            return false;
      if (name.rfind("gl_", 0) == 0 || name.rfind("u", 0) == 0 && name.size() > 1 && std::isupper((unsigned char)name[1]))
         return false; // uniforms are uXxx
      static const char* kReserved[] = { "id", "in", "out", "float", "int", "uint", "bool", "vec2", "vec3", "vec4",
                                         "mat3", "mat4", "if", "else", "for", "while", "return", "main", "discard",
                                         "const", "uniform", "true", "false", "break", "continue", "do", "struct",
                                         "sample", "PI", "TAU", "texture", "dot", "mix", "length", "normalize" };
      for (const char* r : kReserved)
         if (name == r)
            return false;
      return true;
   }

   std::string BuildKernelSource(const KernelSpec& spec, const std::vector<KernelUniform>& uniforms,
                                 std::vector<std::string>& attribNames, std::vector<std::string>& varyings)
   {
      attribNames.clear();
      varyings.clear();
      // Turbo 0.70: GLSL 1.50 has no `precise`; a body may use it (to match the
      // CPU bit for bit on the compute backend) and still build here.
      std::string src = "#version 150\n#define precise\n";
      src += "uniform int uCount;\nuniform float uTime;\nuniform float uBeat;\nuniform float uDelta;\n"
             "uniform int uFrame;\nuniform float uSeed;\nuniform sampler2D uImage;\nuniform int uHasImage;\n";
      for (const KernelUniform& u : uniforms)
         src += std::string("uniform ") + UniformDecl(u.type) + " " + u.name + ";\n";
      for (const KernelIO& r : spec.reads)
      {
         src += std::string("in ") + GlslType(r.comps) + " a_" + r.name + ";\n";
         attribNames.push_back("a_" + r.name);
      }
      for (const KernelIO& w : spec.writes)
      {
         src += std::string("out ") + GlslType(w.comps) + " o_" + w.name + ";\n";
         varyings.push_back("o_" + w.name);
      }
      for (size_t k = 0; k < spec.lookups.size(); k++)
      {
         const KernelIO& l = spec.lookups[k];
         const std::string& ln = l.as.empty() ? l.name : l.as;
         char line[512];
         snprintf(line, sizeof(line), "uniform samplerBuffer t_%s;\nuniform int c_%s;\nuniform int n_%s;\n",
                  ln.c_str(), ln.c_str(), ln.c_str());
         src += line;
      }
      src += kPreamble;
      src += "vec4 sampleImage(vec2 uv) { return uHasImage != 0 ? texture(uImage, uv) : vec4(1.0); }\n";
      for (const KernelIO& l : spec.lookups)
      {
         const std::string& ln = l.as.empty() ? l.name : l.as;
         // Raw float buffer: component c of point i lives at i * comps + c.
         const std::string t = GlslType(l.comps);
         std::string f = t + " fetch_" + ln + "(int i)\n{\n   i = clamp(i, 0, max(0, n_" + ln + " - 1));\n";
         f += "   int b = i * c_" + ln + ";\n";
         if (l.comps == 1)
            f += "   return texelFetch(t_" + ln + ", b).r;\n";
         else
         {
            f += "   return " + t + "(";
            for (int c = 0; c < l.comps; c++)
            {
               if (c > 0)
                  f += ", ";
               f += "(c_" + ln + " > " + std::to_string(c) + " ? texelFetch(t_" + ln + ", b + " +
                    std::to_string(c) + ").r : " + (l.name == attr::Color && c == 3 ? "1.0" : "0.0") + ")";
            }
            f += ");\n";
         }
         f += "}\n";
         src += f;
      }
      src += spec.decls;
      src += "\nvoid main()\n{\n   int id = gl_VertexID;\n";
      for (const KernelIO& r : spec.reads)
         src += std::string("   ") + GlslType(r.comps) + " " + r.name + " = a_" + r.name + ";\n";
      for (const KernelIO& w : spec.writes)
      {
         bool alsoRead = false;
         for (const KernelIO& r : spec.reads)
            alsoRead = alsoRead || r.name == w.name;
         if (!alsoRead)
            src += std::string("   ") + GlslType(w.comps) + " " + w.name + " = " + DefaultLiteral(w.name, w.comps) + ";\n";
      }
      src += "   {\n" + spec.body + "\n   }\n";
      for (const KernelIO& w : spec.writes)
         src += "   o_" + w.name + " = " + w.name + ";\n";
      src += "}\n";

      return src;
   }

   // Turbo 0.70 (P1, docs/adr/0001-pointz-compute-backend.md): the same kernel
   // as a compute shader. Every attribute is a tightly packed float[] SSBO
   // (never vec3[]: std430 pads it to 16 bytes); reads follow the vertex-attribute
   // rules of the transform-feedback path, so both give the same numbers:
   //   rc_<name> = components in the buffer, 0 = absent (the attribute's default);
   //   a buffer with fewer components than the type fills them with 0, 0, 0, 1.
   // Bindings: reads 0.., writes after them, lookups after those.
   std::string BuildComputeSource(const KernelSpec& spec, const std::vector<KernelUniform>& uniforms)
   {
      std::string src = "#version 430\nlayout(local_size_x = 256) in;\n";
      src += "uniform int uCount;\nuniform float uTime;\nuniform float uBeat;\nuniform float uDelta;\n"
             "uniform int uFrame;\nuniform float uSeed;\nuniform sampler2D uImage;\nuniform int uHasImage;\n";
      for (const KernelUniform& u : uniforms)
         src += std::string("uniform ") + UniformDecl(u.type) + " " + u.name + ";\n";
      int binding = 0;
      char line[512];
      for (const KernelIO& r : spec.reads)
      {
         snprintf(line, sizeof(line),
                  "layout(std430, binding = %d) readonly buffer PzR_%s { float r_%s[]; };\nuniform int rc_%s;\n",
                  binding++, r.name.c_str(), r.name.c_str(), r.name.c_str());
         src += line;
      }
      for (const KernelIO& w : spec.writes)
      {
         snprintf(line, sizeof(line), "layout(std430, binding = %d) writeonly buffer PzW_%s { float w_%s[]; };\n",
                  binding++, w.name.c_str(), w.name.c_str());
         src += line;
      }
      for (const KernelIO& l : spec.lookups)
      {
         const std::string& ln = l.as.empty() ? l.name : l.as;
         snprintf(line, sizeof(line),
                  "layout(std430, binding = %d) readonly buffer PzL_%s { float t_%s[]; };\nuniform int c_%s;\n"
                  "uniform int n_%s;\n",
                  binding++, ln.c_str(), ln.c_str(), ln.c_str(), ln.c_str());
         src += line;
      }
      src += kPreamble;
      // No derivatives outside a fragment shader: level 0, as the vertex path samples.
      src += "vec4 sampleImage(vec2 uv) { return uHasImage != 0 ? textureLod(uImage, uv, 0.0) : vec4(1.0); }\n";
      for (const KernelIO& l : spec.lookups)
      {
         const std::string& ln = l.as.empty() ? l.name : l.as;
         const std::string t = GlslType(l.comps);
         std::string f = t + " fetch_" + ln + "(int i)\n{\n   i = clamp(i, 0, max(0, n_" + ln + " - 1));\n";
         f += "   int b = i * c_" + ln + ";\n";
         if (l.comps == 1)
            f += "   return t_" + ln + "[b];\n";
         else
         {
            f += "   return " + t + "(";
            for (int c = 0; c < l.comps; c++)
            {
               if (c > 0)
                  f += ", ";
               f += "(c_" + ln + " > " + std::to_string(c) + " ? t_" + ln + "[b + " + std::to_string(c) + "] : " +
                    (l.name == attr::Color && c == 3 ? "1.0" : "0.0") + ")";
            }
            f += ");\n";
         }
         f += "}\n";
         src += f;
      }
      src += spec.decls;
      src += "\nvoid main()\n{\n   int id = int(gl_GlobalInvocationID.x);\n   if (id >= uCount)\n      return;\n";
      for (const KernelIO& r : spec.reads)
      {
         const std::string& n = r.name;
         src += std::string("   ") + GlslType(r.comps) + " " + n + " = " + DefaultLiteral(n, r.comps) + ";\n";
         src += "   if (rc_" + n + " > 0)\n   {\n      int pz_i = id * rc_" + n + ";\n";
         if (r.comps == 1)
            src += "      " + n + " = r_" + n + "[pz_i];\n";
         else
         {
            src += "      " + n + " = " + GlslType(r.comps) + "(";
            for (int c = 0; c < r.comps; c++)
            {
               if (c > 0)
                  src += ", ";
               src += "(rc_" + n + " > " + std::to_string(c) + " ? r_" + n + "[pz_i + " + std::to_string(c) + "] : " +
                      (c == 3 ? "1.0" : "0.0") + ")";
            }
            src += ");\n";
         }
         src += "   }\n";
      }
      for (const KernelIO& w : spec.writes)
      {
         bool alsoRead = false;
         for (const KernelIO& r : spec.reads)
            alsoRead = alsoRead || r.name == w.name;
         if (!alsoRead)
            src += std::string("   ") + GlslType(w.comps) + " " + w.name + " = " + DefaultLiteral(w.name, w.comps) + ";\n";
      }
      src += "   {\n" + spec.body + "\n   }\n";
      for (const KernelIO& w : spec.writes)
      {
         if (w.comps == 1)
            src += "   w_" + w.name + "[id] = " + w.name + ";\n";
         else
            for (int c = 0; c < w.comps; c++)
               src += "   w_" + w.name + "[id * " + std::to_string(w.comps) + " + " + std::to_string(c) + "] = " + w.name +
                      "[" + std::to_string(c) + "];\n";
      }
      src += "}\n";
      return src;
   }
}

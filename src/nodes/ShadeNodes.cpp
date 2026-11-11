#include "nodes/ShadeNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
   // A literal the Field lexer reads as a float: fixed point, always a '.', near-zero snapped so a baked rotation
   // matrix does not carry 1e-8 noise into a floor().
   std::string F(float v)
   {
      if (std::fabs(v) < 1e-6f || !std::isfinite(v)) v = 0.0f;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.8f", (double)v);
      std::string s = buf;
      while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
      return s == "-0.0" ? "0.0" : s;
   }
   std::string V3(float x, float y, float z) { return "vec3(" + F(x) + ", " + F(y) + ", " + F(z) + ")"; }
   float SrgbToLinear(float c)
   {
      return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
   }

   // R = Rz * Ry * Rx (XYZ Euler, angles in degrees), row-major.
   void Rotation(const float deg[3], float r[3][3])
   {
      const float k = 3.14159265358979f / 180.0f;
      const float cx = std::cos(deg[0] * k), sx = std::sin(deg[0] * k);
      const float cy = std::cos(deg[1] * k), sy = std::sin(deg[1] * k);
      const float cz = std::cos(deg[2] * k), sz = std::sin(deg[2] * k);
      r[0][0] = cz * cy; r[0][1] = cz * sy * sx - sz * cx; r[0][2] = cz * sy * cx + sz * sx;
      r[1][0] = sz * cy; r[1][1] = sz * sy * sx + cz * cx; r[1][2] = sz * sy * cx - cz * sx;
      r[2][0] = -sy;     r[2][1] = cy * sx;                r[2][2] = cy * cx;
   }
   float SafeInv(float s) { return std::fabs(s) < 1e-8f ? 0.0f : 1.0f / s; }
}

std::string ShadeBuild::Input(const ImageCable& c, const std::string& fallback)
{
   auto* n = dynamic_cast<const ShadeNode*>(c.Resolved());
   if (!n) return fallback;
   auto it = vars.find(n);
   if (it != vars.end()) return it->second;
   if (++depth > 64)
   {
      error = "shading chain loops back on itself";
      return fallback;
   }
   const std::string v = n->Emit(*this);
   depth--;
   vars[n] = v;
   return v;
}

std::string ShadeBuild::Generated() const
{
   const float s[3] = { std::max(hi[0] - lo[0], 1e-5f), std::max(hi[1] - lo[1], 1e-5f), std::max(hi[2] - lo[2], 1e-5f) };
   return "((P - " + V3(lo[0], lo[1], lo[2]) + ") * " + V3(1.0f / s[0], 1.0f / s[1], 1.0f / s[2]) + ")";
}

std::string ShadeBuild::NewVar(const ShadeNode*, const std::string& expr)
{
   const std::string name = "sh" + std::to_string(counter++);
   body += name + " = " + expr + "\n";
   return name;
}

const std::vector<std::string>& ShadeCoordNode::OutputNames()
{
   static const std::vector<std::string> k = { "UV", "Object", "World", "Generated", "Normal", "View", "Reflection" };
   return k;
}

std::string ShadeCoordNode::Emit(ShadeBuild& b) const
{
   switch (output)
   {
   case kObject: return b.NewVar(this, "P");
   case kWorld: return b.NewVar(this, "W");
   case kGenerated: return b.NewVar(this, b.Generated());
   case kNormal: return b.NewVar(this, "N");
   case kView: return b.NewVar(this, "V");
   case kReflection: return b.NewVar(this, "N * (2.0 * dot(N, V)) - V");
   default: return b.NewVar(this, "vec3(UV.x, UV.y, 0.0)");
   }
}

const std::vector<std::string>& ShadeMappingNode::KindNames()
{
   static const std::vector<std::string> k = { "Point", "Texture", "Vector", "Normal" };
   return k;
}

std::string ShadeMappingNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mVector, b.Generated());
   float r[3][3];
   Rotation(rotate, r);
   float m[3][3];
   float off[3] = { 0.0f, 0.0f, 0.0f };
   switch (kind)
   {
   case kTexture:
      // S^-1 R^T (v - T)
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++) m[i][j] = SafeInv(scale[i]) * r[j][i];
      for (int i = 0; i < 3; i++) off[i] = -(m[i][0] * translate[0] + m[i][1] * translate[1] + m[i][2] * translate[2]);
      break;
   case kNormal:
      // R (v / S), unit length below
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++) m[i][j] = r[i][j] * SafeInv(scale[j]);
      break;
   default:
      // Point: T + R (S v); Vector: R (S v)
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++) m[i][j] = r[i][j] * scale[j];
      if (kind == kPoint) { off[0] = translate[0]; off[1] = translate[1]; off[2] = translate[2]; }
      break;
   }
   std::string e = "vec3(";
   for (int i = 0; i < 3; i++)
   {
      if (i) e += ", ";
      e += "dot(" + V3(m[i][0], m[i][1], m[i][2]) + ", " + in + ") + " + F(off[i]);
   }
   e += ")";
   if (kind == kNormal) e = "normalize(" + e + ")";
   return b.NewVar(this, e);
}

std::string ShadeCheckerNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mVector, b.Generated());
   const std::string c1 = V3(SrgbToLinear(color1[0]), SrgbToLinear(color1[1]), SrgbToLinear(color1[2]));
   const std::string c2 = V3(SrgbToLinear(color2[0]), SrgbToLinear(color2[1]), SrgbToLinear(color2[2]));
   // checker() is 1 where floor(x) + floor(y) + floor(z) is odd: color1 there.
   return b.NewVar(this, "mix(" + c2 + ", " + c1 + ", checker(" + in + " * " + F(scale) + "))");
}

std::string ComposeShadeProgram(const ShadeNode* out, const float lo[3], const float hi[3], std::string& err)
{
   ShadeBuild b;
   for (int i = 0; i < 3; i++) { b.lo[i] = lo[i]; b.hi[i] = hi[i]; }
   b.depth = 1;
   const std::string v = out->Emit(b);
   if (!b.error.empty())
   {
      err = b.error;
      return std::string();
   }
   // A coordinate on its own shows as a colour, as Blender shows a vector wired into Base Color.
   return b.body + "base = " + v + "\n";
}

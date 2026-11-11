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

const std::vector<std::string>& ShadeNoiseNode::OutputNames()
{
   static const std::vector<std::string> k = { "Value", "Colour" };
   return k;
}

std::string ShadeNoiseNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mVector, b.Generated());
   std::string p = b.NewVar(this, in + " * " + F(scale));
   if (std::fabs(distortion) > 1e-6f)
      p = b.NewVar(this, p + " + vec3(noise3(" + p + " + vec3(13.5, 7.25, 3.75)), noise3(" + p +
                            " + vec3(-5.5, 21.75, 9.25)), noise3(" + p + " + vec3(31.25, -11.5, 17.75))) * " + F(distortion));
   const std::string tail = ", " + F(std::floor(std::max(detail, 0.0f)) + 1.0f) + ", 2.0, " + F(std::min(std::max(roughness, 0.0f), 1.0f)) + ")";
   auto value = [&](const std::string& at) { return "clamp(0.5 + 0.5 * fbm(" + at + tail + ", 0.0, 1.0)"; };
   if (output == kColour)
      return b.NewVar(this, "vec3(" + value(p) + ", " + value(p + " + vec3(47.5, 19.25, -23.75)") + ", " +
                               value(p + " + vec3(-37.25, 53.5, 11.75)") + ")");
   const std::string v = b.NewVar(this, value(p));
   return b.NewVar(this, "vec3(" + v + ", " + v + ", " + v + ")");
}

const std::vector<std::string>& ShadeVoronoiNode::OutputNames()
{
   static const std::vector<std::string> k = { "Distance", "Second", "Edge", "Cell", "Colour" };
   return k;
}

std::string ShadeVoronoiNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mVector, b.Generated());
   const std::string c = b.NewVar(this, "voronoi3(" + in + " * " + F(scale) + ")");
   std::string v;
   switch (output)
   {
   case kSecond: v = c + ".y"; break;
   case kEdge: v = c + ".y - " + c + ".x"; break;
   case kCell: v = c + ".z"; break;
   case kColour:
      return b.NewVar(this, "vec3(" + c + ".z, fract(" + c + ".z * 7.31 + 0.37), fract(" + c + ".z * 13.73 + 0.71))");
   default: v = c + ".x"; break;
   }
   const std::string g = b.NewVar(this, "clamp(" + v + ", 0.0, 1.0)");
   return b.NewVar(this, "vec3(" + g + ", " + g + ", " + g + ")");
}

const std::vector<std::string>& ShadeGradientNode::KindNames()
{
   static const std::vector<std::string> k = { "Linear", "Quadratic", "Easing", "Diagonal", "Spherical",
                                               "Quadratic Sphere", "Radial", "Wave", "Magic" };
   return k;
}

std::string ShadeGradientNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mVector, b.Generated());
   const std::string p = b.NewVar(this, in + " * " + F(scale));
   std::string v;
   switch (kind)
   {
   case kQuadratic:
   {
      const std::string r = b.NewVar(this, "clamp(" + p + ".x, 0.0, 1.0)");
      v = r + " * " + r;
      break;
   }
   case kEasing:
   {
      const std::string r = b.NewVar(this, "clamp(" + p + ".x, 0.0, 1.0)");
      v = r + " * " + r + " * (3.0 - 2.0 * " + r + ")";
      break;
   }
   case kDiagonal: v = "clamp((" + p + ".x + " + p + ".y) * 0.5, 0.0, 1.0)"; break;
   case kSpherical: v = "max(1.0 - length(" + p + "), 0.0)"; break;
   case kQuadraticSphere:
   {
      const std::string r = b.NewVar(this, "max(1.0 - length(" + p + "), 0.0)");
      v = r + " * " + r;
      break;
   }
   case kRadial: v = "atan2(" + p + ".y, " + p + ".x) * 0.15915494 + 0.5"; break;
   case kWave: v = "wave(" + p + ")"; break;
   case kMagic:
      v = "clamp(0.5 + 0.25 * (sin(" + p + ".x * 6.2831853 + cos(" + p + ".y * 6.2831853)) + cos(" + p +
          ".y * 6.2831853 - sin(" + p + ".z * 6.2831853 + " + p + ".x * 3.1415927))), 0.0, 1.0)";
      break;
   default: v = "clamp(" + p + ".x, 0.0, 1.0)"; break;
   }
   const std::string g = b.NewVar(this, v);
   return b.NewVar(this, "vec3(" + g + ", " + g + ", " + g + ")");
}

std::string ShadeBrickNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mVector, b.Generated());
   const float w = std::max(width, 1e-3f), h = std::max(height, 1e-3f);
   const std::string c1 = V3(SrgbToLinear(color1[0]), SrgbToLinear(color1[1]), SrgbToLinear(color1[2]));
   const std::string c2 = V3(SrgbToLinear(color2[0]), SrgbToLinear(color2[1]), SrgbToLinear(color2[2]));
   const std::string cm = V3(SrgbToLinear(mortarColor[0]), SrgbToLinear(mortarColor[1]), SrgbToLinear(mortarColor[2]));
   const std::string p = b.NewVar(this, in + " * " + F(scale));
   const std::string row = b.NewVar(this, "floor(" + p + ".y * " + F(1.0f / h) + ")");
   // every second row shifts by offset bricks; the row at 0 is a shifted one (measured on Cycles' brick texture)
   const std::string x = b.NewVar(this, p + ".x + " + F(w * offset) + " * mod(" + row + " + 1.0, 2.0)");
   const std::string col = b.NewVar(this, "floor(" + x + " * " + F(1.0f / w) + ")");
   const std::string fx = b.NewVar(this, x + " - " + col + " * " + F(w));
   const std::string fy = b.NewVar(this, p + ".y - " + row + " * " + F(h));
   const std::string edge = b.NewVar(this, "min(min(" + fx + ", " + F(w) + " - " + fx + "), min(" + fy + ", " + F(h) + " - " + fy + "))");
   const std::string brick = b.NewVar(this, "mix(" + c1 + ", " + c2 + ", white(vec3(" + col + ", " + row + ", 0.0)) * " +
                                               F(std::min(std::max(tint, 0.0f), 1.0f)) + ")");
   return b.NewVar(this, "mix(" + cm + ", " + brick + ", step(" + F(std::max(mortar, 0.0f)) + ", " + edge + "))");
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

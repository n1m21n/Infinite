#include "nodes/ShadeNodes.h"

#include "core/BlendModes.h"

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

namespace
{
   // A knob a pin can override: the wired node's x (or its vector), else the knob's own value inline.
   std::string PinScalar(ShadeBuild& b, const ImageCable& c, float own)
   {
      const std::string v = b.Input(c, "");
      return v.empty() ? "(" + F(own) + ")" : v + ".x";
   }
   std::string PinColour(ShadeBuild& b, const ImageCable& c, const float srgb[3])
   {
      const std::string v = b.Input(c, "");
      return v.empty() ? V3(SrgbToLinear(srgb[0]), SrgbToLinear(srgb[1]), SrgbToLinear(srgb[2])) : v;
   }
}

std::string ShadeCheckerNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mIn[0], b.Generated());
   const std::string c1 = PinColour(b, mIn[1], color1), c2 = PinColour(b, mIn[2], color2);
   // checker() is 1 where floor(x) + floor(y) + floor(z) is odd: color1 there.
   return b.NewVar(this, "mix(" + c2 + ", " + c1 + ", checker(" + in + " * " + PinScalar(b, mIn[3], scale) + "))");
}

const std::vector<std::string>& ShadeNoiseNode::OutputNames()
{
   static const std::vector<std::string> k = { "Value", "Colour" };
   return k;
}

std::string ShadeNoiseNode::Emit(ShadeBuild& b) const
{
   const std::string in = b.Input(mIn[0], b.Generated());
   std::string p = b.NewVar(this, in + " * " + PinScalar(b, mIn[1], scale));
   // detail stays a knob: it is the octave count, fixed when the program is written
   const bool distWired = mIn[3].IsConnected();
   if (distWired || std::fabs(distortion) > 1e-6f)
      p = b.NewVar(this, p + " + vec3(noise3(" + p + " + vec3(13.5, 7.25, 3.75)), noise3(" + p +
                            " + vec3(-5.5, 21.75, 9.25)), noise3(" + p + " + vec3(31.25, -11.5, 17.75))) * " + PinScalar(b, mIn[3], distortion));
   const std::string rough = mIn[2].IsConnected() ? "clamp(" + PinScalar(b, mIn[2], roughness) + ", 0.0, 1.0)"
                                                  : F(std::min(std::max(roughness, 0.0f), 1.0f));
   const std::string tail = ", " + F(std::floor(std::max(detail, 0.0f)) + 1.0f) + ", 2.0, " + rough + ")";
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
   const std::string in = b.Input(mIn[0], b.Generated());
   const std::string c = b.NewVar(this, "voronoi3(" + in + " * " + PinScalar(b, mIn[1], scale) + ")");
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
   const std::string in = b.Input(mIn[0], b.Generated());
   const float w = std::max(width, 1e-3f), h = std::max(height, 1e-3f);
   const std::string c1 = PinColour(b, mIn[1], color1), c2 = PinColour(b, mIn[2], color2);
   const std::string cm = PinColour(b, mIn[3], mortarColor);
   const std::string p = b.NewVar(this, in + " * " + PinScalar(b, mIn[4], scale));
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

const std::vector<std::string>& ShadeImageNode::ProjectionNames()
{
   static const std::vector<std::string> k = { "Flat", "Box" };
   return k;
}

const std::vector<std::string>& ShadeImageNode::SpaceNames()
{
   static const std::vector<std::string> k = { "Colour", "Data" };
   return k;
}

std::string ShadeImageNode::Emit(ShadeBuild& b) const
{
   // unwired vector: UV for a flat picture (as Blender's Image Texture), Generated for a box
   const std::string in = b.Input(mVector, projection == kBox ? b.Generated() : "vec3(UV.x, UV.y, 0.0)");
   if (mImage.Resolved() == nullptr || dynamic_cast<const ShadeNode*>(mImage.Resolved()) != nullptr)
      return b.NewVar(this, "vec3(0.0, 0.0, 0.0)"); // no picture: black
   int slot = -1;
   for (size_t i = 0; i < b.images.size(); i++)
      if (b.images[i]->ShadeImageTexture() == ShadeImageTexture()) slot = (int)i; // one picture, one slot
   if (slot < 0)
   {
      if (b.images.size() >= 4)
      {
         b.error = "a material reads at most four Shade Image pictures";
         return b.NewVar(this, "vec3(0.0, 0.0, 0.0)");
      }
      slot = (int)b.images.size();
      b.images.push_back(this);
   }
   const std::string fn = std::string(space == kData ? "img" : "imgc") + "(" + F((float)slot) + ", ";
   if (projection != kBox)
   {
      const std::string c = b.NewVar(this, fn + "vec2(" + in + ".x, " + in + ".y))");
      return b.NewVar(this, "vec3(" + c + ".x, " + c + ".y, " + c + ".z)");
   }
   // triplanar: one read along each axis, weighted by how far the normal faces it; blend 0 is a hard box, 1 soft
   const std::string p = b.NewVar(this, in);
   const float sharp = 1.0f + (1.0f - std::clamp(blend, 0.0f, 1.0f)) * 15.0f;
   const std::string w = b.NewVar(this, "pow(max(abs(N), vec3(0.0001, 0.0001, 0.0001)), vec3(" + F(sharp) + ", " + F(sharp) + ", " + F(sharp) + "))");
   const std::string cx = b.NewVar(this, fn + "vec2(" + p + ".z, " + p + ".y))");
   const std::string cy = b.NewVar(this, fn + "vec2(" + p + ".x, " + p + ".z))");
   const std::string cz = b.NewVar(this, fn + "vec2(" + p + ".x, " + p + ".y))");
   const std::string s = b.NewVar(this, "(" + cx + " * " + w + ".x + " + cy + " * " + w + ".y + " + cz + " * " + w + ".z) / (" + w + ".x + " + w + ".y + " + w + ".z)");
   return b.NewVar(this, "vec3(" + s + ".x, " + s + ".y, " + s + ".z)");
}

// ---- Value nodes (row 2.5) ----
namespace
{
   std::string FP(float v) { return "(" + F(v) + ")"; }
   std::string Local(ShadeBuild& b, const std::string& e) { return b.NewVar(nullptr, e); }
   // The scalar a pin carries: x of the wired node, else the node's own number (inline, never a local of its own).
   std::string Scalar(ShadeBuild& b, const ImageCable& c, float own)
   {
      const std::string v = b.Input(c, "");
      return v.empty() ? FP(own) : v + ".x";
   }
   // The vector a pin carries, as a local.
   std::string Vector(ShadeBuild& b, const ImageCable& c, const std::string& own)
   {
      const std::string v = b.Input(c, "");
      return v.empty() ? Local(b, own) : v;
   }
   std::string SafeDiv(const std::string& x, const std::string& y) { return "if(" + y + " != 0.0, " + x + " / " + y + ", 0.0)"; }
   std::string Trunc(const std::string& x) { return "(sign(" + x + ") * floor(abs(" + x + ")))"; }
   std::string TruncMod(const std::string& a, const std::string& b)
   {
      return "if(" + b + " != 0.0, " + a + " - " + b + " * " + Trunc("(" + a + " / " + b + ")") + ", 0.0)";
   }
   std::string Wrap(const std::string& a, const std::string& hi, const std::string& lo)
   {
      const std::string range = "(" + hi + " - " + lo + ")";
      return "if(" + range + " != 0.0, " + a + " - " + range + " * floor((" + a + " - " + lo + ") / " + range + "), " + lo + ")";
   }
   std::string Snap(const std::string& a, const std::string& b) { return "if(" + b + " != 0.0, floor(" + a + " / " + b + ") * " + b + ", 0.0)"; }
   std::string Lum(const std::string& c) { return "dot(" + c + ", vec3(0.3, 0.59, 0.11))"; }
   std::string MinC(const std::string& c) { return "min(" + c + ".x, min(" + c + ".y, " + c + ".z))"; }
   std::string MaxC(const std::string& c) { return "max(" + c + ".x, max(" + c + ".y, " + c + ".z))"; }

   // core/BlendModes.cpp's SetSat / SetLum (with its ClipColor), as locals.
   std::string SetSat(ShadeBuild& b, const std::string& c, const std::string& s)
   {
      const std::string mn = Local(b, MinC(c)), mx = Local(b, MaxC(c));
      return Local(b, "if(" + mx + " > " + mn + ", (" + c + " - vec3(" + mn + ")) * (" + s + " / (" + mx + " - " + mn + ")), vec3(0.0))");
   }
   std::string SetLum(ShadeBuild& b, const std::string& c, const std::string& l)
   {
      const std::string c2 = Local(b, c + " + vec3(" + l + " - " + Lum(c) + ")");
      const std::string l2 = Local(b, Lum(c2)), n = Local(b, MinC(c2)), x = Local(b, MaxC(c2));
      const std::string c3 = Local(b, "if(" + n + " < 0.0, vec3(" + l2 + ") + (" + c2 + " - vec3(" + l2 + ")) * (" + l2 + " / (" + l2 + " - " + n + " + 0.000001)), " + c2 + ")");
      return Local(b, "if(" + x + " > 1.0, vec3(" + l2 + ") + (" + c3 + " - vec3(" + l2 + ")) * ((1.0 - " + l2 + ") / (" + x + " - " + l2 + " + 0.000001)), " + c3 + ")");
   }

   // blendMode(mode, A, B) of core/BlendModes.cpp for the 30 colour modes; A and B are locals.
   std::string Blend(ShadeBuild& b, int mode, const std::string& A, const std::string& B)
   {
      const std::string One = "vec3(1.0)", Z = "vec3(0.0)", E = "vec3(0.0001)", H = "vec3(0.5)";
      const std::string hard = "mix(2.0 * " + A + " * " + B + ", " + One + " - 2.0 * (" + One + " - " + A + ") * (" + One + " - " + B + "), ";
      switch (mode)
      {
      case 0: return B;
      case 1: return "min(" + A + ", " + B + ")";
      case 2: return A + " * " + B;
      case 3: return One + " - min((" + One + " - " + A + ") / max(" + B + ", " + E + "), " + One + ")";
      case 4: return "clamp(" + A + " + " + B + " - " + One + ", " + Z + ", " + One + ")";
      case 5: return "if(" + Lum(A) + " <= " + Lum(B) + ", " + A + ", " + B + ")";
      case 6: return "max(" + A + ", " + B + ")";
      case 7: return One + " - (" + One + " - " + A + ") * (" + One + " - " + B + ")";
      case 8: return "min(" + A + " / max(" + One + " - " + B + ", " + E + "), " + One + ")";
      case 9: return "clamp(" + A + " + " + B + ", " + Z + ", " + One + ")";
      case 10: return "if(" + Lum(A) + " >= " + Lum(B) + ", " + A + ", " + B + ")";
      case 11: return hard + "step(" + H + ", " + A + "))";
      case 12:
      {
         const std::string d = Local(b, "mix(((16.0 * " + A + " - 12.0 * " + One + ") * " + A + " + 4.0 * " + One + ") * " + A + ", sqrt(" + A + "), step(vec3(0.25), " + A + "))");
         return "mix(" + A + " - (" + One + " - 2.0 * " + B + ") * " + A + " * (" + One + " - " + A + "), " + A + " + (2.0 * " + B + " - " + One + ") * (" + d + " - " + A + "), step(" + H + ", " + B + "))";
      }
      case 13: return hard + "step(" + H + ", " + B + "))";
      case 14: return "mix(" + One + " - min((" + One + " - " + A + ") / max(2.0 * " + B + ", " + E + "), " + One + "), min(" + A + " / max(2.0 * (" + One + " - " + B + "), " + E + "), " + One + "), step(" + H + ", " + B + "))";
      case 15: return "clamp(" + A + " + 2.0 * " + B + " - " + One + ", " + Z + ", " + One + ")";
      case 16: return "mix(min(" + A + ", 2.0 * " + B + "), max(" + A + ", 2.0 * " + B + " - " + One + "), step(" + H + ", " + B + "))";
      case 17: return "step(" + H + ", clamp(" + A + " + 2.0 * " + B + " - " + One + ", " + Z + ", " + One + "))";
      case 18: return "abs(" + A + " - " + B + ")";
      case 19: return A + " + " + B + " - 2.0 * " + A + " * " + B;
      case 20: return "clamp(" + A + " - " + B + ", " + Z + ", " + One + ")";
      case 21: return "min(" + A + " / max(" + B + ", " + E + "), " + One + ")";
      case 22: return SetLum(b, SetSat(b, B, Local(b, MaxC(A) + " - " + MinC(A))), Local(b, Lum(A)));
      case 23: return SetLum(b, SetSat(b, A, Local(b, MaxC(B) + " - " + MinC(B))), Local(b, Lum(A)));
      case 24: return SetLum(b, B, Local(b, Lum(A)));
      case 25: return SetLum(b, A, Local(b, Lum(B)));
      case 26: return "(" + A + " + " + B + ") * 0.5";
      case 27: return One + " - abs(" + One + " - " + A + " - " + B + ")";
      case 28: return "min(" + A + " * " + A + " / max(" + One + " - " + B + ", " + E + "), " + One + ")";
      case 29: return "min(" + B + " * " + B + " / max(" + One + " - " + A + ", " + E + "), " + One + ")";
      default: return A;
      }
   }
}

const std::vector<std::string>& ShadeMathNode::OpNames()
{
   static const std::vector<std::string> k = {
      "Add", "Subtract", "Multiply", "Divide", "Multiply Add", "Power", "Logarithm", "Square Root", "Inverse Square Root",
      "Absolute", "Exponent", "Minimum", "Maximum", "Less Than", "Greater Than", "Sign", "Compare", "Smooth Minimum",
      "Smooth Maximum", "Round", "Floor", "Ceil", "Truncate", "Fraction", "Truncated Modulo", "Floored Modulo", "Wrap",
      "Snap", "Ping-Pong", "Sine", "Cosine", "Tangent", "Arcsine", "Arccosine", "Arctangent", "Arctan2",
      "Hyperbolic Sine", "Hyperbolic Cosine", "Hyperbolic Tangent", "To Radians", "To Degrees"
   };
   return k;
}

int ShadeMathNode::Operands(int op)
{
   switch (op)
   {
   case kMultiplyAdd: case kCompare: case kSmoothMin: case kSmoothMax: case kWrap: return 3;
   case kAdd: case kSubtract: case kMultiply: case kDivide: case kPower: case kLogarithm: case kMinimum: case kMaximum:
   case kLessThan: case kGreaterThan: case kTruncModulo: case kFloorModulo: case kSnap: case kPingPong: case kArctan2:
      return 2;
   default: return 1;
   }
}

std::string ShadeMathNode::Emit(ShadeBuild& sb) const
{
   const int n = Operands(op);
   const std::string a = Scalar(sb, mIn[0], value[0]);
   const std::string b = n >= 2 ? Scalar(sb, mIn[1], value[1]) : "0.0";
   const std::string c = n >= 3 ? Scalar(sb, mIn[2], value[2]) : "0.0";
   std::string r;
   switch (op)
   {
   case kAdd: r = a + " + " + b; break;
   case kSubtract: r = a + " - " + b; break;
   case kMultiply: r = a + " * " + b; break;
   case kDivide: r = SafeDiv(a, b); break;
   case kMultiplyAdd: r = a + " * " + b + " + " + c; break;
   case kPower:
   {
      // a negative base only has a real power when the exponent is whole; its sign is the exponent's parity
      const std::string p = Local(sb, "pow(abs(" + a + "), " + b + ")");
      r = "if(" + a + " >= 0.0, " + p + ", if(fract(" + b + ") == 0.0, " + p + " * (1.0 - 2.0 * mod(abs(" + b + "), 2.0)), 0.0))";
      break;
   }
   case kLogarithm: r = "if(" + a + " > 0.0 && " + b + " > 0.0, log(" + a + ") / log(" + b + "), 0.0)"; break;
   case kSqrt: r = "if(" + a + " > 0.0, sqrt(" + a + "), 0.0)"; break;
   case kInvSqrt: r = "if(" + a + " > 0.0, 1.0 / sqrt(" + a + "), 0.0)"; break;
   case kAbsolute: r = "abs(" + a + ")"; break;
   case kExponent: r = "exp(" + a + ")"; break;
   case kMinimum: r = "min(" + a + ", " + b + ")"; break;
   case kMaximum: r = "max(" + a + ", " + b + ")"; break;
   case kLessThan: r = "if(" + a + " < " + b + ", 1.0, 0.0)"; break;
   case kGreaterThan: r = "if(" + a + " > " + b + ", 1.0, 0.0)"; break;
   case kSign: r = "sign(" + a + ")"; break;
   case kCompare: r = "if(abs(" + a + " - " + b + ") <= max(" + c + ", 0.00001), 1.0, 0.0)"; break;
   case kSmoothMin:
   case kSmoothMax:
   {
      const std::string h = Local(sb, "if(" + c + " != 0.0, max(" + c + " - abs(" + a + " - " + b + "), 0.0) / " + c + ", 0.0)");
      const std::string bump = h + " * " + h + " * " + h + " * " + c + " / 6.0";
      r = op == kSmoothMin ? "min(" + a + ", " + b + ") - " + bump : "max(" + a + ", " + b + ") + " + bump;
      break;
   }
   case kRound: r = "floor(" + a + " + 0.5)"; break;
   case kFloor: r = "floor(" + a + ")"; break;
   case kCeil: r = "ceil(" + a + ")"; break;
   case kTruncate: r = Trunc(a); break;
   case kFraction: r = a + " - floor(" + a + ")"; break;
   case kTruncModulo: r = TruncMod(a, b); break;
   case kFloorModulo: r = "if(" + b + " != 0.0, " + a + " - floor(" + a + " / " + b + ") * " + b + ", 0.0)"; break;
   case kWrap: r = Wrap(a, b, c); break;
   case kSnap: r = Snap(a, b); break;
   case kPingPong: r = "if(" + b + " != 0.0, abs(fract((" + a + " - " + b + ") / (" + b + " * 2.0)) * " + b + " * 2.0 - " + b + "), 0.0)"; break;
   case kSine: r = "sin(" + a + ")"; break;
   case kCosine: r = "cos(" + a + ")"; break;
   case kTangent: r = "tan(" + a + ")"; break;
   case kArcsine:
   case kArccosine:
   {
      const std::string x = Local(sb, "clamp(" + a + ", -1.0, 1.0)");
      const std::string q = "sqrt(1.0 - " + x + " * " + x + ")";
      r = op == kArcsine ? "atan2(" + x + ", " + q + ")" : "atan2(" + q + ", " + x + ")";
      break;
   }
   case kArctangent: r = "atan2(" + a + ", 1.0)"; break;
   case kArctan2: r = "atan2(" + a + ", " + b + ")"; break;
   case kSinh: r = "(exp(" + a + ") - exp(0.0 - " + a + ")) * 0.5"; break;
   case kCosh: r = "(exp(" + a + ") + exp(0.0 - " + a + ")) * 0.5"; break;
   case kTanh: r = "(exp(" + a + ") - exp(0.0 - " + a + ")) / (exp(" + a + ") + exp(0.0 - " + a + "))"; break;
   case kToRadians: r = a + " * " + F(3.14159265358979f / 180.0f); break;
   case kToDegrees: r = a + " * " + F(180.0f / 3.14159265358979f); break;
   default: r = a; break;
   }
   if (clampResult) r = "clamp(" + r + ", 0.0, 1.0)";
   return sb.NewVar(this, "vec3(" + r + ")");
}

const std::vector<std::string>& ShadeVectorMathNode::OpNames()
{
   static const std::vector<std::string> k = {
      "Add", "Subtract", "Multiply", "Divide", "Multiply Add", "Cross Product", "Project", "Reflect", "Refract",
      "Faceforward", "Dot Product", "Distance", "Length", "Scale", "Normalize", "Absolute", "Minimum", "Maximum",
      "Floor", "Ceil", "Fraction", "Modulo", "Wrap", "Snap", "Sine", "Cosine", "Tangent"
   };
   return k;
}

int ShadeVectorMathNode::Operands(int op)
{
   switch (op)
   {
   case kMultiplyAdd: case kFaceforward: case kWrap: return 3;
   case kLength: case kScale: case kNormalize: case kAbsolute: case kFloor: case kCeil: case kFraction:
   case kSine: case kCosine: case kTangent:
      return 1;
   default: return 2;
   }
}

bool ShadeVectorMathNode::UsesScale(int op) { return op == kScale || op == kRefract; }

std::string ShadeVectorMathNode::Emit(ShadeBuild& sb) const
{
   const int n = Operands(op);
   const std::string A = Vector(sb, mIn[0], V3(a[0], a[1], a[2]));
   const std::string B = n >= 2 ? Vector(sb, mIn[1], V3(b[0], b[1], b[2])) : A;
   const std::string C = n >= 3 ? Vector(sb, mIn[2], V3(c[0], c[1], c[2])) : A;
   // one scalar expression per component
   auto per = [&](const auto& f) {
      return "vec3(" + f(A + ".x", B + ".x", C + ".x") + ", " + f(A + ".y", B + ".y", C + ".y") + ", " + f(A + ".z", B + ".z", C + ".z") + ")";
   };
   auto unit = [&](const std::string& v) {
      const std::string len = Local(sb, "length(" + v + ")");
      return Local(sb, "if(" + len + " > 0.0, " + v + " / " + len + ", vec3(0.0))");
   };
   using S = const std::string&;
   std::string r;
   switch (op)
   {
   case kAdd: r = A + " + " + B; break;
   case kSubtract: r = A + " - " + B; break;
   case kMultiply: r = A + " * " + B; break;
   case kDivide: r = per([](S x, S y, S) { return SafeDiv(x, y); }); break;
   case kMultiplyAdd: r = A + " * " + B + " + " + C; break;
   case kCross: r = "cross(" + A + ", " + B + ")"; break;
   case kProject:
   {
      const std::string l = Local(sb, "dot(" + B + ", " + B + ")");
      r = "if(" + l + " != 0.0, " + B + " * (dot(" + A + ", " + B + ") / " + l + "), vec3(0.0))";
      break;
   }
   case kReflect:
   {
      const std::string nn = unit(B);
      r = A + " - " + nn + " * (2.0 * dot(" + A + ", " + nn + "))";
      break;
   }
   case kRefract:
   {
      const std::string nn = unit(B), eta = FP(scale);
      const std::string d = Local(sb, "dot(" + nn + ", " + A + ")");
      const std::string k = Local(sb, "1.0 - " + eta + " * " + eta + " * (1.0 - " + d + " * " + d + ")");
      r = "if(" + k + " < 0.0, vec3(0.0), " + A + " * " + eta + " - " + nn + " * (" + eta + " * " + d + " + sqrt(max(" + k + ", 0.0))))";
      break;
   }
   case kFaceforward: r = "if(dot(" + C + ", " + B + ") < 0.0, " + A + ", " + A + " * (-1.0))"; break;
   case kDot: r = "vec3(dot(" + A + ", " + B + "))"; break;
   case kDistance: r = "vec3(length(" + A + " - " + B + "))"; break;
   case kLength: r = "vec3(length(" + A + "))"; break;
   case kScale: r = A + " * " + FP(scale); break;
   case kNormalize: return sb.NewVar(this, unit(A));
   case kAbsolute: r = per([](S x, S, S) { return "abs(" + x + ")"; }); break;
   case kMinimum: r = per([](S x, S y, S) { return "min(" + x + ", " + y + ")"; }); break;
   case kMaximum: r = per([](S x, S y, S) { return "max(" + x + ", " + y + ")"; }); break;
   case kFloor: r = per([](S x, S, S) { return "floor(" + x + ")"; }); break;
   case kCeil: r = per([](S x, S, S) { return "ceil(" + x + ")"; }); break;
   case kFraction: r = per([](S x, S, S) { return x + " - floor(" + x + ")"; }); break;
   case kModulo: r = per([](S x, S y, S) { return TruncMod(x, y); }); break;
   case kWrap: r = per([](S x, S y, S z) { return Wrap(x, y, z); }); break;
   case kSnap: r = per([](S x, S y, S) { return Snap(x, y); }); break;
   case kSine: r = per([](S x, S, S) { return "sin(" + x + ")"; }); break;
   case kCosine: r = per([](S x, S, S) { return "cos(" + x + ")"; }); break;
   case kTangent: r = per([](S x, S, S) { return "tan(" + x + ")"; }); break;
   default: r = A; break;
   }
   return sb.NewVar(this, r);
}

const std::vector<std::string>& ShadeMixNode::DataTypeNames()
{
   static const std::vector<std::string> k = { "Float", "Vector", "Colour" };
   return k;
}

const std::vector<std::string>& ShadeMixNode::ModeNames()
{
   static const std::vector<std::string> k(BlendModes::Names().begin(), BlendModes::Names().begin() + BlendModes::kEraseMode);
   return k;
}

std::string ShadeMixNode::Emit(ShadeBuild& sb) const
{
   const bool wired = dynamic_cast<const ShadeNode*>(mIn[0].Resolved()) != nullptr;
   std::string f = Scalar(sb, mIn[0], clampFactor ? std::clamp(factor, 0.0f, 1.0f) : factor);
   if (clampFactor && wired) f = Local(sb, "clamp(" + f + ", 0.0, 1.0)");
   std::string r;
   if (dataType == kFloat)
   {
      const std::string x = Scalar(sb, mIn[1], a[0]), y = Scalar(sb, mIn[2], b[0]);
      r = "vec3(mix(" + x + ", " + y + ", " + f + "))";
   }
   else if (dataType == kVector)
   {
      const std::string A = Vector(sb, mIn[1], V3(a[0], a[1], a[2])), B = Vector(sb, mIn[2], V3(b[0], b[1], b[2]));
      r = "mix(" + A + ", " + B + ", " + f + ")";
   }
   else
   {
      const std::string A = Vector(sb, mIn[1], V3(SrgbToLinear(a[0]), SrgbToLinear(a[1]), SrgbToLinear(a[2])));
      const std::string B = Vector(sb, mIn[2], V3(SrgbToLinear(b[0]), SrgbToLinear(b[1]), SrgbToLinear(b[2])));
      const int m = mode >= 0 && mode < BlendModes::kEraseMode ? mode : 0;
      const std::string blended = Local(sb, Blend(sb, m, A, B));
      r = "mix(" + A + ", " + blended + ", " + f + ")";
   }
   if (clampResult) r = "clamp(" + r + ", vec3(0.0), vec3(1.0))";
   return sb.NewVar(this, r);
}

const std::vector<std::string>& ShadeRampNode::InterpNames()
{
   static const std::vector<std::string> k = { "Linear", "Ease", "Constant", "B-Spline" };
   return k;
}

void ShadeRampNode::VisitParams(ParamVisitor& v)
{
   static const char* kPos[kMaxStops] = { "pos0", "pos1", "pos2", "pos3", "pos4", "pos5", "pos6", "pos7" };
   static const char* kCol[kMaxStops] = { "color0", "color1", "color2", "color3", "color4", "color5", "color6", "color7" };
   v.Int("interp", interp);
   v.Int("count", count);
   v.Float("factor", factor);
   for (int i = 0; i < kMaxStops; i++)
   {
      v.Float(kPos[i], pos[i]);
      v.Color(kCol[i], color[i]);
   }
}

std::string ShadeRampNode::Emit(ShadeBuild& sb) const
{
   const int n = std::clamp(count, 2, kMaxStops);
   int order[kMaxStops];
   for (int i = 0; i < n; i++) order[i] = i;
   std::stable_sort(order, order + n, [&](int x, int y) { return pos[x] < pos[y]; });
   float p[kMaxStops];
   std::string col[kMaxStops];
   for (int i = 0; i < n; i++)
   {
      const float* c = color[order[i]];
      p[i] = pos[order[i]];
      col[i] = V3(SrgbToLinear(c[0]), SrgbToLinear(c[1]), SrgbToLinear(c[2]));
   }
   const std::string t = Scalar(sb, mFactor, factor);
   auto along = [&](const std::string& at, int i) { // 0..1 across stop i -> i + 1
      return "clamp((" + at + " - " + FP(p[i]) + ") * " + FP(1.0f / std::max(p[i + 1] - p[i], 1e-6f)) + ", 0.0, 1.0)";
   };
   if (interp == kBSpline)
   {
      const std::string tc = Local(sb, "clamp(" + t + ", " + FP(p[0]) + ", " + FP(p[n - 1]) + ")");
      std::string r;
      for (int i = 0; i < n - 1; i++)
      {
         const std::string s = Local(sb, along(tc, i));
         const std::string s2 = "(" + s + " * " + s + ")", s3 = "(" + s + " * " + s + " * " + s + ")";
         const std::string om = "(1.0 - " + s + ")";
         const std::string seg = Local(sb,
            col[std::max(i - 1, 0)] + " * (" + om + " * " + om + " * " + om + " / 6.0) + " +
            col[i] + " * ((3.0 * " + s3 + " - 6.0 * " + s2 + " + 4.0) / 6.0) + " +
            col[i + 1] + " * ((3.0 * " + s2 + " - 3.0 * " + s3 + " + 3.0 * " + s + " + 1.0) / 6.0) + " +
            col[std::min(i + 2, n - 1)] + " * (" + s3 + " / 6.0)");
         r = i == 0 ? seg : Local(sb, "if(" + tc + " >= " + FP(p[i]) + ", " + seg + ", " + r + ")");
      }
      return sb.NewVar(this, r);
   }
   std::string r = Local(sb, col[0]);
   for (int i = 0; i < n - 1; i++)
   {
      std::string w;
      if (interp == kConstant) w = "step(" + FP(p[i + 1]) + ", " + t + ")";
      else
      {
         w = Local(sb, along(t, i));
         if (interp == kEase) w = "(" + w + " * " + w + " * (3.0 - 2.0 * " + w + "))";
      }
      r = Local(sb, "mix(" + r + ", " + col[i + 1] + ", " + w + ")");
   }
   return sb.NewVar(this, r);
}

const std::vector<std::string>& ShadeMapRangeNode::InterpNames()
{
   static const std::vector<std::string> k = { "Linear", "Stepped", "Smoothstep", "Smootherstep" };
   return k;
}

std::string ShadeMapRangeNode::Emit(ShadeBuild& sb) const
{
   const std::string v = Scalar(sb, mValue, value);
   const float d = fromMax - fromMin, span = toMax - toMin;
   std::string r;
   if (interp == kLinear || interp == kStepped)
   {
      if (d == 0.0f) r = "0.0";
      else
      {
         std::string f = "((" + v + " - " + FP(fromMin) + ") * " + FP(1.0f / d) + ")";
         if (interp == kStepped)
            f = steps > 0.0f ? "(floor(" + f + " * " + FP(steps + 1.0f) + ") * " + FP(1.0f / steps) + ")" : "0.0";
         r = FP(toMin) + " + " + f + " * " + FP(span);
      }
      if (clampResult) r = "clamp(" + r + ", " + FP(std::min(toMin, toMax)) + ", " + FP(std::max(toMin, toMax)) + ")";
   }
   else
   {
      // 0..1 across the source range (either direction), then the smooth curve
      const std::string x = Local(sb, d == 0.0f ? "step(" + FP(fromMin) + ", " + v + ")"
                                                : "clamp((" + v + " - " + FP(fromMin) + ") * " + FP(1.0f / d) + ", 0.0, 1.0)");
      const std::string f = interp == kSmoothstep ? "(" + x + " * " + x + " * (3.0 - 2.0 * " + x + "))"
                                                  : "(" + x + " * " + x + " * " + x + " * (" + x + " * (" + x + " * 6.0 - 15.0) + 10.0))";
      r = FP(toMin) + " + " + f + " * " + FP(span);
   }
   return sb.NewVar(this, "vec3(" + r + ")");
}

const std::vector<std::string>& ShadeSeparateNode::ModeNames()
{
   static const std::vector<std::string> k = { "RGB / XYZ", "HSV" };
   return k;
}

const std::vector<std::string>& ShadeSeparateNode::ChannelNames(int mode)
{
   static const std::vector<std::string> kRgb = { "R / X", "G / Y", "B / Z" };
   static const std::vector<std::string> kHsv = { "Hue", "Saturation", "Value" };
   return mode == 1 ? kHsv : kRgb;
}

std::string ShadeSeparateNode::Emit(ShadeBuild& sb) const
{
   const std::string in = Vector(sb, mIn, "vec3(0.0)");
   const int ch = std::clamp(channel, 0, 2);
   if (mode != 1) return sb.NewVar(this, "vec3(" + in + (ch == 0 ? ".x" : ch == 1 ? ".y" : ".z") + ")");
   const std::string r = in + ".x", g = in + ".y", bl = in + ".z";
   const std::string mx = Local(sb, MaxC(in)), mn = Local(sb, MinC(in));
   const std::string d = Local(sb, mx + " - " + mn);
   if (ch == 2) return sb.NewVar(this, "vec3(" + mx + ")");
   if (ch == 1) return sb.NewVar(this, "vec3(if(" + mx + " > 0.0, " + d + " / " + mx + ", 0.0))");
   const std::string h = Local(sb, "if(" + d + " == 0.0, 0.0, if(" + r + " == " + mx + ", (" + g + " - " + bl + ") / " + d + ", if(" + g + " == " + mx +
                                      ", 2.0 + (" + bl + " - " + r + ") / " + d + ", 4.0 + (" + r + " - " + g + ") / " + d + ")) / 6.0)");
   return sb.NewVar(this, "vec3(if(" + h + " < 0.0, " + h + " + 1.0, " + h + "))");
}

std::string ShadeCombineNode::Emit(ShadeBuild& sb) const
{
   const std::string x = Scalar(sb, mIn[0], value[0]), y = Scalar(sb, mIn[1], value[1]), z = Scalar(sb, mIn[2], value[2]);
   if (mode != 1) return sb.NewVar(this, "vec3(" + x + ", " + y + ", " + z + ")");
   auto chan = [&](const char* k) { // x = hue, y = saturation, z = value
      const std::string q = Local(sb, x + " * 6.0 + " + k);
      const std::string w = "clamp(abs(" + q + " - 6.0 * floor(" + q + " / 6.0) - 3.0) - 1.0, 0.0, 1.0)";
      return z + " * (1.0 + " + y + " * (" + w + " - 1.0))";
   };
   return sb.NewVar(this, "vec3(" + chan("0.0") + ", " + chan("4.0") + ", " + chan("2.0") + ")");
}

void ShadeCurvesNode::VisitParams(ParamVisitor& v)
{
   static const char* kX[kMaxPoints] = { "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7" };
   static const char* kY[kMaxPoints] = { "y0", "y1", "y2", "y3", "y4", "y5", "y6", "y7" };
   v.Int("count", count);
   v.Float("factor", factor);
   for (int i = 0; i < kMaxPoints; i++)
   {
      v.Float(kX[i], px[i]);
      v.Float(kY[i], py[i]);
   }
}

std::string ShadeCurvesNode::Emit(ShadeBuild& sb) const
{
   const int n = std::clamp(count, 2, kMaxPoints);
   int order[kMaxPoints];
   for (int i = 0; i < n; i++) order[i] = i;
   std::stable_sort(order, order + n, [&](int a, int b) { return px[a] < px[b]; });
   float x[kMaxPoints], y[kMaxPoints], m[kMaxPoints];
   for (int i = 0; i < n; i++)
   {
      x[i] = px[order[i]];
      y[i] = py[order[i]];
      if (i > 0 && x[i] < x[i - 1] + 1e-6f) x[i] = x[i - 1] + 1e-6f; // stacked points: a step, not a divide by zero
   }
   for (int i = 0; i < n; i++) // Catmull-Rom slopes, one-sided at the ends
   {
      const int lo = std::max(i - 1, 0), hi = std::min(i + 1, n - 1);
      m[i] = (y[hi] - y[lo]) / (x[hi] - x[lo]);
   }
   const std::string in = Vector(sb, mIn, "vec3(0.0)");
   std::string out[3];
   const char* comp[3] = { ".x", ".y", ".z" };
   for (int k = 0; k < 3; k++)
   {
      const std::string t = Local(sb, in + comp[k]);
      std::string r = Local(sb, FP(y[0]) + " + " + FP(m[0]) + " * (" + t + " - " + FP(x[0]) + ")");
      for (int i = 0; i < n - 1; i++)
      {
         const float h = x[i + 1] - x[i];
         const std::string s = Local(sb, "clamp((" + t + " - " + FP(x[i]) + ") * " + FP(1.0f / h) + ", 0.0, 1.0)");
         const std::string s2 = "(" + s + " * " + s + ")", s3 = "(" + s + " * " + s + " * " + s + ")";
         const std::string seg = FP(y[i]) + " * (2.0 * " + s3 + " - 3.0 * " + s2 + " + 1.0) + " + FP(h * m[i]) + " * (" + s3 + " - 2.0 * " + s2 + " + " + s +
                                 ") + " + FP(y[i + 1]) + " * (3.0 * " + s2 + " - 2.0 * " + s3 + ") + " + FP(h * m[i + 1]) + " * (" + s3 + " - " + s2 + ")";
         r = Local(sb, "if(" + t + " >= " + FP(x[i]) + ", " + seg + ", " + r + ")");
      }
      out[k] = Local(sb, "if(" + t + " > " + FP(x[n - 1]) + ", " + FP(y[n - 1]) + " + " + FP(m[n - 1]) + " * (" + t + " - " + FP(x[n - 1]) + "), " + r + ")");
   }
   const std::string curved = "vec3(" + out[0] + ", " + out[1] + ", " + out[2] + ")";
   return sb.NewVar(this, "mix(" + in + ", " + curved + ", " + FP(std::clamp(factor, 0.0f, 1.0f)) + ")");
}

std::string ShadeBumpNode::Emit(ShadeBuild& b) const
{
   const std::string h = b.Input(mIn[0], "");
   if (!h.empty())
   {
      if (mIn[2].IsConnected() || mIn[3].IsConnected())
         b.heights.push_back(h + ".x * " + Scalar(b, mIn[2], strength) + " * " + Scalar(b, mIn[3], distance) + " * " + FP(invert ? -1.0f : 1.0f));
      else
         b.heights.push_back(h + ".x * " + FP(strength * distance * (invert ? -1.0f : 1.0f)));
   }
   return Vector(b, mIn[1], "vec3(0.0, 0.0, 1.0)");
}

std::string ShadeNormalMapNode::Emit(ShadeBuild& b) const
{
   // Blender's strength: the normal leans from flat towards the map, N + (map - N) * strength, then unit length.
   const std::string c = Vector(b, mIn[0], "vec3(0.5, 0.5, 1.0)");
   const std::string t = Local(b, c + " * 2.0 - vec3(1.0)");
   // inline, never a local: a constant local would be hoisted out of a Pixel program
   const std::string s = mIn[1].IsConnected() ? "(" + Scalar(b, mIn[1], strength) + ")" : FP(strength);
   return b.NewVar(this, "normalize(vec3(" + t + ".x * " + s + ", " + t + ".y * " + s + ", 1.0 + (" + t + ".z - 1.0) * " + s + "))");
}

namespace
{
   // Unpolarised Fresnel reflectance of a dielectric at cos(angle) c for relative index eta (the textbook form):
   // g = sqrt(eta^2 - 1 + c^2), F = 1/2 ((g - c) / (g + c))^2 (1 + ((c (g + c) - 1) / (c (g - c) + 1))^2); 1 past the
   // critical angle. Returns a scalar local.
   std::string Fresnel(ShadeBuild& b, const std::string& eta)
   {
      const std::string c = Local(b, "abs(dot(N, V))");
      const std::string e = Local(b, eta);
      const std::string g2 = Local(b, e + " * " + e + " - 1.0 + " + c + " * " + c);
      const std::string g = Local(b, "sqrt(max(" + g2 + ", 0.0))");
      const std::string A = Local(b, "(" + g + " - " + c + ") / (" + g + " + " + c + ")");
      const std::string B = Local(b, "(" + c + " * (" + g + " + " + c + ") - 1.0) / (" + c + " * (" + g + " - " + c + ") + 1.0)");
      return Local(b, "if(" + g2 + " > 0.0, 0.5 * " + A + " * " + A + " * (1.0 + " + B + " * " + B + "), 1.0)");
   }
}

std::string ShadeFresnelNode::Emit(ShadeBuild& b) const
{
   const std::string i = "max(" + Scalar(b, mIor, ior) + ", 0.00001)";
   const std::string f = Fresnel(b, "if(backfacing > 0.5, 1.0 / " + i + ", " + i + ")");
   return b.NewVar(this, "vec3(" + f + ")");
}

const std::vector<std::string>& ShadeLayerWeightNode::OutputNames()
{
   static const std::vector<std::string> k = { "Fresnel", "Facing" };
   return k;
}

std::string ShadeLayerWeightNode::Emit(ShadeBuild& b) const
{
   const std::string bl = Scalar(b, mBlend, blend);
   if (output == kFacing)
   {
      // An unwired blend is folded here (a constant scalar local would be hoisted out of a Pixel program).
      std::string e;
      if (dynamic_cast<const ShadeNode*>(mBlend.Resolved()))
      {
         const std::string k = Local(b, "clamp(" + bl + ", 0.0, 0.99999)");
         e = Local(b, "if(" + k + " < 0.5, 2.0 * " + k + ", 0.5 / (1.0 - " + k + "))");
      }
      else
      {
         const float k = std::clamp(blend, 0.0f, 0.99999f);
         e = FP(k < 0.5f ? 2.0f * k : 0.5f / (1.0f - k));
      }
      return b.NewVar(this, "vec3(1.0 - pow(max(abs(dot(N, V)), 0.000001), " + e + "))");
   }
   const std::string i = "max(1.0 - " + bl + ", 0.00001)";
   const std::string f = Fresnel(b, "if(backfacing > 0.5, " + i + ", 1.0 / " + i + ")");
   return b.NewVar(this, "vec3(" + f + ")");
}

const std::vector<std::string>& ShadeGeometryNode::OutputNames()
{
   static const std::vector<std::string> k = { "Position", "Normal", "Tangent", "Incoming", "Backfacing",
                                               "Curvature", "Cavity", "Random" };
   return k;
}

std::string ShadeGeometryNode::Emit(ShadeBuild& b) const
{
   switch (output)
   {
   case kNormal: return b.NewVar(this, "N");
   case kTangent:
   {
      // Blender's default tangent: around the up axis (Y here) through the middle of the bounds, laid into the surface.
      const std::string d = Local(b, b.Generated() + " - vec3(0.5)");
      const std::string r = Local(b, "vec3(" + d + ".z, 0.0, 0.0 - " + d + ".x)");
      return b.NewVar(this, "cross(N, normalize(cross(" + r + ", N) + vec3(0.0000001, 0.0, 0.0)))");
   }
   case kIncoming: return b.NewVar(this, "V");
   case kBackfacing: return b.NewVar(this, "vec3(backfacing)");
   case kCurvature: return b.NewVar(this, "vec3(curvature)");
   case kCavity: return b.NewVar(this, "vec3(cavity)");
   case kRandom: return b.NewVar(this, "vec3(objrand)");
   default: return b.NewVar(this, "W");
   }
}

std::string ComposeShadeProgram(const std::vector<ShadeOutput>& outs, const float lo[3], const float hi[3],
                                std::string& err, std::vector<const ShadeNode*>* images)
{
   ShadeBuild b;
   for (int i = 0; i < 3; i++) { b.lo[i] = lo[i]; b.hi[i] = hi[i]; }
   b.depth = 1;
   std::string tail;
   for (const ShadeOutput& o : outs)
   {
      if (!o.node) continue;
      std::string v;
      auto it = b.vars.find(o.node);
      if (it != b.vars.end()) v = it->second;
      else { v = o.node->Emit(b); b.vars[o.node] = v; }
      const std::string ch = o.channel;
      // A coordinate on its own shows as a colour, as Blender shows a vector wired into Base Color.
      const bool scalar = ch == "roughness" || ch == "metallic" || ch == "opacity";
      tail += ch + " = " + v + (scalar ? ".x" : "") + "\n";
   }
   if (!b.error.empty())
   {
      err = b.error;
      return std::string();
   }
   if (images) *images = b.images;
   std::string hsum;
   for (const std::string& t : b.heights) hsum += (hsum.empty() ? "" : " + ") + t;
   if (!hsum.empty()) tail += "height = " + hsum + "\n";
   return tail.empty() ? std::string() : b.body + tail;
}

std::string ComposeShadeProgram(const ShadeNode* out, const float lo[3], const float hi[3], std::string& err,
                                std::vector<const ShadeNode*>* images)
{
   return ComposeShadeProgram(std::vector<ShadeOutput>{ { "base", out } }, lo, hi, err, images);
}

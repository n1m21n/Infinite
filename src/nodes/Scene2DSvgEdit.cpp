// SVG as editable vector layers (ai-tools.md 3.3): the agent's `path` and `svg` ops. ImportSvg keeps a file as one
// picture item; this reads the common elements (path, rect, circle, ellipse, line, polyline, polygon, nested g with
// transforms, fill / stroke / opacity attributes and style="") into pen paths the user can edit anchor by anchor.
// Not a full SVG engine: gradients, patterns, text, use, clip paths and CSS classes are left out (reported as skipped).
#include "Scene2DNode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
   using Shape = Scene2DNode::Shape;

   struct Mat
   {
      float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;   // x' = a x + c y + e, y' = b x + d y + f
      void Apply(float x, float y, float& ox, float& oy) const { ox = a * x + c * y + e; oy = b * x + d * y + f; }
      Mat operator*(const Mat& m) const   // this then m inside it: this * m
      {
         Mat r;
         r.a = a * m.a + c * m.b;
         r.b = b * m.a + d * m.b;
         r.c = a * m.c + c * m.d;
         r.d = b * m.c + d * m.d;
         r.e = a * m.e + c * m.f + e;
         r.f = b * m.e + d * m.f + f;
         return r;
      }
      float Scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
   };

   struct Style
   {
      bool fillOn = true, strokeOn = false;
      float fill[4] = { 0, 0, 0, 1 }, stroke[4] = { 0, 0, 0, 1 };
      float width = 1.0f, opacity = 1.0f, fillOp = 1.0f, strokeOp = 1.0f;
      int cap = 0, join = 0;
   };

   // ---- numbers and colours ----------------------------------------------------------------------------------
   void SkipSep(const char*& p)
   {
      while (*p != 0 && (std::isspace((unsigned char)*p) || *p == ','))
         p++;
   }

   bool Num(const char*& p, float& v)
   {
      SkipSep(p);
      char* end = nullptr;
      v = std::strtof(p, &end);
      if (end == p)
         return false;
      p = end;
      return std::isfinite(v);
   }

   // An arc flag: one digit, which SVG lets run into the next number ("a1 1 0 01 1 1").
   bool Flag(const char*& p, bool& v)
   {
      SkipSep(p);
      if (*p != '0' && *p != '1')
         return false;
      v = *p == '1';
      p++;
      return true;
   }

   std::string Trim(const std::string& s)
   {
      size_t a = 0, b = s.size();
      while (a < b && std::isspace((unsigned char)s[a]))
         a++;
      while (b > a && std::isspace((unsigned char)s[b - 1]))
         b--;
      return s.substr(a, b - a);
   }

   int Hex(char ch)
   {
      if (ch >= '0' && ch <= '9')
         return ch - '0';
      ch = (char)std::tolower((unsigned char)ch);
      return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
   }

   // false = "none"; true with rgb set otherwise. Unknown names and url() fills read as black, as browsers fall back.
   bool Colour(const std::string& in, float rgb[3], bool& ok)
   {
      ok = true;
      std::string s = Trim(in);
      std::string low = s;
      for (char& ch : low)
         ch = (char)std::tolower((unsigned char)ch);
      if (low == "none" || low == "transparent")
         return false;
      if (!s.empty() && s[0] == '#')
      {
         int h[6] = {};
         const size_t n = s.size() - 1;
         for (size_t i = 0; i < n && i < 6; i++)
            h[i] = Hex(s[i + 1]);
         if (n == 3)
         {
            for (int k = 0; k < 3; k++)
               rgb[k] = h[k] < 0 ? 0.0f : h[k] * 17 / 255.0f;
            return true;
         }
         if (n == 6)
         {
            for (int k = 0; k < 3; k++)
               rgb[k] = h[k * 2] < 0 || h[k * 2 + 1] < 0 ? 0.0f : (h[k * 2] * 16 + h[k * 2 + 1]) / 255.0f;
            return true;
         }
      }
      if (low.rfind("rgb", 0) == 0)
      {
         const char* p = std::strchr(s.c_str(), '(');
         if (p != nullptr)
         {
            p++;
            for (int k = 0; k < 3; k++)
            {
               float v = 0;
               if (!Num(p, v))
                  break;
               SkipSep(p);
               if (*p == '%')
               {
                  v *= 2.55f;
                  p++;
               }
               rgb[k] = std::clamp(v / 255.0f, 0.0f, 1.0f);
            }
            return true;
         }
      }
      static const struct { const char* n; unsigned v; } kNames[] = {
         { "black", 0x000000 }, { "white", 0xffffff }, { "red", 0xff0000 }, { "green", 0x008000 }, { "lime", 0x00ff00 },
         { "blue", 0x0000ff }, { "yellow", 0xffff00 }, { "cyan", 0x00ffff }, { "aqua", 0x00ffff }, { "magenta", 0xff00ff },
         { "fuchsia", 0xff00ff }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xc0c0c0 }, { "maroon", 0x800000 },
         { "olive", 0x808000 }, { "navy", 0x000080 }, { "purple", 0x800080 }, { "teal", 0x008080 }, { "orange", 0xffa500 },
         { "pink", 0xffc0cb }, { "brown", 0xa52a2a }, { "gold", 0xffd700 }, { "coral", 0xff7f50 }, { "indigo", 0x4b0082 },
         { "violet", 0xee82ee }, { "salmon", 0xfa8072 }, { "tomato", 0xff6347 }, { "skyblue", 0x87ceeb },
         { "darkgray", 0xa9a9a9 }, { "darkgrey", 0xa9a9a9 }, { "lightgray", 0xd3d3d3 }, { "lightgrey", 0xd3d3d3 },
         { "currentcolor", 0x000000 } };
      for (const auto& c : kNames)
         if (low == c.n)
         {
            rgb[0] = ((c.v >> 16) & 255) / 255.0f;
            rgb[1] = ((c.v >> 8) & 255) / 255.0f;
            rgb[2] = (c.v & 255) / 255.0f;
            return true;
         }
      ok = false;
      rgb[0] = rgb[1] = rgb[2] = 0.0f;
      return true;
   }

   Mat Transform(const std::string& s)
   {
      Mat m;
      const char* p = s.c_str();
      while (*p != 0)
      {
         while (*p != 0 && !std::isalpha((unsigned char)*p))
            p++;
         const char* name = p;
         while (std::isalpha((unsigned char)*p))
            p++;
         const std::string fn(name, p - name);
         const char* open = std::strchr(p, '(');
         if (open == nullptr)
            break;
         p = open + 1;
         float v[6] = {};
         int n = 0;
         while (n < 6 && Num(p, v[n]))
            n++;
         const char* close = std::strchr(p, ')');
         p = close != nullptr ? close + 1 : p + std::strlen(p);
         Mat t;
         if (fn == "matrix" && n == 6)
            t = { v[0], v[1], v[2], v[3], v[4], v[5] };
         else if (fn == "translate" && n >= 1)
            t.e = v[0], t.f = n > 1 ? v[1] : 0.0f;
         else if (fn == "scale" && n >= 1)
            t.a = v[0], t.d = n > 1 ? v[1] : v[0];
         else if (fn == "rotate" && n >= 1)
         {
            const float r = v[0] * 3.14159265f / 180.0f, cs = std::cos(r), sn = std::sin(r);
            Mat rot = { cs, sn, -sn, cs, 0, 0 };
            if (n >= 3)
            {
               Mat to = { 1, 0, 0, 1, v[1], v[2] }, back = { 1, 0, 0, 1, -v[1], -v[2] };
               rot = to * rot * back;
            }
            t = rot;
         }
         else if (fn == "skewX" && n >= 1)
            t.c = std::tan(v[0] * 3.14159265f / 180.0f);
         else if (fn == "skewY" && n >= 1)
            t.b = std::tan(v[0] * 3.14159265f / 180.0f);
         m = m * t;
      }
      return m;
   }

   // ---- the path builder: anchors x y inX inY outX outY, absolute ------------------------------------------
   struct Builder
   {
      std::vector<Shape>& out;
      Shape cur;
      bool open = false;
      void Start(float x, float y)
      {
         Flush();
         cur = Shape();
         cur.type = Shape::kPath;
         cur.pts = { x, y, x, y, x, y };
         open = true;
      }
      void Line(float x, float y)
      {
         if (!open)
            Start(x, y);
         else
            cur.pts.insert(cur.pts.end(), { x, y, x, y, x, y });
      }
      void Cubic(float c1x, float c1y, float c2x, float c2y, float x, float y)
      {
         if (!open)
            Start(c1x, c1y);
         const size_t n = cur.pts.size();
         cur.pts[n - 2] = c1x;
         cur.pts[n - 1] = c1y;
         cur.pts.insert(cur.pts.end(), { x, y, c2x, c2y, x, y });
      }
      void Close()
      {
         if (!open)
            return;
         // A closing anchor on the first one folds into it: its in-handle becomes the first's.
         const size_t n = cur.pts.size();
         if (n >= 12 && std::fabs(cur.pts[n - 6] - cur.pts[0]) < 1e-3f && std::fabs(cur.pts[n - 5] - cur.pts[1]) < 1e-3f)
         {
            cur.pts[2] = cur.pts[n - 4];
            cur.pts[3] = cur.pts[n - 3];
            cur.pts.resize(n - 6);
         }
         cur.closed = true;
         Flush();
      }
      void Flush()
      {
         if (open && cur.pts.size() >= 12)
            out.push_back(cur);
         open = false;
      }
   };

   // SVG arc (endpoint form) as cubic pieces of <= 90 degrees.
   void Arc(Builder& b, float x0, float y0, float rx, float ry, float phiDeg, bool large, bool sweep, float x, float y)
   {
      if (rx == 0 || ry == 0 || (x0 == x && y0 == y))
      {
         b.Line(x, y);
         return;
      }
      rx = std::fabs(rx);
      ry = std::fabs(ry);
      const double phi = phiDeg * 3.14159265358979 / 180.0, cs = std::cos(phi), sn = std::sin(phi);
      const double dx = (x0 - x) * 0.5, dy = (y0 - y) * 0.5;
      const double x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
      double l = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
      double Rx = rx, Ry = ry;
      if (l > 1)
         Rx *= std::sqrt(l), Ry *= std::sqrt(l);
      double num = Rx * Rx * Ry * Ry - Rx * Rx * y1 * y1 - Ry * Ry * x1 * x1;
      double den = Rx * Rx * y1 * y1 + Ry * Ry * x1 * x1;
      double k = den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
      if (large == sweep)
         k = -k;
      const double cx1 = k * Rx * y1 / Ry, cy1 = -k * Ry * x1 / Rx;
      const double cx = cs * cx1 - sn * cy1 + (x0 + x) * 0.5, cy = sn * cx1 + cs * cy1 + (y0 + y) * 0.5;
      auto ang = [](double ux, double uy, double vx, double vy) {
         const double a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
         return a;
      };
      const double t1 = ang(1, 0, (x1 - cx1) / Rx, (y1 - cy1) / Ry);
      double dt = ang((x1 - cx1) / Rx, (y1 - cy1) / Ry, (-x1 - cx1) / Rx, (-y1 - cy1) / Ry);
      if (!sweep && dt > 0)
         dt -= 2 * 3.14159265358979;
      else if (sweep && dt < 0)
         dt += 2 * 3.14159265358979;
      const int segs = std::max(1, (int)std::ceil(std::fabs(dt) / (3.14159265358979 / 2) - 1e-6));
      const double step = dt / segs, kk = 4.0 / 3.0 * std::tan(step / 4);
      double t = t1;
      for (int i = 0; i < segs; i++)
      {
         const double ta = t, tb = t + step;
         auto pt = [&](double a, double& px, double& py, double& tx, double& ty) {
            const double ex = Rx * std::cos(a), ey = Ry * std::sin(a);
            px = cs * ex - sn * ey + cx;
            py = sn * ex + cs * ey + cy;
            const double dx2 = -Rx * std::sin(a), dy2 = Ry * std::cos(a);
            tx = cs * dx2 - sn * dy2;
            ty = sn * dx2 + cs * dy2;
         };
         double ax, ay, atx, aty, bx, by, btx, bty;
         pt(ta, ax, ay, atx, aty);
         pt(tb, bx, by, btx, bty);
         if (i == segs - 1)
            bx = x, by = y;
         b.Cubic((float)(ax + kk * atx), (float)(ay + kk * aty), (float)(bx - kk * btx), (float)(by - kk * bty), (float)bx, (float)by);
         t = tb;
      }
   }

   bool ParseD(const std::string& d, std::vector<Shape>& out, std::string* err)
   {
      Builder b{ out };
      const char* p = d.c_str();
      char cmd = 0;
      float cx = 0, cy = 0, sx = 0, sy = 0;     // current point, subpath start
      float lcx = 0, lcy = 0;                   // last control point (S / T reflection)
      char last = 0;
      while (true)
      {
         SkipSep(p);
         if (*p == 0)
            break;
         if (std::isalpha((unsigned char)*p))
            cmd = *p++;
         else if (cmd == 0)
         {
            if (err != nullptr)
               *err = "the path data must start with M";
            return false;
         }
         const bool rel = std::islower((unsigned char)cmd) != 0;
         const char up = (char)std::toupper((unsigned char)cmd);
         float v[7];
         auto need = [&](int n) {
            for (int i = 0; i < n; i++)
               if (!Num(p, v[i]))
                  return false;
            return true;
         };
         bool ok = true;
         if (!b.open && up != 'M' && up != 'Z')
            b.Start(cx, cy);
         switch (up)
         {
            case 'M':
               if ((ok = need(2)))
               {
                  cx = rel ? cx + v[0] : v[0];
                  cy = rel ? cy + v[1] : v[1];
                  b.Start(cx, cy);
                  sx = cx, sy = cy;
                  cmd = rel ? 'l' : 'L';   // more pairs after M are line-tos
               }
               break;
            case 'L':
               if ((ok = need(2)))
               {
                  cx = rel ? cx + v[0] : v[0];
                  cy = rel ? cy + v[1] : v[1];
                  b.Line(cx, cy);
               }
               break;
            case 'H':
               if ((ok = need(1)))
               {
                  cx = rel ? cx + v[0] : v[0];
                  b.Line(cx, cy);
               }
               break;
            case 'V':
               if ((ok = need(1)))
               {
                  cy = rel ? cy + v[0] : v[0];
                  b.Line(cx, cy);
               }
               break;
            case 'C':
               if ((ok = need(6)))
               {
                  const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                  b.Cubic(v[0] + ox, v[1] + oy, v[2] + ox, v[3] + oy, v[4] + ox, v[5] + oy);
                  lcx = v[2] + ox, lcy = v[3] + oy;
                  cx = v[4] + ox, cy = v[5] + oy;
               }
               break;
            case 'S':
               if ((ok = need(4)))
               {
                  const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                  const bool prev = last == 'C' || last == 'S';
                  const float c1x = prev ? 2 * cx - lcx : cx, c1y = prev ? 2 * cy - lcy : cy;
                  b.Cubic(c1x, c1y, v[0] + ox, v[1] + oy, v[2] + ox, v[3] + oy);
                  lcx = v[0] + ox, lcy = v[1] + oy;
                  cx = v[2] + ox, cy = v[3] + oy;
               }
               break;
            case 'Q':
            case 'T':
               if ((ok = need(up == 'Q' ? 4 : 2)))
               {
                  const float ox = rel ? cx : 0, oy = rel ? cy : 0;
                  float qx, qy, x, y;
                  if (up == 'Q')
                     qx = v[0] + ox, qy = v[1] + oy, x = v[2] + ox, y = v[3] + oy;
                  else
                  {
                     const bool prev = last == 'Q' || last == 'T';
                     qx = prev ? 2 * cx - lcx : cx, qy = prev ? 2 * cy - lcy : cy;
                     x = v[0] + ox, y = v[1] + oy;
                  }
                  b.Cubic(cx + (qx - cx) * 2 / 3, cy + (qy - cy) * 2 / 3, x + (qx - x) * 2 / 3, y + (qy - y) * 2 / 3, x, y);
                  lcx = qx, lcy = qy;
                  cx = x, cy = y;
               }
               break;
            case 'A':
            {
               bool large = false, sweep = false;
               ok = Num(p, v[0]) && Num(p, v[1]) && Num(p, v[2]) && Flag(p, large) && Flag(p, sweep) && Num(p, v[5]) && Num(p, v[6]);
               if (ok)
               {
                  const float x = rel ? cx + v[5] : v[5], y = rel ? cy + v[6] : v[6];
                  Arc(b, cx, cy, v[0], v[1], v[2], large, sweep, x, y);
                  cx = x, cy = y;
               }
               break;
            }
            case 'Z':
               b.Close();
               cx = sx, cy = sy;
               // A command after Z without an M starts at the subpath's start.
               break;
            default:
               if (err != nullptr)
                  *err = std::string("unknown path command '") + cmd + "'";
               return false;
         }
         if (!ok)
         {
            if (err != nullptr)
               *err = std::string("bad numbers after '") + cmd + "'";
            return false;
         }
         last = up;
         if (up == 'Z')
         {
            // Z takes no numbers: the next letter must come.
            SkipSep(p);
            if (*p != 0 && !std::isalpha((unsigned char)*p))
            {
               if (err != nullptr)
                  *err = "numbers after Z";
               return false;
            }
         }
      }
      b.Flush();
      return true;
   }

   // ---- the element scanner --------------------------------------------------------------------------------
   struct Tag
   {
      std::string name;
      std::vector<std::pair<std::string, std::string>> attrs;
      bool closing = false, selfClosing = false;
      const std::string* Get(const char* k) const
      {
         for (const auto& a : attrs)
            if (a.first == k)
               return &a.second;
         return nullptr;
      }
   };

   bool NextTag(const std::string& s, size_t& at, Tag& t)
   {
      while (true)
      {
         const size_t lt = s.find('<', at);
         if (lt == std::string::npos)
            return false;
         if (s.compare(lt, 4, "<!--") == 0)
         {
            const size_t e = s.find("-->", lt);
            at = e == std::string::npos ? s.size() : e + 3;
            continue;
         }
         if (s.compare(lt, 2, "<?") == 0 || s.compare(lt, 2, "<!") == 0)
         {
            const size_t e = s.find('>', lt);
            at = e == std::string::npos ? s.size() : e + 1;
            continue;
         }
         size_t p = lt + 1;
         t = Tag();
         if (p < s.size() && s[p] == '/')
         {
            t.closing = true;
            p++;
         }
         const size_t n0 = p;
         while (p < s.size() && !std::isspace((unsigned char)s[p]) && s[p] != '>' && s[p] != '/')
            p++;
         t.name = s.substr(n0, p - n0);
         if (const size_t colon = t.name.find(':'); colon != std::string::npos)
            t.name = t.name.substr(colon + 1);
         while (p < s.size())
         {
            while (p < s.size() && std::isspace((unsigned char)s[p]))
               p++;
            if (p >= s.size())
               return false;
            if (s[p] == '>')
            {
               p++;
               break;
            }
            if (s[p] == '/')
            {
               t.selfClosing = true;
               p++;
               continue;
            }
            const size_t k0 = p;
            while (p < s.size() && s[p] != '=' && !std::isspace((unsigned char)s[p]) && s[p] != '>' && s[p] != '/')
               p++;
            std::string key = s.substr(k0, p - k0);
            while (p < s.size() && std::isspace((unsigned char)s[p]))
               p++;
            std::string val;
            if (p < s.size() && s[p] == '=')
            {
               p++;
               while (p < s.size() && std::isspace((unsigned char)s[p]))
                  p++;
               if (p < s.size() && (s[p] == '"' || s[p] == '\''))
               {
                  const char q = s[p++];
                  const size_t e = s.find(q, p);
                  if (e == std::string::npos)
                     return false;
                  val = s.substr(p, e - p);
                  p = e + 1;
               }
            }
            if (!key.empty())
               t.attrs.push_back({ key, val });
         }
         at = p;
         return true;
      }
   }

   float Attr(const Tag& t, const char* k, float dflt = 0.0f)
   {
      const std::string* v = t.Get(k);
      if (v == nullptr)
         return dflt;
      const char* p = v->c_str();
      float f = dflt;
      return Num(p, f) ? f : dflt;
   }

   void ApplyStyle(Style& st, const std::string& key, const std::string& val, int& unknown)
   {
      const std::string v = Trim(val);
      auto num = [&](float dflt) { const char* p = v.c_str(); float f = dflt; return Num(p, f) ? f : dflt; };
      bool ok = true;
      if (key == "fill")
      {
         st.fillOn = Colour(v, st.fill, ok);
         unknown += ok ? 0 : 1;
      }
      else if (key == "stroke")
      {
         st.strokeOn = Colour(v, st.stroke, ok);
         unknown += ok ? 0 : 1;
      }
      else if (key == "stroke-width")
         st.width = std::max(0.0f, num(1.0f));
      else if (key == "opacity")
         st.opacity *= std::clamp(num(1.0f), 0.0f, 1.0f);
      else if (key == "fill-opacity")
         st.fillOp = std::clamp(num(1.0f), 0.0f, 1.0f);
      else if (key == "stroke-opacity")
         st.strokeOp = std::clamp(num(1.0f), 0.0f, 1.0f);
      else if (key == "stroke-linecap")
         st.cap = v == "round" ? 1 : v == "square" ? 2 : 0;
      else if (key == "stroke-linejoin")
         st.join = v == "round" ? 1 : v == "bevel" ? 2 : 0;
   }

   void TagStyle(const Tag& t, Style& st, int& unknown)
   {
      for (const auto& a : t.attrs)
         ApplyStyle(st, a.first, a.second, unknown);
      if (const std::string* s = t.Get("style"))
      {
         size_t at = 0;
         while (at < s->size())
         {
            size_t semi = s->find(';', at);
            if (semi == std::string::npos)
               semi = s->size();
            const std::string decl = s->substr(at, semi - at);
            const size_t colon = decl.find(':');
            if (colon != std::string::npos)
               ApplyStyle(st, Trim(decl.substr(0, colon)), decl.substr(colon + 1), unknown);
            at = semi + 1;
         }
      }
   }

   void Ellipse(std::vector<Shape>& out, float cx, float cy, float rx, float ry)
   {
      const float k = 0.5522847f;
      Shape s;
      s.type = Shape::kPath;
      s.closed = true;
      const float px[4] = { cx + rx, cx, cx - rx, cx }, py[4] = { cy, cy + ry, cy, cy - ry };
      const float tx[4] = { 0, -rx, 0, rx }, ty[4] = { ry, 0, -ry, 0 };   // direction of travel at each anchor
      for (int i = 0; i < 4; i++)
         s.pts.insert(s.pts.end(), { px[i], py[i], px[i] - tx[i] * k, py[i] - ty[i] * k, px[i] + tx[i] * k, py[i] + ty[i] * k });
      out.push_back(s);
   }

   void Poly(std::vector<Shape>& out, const std::string& pts, bool closed)
   {
      Shape s;
      s.type = Shape::kPath;
      s.closed = closed;
      const char* p = pts.c_str();
      float x, y;
      while (Num(p, x) && Num(p, y))
         s.pts.insert(s.pts.end(), { x, y, x, y, x, y });
      if (s.pts.size() >= 12)
         out.push_back(s);
   }

   void RoundRect(std::vector<Shape>& out, float x, float y, float w, float h, float rx, float ry)
   {
      if (w <= 0 || h <= 0)
         return;
      rx = std::min(rx, w * 0.5f);
      ry = std::min(ry, h * 0.5f);
      if (rx <= 0 || ry <= 0)
      {
         Poly(out, std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(x + w) + " " + std::to_string(y) + " " +
                      std::to_string(x + w) + " " + std::to_string(y + h) + " " + std::to_string(x) + " " + std::to_string(y + h),
              true);
         return;
      }
      std::vector<Shape> tmp;
      Builder b{ tmp };
      b.Start(x + rx, y);
      b.Line(x + w - rx, y);
      Arc(b, x + w - rx, y, rx, ry, 0, false, true, x + w, y + ry);
      b.Line(x + w, y + h - ry);
      Arc(b, x + w, y + h - ry, rx, ry, 0, false, true, x + w - rx, y + h);
      b.Line(x + rx, y + h);
      Arc(b, x + rx, y + h, rx, ry, 0, false, true, x, y + h - ry);
      b.Line(x, y + ry);
      Arc(b, x, y + ry, rx, ry, 0, false, true, x + rx, y);
      b.Close();
      out.insert(out.end(), tmp.begin(), tmp.end());
   }
}

bool Scene2DNode::PathDataToShapes(const std::string& d, std::vector<Shape>& out, std::string* err)
{
   out.clear();
   if (!ParseD(d, out, err))
      return false;
   if (out.empty())
   {
      if (err != nullptr)
         *err = "the path draws nothing (it needs at least two points)";
      return false;
   }
   return true;
}

bool Scene2DNode::SvgToItems(const std::string& svg, std::vector<SvgItem>& out, float& w, float& h, std::string* err, int* skipped)
{
   out.clear();
   w = h = 0;
   int skip = 0, unknownColour = 0;
   struct Frame { Mat m; Style st; std::string name; bool skip; };
   std::vector<Frame> stack;
   stack.push_back({ Mat(), Style(), std::string(), false });
   size_t at = 0;
   Tag t;
   bool sawSvg = false;
   static const char* kSkipBlocks[] = { "defs", "clipPath", "mask", "pattern", "symbol", "linearGradient", "radialGradient",
                                        "style", "title", "desc", "metadata", "text", "marker", "filter" };
   while (NextTag(svg, at, t))
   {
      const bool block = std::find_if(std::begin(kSkipBlocks), std::end(kSkipBlocks), [&](const char* n) { return t.name == n; }) !=
                         std::end(kSkipBlocks);
      if (t.closing)
      {
         if ((t.name == "g" || t.name == "svg" || t.name == "a" || block) && stack.size() > 1)
            stack.pop_back();
         continue;
      }
      Frame f = stack.back();
      if (t.name == "svg" && !sawSvg)
      {
         sawSvg = true;
         float vb[4] = { 0, 0, 0, 0 };
         bool hasVb = false;
         if (const std::string* v = t.Get("viewBox"))
         {
            const char* p = v->c_str();
            hasVb = Num(p, vb[0]) && Num(p, vb[1]) && Num(p, vb[2]) && Num(p, vb[3]) && vb[2] > 0 && vb[3] > 0;
         }
         w = Attr(t, "width", hasVb ? vb[2] : 0.0f);
         h = Attr(t, "height", hasVb ? vb[3] : 0.0f);
         if (const std::string* v = t.Get("width"); v != nullptr && v->find('%') != std::string::npos)
            w = hasVb ? vb[2] : 0.0f;
         if (const std::string* v = t.Get("height"); v != nullptr && v->find('%') != std::string::npos)
            h = hasVb ? vb[3] : 0.0f;
         if (hasVb && w > 0 && h > 0)
         {
            // preserveAspectRatio's default: xMidYMid meet.
            const float k = std::min(w / vb[2], h / vb[3]);
            Mat vm = { k, 0, 0, k, (w - vb[2] * k) * 0.5f - vb[0] * k, (h - vb[3] * k) * 0.5f - vb[1] * k };
            f.m = f.m * vm;
         }
         TagStyle(t, f.st, unknownColour);
         if (!t.selfClosing)
            stack.push_back(f);
         continue;
      }
      if (block)
      {
         if (t.name != "title" && t.name != "desc" && t.name != "metadata" && t.name != "style" && t.name != "defs")
            skip++;
         if (!t.selfClosing)
         {
            f.skip = true;
            stack.push_back(f);
         }
         continue;
      }
      if (const std::string* tr = t.Get("transform"))
         f.m = f.m * Transform(*tr);
      TagStyle(t, f.st, unknownColour);
      if (t.name == "g" || t.name == "a")
      {
         if (const std::string* id = t.Get("id"))
            f.name = *id;
         if (!t.selfClosing)
            stack.push_back(f);
         continue;
      }
      if (f.skip)
         continue;
      std::vector<Shape> shapes;
      if (t.name == "path")
      {
         std::string e;
         if (const std::string* d = t.Get("d"))
            if (!ParseD(*d, shapes, &e) && shapes.empty())
               skip++;
      }
      else if (t.name == "rect")
      {
         float rx = Attr(t, "rx", -1), ry = Attr(t, "ry", -1);
         if (rx < 0)
            rx = ry;
         if (ry < 0)
            ry = rx;
         RoundRect(shapes, Attr(t, "x"), Attr(t, "y"), Attr(t, "width"), Attr(t, "height"), std::max(0.0f, rx), std::max(0.0f, ry));
      }
      else if (t.name == "circle")
      {
         const float r = Attr(t, "r");
         if (r > 0)
            Ellipse(shapes, Attr(t, "cx"), Attr(t, "cy"), r, r);
      }
      else if (t.name == "ellipse")
      {
         const float rx = Attr(t, "rx"), ry = Attr(t, "ry");
         if (rx > 0 && ry > 0)
            Ellipse(shapes, Attr(t, "cx"), Attr(t, "cy"), rx, ry);
      }
      else if (t.name == "line")
      {
         const float x1 = Attr(t, "x1"), y1 = Attr(t, "y1"), x2 = Attr(t, "x2"), y2 = Attr(t, "y2");
         Shape s;
         s.type = Shape::kPath;
         s.pts = { x1, y1, x1, y1, x1, y1, x2, y2, x2, y2, x2, y2 };
         shapes.push_back(s);
         // A line has no inside: unless it says otherwise it is stroked, never filled.
         f.st.fillOn = false;
      }
      else if (t.name == "polyline" || t.name == "polygon")
      {
         if (const std::string* p = t.Get("points"))
            Poly(shapes, *p, t.name == "polygon");
         if (t.name == "polyline")
            f.st.fillOn = f.st.fillOn && t.Get("fill") != nullptr;
      }
      else
      {
         if (t.name != "use" && t.name != "image" && t.name != "tspan")
            continue;
         skip++;
         if (!t.selfClosing)
         {
            f.skip = true;
            stack.push_back(f);
         }
         continue;
      }
      if (shapes.empty())
         continue;
      SvgItem item;
      if (const std::string* id = t.Get("id"))
         item.name = *id;
      else
         item.name = f.name.empty() ? t.name : f.name;
      const float sc = f.m.Scale();
      for (size_t i = 0; i < shapes.size(); i++)
      {
         Shape& s = shapes[i];
         for (size_t k = 0; k + 1 < s.pts.size(); k += 2)
            f.m.Apply(s.pts[k], s.pts[k + 1], s.pts[k], s.pts[k + 1]);
         s.op = i == 0 ? Shape::kAdd : Shape::kXor;   // later subpaths cut holes (even-odd)
         s.fillOn = f.st.fillOn && s.closed;
         if (f.st.fillOn && !s.closed && t.name == "path")
            s.fillOn = true;   // an open path still fills to its chord in SVG
         std::copy(f.st.fill, f.st.fill + 3, s.fillC);
         s.fillC[3] = f.st.opacity * f.st.fillOp;
         s.strokeOn = f.st.strokeOn && f.st.width > 0;
         std::copy(f.st.stroke, f.st.stroke + 3, s.strokeC);
         s.strokeC[3] = f.st.opacity * f.st.strokeOp;
         s.width = f.st.width * (sc > 0 ? sc : 1.0f);
         s.cap = f.st.cap;
         s.join = f.st.join;
      }
      item.shapes = std::move(shapes);
      out.push_back(std::move(item));
   }
   if (skipped != nullptr)
      *skipped = skip;
   if (!sawSvg)
   {
      if (err != nullptr)
         *err = "no <svg> element";
      return false;
   }
   if (out.empty())
   {
      if (err != nullptr)
         *err = "nothing drawable (path, rect, circle, ellipse, line, polyline, polygon)";
      return false;
   }
   (void)unknownColour;
   return true;
}

int Scene2DNode::AddPathLayer(const std::vector<Shape>& docShapes, int above, const std::string& name)
{
   Layers();
   if (docShapes.empty())
      return 0;
   const int id = AddVectorLayer(above);
   Layer* l = FindLayer(id);
   if (l == nullptr)
      return 0;
   for (Shape s : docShapes)
   {
      for (size_t k = 0; k + 1 < s.pts.size(); k += 2)
      {
         float bx, by;
         DocToBox(*l, s.pts[k], s.pts[k + 1], bx, by);
         s.pts[k] = bx;
         s.pts[k + 1] = by;
      }
      l->shapes.push_back(std::move(s));
   }
   if (!name.empty())
      l->name = name;
   Commit();
   return id;
}

int Scene2DNode::ImportSvgEditable(const std::string& svg, int above, float x, float y, float scale, std::string* err, int* skipped)
{
   std::vector<SvgItem> items;
   float w = 0, h = 0;
   if (!SvgToItems(svg, items, w, h, err, skipped))
      return 0;
   if (!(scale > 0.0f) || !std::isfinite(scale) || !std::isfinite(x) || !std::isfinite(y))
   {
      if (err != nullptr)
         *err = "scale must be > 0 and the place finite";
      return 0;
   }
   std::vector<int> ids;
   int prev = above;
   for (SvgItem& it : items)
   {
      for (Shape& s : it.shapes)
      {
         for (size_t k = 0; k + 1 < s.pts.size(); k += 2)
         {
            s.pts[k] = x + s.pts[k] * scale;
            s.pts[k + 1] = y + s.pts[k + 1] * scale;
         }
         s.width *= scale;
      }
      const int id = AddPathLayer(it.shapes, prev, it.name);
      if (id == 0)
         continue;
      ids.push_back(id);
      prev = id;
   }
   if (ids.empty())
      return 0;
   if (ids.size() == 1)
      return ids[0];
   const int g = GroupLayers(ids);
   if (g != 0)
      if (Layer* gl = FindLayer(g))
         gl->name = "SVG";
   Commit();
   return g != 0 ? g : ids.back();
}

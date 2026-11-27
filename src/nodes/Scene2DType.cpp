// Scene2DType.cpp - type on a path, type inside a path (area type), vertical type and Warp Text for the 2D View's
// text layers, plus the warp styles on a paint layer. The letters come from the OS as outlines
// (Platform::GetTextOutlines, cap height = 1, y up) and are laid out here, one glyph at a time, so every placement is
// a rigid move of the font's own curves: the raster, Convert to Shape and Make Work Path all draw the same anchors.
#include "Scene2DNode.h"

#include "Platform.h"
#include "plutovg.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
   using Anchors = std::vector<float>;   // per anchor x y inX inY outX outY
   constexpr float kPi = 3.14159265358979f;

   // A glyph: the contours whose x ranges overlap (a letter with its dot or accent), in cap units, y up.
   struct Glyph
   {
      std::vector<Anchors> c;
      float x0 = 0.0f, x1 = 0.0f;
   };

   Anchors ContourAnchors(const Platform::TextContour& c)
   {
      if (c.curve.size() >= 12)
         return c.curve;
      Anchors a;
      for (size_t i = 0; i + 1 < c.points.size(); i += 2)
         a.insert(a.end(), { c.points[i], c.points[i + 1], c.points[i], c.points[i + 1], c.points[i], c.points[i + 1] });
      return a;
   }

   bool Outlines(const std::string& text, const std::string& font, float letter, std::vector<Platform::TextContour>& out)
   {
      out.clear();
      std::string err;
      float base = 0.0f;
      if (text.empty() || !Platform::GetTextOutlines(text, font, letter, out, err, &base) || out.empty())
         return false;
      // The OS centres the outlines on their ink; type is laid on its baseline, so put the baseline back at 0.
      for (Platform::TextContour& c : out)
      {
         for (size_t i = 1; i < c.points.size(); i += 2)
            c.points[i] -= base;
         for (size_t i = 1; i < c.curve.size(); i += 2)
            c.curve[i] -= base;
      }
      return true;
   }

   std::vector<Glyph> Clusters(const std::vector<Platform::TextContour>& cs)
   {
      struct Piece { float x0, x1; size_t i; };
      std::vector<Piece> pieces;
      for (size_t i = 0; i < cs.size(); i++)
      {
         if (cs[i].points.size() < 6)
            continue;
         float x0 = 1e30f, x1 = -1e30f;
         for (size_t k = 0; k + 1 < cs[i].points.size(); k += 2)
         {
            x0 = std::min(x0, cs[i].points[k]);
            x1 = std::max(x1, cs[i].points[k]);
         }
         pieces.push_back({ x0, x1, i });
      }
      std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.x0 < b.x0; });
      std::vector<Glyph> out;
      for (const Piece& p : pieces)
      {
         if (out.empty() || p.x0 >= out.back().x1 - 1e-4f)
         {
            out.emplace_back();
            out.back().x0 = p.x0;
            out.back().x1 = p.x1;
         }
         out.back().x1 = std::max(out.back().x1, p.x1);
         out.back().c.push_back(ContourAnchors(cs[p.i]));
      }
      return out;
   }

   void InkX(const std::vector<Platform::TextContour>& cs, float& x0, float& x1)
   {
      x0 = 1e30f;
      x1 = -1e30f;
      for (const auto& c : cs)
         for (size_t i = 0; i + 1 < c.points.size(); i += 2)
         {
            x0 = std::min(x0, c.points[i]);
            x1 = std::max(x1, c.points[i]);
         }
      if (x1 < x0)
         x0 = x1 = 0.0f;
   }

   // Every point of the anchors (handles too) through f; a rigid f keeps the curves exact.
   template <class F> void MapAnchors(Anchors& a, F f)
   {
      for (size_t i = 0; i + 1 < a.size(); i += 2)
         f(a[i], a[i + 1]);
   }

   bool AllFinite(const std::vector<Scene2DNode::Shape>& subs, size_t& anchors)
   {
      anchors = 0;
      for (const auto& s : subs)
      {
         anchors += s.pts.size() / 6;
         for (float v : s.pts)
            if (!std::isfinite(v) || std::fabs(v) > Scene2DNode::kPathCoordMax)
               return false;
      }
      return true;
   }

   // Spans inside the outlines (even-odd) along the row y.
   std::vector<std::pair<float, float>> Spans(const std::vector<std::vector<float>>& lines, float y)
   {
      std::vector<float> xs;
      for (const auto& p : lines)
      {
         const size_t n = p.size() / 2;
         for (size_t i = 0; i < n; i++)
         {
            const float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[(i + 1) % n * 2], by = p[(i + 1) % n * 2 + 1];
            if ((ay <= y) != (by <= y))
               xs.push_back(ax + (y - ay) / (by - ay) * (bx - ax));
         }
      }
      std::sort(xs.begin(), xs.end());
      std::vector<std::pair<float, float>> out;
      for (size_t i = 0; i + 1 < xs.size(); i += 2)
         if (xs[i + 1] > xs[i])
            out.push_back({ xs[i], xs[i + 1] });
      return out;
   }

   std::vector<std::pair<float, float>> Intersect(const std::vector<std::pair<float, float>>& a, const std::vector<std::pair<float, float>>& b)
   {
      std::vector<std::pair<float, float>> out;
      for (const auto& p : a)
         for (const auto& q : b)
         {
            const float x0 = std::max(p.first, q.first), x1 = std::min(p.second, q.second);
            if (x1 > x0)
               out.push_back({ x0, x1 });
         }
      std::sort(out.begin(), out.end());
      return out;
   }

   std::vector<std::string> Codepoints(const std::string& s)
   {
      std::vector<std::string> out;
      for (size_t i = 0; i < s.size();)
      {
         const unsigned char c = (unsigned char)s[i];
         const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
         out.push_back(s.substr(i, n));
         i += n;
      }
      return out;
   }

   void DrawAnchors(plutovg_canvas_t* cv, const Anchors& a, float ox, float oy)
   {
      const size_t n = a.size() / 6;
      if (n < 2)
         return;
      plutovg_canvas_move_to(cv, a[0] + ox, a[1] + oy);
      for (size_t i = 1; i <= n; i++)
      {
         const float* p = &a[(i - 1) * 6];
         const float* q = &a[(i % n) * 6];
         plutovg_canvas_cubic_to(cv, p[4] + ox, p[5] + oy, q[2] + ox, q[3] + oy, q[0] + ox, q[1] + oy);
      }
      plutovg_canvas_close_path(cv);
   }
}

const char* Scene2DNode::WarpStyleName(int style)
{
   static const char* names[kWarpStyleCount] = { "None",  "Arc",   "Arc Lower", "Arc Upper", "Arch",    "Bulge",
                                                  "Shell Lower", "Shell Upper", "Flag", "Wave", "Fish", "Rise",
                                                  "Fisheye", "Inflate", "Squeeze", "Twist", "Cylinder" };
   return style >= 0 && style < kWarpStyleCount ? names[style] : "None";
}

bool Scene2DNode::WarpStyleMap(int style, float bend, float hd, float vd, bool vert, float& u, float& v, float aspect)
{
   if (style <= 0 || style >= kWarpStyleCount)
      return false;
   const float b = std::clamp(bend / 100.0f, -1.0f, 1.0f), h = std::clamp(hd / 100.0f, -1.0f, 1.0f),
               d = std::clamp(vd / 100.0f, -1.0f, 1.0f);
   float a = std::clamp(aspect, 0.01f, 100.0f);
   if (vert)
   {
      std::swap(u, v);
      a = 1.0f / a;
   }
   float x = u, up = -v;   // up: y up, half heights
   switch (style)
   {
      case 1:   // Arc: the box bent round a circle, the centre line keeping its length
      {
         const float th = b * kPi * 0.5f;
         if (std::fabs(th) > 1e-4f)
         {
            const float R0 = 1.0f / th, r = R0 + up * a, ph = x * th;
            x = r * std::sin(ph);
            up = (r * std::cos(ph) - R0) / a;
         }
         break;
      }
      case 2: up -= b * (1.0f - x * x) * (1.0f - up) * 0.5f; break;   // Arc Lower: the bottom edge bows
      case 3: up += b * (1.0f - x * x) * (1.0f + up) * 0.5f; break;   // Arc Upper: the top edge bows
      case 4: up += b * (1.0f - x * x); break;                         // Arch: all of it bows
      case 5: up *= 1.0f + b * (1.0f - x * x) * 0.5f; break;          // Bulge: both edges out
      case 6: up += b * (1.0f - x * x) * (1.0f - up) * 0.5f; break;   // Shell Lower: the bottom edge in
      case 7: up -= b * (1.0f - x * x) * (1.0f + up) * 0.5f; break;   // Shell Upper: the top edge in
      case 8: up += b * 0.5f * std::sin(kPi * x); break;               // Flag
      case 9: up += b * 0.5f * std::sin(kPi * x + up * kPi * 0.25f); break;   // Wave: the rows out of phase
      case 10: up = up * (1.0f + b * 0.5f * std::cos(kPi * x * 0.5f)) + b * 0.25f * std::sin(kPi * x); break;   // Fish
      case 11: up += b * 0.5f * x; break;                              // Rise
      case 12:   // Fisheye: the middle swells
      {
         const float r2 = x * x + up * a * up * a, s = 1.0f + b * 0.5f * std::max(0.0f, 1.0f - r2);
         x *= s;
         up *= s;
         break;
      }
      case 13:   // Inflate
      {
         const float nx = x * (1.0f + b * 0.25f * (1.0f - up * up)), nu = up * (1.0f + b * 0.5f * (1.0f - x * x));
         x = nx;
         up = nu;
         break;
      }
      case 14:   // Squeeze: narrow in the middle, taller there
      {
         const float nx = x * (1.0f - b * 0.3f * (1.0f - up * up)), nu = up * (1.0f + b * 0.5f * (1.0f - x * x));
         x = nx;
         up = nu;
         break;
      }
      case 15:   // Twist: turns most at the centre
      {
         const float y = up * a, r = std::sqrt(x * x + y * y), t = b * kPi * 0.5f * std::max(0.0f, 1.0f - r);
         const float c = std::cos(t), s = std::sin(t);
         const float nx = x * c - y * s, ny = x * s + y * c;
         x = nx;
         up = ny / a;
         break;
      }
      case 16:   // Cylinder: the box wrapped round a drum seen from the front
      {
         const float th = std::max(1e-3f, std::fabs(b)) * kPi * 0.5f;
         const float ph = x * th;
         x = std::sin(ph) / std::sin(th);
         up *= 1.0f + (b >= 0.0f ? 1.0f : -1.0f) * 0.15f * (1.0f - std::cos(ph)) / std::max(1e-3f, 1.0f - std::cos(th));
         break;
      }
      default: break;
   }
   // Horizontal / vertical distortion: perspective along the box.
   up *= 1.0f + h * x * 0.5f;
   x *= 1.0f + d * up * 0.5f;
   u = x;
   v = -up;
   if (vert)
      std::swap(u, v);
   return true;
}

bool Scene2DNode::TypeGlyphAnchors(const Layer& l, std::vector<std::vector<float>>& out)
{
   out.clear();
   if (l.kind != Kind::Text || l.text.empty())
      return false;
   const std::string text = l.caps ? UpperText(l.text) : l.text;
   const float size = std::clamp(l.fontSize, 1.0f, 2000.0f);
   const float k = size * 0.7f;   // px per cap height
   const float letter = l.tracking / size;
   const float shear = l.italic ? 0.21f : 0.0f;
   auto italic = [&](Anchors& a) { MapAnchors(a, [&](float& x, float& y) { x += y * shear; }); };

   if (l.typeMode == 1)
   {
      // Type on a path: one line, each glyph standing on the path at its centre's arc length, turned to the tangent.
      if (l.typePath.empty())
         return false;
      std::vector<std::vector<float>> lines;
      Outline(l.typePath[0], lines);
      if (lines.empty() || lines[0].size() < 4)
         return false;
      std::vector<float> P = lines[0];
      const bool closed = l.typePath[0].closed;
      if (closed)
         P.insert(P.end(), { P[0], P[1] });
      if (l.pathFlip)
         for (size_t i = 0, j = P.size() / 2 - 1; i < j; i++, j--)
         {
            std::swap(P[i * 2], P[j * 2]);
            std::swap(P[i * 2 + 1], P[j * 2 + 1]);
         }
      const size_t n = P.size() / 2;
      std::vector<float> cum(n, 0.0f);
      for (size_t i = 1; i < n; i++)
         cum[i] = cum[i - 1] + std::hypot(P[i * 2] - P[i * 2 - 2], P[i * 2 + 1] - P[i * 2 - 1]);
      const float L = cum.back();
      if (!(L > 1.0f))
         return false;
      std::string row = text;
      std::replace(row.begin(), row.end(), '\n', ' ');
      std::vector<Platform::TextContour> cs;
      if (!Outlines(row, l.font, letter, cs))
         return false;
      const std::vector<Glyph> glyphs = Clusters(cs);
      float lx0 = 0.0f, lx1 = 0.0f;
      InkX(cs, lx0, lx1);
      const float wide = (lx1 - lx0) * k;
      float off = l.pathOffset - std::floor(l.pathOffset);
      // An end point on an open path: the text sits (and aligns) between start and end; past the end is hidden.
      const float stop = !closed && l.pathEnd >= 0.0f ? std::clamp(l.pathEnd, 0.0f, 1.0f) * L : L;
      const float room = std::max(0.0f, stop - (closed ? 0.0f : std::min(off, 1.0f) * L));
      float start = off * L + (l.align == 1 ? (room - wide) * 0.5f : l.align == 2 ? room - wide : 0.0f);
      for (const Glyph& g : glyphs)
      {
         const float cx = (g.x0 + g.x1) * 0.5f;
         float s = start + (cx - lx0) * k;
         if (closed)
            s = std::fmod(std::fmod(s, L) + L, L);
         else if (s < 0.0f || s > stop)
            continue;   // overflow past the ends is hidden
         size_t i = (size_t)(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin());
         i = std::clamp<size_t>(i, 1, n - 1);
         const float seg = std::max(1e-6f, cum[i] - cum[i - 1]), t = (s - cum[i - 1]) / seg;
         const float ax = P[i * 2 - 2], ay = P[i * 2 - 1], bx = P[i * 2], by = P[i * 2 + 1];
         const float px = ax + (bx - ax) * t, py = ay + (by - ay) * t;
         const float tx = (bx - ax) / seg, ty = (by - ay) / seg;
         const float nx = ty, ny = -tx;   // up from the path (y down)
         const float lift = l.baseShift;
         for (Anchors a : g.c)
         {
            italic(a);
            MapAnchors(a, [&](float& x, float& y)
                       {
                          const float gx = (x - cx) * k, gy = y * k + lift;
                          x = px + tx * gx + nx * gy;
                          y = py + ty * gx + ny * gy;
                       });
            out.push_back(std::move(a));
         }
      }
      return !out.empty();
   }

   if (l.typeMode == 2)
   {
      // Area type: lines at the leading inside the path; each line takes the spans that are inside at its top, middle and
      // bottom, filled word by word; a line too narrow for the next word is passed over; the rest overflows.
      std::vector<std::vector<float>> lines;
      for (const Shape& s : l.typePath)
      {
         std::vector<std::vector<float>> o;
         Outline(s, o);
         for (auto& p : o)
            if (p.size() >= 6)
               lines.push_back(std::move(p));
      }
      if (lines.empty())
         return false;
      float top = 1e30f, bottom = -1e30f;
      for (const auto& p : lines)
         for (size_t i = 1; i < p.size(); i += 2)
         {
            top = std::min(top, p[i]);
            bottom = std::max(bottom, p[i]);
         }
      struct Word { std::vector<Platform::TextContour> c; float x0 = 0, x1 = 0; bool para = false; };
      std::vector<Word> words;
      {
         size_t i = 0;
         bool para = false;
         while (i <= text.size())
         {
            const size_t j = text.find_first_of(" \n", i);
            const std::string w = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
            if (!w.empty())
            {
               Word wd;
               wd.para = para;
               para = false;
               if (Outlines(w, l.font, letter, wd.c))
                  InkX(wd.c, wd.x0, wd.x1);
               words.push_back(std::move(wd));
            }
            if (j == std::string::npos)
               break;
            if (text[j] == '\n')
               para = true;
            i = j + 1;
         }
      }
      if (words.empty())
         return false;
      float space = 0.3f;
      {
         std::vector<Platform::TextContour> a, b;
         float a0, a1, b0, b1;
         if (Outlines("n n", l.font, letter, a) && Outlines("nn", l.font, letter, b))
         {
            InkX(a, a0, a1);
            InkX(b, b0, b1);
            space = std::max(0.1f, (a1 - a0) - (b1 - b0));
         }
      }
      const float cap = k, pitch = std::max(0.5f, l.leading) * 1.43f * k, gap = space * k;
      size_t next = 0;
      float base = top + cap;
      int guard = 0;
      while (next < words.size() && base <= bottom + 1e-3f && guard++ < 100000)
      {
         auto spans = Intersect(Intersect(Spans(lines, base - cap), Spans(lines, base - cap * 0.5f)), Spans(lines, base));
         bool first = true;
         for (const auto& sp : spans)
         {
            if (next >= words.size())
               break;
            if (!first && words[next].para)
               break;
            // Words that fit this span.
            std::vector<size_t> take;
            float used = 0.0f;
            while (next < words.size())
            {
               const Word& w = words[next];
               if (!take.empty() && w.para)
                  break;
               const float ww = (w.x1 - w.x0) * k, need = used + (take.empty() ? 0.0f : gap) + ww;
               if (need > sp.second - sp.first + 1e-3f)
                  break;
               used = need;
               take.push_back(next++);
               first = false;
            }
            if (take.empty())
               continue;
            const float room = sp.second - sp.first - used;
            float x = sp.first + (l.align == 1 ? room * 0.5f : l.align == 2 ? room : 0.0f);
            for (size_t t : take)
            {
               const Word& w = words[t];
               for (const auto& c : w.c)
               {
                  Anchors a = ContourAnchors(c);
                  italic(a);
                  const float ox = x - w.x0 * k;
                  MapAnchors(a, [&](float& px, float& py)
                             {
                                px = ox + px * k;
                                py = base - py * k - l.baseShift;
                             });
                  out.push_back(std::move(a));
               }
               x += (w.x1 - w.x0) * k + gap;
            }
         }
         // A new paragraph waits for the next line.
         if (next < words.size() && words[next].para && !first)
            words[next].para = false;
         base += pitch;
      }
      return !out.empty();
   }

   if (l.vertical)
   {
      // Vertical type: each letter centred in its column, columns right to left, one per line of the text.
      const float em = size * (1.0f + l.tracking / 1000.0f), col = size * std::max(0.5f, l.leading);
      std::map<std::string, std::vector<Platform::TextContour>> cache;
      int c = 0, r = 0;
      for (const std::string& ch : Codepoints(text))
      {
         if (ch == "\n")
         {
            c++;
            r = 0;
            continue;
         }
         if (ch != " ")
         {
            auto it = cache.find(ch);
            if (it == cache.end())
            {
               std::vector<Platform::TextContour> cs;
               Outlines(ch, l.font, 0.0f, cs);
               it = cache.emplace(ch, std::move(cs)).first;
            }
            float x0, x1;
            InkX(it->second, x0, x1);
            const float cx = (x0 + x1) * 0.5f, colX = -(float)c * col, rowY = (float)r * em + (em + k) * 0.5f;
            for (const auto& cc : it->second)
            {
               Anchors a = ContourAnchors(cc);
               italic(a);
               MapAnchors(a, [&](float& px, float& py)
                          {
                             px = colX + (px - cx) * k;
                             py = rowY - py * k - l.baseShift;
                          });
               out.push_back(std::move(a));
            }
         }
         r++;
      }
      if (out.empty())
         return false;
      // About the ink's centre (point text grows from its box).
      float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
      for (const auto& a : out)
         for (size_t i = 0; i + 1 < a.size(); i += 6)
         {
            x0 = std::min(x0, a[i]);
            x1 = std::max(x1, a[i]);
            y0 = std::min(y0, a[i + 1]);
            y1 = std::max(y1, a[i + 1]);
         }
      const float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
      for (auto& a : out)
         MapAnchors(a, [&](float& px, float& py)
                    {
                       px -= mx;
                       py -= my;
                    });
      return true;
   }

   // Point and box text: lines stacked at the leading, aligned, fitted onto the drawn box about its centre (as
   // Convert to Shape lays them).
   int tw = 0, th = 0;
   if (!TextSize(l, tw, th))
      return false;
   if (l.warpStyle != 0)
   {
      tw = std::max(1, mTextBoxes[l.id].gw);
      th = std::max(1, mTextBoxes[l.id].gh);
   }
   std::vector<std::pair<std::vector<Platform::TextContour>, std::pair<float, float>>> rows;
   size_t i = 0;
   float wide = 0.0f;
   while (i <= text.size())
   {
      const size_t j = text.find('\n', i);
      std::vector<Platform::TextContour> cs;
      float x0 = 0, x1 = 0;
      if (Outlines(text.substr(i, j == std::string::npos ? std::string::npos : j - i), l.font, letter, cs))
         InkX(cs, x0, x1);
      wide = std::max(wide, x1 - x0);
      rows.push_back({ std::move(cs), { x0, x1 } });
      if (j == std::string::npos)
         break;
      i = j + 1;
   }
   const float pitch = std::max(0.5f, l.leading) * 1.43f;
   float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
   for (size_t r = 0; r < rows.size(); r++)
   {
      const float w = rows[r].second.second - rows[r].second.first;
      const float shift = l.align == 1 ? (wide - w) * 0.5f : l.align == 2 ? wide - w : 0.0f;
      for (const auto& c : rows[r].first)
      {
         Anchors a = ContourAnchors(c);
         italic(a);
         MapAnchors(a, [&](float& px, float& py)
                    {
                       px = px - rows[r].second.first + shift;
                       py = -py + (float)r * pitch;
                    });
         for (size_t q = 0; q + 1 < a.size(); q += 6)
         {
            lo[0] = std::min(lo[0], a[q]);
            hi[0] = std::max(hi[0], a[q]);
            lo[1] = std::min(lo[1], a[q + 1]);
            hi[1] = std::max(hi[1], a[q + 1]);
         }
         out.push_back(std::move(a));
      }
   }
   if (out.empty() || hi[0] <= lo[0] || hi[1] <= lo[1])
      return false;
   const float s = std::min((float)tw / (hi[0] - lo[0]), (float)th / (hi[1] - lo[1]));
   const float cx = (lo[0] + hi[0]) * 0.5f, cy = (lo[1] + hi[1]) * 0.5f;
   for (auto& a : out)
      MapAnchors(a, [&](float& px, float& py)
                 {
                    px = (px - cx) * s;
                    py = (py - cy) * s;
                 });
   return true;
}

const char* Scene2DNode::TypeAALabel(int mode)
{
   static const char* const kNames[kTypeAACount] = { "Smooth", "None", "Sharp", "Crisp", "Strong" };
   return kNames[std::clamp(mode, 0, (int)kTypeAACount - 1)];
}

// Photoshop's anti-alias methods, on the rasterizer's coverage c (0..1, before the text's own opacity):
// None is in or out at half coverage; Sharp and Crisp steepen the edge ramp (Sharp most); Strong lifts partial
// coverage, so letters read heavier. Smooth is the rasterizer's own edge.
void Scene2DNode::ShapeTypeEdges(int mode, float opacity, std::vector<uint8_t>& rgba)
{
   if (mode <= kTypeAASmooth || mode >= kTypeAACount)
      return;
   const float op = std::clamp(opacity, 1e-4f, 1.0f);
   uint8_t lut[256];
   for (int i = 0; i < 256; i++)
   {
      const float c = std::clamp(i / 255.0f / op, 0.0f, 1.0f);
      float o = c;
      if (mode == kTypeAANone)
         o = c >= 0.5f ? 1.0f : 0.0f;
      else if (mode == kTypeAASharp)
         o = std::clamp((c - 0.5f) * 3.0f + 0.5f, 0.0f, 1.0f);
      else if (mode == kTypeAACrisp)
         o = std::clamp((c - 0.45f) * 1.8f + 0.5f, 0.0f, 1.0f);
      else if (mode == kTypeAAStrong)
         o = std::pow(c, 0.55f);
      lut[i] = (uint8_t)std::lround(std::min(o * op, 1.0f) * 255.0f);
   }
   for (size_t i = 3; i < rgba.size(); i += 4)
      rgba[i] = lut[rgba[i]];
}

void Scene2DNode::GlyphTypeRaster(const Layer& l, TextBox& tb)
{
   std::vector<std::vector<float>> glyphs;
   const float size = std::clamp(l.fontSize, 1.0f, 2000.0f);
   if (!TypeGlyphAnchors(l, glyphs) || glyphs.empty())
   {
      tb.w = tb.h = std::max(8, (int)size);
      tb.rgba.assign((size_t)tb.w * tb.h * 4, 0);
      return;
   }
   // The box is symmetric about the text's anchor (its centre), so the path stays put as the text changes.
   float mx = 1.0f, my = 1.0f;
   for (const auto& a : glyphs)
      for (size_t i = 0; i + 1 < a.size(); i += 2)
      {
         mx = std::max(mx, std::fabs(a[i]));
         my = std::max(my, std::fabs(a[i + 1]));
      }
   const float pad = 2.0f + (l.outline ? l.outlineW : 0.0f);
   tb.w = std::clamp(2 * (int)std::ceil(mx + pad), 8, 8192);
   tb.h = std::clamp(2 * (int)std::ceil(my + pad), 8, 8192);
   std::vector<uint8_t> argb((size_t)tb.w * tb.h * 4, 0);
   plutovg_surface_t* sf = plutovg_surface_create_for_data(argb.data(), tb.w, tb.h, tb.w * 4);
   plutovg_canvas_t* cv = plutovg_canvas_create(sf);
   plutovg_canvas_set_fill_rule(cv, PLUTOVG_FILL_RULE_EVEN_ODD);
   const float ox = tb.w * 0.5f, oy = tb.h * 0.5f;
   for (const auto& a : glyphs)
      DrawAnchors(cv, a, ox, oy);
   plutovg_canvas_set_rgba(cv, l.textC[0], l.textC[1], l.textC[2], std::clamp(l.textC[3], 0.0f, 1.0f));
   if (l.outline && l.outlineW > 0.0f)
   {
      plutovg_canvas_fill_preserve(cv);
      plutovg_canvas_set_rgba(cv, l.outlineC[0], l.outlineC[1], l.outlineC[2], std::clamp(l.outlineC[3], 0.0f, 1.0f));
      plutovg_canvas_set_line_width(cv, l.outlineW);
      plutovg_canvas_stroke(cv);
   }
   else
      plutovg_canvas_fill(cv);
   plutovg_canvas_destroy(cv);
   plutovg_surface_destroy(sf);
   tb.rgba.resize(argb.size());
   plutovg_convert_argb_to_rgba(tb.rgba.data(), argb.data(), tb.w, tb.h, tb.w * 4);
}

void Scene2DNode::WarpTypeRaster(const Layer& l, TextBox& tb)
{
   const int W0 = tb.w, H0 = tb.h;
   if (W0 <= 0 || H0 <= 0 || tb.rgba.empty())
      return;
   const float aspect = (float)H0 / (float)W0;
   constexpr int N = 32;
   std::vector<float> gx((N + 1) * (N + 1)), gy((N + 1) * (N + 1));
   float mx = 1.0f, my = 1.0f;
   for (int j = 0; j <= N; j++)
      for (int i = 0; i <= N; i++)
      {
         float u = -1.0f + 2.0f * i / N, v = -1.0f + 2.0f * j / N;
         WarpStyleMap(l.warpStyle, l.warpBend, l.warpH, l.warpV, l.warpVert, u, v, aspect);
         gx[j * (N + 1) + i] = u * W0 * 0.5f;
         gy[j * (N + 1) + i] = v * H0 * 0.5f;
         mx = std::max(mx, std::fabs(gx[j * (N + 1) + i]));
         my = std::max(my, std::fabs(gy[j * (N + 1) + i]));
      }
   const int W1 = std::clamp(2 * (int)std::ceil(mx + 1.0f), 8, 8192), H1 = std::clamp(2 * (int)std::ceil(my + 1.0f), 8, 8192);
   // Premultiplied source for bilinear sampling.
   std::vector<float> src((size_t)W0 * H0 * 4);
   for (size_t p = 0; p < (size_t)W0 * H0; p++)
   {
      const float a = tb.rgba[p * 4 + 3] / 255.0f;
      for (int c = 0; c < 3; c++)
         src[p * 4 + c] = tb.rgba[p * 4 + c] / 255.0f * a;
      src[p * 4 + 3] = a;
   }
   auto sample = [&](float sx, float sy, float o[4])
   {
      sx -= 0.5f;
      sy -= 0.5f;
      const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
      const float fx = sx - x0, fy = sy - y0;
      for (int c = 0; c < 4; c++)
         o[c] = 0.0f;
      for (int dy = 0; dy < 2; dy++)
         for (int dx = 0; dx < 2; dx++)
         {
            const int x = x0 + dx, y = y0 + dy;
            if (x < 0 || y < 0 || x >= W0 || y >= H0)
               continue;
            const float w = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
            for (int c = 0; c < 4; c++)
               o[c] += src[((size_t)y * W0 + x) * 4 + c] * w;
         }
   };
   std::vector<float> dst((size_t)W1 * H1 * 4, 0.0f);
   auto tri = [&](int a, int b, int c)
   {
      const float ax = gx[a] + W1 * 0.5f, ay = gy[a] + H1 * 0.5f, bx = gx[b] + W1 * 0.5f, by = gy[b] + H1 * 0.5f,
                  cx = gx[c] + W1 * 0.5f, cy = gy[c] + H1 * 0.5f;
      const float den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
      if (std::fabs(den) < 1e-9f)
         return;
      auto srcOf = [&](int k, float& sx, float& sy)
      {
         sx = (float)(k % (N + 1)) / N * W0;
         sy = (float)(k / (N + 1)) / N * H0;
      };
      float sax, say, sbx, sby, scx, scy;
      srcOf(a, sax, say);
      srcOf(b, sbx, sby);
      srcOf(c, scx, scy);
      const int x0 = std::max(0, (int)std::floor(std::min({ ax, bx, cx }))), x1 = std::min(W1 - 1, (int)std::ceil(std::max({ ax, bx, cx })));
      const int y0 = std::max(0, (int)std::floor(std::min({ ay, by, cy }))), y1 = std::min(H1 - 1, (int)std::ceil(std::max({ ay, by, cy })));
      for (int y = y0; y <= y1; y++)
         for (int x = x0; x <= x1; x++)
         {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
            const float w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f)
               continue;
            float o[4];
            sample(w0 * sax + w1 * sbx + w2 * scx, w0 * say + w1 * sby + w2 * scy, o);
            float* d = &dst[((size_t)y * W1 + x) * 4];
            if (o[3] > d[3])
               std::copy(o, o + 4, d);
         }
   };
   for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++)
      {
         const int a = j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
         tri(a, b, d);
         tri(a, d, c);
      }
   tb.w = W1;
   tb.h = H1;
   tb.rgba.assign((size_t)W1 * H1 * 4, 0);
   for (size_t p = 0; p < (size_t)W1 * H1; p++)
   {
      const float a = dst[p * 4 + 3];
      if (a <= 0.0f)
         continue;
      for (int c = 0; c < 3; c++)
         tb.rgba[p * 4 + c] = (uint8_t)std::lround(std::clamp(dst[p * 4 + c] / a, 0.0f, 1.0f) * 255.0f);
      tb.rgba[p * 4 + 3] = (uint8_t)std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f);
   }
}

bool Scene2DNode::TypePathBounds(const Layer& l, float& x0, float& y0, float& x1, float& y1)
{
   x0 = y0 = 1e30f;
   x1 = y1 = -1e30f;
   int w = 0, h = 0;
   TextSize(l, w, h);
   for (const Shape& s : l.typePath)
   {
      std::vector<std::vector<float>> o;
      Outline(s, o);
      for (const auto& p : o)
         for (size_t i = 0; i + 1 < p.size(); i += 2)
         {
            float dx, dy;
            BoxToDoc(l, p[i] + w * 0.5f, p[i + 1] + h * 0.5f, dx, dy);
            x0 = std::min(x0, dx);
            x1 = std::max(x1, dx);
            y0 = std::min(y0, dy);
            y1 = std::max(y1, dy);
         }
   }
   return x1 >= x0 && y1 >= y0;
}

int Scene2DNode::SetTypePath(int id, std::vector<Shape> subs)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text || subs.empty())
      return kTypePathNoPath;
   size_t anchors = 0;
   if (!AllFinite(subs, anchors))
      return kTypePathNotFinite;
   if (anchors > (size_t)kTypePathAnchorsMax)
      return kTypePathTooManyAnchors;
   if (anchors == 0)
      return kTypePathNoPath;
   for (Shape& s : subs)
      s.type = Shape::kPath;
   l->typePath = std::move(subs);
   if (l->typeMode == 0)
      l->typeMode = 1;
   Commit();
   return kTypePathOk;
}

int Scene2DNode::AttachTextToPath(int id, const std::string& path, bool inside)
{
   Layers();
   Layer* l = FindLayer(id);
   const DocPath* p = FindPath(path);
   if (l == nullptr || l->kind != Kind::Text || p == nullptr || p->subs.empty())
      return kTypePathNoPath;
   size_t anchors = 0;
   if (!AllFinite(p->subs, anchors))
      return kTypePathNotFinite;
   if (anchors > (size_t)kTypePathAnchorsMax)
      return kTypePathTooManyAnchors;
   if (anchors == 0 || (inside && !p->subs[0].closed))
      return kTypePathNoPath;
   // The anchor moves to the path's centre (the box is laid about it); the path goes into the text's own px.
   std::vector<std::vector<float>> lines;
   std::vector<int> ops;
   PathPolylines(*p, lines, ops);
   float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
   for (const auto& q : lines)
      for (size_t i = 0; i + 1 < q.size(); i += 2)
      {
         x0 = std::min(x0, q[i]);
         x1 = std::max(x1, q[i]);
         y0 = std::min(y0, q[i + 1]);
         y1 = std::max(y1, q[i + 1]);
      }
   if (!(x1 >= x0 && y1 >= y0))
      return kTypePathNoPath;
   std::vector<Shape> subs = p->subs;
   l->ax = l->ay = 0.5f;
   l->x = (x0 + x1) * 0.5f;
   l->y = (y0 + y1) * 0.5f;
   l->boxW = 0.0f;
   int w = 0, h = 0;
   TextSize(*l, w, h);
   for (Shape& s : subs)
   {
      s.type = Shape::kPath;
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         float bx, by;
         DocToBox(*l, s.pts[i], s.pts[i + 1], bx, by);
         s.pts[i] = bx - w * 0.5f;
         s.pts[i + 1] = by - h * 0.5f;
      }
   }
   l->typePath = std::move(subs);
   l->typeMode = inside ? 2 : 1;
   l->vertical = false;
   Commit();
   return kTypePathOk;
}

int Scene2DNode::AddTypeOnPath(const std::string& path, const std::string& text, bool inside, int above, int* err)
{
   Layers();
   const DocPath* p = FindPath(path);
   int e = kTypePathOk;
   size_t anchors = 0;
   if (p == nullptr || p->subs.empty())
      e = kTypePathNoPath;
   else if (!AllFinite(p->subs, anchors))
      e = kTypePathNotFinite;
   else if (anchors > (size_t)kTypePathAnchorsMax)
      e = kTypePathTooManyAnchors;
   else if (anchors == 0 || (inside && !p->subs[0].closed))
      e = kTypePathNoPath;
   if (err != nullptr)
      *err = e;
   if (e != kTypePathOk || text.empty())
      return 0;
   const std::vector<Shape> subs = p->subs;   // AddLayer may not move paths, but copy before touching the doc
   Layer& l = AddLayer(Kind::Text, 0, above);
   l.text = text;
   l.fontSize = std::clamp(std::round(mDoc.h / 12.0f), 12.0f, 400.0f);
   l.ax = l.ay = 0.5f;
   const int id = l.id;
   // Same as Attach, in one undo step with the new layer.
   std::vector<std::vector<float>> lines;
   std::vector<int> ops;
   DocPath tmp{ path, subs };
   PathPolylines(tmp, lines, ops);
   float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
   for (const auto& q : lines)
      for (size_t i = 0; i + 1 < q.size(); i += 2)
      {
         x0 = std::min(x0, q[i]);
         x1 = std::max(x1, q[i]);
         y0 = std::min(y0, q[i + 1]);
         y1 = std::max(y1, q[i + 1]);
      }
   Layer* nl = FindLayer(id);
   nl->x = (x0 + x1) * 0.5f;
   nl->y = (y0 + y1) * 0.5f;
   nl->typePath = subs;
   for (Shape& s : nl->typePath)
   {
      s.type = Shape::kPath;
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         s.pts[i] -= nl->x;
         s.pts[i + 1] -= nl->y;
      }
   }
   nl->typeMode = inside ? 2 : 1;
   Commit();
   return id;
}

int Scene2DNode::AddTypeInShape(int shapeLayer, const std::string& text, int above, int* err)
{
   Layers();
   const Layer* v = FindConst(shapeLayer);
   if (v == nullptr || v->kind != Kind::Vector)
   {
      if (err != nullptr)
         *err = kTypePathNoPath;
      return 0;
   }
   // Borrow the Work Path for the layer's outline, then put back what was there.
   std::vector<DocPath> keep = mPaths;
   int id = 0;
   if (MakeWorkPathFromLayer(shapeLayer))
      id = AddTypeOnPath(kWorkPath, text, true, above, err);
   else if (err != nullptr)
      *err = kTypePathNoPath;
   mPaths.swap(keep);
   if (id > 0)
      std::fprintf(stderr, "PROBE type in shape %d subpaths %zu\n", id, FindConst(id)->typePath.size());
   Commit();
   return id;
}

bool Scene2DNode::ReleaseTypePath(int id)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text || l->typeMode == 0)
      return false;
   size_t anchors = 0;
   if (!AllFinite(l->typePath, anchors))
      return false;   // a path that is not finite cannot be released onto a box
   if (l->typeMode == 2)
   {
      // Box text over the path's bounds: its top left where the path's was, wrapping at the path's width.
      float lx0 = 1e30f, ly0 = 1e30f, lx1 = -1e30f;
      for (const Shape& s : l->typePath)
      {
         std::vector<std::vector<float>> o;
         Outline(s, o);
         for (const auto& p : o)
            for (size_t i = 0; i + 1 < p.size(); i += 2)
            {
               lx0 = std::min(lx0, p[i]);
               lx1 = std::max(lx1, p[i]);
               ly0 = std::min(ly0, p[i + 1]);
            }
      }
      if (!(lx1 >= lx0))
         return false;
      int w = 0, h = 0;
      TextSize(*l, w, h);
      float dx, dy;
      BoxToDoc(*l, lx0 + w * 0.5f, ly0 + h * 0.5f, dx, dy);
      l->boxW = lx1 - lx0;
      l->ax = l->ay = 0.0f;
      l->x = dx;
      l->y = dy;
   }
   l->typeMode = 0;
   l->typePath.clear();
   l->pathOffset = 0.0f;
   l->pathEnd = -1.0f;
   l->pathFlip = false;
   Commit();
   return true;
}

bool Scene2DNode::SetVerticalType(int id, bool vertical)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text || l->vertical == vertical)
      return false;
   l->vertical = vertical;
   Commit();
   return true;
}

bool Scene2DNode::SetWarpText(int id, int style, float bend, float hd, float vd, bool vert)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text || style < 0 || style >= kWarpStyleCount || !std::isfinite(bend) ||
       !std::isfinite(hd) || !std::isfinite(vd))
      return false;
   // Warp Text has no cylinder: the cylinder warp needs its own values (a paint layer's Warp Style), as in PhotoCraft.
   if (style == kWarpStyleCount - 1)
   {
      std::fprintf(stderr, "PROBE warp text: cylinder refused on type layer %d\n", id);
      return false;
   }
   l->warpStyle = style;
   l->warpBend = std::clamp(bend, -100.0f, 100.0f);
   l->warpH = std::clamp(hd, -100.0f, 100.0f);
   l->warpV = std::clamp(vd, -100.0f, 100.0f);
   l->warpVert = vert;
   Commit();
   return true;
}

bool Scene2DNode::WarpLayerStyle(int id, int style, float bend, float hd, float vd, bool vert)
{
   if (style <= 0 || style >= kWarpStyleCount)
      return false;
   WarpGrid def;
   if (!MeshRest(id, def))
      return false;
   const Layer* l = FindConst(id);
   if (l == nullptr)
      return false;
   // The Default grid's corners: TL TR BR BL.
   const int nx = def.Nx(), ny = def.Ny();
   const float* p = def.pts.data();
   const float c[8] = { p[0], p[1], p[(nx - 1) * 2], p[(nx - 1) * 2 + 1], p[(ny * nx - 1) * 2], p[(ny * nx - 1) * 2 + 1],
                        p[(ny - 1) * nx * 2], p[(ny - 1) * nx * 2 + 1] };
   WarpGrid rest;
   rest.us.clear();
   rest.vs.clear();
   for (int i = 0; i < kWarpLinesMax; i++)
   {
      rest.us.push_back((float)i / (kWarpLinesMax - 1));
      rest.vs.push_back((float)i / (kWarpLinesMax - 1));
   }
   rest.deflt = false;
   WarpGridRest(rest, c);
   const float bw = std::hypot(c[2] - c[0], c[3] - c[1]), bh = std::hypot(c[6] - c[0], c[7] - c[1]);
   const float aspect = bw > 0.0f ? bh / bw : 1.0f;
   WarpGrid moved = rest;
   for (int j = 0; j < kWarpLinesMax; j++)
      for (int i = 0; i < kWarpLinesMax; i++)
      {
         float u = rest.us[i] * 2.0f - 1.0f, v = rest.vs[j] * 2.0f - 1.0f;
         WarpStyleMap(style, bend, hd, vd, vert, u, v, aspect);
         const float s = (u + 1.0f) * 0.5f, t = (v + 1.0f) * 0.5f;
         // Bilinear between the corners (beyond them as well).
         const float topX = c[0] + (c[2] - c[0]) * s, topY = c[1] + (c[3] - c[1]) * s;
         const float botX = c[6] + (c[4] - c[6]) * s, botY = c[7] + (c[5] - c[7]) * s;
         moved.pts[(j * kWarpLinesMax + i) * 2] = topX + (botX - topX) * t;
         moved.pts[(j * kWarpLinesMax + i) * 2 + 1] = topY + (botY - topY) * t;
      }
   const std::vector<std::string> from = l->tiles;
   return MeshWarp(id, from, moved, true, false, &rest);
}

bool Scene2DNode::MakeWorkPathFromText(int id)
{
   Layers();
   const Layer* src = FindConst(id);
   if (src == nullptr || src->kind != Kind::Text)
      return false;
   const Layer l = *src;
   std::vector<std::vector<float>> glyphs;
   if (!TypeGlyphAnchors(l, glyphs) || glyphs.empty())
      return false;
   int w = 0, h = 0;
   TextSize(l, w, h);
   const TextBox& tb = mTextBoxes[id];
   const float W0 = (float)std::max(1, l.warpStyle != 0 ? tb.gw : w), H0 = (float)std::max(1, l.warpStyle != 0 ? tb.gh : h);
   std::vector<Shape> subs;
   for (auto& a : glyphs)
   {
      Shape s;
      s.type = Shape::kPath;
      s.closed = true;
      s.op = subs.empty() ? Shape::kAdd : Shape::kXor;
      s.pts = a;
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         float bx = s.pts[i] + W0 * 0.5f, by = s.pts[i + 1] + H0 * 0.5f;
         if (l.warpStyle != 0)
         {
            float u = bx / W0 * 2.0f - 1.0f, v = by / H0 * 2.0f - 1.0f;
            WarpStyleMap(l.warpStyle, l.warpBend, l.warpH, l.warpV, l.warpVert, u, v, H0 / W0);
            bx = w * 0.5f + u * W0 * 0.5f;
            by = h * 0.5f + v * H0 * 0.5f;
         }
         BoxToDoc(l, bx, by, s.pts[i], s.pts[i + 1]);
      }
      subs.push_back(std::move(s));
   }
   return SetWorkPath(std::move(subs));
}

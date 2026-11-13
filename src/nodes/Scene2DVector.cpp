// Scene 2D, Block C: vector layers (pen paths, live shapes, imported SVG),
// vector masks and text layers. All three raster on the CPU (plutovg, lunasvg,
// the platform text rasterizer) only when their content changes, then upload
// one texture the compositor samples like any other layer.

#include "Scene2DNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "Platform.h"
#include "lunasvg.h"
#include "plutovg.h"

namespace
{
   constexpr float kPi = 3.14159265358979f;

   unsigned long long H(unsigned long long h, unsigned long long v) { return (h ^ v) * 1099511628211ull; }
   unsigned long long HF(unsigned long long h, float f)
   {
      unsigned u = 0;
      std::memcpy(&u, &f, 4);
      return H(h, u);
   }
   unsigned long long HS(unsigned long long h, const std::string& s)
   {
      for (char c : s)
         h = H(h, (unsigned char)c);
      return H(h, s.size());
   }

   unsigned long long HashShape(unsigned long long h, const Scene2DNode::Shape& s)
   {
      h = H(h, (unsigned)s.type);
      for (float f : { s.x0, s.y0, s.x1, s.y1, s.radius, s.inner, s.width, s.dash, s.gap })
         h = HF(h, f);
      for (float f : s.fillC) h = HF(h, f);
      for (float f : s.strokeC) h = HF(h, f);
      h = H(h, (unsigned)(s.sides * 64 + s.cap * 16 + s.join * 4 + s.align));
      h = H(h, (unsigned)(s.op * 8 + s.closed * 4 + s.fillOn * 2 + s.strokeOn));
      for (float f : s.pts) h = HF(h, f);
      return HS(h, s.svg);
   }

   // Regular polygon / star corners around the box's ellipse, first point at the top.
   void StarPoints(const Scene2DNode::Shape& s, bool star, std::vector<float>& xy)
   {
      const float cx = (s.x0 + s.x1) * 0.5f, cy = (s.y0 + s.y1) * 0.5f;
      const float rx = std::fabs(s.x1 - s.x0) * 0.5f, ry = std::fabs(s.y1 - s.y0) * 0.5f;
      const int n = std::clamp(s.sides, 3, 64) * (star ? 2 : 1);
      xy.clear();
      for (int i = 0; i < n; i++)
      {
         const float a = -kPi * 0.5f + 2.0f * kPi * (float)i / (float)n;
         const float k = star && (i % 2) == 1 ? std::clamp(s.inner, 0.01f, 1.0f) : 1.0f;
         xy.push_back(cx + std::cos(a) * rx * k);
         xy.push_back(cy + std::sin(a) * ry * k);
      }
   }

   void Cubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int steps, std::vector<float>& out)
   {
      for (int i = 1; i <= steps; i++)
      {
         const float t = (float)i / (float)steps, u = 1.0f - t;
         const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
         out.push_back(a * x0 + b * x1 + c * x2 + d * x3);
         out.push_back(a * y0 + b * y1 + c * y2 + d * y3);
      }
   }

   // The item's geometry as the canvas's current path. False: nothing to draw.
   bool BuildPath(plutovg_canvas_t* cv, const Scene2DNode::Shape& s)
   {
      using S = Scene2DNode::Shape;
      plutovg_canvas_new_path(cv);
      const float x = std::min(s.x0, s.x1), y = std::min(s.y0, s.y1);
      const float w = std::fabs(s.x1 - s.x0), h = std::fabs(s.y1 - s.y0);
      switch (s.type)
      {
      case S::kRect:
         if (w <= 0 || h <= 0)
            return false;
         if (s.radius > 0.0f)
         {
            const float r = std::min(s.radius, std::min(w, h) * 0.5f);
            plutovg_canvas_round_rect(cv, x, y, w, h, r, r);
         }
         else
            plutovg_canvas_rect(cv, x, y, w, h);
         return true;
      case S::kEllipse:
         if (w <= 0 || h <= 0)
            return false;
         plutovg_canvas_ellipse(cv, x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f);
         return true;
      case S::kPolygon:
      case S::kStar:
      {
         if (w <= 0 || h <= 0)
            return false;
         std::vector<float> xy;
         StarPoints(s, s.type == S::kStar, xy);
         plutovg_canvas_move_to(cv, xy[0], xy[1]);
         for (size_t i = 2; i < xy.size(); i += 2)
            plutovg_canvas_line_to(cv, xy[i], xy[i + 1]);
         plutovg_canvas_close_path(cv);
         return true;
      }
      case S::kLine:
         plutovg_canvas_move_to(cv, s.x0, s.y0);
         plutovg_canvas_line_to(cv, s.x1, s.y1);
         return true;
      case S::kPath:
      {
         const int n = s.Anchors();
         if (n < 2)
            return false;
         const float* p = s.pts.data();
         plutovg_canvas_move_to(cv, p[0], p[1]);
         const int segs = s.closed ? n : n - 1;
         for (int i = 0; i < segs; i++)
         {
            const float* a = p + i * 6;
            const float* b = p + ((i + 1) % n) * 6;
            plutovg_canvas_cubic_to(cv, a[4], a[5], b[2], b[3], b[0], b[1]);
         }
         if (s.closed)
            plutovg_canvas_close_path(cv);
         return true;
      }
      default:
         return false;
      }
   }

   // An SVG item drawn into its box on `cv`'s surface.
   void DrawSvg(plutovg_canvas_t* cv, const Scene2DNode::Shape& s, float opacity)
   {
      const float x = std::min(s.x0, s.x1), y = std::min(s.y0, s.y1);
      const int w = (int)std::lround(std::fabs(s.x1 - s.x0)), h = (int)std::lround(std::fabs(s.y1 - s.y0));
      if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
         return;
      auto doc = lunasvg::Document::loadFromData(s.svg);
      if (!doc)
         return;
      lunasvg::Bitmap bmp = doc->renderToBitmap(w, h);
      if (bmp.isNull())
         return;
      // lunasvg and plutovg share the premultiplied ARGB32 layout.
      plutovg_surface_t* img = plutovg_surface_create_for_data(bmp.data(), bmp.width(), bmp.height(), bmp.stride());
      plutovg_matrix_t m;
      plutovg_matrix_init_translate(&m, x, y);
      plutovg_canvas_save(cv);
      plutovg_canvas_set_texture(cv, img, PLUTOVG_TEXTURE_TYPE_PLAIN, opacity, &m);
      plutovg_canvas_new_path(cv);
      plutovg_canvas_rect(cv, x, y, (float)w, (float)h);
      plutovg_canvas_fill(cv);
      plutovg_canvas_restore(cv);
      plutovg_surface_destroy(img);
   }

   // One item (fill, then stroke with its alignment) drawn over `cv` with SRC_OVER.
   void DrawItem(plutovg_canvas_t* cv, const Scene2DNode::Shape& s, bool mask)
   {
      using S = Scene2DNode::Shape;
      if (s.type == S::kSvg)
      {
         DrawSvg(cv, s, 1.0f);
         return;
      }
      const bool fill = (s.fillOn || mask) && s.type != S::kLine && !(s.type == S::kPath && !s.closed && !mask && !s.fillOn);
      const bool stroke = s.strokeOn && !mask && s.width > 0.0f;
      auto setColor = [&](const float c[4]) { plutovg_canvas_set_rgba(cv, mask ? 1.0f : c[0], mask ? 1.0f : c[1], mask ? 1.0f : c[2], mask ? 1.0f : c[3]); };
      if (fill && BuildPath(cv, s))
      {
         setColor(s.fillC);
         plutovg_canvas_fill(cv);
      }
      if (!stroke || !BuildPath(cv, s))
         return;
      static const plutovg_line_cap_t caps[3] = { PLUTOVG_LINE_CAP_BUTT, PLUTOVG_LINE_CAP_ROUND, PLUTOVG_LINE_CAP_SQUARE };
      static const plutovg_line_join_t joins[3] = { PLUTOVG_LINE_JOIN_MITER, PLUTOVG_LINE_JOIN_ROUND, PLUTOVG_LINE_JOIN_BEVEL };
      plutovg_canvas_save(cv);
      plutovg_canvas_set_line_cap(cv, caps[std::clamp(s.cap, 0, 2)]);
      plutovg_canvas_set_line_join(cv, joins[std::clamp(s.join, 0, 2)]);
      if (s.dash > 0.0f)
      {
         const float d[2] = { s.dash, std::max(0.0f, s.gap > 0.0f ? s.gap : s.dash) };
         plutovg_canvas_set_dash_array(cv, d, 2);
      }
      const bool closedShape = s.type != S::kLine && !(s.type == S::kPath && !s.closed);
      const int align = closedShape ? s.align : 0;
      setColor(s.strokeC);
      if (align == 1)
      {
         // Inside: twice the width, clipped to the shape.
         plutovg_canvas_clip(cv);
         BuildPath(cv, s);
         plutovg_canvas_set_line_width(cv, s.width * 2.0f);
         plutovg_canvas_stroke(cv);
      }
      else if (align == 2)
      {
         // Outside: twice the width behind the fill's area, which is cut out first.
         plutovg_surface_t* tmp = plutovg_surface_create(plutovg_surface_get_width(plutovg_canvas_get_surface(cv)),
                                                         plutovg_surface_get_height(plutovg_canvas_get_surface(cv)));
         plutovg_canvas_t* t = plutovg_canvas_create(tmp);
         plutovg_canvas_set_line_cap(t, caps[std::clamp(s.cap, 0, 2)]);
         plutovg_canvas_set_line_join(t, joins[std::clamp(s.join, 0, 2)]);
         if (s.dash > 0.0f)
         {
            const float d[2] = { s.dash, std::max(0.0f, s.gap > 0.0f ? s.gap : s.dash) };
            plutovg_canvas_set_dash_array(t, d, 2);
         }
         BuildPath(t, s);
         plutovg_canvas_set_rgba(t, s.strokeC[0], s.strokeC[1], s.strokeC[2], s.strokeC[3]);
         plutovg_canvas_set_line_width(t, s.width * 2.0f);
         plutovg_canvas_stroke(t);
         BuildPath(t, s);
         plutovg_canvas_set_operator(t, PLUTOVG_OPERATOR_DST_OUT);
         plutovg_canvas_set_rgba(t, 0, 0, 0, 1);
         plutovg_canvas_fill(t);
         plutovg_canvas_destroy(t);
         plutovg_canvas_new_path(cv);
         plutovg_canvas_set_texture(cv, tmp, PLUTOVG_TEXTURE_TYPE_PLAIN, 1.0f, nullptr);
         plutovg_canvas_rect(cv, 0, 0, (float)plutovg_surface_get_width(tmp), (float)plutovg_surface_get_height(tmp));
         plutovg_canvas_fill(cv);
         plutovg_surface_destroy(tmp);
      }
      else
      {
         plutovg_canvas_set_line_width(cv, s.width);
         plutovg_canvas_stroke(cv);
      }
      plutovg_canvas_restore(cv);
   }

   // Every item, bottom to top, each combined with what is below by its boolean op.
   // Returns straight RGBA8, top row first.
   void RasterShapes(const std::vector<Scene2DNode::Shape>& shapes, int w, int h, bool mask, std::vector<uint8_t>& out)
   {
      using S = Scene2DNode::Shape;
      plutovg_surface_t* surf = plutovg_surface_create(w, h);
      plutovg_canvas_t* cv = plutovg_canvas_create(surf);
      for (const S& s : shapes)
      {
         if (s.op == S::kAdd)
         {
            DrawItem(cv, s, mask);
            continue;
         }
         // The item alone, then onto the layer with the op over the whole box (so
         // Intersect also clears what lies outside the item).
         plutovg_surface_t* tmp = plutovg_surface_create(w, h);
         plutovg_canvas_t* t = plutovg_canvas_create(tmp);
         DrawItem(t, s, mask);
         plutovg_canvas_destroy(t);
         static const plutovg_operator_t ops[4] = { PLUTOVG_OPERATOR_SRC_OVER, PLUTOVG_OPERATOR_DST_OUT, PLUTOVG_OPERATOR_DST_IN,
                                                    PLUTOVG_OPERATOR_XOR };
         plutovg_canvas_save(cv);
         plutovg_canvas_set_operator(cv, ops[std::clamp(s.op, 0, 3)]);
         plutovg_canvas_set_texture(cv, tmp, PLUTOVG_TEXTURE_TYPE_PLAIN, 1.0f, nullptr);
         plutovg_canvas_new_path(cv);
         plutovg_canvas_rect(cv, 0, 0, (float)w, (float)h);
         plutovg_canvas_fill(cv);
         plutovg_canvas_restore(cv);
         plutovg_surface_destroy(tmp);
      }
      plutovg_canvas_destroy(cv);
      out.resize((size_t)w * h * 4);
      plutovg_convert_argb_to_rgba(out.data(), plutovg_surface_get_data(surf), w, h, plutovg_surface_get_stride(surf));
      plutovg_surface_destroy(surf);
   }

   void Upload(unsigned& tex, int& tw, int& th, int w, int h, const std::vector<uint8_t>& topFirst)
   {
      std::vector<uint8_t> flipped(topFirst.size());
      const size_t stride = (size_t)w * 4;
      for (int y = 0; y < h; y++)
         std::memcpy(&flipped[(size_t)y * stride], &topFirst[(size_t)(h - 1 - y) * stride], stride);
      if (tex == 0)
         glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      if (tw == w && th == h)
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
      else
      {
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glBindTexture(GL_TEXTURE_2D, 0);
      tw = w;
      th = h;
   }

   std::string Hex(const float c[4])
   {
      char b[16];
      std::snprintf(b, sizeof(b), "#%02x%02x%02x", (int)std::lround(std::clamp(c[0], 0.0f, 1.0f) * 255),
                    (int)std::lround(std::clamp(c[1], 0.0f, 1.0f) * 255), (int)std::lround(std::clamp(c[2], 0.0f, 1.0f) * 255));
      return b;
   }
}

// ---- tokens ---------------------------------------------------------------

std::string Scene2DNode::EscapeToken(const std::string& s)
{
   if (s.empty())
      return "-";
   std::string o;
   o.reserve(s.size() + 8);
   for (char c : s)
   {
      switch (c)
      {
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      case ' ': o += "\\s"; break;
      default: o += c;
      }
   }
   // A lone "-" would read back as empty.
   return o == "-" ? "\\-" : o;
}

std::string Scene2DNode::UnescapeToken(const std::string& s)
{
   if (s == "-")
      return "";
   std::string o;
   o.reserve(s.size());
   for (size_t i = 0; i < s.size(); i++)
   {
      if (s[i] != '\\' || i + 1 >= s.size())
      {
         o += s[i];
         continue;
      }
      const char c = s[++i];
      o += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c == 's' ? ' ' : c;
   }
   return o;
}

// ---- layers ---------------------------------------------------------------

int Scene2DNode::AddVectorLayer(int above)
{
   return AddLayer(Kind::Vector, 0, above).id;
}

int Scene2DNode::AddTextLayer(const std::string& text, float x, float y, float boxW, int above)
{
   Layer& l = AddLayer(Kind::Text, 0, above);
   l.text = text;
   l.boxW = std::max(0.0f, boxW);
   l.fontSize = std::clamp(std::round(mDoc.h / 12.0f), 12.0f, 400.0f);
   // Point text grows from where it was clicked; box text keeps its top left.
   l.ax = 0.0f;
   l.ay = 0.0f;
   l.x = x;
   l.y = y;
   const int id = l.id;
   Commit();
   return id;
}

void Scene2DNode::DocToBox(const Layer& l, float x, float y, float& bx, float& by)
{
   float h[9];
   LayerHomography(l, h);
   // Inverse by the adjugate (w's scale cancels).
   const float a[9] = { h[4] * h[8] - h[5] * h[7], h[2] * h[7] - h[1] * h[8], h[1] * h[5] - h[2] * h[4],
                        h[5] * h[6] - h[3] * h[8], h[0] * h[8] - h[2] * h[6], h[2] * h[3] - h[0] * h[5],
                        h[3] * h[7] - h[4] * h[6], h[1] * h[6] - h[0] * h[7], h[0] * h[4] - h[1] * h[3] };
   const float qx = a[0] * x + a[1] * y + a[2], qy = a[3] * x + a[4] * y + a[5], qw = a[6] * x + a[7] * y + a[8];
   const float w = std::fabs(qw) > 1e-20f ? qw : 1e-20f;
   bx = qx / w;
   by = qy / w;
}

void Scene2DNode::BoxToDoc(const Layer& l, float bx, float by, float& x, float& y)
{
   float h[9];
   LayerHomography(l, h);
   const float w = std::max(1e-6f, h[6] * bx + h[7] * by + h[8]);
   x = (h[0] * bx + h[1] * by + h[2]) / w;
   y = (h[3] * bx + h[4] * by + h[5]) / w;
}

void Scene2DNode::Outline(const Shape& s, std::vector<std::vector<float>>& out)
{
   out.clear();
   const float x0 = std::min(s.x0, s.x1), y0 = std::min(s.y0, s.y1);
   const float x1 = std::max(s.x0, s.x1), y1 = std::max(s.y0, s.y1);
   switch (s.type)
   {
   case Shape::kRect:
   case Shape::kSvg:
   {
      const float r = s.type == Shape::kRect ? std::min(s.radius, std::min(x1 - x0, y1 - y0) * 0.5f) : 0.0f;
      if (r <= 0.0f)
      {
         out.push_back({ x0, y0, x1, y0, x1, y1, x0, y1, x0, y0 });
         return;
      }
      std::vector<float> p;
      const float cx[4] = { x1 - r, x1 - r, x0 + r, x0 + r }, cy[4] = { y0 + r, y1 - r, y1 - r, y0 + r };
      for (int c = 0; c < 4; c++)
         for (int i = 0; i <= 8; i++)
         {
            const float a = -kPi * 0.5f + (c + i / 8.0f) * kPi * 0.5f;
            p.push_back(cx[c] + std::cos(a) * r);
            p.push_back(cy[c] + std::sin(a) * r);
         }
      p.push_back(p[0]);
      p.push_back(p[1]);
      out.push_back(p);
      return;
   }
   case Shape::kEllipse:
   {
      std::vector<float> p;
      for (int i = 0; i <= 64; i++)
      {
         const float a = 2.0f * kPi * i / 64.0f;
         p.push_back((x0 + x1) * 0.5f + std::cos(a) * (x1 - x0) * 0.5f);
         p.push_back((y0 + y1) * 0.5f + std::sin(a) * (y1 - y0) * 0.5f);
      }
      out.push_back(p);
      return;
   }
   case Shape::kPolygon:
   case Shape::kStar:
   {
      std::vector<float> p;
      StarPoints(s, s.type == Shape::kStar, p);
      p.push_back(p[0]);
      p.push_back(p[1]);
      out.push_back(p);
      return;
   }
   case Shape::kLine:
      out.push_back({ s.x0, s.y0, s.x1, s.y1 });
      return;
   case Shape::kPath:
   {
      const int n = s.Anchors();
      if (n == 0)
         return;
      const float* p = s.pts.data();
      std::vector<float> o = { p[0], p[1] };
      const int segs = s.closed ? n : n - 1;
      for (int i = 0; i < segs; i++)
      {
         const float* a = p + i * 6;
         const float* b = p + ((i + 1) % n) * 6;
         Cubic(a[0], a[1], a[4], a[5], b[2], b[3], b[0], b[1], 16, o);
      }
      out.push_back(o);
      return;
   }
   default:
      return;
   }
}

bool Scene2DNode::ShapeBounds(const Shape& s, float lo[2], float hi[2])
{
   std::vector<std::vector<float>> lines;
   Outline(s, lines);
   lo[0] = lo[1] = 1e30f;
   hi[0] = hi[1] = -1e30f;
   for (const auto& l : lines)
      for (size_t i = 0; i + 1 < l.size(); i += 2)
      {
         lo[0] = std::min(lo[0], l[i]);
         lo[1] = std::min(lo[1], l[i + 1]);
         hi[0] = std::max(hi[0], l[i]);
         hi[1] = std::max(hi[1], l[i + 1]);
      }
   if (s.strokeOn && s.type != Shape::kSvg)
   {
      const float pad = s.width * (s.align == 2 ? 1.0f : 0.5f);
      lo[0] -= pad;
      lo[1] -= pad;
      hi[0] += pad;
      hi[1] += pad;
   }
   return lo[0] <= hi[0];
}

// ---- text -------------------------------------------------------------------

bool Scene2DNode::TextSize(const Layer& l, int& w, int& h)
{
   unsigned long long key = 1469598103934665603ull;
   key = HS(key, l.text);
   key = HS(key, l.font);
   for (float f : { l.fontSize, l.leading, l.tracking, l.boxW })
      key = HF(key, f);
   for (float f : l.textC)
      key = HF(key, f);
   key = H(key, (unsigned)l.align);
   TextBox& tb = mTextBoxes[l.id];
   if (tb.key != key || tb.rgba.empty())
   {
      tb.key = key;
      // Lay out in a canvas wide enough for the longest line (or the box), then trim to the ink.
      int lines = 1, longest = 0, cur = 0;
      for (char c : l.text)
      {
         if (c == '\n')
         {
            lines++;
            cur = 0;
            continue;
         }
         if ((c & 0xC0) != 0x80)
            longest = std::max(longest, ++cur);
      }
      const float size = std::clamp(l.fontSize, 1.0f, 2000.0f);
      const int pad = (int)std::ceil(size * 0.5f) + 4;
      int cw = l.boxW > 0.0f ? (int)std::ceil(l.boxW) + pad * 2
                             : (int)std::ceil(longest * size * (0.9f + std::max(0.0f, l.tracking) * 0.01f)) + pad * 2 + (int)size;
      int ch = (int)std::ceil(lines * size * std::max(0.5f, l.leading) * 1.4f) + pad * 2;
      if (l.boxW > 0.0f)
         ch = std::max(ch, (int)std::ceil(size * 1.5f * (1 + longest * size * 0.6f / std::max(1.0f, l.boxW)))) + pad * 2;
      cw = std::clamp(cw, 8, 8192);
      ch = std::clamp(ch, 8, 8192);
      std::vector<uint8_t> px((size_t)cw * ch * 4, 0);
      if (!l.text.empty())
      {
         Platform::TextRasterRequest req;
         req.text = l.text;
         // Empty = the platform's own default sans (as the Text node), not the alphabetically first family.
         req.fontName = l.font;
         req.fontSize = size;
         req.color[0] = l.textC[0];
         req.color[1] = l.textC[1];
         req.color[2] = l.textC[2];
         req.tracking = l.tracking;
         req.align = l.align;
         req.wordWrap = true;
         req.wrapWidth = l.boxW > 0.0f ? std::clamp(l.boxW / (float)cw, 0.05f, 1.0f) : 1.0f;
         req.wrapHeight = 1.0f;
         req.lineSpacing = l.leading;
         float fitted = size;
         Platform::RasterizeText(req, cw, ch, px.data(), fitted);
      }
      // Trim to the ink (rows always; columns only for point text, so a box keeps its width).
      int x0 = cw, y0 = ch, x1 = -1, y1 = -1;
      for (int y = 0; y < ch; y++)
         for (int x = 0; x < cw; x++)
            if (px[((size_t)y * cw + x) * 4 + 3] != 0)
            {
               x0 = std::min(x0, x);
               x1 = std::max(x1, x);
               y0 = std::min(y0, y);
               y1 = std::max(y1, y);
            }
      if (x1 < 0)
      {
         // Nothing drawn: a small empty box at the text's size so it can still be picked.
         tb.w = std::max(8, (int)size);
         tb.h = std::max(8, (int)size);
         tb.rgba.assign((size_t)tb.w * tb.h * 4, 0);
      }
      else
      {
         if (l.boxW > 0.0f)
         {
            x0 = std::max(0, (cw - (int)std::ceil(l.boxW)) / 2);
            x1 = std::min(cw - 1, x0 + (int)std::ceil(l.boxW) - 1);
         }
         tb.w = x1 - x0 + 1;
         tb.h = y1 - y0 + 1;
         tb.rgba.resize((size_t)tb.w * tb.h * 4);
         for (int y = 0; y < tb.h; y++)
            std::memcpy(&tb.rgba[(size_t)y * tb.w * 4], &px[((size_t)(y0 + y) * cw + x0) * 4], (size_t)tb.w * 4);
         // The text's own alpha.
         const float a = std::clamp(l.textC[3], 0.0f, 1.0f);
         if (a < 0.999f)
            for (size_t i = 3; i < tb.rgba.size(); i += 4)
               tb.rgba[i] = (uint8_t)std::lround(tb.rgba[i] * a);
      }
   }
   w = tb.w;
   h = tb.h;
   return !l.text.empty();
}

// ---- rasters ----------------------------------------------------------------

Scene2DNode::Raster* Scene2DNode::SyncRaster(std::map<int, Raster>& in, const Layer& l, bool mask)
{
   int w = 0, h = 0;
   unsigned long long key = 1469598103934665603ull;
   if (l.kind == Kind::Text && !mask)
   {
      if (!TextSize(l, w, h) && l.text.empty())
         return nullptr;
      key = mTextBoxes[l.id].key;
   }
   else
   {
      float bw = 1, bh = 1;
      LayerBox(l, bw, bh);
      w = std::clamp((int)std::lround(bw), 1, GLUtil::kMaxTargetSide);
      h = std::clamp((int)std::lround(bh), 1, GLUtil::kMaxTargetSide);
      for (const Shape& s : mask ? l.vmask : l.shapes)
         key = HashShape(key, s);
   }
   key = H(H(key, (unsigned)w), (unsigned)h);
   Raster& r = in[l.id];
   if (r.key == key && r.tex != 0)
      return &r;
   r.key = key;
   if (l.kind == Kind::Text && !mask)
      Upload(r.tex, r.w, r.h, w, h, mTextBoxes[l.id].rgba);
   else
   {
      std::vector<uint8_t> px;
      RasterShapes(mask ? l.vmask : l.shapes, w, h, mask, px);
      Upload(r.tex, r.w, r.h, w, h, px);
   }
   mPaintEdits++;
   return &r;
}

void Scene2DNode::SyncRasters()
{
   for (auto* m : { &mVec, &mVMasks })
      for (auto it = m->begin(); it != m->end();)
      {
         const Layer* l = FindConst(it->first);
         const bool keep = l != nullptr && (m == &mVec ? (l->kind == Kind::Vector || l->kind == Kind::Text) : l->hasVMask);
         if (!keep)
         {
            if (it->second.tex != 0)
               glDeleteTextures(1, &it->second.tex);
            it = m->erase(it);
         }
         else
            ++it;
      }
   for (auto it = mTextBoxes.begin(); it != mTextBoxes.end();)
   {
      const Layer* l = FindConst(it->first);
      it = (l == nullptr || l->kind != Kind::Text) ? mTextBoxes.erase(it) : std::next(it);
   }
}

unsigned Scene2DNode::VectorTexture(int id)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || (l->kind != Kind::Vector && l->kind != Kind::Text))
      return 0;
   Raster* r = SyncRaster(mVec, *l, false);
   return r != nullptr ? r->tex : 0;
}

// ---- SVG ----------------------------------------------------------------------

std::string Scene2DNode::ToSvg(const Layer& l)
{
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   std::ostringstream o;
   o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << bw << "\" height=\"" << bh << "\" viewBox=\"0 0 " << bw << " "
     << bh << "\">\n";
   static const char* caps[3] = { "butt", "round", "square" };
   static const char* joins[3] = { "miter", "round", "bevel" };
   int maskN = 0;
   std::ostringstream body;
   for (const Shape& s : l.shapes)
   {
      std::ostringstream el;
      const float x = std::min(s.x0, s.x1), y = std::min(s.y0, s.y1);
      const float w = std::fabs(s.x1 - s.x0), h = std::fabs(s.y1 - s.y0);
      if (s.type == Shape::kSvg)
      {
         // Nested document, placed in its box.
         std::string inner = s.svg;
         const size_t at = inner.find("<svg");
         if (at != std::string::npos)
            inner.insert(at + 4, " x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(w) +
                                    "\" height=\"" + std::to_string(h) + "\"");
         const size_t decl = inner.find("<?xml");
         if (decl != std::string::npos)
            inner.erase(decl, inner.find("?>", decl) + 2 - decl);
         el << inner << "\n";
      }
      else
      {
         std::string geo;
         if (s.type == Shape::kRect)
            geo = "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(w) +
                  "\" height=\"" + std::to_string(h) + "\" rx=\"" + std::to_string(std::min(s.radius, std::min(w, h) * 0.5f)) + "\"";
         else if (s.type == Shape::kEllipse)
            geo = "<ellipse cx=\"" + std::to_string(x + w * 0.5f) + "\" cy=\"" + std::to_string(y + h * 0.5f) + "\" rx=\"" +
                  std::to_string(w * 0.5f) + "\" ry=\"" + std::to_string(h * 0.5f) + "\"";
         else if (s.type == Shape::kLine)
            geo = "<line x1=\"" + std::to_string(s.x0) + "\" y1=\"" + std::to_string(s.y0) + "\" x2=\"" + std::to_string(s.x1) +
                  "\" y2=\"" + std::to_string(s.y1) + "\"";
         else if (s.type == Shape::kPolygon || s.type == Shape::kStar)
         {
            std::vector<float> xy;
            StarPoints(s, s.type == Shape::kStar, xy);
            geo = "<polygon points=\"";
            for (size_t i = 0; i < xy.size(); i += 2)
               geo += std::to_string(xy[i]) + "," + std::to_string(xy[i + 1]) + " ";
            geo += "\"";
         }
         else
         {
            const int n = s.Anchors();
            if (n < 2)
               continue;
            const float* p = s.pts.data();
            geo = "<path d=\"M" + std::to_string(p[0]) + " " + std::to_string(p[1]);
            const int segs = s.closed ? n : n - 1;
            for (int i = 0; i < segs; i++)
            {
               const float* a = p + i * 6;
               const float* b = p + ((i + 1) % n) * 6;
               geo += " C" + std::to_string(a[4]) + " " + std::to_string(a[5]) + " " + std::to_string(b[2]) + " " +
                      std::to_string(b[3]) + " " + std::to_string(b[0]) + " " + std::to_string(b[1]);
            }
            if (s.closed)
               geo += " Z";
            geo += "\"";
         }
         const bool fill = s.fillOn && s.type != Shape::kLine && !(s.type == Shape::kPath && !s.closed);
         el << geo << " fill=\"" << (fill ? Hex(s.fillC) : "none") << "\"";
         if (fill && s.fillC[3] < 0.999f)
            el << " fill-opacity=\"" << s.fillC[3] << "\"";
         if (s.strokeOn && s.width > 0)
         {
            el << " stroke=\"" << Hex(s.strokeC) << "\" stroke-width=\"" << s.width << "\" stroke-linecap=\""
               << caps[std::clamp(s.cap, 0, 2)] << "\" stroke-linejoin=\"" << joins[std::clamp(s.join, 0, 2)] << "\"";
            if (s.strokeC[3] < 0.999f)
               el << " stroke-opacity=\"" << s.strokeC[3] << "\"";
            if (s.dash > 0)
               el << " stroke-dasharray=\"" << s.dash << " " << (s.gap > 0 ? s.gap : s.dash) << "\"";
         }
         el << "/>\n";
      }
      // Booleans in SVG: Add stacks; Subtract masks what is below; Intersect and
      // Xor keep their own shape over what is below (closest plain-SVG reading).
      if (s.op == Shape::kSubtract)
      {
         std::string id = "m" + std::to_string(maskN++);
         std::string below = body.str();
         body.str("");
         body << "<mask id=\"" << id << "\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\" width=\"" << bw << "\" height=\"" << bh
              << "\"><rect width=\"" << bw << "\" height=\"" << bh << "\" fill=\"white\"/><g style=\"filter:brightness(0)\">"
              << el.str() << "</g></mask>\n<g mask=\"url(#" << id << ")\">\n"
              << below << "</g>\n";
      }
      else
         body << el.str();
   }
   o << body.str() << "</svg>\n";
   return o.str();
}

bool Scene2DNode::ImportSvg(int id, const std::string& svgText)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Vector)
      return false;
   auto doc = lunasvg::Document::loadFromData(svgText);
   if (!doc)
      return false;
   float sw = doc->width(), sh = doc->height();
   if (sw <= 0 || sh <= 0)
      sw = sh = 512.0f;
   // Fit inside the layer's box, centred, never scaled up past 1:1 for small art.
   const float bw = (float)std::max(1, l->pw), bh = (float)std::max(1, l->ph);
   const float k = std::min(1.0f, std::min(bw / sw, bh / sh));
   Shape s;
   s.type = Shape::kSvg;
   s.svg = svgText;
   s.x0 = (bw - sw * k) * 0.5f;
   s.y0 = (bh - sh * k) * 0.5f;
   s.x1 = s.x0 + sw * k;
   s.y1 = s.y0 + sh * k;
   l->shapes.push_back(s);
   Commit();
   return true;
}

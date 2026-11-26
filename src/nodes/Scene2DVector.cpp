// Scene 2D, Block C: vector layers (pen paths, live shapes, imported SVG),
// vector masks and text layers. All three raster on the CPU (plutovg, lunasvg,
// the platform text rasterizer) only when their content changes, then upload
// one texture the compositor samples like any other layer.

#include "Scene2DNode.h"
#include "TileStore.h"
#include "core/SelectMaths.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
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
      h = H(h, (unsigned)s.fillType);
      h = HF(h, s.fillAngle);
      for (const Scene2DNode::GradStop& g : s.fillStops)
      {
         h = HF(h, g.t);
         for (float f : g.c) h = HF(h, f);
      }
      h = HS(h, s.fillImage);
      h = H(h, (unsigned)s.imageMode);
      for (const auto& c : s.meshC) for (float f : c) h = HF(h, f);
      for (int i = 0; i < 4; i++)
      {
         h = HF(h, s.cornerR[i]);
         h = H(h, (unsigned)s.cornerType[i]);
      }
      for (float f : { s.noise, s.miter, s.dashPhase, s.arrowSize, s.arrowConcave, s.taperS, s.taperE, s.pressMin })
         h = HF(h, f);
      h = H(h, (unsigned)(s.arrowS * 64 + s.arrowE * 8 + s.balanced * 4 + s.behind * 2 + s.scaleStroke));
      h = H(h, (unsigned)(s.sides * 64 + s.cap * 16 + s.join * 4 + s.align));
      h = H(h, (unsigned)(s.op * 8 + s.closed * 4 + s.fillOn * 2 + s.strokeOn));
      for (float f : s.pts) h = HF(h, f);
      return HS(h, s.svg);
   }

   // Regular polygon / star corners around the box's ellipse, first point at the top. A triangle fills its box
   // instead, apex at the top centre whichever way the box was dragged (PhotoCraft triangle_box_geometry).
   void StarPoints(const Scene2DNode::Shape& s, bool star, std::vector<float>& xy)
   {
      const float cx = (s.x0 + s.x1) * 0.5f, cy = (s.y0 + s.y1) * 0.5f;
      const float rx = std::fabs(s.x1 - s.x0) * 0.5f, ry = std::fabs(s.y1 - s.y0) * 0.5f;
      int n = std::clamp(s.sides, 3, 64) * (star ? 2 : 1);
      xy.clear();
      if (s.type == Scene2DNode::Shape::kTriangle)
      {
         n = 3;
         xy = { cx, cy - ry, cx + rx, cy + ry, cx - rx, cy + ry };
      }
      else
         for (int i = 0; i < n; i++)
         {
            const float a = -kPi * 0.5f + 2.0f * kPi * (float)i / (float)n;
            const float k = star && (i % 2) == 1 ? std::clamp(s.inner, 0.01f, 1.0f) : 1.0f;
            xy.push_back(cx + std::cos(a) * rx * k);
            xy.push_back(cy + std::sin(a) * ry * k);
         }
      // Corner radius (Photoshop's polygon/star Radius): each corner becomes a circular arc tangent to both
      // sides, the tangent run capped at half the shorter side so neighbouring arcs never cross.
      if (s.radius <= 0.0f)
         return;
      const std::vector<float> v = xy;
      xy.clear();
      for (int i = 0; i < n; i++)
      {
         const float px = v[((i + n - 1) % n) * 2], py = v[((i + n - 1) % n) * 2 + 1];
         const float qx = v[i * 2], qy = v[i * 2 + 1];
         const float nx = v[((i + 1) % n) * 2], ny = v[((i + 1) % n) * 2 + 1];
         float ux = px - qx, uy = py - qy, wx = nx - qx, wy = ny - qy;
         const float lu = std::hypot(ux, uy), lw = std::hypot(wx, wy);
         if (lu < 1e-4f || lw < 1e-4f)
         {
            xy.insert(xy.end(), { qx, qy });
            continue;
         }
         ux /= lu, uy /= lu, wx /= lw, wy /= lw;
         const float half = 0.5f * std::acos(std::clamp(ux * wx + uy * wy, -1.0f, 1.0f));
         if (half < 1e-3f || half > kPi * 0.5f - 1e-3f)
         {
            xy.insert(xy.end(), { qx, qy });
            continue;
         }
         const float t = std::min(s.radius / std::tan(half), 0.5f * std::min(lu, lw));
         const float r = t * std::tan(half), dc = r / std::sin(half);
         float bx = ux + wx, by = uy + wy;
         const float lb = std::hypot(bx, by);
         bx /= lb, by /= lb;
         const float ox = qx + bx * dc, oy = qy + by * dc;
         const float sx = qx + ux * t, sy = qy + uy * t, ex = qx + wx * t, ey = qy + wy * t;
         float a0 = std::atan2(sy - oy, sx - ox), a1 = std::atan2(ey - oy, ex - ox), d = a1 - a0;
         while (d > kPi) d -= 2.0f * kPi;
         while (d < -kPi) d += 2.0f * kPi;
         const int steps = std::clamp((int)std::ceil(std::fabs(d) * r / 2.0f), 2, 24);
         for (int k = 0; k <= steps; k++)
         {
            const float a = a0 + d * (float)k / (float)steps;
            xy.insert(xy.end(), { ox + std::cos(a) * r, oy + std::sin(a) * r });
         }
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

   // A rectangle whose corners differ from one plain radius: per-corner radii or a corner that is not round.
   bool CustomCorners(const Scene2DNode::Shape& s)
   {
      for (int i = 0; i < 4; i++)
         if (s.cornerType[i] != 0 || (s.cornerR[i] >= 0.0f && s.cornerR[i] != s.radius))
            return true;
      return false;
   }

   // The rectangle's outline with each corner cut its own way, clockwise from the top left, flattened.
   // Round: a quarter circle; straight: a chamfer; concave: a quarter circle bitten in; inset: a square notch.
   void RectCorners(const Scene2DNode::Shape& s, std::vector<float>& p)
   {
      const float x0 = std::min(s.x0, s.x1), y0 = std::min(s.y0, s.y1);
      const float x1 = std::max(s.x0, s.x1), y1 = std::max(s.y0, s.y1);
      const float cap = std::min(x1 - x0, y1 - y0) * 0.5f;
      // Corner i: its point, and the unit steps along the edge it comes in on and the one it leaves by.
      const float cx[4] = { x0, x1, x1, x0 }, cy[4] = { y0, y0, y1, y1 };
      const float inX[4] = { 0, -1, 0, 1 }, inY[4] = { 1, 0, -1, 0 };    // back along the incoming edge
      const float outX[4] = { 1, 0, -1, 0 }, outY[4] = { 0, 1, 0, -1 };  // along the outgoing edge
      p.clear();
      for (int i = 0; i < 4; i++)
      {
         const float r = std::clamp(s.CornerR(i), 0.0f, cap);
         const float ax = cx[i] + inX[i] * r, ay = cy[i] + inY[i] * r;     // where the corner starts
         const float bx = cx[i] + outX[i] * r, by = cy[i] + outY[i] * r;   // where it ends
         if (r <= 0.0f)
         {
            p.insert(p.end(), { cx[i], cy[i] });
            continue;
         }
         p.insert(p.end(), { ax, ay });
         switch (s.cornerType[i])
         {
         case 1:
            break;   // straight: a to b
         case 3:
         {
            // Inset: in from a, across, out to b.
            p.insert(p.end(), { cx[i] + (inX[i] + outX[i]) * r, cy[i] + (inY[i] + outY[i]) * r });
            break;
         }
         default:
         {
            // Round about the inner centre; concave about the corner itself.
            const bool concave = s.cornerType[i] == 2;
            const float ox = concave ? cx[i] : cx[i] + (inX[i] + outX[i]) * r;
            const float oy = concave ? cy[i] : cy[i] + (inY[i] + outY[i]) * r;
            const float a0 = std::atan2(ay - oy, ax - ox), a1 = std::atan2(by - oy, bx - ox);
            float d = a1 - a0;
            while (d > kPi) d -= 2 * kPi;
            while (d < -kPi) d += 2 * kPi;
            for (int k = 1; k < 12; k++)
            {
               const float a = a0 + d * (float)k / 12.0f;
               p.insert(p.end(), { ox + std::cos(a) * r, oy + std::sin(a) * r });
            }
            break;
         }
         }
         p.insert(p.end(), { bx, by });
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
         if (CustomCorners(s))
         {
            std::vector<float> p;
            RectCorners(s, p);
            plutovg_canvas_move_to(cv, p[0], p[1]);
            for (size_t i = 2; i + 1 < p.size(); i += 2)
               plutovg_canvas_line_to(cv, p[i], p[i + 1]);
            plutovg_canvas_close_path(cv);
            return true;
         }
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
      case S::kTriangle:
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

   // A gradient fill's stops: its own, or its colour to white when it has none yet.
   std::vector<Scene2DNode::GradStop> FillStops(const Scene2DNode::Shape& s)
   {
      if (s.fillStops.size() >= 2)
         return s.fillStops;
      Scene2DNode::GradStop a, b;
      std::copy(s.fillC, s.fillC + 4, a.c);
      b.t = 1.0f;
      std::fill(b.c, b.c + 4, 1.0f);
      return { a, b };
   }

   // The gradient geometry in the item's box, normalized 0..1 (the fill layer's: linear spans the box along the
   // angle, radial reaches the box's edge mid-sides, reflected mirrors the linear ramp from the centre line).
   void SetGradient(plutovg_canvas_t* cv, const Scene2DNode::Shape& s)
   {
      float lo[2], hi[2];
      if (!Scene2DNode::ShapeBounds(s, lo, hi))
         return;
      const std::vector<Scene2DNode::GradStop> st = FillStops(s);
      std::vector<plutovg_gradient_stop_t> ps;
      for (const auto& g : st)
         ps.push_back({ g.t, { g.c[0], g.c[1], g.c[2], g.c[3] } });
      plutovg_matrix_t m;
      plutovg_matrix_init_translate(&m, lo[0], lo[1]);
      plutovg_matrix_scale(&m, std::max(1e-3f, hi[0] - lo[0]), std::max(1e-3f, hi[1] - lo[1]));
      const float a = s.fillAngle * kPi / 180.0f, dx = std::cos(a), dy = std::sin(a);
      const float k = (std::fabs(dx) + std::fabs(dy)) * 0.5f;
      if (s.fillType == 2)
         plutovg_canvas_set_radial_gradient(cv, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, PLUTOVG_SPREAD_METHOD_PAD, ps.data(),
                                            (int)ps.size(), &m);
      else if (s.fillType == 3)
         plutovg_canvas_set_linear_gradient(cv, 0.5f, 0.5f, 0.5f + dx * k, 0.5f + dy * k, PLUTOVG_SPREAD_METHOD_REFLECT,
                                            ps.data(), (int)ps.size(), &m);
      else
         plutovg_canvas_set_linear_gradient(cv, 0.5f - dx * k, 0.5f - dy * k, 0.5f + dx * k, 0.5f + dy * k,
                                            PLUTOVG_SPREAD_METHOD_PAD, ps.data(), (int)ps.size(), &m);
   }

   // Premultiplied ARGB32, the layout plutovg surfaces hold.
   uint32_t Argb(const float c[4])
   {
      const float a = std::clamp(c[3], 0.0f, 1.0f);
      auto ch = [&](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * a * 255.0f); };
      return ((uint32_t)std::lround(a * 255.0f) << 24) | (ch(c[0]) << 16) | (ch(c[1]) << 8) | ch(c[2]);
   }

   // Bitmap fills: each file decoded once (plutovg's own loader) and kept while the app runs; a few dozen at most.
   plutovg_surface_t* FillBitmap(const std::string& path)
   {
      static std::map<std::string, plutovg_surface_t*> cache;
      auto it = cache.find(path);
      if (it != cache.end())
         return it->second;
      plutovg_surface_t* img = path.empty() ? nullptr : plutovg_surface_load_from_image_file(path.c_str());
      if (cache.size() > 64)
      {
         for (auto& kv : cache)
            if (kv.second != nullptr)
               plutovg_surface_destroy(kv.second);
         cache.clear();
      }
      cache[path] = img;
      return img;
   }

   // The fill's paint on `cv`: a colour, a gradient, or a surface made for the item's box (conical, mesh) or the
   // bitmap. Returns a surface the caller destroys after the fill, if one was made.
   plutovg_surface_t* SetFillPaint(plutovg_canvas_t* cv, const Scene2DNode::Shape& s)
   {
      if (s.fillType >= 1 && s.fillType <= 3)
      {
         SetGradient(cv, s);
         return nullptr;
      }
      float lo[2], hi[2];
      if (s.fillType < 4 || !Scene2DNode::ShapeBounds(s, lo, hi))
      {
         plutovg_canvas_set_rgba(cv, s.fillC[0], s.fillC[1], s.fillC[2], s.fillC[3]);
         return nullptr;
      }
      const float bw = std::max(1.0f, hi[0] - lo[0]), bh = std::max(1.0f, hi[1] - lo[1]);
      plutovg_matrix_t m;
      plutovg_matrix_init_translate(&m, lo[0], lo[1]);
      if (s.fillType == 5)
      {
         plutovg_surface_t* img = FillBitmap(s.fillImage);
         if (img == nullptr)
         {
            plutovg_canvas_set_rgba(cv, s.fillC[0], s.fillC[1], s.fillC[2], s.fillC[3]);
            return nullptr;
         }
         const float iw = (float)plutovg_surface_get_width(img), ih = (float)plutovg_surface_get_height(img);
         if (s.imageMode == 3)
         {
            plutovg_canvas_set_texture(cv, img, PLUTOVG_TEXTURE_TYPE_TILED, s.fillC[3], &m);
            return nullptr;
         }
         float kx = bw / iw, ky = bh / ih;
         if (s.imageMode == 1 || s.imageMode == 2)
            kx = ky = s.imageMode == 1 ? std::max(kx, ky) : std::min(kx, ky);
         plutovg_matrix_translate(&m, (bw - iw * kx) * 0.5f, (bh - ih * ky) * 0.5f);
         plutovg_matrix_scale(&m, kx, ky);
         plutovg_canvas_set_texture(cv, img, PLUTOVG_TEXTURE_TYPE_PLAIN, s.fillC[3], &m);
         return nullptr;
      }
      // Conical and mesh: drawn per pixel at the box's size (a big box costs one pass on the CPU, as the raster).
      const int w = std::clamp((int)std::ceil(bw), 1, 8192), h = std::clamp((int)std::ceil(bh), 1, 8192);
      plutovg_surface_t* surf = plutovg_surface_create(w, h);
      unsigned char* data = plutovg_surface_get_data(surf);
      const int stride = plutovg_surface_get_stride(surf);
      const std::vector<Scene2DNode::GradStop> st = FillStops(s);
      const float a0 = s.fillAngle * kPi / 180.0f;
      for (int y = 0; y < h; y++)
      {
         uint32_t* row = (uint32_t*)(data + (size_t)y * stride);
         const float v = ((float)y + 0.5f) / (float)h;
         for (int x = 0; x < w; x++)
         {
            const float u = ((float)x + 0.5f) / (float)w;
            float c[4];
            if (s.fillType == 4)
            {
               float t = (std::atan2(v - 0.5f, u - 0.5f) - a0) / (2.0f * kPi);
               t -= std::floor(t);
               Scene2DNode::GradAt(st, st.front().c, st.back().c, t, c);
            }
            else
               for (int k = 0; k < 4; k++)
                  c[k] = (s.meshC[0][k] * (1 - u) + s.meshC[1][k] * u) * (1 - v) + (s.meshC[3][k] * (1 - u) + s.meshC[2][k] * u) * v;
            row[x] = Argb(c);
         }
      }
      plutovg_canvas_set_texture(cv, surf, PLUTOVG_TEXTURE_TYPE_PLAIN, 1.0f, &m);
      return surf;
   }

   // Grain: each pixel of `surf` keeps a random share of its alpha (the same grain every redraw).
   void Grain(plutovg_surface_t* surf, float amount)
   {
      unsigned char* data = plutovg_surface_get_data(surf);
      const int w = plutovg_surface_get_width(surf), h = plutovg_surface_get_height(surf), stride = plutovg_surface_get_stride(surf);
      for (int y = 0; y < h; y++)
      {
         uint32_t* row = (uint32_t*)(data + (size_t)y * stride);
         for (int x = 0; x < w; x++)
         {
            uint32_t n = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
            n = (n ^ (n >> 13)) * 1274126177u;
            const float r = (float)((n ^ (n >> 16)) & 0xffff) / 65535.0f;
            const float k = 1.0f - amount * r;
            const uint32_t p = row[x];
            auto sc = [&](int sh) { return (uint32_t)std::lround((float)((p >> sh) & 0xff) * k) << sh; };
            row[x] = sc(24) | sc(16) | sc(8) | sc(0);   // premultiplied: every channel scales with alpha
         }
      }
   }

   // A stroke whose width changes along the path (pressure): each polyline as one filled outline, thinning to
   // pressMin over taperS of the length at the start and taperE at the end.
   void TaperStroke(plutovg_canvas_t* cv, const Scene2DNode::Shape& s, float width)
   {
      std::vector<std::vector<float>> lines;
      Scene2DNode::Outline(s, lines);
      for (const std::vector<float>& p : lines)
      {
         const size_t n = p.size() / 2;
         if (n < 2)
            continue;
         std::vector<float> len(n, 0.0f);
         for (size_t i = 1; i < n; i++)
            len[i] = len[i - 1] + std::hypot(p[i * 2] - p[i * 2 - 2], p[i * 2 + 1] - p[i * 2 - 1]);
         const float total = std::max(1e-3f, len[n - 1]);
         std::vector<float> left, right;
         for (size_t i = 0; i < n; i++)
         {
            const size_t a = i > 0 ? i - 1 : 0, b = i + 1 < n ? i + 1 : n - 1;
            float dx = p[b * 2] - p[a * 2], dy = p[b * 2 + 1] - p[a * 2 + 1];
            const float dl = std::max(1e-6f, std::hypot(dx, dy));
            dx /= dl;
            dy /= dl;
            const float t = len[i] / total;
            float f = 1.0f;
            if (s.taperS > 0.0f && t < s.taperS)
               f = std::min(f, t / s.taperS);
            if (s.taperE > 0.0f && t > 1.0f - s.taperE)
               f = std::min(f, (1.0f - t) / s.taperE);
            f = f * f * (3.0f - 2.0f * f);
            const float hw = width * 0.5f * (s.pressMin + (1.0f - s.pressMin) * f);
            left.insert(left.end(), { p[i * 2] - dy * hw, p[i * 2 + 1] + dx * hw });
            right.insert(right.end(), { p[i * 2] + dy * hw, p[i * 2 + 1] - dx * hw });
         }
         plutovg_canvas_new_path(cv);
         plutovg_canvas_move_to(cv, left[0], left[1]);
         for (size_t i = 2; i + 1 < left.size(); i += 2)
            plutovg_canvas_line_to(cv, left[i], left[i + 1]);
         for (size_t i = right.size(); i >= 2; i -= 2)
            plutovg_canvas_line_to(cv, right[i - 2], right[i - 1]);
         plutovg_canvas_close_path(cv);
         plutovg_canvas_set_fill_rule(cv, PLUTOVG_FILL_RULE_NON_ZERO);
         plutovg_canvas_fill(cv);
      }
   }

   // Arrowheads at an open item's ends, in the stroke's colour, reaching out past the end.
   void Arrowheads(plutovg_canvas_t* cv, const Scene2DNode::Shape& s, float width)
   {
      std::vector<std::vector<float>> lines;
      Scene2DNode::Outline(s, lines);
      if (lines.empty() || lines[0].size() < 4)
         return;
      const std::vector<float>& p = lines[0];
      const size_t n = p.size() / 2;
      auto head = [&](int kind, size_t at, size_t from)
      {
         if (kind <= 0)
            return;
         const float ex = p[at * 2], ey = p[at * 2 + 1];
         float dx = ex - p[from * 2], dy = ey - p[from * 2 + 1];
         const float dl = std::hypot(dx, dy);
         if (dl < 1e-6f)
            return;
         dx /= dl;
         dy /= dl;
         const float L = std::max(1.0f, s.arrowSize * width), W = L * 0.5f;
         const float nx = -dy, ny = dx;
         plutovg_canvas_new_path(cv);
         switch (kind)
         {
         case 1:   // arrow: an open V
            plutovg_canvas_move_to(cv, ex - dx * L * 0.2f + nx * W, ey - dy * L * 0.2f + ny * W);
            plutovg_canvas_line_to(cv, ex + dx * L * 0.8f, ey + dy * L * 0.8f);
            plutovg_canvas_line_to(cv, ex - dx * L * 0.2f - nx * W, ey - dy * L * 0.2f - ny * W);
            plutovg_canvas_set_line_width(cv, width);
            plutovg_canvas_set_line_join(cv, PLUTOVG_LINE_JOIN_MITER);
            plutovg_canvas_stroke(cv);
            return;
         case 2:   // triangle; Concavity moves the back's middle toward the tip (+) or away (-), Photoshop's arrowhead
         {
            const float c = std::clamp(s.arrowConcave, -Scene2DNode::Shape::kArrowConcaveMax, Scene2DNode::Shape::kArrowConcaveMax) / 100.0f * L;
            plutovg_canvas_move_to(cv, ex + nx * W, ey + ny * W);
            plutovg_canvas_line_to(cv, ex + dx * L, ey + dy * L);
            plutovg_canvas_line_to(cv, ex - nx * W, ey - ny * W);
            plutovg_canvas_line_to(cv, ex + dx * c, ey + dy * c);
            plutovg_canvas_close_path(cv);
            break;
         }
         case 3:   // circle
            plutovg_canvas_circle(cv, ex + dx * W * 0.5f, ey + dy * W * 0.5f, W);
            break;
         case 4:   // square
            plutovg_canvas_move_to(cv, ex + nx * W, ey + ny * W);
            plutovg_canvas_line_to(cv, ex + nx * W + dx * L, ey + ny * W + dy * L);
            plutovg_canvas_line_to(cv, ex - nx * W + dx * L, ey - ny * W + dy * L);
            plutovg_canvas_line_to(cv, ex - nx * W, ey - ny * W);
            plutovg_canvas_close_path(cv);
            break;
         default:  // bar across the end
            plutovg_canvas_move_to(cv, ex + nx * W, ey + ny * W);
            plutovg_canvas_line_to(cv, ex - nx * W, ey - ny * W);
            plutovg_canvas_set_line_width(cv, width);
            plutovg_canvas_set_line_cap(cv, PLUTOVG_LINE_CAP_BUTT);
            plutovg_canvas_stroke(cv);
            return;
         }
         plutovg_canvas_fill(cv);
      };
      // The direction from a point a little way back, so a curve's last tiny segment does not swing the head.
      auto back = [&](size_t at, int step)
      {
         size_t i = at;
         float d = 0.0f;
         while (d < width && ((step < 0 && i > 0) || (step > 0 && i + 1 < n)))
         {
            const size_t j = (size_t)((long long)i + step);
            d += std::hypot(p[j * 2] - p[i * 2], p[j * 2 + 1] - p[i * 2 + 1]);
            i = j;
         }
         return i;
      };
      head(s.arrowS, 0, back(0, 1));
      head(s.arrowE, n - 1, back(n - 1, -1));
   }

   // One item (fill and stroke, the stroke under the fill when `behind`) drawn over `cv` with SRC_OVER.
   // strokeK scales stroke widths that do not follow the layer's scale (1 / that scale).
   void DrawItem(plutovg_canvas_t* cv, const Scene2DNode::Shape& s, bool mask, float strokeK = 1.0f)
   {
      using S = Scene2DNode::Shape;
      if (s.type == S::kSvg)
      {
         DrawSvg(cv, s, 1.0f);
         return;
      }
      const bool fill = (s.fillOn || mask) && s.type != S::kLine && !(s.type == S::kPath && !s.closed && !mask && !s.fillOn);
      const float width = s.width * (s.scaleStroke ? 1.0f : strokeK);
      const bool stroke = s.strokeOn && !mask && width > 0.0f;
      auto setColor = [&](const float c[4]) { plutovg_canvas_set_rgba(cv, mask ? 1.0f : c[0], mask ? 1.0f : c[1], mask ? 1.0f : c[2], mask ? 1.0f : c[3]); };
      auto drawFill = [&]()
      {
         if (!fill || !BuildPath(cv, s))
            return;
         if (mask)
         {
            setColor(s.fillC);
            plutovg_canvas_fill(cv);
            return;
         }
         if (s.noise <= 0.0f)
         {
            plutovg_surface_t* own = SetFillPaint(cv, s);
            plutovg_canvas_fill(cv);
            if (own != nullptr)
               plutovg_surface_destroy(own);
            return;
         }
         // Grain: the fill alone on its own surface, grained, then laid on.
         plutovg_surface_t* base = plutovg_canvas_get_surface(cv);
         plutovg_surface_t* tmp = plutovg_surface_create(plutovg_surface_get_width(base), plutovg_surface_get_height(base));
         plutovg_canvas_t* t = plutovg_canvas_create(tmp);
         BuildPath(t, s);
         plutovg_surface_t* own = SetFillPaint(t, s);
         plutovg_canvas_fill(t);
         plutovg_canvas_destroy(t);
         if (own != nullptr)
            plutovg_surface_destroy(own);
         Grain(tmp, std::clamp(s.noise, 0.0f, 1.0f));
         plutovg_canvas_save(cv);
         plutovg_canvas_new_path(cv);
         plutovg_canvas_set_texture(cv, tmp, PLUTOVG_TEXTURE_TYPE_PLAIN, 1.0f, nullptr);
         plutovg_canvas_rect(cv, 0, 0, (float)plutovg_surface_get_width(tmp), (float)plutovg_surface_get_height(tmp));
         plutovg_canvas_fill(cv);
         plutovg_canvas_restore(cv);
         plutovg_surface_destroy(tmp);
      };
      if (!stroke)
      {
         drawFill();
         return;
      }
      if (!s.behind)
         drawFill();
      static const plutovg_line_cap_t caps[3] = { PLUTOVG_LINE_CAP_BUTT, PLUTOVG_LINE_CAP_ROUND, PLUTOVG_LINE_CAP_SQUARE };
      static const plutovg_line_join_t joins[3] = { PLUTOVG_LINE_JOIN_MITER, PLUTOVG_LINE_JOIN_ROUND, PLUTOVG_LINE_JOIN_BEVEL };
      const bool closedShape = s.type != S::kLine && !(s.type == S::kPath && !s.closed);
      const int align = closedShape ? s.align : 0;
      // Line settings for one canvas: caps, joins, miter, dashes (balanced: stretched to a whole number round a
      // closed outline, so it never ends on a stub).
      auto lineStyle = [&](plutovg_canvas_t* c)
      {
         plutovg_canvas_set_line_cap(c, caps[std::clamp(s.cap, 0, 2)]);
         plutovg_canvas_set_line_join(c, joins[std::clamp(s.join, 0, 2)]);
         plutovg_canvas_set_miter_limit(c, std::clamp(s.miter, 1.0f, 100.0f));
         if (s.dash > 0.0f)
         {
            float d[2] = { s.dash, std::max(0.0f, s.gap > 0.0f ? s.gap : s.dash) };
            if (s.balanced && closedShape && BuildPath(c, s))
            {
               const float total = plutovg_path_length(plutovg_canvas_get_path(c));
               const float period = d[0] + d[1];
               const float n = std::max(1.0f, std::round(total / period));
               const float k = total / (n * period);
               d[0] *= k;
               d[1] *= k;
            }
            plutovg_canvas_set_dash(c, s.dashPhase, d, 2);
         }
      };
      plutovg_canvas_save(cv);
      setColor(s.strokeC);
      const bool tapered = s.taperS > 0.0f || s.taperE > 0.0f;
      if (tapered)
         TaperStroke(cv, s, width);
      else if (align == 1 && BuildPath(cv, s))
      {
         // Inside: twice the width, clipped to the shape.
         lineStyle(cv);
         BuildPath(cv, s);
         plutovg_canvas_clip(cv);
         BuildPath(cv, s);
         plutovg_canvas_set_line_width(cv, width * 2.0f);
         plutovg_canvas_stroke(cv);
      }
      else if (align == 2)
      {
         // Outside: twice the width behind the fill's area, which is cut out first.
         plutovg_surface_t* tmp = plutovg_surface_create(plutovg_surface_get_width(plutovg_canvas_get_surface(cv)),
                                                         plutovg_surface_get_height(plutovg_canvas_get_surface(cv)));
         plutovg_canvas_t* t = plutovg_canvas_create(tmp);
         lineStyle(t);
         BuildPath(t, s);
         plutovg_canvas_set_rgba(t, s.strokeC[0], s.strokeC[1], s.strokeC[2], s.strokeC[3]);
         plutovg_canvas_set_line_width(t, width * 2.0f);
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
      else if (BuildPath(cv, s))
      {
         lineStyle(cv);
         BuildPath(cv, s);
         plutovg_canvas_set_line_width(cv, width);
         plutovg_canvas_stroke(cv);
      }
      if (!closedShape && (s.arrowS > 0 || s.arrowE > 0))
      {
         plutovg_canvas_set_dash_array(cv, nullptr, 0);
         setColor(s.strokeC);
         Arrowheads(cv, s, width);
      }
      plutovg_canvas_restore(cv);
      if (s.behind)
         drawFill();
   }

   // Every item, bottom to top, each combined with what is below by its boolean op.
   // Returns straight RGBA8, top row first.
   // The shapes onto a premultiplied ARGB buffer of w*h*4 (plutovg's own form; cleared by the caller).
   void RasterShapesArgb(const Scene2DNode::Shape* first, size_t count, int w, int h, bool mask, uint8_t* argb,
                         float strokeK = 1.0f)
   {
      using S = Scene2DNode::Shape;
      plutovg_surface_t* surf = plutovg_surface_create_for_data(argb, w, h, w * 4);
      plutovg_canvas_t* cv = plutovg_canvas_create(surf);
      for (size_t i = 0; i < count; i++)
      {
         const S& s = first[i];
         if (s.op == S::kAdd)
         {
            DrawItem(cv, s, mask, strokeK);
            continue;
         }
         // The item alone, then onto the layer with the op over the whole box (so
         // Intersect also clears what lies outside the item).
         plutovg_surface_t* tmp = plutovg_surface_create(w, h);
         plutovg_canvas_t* t = plutovg_canvas_create(tmp);
         DrawItem(t, s, mask, strokeK);
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
      plutovg_surface_destroy(surf);
   }

   void RasterShapes(const std::vector<Scene2DNode::Shape>& shapes, int w, int h, bool mask, std::vector<uint8_t>& out,
                     float strokeK = 1.0f)
   {
      std::vector<uint8_t> argb((size_t)w * h * 4, 0);
      RasterShapesArgb(shapes.data(), shapes.size(), w, h, mask, argb.data(), strokeK);
      out.resize(argb.size());
      plutovg_convert_argb_to_rgba(out.data(), argb.data(), w, h, w * 4);
   }

   void Upload(unsigned& tex, int& tw, int& th, int w, int h, const std::vector<uint8_t>& topFirst)
   {
      thread_local std::vector<uint8_t> flipped;
      flipped.resize(topFirst.size());
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

bool Scene2DNode::ShapeHit(const Shape& s, float x, float y, float pad)
{
   std::vector<std::vector<float>> lines;
   Outline(s, lines);
   const float reach = pad + (s.strokeOn || s.type == Shape::kLine ? s.width * 0.5f : 0.0f);
   bool inside = false;
   for (const std::vector<float>& p : lines)
   {
      const size_t n = p.size() / 2;
      if (n < 2)
         continue;
      for (size_t i = 0; i + 1 < n; i++)
      {
         const float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[i * 2 + 2], by = p[i * 2 + 3];
         const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
         const float t = len2 > 0.0f ? std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
         if (std::hypot(x - (ax + dx * t), y - (ay + dy * t)) <= reach)
            return true;
      }
      // Even-odd, the polyline closed back to its start (an open path fills that way too).
      for (size_t i = 0, j = n - 1; i < n; j = i++)
      {
         const float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[j * 2], by = p[j * 2 + 1];
         if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax)
            inside = !inside;
      }
   }
   return inside && s.fillOn && s.type != Shape::kLine;
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
      if (s.type == Shape::kRect && CustomCorners(s))
      {
         std::vector<float> p;
         RectCorners(s, p);
         p.push_back(p[0]);
         p.push_back(p[1]);
         out.push_back(p);
         return;
      }
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
   case Shape::kTriangle:
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
      float pad = s.width * (s.align == 2 ? 1.0f : 0.5f);
      if (s.arrowS > 0 || s.arrowE > 0)
         pad = std::max(pad, s.width * std::max(1.0f, s.arrowSize));
      lo[0] -= pad;
      lo[1] -= pad;
      hi[0] += pad;
      hi[1] += pad;
   }
   return lo[0] <= hi[0];
}

// ---- text -------------------------------------------------------------------

// ALL CAPS for ASCII and Latin-1 letters (a..z, à..þ but ÷); other scripts pass through.
std::string Scene2DNode::UpperText(const std::string& in)
{
   std::string out;
   out.reserve(in.size());
   for (size_t i = 0; i < in.size(); i++)
   {
      const unsigned char c = (unsigned char)in[i];
      if (c >= 'a' && c <= 'z')
         out += (char)(c - 32);
      else if (c == 0xC3 && i + 1 < in.size())
      {
         unsigned char d = (unsigned char)in[i + 1];
         if (d >= 0xA0 && d <= 0xBE && d != 0xB7)
            d -= 0x20;
         out += (char)c;
         out += (char)d;
         i++;
      }
      else
         out += (char)c;
   }
   return out;
}

// Text to vector paths: one closed path per glyph contour, holes cut by Exclude. Lines come from hard breaks;
// the result is fitted onto the text's drawn box so it lands where the text was. The text layer is hidden.
int Scene2DNode::ConvertTextToShapes(int id)
{
   const Layer* src = FindConst(id);
   if (src == nullptr || src->kind != Kind::Text || src->text.empty())
      return 0;
   int tw = 0, th = 0;
   if (!TextSize(*src, tw, th) || tw <= 0 || th <= 0)
      return 0;
   // Warp Text: the letters fit the box before the warp; the anchors go through the warp afterwards.
   if (src->warpStyle != 0)
   {
      tw = std::max(1, mTextBoxes[id].gw);
      th = std::max(1, mTextBoxes[id].gh);
   }
   // Type on / in a path and vertical type: the glyphs where they were laid, about the box centre.
   if (UsesGlyphType(*src))
   {
      std::vector<std::vector<float>> glyphs;
      if (!TypeGlyphAnchors(*src, glyphs) || glyphs.empty())
         return 0;
      const float gx = std::clamp(std::fabs(src->sx), 0.01f, 100.0f), gy = std::clamp(std::fabs(src->sy), 0.01f, 100.0f);
      std::vector<Shape> out;
      for (auto& p : glyphs)
      {
         Shape s;
         s.type = Shape::kPath;
         s.closed = true;
         s.fillOn = true;
         std::copy(src->textC, src->textC + 4, s.fillC);
         s.op = out.empty() ? Shape::kAdd : Shape::kXor;
         s.pts.resize(p.size());
         for (size_t i = 0; i + 1 < p.size(); i += 2)
         {
            s.pts[i] = (p[i] + tw * 0.5f) * gx;
            s.pts[i + 1] = (p[i + 1] + th * 0.5f) * gy;
         }
         out.push_back(std::move(s));
      }
      return ShapesLayerFromText(id, tw, th, std::move(out));
   }
   const std::string text = src->caps ? UpperText(src->text) : src->text;
   const float letter = src->fontSize > 0.0f ? src->tracking / src->fontSize : 0.0f;
   // Cap height is one unit; an em is about 1.43 of it.
   const float pitch = std::max(0.5f, src->leading) * 1.43f;
   struct Line { std::vector<Platform::TextContour> c; float x0, x1; };
   std::vector<Line> lines;
   std::stringstream ss(text);
   std::string row;
   while (std::getline(ss, row))
   {
      Line ln;
      ln.x0 = ln.x1 = 0.0f;
      std::string err;
      float base = 0.0f;
      if (!row.empty() && Platform::GetTextOutlines(row, src->font, letter, ln.c, err, &base))
      {
         // Each row comes back centred on its own ink: put every row's baseline at 0 so rows with and without
         // descenders stack on the leading alike.
         for (auto& c : ln.c)
         {
            for (size_t i = 1; i < c.points.size(); i += 2)
               c.points[i] -= base;
            for (size_t i = 1; i < c.curve.size(); i += 2)
               c.curve[i] -= base;
         }
         ln.x0 = 1e30f;
         ln.x1 = -1e30f;
         for (const auto& c : ln.c)
            for (size_t i = 0; i + 1 < c.points.size(); i += 2)
            {
               ln.x0 = std::min(ln.x0, c.points[i]);
               ln.x1 = std::max(ln.x1, c.points[i]);
            }
      }
      lines.push_back(std::move(ln));
   }
   float wide = 0.0f;
   for (const Line& ln : lines)
      wide = std::max(wide, ln.x1 - ln.x0);
   // Lay out in text units, y down: align each line, stack by the leading, slant if italic. Each contour becomes
   // anchors (x y inX inY outX outY): the font's own Bezier curves where the OS gives them, else the flattened
   // outline thinned to the points that matter (Douglas-Peucker), so every letter stays editable by hand.
   std::vector<std::vector<float>> polys;
   float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
   auto thin = [](const std::vector<float>& p, float tol)
   {
      const size_t n = p.size() / 2;
      std::vector<char> keep(n, 0);
      if (n < 3)
      {
         std::fill(keep.begin(), keep.end(), 1);
      }
      else
      {
         keep[0] = keep[n - 1] = 1;
         std::vector<std::pair<size_t, size_t>> stack { { 0, n - 1 } };
         while (!stack.empty())
         {
            const auto [a, b] = stack.back();
            stack.pop_back();
            const float ax = p[a * 2], ay = p[a * 2 + 1], bx = p[b * 2], by = p[b * 2 + 1];
            const float dx = bx - ax, dy = by - ay, len = std::max(1e-6f, std::hypot(dx, dy));
            float worst = 0.0f;
            size_t at = 0;
            for (size_t i = a + 1; i < b; i++)
            {
               const float d = std::fabs((p[i * 2] - ax) * dy - (p[i * 2 + 1] - ay) * dx) / len;
               if (d > worst)
               {
                  worst = d;
                  at = i;
               }
            }
            if (worst > tol)
            {
               keep[at] = 1;
               stack.push_back({ a, at });
               stack.push_back({ at, b });
            }
         }
      }
      std::vector<float> out;
      for (size_t i = 0; i < n; i++)
         if (keep[i])
            out.insert(out.end(), { p[i * 2], p[i * 2 + 1], p[i * 2], p[i * 2 + 1], p[i * 2], p[i * 2 + 1] });
      return out;
   };
   for (size_t li = 0; li < lines.size(); li++)
   {
      const Line& ln = lines[li];
      const float w = ln.x1 - ln.x0;
      const float shift = src->align == 1 ? (wide - w) * 0.5f : src->align == 2 ? wide - w : 0.0f;
      for (const auto& c : ln.c)
      {
         if (c.points.size() < 6)
            continue;
         std::vector<float> a = c.curve.size() >= 12 ? c.curve : thin(c.points, 0.004f);
         for (size_t i = 0; i + 1 < a.size(); i += 2)
         {
            const float yUp = a[i + 1];
            float x = a[i] - ln.x0 + shift;
            if (src->italic)
               x += yUp * 0.21f;
            a[i] = x;
            a[i + 1] = -yUp + (float)li * pitch;
         }
         for (size_t i = 0; i + 1 < c.points.size(); i += 2)
         {
            const float x = c.points[i] - ln.x0 + shift + (src->italic ? c.points[i + 1] * 0.21f : 0.0f);
            const float y = -c.points[i + 1] + (float)li * pitch;
            lo[0] = std::min(lo[0], x);
            hi[0] = std::max(hi[0], x);
            lo[1] = std::min(lo[1], y);
            hi[1] = std::max(hi[1], y);
         }
         polys.push_back(std::move(a));
      }
   }
   if (polys.empty() || hi[0] <= lo[0] || hi[1] <= lo[1])
      return 0;
   // Fit onto the drawn box, same scale both ways, centred.
   const float k = std::min((float)tw / (hi[0] - lo[0]), (float)th / (hi[1] - lo[1]));
   const float ox = ((float)tw - (hi[0] - lo[0]) * k) * 0.5f, oy = ((float)th - (hi[1] - lo[1]) * k) * 0.5f;
   // The layer's scale goes into the paths (a vector box drawn at its own size stays sharp when it was scaled up).
   const float bx = std::clamp(std::fabs(src->sx), 0.01f, 100.0f), by = std::clamp(std::fabs(src->sy), 0.01f, 100.0f);
   std::vector<Shape> shapes;
   shapes.reserve(polys.size());
   for (const auto& p : polys)
   {
      Shape s;
      s.type = Shape::kPath;
      s.closed = true;
      s.fillOn = true;
      std::copy(src->textC, src->textC + 4, s.fillC);
      s.op = shapes.empty() ? Shape::kAdd : Shape::kXor;
      s.pts.resize(p.size());
      for (size_t i = 0; i + 1 < p.size(); i += 2)
      {
         s.pts[i] = (ox + (p[i] - lo[0]) * k) * bx;
         s.pts[i + 1] = (oy + (p[i + 1] - lo[1]) * k) * by;
      }
      shapes.push_back(std::move(s));
   }
   return ShapesLayerFromText(id, tw, th, std::move(shapes));
}

// The shapes (box px times the layer's scale) as a new vector layer above the text, which hides.
int Scene2DNode::ShapesLayerFromText(int id, int tw, int th, std::vector<Shape> shapes)
{
   const Layer* src = FindConst(id);
   if (src == nullptr || shapes.empty())
      return 0;
   const float bx = std::clamp(std::fabs(src->sx), 0.01f, 100.0f), by = std::clamp(std::fabs(src->sy), 0.01f, 100.0f);
   // Warp Text: every anchor and handle through the same warp the raster had, into the warped box.
   if (src->warpStyle != 0)
   {
      const TextBox& tb = mTextBoxes[id];
      const float W0 = (float)std::max(1, tw), H0 = (float)std::max(1, th);
      const float W1 = (float)std::max(1, tb.w), H1 = (float)std::max(1, tb.h);
      for (Shape& s : shapes)
         for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
         {
            float u = s.pts[i] / bx / W0 * 2.0f - 1.0f, v = s.pts[i + 1] / by / H0 * 2.0f - 1.0f;
            WarpStyleMap(src->warpStyle, src->warpBend, src->warpH, src->warpV, src->warpVert, u, v, H0 / W0);
            s.pts[i] = (W1 * 0.5f + u * W0 * 0.5f) * bx;
            s.pts[i + 1] = (H1 * 0.5f + v * H0 * 0.5f) * by;
         }
      tw = (int)W1;
      th = (int)H1;
   }
   Layer v = *src;   // copy before the add: it may move the layers
   v.typeMode = 0;
   v.typePath.clear();
   v.vertical = false;
   v.warpStyle = 0;
   const int vid = AddVectorLayer(id);
   Layer* dst = FindLayer(vid);
   if (dst == nullptr)
      return 0;
   v.id = vid;
   v.kind = Kind::Vector;
   v.uid = 0;
   v.output = false;
   v.text.clear();
   v.tiles.clear();
   v.mtiles.clear();
   v.mw = v.mh = 0;
   v.pw = std::max(1, (int)std::lround(tw * bx));
   v.ph = std::max(1, (int)std::lround(th * by));
   v.sx = src->sx < 0.0f ? -1.0f : 1.0f;
   v.sy = src->sy < 0.0f ? -1.0f : 1.0f;
   v.shapes = std::move(shapes);
   if (v.name.empty())
      v.name = "Shapes";
   *dst = std::move(v);
   if (Layer* t = FindLayer(id))
      t->visible = false;
   Commit();
   return vid;
}

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
   key = H(key, (unsigned)(l.bold | (l.italic << 1) | (l.underline << 2) | (l.caps << 3) | (l.strike << 4) | (l.highlight << 5) |
                           (l.kerning << 6) | (l.script << 7)));
   for (float f : { l.baseShift, l.textSX, l.textSY, l.indentL, l.indentR, l.indentFirst, l.spaceBefore, l.spaceAfter, l.offsetX })
      key = HF(key, f);
   key = H(key, (unsigned)(l.ulOwn | (l.stOwn << 1) | (l.noBreak << 2) | (l.optical << 3) | (l.outline << 4)));
   key = HS(key, l.lang);
   for (float f : l.ulC) key = HF(key, f);
   for (float f : l.stC) key = HF(key, f);
   for (float f : l.outlineC) key = HF(key, f);
   key = HF(key, l.outlineW);
   if (l.highlight)
      for (float f : l.highlightC)
         key = HF(key, f);
   key = H(key, (unsigned)(l.typeMode | (l.pathFlip << 2) | (l.vertical << 3) | (l.warpVert << 4)));
   key = H(key, (unsigned)l.warpStyle);
   for (float f : { l.pathOffset, l.warpBend, l.warpH, l.warpV })
      key = HF(key, f);
   for (const Shape& s : l.typePath)
      key = HashShape(key, s);
   // Styled runs, as they would be synced now (the layer's fields may have moved since the last save).
   const std::vector<TextRun> runs = CharRuns(l);
   float runMax = l.fontSize;
   if (runs.size() > 1)
      for (const TextRun& r : runs)
      {
         key = H(key, (unsigned)r.len);
         key = HS(key, r.st.font);
         for (float f : { r.st.size, r.st.tracking, r.st.c[0], r.st.c[1], r.st.c[2], r.st.c[3] })
            key = HF(key, f);
         key = H(key, (unsigned)(r.st.bold | (r.st.italic << 1) | (r.st.underline << 2)));
         runMax = std::max(runMax, r.st.size);
      }
   TextBox& tb = mTextBoxes[l.id];
   if (tb.key != key || tb.rgba.empty())
   {
      tb.key = key;
      if (UsesGlyphType(l))
         GlyphTypeRaster(l, tb);
      else
      {
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
      // Super/subscript: the letters at 58%, raised by a third or dropped by a seventh of the full size.
      const float full = std::clamp(l.fontSize, 1.0f, 2000.0f);
      const float size = l.script != 0 ? full * 0.58f : full;
      // The canvas fits the largest run (a run bigger than the first one needs the room).
      const float roomScale = std::clamp(runMax, 1.0f, 2000.0f) / full;
      const float shift = l.baseShift + (l.script == 1 ? full * 0.33f : l.script == 2 ? -full * 0.14f : 0.0f);
      const float tsx = std::clamp(l.textSX, 0.01f, 100.0f), tsy = std::clamp(l.textSY, 0.01f, 100.0f);
      const int pad = (int)std::ceil(size * roomScale * 0.5f + std::fabs(shift)) + 4;
      // No break: the box never wraps (laid out as point text).
      const float boxW = l.noBreak ? 0.0f : l.boxW;
      int cw = boxW > 0.0f ? (int)std::ceil(boxW) + pad * 2
                             : (int)std::ceil((longest * size * roomScale * (0.9f + std::max(0.0f, l.tracking) * 0.01f) + l.indentL + l.indentFirst) * tsx) +
                                  pad * 2 + (int)(size * roomScale);
      int ch = (int)std::ceil((lines * size * roomScale * std::max(0.5f, l.leading) * 1.4f + lines * (l.spaceBefore + l.spaceAfter)) * tsy) + pad * 2;
      if (boxW > 0.0f)
         ch = std::max(ch, (int)std::ceil(size * 1.5f * (1 + longest * size * 0.6f / std::max(1.0f, boxW)))) + pad * 2;
      cw = std::clamp(cw, 8, 8192);
      ch = std::clamp(ch, 8, 8192);
      std::vector<uint8_t> px((size_t)cw * ch * 4, 0);
      if (!l.text.empty())
      {
         Platform::TextRasterRequest req;
         req.text = l.caps ? UpperText(l.text) : l.text;
         req.bold = l.bold;
         req.italic = l.italic;
         req.underline = l.underline;
         // Empty = the platform's own default sans (as the Text node), not the alphabetically first family.
         req.fontName = l.font;
         req.fontSize = size;
         req.color[0] = l.textC[0];
         req.color[1] = l.textC[1];
         req.color[2] = l.textC[2];
         req.tracking = l.tracking;
         req.align = l.align;
         req.wordWrap = true;
         req.wrapWidth = boxW > 0.0f ? std::clamp(boxW / (float)cw, 0.05f, 1.0f) : 1.0f;
         req.wrapHeight = 1.0f;
         req.lineSpacing = l.leading;
         req.strike = l.strike;
         req.kerning = l.kerning;
         req.baseShift = shift;
         req.highlight = l.highlight;
         req.underlineOwn = l.ulOwn;
         req.strikeOwn = l.stOwn;
         std::copy(l.ulC, l.ulC + 4, req.underlineColor);
         std::copy(l.stC, l.stC + 4, req.strikeColor);
         req.language = l.lang;
         // Outline: centred on the letter edge, px -> percent of the size.
         if (l.outline && l.outlineW > 0.0f)
         {
            req.outlineWidth = l.outlineW / size * 100.0f;
            std::copy(l.outlineC, l.outlineC + 3, req.outlineColor);
         }
         std::copy(l.highlightC, l.highlightC + 4, req.highlightColor);
         req.indentL = l.indentL;
         req.indentR = l.indentR;
         req.indentFirst = l.indentFirst;
         req.spaceBefore = l.spaceBefore;
         req.spaceAfter = l.spaceAfter;
         // Width / height scale: the letters stretch about the box centre; a box keeps its width.
         req.scaleX = tsx;
         req.scaleY = tsy;
         // Styled runs; ALL CAPS that changes the byte length (e.g. a German sharp s) draws one style.
         if (runs.size() > 1 && req.text.size() == l.text.size())
         {
            const float alpha = std::max(1e-4f, std::clamp(l.textC[3], 0.0f, 1.0f));
            size_t at = 0;
            for (const TextRun& r : runs)
            {
               Platform::TextRasterRequest::Span sp;
               sp.start = at;
               sp.len = r.len;
               sp.font = r.st.font;
               sp.size = (l.script != 0 ? 0.58f : 1.0f) * std::clamp(r.st.size, 1.0f, 2000.0f);
               sp.tracking = r.st.tracking;
               std::copy(r.st.c, r.st.c + 3, sp.color);
               sp.color[3] = std::clamp(r.st.c[3] / alpha, 0.0f, 1.0f);
               sp.bold = r.st.bold;
               sp.italic = r.st.italic;
               sp.underline = r.st.underline;
               req.spans.push_back(sp);
               at += r.len;
            }
         }
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
         if (boxW > 0.0f)
         {
            x0 = std::max(0, (cw - (int)std::ceil(boxW)) / 2);
            x1 = std::min(cw - 1, x0 + (int)std::ceil(boxW) - 1);
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
               tb.rgba[i] = (uint8_t)std::lround(tb.rgba[i] * a);         // Offset X, and optical alignment (a leading quote, dash or bracket hangs into the margin):
         // the ink moves by padding the side away from it, twice over (the box stays centred on the layer).
         float shiftX = l.offsetX;
         if (l.optical && !l.text.empty() && std::strchr("\"'([{-.,", l.text[0]) != nullptr)
            shiftX -= size * 0.25f;
         else if (l.optical && l.text.size() >= 3 && (unsigned char)l.text[0] == 0xE2 && (unsigned char)l.text[1] == 0x80)
            shiftX -= size * 0.25f;   // curly quotes, en/em dash
         const int padX = std::min(4096, (int)std::lround(std::fabs(shiftX) * 2.0f));
         if (padX > 0)
         {
            const int nw = tb.w + padX, off = shiftX > 0.0f ? padX : 0;
            std::vector<uint8_t> moved((size_t)nw * tb.h * 4, 0);
            for (int y = 0; y < tb.h; y++)
               std::memcpy(&moved[((size_t)y * nw + off) * 4], &tb.rgba[(size_t)y * tb.w * 4], (size_t)tb.w * 4);
            tb.rgba.swap(moved);
            tb.w = nw;
         }
      }
      }
      tb.gw = tb.w;
      tb.gh = tb.h;
      if (l.warpStyle != 0)
         WarpTypeRaster(l, tb);
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
   unsigned long long prefix = 0;
   bool fixedStroke = false;
   float strokeK = 1.0f;
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
      const auto& all = mask ? l.vmask : l.shapes;
      for (size_t i = 0; i < all.size(); i++)
      {
         const Shape& s = all[i];
         if (i + 1 == all.size())
            prefix = key;
         key = HashShape(key, s);
         if (!s.scaleStroke && s.strokeOn)
            fixedStroke = true;
      }
      // Strokes that keep their width when the layer scales are drawn thinner in the layer's box.
      if (fixedStroke)
      {
         strokeK = 1.0f / std::sqrt(std::max(1e-4f, std::fabs(l.sx * l.sy)));
         key = H(key, (unsigned)std::lround(strokeK * 4096.0f));
      }
   }
   if (mask)
      key = H(key, (unsigned)std::lround(l.vmaskFeather * 64.0f));
   key = H(H(H(key, (unsigned)w), (unsigned)h), (unsigned)l.aaMode);
   Raster& r = in[l.id];
   if (r.key == key && r.tex != 0)
      return &r;
   r.key = key;
   // Antialiasing off: every edge pixel is in or out (straight RGBA, so the colour stays).
   auto hard = [&](std::vector<uint8_t>& px) {
      if (l.aaMode == 2)
         for (size_t i = 3; i < px.size(); i += 4)
            px[i] = px[i] >= 128 ? 255 : 0;
   };
   // Feathered vector masks: a Gaussian of sigma = Feather on the alpha, Photoshop's measured ramp
   // (PhotoCraft feather_matches_photoshops_ramp: within 1 level).
   auto featherAlpha = [&](std::vector<uint8_t>& px) {
      if (!mask || l.vmaskFeather <= 0.25f || w <= 0 || h <= 0)
         return;
      const size_t npix = (size_t)w * (size_t)h;
      std::vector<float> a(npix);
      for (size_t i = 0; i < npix; i++)
         a[i] = (float)px[i * 4 + 3];
      SelectMaths::GaussianBlur(a, w, h, l.vmaskFeather);
      for (size_t i = 0; i < npix; i++)
         px[i * 4 + 3] = (uint8_t)std::clamp((int)std::lround(a[i]), 0, 255);
   };
   if (l.kind == Kind::Text && !mask)
   {
      if (l.aaMode == 2)
      {
         std::vector<uint8_t> px = mTextBoxes[l.id].rgba;
         hard(px);
         Upload(r.tex, r.w, r.h, w, h, px);
      }
      else
         Upload(r.tex, r.w, r.h, w, h, mTextBoxes[l.id].rgba);
   }
   else
   {
      const auto& all = mask ? l.vmask : l.shapes;
      const Shape* last = all.empty() ? nullptr : &all.back();
      float lo[2], hi[2];
      // A shape being drawn or dragged is the top one (Illustrator redraws only what changed): the shapes under
      // it are drawn once, and each frame only the last shape's box is redrawn, converted and uploaded.
      const bool local = !(mask && l.vmaskFeather > 0.25f) && last != nullptr && last->op == Shape::kAdd && last->type != Shape::kSvg &&
                         last->noise <= 0.0f && ShapeBounds(*last, lo, hi);
      const unsigned long long bk = H(H(H(H(prefix, (unsigned)w), (unsigned)h), (unsigned)l.aaMode), (unsigned)std::lround(strokeK * 4096.0f) + (mask ? 7u : 0u));
      if (!local)
      {
         r.base.clear();
         r.base.shrink_to_fit();
         r.work.clear();
         r.work.shrink_to_fit();
         r.baseKey = 0;
         std::vector<uint8_t> px;
         RasterShapes(all, w, h, mask, px, strokeK);
         hard(px);
         featherAlpha(px);
         Upload(r.tex, r.w, r.h, w, h, px);
      }
      else
      {
         const size_t bytes = (size_t)w * h * 4;
         const bool reuse = r.baseKey == bk && r.base.size() == bytes && r.work.size() == bytes && r.tex != 0 && r.w == w && r.h == h;
         if (!reuse)
         {
            r.base.assign(bytes, 0);
            RasterShapesArgb(all.data(), all.size() - 1, w, h, mask, r.base.data(), strokeK);
            r.work = r.base;
            r.baseKey = bk;
            r.dirty[0] = 0;
            r.dirty[1] = 0;
            r.dirty[2] = w;
            r.dirty[3] = h;
         }
         // The last shape's box this frame (stroke joins and fixed-width strokes padded), unioned with last frame's.
         const float pad = 2.0f + last->width * std::max(1.0f, strokeK) * std::max(1.0f, std::min(last->miter, 4.0f));
         int nb[4] = { std::clamp((int)std::floor(lo[0] - pad), 0, w), std::clamp((int)std::floor(lo[1] - pad), 0, h),
                       std::clamp((int)std::ceil(hi[0] + pad), 0, w), std::clamp((int)std::ceil(hi[1] + pad), 0, h) };
         int d[4] = { std::min(nb[0], r.dirty[0]), std::min(nb[1], r.dirty[1]), std::max(nb[2], r.dirty[2]), std::max(nb[3], r.dirty[3]) };
         if (reuse)
         {
            // Put back what the last frame's shape covered.
            for (int y = r.dirty[1]; y < r.dirty[3]; y++)
               std::memcpy(&r.work[((size_t)y * w + r.dirty[0]) * 4], &r.base[((size_t)y * w + r.dirty[0]) * 4], (size_t)(r.dirty[2] - r.dirty[0]) * 4);
         }
         else
         {
            d[0] = 0;
            d[1] = 0;
            d[2] = w;
            d[3] = h;
         }
         {
            plutovg_surface_t* ws = plutovg_surface_create_for_data(r.work.data(), w, h, w * 4);
            plutovg_canvas_t* wc = plutovg_canvas_create(ws);
            DrawItem(wc, *last, mask, strokeK);
            plutovg_canvas_destroy(wc);
            plutovg_surface_destroy(ws);
         }
         r.dirty[0] = nb[0];
         r.dirty[1] = nb[1];
         r.dirty[2] = nb[2];
         r.dirty[3] = nb[3];
         const int dw = d[2] - d[0], dh = d[3] - d[1];
         if (dw > 0 && dh > 0)
         {
            std::vector<uint8_t> px((size_t)dw * dh * 4);
            // bottom-up rows, as Upload flips
            for (int y = 0; y < dh; y++)
               plutovg_convert_argb_to_rgba(&px[(size_t)(dh - 1 - y) * dw * 4], &r.work[((size_t)(d[1] + y) * w + d[0]) * 4], dw, 1, w * 4);
            hard(px);
            if (r.tex == 0 || r.w != w || r.h != h)
            {
               std::vector<uint8_t> full((size_t)w * h * 4);
               plutovg_convert_argb_to_rgba(full.data(), r.work.data(), w, h, w * 4);
               hard(full);
               Upload(r.tex, r.w, r.h, w, h, full);
            }
            else
            {
               glBindTexture(GL_TEXTURE_2D, r.tex);
               glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
               glTexSubImage2D(GL_TEXTURE_2D, 0, d[0], h - d[3], dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
               glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
               glBindTexture(GL_TEXTURE_2D, 0);
            }
         }
      }
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
            GLUtil::RetireTexture(it->second.tex); // its thumbnail may be queued this frame
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

bool Scene2DNode::RasterizeLayer(int id)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || (l->kind != Kind::Vector && l->kind != Kind::Text))
      return false;
   Raster* r = SyncRaster(mVec, *l, false);
   if (r == nullptr || r->tex == 0 || r->w <= 0 || r->h <= 0)
      return false;
   // The picture exactly as the compositor reads it, cut into the paint layer's tiles.
   const int w = r->w, h = r->h, kT = TileStore::kTile, cols = (w + kT - 1) / kT, rows = (h + kT - 1) / kT;
   std::vector<uint8_t> px((size_t)w * h * 4), tile;
   glBindTexture(GL_TEXTURE_2D, r->tex);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
   glBindTexture(GL_TEXTURE_2D, 0);
   std::vector<std::string> keys((size_t)cols * rows, "-");
   for (size_t i = 0; i < keys.size(); i++)
   {
      const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
      const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
      tile.resize((size_t)tw * th * 4);
      bool any = false;
      for (int y = 0; y < th; y++)
      {
         std::memcpy(&tile[(size_t)y * tw * 4], &px[((size_t)(y0 + y) * w + x0) * 4], (size_t)tw * 4);
         for (int x = 0; x < tw && !any; x++)
            any = tile[((size_t)y * tw + x) * 4 + 3] != 0;
      }
      if (any)
         keys[i] = TileStore::Put(tile.data(), tw, th);
   }
   if (l->name.empty())
      l->name = l->kind == Kind::Text ? TextName(l->text, 24) : std::string();
   l->kind = Kind::Paint;
   l->pw = w;
   l->ph = h;
   l->tiles = std::move(keys);
   l->shapes.clear();
   l->text.clear();
   Commit();
   return true;
}

namespace
{
   // A document-size picture (rows top-down) cut into paint tiles (GL rows, bottom first); "-" for an empty tile.
   std::vector<std::string> TilesOfPicture(const std::vector<uint8_t>& top, int w, int h, bool& anyPixel)
   {
      const int kT = TileStore::kTile, cols = (w + kT - 1) / kT, rows = (h + kT - 1) / kT;
      std::vector<uint8_t> tile;
      std::vector<std::string> keys((size_t)cols * rows, "-");
      anyPixel = false;
      for (size_t i = 0; i < keys.size(); i++)
      {
         const int x0 = (int)(i % cols) * kT, y0 = (int)(i / cols) * kT;
         const int tw = std::min(kT, w - x0), th = std::min(kT, h - y0);
         tile.resize((size_t)tw * th * 4);
         bool any = false;
         for (int y = 0; y < th; y++)
         {
            const int srcRow = h - 1 - (y0 + y);
            std::memcpy(&tile[(size_t)y * tw * 4], &top[((size_t)srcRow * w + x0) * 4], (size_t)tw * 4);
            for (int x = 0; x < tw && !any; x++)
               any = tile[((size_t)y * tw + x) * 4 + 3] != 0;
         }
         if (any)
         {
            keys[i] = TileStore::Put(tile.data(), tw, th);
            anyPixel = true;
         }
      }
      return keys;
   }
}

// An image / node layer turned into pixels: the layer drawn alone at document size becomes a paint layer in its place
// (name, opacity, blend and place in the stack kept), so the eraser, wand and retouch tools can work on it.
int Scene2DNode::RasterizeNodeLayer(int id, int frameId)
{
   Layers();
   const Layer* src = FindConst(id);
   if (src == nullptr || src->kind != Kind::Node)
      return 0;
   std::vector<uint8_t> top;
   if (!RenderLayerAlone(id, frameId, top) || top.size() != (size_t)mDoc.w * mDoc.h * 4)
      return 0;
   bool anyPixel = false;
   std::vector<std::string> keys = TilesOfPicture(top, mDoc.w, mDoc.h, anyPixel);
   // Nothing visible inside the document (the picture is off the page, or the node gave no frame): keep the layer
   // as it is rather than swap it for an empty one that looks like it vanished.
   if (!anyPixel)
      return 0;
   const std::string name = src->name;
   const float opacity = src->opacity;
   const int blend = src->blend;
   const bool visible = src->visible;
   Layer& nl = AddLayer(Kind::Paint, 0, id);
   nl.tiles = std::move(keys);
   nl.name = name;
   nl.opacity = opacity;
   nl.blend = blend;
   nl.visible = visible;
   const int nid = nl.id;
   RemoveLayer(id);
   Commit();
   return nid;
}

// Merge (Photoshop's Merge Down / Merge Layers / Merge Visible / Flatten): sibling layers drawn together, as they look,
// become one Normal paint layer in the lowest one's place, with its name. Any kind merges (text, shapes, nodes, groups,
// adjustments). A hidden layer in the set is dropped, as Photoshop drops it. The new id, 0 when it cannot.
int Scene2DNode::MergeLayers(const std::vector<int>& ids, int frameId)
{
   Layers();
   std::vector<int> set;
   int parent = -1;
   for (int id : ids)
   {
      const Layer* l = FindConst(id);
      if (l == nullptr)
         continue;
      if (parent < 0)
         parent = l->parent;
      else if (l->parent != parent)
         return 0;   // one level at a time
      set.push_back(id);
   }
   if (set.empty())
      return 0;
   int lowest = 0;
   for (int c : ChildrenOf(parent))
      if (std::find(set.begin(), set.end(), c) != set.end())
      {
         lowest = c;
         break;
      }
   if (lowest == 0)
      return 0;
   const int W = mDoc.w, H = mDoc.h;
   // Pixels past the canvas are kept (PhotoCraft #5067): the frame is the canvas grown to every merged layer's box
   // (plus room for strokes and glows), rendered a canvas-sized tile at a time with the layers shifted under it.
   // A fill in the set covers the whole document, so it keeps the canvas frame.
   float lo[2] = { 0, 0 }, hi[2] = { (float)W, (float)H };
   bool fill = false;
   {
      std::vector<int> todo;
      todo.reserve(set.size());
      for (int id : set)
         todo.push_back(id);
      while (!todo.empty())
      {
         const int id = todo.back();
         todo.pop_back();
         const Layer* l = FindConst(id);
         if (l == nullptr || !l->visible)
            continue;
         if (l->kind == Kind::Group)
         {
            for (int c : ChildrenOf(id))
               todo.push_back(c);
            continue;
         }
         fill = fill || l->kind == Kind::Fill;
         if (l->kind == Kind::Fill || l->kind == Kind::Adjust)
            continue;
         float c[8];
         if (!Corners(*l, c))
            continue;
         const float pad = (l->kind == Kind::Paint && !l->HasEffects() && !l->AnyFx()) ? 0.0f : 64.0f;
         for (int k = 0; k < 4; k++)
         {
            lo[0] = std::min(lo[0], c[k * 2] - pad), hi[0] = std::max(hi[0], c[k * 2] + pad);
            lo[1] = std::min(lo[1], c[k * 2 + 1] - pad), hi[1] = std::max(hi[1], c[k * 2 + 1] + pad);
         }
      }
   }
   int ox = 0, oy = 0, bw = W, bh = H;
   if (!fill)
   {
      ox = (int)std::ceil(std::max(0.0f, -lo[0]));
      oy = (int)std::ceil(std::max(0.0f, -lo[1]));
      bw = std::min(GLUtil::kMaxTargetSide, ox + std::max(W, (int)std::ceil(hi[0])));
      bh = std::min(GLUtil::kMaxTargetSide, oy + std::max(H, (int)std::ceil(hi[1])));
      if (bw - ox < W || bh - oy < H)   // too big to hold: the canvas alone
         ox = oy = 0, bw = W, bh = H;
   }
   // The lowest layer's blend stays on the result (PhotoCraft merge_down): it is drawn Normal into the merge, so a
   // Multiply base keeps multiplying what is under it instead of turning into a Normal copy of how it looked.
   Layer* low = FindLayer(lowest);
   const int lowBlend = low->kind == Kind::Group ? 0 : low->blend;
   const int keepBlend = low->blend;
   if (lowBlend != 0)
      low->blend = 0;
   std::vector<uint8_t> big((size_t)bw * bh * 4, 0);
   bool ok = true;
   for (int ty = 0; ty < bh && ok; ty += H)
      for (int tx = 0; tx < bw && ok; tx += W)
      {
         const float dx = (float)(ox - tx), dy = (float)(oy - ty);   // document point p lands at p + (ox, oy) - (tx, ty)
         const bool shift = dx != 0.0f || dy != 0.0f;
         std::vector<Layer> keep;
         if (shift)
         {
            keep = mLayers;
            for (Layer& l : mLayers)
               if (l.kind != Kind::Fill && l.kind != Kind::Group && l.kind != Kind::Adjust)
                  l.x += dx, l.y += dy;
         }
         std::vector<uint8_t> top;
         ok = RenderSet(set, frameId + (ty / H) * 64 + tx / W, top) && top.size() == (size_t)W * H * 4;
         if (shift)
            mLayers = keep;
         for (int y = 0; ok && y < H && ty + y < bh; y++)
         {
            const int n = std::min(W, bw - tx);
            std::memcpy(&big[((size_t)(ty + y) * bw + tx) * 4], &top[(size_t)y * W * 4], (size_t)n * 4);
         }
      }
   if (FindLayer(lowest) != nullptr)
      FindLayer(lowest)->blend = keepBlend;
   if (!ok)
      return 0;
   bool anyPixel = false;
   std::vector<std::string> keys = TilesOfPicture(big, bw, bh, anyPixel);
   const std::string name = FindConst(lowest)->name;
   Layer& nl = AddLayer(Kind::Paint, 0, lowest);
   nl.pw = bw;
   nl.ph = bh;
   nl.x = (float)-ox + bw * 0.5f;
   nl.y = (float)-oy + bh * 0.5f;
   nl.tiles = std::move(keys);
   nl.name = name;
   nl.blend = lowBlend;
   const int nid = nl.id;
   for (int s : set)
      RemoveLayer(s);
   Commit();
   return nid;
}

bool Scene2DNode::StampLayerInto(int from, int into, int frameId)
{
   Layers();
   const Layer* d0 = FindConst(into);
   Layer* f = FindLayer(from);
   if (d0 == nullptr || f == nullptr || from == into || d0->kind != Kind::Paint || (d0->lock & (kLockPixels | kLockAll)) != 0)
      return false;
   // Drawn plain: the source's masks, styles and effects still apply to it once it is back in the source.
   f->effects.clear();
   for (Fx& x : f->fx)
      x.on = false;
   for (auto& m : f->fxMore)
      m.second.on = false;
   f->pmaskOn = f->vmaskOn = false;
   f->maskUid = 0;
   f->relief = false;
   f->fill = 1.0f;
   std::fill(f->bifT, f->bifT + 4, 0.0f), f->bifT[2] = f->bifT[3] = 1.0f;
   std::fill(f->bifU, f->bifU + 4, 0.0f), f->bifU[2] = f->bifU[3] = 1.0f;
   const int W = mDoc.w, H = mDoc.h;
   // A paint piece is read from its own pixels, so what was dragged or turned past the canvas edge is kept; any
   // other layer is drawn alone into the document first.
   std::vector<uint8_t> pic;
   int pw = W, ph = H;
   float fbw = 1, fbh = 1;
   const bool own = f->kind == Kind::Paint && ReadPaint(from, pic, pw, ph) && pic.size() == (size_t)pw * ph * 4 &&
                    LayerBox(*f, fbw, fbh);
   if (!own)
   {
      pw = W, ph = H;
      if (!RenderLayerAlone(from, frameId, pic) || pic.size() != (size_t)W * H * 4)
         return false;
   }
   std::vector<uint8_t> px;
   int w = 0, h = 0;
   if (!ReadPaint(into, px, w, h) || px.size() != (size_t)w * h * 4)
      return false;
   const Layer dst = *FindConst(into);
   const Layer piece = *FindConst(from);
   float bw = (float)w, bh = (float)h;
   LayerBox(dst, bw, bh);
   // Each of the layer's pixels reads the piece where it lands (bilinear, premultiplied), Normal over.
   auto texel = [&](int x, int y, float out[4]) {   // y top down
      if (x < 0 || y < 0 || x >= pw || y >= ph)
         return;
      const uint8_t* s = &pic[((size_t)(own ? ph - 1 - y : y) * pw + x) * 4];   // paint surfaces run bottom first
      const float a = s[3] / 255.0f;
      out[0] = s[0] * a, out[1] = s[1] * a, out[2] = s[2] * a, out[3] = a;
   };
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
         float dx, dy;
         BoxToDoc(dst, (x + 0.5f) * bw / (float)w, ((float)(h - 1 - y) + 0.5f) * bh / (float)h, dx, dy);   // surface rows run bottom first
         if (own)
         {
            float qx, qy;
            DocToBox(piece, dx, dy, qx, qy);
            dx = qx * (float)pw / std::max(1e-6f, fbw);
            dy = qy * (float)ph / std::max(1e-6f, fbh);
         }
         dx -= 0.5f;
         dy -= 0.5f;
         const int x0 = (int)std::floor(dx), y0 = (int)std::floor(dy);
         if (x0 < -1 || y0 < -1 || x0 >= pw || y0 >= ph)
            continue;
         const float fx = dx - x0, fy = dy - y0;
         float c[4][4] = {};
         texel(x0, y0, c[0]);
         texel(x0 + 1, y0, c[1]);
         texel(x0, y0 + 1, c[2]);
         texel(x0 + 1, y0 + 1, c[3]);
         float s[4];
         for (int k = 0; k < 4; k++)
            s[k] = (c[0][k] * (1 - fx) + c[1][k] * fx) * (1 - fy) + (c[2][k] * (1 - fx) + c[3][k] * fx) * fy;
         if (s[3] <= 0.5f / 255.0f)
            continue;
         uint8_t* p = &px[((size_t)y * w + x) * 4];
         const float da = p[3] / 255.0f, oa = s[3] + da * (1.0f - s[3]);
         for (int k = 0; k < 3; k++)
            p[k] = (uint8_t)std::clamp(std::lround((s[k] + p[k] * da * (1.0f - s[3])) / oa), 0L, 255L);
         p[3] = (uint8_t)std::clamp(std::lround(oa * 255.0f), 0L, 255L);
      }
   std::vector<uint8_t> top((size_t)w * h * 4);
   for (int y = 0; y < h; y++)
      std::memcpy(&top[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
   bool anyPixel = false;
   std::vector<std::string> keys = TilesOfPicture(top, w, h, anyPixel);
   FindLayer(into)->tiles = std::move(keys);
   RemoveLayer(from, false);
   Commit();
   return true;
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
   int maskN = 0, gradN = 0;
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
         else if (s.type == Shape::kPolygon || s.type == Shape::kStar || s.type == Shape::kTriangle)
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
         float lo[2], hi[2];
         if (fill && s.fillType > 0 && s.fillType < 4 && ShapeBounds(s, lo, hi))
         {
            // The same geometry as SetGradient, in the box's own 0..1 units.
            const std::string gid = "g" + std::to_string(++gradN);
            const float a = s.fillAngle * kPi / 180.0f, dx = std::cos(a), dy = std::sin(a);
            const float k = (std::fabs(dx) + std::fabs(dy)) * 0.5f;
            if (s.fillType == 2)
               el << "<radialGradient id=\"" << gid << "\" cx=\"0.5\" cy=\"0.5\" r=\"0.5\">";
            else
               el << "<linearGradient id=\"" << gid << "\" x1=\"" << (s.fillType == 3 ? 0.5f : 0.5f - dx * k) << "\" y1=\""
                  << (s.fillType == 3 ? 0.5f : 0.5f - dy * k) << "\" x2=\"" << 0.5f + dx * k << "\" y2=\"" << 0.5f + dy * k << "\""
                  << (s.fillType == 3 ? " spreadMethod=\"reflect\"" : "") << ">";
            for (const GradStop& g : FillStops(s))
               el << "<stop offset=\"" << g.t << "\" stop-color=\"" << Hex(g.c) << "\" stop-opacity=\"" << g.c[3] << "\"/>";
            el << (s.fillType == 2 ? "</radialGradient>\n" : "</linearGradient>\n");
            el << geo << " fill=\"url(#" << gid << ")\"";
         }
         else
         {
            el << geo << " fill=\"" << (fill ? Hex(s.fillC) : "none") << "\"";
            if (fill && s.fillC[3] < 0.999f)
               el << " fill-opacity=\"" << s.fillC[3] << "\"";
         }
         if (s.strokeOn && s.width > 0)
         {
            el << " stroke=\"" << Hex(s.strokeC) << "\" stroke-width=\"" << s.width << "\" stroke-linecap=\""
               << caps[std::clamp(s.cap, 0, 2)] << "\" stroke-linejoin=\"" << joins[std::clamp(s.join, 0, 2)] << "\"";
            if (s.strokeC[3] < 0.999f)
               el << " stroke-opacity=\"" << s.strokeC[3] << "\"";
            if (s.dash > 0)
               el << " stroke-dasharray=\"" << s.dash << " " << (s.gap > 0 ? s.gap : s.dash) << "\"";
            if (s.dash > 0 && s.dashPhase != 0.0f)
               el << " stroke-dashoffset=\"" << s.dashPhase << "\"";
            if (s.join == 0 && s.miter != 4.0f)
               el << " stroke-miterlimit=\"" << s.miter << "\"";
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

void Scene2DNode::CopyShapeStyle(const Shape& from, Shape& to)
{
   to.fillOn = from.fillOn;
   std::copy(from.fillC, from.fillC + 4, to.fillC);
   to.fillType = from.fillType;
   to.fillAngle = from.fillAngle;
   to.fillStops = from.fillStops;
   to.fillImage = from.fillImage;
   to.imageMode = from.imageMode;
   std::copy(&from.meshC[0][0], &from.meshC[0][0] + 16, &to.meshC[0][0]);
   to.noise = from.noise;
   to.strokeOn = from.strokeOn;
   std::copy(from.strokeC, from.strokeC + 4, to.strokeC);
   to.width = from.width;
   to.cap = from.cap;
   to.join = from.join;
   to.align = from.align;
   to.dash = from.dash;
   to.gap = from.gap;
   to.dashPhase = from.dashPhase;
   to.balanced = from.balanced;
   to.miter = from.miter;
   to.behind = from.behind;
   to.scaleStroke = from.scaleStroke;
}

int Scene2DNode::CombineShapeLayers(const std::vector<int>& ids, int op)
{
   Layers();
   if (op < Shape::kAdd || op > Shape::kXor)
      return 0;
   // The vector layers among the ids, bottom to top in the stack.
   std::vector<int> order;
   for (const Layer& l : mLayers)
      if (l.kind == Kind::Vector && std::find(ids.begin(), ids.end(), l.id) != ids.end())
         order.push_back(l.id);
   if (order.size() < 2)
      return 0;
   auto locked = [&](const Layer& l) {
      unsigned lk = l.lock;
      for (int p = l.parent, guard = 0; p != 0 && guard < 64; guard++)
      {
         const Layer* g = FindConst(p);
         if (g == nullptr)
            break;
         lk |= g->lock;
         p = g->parent;
      }
      return (lk & (kLockAll | kLockPixels)) != 0;
   };
   for (int id : order)
      if (locked(*FindConst(id)))
         return 0;
   const int keep = order.back();
   float t[6];
   LayerMatrix(*FindConst(keep), t);
   const float det = t[0] * t[4] - t[1] * t[3];
   if (!std::isfinite(det) || std::fabs(det) < 1e-8f)
      return 0;
   // Inverse of the top layer's box -> document map.
   const float inv[6] = { t[4] / det, -t[1] / det, (t[1] * t[5] - t[4] * t[2]) / det,
                          -t[3] / det, t[0] / det, (t[3] * t[2] - t[0] * t[5]) / det };
   Shape style;
   if (!FindConst(keep)->shapes.empty())
      style = FindConst(keep)->shapes.front();
   std::vector<Shape> merged;
   for (size_t li = 0; li < order.size(); li++)
   {
      const Layer& src = *FindConst(order[li]);
      float a[6];
      LayerMatrix(src, a);
      // m = inv * a: the source box -> the top layer's box.
      const float m[6] = { inv[0] * a[0] + inv[1] * a[3], inv[0] * a[1] + inv[1] * a[4], inv[0] * a[2] + inv[1] * a[5] + inv[2],
                           inv[3] * a[0] + inv[4] * a[3], inv[3] * a[1] + inv[4] * a[4], inv[3] * a[2] + inv[4] * a[5] + inv[5] };
      const float eps = 1e-5f;
      const bool axial = std::fabs(m[1]) < eps && std::fabs(m[3]) < eps;
      const bool same = axial && std::fabs(m[0] - 1.0f) < eps && std::fabs(m[4] - 1.0f) < eps;   // a pure move
      const float k = std::sqrt(std::fabs(m[0] * m[4] - m[1] * m[3]));
      auto map = [&](float& x, float& y) {
         const float nx = m[0] * x + m[1] * y + m[2], ny = m[3] * x + m[4] * y + m[5];
         x = nx;
         y = ny;
      };
      for (size_t j = 0; j < src.shapes.size(); j++)
      {
         Shape s = src.shapes[j];
         if (!same)
         {
            if (s.type == Shape::kSvg)
            {
               // An SVG item keeps its box; only a move or a positive axis-aligned scale maps it exactly.
               if (!axial || m[0] <= 0.0f || m[4] <= 0.0f)
                  return 0;
            }
            else if (s.type != Shape::kPath && !ShapeToPath(s))
               continue;   // an empty live shape draws nothing
         }
         if (s.type == Shape::kPath)
            for (size_t p = 0; p + 1 < s.pts.size(); p += 2)
               map(s.pts[p], s.pts[p + 1]);
         else
         {
            map(s.x0, s.y0);
            map(s.x1, s.y1);
         }
         if (s.scaleStroke)
            s.width *= k;
         if (order[li] != keep)
            CopyShapeStyle(style, s);
         if (li > 0)
            s.op = op;   // the bottom layer's items keep their own ops
         merged.push_back(std::move(s));
      }
   }
   Layer* top = FindLayer(keep);
   top->shapes = std::move(merged);
   for (int id : order)
      if (id != keep)
         RemoveLayer(id);
   Commit();
   return keep;
}

unsigned Scene2DNode::VectorMaskTexture(int id)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || !l->hasVMask || l->vmask.empty())
      return 0;
   Raster* r = SyncRaster(mVMasks, *l, true);
   return r != nullptr ? r->tex : 0;
}

bool Scene2DNode::ShapeToPath(Shape& s)
{
   if (s.type == Shape::kPath)
      return s.Anchors() >= 2;
   const float x = std::min(s.x0, s.x1), y = std::min(s.y0, s.y1);
   const float w = std::fabs(s.x1 - s.x0), h = std::fabs(s.y1 - s.y0);
   std::vector<float> pts;
   auto pushAnchor = [&](float px, float py, float ix, float iy, float ox, float oy) {
      pts.insert(pts.end(), { px, py, ix, iy, ox, oy });
   };
   auto pushLineVerts = [&](const std::vector<float>& xy) {
      const size_t n = xy.size() / 2;
      for (size_t i = 0; i < n; i++)
         pushAnchor(xy[i * 2], xy[i * 2 + 1], xy[i * 2], xy[i * 2 + 1], xy[i * 2], xy[i * 2 + 1]);
   };
   bool closed = true;
   switch (s.type)
   {
   case Shape::kRect:
      if (w <= 0.0f || h <= 0.0f)
         return false;
      if (CustomCorners(s))
      {
         std::vector<float> xy;
         RectCorners(s, xy);
         pushLineVerts(xy);
      }
      else if (s.radius > 0.0f)
      {
         const float r = std::min(s.radius, std::min(w, h) * 0.5f);
         const float k = r * 0.55228475f;
         const float x1 = x + w, y1 = y + h;
         pushAnchor(x + r, y, x + r - k, y, x + r, y);
         pushAnchor(x1 - r, y, x1 - r, y, x1 - r + k, y);
         pushAnchor(x1, y + r, x1, y + r - k, x1, y + r);
         pushAnchor(x1, y1 - r, x1, y1 - r, x1, y1 - r + k);
         pushAnchor(x1 - r, y1, x1 - r + k, y1, x1 - r, y1);
         pushAnchor(x + r, y1, x + r, y1, x + r - k, y1);
         pushAnchor(x, y1 - r, x, y1 - r + k, x, y1 - r);
         pushAnchor(x, y + r, x, y + r, x, y + r - k);
      }
      else
      {
         pushLineVerts({ x, y, x + w, y, x + w, y + h, x, y + h });
      }
      break;
   case Shape::kEllipse:
   {
      if (w <= 0.0f || h <= 0.0f)
         return false;
      const float cx = x + w * 0.5f, cy = y + h * 0.5f, rx = w * 0.5f, ry = h * 0.5f;
      const float kx = rx * 0.55228475f, ky = ry * 0.55228475f;
      pushAnchor(cx, y, cx - kx, y, cx + kx, y);
      pushAnchor(x + w, cy, x + w, cy - ky, x + w, cy + ky);
      pushAnchor(cx, y + h, cx + kx, y + h, cx - kx, y + h);
      pushAnchor(x, cy, x, cy + ky, x, cy - ky);
      break;
   }
   case Shape::kPolygon:
   case Shape::kStar:
   case Shape::kTriangle:
   {
      if (w <= 0.0f || h <= 0.0f)
         return false;
      std::vector<float> xy;
      StarPoints(s, s.type == Shape::kStar, xy);
      if (xy.size() < 6)
         return false;
      pushLineVerts(xy);
      break;
   }
   case Shape::kLine:
      closed = false;
      pushAnchor(s.x0, s.y0, s.x0, s.y0, s.x0, s.y0);
      pushAnchor(s.x1, s.y1, s.x1, s.y1, s.x1, s.y1);
      break;
   default:
      return false;
   }
   s.type = Shape::kPath;
   s.closed = closed;
   s.pts = std::move(pts);
   return s.Anchors() >= 2;
}

int Scene2DNode::SplitSegment(Shape& s, int seg, float t)
{
   if (s.type != Shape::kPath || seg < 0 || seg >= s.Anchors())
      return -1;
   const int n = s.Anchors(), j = (seg + 1) % n;
   const float ct = std::clamp(t, 0.05f, 0.95f), u = 1.0f - ct;
   const float* a = &s.pts[(size_t)seg * 6];
   const float* b = &s.pts[(size_t)j * 6];
   auto mixp = [&](float p0, float p1) { return p0 * u + p1 * ct; };
   const float q0x = mixp(a[0], a[4]), q0y = mixp(a[1], a[5]);
   const float q1x = mixp(a[4], b[2]), q1y = mixp(a[5], b[3]);
   const float q2x = mixp(b[2], b[0]), q2y = mixp(b[3], b[1]);
   const float r0x = mixp(q0x, q1x), r0y = mixp(q0y, q1y);
   const float r1x = mixp(q1x, q2x), r1y = mixp(q1y, q2y);
   const float mx = mixp(r0x, r1x), my = mixp(r0y, r1y);
   s.pts[(size_t)seg * 6 + 4] = q0x;
   s.pts[(size_t)seg * 6 + 5] = q0y;
   s.pts[(size_t)j * 6 + 2] = q2x;
   s.pts[(size_t)j * 6 + 3] = q2y;
   const size_t pos = (size_t)(seg + 1);
   s.pts.insert(s.pts.begin() + (ptrdiff_t)(pos * 6), { mx, my, r0x, r0y, r1x, r1y });
   return seg + 1;
}

void Scene2DNode::SubdividePath(Shape& s)
{
   if (s.type != Shape::kPath && !ShapeToPath(s))
      return;
   const int n = s.Anchors();
   if (n < 2)
      return;
   const int segs = s.closed ? n : n - 1;
   std::vector<float> pts(s.pts.begin(), s.pts.begin() + (size_t)n * 6);
   std::vector<float> out;
   out.reserve((size_t)(n + segs) * 6);
   auto lerp = [](float a, float b) { return 0.5f * (a + b); };
   for (int i = 0; i < n; i++)
   {
      out.insert(out.end(), pts.begin() + i * 6, pts.begin() + i * 6 + 6);
      if (i >= segs)
         continue;
      const int j = (i + 1) % n;
      const float x0 = pts[i * 6 + 0], y0 = pts[i * 6 + 1];
      const float c1x = pts[i * 6 + 4], c1y = pts[i * 6 + 5];
      const float c2x = pts[j * 6 + 2], c2y = pts[j * 6 + 3];
      const float x3 = pts[j * 6 + 0], y3 = pts[j * 6 + 1];
      const float q0x = lerp(x0, c1x), q0y = lerp(y0, c1y);
      const float q1x = lerp(c1x, c2x), q1y = lerp(c1y, c2y);
      const float q2x = lerp(c2x, x3), q2y = lerp(c2y, y3);
      const float r0x = lerp(q0x, q1x), r0y = lerp(q0y, q1y);
      const float r1x = lerp(q1x, q2x), r1y = lerp(q1y, q2y);
      const float mx = lerp(r0x, r1x), my = lerp(r0y, r1y);
      // Adjust current out-handle and next in-handle, and insert split anchor in between.
      const size_t curBase = out.size() - 6;
      out[curBase + 4] = q0x;
      out[curBase + 5] = q0y;
      pts[j * 6 + 2] = q2x;
      pts[j * 6 + 3] = q2y;
      if (j == 0 && !out.empty())
      {
         out[2] = q2x;
         out[3] = q2y;
      }
      out.insert(out.end(), { mx, my, r0x, r0y, r1x, r1y });
   }
   s.pts = std::move(out);
}

void Scene2DNode::DeformShape(Shape& s, int mode, float amount, float freq)
{
   if (s.type != Shape::kPath && !ShapeToPath(s))
      return;
   for (int pass = 0; pass < 3 && s.Anchors() < 24; pass++)
      SubdividePath(s);
   const int n = s.Anchors();
   if (n < 2)
      return;
   float lo[2], hi[2];
   if (!ShapeBounds(s, lo, hi))
      return;
   const float cx = 0.5f * (lo[0] + hi[0]), cy = 0.5f * (lo[1] + hi[1]);
   const float span = std::max(4.0f, 0.5f * std::hypot(hi[0] - lo[0], hi[1] - lo[1]));
   const float f = std::max(0.25f, freq);
   auto warpPt = [&](float px, float py, int seedIdx, float& ox, float& oy) {
      const float dx = px - cx, dy = py - cy;
      const float nx = dx / span, ny = dy / span;
      switch (mode)
      {
      case 0:   // Wave
      {
         const float ph = (nx * 0.75f + ny * 0.65f) * f * 2.0f * kPi;
         ox = px + std::sin(ph) * amount;
         oy = py + std::cos(ny * f * 2.0f * kPi) * amount * 0.75f;
         break;
      }
      case 1:   // Roughen
      {
         const float h1 = std::sin((float)(seedIdx * 127 + 13) * 1.37f + nx * 19.3f * f + ny * 7.1f);
         const float h2 = std::cos((float)(seedIdx * 269 + 37) * 0.91f + ny * 23.7f * f - nx * 11.3f);
         ox = px + h1 * amount;
         oy = py + h2 * amount;
         break;
      }
      case 2:   // Twist
      {
         const float dist = std::hypot(nx, ny);
         const float ang = (amount * 0.025f) * dist * f;
         const float ca = std::cos(ang), sa = std::sin(ang);
         ox = cx + dx * ca - dy * sa;
         oy = cy + dx * sa + dy * ca;
         break;
      }
      default:   // Bulge / Pinch
      {
         const float dist = std::min(1.5f, std::hypot(nx, ny));
         const float scale = 1.0f + (amount / span) * (1.0f - dist * dist * 0.65f);
         ox = cx + dx * scale;
         oy = cy + dy * scale;
         break;
      }
      }
   };
   for (int i = 0; i < n; i++)
   {
      float* p = &s.pts[(size_t)i * 6];
      const float ax = p[0], ay = p[1];
      float nax = ax, nay = ay;
      warpPt(ax, ay, i * 3, nax, nay);
      const float shiftX = nax - ax, shiftY = nay - ay;
      if (mode == 2 || mode == 3)
      {
         warpPt(p[2], p[3], i * 3 + 1, p[2], p[3]);
         warpPt(p[4], p[5], i * 3 + 2, p[4], p[5]);
      }
      else
      {
         p[2] += shiftX;
         p[3] += shiftY;
         p[4] += shiftX;
         p[5] += shiftY;
      }
      p[0] = nax;
      p[1] = nay;
   }
}

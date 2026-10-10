#include "Sketch3DEngine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

extern "C"
{
#include "quickjs.h"
}

namespace
{
constexpr size_t kMemLimit = 64u * 1024 * 1024;
constexpr size_t kStackLimit = 1024 * 1024;
constexpr int kDrawBudgetMs = 50;
constexpr int kCompileBudgetMs = 200;
constexpr float kPi = 3.14159265358979f;

struct Rng
{
   uint32_t s = 1;
   float Next()
   {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      return (s & 0xFFFFFF) / 16777216.0f;
   }
};

float Hash3(int x, int y, int z)
{
   uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
   h = (h ^ (h >> 13)) * 1274126177u;
   return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
float Fade(float t) { return t * t * (3 - 2 * t); }
float ValueNoise(float x, float y, float z)
{
   int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
   float xf = Fade(x - xi), yf = Fade(y - yi), zf = Fade(z - zi);
   auto l = [](float a, float b, float t) { return a + (b - a) * t; };
   float c[2][2];
   float r[2];
   for (int dz = 0; dz < 2; ++dz)
   {
      for (int dy = 0; dy < 2; ++dy)
         c[dy][0] = l(Hash3(xi, yi + dy, zi + dz), Hash3(xi + 1, yi + dy, zi + dz), xf);
      r[dz] = l(c[0][0], c[1][0], yf);
   }
   return l(r[0], r[1], zf);
}

float SrgbToLinear(float c)
{
   return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

struct V3 { float x = 0, y = 0, z = 0; };
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Norm(V3 a)
{
   const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
   return l > 1e-12f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 1, 0};
}
} // namespace

struct Program
{
   JSRuntime* rt = nullptr;
   JSContext* ctx = nullptr;
   JSValue drawFn = JS_UNDEFINED;
   std::vector<SketchParamDecl> params;
   std::chrono::steady_clock::time_point deadline;

   // Per-run state.
   Mesh mesh;
   Mat4 cur;
   std::vector<Mat4> stack;
   float fill[3] = {1, 1, 1};
   bool usedFill = false;
   Rng rng;
   int shapeKind = -1;             // -1 = not inside beginShape
   std::vector<V3> shape;
   std::vector<float> shapeUv;
   bool tooBig = false;

   ~Program() { Free(); }
   void Free()
   {
      if (ctx)
      {
         JS_FreeValue(ctx, drawFn);
         JS_FreeContext(ctx);
      }
      if (rt) JS_FreeRuntime(rt);
      ctx = nullptr; rt = nullptr; drawFn = JS_UNDEFINED;
   }

   // ---- mesh building -------------------------------------------------------
   // Local-space position + normal, transformed by the current matrix. The normal uses the
   // cofactor matrix (inverse transpose, scaled), so non-uniform scale stays correct.
   unsigned int Vert(V3 p, V3 n, float u, float v)
   {
      const float* m = cur.m;
      Vertex o;
      o.px = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
      o.py = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
      o.pz = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
      // cofactor of the upper 3x3 (column-major: m[col*4 + row])
      const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
      const float c00 = e * i - f * h, c01 = f * g - d * i, c02 = d * h - e * g;
      const float c10 = c * h - b * i, c11 = a * i - c * g, c12 = b * g - a * h;
      const float c20 = b * f - c * e, c21 = c * d - a * f, c22 = a * e - b * d;
      const float sg = Mirrored() ? -1.f : 1.f;   // cofactor carries sign(det)
      V3 t = Norm({sg * (c00 * n.x + c01 * n.y + c02 * n.z), sg * (c10 * n.x + c11 * n.y + c12 * n.z),
                   sg * (c20 * n.x + c21 * n.y + c22 * n.z)});
      o.nx = t.x; o.ny = t.y; o.nz = t.z; o.u = u; o.v = v;
      if (mesh.vertices.size() >= Sketch3DEngine::kMaxVertices) { tooBig = true; return 0; }
      mesh.vertices.push_back(o);
      mesh.vertexColor.push_back(fill[0]);
      mesh.vertexColor.push_back(fill[1]);
      mesh.vertexColor.push_back(fill[2]);
      return (unsigned int)mesh.vertices.size() - 1;
   }
   bool Mirrored() const
   {
      const float* m = cur.m;
      const float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
                        m[8] * (m[1] * m[6] - m[5] * m[2]);
      return det < 0;
   }
   void Tri(unsigned int a, unsigned int b, unsigned int c)
   {
      if (tooBig) return;
      if (mesh.indices.size() + 3 > Sketch3DEngine::kMaxIndices) { tooBig = true; return; }
      if (Mirrored()) std::swap(b, c);
      mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c);
   }
   // (rows+1) x (cols+1) grid; f(i,j) -> position/normal/uv. Wound so that d/di x d/dj points
   // inward unless `flip`.
   void Grid(int rows, int cols, bool flip, const std::function<void(int, int, V3&, V3&, float&, float&)>& f)
   {
      const unsigned int base = (unsigned int)mesh.vertices.size();
      for (int i = 0; i <= rows; ++i)
         for (int j = 0; j <= cols; ++j)
         {
            V3 p, n; float u, v;
            f(i, j, p, n, u, v);
            Vert(p, n, u, v);
         }
      if (tooBig) return;
      for (int i = 0; i < rows; ++i)
         for (int j = 0; j < cols; ++j)
         {
            const unsigned int a = base + i * (cols + 1) + j, b = a + cols + 1;
            if (!flip) { Tri(a, a + 1, b); Tri(a + 1, b + 1, b); }
            else       { Tri(a, b, a + 1); Tri(a + 1, b, b + 1); }
         }
   }
   void Quad(V3 c, V3 u, V3 v, V3 n)
   {
      const unsigned int a = Vert({c.x - u.x - v.x, c.y - u.y - v.y, c.z - u.z - v.z}, n, 0, 0);
      const unsigned int b = Vert({c.x + u.x - v.x, c.y + u.y - v.y, c.z + u.z - v.z}, n, 1, 0);
      const unsigned int d = Vert({c.x + u.x + v.x, c.y + u.y + v.y, c.z + u.z + v.z}, n, 1, 1);
      const unsigned int e = Vert({c.x - u.x + v.x, c.y - u.y + v.y, c.z - u.z + v.z}, n, 0, 1);
      Tri(a, b, d); Tri(a, d, e);
   }
   // Fan cap at height y facing up (+1) or down (-1).
   void Cap(float y, float r, int seg, bool up)
   {
      const V3 n{0, up ? 1.f : -1.f, 0};
      const unsigned int c = Vert({0, y, 0}, n, 0.5f, 0.5f);
      std::vector<unsigned int> ring;
      for (int j = 0; j <= seg; ++j)
      {
         const float phi = 2 * kPi * j / seg;
         ring.push_back(Vert({r * std::cos(phi), y, r * std::sin(phi)}, n, 0.5f + 0.5f * std::cos(phi),
                             0.5f + 0.5f * std::sin(phi)));
      }
      for (int j = 0; j < seg; ++j)
         if (up) Tri(c, ring[j + 1], ring[j]);
         else    Tri(c, ring[j], ring[j + 1]);
   }
};

struct Sketch3DEngine::Impl
{
   Program* prog = nullptr;
   ~Impl() { delete prog; }
};

namespace
{
Program* P(JSContext* c) { return (Program*)JS_GetContextOpaque(c); }

void Reg(JSContext* c, JSValue g, const char* name, JSCFunction* f, int len)
{
   JS_SetPropertyStr(c, g, name, JS_NewCFunction(c, f, name, len));
}

bool Num(JSContext* c, JSValueConst v, float& out)
{
   double d;
   if (JS_ToFloat64(c, &d, v)) return false;
   out = (float)d;
   return true;
}

#define FN(name) static JSValue name(JSContext* c, JSValueConst, int argc, JSValueConst* argv)
#define NEEDN(k) if (argc < (k)) return JS_ThrowTypeError(c, "expected %d arguments", (k))
#define GETF(i, var) float var; if (!Num(c, argv[i], var)) return JS_EXCEPTION
#define OPTF(i, var, def) float var = (def); if (argc > (i) && !JS_IsUndefined(argv[i]) && !Num(c, argv[i], var)) return JS_EXCEPTION
#define CHECK_SIZE(p) if ((p)->tooBig) return JS_ThrowRangeError(c, "mesh too large (limit %d vertices)", (int)Sketch3DEngine::kMaxVertices)

int Detail(float d, int lo, int hi) { return std::max(lo, std::min(hi, (int)std::lround(d))); }

// ---- colour ---------------------------------------------------------------------
FN(js_fill)
{
   Program* p = P(c);
   float v[3] = {0, 0, 0};
   if (argc >= 1 && JS_IsArray(argv[0]))
   {
      for (int i = 0; i < 3; ++i)
      {
         JSValue e = JS_GetPropertyUint32(c, argv[0], i);
         bool ok = Num(c, e, v[i]);
         JS_FreeValue(c, e);
         if (!ok) return JS_ThrowTypeError(c, "fill([r, g, b])");
      }
   }
   else if (argc == 1) { GETF(0, g); v[0] = v[1] = v[2] = g; }
   else if (argc >= 3) { for (int i = 0; i < 3; ++i) if (!Num(c, argv[i], v[i])) return JS_EXCEPTION; }
   else return JS_ThrowTypeError(c, "fill(gray) or fill(r, g, b), 0..1");
   for (int i = 0; i < 3; ++i) p->fill[i] = SrgbToLinear(std::min(1.f, std::max(0.f, v[i])));
   p->usedFill = true;
   return JS_UNDEFINED;
}
FN(js_hsl)
{
   NEEDN(3); GETF(0, h); GETF(1, s); GETF(2, l);
   h = h - std::floor(h);
   s = std::min(1.f, std::max(0.f, s)); l = std::min(1.f, std::max(0.f, l));
   auto f = [&](float n) {
      float k = std::fmod(n + h * 12.f, 12.f);
      float a = s * std::min(l, 1 - l);
      return l - a * std::max(-1.f, std::min(std::min(k - 3, 9 - k), 1.f));
   };
   JSValue arr = JS_NewArray(c);
   JS_SetPropertyUint32(c, arr, 0, JS_NewFloat64(c, f(0)));
   JS_SetPropertyUint32(c, arr, 1, JS_NewFloat64(c, f(8)));
   JS_SetPropertyUint32(c, arr, 2, JS_NewFloat64(c, f(4)));
   return arr;
}

// ---- transforms -----------------------------------------------------------------
FN(js_push) { if (P(c)->stack.size() < 256) P(c)->stack.push_back(P(c)->cur); return JS_UNDEFINED; } // depth cap: a push() loop can't eat memory
FN(js_pop)
{
   Program* p = P(c);
   if (!p->stack.empty()) { p->cur = p->stack.back(); p->stack.pop_back(); }
   return JS_UNDEFINED;
}
void Apply(Program* p, const Mat4& m) { p->cur = Mat4::Multiply(p->cur, m); }
FN(js_translate)
{
   NEEDN(2); GETF(0, x); GETF(1, y); OPTF(2, z, 0);
   Apply(P(c), Mat4::Translation(x, y, z));
   return JS_UNDEFINED;
}
FN(js_rotateX) { NEEDN(1); GETF(0, a); Apply(P(c), Mat4::RotationX(a)); return JS_UNDEFINED; }
FN(js_rotateY) { NEEDN(1); GETF(0, a); Apply(P(c), Mat4::RotationY(a)); return JS_UNDEFINED; }
FN(js_rotateZ) { NEEDN(1); GETF(0, a); Apply(P(c), Mat4::RotationZ(a)); return JS_UNDEFINED; }
FN(js_rotate)   // rotate(angle) about Z, or rotate(angle, ax, ay, az) about an axis
{
   NEEDN(1); GETF(0, a);
   if (argc < 4) { Apply(P(c), Mat4::RotationZ(a)); return JS_UNDEFINED; }
   GETF(1, x); GETF(2, y); GETF(3, z);
   const V3 k = Norm({x, y, z});
   const float cs = std::cos(a), sn = std::sin(a), t = 1 - cs;
   Mat4 m;   // column-major
   m.m[0] = cs + k.x * k.x * t;        m.m[4] = k.x * k.y * t - k.z * sn;  m.m[8]  = k.x * k.z * t + k.y * sn;
   m.m[1] = k.y * k.x * t + k.z * sn;  m.m[5] = cs + k.y * k.y * t;        m.m[9]  = k.y * k.z * t - k.x * sn;
   m.m[2] = k.z * k.x * t - k.y * sn;  m.m[6] = k.z * k.y * t + k.x * sn;  m.m[10] = cs + k.z * k.z * t;
   Apply(P(c), m);
   return JS_UNDEFINED;
}
FN(js_scale)
{
   NEEDN(1); GETF(0, x);
   float y = x, z = x;
   if (argc >= 2) { if (!Num(c, argv[1], y)) return JS_EXCEPTION; z = 1; }
   if (argc >= 3) { if (!Num(c, argv[2], z)) return JS_EXCEPTION; }
   Apply(P(c), Mat4::Scale(x, y, z));
   return JS_UNDEFINED;
}

// ---- primitives -----------------------------------------------------------------
FN(js_box)
{
   Program* p = P(c);
   OPTF(0, w, 1); OPTF(1, h, w); OPTF(2, d, h);
   const float x = w / 2, y = h / 2, z = d / 2;
   p->Quad({0, 0, z}, {x, 0, 0}, {0, y, 0}, {0, 0, 1});
   p->Quad({0, 0, -z}, {-x, 0, 0}, {0, y, 0}, {0, 0, -1});
   p->Quad({x, 0, 0}, {0, 0, -z}, {0, y, 0}, {1, 0, 0});
   p->Quad({-x, 0, 0}, {0, 0, z}, {0, y, 0}, {-1, 0, 0});
   p->Quad({0, y, 0}, {x, 0, 0}, {0, 0, -z}, {0, 1, 0});
   p->Quad({0, -y, 0}, {x, 0, 0}, {0, 0, z}, {0, -1, 0});
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}
FN(js_plane)   // XY plane facing +z
{
   Program* p = P(c);
   OPTF(0, w, 1); OPTF(1, h, w);
   p->Quad({0, 0, 0}, {w / 2, 0, 0}, {0, h / 2, 0}, {0, 0, 1});
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}
FN(js_sphere)
{
   Program* p = P(c);
   OPTF(0, r, 0.5f); OPTF(1, dt, 24);
   const int rows = Detail(dt, 3, 128), cols = rows * 2;
   p->Grid(rows, cols, false, [&](int i, int j, V3& pos, V3& n, float& u, float& v) {
      const float th = kPi * i / rows, ph = 2 * kPi * j / cols;
      n = {std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
      pos = {n.x * r, n.y * r, n.z * r};
      u = (float)j / cols; v = (float)i / rows;
   });
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}
FN(js_cylinder)
{
   Program* p = P(c);
   OPTF(0, r, 0.5f); OPTF(1, h, 1); OPTF(2, dt, 24);
   const int seg = Detail(dt, 3, 256);
   p->Grid(1, seg, false, [&](int i, int j, V3& pos, V3& n, float& u, float& v) {
      const float ph = 2 * kPi * j / seg;
      n = {std::cos(ph), 0, std::sin(ph)};
      pos = {r * n.x, h * (0.5f - i), r * n.z};
      u = (float)j / seg; v = (float)i;
   });
   p->Cap(h / 2, r, seg, true);
   p->Cap(-h / 2, r, seg, false);
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}
FN(js_cone)
{
   Program* p = P(c);
   OPTF(0, r, 0.5f); OPTF(1, h, 1); OPTF(2, dt, 24);
   const int seg = Detail(dt, 3, 256);
   p->Grid(1, seg, false, [&](int i, int j, V3& pos, V3& n, float& u, float& v) {
      const float ph = 2 * kPi * j / seg;
      const float rr = r * i;   // apex at i=0, base at i=1
      n = Norm({h * std::cos(ph), r, h * std::sin(ph)});
      pos = {rr * std::cos(ph), h * (0.5f - i), rr * std::sin(ph)};
      u = (float)j / seg; v = (float)i;
   });
   p->Cap(-h / 2, r, seg, false);
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}
FN(js_torus)
{
   Program* p = P(c);
   OPTF(0, R, 0.5f); OPTF(1, r, 0.2f); OPTF(2, dt, 24);
   const int cols = Detail(dt, 3, 256), rows = std::max(3, cols / 2);
   p->Grid(rows, cols, true, [&](int i, int j, V3& pos, V3& n, float& u, float& v) {
      const float vv = 2 * kPi * i / rows, uu = 2 * kPi * j / cols;
      n = {std::cos(vv) * std::cos(uu), std::sin(vv), std::cos(vv) * std::sin(uu)};
      pos = {(R + r * std::cos(vv)) * std::cos(uu), r * std::sin(vv), (R + r * std::cos(vv)) * std::sin(uu)};
      u = (float)j / cols; v = (float)i / rows;
   });
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}
FN(js_tube)   // tube(x1,y1,z1, x2,y2,z2, radius, sides): a capless cylinder between two points
{
   Program* p = P(c);
   NEEDN(6);
   GETF(0, x1); GETF(1, y1); GETF(2, z1); GETF(3, x2); GETF(4, y2); GETF(5, z2);
   OPTF(6, r, 0.02f); OPTF(7, sd, 8);
   const int seg = Detail(sd, 3, 64);
   const V3 a{x1, y1, z1}, b{x2, y2, z2};
   const V3 ax = Norm(Sub(b, a));
   const V3 ref = std::fabs(ax.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0};
   const V3 s = Norm(Cross(ax, ref)), t = Cross(ax, s);
   p->Grid(1, seg, false, [&](int i, int j, V3& pos, V3& n, float& u, float& v) {
      const float ph = 2 * kPi * j / seg;
      n = {s.x * std::cos(ph) + t.x * std::sin(ph), s.y * std::cos(ph) + t.y * std::sin(ph),
           s.z * std::cos(ph) + t.z * std::sin(ph)};
      const V3& e = i == 0 ? a : b;
      pos = {e.x + r * n.x, e.y + r * n.y, e.z + r * n.z};
      u = (float)j / seg; v = (float)i;
   });
   CHECK_SIZE(p);
   return JS_UNDEFINED;
}

// ---- custom shapes --------------------------------------------------------------
FN(js_beginShape)
{
   Program* p = P(c);
   float k = 0;
   if (argc > 0 && !JS_IsUndefined(argv[0]) && !Num(c, argv[0], k)) return JS_EXCEPTION;
   if (k < 0 || k > 3) return JS_ThrowRangeError(c, "beginShape(TRIANGLES | TRIANGLE_STRIP | TRIANGLE_FAN | QUADS)");
   p->shapeKind = (int)k;
   p->shape.clear();
   p->shapeUv.clear();
   return JS_UNDEFINED;
}
FN(js_vertex)
{
   Program* p = P(c);
   if (p->shapeKind < 0) return JS_ThrowTypeError(c, "vertex() outside beginShape()");
   NEEDN(3); GETF(0, x); GETF(1, y); GETF(2, z);
   OPTF(3, u, 0); OPTF(4, v, 0);
   if (p->shape.size() >= Sketch3DEngine::kMaxVertices) return JS_ThrowRangeError(c, "shape too large");
   p->shape.push_back({x, y, z});
   p->shapeUv.push_back(u); p->shapeUv.push_back(v);
   return JS_UNDEFINED;
}
FN(js_endShape)
{
   Program* p = P(c);
   if (p->shapeKind < 0) return JS_ThrowTypeError(c, "endShape() without beginShape()");
   std::vector<std::array<int, 3>> tris;
   const int n = (int)p->shape.size();
   switch (p->shapeKind)
   {
      case 0: for (int i = 0; i + 2 < n; i += 3) tris.push_back({i, i + 1, i + 2}); break;
      case 1: for (int i = 0; i + 2 < n; ++i) tris.push_back(i % 2 ? std::array<int, 3>{i + 1, i, i + 2} : std::array<int, 3>{i, i + 1, i + 2}); break;
      case 2: for (int i = 1; i + 1 < n; ++i) tris.push_back({0, i, i + 1}); break;
      case 3: for (int i = 0; i + 3 < n; i += 4) { tris.push_back({i, i + 1, i + 2}); tris.push_back({i, i + 2, i + 3}); } break;
   }
   for (const auto& t : tris)
   {
      const V3 &a = p->shape[t[0]], &b = p->shape[t[1]], &d = p->shape[t[2]];
      const V3 fn = Norm(Cross(Sub(b, a), Sub(d, a)));
      const unsigned int ia = p->Vert(a, fn, p->shapeUv[t[0] * 2], p->shapeUv[t[0] * 2 + 1]);
      const unsigned int ib = p->Vert(b, fn, p->shapeUv[t[1] * 2], p->shapeUv[t[1] * 2 + 1]);
      const unsigned int id = p->Vert(d, fn, p->shapeUv[t[2] * 2], p->shapeUv[t[2] * 2 + 1]);
      p->Tri(ia, ib, id);
      CHECK_SIZE(p);
   }
   p->shapeKind = -1;
   p->shape.clear();
   p->shapeUv.clear();
   return JS_UNDEFINED;
}

// ---- random / noise / params ----------------------------------------------------
FN(js_random)
{
   Program* p = P(c);
   float r = p->rng.Next();
   if (argc == 1) { GETF(0, hi); return JS_NewFloat64(c, r * hi); }
   if (argc >= 2) { GETF(0, lo); GETF(1, hi); return JS_NewFloat64(c, lo + r * (hi - lo)); }
   return JS_NewFloat64(c, r);
}
FN(js_noise)
{
   float x = 0, y = 0, z = 0;
   if (argc > 0 && !Num(c, argv[0], x)) return JS_EXCEPTION;
   if (argc > 1 && !Num(c, argv[1], y)) return JS_EXCEPTION;
   if (argc > 2 && !Num(c, argv[2], z)) return JS_EXCEPTION;
   return JS_NewFloat64(c, ValueNoise(x, y, z));
}
FN(js_param)
{
   Program* p = P(c);
   if (argc < 4) return JS_ThrowTypeError(c, "param(name, default, lo, hi)");
   const char* n = JS_ToCString(c, argv[0]);
   if (!n) return JS_EXCEPTION;
   SketchParamDecl d;
   d.name = n;
   JS_FreeCString(c, n);
   bool ok = Num(c, argv[1], d.def) && Num(c, argv[2], d.lo) && Num(c, argv[3], d.hi);
   if (!ok) return JS_EXCEPTION;
   if (d.name.empty() || !(std::isalpha((unsigned char)d.name[0]) || d.name[0] == '_'))
      return JS_ThrowTypeError(c, "param name must be an identifier");
   if (d.hi < d.lo) std::swap(d.lo, d.hi);
   d.def = std::min(d.hi, std::max(d.lo, d.def));
   for (auto& e : p->params)
      if (e.name == d.name) { e = d; return JS_UNDEFINED; }
   if (p->params.size() >= 32) return JS_ThrowRangeError(c, "at most 32 params");
   p->params.push_back(d);
   return JS_UNDEFINED;
}

const char* kPrelude = R"JS(
const PI = Math.PI, TAU = Math.PI * 2, HALF_PI = Math.PI / 2;
const TRIANGLES = 0, TRIANGLE_STRIP = 1, TRIANGLE_FAN = 2, QUADS = 3;
const sin = Math.sin, cos = Math.cos, tan = Math.tan, atan2 = Math.atan2, atan = Math.atan,
      asin = Math.asin, acos = Math.acos, sqrt = Math.sqrt, abs = Math.abs, floor = Math.floor,
      ceil = Math.ceil, round = Math.round, pow = Math.pow, exp = Math.exp, log = Math.log,
      min = Math.min, max = Math.max, sign = Math.sign;
const console = { log() {}, warn() {}, error() {} };
const lerp = (a, b, t) => a + (b - a) * t;
const constrain = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const map = (v, a, b, c, d) => c + (d - c) * ((v - a) / (b - a));
const dist = (x1, y1, z1, x2, y2, z2) => Math.hypot(x2 - x1, y2 - y1, (z2 || 0) - (z1 || 0));
const radians = (d) => d * Math.PI / 180, degrees = (r) => r * 180 / Math.PI;
)JS";

void ExtractError(JSContext* c, JSValue ex, SketchError& err)
{
   const char* msg = JS_ToCString(c, ex);
   err.message = msg ? msg : "error";
   if (msg) JS_FreeCString(c, msg);
   JSValue stack = JS_GetPropertyStr(c, ex, "stack");
   if (!JS_IsUndefined(stack))
   {
      const char* st = JS_ToCString(c, stack);
      if (st)
      {
         const char* at = std::strstr(st, "sketch:");
         if (at) std::sscanf(at, "sketch:%d:%d", &err.line, &err.col);
         JS_FreeCString(c, st);
      }
   }
   JS_FreeValue(c, stack);
   if (err.message.find("interrupted") != std::string::npos)
      err.message = "sketch took too long (over 50 ms) and was stopped";
}

int Interrupt(JSContext*, void* op)
{
   return std::chrono::steady_clock::now() > ((Program*)op)->deadline ? 1 : 0;
}
} // namespace

Sketch3DEngine::Sketch3DEngine() : mImpl(new Impl) {}
Sketch3DEngine::~Sketch3DEngine() { delete mImpl; }
bool Sketch3DEngine::HasProgram() const { return mImpl->prog && mImpl->prog->ctx; }
const std::vector<SketchParamDecl>& Sketch3DEngine::Params() const
{
   static const std::vector<SketchParamDecl> none;
   return mImpl->prog ? mImpl->prog->params : none;
}

bool Sketch3DEngine::Compile(const std::string& code, SketchError& err)
{
   err = SketchError{};
   auto prog = std::make_unique<Program>();
   prog->rt = JS_NewRuntime();
   if (!prog->rt) { err.message = "could not create JS runtime"; return false; }
   JS_SetMemoryLimit(prog->rt, kMemLimit);
   JS_SetMaxStackSize(prog->rt, kStackLimit);
   JS_SetInterruptHandler(prog->rt, Interrupt, prog.get());
   prog->ctx = JS_NewContext(prog->rt);
   if (!prog->ctx) { err.message = "could not create JS context"; return false; }
   JSContext* c = prog->ctx;
   JS_SetContextOpaque(c, prog.get());

   JSValue g = JS_GetGlobalObject(c);
   Reg(c, g, "fill", js_fill, 1);
   Reg(c, g, "hsl", js_hsl, 3);
   Reg(c, g, "push", js_push, 0);
   Reg(c, g, "pop", js_pop, 0);
   Reg(c, g, "translate", js_translate, 3);
   Reg(c, g, "rotate", js_rotate, 4);
   Reg(c, g, "rotateX", js_rotateX, 1);
   Reg(c, g, "rotateY", js_rotateY, 1);
   Reg(c, g, "rotateZ", js_rotateZ, 1);
   Reg(c, g, "scale", js_scale, 3);
   Reg(c, g, "box", js_box, 3);
   Reg(c, g, "plane", js_plane, 2);
   Reg(c, g, "sphere", js_sphere, 2);
   Reg(c, g, "cylinder", js_cylinder, 3);
   Reg(c, g, "cone", js_cone, 3);
   Reg(c, g, "torus", js_torus, 3);
   Reg(c, g, "tube", js_tube, 8);
   Reg(c, g, "beginShape", js_beginShape, 1);
   Reg(c, g, "vertex", js_vertex, 5);
   Reg(c, g, "endShape", js_endShape, 0);
   Reg(c, g, "random", js_random, 2);
   Reg(c, g, "noise", js_noise, 3);
   Reg(c, g, "param", js_param, 4);
   JS_FreeValue(c, g);

   const std::string pre = std::string(kPrelude) + "Math.random = () => random();\n";
   prog->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompileBudgetMs);
   JSValue r = JS_Eval(c, pre.c_str(), pre.size(), "prelude", JS_EVAL_TYPE_GLOBAL);
   if (JS_IsException(r))
   {
      JSValue ex = JS_GetException(c);
      ExtractError(c, ex, err);
      JS_FreeValue(c, ex);
      err.line = 0;
      return false;
   }
   JS_FreeValue(c, r);

   prog->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompileBudgetMs);
   r = JS_Eval(c, code.c_str(), code.size(), "sketch", JS_EVAL_TYPE_GLOBAL);
   if (JS_IsException(r))
   {
      JSValue ex = JS_GetException(c);
      ExtractError(c, ex, err);
      JS_FreeValue(c, ex);
      return false;
   }
   JS_FreeValue(c, r);

   g = JS_GetGlobalObject(c);
   prog->drawFn = JS_GetPropertyStr(c, g, "draw");
   JS_FreeValue(c, g);
   if (!JS_IsFunction(c, prog->drawFn))
   {
      err.message = "define a function draw(t) { ... }";
      return false;
   }
   delete mImpl->prog;
   mImpl->prog = prog.release();
   return true;
}

bool Sketch3DEngine::Run(const Frame& f, Mesh& out, SketchError& err)
{
   err = SketchError{};
   Program* p = mImpl->prog;
   if (!p || !p->ctx) { err.message = "no program"; return false; }
   JSContext* c = p->ctx;

   p->mesh = Mesh();
   p->cur = Mat4::Identity();
   p->stack.clear();
   p->shape.clear();
   p->shapeUv.clear();
   p->shapeKind = -1;
   p->fill[0] = p->fill[1] = p->fill[2] = 1;
   p->usedFill = false;
   p->tooBig = false;
   p->rng.s = (f.seed * 2654435761u) ^ ((uint32_t)f.frame * 40503u + 1u);
   if (!p->rng.s) p->rng.s = 1;

   JSValue g = JS_GetGlobalObject(c);
   JS_SetPropertyStr(c, g, "t", JS_NewFloat64(c, f.t));
   JS_SetPropertyStr(c, g, "beat", JS_NewFloat64(c, f.beat));
   JS_SetPropertyStr(c, g, "frame", JS_NewInt32(c, f.frame));
   for (const auto& d : p->params)
   {
      float v = d.def;
      if (f.params)
      {
         auto it = f.params->find(d.name);
         if (it != f.params->end()) v = std::min(d.hi, std::max(d.lo, it->second));
      }
      JS_SetPropertyStr(c, g, d.name.c_str(), JS_NewFloat64(c, v));
   }
   JS_FreeValue(c, g);

   p->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDrawBudgetMs);
   JSValue targ = JS_NewFloat64(c, f.t);
   JSValue r = JS_Call(c, p->drawFn, JS_UNDEFINED, 1, &targ);
   JS_FreeValue(c, targ);
   const bool ok = !JS_IsException(r);
   if (!ok)
   {
      JSValue ex = JS_GetException(c);
      ExtractError(c, ex, err);
      JS_FreeValue(c, ex);
   }
   JS_FreeValue(c, r);
   if (!ok) return false;

   if (!p->usedFill) p->mesh.vertexColor.clear();
   out = std::move(p->mesh);
   p->mesh = Mesh();
   return true;
}

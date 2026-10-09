#include "SketchEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

extern "C"
{
#include "quickjs.h"
#include "plutovg.h"
}

namespace
{
constexpr size_t kMemLimit = 64u * 1024 * 1024;
constexpr size_t kStackLimit = 1024 * 1024;
constexpr int kDrawBudgetMs = 50;      // S8: abort a draw() past this
constexpr int kCompileBudgetMs = 200;

struct DrawState
{
   bool fillOn = true, strokeOn = false;
   float fill[4] = {1, 1, 1, 1};
   float stroke[4] = {0, 0, 0, 1};
   float weight = 1;
   float textSize = 24;
};

// Small xorshift so random() is reproducible per (node seed, frame).
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
} // namespace

// One compiled program: its own runtime so a failed recompile can't disturb the
// working one.
struct Program
{
   JSRuntime* rt = nullptr;
   JSContext* ctx = nullptr;
   JSValue drawFn = JS_UNDEFINED;
   std::vector<SketchParamDecl> params;
   std::chrono::steady_clock::time_point deadline;

   // Per-run drawing state.
   plutovg_surface_t* surf = nullptr;
   plutovg_canvas_t* cv = nullptr;
   plutovg_font_face_t* font = nullptr;
   DrawState st;
   std::vector<DrawState> stack;
   Rng rng;
   bool inShape = false;
   std::vector<std::pair<float, float>> shape;
   int W = 0, H = 0;

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
};

struct SketchEngine::Impl
{
   Program* prog = nullptr;
   std::string fontPath;
   plutovg_font_face_t* font = nullptr;
   ~Impl()
   {
      delete prog;
      if (font) plutovg_font_face_destroy(font);
   }
};

namespace
{
Program* P(JSContext* c) { return (Program*)JS_GetContextOpaque(c); }

bool Num(JSContext* c, JSValueConst v, float& out)
{
   double d;
   if (JS_ToFloat64(c, &d, v)) return false;
   out = (float)d;
   return true;
}

// p5-style colour args, 0..1: (g) (g,a) (r,g,b) (r,g,b,a) or ([r,g,b(,a)]).
bool ColorArgs(JSContext* c, int argc, JSValueConst* argv, float out[4])
{
   float v[4] = {0, 0, 0, 1};
   int n = 0;
   if (argc >= 1 && JS_IsArray(argv[0]))
   {
      for (int i = 0; i < 4; ++i)
      {
         JSValue e = JS_GetPropertyUint32(c, argv[0], i);
         if (JS_IsUndefined(e)) { JS_FreeValue(c, e); break; }
         bool ok = Num(c, e, v[i]);
         JS_FreeValue(c, e);
         if (!ok) return false;
         ++n;
      }
      if (n == 1) { v[1] = v[2] = v[0]; }
      else if (n < 3) return false;
   }
   else
   {
      for (int i = 0; i < argc && i < 4; ++i)
         if (!Num(c, argv[i], v[i])) return false;
      n = std::min(argc, 4);
      if (n == 1) { v[1] = v[2] = v[0]; }
      else if (n == 2) { v[3] = v[1]; v[1] = v[2] = v[0]; }
      else if (n < 1) return false;
   }
   for (int i = 0; i < 4; ++i) out[i] = std::min(1.f, std::max(0.f, v[i]));
   return true;
}

#define FN(name) static JSValue name(JSContext* c, JSValueConst, int argc, JSValueConst* argv)
#define NEEDN(k) if (argc < (k)) return JS_ThrowTypeError(c, "expected %d arguments", (k))
#define GETF(i, var) float var; if (!Num(c, argv[i], var)) return JS_EXCEPTION

void Paint(Program* p)
{
   plutovg_canvas_t* cv = p->cv;
   if (p->st.fillOn)
   {
      plutovg_canvas_set_rgba(cv, p->st.fill[0], p->st.fill[1], p->st.fill[2], p->st.fill[3]);
      plutovg_canvas_fill_preserve(cv);
   }
   if (p->st.strokeOn)
   {
      plutovg_canvas_set_rgba(cv, p->st.stroke[0], p->st.stroke[1], p->st.stroke[2], p->st.stroke[3]);
      plutovg_canvas_set_line_width(cv, p->st.weight);
      plutovg_canvas_stroke_preserve(cv);
   }
   plutovg_canvas_new_path(cv);
}

FN(js_background)
{
   Program* p = P(c);
   float col[4];
   if (!ColorArgs(c, argc, argv, col)) return JS_ThrowTypeError(c, "background(gray | r,g,b | r,g,b,a)");
   plutovg_canvas_save(p->cv);
   plutovg_matrix_t ident;
   plutovg_matrix_init_identity(&ident);
   plutovg_canvas_set_matrix(p->cv, &ident);
   plutovg_canvas_set_rgba(p->cv, col[0], col[1], col[2], col[3]);
   plutovg_canvas_set_operator(p->cv, PLUTOVG_OPERATOR_SRC);
   plutovg_canvas_fill_rect(p->cv, 0, 0, (float)p->W, (float)p->H);
   plutovg_canvas_restore(p->cv);
   return JS_UNDEFINED;
}
FN(js_fill)
{
   Program* p = P(c);
   if (!ColorArgs(c, argc, argv, p->st.fill)) return JS_ThrowTypeError(c, "fill(gray | r,g,b | r,g,b,a)");
   p->st.fillOn = true;
   return JS_UNDEFINED;
}
FN(js_stroke)
{
   Program* p = P(c);
   if (!ColorArgs(c, argc, argv, p->st.stroke)) return JS_ThrowTypeError(c, "stroke(gray | r,g,b | r,g,b,a)");
   p->st.strokeOn = true;
   return JS_UNDEFINED;
}
FN(js_noFill) { P(c)->st.fillOn = false; return JS_UNDEFINED; }
FN(js_noStroke) { P(c)->st.strokeOn = false; return JS_UNDEFINED; }
FN(js_strokeWeight) { NEEDN(1); GETF(0, w); P(c)->st.weight = std::max(0.f, w); return JS_UNDEFINED; }
FN(js_textSize) { NEEDN(1); GETF(0, s); P(c)->st.textSize = std::max(1.f, s); return JS_UNDEFINED; }
FN(js_push) { Program* p = P(c); p->stack.push_back(p->st); plutovg_canvas_save(p->cv); return JS_UNDEFINED; }
FN(js_pop)
{
   Program* p = P(c);
   if (p->stack.empty()) return JS_ThrowRangeError(c, "pop() without matching push()");
   p->st = p->stack.back(); p->stack.pop_back();
   plutovg_canvas_restore(p->cv);
   return JS_UNDEFINED;
}
FN(js_translate) { NEEDN(2); GETF(0, x); GETF(1, y); plutovg_canvas_translate(P(c)->cv, x, y); return JS_UNDEFINED; }
FN(js_rotate) { NEEDN(1); GETF(0, a); plutovg_canvas_rotate(P(c)->cv, a); return JS_UNDEFINED; }
FN(js_scale)
{
   NEEDN(1); GETF(0, sx);
   float sy = sx;
   if (argc > 1 && !Num(c, argv[1], sy)) return JS_EXCEPTION;
   plutovg_canvas_scale(P(c)->cv, sx, sy);
   return JS_UNDEFINED;
}
FN(js_rect)
{
   NEEDN(4); GETF(0, x); GETF(1, y); GETF(2, w); GETF(3, h);
   Program* p = P(c);
   plutovg_canvas_new_path(p->cv);
   if (argc > 4)
   {
      GETF(4, r);
      plutovg_canvas_round_rect(p->cv, x, y, w, h, r, r);
   }
   else plutovg_canvas_rect(p->cv, x, y, w, h);
   Paint(p);
   return JS_UNDEFINED;
}
// p5: circle(x, y, diameter); ellipse(x, y, w[, h]) full extents.
FN(js_circle)
{
   NEEDN(3); GETF(0, x); GETF(1, y); GETF(2, d);
   Program* p = P(c);
   plutovg_canvas_new_path(p->cv);
   plutovg_canvas_circle(p->cv, x, y, d * 0.5f);
   Paint(p);
   return JS_UNDEFINED;
}
FN(js_ellipse)
{
   NEEDN(3); GETF(0, x); GETF(1, y); GETF(2, w);
   float h = w;
   if (argc > 3 && !Num(c, argv[3], h)) return JS_EXCEPTION;
   Program* p = P(c);
   plutovg_canvas_new_path(p->cv);
   plutovg_canvas_ellipse(p->cv, x, y, w * 0.5f, h * 0.5f);
   Paint(p);
   return JS_UNDEFINED;
}
FN(js_line)
{
   NEEDN(4); GETF(0, x1); GETF(1, y1); GETF(2, x2); GETF(3, y2);
   Program* p = P(c);
   if (!p->st.strokeOn) return JS_UNDEFINED;
   plutovg_canvas_new_path(p->cv);
   plutovg_canvas_move_to(p->cv, x1, y1);
   plutovg_canvas_line_to(p->cv, x2, y2);
   plutovg_canvas_set_rgba(p->cv, p->st.stroke[0], p->st.stroke[1], p->st.stroke[2], p->st.stroke[3]);
   plutovg_canvas_set_line_width(p->cv, p->st.weight);
   plutovg_canvas_stroke(p->cv);
   return JS_UNDEFINED;
}
FN(js_point)
{
   NEEDN(2); GETF(0, x); GETF(1, y);
   Program* p = P(c);
   plutovg_canvas_new_path(p->cv);
   plutovg_canvas_circle(p->cv, x, y, std::max(0.5f, p->st.weight * 0.5f));
   plutovg_canvas_set_rgba(p->cv, p->st.stroke[0], p->st.stroke[1], p->st.stroke[2], p->st.stroke[3]);
   plutovg_canvas_fill(p->cv);
   return JS_UNDEFINED;
}
JSValue PolyN(JSContext* c, int argc, JSValueConst* argv, int pts)
{
   if (argc < pts * 2) return JS_ThrowTypeError(c, "expected %d coordinates", pts * 2);
   Program* p = P(c);
   plutovg_canvas_new_path(p->cv);
   for (int i = 0; i < pts; ++i)
   {
      float x, y;
      if (!Num(c, argv[i * 2], x) || !Num(c, argv[i * 2 + 1], y)) return JS_EXCEPTION;
      if (i == 0) plutovg_canvas_move_to(p->cv, x, y); else plutovg_canvas_line_to(p->cv, x, y);
   }
   plutovg_canvas_close_path(p->cv);
   Paint(p);
   return JS_UNDEFINED;
}
FN(js_triangle) { return PolyN(c, argc, argv, 3); }
FN(js_quad) { return PolyN(c, argc, argv, 4); }
FN(js_bezier)
{
   NEEDN(8);
   float v[8];
   for (int i = 0; i < 8; ++i) if (!Num(c, argv[i], v[i])) return JS_EXCEPTION;
   Program* p = P(c);
   plutovg_canvas_new_path(p->cv);
   plutovg_canvas_move_to(p->cv, v[0], v[1]);
   plutovg_canvas_cubic_to(p->cv, v[2], v[3], v[4], v[5], v[6], v[7]);
   bool wasFill = p->st.fillOn;
   p->st.fillOn = false;                 // p5 bezier() is an open curve: stroke only
   Paint(p);
   p->st.fillOn = wasFill;
   return JS_UNDEFINED;
}
FN(js_beginShape) { Program* p = P(c); p->inShape = true; p->shape.clear(); return JS_UNDEFINED; }
FN(js_vertex)
{
   NEEDN(2); GETF(0, x); GETF(1, y);
   Program* p = P(c);
   if (!p->inShape) return JS_ThrowTypeError(c, "vertex() outside beginShape()");
   if (p->shape.size() < 1000000) p->shape.emplace_back(x, y);
   return JS_UNDEFINED;
}
FN(js_endShape)
{
   Program* p = P(c);
   if (!p->inShape) return JS_ThrowTypeError(c, "endShape() without beginShape()");
   p->inShape = false;
   if (p->shape.size() < 2) return JS_UNDEFINED;
   bool close = false;
   if (argc > 0) { int32_t m = 0; JS_ToInt32(c, &m, argv[0]); close = (m == 1); }   // CLOSE = 1
   plutovg_canvas_new_path(p->cv);
   plutovg_canvas_move_to(p->cv, p->shape[0].first, p->shape[0].second);
   for (size_t i = 1; i < p->shape.size(); ++i)
      plutovg_canvas_line_to(p->cv, p->shape[i].first, p->shape[i].second);
   if (close) plutovg_canvas_close_path(p->cv);
   bool wasFill = p->st.fillOn;
   if (!close) p->st.fillOn = false;     // open shapes stroke only
   Paint(p);
   p->st.fillOn = wasFill;
   p->shape.clear();
   return JS_UNDEFINED;
}
FN(js_text)
{
   NEEDN(3);
   Program* p = P(c);
   const char* s = JS_ToCString(c, argv[0]);
   if (!s) return JS_EXCEPTION;
   float x, y;
   bool ok = Num(c, argv[1], x) && Num(c, argv[2], y);
   if (ok && p->font && p->st.fillOn)
   {
      plutovg_canvas_set_font(p->cv, p->font, p->st.textSize);
      plutovg_canvas_set_rgba(p->cv, p->st.fill[0], p->st.fill[1], p->st.fill[2], p->st.fill[3]);
      plutovg_canvas_fill_text(p->cv, s, -1, PLUTOVG_TEXT_ENCODING_UTF8, x, y);
   }
   JS_FreeCString(c, s);
   return ok ? JS_UNDEFINED : JS_EXCEPTION;
}
FN(js_textWidth)
{
   NEEDN(1);
   Program* p = P(c);
   const char* s = JS_ToCString(c, argv[0]);
   if (!s) return JS_EXCEPTION;
   float w = 0;
   if (p->font)
   {
      plutovg_canvas_set_font(p->cv, p->font, p->st.textSize);
      w = plutovg_canvas_text_extents(p->cv, s, -1, PLUTOVG_TEXT_ENCODING_UTF8, nullptr);
   }
   JS_FreeCString(c, s);
   return JS_NewFloat64(c, w);
}
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

int Interrupt(JSContext*, void* op)
{
   Program* p = (Program*)op;
   return std::chrono::steady_clock::now() > p->deadline ? 1 : 0;
}

void Reg(JSContext* c, JSValue g, const char* name, JSCFunction* f, int len)
{
   JS_SetPropertyStr(c, g, name, JS_NewCFunction(c, f, name, len));
}

const char* kPrelude = R"JS(
const PI = Math.PI, TAU = Math.PI * 2, HALF_PI = Math.PI / 2, CLOSE = 1;
const sin = Math.sin, cos = Math.cos, tan = Math.tan, atan2 = Math.atan2, atan = Math.atan,
      asin = Math.asin, acos = Math.acos, sqrt = Math.sqrt, abs = Math.abs, floor = Math.floor,
      ceil = Math.ceil, round = Math.round, pow = Math.pow, exp = Math.exp, log = Math.log,
      min = Math.min, max = Math.max, sign = Math.sign;
const console = { log() {}, warn() {}, error() {} };
const lerp = (a, b, t) => a + (b - a) * t;
const constrain = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const map = (v, a, b, c, d) => c + (d - c) * ((v - a) / (b - a));
const dist = (x1, y1, x2, y2) => Math.hypot(x2 - x1, y2 - y1);
const radians = (d) => d * Math.PI / 180, degrees = (r) => r * 180 / Math.PI;
)JS";

// Pulls "line:col" out of a quickjs stack ("    at <name> (sketch:LINE:COL)").
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

} // namespace

SketchEngine::SketchEngine() : mImpl(new Impl) {}
SketchEngine::~SketchEngine() { delete mImpl; }
bool SketchEngine::HasProgram() const { return mImpl->prog && mImpl->prog->ctx; }
const std::vector<SketchParamDecl>& SketchEngine::Params() const
{
   static const std::vector<SketchParamDecl> none;
   return mImpl->prog ? mImpl->prog->params : none;
}

void SketchEngine::SetFontFile(const std::string& path)
{
   if (path == mImpl->fontPath) return;
   mImpl->fontPath = path;
   if (mImpl->font) { plutovg_font_face_destroy(mImpl->font); mImpl->font = nullptr; }
   if (!path.empty()) mImpl->font = plutovg_font_face_load_from_file(path.c_str(), 0);
}

bool SketchEngine::Compile(const std::string& code, SketchError& err)
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
   Reg(c, g, "background", js_background, 1);
   Reg(c, g, "fill", js_fill, 1);
   Reg(c, g, "stroke", js_stroke, 1);
   Reg(c, g, "noFill", js_noFill, 0);
   Reg(c, g, "noStroke", js_noStroke, 0);
   Reg(c, g, "strokeWeight", js_strokeWeight, 1);
   Reg(c, g, "textSize", js_textSize, 1);
   Reg(c, g, "push", js_push, 0);
   Reg(c, g, "pop", js_pop, 0);
   Reg(c, g, "translate", js_translate, 2);
   Reg(c, g, "rotate", js_rotate, 1);
   Reg(c, g, "scale", js_scale, 2);
   Reg(c, g, "rect", js_rect, 4);
   Reg(c, g, "circle", js_circle, 3);
   Reg(c, g, "ellipse", js_ellipse, 4);
   Reg(c, g, "line", js_line, 4);
   Reg(c, g, "point", js_point, 2);
   Reg(c, g, "triangle", js_triangle, 6);
   Reg(c, g, "quad", js_quad, 8);
   Reg(c, g, "bezier", js_bezier, 8);
   Reg(c, g, "beginShape", js_beginShape, 0);
   Reg(c, g, "vertex", js_vertex, 2);
   Reg(c, g, "endShape", js_endShape, 1);
   Reg(c, g, "text", js_text, 3);
   Reg(c, g, "textWidth", js_textWidth, 1);
   Reg(c, g, "random", js_random, 2);
   Reg(c, g, "noise", js_noise, 3);
   Reg(c, g, "hsl", js_hsl, 3);
   Reg(c, g, "param", js_param, 4);
   JS_FreeValue(c, g);

   // Math.random must be reproducible too (S7): route it to the seeded stream.
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

bool SketchEngine::Run(const Frame& f, std::vector<uint8_t>& rgba, SketchError& err)
{
   err = SketchError{};
   Program* p = mImpl->prog;
   if (!p || !p->ctx) { err.message = "no program"; return false; }
   const int W = std::max(4, std::min(f.width, 8192)), H = std::max(4, std::min(f.height, 8192));
   JSContext* c = p->ctx;

   p->surf = plutovg_surface_create(W, H);
   if (!p->surf) { err.message = "out of memory"; return false; }
   p->cv = plutovg_canvas_create(p->surf);
   p->W = W; p->H = H;
   p->st = DrawState{};
   p->stack.clear();
   p->shape.clear();
   p->inShape = false;
   p->font = mImpl->font;
   p->rng.s = (f.seed * 2654435761u) ^ ((uint32_t)f.frame * 40503u + 1u);
   if (!p->rng.s) p->rng.s = 1;

   JSValue g = JS_GetGlobalObject(c);
   JS_SetPropertyStr(c, g, "width", JS_NewInt32(c, W));
   JS_SetPropertyStr(c, g, "height", JS_NewInt32(c, H));
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
   bool ok = !JS_IsException(r);
   if (!ok)
   {
      JSValue ex = JS_GetException(c);
      ExtractError(c, ex, err);
      JS_FreeValue(c, ex);
   }
   JS_FreeValue(c, r);

   if (ok)
   {
      // plutovg is premultiplied BGRA in memory; the texture wants straight RGBA.
      rgba.resize((size_t)W * H * 4);
      const uint8_t* src = plutovg_surface_get_data(p->surf);
      const int stride = plutovg_surface_get_stride(p->surf);
      for (int y = 0; y < H; ++y)
      {
         const uint8_t* s = src + (size_t)y * stride;
         uint8_t* d = rgba.data() + (size_t)y * W * 4;
         for (int x = 0; x < W; ++x, s += 4, d += 4)
         {
            const uint32_t a = s[3];
            if (a == 0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            d[0] = (uint8_t)std::min<uint32_t>(255, (s[2] * 255u + a / 2) / a);
            d[1] = (uint8_t)std::min<uint32_t>(255, (s[1] * 255u + a / 2) / a);
            d[2] = (uint8_t)std::min<uint32_t>(255, (s[0] * 255u + a / 2) / a);
            d[3] = (uint8_t)a;
         }
      }
   }
   plutovg_canvas_destroy(p->cv);
   plutovg_surface_destroy(p->surf);
   p->cv = nullptr; p->surf = nullptr; p->font = nullptr;
   return ok;
}

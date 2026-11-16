// Scene 2D, Block D: 3D cards in a comp camera (After Effects model), distort,
// layer fx, blend if and painting symmetry. Plan: internal/plans/2d-view §9.9.
#include "Scene2DNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "BlendModes.h"
#include "Geometry3DNodes.h"
#include "NodeViewport.h"
#include "SceneNodes.h"
#include "SharedViewportCamera.h"
#include "TileStore.h"

namespace
{
   constexpr float kPi = 3.14159265358979f;
   constexpr float kD2R = kPi / 180.0f;

   void Mul3(const float a[9], const float b[9], float out[9])
   {
      float r[9];
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++)
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
      std::copy(r, r + 9, out);
   }

   void RotX(float deg, float m[9])
   {
      const float c = std::cos(deg * kD2R), s = std::sin(deg * kD2R);
      const float r[9] = { 1, 0, 0, 0, c, -s, 0, s, c };
      std::copy(r, r + 9, m);
   }
   void RotY(float deg, float m[9])
   {
      const float c = std::cos(deg * kD2R), s = std::sin(deg * kD2R);
      const float r[9] = { c, 0, s, 0, 1, 0, -s, 0, c };
      std::copy(r, r + 9, m);
   }
   void RotZ(float deg, float m[9])
   {
      const float c = std::cos(deg * kD2R), s = std::sin(deg * kD2R);
      const float r[9] = { c, -s, 0, s, c, 0, 0, 0, 1 };
      std::copy(r, r + 9, m);
   }

   // The unit square (0,0) (1,0) (1,1) (0,1) onto a quad, Heckbert's closed form.
   void SquareToQuad(const float q[8], float h[9])
   {
      const float x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], x2 = q[4], y2 = q[5], x3 = q[6], y3 = q[7];
      const float sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3;
      float g = 0, k = 0;
      if (std::fabs(sx) > 1e-6f || std::fabs(sy) > 1e-6f)
      {
         const float dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
         const float den = dx1 * dy2 - dx2 * dy1;
         if (std::fabs(den) > 1e-9f)
         {
            g = (sx * dy2 - dx2 * sy) / den;
            k = (dx1 * sy - sx * dy1) / den;
         }
      }
      const float r[9] = { x1 - x0 + g * x1, x3 - x0 + k * x3, x0, y1 - y0 + g * y1, y3 - y0 + k * y3, y0, g, k, 1.0f };
      std::copy(r, r + 9, h);
   }

   bool Apply(const float h[9], float x, float y, float& ox, float& oy)
   {
      const float w = h[6] * x + h[7] * y + h[8];
      if (w <= 1e-6f)
         return false;
      ox = (h[0] * x + h[1] * y + h[2]) / w;
      oy = (h[3] * x + h[4] * y + h[5]) / w;
      return true;
   }

   bool Is3dKind(Scene2DNode::Kind k) { return k != Scene2DNode::Kind::Group && k != Scene2DNode::Kind::Fill && k != Scene2DNode::Kind::Adjust; }

   const char* kBlurFrag = R"(#version 150
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uDir;
uniform vec4 uSigma;
uniform int uTaps;
uniform float uStride;
uniform int uFirst;
void main() {
   vec4 acc = vec4(0.0), wsum = vec4(0.0);
   vec4 s2 = 2.0 * max(uSigma * uSigma, vec4(1e-4));
   for (int i = -64; i <= 64; i++) {
      if (abs(i) > uTaps) continue;
      float x = float(i) * uStride;
      vec4 w = exp(-x * x / s2);
      vec4 t = texture(uSrc, vUv + uDir * x);
      acc += (uFirst == 1 ? vec4(t.a) : t) * w;
      wsum += w;
   }
   fragColor = acc / max(wsum, vec4(1e-6));
}
)";

   const char* kFxFrag = R"(#version 150
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uL;
uniform sampler2D uB;
uniform sampler2D uB2;
uniform vec2 uDoc;
uniform int uShOn, uInOn, uGlOn, uStOn, uOvOn, uStPos, uIgOn, uBvOn, uGrOn, uSaOn, uPaOn, uPaKind, uBvMat;
uniform vec4 uShC, uInC, uGlC, uStC, uOvC, uIgC, uBvHi, uBvLo, uGrA, uGrB, uSaC, uPaA, uPaB;
uniform vec4 uShK, uGlK, uBvK, uSaK;   // contours: two inner points (x y x y) of a 0..1 curve
uniform vec2 uSaOff;
uniform float uPaScale, uPaAngle;
uniform vec2 uGrDir;
uniform vec3 uBvLight;
uniform float uIgSpread, uBvDepth;
uniform vec2 uShOff, uInOff;
uniform float uShSpread, uInSpread, uGlSpread, uStSize, uFill;
vec4 over(vec4 d, vec4 s) {
   float a = s.a + d.a * (1.0 - s.a);
   vec3 c = a > 1e-5 ? (s.rgb * s.a + d.rgb * d.a * (1.0 - s.a)) / a : vec3(0.0);
   return vec4(c, a);
}
float spreadA(float v, float sp) { return clamp(v / (1.0 - min(sp, 0.99)), 0.0, 1.0); }
float contour(float v, vec4 k) {
   if (v < k.x) return k.x > 0.0 ? v / k.x * k.y : k.y;
   if (v < k.z) return mix(k.y, k.w, (v - k.x) / max(k.z - k.x, 1e-4));
   return k.z < 1.0 ? mix(k.w, 1.0, (v - k.z) / (1.0 - k.z)) : k.w;
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float patternAt(vec2 uv) {
   vec2 p = uv * uDoc;
   float ca = cos(uPaAngle), sa = sin(uPaAngle);
   p = vec2(ca * p.x - sa * p.y, sa * p.x + ca * p.y) / max(uPaScale, 2.0);
   vec2 f = fract(p);
   if (uPaKind == 0) return mod(floor(p.x) + floor(p.y), 2.0);
   if (uPaKind == 1) return 1.0 - smoothstep(0.28, 0.34, length(f - 0.5));
   if (uPaKind == 2) return step(0.5, f.x);
   if (uPaKind == 3) return step(min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)), 0.06);
   return hash(floor(p));
}
// The relief layers' materials (Scene2DNode kReliefFrag): one lighting model in the whole view.
vec3 material(int m, vec3 base, vec3 n, vec3 L) {
   vec3 Hv = normalize(L + vec3(0.0, 0.0, 1.0));
   float diff = max(dot(n, L), 0.0), nh = max(dot(n, Hv), 0.0);
   if (m == 0) return base * (0.35 + 0.75 * diff) + vec3(0.22) * pow(nh, 18.0);
   if (m == 1) return base * (0.18 + 0.32 * diff) + mix(base, vec3(1.0), 0.35) * pow(nh, 42.0) * 1.3;
   if (m == 2) {
      vec3 r = reflect(vec3(0.0, 0.0, -1.0), n);
      float t = dot(normalize(r.xy + 1e-4), normalize(L.xy + 1e-4)) * length(r.xy);
      float sky = mix(0.06, 1.0, smoothstep(-0.25, 0.55, t)) - 0.35 * smoothstep(0.0, 0.08, abs(t - 0.2)) + 0.35;
      return mix(vec3(sky), base * sky, 0.25) + vec3(1.0) * pow(nh, 160.0);
   }
   float lit = clamp(0.25 + 0.85 * diff, 0.0, 1.0);
   vec2 cell = floor(gl_FragCoord.xy / 3.0);
   float dot1 = step(lit, hash(cell)) * (1.0 - smoothstep(0.3, 0.5, length(fract(gl_FragCoord.xy / 3.0) - 0.5)));
   return base * (1.0 - dot1);
}
void main() {
   vec4 L = texture(uL, vUv);
   vec4 res = vec4(0.0);
   if (uShOn == 1) res = over(res, vec4(uShC.rgb, contour(spreadA(texture(uB, vUv - uShOff).r, uShSpread), uShK) * uShC.a));
   if (uGlOn == 1) res = over(res, vec4(uGlC.rgb, contour(spreadA(texture(uB, vUv).g, uGlSpread), uGlK) * uGlC.a));
   res = over(res, vec4(L.rgb, L.a * uFill));
   if (uPaOn == 1) res = over(res, vec4(mix(uPaB.rgb, uPaA.rgb, patternAt(vUv)), L.a * uPaA.a));
   if (uGrOn == 1) {
      float t = clamp(dot(vUv - 0.5, uGrDir) + 0.5, 0.0, 1.0);
      res = over(res, vec4(mix(uGrA.rgb, uGrB.rgb, t), L.a * uGrA.a));
   }
   if (uOvOn == 1) res = over(res, vec4(uOvC.rgb, L.a * uOvC.a));
   if (uSaOn == 1) {
      float sa = abs(texture(uB2, vUv + uSaOff).b - texture(uB2, vUv - uSaOff).b);
      res = over(res, vec4(uSaC.rgb, L.a * contour(clamp(sa, 0.0, 1.0), uSaK) * uSaC.a));
   }
   if (uBvOn == 1) {
      vec2 px = 1.0 / uDoc;
      float gx = contour(texture(uB2, vUv + vec2(px.x, 0.0)).g, uBvK) - contour(texture(uB2, vUv - vec2(px.x, 0.0)).g, uBvK);
      float gy = contour(texture(uB2, vUv + vec2(0.0, px.y)).g, uBvK) - contour(texture(uB2, vUv - vec2(0.0, px.y)).g, uBvK);
      vec3 n = normalize(vec3(-gx * uBvDepth, -gy * uBvDepth, 1.0));
      if (uBvMat >= 0) {
         vec3 lit = clamp(material(uBvMat, res.rgb, n, normalize(uBvLight)), 0.0, 1.0);
         res = vec4(mix(res.rgb, lit, L.a * uBvHi.a), res.a);
      } else {
         float s = dot(n, uBvLight) - uBvLight.z;
         res = over(res, vec4(uBvHi.rgb, L.a * clamp(s * 3.0, 0.0, 1.0) * uBvHi.a));
         res = over(res, vec4(uBvLo.rgb, L.a * clamp(-s * 3.0, 0.0, 1.0) * uBvHi.a));
      }
   }
   if (uIgOn == 1) {
      float e = spreadA(texture(uB2, vUv).r, uIgSpread);
      res = over(res, vec4(uIgC.rgb, L.a * (1.0 - e) * uIgC.a));
   }
   if (uInOn == 1) {
      float lit = spreadA(texture(uB, vUv - uInOff).b, uInSpread);
      res = over(res, vec4(uInC.rgb, L.a * (1.0 - lit) * uInC.a));
   }
   if (uStOn == 1 && uStSize > 0.0) {
      float rOut = uStPos == 0 ? uStSize : uStPos == 2 ? uStSize * 0.5 : 0.0;
      float rIn = uStPos == 1 ? uStSize : uStPos == 2 ? uStSize * 0.5 : 0.0;
      float dil = L.a, ero = L.a;
      for (int k = 0; k < 24; k++) {
         float an = 6.2831853 * float(k) / 24.0;
         vec2 d = vec2(cos(an), sin(an)) / uDoc;
         for (int j = 1; j <= 6; j++) {
            float f = float(j) / 6.0;
            if (rOut > 0.0) dil = max(dil, texture(uL, vUv + d * rOut * f).a);
            if (rIn > 0.0) ero = min(ero, texture(uL, vUv + d * rIn * f).a);
         }
      }
      res = over(res, vec4(uStC.rgb, clamp(dil - ero, 0.0, 1.0) * uStC.a));
   }
   fragColor = res;
}
)";

   // Two fx results stacked: uTop over uBottom (straight alpha).
   const char* kFxCombineFrag = R"(#version 150
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTop;
uniform sampler2D uBottom;
void main() {
   vec4 s = texture(uTop, vUv), d = texture(uBottom, vUv);
   float a = s.a + d.a * (1.0 - s.a);
   vec3 c = a > 1e-5 ? (s.rgb * s.a + d.rgb * d.a * (1.0 - s.a)) / a : vec3(0.0);
   fragColor = vec4(c, a);
}
)";

   float ToWorking(float c, bool linear) { return linear ? (c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f)) : c; }
}

Scene2DNode::Fx Scene2DNode::FxDefault(int type)
{
   Fx f;
   switch (type)
   {
   case kFxShadow: break;
   case kFxInner: f.dist = 5.0f; f.size = 5.0f; break;
   case kFxGlow:
      f.c[0] = 1.0f; f.c[1] = 1.0f; f.c[2] = 0.75f; f.c[3] = 0.75f;
      f.dist = 0.0f; f.size = 15.0f;
      break;
   case kFxStroke:
      f.c[3] = 1.0f;
      f.dist = 0.0f; f.size = 3.0f;
      break;
   case kFxOverlay:
      f.c[0] = 1.0f; f.c[3] = 1.0f;
      f.dist = 0.0f; f.size = 0.0f;
      break;
   case kFxInnerGlow:
      f.c[0] = 1.0f; f.c[1] = 1.0f; f.c[2] = 0.75f; f.c[3] = 0.75f;
      f.dist = 0.0f; f.size = 15.0f;
      break;
   case kFxBevel:
      f.c[0] = 1.0f; f.c[1] = 1.0f; f.c[2] = 1.0f; f.c[3] = 0.75f;
      f.c2[0] = 0.0f; f.c2[1] = 0.0f; f.c2[2] = 0.0f;
      f.angle = 120.0f; f.dist = 100.0f; f.size = 5.0f;
      break;
   case kFxGradient:
      f.c[0] = 0.0f; f.c[1] = 0.0f; f.c[2] = 0.0f; f.c[3] = 1.0f;
      f.angle = 90.0f; f.dist = 0.0f; f.size = 0.0f;
      break;
   case kFxSatin:
      f.c[3] = 0.5f;
      f.angle = 19.0f; f.dist = 11.0f; f.size = 14.0f;
      break;
   case kFxPattern:
      f.c[3] = 1.0f;
      f.angle = 0.0f; f.dist = 0.0f; f.size = 20.0f;
      break;
   default: break;
   }
   return f;
}

bool Scene2DNode::FxSame(const Fx& a, const Fx& b)
{
   return a.on == b.on && std::equal(a.c, a.c + 4, b.c) && a.angle == b.angle && a.dist == b.dist && a.size == b.size &&
          a.spread == b.spread && a.pos == b.pos && std::equal(a.c2, a.c2 + 4, b.c2) && a.pattern == b.pattern &&
          a.material == b.material && std::equal(a.contour, a.contour + 4, b.contour);
}

const char* Scene2DNode::FxPatternName(int p)
{
   static const char* names[kFxPatternCount] = { "Checker", "Dots", "Lines", "Grid", "Noise" };
   return p >= 0 && p < kFxPatternCount ? names[p] : "";
}

const char* Scene2DNode::FxName(int type)
{
   static const char* names[kFxCount] = { "Drop Shadow", "Inner Shadow", "Outer Glow", "Stroke", "Colour Overlay", "Inner Glow", "Bevel",
                                           "Gradient Overlay", "Satin", "Pattern Overlay" };
   return type >= 0 && type < kFxCount ? names[type] : "";
}

// ---- camera and cards ----------------------------------------------------

Scene2DNode::CompCam Scene2DNode::Camera()
{
   CompCam c;
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   // After Effects' default: a 50 mm lens on the longer side of a 36 mm sensor, so Z = 0 is exactly 2D.
   float zoom = std::max(W, H) * 50.0f / 36.0f;
   float az = 0, el = 0, roll = 0, dist = zoom;
   float t[3] = { W * 0.5f, H * 0.5f, 0.0f };
   if (mDoc.camUid != 0 && ResolveUid)
      if (auto* cam = dynamic_cast<CameraNode*>(ResolveUid(mDoc.camUid)))
      {
         // The node's lens and orbit; a world unit is a third of the lens distance, so distance 3 keeps Z = 0 at size.
         zoom = H * 0.5f / std::tan(std::clamp(cam->fov, 1.0f, 170.0f) * 0.5f * kD2R);
         const float unit = zoom / 3.0f;
         dist = std::max(1.0f, cam->distance * unit);
         az = cam->azimuth;
         el = cam->elevation;
         roll = cam->roll;
         t[0] += cam->targetX * unit;
         t[1] -= cam->targetY * unit;
         t[2] += cam->targetZ * unit;
      }
   c.zoom = zoom;
   c.az = az;
   c.el = el;
   const float A = az * kD2R, E = el * kD2R;
   const float off[3] = { -std::cos(E) * std::sin(A), -std::sin(E), -std::cos(E) * std::cos(A) };
   for (int i = 0; i < 3; i++)
      c.pos[i] = t[i] + dist * off[i];
   const float f[3] = { -off[0], -off[1], -off[2] };
   float r[3] = { f[2], 0.0f, -f[0] };
   const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
   if (rl < 1e-5f)
   {
      r[0] = 1.0f;
      r[2] = 0.0f;
   }
   else
   {
      r[0] /= rl;
      r[2] /= rl;
   }
   float d[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
   const float cr = std::cos(roll * kD2R), sr = std::sin(roll * kD2R);
   for (int i = 0; i < 3; i++)
   {
      const float ri = r[i] * cr + d[i] * sr, di = d[i] * cr - r[i] * sr;
      c.rot[i] = ri;
      c.rot[3 + i] = di;
      c.rot[6 + i] = f[i];
   }
   return c;
}

// The card's homography before any distort; depth (camera z at the box centre) out.
static bool CardHomography(Scene2DNode& doc, const Scene2DNode::Layer& l, float bw, float bh, float h[9], float* depth)
{
   const Scene2DNode::CompCam cam = doc.Camera();
   float R[9], t[9];
   RotX(l.orient[0], R);
   RotY(l.orient[1], t);
   Mul3(R, t, R);
   RotZ(l.orient[2], t);
   Mul3(R, t, R);
   RotX(l.rx, t);
   Mul3(R, t, R);
   RotY(l.ry, t);
   Mul3(R, t, R);
   RotZ(l.angle, t);
   Mul3(R, t, R);
   const float kx = l.sx * (l.flipX ? -1.0f : 1.0f), ky = l.sy * (l.flipY ? -1.0f : 1.0f);
   const float eu[3] = { R[0] * kx, R[3] * kx, R[6] * kx };
   const float ev[3] = { R[1] * ky, R[4] * ky, R[7] * ky };
   const float a[3] = { kx * l.ax * bw, ky * l.ay * bh, l.az };
   float o[3];
   for (int i = 0; i < 3; i++)
      o[i] = (i == 0 ? l.x : i == 1 ? l.y : l.z) - (R[i * 3] * a[0] + R[i * 3 + 1] * a[1] + R[i * 3 + 2] * a[2]) - cam.pos[i];
   const float W2 = doc.Doc().w * 0.5f, H2 = doc.Doc().h * 0.5f;
   const float* cols[3] = { eu, ev, o };
   for (int j = 0; j < 3; j++)
   {
      const float* v = cols[j];
      const float cx = cam.rot[0] * v[0] + cam.rot[1] * v[1] + cam.rot[2] * v[2];
      const float cy = cam.rot[3] * v[0] + cam.rot[4] * v[1] + cam.rot[5] * v[2];
      const float cz = cam.rot[6] * v[0] + cam.rot[7] * v[1] + cam.rot[8] * v[2];
      h[j] = cam.zoom * cx + W2 * cz;
      h[3 + j] = cam.zoom * cy + H2 * cz;
      h[6 + j] = cz;
   }
   const float zc = h[6] * bw * 0.5f + h[7] * bh * 0.5f + h[8];
   if (depth != nullptr)
      *depth = zc;
   return zc > 1.0f;
}

bool Scene2DNode::LayerHomography(const Layer& l, float h[9])
{
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   bool ok = true;
   if (l.is3d && Is3dKind(l.kind))
      ok = CardHomography(*this, l, bw, bh, h, nullptr);
   else
   {
      float m[6];
      LayerMatrix(l, m);
      const float r[9] = { m[0], m[1], m[2], m[3], m[4], m[5], 0, 0, 1 };
      std::copy(r, r + 9, h);
   }
   if (l.warpOn && Is3dKind(l.kind) && ok)
   {
      const float box[8] = { 0, 0, bw, 0, bw, bh, 0, bh };
      float q[8];
      for (int i = 0; i < 4; i++)
      {
         if (!Apply(h, box[i * 2], box[i * 2 + 1], q[i * 2], q[i * 2 + 1]))
            return false;
         q[i * 2] += l.warp[i * 2];
         q[i * 2 + 1] += l.warp[i * 2 + 1];
      }
      SquareToQuad(q, h);
      for (int r = 0; r < 3; r++)
      {
         h[r * 3] /= bw;
         h[r * 3 + 1] /= bh;
      }
   }
   return ok;
}

float Scene2DNode::CardDepth(const Layer& l)
{
   float bw = 1, bh = 1, h[9], d = 0;
   LayerBox(l, bw, bh);
   CardHomography(*this, l, bw, bh, h, &d);
   return d;
}

bool Scene2DNode::ProjectPoint(const float p[3], float& x, float& y, float* depth)
{
   const CompCam cam = Camera();
   const float v[3] = { p[0] - cam.pos[0], p[1] - cam.pos[1], p[2] - cam.pos[2] };
   const float cx = cam.rot[0] * v[0] + cam.rot[1] * v[1] + cam.rot[2] * v[2];
   const float cy = cam.rot[3] * v[0] + cam.rot[4] * v[1] + cam.rot[5] * v[2];
   const float cz = cam.rot[6] * v[0] + cam.rot[7] * v[1] + cam.rot[8] * v[2];
   if (depth != nullptr)
      *depth = cz;
   if (cz <= 1.0f)
      return false;
   x = cam.zoom * cx / cz + mDoc.w * 0.5f;
   y = cam.zoom * cy / cz + mDoc.h * 0.5f;
   return true;
}

void Scene2DNode::CardAxes(const Layer& l, float rot[3][3], float card[2][3])
{
   // Same order as CardHomography: orient, then X, Y, Z. Each rotation turns about its axis as the ones before
   // it left it.
   float R[9], t[9];
   RotX(l.orient[0], R);
   RotY(l.orient[1], t);
   Mul3(R, t, R);
   RotZ(l.orient[2], t);
   Mul3(R, t, R);
   for (int i = 0; i < 3; i++)
      rot[0][i] = R[i * 3];
   RotX(l.rx, t);
   Mul3(R, t, R);
   for (int i = 0; i < 3; i++)
      rot[1][i] = R[i * 3 + 1];
   RotY(l.ry, t);
   Mul3(R, t, R);
   for (int i = 0; i < 3; i++)
      rot[2][i] = R[i * 3 + 2];
   RotZ(l.angle, t);
   Mul3(R, t, R);
   for (int i = 0; i < 3; i++)
   {
      card[0][i] = R[i * 3];
      card[1][i] = R[i * 3 + 1];
   }
}

std::vector<const Scene2DNode::Layer*> Scene2DNode::ListOf(int parent)
{
   std::vector<const Layer*> out;
   for (const Layer& l : mLayers)
   {
      if (l.parent != parent)
         continue;
      // A collapsed group adds nothing of its own, so its layers can join the 3D run outside it.
      const bool open = l.kind == Kind::Group && l.collapse && l.blend == kPassThrough && l.maskUid == 0 &&
                        !l.HasPixelMask() && !l.hasVMask && l.opacity >= 0.999f && l.fill >= 0.999f && !l.AnyFx() &&
                        !l.clip && !l.HasBlendIf();
      if (!open)
      {
         out.push_back(&l);
         continue;
      }
      if (!Shown(l))
         continue;
      for (const Layer* k : ListOf(l.id))
         out.push_back(k);
   }
   return out;
}

bool Scene2DNode::BeginDepth(int depth)
{
   Pair& p = PairAt(depth);
   DepthBufs& d = mDepth[depth];
   if (d.w != mDoc.w || d.h != mDoc.h || d.tex[0] == 0)
   {
      for (unsigned& t : d.tex)
      {
         if (t == 0)
            glGenTextures(1, &t);
         glBindTexture(GL_TEXTURE_2D, t);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mDoc.w, mDoc.h, 0, GL_RED, GL_FLOAT, nullptr);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      glBindTexture(GL_TEXTURE_2D, 0);
      d.w = mDoc.w;
      d.h = mDoc.h;
   }
   GLint prev = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
   for (int i = 0; i < 2; i++)
   {
      glBindFramebuffer(GL_FRAMEBUFFER, p.f[i].fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, d.tex[i], 0);
   }
   // Nothing drawn yet: everything is far.
   glBindFramebuffer(GL_FRAMEBUFFER, p.f[p.cur].fbo);
   const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
   glDrawBuffers(2, bufs);
   const float far[4] = { 1e30f, 0, 0, 0 };
   glClearBufferfv(GL_COLOR, 1, far);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glBindFramebuffer(GL_FRAMEBUFFER, prev);
   mDepthLevel = depth;
   return true;
}

void Scene2DNode::EndDepth(int depth)
{
   (void)depth;
   mDepthLevel = -1;
}

// ---- 3D object layers: real 3D in the comp camera ---------------------------------------------------------------

namespace
{
   // Row-major 4x4 helpers (the Mat4 type is column-major; To() converts).
   struct M4
   {
      float a[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
      float& at(int r, int c) { return a[r * 4 + c]; }
      float at(int r, int c) const { return a[r * 4 + c]; }
   };
   M4 Mul(const M4& x, const M4& y)
   {
      M4 r;
      for (int i = 0; i < 4; i++)
         for (int j = 0; j < 4; j++)
         {
            float s = 0;
            for (int k = 0; k < 4; k++)
               s += x.at(i, k) * y.at(k, j);
            r.at(i, j) = s;
         }
      return r;
   }
   Mat4 To(const M4& x)
   {
      Mat4 m;
      for (int r = 0; r < 4; r++)
         for (int c = 0; c < 4; c++)
            m.m[c * 4 + r] = x.at(r, c);
      return m;
   }
   // An affine (rotation x positive scale + translation) inverted.
   M4 InvAffine(const M4& x)
   {
      const float* m = x.a;
      const float A = m[0], B = m[1], C = m[2], D = m[4], E = m[5], F = m[6], G = m[8], H = m[9], I = m[10];
      const float det = A * (E * I - F * H) - B * (D * I - F * G) + C * (D * H - E * G);
      const float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
      M4 r;
      r.at(0, 0) = (E * I - F * H) * id; r.at(0, 1) = (C * H - B * I) * id; r.at(0, 2) = (B * F - C * E) * id;
      r.at(1, 0) = (F * G - D * I) * id; r.at(1, 1) = (A * I - C * G) * id; r.at(1, 2) = (C * D - A * F) * id;
      r.at(2, 0) = (D * H - E * G) * id; r.at(2, 1) = (B * G - A * H) * id; r.at(2, 2) = (A * E - B * D) * id;
      for (int i = 0; i < 3; i++)
         r.at(i, 3) = -(r.at(i, 0) * m[3] + r.at(i, 1) * m[7] + r.at(i, 2) * m[11]);
      return r;
   }

   // A painted skin over the object's albedo by UV: the same object, its colour and picture baked with the paint.
   struct SkinProxy : public IGeometrySource
   {
      IGeometrySource* base = nullptr;
      unsigned tex = 0;
      unsigned long long rev = 0;
      const Mesh& GetMesh() override { return base->GetMesh(); }
      unsigned long long MeshRevision() override { return base->MeshRevision(); }
      Mat4 GetModelMatrix() const override { return base->GetModelMatrix(); }
      Material GetMaterial() const override
      {
         Material m = base->GetMaterial();
         m.color[0] = m.color[1] = m.color[2] = 1.0f;   // baked into the picture
         return m;
      }
      unsigned long long MaterialRevision() const override { return base->MaterialRevision() * 31ull + rev; }
      unsigned int GetSurfaceTexture() override { return tex; }
      unsigned long long SurfaceTextureRevision() const override { return rev; }
      unsigned int GetMaterialTexture(int map) override { return map == 0 ? tex : base->GetMaterialTexture(map); }
      void ObjectColor(float c[3]) const override { base->ObjectColor(c); }
      bool CastsShadow() const override { return base->CastsShadow(); }
      unsigned int LightMask() const override { return base->LightMask(); }
   };

   const char* kSkinBakeFrag = R"(#version 150
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uBase;
uniform sampler2D uSkin;
uniform int uHasBase;
uniform vec3 uColor;
void main() {
   vec3 c = pow(max(uColor, vec3(0.0)), vec3(1.0 / 2.2));
   vec4 b = uHasBase == 1 ? texture(uBase, vUv) : vec4(1.0);
   vec4 s = texture(uSkin, vUv);
   fragColor = vec4(mix(c * b.rgb, s.rgb, s.a), 1.0);
}
)";
}

struct Scene2DGeo
{
   std::unique_ptr<Render3DNode> render;
   Render3DNode::Rig rig;
   std::vector<float> stamp;
   unsigned long long rigRev = 0;
   int frame = 0;
   // The object's frame for placing it: centre and radius, from its own mesh (so the node's own motion still shows).
   const void* boundsSrc = nullptr;
   unsigned long long boundsRev = ~0ull;
   bool boundsOk = false;
   float c[3] = { 0, 0, 0 };
   float r = 1.0f;
   float lo[3] = { -1, -1, -1 }, hi[3] = { 1, 1, 1 };
   // Framed once, like the node's own viewport: a mesh whose points move (a Transform node turning it) keeps its
   // size and place instead of being refitted to its new box every frame.
   bool framed = false;
   const void* framedSrc = nullptr;
   SkinProxy proxy;
   GLUtil::Fbo bake;
   unsigned bakeProgram = 0;
   unsigned long long bakeKey = 0;
};

struct Scene2DNode::GeoCam
{
   Mat4 view, proj, viewProj;
   float eye[3] = { 0, 0, 0 };   // in the object's world
   float nf[2] = { 1, 2 };
   float toObj[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };   // comp direction -> object world direction (rotation part)
   M4 fromComp;                                      // comp world point -> object world point
   M4 toComp;                                        // and back
   float lo[3] = { -1, -1, -1 }, hi[3] = { 1, 1, 1 }; // the object's box in its world
   float key[3] = { -0.45f, -0.7f, -0.55f };          // toward the key light, comp direction
};

bool Scene2DNode::GeoPlace(const Layer& l, IGeometrySource* geo, GeoCam& out)
{
   if (geo == nullptr)
      return false;
   std::shared_ptr<Scene2DGeo>& g = mGeo[l.id];
   if (!g)
      g = std::make_shared<Scene2DGeo>();
   // Bounds: the mesh's own (pre-model) box; an empty mesh (group, cloud) takes the render's once.
   const Mesh& mesh = geo->GetMesh();
   if (!mesh.vertices.empty())
   {
      const unsigned long long rev = geo->MeshRevision();
      if (g->boundsSrc != geo || g->boundsRev != rev || !g->boundsOk)
      {
         float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
         for (const Vertex& v : mesh.vertices)
         {
            const float p[3] = { v.px, v.py, v.pz };
            for (int k = 0; k < 3; k++)
            {
               lo[k] = std::min(lo[k], p[k]);
               hi[k] = std::max(hi[k], p[k]);
            }
         }
         float r2 = 0, c[3];
         for (int k = 0; k < 3; k++)
         {
            c[k] = 0.5f * (lo[k] + hi[k]);
            r2 += 0.25f * (hi[k] - lo[k]) * (hi[k] - lo[k]);
         }
         const float r = std::max(1e-4f, std::sqrt(r2));
         // Refit only for a new source or a mesh of another size altogether (a different primitive, a big resize).
         if (!g->framed || g->framedSrc != geo || r > g->r * 2.0f || r < g->r * 0.5f)
         {
            std::copy(c, c + 3, g->c);
            g->r = r;
            g->framed = true;
            g->framedSrc = geo;
         }
         std::copy(lo, lo + 3, g->lo);
         std::copy(hi, hi + 3, g->hi);
         g->boundsSrc = geo;
         g->boundsRev = rev;
         g->boundsOk = true;
      }
   }
   else if (g->boundsSrc != geo || !g->boundsOk)
   {
      float lo[3], hi[3];
      g->boundsSrc = geo;
      g->boundsOk = false;
      g->c[0] = g->c[1] = g->c[2] = 0.0f;
      g->r = 1.0f;
      if (g->render && g->render->SourceBounds(geo, lo, hi))
      {
         float r2 = 0;
         for (int k = 0; k < 3; k++)
         {
            g->c[k] = 0.5f * (lo[k] + hi[k]);
            r2 += 0.25f * (hi[k] - lo[k]) * (hi[k] - lo[k]);
         }
         g->r = std::max(1e-4f, std::sqrt(r2));
         std::copy(lo, lo + 3, g->lo);
         std::copy(hi, hi + 3, g->hi);
         g->boundsOk = true;
      }
      else
         for (int k = 0; k < 3; k++)
         {
            g->lo[k] = -1.0f;
            g->hi[k] = 1.0f;
         }
   }

   // Placement: the object's centre sits on the layer box's centre, its bounding sphere 0.8 of the box across, turned
   // and scaled by the layer like a card (same order as CardHomography), so the gizmo's axes are the object's.
   float bw = 1, bh = 1;
   LayerBox(l, bw, bh);
   const float S = std::max(1.0f, std::min(bw, bh));
   const float s = 0.4f * S / g->r;
   const bool d3 = l.is3d;
   float R[9], t[9];
   RotX(d3 ? l.orient[0] : 0.0f, R);
   RotY(d3 ? l.orient[1] : 0.0f, t);
   Mul3(R, t, R);
   RotZ(d3 ? l.orient[2] : 0.0f, t);
   Mul3(R, t, R);
   RotX(d3 ? l.rx : 0.0f, t);
   Mul3(R, t, R);
   RotY(d3 ? l.ry : 0.0f, t);
   Mul3(R, t, R);
   RotZ(l.angle, t);
   Mul3(R, t, R);
   const float kx = std::fabs(l.sx), ky = std::fabs(l.sy), kz = std::sqrt(kx * ky) * std::max(0.01f, std::fabs(l.sz));
   const float a[3] = { kx * (0.5f - l.ax) * bw, ky * (0.5f - l.ay) * bh, d3 ? -l.az : 0.0f };
   float C[3];
   for (int i = 0; i < 3; i++)
      C[i] = (i == 0 ? l.x : i == 1 ? l.y : (d3 ? l.z : 0.0f)) + R[i * 3] * a[0] + R[i * 3 + 1] * a[1] + R[i * 3 + 2] * a[2];
   // Object world (y up, z toward the viewer) -> comp world (y down, z away): flip y and z.
   // Objects are seen the way the 3D View first shows them: from its default camera height (22.9 degrees up), so a
   // turn reads as a turn. Straight on, a cube only shows its sides and a spin looks like it is growing and shrinking.
   // The view is the node's own viewport's (its orbit, default 34.4 degrees round, 22.9 up), so the page shows
   // the object the way its node card does.
   float viewAz = 34.3775f, viewEl = 22.9183f;
   if (NodeViewOf)
      NodeViewOf(l.uid, viewAz, viewEl);
   float Tl[9], Ty[9];
   RotX(viewEl, Tl);
   RotY(viewAz - 90.0f, Ty);
   Mul3(Tl, Ty, Tl);
   // The node viewport's light (fixed in its world, up and to the right) as that view shows it, then held still on
   // the page: an unturned layer is lit like the card, and turning the layer turns the object under the light.
   {
      const float d[3] = { 0.4f, 0.85f, 0.35f };
      for (int i = 0; i < 3; i++)
         out.key[i] = (i == 0 ? 1.0f : -1.0f) * (Tl[i * 3] * d[0] + Tl[i * 3 + 1] * d[1] + Tl[i * 3 + 2] * d[2]);
   }
   M4 M;
   const float ks[3] = { s * kx, -s * ky, -s * kz };
   for (int i = 0; i < 3; i++)
   {
      for (int j = 0; j < 3; j++)
         M.at(i, j) = R[i * 3] * ks[0] * Tl[j] + R[i * 3 + 1] * ks[1] * Tl[3 + j] + R[i * 3 + 2] * ks[2] * Tl[6 + j];
      M.at(i, 3) = C[i] - (M.at(i, 0) * g->c[0] + M.at(i, 1) * g->c[1] + M.at(i, 2) * g->c[2]);
   }
   // Comp world -> GL eye: the camera's rows, then y and z flipped (GL looks down -z with y up).
   const CompCam cam = Camera();
   M4 E;
   for (int i = 0; i < 3; i++)
   {
      const float sg = i == 0 ? 1.0f : -1.0f;
      for (int j = 0; j < 3; j++)
         E.at(i, j) = sg * cam.rot[i * 3 + j];
      E.at(i, 3) = -sg * (cam.rot[i * 3] * cam.pos[0] + cam.rot[i * 3 + 1] * cam.pos[1] + cam.rot[i * 3 + 2] * cam.pos[2]);
   }
   const M4 V = Mul(E, M);
   out.fromComp = InvAffine(M);
   out.toComp = M;
   std::copy(g->lo, g->lo + 3, out.lo);
   std::copy(g->hi, g->hi + 3, out.hi);
   for (int i = 0; i < 3; i++)
      out.eye[i] = out.fromComp.at(i, 0) * cam.pos[0] + out.fromComp.at(i, 1) * cam.pos[1] + out.fromComp.at(i, 2) * cam.pos[2] +
                   out.fromComp.at(i, 3);
   // Directions comp -> object: the rotation of M^-1 without its scale.
   for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
         out.toObj[i * 3 + j] = R[j * 3 + i] * (i == 0 ? 1.0f : -1.0f);
   {
      // The view tilt sits inside the object frame: comp -> object then takes its inverse (transpose) too.
      float t[9];
      for (int i = 0; i < 3; i++)
         for (int j = 0; j < 3; j++)
            t[i * 3 + j] = Tl[i] * out.toObj[j] + Tl[3 + i] * out.toObj[3 + j] + Tl[6 + i] * out.toObj[6 + j];
      std::copy(t, t + 9, out.toObj);
   }
   // Depth range around the object's sphere, in document px.
   const float dc = cam.rot[6] * (C[0] - cam.pos[0]) + cam.rot[7] * (C[1] - cam.pos[1]) + cam.rot[8] * (C[2] - cam.pos[2]);
   const float rp = 1.2f * s * g->r * std::max(kx, std::max(ky, kz));
   if (dc + rp <= 1.0f)
      return false;   // behind the camera
   const float N = std::max(1.0f, dc - rp), F = std::max(N + 1.0f, dc + rp);
   out.nf[0] = N;
   out.nf[1] = F;
   const float W = (float)mDoc.w, H = (float)mDoc.h;
   M4 P;
   P.at(0, 0) = 2.0f * cam.zoom / W;
   P.at(1, 1) = 2.0f * cam.zoom / H;
   P.at(2, 2) = -(F + N) / (F - N);
   P.at(2, 3) = -2.0f * F * N / (F - N);
   P.at(3, 2) = -1.0f;
   P.at(3, 3) = 0.0f;
   out.view = To(V);
   out.proj = To(P);
   out.viewProj = To(Mul(P, V));
   return true;
}

unsigned long long Scene2DNode::SkinRevision(const Layer& l) const
{
   unsigned long long r = mPaintEdits * 1000003ull + 7ull;
   if (mStroke.Active() && mPaintId == l.id)
      r += (unsigned long long)mStroke.DabCount() + 1ull;
   return r;
}

bool Scene2DNode::GeometryRender(const Layer& l, unsigned& color, unsigned& depthTex, float nf[2], float uv[4], int px[4])
{
   color = depthTex = 0;
   uv[0] = uv[1] = 0.0f;
   uv[2] = uv[3] = 1.0f;
   px[0] = px[1] = px[2] = px[3] = 0;
   auto* geo = dynamic_cast<IGeometrySource*>(Source(l));
   GeoCam gc;
   if (geo == nullptr || !GeoPlace(l, geo, gc))
      return false;
   Scene2DGeo& g = *mGeo[l.id];
   if (!g.render)
   {
      g.render = std::make_unique<Render3DNode>();
      g.render->samples = 2;
      g.render->RequestDepth();
   }
   Render3DNode& r = *g.render;
   // The painted skin rides over the albedo by UV, so it turns with the object.
   IGeometrySource* draw = geo;
   if (l.HasSkin())
   {
      auto it = mSurfaces.find(l.id);
      if (it != mSurfaces.end())
      {
         const unsigned skin = (mStroke.Active() && mPaintId == l.id && mPaintTarget == 0) ? mStroke.DisplayTexture(it->second.fbo) : it->second.fbo.tex;
         const unsigned base = geo->GetSurfaceTexture();
         const Material mat = geo->GetMaterial();
         unsigned long long key = SkinRevision(l) * 1315423911ull + skin * 2654435761ull + base * 97ull + geo->SurfaceTextureRevision();
         for (float c : mat.color)
         {
            unsigned bits = 0;
            std::memcpy(&bits, &c, sizeof bits);
            key = key * 1099511628211ull + bits;
         }
         if (g.bakeProgram == 0)
            g.bakeProgram = GLUtil::CompileProgram(kSkinBakeFrag);
         if (g.bakeProgram != 0 && GLUtil::EnsureFbo(g.bake, l.pw, l.ph, GL_RGBA8, "Scene2D skin bake"))
         {
            if (key != g.bakeKey)
            {
               g.bakeKey = key;
               const unsigned prog = g.bakeProgram;
               GLUtil::RunShaderPass(g.bake, prog, [&]()
               {
                  glActiveTexture(GL_TEXTURE1);
                  glBindTexture(GL_TEXTURE_2D, skin);
                  glUniform1i(glGetUniformLocation(prog, "uSkin"), 1);
                  glActiveTexture(GL_TEXTURE0);
                  glBindTexture(GL_TEXTURE_2D, base);
                  glUniform1i(glGetUniformLocation(prog, "uBase"), 0);
                  glUniform1i(glGetUniformLocation(prog, "uHasBase"), base != 0 ? 1 : 0);
                  glUniform3fv(glGetUniformLocation(prog, "uColor"), 1, mat.color);
               });
            }
            g.proxy.base = geo;
            g.proxy.tex = g.bake.tex;
            g.proxy.rev = key;
            draw = &g.proxy;
         }
      }
   }
   r.geometry[0] = draw;
   // Only the object's box on the page is drawn: the projection is cropped to it (an off-axis window of the same
   // camera), so a small object turning costs its own pixels, not the whole document's. The box grows in 64 px steps
   // so the render's buffers keep their size while the object moves.
   Mat4 proj = gc.proj;
   int bx0 = 0, by0 = 0, bw = mDoc.w, bh = mDoc.h;
   {
      float c8[8];
      if (GeometryCorners(l, c8, true))
      {
         const float x0 = std::max(0.0f, c8[0] - 2.0f), y0 = std::max(0.0f, c8[1] - 2.0f);
         const float x1 = std::min((float)mDoc.w, c8[4] + 2.0f), y1 = std::min((float)mDoc.h, c8[5] + 2.0f);
         if (x1 <= x0 || y1 <= y0)
            return false;   // wholly off the page
         auto snap = [](float lo, float hi, int full, int& o, int& n)
         {
            n = std::min(full, ((int)std::ceil(hi - lo) + 63) / 64 * 64);
            o = std::clamp((int)std::floor(0.5f * (lo + hi) - 0.5f * n), 0, full - n);
         };
         snap(x0, x1, mDoc.w, bx0, bw);
         // y: page rows run down, GL rows up.
         int gy = 0;
         snap((float)mDoc.h - y1, (float)mDoc.h - y0, mDoc.h, gy, bh);
         by0 = gy;
         if (bw < mDoc.w || bh < mDoc.h)
         {
            const float ux0 = (float)bx0 / mDoc.w, ux1 = (float)(bx0 + bw) / mDoc.w;
            const float vy0 = (float)by0 / mDoc.h, vy1 = (float)(by0 + bh) / mDoc.h;
            // The NDC box [a, b] maps to [-1, 1]: x' = k * (x - (a + b) / 2), in clip space with w; per row of the
            // column-major matrix (m[c * 4 + r]).
            const float kx = 1.0f / (ux1 - ux0), mx = ux0 + ux1 - 1.0f;   // (a + b) / 2 with a = 2 * ux0 - 1, b = 2 * ux1 - 1
            const float ky = 1.0f / (vy1 - vy0), my = vy0 + vy1 - 1.0f;
            for (int c = 0; c < 4; c++)
            {
               const float w = gc.proj.m[c * 4 + 3];
               proj.m[c * 4 + 0] = kx * (gc.proj.m[c * 4 + 0] - mx * w);
               proj.m[c * 4 + 1] = ky * (gc.proj.m[c * 4 + 1] - my * w);
            }
            uv[0] = ux0;
            uv[1] = vy0;
            uv[2] = ux1;
            uv[3] = vy1;
            px[0] = bx0;
            px[1] = by0;
            px[2] = bx0 + bw;
            px[3] = by0 + bh;
         }
      }
   }
   r.width = (float)bw;
   r.height = (float)bh;
   r.envAsBackground = false;
   r.bgOpacity = 0.0f;
   // No rim light on the page: it left dark blocks and stripes across flat faces near their edges (A/B against rim 0
   // and env 0; the environment's colour is kept, the key light and surround still model the form).
   r.rimIntensity = 0.0f;
   // One object on a page has nothing to bounce light off; the GI probes only rebuild once the scene holds still,
   // so a turning object was lit by probes from an older pose (bands and blocks across flat faces).
   r.probesOff = true;
   r.lightIntensity = 4.0f;   // the card's lit side reads near white
   r.tonemap = 0;             // the node viewport draws untonemapped; AgX greyed a white object
   // A neutral surround, as the node's viewport: a white object stays white instead of taking the sky's blue.
   for (int k = 0; k < 3; k++)
   {
      r.ambientColor[k] = 0.35f;
      r.envSky[k] = r.envHorizon[k] = 0.55f;
      r.envGround[k] = 0.3f;
   }
   // The key light stays put in the comp (upper left, in front) however the layer turns the object.
   {
      float toL[3] = { gc.key[0], gc.key[1], gc.key[2] };
      const float n = std::sqrt(toL[0] * toL[0] + toL[1] * toL[1] + toL[2] * toL[2]);
      float o[3];
      for (int i = 0; i < 3; i++)
         o[i] = (gc.toObj[i * 3] * toL[0] + gc.toObj[i * 3 + 1] * toL[1] + gc.toObj[i * 3 + 2] * toL[2]) / n;
      r.lightElevation = std::asin(std::clamp(o[1], -1.0f, 1.0f)) / kD2R;
      r.lightAzimuth = std::atan2(o[2], o[0]) / kD2R;
   }
   g.rig.hasView = true;
   g.rig.view = gc.view;
   std::copy(gc.eye, gc.eye + 3, g.rig.eye);
   g.rig.lens = nullptr;
   g.rig.hasProj = true;
   g.rig.proj = proj;
   std::vector<float> stamp(gc.view.m, gc.view.m + 16);
   stamp.insert(stamp.end(), proj.m, proj.m + 16);
   stamp.push_back(r.lightAzimuth);
   stamp.push_back(r.lightElevation);
   if (stamp != g.stamp)
   {
      g.stamp.swap(stamp);
      g.rigRev++;
   }
   g.rig.revision = g.rigRev;
   r.rig = &g.rig;
   r.CookIfNeeded(++g.frame);
   color = r.GetOutputTexture();
   depthTex = r.DepthValid() ? r.DepthTexture() : 0;
   nf[0] = gc.nf[0];
   nf[1] = gc.nf[1];
   return color != 0;
}

bool Scene2DNode::IsObjectLayer(const Layer& l)
{
   return l.kind == Kind::Node && dynamic_cast<IGeometrySource*>(Source(l)) != nullptr;
}

bool Scene2DNode::GeometryCorners(const Layer& l, float out[8], bool needAll)
{
   auto* geo = dynamic_cast<IGeometrySource*>(Source(l));
   GeoCam gc;
   if (geo == nullptr || !GeoPlace(l, geo, gc))
      return false;
   // The screen box of the object's eight box corners (the model's own matrix included).
   const Mat4 model = geo->GetModelMatrix();
   float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
   int seen = 0;
   for (int k = 0; k < 8; k++)
   {
      const float p[3] = { (k & 1) ? gc.hi[0] : gc.lo[0], (k & 2) ? gc.hi[1] : gc.lo[1], (k & 4) ? gc.hi[2] : gc.lo[2] };
      float w[3], q[3];
      for (int i = 0; i < 3; i++)
         w[i] = model.m[i] * p[0] + model.m[4 + i] * p[1] + model.m[8 + i] * p[2] + model.m[12 + i];
      for (int i = 0; i < 3; i++)
         q[i] = gc.toComp.at(i, 0) * w[0] + gc.toComp.at(i, 1) * w[1] + gc.toComp.at(i, 2) * w[2] + gc.toComp.at(i, 3);
      float x = 0, y = 0;
      if (!ProjectPoint(q, x, y))
         continue;
      seen++;
      x0 = std::min(x0, x);
      y0 = std::min(y0, y);
      x1 = std::max(x1, x);
      y1 = std::max(y1, y);
   }
   if (seen == 0 || (needAll && seen < 8))
      return false;
   const float r[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
   std::copy(r, r + 8, out);
   return true;
}

int Scene2DNode::GeometryRenderWidth(int id) const
{
   // The width of the texture the object was drawn into (what the comp samples).
   auto it = mGeo.find(id);
   const unsigned tex = it != mGeo.end() && it->second && it->second->render ? it->second->render->GetOutputTexture() : 0;
   if (tex == 0)
      return 0;
   GLint w = 0, prev = 0;
   glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
   glBindTexture(GL_TEXTURE_2D, tex);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
   glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
   return w;
}

// ---- 3D surface paint ----------------------------------------------------------

bool Scene2DNode::CanSurfacePaint(int id)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Node || (l->lock & (kLockPixels | kLockAll)) != 0)
      return false;
   auto* geo = dynamic_cast<IGeometrySource*>(Source(*l));
   return geo != nullptr && geo->GroupChildCount() == 0;
}

int Scene2DNode::SkinSide(int id)
{
   const Layer* l = FindConst(id);
   return l != nullptr && l->HasSkin() ? l->pw : 0;
}

bool Scene2DNode::SurfaceAt(int id, float x, float y, SurfacePoint& out)
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || l->kind != Kind::Node)
      return false;
   auto* geo = dynamic_cast<IGeometrySource*>(Source(*l));
   GeoCam gc;
   if (geo == nullptr || !GeoPlace(*l, geo, gc))
      return false;
   // The ray through the document pixel, from the comp camera, into the object's world: what the render drew there.
   const CompCam cam = Camera();
   const float dv[3] = { (x - mDoc.w * 0.5f) / cam.zoom, (y - mDoc.h * 0.5f) / cam.zoom, 1.0f };
   float dc[3];
   for (int i = 0; i < 3; i++)
      dc[i] = cam.rot[i] * dv[0] + cam.rot[3 + i] * dv[1] + cam.rot[6 + i] * dv[2];
   float d[3];
   for (int i = 0; i < 3; i++)
      d[i] = gc.fromComp.at(i, 0) * dc[0] + gc.fromComp.at(i, 1) * dc[1] + gc.fromComp.at(i, 2) * dc[2];
   const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
   if (dl < 1e-12f)
      return false;
   for (float& v : d)
      v /= dl;
   const int skin = l->HasSkin() ? l->pw : 1024;
   NodeViewport::SurfaceHit hit;
   if (!NodeViewport::RaySurface(geo, gc.eye, d, gc.viewProj, mDoc.w, mDoc.h, skin, hit))
      return false;
   std::copy(hit.uv, hit.uv + 2, out.uv);
   std::copy(hit.pos, hit.pos + 3, out.pos);
   out.facing = hit.facing;
   out.texelsPerPx = hit.texelsPerPx;   // the pick ran at document size
   return true;
}

bool Scene2DNode::BeginSurfacePaint(int id, const BrushEngine::Brush& b, float x, float y, float pressure, int side)
{
   if (!CanSurfacePaint(id))
      return false;
   if (mStroke.Active())
      EndPaint();
   Layer* l = FindLayer(id);
   if (l == nullptr)
      return false;
   if (!l->HasSkin())
   {
      side = std::clamp(side, 64, 4096);
      l->pw = l->ph = side;
      const int n = ((side + TileStore::kTile - 1) / TileStore::kTile) * ((side + TileStore::kTile - 1) / TileStore::kTile);
      l->tiles.assign(n, "-");
   }
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   SurfacePoint sp;
   if (!SurfaceAt(id, x, y, sp))
      return false;
   BrushEngine::Brush brush = b;
   brush.lockAlpha = brush.lockAlpha || (l->lock & kLockAlpha) != 0;
   brush.pressureSize = false;      // the size is a screen size; pressure and facing drive the opacity
   brush.pressureOpacity = true;
   brush.jitter = 0.0f;
   mSkinStroke = SkinStroke();
   mSkinStroke.screen = std::max(1.0f, b.size);
   mSkinStroke.spacing = std::max(0.02f, b.spacing);
   mSkinStroke.texels = std::max(0.05f, sp.texelsPerPx);
   brush.size = std::clamp(mSkinStroke.screen * mSkinStroke.texels, 1.0f, (float)l->pw * 0.5f);
   mPaintId = id;
   mPaintTarget = 0;
   mStroke.SetMirrors({});
   const float t0 = std::clamp((sp.facing - 0.08f) / 0.42f, 0.0f, 1.0f);
   mStroke.Begin(it->second.fbo, brush, sp.uv[0] * (float)l->pw, sp.uv[1] * (float)l->ph,
                 std::clamp(pressure < 0.0f ? 1.0f : pressure, 0.0f, 1.0f) * t0 * t0 * (3.0f - 2.0f * t0), 0);
   if (!mStroke.Active())
      return false;
   mSkinStroke.on = true;
   mSkinStroke.lx = x;
   mSkinStroke.ly = y;
   SkinDab(*l, x, y, pressure, true);
   return true;
}

void Scene2DNode::SkinDab(const Layer& l, float x, float y, float pressure, bool skipMain)
{
   SurfacePoint sp;
   if (!SurfaceAt(l.id, x, y, sp))
      return;
   // Strokes fade out toward grazing angles instead of smearing across the surface.
   const float t = std::clamp((sp.facing - 0.08f) / 0.42f, 0.0f, 1.0f);
   const float fall = t * t * (3.0f - 2.0f * t);
   if (fall <= 0.0f)
      return;
   const float P = std::clamp(pressure, 0.0f, 1.0f) * fall;
   const float side = (float)l.pw;
   if (!skipMain)
      mStroke.Dab(sp.uv[0] * side, sp.uv[1] * side, P, mSkinStroke.seed++);
   if (l.skinMirror == 0)
      return;
   auto* geo = dynamic_cast<IGeometrySource*>(Source(l));
   for (int m = 1; m < 8; m++)
   {
      if ((m & ~l.skinMirror) != 0)
         continue;
      float q[3] = { sp.pos[0] * ((m & 1) ? -1.0f : 1.0f), sp.pos[1] * ((m & 2) ? -1.0f : 1.0f), sp.pos[2] * ((m & 4) ? -1.0f : 1.0f) };
      NodeViewport::SurfaceHit h;
      if (NodeViewport::ClosestSurface(geo, q, h))
         mStroke.Dab(h.uv[0] * side, h.uv[1] * side, P, mSkinStroke.seed++);
   }
}

void Scene2DNode::SurfacePaintTo(float x, float y, float pressure)
{
   if (!mSkinStroke.on || !mStroke.Active())
      return;
   const Layer* l = FindConst(mPaintId);
   if (l == nullptr)
      return;
   const float dx = x - mSkinStroke.lx, dy = y - mSkinStroke.ly;
   const float d = std::sqrt(dx * dx + dy * dy);
   if (d < 1e-4f)
      return;
   const float step = std::max(0.5f, mSkinStroke.spacing * mSkinStroke.screen);
   float pos = 0.0f;
   while (mSkinStroke.carry + (d - pos) >= step)
   {
      pos += step - mSkinStroke.carry;
      mSkinStroke.carry = 0.0f;
      SkinDab(*l, mSkinStroke.lx + dx * pos / d, mSkinStroke.ly + dy * pos / d, pressure);
   }
   mSkinStroke.carry += d - pos;
   mSkinStroke.lx = x;
   mSkinStroke.ly = y;
}

bool Scene2DNode::SkinAt(int id, float u, float v, float rgba[4])
{
   Layers();
   const Layer* l = FindConst(id);
   if (l == nullptr || !l->HasSkin())
      return false;
   SyncSurfaces();
   auto it = mSurfaces.find(id);
   if (it == mSurfaces.end())
      return false;
   const int px = std::clamp((int)std::floor(u * l->pw), 0, l->pw - 1), py = std::clamp((int)std::floor(v * l->ph), 0, l->ph - 1);
   float c[4] = { 0, 0, 0, 0 };
   GLint prev = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, it->second.fbo.fbo);
   glReadPixels(px, py, 1, 1, GL_RGBA, GL_FLOAT, c);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, prev);
   std::copy(c, c + 4, rgba);
   return true;
}

bool Scene2DNode::SurfaceColor(int id, float x, float y, float rgba[4])
{
   SurfacePoint sp;
   if (!SurfaceAt(id, x, y, sp))
      return false;
   float c[4];
   if (!SkinAt(id, sp.uv[0], sp.uv[1], c) || c[3] <= 0.0f)
      return false;
   std::copy(c, c + 4, rgba);
   return true;
}

void Scene2DNode::ClearSkin(int id)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || !l->HasSkin())
      return;
   if (mStroke.Active())
      EndPaint();
   l->tiles.assign(l->tiles.size(), "-");
   mPaintEdits++;
   Commit();
}

// ---- layer fx ------------------------------------------------------------

bool Scene2DNode::EnsureFxShaders()
{
   if (!mFxTried)
   {
      mFxTried = true;
      mBlurProgram = GLUtil::CompileProgram(kBlurFrag);
      mFxProgram = GLUtil::CompileProgram(kFxFrag);
      mFxCombineProgram = GLUtil::CompileProgram(kFxCombineFrag);
   }
   return mBlurProgram != 0 && mFxProgram != 0;
}

void Scene2DNode::DrawFx(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode)
{
   Layer plain = l;
   for (Fx& f : plain.fx)
      f.on = false;
   plain.fxMore.clear();
   if (!EnsureFxShaders())
   {
      DrawLayer(plain, depth, frameId, opacityMul, clipMode);
      return;
   }
   // The layer alone, masks and fill-free, in document space.
   plain.blend = 0;
   plain.opacity = 1.0f;
   plain.fill = 1.0f;
   plain.clip = false;
   const Layer fresh;
   std::copy(fresh.bifT, fresh.bifT + 4, plain.bifT);
   std::copy(fresh.bifU, fresh.bifU + 4, plain.bifU);
   Pair& sub = PairAt(depth + 1);
   const float clear[4] = { 0, 0, 0, 0 };
   Clear(sub, clear);
   DrawLayer(plain, depth + 1, frameId, 1.0f, false);
   const unsigned L = PairAt(depth + 1).Tex();
   const int W = mDoc.w, H = mDoc.h;
   const unsigned fmt = GLUtil::ImageWorkingFormat();
   if (!GLUtil::EnsureFbo(mFxBlur[0], W, H, GL_RGBA16F, "Scene2DFx") || !GLUtil::EnsureFbo(mFxBlur[1], W, H, GL_RGBA16F, "Scene2DFx") ||
       !GLUtil::EnsureFbo(mFxOut, W, H, fmt, "Scene2DFx"))
      return;
   // One fx pass of `l`'s styles over its picture L (fill fillV) into outFbo.
   auto runFx = [&](const Layer& fl, float fillV, GLUtil::Fbo& outFbo) -> bool
   {
   const Fx& sh = fl.fx[kFxShadow];
   const Fx& in = fl.fx[kFxInner];
   const Fx& gl = fl.fx[kFxGlow];
   const Fx& st = fl.fx[kFxStroke];
   const Fx& ov = fl.fx[kFxOverlay];
   // Blurred alpha: r for the drop shadow, g for the glow, b for the inner shadow.
   const float sig[3] = { sh.on ? std::max(0.0f, sh.size) * 0.5f : 0.0f, gl.on ? std::max(0.0f, gl.size) * 0.5f : 0.0f,
                          in.on ? std::max(0.0f, in.size) * 0.5f : 0.0f };
   const float maxSig = std::max({ sig[0], sig[1], sig[2] });
   const float stride = std::max(1.0f, 3.0f * maxSig / 48.0f);
   const int taps = std::min(64, (int)std::ceil(3.0f * maxSig / stride));
   const Fx& ig = fl.fx[kFxInnerGlow];
   const Fx& bv = fl.fx[kFxBevel];
   const Fx& gr = fl.fx[kFxGradient];
   const Fx& sa = fl.fx[kFxSatin];
   const Fx& pa = fl.fx[kFxPattern];
   // Separable gaussian of the layer's alpha into `fb`, one sigma per channel.
   auto blur = [&](GLUtil::Fbo* fb, const float* s4)
   {
      const float mx = std::max({ s4[0], s4[1], s4[2], s4[3] });
      const float strideB = std::max(1.0f, 3.0f * mx / 48.0f);
      const int tapsB = std::min(64, (int)std::ceil(3.0f * mx / strideB));
      for (int pass = 0; pass < 2; pass++)
      {
         const unsigned src = pass == 0 ? L : fb[0].tex;
         GLUtil::RunShaderPass(fb[pass], mBlurProgram, [&]()
         {
            const unsigned prog = mBlurProgram;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src);
            glUniform1i(glGetUniformLocation(prog, "uSrc"), 0);
            glUniform2f(glGetUniformLocation(prog, "uDir"), pass == 0 ? 1.0f / W : 0.0f, pass == 0 ? 0.0f : 1.0f / H);
            glUniform4f(glGetUniformLocation(prog, "uSigma"), s4[0], s4[1], s4[2], s4[3]);
            glUniform1i(glGetUniformLocation(prog, "uTaps"), tapsB);
            glUniform1f(glGetUniformLocation(prog, "uStride"), strideB);
            glUniform1i(glGetUniformLocation(prog, "uFirst"), pass == 0 ? 1 : 0);
         });
      }
   };
   (void)stride; (void)taps;
   if (sh.on || gl.on || in.on)
   {
      const float s4[4] = { sig[0], sig[1], sig[2], 0.0f };
      blur(mFxBlur, s4);
   }
   // Second set: r for the inner glow, g for the bevel's height.
   const bool two = ig.on || bv.on || sa.on;
   if (two)
   {
      if (!GLUtil::EnsureFbo(mFxBlur2[0], W, H, GL_RGBA16F, "Scene2DFx") || !GLUtil::EnsureFbo(mFxBlur2[1], W, H, GL_RGBA16F, "Scene2DFx"))
         return false;
      const float s4[4] = { ig.on ? std::max(0.0f, ig.size) * 0.5f : 0.0f, bv.on ? std::max(0.5f, bv.size) * 0.5f : 0.0f,
                            sa.on ? std::max(0.5f, sa.size) * 0.5f : 0.0f, 0.0f };
      blur(mFxBlur2, s4);
   }
   const bool lin = mDoc.linear;
   auto col = [&](const Fx& f, const char* name)
   {
      glUniform4f(glGetUniformLocation(mFxProgram, name), ToWorking(f.c[0], lin), ToWorking(f.c[1], lin), ToWorking(f.c[2], lin),
                  std::clamp(f.c[3], 0.0f, 1.0f));
   };
   // Light from `angle` (90 = from the top) throws the shadow the other way; uv is y up.
   auto off = [&](const Fx& f, const char* name)
   {
      const float a = f.angle * kD2R;
      glUniform2f(glGetUniformLocation(mFxProgram, name), -std::cos(a) * f.dist / W, -std::sin(a) * f.dist / H);
   };
   GLUtil::RunShaderPass(outFbo, mFxProgram, [&]()
   {
      const unsigned prog = mFxProgram;
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, L);
      glUniform1i(glGetUniformLocation(prog, "uL"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mFxBlur[1].tex);
      glUniform1i(glGetUniformLocation(prog, "uB"), 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, two ? mFxBlur2[1].tex : mFxBlur[1].tex);
      glUniform1i(glGetUniformLocation(prog, "uB2"), 2);
      glActiveTexture(GL_TEXTURE0);
      glUniform1i(glGetUniformLocation(prog, "uIgOn"), ig.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uBvOn"), bv.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uGrOn"), gr.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uSaOn"), sa.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uPaOn"), pa.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uPaKind"), std::clamp(pa.pattern, 0, kFxPatternCount - 1));
      glUniform1i(glGetUniformLocation(prog, "uBvMat"), std::clamp(bv.material, -1, 3));
      glUniform1f(glGetUniformLocation(prog, "uPaScale"), std::max(2.0f, pa.size));
      glUniform1f(glGetUniformLocation(prog, "uPaAngle"), pa.angle * kD2R);
      col(sa, "uSaC");
      col(pa, "uPaA");
      off(sa, "uSaOff");
      auto contourU = [&](const Fx& f, const char* name)
      {
         glUniform4f(glGetUniformLocation(prog, name), f.contour[0], f.contour[1], f.contour[2], f.contour[3]);
      };
      contourU(sh, "uShK");
      contourU(gl, "uGlK");
      contourU(bv, "uBvK");
      contourU(sa, "uSaK");
      col(ig, "uIgC");
      col(bv, "uBvHi");
      col(gr, "uGrA");
      auto col2 = [&](const Fx& f, const char* name)
      {
         glUniform4f(glGetUniformLocation(mFxProgram, name), ToWorking(f.c2[0], lin), ToWorking(f.c2[1], lin), ToWorking(f.c2[2], lin),
                     std::clamp(f.c2[3], 0.0f, 1.0f));
      };
      col2(bv, "uBvLo");
      col2(gr, "uGrB");
      col2(pa, "uPaB");
      glUniform1f(glGetUniformLocation(prog, "uIgSpread"), std::clamp(ig.spread, 0.0f, 1.0f));
      // Bevel: height slope scaled by its size and depth; light from `angle`, 30 degrees up. uv is y up.
      glUniform1f(glGetUniformLocation(prog, "uBvDepth"), std::max(0.5f, bv.size) * std::clamp(bv.dist, 0.0f, 1000.0f) * 0.04f);
      {
         const float a = bv.angle * kD2R, el = 30.0f * kD2R;
         glUniform3f(glGetUniformLocation(prog, "uBvLight"), std::cos(a) * std::cos(el), std::sin(a) * std::cos(el), std::sin(el));
      }
      {
         // Gradient overlay: start colour where the angle points away from, across the document.
         const float a = gr.angle * kD2R;
         const float dx = std::cos(a), dy = std::sin(a);
         const float span = std::abs(dx) + std::abs(dy);
         glUniform2f(glGetUniformLocation(prog, "uGrDir"), dx / span * 2.0f * 0.5f, dy / span * 2.0f * 0.5f);
      }
      glUniform2f(glGetUniformLocation(prog, "uDoc"), (float)W, (float)H);
      glUniform1i(glGetUniformLocation(prog, "uShOn"), sh.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uInOn"), in.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uGlOn"), gl.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uStOn"), st.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uOvOn"), ov.on ? 1 : 0);
      glUniform1i(glGetUniformLocation(prog, "uStPos"), std::clamp(st.pos, 0, 2));
      col(sh, "uShC");
      col(in, "uInC");
      col(gl, "uGlC");
      col(st, "uStC");
      col(ov, "uOvC");
      off(sh, "uShOff");
      off(in, "uInOff");
      glUniform1f(glGetUniformLocation(prog, "uShSpread"), std::clamp(sh.spread, 0.0f, 1.0f));
      glUniform1f(glGetUniformLocation(prog, "uInSpread"), std::clamp(in.spread, 0.0f, 1.0f));
      glUniform1f(glGetUniformLocation(prog, "uGlSpread"), std::clamp(gl.spread, 0.0f, 1.0f));
      glUniform1f(glGetUniformLocation(prog, "uStSize"), std::clamp(st.size, 0.0f, 250.0f));
      glUniform1f(glGetUniformLocation(prog, "uFill"), std::clamp(fillV, 0.0f, 1.0f));
   });
   return true;
   };
   if (!runFx(l, l.fill, mFxOut))
      return;
   unsigned outTex = mFxOut.tex;
   // More of one kind: each drawn alone (no fill) and stacked, under the result for shadows and glows, over it otherwise.
   if (!l.fxMore.empty() && mFxCombineProgram != 0 && GLUtil::EnsureFbo(mFxExtra, W, H, fmt, "Scene2DFx") &&
       GLUtil::EnsureFbo(mFxMore[0], W, H, fmt, "Scene2DFx") && GLUtil::EnsureFbo(mFxMore[1], W, H, fmt, "Scene2DFx"))
   {
      int ping = 0;
      for (const auto& m : l.fxMore)
      {
         if (!m.second.on || m.first < 0 || m.first >= kFxCount)
            continue;
         Layer one = plain;
         one.fx[m.first] = m.second;
         if (!runFx(one, 0.0f, mFxExtra))
            continue;
         const bool under = m.first == kFxShadow || m.first == kFxGlow;
         const unsigned top = under ? outTex : mFxExtra.tex, bottom = under ? mFxExtra.tex : outTex;
         GLUtil::RunShaderPass(mFxMore[ping], mFxCombineProgram, [&]()
         {
            const unsigned prog = mFxCombineProgram;
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, bottom);
            glUniform1i(glGetUniformLocation(prog, "uBottom"), 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, top);
            glUniform1i(glGetUniformLocation(prog, "uTop"), 0);
         });
         outTex = mFxMore[ping].tex;
         ping ^= 1;
      }
   }
   PassArgs a;
   a.tex = outTex;
   a.texW = W;
   a.texH = H;
   a.box[0] = (float)W;
   a.box[1] = (float)H;
   a.srgb = false;
   a.blend = l.kind == Kind::Group && l.blend == kPassThrough ? 0 : std::clamp(l.blend, 0, BlendModes::kContrastInvertMode);
   a.opacity = l.opacity * opacityMul;
   a.clipMode = clipMode;
   a.bif = l.HasBlendIf() ? &l : nullptr;
   Pass(depth, a);
}

// ---- symmetry ------------------------------------------------------------

std::vector<std::array<float, 6>> Scene2DNode::DocMirrors() const
{
   std::vector<std::array<float, 6>> out;
   const float cx = mDoc.w * 0.5f, cy = mDoc.h * 0.5f;
   const std::array<float, 6> lr { -1, 0, 2 * cx, 0, 1, 0 }, tb { 1, 0, 0, 0, -1, 2 * cy }, both { -1, 0, 2 * cx, 0, -1, 2 * cy };
   switch (mDoc.symmetry)
   {
   case 1: out.push_back(lr); break;
   case 2: out.push_back(tb); break;
   case 3:
      out.push_back(lr);
      out.push_back(tb);
      out.push_back(both);
      break;
   case 4:
   {
      const int n = std::clamp(mDoc.symN, 2, 32);
      for (int k = 1; k < n; k++)
      {
         const float a = 2.0f * kPi * k / n, c = std::cos(a), s = std::sin(a);
         out.push_back({ c, -s, cx - c * cx + s * cy, s, c, cy - s * cx - c * cy });
      }
      break;
   }
   default: break;
   }
   return out;
}

std::vector<std::pair<float, float>> Scene2DNode::SymmetryPoints(float x, float y)
{
   std::vector<std::pair<float, float>> out { { x, y } };
   for (const auto& m : DocMirrors())
      out.push_back({ m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5] });
   return out;
}

// Each doc mirror carried onto the surface: three doc points and their mirrors, mapped
// through DocToSurface, fix the surface affine (exact for a flat layer).
std::vector<std::array<float, 6>> Scene2DNode::SurfaceMirrors(const Layer& l, int target)
{
   std::vector<std::array<float, 6>> out;
   const float cx = mDoc.w * 0.5f, cy = mDoc.h * 0.5f, r = 0.25f * std::max(1, std::min(mDoc.w, mDoc.h));
   const float d[3][2] = { { cx, cy }, { cx + r, cy }, { cx, cy + r } };
   for (const auto& m : DocMirrors())
   {
      float s[3][2], t[3][2], k = 1;
      bool ok = true;
      for (int i = 0; i < 3 && ok; i++)
      {
         const float mx = m[0] * d[i][0] + m[1] * d[i][1] + m[2], my = m[3] * d[i][0] + m[4] * d[i][1] + m[5];
         ok = DocToSurface(l, target, d[i][0], d[i][1], s[i][0], s[i][1], k) && DocToSurface(l, target, mx, my, t[i][0], t[i][1], k);
      }
      if (!ok)
         continue;
      // Solve A s_i = t_i: columns from the two edge vectors.
      const float ux = s[1][0] - s[0][0], uy = s[1][1] - s[0][1], vx = s[2][0] - s[0][0], vy = s[2][1] - s[0][1];
      const float det = ux * vy - vx * uy;
      if (std::fabs(det) < 1e-9f)
         continue;
      const float pux = t[1][0] - t[0][0], puy = t[1][1] - t[0][1], pvx = t[2][0] - t[0][0], pvy = t[2][1] - t[0][1];
      std::array<float, 6> a;
      a[0] = (pux * vy - pvx * uy) / det;
      a[1] = (pvx * ux - pux * vx) / det;
      a[3] = (puy * vy - pvy * uy) / det;
      a[4] = (pvy * ux - puy * vx) / det;
      a[2] = t[0][0] - (a[0] * s[0][0] + a[1] * s[0][1]);
      a[5] = t[0][1] - (a[3] * s[0][0] + a[4] * s[0][1]);
      out.push_back(a);
   }
   return out;
}

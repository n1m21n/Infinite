// Scene 2D, Block D: 3D cards in a comp camera (After Effects model), distort,
// layer fx, blend if and painting symmetry. Plan: internal/plans/2d-view §9.9.
#include "Scene2DNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

#include "BlendModes.h"
#include "Geometry3DNodes.h"
#include "NodeViewport.h"
#include "SceneNodes.h"
#include "SharedViewportCamera.h"

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
uniform vec3 uSigma;
uniform int uTaps;
uniform float uStride;
uniform int uFirst;
void main() {
   vec3 acc = vec3(0.0), wsum = vec3(0.0);
   vec3 s2 = 2.0 * max(uSigma * uSigma, vec3(1e-4));
   for (int i = -64; i <= 64; i++) {
      if (abs(i) > uTaps) continue;
      float x = float(i) * uStride;
      vec3 w = exp(-x * x / s2);
      vec4 t = texture(uSrc, vUv + uDir * x);
      acc += (uFirst == 1 ? vec3(t.a) : t.rgb) * w;
      wsum += w;
   }
   fragColor = vec4(acc / max(wsum, vec3(1e-6)), 1.0);
}
)";

   const char* kFxFrag = R"(#version 150
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uL;
uniform sampler2D uB;
uniform vec2 uDoc;
uniform int uShOn, uInOn, uGlOn, uStOn, uOvOn, uStPos;
uniform vec4 uShC, uInC, uGlC, uStC, uOvC;
uniform vec2 uShOff, uInOff;
uniform float uShSpread, uInSpread, uGlSpread, uStSize, uFill;
vec4 over(vec4 d, vec4 s) {
   float a = s.a + d.a * (1.0 - s.a);
   vec3 c = a > 1e-5 ? (s.rgb * s.a + d.rgb * d.a * (1.0 - s.a)) / a : vec3(0.0);
   return vec4(c, a);
}
float spreadA(float v, float sp) { return clamp(v / (1.0 - min(sp, 0.99)), 0.0, 1.0); }
void main() {
   vec4 L = texture(uL, vUv);
   vec4 res = vec4(0.0);
   if (uShOn == 1) res = over(res, vec4(uShC.rgb, spreadA(texture(uB, vUv - uShOff).r, uShSpread) * uShC.a));
   if (uGlOn == 1) res = over(res, vec4(uGlC.rgb, spreadA(texture(uB, vUv).g, uGlSpread) * uGlC.a));
   res = over(res, vec4(L.rgb, L.a * uFill));
   if (uOvOn == 1) res = over(res, vec4(uOvC.rgb, L.a * uOvC.a));
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
   default: break;
   }
   return f;
}

const char* Scene2DNode::FxName(int type)
{
   static const char* names[kFxCount] = { "Drop Shadow", "Inner Shadow", "Outer Glow", "Stroke", "Colour Overlay" };
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

unsigned Scene2DNode::GeometryTexture(const Layer& l, int& w, int& h)
{
   auto* geo = dynamic_cast<IGeometrySource*>(Source(l));
   if (geo == nullptr)
      return 0;
   std::shared_ptr<NodeViewport>& vp = mGeo[l.id];
   if (!vp)
      vp = std::make_shared<NodeViewport>();
   // The solo render's default view, turned with the comp camera.
   SharedViewportCamera cam;
   const CompCam c = Camera();
   cam.azimuth += c.az;
   cam.elevation = std::clamp(cam.elevation + c.el, -89.0f, 89.0f);
   const int side = std::clamp(std::min(mDoc.w, mDoc.h), 64, 2048);
   w = h = side;
   return vp->Render(geo, cam, side, side);
}

// ---- layer fx ------------------------------------------------------------

bool Scene2DNode::EnsureFxShaders()
{
   if (!mFxTried)
   {
      mFxTried = true;
      mBlurProgram = GLUtil::CompileProgram(kBlurFrag);
      mFxProgram = GLUtil::CompileProgram(kFxFrag);
   }
   return mBlurProgram != 0 && mFxProgram != 0;
}

void Scene2DNode::DrawFx(const Layer& l, int depth, int frameId, float opacityMul, bool clipMode)
{
   Layer plain = l;
   for (Fx& f : plain.fx)
      f.on = false;
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
   const Fx& sh = l.fx[kFxShadow];
   const Fx& in = l.fx[kFxInner];
   const Fx& gl = l.fx[kFxGlow];
   const Fx& st = l.fx[kFxStroke];
   const Fx& ov = l.fx[kFxOverlay];
   // Blurred alpha: r for the drop shadow, g for the glow, b for the inner shadow.
   const float sig[3] = { sh.on ? std::max(0.0f, sh.size) * 0.5f : 0.0f, gl.on ? std::max(0.0f, gl.size) * 0.5f : 0.0f,
                          in.on ? std::max(0.0f, in.size) * 0.5f : 0.0f };
   const float maxSig = std::max({ sig[0], sig[1], sig[2] });
   const float stride = std::max(1.0f, 3.0f * maxSig / 48.0f);
   const int taps = std::min(64, (int)std::ceil(3.0f * maxSig / stride));
   if (sh.on || gl.on || in.on)
      for (int pass = 0; pass < 2; pass++)
      {
         const unsigned src = pass == 0 ? L : mFxBlur[0].tex;
         GLUtil::RunShaderPass(mFxBlur[pass], mBlurProgram, [&]()
         {
            const unsigned prog = mBlurProgram;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src);
            glUniform1i(glGetUniformLocation(prog, "uSrc"), 0);
            glUniform2f(glGetUniformLocation(prog, "uDir"), pass == 0 ? 1.0f / W : 0.0f, pass == 0 ? 0.0f : 1.0f / H);
            glUniform3f(glGetUniformLocation(prog, "uSigma"), sig[0], sig[1], sig[2]);
            glUniform1i(glGetUniformLocation(prog, "uTaps"), taps);
            glUniform1f(glGetUniformLocation(prog, "uStride"), stride);
            glUniform1i(glGetUniformLocation(prog, "uFirst"), pass == 0 ? 1 : 0);
         });
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
   GLUtil::RunShaderPass(mFxOut, mFxProgram, [&]()
   {
      const unsigned prog = mFxProgram;
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, L);
      glUniform1i(glGetUniformLocation(prog, "uL"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mFxBlur[1].tex);
      glUniform1i(glGetUniformLocation(prog, "uB"), 1);
      glActiveTexture(GL_TEXTURE0);
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
      glUniform1f(glGetUniformLocation(prog, "uFill"), std::clamp(l.fill, 0.0f, 1.0f));
   });
   PassArgs a;
   a.tex = mFxOut.tex;
   a.texW = W;
   a.texH = H;
   a.box[0] = (float)W;
   a.box[1] = (float)H;
   a.srgb = false;
   a.blend = l.kind == Kind::Group && l.blend == kPassThrough ? 0 : std::clamp(l.blend, 0, (int)BlendModes::kAntiEraseMode);
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

#include "FluidNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
   const std::vector<std::string> kStirrerNames = { "Off", "Orbit", "Pulse", "Spin" };
   const std::vector<std::string> kViewNames = { "Dye", "Velocity", "Pressure", "Obstacles" };

   // Every pass reads cells with texelFetch and treats a neighbour that is off
   // the grid or inside an obstacle as a wall: no flow through it, no pressure
   // difference across it. One shared prelude keeps that rule identical in the
   // divergence, pressure and projection passes.
   const char* kPrelude =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n";

   // The velocity lives on a staggered grid: texel (i, j) holds u, the x velocity
   // on the left face of cell (i, j), in .x, and v, the y velocity on its bottom
   // face, in .y. With that layout the divergence (a difference of the four faces)
   // and the pressure gradient (a difference of two cells) pair up exactly, so a
   // converged pressure solve leaves no divergence behind. A face is a wall when
   // the cell on either side is solid or off the grid.
   const char* kWallRule =
      "bool solidAt(sampler2D obs, ivec2 q, ivec2 sz) {\n"
      "   if (q.x < 0 || q.y < 0 || q.x >= sz.x || q.y >= sz.y) return true;\n"
      "   return texelFetch(obs, q, 0).r > 0.5;\n"
      "}\n"
      "bool wallU(sampler2D obs, ivec2 p, ivec2 sz) { return solidAt(obs, p, sz) || solidAt(obs, p - ivec2(1, 0), sz); }\n"
      "bool wallV(sampler2D obs, ivec2 p, ivec2 sz) { return solidAt(obs, p, sz) || solidAt(obs, p - ivec2(0, 1), sz); }\n"
      "float faceU(sampler2D vt, sampler2D obs, ivec2 p, ivec2 sz) { return wallU(obs, p, sz) ? 0.0 : texelFetch(vt, p, 0).x; }\n"
      "float faceV(sampler2D vt, sampler2D obs, ivec2 p, ivec2 sz) { return wallV(obs, p, sz) ? 0.0 : texelFetch(vt, p, 0).y; }\n"
      "// Velocity at a point given in texels, interpolating each component on its own face grid.\n"
      "vec2 velAt(sampler2D vt, vec2 pos) {\n"
      "   vec2 sz = vec2(textureSize(vt, 0));\n"
      "   return vec2(texture(vt, vec2(pos.x + 0.5, pos.y) / sz).x, texture(vt, vec2(pos.x, pos.y + 0.5) / sz).y);\n"
      "}\n"
      "// The velocity at the middle of a cell, from its four faces.\n"
      "vec2 centreVel(sampler2D vt, sampler2D obs, ivec2 p, ivec2 sz) {\n"
      "   p = clamp(p, ivec2(0), sz - 1);\n"
      "   return 0.5 * vec2(faceU(vt, obs, p, sz) + faceU(vt, obs, p + ivec2(1, 0), sz),\n"
      "                     faceV(vt, obs, p, sz) + faceV(vt, obs, p + ivec2(0, 1), sz));\n"
      "}\n";

   // Forces: the force image (pixels per frame of its own image, converted to
   // simulation texels by uForceScale) plus the built-in stirrer.
   const char* kForceBody =
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uForce;\n"
      "uniform int uHasForce;\n"
      "uniform float uForceScale;\n"
      "uniform int uStirrer;\n"
      "uniform float uStrength;\n"
      "uniform float uSize;\n"
      "uniform float uTime;\n"
      "uniform float uAspect;\n"
      "vec2 stir(vec2 uv) {\n"
      "   float r2 = max(uSize * uSize, 1e-6);\n"
      "   if (uStirrer == 1) {\n"
      "      vec2 c = vec2(0.5) + 0.3 * vec2(cos(uTime * 0.9), sin(uTime * 0.9));\n"
      "      vec2 d = (uv - c) * vec2(uAspect, 1.0);\n"
      "      vec2 dir = vec2(-sin(uTime * 0.9), cos(uTime * 0.9));\n"
      "      return dir * (3.0 * uStrength) * exp(-dot(d, d) / r2);\n"
      "   } else if (uStirrer == 2) {\n"
      "      vec2 d = (uv - vec2(0.5)) * vec2(uAspect, 1.0);\n"
      "      float ph = fract(uTime / 2.0);\n"
      "      float ring = exp(-pow(length(d) - ph * 0.45, 2.0) / r2);\n"
      "      return normalize(d + vec2(1e-5)) * (3.0 * uStrength) * (1.0 - ph) * ring;\n"
      "   } else if (uStirrer == 3) {\n"
      "      vec2 d = (uv - vec2(0.5)) * vec2(uAspect, 1.0);\n"
      "      vec2 perp = vec2(-d.y, d.x) / (length(d) + 1e-3);\n"
      "      return perp * (2.0 * uStrength) * exp(-dot(d, d) / (r2 * 6.0));\n"
      "   }\n"
      "   return vec2(0.0);\n"
      "}\n"
      "void main() {\n"
      "   vec2 sz = vec2(textureSize(uVel, 0));\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   vec2 uvU = (vec2(p) + vec2(0.0, 0.5)) / sz;\n"
      "   vec2 uvV = (vec2(p) + vec2(0.5, 0.0)) / sz;\n"
      "   float fu = stir(uvU).x;\n"
      "   float fv = stir(uvV).y;\n"
      "   if (uHasForce != 0) {\n"
      "      fu += texture(uForce, uvU).x * uForceScale;\n"
      "      fv += texture(uForce, uvV).y * uForceScale;\n"
      "   }\n"
      "   fragColor = vec4(texelFetch(uVel, p, 0).xy + vec2(fu, fv), 0.0, 1.0);\n"
      "}\n";

   // Semi-Lagrangian move of the velocity along itself, plus viscosity (a
   // Laplacian term) and damping. Solid cells hold no velocity.
   // Semi-Lagrangian move of every face's velocity along the flow, plus viscosity (a
   // Laplacian term) and damping. Wall faces hold no velocity.
   const char* kAdvectVelBody =
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uObs;\n"
      "uniform float uVisc;\n"
      "uniform float uDamp;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uVel, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   vec2 pu = vec2(p) + vec2(0.0, 0.5);\n"
      "   vec2 pv = vec2(p) + vec2(0.5, 0.0);\n"
      "   float u = velAt(uVel, pu - velAt(uVel, pu)).x;\n"
      "   float v = velAt(uVel, pv - velAt(uVel, pv)).y;\n"
      "   ivec2 mx = sz - 1;\n"
      "   vec2 c0 = texelFetch(uVel, p, 0).xy;\n"
      "   vec2 lap = texelFetch(uVel, clamp(p + ivec2(-1, 0), ivec2(0), mx), 0).xy + texelFetch(uVel, clamp(p + ivec2(1, 0), ivec2(0), mx), 0).xy\n"
      "            + texelFetch(uVel, clamp(p + ivec2(0, -1), ivec2(0), mx), 0).xy + texelFetch(uVel, clamp(p + ivec2(0, 1), ivec2(0), mx), 0).xy\n"
      "            - 4.0 * c0;\n"
      "   vec2 a = (vec2(u, v) + uVisc * lap) * (1.0 - uDamp);\n"
      "   float len = length(a);\n"
      "   if (len > 24.0) a *= 24.0 / len;\n"
      "   if (wallU(uObs, p, sz)) a.x = 0.0;\n"
      "   if (wallV(uObs, p, sz)) a.y = 0.0;\n"
      "   fragColor = vec4(a, 0.0, 1.0);\n"
      "}\n";

   const char* kCurlBody =
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uObs;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uVel, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   float l = centreVel(uVel, uObs, p + ivec2(-1, 0), sz).y;\n"
      "   float r = centreVel(uVel, uObs, p + ivec2(1, 0), sz).y;\n"
      "   float b = centreVel(uVel, uObs, p + ivec2(0, -1), sz).x;\n"
      "   float t = centreVel(uVel, uObs, p + ivec2(0, 1), sz).x;\n"
      "   fragColor = vec4(0.5 * ((r - l) - (t - b)), 0.0, 0.0, 1.0);\n"
      "}\n";

   // Vorticity confinement (Fedkiw 2001): pushes the flow around the centres of
   // its own swirls, putting back what advection smooths away.
   // Vorticity confinement (Fedkiw 2001): a force around the centres of the flow's own
   // swirls, putting back what advection smooths away. Written per cell; the next pass
   // hands it to the faces.
   const char* kVortBody =
      "uniform sampler2D uCurl;\n"
      "uniform float uEps;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uCurl, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   float l = texelFetch(uCurl, clamp(p + ivec2(-1, 0), ivec2(0), sz - 1), 0).x;\n"
      "   float r = texelFetch(uCurl, clamp(p + ivec2(1, 0), ivec2(0), sz - 1), 0).x;\n"
      "   float b = texelFetch(uCurl, clamp(p + ivec2(0, -1), ivec2(0), sz - 1), 0).x;\n"
      "   float t = texelFetch(uCurl, clamp(p + ivec2(0, 1), ivec2(0), sz - 1), 0).x;\n"
      "   float w = texelFetch(uCurl, p, 0).x;\n"
      "   vec2 g = 0.5 * vec2(abs(r) - abs(l), abs(t) - abs(b));\n"
      "   vec2 n = g / (length(g) + 1e-5);\n"
      "   fragColor = vec4(uEps * vec2(n.y, -n.x) * w, 0.0, 1.0);\n"
      "}\n";

   const char* kVortApplyBody =
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uForce;\n"
      "uniform sampler2D uObs;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uVel, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   vec2 f0 = texelFetch(uForce, p, 0).xy;\n"
      "   vec2 fl = texelFetch(uForce, max(p - ivec2(1, 0), ivec2(0)), 0).xy;\n"
      "   vec2 fb = texelFetch(uForce, max(p - ivec2(0, 1), ivec2(0)), 0).xy;\n"
      "   vec2 a = texelFetch(uVel, p, 0).xy + vec2(0.5 * (f0.x + fl.x), 0.5 * (f0.y + fb.y));\n"
      "   if (wallU(uObs, p, sz)) a.x = 0.0;\n"
      "   if (wallV(uObs, p, sz)) a.y = 0.0;\n"
      "   fragColor = vec4(a, 0.0, 1.0);\n"
      "}\n";

   const char* kDivBody =
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uObs;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uVel, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   if (solidAt(uObs, p, sz)) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
      "   float d = (faceU(uVel, uObs, p + ivec2(1, 0), sz) - faceU(uVel, uObs, p, sz))\n"
      "           + (faceV(uVel, uObs, p + ivec2(0, 1), sz) - faceV(uVel, uObs, p, sz));\n"
      "   fragColor = vec4(d, 0.0, 0.0, 1.0);\n"
      "}\n";

   // Damped Jacobi (0.8): plain Jacobi leaves the checkerboard mode untouched, which
   // a multigrid smoother must not.
   const char* kJacobiBody =
      "uniform sampler2D uPress;\n"
      "uniform sampler2D uDiv;\n"
      "uniform sampler2D uObs;\n"
      "float pr(ivec2 q, ivec2 sz, float c) { return solidAt(uObs, q, sz) ? c : texelFetch(uPress, q, 0).x; }\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uPress, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   float c = texelFetch(uPress, p, 0).x;\n"
      "   if (solidAt(uObs, p, sz)) { fragColor = vec4(c, 0.0, 0.0, 1.0); return; }\n"
      "   float s = pr(p + ivec2(-1, 0), sz, c) + pr(p + ivec2(1, 0), sz, c)\n"
      "           + pr(p + ivec2(0, -1), sz, c) + pr(p + ivec2(0, 1), sz, c);\n"
      "   float j = (s - texelFetch(uDiv, p, 0).x) * 0.25;\n"
      "   fragColor = vec4(mix(c, j, 0.8), 0.0, 0.0, 1.0);\n"
      "}\n";

   // Residual of the pressure equation: rhs - laplacian(p).
   const char* kResidBody =
      "uniform sampler2D uPress;\n"
      "uniform sampler2D uDiv;\n"
      "uniform sampler2D uObs;\n"
      "float pr(ivec2 q, ivec2 sz, float c) { return solidAt(uObs, q, sz) ? c : texelFetch(uPress, q, 0).x; }\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uPress, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   if (solidAt(uObs, p, sz)) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
      "   float c = texelFetch(uPress, p, 0).x;\n"
      "   float s = pr(p + ivec2(-1, 0), sz, c) + pr(p + ivec2(1, 0), sz, c)\n"
      "           + pr(p + ivec2(0, -1), sz, c) + pr(p + ivec2(0, 1), sz, c);\n"
      "   fragColor = vec4(texelFetch(uDiv, p, 0).x - (s - 4.0 * c), 0.0, 0.0, 1.0);\n"
      "}\n";

   // A coarse cell covers four fine ones; on the half-size grid the equation
   // (laplacian with unit spacing) has a right-hand side four times as large,
   // which is the sum of the four fine values.
   const char* kRestrictBody =
      "uniform sampler2D uFine;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uFine, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy) * 2;\n"
      "   float s = 0.0;\n"
      "   for (int j = 0; j < 2; j++)\n"
      "      for (int i = 0; i < 2; i++)\n"
      "         s += texelFetch(uFine, min(p + ivec2(i, j), sz - 1), 0).x;\n"
      "   fragColor = vec4(s, 0.0, 0.0, 1.0);\n"
      "}\n";

   const char* kObsDownBody =
      "uniform sampler2D uFine;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uFine, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy) * 2;\n"
      "   float s = 0.0;\n"
      "   for (int j = 0; j < 2; j++)\n"
      "      for (int i = 0; i < 2; i++)\n"
      "         s = max(s, texelFetch(uFine, min(p + ivec2(i, j), sz - 1), 0).x);\n"
      "   fragColor = vec4(s, 0.0, 0.0, 1.0);\n"
      "}\n";

   // fine pressure + the coarse correction, bilinear.
   const char* kProlongBody =
      "uniform sampler2D uFine;\n"
      "uniform sampler2D uCoarse;\n"
      "void main() {\n"
      "   float f = texelFetch(uFine, ivec2(gl_FragCoord.xy), 0).x;\n"
      "   fragColor = vec4(f + texture(uCoarse, vUv).x, 0.0, 0.0, 1.0);\n"
      "}\n";

   const char* kGradBody =
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uPress;\n"
      "uniform sampler2D uObs;\n"
      "void main() {\n"
      "   ivec2 sz = textureSize(uVel, 0);\n"
      "   ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "   vec2 a = texelFetch(uVel, p, 0).xy;\n"
      "   float c = texelFetch(uPress, p, 0).x;\n"
      "   if (wallU(uObs, p, sz)) a.x = 0.0;\n"
      "   else a.x -= c - texelFetch(uPress, p - ivec2(1, 0), 0).x;\n"
      "   if (wallV(uObs, p, sz)) a.y = 0.0;\n"
      "   else a.y -= c - texelFetch(uPress, p - ivec2(0, 1), 0).x;\n"
      "   fragColor = vec4(a, 0.0, 1.0);\n"
      "}\n";

   // Bright parts of the obstacle image are solid.
   const char* kObsBody =
      "uniform sampler2D uSrc;\n"
      "void main() {\n"
      "   vec3 c = texture(uSrc, vUv).rgb;\n"
      "   fragColor = vec4(dot(c, vec3(0.299, 0.587, 0.114)) > 0.5 ? 1.0 : 0.0, 0.0, 0.0, 1.0);\n"
      "}\n";

   const char* kAdvectDyeBody =
      "uniform sampler2D uDye;\n"
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uSrc;\n"
      "uniform sampler2D uObs;\n"
      "uniform int uHasSrc;\n"
      "uniform float uRefresh;\n"
      "uniform float uFade;\n"
      "vec3 hsv(float h, float s, float v) {\n"
      "   vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
      "   return v * mix(vec3(1.0), k, s);\n"
      "}\n"
      "void main() {\n"
      "   vec2 ssz = vec2(textureSize(uVel, 0));\n"
      "   vec2 v = velAt(uVel, vUv * ssz);\n"
      "   vec4 d = texture(uDye, vUv - v / ssz);\n"
      "   vec3 s = uHasSrc != 0 ? texture(uSrc, vUv).rgb\n"
      "      : hsv(fract(vUv.x + 0.5 * vUv.y), 0.8, 0.45 + 0.5 * step(0.5, fract(vUv.y * 8.0)));\n"
      "   vec3 c = mix(d.rgb, s, uRefresh) * (1.0 - uFade);\n"
      "   if (texture(uObs, vUv).r > 0.5) c = s;\n"
      "   fragColor = vec4(c, 1.0);\n"
      "}\n";

   const char* kOutBody =
      "uniform sampler2D uDye;\n"
      "uniform sampler2D uVel;\n"
      "uniform sampler2D uPress;\n"
      "uniform sampler2D uObs;\n"
      "uniform int uView;\n"
      "vec3 hsv(float h, float s, float v) {\n"
      "   vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
      "   return v * mix(vec3(1.0), k, s);\n"
      "}\n"
      "void main() {\n"
      "   if (uView == 1) {\n"
      "      vec2 v = velAt(uVel, vUv * vec2(textureSize(uVel, 0)));\n"
      "      fragColor = vec4(hsv(atan(v.y, v.x) / 6.2831853, 1.0, 1.0 - exp(-length(v) * 0.5)), 1.0);\n"
      "   } else if (uView == 2) {\n"
      "      float p = texture(uPress, vUv).x;\n"
      "      fragColor = vec4(clamp(vec3(0.5 + p * 0.1, 0.5 - abs(p) * 0.05, 0.5 - p * 0.1), 0.0, 1.0), 1.0);\n"
      "   } else if (uView == 3) {\n"
      "      float o = texture(uObs, vUv).r;\n"
      "      fragColor = vec4(vec3(o), 1.0);\n"
      "   } else {\n"
      "      fragColor = vec4(texture(uDye, vUv).rgb, 1.0);\n"
      "   }\n"
      "}\n";

   const char* kZeroFrag =
      "#version 150\n"
      "out vec4 fragColor;\n"
      "void main() { fragColor = vec4(0.0, 0.0, 0.0, 1.0); }\n";

   std::string Make(const char* body, bool wallRule = false)
   {
      (void)wallRule;
      return std::string(kPrelude) + kWallRule + body;
   }

   void Bind(unsigned int prog, int unit, unsigned int tex, const char* name)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, tex);
      glUniform1i(glGetUniformLocation(prog, name), unit);
   }
   void F1(unsigned int prog, const char* name, float v) { glUniform1f(glGetUniformLocation(prog, name), v); }
   void I1(unsigned int prog, const char* name, int v) { glUniform1i(glGetUniformLocation(prog, name), v); }
}

const std::vector<std::string>& FluidNode::StirrerNames() { return kStirrerNames; }
const std::vector<std::string>& FluidNode::ViewNames() { return kViewNames; }

FluidNode::~FluidNode()
{
   DestroyTargets();
   for (unsigned int p : { mProgResid, mProgRestrict, mProgProlong, mProgObsDown, mProgForce, mProgAdvectVel, mProgCurl, mProgVort, mProgVortApply, mProgDiv, mProgJacobi, mProgGrad,
                           mProgObs, mProgAdvectDye, mProgOut, mProgZero })
      if (p != 0)
         glDeleteProgram(p);
}

void FluidNode::DestroyTargets()
{
   for (int i = 0; i < 2; i++)
   {
      GLUtil::DestroyFbo(mVel[i]);
      GLUtil::DestroyFbo(mPressure[i]);
      GLUtil::DestroyFbo(mDye[i]);
   }
   for (int l = 0; l < kMaxLevels; l++)
   {
      GLUtil::DestroyFbo(mLvP[l][0]);
      GLUtil::DestroyFbo(mLvP[l][1]);
      GLUtil::DestroyFbo(mLvRhs[l]);
      GLUtil::DestroyFbo(mLvRes[l]);
      GLUtil::DestroyFbo(mLvObs[l]);
   }
   GLUtil::DestroyFbo(mDiv);
   GLUtil::DestroyFbo(mCurl);
   GLUtil::DestroyFbo(mObs);
   GLUtil::DestroyFbo(mOut);
}

bool FluidNode::EnsureShaders()
{
   if (mShadersTried)
      return mProgOut != 0;
   mShadersTried = true;
   const std::string force = Make(kForceBody), adv = Make(kAdvectVelBody), curl = Make(kCurlBody),
                     vort = Make(kVortBody), div = Make(kDivBody, true), jac = Make(kJacobiBody, true),
                     grad = Make(kGradBody, true), obs = Make(kObsBody), dye = Make(kAdvectDyeBody),
                     out = Make(kOutBody);
   mProgForce = GLUtil::CompileProgram(force.c_str());
   mProgAdvectVel = GLUtil::CompileProgram(adv.c_str());
   mProgCurl = GLUtil::CompileProgram(curl.c_str());
   mProgVort = GLUtil::CompileProgram(vort.c_str());
   const std::string vortApply = Make(kVortApplyBody, true);
   mProgVortApply = GLUtil::CompileProgram(vortApply.c_str());
   mProgDiv = GLUtil::CompileProgram(div.c_str());
   mProgJacobi = GLUtil::CompileProgram(jac.c_str());
   mProgGrad = GLUtil::CompileProgram(grad.c_str());
   mProgObs = GLUtil::CompileProgram(obs.c_str());
   mProgAdvectDye = GLUtil::CompileProgram(dye.c_str());
   mProgOut = GLUtil::CompileProgram(out.c_str());
   const std::string resid = Make(kResidBody, true), restrictSrc = Make(kRestrictBody),
                     prolong = Make(kProlongBody), obsDown = Make(kObsDownBody);
   mProgResid = GLUtil::CompileProgram(resid.c_str());
   mProgRestrict = GLUtil::CompileProgram(restrictSrc.c_str());
   mProgProlong = GLUtil::CompileProgram(prolong.c_str());
   mProgObsDown = GLUtil::CompileProgram(obsDown.c_str());
   mProgZero = GLUtil::CompileProgram(kZeroFrag);
   if (!(mProgResid && mProgRestrict && mProgProlong && mProgObsDown && mProgForce && mProgAdvectVel && mProgCurl && mProgVort && mProgVortApply && mProgDiv && mProgJacobi && mProgGrad &&
         mProgObs && mProgAdvectDye && mProgOut && mProgZero))
   {
      mProgOut = 0;
      return false;
   }
   return true;
}

void FluidNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // Pull every input before rendering, connected or not.
   const unsigned int srcTex = mInput.Pull(frameId);
   const unsigned int forceTex = mForce.Pull(frameId);
   const unsigned int obsTex = mObstacles.Pull(frameId);
   if (!EnsureShaders())
      return;

   const bool hasSrc = srcTex != 0;
   const int dyeW = hasSrc ? std::max(16, mInput.Width()) : 1024;
   const int dyeH = hasSrc ? std::max(16, mInput.Height()) : 576;
   const int div = resolution <= 0 ? 1 : (resolution == 1 ? 2 : 4);
   const int simW = std::max(16, dyeW / div);
   const int simH = std::max(16, dyeH / div);

   if (simW != mSimW || simH != mSimH || dyeW != mDyeW || dyeH != mDyeH)
   {
      mSimW = simW; mSimH = simH; mDyeW = dyeW; mDyeH = dyeH;
      mReset = true;
   }

   bool ok = true;
   for (int i = 0; i < 2 && ok; i++)
      ok = GLUtil::EnsureFbo(mVel[i], simW, simH, GL_RGBA32F, "Fluid")
        && GLUtil::EnsureFbo(mPressure[i], simW, simH, GL_RGBA32F, "Fluid")
        && GLUtil::EnsureFbo(mDye[i], dyeW, dyeH, GL_RGBA16F, "Fluid");
   // Velocity and pressure are 32-bit: a warm-started pressure runs into the
   // hundreds, where a half float steps by 0.5 and the projection cannot be exact.
   ok = ok && GLUtil::EnsureFbo(mDiv, simW, simH, GL_RGBA32F, "Fluid")
           && GLUtil::EnsureFbo(mCurl, simW, simH, GL_RGBA32F, "Fluid")
           && GLUtil::EnsureFbo(mObs, simW, simH, GL_RGBA16F, "Fluid")
           && GLUtil::EnsureFbo(mOut, dyeW, dyeH, GL_RGBA16F, "Fluid");
   // Coarser levels for the pressure solve: halve until the grid is small.
   mLevels = 1;
   mLvW[0] = simW;
   mLvH[0] = simH;
   while (mLevels < kMaxLevels && std::min(mLvW[mLevels - 1], mLvH[mLevels - 1]) >= 32)
   {
      mLvW[mLevels] = (mLvW[mLevels - 1] + 1) / 2;
      mLvH[mLevels] = (mLvH[mLevels - 1] + 1) / 2;
      mLevels++;
   }
   for (int l = 1; l < mLevels && ok; l++)
      ok = GLUtil::EnsureFbo(mLvP[l][0], mLvW[l], mLvH[l], GL_RGBA32F, "Fluid")
        && GLUtil::EnsureFbo(mLvP[l][1], mLvW[l], mLvH[l], GL_RGBA32F, "Fluid")
        && GLUtil::EnsureFbo(mLvRhs[l], mLvW[l], mLvH[l], GL_RGBA32F, "Fluid")
        && GLUtil::EnsureFbo(mLvRes[l], mLvW[l], mLvH[l], GL_RGBA32F, "Fluid")
        && GLUtil::EnsureFbo(mLvObs[l], mLvW[l], mLvH[l], GL_RGBA16F, "Fluid");
   if (!ok)
      return;

   if (mReset)
   {
      for (int i = 0; i < 2; i++)
      {
         GLUtil::RunShaderPass(mVel[i], mProgZero, []() {});
         GLUtil::RunShaderPass(mPressure[i], mProgZero, []() {});
      }
      mVelCur = mPressCur = mDyeCur = 0;
      mStep = 0;
      mHaveVelocity = true;
   }
   const bool fresh = mReset;
   mReset = false;

   // Obstacles for this frame.
   if (obsTex != 0)
      GLUtil::RunShaderPass(mObs, mProgObs, [&]() { Bind(mProgObs, 0, obsTex, "uSrc"); });
   else
      GLUtil::RunShaderPass(mObs, mProgZero, []() {});
   const unsigned int obs = GLUtil::FboTexture(mObs);

   // Forces.
   {
      const int src = mVelCur, dst = 1 - mVelCur;
      const float forceW = (float)std::max(1, mForce.Width());
      GLUtil::RunShaderPass(mVel[dst], mProgForce, [&]()
      {
         Bind(mProgForce, 0, GLUtil::FboTexture(mVel[src]), "uVel");
         Bind(mProgForce, 1, forceTex, "uForce");
         I1(mProgForce, "uHasForce", forceTex != 0 ? 1 : 0);
         F1(mProgForce, "uForceScale", forceGain * (float)simW / forceW);
         I1(mProgForce, "uStirrer", std::clamp(stirrer, 0, 3));
         F1(mProgForce, "uStrength", stirrerStrength);
         F1(mProgForce, "uSize", std::max(0.005f, stirrerSize));
         F1(mProgForce, "uTime", (float)mStep / 60.0f);
         F1(mProgForce, "uAspect", (float)dyeW / (float)dyeH);
      });
      mVelCur = dst;
   }

   // Move the velocity along itself.
   {
      const int src = mVelCur, dst = 1 - mVelCur;
      GLUtil::RunShaderPass(mVel[dst], mProgAdvectVel, [&]()
      {
         Bind(mProgAdvectVel, 0, GLUtil::FboTexture(mVel[src]), "uVel");
         Bind(mProgAdvectVel, 1, obs, "uObs");
         F1(mProgAdvectVel, "uVisc", std::clamp(viscosity, 0.0f, 0.24f));
         F1(mProgAdvectVel, "uDamp", std::clamp(velocityDamping, 0.0f, 0.5f));
      });
      mVelCur = dst;
   }

   // Swirl: curl per cell, a confinement force per cell (parked in mDiv, which the
   // projection overwrites next), then the force handed to the faces.
   if (vorticity > 0.0f)
   {
      GLUtil::RunShaderPass(mCurl, mProgCurl, [&]()
      {
         Bind(mProgCurl, 0, GLUtil::FboTexture(mVel[mVelCur]), "uVel");
         Bind(mProgCurl, 1, obs, "uObs");
      });
      GLUtil::RunShaderPass(mDiv, mProgVort, [&]()
      {
         Bind(mProgVort, 0, GLUtil::FboTexture(mCurl), "uCurl");
         F1(mProgVort, "uEps", vorticity);
      });
      const int src = mVelCur, dst = 1 - mVelCur;
      GLUtil::RunShaderPass(mVel[dst], mProgVortApply, [&]()
      {
         Bind(mProgVortApply, 0, GLUtil::FboTexture(mVel[src]), "uVel");
         Bind(mProgVortApply, 1, GLUtil::FboTexture(mDiv), "uForce");
         Bind(mProgVortApply, 2, obs, "uObs");
      });
      mVelCur = dst;
   }

   // Project: remove the divergence with a pressure solve.
   GLUtil::RunShaderPass(mDiv, mProgDiv, [&]()
   {
      Bind(mProgDiv, 0, GLUtil::FboTexture(mVel[mVelCur]), "uVel");
      Bind(mProgDiv, 1, obs, "uObs");
   });
   const int iters = std::clamp(pressureIterations, 1, kMaxPressureIterations);
   {
      // One multigrid V-cycle, warm-started from last frame's pressure: smooth,
      // push the residual down a level, solve there for a correction, bring it
      // back up and smooth again. `iters` is the smoothing on the finest grid.
      const GLUtil::Fbo* lvObs[kMaxLevels];
      const GLUtil::Fbo* lvRhs[kMaxLevels];
      const GLUtil::Fbo* lvRes[kMaxLevels];
      GLUtil::Fbo* lvP[kMaxLevels][2];
      int cur[kMaxLevels] = {};
      lvObs[0] = &mObs; lvRhs[0] = &mDiv; lvRes[0] = &mCurl;
      lvP[0][0] = &mPressure[0]; lvP[0][1] = &mPressure[1];
      cur[0] = mPressCur;
      for (int l = 1; l < mLevels; l++)
      {
         lvObs[l] = &mLvObs[l]; lvRhs[l] = &mLvRhs[l]; lvRes[l] = &mLvRes[l];
         lvP[l][0] = &mLvP[l][0]; lvP[l][1] = &mLvP[l][1];
         const GLUtil::Fbo& fineObs = *lvObs[l - 1];
         GLUtil::RunShaderPass(mLvObs[l], mProgObsDown, [&]() { Bind(mProgObsDown, 0, GLUtil::FboTexture(fineObs), "uFine"); });
      }
      auto smooth = [&](int l, int n)
      {
         for (int i = 0; i < n; i++)
         {
            const int src = cur[l], dst = 1 - cur[l];
            GLUtil::RunShaderPass(*lvP[l][dst], mProgJacobi, [&]()
            {
               Bind(mProgJacobi, 0, GLUtil::FboTexture(*lvP[l][src]), "uPress");
               Bind(mProgJacobi, 1, GLUtil::FboTexture(*lvRhs[l]), "uDiv");
               Bind(mProgJacobi, 2, GLUtil::FboTexture(*lvObs[l]), "uObs");
            });
            cur[l] = dst;
         }
      };
      const int pre = std::max(1, iters / 2), post = std::max(1, iters - iters / 2);
      for (int l = 0; l < mLevels - 1; l++)
      {
         smooth(l, l == 0 ? pre : 6);
         GLUtil::RunShaderPass(*const_cast<GLUtil::Fbo*>(lvRes[l]), mProgResid, [&]()
         {
            Bind(mProgResid, 0, GLUtil::FboTexture(*lvP[l][cur[l]]), "uPress");
            Bind(mProgResid, 1, GLUtil::FboTexture(*lvRhs[l]), "uDiv");
            Bind(mProgResid, 2, GLUtil::FboTexture(*lvObs[l]), "uObs");
         });
         GLUtil::RunShaderPass(*const_cast<GLUtil::Fbo*>(lvRhs[l + 1]), mProgRestrict, [&]()
         {
            Bind(mProgRestrict, 0, GLUtil::FboTexture(*lvRes[l]), "uFine");
         });
         GLUtil::RunShaderPass(*lvP[l + 1][0], mProgZero, []() {});
         GLUtil::RunShaderPass(*lvP[l + 1][1], mProgZero, []() {});
         cur[l + 1] = 0;
      }
      smooth(mLevels - 1, mLevels > 1 ? 30 : post);
      for (int l = mLevels - 2; l >= 0; l--)
      {
         const int src = cur[l], dst = 1 - cur[l];
         GLUtil::RunShaderPass(*lvP[l][dst], mProgProlong, [&]()
         {
            Bind(mProgProlong, 0, GLUtil::FboTexture(*lvP[l][src]), "uFine");
            Bind(mProgProlong, 1, GLUtil::FboTexture(*lvP[l + 1][cur[l + 1]]), "uCoarse");
         });
         cur[l] = dst;
         smooth(l, l == 0 ? post : 6);
      }
      mPressCur = cur[0];
   }
   {
      const int src = mVelCur, dst = 1 - mVelCur;
      GLUtil::RunShaderPass(mVel[dst], mProgGrad, [&]()
      {
         Bind(mProgGrad, 0, GLUtil::FboTexture(mVel[src]), "uVel");
         Bind(mProgGrad, 1, GLUtil::FboTexture(mPressure[mPressCur]), "uPress");
         Bind(mProgGrad, 2, obs, "uObs");
      });
      mVelCur = dst;
   }

   // Carry the picture.
   {
      const int src = mDyeCur, dst = 1 - mDyeCur;
      GLUtil::RunShaderPass(mDye[dst], mProgAdvectDye, [&]()
      {
         Bind(mProgAdvectDye, 0, GLUtil::FboTexture(mDye[src]), "uDye");
         Bind(mProgAdvectDye, 1, GLUtil::FboTexture(mVel[mVelCur]), "uVel");
         Bind(mProgAdvectDye, 2, srcTex, "uSrc");
         Bind(mProgAdvectDye, 3, obs, "uObs");
         I1(mProgAdvectDye, "uHasSrc", hasSrc ? 1 : 0);
         F1(mProgAdvectDye, "uRefresh", fresh ? 1.0f : std::clamp(refresh, 0.0f, 1.0f));
         F1(mProgAdvectDye, "uFade", std::clamp(dyeFade, 0.0f, 1.0f));
      });
      mDyeCur = dst;
   }

   GLUtil::RunShaderPass(mOut, mProgOut, [&]()
   {
      Bind(mProgOut, 0, GLUtil::FboTexture(mDye[mDyeCur]), "uDye");
      Bind(mProgOut, 1, GLUtil::FboTexture(mVel[mVelCur]), "uVel");
      Bind(mProgOut, 2, GLUtil::FboTexture(mPressure[mPressCur]), "uPress");
      Bind(mProgOut, 3, obs, "uObs");
      I1(mProgOut, "uView", std::clamp(view, 0, 3));
   });
   mStep++;
}

float FluidNode::MeasureDivergence()
{
   if (!mHaveVelocity || mVel[0].fbo == 0 || mSimW < 8 || mSimH < 8)
      return -1.0f;
   auto readback = [](const GLUtil::Fbo& f, std::vector<float>& out)
   {
      out.assign((size_t)f.w * f.h * 4, 0.0f);
      GLint prev = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
      glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
      glReadPixels(0, 0, f.w, f.h, GL_RGBA, GL_FLOAT, out.data());
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
   };
   std::vector<float> v, o;
   readback(mVel[mVelCur], v);
   readback(mObs, o);
   const int w = mSimW, h = mSimH;
   auto solid = [&](int x, int y) { return o[((size_t)y * w + x) * 4] > 0.5f; };
   float worst = 0.0f; int wx = 0, wy = 0;
   for (int y = 2; y < h - 2; y++)
      for (int x = 2; x < w - 2; x++)
      {
         if (solid(x, y) || solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1))
            continue;
         const float d = (v[((size_t)y * w + x + 1) * 4] - v[((size_t)y * w + x) * 4])
                       + (v[((size_t)(y + 1) * w + x) * 4 + 1] - v[((size_t)y * w + x) * 4 + 1]);
         if (std::fabs(d) > worst) { worst = std::fabs(d); wx = x; wy = y; }
      }
   return worst;
}

#pragma once
// The raster renderers' shared CPU side (Metal and Vulkan, OPUS_PLAN row 9.3): the frame / instance records in the
// shaders' layout, the material encoding (Render3DMaterial.h) and the shadow map's fit. One copy, so both backends
// feed their shaders the same numbers.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "engine/Scene.h"
#include "nodes/Render3DMaterial.h"

namespace rhi
{
namespace rr
{
   struct LightU { float vec[4], aim[4], color[4], terms[4], spot[4]; };
   struct FrameU
   {
      float viewProj[16], eye[4], ambient[4], sky[4], horizon[4], ground[4], params[4], lightVP[16], shP[4], shQ[4], gi[4], envm[4];
      LightU lights[8];
      float fld[4];
   };
   inline float ToLin(float c)
   {
      c = c < 0.0f ? 0.0f : c;
      return c < 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
   }
   inline void Lin3(const float* c, float* o, float scale = 1.0f)
   {
      for (int i = 0; i < 3; i++)
         o[i] = ToLin(c[i]) * scale;
      o[3] = 0;
   }

   // The material record (Render3DMaterial.h) as the shader's InstU / RInst carry it.
   struct InstU { float model[16], normalM[16]; R3dMatH mat; };
   static_assert(sizeof(InstU) == 496, "InstU layout as the shaders'");
   inline void Lin3x(const float* c, float* o) { for (int i = 0; i < 3; i++) o[i] = ToLin(c[i]); }
   // The surface the GI and reflection rays see: base, metal, roughness, ior, emission (no maps, sheen or hair).
   inline void FillMatRays(const engine::Instance& in, R3dMatH& m)
   {
      Lin3x(in.baseSrgb, m.base); m.metallic = in.metallic; m.roughness = in.roughness; m.ior = in.ior;
      std::memcpy(m.emission, in.emission, 12);
      m.program = (float)(in.fieldSeeThrough ? -in.fieldProgram : in.fieldProgram); // the rays run it too (row 1.4)
      m.fimg0 = (float)(in.fieldImage[0] + 1); m.fimg1 = (float)(in.fieldImage[1] + 1);
      m.fimg2 = (float)(in.fieldImage[2] + 1); m.fimg3 = (float)(in.fieldImage[3] + 1);
      std::memcpy(&m.p0, in.fieldParams, sizeof(in.fieldParams)); // row 3.4: knobs live, no relink
      m.strandFrom = (float)(in.strandFrom + 1); // row 5.2: the shadow rays tell strands from solids
      m.objRand = in.objRand;                    // row 7.2: Shade Geometry's random
   }
   // The whole material, encoded so all zeros is the plain material (see Render3DMaterial.h).
   inline void FillMat(const engine::Instance& in, R3dMatH& m)
   {
      FillMatRays(in, m);
      m.transmission = in.transmission;
      m.mapAlbedo = (float)(in.mapAlbedo + 1); m.mapRoughness = (float)(in.mapRoughness + 1);
      m.mapMetallic = (float)(in.mapMetallic + 1); m.mapNormal = (float)(in.mapNormal + 1);
      m.mapAo = (float)(in.mapAo + 1); m.mapEmission = (float)(in.mapEmission + 1); m.mapSheen = (float)(in.mapSheen + 1);
      m.mapOpacity = (float)(in.mapOpacity + 1); m.mapCoat = (float)(in.mapClearcoat + 1); m.mapTransmission = (float)(in.mapTransmission + 1);
      m.sheen = in.sheen; m.sheenRoughness = in.sheenRoughness; m.sheenDir = in.sheenDir; std::memcpy(m.sheenColor, in.sheenColor, 12);
      m.normalStrength = in.normalStrength; m.wrapBump = (float)((in.wrap & 3) + (in.normalBump ? 4 : 0));
      m.mapSpace = (float)in.mapSpace; m.triplanar = in.triplanar; m.alphaCutoff = in.alphaCutoff;
      m.clear = 1.0f - std::min(std::max(in.opacity, 0.0f), 1.0f);
      const float r = (in.ior - 1.0f) / (in.ior + 1.0f);
      const float f0 = std::min(std::max(r * r * 2.0f * in.specular, 0.0f), 1.0f);
      for (int k = 0; k < 3; k++)
      {
         m.mapScale[k] = in.mapScale[k]; m.mapRotate[k] = in.mapRotate[k]; m.mapTranslate[k] = in.mapTranslate[k];
         m.boxLo[k] = in.objLo[k]; m.boxHi[k] = in.objHi[k];
         m.hairSigma[k] = in.hairSigma[k]; m.sssRadius[k] = in.sssRadius[k];
      }
      m.coat = in.clearcoat; m.coatRoughness = in.clearcoatRoughness; m.f0Extra = f0 - 0.04f;
      m.breakup = in.breakup; m.detailScale = in.detailScale; m.detailStrength = in.detailStrength;
      m.hairFrom = (float)in.hairFrom; m.hairRadialNeg = -std::max(in.hairRadial, 0.0f); m.hairShift = in.hairShift;
      m.subsurface = in.subsurface; m.translucency = in.translucency;
      m.iridescence = in.iridescence; m.iridIor = in.iridescenceIor; m.iridThickness = in.iridescenceThickness;
   }
   // A run the depth prepass leaves out: it cuts pixels away, so its depth is known only once it is shaded.
   inline bool SeeThrough(const InstU& u) { return u.mat.clear > 0.0f || u.mat.mapOpacity > 0.5f || u.mat.alphaCutoff > 0.0f || u.mat.program < -0.5f; }
   inline bool Inv4(const float* m, float* o)
   {
      double a[16], inv[16];
      for (int i = 0; i < 16; i++) a[i] = m[i];
      inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
      inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
      inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
      inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
      inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
      inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
      inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
      inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
      inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
      inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
      inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
      inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
      inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
      inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
      inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
      inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
      const double det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
      if (std::fabs(det) < 1e-30) return false;
      for (int i = 0; i < 16; i++) o[i] = (float)(inv[i] / det);
      return true;
   }
   inline void Norm(float* v) { const float l = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); if (l > 1e-5f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
   // The frame record: camera, sky, tonemap, env flags and the first 8 lights (all of them in `all`, row 5.6).
   inline void FillFrameRecord(FrameU& f, const engine::Frame& fr, bool envMap, std::vector<LightU>& all)
   {
      f.fld[0] = fr.fieldTime;
      std::memcpy(f.viewProj, fr.viewProj, 64);
      std::memcpy(f.eye, fr.eye, 12);
      f.eye[3] = fr.rim;
      Lin3(fr.ambientSrgb, f.ambient);
      Lin3(fr.envSkySrgb, f.sky);
      Lin3(fr.envHorizonSrgb, f.horizon);
      Lin3(fr.envGroundSrgb, f.ground);
      const int n = (int)std::min<size_t>(fr.lights.size(), engine::kMaxLights);
      // Row 5.6: params.w keeps the first 8 (every per-pixel loop that walks f.lights); fld.y is all of them, in the
      // light buffer (`all`) the live froxel loop and settle's pick read.
      f.params[0] = fr.exposure; f.params[1] = (float)fr.tonemap; f.params[2] = fr.envIntensity; f.params[3] = (float)std::min(n, 8);
      f.fld[1] = (float)n;
      all.assign((size_t)std::max(n, 1), LightU{});
      f.envm[0] = (envMap ? 1.0f : 0.0f) + (envMap && fr.envBackground ? 2.0f : 0.0f);
      f.envm[1] = fr.envRotationRad; f.envm[2] = fr.envMaxLod; f.envm[3] = fr.envBaseLod;
      int visCh = 0;
      for (int i = 0; i < n; i++)
      {
         const engine::Light& l = fr.lights[i];
         LightU& o = all[(size_t)i];
         std::memcpy(o.vec, l.vec, 12); o.vec[3] = (float)(int)l.type;
         std::memcpy(o.aim, l.aim, 12);
         // aim.w: this light's channel in the half-res shadow mask (directional, point, sun, spot; first four), else -1
         o.aim[3] = -1.0f;
         if (l.type == engine::LightType::Directional || l.type == engine::LightType::Point || l.type == engine::LightType::Sun || l.type == engine::LightType::Spot)
            if (visCh < 4 && i < 8) o.aim[3] = (float)visCh++;
         Lin3(l.colorSrgb, o.color, l.intensity);
         o.color[3] = l.castShadow ? 0.0f : 1.0f; // 1: this light is never blocked
         o.terms[0] = l.diffuse; o.terms[1] = l.specular; o.terms[2] = l.falloff; o.terms[3] = l.cutoff;
         o.spot[0] = l.spotAngleDeg; o.spot[1] = l.spotPenumbra; o.spot[2] = l.areaW; o.spot[3] = l.areaH;
         if (l.type == engine::LightType::Area) { o.spot[0] = float(l.areaShape); o.spot[1] = l.areaSpread; } // area: shape, spread
         if (l.type == engine::LightType::Directional || l.type == engine::LightType::Sun) o.spot[0] = l.sunAngleDeg; // the disc, for these
         // Row 4.2: point / spot keep their IES row + 1 in spot.w (0 = none); the tables follow the lights in `all`.
         if (l.type == engine::LightType::Point || l.type == engine::LightType::Spot) o.spot[3] = l.iesRow >= 0 ? float(l.iesRow + 1) : 0.0f;
         if (i < 8) f.lights[i] = o;
      }
      if (n > 0 && !fr.ies.empty())
      {
         const size_t per = sizeof(LightU) / sizeof(float), extra = (fr.ies.size() + per - 1) / per;
         all.resize((size_t)n + extra, LightU{});
         std::memcpy(&all[(size_t)n], fr.ies.data(), fr.ies.size() * sizeof(float));
      }
   }
   // The instance records (dst, one per instance) and the draw runs: consecutive copies of one mesh with one
   // material (their random aside) and one castShadow, as (first, count).
   inline void BuildRunRecords(const engine::Frame& fr, int meshCount, InstU* dst, std::vector<std::pair<uint32_t, uint32_t>>& runs)
   {
      runs.clear();
      const size_t n = fr.instances.size();
      for (size_t i = 0; i < n; i++)
      {
         const engine::Instance& in = fr.instances[i];
         InstU& u = dst[i]; u = {};
         std::memcpy(u.model, in.model, 64);
         std::memcpy(u.normalM, in.normalMatrix, 64);
         FillMat(in, u.mat);
         if (in.mesh < 0 || in.mesh >= meshCount)
            continue;
         if (!runs.empty())
         {
            auto& r = runs.back();
            const engine::Instance& a = fr.instances[r.first];
            R3dMatH hm = dst[r.first].mat; hm.objRand = u.mat.objRand; // copies differ only in their random (VOut.orand carries it)
            if (r.first + r.second == i && a.mesh == in.mesh && a.castShadow == in.castShadow && std::memcmp(&hm, &u.mat, sizeof(R3dMatH)) == 0)
            {
               r.second++;
               continue;
            }
         }
         runs.emplace_back((uint32_t)i, 1u);
      }
   }
   // Fits an orthographic depth map to the scene's bounding sphere, as Render 3D does: fills lightVP, shP, shQ and
   // returns the light's view-projection in vpOut. False when light 0 casts no map (the caller then binds a dummy).
   // bounds(mesh, lo, hi) gives a mesh's object-space box, false for a mesh that is not drawn.
   inline bool ShadowFit(const engine::Frame& fr, const std::function<bool(int, float*, float*)>& bounds, FrameU& f, float* vpOut)
   {
      const engine::Shadow& sh = fr.shadow;
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      bool any = false;
      for (const engine::Instance& in : fr.instances)
      {
         float glo[3], ghi[3];
         if (!bounds(in.mesh, glo, ghi))
            continue;
         for (int c = 0; c < 8; c++)
         {
            const float p[3] = { (c & 1) ? ghi[0] : glo[0], (c & 2) ? ghi[1] : glo[1], (c & 4) ? ghi[2] : glo[2] };
            for (int k = 0; k < 3; k++)
            {
               const float w = in.model[k] * p[0] + in.model[4 + k] * p[1] + in.model[8 + k] * p[2] + in.model[12 + k];
               lo[k] = std::min(lo[k], w);
               hi[k] = std::max(hi[k], w);
            }
         }
         any = true;
      }
      if (!any)
         return false;
      const engine::Light& L = fr.lights[0];
      const float centre[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
      const float radius = std::max(0.01f, 0.5f * std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0]) + (hi[1]-lo[1])*(hi[1]-lo[1]) + (hi[2]-lo[2])*(hi[2]-lo[2])));
      float dir[3] = { L.vec[0], L.vec[1], L.vec[2] };
      const float dl = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
      if (dl > 1e-5f) Norm(dir); else { dir[0] = 0; dir[1] = 1; dir[2] = 0; }
      const float eye[3] = { centre[0] + dir[0] * radius * 2.5f, centre[1] + dir[1] * radius * 2.5f, centre[2] + dir[2] * radius * 2.5f };
      // LookAt, GL conventions
      const float upv[3] = { 0, std::fabs(dir[1]) > 0.99f ? 0.0f : 1.0f, std::fabs(dir[1]) > 0.99f ? 1.0f : 0.0f };
      float fw[3] = { centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2] };
      Norm(fw);
      float sd[3] = { fw[1]*upv[2] - fw[2]*upv[1], fw[2]*upv[0] - fw[0]*upv[2], fw[0]*upv[1] - fw[1]*upv[0] };
      Norm(sd);
      const float u[3] = { sd[1]*fw[2] - sd[2]*fw[1], sd[2]*fw[0] - sd[0]*fw[2], sd[0]*fw[1] - sd[1]*fw[0] };
      float V[16] = { sd[0], u[0], -fw[0], 0, sd[1], u[1], -fw[1], 0, sd[2], u[2], -fw[2], 0,
                      -(sd[0]*eye[0] + sd[1]*eye[1] + sd[2]*eye[2]), -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]),
                      (fw[0]*eye[0] + fw[1]*eye[1] + fw[2]*eye[2]), 1 };
      float clipNear = sh.clipNear;
      if (L.type == engine::LightType::Spot)
      {
         const float d[3] = { L.vec[0] - centre[0], L.vec[1] - centre[1], L.vec[2] - centre[2] };
         clipNear += radius * 2.5f - std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
      }
      clipNear = std::min(std::max(clipNear, 0.05f), radius * 4.0f);
      const float farZ = radius * 5.0f, hh = radius * 1.2f;
      float P[16] = {};
      P[0] = 1.0f / hh; P[5] = 1.0f / hh; P[10] = -2.0f / (farZ - clipNear); P[14] = -(farZ + clipNear) / (farZ - clipNear); P[15] = 1;
      float VP[16];
      for (int c = 0; c < 4; c++)
         for (int r = 0; r < 4; r++)
         {
            float a = 0;
            for (int k = 0; k < 4; k++) a += P[k * 4 + r] * V[c * 4 + k];
            VP[c * 4 + r] = a;
         }
      const float texelWorld = 2.4f * radius / (float)sh.size;
      const float biasDepth = (sh.bias + 2.5f * texelWorld * std::max(1.0f, std::min(sh.softness, 2.0f))) / (farZ - clipNear);
      std::memcpy(f.lightVP, VP, 64);
      std::memcpy(vpOut, VP, 64);
      f.shP[0] = 1; f.shP[1] = 0; f.shP[2] = biasDepth; f.shP[3] = sh.strength;
      f.shQ[0] = 1.0f / (float)sh.size; f.shQ[1] = sh.softness;
      return true;
   }
}
}

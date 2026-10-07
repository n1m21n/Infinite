#pragma once

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#ifndef INFINITE_VERSION_STRING
#define INFINITE_VERSION_STRING "0.0.0"
#endif

// Launcher screen: the first thing shown when Infinite opens.
//
// A compact card, centred over a dimmed window (the way Bitwig opens):
//   - the Infinite mark (the same lemniscate the website draws) strokes itself in, then a
//     bead rides the loop;
//   - bottom left: Author / Contributors / Agent; bottom right: the version;
//   - queued background tasks (AddTask) run one per frame behind it, with a hairline of
//     progress, so slow start-up work has somewhere to live without freezing a blank window.
//
// The overlay owns input while it is up (a topmost window), and any click, Space, Enter or
// Escape skips it once it has been on screen for kSkipAfter. It ends after kMinShow once
// every queued task has run. Never shown in headless / test runs: the
// caller decides (see main.cpp), this file only draws.
//
// To credit someone new, add a line to Credits() below; nothing else needs to change.
namespace Splash
{
   struct CreditSection
   {
      const char* heading;
      std::vector<const char*> names;
   };

   // Three groups, in this order. Contributors are people who have landed work through
   // GitHub (PRs, issues with reproductions); add new ones to the middle list.
   inline const std::vector<CreditSection>& Credits()
   {
      static const std::vector<CreditSection> k = {
         {"AUTHOR", {"Naman"}},
         {"CONTRIBUTORS", {"Ricardo Palmieri"}},
         {"AGENT", {"Claude"}},
      };
      return k;
   }

   constexpr float kLogoDrawStart = 0.20f; // s
   constexpr float kLogoDrawEnd = 1.50f;
   constexpr float kMinShow = 2.40f; // shortest time on screen once the mark has drawn in
   constexpr float kSkipAfter = 0.70f;
   constexpr float kFadeOut = 0.45f; // logo melts into the backdrop first, then the backdrop fades
   constexpr float kHardCap = 12.0f; // never hold the app hostage, whatever the tasks do

   struct State
   {
      bool started = false;
      bool finished = false;
      double t0 = 0.0;
      double fadeStart = -1.0; // >= 0 once the exit fade has begun
      size_t tasksTotal = 0;
      size_t tasksDone = 0;
   };

   inline State& S()
   {
      static State s;
      return s;
   }

   struct Task
   {
      std::string label;
      std::function<void()> fn;
   };

   inline std::vector<Task>& Tasks()
   {
      static std::vector<Task> t;
      return t;
   }

   // Queue background work. Runs on the UI thread, one task per frame, after the first
   // splash frame has been drawn. Keep each task short; split big ones.
   inline void AddTask(const char* label, std::function<void()> fn)
   {
      Tasks().push_back({label ? label : "", std::move(fn)});
      ++S().tasksTotal;
   }

   inline bool Active()
   {
      return S().started && !S().finished;
   }

   // Call once, right before the main loop.
   inline void Begin(float startAtSeconds = 0.0f)
   {
      S() = State{};
      S().started = true;
      S().t0 = ImGui::GetTime() - startAtSeconds;
   }

   // ---- pure helpers (kept free of ImGui state so the maths is easy to eyeball) ----------

   inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
   inline float EaseInOutCubic(float x)
   {
      x = Clamp01(x);
      return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) * 0.5f;
   }
   inline float Smooth(float x)
   {
      x = Clamp01(x);
      return x * x * (3.0f - 2.0f * x);
   }

   // Lemniscate of Bernoulli, unit half-width: the website logo's exact curve.
   inline ImVec2 Lemniscate(float p)
   {
      const float s = std::sin(p), c = std::cos(p), q = 1.0f + c * c;
      return ImVec2(s / q, s * c / q);
   }

   // Brand gradient, coral (left) to sky (right); u in [0,1].
   inline ImU32 BrandColor(float u, float alpha)
   {
      struct Stop { float at; float r, g, b; };
      static const Stop stops[] = {
         {0.00f, 0xF5, 0x7F, 0x66}, {0.35f, 0xD9, 0x9A, 0x9C}, {0.65f, 0xC7, 0xAF, 0xC0}, {1.00f, 0xA9, 0xCD, 0xF1}};
      u = Clamp01(u);
      for (int i = 0; i < 3; ++i)
      {
         if (u <= stops[i + 1].at)
         {
            const float k = (u - stops[i].at) / (stops[i + 1].at - stops[i].at);
            return IM_COL32((int)(stops[i].r + (stops[i + 1].r - stops[i].r) * k),
                            (int)(stops[i].g + (stops[i + 1].g - stops[i].g) * k),
                            (int)(stops[i].b + (stops[i + 1].b - stops[i].b) * k), (int)(alpha * 255.0f));
         }
      }
      return IM_COL32(0xA9, 0xCD, 0xF1, (int)(alpha * 255.0f));
   }

   // Draws the overlay. Returns true while it is still up this frame.
   inline bool Draw()
   {
      State& st = S();
      if (!st.started || st.finished)
         return false;

      ImGuiIO& io = ImGui::GetIO();
      const float W = io.DisplaySize.x, H = io.DisplaySize.y;
      if (W <= 1.0f || H <= 1.0f)
         return true;

      const double now = ImGui::GetTime();
      const float t = (float)(now - st.t0);

      if (st.fadeStart < 0.0)
         if (const char* f = getenv("INFINITE_SPLASHFADE")) // dev: start the exit fade, N s in, once the clock allows
            if (now > atof(f) + 0.05)
               st.fadeStart = now - atof(f);

      // --- input: skip -------------------------------------------------------------------
      if (st.fadeStart < 0.0 && t >= kSkipAfter)
      {
         if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            st.fadeStart = now;
      }

      // --- background tasks: one per frame, only once the logo is on screen --------------
      if (t > 0.1f && !Tasks().empty())
      {
         Task task = std::move(Tasks().front());
         Tasks().erase(Tasks().begin());
         if (task.fn)
            task.fn();
         ++st.tasksDone;
      }

      if (st.fadeStart < 0.0 && ((t > kMinShow && Tasks().empty()) || t > kHardCap))
         st.fadeStart = now;

      // Exit: the logo, bead and text never go translucent (the figure-eight overlaps itself at
      // the crossing, and translucent overlap shows as a polygon). They blend opaquely into the
      // card colour (fadeK) and are then no longer drawn; only then does the card itself fade.
      float alpha = 1.0f, fadeK = 0.0f;
      if (st.fadeStart >= 0.0)
      {
         const float e = (float)(now - st.fadeStart);
         fadeK = Smooth(e / 0.18f);
         alpha = 1.0f - Smooth((e - 0.15f) / 0.30f);
         if (alpha <= 0.0f)
         {
            st.finished = true;
            return false;
         }
      }

      // --- layout: a card about 600 x 380 points ----------------------------------------
      const float sc = std::max(0.7f, std::min(1.4f, std::min(W / 760.0f, H / 520.0f)));
      const float cardW = 600.0f * sc, cardH = 380.0f * sc;
      const float x0 = std::floor((W - cardW) * 0.5f), y0 = std::floor((H - cardH) * 0.5f);
      const float x1 = x0 + cardW, y1 = y0 + cardH;
      const float rounding = 18.0f * sc;
      const float pad = 26.0f * sc;
      const float A = 105.0f * sc;      // lemniscate half-width
      const float strokeW = 0.257f * A; // 87/338 of A, as on the website
      const float cx = (x0 + x1) * 0.5f;
      const float cy = y0 + cardH * 0.43f;

      // The card's own gradient (the icon's navy, a touch lighter toward the bottom).
      auto cardCol = [&](float y, float a) {
         const float k = Clamp01((y - y0) / cardH);
         return IM_COL32((int)(0x15 + (0x23 - 0x15) * k), (int)(0x1A + (0x27 - 0x1A) * k), (int)(0x2F + (0x46 - 0x2F) * k),
                         (int)(a * 255.0f));
      };
      // Blend a colour toward the card gradient at height y, keeping its alpha.
      auto mixBg = [&](ImU32 c, float y) {
         const ImU32 b = cardCol(y, 1.0f);
         const float r = (float)((c >> IM_COL32_R_SHIFT) & 255), g = (float)((c >> IM_COL32_G_SHIFT) & 255),
                     bl = (float)((c >> IM_COL32_B_SHIFT) & 255);
         const float br = (float)((b >> IM_COL32_R_SHIFT) & 255), bg = (float)((b >> IM_COL32_G_SHIFT) & 255),
                     bb = (float)((b >> IM_COL32_B_SHIFT) & 255);
         return IM_COL32((int)(r + (br - r) * fadeK), (int)(g + (bg - g) * fadeK), (int)(bl + (bb - bl) * fadeK),
                         (int)((c >> IM_COL32_A_SHIFT) & 255));
      };

      // --- the topmost window that owns input --------------------------------------------
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::SetNextWindowFocus();
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::Begin("##infinite_launcher", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::InvisibleButton("##swallow", io.DisplaySize); // eats clicks meant for the canvas
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // dim the app behind the card, then the card with a soft shadow
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 8, 16, (int)(150.0f * alpha)));
      for (int i = 6; i >= 1; --i)
         dl->AddRectFilled(ImVec2(x0 - i * 4.0f * sc, y0 - i * 3.0f * sc + 10.0f * sc),
                           ImVec2(x1 + i * 4.0f * sc, y1 + i * 3.0f * sc + 10.0f * sc), IM_COL32(0, 0, 0, (int)(9.0f * alpha)),
                           rounding + i * 4.0f * sc);
      {
         // Rounded card with a vertical gradient: clip the gradient rect to the rounded shape by
         // drawing a solid rounded rect, then a gradient inset by the corner radius in the middle.
         const ImU32 top = cardCol(y0, alpha), bot = cardCol(y1, alpha);
         dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), cardCol((y0 + y1) * 0.5f, alpha), rounding);
         dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + rounding * 2.0f), top, rounding, ImDrawFlags_RoundCornersTop);
         dl->AddRectFilled(ImVec2(x0, y1 - rounding * 2.0f), ImVec2(x1, y1), bot, rounding, ImDrawFlags_RoundCornersBottom);
         dl->AddRectFilledMultiColor(ImVec2(x0, y0 + rounding * 2.0f), ImVec2(x1, y1 - rounding * 2.0f),
                                     cardCol(y0 + rounding * 2.0f, alpha), cardCol(y0 + rounding * 2.0f, alpha),
                                     cardCol(y1 - rounding * 2.0f, alpha), cardCol(y1 - rounding * 2.0f, alpha));
      }

      // --- logo: stroke draws itself in, then a bead rides the loop ----------------------
      const float draw = EaseInOutCubic((t - kLogoDrawStart) / (kLogoDrawEnd - kLogoDrawStart));
      const float twoPi = 6.28318530718f;
      if (draw > 0.0f && fadeK < 0.999f)
      {
         // One continuous triangle strip with a 1-unit antialiased fringe on both edges.
         // Stacking round-capped AddLine segments instead leaves a dotted, aliased outline.
         const int kSeg = 480;
         const int samples = std::max(2, (int)std::ceil(draw * (float)kSeg) + 1);
         const float hw = strokeW * 0.5f, aa = 1.0f;
         const ImVec2 uv = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
         dl->PrimReserve((samples - 1) * 18, samples * 4);
         const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
         for (int i = 0; i < samples; ++i)
         {
            const float p = twoPi * draw * (float)i / (float)(samples - 1);
            const ImVec2 q0 = Lemniscate(p - 0.0005f), q1 = Lemniscate(p + 0.0005f), q = Lemniscate(p);
            float tx = (q1.x - q0.x), ty = (q1.y - q0.y);
            const float tl = std::sqrt(tx * tx + ty * ty);
            tx = tl > 0.0f ? tx / tl : 1.0f;
            ty = tl > 0.0f ? ty / tl : 0.0f;
            const float nx = -ty, ny = tx;
            const ImVec2 c(cx + q.x * A, cy + q.y * A);
            const ImU32 on = mixBg(BrandColor((q.x + 1.0f) * 0.5f, 1.0f), c.y);
            const ImU32 off = on & ~IM_COL32_A_MASK;
            dl->PrimWriteVtx(ImVec2(c.x + nx * (hw + aa), c.y + ny * (hw + aa)), uv, off);
            dl->PrimWriteVtx(ImVec2(c.x + nx * hw, c.y + ny * hw), uv, on);
            dl->PrimWriteVtx(ImVec2(c.x - nx * hw, c.y - ny * hw), uv, on);
            dl->PrimWriteVtx(ImVec2(c.x - nx * (hw + aa), c.y - ny * (hw + aa)), uv, off);
         }
         for (int i = 0; i + 1 < samples; ++i)
         {
            const ImDrawIdx r0 = (ImDrawIdx)(base + i * 4), r1 = (ImDrawIdx)(base + (i + 1) * 4);
            for (int k = 0; k < 3; ++k)
            {
               dl->PrimWriteIdx((ImDrawIdx)(r0 + k));
               dl->PrimWriteIdx((ImDrawIdx)(r1 + k));
               dl->PrimWriteIdx((ImDrawIdx)(r1 + k + 1));
               dl->PrimWriteIdx((ImDrawIdx)(r0 + k));
               dl->PrimWriteIdx((ImDrawIdx)(r1 + k + 1));
               dl->PrimWriteIdx((ImDrawIdx)(r0 + k + 1));
            }
         }
         if (draw < 1.0f) // round head while the stroke is still drawing in
         {
            const ImVec2 hq = Lemniscate(twoPi * draw);
            dl->AddCircleFilled(ImVec2(cx + hq.x * A, cy + hq.y * A), hw,
                                mixBg(BrandColor((hq.x + 1.0f) * 0.5f, 1.0f), cy + hq.y * A), 48);
         }
      }

      if (t > kLogoDrawEnd && fadeK < 0.999f)
      {
         const float bt = t - kLogoDrawEnd;
         const float fadeIn = Smooth(bt * 3.0f);
         const float u = std::fmod(bt / 4.0f, 1.0f);
         const float e = u + 0.035f * std::sin(u * 3.14159265f * 4.0f); // quicker through the crossing
         const ImVec2 q = Lemniscate(twoPi * (e - std::floor(e)));
         const ImVec2 c(cx + q.x * A, cy + q.y * A);
         const float r = strokeW * 0.30f;
         dl->AddCircleFilled(c, r, mixBg(IM_COL32(0xC2, 0x59, 0x3F, (int)(255.0f * fadeIn)), c.y), 24);
         dl->AddCircleFilled(ImVec2(c.x - r * 0.18f, c.y - r * 0.2f), r * 0.78f,
                             mixBg(IM_COL32(0xF9, 0xA5, 0x8F, (int)(255.0f * fadeIn)), c.y), 24);
         dl->AddCircleFilled(ImVec2(c.x - r * 0.34f, c.y - r * 0.38f), r * 0.28f,
                             mixBg(IM_COL32(255, 255, 255, (int)(230.0f * fadeIn)), c.y), 16);
      }

      // --- bottom left: credits; bottom right: version -----------------------------------
      if (fadeK < 0.999f)
      {
         ImFont* font = ImGui::GetFont();
         const float textA = Smooth((t - 0.6f) * 2.0f);
         const float labelFs = 8.5f * sc, nameFs = 11.5f * sc, rowH = 17.0f * sc;
         const float labelW = 92.0f * sc;
         const float rows = (float)Credits().size();
         float y = y1 - pad - rows * rowH + (rowH - nameFs) * 0.5f;
         for (const CreditSection& sec : Credits())
         {
            std::string names;
            for (const char* n : sec.names)
            {
               if (!names.empty())
                  names += ", ";
               names += n;
            }
            std::string spaced;
            for (const char* p = sec.heading; *p; ++p)
            {
               spaced += *p;
               if (p[1]) spaced += ' ';
            }
            dl->AddText(font, labelFs, ImVec2(x0 + pad, y + (nameFs - labelFs) * 0.5f),
                        mixBg(IM_COL32(0x8F, 0x98, 0xB8, (int)(255.0f * textA)), y), spaced.c_str());
            dl->AddText(font, nameFs, ImVec2(x0 + pad + labelW, y),
                        mixBg(IM_COL32(0xE8, 0xEC, 0xF8, (int)(255.0f * textA)), y), names.c_str());
            y += rowH;
         }
         const std::string ver = std::string("v") + INFINITE_VERSION_STRING;
         const float vfs = 11.5f * sc;
         const ImVec2 vsz = font->CalcTextSizeA(vfs, FLT_MAX, 0.0f, ver.c_str());
         dl->AddText(font, vfs, ImVec2(x1 - pad - vsz.x, y1 - pad - vsz.y),
                     mixBg(IM_COL32(0x8F, 0x98, 0xB8, (int)(255.0f * textA)), y1 - pad), ver.c_str());

         // progress hairline under the mark, only while background tasks are queued
         if (st.tasksTotal > 0)
         {
            const float frac = (float)st.tasksDone / (float)st.tasksTotal;
            const float w = 120.0f * sc, lx = cx - w * 0.5f, ly = cy + A * 0.5f + 34.0f * sc;
            dl->AddLine(ImVec2(lx, ly), ImVec2(lx + w, ly), mixBg(IM_COL32(0x8F, 0x98, 0xB8, 60), ly), 1.0f);
            dl->AddLine(ImVec2(lx, ly), ImVec2(lx + w * frac, ly), mixBg(BrandColor(0.5f, 1.0f), ly), 1.5f);
         }
      }

      ImGui::End();
      ImGui::PopStyleVar(3);
      return true;
   }
} // namespace Splash

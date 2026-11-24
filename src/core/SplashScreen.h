#pragma once

#include "imgui.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"

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
//   - bottom left: one small footer line (trademark, made in India, open source); bottom right: the version;
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
      bool cardOnly = false; // the OS window IS the card (main.cpp shrinks it); no dim, no shadow, square corners
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
   inline void Begin(float startAtSeconds = 0.0f, bool cardOnly = false)
   {
      S() = State{};
      S().started = true;
      S().cardOnly = cardOnly;
      S().t0 = ImGui::GetTime() - startAtSeconds;
   }

   // ---- pure helpers (kept free of ImGui state so the maths is easy to eyeball) ----------

   inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
   inline float EaseInOutCubic(float x)
   {
      x = Clamp01(x);
      return x < tok::splash_screen_ease_in_out_cubic_limit ? tok::splash_screen_ease_in_out_cubic_ratio * x * x * x : 1.0f - std::pow(-tok::splash_screen_ease_in_out_cubic_scale * x + tok::space_0_5, tok::splash_screen_ease_in_out_cubic) * 0.5f;
   }
   inline float Smooth(float x)
   {
      x = Clamp01(x);
      return x * x * (3.0f - tok::splash_screen_smooth_ratio * x);
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
      return IM_COL32(tok::splash_screen_brand_color_shade, tok::splash_screen_brand_color_alpha_sm, tok::splash_screen_brand_color_alpha_lg, (int)(alpha * 255.0f));
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
         alpha = 1.0f - Smooth((e - tok::splash_screen_inset) / tok::splash_screen_smooth_in);
         if (st.cardOnly)
            alpha = 1.0f; // the window itself is the card; the main window is restored right after the melt
         if (st.cardOnly ? e > 0.22f : alpha <= 0.0f)
         {
            st.finished = true;
            return false;
         }
      }

      // --- layout: a card about 600 x 380 points ----------------------------------------
      const float sc = st.cardOnly ? std::min(W / tok::splash_screen_sc_max_md, H / tok::splash_screen_sc_max_xs)
                                   : std::max(0.7f, std::min(1.4f, std::min(W / tok::splash_screen_sc_max_lg, H / tok::splash_screen_sc_max_sm)));
      const float cardW = st.cardOnly ? W : tok::splash_screen_card_w_ratio * sc, cardH = st.cardOnly ? H : tok::splash_screen_card_h_ratio * sc;
      const float x0 = st.cardOnly ? 0.0f : std::floor((W - cardW) * 0.5f);
      const float y0 = st.cardOnly ? 0.0f : std::floor((H - cardH) * 0.5f);
      const float x1 = x0 + cardW, y1 = y0 + cardH;
      const float rounding = 18.0f * sc;
      const float pad = tok::splash_screen_pad_ratio * sc;
      const float A = 105.0f * sc;      // lemniscate half-width
      const float strokeW = tok::splash_screen_stroke_w_ratio * A; // 87/338 of A, as on the website
      const float cx = (x0 + x1) * 0.5f;
      const float cy = y0 + cardH * tok::splash_screen_cy_ratio;

      // The card's own gradient (the icon's navy, a touch lighter toward the bottom).
      auto cardCol = [&](float y, float a) {
         const float k = Clamp01((y - y0) / cardH);
         constexpr tok::Rgba8 t = tok::splash_screen_card_top_shade, b = tok::splash_screen_card_bottom_shade;
         return IM_COL32((int)(t.r + (b.r - t.r) * k), (int)(t.g + (b.g - t.g) * k), (int)(t.b + (b.b - t.b) * k),
                         (int)(a * 255.0f));
      };
      // Blend a colour toward the card gradient at height y, keeping its alpha.
      auto mixBg = [&](ImU32 c, float y) {
         const ImU32 b = cardCol(y, 1.0f);
         const float r = (float)((c >> IM_COL32_R_SHIFT) & tok::splash_screen_br_shade), g = (float)((c >> IM_COL32_G_SHIFT) & 255),
                     bl = (float)((c >> IM_COL32_B_SHIFT) & 255);
         const float br = (float)((b >> IM_COL32_R_SHIFT) & tok::splash_screen_br_shade), bg = (float)((b >> IM_COL32_G_SHIFT) & 255),
                     bb = (float)((b >> IM_COL32_B_SHIFT) & tok::splash_screen_br_shade);
         return IM_COL32((int)(r + (br - r) * fadeK), (int)(g + (bg - g) * fadeK), (int)(bl + (bb - bl) * fadeK),
                         (int)((c >> IM_COL32_A_SHIFT) & tok::splash_screen_br_shade));
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
      if (!st.cardOnly)
         dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), tok::U32(tok::splash_screen_fill_shade, (int)(tok::splash_screen_shade_sm * alpha)));
      for (int i = st.cardOnly ? 0 : 6; i >= 1; --i)
         dl->AddRectFilled(ImVec2(x0 - i * tok::splash_screen_fill_x * sc, y0 - i * tok::splash_screen_fill_y_md * sc + tok::splash_screen_fill_y_lg * sc),
                           ImVec2(x1 + i * tok::splash_screen_fill_x * sc, y1 + i * tok::splash_screen_fill_y_md * sc + tok::splash_screen_fill_y_lg * sc), IM_COL32(0, 0, 0, (int)(tok::splash_screen_shade_xs * alpha)),
                           rounding + i * tok::splash_screen_fill_x * sc);
      {
         // Rounded card with a vertical gradient: clip the gradient rect to the rounded shape by
         // drawing a solid rounded rect, then a gradient inset by the corner radius in the middle.
         const ImU32 top = cardCol(y0, alpha), bot = cardCol(y1, alpha);
         dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), cardCol((y0 + y1) * 0.5f, alpha), rounding);
         dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + rounding * tok::splash_screen_fill_y_sm), top, rounding, ImDrawFlags_RoundCornersTop);
         dl->AddRectFilled(ImVec2(x0, y1 - rounding * tok::splash_screen_fill_y_sm), ImVec2(x1, y1), bot, rounding, ImDrawFlags_RoundCornersBottom);
         dl->AddRectFilledMultiColor(ImVec2(x0, y0 + rounding * tok::splash_screen_fill_y_sm), ImVec2(x1, y1 - rounding * tok::splash_screen_fill_y_sm),
                                     cardCol(y0 + rounding * tok::splash_screen_card_col_scale, alpha), cardCol(y0 + rounding * tok::splash_screen_card_col_scale, alpha),
                                     cardCol(y1 - rounding * tok::splash_screen_card_col_scale, alpha), cardCol(y1 - rounding * tok::splash_screen_card_col_scale, alpha));
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
                                mixBg(BrandColor((hq.x + 1.0f) * 0.5f, 1.0f), cy + hq.y * A), tok::splash_screen_dot_segments_lg);
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
         const float r = strokeW * tok::splash_screen_r_ratio;
         dl->AddCircleFilled(c, r, mixBg(IM_COL32(tok::splash_screen_mix_bg_shade_md, tok::splash_screen_mix_bg_alpha_md, tok::splash_screen_mix_bg_alpha_sm, (int)(tok::splash_screen_shade_lg * fadeIn)), c.y), tok::splash_screen_dot_segments_md);
         dl->AddCircleFilled(ImVec2(c.x - r * tok::splash_screen_dot_x_sm, c.y - r * tok::splash_screen_dot_y_sm), r * tok::splash_screen_dot_r_lg,
                             mixBg(IM_COL32(tok::splash_screen_mix_bg_shade_lg, tok::splash_screen_mix_bg_alpha_lg, tok::splash_screen_mix_bg_shade_sm, (int)(tok::splash_screen_shade_lg * fadeIn)), c.y), tok::splash_screen_dot_segments_md);
         dl->AddCircleFilled(ImVec2(c.x - r * tok::splash_screen_dot_x_lg, c.y - r * tok::splash_screen_dot_y_lg), r * tok::splash_screen_dot_r_sm,
                             mixBg(tok::U32(tok::splash_screen_mix_bg_shade, (int)(tok::splash_screen_shade_md * fadeIn)), c.y), tok::splash_screen_dot_segments_sm);
      }

      // --- bottom left: credits; bottom right: version -----------------------------------
      if (fadeK < 0.999f)
      {
         ImFont* font = ImGui::GetFont();
         const float textA = Smooth((t - 0.6f) * 2.0f);
         // One quiet footer line, like Bitwig's. Names are not shown for now (Credits() is kept for later).
         const float fs = 8.5f * sc;
         // The font has no trademark glyph, so "TM" is drawn as a small raised superscript.
         const ImU32 footCol = IM_COL32(tok::splash_screen_mix_bg_shade_sm, tok::splash_screen_foot_col_alpha_sm, tok::splash_screen_foot_col_alpha_lg, (int)(tok::splash_screen_foot_col_shade * textA));
         const float fy = y1 - pad - fs;
         float fx = x0 + pad;
         dl->AddText(font, fs, ImVec2(fx, fy), mixBg(footCol, fy), "Infinite");   // i18n-ok: brand mark, never translated
         fx += font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "Infinite").x + 1.0f * sc;
         dl->AddText(font, fs * tok::splash_screen_text_font_size, ImVec2(fx, fy - fs * tok::splash_screen_text_y), mixBg(footCol, fy), "TM");   // i18n-ok: brand mark, never translated
         fx += font->CalcTextSizeA(fs * 0.55f, FLT_MAX, 0.0f, "TM").x;
         dl->AddText(font, fs, ImVec2(fx, fy), mixBg(footCol, fy), "   |   Made in India   |   Open source (MIT license)");   // i18n-ok: brand footer, never translated
         const std::string ver = std::string("v") + INFINITE_VERSION_STRING;
         const float vfs = 8.5f * sc;
         const ImVec2 vsz = font->CalcTextSizeA(vfs, FLT_MAX, 0.0f, ver.c_str());
         dl->AddText(font, vfs, ImVec2(x1 - pad - vsz.x, y1 - pad - vsz.y),
                     mixBg(footCol, y1 - pad), ver.c_str());

         // progress hairline under the mark, only while background tasks are queued
         if (st.tasksTotal > 0)
         {
            const float frac = (float)st.tasksDone / (float)st.tasksTotal;
            const float w = tok::splash_screen_w_ratio * sc, lx = cx - w * 0.5f, ly = cy + A * 0.5f + 34.0f * sc;
            dl->AddLine(ImVec2(lx, ly), ImVec2(lx + w, ly), mixBg(IM_COL32(tok::splash_screen_mix_bg_shade_sm, tok::splash_screen_foot_col_alpha_sm, tok::splash_screen_foot_col_alpha_lg, tok::splash_screen_mix_bg_alpha), ly), tok::splash_screen_line_stroke_sm);
            dl->AddLine(ImVec2(lx, ly), ImVec2(lx + w * frac, ly), mixBg(BrandColor(tok::splash_screen_brand_color_u, tok::splash_screen_brand_color_alpha), ly), tok::splash_screen_line_stroke_lg);
         }
      }

      ImGui::End();
      ImGui::PopStyleVar(3);
      return true;
   }
} // namespace Splash

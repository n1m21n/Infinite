#pragma once

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// Launcher screen: the first thing shown when Infinite opens.
//
// A full-window overlay drawn over the normal UI for the first few seconds:
//   - the Infinite mark (the same lemniscate the website draws) strokes itself in, then a
//     bead rides the loop;
//   - below it the credits roll upward like end credits: Authors, Contributors, Agents;
//   - queued background tasks (AddTask) run one per frame behind it, with a hairline of
//     progress, so slow start-up work has somewhere to live without freezing a blank window.
//
// The overlay owns input while it is up (a topmost window), and any click, Space, Enter or
// Escape skips it once it has been on screen for kSkipAfter. It ends when the credits have
// scrolled out *and* every queued task has run. Never shown in headless / test runs: the
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
   constexpr float kCreditsStart = 1.00f;
   constexpr float kSkipAfter = 0.70f;
   constexpr float kFadeOut = 0.45f;
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

   // Total scroll distance, in layout units (multiply by scale), until the credits have
   // fully left the region of height regionH.
   inline float CreditsContentHeight()
   {
      float h = 0.0f;
      for (const CreditSection& sec : Credits())
         h += 18.0f + 10.0f + 32.0f * (float)sec.names.size() + 46.0f;
      return h;
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

      // --- input: skip -------------------------------------------------------------------
      if (st.fadeStart < 0.0 && t >= kSkipAfter)
      {
         if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            st.fadeStart = now;
      }

      // --- background tasks: one per frame, only once the logo is on screen --------------
      std::string taskLabel;
      if (t > 0.1f && !Tasks().empty())
      {
         Task task = std::move(Tasks().front());
         Tasks().erase(Tasks().begin());
         taskLabel = task.label;
         if (task.fn)
            task.fn();
         ++st.tasksDone;
      }

      // --- layout ------------------------------------------------------------------------
      const float sc = std::max(0.8f, std::min(1.6f, std::min(W / 900.0f, H / 640.0f)));
      const float A = 130.0f * sc;          // lemniscate half-width
      const float strokeW = 0.257f * A;     // 87/338 of A, as on the website
      const float cx = W * 0.5f;
      const float cy = H * 0.34f;

      const float regionTop = cy + A * 0.5f + 46.0f * sc;
      const float regionBot = H - 48.0f * sc;
      const float regionH = std::max(40.0f, regionBot - regionTop);
      const float speed = 78.0f * sc;       // px / s
      const float creditsT = std::max(0.0f, t - kCreditsStart);
      const float scrolled = creditsT * speed;
      const float contentH = CreditsContentHeight() * sc;
      const bool creditsDone = scrolled > regionH + contentH;

      if (st.fadeStart < 0.0 && ((creditsDone && Tasks().empty()) || t > kHardCap))
         st.fadeStart = now;

      float alpha = 1.0f;
      if (st.fadeStart >= 0.0)
      {
         alpha = 1.0f - Smooth((float)((now - st.fadeStart) / kFadeOut));
         if (alpha <= 0.0f)
         {
            st.finished = true;
            return false;
         }
      }

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

      // background: the icon's navy, slightly lighter toward the bottom
      const int a255 = (int)(alpha * 255.0f);
      dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0x15, 0x1A, 0x2F, a255), IM_COL32(0x15, 0x1A, 0x2F, a255),
                                  IM_COL32(0x23, 0x27, 0x46, a255), IM_COL32(0x23, 0x27, 0x46, a255));

      // --- logo: stroke draws itself in, then a bead rides the loop ----------------------
      const float draw = EaseInOutCubic((t - kLogoDrawStart) / (kLogoDrawEnd - kLogoDrawStart));
      const int kSeg = 220;
      const int segsShown = (int)std::ceil(draw * (float)kSeg);
      const float twoPi = 6.28318530718f;
      for (int k = 0; k < segsShown; ++k)
      {
         const ImVec2 p0 = Lemniscate(twoPi * (float)k / (float)kSeg);
         const ImVec2 p1 = Lemniscate(twoPi * (float)(k + 1) / (float)kSeg);
         const ImVec2 a(cx + p0.x * A, cy + p0.y * A), b(cx + p1.x * A, cy + p1.y * A);
         const ImU32 col = BrandColor((p1.x + 1.0f) * 0.5f, alpha);
         dl->AddLine(a, b, col, strokeW);
         dl->AddCircleFilled(b, strokeW * 0.5f, col, 16); // round joints: no gaps on the curves
      }
      // The start cap sits on the crossing where the stroke ends too, so one cap covers both.

      if (t > kLogoDrawEnd)
      {
         const float bt = t - kLogoDrawEnd;
         const float fadeIn = Smooth(bt * 3.0f);
         const float u = std::fmod(bt / 4.0f, 1.0f);
         const float e = u + 0.035f * std::sin(u * 3.14159265f * 4.0f); // quicker through the crossing
         const ImVec2 q = Lemniscate(twoPi * (e - std::floor(e)));
         const ImVec2 c(cx + q.x * A, cy + q.y * A);
         const float r = strokeW * 0.30f;
         dl->AddCircleFilled(c, r * 1.9f, IM_COL32(0xF9, 0xA5, 0x8F, (int)(40.0f * fadeIn * alpha)), 24);
         dl->AddCircleFilled(c, r, IM_COL32(0xC2, 0x59, 0x3F, (int)(255.0f * fadeIn * alpha)), 24);
         dl->AddCircleFilled(ImVec2(c.x - r * 0.18f, c.y - r * 0.2f), r * 0.78f,
                             IM_COL32(0xF9, 0xA5, 0x8F, (int)(255.0f * fadeIn * alpha)), 24);
         dl->AddCircleFilled(ImVec2(c.x - r * 0.34f, c.y - r * 0.38f), r * 0.28f,
                             IM_COL32(255, 255, 255, (int)(230.0f * fadeIn * alpha)), 16);
      }

      // --- credits: vertical marquee, clipped to a region, eased at both edges -----------
      {
         ImFont* font = ImGui::GetFont();
         const float fadeEdge = 42.0f * sc;
         const float cdAlpha = Smooth(creditsT * 1.5f) * alpha;
         dl->PushClipRect(ImVec2(0, regionTop), ImVec2(W, regionBot), true);
         float y = regionBot - scrolled;
         for (const CreditSection& sec : Credits())
         {
            auto edge = [&](float lineMid) {
               const float d = std::min(lineMid - regionTop, regionBot - lineMid);
               return Smooth(d / fadeEdge);
            };
            // heading: small, spaced, muted
            {
               const float fs = 12.0f * sc;
               std::string spaced;
               for (const char* p = sec.heading; *p; ++p)
               {
                  spaced += *p;
                  if (p[1]) spaced += ' ';
               }
               const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, spaced.c_str());
               const float e = edge(y + 9.0f * sc) * cdAlpha;
               dl->AddText(font, fs, ImVec2(cx - sz.x * 0.5f, y), IM_COL32(0x8F, 0x98, 0xB8, (int)(255.0f * e)), spaced.c_str());
            }
            y += 18.0f * sc + 10.0f * sc;
            for (const char* name : sec.names)
            {
               const float fs = 24.0f * sc;
               const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
               const float e = edge(y + 12.0f * sc) * cdAlpha;
               dl->AddText(font, fs, ImVec2(cx - sz.x * 0.5f, y), IM_COL32(0xE8, 0xEC, 0xF8, (int)(255.0f * e)), name);
               y += 32.0f * sc;
            }
            y += 46.0f * sc;
         }
         dl->PopClipRect();
      }

      // --- progress hairline + task label, bottom edge ----------------------------------
      if (st.tasksTotal > 0)
      {
         const float frac = (float)st.tasksDone / (float)st.tasksTotal;
         const float w = 160.0f * sc, x0 = cx - w * 0.5f, yy = H - 28.0f * sc;
         dl->AddLine(ImVec2(x0, yy), ImVec2(x0 + w, yy), IM_COL32(0x8F, 0x98, 0xB8, (int)(60.0f * alpha)), 1.0f);
         dl->AddLine(ImVec2(x0, yy), ImVec2(x0 + w * frac, yy), BrandColor(0.5f, alpha), 1.5f);
      }

      ImGui::End();
      ImGui::PopStyleVar(3);
      return true;
   }
} // namespace Splash

// CurveGraph: the one tone / transfer curve editor (2D View Curves, the Curves node, Mod Curve). Points are x, y
// pairs in 0..1, sorted by x. Same gestures everywhere: click empty space to add a point, drag to move, double-click,
// right-click or drag off the graph to remove, Delete removes the selected point, Alt-click resets to a straight line.
// Points never share an x (two points at one x would make the curve jump), and a readout line is always reserved
// under the graph so nothing below moves while you hover.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <imgui.h>
#include "app/AppShared.h"
#include "app/ui/design/Keymap.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/components/MenuParts.h"
#include "core/I18n.h"

namespace CurveGraph
{
   // Fills out[n] with the curve sampled at x = i / (n - 1).
   using Sampler = std::function<void(const std::vector<float>& pts, float* out, int n)>;

   struct Ghost
   {
      const std::vector<float>* pts = nullptr;
      ImU32 col = 0;
   };

   struct Opts
   {
      Sampler sample;                            // required: the model's own interpolation
      ImU32 col = 0;                             // 0: the text colour
      float width = 0.0f;                        // 0: the available width
      float height = 0.0f;                       // 0: square, capped at the 2D View's curve height
      const std::vector<float>* hist = nullptr;  // bins 0..1 drawn under the curve
      std::vector<Ghost> ghosts;                 // other channels, faint
      bool endsMoveX = true;                     // Photoshop: the end points slide along x too
      bool resetOnEmptyRightClick = false;       // Mod Curve: right-click on empty space resets
      bool crosshair = false;                    // a 0.5 / 0.5 cross (bipolar modulation)
      float liveX = -1.0f;                       // >= 0: a dot riding the curve at that input
      bool readout = true;                       // the In / Out line under the graph
      int maxPoints = 16;
      const char* undoLabel = "Curves";
   };

   namespace detail
   {
      struct State
      {
         ImGuiID id = 0;   // the graph being dragged
         int drag = -1;
         ImGuiID selId = 0;   // the graph whose point is selected
         int sel = -1;
      };
      inline State& St()
      {
         static State s;
         return s;
      }
      inline bool Straight(const std::vector<float>& c)
      {
         return c.size() == 4 && c[0] == 0.0f && c[1] == 0.0f && c[2] == 1.0f && c[3] == 1.0f;
      }
   }

   // Returns true when the points changed this frame (the caller marks its model dirty).
   inline bool Draw(const char* id, std::vector<float>& c, const Opts& o)
   {
      using detail::St;
      if (c.size() < 4 || c.size() % 2 != 0)
         c = { 0.0f, 0.0f, 1.0f, 1.0f };
      const float w = o.width > 0.0f ? o.width : ImGui::GetContentRegionAvail().x;
      const float h = o.height > 0.0f ? o.height : std::min(w, tok::view2d_curve_max);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      UiInteract::Hit(id, ImVec2(w, h));
      const ImGuiID gid = ImGui::GetItemID();
      const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
      const ImVec2 p1(p0.x + w, p0.y + h);
      // Values live in a box inset from the frame so points at 0 and 1 are not cut in half.
      const float pad = tok::space_1_5, iw = std::max(1.0f, w - 2.0f * pad), ih = std::max(1.0f, h - 2.0f * pad);
      const auto ToScreen = [&](float x, float y) { return ImVec2(p0.x + pad + x * iw, p0.y + pad + (1.0f - y) * ih); };
      const ImVec2 m = ImGui::GetIO().MousePos;
      const float mx = std::clamp((m.x - p0.x - pad) / iw, 0.0f, 1.0f);
      const float my = std::clamp(1.0f - (m.y - p0.y - pad) / ih, 0.0f, 1.0f);
      const float gap = tok::view2d_curve_editor_gap, minGap = tok::view2d_curve_editor_lo, off = tok::space_5;
      const bool outside = m.x < p0.x - off || m.x > p1.x + off || m.y < p0.y - off || m.y > p1.y + off;
      detail::State& s = St();
      int n = (int)c.size() / 2;
      int near = -1;
      float best = tok::space_2_5 * tok::space_2_5;
      for (int i = 0; i < n; i++)
      {
         const ImVec2 q = ToScreen(c[(size_t)i * 2], c[(size_t)i * 2 + 1]);
         const float d2 = (q.x - m.x) * (q.x - m.x) + (q.y - m.y) * (q.y - m.y);
         if (d2 < best)
         {
            best = d2;
            near = i;
         }
      }
      bool changed = false;
      const auto Remove = [&](int i) {
         if (i <= 0 || i >= n - 1)
            return;
         c.erase(c.begin() + (ptrdiff_t)i * 2, c.begin() + (ptrdiff_t)i * 2 + 2);
         n--;
         if (s.selId == gid)
            s.sel = -1;
         changed = true;
      };
      const bool mine = s.id == gid && s.drag >= 0 && s.drag < n;
      const bool pullOff = active && mine && s.drag > 0 && s.drag < n - 1 && outside;
      if (hovered && (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || MenuParts::RightPressed()))
      {
         if (near > 0 && near < n - 1)
         {
            app::PushUndoCheckpoint(o.undoLabel);
            Remove(near);
            s.drag = -1;
         }
         else if (near < 0 && o.resetOnEmptyRightClick && MenuParts::RightPressed() && !detail::Straight(c))
         {
            app::PushUndoCheckpoint(o.undoLabel);
            c = { 0.0f, 0.0f, 1.0f, 1.0f };
            n = 2;
            changed = true;
         }
      }
      else if (ImGui::IsItemActivated())
      {
         app::PushUndoCheckpoint(o.undoLabel);
         s.id = gid;
         s.drag = -1;
         if (ImGui::GetIO().KeyAlt)
         {
            if (!detail::Straight(c))
            {
               c = { 0.0f, 0.0f, 1.0f, 1.0f };
               n = 2;
               changed = true;
            }
         }
         else if (near >= 0)
            s.drag = near;
         else if (n < o.maxPoints)
         {
            int at = 1;
            while (at < n - 1 && c[(size_t)at * 2] < mx)
               at++;
            // Never on top of a neighbour's x: the new point goes where there is room, or not at all.
            const float lo = c[(size_t)(at - 1) * 2] + gap, hi = c[(size_t)at * 2] - gap;
            if (lo <= hi)
            {
               c.insert(c.begin() + (ptrdiff_t)at * 2, { std::clamp(mx, lo, hi), my });
               n++;
               s.drag = at;
               changed = true;
            }
         }
         s.sel = s.drag;
         s.selId = gid;
      }
      if (active && s.id == gid && s.drag >= 0 && s.drag < n && !pullOff)
      {
         const int i = s.drag, last = n - 1;
         float x = mx;
         if (i == 0)
            x = o.endsMoveX ? std::clamp(x, 0.0f, std::max(0.0f, c[2] - gap)) : 0.0f;
         else if (i == last)
            x = o.endsMoveX ? std::clamp(x, std::min(1.0f, c[(size_t)(last - 1) * 2] + gap), 1.0f) : 1.0f;
         else
         {
            const float lo = c[(size_t)(i - 1) * 2] + minGap, hi = c[(size_t)(i + 1) * 2] - minGap;
            x = lo <= hi ? std::clamp(x, lo, hi) : 0.5f * (c[(size_t)(i - 1) * 2] + c[(size_t)(i + 1) * 2]);
         }
         if (x != c[(size_t)i * 2] || my != c[(size_t)i * 2 + 1])
         {
            c[(size_t)i * 2] = x;
            c[(size_t)i * 2 + 1] = my;
            changed = true;
         }
      }
      else if (ImGui::IsItemDeactivated() && s.id == gid)
      {
         if (s.drag > 0 && s.drag < n - 1 && outside)
            Remove(s.drag);
         s.drag = -1;
      }
      if (hovered && s.selId == gid && s.sel > 0 && s.sel < n - 1 &&
          (Keymap::KeyPressed(Keymap::Act::Delete, true) || Keymap::KeyPressed(Keymap::Act::DeleteAlt, true)))
      {
         app::PushUndoCheckpoint(o.undoLabel);
         Remove(s.sel);
      }

      // Drawing.
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 col = o.col != 0 ? o.col : ImGui::GetColorU32(ImGuiCol_Text);
      dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), tok::radius_control);
      if (o.hist != nullptr && !o.hist->empty())
      {
         const int nb = (int)o.hist->size();
         const ImU32 hc = ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_histogram_under_color_alpha_mul);
         for (int i = 0; i < nb; i++)
         {
            const float x0 = p0.x + pad + iw * i / nb, x1 = p0.x + pad + iw * (i + 1) / nb;
            dl->AddRectFilled(ImVec2(x0, p1.y - pad - ih * std::clamp((*o.hist)[(size_t)i], 0.0f, 1.0f)), ImVec2(x1, p1.y - pad), hc);
         }
      }
      const ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_grid_col_color_alpha_mul);
      for (int i = 1; i < 4; i++)
      {
         const float f = i / 4.0f;
         dl->AddLine(ImVec2(p0.x + pad + iw * f, p0.y), ImVec2(p0.x + pad + iw * f, p1.y), gridCol);
         dl->AddLine(ImVec2(p0.x, p0.y + pad + ih * f), ImVec2(p1.x, p0.y + pad + ih * f), gridCol);
      }
      dl->AddLine(ToScreen(0.0f, 0.0f), ToScreen(1.0f, 1.0f), ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_curve_editor_color_alpha_mul_sm),
                  tok::view2d_curve_editor_stroke);
      if (o.crosshair)
      {
         const ImVec2 mid = ToScreen(0.5f, 0.5f);
         const ImU32 cc = ImGui::GetColorU32(ImGuiCol_Border, tok::view2d_curve_editor_color_alpha_mul_lg);
         dl->AddLine(ImVec2(mid.x, p0.y), ImVec2(mid.x, p1.y), cc);
         dl->AddLine(ImVec2(p0.x, mid.y), ImVec2(p1.x, mid.y), cc);
      }
      constexpr int kSamples = 128;
      float lut[kSamples];
      ImVec2 line[kSamples];
      for (const Ghost& g : o.ghosts)
      {
         if (g.pts == nullptr || detail::Straight(*g.pts) || !o.sample)
            continue;
         o.sample(*g.pts, lut, kSamples);
         for (int i = 0; i < kSamples; i++)
            line[i] = ToScreen(i / (float)(kSamples - 1), lut[i]);
         dl->AddPolyline(line, kSamples, g.col, 0, 1.0f);
      }
      if (o.sample)
      {
         o.sample(c, lut, kSamples);
         for (int i = 0; i < kSamples; i++)
            line[i] = ToScreen(i / (float)(kSamples - 1), std::clamp(lut[i], 0.0f, 1.0f));
         dl->AddPolyline(line, kSamples, col, 0, tok::view2d_line_stroke_md);
      }
      const bool dragging = active && s.id == gid && s.drag >= 0;
      for (int i = 0; i < n; i++)
      {
         const ImVec2 q = ToScreen(c[(size_t)i * 2], c[(size_t)i * 2 + 1]);
         const bool act = dragging && i == s.drag, sel = s.selId == gid && i == s.sel, hov = hovered && i == near;
         const float r = act || hov ? tok::view2d_r : tok::view2d_curve_editor_r;
         dl->AddCircleFilled(q, r, act && pullOff ? ImGui::GetColorU32(ImGuiCol_TextDisabled, tok::view2d_pt_col_color_alpha_mul) : col);
         if (sel || act)
            dl->AddCircle(q, r + tok::space_0_5, ImGui::GetColorU32(ImGuiCol_Text, tok::view2d_curve_editor_color_alpha_mul_lg), 0,
                          tok::view2d_ring_stroke_sm);
      }
      if (o.liveX >= 0.0f && o.sample)
      {
         const float lx = std::clamp(o.liveX, 0.0f, 1.0f);
         const ImVec2 dot = ToScreen(lx, std::clamp(lut[(int)std::lround(lx * (kSamples - 1))], 0.0f, 1.0f));
         dl->AddCircleFilled(dot, tok::view2d_r, ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
         dl->AddCircle(dot, tok::view2d_r, ImGui::GetColorU32(ImGuiCol_FrameBg), 0, tok::view2d_ring_stroke_sm);
      }
      dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), tok::radius_control);
      if (o.readout)
      {
         // Always one line tall, so hovering never moves what is under the graph.
         const int show = dragging && s.drag < n ? s.drag : hovered && near >= 0 ? near : s.selId == gid && s.sel >= 0 && s.sel < n ? s.sel : -1;
         if (pullOff)
            ImGui::TextDisabled("%s", I18n::T("Release to remove point"));
         else if (show >= 0)
            ImGui::TextDisabled(I18n::T("In %d   Out %d"), (int)std::lround(c[(size_t)show * 2] * 255.0f),
                                (int)std::lround(c[(size_t)show * 2 + 1] * 255.0f));
         else if (hovered)
            ImGui::TextDisabled("%s", I18n::T("Click to add, drag off or right-click to remove"));
         else
            ImGui::TextDisabled(" ");
      }
      return changed;
   }
}

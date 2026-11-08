// Curve Editor panel (docs/plans/curve-editor/README.md §8.2-§8.5). Docks on any side like the Terminal; one page per
// MSEG node, following the canvas selection. The node is the model: every edit goes through MSEGNode and
// core/CurveEdit.h, and is a patch undo step. The panel keeps only its view: page, zoom, tool and selection.
#include "app/panels/CurveEditorPanel.h"

#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "app/ui/design/components/TimeRuler.h"
#include "audio/MusicTime.h"
#include "core/CurveEdit.h"
#include "nodes/ModulatorNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace app
{
bool gCurveEditorOpen = false;
int gCurveEditorDock = 0;
float gCurveEditorHeight = 360.0f;
float gCurveEditorWidth = 560.0f;
bool gCurveEditorClaimedKeys = false;
ImVec2 gCurveEditorRectMin(0.0f, 0.0f), gCurveEditorRectMax(0.0f, 0.0f);

namespace
{
   constexpr float kMinHeight = 220.0f, kMinWidth = 360.0f;
   constexpr float kIconW = 30.0f, kPageW = 140.0f, kInspectorW = 268.0f, kRulerH = 16.0f;
   constexpr float kPad = 8.0f;     // graph edge to the 0 and 1 value lines
   constexpr float kHit = 7.0f;     // px a point grabs from
   constexpr float kMinSpan = 1.0f / 512.0f;

   enum class Tool
   {
      Select,
      Hand,
      Pen,
      Draw,
      Line,
      Bend,
      Eraser,
      Count
   };

   enum class Drag
   {
      None,
      Points,
      Marquee,
      Pan
   };

   // Grid steps for the time bases that have no MusicTime table: seconds and frames.
   constexpr double kSecondSteps[] = { 0.01, 0.05, 0.1, 0.25, 0.5, 1.0 };
   constexpr const char* kSecondNames[] = { "10 ms", "50 ms", "0.1 s", "0.25 s", "0.5 s", "1 s" };
   constexpr double kFrameSteps[] = { 1.0, 5.0, 10.0, 0.0 }; // 0 = one second of frames (fps)
   constexpr const char* kFrameNames[] = { "1 frame", "5 frames", "10 frames", "1 s" };

   struct State
   {
      uint64_t page = 0;            // the MSEG node on show
      uint64_t lastSelected = 0;    // the canvas selection last followed
      float v0 = 0.0f, v1 = 1.0f;   // the shape span on show
      Tool tool = Tool::Select;
      bool snap = true;
      int gridBeats = MusicTime::kSixteenth;
      int gridSeconds = 2, gridFrames = 0;
      std::vector<int> sel;         // selected point indices, ascending
      size_t selCount = 0;          // the node's point count the selection was made against
      Drag drag = Drag::None;
      std::vector<MSEGNode::Point> base; // the points as the drag found them
      int grab = -1;                // the point under the press
      ImVec2 from {};
      float panV0 = 0.0f;
   };

   State& S()
   {
      static State s;
      return s;
   }

   MSEGNode* AsMseg(GraphNode* gn) { return gn != nullptr ? dynamic_cast<MSEGNode*>(gn->node.get()) : nullptr; }

   std::vector<GraphNode*> Pages()
   {
      std::vector<GraphNode*> out;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<MSEGNode*>(gn.node.get()) != nullptr)
            out.push_back(&gn);
      return out;
   }

   GraphNode* PageNode() { return S().page != 0 ? FindNodeByUid(S().page) : nullptr; }

   std::string PageLabel(const GraphNode& gn) { return NodeTitleWithInstance(gn) + "##ce" + std::to_string(gn.uid); }

   void ClearSelection()
   {
      S().sel.clear();
      S().selCount = 0;
   }

   void FollowCanvasSelection()
   {
      State& s = S();
      uint64_t uid = 0;
      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<MSEGNode*>(gn.node.get()) != nullptr && ed::IsNodeSelected(gn.NodeId()))
         {
            uid = gn.uid;
            break;
         }
      ed::SetCurrentEditor(prevEditor);
      if (uid == s.lastSelected)
         return;
      s.lastSelected = uid;
      if (uid != 0 && uid != s.page)
      {
         s.page = uid;
         ClearSelection();
      }
   }

   void AddPage()
   {
      PushUndoCheckpoint("Add Curve");
      GraphNode* gn = SpawnNode("MSEG", "Modulators", gViewCenterCanvas.x, gViewCenterCanvas.y);
      if (gn == nullptr)
         return;
      S().page = gn->uid;
      S().v0 = 0.0f;
      S().v1 = 1.0f;
      ClearSelection();
   }

   ImU32 Tint(float a) { return ImGui::GetColorU32(ImGuiCol_Text, a); }
   ImU32 LineCol() { return IsThemeLight() ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_E6EBFAFF); }
   ImU32 SelCol() { return IsThemeLight() ? tok::U32(tok::pal::c_DC8214FF) : tok::U32(tok::pal::c_FFC83CFF); }
   ImU32 RingCol() { return IsThemeLight() ? tok::U32(tok::pal::c_F0F2F8FF) : tok::U32(tok::pal::c_12121AFF); }

   // ---- small faces, the Piano Roll's grammar

   bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip)
   {
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(id, ImVec2(kIconW, tok::tile));
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (ImGui::IsItemHovered())
         HelpTip("%s", tip);
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tok::icon_md,
                  on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text), glyphStr);
      return clicked;
   }

   bool PickerFace(const char* id, const char* value, float w, const char* tip, ImVec2& bmin, ImVec2& bmax)
   {
      const bool clicked = ActionButton::Draw(id, ImVec2(w, tok::tile));
      if (tip != nullptr && ImGui::IsItemHovered())
         HelpTip("%s", tip);
      bmin = ImGui::GetItemRectMin();
      bmax = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float cy = (bmin.y + bmax.y) * 0.5f;
      dl->PushClipRect(bmin, ImVec2(bmax.x - tok::space_2, bmax.y), true);
      dl->AddText(ImVec2(bmin.x + tok::space_3, cy - ImGui::GetFontSize() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), value);
      dl->PopClipRect();
      return clicked;
   }

   bool BeginPickerMenu(const char* id, ImVec2 bmin, ImVec2 bmax, float w)
   {
      ImGui::SetNextWindowPos(ImVec2(bmin.x, bmax.y + tok::space_1));
      ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(120.0f, w), 0.0f), ImVec2(280.0f, 10000.0f));
      return MenuParts::BeginPopup(id);
   }

   // A labelled dropdown row in the inspector. Returns true when the pick changed.
   bool PickRow(const char* label, const char* id, const std::vector<std::string>& items, int& cur)
   {
      FormParts::Label(label);
      const float w = FormParts::ControlW();
      ImVec2 bmin, bmax;
      const int shown = std::clamp(cur, 0, std::max(0, (int)items.size() - 1));
      if (PickerFace(id, items.empty() ? "" : T(items[shown].c_str()), w, nullptr, bmin, bmax))
         ImGui::OpenPopup(id);
      bool changed = false;
      if (BeginPickerMenu(id, bmin, bmax, w))
      {
         for (int i = 0; i < (int)items.size(); i++)
            if (MenuParts::Choice(T(items[i].c_str()), i == cur) && i != cur)
            {
               cur = i;
               changed = true;
            }
         MenuParts::EndPopup();
      }
      return changed;
   }

   // An inspector edit is one undo step: the checkpoint lands when the control is grabbed.
   void UndoOnGrab()
   {
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint("Edit Curve");
   }

   // ---- time

   // The shape span (0..1) one grid step covers, or 0 with snap off.
   double GridStep(const MSEGNode& n)
   {
      const State& s = S();
      if (n.rateMode == MSEGNode::kRateFrames)
      {
         const double frames = kFrameSteps[s.gridFrames] > 0.0 ? kFrameSteps[s.gridFrames] : std::max(1.0, (double)n.fps);
         return frames / std::max(1.0, (double)n.lengthFrames);
      }
      if (n.OnSeconds())
         return kSecondSteps[s.gridSeconds] * n.PassesPerSecond();
      return MusicTime::BeatsFor((MusicTime::RateDivision)s.gridBeats) / std::max(1e-6, n.BeatsPerPass());
   }

   float SnapX(const MSEGNode& n, float x)
   {
      const ImGuiIO& io = ImGui::GetIO();
      if (!S().snap || io.KeyAlt)
         return x;
      return CurveEdit::SnapX(x, (float)GridStep(n));
   }

   float SnapY(float y)
   {
      return ImGui::GetIO().KeyCtrl ? std::round(y * 8.0f) / 8.0f : y;
   }

   // ---- the graph

   struct Areas
   {
      float x0, x1, rulerY, y0, y1;
      float X(float u) const { return x0 + (u - S().v0) / (S().v1 - S().v0) * (x1 - x0); }
      float U(float x) const { return S().v0 + (x - x0) / std::max(1.0f, x1 - x0) * (S().v1 - S().v0); }
      float Y(float v) const { return y1 - kPad - v * (y1 - y0 - 2.0f * kPad); }
      float V(float y) const { return (y1 - kPad - y) / std::max(1.0f, y1 - y0 - 2.0f * kPad); }
   };

   void DrawRuler(ImDrawList* dl, const Areas& a, const MSEGNode& n)
   {
      const State& s = S();
      const float w = std::max(1.0f, a.x1 - a.x0);
      if (n.rateMode == MSEGNode::kRateFrames)
      {
         const double perPass = std::max(1.0, (double)n.lengthFrames) / std::clamp((double)n.speed, 0.25, 4.0);
         const TimeRuler::Axis axis { a.x0, a.x1, s.v0 * perPass, w / ((s.v1 - s.v0) * perPass), 4.0 };
         TimeRuler::Background(dl, axis, a.rulerY);
         TimeRuler::DrawUnits(dl, axis, a.rulerY, 1.0, 1.0, [](double f, double) { return std::to_string((long long)std::llround(f)); });
      }
      else if (n.OnSeconds())
      {
         const double perPass = 1.0 / std::max(1e-6, n.PassesPerSecond());
         const TimeRuler::Axis axis { a.x0, a.x1, s.v0 * perPass, w / ((s.v1 - s.v0) * perPass), 4.0 };
         TimeRuler::Background(dl, axis, a.rulerY);
         TimeRuler::DrawUnits(dl, axis, a.rulerY, 1.0, 0.001, [](double sec, double step) { return ArrangeFormatSeconds(sec, step < 1.0); });
      }
      else
      {
         const double perPass = n.BeatsPerPass();
         const TimeRuler::Axis axis { a.x0, a.x1, s.v0 * perPass, w / ((s.v1 - s.v0) * perPass), 4.0 };
         TimeRuler::Background(dl, axis, a.rulerY);
         TimeRuler::DrawBars(dl, axis, a.rulerY);
      }
   }

   void DrawGraph(ImDrawList* dl, const Areas& a, const MSEGNode& n, int hover)
   {
      const State& s = S();
      const ImVec2 mn(a.x0, a.y0), mx(a.x1, a.y1);
      dl->AddRectFilled(mn, mx, Tint(0.03f));
      dl->PushClipRect(mn, mx, true);
      // Outside the shape: shaded, the curve does not run there.
      if (s.v0 < 0.0f)
         dl->AddRectFilled(mn, ImVec2(a.X(0.0f), a.y1), Tint(0.04f));
      if (s.v1 > 1.0f)
         dl->AddRectFilled(ImVec2(a.X(1.0f), a.y0), mx, Tint(0.04f));
      // Grid: time steps (thinned until 6 px apart), value eighths.
      const double step = GridStep(n);
      if (step > 0.0)
      {
         double drawn = step;
         while ((a.X((float)drawn) - a.X(0.0f)) < 6.0f)
            drawn *= 2.0;
         const double first = std::ceil(std::max(0.0f, s.v0) / drawn) * drawn;
         for (double u = first; u <= std::min(1.0f, s.v1) + 1e-9; u += drawn)
            dl->AddLine(ImVec2(a.X((float)u), a.y0), ImVec2(a.X((float)u), a.y1), Tint(0.06f));
      }
      for (int i = 0; i <= 8; i++)
         dl->AddLine(ImVec2(a.x0, a.Y(i / 8.0f)), ImVec2(a.x1, a.Y(i / 8.0f)), Tint(i % 4 == 0 ? 0.12f : 0.05f));
      if (n.loop)
      {
         const float lo = std::clamp(std::min(n.loopStart, n.loopEnd), 0.0f, 1.0f);
         const float hi = std::clamp(std::max(n.loopStart, n.loopEnd), 0.0f, 1.0f);
         TimeRuler::LoopBand(dl, a.X(lo), a.X(hi), a.y0, a.y1, false, 0);
      }
      // The curve, one sample per two pixels.
      const int count = (int)n.points.size();
      const ImU32 line = LineCol();
      int hint = -1;
      ImVec2 prev {};
      bool have = false;
      for (float x = std::max(a.x0, a.X(0.0f)); x <= std::min(a.x1, a.X(1.0f)) + 0.5f; x += 2.0f)
      {
         const float u = std::clamp(a.U(x), 0.0f, 1.0f);
         const ImVec2 p(x, a.Y(ModShapes::MsegEvaluate(n.points.data(), count, u, hint)));
         if (have)
            dl->AddLine(prev, p, line, 1.8f);
         prev = p;
         have = true;
      }
      // Bezier handles of the selected points.
      for (int i : s.sel)
      {
         if (i < 0 || i >= count)
            continue;
         const MSEGNode::Point& p = n.points[i];
         const bool showL = i > 0 && n.points[i - 1].interp == ModShapes::kInterpBezier;
         const bool showR = i + 1 < count && p.interp == ModShapes::kInterpBezier;
         const ImVec2 c(a.X(p.x), a.Y(p.y));
         if (showL)
         {
            const ImVec2 h(a.X(p.x + p.lx), a.Y(p.y + p.ly));
            dl->AddLine(c, h, Tint(0.45f), 1.0f);
            dl->AddCircleFilled(h, 3.0f, Tint(0.75f));
         }
         if (showR)
         {
            const ImVec2 h(a.X(p.x + p.rx), a.Y(p.y + p.ry));
            dl->AddLine(c, h, Tint(0.45f), 1.0f);
            dl->AddCircleFilled(h, 3.0f, Tint(0.75f));
         }
      }
      // Points; selected ones in the live colour.
      const ImU32 selCol = SelCol(), ring = RingCol();
      for (int i = 0; i < count; i++)
      {
         const ImVec2 c(a.X(n.points[i].x), a.Y(n.points[i].y));
         const bool on = std::binary_search(s.sel.begin(), s.sel.end(), i);
         const float r = i == hover ? 6.0f : 4.5f;
         dl->AddCircleFilled(c, r, on ? selCol : line);
         dl->AddCircle(c, r, ring, 0, 1.5f);
      }
      // Where the node is reading now.
      const float px = n.Position();
      TimeRuler::Playhead(dl, a.X(px), a.y0, a.y1);
      if (s.drag == Drag::Marquee)
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const ImVec2 r0(std::min(m.x, s.from.x), std::min(m.y, s.from.y)), r1(std::max(m.x, s.from.x), std::max(m.y, s.from.y));
         dl->AddRectFilled(r0, r1, (selCol & 0x00FFFFFF) | 0x22000000);
         dl->AddRect(r0, r1, selCol);
      }
      dl->PopClipRect();
      dl->AddRect(mn, mx, Tint(0.10f));
   }

   int PointAt(const Areas& a, const MSEGNode& n, ImVec2 m)
   {
      int best = -1;
      float bestD = kHit;
      for (int i = 0; i < (int)n.points.size(); i++)
      {
         const float dx = a.X(n.points[i].x) - m.x, dy = a.Y(n.points[i].y) - m.y;
         const float d = std::sqrt(dx * dx + dy * dy);
         if (d < bestD)
         {
            bestD = d;
            best = i;
         }
      }
      return best;
   }

   void Zoom(const Areas& a, float factor, float atX)
   {
      State& s = S();
      const float u = a.U(atX);
      const float span = std::clamp((s.v1 - s.v0) * factor, kMinSpan, 1.5f);
      const float t = (atX - a.x0) / std::max(1.0f, a.x1 - a.x0);
      s.v0 = u - t * span;
      s.v1 = s.v0 + span;
      if (span >= 1.0f)
      {
         s.v0 = 0.5f - span * 0.5f;
         s.v1 = 0.5f + span * 0.5f;
      }
   }

   // Moves the selection by the grabbed point's snapped offset, from the drag's base. Points keep their order:
   // each moves after the neighbour it is heading towards, so it can never pass it.
   void MoveSelection(MSEGNode& n, const Areas& a)
   {
      State& s = S();
      n.points = s.base;
      if (s.grab < 0 || s.grab >= (int)s.base.size())
         return;
      const ImVec2 m = ImGui::GetIO().MousePos;
      const MSEGNode::Point& g = s.base[s.grab];
      const float gx = SnapX(n, std::clamp(a.U(m.x), 0.0f, 1.0f));
      const float gy = SnapY(std::clamp(a.V(m.y), 0.0f, 1.0f));
      const float dx = ImGui::GetIO().KeyShift && std::fabs(m.y - s.from.y) > std::fabs(m.x - s.from.x) ? 0.0f : gx - g.x;
      const float dy = gy - g.y;
      std::vector<int> order = s.sel;
      if (dx > 0.0f)
         std::reverse(order.begin(), order.end());
      for (int i : order)
         if (i >= 0 && i < (int)n.points.size())
            n.MovePoint(i, s.base[i].x + dx, s.base[i].y + dy);
   }

   void SelectInRect(const Areas& a, const MSEGNode& n, ImVec2 p0, ImVec2 p1, bool add)
   {
      State& s = S();
      const ImVec2 r0(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), r1(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
      if (!add)
         s.sel.clear();
      for (int i = 0; i < (int)n.points.size(); i++)
      {
         const float x = a.X(n.points[i].x), y = a.Y(n.points[i].y);
         if (x >= r0.x && x <= r1.x && y >= r0.y && y <= r1.y && !std::binary_search(s.sel.begin(), s.sel.end(), i))
            s.sel.insert(std::upper_bound(s.sel.begin(), s.sel.end(), i), i);
      }
      s.selCount = n.points.size();
   }

   void DeleteSelection(MSEGNode& n)
   {
      State& s = S();
      if (s.sel.empty())
         return;
      PushUndoCheckpoint("Delete Curve Points");
      for (auto it = s.sel.rbegin(); it != s.sel.rend(); ++it)
         n.RemovePoint(*it);
      ClearSelection();
   }

   // The segments the selection's points start (one point: its own; none: every segment).
   std::pair<int, int> SelectionSpan(const MSEGNode& n)
   {
      const State& s = S();
      if (s.sel.empty())
         return { 0, (int)n.points.size() - 1 };
      return { s.sel.front(), s.sel.back() };
   }

   void SetInterp(MSEGNode& n, int interp)
   {
      PushUndoCheckpoint("Curve Interpolation");
      const auto [first, last] = SelectionSpan(n);
      n.SetInterp(first, last, interp);
   }

   void SetHandle(MSEGNode& n, int handle)
   {
      PushUndoCheckpoint("Curve Handles");
      const auto [first, last] = SelectionSpan(n);
      n.SetHandleType(first, last, handle);
   }

   void HandleGraphInput(const Areas& a, MSEGNode& n, bool hovered, int hover)
   {
      State& s = S();
      ImGuiIO& io = ImGui::GetIO();
      const ImVec2 m = io.MousePos;
      if (hovered && io.MouseWheel != 0.0f)
      {
         if (io.KeyShift)
         {
            const float span = s.v1 - s.v0;
            s.v0 -= io.MouseWheel * span * 0.1f;
            s.v1 = s.v0 + span;
         }
         else
            Zoom(a, std::pow(0.85f, io.MouseWheel), m.x);
      }
      const bool panPress = hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                                        (s.tool == Tool::Hand && ImGui::IsMouseClicked(ImGuiMouseButton_Left)));
      if (panPress)
      {
         s.drag = Drag::Pan;
         s.from = m;
         s.panV0 = s.v0;
      }
      else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s.drag == Drag::None)
      {
         s.from = m;
         if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hover < 0)
         {
            PushUndoCheckpoint("Add Curve Point");
            const int at = n.AddPoint(SnapX(n, std::clamp(a.U(m.x), 0.0f, 1.0f)), SnapY(std::clamp(a.V(m.y), 0.0f, 1.0f)));
            s.sel.clear();
            if (at >= 0)
               s.sel.push_back(at);
            s.selCount = n.points.size();
         }
         else if (hover >= 0)
         {
            const bool on = std::binary_search(s.sel.begin(), s.sel.end(), hover);
            if (io.KeyShift && on)
               s.sel.erase(std::lower_bound(s.sel.begin(), s.sel.end(), hover));
            else if (!on)
            {
               if (!io.KeyShift)
                  s.sel.clear();
               s.sel.insert(std::upper_bound(s.sel.begin(), s.sel.end(), hover), hover);
            }
            s.selCount = n.points.size();
            if (std::binary_search(s.sel.begin(), s.sel.end(), hover))
            {
               PushUndoCheckpoint("Move Curve Points");
               s.drag = Drag::Points;
               s.grab = hover;
               s.base = n.points;
            }
         }
         else
            s.drag = Drag::Marquee;
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         if (hover >= 0 && !std::binary_search(s.sel.begin(), s.sel.end(), hover))
         {
            s.sel = { hover };
            s.selCount = n.points.size();
         }
         ImGui::OpenPopup("##curveMenu");
      }

      if (s.drag == Drag::Pan)
      {
         const float span = s.v1 - s.v0;
         s.v0 = s.panV0 - (m.x - s.from.x) / std::max(1.0f, a.x1 - a.x0) * span;
         s.v1 = s.v0 + span;
      }
      else if (s.drag == Drag::Points)
         MoveSelection(n, a);
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && s.drag != Drag::None)
      {
         if (s.drag == Drag::Marquee)
            SelectInRect(a, n, s.from, m, io.KeyShift);
         s.drag = Drag::None;
         s.grab = -1;
         s.base.clear();
      }
   }

   void DrawCurveMenu(MSEGNode& n)
   {
      if (!MenuParts::BeginPopup("##curveMenu"))
         return;
      static const char* kInterp[] = { "Constant", "Linear", "Bend", "Bezier" };
      static const char* kKeys[] = { "1", "2", "3", "4" };
      for (int i = 0; i < ModShapes::kInterpCount; i++)
         if (MenuParts::Item(T(kInterp[i]), kKeys[i]))
            SetInterp(n, i);
      MenuParts::Separator();
      if (MenuParts::SubMenu(T("Handles")))
      {
         static const char* kHandles[] = { "Auto Clamped", "Auto", "Vector", "Aligned", "Free" };
         static const char* kHKeys[] = { "Shift+1", "Shift+2", "Shift+3", "Shift+4", "Shift+5" };
         for (int i = 0; i < ModShapes::kHandleCount; i++)
            if (MenuParts::Item(T(kHandles[i]), kHKeys[i]))
               SetHandle(n, i);
         ImGui::EndMenu();
      }
      MenuParts::Separator();
      if (MenuParts::Item(T("Delete"), "Del", false, !S().sel.empty()))
         DeleteSelection(n);
      MenuParts::EndPopup();
   }

   void HandleKeys(MSEGNode& n)
   {
      const ImGuiIO& io = ImGui::GetIO();
      State& s = S();
      if (!gCurveEditorClaimedKeys || io.WantTextInput || s.drag != Drag::None)
         return;
      const bool plain = !io.KeyCtrl && !io.KeySuper && !io.KeyAlt;
      const bool cmd = io.KeySuper || io.KeyCtrl;
      if (plain && !io.KeyShift)
      {
         if (ImGui::IsKeyPressed(ImGuiKey_A, false)) s.tool = Tool::Select;
         if (ImGui::IsKeyPressed(ImGuiKey_H, false)) s.tool = Tool::Hand;
         if (ImGui::IsKeyPressed(ImGuiKey_G, false)) s.snap = !s.snap;
         if (ImGui::IsKeyPressed(ImGuiKey_F, false)) s.v0 = 0.0f, s.v1 = 1.0f;
      }
      if (plain && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
         DeleteSelection(n);
      static const ImGuiKey kNum[] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5 };
      for (int i = 0; i < 5 && plain; i++)
         if (ImGui::IsKeyPressed(kNum[i], false))
         {
            if (io.KeyShift)
               SetHandle(n, i);
            else if (i < ModShapes::kInterpCount)
               SetInterp(n, i);
         }
      if (cmd && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false))
      {
         s.sel.clear();
         for (int i = 0; i < (int)n.points.size(); i++)
            s.sel.push_back(i);
         s.selCount = n.points.size();
      }
      if (plain && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
         ClearSelection();
   }

   // ---- columns

   const char* ToolGlyph(Tool t)
   {
      switch (t)
      {
      case Tool::Hand: return IconsInfinite::Hand;
      case Tool::Pen: return IconsInfinite::PenTool;
      case Tool::Draw: return IconsInfinite::Pencil;
      case Tool::Line: return IconsInfinite::LineTool;
      case Tool::Bend: return IconsInfinite::Bend;
      case Tool::Eraser: return IconsInfinite::Erase;
      default: return IconsInfinite::Pointer;
      }
   }

   const char* ToolTip(Tool t)
   {
      switch (t)
      {
      case Tool::Hand: return T("Hand (H)");
      case Tool::Pen: return T("Pen (P)");
      case Tool::Draw: return T("Draw (D)");
      case Tool::Line: return T("Line (L)");
      case Tool::Bend: return T("Bend (B)");
      case Tool::Eraser: return T("Eraser (E)");
      default: return T("Select (A)");
      }
   }

   void DrawToolButtons(bool row)
   {
      State& s = S();
      for (int i = 0; i < (int)Tool::Count; i++)
      {
         const Tool t = (Tool)i;
         if (i > 0 && row)
            FormParts::Inline(0.0f, tok::space_1);
         const std::string id = "##ceTool" + std::to_string(i);
         if (IconChip(id.c_str(), ToolGlyph(t), s.tool == t, ToolTip(t)))
            s.tool = t;
      }
      if (row)
         FormParts::Inline(0.0f, tok::space_3);
      else
         ImGui::Dummy(ImVec2(0.0f, tok::space_2));
      if (IconChip("##ceSnap", s.snap ? IconsInfinite::NoteSnapFill : IconsInfinite::NoteSnap, s.snap, T("Snap to grid (G)")))
         s.snap = !s.snap;
   }

   void DrawPagePicker()
   {
      State& s = S();
      GraphNode* gn = PageNode();
      ImVec2 bmin, bmax;
      const std::string face = gn != nullptr ? NodeTitleWithInstance(*gn) : std::string(T("No curve"));
      if (PickerFace("##cePage", face.c_str(), kPageW, T("Curves: every MSEG node in the patch"), bmin, bmax))
         ImGui::OpenPopup("##cePageMenu");
      if (BeginPickerMenu("##cePageMenu", bmin, bmax, kPageW))
      {
         for (GraphNode* p : Pages())
            if (MenuParts::Choice(PageLabel(*p).c_str(), p->uid == s.page) && p->uid != s.page)
            {
               s.page = p->uid;
               ClearSelection();
            }
         MenuParts::Separator();
         if (MenuParts::Item(T("New Curve")))
            AddPage();
         MenuParts::EndPopup();
      }
      FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##ceAdd", IconsInfinite::Plus, false, T("New curve: an MSEG node on the canvas")))
         AddPage();
   }

   void DrawInspector(MSEGNode& n)
   {
      State& s = S();
      FormParts::Narrow narrow(84.0f);
      SectionCard::BeginWindow();

      FormParts::Section(T("Time"));
      int mode = n.rateMode;
      if (PickRow(T("Time"), "##ceTimeBase", MSEGNode::RateModeNames(), mode))
      {
         PushUndoCheckpoint("Curve Time Base");
         n.rateMode = mode;
      }
      switch (n.rateMode)
      {
      case LFONode::kRateBeats:
         FormParts::SliderFloat(T("Length"), &n.rateBeats, 0.0625f, 256.0f, "%.3g beats", ImGuiSliderFlags_Logarithmic);
         UndoOnGrab();
         break;
      case LFONode::kRateSynced:
      {
         int div = MusicTime::NearestRateDivision(n.rateBeats);
         if (PickRow(T("Length"), "##ceDivision", MusicTime::RateDivisionList(), div))
         {
            PushUndoCheckpoint("Curve Length");
            n.rateBeats = (float)MusicTime::BeatsFor((MusicTime::RateDivision)div);
         }
         break;
      }
      case LFONode::kRateHz:
         FormParts::SliderFloat(T("Rate"), &n.rateHz, 0.01f, 40.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic);
         UndoOnGrab();
         break;
      case MSEGNode::kRateSeconds:
         FormParts::SliderFloat(T("Length"), &n.lengthSec, 0.01f, 600.0f, "%.2f s", ImGuiSliderFlags_Logarithmic);
         UndoOnGrab();
         break;
      default:
         FormParts::SliderFloat(T("Length"), &n.lengthFrames, 1.0f, 100000.0f, "%.0f frames", ImGuiSliderFlags_Logarithmic);
         UndoOnGrab();
         n.lengthFrames = std::max(1.0f, std::round(n.lengthFrames));
         FormParts::SliderFloat(T("FPS"), &n.fps, 1.0f, 240.0f, "%.3g");
         UndoOnGrab();
         break;
      }
      FormParts::SliderFloat(T("Speed"), &n.speed, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
      UndoOnGrab();
      static const std::vector<std::string> kPlay = { "Repeat", "Once", "Ping-pong" };
      int play = n.play;
      if (PickRow(T("Play"), "##cePlay", kPlay, play))
      {
         PushUndoCheckpoint("Curve Play");
         n.play = play;
      }
      bool freeRun = n.freeRun;
      if (FormParts::Checkbox(T("Free run"), &freeRun))
      {
         PushUndoCheckpoint("Curve Free Run");
         n.freeRun = freeRun;
      }

      FormParts::Section(T("Loop"));
      bool loop = n.loop;
      if (FormParts::Checkbox(T("Loop"), &loop))
      {
         PushUndoCheckpoint("Curve Loop");
         n.loop = loop;
      }
      ImGui::BeginDisabled(!n.loop);
      FormParts::SliderFloat(T("Start"), &n.loopStart, 0.0f, 1.0f, "%.3f");
      UndoOnGrab();
      FormParts::SliderFloat(T("End"), &n.loopEnd, 0.0f, 1.0f, "%.3f");
      UndoOnGrab();
      FormParts::Label(T("Keep"));
      if (ActionButton::Draw(T("Crop to Loop"), ImVec2(FormParts::ControlW(), 0.0f)))
      {
         const float a = std::clamp(std::min(n.loopStart, n.loopEnd), 0.0f, 1.0f);
         const float b = std::clamp(std::max(n.loopStart, n.loopEnd), 0.0f, 1.0f);
         PushUndoCheckpoint("Crop Curve");
         if (CurveEdit::Crop(n.points, a, b))
         {
            const float kept = b - a;
            switch (n.rateMode)
            {
            case LFONode::kRateSynced: n.rateMode = LFONode::kRateBeats; [[fallthrough]];
            case LFONode::kRateBeats: n.rateBeats *= kept; break;
            case LFONode::kRateHz: n.rateHz /= kept; break;
            case MSEGNode::kRateSeconds: n.lengthSec *= kept; break;
            default: n.lengthFrames = std::max(1.0f, std::round(n.lengthFrames * kept)); break;
            }
            n.loop = false;
            n.loopStart = 0.0f;
            n.loopEnd = 1.0f;
            n.Normalize();
            ClearSelection();
         }
      }
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Keep only the looped part; the length shrinks to it"));
      ImGui::EndDisabled();

      FormParts::Section(T("Grid"));
      bool snap = s.snap;
      if (FormParts::Checkbox(T("Snap"), &snap))
         s.snap = snap;
      if (n.rateMode == MSEGNode::kRateFrames)
      {
         static const std::vector<std::string> names(std::begin(kFrameNames), std::end(kFrameNames));
         PickRow(T("Grid"), "##ceGridF", names, s.gridFrames);
      }
      else if (n.OnSeconds())
      {
         static const std::vector<std::string> names(std::begin(kSecondNames), std::end(kSecondNames));
         PickRow(T("Grid"), "##ceGridS", names, s.gridSeconds);
      }
      else
         PickRow(T("Grid"), "##ceGridB", MusicTime::RateDivisionList(), s.gridBeats);

      if (!s.sel.empty())
      {
         FormParts::Section(s.sel.size() == 1 ? T("Point") : T("Points"));
         static const std::vector<std::string> kInterp = { "Constant", "Linear", "Bend", "Bezier" };
         static const std::vector<std::string> kHandles = { "Auto Clamped", "Auto", "Vector", "Aligned", "Free" };
         const int first = s.sel.front();
         int interp = first < (int)n.points.size() ? n.points[first].interp : 0;
         if (PickRow(T("Curve"), "##ceInterp", kInterp, interp))
            SetInterp(n, interp);
         int handle = first < (int)n.points.size() ? n.points[first].handle : 0;
         if (PickRow(T("Handles"), "##ceHandle", kHandles, handle))
            SetHandle(n, handle);
         if (s.sel.size() == 1 && first < (int)n.points.size())
         {
            MSEGNode::Point p = n.points[first];
            float x = p.x, y = p.y;
            ImGui::BeginDisabled(first == 0 || first == (int)n.points.size() - 1);
            if (FormParts::SliderFloat(T("Time"), &x, 0.0f, 1.0f, "%.4f"))
               n.MovePoint(first, x, p.y);
            UndoOnGrab();
            ImGui::EndDisabled();
            if (FormParts::SliderFloat(T("Value"), &y, 0.0f, 1.0f, "%.4f"))
               n.MovePoint(first, n.points[first].x, y);
            UndoOnGrab();
            if (p.interp == ModShapes::kInterpBend && first + 1 < (int)n.points.size())
            {
               FormParts::SliderFloat(T("Bend"), &n.points[first].curve, -1.0f, 1.0f, "%.2f");
               UndoOnGrab();
            }
         }
      }

      FormParts::Section(T("Voices"));
      bool voice = n.voice;
      if (FormParts::Checkbox(T("Per voice"), &voice))
      {
         PushUndoCheckpoint("Curve Per Voice");
         n.voice = voice;
      }
      SectionCard::EndWindow();
   }

   void DrawDockMenu()
   {
      const ImVec2 mn = ImGui::GetWindowPos(), mx(mn.x + ImGui::GetWindowSize().x, mn.y + tok::tile + tok::space_3);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(mn, mx) && !ImGui::IsAnyItemHovered() &&
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
         ImGui::OpenPopup("##curveDockMenu");
      if (MenuParts::BeginPopup("##curveDockMenu"))
      {
         if (MenuParts::SubMenu(T("Dock Position")))
         {
            static const char* kDocks[] = { "Bottom", "Right", "Left", "Top" };
            for (int i = 0; i < 4; i++)
               if (MenuParts::Item(T(kDocks[i]), nullptr, gCurveEditorDock == i))
                  gCurveEditorDock = i;
            ImGui::EndMenu();
         }
         ImGui::Dummy(ImVec2(0.0f, tok::space_1));
         if (MenuParts::Item(T("Close Curve Editor")))
            gCurveEditorOpen = false;
         MenuParts::EndPopup();
      }
   }

   void DrawGraphArea(MSEGNode& n, ImVec2 size)
   {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##ceGraph", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
      const bool hovered = ImGui::IsItemHovered();
      Areas a { at.x, at.x + size.x, at.y, at.y + kRulerH, at.y + size.y };
      if (S().selCount != n.points.size())
         ClearSelection(); // the points changed elsewhere (card, undo, agent): indices no longer hold
      const int hover = hovered ? PointAt(a, n, ImGui::GetIO().MousePos) : -1;
      HandleGraphInput(a, n, hovered, hover);
      if (S().tool == Tool::Hand || S().drag == Drag::Pan)
         if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      DrawCurveMenu(n);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      DrawRuler(dl, a, n);
      DrawGraph(dl, a, n, hover);
   }

   void DrawPanel()
   {
      gCurveEditorClaimedKeys = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
      FollowCanvasSelection();
      DrawDockMenu();
      State& s = S();
      MSEGNode* n = AsMseg(PageNode());
      if (n == nullptr)
      {
         const std::vector<GraphNode*> pages = Pages();
         s.page = pages.empty() ? 0 : pages.front()->uid;
         n = AsMseg(PageNode());
         ClearSelection();
      }

      const bool side = gCurveEditorDock == 1 || gCurveEditorDock == 2;
      DrawPagePicker();
      if (side && n != nullptr)
      {
         FormParts::Inline(0.0f, tok::space_3);
         DrawToolButtons(true);
      }
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      if (n == nullptr)
      {
         const ImVec2 mn = ImGui::GetCursorScreenPos();
         EmptyState::Draw(mn, ImVec2(mn.x + avail.x, mn.y + avail.y), T("No curve yet"), T("+ makes an MSEG node and opens it here"));
         return;
      }
      HandleKeys(*n);
      const float gap = tok::space_2;
      if (side)
      {
         // Three rows: tools (above), graph, inspector folded under it.
         const float graphH = std::max(140.0f, avail.y * 0.5f);
         DrawGraphArea(*n, ImVec2(avail.x, graphH));
         ImGui::Dummy(ImVec2(0.0f, gap * 0.5f));
         ImGui::BeginChild("##ceInspector", ImVec2(avail.x, std::max(1.0f, ImGui::GetContentRegionAvail().y)), false);
         DrawInspector(*n);
         ImGui::EndChild();
         return;
      }
      // Three columns: tool rail on the canvas side... here the left, inspector on the outer edge, the right.
      ImGui::BeginChild("##ceRail", ImVec2(kIconW, avail.y), false, ImGuiWindowFlags_NoScrollbar);
      DrawToolButtons(false);
      ImGui::EndChild();
      FormParts::Inline(0.0f, gap);
      const float graphW = std::max(80.0f, avail.x - kIconW - kInspectorW - 2.0f * gap);
      DrawGraphArea(*n, ImVec2(graphW, avail.y));
      FormParts::Inline(0.0f, gap);
      ImGui::BeginChild("##ceInspector", ImVec2(kInspectorW, avail.y), false);
      DrawInspector(*n);
      ImGui::EndChild();
   }
}

void CurveEditorOpenNode(uint64_t uid)
{
   gCurveEditorOpen = true;
   if (uid != S().page)
   {
      S().page = uid;
      ClearSelection();
   }
}

void DrawCurveEditorDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const int dock = gCurveEditorDock;
   const bool vertical = dock == 1 || dock == 2;  // the grip is a column
   const bool gripFirst = dock == 0 || dock == 1; // the canvas is above or to the left
   ImGui::BeginChild(id, size, false);
   gCurveEditorRectMin = ImGui::GetWindowPos();
   gCurveEditorRectMax = ImVec2(gCurveEditorRectMin.x + ImGui::GetWindowSize().x, gCurveEditorRectMin.y + ImGui::GetWindowSize().y);
   gCurveEditorClaimedKeys = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##curvegrip", vertical ? ImVec2(kGrip, std::max(1.0f, inner.y)) : ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(vertical, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         const ImVec2 d = ImGui::GetIO().MouseDelta;
         switch (dock)
         {
         case 0: gCurveEditorHeight -= d.y; break;
         case 1: gCurveEditorWidth -= d.x; break;
         case 2: gCurveEditorWidth += d.x; break;
         default: gCurveEditorHeight += d.y; break;
         }
         gCurveEditorHeight = std::max(kMinHeight, gCurveEditorHeight);
         gCurveEditorWidth = std::max(kMinWidth, gCurveEditorWidth);
      }
   };
   if (gripFirst)
   {
      grip();
      if (vertical)
         FormParts::Inline();
   }
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize = vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), std::max(1.0f, inner.y))
                                    : ImVec2(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
   PanelFrame::Insets in;
   switch (dock)
   {
   case 0: in.t = 0.0f; break;
   case 1: in.l = 0.0f; break;
   case 2: in.r = 0.0f; break;
   default: in.b = 0.0f; break;
   }
   PanelFrame::BeginCard("##curvecontent", cardSize, in);
   DrawPanel();
   PanelFrame::EndCard();
   if (!gripFirst)
   {
      if (vertical)
         FormParts::Inline();
      grip();
   }
   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}
}

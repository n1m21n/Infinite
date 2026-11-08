// Curve Editor panel (docs/plans/curve-editor/README.md §8.2-§8.7). Docks on any side like the Terminal; one page per
// MSEG node, following the canvas selection. The node is the model: every edit goes through MSEGNode and
// core/CurveEdit.h, and is a patch undo step. The panel keeps only its view: pages, zoom, tool and selection.
#include "app/panels/CurveEditorPanel.h"

#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/EmptyState.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/components/PanelFrame.h"
#include "app/ui/design/components/SectionCard.h"
#include "app/ui/design/components/TimeRuler.h"
#include "app/ui/design/components/ToolCursor.h"
#include "audio/MusicTime.h"
#include "core/CurveEdit.h"
#include "core/Modulation.h"
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
uint64_t gCurveAssignUid = 0;

namespace
{
   constexpr float kMinHeight = 220.0f, kMinWidth = 360.0f;
   constexpr float kIconW = 30.0f, kPageW = 140.0f, kInspectorW = 268.0f, kRulerH = 16.0f;
   constexpr float kPad = 8.0f;     // graph edge to the 0 and 1 value lines
   constexpr float kHit = 7.0f;     // px a point or handle grabs from
   constexpr float kEraseR = 9.0f;  // px the eraser rubs out
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
      Pan,
      Handle,
      Stroke,
      Line,
      Bend,
      Erase
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
      std::vector<uint64_t> closed; // pages hidden from the strip (the nodes stay)
      bool ghost = false;           // other pages drawn faint behind this one
      float v0 = 0.0f, v1 = 1.0f;   // the shape span on show
      Tool tool = Tool::Select;
      bool snap = true;
      int gridBeats = MusicTime::kSixteenth;
      int gridSeconds = 2, gridFrames = 0;
      float thinning = 0.01f;       // Draw: how far (shape units) the points may stray from the stroke
      std::vector<int> sel;         // selected point indices, ascending
      size_t selCount = 0;          // the node's point count the selection was made against
      Drag drag = Drag::None;
      std::vector<MSEGNode::Point> base; // the points as the drag found them
      int grab = -1;                // the point (or segment, for Bend) under the press
      int handleSide = 0;           // Handle drag: -1 left, +1 right; 0 = both, mirrored (the Pen's pull)
      ImVec2 from {};
      float panV0 = 0.0f;
      std::vector<CurveEdit::XY> stroke;
      CurveEdit::XY lineFrom {};
      float bendBase = 0.0f, bendSign = 1.0f;
   };

   State& S()
   {
      static State s;
      return s;
   }

   MSEGNode* AsMseg(GraphNode* gn) { return gn != nullptr ? dynamic_cast<MSEGNode*>(gn->node.get()) : nullptr; }

   bool IsClosed(uint64_t uid) { return std::find(S().closed.begin(), S().closed.end(), uid) != S().closed.end(); }

   void Reopen(uint64_t uid) { S().closed.erase(std::remove(S().closed.begin(), S().closed.end(), uid), S().closed.end()); }

   std::vector<GraphNode*> Pages(bool includeClosed = false)
   {
      std::vector<GraphNode*> out;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<MSEGNode*>(gn.node.get()) != nullptr && (includeClosed || !IsClosed(gn.uid)))
            out.push_back(&gn);
      return out;
   }

   GraphNode* PageNode() { return S().page != 0 ? FindNodeByUid(S().page) : nullptr; }

   void ClearSelection()
   {
      S().sel.clear();
      S().selCount = 0;
   }

   void ShowPage(uint64_t uid)
   {
      State& s = S();
      Reopen(uid);
      if (uid != s.page)
      {
         s.page = uid;
         ClearSelection();
      }
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
      if (uid != 0)
         ShowPage(uid); // selecting a closed page on the canvas reopens it
   }

   void AddPage()
   {
      PushUndoCheckpoint("Add Curve");
      GraphNode* gn = SpawnNode("MSEG", "Modulators", gViewCenterCanvas.x, gViewCenterCanvas.y);
      if (gn == nullptr)
         return;
      ShowPage(gn->uid);
      S().v0 = 0.0f;
      S().v1 = 1.0f;
   }

   void DeletePage(uint64_t uid)
   {
      GraphNode* gn = FindNodeByUid(uid);
      if (gn == nullptr)
         return;
      PushUndoCheckpoint("Delete Curve");
      if (gCurveAssignUid == uid)
         gCurveAssignUid = 0;
      RemoveNodeByIndex(gn->index);
      if (S().page == uid)
      {
         S().page = 0;
         ClearSelection();
      }
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

   // A dropdown face of width w and its menu. Returns true when the pick changed.
   bool Picker(const char* id, const std::vector<std::string>& items, int& cur, float w, const char* tip = nullptr)
   {
      ImVec2 bmin, bmax;
      const int shown = std::clamp(cur, 0, std::max(0, (int)items.size() - 1));
      if (PickerFace(id, items.empty() ? "" : T(items[shown].c_str()), w, tip, bmin, bmax))
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

   // A labelled dropdown row in the inspector.
   bool PickRow(const char* label, const char* id, const std::vector<std::string>& items, int& cur)
   {
      FormParts::Label(label);
      return Picker(id, items, cur, FormParts::ControlW());
   }

   // An inspector edit is one undo step: the checkpoint lands when the control is grabbed.
   void UndoOnGrab()
   {
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint("Edit Curve");
   }

   // ---- time

   // The shape span (0..1) one grid step covers.
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

   // Alt holds snap off for one gesture.
   float SnapX(const MSEGNode& n, float x)
   {
      if (!S().snap || ImGui::GetIO().KeyAlt)
         return x;
      return CurveEdit::SnapX(x, (float)GridStep(n));
   }

   float SnapY(float y) { return ImGui::GetIO().KeyCtrl ? std::round(y * 8.0f) / 8.0f : y; }

   const std::vector<std::string>& GridNames(const MSEGNode& n)
   {
      static const std::vector<std::string> frames(std::begin(kFrameNames), std::end(kFrameNames));
      static const std::vector<std::string> seconds(std::begin(kSecondNames), std::end(kSecondNames));
      if (n.rateMode == MSEGNode::kRateFrames)
         return frames;
      if (n.OnSeconds())
         return seconds;
      return MusicTime::RateDivisionList();
   }

   int& GridIndex(const MSEGNode& n)
   {
      State& s = S();
      if (n.rateMode == MSEGNode::kRateFrames)
         return s.gridFrames;
      if (n.OnSeconds())
         return s.gridSeconds;
      return s.gridBeats;
   }

   // ---- the graph

   struct Areas
   {
      float x0, x1, rulerY, y0, y1;
      float X(float u) const { return x0 + (u - S().v0) / (S().v1 - S().v0) * (x1 - x0); }
      float U(float x) const { return S().v0 + (x - x0) / std::max(1.0f, x1 - x0) * (S().v1 - S().v0); }
      float Y(float v) const { return y1 - kPad - v * (y1 - y0 - 2.0f * kPad); }
      float V(float y) const { return (y1 - kPad - y) / std::max(1.0f, y1 - y0 - 2.0f * kPad); }
      ImVec2 P(float u, float v) const { return ImVec2(X(u), Y(v)); }
   };

   CurveEdit::XY MouseXY(const Areas& a, bool snap, const MSEGNode& n)
   {
      const ImVec2 m = ImGui::GetIO().MousePos;
      float x = std::clamp(a.U(m.x), 0.0f, 1.0f), y = std::clamp(a.V(m.y), 0.0f, 1.0f);
      if (snap)
      {
         x = SnapX(n, x);
         y = SnapY(y);
      }
      return { x, y };
   }

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

   // The curve between u0 and u1, one sample per two pixels.
   void DrawCurve(ImDrawList* dl, const Areas& a, const MSEGNode& n, ImU32 col, float thick, float u0 = 0.0f, float u1 = 1.0f)
   {
      const int count = (int)n.points.size();
      if (count < 2)
         return;
      int hint = -1;
      ImVec2 prev {};
      bool have = false;
      const float xa = std::max(a.x0, a.X(u0)), xb = std::min(a.x1, a.X(u1));
      for (float x = xa; x <= xb + 1.99f; x += 2.0f)
      {
         const float u = std::clamp(a.U(std::min(x, xb)), u0, u1);
         const ImVec2 p(std::min(x, xb), a.Y(ModShapes::MsegEvaluate(n.points.data(), count, u, hint)));
         if (have)
            dl->AddLine(prev, p, col, thick);
         prev = p;
         have = true;
      }
   }

   // Which sides of point i show a bezier handle.
   bool ShowsLeft(const MSEGNode& n, int i) { return i > 0 && n.points[i - 1].interp == ModShapes::kInterpBezier; }
   bool ShowsRight(const MSEGNode& n, int i) { return i + 1 < (int)n.points.size() && n.points[i].interp == ModShapes::kInterpBezier; }

   void DrawGraph(ImDrawList* dl, const Areas& a, const MSEGNode& n, int hover)
   {
      const State& s = S();
      const ImVec2 mn(a.x0, a.y0), mx(a.x1, a.y1), m = ImGui::GetIO().MousePos;
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
      if (s.ghost)
         for (GraphNode* g : Pages())
            if (g->uid != s.page)
               if (const MSEGNode* other = AsMseg(g))
                  DrawCurve(dl, a, *other, Tint(0.16f), 1.2f);
      const int count = (int)n.points.size();
      const ImU32 selCol = SelCol(), ring = RingCol(), line = LineCol();
      // Bend: the segment under the pointer, thicker.
      if (s.tool == Tool::Bend && count >= 2 && (s.drag == Drag::Bend || (s.drag == Drag::None && ImGui::IsMouseHoveringRect(mn, mx))))
      {
         const int seg = s.drag == Drag::Bend ? s.grab : ModShapes::MsegFindSegment(n.points.data(), count, std::clamp(a.U(m.x), 0.0f, 1.0f), -1);
         if (seg >= 0 && seg + 1 < count)
            DrawCurve(dl, a, n, (selCol & 0x00FFFFFF) | 0x80000000, 5.0f, n.points[seg].x, n.points[seg + 1].x);
      }
      DrawCurve(dl, a, n, line, 1.8f);
      // Bezier handles of the selected points.
      for (int i : s.sel)
      {
         if (i < 0 || i >= count)
            continue;
         const MSEGNode::Point& p = n.points[i];
         const ImVec2 c = a.P(p.x, p.y);
         if (ShowsLeft(n, i))
         {
            const ImVec2 h = a.P(p.x + p.lx, p.y + p.ly);
            dl->AddLine(c, h, Tint(0.45f), 1.0f);
            dl->AddCircleFilled(h, 3.5f, Tint(0.8f));
         }
         if (ShowsRight(n, i))
         {
            const ImVec2 h = a.P(p.x + p.rx, p.y + p.ry);
            dl->AddLine(c, h, Tint(0.45f), 1.0f);
            dl->AddCircleFilled(h, 3.5f, Tint(0.8f));
         }
      }
      // Points; selected ones in the live colour.
      for (int i = 0; i < count; i++)
      {
         const ImVec2 c = a.P(n.points[i].x, n.points[i].y);
         const bool on = std::binary_search(s.sel.begin(), s.sel.end(), i);
         const float r = i == hover ? 6.0f : 4.5f;
         dl->AddCircleFilled(c, r, on ? selCol : line);
         dl->AddCircle(c, r, ring, 0, 1.5f);
      }
      // Gestures in progress.
      if (s.drag == Drag::Stroke)
         for (size_t i = 1; i < s.stroke.size(); i++)
            dl->AddLine(a.P(s.stroke[i - 1].x, s.stroke[i - 1].y), a.P(s.stroke[i].x, s.stroke[i].y), selCol, 2.0f);
      if (s.drag == Drag::Line)
      {
         const CurveEdit::XY to = MouseXY(a, true, n);
         dl->AddLine(a.P(s.lineFrom.x, s.lineFrom.y), a.P(to.x, to.y), selCol, 2.0f);
      }
      if (s.tool == Tool::Eraser && ImGui::IsMouseHoveringRect(mn, mx))
         dl->AddCircle(m, kEraseR, selCol, 0, 1.5f);
      // Where the node is reading now.
      TimeRuler::Playhead(dl, a.X(n.Position()), a.y0, a.y1);
      if (s.drag == Drag::Marquee)
      {
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
         const ImVec2 c = a.P(n.points[i].x, n.points[i].y);
         const float d = std::hypot(c.x - m.x, c.y - m.y);
         if (d < bestD)
         {
            bestD = d;
            best = i;
         }
      }
      return best;
   }

   // A shown handle of a selected point under m: its point, and the side in `side`.
   int HandleAt(const Areas& a, const MSEGNode& n, ImVec2 m, int& side)
   {
      for (int i : S().sel)
      {
         if (i < 0 || i >= (int)n.points.size())
            continue;
         const MSEGNode::Point& p = n.points[i];
         const ImVec2 l = a.P(p.x + p.lx, p.y + p.ly), r = a.P(p.x + p.rx, p.y + p.ry);
         if (ShowsRight(n, i) && std::hypot(r.x - m.x, r.y - m.y) < kHit)
         {
            side = 1;
            return i;
         }
         if (ShowsLeft(n, i) && std::hypot(l.x - m.x, l.y - m.y) < kHit)
         {
            side = -1;
            return i;
         }
      }
      return -1;
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
   // each moves after the neighbour it is heading towards, so it can never pass it. Shift locks to one axis.
   void MoveSelection(MSEGNode& n, const Areas& a)
   {
      State& s = S();
      n.points = s.base;
      if (s.grab < 0 || s.grab >= (int)s.base.size())
         return;
      const ImVec2 m = ImGui::GetIO().MousePos;
      const MSEGNode::Point& g = s.base[s.grab];
      const CurveEdit::XY to = MouseXY(a, true, n);
      const bool shift = ImGui::GetIO().KeyShift;
      const bool vertical = shift && std::fabs(m.y - s.from.y) > std::fabs(m.x - s.from.x);
      const float dx = vertical ? 0.0f : to.x - g.x;
      const float dy = shift && !vertical ? 0.0f : to.y - g.y;
      std::vector<int> order = s.sel;
      if (dx > 0.0f)
         std::reverse(order.begin(), order.end());
      for (int i : order)
         if (i >= 0 && i < (int)n.points.size())
            n.MovePoint(i, s.base[i].x + dx, s.base[i].y + dy);
   }

   // Pulls a handle to the pointer. A dragged auto handle becomes Aligned (Blender's rule); Aligned keeps the
   // other side in line at its own length, Free leaves it. Side 0 pulls both, mirrored (the Pen's drag).
   void DragHandle(MSEGNode& n, const Areas& a)
   {
      State& s = S();
      if (s.grab < 0 || s.grab >= (int)n.points.size())
         return;
      MSEGNode::Point& p = n.points[s.grab];
      const ImVec2 m = ImGui::GetIO().MousePos;
      float ox = a.U(m.x) - p.x, oy = a.V(m.y) - p.y;
      if (p.handle != ModShapes::kHandleAligned && p.handle != ModShapes::kHandleFree)
         p.handle = ModShapes::kHandleAligned;
      const int side = s.handleSide;
      if (side == 0)
      {
         const float sx = ox >= 0.0f ? 1.0f : -1.0f;
         p.rx = ox * sx, p.ry = oy * sx;
         p.lx = -p.rx, p.ly = -p.ry;
      }
      else if (side > 0)
      {
         ox = std::max(0.0f, ox);
         p.rx = ox, p.ry = oy;
      }
      else
      {
         ox = std::min(0.0f, ox);
         p.lx = ox, p.ly = oy;
      }
      if (side != 0 && p.handle == ModShapes::kHandleAligned)
      {
         const float len = side > 0 ? std::hypot(p.lx, p.ly) : std::hypot(p.rx, p.ry);
         const float d = std::max(1.0e-6f, std::hypot(ox, oy));
         if (side > 0)
            p.lx = -ox / d * len, p.ly = -oy / d * len;
         else
            p.rx = -ox / d * len, p.ry = -oy / d * len;
      }
      n.Normalize();
   }

   void SelectInRect(const Areas& a, const MSEGNode& n, ImVec2 p0, ImVec2 p1, bool add)
   {
      State& s = S();
      const ImVec2 r0(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), r1(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
      if (!add)
         s.sel.clear();
      for (int i = 0; i < (int)n.points.size(); i++)
      {
         const ImVec2 c = a.P(n.points[i].x, n.points[i].y);
         if (c.x >= r0.x && c.x <= r1.x && c.y >= r0.y && c.y <= r1.y && !std::binary_search(s.sel.begin(), s.sel.end(), i))
            s.sel.insert(std::upper_bound(s.sel.begin(), s.sel.end(), i), i);
      }
      s.selCount = n.points.size();
   }

   void SelectOnly(int i, const MSEGNode& n)
   {
      S().sel.clear();
      if (i >= 0)
         S().sel.push_back(i);
      S().selCount = n.points.size();
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

   // The segments the selection's points start (none selected: every segment).
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

   // Moves the selected points (none selected: all) to the grid, one undo step.
   void Quantise(MSEGNode& n)
   {
      std::vector<int> idx = S().sel;
      if (idx.empty())
         for (int i = 0; i < (int)n.points.size(); i++)
            idx.push_back(i);
      PushUndoCheckpoint("Quantise Curve");
      CurveEdit::QuantiseX(n.points, idx, (float)GridStep(n));
      n.Normalize();
   }

   // Rubs out the inner points within kEraseR px of the pointer. The ends stay.
   bool EraseAt(const Areas& a, MSEGNode& n, ImVec2 m)
   {
      bool any = false;
      for (int i = (int)n.points.size() - 2; i >= 1; i--)
      {
         const ImVec2 c = a.P(n.points[i].x, n.points[i].y);
         if (std::hypot(c.x - m.x, c.y - m.y) <= kEraseR)
         {
            n.RemovePoint(i);
            any = true;
         }
      }
      return any;
   }

   void Press(const Areas& a, MSEGNode& n, int hover)
   {
      State& s = S();
      const ImGuiIO& io = ImGui::GetIO();
      const ImVec2 m = io.MousePos;
      s.from = m;
      const int count = (int)n.points.size();
      int side = 0;
      const int handle = (s.tool == Tool::Select || s.tool == Tool::Pen) ? HandleAt(a, n, m, side) : -1;
      if (handle >= 0)
      {
         PushUndoCheckpoint("Curve Handle");
         s.drag = Drag::Handle;
         s.grab = handle;
         s.handleSide = side;
         return;
      }
      switch (s.tool)
      {
      case Tool::Draw:
         s.drag = Drag::Stroke;
         s.stroke = { MouseXY(a, false, n) };
         return;
      case Tool::Line:
         s.drag = Drag::Line;
         s.lineFrom = MouseXY(a, true, n);
         return;
      case Tool::Bend:
      {
         if (count < 2)
            return;
         const int seg = ModShapes::MsegFindSegment(n.points.data(), count, std::clamp(a.U(m.x), 0.0f, 1.0f), -1);
         if (seg < 0 || seg + 1 >= count)
            return;
         PushUndoCheckpoint("Bend Curve");
         n.points[seg].interp = ModShapes::kInterpBend;
         s.drag = Drag::Bend;
         s.grab = seg;
         s.bendBase = n.points[seg].curve;
         // A positive bend moves fast first: above the straight line on a rise, below it on a fall.
         s.bendSign = n.points[seg + 1].y >= n.points[seg].y ? 1.0f : -1.0f;
         return;
      }
      case Tool::Eraser:
         PushUndoCheckpoint("Erase Curve Points");
         s.drag = Drag::Erase;
         if (EraseAt(a, n, m))
            ClearSelection();
         return;
      default: break;
      }
      // Select and Pen.
      if (hover >= 0)
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
         return;
      }
      const bool addPoint = s.tool == Tool::Pen || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
      if (!addPoint)
      {
         s.drag = Drag::Marquee;
         return;
      }
      PushUndoCheckpoint("Add Curve Point");
      const CurveEdit::XY at = MouseXY(a, true, n);
      const int i = n.AddPoint(at.x, at.y);
      SelectOnly(i, n);
      if (i <= 0 || s.tool != Tool::Pen)
         return;
      // The Pen draws bezier: the segments into and out of the new point, and a drag pulls its handles.
      n.points[i].interp = ModShapes::kInterpBezier;
      n.points[i - 1].interp = ModShapes::kInterpBezier;
      n.Normalize();
      s.drag = Drag::Handle;
      s.grab = i;
      s.handleSide = 0;
   }

   void Release(const Areas& a, MSEGNode& n)
   {
      State& s = S();
      const ImVec2 m = ImGui::GetIO().MousePos;
      switch (s.drag)
      {
      case Drag::Marquee: SelectInRect(a, n, s.from, m, ImGui::GetIO().KeyShift); break;
      case Drag::Stroke:
      {
         const std::vector<CurveEdit::XY> run = CurveEdit::Simplify(CurveEdit::CleanStroke(s.stroke), s.thinning);
         if (run.size() >= 2 && run.back().x - run.front().x > 1.0e-4f)
         {
            PushUndoCheckpoint("Draw Curve");
            CurveEdit::ReplaceRange(n.points, run, ModShapes::kInterpLinear, MSEGNode::kMaxPoints);
            n.Normalize();
            ClearSelection();
         }
         s.stroke.clear();
         break;
      }
      case Drag::Line:
      {
         CurveEdit::XY p0 = s.lineFrom, p1 = MouseXY(a, true, n);
         if (p1.x < p0.x)
            std::swap(p0, p1);
         if (p1.x - p0.x > 1.0e-4f)
         {
            PushUndoCheckpoint("Line Curve");
            CurveEdit::ReplaceRange(n.points, { p0, p1 }, ModShapes::kInterpLinear, MSEGNode::kMaxPoints);
            n.Normalize();
            ClearSelection();
         }
         break;
      }
      default: break;
      }
      s.drag = Drag::None;
      s.grab = -1;
      s.base.clear();
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
         Press(a, n, hover);
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && s.drag == Drag::None)
      {
         if (hover >= 0 && !std::binary_search(s.sel.begin(), s.sel.end(), hover))
            SelectOnly(hover, n);
         ImGui::OpenPopup("##curveMenu");
      }

      switch (s.drag)
      {
      case Drag::Pan:
      {
         const float span = s.v1 - s.v0;
         s.v0 = s.panV0 - (m.x - s.from.x) / std::max(1.0f, a.x1 - a.x0) * span;
         s.v1 = s.v0 + span;
         break;
      }
      case Drag::Points: MoveSelection(n, a); break;
      case Drag::Handle:
         if (std::hypot(m.x - s.from.x, m.y - s.from.y) > 2.0f)
            DragHandle(n, a);
         break;
      case Drag::Stroke:
      {
         const CurveEdit::XY p = MouseXY(a, false, n);
         if (std::fabs(a.X(p.x) - a.X(s.stroke.back().x)) >= 1.0f || std::fabs(a.Y(p.y) - a.Y(s.stroke.back().y)) >= 2.0f)
            s.stroke.push_back(p);
         break;
      }
      case Drag::Bend:
         if (s.grab >= 0 && s.grab < (int)n.points.size())
            n.points[s.grab].curve =
               std::clamp(s.bendBase + s.bendSign * (s.from.y - m.y) / std::max(1.0f, (a.y1 - a.y0) * 0.5f), -1.0f, 1.0f);
         break;
      case Drag::Erase:
         if (EraseAt(a, n, m))
            ClearSelection();
         break;
      default: break;
      }
      if (s.drag != Drag::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
         Release(a, n);
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
      if (MenuParts::Item(T("Quantise to Grid"), "Q"))
         Quantise(n);
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
         static const struct
         {
            ImGuiKey key;
            Tool tool;
         } kTools[] = { { ImGuiKey_A, Tool::Select }, { ImGuiKey_H, Tool::Hand }, { ImGuiKey_P, Tool::Pen }, { ImGuiKey_D, Tool::Draw },
                        { ImGuiKey_L, Tool::Line },   { ImGuiKey_B, Tool::Bend }, { ImGuiKey_E, Tool::Eraser } };
         for (const auto& t : kTools)
            if (ImGui::IsKeyPressed(t.key, false))
               s.tool = t.tool;
         if (ImGui::IsKeyPressed(ImGuiKey_G, false))
            s.snap = !s.snap;
         if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            s.v0 = 0.0f, s.v1 = 1.0f;
         if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
            Quantise(n);
         if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
         {
            if (gCurveAssignUid != 0)
               gCurveAssignUid = 0;
            else
               ClearSelection();
         }
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
         if (i > 0 && row)
            FormParts::Inline(0.0f, tok::space_1);
         const std::string id = "##ceTool" + std::to_string(i);
         if (IconChip(id.c_str(), ToolGlyph((Tool)i), s.tool == (Tool)i, ToolTip((Tool)i)))
            s.tool = (Tool)i;
      }
   }

   // The page strip: one chip per open page, then +, and the ghost toggle at the end.
   void DrawPageStrip(float width)
   {
      State& s = S();
      const float ghostW = kIconW + tok::space_2;
      ImGui::BeginChild("##cePages", ImVec2(std::max(1.0f, width - ghostW), tok::tile), false, ImGuiWindowFlags_NoScrollbar);
      if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
         ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 40.0f);
      static uint64_t sMenuPage = 0;
      bool openMenu = false;
      bool first = true;
      for (GraphNode* gn : Pages())
      {
         if (!first)
            FormParts::Inline(0.0f, tok::space_1);
         first = false;
         const bool on = gn->uid == s.page;
         const std::string title = NodeTitleWithInstance(*gn);
         const std::string id = "##cePage" + std::to_string(gn->uid);
         const float w = std::min(kPageW, ImGui::CalcTextSize(title.c_str()).x + tok::icon_md + 3.0f * tok::space_3);
         ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
         if (ActionButton::Draw(id.c_str(), ImVec2(w, tok::tile)))
            ShowPage(gn->uid);
         ActionButton::Scoped() = ActionButton::Kind::Plain;
         if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
         {
            sMenuPage = gn->uid;
            openMenu = true;
         }
         const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
         const ImU32 col = on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float cy = (bmin.y + bmax.y) * 0.5f;
         glyph::Draw(dl, ImVec2(bmin.x + tok::space_2 + tok::icon_md * 0.5f, cy), tok::icon_md * 0.8f, col, IconsInfinite::Curve);
         dl->PushClipRect(bmin, ImVec2(bmax.x - tok::space_2, bmax.y), true);
         dl->AddText(ImVec2(bmin.x + tok::space_2 + tok::icon_md + tok::space_1, cy - ImGui::GetFontSize() * 0.5f), col, title.c_str());
         dl->PopClipRect();
      }
      if (!first)
         FormParts::Inline(0.0f, tok::space_1);
      if (IconChip("##ceAdd", IconsInfinite::Plus, false, T("New curve")))
         AddPage();
      if (openMenu)
         ImGui::OpenPopup("##cePageMenu");
      if (MenuParts::BeginPopup("##cePageMenu"))
      {
         if (MenuParts::Item(T("Close Page")))
         {
            s.closed.push_back(sMenuPage);
            if (s.page == sMenuPage)
            {
               s.page = 0;
               ClearSelection();
            }
         }
         if (MenuParts::Item(T("Delete Curve")))
            DeletePage(sMenuPage);
         MenuParts::EndPopup();
      }
      ImGui::EndChild();
      FormParts::Inline(0.0f, tok::space_2);
      if (IconChip("##ceGhost", s.ghost ? IconsInfinite::Eye : IconsInfinite::EyeOff, s.ghost, T("Show other curves")))
         s.ghost = !s.ghost;
   }

   std::string TargetName(int nodeIndex, int paramIndex)
   {
      std::string name;
      if (GraphNode* gn = FindNodeByIndex(nodeIndex))
         name = NodeTitleWithInstance(*gn);
      const ParamRef* ref = Modulation::Instance().KnownParam(nodeIndex, paramIndex);
      name += ": ";
      name += ref != nullptr && !ref->name.empty() ? ref->name : std::string(T("param ")) + std::to_string(paramIndex);
      return name;
   }

   int TargetCount(const GraphNode& gn)
   {
      int count = 0;
      for (const auto& link : Modulation::Instance().Links())
         if (link.second.nodeIndex == gn.index)
            count++;
      return count;
   }

   void DrawTargets(const GraphNode& gn)
   {
      FormParts::Section(T("Targets"));
      Modulation& mod = Modulation::Instance();
      std::vector<std::pair<Modulation::Key, Modulation::Source>> rows;
      for (const auto& link : mod.Links())
         if (link.second.nodeIndex == gn.index)
            rows.push_back(link);
      if (rows.empty())
      {
         ImGui::TextDisabled("%s", T("Assign, then click a knob"));
         return;
      }
      for (const auto& [key, src] : rows)
      {
         ImGui::PushID(key.first * 4096 + key.second);
         const std::string name = TargetName(key.first, key.second);
         const float rowX = ImGui::GetCursorPosX(), rowW = ImGui::GetContentRegionAvail().x;
         ImGui::AlignTextToFramePadding();
         ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + rowW - kIconW - tok::space_2, 1.0e6f), true);
         ImGui::TextUnformatted(name.c_str());
         ImGui::PopClipRect();
         ImGui::SameLine(rowX + rowW - kIconW);
         if (IconChip("##ceUnbind", IconsInfinite::Close, false, T("Remove target")))
         {
            PushUndoCheckpoint("Remove Curve Target");
            mod.Unbind(key.first, key.second);
            gPatchDirty = true;
            ImGui::PopID();
            break;
         }
         const ParamRef* ref = mod.KnownParam(key.first, key.second);
         float lo = src.lo, hi = src.hi;
         const float pmin = ref != nullptr ? ref->minValue : std::min(lo, hi);
         const float pmax = ref != nullptr ? ref->maxValue : std::max(lo, hi);
         bool changed = FormParts::SliderFloat(T("Low"), &lo, pmin, pmax, "%.3g");
         UndoOnGrab();
         changed |= FormParts::SliderFloat(T("High"), &hi, pmin, pmax, "%.3g");
         UndoOnGrab();
         if (changed)
         {
            mod.SetRange(key.first, key.second, lo, hi);
            gPatchDirty = true;
         }
         ImGui::PopID();
      }
   }

   void DrawInspector(GraphNode& gn, MSEGNode& n)
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
            const MSEGNode::Point p = n.points[first];
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

      DrawTargets(gn);

      FormParts::Section(T("Voices"));
      bool voice = n.voice;
      if (FormParts::Checkbox(T("Per voice"), &voice))
      {
         PushUndoCheckpoint("Curve Per Voice");
         n.voice = voice;
      }
      SectionCard::EndWindow();
   }

   // The footer under the graph: snap, grid, Draw's thinning; Assign and the target count on the right.
   void DrawFooter(GraphNode& gn, MSEGNode& n, float width)
   {
      State& s = S();
      const float startX = ImGui::GetCursorPosX();
      if (IconChip("##ceSnap", s.snap ? IconsInfinite::NoteSnapFill : IconsInfinite::NoteSnap, s.snap, T("Snap to grid (G)")))
         s.snap = !s.snap;
      FormParts::Inline(0.0f, tok::space_1);
      Picker("##ceGrid", GridNames(n), GridIndex(n), 96.0f, T("Grid"));
      if (s.tool == Tool::Draw)
      {
         FormParts::Inline(0.0f, tok::space_3);
         ImGui::SetNextItemWidth(110.0f);
         float pct = s.thinning * 100.0f;
         if (ImGui::SliderFloat("##ceThin", &pct, 0.1f, 10.0f, "thin %.1f%%", ImGuiSliderFlags_Logarithmic))
            s.thinning = pct / 100.0f;
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Thinning"));
      }
      const int targets = TargetCount(gn);
      char count[48];
      std::snprintf(count, sizeof count, targets == 1 ? T("%d target") : T("%d targets"), targets);
      const float countW = ImGui::CalcTextSize(count).x;
      constexpr float kAssignW = 80.0f;
      const float right = startX + width - kAssignW - countW - tok::space_3;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX() + tok::space_3, right));
      const bool armed = gCurveAssignUid == gn.uid;
      if (ActionButton::Draw(T("Assign"), ImVec2(kAssignW, tok::tile), armed ? ActionButton::Kind::Learn : ActionButton::Kind::Plain))
         gCurveAssignUid = armed ? 0 : gn.uid;
      if (ImGui::IsItemHovered())
         HelpTip("%s", T("Click a knob to drive it; Shift picks more, Esc stops"));
      FormParts::Inline(0.0f, tok::space_3);
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", count);
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
         std::vector<GraphNode*> closed;
         for (GraphNode* gn : Pages(true))
            if (IsClosed(gn->uid))
               closed.push_back(gn);
         if (!closed.empty() && MenuParts::SubMenu(T("Reopen Page")))
         {
            for (GraphNode* gn : closed)
               if (MenuParts::Item(NodeTitleWithInstance(*gn).c_str()))
                  ShowPage(gn->uid);
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         if (MenuParts::Item(T("Close Curve Editor")))
            gCurveEditorOpen = false;
         MenuParts::EndPopup();
      }
   }

   void DrawGraphArea(MSEGNode& n, ImVec2 size)
   {
      State& s = S();
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##ceGraph", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                             ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
      const bool hovered = ImGui::IsItemHovered();
      const Areas a { at.x, at.x + size.x, at.y, at.y + kRulerH, at.y + size.y };
      if (!s.sel.empty() && s.selCount != n.points.size() && s.drag == Drag::None)
         ClearSelection(); // the points changed elsewhere (card, undo, agent): indices no longer hold
      const int hover = hovered ? PointAt(a, n, ImGui::GetIO().MousePos) : -1;
      HandleGraphInput(a, n, hovered, hover);
      if (!s.sel.empty())
         s.selCount = n.points.size(); // edits made here keep the selection's count in step
      if (hovered || s.drag != Drag::None)
      {
         if (s.tool == Tool::Hand || s.drag == Drag::Pan)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         else if (s.tool != Tool::Select && s.tool != Tool::Eraser)
            ToolCursor::Badge(ToolGlyph(s.tool));
      }
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
      if (AsMseg(PageNode()) == nullptr || IsClosed(s.page))
      {
         const std::vector<GraphNode*> pages = Pages();
         s.page = pages.empty() ? 0 : pages.front()->uid;
         ClearSelection();
      }
      GraphNode* gn = PageNode();
      MSEGNode* n = AsMseg(gn);
      if (gCurveAssignUid != 0 && AsMseg(FindNodeByUid(gCurveAssignUid)) == nullptr)
         gCurveAssignUid = 0;

      const bool side = gCurveEditorDock == 1 || gCurveEditorDock == 2;
      DrawPageStrip(ImGui::GetContentRegionAvail().x);
      if (n == nullptr)
      {
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         const ImVec2 mn = ImGui::GetCursorScreenPos();
         EmptyState::Draw(mn, ImVec2(mn.x + avail.x, mn.y + avail.y), T("No curve yet"), T("+ makes an MSEG node and opens it here"));
         return;
      }
      HandleKeys(*n);
      const float gap = tok::space_2;
      const float footerH = tok::tile + gap;
      if (side)
      {
         // Rows: tools, graph, footer, inspector folded under it.
         DrawToolButtons(true);
         const ImVec2 avail = ImGui::GetContentRegionAvail();
         DrawGraphArea(*n, ImVec2(avail.x, std::max(140.0f, avail.y * 0.5f)));
         ImGui::Dummy(ImVec2(0.0f, gap * 0.5f));
         DrawFooter(*gn, *n, avail.x);
         ImGui::BeginChild("##ceInspector", ImVec2(avail.x, std::max(1.0f, ImGui::GetContentRegionAvail().y)), false);
         DrawInspector(*gn, *n);
         ImGui::EndChild();
         return;
      }
      // Columns: tool rail, graph over its footer, inspector on the outer edge.
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float bodyH = std::max(1.0f, avail.y);
      ImGui::BeginChild("##ceRail", ImVec2(kIconW, bodyH), false, ImGuiWindowFlags_NoScrollbar);
      DrawToolButtons(false);
      ImGui::EndChild();
      FormParts::Inline(0.0f, gap);
      const float graphW = std::max(80.0f, avail.x - kIconW - kInspectorW - 2.0f * gap);
      ImGui::BeginGroup();
      DrawGraphArea(*n, ImVec2(graphW, std::max(60.0f, bodyH - footerH)));
      ImGui::Dummy(ImVec2(0.0f, gap * 0.5f));
      DrawFooter(*gn, *n, graphW);
      ImGui::EndGroup();
      FormParts::Inline(0.0f, gap);
      ImGui::BeginChild("##ceInspector", ImVec2(kInspectorW, bodyH), false);
      DrawInspector(*gn, *n);
      ImGui::EndChild();
   }
}

void CurveEditorOpenNode(uint64_t uid)
{
   gCurveEditorOpen = true;
   ShowPage(uid);
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

// Generic node parameter bodies, part 2: curves, palettes, ramps, analyze (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/Thumb.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/components/Tip.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/AppShared.h"
#include "app/ui/design/DrawParam.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "app/ui/design/Glyphs.gen.h"
#include "platform/AppPaths.h"
#include "app/ui/design/UiFmt.h"

namespace app
{
   // Interactive curve editor shared by CurvesNode (image tone curve) and
   // ModCurveNode (modulator transfer curve). Drag points, click empty space
   // to add one, right-click a point to remove it. When resetOnEmptyRightClick
   // is set, right-clicking empty space resets the whole curve to a straight
   // line instead of doing nothing - ModCurveNode wants this (there is no
   // per-channel "Reset channel" button for it), CurvesNode does not.
   // liveX >= 0 draws a dot on the curve at that input position; crosshair
   // draws a 0.5/0.5 gridline, which is where "no modulation" lives under a
   // bipolar binding.
   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawCurveEditor(CurveShape& shape, ImU32 lineCol, bool resetOnEmptyRightClick = false,
                        bool crosshair = false, float liveX = -1.0f)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::PushID(&shape);
      UiInteract::Hit("##curve", ImVec2(size, size));
      ImGui::PopID();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      // R12: plotted values and handles live in an inset box so nothing touches or crosses the frame.
      const float pad = tok::space_1_5;
      const float inner = size - tok::param_inner_ratio * pad;
      auto toScreen = [&](float x, float y) {
         return ImVec2(origin.x + pad + x * inner, origin.y + pad + (1.0f - y) * inner);
      };
      auto toCurve = [&](ImVec2 p) {
         return ImVec2(std::min(1.0f, std::max(0.0f, (p.x - origin.x - pad) / inner)),
                       std::min(1.0f, std::max(0.0f, 1.0f - (p.y - origin.y - pad) / inner)));
      };

      std::vector<CurveShape::Point>& pts = shape.points;

      static int sDragIndex = -1;
      static CurveShape* sDragShape = nullptr;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 12.0f;
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         float d = std::sqrt((sp.x - mouse.x) * (sp.x - mouse.x) + (sp.y - mouse.y) * (sp.y - mouse.y));
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         if (nearest >= 0)
         {
            sDragIndex = nearest;
            sDragShape = &shape;
         }
         else
         {
            ImVec2 c = toCurve(mouse);
            sDragIndex = shape.AddPoint(c.x, c.y);
            sDragShape = &shape;
         }
      }
      if (hovered && MenuParts::RightPressed())
      {
         if (nearest >= 0)
         {
            PushUndoCheckpoint();
            shape.RemovePoint(nearest);
         }
         else if (resetOnEmptyRightClick)
         {
            PushUndoCheckpoint();
            shape.Reset();
         }
      }

      if (active && sDragShape == &shape && sDragIndex >= 0)
      {
         ImVec2 c = toCurve(mouse);
         shape.MovePoint(sDragIndex, c.x, c.y);
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragShape = nullptr;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + pad + inner * f, origin.y), ImVec2(origin.x + pad + inner * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + pad + inner * f), ImVec2(br.x, origin.y + pad + inner * f), ScopeGridCol());
      }
      dl->AddLine(toScreen(0.0f, 0.0f), toScreen(tok::param_to_screen, tok::param_to_screen), ScopeMidLineCol()); // identity reference
      if (crosshair)
      {
         ImVec2 mid = toScreen(0.5f, 0.5f);
         dl->AddLine(ImVec2(mid.x, origin.y), ImVec2(mid.x, br.y), isLight ? tok::Base(tok::col::edge_lo_pale, tok::param_line_alpha_md) : tok::Base(tok::col::edge_mid, tok::param_line_alpha_md));
         dl->AddLine(ImVec2(origin.x, mid.y), ImVec2(br.x, mid.y), isLight ? tok::Base(tok::col::edge_lo_pale, tok::param_line_alpha_md) : tok::Base(tok::col::edge_mid, tok::param_line_alpha_md));
      }

      const int kSegments = tok::param_segments;
      for (int i = 1; i <= kSegments; i++)
      {
         float x0 = (float)(i - 1) / kSegments;
         float x1 = (float)i / kSegments;
         dl->AddLine(toScreen(x0, shape.Evaluate(x0)),
                     toScreen(x1, shape.Evaluate(x1)), lineCol, tok::param_curve_editor_stroke);
      }
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearest ? tok::param_curve_editor_r : tok::param_curve_editor_dot_r_sm, lineCol);
         dl->AddCircle(sp, i == nearest ? tok::param_curve_editor_r : tok::param_curve_editor_dot_r_sm, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), 0, tok::param_ring_stroke_md);
      }
      if (liveX >= 0.0f)
      {
         float clampedX = std::min(1.0f, std::max(0.0f, liveX));
         ImVec2 dot = toScreen(clampedX, shape.Evaluate(clampedX));
         dl->AddCircleFilled(dot, tok::param_curve_editor_dot_r_lg, isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation));
         dl->AddCircle(dot, tok::param_curve_editor_dot_r_lg, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), 0, tok::param_ring_stroke_md);
      }
      AudioViz::Border(dl, origin, br);
   }
   // ui-content-end


   namespace
   {
      using CurvesParamsRow = params::Row<CurvesNode>;
   }

   void DrawCurvesParams(CurvesNode* n)
   {
      Ui::DrawParam(n, CurvesParamsRow::Choice<&CurvesNode::activeChannel>("channel", &CurvesNode::ChannelNames));
      const bool isLight = IsThemeLight();
      ImU32 lineCol = isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::edge_hi);
      if (n->activeChannel == CurvesNode::kRed)   lineCol = isLight ? tok::Base(tok::col::state_err) : tok::Base(tok::col::text_err);
      if (n->activeChannel == CurvesNode::kGreen) lineCol = isLight ? tok::Base(tok::col::state_ok_deep_vivid) : tok::Base(tok::col::state_ok);
      if (n->activeChannel == CurvesNode::kBlue)  lineCol = isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_xlo);
      DrawCurveEditor(n->Shape(n->activeChannel), lineCol);
      if (ActionButton::Draw("Reset channel", ImVec2(kPreviewSize, 0)))
         n->ResetChannel(n->activeChannel);
      Ui::DrawParam(n, CurvesParamsRow::Slider<&CurvesNode::mix>("mix", 0.0f, 1.0f));
   }


   namespace
   {
      using PredictiveColoringParamsRow = params::Row<PredictiveColoringNode>;
      const PredictiveColoringParamsRow kPredictiveColoringParamsRows[] = {
         PredictiveColoringParamsRow::Slider<&PredictiveColoringNode::wander>("wander", 0.0f, 1.0f),
         PredictiveColoringParamsRow::Slider<&PredictiveColoringNode::mix>("mix", 0.0f, 1.0f),
      };
   }

   void DrawPredictiveColoringParams(PredictiveColoringNode* n)
   {
      ImGui::PushID(n);
      const bool isLight = IsThemeLight();
      const bool learning = n->IsLearning();
      const float conf = n->Confidence01();
      const bool hasLearned = n->TotalSamples() > 0;

      char statusBuf[64];
      if (learning)
         snprintf(statusBuf, sizeof(statusBuf), "learning active footage...");
      else if (hasLearned)
         snprintf(statusBuf, sizeof(statusBuf), "target profile active");
      else
         snprintf(statusBuf, sizeof(statusBuf), "press Learn to profile footage");

      ImGui::TextDisabled("%s", statusBuf);

      const char* learnLabel = learning ? "Stop Learning" : (hasLearned ? "Learn Again" : "Learn");
      if (ActionButton::Draw(learnLabel, ImVec2(hasLearned && !learning ? tok::param_predictive_coloring_x_sm : tok::param_predictive_coloring_x_lg, 0)))
      {
         PushUndoCheckpoint();
         n->SetLearning(!learning);
      }
      if (hasLearned && !learning)
      {
         FormParts::Inline();
         if (ActionButton::Draw("Reset", ImVec2(tok::param_reset_x, 0)))
         {
            PushUndoCheckpoint();
            n->ResetProfile();
         }
      }
      // Confidence reads on its own status line under the buttons, not squeezed beside Learn.
      char confText[32];
      // While learning, show THIS take's own local progress - not the blended confidence, which
      // is mostly settled shared/house-style weight and barely moves as this instance learns.
      // Once stopped, show the blended confidence that actually governs the applied grade.
      const float badgeVal = learning ? n->LocalConfidence01() : conf;
      if (learning)
         snprintf(confText, sizeof(confText), "learning - %s", UiFmt::Pct((int)std::round(badgeVal * 100.0f)).c);
      else
         snprintf(confText, sizeof(confText), "%s conf", UiFmt::Pct((int)std::round(badgeVal * 100.0f)).c);
      ImGui::TextColored(badgeVal > tok::param_text_colored_limit ? (isLight ? tok::BaseV4(tok::col::state_ok_deep_vivid) : tok::BaseV4(tok::col::text_ok_dark))
                                    : (isLight ? tok::BaseV4(tok::col::text_warn_deep_fixed) : tok::BaseV4(tok::col::text_modulation_pale)),
                         "%s", confText);

      PushCheckboxStyle();
      Ui::DrawParam(n, PredictiveColoringParamsRow::Toggle<&PredictiveColoringNode::selfNormalize>("self normalize"));
      PopCheckboxStyle();

      Ui::DrawParams(n, kPredictiveColoringParamsRows);
      ImGui::PopID();
   }


   namespace
   {
      using ModCurveParamsRow = params::Row<ModCurveNode>;
   }

   void DrawModCurveParams(ModCurveNode* n)
   {
      const bool isLight = IsThemeLight();
      const float in = n->input ? n->input->Value01() : n->constantIn;
      DrawCurveEditor(n->curve, isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::edge_hi), /*resetOnEmptyRightClick=*/true,
                     /*crosshair=*/true, /*liveX=*/in);
      if (n->input == nullptr)
         Ui::DrawParam(n, ModCurveParamsRow::Slider<&ModCurveNode::constantIn>("in (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in: patched");
      Ui::DrawParam(n, ModCurveParamsRow::Slider<&ModCurveNode::mix>("mix", 0.0f, 1.0f));
   }


   // The MSEG's shape: drag a point, click empty space to add one, right-click
   // a point to remove it (empty space: reset). The small ring on each segment
   // bends it; drag it up or down. Loop on shades the looped part, and the
   // vertical line is where the shape is now.
   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   static void DrawMSEGEditor(MSEGNode* n, ImU32 lineCol)
   {
      const float width = kPreviewSize, height = tok::param_mseg_editor_height;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::PushID(n);
      UiInteract::Hit("##mseg", ImVec2(width, height));
      ImGui::PopID();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      const float pad = tok::space_1_5;
      const float innerW = width - tok::param_inner_w_ratio * pad, innerH = height - tok::param_inner_h_ratio * pad;
      auto toScreen = [&](float x, float y) {
         return ImVec2(origin.x + pad + x * innerW, origin.y + pad + (1.0f - y) * innerH);
      };
      auto toShape = [&](ImVec2 p) {
         return ImVec2(std::clamp((p.x - origin.x - pad) / innerW, 0.0f, 1.0f),
                       std::clamp(1.0f - (p.y - origin.y - pad) / innerH, 0.0f, 1.0f));
      };
      std::vector<MSEGNode::Point>& pts = n->points;
      auto bendHandle = [&](int i) {
         const float xm = 0.5f * (pts[i].x + pts[i + 1].x);
         return toScreen(xm, MSEGNode::Segment(pts[i].y, pts[i + 1].y, pts[i].curve, 0.5f));
      };

      static MSEGNode* sDragNode = nullptr;
      static int sDragIndex = -1;
      static bool sDragBend = false;
      static float sDragStartY = 0.0f, sDragStartCurve = 0.0f;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      auto dist = [&](ImVec2 p) { return std::sqrt((p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y)); };
      int nearPoint = -1, nearBend = -1;
      float best = 10.0f;
      for (int i = 0; i < (int)pts.size(); i++)
         if (dist(toScreen(pts[i].x, pts[i].y)) < best)
         {
            best = dist(toScreen(pts[i].x, pts[i].y));
            nearPoint = i;
         }
      if (nearPoint < 0)
      {
         best = 8.0f;
         for (int i = 0; i + 1 < (int)pts.size(); i++)
            if (pts[i].interp == ModShapes::kInterpBend && pts[i + 1].x - pts[i].x > tok::param_mseg_editor_limit && dist(bendHandle(i)) < best)
            {
               best = dist(bendHandle(i));
               nearBend = i;
            }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         sDragNode = n;
         sDragBend = false;
         if (nearPoint >= 0)
            sDragIndex = nearPoint;
         else if (nearBend >= 0)
         {
            sDragIndex = nearBend;
            sDragBend = true;
            sDragStartY = mouse.y;
            sDragStartCurve = pts[nearBend].curve;
         }
         else
         {
            const ImVec2 c = toShape(mouse);
            sDragIndex = n->AddPoint(c.x, c.y);
         }
      }
      if (hovered && MenuParts::RightPressed())
      {
         PushUndoCheckpoint();
         if (nearPoint >= 0)
            n->RemovePoint(nearPoint);
         else if (nearBend >= 0)
            pts[nearBend].curve = 0.0f;
         else
            n->Reset();
      }
      if (active && sDragNode == n && sDragIndex >= 0 && sDragIndex < (int)pts.size())
      {
         if (sDragBend && sDragIndex + 1 < (int)pts.size())
         {
            const float rising = pts[sDragIndex + 1].y >= pts[sDragIndex].y ? 1.0f : -1.0f;
            pts[sDragIndex].curve = std::clamp(sDragStartCurve + rising * (sDragStartY - mouse.y) / tok::param_mseg_editor_ratio, -1.0f, 1.0f);
         }
         else if (!sDragBend)
         {
            const ImVec2 c = toShape(mouse);
            n->MovePoint(sDragIndex, c.x, c.y);
         }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragNode = nullptr;
         sDragIndex = -1;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + width, origin.y + height);
      const bool isLight = IsThemeLight();
      const ImU32 ring = isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep);
      AudioViz::Fill(dl, origin, br);
      for (int i = 1; i < 4; i++)
      {
         const float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + pad + innerW * f, origin.y), ImVec2(origin.x + pad + innerW * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + pad + innerH * f), ImVec2(br.x, origin.y + pad + innerH * f), ScopeGridCol());
      }
      if (n->loop)
      {
         const float a = std::clamp(std::min(n->loopStart, n->loopEnd), 0.0f, 1.0f);
         const float b = std::clamp(std::max(n->loopStart, n->loopEnd), 0.0f, 1.0f);
         dl->AddRectFilled(ImVec2(toScreen(a, 0).x, origin.y), ImVec2(toScreen(b, 0).x, br.y), (lineCol & 0x00FFFFFF) | tok::param_mseg_editor_alpha);  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)
         dl->AddLine(ImVec2(toScreen(a, 0).x, origin.y), ImVec2(toScreen(a, 0).x, br.y), ScopeMidLineCol());
         dl->AddLine(ImVec2(toScreen(b, 0).x, origin.y), ImVec2(toScreen(b, 0).x, br.y), ScopeMidLineCol());
      }
      const int kSteps = 96;
      for (int i = 1; i <= kSteps; i++)
      {
         const float x0 = (float)(i - 1) / kSteps, x1 = (float)i / kSteps;
         dl->AddLine(toScreen(x0, n->Evaluate(x0)), toScreen(x1, n->Evaluate(x1)), lineCol, tok::param_mseg_editor_stroke);
      }
      for (int i = 0; i + 1 < (int)pts.size(); i++)
         if (pts[i].interp == ModShapes::kInterpBend && pts[i + 1].x - pts[i].x > tok::param_mseg_editor_limit)
            dl->AddCircle(bendHandle(i), i == nearBend ? tok::param_mseg_editor_r : tok::param_ring_r_sm, lineCol, 0, tok::param_ring_stroke_sm);
      for (int i = 0; i < (int)pts.size(); i++)
      {
         const ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearPoint ? tok::param_mseg_editor_dot_r_lg : tok::param_mseg_editor_r, lineCol);
         dl->AddCircle(sp, i == nearPoint ? tok::param_mseg_editor_dot_r_lg : tok::param_mseg_editor_r, ring, 0, tok::param_ring_stroke_md);
      }
      const float px = n->Position();
      const ImU32 liveCol = isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation);
      dl->AddLine(ImVec2(toScreen(px, 0).x, origin.y), ImVec2(toScreen(px, 0).x, br.y), (liveCol & 0x00FFFFFF) | tok::param_line_alpha_sm_hex);  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)
      dl->AddCircleFilled(toScreen(px, n->Evaluate(px)), tok::param_mseg_editor_dot_r_sm, liveCol);
      dl->AddCircle(toScreen(px, n->Evaluate(px)), tok::param_mseg_editor_dot_r_sm, ring, 0, tok::param_ring_stroke_md);
      AudioViz::Border(dl, origin, br);
   }
   // ui-content-end


   void DrawModRateControls(int* rateMode, float* rateBeats, float* rateHz, int hzOrdinal,
                            const std::vector<std::string>& names); // ParamBodies1.cpp
   void RegisterHiddenParam(float* value, float minV, float maxV, const char* name, int ordinal); // ParamBodies1.cpp

   // A slider at a fixed ordinal, or (not shown) the same param registered
   // hidden, so modulation keeps its slot whichever controls are on show.
   static void FixedModSlider(bool shown, int ordinal, const char* label, float* v, float lo, float hi, const char* fmt)
   {
      FixedParamOrdinal at(ordinal);
      if (shown)
         ModSlider(label, v, lo, hi, fmt);
      else
      {
         RegisterHiddenParam(v, lo, hi, label, ordinal);
         NoteHiddenParamAnchorAtLastItem(ordinal);
      }
   }

   static const std::vector<std::string>& MsegPlayNames() // ModShapes::MsegPlay order
   {
      static const std::vector<std::string> names = { "Repeat", "Once", "Ping-pong" };
      return names;
   }

   namespace
   {
      using MSEGParamsRow = params::Row<MSEGNode>;
      const MSEGParamsRow kMSEGParamsRows[] = {
         MSEGParamsRow::Slider<&MSEGNode::loopStart>("loop start", 0.0f, 1.0f),
         MSEGParamsRow::Slider<&MSEGNode::loopEnd>("loop end", 0.0f, 1.0f),
      };
   }

   void DrawMSEGParams(MSEGNode* n)
   {
      const bool isLight = IsThemeLight();
      DrawMSEGEditor(n, isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::edge_hi));
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 3, MSEGNode::RateModeNames());
      // Ordinals 4-7 come after the card's first four (rate, loop start,
      // loop end, rate Hz) so saved cables keep their targets.
      FixedModSlider(n->rateMode == MSEGNode::kRateSeconds, 5, "length", &n->lengthSec, 0.01f, 600.0f, "%.2f s");
      FixedModSlider(n->rateMode == MSEGNode::kRateFrames, 6, "frames", &n->lengthFrames, 1.0f, 10000.0f, "%.0f");
      FixedModSlider(n->rateMode == MSEGNode::kRateFrames, 7, "fps", &n->fps, 1.0f, 240.0f, "%.2f");
      FixedModSlider(true, 4, "speed", &n->speed, 0.25f, 4.0f, "%.2fx");
      DropdownButton("play", MsegPlayNames(), n->play, [n](int i) { PushUndoCheckpoint(); n->play = i; });
      PushCheckboxStyle();
      Ui::DrawParam(n, MSEGParamsRow::Toggle<&MSEGNode::loop>("loop"));
      PopCheckboxStyle();
      // Drawn either way so the two keep their slots; they do nothing with loop off.
      ImGui::BeginDisabled(!n->loop);
      Ui::DrawParams(n, kMSEGParamsRows);
      ImGui::EndDisabled();
      DrawPerVoiceCheckbox(&n->voice);
      PushCheckboxStyle();
      Ui::DrawParam(n, MSEGParamsRow::Toggle<&MSEGNode::freeRun>("free run"));
      PopCheckboxStyle();
   }


   namespace
   {
      using RemoveBgParamsRow = params::Row<RemoveBgNode>;
      const RemoveBgParamsRow kRemoveBgParamsRows1[] = {
         RemoveBgParamsRow::Choice<&RemoveBgNode::mode>("detect", &RemoveBgNode::ModeNames),
         RemoveBgParamsRow::Choice<&RemoveBgNode::outputMode>("output", &RemoveBgNode::OutputModeNames),
      };
      const RemoveBgParamsRow kRemoveBgParamsRows2[] = {
         RemoveBgParamsRow::Slider<&RemoveBgNode::feather>("feather", params::RemoveBg::feather_lo, params::RemoveBg::feather_hi),
         RemoveBgParamsRow::Slider<&RemoveBgNode::threshold>("threshold", 0.0f, 1.0f),
         RemoveBgParamsRow::Slider<&RemoveBgNode::contrast>("edge contrast", params::RemoveBg::contrast_lo, params::RemoveBg::contrast_hi),
         RemoveBgParamsRow::Colour<&RemoveBgNode::bgColor>("bg"),
         RemoveBgParamsRow::Slider<&RemoveBgNode::bgOpacity>("bg opacity", 0.0f, 1.0f),

         RemoveBgParamsRow::Toggle<&RemoveBgNode::autoRefresh>("auto refresh (video)"),
      };
   }

   void DrawRemoveBgParams(RemoveBgNode* n)
   {
      Ui::DrawParams(n, kRemoveBgParamsRows1);

      if (ActionButton::Draw("Remove Background", ImVec2(kPreviewSize, 0)))
      {
         PushUndoCheckpoint("Remove Background");
         n->RequestMask();
      }

      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      if (ImGui::IsItemHovered())
         Tip::Show("engine: %s", Platform::MattingBackend().c_str());
      PopTextWrapWidth();

      Ui::DrawParams(n, kRemoveBgParamsRows2);
      if (n->autoRefresh)
      {
         Ui::DrawParam(n, RemoveBgParamsRow::Slider<&RemoveBgNode::refreshFrames>("every N frames", params::RemoveBg::refresh_frames_lo, params::RemoveBg::refresh_frames_hi).Fmt("%.0f"));
      }
   }


   void DrawFeedbackParams(FeedbackNode*)
   {
   }


   namespace
   {
      using TrailsParamsRow = params::Row<TrailsNode>;
      const TrailsParamsRow kTrailsParamsRows[] = {
         TrailsParamsRow::Slider<&TrailsNode::decay>("decay", params::Trails::decay_lo, params::Trails::decay_hi),
         TrailsParamsRow::Slider<&TrailsNode::zoom>("zoom", params::Trails::zoom_lo, params::Trails::zoom_hi),
         TrailsParamsRow::Slider<&TrailsNode::rotate>("rotate", params::Trails::rotate_lo, params::Trails::rotate_hi).Fmt("%.1f\xC2\xB0"),
         TrailsParamsRow::Slider<&TrailsNode::driftX>("drift x", params::Trails::drift_x_lo, params::Trails::drift_x_hi),
         TrailsParamsRow::Slider<&TrailsNode::driftY>("drift y", params::Trails::drift_y_lo, params::Trails::drift_y_hi),
         TrailsParamsRow::Slider<&TrailsNode::hueShift>("hue shift", params::Trails::hue_shift_lo, params::Trails::hue_shift_hi),
      };
   }

   void DrawTrailsParams(TrailsNode* n)
   {
      static const std::vector<std::string> kBlends = { "Max", "Add", "Screen" };
      DropdownButton("blend", kBlends, n->blendMode, [n](int i) { n->blendMode = i; });
      Ui::DrawParams(n, kTrailsParamsRows);
      if (ActionButton::Draw("Clear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }


   namespace
   {
      using OpticalFlowParamsRow = params::Row<OpticalFlowNode>;
      const OpticalFlowParamsRow kOpticalFlowParamsRows[] = {
         OpticalFlowParamsRow::Slider<&OpticalFlowNode::gain>("gain", params::OpticalFlow::gain_lo, params::OpticalFlow::gain_hi),
         OpticalFlowParamsRow::SliderInt<&OpticalFlowNode::levels>("levels", 1, OpticalFlowNode::kMaxLevels),
         OpticalFlowParamsRow::SliderInt<&OpticalFlowNode::window>("window", params::OpticalFlow::window_lo, params::OpticalFlow::window_hi),
         OpticalFlowParamsRow::SliderInt<&OpticalFlowNode::iterations>("iterations", params::OpticalFlow::iterations_lo, params::OpticalFlow::iterations_hi),
      };
   }

   void DrawOpticalFlowParams(OpticalFlowNode* n)
   {
      static const std::vector<std::string> kViews = { "Vectors", "Colour" };
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("view", kViews, n->view, [n](int i) { n->view = i; });
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      Ui::DrawParams(n, kOpticalFlowParamsRows);
   }


   namespace
   {
      using TimeMachineParamsRow = params::Row<TimeMachineNode>;
      const TimeMachineParamsRow kTimeMachineParamsRows[] = {
         TimeMachineParamsRow::SliderInt<&TimeMachineNode::length>("length", params::TimeMachine::length_lo, TimeMachineNode::kMaxLength),
         TimeMachineParamsRow::Toggle<&TimeMachineNode::invert>("invert"),
      };
   }

   void DrawTimeMachineParams(TimeMachineNode* n)
   {
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      Ui::DrawParams(n, kTimeMachineParamsRows);
   }


   namespace
   {
      using FluidParamsRow = params::Row<FluidNode>;
      const FluidParamsRow kFluidParamsRows[] = {
         FluidParamsRow::Choice<&FluidNode::stirrer>("stirrer", &FluidNode::StirrerNames),
         FluidParamsRow::Slider<&FluidNode::stirrerStrength>("stirrer strength", params::Fluid::stirrer_strength_lo, params::Fluid::stirrer_strength_hi),
         FluidParamsRow::Slider<&FluidNode::stirrerSize>("stirrer size", params::Fluid::stirrer_size_lo, params::Fluid::stirrer_size_hi),
         FluidParamsRow::Slider<&FluidNode::forceGain>("force gain", params::Fluid::force_gain_lo, params::Fluid::force_gain_hi),
         FluidParamsRow::Slider<&FluidNode::vorticity>("swirl", params::Fluid::vorticity_lo, params::Fluid::vorticity_hi),
         FluidParamsRow::Slider<&FluidNode::viscosity>("viscosity", params::Fluid::viscosity_lo, params::Fluid::viscosity_hi),
         FluidParamsRow::Slider<&FluidNode::velocityDamping>("damping", params::Fluid::velocity_damping_lo, params::Fluid::velocity_damping_hi),
         FluidParamsRow::Slider<&FluidNode::dyeFade>("dye fade", params::Fluid::dye_fade_lo, params::Fluid::dye_fade_hi),
         FluidParamsRow::Slider<&FluidNode::refresh>("image refresh", 0.0f, 1.0f),
         FluidParamsRow::SliderInt<&FluidNode::pressureIterations>("pressure iterations", 1, FluidNode::kMaxPressureIterations),
      };
   }

   void DrawFluidParams(FluidNode* n)
   {
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      Ui::DrawParam(n, FluidParamsRow::Choice<&FluidNode::view>("view", &FluidNode::ViewNames));
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      Ui::DrawParams(n, kFluidParamsRows);
      if (ActionButton::Draw("Reset", ImVec2(kPreviewSize, 0)))
         n->Reset();
   }


   namespace
   {
      using VolumeParamsRow = params::Row<VolumeNode>;
      const VolumeParamsRow kVolumeParamsRows1[] = {
         VolumeParamsRow::Choice<&VolumeNode::shape>("shape", &VolumeNode::ShapeNames),
         VolumeParamsRow::Slider<&VolumeNode::density>("density", params::Volume::density_lo, params::Volume::density_hi),
         VolumeParamsRow::Slider<&VolumeNode::size>("size", params::Volume::size_lo, params::Volume::size_hi),
         VolumeParamsRow::Slider<&VolumeNode::height>("height", params::Volume::height_lo, params::Volume::height_hi),
         VolumeParamsRow::Slider<&VolumeNode::centerX>("center x", params::Volume::center_x_lo, params::Volume::center_x_hi),
         VolumeParamsRow::Slider<&VolumeNode::centerY>("center y", params::Volume::center_y_lo, params::Volume::center_y_hi),
         VolumeParamsRow::Slider<&VolumeNode::centerZ>("center z", params::Volume::center_z_lo, params::Volume::center_z_hi),
         VolumeParamsRow::Slider<&VolumeNode::noiseScale>("noise scale", params::Volume::noise_scale_lo, params::Volume::noise_scale_hi),
         VolumeParamsRow::Slider<&VolumeNode::noiseAmount>("noise amount", 0.0f, 1.0f),
         VolumeParamsRow::Slider<&VolumeNode::seed>("seed", params::Volume::seed_lo, params::Volume::seed_hi).Fmt("%.0f"),
         VolumeParamsRow::Slider<&VolumeNode::windX>("wind x", params::Volume::wind_x_lo, params::Volume::wind_x_hi),
         VolumeParamsRow::Slider<&VolumeNode::windY>("wind y", params::Volume::wind_y_lo, params::Volume::wind_y_hi),
         VolumeParamsRow::Slider<&VolumeNode::windZ>("wind z", params::Volume::wind_z_lo, params::Volume::wind_z_hi),
         VolumeParamsRow::Slider<&VolumeNode::speed>("speed", params::Volume::speed_lo, params::Volume::speed_hi),
         VolumeParamsRow::Colour<&VolumeNode::color>("colour"),
      };
      const VolumeParamsRow kVolumeParamsRows2[] = {
         VolumeParamsRow::Colour<&VolumeNode::lightColor>("light colour"),
         VolumeParamsRow::Slider<&VolumeNode::lightAzimuth>("light azimuth", params::Volume::light_azimuth_lo, params::Volume::light_azimuth_hi).Fmt("%.0f"),
         VolumeParamsRow::Slider<&VolumeNode::lightElevation>("light elevation", params::Volume::light_elevation_lo, params::Volume::light_elevation_hi).Fmt("%.0f"),
         VolumeParamsRow::Slider<&VolumeNode::lightIntensity>("light", params::Volume::light_intensity_lo, params::Volume::light_intensity_hi),
         VolumeParamsRow::Slider<&VolumeNode::ambient>("ambient", params::Volume::ambient_lo, params::Volume::ambient_hi),
         VolumeParamsRow::Slider<&VolumeNode::scatter>("scatter", 0.0f, 1.0f),
         VolumeParamsRow::Slider<&VolumeNode::anisotropy>("anisotropy", params::Volume::anisotropy_lo, params::Volume::anisotropy_hi),
         VolumeParamsRow::SliderInt<&VolumeNode::lightSteps>("light steps", params::Volume::light_steps_lo, params::Volume::light_steps_hi),
         VolumeParamsRow::SliderInt<&VolumeNode::steps>("steps", params::Volume::steps_lo, params::Volume::steps_hi),
         VolumeParamsRow::SliderInt<&VolumeNode::renderSteps>("render steps", params::Volume::render_steps_lo, params::Volume::render_steps_hi),
         VolumeParamsRow::Slider<&VolumeNode::maxDistance>("max distance", params::Volume::max_distance_lo, params::Volume::max_distance_hi).Fmt("%.0f"),
      };
      const VolumeParamsRow kVolumeParamsRows3[] = {
         VolumeParamsRow::Slider<&VolumeNode::camAzimuth>("cam azimuth", params::Volume::cam_azimuth_lo, params::Volume::cam_azimuth_hi).Fmt("%.0f"),
         VolumeParamsRow::Slider<&VolumeNode::camElevation>("cam elevation", params::Volume::cam_elevation_lo, params::Volume::cam_elevation_hi).Fmt("%.0f"),
         VolumeParamsRow::Slider<&VolumeNode::camDistance>("cam distance", params::Volume::cam_distance_lo, params::Volume::cam_distance_hi),
         VolumeParamsRow::Slider<&VolumeNode::fov>("fov", params::Volume::fov_lo, params::Volume::fov_hi).Fmt("%.0f"),
      };
   }

   void DrawVolumeParams(VolumeNode* n)
   {
      Ui::DrawParams(n, kVolumeParamsRows1);
      // Lighting, quality and the own-camera rows sit behind "show more" (GraphNode::showAdvancedParams, saved
      // with the patch). They are still registered in the same order, so indices, modulation, expressions and
      // the matrix are unchanged; a folded row with a cable, expression or palette binding opens the section.
      {
         constexpr int kFoldedParams = 14; // light azimuth .. fov  // ui-audit-ok: model (param count)
         GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex);
         const bool savedRegisterOnly = gParamRegisterOnly;
         const bool pinned = gn != nullptr && gn->showAdvancedParams;
         bool inUse = PaletteBinding::Instance().IsBound(gCurrentNodeIndex, gColorCounter);
         for (int k = 0; k < kFoldedParams && !inUse; k++)
            inUse = Modulation::Instance().IsModulated(gCurrentNodeIndex, gParamCounter + k) ||
                    Modulation::Instance().HasExpression(gCurrentNodeIndex, gParamCounter + k);
         const bool open = pinned || inUse;
         ImGui::PushID("showMoreVolume");
         if (ActionButton::Column(inUse && !pinned ? "show more (in use)" : open ? "show less" : "show more",
                                  kParamWidth) &&
             gn != nullptr)
            gn->showAdvancedParams = !gn->showAdvancedParams;
         ImGui::PopID();
         gParamRegisterOnly = savedRegisterOnly || !open;
         Ui::DrawParams(n, kVolumeParamsRows2);
         if (open)
            ImGui::TextDisabled("own camera (no Render 3D input):");
         Ui::DrawParams(n, kVolumeParamsRows3);
         gParamRegisterOnly = savedRegisterOnly;
      }
   }


   namespace
   {
      using ReactionDiffusionParamsRow = params::Row<ReactionDiffusionNode>;
      const ReactionDiffusionParamsRow kReactionDiffusionParamsRows1[] = {
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::feed>("feed", params::ReactionDiffusion::feed_lo, params::ReactionDiffusion::feed_hi).Fmt("%.4f"),
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::kill>("kill", params::ReactionDiffusion::kill_lo, params::ReactionDiffusion::kill_hi).Fmt("%.4f"),
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::diffuseA>("diffuse A", params::ReactionDiffusion::diffuse_a_lo, params::ReactionDiffusion::diffuse_a_hi),
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::diffuseB>("diffuse B", params::ReactionDiffusion::diffuse_b_lo, params::ReactionDiffusion::diffuse_b_hi),
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::stepsPerFrame>("steps / frame", params::ReactionDiffusion::steps_per_frame_lo, params::ReactionDiffusion::steps_per_frame_hi).Fmt("%.0f"),
      };
      const ReactionDiffusionParamsRow kReactionDiffusionParamsRows2[] = {
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::sourceInfluence>("source influence", 0.0f, 1.0f),
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::width>("width", params::ReactionDiffusion::width_lo, params::ReactionDiffusion::width_hi).Fmt("%.0f"),
         ReactionDiffusionParamsRow::Slider<&ReactionDiffusionNode::height>("height", params::ReactionDiffusion::height_lo, params::ReactionDiffusion::height_hi).Fmt("%.0f"),
         ReactionDiffusionParamsRow::Colour<&ReactionDiffusionNode::lowColor>("low"),
         ReactionDiffusionParamsRow::Colour<&ReactionDiffusionNode::highColor>("high"),
      };
   }

   void DrawReactionDiffusionParams(ReactionDiffusionNode* n)
   {
      DropdownButton("preset", ReactionDiffusionNode::PresetNames(), n->preset,
                     [n](int i) { n->ApplyPreset(i); });
      Ui::DrawParams(n, kReactionDiffusionParamsRows1);
      ImGui::TextDisabled("with an input connected:");
      Ui::DrawParams(n, kReactionDiffusionParamsRows2);
      if (ActionButton::Draw("Reseed", ImVec2(kPreviewSize, 0)))
         n->Reseed();
   }


   // --- Palette from Image ---------------------------------------------
   // Preview is the palette itself, not the strip texture: the strip is a
   // by-product, while the swatches are what the rest of the graph binds to,
   // and they need to be readable and countable at a glance.
   // ui-content-begin: node preview: the node's output picture
   void DrawPalettePreview(PaletteNode* n)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = tok::param_palette_preview_h;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        tok::Role(tok::col::surface_deep), tok::radius_control);

      const int count = std::max(1, n->SwatchCount());
      const float pad = tok::space_1_5;
      const float chipW = (kPreviewSize - pad * tok::param_palette_preview_ratio) / (float)count;
      const float chipH = tok::param_chip_h;
      for (int i = 0; i < count; i++)
      {
         float rgb[3];
         n->GetSwatch(i, rgb);
         const ImVec2 tl(origin.x + pad + chipW * (float)i, origin.y + pad);
         const ImVec2 br(tl.x + chipW - tok::space_0_5, tl.y + chipH);
         dl->AddRectFilled(tl, br,
                           IM_COL32((int)(rgb[0] * tok::param_palette_preview_shade), (int)(rgb[1] * tok::param_palette_preview_shade),
                                    (int)(rgb[2] * tok::param_palette_preview_shade), tok::param_palette_preview_fill_alpha),
                           tok::param_palette_preview_radius);

         // How much of the reference each swatch actually covers. Without it a
         // one-percent accent looks exactly as important as the colour half the
         // photo is made of, which is the wrong thing to build a look on.
         const float weight = n->SwatchWeight(i);
         dl->AddRectFilled(ImVec2(tl.x, br.y + tok::space_1),
                           ImVec2(tl.x + (chipW - tok::space_0_5) * std::min(1.0f, weight), br.y + tok::param_palette_preview_gap),
                           tok::Role(tok::col::text_secondary));

         char idx[16];
         snprintf(idx, sizeof(idx), "%d", i + 1);
         dl->AddText(ImVec2(tl.x + tok::space_0_5, br.y + tok::param_text_y_sm), tok::Role(tok::col::edge_mid), idx);
      }

      const char* status;
      if (n->Input().IsConnected())
         status = n->live ? "live from cable" : "from cable";
      else if (!n->LoadedPath().empty())
         status = "from file";
      else
         status = "choose a photo or cable 'ref'";
      dl->AddText(ImVec2(origin.x + pad, origin.y + h - tok::param_text_y_lg),
                  tok::Role(tok::col::edge_mid), status);

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  tok::Role(tok::col::edge_lo_light), tok::radius_control);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
   }
   // ui-content-end


   namespace
   {
      using PaletteParamsRow = params::Row<PaletteNode>;
      const PaletteParamsRow kPaletteParamsRows1[] = {
         PaletteParamsRow::SliderInt<&PaletteNode::swatchCount>("swatches", params::Palette::swatch_count_lo, PaletteNode::kMaxSwatches),
         PaletteParamsRow::Choice<&PaletteNode::sortMode>("order", &PaletteNode::SortNames).Undo(),
      };
      const PaletteParamsRow kPaletteParamsRows2[] = {
         PaletteParamsRow::Slider<&PaletteNode::minChroma>("min chroma", params::Palette::min_chroma_lo, params::Palette::min_chroma_hi),
         PaletteParamsRow::Toggle<&PaletteNode::includeNeutrals>("include neutrals"),
         PaletteParamsRow::Slider<&PaletteNode::seed>("seed", params::Palette::seed_lo, params::Palette::seed_hi).Fmt("%.1f"),
         PaletteParamsRow::SliderInt<&PaletteNode::sampleSize>("sample", params::Palette::sample_size_lo, params::Palette::sample_size_hi),
         PaletteParamsRow::Toggle<&PaletteNode::live>("live"),
         // Drawn whether or not Live is on: hiding a modulatable slider behind a
         // checkbox renumbers every pin after it, which silently repoints this
         // node's modulation the moment the box is ticked.
         PaletteParamsRow::Slider<&PaletteNode::sampleRate>("rate", params::Palette::sample_rate_lo, params::Palette::sample_rate_hi).Fmt("%.0f"),
      };
      const PaletteParamsRow kPaletteParamsRows3[] = {
         PaletteParamsRow::Slider<&PaletteNode::hueShift>("hue", params::Palette::hue_shift_lo, params::Palette::hue_shift_hi),
         PaletteParamsRow::Slider<&PaletteNode::saturation>("saturation", params::Palette::saturation_lo, params::Palette::saturation_hi),
         PaletteParamsRow::Slider<&PaletteNode::brightness>("brightness", params::Palette::brightness_lo, params::Palette::brightness_hi),
         PaletteParamsRow::Slider<&PaletteNode::spread>("spread", params::Palette::spread_lo, params::Palette::spread_hi),
      };
   }

   void DrawPaletteParams(PaletteNode* n)
   {
      if (ActionButton::Draw("Choose reference...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::V4(tok::param_palette_shade), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }
      else if (!n->LoadedPath().empty())
      {
         const std::string file = AppPaths::FsPath(n->LoadedPath()).filename().u8string();
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         PopTextWrapWidth();
      }
      Ui::DrawParams(n, kPaletteParamsRows1);

      NodeSeparator("extract");
      Ui::DrawParams(n, kPaletteParamsRows2);
      if (ActionButton::Draw("Re-extract", ImVec2(kPreviewSize, 0)))
         n->RequestExtract();

      // Shaping runs on the stored cluster centres, so these are live: they
      // re-grade the palette without re-clustering the photo behind it.
      NodeSeparator("shape");
      Ui::DrawParams(n, kPaletteParamsRows3);

      NodeSeparator("strip output");
      Ui::DrawParam(n, PaletteParamsRow::Choice<&PaletteNode::stripMode>("blend", &PaletteNode::StripNames).Undo());
      ImGui::TextDisabled("out is a gradient of the palette");
   }


   namespace
   {
      using RampParamsRow = params::Row<RampNode>;
      const RampParamsRow kRampParamsRows[] = {
         RampParamsRow::Choice<&RampNode::type>("type", &RampNode::TypeNames),
         RampParamsRow::Choice<&RampNode::repeat>("repeat", &RampNode::RepeatNames),
         RampParamsRow::Slider<&RampNode::width>("width", params::Ramp::width_lo, params::Ramp::width_hi).Fmt("%.0f"),
         RampParamsRow::Slider<&RampNode::height>("height", params::Ramp::height_lo, params::Ramp::height_hi).Fmt("%.0f"),
         RampParamsRow::Slider<&RampNode::angle>("angle", params::Ramp::angle_lo, params::Ramp::angle_hi).Fmt("%.1f\xC2\xB0"),
         RampParamsRow::Slider<&RampNode::centerX>("center x", params::Ramp::center_x_lo, params::Ramp::center_x_hi),
         RampParamsRow::Slider<&RampNode::centerY>("center y", params::Ramp::center_y_lo, params::Ramp::center_y_hi),
         RampParamsRow::Slider<&RampNode::scale>("scale", params::Ramp::scale_lo, params::Ramp::scale_hi),
         RampParamsRow::Slider<&RampNode::offset>("offset", -1.0f, 1.0f),
         RampParamsRow::Slider<&RampNode::gamma>("gamma", params::Ramp::gamma_lo, params::Ramp::gamma_hi),
         RampParamsRow::Slider<&RampNode::dither>("dither", 0.0f, 1.0f),

         RampParamsRow::SliderInt<&RampNode::stopCount>("stops", params::Ramp::stop_count_lo, RampNode::kStops),
      };
   }

   void DrawRampParams(RampNode* n)
   {
      Ui::DrawParams(n, kRampParamsRows);
      for (int i = 0; i < n->stopCount; i++)
      {
         ImGui::PushID(i);
         char label[24];
         snprintf(label, sizeof(label), "stop %d", i + 1);
         ColorSwatch(label, n->stopColor[i], n);
         ModSlider("at", &n->stopPos[i], 0.0f, 1.0f);
         ImGui::PopID();
      }
   }


   // Gradient-tool-style stop editor: drag a marker to reposition, click empty
   // track to add a stop (seeded with the color already showing there), right
   // click a marker to remove it. Position isn't run through ModSlider like
   // RampNode's - dragging on the bar is the primary interaction, and it would
   // otherwise fight the drag for control of the same float every frame.
   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawColorRampEditor(ColorRampNode* n)
   {
      const float size = kPreviewSize;
      const float barH = tok::param_bar_h;
      const float trackH = tok::param_track_h;
      const float gap = tok::space_1;
      const float pad = tok::param_pad; // R12: stop handles (radius up to 7) stay inside the frame
      const float inner = size - tok::param_inner_ratio * pad;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // gradient preview strip
      const int kSegments = 96;
      for (int i = 0; i < kSegments; i++)
      {
         float t0 = (float)i / kSegments;
         float t1 = (float)(i + 1) / kSegments;
         float c0[3], c1[3];
         n->Evaluate(t0, c0);
         n->Evaluate(t1, c1);
         ImVec2 tl(origin.x + pad + t0 * inner, origin.y);
         ImVec2 br(origin.x + pad + t1 * inner + (i + 1 == kSegments ? 0.0f : 1.0f), origin.y + barH);
         ImU32 col0 = IM_COL32((int)(c0[0] * 255), (int)(c0[1] * 255), (int)(c0[2] * 255), tok::param_col0_alpha);
         ImU32 col1 = IM_COL32((int)(c1[0] * 255), (int)(c1[1] * 255), (int)(c1[2] * 255), tok::param_col0_alpha);
         dl->AddRectFilledMultiColor(tl, br, col0, col1, col1, col0);
      }
      dl->AddRect(ImVec2(origin.x + pad, origin.y), ImVec2(origin.x + size - pad, origin.y + barH), tok::Role(tok::col::edge_lo_light), tok::param_color_ramp_editor_radius);

      // stop track
      ImVec2 trackOrigin(origin.x, origin.y + barH + gap);
      ImVec2 trackBr(origin.x + size, trackOrigin.y + trackH);
      dl->AddRectFilled(trackOrigin, trackBr, tok::Role(tok::col::surface_deep), tok::param_color_ramp_editor_radius);
      dl->AddRect(trackOrigin, trackBr, tok::Role(tok::col::edge_lo_light), tok::param_color_ramp_editor_radius);

      ImGui::SetCursorScreenPos(trackOrigin);
      UiInteract::Hit("##colorramp", ImVec2(size, trackH));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toScreenX = [&](float x) { return trackOrigin.x + pad + x * inner; };
      auto toX = [&](float screenX) {
         return std::min(1.0f, std::max(0.0f, (screenX - trackOrigin.x - pad) / inner));
      };

      static ColorRampNode* sDragNode = nullptr;
      static int sDragIndex = -1;
      static ColorRampNode* sSelNode = nullptr;
      static int sSelIndex = -1;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 10.0f;
      for (int i = 0; i < n->stopCount; i++)
      {
         float d = std::fabs(toScreenX(n->stopPos[i]) - mouse.x);
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         if (nearest >= 0)
         {
            sDragIndex = nearest;
         }
         else
         {
            float x = toX(mouse.x);
            float seedColor[3];
            n->Evaluate(x, seedColor);
            sDragIndex = n->AddStop(x, seedColor);
         }
         sDragNode = n;
         sSelNode = n;
         sSelIndex = sDragIndex;
      }
      if (hovered && MenuParts::RightPressed() && nearest >= 0)
      {
         PushUndoCheckpoint();
         n->RemoveStop(nearest);
         if (sSelNode == n && sSelIndex == nearest)
            sSelIndex = -1;
      }
      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && nearest >= 0)
      {
         PushUndoCheckpoint();
         n->RemoveStop(nearest);
         if (sSelNode == n && sSelIndex == nearest)
            sSelIndex = -1;
         sDragIndex = -1;
         sDragNode = nullptr;
      }
      if (active && sDragNode == n && sDragIndex >= 0)
         n->MoveStop(sDragIndex, toX(mouse.x));
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragNode = nullptr;
      }
      if (sSelNode == n && sSelIndex >= n->stopCount)
         sSelIndex = -1;

      for (int i = 0; i < n->stopCount; i++)
      {
         float x = toScreenX(n->stopPos[i]);
         const bool isSel = (sSelNode == n && sSelIndex == i);
         ImU32 fill = IM_COL32((int)(n->stopColor[i][0] * 255), (int)(n->stopColor[i][1] * 255),
                               (int)(n->stopColor[i][2] * 255), tok::param_col0_alpha);
         float r = (i == nearest || isSel) ? tok::param_r : tok::param_color_ramp_editor_r;
         ImVec2 tip(x, trackOrigin.y + tok::space_0_5);
         dl->AddTriangleFilled(ImVec2(x - r, tip.y + r * tok::param_color_ramp_editor_ratio), ImVec2(x + r, tip.y + r * tok::param_color_ramp_editor_ratio), tip,
                               isSel ? tok::Role(tok::col::role_solo) : tok::Role(tok::col::surface_hi));
         ImVec2 chipTl(x - r * tok::param_chip_tl_scale, tip.y + r * tok::param_color_ramp_editor_ratio + 1.0f);
         dl->AddRectFilled(chipTl, ImVec2(chipTl.x + r * tok::param_fill_x, chipTl.y + tok::space_1_25), fill);
      }

      ImGui::SetCursorScreenPos(ImVec2(origin.x, trackBr.y + gap));

      // One row per stop, left-to-right by position along the ramp - not by
      // raw array index, since RemoveStop's compaction reassigns indices and
      // a raw-index label would relabel unrelated stops out from under the
      // user every time one is deleted.
      int order[ColorRampNode::kMaxStops];
      for (int i = 0; i < n->stopCount; i++)
         order[i] = i;
      for (int i = 0; i < n->stopCount; i++)
         for (int j = i + 1; j < n->stopCount; j++)
            if (n->stopPos[order[j]] < n->stopPos[order[i]])
               std::swap(order[i], order[j]);

      for (int rank = 0; rank < n->stopCount; rank++)
      {
         const int idx = order[rank];
         ImGui::PushID(idx + 20000);
         char label[32];
         snprintf(label, sizeof(label), "stop %d", rank + 1);
         ColorSwatch(label, n->stopColor[idx], n);
         ImGui::SameLine(size - tok::param_color_ramp_editor_gap);
         ImGui::BeginDisabled(n->stopCount <= 2);
         if (GlyphToggle::Draw("##delstop", IconsInfinite::Close, IconsInfinite::Close, false))
         {
            PushUndoCheckpoint();
            n->RemoveStop(idx);
            if (sSelNode == n && sSelIndex == idx)
               sSelIndex = -1;
         }
         ImGui::EndDisabled();
         ImGui::PopID();
      }
      n->MarkDirty();

      ImGui::BeginDisabled(n->stopCount >= ColorRampNode::kMaxStops);
      if (ActionButton::Draw("+ stop", ImVec2(size, 0)))
      {
         PushUndoCheckpoint();
         float x = n->stopCount > 0 ? std::min(1.0f, n->stopPos[order[n->stopCount - 1]] + tok::param_color_ramp_editor_max) : tok::param_x;
         float seedColor[3];
         n->Evaluate(x, seedColor);
         n->AddStop(x, seedColor);
      }
      ImGui::EndDisabled();
   }
   // ui-content-end


   namespace
   {
      using ColorRampParamsRow = params::Row<ColorRampNode>;
   }

   void DrawColorRampParams(ColorRampNode* n)
   {
      DrawColorRampEditor(n);
      DropdownButton("interpolation", ColorRampNode::InterpNames(), n->interpMode,
                     [n](int i) { PushUndoCheckpoint(); n->interpMode = i; n->MarkDirty(); });
      Ui::DrawParam(n, ColorRampParamsRow::Slider<&ColorRampNode::mix>("mix", 0.0f, 1.0f));
   }


   namespace
   {
      using ImageAnalyzeParamsRow = params::Row<ImageAnalyzeNode>;
      const ImageAnalyzeParamsRow kImageAnalyzeParamsRows1[] = {
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::probeU>("probe U", 0.0f, 1.0f),
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::probeV>("probe V", 0.0f, 1.0f),
      };
      const ImageAnalyzeParamsRow kImageAnalyzeParamsRows2[] = {
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::smoothing>("smoothing", params::ImageAnalyze::smoothing_lo, params::ImageAnalyze::smoothing_hi),
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::sampleRate>("samples / sec", params::ImageAnalyze::sample_rate_lo, params::ImageAnalyze::sample_rate_hi).Fmt("%.0f"),
         ImageAnalyzeParamsRow::SliderInt<&ImageAnalyzeNode::sampleSize>("sample res", params::ImageAnalyze::sample_size_lo, params::ImageAnalyze::sample_size_hi),
      };
      const ImageAnalyzeParamsRow kImageAnalyzeParamsRows3[] = {
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::gain>("gain", params::ImageAnalyze::gain_lo, params::ImageAnalyze::gain_hi),
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::offset>("offset", -1.0f, 1.0f),
         ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::power>("power", params::ImageAnalyze::power_lo, params::ImageAnalyze::power_hi),
         ImageAnalyzeParamsRow::Toggle<&ImageAnalyzeNode::invert>("invert"),
         ImageAnalyzeParamsRow::Toggle<&ImageAnalyzeNode::clamp01>("clamp 0..1"),
      };
   }

   void DrawImageAnalyzeParams(ImageAnalyzeNode* n)
   {
      const float r = n->RawR();
      const float g = n->RawG();
      const float b = n->RawB();
      const float res = n->Value(ImageAnalyzeNode::kResult);

      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = tok::param_gutter;

      // Color swatch + Result meter (header, full width)
      ImVec4 col(r, g, b, 1.0f);
      ImGui::ColorButton("##ImageAnalyzeSwatch", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(tok::param_image_analyze_params_color_button_w, tok::param_image_analyze_params_color_button_h));
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Dummy(ImVec2(0.0f, tok::param_spacer_h));
      ImGui::Text("Result: %s", UiFmt::F3(res).c);
      ImGui::EndGroup();

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("sample", colW);
      DropdownButton("sample mode", ImageAnalyzeNode::SampleModeNames(), n->sampleMode,
                     [n](int i) { PushUndoCheckpoint(); n->sampleMode = i; }, colW, /*showCaption=*/false);

      if (n->sampleMode == ImageAnalyzeNode::kPointProbe ||
          n->sampleMode == ImageAnalyzeNode::kBoxRegion ||
          n->sampleMode == ImageAnalyzeNode::kCenterWeighted)
      {
         Ui::DrawParams(n, kImageAnalyzeParamsRows1);
         if (n->sampleMode == ImageAnalyzeNode::kBoxRegion)
            Ui::DrawParam(n, ImageAnalyzeParamsRow::Slider<&ImageAnalyzeNode::probeRadius>("radius", params::ImageAnalyze::probe_radius_lo, params::ImageAnalyze::probe_radius_hi));
      }

      NodeSeparator("operation", colW);
      DropdownButton("operation", ImageAnalyzeNode::MathOpNames(), n->mathOp,
                     [n](int i) { PushUndoCheckpoint(); n->mathOp = i; }, colW, /*showCaption=*/false);

      if (n->mathOp == ImageAnalyzeNode::kCustomExpression)
      {
         char buf[256];
         snprintf(buf, sizeof(buf), "%s", n->customFormula.c_str());
         ImGui::SetNextItemWidth(colW);
         if (FieldWell::InputText("formula", buf, sizeof(buf)))
         {
            n->customFormula = buf;
         }
         if (!n->ExpressionError().empty())
         {
            ImGui::TextColored(tok::RoleV4(tok::col::text_err), "err: %s", n->ExpressionError().c_str());
         }
         else
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextDisabled("vars: r, g, b, a, lum, sat, hue, delta, t");
            PopTextWrapWidth();
         }
      }

      NodeSeparator("sampling", colW);
      Ui::DrawParams(n, kImageAnalyzeParamsRows2);

      ImGui::EndGroup();

      // --- Right Column ---
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("shaping", colW);
      Ui::DrawParams(n, kImageAnalyzeParamsRows3);

      ImGui::EndGroup();

      NodeSeparator("outputs", colW * tok::param_image_analyze_params_ratio + gutter);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   // The small live view every tracking node shows: the frame the tracker saw, with what it found drawn on top,
   // so it is clear what is being tracked (and that the image is reaching the node at all).
   // ui-content-begin: node preview: the node's output picture
   static void DrawTrackPreview(TrackNodeBase* n, float w)
   {
      const float aspect = std::max(0.5f, std::min(2.0f, n->PreviewAspect()));
      const float h = std::min(w / aspect, w * tok::param_track_preview_ratio);
      const float iw = std::min(w, h * aspect);
      const ImVec2 tl = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(w, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(tl.x + w, tl.y + h);
      dl->AddRectFilled(tl, br, ImGui::GetColorU32(ImGuiCol_FrameBg), tok::radius_control);
      const ImVec2 a(tl.x + (w - iw) * 0.5f, tl.y), b(a.x + iw, br.y);
      const unsigned int tex = n->PreviewTexture();
      if (tex == 0)
      {
         const char* msg = n->Input().IsConnected() ? "waiting for a frame" : "connect an image";
         const ImVec2 ts = ImGui::CalcTextSize(msg);
         dl->AddText(ImVec2(tl.x + (w - ts.x) * 0.5f, tl.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
         return;
      }
      const bool mir = n->mirror;
      dl->PushClipRect(tl, br, true);
      Thumb::Add(dl, (ImTextureID)(intptr_t)tex, a, b, ImVec2(mir ? 1.f : 0.f, 1.f), ImVec2(mir ? 0.f : 1.f, 0.f));
      std::vector<float> xy;
      n->GetOverlay(xy);
      const int np = (int)xy.size() / tok::param_np_ratio;
      if (np > 0)
      {
         auto pt = [&](int i) { return ImVec2(a.x + (mir ? 1.f - xy[i * 2] : xy[i * 2]) * iw, a.y + xy[i * 2 + 1] * h); };
         const ImU32 shadow = tok::Base(tok::col::ink_black, tok::param_track_preview_ink_black_alpha);
         const ImU32 ink = tok::U32(tok::action_go, CategoryColors::IsThemeLight());
         for (const auto& l : n->OverlayLines())
            if (l.first < np && l.second < np)
               dl->AddLine(pt(l.first), pt(l.second), shadow, tok::param_track_preview_stroke);
         for (const auto& l : n->OverlayLines())
            if (l.first < np && l.second < np)
               dl->AddLine(pt(l.first), pt(l.second), ink, tok::param_line_stroke_md);
         for (int i = 0; i < np; i++)
         {
            const bool key = n->OverlayKeyPoint(i);
            dl->AddCircleFilled(pt(i), key ? tok::param_track_preview_r : tok::param_track_preview_dot_r_md, shadow, tok::param_track_preview_segments);
            dl->AddCircleFilled(pt(i), key ? tok::param_track_preview_dot_r_lg : tok::param_track_preview_dot_r_sm, key ? ImGui::GetColorU32(ImGuiCol_Text) : ink, tok::param_track_preview_segments);
         }
      }
      dl->PopClipRect();
   }
   // ui-content-end

   namespace
   {
      using TrackParamsRow = params::Row<TrackNodeBase>;
      const TrackParamsRow kTrackParamsRows[] = {
         TrackParamsRow::Toggle<&TrackNodeBase::mirror>("mirror"),
         TrackParamsRow::Slider<&TrackNodeBase::smoothing>("smoothing", 0.0f, 1.0f).Fmt("%.2f"),
         TrackParamsRow::Slider<&TrackNodeBase::holdMs>("hold ms", params::param::hold_ms_lo, params::param::hold_ms_hi).Fmt("%.0f"),
      };
   }

   void DrawTrackParams(TrackNodeBase* n)
   {
      const float colW = kParamWidth;
      const float gutter = tok::param_gutter;
      if (n->PackMissing())
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", T("Install the Tracking pack: Settings > Extensions"));
      else
         ImGui::TextDisabled("%s", n->Status().c_str());

      NodeSeparator("tracking", colW * tok::param_track_params_ratio + gutter);
      ImGui::BeginGroup();
      DrawTrackPreview(n, colW);
      ImGui::EndGroup();
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();
      Ui::DrawParams(n, kTrackParamsRows);
      ImGui::EndGroup();

      NodeSeparator("outputs", colW * tok::param_track_params_ratio + gutter);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   void DrawBrainSourceBody(BrainSourceNode* n, int /*nodeIndex*/)
   {
      Bci::TelemetrySnapshot snap;
      n->GetTelemetry(snap);

      const float w = kPreviewSize;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float pillH = tok::param_pill_h;
      const float scopeH = tok::param_scope_h;
      const float totalH = pillH + tok::space_1 + scopeH;
      ImGui::Dummy(ImVec2(w, totalH));
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // 1. 4-Electrode Contact Quality Pill Bar (TP9 | AF7 | AF8 | TP10)
      static constexpr const char* kPillNames[4] = { "TP9", "AF7", "AF8", "TP10" };
      static constexpr int kChanMap[4] = { 2, 0, 1, 3 };
      const float pillGap = tok::space_1;
      const float pillW = (w - pillGap * tok::param_brain_source_body_ratio) * tok::param_pill_w_ratio;
      for (int p = 0; p < 4; ++p)
      {
         const float q = snap.contactQuality[kChanMap[p]];
         const ImVec2 p0(origin.x + p * (pillW + pillGap), origin.y);
         const ImVec2 p1(p0.x + pillW, p0.y + pillH);
         ImU32 fillCol = tok::Role(tok::col::state_ok_dark_vivid_hi);
         if (q < 0.40f)
            fillCol = tok::Role(tok::col::state_err);
         else if (q < 0.75f)
            fillCol = tok::Role(tok::col::role_modulation);
         dl->AddRectFilled(p0, p1, tok::Role(tok::col::ink_white, tok::param_brain_source_fill_alpha), tok::param_brain_source_body_radius);
         dl->AddRectFilled(p0, ImVec2(p0.x + pillW * std::clamp(q, tok::param_brain_source_lo, 1.0f), p1.y),
                           (fillCol & 0x00FFFFFFu) | tok::param_brain_source_body_alpha, tok::param_brain_source_body_radius);  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)
         const ImVec2 ts = ImGui::CalcTextSize(kPillNames[p]);
         dl->AddText(ImVec2(p0.x + (pillW - ts.x) * 0.5f, p0.y + (pillH - ts.y) * 0.5f),
                     ImGui::GetColorU32(ImGuiCol_Text), kPillNames[p]);
      }

      // 2. Dual-Layer Live Micro-Scope (Top: frontal/temporal traces; Bottom: 5 cortical bands)
      const ImVec2 sTl(origin.x, origin.y + pillH + tok::space_1);
      const ImVec2 sBr(origin.x + w, sTl.y + scopeH);
      AudioViz::Fill(dl, sTl, sBr);
      dl->PushClipRect(sTl, sBr, true);

      const float traceTop = sTl.y + tok::space_1;
      const float traceH = tok::param_trace_h;
      const float midY = traceTop + traceH * 0.5f;
      const ImU32 frontCol = tok::Role(tok::col::state_ok_dark_vivid_hi);
      const ImU32 tempCol = tok::Role(tok::col::role_modulation);
      for (int i = 1; i < Bci::kTraceSamples; ++i)
      {
         const float x0 = sTl.x + tok::space_1 + ((float)(i - 1) / (float)(Bci::kTraceSamples - 1)) * (w - tok::space_2);
         const float x1 = sTl.x + tok::space_1 + ((float)i / (float)(Bci::kTraceSamples - 1)) * (w - tok::space_2);
         const float y0f = midY - std::clamp(snap.traceFrontal[i - 1], -1.0f, 1.0f) * (traceH * tok::param_y0f_scale);
         const float y1f = midY - std::clamp(snap.traceFrontal[i], -1.0f, 1.0f) * (traceH * tok::param_y0f_scale);
         const float y0t = midY - std::clamp(snap.traceTemporal[i - 1], -1.0f, 1.0f) * (traceH * tok::param_y0f_scale);
         const float y1t = midY - std::clamp(snap.traceTemporal[i], -1.0f, 1.0f) * (traceH * tok::param_y0f_scale);
         dl->AddLine(ImVec2(x0, y0t), ImVec2(x1, y1t), (tempCol & 0x00FFFFFFu) | tok::param_line_alpha_lg_hex, tok::param_brain_source_body_stroke);  // ui-audit-ok: mask (channel mask / bit packing, not a colour value)
         dl->AddLine(ImVec2(x0, y0f), ImVec2(x1, y1f), frontCol, tok::param_line_stroke_sm);
      }

      // Flash badge when Blink / Wink / Jaw fires
      if (snap.blinkFlash > 0.15f || snap.jawLFlash > 0.15f || snap.jawRFlash > 0.15f || snap.winkFlash > 0.15f)
      {
         const char* tag = snap.blinkFlash > 0.15f ? "BLINK"
                         : (snap.jawLFlash > 0.15f ? "JAW L"
                         : (snap.jawRFlash > 0.15f ? "JAW R" : "WINK"));
         dl->AddText(ImVec2(sTl.x + tok::space_1_5, sTl.y + tok::space_0_75), tok::Role(tok::col::role_modulation), tag);
      }

      // Bottom 5 detrended cortical band bars (d, t, a, b, g) + 1/f slope readout
      const float barAreaTop = traceTop + traceH + tok::space_1;
      const float barAreaH = (sBr.y - tok::space_1) - barAreaTop;
      const float bw = (w - tok::param_brain_source_body_gap) / tok::param_bw_ratio;
      for (int b = 0; b < Bci::kBandCount; ++b)
      {
         const float p = std::clamp(snap.bandPower[b], 0.0f, 1.0f);
         const float bx0 = sTl.x + tok::space_1 + (float)b * bw;
         const float bx1 = bx0 + bw - 3.0f;
         const float by1 = sBr.y - tok::space_1;
         const float by0 = by1 - std::max(2.0f, p * barAreaH);
         dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1),
                           b == tok::param_fill_limit ? frontCol : tempCol, tok::param_fill_radius);
      }
      char slopeBuf[32];
      snprintf(slopeBuf, sizeof(slopeBuf), "1/f^%s", UiFmt::F1(snap.aperiodicSlope).c);
      dl->AddText(ImVec2(sBr.x - tok::param_text_x, barAreaTop + tok::space_1), ImGui::GetColorU32(ImGuiCol_TextDisabled), slopeBuf);
      dl->PopClipRect();
      AudioViz::Border(dl, sTl, sBr);

      // 3. Action row: Calibrate (30 s open, 30 s closed, 12 s blinks) + Rec Take + Synthetic Gesture triggers
      const float btnW = (w - tok::space_1) * 0.5f;
      if (ActionButton::Draw(snap.calibrating ? "Cancel Cal##bciCal" : "Calibrate##bciCal", ImVec2(btnW, 0)))
      {
         if (snap.calibrating) n->CancelCalibration();
         else n->StartCalibration();
      }
      ImGui::SameLine(0.0f, tok::space_1);
      const char* recLabel = n->IsRecordingTake() ? "Stop Take##bciRec" : (n->HasTake() ? "Re-Record##bciRec" : "Rec Take##bciRec");
      if (ActionButton::Draw(recLabel, ImVec2(btnW, 0)))
      {
         if (n->IsRecordingTake()) n->StopRecordingTake();
         else n->StartRecordingTake();
      }

      if (snap.calibrating)
      {
         const char* phaseStr = snap.calibPhase == Bci::CalibrationPhase::EyesOpen     ? "Eyes open"
                                : snap.calibPhase == Bci::CalibrationPhase::EyesClosed ? "Eyes closed"
                                                                                       : "Blink 5 times";
         ImGui::TextDisabled("%s: %s", phaseStr, UiFmt::PctOf(snap.calibProgress).c);
      }

      if (n->device == BrainSourceNode::kDeviceSynthetic)
      {
         const float gBtnW = (w - tok::space_3) * tok::param_btn_w_ratio;
         if (ActionButton::Draw("Blink##bciB", ImVec2(gBtnW, 0))) n->TriggerSyntheticGesture(Bci::GestureKind::Blink);
         ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw("Wink L##bciWL", ImVec2(gBtnW, 0))) n->TriggerSyntheticGesture(Bci::GestureKind::WinkLeft);
         ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw("Jaw L##bciJL", ImVec2(gBtnW, 0))) n->TriggerSyntheticGesture(Bci::GestureKind::JawLeft);
         ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw("Jaw R##bciJR", ImVec2(gBtnW, 0))) n->TriggerSyntheticGesture(Bci::GestureKind::JawRight);
      }
   }


   namespace
   {
      using BrainSourceParamsRow = params::Row<BrainSourceNode>;
      const BrainSourceParamsRow kBrainSourceParamsRows[] = {
         BrainSourceParamsRow::Choice<&BrainSourceNode::syntheticPreset>("synthetic preset", &BrainSourceNode::PresetNames).Undo(),
         BrainSourceParamsRow::Choice<&BrainSourceNode::blinkMode>("blink filter", &BrainSourceNode::BlinkModeNames).Undo(),
         BrainSourceParamsRow::Choice<&BrainSourceNode::imageMode>("image", &BrainSourceNode::ImageModeNames).Undo(),
         BrainSourceParamsRow::Slider<&BrainSourceNode::smoothing>("smoothing", 0.0f, 1.0f).Fmt("%.2f"),
         BrainSourceParamsRow::Slider<&BrainSourceNode::holdMs>("hold ms", params::BrainSource::hold_ms_lo, params::BrainSource::hold_ms_hi).Fmt("%.0f"),
         BrainSourceParamsRow::Toggle<&BrainSourceNode::artifactBlanking>("artifact blanking"),
         BrainSourceParamsRow::Toggle<&BrainSourceNode::startleGuard>("startle guard"),
      };
   }

   void DrawBrainSourceParams(BrainSourceNode* n)
   {
      const float colW = kParamWidth;
      const float gutter = tok::param_gutter;
      if (n->PackMissing())
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", T("Install the BCI pack: Settings > Extensions (synthetic fallback active)"));
      else
         ImGui::TextDisabled("%s", n->Status().c_str());

      NodeSeparator("acquisition", colW * tok::param_brain_source_params_ratio + gutter);
      Ui::DrawParam(n, BrainSourceParamsRow::Choice<&BrainSourceNode::device>("device", &BrainSourceNode::DeviceNames).Undo());
      if (n->device == BrainSourceNode::kDeviceCsvReplay)
      {
         char pathBuf[512];
         snprintf(pathBuf, sizeof(pathBuf), "%s", n->csvPath.c_str());
         ImGui::SetNextItemWidth(colW * tok::param_field_w + gutter);
         if (FieldWell::InputTextWithHint("csv file", "/path/to/heroic_muse2.csv", pathBuf, sizeof(pathBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue))
         {
            PushUndoCheckpoint();
            n->csvPath = pathBuf;
         }
         else if (ImGui::IsItemDeactivatedAfterEdit())
         {
            PushUndoCheckpoint();
            n->csvPath = pathBuf;
         }
      }
      Ui::DrawParams(n, kBrainSourceParamsRows);
      ImGui::BeginDisabled(!n->HasTake() && !gParamRegisterOnly);
      Ui::DrawParam(n, BrainSourceParamsRow::Toggle<&BrainSourceNode::playTake>("play baked take"));
      ImGui::EndDisabled();

      NodeSeparator("24 neural & somatic outputs", colW * tok::param_brain_source_params_ratio + gutter);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   namespace
   {
      using NullModulatorParamsRow = params::Row<NullModulatorNode>;
   }

   void DrawNullModulatorParams(NullModulatorNode* n)
   {
      if (!n->input)
      {
         Ui::DrawParam(n, NullModulatorParamsRow::Slider<&NullModulatorNode::constantIn>("constant", 0.0f, 1.0f));
      }
      else
      {
         ImGui::TextDisabled("driven by input modulator");
      }
   }
}

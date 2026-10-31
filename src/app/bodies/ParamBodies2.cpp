// Generic node parameter bodies, part 2: curves, palettes, ramps, analyze (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/AppShared.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/GlyphToggle.h"
#include "app/ui/design/Glyphs.gen.h"

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
   void DrawCurveEditor(CurveShape& shape, ImU32 lineCol, bool resetOnEmptyRightClick = false,
                        bool crosshair = false, float liveX = -1.0f)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::PushID(&shape);
      ImGui::InvisibleButton("##curve", ImVec2(size, size));
      ImGui::PopID();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      // R12: plotted values and handles live in an inset box so nothing touches or crosses the frame.
      const float pad = 6.0f;
      const float inner = size - 2.0f * pad;
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
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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
      dl->AddLine(toScreen(0.0f, 0.0f), toScreen(1.0f, 1.0f), ScopeMidLineCol()); // identity reference
      if (crosshair)
      {
         ImVec2 mid = toScreen(0.5f, 0.5f);
         dl->AddLine(ImVec2(mid.x, origin.y), ImVec2(mid.x, br.y), isLight ? tok::U32(tok::pal::c_646E82C8) : tok::U32(tok::pal::c_787C8CC8));
         dl->AddLine(ImVec2(origin.x, mid.y), ImVec2(br.x, mid.y), isLight ? tok::U32(tok::pal::c_646E82C8) : tok::U32(tok::pal::c_787C8CC8));
      }

      const int kSegments = 64;
      for (int i = 1; i <= kSegments; i++)
      {
         float x0 = (float)(i - 1) / kSegments;
         float x1 = (float)i / kSegments;
         dl->AddLine(toScreen(x0, shape.Evaluate(x0)),
                     toScreen(x1, shape.Evaluate(x1)), lineCol, 1.8f);
      }
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearest ? 6.0f : 4.5f, lineCol);
         dl->AddCircle(sp, i == nearest ? 6.0f : 4.5f, isLight ? tok::U32(tok::pal::c_F0F2F8FF) : tok::U32(tok::pal::c_12121AFF), 0, 1.5f);
      }
      if (liveX >= 0.0f)
      {
         float clampedX = std::min(1.0f, std::max(0.0f, liveX));
         ImVec2 dot = toScreen(clampedX, shape.Evaluate(clampedX));
         dl->AddCircleFilled(dot, 5.0f, isLight ? tok::U32(tok::pal::c_DC8214FF) : tok::U32(tok::pal::c_FFC83CFF));
         dl->AddCircle(dot, 5.0f, isLight ? tok::U32(tok::pal::c_F0F2F8FF) : tok::U32(tok::pal::c_12121AFF), 0, 1.5f);
      }
      AudioViz::Border(dl, origin, br);
   }


   void DrawCurvesParams(CurvesNode* n)
   {
      DropdownButton("channel", CurvesNode::ChannelNames(), n->activeChannel,
                     [n](int i) { n->activeChannel = i; });
      const bool isLight = IsThemeLight();
      ImU32 lineCol = isLight ? tok::U32(tok::pal::c_1E283CFF) : tok::U32(tok::pal::c_E6EBFAFF);
      if (n->activeChannel == CurvesNode::kRed)   lineCol = isLight ? tok::U32(tok::pal::c_DC2828FF) : tok::U32(tok::pal::c_FF6E6EFF);
      if (n->activeChannel == CurvesNode::kGreen) lineCol = isLight ? tok::U32(tok::pal::c_19A03CFF) : tok::U32(tok::pal::c_78E682FF);
      if (n->activeChannel == CurvesNode::kBlue)  lineCol = isLight ? tok::U32(tok::pal::c_1E64E6FF) : tok::U32(tok::pal::c_78AAFFFF);
      DrawCurveEditor(n->Shape(n->activeChannel), lineCol);
      if (ActionButton::Draw("Reset channel", ImVec2(kPreviewSize, 0)))
         n->ResetChannel(n->activeChannel);
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
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
      if (ActionButton::Draw(learnLabel, ImVec2(hasLearned && !learning ? 95 : 120, 0)))
      {
         PushUndoCheckpoint();
         n->SetLearning(!learning);
      }
      if (hasLearned && !learning)
      {
         FormParts::Inline();
         if (ActionButton::Draw("Reset", ImVec2(50, 0)))
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
         snprintf(confText, sizeof(confText), "learning - %d%%", (int)std::round(badgeVal * 100.0f));
      else
         snprintf(confText, sizeof(confText), "%d%% conf", (int)std::round(badgeVal * 100.0f));
      ImGui::TextColored(badgeVal > 0.6f ? (isLight ? tok::V4(tok::palf::v_100_600_200_1000) : tok::V4(tok::palf::v_200_850_350_1000))
                                    : (isLight ? tok::V4(tok::palf::v_700_400_100_1000) : tok::V4(tok::palf::v_900_700_200_1000)),
                         "%s", confText);

      PushCheckboxStyle();
      ModCheckbox("self normalize", &n->selfNormalize);
      PopCheckboxStyle();

      ModSlider("wander", &n->wander, 0.0f, 1.0f);
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
      ImGui::PopID();
   }


   void DrawModCurveParams(ModCurveNode* n)
   {
      const bool isLight = IsThemeLight();
      const float in = n->input ? n->input->Value01() : n->constantIn;
      DrawCurveEditor(n->curve, isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_E6EBFAFF), /*resetOnEmptyRightClick=*/true,
                     /*crosshair=*/true, /*liveX=*/in);
      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }


   // The MSEG's shape: drag a point, click empty space to add one, right-click
   // a point to remove it (empty space: reset). The small ring on each segment
   // bends it; drag it up or down. Loop on shades the looped part, and the
   // vertical line is where the shape is now.
   static void DrawMSEGEditor(MSEGNode* n, ImU32 lineCol)
   {
      const float width = kPreviewSize, height = 120.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::PushID(n);
      ImGui::InvisibleButton("##mseg", ImVec2(width, height));
      ImGui::PopID();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      const float pad = 6.0f;
      const float innerW = width - 2.0f * pad, innerH = height - 2.0f * pad;
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
            if (pts[i + 1].x - pts[i].x > 0.02f && dist(bendHandle(i)) < best)
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
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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
            pts[sDragIndex].curve = std::clamp(sDragStartCurve + rising * (sDragStartY - mouse.y) / 50.0f, -1.0f, 1.0f);
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
      const ImU32 ring = isLight ? tok::U32(tok::pal::c_F0F2F8FF) : tok::U32(tok::pal::c_12121AFF);
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
         dl->AddRectFilled(ImVec2(toScreen(a, 0).x, origin.y), ImVec2(toScreen(b, 0).x, br.y), (lineCol & 0x00FFFFFF) | 0x1E000000);
         dl->AddLine(ImVec2(toScreen(a, 0).x, origin.y), ImVec2(toScreen(a, 0).x, br.y), ScopeMidLineCol());
         dl->AddLine(ImVec2(toScreen(b, 0).x, origin.y), ImVec2(toScreen(b, 0).x, br.y), ScopeMidLineCol());
      }
      const int kSteps = 96;
      for (int i = 1; i <= kSteps; i++)
      {
         const float x0 = (float)(i - 1) / kSteps, x1 = (float)i / kSteps;
         dl->AddLine(toScreen(x0, n->Evaluate(x0)), toScreen(x1, n->Evaluate(x1)), lineCol, 1.8f);
      }
      for (int i = 0; i + 1 < (int)pts.size(); i++)
         if (pts[i + 1].x - pts[i].x > 0.02f)
            dl->AddCircle(bendHandle(i), i == nearBend ? 4.5f : 3.0f, lineCol, 0, 1.2f);
      for (int i = 0; i < (int)pts.size(); i++)
      {
         const ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearPoint ? 6.0f : 4.5f, lineCol);
         dl->AddCircle(sp, i == nearPoint ? 6.0f : 4.5f, ring, 0, 1.5f);
      }
      const float px = n->Position();
      const ImU32 liveCol = isLight ? tok::U32(tok::pal::c_DC8214FF) : tok::U32(tok::pal::c_FFC83CFF);
      dl->AddLine(ImVec2(toScreen(px, 0).x, origin.y), ImVec2(toScreen(px, 0).x, br.y), (liveCol & 0x00FFFFFF) | 0x80000000);
      dl->AddCircleFilled(toScreen(px, n->Evaluate(px)), 4.0f, liveCol);
      dl->AddCircle(toScreen(px, n->Evaluate(px)), 4.0f, ring, 0, 1.5f);
      AudioViz::Border(dl, origin, br);
   }


   void DrawMSEGParams(MSEGNode* n)
   {
      const bool isLight = IsThemeLight();
      DrawMSEGEditor(n, isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_E6EBFAFF));
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 3);
      PushCheckboxStyle();
      ModCheckbox("loop", &n->loop);
      PopCheckboxStyle();
      // Drawn either way so the two keep their slots; they do nothing with loop off.
      ImGui::BeginDisabled(!n->loop);
      ModSlider("loop start", &n->loopStart, 0.0f, 1.0f);
      ModSlider("loop end", &n->loopEnd, 0.0f, 1.0f);
      ImGui::EndDisabled();
      DrawPerVoiceCheckbox(&n->voice);
   }


   void DrawRemoveBgParams(RemoveBgNode* n)
   {
      DropdownButton("detect", RemoveBgNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      DropdownButton("output", RemoveBgNode::OutputModeNames(), n->outputMode,
                     [n](int i) { n->outputMode = i; });

      if (ActionButton::Draw("Remove Background", ImVec2(kPreviewSize, 0)))
         n->RequestMask();

      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("engine: %s", Platform::MattingBackend().c_str());
      PopTextWrapWidth();

      ModSlider("feather", &n->feather, 0.0f, 4.0f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);
      ModSlider("edge contrast", &n->contrast, 0.5f, 8.0f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);

      ModCheckbox("auto refresh (video)", &n->autoRefresh);
      if (n->autoRefresh)
      {
         ModSlider("every N frames", &n->refreshFrames, 1.0f, 120.0f, "%.0f");
      }
   }


   void DrawFeedbackParams(FeedbackNode*)
   {
   }


   void DrawTrailsParams(TrailsNode* n)
   {
      static const std::vector<std::string> kBlends = { "Max", "Add", "Screen" };
      DropdownButton("blend", kBlends, n->blendMode, [n](int i) { n->blendMode = i; });
      ModSlider("decay", &n->decay, 0.5f, 0.999f);
      ModSlider("zoom", &n->zoom, 0.9f, 1.1f);
      ModSlider("rotate", &n->rotate, -5.7296f, 5.7296f, "%.1f\xC2\xB0");
      ModSlider("drift x", &n->driftX, -0.02f, 0.02f);
      ModSlider("drift y", &n->driftY, -0.02f, 0.02f);
      ModSlider("hue shift", &n->hueShift, -0.05f, 0.05f);
      if (ActionButton::Draw("Clear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }


   void DrawOpticalFlowParams(OpticalFlowNode* n)
   {
      static const std::vector<std::string> kViews = { "Vectors", "Colour" };
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("view", kViews, n->view, [n](int i) { n->view = i; });
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      ModSlider("gain", &n->gain, 0.0f, 8.0f);
      ModSliderInt("levels", &n->levels, 1, OpticalFlowNode::kMaxLevels);
      ModSliderInt("window", &n->window, 1, 6);
      ModSliderInt("iterations", &n->iterations, 1, 6);
   }


   void DrawTimeMachineParams(TimeMachineNode* n)
   {
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      ModSliderInt("length", &n->length, 2, TimeMachineNode::kMaxLength);
      ModCheckbox("invert", &n->invert);
   }


   void DrawFluidParams(FluidNode* n)
   {
      static const std::vector<std::string> kRes = { "Full", "Half", "Quarter" };
      DropdownButton("view", FluidNode::ViewNames(), n->view, [n](int i) { n->view = i; });
      DropdownButton("resolution", kRes, n->resolution, [n](int i) { n->resolution = i; });
      DropdownButton("stirrer", FluidNode::StirrerNames(), n->stirrer, [n](int i) { n->stirrer = i; });
      ModSlider("stirrer strength", &n->stirrerStrength, 0.0f, 4.0f);
      ModSlider("stirrer size", &n->stirrerSize, 0.01f, 0.3f, "%.3f");
      ModSlider("force gain", &n->forceGain, 0.0f, 8.0f);
      ModSlider("swirl", &n->vorticity, 0.0f, 2.0f);
      ModSlider("viscosity", &n->viscosity, 0.0f, 0.2f, "%.3f");
      ModSlider("damping", &n->velocityDamping, 0.0f, 0.1f, "%.3f");
      ModSlider("dye fade", &n->dyeFade, 0.0f, 0.1f, "%.3f");
      ModSlider("image refresh", &n->refresh, 0.0f, 1.0f, "%.3f");
      ModSliderInt("pressure iterations", &n->pressureIterations, 1, FluidNode::kMaxPressureIterations);
      if (ActionButton::Draw("Reset", ImVec2(kPreviewSize, 0)))
         n->Reset();
   }


   void DrawVolumeParams(VolumeNode* n)
   {
      DropdownButton("shape", VolumeNode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      ModSlider("density", &n->density, 0.0f, 8.0f);
      ModSlider("size", &n->size, 0.05f, 6.0f);
      ModSlider("height", &n->height, 0.1f, 10.0f);
      ModSlider("center x", &n->centerX, -6.0f, 6.0f);
      ModSlider("center y", &n->centerY, -6.0f, 6.0f);
      ModSlider("center z", &n->centerZ, -6.0f, 6.0f);
      ModSlider("noise scale", &n->noiseScale, 0.1f, 6.0f);
      ModSlider("noise amount", &n->noiseAmount, 0.0f, 1.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f");
      ModSlider("wind x", &n->windX, -2.0f, 2.0f);
      ModSlider("wind y", &n->windY, -2.0f, 2.0f);
      ModSlider("wind z", &n->windZ, -2.0f, 2.0f);
      ModSlider("speed", &n->speed, 0.0f, 4.0f);
      ColorSwatch("colour", n->color, n);
      // Lighting, quality and the own-camera rows sit behind "show more" (GraphNode::showAdvancedParams, saved
      // with the patch). They are still registered in the same order, so indices, modulation, expressions and
      // the matrix are unchanged; a folded row with a cable, expression or palette binding opens the section.
      {
         constexpr int kFoldedParams = 14; // light azimuth .. fov
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
         ColorSwatch("light colour", n->lightColor, n);
         ModSlider("light azimuth", &n->lightAzimuth, -180.0f, 180.0f, "%.0f");
         ModSlider("light elevation", &n->lightElevation, -90.0f, 90.0f, "%.0f");
         ModSlider("light", &n->lightIntensity, 0.0f, 4.0f);
         ModSlider("ambient", &n->ambient, 0.0f, 2.0f);
         ModSlider("scatter", &n->scatter, 0.0f, 1.0f);
         ModSlider("anisotropy", &n->anisotropy, -0.9f, 0.9f);
         ModSliderInt("light steps", &n->lightSteps, 0, 16);
         ModSliderInt("steps", &n->steps, 8, 256);
         ModSliderInt("render steps", &n->renderSteps, 16, 512);
         ModSlider("max distance", &n->maxDistance, 1.0f, 100.0f, "%.0f");
         if (open)
            ImGui::TextDisabled("own camera (no Render 3D input):");
         ModSlider("cam azimuth", &n->camAzimuth, -180.0f, 180.0f, "%.0f");
         ModSlider("cam elevation", &n->camElevation, -89.0f, 89.0f, "%.0f");
         ModSlider("cam distance", &n->camDistance, 0.5f, 30.0f);
         ModSlider("fov", &n->fov, 10.0f, 120.0f, "%.0f");
         gParamRegisterOnly = savedRegisterOnly;
      }
   }


   void DrawReactionDiffusionParams(ReactionDiffusionNode* n)
   {
      DropdownButton("preset", ReactionDiffusionNode::PresetNames(), n->preset,
                     [n](int i) { n->ApplyPreset(i); });
      ModSlider("feed", &n->feed, 0.01f, 0.09f, "%.4f");
      ModSlider("kill", &n->kill, 0.03f, 0.08f, "%.4f");
      ModSlider("diffuse A", &n->diffuseA, 0.2f, 1.5f);
      ModSlider("diffuse B", &n->diffuseB, 0.1f, 1.0f);
      ModSlider("steps / frame", &n->stepsPerFrame, 1.0f, 32.0f, "%.0f");
      ImGui::TextDisabled("with an input connected:");
      ModSlider("source influence", &n->sourceInfluence, 0.0f, 1.0f);
      ModSlider("width", &n->width, 64.0f, 2048.0f, "%.0f");
      ModSlider("height", &n->height, 64.0f, 2048.0f, "%.0f");
      ColorSwatch("low", n->lowColor, n);
      ColorSwatch("high", n->highColor, n);
      if (ActionButton::Draw("Reseed", ImVec2(kPreviewSize, 0)))
         n->Reseed();
   }


   // --- Palette from Image ---------------------------------------------
   // Preview is the palette itself, not the strip texture: the strip is a
   // by-product, while the swatches are what the rest of the graph binds to,
   // and they need to be readable and countable at a glance.
   void DrawPalettePreview(PaletteNode* n)
   {
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 118.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        tok::U32(tok::pal::c_121218FF), 4.0f);

      const int count = std::max(1, n->SwatchCount());
      const float pad = 6.0f;
      const float chipW = (kPreviewSize - pad * 2.0f) / (float)count;
      const float chipH = 60.0f;
      for (int i = 0; i < count; i++)
      {
         float rgb[3];
         n->GetSwatch(i, rgb);
         const ImVec2 tl(origin.x + pad + chipW * (float)i, origin.y + pad);
         const ImVec2 br(tl.x + chipW - 2.0f, tl.y + chipH);
         dl->AddRectFilled(tl, br,
                           IM_COL32((int)(rgb[0] * 255.0f), (int)(rgb[1] * 255.0f),
                                    (int)(rgb[2] * 255.0f), 255),
                           3.0f);

         // How much of the reference each swatch actually covers. Without it a
         // one-percent accent looks exactly as important as the colour half the
         // photo is made of, which is the wrong thing to build a look on.
         const float weight = n->SwatchWeight(i);
         dl->AddRectFilled(ImVec2(tl.x, br.y + 4.0f),
                           ImVec2(tl.x + (chipW - 2.0f) * std::min(1.0f, weight), br.y + 7.0f),
                           tok::U32(tok::pal::c_969CB0FF));

         char idx[8];
         snprintf(idx, sizeof(idx), "%d", i + 1);
         dl->AddText(ImVec2(tl.x + 2.0f, br.y + 9.0f), tok::U32(tok::pal::c_767C90FF), idx);
      }

      const char* status;
      if (n->Input().IsConnected())
         status = n->live ? "live from cable" : "from cable";
      else if (!n->LoadedPath().empty())
         status = "from file";
      else
         status = "choose a photo or cable 'ref'";
      dl->AddText(ImVec2(origin.x + pad, origin.y + h - 18.0f),
                  tok::U32(tok::pal::c_7E8498FF), status);

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  tok::U32(tok::pal::c_464A5AFF), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
   }


   void DrawPaletteParams(PaletteNode* n)
   {
      if (ActionButton::Draw("Choose reference...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         PopTextWrapWidth();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         const size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         PopTextWrapWidth();
      }
      ModSliderInt("swatches", &n->swatchCount, 2, PaletteNode::kMaxSwatches);
      DropdownButton("order", PaletteNode::SortNames(), n->sortMode,
                     [n](int i) { PushUndoCheckpoint(); n->sortMode = i; });

      NodeSeparator("extract");
      ModSlider("min chroma", &n->minChroma, 0.0f, 0.2f);
      ModCheckbox("include neutrals", &n->includeNeutrals);
      ModSlider("seed", &n->seed, 0.0f, 64.0f, "%.1f");
      ModSliderInt("sample", &n->sampleSize, 16, 256);
      ModCheckbox("live", &n->live);
      // Drawn whether or not Live is on: hiding a modulatable slider behind a
      // checkbox renumbers every pin after it, which silently repoints this
      // node's modulation the moment the box is ticked.
      ModSlider("rate", &n->sampleRate, 1.0f, 60.0f, "%.0f");
      if (ActionButton::Draw("Re-extract", ImVec2(kPreviewSize, 0)))
         n->RequestExtract();

      // Shaping runs on the stored cluster centres, so these are live: they
      // re-grade the palette without re-clustering the photo behind it.
      NodeSeparator("shape");
      ModSlider("hue", &n->hueShift, -0.5f, 0.5f);
      ModSlider("saturation", &n->saturation, 0.0f, 2.0f);
      ModSlider("brightness", &n->brightness, -0.4f, 0.4f);
      ModSlider("spread", &n->spread, 0.0f, 2.0f);

      NodeSeparator("strip output");
      DropdownButton("blend", PaletteNode::StripNames(), n->stripMode,
                     [n](int i) { PushUndoCheckpoint(); n->stripMode = i; });
      ImGui::TextDisabled("out is a gradient of the palette");
   }


   void DrawRampParams(RampNode* n)
   {
      DropdownButton("type", RampNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      DropdownButton("repeat", RampNode::RepeatNames(), n->repeat, [n](int i) { n->repeat = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("angle", &n->angle, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("center x", &n->centerX, -0.5f, 1.5f);
      ModSlider("center y", &n->centerY, -0.5f, 1.5f);
      ModSlider("scale", &n->scale, 0.05f, 8.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ModSlider("gamma", &n->gamma, 0.1f, 4.0f);
      ModSlider("dither", &n->dither, 0.0f, 1.0f);

      ModSliderInt("stops", &n->stopCount, 2, RampNode::kStops);
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
   void DrawColorRampEditor(ColorRampNode* n)
   {
      const float size = kPreviewSize;
      const float barH = 26.0f;
      const float trackH = 22.0f;
      const float gap = 4.0f;
      const float pad = 7.0f; // R12: stop handles (radius up to 7) stay inside the frame
      const float inner = size - 2.0f * pad;

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
         ImU32 col0 = IM_COL32((int)(c0[0] * 255), (int)(c0[1] * 255), (int)(c0[2] * 255), 255);
         ImU32 col1 = IM_COL32((int)(c1[0] * 255), (int)(c1[1] * 255), (int)(c1[2] * 255), 255);
         dl->AddRectFilledMultiColor(tl, br, col0, col1, col1, col0);
      }
      dl->AddRect(ImVec2(origin.x + pad, origin.y), ImVec2(origin.x + size - pad, origin.y + barH), tok::U32(tok::pal::c_464A5AFF), 3.0f);

      // stop track
      ImVec2 trackOrigin(origin.x, origin.y + barH + gap);
      ImVec2 trackBr(origin.x + size, trackOrigin.y + trackH);
      dl->AddRectFilled(trackOrigin, trackBr, tok::U32(tok::pal::c_101016FF), 3.0f);
      dl->AddRect(trackOrigin, trackBr, tok::U32(tok::pal::c_464A5AFF), 3.0f);

      ImGui::SetCursorScreenPos(trackOrigin);
      ImGui::InvisibleButton("##colorramp", ImVec2(size, trackH));
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
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearest >= 0)
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
                               (int)(n->stopColor[i][2] * 255), 255);
         float r = (i == nearest || isSel) ? 7.0f : 5.5f;
         ImVec2 tip(x, trackOrigin.y + 2.0f);
         dl->AddTriangleFilled(ImVec2(x - r, tip.y + r * 1.6f), ImVec2(x + r, tip.y + r * 1.6f), tip,
                               isSel ? tok::U32(tok::pal::c_FFDC78FF) : tok::U32(tok::pal::c_DCE0ECFF));
         ImVec2 chipTl(x - r * 0.6f, tip.y + r * 1.6f + 1.0f);
         dl->AddRectFilled(chipTl, ImVec2(chipTl.x + r * 1.2f, chipTl.y + 5.0f), fill);
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
         char label[16];
         snprintf(label, sizeof(label), "stop %d", rank + 1);
         ColorSwatch(label, n->stopColor[idx], n);
         ImGui::SameLine(size - 18.0f);
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
         float x = n->stopCount > 0 ? std::min(1.0f, n->stopPos[order[n->stopCount - 1]] + 0.1f) : 0.5f;
         float seedColor[3];
         n->Evaluate(x, seedColor);
         n->AddStop(x, seedColor);
      }
      ImGui::EndDisabled();
   }


   void DrawColorRampParams(ColorRampNode* n)
   {
      DrawColorRampEditor(n);
      DropdownButton("interpolation", ColorRampNode::InterpNames(), n->interpMode,
                     [n](int i) { PushUndoCheckpoint(); n->interpMode = i; n->MarkDirty(); });
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }


   void DrawImageAnalyzeParams(ImageAnalyzeNode* n)
   {
      const float r = n->RawR();
      const float g = n->RawG();
      const float b = n->RawB();
      const float res = n->Value(ImageAnalyzeNode::kResult);

      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = 16.0f;

      // Color swatch + Result meter (header, full width)
      ImVec4 col(r, g, b, 1.0f);
      ImGui::ColorButton("##ImageAnalyzeSwatch", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(24, 24));
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      ImGui::Text("Result: %.3f", res);
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
         ModSlider("probe U", &n->probeU, 0.0f, 1.0f, "%.3f", colW);
         ModSlider("probe V", &n->probeV, 0.0f, 1.0f, "%.3f", colW);
         if (n->sampleMode == ImageAnalyzeNode::kBoxRegion)
            ModSlider("radius", &n->probeRadius, 0.01f, 0.5f, "%.3f", colW);
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
            ImGui::TextColored(tok::V4(tok::palf::v_1000_400_400_1000), "err: %s", n->ExpressionError().c_str());
         }
         else
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextDisabled("vars: r, g, b, a, lum, sat, hue, delta, t");
            PopTextWrapWidth();
         }
      }

      NodeSeparator("sampling", colW);
      ModSlider("smoothing", &n->smoothing, 0.0f, 0.99f, "%.3f", colW);
      ModSlider("samples / sec", &n->sampleRate, 1.0f, 60.0f, "%.0f", colW);
      ModSliderInt("sample res", &n->sampleSize, 8, 256, colW);

      ImGui::EndGroup();

      // --- Right Column ---
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("shaping", colW);
      ModSlider("gain", &n->gain, 0.0f, 8.0f, "%.3f", colW);
      ModSlider("offset", &n->offset, -1.0f, 1.0f, "%.3f", colW);
      ModSlider("power", &n->power, 0.1f, 5.0f, "%.3f", colW);
      ModCheckbox("invert", &n->invert);
      ModCheckbox("clamp 0..1", &n->clamp01);

      ImGui::EndGroup();

      NodeSeparator("outputs", colW * 2 + gutter);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   // The small live view every tracking node shows: the frame the tracker saw, with what it found drawn on top,
   // so it is clear what is being tracked (and that the image is reaching the node at all).
   static void DrawTrackPreview(TrackNodeBase* n, float w)
   {
      const float aspect = std::max(0.5f, std::min(2.0f, n->PreviewAspect()));
      const float h = std::min(w / aspect, w * 0.8f);
      const float iw = std::min(w, h * aspect);
      const ImVec2 tl = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(w, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(tl.x + w, tl.y + h);
      dl->AddRectFilled(tl, br, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
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
      dl->AddImage((ImTextureID)(intptr_t)tex, a, b, ImVec2(mir ? 1.f : 0.f, 1.f), ImVec2(mir ? 0.f : 1.f, 0.f));
      std::vector<float> xy;
      n->GetOverlay(xy);
      const int np = (int)xy.size() / 2;
      if (np > 0)
      {
         auto pt = [&](int i) { return ImVec2(a.x + (mir ? 1.f - xy[i * 2] : xy[i * 2]) * iw, a.y + xy[i * 2 + 1] * h); };
         const ImU32 shadow = tok::U32(tok::pal::c_00000096);
         const ImU32 ink = tok::U32(tok::action_go, CategoryColors::IsThemeLight());
         for (const auto& l : n->OverlayLines())
            if (l.first < np && l.second < np)
               dl->AddLine(pt(l.first), pt(l.second), shadow, 3.0f);
         for (const auto& l : n->OverlayLines())
            if (l.first < np && l.second < np)
               dl->AddLine(pt(l.first), pt(l.second), ink, 1.5f);
         for (int i = 0; i < np; i++)
         {
            const bool key = n->OverlayKeyPoint(i);
            dl->AddCircleFilled(pt(i), key ? 4.0f : 2.0f, shadow, 10);
            dl->AddCircleFilled(pt(i), key ? 3.0f : 1.2f, key ? ImGui::GetColorU32(ImGuiCol_Text) : ink, 10);
         }
      }
      dl->PopClipRect();
   }

   void DrawTrackParams(TrackNodeBase* n)
   {
      const float colW = kParamWidth;
      const float gutter = 16.0f;
      if (n->PackMissing())
         ImGui::TextColored(tok::V4(tok::palf::v_1000_600_200_1000), "%s", T("Install the Tracking pack: Settings > Extensions"));
      else
         ImGui::TextDisabled("%s", n->Status().c_str());

      NodeSeparator("tracking", colW * 2 + gutter);
      ImGui::BeginGroup();
      DrawTrackPreview(n, colW);
      ImGui::EndGroup();
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();
      ModCheckbox("mirror", &n->mirror);
      ModSlider("smoothing", &n->smoothing, 0.0f, 1.0f, "%.2f", colW);
      ModSlider("hold ms", &n->holdMs, 0.0f, 1000.0f, "%.0f", colW);
      ImGui::EndGroup();

      NodeSeparator("outputs", colW * 2 + gutter);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   void DrawNullModulatorParams(NullModulatorNode* n)
   {
      if (!n->input)
      {
         ModSlider("constant", &n->constantIn, 0.0f, 1.0f);
      }
      else
      {
         ImGui::TextDisabled("driven by input modulator");
      }
   }
}

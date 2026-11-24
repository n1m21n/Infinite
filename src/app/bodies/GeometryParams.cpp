// Geometry / 3D / material / render parameter bodies (moved verbatim from main.cpp).
#include <algorithm>
#include "app/graph/NodeParams.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/components/Tip.h"
#include <cmath>
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/PinDot.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/DrawParam.h"
#include "platform/AppPaths.h"
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/UiScope.h"
#include "app/ui/design/components/FormParts.h"

namespace app
{
   // Log taper for controls that span macro (sub-millimetre) to room scale: equal travel per decade.
   static float LogPosToValue(float pos01, float minV, float maxV)
   {
      return minV * std::pow(maxV / minV, std::clamp(pos01, 0.0f, 1.0f));
   }
   static float LogValueToPos(float value, float minV, float maxV)
   {
      return std::log(std::max(value, minV) / minV) / std::log(maxV / minV);
   }
   inline const std::vector<std::string> kAxisXYZ = { "x", "y", "z" };

   // The pose and IK sections every rigged node shows (Model 3D, Armature):
   // joints turned in the 3D View's Pose mode (Ctrl+Tab), three angles each, and
   // the IK goal, set from a 3D View item in Pose mode or typed / modulated here
   // in the node's own space.
   void DrawRigPoseParams(IRigged* rig)
   {
      RigPose& p = rig->Pose();
      bool anyPose = false;
      for (int i = 0; i < RigPose::kSlots; i++)
      {
         const int j = p.poseJoint[i];
         const bool used = j >= 0 && j < rig->JointCount();
         if (!used && !gParamRegisterOnly)
            continue;
         if (!anyPose)
            NodeSeparator("pose");
         anyPose = true;
         ImGui::PushID(1000 + i);
         const std::string name = used ? rig->JointName(j) : "joint " + std::to_string(i + 1);
         for (int a = 0; a < 3; a++)
         {
            const std::string label = name + (a == 0 ? " X" : a == 1 ? " Y" : " Z");
            ImGui::PushID(a);
            ModSlider(label.c_str(), &p.poseRot[i][a], params::geometry_params::pose_rot_lo, params::geometry_params::pose_rot_hi, "%.1f");
            ImGui::PopID();
         }
         ImGui::PopID();
      }

      if (rig->JointCount() < 2 && !gParamRegisterOnly)
         return;
      NodeSeparator("ik");
      ModCheckbox("ik", &p.ikOn);
      if (!p.ikOn && !gParamRegisterOnly)
         return;
      std::vector<std::string> joints{ "none" };
      for (int j = 0; j < rig->JointCount(); j++)
         joints.push_back(rig->JointName(j));
      DropdownButton("reach with", joints, std::clamp(p.ikJoint + 1, 0, (int)joints.size() - 1),
                     [&p](int i) { PushUndoCheckpoint(); p.ikJoint = i - 1; });
      ModSliderInt("chain", &p.ikChain, params::geometry_params::ik_chain_lo, params::geometry_params::ik_chain_hi);
      ModSlider("ik weight", &p.ikWeight, 0.0f, 1.0f);
      ModSlider("target X", &p.ikTarget[0], params::geometry_params::ik_target_0_lo, params::geometry_params::ik_target_0_hi);
      ModSlider("target Y", &p.ikTarget[1], params::geometry_params::ik_target_1_lo, params::geometry_params::ik_target_1_hi);
      ModSlider("target Z", &p.ikTarget[2], params::geometry_params::ik_target_2_lo, params::geometry_params::ik_target_2_hi);
      ModCheckbox("pole", &p.ikPoleOn);
      if (p.ikPoleOn || gParamRegisterOnly)
      {
         ModSlider("pole X", &p.ikPole[0], params::geometry_params::ik_pole_0_lo, params::geometry_params::ik_pole_0_hi);
         ModSlider("pole Y", &p.ikPole[1], params::geometry_params::ik_pole_1_lo, params::geometry_params::ik_pole_1_hi);
         ModSlider("pole Z", &p.ikPole[2], params::geometry_params::ik_pole_2_lo, params::geometry_params::ik_pole_2_hi);
      }
   }

   namespace
   {
      using AudioFileParamsRow = params::Row<AudioFileNode>;
      const AudioFileParamsRow kAudioFileParamsRows[] = {
         AudioFileParamsRow::Toggle<&AudioFileNode::followTransport>("follow transport"),
         AudioFileParamsRow::Toggle<&AudioFileNode::loop>("loop"),
         AudioFileParamsRow::Toggle<&AudioFileNode::monitor>("audible"),
         AudioFileParamsRow::Slider<&AudioFileNode::volume>("volume", 0.0f, 1.0f),
         AudioFileParamsRow::Slider<&AudioFileNode::gain>("gain", params::AudioFile::gain_lo, params::AudioFile::gain_hi),
         AudioFileParamsRow::Slider<&AudioFileNode::pitch>("pitch", params::AudioFile::pitch_lo, params::AudioFile::pitch_hi),
      };
   }

   void DrawAudioFileParams(AudioFileNode* n)
   {
      if (ActionButton::Draw("Choose audio...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      PushTextWrapWidth(kPreviewSize);
      if (!n->FileName().empty())
         ImGui::TextDisabled("%s", n->FileName().c_str());
      ImGui::TextDisabled("%s", n->Status().c_str());
      PopTextWrapWidth();

      if (n->IsLoaded())
      {
         const double dur = std::max(0.001, n->Duration());
         ImGui::ProgressBar((float)(n->Position() / dur), ImVec2(kPreviewSize, 0));
         ImGui::TextDisabled("%s / %s", UiFmt::Sec(n->Position()).c, UiFmt::Sec(dur).c);

         if (n->IsPlaying())
         {
            if (ActionButton::Draw("Pause", ImVec2(kPreviewSize * tok::geometry_params_audio_file_ratio, 0)))
               n->Pause();
         }
         else if (ActionButton::Draw("Play", ImVec2(kPreviewSize * tok::geometry_params_audio_file_ratio, 0)))
         {
            n->Play();
         }
         Ui::SameRow();
         if (ActionButton::Draw("Restart", ImVec2(kPreviewSize * tok::geometry_params_audio_file_ratio, 0)))
            n->Restart();

         Ui::DrawParams(n, kAudioFileParamsRows);

         const Platform::AudioLevels& lv = n->Levels();
         ImGui::Text("level "); Ui::SameRow();
         ImGui::ProgressBar(std::min(1.0f, lv.rms * n->gain), ImVec2(kPreviewSize * tok::geometry_params_audio_file_w, 0), "");
      }
   }


   namespace
   {
      using AudioAnalyzeParamsRow = params::Row<AudioAnalyzeNode>;
      const AudioAnalyzeParamsRow kAudioAnalyzeParamsRows1[] = {
         AudioAnalyzeParamsRow::Slider<&AudioAnalyzeNode::gain>("gain", params::AudioAnalyze::gain_lo, params::AudioAnalyze::gain_hi),
         AudioAnalyzeParamsRow::Slider<&AudioAnalyzeNode::onsetHold>("onset hold", params::AudioAnalyze::onset_hold_lo, params::AudioAnalyze::onset_hold_hi),
      };
      const AudioAnalyzeParamsRow kAudioAnalyzeParamsRows2[] = {
         AudioAnalyzeParamsRow::Slider<&AudioAnalyzeNode::attack>("attack", params::AudioAnalyze::attack_lo, params::AudioAnalyze::attack_hi),
         AudioAnalyzeParamsRow::Slider<&AudioAnalyzeNode::release>("release", params::AudioAnalyze::release_lo, params::AudioAnalyze::release_hi),
      };
   }

   void DrawAudioAnalyzeParams(AudioAnalyzeNode* n)
   {
      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = tok::geometry_params_gutter;
      const float bodyW = colW * tok::geometry_params_body_w_ratio + gutter;

      if (n->input.IsConnected())
      {
         // Name the actual node rather than the old hardcoded "Audio File" -
         // any audio source can be patched in here now.
         const char* srcName = "patched source";
         for (GraphNode& src : gNodes)
         {
            if (src.node.get() == n->input.GetSource())
            {
               srcName = src.typeName.c_str();
               break;
            }
         }
         PushTextWrapWidth(bodyW);
         ImGui::TextColored(tok::RoleV4(tok::col::text_alt), "source: %s", srcName);
         PopTextWrapWidth();
      }
      else
      {
         if (Platform::AudioIsRunning())
         {
            if (ActionButton::Draw("Stop listening", ImVec2(bodyW, 0), ActionButton::Kind::Record))
               n->Stop();
         }
         else if (ActionButton::Draw("Start listening", ImVec2(bodyW, 0)))
         {
            n->Start();
         }
         PushTextWrapWidth(bodyW);
         ImGui::TextDisabled("%s", n->Status().c_str());
         PopTextWrapWidth();
      }

      // spectrum, so it is obvious whether audio is actually arriving
      const Platform::AudioLevels& levels = n->Levels();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = tok::geometry_params_h;
      Ui::Claim(ImVec2(bodyW, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + bodyW, origin.y + h), tok::Role(tok::col::surface_deep), tok::geometry_params_audio_analyze_radius);
      const float bw = bodyW / (float)Platform::kAudioBands;
      for (int i = 0; i < Platform::kAudioBands; i++)
      {
         const float v = std::min(1.0f, levels.bands[i] * n->gain);
         dl->AddRectFilled(ImVec2(origin.x + i * bw + 1, origin.y + h - v * h),
                           ImVec2(origin.x + (i + 1) * bw - 1, origin.y + h),
                           tok::Role(tok::col::accent_pale_muted_warm, tok::geometry_params_fill_alpha_lg));
      }
      if (!n->input.IsConnected() && !Platform::AudioIsRunning())
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + bodyW, origin.y + h)}, "idle");
      dl->AddRect(origin, ImVec2(origin.x + bodyW, origin.y + h), tok::Role(tok::col::edge_lo_light), tok::geometry_params_audio_analyze_radius);

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("input", colW);
      Ui::DrawParams(n, kAudioAnalyzeParamsRows1);

      ImGui::EndGroup();

      // --- Right Column ---
      FormParts::Inline(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("envelope", colW);
      Ui::DrawParams(n, kAudioAnalyzeParamsRows2);

      ImGui::EndGroup();

      NodeSeparator("outputs", bodyW);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   namespace
   {
      using PathParamsRow = params::Row<PathNode>;
      const PathParamsRow kPathParamsRows1[] = {
         PathParamsRow::Slider<&PathNode::speed>("speed / beat", params::Path::speed_lo, params::Path::speed_hi),
         PathParamsRow::Slider<&PathNode::phase>("phase", 0.0f, 1.0f),
         PathParamsRow::Toggle<&PathNode::pingPong>("ping-pong"),
      };
      const PathParamsRow kPathParamsRows2[] = {
         PathParamsRow::Choice<&PathNode::sliceAxis>("axis", &kAxisXYZ).Undo().Segmented(),
         PathParamsRow::Slider<&PathNode::slicePosition>("slice at", params::Path::slice_position_lo, params::Path::slice_position_hi),
      };
      const PathParamsRow kPathParamsRows3[] = {
         PathParamsRow::Slider<&PathNode::sizeX>("size x", params::Path::size_x_lo, params::Path::size_x_hi),
         PathParamsRow::Slider<&PathNode::sizeY>("size y", params::Path::size_y_lo, params::Path::size_y_hi),
         PathParamsRow::Slider<&PathNode::sizeZ>("size z", params::Path::size_z_lo, params::Path::size_z_hi),
      };
      const PathParamsRow kPathParamsRows4[] = {
         PathParamsRow::SliderInt<&PathNode::lissajousA>("ratio a", params::Path::lissajous_a_lo, params::Path::lissajous_a_hi),
         PathParamsRow::SliderInt<&PathNode::lissajousB>("ratio b", params::Path::lissajous_b_lo, params::Path::lissajous_b_hi),
      };
   }

   void DrawPathParams(PathNode* n)
   {
      if (n->IsFollowing())
         ImGui::TextDisabled("following %s points", UiFmt::Count((long long)(n->FollowPointCount())).c);
      else
         Ui::DrawParam(n, PathParamsRow::Choice<&PathNode::shape>("shape", &PathNode::ShapeNames));
      float p[3];
      n->CurrentPoint(p);
      ImGui::TextDisabled("t %s   (%s, %s, %s)", UiFmt::F2(n->Progress()).c, UiFmt::F2(p[0]).c, UiFmt::F2(p[1]).c, UiFmt::F2(p[2]).c);

      NodeSeparator("motion");
      Ui::DrawParams(n, kPathParamsRows1);

      const bool followingCurve = n->curveSource != nullptr && n->curveSource->GetCurve() != nullptr;
      if (n->geometrySource != nullptr && !followingCurve)
      {
         NodeSeparator("follow");
         Ui::DrawParam(n, PathParamsRow::Choice<&PathNode::followMode>("mode", &PathNode::FollowModeNames));
         if (n->followMode == PathNode::kFollowSlice)
         {
            Ui::DrawParams(n, kPathParamsRows2);
         }
         Ui::DrawParam(n, PathParamsRow::SliderInt<&PathNode::contourIndex>("contour", params::Path::contour_index_lo, params::Path::contour_index_hi));
      }

      NodeSeparator("shape");
      Ui::DrawParams(n, kPathParamsRows3);
      if (n->shape == PathNode::kHelix || n->shape == PathNode::kSpiral)
         Ui::DrawParam(n, PathParamsRow::Slider<&PathNode::turns>("turns", params::Path::turns_lo, params::Path::turns_hi));
      if (n->shape == PathNode::kLissajous)
      {
         Ui::DrawParams(n, kPathParamsRows4);
      }
   }


   // The row bank as an actual matrix rather than a column of "x1 .xx y1 .xx
   // z1 .xx" text lines: one bordered cell per (row, axis), value centred, a
   // draggable output pin sitting in the cell's bottom-right corner. Pin ids
   // are computed the same way ModSlider computes param pin ids (nodeIndex is
   // gCurrentNodeIndex, already set by the surrounding BeginNodeParams call),
   // rather than going through GraphNode::OutputPinId() - this function only
   // sees the node, not its GraphNode wrapper.
   void DrawGeometryTableGrid(GeometryTableNode* n, int nodeIndex)
   {
      const int rowCount = n->RowCount();
      const float cellW = kPreviewSize / tok::geometry_params_cell_w_ratio;
      const float cellH = tok::geometry_params_cell_h;
      const float gridW = cellW * tok::geometry_params_grid_w_ratio;
      const float gridH = cellH * (float)rowCount;

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool isLight = IsThemeLight();
      const ImU32 borderCol = isLight ? tok::Base(tok::col::edge_lo_light, tok::geometry_params_table_grid_edge_lo_light_alpha) : tok::Base(tok::col::surface_hi, tok::geometry_params_table_grid_surface_hi_alpha);
      const ImU32 textCol = isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::surface_hi);

      dl->AddRect(origin, ImVec2(origin.x + gridW, origin.y + gridH), borderCol, tok::geometry_params_table_grid_radius, 0, tok::geometry_params_table_grid_stroke);
      for (int r = 1; r < rowCount; r++)
      {
         const float y = origin.y + cellH * (float)r;
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + gridW, y), borderCol, tok::geometry_params_line_stroke_sm);
      }
      for (int c = 1; c < 3; c++)
      {
         const float x = origin.x + cellW * (float)c;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + gridH), borderCol, tok::geometry_params_line_stroke_sm);
      }

      for (int r = 0; r < rowCount; r++)
      {
         for (int a = 0; a < 3; a++)
         {
            const ImVec2 cellMin(origin.x + cellW * (float)a, origin.y + cellH * (float)r);
            const ImVec2 cellMax(cellMin.x + cellW, cellMin.y + cellH);
            const int outputIndex = tok::geometry_params_output_index_gap + r * tok::geometry_params_output_index_ratio + a;

            char buf[16];
            snprintf(buf, sizeof(buf), "%s", UiFmt::F2(n->DisplayValue(outputIndex)).c);
            const ImVec2 textSize = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(cellMin.x + (cellW - textSize.x) * 0.5f,
                               cellMin.y + (cellH - textSize.y) * 0.5f), textCol, buf);

            // The pin itself, positioned in the cell's bottom-right corner.
            // NOTE: EndPin() normally derives the pin's hit bounds from the
            // ImGui group opened by BeginPin() - that group's rect is anchored
            // at whatever the layout cursor was *before* BeginPin() and grows
            // via the window's monotonic CursorMaxPos. That works for pins
            // drawn in natural left-to-right/top-to-bottom flow (DrawPin(),
            // ModSlider's param pins), but here every cell jumps the cursor
            // to an absolute position and never advances it, so each
            // successive pin's group-derived bounds would balloon to include
            // every prior cell too - only the last-drawn pin would end up
            // hit-testable across the whole grid. ed::PinRect() sets the
            // pin's bounds explicitly, sidestepping that group-bounds path.
            ImGui::PushID(outputIndex);
            const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kOutputBase + outputIndex;
            ed::BeginPin(pinId, ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(tok::geometry_params_table_grid_pin_pivot_alignment_x, tok::geometry_params_table_grid_pin_pivot_alignment_y));
            const ImVec2 pinCenter(cellMax.x - tok::geometry_params_table_grid_gap, cellMax.y - tok::geometry_params_table_grid_gap);
            const ImVec2 pinMin(pinCenter.x - kPinHit * 0.5f, pinCenter.y - kPinHit * 0.5f);
            const ImVec2 pinMax(pinCenter.x + kPinHit * 0.5f, pinCenter.y + kPinHit * 0.5f);
            ed::PinRect(pinMin, pinMax);
            PinDot::Cable(dl, pinCenter, /*prediction=*/false, isLight, /*small=*/true);
            ed::EndPin();
            ImGui::PopID();
         }
      }

      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + gridH));
      Ui::Claim(ImVec2(gridW, 0.0f));
   }


   namespace
   {
      using GeometryTableParamsRow = params::Row<GeometryTableNode>;
      const GeometryTableParamsRow kGeometryTableParamsRows1[] = {
         GeometryTableParamsRow::Choice<&GeometryTableNode::sampleMode>("mode", &GeometryTableNode::SampleModeNames),
         GeometryTableParamsRow::SliderInt<&GeometryTableNode::rows>("rows", params::GeometryTable::rows_lo, params::GeometryTable::rows_hi),
         GeometryTableParamsRow::Choice<&GeometryTableNode::sortMode>("sort", &GeometryTableNode::SortModeNames),
         GeometryTableParamsRow::Slider<&GeometryTableNode::offset>("offset", 0.0f, 1.0f),
      };
      const GeometryTableParamsRow kGeometryTableParamsRows2[] = {
         GeometryTableParamsRow::Choice<&GeometryTableNode::sliceAxis>("axis", &kAxisXYZ).Undo().Segmented(),
         GeometryTableParamsRow::Slider<&GeometryTableNode::slicePosition>("slice at", params::GeometryTable::slice_position_lo, params::GeometryTable::slice_position_hi),
      };
   }

   void DrawGeometryTableParams(GeometryTableNode* n)
   {
      if (n->HasSamples())
         ImGui::TextDisabled("following %s points", UiFmt::Count((long long)(n->SampleCount())).c);
      else
         ImGui::TextDisabled("no geometry");

      NodeSeparator("sampling");
      Ui::DrawParams(n, kGeometryTableParamsRows1);

      if (n->sampleMode == GeometryTableNode::kContour)
      {
         Ui::DrawParams(n, kGeometryTableParamsRows2);
      }
      if (n->sampleMode == GeometryTableNode::kScatter)
         Ui::DrawParam(n, GeometryTableParamsRow::Slider<&GeometryTableNode::seed>("seed", params::GeometryTable::seed_lo, params::GeometryTable::seed_hi).Fmt("%.0f").Step(1.0f));

      NodeSeparator("range");
      Ui::DrawParam(n, GeometryTableParamsRow::Choice<&GeometryTableNode::space>("space", &GeometryTableNode::SpaceNames));
      if (n->space == GeometryTableNode::kSpaceFixed)
         Ui::DrawParam(n, GeometryTableParamsRow::Slider<&GeometryTableNode::extent>("extent", params::GeometryTable::extent_lo, params::GeometryTable::extent_hi));
      Ui::DrawParam(n, GeometryTableParamsRow::Slider<&GeometryTableNode::smooth>("smooth", 0.0f, 1.0f));

      NodeSeparator("table");
      if (!n->HasSamples())
         ImGui::TextDisabled("(unpatched - holding 0.5)");
      // Drawn either way, unpatched or not: every row pin needs to exist and
      // be draggable this frame regardless of whether it currently reads a
      // real sample or the neutral 0.5 fallback (§3's "hold at 0.5" rule) -
      // the generic output-pin row above is capped to the 4 aggregates for
      // this node type specifically so it never draws these same pin ids too.
      DrawGeometryTableGrid(n, gCurrentNodeIndex);
   }


   namespace
   {
      using OceanParamsRow = params::Row<OceanNode>;
      const OceanParamsRow kOceanParamsRows1[] = {
         OceanParamsRow::SliderInt<&OceanNode::resolution>("resolution", params::Ocean::resolution_lo, params::Ocean::resolution_hi),
         OceanParamsRow::Slider<&OceanNode::size>("size", params::Ocean::size_lo, params::Ocean::size_hi),
         OceanParamsRow::Slider<&OceanNode::horizon>("horizon", params::Ocean::horizon_lo, params::Ocean::horizon_hi),
      };
      const OceanParamsRow kOceanParamsRows2[] = {
         OceanParamsRow::Slider<&OceanNode::amplitude>("amplitude", 0.0f, 1.0f),
         OceanParamsRow::Slider<&OceanNode::wavelength>("wavelength", params::Ocean::wavelength_lo, params::Ocean::wavelength_hi),
         OceanParamsRow::Slider<&OceanNode::steepness>("steepness", params::Ocean::steepness_lo, params::Ocean::steepness_hi),
         OceanParamsRow::Slider<&OceanNode::choppiness>("choppiness", params::Ocean::choppiness_lo, params::Ocean::choppiness_hi),
         OceanParamsRow::Slider<&OceanNode::direction>("direction", params::Ocean::direction_lo, params::Ocean::direction_hi).Fmt("%.1f\xC2\xB0"),
         OceanParamsRow::SliderInt<&OceanNode::octaves>("octaves", params::Ocean::octaves_lo, params::Ocean::octaves_hi),
         OceanParamsRow::Slider<&OceanNode::speed>("speed", params::Ocean::speed_lo, params::Ocean::speed_hi),
      };
   }

   void DrawOceanParams(OceanNode* n)
   {
      NodeSeparator("surface");
      Ui::DrawParams(n, kOceanParamsRows1);

      NodeSeparator("waves");
      Ui::DrawParams(n, kOceanParamsRows2);
   }


   namespace
   {
      using CurveParamsRow = params::Row<CurveNode>;
      const CurveParamsRow kCurveParamsRows1[] = {
         CurveParamsRow::Choice<&CurveNode::kind>("type", &CurveNode::KindNames),
         CurveParamsRow::Choice<&CurveNode::preset>("preset", &CurveNode::PresetNames),
      };
      const CurveParamsRow kCurveParamsRows2[] = {
         CurveParamsRow::SliderInt<&CurveNode::pointCount>("points", params::Curve::point_count_lo, CurveNode::kMaxPoints),
         CurveParamsRow::SliderInt<&CurveNode::segments>("smoothness", params::Curve::segments_lo, params::Curve::segments_hi),
         CurveParamsRow::Toggle<&CurveNode::closed>("closed"),
         CurveParamsRow::Slider<&CurveNode::spread>("spread", params::Curve::spread_lo, params::Curve::spread_hi),
         CurveParamsRow::Slider<&CurveNode::height>("height", params::Curve::height_lo, params::Curve::height_hi),
         CurveParamsRow::Slider<&CurveNode::twist>("twist", params::Curve::twist_lo, params::Curve::twist_hi).Fmt("%.1f\xC2\xB0"),
      };
      const CurveParamsRow kCurveParamsRows3[] = {
         CurveParamsRow::Slider<&CurveNode::radius>("radius", params::Curve::radius_lo, params::Curve::radius_hi),
         CurveParamsRow::SliderInt<&CurveNode::sides>("sides", params::Curve::sides_lo, params::Curve::sides_hi),
         CurveParamsRow::Slider<&CurveNode::taper>("taper", 0.0f, 1.0f),
      };
   }

   void DrawCurveParams(CurveNode* n)
   {
      ImGui::TextDisabled("%s points, %s triangles", UiFmt::Count((long long)(n->PointCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParams(n, kCurveParamsRows1);

      NodeSeparator("shape");
      Ui::DrawParams(n, kCurveParamsRows2);
      if (n->preset == 3)
         Ui::DrawParam(n, CurveParamsRow::Slider<&CurveNode::seed>("seed", params::Curve::seed_lo, params::Curve::seed_hi).Fmt("%.0f").Step(1.0f));

      NodeSeparator("tube");
      Ui::DrawParams(n, kCurveParamsRows3);
   }


   namespace
   {
      using LSystemParamsRow = params::Row<LSystemNode>;
      const LSystemParamsRow kLSystemParamsRows1[] = {
         LSystemParamsRow::SliderInt<&LSystemNode::iterations>("generations", 0, LSystemNode::kMaxIterations),
         LSystemParamsRow::Slider<&LSystemNode::angle>("angle", params::LSystem::angle_lo, params::LSystem::angle_hi).Fmt("%.1f\xC2\xB0"),
         LSystemParamsRow::Slider<&LSystemNode::length>("length", params::LSystem::length_lo, params::LSystem::length_hi),
         LSystemParamsRow::Slider<&LSystemNode::lengthScale>("length / branch", params::LSystem::length_scale_lo, params::LSystem::length_scale_hi),
      };
      const LSystemParamsRow kLSystemParamsRows2[] = {
         LSystemParamsRow::Slider<&LSystemNode::radius>("radius", params::LSystem::radius_lo, params::LSystem::radius_hi),
         LSystemParamsRow::Slider<&LSystemNode::radiusScale>("radius / branch", params::LSystem::radius_scale_lo, params::LSystem::radius_scale_hi),
         LSystemParamsRow::SliderInt<&LSystemNode::sides>("sides", params::LSystem::sides_lo, params::LSystem::sides_hi),
         LSystemParamsRow::Slider<&LSystemNode::taper>("taper", 0.0f, 1.0f),
      };
   }

   void DrawLSystemParams(LSystemNode* n)
   {
      ImGui::TextDisabled("%s", n->Status().c_str());
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->axiom.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("axiom", buf, sizeof(buf)))
         n->axiom = buf;
      snprintf(buf, sizeof(buf), "%s", n->rules.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("rules", buf, sizeof(buf)))
         n->rules = buf;

      NodeSeparator("growth");
      Ui::DrawParams(n, kLSystemParamsRows1);

      NodeSeparator("tube");
      Ui::DrawParams(n, kLSystemParamsRows2);

      if (!n->CookWarning().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextUnformatted(n->CookWarning().c_str());
         PopTextWrapWidth();
      }
   }


   namespace
   {
      using MetaBallParamsRow = params::Row<MetaBallNode>;
      const MetaBallParamsRow kMetaBallParamsRows1[] = {
         MetaBallParamsRow::SliderInt<&MetaBallNode::ballCount>("balls", 1, MetaBallNode::kMaxBalls),
         MetaBallParamsRow::Slider<&MetaBallNode::spread>("spread", params::MetaBall::spread_lo, params::MetaBall::spread_hi),
         MetaBallParamsRow::Slider<&MetaBallNode::spin>("orbit / beat", -1.0f, 1.0f),
      };
      const MetaBallParamsRow kMetaBallParamsRows2[] = {
         MetaBallParamsRow::Slider<&MetaBallNode::radius>("radius", params::MetaBall::radius_lo, params::MetaBall::radius_hi),
         MetaBallParamsRow::Slider<&MetaBallNode::threshold>("threshold", params::MetaBall::threshold_lo, params::MetaBall::threshold_hi),
         MetaBallParamsRow::Slider<&MetaBallNode::bounds>("bounds", params::MetaBall::bounds_lo, params::MetaBall::bounds_hi),
         MetaBallParamsRow::SliderInt<&MetaBallNode::resolution>("resolution", params::MetaBall::resolution_lo, params::MetaBall::resolution_hi),
      };
   }

   void DrawMetaBallParams(MetaBallNode* n)
   {
      ImGui::TextDisabled("%s balls, %s triangles", UiFmt::Count((long long)(n->BallCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      NodeSeparator("field");
      if (n->cloudSource == nullptr || n->cloudSource->GetPointCloud() == nullptr)
      {
         Ui::DrawParams(n, kMetaBallParamsRows1);
      }
      else
      {
         Ui::DrawParam(n, MetaBallParamsRow::SliderInt<&MetaBallNode::maxFromCloud>("max from cloud", params::MetaBall::max_from_cloud_lo, params::MetaBall::max_from_cloud_hi));
      }
      Ui::DrawParams(n, kMetaBallParamsRows2);
   }


   namespace
   {
      using JoinGeometryParamsRow = params::Row<JoinGeometryNode>;
      const JoinGeometryParamsRow kJoinGeometryParamsRows[] = {
         JoinGeometryParamsRow::Choice<&JoinGeometryNode::mode>("mode", &JoinGeometryNode::ModeNames),
         // A merged mesh is one draw call, so it can only wear one material -
         // picked from an input rather than authored here, since editing colour
         // and shading now lives on the dedicated Material node.
         JoinGeometryParamsRow::SliderInt<&JoinGeometryNode::materialFrom>("material from input", 0, JoinGeometryNode::kSlots - 1),
      };
   }

   void DrawJoinGeometryParams(JoinGeometryNode* n)
   {
      ImGui::TextDisabled("%s inputs, %s triangles", UiFmt::Count((long long)(n->ConnectedCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParams(n, kJoinGeometryParamsRows);
   }


   namespace
   {
      using Group3DParamsRow = params::Row<Group3DNode>;
      const Group3DParamsRow kGroup3DParamsRows1[] = {
         Group3DParamsRow::Slider<&Group3DNode::offsetX>("move x", params::Group3D::offset_x_lo, params::Group3D::offset_x_hi),
         Group3DParamsRow::Slider<&Group3DNode::offsetY>("move y", params::Group3D::offset_y_lo, params::Group3D::offset_y_hi),
         Group3DParamsRow::Slider<&Group3DNode::offsetZ>("move z", params::Group3D::offset_z_lo, params::Group3D::offset_z_hi),
         Group3DParamsRow::Slider<&Group3DNode::rotX>("rotate x", params::Group3D::rot_x_lo, params::Group3D::rot_x_hi).Fmt("%.1f\xC2\xB0"),
         Group3DParamsRow::Slider<&Group3DNode::rotY>("rotate y", params::Group3D::rot_y_lo, params::Group3D::rot_y_hi).Fmt("%.1f\xC2\xB0"),
         Group3DParamsRow::Slider<&Group3DNode::rotZ>("rotate z", params::Group3D::rot_z_lo, params::Group3D::rot_z_hi).Fmt("%.1f\xC2\xB0"),
         Group3DParamsRow::Slider<&Group3DNode::scale>("scale", params::Group3D::scale_lo, params::Group3D::scale_hi),
      };
      const Group3DParamsRow kGroup3DParamsRows2[] = {
         Group3DParamsRow::Slider<&Group3DNode::pivotX>("pivot x", params::Group3D::pivot_x_lo, params::Group3D::pivot_x_hi),
         Group3DParamsRow::Slider<&Group3DNode::pivotY>("pivot y", params::Group3D::pivot_y_lo, params::Group3D::pivot_y_hi),
         Group3DParamsRow::Slider<&Group3DNode::pivotZ>("pivot z", params::Group3D::pivot_z_lo, params::Group3D::pivot_z_hi),
      };
   }

   void DrawGroup3DParams(Group3DNode* n)
   {
      Ui::DrawParams(n, kGroup3DParamsRows1);
      Ui::DrawParams(n, kGroup3DParamsRows2, !(n->rotX == 0.0f && n->rotY == 0.0f && n->rotZ == 0.0f && n->scale == 1.0f));
   }


   namespace
   {
      using Switcher3DParamsRow = params::Row<Switcher3DNode>;
      const Switcher3DParamsRow kSwitcher3DParamsRows[] = {
         Switcher3DParamsRow::Choice<&Switcher3DNode::unit>("unit", &Switcher3DNode::UnitNames),
         Switcher3DParamsRow::Slider<&Switcher3DNode::interval>("every", params::Switcher3D::interval_lo, params::Switcher3D::interval_hi),
      };
   }

   void DrawSwitcher3DParams(Switcher3DNode* n)
   {
      Ui::DrawParams(n, kSwitcher3DParamsRows, !n->manual);
      Ui::DrawParam(n, Switcher3DParamsRow::Toggle<&Switcher3DNode::manual>("manual"));
      if (n->manual)
      {
         Ui::DrawParam(n, Switcher3DParamsRow::SliderInt<&Switcher3DNode::manualSlot>("slot", 0, Switcher3DNode::kSlots - 1));
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }


   namespace
   {
      using WrapParamsRow = params::Row<WrapNode>;
      const WrapParamsRow kWrapParamsRows[] = {
         WrapParamsRow::Slider<&WrapNode::offset>("offset", params::Wrap::offset_lo, params::Wrap::offset_hi),
         WrapParamsRow::Slider<&WrapNode::blend>("blend", 0.0f, 1.0f),
         WrapParamsRow::Toggle<&WrapNode::flatShade>("flat shade"),
         WrapParamsRow::Toggle<&WrapNode::flipNormals>("flip normals"),
      };
   }

   void DrawWrapParams(WrapNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParam(n, WrapParamsRow::Choice<&WrapNode::mode>("mode", &WrapNode::ModeNames));
      // Axis, radius and fit steer the parametric bend; nearest-surface has no
      // parameterisation for them to act on.
      if (n->mode != MeshOps::kWrapNearest)
      {
         Ui::DrawParam(n, WrapParamsRow::Choice<&WrapNode::axis>("axis", &kAxisXYZ).Undo().Segmented());
         // With a target the radius always follows the target's size, so the
         // only control is a multiplier - and the resolved value is shown so
         // that link stays visible. Without one there is nothing to follow,
         // so the radius is set outright. Never both.
         if (n->targetInput != nullptr)
         {
            ImGui::TextDisabled("radius %s (from target)", UiFmt::F3(n->ResolvedRadius()).c);
            Ui::DrawParam(n, WrapParamsRow::Slider<&WrapNode::radiusScale>("radius scale", params::Wrap::radius_scale_lo, params::Wrap::radius_scale_hi));
         }
         else
         {
            Ui::DrawParam(n, WrapParamsRow::Slider<&WrapNode::radiusOverride>("radius", params::Wrap::radius_override_lo, params::Wrap::radius_override_hi));
         }
         Ui::DrawParam(n, WrapParamsRow::Toggle<&WrapNode::fitAround>("fit around"));
      }
      Ui::DrawParams(n, kWrapParamsRows);
   }


   namespace
   {
      using ClothParamsRow = params::Row<ClothNode>;
      const ClothParamsRow kClothParamsRows1[] = {
         ClothParamsRow::Choice<&ClothNode::pinMode>("pinned", &ClothNode::PinModeNames),
         ClothParamsRow::Slider<&ClothNode::stiffness>("stiffness", 0.0f, 1.0f),
         ClothParamsRow::SliderInt<&ClothNode::iterations>("iterations", params::Cloth::iterations_lo, params::Cloth::iterations_hi),
         ClothParamsRow::Slider<&ClothNode::damping>("damping", params::Cloth::damping_lo, params::Cloth::damping_hi),
         ClothParamsRow::Slider<&ClothNode::mass>("mass", params::Cloth::mass_lo, params::Cloth::mass_hi),
         ClothParamsRow::Slider<&ClothNode::shapeRetention>("hold shape", 0.0f, 1.0f),
         ClothParamsRow::Slider<&ClothNode::bending>("bending", 0.0f, 1.0f),
      };
      const ClothParamsRow kClothParamsRows2[] = {
         ClothParamsRow::Slider<&ClothNode::gravityX>("gravity x", params::Cloth::gravity_x_lo, params::Cloth::gravity_x_hi),
         ClothParamsRow::Slider<&ClothNode::gravityY>("gravity y", params::Cloth::gravity_y_lo, params::Cloth::gravity_y_hi),
         ClothParamsRow::Slider<&ClothNode::gravityZ>("gravity z", params::Cloth::gravity_z_lo, params::Cloth::gravity_z_hi),
         ClothParamsRow::Slider<&ClothNode::windX>("wind x", params::Cloth::wind_x_lo, params::Cloth::wind_x_hi),
         ClothParamsRow::Slider<&ClothNode::windY>("wind y", params::Cloth::wind_y_lo, params::Cloth::wind_y_hi),
         ClothParamsRow::Slider<&ClothNode::windZ>("wind z", params::Cloth::wind_z_lo, params::Cloth::wind_z_hi),
         ClothParamsRow::Slider<&ClothNode::windTurbulence>("turbulence", params::Cloth::wind_turbulence_lo, params::Cloth::wind_turbulence_hi),
      };
      const ClothParamsRow kClothParamsRows3[] = {
         ClothParamsRow::Slider<&ClothNode::groundHeight>("height", params::Cloth::ground_height_lo, params::Cloth::ground_height_hi),
         ClothParamsRow::Slider<&ClothNode::bounce>("bounce", 0.0f, 1.0f),
         ClothParamsRow::Slider<&ClothNode::friction>("friction", 0.0f, 1.0f),
      };
      const ClothParamsRow kClothParamsRows4[] = {
         ClothParamsRow::Slider<&ClothNode::colliderDistance>("distance", params::Cloth::collider_distance_lo, params::Cloth::collider_distance_hi),
         ClothParamsRow::Slider<&ClothNode::colliderFriction>("friction##collider", params::Cloth::collider_friction_lo, params::Cloth::collider_friction_hi),
      };
   }

   void DrawClothParams(ClothNode* n)
   {
      ImGui::TextDisabled("%s triangles, %s links", UiFmt::Count((long long)(n->TriangleCount())).c, UiFmt::Count((long long)(n->ConstraintCount())).c);
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("solver");
      Ui::DrawParams(n, kClothParamsRows1);

      NodeSeparator("forces");
      Ui::DrawParams(n, kClothParamsRows2);

      NodeSeparator("ground");
      Ui::DrawParam(n, ClothParamsRow::Toggle<&ClothNode::groundEnabled>("collide with ground"));
      if (n->groundEnabled)
      {
         Ui::DrawParams(n, kClothParamsRows3);
      }

      NodeSeparator("collider");
      Ui::DrawParams(n, kClothParamsRows4, n->collider != nullptr);
   }


   // Shared "show more" fold for long single-column bodies (GraphNode::showAdvancedParams, saved with the patch).
   // Call right before the folded rows with how many float params (ModSlider/ModSliderInt) and colours they
   // register; folded rows are still registered in order (gParamRegisterOnly), so indices, modulation and
   // expressions are unchanged, and a folded row with a cable, expression or palette binding opens the fold.
   // Returns whether the rows are shown; restore gParamRegisterOnly to `saved` after them.
   bool BeginAdvancedFold(const char* id, int floatCount, int colorCount, bool& saved)
   {
      GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex);
      saved = gParamRegisterOnly;
      const bool pinned = gn != nullptr && gn->showAdvancedParams;
      bool inUse = false;
      for (int k = 0; k < colorCount && !inUse; k++)
         inUse = PaletteBinding::Instance().IsBound(gCurrentNodeIndex, gColorCounter + k);
      for (int k = 0; k < floatCount && !inUse; k++)
         inUse = Modulation::Instance().IsModulated(gCurrentNodeIndex, gParamCounter + k) ||
                 Modulation::Instance().HasExpression(gCurrentNodeIndex, gParamCounter + k);
      const bool open = pinned || inUse;
      ImGui::PushID(id);
      if (ActionButton::Column(inUse && !pinned ? "show more (in use)" : open ? "show less" : "show more",
                               kParamWidth) &&
          gn != nullptr)
         gn->showAdvancedParams = !gn->showAdvancedParams;
      ImGui::PopID();
      gParamRegisterOnly = saved || !open;
      return open;
   }


   namespace
   {
      using RigidBodyParamsRow = params::Row<RigidBodyNode>;
      const RigidBodyParamsRow kRigidBodyParamsRows1[] = {
         RigidBodyParamsRow::Choice<&RigidBodyNode::collider>("collider", &RigidBodyNode::ColliderNames),
         RigidBodyParamsRow::Slider<&RigidBodyNode::mass>("mass", params::RigidBody::mass_lo, params::RigidBody::mass_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::friction>("friction", 0.0f, 1.0f),
         RigidBodyParamsRow::Slider<&RigidBodyNode::bounce>("bounce", 0.0f, 1.0f),
         RigidBodyParamsRow::Slider<&RigidBodyNode::linearDamping>("linear damping", params::RigidBody::linear_damping_lo, params::RigidBody::linear_damping_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::angularDamping>("angular damping", params::RigidBody::angular_damping_lo, params::RigidBody::angular_damping_hi),
         RigidBodyParamsRow::SliderInt<&RigidBodyNode::iterations>("iterations", params::RigidBody::iterations_lo, params::RigidBody::iterations_hi),
      };
      const RigidBodyParamsRow kRigidBodyParamsRows2[] = {
         RigidBodyParamsRow::Slider<&RigidBodyNode::gravityX>("gravity x", params::RigidBody::gravity_x_lo, params::RigidBody::gravity_x_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::gravityY>("gravity y", params::RigidBody::gravity_y_lo, params::RigidBody::gravity_y_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::gravityZ>("gravity z", params::RigidBody::gravity_z_lo, params::RigidBody::gravity_z_hi),
      };
      const RigidBodyParamsRow kRigidBodyParamsRows3[] = {
         RigidBodyParamsRow::Slider<&RigidBodyNode::velX>("velocity x", params::RigidBody::vel_x_lo, params::RigidBody::vel_x_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::velY>("velocity y", params::RigidBody::vel_y_lo, params::RigidBody::vel_y_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::velZ>("velocity z", params::RigidBody::vel_z_lo, params::RigidBody::vel_z_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::spinX>("spin x", params::RigidBody::spin_x_lo, params::RigidBody::spin_x_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::spinY>("spin y", params::RigidBody::spin_y_lo, params::RigidBody::spin_y_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::spinZ>("spin z", params::RigidBody::spin_z_lo, params::RigidBody::spin_z_hi),
      };
      const RigidBodyParamsRow kRigidBodyParamsRows4[] = {
         RigidBodyParamsRow::Slider<&RigidBodyNode::kickTime>("kick time", params::RigidBody::kick_time_lo, params::RigidBody::kick_time_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::kickX>("kick x", params::RigidBody::kick_x_lo, params::RigidBody::kick_x_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::kickY>("kick y", params::RigidBody::kick_y_lo, params::RigidBody::kick_y_hi),
         RigidBodyParamsRow::Slider<&RigidBodyNode::kickZ>("kick z", params::RigidBody::kick_z_lo, params::RigidBody::kick_z_hi),
      };
   }

   void DrawRigidBodyParams(RigidBodyNode* n)
   {
      ImGui::TextDisabled("%s bodies, %s triangles", UiFmt::Count((long long)(n->BodyCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("bodies");
      Ui::DrawParams(n, kRigidBodyParamsRows1);

      NodeSeparator("forces");
      Ui::DrawParams(n, kRigidBodyParamsRows2);

      bool rbSaved = false;
      const bool rbOpen = BeginAdvancedFold("showMoreRigid", 10, 0, rbSaved);   // start + kick sliders
      if (rbOpen)
         NodeSeparator("start");
      Ui::DrawParams(n, kRigidBodyParamsRows3);

      if (rbOpen)
         NodeSeparator("kick");
      Ui::DrawParams(n, kRigidBodyParamsRows4);
      gParamRegisterOnly = rbSaved;

      NodeSeparator("world");
      Ui::DrawParam(n, RigidBodyParamsRow::Toggle<&RigidBodyNode::groundEnabled>("ground"));
      if (n->groundEnabled)
         Ui::DrawParam(n, RigidBodyParamsRow::Slider<&RigidBodyNode::groundHeight>("ground height", params::RigidBody::ground_height_lo, params::RigidBody::ground_height_hi));
      Ui::DrawParam(n, RigidBodyParamsRow::Toggle<&RigidBodyNode::wallsEnabled>("walls"));
      if (n->wallsEnabled)
         Ui::DrawParam(n, RigidBodyParamsRow::Slider<&RigidBodyNode::wallSize>("wall distance", params::RigidBody::wall_size_lo, params::RigidBody::wall_size_hi));
   }


   namespace
   {
      using ParticleSystemParamsRow = params::Row<ParticleSystemNode>;
      const ParticleSystemParamsRow kParticleSystemParamsRows1[] = {
         ParticleSystemParamsRow::Choice<&ParticleSystemNode::emitShape>("shape", &ParticleSystemNode::EmitShapeNames),
         ParticleSystemParamsRow::SliderInt<&ParticleSystemNode::maxParticles>("max particles", params::ParticleSystem::max_particles_lo, ParticleSystemNode::kGpuMaxParticles),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::emitRate>("rate / sec", params::ParticleSystem::emit_rate_lo, params::ParticleSystem::emit_rate_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::emitRadius>("radius", params::ParticleSystem::emit_radius_lo, params::ParticleSystem::emit_radius_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::lifetime>("lifetime", params::ParticleSystem::lifetime_lo, params::ParticleSystem::lifetime_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::lifetimeRandom>("life random", 0.0f, 1.0f),
      };
      const ParticleSystemParamsRow kParticleSystemParamsRows2[] = {
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::initialSpeed>("speed", params::ParticleSystem::initial_speed_lo, params::ParticleSystem::initial_speed_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::speedRandom>("speed random", 0.0f, 1.0f),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::spread>("spread", 0.0f, 1.0f),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::dirX>("dir x", -1.0f, 1.0f),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::dirY>("dir y", -1.0f, 1.0f),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::dirZ>("dir z", -1.0f, 1.0f),
      };
      const ParticleSystemParamsRow kParticleSystemParamsRows3[] = {
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::gravityX>("gravity x", params::ParticleSystem::gravity_x_lo, params::ParticleSystem::gravity_x_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::gravityY>("gravity y", params::ParticleSystem::gravity_y_lo, params::ParticleSystem::gravity_y_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::gravityZ>("gravity z", params::ParticleSystem::gravity_z_lo, params::ParticleSystem::gravity_z_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::drag>("drag", params::ParticleSystem::drag_lo, params::ParticleSystem::drag_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::turbulence>("turbulence", params::ParticleSystem::turbulence_lo, params::ParticleSystem::turbulence_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::turbulenceScale>("turb scale", params::ParticleSystem::turbulence_scale_lo, params::ParticleSystem::turbulence_scale_hi),
      };
      const ParticleSystemParamsRow kParticleSystemParamsRows4[] = {
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::startSize>("start size", params::ParticleSystem::start_size_lo, params::ParticleSystem::start_size_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::endSize>("end size", params::ParticleSystem::end_size_lo, params::ParticleSystem::end_size_hi),
         ParticleSystemParamsRow::Colour<&ParticleSystemNode::startColor>("start colour"),
         ParticleSystemParamsRow::Colour<&ParticleSystemNode::endColor>("end colour"),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::seed>("seed", params::ParticleSystem::seed_lo, params::ParticleSystem::seed_hi).Fmt("%.0f").Step(1.0f),
      };
      const ParticleSystemParamsRow kParticleSystemParamsRows5[] = {
         ParticleSystemParamsRow::Toggle<&ParticleSystemNode::emitFromInput>("emit from input"),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::speedAlongNormal>("speed along normal", params::ParticleSystem::speed_along_normal_lo, params::ParticleSystem::speed_along_normal_hi),
         ParticleSystemParamsRow::Toggle<&ParticleSystemNode::copySourceColor>("copy source colour"),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::mass>("mass", params::ParticleSystem::mass_lo, params::ParticleSystem::mass_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::timeScale>("time scale", params::ParticleSystem::time_scale_lo, params::ParticleSystem::time_scale_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::sizeCurve>("size curve", params::ParticleSystem::size_curve_lo, params::ParticleSystem::size_curve_hi),
         ParticleSystemParamsRow::Slider<&ParticleSystemNode::colorCurve>("colour curve", params::ParticleSystem::color_curve_lo, params::ParticleSystem::color_curve_hi),
      };
   }

   void DrawParticleSystemParams(ParticleSystemNode* n)
   {
      ImGui::TextDisabled(n->OnGpu() ? "%zu alive (GPU)" : "%zu alive", n->AliveCount());
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("emitter");
      Ui::DrawParams(n, kParticleSystemParamsRows1);

      NodeSeparator("launch");
      Ui::DrawParams(n, kParticleSystemParamsRows2);

      bool psSaved = false;
      const bool psOpen = BeginAdvancedFold("showMoreParticles", 14, 2, psSaved);   // forces + over life + emit/time
      if (psOpen)
         NodeSeparator("forces");
      Ui::DrawParams(n, kParticleSystemParamsRows3);

      if (psOpen)
         NodeSeparator("over life");
      Ui::DrawParams(n, kParticleSystemParamsRows4);

      // Appended after seed so every older row keeps its modulation slot.
      if (psOpen)
         NodeSeparator("emit & time");
      Ui::DrawParams(n, kParticleSystemParamsRows5);
      gParamRegisterOnly = psSaved;

      if (!n->CookWarning().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextUnformatted(n->CookWarning().c_str());
         PopTextWrapWidth();
      }
   }


   void LoadMaterialMap(INode* material, int map, const std::string& path, bool checkpoint)
   {
      int matIndex = -1;
      ImVec2 pos;
      // The "load map" pick drains after the canvas pass (RunPendingMapLoad), where no
      // editor is current: GetNodePosition dereferenced a null context (B-L-002, a 1.1.0 crash).
      ed::EditorContext* prevEditor = ed::GetCurrentEditor();
      ed::SetCurrentEditor(gEditor);
      for (GraphNode& gn : gNodes)
         if (gn.node.get() == material)
         {
            matIndex = gn.index;
            pos = ed::GetNodePosition(gn.NodeId());
         }
      ed::SetCurrentEditor(prevEditor);
      if (matIndex < 0)
         return;
      if (checkpoint)
         PushUndoCheckpoint("Load map image");
      // Left of the Material, one rung per map, so several loads don't land on each other.
      GraphNode* img = SpawnNode("Image Source", "Source", pos.x - 420.0f, pos.y + map * 120.0f);
      if (img == nullptr)
         return;
      if (auto* src = dynamic_cast<ImageSourceNode*>(img->node.get()))
         src->Load(path);
      std::string err;
      ConnectNodes(img->index, 0, matIndex, 1 + map, err);
      gPatchDirty = true;
   }


   void RunPendingMapLoad()
   {
      if (gPendingMapLoad.material == nullptr)
         return;
      const PendingMapLoad req = gPendingMapLoad;
      gPendingMapLoad = PendingMapLoad();
      LoadMaterialMap(req.material, req.map, req.path, true);
   }


   int MaterialMapForFile(MaterialNode* n, const std::string& path)
   {
      // The usual PBR set names (wood_Roughness.png, brick_nrm.jpg); anything else is the albedo.
      std::string name = path.substr(path.find_last_of("/\\") + 1);
      for (char& c : name)
         c = (char)std::tolower((unsigned char)c);
      static const std::pair<const char*, int> kHints[] = {
         { "normal", kMapNormal }, { "nrm", kMapNormal }, { "_nor", kMapNormal }, { "rough", kMapRoughness },
         { "metal", kMapMetallic }, { "occlusion", kMapAmbientOcclusion }, { "_ao", kMapAmbientOcclusion },
         { "emis", kMapEmission }, { "emit", kMapEmission }, { "opacity", kMapOpacity }, { "alpha", kMapOpacity },
         { "transm", kMapTransmission }, { "coat", kMapClearcoat }, { "sheen", kMapSheen },
      };
      int want = kMapAlbedo;
      for (const auto& [key, map] : kHints)
         if (name.find(key) != std::string::npos)
         {
            want = map;
            break;
         }
      if (!n->MapInput(want).IsConnected())
         return want;
      for (int m = 0; m < kMapCount; ++m)
         if (!n->MapInput(m).IsConnected())
            return m;
      return -1;
   }


   // True when every Render 3D downstream of `node` path traces (and there is at least one), so the
   // Material can grey what the path tracer ignores. A raster view anywhere downstream keeps them live.
   static bool FeedsOnlyPathTracer(const INode* node)
   {
      int start = -1;
      for (const GraphNode& gn : gNodes)
         if (gn.node.get() == node)
            start = gn.index;
      if (start < 0)
         return false;
      std::vector<int> stack{ start }, seen{ start };
      bool anyPt = false;
      while (!stack.empty())
      {
         const int at = stack.back();
         stack.pop_back();
         for (const LinkInfo& link : gLinks)
         {
            if (!GraphNode::IsOutputPin(link.srcPin) || GraphNode::NodeIndexFromPin(link.srcPin) != at)
               continue;
            const int to = GraphNode::NodeIndexFromPin(link.dstPin);
            if (std::find(seen.begin(), seen.end(), to) != seen.end())
               continue;
            seen.push_back(to);
            for (const GraphNode& gn : gNodes)
               if (gn.index == to)
               {
                  if (auto* r = dynamic_cast<const Render3DNode*>(gn.node.get()))
                  {
                     if (r->engine != 1)
                        return false;
                     anyPt = true;
                  }
                  else
                     stack.push_back(to);
               }
         }
      }
      return anyPt;
   }


   namespace
   {
      using MaterialParamsRow = params::Row<MaterialNode>;
      const MaterialParamsRow kMaterialParamsRows1[] = {
         MaterialParamsRow::Toggle<&MaterialNode::normalBump>("bump (height map)##matNormalBump"),
         MaterialParamsRow::Slider<&MaterialNode::normalStrength>("normal strength", params::Material::normal_strength_lo, params::Material::normal_strength_hi),
         MaterialParamsRow::Slider<&MaterialNode::detailScale>("detail scale", params::Material::detail_scale_lo, params::Material::detail_scale_hi).Fmt("%.2f"),
      };
      const MaterialParamsRow kMaterialParamsRows2[] = {
         MaterialParamsRow::Choice<&MaterialNode::wrapMode>("wrap", &MaterialNode::WrapModeNames),
         MaterialParamsRow::Colour<&MaterialNode::color>("colour"),
      };
      const MaterialParamsRow kMaterialParamsRows3[] = {
         MaterialParamsRow::Slider<&MaterialNode::metallic>("metallic", 0.0f, 1.0f),
         MaterialParamsRow::Slider<&MaterialNode::roughness>("roughness", params::Material::roughness_lo, params::Material::roughness_hi),
         MaterialParamsRow::Slider<&MaterialNode::specular>("specular", 0.0f, 1.0f),
         MaterialParamsRow::Slider<&MaterialNode::ior>("ior", params::Material::ior_lo, params::Material::ior_hi),
      };
      const MaterialParamsRow kMaterialParamsRows4[] = {
         MaterialParamsRow::Slider<&MaterialNode::opacity>("opacity", 0.0f, 1.0f),
         MaterialParamsRow::Toggle<&MaterialNode::textureAlpha>("texture alpha"),
      };
      const MaterialParamsRow kMaterialParamsRows5[] = {
         MaterialParamsRow::Colour<&MaterialNode::emissionColor>("emission"),
         MaterialParamsRow::Slider<&MaterialNode::emission>("emission", params::Material::emission_lo, params::Material::emission_hi),
      };
      const MaterialParamsRow kMaterialParamsRows6[] = {
         MaterialParamsRow::Colour<&MaterialNode::sheenColor>("sheen tint"),
         MaterialParamsRow::Slider<&MaterialNode::sheenRoughness>("sheen roughness", params::Material::sheen_roughness_lo, params::Material::sheen_roughness_hi),
         MaterialParamsRow::Slider<&MaterialNode::sheenDirection>("sheen direction", -1.0f, 1.0f).Fmt("%.2f"),
      };
      const MaterialParamsRow kMaterialParamsRows7[] = {
         MaterialParamsRow::Slider<&MaterialNode::iridescenceIor>("irid IOR", params::Material::iridescence_ior_lo, params::Material::iridescence_ior_hi),
         MaterialParamsRow::Slider<&MaterialNode::iridescenceThickness>("irid thickness", params::Material::iridescence_thickness_lo, params::Material::iridescence_thickness_hi).Fmt("%.0f nm"),
      };
      const MaterialParamsRow kMaterialParamsRows8[] = {
         MaterialParamsRow::Colour<&MaterialNode::subsurfaceColor>("sss colour"),
         MaterialParamsRow::Slider<&MaterialNode::subsurfaceRadius>("sss radius", 0.0f, 1.0f),
      };
      const MaterialParamsRow kMaterialParamsRows9[] = {
         MaterialParamsRow::Slider<&MaterialNode::melanin>("melanin", 0.0f, 1.0f),
         MaterialParamsRow::Slider<&MaterialNode::melaninRedness>("redness", 0.0f, 1.0f),
         MaterialParamsRow::Colour<&MaterialNode::hairTint>("hair tint"),
      };
      const MaterialParamsRow kMaterialParamsRows10[] = {
         MaterialParamsRow::Slider<&MaterialNode::radialRoughness>("radial rough", params::Material::radial_roughness_lo, params::Material::radial_roughness_hi),
         MaterialParamsRow::Slider<&MaterialNode::hairShift>("cuticle shift", params::Material::hair_shift_lo, params::Material::hair_shift_hi).Fmt("%.1f\xC2\xB0"),
      };
   }

   void DrawMaterialParams(MaterialNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = tok::geometry_params_gutter;

      // As Render 3D: both columns draw every row every frame, and a row that does not apply right now
      // is greyed, never dropped, so the body never grows or shifts as a value crosses zero. The rows
      // keep their registration order (the left column, then the right), which is what saved
      // modulation is keyed on. Left: normal map, surface, emission, coat & sheen. Right: optics, hair.
      const bool pbrUnused = n->shading != 0 && !gParamRegisterOnly;   // Normals/UV/Flat ignore the lighting model
      // The path tracer has no anisotropy, iridescence, dispersion, subsurface or non-Lit shading.
      const bool ptOnly = !gParamRegisterOnly && FeedsOnlyPathTracer(n);

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("normal map", colW);
      {
         Ui::RowsLive on(n->MapInput(kMapNormal).IsConnected());
         Ui::DrawParams(n, kMaterialParamsRows1);
         Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::detailStrength>("detail strength", params::Material::detail_strength_lo, params::Material::detail_strength_hi), !(n->detailScale <= 0.0f));
      }

      NodeSeparator("surface", colW);
      Ui::DrawParam(n, MaterialParamsRow::Choice<&MaterialNode::shading>("shading", &GeometryNode::ShadingNames), !ptOnly);
      Ui::DrawParams(n, kMaterialParamsRows2);
      Ui::DrawParams(n, kMaterialParamsRows3, !pbrUnused);
      Ui::DrawParams(n, kMaterialParamsRows4);
      Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::alphaCutoff>("alpha cutoff", 0.0f, 1.0f), n->opacity < 1.0f || n->alphaCutoff > 0.0f || n->textureAlpha || n->MapInput(kMapOpacity).IsConnected());

      {
         Ui::Section s("emission", colW, !pbrUnused);
         Ui::DrawParams(n, kMaterialParamsRows5);
      }

      {
         Ui::Section s("coat & sheen", colW, !pbrUnused);
         Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::clearcoat>("clearcoat", 0.0f, 1.0f));
         Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::clearcoatRoughness>("coat roughness", params::Material::clearcoat_roughness_lo, params::Material::clearcoat_roughness_hi), n->clearcoat > 0.0f || n->MapInput(kMapClearcoat).IsConnected());
         Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::sheen>("sheen", 0.0f, 1.0f));
         Ui::DrawParams(n, kMaterialParamsRows6, n->sheen > 0.0f || n->MapInput(kMapSheen).IsConnected());
      }

      ImGui::EndGroup();

      // --- Right Column --- (the split sits here so both columns hold about the same rows: 22 left, 21 right)
      FormParts::Inline(0.0f, gutter);
      ImGui::BeginGroup();

      {
         Ui::Section s("optics", colW, !pbrUnused);
         {
            Ui::RowsLive on(!ptOnly);
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::anisotropy>("anisotropy", -1.0f, 1.0f));
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::anisotropyRotation>("aniso rotation", 0.0f, 1.0f), n->anisotropy != 0.0f);
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::iridescence>("iridescence", 0.0f, 1.0f));
            Ui::DrawParams(n, kMaterialParamsRows7, !(n->iridescence <= 0.0f));
         }
         Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::transmission>("transmission", 0.0f, 1.0f));
         {
            Ui::RowsLive on(!(n->transmission <= 0.0f));
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::transmissionRoughness>("trans rough", 0.0f, 1.0f));
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::dispersion>("dispersion", 0.0f, 1.0f), !ptOnly);
         }
         {
            Ui::RowsLive on(!ptOnly);
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::subsurface>("subsurface", 0.0f, 1.0f));
            Ui::DrawParams(n, kMaterialParamsRows8, !(n->subsurface <= 0.0f));
            Ui::DrawParam(n, MaterialParamsRow::Slider<&MaterialNode::translucency>("translucency", 0.0f, 1.0f));
         }
      }

      // Hair (Chiang fibre BSDF): last, so the rows above keep their keys. Roughness and ior are the fibre's.
      static const std::vector<std::string> kModels = { "Principled", "Hair" }, kHairColour = { "Melanin", "Colour" };
      NodeSeparator("hair", colW);
      DropdownButton("model", kModels, n->model, [n](int i) { n->model = i; }, colW);
      {
         Ui::RowsLive on(n->model == 1);
         DropdownButton("hair colour", kHairColour, n->hairColour, [n](int i) { n->hairColour = i; }, colW);
         Ui::DrawParams(n, kMaterialParamsRows9, n->hairColour == 0);
         Ui::DrawParams(n, kMaterialParamsRows10);
      }
      if (n->model == 1 && !gParamRegisterOnly)
      {
         // The fibre axis comes from Fur's Tangent normals; any other mesh has its surface normal there.
         auto* fur = dynamic_cast<GeometryOpNode*>(n->input);
         if (!(fur && fur->op == GeometryOpNode::kFur && fur->furNormals == 1))
            ImGui::TextDisabled("needs Fur, Tangent normals");
      }

      ImGui::EndGroup();
   }


   namespace
   {
      using MappingParamsRow = params::Row<MappingNode>;
      const MappingParamsRow kMappingParamsRows1[] = {
         MappingParamsRow::Slider<&MappingNode::translateX>("x", params::Mapping::translate_x_lo, params::Mapping::translate_x_hi),
         MappingParamsRow::Slider<&MappingNode::translateY>("y", params::Mapping::translate_y_lo, params::Mapping::translate_y_hi),
      };
      const MappingParamsRow kMappingParamsRows2[] = {
         MappingParamsRow::Slider<&MappingNode::rotateX>("x", params::Mapping::rotate_x_lo, params::Mapping::rotate_x_hi).Fmt("%.1f\xC2\xB0"),
         MappingParamsRow::Slider<&MappingNode::rotateY>("y", params::Mapping::rotate_y_lo, params::Mapping::rotate_y_hi).Fmt("%.1f\xC2\xB0"),
         MappingParamsRow::Slider<&MappingNode::rotateZ>("z", params::Mapping::rotate_z_lo, params::Mapping::rotate_z_hi).Fmt("%.1f\xC2\xB0"),
      };
      const MappingParamsRow kMappingParamsRows3[] = {
         MappingParamsRow::Slider<&MappingNode::scaleX>("x", params::Mapping::scale_x_lo, params::Mapping::scale_x_hi),
         MappingParamsRow::Slider<&MappingNode::scaleY>("y", params::Mapping::scale_y_lo, params::Mapping::scale_y_hi),
      };
   }

   void DrawMappingParams(MappingNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParam(n, MappingParamsRow::Choice<&MappingNode::space>("space", &MappingNode::SpaceNames));
      if (n->space != kMapSpaceUv)
         Ui::DrawParam(n, MappingParamsRow::Slider<&MappingNode::triplanarBlend>("blend softness", 0.0f, 1.0f));
      Ui::DrawParam(n, MappingParamsRow::Slider<&MappingNode::breakup>("breakup", 0.0f, 1.0f));

      NodeSeparator("translate");
      Ui::DrawParams(n, kMappingParamsRows1);
      if (n->space != kMapSpaceUv)
         Ui::DrawParam(n, MappingParamsRow::Slider<&MappingNode::translateZ>("z", params::Mapping::translate_z_lo, params::Mapping::translate_z_hi));

      NodeSeparator("rotate");
      if (n->space == kMapSpaceUv)
      {
         Ui::DrawParam(n, MappingParamsRow::Slider<&MappingNode::rotateZ>("z", params::Mapping::rotate_z_lo, params::Mapping::rotate_z_hi).Fmt("%.1f\xC2\xB0"));
      }
      else
      {
         Ui::DrawParams(n, kMappingParamsRows2);
      }

      NodeSeparator("scale");
      Ui::DrawParams(n, kMappingParamsRows3);
      if (n->space != kMapSpaceUv)
         Ui::DrawParam(n, MappingParamsRow::Slider<&MappingNode::scaleZ>("z", params::Mapping::scale_z_lo, params::Mapping::scale_z_hi));
   }


   namespace
   {
      using ShadeCoordParamsRow = params::Row<ShadeCoordNode>;
   }

   void DrawShadeCoordParams(ShadeCoordNode* n)
   {
      Ui::DrawParam(n, ShadeCoordParamsRow::Choice<&ShadeCoordNode::output>("output", &ShadeCoordNode::OutputNames).Undo());
   }


   namespace
   {
      using ShadeMappingParamsRow = params::Row<ShadeMappingNode>;
      const ShadeMappingParamsRow kShadeMappingParamsRows1[] = {
         ShadeMappingParamsRow::Slider("x", [](ShadeMappingNode* n) { return &n->translate[0]; }, params::ShadeMapping::translate_0_lo, params::ShadeMapping::translate_0_hi),
         ShadeMappingParamsRow::Slider("y", [](ShadeMappingNode* n) { return &n->translate[1]; }, params::ShadeMapping::translate_1_lo, params::ShadeMapping::translate_1_hi),
         ShadeMappingParamsRow::Slider("z", [](ShadeMappingNode* n) { return &n->translate[2]; }, params::ShadeMapping::translate_2_lo, params::ShadeMapping::translate_2_hi),
      };
      const ShadeMappingParamsRow kShadeMappingParamsRows2[] = {
         ShadeMappingParamsRow::Slider("x", [](ShadeMappingNode* n) { return &n->rotate[0]; }, params::ShadeMapping::rotate_0_lo, params::ShadeMapping::rotate_0_hi).Fmt("%.1f\xC2\xB0"),
         ShadeMappingParamsRow::Slider("y", [](ShadeMappingNode* n) { return &n->rotate[1]; }, params::ShadeMapping::rotate_1_lo, params::ShadeMapping::rotate_1_hi).Fmt("%.1f\xC2\xB0"),
         ShadeMappingParamsRow::Slider("z", [](ShadeMappingNode* n) { return &n->rotate[2]; }, params::ShadeMapping::rotate_2_lo, params::ShadeMapping::rotate_2_hi).Fmt("%.1f\xC2\xB0"),
      };
      const ShadeMappingParamsRow kShadeMappingParamsRows3[] = {
         ShadeMappingParamsRow::Slider("x", [](ShadeMappingNode* n) { return &n->scale[0]; }, params::ShadeMapping::scale_0_lo, params::ShadeMapping::scale_0_hi),
         ShadeMappingParamsRow::Slider("y", [](ShadeMappingNode* n) { return &n->scale[1]; }, params::ShadeMapping::scale_1_lo, params::ShadeMapping::scale_1_hi),
         ShadeMappingParamsRow::Slider("z", [](ShadeMappingNode* n) { return &n->scale[2]; }, params::ShadeMapping::scale_2_lo, params::ShadeMapping::scale_2_hi),
      };
   }

   void DrawShadeMappingParams(ShadeMappingNode* n)
   {
      Ui::DrawParam(n, ShadeMappingParamsRow::Choice<&ShadeMappingNode::kind>("type", &ShadeMappingNode::KindNames).Undo());

      NodeSeparator("translate");
      Ui::DrawParams(n, kShadeMappingParamsRows1);

      NodeSeparator("rotate");
      Ui::DrawParams(n, kShadeMappingParamsRows2);

      NodeSeparator("scale");
      Ui::DrawParams(n, kShadeMappingParamsRows3);
   }


   namespace
   {
      using ShadeCheckerParamsRow = params::Row<ShadeCheckerNode>;
      const ShadeCheckerParamsRow kShadeCheckerParamsRows[] = {
         ShadeCheckerParamsRow::Colour<&ShadeCheckerNode::color1>("colour 1"),
         ShadeCheckerParamsRow::Colour<&ShadeCheckerNode::color2>("colour 2"),
         ShadeCheckerParamsRow::Slider<&ShadeCheckerNode::scale>("scale", params::ShadeChecker::scale_lo, params::ShadeChecker::scale_hi),
      };
   }

   void DrawShadeCheckerParams(ShadeCheckerNode* n)
   {
      Ui::DrawParams(n, kShadeCheckerParamsRows);
   }


   namespace
   {
      using ShadeNoiseParamsRow = params::Row<ShadeNoiseNode>;
      const ShadeNoiseParamsRow kShadeNoiseParamsRows[] = {
         ShadeNoiseParamsRow::Choice<&ShadeNoiseNode::output>("output", &ShadeNoiseNode::OutputNames).Undo(),
         ShadeNoiseParamsRow::Slider<&ShadeNoiseNode::scale>("scale", params::ShadeNoise::scale_lo, params::ShadeNoise::scale_hi),
         ShadeNoiseParamsRow::Slider<&ShadeNoiseNode::detail>("detail", params::ShadeNoise::detail_lo, params::ShadeNoise::detail_hi).Fmt("%.0f").Step(1.0f),
         ShadeNoiseParamsRow::Slider<&ShadeNoiseNode::roughness>("roughness", 0.0f, 1.0f),
         ShadeNoiseParamsRow::Slider<&ShadeNoiseNode::distortion>("distortion", params::ShadeNoise::distortion_lo, params::ShadeNoise::distortion_hi),
      };
   }

   void DrawShadeNoiseParams(ShadeNoiseNode* n)
   {
      Ui::DrawParams(n, kShadeNoiseParamsRows);
   }


   namespace
   {
      using ShadeVoronoiParamsRow = params::Row<ShadeVoronoiNode>;
      const ShadeVoronoiParamsRow kShadeVoronoiParamsRows[] = {
         ShadeVoronoiParamsRow::Choice<&ShadeVoronoiNode::output>("output", &ShadeVoronoiNode::OutputNames).Undo(),
         ShadeVoronoiParamsRow::Slider<&ShadeVoronoiNode::scale>("scale", params::ShadeVoronoi::scale_lo, params::ShadeVoronoi::scale_hi),
      };
   }

   void DrawShadeVoronoiParams(ShadeVoronoiNode* n)
   {
      Ui::DrawParams(n, kShadeVoronoiParamsRows);
   }


   namespace
   {
      using ShadeGradientParamsRow = params::Row<ShadeGradientNode>;
      const ShadeGradientParamsRow kShadeGradientParamsRows[] = {
         ShadeGradientParamsRow::Choice<&ShadeGradientNode::kind>("type", &ShadeGradientNode::KindNames).Undo(),
         ShadeGradientParamsRow::Slider<&ShadeGradientNode::scale>("scale", params::ShadeGradient::scale_lo, params::ShadeGradient::scale_hi),
      };
   }

   void DrawShadeGradientParams(ShadeGradientNode* n)
   {
      Ui::DrawParams(n, kShadeGradientParamsRows);
   }


   namespace
   {
      using ShadeBrickParamsRow = params::Row<ShadeBrickNode>;
      const ShadeBrickParamsRow kShadeBrickParamsRows[] = {
         ShadeBrickParamsRow::Colour<&ShadeBrickNode::color1>("colour 1"),
         ShadeBrickParamsRow::Colour<&ShadeBrickNode::color2>("colour 2"),
         ShadeBrickParamsRow::Colour<&ShadeBrickNode::mortarColor>("mortar"),
         ShadeBrickParamsRow::Slider<&ShadeBrickNode::scale>("scale", params::ShadeBrick::scale_lo, params::ShadeBrick::scale_hi),
         ShadeBrickParamsRow::Slider<&ShadeBrickNode::width>("width", params::ShadeBrick::width_lo, params::ShadeBrick::width_hi),
         ShadeBrickParamsRow::Slider<&ShadeBrickNode::height>("height", params::ShadeBrick::height_lo, params::ShadeBrick::height_hi),
         ShadeBrickParamsRow::Slider<&ShadeBrickNode::offset>("offset", 0.0f, 1.0f),
         ShadeBrickParamsRow::Slider<&ShadeBrickNode::mortar>("mortar size", params::ShadeBrick::mortar_lo, params::ShadeBrick::mortar_hi),
         ShadeBrickParamsRow::Slider<&ShadeBrickNode::tint>("tint", 0.0f, 1.0f),
      };
   }

   void DrawShadeBrickParams(ShadeBrickNode* n)
   {
      Ui::DrawParams(n, kShadeBrickParamsRows);
   }


   namespace
   {
      using ShadeImageParamsRow = params::Row<ShadeImageNode>;
      const ShadeImageParamsRow kShadeImageParamsRows[] = {
         ShadeImageParamsRow::Choice<&ShadeImageNode::projection>("projection", &ShadeImageNode::ProjectionNames).Undo(),
         ShadeImageParamsRow::Choice<&ShadeImageNode::space>("space", &ShadeImageNode::SpaceNames).Undo(),
      };
   }

   void DrawShadeImageParams(ShadeImageNode* n)
   {
      Ui::DrawParams(n, kShadeImageParamsRows);
      if (n->projection == ShadeImageNode::kBox)
         Ui::DrawParam(n, ShadeImageParamsRow::Slider<&ShadeImageNode::blend>("blend", 0.0f, 1.0f));
   }


   namespace
   {
      using ShadeMathParamsRow = params::Row<ShadeMathNode>;
   }

   void DrawShadeMathParams(ShadeMathNode* n)
   {
      Ui::DrawParam(n, ShadeMathParamsRow::Choice<&ShadeMathNode::op>("op", &ShadeMathNode::OpNames).Undo());
      const int k = ShadeMathNode::Operands(n->op);
      static const char* kNames[3] = { "a", "b", "c" };
      for (int i = 0; i < k; i++)
         ModSlider(kNames[i], &n->value[i], params::ShadeMath::value_lo, params::ShadeMath::value_hi);
      Ui::DrawParam(n, ShadeMathParamsRow::Toggle<&ShadeMathNode::clampResult>("clamp"));
   }


   namespace
   {
      using ShadeVectorMathParamsRow = params::Row<ShadeVectorMathNode>;
   }

   void DrawShadeVectorMathParams(ShadeVectorMathNode* n)
   {
      Ui::DrawParam(n, ShadeVectorMathParamsRow::Choice<&ShadeVectorMathNode::op>("op", &ShadeVectorMathNode::OpNames).Undo());
      const int k = ShadeVectorMathNode::Operands(n->op);
      float* vecs[3] = { n->a, n->b, n->c };
      static const char* kLabels[3][3] = { { "a x", "a y", "a z" }, { "b x", "b y", "b z" }, { "c x", "c y", "c z" } };
      static const char* kParams[3][3] = { { "aX", "aY", "aZ" }, { "bX", "bY", "bZ" }, { "cX", "cY", "cZ" } };
      for (int v = 0; v < k; v++)
         for (int c = 0; c < 3; c++)
            ModSlider(kLabels[v][c], &vecs[v][c], params::geometry_params::vecs_lo, params::geometry_params::vecs_hi, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kParams[v][c]);
      if (ShadeVectorMathNode::UsesScale(n->op))
         Ui::DrawParam(n, ShadeVectorMathParamsRow::Slider<&ShadeVectorMathNode::scale>("scale", params::ShadeVectorMath::scale_lo, params::ShadeVectorMath::scale_hi));
   }


   namespace
   {
      using ShadeMixParamsRow = params::Row<ShadeMixNode>;
      const ShadeMixParamsRow kShadeMixParamsRows1[] = {
         ShadeMixParamsRow::Slider("a", [](ShadeMixNode* n) { return &n->a[0]; }, params::ShadeMix::a_0_lo, params::ShadeMix::a_0_hi),
         ShadeMixParamsRow::Slider("b", [](ShadeMixNode* n) { return &n->b[0]; }, params::ShadeMix::b_0_lo, params::ShadeMix::b_0_hi),
      };
      const ShadeMixParamsRow kShadeMixParamsRows2[] = {
         ShadeMixParamsRow::Colour<&ShadeMixNode::a>("a"),
         ShadeMixParamsRow::Colour<&ShadeMixNode::b>("b"),
      };
      const ShadeMixParamsRow kShadeMixParamsRows3[] = {
         ShadeMixParamsRow::Toggle<&ShadeMixNode::clampFactor>("clamp factor"),
         ShadeMixParamsRow::Toggle<&ShadeMixNode::clampResult>("clamp result"),
      };
   }

   void DrawShadeMixParams(ShadeMixNode* n)
   {
      Ui::DrawParam(n, ShadeMixParamsRow::Choice<&ShadeMixNode::dataType>("type", &ShadeMixNode::DataTypeNames).Undo());
      if (n->dataType == ShadeMixNode::kColour)
         Ui::DrawParam(n, ShadeMixParamsRow::Choice<&ShadeMixNode::mode>("mode", &ShadeMixNode::ModeNames).Undo());
      Ui::DrawParam(n, ShadeMixParamsRow::Slider<&ShadeMixNode::factor>("factor", 0.0f, 1.0f));
      if (n->dataType == ShadeMixNode::kFloat)
      {
         Ui::DrawParams(n, kShadeMixParamsRows1);
      }
      else if (n->dataType == ShadeMixNode::kVector)
      {
         static const char* kA[3] = { "a x", "a y", "a z" };
         static const char* kB[3] = { "b x", "b y", "b z" };
         for (int c = 0; c < 3; c++) ModSlider(kA[c], &n->a[c], params::ShadeMix::a_lo, params::ShadeMix::a_hi);
         for (int c = 0; c < 3; c++) ModSlider(kB[c], &n->b[c], params::ShadeMix::b_lo, params::ShadeMix::b_hi);
      }
      else
      {
         Ui::DrawParams(n, kShadeMixParamsRows2);
      }
      Ui::DrawParams(n, kShadeMixParamsRows3);
   }


   namespace
   {
      using ShadeRampParamsRow = params::Row<ShadeRampNode>;
      const ShadeRampParamsRow kShadeRampParamsRows[] = {
         ShadeRampParamsRow::Choice<&ShadeRampNode::interp>("interpolation", &ShadeRampNode::InterpNames).Undo(),
         ShadeRampParamsRow::Slider<&ShadeRampNode::factor>("factor", 0.0f, 1.0f),
         ShadeRampParamsRow::SliderInt<&ShadeRampNode::count>("stops", params::ShadeRamp::count_lo, ShadeRampNode::kMaxStops),
      };
   }

   void DrawShadeRampParams(ShadeRampNode* n)
   {
      Ui::DrawParams(n, kShadeRampParamsRows);
      static const char* kPos[ShadeRampNode::kMaxStops] = { "pos0", "pos1", "pos2", "pos3", "pos4", "pos5", "pos6", "pos7" };
      for (int i = 0; i < n->count && i < ShadeRampNode::kMaxStops; i++)
      {
         ImGui::PushID(i);
         ModSlider("position", &n->pos[i], 0.0f, 1.0f, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kPos[i]);
         ColorSwatch("colour", n->color[i], n);
         ImGui::PopID();
      }
   }


   namespace
   {
      using ShadeMapRangeParamsRow = params::Row<ShadeMapRangeNode>;
      const ShadeMapRangeParamsRow kShadeMapRangeParamsRows[] = {
         ShadeMapRangeParamsRow::Choice<&ShadeMapRangeNode::interp>("interpolation", &ShadeMapRangeNode::InterpNames).Undo(),
         ShadeMapRangeParamsRow::Slider<&ShadeMapRangeNode::value>("value", params::ShadeMapRange::value_lo, params::ShadeMapRange::value_hi),
         ShadeMapRangeParamsRow::Slider<&ShadeMapRangeNode::fromMin>("from min", params::ShadeMapRange::from_min_lo, params::ShadeMapRange::from_min_hi).Named("fromMin"),
         ShadeMapRangeParamsRow::Slider<&ShadeMapRangeNode::fromMax>("from max", params::ShadeMapRange::from_max_lo, params::ShadeMapRange::from_max_hi).Named("fromMax"),
         ShadeMapRangeParamsRow::Slider<&ShadeMapRangeNode::toMin>("to min", params::ShadeMapRange::to_min_lo, params::ShadeMapRange::to_min_hi).Named("toMin"),
         ShadeMapRangeParamsRow::Slider<&ShadeMapRangeNode::toMax>("to max", params::ShadeMapRange::to_max_lo, params::ShadeMapRange::to_max_hi).Named("toMax"),
      };
   }

   void DrawShadeMapRangeParams(ShadeMapRangeNode* n)
   {
      Ui::DrawParams(n, kShadeMapRangeParamsRows);
      if (n->interp == ShadeMapRangeNode::kStepped)
         Ui::DrawParam(n, ShadeMapRangeParamsRow::Slider<&ShadeMapRangeNode::steps>("steps", params::ShadeMapRange::steps_lo, params::ShadeMapRange::steps_hi).Fmt("%.0f").Step(1.0f));
      if (n->interp == ShadeMapRangeNode::kLinear || n->interp == ShadeMapRangeNode::kStepped)
         Ui::DrawParam(n, ShadeMapRangeParamsRow::Toggle<&ShadeMapRangeNode::clampResult>("clamp"));
   }


   void DrawShadeSeparateParams(ShadeSeparateNode* n)
   {
      DropdownButton("mode", ShadeSeparateNode::ModeNames(), n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; n->channel = 0; });
      DropdownButton("channel", ShadeSeparateNode::ChannelNames(n->mode), n->channel, [n](int i) { PushUndoCheckpoint(); n->channel = i; });
   }


   namespace
   {
      using ShadeCombineParamsRow = params::Row<ShadeCombineNode>;
   }

   void DrawShadeCombineParams(ShadeCombineNode* n)
   {
      Ui::DrawParam(n, ShadeCombineParamsRow::Choice<&ShadeCombineNode::mode>("mode", &ShadeSeparateNode::ModeNames).Undo());
      static const char* kXyz[3] = { "x", "y", "z" };
      static const char* kHsv[3] = { "hue", "saturation", "value" };
      const char* const* lab = n->mode == 1 ? kHsv : kXyz;
      for (int i = 0; i < 3; i++)
         ModSlider(lab[i], &n->value[i], 0.0f, 1.0f, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kXyz[i]);
   }


   namespace
   {
      using ShadeCurvesParamsRow = params::Row<ShadeCurvesNode>;
      const ShadeCurvesParamsRow kShadeCurvesParamsRows[] = {
         ShadeCurvesParamsRow::Slider<&ShadeCurvesNode::factor>("factor", 0.0f, 1.0f),
         ShadeCurvesParamsRow::SliderInt<&ShadeCurvesNode::count>("points", params::ShadeCurves::count_lo, ShadeCurvesNode::kMaxPoints),
      };
   }

   void DrawShadeCurvesParams(ShadeCurvesNode* n)
   {
      Ui::DrawParams(n, kShadeCurvesParamsRows);
      static const char* kX[ShadeCurvesNode::kMaxPoints] = { "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7" };
      static const char* kY[ShadeCurvesNode::kMaxPoints] = { "y0", "y1", "y2", "y3", "y4", "y5", "y6", "y7" };
      for (int i = 0; i < n->count && i < ShadeCurvesNode::kMaxPoints; i++)
      {
         ImGui::PushID(i);
         ModSlider("x", &n->px[i], 0.0f, 1.0f, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kX[i]);
         ModSlider("y", &n->py[i], 0.0f, 1.0f, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kY[i]);
         ImGui::PopID();
      }
   }


   namespace
   {
      using ShadeBumpParamsRow = params::Row<ShadeBumpNode>;
      const ShadeBumpParamsRow kShadeBumpParamsRows[] = {
         ShadeBumpParamsRow::Slider<&ShadeBumpNode::strength>("strength", 0.0f, 1.0f),
         ShadeBumpParamsRow::Slider<&ShadeBumpNode::distance>("distance", 0.0f, 1.0f),
         ShadeBumpParamsRow::Toggle<&ShadeBumpNode::invert>("invert"),
      };
   }

   void DrawShadeBumpParams(ShadeBumpNode* n)
   {
      Ui::DrawParams(n, kShadeBumpParamsRows);
   }


   namespace
   {
      using ShadeNormalMapParamsRow = params::Row<ShadeNormalMapNode>;
   }

   void DrawShadeNormalMapParams(ShadeNormalMapNode* n)
   {
      Ui::DrawParam(n, ShadeNormalMapParamsRow::Slider<&ShadeNormalMapNode::strength>("strength", params::ShadeNormalMap::strength_lo, params::ShadeNormalMap::strength_hi));
   }


   namespace
   {
      using ShadeFresnelParamsRow = params::Row<ShadeFresnelNode>;
   }

   void DrawShadeFresnelParams(ShadeFresnelNode* n)
   {
      Ui::DrawParam(n, ShadeFresnelParamsRow::Slider<&ShadeFresnelNode::ior>("ior", params::ShadeFresnel::ior_lo, params::ShadeFresnel::ior_hi));
   }


   namespace
   {
      using ShadeLayerWeightParamsRow = params::Row<ShadeLayerWeightNode>;
      const ShadeLayerWeightParamsRow kShadeLayerWeightParamsRows[] = {
         ShadeLayerWeightParamsRow::Choice<&ShadeLayerWeightNode::output>("output", &ShadeLayerWeightNode::OutputNames).Undo(),
         ShadeLayerWeightParamsRow::Slider<&ShadeLayerWeightNode::blend>("blend", 0.0f, 1.0f),
      };
   }

   void DrawShadeLayerWeightParams(ShadeLayerWeightNode* n)
   {
      Ui::DrawParams(n, kShadeLayerWeightParamsRows);
   }


   namespace
   {
      using ShadeGeometryParamsRow = params::Row<ShadeGeometryNode>;
   }

   void DrawShadeGeometryParams(ShadeGeometryNode* n)
   {
      Ui::DrawParam(n, ShadeGeometryParamsRow::Choice<&ShadeGeometryNode::output>("output", &ShadeGeometryNode::OutputNames).Undo());
   }


   namespace
   {
      using MeshResynthParamsRow = params::Row<MeshResynthNode>;
      const MeshResynthParamsRow kMeshResynthParamsRows1[] = {
         MeshResynthParamsRow::Slider<&MeshResynthNode::chaos>("chaos", params::MeshResynth::chaos_lo, params::MeshResynth::chaos_hi),
         MeshResynthParamsRow::Toggle<&MeshResynthNode::autoStep>("auto step"),
      };
      const MeshResynthParamsRow kMeshResynthParamsRows2[] = {
         MeshResynthParamsRow::Slider<&MeshResynthNode::seed>("seed", params::MeshResynth::seed_lo, params::MeshResynth::seed_hi).Fmt("%.0f").Step(1.0f),
         MeshResynthParamsRow::SliderInt<&MeshResynthNode::triangleBudget>("triangle budget", params::MeshResynth::triangle_budget_lo, params::MeshResynth::triangle_budget_hi),
      };
   }

   void DrawMeshResynthParams(MeshResynthNode* n)
   {
      ImGui::TextDisabled("generation %d, %s triangles", n->Generation(), UiFmt::Count((long long)(n->TriangleCount())).c);
      if (ActionButton::Draw("step")) n->StepOnce();
      Ui::SameRow();
      if (ActionButton::Draw("reset")) n->Reset();
      Ui::SameRow();
      if (ActionButton::Draw("randomise")) n->Randomise();

      NodeSeparator("evolve");
      Ui::DrawParams(n, kMeshResynthParamsRows1);
      if (n->autoStep)
         Ui::DrawParam(n, MeshResynthParamsRow::Slider<&MeshResynthNode::stepsPerBeat>("steps per beat", params::MeshResynth::steps_per_beat_lo, params::MeshResynth::steps_per_beat_hi));
      Ui::DrawParams(n, kMeshResynthParamsRows2);

      NodeSeparator("operators");
      for (int i = 0; i < MeshResynthNode::kOpCount; i++)
         ModSlider(MeshResynthNode::OpNames()[i].c_str(), &n->weight[i], 0.0f, 1.0f);
   }


   namespace
   {
      using ImageToPointsParamsRow = params::Row<ImageToPointsNode>;
      const ImageToPointsParamsRow kImageToPointsParamsRows1[] = {
         ImageToPointsParamsRow::SliderInt<&ImageToPointsNode::density>("density", params::ImageToPoints::density_lo, params::ImageToPoints::density_hi),
         ImageToPointsParamsRow::Slider<&ImageToPointsNode::width>("width", params::ImageToPoints::width_lo, params::ImageToPoints::width_hi),
         ImageToPointsParamsRow::Slider<&ImageToPointsNode::height>("height", params::ImageToPoints::height_lo, params::ImageToPoints::height_hi),
         ImageToPointsParamsRow::Slider<&ImageToPointsNode::threshold>("threshold", 0.0f, 1.0f),
      };
      const ImageToPointsParamsRow kImageToPointsParamsRows2[] = {
         ImageToPointsParamsRow::Choice<&ImageToPointsNode::depthSource>("from", &ImageToPointsNode::DepthSourceNames),
         ImageToPointsParamsRow::Slider<&ImageToPointsNode::depthScale>("depth scale", params::ImageToPoints::depth_scale_lo, params::ImageToPoints::depth_scale_hi),
      };
      const ImageToPointsParamsRow kImageToPointsParamsRows3[] = {
         ImageToPointsParamsRow::Slider<&ImageToPointsNode::pointSize>("point size", params::ImageToPoints::point_size_lo, params::ImageToPoints::point_size_hi),
         ImageToPointsParamsRow::Slider<&ImageToPointsNode::sizeFromLuma>("size from luma", -1.0f, 1.0f),
         ImageToPointsParamsRow::Toggle<&ImageToPointsNode::useImageColor>("use image colour"),
         ImageToPointsParamsRow::Colour<&ImageToPointsNode::tint>("tint"),
      };
   }

   void DrawImageToPointsParams(ImageToPointsNode* n)
   {
      ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      Ui::DrawParams(n, kImageToPointsParamsRows1);

      NodeSeparator("depth");
      Ui::DrawParams(n, kImageToPointsParamsRows2);

      NodeSeparator("points");
      Ui::DrawParams(n, kImageToPointsParamsRows3);
   }


   namespace
   {
      using DepthProjectionParamsRow = params::Row<DepthProjectionNode>;
      const DepthProjectionParamsRow kDepthProjectionParamsRows1[] = {
         DepthProjectionParamsRow::Choice<&DepthProjectionNode::projection>("projection", &DepthProjectionNode::ProjectionNames),
         DepthProjectionParamsRow::Choice<&DepthProjectionNode::outputType>("output", &DepthProjectionNode::OutputTypeNames),
         DepthProjectionParamsRow::SliderInt<&DepthProjectionNode::density>("density", params::DepthProjection::density_lo, params::DepthProjection::density_hi),
      };
      const DepthProjectionParamsRow kDepthProjectionParamsRows2[] = {
         DepthProjectionParamsRow::Choice<&DepthProjectionNode::depthSource>("source", &DepthProjectionNode::DepthSourceNames),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::nearDepth>("near depth", params::DepthProjection::near_depth_lo, params::DepthProjection::near_depth_hi),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::farDepth>("far depth", params::DepthProjection::far_depth_lo, params::DepthProjection::far_depth_hi),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::depthScale>("depth scale", params::DepthProjection::depth_scale_lo, params::DepthProjection::depth_scale_hi),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::depthCurve>("depth curve", params::DepthProjection::depth_curve_lo, params::DepthProjection::depth_curve_hi),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::clipNear>("clip near", 0.0f, 1.0f),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::clipFar>("clip far", 0.0f, 1.0f),
      };
      const DepthProjectionParamsRow kDepthProjectionParamsRows3[] = {
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::fov>("fov", params::DepthProjection::fov_lo, params::DepthProjection::fov_hi),
         DepthProjectionParamsRow::Toggle<&DepthProjectionNode::autoAspect>("auto aspect"),
      };
      const DepthProjectionParamsRow kDepthProjectionParamsRows4[] = {
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::focalScale>("focal scale", params::DepthProjection::focal_scale_lo, params::DepthProjection::focal_scale_hi),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::principalPointX>("center x", -1.0f, 1.0f),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::principalPointY>("center y", -1.0f, 1.0f),
      };
      const DepthProjectionParamsRow kDepthProjectionParamsRows5[] = {
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::planarWidth>("width", params::DepthProjection::planar_width_lo, params::DepthProjection::planar_width_hi),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::planarHeight>("height", params::DepthProjection::planar_height_lo, params::DepthProjection::planar_height_hi),
      };
      const DepthProjectionParamsRow kDepthProjectionParamsRows6[] = {
         DepthProjectionParamsRow::Choice<&DepthProjectionNode::colorMode>("color", &DepthProjectionNode::ColorModeNames),
         DepthProjectionParamsRow::Colour<&DepthProjectionNode::tint>("tint"),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::metallic>("metallic", 0.0f, 1.0f),
         DepthProjectionParamsRow::Slider<&DepthProjectionNode::roughness>("roughness", 0.0f, 1.0f),
      };
   }

   void DrawDepthProjectionParams(DepthProjectionNode* n)
   {
      if (n->outputType == DepthProjectionNode::kPoints)
         ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      else
         ImGui::TextDisabled("%s triangles (%s vertices)", UiFmt::Count((long long)(n->TriangleCount())).c, UiFmt::Count((long long)(n->GetMesh().vertices.size())).c);

      Ui::DrawParams(n, kDepthProjectionParamsRows1);

      NodeSeparator("depth");
      Ui::DrawParams(n, kDepthProjectionParamsRows2);

      if (n->projection == DepthProjectionNode::kPerspective || n->projection == DepthProjectionNode::kRadial)
      {
         NodeSeparator("camera");
         Ui::DrawParams(n, kDepthProjectionParamsRows3);
         if (!n->autoAspect)
            Ui::DrawParam(n, DepthProjectionParamsRow::Slider<&DepthProjectionNode::customAspect>("aspect", params::DepthProjection::custom_aspect_lo, params::DepthProjection::custom_aspect_hi));
         Ui::DrawParams(n, kDepthProjectionParamsRows4);
      }
      else
      {
         NodeSeparator("dimensions");
         Ui::DrawParams(n, kDepthProjectionParamsRows5);
      }

      NodeSeparator("appearance");
      if (n->outputType == DepthProjectionNode::kPoints)
         Ui::DrawParam(n, DepthProjectionParamsRow::Slider<&DepthProjectionNode::pointSize>("point size", params::DepthProjection::point_size_lo, params::DepthProjection::point_size_hi));
      else
         Ui::DrawParam(n, DepthProjectionParamsRow::Slider<&DepthProjectionNode::edgeTearThreshold>("edge tear", params::DepthProjection::edge_tear_threshold_lo, params::DepthProjection::edge_tear_threshold_hi));

      Ui::DrawParams(n, kDepthProjectionParamsRows6);
   }


   namespace
   {
      using MeshToPointsParamsRow = params::Row<MeshToPointsNode>;
      const MeshToPointsParamsRow kMeshToPointsParamsRows[] = {
         MeshToPointsParamsRow::SliderInt<&MeshToPointsNode::maxPoints>("max points", params::geometry_params::max_points_lo, params::geometry_params::max_points_hi),
         MeshToPointsParamsRow::Slider<&MeshToPointsNode::pointSize>("point size", params::geometry_params::point_size_lo, params::geometry_params::point_size_hi),
         MeshToPointsParamsRow::Toggle<&MeshToPointsNode::weld>("weld seams"),
      };
   }

   void DrawMeshToPointsParams(MeshToPointsNode* n)
   {
      Ui::DrawParam(n, MeshToPointsParamsRow::Choice<&MeshToPointsNode::mode>("sample", &MeshToPointsNode::ModeNames));
      ImGui::TextDisabled("%s points, %s triangles", UiFmt::Count((long long)(n->PointCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParams(n, kMeshToPointsParamsRows);
      if (n->mode == 1)
         Ui::DrawParam(n, MeshToPointsParamsRow::Slider<&MeshToPointsNode::dissolveAngleDegrees>("dissolve angle", params::geometry_params::dissolve_angle_degrees_lo, params::geometry_params::dissolve_angle_degrees_hi));
   }


   namespace
   {
      using DistributePointsOnFacesParamsRow = params::Row<DistributePointsOnFacesNode>;
      const DistributePointsOnFacesParamsRow kDistributePointsOnFacesParamsRows1[] = {
         DistributePointsOnFacesParamsRow::Slider<&DistributePointsOnFacesNode::density>("density", params::DistributePointsOnFaces::density_lo, params::DistributePointsOnFaces::density_hi).Fmt("%.0f").Curve(LogPosToValue, LogValueToPos),
         DistributePointsOnFacesParamsRow::Choice<&DistributePointsOnFacesNode::method>("method", &DistributePointsOnFacesNode::MethodNames),
      };
      const DistributePointsOnFacesParamsRow kDistributePointsOnFacesParamsRows2[] = {
         DistributePointsOnFacesParamsRow::Slider<&DistributePointsOnFacesNode::pointSize>("point size", params::DistributePointsOnFaces::point_size_lo, params::DistributePointsOnFaces::point_size_hi),
         DistributePointsOnFacesParamsRow::Slider<&DistributePointsOnFacesNode::seed>("seed", params::DistributePointsOnFaces::seed_lo, params::DistributePointsOnFaces::seed_hi).Fmt("%.0f").Step(1.0f),
      };
   }

   void DrawDistributePointsOnFacesParams(DistributePointsOnFacesNode* n)
   {
      ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      Ui::DrawParams(n, kDistributePointsOnFacesParamsRows1);
      if (n->method == MeshOps::kDistributePoisson)
         Ui::DrawParam(n, DistributePointsOnFacesParamsRow::Slider<&DistributePointsOnFacesNode::minDistance>("min distance", params::DistributePointsOnFaces::min_distance_lo, params::DistributePointsOnFaces::min_distance_hi));
      Ui::DrawParams(n, kDistributePointsOnFacesParamsRows2);
   }


   namespace
   {
      using PointsToVerticesParamsRow = params::Row<PointsToVerticesNode>;
   }

   void DrawPointsToVerticesParams(PointsToVerticesNode* n)
   {
      ImGui::TextDisabled("%s vertices", UiFmt::Count((long long)(n->VertexCount())).c);
      Ui::DrawParam(n, PointsToVerticesParamsRow::Toggle<&PointsToVerticesNode::aliveOnly>("alive only"));
   }


   namespace
   {
      using ProximityLinesParamsRow = params::Row<ProximityLinesNode>;
      const ProximityLinesParamsRow kProximityLinesParamsRows[] = {
         ProximityLinesParamsRow::Slider<&ProximityLinesNode::radius>("radius", params::ProximityLines::radius_lo, params::ProximityLines::radius_hi),
         ProximityLinesParamsRow::SliderInt<&ProximityLinesNode::maxPerPoint>("max per point", params::ProximityLines::max_per_point_lo, params::ProximityLines::max_per_point_hi),
         ProximityLinesParamsRow::SliderInt<&ProximityLinesNode::maxLinks>("max lines", params::ProximityLines::max_links_lo, params::ProximityLines::max_links_hi),
         ProximityLinesParamsRow::Slider<&ProximityLinesNode::thickness>("thickness", params::ProximityLines::thickness_lo, params::ProximityLines::thickness_hi).Fmt("%.4f"),
         ProximityLinesParamsRow::Toggle<&ProximityLinesNode::aliveOnly>("alive only"),
      };
   }

   void DrawProximityLinesParams(ProximityLinesNode* n)
   {
      ImGui::TextDisabled("%s lines", UiFmt::Count((long long)(n->LinkCount())).c);
      Ui::DrawParams(n, kProximityLinesParamsRows);
   }


   namespace
   {
      using PointTrailsParamsRow = params::Row<PointTrailsNode>;
      const PointTrailsParamsRow kPointTrailsParamsRows1[] = {
         PointTrailsParamsRow::SliderInt<&PointTrailsNode::length>("length", params::PointTrails::length_lo, params::PointTrails::length_hi),
         PointTrailsParamsRow::SliderInt<&PointTrailsNode::sampleEvery>("sample every", params::PointTrails::sample_every_lo, params::PointTrails::sample_every_hi),
         PointTrailsParamsRow::Slider<&PointTrailsNode::width>("width", params::PointTrails::width_lo, params::PointTrails::width_hi).Fmt("%.4f"),
         PointTrailsParamsRow::Slider<&PointTrailsNode::fade>("fade", 0.0f, 1.0f),
         PointTrailsParamsRow::Slider<&PointTrailsNode::taper>("taper", 0.0f, 1.0f),
         PointTrailsParamsRow::Choice<&PointTrailsNode::colorMode>("colour", &PointTrailsNode::ColorModeNames).Undo(),
      };
      const PointTrailsParamsRow kPointTrailsParamsRows2[] = {
         PointTrailsParamsRow::Colour<&PointTrailsNode::headColor>("head"),
         PointTrailsParamsRow::Colour<&PointTrailsNode::tailColor>("tail"),
      };
      const PointTrailsParamsRow kPointTrailsParamsRows3[] = {
         PointTrailsParamsRow::Choice<&PointTrailsNode::matchMode>("match by", &PointTrailsNode::MatchModeNames).Undo(),
         PointTrailsParamsRow::SliderInt<&PointTrailsNode::pointLimit>("point limit", params::PointTrails::point_limit_lo, params::PointTrails::point_limit_hi),
      };
   }

   void DrawPointTrailsParams(PointTrailsNode* n)
   {
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%zu tails, %s points tracked", n->TrailCount(), UiFmt::Count((long long)(n->TrackedPoints())).c);
      if (n->input != nullptr && n->TrailCount() == 0)
         ImGui::TextDisabled("no trails yet: the points are not moving");
      else if (n->input != nullptr)
         ImGui::TextDisabled("outputs only the tails: patch the source into Render 3D too");
      if (n->ReadsBackFromGpu())
         ImGui::TextDisabled("GPU points: first %d read back per sample", n->TrackedPoints());
      PopTextWrapWidth();
      Ui::DrawParams(n, kPointTrailsParamsRows1);
      if (n->colorMode == PointTrailsNode::kColorGradient)
      {
         Ui::DrawParams(n, kPointTrailsParamsRows2);
      }
      Ui::DrawParams(n, kPointTrailsParamsRows3);
      if (ActionButton::Draw("Clear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }


   namespace
   {
      using CurveOpsParamsRow = params::Row<CurveOpsNode>;
      const CurveOpsParamsRow kCurveOpsParamsRows1[] = {
         CurveOpsParamsRow::SliderInt<&CurveOpsNode::iterations>("iterations", params::CurveOps::iterations_lo, params::CurveOps::iterations_hi),
         CurveOpsParamsRow::Slider<&CurveOpsNode::strength>("strength", 0.0f, 1.0f),
      };
      const CurveOpsParamsRow kCurveOpsParamsRows2[] = {
         CurveOpsParamsRow::Slider<&CurveOpsNode::distance>("distance", params::CurveOps::distance_lo, params::CurveOps::distance_hi),
         CurveOpsParamsRow::SliderInt<&CurveOpsNode::arcSteps>("arc steps", params::CurveOps::arc_steps_lo, params::CurveOps::arc_steps_hi),
      };
   }

   void DrawCurveOpsParams(CurveOpsNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s points out", UiFmt::Count((long long)(n->PointCount())).c);
      Ui::DrawParam(n, CurveOpsParamsRow::Choice<&CurveOpsNode::mode>("mode", &CurveOpsNode::ModeNames).Undo());
      switch (n->mode)
      {
         case CurveOpsNode::kSimplify:
            Ui::DrawParam(n, CurveOpsParamsRow::Slider<&CurveOpsNode::tolerance>("tolerance", params::CurveOps::tolerance_lo, params::CurveOps::tolerance_hi));
            break;
         case CurveOpsNode::kSmooth:
            Ui::DrawParams(n, kCurveOpsParamsRows1);
            break;
         case CurveOpsNode::kOffset:
            Ui::DrawParams(n, kCurveOpsParamsRows2);
            break;
         default:
            Ui::DrawParam(n, CurveOpsParamsRow::SliderInt<&CurveOpsNode::count>("count", params::CurveOps::count_lo, params::CurveOps::count_hi));
            break;
      }
      Ui::DrawParam(n, CurveOpsParamsRow::Slider<&CurveOpsNode::radius>("tube radius", params::CurveOps::radius_lo, params::CurveOps::radius_hi));
   }


   namespace
   {
      using RevolveParamsRow = params::Row<RevolveNode>;
      const RevolveParamsRow kRevolveParamsRows[] = {
         RevolveParamsRow::Choice<&RevolveNode::axis>("axis", &kAxisXYZ).Undo(),
         RevolveParamsRow::Slider<&RevolveNode::angle>("angle", params::Revolve::angle_lo, params::Revolve::angle_hi).Fmt("%.0f"),
         RevolveParamsRow::SliderInt<&RevolveNode::segments>("segments", params::Revolve::segments_lo, params::Revolve::segments_hi),
         RevolveParamsRow::Toggle<&RevolveNode::caps>("caps"),
      };
   }

   void DrawRevolveParams(RevolveNode* n)
   {
      Ui::DrawParams(n, kRevolveParamsRows);
   }


   namespace
   {
      using SweepParamsRow = params::Row<SweepNode>;
      const SweepParamsRow kSweepParamsRows1[] = {
         SweepParamsRow::Slider<&SweepNode::profileRadius>("profile radius", params::Sweep::profile_radius_lo, params::Sweep::profile_radius_hi),
         SweepParamsRow::SliderInt<&SweepNode::profileSides>("profile sides", params::Sweep::profile_sides_lo, params::Sweep::profile_sides_hi),
      };
      const SweepParamsRow kSweepParamsRows2[] = {
         SweepParamsRow::Slider<&SweepNode::startScale>("start scale", params::Sweep::start_scale_lo, params::Sweep::start_scale_hi),
         SweepParamsRow::Slider<&SweepNode::endScale>("end scale", params::Sweep::end_scale_lo, params::Sweep::end_scale_hi),
         SweepParamsRow::Slider<&SweepNode::twist>("twist", params::Sweep::twist_lo, params::Sweep::twist_hi).Fmt("%.0f"),
         SweepParamsRow::Toggle<&SweepNode::caps>("caps"),
      };
   }

   void DrawSweepParams(SweepNode* n)
   {
      if (n->profile == nullptr)
      {
         Ui::DrawParams(n, kSweepParamsRows1);
      }
      Ui::DrawParams(n, kSweepParamsRows2);
   }


   void OpenIn3DViewButton(int mode)
   {
      if (gParamRegisterOnly || !ActionButton::Draw("Open in 3D View", ImVec2(kPreviewSize, 0)))
         return;
      if (const GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex))
         View3DOpenNode(*gn, mode);
   }

   namespace
   {
      using ArmatureParamsRow = params::Row<ArmatureNode>;
      const ArmatureParamsRow kArmatureParamsRows1[] = {
         ArmatureParamsRow::SliderInt<&ArmatureNode::bones>("bones", params::Armature::bones_lo, params::Armature::bones_hi),
         ArmatureParamsRow::Slider<&ArmatureNode::envelope>("envelope", params::Armature::envelope_lo, params::Armature::envelope_hi),
      };
      const ArmatureParamsRow kArmatureParamsRows2[] = {
         ArmatureParamsRow::Slider<&ArmatureNode::curl>("curl", params::Armature::curl_lo, params::Armature::curl_hi).Fmt("%.0f"),
         ArmatureParamsRow::Slider<&ArmatureNode::curlDirection>("curl direction", params::Armature::curl_direction_lo, params::Armature::curl_direction_hi).Fmt("%.0f"),
         ArmatureParamsRow::Slider<&ArmatureNode::twist>("twist", params::Armature::twist_lo, params::Armature::twist_hi).Fmt("%.0f"),
      };
      const ArmatureParamsRow kArmatureParamsRows3[] = {
         ArmatureParamsRow::Slider<&ArmatureNode::waveAmount>("wave amount", params::Armature::wave_amount_lo, params::Armature::wave_amount_hi).Fmt("%.1f"),
         ArmatureParamsRow::Slider<&ArmatureNode::waveLength>("wave length", params::Armature::wave_length_lo, params::Armature::wave_length_hi),
         ArmatureParamsRow::Slider<&ArmatureNode::waveSpeed>("wave speed", params::Armature::wave_speed_lo, params::Armature::wave_speed_hi),
         ArmatureParamsRow::Slider<&ArmatureNode::wavePhase>("wave phase", 0.0f, 1.0f),
      };
   }

   void DrawArmatureParams(ArmatureNode* n)
   {
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      PopTextWrapWidth();
      if (n->path == nullptr || gParamRegisterOnly)
         Ui::DrawParam(n, ArmatureParamsRow::Choice<&ArmatureNode::axis>("axis", &kAxisXYZ).Undo());
      Ui::DrawParams(n, kArmatureParamsRows1);
      NodeSeparator("bend");
      Ui::DrawParams(n, kArmatureParamsRows2);
      NodeSeparator("wave");
      Ui::DrawParams(n, kArmatureParamsRows3);
      DrawRigPoseParams(n);
      OpenIn3DViewButton(3);
   }

   namespace
   {
      using DelaunayParamsRow = params::Row<DelaunayMeshNode>;
      const DelaunayParamsRow kDelaunayParamsRows[] = {
         DelaunayParamsRow::Choice<&DelaunayMeshNode::plane>("plane", &DelaunayMeshNode::PlaneNames).Undo(),
         DelaunayParamsRow::Toggle<&DelaunayMeshNode::aliveOnly>("alive only"),
         DelaunayParamsRow::SliderInt<&DelaunayMeshNode::maxPoints>("max points", params::DelaunayMesh::max_points_lo, params::DelaunayMesh::max_points_hi),
      };
   }

   void DrawDelaunayParams(DelaunayMeshNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParams(n, kDelaunayParamsRows);
      if (dynamic_cast<VoronoiCellsNode*>(n) != nullptr)
         Ui::DrawParam(n, DelaunayParamsRow::Slider<&DelaunayMeshNode::inset>("inset", params::DelaunayMesh::inset_lo, params::DelaunayMesh::inset_hi));
   }


   namespace
   {
      using DistributePointsInGridParamsRow = params::Row<DistributePointsInGridNode>;
      const DistributePointsInGridParamsRow kDistributePointsInGridParamsRows[] = {
         DistributePointsInGridParamsRow::SliderInt<&DistributePointsInGridNode::countX>("count x", params::DistributePointsInGrid::count_x_lo, params::DistributePointsInGrid::count_x_hi),
         DistributePointsInGridParamsRow::SliderInt<&DistributePointsInGridNode::countY>("count y", params::DistributePointsInGrid::count_y_lo, params::DistributePointsInGrid::count_y_hi),
         DistributePointsInGridParamsRow::Slider<&DistributePointsInGridNode::spacingX>("spacing x", params::DistributePointsInGrid::spacing_x_lo, params::DistributePointsInGrid::spacing_x_hi),
         DistributePointsInGridParamsRow::Slider<&DistributePointsInGridNode::spacingY>("spacing y", params::DistributePointsInGrid::spacing_y_lo, params::DistributePointsInGrid::spacing_y_hi),
         DistributePointsInGridParamsRow::Slider<&DistributePointsInGridNode::jitter>("jitter", 0.0f, 1.0f),
         DistributePointsInGridParamsRow::Slider<&DistributePointsInGridNode::pointSize>("point size", params::DistributePointsInGrid::point_size_lo, params::DistributePointsInGrid::point_size_hi),
         DistributePointsInGridParamsRow::Slider<&DistributePointsInGridNode::seed>("seed", params::DistributePointsInGrid::seed_lo, params::DistributePointsInGrid::seed_hi).Fmt("%.0f").Step(1.0f),
         DistributePointsInGridParamsRow::Colour<&DistributePointsInGridNode::tint>("tint"),
      };
   }

   void DrawDistributePointsInGridParams(DistributePointsInGridNode* n)
   {
      ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      Ui::DrawParams(n, kDistributePointsInGridParamsRows);
   }


   namespace
   {
      using MergeByDistanceParamsRow = params::Row<MergeByDistanceNode>;
   }

   void DrawMergeByDistanceParams(MergeByDistanceNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParam(n, MergeByDistanceParamsRow::Slider<&MergeByDistanceNode::threshold>("threshold", params::MergeByDistance::threshold_lo, params::MergeByDistance::threshold_hi).Fmt("%.4f"));
   }


   namespace
   {
      using BakeParamsRow = params::Row<BakeNode>;
      const BakeParamsRow kBakeParamsRows1[] = {
         BakeParamsRow::Choice<&BakeNode::mode>("bake", &BakeNode::ModeNames).Undo(),
         BakeParamsRow::Choice<&BakeNode::size>("size", &BakeNode::SizeNames).Undo().Segmented(),
      };
      const BakeParamsRow kBakeParamsRows2[] = {
         BakeParamsRow::SliderInt<&BakeNode::samples>("samples", params::Bake::samples_lo, params::Bake::samples_hi),
         BakeParamsRow::Slider<&BakeNode::distance>("distance", params::Bake::distance_lo, params::Bake::distance_hi),
      };
   }

   void DrawBakeParams(BakeNode* n)
   {
      Ui::DrawParams(n, kBakeParamsRows1);
      if (n->mode == BakeNode::kAO || n->mode == BakeNode::kThickness)
      {
         Ui::DrawParams(n, kBakeParamsRows2);
      }
      if (n->Baking())
         ImGui::TextDisabled("baking");
   }


   namespace
   {
      using SculptParamsRow = params::Row<SculptNode>;
      const SculptParamsRow kSculptParamsRows[] = {
         SculptParamsRow::Slider<&SculptNode::amount>("amount", params::Sculpt::amount_lo, params::Sculpt::amount_hi),
         SculptParamsRow::SliderInt<&SculptNode::level>("level", params::Sculpt::level_lo, params::Sculpt::level_hi),
      };
   }

   void DrawSculptParams(SculptNode* n)
   {
      Ui::DrawParams(n, kSculptParamsRows);
      if (!gParamRegisterOnly && n->HasSculpt() && ActionButton::Draw("Clear", ImVec2(kPreviewSize, 0)))
      {
         PushUndoCheckpoint("Clear sculpt");
         n->Clear();
      }
      OpenIn3DViewButton(2);
   }


   void DrawEditMeshParams(EditMeshNode* n)
   {
      if (gParamRegisterOnly)
         return;
      ImGui::TextDisabled("%d ops", n->OpCount());
      if (!n->ops.empty() && ActionButton::Draw("Clear", ImVec2(kPreviewSize, 0)))
      {
         PushUndoCheckpoint("Clear edits");
         n->ops.clear();
      }
      OpenIn3DViewButton(1);
   }


   namespace
   {
      using Text3DParamsRow = params::Row<Text3DNode>;
      const Text3DParamsRow kText3DParamsRows1[] = {
         Text3DParamsRow::Slider<&Text3DNode::depth>("depth", params::Text3D::depth_lo, params::Text3D::depth_hi),
         Text3DParamsRow::Slider<&Text3DNode::bevel>("bevel", params::Text3D::bevel_lo, params::Text3D::bevel_hi),
         Text3DParamsRow::Slider<&Text3DNode::letterSpacing>("tracking", params::Text3D::letter_spacing_lo, params::Text3D::letter_spacing_hi),
      };
      const Text3DParamsRow kText3DParamsRows2[] = {
         Text3DParamsRow::Slider<&Text3DNode::posX>("pos X", params::Text3D::pos_x_lo, params::Text3D::pos_x_hi),
         Text3DParamsRow::Slider<&Text3DNode::posY>("pos Y", params::Text3D::pos_y_lo, params::Text3D::pos_y_hi),
         Text3DParamsRow::Slider<&Text3DNode::posZ>("pos Z", params::Text3D::pos_z_lo, params::Text3D::pos_z_hi),
         Text3DParamsRow::Slider<&Text3DNode::rotX>("rot X", params::Text3D::rot_x_lo, params::Text3D::rot_x_hi),
         Text3DParamsRow::Slider<&Text3DNode::rotY>("rot Y", params::Text3D::rot_y_lo, params::Text3D::rot_y_hi),
         Text3DParamsRow::Slider<&Text3DNode::rotZ>("rot Z", params::Text3D::rot_z_lo, params::Text3D::rot_z_hi),
         Text3DParamsRow::Slider<&Text3DNode::uniformScale>("scale", params::Text3D::uniform_scale_lo, params::Text3D::uniform_scale_hi),
         Text3DParamsRow::Slider<&Text3DNode::spinY>("spin", params::Text3D::spin_y_lo, params::Text3D::spin_y_hi),
      };
   }

   void DrawText3DParams(Text3DNode* n)
   {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("text", buf, sizeof(buf)))
         n->text = buf;

      {
         const std::vector<std::string>& families = Platform::AvailableFontFamilies();
         int current = 0;
         for (size_t i = 0; i < families.size(); i++)
            if (families[i] == n->fontName)
               current = (int)i;
         DropdownButton("font", families, current,
                        [n, &families](int i)
                        {
                           if (i >= 0 && i < (int)families.size())
                              n->fontName = families[(size_t)i];
                        });
      }

      PushTextWrapWidth(kPreviewSize);
      ImGui::TextUnformatted(n->Status().c_str());
      PopTextWrapWidth();

      NodeSeparator("form");
      Ui::DrawParams(n, kText3DParamsRows1);

      NodeSeparator("transform");
      Ui::DrawParams(n, kText3DParamsRows2);
   }


   namespace
   {
      using ModelParamsRow = params::Row<ModelSourceNode>;
      const ModelParamsRow kModelParamsRows[] = {
         ModelParamsRow::Slider<&ModelSourceNode::clipSpeed>("speed", params::ModelSource::clip_speed_lo, params::ModelSource::clip_speed_hi),
         ModelParamsRow::Slider<&ModelSourceNode::clipPhase>("phase", 0.0f, 1.0f),
      };
   }

   void DrawModelParams(ModelSourceNode* n)
   {
      if (ActionButton::Column("Open model...", kParamWidth))
      {
         const std::string path = Platform::OpenModelDialog();
         if (!path.empty())
            n->Load(path);
      }
      // Bare TextWrapped has no usable content width inside the node editor and
      // wraps to one character per line; every other panel here sets the wrap
      // position explicitly.
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextUnformatted(n->Status().c_str());
      PopTextWrapWidth();

      NodeSeparator("import");
      // Evaluated into separate variables rather than OR'd inline: `||` would
      // short-circuit and skip drawing the second checkbox on any frame the
      // first one was clicked.
      const bool fitChanged = ModCheckbox("fit to unit size", &n->normalizeScale);
      const bool centreChanged = ModCheckbox("recentre", &n->recenter);
      if (fitChanged || centreChanged)
      {
         // Both are applied at load time, so changing them has to re-import.
         if (!n->Path().empty())
            n->Load(n->Path());
      }

      // Skinned / morphing glTF: clip, its time, and one weight per morph.
      if (n->HasRig() || gParamRegisterOnly)
      {
         NodeSeparator("rig");
         const std::vector<std::string> clips = n->ClipNames();
         if (clips.size() > 1 || gParamRegisterOnly)
         {
            DropdownButton("clip", clips, std::clamp(n->clip, 0, (int)clips.size() - 1),
                           [n](int i) { PushUndoCheckpoint(); n->clip = i; });
            Ui::DrawParams(n, kModelParamsRows);
            DropdownButton("sync", { "seconds", "beats" }, n->clipSync,
                           [n](int i) { PushUndoCheckpoint(); n->clipSync = i; }, kParamWidth, true, true);
            if (n->clipSync == 1 || gParamRegisterOnly)
               Ui::DrawParam(n, ModelParamsRow::Slider<&ModelSourceNode::clipBeats>("loop beats", params::ModelSource::clip_beats_lo, params::ModelSource::clip_beats_hi));
         }
         const int morphs = gParamRegisterOnly ? ModelSourceNode::kMaxMorphs : n->MorphCount();
         for (int i = 0; i < morphs; i++)
         {
            ImGui::PushID(i);
            const std::string label = i < n->MorphCount() ? n->MorphName(i) : "morph " + std::to_string(i + 1);
            ModSlider(label.c_str(), &n->morph[i], 0.0f, 1.0f);
            ImGui::PopID();
         }

         DrawRigPoseParams(n);
      }

      // A face-less .ply loads as points; their size is the only point knob.
      if (n->IsPointCloud() || gParamRegisterOnly)
      {
         NodeSeparator("points");
         Ui::DrawParam(n, ModelParamsRow::Slider<&ModelSourceNode::pointSize>("point size", params::ModelSource::point_size_lo, params::ModelSource::point_size_hi));
      }
   }


   namespace
   {
      using GeometryParamsRow = params::Row<GeometryNode>;
      const GeometryParamsRow kGeometryParamsRows1[] = {
         GeometryParamsRow::Slider<&GeometryNode::superN2>("n2", params::Geometry::super_n2_lo, params::Geometry::super_n2_hi),
         GeometryParamsRow::Slider<&GeometryNode::superN3>("n3", params::Geometry::super_n3_lo, params::Geometry::super_n3_hi),
         GeometryParamsRow::Slider<&GeometryNode::superP2>("p2", params::Geometry::super_p2_lo, params::Geometry::super_p2_hi),
         GeometryParamsRow::Slider<&GeometryNode::superP3>("p3", params::Geometry::super_p3_lo, params::Geometry::super_p3_hi),
      };
      const GeometryParamsRow kGeometryParamsRows2[] = {
         GeometryParamsRow::Slider<&GeometryNode::superN2>("tooth depth", params::Geometry::super_n2_lo, params::Geometry::super_n2_2_hi),
         GeometryParamsRow::Slider<&GeometryNode::superN3>("hub hole", params::Geometry::super_n3_2_lo, params::Geometry::super_n3_2_hi),
      };
      const GeometryParamsRow kGeometryParamsRows3[] = {
         GeometryParamsRow::Slider<&GeometryNode::tubeRadius>("depth", params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi),
         GeometryParamsRow::Slider<&GeometryNode::superN2>("inner ratio", params::Geometry::super_n2_lo, params::Geometry::super_n2_3_hi),
      };
   }

   void DrawGeometryParams(GeometryNode* n)
   {
      DropdownButton("shape", GeometryNode::ShapeNames(), n->shape,
         [n](int i)
         {
            // Pyramid and Prism are Cylinder/Cone under the hood, told apart
            // only by `sides` - at the high facet counts those default to
            // (left over from a round shape, or just never touched) they
            // read as a cone/cylinder instead of the flat-faced primitive
            // the name promises. Snap to a canonical low count on selection
            // so the dropdown pick is recognizable immediately; the user can
            // still dial `sides` back up afterward for a rounder look.
            if (i == 10 && n->shape != 10) n->sides = 4;       // Pyramid -> square pyramid
            else if (i == 11 && n->shape != 11) n->sides = 3;  // Prism -> triangular prism
            n->shape = i;
         });
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      NodeSeparator("form");
      {
         // Shapes whose generator ignores `detail` (fixed solids, flat
         // profiles built from `sides` alone): dim it rather than offer a
         // dead slider.
         const bool detailUnused = n->shape == 10 || (n->shape >= 14 && n->shape <= 16) || n->shape >= 20;
         Ui::DrawParam(n, GeometryParamsRow::SliderInt<&GeometryNode::detail>("detail", params::Geometry::detail_lo, params::Geometry::detail_hi), !detailUnused);
      }
      Ui::DrawParam(n, GeometryParamsRow::Slider<&GeometryNode::bevel>("bevel", 0.0f, 1.0f));
      if (n->shape == 13) // supershape
      {
         Ui::DrawParams(n, kGeometryParamsRows1);
      }
      if (n->bevel > 0.0f)
         Ui::DrawParam(n, GeometryParamsRow::SliderInt<&GeometryNode::bevelSegments>("bevel segments", params::Geometry::bevel_segments_lo, params::Geometry::bevel_segments_hi));
      {
         const bool sidesUnused = n->shape == 0 || n->shape == 1 || n->shape == 3 || (n->shape >= 14 && n->shape <= 19);
         Ui::DrawParam(n, GeometryParamsRow::SliderInt<&GeometryNode::sides>("sides", params::Geometry::sides_lo, params::Geometry::sides_hi), !sidesUnused);
      }
      // `tube` and `n2` are reused as the second and third form parameter by
      // several primitives, so each one is labelled for the shape in front of
      // you rather than by the variable it happens to live in. Capsule, Tube
      // and Helix were reading tubeRadius all along with no way to set it.
      switch (n->shape)
      {
         case 4: case 7: case 8:
            ModSlider("tube", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi); break;
         case 9:
            ModSlider("inner radius", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi); break;
         case 12:
            ModSlider("wire", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi); break;
         case 17:
            ModSlider("corner radius", &n->tubeRadius, params::Geometry::tube_radius_2_lo, params::Geometry::tube_radius_2_hi); break;
         case 18:
            ModSlider("width", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi); break;
         case 20:
            ModSlider("depth", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi);
            Ui::DrawParams(n, kGeometryParamsRows2);
            break;
         case 21:
            Ui::DrawParams(n, kGeometryParamsRows3);
            break;
         case 22:
            ModSlider("inner radius", &n->discInner, params::Geometry::disc_inner_lo, params::Geometry::disc_inner_hi); break;
         case 23:
            ModSlider("shaft radius", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_3_hi);
            Ui::DrawParam(n, GeometryParamsRow::Slider<&GeometryNode::superN2>("head length", params::Geometry::super_n2_2_lo, params::Geometry::super_n2_4_hi));
            break;
         default: break;
      }
      if (n->shape == 7 || n->shape == 12)
      {
         ModSliderInt(n->shape == 12 ? "turns" : "knot p", &n->knotP, params::Geometry::knot_p_lo, params::Geometry::knot_p_hi);   // ui-audit-ok: index (shape id 12 (helix) relabels the knot controls)
         ModSliderInt(n->shape == 12 ? "height" : "knot q", &n->knotQ, params::Geometry::knot_q_lo, params::Geometry::knot_q_hi);   // ui-audit-ok: index (shape id 12 (helix) relabels the knot controls)
      }
   }


   // Which ops "only affects selected faces" is even meaningful for - group 1
   // (per-face independent) and kTwist (per-vertex) from
   // docs/plans/phase4-selection-as-input.md. Connectivity-dependent ops
   // (Subdivide/Smooth/Screw) and whole-mesh-duplicating ones (Array/Mirror)
   // are left out - showing a toggle that does nothing is worse than no
   // toggle. kSelect and the deprecated `*Selected` ops are left out too:
   // Select produces the mask rather than consuming one, and the `*Selected`
   // ops are selection-restricted unconditionally already.
   bool GeometryOpSupportsSelectionOnly(int op)
   {
      switch (op)
      {
         case GeometryOpNode::kFur:
         case GeometryOpNode::kTransform:
         case GeometryOpNode::kSolidify:
         case GeometryOpNode::kExtrude:
         case GeometryOpNode::kInset:
         case GeometryOpNode::kWireframe:
         case GeometryOpNode::kTriangulate:
         case GeometryOpNode::kNormals:
         case GeometryOpNode::kExplode:
         case GeometryOpNode::kTwist:
         case GeometryOpNode::kDelete:
            return true;
         default:
            return false;
      }
   }


   namespace
   {
      using GeometryOpParamsRow = params::Row<GeometryOpNode>;
      const GeometryOpParamsRow kGeometryOpParamsRows1[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetX>("move x", params::geometry_params::offset_x_lo, params::geometry_params::offset_x_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetY>("move y", params::geometry_params::offset_y_lo, params::geometry_params::offset_y_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetZ>("move z", params::geometry_params::offset_z_lo, params::geometry_params::offset_z_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotX>("rotate x", params::geometry_params::rot_x_lo, params::geometry_params::rot_x_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotY>("rotate y", params::geometry_params::rot_y_lo, params::geometry_params::rot_y_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotZ>("rotate z", params::geometry_params::rot_z_lo, params::geometry_params::rot_z_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleX>("scale x", params::geometry_params::scale_x_lo, params::geometry_params::scale_x_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleY>("scale y", params::geometry_params::scale_y_lo, params::geometry_params::scale_y_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleZ>("scale z", params::geometry_params::scale_z_lo, params::geometry_params::scale_z_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows2[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::pivotX>("pivot x", params::geometry_params::pivot_x_lo, params::geometry_params::pivot_x_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::pivotY>("pivot y", params::geometry_params::pivot_y_lo, params::geometry_params::pivot_y_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::pivotZ>("pivot z", params::geometry_params::pivot_z_lo, params::geometry_params::pivot_z_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows3[] = {
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::count>("count", params::geometry_params::count_lo, params::geometry_params::count_hi),
         GeometryOpParamsRow::Toggle<&GeometryOpNode::radial>("radial"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows4[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetX>("offset x", params::geometry_params::offset_x_2_lo, params::geometry_params::offset_x_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetY>("offset y", params::geometry_params::offset_y_2_lo, params::geometry_params::offset_y_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetZ>("offset z", params::geometry_params::offset_z_2_lo, params::geometry_params::offset_z_2_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows5[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotStep>("rot / step", params::geometry_params::rot_step_lo, params::geometry_params::rot_step_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleStep>("scale / step", params::geometry_params::scale_step_lo, params::geometry_params::scale_step_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows6[] = {
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::levels>("levels", params::geometry_params::levels_lo, params::geometry_params::levels_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::smooth>("smooth", params::geometry_params::smooth_lo, params::geometry_params::smooth_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows7[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::thickness>("thickness", params::geometry_params::thickness_lo, params::geometry_params::thickness_hi).Fmt("%.4f").Curve(LogPosToValue, LogValueToPos),
         GeometryOpParamsRow::Toggle<&GeometryOpNode::keepOriginal>("keep original"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows8[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::thickness>("distance", params::geometry_params::thickness_2_lo, params::geometry_params::thickness_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::inset>("inset", params::geometry_params::inset_lo, params::geometry_params::inset_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows9[] = {
         GeometryOpParamsRow::Toggle<&GeometryOpNode::flatShade>("flat shade"),
         GeometryOpParamsRow::Toggle<&GeometryOpNode::flipNormals>("flip"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows10[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::amount>("amount", params::geometry_params::amount_2_lo, params::geometry_params::amount_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::seed>("seed", params::geometry_params::seed_lo, params::geometry_params::seed_hi).Fmt("%.0f").Step(1.0f),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows11[] = {
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::levels>("pre-subdivide", params::geometry_params::levels_lo, params::geometry_params::levels_hi),
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::iterations>("iterations", params::geometry_params::iterations_lo, params::geometry_params::iterations_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::amount>("strength", 0.0f, 1.0f),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows12[] = {
         GeometryOpParamsRow::Choice<&GeometryOpNode::axis>("axis", &kAxisXYZ).Undo().Segmented(),
         GeometryOpParamsRow::Slider<&GeometryOpNode::mirrorOffset>("plane offset", params::geometry_params::mirror_offset_lo, params::geometry_params::mirror_offset_hi),
         GeometryOpParamsRow::Toggle<&GeometryOpNode::keepOriginal>("keep original"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows13[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectA>("start", params::geometry_params::select_a_lo, params::geometry_params::select_a_hi).Fmt("%.0f"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectB>("count", params::geometry_params::select_b_lo, params::geometry_params::select_b_hi).Fmt("%.0f"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectC>("every", params::geometry_params::select_c_lo, params::geometry_params::select_c_hi).Fmt("%.0f"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows14[] = {
         GeometryOpParamsRow::Choice<&GeometryOpNode::axis>("axis", &kAxisXYZ).Undo().Segmented(),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectA>("min", params::geometry_params::select_a_2_lo, params::geometry_params::select_a_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectB>("max", params::geometry_params::select_b_2_lo, params::geometry_params::select_b_2_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows15[] = {
         GeometryOpParamsRow::Choice<&GeometryOpNode::axis>("axis", &kAxisXYZ).Undo().Segmented(),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectA>("facing", -1.0f, 1.0f),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectC>("sign", -1.0f, 1.0f),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows16[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectA>("amount", 0.0f, 1.0f),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectSeed>("seed", params::geometry_params::select_seed_lo, params::geometry_params::select_seed_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows17[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectA>("x", params::geometry_params::select_a_2_lo, params::geometry_params::select_a_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectB>("y", params::geometry_params::select_b_2_lo, params::geometry_params::select_b_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectC>("z", params::geometry_params::select_c_2_lo, params::geometry_params::select_c_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::selectSeed>("radius", params::geometry_params::select_seed_2_lo, params::geometry_params::select_seed_2_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows18[] = {
         GeometryOpParamsRow::Toggle<&GeometryOpNode::selectInvert>("invert"),
         GeometryOpParamsRow::Toggle<&GeometryOpNode::selectAppend>("add to selection"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows19[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetX>("move x", params::geometry_params::offset_x_lo, params::geometry_params::offset_x_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetY>("move y", params::geometry_params::offset_y_lo, params::geometry_params::offset_y_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::offsetZ>("move z", params::geometry_params::offset_z_lo, params::geometry_params::offset_z_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotX>("rotate x", params::geometry_params::rot_x_lo, params::geometry_params::rot_x_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotY>("rotate y", params::geometry_params::rot_y_lo, params::geometry_params::rot_y_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rotZ>("rotate z", params::geometry_params::rot_z_lo, params::geometry_params::rot_z_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleX>("scale x", params::geometry_params::scale_x_2_lo, params::geometry_params::scale_x_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleY>("scale y", params::geometry_params::scale_y_2_lo, params::geometry_params::scale_y_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::scaleZ>("scale z", params::geometry_params::scale_z_2_lo, params::geometry_params::scale_z_2_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::spin>("spin / beat", params::geometry_params::spin_lo, params::geometry_params::spin_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows20[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::thickness>("distance", -1.0f, 1.0f),
         GeometryOpParamsRow::Slider<&GeometryOpNode::inset>("inset", params::geometry_params::inset_lo, params::geometry_params::inset_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows21[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::bevelWidth>("width", params::geometry_params::bevel_width_lo, params::geometry_params::bevel_width_hi),
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::bevelSegments>("segments", params::geometry_params::bevel_segments_lo, params::geometry_params::bevel_segments_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::bevelAngle>("sharp angle", params::geometry_params::bevel_angle_lo, params::geometry_params::bevel_angle_hi).Fmt("%.0f\xC2\xB0"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows22[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::insetWidth>("width", params::geometry_params::inset_width_lo, params::geometry_params::inset_width_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::insetDepth>("depth", params::geometry_params::inset_depth_lo, params::geometry_params::inset_depth_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::bevelAngle>("patch angle", params::geometry_params::bevel_angle_lo, params::geometry_params::bevel_angle_hi).Fmt("%.0f\xC2\xB0"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows23[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::uvAngle>("angle", params::geometry_params::uv_angle_lo, params::geometry_params::uv_angle_hi).Fmt("%.0f\xC2\xB0"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::uvMargin>("margin", params::geometry_params::uv_margin_lo, params::geometry_params::uv_margin_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows24[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::furDensity>("density", params::geometry_params::fur_density_lo, params::geometry_params::fur_density_hi).Fmt("%.0f / cm2"),
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::furMaxStrands>("max strands", params::geometry_params::fur_max_strands_lo, params::geometry_params::fur_max_strands_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::furLength>("length", params::geometry_params::fur_length_lo, params::geometry_params::fur_length_hi).Fmt("%.4f"),
         GeometryOpParamsRow::Slider<&GeometryOpNode::furJitter>("length jitter", 0.0f, 1.0f),
         GeometryOpParamsRow::Slider<&GeometryOpNode::furWidth>("width", params::geometry_params::fur_width_lo, params::geometry_params::fur_width_hi).Fmt("%.5f"),
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::furSegments>("segments", params::geometry_params::fur_segments_lo, params::geometry_params::fur_segments_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::furGravity>("gravity", 0.0f, 1.0f),
         GeometryOpParamsRow::Slider<&GeometryOpNode::furClump>("clump", 0.0f, 1.0f),
         GeometryOpParamsRow::Slider<&GeometryOpNode::furCurl>("curl", 0.0f, 1.0f),
         GeometryOpParamsRow::Slider("comb x", [](GeometryOpNode* n) { return &n->furComb[0]; }, params::geometry_params::fur_comb_0_lo, params::geometry_params::fur_comb_0_hi),
         GeometryOpParamsRow::Slider("comb y", [](GeometryOpNode* n) { return &n->furComb[1]; }, params::geometry_params::fur_comb_1_lo, params::geometry_params::fur_comb_1_hi),
         GeometryOpParamsRow::Slider("comb z", [](GeometryOpNode* n) { return &n->furComb[2]; }, params::geometry_params::fur_comb_2_lo, params::geometry_params::fur_comb_2_hi),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows25[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::decimateRatio>("keep", params::geometry_params::decimate_ratio_lo, params::geometry_params::decimate_ratio_hi).Fmt("%.2f"),
         GeometryOpParamsRow::Toggle<&GeometryOpNode::lockBorder>("lock border"),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows26[] = {
         GeometryOpParamsRow::SliderInt<&GeometryOpNode::screwSteps>("steps", params::geometry_params::screw_steps_lo, params::geometry_params::screw_steps_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::turns>("turns", params::geometry_params::turns_lo, params::geometry_params::turns_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::rise>("rise / turn", params::geometry_params::rise_lo, params::geometry_params::rise_hi),
         GeometryOpParamsRow::Slider<&GeometryOpNode::radiusOffset>("radius", params::geometry_params::radius_offset_lo, params::geometry_params::radius_offset_hi),
         GeometryOpParamsRow::Choice<&GeometryOpNode::axis>("axis", &kAxisXYZ).Undo().Segmented(),
      };
      const GeometryOpParamsRow kGeometryOpParamsRows27[] = {
         GeometryOpParamsRow::Slider<&GeometryOpNode::amount>("angle", params::geometry_params::amount_3_lo, params::geometry_params::amount_3_hi).Fmt("%.1f\xC2\xB0"),
         GeometryOpParamsRow::Choice<&GeometryOpNode::axis>("axis", &kAxisXYZ).Undo().Segmented(),
      };
   }

   void DrawGeometryOpParams(GeometryOpNode* n)
   {
      // Dropdown shows only spawnable ops (GeometryOpNode::IsSpawnable), kept
      // as a name+real-index pair since the underlying `op` enum still has
      // gaps at the three deprecated indices.
      static std::vector<std::string> sVisibleOpNames;
      static std::vector<int> sVisibleOpIndices;
      if (sVisibleOpNames.empty())
      {
         const auto& names = GeometryOpNode::OpNames();
         for (int i = 0; i < GeometryOpNode::kOpCount; i++)
         {
            if (!GeometryOpNode::IsSpawnable(i))
               continue;
            sVisibleOpNames.push_back(names[i]);
            sVisibleOpIndices.push_back(i);
         }
      }
      int visibleCurrent = 0;
      for (size_t i = 0; i < sVisibleOpIndices.size(); i++)
         if (sVisibleOpIndices[i] == n->op) { visibleCurrent = (int)i; break; }
      DropdownButton("operation", sVisibleOpNames, visibleCurrent,
         [n](int i) { n->op = sVisibleOpIndices[i]; });
      if (n->input != nullptr)
      {
         // Downstream of an Instance on Points, every op but Transform runs
         // once on the shared stamp mesh rather than per instance - state
         // which frame of reference is in effect right where the triangle
         // count would otherwise read as "the whole scatter" (see
         // GeometryOpNode::ActsOnInstanceStamp / the kTransform case in
         // GetMesh()).
         if (n->ActsOnInstanceStamp())
         {
            if (n->op == GeometryOpNode::kTransform)
               ImGui::TextDisabled("whole group, %s copies",
                                    UiFmt::Count((long long)(n->UpstreamInstanceCount())).c);
            else
               ImGui::TextDisabled("%s tris, per shape x%zu",
                                    UiFmt::Count((long long)(n->TriangleCount())).c, n->UpstreamInstanceCount());
         }
         else
            ImGui::TextDisabled("%s triangles out", UiFmt::Count((long long)(n->TriangleCount())).c);
      }
      if (GeometryOpSupportsSelectionOnly(n->op))
         Ui::DrawParam(n, GeometryOpParamsRow::Toggle<&GeometryOpNode::selectionOnly>("selection only"));

      switch (n->op)
      {
         case GeometryOpNode::kTransform:
            if (n->selectionOnly)
            {
               ImGui::TextDisabled("%zu of %s faces selected", n->SelectedCount(), UiFmt::Count((long long)(n->TriangleCount())).c);
               Ui::DrawParam(n, GeometryOpParamsRow::Toggle<&GeometryOpNode::moveAlongNormals>("move along normals"));
               if (n->moveAlongNormals)
                  Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::normalAmount>("normal amount", params::geometry_params::normal_amount_lo, params::geometry_params::normal_amount_hi));
            }
            Ui::DrawParams(n, kGeometryOpParamsRows1);
            // Point rotate/scale pivot about - default (0,0,0) matches the
            // mesh's own origin, i.e. no visible change from before pivot
            // existed. Sits after scale/before spin, same order TransformMatrix()
            // applies move/rotate/scale/pivot in.
            {
            const bool pivotUnused = n->rotX == 0.0f && n->rotY == 0.0f && n->rotZ == 0.0f && n->spin == 0.0f
               && n->scaleX == 1.0f && n->scaleY == 1.0f && n->scaleZ == 1.0f;
            Ui::DrawParams(n, kGeometryOpParamsRows2, !pivotUnused);
            }
            Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::spin>("spin / beat", params::geometry_params::spin_lo, params::geometry_params::spin_hi));
            break;
         case GeometryOpNode::kArray:
            Ui::DrawParams(n, kGeometryOpParamsRows3);
            if (n->radial)
               Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::radius>("radius", params::geometry_params::radius_lo, params::geometry_params::radius_hi));
            else
            {
               Ui::DrawParams(n, kGeometryOpParamsRows4);
            }
            Ui::DrawParams(n, kGeometryOpParamsRows5);
            break;
         case GeometryOpNode::kSubdivide:
            Ui::DrawParams(n, kGeometryOpParamsRows6);
            break;
         case GeometryOpNode::kSolidify:
            Ui::DrawParams(n, kGeometryOpParamsRows7);
            break;
         case GeometryOpNode::kExtrude:
            Ui::DrawParams(n, kGeometryOpParamsRows8);
            break;
         case GeometryOpNode::kWireframe:
            Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::thickness>("thickness", params::geometry_params::thickness_3_lo, params::geometry_params::thickness_3_hi));
            break;
         case GeometryOpNode::kTriangulate:
            Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::amount>("jitter", params::geometry_params::amount_lo, params::geometry_params::amount_hi));
            break;
         case GeometryOpNode::kNormals:
            Ui::DrawParams(n, kGeometryOpParamsRows9);
            break;
         case GeometryOpNode::kExplode:
         {
            static const std::vector<std::string> kExplodeByNames = { "Faces", "Loose Parts" };
            DropdownButton("by", kExplodeByNames, n->explodeBy, [n](int i) { n->explodeBy = i; });
            Ui::DrawParams(n, kGeometryOpParamsRows10);
            break;
         }
         case GeometryOpNode::kSmooth:
            Ui::DrawParams(n, kGeometryOpParamsRows11);
            break;
         case GeometryOpNode::kMirror:
            Ui::DrawParams(n, kGeometryOpParamsRows12);
            Ui::DrawParam(n, GeometryOpParamsRow::Toggle<&GeometryOpNode::weldSeam>("weld seam"), n->keepOriginal);
            break;
         case GeometryOpNode::kSelect:
         {
            static const std::vector<std::string> kSelectModes = {
               "All", "By index", "By position", "By normal", "Random", "By radius"
            };
            ImGui::TextDisabled("%zu of %s faces", n->SelectedCount(), UiFmt::Count((long long)(n->TriangleCount())).c);
            DropdownButton("mode", kSelectModes, n->selectMode, [n](int i) { n->selectMode = i; });
            switch (n->selectMode)
            {
               case 1: // index
                  Ui::DrawParams(n, kGeometryOpParamsRows13);
                  break;
               case 2: // position along an axis
                  Ui::DrawParams(n, kGeometryOpParamsRows14);
                  break;
               case 3: // normal direction
                  Ui::DrawParams(n, kGeometryOpParamsRows15);
                  break;
               case 4: // random
                  Ui::DrawParams(n, kGeometryOpParamsRows16);
                  break;
               case 5: // radius
                  Ui::DrawParams(n, kGeometryOpParamsRows17);
                  break;
               default: break;
            }
            Ui::DrawParams(n, kGeometryOpParamsRows18);
            break;
         }
         case GeometryOpNode::kDelete:
         case GeometryOpNode::kDeleteSelected:
            if (n->selectionOnly)
               ImGui::TextDisabled("%zu of %s faces selected", n->SelectedCount(), UiFmt::Count((long long)(n->TriangleCount())).c);
            Ui::DrawParam(n, GeometryOpParamsRow::Toggle<&GeometryOpNode::keepSelected>("keep selected instead"));
            break;
         case GeometryOpNode::kTransformSelected:
            Ui::DrawParam(n, GeometryOpParamsRow::Toggle<&GeometryOpNode::moveAlongNormals>("move along normals"));
            if (n->moveAlongNormals)
               Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::normalAmount>("normal amount", params::geometry_params::normal_amount_lo, params::geometry_params::normal_amount_hi));
            Ui::DrawParams(n, kGeometryOpParamsRows19);
            break;
         case GeometryOpNode::kExtrudeSelected:
            Ui::DrawParams(n, kGeometryOpParamsRows20);
            break;
         case GeometryOpNode::kBevel:
            Ui::DrawParams(n, kGeometryOpParamsRows21);
            break;
         case GeometryOpNode::kInset:
            Ui::DrawParams(n, kGeometryOpParamsRows22);
            break;
         case GeometryOpNode::kUVProject:
         {
            static const std::vector<std::string> kUVModes = { "Smart", "Cube", "Cylinder", "Sphere" };
            DropdownButton("project", kUVModes, n->uvMode, [n](int i) { PushUndoCheckpoint(); n->uvMode = i; }, kParamWidth, true, true);
            if (n->uvMode == 0)
            {
               Ui::DrawParams(n, kGeometryOpParamsRows23);
            }
            Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::uvScale>("scale", params::geometry_params::uv_scale_lo, params::geometry_params::uv_scale_hi));
            break;
         }
         case GeometryOpNode::kFur:
         {
            static const std::vector<std::string> kFurNormals = { "Ribbon", "Tangent" };
            Ui::DrawParams(n, kGeometryOpParamsRows24);
            DropdownButton("normals", kFurNormals, n->furNormals, [n](int i) { PushUndoCheckpoint(); n->furNormals = i; }, kParamWidth, true, true);
            Ui::DrawParam(n, GeometryOpParamsRow::Slider<&GeometryOpNode::seed>("seed", params::geometry_params::seed_lo, params::geometry_params::seed_hi).Fmt("%.0f").Step(1.0f));
            break;
         }
         case GeometryOpNode::kDecimate:
            Ui::DrawParams(n, kGeometryOpParamsRows25);
            break;
         case GeometryOpNode::kScrew:
            Ui::DrawParams(n, kGeometryOpParamsRows26);
            break;
         default:
            Ui::DrawParams(n, kGeometryOpParamsRows27);
            break;
      }
   }


   namespace
   {
      using DisplacementParamsRow = params::Row<DisplacementNode>;
      const DisplacementParamsRow kDisplacementParamsRows1[] = {
         DisplacementParamsRow::Choice<&DisplacementNode::noiseType>("type", &DisplacementNode::NoiseTypeNames),
         DisplacementParamsRow::Choice<&DisplacementNode::affects>("affects", &DisplacementNode::AffectsNames),
         DisplacementParamsRow::Slider<&DisplacementNode::frequency>("frequency", params::Displacement::frequency_lo, params::Displacement::frequency_hi),
         DisplacementParamsRow::Slider<&DisplacementNode::speed>("speed", params::Displacement::speed_lo, params::Displacement::speed_hi),
      };
      const DisplacementParamsRow kDisplacementParamsRows2[] = {
         DisplacementParamsRow::SliderInt<&DisplacementNode::octaves>("detail", params::Displacement::octaves_lo, params::Displacement::octaves_hi),
         DisplacementParamsRow::Slider<&DisplacementNode::noiseRoughness>("grain", 0.0f, 1.0f),
      };
      const DisplacementParamsRow kDisplacementParamsRows3[] = {
         DisplacementParamsRow::Toggle<&DisplacementNode::flatShade>("flat shade"),
         DisplacementParamsRow::Toggle<&DisplacementNode::flipNormals>("flip normals"),
         DisplacementParamsRow::Toggle<&DisplacementNode::selectionOnly>("selection only"),
      };
   }

   void DrawDisplacementParams(DisplacementNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      static const std::vector<std::string> kSourceNames = { "Texture", "Noise" };
      DropdownButton("source", kSourceNames, n->source, [n](int i) { n->source = i; });
      const bool noise = n->source == DisplacementNode::kNoise;
      static const std::vector<std::string> kModeNames = { "Scalar (along normal)", "Vector (RGB = XYZ)" };
      static const std::vector<std::string> kNoiseModeNames = { "Scalar (along normal)", "Vector (XYZ)" };
      DropdownButton("mode", noise ? kNoiseModeNames : kModeNames, n->mode, [n](int i) { n->mode = i; });
      Ui::DrawParam(n, DisplacementParamsRow::Slider<&DisplacementNode::strength>("strength", params::Displacement::strength_lo, params::Displacement::strength_hi));
      if (n->mode == DisplacementNode::kScalar && !noise)
         Ui::DrawParam(n, DisplacementParamsRow::Slider<&DisplacementNode::midlevel>("midlevel", 0.0f, 1.0f));
      if (noise || gParamRegisterOnly)
      {
         NodeSeparator("noise");
         Ui::DrawParams(n, kDisplacementParamsRows1);
         // Detail and grain only shape the octave types.
         Ui::DrawParams(n, kDisplacementParamsRows2, !(n->noiseType != 1 && n->noiseType != 3));
         Ui::DrawParam(n, DisplacementParamsRow::SliderInt<&DisplacementNode::seed>("seed", params::Displacement::seed_lo, params::Displacement::seed_hi));
      }
      Ui::DrawParams(n, kDisplacementParamsRows3);
   }


   void DrawGeometryFeedbackParams(GeometryFeedbackNode* n)
   {
      if (n->seed == nullptr)
         ImGui::TextDisabled("patch a shape into seed");
      else if (!n->Looping())
         ImGui::TextDisabled("passing the seed through");
      else
         ImGui::TextDisabled("looping, frame %d", n->Generation());
      if (ActionButton::Draw("Reset", ImVec2(kPreviewSize, 0)))
         n->Reset();
   }


   namespace
   {
      using AudioDisplacementParamsRow = params::Row<AudioDisplacementNode>;
      const AudioDisplacementParamsRow kAudioDisplacementParamsRows1[] = {
         AudioDisplacementParamsRow::Choice<&AudioDisplacementNode::mode>("mode", &AudioDisplacementNode::ModeNames),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::strength>("strength", params::AudioDisplacement::strength_lo, params::AudioDisplacement::strength_hi),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::frequency>("frequency", params::AudioDisplacement::frequency_lo, params::AudioDisplacement::frequency_hi),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::speed>("speed", params::AudioDisplacement::speed_lo, params::AudioDisplacement::speed_hi),
      };
      const AudioDisplacementParamsRow kAudioDisplacementParamsRows2[] = {
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::attack>("attack", params::AudioDisplacement::attack_lo, params::AudioDisplacement::attack_hi).Fmt("%.0f ms"),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::decay>("decay", params::AudioDisplacement::decay_lo, params::AudioDisplacement::decay_hi).Fmt("%.0f ms"),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::midlevel>("midlevel", -1.0f, 1.0f),
         AudioDisplacementParamsRow::SliderInt<&AudioDisplacementNode::subdivide>("subdivide", params::AudioDisplacement::subdivide_lo, params::AudioDisplacement::subdivide_hi),
      };
      const AudioDisplacementParamsRow kAudioDisplacementParamsRows3[] = {
         AudioDisplacementParamsRow::SliderInt<&AudioDisplacementNode::chladniM>("mode m", params::AudioDisplacement::chladni_m_lo, params::AudioDisplacement::chladni_m_hi),
         AudioDisplacementParamsRow::SliderInt<&AudioDisplacementNode::chladniN>("mode n", params::AudioDisplacement::chladni_n_lo, params::AudioDisplacement::chladni_n_hi),
      };
      const AudioDisplacementParamsRow kAudioDisplacementParamsRows4[] = {
         AudioDisplacementParamsRow::Toggle<&AudioDisplacementNode::flatShade>("flat shade"),
         AudioDisplacementParamsRow::Toggle<&AudioDisplacementNode::flipNormals>("flip normals"),
         AudioDisplacementParamsRow::Toggle<&AudioDisplacementNode::inheritMaterial>("inherit material"),
      };
      const AudioDisplacementParamsRow kAudioDisplacementParamsRows5[] = {
         AudioDisplacementParamsRow::Choice<&AudioDisplacementNode::shading>("shading", &GeometryNode::ShadingNames),
         AudioDisplacementParamsRow::Colour<&AudioDisplacementNode::color>("colour"),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::metallic>("metallic", 0.0f, 1.0f),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::roughness>("roughness", params::AudioDisplacement::roughness_lo, params::AudioDisplacement::roughness_hi),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::opacity>("opacity", 0.0f, 1.0f),
         AudioDisplacementParamsRow::Colour<&AudioDisplacementNode::emissionColor>("emission"),
         AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::emission>("emission", params::AudioDisplacement::emission_lo, params::AudioDisplacement::emission_hi),
      };
   }

   void DrawAudioDisplacementParams(AudioDisplacementNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      Ui::DrawParams(n, kAudioDisplacementParamsRows1);
      Ui::DrawParam(n, AudioDisplacementParamsRow::Slider<&AudioDisplacementNode::damping>("damping", params::AudioDisplacement::damping_lo, params::AudioDisplacement::damping_hi), n->mode == AudioDisplacementNode::kModeAcousticRipples);
      Ui::DrawParams(n, kAudioDisplacementParamsRows2);

      if (n->mode == AudioDisplacementNode::kModeDirectionalAxis)
      {
         static const std::vector<std::string> kAxes = { "X Axis", "Y Axis", "Z Axis" };
         DropdownButton("axis", kAxes, n->axis, [n](int i) { n->axis = i; });
      }
      else if (n->mode == AudioDisplacementNode::kModeCymaticsChladni)
      {
         Ui::DrawParams(n, kAudioDisplacementParamsRows3);
      }

      Ui::DrawParams(n, kAudioDisplacementParamsRows4);
      if (!n->inheritMaterial)
      {
         NodeSeparator("material");
         Ui::DrawParams(n, kAudioDisplacementParamsRows5);
      }
   }


   namespace
   {
      using AudioTextureParamsRow = params::Row<AudioTextureNode>;
      const AudioTextureParamsRow kAudioTextureParamsRows[] = {
         AudioTextureParamsRow::Slider<&AudioTextureNode::gain>("gain", params::AudioTexture::gain_lo, params::AudioTexture::gain_hi),
         AudioTextureParamsRow::Slider<&AudioTextureNode::smoothing>("smoothing", params::AudioTexture::smoothing_lo, params::AudioTexture::smoothing_hi),
      };
   }

   void DrawAudioTextureParams(AudioTextureNode* n)
   {
      Ui::DrawParam(n, AudioTextureParamsRow::Choice<&AudioTextureNode::mode>("mode", &AudioTextureNode::ModeNames));
      if (n->mode == AudioTextureNode::kModeWaveform)
      {
         Ui::DrawParam(n, AudioTextureParamsRow::Choice<&AudioTextureNode::windowSizeIndex>("window", &AudioTextureNode::WindowSizeNames));
      }
      else
      {
         Ui::RowsLive off(false);
         int dummy = 1; // 1024
         DropdownButton("window (1024)", AudioTextureNode::WindowSizeNames(), dummy, [](int) {});
      }
      Ui::DrawParams(n, kAudioTextureParamsRows);
   }


   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawAudioColorRampEditor(AudioColorRampNode* n)
   {
      const float size = kPreviewSize;
      const float barH = tok::geometry_params_bar_h;
      const float gap = tok::space_1;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      dl->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + barH), tok::Role(tok::col::surface_deep), tok::geometry_params_audio_color_ramp_editor_radius);

      const int count = std::clamp(n->bandCount, 2, AudioColorRampNode::kMaxBands);
      const float* energies = n->GetBandEnergies();

      auto toScreenX = [&](float x) { return origin.x + x * size; };

      // 1. Per-band energy bars, coloured by that band's assigned colour -
      // this is the live feedback for "which band is loud right now" and
      // doubles as a preview of where each band's frequency range sits.
      // Always shows a visible floor so the visualizer isn't blank at rest.
      float prevX = 0.0f;
      for (int b = 0; b < count; b++)
      {
         float nextX = (b == count - 1) ? 1.0f : n->crossoverPos[b];
         float x0 = toScreenX(prevX) + 1.0f;
         float x1 = toScreenX(nextX) - 1.0f;
         if (x1 > x0)
         {
            float lvl = std::clamp(energies[b], 0.0f, 1.0f);
            float h = barH * tok::geometry_params_h_ratio + (barH * tok::geometry_params_audio_color_ramp_editor_ratio) * lvl;
            ImU32 col = IM_COL32((int)(std::clamp(n->bandColor[b][0], 0.0f, 1.0f) * 255),
                                 (int)(std::clamp(n->bandColor[b][1], 0.0f, 1.0f) * 255),
                                 (int)(std::clamp(n->bandColor[b][2], 0.0f, 1.0f) * 255), tok::geometry_params_audio_color_ramp_editor_alpha);
            dl->AddRectFilled(ImVec2(x0, origin.y + barH - h), ImVec2(x1, origin.y + barH - 1.0f), col);
         }
         prevX = nextX;
      }

      // 2. Real-time continuous FFT curve overlay, for detail finer than the bands.
      const auto& spec = n->GetSmoothedSpectrum();
      if (!spec.empty())
      {
         std::vector<ImVec2> pts;
         pts.reserve(96);
         for (int i = 0; i < 96; i++)
         {
            float t = (float)i / 95.0f;
            float freq = AudioColorRampNode::PosToFreq(t);
            int bin = std::clamp((int)(freq * (1024.0f / 44100.0f)), 1, 511);
            float mag = std::clamp(spec[bin] * 2.5f, 0.0f, 1.0f);
            pts.push_back(ImVec2(origin.x + t * size, origin.y + barH - mag * (barH - tok::space_1)));
         }
         dl->AddPolyline(pts.data(), (int)pts.size(), tok::Role(tok::col::ink_white, tok::geometry_params_line_alpha), false, tok::geometry_params_audio_color_ramp_editor_stroke);
      }

      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + barH), tok::Role(tok::col::edge_lo_light), tok::geometry_params_audio_color_ramp_editor_radius);

      // 3. Interactive band-boundary dividers, drawn directly on the spectrum.
      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit("##acr_track", ImVec2(size, barH));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toX = [&](float screenX) { return std::clamp((screenX - origin.x) / size, 0.01f, 0.99f); };

      static AudioColorRampNode* sDragNode = nullptr;
      static int sDragIndex = -1;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const int numDividers = count - 1;
      int nearest = -1;
      float nearestDist = 10.0f;
      for (int i = 0; i < numDividers; i++)
      {
         float d = std::fabs(toScreenX(n->crossoverPos[i]) - mouse.x);
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && nearest >= 0)
      {
         PushUndoCheckpoint();
         sDragIndex = nearest;
         sDragNode = n;
      }
      if (active && sDragNode == n && sDragIndex >= 0)
      {
         float minPos = (sDragIndex == 0) ? tok::geometry_params_min_pos : (n->crossoverPos[sDragIndex - 1] + tok::geometry_params_audio_color_ramp_editor_gap);
         float maxPos = (sDragIndex == numDividers - 1) ? tok::geometry_params_max_pos : (n->crossoverPos[sDragIndex + 1] - tok::geometry_params_audio_color_ramp_editor_gap);
         n->crossoverPos[sDragIndex] = std::clamp(toX(mouse.x), minPos, maxPos);
         n->MarkDirty();
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragNode = nullptr;
      }

      // Hovered divider's frequency goes in a fixed readout line below the
      // track rather than a tooltip - a tooltip drawn from inside ed::Begin()/
      // ed::End() (even Suspend()/Resume()-wrapped) fights the node editor's
      // per-node draw-channel splitting and can blank the node body for a
      // frame; see the "audio node value readout" note above SetAudioReadout.
      char readout[48] = "";
      for (int i = 0; i < numDividers; i++)
      {
         float x = toScreenX(n->crossoverPos[i]);
         bool isHov = (nearest == i || (sDragNode == n && sDragIndex == i));
         ImU32 lineCol = isHov ? tok::Role(tok::col::role_modulation, tok::geometry_params_audio_color_ramp_editor_role_modulation_alpha) : tok::Role(tok::col::ink_white, tok::geometry_params_audio_color_ramp_editor_ink_white_alpha);
         dl->AddLine(ImVec2(x, origin.y + tok::space_0_5), ImVec2(x, origin.y + barH - tok::space_0_5), lineCol, isHov ? tok::geometry_params_line_stroke_lg : tok::geometry_params_audio_color_ramp_editor_stroke);

         if (isHov)
         {
            float hz = AudioColorRampNode::PosToFreq(n->crossoverPos[i]);
            char hzText[32];
            if (hz >= 1000.0f)
               snprintf(hzText, sizeof(hzText), "%s", UiFmt::Hz(hz).c);
            else
               snprintf(hzText, sizeof(hzText), "%s", UiFmt::Hz(hz).c);
            snprintf(readout, sizeof(readout), "crossover %d: %s", i + 1, hzText);
         }
      }

      const float readoutH = ImGui::GetTextLineHeight();
      dl->AddText(ImVec2(origin.x, origin.y + barH + gap), tok::Role(tok::col::text_secondary), readout);

      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + barH + gap + readoutH + gap));
   }
   // ui-content-end


   namespace
   {
      using AudioColorRampParamsRow = params::Row<AudioColorRampNode>;
      const AudioColorRampParamsRow kAudioColorRampParamsRows[] = {
         AudioColorRampParamsRow::Slider<&AudioColorRampNode::gain>("gain", params::AudioColorRamp::gain_lo, params::AudioColorRamp::gain_hi),
         AudioColorRampParamsRow::Slider<&AudioColorRampNode::minBrightness>("min glow", 0.0f, 1.0f),
      };
   }

   void DrawAudioColorRampParams(AudioColorRampNode* n)
   {
      DrawAudioColorRampEditor(n);

      NodeSeparator("band colors");
      for (int b = 0; b < n->bandCount; b++)
      {
         ImGui::PushID(b + 30000);
         char label[32];
         float f0 = (b == 0) ? 20.0f : AudioColorRampNode::PosToFreq(n->crossoverPos[b - 1]);
         float f1 = (b == n->bandCount - 1) ? 20000.0f : AudioColorRampNode::PosToFreq(n->crossoverPos[b]);
         if (f1 >= 1000.0f)
            snprintf(label, sizeof(label), "B%d (%s-%sk)", b + 1, UiFmt::F0(f0).c, UiFmt::F1(f1 / 1000.0f).c);
         else
            snprintf(label, sizeof(label), "B%d (%s-%sHz)", b + 1, UiFmt::F0(f0).c, UiFmt::F0(f1).c);

         ColorSwatch(label, n->bandColor[b], n);
         ImGui::PopID();
      }

      DropdownButton("mode", AudioColorRampNode::ModeNames(), n->mode,
                     [n](int i) { PushUndoCheckpoint(); n->mode = i; n->MarkDirty(); });
      DropdownButton("interpolation", AudioColorRampNode::InterpNames(), n->interpMode,
                     [n](int i) { PushUndoCheckpoint(); n->interpMode = i; n->MarkDirty(); });

      int bCount = n->bandCount;
      if (ModSliderInt("bands", &bCount, params::geometry_params::b_count_lo, AudioColorRampNode::kMaxBands))
      {
         PushUndoCheckpoint();
         n->SetBandCount(bCount);
      }

      Ui::DrawParams(n, kAudioColorRampParamsRows);
   }


   namespace
   {
      using AudioRibbonParamsRow = params::Row<AudioRibbonNode>;
      const AudioRibbonParamsRow kAudioRibbonParamsRows1[] = {
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::width>("width", params::AudioRibbon::width_lo, params::AudioRibbon::width_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::heightScale>("height scale", params::AudioRibbon::height_scale_lo, params::AudioRibbon::height_scale_hi),
         AudioRibbonParamsRow::SliderInt<&AudioRibbonNode::segments>("segments", params::AudioRibbon::segments_lo, params::AudioRibbon::segments_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::gain>("gain", params::AudioRibbon::gain_lo, params::AudioRibbon::gain_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::smoothing>("smoothing", params::AudioRibbon::smoothing_lo, params::AudioRibbon::smoothing_hi),
      };
      const AudioRibbonParamsRow kAudioRibbonParamsRows2[] = {
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::posX>("pos X", params::AudioRibbon::pos_x_lo, params::AudioRibbon::pos_x_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::posY>("pos Y", params::AudioRibbon::pos_y_lo, params::AudioRibbon::pos_y_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::posZ>("pos Z", params::AudioRibbon::pos_z_lo, params::AudioRibbon::pos_z_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::rotX>("rot X", params::AudioRibbon::rot_x_lo, params::AudioRibbon::rot_x_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::rotY>("rot Y", params::AudioRibbon::rot_y_lo, params::AudioRibbon::rot_y_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::rotZ>("rot Z", params::AudioRibbon::rot_z_lo, params::AudioRibbon::rot_z_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::scaleX>("scale X", params::AudioRibbon::scale_x_lo, params::AudioRibbon::scale_x_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::scaleY>("scale Y", params::AudioRibbon::scale_y_lo, params::AudioRibbon::scale_y_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::scaleZ>("scale Z", params::AudioRibbon::scale_z_lo, params::AudioRibbon::scale_z_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::uniformScale>("uniform scale", params::AudioRibbon::uniform_scale_lo, params::AudioRibbon::uniform_scale_hi),
      };
      const AudioRibbonParamsRow kAudioRibbonParamsRows3[] = {
         AudioRibbonParamsRow::Choice<&AudioRibbonNode::shading>("shading", &GeometryNode::ShadingNames),
         AudioRibbonParamsRow::Colour<&AudioRibbonNode::color>("colour"),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::metallic>("metallic", 0.0f, 1.0f),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::roughness>("roughness", params::AudioRibbon::roughness_lo, params::AudioRibbon::roughness_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::opacity>("opacity", 0.0f, 1.0f),
         AudioRibbonParamsRow::Colour<&AudioRibbonNode::emissionColor>("emission"),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::emission>("emission", params::AudioRibbon::emission_lo, params::AudioRibbon::emission_hi),
         AudioRibbonParamsRow::Slider<&AudioRibbonNode::ior>("ior", params::AudioRibbon::ior_lo, params::AudioRibbon::ior_hi),
      };
   }

   void DrawAudioRibbonParams(AudioRibbonNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      Ui::DrawParams(n, kAudioRibbonParamsRows1);

      NodeSeparator("transform");
      Ui::DrawParams(n, kAudioRibbonParamsRows2);

      NodeSeparator("material");
      Ui::DrawParams(n, kAudioRibbonParamsRows3);
   }


   namespace
   {
      using SetColorParamsRow = params::Row<SetColorNode>;
      const SetColorParamsRow kSetColorParamsRows[] = {
         SetColorParamsRow::Colour<&SetColorNode::rampA>("ramp start"),
         SetColorParamsRow::Colour<&SetColorNode::rampB>("ramp end"),
      };
   }

   void DrawSetColorParams(SetColorNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      Ui::DrawParam(n, SetColorParamsRow::Choice<&SetColorNode::source>("source", &SetColorNode::SourceNames));
      switch (n->source)
      {
      case SetColorNode::kFlat:
         Ui::DrawParam(n, SetColorParamsRow::Colour<&SetColorNode::flatColor>("colour"));
         break;
      case SetColorNode::kIndex:
         Ui::DrawParams(n, kSetColorParamsRows);
         break;
      case SetColorNode::kRandom:
         Ui::DrawParam(n, SetColorParamsRow::Slider<&SetColorNode::seed>("seed", params::SetColor::seed_lo, params::SetColor::seed_hi).Fmt("%.0f").Step(1.0f));
         break;
      case SetColorNode::kPalette:
         Ui::DrawParam(n, SetColorParamsRow::SliderInt<&SetColorNode::paletteOffset>("palette offset", params::SetColor::palette_offset_lo, params::SetColor::palette_offset_hi));
         if (n->paletteInput == nullptr)
            ImGui::TextDisabled("patch a Palette into the palette pin");
         break;
      case SetColorNode::kTexture:
         if (!n->TextureInput().IsConnected())
            ImGui::TextDisabled("patch an image into the texture pin");
         break;
      case SetColorNode::kPosition:
      case SetColorNode::kNormal:
      default:
         break;
      }
   }


   namespace
   {
      using InstanceParamsRow = params::Row<InstanceOnPointsNode>;
      const InstanceParamsRow kInstanceParamsRows[] = {
         InstanceParamsRow::Choice<&InstanceOnPointsNode::pointMode>("points from", &InstanceOnPointsNode::SourceNames),
         InstanceParamsRow::SliderInt<&InstanceOnPointsNode::maxPoints>("max points", params::InstanceOnPoints::max_points_lo, params::InstanceOnPoints::max_points_hi),
         InstanceParamsRow::Slider<&InstanceOnPointsNode::instanceScale>("scale", params::InstanceOnPoints::instance_scale_lo, params::InstanceOnPoints::instance_scale_hi).Fmt("%.4f").Curve(LogPosToValue, LogValueToPos),
         InstanceParamsRow::Slider<&InstanceOnPointsNode::scaleRandom>("scale random", 0.0f, 1.0f),
         InstanceParamsRow::Slider<&InstanceOnPointsNode::rotationRandom>("rotation random", 0.0f, 1.0f),
         InstanceParamsRow::Slider<&InstanceOnPointsNode::normalOffset>("normal offset", params::InstanceOnPoints::normal_offset_lo, params::InstanceOnPoints::normal_offset_hi),
         InstanceParamsRow::Toggle<&InstanceOnPointsNode::alignToNormal>("align to normal"),
         InstanceParamsRow::Slider<&InstanceOnPointsNode::seed>("seed", params::InstanceOnPoints::seed_lo, params::InstanceOnPoints::seed_hi).Fmt("%.0f").Step(1.0f),
      };
   }

   void DrawInstanceParams(InstanceOnPointsNode* n)
   {
      if (n->pointSource != nullptr && n->instanceShape != nullptr)
         ImGui::TextDisabled("%s instances, %s triangles", UiFmt::Count((long long)(n->InstanceCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);

      Ui::DrawParams(n, kInstanceParamsRows);
   }


   // The 3D View node: each placed item's Motion (location, rotation, scale on top of where it was put) as
   // params, so a cable, MIDI or an expression can move it. The sliders shown are the item selected in the 3D View
   // (or picked here); every other item's nine still register, unseen, so their modulation keeps playing.
   void DrawScene3DParams(Scene3DNode* n)
   {
      auto& items = n->Items();
      std::vector<std::string> names;
      std::vector<int> ids;
      for (const Scene3DNode::Item& it : items)
         if (it.motionSlot != 0)
         {
            const GraphNode* gn = FindNodeByUid(it.uid);
            names.push_back(!it.name.empty() ? it.name : gn != nullptr ? gn->typeName + " " + std::to_string(gn->index) : "source missing");
            ids.push_back(it.id);
         }
      if (ids.empty())
      {
         ImGui::TextDisabled("Add something in the 3D View to move it from here");
         return;
      }
      int shown = 0;
      for (int i = 0; i < (int)ids.size(); i++)
         if (ids[i] == n->motionShown)
            shown = i;
      DropdownButton("motion of", names, shown, [n, ids](int i) { n->motionShown = ids[i]; });
      static const float kLo[3] = { -20.0f, -360.0f, 0.0f }, kHi[3] = { 20.0f, 360.0f, 4.0f };
      static const char* kFmt[3] = { "%.2f m", "%.1f\xC2\xB0", "%.3f" };
      const bool keptOnly = gParamRegisterOnly;
      for (int i = 0; i < (int)ids.size(); i++)
      {
         Scene3DNode::Item* it = n->FindItem(ids[i]);
         gParamRegisterOnly = keptOnly || i != shown;
         for (int c = 0; c < 9; c++)
         {
            const int group = c / 3;
            const std::string full = names[i] + " " + Scene3DNode::MotionChannelName(c);
            ModSlider(Scene3DNode::MotionChannelName(c), &it->motion[c], kLo[group], kHi[group], kFmt[group], kParamWidth,
                      false, 0.0f, nullptr, nullptr, Scene3DNode::kMotionParamBase + (it->motionSlot - 1) * 9 + c, full.c_str());   // ui-audit-ok: index (motion param id layout, 9 channels per slot)
         }
      }
      gParamRegisterOnly = keptOnly;
   }

   namespace
   {
      using CameraParamsRow = params::Row<CameraNode>;
      const CameraParamsRow kCameraParamsRows[] = {
         CameraParamsRow::Slider<&CameraNode::distance>("distance", params::Camera::distance_lo, params::Camera::distance_hi),
         CameraParamsRow::Slider<&CameraNode::azimuth>("orbit", params::Camera::azimuth_lo, params::Camera::azimuth_hi).Fmt("%.1f\xC2\xB0"),
         CameraParamsRow::Slider<&CameraNode::elevation>("elevation", params::Camera::elevation_lo, params::Camera::elevation_hi).Fmt("%.1f\xC2\xB0"),
         CameraParamsRow::Slider<&CameraNode::roll>("roll", params::Camera::roll_lo, params::Camera::roll_hi).Fmt("%.1f\xC2\xB0"),
         CameraParamsRow::Slider<&CameraNode::orbitPerBeat>("orbit / beat", -1.0f, 1.0f),
         CameraParamsRow::Slider<&CameraNode::targetX>("target x", params::Camera::target_x_lo, params::Camera::target_x_hi),
         CameraParamsRow::Slider<&CameraNode::targetY>("target y", params::Camera::target_y_lo, params::Camera::target_y_hi),
         CameraParamsRow::Slider<&CameraNode::targetZ>("target z", params::Camera::target_z_lo, params::Camera::target_z_hi),
         CameraParamsRow::Slider<&CameraNode::nearPlane>("near", params::Camera::near_plane_lo, params::Camera::near_plane_hi),
         CameraParamsRow::Slider<&CameraNode::farPlane>("far", params::Camera::far_plane_lo, params::Camera::far_plane_hi),
      };
   }

   void DrawCameraParams(CameraNode* n)
   {
      Ui::DrawParam(n, CameraParamsRow::Choice<&CameraNode::projection>("projection", &CameraNode::ProjectionNames));
      if (n->projection == 0)
         Ui::DrawParam(n, CameraParamsRow::Slider<&CameraNode::fov>("fov", params::Camera::fov_lo, params::Camera::fov_hi));
      else
         Ui::DrawParam(n, CameraParamsRow::Slider<&CameraNode::orthoHeight>("ortho height", params::Camera::ortho_height_lo, params::Camera::ortho_height_hi));
      Ui::DrawParams(n, kCameraParamsRows);
   }


   namespace
   {
      using LightParamsRow = params::Row<LightNode>;
      const LightParamsRow kLightParamsRows1[] = {
         LightParamsRow::Choice<&LightNode::type>("type", &LightNode::TypeNames),
         LightParamsRow::Slider<&LightNode::azimuth>("orbit", params::Light::azimuth_lo, params::Light::azimuth_hi).Fmt("%.1f\xC2\xB0"),
         LightParamsRow::Slider<&LightNode::elevation>("elevation", params::Light::elevation_lo, params::Light::elevation_hi).Fmt("%.1f\xC2\xB0"),
      };
      const LightParamsRow kLightParamsRows2[] = {
         LightParamsRow::Slider<&LightNode::spotAngle>("cone angle", params::Light::spot_angle_lo, params::Light::spot_angle_hi).Fmt("%.1f\xC2\xB0"),
         LightParamsRow::Slider<&LightNode::spotPenumbra>("penumbra", 0.0f, 1.0f),
      };
      const LightParamsRow kLightParamsRows3[] = {
         LightParamsRow::Slider<&LightNode::areaWidth>("width", params::Light::area_width_lo, params::Light::area_width_hi),
         LightParamsRow::Slider<&LightNode::areaHeight>("height", params::Light::area_height_lo, params::Light::area_height_hi),
      };
      const LightParamsRow kLightParamsRows4[] = {
         LightParamsRow::Colour<&LightNode::color>("colour"),
         LightParamsRow::Toggle<&LightNode::useTemperature>("temperature"),
      };
      const LightParamsRow kLightParamsRows5[] = {
         LightParamsRow::Slider<&LightNode::intensity>("intensity", params::Light::intensity_lo, params::Light::intensity_hi),
         LightParamsRow::Slider<&LightNode::exposure>("exposure", params::Light::exposure_lo, params::Light::exposure_hi).Fmt("%.2f"),  // white by +3, black by -6
      };
      const LightParamsRow kLightParamsRows6[] = {
         LightParamsRow::Slider<&LightNode::falloff>("falloff", params::Light::falloff_lo, params::Light::falloff_hi),
         LightParamsRow::Toggle<&LightNode::useCutoff>("cutoff"),
      };
      const LightParamsRow kLightParamsRows7[] = {
         LightParamsRow::Slider<&LightNode::diffuse>("diffuse", params::Light::diffuse_lo, params::Light::diffuse_hi),
         LightParamsRow::Slider<&LightNode::specular>("specular", params::Light::specular_lo, params::Light::specular_hi),
         LightParamsRow::Slider<&LightNode::orbitPerBeat>("orbit / beat", -1.0f, 1.0f),
      };
   }

   void DrawLightParams(LightNode* n)
   {
      Ui::DrawParams(n, kLightParamsRows1);
      // Every row is always drawn; a row the chosen type does not use is greyed, so the node never changes size.
      Ui::DrawParam(n, LightParamsRow::Slider<&LightNode::distance>("distance", params::Light::distance_lo, params::Light::distance_hi), n->type == 1 || n->type == 4 || n->type == 5);
      Ui::DrawParams(n, kLightParamsRows2, n->type == 4);
      {
         Ui::RowsLive on(n->type == 5);
         Ui::DrawParams(n, kLightParamsRows3);
         static const std::vector<std::string> kShapes = { "Rectangle", "Disc" };
         DropdownButton("shape", kShapes, n->areaShape, [n](int i) { n->areaShape = i; });
         ModSlider("spread", &n->areaSpread, params::Light::area_spread_lo, params::Light::area_spread_hi, "%.0f\xC2\xB0");
      }
      // Row 4.2: point and spot take an IES profile; the button names the file once one is set.
      {
         Ui::RowsLive on(n->type == 1 || n->type == 4);
         const bool has = !n->iesFile.empty();
         const std::string label = has ? AppPaths::FsPath(n->iesFile).filename().u8string() : std::string("IES profile...");
         if (ActionButton::Draw(label.c_str(), ImVec2(has ? kPreviewSize * tok::geometry_params_light_ratio : kPreviewSize, 0)))
         {
            const std::string path = Platform::OpenIesDialog();
            if (!path.empty()) { PushUndoCheckpoint(); n->iesFile = path; }
         }
         if (has)
         {
            Ui::SameRow();
            if (ActionButton::Draw("Clear", ImVec2(kPreviewSize * tok::geometry_params_clear_x, 0))) { PushUndoCheckpoint(); n->iesFile.clear(); }
         }
      }
      Ui::DrawParams(n, kLightParamsRows4);
      Ui::DrawParam(n, LightParamsRow::Slider<&LightNode::temperature>("kelvin", params::Light::temperature_lo, params::Light::temperature_hi).Fmt("%.0f K"), n->useTemperature);
      Ui::DrawParams(n, kLightParamsRows5);
      {
         Ui::RowsLive on(n->type == 1 || n->type == 4 || n->type == 5);
         Ui::DrawParams(n, kLightParamsRows6);
         Ui::DrawParam(n, LightParamsRow::Slider<&LightNode::cutoffDistance>("cutoff distance", params::Light::cutoff_distance_lo, params::Light::cutoff_distance_hi), n->useCutoff);
      }
      Ui::DrawParams(n, kLightParamsRows7);
   }


   // Fits the camera around everything patched into a Render node.
   //
   // Bounds are taken from each mesh's object-space corners pushed through its
   // model matrix, rather than from every vertex: a rotated box's true extent
   // needs the corners, and a scene can carry hundreds of thousands of vertices
   // that would be pointless to walk for a button press.
   void FrameSceneInView(Render3DNode* n)
   {
      float lo[3] = { 1e30f, 1e30f, 1e30f };
      float hi[3] = { -1e30f, -1e30f, -1e30f };
      bool any = false;

      for (IGeometrySource* source : n->FlattenedGeometry())
      {
         if (source == nullptr)
            continue;
         const Mesh& mesh = source->GetMesh();

         float mlo[3] = { 1e30f, 1e30f, 1e30f };
         float mhi[3] = { -1e30f, -1e30f, -1e30f };
         if (!mesh.Empty())
         {
            for (const Vertex& v : mesh.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               for (int k = 0; k < 3; k++)
               {
                  if (!std::isfinite(p[k]))
                     continue;
                  mlo[k] = std::min(mlo[k], p[k]);
                  mhi[k] = std::max(mhi[k], p[k]);
               }
            }
         }
         else
         {
            // D5 (geometry-domains audit, Phase 4): mesh:standin producers
            // (Mesh to Points, Distribute on Faces/in Grid, Image to Points)
            // now return an honestly empty GetMesh() - fall back to the point
            // cloud's own bounds so camera-fit doesn't lose them.
            const std::vector<Particle>* cloud = source->GetPointCloud();
            if (cloud != nullptr)
            {
               const float baseSize = source->PointBaseSize();
               for (const Particle& p : *cloud)
               {
                  const float r = baseSize * p.scale;
                  const float lo3[3] = { p.px - r, p.py - r, p.pz - r };
                  const float hi3[3] = { p.px + r, p.py + r, p.pz + r };
                  for (int k = 0; k < 3; k++)
                  {
                     if (!std::isfinite(lo3[k]) || !std::isfinite(hi3[k]))
                        continue;
                     mlo[k] = std::min(mlo[k], lo3[k]);
                     mhi[k] = std::max(mhi[k], hi3[k]);
                  }
               }
            }
         }
         if (mlo[0] > mhi[0])
            continue;

         const Mat4 model = source->GetModelMatrix();
         for (int corner = 0; corner < 8; corner++)
         {
            const float c[3] = {
               (corner & 1) ? mhi[0] : mlo[0],
               (corner & 2) ? mhi[1] : mlo[1],
               (corner & 4) ? mhi[2] : mlo[2]
            };
            for (int k = 0; k < 3; k++)
            {
               const float w = model.m[k] * c[0] + model.m[4 + k] * c[1] +
                               model.m[8 + k] * c[2] + model.m[12 + k];
               lo[k] = std::min(lo[k], w);
               hi[k] = std::max(hi[k], w);
            }
         }
         any = true;
      }

      if (!any)
         return;

      const float centre[3] = { (lo[0]+hi[0])*0.5f, (lo[1]+hi[1])*0.5f, (lo[2]+hi[2])*0.5f };
      const float radius = 0.5f * std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0]) +
                                            (hi[1]-lo[1])*(hi[1]-lo[1]) +
                                            (hi[2]-lo[2])*(hi[2]-lo[2]));

      // Distance that puts the bounding sphere just inside the vertical field
      // of view, with a little air around it.
      const float fovDegrees = n->camera ? n->camera->fov : n->fov;
      const float halfFov = std::max(0.1f, fovDegrees * 0.5f * 3.14159265f / 180.0f);
      const float distance = std::max(0.3f, (radius / std::sin(halfFov)) * 1.15f);

      if (n->camera != nullptr)
      {
         n->camera->targetX = centre[0];
         n->camera->targetY = centre[1];
         n->camera->targetZ = centre[2];
         n->camera->distance = distance;
      }
      else
      {
         n->targetX = centre[0];
         n->targetY = centre[1];
         n->targetZ = centre[2];
         n->camDistance = distance;
      }
   }


   namespace
   {
      using Render3DParamsRow = params::Row<Render3DNode>;
      const Render3DParamsRow kRender3DParamsRows1[] = {
         Render3DParamsRow::Slider<&Render3DNode::mistStart>("mist start", params::Render3D::mist_start_lo, params::Render3D::mist_start_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::mistDepth>("mist depth", params::Render3D::mist_depth_lo, params::Render3D::mist_depth_hi).Fmt("%.2f"),
         Render3DParamsRow::Choice<&Render3DNode::mistFalloff>("mist falloff", &Render3DNode::MistFalloffNames),
         Render3DParamsRow::Slider<&Render3DNode::mistIntensity>("mist intensity", 0.0f, 1.0f).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::mistHeight>("mist height", params::Render3D::mist_height_lo, params::Render3D::mist_height_hi).Fmt("%.2f"),
      };
      const Render3DParamsRow kRender3DParamsRows2[] = {
         Render3DParamsRow::Slider<&Render3DNode::width>("width", params::Render3D::width_lo, params::Render3D::width_hi).Fmt("%.0f"),
         Render3DParamsRow::Slider<&Render3DNode::height>("height", params::Render3D::height_lo, params::Render3D::height_hi).Fmt("%.0f"),
      };
      const Render3DParamsRow kRender3DParamsRows3[] = {
         Render3DParamsRow::Choice<&Render3DNode::tonemap>("tonemap", &Render3DNode::TonemapNames),
         Render3DParamsRow::Slider<&Render3DNode::exposure>("exposure", params::Render3D::exposure_lo, params::Render3D::exposure_hi),
      };
      const Render3DParamsRow kRender3DParamsRows4[] = {
         Render3DParamsRow::Colour<&Render3DNode::bgColor>("background"),
         Render3DParamsRow::Slider<&Render3DNode::bgOpacity>("bg opacity", 0.0f, 1.0f),
      };
      const Render3DParamsRow kRender3DParamsRows5[] = {
         Render3DParamsRow::Slider<&Render3DNode::latMin>("min latitude", params::Render3D::lat_min_lo, params::Render3D::lat_min_hi).Fmt("%.0f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::latMax>("max latitude", params::Render3D::lat_max_lo, params::Render3D::lat_max_hi).Fmt("%.0f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::lonMin>("min longitude", params::Render3D::lon_min_lo, params::Render3D::lon_min_hi).Fmt("%.0f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::lonMax>("max longitude", params::Render3D::lon_max_lo, params::Render3D::lon_max_hi).Fmt("%.0f\xC2\xB0"),
      };
      const Render3DParamsRow kRender3DParamsRows6[] = {
         Render3DParamsRow::Slider<&Render3DNode::camDistance>("distance", params::Render3D::cam_distance_lo, params::Render3D::cam_distance_hi),
         Render3DParamsRow::Slider<&Render3DNode::camAzimuth>("orbit", params::Render3D::cam_azimuth_lo, params::Render3D::cam_azimuth_hi).Fmt("%.1f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::camElevation>("elevation", params::Render3D::cam_elevation_lo, params::Render3D::cam_elevation_hi).Fmt("%.1f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::targetX>("target x", params::Render3D::target_x_lo, params::Render3D::target_x_hi),
         Render3DParamsRow::Slider<&Render3DNode::targetY>("target y", params::Render3D::target_y_lo, params::Render3D::target_y_hi),
         Render3DParamsRow::Slider<&Render3DNode::targetZ>("target z", params::Render3D::target_z_lo, params::Render3D::target_z_hi),
      };
      const Render3DParamsRow kRender3DParamsRows7[] = {
         Render3DParamsRow::Choice<&Render3DNode::spriteShape>("sprite shape", &Render3DNode::SpriteShapeNames),
         Render3DParamsRow::Choice<&Render3DNode::spriteSizeMode>("sprite size", &Render3DNode::SpriteSizeModeNames),
      };
      const Render3DParamsRow kRender3DParamsRows8[] = {
         Render3DParamsRow::Toggle<&Render3DNode::depthTest>("depth test"),
         Render3DParamsRow::Toggle<&Render3DNode::backfaceCull>("cull backfaces"),
      };
      const Render3DParamsRow kRender3DParamsRows9[] = {
         Render3DParamsRow::Slider<&Render3DNode::lightAzimuth>("light orbit", params::Render3D::light_azimuth_lo, params::Render3D::light_azimuth_hi).Fmt("%.1f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::lightElevation>("light height", params::Render3D::light_elevation_lo, params::Render3D::light_elevation_hi).Fmt("%.1f\xC2\xB0"),
         Render3DParamsRow::Colour<&Render3DNode::lightColor>("light"),
         Render3DParamsRow::Slider<&Render3DNode::lightIntensity>("intensity", params::Render3D::light_intensity_lo, params::Render3D::light_intensity_hi),
      };
      const Render3DParamsRow kRender3DParamsRows10[] = {
         Render3DParamsRow::Colour<&Render3DNode::ambientColor>("ambient"),
         Render3DParamsRow::Slider<&Render3DNode::rimIntensity>("rim", params::Render3D::rim_intensity_lo, params::Render3D::rim_intensity_hi),
      };
      const Render3DParamsRow kRender3DParamsRows11[] = {
         Render3DParamsRow::Slider<&Render3DNode::shadowSoftness>("softness", params::Render3D::shadow_softness_lo, params::Render3D::shadow_softness_hi),
         Render3DParamsRow::Slider<&Render3DNode::shadowBias>("bias", params::Render3D::shadow_bias_lo, params::Render3D::shadow_bias_hi).Fmt("%.4f"),
      };
      const Render3DParamsRow kRender3DParamsRows12[] = {
         Render3DParamsRow::Colour<&Render3DNode::envSky>("sky"),
         Render3DParamsRow::Colour<&Render3DNode::envHorizon>("horizon"),
         Render3DParamsRow::Colour<&Render3DNode::envGround>("ground"),
      };
      const Render3DParamsRow kRender3DParamsRows13[] = {
         Render3DParamsRow::Choice<&Render3DNode::cavityType>("cavity type", &Render3DNode::CavityTypeNames),
         Render3DParamsRow::Slider<&Render3DNode::cavityRidge>("cavity ridge", params::Render3D::cavity_ridge_lo, params::Render3D::cavity_ridge_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::cavityValley>("cavity valley", params::Render3D::cavity_valley_lo, params::Render3D::cavity_valley_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::cavityDistance>("cavity distance", params::Render3D::cavity_distance_lo, params::Render3D::cavity_distance_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::curvatureRidge>("curvature ridge", params::Render3D::curvature_ridge_lo, params::Render3D::curvature_ridge_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::curvatureValley>("curvature valley", params::Render3D::curvature_valley_lo, params::Render3D::curvature_valley_hi).Fmt("%.2f"),
      };
      const Render3DParamsRow kRender3DParamsRows14[] = {
         Render3DParamsRow::Colour<&Render3DNode::fogColor>("fog colour"),
         Render3DParamsRow::Slider<&Render3DNode::fogAnisotropy>("fog anisotropy", params::Render3D::fog_anisotropy_lo, params::Render3D::fog_anisotropy_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::fogFalloff>("fog falloff", 0.0f, 1.0f).Fmt("%.2f"),
      };
      const Render3DParamsRow kRender3DParamsRows15[] = {
         Render3DParamsRow::Slider<&Render3DNode::focusDistance>("focus distance", params::Render3D::focus_distance_lo, params::Render3D::focus_distance_hi),
         Render3DParamsRow::SliderInt<&Render3DNode::dofBlades>("aperture blades", params::Render3D::dof_blades_lo, params::Render3D::dof_blades_hi),
         Render3DParamsRow::Slider<&Render3DNode::dofRotation>("aperture rotation", params::Render3D::dof_rotation_lo, params::Render3D::dof_rotation_hi).Fmt("%.0f\xC2\xB0"),
         Render3DParamsRow::Slider<&Render3DNode::dofRatio>("aperture ratio", params::Render3D::dof_ratio_lo, params::Render3D::dof_ratio_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::dofCatEye>("cat eye", 0.0f, 1.0f).Fmt("%.2f"),
      };
      const Render3DParamsRow kRender3DParamsRows16[] = {
         Render3DParamsRow::Slider<&Render3DNode::lensDistortion>("lens distortion", params::Render3D::lens_distortion_lo, params::Render3D::lens_distortion_hi),
         Render3DParamsRow::Slider<&Render3DNode::lensCA>("chromatic aberration", params::Render3D::lens_ca_lo, params::Render3D::lens_ca_hi).Fmt("%.4f"),
      };
      const Render3DParamsRow kRender3DParamsRows17[] = {
         Render3DParamsRow::Slider<&Render3DNode::bloomThreshold>("bloom threshold", params::Render3D::bloom_threshold_lo, params::Render3D::bloom_threshold_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::bloomRadius>("bloom radius", 0.0f, 1.0f),
      };
      const Render3DParamsRow kRender3DParamsRows18[] = {
         Render3DParamsRow::Choice<&Render3DNode::motionPosition>("motion position", &Render3DNode::MotionPositionNames),
         Render3DParamsRow::Slider<&Render3DNode::motionShutter>("motion shutter", params::Render3D::motion_shutter_lo, params::Render3D::motion_shutter_hi).Fmt("%.2f"),
         Render3DParamsRow::Slider<&Render3DNode::motionMax>("motion max blur", params::Render3D::motion_max_lo, params::Render3D::motion_max_hi).Fmt("%.0f"),
      };
      const Render3DParamsRow kRender3DParamsRows19[] = {
         Render3DParamsRow::SliderInt<&Render3DNode::ptSamples>("samples", params::Render3D::pt_samples_lo, params::Render3D::pt_samples_hi),
         Render3DParamsRow::SliderInt<&Render3DNode::ptBounces>("bounces", params::Render3D::pt_bounces_lo, params::Render3D::pt_bounces_hi),
      };
   }

   void DrawRender3DParams(Render3DNode* n)
   {
      const int connected = (int)n->FlattenedGeometry().size();
      ImGui::TextDisabled("%d geometry, %s camera", connected, n->camera ? "patched" : "built-in");
      Ui::SameRow(Ui::Space::S4);
      if (n->LastGpuBytes() > 0)
         ImGui::TextDisabled("%s triangles in %zu draw calls, %s GPU", UiFmt::Count((long long)(n->LastTriangleCount())).c, n->LastDrawCalls(), UiFmt::MB((double)n->LastGpuBytes() / 1048576.0).c);
      else
         ImGui::TextDisabled("%s triangles in %zu draw calls", UiFmt::Count((long long)(n->LastTriangleCount())).c, n->LastDrawCalls());
      {
         // The count changes every frame; pad to a fixed worst-case width so the node body doesn't resize with the digits.
         const ImVec2 textMin = ImGui::GetItemRectMin();
         const ImVec2 textSize = ImGui::GetItemRectSize();
         const float fixedW = ImGui::CalcTextSize(n->LastGpuBytes() > 0 ? "00000000 triangles in 000 draw calls, 0000 MB GPU" : "00000000 triangles in 000 draw calls").x;
         if (textSize.x < fixedW)
         {
            ImGui::SetCursorScreenPos(ImVec2(textMin.x + textSize.x, textMin.y));
            Ui::Claim(ImVec2(fixedW - textSize.x, textSize.y));
         }
      }
      if (!n->EngineError().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextColored(tok::RoleV4(tok::col::text_warn), "%s", n->EngineError().c_str());
         PopTextWrapWidth();
      }

      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = tok::geometry_params_gutter;

      // The rarely-touched rows sit behind one node-wide "show more" (GraphNode::showAdvancedParams, saved with
      // the patch), the first row of the right column, level with Frame scene on the left. Folded rows are still registered, in the
      // same order, so indices, modulation, expressions and the matrix are unchanged; a folded row with a cable,
      // expression or palette binding stays shown (see hideBegin) and its pin with it.
      GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex);
      const bool pinned = gn != nullptr && gn->showAdvancedParams;
      static std::map<int, bool> sFoldedInUse;   // last frame's "a folded row is shown because it is driven"
      bool& foldedInUse = sFoldedInUse[gCurrentNodeIndex];
      const bool wasInUse = foldedInUse;
      foldedInUse = false;
      const bool savedRegisterOnly = gParamRegisterOnly;
      // Starts a folded group of rows: how many float params (ModSlider/ModSliderInt) and colours it registers and
      // which discrete params (checkbox/dropdown labels). Shown when pinned open, forced, or any of them is driven.
      auto hideBegin = [&](int floatCount, int colorCount, std::initializer_list<const char*> discrete,
                           bool force = false) {
         bool inUse = false;
         for (int k = 0; k < colorCount && !inUse; k++)
            inUse = PaletteBinding::Instance().IsBound(gCurrentNodeIndex, gColorCounter + k);
         for (int k = 0; k < floatCount && !inUse; k++)
            inUse = Modulation::Instance().IsModulated(gCurrentNodeIndex, gParamCounter + k) ||
                    Modulation::Instance().HasExpression(gCurrentNodeIndex, gParamCounter + k);
         for (const char* d : discrete)
            if (!inUse)
               inUse = Modulation::Instance().IsModulated(gCurrentNodeIndex, DiscreteParamSlot(gCurrentNodeIndex, d));
         if (inUse && !pinned)
            foldedInUse = true;
         gParamRegisterOnly = savedRegisterOnly || !(pinned || inUse || force);
      };
      auto hideEnd = [&]() { gParamRegisterOnly = savedRegisterOnly; };

      // Both columns draw every row every frame: a row that does not apply right now is greyed, never
      // dropped, so nothing grows, shifts or leaves a gap when a switch flips or a param is added.
      // Sections are drawn in one fixed order (so param ordinals never move) and split into two columns at a point
      // that depends on the fold: shown less, output/camera/raster | light/shadows/environment/quality; shown more,
      // light moves to the left so both columns stay within a few rows of each other.
      char label[64];
      // Same clamp the renderer applies (Render3DNode::Cook): a patch can hold any int here.
      const int aaIndex = std::clamp(n->samples, 0, (int)Render3DNode::SampleNames().size() - 1);
      const int wantedAa = (aaIndex == 0) ? 0 : (1 << aaIndex);

      std::vector<std::function<void()>> sections;
      // Path trace replaces the raster lighting tricks below with real light transport, so the rows it
      // has no use for (shadow maps, AO, rim, bloom, motion blur, antialias) grey while it is on.
      const bool pt = n->PathTraceActive();

      sections.push_back([&]() {
      NodeSeparator("output", colW);
      // Path trace renders the beauty pass of a perspective or orthographic camera only.
      Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::engine>("engine", &Render3DNode::EngineNames), !(n->renderPass != 0 || n->SolidActive() || n->PanoActive()));
      Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::renderPass>("pass", &Render3DNode::RenderPassNames));
      // Row 7.3: depth, normal, albedo, mask and id as extra outputs beside the image.
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::passes>("passes"), !(n->renderPass != 0 || n->PanoActive()));
      if (n->passes && n->renderPass == 0)
         Ui::DrawParam(n, Render3DParamsRow::SliderInt<&Render3DNode::passMask>("mask index", params::Render3D::pass_mask_lo, params::Render3D::pass_mask_hi));
      if (n->renderPass >= 1 && n->renderPass <= 9)
         Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::passThreshold>("pass alpha threshold", 0.0f, 1.0f).Fmt("%.2f"));
      if (n->renderPass == 5)
      {
         Ui::DrawParams(n, kRender3DParamsRows1);
      }
      // This is the export resolution: Output sizes its own buffer from whatever
      // its input hands it, so a 4000px PNG needs 4000 set here or it is an
      // upscale of a smaller render.
      Ui::DrawParams(n, kRender3DParamsRows2);
      Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::samples>("antialias", &Render3DNode::SampleNames), !pt);
      // The requested sample count is clamped by both the driver and a memory
      // budget, so say what actually happened when they disagree.
      if (n->ActiveSamples() != wantedAa && ImGui::IsItemHovered())
         Tip::Show("antialias reduced to %dx at this size", n->ActiveSamples());
      Ui::DrawParams(n, kRender3DParamsRows3);
      // HDR lights the scene in float and tonemaps once at the end, so bloom
      // sees light above white. Only the beauty pass has light to keep.
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::hdr>("hdr"), !(n->renderPass != 0 || pt));
      Ui::DrawParams(n, kRender3DParamsRows4);
      hideBegin(0, 0, {"16-bit float output"});
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::floatOutput>("16-bit float output"));
      hideEnd();
      });

      sections.push_back([&]() {
      snprintf(label, sizeof(label), n->camera != nullptr ? "camera - from a Camera node" : "camera");
      NodeSeparator(label, colW);
      {
         // This render's own camera, entirely separate from any node's
         // mini-viewport/panel camera (SharedViewportCamera.h/gNodeCameras):
         // the final output framing is a deliberate setup, not something
         // casual node-viewport orbiting should ever move.
         Ui::RowsLive on(n->camera == nullptr);
         Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::projection>("projection", &Render3DNode::ProjectionNames));
         // One slot, three meanings: the field of view for perspective, the view
         // height for orthographic, the dome's angle for a fisheye. A 360 or a
         // cubemap always sees everything, so fov stays, greyed.
         if (n->projection == Render3DNode::kOrthographic)
            Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::orthoHeight>("ortho height", params::Render3D::ortho_height_lo, params::Render3D::ortho_height_hi));
         else if (n->projection == Render3DNode::kFisheye)
            Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::domeAngle>("dome angle", params::Render3D::dome_angle_lo, params::Render3D::dome_angle_hi).Fmt("%.0f\xC2\xB0"));
         else
         {
            Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::fov>("fov", params::Render3D::fov_lo, params::Render3D::fov_hi), n->projection == Render3DNode::kPerspective);
         }
         hideBegin(1, 0, {}, n->projection == Render3DNode::kFisheye);
         Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::domeTilt>("dome tilt", params::Render3D::dome_tilt_lo, params::Render3D::dome_tilt_hi).Fmt("%.0f\xC2\xB0"), n->projection == Render3DNode::kFisheye);
         hideEnd();
         if (n->projection == Render3DNode::kFisheye)
            Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::fisheyeLens>("lens", &Render3DNode::FisheyeLensNames));
         if (n->projection == Render3DNode::kEquirect || n->projection == Render3DNode::kCylindrical)
         {
            Ui::DrawParams(n, kRender3DParamsRows5);
         }
         Ui::DrawParams(n, kRender3DParamsRows6);
      }
      });

      sections.push_back([&]() {
      hideBegin(0, 0, {"sprite shape", "sprite size"});
      if (!gParamRegisterOnly)
         NodeSeparator("points", colW);
      Ui::DrawParams(n, kRender3DParamsRows7);
      hideEnd();
      });

      sections.push_back([&]() {
      NodeSeparator("raster", colW);
      Ui::DrawParams(n, kRender3DParamsRows8, !pt);
      });

      sections.push_back([&]() {
      int patchedLights = 0;
      for (int i = 0; i < Render3DNode::kLightSlots; i++)
         if (n->lights[i] != nullptr)
            patchedLights++;
      snprintf(label, sizeof(label), patchedLights > 0 ? "light - %d from Light nodes" : "light", patchedLights);
      NodeSeparator(label, colW);
      Ui::DrawParams(n, kRender3DParamsRows9, !(patchedLights > 0));
      Ui::DrawParams(n, kRender3DParamsRows10, !pt);
      });

      sections.push_back([&]() {
      NodeSeparator("shadows", colW);
      Ui::RowsLive traced(!pt);   // every traced light casts its own shadow
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::shadowsEnabled>("cast shadows"));
      Ui::RowsLive on(n->shadowsEnabled);
      hideBegin(0, 0, {"resolution"});
      Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::shadowQuality>("resolution", &Render3DNode::ShadowQualityNames));
      hideEnd();
      Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::shadowStrength>("strength", 0.0f, 1.0f));
      hideBegin(2, 0, {});
      Ui::DrawParams(n, kRender3DParamsRows11);
      hideEnd();
      });

      sections.push_back([&]() {
      const bool envConnected = n->envInput.IsConnected();
      NodeSeparator(envConnected ? "environment - from an HDRI node" : "environment", colW);
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::envAsBackground>("use as background"));
      Ui::DrawParams(n, kRender3DParamsRows12, !envConnected);
      Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::envIntensity>("env intensity", params::Render3D::env_intensity_lo, params::Render3D::env_intensity_hi));
      });

      sections.push_back([&]() {
      NodeSeparator("quality", colW);
      Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::aoStrength>("ambient occlusion", 0.0f, 1.0f), !pt);
      hideBegin(1, 0, {});
      Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::aoRadius>("occlusion radius", params::Render3D::ao_radius_lo, params::Render3D::ao_radius_hi), !(n->aoStrength <= 0.0f || pt));
      hideEnd();
      Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::viewShading>("shading", &Render3DNode::ViewShadingNames));
      if (n->viewShading == 1)
      {
         Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::solidLighting>("solid lighting", &Render3DNode::SolidLightingNames));
         if (n->solidLighting == 1)
            Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::matcap>("matcap", &Render3DNode::MatcapNames));
         if (n->solidLighting == 0)
         {
            Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::studioWorld>("studio world space"));
            Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::studioRotation>("studio rotation", params::Render3D::studio_rotation_lo, params::Render3D::studio_rotation_hi).Fmt("%.0f"), n->studioWorld);
            Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::solidSpecular>("solid specular"));
         }
         Ui::DrawParam(n, Render3DParamsRow::Choice<&Render3DNode::solidColor>("solid color", &Render3DNode::SolidColorNames));
         if (n->solidColor == 2)
            Ui::DrawParam(n, Render3DParamsRow::Colour<&Render3DNode::solidSingle>("single color"));
      }
      hideBegin(5, 0, {"cavity", "cavity type"});
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::cavity>("cavity"));
      Ui::DrawParams(n, kRender3DParamsRows13, n->cavity);
      hideEnd();
      Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::fogDensity>("fog", params::Render3D::fog_density_lo, params::Render3D::fog_density_hi));
      if (n->fogDensity > 0.0f)
      {
         Ui::DrawParams(n, kRender3DParamsRows14);
         ImGui::SetNextItemWidth(colW); ImGui::DragFloat3("fog min", n->fogMin, params::geometry_params::fog_min_speed, params::geometry_params::fog_min_lo, params::geometry_params::fog_min_hi, "%.2f");
         ImGui::SetNextItemWidth(colW); ImGui::DragFloat3("fog max", n->fogMax, params::geometry_params::fog_max_speed, params::geometry_params::fog_max_lo, params::geometry_params::fog_max_hi, "%.2f");
      }
      // A panorama has no focal plane. 16: a 100 mm macro at f/2.8.
      Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::dofAperture>("depth of field", params::Render3D::dof_aperture_lo, params::Render3D::dof_aperture_hi), !n->PanoActive());
      hideBegin(8, 0, {});   // focus distance, aperture x3, cat eye, distortion, CA, offline supersample
      Ui::DrawParams(n, kRender3DParamsRows15, !(n->dofAperture <= 0.0f || n->PanoActive()));
      Ui::DrawParams(n, kRender3DParamsRows16);
      Ui::DrawParam(n, Render3DParamsRow::SliderInt<&Render3DNode::supersample>("offline supersample", params::Render3D::supersample_lo, params::Render3D::supersample_hi), !pt);
      hideEnd();
      {
         Ui::RowsLive on(n->LiveActive() || (n->HdrActive() && !pt)); // the live engine blooms its own picture (row 6.3)
         Ui::DrawParam(n, Render3DParamsRow::Slider<&Render3DNode::bloom>("bloom", 0.0f, 1.0f));
         hideBegin(2, 0, {});
         Ui::DrawParams(n, kRender3DParamsRows17, !(n->bloom <= 0.0f));
         hideEnd();
      }
      Ui::RowsLive smear(!(n->PanoActive() || pt)); // a panorama has no previous frame to smear from
      Ui::DrawParam(n, Render3DParamsRow::Toggle<&Render3DNode::motionBlur>("motion blur"));
      Ui::RowsLive on(n->motionBlur);
      hideBegin(2, 0, {"motion position"});
      Ui::DrawParams(n, kRender3DParamsRows18);
      hideEnd();
      });

      // Last, so the two new sliders take the next ordinals and no earlier one moves.
      sections.push_back([&]() {
      NodeSeparator("path trace", colW);
      Ui::DrawParams(n, kRender3DParamsRows19, pt);
      });

      const size_t splitAt = pinned ? 5 : 4;
      ImGui::BeginGroup();
      if (ActionButton::Column("Frame scene", colW))
         FrameSceneInView(n);
      for (size_t i = 0; i < sections.size(); i++)
      {
         if (i == splitAt)
         {
            ImGui::EndGroup();
            FormParts::Inline(0.0f, gutter);
            ImGui::BeginGroup();
            ImGui::PushID("showMoreRender3D");
            if (ActionButton::Column(wasInUse && !pinned ? "show more (in use)" : pinned ? "show less" : "show more",
                                     colW) &&
                gn != nullptr)
               gn->showAdvancedParams = !gn->showAdvancedParams;
            ImGui::PopID();
         }
         sections[i]();
      }
      ImGui::EndGroup();
   }


   void DrawBlendParams(BlendNode* n)
   {
      DropdownButton("mode", BlendModes::NamesFor(n->ModeIndex()), n->ModeIndex(),
                     [n](int i) { n->ModeIndex() = i; });
      ModSlider("opacity", &n->Mix(), 0.0f, 1.0f);
      ModCheckbox("linear light", &n->Linear());
   }


   namespace
   {
      using LayerStackParamsRow = params::Row<LayerStackNode>;
   }

   void DrawLayerStackParams(LayerStackNode* n)
   {
      // Layers composite bottom-up: A is the base, D sits on top. Grab a layer's
      // header strip and drag vertically to reorder - the whole layer moves,
      // cable, blend mode and opacity together.
      static LayerStackNode* sDragNode = nullptr;
      static int sDragSlot = -1;
      static float sDragAccum = 0.0f;
      const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

      // Layers whose aspect differs from the canvas (the first connected layer's size): stretch, fit inside, or fill and crop.
      Ui::DrawParam(n, LayerStackParamsRow::Choice<&LayerStackNode::fit>("fit", &LayerStackNode::FitNames).Undo());

      for (int slot = 0; slot < LayerStackNode::kSlots; slot++)
      {
         ImGui::PushID(slot);

         const bool dragging = (sDragNode == n && sDragSlot == slot);
         ImVec2 headerPos = ImGui::GetCursorScreenPos();
         UiInteract::Hit("##grip", ImVec2(kPreviewSize, rowHeight));
         if (ImGui::IsItemActive() && sDragNode == nullptr)
         {
            sDragNode = n;
            sDragSlot = slot;
            sDragAccum = 0.0f;
         }

         ImDrawList* dl = ImGui::GetWindowDrawList();
         if (dragging || ImGui::IsItemHovered())
         {
            dl->AddRectFilled(headerPos,
                              ImVec2(headerPos.x + kPreviewSize, headerPos.y + rowHeight),
                              dragging ? tok::Role(tok::col::surface_lo_pale_fixed, tok::geometry_params_fill_alpha_md) : tok::Role(tok::col::edge_lo, tok::geometry_params_fill_alpha_sm), tok::geometry_params_layer_stack_radius);
         }
         // grip dots, so the header reads as draggable
         for (int d = 0; d < 3; d++)
         {
            dl->AddCircleFilled(ImVec2(headerPos.x + tok::space_1_5, headerPos.y + tok::space_1_25 + d * tok::geometry_params_layer_stack_ratio), tok::geometry_params_layer_stack_r,
                                tok::Role(tok::col::edge_mid));
            dl->AddCircleFilled(ImVec2(headerPos.x + tok::geometry_params_layer_stack_gap, headerPos.y + tok::space_1_25 + d * tok::geometry_params_layer_stack_ratio), tok::geometry_params_layer_stack_r,
                                tok::Role(tok::col::edge_mid));
         }
         // Name the layer after whatever is feeding it - far more useful than
         // "layer C" once a stack has four things in it.
         char title[96];
         const INode* source = n->Input(slot).GetSource();
         if (source != nullptr)
         {
            const char* sourceName = "?";
            for (const GraphNode& other : gNodes)
            {
               if (other.node.get() == source)
                  sourceName = other.typeName.c_str();
            }
            snprintf(title, sizeof(title), "%c  %s", 'A' + slot, sourceName);
         }
         else
         {
            snprintf(title, sizeof(title), "%c  (empty)", 'A' + slot);
         }
         dl->AddText(ImVec2(headerPos.x + tok::geometry_params_text_x, headerPos.y + tok::space_0_5),
                     source ? tok::Role(tok::col::surface_mid) : tok::Role(tok::col::edge_mid), title);

         char modeLabel[32];
         snprintf(modeLabel, sizeof(modeLabel), "mode##%d", slot);
         DropdownButton(modeLabel, BlendModes::NamesFor(n->modes[slot]), n->modes[slot],
                        [n, slot](int i) { n->modes[slot] = i; });
         ModSlider("opacity", &n->opacities[slot], 0.0f, 1.0f);
         ImGui::PopID();
      }

      // Resolve the drag once per frame: accumulate vertical movement and swap a
      // slot at a time so the layer follows the cursor.
      if (sDragNode == n && sDragSlot >= 0)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            sDragNode = nullptr;
            sDragSlot = -1;
         }
         else
         {
            sDragAccum += ImGui::GetIO().MouseDelta.y;
            const float threshold = rowHeight * tok::geometry_params_layer_block_rows; // one full layer block
            while (sDragAccum > threshold && sDragSlot < LayerStackNode::kSlots - 1)
            {
               n->SwapLayers(sDragSlot, sDragSlot + 1);
               sDragSlot++;
               sDragAccum -= threshold;
            }
            while (sDragAccum < -threshold && sDragSlot > 0)
            {
               n->SwapLayers(sDragSlot, sDragSlot - 1);
               sDragSlot--;
               sDragAccum += threshold;
            }
         }
      }

      Ui::DrawParam(n, LayerStackParamsRow::Toggle<&LayerStackNode::linear>("linear light"));
   }


   void DrawFilterParams(FilterNode* n)
   {
      const FilterDef& def = n->Def();

      // "color adjustments" is long enough that its rarely-used grading sections sit behind a "show more" button
      // (GraphNode::showAdvancedParams, saved with the patch). Folded params are still registered, in the same
      // order, so their indices, modulation, expressions and the matrix are unchanged; a folded param that has a
      // cable, expression or palette binding opens the section so its pin is never hidden.
      size_t foldFrom = def.params.size();
      if (def.name == "color adjustments")
         for (size_t i = 0; i < def.params.size(); i++)
            if (def.params[i].sectionLabel == "channel mixer")
               foldFrom = i;
      const bool savedRegisterOnly = gParamRegisterOnly;
      for (size_t i = 0; i < def.params.size(); i++)
      {
         const FilterParamDef& p = def.params[i];
         if (i == foldFrom)
         {
            GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex);
            bool open = gn != nullptr && gn->showAdvancedParams;
            if (!open)
            {
               int pc = gParamCounter, cc = gColorCounter;
               for (size_t k = foldFrom; k < def.params.size() && !open; k++)
               {
                  const FilterParamDef& hp = def.params[k];
                  if (hp.type == FilterParamDef::Type::Float)
                  {
                     open = Modulation::Instance().IsModulated(gCurrentNodeIndex, pc) ||
                            Modulation::Instance().HasExpression(gCurrentNodeIndex, pc);
                     pc++;
                  }
                  else if (hp.type == FilterParamDef::Type::Color)
                     open = PaletteBinding::Instance().IsBound(gCurrentNodeIndex, cc++);
                  else
                  {
                     const int slot = DiscreteParamSlot(gCurrentNodeIndex, hp.label);
                     open = Modulation::Instance().IsModulated(gCurrentNodeIndex, slot);
                  }
               }
            }
            ImGui::PushID("showMoreFilter");
            if (ActionButton::Column(open && !(gn != nullptr && gn->showAdvancedParams) ? "show more (in use)"
                                     : open ? "show less" : "show more", kParamWidth) && gn != nullptr)
               gn->showAdvancedParams = !gn->showAdvancedParams;
            ImGui::PopID();
            gParamRegisterOnly = savedRegisterOnly || !open;
         }
         if (!p.sectionLabel.empty() && !gParamRegisterOnly)
            NodeSeparator(p.sectionLabel.c_str());
         if (def.name == "convolve" && i < 9)
         {
            // The 3x3 kernel reads as the matrix it is: nine number cells, each a full slider (same pin, typing,
            // menus, recording and binding as any other param), named k11..k33 for the modulation matrix.
            if (i == 0)
            {
               const float gap = tok::space_1;
               const float cellW = std::floor((kParamWidth - tok::geometry_params_filter_ratio * gap) / tok::geometry_params_cell_w_u_res);
               for (size_t k = 0; k < 9; k++)
               {
                  const FilterParamDef& kp = def.params[k];
                  ImGui::PushID((int)k);
                  if (k % 3 != 0)
                     FormParts::Inline(0.0f, gap);
                  ModSlider(("##" + kp.label).c_str(), n->ParamPtr(k), kp.minVal, kp.maxVal, "%.1f", cellW, false, 0.0f,
                            nullptr, nullptr, -1, kp.label.c_str());
                  ImGui::PopID();
               }
            }
            continue;
         }
         // Controls the chosen mode never reads sit greyed out (audit-verified against the shaders).
         bool unused = false;
         {
            auto uni = [&](const char* u) -> int
            {
               for (size_t k = 0; k < def.params.size(); k++)
                  if (def.params[k].uniformName == u)
                     return (int)(*n->ParamPtr(k) + 0.5f);
               return 0;
            };
            const std::string& un = p.uniformName;
            if (def.name == "glitch")
               unused = (un == "uSpeed" && uni("uKind") == 1) || (un == "uSeed" && (uni("uKind") == 1 || uni("uKind") == 2 || uni("uKind") == 4));
            else if (def.name == "symmetry")
               unused = (un == "uCenterX" && uni("uAxis") == 1) || (un == "uCenterY" && uni("uAxis") == 0);
            else if (def.name == "normal map")
               unused = (un == "uStrength" && uni("uOutput") == 1) || (un == "uInvertY" && uni("uOutput") != 0);
         }
         Ui::RowsLive on(!unused);
         ImGui::PushID((int)i);
         if (p.type == FilterParamDef::Type::Color)
         {
            ColorSwatch(p.label.c_str(), n->ParamPtr(i), n);
         }
         else if (p.type == FilterParamDef::Type::Enum)
         {
            float* slot = n->ParamPtr(i);
            DropdownButton(p.label.c_str(), p.options, (int)(*slot + 0.5f),
                           [slot](int choice) { *slot = (float)choice; });
         }
         else if (p.type == FilterParamDef::Type::Bool)
         {
            float* slot = n->ParamPtr(i);
            bool checked = *slot != 0.0f;
            if (ModCheckbox(p.label.c_str(), &checked))
               *slot = checked ? 1.0f : 0.0f;
         }
         else if (p.isDegrees)
         {
            ModSlider(p.label.c_str(), n->ParamPtr(i), p.minVal, p.maxVal, "%.1f\xC2\xB0");
         }
         else
         {
            ModSlider(p.label.c_str(), n->ParamPtr(i), p.minVal, p.maxVal,
                      p.format.empty() ? "%.3f" : p.format.c_str(), kParamWidth, false, p.integer ? 1.0f : 0.0f);
         }
         ImGui::PopID();
      }
      gParamRegisterOnly = savedRegisterOnly;
      if (def.params.empty())
         ImGui::TextDisabled("(no parameters)");
   }
}

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
#include "platform/AppPaths.h"
#include "app/ui/design/UiFmt.h"

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
         ImGui::SameLine();
         if (ActionButton::Draw("Restart", ImVec2(kPreviewSize * tok::geometry_params_audio_file_ratio, 0)))
            n->Restart();

         ModCheckbox("follow transport", &n->followTransport);
         ModCheckbox("loop", &n->loop);
         ModCheckbox("audible", &n->monitor);
         ModSlider("volume", &n->volume, 0.0f, 1.0f);
         ModSlider("gain", &n->gain, params::AudioFile::gain_lo, params::AudioFile::gain_hi);
         ModSlider("pitch", &n->pitch, params::AudioFile::pitch_lo, params::AudioFile::pitch_hi);

         const Platform::AudioLevels& lv = n->Levels();
         ImGui::Text("level "); ImGui::SameLine();
         ImGui::ProgressBar(std::min(1.0f, lv.rms * n->gain), ImVec2(kPreviewSize * tok::geometry_params_audio_file_ratio_2, 0), "");
      }
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
      ImGui::Dummy(ImVec2(bodyW, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + bodyW, origin.y + h), tok::Role(tok::col::surface_deep), tok::geometry_params_audio_analyze_radius);
      const float bw = bodyW / (float)Platform::kAudioBands;
      for (int i = 0; i < Platform::kAudioBands; i++)
      {
         const float v = std::min(1.0f, levels.bands[i] * n->gain);
         dl->AddRectFilled(ImVec2(origin.x + i * bw + 1, origin.y + h - v * h),
                           ImVec2(origin.x + (i + 1) * bw - 1, origin.y + h),
                           tok::Role(tok::col::accent_2, tok::geometry_params_audio_analyze_accent_2_alpha_2));
      }
      if (!n->input.IsConnected() && !Platform::AudioIsRunning())
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + bodyW, origin.y + h)}, "idle");
      dl->AddRect(origin, ImVec2(origin.x + bodyW, origin.y + h), tok::Role(tok::col::edge_lo_2), tok::geometry_params_audio_analyze_radius);

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("input", colW);
      ModSlider("gain", &n->gain, params::AudioAnalyze::gain_lo, params::AudioAnalyze::gain_hi, "%.3f", colW);
      ModSlider("onset hold", &n->onsetHold, params::AudioAnalyze::onset_hold_lo, params::AudioAnalyze::onset_hold_hi, "%.3f", colW);

      ImGui::EndGroup();

      // --- Right Column ---
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("envelope", colW);
      ModSlider("attack", &n->attack, params::AudioAnalyze::attack_lo, params::AudioAnalyze::attack_hi, "%.3f", colW);
      ModSlider("release", &n->release, params::AudioAnalyze::release_lo, params::AudioAnalyze::release_hi, "%.3f", colW);

      ImGui::EndGroup();

      NodeSeparator("outputs", bodyW);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   void DrawPathParams(PathNode* n)
   {
      if (n->IsFollowing())
         ImGui::TextDisabled("following %s points", UiFmt::Count((long long)(n->FollowPointCount())).c);
      else
         DropdownButton("shape", PathNode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      float p[3];
      n->CurrentPoint(p);
      ImGui::TextDisabled("t %s   (%s, %s, %s)", UiFmt::F2(n->Progress()).c, UiFmt::F2(p[0]).c, UiFmt::F2(p[1]).c, UiFmt::F2(p[2]).c);

      NodeSeparator("motion");
      ModSlider("speed / beat", &n->speed, params::Path::speed_lo, params::Path::speed_hi);
      ModSlider("phase", &n->phase, 0.0f, 1.0f);
      ModCheckbox("ping-pong", &n->pingPong);

      const bool followingCurve = n->curveSource != nullptr && n->curveSource->GetCurve() != nullptr;
      if (n->geometrySource != nullptr && !followingCurve)
      {
         NodeSeparator("follow");
         DropdownButton("mode", PathNode::FollowModeNames(), n->followMode,
                        [n](int i) { n->followMode = i; });
         if (n->followMode == PathNode::kFollowSlice)
         {
            DropdownButton("axis", kAxisXYZ, n->sliceAxis, [n](int i) { PushUndoCheckpoint(); n->sliceAxis = i; }, kParamWidth, true, true);
            ModSlider("slice at", &n->slicePosition, params::Path::slice_position_lo, params::Path::slice_position_hi);
         }
         ModSliderInt("contour", &n->contourIndex, params::Path::contour_index_lo, params::Path::contour_index_hi);
      }

      NodeSeparator("shape");
      ModSlider("size x", &n->sizeX, params::Path::size_x_lo, params::Path::size_x_hi);
      ModSlider("size y", &n->sizeY, params::Path::size_y_lo, params::Path::size_y_hi);
      ModSlider("size z", &n->sizeZ, params::Path::size_z_lo, params::Path::size_z_hi);
      if (n->shape == PathNode::kHelix || n->shape == PathNode::kSpiral)
         ModSlider("turns", &n->turns, params::Path::turns_lo, params::Path::turns_hi);
      if (n->shape == PathNode::kLissajous)
      {
         ModSliderInt("ratio a", &n->lissajousA, params::Path::lissajous_a_lo, params::Path::lissajous_a_hi);
         ModSliderInt("ratio b", &n->lissajousB, params::Path::lissajous_b_lo, params::Path::lissajous_b_hi);
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
      const ImU32 borderCol = isLight ? tok::Base(tok::col::edge_lo_2, 140) : tok::Base(tok::col::surface_hi, 70);
      const ImU32 textCol = isLight ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::surface_hi);

      dl->AddRect(origin, ImVec2(origin.x + gridW, origin.y + gridH), borderCol, tok::geometry_params_table_grid_radius, 0, tok::geometry_params_table_grid_stroke);
      for (int r = 1; r < rowCount; r++)
      {
         const float y = origin.y + cellH * (float)r;
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + gridW, y), borderCol, tok::geometry_params_table_grid_stroke_2);
      }
      for (int c = 1; c < 3; c++)
      {
         const float x = origin.x + cellW * (float)c;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + gridH), borderCol, tok::geometry_params_table_grid_stroke_2);
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
      ImGui::Dummy(ImVec2(gridW, 0.0f));
   }


   void DrawGeometryTableParams(GeometryTableNode* n)
   {
      if (n->HasSamples())
         ImGui::TextDisabled("following %s points", UiFmt::Count((long long)(n->SampleCount())).c);
      else
         ImGui::TextDisabled("no geometry");

      NodeSeparator("sampling");
      DropdownButton("mode", GeometryTableNode::SampleModeNames(), n->sampleMode,
                     [n](int i) { n->sampleMode = i; });
      ModSliderInt("rows", &n->rows, params::GeometryTable::rows_lo, params::GeometryTable::rows_hi);
      DropdownButton("sort", GeometryTableNode::SortModeNames(), n->sortMode,
                     [n](int i) { n->sortMode = i; });
      ModSlider("offset", &n->offset, 0.0f, 1.0f);

      if (n->sampleMode == GeometryTableNode::kContour)
      {
         DropdownButton("axis", kAxisXYZ, n->sliceAxis, [n](int i) { PushUndoCheckpoint(); n->sliceAxis = i; }, kParamWidth, true, true);
         ModSlider("slice at", &n->slicePosition, params::GeometryTable::slice_position_lo, params::GeometryTable::slice_position_hi);
      }
      if (n->sampleMode == GeometryTableNode::kScatter)
         ModSlider("seed", &n->seed, params::GeometryTable::seed_lo, params::GeometryTable::seed_hi, "%.0f", kParamWidth, false, 1.0f);

      NodeSeparator("range");
      DropdownButton("space", GeometryTableNode::SpaceNames(), n->space,
                     [n](int i) { n->space = i; });
      if (n->space == GeometryTableNode::kSpaceFixed)
         ModSlider("extent", &n->extent, params::GeometryTable::extent_lo, params::GeometryTable::extent_hi);
      ModSlider("smooth", &n->smooth, 0.0f, 1.0f);

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


   void DrawOceanParams(OceanNode* n)
   {
      NodeSeparator("surface");
      ModSliderInt("resolution", &n->resolution, params::Ocean::resolution_lo, params::Ocean::resolution_hi);
      ModSlider("size", &n->size, params::Ocean::size_lo, params::Ocean::size_hi);
      ModSlider("horizon", &n->horizon, params::Ocean::horizon_lo, params::Ocean::horizon_hi);

      NodeSeparator("waves");
      ModSlider("amplitude", &n->amplitude, 0.0f, 1.0f);
      ModSlider("wavelength", &n->wavelength, params::Ocean::wavelength_lo, params::Ocean::wavelength_hi);
      ModSlider("steepness", &n->steepness, params::Ocean::steepness_lo, params::Ocean::steepness_hi);
      ModSlider("choppiness", &n->choppiness, params::Ocean::choppiness_lo, params::Ocean::choppiness_hi);
      ModSlider("direction", &n->direction, params::Ocean::direction_lo, params::Ocean::direction_hi, "%.1f\xC2\xB0");
      ModSliderInt("octaves", &n->octaves, params::Ocean::octaves_lo, params::Ocean::octaves_hi);
      ModSlider("speed", &n->speed, params::Ocean::speed_lo, params::Ocean::speed_hi);
   }


   void DrawCurveParams(CurveNode* n)
   {
      ImGui::TextDisabled("%s points, %s triangles", UiFmt::Count((long long)(n->PointCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      DropdownButton("type", CurveNode::KindNames(), n->kind, [n](int i) { n->kind = i; });
      DropdownButton("preset", CurveNode::PresetNames(), n->preset, [n](int i) { n->preset = i; });

      NodeSeparator("shape");
      ModSliderInt("points", &n->pointCount, params::Curve::point_count_lo, CurveNode::kMaxPoints);
      ModSliderInt("smoothness", &n->segments, params::Curve::segments_lo, params::Curve::segments_hi);
      ModCheckbox("closed", &n->closed);
      ModSlider("spread", &n->spread, params::Curve::spread_lo, params::Curve::spread_hi);
      ModSlider("height", &n->height, params::Curve::height_lo, params::Curve::height_hi);
      ModSlider("twist", &n->twist, params::Curve::twist_lo, params::Curve::twist_hi, "%.1f\xC2\xB0");
      if (n->preset == 3)
         ModSlider("seed", &n->seed, params::Curve::seed_lo, params::Curve::seed_hi, "%.0f", kParamWidth, false, 1.0f);

      NodeSeparator("tube");
      ModSlider("radius", &n->radius, params::Curve::radius_lo, params::Curve::radius_hi);
      ModSliderInt("sides", &n->sides, params::Curve::sides_lo, params::Curve::sides_hi);
      ModSlider("taper", &n->taper, 0.0f, 1.0f);
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
      ModSliderInt("generations", &n->iterations, 0, LSystemNode::kMaxIterations);
      ModSlider("angle", &n->angle, params::LSystem::angle_lo, params::LSystem::angle_hi, "%.1f\xC2\xB0");
      ModSlider("length", &n->length, params::LSystem::length_lo, params::LSystem::length_hi);
      ModSlider("length / branch", &n->lengthScale, params::LSystem::length_scale_lo, params::LSystem::length_scale_hi);

      NodeSeparator("tube");
      ModSlider("radius", &n->radius, params::LSystem::radius_lo, params::LSystem::radius_hi);
      ModSlider("radius / branch", &n->radiusScale, params::LSystem::radius_scale_lo, params::LSystem::radius_scale_hi);
      ModSliderInt("sides", &n->sides, params::LSystem::sides_lo, params::LSystem::sides_hi);
      ModSlider("taper", &n->taper, 0.0f, 1.0f);

      if (!n->CookWarning().empty())
      {
         PushTextWrapWidth(kPreviewSize);
         ImGui::TextUnformatted(n->CookWarning().c_str());
         PopTextWrapWidth();
      }
   }


   void DrawMetaBallParams(MetaBallNode* n)
   {
      ImGui::TextDisabled("%s balls, %s triangles", UiFmt::Count((long long)(n->BallCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      NodeSeparator("field");
      if (n->cloudSource == nullptr || n->cloudSource->GetPointCloud() == nullptr)
      {
         ModSliderInt("balls", &n->ballCount, 1, MetaBallNode::kMaxBalls);
         ModSlider("spread", &n->spread, params::MetaBall::spread_lo, params::MetaBall::spread_hi);
         ModSlider("orbit / beat", &n->spin, -1.0f, 1.0f);
      }
      else
      {
         ModSliderInt("max from cloud", &n->maxFromCloud, params::MetaBall::max_from_cloud_lo, params::MetaBall::max_from_cloud_hi);
      }
      ModSlider("radius", &n->radius, params::MetaBall::radius_lo, params::MetaBall::radius_hi);
      ModSlider("threshold", &n->threshold, params::MetaBall::threshold_lo, params::MetaBall::threshold_hi);
      ModSlider("bounds", &n->bounds, params::MetaBall::bounds_lo, params::MetaBall::bounds_hi);
      ModSliderInt("resolution", &n->resolution, params::MetaBall::resolution_lo, params::MetaBall::resolution_hi);
   }


   void DrawJoinGeometryParams(JoinGeometryNode* n)
   {
      ImGui::TextDisabled("%s inputs, %s triangles", UiFmt::Count((long long)(n->ConnectedCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      DropdownButton("mode", JoinGeometryNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      // A merged mesh is one draw call, so it can only wear one material -
      // picked from an input rather than authored here, since editing colour
      // and shading now lives on the dedicated Material node.
      ModSliderInt("material from input", &n->materialFrom, 0, JoinGeometryNode::kSlots - 1);
   }


   void DrawGroup3DParams(Group3DNode* n)
   {
      ModSlider("move x", &n->offsetX, params::Group3D::offset_x_lo, params::Group3D::offset_x_hi);
      ModSlider("move y", &n->offsetY, params::Group3D::offset_y_lo, params::Group3D::offset_y_hi);
      ModSlider("move z", &n->offsetZ, params::Group3D::offset_z_lo, params::Group3D::offset_z_hi);
      ModSlider("rotate x", &n->rotX, params::Group3D::rot_x_lo, params::Group3D::rot_x_hi, "%.1f\xC2\xB0");
      ModSlider("rotate y", &n->rotY, params::Group3D::rot_y_lo, params::Group3D::rot_y_hi, "%.1f\xC2\xB0");
      ModSlider("rotate z", &n->rotZ, params::Group3D::rot_z_lo, params::Group3D::rot_z_hi, "%.1f\xC2\xB0");
      ModSlider("scale", &n->scale, params::Group3D::scale_lo, params::Group3D::scale_hi);
      ImGui::BeginDisabled(n->rotX == 0.0f && n->rotY == 0.0f && n->rotZ == 0.0f && n->scale == 1.0f && !gParamRegisterOnly);
      ModSlider("pivot x", &n->pivotX, params::Group3D::pivot_x_lo, params::Group3D::pivot_x_hi);
      ModSlider("pivot y", &n->pivotY, params::Group3D::pivot_y_lo, params::Group3D::pivot_y_hi);
      ModSlider("pivot z", &n->pivotZ, params::Group3D::pivot_z_lo, params::Group3D::pivot_z_hi);
      ImGui::EndDisabled();
   }


   void DrawSwitcher3DParams(Switcher3DNode* n)
   {
      ImGui::BeginDisabled(n->manual && !gParamRegisterOnly);   // manual pins the slot; the clock is unused
      DropdownButton("unit", Switcher3DNode::UnitNames(), n->unit, [n](int i) { n->unit = i; });
      ModSlider("every", &n->interval, params::Switcher3D::interval_lo, params::Switcher3D::interval_hi);
      ImGui::EndDisabled();
      ModCheckbox("manual", &n->manual);
      if (n->manual)
      {
         ModSliderInt("slot", &n->manualSlot, 0, Switcher3DNode::kSlots - 1);
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }


   void DrawWrapParams(WrapNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      DropdownButton("mode", WrapNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      // Axis, radius and fit steer the parametric bend; nearest-surface has no
      // parameterisation for them to act on.
      if (n->mode != MeshOps::kWrapNearest)
      {
         DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
         // With a target the radius always follows the target's size, so the
         // only control is a multiplier - and the resolved value is shown so
         // that link stays visible. Without one there is nothing to follow,
         // so the radius is set outright. Never both.
         if (n->targetInput != nullptr)
         {
            ImGui::TextDisabled("radius %s (from target)", UiFmt::F3(n->ResolvedRadius()).c);
            ModSlider("radius scale", &n->radiusScale, params::Wrap::radius_scale_lo, params::Wrap::radius_scale_hi);
         }
         else
         {
            ModSlider("radius", &n->radiusOverride, params::Wrap::radius_override_lo, params::Wrap::radius_override_hi);
         }
         ModCheckbox("fit around", &n->fitAround);
      }
      ModSlider("offset", &n->offset, params::Wrap::offset_lo, params::Wrap::offset_hi);
      ModSlider("blend", &n->blend, 0.0f, 1.0f);
      ModCheckbox("flat shade", &n->flatShade);
      ModCheckbox("flip normals", &n->flipNormals);
   }


   void DrawClothParams(ClothNode* n)
   {
      ImGui::TextDisabled("%s triangles, %s links", UiFmt::Count((long long)(n->TriangleCount())).c, UiFmt::Count((long long)(n->ConstraintCount())).c);
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("solver");
      DropdownButton("pinned", ClothNode::PinModeNames(), n->pinMode, [n](int i) { n->pinMode = i; });
      ModSlider("stiffness", &n->stiffness, 0.0f, 1.0f);
      ModSliderInt("iterations", &n->iterations, params::Cloth::iterations_lo, params::Cloth::iterations_hi);
      ModSlider("damping", &n->damping, params::Cloth::damping_lo, params::Cloth::damping_hi);
      ModSlider("mass", &n->mass, params::Cloth::mass_lo, params::Cloth::mass_hi);
      ModSlider("hold shape", &n->shapeRetention, 0.0f, 1.0f);
      ModSlider("bending", &n->bending, 0.0f, 1.0f);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, params::Cloth::gravity_x_lo, params::Cloth::gravity_x_hi);
      ModSlider("gravity y", &n->gravityY, params::Cloth::gravity_y_lo, params::Cloth::gravity_y_hi);
      ModSlider("gravity z", &n->gravityZ, params::Cloth::gravity_z_lo, params::Cloth::gravity_z_hi);
      ModSlider("wind x", &n->windX, params::Cloth::wind_x_lo, params::Cloth::wind_x_hi);
      ModSlider("wind y", &n->windY, params::Cloth::wind_y_lo, params::Cloth::wind_y_hi);
      ModSlider("wind z", &n->windZ, params::Cloth::wind_z_lo, params::Cloth::wind_z_hi);
      ModSlider("turbulence", &n->windTurbulence, params::Cloth::wind_turbulence_lo, params::Cloth::wind_turbulence_hi);

      NodeSeparator("ground");
      ModCheckbox("collide with ground", &n->groundEnabled);
      if (n->groundEnabled)
      {
         ModSlider("height", &n->groundHeight, params::Cloth::ground_height_lo, params::Cloth::ground_height_hi);
         ModSlider("bounce", &n->bounce, 0.0f, 1.0f);
         ModSlider("friction", &n->friction, 0.0f, 1.0f);
      }

      NodeSeparator("collider");
      ImGui::BeginDisabled(n->collider == nullptr);
      ModSlider("distance", &n->colliderDistance, params::Cloth::collider_distance_lo, params::Cloth::collider_distance_hi);
      ModSlider("friction##collider", &n->colliderFriction, params::Cloth::collider_friction_lo, params::Cloth::collider_friction_hi);
      ImGui::EndDisabled();
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


   void DrawRigidBodyParams(RigidBodyNode* n)
   {
      ImGui::TextDisabled("%s bodies, %s triangles", UiFmt::Count((long long)(n->BodyCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("bodies");
      DropdownButton("collider", RigidBodyNode::ColliderNames(), n->collider, [n](int i) { n->collider = i; });
      ModSlider("mass", &n->mass, params::RigidBody::mass_lo, params::RigidBody::mass_hi);
      ModSlider("friction", &n->friction, 0.0f, 1.0f);
      ModSlider("bounce", &n->bounce, 0.0f, 1.0f);
      ModSlider("linear damping", &n->linearDamping, params::RigidBody::linear_damping_lo, params::RigidBody::linear_damping_hi);
      ModSlider("angular damping", &n->angularDamping, params::RigidBody::angular_damping_lo, params::RigidBody::angular_damping_hi);
      ModSliderInt("iterations", &n->iterations, params::RigidBody::iterations_lo, params::RigidBody::iterations_hi);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, params::RigidBody::gravity_x_lo, params::RigidBody::gravity_x_hi);
      ModSlider("gravity y", &n->gravityY, params::RigidBody::gravity_y_lo, params::RigidBody::gravity_y_hi);
      ModSlider("gravity z", &n->gravityZ, params::RigidBody::gravity_z_lo, params::RigidBody::gravity_z_hi);

      bool rbSaved = false;
      const bool rbOpen = BeginAdvancedFold("showMoreRigid", 10, 0, rbSaved);   // start + kick sliders
      if (rbOpen)
         NodeSeparator("start");
      ModSlider("velocity x", &n->velX, params::RigidBody::vel_x_lo, params::RigidBody::vel_x_hi);
      ModSlider("velocity y", &n->velY, params::RigidBody::vel_y_lo, params::RigidBody::vel_y_hi);
      ModSlider("velocity z", &n->velZ, params::RigidBody::vel_z_lo, params::RigidBody::vel_z_hi);
      ModSlider("spin x", &n->spinX, params::RigidBody::spin_x_lo, params::RigidBody::spin_x_hi);
      ModSlider("spin y", &n->spinY, params::RigidBody::spin_y_lo, params::RigidBody::spin_y_hi);
      ModSlider("spin z", &n->spinZ, params::RigidBody::spin_z_lo, params::RigidBody::spin_z_hi);

      if (rbOpen)
         NodeSeparator("kick");
      ModSlider("kick time", &n->kickTime, params::RigidBody::kick_time_lo, params::RigidBody::kick_time_hi);
      ModSlider("kick x", &n->kickX, params::RigidBody::kick_x_lo, params::RigidBody::kick_x_hi);
      ModSlider("kick y", &n->kickY, params::RigidBody::kick_y_lo, params::RigidBody::kick_y_hi);
      ModSlider("kick z", &n->kickZ, params::RigidBody::kick_z_lo, params::RigidBody::kick_z_hi);
      gParamRegisterOnly = rbSaved;

      NodeSeparator("world");
      ModCheckbox("ground", &n->groundEnabled);
      if (n->groundEnabled)
         ModSlider("ground height", &n->groundHeight, params::RigidBody::ground_height_lo, params::RigidBody::ground_height_hi);
      ModCheckbox("walls", &n->wallsEnabled);
      if (n->wallsEnabled)
         ModSlider("wall distance", &n->wallSize, params::RigidBody::wall_size_lo, params::RigidBody::wall_size_hi);
   }


   void DrawParticleSystemParams(ParticleSystemNode* n)
   {
      ImGui::TextDisabled(n->OnGpu() ? "%zu alive (GPU)" : "%zu alive", n->AliveCount());
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("emitter");
      DropdownButton("shape", ParticleSystemNode::EmitShapeNames(), n->emitShape,
                     [n](int i) { n->emitShape = i; });
      ModSliderInt("max particles", &n->maxParticles, params::ParticleSystem::max_particles_lo, ParticleSystemNode::kGpuMaxParticles);
      ModSlider("rate / sec", &n->emitRate, params::ParticleSystem::emit_rate_lo, params::ParticleSystem::emit_rate_hi);
      ModSlider("radius", &n->emitRadius, params::ParticleSystem::emit_radius_lo, params::ParticleSystem::emit_radius_hi);
      ModSlider("lifetime", &n->lifetime, params::ParticleSystem::lifetime_lo, params::ParticleSystem::lifetime_hi);
      ModSlider("life random", &n->lifetimeRandom, 0.0f, 1.0f);

      NodeSeparator("launch");
      ModSlider("speed", &n->initialSpeed, params::ParticleSystem::initial_speed_lo, params::ParticleSystem::initial_speed_hi);
      ModSlider("speed random", &n->speedRandom, 0.0f, 1.0f);
      ModSlider("spread", &n->spread, 0.0f, 1.0f);
      ModSlider("dir x", &n->dirX, -1.0f, 1.0f);
      ModSlider("dir y", &n->dirY, -1.0f, 1.0f);
      ModSlider("dir z", &n->dirZ, -1.0f, 1.0f);

      bool psSaved = false;
      const bool psOpen = BeginAdvancedFold("showMoreParticles", 14, 2, psSaved);   // forces + over life + emit/time
      if (psOpen)
         NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, params::ParticleSystem::gravity_x_lo, params::ParticleSystem::gravity_x_hi);
      ModSlider("gravity y", &n->gravityY, params::ParticleSystem::gravity_y_lo, params::ParticleSystem::gravity_y_hi);
      ModSlider("gravity z", &n->gravityZ, params::ParticleSystem::gravity_z_lo, params::ParticleSystem::gravity_z_hi);
      ModSlider("drag", &n->drag, params::ParticleSystem::drag_lo, params::ParticleSystem::drag_hi);
      ModSlider("turbulence", &n->turbulence, params::ParticleSystem::turbulence_lo, params::ParticleSystem::turbulence_hi);
      ModSlider("turb scale", &n->turbulenceScale, params::ParticleSystem::turbulence_scale_lo, params::ParticleSystem::turbulence_scale_hi);

      if (psOpen)
         NodeSeparator("over life");
      ModSlider("start size", &n->startSize, params::ParticleSystem::start_size_lo, params::ParticleSystem::start_size_hi);
      ModSlider("end size", &n->endSize, params::ParticleSystem::end_size_lo, params::ParticleSystem::end_size_hi);
      ColorSwatch("start colour", n->startColor, n);
      ColorSwatch("end colour", n->endColor, n);
      ModSlider("seed", &n->seed, params::ParticleSystem::seed_lo, params::ParticleSystem::seed_hi, "%.0f", kParamWidth, false, 1.0f);

      // Appended after seed so every older row keeps its modulation slot.
      if (psOpen)
         NodeSeparator("emit & time");
      ModCheckbox("emit from input", &n->emitFromInput);
      ModSlider("speed along normal", &n->speedAlongNormal, params::ParticleSystem::speed_along_normal_lo, params::ParticleSystem::speed_along_normal_hi);
      ModCheckbox("copy source colour", &n->copySourceColor);
      ModSlider("mass", &n->mass, params::ParticleSystem::mass_lo, params::ParticleSystem::mass_hi);
      ModSlider("time scale", &n->timeScale, params::ParticleSystem::time_scale_lo, params::ParticleSystem::time_scale_hi);
      ModSlider("size curve", &n->sizeCurve, params::ParticleSystem::size_curve_lo, params::ParticleSystem::size_curve_hi);
      ModSlider("colour curve", &n->colorCurve, params::ParticleSystem::color_curve_lo, params::ParticleSystem::color_curve_hi);
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
      ImGui::BeginDisabled(!n->MapInput(kMapNormal).IsConnected());
      ModCheckbox("bump (height map)##matNormalBump", &n->normalBump);
      ModSlider("normal strength", &n->normalStrength, params::Material::normal_strength_lo, params::Material::normal_strength_hi, "%.3f", colW);
      ModSlider("detail scale", &n->detailScale, params::Material::detail_scale_lo, params::Material::detail_scale_hi, "%.2f", colW);
      ImGui::BeginDisabled(n->detailScale <= 0.0f);
      ModSlider("detail strength", &n->detailStrength, params::Material::detail_strength_lo, params::Material::detail_strength_hi, "%.3f", colW);
      ImGui::EndDisabled();
      ImGui::EndDisabled();

      NodeSeparator("surface", colW);
      ImGui::BeginDisabled(ptOnly);
      DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; }, colW);
      ImGui::EndDisabled();
      DropdownButton("wrap", MaterialNode::WrapModeNames(), n->wrapMode, [n](int i) { n->wrapMode = i; }, colW);
      ColorSwatch("colour", n->color, n);
      ImGui::BeginDisabled(pbrUnused);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f, "%.3f", colW);
      ModSlider("roughness", &n->roughness, params::Material::roughness_lo, params::Material::roughness_hi, "%.3f", colW);
      ModSlider("specular", &n->specular, 0.0f, 1.0f, "%.3f", colW);
      ModSlider("ior", &n->ior, params::Material::ior_lo, params::Material::ior_hi, "%.3f", colW);
      ImGui::EndDisabled();
      ModSlider("opacity", &n->opacity, 0.0f, 1.0f, "%.3f", colW);
      ModCheckbox("texture alpha", &n->textureAlpha);
      ImGui::BeginDisabled(!(n->opacity < 1.0f || n->alphaCutoff > 0.0f || n->textureAlpha ||
                             n->MapInput(kMapOpacity).IsConnected()));
      ModSlider("alpha cutoff", &n->alphaCutoff, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();

      ImGui::BeginDisabled(pbrUnused);
      NodeSeparator("emission", colW);
      ColorSwatch("emission", n->emissionColor, n);
      ModSlider("emission", &n->emission, params::Material::emission_lo, params::Material::emission_hi, "%.3f", colW);
      ImGui::EndDisabled();

      ImGui::BeginDisabled(pbrUnused);
      NodeSeparator("coat & sheen", colW);
      ModSlider("clearcoat", &n->clearcoat, 0.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(!(n->clearcoat > 0.0f || n->MapInput(kMapClearcoat).IsConnected()));
      ModSlider("coat roughness", &n->clearcoatRoughness, params::Material::clearcoat_roughness_lo, params::Material::clearcoat_roughness_hi, "%.3f", colW);
      ImGui::EndDisabled();
      ModSlider("sheen", &n->sheen, 0.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(!(n->sheen > 0.0f || n->MapInput(kMapSheen).IsConnected()));
      ColorSwatch("sheen tint", n->sheenColor, n);
      ModSlider("sheen roughness", &n->sheenRoughness, params::Material::sheen_roughness_lo, params::Material::sheen_roughness_hi, "%.3f", colW);
      ModSlider("sheen direction", &n->sheenDirection, -1.0f, 1.0f, "%.2f", colW);
      ImGui::EndDisabled();
      ImGui::EndDisabled();

      ImGui::EndGroup();

      // --- Right Column --- (the split sits here so both columns hold about the same rows: 22 left, 21 right)
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      ImGui::BeginDisabled(pbrUnused);
      NodeSeparator("optics", colW);
      ImGui::BeginDisabled(ptOnly);
      ModSlider("anisotropy", &n->anisotropy, -1.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(n->anisotropy == 0.0f);
      ModSlider("aniso rotation", &n->anisotropyRotation, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();
      ModSlider("iridescence", &n->iridescence, 0.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(n->iridescence <= 0.0f);
      ModSlider("irid IOR", &n->iridescenceIor, params::Material::iridescence_ior_lo, params::Material::iridescence_ior_hi, "%.3f", colW);
      ModSlider("irid thickness", &n->iridescenceThickness, params::Material::iridescence_thickness_lo, params::Material::iridescence_thickness_hi, "%.0f nm", colW);
      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ModSlider("transmission", &n->transmission, 0.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(n->transmission <= 0.0f);
      ModSlider("trans rough", &n->transmissionRoughness, 0.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(ptOnly);
      ModSlider("dispersion", &n->dispersion, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ImGui::BeginDisabled(ptOnly);
      ModSlider("subsurface", &n->subsurface, 0.0f, 1.0f, "%.3f", colW);
      ImGui::BeginDisabled(n->subsurface <= 0.0f);
      ColorSwatch("sss colour", n->subsurfaceColor, n);
      ModSlider("sss radius", &n->subsurfaceRadius, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();
      ModSlider("translucency", &n->translucency, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();
      ImGui::EndDisabled();

      // Hair (Chiang fibre BSDF): last, so the rows above keep their keys. Roughness and ior are the fibre's.
      static const std::vector<std::string> kModels = { "Principled", "Hair" }, kHairColour = { "Melanin", "Colour" };
      NodeSeparator("hair", colW);
      DropdownButton("model", kModels, n->model, [n](int i) { n->model = i; }, colW);
      ImGui::BeginDisabled(n->model != 1 && !gParamRegisterOnly);
      DropdownButton("hair colour", kHairColour, n->hairColour, [n](int i) { n->hairColour = i; }, colW);
      ImGui::BeginDisabled(n->hairColour != 0 && !gParamRegisterOnly);
      ModSlider("melanin", &n->melanin, 0.0f, 1.0f, "%.3f", colW);
      ModSlider("redness", &n->melaninRedness, 0.0f, 1.0f, "%.3f", colW);
      ColorSwatch("hair tint", n->hairTint, n);
      ImGui::EndDisabled();
      ModSlider("radial rough", &n->radialRoughness, params::Material::radial_roughness_lo, params::Material::radial_roughness_hi, "%.3f", colW);
      ModSlider("cuticle shift", &n->hairShift, params::Material::hair_shift_lo, params::Material::hair_shift_hi, "%.1f deg", colW);
      ImGui::EndDisabled();
      if (n->model == 1 && !gParamRegisterOnly)
      {
         // The fibre axis comes from Fur's Tangent normals; any other mesh has its surface normal there.
         auto* fur = dynamic_cast<GeometryOpNode*>(n->input);
         if (!(fur && fur->op == GeometryOpNode::kFur && fur->furNormals == 1))
            ImGui::TextDisabled("needs Fur, Tangent normals");
      }

      ImGui::EndGroup();
   }


   void DrawMappingParams(MappingNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      DropdownButton("space", MappingNode::SpaceNames(), n->space, [n](int i) { n->space = i; });
      if (n->space != kMapSpaceUv)
         ModSlider("blend softness", &n->triplanarBlend, 0.0f, 1.0f);
      ModSlider("breakup", &n->breakup, 0.0f, 1.0f);

      NodeSeparator("translate");
      ModSlider("x", &n->translateX, params::Mapping::translate_x_lo, params::Mapping::translate_x_hi);
      ModSlider("y", &n->translateY, params::Mapping::translate_y_lo, params::Mapping::translate_y_hi);
      if (n->space != kMapSpaceUv)
         ModSlider("z", &n->translateZ, params::Mapping::translate_z_lo, params::Mapping::translate_z_hi);

      NodeSeparator("rotate");
      if (n->space == kMapSpaceUv)
      {
         ModSlider("z", &n->rotateZ, params::Mapping::rotate_z_lo, params::Mapping::rotate_z_hi, "%.1f\xC2\xB0");
      }
      else
      {
         ModSlider("x", &n->rotateX, params::Mapping::rotate_x_lo, params::Mapping::rotate_x_hi, "%.1f\xC2\xB0");
         ModSlider("y", &n->rotateY, params::Mapping::rotate_y_lo, params::Mapping::rotate_y_hi, "%.1f\xC2\xB0");
         ModSlider("z", &n->rotateZ, params::Mapping::rotate_z_lo, params::Mapping::rotate_z_hi, "%.1f\xC2\xB0");
      }

      NodeSeparator("scale");
      ModSlider("x", &n->scaleX, params::Mapping::scale_x_lo, params::Mapping::scale_x_hi);
      ModSlider("y", &n->scaleY, params::Mapping::scale_y_lo, params::Mapping::scale_y_hi);
      if (n->space != kMapSpaceUv)
         ModSlider("z", &n->scaleZ, params::Mapping::scale_z_lo, params::Mapping::scale_z_hi);
   }


   void DrawShadeCoordParams(ShadeCoordNode* n)
   {
      DropdownButton("output", ShadeCoordNode::OutputNames(), n->output, [n](int i) { PushUndoCheckpoint(); n->output = i; });
   }


   void DrawShadeMappingParams(ShadeMappingNode* n)
   {
      DropdownButton("type", ShadeMappingNode::KindNames(), n->kind, [n](int i) { PushUndoCheckpoint(); n->kind = i; });

      NodeSeparator("translate");
      ModSlider("x", &n->translate[0], params::ShadeMapping::translate_0_lo, params::ShadeMapping::translate_0_hi);
      ModSlider("y", &n->translate[1], params::ShadeMapping::translate_1_lo, params::ShadeMapping::translate_1_hi);
      ModSlider("z", &n->translate[2], params::ShadeMapping::translate_2_lo, params::ShadeMapping::translate_2_hi);

      NodeSeparator("rotate");
      ModSlider("x", &n->rotate[0], params::ShadeMapping::rotate_0_lo, params::ShadeMapping::rotate_0_hi, "%.1f\xC2\xB0");
      ModSlider("y", &n->rotate[1], params::ShadeMapping::rotate_1_lo, params::ShadeMapping::rotate_1_hi, "%.1f\xC2\xB0");
      ModSlider("z", &n->rotate[2], params::ShadeMapping::rotate_2_lo, params::ShadeMapping::rotate_2_hi, "%.1f\xC2\xB0");

      NodeSeparator("scale");
      ModSlider("x", &n->scale[0], params::ShadeMapping::scale_0_lo, params::ShadeMapping::scale_0_hi);
      ModSlider("y", &n->scale[1], params::ShadeMapping::scale_1_lo, params::ShadeMapping::scale_1_hi);
      ModSlider("z", &n->scale[2], params::ShadeMapping::scale_2_lo, params::ShadeMapping::scale_2_hi);
   }


   void DrawShadeCheckerParams(ShadeCheckerNode* n)
   {
      ColorSwatch("colour 1", n->color1, n);
      ColorSwatch("colour 2", n->color2, n);
      ModSlider("scale", &n->scale, params::ShadeChecker::scale_lo, params::ShadeChecker::scale_hi);
   }


   void DrawShadeNoiseParams(ShadeNoiseNode* n)
   {
      DropdownButton("output", ShadeNoiseNode::OutputNames(), n->output, [n](int i) { PushUndoCheckpoint(); n->output = i; });
      ModSlider("scale", &n->scale, params::ShadeNoise::scale_lo, params::ShadeNoise::scale_hi);
      ModSlider("detail", &n->detail, params::ShadeNoise::detail_lo, params::ShadeNoise::detail_hi, "%.0f", kParamWidth, false, 1.0f);
      ModSlider("roughness", &n->roughness, 0.0f, 1.0f);
      ModSlider("distortion", &n->distortion, params::ShadeNoise::distortion_lo, params::ShadeNoise::distortion_hi);
   }


   void DrawShadeVoronoiParams(ShadeVoronoiNode* n)
   {
      DropdownButton("output", ShadeVoronoiNode::OutputNames(), n->output, [n](int i) { PushUndoCheckpoint(); n->output = i; });
      ModSlider("scale", &n->scale, params::ShadeVoronoi::scale_lo, params::ShadeVoronoi::scale_hi);
   }


   void DrawShadeGradientParams(ShadeGradientNode* n)
   {
      DropdownButton("type", ShadeGradientNode::KindNames(), n->kind, [n](int i) { PushUndoCheckpoint(); n->kind = i; });
      ModSlider("scale", &n->scale, params::ShadeGradient::scale_lo, params::ShadeGradient::scale_hi);
   }


   void DrawShadeBrickParams(ShadeBrickNode* n)
   {
      ColorSwatch("colour 1", n->color1, n);
      ColorSwatch("colour 2", n->color2, n);
      ColorSwatch("mortar", n->mortarColor, n);
      ModSlider("scale", &n->scale, params::ShadeBrick::scale_lo, params::ShadeBrick::scale_hi);
      ModSlider("width", &n->width, params::ShadeBrick::width_lo, params::ShadeBrick::width_hi);
      ModSlider("height", &n->height, params::ShadeBrick::height_lo, params::ShadeBrick::height_hi);
      ModSlider("offset", &n->offset, 0.0f, 1.0f);
      ModSlider("mortar size", &n->mortar, params::ShadeBrick::mortar_lo, params::ShadeBrick::mortar_hi, "%.3f");
      ModSlider("tint", &n->tint, 0.0f, 1.0f);
   }


   void DrawShadeImageParams(ShadeImageNode* n)
   {
      DropdownButton("projection", ShadeImageNode::ProjectionNames(), n->projection, [n](int i) { PushUndoCheckpoint(); n->projection = i; });
      DropdownButton("space", ShadeImageNode::SpaceNames(), n->space, [n](int i) { PushUndoCheckpoint(); n->space = i; });
      if (n->projection == ShadeImageNode::kBox)
         ModSlider("blend", &n->blend, 0.0f, 1.0f);
   }


   void DrawShadeMathParams(ShadeMathNode* n)
   {
      DropdownButton("op", ShadeMathNode::OpNames(), n->op, [n](int i) { PushUndoCheckpoint(); n->op = i; });
      const int k = ShadeMathNode::Operands(n->op);
      static const char* kNames[3] = { "a", "b", "c" };
      for (int i = 0; i < k; i++)
         ModSlider(kNames[i], &n->value[i], params::ShadeMath::value_lo, params::ShadeMath::value_hi);
      ModCheckbox("clamp", &n->clampResult);
   }


   void DrawShadeVectorMathParams(ShadeVectorMathNode* n)
   {
      DropdownButton("op", ShadeVectorMathNode::OpNames(), n->op, [n](int i) { PushUndoCheckpoint(); n->op = i; });
      const int k = ShadeVectorMathNode::Operands(n->op);
      float* vecs[3] = { n->a, n->b, n->c };
      static const char* kLabels[3][3] = { { "a x", "a y", "a z" }, { "b x", "b y", "b z" }, { "c x", "c y", "c z" } };
      static const char* kParams[3][3] = { { "aX", "aY", "aZ" }, { "bX", "bY", "bZ" }, { "cX", "cY", "cZ" } };
      for (int v = 0; v < k; v++)
         for (int c = 0; c < 3; c++)
            ModSlider(kLabels[v][c], &vecs[v][c], params::geometry_params::vecs_lo, params::geometry_params::vecs_hi, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kParams[v][c]);
      if (ShadeVectorMathNode::UsesScale(n->op))
         ModSlider("scale", &n->scale, params::ShadeVectorMath::scale_lo, params::ShadeVectorMath::scale_hi);
   }


   void DrawShadeMixParams(ShadeMixNode* n)
   {
      DropdownButton("type", ShadeMixNode::DataTypeNames(), n->dataType, [n](int i) { PushUndoCheckpoint(); n->dataType = i; });
      if (n->dataType == ShadeMixNode::kColour)
         DropdownButton("mode", ShadeMixNode::ModeNames(), n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; });
      ModSlider("factor", &n->factor, 0.0f, 1.0f);
      if (n->dataType == ShadeMixNode::kFloat)
      {
         ModSlider("a", &n->a[0], params::ShadeMix::a_0_lo, params::ShadeMix::a_0_hi);
         ModSlider("b", &n->b[0], params::ShadeMix::b_0_lo, params::ShadeMix::b_0_hi);
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
         ColorSwatch("a", n->a, n);
         ColorSwatch("b", n->b, n);
      }
      ModCheckbox("clamp factor", &n->clampFactor);
      ModCheckbox("clamp result", &n->clampResult);
   }


   void DrawShadeRampParams(ShadeRampNode* n)
   {
      DropdownButton("interpolation", ShadeRampNode::InterpNames(), n->interp, [n](int i) { PushUndoCheckpoint(); n->interp = i; });
      ModSlider("factor", &n->factor, 0.0f, 1.0f);
      ModSliderInt("stops", &n->count, params::ShadeRamp::count_lo, ShadeRampNode::kMaxStops);
      static const char* kPos[ShadeRampNode::kMaxStops] = { "pos0", "pos1", "pos2", "pos3", "pos4", "pos5", "pos6", "pos7" };
      for (int i = 0; i < n->count && i < ShadeRampNode::kMaxStops; i++)
      {
         ImGui::PushID(i);
         ModSlider("position", &n->pos[i], 0.0f, 1.0f, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kPos[i]);
         ColorSwatch("colour", n->color[i], n);
         ImGui::PopID();
      }
   }


   void DrawShadeMapRangeParams(ShadeMapRangeNode* n)
   {
      DropdownButton("interpolation", ShadeMapRangeNode::InterpNames(), n->interp, [n](int i) { PushUndoCheckpoint(); n->interp = i; });
      ModSlider("value", &n->value, params::ShadeMapRange::value_lo, params::ShadeMapRange::value_hi);
      ModSlider("from min", &n->fromMin, params::ShadeMapRange::from_min_lo, params::ShadeMapRange::from_min_hi, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, "fromMin");
      ModSlider("from max", &n->fromMax, params::ShadeMapRange::from_max_lo, params::ShadeMapRange::from_max_hi, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, "fromMax");
      ModSlider("to min", &n->toMin, params::ShadeMapRange::to_min_lo, params::ShadeMapRange::to_min_hi, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, "toMin");
      ModSlider("to max", &n->toMax, params::ShadeMapRange::to_max_lo, params::ShadeMapRange::to_max_hi, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, "toMax");
      if (n->interp == ShadeMapRangeNode::kStepped)
         ModSlider("steps", &n->steps, params::ShadeMapRange::steps_lo, params::ShadeMapRange::steps_hi, "%.0f", kParamWidth, false, 1.0f);
      if (n->interp == ShadeMapRangeNode::kLinear || n->interp == ShadeMapRangeNode::kStepped)
         ModCheckbox("clamp", &n->clampResult);
   }


   void DrawShadeSeparateParams(ShadeSeparateNode* n)
   {
      DropdownButton("mode", ShadeSeparateNode::ModeNames(), n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; n->channel = 0; });
      DropdownButton("channel", ShadeSeparateNode::ChannelNames(n->mode), n->channel, [n](int i) { PushUndoCheckpoint(); n->channel = i; });
   }


   void DrawShadeCombineParams(ShadeCombineNode* n)
   {
      DropdownButton("mode", ShadeSeparateNode::ModeNames(), n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; });
      static const char* kXyz[3] = { "x", "y", "z" };
      static const char* kHsv[3] = { "hue", "saturation", "value" };
      const char* const* lab = n->mode == 1 ? kHsv : kXyz;
      for (int i = 0; i < 3; i++)
         ModSlider(lab[i], &n->value[i], 0.0f, 1.0f, "%.3f", kParamWidth, false, 0.0f, nullptr, nullptr, -1, kXyz[i]);
   }


   void DrawShadeCurvesParams(ShadeCurvesNode* n)
   {
      ModSlider("factor", &n->factor, 0.0f, 1.0f);
      ModSliderInt("points", &n->count, params::ShadeCurves::count_lo, ShadeCurvesNode::kMaxPoints);
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


   void DrawShadeBumpParams(ShadeBumpNode* n)
   {
      ModSlider("strength", &n->strength, 0.0f, 1.0f);
      ModSlider("distance", &n->distance, 0.0f, 1.0f);
      ModCheckbox("invert", &n->invert);
   }


   void DrawShadeNormalMapParams(ShadeNormalMapNode* n)
   {
      ModSlider("strength", &n->strength, params::ShadeNormalMap::strength_lo, params::ShadeNormalMap::strength_hi);
   }


   void DrawShadeFresnelParams(ShadeFresnelNode* n)
   {
      ModSlider("ior", &n->ior, params::ShadeFresnel::ior_lo, params::ShadeFresnel::ior_hi);
   }


   void DrawShadeLayerWeightParams(ShadeLayerWeightNode* n)
   {
      DropdownButton("output", ShadeLayerWeightNode::OutputNames(), n->output, [n](int i) { PushUndoCheckpoint(); n->output = i; });
      ModSlider("blend", &n->blend, 0.0f, 1.0f);
   }


   void DrawShadeGeometryParams(ShadeGeometryNode* n)
   {
      DropdownButton("output", ShadeGeometryNode::OutputNames(), n->output, [n](int i) { PushUndoCheckpoint(); n->output = i; });
   }


   void DrawMeshResynthParams(MeshResynthNode* n)
   {
      ImGui::TextDisabled("generation %d, %s triangles", n->Generation(), UiFmt::Count((long long)(n->TriangleCount())).c);
      if (ActionButton::Draw("step")) n->StepOnce();
      ImGui::SameLine();
      if (ActionButton::Draw("reset")) n->Reset();
      ImGui::SameLine();
      if (ActionButton::Draw("randomise")) n->Randomise();

      NodeSeparator("evolve");
      ModSlider("chaos", &n->chaos, params::MeshResynth::chaos_lo, params::MeshResynth::chaos_hi);
      ModCheckbox("auto step", &n->autoStep);
      if (n->autoStep)
         ModSlider("steps per beat", &n->stepsPerBeat, params::MeshResynth::steps_per_beat_lo, params::MeshResynth::steps_per_beat_hi);
      ModSlider("seed", &n->seed, params::MeshResynth::seed_lo, params::MeshResynth::seed_hi, "%.0f", kParamWidth, false, 1.0f);
      ModSliderInt("triangle budget", &n->triangleBudget, params::MeshResynth::triangle_budget_lo, params::MeshResynth::triangle_budget_hi);

      NodeSeparator("operators");
      for (int i = 0; i < MeshResynthNode::kOpCount; i++)
         ModSlider(MeshResynthNode::OpNames()[i].c_str(), &n->weight[i], 0.0f, 1.0f);
   }


   void DrawImageToPointsParams(ImageToPointsNode* n)
   {
      ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      ModSliderInt("density", &n->density, params::ImageToPoints::density_lo, params::ImageToPoints::density_hi);
      ModSlider("width", &n->width, params::ImageToPoints::width_lo, params::ImageToPoints::width_hi);
      ModSlider("height", &n->height, params::ImageToPoints::height_lo, params::ImageToPoints::height_hi);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);

      NodeSeparator("depth");
      DropdownButton("from", ImageToPointsNode::DepthSourceNames(), n->depthSource,
                     [n](int i) { n->depthSource = i; });
      ModSlider("depth scale", &n->depthScale, params::ImageToPoints::depth_scale_lo, params::ImageToPoints::depth_scale_hi);

      NodeSeparator("points");
      ModSlider("point size", &n->pointSize, params::ImageToPoints::point_size_lo, params::ImageToPoints::point_size_hi);
      ModSlider("size from luma", &n->sizeFromLuma, -1.0f, 1.0f);
      ModCheckbox("use image colour", &n->useImageColor);
      ColorSwatch("tint", n->tint, n);
   }


   void DrawDepthProjectionParams(DepthProjectionNode* n)
   {
      if (n->outputType == DepthProjectionNode::kPoints)
         ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      else
         ImGui::TextDisabled("%s triangles (%s vertices)", UiFmt::Count((long long)(n->TriangleCount())).c, UiFmt::Count((long long)(n->GetMesh().vertices.size())).c);

      DropdownButton("projection", DepthProjectionNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      DropdownButton("output", DepthProjectionNode::OutputTypeNames(), n->outputType,
                     [n](int i) { n->outputType = i; });
      ModSliderInt("density", &n->density, params::DepthProjection::density_lo, params::DepthProjection::density_hi);

      NodeSeparator("depth");
      DropdownButton("source", DepthProjectionNode::DepthSourceNames(), n->depthSource,
                     [n](int i) { n->depthSource = i; });
      ModSlider("near depth", &n->nearDepth, params::DepthProjection::near_depth_lo, params::DepthProjection::near_depth_hi);
      ModSlider("far depth", &n->farDepth, params::DepthProjection::far_depth_lo, params::DepthProjection::far_depth_hi);
      ModSlider("depth scale", &n->depthScale, params::DepthProjection::depth_scale_lo, params::DepthProjection::depth_scale_hi);
      ModSlider("depth curve", &n->depthCurve, params::DepthProjection::depth_curve_lo, params::DepthProjection::depth_curve_hi);
      ModSlider("clip near", &n->clipNear, 0.0f, 1.0f);
      ModSlider("clip far", &n->clipFar, 0.0f, 1.0f);

      if (n->projection == DepthProjectionNode::kPerspective || n->projection == DepthProjectionNode::kRadial)
      {
         NodeSeparator("camera");
         ModSlider("fov", &n->fov, params::DepthProjection::fov_lo, params::DepthProjection::fov_hi);
         ModCheckbox("auto aspect", &n->autoAspect);
         if (!n->autoAspect)
            ModSlider("aspect", &n->customAspect, params::DepthProjection::custom_aspect_lo, params::DepthProjection::custom_aspect_hi);
         ModSlider("focal scale", &n->focalScale, params::DepthProjection::focal_scale_lo, params::DepthProjection::focal_scale_hi);
         ModSlider("center x", &n->principalPointX, -1.0f, 1.0f);
         ModSlider("center y", &n->principalPointY, -1.0f, 1.0f);
      }
      else
      {
         NodeSeparator("dimensions");
         ModSlider("width", &n->planarWidth, params::DepthProjection::planar_width_lo, params::DepthProjection::planar_width_hi);
         ModSlider("height", &n->planarHeight, params::DepthProjection::planar_height_lo, params::DepthProjection::planar_height_hi);
      }

      NodeSeparator("appearance");
      if (n->outputType == DepthProjectionNode::kPoints)
         ModSlider("point size", &n->pointSize, params::DepthProjection::point_size_lo, params::DepthProjection::point_size_hi);
      else
         ModSlider("edge tear", &n->edgeTearThreshold, params::DepthProjection::edge_tear_threshold_lo, params::DepthProjection::edge_tear_threshold_hi);

      DropdownButton("color", DepthProjectionNode::ColorModeNames(), n->colorMode,
                     [n](int i) { n->colorMode = i; });
      ColorSwatch("tint", n->tint, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
      ModSlider("roughness", &n->roughness, 0.0f, 1.0f);
   }


   void DrawMeshToPointsParams(MeshToPointsNode* n)
   {
      DropdownButton("sample", MeshToPointsNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ImGui::TextDisabled("%s points, %s triangles", UiFmt::Count((long long)(n->PointCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);
      ModSliderInt("max points", &n->maxPoints, params::geometry_params::max_points_lo, params::geometry_params::max_points_hi);
      ModSlider("point size", &n->pointSize, params::geometry_params::point_size_lo, params::geometry_params::point_size_hi);
      ModCheckbox("weld seams", &n->weld);
      if (n->mode == 1)
         ModSlider("dissolve angle", &n->dissolveAngleDegrees, params::geometry_params::dissolve_angle_degrees_lo, params::geometry_params::dissolve_angle_degrees_hi);
   }


   void DrawDistributePointsOnFacesParams(DistributePointsOnFacesNode* n)
   {
      ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      ModSlider("density", &n->density, params::DistributePointsOnFaces::density_lo, params::DistributePointsOnFaces::density_hi, "%.0f", kParamWidth, false, 0.0f, LogPosToValue, LogValueToPos);
      DropdownButton("method", DistributePointsOnFacesNode::MethodNames(), n->method,
                     [n](int i) { n->method = i; });
      if (n->method == MeshOps::kDistributePoisson)
         ModSlider("min distance", &n->minDistance, params::DistributePointsOnFaces::min_distance_lo, params::DistributePointsOnFaces::min_distance_hi);
      ModSlider("point size", &n->pointSize, params::DistributePointsOnFaces::point_size_lo, params::DistributePointsOnFaces::point_size_hi);
      ModSlider("seed", &n->seed, params::DistributePointsOnFaces::seed_lo, params::DistributePointsOnFaces::seed_hi, "%.0f", kParamWidth, false, 1.0f);
   }


   void DrawPointsToVerticesParams(PointsToVerticesNode* n)
   {
      ImGui::TextDisabled("%s vertices", UiFmt::Count((long long)(n->VertexCount())).c);
      ModCheckbox("alive only", &n->aliveOnly);
   }


   void DrawProximityLinesParams(ProximityLinesNode* n)
   {
      ImGui::TextDisabled("%s lines", UiFmt::Count((long long)(n->LinkCount())).c);
      ModSlider("radius", &n->radius, params::ProximityLines::radius_lo, params::ProximityLines::radius_hi, "%.3f");
      ModSliderInt("max per point", &n->maxPerPoint, params::ProximityLines::max_per_point_lo, params::ProximityLines::max_per_point_hi);
      ModSliderInt("max lines", &n->maxLinks, params::ProximityLines::max_links_lo, params::ProximityLines::max_links_hi);
      ModSlider("thickness", &n->thickness, params::ProximityLines::thickness_lo, params::ProximityLines::thickness_hi, "%.4f");
      ModCheckbox("alive only", &n->aliveOnly);
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
      ModSliderInt("length", &n->length, params::PointTrails::length_lo, params::PointTrails::length_hi);
      ModSliderInt("sample every", &n->sampleEvery, params::PointTrails::sample_every_lo, params::PointTrails::sample_every_hi);
      ModSlider("width", &n->width, params::PointTrails::width_lo, params::PointTrails::width_hi, "%.4f");
      ModSlider("fade", &n->fade, 0.0f, 1.0f);
      ModSlider("taper", &n->taper, 0.0f, 1.0f);
      DropdownButton("colour", PointTrailsNode::ColorModeNames(), n->colorMode,
                     [n](int i) { PushUndoCheckpoint(); n->colorMode = i; });
      if (n->colorMode == PointTrailsNode::kColorGradient)
      {
         ColorSwatch("head", n->headColor, n);
         ColorSwatch("tail", n->tailColor, n);
      }
      DropdownButton("match by", PointTrailsNode::MatchModeNames(), n->matchMode,
                     [n](int i) { PushUndoCheckpoint(); n->matchMode = i; });
      ModSliderInt("point limit", &n->pointLimit, params::PointTrails::point_limit_lo, params::PointTrails::point_limit_hi);
      if (ActionButton::Draw("Clear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }


   void DrawCurveOpsParams(CurveOpsNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s points out", UiFmt::Count((long long)(n->PointCount())).c);
      DropdownButton("mode", CurveOpsNode::ModeNames(), n->mode,
                     [n](int i) { PushUndoCheckpoint(); n->mode = i; });
      switch (n->mode)
      {
         case CurveOpsNode::kSimplify:
            ModSlider("tolerance", &n->tolerance, params::CurveOps::tolerance_lo, params::CurveOps::tolerance_hi, "%.3f");
            break;
         case CurveOpsNode::kSmooth:
            ModSliderInt("iterations", &n->iterations, params::CurveOps::iterations_lo, params::CurveOps::iterations_hi);
            ModSlider("strength", &n->strength, 0.0f, 1.0f);
            break;
         case CurveOpsNode::kOffset:
            ModSlider("distance", &n->distance, params::CurveOps::distance_lo, params::CurveOps::distance_hi);
            ModSliderInt("arc steps", &n->arcSteps, params::CurveOps::arc_steps_lo, params::CurveOps::arc_steps_hi);
            break;
         default:
            ModSliderInt("count", &n->count, params::CurveOps::count_lo, params::CurveOps::count_hi);
            break;
      }
      ModSlider("tube radius", &n->radius, params::CurveOps::radius_lo, params::CurveOps::radius_hi, "%.3f");
   }


   void DrawRevolveParams(RevolveNode* n)
   {
      DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; });
      ModSlider("angle", &n->angle, params::Revolve::angle_lo, params::Revolve::angle_hi, "%.0f");
      ModSliderInt("segments", &n->segments, params::Revolve::segments_lo, params::Revolve::segments_hi);
      ModCheckbox("caps", &n->caps);
   }


   void DrawSweepParams(SweepNode* n)
   {
      if (n->profile == nullptr)
      {
         ModSlider("profile radius", &n->profileRadius, params::Sweep::profile_radius_lo, params::Sweep::profile_radius_hi, "%.3f");
         ModSliderInt("profile sides", &n->profileSides, params::Sweep::profile_sides_lo, params::Sweep::profile_sides_hi);
      }
      ModSlider("start scale", &n->startScale, params::Sweep::start_scale_lo, params::Sweep::start_scale_hi);
      ModSlider("end scale", &n->endScale, params::Sweep::end_scale_lo, params::Sweep::end_scale_hi);
      ModSlider("twist", &n->twist, params::Sweep::twist_lo, params::Sweep::twist_hi, "%.0f");
      ModCheckbox("caps", &n->caps);
   }


   void OpenIn3DViewButton(int mode)
   {
      if (gParamRegisterOnly || !ActionButton::Draw("Open in 3D View", ImVec2(kPreviewSize, 0)))
         return;
      if (const GraphNode* gn = FindNodeByIndex(gCurrentNodeIndex))
         View3DOpenNode(*gn, mode);
   }

   void DrawArmatureParams(ArmatureNode* n)
   {
      PushTextWrapWidth(kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      PopTextWrapWidth();
      if (n->path == nullptr || gParamRegisterOnly)
         DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; });
      ModSliderInt("bones", &n->bones, params::Armature::bones_lo, params::Armature::bones_hi);
      ModSlider("envelope", &n->envelope, params::Armature::envelope_lo, params::Armature::envelope_hi);
      NodeSeparator("bend");
      ModSlider("curl", &n->curl, params::Armature::curl_lo, params::Armature::curl_hi, "%.0f");
      ModSlider("curl direction", &n->curlDirection, params::Armature::curl_direction_lo, params::Armature::curl_direction_hi, "%.0f");
      ModSlider("twist", &n->twist, params::Armature::twist_lo, params::Armature::twist_hi, "%.0f");
      NodeSeparator("wave");
      ModSlider("wave amount", &n->waveAmount, params::Armature::wave_amount_lo, params::Armature::wave_amount_hi, "%.1f");
      ModSlider("wave length", &n->waveLength, params::Armature::wave_length_lo, params::Armature::wave_length_hi);
      ModSlider("wave speed", &n->waveSpeed, params::Armature::wave_speed_lo, params::Armature::wave_speed_hi);
      ModSlider("wave phase", &n->wavePhase, 0.0f, 1.0f);
      DrawRigPoseParams(n);
      OpenIn3DViewButton(3);
   }

   void DrawDelaunayParams(DelaunayMeshNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      DropdownButton("plane", DelaunayMeshNode::PlaneNames(), n->plane,
                     [n](int i) { PushUndoCheckpoint(); n->plane = i; });
      ModCheckbox("alive only", &n->aliveOnly);
      ModSliderInt("max points", &n->maxPoints, params::DelaunayMesh::max_points_lo, params::DelaunayMesh::max_points_hi);
      if (dynamic_cast<VoronoiCellsNode*>(n) != nullptr)
         ModSlider("inset", &n->inset, params::DelaunayMesh::inset_lo, params::DelaunayMesh::inset_hi);
   }


   void DrawDistributePointsInGridParams(DistributePointsInGridNode* n)
   {
      ImGui::TextDisabled("%s points", UiFmt::Count((long long)(n->PointCount())).c);
      ModSliderInt("count x", &n->countX, params::DistributePointsInGrid::count_x_lo, params::DistributePointsInGrid::count_x_hi);
      ModSliderInt("count y", &n->countY, params::DistributePointsInGrid::count_y_lo, params::DistributePointsInGrid::count_y_hi);
      ModSlider("spacing x", &n->spacingX, params::DistributePointsInGrid::spacing_x_lo, params::DistributePointsInGrid::spacing_x_hi);
      ModSlider("spacing y", &n->spacingY, params::DistributePointsInGrid::spacing_y_lo, params::DistributePointsInGrid::spacing_y_hi);
      ModSlider("jitter", &n->jitter, 0.0f, 1.0f);
      ModSlider("point size", &n->pointSize, params::DistributePointsInGrid::point_size_lo, params::DistributePointsInGrid::point_size_hi);
      ModSlider("seed", &n->seed, params::DistributePointsInGrid::seed_lo, params::DistributePointsInGrid::seed_hi, "%.0f", kParamWidth, false, 1.0f);
      ColorSwatch("tint", n->tint, n);
   }


   void DrawMergeByDistanceParams(MergeByDistanceNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      ModSlider("threshold", &n->threshold, params::MergeByDistance::threshold_lo, params::MergeByDistance::threshold_hi, "%.4f");
   }


   void DrawBakeParams(BakeNode* n)
   {
      DropdownButton("bake", BakeNode::ModeNames(), n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; }, kParamWidth, true, false);
      DropdownButton("size", BakeNode::SizeNames(), n->size, [n](int i) { PushUndoCheckpoint(); n->size = i; }, kParamWidth, true, true);
      if (n->mode == BakeNode::kAO || n->mode == BakeNode::kThickness)
      {
         ModSliderInt("samples", &n->samples, params::Bake::samples_lo, params::Bake::samples_hi);
         ModSlider("distance", &n->distance, params::Bake::distance_lo, params::Bake::distance_hi);
      }
      if (n->Baking())
         ImGui::TextDisabled("baking");
   }


   void DrawSculptParams(SculptNode* n)
   {
      ModSlider("amount", &n->amount, params::Sculpt::amount_lo, params::Sculpt::amount_hi);
      ModSliderInt("level", &n->level, params::Sculpt::level_lo, params::Sculpt::level_hi);
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
      ModSlider("depth", &n->depth, params::Text3D::depth_lo, params::Text3D::depth_hi);
      ModSlider("bevel", &n->bevel, params::Text3D::bevel_lo, params::Text3D::bevel_hi);
      ModSlider("tracking", &n->letterSpacing, params::Text3D::letter_spacing_lo, params::Text3D::letter_spacing_hi);

      NodeSeparator("transform");
      ModSlider("pos X", &n->posX, params::Text3D::pos_x_lo, params::Text3D::pos_x_hi);
      ModSlider("pos Y", &n->posY, params::Text3D::pos_y_lo, params::Text3D::pos_y_hi);
      ModSlider("pos Z", &n->posZ, params::Text3D::pos_z_lo, params::Text3D::pos_z_hi);
      ModSlider("rot X", &n->rotX, params::Text3D::rot_x_lo, params::Text3D::rot_x_hi);
      ModSlider("rot Y", &n->rotY, params::Text3D::rot_y_lo, params::Text3D::rot_y_hi);
      ModSlider("rot Z", &n->rotZ, params::Text3D::rot_z_lo, params::Text3D::rot_z_hi);
      ModSlider("scale", &n->uniformScale, params::Text3D::uniform_scale_lo, params::Text3D::uniform_scale_hi);
      ModSlider("spin", &n->spinY, params::Text3D::spin_y_lo, params::Text3D::spin_y_hi);
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
            ModSlider("speed", &n->clipSpeed, params::ModelSource::clip_speed_lo, params::ModelSource::clip_speed_hi);
            ModSlider("phase", &n->clipPhase, 0.0f, 1.0f);
            DropdownButton("sync", { "seconds", "beats" }, n->clipSync,
                           [n](int i) { PushUndoCheckpoint(); n->clipSync = i; }, kParamWidth, true, true);
            if (n->clipSync == 1 || gParamRegisterOnly)
               ModSlider("loop beats", &n->clipBeats, params::ModelSource::clip_beats_lo, params::ModelSource::clip_beats_hi);
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
         ModSlider("point size", &n->pointSize, params::ModelSource::point_size_lo, params::ModelSource::point_size_hi);
      }
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
         ImGui::BeginDisabled(detailUnused && !gParamRegisterOnly);
         ModSliderInt("detail", &n->detail, params::Geometry::detail_lo, params::Geometry::detail_hi);
         ImGui::EndDisabled();
      }
      ModSlider("bevel", &n->bevel, 0.0f, 1.0f);
      if (n->shape == 13) // supershape
      {
         ModSlider("n2", &n->superN2, params::Geometry::super_n2_lo, params::Geometry::super_n2_hi);
         ModSlider("n3", &n->superN3, params::Geometry::super_n3_lo, params::Geometry::super_n3_hi);
         ModSlider("p2", &n->superP2, params::Geometry::super_p2_lo, params::Geometry::super_p2_hi);
         ModSlider("p3", &n->superP3, params::Geometry::super_p3_lo, params::Geometry::super_p3_hi);
      }
      if (n->bevel > 0.0f)
         ModSliderInt("bevel segments", &n->bevelSegments, params::Geometry::bevel_segments_lo, params::Geometry::bevel_segments_hi);
      {
         const bool sidesUnused = n->shape == 0 || n->shape == 1 || n->shape == 3 || (n->shape >= 14 && n->shape <= 19);
         ImGui::BeginDisabled(sidesUnused && !gParamRegisterOnly);
         ModSliderInt("sides", &n->sides, params::Geometry::sides_lo, params::Geometry::sides_hi);
         ImGui::EndDisabled();
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
            ModSlider("tooth depth", &n->superN2, params::Geometry::super_n2_lo, params::Geometry::super_n2_2_hi);
            ModSlider("hub hole", &n->superN3, params::Geometry::super_n3_2_lo, params::Geometry::super_n3_2_hi);
            break;
         case 21:
            ModSlider("depth", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_hi);
            ModSlider("inner ratio", &n->superN2, params::Geometry::super_n2_lo, params::Geometry::super_n2_3_hi);
            break;
         case 22:
            ModSlider("inner radius", &n->discInner, params::Geometry::disc_inner_lo, params::Geometry::disc_inner_hi); break;
         case 23:
            ModSlider("shaft radius", &n->tubeRadius, params::Geometry::tube_radius_lo, params::Geometry::tube_radius_3_hi);
            ModSlider("head length", &n->superN2, params::Geometry::super_n2_2_lo, params::Geometry::super_n2_4_hi);
            break;
         default: break;
      }
      if (n->shape == 7 || n->shape == 12)
      {
         ModSliderInt(n->shape == 12 ? "turns" : "knot p", &n->knotP, params::Geometry::knot_p_lo, params::Geometry::knot_p_hi);   // ui-audit-ok: shape id 12 (helix) relabels the knot controls
         ModSliderInt(n->shape == 12 ? "height" : "knot q", &n->knotQ, params::Geometry::knot_q_lo, params::Geometry::knot_q_hi);   // ui-audit-ok: shape id 12 (helix) relabels the knot controls
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
         ModCheckbox("selection only", &n->selectionOnly);

      switch (n->op)
      {
         case GeometryOpNode::kTransform:
            if (n->selectionOnly)
            {
               ImGui::TextDisabled("%zu of %s faces selected", n->SelectedCount(), UiFmt::Count((long long)(n->TriangleCount())).c);
               ModCheckbox("move along normals", &n->moveAlongNormals);
               if (n->moveAlongNormals)
                  ModSlider("normal amount", &n->normalAmount, params::geometry_params::normal_amount_lo, params::geometry_params::normal_amount_hi);
            }
            ModSlider("move x", &n->offsetX, params::geometry_params::offset_x_lo, params::geometry_params::offset_x_hi);
            ModSlider("move y", &n->offsetY, params::geometry_params::offset_y_lo, params::geometry_params::offset_y_hi);
            ModSlider("move z", &n->offsetZ, params::geometry_params::offset_z_lo, params::geometry_params::offset_z_hi);
            ModSlider("rotate x", &n->rotX, params::geometry_params::rot_x_lo, params::geometry_params::rot_x_hi, "%.1f\xC2\xB0");
            ModSlider("rotate y", &n->rotY, params::geometry_params::rot_y_lo, params::geometry_params::rot_y_hi, "%.1f\xC2\xB0");
            ModSlider("rotate z", &n->rotZ, params::geometry_params::rot_z_lo, params::geometry_params::rot_z_hi, "%.1f\xC2\xB0");
            ModSlider("scale x", &n->scaleX, params::geometry_params::scale_x_lo, params::geometry_params::scale_x_hi);
            ModSlider("scale y", &n->scaleY, params::geometry_params::scale_y_lo, params::geometry_params::scale_y_hi);
            ModSlider("scale z", &n->scaleZ, params::geometry_params::scale_z_lo, params::geometry_params::scale_z_hi);
            // Point rotate/scale pivot about - default (0,0,0) matches the
            // mesh's own origin, i.e. no visible change from before pivot
            // existed. Sits after scale/before spin, same order TransformMatrix()
            // applies move/rotate/scale/pivot in.
            {
            const bool pivotUnused = n->rotX == 0.0f && n->rotY == 0.0f && n->rotZ == 0.0f && n->spin == 0.0f
               && n->scaleX == 1.0f && n->scaleY == 1.0f && n->scaleZ == 1.0f;
            ImGui::BeginDisabled(pivotUnused && !gParamRegisterOnly);   // pivot only matters once something rotates or scales
            ModSlider("pivot x", &n->pivotX, params::geometry_params::pivot_x_lo, params::geometry_params::pivot_x_hi);
            ModSlider("pivot y", &n->pivotY, params::geometry_params::pivot_y_lo, params::geometry_params::pivot_y_hi);
            ModSlider("pivot z", &n->pivotZ, params::geometry_params::pivot_z_lo, params::geometry_params::pivot_z_hi);
            ImGui::EndDisabled();
            }
            ModSlider("spin / beat", &n->spin, params::geometry_params::spin_lo, params::geometry_params::spin_hi);
            break;
         case GeometryOpNode::kArray:
            ModSliderInt("count", &n->count, params::geometry_params::count_lo, params::geometry_params::count_hi);
            ModCheckbox("radial", &n->radial);
            if (n->radial)
               ModSlider("radius", &n->radius, params::geometry_params::radius_lo, params::geometry_params::radius_hi);
            else
            {
               ModSlider("offset x", &n->offsetX, params::geometry_params::offset_x_2_lo, params::geometry_params::offset_x_2_hi);
               ModSlider("offset y", &n->offsetY, params::geometry_params::offset_y_2_lo, params::geometry_params::offset_y_2_hi);
               ModSlider("offset z", &n->offsetZ, params::geometry_params::offset_z_2_lo, params::geometry_params::offset_z_2_hi);
            }
            ModSlider("rot / step", &n->rotStep, params::geometry_params::rot_step_lo, params::geometry_params::rot_step_hi, "%.1f\xC2\xB0");
            ModSlider("scale / step", &n->scaleStep, params::geometry_params::scale_step_lo, params::geometry_params::scale_step_hi);
            break;
         case GeometryOpNode::kSubdivide:
            ModSliderInt("levels", &n->levels, params::geometry_params::levels_lo, params::geometry_params::levels_hi);
            ModSlider("smooth", &n->smooth, params::geometry_params::smooth_lo, params::geometry_params::smooth_hi);
            break;
         case GeometryOpNode::kSolidify:
            ModSlider("thickness", &n->thickness, params::geometry_params::thickness_lo, params::geometry_params::thickness_hi, "%.4f", kParamWidth, false, 0.0f, LogPosToValue, LogValueToPos);
            ModCheckbox("keep original", &n->keepOriginal);
            break;
         case GeometryOpNode::kExtrude:
            ModSlider("distance", &n->thickness, params::geometry_params::thickness_2_lo, params::geometry_params::thickness_2_hi);
            ModSlider("inset", &n->inset, params::geometry_params::inset_lo, params::geometry_params::inset_hi);
            break;
         case GeometryOpNode::kWireframe:
            ModSlider("thickness", &n->thickness, params::geometry_params::thickness_3_lo, params::geometry_params::thickness_3_hi);
            break;
         case GeometryOpNode::kTriangulate:
            ModSlider("jitter", &n->amount, params::geometry_params::amount_lo, params::geometry_params::amount_hi);
            break;
         case GeometryOpNode::kNormals:
            ModCheckbox("flat shade", &n->flatShade);
            ModCheckbox("flip", &n->flipNormals);
            break;
         case GeometryOpNode::kExplode:
         {
            static const std::vector<std::string> kExplodeByNames = { "Faces", "Loose Parts" };
            DropdownButton("by", kExplodeByNames, n->explodeBy, [n](int i) { n->explodeBy = i; });
            ModSlider("amount", &n->amount, params::geometry_params::amount_2_lo, params::geometry_params::amount_2_hi);
            ModSlider("seed", &n->seed, params::geometry_params::seed_lo, params::geometry_params::seed_hi, "%.0f", kParamWidth, false, 1.0f);
            break;
         }
         case GeometryOpNode::kSmooth:
            ModSliderInt("pre-subdivide", &n->levels, params::geometry_params::levels_lo, params::geometry_params::levels_hi);
            ModSliderInt("iterations", &n->iterations, params::geometry_params::iterations_lo, params::geometry_params::iterations_hi);
            ModSlider("strength", &n->amount, 0.0f, 1.0f);
            break;
         case GeometryOpNode::kMirror:
            DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
            ModSlider("plane offset", &n->mirrorOffset, params::geometry_params::mirror_offset_lo, params::geometry_params::mirror_offset_hi);
            ModCheckbox("keep original", &n->keepOriginal);
            ImGui::BeginDisabled(!n->keepOriginal && !gParamRegisterOnly); // the seam only exists when both halves are kept
            ModCheckbox("weld seam", &n->weldSeam);
            ImGui::EndDisabled();
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
                  ModSlider("start", &n->selectA, params::geometry_params::select_a_lo, params::geometry_params::select_a_hi, "%.0f");
                  ModSlider("count", &n->selectB, params::geometry_params::select_b_lo, params::geometry_params::select_b_hi, "%.0f");
                  ModSlider("every", &n->selectC, params::geometry_params::select_c_lo, params::geometry_params::select_c_hi, "%.0f");
                  break;
               case 2: // position along an axis
                  DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
                  ModSlider("min", &n->selectA, params::geometry_params::select_a_2_lo, params::geometry_params::select_a_2_hi);
                  ModSlider("max", &n->selectB, params::geometry_params::select_b_2_lo, params::geometry_params::select_b_2_hi);
                  break;
               case 3: // normal direction
                  DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
                  ModSlider("facing", &n->selectA, -1.0f, 1.0f);
                  ModSlider("sign", &n->selectC, -1.0f, 1.0f);
                  break;
               case 4: // random
                  ModSlider("amount", &n->selectA, 0.0f, 1.0f);
                  ModSlider("seed", &n->selectSeed, params::geometry_params::select_seed_lo, params::geometry_params::select_seed_hi);
                  break;
               case 5: // radius
                  ModSlider("x", &n->selectA, params::geometry_params::select_a_2_lo, params::geometry_params::select_a_2_hi);
                  ModSlider("y", &n->selectB, params::geometry_params::select_b_2_lo, params::geometry_params::select_b_2_hi);
                  ModSlider("z", &n->selectC, params::geometry_params::select_c_2_lo, params::geometry_params::select_c_2_hi);
                  ModSlider("radius", &n->selectSeed, params::geometry_params::select_seed_2_lo, params::geometry_params::select_seed_2_hi);
                  break;
               default: break;
            }
            ModCheckbox("invert", &n->selectInvert);
            ModCheckbox("add to selection", &n->selectAppend);
            break;
         }
         case GeometryOpNode::kDelete:
         case GeometryOpNode::kDeleteSelected:
            if (n->selectionOnly)
               ImGui::TextDisabled("%zu of %s faces selected", n->SelectedCount(), UiFmt::Count((long long)(n->TriangleCount())).c);
            ModCheckbox("keep selected instead", &n->keepSelected);
            break;
         case GeometryOpNode::kTransformSelected:
            ModCheckbox("move along normals", &n->moveAlongNormals);
            if (n->moveAlongNormals)
               ModSlider("normal amount", &n->normalAmount, params::geometry_params::normal_amount_lo, params::geometry_params::normal_amount_hi);
            ModSlider("move x", &n->offsetX, params::geometry_params::offset_x_lo, params::geometry_params::offset_x_hi);
            ModSlider("move y", &n->offsetY, params::geometry_params::offset_y_lo, params::geometry_params::offset_y_hi);
            ModSlider("move z", &n->offsetZ, params::geometry_params::offset_z_lo, params::geometry_params::offset_z_hi);
            ModSlider("rotate x", &n->rotX, params::geometry_params::rot_x_lo, params::geometry_params::rot_x_hi, "%.1f\xC2\xB0");
            ModSlider("rotate y", &n->rotY, params::geometry_params::rot_y_lo, params::geometry_params::rot_y_hi, "%.1f\xC2\xB0");
            ModSlider("rotate z", &n->rotZ, params::geometry_params::rot_z_lo, params::geometry_params::rot_z_hi, "%.1f\xC2\xB0");
            ModSlider("scale x", &n->scaleX, params::geometry_params::scale_x_2_lo, params::geometry_params::scale_x_2_hi);
            ModSlider("scale y", &n->scaleY, params::geometry_params::scale_y_2_lo, params::geometry_params::scale_y_2_hi);
            ModSlider("scale z", &n->scaleZ, params::geometry_params::scale_z_2_lo, params::geometry_params::scale_z_2_hi);
            ModSlider("spin / beat", &n->spin, params::geometry_params::spin_lo, params::geometry_params::spin_hi);
            break;
         case GeometryOpNode::kExtrudeSelected:
            ModSlider("distance", &n->thickness, -1.0f, 1.0f);
            ModSlider("inset", &n->inset, params::geometry_params::inset_lo, params::geometry_params::inset_hi);
            break;
         case GeometryOpNode::kBevel:
            ModSlider("width", &n->bevelWidth, params::geometry_params::bevel_width_lo, params::geometry_params::bevel_width_hi, "%.3f");
            ModSliderInt("segments", &n->bevelSegments, params::geometry_params::bevel_segments_lo, params::geometry_params::bevel_segments_hi);
            ModSlider("sharp angle", &n->bevelAngle, params::geometry_params::bevel_angle_lo, params::geometry_params::bevel_angle_hi, "%.0f deg");
            break;
         case GeometryOpNode::kInset:
            ModSlider("width", &n->insetWidth, params::geometry_params::inset_width_lo, params::geometry_params::inset_width_hi, "%.3f");
            ModSlider("depth", &n->insetDepth, params::geometry_params::inset_depth_lo, params::geometry_params::inset_depth_hi, "%.3f");
            ModSlider("patch angle", &n->bevelAngle, params::geometry_params::bevel_angle_lo, params::geometry_params::bevel_angle_hi, "%.0f deg");
            break;
         case GeometryOpNode::kUVProject:
         {
            static const std::vector<std::string> kUVModes = { "Smart", "Cube", "Cylinder", "Sphere" };
            DropdownButton("project", kUVModes, n->uvMode, [n](int i) { PushUndoCheckpoint(); n->uvMode = i; }, kParamWidth, true, true);
            if (n->uvMode == 0)
            {
               ModSlider("angle", &n->uvAngle, params::geometry_params::uv_angle_lo, params::geometry_params::uv_angle_hi, "%.0f deg");
               ModSlider("margin", &n->uvMargin, params::geometry_params::uv_margin_lo, params::geometry_params::uv_margin_hi, "%.3f");
            }
            ModSlider("scale", &n->uvScale, params::geometry_params::uv_scale_lo, params::geometry_params::uv_scale_hi);
            break;
         }
         case GeometryOpNode::kFur:
         {
            static const std::vector<std::string> kFurNormals = { "Ribbon", "Tangent" };
            ModSlider("density", &n->furDensity, params::geometry_params::fur_density_lo, params::geometry_params::fur_density_hi, "%.0f / cm2");
            ModSliderInt("max strands", &n->furMaxStrands, params::geometry_params::fur_max_strands_lo, params::geometry_params::fur_max_strands_hi);
            ModSlider("length", &n->furLength, params::geometry_params::fur_length_lo, params::geometry_params::fur_length_hi, "%.4f");
            ModSlider("length jitter", &n->furJitter, 0.0f, 1.0f);
            ModSlider("width", &n->furWidth, params::geometry_params::fur_width_lo, params::geometry_params::fur_width_hi, "%.5f");
            ModSliderInt("segments", &n->furSegments, params::geometry_params::fur_segments_lo, params::geometry_params::fur_segments_hi);
            ModSlider("gravity", &n->furGravity, 0.0f, 1.0f);
            ModSlider("clump", &n->furClump, 0.0f, 1.0f);
            ModSlider("curl", &n->furCurl, 0.0f, 1.0f);
            ModSlider("comb x", &n->furComb[0], params::geometry_params::fur_comb_0_lo, params::geometry_params::fur_comb_0_hi);
            ModSlider("comb y", &n->furComb[1], params::geometry_params::fur_comb_1_lo, params::geometry_params::fur_comb_1_hi);
            ModSlider("comb z", &n->furComb[2], params::geometry_params::fur_comb_2_lo, params::geometry_params::fur_comb_2_hi);
            DropdownButton("normals", kFurNormals, n->furNormals, [n](int i) { PushUndoCheckpoint(); n->furNormals = i; }, kParamWidth, true, true);
            ModSlider("seed", &n->seed, params::geometry_params::seed_lo, params::geometry_params::seed_hi, "%.0f", kParamWidth, false, 1.0f);
            break;
         }
         case GeometryOpNode::kDecimate:
            ModSlider("keep", &n->decimateRatio, params::geometry_params::decimate_ratio_lo, params::geometry_params::decimate_ratio_hi, "%.2f");
            ModCheckbox("lock border", &n->lockBorder);
            break;
         case GeometryOpNode::kScrew:
            ModSliderInt("steps", &n->screwSteps, params::geometry_params::screw_steps_lo, params::geometry_params::screw_steps_hi);
            ModSlider("turns", &n->turns, params::geometry_params::turns_lo, params::geometry_params::turns_hi);
            ModSlider("rise / turn", &n->rise, params::geometry_params::rise_lo, params::geometry_params::rise_hi);
            ModSlider("radius", &n->radiusOffset, params::geometry_params::radius_offset_lo, params::geometry_params::radius_offset_hi);
            DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
            break;
         default:
            ModSlider("angle", &n->amount, params::geometry_params::amount_3_lo, params::geometry_params::amount_3_hi, "%.1f\xC2\xB0");
            DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
            break;
      }
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
      ModSlider("strength", &n->strength, params::Displacement::strength_lo, params::Displacement::strength_hi);
      if (n->mode == DisplacementNode::kScalar && !noise)
         ModSlider("midlevel", &n->midlevel, 0.0f, 1.0f);
      if (noise || gParamRegisterOnly)
      {
         NodeSeparator("noise");
         DropdownButton("type", DisplacementNode::NoiseTypeNames(), n->noiseType, [n](int i) { n->noiseType = i; });
         DropdownButton("affects", DisplacementNode::AffectsNames(), n->affects, [n](int i) { n->affects = i; });
         ModSlider("frequency", &n->frequency, params::Displacement::frequency_lo, params::Displacement::frequency_hi);
         ModSlider("speed", &n->speed, params::Displacement::speed_lo, params::Displacement::speed_hi);
         // Detail and grain only shape the octave types.
         ImGui::BeginDisabled(n->noiseType != 1 && n->noiseType != 3 && !gParamRegisterOnly);
         ModSliderInt("detail", &n->octaves, params::Displacement::octaves_lo, params::Displacement::octaves_hi);
         ModSlider("grain", &n->noiseRoughness, 0.0f, 1.0f);
         ImGui::EndDisabled();
         ModSliderInt("seed", &n->seed, params::Displacement::seed_lo, params::Displacement::seed_hi);
      }
      ModCheckbox("flat shade", &n->flatShade);
      ModCheckbox("flip normals", &n->flipNormals);
      ModCheckbox("selection only", &n->selectionOnly);
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


   void DrawAudioDisplacementParams(AudioDisplacementNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      DropdownButton("mode", AudioDisplacementNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModSlider("strength", &n->strength, params::AudioDisplacement::strength_lo, params::AudioDisplacement::strength_hi);
      ModSlider("frequency", &n->frequency, params::AudioDisplacement::frequency_lo, params::AudioDisplacement::frequency_hi);
      ModSlider("speed", &n->speed, params::AudioDisplacement::speed_lo, params::AudioDisplacement::speed_hi);
      ImGui::BeginDisabled(n->mode != AudioDisplacementNode::kModeAcousticRipples && !gParamRegisterOnly);   // only the ripple falloff reads it
      ModSlider("damping", &n->damping, params::AudioDisplacement::damping_lo, params::AudioDisplacement::damping_hi);
      ImGui::EndDisabled();
      ModSlider("attack", &n->attack, params::AudioDisplacement::attack_lo, params::AudioDisplacement::attack_hi, "%.0f ms");
      ModSlider("decay", &n->decay, params::AudioDisplacement::decay_lo, params::AudioDisplacement::decay_hi, "%.0f ms");
      ModSlider("midlevel", &n->midlevel, -1.0f, 1.0f);
      ModSliderInt("subdivide", &n->subdivide, params::AudioDisplacement::subdivide_lo, params::AudioDisplacement::subdivide_hi);

      if (n->mode == AudioDisplacementNode::kModeDirectionalAxis)
      {
         static const std::vector<std::string> kAxes = { "X Axis", "Y Axis", "Z Axis" };
         DropdownButton("axis", kAxes, n->axis, [n](int i) { n->axis = i; });
      }
      else if (n->mode == AudioDisplacementNode::kModeCymaticsChladni)
      {
         ModSliderInt("mode m", &n->chladniM, params::AudioDisplacement::chladni_m_lo, params::AudioDisplacement::chladni_m_hi);
         ModSliderInt("mode n", &n->chladniN, params::AudioDisplacement::chladni_n_lo, params::AudioDisplacement::chladni_n_hi);
      }

      ModCheckbox("flat shade", &n->flatShade);
      ModCheckbox("flip normals", &n->flipNormals);
      ModCheckbox("inherit material", &n->inheritMaterial);
      if (!n->inheritMaterial)
      {
         NodeSeparator("material");
         DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; });
         ColorSwatch("colour", n->color, n);
         ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
         ModSlider("roughness", &n->roughness, params::AudioDisplacement::roughness_lo, params::AudioDisplacement::roughness_hi);
         ModSlider("opacity", &n->opacity, 0.0f, 1.0f);
         ColorSwatch("emission", n->emissionColor, n);
         ModSlider("emission", &n->emission, params::AudioDisplacement::emission_lo, params::AudioDisplacement::emission_hi);
      }
   }


   void DrawAudioTextureParams(AudioTextureNode* n)
   {
      DropdownButton("mode", AudioTextureNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      if (n->mode == AudioTextureNode::kModeWaveform)
      {
         DropdownButton("window", AudioTextureNode::WindowSizeNames(), n->windowSizeIndex, [n](int i) { n->windowSizeIndex = i; });
      }
      else
      {
         ImGui::BeginDisabled();
         int dummy = 1; // 1024
         DropdownButton("window (1024)", AudioTextureNode::WindowSizeNames(), dummy, [](int) {});
         ImGui::EndDisabled();
      }
      ModSlider("gain", &n->gain, params::AudioTexture::gain_lo, params::AudioTexture::gain_hi);
      ModSlider("smoothing", &n->smoothing, params::AudioTexture::smoothing_lo, params::AudioTexture::smoothing_hi);
   }


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
                                 (int)(std::clamp(n->bandColor[b][2], 0.0f, 1.0f) * 255), tok::geometry_params_audio_color_ramp_editor_alpha_2);
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
         dl->AddPolyline(pts.data(), (int)pts.size(), tok::Role(tok::col::ink_white, tok::geometry_params_audio_color_ramp_editor_ink_white_alpha_2), false, tok::geometry_params_audio_color_ramp_editor_stroke);
      }

      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + barH), tok::Role(tok::col::edge_lo_2), tok::geometry_params_audio_color_ramp_editor_radius);

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
         ImU32 lineCol = isHov ? tok::Role(tok::col::role_modulation, 230) : tok::Role(tok::col::ink_white, 100);
         dl->AddLine(ImVec2(x, origin.y + tok::space_0_5), ImVec2(x, origin.y + barH - tok::space_0_5), lineCol, isHov ? tok::geometry_params_audio_color_ramp_editor_stroke_2 : tok::geometry_params_audio_color_ramp_editor_stroke);

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

      ModSlider("gain", &n->gain, params::AudioColorRamp::gain_lo, params::AudioColorRamp::gain_hi);
      ModSlider("min glow", &n->minBrightness, 0.0f, 1.0f);
   }


   void DrawAudioRibbonParams(AudioRibbonNode* n)
   {
      ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);
      ModSlider("width", &n->width, params::AudioRibbon::width_lo, params::AudioRibbon::width_hi);
      ModSlider("height scale", &n->heightScale, params::AudioRibbon::height_scale_lo, params::AudioRibbon::height_scale_hi);
      ModSliderInt("segments", &n->segments, params::AudioRibbon::segments_lo, params::AudioRibbon::segments_hi);
      ModSlider("gain", &n->gain, params::AudioRibbon::gain_lo, params::AudioRibbon::gain_hi);
      ModSlider("smoothing", &n->smoothing, params::AudioRibbon::smoothing_lo, params::AudioRibbon::smoothing_hi);

      NodeSeparator("transform");
      ModSlider("pos X", &n->posX, params::AudioRibbon::pos_x_lo, params::AudioRibbon::pos_x_hi);
      ModSlider("pos Y", &n->posY, params::AudioRibbon::pos_y_lo, params::AudioRibbon::pos_y_hi);
      ModSlider("pos Z", &n->posZ, params::AudioRibbon::pos_z_lo, params::AudioRibbon::pos_z_hi);
      ModSlider("rot X", &n->rotX, params::AudioRibbon::rot_x_lo, params::AudioRibbon::rot_x_hi);
      ModSlider("rot Y", &n->rotY, params::AudioRibbon::rot_y_lo, params::AudioRibbon::rot_y_hi);
      ModSlider("rot Z", &n->rotZ, params::AudioRibbon::rot_z_lo, params::AudioRibbon::rot_z_hi);
      ModSlider("scale X", &n->scaleX, params::AudioRibbon::scale_x_lo, params::AudioRibbon::scale_x_hi);
      ModSlider("scale Y", &n->scaleY, params::AudioRibbon::scale_y_lo, params::AudioRibbon::scale_y_hi);
      ModSlider("scale Z", &n->scaleZ, params::AudioRibbon::scale_z_lo, params::AudioRibbon::scale_z_hi);
      ModSlider("uniform scale", &n->uniformScale, params::AudioRibbon::uniform_scale_lo, params::AudioRibbon::uniform_scale_hi);

      NodeSeparator("material");
      DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; });
      ColorSwatch("colour", n->color, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
      ModSlider("roughness", &n->roughness, params::AudioRibbon::roughness_lo, params::AudioRibbon::roughness_hi);
      ModSlider("opacity", &n->opacity, 0.0f, 1.0f);
      ColorSwatch("emission", n->emissionColor, n);
      ModSlider("emission", &n->emission, params::AudioRibbon::emission_lo, params::AudioRibbon::emission_hi);
      ModSlider("ior", &n->ior, params::AudioRibbon::ior_lo, params::AudioRibbon::ior_hi);
   }


   void DrawSetColorParams(SetColorNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%s triangles", UiFmt::Count((long long)(n->TriangleCount())).c);

      DropdownButton("source", SetColorNode::SourceNames(), n->source, [n](int i) { n->source = i; });
      switch (n->source)
      {
      case SetColorNode::kFlat:
         ColorSwatch("colour", n->flatColor, n);
         break;
      case SetColorNode::kIndex:
         ColorSwatch("ramp start", n->rampA, n);
         ColorSwatch("ramp end", n->rampB, n);
         break;
      case SetColorNode::kRandom:
         ModSlider("seed", &n->seed, params::SetColor::seed_lo, params::SetColor::seed_hi, "%.0f", kParamWidth, false, 1.0f);
         break;
      case SetColorNode::kPalette:
         ModSliderInt("palette offset", &n->paletteOffset, params::SetColor::palette_offset_lo, params::SetColor::palette_offset_hi);
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


   void DrawInstanceParams(InstanceOnPointsNode* n)
   {
      if (n->pointSource != nullptr && n->instanceShape != nullptr)
         ImGui::TextDisabled("%s instances, %s triangles", UiFmt::Count((long long)(n->InstanceCount())).c, UiFmt::Count((long long)(n->TriangleCount())).c);

      DropdownButton("points from", InstanceOnPointsNode::SourceNames(), n->pointMode,
                     [n](int i) { n->pointMode = i; });
      ModSliderInt("max points", &n->maxPoints, params::InstanceOnPoints::max_points_lo, params::InstanceOnPoints::max_points_hi);
      ModSlider("scale", &n->instanceScale, params::InstanceOnPoints::instance_scale_lo, params::InstanceOnPoints::instance_scale_hi, "%.4f", kParamWidth, false, 0.0f, LogPosToValue, LogValueToPos);
      ModSlider("scale random", &n->scaleRandom, 0.0f, 1.0f);
      ModSlider("rotation random", &n->rotationRandom, 0.0f, 1.0f);
      ModSlider("normal offset", &n->normalOffset, params::InstanceOnPoints::normal_offset_lo, params::InstanceOnPoints::normal_offset_hi);
      ModCheckbox("align to normal", &n->alignToNormal);
      ModSlider("seed", &n->seed, params::InstanceOnPoints::seed_lo, params::InstanceOnPoints::seed_hi, "%.0f", kParamWidth, false, 1.0f);
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
                      false, 0.0f, nullptr, nullptr, Scene3DNode::kMotionParamBase + (it->motionSlot - 1) * 9 + c, full.c_str());   // ui-audit-ok: motion param id layout, 9 channels per slot
         }
      }
      gParamRegisterOnly = keptOnly;
   }

   void DrawCameraParams(CameraNode* n)
   {
      DropdownButton("projection", CameraNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      if (n->projection == 0)
         ModSlider("fov", &n->fov, params::Camera::fov_lo, params::Camera::fov_hi);
      else
         ModSlider("ortho height", &n->orthoHeight, params::Camera::ortho_height_lo, params::Camera::ortho_height_hi);
      ModSlider("distance", &n->distance, params::Camera::distance_lo, params::Camera::distance_hi);
      ModSlider("orbit", &n->azimuth, params::Camera::azimuth_lo, params::Camera::azimuth_hi, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->elevation, params::Camera::elevation_lo, params::Camera::elevation_hi, "%.1f\xC2\xB0");
      ModSlider("roll", &n->roll, params::Camera::roll_lo, params::Camera::roll_hi, "%.1f\xC2\xB0");
      ModSlider("orbit / beat", &n->orbitPerBeat, -1.0f, 1.0f);
      ModSlider("target x", &n->targetX, params::Camera::target_x_lo, params::Camera::target_x_hi);
      ModSlider("target y", &n->targetY, params::Camera::target_y_lo, params::Camera::target_y_hi);
      ModSlider("target z", &n->targetZ, params::Camera::target_z_lo, params::Camera::target_z_hi);
      ModSlider("near", &n->nearPlane, params::Camera::near_plane_lo, params::Camera::near_plane_hi);
      ModSlider("far", &n->farPlane, params::Camera::far_plane_lo, params::Camera::far_plane_hi);
   }


   void DrawLightParams(LightNode* n)
   {
      DropdownButton("type", LightNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      ModSlider("orbit", &n->azimuth, params::Light::azimuth_lo, params::Light::azimuth_hi, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->elevation, params::Light::elevation_lo, params::Light::elevation_hi, "%.1f\xC2\xB0");
      // Every row is always drawn; a row the chosen type does not use is greyed, so the node never changes size.
      ImGui::BeginDisabled(!(n->type == 1 || n->type == 4 || n->type == 5));
      ModSlider("distance", &n->distance, params::Light::distance_lo, params::Light::distance_hi);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(n->type != 4);
      ModSlider("cone angle", &n->spotAngle, params::Light::spot_angle_lo, params::Light::spot_angle_hi, "%.1f\xC2\xB0");
      ModSlider("penumbra", &n->spotPenumbra, 0.0f, 1.0f);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(n->type != 5);
      ModSlider("width", &n->areaWidth, params::Light::area_width_lo, params::Light::area_width_hi);
      ModSlider("height", &n->areaHeight, params::Light::area_height_lo, params::Light::area_height_hi);
      { static const std::vector<std::string> kShapes = { "Rectangle", "Disc" };
        DropdownButton("shape", kShapes, n->areaShape, [n](int i) { n->areaShape = i; }); }
      ModSlider("spread", &n->areaSpread, params::Light::area_spread_lo, params::Light::area_spread_hi, "%.0f\xC2\xB0");
      ImGui::EndDisabled();
      // Row 4.2: point and spot take an IES profile; the button names the file once one is set.
      ImGui::BeginDisabled(!(n->type == 1 || n->type == 4));
      {
         const bool has = !n->iesFile.empty();
         const std::string label = has ? AppPaths::FsPath(n->iesFile).filename().u8string() : std::string("IES profile...");
         if (ActionButton::Draw(label.c_str(), ImVec2(has ? kPreviewSize * tok::geometry_params_light_ratio : kPreviewSize, 0)))
         {
            const std::string path = Platform::OpenIesDialog();
            if (!path.empty()) { PushUndoCheckpoint(); n->iesFile = path; }
         }
         if (has)
         {
            ImGui::SameLine();
            if (ActionButton::Draw("Clear", ImVec2(kPreviewSize * tok::geometry_params_light_ratio_2, 0))) { PushUndoCheckpoint(); n->iesFile.clear(); }
         }
      }
      ImGui::EndDisabled();
      ColorSwatch("colour", n->color, n);
      ModCheckbox("temperature", &n->useTemperature);
      ImGui::BeginDisabled(!n->useTemperature);
      ModSlider("kelvin", &n->temperature, params::Light::temperature_lo, params::Light::temperature_hi, "%.0f K");
      ImGui::EndDisabled();
      ModSlider("intensity", &n->intensity, params::Light::intensity_lo, params::Light::intensity_hi);
      ModSlider("exposure", &n->exposure, params::Light::exposure_lo, params::Light::exposure_hi, "%.2f");   // white by +3, black by -6
      ImGui::BeginDisabled(!(n->type == 1 || n->type == 4 || n->type == 5));
      ModSlider("falloff", &n->falloff, params::Light::falloff_lo, params::Light::falloff_hi);
      ModCheckbox("cutoff", &n->useCutoff);
      ImGui::BeginDisabled(!n->useCutoff);
      ModSlider("cutoff distance", &n->cutoffDistance, params::Light::cutoff_distance_lo, params::Light::cutoff_distance_hi);
      ImGui::EndDisabled();
      ImGui::EndDisabled();
      ModSlider("diffuse", &n->diffuse, params::Light::diffuse_lo, params::Light::diffuse_hi);
      ModSlider("specular", &n->specular, params::Light::specular_lo, params::Light::specular_hi);
      ModSlider("orbit / beat", &n->orbitPerBeat, -1.0f, 1.0f);
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


   void DrawRender3DParams(Render3DNode* n)
   {
      const int connected = (int)n->FlattenedGeometry().size();
      ImGui::TextDisabled("%d geometry, %s camera", connected, n->camera ? "patched" : "built-in");
      ImGui::SameLine(0.0f, tok::space_4);
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
            ImGui::Dummy(ImVec2(fixedW - textSize.x, textSize.y));
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
      ImGui::BeginDisabled(n->renderPass != 0 || n->SolidActive() || n->PanoActive());
      DropdownButton("engine", Render3DNode::EngineNames(), n->engine,
                     [n](int i) { n->engine = i; }, colW);
      ImGui::EndDisabled();
      DropdownButton("pass", Render3DNode::RenderPassNames(), n->renderPass,
                     [n](int i) { n->renderPass = i; }, colW);
      // Row 7.3: depth, normal, albedo, mask and id as extra outputs beside the image.
      ImGui::BeginDisabled(n->renderPass != 0 || n->PanoActive());
      ModCheckbox("passes", &n->passes);
      ImGui::EndDisabled();
      if (n->passes && n->renderPass == 0)
         ModSliderInt("mask index", &n->passMask, params::Render3D::pass_mask_lo, params::Render3D::pass_mask_hi);
      if (n->renderPass >= 1 && n->renderPass <= 9)
         ModSlider("pass alpha threshold", &n->passThreshold, 0.0f, 1.0f, "%.2f", colW);
      if (n->renderPass == 5)
      {
         ModSlider("mist start", &n->mistStart, params::Render3D::mist_start_lo, params::Render3D::mist_start_hi, "%.2f", colW);
         ModSlider("mist depth", &n->mistDepth, params::Render3D::mist_depth_lo, params::Render3D::mist_depth_hi, "%.2f", colW);
         DropdownButton("mist falloff", Render3DNode::MistFalloffNames(), n->mistFalloff,
                        [n](int i) { n->mistFalloff = i; }, colW);
         ModSlider("mist intensity", &n->mistIntensity, 0.0f, 1.0f, "%.2f", colW);
         ModSlider("mist height", &n->mistHeight, params::Render3D::mist_height_lo, params::Render3D::mist_height_hi, "%.2f", colW);
      }
      // This is the export resolution: Output sizes its own buffer from whatever
      // its input hands it, so a 4000px PNG needs 4000 set here or it is an
      // upscale of a smaller render.
      ModSlider("width", &n->width, params::Render3D::width_lo, params::Render3D::width_hi, "%.0f", colW);
      ModSlider("height", &n->height, params::Render3D::height_lo, params::Render3D::height_hi, "%.0f", colW);
      ImGui::BeginDisabled(pt);
      DropdownButton("antialias", Render3DNode::SampleNames(), n->samples,
                     [n](int i) { n->samples = i; }, colW);
      ImGui::EndDisabled();
      // The requested sample count is clamped by both the driver and a memory
      // budget, so say what actually happened when they disagree.
      if (n->ActiveSamples() != wantedAa && ImGui::IsItemHovered())
         Tip::Show("antialias reduced to %dx at this size", n->ActiveSamples());
      DropdownButton("tonemap", Render3DNode::TonemapNames(), n->tonemap,
                     [n](int i) { n->tonemap = i; }, colW);
      ModSlider("exposure", &n->exposure, params::Render3D::exposure_lo, params::Render3D::exposure_hi, "%.3f", colW);
      // HDR lights the scene in float and tonemaps once at the end, so bloom
      // sees light above white. Only the beauty pass has light to keep.
      ImGui::BeginDisabled(n->renderPass != 0 || pt);
      ModCheckbox("hdr", &n->hdr);
      ImGui::EndDisabled();
      ColorSwatch("background", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f, "%.3f", colW);
      hideBegin(0, 0, {"16-bit float output"});
      ModCheckbox("16-bit float output", &n->floatOutput);
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
         ImGui::BeginDisabled(n->camera != nullptr);
         DropdownButton("projection", Render3DNode::ProjectionNames(), n->projection,
                        [n](int i) { n->projection = i; }, colW);
         // One slot, three meanings: the field of view for perspective, the view
         // height for orthographic, the dome's angle for a fisheye. A 360 or a
         // cubemap always sees everything, so fov stays, greyed.
         if (n->projection == Render3DNode::kOrthographic)
            ModSlider("ortho height", &n->orthoHeight, params::Render3D::ortho_height_lo, params::Render3D::ortho_height_hi, "%.3f", colW);
         else if (n->projection == Render3DNode::kFisheye)
            ModSlider("dome angle", &n->domeAngle, params::Render3D::dome_angle_lo, params::Render3D::dome_angle_hi, "%.0f\xC2\xB0", colW);
         else
         {
            ImGui::BeginDisabled(n->projection != Render3DNode::kPerspective);
            ModSlider("fov", &n->fov, params::Render3D::fov_lo, params::Render3D::fov_hi, "%.3f", colW);
            ImGui::EndDisabled();
         }
         hideBegin(1, 0, {}, n->projection == Render3DNode::kFisheye);
         ImGui::BeginDisabled(n->projection != Render3DNode::kFisheye);
         ModSlider("dome tilt", &n->domeTilt, params::Render3D::dome_tilt_lo, params::Render3D::dome_tilt_hi, "%.0f\xC2\xB0", colW);
         ImGui::EndDisabled();
         hideEnd();
         if (n->projection == Render3DNode::kFisheye)
            DropdownButton("lens", Render3DNode::FisheyeLensNames(), n->fisheyeLens,
                           [n](int i) { n->fisheyeLens = i; }, colW);
         if (n->projection == Render3DNode::kEquirect || n->projection == Render3DNode::kCylindrical)
         {
            ModSlider("min latitude", &n->latMin, params::Render3D::lat_min_lo, params::Render3D::lat_min_hi, "%.0f\xC2\xB0", colW);
            ModSlider("max latitude", &n->latMax, params::Render3D::lat_max_lo, params::Render3D::lat_max_hi, "%.0f\xC2\xB0", colW);
            ModSlider("min longitude", &n->lonMin, params::Render3D::lon_min_lo, params::Render3D::lon_min_hi, "%.0f\xC2\xB0", colW);
            ModSlider("max longitude", &n->lonMax, params::Render3D::lon_max_lo, params::Render3D::lon_max_hi, "%.0f\xC2\xB0", colW);
         }
         ModSlider("distance", &n->camDistance, params::Render3D::cam_distance_lo, params::Render3D::cam_distance_hi, "%.3f", colW);
         ModSlider("orbit", &n->camAzimuth, params::Render3D::cam_azimuth_lo, params::Render3D::cam_azimuth_hi, "%.1f\xC2\xB0", colW);
         ModSlider("elevation", &n->camElevation, params::Render3D::cam_elevation_lo, params::Render3D::cam_elevation_hi, "%.1f\xC2\xB0", colW);
         ModSlider("target x", &n->targetX, params::Render3D::target_x_lo, params::Render3D::target_x_hi, "%.3f", colW);
         ModSlider("target y", &n->targetY, params::Render3D::target_y_lo, params::Render3D::target_y_hi, "%.3f", colW);
         ModSlider("target z", &n->targetZ, params::Render3D::target_z_lo, params::Render3D::target_z_hi, "%.3f", colW);
         ImGui::EndDisabled();
      }
      });

      sections.push_back([&]() {
      hideBegin(0, 0, {"sprite shape", "sprite size"});
      if (!gParamRegisterOnly)
         NodeSeparator("points", colW);
      DropdownButton("sprite shape", Render3DNode::SpriteShapeNames(), n->spriteShape,
                     [n](int i) { n->spriteShape = i; }, colW);
      DropdownButton("sprite size", Render3DNode::SpriteSizeModeNames(), n->spriteSizeMode,
                     [n](int i) { n->spriteSizeMode = i; }, colW);
      hideEnd();
      });

      sections.push_back([&]() {
      NodeSeparator("raster", colW);
      ImGui::BeginDisabled(pt);
      ModCheckbox("depth test", &n->depthTest);
      ModCheckbox("cull backfaces", &n->backfaceCull);
      ImGui::EndDisabled();
      });

      sections.push_back([&]() {
      int patchedLights = 0;
      for (int i = 0; i < Render3DNode::kLightSlots; i++)
         if (n->lights[i] != nullptr)
            patchedLights++;
      snprintf(label, sizeof(label), patchedLights > 0 ? "light - %d from Light nodes" : "light", patchedLights);
      NodeSeparator(label, colW);
      ImGui::BeginDisabled(patchedLights > 0);
      ModSlider("light orbit", &n->lightAzimuth, params::Render3D::light_azimuth_lo, params::Render3D::light_azimuth_hi, "%.1f\xC2\xB0", colW);
      ModSlider("light height", &n->lightElevation, params::Render3D::light_elevation_lo, params::Render3D::light_elevation_hi, "%.1f\xC2\xB0", colW);
      ColorSwatch("light", n->lightColor, n);
      ModSlider("intensity", &n->lightIntensity, params::Render3D::light_intensity_lo, params::Render3D::light_intensity_hi, "%.3f", colW);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(pt);
      ColorSwatch("ambient", n->ambientColor, n);
      ModSlider("rim", &n->rimIntensity, params::Render3D::rim_intensity_lo, params::Render3D::rim_intensity_hi, "%.3f", colW);
      ImGui::EndDisabled();
      });

      sections.push_back([&]() {
      NodeSeparator("shadows", colW);
      ImGui::BeginDisabled(pt);   // every traced light casts its own shadow
      ModCheckbox("cast shadows", &n->shadowsEnabled);
      ImGui::BeginDisabled(!n->shadowsEnabled);
      hideBegin(0, 0, {"resolution"});
      DropdownButton("resolution", Render3DNode::ShadowQualityNames(), n->shadowQuality,
                     [n](int i) { n->shadowQuality = i; }, colW);
      hideEnd();
      ModSlider("strength", &n->shadowStrength, 0.0f, 1.0f, "%.3f", colW);
      hideBegin(2, 0, {});
      ModSlider("softness", &n->shadowSoftness, params::Render3D::shadow_softness_lo, params::Render3D::shadow_softness_hi, "%.3f", colW);
      ModSlider("bias", &n->shadowBias, params::Render3D::shadow_bias_lo, params::Render3D::shadow_bias_hi, "%.4f", colW);
      hideEnd();
      ImGui::EndDisabled();
      ImGui::EndDisabled();
      });

      sections.push_back([&]() {
      const bool envConnected = n->envInput.IsConnected();
      NodeSeparator(envConnected ? "environment - from an HDRI node" : "environment", colW);
      ModCheckbox("use as background", &n->envAsBackground);
      ImGui::BeginDisabled(envConnected);
      ColorSwatch("sky", n->envSky, n);
      ColorSwatch("horizon", n->envHorizon, n);
      ColorSwatch("ground", n->envGround, n);
      ImGui::EndDisabled();
      ModSlider("env intensity", &n->envIntensity, params::Render3D::env_intensity_lo, params::Render3D::env_intensity_hi, "%.3f", colW);
      });

      sections.push_back([&]() {
      NodeSeparator("quality", colW);
      ImGui::BeginDisabled(pt);
      ModSlider("ambient occlusion", &n->aoStrength, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();
      hideBegin(1, 0, {});
      ImGui::BeginDisabled(n->aoStrength <= 0.0f || pt);
      ModSlider("occlusion radius", &n->aoRadius, params::Render3D::ao_radius_lo, params::Render3D::ao_radius_hi, "%.3f", colW);
      ImGui::EndDisabled();
      hideEnd();
      DropdownButton("shading", Render3DNode::ViewShadingNames(), n->viewShading,
                     [n](int i) { n->viewShading = i; }, colW);
      if (n->viewShading == 1)
      {
         DropdownButton("solid lighting", Render3DNode::SolidLightingNames(), n->solidLighting,
                        [n](int i) { n->solidLighting = i; }, colW);
         if (n->solidLighting == 1)
            DropdownButton("matcap", Render3DNode::MatcapNames(), n->matcap,
                           [n](int i) { n->matcap = i; }, colW);
         if (n->solidLighting == 0)
         {
            ModCheckbox("studio world space", &n->studioWorld);
            ImGui::BeginDisabled(!n->studioWorld);
            ModSlider("studio rotation", &n->studioRotation, params::Render3D::studio_rotation_lo, params::Render3D::studio_rotation_hi, "%.0f", colW);
            ImGui::EndDisabled();
            ModCheckbox("solid specular", &n->solidSpecular);
         }
         DropdownButton("solid color", Render3DNode::SolidColorNames(), n->solidColor,
                        [n](int i) { n->solidColor = i; }, colW);
         if (n->solidColor == 2)
            ColorSwatch("single color", n->solidSingle, n);
      }
      hideBegin(5, 0, {"cavity", "cavity type"});
      ModCheckbox("cavity", &n->cavity);
      ImGui::BeginDisabled(!n->cavity);
      DropdownButton("cavity type", Render3DNode::CavityTypeNames(), n->cavityType,
                     [n](int i) { n->cavityType = i; }, colW);
      ModSlider("cavity ridge", &n->cavityRidge, params::Render3D::cavity_ridge_lo, params::Render3D::cavity_ridge_hi, "%.2f", colW);
      ModSlider("cavity valley", &n->cavityValley, params::Render3D::cavity_valley_lo, params::Render3D::cavity_valley_hi, "%.2f", colW);
      ModSlider("cavity distance", &n->cavityDistance, params::Render3D::cavity_distance_lo, params::Render3D::cavity_distance_hi, "%.2f", colW);
      ModSlider("curvature ridge", &n->curvatureRidge, params::Render3D::curvature_ridge_lo, params::Render3D::curvature_ridge_hi, "%.2f", colW);
      ModSlider("curvature valley", &n->curvatureValley, params::Render3D::curvature_valley_lo, params::Render3D::curvature_valley_hi, "%.2f", colW);
      ImGui::EndDisabled();
      hideEnd();
      ModSlider("fog", &n->fogDensity, params::Render3D::fog_density_lo, params::Render3D::fog_density_hi, "%.3f", colW);
      if (n->fogDensity > 0.0f)
      {
         ColorSwatch("fog colour", n->fogColor, n);
         ModSlider("fog anisotropy", &n->fogAnisotropy, params::Render3D::fog_anisotropy_lo, params::Render3D::fog_anisotropy_hi, "%.2f", colW);
         ModSlider("fog falloff", &n->fogFalloff, 0.0f, 1.0f, "%.2f", colW);
         ImGui::SetNextItemWidth(colW); ImGui::DragFloat3("fog min", n->fogMin, params::geometry_params::fog_min_speed, params::geometry_params::fog_min_lo, params::geometry_params::fog_min_hi, "%.2f");
         ImGui::SetNextItemWidth(colW); ImGui::DragFloat3("fog max", n->fogMax, params::geometry_params::fog_max_speed, params::geometry_params::fog_max_lo, params::geometry_params::fog_max_hi, "%.2f");
      }
      ImGui::BeginDisabled(n->PanoActive()); // a panorama has no focal plane
      ModSlider("depth of field", &n->dofAperture, params::Render3D::dof_aperture_lo, params::Render3D::dof_aperture_hi, "%.3f", colW);   // 16: a 100 mm macro at f/2.8
      ImGui::EndDisabled();
      hideBegin(8, 0, {});   // focus distance, aperture x3, cat eye, distortion, CA, offline supersample
      ImGui::BeginDisabled(n->dofAperture <= 0.0f || n->PanoActive());
      ModSlider("focus distance", &n->focusDistance, params::Render3D::focus_distance_lo, params::Render3D::focus_distance_hi, "%.3f", colW);
      ModSliderInt("aperture blades", &n->dofBlades, params::Render3D::dof_blades_lo, params::Render3D::dof_blades_hi, colW);
      ModSlider("aperture rotation", &n->dofRotation, params::Render3D::dof_rotation_lo, params::Render3D::dof_rotation_hi, "%.0f\xC2\xB0", colW);
      ModSlider("aperture ratio", &n->dofRatio, params::Render3D::dof_ratio_lo, params::Render3D::dof_ratio_hi, "%.2f", colW);
      ModSlider("cat eye", &n->dofCatEye, 0.0f, 1.0f, "%.2f", colW);
      ImGui::EndDisabled();
      ModSlider("lens distortion", &n->lensDistortion, params::Render3D::lens_distortion_lo, params::Render3D::lens_distortion_hi, "%.3f", colW);
      ModSlider("chromatic aberration", &n->lensCA, params::Render3D::lens_ca_lo, params::Render3D::lens_ca_hi, "%.4f", colW);
      ImGui::BeginDisabled(pt);   // path trace takes its samples from the path trace section
      ModSliderInt("offline supersample", &n->supersample, params::Render3D::supersample_lo, params::Render3D::supersample_hi, colW);
      ImGui::EndDisabled();
      hideEnd();
      ImGui::BeginDisabled(!n->LiveActive() && (!n->HdrActive() || pt)); // the live engine blooms its own picture (row 6.3)
      ModSlider("bloom", &n->bloom, 0.0f, 1.0f, "%.3f", colW);
      hideBegin(2, 0, {});
      ImGui::BeginDisabled(n->bloom <= 0.0f);
      ModSlider("bloom threshold", &n->bloomThreshold, params::Render3D::bloom_threshold_lo, params::Render3D::bloom_threshold_hi, "%.2f", colW);
      ModSlider("bloom radius", &n->bloomRadius, 0.0f, 1.0f, "%.3f", colW);
      ImGui::EndDisabled();
      hideEnd();
      ImGui::EndDisabled();
      ImGui::BeginDisabled(n->PanoActive() || pt); // a panorama has no previous frame to smear from
      ModCheckbox("motion blur", &n->motionBlur);
      ImGui::BeginDisabled(!n->motionBlur);
      hideBegin(2, 0, {"motion position"});
      DropdownButton("motion position", Render3DNode::MotionPositionNames(), n->motionPosition,
                     [n](int i) { n->motionPosition = i; }, colW);
      ModSlider("motion shutter", &n->motionShutter, params::Render3D::motion_shutter_lo, params::Render3D::motion_shutter_hi, "%.2f", colW);
      ModSlider("motion max blur", &n->motionMax, params::Render3D::motion_max_lo, params::Render3D::motion_max_hi, "%.0f", colW);
      hideEnd();
      ImGui::EndDisabled();
      ImGui::EndDisabled();
      });

      // Last, so the two new sliders take the next ordinals and no earlier one moves.
      sections.push_back([&]() {
      NodeSeparator("path trace", colW);
      ImGui::BeginDisabled(!pt);
      ModSliderInt("samples", &n->ptSamples, params::Render3D::pt_samples_lo, params::Render3D::pt_samples_hi, colW);
      ModSliderInt("bounces", &n->ptBounces, params::Render3D::pt_bounces_lo, params::Render3D::pt_bounces_hi, colW);
      ImGui::EndDisabled();
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
            ImGui::SameLine(0.0f, gutter);
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
      DropdownButton("fit", LayerStackNode::FitNames(), n->fit, [n](int i) { PushUndoCheckpoint(); n->fit = i; });

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
                              dragging ? tok::Role(tok::col::surface_lo_2, tok::geometry_params_layer_stack_surface_lo_2_alpha_2) : tok::Role(tok::col::edge_lo, tok::geometry_params_layer_stack_edge_lo_alpha_2), tok::geometry_params_layer_stack_radius);
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
         dl->AddText(ImVec2(headerPos.x + tok::geometry_params_layer_stack_gap_2, headerPos.y + tok::space_0_5),
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
            const float threshold = rowHeight * 3.0f; // one full layer block  // ui-audit-ok: three rows (a layer block)
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

      ModCheckbox("linear light", &n->linear);
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
               const float cellW = std::floor((kParamWidth - tok::geometry_params_filter_ratio * gap) / tok::geometry_params_filter_ratio_2);
               for (size_t k = 0; k < 9; k++)
               {
                  const FilterParamDef& kp = def.params[k];
                  ImGui::PushID((int)k);
                  if (k % 3 != 0)
                     ImGui::SameLine(0.0f, gap);
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
         ImGui::BeginDisabled(unused && !gParamRegisterOnly);
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
         ImGui::EndDisabled();
      }
      gParamRegisterOnly = savedRegisterOnly;
      if (def.params.empty())
         ImGui::TextDisabled("(no parameters)");
   }
}

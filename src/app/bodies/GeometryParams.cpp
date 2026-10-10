// Geometry / 3D / material / render parameter bodies (moved verbatim from main.cpp).
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/PinDot.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   inline const std::vector<std::string> kAxisXYZ = { "x", "y", "z" };

   void DrawAudioFileParams(AudioFileNode* n)
   {
      if (ActionButton::Draw("Choose audio...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!n->FileName().empty())
         ImGui::TextDisabled("%s", n->FileName().c_str());
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      if (n->IsLoaded())
      {
         const double dur = std::max(0.001, n->Duration());
         ImGui::ProgressBar((float)(n->Position() / dur), ImVec2(kPreviewSize, 0));
         ImGui::TextDisabled("%.1f / %.1f s", n->Position(), dur);

         if (n->IsPlaying())
         {
            if (ActionButton::Draw("Pause", ImVec2(kPreviewSize * 0.48f, 0)))
               n->Pause();
         }
         else if (ActionButton::Draw("Play", ImVec2(kPreviewSize * 0.48f, 0)))
         {
            n->Play();
         }
         ImGui::SameLine();
         if (ActionButton::Draw("Restart", ImVec2(kPreviewSize * 0.48f, 0)))
            n->Restart();

         ModCheckbox("follow transport", &n->followTransport);
         ModCheckbox("loop", &n->loop);
         ModCheckbox("audible", &n->monitor);
         ModSlider("volume", &n->volume, 0.0f, 1.0f);
         ModSlider("gain", &n->gain, 0.1f, 16.0f);
         ModSlider("pitch", &n->pitch, -24.0f, 24.0f);

         const Platform::AudioLevels& lv = n->Levels();
         ImGui::Text("level "); ImGui::SameLine();
         ImGui::ProgressBar(std::min(1.0f, lv.rms * n->gain), ImVec2(kPreviewSize * 0.6f, 0), "");
      }
   }


   void DrawAudioAnalyzeParams(AudioAnalyzeNode* n)
   {
      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = 16.0f;
      const float bodyW = colW * 2 + gutter;

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
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bodyW);
         ImGui::TextColored(tok::V4(tok::palf::v_500_900_1000_1000), "source: %s", srcName);
         ImGui::PopTextWrapPos();
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
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bodyW);
         ImGui::TextDisabled("%s", n->Status().c_str());
         ImGui::PopTextWrapPos();
      }

      // spectrum, so it is obvious whether audio is actually arriving
      const Platform::AudioLevels& levels = n->Levels();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 60.0f;
      ImGui::Dummy(ImVec2(bodyW, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + bodyW, origin.y + h), tok::U32(tok::pal::c_101016FF), 3.0f);
      const float bw = bodyW / (float)Platform::kAudioBands;
      for (int i = 0; i < Platform::kAudioBands; i++)
      {
         const float v = std::min(1.0f, levels.bands[i] * n->gain);
         dl->AddRectFilled(ImVec2(origin.x + i * bw + 1, origin.y + h - v * h),
                           ImVec2(origin.x + (i + 1) * bw - 1, origin.y + h),
                           tok::U32(tok::pal::c_78C8FFEB));
      }
      if (!n->input.IsConnected() && !Platform::AudioIsRunning())
         AudioViz::IdleLabel(AudioViz::Frame{dl, origin, ImVec2(origin.x + bodyW, origin.y + h)}, "idle");
      dl->AddRect(origin, ImVec2(origin.x + bodyW, origin.y + h), tok::U32(tok::pal::c_464A5AFF), 3.0f);

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("input", colW);
      ModSlider("gain", &n->gain, 0.1f, 16.0f, "%.3f", colW);
      ModSlider("onset hold", &n->onsetHold, 0.02f, 1.0f, "%.3f", colW);

      ImGui::EndGroup();

      // --- Right Column ---
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("envelope", colW);
      ModSlider("attack", &n->attack, 0.02f, 1.0f, "%.3f", colW);
      ModSlider("release", &n->release, 0.005f, 1.0f, "%.3f", colW);

      ImGui::EndGroup();

      NodeSeparator("outputs", bodyW);
      DrawOutputMeters(n, colW, 2, gutter);
   }


   void DrawPathParams(PathNode* n)
   {
      if (n->IsFollowing())
         ImGui::TextDisabled("following %zu points", n->FollowPointCount());
      else
         DropdownButton("shape", PathNode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      float p[3];
      n->CurrentPoint(p);
      ImGui::TextDisabled("t %.2f   (%.2f, %.2f, %.2f)", n->Progress(), p[0], p[1], p[2]);

      NodeSeparator("motion");
      ModSlider("speed / beat", &n->speed, -2.0f, 2.0f);
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
            ModSlider("slice at", &n->slicePosition, -3.0f, 3.0f);
         }
         ModSliderInt("contour", &n->contourIndex, 0, 8);
      }

      NodeSeparator("shape");
      ModSlider("size x", &n->sizeX, 0.0f, 3.0f);
      ModSlider("size y", &n->sizeY, 0.0f, 3.0f);
      ModSlider("size z", &n->sizeZ, 0.0f, 3.0f);
      if (n->shape == PathNode::kHelix || n->shape == PathNode::kSpiral)
         ModSlider("turns", &n->turns, 0.5f, 12.0f);
      if (n->shape == PathNode::kLissajous)
      {
         ModSliderInt("ratio a", &n->lissajousA, 1, 9);
         ModSliderInt("ratio b", &n->lissajousB, 1, 9);
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
      const float cellW = kPreviewSize / 3.0f;
      const float cellH = 34.0f;
      const float gridW = cellW * 3.0f;
      const float gridH = cellH * (float)rowCount;

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool isLight = IsThemeLight();
      const ImU32 borderCol = isLight ? tok::U32(tok::pal::c_3C44558C) : tok::U32(tok::pal::c_D2DAEB46);
      const ImU32 textCol = isLight ? tok::U32(tok::pal::c_232834FF) : tok::U32(tok::pal::c_DEE4F0FF);

      dl->AddRect(origin, ImVec2(origin.x + gridW, origin.y + gridH), borderCol, 8.0f, 0, 1.5f);
      for (int r = 1; r < rowCount; r++)
      {
         const float y = origin.y + cellH * (float)r;
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + gridW, y), borderCol, 1.0f);
      }
      for (int c = 1; c < 3; c++)
      {
         const float x = origin.x + cellW * (float)c;
         dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + gridH), borderCol, 1.0f);
      }

      for (int r = 0; r < rowCount; r++)
      {
         for (int a = 0; a < 3; a++)
         {
            const ImVec2 cellMin(origin.x + cellW * (float)a, origin.y + cellH * (float)r);
            const ImVec2 cellMax(cellMin.x + cellW, cellMin.y + cellH);
            const int outputIndex = 4 + r * 3 + a;

            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", n->DisplayValue(outputIndex));
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
            ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
            const ImVec2 pinCenter(cellMax.x - 9.0f, cellMax.y - 9.0f);
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
         ImGui::TextDisabled("following %d points", n->SampleCount());
      else
         ImGui::TextDisabled("no geometry");

      NodeSeparator("sampling");
      DropdownButton("mode", GeometryTableNode::SampleModeNames(), n->sampleMode,
                     [n](int i) { n->sampleMode = i; });
      ModSliderInt("rows", &n->rows, 1, 16);
      DropdownButton("sort", GeometryTableNode::SortModeNames(), n->sortMode,
                     [n](int i) { n->sortMode = i; });
      ModSlider("offset", &n->offset, 0.0f, 1.0f);

      if (n->sampleMode == GeometryTableNode::kContour)
      {
         DropdownButton("axis", kAxisXYZ, n->sliceAxis, [n](int i) { PushUndoCheckpoint(); n->sliceAxis = i; }, kParamWidth, true, true);
         ModSlider("slice at", &n->slicePosition, -3.0f, 3.0f);
      }
      if (n->sampleMode == GeometryTableNode::kScatter)
         ModSlider("seed", &n->seed, 0.0f, 32.0f, "%.0f", kParamWidth, false, 1.0f);

      NodeSeparator("range");
      DropdownButton("space", GeometryTableNode::SpaceNames(), n->space,
                     [n](int i) { n->space = i; });
      if (n->space == GeometryTableNode::kSpaceFixed)
         ModSlider("extent", &n->extent, 0.1f, 10.0f);
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
      ModSliderInt("resolution", &n->resolution, 8, 300);
      ModSlider("size", &n->size, 0.5f, 20.0f);

      NodeSeparator("waves");
      ModSlider("amplitude", &n->amplitude, 0.0f, 1.0f);
      ModSlider("wavelength", &n->wavelength, 0.1f, 8.0f);
      ModSlider("steepness", &n->steepness, 0.0f, 2.0f);
      ModSlider("choppiness", &n->choppiness, 0.0f, 2.0f);
      ModSlider("direction", &n->direction, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSliderInt("octaves", &n->octaves, 1, 8);
      ModSlider("speed", &n->speed, -3.0f, 3.0f);
   }


   void DrawCurveParams(CurveNode* n)
   {
      ImGui::TextDisabled("%zu points, %zu triangles", n->PointCount(), n->TriangleCount());
      DropdownButton("type", CurveNode::KindNames(), n->kind, [n](int i) { n->kind = i; });
      DropdownButton("preset", CurveNode::PresetNames(), n->preset, [n](int i) { n->preset = i; });

      NodeSeparator("shape");
      ModSliderInt("points", &n->pointCount, 2, CurveNode::kMaxPoints);
      ModSliderInt("smoothness", &n->segments, 1, 64);
      ModCheckbox("closed", &n->closed);
      ModSlider("spread", &n->spread, 0.0f, 4.0f);
      ModSlider("height", &n->height, -3.0f, 3.0f);
      ModSlider("twist", &n->twist, -180.0f, 180.0f, "%.1f\xC2\xB0");
      if (n->preset == 3)
         ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);

      NodeSeparator("tube");
      ModSlider("radius", &n->radius, 0.0f, 0.5f);
      ModSliderInt("sides", &n->sides, 3, 32);
      ModSlider("taper", &n->taper, 0.0f, 1.0f);
   }


   void DrawMetaBallParams(MetaBallNode* n)
   {
      ImGui::TextDisabled("%zu balls, %zu triangles", n->BallCount(), n->TriangleCount());
      NodeSeparator("field");
      if (n->cloudSource == nullptr || n->cloudSource->GetPointCloud() == nullptr)
      {
         ModSliderInt("balls", &n->ballCount, 1, MetaBallNode::kMaxBalls);
         ModSlider("spread", &n->spread, 0.0f, 2.0f);
         ModSlider("orbit / beat", &n->spin, -1.0f, 1.0f);
      }
      else
      {
         ModSliderInt("max from cloud", &n->maxFromCloud, 1, 64);
      }
      ModSlider("radius", &n->radius, 0.05f, 1.5f);
      ModSlider("threshold", &n->threshold, 0.5f, 40.0f);
      ModSlider("bounds", &n->bounds, 0.5f, 6.0f);
      ModSliderInt("resolution", &n->resolution, 8, 96);
   }


   void DrawJoinGeometryParams(JoinGeometryNode* n)
   {
      ImGui::TextDisabled("%d inputs, %zu triangles", n->ConnectedCount(), n->TriangleCount());
      DropdownButton("mode", JoinGeometryNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      // A merged mesh is one draw call, so it can only wear one material -
      // picked from an input rather than authored here, since editing colour
      // and shading now lives on the dedicated Material node.
      ModSliderInt("material from input", &n->materialFrom, 0, JoinGeometryNode::kSlots - 1);
   }


   void DrawSwitcher3DParams(Switcher3DNode* n)
   {
      DropdownButton("unit", Switcher3DNode::UnitNames(), n->unit, [n](int i) { n->unit = i; });
      ModSlider("every", &n->interval, 0.05f, 32.0f);
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
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
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
            ImGui::TextDisabled("radius %.3f (from target)", n->ResolvedRadius());
            ModSlider("radius scale", &n->radiusScale, 0.05f, 3.0f);
         }
         else
         {
            ModSlider("radius", &n->radiusOverride, 0.05f, 5.0f);
         }
         ModCheckbox("fit around", &n->fitAround);
      }
      ModSlider("offset", &n->offset, -0.5f, 0.5f);
      ModSlider("blend", &n->blend, 0.0f, 1.0f);
      ModCheckbox("flat shade", &n->flatShade);
      ModCheckbox("flip normals", &n->flipNormals);
   }


   void DrawClothParams(ClothNode* n)
   {
      ImGui::TextDisabled("%zu triangles, %zu links", n->TriangleCount(), n->ConstraintCount());
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("solver");
      DropdownButton("pinned", ClothNode::PinModeNames(), n->pinMode, [n](int i) { n->pinMode = i; });
      ModSlider("stiffness", &n->stiffness, 0.0f, 1.0f);
      ModSliderInt("iterations", &n->iterations, 1, 40);
      ModSlider("damping", &n->damping, 0.0f, 0.5f);
      ModSlider("mass", &n->mass, 0.05f, 10.0f);
      ModSlider("hold shape", &n->shapeRetention, 0.0f, 1.0f);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, -20.0f, 20.0f);
      ModSlider("gravity y", &n->gravityY, -20.0f, 20.0f);
      ModSlider("gravity z", &n->gravityZ, -20.0f, 20.0f);
      ModSlider("wind x", &n->windX, -20.0f, 20.0f);
      ModSlider("wind y", &n->windY, -20.0f, 20.0f);
      ModSlider("wind z", &n->windZ, -20.0f, 20.0f);
      ModSlider("turbulence", &n->windTurbulence, 0.0f, 20.0f);

      NodeSeparator("ground");
      ModCheckbox("collide with ground", &n->groundEnabled);
      if (n->groundEnabled)
      {
         ModSlider("height", &n->groundHeight, -5.0f, 5.0f);
         ModSlider("bounce", &n->bounce, 0.0f, 1.0f);
         ModSlider("friction", &n->friction, 0.0f, 1.0f);
      }
   }


   void DrawParticleSystemParams(ParticleSystemNode* n)
   {
      ImGui::TextDisabled("%zu alive", n->AliveCount());
      if (ActionButton::Column("Reset", kParamWidth))
         n->Reset();

      NodeSeparator("emitter");
      DropdownButton("shape", ParticleSystemNode::EmitShapeNames(), n->emitShape,
                     [n](int i) { n->emitShape = i; });
      ModSliderInt("max particles", &n->maxParticles, 16, 50000);
      ModSlider("rate / sec", &n->emitRate, 0.0f, 3000.0f);
      ModSlider("radius", &n->emitRadius, 0.0f, 3.0f);
      ModSlider("lifetime", &n->lifetime, 0.1f, 20.0f);
      ModSlider("life random", &n->lifetimeRandom, 0.0f, 1.0f);

      NodeSeparator("launch");
      ModSlider("speed", &n->initialSpeed, 0.0f, 10.0f);
      ModSlider("speed random", &n->speedRandom, 0.0f, 1.0f);
      ModSlider("spread", &n->spread, 0.0f, 1.0f);
      ModSlider("dir x", &n->dirX, -1.0f, 1.0f);
      ModSlider("dir y", &n->dirY, -1.0f, 1.0f);
      ModSlider("dir z", &n->dirZ, -1.0f, 1.0f);

      NodeSeparator("forces");
      ModSlider("gravity x", &n->gravityX, -10.0f, 10.0f);
      ModSlider("gravity y", &n->gravityY, -10.0f, 10.0f);
      ModSlider("gravity z", &n->gravityZ, -10.0f, 10.0f);
      ModSlider("drag", &n->drag, 0.0f, 5.0f);
      ModSlider("turbulence", &n->turbulence, 0.0f, 10.0f);
      ModSlider("turb scale", &n->turbulenceScale, 0.1f, 8.0f);

      NodeSeparator("over life");
      ModSlider("start size", &n->startSize, 0.0f, 0.5f);
      ModSlider("end size", &n->endSize, 0.0f, 0.5f);
      ColorSwatch("start colour", n->startColor, n);
      ColorSwatch("end colour", n->endColor, n);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);
   }


   void DrawMaterialParams(MaterialNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      if (n->MapInput(kMapNormal).IsConnected())
      {
         ModCheckbox("bump (height map)##matNormalBump", &n->normalBump);
         ModSlider("normal strength", &n->normalStrength, 0.0f, 4.0f);
      }

      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = 16.0f;

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("surface", colW);
      DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; }, colW);
      DropdownButton("wrap", MaterialNode::WrapModeNames(), n->wrapMode, [n](int i) { n->wrapMode = i; }, colW);
      ColorSwatch("colour", n->color, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f, "%.3f", colW);
      ModSlider("roughness", &n->roughness, 0.02f, 1.0f, "%.3f", colW);
      ModSlider("specular", &n->specular, 0.0f, 1.0f, "%.3f", colW);
      ModSlider("ior", &n->ior, 1.0f, 3.0f, "%.3f", colW);
      ModSlider("opacity", &n->opacity, 0.0f, 1.0f, "%.3f", colW);
      ModCheckbox("texture alpha", &n->textureAlpha);
      if (n->opacity < 1.0f || n->alphaCutoff > 0.0f || n->textureAlpha)
         ModSlider("alpha cutoff", &n->alphaCutoff, 0.0f, 1.0f, "%.3f", colW);

      ImGui::EndGroup();

      // --- Right Column ---
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("emission", colW);
      ColorSwatch("emission", n->emissionColor, n);
      ModSlider("emission", &n->emission, 0.0f, 8.0f, "%.3f", colW);

      NodeSeparator("coat & sheen", colW);
      ModSlider("clearcoat", &n->clearcoat, 0.0f, 1.0f, "%.3f", colW);
      if (n->clearcoat > 0.0f || n->MapInput(kMapClearcoat).IsConnected())
         ModSlider("coat roughness", &n->clearcoatRoughness, 0.02f, 1.0f, "%.3f", colW);
      ModSlider("sheen", &n->sheen, 0.0f, 1.0f, "%.3f", colW);
      if (n->sheen > 0.0f || n->MapInput(kMapSheen).IsConnected())
      {
         ColorSwatch("sheen tint", n->sheenColor, n);
         ModSlider("sheen roughness", &n->sheenRoughness, 0.01f, 1.0f, "%.3f", colW);
      }

      NodeSeparator("optics", colW);
      ModSlider("anisotropy", &n->anisotropy, -1.0f, 1.0f, "%.3f", colW);
      if (n->anisotropy != 0.0f)
         ModSlider("aniso rotation", &n->anisotropyRotation, 0.0f, 1.0f, "%.3f", colW);
      ModSlider("iridescence", &n->iridescence, 0.0f, 1.0f, "%.3f", colW);
      if (n->iridescence > 0.0f)
      {
         ModSlider("irid IOR", &n->iridescenceIor, 1.0f, 3.0f, "%.3f", colW);
         ModSlider("irid thickness", &n->iridescenceThickness, 100.0f, 1000.0f, "%.0f nm", colW);
      }
      ModSlider("transmission", &n->transmission, 0.0f, 1.0f, "%.3f", colW);
      if (n->transmission > 0.0f)
      {
         ModSlider("trans rough", &n->transmissionRoughness, 0.0f, 1.0f, "%.3f", colW);
         ModSlider("dispersion", &n->dispersion, 0.0f, 1.0f, "%.3f", colW);
      }
      ModSlider("subsurface", &n->subsurface, 0.0f, 1.0f, "%.3f", colW);
      if (n->subsurface > 0.0f)
      {
         ColorSwatch("sss colour", n->subsurfaceColor, n);
         ModSlider("sss radius", &n->subsurfaceRadius, 0.0f, 1.0f, "%.3f", colW);
      }

      ImGui::EndGroup();
   }


   void DrawMappingParams(MappingNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      DropdownButton("space", MappingNode::SpaceNames(), n->space, [n](int i) { n->space = i; });
      if (n->space != kMapSpaceUv)
         ModSlider("blend softness", &n->triplanarBlend, 0.0f, 1.0f);

      NodeSeparator("translate");
      ModSlider("x", &n->translateX, -4.0f, 4.0f);
      ModSlider("y", &n->translateY, -4.0f, 4.0f);
      if (n->space != kMapSpaceUv)
         ModSlider("z", &n->translateZ, -4.0f, 4.0f);

      NodeSeparator("rotate");
      if (n->space == kMapSpaceUv)
      {
         ModSlider("z", &n->rotateZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
      }
      else
      {
         ModSlider("x", &n->rotateX, -180.0f, 180.0f, "%.1f\xC2\xB0");
         ModSlider("y", &n->rotateY, -180.0f, 180.0f, "%.1f\xC2\xB0");
         ModSlider("z", &n->rotateZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
      }

      NodeSeparator("scale");
      ModSlider("x", &n->scaleX, 0.05f, 8.0f);
      ModSlider("y", &n->scaleY, 0.05f, 8.0f);
      if (n->space != kMapSpaceUv)
         ModSlider("z", &n->scaleZ, 0.05f, 8.0f);
   }


   void DrawMeshResynthParams(MeshResynthNode* n)
   {
      ImGui::TextDisabled("generation %d, %zu triangles", n->Generation(), n->TriangleCount());
      if (ActionButton::Draw("step")) n->StepOnce();
      ImGui::SameLine();
      if (ActionButton::Draw("reset")) n->Reset();
      ImGui::SameLine();
      if (ActionButton::Draw("randomise")) n->Randomise();

      NodeSeparator("evolve");
      ModSlider("chaos", &n->chaos, 0.0f, 1.5f);
      ModCheckbox("auto step", &n->autoStep);
      if (n->autoStep)
         ModSlider("steps per beat", &n->stepsPerBeat, 0.05f, 8.0f);
      ModSlider("seed", &n->seed, 0.0f, 1000.0f, "%.0f", kParamWidth, false, 1.0f);
      ModSliderInt("triangle budget", &n->triangleBudget, 2000, 500000);

      NodeSeparator("operators");
      for (int i = 0; i < MeshResynthNode::kOpCount; i++)
         ModSlider(MeshResynthNode::OpNames()[i].c_str(), &n->weight[i], 0.0f, 1.0f);
   }


   void DrawImageToPointsParams(ImageToPointsNode* n)
   {
      ImGui::TextDisabled("%zu points", n->PointCount());
      ModSliderInt("density", &n->density, 4, 400);
      ModSlider("width", &n->width, 0.1f, 8.0f);
      ModSlider("height", &n->height, 0.1f, 8.0f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);

      NodeSeparator("depth");
      DropdownButton("from", ImageToPointsNode::DepthSourceNames(), n->depthSource,
                     [n](int i) { n->depthSource = i; });
      ModSlider("depth scale", &n->depthScale, -4.0f, 4.0f);

      NodeSeparator("points");
      ModSlider("point size", &n->pointSize, 0.01f, 4.0f);
      ModSlider("size from luma", &n->sizeFromLuma, -1.0f, 1.0f);
      ModCheckbox("use image colour", &n->useImageColor);
      ColorSwatch("tint", n->tint, n);
   }


   void DrawDepthProjectionParams(DepthProjectionNode* n)
   {
      if (n->outputType == DepthProjectionNode::kPoints)
         ImGui::TextDisabled("%zu points", n->PointCount());
      else
         ImGui::TextDisabled("%zu triangles (%zu vertices)", n->TriangleCount(), n->GetMesh().vertices.size());

      DropdownButton("projection", DepthProjectionNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      DropdownButton("output", DepthProjectionNode::OutputTypeNames(), n->outputType,
                     [n](int i) { n->outputType = i; });
      ModSliderInt("density", &n->density, 8, 400);

      NodeSeparator("depth");
      DropdownButton("source", DepthProjectionNode::DepthSourceNames(), n->depthSource,
                     [n](int i) { n->depthSource = i; });
      ModSlider("near depth", &n->nearDepth, 0.01f, 20.0f);
      ModSlider("far depth", &n->farDepth, 0.1f, 50.0f);
      ModSlider("depth scale", &n->depthScale, -5.0f, 5.0f);
      ModSlider("depth curve", &n->depthCurve, 0.1f, 4.0f);
      ModSlider("clip near", &n->clipNear, 0.0f, 1.0f);
      ModSlider("clip far", &n->clipFar, 0.0f, 1.0f);

      if (n->projection == DepthProjectionNode::kPerspective || n->projection == DepthProjectionNode::kRadial)
      {
         NodeSeparator("camera");
         ModSlider("fov", &n->fov, 10.0f, 130.0f);
         ModCheckbox("auto aspect", &n->autoAspect);
         if (!n->autoAspect)
            ModSlider("aspect", &n->customAspect, 0.2f, 4.0f);
         ModSlider("focal scale", &n->focalScale, 0.1f, 3.0f);
         ModSlider("center x", &n->principalPointX, -1.0f, 1.0f);
         ModSlider("center y", &n->principalPointY, -1.0f, 1.0f);
      }
      else
      {
         NodeSeparator("dimensions");
         ModSlider("width", &n->planarWidth, 0.1f, 20.0f);
         ModSlider("height", &n->planarHeight, 0.1f, 20.0f);
      }

      NodeSeparator("appearance");
      if (n->outputType == DepthProjectionNode::kPoints)
         ModSlider("point size", &n->pointSize, 0.01f, 4.0f);
      else
         ModSlider("edge tear", &n->edgeTearThreshold, 0.01f, 2.0f);

      DropdownButton("color", DepthProjectionNode::ColorModeNames(), n->colorMode,
                     [n](int i) { n->colorMode = i; });
      ColorSwatch("tint", n->tint, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
      ModSlider("roughness", &n->roughness, 0.0f, 1.0f);
   }


   void DrawMeshToPointsParams(MeshToPointsNode* n)
   {
      DropdownButton("sample", MeshToPointsNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ImGui::TextDisabled("%zu points, %zu triangles", n->PointCount(), n->TriangleCount());
      ModSliderInt("max points", &n->maxPoints, 16, 20000);
      ModSlider("point size", &n->pointSize, 0.002f, 0.3f);
      ModCheckbox("weld seams", &n->weld);
      if (n->mode == 1)
         ModSlider("dissolve angle", &n->dissolveAngleDegrees, 0.0f, 30.0f);
   }


   void DrawDistributePointsOnFacesParams(DistributePointsOnFacesNode* n)
   {
      ImGui::TextDisabled("%zu points", n->PointCount());
      ModSlider("density", &n->density, 1.0f, 500.0f);
      DropdownButton("method", DistributePointsOnFacesNode::MethodNames(), n->method,
                     [n](int i) { n->method = i; });
      if (n->method == MeshOps::kDistributePoisson)
         ModSlider("min distance", &n->minDistance, 0.005f, 1.0f);
      ModSlider("point size", &n->pointSize, 0.002f, 0.3f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);
   }


   void DrawPointsToVerticesParams(PointsToVerticesNode* n)
   {
      ImGui::TextDisabled("%zu vertices", n->VertexCount());
      ModCheckbox("alive only", &n->aliveOnly);
   }


   void DrawCurveOpsParams(CurveOpsNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu points out", n->PointCount());
      DropdownButton("mode", CurveOpsNode::ModeNames(), n->mode,
                     [n](int i) { PushUndoCheckpoint(); n->mode = i; });
      switch (n->mode)
      {
         case CurveOpsNode::kSimplify:
            ModSlider("tolerance", &n->tolerance, 0.0f, 0.5f, "%.3f");
            break;
         case CurveOpsNode::kSmooth:
            ModSliderInt("iterations", &n->iterations, 0, 50);
            ModSlider("strength", &n->strength, 0.0f, 1.0f);
            break;
         case CurveOpsNode::kOffset:
            ModSlider("distance", &n->distance, -2.0f, 2.0f);
            ModSliderInt("arc steps", &n->arcSteps, 1, 16);
            break;
         default:
            ModSliderInt("count", &n->count, 2, 512);
            break;
      }
      ModSlider("tube radius", &n->radius, 0.002f, 0.2f, "%.3f");
   }


   void DrawDelaunayParams(DelaunayMeshNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      DropdownButton("plane", DelaunayMeshNode::PlaneNames(), n->plane,
                     [n](int i) { n->plane = i; });
      ModCheckbox("alive only", &n->aliveOnly);
      ModSliderInt("max points", &n->maxPoints, 3, 5000);
      if (dynamic_cast<VoronoiCellsNode*>(n) != nullptr)
         ModSlider("inset", &n->inset, 0.0f, 0.95f);
   }


   void DrawDistributePointsInGridParams(DistributePointsInGridNode* n)
   {
      ImGui::TextDisabled("%zu points", n->PointCount());
      ModSliderInt("count x", &n->countX, 1, 200);
      ModSliderInt("count y", &n->countY, 1, 200);
      ModSlider("spacing x", &n->spacingX, 0.01f, 2.0f);
      ModSlider("spacing y", &n->spacingY, 0.01f, 2.0f);
      ModSlider("jitter", &n->jitter, 0.0f, 1.0f);
      ModSlider("point size", &n->pointSize, 0.002f, 0.3f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);
      ColorSwatch("tint", n->tint, n);
   }


   void DrawMergeByDistanceParams(MergeByDistanceNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      ModSlider("threshold", &n->threshold, 0.0f, 0.5f, "%.4f");
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

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextUnformatted(n->Status().c_str());
      ImGui::PopTextWrapPos();

      NodeSeparator("form");
      ModSlider("depth", &n->depth, 0.0f, 1.5f);
      ModSlider("bevel", &n->bevel, 0.0f, 0.45f);
      ModSlider("tracking", &n->letterSpacing, -0.1f, 0.5f);
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
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextUnformatted(n->Status().c_str());
      ImGui::PopTextWrapPos();

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
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());

      NodeSeparator("form");
      ModSliderInt("detail", &n->detail, 2, 96);
      ModSlider("bevel", &n->bevel, 0.0f, 1.0f);
      if (n->shape == 13) // supershape
      {
         ModSlider("n2", &n->superN2, 0.1f, 8.0f);
         ModSlider("n3", &n->superN3, 0.1f, 8.0f);
         ModSlider("p2", &n->superP2, 0.1f, 8.0f);
         ModSlider("p3", &n->superP3, 0.1f, 8.0f);
      }
      if (n->bevel > 0.0f)
         ModSliderInt("bevel segments", &n->bevelSegments, 1, 3);
      ModSliderInt("sides", &n->sides, 3, 64);
      // `tube` and `n2` are reused as the second and third form parameter by
      // several primitives, so each one is labelled for the shape in front of
      // you rather than by the variable it happens to live in. Capsule, Tube
      // and Helix were reading tubeRadius all along with no way to set it.
      switch (n->shape)
      {
         case 4: case 7: case 8:
            ModSlider("tube", &n->tubeRadius, 0.02f, 0.95f); break;
         case 9:
            ModSlider("inner radius", &n->tubeRadius, 0.02f, 0.95f); break;
         case 12:
            ModSlider("wire", &n->tubeRadius, 0.02f, 0.95f); break;
         case 17:
            ModSlider("corner radius", &n->tubeRadius, 0.0f, 0.99f); break;
         case 18:
            ModSlider("width", &n->tubeRadius, 0.02f, 0.95f); break;
         case 20:
            ModSlider("depth", &n->tubeRadius, 0.02f, 0.95f);
            ModSlider("tooth depth", &n->superN2, 0.1f, 2.0f);
            ModSlider("hub hole", &n->superN3, 0.0f, 2.0f);
            break;
         case 21:
            ModSlider("depth", &n->tubeRadius, 0.02f, 0.95f);
            ModSlider("inner ratio", &n->superN2, 0.1f, 1.9f);
            break;
         case 22:
            ModSlider("inner radius", &n->discInner, 0.0f, 0.98f); break;
         case 23:
            ModSlider("shaft radius", &n->tubeRadius, 0.02f, 0.8f);
            ModSlider("head length", &n->superN2, 0.15f, 2.5f);
            break;
         default: break;
      }
      if (n->shape == 7 || n->shape == 12)
      {
         ModSliderInt(n->shape == 12 ? "turns" : "knot p", &n->knotP, 1, 8);
         ModSliderInt(n->shape == 12 ? "height" : "knot q", &n->knotQ, 1, 8);
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
         case GeometryOpNode::kTransform:
         case GeometryOpNode::kSolidify:
         case GeometryOpNode::kExtrude:
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
               ImGui::TextDisabled("moving the whole instanced group (%zu copies)",
                                    n->UpstreamInstanceCount());
            else
               ImGui::TextDisabled("%zu triangles out - applied to the instanced shape, %zu copies",
                                    n->TriangleCount(), n->UpstreamInstanceCount());
         }
         else
            ImGui::TextDisabled("%zu triangles out", n->TriangleCount());
      }
      if (GeometryOpSupportsSelectionOnly(n->op))
         ModCheckbox("selection only", &n->selectionOnly);

      switch (n->op)
      {
         case GeometryOpNode::kTransform:
            if (n->selectionOnly)
            {
               ImGui::TextDisabled("%zu of %zu faces selected", n->SelectedCount(), n->TriangleCount());
               ModCheckbox("move along normals", &n->moveAlongNormals);
               if (n->moveAlongNormals)
                  ModSlider("normal amount", &n->normalAmount, -2.0f, 2.0f);
            }
            ModSlider("move x", &n->offsetX, -3.0f, 3.0f);
            ModSlider("move y", &n->offsetY, -3.0f, 3.0f);
            ModSlider("move z", &n->offsetZ, -3.0f, 3.0f);
            ModSlider("rotate x", &n->rotX, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate y", &n->rotY, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate z", &n->rotZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("scale x", &n->scaleX, 0.05f, 4.0f);
            ModSlider("scale y", &n->scaleY, 0.05f, 4.0f);
            ModSlider("scale z", &n->scaleZ, 0.05f, 4.0f);
            // Point rotate/scale pivot about - default (0,0,0) matches the
            // mesh's own origin, i.e. no visible change from before pivot
            // existed. Sits after scale/before spin, same order TransformMatrix()
            // applies move/rotate/scale/pivot in.
            ModSlider("pivot x", &n->pivotX, -3.0f, 3.0f);
            ModSlider("pivot y", &n->pivotY, -3.0f, 3.0f);
            ModSlider("pivot z", &n->pivotZ, -3.0f, 3.0f);
            ModSlider("spin / beat", &n->spin, -2.0f, 2.0f);
            break;
         case GeometryOpNode::kArray:
            ModSliderInt("count", &n->count, 1, 128);
            ModCheckbox("radial", &n->radial);
            if (n->radial)
               ModSlider("radius", &n->radius, 0.0f, 5.0f);
            else
            {
               ModSlider("offset x", &n->offsetX, -2.0f, 2.0f);
               ModSlider("offset y", &n->offsetY, -2.0f, 2.0f);
               ModSlider("offset z", &n->offsetZ, -2.0f, 2.0f);
            }
            ModSlider("rot / step", &n->rotStep, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
            ModSlider("scale / step", &n->scaleStep, 0.5f, 1.5f);
            break;
         case GeometryOpNode::kSubdivide:
            ModSliderInt("levels", &n->levels, 0, 3);
            ModSlider("smooth", &n->smooth, 0.0f, 2.0f);
            break;
         case GeometryOpNode::kSolidify:
            ModSlider("thickness", &n->thickness, 0.001f, 0.5f);
            ModCheckbox("keep original", &n->keepOriginal);
            break;
         case GeometryOpNode::kExtrude:
            ModSlider("distance", &n->thickness, -0.5f, 0.5f);
            ModSlider("inset", &n->inset, 0.0f, 0.9f);
            break;
         case GeometryOpNode::kWireframe:
            ModSlider("thickness", &n->thickness, 0.002f, 0.1f);
            break;
         case GeometryOpNode::kTriangulate:
            ModSlider("jitter", &n->amount, 0.0f, 2.0f);
            break;
         case GeometryOpNode::kNormals:
            ModCheckbox("flat shade", &n->flatShade);
            ModCheckbox("flip", &n->flipNormals);
            break;
         case GeometryOpNode::kExplode:
         {
            static const std::vector<std::string> kExplodeByNames = { "Faces", "Loose Parts" };
            DropdownButton("by", kExplodeByNames, n->explodeBy, [n](int i) { n->explodeBy = i; });
            ModSlider("amount", &n->amount, 0.0f, 3.0f);
            ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);
            break;
         }
         case GeometryOpNode::kSmooth:
            ModSliderInt("pre-subdivide", &n->levels, 0, 3);
            ModSliderInt("iterations", &n->iterations, 1, 20);
            ModSlider("strength", &n->amount, 0.0f, 1.0f);
            break;
         case GeometryOpNode::kMirror:
            DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
            ModSlider("plane offset", &n->mirrorOffset, -2.0f, 2.0f);
            ModCheckbox("keep original", &n->keepOriginal);
            ModCheckbox("weld seam", &n->weldSeam);
            break;
         case GeometryOpNode::kSelect:
         {
            static const std::vector<std::string> kSelectModes = {
               "All", "By index", "By position", "By normal", "Random", "By radius"
            };
            ImGui::TextDisabled("%zu of %zu faces", n->SelectedCount(), n->TriangleCount());
            DropdownButton("mode", kSelectModes, n->selectMode, [n](int i) { n->selectMode = i; });
            switch (n->selectMode)
            {
               case 1: // index
                  ModSlider("start", &n->selectA, 0.0f, 2000.0f, "%.0f");
                  ModSlider("count", &n->selectB, 0.0f, 2000.0f, "%.0f");
                  ModSlider("every", &n->selectC, 1.0f, 32.0f, "%.0f");
                  break;
               case 2: // position along an axis
                  DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
                  ModSlider("min", &n->selectA, -3.0f, 3.0f);
                  ModSlider("max", &n->selectB, -3.0f, 3.0f);
                  break;
               case 3: // normal direction
                  DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
                  ModSlider("facing", &n->selectA, -1.0f, 1.0f);
                  ModSlider("sign", &n->selectC, -1.0f, 1.0f);
                  break;
               case 4: // random
                  ModSlider("amount", &n->selectA, 0.0f, 1.0f);
                  ModSlider("seed", &n->selectSeed, 0.0f, 100.0f);
                  break;
               case 5: // radius
                  ModSlider("x", &n->selectA, -3.0f, 3.0f);
                  ModSlider("y", &n->selectB, -3.0f, 3.0f);
                  ModSlider("z", &n->selectC, -3.0f, 3.0f);
                  ModSlider("radius", &n->selectSeed, 0.0f, 3.0f);
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
               ImGui::TextDisabled("%zu of %zu faces selected", n->SelectedCount(), n->TriangleCount());
            ModCheckbox("keep selected instead", &n->keepSelected);
            break;
         case GeometryOpNode::kTransformSelected:
            ModCheckbox("move along normals", &n->moveAlongNormals);
            if (n->moveAlongNormals)
               ModSlider("normal amount", &n->normalAmount, -2.0f, 2.0f);
            ModSlider("move x", &n->offsetX, -3.0f, 3.0f);
            ModSlider("move y", &n->offsetY, -3.0f, 3.0f);
            ModSlider("move z", &n->offsetZ, -3.0f, 3.0f);
            ModSlider("rotate x", &n->rotX, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate y", &n->rotY, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("rotate z", &n->rotZ, -180.0f, 180.0f, "%.1f\xC2\xB0");
            ModSlider("scale x", &n->scaleX, 0.1f, 3.0f);
            ModSlider("scale y", &n->scaleY, 0.1f, 3.0f);
            ModSlider("scale z", &n->scaleZ, 0.1f, 3.0f);
            ModSlider("spin / beat", &n->spin, -2.0f, 2.0f);
            break;
         case GeometryOpNode::kExtrudeSelected:
            ModSlider("distance", &n->thickness, -1.0f, 1.0f);
            ModSlider("inset", &n->inset, 0.0f, 0.9f);
            break;
         case GeometryOpNode::kDecimate:
            ModSlider("keep", &n->decimateRatio, 0.01f, 1.0f, "%.2f");
            ModCheckbox("lock border", &n->lockBorder);
            break;
         case GeometryOpNode::kScrew:
            ModSliderInt("steps", &n->screwSteps, 3, 256);
            ModSlider("turns", &n->turns, 0.05f, 6.0f);
            ModSlider("rise / turn", &n->rise, -2.0f, 2.0f);
            ModSlider("radius", &n->radiusOffset, 0.0f, 3.0f);
            DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
            break;
         default:
            ModSlider("angle", &n->amount, -171.8873f, 171.8873f, "%.1f\xC2\xB0");
            DropdownButton("axis", kAxisXYZ, n->axis, [n](int i) { PushUndoCheckpoint(); n->axis = i; }, kParamWidth, true, true);
            break;
      }
   }


   void DrawDisplacementParams(DisplacementNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu triangles", n->TriangleCount());

      static const std::vector<std::string> kModeNames = { "Scalar (along normal)", "Vector (RGB = XYZ)" };
      DropdownButton("mode", kModeNames, n->mode, [n](int i) { n->mode = i; });
      ModSlider("strength", &n->strength, -2.0f, 2.0f);
      if (n->mode == DisplacementNode::kScalar)
         ModSlider("midlevel", &n->midlevel, 0.0f, 1.0f);
      ModCheckbox("flat shade", &n->flatShade);
      ModCheckbox("flip normals", &n->flipNormals);
      ModCheckbox("selection only", &n->selectionOnly);
   }


   void DrawAudioDisplacementParams(AudioDisplacementNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu triangles", n->TriangleCount());

      DropdownButton("mode", AudioDisplacementNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModSlider("strength", &n->strength, -4.0f, 4.0f);
      ModSlider("frequency", &n->frequency, 0.1f, 12.0f);
      ModSlider("speed", &n->speed, -5.0f, 5.0f);
      ModSlider("damping", &n->damping, 0.0f, 4.0f);
      ModSlider("attack", &n->attack, 1.0f, 300.0f, "%.0f ms");
      ModSlider("decay", &n->decay, 10.0f, 1500.0f, "%.0f ms");
      ModSlider("midlevel", &n->midlevel, -1.0f, 1.0f);
      ModSliderInt("subdivide", &n->subdivide, 0, 4);

      if (n->mode == AudioDisplacementNode::kModeDirectionalAxis)
      {
         static const std::vector<std::string> kAxes = { "X Axis", "Y Axis", "Z Axis" };
         DropdownButton("axis", kAxes, n->axis, [n](int i) { n->axis = i; });
      }
      else if (n->mode == AudioDisplacementNode::kModeCymaticsChladni)
      {
         ModSliderInt("mode m", &n->chladniM, 1, 12);
         ModSliderInt("mode n", &n->chladniN, 1, 12);
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
         ModSlider("roughness", &n->roughness, 0.02f, 1.0f);
         ModSlider("opacity", &n->opacity, 0.0f, 1.0f);
         ColorSwatch("emission", n->emissionColor, n);
         ModSlider("emission", &n->emission, 0.0f, 8.0f);
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
      ModSlider("gain", &n->gain, 0.1f, 10.0f);
      ModSlider("smoothing", &n->smoothing, 0.0f, 0.99f);
   }


   void DrawAudioColorRampEditor(AudioColorRampNode* n)
   {
      const float size = kPreviewSize;
      const float barH = 64.0f;
      const float gap = 4.0f;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      dl->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + barH), tok::U32(tok::pal::c_0E0E14FF), 3.0f);

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
            float h = barH * 0.10f + (barH * 0.85f) * lvl;
            ImU32 col = IM_COL32((int)(std::clamp(n->bandColor[b][0], 0.0f, 1.0f) * 255),
                                 (int)(std::clamp(n->bandColor[b][1], 0.0f, 1.0f) * 255),
                                 (int)(std::clamp(n->bandColor[b][2], 0.0f, 1.0f) * 255), 255);
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
            pts.push_back(ImVec2(origin.x + t * size, origin.y + barH - mag * (barH - 4.0f)));
         }
         dl->AddPolyline(pts.data(), (int)pts.size(), tok::U32(tok::pal::c_FFFFFF82), false, 1.5f);
      }

      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + barH), tok::U32(tok::pal::c_464A5AFF), 3.0f);

      // 3. Interactive band-boundary dividers, drawn directly on the spectrum.
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##acr_track", ImVec2(size, barH));
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
         float minPos = (sDragIndex == 0) ? 0.02f : (n->crossoverPos[sDragIndex - 1] + 0.02f);
         float maxPos = (sDragIndex == numDividers - 1) ? 0.98f : (n->crossoverPos[sDragIndex + 1] - 0.02f);
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
         ImU32 lineCol = isHov ? tok::U32(tok::pal::c_FFDC64E6) : tok::U32(tok::pal::c_FFFFFF64);
         dl->AddLine(ImVec2(x, origin.y + 2.0f), ImVec2(x, origin.y + barH - 2.0f), lineCol, isHov ? 2.5f : 1.5f);

         if (isHov)
         {
            float hz = AudioColorRampNode::PosToFreq(n->crossoverPos[i]);
            char hzText[32];
            if (hz >= 1000.0f)
               snprintf(hzText, sizeof(hzText), "%.1f kHz", hz / 1000.0f);
            else
               snprintf(hzText, sizeof(hzText), "%.0f Hz", hz);
            snprintf(readout, sizeof(readout), "crossover %d: %s", i + 1, hzText);
         }
      }

      const float readoutH = ImGui::GetTextLineHeight();
      dl->AddText(ImVec2(origin.x, origin.y + barH + gap), tok::U32(tok::pal::c_A0A6BAFF), readout);

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
            snprintf(label, sizeof(label), "B%d (%.0f-%.1fk)", b + 1, f0, f1 / 1000.0f);
         else
            snprintf(label, sizeof(label), "B%d (%.0f-%.0fHz)", b + 1, f0, f1);

         ColorSwatch(label, n->bandColor[b], n);
         ImGui::PopID();
      }

      DropdownButton("mode", AudioColorRampNode::ModeNames(), n->mode,
                     [n](int i) { PushUndoCheckpoint(); n->mode = i; n->MarkDirty(); });
      DropdownButton("interpolation", AudioColorRampNode::InterpNames(), n->interpMode,
                     [n](int i) { PushUndoCheckpoint(); n->interpMode = i; n->MarkDirty(); });

      int bCount = n->bandCount;
      if (ModSliderInt("bands", &bCount, 2, AudioColorRampNode::kMaxBands))
      {
         PushUndoCheckpoint();
         n->SetBandCount(bCount);
      }

      ModSlider("gain", &n->gain, 0.1f, 10.0f);
      ModSlider("min glow", &n->minBrightness, 0.0f, 1.0f);
   }


   void DrawAudioRibbonParams(AudioRibbonNode* n)
   {
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());
      ModSlider("width", &n->width, 0.05f, 5.0f);
      ModSlider("height scale", &n->heightScale, 0.0f, 5.0f);
      ModSliderInt("segments", &n->segments, 16, 1024);
      ModSlider("gain", &n->gain, 0.1f, 10.0f);
      ModSlider("smoothing", &n->smoothing, 0.0f, 0.99f);

      NodeSeparator("transform");
      ModSlider("pos X", &n->posX, -10.0f, 10.0f);
      ModSlider("pos Y", &n->posY, -10.0f, 10.0f);
      ModSlider("pos Z", &n->posZ, -10.0f, 10.0f);
      ModSlider("rot X", &n->rotX, -3.14159f, 3.14159f);
      ModSlider("rot Y", &n->rotY, -3.14159f, 3.14159f);
      ModSlider("rot Z", &n->rotZ, -3.14159f, 3.14159f);
      ModSlider("scale X", &n->scaleX, 0.01f, 10.0f);
      ModSlider("scale Y", &n->scaleY, 0.01f, 10.0f);
      ModSlider("scale Z", &n->scaleZ, 0.01f, 10.0f);
      ModSlider("uniform scale", &n->uniformScale, 0.01f, 10.0f);

      NodeSeparator("material");
      DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; });
      ColorSwatch("colour", n->color, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
      ModSlider("roughness", &n->roughness, 0.02f, 1.0f);
      ModSlider("opacity", &n->opacity, 0.0f, 1.0f);
      ColorSwatch("emission", n->emissionColor, n);
      ModSlider("emission", &n->emission, 0.0f, 8.0f);
      ModSlider("ior", &n->ior, 1.0f, 3.0f);
   }


   void DrawSetColorParams(SetColorNode* n)
   {
      if (n->input != nullptr)
         ImGui::TextDisabled("%zu triangles", n->TriangleCount());

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
         ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);
         break;
      case SetColorNode::kPalette:
         ModSliderInt("palette offset", &n->paletteOffset, 0, 32);
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
         ImGui::TextDisabled("%zu instances, %zu triangles", n->InstanceCount(), n->TriangleCount());

      DropdownButton("points from", InstanceOnPointsNode::SourceNames(), n->pointMode,
                     [n](int i) { n->pointMode = i; });
      ModSliderInt("max points", &n->maxPoints, 1, 20000);
      ModSlider("scale", &n->instanceScale, 0.005f, 1.0f);
      ModSlider("scale random", &n->scaleRandom, 0.0f, 1.0f);
      ModSlider("rotation random", &n->rotationRandom, 0.0f, 1.0f);
      ModSlider("normal offset", &n->normalOffset, -0.5f, 0.5f);
      ModCheckbox("align to normal", &n->alignToNormal);
      ModSlider("seed", &n->seed, 0.0f, 100.0f, "%.0f", kParamWidth, false, 1.0f);
   }


   void DrawCameraParams(CameraNode* n)
   {
      DropdownButton("projection", CameraNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      if (n->projection == 0)
         ModSlider("fov", &n->fov, 10.0f, 120.0f);
      else
         ModSlider("ortho height", &n->orthoHeight, 0.2f, 8.0f);
      ModSlider("distance", &n->distance, 0.3f, 20.0f);
      ModSlider("orbit", &n->azimuth, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->elevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
      ModSlider("roll", &n->roll, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("orbit / beat", &n->orbitPerBeat, -1.0f, 1.0f);
      ModSlider("target x", &n->targetX, -3.0f, 3.0f);
      ModSlider("target y", &n->targetY, -3.0f, 3.0f);
      ModSlider("target z", &n->targetZ, -3.0f, 3.0f);
      ModSlider("near", &n->nearPlane, 0.01f, 1.0f);
      ModSlider("far", &n->farPlane, 5.0f, 500.0f);
   }


   void DrawLightParams(LightNode* n)
   {
      DropdownButton("type", LightNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      ModSlider("orbit", &n->azimuth, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("elevation", &n->elevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0");
      if (n->type == 1 || n->type == 4)
         ModSlider("distance", &n->distance, 0.2f, 20.0f);
      if (n->type == 4)
      {
         ModSlider("cone angle", &n->spotAngle, 5.0f, 85.0f, "%.1f\xC2\xB0");
         ModSlider("penumbra", &n->spotPenumbra, 0.0f, 1.0f);
      }
      ColorSwatch("colour", n->color, n);
      ModSlider("intensity", &n->intensity, 0.0f, 5.0f);
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
      ImGui::SameLine(0.0f, 16.0f);
      ImGui::TextDisabled("%zu triangles in %zu draw calls", n->LastTriangleCount(), n->LastDrawCalls());

      gParamWidthLive = kParamWidthBase;   // two-column body keeps its column width; only single-column bodies stretch to the header (R3)
      const float colW = kParamWidth;
      const float gutter = 16.0f;

      if (ActionButton::Column("Frame scene", colW))
         FrameSceneInView(n);

      // --- Left Column ---
      ImGui::BeginGroup();

      NodeSeparator("output", colW);
      DropdownButton("pass", Render3DNode::RenderPassNames(), n->renderPass,
                     [n](int i) { n->renderPass = i; }, colW);
      // This is the export resolution: Output sizes its own buffer from whatever
      // its input hands it, so a 4000px PNG needs 4000 set here or it is an
      // upscale of a smaller render.
      ModSlider("width", &n->width, 64.0f, 8192.0f, "%.0f", colW);
      ModSlider("height", &n->height, 64.0f, 8192.0f, "%.0f", colW);
      DropdownButton("antialias", Render3DNode::SampleNames(), n->samples,
                     [n](int i) { n->samples = i; }, colW);
      {
         // The requested sample count is clamped by both the driver and a memory
         // budget, so show what actually happened when they disagree.
         const int wanted = (n->samples <= 0) ? 0 : (1 << n->samples);
         if (n->ActiveSamples() != wanted)
            ImGui::TextColored(tok::V4(tok::palf::v_950_750_350_1000),
                               "antialias reduced to %dx at this size",
                               n->ActiveSamples());
      }
      DropdownButton("tonemap", Render3DNode::TonemapNames(), n->tonemap,
                     [n](int i) { n->tonemap = i; }, colW);
      ModSlider("exposure", &n->exposure, 0.1f, 4.0f, "%.3f", colW);
      ColorSwatch("background", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f, "%.3f", colW);

      NodeSeparator("camera", colW);
      if (n->camera != nullptr)
         ImGui::TextDisabled("driven by a Camera node");
      else
      {
         DropdownButton("projection", Render3DNode::ProjectionNames(), n->projection,
                        [n](int i) { n->projection = i; }, colW);
         if (n->projection == 0)
            ModSlider("fov", &n->fov, 10.0f, 120.0f, "%.3f", colW);
         else
            ModSlider("ortho height", &n->orthoHeight, 0.2f, 8.0f, "%.3f", colW);
         ModSlider("distance", &n->camDistance, 0.3f, 20.0f, "%.3f", colW);
         // This render's own camera, entirely separate from any node's
         // mini-viewport/panel camera (SharedViewportCamera.h/gNodeCameras):
         // the final output framing is a deliberate setup, not something
         // casual node-viewport orbiting should ever move.
         ModSlider("orbit", &n->camAzimuth, -180.0f, 180.0f, "%.1f\xC2\xB0", colW);
         ModSlider("elevation", &n->camElevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0", colW);
         ModSlider("target x", &n->targetX, -3.0f, 3.0f, "%.3f", colW);
         ModSlider("target y", &n->targetY, -3.0f, 3.0f, "%.3f", colW);
         ModSlider("target z", &n->targetZ, -3.0f, 3.0f, "%.3f", colW);
      }

      ImGui::EndGroup();

      // --- Right Column ---
      ImGui::SameLine(0.0f, gutter);
      ImGui::BeginGroup();

      NodeSeparator("light", colW);
      int patchedLights = 0;
      for (int i = 0; i < Render3DNode::kLightSlots; i++)
         if (n->lights[i] != nullptr)
            patchedLights++;
      if (patchedLights > 0)
         ImGui::TextDisabled("%d Light node%s patched", patchedLights, patchedLights == 1 ? "" : "s");
      else
      {
         ModSlider("light orbit", &n->lightAzimuth, -180.0f, 180.0f, "%.1f\xC2\xB0", colW);
         ModSlider("light height", &n->lightElevation, -85.9437f, 85.9437f, "%.1f\xC2\xB0", colW);
         ColorSwatch("light", n->lightColor, n);
         ModSlider("intensity", &n->lightIntensity, 0.0f, 4.0f, "%.3f", colW);
      }
      ColorSwatch("ambient", n->ambientColor, n);
      ModSlider("rim", &n->rimIntensity, 0.0f, 2.0f, "%.3f", colW);

      NodeSeparator("shadows", colW);
      ModCheckbox("cast shadows", &n->shadowsEnabled);
      if (n->shadowsEnabled)
      {
         DropdownButton("resolution", Render3DNode::ShadowQualityNames(), n->shadowQuality,
                        [n](int i) { n->shadowQuality = i; }, colW);
         ModSlider("strength", &n->shadowStrength, 0.0f, 1.0f, "%.3f", colW);
         ModSlider("softness", &n->shadowSoftness, 0.0f, 4.0f, "%.3f", colW);
         ModSlider("bias", &n->shadowBias, 0.0002f, 0.02f, "%.4f", colW);
      }

      NodeSeparator("environment", colW);
      const bool envConnected = n->envInput.IsConnected();
      if (envConnected)
      {
         ImGui::TextDisabled("driven by an HDRI node");
         ModCheckbox("use as background", &n->envAsBackground);
      }
      else
      {
         ColorSwatch("sky", n->envSky, n);
         ColorSwatch("horizon", n->envHorizon, n);
         ColorSwatch("ground", n->envGround, n);
      }
      ModSlider("env intensity", &n->envIntensity, 0.0f, 3.0f, "%.3f", colW);

      NodeSeparator("points", colW);
      DropdownButton("sprite shape", Render3DNode::SpriteShapeNames(), n->spriteShape,
                     [n](int i) { n->spriteShape = i; }, colW);
      DropdownButton("sprite size", Render3DNode::SpriteSizeModeNames(), n->spriteSizeMode,
                     [n](int i) { n->spriteSizeMode = i; }, colW);

      NodeSeparator("raster", colW);
      ModCheckbox("depth test", &n->depthTest);
      ModCheckbox("cull backfaces", &n->backfaceCull);

      ImGui::EndGroup();
   }


   void DrawBlendParams(BlendNode* n)
   {
      DropdownButton("mode", BlendNode::ModeNames(), n->ModeIndex(),
                     [n](int i) { n->ModeIndex() = i; });
      ModSlider("opacity", &n->Mix(), 0.0f, 1.0f);
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

      for (int slot = 0; slot < LayerStackNode::kSlots; slot++)
      {
         ImGui::PushID(slot);

         const bool dragging = (sDragNode == n && sDragSlot == slot);
         ImVec2 headerPos = ImGui::GetCursorScreenPos();
         ImGui::InvisibleButton("##grip", ImVec2(kPreviewSize, rowHeight));
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
                              dragging ? tok::U32(tok::pal::c_465A82C8) : tok::U32(tok::pal::c_323644A0), 3.0f);
         }
         // grip dots, so the header reads as draggable
         for (int d = 0; d < 3; d++)
         {
            dl->AddCircleFilled(ImVec2(headerPos.x + 6, headerPos.y + 5 + d * 4.0f), 1.3f,
                                tok::U32(tok::pal::c_8C92A8FF));
            dl->AddCircleFilled(ImVec2(headerPos.x + 11, headerPos.y + 5 + d * 4.0f), 1.3f,
                                tok::U32(tok::pal::c_8C92A8FF));
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
         dl->AddText(ImVec2(headerPos.x + 20, headerPos.y + 2),
                     source ? tok::U32(tok::pal::c_BEC4D7FF) : tok::U32(tok::pal::c_787C8EFF), title);

         char modeLabel[32];
         snprintf(modeLabel, sizeof(modeLabel), "mode##%d", slot);
         DropdownButton(modeLabel, BlendModes::Names(), n->modes[slot],
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
            const float threshold = rowHeight * 3.0f; // one full layer block
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
   }


   void DrawFilterParams(FilterNode* n)
   {
      const FilterDef& def = n->Def();
      for (size_t i = 0; i < def.params.size(); i++)
      {
         const FilterParamDef& p = def.params[i];
         if (!p.sectionLabel.empty())
            NodeSeparator(p.sectionLabel.c_str());
         if (def.name == "convolve" && i < 9)
         {
            // The 3x3 kernel reads as the matrix it is: nine number cells, each a full slider (same pin, typing,
            // menus, recording and binding as any other param), named k11..k33 for the modulation matrix.
            if (i == 0)
            {
               const float gap = tok::space_1;
               const float cellW = std::floor((kParamWidth - 2.0f * gap) / 3.0f);
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
      if (def.params.empty())
         ImGui::TextDisabled("(no parameters)");
   }
}

// Generic node parameter bodies, part 1 (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/StepCell.h"

namespace app
{
   // Copies every parameter VisitParams declares, for any node type, by
   // routing through the same save/load format used for patch files: write src
   // to an in-memory param list, then read it back into dst. This used to be a
   // hand-maintained field-by-field switch covering 7 of roughly 50 node types;
   // every type it didn't cover pasted a node whose values silently stayed at
   // spawn defaults. Now paste and patch load share one source of truth, so a
   // node that round-trips through Save also round-trips through Copy.
   void CopyParams(INode* dstNode, INode* srcNode)
   {
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(srcNode, params);
      Patch::LoadParams(dstNode, params);
      ReloadDerivedState(dstNode);

      // A copy/duplicate/paste of a Predictive node must not inherit the source's exact learned
      // model verbatim - VisitParams round-trips the learned blob (profile/model/fitData) like
      // any other param, so without this a "duplicate" opens already showing "Learn Again" with
      // someone else's confidence instead of "press Learn" (bug-blast-radius review, step-10).
      // Only the per-instance learned state resets; each node's own shared cross-session
      // contribution (if any) is untouched - the source instance's material stays in house style.
      if (auto* cn = dynamic_cast<PredictiveColoringNode*>(dstNode))
         cn->ResetProfile();
      else if (auto* nn = dynamic_cast<PredictiveNotesNode*>(dstNode))
         nn->ResetLearnedState();
      else if (auto* mn = dynamic_cast<PredictiveModulatorNode*>(dstNode))
         mn->ResetLearnedState();
      else if (auto* rn = dynamic_cast<PredictiveRhythmNode*>(dstNode))
         rn->ResetLearnedState();
   }


   // ---------------- per-node parameter UI ----------------

   void DrawImageSourceParams(ImageSourceNode* n)
   {
      if (ActionButton::Draw("Choose image...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         // wrap pos is window-relative; passing a bare width put it left of the
         // cursor and wrapped every single character onto its own line
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
   }


   void DrawSlideshowParams(SlideshowNode* n)
   {
      if (ActionButton::Draw("Choose folder...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->FolderPath().empty())
      {
         if (ActionButton::Draw("Refresh", ImVec2(kPreviewSize, 0)))
            n->ReloadFromFolder();
      }

      DropdownButton("transition", SlideshowNode::TransitionNames(), n->transition,
                     [n](int i) { n->transition = i; });
      ModSlider("hold", &n->holdDuration, 0.05f, 60.0f, "%.2f s");
      ModSlider("transition time", &n->transitionDuration, 0.0f, 15.0f, "%.2f s");
      DropdownButton("fit", SlideshowNode::FitModeNames(), n->fitMode,
                     [n](int i) { n->fitMode = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!n->LastError().empty())
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
      if (n->ImageCount() > 0)
      {
         const std::string file = n->CurrentFileName();
         ImGui::TextDisabled("%d/%d  %s", n->CurrentImageNumber(), n->ImageCount(), file.c_str());
      }
      else
         ImGui::TextDisabled("Choose a folder containing images.");
      ImGui::PopTextWrapPos();
   }


   void DrawSyphonOutParams(SyphonOutNode* n)
   {
#if !defined(__APPLE__) && !defined(_WIN32)
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("Syphon/Spout texture sharing is unavailable on Linux.");
      ImGui::PopTextWrapPos();
      return;
#else
      ImGui::SetNextItemWidth(kPreviewSize);
      if (FieldWell::InputText("##syphon_name", &n->serverNameInput, ImGuiInputTextFlags_EnterReturnsTrue))
      {
         n->SetServerName(n->serverNameInput);
      }
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
         n->SetServerName(n->serverNameInput);
      }

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (n->PublishedWidth() > 0 && n->PublishedHeight() > 0)
      {
         ImGui::TextColored(tok::V4(tok::palf::v_400_1000_500_1000), "Broadcasting: %dx%d", n->PublishedWidth(), n->PublishedHeight());
         if (!Platform::SyphonServerCanReportClients())
            ImGui::TextDisabled("Clients: not reported");
         else if (n->HasClients())
            ImGui::TextColored(tok::V4(tok::palf::v_300_900_1000_1000), "Clients: Active");
         else
            ImGui::TextDisabled("Clients: Waiting for app...");
      }
      else
      {
         ImGui::TextDisabled("Connect an image input to publish");
      }
      ImGui::PopTextWrapPos();
#endif
   }


   void DrawSyphonInParams(SyphonInNode* n)
   {
#if !defined(__APPLE__) && !defined(_WIN32)
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("Syphon/Spout texture sharing is unavailable on Linux.");
      ImGui::PopTextWrapPos();
      return;
#else
      if (ActionButton::Draw("Refresh Servers", ImVec2(kPreviewSize, 0)))
      {
         n->RefreshServers();
      }

      const auto& servers = n->AvailableServers();
      if (servers.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
#if defined(_WIN32)
         ImGui::TextDisabled("No active Spout senders found.");
#else
         ImGui::TextDisabled("No active Syphon servers found.");
#endif
         ImGui::PopTextWrapPos();
      }
      else
      {
         std::vector<std::string> serverNames;
         serverNames.reserve(servers.size());
         for (const auto& s : servers)
         {
            std::string label = s.appName.empty() ? "App" : s.appName;
            if (!s.serverName.empty())
               label += " (" + s.serverName + ")";
            serverNames.push_back(label);
         }

         int currentIdx = n->SelectedServerIndex();
         if (currentIdx < 0) currentIdx = 0;
         DropdownButton("##syphon_server", serverNames, currentIdx, [n](int idx) {
            n->SelectServer(idx);
         }, kPreviewSize);

         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         if (n->IsConnected())
         {
            ImGui::TextColored(tok::V4(tok::palf::v_400_1000_500_1000), "Receiving: %dx%d", n->GetOutputWidth(), n->GetOutputHeight());
         }
         else
         {
            ImGui::TextColored(tok::V4(tok::palf::v_1000_600_200_1000), "Connecting...");
         }
         ImGui::PopTextWrapPos();
      }
#endif
   }


   void DrawNdiOutParams(NdiOutNode* n)
   {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!Ndi::Available())
      {
         ImGui::TextDisabled("NDI runtime not found (install from ndi.video)");
         ImGui::PopTextWrapPos();
         return;
      }
      ImGui::PopTextWrapPos();

      ImGui::SetNextItemWidth(kPreviewSize);
      if (FieldWell::InputText("##ndi_name", &n->sourceNameInput, ImGuiInputTextFlags_EnterReturnsTrue))
         n->SetSourceName(n->sourceNameInput);
      if (ImGui::IsItemDeactivatedAfterEdit())
         n->SetSourceName(n->sourceNameInput);

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (n->IsPublishing())
      {
         ImGui::TextColored(tok::V4(tok::palf::v_400_1000_500_1000), "Broadcasting: %dx%d", n->PublishedWidth(), n->PublishedHeight());
         if (n->Connections() > 0)
            ImGui::TextColored(tok::V4(tok::palf::v_300_900_1000_1000), "Receivers: %d", n->Connections());
         else
            ImGui::TextDisabled("Receivers: waiting...");
      }
      else
         ImGui::TextDisabled("Connect an image input to publish");
      ImGui::PopTextWrapPos();
   }


   void DrawNdiInParams(NdiInNode* n)
   {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!Ndi::Available())
      {
         ImGui::TextDisabled("NDI runtime not found (install from ndi.video)");
         ImGui::PopTextWrapPos();
         return;
      }
      ImGui::PopTextWrapPos();

      const std::vector<std::string> sources = n->AvailableSources();
      if (sources.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("No NDI sources found on the network.");
         ImGui::PopTextWrapPos();
         return;
      }

      int currentIdx = 0;
      for (size_t i = 0; i < sources.size(); i++)
         if (sources[i] == n->SourceName())
            currentIdx = (int)i;
      std::vector<std::string> labels;
      labels.reserve(sources.size());
      for (const std::string& s : sources)
         labels.push_back(s);
      DropdownButton("##ndi_source", labels, currentIdx, [n, sources](int idx) {
         n->SelectSource(sources[(size_t)idx]);
      }, kPreviewSize);

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (n->SourceName().empty())
         ImGui::TextDisabled("Pick a source");
      else if (n->IsReceiving())
         ImGui::TextColored(tok::V4(tok::palf::v_400_1000_500_1000), "Receiving: %dx%d", n->GetOutputWidth(), n->GetOutputHeight());
      else
         ImGui::TextColored(tok::V4(tok::palf::v_1000_600_200_1000), "Waiting for frames...");
      ImGui::PopTextWrapPos();
   }


   void DrawOscReceiveParams(OscReceiveNode* n)
   {
      ModSliderInt("port", &n->port, 1, 65535);
      ImGui::SetNextItemWidth(kParamWidth);
      FieldWell::InputText("address", &n->address);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }


   void DrawOscSendParams(OscSendNode* n)
   {
      ImGui::SetNextItemWidth(kParamWidth);
      FieldWell::InputText("host", &n->host);
      ModSliderInt("port", &n->port, 1, 65535);
      ImGui::SetNextItemWidth(kParamWidth);
      FieldWell::InputText("address", &n->address);
      ModSlider("epsilon", &n->epsilon, 0.0f, 0.1f, "%.4f");
      ModSlider("interval (ms)", &n->intervalMs, 1.0f, 1000.0f, "%.0f");
      if (n->LastSent() < 0.0f)
         ImGui::TextDisabled("not sent yet");
      else
         ImGui::TextDisabled("last sent: %.3f", n->LastSent());
   }


   void DrawEnvironmentParams(EnvironmentNode* n)
   {
      if (ActionButton::Draw("Choose HDRI...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
      else
      {
         ImGui::TextDisabled("no image - patch into Render 3D's env pin anyway");
         ImGui::TextDisabled("to use its procedural sky instead");
      }

      ModSlider("intensity", &n->intensity, 0.0f, 8.0f);
      ModSlider("rotation", &n->rotation, -180.0f, 180.0f, "%.1f\xC2\xB0");
   }


   void DrawShapeParams(ShapeNode* n)
   {
      // Not every shape uses corner/sides/inner ratio - e.g. a Circle has no
      // corners and no sides, a Triangle's side count is fixed at 3. Hide the
      // params a shape ignores so the panel doesn't show dead controls.
      // Keep in sync with the uShape switch in ShapeNode.cpp's fragment shader.
      const bool usesCorner = n->shapeType == 3 || n->shapeType == 7 || n->shapeType == 9 || n->shapeType == 18;
      const bool usesSides = n->shapeType == 5 || n->shapeType == 6 || n->shapeType == 14 || n->shapeType == 19;
      const bool usesInner = n->shapeType == 6 || n->shapeType == 14 || n->shapeType == 15 || n->shapeType == 16;

      DropdownButton("shape", ShapeNode::ShapeNames(), n->shapeType,
                     [n](int i) { n->shapeType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("size x", &n->sizeX, 0.01f, 1.0f);
      ModSlider("size y", &n->sizeY, 0.01f, 1.0f);
      if (usesCorner)
         ModSlider("corner/thick", &n->cornerRadius, 0.0f, 0.3f);
      if (usesSides)
         ModSliderInt("sides", &n->sides, 3, 20);
      if (usesInner)
         ModSlider("inner ratio", &n->innerRatio, 0.05f, 1.0f);
      ModSlider("rotation", &n->rotation, -180.0f, 180.0f, "%.1f\xC2\xB0");
      ModSlider("pos x", &n->posX, 0.0f, 1.0f);
      ModSlider("pos y", &n->posY, 0.0f, 1.0f);
      ColorSwatch("fill", n->fillColor, n);
      ModSlider("fill opacity", &n->fillOpacity, 0.0f, 1.0f);
      ModSlider("stroke width", &n->strokeWidth, 0.0f, 0.1f);
      ColorSwatch("stroke", n->strokeColor, n);
      ModSlider("feather", &n->feather, 0.0f, 0.1f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
   }


   void DrawFormulaParams(FormulaNode* n)
   {
      // The GLSL editor lives in its own window, not inline: ImGui multi-line
      // fields are child windows, and child windows inside the node canvas get
      // clipped away - which is why the box used to render empty.
      //
      // Field build step 17: this dropdown now folds the factory presets and
      // the user's saved .infdev devices into one control (plan §6), rather
      // than a bare preset picker - FormulaNode's device.params stays empty
      // (plan §7), only `formula` round-trips.
      DrawFieldDeviceControls<FormulaNode>(n, "formula", &FormulaNode::PresetNames(),
                                          [](FormulaNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });

      if (ActionButton::Draw("Edit GLSL...", ImVec2(kPreviewSize, 0)))
      {
         gFormulaEditor = n;
         gFormulaEditorOpen = true;
      }

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }

      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("uA", &n->knobA, 0.0f, 1.0f);
      ModSlider("uB", &n->knobB, 0.0f, 1.0f);
      ModSlider("uC", &n->knobC, 0.0f, 1.0f);
      ModSlider("uD", &n->knobD, 0.0f, 1.0f);
      ModCheckbox("animate", &n->animate);
   }


   void DrawTextParams(TextNode* n)
   {
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (FieldWell::InputText("text", buf, sizeof(buf)))
         n->text = buf;

      const std::vector<std::string>& fonts = TextNode::AvailableFonts();
      if (n->fontName.empty())
         n->fontName = fonts.front();
      int fontIdx = 0;
      for (int i = 0; i < (int)fonts.size(); i++)
      {
         if (fonts[i] == n->fontName)
            fontIdx = i;
      }
      DropdownButton("font", fonts, fontIdx, [n, &fonts](int i) { n->fontName = fonts[i]; });

      ModSlider("size", &n->fontSize, 8.0f, 300.0f);
      ColorSwatch("color", n->color, n);
      ModSlider("tracking", &n->tracking, -10.0f, 40.0f);
      ModSlider("pos x", &n->posX, 0.0f, 1.0f);
      ModSlider("pos y", &n->posY, 0.0f, 1.0f);
      DropdownButton("align", AlignOptions(), n->align, [n](int i) { n->align = i; });
      ModSlider("scale x", &n->scaleX, 0.1f, 4.0f);
      ModSlider("scale y", &n->scaleY, 0.1f, 4.0f);
      ModCheckbox("word wrap", &n->wordWrap);
      if (n->wordWrap)
      {
         ModSlider("box width", &n->wrapWidth, 0.1f, 1.0f);
         ModSlider("box height", &n->wrapHeight, 0.1f, 1.0f);
         ModCheckbox("fit text to box", &n->fitToBox);
         if (n->fitToBox)
            ImGui::TextDisabled("size is a maximum - fitted to %.0f pt", n->FittedSize());
         ModSlider("line spacing", &n->lineSpacing, 0.5f, 2.5f);
      }
      ModSlider("outline", &n->outlineWidth, 0.0f, 20.0f);
      if (n->outlineWidth > 0.0f)
      {
         ColorSwatch("outline colour", n->outlineColor, n);
         ModCheckbox("outline only", &n->outlineOnly);
      }
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
   }


   void DrawVideoParams(VideoSourceNode* n)
   {
      if (ActionButton::Draw("Choose video...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         ImGui::PopTextWrapPos();
         ImGui::TextDisabled("%dx%d  %.1fs / %.1fs", n->GetOutputWidth(), n->GetOutputHeight(),
                             n->Position(), n->Duration());
      }
      ModCheckbox("loop", &n->loop);
      ModSlider("speed", &n->speed, -2.0f, 4.0f);
      {
         double dur = n->Duration();
         float sliderMax = (dur > 0.0) ? (float)dur : 1.0f;
         ModSlider("start", &n->trimStart, 0.0f, sliderMax);
         ModSlider("end", &n->trimEnd, 0.0f, sliderMax);
      }

      NodeSeparator();
      if (n->HasAudio())
      {
         ModCheckbox("audioEnabled", &n->audioEnabled);
         ModSlider("volume", &n->volume, 0.0f, 1.0f);
      }
      else if (!n->AudioError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("audio: %s", n->AudioError().c_str());
         ImGui::PopTextWrapPos();
      }
   }


   void DrawVideoInParams(VideoInNode* n)
   {
      ModCheckbox("active", &n->active);
      ImGui::SameLine();
      ModCheckbox("mirror", &n->mirror);

      n->RefreshDevices();
      const auto& devices = n->AvailableDevices();
      if (devices.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("No camera devices found.");
         ImGui::PopTextWrapPos();
      }
      else
      {
         std::vector<std::string> deviceNames;
         deviceNames.reserve(devices.size());
         int currentDevIdx = 0;
         for (size_t i = 0; i < devices.size(); i++)
         {
            std::string name = devices[i].localizedName;
            if (devices[i].isDefault)
               name += " (Default)";
            deviceNames.push_back(name);
            if (n->deviceId == devices[i].uniqueId || (n->deviceId.empty() && devices[i].isDefault))
               currentDevIdx = (int)i;
         }

         DropdownButton("device", deviceNames, currentDevIdx, [n, &devices](int idx) {
            if (idx >= 0 && idx < (int)devices.size())
               n->deviceId = devices[idx].uniqueId;
         });
      }

      DropdownButton("res", VideoInNode::ResolutionNames(), n->resolution, [n](int idx) {
         n->resolution = idx;
      });

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (n->IsRunning() && n->GetOutputWidth() > 0 && n->GetOutputHeight() > 0)
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%dx%d", n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
   }


   void DrawFitParams(FitNode* n)
   {
      DropdownButton("mode", FitNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModCheckbox("match input res", &n->matchInput);
      if (!n->matchInput)
      {
         ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
         ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      }
      ModSlider("offset X", &n->offsetX, -4096.0f, 4096.0f, "%.0f px");
      ModSlider("offset Y", &n->offsetY, -4096.0f, 4096.0f, "%.0f px");
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
   }


   void DrawProjectionHandleOverlay(ProjectionNode* node, ImVec2 origin, ImVec2 imageSize, const char* btnIdSuffix)
   {
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton(btnIdSuffix, imageSize);

      static int sDragRow = -1;
      static int sDragCol = -1;
      static ProjectionNode* sDragNode = nullptr;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      auto toScreen = [&](float nx, float ny) -> ImVec2 {
         return ImVec2(origin.x + nx * imageSize.x, origin.y + ny * imageSize.y);
      };

      const bool isCornerPin = (node->mode == (int)ProjectionNode::WarpMode::CornerPin);
      const int gw = isCornerPin ? 2 : std::max(2, std::min(8, node->gridW));
      const int gh = isCornerPin ? 2 : std::max(2, std::min(8, node->gridH));

      // Handle point selection & dragging
      if (ImGui::IsItemActive())
      {
         if (sDragNode != node || sDragRow < 0 || sDragCol < 0)
         {
            float nearestDist = 32.0f;
            int bestR = -1, bestC = -1;
            for (int r = 0; r < gh; ++r)
            {
               for (int c = 0; c < gw; ++c)
               {
                  ImVec2 pt = toScreen(node->points[r][c].x, node->points[r][c].y);
                  float d = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
                  if (d < nearestDist)
                  {
                     nearestDist = d;
                     bestR = r;
                     bestC = c;
                  }
               }
            }
            if (bestR >= 0 && bestC >= 0)
            {
               sDragNode = node;
               sDragRow = bestR;
               sDragCol = bestC;
            }
         }

         if (sDragNode == node && sDragRow >= 0 && sDragCol >= 0)
         {
            float nx = (mouse.x - origin.x) / std::max(1.0f, imageSize.x);
            float ny = (mouse.y - origin.y) / std::max(1.0f, imageSize.y);
            node->points[sDragRow][sDragCol].x = std::max(0.0f, std::min(1.0f, nx));
            node->points[sDragRow][sDragCol].y = std::max(0.0f, std::min(1.0f, ny));
         }
      }
      else if (ImGui::IsItemDeactivated())
      {
         PushUndoCheckpoint();
         sDragNode = nullptr;
         sDragRow = -1;
         sDragCol = -1;
      }

      // Draw connecting wireframe
      for (int r = 0; r < gh; ++r)
      {
         for (int c = 0; c < gw - 1; ++c)
         {
            ImVec2 pA = toScreen(node->points[r][c].x, node->points[r][c].y);
            ImVec2 pB = toScreen(node->points[r][c + 1].x, node->points[r][c + 1].y);
            dl->AddLine(pA, pB, tok::U32(tok::pal::c_FFB432C8), 1.5f);
         }
      }
      for (int c = 0; c < gw; ++c)
      {
         for (int r = 0; r < gh - 1; ++r)
         {
            ImVec2 pA = toScreen(node->points[r][c].x, node->points[r][c].y);
            ImVec2 pB = toScreen(node->points[r + 1][c].x, node->points[r + 1][c].y);
            dl->AddLine(pA, pB, tok::U32(tok::pal::c_FFB432C8), 1.5f);
         }
      }

      // Draw corner diagonals in corner pin mode for visual alignment
      if (isCornerPin)
      {
         ImVec2 pTL = toScreen(node->points[0][0].x, node->points[0][0].y);
         ImVec2 pTR = toScreen(node->points[0][1].x, node->points[0][1].y);
         ImVec2 pBR = toScreen(node->points[1][1].x, node->points[1][1].y);
         ImVec2 pBL = toScreen(node->points[1][0].x, node->points[1][0].y);
         dl->AddLine(pTL, pBR, tok::U32(tok::pal::c_FFB43246), 1.0f);
         dl->AddLine(pTR, pBL, tok::U32(tok::pal::c_FFB43246), 1.0f);
      }

      // Draw circular pin handles
      for (int r = 0; r < gh; ++r)
      {
         for (int c = 0; c < gw; ++c)
         {
            ImVec2 pt = toScreen(node->points[r][c].x, node->points[r][c].y);
            const bool isDragged = (sDragNode == node && sDragRow == r && sDragCol == c);
            float distToMouse = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
            const bool isHovered = (distToMouse < 18.0f);

            dl->AddCircleFilled(pt, 6.0f, tok::U32(tok::pal::c_FFB432FF));
            dl->AddCircle(pt, 7.0f, tok::U32(tok::pal::c_141419FF), 0, 1.5f);
            if (isDragged || isHovered)
               dl->AddCircle(pt, 11.0f, tok::U32(tok::pal::c_FFFFFFF0), 0, 2.0f);
         }
      }
   }


   void DrawProjectionPreview(ProjectionNode* node)
   {
      const float size = kViewportSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Checkerboard backdrop
      DrawCheckerboardBackdrop(dl, origin, size);

      if (node->GetOutputTexture() != 0)
      {
         dl->AddImage((ImTextureID)(intptr_t)node->GetOutputTexture(), origin,
                      ImVec2(origin.x + size, origin.y + size), ImVec2(0, 1), ImVec2(1, 0));
      }

      char btnId[32];
      snprintf(btnId, sizeof(btnId), "##projinlinenode%p", (void*)node);
      DrawProjectionHandleOverlay(node, origin, ImVec2(size, size), btnId);
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size), tok::U32(tok::pal::c_5A82BEFF), 4.0f, 0, 2.0f);

      // Reserve space so the eye toggle and params below aren't drawn over the preview!
      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(size, size));
   }


   void DrawProjectionParams(ProjectionNode* n)
   {
      const float colW = kViewportSize;
      static const std::vector<std::string> kModes = { "Corner Pin (Perspective)", "Mesh Grid" };
      DropdownButton("mode", kModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; }, colW);
      DropdownButton("pattern", ProjectionNode::PatternNames(), n->patternMode,
                     [n](int i) { PushUndoCheckpoint(); n->patternMode = i; }, colW);

      // Through a temporary so the checkpoint is taken before the node
      // changes: ModCheckbox flips its bool in place, and checkpointing after
      // it made Undo restore the already-flipped value.
      bool matchInput = n->matchInput;
      bool matchInputUserChanged = false;
      if (ModCheckbox("match input res", &matchInput, &matchInputUserChanged))
      {
         if (matchInputUserChanged) // a cable flip writes the value but never checkpoints
            PushUndoCheckpoint();
         n->matchInput = matchInput;
      }

      if (!n->matchInput)
      {
         ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f", colW);
         ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f", colW);
      }

      if (n->mode == 1) // Mesh Grid
      {
         // No checkpoint here: ModSlider already pushes one on activation,
         // before the drag changes anything. The IsItemDeactivatedAfterEdit
         // checkpoint that used to follow each slider captured the grid AFTER
         // the drag, so the first Undo restored the edited size (a no-op).
         int gw = n->gridW;
         if (ModSliderInt("grid X", &gw, 2, 8, 100.0f))
            n->SetGridSize(gw, n->gridH);

         ImGui::SameLine(0.0f, 16.0f);
         int gh = n->gridH;
         if (ModSliderInt("grid Y", &gh, 2, 8, 100.0f))
            n->SetGridSize(n->gridW, gh);
      }

      const float btnW = (colW - 12.0f) * 0.25f;
      if (ActionButton::Draw("Reset Corners", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->ResetCorners();
      }
      ImGui::SameLine(0.0f, 4.0f);
      if (ActionButton::Draw("Reset All", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->ResetAllPoints();
      }
      ImGui::SameLine(0.0f, 4.0f);
      if (ActionButton::Draw("Flip H", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->FlipH();
      }
      ImGui::SameLine(0.0f, 4.0f);
      if (ActionButton::Draw("Flip V", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->FlipV();
      }
   }


   void DrawLFOParams(LFONode* n)
   {
      DropdownButton("shape", LFONode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 32.0f);
      ModSlider("phase", &n->phase, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }


   void DrawRandomParams(RandomNode* n)
   {
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 32.0f);
      ModSlider("smooth", &n->smooth, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
      ModSlider("seed", &n->seed, 0.0f, 200.0f);
   }


   void DrawPatternStepGrid(PatternNode* n)
   {
      const int count = std::clamp(n->EffectiveLength(), 1, PatternNode::kSteps);
      const int curStep = n->CurrentStep();
      const int groupSize = 4;
      const float size = kPreviewSize;
      const float height = 115.0f;
      const float gap = 2.0f;
      const float cellW = count > 1 ? (size - gap * (float)(count - 1)) / (float)count : size;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImVec2 br(origin.x + size, origin.y + height);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImFont* font = ImGui::GetFont();

      ImGui::InvisibleButton("##patterngrid", ImVec2(size, height));
      const bool hovered = ImGui::IsItemHovered();
      const bool itemActive = ImGui::IsItemActive();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      auto stepAt = [&](float mx) {
         const float stepSpan = cellW + gap;
         if (stepSpan <= 0.0f) return 0;
         int s = (int)((mx - origin.x) / stepSpan);
         return std::clamp(s, 0, count - 1);
      };

      if (ImGui::IsItemActivated())
      {
         PushUndoCheckpoint();
         gPatternGridDrag.node = n;
         gPatternGridDrag.lastStep = -1;
      }

      int hoverStep = -1;
      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         PushUndoCheckpoint();
         const int s = stepAt(mouse.x);
         if (s >= 0 && s < PatternNode::kSteps)
         {
            n->steps[s] = (n->steps[s] > 0.05f) ? 0.0f : 1.0f;
            hoverStep = s;
            gPatternGridDrag.lastStep = s;
         }
      }
      else if (itemActive && gPatternGridDrag.node == n)
      {
         const int s = stepAt(mouse.x);
         if (s >= 0 && s < PatternNode::kSteps)
         {
            if (ImGui::GetIO().KeyShift)
            {
               n->steps[s] = std::clamp(n->steps[s] - ImGui::GetIO().MouseDelta.y / height * 0.25f, 0.0f, 1.0f);
            }
            else
            {
               const float t = 1.0f - std::clamp((mouse.y - origin.y) / height, 0.0f, 1.0f);
               const int lo = std::clamp(gPatternGridDrag.lastStep >= 0 ? std::min(gPatternGridDrag.lastStep, s) : s, 0, count - 1);
               const int hi = std::clamp(gPatternGridDrag.lastStep >= 0 ? std::max(gPatternGridDrag.lastStep, s) : s, 0, count - 1);
               for (int i = lo; i <= hi; i++)
                  if (i >= 0 && i < PatternNode::kSteps)
                     n->steps[i] = t;
            }
            gPatternGridDrag.lastStep = s;
            hoverStep = s;
         }
      }
      else if (gPatternGridDrag.node == n)
      {
         gPatternGridDrag.node = nullptr;
         gPatternGridDrag.lastStep = -1;
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         PushUndoCheckpoint();
         const int s = stepAt(mouse.x);
         if (s >= 0 && s < PatternNode::kSteps)
         {
            n->steps[s] = 0.0f;
            hoverStep = s;
         }
      }
      if (hoverStep < 0 && hovered)
         hoverStep = stepAt(mouse.x);

      const bool isLight = IsThemeLight();

      // ---- 1) Container chassis background ----
      dl->AddRectFilled(origin, br, isLight ? tok::U32(tok::pal::c_ECF0F8FF) : tok::U32(tok::pal::c_101016FF), 4.0f);

      // ---- 2) Subtle horizontal guide lines (25%, 50%, 75%) ----
      const float y25 = origin.y + height * 0.25f;
      const float y50 = origin.y + height * 0.50f;
      const float y75 = origin.y + height * 0.75f;
      const ImU32 subGuideCol = isLight ? tok::U32(tok::pal::c_DADFEA96) : tok::U32(tok::pal::c_20232E96);
      dl->AddLine(ImVec2(origin.x + 2.0f, y25), ImVec2(br.x - 2.0f, y25), subGuideCol, 1.0f);
      dl->AddLine(ImVec2(origin.x + 2.0f, y50), ImVec2(br.x - 2.0f, y50), subGuideCol, 1.0f);
      dl->AddLine(ImVec2(origin.x + 2.0f, y75), ImVec2(br.x - 2.0f, y75), subGuideCol, 1.0f);

      auto DrawTextCentered = [&](const ImVec2& minP, const ImVec2& maxP, ImU32 col, const char* text, float fSz) {
         const ImVec2 sz = font->CalcTextSizeA(fSz, FLT_MAX, 0.0f, text);
         const float tx = minP.x + std::max(0.0f, (maxP.x - minP.x - sz.x) * 0.5f);
         const float ty = minP.y + std::max(0.0f, (maxP.y - minP.y - sz.y) * 0.5f);
         dl->AddText(font, fSz, ImVec2(tx, ty), col, text);
      };

      // ---- 3) Step columns ----
      for (int i = 0; i < count; i++)
      {
         const float x0 = origin.x + (float)i * (cellW + gap);
         const float x1 = x0 + cellW;
         const bool isCurrent = (i == curStep) && curStep < count;
         const bool isGroupStart = (i % groupSize) == 0;
         const bool isHoveredCol = (hoverStep == i);

         // Track lane background
         const ImU32 gridCol = isLight
            ? (isGroupStart ? tok::U32(tok::pal::c_D4DAE6FF) : tok::U32(tok::pal::c_E0E4EEFF))
            : (isGroupStart ? tok::U32(tok::pal::c_20232EFF) : tok::U32(tok::pal::c_161820FF));
         dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y), gridCol, 2.0f);

         // Column hover or playhead background wash
         if (isCurrent)
         {
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? tok::U32(tok::pal::c_FFC85032) : tok::U32(tok::pal::c_FFBE5023), 2.0f);
         }
         else if (isHoveredCol)
         {
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? tok::U32(tok::pal::c_0000000A) : tok::U32(tok::pal::c_FFFFFF0C), 2.0f);
         }

         const float v = std::clamp(n->steps[i], 0.0f, 1.0f);
         const float fillTop = br.y - v * height;
         const float fillBottom = br.y;

         // Bar stem fill
         const ImU32 fillCol = isCurrent
            ? (isLight ? tok::U32(tok::pal::c_EB911EF0) : tok::U32(tok::pal::c_FFBE50F0))
            : (isLight ? tok::U32(tok::pal::c_2378EBE6) : tok::U32(tok::pal::c_4BA5FFDC));

         if (std::fabs(fillBottom - fillTop) > 1.0f)
         {
            dl->AddRectFilled(ImVec2(x0 + 1.0f, fillTop), ImVec2(x1 - 1.0f, fillBottom), fillCol, 2.0f);
         }

         // Value Cap Pill at the level mark
         const float capH = 14.0f;
         const float capY = std::clamp(fillTop, origin.y, br.y - capH);

         const ImU32 capCol = isCurrent
            ? (isLight ? tok::U32(tok::pal::c_F59B19FF) : tok::U32(tok::pal::c_FFC850FF))
            : (isLight ? tok::U32(tok::pal::c_2882F0FF) : tok::U32(tok::pal::c_5FB9FFFF));
         const ImU32 capBorder = isCurrent
            ? (isLight ? tok::U32(tok::pal::c_FFE68CFF) : tok::U32(tok::pal::c_FFF5B4FF))
            : (isLight ? tok::U32(tok::pal::c_A0CDFFC8) : tok::U32(tok::pal::c_B4E6FFB4));

         dl->AddRectFilled(ImVec2(x0 + 1.0f, capY), ImVec2(x1 - 1.0f, capY + capH), capCol, 2.5f);
         dl->AddRect(ImVec2(x0 + 1.0f, capY), ImVec2(x1 - 1.0f, capY + capH), capBorder, 2.5f);

         // Display value inside cap if column is wide enough
         if (cellW >= 18.0f)
         {
            char valStr[16];
            if (cellW >= 30.0f)
               snprintf(valStr, sizeof(valStr), "%.2f", v);
            else
               snprintf(valStr, sizeof(valStr), "%.1f", v);

            const float fontValSz = count <= 8 ? 9.5f : 8.5f;
            const ImU32 txtCol = isCurrent ? tok::U32(tok::pal::c_140F05FF) : tok::U32(tok::pal::c_0A1423FF);
            DrawTextCentered(ImVec2(x0 + 1.0f, capY), ImVec2(x1 - 1.0f, capY + capH), txtCol, valStr, fontValSz);
         }

         // Playhead outline
         if (isCurrent)
         {
            dl->AddRect(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                        isLight ? tok::U32(tok::pal::c_E18214F0) : tok::U32(tok::pal::c_FFD250E6), 2.0f, 0, 1.5f);
         }

         // Step number label below column
         char label[8];
         snprintf(label, sizeof(label), "%d", i + 1);
         const float labelFontSize = count <= 8 ? ImGui::GetFontSize()
                                                 : std::clamp(cellW * 0.95f, 8.0f, ImGui::GetFontSize());
         const ImVec2 textSize = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
         const ImU32 stepNumCol = isCurrent
            ? (isLight ? tok::U32(tok::pal::c_E18214FF) : tok::U32(tok::pal::c_FFC850FF))
            : (isLight
                  ? (isGroupStart ? tok::U32(tok::pal::c_283041FF) : tok::U32(tok::pal::c_6E7484FF))
                  : (isGroupStart ? tok::U32(tok::pal::c_BEC2D2FF) : tok::U32(tok::pal::c_6E7282FF)));
         dl->AddText(font, labelFontSize, ImVec2(x0 + (cellW - textSize.x) * 0.5f, br.y + 3.0f),
                     stepNumCol, label);
      }

      // Outer border
      dl->AddRect(origin, br, isLight ? tok::U32(tok::pal::c_B9C0D0FF) : tok::U32(tok::pal::c_414655FF), 4.0f);

      const float labelRowH = ImGui::GetFontSize() + 5.0f;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, br.y + labelRowH));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
      if (hoverStep >= 0)
      {
         ImGui::TextDisabled("step %d: %.2f", hoverStep + 1, n->steps[hoverStep]);
      }
      else
      {
         ImGui::TextDisabled("%d steps, looped", count);
      }
      ImGui::PopTextWrapPos();
   }


   void DrawPatternParams(PatternNode* n)
   {
      DrawPatternStepGrid(n);

      ModSliderInt("length", &n->length, 1, PatternNode::kSteps);
      ModSlider("beats / step", &n->stepBeats, 0.05f, 8.0f);
      ModCheckbox("glide between steps", &n->smoothSteps);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }


   void DrawMathParams(MathNode* n)
   {
      DropdownButton("operation", MathNode::OpNames(), n->op, [n](int i) { n->op = i; });
      if (n->inputA == nullptr)
         ModSlider("A (no cable)", &n->constantA, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("A: patched");
      if (n->inputB == nullptr)
         ModSlider("B (no cable)", &n->constantB, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("B: patched");
      ModSlider("gain", &n->gain, -4.0f, 4.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ModCheckbox("clamp to 0..1", &n->clampOutput);
   }


   void DrawCompareParams(CompareNode* n)
   {
      DropdownButton("operation", CompareNode::OpNames(), n->op, [n](int i) { n->op = i; });
      if (n->inputA == nullptr)
         ModSlider("A (no cable)", &n->constantA, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("A: patched");
      if (n->inputB == nullptr)
         ModSlider("B (no cable)", &n->constantB, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("B: patched");
      if (n->op == 4 || n->op == 5) // == or !=
         ModSlider("tolerance", &n->tolerance, 0.0f, 0.1f);
   }


   void DrawRangeToRangeParams(RangeToRangeNode* n)
   {
      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");
      ModSlider("in low", &n->inLow, -4.0f, 4.0f);
      ModSlider("in high", &n->inHigh, -4.0f, 4.0f);
      ModSlider("out low", &n->outLow, -4.0f, 4.0f);
      ModSlider("out high", &n->outHigh, -4.0f, 4.0f);
      ModCheckbox("clamp to out range", &n->clampOutput);
   }


   void DrawSmoothParams(SmoothNode* n)
   {
      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");
      ModSlider("amount", &n->amount, 0.0f, 0.99f);
   }


   void DrawInvertParams(InvertNode* n)
   {
      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");
      ModSlider("low", &n->low, -4.0f, 4.0f);
      ModSlider("high", &n->high, -4.0f, 4.0f);
   }


   void DrawModDepthParams(ModDepthNode* n)
   {
      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");
      ModSlider("depth", &n->depth, -1.0f, 1.0f);
   }


   void DrawNoiseParams(NoiseNode* n)
   {
      DropdownButton("type", NoiseNode::TypeNames(), n->noiseType, [n](int i) { n->noiseType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("scale", &n->scale, 0.5f, 60.0f);
      ModSlider("octaves", &n->octaves, 1.0f, 8.0f, "%.0f");
      ModSlider("lacunarity", &n->lacunarity, 1.0f, 4.0f);
      ModSlider("gain", &n->gain, 0.1f, 0.9f);
      ModSlider("warp", &n->warp, 0.0f, 2.0f);
      ModSlider("speed", &n->speed, -2.0f, 2.0f);
      ModSlider("contrast", &n->contrast, 0.1f, 4.0f);
      ModSlider("brightness", &n->brightness, -0.5f, 0.5f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
      ModCheckbox("rgb noise", &n->colorNoise);
      if (!n->colorNoise)
      {
         ColorSwatch("low", n->lowColor, n);
         ColorSwatch("high", n->highColor, n);
      }
   }


   void DrawTextureParams(TextureNode* n)
   {
      DropdownButton("type", TextureNode::TypeNames(), n->textureType, [n](int i) { n->textureType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("scale", &n->scale, 0.5f, 60.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);

      if (n->textureType == 0) // Voronoi
      {
         DropdownButton("distance", TextureNode::VoronoiDistanceNames(), n->voronoiDistance, [n](int i) { n->voronoiDistance = i; });
         DropdownButton("feature", TextureNode::VoronoiFeatureNames(), n->voronoiFeature, [n](int i) { n->voronoiFeature = i; });
         if (n->voronoiDistance == 3)
            ModSlider("minkowski exponent", &n->voronoiMinkowskiExponent, 0.1f, 8.0f);
         if (n->voronoiFeature == 2)
            ModSlider("smoothness", &n->voronoiSmoothness, 0.001f, 1.0f);
         ModSlider("randomness", &n->voronoiRandomness, 0.0f, 1.0f);
         ModCheckbox("cell color", &n->voronoiCellColor);
      }
      else if (n->textureType == 1) // Brick
      {
         ModSlider("brick width", &n->brickWidth, 0.05f, 2.0f);
         ModSlider("row height", &n->brickHeight, 0.05f, 2.0f);
         ModSlider("row offset", &n->brickRowOffset, 0.0f, 1.0f);
         ModSlider("mortar size", &n->brickMortarSize, 0.0f, 0.2f);
         ModSlider("mortar smooth", &n->brickMortarSmooth, 0.0f, 1.0f);
         ModSlider("bias", &n->brickBias, -0.5f, 0.5f);
         ColorSwatch("mortar", n->mortarColor, n);
      }
      else if (n->textureType == 2) // Magic
      {
         ModSlider("depth", &n->magicDepth, 1.0f, 10.0f, "%.0f");
         ModSlider("distortion", &n->magicDistortion, 0.0f, 4.0f);
      }
      else if (n->textureType == 3) // Wave
      {
         DropdownButton("wave type", TextureNode::WaveTypeNames(), n->waveType, [n](int i) { n->waveType = i; });
         if (n->waveType == 0)
            DropdownButton("bands direction", TextureNode::WaveBandsDirectionNames(), n->waveBandsDirection, [n](int i) { n->waveBandsDirection = i; });
         DropdownButton("profile", TextureNode::WaveProfileNames(), n->waveProfile, [n](int i) { n->waveProfile = i; });
         ModSlider("distortion", &n->waveDistortion, 0.0f, 4.0f);
         ModSlider("detail", &n->waveDetail, 1.0f, 8.0f, "%.0f");
         ModSlider("detail scale", &n->waveDetailScale, 0.1f, 8.0f);
         ModSlider("phase offset", &n->wavePhaseOffset, 0.0f, 1.0f);
      }
      else if (n->textureType == 4) // Musgrave
      {
         DropdownButton("musgrave type", TextureNode::MusgraveTypeNames(), n->musgraveType, [n](int i) { n->musgraveType = i; });
         ModSlider("dimension", &n->musgraveDimension, 0.0f, 4.0f);
         ModSlider("lacunarity", &n->musgraveLacunarity, 1.001f, 6.0f);
         ModSlider("octaves", &n->musgraveOctaves, 1.0f, 8.0f, "%.0f");
         if (n->musgraveType == 3)
            ModSlider("gain", &n->musgraveGain, 0.0f, 4.0f);
         if (n->musgraveType >= 2)
            ModSlider("offset", &n->musgraveOffset, 0.0f, 4.0f);
      }
      else if (n->textureType == 5) // Checker
      {
         // No extra params - scale/seed above already control cell size.
      }
      else if (n->textureType == 6) // Gradient
      {
         DropdownButton("gradient type", TextureNode::GradientTypeNames(), n->gradientType, [n](int i) { n->gradientType = i; });
      }
      else if (n->textureType == 7) // Clouds
      {
         ModSlider("depth", &n->cloudsDepth, 1.0f, 8.0f, "%.0f");
         ModCheckbox("hard", &n->cloudsHard);
      }
      else if (n->textureType == 8) // Marble
      {
         DropdownButton("marble type", TextureNode::MarbleTypeNames(), n->marbleType, [n](int i) { n->marbleType = i; });
         ModSlider("turbulence", &n->marbleTurbulence, 0.0f, 20.0f);
         ModSlider("noise scale", &n->marbleNoiseScale, 0.1f, 8.0f);
         ModSlider("noise depth", &n->marbleNoiseDepth, 1.0f, 8.0f, "%.0f");
      }
      else // Wood
      {
         DropdownButton("wood type", TextureNode::WoodTypeNames(), n->woodType, [n](int i) { n->woodType = i; });
         ModSlider("turbulence", &n->woodTurbulence, 0.0f, 20.0f);
         ModSlider("noise scale", &n->woodNoiseScale, 0.1f, 8.0f);
      }

      ModSlider("contrast", &n->contrast, 0.1f, 4.0f);
      ModSlider("brightness", &n->brightness, -0.5f, 0.5f);
      if (n->textureType != 1 && !(n->textureType == 0 && n->voronoiCellColor) && n->textureType != 2)
      {
         ColorSwatch("low", n->lowColor, n);
         ColorSwatch("high", n->highColor, n);
      }
   }


   void DrawSwitcherParams(SwitcherNode* n)
   {
      DropdownButton("unit", SwitcherNode::UnitNames(), n->unit, [n](int i) { n->unit = i; });
      ModSlider("every", &n->interval, 0.05f, 32.0f);
      ModSlider("crossfade", &n->crossfade, 0.0f, 0.99f);
      ModCheckbox("manual", &n->manual);
      if (n->manual)
      {
         ModSliderInt("slot", &n->manualSlot, 0, SwitcherNode::kSlots - 1);
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }


   // 2D control surface. Dragging the orb sweeps the mutation weights; the
   // recorded path is drawn behind it so a loop is visible while it plays.
   void DrawFxPad(ResynthNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##fxpad", ImVec2(size, size));
      const bool active = ImGui::IsItemActive();

      if (active)
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);

      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y),
                     ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f),
                     ScopeGridCol());
      }

      const std::vector<ResynthNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, isLight ? tok::U32(tok::pal::c_1E78E6DC) : tok::U32(tok::pal::c_78C8FFAA), 1.6f);
      }

      // Corner labels: the pad blends between these four named effects, so it is
      // obvious what is being swept rather than four anonymous weights.
      const ImU32 labelCol = isLight ? tok::U32(tok::pal::c_323A4BFF) : tok::U32(tok::pal::c_969CB4FF);
      const char* bl = n->CornerLabel(0);
      const char* brName = n->CornerLabel(1);
      const char* tl = n->CornerLabel(2);
      const char* trName = n->CornerLabel(3);
      dl->AddText(ImVec2(origin.x + 5, origin.y + 4), labelCol, tl);
      ImVec2 trSize = ImGui::CalcTextSize(trName);
      dl->AddText(ImVec2(br.x - trSize.x - 5, origin.y + 4), labelCol, trName);
      ImVec2 blSize = ImGui::CalcTextSize(bl);
      dl->AddText(ImVec2(origin.x + 5, br.y - blSize.y - 4), labelCol, bl);
      ImVec2 brSize = ImGui::CalcTextSize(brName);
      dl->AddText(ImVec2(br.x - brSize.x - 5, br.y - brSize.y - 4), labelCol, brName);

      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? tok::U32(tok::pal::c_FF5A5AFF)
                     : n->IsPlayingPath()   ? (isLight ? tok::U32(tok::pal::c_1EB450FF) : tok::U32(tok::pal::c_78EB96FF))
                                            : (isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFBE5AFF));
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, isLight ? tok::U32(tok::pal::c_F0F0F0FF) : tok::U32(tok::pal::c_14141CFF), 0, 2.0f);
      AudioViz::Border(dl, origin, br);
   }


   void DrawResynthParams(ResynthNode* n)
   {
      DrawFxPad(n);

      if (n->IsRecordingPath())
      {
         if (ActionButton::Draw("Stop rec", ImVec2(kPreviewSize * 0.48f, 0), ActionButton::Kind::Record))
            n->StopRecording();
      }
      else
      {
         if (ActionButton::Draw("Rec path", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingPath())
      {
         if (ActionButton::Draw("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPath();
      }
      else
      {
         if (ActionButton::Draw("Play path", ImVec2(kPreviewSize * 0.48f, 0)))
            n->PlayPath();
      }
      ModCheckbox("loop path", &n->loopPath);
      ImGui::SameLine();
      if (ActionButton::Draw("clear"))
         n->ClearPath();

      NodeSeparator();
      DropdownButton("mode", ResynthNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModSlider("chaos", &n->chaos, 0.0f, 1.0f);
      ModSlider("mutation", &n->mutation, 0.0f, 1.0f);
      ModSlider("feedback", &n->feedback, 0.0f, 1.0f);
      ModSlider("source pull", &n->sourcePull, 0.0f, 1.0f);

      NodeSeparator();
      ImGui::TextDisabled("generation %d", n->Generation());
      if (ActionButton::Draw("Iterate", ImVec2(kPreviewSize * 0.48f, 0)))
         n->StepOnce();
      ImGui::SameLine();
      if (ActionButton::Draw("Reset", ImVec2(kPreviewSize * 0.48f, 0)))
         n->Reset();
      if (ActionButton::Draw("Randomise FX", ImVec2(kPreviewSize, 0)))
         n->Randomise();

      if (ImGui::TreeNode("pad corners"))
      {
         static const char* kCornerNames[4] = { "bottom left", "bottom right", "top left", "top right" };
         for (int c = 0; c < ResynthNode::kCorners; c++)
         {
            ImGui::PushID(c);
            char label[32];
            snprintf(label, sizeof(label), "%s##c%d", kCornerNames[c], c);
            DropdownButton(label, ResynthNode::EffectNames(), n->cornerEffect[c],
                           [n, c](int i) { n->cornerEffect[c] = i; });
            ModSlider("amount", &n->cornerAmount[c], 0.0f, 1.0f);
            ImGui::PopID();
         }
         ImGui::TreePop();
      }
      ModCheckbox("auto iterate", &n->autoIterate);
      if (n->autoIterate)
         ModSlider("steps / beat", &n->stepsPerBeat, 0.05f, 16.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
   }


   // ---- macro node bodies -------------------------------------------------
   // Every macro body is drawn the same way: the control sits flush at the
   // top-left of its cell, its label is centred directly underneath, and the
   // cell is reserved afterwards so the node sizes to the control instead of
   // to a 190px image preview that isn't there. MacroBodyEnd is the second
   // half of that contract for the hand-drawn controls; the knob and fader
   // widgets already do it themselves (see KnobFloat's tail).
   void MacroBodyEnd(const ImVec2& origin, float cellW, float contentH, const std::string& caption)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImU32 capCol = isLight ? tok::U32(tok::pal::c_323746FF) : tok::U32(tok::pal::c_B0B6C6FF);
      const float textH = ImGui::GetTextLineHeight();
      const float capY = origin.y + contentH + 4.0f;
      if (!caption.empty())
      {
         const ImVec2 ts = ImGui::CalcTextSize(caption.c_str());
         if (ts.x <= cellW)
         {
            dl->AddText(ImVec2(origin.x + (cellW - ts.x) * 0.5f, capY), capCol, caption.c_str());
         }
         else
         {
            // Clip rather than let a long user-typed label widen the node.
            dl->PushClipRect(ImVec2(origin.x, capY), ImVec2(origin.x + cellW, capY + textH), true);
            dl->AddText(ImVec2(origin.x, capY), capCol, caption.c_str());
            dl->PopClipRect();
         }
      }
      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(cellW, contentH + 4.0f + textH));
   }


   // Same diameter every other audio/modulator knob in the app uses
   // (kKnobStd): KnobFloat's face shading and tick are pixel-offset constants
   // tuned for that size, so scaling the diameter up on its own (108 in an
   // earlier pass) didn't make a bigger knob, it made a flat disc with a thin
   // ring. What changed is the *cell*, not the knob - kMacroCell instead of
   // kPreviewSize.
   void DrawMacroKnobBody(MacroKnobNode* n)
   {
      const std::string caption = n->label.empty() ? std::string("macro") : n->label;
      ModKnob(caption.c_str(), &n->value, 0.0f, 1.0f, "%.3f", kKnobStd, kMacroCell);
   }


   // The name field is the only param most macro nodes have, and it used to be
   // kParamWidth (168px) plus a visible "name" label - together wider than any
   // macro body, so opening params stretched the node and left the dead space
   // around the control that the body metrics had just removed. Sized to the
   // node's own body cell instead, with the word "name" as a placeholder
   // rather than a label taking layout width.
   void MacroNameField(std::string& label, float cellW)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "%s", label.c_str());
      ImGui::SetNextItemWidth(cellW);
      if (FieldWell::InputTextWithHint("##macroname", "name", buf, sizeof(buf)))
         label = buf;
   }


   void DrawMacroKnobParams(MacroKnobNode* n)
   {
      MacroNameField(n->label, kMacroCell);
   }


   void DrawMacroSliderBody(MacroSliderNode* n)
   {
      // kMacroFaderH, not kPreviewSize - 12: a 178px fader beside a 56px knob
      // was the single worst size mismatch in the family, and nothing about a
      // 0..1 macro needs that much travel.
      const std::string caption = n->label.empty() ? std::string("slider") : n->label;
      ModKnob(caption.c_str(), &n->value, 0.0f, 1.0f, "%.2f", kMacroFaderH, kMacroCell, AudioWidgetStyle::VFader);
   }


   void DrawMacroSliderParams(MacroSliderNode* n)
   {
      MacroNameField(n->label, kMacroCell);
   }


   void DrawMacroBipolarKnobBody(MacroBipolarKnobNode* n)
   {
      const std::string caption = n->label.empty() ? std::string("bipolar") : n->label;
      ModKnob(caption.c_str(), &n->value, -1.0f, 1.0f, "%.2f", kKnobStd, kMacroCell, AudioWidgetStyle::KnobBipolar);
   }


   void DrawMacroBipolarKnobParams(MacroBipolarKnobNode* n)
   {
      MacroNameField(n->label, kMacroCell);
   }


   void DrawMacroToggleBody(MacroToggleNode* n)
   {
      const float btnH = kMacroRowH + 4.0f;   // a switch reads as a switch only if it has some body
      const float btnW = 72.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 bTL(origin.x + (kMacroCell - btnW) * 0.5f, origin.y);
      const ImVec2 bBR(bTL.x + btnW, bTL.y + btnH);

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();

      ImGui::SetCursorScreenPos(bTL);
      ImGui::PushID(901);
      if (ImGui::InvisibleButton("##macrotogglebtn", ImVec2(btnW, btnH)))
      {
         PushUndoCheckpoint();
         n->state = !n->state;
      }
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();

      const float rad = btnH * 0.5f;
      const ImU32 bgCol = n->state
         ? (isLight ? tok::U32(tok::pal::c_22C55EFF) : tok::U32(tok::pal::c_16A34AFF))
         : (isLight ? tok::U32(tok::pal::c_D7DEEBFF) : tok::U32(tok::pal::c_242834FF));
      dl->AddRectFilled(bTL, bBR, bgCol, rad);
      dl->AddRect(bTL, bBR, hovered ? tok::U32(tok::pal::c_FFFFFF78)
                                    : (isLight ? tok::U32(tok::pal::c_B4BECDFF) : tok::U32(tok::pal::c_373E4EFF)),
                  rad, 0, 1.2f);

      const float thumbR = rad - 4.0f;
      const float thumbX = n->state ? (bBR.x - rad) : (bTL.x + rad);
      const float thumbY = bTL.y + rad;
      dl->AddCircleFilled(ImVec2(thumbX, thumbY), thumbR, tok::U32(tok::pal::c_FFFFFFFF));
      dl->AddCircle(ImVec2(thumbX, thumbY), thumbR,
                    isLight ? tok::U32(tok::pal::c_B4B4B4FF) : tok::U32(tok::pal::c_282832FF), 0, 1.0f);

      const char* text = n->state ? "ON" : "OFF";
      const ImVec2 tSize = ImGui::CalcTextSize(text);
      const float textX = n->state ? (bTL.x + (rad * 2.0f - tSize.x) * 0.5f)
                                   : (bBR.x - rad * 2.0f + (rad * 2.0f - tSize.x) * 0.5f);
      dl->AddText(ImVec2(textX, bTL.y + (btnH - tSize.y) * 0.5f),
                  n->state ? tok::U32(tok::pal::c_FFFFFFFF)
                           : (isLight ? tok::U32(tok::pal::c_5A6478FF) : tok::U32(tok::pal::c_A0AABEFF)),
                  text);

      MacroBodyEnd(origin, kMacroCell, btnH, n->label.empty() ? std::string("toggle") : n->label);
   }


   void DrawMacroToggleParams(MacroToggleNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      ModCheckbox("state", &n->state);
   }


   void DrawMacroTriggerBody(MacroTriggerNode* n)
   {
      // Sized so the pad's overall footprint (2 * (r + bezel)) matches
      // kKnobStd - a bang and a knob are peers on a front panel and must not
      // differ in size for no reason.
      const float r = 22.0f;
      const float bezel = 3.0f;
      const float contentH = (r + bezel) * 2.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 center(origin.x + kMacroCell * 0.5f, origin.y + contentH * 0.5f);

      ImGui::SetCursorScreenPos(ImVec2(center.x - r - bezel, origin.y));
      ImGui::PushID(902);
      ImGui::InvisibleButton("##macrotriggerbtn", ImVec2(contentH, contentH));
      n->pressed = ImGui::IsItemActive();
      if (ImGui::IsItemActivated())
      {
         n->justTriggered = true;
         n->flash = 1.0f;
      }
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();

      if (n->pressed)
         n->flash = 1.0f;
      else if (n->flash > 0.0f)
      {
         n->flash -= ImGui::GetIO().DeltaTime * 4.0f;
         if (n->flash < 0.0f) n->flash = 0.0f;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();

      // Outer bezel ring
      dl->AddCircleFilled(center, r + bezel, isLight ? tok::U32(tok::pal::c_D2DAE6FF) : tok::U32(tok::pal::c_1E222CFF), 32);
      dl->AddCircle(center, r + bezel, isLight ? tok::U32(tok::pal::c_AFB9C8FF) : tok::U32(tok::pal::c_323846FF), 32, 1.2f);

      // Inner pad
      if (n->flash > 0.0f)
      {
         ImU32 flashCol = IM_COL32(245, 158, 11, (int)(n->flash * 255.0f));
         dl->AddCircleFilled(center, r, flashCol, 32);
         dl->AddCircle(center, r, tok::U32(tok::pal::c_FFE664FF), 32, 2.0f);
      }
      else
      {
         ImU32 padCol = isLight ? tok::U32(tok::pal::c_EBF0FAFF) : tok::U32(tok::pal::c_2A303EFF);
         dl->AddCircleFilled(center, r, padCol, 32);
         dl->AddCircle(center, r, isLight ? tok::U32(tok::pal::c_BEC8D7FF) : tok::U32(tok::pal::c_3C4455FF), 32, 1.0f);
      }
      if (hovered)
         dl->AddCircle(center, r + 2.0f, tok::U32(tok::pal::c_FFFFFF50), 32, 1.0f);

      // Centred dot
      dl->AddCircleFilled(center, 4.5f,
                          n->flash > 0.0f ? tok::U32(tok::pal::c_FFFFFFFF)
                                          : (isLight ? tok::U32(tok::pal::c_8C96AAFF) : tok::U32(tok::pal::c_505A6EFF)), 16);

      MacroBodyEnd(origin, kMacroCell, contentH, n->label.empty() ? std::string("bang") : n->label);
   }


   void DrawMacroTriggerParams(MacroTriggerNode* n)
   {
      MacroNameField(n->label, kMacroCell);
   }


   void DrawMacroNumBoxBody(MacroNumBoxNode* n)
   {
      const float boxW = kMacroCell - 8.0f;
      const float boxH = kMacroRowH;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 bTL(origin.x + 4.0f, origin.y);
      const ImVec2 bBR(bTL.x + boxW, bTL.y + boxH);

      (void)bBR;
      // The shared field well (DragFloat frame in the FieldWell style), same as every numeric field.
      ImGui::SetCursorScreenPos(ImVec2(bTL.x, bTL.y + (boxH - ImGui::GetFrameHeight()) * 0.5f));
      // Unbounded: no min/max params, so drag speed scales with the value's
      // own magnitude and the DragFloat gets no clamp (v_min >= v_max).
      float speed = std::max(0.01f, std::abs(n->value) * 0.01f);
      FieldWell::PushStyle(ImGui::GetID("##macronumboxdrag"));
      ImGui::SetNextItemWidth(boxW);
      ImGui::DragFloat("##macronumboxdrag", &n->value, speed, 0.0f, 0.0f,
                       (std::abs(n->value) >= 10.0f) ? "%.1f" : "%.3f");
      FieldWell::PopStyle();

      MacroBodyEnd(origin, kMacroCell, boxH, n->label.empty() ? std::string("value") : n->label);
   }


   void DrawMacroNumBoxParams(MacroNumBoxNode* n)
   {
      MacroNameField(n->label, kMacroCell);
      // No min/max: the number box takes any value the user types, and each
      // destination clamps it to its own declared range on the way in (see
      // the MacroNumBox case in the modulation apply loop).
   }


   void DrawMacroRadioSelectorBody(MacroRadioSelectorNode* n)
   {
      const int count = std::clamp(n->count, 2, 8);
      const float gap = 2.0f;
      const float btnH = kMacroRowH;
      const float btnW = (kMacroWideCell - (count - 1) * gap) / (float)count;
      const ImVec2 origin = ImGui::GetCursorScreenPos();

      const bool isLight = IsThemeLight();
      for (int b = 0; b < count; b++)
      {
         ImGui::SetCursorScreenPos(ImVec2(origin.x + b * (btnW + gap), origin.y));
         ImGui::PushID(b + 700);

         const std::string btnText = std::to_string(b + 1);
         if (ActionButton::Draw(btnText.c_str(), ImVec2(btnW, btnH),
                                b == n->selected ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
         {
            PushUndoCheckpoint();
            n->selected = b;
         }
         ImGui::PopID();
      }

      MacroBodyEnd(origin, kMacroWideCell, btnH, n->label.empty() ? std::string("selector") : n->label);
   }


   void DrawMacroRadioSelectorParams(MacroRadioSelectorNode* n)
   {
      MacroNameField(n->label, kMacroWideCell);
      // Clamped to 8 because the body only ever draws 8 segments - a count of
      // 16 used to leave half the selector unreachable.
      ModSliderInt("count", &n->count, 2, 8);
   }


   void DrawMacroStepGateBody(MacroStepGateNode* n)
   {
      const float gap = 2.0f;
      const float stepH = kMacroRowH;
      const float stepW = (kMacroWideCell - 7.0f * gap) / 8.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const int playStep = n->CurrentStep();

      for (int s = 0; s < 8; s++)
      {
         const float sx = origin.x + s * (stepW + gap);
         const ImVec2 sTL(sx, origin.y);
         const ImVec2 sBR(sx + stepW, origin.y + stepH);

         ImGui::SetCursorScreenPos(sTL);
         ImGui::PushID(s + 800);
         ImGui::InvisibleButton("##stepgatebtn", ImVec2(stepW, stepH));
         const bool hovered = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked())
         {
            PushUndoCheckpoint();
            n->pattern ^= (1 << s);
         }
         ImGui::PopID();

         const bool isOn = (n->pattern & (1 << s)) != 0;
         StepCell::Draw(dl, sTL, sBR, isOn, s == playStep && Transport::Instance().IsPlaying(), hovered);
      }

      MacroBodyEnd(origin, kMacroWideCell, stepH, n->label.empty() ? std::string("step gate") : n->label);
   }


   void DrawMacroStepGateParams(MacroStepGateNode* n)
   {
      MacroNameField(n->label, kMacroWideCell);
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 4.0f);
   }


   void DrawMidiCCParams(MidiCCNode* n)
   {
      if (n->IsLearning())
      {
         if (ActionButton::Draw("Listening... move a control", ImVec2(kPreviewSize, 0), ActionButton::Kind::Learn))
            n->CancelLearn();
      }
      else if (ActionButton::Draw(n->IsBound() ? "Re-learn" : "MIDI Learn", ImVec2(kPreviewSize, 0)))
      {
         MidiLearnCancelAll();
         n->StartLearn();
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      ImGui::Text("bound: %s", n->BindingLabel().c_str());
      ImGui::ProgressBar(n->Value01(), ImVec2(kPreviewSize * 0.6f, 0), "");

      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
      ModCheckbox("invert", &n->invert);
   }


   void DrawMidiTriggerParams(MidiTriggerNode* n)
   {
      DropdownButton("device type", MidiTriggerNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });

      if (n->IsLearning())
      {
         if (ActionButton::Draw("Listening... hit a pad", ImVec2(kPreviewSize, 0), ActionButton::Kind::Learn))
            n->CancelLearn();
      }
      else
      {
         const bool lit = n->Value01() > 0.01f;
         if (ActionButton::Draw(n->IsBound() ? "Re-learn" : "MIDI Learn", ImVec2(kPreviewSize, 0), (lit) ? ActionButton::Kind::Go : ActionButton::Kind::Plain))
         {
            MidiLearnCancelAll();
            n->StartLearn();
         }
      }
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      ImGui::Text("bound: %s", n->BindingLabel().c_str());
      ImGui::ProgressBar(n->Value01(), ImVec2(kPreviewSize * 0.6f, 0), "");

      if (n->mode == MidiTriggerNode::kKeyboard)
      {
         ImGui::TextDisabled("value = note number / 127, held until the next key");
      }
      else
      {
         ModSlider("hold", &n->hold, 0.02f, 2.0f);
         ModCheckbox("velocity sensitive", &n->velocitySensitive);
      }
   }


   void DrawMacroXYBody(MacroXYNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##macroxy", ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), ScopeGridCol());
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), ScopeGridCol());
      }
      const std::vector<MacroXYNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, isLight ? tok::U32(tok::pal::c_1E78E6DC) : tok::U32(tok::pal::c_78C8FFAA), 1.6f);
      }
      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? tok::U32(tok::pal::c_FF5A5AFF)
                     : n->IsPlayingPath()   ? (isLight ? tok::U32(tok::pal::c_1EB450FF) : tok::U32(tok::pal::c_78EB96FF))
                                            : (isLight ? tok::U32(tok::pal::c_E68C14FF) : tok::U32(tok::pal::c_FFBE5AFF));
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, isLight ? tok::U32(tok::pal::c_F0F0F0FF) : tok::U32(tok::pal::c_14141CFF), 0, 2.0f);
      AudioViz::Border(dl, origin, br);
   }


   void DrawMacroXYParams(MacroXYNode* n)
   {
      ImGui::TextDisabled("x %.3f   y %.3f", n->padX, n->padY);

      if (n->IsRecordingPath())
      {
         if (ActionButton::Draw("Stop rec", ImVec2(kPreviewSize * 0.48f, 0), ActionButton::Kind::Record))
            n->StopRecording();
      }
      else if (ActionButton::Draw("Rec path", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingPath())
      {
         if (ActionButton::Draw("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPath();
      }
      else if (ActionButton::Draw("Play path", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->PlayPath();
      }
      ModCheckbox("loop", &n->loopPath);
      ImGui::SameLine();
      if (ActionButton::Draw("clear"))
         n->ClearPath();
      ModSlider("speed", &n->speed, 0.05f, 4.0f);
   }
}

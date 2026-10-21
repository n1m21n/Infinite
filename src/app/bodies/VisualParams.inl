// Part of src/main.cpp, included there in order (one translation unit).
// per-node parameter UI of the visual nodes

   // ---------------- per-node parameter UI ----------------

   void DrawImageSourceParams(ImageSourceNode* n)
   {
      if (ImGui::Button("Choose image...", ImVec2(kPreviewSize, 0)))
         StartNodeFileDialog(n, Platform::OpenImageDialog, [](auto* m, const std::string& path) { m->Load(path); });

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
         size_t slash = file.find_last_of("/\\");
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
   }

   void DrawSyphonOutParams(SyphonOutNode* n)
   {
      ImGui::SetNextItemWidth(kPreviewSize);
      if (ImGui::InputText("##syphon_name", &n->serverNameInput, ImGuiInputTextFlags_EnterReturnsTrue))
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
         ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Broadcasting: %dx%d", n->PublishedWidth(), n->PublishedHeight());
         if (n->HasClients())
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "Clients: Active");
         else
            ImGui::TextDisabled("Clients: Waiting for app...");
      }
      else
      {
         ImGui::TextDisabled("Connect an image input to publish");
      }
      ImGui::PopTextWrapPos();
   }

   void DrawSyphonInParams(SyphonInNode* n)
   {
      if (ImGui::Button("Refresh Servers", ImVec2(kPreviewSize, 0)))
      {
         n->RefreshServers();
      }

      const auto& servers = n->AvailableServers();
      if (servers.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("No active Spout senders found.");
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
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Receiving: %dx%d", n->GetOutputWidth(), n->GetOutputHeight());
         }
         else
         {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Connecting...");
         }
         ImGui::PopTextWrapPos();
      }
   }

   void DrawOscReceiveParams(OscReceiveNode* n)
   {
      ModSliderInt("port", &n->port, 1, 65535);
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::InputText("address", &n->address);
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::DragInt("argument", &n->argIndex, 0.1f, 0, 15);
      // Any range: many controllers send 0..127 or -1..1.
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::DragFloat("low", &n->low, 0.01f, -100000.0f, 100000.0f, "%.3f");
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::DragFloat("high", &n->high, 0.01f, -100000.0f, 100000.0f, "%.3f");
      ImGui::TextDisabled("raw %.3f%s", n->RawValue(), n->PortOpen() ? "" : "  (port not open)");
      ImGui::TextDisabled("last: %s", OscHub::LastAddress(n->port).c_str());
   }

   void DrawOscToCvParams(OscToCvNode* n)
   {
      const float W = 300.0f;
      ImGui::SetNextItemWidth(120.0f);
      int port = n->port;
      if (ImGui::InputInt("port##osccvPort", &port, 0, 0))
         n->port = std::clamp(port, 1, 65535);
      ImGui::SameLine();
      ImGui::TextDisabled(n->PortOpen() ? "listening" : "port not open");
      {
         const std::string last = n->LastAddress();
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
         ImGui::TextDisabled("last: %s", last.empty() ? "-" : last.c_str());
         ImGui::PopTextWrapPos();
      }
      for (int c = 0; c < OscToCvNode::kChannels; c++)
      {
         ImGui::PushID(9700 + c);
         ImGui::Separator();
         ImGui::TextColored(n->Received(c) ? ImVec4(0.5f, 0.9f, 0.6f, 1.0f) : ImVec4(0.6f, 0.62f, 0.68f, 1.0f),
                            "cv%d", c + 1);
         ImGui::SameLine();
         ImGui::SetNextItemWidth(W - 110.0f);
         ImGui::InputTextWithHint("##addr", "/address", &n->address[c]);
         ImGui::SameLine();
         const bool learning = n->learnChannel == c;
         if (learning)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.15f, 1.0f));
         if (ImGui::Button(learning ? "..." : "learn", ImVec2(50.0f, 0)))
            n->learnChannel = learning ? -1 : c;
         if (learning)
            ImGui::PopStyleColor();
         ImGui::SetNextItemWidth(60.0f);
         ImGui::DragInt("##arg", &n->argIndex[c], 0.1f, 0, 15, "arg %d");
         ImGui::SameLine();
         ImGui::SetNextItemWidth(W - 150.0f);
         ImGui::DragFloatRange2("##range", &n->inMin[c], &n->inMax[c], 0.01f, -100000.0f, 100000.0f, "%.2f", "%.2f");
         ImGui::SameLine();
         ImGui::Checkbox("inv", &n->invert[c]);
         ImGui::SetNextItemWidth(90.0f);
         ImGui::DragFloat("##smooth", &n->smoothing[c], 0.005f, 0.0f, 0.99f, "smooth %.2f");
         ImGui::SameLine();
         const ImVec2 p0 = ImGui::GetCursorScreenPos();
         const float bw = W - 100.0f, bh = ImGui::GetFrameHeight() - 6.0f;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(ImVec2(p0.x, p0.y + 3.0f), ImVec2(p0.x + bw, p0.y + 3.0f + bh), IM_COL32(40, 44, 56, 255), 2.0f);
         dl->AddRectFilled(ImVec2(p0.x, p0.y + 3.0f), ImVec2(p0.x + bw * n->Value(c), p0.y + 3.0f + bh), IM_COL32(120, 190, 255, 255), 2.0f);
         char raw[32];
         snprintf(raw, sizeof(raw), "%.3f", n->Raw(c));
         dl->AddText(ImVec2(p0.x + 4.0f, p0.y + 2.0f), IM_COL32(230, 235, 245, 255), raw);
         ImGui::Dummy(ImVec2(bw, ImGui::GetFrameHeight()));
         ImGui::PopID();
      }
   }

   // Turbo: Send / Receive (Pure Data style). The channel name is the whole
   // connection, so it gets a text field plus a picker of names already in use.
   void DrawWirelessParams(WirelessNode* n)
   {
      char buf[128];
      snprintf(buf, sizeof(buf), "%s", n->channel.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("channel##wirelessName", buf, sizeof(buf)))
      {
         n->channel = buf;
         gPatchDirty = true;
      }
      if (ImGui::IsItemActivated())
         PushUndoCheckpoint();
      const std::vector<std::string> names = WirelessNode::ChannelNames();
      if (names.size() > 1)
      {
         int current = -1;
         for (int i = 0; i < (int)names.size(); i++)
            if (names[i] == n->channel)
               current = i;
         DropdownButton("pick##wirelessPick", names, std::max(0, current), [n, names](int i) {
            if (i < 0 || i >= (int)names.size() || names[i] == n->channel)
               return;
            PushUndoCheckpoint();
            n->channel = names[i];
            gPatchDirty = true;
         }, kParamWidth, false);
      }

      auto setter = [](int* field) {
         return [field](int i) {
            if (*field == i)
               return;
            if (!gApplyingDiscreteCv)
               PushUndoCheckpoint();
            *field = i;
            gPatchDirty = true;
         };
      };
      if (auto* r = dynamic_cast<ReceiveImageNode*>(n))
      {
         DropdownButton("combine", ReceiveImageNode::ModeNames(), r->mode, setter(&r->mode));
         if (r->mode == ReceiveImageNode::kIndex)
            ModSliderInt("index", &r->index, 0, ReceiveImageNode::kMaxSenders - 1);
      }
      else if (auto* r = dynamic_cast<ReceiveCVNode*>(n))
      {
         DropdownButton("combine", ReceiveCVNode::ModeNames(), r->mode, setter(&r->mode));
         ImGui::TextDisabled("value %.3f", r->Value01());
      }
      else if (auto* r = dynamic_cast<ReceiveAudioNode*>(n))
         ModSlider("gain", &r->gain, 0.0f, 2.0f);
      else if (auto* r = dynamic_cast<ReceiveGeometryNode*>(n))
         ModSliderInt("index", &r->index, 0, 15);

      // Who is on this channel, so a typo in a name shows up at once.
      const size_t sends = WirelessNode::Senders(n->GetKind(), n->channel).size();
      const size_t recvs = WirelessNode::Receivers(n->GetKind(), n->channel).size();
      if (n->IsSend())
         ImGui::TextDisabled("%zu %s receive%s on \"%s\"", recvs, WirelessNode::KindName(n->GetKind()),
                             recvs == 1 ? "" : "s", n->channel.c_str());
      else
         ImGui::TextDisabled("%zu %s send%s on \"%s\"", sends, WirelessNode::KindName(n->GetKind()),
                             sends == 1 ? "" : "s", n->channel.c_str());
      if (auto* r = dynamic_cast<ReceiveGeometryNode*>(n))
         if (sends > 0 && r->Source() == nullptr)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "nothing to forward: Send unpatched, or a loop");
   }

   void DrawOscSendParams(OscSendNode* n)
   {
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::InputText("host", &n->host);
      ModSliderInt("port", &n->port, 1, 65535);
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::InputText("address", &n->address);
      ModSlider("epsilon", &n->epsilon, 0.0f, 0.1f, "%.4f");
      ModSlider("interval (ms)", &n->intervalMs, 1.0f, 1000.0f, "%.0f");
      if (n->LastSent() < 0.0f)
         ImGui::TextDisabled("not sent yet");
      else
         ImGui::TextDisabled("last sent: %.3f", n->LastSent());
   }

   void DrawEnvironmentParams(EnvironmentNode* n)
   {
      if (ImGui::Button("Choose HDRI...", ImVec2(kPreviewSize, 0)))
         StartNodeFileDialog(n, Platform::OpenHdrDialog, [](auto* m, const std::string& path) { m->Load(path); });

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of("/\\");
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
      DropdownButton("shape", ShapeNode::ShapeNames(), n->shapeType,
                     [n](int i) { n->shapeType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("size x", &n->sizeX, 0.01f, 1.0f);
      ModSlider("size y", &n->sizeY, 0.01f, 1.0f);
      ModSlider("corner/thick", &n->cornerRadius, 0.0f, 0.3f);
      ModSliderInt("sides", &n->sides, 3, 20);
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
      DropdownButton("preset", FormulaNode::PresetNames(), n->presetIndex,
                     [n](int i) { n->presetIndex = i; n->LoadPreset(i); });

      if (ImGui::Button("Edit GLSL...", ImVec2(kPreviewSize, 0)))
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
      ModCheckbox("size from in0", &n->matchInput);
      // Turbo 0.56: Shadertoy mode controls, then the knobs declared in the
      // code (uniform float name; // lo..hi = default). The sliders always
      // draw in this order so modulation ordinals stay put.
      ModSlider("mouse x", &n->mouseX, 0.0f, 1.0f);
      ModSlider("mouse y", &n->mouseY, 0.0f, 1.0f);
      ModCheckbox("mouse down", &n->mouseDown);
      ModCheckbox("float buffer", &n->floatBuffer);
      {
         static const std::vector<std::string> kSelf = { "off", "iChannel0 / in0", "iChannel1 / in1",
                                                         "iChannel2 / in2", "iChannel3 / in3" };
         DropdownButton("last frame to", kSelf, std::clamp(n->selfChannel + 1, 0, 4),
                        [n](int i) { PushUndoCheckpoint(); n->selfChannel = i - 1; }, kParamWidth, false);
      }
      n->ParseUserParams();
      for (FormulaNode::UserParam& up : n->userParams)
         ModSlider(up.name.c_str(), &up.value, up.lo, up.hi);
      if (n->ShadertoyMode())
         ImGui::TextDisabled("Shadertoy mode: iChannel0-3 = in0-in3, iMouse = mouse x / y");
      else
         ImGui::TextDisabled("images: texture(uIn0, uv) .. uIn3");
   }

   void DrawTextParams(TextNode* n)
   {
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("text", buf, sizeof(buf)))
         n->text = buf;

      const std::vector<std::string>& fonts = TextNode::AvailableFonts();
      if (n->fontName.empty())
      {
         const auto preferred = std::find(fonts.begin(), fonts.end(), "Segoe UI");
         n->fontName = preferred != fonts.end() ? *preferred : fonts.front();
      }
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
      if (ImGui::Button("Choose video...", ImVec2(kPreviewSize, 0)))
         StartNodeFileDialog(n, Platform::OpenVideoDialog, [](auto* m, const std::string& path) { m->Open(path); });

      if (ModTriggerButton("Restart##videoRestart", ImVec2(kPreviewSize, 0)))
         n->Restart();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of("/\\");
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         ImGui::PopTextWrapPos();
         ImGui::TextDisabled("%dx%d  %.1fs / %.1fs", n->GetOutputWidth(), n->GetOutputHeight(),
                             n->Position(), n->Duration());
      }

      if (n->Duration() > 0.0)
      {
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 size(kPreviewSize, 32.0f);
         ImGui::InvisibleButton("video timeline", size);
         ImDrawList* draw = ImGui::GetWindowDrawList();
         const ImVec2 end(origin.x + size.x, origin.y + size.y);
         draw->AddRectFilled(origin, end, IM_COL32(25, 28, 38, 255), 4.0f);

         const float trimX0 = origin.x + n->trimStart * size.x;
         const float trimX1 = origin.x + n->trimEnd * size.x;
         draw->AddRectFilled(ImVec2(trimX0, origin.y), ImVec2(trimX1, end.y),
                             IM_COL32(58, 90, 135, 255), 4.0f);
         const float position01 = (float)std::clamp(n->Position() / n->Duration(), 0.0, 1.0);
         const float playheadX = origin.x + position01 * size.x;
         draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, end.y),
                       IM_COL32(245, 245, 255, 255), 2.0f);

         static const ImU32 cueColors[VideoSourceNode::kCueCount] = {
            IM_COL32(255, 190, 80, 255), IM_COL32(100, 220, 150, 255),
            IM_COL32(115, 175, 255, 255), IM_COL32(220, 125, 255, 255)
         };
         for (int i = 0; i < VideoSourceNode::kCueCount; ++i)
         {
            const float cue = n->CueNormalized(i);
            if (cue < 0.0f)
               continue;
            const float x = origin.x + cue * size.x;
            draw->AddTriangleFilled(ImVec2(x, origin.y + 1.0f),
                                    ImVec2(x - 5.0f, origin.y + 9.0f),
                                    ImVec2(x + 5.0f, origin.y + 9.0f), cueColors[i]);
            char cueNumber[2] = { (char)('1' + i), '\0' };
            draw->AddText(ImVec2(x - 3.0f, origin.y + 12.0f), cueColors[i], cueNumber);
         }

         if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
         {
            const float at = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
            if (ImGui::GetIO().KeyShift)
               n->SetNextCue(at);
            else
               n->SeekNormalized(at);
         }
         ImGui::TextDisabled("click: seek   Shift+click: add cue");
      }

      ModCheckbox("loop", &n->loop);
      ImGui::SameLine();
      ModCheckbox("reverse", &n->reverse);
      ModCheckbox("audio", &n->audioEnabled);
      ModSlider("volume", &n->volume, 0.0f, 2.0f, "%.2f");
      ModSlider("speed", &n->speed, 0.05f, 4.0f, "%.2fx");

      if (ModSlider("trim in", &n->trimStart, 0.0f, 1.0f, "%.3f"))
         n->trimStart = std::min(n->trimStart, n->trimEnd);
      if (ModSlider("trim out", &n->trimEnd, 0.0f, 1.0f, "%.3f"))
         n->trimEnd = std::max(n->trimEnd, n->trimStart);

      if (!n->HasAudio() && !n->LoadedPath().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("audio: %s", n->AudioError().c_str());
         ImGui::PopTextWrapPos();
      }

      for (int i = 0; i < VideoSourceNode::kCueCount; ++i)
      {
         ImGui::PushID(i);
         char setLabel[32];
         std::snprintf(setLabel, sizeof(setLabel), "Set %d##videoCueSet", i + 1);
         if (ModTriggerButton(setLabel, ImVec2(56.0f, 0)))
         {
            const float at = n->Duration() > 0.0
               ? (float)std::clamp(n->Position() / n->Duration(), 0.0, 1.0) : 0.0f;
            n->SetCue(i, at);
         }
         ImGui::SameLine();
         if (ModTriggerButton("Go##videoCueGo", ImVec2(42.0f, 0)))
            n->TriggerCue(i);
         ImGui::SameLine();
         if (ModTriggerButton("x##videoCueClear", ImVec2(32.0f, 0)))
            n->ClearCue(i);
         ImGui::SameLine();
         if (n->CueNormalized(i) >= 0.0f)
            ImGui::TextDisabled("cue %d  %.2fs", i + 1, n->CueSeconds(i));
         else
            ImGui::TextDisabled("cue %d  --", i + 1);
         ImGui::PopID();
      }
   }

   void DrawVideoInParams(VideoInNode* n)
   {
      ModCheckbox("active", &n->active);
      ImGui::SameLine();
      ModCheckbox("mirror", &n->mirror);

      if (ImGui::Button("Refresh cameras", ImVec2(kPreviewSize, 0)))
         n->RefreshDevices();
      const auto& devices = n->AvailableDevices();
      if (devices.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("No camera list loaded. Click Refresh cameras.");
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
      DropdownButton("fps", VideoInNode::FrameRateNames(), n->frameRate, [n](int idx) {
         n->frameRate = idx;
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
         ImGui::TextDisabled("%s", n->StatusLine().c_str());
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
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
      ModSlider("offset X", &n->offsetX, -4096.0f, 4096.0f, "%.0f px"); // Turbo 0.46
      ModSlider("offset Y", &n->offsetY, -4096.0f, 4096.0f, "%.0f px");
      if (!n->matchInput)
         ModCheckbox("limit (shrink only)", &n->limit);
      DropdownButton("smoothness", FilterNode::Common::SmoothNames(), n->smooth, [n](int i) {
         if (n->smooth == i)
            return;
         if (!gApplyingDiscreteCv)
            PushUndoCheckpoint();
         n->smooth = i;
      });
      ImGui::TextDisabled("%d x %d", n->GetOutputWidth(), n->GetOutputHeight());
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
            dl->AddLine(pA, pB, IM_COL32(255, 180, 50, 200), 1.5f);
         }
      }
      for (int c = 0; c < gw; ++c)
      {
         for (int r = 0; r < gh - 1; ++r)
         {
            ImVec2 pA = toScreen(node->points[r][c].x, node->points[r][c].y);
            ImVec2 pB = toScreen(node->points[r + 1][c].x, node->points[r + 1][c].y);
            dl->AddLine(pA, pB, IM_COL32(255, 180, 50, 200), 1.5f);
         }
      }

      // Draw corner diagonals in corner pin mode for visual alignment
      if (isCornerPin)
      {
         ImVec2 pTL = toScreen(node->points[0][0].x, node->points[0][0].y);
         ImVec2 pTR = toScreen(node->points[0][1].x, node->points[0][1].y);
         ImVec2 pBR = toScreen(node->points[1][1].x, node->points[1][1].y);
         ImVec2 pBL = toScreen(node->points[1][0].x, node->points[1][0].y);
         dl->AddLine(pTL, pBR, IM_COL32(255, 180, 50, 70), 1.0f);
         dl->AddLine(pTR, pBL, IM_COL32(255, 180, 50, 70), 1.0f);
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

            dl->AddCircleFilled(pt, 6.0f, IM_COL32(255, 180, 50, 255));
            dl->AddCircle(pt, 7.0f, IM_COL32(20, 20, 25, 255), 0, 1.5f);
            if (isDragged || isHovered)
               dl->AddCircle(pt, 11.0f, IM_COL32(255, 255, 255, 240), 0, 2.0f);
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
      dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size), IM_COL32(90, 130, 190, 255), 4.0f, 0, 2.0f);

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

      if (ModCheckbox("match input res", &n->matchInput))
         PushUndoCheckpoint();
      if (ModCheckbox("preserve source aspect", &n->preserveAspect))
         PushUndoCheckpoint();

      if (!n->matchInput)
      {
         ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f", colW);
         ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f", colW);
      }

      if (n->mode == 1) // Mesh Grid
      {
         ImGui::SetNextItemWidth(100.0f);
         int gw = n->gridW;
         if (ImGui::SliderInt("grid X", &gw, 2, 8))
            n->SetGridSize(gw, n->gridH);
         if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoCheckpoint();

         ImGui::SameLine(0.0f, 16.0f);
         ImGui::SetNextItemWidth(100.0f);
         int gh = n->gridH;
         if (ImGui::SliderInt("grid Y", &gh, 2, 8))
            n->SetGridSize(n->gridW, gh);
         if (ImGui::IsItemDeactivatedAfterEdit()) PushUndoCheckpoint();
      }

      const float btnW = (colW - 12.0f) * 0.25f;
      if (ModTriggerButton("Reset Corners##projectionResetCorners", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->ResetCorners();
      }
      ImGui::SameLine(0.0f, 4.0f);
      if (ModTriggerButton("Reset All##projectionResetAll", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->ResetAllPoints();
      }
      ImGui::SameLine(0.0f, 4.0f);
      if (ModTriggerButton("Flip H##projectionFlipH", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->FlipH();
      }
      ImGui::SameLine(0.0f, 4.0f);
      if (ModTriggerButton("Flip V##projectionFlipV", ImVec2(btnW, 0)))
      {
         PushUndoCheckpoint();
         n->FlipV();
      }

      // Turbo: alpha outside the geometry + per-edge blending. Always drawn
      // in this order so the sliders keep stable CV indices.
      if (ModCheckbox("transparent outside (alpha)", &n->transparentOutside))
         PushUndoCheckpoint();
      ImGui::TextDisabled("edge blend");
      static const char* kEdgeLabels[4] = { "left##pjEdgeL", "right##pjEdgeR", "top##pjEdgeT", "bottom##pjEdgeB" };
      static const char* kEdgeWidthLabels[4] = { "left width", "right width", "top width", "bottom width" };
      for (int e = 0; e < 4; ++e)
      {
         if (e > 0)
            ImGui::SameLine(0.0f, 8.0f);
         if (ModCheckbox(kEdgeLabels[e], &n->edgeBlendOn[e]))
            PushUndoCheckpoint();
      }
      for (int e = 0; e < 4; ++e)
      {
         ImGui::BeginDisabled(!n->edgeBlendOn[e]);
         ModSlider(kEdgeWidthLabels[e], &n->edgeBlendWidth[e], 0.0f, 1.0f, "%.3f", colW);
         ImGui::EndDisabled();
      }
      ModSlider("blend curve", &n->edgeBlendCurve, 1.0f, 6.0f, "%.2f", colW);
      ModSlider("blend gamma", &n->edgeBlendGamma, 0.5f, 4.0f, "%.2f", colW);
      static const std::vector<std::string> kBlendModes = { "blend: alpha (fade out)", "blend: black (darken RGB)" };
      DropdownButton("##pjBlendMode", kBlendModes, n->edgeBlendMode,
                     [n](int i) { PushUndoCheckpoint(); n->edgeBlendMode = i; }, colW);
      if (ModCheckbox("antialias edges", &n->antialias))
         PushUndoCheckpoint();
      ImGui::BeginDisabled(!n->antialias);
      ModSlider("antialias px", &n->antialiasPx, 0.5f, 8.0f, "%.1f", colW);
      ImGui::EndDisabled();
   }

   // ---- Turbo 0.60: modulator helpers for the ported MSEG / Voice Spread / Note Expression
   inline void PushCheckboxStyle() {}
   inline void PopCheckboxStyle() {}
   // LFO / Random / MSEG: run once per voice when cabled straight into a
   // synth's voice param (audio/VoiceMod.h). Off: one shared value, as ever.
   void DrawPerVoiceCheckbox(bool* voice)
   {
      PushCheckboxStyle();
      ModCheckbox("per voice", voice);
      PopCheckboxStyle();
   }
   // Upstream's "rate in" row (beats / synced division / Hz). Turbo keeps the
   // beats slider at this place in every mode, so the sliders after it never
   // change ordinal; DrawModRateHz draws the Hz slider, called last.
   void DrawModRateControls(int* rateMode, float* rateBeats, float* /*rateHz*/, int /*hzOrdinal*/)
   {
      DropdownButton("rate in", LFONode::RateModeNames(), *rateMode,
                     [rateMode](int i) { PushUndoCheckpoint(); *rateMode = i; });
      if (*rateMode == LFONode::kRateSynced)
      {
         const int div = MusicTime::NearestRateDivision(*rateBeats);
         DropdownButton("division", MusicTime::RateDivisionList(), div, [rateBeats](int i) {
            PushUndoCheckpoint();
            *rateBeats = (float)MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(i, 0, (int)MusicTime::kNumRateDivisions - 1));
         });
      }
      ImGui::BeginDisabled(*rateMode == LFONode::kRateHz);
      ModSlider("rate", rateBeats, 0.05f, 32.0f, "%.2f beats");
      ImGui::EndDisabled();
   }
   void DrawModRateHz(int rateMode, float* rateHz)
   {
      ImGui::BeginDisabled(rateMode != LFONode::kRateHz);
      ModSlider("rate (Hz)", rateHz, 0.01f, 40.0f, "%.2f Hz");
      ImGui::EndDisabled();
   }


   void DrawLFOParams(LFONode* n)
   {
      DropdownButton("shape", LFONode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 4); // Turbo 0.60: upstream's rate modes
      ModSlider("phase", &n->phase, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
      // Per voice, each note starts the cycle: phase from does not apply.
      ImGui::BeginDisabled(n->voice);
      DropdownButton("phase from", LFONode::PhaseModeNames(), n->phaseMode,
                     [n](int i) { PushUndoCheckpoint(); n->phaseMode = i; });
      ImGui::EndDisabled();
      DrawPerVoiceCheckbox(&n->voice);
      DrawModRateHz(n->rateMode, &n->rateHz); // after "high": ordinal 4, as upstream
   }

   void DrawRandomParams(RandomNode* n)
   {
      DrawModRateControls(&n->rateMode, &n->rateBeats, &n->rateHz, 5); // Turbo 0.60: upstream's rate modes
      ModSlider("smooth", &n->smooth, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
      ModSlider("seed", &n->seed, 0.0f, 200.0f);
      DrawPerVoiceCheckbox(&n->voice);
      DrawModRateHz(n->rateMode, &n->rateHz); // ordinal 5, as upstream
   }

   // Drag/paint state for the pattern step grid. A horizontal paint gesture
   // spans many frames and, unlike a click-to-toggle grid, needs to remember
   // which bar the mouse was over last frame - otherwise a fast drag leaves
   // gaps between samples instead of a continuous paint. Mirrors
   // gDrumGridDrag's reasoning (see DrawDrumSequencerBody).
   struct PatternGridDragState
   {
      PatternNode* node = nullptr;
      int lastStep = -1;
   };
   PatternGridDragState gPatternGridDrag;

   // One vertical bar per live step, filling the node body's width. Click-
   // drag sets a bar's value; dragging horizontally paints every bar the
   // mouse crosses. Shift-drag is a fine (relative) adjustment instead of the
   // usual absolute mouse-y mapping. Right-click resets a bar to centre.
   // steps[] always stores 0..1 - `bipolar` only changes how this draws and
   // reads back (see 00-modulation-polarity.md for why the swing-around-
   // centre convention matters once a binding is bipolar).
   void DrawPatternStepGrid(PatternNode* n)
   {
      const int count = std::max(1, n->EffectiveLength());
      const int curStep = n->CurrentStep();
      const int groupSize = n->fitToBar ? std::max(1, n->fitDivision) : 4;
      const float size = kPreviewSize;
      const float height = 110.0f;
      const float gap = 2.0f;
      const float cellW = (size - gap * (float)(count - 1)) / (float)count;

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImVec2 br(origin.x + size, origin.y + height);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      ImGui::InvisibleButton("##patterngrid", ImVec2(size, height));
      const bool hovered = ImGui::IsItemHovered();
      const bool itemActive = ImGui::IsItemActive();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      auto stepAt = [&](float mx) {
         int s = (int)((mx - origin.x) / (cellW + gap));
         return std::clamp(s, 0, count - 1);
      };

      if (ImGui::IsItemActivated())
      {
         PushUndoCheckpoint();
         gPatternGridDrag.node = n;
         gPatternGridDrag.lastStep = -1;
      }

      int hoverStep = -1;
      if (itemActive && gPatternGridDrag.node == n)
      {
         const int s = stepAt(mouse.x);
         if (ImGui::GetIO().KeyShift)
         {
            n->steps[s] = std::clamp(n->steps[s] - ImGui::GetIO().MouseDelta.y / height * 0.25f, 0.0f, 1.0f);
         }
         else
         {
            const float t = 1.0f - std::clamp((mouse.y - origin.y) / height, 0.0f, 1.0f);
            const int lo = gPatternGridDrag.lastStep >= 0 ? std::min(gPatternGridDrag.lastStep, s) : s;
            const int hi = gPatternGridDrag.lastStep >= 0 ? std::max(gPatternGridDrag.lastStep, s) : s;
            for (int i = lo; i <= hi; i++)
               n->steps[i] = t;
         }
         gPatternGridDrag.lastStep = s;
         hoverStep = s;
      }
      else if (gPatternGridDrag.node == n)
      {
         gPatternGridDrag.node = nullptr;
         gPatternGridDrag.lastStep = -1;
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
         PushUndoCheckpoint();
         n->steps[stepAt(mouse.x)] = 0.5f;
      }
      if (hoverStep < 0 && hovered)
         hoverStep = stepAt(mouse.x);

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, isLight ? IM_COL32(236, 240, 248, 255) : IM_COL32(16, 16, 22, 255), 4.0f);

      const float centerY = origin.y + height * 0.5f;
      if (n->bipolar)
         dl->AddLine(ImVec2(origin.x, centerY), ImVec2(br.x, centerY), isLight ? IM_COL32(180, 186, 202, 255) : IM_COL32(80, 84, 100, 255));

      for (int i = 0; i < count; i++)
      {
         const float x0 = origin.x + (float)i * (cellW + gap);
         const float x1 = x0 + cellW;
         const bool isGroupStart = (i % groupSize) == 0;
         const ImU32 gridCol = isLight
            ? (isGroupStart ? IM_COL32(210, 216, 228, 255) : IM_COL32(224, 228, 238, 255))
            : (isGroupStart ? IM_COL32(64, 68, 84, 255) : IM_COL32(38, 40, 50, 255));
         dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y), gridCol, 2.0f);

         const float v = n->steps[i];
         float fillTop, fillBottom;
         if (n->bipolar)
         {
            if (v >= 0.5f)
            {
               fillTop = centerY - (v - 0.5f) * 2.0f * (height * 0.5f);
               fillBottom = centerY;
            }
            else
            {
               fillTop = centerY;
               fillBottom = centerY + (0.5f - v) * 2.0f * (height * 0.5f);
            }
         }
         else
         {
            fillTop = br.y - v * height;
            fillBottom = br.y;
         }
         const bool isPlayhead = (i == curStep) && curStep < count;
         const ImU32 fillCol = isPlayhead
            ? (isLight ? IM_COL32(225, 130, 20, 255) : IM_COL32(255, 190, 90, 255))
            : (isLight ? IM_COL32(35, 115, 235, 230) : IM_COL32(120, 190, 255, 220));
         dl->AddRectFilled(ImVec2(x0, fillTop), ImVec2(x1, fillBottom), fillCol, 2.0f);
         if (isPlayhead)
            dl->AddRect(ImVec2(x0, origin.y), ImVec2(x1, br.y), isLight ? IM_COL32(0, 0, 0, 80) : IM_COL32(255, 255, 255, 130), 2.0f);

         char label[8];
         snprintf(label, sizeof(label), "%d", i + 1);
         // The default font stops fitting two-digit labels once more than
         // eight bars share the row (16 steps at kPreviewSize leaves under
         // 12px per cell) - shrink to fit rather than letting neighbours
         // overlap into unreadable clutter.
         const float labelFontSize = count <= 8 ? ImGui::GetFontSize() : std::max(8.0f, cellW * 0.95f);
         const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
         const ImU32 stepNumCol = isLight
            ? (isGroupStart ? IM_COL32(40, 48, 65, 255) : IM_COL32(110, 116, 132, 255))
            : (isGroupStart ? IM_COL32(190, 194, 210, 255) : IM_COL32(110, 114, 130, 255));
         dl->AddText(ImGui::GetFont(), labelFontSize, ImVec2(x0 + (cellW - textSize.x) * 0.5f, br.y + 3.0f),
                     stepNumCol, label);
      }
      dl->AddRect(origin, br, isLight ? IM_COL32(185, 192, 208, 255) : IM_COL32(70, 74, 90, 255), 4.0f);

      const float labelRowH = ImGui::GetFontSize() + 6.0f;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, br.y + labelRowH));
      if (hoverStep >= 0)
      {
         const float display = n->bipolar ? (n->steps[hoverStep] - 0.5f) * 2.0f : n->steps[hoverStep];
         ImGui::TextDisabled("step %d: %.2f", hoverStep + 1, display);
      }
      else
      {
         ImGui::TextDisabled("%d steps, looped", count);
      }
   }

   void DrawPatternParams(PatternNode* n)
   {
      DrawPatternStepGrid(n);

      static const std::vector<std::string> kFitDivisionNames = { "1 / beat", "2 / beat", "4 / beat" };
      static const int kFitDivisionValues[3] = { 1, 2, 4 };

      ModCheckbox("fit to bar", &n->fitToBar);
      if (n->fitToBar)
      {
         int cur = 0;
         for (int i = 0; i < 3; i++)
            if (kFitDivisionValues[i] == n->fitDivision)
               cur = i;
         DropdownButton("steps/beat", kFitDivisionNames, cur, [n](int i) { n->fitDivision = kFitDivisionValues[i]; });
      }
      else
      {
         ImGui::SetNextItemWidth(kParamWidth);
         ImGui::SliderInt("length", &n->length, 1, PatternNode::kSteps);
      }
      ModSlider("beats / step", &n->stepBeats, 0.05f, 8.0f);
      ModCheckbox("glide between steps", &n->smoothSteps);
      ModCheckbox("bipolar", &n->bipolar);
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
      if (n->noiseType >= 6)
      {
         // 4D types: speed moves along the 4th axis (morph in place); drift
         // is an optional scroll. Drawn after every older slider so their
         // CV indices do not move.
         ModSlider("translate x", &n->translateX, -50.0f, 50.0f);
         ModSlider("translate y", &n->translateY, -50.0f, 50.0f);
         ModSlider("drift x", &n->driftX, -5.0f, 5.0f);
         ModSlider("drift y", &n->driftY, -5.0f, 5.0f);
         ModSlider("z", &n->zOffset, -20.0f, 20.0f);
         ModSlider("rotate", &n->rotate, -180.0f, 180.0f, "%.0f deg");
         ModSlider("exponent", &n->exponent, 0.2f, 4.0f);
      }
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
         // Turbo (TOP parity): fractional index blends neighbouring inputs.
         if (n->index < 0.0f)
            n->index = (float)n->manualSlot;
         ModSlider("index", &n->index, 0.0f, (float)(SwitcherNode::kSlots - 1), "%.2f");
         ImGui::TextDisabled("A=0  B=1  C=2  D=3");
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
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);

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
         dl->AddLine(a, b, isLight ? IM_COL32(30, 120, 230, 220) : IM_COL32(120, 200, 255, 170), 1.6f);
      }

      // Corner labels: the pad blends between these four named effects, so it is
      // obvious what is being swept rather than four anonymous weights.
      const ImU32 labelCol = isLight ? IM_COL32(50, 58, 75, 255) : IM_COL32(150, 156, 180, 255);
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
      ImU32 orbColor = n->IsRecordingPath() ? IM_COL32(255, 90, 90, 255)
                     : n->IsPlayingPath()   ? (isLight ? IM_COL32(30, 180, 80, 255) : IM_COL32(120, 235, 150, 255))
                                            : (isLight ? IM_COL32(230, 140, 20, 255) : IM_COL32(255, 190, 90, 255));
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, isLight ? IM_COL32(240, 240, 240, 255) : IM_COL32(20, 20, 28, 255), 0, 2.0f);
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
   }

   void DrawResynthParams(ResynthNode* n)
   {
      DrawFxPad(n);

      const bool recordingPath = n->IsRecordingPath();
      bool requestedRecordingPath = recordingPath;
      if (recordingPath)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
      if (ModStateButton(recordingPath ? "Stop rec##resynthRecordPath" : "Rec path##resynthRecordPath",
                         recordingPath, requestedRecordingPath, ImVec2(kPreviewSize * 0.48f, 0)))
      {
         if (requestedRecordingPath)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recordingPath)
         ImGui::PopStyleColor();
      ImGui::SameLine();
      const bool playingPath = n->IsPlayingPath();
      bool requestedPlayingPath = playingPath;
      if (ModStateButton(playingPath ? "Stop##resynthPlayPath" : "Play path##resynthPlayPath",
                         playingPath, requestedPlayingPath, ImVec2(kPreviewSize * 0.48f, 0)))
      {
         if (requestedPlayingPath)
            n->PlayPath();
         else
            n->StopPath();
      }
      ModCheckbox("loop path", &n->loopPath);
      ImGui::SameLine();
      if (ModTriggerButton("clear##resynthClearPath", ImVec2(50.0f, 0)))
         n->ClearPath();

      NodeSeparator();
      DropdownButton("mode", ResynthNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModSlider("chaos", &n->chaos, 0.0f, 1.0f);
      ModSlider("mutation", &n->mutation, 0.0f, 1.0f);
      ModSlider("feedback", &n->feedback, 0.0f, 1.0f);
      ModSlider("source pull", &n->sourcePull, 0.0f, 1.0f);

      NodeSeparator();
      ImGui::TextDisabled("generation %d", n->Generation());
      if (ModTriggerButton("Iterate##resynthIterate", ImVec2(kPreviewSize * 0.48f, 0)))
         n->StepOnce();
      ImGui::SameLine();
      if (ModTriggerButton("Reset##resynthReset", ImVec2(kPreviewSize * 0.48f, 0)))
         n->Reset();
      if (ModTriggerButton("Randomise FX##resynthRandomise", ImVec2(kPreviewSize, 0)))
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

   void DrawMacroKnobParams(MacroKnobNode* n)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "%s", n->label.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("name", buf, sizeof(buf)))
         n->label = buf;

      ImGui::SetNextItemWidth(kPreviewSize);
      ImGui::SliderFloat("##macro", &n->value, 0.0f, 1.0f, "%.3f");
      ModSlider("curve", &n->curve, 0.2f, 4.0f);
      ModCheckbox("invert", &n->invert);
   }

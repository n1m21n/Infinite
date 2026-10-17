// Part of src/main.cpp, included there in order (one translation unit).
// Turbo 0.59 (after upstream's v0.6 show safety): for a live show.
//  - Panic (Ctrl+.): the output fades to silence in 5 ms, every MIDI Out sends
//    all-notes-off, plugins release their notes and the transport stops.
//    Pressing it again brings the sound back (and the transport, if it ran).
//  - Show lock (Ctrl+Shift+L): no deleting nodes or cables, no dragging nodes,
//    no new cables, no cut, no undo / redo. Knobs, transport, Performance mode
//    and the Set list keep working.
//  - Pre-show check: what would go wrong on stage (closed audio device,
//    plugins not loaded, missing media, MIDI devices not found, Outputs with
//    nothing connected).

   bool gShowLock = false;
   bool gPanicActive = false;
   bool gPanicWasPlaying = false;
   bool gPreShowOpen = false;
   // Node positions held while the show lock is on (index -> canvas position).
   std::map<int, ImVec2> gShowLockPositions;

   void SetShowLock(bool on)
   {
      gShowLock = on;
      gShowLockPositions.clear();
      if (on)
         for (GraphNode& gn : gNodes)
            gShowLockPositions[gn.index] = ImVec2(gn.liveX, gn.liveY);
      gPatchStatus = on ? "Show lock ON: editing is blocked (Ctrl+Shift+L unlocks)" : "Show lock off";
   }

   void TogglePanic()
   {
      gPanicActive = !gPanicActive;
      AudioEngine::Instance().SetPanicMute(gPanicActive);
      Transport& transport = Transport::Instance();
      if (gPanicActive)
      {
         gPanicWasPlaying = transport.IsPlaying();
         transport.SetPlaying(false);
         for (GraphNode& gn : gNodes)
         {
            if (auto* midiOut = dynamic_cast<MidiOutNode*>(gn.node.get()))
               midiOut->Panic();
            if (auto* plugin = dynamic_cast<AudioPluginNode*>(gn.node.get()))
               plugin->FlushNotes();
         }
         gPatchStatus = "PANIC: output muted, notes released, transport stopped (Ctrl+. again resumes)";
      }
      else
      {
         if (gPanicWasPlaying)
            transport.SetPlaying(true);
         gPatchStatus = "Panic released";
      }
   }

   // Inside the node editor, before the nodes draw: a locked node goes back
   // to where it was if something moved it (a drag).
   void HoldShowLockPositions()
   {
      if (!gShowLock)
         return;
      for (GraphNode& gn : gNodes)
      {
         auto it = gShowLockPositions.find(gn.index);
         if (it == gShowLockPositions.end())
         {
            gShowLockPositions[gn.index] = ImVec2(gn.liveX, gn.liveY); // a node added while locked
            continue;
         }
         const ImVec2 now = ed::GetNodePosition(gn.NodeId());
         if (std::isfinite(now.x) && std::isfinite(now.y) &&
             (std::fabs(now.x - it->second.x) > 0.5f || std::fabs(now.y - it->second.y) > 0.5f))
            ed::SetNodePosition(gn.NodeId(), it->second);
      }
   }

   // Ctrl+. panic and Ctrl+Shift+L lock, from anywhere but a text field.
   void HandleShowSafetyKeys()
   {
      const ImGuiIO& io = ImGui::GetIO();
      if (io.WantTextInput || !io.KeyCtrl || io.KeyAlt)
         return;
      if (ImGui::IsKeyPressed(ImGuiKey_Period, false) && !io.KeyShift)
         TogglePanic();
      if (ImGui::IsKeyPressed(ImGuiKey_L, false) && io.KeyShift)
         SetShowLock(!gShowLock);
   }

   // Strings a node keeps that look like a file on disk.
   std::vector<std::string> NodeFilePaths(INode* node)
   {
      struct PathFinder : public ParamVisitor
      {
         std::vector<std::string> paths;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Bool(const char*, bool&) override {}
         void Color(const char*, float[3]) override {}
         void Text(const char*, std::string& v) override
         {
            if (v.size() < 4 || v.size() > 1024 || v.find('\n') != std::string::npos)
               return;
            const bool absolute = (v.size() > 2 && v[1] == ':' && (v[2] == '\\' || v[2] == '/')) ||
                                  (v[0] == '\\' && v[1] == '\\');
            const size_t dot = v.find_last_of('.');
            const size_t slash = v.find_last_of("/\\");
            const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash) &&
                                v.size() - dot >= 3 && v.size() - dot <= 6;
            if (absolute && hasExt)
               paths.push_back(v);
         }
      } finder;
      node->VisitParams(finder);
      return finder.paths;
   }

   void DrawPreShowCheck()
   {
      if (!gPreShowOpen)
         return;
      ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin("Pre-show check###preshow", &gPreShowOpen, ImGuiWindowFlags_NoCollapse))
      {
         ImGui::End();
         return;
      }
      const ImVec4 ok(0.45f, 0.85f, 0.5f, 1.0f), bad(0.95f, 0.55f, 0.35f, 1.0f);
      int problems = 0;
      auto issue = [&](const std::string& text, int nodeIndex) {
         problems++;
         ImGui::PushID(problems);
         ImGui::TextColored(bad, "!");
         ImGui::SameLine();
         ImGui::TextWrapped("%s", text.c_str());
         if (nodeIndex >= 0)
         {
            ImGui::SameLine();
            if (ImGui::SmallButton("show"))
            {
               if (GraphNode* gn = FindNodeByIndex(nodeIndex))
               {
                  gPendingSelect.clear();
                  gPendingSelect.push_back(gn->NodeId());
                  gPendingNavigateToSelection = true;
               }
            }
         }
         ImGui::PopID();
      };

      if (AudioEngine::Instance().SampleRate() <= 0.0)
         issue("The audio device is closed: nothing will sound (the power button in the top bar starts it).", -1);
      for (GraphNode& gn : gNodes)
      {
         if (gn.node->blockOff)
            continue; // a switched-off Set block is checked when it is switched on
         const std::string title = NodeTitleWithInstance(gn);
         if (auto* plugin = dynamic_cast<AudioPluginNode*>(gn.node.get()))
            if (plugin->HasPlugin() && !plugin->IsReady())
               issue(title + ": plugin not loaded (" + plugin->Status() + ")", gn.index);
         if (auto* midiOut = dynamic_cast<MidiOutNode*>(gn.node.get()))
            if (!midiOut->device.empty() && !midiOut->DeviceOpen())
               issue(title + ": MIDI device \"" + midiOut->device + "\" not found", gn.index);
         if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
            if (out->Input().GetSource() == nullptr)
               issue(title + ": nothing is connected to this Output", gn.index);
         for (const std::string& path : NodeFilePaths(gn.node.get()))
         {
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::u8path(path), ec))
               issue(title + ": missing file " + path, gn.index);
         }
      }
      if (problems == 0)
         ImGui::TextColored(ok, "All clear: audio open, plugins loaded, media found, Outputs connected.");
      ImGui::Separator();
      ImGui::TextDisabled("Checks the blocks that are on. Ctrl+. is the panic, Ctrl+Shift+L the show lock.");
      ImGui::End();
   }

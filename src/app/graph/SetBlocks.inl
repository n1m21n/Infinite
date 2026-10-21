// Part of src/main.cpp, included there in order (one translation unit).
// Turbo 0.59: Set blocks, for long performances. Every Group is a block that
// can be switched off: its members stop cooking, leave the audio topology
// (silent, no CPU) and read as nothing to any cable from outside, so several
// songs can live in one patch and a show moves between them without opening
// files. The Set list window (VIEW > Set list, Shift+L) runs the show: GO on a
// song switches it on (alone, by default), rewinds and plays.

   bool gSetListOpen = false;
   bool gSetExclusive = true;   // GO switches every other block off
   bool gSetGoRewinds = true;   // GO rewinds the timeline and plays
   int gSetCurrentGroup = -1;   // GraphNode index of the last block sent with GO
   bool gPendingNavigateToSelection = false; // Canvas.inl frames gPendingSelect next frame

   // Blocks in show order: left to right on the canvas, then top to bottom.
   std::vector<GraphNode*> SetBlocksInOrder()
   {
      std::vector<GraphNode*> out;
      for (GraphNode& gn : gNodes)
         if (dynamic_cast<GroupNode*>(gn.node.get()) != nullptr)
            out.push_back(&gn);
      std::sort(out.begin(), out.end(), [](const GraphNode* a, const GraphNode* b) {
         if (std::fabs(a->liveX - b->liveX) > 40.0f)
            return a->liveX < b->liveX;
         return a->liveY < b->liveY;
      });
      return out;
   }

   // Once a frame, before cooking: which nodes sit in a switched-off block.
   // A change rebuilds the audio topology (so a song stops, or starts, on the
   // next audio block).
   void UpdateBlockStates()
   {
      bool changed = false;
      for (GraphNode& gn : gNodes)
      {
         bool off = false;
         if (dynamic_cast<GroupNode*>(gn.node.get()) == nullptr)
            if (GroupNode* owner = GroupOwning(gn.index))
               off = !owner->active;
         if (gn.node->blockOff != off)
         {
            gn.node->blockOff = off;
            changed = true;
         }
      }
      if (changed)
         RebuildAudioTopology();
   }

   void SetBlockActive(GroupNode* g, bool on)
   {
      if (g != nullptr && g->active != on)
      {
         g->active = on;
         gPatchDirty = true;
      }
   }

   // GO: this block on (alone when gSetExclusive), timeline back to the start
   // and playing (when gSetGoRewinds).
   void SetBlockGo(GraphNode& blockNode)
   {
      auto* g = dynamic_cast<GroupNode*>(blockNode.node.get());
      if (g == nullptr)
         return;
      PushUndoCheckpoint();
      if (gSetExclusive)
         for (GraphNode* other : SetBlocksInOrder())
            if (other != &blockNode)
               SetBlockActive(static_cast<GroupNode*>(other->node.get()), false);
      SetBlockActive(g, true);
      gSetCurrentGroup = blockNode.index;
      if (gSetGoRewinds)
      {
         Transport::Instance().Rewind();
         Transport::Instance().SetPlaying(true);
      }
      gPatchStatus = "Set: " + g->label;
   }

   // A block's on / off in Performance mode: a Toggle bound to the group's
   // "active" flag (any bool param works that way, see WriteNodeBool), so it
   // also takes MIDI learn and lives on any page.
   void AddBlockToPerformance(int groupIndex)
   {
      GraphNode* gn = FindNodeByIndex(groupIndex);
      auto* g = gn != nullptr ? dynamic_cast<GroupNode*>(gn->node.get()) : nullptr;
      if (g == nullptr)
         return;
      AddToPerformanceMatrix(groupIndex, -1, 3, g->label, -1, "active");
      gPatchStatus = "Performance mode: on / off for " + g->label;
   }

   // Performance mode's "Assign..." is waiting and the user clicked a block's
   // power button: the element becomes that block's on / off (a Toggle).
   bool BindPerfElementToBlock(int groupIndex)
   {
      if (gPerfAssigningElemIdx < 0 || gPerfAssigningElemIdx >= (int)gPerfElements.size())
         return false;
      GraphNode* gn = FindNodeByIndex(groupIndex);
      auto* g = gn != nullptr ? dynamic_cast<GroupNode*>(gn->node.get()) : nullptr;
      if (g == nullptr)
         return false;
      PushUndoCheckpoint();
      Patch::PerfRecord& e = gPerfElements[(size_t)gPerfAssigningElemIdx];
      static const char* kDefaults[] = { "Knob", "Fader", "Slider", "Toggle", "XY Pad", "Trigger",
                                         "NumBox", "Selector", "Pan/Detent", "Step Gate" };
      bool isDefault = e.label.empty();
      for (const char* d : kDefaults)
         isDefault = isDefault || e.label == d;
      if (isDefault)
         e.label = g->label;
      e.kind = 3; // an on / off is a Toggle, whatever the element was
      e.dstIndex = groupIndex;
      e.dstParam = -1;
      e.dstParam2 = -1;
      e.boolName = "active";
      e.targets.clear();
      e.targetsY.clear();
      gPerfAssigningElemIdx = -1;
      gPatchStatus = "Performance mode: '" + e.label + "' switches " + g->label + " on / off";
      return true;
   }

   void SetBlockStep(int direction)
   {
      std::vector<GraphNode*> blocks = SetBlocksInOrder();
      if (blocks.empty())
         return;
      int at = -1;
      for (int i = 0; i < (int)blocks.size(); i++)
         if (blocks[(size_t)i]->index == gSetCurrentGroup)
            at = i;
      int next = at < 0 ? (direction > 0 ? 0 : (int)blocks.size() - 1) : at + direction;
      next = std::clamp(next, 0, (int)blocks.size() - 1);
      SetBlockGo(*blocks[(size_t)next]);
   }

   // FILE > Import patch as a block: the whole .inf joins the open patch,
   // right of everything already there, inside a new Group named after the
   // file (off when the patch already has content, so nothing starts sounding).
   // Done the way undo works: the current graph and the imported one are
   // merged as patch data, indices shifted, and the result re-applied.
   bool ImportPatchAsBlock(const std::string& path)
   {
      Patch::Data add;
      std::string error;
      if (!Patch::Read(path, add, error))
      {
         gPatchStatus = "Import failed: " + error;
         return false;
      }
      ResolveRelativeMedia(add, path); // Turbo 0.61: relative media paths
      PushUndoCheckpoint();
      Patch::Data cur = BuildPatchData();
      const bool hadContent = !cur.nodes.empty();

      int maxIndex = 0;
      float curMaxX = 0.0f, curMinY = 0.0f;
      bool anyCur = false;
      for (const Patch::NodeRecord& r : cur.nodes)
      {
         maxIndex = std::max(maxIndex, r.index);
         if (!r.hasPos)
            continue;
         curMaxX = anyCur ? std::max(curMaxX, r.x) : r.x;
         curMinY = anyCur ? std::min(curMinY, r.y) : r.y;
         anyCur = true;
      }
      const int offset = maxIndex + 1;
      float addMinX = 0.0f, addMinY = 0.0f, addMaxX = 0.0f, addMaxY = 0.0f;
      bool anyAdd = false;
      for (const Patch::NodeRecord& r : add.nodes)
         if (r.hasPos)
         {
            if (!anyAdd)
            {
               addMinX = addMaxX = r.x;
               addMinY = addMaxY = r.y;
               anyAdd = true;
            }
            addMinX = std::min(addMinX, r.x); addMaxX = std::max(addMaxX, r.x);
            addMinY = std::min(addMinY, r.y); addMaxY = std::max(addMaxY, r.y);
         }
      // Place the block right of the current content, top-aligned with it,
      // leaving room for the widest node (positions are top-left corners).
      const float dx = (anyCur ? curMaxX + 900.0f : 0.0f) - addMinX;
      const float dy = (anyCur ? curMinY : 0.0f) - addMinY;

      int row = 0;
      for (Patch::NodeRecord r : add.nodes)
      {
         r.index += offset;
         r.uid = 0; // fresh identities: the open patch may use the same ones
         if (r.hasPos)
         {
            r.x += dx;
            r.y += dy;
         }
         else
         {
            r.hasPos = true;
            r.x = addMinX + dx;
            r.y = addMaxY + dy + 280.0f * (float)(++row);
         }
         // A group inside the imported patch would nest inside the new block,
         // which groups do not support: it becomes plain content.
         if (r.typeName == "Group")
            continue;
         cur.nodes.push_back(r);
      }
      auto shiftCables = [offset](std::vector<Patch::CableRecord>& to, const std::vector<Patch::CableRecord>& from) {
         for (Patch::CableRecord c : from)
         {
            c.dstIndex += offset;
            c.srcIndex += offset;
            to.push_back(c);
         }
      };
      shiftCables(cur.cables, add.cables);
      shiftCables(cur.geometry, add.geometry);
      shiftCables(cur.audio, add.audio);
      shiftCables(cur.notes, add.notes);
      for (Patch::ModRecord m : add.modulation)
      {
         m.dstIndex += offset;
         m.srcIndex += offset;
         cur.modulation.push_back(m);
      }
      for (Patch::PaletteRecord p : add.palette)
      {
         p.dstIndex += offset;
         p.srcIndex += offset;
         cur.palette.push_back(p);
      }
      for (Patch::ExprRecord e : add.expressions)
      {
         e.dstIndex += offset;
         cur.expressions.push_back(e);
      }
      for (Patch::MidiMapRecord m : add.midi)
      {
         m.dstIndex += offset;
         cur.midi.push_back(m);
      }
      for (Patch::GestureRecord g : add.gestures)
      {
         g.dstIndex += offset;
         cur.gestures.push_back(g);
      }
      for (Patch::PerfRecord p : add.performance)
      {
         if (p.dstIndex >= 0)
            p.dstIndex += offset;
         for (Patch::PerfTarget& t : p.targets)
            if (t.dstIndex >= 0)
               t.dstIndex += offset;
         for (Patch::PerfTarget& t : p.targetsY)
            if (t.dstIndex >= 0)
               t.dstIndex += offset;
         cur.performance.push_back(p);
      }
      for (const Patch::GlobalRecord& g : add.globals)
      {
         bool have = false;
         for (const Patch::GlobalRecord& mine : cur.globals)
            have = have || mine.name == g.name;
         if (!have)
            cur.globals.push_back(g);
      }

      // The block itself: a Group around the imported nodes. It fits itself to
      // its members on the first frames (AutoFitGroupToMembers adopts every
      // node fully inside its box), so a generous first box is enough.
      Patch::NodeRecord group;
      group.index = offset;
      for (const Patch::NodeRecord& r : cur.nodes)
         group.index = std::max(group.index, r.index + 1);
      group.typeName = "Group";
      group.category = "Compositing";
      group.hasPos = true;
      group.x = addMinX + dx - 60.0f;
      group.y = addMinY + dy - 110.0f;
      const std::string label = std::filesystem::u8path(path).stem().u8string();
      char wbuf[32], hbuf[32];
      snprintf(wbuf, sizeof(wbuf), "%g", (addMaxX - addMinX) + 1200.0f);
      snprintf(hbuf, sizeof(hbuf), "%g", (addMaxY - addMinY) + 1400.0f);
      group.params = { { "label", label }, { "width", wbuf }, { "height", hbuf },
                       { "active", hadContent ? "0" : "1" } };
      // Each imported song gets its own colour, cycling through a few hues.
      static const float kHues[6][3] = { { 0.45f, 0.65f, 0.95f }, { 0.95f, 0.55f, 0.35f }, { 0.55f, 0.85f, 0.45f },
                                          { 0.85f, 0.45f, 0.85f }, { 0.95f, 0.85f, 0.35f }, { 0.40f, 0.85f, 0.85f } };
      int groups = 0;
      for (const Patch::NodeRecord& r : cur.nodes)
         groups += r.typeName == "Group" ? 1 : 0;
      const float* hue = kHues[groups % 6];
      char cbuf[64];
      snprintf(cbuf, sizeof(cbuf), "%g %g %g", hue[0], hue[1], hue[2]);
      group.params.push_back({ "color", cbuf });
      cur.nodes.push_back(group);

      ApplyPatchData(cur, false);
      gPatchDirty = true;
      gRequestFitView = true;
      gPatchStatus = "Imported " + label + (hadContent ? " as a block (off: GO it in the Set list)" : " as a block");
      if (!add.streams.empty())
         gPatchStatus += "; its timeline clips were not imported";
      return true;
   }

   void DrawSetList()
   {
      // Shift+L, and Page Up / Page Down step through the set while the window
      // is open (not while typing).
      ImGuiIO& io = ImGui::GetIO();
      if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_L, false) && io.KeyShift && !io.KeyCtrl && !io.KeyAlt)
         gSetListOpen = !gSetListOpen;
      if (!gSetListOpen)
         return;
      if (!io.WantTextInput)
      {
         if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false))
            SetBlockStep(+1);
         if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
            SetBlockStep(-1);
      }

      ImGui::SetNextWindowSize(ImVec2(440, 420), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin("Set list###setlist", &gSetListOpen, ImGuiWindowFlags_NoCollapse))
      {
         ImGui::End();
         return;
      }
      std::vector<GraphNode*> blocks = SetBlocksInOrder();
      if (blocks.empty())
      {
         ImGui::TextWrapped("Every Group on the canvas is a block of the set: a song, a scene, a part of the show. "
                            "Switch a block off and it stops completely (no processing, no sound, no picture); "
                            "GO starts one from the top.");
         ImGui::Spacing();
         ImGui::TextWrapped("Make one: select nodes and press Ctrl+G, or bring a whole patch in as a block:");
      }
      else
      {
         ImGui::Checkbox("One block at a time", &gSetExclusive);
         ImGui::SameLine();
         ImGui::Checkbox("GO rewinds and plays", &gSetGoRewinds);
         ImGui::Separator();
         const float rowH = ImGui::GetFrameHeight();
         if (ImGui::BeginTable("##setlist", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
         {
            ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, rowH * 1.3f);
            ImGui::TableSetupColumn("go", ImGuiTableColumnFlags_WidthFixed, rowH * 2.2f);
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("perf", ImGuiTableColumnFlags_WidthFixed, rowH * 1.3f);
            ImGui::TableSetupColumn("view", ImGuiTableColumnFlags_WidthFixed, rowH * 1.3f);
            int n = 0;
            for (GraphNode* b : blocks)
            {
               auto* g = static_cast<GroupNode*>(b->node.get());
               ImGui::PushID(b->index);
               ImGui::TableNextRow();
               const bool current = b->index == gSetCurrentGroup;
               if (current)
                  ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(g->color[0], g->color[1], g->color[2], 0.22f)));
               ImGui::TableNextColumn();
               if (UiIconButton("##on", UiIcon::Power, g->active, g->active ? "on: click to switch off" : "off: click to switch on",
                                ImVec4(0.16f, 0.52f, 0.28f, 1.0f)))
               {
                  PushUndoCheckpoint();
                  SetBlockActive(g, !g->active);
               }
               ImGui::TableNextColumn();
               if (ImGui::Button("GO", ImVec2(rowH * 2.1f, 0)))
                  SetBlockGo(*b);
               ImGui::TableNextColumn();
               ImGui::AlignTextToFramePadding();
               const std::set<int>& members = gGroupMembers[g];
               ImGui::TextColored(ImVec4(g->color[0] * 0.4f + 0.6f, g->color[1] * 0.4f + 0.6f, g->color[2] * 0.4f + 0.6f,
                                         g->active ? 1.0f : 0.5f),
                                  "%d. %s", ++n, g->label.c_str());
               ImGui::SameLine();
               ImGui::TextDisabled("%zu nodes", members.size());
               ImGui::TableNextColumn();
               if (UiIconButton("##perf", UiIcon::Perf, false, "add this block's on / off to Performance mode (a Toggle)"))
                  AddBlockToPerformance(b->index);
               ImGui::TableNextColumn();
               if (ImGui::SmallButton("view"))
               {
                  // Select the block's nodes and frame them (applied inside
                  // the editor next frame, see Canvas.inl).
                  gPendingSelect.clear();
                  gPendingSelect.push_back(b->NodeId());
                  for (int idx : members)
                     if (GraphNode* m = FindNodeByIndex(idx))
                        gPendingSelect.push_back(m->NodeId());
                  gPendingNavigateToSelection = true;
               }
               ImGui::PopID();
            }
            ImGui::EndTable();
         }
         ImGui::Spacing();
         if (UiIconButton("##prev", UiIcon::Prev, false, "previous block (Page Up)"))
            SetBlockStep(-1);
         ImGui::SameLine();
         if (UiIconButton("##next", UiIcon::Next, false, "next block (Page Down)"))
            SetBlockStep(+1);
         ImGui::SameLine();
         if (ImGui::Button("All on"))
         {
            PushUndoCheckpoint();
            for (GraphNode* b : blocks)
               SetBlockActive(static_cast<GroupNode*>(b->node.get()), true);
         }
         ImGui::SameLine();
         if (ImGui::Button("All off"))
         {
            PushUndoCheckpoint();
            for (GraphNode* b : blocks)
               SetBlockActive(static_cast<GroupNode*>(b->node.get()), false);
         }
      }
      ImGui::Spacing();
      if (ImGui::Button("Import a patch as a block..."))
         StartFileDialog(Platform::OpenPatchDialog, [](const std::string& p) { ImportPatchAsBlock(p); });
      ImGui::TextDisabled("Nodes outside every group (a master mixer, the output) always run.");
      ImGui::End();
   }

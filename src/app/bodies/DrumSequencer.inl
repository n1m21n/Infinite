// Part of src/main.cpp, included there in order (one translation unit).
// Drum Sequencer and bundled pattern packs

   void DrawDrumLaneWaveform(DrumSequencerNode* n, int lane, float h)
   {
      const float w = gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->laneWaveCount[lane] > 0;
      const WavePeaks& peaks = n->lanePeaks[lane];
      // Turbo 0.49: shared view (zoom-to-trim, per-pixel peaks, cursors).
      WaveView view = WaveViewBegin("##drumlanewave", origin, w, h, n->laneStart[lane], n->laneEnd[lane],
                                    peaks.NumFrames());

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##drumlanewavebody", ImVec2(w, h));
      if (!hasSample && ImGui::IsItemActivated())
      {
         StartNodeFileDialog(n, Platform::OpenAudioDialog, [lane](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            m->LoadFileToLane(lane, path);
         });
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      if (hasSample)
      {
         DrawWavePeaks(dl, view, peaks, n->laneStart[lane], n->laneEnd[lane],
                       isLight ? IM_COL32(30, 110, 230, 230) : IM_COL32(150, 214, 255, 220));
         // Turbo 0.49: a cursor per sounding voice of this lane.
         float pos[DrumSequencerNode::kVoicesPerLane];
         const int count = n->LaneVoicePositions(lane, pos, DrumSequencerNode::kVoicesPerLane);
         for (int i = 0; i < count; i++)
            DrawPlayCursor(dl, view, pos[i], PlayCursorColor());
      }
      else
      {
         const float midY = origin.y + h * 0.5f;
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "drop sample / click");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
         WaveTrimHandles(view, "##drumlanetrim", &n->laneStart[lane], &n->laneEnd[lane],
                         std::max(0.001f, 0.01f * view.Span()), &peaks, TrimStartCol(), TrimEndCol());

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   // One lane's card: waveform + clear/choke row + the five per-lane knobs.
   // Called once per lane inside each of the two BeginAudioColumn scopes in
   // DrawDrumSequencerBody, so gAudioBodyW/gAudioContentW already read as
   // the column's width - the section panel and knob row size themselves
   // from that with no extra plumbing (the "width is a scope" rule).
   // Turbo: the default bundled drum kit (0.54: the CC0 acoustic kit, else
   // turbo-basic), next to the exe or relative to the working directory.
   // Empty when none exists.
   std::string TurboDrumKitDir()
   {
      return DrumPatterns::BundledKitDir();
   }

   // Turbo 0.54: every bundled kit the Drum Sequencer's kit picker offers:
   // folders under assets/drumkits (both builds) and assets/drumkits-gpl
   // (full build: SLOOP's kits, rendered) holding at least a kick lane.
   struct BundledKit { std::string name, dir; };
   const std::vector<BundledKit>& BundledKits()
   {
      static std::vector<BundledKit> sKits;
      static bool sScanned = false;
      if (sScanned)
         return sKits;
      sScanned = true;
      const std::filesystem::path root = DrumPatterns::BundledKitsRoot();
      if (root.empty())
         return sKits;
      for (const std::filesystem::path& base : { root, root.parent_path() / "drumkits-gpl" })
      {
         std::error_code ec;
         if (!std::filesystem::is_directory(base, ec))
            continue;
         std::vector<BundledKit> found;
         for (const auto& e : std::filesystem::directory_iterator(base, ec))
         {
            if (!e.is_directory(ec))
               continue;
            const std::string dir = e.path().u8string();
            if (!DrumPatterns::KitLanePath(dir, 0).empty())
               found.push_back({ e.path().filename().u8string(), dir });
         }
         std::sort(found.begin(), found.end(), [](const BundledKit& a, const BundledKit& b) { return a.name < b.name; });
         sKits.insert(sKits.end(), found.begin(), found.end());
      }
      return sKits;
   }

   // Turbo 0.54: bundled pattern packs (assets/drum-patterns/<pack>) are
   // copied once into the user's MIDI pattern folder, where the Drum
   // Sequencer's MIDI browser lists them by style. A marker file per pack
   // keeps it from coming back after the user deletes it.
   void SeedBundledDrumPatterns()
   {
      const std::filesystem::path kitsRoot = DrumPatterns::BundledKitsRoot();
      const std::string userDir = DrumMidi::DefaultFolder();
      if (kitsRoot.empty() || userDir.empty())
         return;
      const std::filesystem::path packsRoot = kitsRoot.parent_path() / "drum-patterns";
      std::error_code ec;
      if (!std::filesystem::is_directory(packsRoot, ec))
         return;
      bool copied = false;
      for (const auto& pack : std::filesystem::directory_iterator(packsRoot, ec))
      {
         if (!pack.is_directory(ec))
            continue;
         const std::string name = pack.path().filename().u8string();
         const std::filesystem::path marker = std::filesystem::u8path(userDir) / (".turbo-pack-" + name);
         if (std::filesystem::exists(marker, ec))
            continue;
         std::filesystem::copy(pack.path(), std::filesystem::u8path(userDir) / pack.path().filename(),
                               std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
         std::ofstream(marker) << "copied by Infinite-Turbo; delete the pack folder to remove it for good\n";
         RuntimeLog::Write("drum patterns: installed pack '%s' (%s)", name.c_str(), ec ? ec.message().c_str() : "ok");
         copied = true;
      }
      if (copied)
         DrumMidi::RequestScan();
   }

   // Turbo 0.57: the bundled example note patterns go into the Pattern
   // Player's user folder once per pack (marker file), like the drum packs.
   void SeedBundledNotePatterns()
   {
      const std::filesystem::path kitsRoot = DrumPatterns::BundledKitsRoot();
      const std::string userDir = NotePatterns::UserFolder();
      if (kitsRoot.empty() || userDir.empty())
         return;
      const std::filesystem::path packsRoot = kitsRoot.parent_path() / "note-patterns";
      std::error_code ec;
      if (!std::filesystem::is_directory(packsRoot, ec))
         return;
      bool copied = false;
      for (const auto& pack : std::filesystem::directory_iterator(packsRoot, ec))
      {
         if (!pack.is_directory(ec))
            continue;
         const std::string name = pack.path().filename().u8string();
         const std::filesystem::path marker = std::filesystem::u8path(userDir) / (".turbo-pack-" + name);
         if (std::filesystem::exists(marker, ec))
            continue;
         std::filesystem::copy(pack.path(), std::filesystem::u8path(userDir) / pack.path().filename(),
                               std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
         std::ofstream(marker) << "copied by Infinite-Turbo; delete the pack folder to remove it for good\n";
         RuntimeLog::Write("note patterns: installed pack '%s' (%s)", name.c_str(), ec ? ec.message().c_str() : "ok");
         copied = true;
      }
      if (copied)
         NotePatterns::Rescan();
   }

   // Turbo 0.54: defaults applied when the user (or Claude through MCP) adds a
   // node, never when a patch loads or nodes are pasted: a new Drum Sequencer
   // comes with the default kit on its lanes.
   void ApplySpawnDefaults(GraphNode* gn)
   {
      if (gn == nullptr)
         return;
      if (auto* d = dynamic_cast<DrumSequencerNode*>(gn->node.get()))
         d->LoadKitIntoEmptyLanes(TurboDrumKitDir());
   }

   // Turbo 0.47: groove index of the node's saved pattern name, -1 if none.
   int DrumGrooveIndex(const DrumSequencerNode* n)
   {
      return n->GrooveIndex();
   }

   DrumSequencerNode* LiveDrumNode(INode* target)
   {
      for (GraphNode& g : gNodes)
         if (g.node.get() == target)
            return dynamic_cast<DrumSequencerNode*>(g.node.get());
      return nullptr;
   }

   // Applies groove / part to a node that may have been deleted meanwhile
   // (dropdown callbacks run later in the frame), loading the kit into empty
   // lanes. Undo checkpoints are the caller's.
   // Turbo 0.50: through SelectGroove, so the groove being left keeps its
   // edits (restored when it is picked again) and the same groove only
   // switches part (rate and swing stay as the user set them).
   void DrumApplyGroove(INode* target, int groove, int part)
   {
      DrumSequencerNode* d = LiveDrumNode(target);
      if (d == nullptr || !d->SelectGroove(groove, part))
         return;
      d->browsingCategory = false;
      d->LoadKitIntoEmptyLanes(TurboDrumKitDir());
      gPatchDirty = true;
   }

   // Turbo 0.50: a groove from the user's MIDI folder, same contract as
   // DrumApplyGroove (the node's status line says why when the file fails).
   void DrumApplyMidi(INode* target, const std::string& path, int part)
   {
      DrumSequencerNode* d = LiveDrumNode(target);
      std::string msg;
      if (d == nullptr || !d->SelectMidiGroove(path, part, false, msg))
         return;
      d->browsingCategory = false;
      d->LoadKitIntoEmptyLanes(TurboDrumKitDir());
      gPatchDirty = true;
   }

   // One row of the picker's groove list: a library groove or a MIDI file.
   struct DrumPickItem
   {
      int lib = -1;     // DrumPatterns index, or -1 for a MIDI file
      std::string path; // MIDI file
      std::string name;
      std::string row;  // the dropdown line, with the tempo
   };

   void DrumApplyItem(INode* target, const DrumPickItem& item, int part)
   {
      if (item.lib >= 0)
         DrumApplyGroove(target, item.lib, part);
      else
         DrumApplyMidi(target, item.path, part);
   }

   // Turbo 0.49: what a CV / MIDI / Performance drive last selected on an
   // edge-driven selector (drum part, groove). Applying a groove rewrites the
   // grid, so a held value applies once when it changes, never every frame.
   std::map<std::pair<int, int>, int> gDiscreteLastDriven;

   // The driven option index when the drive changed this frame, else -1.
   int DiscreteDrivenChange(const DiscreteParamRef& ref, int optionCount)
   {
      if (!ref.valid || optionCount <= 0)
         return -1;
      const std::pair<int, int> key(ref.nodeIndex, ref.paramIndex);
      if (!ref.modulated)
      {
         gDiscreteLastDriven.erase(key);
         return -1;
      }
      const int driven = std::clamp((int)lroundf(*ref.value), 0, optionCount - 1);
      auto it = gDiscreteLastDriven.find(key);
      const bool changed = it == gDiscreteLastDriven.end() || it->second != driven;
      gDiscreteLastDriven[key] = driven;
      return changed ? driven : -1;
   }

   // One undo step per driven change from MIDI or the Performance panel (a
   // performer's gesture); none for a CV cable, which can change it every
   // beat, like gApplyingDiscreteCv elsewhere.
   void PushUndoForDrivenChange(const DiscreteParamRef& ref)
   {
      if (!Modulation::Instance().IsModulated(ref.nodeIndex, ref.paramIndex))
         PushUndoCheckpoint();
   }

   // The pattern picker: category, groove (only that category's, so the list
   // stays short), prev / next, and the three parts as buttons.
   // Turbo 0.49: groove, prev / next, the part (one 3-way "part" selector pin
   // at the row start) and each part button are mappable (CV, MIDI learn,
   // Performance Mode), so a pad can fire "B bridge".
   void DrawDrumPatternPicker(DrumSequencerNode* n)
   {
      int count = 0, catCount = 0;
      const DrumPatterns::Groove* all = DrumPatterns::All(count);
      const char* const* cats = DrumPatterns::Categories(catCount);
      // Turbo 0.50: the user's MIDI folder (scanned in the background) is
      // folded into the built-in groups by style (DrumPatterns::StyleToCategory),
      // so "electro-funk" sits in Electronic next to the library grooves. Only
      // styles that match no group get their own "MIDI: <style>" entry.
      DrumMidi::EnsureScanned();
      const std::shared_ptr<const DrumMidi::Library> midi = DrumMidi::Current();
      std::vector<std::string> extraGroups; // unmatched MIDI styles, in folder order
      std::vector<int> entryCat(midi->entries.size(), -1);
      for (size_t e = 0; e < midi->entries.size(); e++)
      {
         int c = DrumPatterns::StyleToCategory(midi->entries[e].group);
         if (c < 0 || c >= catCount)
         {
            const std::string& g = midi->entries[e].group;
            auto it = std::find(extraGroups.begin(), extraGroups.end(), g);
            c = catCount + (int)(it - extraGroups.begin());
            if (it == extraGroups.end())
               extraGroups.push_back(g);
         }
         entryCat[e] = c;
      }
      const int midiGroups = (int)extraGroups.size();
      const int catTotal = catCount + midiGroups;
      const int current = DrumGrooveIndex(n);
      const int currentMidi = n->IsMidiGroove() ? midi->Find(n->MidiGroovePath()) : -1;
      if (!n->browsingCategory)
      {
         if (current >= 0)
         {
            for (int c = 0; c < catCount; c++)
               if (strcmp(cats[c], all[current].category) == 0)
                  n->patternCategory = c;
         }
         else if (currentMidi >= 0 && currentMidi < (int)entryCat.size())
            n->patternCategory = entryCat[currentMidi];
      }
      n->patternCategory = std::clamp(n->patternCategory, 0, catTotal - 1);
      const bool midiCat = n->patternCategory >= catCount;
      std::vector<DrumPickItem> inCat;
      int shownK = -1;
      if (!midiCat)
      {
         for (int i = 0; i < count; i++)
            if (strcmp(all[i].category, cats[n->patternCategory]) == 0)
            {
               DrumPickItem it;
               it.lib = i;
               it.name = all[i].name;
               char row[160];
               snprintf(row, sizeof(row), "%s   ~%d bpm", all[i].name, all[i].bpm);
               it.row = row;
               if (i == current)
                  shownK = (int)inCat.size();
               inCat.push_back(std::move(it));
            }
      }
      // The group's MIDI files after its library grooves (all of them for a
      // "MIDI: <style>" group), tagged so they read apart from the library.
      for (size_t e = 0; e < midi->entries.size(); e++)
      {
         if (entryCat[e] != n->patternCategory)
            continue;
         const DrumMidi::Entry& en = midi->entries[e];
         DrumPickItem it;
         it.path = en.path;
         it.name = en.title;
         it.row = en.title + "   MIDI";
         if (!midiCat && !en.group.empty())
            it.row += " " + en.group;
         if (en.unreadable)
            it.row += ", unreadable";
         else if (en.empty)
            it.row += ", no drum notes";
         else
         {
            if (en.bpm > 0)
               it.row += ", orig " + std::to_string(en.bpm) + " bpm";
            if (en.bars > 0)
               it.row += ", " + std::to_string(en.bars) + (en.bars == 1 ? " bar" : " bars");
         }
         if ((int)e == currentMidi)
            shownK = (int)inCat.size();
         inCat.push_back(std::move(it));
      }
      INode* target = n;
      const int part = std::clamp(n->patternPart, 0, 2);

      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float full = AudioFullWidth();
      constexpr float kPinSlot = 18.0f; // a discrete param's pin + its spacing
      const float arrowW = ImGui::GetFrameHeight() + kPinSlot;
      const float catW = std::floor((full - arrowW * 2.0f - gap * 3.0f) * 0.40f);
      const float grooveW = full - catW - arrowW * 2.0f - gap * 3.0f;

      // Category: switching it shows that list; nothing changes until a groove is picked.
      const std::string catLabel = midiCat ? "MIDI: " + extraGroups[n->patternCategory - catCount]
                                           : std::string(cats[n->patternCategory]);
      if (ImGui::Button((catLabel + "##drumCat").c_str(), ImVec2(catW, 0)))
      {
         std::vector<std::string> names(cats, cats + catCount);
         std::vector<std::string> groups(catCount, "pattern library");
         for (const std::string& g : extraGroups)
            names.push_back("MIDI: " + g);
         names.push_back(midi->scanning ? "MIDI: rescan folder (scanning...)" : "MIDI: rescan folder");
         names.push_back(midiGroups == 0 ? "MIDI: open folder (put .mid files there)" : "MIDI: open folder");
         groups.resize(names.size(), "your MIDI folder");
         PrepareDropdown(names, groups, n->patternCategory, [target, catTotal](int c) {
            if (c == catTotal)
            {
               DrumMidi::RequestScan();
               return;
            }
            if (c == catTotal + 1)
            {
               const std::string dir = DrumMidi::DefaultFolder();
               if (!dir.empty())
                  Platform::RevealInFileManager(dir);
               return;
            }
            if (DrumSequencerNode* d = LiveDrumNode(target))
            {
               d->patternCategory = c; // browse it; the grid and the A / B / C groove stay
               d->browsingCategory = true;
            }
         }, true);
      }
      if (ImGui::IsItemHovered())
      {
         if (midi->entries.empty())
            ImGui::SetTooltip("pattern library: groups\nyour own .mid drum patterns: put them (in subfolders or with an "
                              "index.json) in\n%s\nthen pick \"MIDI: rescan folder\"",
                              midi->root.empty() ? DrumMidi::DefaultFolder().c_str() : midi->root.c_str());
         else
            ImGui::SetTooltip("pattern library: groups, then your MIDI folder (%d files)\n%s", (int)midi->entries.size(),
                              midi->root.c_str());
      }
      ImGui::SameLine(0.0f, gap);
      const bool inThisCat = shownK >= 0;
      std::vector<std::string> grooveNames;
      for (const DrumPickItem& it : inCat)
         grooveNames.push_back(it.name);

      // Groove: a selector over the shown category's list (index into it).
      // A drive applies the groove with the current part when it changes.
      {
         DiscreteParamRef ref;
         if (!inCat.empty())
            ref = BeginDiscreteParam("groove##drumGroove", (float)std::max(0, shownK), 0.0f,
                                     (float)inCat.size() - 1.0f, 3, &grooveNames);
         const int driven = DiscreteDrivenChange(ref, (int)inCat.size());
         if (driven >= 0 && driven != shownK)
         {
            PushUndoForDrivenChange(ref);
            DrumApplyItem(target, inCat[driven], part);
         }
         std::string grooveCaption = inThisCat ? inCat[shownK].name : std::string("choose a groove...");
         // A MIDI groove whose file is not in the folder (moved, or loaded by path over MCP).
         if (!inThisCat && !n->browsingCategory && n->IsMidiGroove())
            grooveCaption = DrumMidi::TitleFromFileName(n->MidiGroovePath());
         grooveCaption += "##drumGrooveBtn";
         const float btnW = ref.valid ? std::max(24.0f, grooveW - kPinSlot) : grooveW;
         if (ImGui::Button(grooveCaption.c_str(), ImVec2(btnW, 0)))
         {
            std::vector<std::string> names;
            for (const DrumPickItem& it : inCat)
               names.push_back(it.row);
            if (names.empty())
               n->importStatus = "no grooves in this group: rescan the MIDI folder";
            else
               PrepareDropdown(names, {}, shownK, [target, inCat, part](int k) {
                  if (k >= 0 && k < (int)inCat.size())
                     DrumApplyItem(target, inCat[k], part);
               }, true);
         }
         const bool hovered = ImGui::IsItemHovered();
         if (hovered)
         {
            if (inThisCat && inCat[shownK].lib >= 0 && current >= 0)
               ImGui::SetTooltip("%s - suggested tempo %d bpm\nLanes: 1 kick, 2 snare, 3 closed hat, 4 open hat, 5 clap/rim,\n"
                                 "6 low tom/conga, 7 high tom/conga, 8 bell/ride. Empty lanes get the Turbo kit.",
                                 all[current].name, all[current].bpm);
            else if (n->IsMidiGroove() && !n->browsingCategory)
            {
               const int bpm = n->MidiGrooveBpm();
               ImGui::SetTooltip("%s\nfrom your MIDI folder%s (the tempo is not changed)\nA, B, C: the file's first "
                                 "distinct chunks of bars (copies of A for a short file)\n%s",
                                 grooveCaption.substr(0, grooveCaption.find("##")).c_str(),
                                 bpm > 0 ? (", original tempo " + std::to_string(bpm) + " bpm").c_str() : "",
                                 n->MidiGroovePath().c_str());
            }
            else if (midiCat)
               ImGui::SetTooltip("pick a MIDI file: A / B / C get its first chunks of bars, rate and swing from the file\n"
                                 "(GM drum map; empty lanes get the Turbo kit; the tempo is not changed)");
            else
               ImGui::SetTooltip("pick a groove: fills the grid, steps, rate and swing (empty lanes get the Turbo kit)");
         }
         EndDiscreteParam(ref, hovered);
      }
      auto step = [&](int dir) {
         if (inCat.empty())
            return;
         int k = 0;
         if (inThisCat)
            k = shownK + dir;
         else
            k = dir > 0 ? 0 : (int)inCat.size() - 1;
         k = (k % (int)inCat.size() + (int)inCat.size()) % (int)inCat.size();
         PushUndoCheckpoint();
         DrumApplyItem(target, inCat[k], part);
      };
      ImGui::SameLine(0.0f, gap);
      if (ModTriggerButton("prev groove##drumPrev", ImVec2(arrowW, 0), "<##drumPrevBtn"))
         step(-1);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("previous groove");
      ImGui::SameLine(0.0f, gap);
      if (ModTriggerButton("next groove##drumNext", ImVec2(arrowW, 0), ">##drumNextBtn"))
         step(1);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("next groove");

      // Parts: A verse, B bridge, C chorus. The lit one is what the grid holds.
      // The row starts with the "part" selector's pin (0 A, 1 B, 2 C): a CV,
      // MIDI knob or Performance selector switches the part when it changes.
      {
         static const std::vector<std::string> kPartNames = {
            DrumPatterns::PartName(0), DrumPatterns::PartName(1), DrumPatterns::PartName(2) };
         DiscreteParamRef ref = BeginDiscreteParam("part##drumPartSel", (float)part, 0.0f, 2.0f, 3, &kPartNames);
         const bool pinHovered = ref.valid && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
         const int driven = DiscreteDrivenChange(ref, 3);
         if (driven >= 0 && driven != part)
         {
            PushUndoForDrivenChange(ref);
            n->SwitchPart(driven); // Turbo 0.50: keeps each part's edits, rate and swing
            gPatchDirty = true;
         }
         if (pinHovered)
            ImGui::SetTooltip("part selector (A / B / C) for CV, MIDI or a Performance selector");
         EndDiscreteParam(ref, pinHovered);
      }
      const float partW = (full - kPinSlot - gap * 2.0f) / 3.0f;
      for (int pi = 0; pi < 3; pi++)
      {
         if (pi > 0)
            ImGui::SameLine(0.0f, gap);
         // Turbo 0.50: parts work without a library groove too (a manual,
         // imported or preset pattern), and each keeps its own edits.
         const bool lit = pi == part;
         if (lit)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
         char label[32];
         snprintf(label, sizeof(label), "%s##drumPart%d", DrumPatterns::PartName(pi), pi);
         // A momentary trigger per part, so a MIDI pad or Perf trigger fires it.
         if (ModTriggerButton(label, ImVec2(partW, 0)) && pi != part)
         {
            PushUndoCheckpoint();
            n->SwitchPart(pi);
            gPatchDirty = true;
         }
         if (lit)
            ImGui::PopStyleColor();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nyour edits stay with each part; rate and swing are shared",
                              pi == 0 ? "A: the main groove (verse)"
                              : pi == 1 ? "B: lighter / breakdown variation (bridge)"
                                        : "C: fuller variation, often two bars ending in a fill (chorus)");
      }

      // Turbo 0.50: user presets (all three parts, rate, swing, accents) and
      // a revert of the live part to the library groove.
      {
         const float revertW = 58.0f;
         const float saveW = 46.0f;
         const float listW = std::floor((full - revertW - saveW - gap * 3.0f) * 0.5f);
         const float nameW = full - revertW - saveW - listW - gap * 3.0f;
         std::string listCaption = n->presetName.empty() ? std::string("presets...") : n->presetName;
         listCaption += "##drumPresetList";
         if (ImGui::Button(listCaption.c_str(), ImVec2(listW, 0)))
         {
            std::vector<std::string> names = DrumSequencerNode::ListPresets();
            if (names.empty())
               n->importStatus = "no presets yet: type a name and press save";
            else
               PrepareDropdown(names, {}, -1, [target, names](int k) {
                  DrumSequencerNode* d = LiveDrumNode(target);
                  if (d == nullptr || k < 0 || k >= (int)names.size())
                     return;
                  PushUndoCheckpoint();
                  std::string err;
                  if (d->LoadPreset(names[k], err))
                  {
                     d->importStatus = "preset loaded: " + names[k];
                     d->LoadKitIntoEmptyLanes(TurboDrumKitDir());
                  }
                  else
                     d->importStatus = err;
                  gPatchDirty = true;
               }, true);
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("load a saved drum preset (all parts, rate, swing)");
         ImGui::SameLine(0.0f, gap);
         static std::map<INode*, std::string> sPresetDraft; // per node, UI only
         std::string& draft = sPresetDraft[target];
         char buf[96];
         snprintf(buf, sizeof(buf), "%s", draft.c_str());
         ImGui::SetNextItemWidth(nameW);
         if (ImGui::InputTextWithHint("##drumPresetName", "preset name", buf, sizeof(buf)))
            draft = buf;
         ImGui::SameLine(0.0f, gap);
         ImGui::BeginDisabled(draft.empty());
         if (ImGui::Button("save##drumPresetSave", ImVec2(saveW, 0)))
         {
            std::string err;
            if (n->SavePreset(draft, err))
            {
               n->importStatus = "preset saved: " + n->presetName;
               draft.clear();
            }
            else
               n->importStatus = err;
         }
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("save all three parts, rate, swing and accents as a preset\n(same name overwrites)");
         ImGui::SameLine(0.0f, gap);
         ImGui::BeginDisabled(current < 0 && !n->IsMidiGroove());
         if (ImGui::Button("revert##drumRevert", ImVec2(revertW, 0)))
         {
            PushUndoCheckpoint();
            n->RevertPart();
            gPatchDirty = true;
         }
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("reload this part from the library groove or MIDI file (drops your edits to it)");
      }
   }

   void DrawDrumLaneCard(DrumSequencerNode* n, int lane)
   {
      ImGui::PushID(lane);
      // Cached for drag-drop lane resolution, covering the *whole* card
      // (header, buttons, waveform, sliders) rather than just the waveform
      // strip - a drop anywhere on the card should land on this lane, not
      // only inside the narrow black waveform box. GetCursorScreenPos()
      // here is canvas space, not real screen space - see the fields'
      // comment on DrumSequencerNode and DrumSequencerLaneForCanvasPos.
      const ImVec2 cardTop = ImGui::GetCursorScreenPos();
      n->laneCardCanvasX0[lane] = cardTop.x;
      n->laneCardCanvasY0[lane] = cardTop.y;
      n->laneCardCanvasX1[lane] = cardTop.x + gAudioContentW;
      char header[48];
      const std::string& fn = n->FileName(lane);
      if (fn.empty())
         snprintf(header, sizeof(header), "lane %d - (%s)", lane + 1, DrumPatterns::LaneRole(lane));
      else
      {
         std::string trimmed = fn.size() > 20 ? fn.substr(0, 19) + "." : fn;
         snprintf(header, sizeof(header), "lane %d - %s", lane + 1, trimmed.c_str());
      }
      BeginAudioSection(header);

      {
         const float btnW = 20.0f;
         const float rowY = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - btnW, rowY));
         if (ModTriggerButton("x##drumClearLane", ImVec2(btnW, 0)))
         {
            PushUndoCheckpoint();
            n->ClearLane(lane);
         }
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowY));
         static const std::vector<std::string> kChokeNames = {"choke -", "choke 1", "choke 2", "choke 3"};
         AudioBareDropdown("drumChoke", kChokeNames, n->laneChoke[lane],
                           [n, lane](int value) { PushUndoCheckpoint(); n->laneChoke[lane] = value; }, 70.0f);
         ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight()));
      }

      DrawDrumLaneWaveform(n, lane, 84.0f);
      ImGui::Dummy(ImVec2(0.0f, 3.0f));

      // Horizontal sliders, two per row, rather than a knob row - each is
      // shorter than a knob-plus-caption, and the height that frees up goes
      // to the waveform above instead (same two-per-row rhythm Sampler's
      // body uses). pitch pairs with fine tune (mirrors Sampler's own
      // pitch/finetune pair) rather than leaving pan alone on an odd row.
      {
         const float half = AudioHalfWidth();
         AudioSlider("transient", &n->laneTransient[lane], -1.0f, 1.0f, "%.2f", half);
         ImGui::SameLine();
         AudioSlider("decay", &n->laneDecay[lane], 0.0f, 1.0f, "%.2f", half);
         AudioSlider("pitch", &n->lanePitch[lane], -24.0f, 24.0f, "%.1f st", half);
         ImGui::SameLine();
         AudioSlider("fine tune", &n->laneFineTune[lane], -50.0f, 50.0f, "%.0f c", half);
         AudioSlider("volume", &n->laneVolume[lane], 0.0f, 1.0f, "%.2f", half);
         ImGui::SameLine();
         AudioSlider("pan", &n->lanePan[lane], -1.0f, 1.0f, "%.2f", half);
         // Turbo 0.47: pitch of the accented steps (two-tone bells).
         AudioSlider("accent pitch", &n->laneAccentPitch[lane], -24.0f, 24.0f, "%.1f st", half);
      }

      EndAudioSection();
      n->laneCardCanvasY1[lane] = ImGui::GetCursorScreenPos().y;
      ImGui::PopID();
   }

   void DrawDrumSequencerBody(GraphNode& gn, DrumSequencerNode* n)
   {
      char stat[80];
      if (n->run)
         snprintf(stat, sizeof(stat), "%d steps - %s - %d loaded", std::clamp(n->numSteps, 1, DrumSequencerNode::kMaxSteps),
                  MusicTime::RateDivisionName(n->rate), n->LoadedLaneCount());
      else
         snprintf(stat, sizeof(stat), "stopped - %d steps - %s", std::clamp(n->numSteps, 1, DrumSequencerNode::kMaxSteps),
                  MusicTime::RateDivisionName(n->rate));

      BeginAudioBody(gn.index, gn.category, kAudioWideWidth, stat);

      // Turbo 0.54: kit picker (bundled kits; replaces every lane, undoable).
      {
         static std::unordered_map<int, int> sKitChoice; // node index -> picked kit (UI only)
         const std::vector<BundledKit>& kits = BundledKits();
         if (!kits.empty())
         {
            std::vector<std::string> names;
            names.reserve(kits.size());
            for (const BundledKit& k : kits)
               names.push_back(k.name);
            auto it = sKitChoice.find(gn.index);
            int current = it != sKitChoice.end() ? it->second : -1;
            if (current < 0) // not picked here yet: show the default kit a new node gets
            {
               const std::string def = TurboDrumKitDir();
               for (size_t k = 0; k < kits.size(); ++k)
                  if (kits[k].dir == def)
                     current = (int)k;
            }
            const int nodeIndex = gn.index;
            DropdownButton("kit", names, current, [n, nodeIndex](int idx)
            {
               const std::vector<BundledKit>& list = BundledKits();
               if (idx < 0 || idx >= (int)list.size())
                  return;
               PushUndoCheckpoint();
               n->LoadKit(list[idx].dir);
               sKitChoice[nodeIndex] = idx;
               gPatchDirty = true;
            }, 220.0f, /*modulatable=*/false);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
         }
      }

      // ---- lane cards: 4 rows x 2 columns, left column lanes 1-4, right
      // column lanes 5-8, matching the mockup. BeginAudioColumns is the
      // §1k mechanism for a node wider than kAudioNodeWidth - unlike
      // Wavetable's A/B engines, both columns here are the *same* kind of
      // content (four stacked lane cards each), not two parallel modes.
      {
         BeginAudioColumns(2);
         BeginAudioColumn(0);
         for (int lane = 0; lane < 4; lane++)
         {
            DrawDrumLaneCard(n, lane);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
         }
         EndAudioColumn();
         BeginAudioColumn(1);
         for (int lane = 4; lane < DrumSequencerNode::kNumLanes; lane++)
         {
            DrawDrumLaneCard(n, lane);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
         }
         EndAudioColumn();
         EndAudioColumns();
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // ---- step grid: the visualizer, and the primary editable control ----
      {
         const int steps = std::clamp(n->numSteps, 1, DrumSequencerNode::kMaxSteps);
         const int pageCount = std::max(1, (steps + 15) / 16);
         n->editPage = std::clamp(n->editPage, 0, pageCount - 1);
         if (ImGui::SmallButton("<##drumPage")) n->editPage = std::max(0, n->editPage - 1);
         ImGui::SameLine();
         ImGui::TextDisabled("steps %d-%d / %d", n->editPage * 16 + 1,
                             std::min(steps, n->editPage * 16 + 16), steps);
         ImGui::SameLine();
         if (ImGui::SmallButton(">##drumPage")) n->editPage = std::min(pageCount - 1, n->editPage + 1);
         const int firstStep = n->editPage * 16;
         const int visibleSteps = std::max(1, std::min(16, steps - firstStep));
         // Each micro control now carries a CV pin. Keep enough gutter for
         // pin + compact button without overlapping the first step cell.
         const float toggleW = 30.0f;
         const float gutterW = toggleW * 3.0f + 4.0f; // R | M | S
         const float rowH = 28.0f;
         const float rowGap = 2.0f;
         const float cellGap = 1.0f;
         const float stepsW = gAudioBodyW - gutterW;
         const float cellW = (stepsW - cellGap * (float)(visibleSteps - 1)) / (float)visibleSteps;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         n->gridCanvasTopY = origin.y;
         n->gridCanvasRowH = rowH + rowGap;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const int curStep = n->CurrentStep();
         const double beatsPerBar = Transport::Instance().BeatsPerBar();
         const double beatsPerStep = std::max(1e-6, MusicTime::BeatsFor((MusicTime::RateDivision)n->rate));
         const int stepsPerBar = std::max(1, (int)std::lround(beatsPerBar / beatsPerStep));

         for (int lane = 0; lane < DrumSequencerNode::kNumLanes; lane++)
         {
            const float y0 = origin.y + (float)lane * (rowH + rowGap);
            ImGui::PushID(lane);

            // ---- gutter: randomise, mute, solo ------------------------
            ImGui::SetCursorScreenPos(ImVec2(origin.x, y0));
            if (ModTriggerButton("R##drumRandomLane", ImVec2(toggleW, rowH)))
            {
               PushUndoCheckpoint();
               n->RandomizeLane(lane);
            }
            ImGui::SameLine(0.0f, 2.0f);
            bool muteBool = n->laneMute[lane];
            if (AudioToggleButton("M##mute", &muteBool, toggleW))
            {
               PushUndoCheckpoint();
               n->laneMute[lane] = muteBool;
            }
            ImGui::SameLine(0.0f, 2.0f);
            bool soloBool = n->laneSolo[lane];
            if (AudioToggleButton("S##solo", &soloBool, toggleW))
            {
               PushUndoCheckpoint();
               n->laneSolo[lane] = soloBool;
            }

            // ---- steps -----------------------------------------------------
            for (int visible = 0; visible < visibleSteps; visible++)
            {
               const int s = firstStep + visible;
               const float x0 = origin.x + gutterW + (float)visible * (cellW + cellGap);
               ImGui::PushID(s);
               ImGui::SetCursorScreenPos(ImVec2(x0, y0));
               ImGui::InvisibleButton("cell", ImVec2(cellW, rowH));
               float& vel = n->stepVel[lane][s];

               if (ImGui::IsItemActivated())
               {
                  PushUndoCheckpoint();
                  const bool wasOn = vel > 0.0f;
                  vel = wasOn ? 0.0f : 0.8f;
                  gDrumGridDrag.active = true;
                  gDrumGridDrag.paintOn = !wasOn;
                  gDrumGridDrag.originLane = lane;
                  gDrumGridDrag.originStep = s;
               }
               const bool isOrigin = (gDrumGridDrag.originLane == lane && gDrumGridDrag.originStep == s);
               if (isOrigin && ImGui::IsItemActive() && vel > 0.0f &&
                   ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
               {
                  const float my = ImGui::GetIO().MousePos.y;
                  const float t = 1.0f - std::clamp((my - y0) / rowH, 0.0f, 1.0f);
                  vel = std::clamp(0.05f + t * 0.95f, 0.05f, 1.0f);
                  char buf[32];
                  snprintf(buf, sizeof(buf), "%.2f", vel);
                  SetAudioReadout("velocity", buf);
               }
               else if (gDrumGridDrag.active && !isOrigin && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
               {
                  vel = gDrumGridDrag.paintOn ? 0.8f : 0.0f;
               }

               const bool isBar = (s % stepsPerBar) == 0;
               const ImU32 frameCol = isBar ? IM_COL32(60, 64, 78, 255) : IM_COL32(40, 43, 53, 255);
               dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cellW - cellGap, y0 + rowH), frameCol, 2.0f);
               if (vel > 0.0f)
               {
                  const float fillTop = y0 + (1.0f - vel) * rowH;
                  const ImU32 fillCol = (s == curStep && n->run) ? IM_COL32(255, 200, 100, 255)
                                                                  : IM_COL32(120, 200, 255, 220);
                  dl->AddRectFilled(ImVec2(x0, fillTop), ImVec2(x0 + cellW - cellGap, y0 + rowH), fillCol, 2.0f);
               }
               if (s == curStep)
                  dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + cellW - cellGap, y0 + rowH), IM_COL32(255, 255, 255, 130),
                              2.0f);

               ImGui::PopID();
            }

            ImGui::PopID();
         }
         if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            gDrumGridDrag.active = false;

         ImGui::SetCursorScreenPos(
            ImVec2(origin.x, origin.y + (float)DrumSequencerNode::kNumLanes * (rowH + rowGap) + 4.0f));
         ImGui::Dummy(ImVec2(gAudioBodyW, 1.0f));
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // ---- global controls: rate + 3 pattern knobs on top, then the four
      // offset knobs that compose on top of every lane's own value
      // (DrumSequencerNode's class comment / PushDirtyParams) below -
      // symmetric 4-and-4 so neither row reads as an afterthought tacked
      // onto the other. "output" here is the node's own post-mix level;
      // there is no second "volume" knob in the offsets row below - a
      // per-lane multiplicative volume offset duplicated what this one
      // already does, so it was folded away rather than kept as a
      // confusable second control with the same effect.
      {
         AudioKnobRow row(4);
         row.Dropdown("rate", MusicTime::RateDivisionList(), n->rate,
                      [n](int i) { PushUndoCheckpoint(); n->rate = i; });
         row.KnobInt("steps", &n->numSteps, 1, DrumSequencerNode::kMaxSteps);
         row.Knob("swing", &n->swing, 0.0f, 1.0f, "%.2f");
         row.Knob("output", &n->volume, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("transient", &n->globalTransient, -1.0f, 1.0f, "%.2f");
         row.Knob("decay", &n->globalDecay, -1.0f, 1.0f, "%.2f");
         row.Knob("pitch", &n->globalPitch, -24.0f, 24.0f, "%.1f st");
         row.Knob("pan", &n->globalPan, -1.0f, 1.0f, "%.2f");
         row.End();
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      // Turbo 0.47: pattern library picker (category, groove, A / B / C).
      DrawDrumPatternPicker(n);
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      {
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float btnW = (AudioFullWidth() - gap * 3.0f) / 4.0f;
         AudioToggleButton(n->run ? "Stop##drumrun" : "Run##drumrun", &n->run, btnW);
         ImGui::SameLine();
         if (ModTriggerButton("Randomise##drumRandomise", ImVec2(btnW, 0)))
         {
            PushUndoCheckpoint();
            n->Randomize();
         }
         ImGui::SameLine();
         if (ModTriggerButton("Clear##drumClear", ImVec2(btnW, 0)))
         {
            PushUndoCheckpoint();
            n->ClearPattern();
         }
         // Turbo 0.50: a .mid drum pattern into the live part (dropping a
         // .mid on the node does the same).
         ImGui::SameLine();
         if (ImGui::Button("Import .mid##drumImport", ImVec2(btnW, 0)))
            StartNodeFileDialog(n, Platform::OpenMidiDialog, [](auto* d, const std::string& path) {
               PushUndoCheckpoint();
               std::string msg;
               d->ImportMidiFile(path, msg);
               gPatchDirty = true;
            });
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("import a drum pattern from a MIDI file into this part\n"
                              "(GM drum notes: kick, snare, hats, clap/rim, toms, bells/cymbals)");
      }
      if (!n->importStatus.empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + AudioFullWidth());
         ImGui::TextDisabled("%s", n->importStatus.c_str());
         ImGui::PopTextWrapPos();
      }

      EndAudioBody();
   }

   struct MediaThumbnail
   {
      unsigned int texture = 0;
      int width = 0;
      int height = 0;
      bool attempted = false;
      uint64_t lastUsedFrame = 0;
   };

   std::unordered_map<std::string, MediaThumbnail> gMediaThumbnails;
   uint64_t gMediaThumbnailFrame = 0;

   MediaThumbnail* GetMediaThumbnail(const std::string& path)
   {
      if (!HasExtension(path, kImageExt))
         return nullptr;

      MediaThumbnail& thumb = gMediaThumbnails[path];
      thumb.lastUsedFrame = ++gMediaThumbnailFrame;
      if (thumb.attempted)
         return thumb.texture != 0 ? &thumb : nullptr;
      thumb.attempted = true;

      // Keep GPU memory bounded even for libraries containing thousands of
      // large photos. Only visible rows are decoded, then reduced to a tiny
      // texture before upload; the full decoded pixels are immediately freed.
      if (gMediaThumbnails.size() > 160)
      {
         auto victim = gMediaThumbnails.end();
         for (auto it = gMediaThumbnails.begin(); it != gMediaThumbnails.end(); ++it)
            if (it->first != path && it->second.texture != 0 &&
                (victim == gMediaThumbnails.end() || it->second.lastUsedFrame < victim->second.lastUsedFrame))
               victim = it;
         if (victim != gMediaThumbnails.end())
         {
            glDeleteTextures(1, &victim->second.texture);
            gMediaThumbnails.erase(victim);
         }
      }

      std::vector<unsigned char> source;
      int sourceW = 0, sourceH = 0;
      std::string error;
      if (!Platform::LoadImageRGBA(path, source, sourceW, sourceH, error) || sourceW <= 0 || sourceH <= 0)
         return nullptr;

      const float scale = std::min(1.0f, std::min(96.0f / (float)sourceW, 72.0f / (float)sourceH));
      const int w = std::max(1, (int)std::round(sourceW * scale));
      const int h = std::max(1, (int)std::round(sourceH * scale));
      std::vector<unsigned char> pixels((size_t)w * (size_t)h * 4);
      for (int y = 0; y < h; ++y)
      {
         const int sy = std::min(sourceH - 1, y * sourceH / h);
         for (int x = 0; x < w; ++x)
         {
            const int sx = std::min(sourceW - 1, x * sourceW / w);
            const unsigned char* src = &source[((size_t)sy * sourceW + sx) * 4];
            unsigned char* dst = &pixels[((size_t)y * w + x) * 4];
            std::memcpy(dst, src, 4);
         }
      }

      glGenTextures(1, &thumb.texture);
      glBindTexture(GL_TEXTURE_2D, thumb.texture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      thumb.width = w;
      thumb.height = h;
      return &thumb;
   }

   void ClearMediaThumbnails()
   {
      for (auto& entry : gMediaThumbnails)
         if (entry.second.texture != 0)
            glDeleteTextures(1, &entry.second.texture);
      gMediaThumbnails.clear();
   }

   // The Samples/Media modes of the docked node-browser panel (docs/plans/
   // audio/README.md P3e): folder list management, a background-thread scan
   // (SampleScanner), filter-as-you-type over the persisted index, and a
   // drag source per result row that gSampleDragActive (above) resolves on
   // release into either a new node or an existing matching one's file.
   // Shared by both modes - idPrefix keeps the two modes' ImGui widget IDs
   // (and therefore their input focus/state) from colliding, searchHint is
   // the mode-specific placeholder text, and mediaKind tags the drag so the
   // release handler knows whether to resolve it against Sampler or against
   // Image Source/Video.

// Infinite-Turbo 0.46: RPC / MCP tools for Turbo-only features (Clip Matrix,
// MPC / VMPC pads, Looper, Performance Mode; 0.47: Drum Sequencer patterns). Included by main.cpp inside its
// anonymous namespace after PerfPanel.inl; dispatched from HandleRpcCommand.

int RpcPerfKind(const nlohmann::json& v)
{
   if (v.is_number_integer())
      return std::clamp(v.get<int>(), 0, 9);
   static const char* kNames[] = { "knob", "fader", "slider", "toggle", "xy", "trigger",
                                   "numbox", "selector", "bipolar", "stepgate" };
   const std::string want = v.is_string() ? RpcNorm(v.get<std::string>()) : std::string();
   for (int i = 0; i < 10; i++)
      if (want == kNames[i])
         return i;
   return -1;
}

nlohmann::json RpcPerfElementJson(size_t i)
{
   static const char* kNames[] = { "knob", "fader", "slider", "toggle", "xy", "trigger",
                                   "numbox", "selector", "bipolar", "stepgate" };
   const Patch::PerfRecord& e = gPerfElements[i];
   nlohmann::json j = { { "element", (int)i }, { "kind", kNames[std::clamp(e.kind, 0, 9)] },
                        { "label", e.label }, { "page", e.page }, { "cell", { e.cellX, e.cellY } } };
   if (e.dstIndex >= 0)
   {
      j["node"] = e.dstIndex;
      const ParamRef* p = Modulation::Instance().KnownParam(e.dstIndex, e.dstParam);
      j["param"] = p != nullptr ? nlohmann::json(p->name) : nlohmann::json(e.dstParam);
      if (e.kind == 4 && e.dstParam2 >= 0)
      {
         const ParamRef* p2 = Modulation::Instance().KnownParam(e.dstIndex, e.dstParam2);
         j["param_y"] = p2 != nullptr ? nlohmann::json(p2->name) : nlohmann::json(e.dstParam2);
      }
   }
   // Turbo 0.50: extra destinations driven with the same value / bang.
   if (!e.targets.empty())
   {
      nlohmann::json extra = nlohmann::json::array();
      for (const Patch::PerfTarget& t : e.targets)
      {
         const ParamRef* p = Modulation::Instance().KnownParam(t.dstIndex, t.dstParam);
         extra.push_back({ { "node", t.dstIndex },
                           { "param", !t.boolName.empty() ? nlohmann::json(t.boolName) : p != nullptr ? nlohmann::json(p->name) : nlohmann::json(t.dstParam) } });
      }
      j["extra_targets"] = extra;
   }
   if (e.midiDevice != 0)
      j["midi"] = std::string(e.midiIsNote ? "note " : "cc ") + std::to_string(e.midiController) +
                  " ch " + std::to_string(e.midiChannel + 1);
   return j;
}

bool HandleRpcCommandTurbo(const std::string& method, const nlohmann::json& params,
                           nlohmann::json& outResult, std::string& outError, bool& handled)
{
   using json = nlohmann::json;
   handled = true;

   if (method == "clip_matrix")
   {
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      auto* cm = gn ? dynamic_cast<ClipMatrixNode*>(gn->node.get()) : nullptr;
      if (cm == nullptr)
      {
         outError = "index is not a Clip Matrix node";
         return false;
      }
      const std::string action = params.value("action", std::string("state"));
      const int row = params.value("row", 0), col = params.value("col", 0);
      if (action == "launch") cm->Launch(row, col);
      else if (action == "release") cm->Release(row, col);
      else if (action == "stop_row") cm->StopRow(row);
      else if (action == "scene") cm->LaunchScene(col);
      else if (action == "stop_all") cm->StopAll();
      else if (action != "state")
      {
         outError = "action: launch, release, stop_row, scene, stop_all or state";
         return false;
      }
      json rows = json::array();
      for (int r = 0; r < cm->rows; r++)
      {
         json cells = json::array();
         for (int c = 0; c < cm->cols; c++)
         {
            const ClipMatrixNode::Cell& cell = cm->CellAt(r, c);
            cells.push_back(cell.kind == ClipMatrixNode::kEmpty ? json(nullptr) : json(cell.name.empty() ? cell.path : cell.name));
         }
         rows.push_back({ { "row", r }, { "playing", cm->PlayingCol(r) }, { "queued", cm->QueuedCol(r) }, { "cells", cells } });
      }
      outResult = { { "rows", rows }, { "note", "launches wait for the quantize grid while the transport plays" } };
      return true;
   }
   if (method == "pads")
   {
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      INode* n = gn ? gn->node.get() : nullptr;
      auto* mpc = dynamic_cast<MpcNode*>(n);
      auto* vmpc = dynamic_cast<VmpcNode*>(n);
      if (mpc == nullptr && vmpc == nullptr)
      {
         outError = "index is not an MPC or VMPC node";
         return false;
      }
      const int pad = std::clamp(params.value("pad", 0), 0, 15);
      const float vel = std::clamp(params.value("velocity", 1.0f), 0.0f, 1.0f);
      const std::string action = params.value("action", std::string("hit"));
      // Turbo 0.48: optional per-pad settings (MPC), applied before the action.
      // Saved as pad<n>_speed / _fine / _fadein / _fadeout / _sync / _div.
      // Everything (action included) is validated first: a bad param errors
      // out with no undo checkpoint and nothing changed.
      const bool actionOk = action == "hit" || action == "down" || action == "up" || action == "state" ||
                            (action == "stop_all" && vmpc != nullptr);
      if (!actionOk)
      {
         outError = "action: hit, down, up, state (VMPC also stop_all)";
         return false;
      }
      struct FloatSet
      {
         const char* key;
         float lo, hi;
         bool has = false;
         float value = 0.0f;
      };
      FloatSet padFloats[] = { { "speed", -2.0f, 2.0f }, { "fine", -50.0f, 50.0f },
                               { "fade_in", 0.0f, 250.0f }, { "fade_out", 0.0f, 250.0f } };
      bool hasSync = false, syncOn = false;
      int newDiv = -1;
      for (FloatSet& f : padFloats)
         if (params.contains(f.key))
         {
            if (mpc == nullptr || !params[f.key].is_number())
            {
               outError = std::string(f.key) + (mpc == nullptr ? ": MPC only" : ": a number");
               return false;
            }
            f.has = true;
            f.value = std::clamp(params[f.key].get<float>(), f.lo, f.hi);
         }
      if (params.contains("sync"))
      {
         if (mpc == nullptr || !params["sync"].is_boolean())
         {
            outError = mpc == nullptr ? "sync: MPC only" : "sync: true or false";
            return false;
         }
         hasSync = true;
         syncOn = params["sync"].get<bool>();
      }
      if (params.contains("division"))
      {
         if (params["division"].is_number_integer())
            newDiv = params["division"].get<int>();
         else if (params["division"].is_string())
            for (int i = 0; i < MusicTime::kNumRateDivisions; i++)
               if (params["division"].get<std::string>() == MusicTime::RateDivisionName(i))
                  newDiv = i;
         if (mpc == nullptr || newDiv < 0 || newDiv >= MusicTime::kNumRateDivisions)
         {
            outError = mpc == nullptr ? "division: MPC only"
                                      : "division: a MusicTime division name (\"1/4\", \"1/8T\", \"1 bar\"...) or index 0..18";
            return false;
         }
      }
      const bool anySet = hasSync || newDiv >= 0 || padFloats[0].has || padFloats[1].has || padFloats[2].has ||
                          padFloats[3].has;
      if (anySet)
      {
         PushUndoCheckpoint(); // validated: one checkpoint before the changes
         float* dsts[] = { &mpc->padSpeed[pad], &mpc->padFine[pad], &mpc->padFadeIn[pad], &mpc->padFadeOut[pad] };
         for (int i = 0; i < 4; i++)
            if (padFloats[i].has)
               *dsts[i] = padFloats[i].value;
         if (hasSync)
            mpc->padSync[pad] = syncOn ? MpcNode::kSynced : MpcNode::kFree;
         if (newDiv >= 0)
            mpc->padDiv[pad] = newDiv;
         gPatchDirty = true;
      }
      auto event = [&](bool down) { if (mpc) mpc->PadEvent(pad, down, vel); else vmpc->PadEvent(pad, down, vel); };
      if (action == "hit") { event(true); event(false); }
      else if (action == "down") event(true);
      else if (action == "up") event(false);
      else if (action == "stop_all") vmpc->StopAll();
      outResult = { { "pad", pad }, { "playing", mpc ? mpc->PadPlaying(pad) : vmpc->PadPlaying(pad) } };
      if (mpc != nullptr)
      {
         outResult["speed"] = mpc->padSpeed[pad];
         outResult["fine"] = mpc->padFine[pad];
         outResult["fade_in"] = mpc->padFadeIn[pad];
         outResult["fade_out"] = mpc->padFadeOut[pad];
         outResult["sync"] = mpc->padSync[pad] == MpcNode::kSynced;
         outResult["division"] = MusicTime::RateDivisionName(mpc->padDiv[pad]);
      }
      return true;
   }
   if (method == "looper")
   {
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      auto* lp = gn ? dynamic_cast<LooperNode*>(gn->node.get()) : nullptr;
      if (lp == nullptr)
      {
         outError = "index is not a Looper node";
         return false;
      }
      const std::string a = params.value("action", std::string("state"));
      // Turbo 0.48: optional playback settings, applied before the action
      // (saved keys speed, pitch, finetune, fadeIn, fadeOut, volume,
      // lengthMode 3 = Division with takeDivision). Everything (action
      // included) is validated first: a bad param errors out with no undo
      // checkpoint and nothing changed.
      static const char* kActions[] = { "record", "stop_record", "play", "stop", "overdub",
                                        "stop_overdub", "clear", "undo", "redo", "state" };
      if (std::find(std::begin(kActions), std::end(kActions), a) == std::end(kActions))
      {
         outError = "action: record, stop_record, play, stop, overdub, stop_overdub, clear, undo, redo or state";
         return false;
      }
      struct FloatSet
      {
         const char* key;
         float* dst;
         float lo, hi;
         bool has = false;
         float value = 0.0f;
      };
      FloatSet lpFloats[] = { { "speed", &lp->speed, -2.0f, 2.0f },       { "pitch", &lp->pitch, -24.0f, 24.0f },
                              { "finetune", &lp->finetune, -50.0f, 50.0f }, { "fade_in", &lp->fadeIn, 0.0f, 250.0f },
                              { "fade_out", &lp->fadeOut, 0.0f, 250.0f },  { "volume", &lp->volume, 0.0f, 2.0f } };
      bool anySet = false;
      for (FloatSet& f : lpFloats)
         if (params.contains(f.key))
         {
            if (!params[f.key].is_number())
            {
               outError = std::string(f.key) + ": a number";
               return false;
            }
            f.has = anySet = true;
            f.value = std::clamp(params[f.key].get<float>(), f.lo, f.hi);
         }
      int newMode = -1, newDiv = -1, newBars = -1, newSub = -1, newInTime = -1;
      // Turbo 0.48: `length` as the take-length menu reads: "free", "1/16 bar",
      // "1/8 bar", "1/4 bar", "1/2 bar", "N bars" (or a number of bars).
      if (params.contains("length"))
      {
         const json& l = params["length"];
         std::string t = l.is_string() ? RpcNorm(l.get<std::string>()) : std::string();
         int nb = l.is_number_integer() ? l.get<int>() : -1;
         if (t == "free")
            newMode = LooperNode::kLengthFree;
         else if (t == "1/2bar" || t == "1/2")
            newMode = LooperNode::kLengthSubBar, newSub = 0;
         else if (t == "1/4bar" || t == "1/4" || t == "1beat")
            newMode = LooperNode::kLengthSubBar, newSub = 1;
         else if (t == "1/8bar" || t == "1/8")
            newMode = LooperNode::kLengthSubBar, newSub = 2;
         else if (t == "1/16bar" || t == "1/16")
            newMode = LooperNode::kLengthSubBar, newSub = 3;
         else
         {
            if (nb < 0 && !t.empty())
            {
               const size_t digits = t.find_first_not_of("0123456789");
               if (digits > 0 && (digits == std::string::npos || t.compare(digits, std::string::npos, "bar") == 0 ||
                                  t.compare(digits, std::string::npos, "bars") == 0))
                  nb = std::atoi(t.c_str());
            }
            if (nb < 1 || nb > 32)
            {
               outError = "length: \"free\", \"1/16 bar\", \"1/8 bar\", \"1/4 bar\", \"1/2 bar\" or 1..32 bars (\"2 bars\" or 2)";
               return false;
            }
            newMode = LooperNode::kLengthBars;
            newBars = nb;
         }
         anySet = true;
      }
      if (params.contains("in_time"))
      {
         if (!params["in_time"].is_boolean())
         {
            outError = "in_time: true or false";
            return false;
         }
         newInTime = params["in_time"].get<bool>() ? 1 : 0;
         anySet = true;
      }
      if (params.contains("take"))
      {
         // "free", "bars" (keeps `bars`), or a MusicTime division name / index.
         const json& t = params["take"];
         const std::string name = t.is_string() ? t.get<std::string>() : std::string();
         int d = t.is_number_integer() ? t.get<int>() : -1;
         for (int i = 0; i < MusicTime::kNumRateDivisions && d < 0 && !name.empty(); i++)
            if (name == MusicTime::RateDivisionName(i))
               d = i;
         if (name == "free")
            newMode = LooperNode::kLengthFree;
         else if (name == "bars")
            newMode = LooperNode::kLengthBars;
         else if (d >= 0 && d < MusicTime::kNumRateDivisions)
         {
            newMode = LooperNode::kLengthDivision;
            newDiv = d;
         }
         else
         {
            outError = "take: \"free\", \"bars\" or a MusicTime division (\"1 bar\", \"2 bars\", \"1/4\", \"1/8T\"...)";
            return false;
         }
         anySet = true;
      }
      if (anySet)
      {
         PushUndoCheckpoint(); // validated: one checkpoint before the changes
         for (const FloatSet& f : lpFloats)
            if (f.has)
               *f.dst = f.value;
         if (newMode >= 0)
            lp->lengthMode = newMode;
         if (newDiv >= 0)
            lp->takeDivision = newDiv;
         if (newBars >= 0)
            lp->bars = newBars;
         if (newSub >= 0)
            lp->subDivision = newSub;
         if (newInTime >= 0)
            lp->syncStart = newInTime == 1;
         gPatchDirty = true;
      }
      if (a == "record") lp->SetRecord(true);
      else if (a == "stop_record") lp->SetRecord(false);
      else if (a == "play") lp->SetPlay(true);
      else if (a == "stop") lp->SetPlay(false);
      else if (a == "overdub") lp->SetOverdub(true);
      else if (a == "stop_overdub") lp->SetOverdub(false);
      else if (a == "clear") lp->Clear();
      else if (a == "undo") lp->UndoLayer();
      else if (a == "redo") lp->RedoLayer();
      outResult = { { "recording", lp->IsRecordingOrArmed() }, { "playing", lp->IsPlaying() },
                    { "overdubbing", lp->IsOverdubbing() }, { "has_loop", lp->HasLoop() } };
      static const char* kModes[] = { "bars", "sub-bar", "free", "division" };
      outResult["length_mode"] = kModes[std::clamp(lp->lengthMode, 0, 3)];
      if (lp->lengthMode == LooperNode::kLengthDivision)
         outResult["take"] = MusicTime::RateDivisionName(lp->takeDivision);
      outResult["speed"] = lp->speed;
      outResult["pitch"] = lp->pitch;
      outResult["finetune"] = lp->finetune;
      outResult["fade_in"] = lp->fadeIn;
      outResult["fade_out"] = lp->fadeOut;
      outResult["volume"] = lp->volume;
      outResult["at_unity"] = lp->AtUnity(); // false: drifts against the transport, overdub paused
      {
         static const char* kSubs[] = { "1/2 bar", "1/4 bar", "1/8 bar", "1/16 bar" };
         std::string len;
         if (lp->lengthMode == LooperNode::kLengthFree)
            len = "free";
         else if (lp->lengthMode == LooperNode::kLengthSubBar)
            len = kSubs[std::clamp(lp->subDivision, 0, 3)];
         else if (lp->lengthMode == LooperNode::kLengthBars)
            len = std::to_string(lp->bars) + (lp->bars == 1 ? " bar" : " bars");
         else
            len = std::string(MusicTime::RateDivisionName(lp->takeDivision)) + " (division)";
         outResult["length"] = len;
      }
      outResult["in_time"] = lp->syncStart;
      outResult["waiting_s"] = lp->CurrentState() == LooperNode::kArmed ? lp->ArmedSeconds() : lp->PlayWaitSeconds();
      return true;
   }
   if (method == "drum_pattern")
   {
      // Turbo 0.47: the Drum Sequencer's pattern library. Without `pattern`
      // it lists the grooves; with it, fills the grid like the picker (empty
      // lanes get the bundled kit unless kit is false). part: A / B / C.
      int count = 0;
      const DrumPatterns::Groove* all = DrumPatterns::All(count);
      // Turbo 0.50: the user's MIDI folder. The UI scans it in the
      // background; before the first scan finished, a quick listing (files
      // and index.json, no file reads) answers this call.
      if (params.value("rescan", false))
         DrumMidi::RequestScan();
      else
         DrumMidi::EnsureScanned();
      std::shared_ptr<const DrumMidi::Library> midiLib = DrumMidi::Current();
      if (!midiLib->scanned)
         midiLib = std::make_shared<DrumMidi::Library>(DrumMidi::Scan(DrumMidi::DefaultFolder(), false));
      // Turbo 0.50: presets, .mid import and part switching, each on a node.
      if (params.value("list_presets", false))
      {
         outResult = { { "presets", DrumSequencerNode::ListPresets() },
                       { "folder", DrumSequencerNode::PresetDirectory() } };
         return true;
      }
      if (params.contains("import") || params.contains("save_preset") || params.contains("load_preset") ||
          (params.contains("part") && !params.contains("pattern") && params.contains("index")))
      {
         GraphNode* tgn = FindNodeByIndex(params.value("index", -1));
         auto* d = tgn ? dynamic_cast<DrumSequencerNode*>(tgn->node.get()) : nullptr;
         if (d == nullptr)
         {
            outError = "index is not a Drum Sequencer node";
            return false;
         }
         auto partOf = [](const json& pv) {
            if (pv.is_number())
               return (int)std::lround(pv.get<double>());
            if (pv.is_string() && !pv.get<std::string>().empty())
            {
               const char ch = (char)std::toupper((unsigned char)pv.get<std::string>()[0]);
               return ch == 'A' || ch == '0' ? 0 : (ch == 'B' || ch == '1' ? 1 : (ch == 'C' || ch == '2' ? 2 : -1));
            }
            return -1;
         };
         std::string msg;
         if (params.contains("save_preset"))
         {
            if (!params["save_preset"].is_string() || !d->SavePreset(params["save_preset"].get<std::string>(), msg))
            {
               outError = msg.empty() ? "save_preset: a name" : msg;
               return false;
            }
            outResult = { { "saved", d->presetName }, { "folder", DrumSequencerNode::PresetDirectory() } };
            return true;
         }
         PushUndoCheckpoint();
         if (params.contains("load_preset"))
         {
            if (!params["load_preset"].is_string() || !d->LoadPreset(params["load_preset"].get<std::string>(), msg))
            {
               outError = msg.empty() ? "load_preset: a name from list_presets" : msg;
               return false;
            }
            if (params.value("kit", true))
               d->LoadKitIntoEmptyLanes(TurboDrumKitDir());
            msg = "preset loaded: " + d->presetName;
            d->importStatus = msg;
         }
         if (params.contains("part"))
         {
            const int part = partOf(params["part"]);
            if (part < 0 || part > 2)
            {
               outError = "part: A, B or C (0-2)";
               return false;
            }
            d->SwitchPart(part); // before an import, so the import lands in that part
         }
         if (params.contains("import"))
         {
            if (!params["import"].is_string() || !d->ImportMidiFile(params["import"].get<std::string>(), msg))
            {
               outError = msg.empty() ? "import: a .mid file path" : msg;
               return false;
            }
         }
         gPatchDirty = true;
         outResult = { { "message", msg }, { "part", DrumPatterns::PartName(d->patternPart) },
                       { "steps", d->numSteps }, { "rate", MusicTime::RateDivisionName(d->rate) },
                       { "swing", d->swing } };
         return true;
      }
      if (!params.contains("pattern"))
      {
         const std::string onlyCat = params.contains("category") && params["category"].is_string()
                                        ? RpcNorm(params["category"].get<std::string>()) : std::string();
         const std::string onlySource = params.value("source", std::string());
         json list = json::array();
         for (int i = 0; i < count && onlySource != "midi"; i++)
         {
            if (!onlyCat.empty() && RpcNorm(all[i].category).find(onlyCat) == std::string::npos)
               continue;
            list.push_back({ { "pattern", i }, { "source", "library" }, { "category", all[i].category },
                             { "name", all[i].name }, { "bpm", all[i].bpm },
                             { "steps", { all[i].parts[0].steps, all[i].parts[1].steps, all[i].parts[2].steps } } });
         }
         // Turbo 0.50: the user's MIDI files ("MIDI: <style>"), loaded by path or name.
         for (size_t e = 0; e < midiLib->entries.size() && onlySource != "library"; e++)
         {
            const DrumMidi::Entry& en = midiLib->entries[e];
            // Folded into a built-in group by style, like the picker; else "MIDI: <style>".
            int catCount = 0;
            const char* const* cats = DrumPatterns::Categories(catCount);
            const int ci = DrumPatterns::StyleToCategory(en.group);
            const std::string cat = ci >= 0 && ci < catCount ? std::string(cats[ci]) : "MIDI: " + en.group;
            if (!onlyCat.empty() && RpcNorm(cat).find(onlyCat) == std::string::npos &&
                RpcNorm(en.group).find(onlyCat) == std::string::npos)
               continue;
            json row = { { "source", "midi" }, { "category", cat }, { "style", en.group }, { "name", en.title },
                         { "path", en.path }, { "orig_bpm", en.bpm } };
            if (en.bars > 0)
               row["bars"] = en.bars;
            if (en.empty)
               row["empty"] = true;
            if (en.unreadable)
               row["unreadable"] = true;
            list.push_back(row);
         }
         json lanes = json::array();
         for (int l = 0; l < 8; l++)
            lanes.push_back(DrumPatterns::LaneRole(l));
         outResult = { { "patterns", list }, { "lanes", lanes },
                       { "parts", { "A verse", "B bridge", "C chorus (often 2 bars with a fill)" } },
                       { "midi_folder", midiLib->root.empty() ? DrumMidi::DefaultFolder() : midiLib->root },
                       { "midi_files", midiLib->entries.size() } };
         if (!midiLib->error.empty())
            outResult["midi_error"] = midiLib->error;
         if (midiLib->truncated)
            outResult["midi_truncated"] = true;
         return true;
      }
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      auto* drum = gn ? dynamic_cast<DrumSequencerNode*>(gn->node.get()) : nullptr;
      if (drum == nullptr)
      {
         outError = "index is not a Drum Sequencer node";
         return false;
      }
      int pick = -1;
      std::string midiPath; // Turbo 0.50: a MIDI groove instead of a library one
      const json& want = params["pattern"];
      const bool midiOnly = params.value("source", std::string()) == "midi";
      if (want.is_number_integer() && !midiOnly)
         pick = want.get<int>();
      else if (want.is_string())
      {
         std::string raw = want.get<std::string>();
         const bool prefixed = raw.rfind(DrumSequencerNode::kMidiPrefix, 0) == 0;
         if (prefixed)
            raw = raw.substr(5);
         std::string lower = raw;
         std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         auto endsWith = [&](const char* sfx) {
            const size_t n = strlen(sfx);
            return lower.size() >= n && lower.compare(lower.size() - n, n, sfx) == 0;
         };
         // Library names hold slashes ("Calypso / soca"): a path is a .mid
         // name, a "midi:" key, or an absolute path.
         const bool isPath = prefixed || endsWith(".mid") || endsWith(".midi") ||
                             (raw.size() > 2 && raw[1] == ':' && (raw[2] == '\\' || raw[2] == '/')) ||
                             (!raw.empty() && (raw[0] == '/' || raw[0] == '\\'));
         const std::string w = RpcNorm(raw);
         if (isPath)
         {
            // A full path, or one relative to the MIDI folder.
            std::filesystem::path pth = std::filesystem::u8path(raw);
            if (pth.is_relative() && !midiLib->root.empty())
               pth = std::filesystem::u8path(midiLib->root) / pth;
            midiPath = pth.lexically_normal().u8string();
            // A bare file name may sit in a subfolder: match the listing's tail.
            std::error_code ec;
            if (!std::filesystem::exists(pth, ec))
            {
               std::string wantTail = lower;
               std::replace(wantTail.begin(), wantTail.end(), '\\', '/');
               for (const DrumMidi::Entry& en : midiLib->entries)
               {
                  std::string tail = en.path;
                  std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                  std::replace(tail.begin(), tail.end(), '\\', '/');
                  if (tail.size() > wantTail.size() && tail[tail.size() - wantTail.size() - 1] == '/' &&
                      tail.compare(tail.size() - wantTail.size(), wantTail.size(), wantTail) == 0)
                  {
                     midiPath = en.path;
                     break;
                  }
               }
            }
         }
         // Library exact, MIDI exact, library partial, MIDI partial.
         for (int i = 0; i < count && pick < 0 && midiPath.empty() && !midiOnly; i++)
            if (RpcNorm(all[i].name) == w)
               pick = i;
         for (size_t e = 0; e < midiLib->entries.size() && pick < 0 && midiPath.empty(); e++)
            if (RpcNorm(midiLib->entries[e].title) == w)
               midiPath = midiLib->entries[e].path;
         for (int i = 0; i < count && pick < 0 && midiPath.empty() && !midiOnly; i++) // then a partial match
            if (RpcNorm(all[i].name).find(w) != std::string::npos)
               pick = i;
         for (size_t e = 0; e < midiLib->entries.size() && pick < 0 && midiPath.empty(); e++)
            if (RpcNorm(midiLib->entries[e].title).find(w) != std::string::npos)
               midiPath = midiLib->entries[e].path;
      }
      if (midiPath.empty() && (pick < 0 || pick >= count))
      {
         outError = "unknown pattern (call drum_pattern without pattern for the list)";
         return false;
      }
      int part = 0;
      if (params.contains("part"))
      {
         const json& pv = params["part"];
         part = -1;
         if (pv.is_number())
            part = (int)std::lround(pv.get<double>());
         else if (pv.is_string() && !pv.get<std::string>().empty())
         {
            const char ch = (char)std::toupper((unsigned char)pv.get<std::string>()[0]);
            part = ch == 'A' || ch == '0' ? 0 : (ch == 'B' || ch == '1' ? 1 : (ch == 'C' || ch == '2' ? 2 : -1));
         }
      }
      if (part < 0 || part > 2)
      {
         outError = "part: A, B or C (0-2)";
         return false;
      }
      if (!midiPath.empty())
      {
         // A bad file leaves the node untouched (the undo step is then a no-op).
         PushUndoCheckpoint();
         std::string msg;
         if (!drum->SelectMidiGroove(midiPath, part, params.value("reset", false), msg))
         {
            outError = msg;
            return false;
         }
         drum->browsingCategory = false;
         int midiKit = 0;
         if (params.value("kit", true))
            midiKit = drum->LoadKitIntoEmptyLanes(TurboDrumKitDir());
         gPatchDirty = true;
         outResult = { { "source", "midi" }, { "path", drum->MidiGroovePath() },
                       { "part", DrumPatterns::PartName(part) }, { "steps", drum->numSteps },
                       { "rate", MusicTime::RateDivisionName(drum->rate) }, { "swing", drum->swing },
                       { "orig_bpm", drum->MidiGrooveBpm() }, { "message", msg }, { "kit_lanes_loaded", midiKit } };
         return true;
      }
      PushUndoCheckpoint();
      // Turbo 0.50: like the picker, edits of each groove and part are kept;
      // reset:true reloads the library groove.
      drum->SelectGroove(pick, part, params.value("reset", false));
      int kitLanes = 0;
      if (params.value("kit", true))
         kitLanes = drum->LoadKitIntoEmptyLanes(TurboDrumKitDir());
      gPatchDirty = true;
      outResult = { { "pattern", pick }, { "name", all[pick].name }, { "part", DrumPatterns::PartName(part) },
                    { "steps", drum->numSteps }, { "suggested_bpm", all[pick].bpm }, { "kit_lanes_loaded", kitLanes } };
      return true;
   }
   if (method == "perf_list")
   {
      json els = json::array();
      for (size_t i = 0; i < gPerfElements.size(); i++)
         els.push_back(RpcPerfElementJson(i));
      json pages = json::array();
      for (int p = 0; p < std::max(1, gPerfLayout.pageCount); p++)
         pages.push_back(p < (int)gPerfLayout.pageNames.size() && !gPerfLayout.pageNames[p].empty()
                            ? gPerfLayout.pageNames[p] : "Page " + std::to_string(p + 1));
      outResult = { { "open", gPerfPanelOpen }, { "pages", pages }, { "active_page", gPerfActivePage }, { "elements", els } };
      return true;
   }
   if (method == "perf_add")
   {
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      if (gn == nullptr)
      {
         outError = "unknown node index";
         return false;
      }
      const RpcParamInfo* p = RpcFindParam(*gn, params.contains("param") ? params["param"] : json());
      if (p == nullptr)
      {
         outError = "param not found among the node's drawn params (explain lists them; a brand-new node "
                    "needs a frame to draw - try again)";
         return false;
      }
      // Turbo 0.50: element = an existing control: add this param as one
      // more destination of it (one button, several params).
      if (params.contains("element"))
      {
         const int e = params.value("element", -1);
         if (e < 0 || e >= (int)gPerfElements.size())
         {
            outError = "unknown element (perf_list numbers them)";
            return false;
         }
         Patch::PerfRecord& rec = gPerfElements[(size_t)e];
         if (rec.dstIndex < 0 || rec.dstParam < 0)
         {
            PushUndoCheckpoint();
            rec.dstIndex = gn->index; // an empty control: this becomes its primary
            rec.dstParam = p->paramIndex;
         }
         else
         {
            bool dup = rec.dstIndex == gn->index && rec.dstParam == p->paramIndex;
            for (const Patch::PerfTarget& t : rec.targets)
               dup = dup || (t.dstIndex == gn->index && t.dstParam == p->paramIndex);
            if (!dup)
            {
               PushUndoCheckpoint();
               rec.targets.push_back({ gn->index, p->paramIndex, std::string() });
            }
         }
         outResult = RpcPerfElementJson((size_t)e);
         return true;
      }
      const ParamRef* known = Modulation::Instance().KnownParam(gn->index, p->paramIndex);
      int kind = params.contains("kind") ? RpcPerfKind(params["kind"])
                                         : (known && known->isBool ? 3 : known && known->isEnum ? 7 : known && known->momentary ? 5 : 0);
      if (kind < 0)
      {
         outError = "kind: knob, fader, slider, toggle, xy, trigger, numbox, selector, bipolar, stepgate";
         return false;
      }
      int paramY = -1;
      if (kind == 4 && params.contains("param_y"))
      {
         const RpcParamInfo* py = RpcFindParam(*gn, params["param_y"]);
         if (py == nullptr)
         {
            outError = "param_y not found on that node";
            return false;
         }
         paramY = py->paramIndex;
      }
      const int savedPage = gPerfActivePage;
      if (params.contains("page"))
      {
         const int page = std::max(0, params.value("page", 0));
         while (gPerfLayout.pageCount <= page && gPerfLayout.pageCount < 12)
         {
            gPerfLayout.pageNames.resize((size_t)gPerfLayout.pageCount);
            gPerfLayout.pageNames.push_back("Page " + std::to_string(gPerfLayout.pageCount + 1));
            gPerfLayout.pageCount++;
         }
         gPerfActivePage = std::min(page, gPerfLayout.pageCount - 1);
      }
      AddToPerformanceMatrix(gn->index, p->paramIndex, kind, params.value("label", std::string()), paramY);
      if (params.contains("page"))
         gPerfActivePage = savedPage;
      outResult = RpcPerfElementJson(gPerfElements.size() - 1);
      return true;
   }
   if (method == "perf_remove")
   {
      const int e = params.value("element", -1);
      if (e < 0 || e >= (int)gPerfElements.size())
      {
         outError = "unknown element (perf_list numbers them)";
         return false;
      }
      PushUndoCheckpoint();
      PerfResetIndexState();
      gPerfElements.erase(gPerfElements.begin() + e);
      outResult = { { "remaining", (int)gPerfElements.size() } };
      return true;
   }
   if (method == "perf_show")
   {
      if (params.contains("open")) gPerfPanelOpen = params.value("open", true);
      if (params.contains("perform")) gPerfEditMode = !params.value("perform", false);
      if (params.contains("page")) gPerfActivePage = std::clamp(params.value("page", 0), 0, std::max(0, gPerfLayout.pageCount - 1));
      if (params.contains("dock"))
      {
         const std::string d = params.value("dock", std::string("bottom"));
         gPerfPanelDock = d == "right" ? 1 : d == "left" ? 2 : d == "top" ? 3 : 0;
      }
      outResult = { { "open", gPerfPanelOpen }, { "perform", !gPerfEditMode }, { "page", gPerfActivePage } };
      return true;
   }
   // Turbo 0.50: Scenes node. Settings first (all validated before anything
   // changes), then the action. Cell values are in the user's units: 0/1 for
   // on/off and pulse, a 0-based choice, or a 0..1 level. Scene -1 = off row.
   if (method == "scenes")
   {
      GraphNode* gn = FindNodeByIndex(params.value("index", -1));
      auto* sc = gn ? dynamic_cast<ScenesNode*>(gn->node.get()) : nullptr;
      if (sc == nullptr)
      {
         outError = "index is not a Scenes node";
         return false;
      }
      const std::string action = params.value("action", std::string("state"));
      if (action != "state" && action != "go" && action != "press" && action != "off" && action != "next" &&
          action != "prev" && action != "cancel")
      {
         outError = "action: state, go (scene), press (scene, toggles), off, next, prev, cancel";
         return false;
      }
      auto countOk = [&](const char* key) {
         return !params.contains(key) || (params[key].is_number_integer() && params[key].get<int>() >= 1 &&
                                          params[key].get<int>() <= ScenesNode::kMax);
      };
      if (!countOk("scenes") || !countOk("outputs"))
      {
         outError = "scenes / outputs: 1..8";
         return false;
      }
      int quant = -1;
      if (params.contains("quantize"))
      {
         const json& q = params["quantize"];
         if (q.is_number_integer())
            quant = q.get<int>();
         else if (q.is_string())
            for (int i = 0; i < ScenesNode::kQuantCount; i++)
               if (RpcNorm(q.get<std::string>()) == RpcNorm(ScenesNode::QuantName(i)))
                  quant = i;
         if (quant < 0 || quant >= ScenesNode::kQuantCount)
         {
            outError = "quantize: 0 immediate, 1 next beat, 2 next bar, 3 2 bars, 4 4 bars (number or name)";
            return false;
         }
      }
      if (params.contains("press_again_off") && !params["press_again_off"].is_boolean())
      {
         outError = "press_again_off: true or false";
         return false;
      }
      if (params.contains("names") && !params["names"].is_array())
      {
         outError = "names: an array of scene names";
         return false;
      }
      // Mode names; "value" / "smooth" from the first draft still parse.
      auto parseMode = [](const std::string& m) -> int {
         const std::string k = RpcNorm(m);
         if (k == "on/off" || k == "onoff" || k == "on" || k == "toggle" || k == "switch")
            return ScenesNode::kOnOff;
         if (k == "choice" || k == "value" || k == "stepped")
            return ScenesNode::kChoice;
         if (k == "level" || k == "smooth" || k == "continuous")
            return ScenesNode::kLevel;
         if (k == "pulse" || k == "trigger")
            return ScenesNode::kPulse;
         return -1;
      };
      if (params.contains("outs"))
      {
         if (!params["outs"].is_array() || params["outs"].size() > (size_t)ScenesNode::kMax)
         {
            outError = "outs: up to 8 objects {label, mode: on/off|choice|level|pulse, steps: auto|2..128 (choice)}";
            return false;
         }
         for (const json& o : params["outs"])
         {
            if (!o.is_object())
            {
               outError = "outs: objects {label, mode, steps}";
               return false;
            }
            if (o.contains("mode") && !(o["mode"].is_string() && parseMode(o["mode"].get<std::string>()) >= 0))
            {
               outError = "outs[].mode: on/off, choice, level or pulse";
               return false;
            }
            if (o.contains("steps") && !(o["steps"].is_string() ? (o["steps"].get<std::string>() == "auto" || o["steps"].get<std::string>() == "smooth")
                                                                : (o["steps"].is_number_integer() && o["steps"].get<int>() >= 2 && o["steps"].get<int>() <= 128)))
            {
               outError = "outs[].steps (choice): \"auto\" or 2..128";
               return false;
            }
         }
      }
      auto rowOk = [](const json& row) {
         bool ok = row.is_array() && row.size() <= (size_t)ScenesNode::kMax;
         if (ok)
            for (const json& v : row)
               ok = ok && (v.is_number() || v.is_boolean() || v.is_null() || v.is_string());
         return ok;
      };
      if (params.contains("grid"))
      {
         bool ok = params["grid"].is_array() && params["grid"].size() <= (size_t)ScenesNode::kMax;
         if (ok)
            for (const json& row : params["grid"])
               ok = ok && rowOk(row);
         if (!ok)
         {
            outError = "grid: rows (scenes) of cells (outputs); null keeps a cell";
            return false;
         }
      }
      if (params.contains("off") && !rowOk(params["off"]))
      {
         outError = "off: one row of cells (outputs), the state with no scene playing";
         return false;
      }
      if (params.contains("cells"))
      {
         bool ok = params["cells"].is_array();
         if (ok)
            for (const json& c : params["cells"])
               ok = ok && c.is_array() && c.size() == 3 && c[0].is_number_integer() && c[1].is_number_integer() &&
                    (c[2].is_number() || c[2].is_boolean() || c[2].is_string()) && c[0].get<int>() >= ScenesNode::kOff &&
                    c[0].get<int>() < ScenesNode::kMax && c[1].get<int>() >= 0 && c[1].get<int>() < ScenesNode::kMax;
         if (!ok)
         {
            outError = "cells: [[scene, output, value], ...] (0-based, scene -1 = off row)";
            return false;
         }
      }
      if ((action == "go" || action == "press") && !params.contains("scene"))
      {
         outError = "go / press need scene (0-based; go accepts -1 = off)";
         return false;
      }

      const bool edits = params.contains("scenes") || params.contains("outputs") || quant >= 0 || params.contains("names") ||
                         params.contains("outs") || params.contains("grid") || params.contains("cells") ||
                         params.contains("off") || params.contains("press_again_off");
      if (edits)
      {
         PushUndoCheckpoint();
         gPatchDirty = true;
         if (params.contains("scenes")) sc->scenes = params["scenes"].get<int>();
         if (params.contains("outputs")) sc->outputs = params["outputs"].get<int>();
         if (quant >= 0) sc->quantize = quant;
         if (params.contains("press_again_off")) sc->pressAgainOff = params["press_again_off"].get<bool>();
         if (params.contains("names"))
         {
            size_t i = 0;
            for (const json& nm : params["names"])
               if (i < (size_t)ScenesNode::kMax)
                  sc->sceneName[i++] = nm.is_string() ? nm.get<std::string>() : std::string();
         }
         if (params.contains("outs"))
         {
            size_t i = 0;
            for (const json& o : params["outs"])
            {
               if (o.contains("label") && o["label"].is_string()) sc->label[i] = o["label"].get<std::string>();
               if (o.contains("mode")) sc->mode[i] = parseMode(o["mode"].get<std::string>());
               if (o.contains("steps"))
               {
                  if (o["steps"].is_string() && o["steps"].get<std::string>() == "smooth")
                     sc->mode[i] = ScenesNode::kLevel; // first-draft spelling
                  else
                     sc->steps[i] = o["steps"].is_string() ? -1 : o["steps"].get<int>();
               }
               i++;
            }
         }
      }
      // Auto choices come from the cables: resolve before reading or writing cells.
      ScenesTarget targets[ScenesNode::kMax];
      ScenesResolve(sc, gn->index, targets);
      // A cell: number, bool, "on"/"off", or a choice by its dropdown name.
      auto cellValue = [&](const json& v, int o, float& out) {
         if (v.is_boolean())
            out = v.get<bool>() ? 1.0f : 0.0f;
         else if (v.is_number())
            out = v.get<float>();
         else
         {
            const std::string k = RpcNorm(v.get<std::string>());
            if (k == "on" || k == "true")
               out = 1.0f;
            else if (k == "off" || k == "false")
               out = 0.0f;
            else
            {
               const ParamRef* p = targets[o].param;
               if (p == nullptr || !p->isEnum)
                  return false;
               // Exact name first, then a prefix ("A" finds "A verse").
               for (int pass = 0; pass < 2; pass++)
                  for (size_t i = 0; i < p->enumOptions.size(); i++)
                  {
                     const std::string opt = RpcNorm(p->enumOptions[i]);
                     if (pass == 0 ? opt == k : (!k.empty() && opt.compare(0, k.size(), k) == 0))
                     {
                        out = (float)i;
                        return true;
                     }
                  }
               return false;
            }
         }
         return true;
      };
      std::string cellErrors;
      auto setCell = [&](int s, int o, const json& v) {
         float f = 0.0f;
         if (cellValue(v, o, f))
            sc->SetCellUser(s, o, f);
         else if (cellErrors.size() < 200)
            cellErrors += "output " + std::to_string(o) + ": unknown choice \"" + v.get<std::string>() + "\"; ";
      };
      if (params.contains("grid"))
      {
         int r = 0;
         for (const json& row : params["grid"])
         {
            int c = 0;
            for (const json& v : row)
            {
               if (!v.is_null())
                  setCell(r, c, v);
               c++;
            }
            r++;
         }
      }
      if (params.contains("off"))
      {
         int c = 0;
         for (const json& v : params["off"])
         {
            if (!v.is_null())
               setCell(ScenesNode::kOff, c, v);
            c++;
         }
      }
      if (params.contains("cells"))
         for (const json& c : params["cells"])
            setCell(c[0].get<int>(), c[1].get<int>(), c[2]);

      const bool now = params.value("now", false);
      if (action == "go")
         sc->Request(params.value("scene", 0), true, now);
      else if (action == "press")
         sc->Press(params.value("scene", 0));
      else if (action == "off")
         sc->Request(ScenesNode::kOff, false, now);
      else if (action == "next")
         sc->Step(1);
      else if (action == "prev")
         sc->Step(-1);
      else if (action == "cancel")
         sc->CancelPending();

      json outs = json::array(), values = json::array();
      for (int o = 0; o < sc->Outputs(); o++)
      {
         json jo = { { "output", o }, { "label", sc->OutputLabel(o) },
                     { "mode", ScenesNode::ModeName(sc->mode[o]) },
                     { "target", ScenesTargetText(targets[o]) } };
         if (sc->mode[o] == ScenesNode::kChoice)
         {
            jo["choices"] = sc->ChoiceCount(o);
            jo["steps_setting"] = sc->steps[o] < 0 ? json("auto") : json(sc->steps[o]);
            if (targets[o].param != nullptr && targets[o].param->isEnum && sc->ChoiceAutoResolved(o))
               jo["choice_names"] = targets[o].param->enumOptions;
         }
         outs.push_back(jo);
         values.push_back(sc->OutputValue(o));
      }
      json grid = json::array(), names = json::array(), offRow = json::array();
      for (int o = 0; o < sc->Outputs(); o++)
         offRow.push_back(sc->CellUser(ScenesNode::kOff, o));
      for (int s = 0; s < sc->Scenes(); s++)
      {
         json row = json::array();
         for (int o = 0; o < sc->Outputs(); o++)
            row.push_back(sc->CellUser(s, o));
         grid.push_back(row);
         names.push_back(ScenesSceneName(sc, s));
      }
      outResult = { { "current", sc->Current() }, { "current_name", ScenesSceneName(sc, sc->Current()) },
                    { "pending", sc->HasPending() ? json(sc->Pending()) : json(nullptr) },
                    { "quantize", ScenesNode::QuantName(sc->quantize) }, { "press_again_off", sc->pressAgainOff },
                    { "names", names }, { "outputs", outs }, { "off", offRow }, { "grid", grid },
                    { "output_values", values },
                    { "params", "mappable triggers: \"scene 1\"..\"scene 8\" (press: radio, again = off), \"all off\", "
                                "\"prev scene\", \"next scene\"; selector \"scene\" (0 = off, 1.. = scenes)" } };
      if (sc->HasPending())
         outResult["pending_in_beats"] = std::max(0.0, sc->PendingBeat() - Transport::Instance().Beats());
      if (!cellErrors.empty())
         outResult["warnings"] = cellErrors;
      return true;
   }
   handled = false;
   return false;
}

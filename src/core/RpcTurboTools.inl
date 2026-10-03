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
      if (!params.contains("pattern"))
      {
         const std::string onlyCat = params.contains("category") && params["category"].is_string()
                                        ? RpcNorm(params["category"].get<std::string>()) : std::string();
         json list = json::array();
         for (int i = 0; i < count; i++)
         {
            if (!onlyCat.empty() && RpcNorm(all[i].category).find(onlyCat) == std::string::npos)
               continue;
            list.push_back({ { "pattern", i }, { "category", all[i].category }, { "name", all[i].name },
                             { "bpm", all[i].bpm },
                             { "steps", { all[i].parts[0].steps, all[i].parts[1].steps, all[i].parts[2].steps } } });
         }
         json lanes = json::array();
         for (int l = 0; l < 8; l++)
            lanes.push_back(DrumPatterns::LaneRole(l));
         outResult = { { "patterns", list }, { "lanes", lanes },
                       { "parts", { "A verse", "B bridge", "C chorus (often 2 bars with a fill)" } } };
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
      const json& want = params["pattern"];
      if (want.is_number_integer())
         pick = want.get<int>();
      else if (want.is_string())
      {
         const std::string w = RpcNorm(want.get<std::string>());
         for (int i = 0; i < count && pick < 0; i++)
            if (RpcNorm(all[i].name) == w)
               pick = i;
         for (int i = 0; i < count && pick < 0; i++) // then a partial match
            if (RpcNorm(all[i].name).find(w) != std::string::npos)
               pick = i;
      }
      if (pick < 0 || pick >= count)
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
      PushUndoCheckpoint();
      drum->ApplyPattern(pick, part);
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
   handled = false;
   return false;
}

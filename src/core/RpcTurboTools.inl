// Infinite-Turbo 0.46: RPC / MCP tools for Turbo-only features (Clip Matrix,
// MPC / VMPC pads, Looper, Performance Mode). Included by main.cpp inside its
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
      auto event = [&](bool down) { if (mpc) mpc->PadEvent(pad, down, vel); else vmpc->PadEvent(pad, down, vel); };
      if (action == "hit") { event(true); event(false); }
      else if (action == "down") event(true);
      else if (action == "up") event(false);
      else if (action == "stop_all" && vmpc) vmpc->StopAll();
      else
      {
         outError = "action: hit, down, up (VMPC also stop_all)";
         return false;
      }
      outResult = { { "pad", pad }, { "playing", mpc ? mpc->PadPlaying(pad) : vmpc->PadPlaying(pad) } };
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
      if (a == "record") lp->SetRecord(true);
      else if (a == "stop_record") lp->SetRecord(false);
      else if (a == "play") lp->SetPlay(true);
      else if (a == "stop") lp->SetPlay(false);
      else if (a == "overdub") lp->SetOverdub(true);
      else if (a == "stop_overdub") lp->SetOverdub(false);
      else if (a == "clear") lp->Clear();
      else if (a == "undo") lp->UndoLayer();
      else if (a == "redo") lp->RedoLayer();
      else if (a != "state")
      {
         outError = "action: record, stop_record, play, stop, overdub, stop_overdub, clear, undo, redo or state";
         return false;
      }
      outResult = { { "recording", lp->IsRecordingOrArmed() }, { "playing", lp->IsPlaying() },
                    { "overdubbing", lp->IsOverdubbing() }, { "has_loop", lp->HasLoop() } };
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

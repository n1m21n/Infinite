// Part of src/main.cpp, included there in order (one translation unit).
// MCP remote control (RPC) helpers

   std::string RpcFindSettingKey(INode* node, const std::string& name)
   {
      std::vector<std::pair<std::string, std::string>> raw;
      Patch::SaveParams(node, raw);
      for (const auto& kv : raw)
      {
         const size_t sp = kv.first.find(' ');
         if (sp != std::string::npos && kv.first.substr(sp + 1) == name)
            return kv.first;
      }
      for (const auto& kv : raw)
      {
         const size_t sp = kv.first.find(' ');
         if (sp != std::string::npos && RpcNorm(kv.first.substr(sp + 1)) == RpcNorm(name))
            return kv.first;
      }
      return std::string();
   }

   bool RpcSetSetting(GraphNode& gn, const std::string& name, const nlohmann::json& v, std::string& outError)
   {
      const std::string matchedKey = RpcFindSettingKey(gn.node.get(), name);
      if (matchedKey.empty())
      {
         outError = "unknown setting '" + name + "' on " + gn.typeName + " (describe or explain lists them)";
         return false;
      }
      std::string valueStr;
      if (v.is_string())
         valueStr = v.get<std::string>();
      else if (v.is_boolean())
         valueStr = v.get<bool>() ? "1" : "0";
      else if (v.is_array() && matchedKey[0] == 'c')
      {
         for (const auto& e : v)
            valueStr += (valueStr.empty() ? "" : " ") + e.dump();
      }
      else
         valueStr = v.dump();
      Patch::LoadParams(gn.node.get(), { { matchedKey, valueStr } });
      // File paths and other text settings need their derived state rebuilt
      // (a plugin only when its identity changed - a reload loses its state).
      const bool isPlugin = dynamic_cast<AudioPluginNode*>(gn.node.get()) != nullptr;
      if (matchedKey[0] == 's' && (!isPlugin || matchedKey.find("plugin_id") != std::string::npos))
         ReloadDerivedState(gn.node.get());
      if (gn.node->RequiresAudioProcessing())
         RebuildAudioTopology();
      return true;
   }

   bool HandleRpcCommandBase(const std::string& method, const nlohmann::json& params,
                             nlohmann::json& outResult, std::string& outError)
   {
      using json = nlohmann::json;

      if (method == "list_node_types")
      {
         json categories = json::object();
         for (const std::string& cat : NodeFactory::Instance().GetCategories())
         {
            json names = json::array();
            for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
               names.push_back(n);
            categories[cat] = names;
         }
         outResult = categories;
         return true;
      }
      else if (method == "create_node")
      {
         std::string typeName = params.value("typeName", std::string());
         if (typeName.empty())
            typeName = params.value("type", std::string());
         const std::string category = params.value("category", std::string());
         // Turbo 0.45: no position given = auto layout once it has drawn.
         const bool autoPlace = !params.contains("x") || !params.contains("y");
         float x = params.value("x", 0.0f);
         float y = params.value("y", 0.0f);
         if (autoPlace)
         {
            float right = 0.0f, top = 0.0f;
            bool any = false;
            for (const GraphNode& other : gNodes)
            {
               right = any ? std::max(right, other.liveX) : other.liveX;
               top = any ? std::min(top, other.liveY) : other.liveY;
               any = true;
            }
            x = any ? right + 320.0f : 0.0f;
            y = top;
         }
         // Settings are checked on a throwaway instance first, so a bad key
         // leaves no node and no undo step behind.
         if (params.contains("settings") && params["settings"].is_object())
         {
            std::unique_ptr<INode> probe(NodeFactory::Instance().MakeNode(NodeFactory::CanonicalName(typeName)));
            if (probe)
               for (auto it = params["settings"].begin(); it != params["settings"].end(); ++it)
                  if (RpcFindSettingKey(probe.get(), it.key()).empty())
                  {
                     outError = "unknown setting '" + it.key() + "' on " + typeName + " (describe lists them)";
                     return false;
                  }
         }
         GraphNode* gn = SpawnNode(typeName, category, x, y);
         ApplySpawnDefaults(gn); // Turbo 0.54: MCP create_node gets the same defaults
         if (gn == nullptr)
         {
            outError = "unknown node type '" + typeName + "' (describe lists them)";
            return false;
         }
         gn->liveX = x;
         gn->liveY = y;
         if (params.contains("settings") && params["settings"].is_object())
         {
            const bool wasSuppressed = gSuppressUndoCheckpoints;
            gSuppressUndoCheckpoints = true; // part of the creation's undo step
            for (auto it = params["settings"].begin(); it != params["settings"].end(); ++it)
               RpcSetSetting(*gn, it.key(), it.value(), outError);
            gSuppressUndoCheckpoints = wasSuppressed;
            outError.clear();
         }
         if (params.value("show_params", false))
            gn->showParams = true;
         if (autoPlace && params.value("auto_layout", true))
         {
            gAutoLayoutPending.push_back(gn->index);
            gAutoLayoutWait = 2;
         }
         outResult = RpcDescribeNodeShape(*gn, false);
         outResult["index"] = gn->index;
         return true;
      }
      else if (method == "delete_node")
      {
         const int index = params.value("index", -1);
         if (FindNodeByIndex(index) == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         RemoveNodeByIndex(index);
         outResult = json::object();
         return true;
      }
      else if (method == "connect")
      {
         const int srcIndex = params.value("srcIndex", -1);
         const int dstIndex = params.value("dstIndex", -1);
         GraphNode* srcGn = FindNodeByIndex(srcIndex);
         GraphNode* dstGn = FindNodeByIndex(dstIndex);
         if (srcGn == nullptr || dstGn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         // Turbo 0.45: slot / output by number or by label ("B", "audio").
         const int srcOutput = params.contains("srcOutput") ? RpcResolveOutput(*srcGn, params["srcOutput"]) : 0;
         const int dstSlot = params.contains("dstSlot") ? RpcResolveSlot(*dstGn, params["dstSlot"]) : 0;
         if (srcOutput < 0 || dstSlot < 0 || dstSlot >= InputCountFor(*dstGn))
         {
            outError = "unknown output or input slot (explain / describe list them)";
            return false;
         }
         if (!ConnectNodes(srcIndex, srcOutput, dstIndex, dstSlot, outError))
            return false;
         outResult = json::object();
         return true;
      }
      else if (method == "disconnect")
      {
         const int linkId = params.value("linkId", -1);
         if (FindLink(linkId) == nullptr)
         {
            outError = "unknown link id";
            return false;
         }
         PushUndoCheckpoint();
         DisconnectLinkById(linkId);
         outResult = json::object();
         return true;
      }
      else if (method == "get_graph")
      {
         json out = PatchJson::ToJson(BuildPatchData());
         // Patch::Data's cable records carry endpoints but no link id, since
         // ids are a live editor concept and never hit the patch file. The
         // disconnect() method needs one, so publish gLinks alongside the
         // patch, with each pin decoded back to a node index + slot/output.
         json links = json::array();
         for (const LinkInfo& link : gLinks)
         {
            json entry = { {"id", link.id} };
            entry["srcIndex"] = GraphNode::NodeIndexFromPin(link.srcPin);
            entry["srcOutput"] = GraphNode::IsOutputPin(link.srcPin)
                                    ? GraphNode::OutputIndexFromPin(link.srcPin)
                                    : 0;
            entry["dstIndex"] = GraphNode::NodeIndexFromPin(link.dstPin);
            const int dstOff = GraphNode::OffsetFromPin(link.dstPin);
            if (GraphNode::IsParamPin(link.dstPin))
               entry["dstParamPin"] = dstOff - GraphNode::kParamBase;
            else if (GraphNode::IsColorPin(link.dstPin))
               entry["dstColorPin"] = GraphNode::ColorIndexFromPin(link.dstPin);
            else
               entry["dstSlot"] = dstOff - 1;
            links.push_back(entry);
         }
         out["links"] = links;
         outResult = out;
         return true;
      }
      else if (method == "get_params")
      {
         const int index = params.value("index", -1);
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         std::vector<std::pair<std::string, std::string>> raw;
         Patch::SaveParams(gn->node.get(), raw);
         json out = json::object();
         for (const auto& kv : raw)
            out[kv.first] = kv.second;
         outResult = out;
         return true;
      }
      else if (method == "set_param")
      {
         const int index = params.value("index", -1);
         const std::string name = params.value("name", std::string());
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         // Turbo 0.45: shared with create_node's "settings" (RpcSetSetting).
         if (RpcFindSettingKey(gn->node.get(), name).empty())
         {
            outError = "unknown setting '" + name + "' on " + gn->typeName + " (describe or explain lists them)";
            return false;
         }
         PushUndoCheckpoint();
         if (!RpcSetSetting(*gn, name, params.contains("value") ? params["value"] : json(""), outError))
            return false;
         outResult = json::object();
         return true;
      }
      else if (method == "set_node_position" || method == "get_node_position")
      {
         const int index = params.value("index", -1);
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         ed::EditorContext* prevEditor = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         if (method == "set_node_position")
         {
            const float x = params.value("x", 0.0f);
            const float y = params.value("y", 0.0f);
            ed::SetNodePosition(gn->NodeId(), ImVec2(x, y));
            // Turbo 0.45: a node that has not drawn yet would take its spawn
            // position on its first draw, and the auto layout would move it.
            gn->spawnX = x;
            gn->spawnY = y;
            gn->liveX = x;
            gn->liveY = y;
            gAutoLayoutPending.erase(std::remove(gAutoLayoutPending.begin(), gAutoLayoutPending.end(), gn->index),
                                     gAutoLayoutPending.end());
            outResult = json::object();
         }
         else
         {
            const ImVec2 p = ed::GetNodePosition(gn->NodeId());
            outResult = { {"x", p.x}, {"y", p.y} };
         }
         ed::SetCurrentEditor(prevEditor);
         return true;
      }
      else if (method == "fit_view")
      {
         gRequestFitView = true;
         outResult = json::object();
         return true;
      }
      else if (method == "save_patch")
      {
         const std::string path = params.value("path", std::string());
         if (!SavePatchTo(path))
         {
            outError = gPatchStatus;
            return false;
         }
         outResult = json::object();
         return true;
      }
      else if (method == "load_patch")
      {
         const std::string path = params.value("path", std::string());
         if (!LoadPatchFrom(path))
         {
            outError = gPatchStatus;
            return false;
         }
         outResult = json::object();
         return true;
      }
      else if (method == "new_patch")
      {
         NewPatch();
         outResult = json::object();
         return true;
      }
      else if (method == "undo")
      {
         Undo();
         outResult = json::object();
         return true;
      }
      else if (method == "redo")
      {
         Redo();
         outResult = json::object();
         return true;
      }

      outError = "unknown method '" + method + "'";
      return false;
   }

   // Turbo 0.45: the RPC entry point - the extra tools first, then the base set.
   bool HandleRpcCommand(const std::string& method, const nlohmann::json& params,
                         nlohmann::json& outResult, std::string& outError)
   {
      // A wrong JSON type in the arguments (a string index, say) throws in
      // nlohmann's value()/get(): an error reply, never a crash.
      try
      {
         bool handled = false;
         const bool ok = HandleRpcCommandExtra(method, params, outResult, outError, handled);
         if (handled)
            return ok;
         const bool okTurbo = HandleRpcCommandTurbo(method, params, outResult, outError, handled);
         if (handled)
            return okTurbo;
         return HandleRpcCommandBase(method, params, outResult, outError);
      }
      catch (const std::exception& e)
      {
         outError = std::string("bad arguments: ") + e.what();
         return false;
      }
   }

   // Drawn inside the ed::Suspend() block alongside the popups, so plain
   // ImGui widgets work normally without the editor intercepting the mouse.

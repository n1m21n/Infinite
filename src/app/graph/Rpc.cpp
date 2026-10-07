// RPC command handler (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Dispatches one RemoteControl JSON-RPC request. Runs on the main thread,
   // once per frame per pending command (see RemoteControl::DrainPending's
   // call site, right after glfwPollEvents()) - so every case here is free to
   // touch gNodes/gEditor/etc exactly as the normal UI code does. Returns
   // false with outError set on any failure; RemoteControl wraps that into a
   // JSON-RPC error reply.
   bool HandleRpcCommand(const std::string& method, const nlohmann::json& params,
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
         const std::string typeName = params.value("typeName", std::string());
         const std::string category = params.value("category", std::string());
         const float x = params.value("x", 0.0f);
         const float y = params.value("y", 0.0f);
         GraphNode* gn = SpawnNode(typeName, category, x, y);
         if (gn == nullptr)
         {
            outError = "unknown node type '" + typeName + "'";
            return false;
         }
         outResult = { {"index", gn->index} };
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
         const int srcOutput = params.value("srcOutput", 0);
         const int dstIndex = params.value("dstIndex", -1);
         const int dstSlot = params.value("dstSlot", -1);
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
      else if (method == "explain")
      {
         JoinLiveTier1();
         outResult = json::object();
         outResult["text"] = ExplainLive(false, params.value("all", false));
         outResult["graph"] = json::parse(ExplainLive(true, true));
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
         // Params are keyed "<type letter> <name>" (Patch.h's f/i/b/c/s
         // convention) - find the existing key for `name` so we replay it
         // with the right type letter rather than guessing one.
         std::vector<std::pair<std::string, std::string>> raw;
         Patch::SaveParams(gn->node.get(), raw);
         std::string matchedKey;
         for (const auto& kv : raw)
         {
            const size_t sp = kv.first.find(' ');
            if (sp != std::string::npos && kv.first.substr(sp + 1) == name)
            {
               matchedKey = kv.first;
               break;
            }
         }
         if (matchedKey.empty())
         {
            outError = "unknown param '" + name + "' on this node";
            return false;
         }
         std::string valueStr;
         if (params.contains("value"))
         {
            const json& v = params["value"];
            if (v.is_string())
               valueStr = v.get<std::string>();
            else
               valueStr = v.dump();
         }
         PushUndoCheckpoint();
         Patch::LoadParams(gn->node.get(), { { matchedKey, valueStr } });
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
      else if (method == "fit_view_node")
      {
         const int index = params.value("index", -1);
         if (FindNodeByIndex(index) == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         gRequestFitViewNodeIndex = index;
         outResult = json::object();
         return true;
      }
      else if (method == "expand_node")
      {
         const int index = params.value("index", -1);
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         // Documentation screenshots: both "params start collapsed" flags
         // (GraphNode.h) default off so a fresh node leads with its preview -
         // force them open so the full param body actually renders.
         gn->showParams = true;
         gn->showAdvancedParams = true;
         outResult = json::object();
         return true;
      }
      else if (method == "auto_wire_inputs")
      {
         // Feeds every otherwise-unconnected input slot on a node with a
         // plausible source, since most filter/effect/operator nodes render
         // blank with nothing wired in. Spawns one shared feeder per slot
         // *kind* needed (image/audio/note/geometry/modulator), tucked far
         // off in canvas space so it never intrudes on the target's own
         // on-screen rect that screenshot_node crops to - fit_view_node
         // frames only the target node regardless of where else on the
         // canvas a feeder sits.
         //
         // ConnectNodes() below re-validates every guess through the same
         // IsInputSlotCompatible() the UI itself uses, so a wrong guess here
         // just fails to wire that slot rather than mis-wiring it - this
         // deliberately doesn't cover Render3DNode's camera/light/environment
         // slots or SetColorNode's palette slot, both multi-kind special
         // cases not worth the complexity here.
         const int index = params.value("index", -1);
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }

         std::map<std::string, int> feederIndexByType;
         // SpawnNode() push_backs onto gNodes, which can reallocate and
         // invalidate every GraphNode* into it - including whatever the
         // caller was holding. ensureFeeder always re-resolves through
         // FindNodeByIndex(), and callers below re-resolve `target` after
         // calling it, precisely so no pointer is held across a spawn.
         auto ensureFeeder = [&](const std::string& typeName,
                                 const std::string& category) -> GraphNode*
         {
            auto it = feederIndexByType.find(typeName);
            if (it != feederIndexByType.end())
               return FindNodeByIndex(it->second);
            GraphNode* feeder = SpawnNode(typeName, category, -8000.0f,
                                          -8000.0f + (float)feederIndexByType.size() * 400.0f);
            if (feeder != nullptr)
               feederIndexByType[typeName] = feeder->index;
            return feeder;
         };

         json wired = json::array();
         const int inputs = InputCountFor(*gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            GraphNode* target = FindNodeByIndex(index); // re-resolve: a prior iteration may have spawned a feeder
            if (target == nullptr)
               break;

            GraphNode* feeder = nullptr;
            if (AudioCable* cable = target->node->AudioInputSlot(slot))
            {
               if (cable->IsConnected())
                  continue;
               feeder = ensureFeeder("Wavetable", "Synths");
            }
            else if (NoteCable* cable = target->node->NoteInputSlot(slot))
            {
               if (cable->IsConnected())
                  continue;
               feeder = ensureFeeder("MIDI Notes", "Notes");
            }
            else if (IGeometrySource** field = target->node->GeometryInputSlot(slot))
            {
               if (*field != nullptr)
                  continue;
               feeder = ensureFeeder("Sphere", "3D");
            }
            else if (IModulator** field = target->node->ModulatorInputSlot(slot))
            {
               if (*field != nullptr)
                  continue;
               feeder = ensureFeeder("Envelope", "Modulators");
            }
            else if (ImageCable* cable = CableFor(*gn, slot))
            {
               if (cable->IsConnected())
                  continue;
               feeder = ensureFeeder("Shape", "Source");
            }
            if (feeder == nullptr)
               continue;

            std::string ignoredErr;
            if (ConnectNodes(feeder->index, 0, gn->index, slot, ignoredErr))
               wired.push_back(slot);
         }

         json feederIndices = json::array();
         for (const auto& kv : feederIndexByType)
            feederIndices.push_back(kv.second);
         outResult = { {"wiredSlots", wired}, {"feederIndices", feederIndices} };
         return true;
      }
      else if (method == "load_patch_text")
      {
         // Patch source in the request, no temp file. Applied as one undo step.
         Patch::Data data;
         std::string readError;
         if (!Patch::ReadText(params.value("text", std::string()), data, readError))
         {
            outError = readError;
            return false;
         }
         if (!LoadPatchDataImpl(data, std::string(), true))
         {
            outError = gPatchStatus;
            return false;
         }
         outResult = { {"nodes", (int)gNodes.size()}, {"status", gPatchStatus} };
         return true;
      }
      else if (method == "validate_patch_text")
      {
         // The strict check the CLI runs before a load, against patch source
         // held in the request. Nothing on the canvas changes.
         Patch::Data data;
         std::string readError;
         std::vector<Headless::Issue> errors, warnings;
         if (!Patch::ReadText(params.value("text", std::string()), data, readError))
            errors.push_back({ "E_LOAD", readError, 0, -1 });
         else
         {
            const PatchSchema::Env env = MakeSchemaEnv(false);
            PatchSchema::Resolve(data, env, errors);
            if (errors.empty())
               PatchSchema::ResolveKeys(data, env, errors);
            if (errors.empty())
               PatchSchema::Validate(data, env, errors, warnings);
            if (params.value("strict", true))
               Headless::PromoteWarnings(warnings, errors);
         }
         auto issuesJson = [](const std::vector<Headless::Issue>& v)
         {
            json a = json::array();
            for (const Headless::Issue& is : v)
               a.push_back({ {"code", is.code}, {"message", is.message}, {"line", is.line}, {"node", is.node}, {"hint", is.hint} });
            return a;
         };
         outResult = { {"ok", errors.empty()}, {"errors", issuesJson(errors)}, {"warnings", issuesJson(warnings)},
                       {"nodes", (int)data.nodes.size()} };
         return true;
      }
      else if (method == "describe")
      {
         // The static schema of one node type (or the list of types when none is
         // given). Control ranges need a drawn node, so they are the CLI's job:
         // `Infinite --describe <type> --json`.
         const std::string type = params.value("type", std::string());
         if (type.empty())
         {
            json cats = json::object();
            for (const std::string& cat : NodeFactory::Instance().GetCategories())
               cats[cat] = NodeFactory::Instance().GetNodesInCategory(cat);
            outResult = { {"types", cats} };
            return true;
         }
         const PatchSchema::TypeSchema* ts = SchemaFor(type);
         if (ts == nullptr)
         {
            outError = "unknown node type '" + type + "'";
            return false;
         }
         json pj = json::array(), in = json::array(), out = json::array();
         for (const auto& pi : ts->params)
            pj.push_back({ {"key", pi.key}, {"kind", std::string(1, pi.kind)}, {"default", pi.def} });
         for (const auto& si : ts->inputs)
            in.push_back({ {"slot", si.slot}, {"kind", si.kind}, {"label", si.label} });
         for (const auto& oi : ts->outputs)
            out.push_back({ {"label", oi.label}, {"kind", oi.kind}, {"modulator", oi.modulator} });
         outResult = { {"type", ts->name}, {"category", ts->category}, {"params", pj}, {"inputs", in}, {"outputs", out},
                       {"canBypass", ts->canBypass}, {"hardwareDriven", ts->hardwareDriven} };
         return true;
      }
      else if (method == "batch")
      {
         // Array of {method, params} run in one frame, all or nothing, as ONE
         // undo step. On the first failure everything already done is undone.
         // Node indices are renumbered by that undo (ApplyPatchData remaps), so
         // a caller that rolled back should re-read get_graph.
         if (!params.contains("calls") || !params["calls"].is_array())
         {
            outError = "batch needs a 'calls' array of {method, params}";
            return false;
         }
         PushUndoCheckpoint(); // the single checkpoint for the whole batch
         const size_t undoDepth = gUndoStack.size();
         const bool wasSuppressed = gSuppressUndoCheckpoints;
         gSuppressUndoCheckpoints = true; // calls inside must not add their own
         json results = json::array();
         std::string failMsg;
         int failAt = -1;
         for (size_t i = 0; i < params["calls"].size() && failAt < 0; i++)
         {
            const json& call = params["calls"][i];
            const std::string m = call.value("method", std::string());
            if (m.empty() || m == "batch" || m == "undo" || m == "redo" || m == "new_patch" || m == "load_patch")
            {
               failMsg = "method '" + m + "' is not allowed inside a batch";
               failAt = (int)i;
               break;
            }
            json r;
            std::string e;
            if (!HandleRpcCommand(m, call.contains("params") ? call["params"] : json::object(), r, e))
            {
               failMsg = m + ": " + e;
               failAt = (int)i;
               break;
            }
            results.push_back(r);
         }
         gSuppressUndoCheckpoints = wasSuppressed;
         if (failAt >= 0)
         {
            if (gUndoStack.size() == undoDepth)
            {
               Undo();
               gRedoStack.clear(); // the rolled-back attempt is not something to redo
            }
            outError = "batch call " + std::to_string(failAt) + " failed, nothing applied - " + failMsg;
            return false;
         }
         outResult = { {"results", results} };
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
      else if (method == "screenshot_node")
      {
         // Documentation tooling: crop a PNG to exactly one node's on-screen
         // rect. Reads GL_FRONT (the last completed, already-swapped frame)
         // rather than GL_BACK, since this handler runs at the top of the
         // next frame (see RemoteControl::DrainPending's call site) - the
         // back buffer at that point holds stale/undefined content, while
         // front always holds what's actually on screen right now. Callers
         // should fit_view and let a couple of real frames pass first so the
         // node has settled into view and finished cooking.
         const int index = params.value("index", -1);
         const std::string path = params.value("path", std::string());
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         if (path.empty())
         {
            outError = "missing path";
            return false;
         }

         ed::EditorContext* prevEditor = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         const ImVec2 canvasPos = ed::GetNodePosition(gn->NodeId());
         const ImVec2 canvasSize = ed::GetNodeSize(gn->NodeId());
         const ImVec2 screenMin = ed::CanvasToScreen(canvasPos);
         const ImVec2 screenMax =
            ed::CanvasToScreen(ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));
         ed::SetCurrentEditor(prevEditor);

         GLFWwindow* mainWin = glfwGetCurrentContext();
         int fbW = 0, fbH = 0;
         glfwGetFramebufferSize(mainWin, &fbW, &fbH);
         // ImGui points -> framebuffer pixels. fb/window alone misses the point scale that
         // Windows/X11 apply on hi-DPI monitors (core/UiScale.h).
         const float scaleX = ImGui::GetIO().DisplayFramebufferScale.x;
         const float scaleY = ImGui::GetIO().DisplayFramebufferScale.y;

         const int pad = params.value("padding", 12);
         const int x0 = std::max(0, (int)std::floor(screenMin.x * scaleX) - pad);
         const int y0 = std::max(0, (int)std::floor(screenMin.y * scaleY) - pad);
         const int x1 = std::min(fbW, (int)std::ceil(screenMax.x * scaleX) + pad);
         const int y1 = std::min(fbH, (int)std::ceil(screenMax.y * scaleY) + pad);
         const int cropW = x1 - x0;
         const int cropH = y1 - y0;
         if (cropW <= 0 || cropH <= 0)
         {
            outError = "node has no visible on-screen rect to capture";
            return false;
         }

         std::vector<unsigned char> full((size_t)fbW * fbH * 4);
         glReadBuffer(GL_FRONT);
         glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, full.data());

         // full[] is OpenGL's bottom-up row order; x0/y0/x1/y1 are top-down
         // screen-space. Flip per row while cropping instead of flipping the
         // whole framebuffer first.
         std::vector<unsigned char> crop((size_t)cropW * cropH * 4);
         for (int row = 0; row < cropH; ++row)
         {
            const int srcY = fbH - 1 - (y0 + row);
            std::memcpy(&crop[(size_t)row * cropW * 4], &full[((size_t)srcY * fbW + x0) * 4],
                        (size_t)cropW * 4);
         }
         stbi_write_png(path.c_str(), cropW, cropH, 4, crop.data(), cropW * 4);
         outResult = { {"width", cropW}, {"height", cropH} };
         return true;
      }

      else if (method == "render_frame")
      {
         // The live counterpart of `--frame`: writes the Output's current image (what the last
         // cooked frame produced, at the Output's own size) through the same ExportImage and
         // reports the same per-frame numbers. It does not seek the transport or step time, so
         // a running patch is left exactly as it was; use --frame for a frame at a given time.
         const std::string path = params.value("path", std::string());
         if (path.empty())
         {
            outError = "missing path";
            return false;
         }
         std::vector<int> outs;
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Output")
               outs.push_back(gn.index);
         if (outs.empty())
         {
            outError = "the patch has no Output node";
            return false;
         }
         int outIndex = params.value("output", -1);
         if (outIndex < 0 && outs.size() > 1)
         {
            outError = "the patch has " + std::to_string(outs.size()) + " Output nodes; pass output = node index";
            return false;
         }
         if (outIndex < 0)
            outIndex = outs.front();
         GraphNode* og = FindNodeByIndex(outIndex);
         if (og == nullptr || og->typeName != "Output")
         {
            outError = "output is not an Output node index";
            return false;
         }
         OutputNode* out = static_cast<OutputNode*>(og->node.get());
         const int w = out->GetOutputWidth(), h = out->GetOutputHeight();
         if (w <= 0 || h <= 0)
         {
            outError = "the Output produced no image (is its input connected?)";
            return false;
         }
         std::error_code ec;
         const std::filesystem::path parent = std::filesystem::path(path).parent_path();
         if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
         std::vector<unsigned char> pixels;
         ExportImage(out, path, 90, &pixels);
         if (!std::filesystem::exists(path, ec))
         {
            outError = "could not write " + path;
            return false;
         }
         const ColorStats::FrameSummary fs = ColorStats::SummarizeRgba8(pixels.data(), w, h);
         outResult = { { "file", path }, { "width", w }, { "height", h }, { "output", outIndex },
                       { "frame_stats", json::parse("{" + ColorStats::FrameSummaryJsonFields(fs) + "}") } };
         if (fs.blackPercent >= 100.0)
            outResult["warning"] = "W_BLACK_FRAME: the frame is entirely black";
         return true;
      }

      outError = "unknown method '" + method + "'";
      return false;
   }
}

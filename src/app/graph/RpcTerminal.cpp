// The RPC verbs the `infinite` command adds to the existing set (Rpc.cpp): what the human is
// pointing at, the transport, modulation by control name, bypass, and the code errors of the
// open patch. Same rules as Rpc.cpp: main thread, between frames, one undo step per edit.
#include <algorithm>
#include <climits>
#include <cstdlib>
#include "app/AppShared.h"
#include "core/GestureRecorder.h"
#include "platform/common/ProcessTree.h"

namespace app
{
   bool HandleAppRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError);
   bool HandleTimelineRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError);

   namespace
   {
      using json = nlohmann::json;

      // The modulatable index of a control, by the key `describe` and the patch format use.
      int ParamIndexOfKey(const GraphNode& gn, const std::string& key, std::string& outError)
      {
         // Controls a script declares are saved as p.<name>, so both spellings work.
         // Also accept a typed key ("f rotation") when VisitParams uses the bare name ("rotation").
         int param = ParamIndexOfKeyOnNode(gn, key);
         if (param < 0)
            param = ParamIndexOfKeyOnNode(gn, "p." + key);
         if (param < 0 && key.size() > 2 && key[1] == ' ')  // ui-audit-ok: key parsing, not a screen quantity
            param = ParamIndexOfKeyOnNode(gn, key.substr(2));
         if (param >= 0)
         {
            gParamProbeNode = -1;
            return param;
         }
         // A closed node has no controls until something asks. The client retries on this text.
         const std::vector<ParamRef>& drawn = Modulation::Instance().FrameParams();
         const bool registered = std::any_of(drawn.begin(), drawn.end(), [&](const ParamRef& r) { return r.nodeIndex == gn.index; });
         if (!registered)
         {
            gParamProbeNode = gn.index;
            outError = gn.typeName + " has no controls yet; retry on the next frame";
            return -1;
         }
         gParamProbeNode = -1;
         outError = "'" + key + "' is not a modulatable control of " + gn.typeName + "; `infinite params " + std::to_string(gn.index) + "` lists its keys";
         return param;
      }

      // Bounded and total: accepts integer, numeric string, or nested {index|node|id|dst} up to depth 8.
      int NodeIndexOfField(const json& field, int depth = 0)
      {
         if (depth > 8)
            return -1;
         if (field.is_number_unsigned())
         {
            const unsigned long long wide = field.get<unsigned long long>();
            return wide <= (unsigned long long)INT_MAX ? (int)wide : -1;
         }
         if (field.is_number_integer())
         {
            const long long wide = field.get<long long>();
            return (wide >= INT_MIN && wide <= INT_MAX) ? (int)wide : -1;
         }
         if (field.is_number())
         {
            const double wide = field.get<double>();
            return (wide >= (double)INT_MIN && wide <= (double)INT_MAX) ? (int)wide : -1;
         }
         if (field.is_string())
         {
            const std::string text = field.get<std::string>();
            if (text.empty())
               return -1;
            char* end = nullptr;
            const long parsed = std::strtol(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
               return -1;
            return (int)parsed;
         }
         if (field.is_object())
            for (const char* name : { "index", "node", "id", "dst" })
               if (field.contains(name) && !field[name].is_null())
                  return NodeIndexOfField(field[name], depth + 1);
         return -1;
      }

      std::string ControlKeyFrom(const json& params, const json& node)
      {
         for (const json* holder : { &params, &node })
            if (holder->is_object())
               for (const char* name : { "key", "param", "name", "control" })
                  if (holder->contains(name) && (*holder)[name].is_string() && !(*holder)[name].get<std::string>().empty())
                     return (*holder)[name].get<std::string>();
         return std::string();
      }

      json CodeIssue(const GraphNode& gn, const std::string& message)
      {
         const bool sketch = gn.typeName == "Sketch" || gn.typeName == "Sketch 3D";
         const bool glsl = gn.typeName == "Shadertoy";
         const bool formula = gn.typeName == "Formula" || gn.typeName == "Equation Synth";
         return { { "code", glsl ? "E_GLSL_COMPILE" : sketch ? "E_SKETCH_COMPILE" : formula ? "E_FORMULA_COMPILE" : "E_FIELD_COMPILE" },
                  { "node", gn.index }, { "type", gn.typeName }, { "title", NodeTitle(gn) }, { "message", message } };
      }
   }

   bool HandleTerminalRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError)
   {
      if (method == "selection")
      {
         // Requests run between frames, when no editor is current.
         ed::EditorContext* previous = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);
         const int count = ed::GetSelectedObjectCount();
         std::vector<ed::NodeId> ids((size_t)std::max(1, count));
         const int got = ed::GetSelectedNodes(ids.data(), count);
         ed::SetCurrentEditor(previous);
         json nodes = json::array();
         for (int i = 0; i < got; i++)
            if (GraphNode* gn = FindNodeByIndex((int)ids[(size_t)i].Get() / GraphNode::kStride))
               nodes.push_back({ { "index", gn->index }, { "type", gn->typeName }, { "title", NodeTitle(*gn) } });
         outResult = { { "nodes", nodes } };
         return true;
      }
      if (method == "list_nodes")
      {
         json nodes = json::array();
         for (const GraphNode& gn : gNodes)
            nodes.push_back({ { "index", gn.index }, { "type", gn.typeName }, { "title", NodeTitle(gn) } });
         outResult = { { "nodes", nodes } };
         return true;
      }
      if (method == "status")
      {
         Transport& tr = Transport::Instance();
         outResult = { { "window", ProcessTree::Self() }, { "patch", gPatchPath }, { "dirty", gPatchDirty }, { "nodes", gNodes.size() }, { "cables", gLinks.size() },
                       { "playing", tr.IsPlaying() }, { "seconds", tr.Seconds() }, { "beats", tr.Beats() }, { "bpm", tr.Tempo() },
                       { "showLock", gShowLock }, { "fps", ImGui::GetIO().Framerate } };   // ImGui's 60-frame average
         return true;
      }
      if (method == "transport")
      {
         Transport& tr = Transport::Instance();
         const std::string action = params.value("action", std::string());
         if (action == "play")
            tr.SetPlaying(true);
         else if (action == "stop")
            tr.SetPlaying(false);
         else if (action == "rewind")
            tr.Rewind();
         else if (action == "seek")
            tr.Seek(params.value("seconds", 0.0));
         else if (action == "tempo")
         {
            const float bpm = params.value("bpm", 0.0f);
            if (bpm < 1.0f || bpm > 999.0f)
            {
               outError = "tempo must be 1 to 999 BPM";
               return true;
            }
            tr.SetTempo(bpm);
         }
         else if (action == "loop")
         {
            // Goes through ArrangeSetLoop, never tr.SetLoop directly, because the arrange model
            // republishes its loop to the transport every frame (PublishArrangeLoop).
            if (params.contains("enabled") && !params["enabled"].is_boolean())
            {
               outError = "enabled must be a boolean";
               return true;
            }
            if ((params.contains("start") && !params["start"].is_number()) ||
                (params.contains("end") && !params["end"].is_number()))
            {
               outError = "start and end must be numbers (beats)";
               return true;
            }
            const Arrange::LoopRange& cur = gArrange.settings.loop;
            const bool enabled = params.contains("enabled") ? params["enabled"].get<bool>() : true;
            if (enabled && !params.contains("start") && !params.contains("end") && cur.end <= cur.start)
            {
               outError = "the loop has no range yet: give start and end (beats), e.g. {\"action\":\"loop\",\"enabled\":true,\"start\":0,\"end\":16}";
               return true;
            }
            const Arrange::Tick start = params.contains("start") ? Arrange::BeatsToTicks(params["start"].get<double>()) : cur.start;
            const Arrange::Tick end = params.contains("end") ? Arrange::BeatsToTicks(params["end"].get<double>()) : cur.end;
            if (enabled && end <= start)
            {
               outError = "end must be after start: start " + std::to_string(start) + " tick, end " + std::to_string(end) +
                          " tick. Beats, e.g. start 0 end 16 is four bars in 4/4";
               return true;
            }
            ArrangeSetLoop(enabled, start, end);
         }
         else
         {
            outError = "unknown transport action '" + action + "'";
            return true;
         }
         outResult = { { "playing", tr.IsPlaying() }, { "seconds", tr.Seconds() }, { "bpm", tr.Tempo() },
                       { "loopEnabled", tr.LoopEnabled() }, { "loopStart", tr.LoopStartBeats() },
                       { "loopEnd", tr.LoopEndBeats() } };
         return true;
      }
      if (method == "set_bypass")
      {
         GraphNode* gn = FindNodeByIndex(params.value("index", -1));
         if (gn == nullptr || !gn->node)
         {
            outError = "unknown node index";
            return true;
         }
         if (!CanBypass(*gn))
         {
            outError = gn->typeName + " cannot be bypassed";
            return true;
         }
         const bool on = params.value("bypassed", true);
         if (gn->node->bypassed != on)
         {
            PushUndoCheckpoint();
            gn->node->bypassed = on;
            RebuildAudioTopology();
            gPatchDirty = true;
         }
         outResult = { { "index", gn->index }, { "bypassed", on } };
         return true;
      }
      if (method == "set_expr" || method == "bind_mod" || method == "unbind_mod")
      {
         if (!params.is_object())
         {
            outError = "params must be a JSON object, e.g. {\"dst\":5,\"key\":\"f rotation\",\"src\":4}";
            return true;
         }
         json dstField;
         for (const char* name : { "dst", "index", "node", "target" })
            if (params.contains(name) && !params[name].is_null())
            {
               dstField = params[name];
               break;
            }
         const int dstIndex = NodeIndexOfField(dstField);
         const std::string key = ControlKeyFrom(params, dstField);
         GraphNode* gn = FindNodeByIndex(dstIndex);
         if (gn == nullptr)
         {
            outError = "unknown node index " + (dstIndex >= 0 ? std::to_string(dstIndex) : std::string("<missing>")) +
                       ": pass dst (or index) as the integer node index from get_graph";
            return true;
         }
         if (key.empty())
         {
            outError = "missing control key: pass key with its type letter, e.g. \"f rotation\"; `infinite params " + std::to_string(gn->index) + "` lists them";
            return true;
         }
         // A bare name ("uRotation") works as well as the typed key ("f uRotation").
         int param = ParamIndexOfKey(*gn, key, outError);
         for (const char* prefix : { "f ", "i ", "b ", "c ", "s " })
            if (param < 0 && key.find(' ') == std::string::npos)
            {
               std::string e2;
               const int p2 = ParamIndexOfKey(*gn, std::string(prefix) + key, e2);
               if (p2 >= 0)
                  param = p2;
            }
         if (param >= 0)
            outError.clear();
         if (param < 0)
            return true;
         if (method == "set_expr")
         {
            // The same as typing `=formula` into the field: the `=` is how a person says it, not part of it.
            if (params.contains("text") && !params["text"].is_null() && !params["text"].is_string())
            {
               outError = "text must be a string, e.g. \"sin(t)*0.3\"; an empty string clears the expression";
               return true;
            }
            std::string text = params.contains("text") && params["text"].is_string() ? params["text"].get<std::string>() : std::string();
            auto trim = [](std::string& t)
            {
               const size_t a = t.find_first_not_of(" \t\n");
               t = a == std::string::npos ? std::string() : t.substr(a, t.find_last_not_of(" \t\n") - a + 1);
            };
            trim(text);
            if (!text.empty() && text[0] == '=')
               text.erase(0, 1);
            trim(text);
            PushUndoCheckpoint();
            if (text.empty())
               Modulation::Instance().ClearExpression(gn->index, param);
            else
            {
               Modulation::Instance().SetExpression(gn->index, param, text);
               GestureRecorder::Instance().CancelArm(gn->index, param);
               GestureRecorder::Instance().StopPlayback(gn->index, param);
            }
         }
         else if (method == "unbind_mod")
         {
            const bool wasBound = Modulation::Instance().IsModulated(gn->index, param);
            if (wasBound)
               PushUndoCheckpoint();
            Modulation::Instance().Unbind(gn->index, param);
            gPatchDirty = gPatchDirty || wasBound;
            outResult = { { "index", gn->index }, { "param", param }, { "key", key }, { "wasModulated", wasBound } };
            return true;
         }
         else
         {
            json srcField;
            for (const char* name : { "src", "source", "from", "modulator" })
               if (params.contains(name) && !params[name].is_null())
               {
                  srcField = params[name];
                  break;
               }
            const int srcIndex = NodeIndexOfField(srcField);
            GraphNode* src = FindNodeByIndex(srcIndex);
            int output = 0;
            for (const char* name : { "output", "out" })
               if (params.contains(name) && !params[name].is_null())
                  output = NodeIndexOfField(params[name]);
            if (srcField.is_object())
               for (const char* name : { "output", "out" })
                  if (srcField.contains(name) && !srcField[name].is_null())
                     output = NodeIndexOfField(srcField[name]);
            const bool wantsRange = params.contains("lo") || params.contains("hi");
            if (wantsRange && (!params.contains("lo") || !params.contains("hi") || !params["lo"].is_number() || !params["hi"].is_number()))
            {
               outError = "pass lo and hi together as numbers, in destination units, e.g. \"lo\":0,\"hi\":1";
               return true;
            }
            if (src == nullptr)
            {
               outError = "unknown modulator node " + (srcIndex >= 0 ? std::to_string(srcIndex) : std::string("<missing>")) +
                          ": pass src as the integer node index of a modulator from get_graph";
               return true;
            }
            if (output < 0 || output >= GraphNode::kStride - GraphNode::kOutputBase)
            {
               outError = "unknown output " + std::to_string(output) + " of " + src->typeName;
               return true;
            }
            PushUndoCheckpoint();
            Modulation::Instance().Bind(gn->index, param, src->index, output);
            if (wantsRange)
               Modulation::Instance().SetRange(gn->index, param, params["lo"].get<float>(), params["hi"].get<float>());
            gPatchDirty = true;
            outResult = { { "index", gn->index }, { "param", param }, { "key", key }, { "src", src->index }, { "output", output } };
            return true;
         }
         gPatchDirty = true;
         outResult = { { "index", gn->index }, { "param", param }, { "key", key } };
         return true;
      }
      if (method == "reset_param")
      {
         if (!params.is_object())
         {
            outError = "params must be a JSON object, e.g. {\"index\":5,\"key\":\"rotation\"}";
            return true;
         }
         json dstField;
         for (const char* name : { "dst", "index", "node", "target" })
            if (params.contains(name) && !params[name].is_null())
            {
               dstField = params[name];
               break;
            }
         const int dstIndex = NodeIndexOfField(dstField);
         const std::string key = ControlKeyFrom(params, dstField);
         GraphNode* gn = FindNodeByIndex(dstIndex);
         if (gn == nullptr)
         {
            outError = "unknown node index " + (dstIndex >= 0 ? std::to_string(dstIndex) : std::string("<missing>")) +
                       ": pass index as the integer node index from get_graph";
            return true;
         }
         if (key.empty())
         {
            outError = "missing control key: pass key, e.g. \"rotation\"; `infinite params " + std::to_string(gn->index) + "` lists them";
            return true;
         }
         if (!ResetParamKeyToDefault(*gn, key, /*pushUndo=*/true))
         {
            outError = "'" + key + "' has no resettable default on " + gn->typeName;
            return true;
         }
         outResult = { { "index", gn->index }, { "key", key }, { "reset", true } };
         return true;
      }
      if (method == "code_errors")
      {
         json errors = json::array();
         for (GraphNode& gn : gNodes)
            if (gn.node)
               if (const std::string err = gn.node->CodeError(); !err.empty())
                  errors.push_back(CodeIssue(gn, err));
         outResult = { { "errors", errors } };
         return true;
      }
      return HandleTerminalSessionRpc(method, params, outResult, outError) ||
             HandleAppRpc(method, params, outResult, outError) || HandleTimelineRpc(method, params, outResult, outError);
   }
}

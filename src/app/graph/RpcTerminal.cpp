// The RPC verbs the `infinite` command adds to the existing set (Rpc.cpp): what the human is
// pointing at, the transport, modulation by control name, bypass, and the code errors of the
// open patch. Same rules as Rpc.cpp: main thread, between frames, one undo step per edit.
#include <algorithm>
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
         int param = ParamIndexOfKeyOnNode(gn, key);
         if (param < 0)
            param = ParamIndexOfKeyOnNode(gn, "p." + key);
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
                       { "showLock", gShowLock } };
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
         else
         {
            outError = "unknown transport action '" + action + "'";
            return true;
         }
         outResult = { { "playing", tr.IsPlaying() }, { "seconds", tr.Seconds() }, { "bpm", tr.Tempo() } };
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
         // fork: also accept the shapes an agent naturally writes: dst / src as {index, output, param|key}.
         const json& dstField = method == "bind_mod" ? params.value("dst", json()) : params.value("index", json());
         int dstIndex = dstField.is_number_integer() ? dstField.get<int>() : -1;
         std::string key;
         if (const json k = params.value("key", json()); k.is_string())
            key = k.get<std::string>();
         if (dstField.is_object())
         {
            dstIndex = dstField.value("index", -1);
            for (const char* name : { "key", "param", "name" })
               if (dstField.contains(name) && dstField[name].is_string() && key.empty())
                  key = dstField[name].get<std::string>();
         }
         GraphNode* gn = FindNodeByIndex(dstIndex);
         if (gn == nullptr)
         {
            outError = "unknown node index (dst/index must be an integer node index from get_graph)";
            return true;
         }
         // A bare name ("uRotation") works as well as the typed key ("f uRotation").
         std::string tryKey = key;
         int param = ParamIndexOfKey(*gn, tryKey, outError);
         for (const char* prefix : { "f ", "i ", "b ", "c ", "s " })
            if (param < 0 && key.find(' ') == std::string::npos && !key.empty())
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
            std::string text = params.value("text", std::string());
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
            PushUndoCheckpoint();
            Modulation::Instance().Unbind(gn->index, param);
         }
         else
         {
            const json srcField = params.value("src", json());
            const int srcIndex = srcField.is_number_integer() ? srcField.get<int>() : srcField.is_object() ? srcField.value("index", -1) : -1;
            GraphNode* src = FindNodeByIndex(srcIndex);
            int output = params.value("output", json()).is_number_integer() ? params.value("output", 0) : 0;
            if (srcField.is_object() && srcField.value("output", json()).is_number_integer())
               output = srcField.value("output", 0);
            if (src == nullptr || output < 0 || output >= GraphNode::kStride - GraphNode::kOutputBase)
            {
               outError = "unknown modulator node or output";
               return true;
            }
            PushUndoCheckpoint();
            Modulation::Instance().Bind(gn->index, param, src->index, output);
            if (params.contains("lo") && params.contains("hi"))
               Modulation::Instance().SetRange(gn->index, param, params.value("lo", 0.0f), params.value("hi", 1.0f));
         }
         gPatchDirty = true;
         outResult = { { "index", gn->index }, { "param", param } };
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

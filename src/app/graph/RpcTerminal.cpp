// The RPC verbs the `infinite` command adds to the existing set (Rpc.cpp): what the human is
// pointing at, the transport, modulation by control name, bypass, and the code errors of the
// open patch. Same rules as Rpc.cpp: main thread, between frames, one undo step per edit.
#include <algorithm>
#include <climits> // fork: k17 INT_MIN/INT_MAX, so a wild number is refused instead of cast
#include <cstdlib> // fork: k17 std::strtol, for a node index written as a string
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

      // fork: k17 how a node is named in a call. The same argument arrives as a bare integer (5), a numeric
      // string ("5"), or an object ({"index":5} / {"node":5}) depending on which tool the caller
      // learned, and under two names - `dst` on bind_mod, `index` everywhere else. Reading one
      // spelling only is what made bind_mod answer "unknown node index" for a node that exists.
      // Bounded and total: any JSON value at all is accepted and answered with -1, never an exception
      // and never a walk into a pathological nesting an agent could send by accident.
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
            // A double outside int range is not a node index; casting it is undefined behaviour.
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

      // fork: k17 `key`, `param`, `name` and `control` mean the same thing, at the top level or inside the
      // destination object. An empty result is reported by the caller, never silently ignored.
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
         else if (action == "loop")
         {
            // fork: the arrange loop had no RPC at all, so loopStart/loopEnd could only be set by
            // dragging in the UI (API-7). Beats, like `timeline loop` and the transport itself.
            // Goes through ArrangeSetLoop, never tr.SetLoop directly: the model republishes to the
            // transport every frame (PublishArrangeLoop), so a direct write would be overwritten.
            const Arrange::LoopRange& cur = gArrange.settings.loop;
            bool enabled = params.value("enabled", true);
            if (enabled && !params.contains("start") && !params.contains("end") && cur.end <= cur.start)
            {
               outError = "the loop has no range yet: give start and end (beats), e.g. {\"action\":\"loop\",\"enabled\":true,\"start\":0,\"end\":16}";
               return true;
            }
            Arrange::Tick start = params.contains("start") ? Arrange::BeatsToTicks(params.value("start", 0.0)) : cur.start;
            Arrange::Tick end = params.contains("end") ? Arrange::BeatsToTicks(params.value("end", 0.0)) : cur.end;
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
         // The control socket checks this (RemoteControl::SafeDispatch), but a batch, the chat panel and
         // the self-tests call the handler directly, and nlohmann's value() throws on a non-object.
         // Refuse the shape here instead of letting an exception escape the command.
         if (!params.is_object())
         {
            outError = "params must be a JSON object, e.g. {\"dst\":5,\"key\":\"f rotation\",\"src\":4}";
            return true;
         }
         // fork: k17 `dst`, `index`, `node` and `target` all name the node. bind_mod's documentation says
         // `dst`, but get_params, set_param and unbind_mod all say `index`, so a caller that learned
         // one spelling and wrote the other used to get "unknown node index" for a node that exists
         // (API-6). Each field also takes an integer, a numeric string, or an {index|node: n} object.
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
            // A non-string text (yaml-style `text: 5`) is named instead of thrown at: value<string>()
            // raises a type error, and the caller is an agent that can fix its own arguments.
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
            // An unbind of something that was never bound used to push an empty undo step; keep the
            // history honest by only checkpointing a change that exists.
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
            // fork: k17 the modulator takes the same spellings as the destination, under the names an
            // agent reaches for (`src`, `source`, `from`, `modulator`), plus `output` / `out`.
            json srcField;
            for (const char* name : { "src", "source", "from", "modulator" })
               if (params.contains(name) && !params[name].is_null())
               {
                  srcField = params[name];
                  break;
               }
            const int srcIndex = NodeIndexOfField(srcField);
            GraphNode* src = FindNodeByIndex(srcIndex);
            // NodeIndexOfField is the same total reader the destination uses: it clamps, so a wild
            // `output` (1e300, a huge unsigned, "x") comes back as -1 and is refused below instead of
            // being cast (an out-of-range cast is undefined and threw out of json::get<int>() before).
            int output = 0;
            for (const char* name : { "output", "out" })
               if (params.contains(name) && !params[name].is_null())
                  output = NodeIndexOfField(params[name]);
            if (srcField.is_object())
               for (const char* name : { "output", "out" })
                  if (srcField.contains(name) && !srcField[name].is_null())
                     output = NodeIndexOfField(srcField[name]);
            // lo/hi are validated before anything is bound: half a range or a string is a mistake the
            // caller can fix, not a binding that silently keeps the destination's own span.
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

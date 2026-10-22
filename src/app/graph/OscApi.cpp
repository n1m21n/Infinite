// The OSC control API's address map (v0.6 2.31). Socket and queue are in
// core/OscControl; this runs on the main thread from the frame pump, so it
// uses the same graph calls as the RemoteControl RPC (Rpc.cpp).
//
//   /infinite/node/<index>/<param> <value>   set a param (no undo step: a
//                                           fader moves it many times a second)
//   /infinite/node/<index>/<param>           query; reply on the same address
//   /infinite/node/<index>                   reply every param, then .../end
//   /infinite/nodes                          reply /infinite/node <index> <type> per node, then /infinite/nodes/end <count>
//   /infinite/listen <address>               send <address> <value> whenever it changes (and once now)
//   /infinite/unlisten <address>
//   /infinite/create <type> [x y]            reply /infinite/created <index>
//   /infinite/delete <index>
//   /infinite/connect <src> <output> <dst> <slot>
//   /infinite/disconnect <dst> <slot>
//   /infinite/show/next, /infinite/show/prev  step the show set (v0.6 2.35)
//   /infinite/show/go <index>                switch to show-set entry <index> (0-based)
//
// Param names are the patch-file names with spaces as '_' (OSC addresses
// cannot hold a space): "warp ratio" is /infinite/node/3/warp_ratio. Any
// failure replies /infinite/error <message>.
#include "app/AppShared.h"

#include "core/OscControl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app
{
   namespace
   {
      using OscMessage::Arg;
      using OscMessage::FloatArg;
      using OscMessage::IntArg;
      using OscMessage::StringArg;

      struct Listener
      {
         OscControl::Peer peer;
         std::string address;
         int nodeIndex = -1;
         std::string param;
         std::string lastValue;
         bool sent = false;
      };
      std::vector<Listener> gListeners;
      constexpr size_t kMaxListeners = 512;

      std::string OscName(const std::string& name)
      {
         std::string s = name;
         for (char& c : s)
            if (c == ' ')
               c = '_';
         return s;
      }

      void Error(const OscControl::Peer& to, const std::string& msg)
      {
         OscControl::Send(to, "/infinite/error", { StringArg(msg) });
      }

      // Show lock keeps nodes and cables where they are: a controller gets the
      // refusal the canvas gives. Param sets and creating a node still go through.
      bool RefusedByShowLock(const OscControl::Request& req)
      {
         if (!ShowLockBlocks())
            return false;
         Error(req.from, req.address + ": show lock is on");
         return true;
      }

      // "f name" / "i name" / ... -> the value as the OSC type it reads best as.
      Arg ValueArg(const std::string& key, const std::string& value)
      {
         const char t = key.empty() ? 's' : key[0];
         if (t == 'f')
            return FloatArg(std::strtof(value.c_str(), nullptr));
         if (t == 'i' || t == 'b')
            return IntArg(static_cast<int32_t>(std::strtol(value.c_str(), nullptr, 10)));
         return StringArg(value);
      }

      // The saved key ("<type letter> <name>") and value of a param, matched by
      // its OSC name or its real name.
      bool FindParam(GraphNode* gn, const std::string& name, std::string& outKey, std::string& outValue)
      {
         std::vector<std::pair<std::string, std::string>> raw;
         Patch::SaveParams(gn->node.get(), raw);
         for (const auto& kv : raw)
         {
            const size_t sp = kv.first.find(' ');
            if (sp == std::string::npos)
               continue;
            const std::string real = kv.first.substr(sp + 1);
            if (real == name || OscName(real) == name)
            {
               outKey = kv.first;
               outValue = kv.second;
               return true;
            }
         }
         return false;
      }

      // "/infinite/node/12/cutoff" -> 12, "cutoff". param is empty for "/infinite/node/12".
      bool ParseNodeAddress(const std::string& address, int& outIndex, std::string& outParam)
      {
         static const std::string kPrefix = "/infinite/node/";
         if (address.compare(0, kPrefix.size(), kPrefix) != 0)
            return false;
         const std::string rest = address.substr(kPrefix.size());
         const size_t slash = rest.find('/');
         const std::string num = rest.substr(0, slash);
         if (num.empty() || num.find_first_not_of("0123456789") != std::string::npos)
            return false;
         outIndex = std::atoi(num.c_str());
         outParam = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
         return true;
      }

      std::string ArgToParamText(const std::string& key, const Arg& a)
      {
         const char t = key.empty() ? 's' : key[0];
         char buf[64];
         switch (a.type)
         {
            case OscMessage::ArgType::Float:
               if (t == 'i' || t == 'b')
                  snprintf(buf, sizeof(buf), "%ld", std::lround(a.f));
               else
                  snprintf(buf, sizeof(buf), "%.9g", a.f);
               return buf;
            case OscMessage::ArgType::Int:
               snprintf(buf, sizeof(buf), "%d", a.i);
               return buf;
            default:
               return a.s;
         }
      }

      bool ArgInt(const std::vector<Arg>& args, size_t i, int& out)
      {
         if (i >= args.size())
            return false;
         if (args[i].type == OscMessage::ArgType::Int)
            out = args[i].i;
         else if (args[i].type == OscMessage::ArgType::Float)
            out = static_cast<int>(std::lround(args[i].f));
         else
            return false;
         return true;
      }

      bool HasCableInto(int dst, int slot)
      {
         const Patch::Data d = BuildPatchData();
         for (const auto* list : { &d.cables, &d.geometry, &d.audio, &d.notes })
            for (const Patch::CableRecord& c : *list)
               if (c.dstIndex == dst && c.dstSlot == slot)
                  return true;
         return false;
      }

      std::string CategoryOf(const std::string& typeName)
      {
         for (const std::string& cat : NodeFactory::Instance().GetCategories())
            for (const std::string& n : NodeFactory::Instance().GetNodesInCategory(cat))
               if (n == typeName)
                  return cat;
         return std::string();
      }

      void HandleNodeAddress(const OscControl::Request& req, int index, const std::string& param)
      {
         GraphNode* gn = FindNodeByIndex(index);
         if (gn == nullptr)
         {
            Error(req.from, "unknown node " + std::to_string(index));
            return;
         }
         if (param.empty())
         {
            std::vector<std::pair<std::string, std::string>> raw;
            Patch::SaveParams(gn->node.get(), raw);
            const std::string base = "/infinite/node/" + std::to_string(index) + "/";
            for (const auto& kv : raw)
            {
               const size_t sp = kv.first.find(' ');
               if (sp != std::string::npos)
                  OscControl::Send(req.from, base + OscName(kv.first.substr(sp + 1)), { ValueArg(kv.first, kv.second) });
            }
            OscControl::Send(req.from, base + "end", { IntArg(static_cast<int32_t>(raw.size())) });
            return;
         }
         std::string key, value;
         if (!FindParam(gn, param, key, value))
         {
            Error(req.from, "node " + std::to_string(index) + " has no param '" + param + "'");
            return;
         }
         if (req.args.empty())
         {
            OscControl::Send(req.from, req.address, { ValueArg(key, value) });
            return;
         }
         if (req.args[0].type == OscMessage::ArgType::Float && !std::isfinite(req.args[0].f))
         {
            Error(req.from, req.address + ": value must be a finite number");
            return;
         }
         Patch::LoadParams(gn->node.get(), { { key, ArgToParamText(key, req.args[0]) } });
      }
   }

   void HandleOscRequest(const OscControl::Request& req)
   {
      const std::string& a = req.address;
      int index = -1;
      std::string param;
      if (ParseNodeAddress(a, index, param))
      {
         HandleNodeAddress(req, index, param);
      }
      else if (a == "/infinite/nodes")
      {
         for (const GraphNode& gn : gNodes)
            OscControl::Send(req.from, "/infinite/node", { IntArg(gn.index), StringArg(gn.typeName) });
         OscControl::Send(req.from, "/infinite/nodes/end", { IntArg(static_cast<int32_t>(gNodes.size())) });
      }
      else if (a == "/infinite/listen" || a == "/infinite/unlisten")
      {
         if (req.args.empty() || req.args[0].type != OscMessage::ArgType::String)
         {
            Error(req.from, a + " needs an address string");
            return;
         }
         const std::string& target = req.args[0].s;
         gListeners.erase(std::remove_if(gListeners.begin(), gListeners.end(),
                                         [&](const Listener& l) { return l.peer == req.from && l.address == target; }),
                          gListeners.end());
         if (a == "/infinite/unlisten")
            return;
         Listener l;
         l.peer = req.from;
         l.address = target;
         std::string key, value;
         GraphNode* gn = nullptr;
         if (!ParseNodeAddress(target, l.nodeIndex, l.param) || l.param.empty() ||
             (gn = FindNodeByIndex(l.nodeIndex)) == nullptr || !FindParam(gn, l.param, key, value))
         {
            Error(req.from, "cannot listen to '" + target + "'");
            return;
         }
         if (gListeners.size() >= kMaxListeners)
         {
            Error(req.from, "too many listeners");
            return;
         }
         gListeners.push_back(l); // PollOscListeners sends the current value this frame
      }
      else if (a == "/infinite/create")
      {
         if (req.args.empty() || req.args[0].type != OscMessage::ArgType::String)
         {
            Error(req.from, "/infinite/create needs a node type");
            return;
         }
         const std::string type = req.args[0].s;
         const std::string cat = CategoryOf(type);
         const float x = req.args.size() > 1 && req.args[1].type == OscMessage::ArgType::Float ? req.args[1].f : 0.0f;
         const float y = req.args.size() > 2 && req.args[2].type == OscMessage::ArgType::Float ? req.args[2].f : 0.0f;
         GraphNode* gn = cat.empty() ? nullptr : SpawnNode(type, cat, x, y);
         if (gn == nullptr)
         {
            Error(req.from, "unknown node type '" + type + "'");
            return;
         }
         OscControl::Send(req.from, "/infinite/created", { IntArg(gn->index) });
      }
      else if (a == "/infinite/delete")
      {
         if (RefusedByShowLock(req))
            return;
         int idx = -1;
         if (!ArgInt(req.args, 0, idx) || FindNodeByIndex(idx) == nullptr)
         {
            Error(req.from, "/infinite/delete: unknown node");
            return;
         }
         RemoveNodeByIndex(idx);
      }
      else if (a == "/infinite/connect")
      {
         if (RefusedByShowLock(req))
            return;
         int src = -1, out = 0, dst = -1, slot = 0;
         if (!ArgInt(req.args, 0, src) || !ArgInt(req.args, 1, out) || !ArgInt(req.args, 2, dst) ||
             !ArgInt(req.args, 3, slot))
         {
            Error(req.from, "/infinite/connect needs src output dst slot");
            return;
         }
         std::string err;
         if (!ConnectNodes(src, out, dst, slot, err))
            Error(req.from, "/infinite/connect: " + err);
      }
      else if (a == "/infinite/disconnect")
      {
         if (RefusedByShowLock(req))
            return;
         int dst = -1, slot = 0;
         if (!ArgInt(req.args, 0, dst) || !ArgInt(req.args, 1, slot))
         {
            Error(req.from, "/infinite/disconnect needs dst slot");
            return;
         }
         GraphNode* gn = FindNodeByIndex(dst);
         if (gn != nullptr && HasCableInto(dst, slot))
         {
            PushUndoCheckpoint("Disconnect cable");
            DisconnectDstPin(gn->InputPinId(slot));
            return;
         }
         Error(req.from, "/infinite/disconnect: no cable into that slot");
      }
      else if (a == "/infinite/show/next" || a == "/infinite/show/prev")
      {
         ShowSetStep(a == "/infinite/show/next" ? +1 : -1);
      }
      else if (a == "/infinite/show/go")
      {
         int idx = -1;
         if (!ArgInt(req.args, 0, idx) || idx < 0 || idx >= ShowSetSize())
         {
            Error(req.from, "/infinite/show/go: no such show-set entry");
            return;
         }
         ShowSetGo(idx);
      }
      else
      {
         Error(req.from, "unknown address '" + a + "'");
      }
   }

   // Sends each listened param whose value changed since it was last sent.
   // Main thread, once per frame after HandleOscRequest.
   void PollOscListeners()
   {
      for (size_t i = 0; i < gListeners.size();)
      {
         Listener& l = gListeners[i];
         GraphNode* gn = FindNodeByIndex(l.nodeIndex);
         std::string key, value;
         if (gn == nullptr || !FindParam(gn, l.param, key, value))
         {
            Error(l.peer, "'" + l.address + "' is gone; stopped listening");
            gListeners.erase(gListeners.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
         }
         if (!l.sent || value != l.lastValue)
         {
            OscControl::Send(l.peer, l.address, { ValueArg(key, value) });
            l.lastValue = value;
            l.sent = true;
         }
         i++;
      }
   }

   void ClearOscListeners() { gListeners.clear(); }
}

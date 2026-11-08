// Host slots <-> Macro nodes in plugin mode (docs/plans/vst-au/README.md 8.2, step 2.2).
//
//   host slot (smoothed, snapshot per frame grid) -> bound Macro value -> modulation cables
//   Macro turned in Infinite -> begin / perform / end edit -> host automation
//   Macro label -> slot name in the host
//
// A Macro's "Host slot" field (1..128, 0 = none) binds it; an XY pad binds X and Y separately.
// Any other knob binds through its right-click menu (Host automation): gHostParams, named
// "<node> <param>" in the host. Unbound slots are hidden in the host.
#include "app/AppShared.h"
#include "plugin/engine/HostAudio.h"
#include "app/ui/design/components/MenuParts.h"
#include "platform/Platform.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace app
{
namespace
{
   enum class Kind { Knob, Slider, Bipolar, Toggle, Trigger, NumBox, Radio, StepGate, XYx, XYy, Param };

   struct Binding
   {
      INode* node = nullptr;
      Kind kind = Kind::Knob;
      uint64_t key = 0; // node uid * 2 + axis; a Param: (uid * 4096 + param) | bit 63
      int slot = 0;     // 0..kSlotCount-1
      std::string name;
      const ParamRef* ref = nullptr; // Kind::Param: this frame's widget
   };

   bool Stepped(const Binding& b)
   {
      if (b.kind == Kind::Param)
         return b.ref->isBool || b.ref->isEnum || b.ref->step > 0.0f;
      const Kind k = b.kind;
      return k == Kind::Toggle || k == Kind::Trigger || k == Kind::Radio || k == Kind::StepGate;
   }

   float Read(const Binding& b)
   {
      switch (b.kind)
      {
      case Kind::Param: return ParamToPos(*b.ref, *b.ref->value);
      case Kind::Knob: return static_cast<MacroKnobNode*>(b.node)->Value01();
      case Kind::Slider: return static_cast<MacroSliderNode*>(b.node)->Value01();
      case Kind::Bipolar: return static_cast<MacroBipolarKnobNode*>(b.node)->Value01();
      case Kind::Toggle: return static_cast<MacroToggleNode*>(b.node)->state ? 1.0f : 0.0f;
      case Kind::Trigger: return static_cast<MacroTriggerNode*>(b.node)->pressed ? 1.0f : 0.0f;
      case Kind::NumBox: return static_cast<MacroNumBoxNode*>(b.node)->Value01();
      case Kind::Radio: return static_cast<MacroRadioSelectorNode*>(b.node)->Value01();
      case Kind::StepGate: return (float)static_cast<MacroStepGateNode*>(b.node)->pattern / 255.0f;
      case Kind::XYx: return std::clamp(static_cast<MacroXYNode*>(b.node)->padX, 0.0f, 1.0f);
      case Kind::XYy: return std::clamp(static_cast<MacroXYNode*>(b.node)->padY, 0.0f, 1.0f);
      }
      return 0.0f;
   }

   void Write(const Binding& b, float v)
   {
      v = std::clamp(v, 0.0f, 1.0f);
      switch (b.kind)
      {
      case Kind::Param:
         *b.ref->value = b.ref->isBool ? (v >= 0.5f ? 1.0f : 0.0f) : ShapeToParam(*b.ref, PosToParam(*b.ref, v));
         break;
      case Kind::Knob: static_cast<MacroKnobNode*>(b.node)->value = v; break;
      case Kind::Slider: static_cast<MacroSliderNode*>(b.node)->value = v; break;
      case Kind::Bipolar: static_cast<MacroBipolarKnobNode*>(b.node)->value = v * 2.0f - 1.0f; break;
      case Kind::Toggle: static_cast<MacroToggleNode*>(b.node)->state = v >= 0.5f; break;
      case Kind::Trigger:
      {
         auto* n = static_cast<MacroTriggerNode*>(b.node);
         if (v >= 0.5f && !n->pressed)
            n->Trigger();
         else if (v < 0.5f)
            n->pressed = false;
         break;
      }
      case Kind::NumBox:
      {
         auto* n = static_cast<MacroNumBoxNode*>(b.node);
         n->value = n->minVal + v * (n->maxVal - n->minVal);
         break;
      }
      case Kind::Radio:
      {
         auto* n = static_cast<MacroRadioSelectorNode*>(b.node);
         n->selected = n->count > 1 ? (int)std::lround(v * (float)(n->count - 1)) : 0;
         break;
      }
      case Kind::StepGate: static_cast<MacroStepGateNode*>(b.node)->pattern = (uint8_t)std::lround(v * 255.0f); break;
      case Kind::XYx: static_cast<MacroXYNode*>(b.node)->padX = v; break;
      case Kind::XYy: static_cast<MacroXYNode*>(b.node)->padY = v; break;
      }
   }

   template <class T>
   bool Add(std::vector<Binding>& out, GraphNode& gn, Kind kind)
   {
      T* n = dynamic_cast<T*>(gn.node.get());
      if (n == nullptr)
         return false;
      if (n->hostSlot >= 1 && n->hostSlot <= PluginEngine::kSlotCount)
         out.push_back({ n, kind, gn.uid * 2, n->hostSlot - 1, n->label });
      return true;
   }

   void Collect(std::vector<Binding>& out)
   {
      for (GraphNode& gn : gNodes)
      {
         if (gn.node == nullptr)
            continue;
         if (auto* xy = dynamic_cast<MacroXYNode*>(gn.node.get()))
         {
            if (xy->hostSlot >= 1 && xy->hostSlot <= PluginEngine::kSlotCount)
               out.push_back({ xy, Kind::XYx, gn.uid * 2, xy->hostSlot - 1, "XY X" });
            if (xy->hostSlotY >= 1 && xy->hostSlotY <= PluginEngine::kSlotCount)
               out.push_back({ xy, Kind::XYy, gn.uid * 2 + 1, xy->hostSlotY - 1, "XY Y" });
            continue;
         }
         Add<MacroKnobNode>(out, gn, Kind::Knob) || Add<MacroSliderNode>(out, gn, Kind::Slider) ||
            Add<MacroBipolarKnobNode>(out, gn, Kind::Bipolar) || Add<MacroToggleNode>(out, gn, Kind::Toggle) ||
            Add<MacroTriggerNode>(out, gn, Kind::Trigger) || Add<MacroNumBoxNode>(out, gn, Kind::NumBox) ||
            Add<MacroRadioSelectorNode>(out, gn, Kind::Radio) || Add<MacroStepGateNode>(out, gn, Kind::StepGate);
      }
      if (gHostParams.empty())
         return;
      std::unordered_map<uint64_t, const ParamRef*> refs;
      for (const ParamRef& r : Modulation::Instance().FrameParams())
         if (r.value != nullptr)
            refs.emplace((uint64_t)(uint32_t)r.nodeIndex << 32 | (uint32_t)r.paramIndex, &r);
      for (HostParamBinding& h : gHostParams)
      {
         GraphNode* gn = FindNodeByUid(h.uid);
         if (gn == nullptr || h.slot < 0 || h.slot >= PluginEngine::kSlotCount)
            continue;
         const auto it = refs.find((uint64_t)(uint32_t)gn->index << 32 | (uint32_t)h.param);
         if (it != refs.end())
         {
            // "<Node> <Param>" as the app shows them, in Title Case for host lists ("Analog Cutoff");
            // a one-param node is just its title ("Gain").
            std::string node = NodeTitle(*gn), param = it->second->name;
            std::string name = node;
            std::string lo = node, plo = param;
            for (char& c : lo) c = (char)std::tolower((unsigned char)c);
            for (char& c : plo) c = (char)std::tolower((unsigned char)c);
            if (!param.empty() && plo != lo)
               name += " " + param;
            for (size_t c = 0; c < name.size(); c++)
               if (c == 0 || name[c - 1] == ' ')
                  name[c] = (char)std::toupper((unsigned char)name[c]);
            h.name = name;
         }
         Binding b;
         b.node = gn->node.get();
         b.kind = Kind::Param;
         b.key = (h.uid * 4096 + (uint64_t)h.param) | (1ull << 63);
         b.slot = h.slot;
         b.name = h.name.empty() ? std::string("Param") : h.name;
         b.ref = it != refs.end() ? it->second : nullptr;
         out.push_back(b);
      }
   }

   // Slots in use by a Macro or a host-automated param.
   PluginEngine::SlotMask UsedSlots()
   {
      std::vector<Binding> binds;
      Collect(binds);
      PluginEngine::SlotMask m;
      for (const Binding& b : binds)
         m.Set(b.slot);
      return m;
   }

   struct SlotState
   {
      float lastApplied = NAN; // the value as of the end of the last frame (slot domain)
      float lastSnap = NAN;    // host snapshot last seen
      bool editing = false;    // a begin was sent, end not yet
      int quiet = 0;           // frames since the last perform
      int hold = 0;            // frames the host snapshot is ignored after a perform
      uint64_t seen = 0;
   };

   constexpr float kEps = 1e-5f;
   constexpr int kEndAfterQuietFrames = 15;
   constexpr int kHoldFrames = 30; // ~0.5 s at 60 fps
}

void HostSlotsFrame()
{
   static std::unordered_map<uint64_t, SlotState> sStates;
   static uint64_t sFrame = 0;
   static bool sNamesSent = false;
   static std::string sNames[PluginEngine::kSlotCount];
   sFrame++;

   PluginEngine::SlotFrame frame;
   PluginEngine::BeginSlotFrame(&frame);

   std::vector<Binding> binds;
   Collect(binds);

   // Development / tests only (tools/plugin, ShellCheck): "<frame>:<value>" turns the first bound
   // Macro to value on that frame, as a hand would, to exercise the edit path back to the host.
   bool testEdit = false;
   static int sTestFrame = -2;
   static float sTestValue = 0.0f;
   if (sTestFrame == -2)
   {
      sTestFrame = -1;
      if (const char* e = std::getenv("INFINITE_PLUGIN_TEST_EDIT"))
         if (std::sscanf(e, "%d:%f", &sTestFrame, &sTestValue) != 2)
            sTestFrame = -1;
   }
   if (sTestFrame >= 0 && (uint64_t)sTestFrame == sFrame && !binds.empty() && (binds[0].kind != Kind::Param || binds[0].ref))
   {
      Write(binds[0], sTestValue);
      testEdit = true;
   }

   const ImGuiIO& io = ImGui::GetIO();
   const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
   const bool userInput = mouseDown || io.WantTextInput || testEdit;

   PluginEngine::SlotMask bound, stepped;
   std::string names[PluginEngine::kSlotCount];
   for (const Binding& b : binds)
   {
      if (!bound.Test(b.slot) && names[b.slot].empty())
         names[b.slot] = b.name.empty() ? std::string("Macro") : b.name;
      if (b.kind == Kind::Param && b.ref == nullptr)
         continue; // its widget is not drawn this frame: the slot keeps its name, values wait
      bound.Set(b.slot);
      if (Stepped(b))
         stepped.Set(b.slot);

      SlotState& st = sStates[b.key ^ ((uint64_t)b.slot << 52)];
      st.seen = sFrame;
      const bool fresh = std::isnan(st.lastApplied);
      const float cur = Read(b);

      // Infinite -> host: a hand moved it since we last looked.
      if (!std::isnan(st.lastApplied) && std::fabs(cur - st.lastApplied) > kEps)
      {
         if (userInput || st.editing)
         {
            if (!st.editing)
               PluginEngine::SendSlotEdit(PluginEngine::kSlotEditBegin, b.slot, cur);
            PluginEngine::SendSlotEdit(PluginEngine::kSlotEditPerform, b.slot, cur);
            st.editing = true;
            st.quiet = 0;
            st.hold = kHoldFrames;
         }
         st.lastApplied = cur;
      }
      else if (st.editing && ++st.quiet >= kEndAfterQuietFrames && !mouseDown)
      {
         PluginEngine::SendSlotEdit(PluginEngine::kSlotEditEnd, b.slot, cur);
         st.editing = false;
      }

      // Host -> Infinite.
      if (frame.validMask.Test(b.slot))
      {
         const float sv = frame.values[b.slot];
         if (st.editing || st.hold > 0)
         {
            // The host echoes the edit back; until it does (or the hold runs out) the hand wins.
            st.hold = std::fabs(sv - cur) < 1e-3f ? 0 : st.hold - 1;
         }
         else if (std::isnan(st.lastSnap) || sv != st.lastSnap)
         {
            Write(b, sv);
            st.lastApplied = Read(b);
         }
         st.lastSnap = sv;
      }
      if (std::isnan(st.lastApplied))
         st.lastApplied = Read(b);
      // A new binding the host has no value for: the patch's value becomes the slot's (the shell
      // drops it if the host knew the slot after all, and its value then arrives here).
      if (fresh && !frame.validMask.Test(b.slot))
         PluginEngine::SendSlotEdit(PluginEngine::kSlotEditSync, b.slot, st.lastApplied);
   }
   for (auto it = sStates.begin(); it != sStates.end();)
      it = it->second.seen == sFrame ? std::next(it) : sStates.erase(it);

   PluginEngine::SetSlotBindings(bound, stepped);

   bool namesChanged = !sNamesSent;
   for (int i = 0; i < PluginEngine::kSlotCount && !namesChanged; i++)
      namesChanged = names[i] != sNames[i];
   if (namesChanged)
   {
      for (int i = 0; i < PluginEngine::kSlotCount; i++)
         sNames[i] = names[i];
      PluginEngine::SendSlotNames(sNames);
      sNamesSent = true;
   }
}

void DrawParamHostAutomationMenuItem(int nodeIndex, int paramIndex)
{
   if (!Platform::PluginModeActive())
      return;
   const GraphNode* gn = FindNodeByIndex(nodeIndex);
   if (gn == nullptr)
      return;
   for (size_t i = 0; i < gHostParams.size(); i++)
   {
      const HostParamBinding& h = gHostParams[i];
      if (h.uid != gn->uid || h.param != paramIndex)
         continue;
      char label[64];
      std::snprintf(label, sizeof label, "Remove host automation (%d)", h.slot + 1);
      if (MenuParts::Item(label))
      {
         PushUndoCheckpoint("Remove host automation");
         gHostParams.erase(gHostParams.begin() + (std::ptrdiff_t)i);
      }
      return;
   }
   const PluginEngine::SlotMask used = UsedSlots();
   int slot = -1;
   for (int s = 0; s < PluginEngine::kSlotCount && slot < 0; s++)
      if (!used.Test(s))
         slot = s;
   if (slot < 0)
   {
      ImGui::BeginDisabled();
      MenuParts::Item("Host automation (slots full)");
      ImGui::EndDisabled();
      return;
   }
   if (MenuParts::Item("Host automation"))
   {
      PushUndoCheckpoint("Host automation");
      gHostParams.push_back({ gn->uid, paramIndex, slot, std::string() });
   }
}

std::vector<HostParamBinding> gHostParams;
}

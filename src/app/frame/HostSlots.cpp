// Host slots <-> Macro nodes in plugin mode (docs/plans/vst-au/README.md 8.2, step 2.2).
//
//   host slot (smoothed, snapshot per frame grid) -> bound Macro value -> modulation cables
//   Macro turned in Infinite -> begin / perform / end edit -> host automation
//   Macro label -> slot name in the host
//
// A Macro's "Host slot" field (1..32, 0 = none) binds it; an XY pad binds X and Y separately.
#include "app/AppShared.h"
#include "plugin/engine/HostAudio.h"

#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace app
{
namespace
{
   enum class Kind { Knob, Slider, Bipolar, Toggle, Trigger, NumBox, Radio, StepGate, XYx, XYy };

   struct Binding
   {
      INode* node = nullptr;
      Kind kind = Kind::Knob;
      uint64_t key = 0; // node uid * 2 + axis
      int slot = 0;     // 0..31
      std::string name;
   };

   bool Stepped(Kind k) { return k == Kind::Toggle || k == Kind::Trigger || k == Kind::Radio || k == Kind::StepGate; }

   float Read(const Binding& b)
   {
      switch (b.kind)
      {
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
   if (sTestFrame >= 0 && (uint64_t)sTestFrame == sFrame && !binds.empty())
   {
      Write(binds[0], sTestValue);
      testEdit = true;
   }

   const ImGuiIO& io = ImGui::GetIO();
   const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
   const bool userInput = mouseDown || io.WantTextInput || testEdit;

   uint32_t bound = 0, stepped = 0;
   std::string names[PluginEngine::kSlotCount];
   for (const Binding& b : binds)
   {
      const uint32_t bit = 1u << b.slot;
      if (!(bound & bit) && names[b.slot].empty())
         names[b.slot] = b.name.empty() ? std::string("Macro") : b.name;
      bound |= bit;
      if (Stepped(b.kind))
         stepped |= bit;

      SlotState& st = sStates[b.key * 64 + (uint64_t)b.slot];
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
      if (frame.validMask & bit)
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
      if (fresh && !(frame.validMask & bit))
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
}

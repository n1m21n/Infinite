// Mixer panel self-test (INFINITE_MIXERPANELTEST): the A3 shell. Five docks drawn where they belong,
// open/dock/size survive save and load, and Mixer Audio lets only a Mixer-fed Audio Out reach the device.
// Plan: internal/plans/mixer-panel.
#include "app/AppShared.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace app
{
namespace
{
   bool sOk = true;
   void Check(const std::string& label, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", label.c_str());
      if (!pass)
         sOk = false;
   }

   void CheckRoutingAndPersist()
   {
      // Save and load: one writer, one reader.
      gMixerPanelOpen = true;
      gMixerPanelDock = 2;
      gMixerPanelWidth = 640.0f;
      gMixerPanelHeight = 333.0f;
      std::ostringstream saved;
      MixerPanelDockWrite(saved);
      gMixerPanelOpen = false; gMixerPanelDock = 0; gMixerPanelWidth = 400.0f; gMixerPanelHeight = 200.0f;
      std::istringstream in(saved.str());
      for (std::string line; std::getline(in, line);)
      {
         const size_t eq = line.find('=');
         if (eq != std::string::npos)
            MixerPanelDockReadKey(line.substr(0, eq), line.substr(eq + 1));
      }
      Check("open, dock and sizes survive save and load",
            gMixerPanelOpen && gMixerPanelDock == 2 && gMixerPanelWidth == 640.0f && gMixerPanelHeight == 333.0f);
      MixerPanelDockReadKey("panelMixerDock", "99");
      MixerPanelDockReadKey("panelMixerW", "3");
      Check("a bad dock or size is clamped on load", gMixerPanelDock == kDockFloating && gMixerPanelWidth == kMixerPanelMinWidth);

      // Routing: Delay A -> Audio Out 1 (direct); Delay B -> Mixer -> Audio Out 2 (a console).
      NewPatch();
      const int a = SpawnNode("Delay", "AudioEffects", 0.0f, 0.0f)->index;
      const int b = SpawnNode("Delay", "AudioEffects", 0.0f, 300.0f)->index;
      const int mix = SpawnNode("Mixer", "Utility", 300.0f, 300.0f)->index;
      const int out1 = SpawnNode("Audio Out", "Utility", 600.0f, 0.0f)->index;
      const int out2 = SpawnNode("Audio Out", "Utility", 600.0f, 300.0f)->index;
      std::string err;
      bool wired = ConnectNodes(a, 0, out1, 0, err);
      wired = ConnectNodes(b, 0, mix, 0, err) && wired;
      wired = ConnectNodes(mix, 0, out2, 0, err) && wired;
      Check("test patch wires up", wired);

      gAudioMode = AudioMode::Canvas;
      RebuildAudioTopology();
      const int canvasTerminals = gAudioTopologyTerminalCount;
      Check("canvas mode: both Audio Outs reach the device", canvasTerminals == 2);

      gAudioMode = AudioMode::Mixer; // set directly: SetMixerAudio would start the device
      Check("Mixer mode is not timeline routing", !ArrangeTimelineRoutingActive());
      Check("Mixer mode routes the console only", MixerOnlyRouting());
      Check("switching mode marks the topology stale", ArrangeAudioRebuildIfStale());
      Check("Mixer mode: only the Mixer-fed Audio Out reaches the device", gAudioTopologyTerminalCount == 1);
      Check("no rebuild while nothing changes", !ArrangeAudioRebuildIfStale());

      SetMixerAudio(false);
      Check("leaving Mixer Audio returns to the canvas", gAudioMode == AudioMode::Canvas && gAudioTopologyTerminalCount == 2);
      NewPatch();
   }
}

// Sets each dock and reads the rect the panel was drawn in, three frames later; then the frame-free checks.
bool MixerPanelTestStep(int frameId)
{
   static const char* kNames[5] = { "bottom", "right", "left", "top", "floating" };
   static const int kOrder[5] = { 0, 3, 1, 2, kDockFloating };
   static int sStep = -1, sSetAt = 0;
   if (sStep > 5)
      return true;
   if (sStep < 0)
   {
      gMixerPanelOpen = true;
      gView2DOpen = gView3DOpen = gArrangePanelOpen = gSessionPanelOpen = gSampleStudioOpen = gPianoRollOpen = false;
      gViewportPanelOpen = gModMatrixOpen = gPerfPanelOpen = gTerminalOpen = gHistoryOpen = false;
      gMixerPanelHeight = 300.0f;
      gMixerPanelWidth = 520.0f;
      gMixerPanelDock = kOrder[0];
      sStep = 0;
      sSetAt = frameId;
      return false;
   }
   if (sStep < 5)
   {
      if (frameId < sSetAt + 3)
         return false;
      const int dock = kOrder[sStep];
      const ImVec2 mn = gMixerPanelRectMin, mx = gMixerPanelRectMax;
      const ImVec2 disp = ImGui::GetIO().DisplaySize;
      const float w = mx.x - mn.x, h = mx.y - mn.y;
      printf("  dock %s: card %.0f,%.0f .. %.0f,%.0f in a %.0f x %.0f window\n", kNames[dock], mn.x, mn.y, mx.x, mx.y, disp.x, disp.y);
      // The rect is the card, inset from the dock's outer edge by the panel gap and short of it by the grip.
      switch (dock)
      {
         case 0: Check("bottom sits on the window's bottom edge", mx.y > disp.y - 30.0f && h > 200.0f && h <= 300.0f && w > disp.x * 0.9f); break;
         case 3: Check("top sits under the menu bar", mn.y < 90.0f && h > 200.0f && h <= 300.0f && w > disp.x * 0.9f); break;
         case 1: Check("right is a column right of the canvas", w > 420.0f && w <= 520.0f && mx.x > disp.x - 80.0f && mn.x > gGraphScreenTL.x + 100.0f); break;
         case 2: Check("left is a column on the window's left edge", w > 420.0f && w <= 520.0f && mn.x < 30.0f && gGraphScreenTL.x >= mx.x - 1.0f); break;
         default: Check("floating draws its own window", w > 200.0f && h > 100.0f); break;
      }
      if (++sStep < 5)
      {
         gMixerPanelDock = kOrder[sStep];
         sSetAt = frameId;
      }
      return false;
   }
   CheckRoutingAndPersist();
   gMixerPanelOpen = false;
   gMixerPanelDock = 0;
   printf("MIXERPANELTEST %s\n", sOk ? "PASS" : "FAIL");
   sStep = 6;
   return true;
}
}

// Mixer panel self-test (INFINITE_MIXERPANELTEST). A3: five docks drawn where they belong, open/dock/size
// survive save and load, Mixer Audio lets only page terminals reach the device. A4: a page of 8, 16 and
// 32 strips laid out in every dock, page tabs, group strips, "Create from Audio Outs". B1: a reverb send
// renders through the real topology, and a return into its own sender is refused with a notice.
// Plan: internal/plans/mixer-panel.
#include "app/AppShared.h"
#include "nodes/AudioNodes.h"
#include "nodes/AudioEffectNode.h"
#include "audio/AudioEngine.h"
#include "core/Notices.h"
#include "core/Transport.h"
#include "audio/ParamMailbox.h"
#include "core/Patch.h"
#include "nodes/OutputNode.h"
#include "arrange/ArrangeModel.h"
#include "audio/dsp/MixerMeter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <thread>
#include <fstream>
#include <vector>
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
      gMixerPanelDock = 3;
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
            gMixerPanelOpen && gMixerPanelDock == 3 && gMixerPanelWidth == 640.0f && gMixerPanelHeight == 333.0f);
      MixerPanelDockReadKey("panelMixerDock", "99");
      MixerPanelDockReadKey("panelMixerW", "3");
      Check("a bad dock or size is clamped on load", gMixerPanelDock == 0 && gMixerPanelWidth == kMixerPanelMinWidth);

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
      Check("one page: the Mixer-fed Audio Out", MixerPanelPageCount() == 1);
      const int page2 = MixerPanelAddPage();
      Check("+ page adds a Mixer + Audio Out pair", page2 > 0 && MixerPanelPageCount() == 2);
      NewPatch();

      // D13: two direct Audio Outs become one console feeding the first; the second is unplugged.
      const int c = SpawnNode("Delay", "AudioEffects", 0.0f, 0.0f)->index;
      const int d = SpawnNode("Delay", "AudioEffects", 0.0f, 300.0f)->index;
      const int o1 = SpawnNode("Audio Out", "Utility", 600.0f, 0.0f)->index;
      const int o2 = SpawnNode("Audio Out", "Utility", 600.0f, 300.0f)->index;
      wired = ConnectNodes(c, 0, o1, 0, err) && ConnectNodes(d, 0, o2, 0, err);
      Check("D13 patch wires up", wired && MixerPanelPageCount() == 0);
      Check("Create from Audio Outs makes one page", MixerPanelCreateFromAudioOuts() && MixerPanelPageCount() == 1);
      auto* unplugged = dynamic_cast<AudioOutputNode*>(FindNodeByIndex(o2)->node.get());
      Check("the other Audio Out is unplugged, so nothing plays twice", unplugged && !unplugged->input.IsConnected());
      gAudioMode = AudioMode::Canvas;
      RebuildAudioTopology();
      Check("both sources now reach the device once, through the console", gAudioTopologyTerminalCount == 1);

      // A5: "Open in Mixer panel" on a nested Mixer brings up its page with the group open.
      NewPatch();
      const int topA = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int outA = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      const int topB = SpawnNode("Mixer", "Utility", 400.0f, 400.0f)->index;
      const int outB = SpawnNode("Audio Out", "Utility", 700.0f, 400.0f)->index;
      const int grpB = SpawnNode("Mixer", "Utility", 200.0f, 400.0f)->index;
      const int loose = SpawnNode("Mixer", "Utility", 200.0f, 800.0f)->index;
      wired = ConnectNodes(topA, 0, outA, 0, err) && ConnectNodes(topB, 0, outB, 0, err) && ConnectNodes(grpB, 0, topB, 2, err);
      Check("A5 patch wires up: two pages, a group on the second", wired && MixerPanelPageCount() == 2);
      gMixerPanelOpen = false;
      MixerPanelSelectPage(0);
      Check("Open in Mixer panel on a group finds its page", MixerPanelShowMixer(grpB) && gMixerPanelOpen);
      Check("the page shown is the group's console, folded open", MixerPanelCurrentPageTop() == topB && MixerPanelGroupOpen(grpB));
      Check("a Mixer on no page still opens the panel", !MixerPanelShowMixer(loose) && gMixerPanelOpen);
      gMixerPanelOpen = false;
      NewPatch();
   }

   // A steady 220 Hz sine, off the canvas: the topology walks cables, so a Mixer slot can hold it.
   class ToneAudio : public AudioNode
   {
   public:
      void ProcessBlock(const AudioBuffer* const*, int, AudioBuffer& out) override
      {
         for (int i = 0; i < out.numFrames; i++)
         {
            const float v = 0.5f * std::sin(mPhase);
            mPhase += 2.0f * 3.14159265f * mHz / 48000.0f;
            for (int ch = 0; ch < out.numChannels; ch++)
               out.channels[ch][i] = v;
         }
         mPhase = std::fmod(mPhase, 2.0f * 3.14159265f);
      }
      float mHz = 220.0f;
      float mPhase = 0.0f;
   };
   class ToneNode : public INode, public IAudioSource
   {
   public:
      unsigned int GetOutputTexture() override { return 0; }
      int GetOutputWidth() const override { return 0; }
      int GetOutputHeight() const override { return 0; }
      void CookIfNeeded(int) override {}
      void VisitParams(ParamVisitor&) override {}
      AudioNode* GetAudioNode() override { return &mAudio; }
      void Set(float hz) { mAudio.mHz = hz; mAudio.mPhase = 0.0f; }
      void Rewind() { mAudio.mPhase = 0.0f; }
   private:
      ToneAudio mAudio;
   };

   // Peak of the console's output over `blocks` offline blocks, after cooking the canvas once.
   float RenderPeak(int blocks)
   {
      static int sFrame = 100000;
      ++sFrame;
      for (GraphNode& gn : gNodes)
         gn.node->CookIfNeeded(sFrame);
      RebuildAudioTopology();
      static float sL[512], sR[512];
      float* ch[2] = { sL, sR };
      AudioBuffer buf { ch, 2, 512 };
      float peak = 0.0f;
      for (int b = 0; b < blocks; b++)
      {
         AudioEngine::Instance().ProcessOffline(buf);
         for (int i = 0; i < 512; i++)
            peak = std::max(peak, std::max(std::fabs(sL[i]), std::fabs(sR[i])));
      }
      return peak;
   }

   void CheckSends()
   {
      // B1: one strip on a console wired straight to its Audio Out; "New send FX: Reverb" on it.
      static ToneNode tone;
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      std::string err;
      const bool wired = ConnectNodes(top, 0, out, 0, err);
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      m->numChannels = 1;
      m->inputs[0].Connect(&tone);
      Check("B1 patch wires up: one console, one strip", wired && MixerPanelPageCount() == 1);
      Check("a Mixer without sends has one output pin", m->OutputCount() == 1);

      const int fx = MixerPanelNewSendFx(top, 0, "Reverb");
      const int newTop = MixerPanelCurrentPageTop();
      auto* nt = newTop >= 0 ? dynamic_cast<MixerNode*>(FindNodeByIndex(newTop)->node.get()) : nullptr;
      GraphNode* fxGn = fx >= 0 ? FindNodeByIndex(fx) : nullptr;
      auto* rev = fxGn ? dynamic_cast<AudioEffectNode*>(fxGn->node.get()) : nullptr;
      Check("New send FX spawns a Reverb", rev != nullptr);
      Check("G1: the console is wrapped, its old top now a group under a new top",
            nt != nullptr && newTop != top && MixerPanelPageCount() == 1 &&
            nt->inputs[0].GetSource() == m && MixerPanelGroupOpen(top));
      Check("the sender gains a \"send 1\" pin", m->OutputCount() == 2 && std::string(m->OutputLabel(1)) == "send 1");
      Check("send 1 feeds the Reverb, all wet",
            rev && rev->input.GetSource() == m && rev->input.GetOutputSlot() == 1 && rev->mix == 1.0f);
      Check("the return lands on the new top", nt && nt->numChannels == 2 && nt->inputs[1].GetSource() == rev);

      // Render: dry strip muted on the top, so only the return can reach the output.
      const bool deviceWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
      if (deviceWasRunning)
         AudioEngine::Instance().Stop();
      if (nt)
         nt->mute[0] = true;
      m->sendDb[0][0] = MixerNode::kSendOffDb;
      const float silent = RenderPeak(20);
      m->sendDb[0][0] = 0.0f;
      const float wet = RenderPeak(60);
      char label[160];
      snprintf(label, sizeof(label), "send off: the return is silent (peak %.5f)", silent);
      Check(label, silent < 1e-4f);
      snprintf(label, sizeof(label), "send at 0 dB: the reverb return renders (peak %.3f)", wet);
      Check(label, wet > 0.01f);
      m->gainDb[0] = -60.0f;
      m->sendPre[0] = true;
      const float pre = RenderPeak(20);
      snprintf(label, sizeof(label), "pre-fader send keeps sending with the fader down (peak %.3f)", pre);
      Check(label, pre > 0.01f);
      if (deviceWasRunning)
      {
         std::string startErr;
         StartAudioEngine(startErr);
      }

      // The return into its own sender is a loop: refused, with a notice on screen.
      for (const Notices::Notice& n : Notices::Active())
         Notices::DismissId(n.id);
      const bool refused = !ConnectNodes(fx, 0, top, 0, err);
      bool noticed = false;
      for (const Notices::Notice& n : Notices::Active())
         noticed = noticed || n.key == "audio.cycle";
      Check("a return wired back into its sender is refused with a notice", refused && noticed);
      for (const Notices::Notice& n : Notices::Active())
         Notices::DismissId(n.id);
      m->inputs[0].Disconnect();
      NewPatch();
   }

   // B2: the slot's inserts are the single-input chain above it; add, remove, reorder, bypass are one undo step each.
   void CheckInserts()
   {
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      const int src = SpawnNode("Gain", "AudioEffects", 0.0f, 0.0f)->index; // unplugged input: a source, not an insert
      std::string err;
      const bool wired = ConnectNodes(top, 0, out, 0, err) && ConnectNodes(src, 0, top, 0, err);
      Check("B2 patch wires up: a source on slot 1", wired && MixerPanelInsertChain(top, 0).empty());

      auto mixer = [&]() { return dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get()); };
      auto nodeOf = [](int i) { GraphNode* g = FindNodeByIndex(i); return g ? g->node.get() : nullptr; };
      auto inOf = [&](int i) { INode* n = nodeOf(i); AudioCable* c = n ? n->AudioInputSlot(0) : nullptr; return c ? c->GetSource() : nullptr; };
      // Wiring matches `want`: src -> want... -> the slot.
      auto wiredAs = [&](const std::vector<int>& want) {
         if (MixerPanelInsertChain(top, 0) != want)
            return false;
         INode* prev = nodeOf(src);
         for (int i : want)
         {
            if (inOf(i) != prev)
               return false;
            prev = nodeOf(i);
         }
         return mixer()->inputs[0].GetSource() == prev;
      };

      size_t undo0 = gUndoStack.size();
      const int eq = MixerPanelInsertAdd(top, 0, "EQ");
      Check("add: EQ spliced between the source and the slot", eq >= 0 && wiredAs({ eq }));
      Check("add is one undo step", gUndoStack.size() == undo0 + 1);
      const int dly = MixerPanelInsertAdd(top, 0, "Delay");
      const int rev = MixerPanelInsertAdd(top, 0, "Reverb");
      Check("adds append nearest the fader", dly >= 0 && rev >= 0 && wiredAs({ eq, dly, rev }));

      undo0 = gUndoStack.size();
      Check("reorder: Reverb to the top of the chain", MixerPanelInsertMove(top, 0, 2, 0) && wiredAs({ rev, eq, dly }));
      Check("reorder is one undo step", gUndoStack.size() == undo0 + 1);
      Undo();
      Check("undo puts the old order back", wiredAs({ eq, dly, rev }));

      undo0 = gUndoStack.size();
      Check("remove: Delay leaves, its neighbours join", MixerPanelInsertRemove(top, 0, 1) && wiredAs({ eq, rev }) && nodeOf(dly) == nullptr);
      Check("remove is one undo step", gUndoStack.size() == undo0 + 1);
      Undo();
      Check("undo brings the Delay back in place", wiredAs({ eq, dly, rev }));

      undo0 = gUndoStack.size();
      const bool bypassed = MixerPanelInsertBypass(top, 0, 0) && nodeOf(eq)->bypassed;
      Check("bypass toggles the chip's node, one undo step", bypassed && gUndoStack.size() == undo0 + 1 && wiredAs({ eq, dly, rev }));

      // G4: the Reverb also feeds slot 2, so the chain is shared and refuses inline edits.
      for (const Notices::Notice& n : Notices::Active())
         Notices::DismissId(n.id);
      mixer()->inputs[1].Connect(nodeOf(rev));
      bool shared = false;
      MixerPanelInsertChain(top, 0, &shared);
      undo0 = gUndoStack.size();
      const bool refused = MixerPanelInsertAdd(top, 0, "EQ") < 0 && !MixerPanelInsertMove(top, 0, 0, 1);
      bool noticed = false;
      for (const Notices::Notice& n : Notices::Active())
         noticed = noticed || n.key == "mixer.insert";
      Check("fan-out makes the strip shared: edits refused with a notice, no undo step",
            shared && refused && noticed && gUndoStack.size() == undo0);
      for (const Notices::Notice& n : Notices::Active())
         Notices::DismissId(n.id);
      NewPatch();
   }

   // Hash of `blocks` offline blocks after `warm` warm-up blocks (the ramps settle first).
   unsigned long long RenderHash(int warm, int blocks, float* peakOut = nullptr)
   {
      static int sFrame = 200000;
      ++sFrame;
      for (GraphNode& gn : gNodes)
         gn.node->CookIfNeeded(sFrame);
      RebuildAudioTopology();
      static float sL[512], sR[512];
      float* ch[2] = { sL, sR };
      AudioBuffer buf { ch, 2, 512 };
      unsigned long long hash = 1469598103934665603ull;
      float peak = 0.0f;
      for (int b = 0; b < warm + blocks; b++)
      {
         AudioEngine::Instance().ProcessOffline(buf);
         if (b < warm)
            continue;
         for (int i = 0; i < 512; i++)
            for (int c = 0; c < 2; c++)
            {
               unsigned int bits;
               std::memcpy(&bits, &ch[c][i], 4);
               hash = (hash ^ bits) * 1099511628211ull;
               peak = std::max(peak, std::fabs(ch[c][i]));
            }
      }
      if (peakOut)
         *peakOut = peak;
      return hash;
   }

   void ClearNotices()
   {
      for (const Notices::Notice& n : Notices::Active())
         Notices::DismissId(n.id);
   }
   bool Noticed(const char* key)
   {
      for (const Notices::Notice& n : Notices::Active())
         if (n.key == key)
            return true;
      return false;
   }

   // B3: group, ungroup and reorder are one undo step each, and group -> ungroup gives the same samples back.
   void CheckGroups()
   {
      static ToneNode tones[3];
      NewPatch();
      const int top0 = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      std::string err;
      const bool wired = ConnectNodes(top0, 0, out, 0, err);
      MixerNode* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top0)->node.get());
      m->numChannels = 3;
      static const float kHz[3] = { 220.0f, 330.0f, 550.0f };
      static const float kGain[3] = { -6.0f, -3.0f, -9.0f };
      static const float kPan[3] = { -0.5f, 0.3f, 0.0f };
      for (int i = 0; i < 3; i++)
      {
         m->inputs[i].Connect(&tones[i]);
         m->gainDb[i] = kGain[i];
         m->pan[i] = kPan[i];
      }
      Check("B3 patch wires up: three strips", wired && MixerPanelPageCount() == 1);
      auto rewind = [&]() { for (int i = 0; i < 3; i++) tones[i].Set(kHz[i]); };
      auto topIndex = [&]() { return MixerPanelCurrentPageTop(); };

      const bool deviceWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
      if (deviceWasRunning)
         AudioEngine::Instance().Stop();
      rewind();
      RenderHash(40, 20);
      rewind();
      float peakA = 0.0f;
      const unsigned long long flat = RenderHash(40, 20, &peakA);

      size_t undo0 = gUndoStack.size();
      const int grp = MixerPanelGroupStrips(topIndex(), { 0, 1 });
      m = dynamic_cast<MixerNode*>(FindNodeByIndex(topIndex())->node.get());
      GraphNode* gGn = grp >= 0 ? FindNodeByIndex(grp) : nullptr;
      MixerNode* g = gGn ? dynamic_cast<MixerNode*>(gGn->node.get()) : nullptr;
      Check("group: two strips move into a new Mixer on the first one's slot",
            g != nullptr && m->numChannels == 2 && m->inputs[0].GetSource() == g && m->inputs[1].GetSource() == &tones[2] &&
            g->numChannels == 2 && g->inputs[0].GetSource() == &tones[0] && g->inputs[1].GetSource() == &tones[1]);
      Check("group: the strips keep their settings, the group strip sits at unity",
            g != nullptr && g->gainDb[0] == kGain[0] && g->gainDb[1] == kGain[1] && g->pan[0] == kPan[0] &&
            m->gainDb[0] == 0.0f && m->pan[0] == 0.0f && m->gainDb[1] == kGain[2]);
      Check("group is one undo step", gUndoStack.size() == undo0 + 1);
      rewind();
      float peakB = 0.0f;
      RenderHash(40, 20, &peakB);
      char label[200];
      snprintf(label, sizeof(label), "the grouped console still sounds (peak %.3f flat, %.3f grouped)", peakA, peakB);
      Check(label, peakA > 0.05f && peakB > 0.05f);

      undo0 = gUndoStack.size();
      const size_t nodes0 = gNodes.size();
      const bool ungrouped = MixerPanelUngroup(topIndex(), 0);
      m = dynamic_cast<MixerNode*>(FindNodeByIndex(topIndex())->node.get());
      bool same = m->numChannels == 3;
      for (int i = 0; i < 3 && same; i++)
         same = m->inputs[i].GetSource() == &tones[i] && m->gainDb[i] == kGain[i] && m->pan[i] == kPan[i];
      Check("ungroup: the strips return to their slots, the group Mixer is gone", ungrouped && same && gNodes.size() == nodes0 - 1);
      Check("ungroup is one undo step", gUndoStack.size() == undo0 + 1);
      rewind();
      const unsigned long long back = RenderHash(40, 20);
      Check("group then ungroup renders bit-identical", back == flat);

      undo0 = gUndoStack.size();
      const bool moved = MixerPanelStripMove(topIndex(), 0, 2);
      Check("move: strip 1 to the end, its settings with it",
            moved && m->inputs[0].GetSource() == &tones[1] && m->inputs[2].GetSource() == &tones[0] &&
            m->gainDb[2] == kGain[0] && m->pan[2] == kPan[0] && m->gainDb[0] == kGain[1]);
      Check("move is one undo step", gUndoStack.size() == undo0 + 1);
      MixerPanelStripMove(topIndex(), 2, 0);
      rewind();
      Check("move there and back renders bit-identical", RenderHash(40, 20) == flat);

      // A strip with a live send can't move into a group: its send bus lives on this Mixer.
      ClearNotices();
      m->sendBuses = 1;
      m->sendDb[0][0] = 0.0f;
      undo0 = gUndoStack.size();
      const bool refused = MixerPanelGroupStrips(topIndex(), { 0, 1 }) < 0;
      Check("a sending strip refuses to group, with a notice and no undo step",
            refused && Noticed("mixer.group") && gUndoStack.size() == undo0);
      ClearNotices();
      if (deviceWasRunning)
      {
         std::string startErr;
         StartAudioEngine(startErr);
      }
      for (int i = 0; i < 3; i++)
         m->inputs[i].Disconnect();
      NewPatch();
   }

   // B4: the meter against the EBU Tech 3341 reference signals, and its gate on the Mixer.
   void CheckMeters()
   {
      const double rate = 48000.0;
      static std::vector<float> l, r;
      auto run = [&](MixerMeter& meter, double hz, double db, double seconds, double phase = 0.0) {
         const int n = (int)(seconds * rate);
         l.resize((size_t)n);
         r.resize((size_t)n);
         const double a = std::pow(10.0, db / 20.0);
         for (int i = 0; i < n; i++)
            l[(size_t)i] = r[(size_t)i] = (float)(a * std::sin(2.0 * 3.14159265358979 * hz * (double)i / rate + phase));
         for (int at = 0; at < n; at += 512)
            meter.Process(l.data() + at, r.data() + at, std::min(512, n - at));
      };
      char label[200];
      {
         static MixerMeter meter;
         meter.Prepare(rate);
         run(meter, 1000.0, -23.0, 20.0);
         snprintf(label, sizeof(label), "1 kHz stereo sine at -23 dBFS reads -23 LUFS (M %.2f, S %.2f, I %.2f)",
                  meter.Momentary(), meter.ShortTerm(), meter.Integrated());
         Check(label, std::fabs(meter.Momentary() + 23.0f) <= 0.1f && std::fabs(meter.ShortTerm() + 23.0f) <= 0.1f &&
                         std::fabs(meter.Integrated() + 23.0f) <= 0.1f);
         snprintf(label, sizeof(label), "its true peak reads -23 dBTP (%.2f)", meter.TruePeakDb());
         Check(label, std::fabs(meter.TruePeakDb() + 23.0f) <= 0.1f);
         run(meter, 1000.0, -40.0, 20.0);
         snprintf(label, sizeof(label), "a quiet passage 17 LU down is gated out of integrated (I %.2f)", meter.Integrated());
         Check(label, std::fabs(meter.Integrated() + 23.0f) <= 0.2f);
         meter.Reset();
         run(meter, 1000.0, -33.0, 5.0);
         snprintf(label, sizeof(label), "reset starts integrated over (I %.2f)", meter.Integrated());
         Check(label, std::fabs(meter.Integrated() + 33.0f) <= 0.1f);
      }
      {
         // A quarter-rate sine sampled 45 degrees off its crests: samples peak 3 dB under the wave.
         static MixerMeter meter;
         meter.Prepare(rate);
         run(meter, rate / 4.0, 0.0, 1.0, 3.14159265358979 / 4.0);
         snprintf(label, sizeof(label), "true peak finds the crest between samples (%.2f dBTP, samples at -3.01)", meter.TruePeakDb());
         Check(label, meter.TruePeakDb() > -0.5f && meter.TruePeakDb() < 0.3f);
      }

      static ToneNode tone;
      tone.Set(1000.0f);
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      std::string err;
      ConnectNodes(top, 0, out, 0, err);
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      m->numChannels = 1;
      m->inputs[0].Connect(&tone);
      const bool deviceWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
      if (deviceWasRunning)
         AudioEngine::Instance().Stop();
      RenderPeak(100);
      RenderPeak(1);
      Check("nobody asks: the master meter does not run", !m->MetersRunning() && m->LoudnessMomentary() <= -99.0f);
      m->RequestMeters();
      RenderPeak(150);
      m->RequestMeters();
      RenderPeak(1);
      snprintf(label, sizeof(label), "the panel asks: the master reads the mix (M %.1f LUFS, TP %.1f dBTP)",
               m->LoudnessMomentary(), m->TruePeakDb());
      Check(label, m->MetersRunning() && m->LoudnessMomentary() > -30.0f && m->LoudnessMomentary() < 0.0f && m->TruePeakDb() > -30.0f);
      RenderPeak(1);
      RenderPeak(1);
      Check("the panel stops asking: the meter stops", !m->MetersRunning());
      if (deviceWasRunning)
      {
         std::string startErr;
         StartAudioEngine(startErr);
      }
      m->inputs[0].Disconnect();
      NewPatch();
   }

   // Block C: a Fast take is sample-exact, names never collide, and Fast refuses what it can't render.
   void CheckRender()
   {
      namespace fs = std::filesystem;
      std::error_code ec;
      const fs::path dirPath = fs::temp_directory_path(ec) / "infinite-mixer-render-test";
      fs::remove_all(dirPath, ec);
      fs::create_directories(dirPath, ec);
      const std::string dir = dirPath.string();

      Check("a free name is used as it is", MixerRenderFileName("mix", dir, "wav") == "mix.wav");
      { std::ofstream f(dir + "/mix.wav"); f << "x"; }
      Check("a taken name gets a number, never an overwrite", MixerRenderFileName("mix", dir, "wav") == "mix-2.wav");
      { std::ofstream f(dir + "/mix-001.wav"); f << "x"; }
      Check("{take} counts past existing files", MixerRenderFileName("mix-{take}", dir, "wav") == "mix-002.wav");

      static ToneNode tone;
      tone.Set(220.0f);
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      std::string err;
      ConnectNodes(top, 0, out, 0, err);
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      auto* o = dynamic_cast<AudioOutputNode*>(FindNodeByIndex(out)->node.get());
      m->numChannels = 1;
      m->inputs[0].Connect(&tone);
      o->recordDirectory = dir;
      const bool deviceWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
      if (deviceWasRunning)
         AudioEngine::Instance().Stop();
      const bool wasPlaying = Transport::Instance().IsPlaying();

      const MixerRenderSettings saved = gMixerRender;
      gMixerRender = MixerRenderSettings();
      gMixerRender.mode = 1;
      gMixerRender.length = 1.0f;
      gMixerRender.tail = 0.5f;
      gMixerRender.name = "take-{take}";
      auto runTake = [&]() {
         if (!MixerRenderStart({ out }))
            return false;
         for (int i = 0; i < 200000 && MixerRenderActive(); i++)
            MixerRenderTick();
         return !MixerRenderActive();
      };
      char label[240];
      const bool ran = runTake();
      const std::vector<MixerRenderResult> first = MixerRenderResults();
      Check("a Fast take runs to its end", ran && first.size() == 1);
      if (first.size() == 1)
      {
         const MixerRenderResult& r = first[0];
         const double rate = r.seconds > 0.0 ? (double)r.frames / r.seconds : 0.0;
         const int64_t want = (int64_t)std::llround(1.5 * rate);
         snprintf(label, sizeof(label), "length + tail is sample-exact: %lld frames at %.0f Hz, %lld bytes",
                  (long long)r.frames, rate, (long long)r.bytes);
         Check(label, rate > 0.0 && r.frames == want && r.bytes >= r.frames * 4 && r.bytes <= r.frames * 4 + 512);
         snprintf(label, sizeof(label), "the result row has the take's numbers (peak %.3f, %.1f LUFS, %.1f dBTP)", r.peak, r.lufs, r.truePeakDb);
         Check(label, r.peak > 0.05f && r.peak <= 1.0f && r.lufs > -40.0f && r.lufs < 0.0f && r.truePeakDb > -30.0f);
         Check("the file is named from the template", r.path == dir + "/take-001.wav");
      }
      Check("the transport is handed back", !Transport::Instance().IsOfflineMode() && Transport::Instance().IsPlaying() == wasPlaying && !o->IsRecording());
      const bool ranAgain = runTake();
      Check("a second take gets the next number", ranAgain && MixerRenderResults().size() == 1 &&
                                                    MixerRenderResults()[0].path == dir + "/take-002.wav");

      // Cancel part-way: no file is left behind.
      gMixerRender.length = 30.0f;
      const bool started = MixerRenderStart({ out });
      MixerRenderTick();
      const bool midway = MixerRenderActive() && MixerRenderProgress() > 0.0f && MixerRenderProgress() < 1.0f;
      MixerRenderCancel();
      Check("cancel stops a Fast take and removes its file",
            started && midway && !MixerRenderActive() && !fs::exists(dirPath / "take-003.wav") && !o->IsRecording() &&
            !Transport::Instance().IsOfflineMode());

      ClearNotices();
      gMixerRender.lengthUnit = 2;
      const bool refused = !MixerRenderStart({ out }) && !MixerRenderActive() && !MixerRenderRefusal().empty();
      Check("Fast without a length is refused with a notice", refused && Noticed("mixer.render"));
      ClearNotices();

      gMixerRender = saved;
      if (deviceWasRunning)
      {
         std::string startErr;
         StartAudioEngine(startErr);
      }
      m->inputs[0].Disconnect();
      NewPatch();
      fs::remove_all(dirPath, ec);
   }

   // D4 master inserts: effects between the top Mixer and its Audio Out still make a page.
   void CheckMasterInserts()
   {
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 900.0f, 0.0f)->index;
      std::string err;
      ConnectNodes(top, 0, out, 0, err);
      auto* o = dynamic_cast<AudioOutputNode*>(FindNodeByIndex(out)->node.get());
      Check("master inserts: a bare console has none", MixerPanelInsertChain(top, kMixerMasterSlot).empty());
      const int fx1 = MixerPanelInsertAdd(top, kMixerMasterSlot, "Gain");
      const int fx2 = MixerPanelInsertAdd(top, kMixerMasterSlot, "Delay");
      const std::vector<int> chain = MixerPanelInsertChain(top, kMixerMasterSlot);
      Check("two master inserts, in signal order", fx1 >= 0 && fx2 >= 0 && chain.size() == 2 && chain[0] == fx1 && chain[1] == fx2);
      Check("the Audio Out now hangs off the last insert", o->input.GetSource() == FindNodeByIndex(fx2)->node.get());
      Check("an Audio Out behind master inserts is still a page", MixerPanelPageCount() == 1 && MixerPanelCurrentPageTop() == top);
      gAudioMode = AudioMode::Mixer;
      RebuildAudioTopology();
      Check("Mixer Audio still routes that page", gAudioTopologyTerminalCount == 1);
      gAudioMode = AudioMode::Canvas;
      Check("reorder master inserts", MixerPanelInsertMove(top, kMixerMasterSlot, 1, 0) &&
                                          MixerPanelInsertChain(top, kMixerMasterSlot).front() == fx2);
      Check("remove both: the console feeds its Audio Out directly again",
            MixerPanelInsertRemove(top, kMixerMasterSlot, 0) && MixerPanelInsertRemove(top, kMixerMasterSlot, 0) &&
            MixerPanelInsertChain(top, kMixerMasterSlot).empty() && o->input.GetSource() == FindNodeByIndex(top)->node.get());

      // A chain hanging from a send pin is a return, not a page.
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      m->sendBuses = 1;
      const int fx = SpawnNode("Gain", "AudioEffects", 600.0f, 300.0f)->index;
      const int out2 = SpawnNode("Audio Out", "Utility", 900.0f, 300.0f)->index;
      const bool wired = ConnectNodes(top, 1, fx, 0, err) && ConnectNodes(fx, 0, out2, 0, err);
      Check("an Audio Out fed from a send pin through an effect is not a page", wired && MixerPanelPageCount() == 1);
      NewPatch();
   }

   // D7: mono + dim act on the device buffer only; ProcessOffline (every render) never runs them.
   void CheckMonitor()
   {
      AudioEngine& eng = AudioEngine::Instance();
      static float sL[256], sR[256];
      float* ch[2] = { sL, sR };
      AudioBuffer buf { ch, 2, 256 };
      auto fill = [&]() {
         for (int i = 0; i < 256; i++)
         {
            sL[i] = 0.5f;
            sR[i] = -0.25f;
         }
      };
      eng.SetMonitor(false, false);
      fill();
      eng.ApplyMonitor(buf);
      Check("monitor off leaves the block untouched", sL[255] == 0.5f && sR[255] == -0.25f);
      eng.SetMonitor(true, true);
      fill();
      eng.ApplyMonitor(buf);
      float step = 0.0f;
      for (int i = 1; i < 256; i++)
         step = std::max(step, std::fabs(sL[i] - sL[i - 1]));
      Check("switching on ramps across the block, no step", step < 0.01f && std::fabs(sL[255] - 0.0125f) < 1e-5f);
      fill();
      eng.ApplyMonitor(buf);
      Check("mono: both sides carry the mid", std::fabs(sL[10] - sR[10]) < 1e-7f && std::fabs(sL[10] - 0.0125f) < 1e-6f);
      Check("dim: -20 dB", std::fabs(sL[100] / 0.125f - 0.1f) < 1e-5f);
      eng.SetMonitor(false, false);
      fill();
      eng.ApplyMonitor(buf);
      fill();
      eng.ApplyMonitor(buf);
      Check("switching off returns to unity", sL[0] == 0.5f && sR[0] == -0.25f);

      // B5: a render with mono + dim on equals the clean render.
      static ToneNode tone;
      tone.Set(220.0f);
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      std::string err;
      ConnectNodes(top, 0, out, 0, err);
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      m->numChannels = 1;
      m->inputs[0].Connect(&tone);
      m->pan[0] = -1.0f;
      const bool deviceWasRunning = eng.SampleRate() > 0.0;
      if (deviceWasRunning)
         eng.Stop();
      tone.Rewind();
      const float clean = RenderPeak(20);
      eng.SetMonitor(true, true);
      tone.Rewind();
      const float monitored = RenderPeak(20);
      eng.SetMonitor(false, false);
      char label[160];
      snprintf(label, sizeof(label), "a render with mono + dim on equals the clean one (%.5f vs %.5f)", monitored, clean);
      Check(label, clean > 0.1f && std::fabs(monitored - clean) < 1e-6f);
      if (deviceWasRunning)
      {
         std::string startErr;
         StartAudioEngine(startErr);
      }
      m->inputs[0].Disconnect();
      NewPatch();
   }

   // B6: snapshots A-H over the whole console, crossfaded, saved in the patch; Alt-clear's solo clear.
   void CheckSnapshots()
   {
      static ToneNode tone;
      tone.Set(220.0f);
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      const int grp = SpawnNode("Mixer", "Utility", 200.0f, 0.0f)->index;
      std::string err;
      const bool wired = ConnectNodes(top, 0, out, 0, err) && ConnectNodes(grp, 0, top, 1, err);
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      auto* g = dynamic_cast<MixerNode*>(FindNodeByIndex(grp)->node.get());
      m->numChannels = 2;
      m->inputs[0].Connect(&tone);
      Check("B6 patch wires up: a console with a group", wired && MixerPanelPageCount() == 1);
      Check("an empty snapshot does not recall", !MixerPanelSnapshotRecall(top, 0));

      m->gainDb[0] = 0.0f; m->pan[0] = 0.0f; m->mute[0] = false; g->gainDb[0] = -6.0f; m->masterDb = 0.0f;
      Check("store A", MixerPanelSnapshotStore(top, 0) && m->SnapshotHas(0) && g->SnapshotHas(0));
      m->gainDb[0] = -30.0f; m->pan[0] = 1.0f; m->phase[0] = true; m->soloSafe[0] = true; g->gainDb[0] = 3.0f; m->masterDb = -10.0f;
      m->sendDb[0][0] = -12.0f;
      Check("store B", MixerPanelSnapshotStore(top, 1));

      // Persist: the snapshot text survives the patch's own param round trip, and garbage is ignored.
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(m, params);
      MixerNode fresh;
      Patch::LoadParams(&fresh, params);
      fresh.SnapshotRecall(1, 0.0f);
      Check("snapshots are saved in the patch", fresh.SnapshotHas(0) && fresh.SnapshotHas(1) && !fresh.SnapshotHas(2) &&
                                                     fresh.gainDb[0] == -30.0f && fresh.pan[0] == 1.0f && fresh.phase[0] &&
                                                     fresh.soloSafe[0] && fresh.masterDb == -10.0f && fresh.sendDb[0][0] == -12.0f);
      // Strip name and colour (the desk's scribble strip) are saved with the Mixer.
      m->chName[1] = "Kick"; m->chColor[1] = 0x3E63DD;
      std::vector<std::pair<std::string, std::string>> nameParams;
      Patch::SaveParams(m, nameParams);
      MixerNode named;
      Patch::LoadParams(&named, nameParams);
      Check("strip name and colour are saved in the patch", named.chName[1] == "Kick" && named.chColor[1] == 0x3E63DD && named.chColor[0] == 0u);
      MixerNode junk;
      Patch::LoadParams(&junk, { { "s snapA", "3 nan x" }, { "s snapfade", "-5" } });
      Check("a damaged snapshot loads as empty; the fade is clamped", !junk.SnapshotHas(0) && junk.snapFadeMs == 0);

      // Recall A during playback with a 200 ms crossfade: no sample-to-sample step above the tone's own.
      const bool deviceWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
      if (deviceWasRunning)
         AudioEngine::Instance().Stop();
      m->snapFadeMs = 200;
      m->phase[0] = false; // a polarity switch flips at the midpoint by design; keep this pass to the faded values
      // Prepare the nodes at a real rate, as a running device or the Fast render does, so fader values ramp.
      const double rateWas = gHeadlessAudioRate;
      if (!(gHeadlessAudioRate > 0.0))
         gHeadlessAudioRate = 48000.0;
      ForceAudioRepare();
      RenderPeak(4);
      Check("recall A starts a crossfade", MixerPanelSnapshotRecall(top, 0) && m->SnapshotFading());
      static float sL[512], sR[512];
      float* ch[2] = { sL, sR };
      AudioBuffer buf { ch, 2, 512 };
      float last = 0.0f, maxStep = 0.0f, midGain = 0.0f;
      bool first = true;
      static int sFrame = 300000;
      // No UI frame stamps here: the mailbox uses its fallback ramp per pushed value, as between frames.
      ParamMailbox::PublishFrame(-1.0, 0.0);
      for (int b = 0; b < 40; b++)
      {
         m->SnapshotAdvance(512.0f * 1000.0f / 48000.0f);
         g->SnapshotAdvance(512.0f * 1000.0f / 48000.0f);
         ++sFrame;
         for (GraphNode& gn : gNodes)
            gn.node->CookIfNeeded(sFrame);
         if (b == 9)
            midGain = m->gainDb[0];
         AudioEngine::Instance().ProcessOffline(buf);
         for (int i = 0; i < 512; i++)
         {
            if (!first)
               maxStep = std::max(maxStep, std::fabs(sL[i] - last));
            last = sL[i];
            first = false;
         }
      }
      char label[200];
      snprintf(label, sizeof(label), "crossfade passes through the middle (gain %.1f dB at ~107 ms)", midGain);
      Check(label, midGain < -5.0f && midGain > -25.0f);
      snprintf(label, sizeof(label), "recall during playback is click-free (largest step %.4f)", maxStep);
      Check(label, maxStep < 0.03f);
      gHeadlessAudioRate = rateWas;
      Check("A is back on every Mixer of the console", !m->SnapshotFading() && m->gainDb[0] == 0.0f && m->pan[0] == 0.0f &&
                                                           !m->phase[0] && !m->soloSafe[0] && g->gainDb[0] == -6.0f &&
                                                           m->masterDb == 0.0f && m->SnapshotLast() == 0);
      m->snapFadeMs = 0;
      MixerPanelSnapshotRecall(top, 1);
      Check("no crossfade: B lands at once", !m->SnapshotFading() && m->gainDb[0] == -30.0f && g->gainDb[0] == 3.0f);
      if (deviceWasRunning)
      {
         std::string startErr;
         StartAudioEngine(startErr);
      }

      m->solo[0] = true;
      g->solo[0] = true;
      Check("a solo anywhere on the console counts", MixerPanelAnySolo(top));
      MixerPanelClearSolos(top);
      Check("Clear solo clears the top and its groups", !MixerPanelAnySolo(top) && !m->solo[0] && !g->solo[0]);
      m->inputs[0].Disconnect();
      NewPatch();
   }

   // Block C extras: All pages as one sum, arrange range, formats, Live, refusals, saved settings.
   void CheckRenderMore()
   {
      namespace fs = std::filesystem;
      std::error_code ec;
      const fs::path dirPath = fs::temp_directory_path(ec) / "infinite-mixer-render-test2";
      fs::remove_all(dirPath, ec);
      fs::create_directories(dirPath, ec);
      const std::string dir = dirPath.string();

      // Settings saved with the workspace.
      const MixerRenderSettings saved = gMixerRender;
      gMixerRender.source = 1; gMixerRender.mode = 1; gMixerRender.lengthUnit = 3; gMixerRender.length = 12.0f;
      gMixerRender.tail = 4.5f; gMixerRender.start = 2; gMixerRender.format = 2; gMixerRender.name = "mix-{date}";
      std::ostringstream ws;
      MixerPanelDockWrite(ws);
      gMixerRender = MixerRenderSettings();
      std::istringstream in(ws.str());
      for (std::string line; std::getline(in, line);)
      {
         const size_t eq = line.find('=');
         if (eq != std::string::npos)
            MixerPanelDockReadKey(line.substr(0, eq), line.substr(eq + 1));
      }
      const MixerRenderSettings& r = gMixerRender;
      Check("render settings are saved with the workspace", r.source == 1 && r.mode == 1 && r.lengthUnit == 3 &&
                                                               r.length == 12.0f && r.tail == 4.5f && r.start == 2 &&
                                                               r.format == 2 && r.name == "mix-{date}");
      MixerPanelDockReadKey("mixerRenderLengthUnit", "9");
      MixerPanelDockReadKey("mixerRenderTail", "nan");
      Check("bad render settings are clamped on load", gMixerRender.lengthUnit == 3 && gMixerRender.tail == 2.0f);

      // Two pages, a tone on each.
      static ToneNode toneA, toneB;
      toneA.Set(220.0f);
      toneB.Set(330.0f);
      NewPatch();
      const int topA = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int outA = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      const int topB = SpawnNode("Mixer", "Utility", 400.0f, 400.0f)->index;
      const int outB = SpawnNode("Audio Out", "Utility", 700.0f, 400.0f)->index;
      std::string err;
      ConnectNodes(topA, 0, outA, 0, err);
      ConnectNodes(topB, 0, outB, 0, err);
      auto* ma = dynamic_cast<MixerNode*>(FindNodeByIndex(topA)->node.get());
      auto* mb = dynamic_cast<MixerNode*>(FindNodeByIndex(topB)->node.get());
      auto* oa = dynamic_cast<AudioOutputNode*>(FindNodeByIndex(outA)->node.get());
      auto* ob = dynamic_cast<AudioOutputNode*>(FindNodeByIndex(outB)->node.get());
      ma->numChannels = mb->numChannels = 1;
      ma->inputs[0].Connect(&toneA);
      mb->inputs[0].Connect(&toneB);
      oa->recordDirectory = ob->recordDirectory = dir;
      const bool deviceWasRunning = AudioEngine::Instance().SampleRate() > 0.0;
      if (deviceWasRunning)
         AudioEngine::Instance().Stop();

      gMixerRender = MixerRenderSettings();
      gMixerRender.mode = 1;
      gMixerRender.length = 1.0f;
      gMixerRender.tail = 0.0f;
      auto run = [&](const std::vector<int>& outs) {
         toneA.Rewind();
         toneB.Rewind();
         if (!MixerRenderStart(outs))
            return false;
         for (int i = 0; i < 200000 && MixerRenderActive(); i++)
            MixerRenderTick();
         return !MixerRenderActive() && MixerRenderResults().size() == 1;
      };
      char label[240];
      gMixerRender.name = "pageA";
      const bool aRan = run({ outA });
      const MixerRenderResult alone = aRan ? MixerRenderResults()[0] : MixerRenderResult();
      gMixerRender.source = 1;
      gMixerRender.name = "summuted";
      mb->mute[0] = true;
      const bool mutedRan = run({ outA, outB });
      const MixerRenderResult mutedSum = mutedRan ? MixerRenderResults()[0] : MixerRenderResult();
      snprintf(label, sizeof(label), "All pages with page B muted equals page A alone (peak %.4f vs %.4f, %lld vs %lld frames)",
               mutedSum.peak, alone.peak, (long long)mutedSum.frames, (long long)alone.frames);
      Check(label, aRan && mutedRan && alone.peak > 0.1f && std::fabs(mutedSum.peak - alone.peak) < 1e-4f &&
                   mutedSum.frames == alone.frames);
      mb->mute[0] = false;
      gMixerRender.name = "sum";
      const bool sumRan = run({ outA, outB });
      const MixerRenderResult sum = sumRan ? MixerRenderResults()[0] : MixerRenderResult();
      snprintf(label, sizeof(label), "All pages writes one file, the sum (peak %.3f vs %.3f, %.1f LUFS vs %.1f alone)",
               sum.peak, alone.peak, sum.lufs, alone.lufs);
      Check(label, sumRan && sum.peak > alone.peak + 0.05f && sum.lufs > alone.lufs + 1.0f &&
                   !fs::exists(dirPath / "sum-2.wav") && !oa->CaptureRing().joinSum.load() && !ob->CaptureRing().joinSum.load());
      gMixerRender.source = 0;

      // Formats.
      struct Fmt { int format; const char* ext; int bytesPerFrame; };
      const Fmt fmts[3] = { { 1, "wav", 6 }, { 2, "flac", 0 }, { 3, "mp3", 0 } };
      for (const Fmt& f : fmts)
      {
         gMixerRender.format = f.format;
         gMixerRender.name = std::string("fmt-") + f.ext;
         const bool ok = run({ outA });
         const MixerRenderResult res = ok ? MixerRenderResults()[0] : MixerRenderResult();
         const bool named = res.path == dir + "/fmt-" + f.ext + "." + f.ext;
         const bool sized = f.bytesPerFrame > 0 ? (res.bytes >= res.frames * f.bytesPerFrame && res.bytes <= res.frames * f.bytesPerFrame + 512)
                                                : (res.bytes > 1000 && res.bytes < res.frames * 4);
         snprintf(label, sizeof(label), "%s take: %lld frames, %lld bytes, peak %.3f", f.format == 1 ? "WAV 24" : f.ext,
                  (long long)res.frames, (long long)res.bytes, res.peak);
         Check(label, ok && named && sized && res.frames == (int64_t)std::llround(res.seconds * 48000.0) && res.peak > 0.1f);
      }
      gMixerRender.format = 0;

      // Arrange range: the loop when it is on.
      const auto savedLoop = gArrange.settings.loop;
      gArrange.settings.loop.enabled = true;
      gArrange.settings.loop.start = Arrange::kTicksPerBar;
      gArrange.settings.loop.end = Arrange::kTicksPerBar * 2;
      gMixerRender.lengthUnit = 3;
      gMixerRender.name = "range";
      double rs = 0.0, rl = 0.0;
      const bool span = MixerRenderArrangeSpan(rs, rl);
      const bool rangeRan = run({ outA });
      const MixerRenderResult rr = rangeRan ? MixerRenderResults()[0] : MixerRenderResult();
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      const double barSec = Arrange::TicksToSeconds(Arrange::kTicksPerBar, bpm);
      snprintf(label, sizeof(label), "arrange range renders the loop: starts %.2f s, %.2f s long, %lld frames", rs, rl, (long long)rr.frames);
      Check(label, span && std::fabs(rs - barSec) < 1e-9 && std::fabs(rl - barSec) < 1e-9 && rangeRan &&
                   rr.frames == (int64_t)std::llround(barSec * 48000.0));
      gArrange.settings.loop = savedLoop;
      gMixerRender.lengthUnit = 0;

      // Fast refuses a hardware source.
      ClearNotices();
      SpawnNode("MIDI CC", "Modulators", 0.0f, 800.0f);
      const bool hwRefused = !MixerRenderStart({ outA }) && !MixerRenderActive() && Noticed("mixer.render");
      Check("Fast refuses a patch with a live source, with a notice", hwRefused);
      ClearNotices();

      // Live: needs the device. Without one it is refused; with one, a short take lands on length + tail.
      gMixerRender.mode = 0;
      gMixerRender.length = 0.5f;
      gMixerRender.tail = 0.25f;
      gMixerRender.name = "live";
      const bool liveRefused = !MixerRenderStart({ outA }) && Noticed("mixer.render");
      Check("Live with audio off is refused with a notice", liveRefused);
      ClearNotices();
      std::string startErr;
      if (StartAudioEngine(startErr))
      {
         const double rate = AudioEngine::Instance().SampleRate();
         const bool started = MixerRenderStart({ outA });
         const auto t0 = std::chrono::steady_clock::now();
         static int sLiveFrame = 400000;
         while (started && MixerRenderActive() &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0)
         {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++sLiveFrame;
            for (GraphNode& gn : gNodes)
               gn.node->CookIfNeeded(sLiveFrame);
            MixerRenderTick();
         }
         const bool done = started && !MixerRenderActive() && MixerRenderResults().size() == 1;
         const MixerRenderResult live = done ? MixerRenderResults()[0] : MixerRenderResult();
         snprintf(label, sizeof(label), "a Live take stops at length + tail: %lld frames at %.0f Hz", (long long)live.frames, rate);
         Check(label, done && live.frames == (int64_t)std::llround(0.5 * rate) + (int64_t)std::llround(0.25 * rate) && live.peak > 0.1f);
         if (!deviceWasRunning)
            AudioEngine::Instance().Stop();
      }
      else
         printf("  [skip] Live take: no audio device (%s)\n", startErr.c_str());

      gMixerRender = saved;
      if (deviceWasRunning && AudioEngine::Instance().SampleRate() <= 0.0)
         StartAudioEngine(startErr);
      ma->inputs[0].Disconnect();
      mb->inputs[0].Disconnect();
      NewPatch();
      fs::remove_all(dirPath, ec);
   }

   // A page whose top Mixer has `strips` slots, one source each, and one group Mixer on slot 0.
   void BuildConsole(int strips)
   {
      NewPatch();
      const int top = SpawnNode("Mixer", "Utility", 400.0f, 0.0f)->index;
      const int out = SpawnNode("Audio Out", "Utility", 700.0f, 0.0f)->index;
      auto* m = dynamic_cast<MixerNode*>(FindNodeByIndex(top)->node.get());
      m->numChannels = strips;
      std::string err;
      bool ok = ConnectNodes(top, 0, out, 0, err);
      const int grp = SpawnNode("Mixer", "Utility", 200.0f, 0.0f)->index;
      ok = ConnectNodes(grp, 0, top, 0, err) && ok;
      for (int i = 1; i < strips; i++)
      {
         const int src = SpawnNode("Gain", "AudioEffects", 0.0f, 120.0f * (float)i)->index;
         ok = ConnectNodes(src, 0, top, i, err) && ok;
      }
      if (!ok)
         Check("console patch wires up: " + err, false);
      MixerPanelSelectPage(0);
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
   // A4: every strip count in every dock. The strip row and the master lie inside the card.
   static const int kCounts[3] = { 8, 16, 32 };
   static int sCase = -1, sCaseAt = 0;
   if (sCase < 0)
   {
      CheckRoutingAndPersist();
      CheckSends();
      CheckInserts();
      CheckGroups();
      CheckMeters();
      CheckRender();
      CheckMasterInserts();
      CheckMonitor();
      CheckSnapshots();
      CheckRenderMore();
      sCase = 0;
      BuildConsole(kCounts[0]);
      gMixerPanelDock = kOrder[0];
      gMixerPanelOpen = true;
      sCaseAt = frameId;
      return false;
   }
   if (sCase < 15)
   {
      if (frameId < sCaseAt + 3)
         return false;
      const int count = kCounts[sCase / 5];
      const int dock = kOrder[sCase % 5];
      const ImVec2 cmn = gMixerPanelRectMin, cmx = gMixerPanelRectMax;
      const bool inside = gMixerStripsMin.x >= cmn.x - 1.0f && gMixerStripsMax.x <= cmx.x + 1.0f &&
                          gMixerStripsMin.y >= cmn.y - 1.0f && gMixerStripsMax.y <= cmx.y + 1.0f &&
                          gMixerMasterMin.x >= cmn.x - 1.0f && gMixerMasterMax.x <= cmx.x + 1.0f &&
                          gMixerMasterMax.y <= cmx.y + 1.0f;
      const bool overlap = gMixerMasterMin.x < gMixerStripsMax.x && gMixerMasterMin.y < gMixerStripsMax.y;
      char label[160];
      snprintf(label, sizeof(label), "%d strips, %s: %d laid out, fader %.0f px, inside the card, master clear of strips",
               count, kNames[dock], gMixerPanelStripCount, gMixerPanelFaderH);
      Check(label, gMixerPanelStripCount == count && gMixerPanelFaderH >= 40.0f && inside && !overlap);
      if (++sCase < 15)
      {
         if (sCase % 5 == 0)
            BuildConsole(kCounts[sCase / 5]);
         gMixerPanelDock = kOrder[sCase % 5];
         sCaseAt = frameId;
      }
      return false;
   }
   // Block C: the render column, on a wide dock and on a tall one, stays inside the card and clear of the strips.
   if (sCase < 17)
   {
      static bool sRenderSet = false;
      if (!sRenderSet)
      {
         sRenderSet = true;
         gMixerRenderOpen = true;
         gMixerPanelDock = 0;
         sCaseAt = frameId;
         return false;
      }
      if (frameId < sCaseAt + 3)
         return false;
      const ImVec2 cmn = gMixerPanelRectMin, cmx = gMixerPanelRectMax;
      const ImVec2 rmn = gMixerRenderMin, rmx = gMixerRenderMax;
      auto apart = [](ImVec2 amn, ImVec2 amx, ImVec2 bmn, ImVec2 bmx) {
         return amx.x <= bmn.x + 0.5f || bmx.x <= amn.x + 0.5f || amx.y <= bmn.y + 0.5f || bmx.y <= amn.y + 0.5f;
      };
      const bool drawn = rmx.x - rmn.x >= 150.0f && rmx.y - rmn.y > 40.0f;
      const bool inside = rmn.x >= cmn.x - 1.0f && rmx.x <= cmx.x + 1.0f && rmn.y >= cmn.y - 1.0f && rmx.y <= cmx.y + 1.0f &&
                          gMixerStripsMax.x <= cmx.x + 1.0f && gMixerMasterMax.x <= cmx.x + 1.0f;
      const bool clear = apart(rmn, rmx, gMixerStripsMin, gMixerStripsMax) && apart(rmn, rmx, gMixerMasterMin, gMixerMasterMax) &&
                         apart(gMixerMasterMin, gMixerMasterMax, gMixerStripsMin, gMixerStripsMax);
      char label[160];
      snprintf(label, sizeof(label), "render column, %s: %.0f px wide, inside the card, clear of strips and master",
               sCase == 15 ? "bottom" : "right", rmx.x - rmn.x);
      Check(label, drawn && inside && clear && gMixerPanelStripCount == 32);
      if (++sCase < 17)
      {
         gMixerPanelDock = 1;
         sCaseAt = frameId;
      }
      return false;
   }
   gMixerRenderOpen = false;
   NewPatch();
   gMixerPanelOpen = false;
   gMixerPanelDock = 0;
   printf("MIXERPANELTEST %s\n", sOk ? "PASS" : "FAIL");
   sStep = 6;
   return true;
}
}

// MIDI Out self-test (docs/plans/midi-out): headless, no hardware, no device.
#include "app/AppShared.h"

#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"
#include "platform/Platform.h"

namespace app
{
// ============================================================ INFINITE_MIDIOUTTEST
//
// Drives the real MidiOutNode audio half with a hand-fed note queue and a fake in place of the
// OS, then asserts the exact byte stream and timestamps. This is the part CI can prove; what the
// bytes do to a real synth is the manual pass on one device per OS (the plan's Tests section).
// Early exit before glfwInit(), like DSPTEST.
namespace
{
   struct Capture
   {
      std::mutex m;
      std::vector<MidiOutSink::Msg> msgs;
      void Add(const MidiOutSink::Msg& x) { std::lock_guard<std::mutex> l(m); msgs.push_back(x); }
      std::vector<MidiOutSink::Msg> Take()
      {
         // The sink's thread polls at 1 ms; give it a few polls to drain.
         std::this_thread::sleep_for(std::chrono::milliseconds(40));
         std::lock_guard<std::mutex> l(m);
         std::vector<MidiOutSink::Msg> out = std::move(msgs);
         msgs.clear();
         return out;
      }
   };

   NoteEvent On(int note, float vel, int frameOffset)
   {
      NoteEvent e;
      e.note = note; e.velocity = vel; e.isNoteOn = true; e.frameOffset = frameOffset;
      return e;
   }
   NoteEvent Off(int note, int frameOffset)
   {
      NoteEvent e;
      e.note = note; e.isNoteOn = false; e.frameOffset = frameOffset;
      return e;
   }

   bool Is(const MidiOutSink::Msg& m, int status, int d1, int d2)
   {
      return m.len == 3 && m.bytes[0] == status && m.bytes[1] == d1 && m.bytes[2] == d2;
   }

   // One block through the node's audio half.
   void Block(AudioNode* an, int frames = 512)
   {
      float ch0[512] = {}, ch1[512] = {};
      float* chans[2] = { ch0, ch1 };
      AudioBuffer out;
      out.channels = chans; out.numChannels = 2; out.numFrames = frames;
      an->ProcessBlock(nullptr, 0, out);
   }
}

bool RunMidiOutTest()
{
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      printf("MIDIOUTTEST %s: %s\n", what, cond ? "OK" : "FAIL");
      if (!cond)
         ok = false;
   };

   Transport::Instance().SetPlaying(false);

   // ---- bytes, channel, velocity, timestamps ----------------------------------------------
   {
      Capture cap;
      NoteEventQueue q;
      MidiOutNode node;
      node.channel = 3;
      node.UseTestSink([&](const MidiOutSink::Msg& m) { cap.Add(m); });
      AudioNode* an = node.AudioNodeForNotePorts();
      an->PrepareToPlay(48000.0, 512);
      an->SetNoteInbox(&q, q.RegisterConsumer());
      node.CookIfNeeded(1);

      q.Push(On(60, 0.5f, 0));
      q.Push(Off(60, 100));
      q.Push(On(64, 1.0f, 240));
      Block(an);
      const auto got = cap.Take();
      check(got.size() == 3, "three messages for on/off/on");
      if (got.size() == 3)
      {
         check(Is(got[0], 0x92, 60, 64), "note on: status 0x90|ch3-1, velocity 0.5 -> 64");
         check(Is(got[1], 0x82, 60, 0), "note off: 0x80|ch");
         check(Is(got[2], 0x92, 64, 127), "velocity 1.0 -> 127");
         const double dt = got[2].at - got[0].at;
         check(std::fabs(dt - 240.0 / 48000.0) < 1e-6, "frame offset 240 @ 48k = 5.000 ms after the first");
      }

      // Retrigger of a sounding pitch closes the old note first.
      q.Push(On(70, 0.8f, 0));
      q.Push(On(70, 0.8f, 10));
      Block(an);
      const auto re = cap.Take();
      check(re.size() == 3 && Is(re[0], 0x92, 70, 102) && Is(re[1], 0x82, 70, 0) && Is(re[2], 0x92, 70, 102),
            "retrigger: on, off, on (no stacked voice)");
      q.Push(Off(70, 0));
      Block(an);
      cap.Take();

      // A channel change mid-note: the note-off still goes to the channel the note-on used.
      q.Push(On(72, 0.5f, 0));
      Block(an);
      cap.Take();
      node.channel = 9;
      node.CookIfNeeded(2);
      q.Push(Off(72, 0));
      Block(an);
      const auto ch = cap.Take();
      check(ch.size() == 1 && Is(ch[0], 0x82, 72, 0), "note-off follows the on's channel after a channel change");

      // Panic: CC123 + CC120 on all 16 channels.
      node.Panic();
      Block(an);
      const auto pan = cap.Take();
      bool panicOk = pan.size() == 32;
      for (int c = 0; panicOk && c < 16; c++)
         panicOk = Is(pan[c * 2], 0xB0 | c, 123, 0) && Is(pan[c * 2 + 1], 0xB0 | c, 120, 0);
      check(panicOk, "panic: CC123 + CC120 x 16 channels");

      // Transport stop releases held notes (D9).
      Transport::Instance().SetPlaying(true);
      Block(an); // observe "playing"
      q.Push(On(65, 0.5f, 0));
      Block(an);
      cap.Take();
      Transport::Instance().SetPlaying(false);
      Block(an);
      const auto stop = cap.Take();
      check(stop.size() == 1 && Is(stop[0], 0x88, 65, 0), "transport stop sends note-off for held note");
   }

   // ---- delete mid-note: the node's own destructor sends the note-off (D9) -----------------
   {
      Capture cap;
      NoteEventQueue q;
      {
         MidiOutNode node;
         node.channel = 2;
         node.UseTestSink([&](const MidiOutSink::Msg& m) { cap.Add(m); });
         AudioNode* an = node.AudioNodeForNotePorts();
         an->PrepareToPlay(48000.0, 512);
         an->SetNoteInbox(&q, q.RegisterConsumer());
         node.CookIfNeeded(1); // pushes the channel to the audio half
         q.Push(On(48, 1.0f, 0));
         Block(an);
         cap.Take();
         // node destroyed here, mid-note
      }
      const auto last = cap.Take();
      check(last.size() == 1 && Is(last[0], 0x81, 48, 0), "delete mid-note emits the note-off");
   }

   // ---- phase 2: offset, CC rows, clock ---------------------------------------------------
   {
      Capture cap;
      NoteEventQueue q;
      MidiOutNode node;
      node.channel = 1;
      node.UseTestSink([&](const MidiOutSink::Msg& m) { cap.Add(m); });
      AudioNode* an = node.AudioNodeForNotePorts();
      an->PrepareToPlay(48000.0, 512);
      an->SetNoteInbox(&q, q.RegisterConsumer());

      // Offset shifts timestamps by exactly offsetMs (D7), and only timestamps.
      node.offsetMs = 0.0f;
      node.CookIfNeeded(1);
      q.Push(On(60, 0.5f, 0));
      const double t0 = Platform::MidiOutNowSeconds();
      Block(an);
      const auto a = cap.Take();
      node.offsetMs = 20.0f;
      node.CookIfNeeded(2);
      q.Push(Off(60, 0));
      const double t1 = Platform::MidiOutNowSeconds();
      Block(an);
      const auto b = cap.Take();
      check(a.size() == 1 && b.size() == 1 && Is(b[0], 0x80, 60, 0), "offset leaves the bytes alone");
      // Measured against the wall clock taken just before each block, since the blocks run apart.
      check(a.size() == 1 && b.size() == 1 && std::fabs((a[0].at - t0)) < 0.005 &&
               std::fabs((b[0].at - t1) - 0.020) < 0.005,
            "offset +20 ms puts the timestamp 20 ms after the block's own start");
      node.offsetMs = 0.0f;

      // CC rows: nothing while off; sent once on enable; only on change; modulated value follows.
      node.ccNum[1] = 74;
      node.ccVal[1] = 0.5f;
      node.CookIfNeeded(3);
      Block(an);
      check(cap.Take().empty(), "CC row off sends nothing");
      node.ccOn[1] = true;
      node.CookIfNeeded(4);
      Block(an);
      const auto c1 = cap.Take();
      check(c1.size() == 1 && Is(c1[0], 0xB0, 74, 64), "CC row on: sends cc74 = round(0.5*127) = 64");
      node.CookIfNeeded(5);
      Block(an);
      check(cap.Take().empty(), "unchanged value is not resent");
      node.ccVal[1] = 0.5f + 0.001f; // still rounds to 64
      node.CookIfNeeded(6);
      Block(an);
      check(cap.Take().empty(), "a change under one MIDI step is not sent");
      node.ccVal[1] = 1.0f;
      node.CookIfNeeded(7);
      Block(an);
      const auto c2 = cap.Take();
      check(c2.size() == 1 && Is(c2[0], 0xB0, 74, 127), "modulated value change sends the new step");
      node.ccOn[1] = false;
      node.CookIfNeeded(8);

      // Clock: 120 BPM, 4 bars = 384 pulses, Start first, Stop last.
      Transport& tr = Transport::Instance();
      tr.NotifyAudioEngineStarted(48000.0);
      tr.SetTempo(120.0f);
      tr.Rewind();
      node.clock = true;
      node.CookIfNeeded(9);
      tr.SetPlaying(true);
      const int blocks = 750; // 750 * 512 frames = 16.0 beats at 120 BPM, 48 kHz
      std::vector<MidiOutSink::Msg> all;
      for (int i = 0; i < blocks; i++)
      {
         tr.AdvanceAudioClock(512);
         Block(an);
         if ((i % 50) == 49)
         {
            const auto part = cap.Take();
            all.insert(all.end(), part.begin(), part.end());
         }
      }
      tr.SetPlaying(false);
      Block(an);
      {
         const auto part = cap.Take();
         all.insert(all.end(), part.begin(), part.end());
      }
      int pulses = 0, starts = 0, stops = 0;
      bool startFirst = !all.empty() && all.front().len == 1 && all.front().bytes[0] == 0xFA;
      bool stopLast = !all.empty() && all.back().len == 1 && all.back().bytes[0] == 0xFC;
      bool pulsesOrdered = true;
      double lastPulseAt = -1.0;
      for (const auto& m : all)
      {
         if (m.len != 1) continue;
         if (m.bytes[0] == 0xF8) { pulses++; if (m.at < lastPulseAt - 1e-9) pulsesOrdered = false; lastPulseAt = m.at; }
         if (m.bytes[0] == 0xFA) starts++;
         if (m.bytes[0] == 0xFC) stops++;
      }
      check(pulses == 384, "clock: 120 BPM x 4 bars = exactly 384 pulses");
      check(starts == 1 && startFirst, "clock: one Start, and it comes first");
      check(stops == 1 && stopLast, "clock: one Stop, and it comes last");
      check(pulsesOrdered, "clock: pulse timestamps never go backwards");

      // Locate: Song Position Pointer then Continue when starting mid-song.
      tr.SeekBeats(8.0);
      tr.SetPlaying(true);
      tr.AdvanceAudioClock(512);
      Block(an);
      const auto mid = cap.Take();
      bool spp = false, cont = false;
      for (size_t i = 0; i + 1 < mid.size(); i++)
         if (mid[i].len == 3 && mid[i].bytes[0] == 0xF2 && mid[i].bytes[1] == 32 && mid[i].bytes[2] == 0 &&
             mid[i + 1].len == 1 && mid[i + 1].bytes[0] == 0xFB)
            spp = cont = true; // 8 beats = 32 sixteenths
      check(spp && cont, "start mid-song: Song Position 32 then Continue");
      tr.SetPlaying(false);
      node.clock = false;
      tr.Rewind();
   }

   // ---- param round trip ------------------------------------------------------------------
   {
      MidiOutNode a;
      a.device = "Test Synth";
      a.channel = 11;
      struct Rec : ParamVisitor
      {
         std::string dev; int ch = 0;
         void Float(const char*, float&) override {}
         void Int(const char* n, int& v) override { if (std::string(n) == "channel") ch = v; }
         void Bool(const char*, bool&) override {}
         void Text(const char* n, std::string& v) override { if (std::string(n) == "device") dev = v; }
         void Color(const char*, float*) override {}
      } r;
      a.VisitParams(r);
      check(r.dev == "Test Synth" && r.ch == 11, "device name and channel are saved params");
   }

   printf("MIDIOUTTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}
}

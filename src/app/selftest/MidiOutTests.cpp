// MIDI Out self-test (docs/plans/midi-out): headless, no hardware, no device.
#include "app/AppShared.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "core/Transport.h"

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

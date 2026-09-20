// Linux implementation of the Platform facade's MIDI surface, via the ALSA
// sequencer (snd_seq_*) - replacing CoreMIDI (Platform.mm) / WinMM
// (MidiWin.cpp). See docs/plans/linux/phase-02-audio-midi.md section 2.4.
//
// Shape, matched to MidiWin.cpp so the three platforms behave identically:
//   - One ALSA sequencer client with one application (input) port. Every
//     readable port on the system is subscribed to it at MidiStart, except
//     our own, SND_SEQ_CLIENT_SYSTEM's ports (timer/announce, not real
//     instruments) and "Midi Through" - UNLESS no other hardware/software
//     source exists, in which case Through is allowed so headless/CI setups
//     with only `snd-virmidi`/aconnect-style virtual ports still have
//     something to talk to (see MIDINOTESTREAMTEST and the P0 spike notes in
//     docs/plans/linux/validation.md).
//   - Hotplug via the sequencer's own announce port (SND_SEQ_PORT_SYSTEM_
//     ANNOUNCE on client SND_SEQ_CLIENT_SYSTEM): a PORT_START event triggers
//     an on-the-fly (re)subscribe using the same filter as startup; PORT_EXIT
//     just lets the subscription lapse (ALSA drops it when the port closes).
//   - One reader thread. It does NOT poll() only the sequencer's own fds -
//     it also polls a self-pipe, so MidiStop can wake it for a clean exit
//     without a timeout poll. Teardown follows windows-parity SS3.1 /
//     linux-parity SS3.9 exactly: join() is gated on joinable(), never on our
//     own "running" flag.
//   - CC/note values land in the same mutex-guarded table shape as
//     MidiWin.cpp (CcKey/ChannelKey/MidiState), and notes additionally
//     publish onto a lock-free single-writer ring for MidiReadNotesSince.
//     There is exactly one producer (the reader thread above), so this file
//     does not need MidiWin.cpp's ringMutex workaround for its multi-thread-
//     per-device race (SS3.3 of windows-parity) - that bug is structural to
//     WinMM (one callback thread per opened device) and doesn't exist here.
//   - MIDI clock (0xF8) / Start (0xFA) / Stop (0xFC) / Continue (0xFB) come
//     in as their own distinct ALSA event types (SND_SEQ_EVENT_CLOCK/START/
//     STOP/CONTINUE), not as a raw status byte needing the >=0xF0-before-
//     masking care MidiWin.cpp SS3.2 required - the sequencer has already done
//     that dispatch for us. Continue is treated like Start/Stop for the
//     rolling BPM window (it changes the pulse cadence's phase, so averaging
//     across it would be wrong), matching the spirit of MidiWin.cpp's
//     Start/Stop reset without claiming Continue is identical to a hard stop.
//
// MidiDeviceId is derived from the ALSA "client:port" NAME (not the
// client/port numbers, which are reassigned by the kernel on every connect
// and every reboot) via a stable FNV-1a hash - see DeviceIdForName. Names are
// what's stable; two devices that happen to report the exact same name are
// indistinguishable on Linux the same way two identically-named class-
// compliant USB devices are indistinguishable via CoreMIDI on macOS.
//
// Testability (docs/plans/linux/phase-02-audio-midi.md 2.5.2): everything
// that turns a `snd_seq_event_t` into table/ring updates lives in
// HandleSeqEvent(), which takes no ALSA handle and touches only gState/
// gClock - so INFINITE_MIDIPARSETEST (src/main.cpp) can feed it synthetic
// events with no real /dev/snd/seq, and does so on every CI job.

#include "../Platform.h"
#include "../common/MidiCC14.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
   using Clock = std::chrono::steady_clock;

   constexpr size_t kNoteRingCapacity = 4096; // power of two, matches MidiWin.cpp

   struct CcKey
   {
      unsigned int device;
      int channel;
      int controller;
      bool isNote;

      bool operator<(const CcKey& o) const
      {
         if (device != o.device) return device < o.device;
         if (channel != o.channel) return channel < o.channel;
         if (controller != o.controller) return controller < o.controller;
         return isNote < o.isNote;
      }
   };

   struct ChannelKey
   {
      unsigned int device;
      int channel;

      bool operator<(const ChannelKey& o) const
      {
         if (device != o.device) return device < o.device;
         return channel < o.channel;
      }
   };

   struct NoteRingSlot
   {
      uint64_t seq = 0;
      Platform::MidiNoteMessage msg;
   };

   struct MidiState
   {
      std::mutex mutex;
      std::map<CcKey, float> values;
      std::map<CcKey, unsigned int> noteHits;
      std::map<ChannelKey, Platform::MidiLastNote> channelLast;
      MidiCC14::Tracker cc14;
      Platform::MidiCCValue lastTouched;
      bool lastTouchedPending = false;

      // device id (name hash) -> display name, in first-seen order for
      // MidiDeviceSummary's join.
      std::map<unsigned int, std::string> deviceNames;
      std::vector<unsigned int> deviceOrder;

      // Single-writer ring: only the reader thread (real ALSA path) or the
      // INFINITE_MIDIPARSETEST harness (never both at once) ever calls
      // PublishNote, so unlike MidiWin.cpp this needs no producer-side mutex.
      NoteRingSlot ring[kNoteRingCapacity];
      std::atomic<uint64_t> ringWrite{ 0 };
   };

   MidiState gState;

   struct ClockTracker
   {
      std::mutex mutex;
      std::deque<double> pulseMs;
      double bpm = 0.0;
      Clock::time_point lastPulse{};
      bool haveLast = false;

      void Pulse()
      {
         const auto now = Clock::now();
         const double nowMs = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();

         std::lock_guard<std::mutex> lock(mutex);
         if (haveLast)
         {
            const double intervalMs = std::chrono::duration<double, std::milli>(now - lastPulse).count();
            if (intervalMs > 8.0 && intervalMs < 100.0)
            {
               pulseMs.push_back(nowMs);
               while (pulseMs.size() > 96)
                  pulseMs.pop_front();
               RecomputeBpmLocked();
            }
         }
         else
         {
            pulseMs.push_back(nowMs);
         }
         haveLast = true;
         lastPulse = now;
      }

      void Reset()
      {
         std::lock_guard<std::mutex> lock(mutex);
         pulseMs.clear();
         bpm = 0.0;
         haveLast = false;
      }

      void RecomputeBpmLocked()
      {
         if (pulseMs.size() < 4)
            return;
         std::vector<double> gaps;
         gaps.reserve(pulseMs.size());
         for (size_t i = 1; i < pulseMs.size(); i++)
         {
            const double g = pulseMs[i] - pulseMs[i - 1];
            if (g > 1.0)
               gaps.push_back(g);
         }
         if (gaps.empty())
            return;
         std::sort(gaps.begin(), gaps.end());
         const double median = gaps[gaps.size() / 2];
         bpm = 60000.0 / (median * 24.0);
      }
   };

   ClockTracker gClock;

   // FNV-1a 32-bit. Deterministic for a given name string across runs and
   // reboots - the whole point per phase-02-audio-midi.md 2.4 ("MidiDeviceId/
   // MidiDeviceName stable across reboots - use client+port names, not
   // numbers"). 0 is reserved by Platform.h as "unbound", so remap it.
   unsigned int DeviceIdForName(const std::string& name)
   {
      uint32_t h = 2166136261u;
      for (unsigned char c : name)
      {
         h ^= c;
         h *= 16777619u;
      }
      if (h == 0)
         h = 1;
      return h;
   }

   void RegisterDeviceLocked(unsigned int dev, const std::string& name)
   {
      auto it = gState.deviceNames.find(dev);
      if (it == gState.deviceNames.end())
      {
         gState.deviceNames[dev] = name;
         gState.deviceOrder.push_back(dev);
      }
   }

   void PublishNote(unsigned int dev, int channel, int note, float velocity01, bool isNoteOn)
   {
      const uint64_t idx = gState.ringWrite.load(std::memory_order_relaxed);
      NoteRingSlot& slot = gState.ring[idx % kNoteRingCapacity];
      slot.msg.device = dev;
      slot.msg.channel = channel;
      slot.msg.note = note;
      slot.msg.velocity01 = velocity01;
      slot.msg.isNoteOn = isNoteOn;
      slot.seq = idx + 1;
      gState.ringWrite.store(idx + 1, std::memory_order_release);
   }

   // The pure, seq-handle-free event->table/ring translator. `dev` is the
   // already-resolved MidiDeviceId (name hash) for the event's source; the
   // caller (either the real ALSA reader thread or INFINITE_MIDIPARSETEST)
   // is responsible for source->name->id resolution, since a synthetic test
   // event has no real ALSA client/port to look up.
   void HandleSeqEvent(unsigned int dev, const snd_seq_event_t& ev)
   {
      switch (ev.type)
      {
         case SND_SEQ_EVENT_NOTEON:
         {
            const int channel = ev.data.note.channel;
            const int note = ev.data.note.note;
            const int vel = ev.data.note.velocity;
            const bool realOn = vel > 0;
            const float velocity01 = (float)vel / 127.0f;

            {
               std::lock_guard<std::mutex> lock(gState.mutex);
               const CcKey key{ dev, channel, note, true };
               gState.values[key] = velocity01;
               if (realOn)
               {
                  gState.noteHits[key] += 1;

                  Platform::MidiLastNote& last = gState.channelLast[{ dev, channel }];
                  last.note = note;
                  last.velocity01 = velocity01;
                  last.hitSeq += 1;

                  gState.lastTouched.device = dev;
                  gState.lastTouched.channel = channel;
                  gState.lastTouched.controller = note;
                  gState.lastTouched.isNote = true;
                  gState.lastTouched.value01 = velocity01;
                  gState.lastTouchedPending = true;
               }
               else
               {
                  gState.values[{ dev, channel, note, true }] = 0.0f;
               }
            }
            // Velocity-0 Note On is a Note Off by MIDI convention - same
            // handling as MidiWin.cpp's 0x90 case.
            PublishNote(dev, channel, note, velocity01, realOn);
            break;
         }
         case SND_SEQ_EVENT_NOTEOFF:
         {
            const int channel = ev.data.note.channel;
            const int note = ev.data.note.note;
            std::lock_guard<std::mutex> lock(gState.mutex);
            gState.values[{ dev, channel, note, true }] = 0.0f;
            PublishNote(dev, channel, note, 0.0f, false);
            break;
         }
         case SND_SEQ_EVENT_CONTROLLER:
         {
            const int channel = ev.data.control.channel;
            const int controller = ev.data.control.param;
            std::lock_guard<std::mutex> lock(gState.mutex);
            const MidiCC14::Event cc = gState.cc14.OnCC(dev, channel, controller,
                                                        std::clamp((int)ev.data.control.value, 0, 127), MidiCC14::NowMs());
            const float value01 = cc.value01;
            gState.values[{ dev, channel, cc.controller, false }] = value01;
            gState.lastTouched.device = dev;
            gState.lastTouched.channel = channel;
            gState.lastTouched.controller = cc.controller;
            gState.lastTouched.isNote = false;
            gState.lastTouched.value01 = value01;
            gState.lastTouchedPending = true;
            break;
         }
         case SND_SEQ_EVENT_PITCHBEND:
         {
            // Not part of the CC/note table contract on any platform today
            // (Platform.h has no pitch-bend accessor) - accepted and ignored,
            // same as MidiWin.cpp's `default: break;` for anything it
            // doesn't model. Listed as its own case (rather than falling into
            // default) purely so a future reader sees it was considered, per
            // the plan's "distinct event types" requirement for clock only -
            // pitch bend isn't clock, but is worth naming explicitly here.
            break;
         }
         case SND_SEQ_EVENT_CLOCK:
            gClock.Pulse();
            break;
         case SND_SEQ_EVENT_START:
         case SND_SEQ_EVENT_STOP:
         case SND_SEQ_EVENT_CONTINUE:
            gClock.Reset();
            break;
         default:
            break;
      }
   }

   // ---- Real ALSA sequencer plumbing ----------------------------------------

   struct SeqState
   {
      snd_seq_t* handle = nullptr;
      int ourClient = -1;
      int ourPort = -1;
      int wakePipe[2] = { -1, -1 };
      std::thread reader;
      std::atomic<bool> running{ false };

      // client:port name -> our resolved device id, so hotplug re-subscribes
      // don't lose the identity a note in flight was tagged with.
      std::map<std::string, unsigned int> subscribed; // "client:port" -> deviceId
   };

   SeqState gSeq;

   std::string PortKey(int client, int port) { return std::to_string(client) + ":" + std::to_string(port); }

   // Should this readable source port be subscribed? `allowThrough` lets the
   // caller relax the "Midi Through" exclusion when it's the only thing on
   // the system (see EnumerateAndSubscribe).
   bool WantsPort(const snd_seq_addr_t& addr, const std::string& clientName, const std::string& portName,
                  bool allowThrough)
   {
      if (addr.client == gSeq.ourClient)
         return false;
      if (addr.client == SND_SEQ_CLIENT_SYSTEM)
         return false; // timer/announce, not an instrument
      if (!allowThrough && (clientName.find("Midi Through") != std::string::npos ||
                             portName.find("Midi Through") != std::string::npos))
         return false;
      return true;
   }

   void SubscribeLocked(int client, int port, const std::string& name)
   {
      const std::string key = PortKey(client, port);
      if (gSeq.subscribed.count(key))
         return;

      snd_seq_port_subscribe_t* sub = nullptr;
      snd_seq_port_subscribe_alloca(&sub);
      snd_seq_addr_t sender{ (unsigned char)client, (unsigned char)port };
      snd_seq_addr_t dest{ (unsigned char)gSeq.ourClient, (unsigned char)gSeq.ourPort };
      snd_seq_port_subscribe_set_sender(sub, &sender);
      snd_seq_port_subscribe_set_dest(sub, &dest);
      if (snd_seq_subscribe_port(gSeq.handle, sub) == 0)
      {
         const unsigned int dev = DeviceIdForName(name);
         gSeq.subscribed[key] = dev;
         std::lock_guard<std::mutex> lock(gState.mutex);
         RegisterDeviceLocked(dev, name);
      }
   }

   // Full enumeration pass, used both at MidiStart and (for simplicity and
   // correctness over micro-optimising the hotplug path) on every PORT_START
   // announce: re-scanning is cheap next to MIDI traffic rates and sidesteps
   // ordering races between "hardware just appeared" and "is Through still
   // the only option" without needing extra bookkeeping.
   void EnumerateAndSubscribe()
   {
      snd_seq_client_info_t* cinfo;
      snd_seq_port_info_t* pinfo;
      snd_seq_client_info_alloca(&cinfo);
      snd_seq_port_info_alloca(&pinfo);

      struct Candidate
      {
         int client, port;
         std::string clientName, portName, fullName;
      };
      std::vector<Candidate> candidates;
      bool haveNonThrough = false;

      snd_seq_client_info_set_client(cinfo, -1);
      while (snd_seq_query_next_client(gSeq.handle, cinfo) >= 0)
      {
         const int client = snd_seq_client_info_get_client(cinfo);
         const char* clientNameC = snd_seq_client_info_get_name(cinfo);
         const std::string clientName = clientNameC ? clientNameC : "";

         snd_seq_port_info_set_client(pinfo, client);
         snd_seq_port_info_set_port(pinfo, -1);
         while (snd_seq_query_next_port(gSeq.handle, pinfo) >= 0)
         {
            const unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if (!(caps & SND_SEQ_PORT_CAP_READ) || !(caps & SND_SEQ_PORT_CAP_SUBS_READ))
               continue;

            const int port = snd_seq_port_info_get_port(pinfo);
            snd_seq_addr_t addr{ (unsigned char)client, (unsigned char)port };
            if (client == gSeq.ourClient || client == SND_SEQ_CLIENT_SYSTEM)
               continue;

            const char* portNameC = snd_seq_port_info_get_name(pinfo);
            const std::string portName = portNameC ? portNameC : "";
            std::string fullName = clientName;
            if (!portName.empty() && portName != clientName)
               fullName += " " + portName;

            const bool isThrough =
               clientName.find("Midi Through") != std::string::npos ||
               portName.find("Midi Through") != std::string::npos;
            if (!isThrough)
               haveNonThrough = true;

            candidates.push_back({ client, port, clientName, portName, fullName });
            (void)addr;
         }
      }

      for (const auto& c : candidates)
      {
         const bool isThrough =
            c.clientName.find("Midi Through") != std::string::npos ||
            c.portName.find("Midi Through") != std::string::npos;
         if (isThrough && haveNonThrough)
            continue; // real hardware/software sources exist - skip Through
         SubscribeLocked(c.client, c.port, c.fullName);
      }
   }

   void ReaderThreadMain()
   {
      const int numPfds = snd_seq_poll_descriptors_count(gSeq.handle, POLLIN);
      std::vector<struct pollfd> pfds(numPfds + 1);
      snd_seq_poll_descriptors(gSeq.handle, pfds.data(), numPfds, POLLIN);
      pfds[numPfds].fd = gSeq.wakePipe[0];
      pfds[numPfds].events = POLLIN;

      while (gSeq.running.load(std::memory_order_acquire))
      {
         const int n = poll(pfds.data(), pfds.size(), -1);
         if (n <= 0)
            continue;

         if (pfds[numPfds].revents & POLLIN)
            break; // woken for shutdown

         snd_seq_event_t* ev = nullptr;
         while (snd_seq_event_input(gSeq.handle, &ev) >= 0 && ev != nullptr)
         {
            if (ev->source.client == SND_SEQ_CLIENT_SYSTEM)
            {
               // Announce port: hotplug. Re-subscribing on every PORT_START
               // catches both "a new device appeared" and "the only source
               // used to be Midi Through and now it isn't" without separate
               // bookkeeping - see EnumerateAndSubscribe's comment.
               if (ev->type == SND_SEQ_EVENT_PORT_START)
                  EnumerateAndSubscribe();
               continue;
            }

            const std::string key = PortKey(ev->source.client, ev->source.port);
            const auto it = gSeq.subscribed.find(key);
            const unsigned int dev = it != gSeq.subscribed.end() ? it->second : 0;
            if (dev != 0)
               HandleSeqEvent(dev, *ev);
         }
      }
   }
}

namespace Platform
{
   bool MidiStart(std::string& outError)
   {
      outError.clear();
      if (gSeq.running.load(std::memory_order_acquire))
         return true;

      MidiStop(); // close anything stale first

      snd_seq_t* handle = nullptr;
      if (snd_seq_open(&handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
      {
         // Not fatal: same "runs fine with nothing attached" contract as
         // MidiWin.cpp when midiInGetNumDevs() == 0 - no /dev/snd/seq (no
         // ALSA sequencer kernel module) is the documented case on GitHub
         // Actions/OrbStack (docs/plans/linux/validation.md P0 spike).
         gSeq.running.store(true, std::memory_order_release);
         return true;
      }

      snd_seq_set_client_name(handle, "Infinite");
      const int port = snd_seq_create_simple_port(
         handle, "Infinite In",
         SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
         SND_SEQ_PORT_TYPE_APPLICATION);
      if (port < 0)
      {
         snd_seq_close(handle);
         outError = "ALSA sequencer: could not create application port";
         return false;
      }

      // Subscribe to the system announce port so we hear PORT_START/PORT_EXIT.
      snd_seq_connect_from(handle, port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

      if (pipe(gSeq.wakePipe) != 0)
      {
         snd_seq_close(handle);
         outError = "ALSA sequencer: could not create wake pipe";
         return false;
      }

      gSeq.handle = handle;
      gSeq.ourClient = snd_seq_client_id(handle);
      gSeq.ourPort = port;
      gSeq.subscribed.clear();

      EnumerateAndSubscribe();

      gSeq.running.store(true, std::memory_order_release);
      gSeq.reader = std::thread(ReaderThreadMain);
      return true;
   }

   void MidiStop()
   {
      gSeq.running.store(false, std::memory_order_release);
      if (gSeq.wakePipe[1] >= 0)
      {
         const char byte = 0;
         (void)!write(gSeq.wakePipe[1], &byte, 1);
      }
      // See windows-parity SS3.1 / linux-parity SS3.9: joinable() is the only
      // correct predicate, never our own running flag - a joinable thread
      // nobody joins terminates the process at destruction.
      if (gSeq.reader.joinable())
         gSeq.reader.join();

      if (gSeq.wakePipe[0] >= 0) close(gSeq.wakePipe[0]);
      if (gSeq.wakePipe[1] >= 0) close(gSeq.wakePipe[1]);
      gSeq.wakePipe[0] = gSeq.wakePipe[1] = -1;

      if (gSeq.handle != nullptr)
      {
         snd_seq_close(gSeq.handle);
         gSeq.handle = nullptr;
      }
      gSeq.ourClient = gSeq.ourPort = -1;
      gSeq.subscribed.clear();
   }

   bool MidiIsRunning()
   {
      return gSeq.running.load(std::memory_order_acquire);
   }

   std::string MidiDeviceSummary()
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      if (gState.deviceOrder.empty())
         return "no inputs connected";
      std::string summary;
      for (size_t i = 0; i < gState.deviceOrder.size(); i++)
      {
         if (i > 0)
            summary += ", ";
         summary += gState.deviceNames[gState.deviceOrder[i]];
      }
      return summary;
   }

   std::string MidiDeviceName(MidiDeviceId device)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      const auto it = gState.deviceNames.find(device);
      return it == gState.deviceNames.end() ? std::string() : it->second;
   }

   bool MidiRead(MidiDeviceId device, int channel, int controller, bool isNote, float& outValue01)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      if (!isNote)
         controller = gState.cc14.Resolve(device, channel, controller);
      const auto it = gState.values.find({ device, channel, controller, isNote });
      if (it == gState.values.end())
      {
         outValue01 = 0.0f;
         return false;
      }
      outValue01 = it->second;
      return true;
   }

   bool MidiPollLastTouched(MidiCCValue& outLast)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      if (!gState.lastTouchedPending)
         return false;
      outLast = gState.lastTouched;
      gState.lastTouchedPending = false;
      return true;
   }

   unsigned int MidiNoteHitCount(MidiDeviceId device, int channel, int note)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      const auto it = gState.noteHits.find({ device, channel, note, true });
      return it == gState.noteHits.end() ? 0u : it->second;
   }

   bool MidiChannelLastNote(MidiDeviceId device, int channel, MidiLastNote& out)
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      const auto it = gState.channelLast.find({ device, channel });
      if (it == gState.channelLast.end())
         return false;
      out = it->second;
      return true;
   }

   int MidiReadNotesSince(unsigned long long& cursor, MidiNoteMessage* out, int maxCount)
   {
      if (out == nullptr || maxCount <= 0)
         return 0;

      const uint64_t w = gState.ringWrite.load(std::memory_order_acquire);
      if (cursor == 0)
         cursor = w;
      if (w - cursor > kNoteRingCapacity)
         cursor = w - kNoteRingCapacity;

      int count = 0;
      while (cursor < w && count < (uint64_t)maxCount)
      {
         const NoteRingSlot& slot = gState.ring[cursor % kNoteRingCapacity];
         out[count] = slot.msg;
         count++;
         cursor++;
      }
      return count;
   }

   unsigned long long MidiNoteStreamPosition()
   {
      return gState.ringWrite.load(std::memory_order_acquire);
   }

   bool MidiClockIsPresent()
   {
      std::lock_guard<std::mutex> lock(gClock.mutex);
      if (gClock.pulseMs.empty())
         return false;
      const double nowMs = std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
      return nowMs - gClock.pulseMs.back() < 2000.0;
   }

   float MidiClockBpm()
   {
      std::lock_guard<std::mutex> lock(gClock.mutex);
      return (float)gClock.bpm;
   }
}

// ---- INFINITE_MIDIPARSETEST harness hook ------------------------------------
// A small, deliberately non-Platform:: entry point so main.cpp can drive the
// pure parser above with synthetic snd_seq_event_t values, with no real ALSA
// handle required. Declared in a header-free way (extern "C"-ish plain C++
// linkage, internal namespace) because it's test-only plumbing, not part of
// the cross-platform Platform.h contract - macOS/Windows don't need it since
// their own MIDI parsers are exercised by the same INFINITE_MIDIPARSETEST via
// #ifdef __linux__ guarding the *caller* in main.cpp, not by adding this
// function to Platform.h for two platforms that would never implement it.
namespace PlatformLinuxTestHooks
{
   void MidiParseTestResetState()
   {
      std::lock_guard<std::mutex> lock(gState.mutex);
      gState.values.clear();
      gState.cc14.Clear();
      gState.noteHits.clear();
      gState.channelLast.clear();
      gState.lastTouched = Platform::MidiCCValue{};
      gState.lastTouchedPending = false;
      gState.deviceNames.clear();
      gState.deviceOrder.clear();
      gState.ringWrite.store(0, std::memory_order_relaxed);
      gClock.Reset();
   }

   unsigned int MidiParseTestDeviceId(const std::string& name)
   {
      const unsigned int dev = DeviceIdForName(name);
      std::lock_guard<std::mutex> lock(gState.mutex);
      RegisterDeviceLocked(dev, name);
      return dev;
   }

   void MidiParseTestFeedEvent(unsigned int dev, const snd_seq_event_t& ev)
   {
      HandleSeqEvent(dev, ev);
   }
}

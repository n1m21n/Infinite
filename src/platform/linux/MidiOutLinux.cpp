// Linux MIDI output (docs/plans/midi-out). ALSA sequencer, linked already for input.
//
// One sequencer client per open destination, so closing a node tears down exactly its own
// connection. Each handle owns a queue started at open; MidiOutSend turns the steady-clock
// deliver time into a queue-relative real time and lets the kernel schedule it, which is the
// "scheduled tick" of D6. No /dev/snd/seq (GitHub Actions, OrbStack) means an empty device list
// and MidiOutOpen failing with a message, never a crash: the same contract as input.
//
// Signatures come from ../Platform.h.

#include "../Platform.h"

#include <alsa/asoundlib.h>

#include <chrono>
#include <string>
#include <vector>

namespace
{
   struct Destination
   {
      std::string name;
      int client = 0;
      int port = 0;
   };

   // "Client: port", or just the client name when ALSA names both the same (a single-port
   // device such as "Midi Through"). Stable across launches, unlike the numeric ids.
   std::string DisplayName(const char* client, const char* port)
   {
      const std::string c = client ? client : "";
      const std::string p = port ? port : "";
      if (p.empty() || p == c) return c;
      return c + ": " + p;
   }

   std::vector<Destination> ListDestinations(snd_seq_t* seq, int skipClient)
   {
      std::vector<Destination> out;
      snd_seq_client_info_t* cinfo;
      snd_seq_port_info_t* pinfo;
      snd_seq_client_info_alloca(&cinfo);
      snd_seq_port_info_alloca(&pinfo);

      snd_seq_client_info_set_client(cinfo, -1);
      while (snd_seq_query_next_client(seq, cinfo) >= 0)
      {
         const int client = snd_seq_client_info_get_client(cinfo);
         if (client == SND_SEQ_CLIENT_SYSTEM || client == skipClient)
            continue;
         snd_seq_port_info_set_client(pinfo, client);
         snd_seq_port_info_set_port(pinfo, -1);
         while (snd_seq_query_next_port(seq, pinfo) >= 0)
         {
            const unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if (!(caps & SND_SEQ_PORT_CAP_WRITE) || !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE))
               continue;
            if (snd_seq_port_info_get_type(pinfo) & SND_SEQ_PORT_TYPE_MIDI_GENERIC)
            {
               Destination d;
               d.name = DisplayName(snd_seq_client_info_get_name(cinfo), snd_seq_port_info_get_name(pinfo));
               d.client = client;
               d.port = snd_seq_port_info_get_port(pinfo);
               out.push_back(std::move(d));
            }
         }
      }
      return out;
   }
}

namespace Platform
{
   struct MidiOutHandle
   {
      snd_seq_t* seq = nullptr;
      int port = -1;
      int queue = -1;
      int destClient = 0;
      int destPort = 0;
      double openedAt = 0.0; // MidiOutNowSeconds() when the queue started
   };

   double MidiOutNowSeconds()
   {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
   }

   std::vector<std::string> MidiOutListDevices()
   {
      std::vector<std::string> names;
      snd_seq_t* seq = nullptr;
      if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0)
         return names;
      snd_seq_set_client_name(seq, "Infinite Out Scan");
      for (const Destination& d : ListDestinations(seq, snd_seq_client_id(seq)))
         names.push_back(d.name);
      snd_seq_close(seq);
      return names;
   }

   MidiOutHandle* MidiOutOpen(const std::string& name, std::string& outError)
   {
      outError.clear();
      snd_seq_t* seq = nullptr;
      if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0)
      {
         outError = "ALSA sequencer is not available";
         return nullptr;
      }
      snd_seq_set_client_name(seq, "Infinite");

      const Destination* found = nullptr;
      const std::vector<Destination> all = ListDestinations(seq, snd_seq_client_id(seq));
      for (const Destination& d : all)
         if (d.name == name) { found = &d; break; }
      if (!found)
      {
         snd_seq_close(seq);
         outError = "MIDI output \"" + name + "\" is not connected";
         return nullptr;
      }

      const int port = snd_seq_create_simple_port(seq, "Infinite Out",
         SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
      const int queue = port >= 0 ? snd_seq_alloc_queue(seq) : -1;
      if (port < 0 || queue < 0 ||
          snd_seq_connect_to(seq, port, found->client, found->port) < 0)
      {
         if (queue >= 0) snd_seq_free_queue(seq, queue);
         snd_seq_close(seq);
         outError = "could not connect to MIDI output \"" + name + "\"";
         return nullptr;
      }
      snd_seq_start_queue(seq, queue, nullptr);
      snd_seq_drain_output(seq);

      MidiOutHandle* h = new MidiOutHandle();
      h->seq = seq;
      h->port = port;
      h->queue = queue;
      h->destClient = found->client;
      h->destPort = found->port;
      h->openedAt = MidiOutNowSeconds();
      return h;
   }

   void MidiOutClose(MidiOutHandle* handle)
   {
      if (!handle) return;
      if (handle->seq)
      {
         snd_seq_drop_output(handle->seq); // anything still scheduled must not fire after close
         snd_seq_free_queue(handle->seq, handle->queue);
         snd_seq_close(handle->seq);
      }
      delete handle;
   }

   bool MidiOutSend(MidiOutHandle* handle, const unsigned char* bytes, size_t len, double deliverAtSeconds)
   {
      if (!handle || !handle->seq || !bytes || len == 0 || len > 3) return false;

      snd_seq_event_t ev;
      snd_seq_ev_clear(&ev);
      snd_seq_ev_set_source(&ev, handle->port);
      snd_seq_ev_set_subs(&ev);

      const unsigned char status = bytes[0];
      const unsigned char kind = status & 0xF0;
      const int channel = status & 0x0F;
      const int d1 = len > 1 ? bytes[1] : 0;
      const int d2 = len > 2 ? bytes[2] : 0;

      if (status >= 0xF8)
      {
         // System realtime: clock, start, continue, stop. ALSA has typed events for each.
         switch (status)
         {
            case 0xF8: ev.type = SND_SEQ_EVENT_CLOCK; break;
            case 0xFA: ev.type = SND_SEQ_EVENT_START; break;
            case 0xFB: ev.type = SND_SEQ_EVENT_CONTINUE; break;
            case 0xFC: ev.type = SND_SEQ_EVENT_STOP; break;
            default: return false;
         }
      }
      else if (status == 0xF2)
      {
         ev.type = SND_SEQ_EVENT_SONGPOS;
         ev.data.control.value = d1 | (d2 << 7);
      }
      else
      {
         switch (kind)
         {
            case 0x80: snd_seq_ev_set_noteoff(&ev, channel, d1, d2); break;
            case 0x90: snd_seq_ev_set_noteon(&ev, channel, d1, d2); break;
            case 0xB0: snd_seq_ev_set_controller(&ev, channel, d1, d2); break;
            default: return false; // pitch bend, program change etc. are out of scope for v1
         }
      }

      const double queueTime = deliverAtSeconds - handle->openedAt;
      if (queueTime > 0.0)
      {
         snd_seq_real_time_t rt;
         rt.tv_sec = (unsigned)queueTime;
         rt.tv_nsec = (unsigned)((queueTime - (double)rt.tv_sec) * 1.0e9);
         snd_seq_ev_schedule_real(&ev, handle->queue, 0, &rt);
      }
      else
      {
         snd_seq_ev_set_direct(&ev);
      }

      if (snd_seq_event_output(handle->seq, &ev) < 0)
         return false;
      return snd_seq_drain_output(handle->seq) >= 0;
   }

   bool MidiOutVirtualAvailable()
   {
      return true;
   }
}

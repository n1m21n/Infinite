// macOS MIDI output (docs/plans/midi-out). Core MIDI, no new dependency.
//
// Its own file rather than Platform.mm: input keeps its client there, output needs only a
// client, one output port and the destination list. MIDISend takes a host-time
// timestamp, so the sample-accurate scheduling the plan promises (D6) is the OS's job.
//
// Signatures come from Platform.h. Everything here is main-thread or the MIDI-out thread;
// the audio thread never reaches it.

#include "Platform.h"

#import <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>

#include <chrono>
#include <mutex>

namespace
{
   std::mutex gOutMutex;
   MIDIClientRef gOutClient = 0;
   MIDIPortRef gOutPort = 0;
   int gOutRefs = 0;

   std::string CfToUtf8(CFStringRef s)
   {
      if (!s) return std::string();
      char buf[256] = {};
      if (!CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return std::string();
      return buf;
   }

   // Display name when there is one ("IAC Driver Bus 1", "Minilogue XD KBD/KNOB"), else the
   // plain name. Matches what the user sees in Audio MIDI Setup.
   std::string DestinationName(MIDIEndpointRef ep)
   {
      CFStringRef s = nullptr;
      std::string out;
      if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &s) == noErr && s)
      {
         out = CfToUtf8(s);
         CFRelease(s);
      }
      if (out.empty() && MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &s) == noErr && s)
      {
         out = CfToUtf8(s);
         CFRelease(s);
      }
      return out;
   }

   // Core MIDI wants the client alive for as long as any port is open.
   bool AcquireClient(std::string& outError)
   {
      std::lock_guard<std::mutex> lock(gOutMutex);
      if (gOutRefs == 0)
      {
         if (MIDIClientCreate(CFSTR("Infinite Out"), nullptr, nullptr, &gOutClient) != noErr)
         {
            gOutClient = 0;
            outError = "failed to create Core MIDI client";
            return false;
         }
         if (MIDIOutputPortCreate(gOutClient, CFSTR("Infinite Output"), &gOutPort) != noErr)
         {
            MIDIClientDispose(gOutClient);
            gOutClient = 0;
            gOutPort = 0;
            outError = "failed to create Core MIDI output port";
            return false;
         }
      }
      gOutRefs++;
      return true;
   }

   void ReleaseClient()
   {
      std::lock_guard<std::mutex> lock(gOutMutex);
      if (gOutRefs > 0 && --gOutRefs == 0)
      {
         MIDIPortDispose(gOutPort);
         MIDIClientDispose(gOutClient);
         gOutPort = 0;
         gOutClient = 0;
      }
   }

   MIDITimeStamp HostTicksAfter(double seconds)
   {
      static mach_timebase_info_data_t tb = {};
      if (tb.denom == 0) mach_timebase_info(&tb);
      const double nanos = seconds * 1.0e9;
      return mach_absolute_time() + (MIDITimeStamp)(nanos * (double)tb.denom / (double)tb.numer);
   }
}

namespace Platform
{
   struct MidiOutHandle
   {
      MIDIEndpointRef destination = 0;
   };

   double MidiOutNowSeconds()
   {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
   }

   std::vector<std::string> MidiOutListDevices()
   {
      std::vector<std::string> names;
      const ItemCount count = MIDIGetNumberOfDestinations();
      for (ItemCount i = 0; i < count; i++)
      {
         const std::string name = DestinationName(MIDIGetDestination(i));
         if (!name.empty()) names.push_back(name);
      }
      return names;
   }

   MidiOutHandle* MidiOutOpen(const std::string& name, std::string& outError)
   {
      outError.clear();
      MIDIEndpointRef found = 0;
      const ItemCount count = MIDIGetNumberOfDestinations();
      for (ItemCount i = 0; i < count && !found; i++)
      {
         MIDIEndpointRef ep = MIDIGetDestination(i);
         if (DestinationName(ep) == name) found = ep;
      }
      if (!found)
      {
         outError = "MIDI output \"" + name + "\" is not connected";
         return nullptr;
      }
      if (!AcquireClient(outError)) return nullptr;
      MidiOutHandle* h = new MidiOutHandle();
      h->destination = found;
      return h;
   }

   void MidiOutClose(MidiOutHandle* handle)
   {
      if (!handle) return;
      delete handle;
      ReleaseClient();
   }

   bool MidiOutSend(MidiOutHandle* handle, const unsigned char* bytes, size_t len, double deliverAtSeconds)
   {
      if (!handle || !bytes || len == 0 || len > 3) return false;

      // MIDIPacketList on the stack: one packet of at most 3 bytes. Allocation here would
      // be harmless (not the audio thread) but needless.
      Byte buffer[sizeof(MIDIPacketList) + 8] = {};
      MIDIPacketList* list = (MIDIPacketList*)buffer;
      MIDIPacket* pkt = MIDIPacketListInit(list);

      const double ahead = deliverAtSeconds - MidiOutNowSeconds();
      const MIDITimeStamp ts = ahead > 0.0 ? HostTicksAfter(ahead) : 0; // 0 = now
      pkt = MIDIPacketListAdd(list, sizeof(buffer), pkt, ts, len, bytes);
      if (!pkt) return false;

      return MIDISend(gOutPort, handle->destination, list) == noErr;
   }

   bool MidiOutVirtualAvailable()
   {
      return true;
   }
}

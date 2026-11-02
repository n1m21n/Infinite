#include "platform/WatchLink.h"
#include "platform/WatchLinkInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

namespace WatchLink
{
   namespace
   {
      constexpr int kRing = 64;
      constexpr double kSilentSec = 1.0;   // a watch that sends nothing for this long counts as gone

      double NowSec()
      {
         return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      }

      struct Slot
      {
         bool used = false;
         std::string key;
         std::string name;
         bool linked = false;       // set by a packet, cleared by Drop
         bool needsUpdate = false;  // last packet was a newer version
         double lastAt = 0;
         uint32_t drops = 0;
         int lastSeq = -1;
         uint64_t total = 0;        // packets ever stored; ring[total % kRing] is the next write
         WatchPacket::Sample ring[kRing];
      };

      // One lock for the table. It is held for a 44-byte copy, so the main thread never waits on Bluetooth.
      std::mutex gMu;
      Slot gSlots[kMaxWatches];
      Transport::Radio gRadio = Transport::Radio::kOff;
      std::atomic<bool> gDisabled { false };
      int gRefs = 0;

      bool Live(const Slot& s, double now) { return s.used && s.linked && now - s.lastAt < kSilentSec; }

      Slot* ByKey(const std::string& key)
      {
         for (Slot& s : gSlots)
            if (s.used && s.key == key) return &s;
         return nullptr;
      }

      // Slot for a name; empty = first live watch.
      Slot* ByName(const std::string& name, double now)
      {
         for (Slot& s : gSlots)
            if (s.used && (name.empty() ? Live(s, now) : s.name == name)) return &s;
         return nullptr;
      }

      std::string UniqueName(const std::string& want, const Slot* self)
      {
         std::string out = want.empty() ? "Apple Watch" : want;
         for (int n = 2;; n++)
         {
            bool clash = false;
            for (const Slot& s : gSlots)
               clash = clash || (&s != self && s.used && s.name == out);
            if (!clash) return out;
            out = (want.empty() ? "Apple Watch" : want) + " " + std::to_string(n);
         }
      }

      // A new central takes a free slot, else the longest-silent dropped one; a fifth live watch is ignored.
      Slot* Claim(const std::string& key, double now)
      {
         if (Slot* s = ByKey(key)) return s;
         Slot* pick = nullptr;
         for (Slot& s : gSlots)
         {
            if (!s.used) { pick = &s; break; }
            if (!Live(s, now) && (!pick || s.lastAt < pick->lastAt)) pick = &s;
         }
         if (!pick) return nullptr;
         *pick = Slot();
         pick->used = true;
         pick->key = key;
         pick->name = UniqueName("", pick);
         pick->lastAt = now;
         return pick;
      }
   }

   bool Supported() { return Transport::Supported(); }
   void Disable() { gDisabled = true; }

   void Start()
   {
      if (gDisabled || !Transport::Supported()) return;
      if (gRefs++ > 0) return;
      Transport::Begin();
   }

   void Stop()
   {
      if (gRefs == 0) return;
      if (--gRefs > 0) return;
      Transport::End();
      std::lock_guard<std::mutex> lk(gMu);
      for (Slot& s : gSlots)
         if (s.used && s.key.rfind("inject:", 0) != 0) s = Slot();
      gRadio = Transport::Radio::kOff;
   }

   std::vector<WatchInfo> ListWatches()
   {
      std::lock_guard<std::mutex> lk(gMu);
      const double now = NowSec();
      std::vector<WatchInfo> out;
      for (const Slot& s : gSlots)
         if (s.used) out.push_back({ s.name, Live(s, now) });
      return out;
   }

   State Poll(const std::string& name, uint64_t& cursor, std::vector<WatchPacket::Sample>& fresh)
   {
      std::lock_guard<std::mutex> lk(gMu);
      const double now = NowSec();
      Slot* s = ByName(name, now);
      if (!s) { cursor = UINT64_MAX; return State::kUnknown; }   // the next watch to show up starts fresh, no backlog
      if (cursor > s->total) cursor = s->total;                 // another watch or an older run
      if (s->total - cursor > (uint64_t)kRing) cursor = s->total - kRing;
      for (; cursor < s->total; cursor++)
         fresh.push_back(s->ring[cursor % kRing]);
      if (s->needsUpdate) return State::kNeedsUpdate;
      return Live(*s, now) ? State::kConnected : State::kDisconnected;
   }

   bool Read(const std::string& name, WatchPacket::Sample& out)
   {
      std::lock_guard<std::mutex> lk(gMu);
      Slot* s = ByName(name, NowSec());
      if (!s || !Live(*s, NowSec()) || s->total == 0) return false;
      out = s->ring[(s->total - 1) % kRing];
      return true;
   }

   uint32_t Dropped(const std::string& name)
   {
      std::lock_guard<std::mutex> lk(gMu);
      Slot* s = ByName(name, NowSec());
      return s ? s->drops : 0;
   }

   const char* Status()
   {
      static thread_local std::string text;
      std::lock_guard<std::mutex> lk(gMu);
      const double now = NowSec();
      int live = 0;
      bool update = false;
      for (const Slot& s : gSlots)
      {
         live += Live(s, now) ? 1 : 0;
         update = update || (s.used && s.needsUpdate);
      }
      if (gDisabled && live == 0) text = "Apple Watch link is off in headless runs";
      else if (!Transport::Supported() && live == 0) text = "Apple Watch needs macOS for now";
      else if (update) text = "Update Infinite Remote on the watch";
      else if (live > 0) text = live == 1 ? "1 watch connected" : std::to_string(live) + " watches connected";
      else if (gRadio == Transport::Radio::kPoweredOff) text = "Bluetooth is off: turn it on to connect a watch";
      else if (gRadio == Transport::Radio::kUnauthorized) text = "Allow Bluetooth for Infinite in System Settings > Privacy";
      else if (gRefs == 0) text = "Apple Watch link is off";
      else text = "Waiting for a watch: open Infinite Remote on it";
      return text.c_str();
   }

   void InjectPacket(const std::string& name, const uint8_t* data, size_t len)
   {
      const std::string key = "inject:" + name;
      {
         std::lock_guard<std::mutex> lk(gMu);
         Slot* s = Claim(key, NowSec());
         if (s) s->name = UniqueName(name, s);
      }
      Table::Deliver(key, data, len);
   }

   void InjectDisconnect(const std::string& name) { Table::Drop("inject:" + name); }

   namespace Table
   {
      void Deliver(const std::string& key, const uint8_t* data, size_t len)
      {
         WatchPacket::Sample p;
         const WatchPacket::Result r = WatchPacket::Decode(data, len, p);
         std::lock_guard<std::mutex> lk(gMu);
         const double now = NowSec();
         if (r == WatchPacket::Result::kBadMagic || r == WatchPacket::Result::kShort) return;
         Slot* s = Claim(key, now);
         if (!s) return;
         s->lastAt = now;
         s->linked = true;
         if (r == WatchPacket::Result::kNewerVersion) { s->needsUpdate = true; return; }
         s->needsUpdate = false;
         if (s->lastSeq >= 0)
            s->drops += (uint32_t)((p.seq - s->lastSeq - 1) & 0xff);
         s->lastSeq = p.seq;
         s->ring[s->total % kRing] = p;
         s->total++;
      }

      void SetName(const std::string& key, const std::string& name)
      {
         std::lock_guard<std::mutex> lk(gMu);
         if (Slot* s = Claim(key, NowSec()))
            s->name = UniqueName(name.substr(0, 48), s);
      }

      void Drop(const std::string& key)
      {
         std::lock_guard<std::mutex> lk(gMu);
         if (Slot* s = ByKey(key))
         {
            s->linked = false;
            s->lastSeq = -1;
         }
      }

      void SetRadio(Transport::Radio state)
      {
         std::lock_guard<std::mutex> lk(gMu);
         gRadio = state;
      }
   }
}

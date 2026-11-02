#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/WatchPacket.h"

// Bluetooth link to Apple Watches for the Apple Watch node (docs/plans/apple-watch, Block A).
// The Mac is the Bluetooth LE peripheral and the watch (Infinite Remote) the central: the watch writes 44-byte
// packets (core/WatchPacket.h) to the `sample` characteristic at about 50 Hz. Up to four watches at once.
// Reference counted, so two Apple Watch nodes share one radio. Start/Stop/Disable are main thread; Poll/Read/
// ListWatches may be called from any thread (the transport delivers on its own queue).
namespace WatchLink
{
   constexpr int kMaxWatches = 4;

   // Fixed 128-bit UUIDs of the GATT service "Infinite Remote" and its characteristics. The watch app uses the
   // same strings.
   constexpr const char* kServiceUUID = "9A1F0001-6B6D-4F1B-9E3C-1AF1D17E0001";
   constexpr const char* kSampleUUID = "9A1F0002-6B6D-4F1B-9E3C-1AF1D17E0001";    // watch -> Mac, write without response
   constexpr const char* kControlUUID = "9A1F0003-6B6D-4F1B-9E3C-1AF1D17E0001";   // Mac -> watch, notify (the watch subscribes on connect)
   constexpr const char* kNameUUID = "9A1F0004-6B6D-4F1B-9E3C-1AF1D17E0001";      // watch -> Mac, write: UTF-8 watch name, once per connection

   // macOS only for now (Block C adds Windows and Linux). False elsewhere: Start does nothing and Status says so.
   bool Supported();

   // Headless and self-test runs: Start becomes a no-op. A process started from a terminal has the terminal as
   // its privacy-responsible parent, and macOS kills it on the first Bluetooth call (TCC). InjectPacket still works.
   void Disable();

   void Start();
   void Stop();

   struct WatchInfo
   {
      std::string name;
      bool connected = false;
   };
   // Every watch seen since Start, connected or not (a dropped one keeps its row so a saved patch can name it).
   std::vector<WatchInfo> ListWatches();

   enum class State { kUnknown, kConnected, kDisconnected, kNeedsUpdate };

   // New samples since `cursor`, oldest first, appended to `fresh` (nothing is lost between frames: the link keeps
   // the last 64). `name` empty = the first connected watch. Start with a cursor of UINT64_MAX (skip what is already
   // stored); a cursor of a different watch or an older run just resyncs. kNeedsUpdate: the watch speaks a newer packet version than this Infinite.
   State Poll(const std::string& name, uint64_t& cursor, std::vector<WatchPacket::Sample>& fresh);

   // Latest sample of a connected watch. False when unknown, dropped or silent for over a second.
   bool Read(const std::string& name, WatchPacket::Sample& out);

   // Packets the Mac saw missing (seq gaps) since the watch connected.
   uint32_t Dropped(const std::string& name);

   // One short line for the UI ("1 watch connected", "Bluetooth is off ...").
   const char* Status();

   // Tests: deliver a packet as if watch `name` had written it, or drop that watch. Work with Disable().
   void InjectPacket(const std::string& name, const uint8_t* data, size_t len);
   void InjectDisconnect(const std::string& name);
}

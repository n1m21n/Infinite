#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Between WatchLink.cpp (the per-watch table, all platforms) and the radio behind it (WatchLink.mm on macOS,
// WatchLinkStub.cpp elsewhere).
namespace WatchLink
{
   namespace Transport
   {
      enum class Radio { kOff, kStarting, kReady, kPoweredOff, kUnauthorized, kUnsupported };

      // Implemented per platform. Begin/End are main thread.
      bool Supported();
      void Begin();
      void End();
   }

   namespace Table
   {
      // Called from the radio's own thread. `key` identifies the central (its Bluetooth identifier).
      void Deliver(const std::string& key, const uint8_t* data, size_t len);
      void SetName(const std::string& key, const std::string& name);
      void Drop(const std::string& key);
      void SetRadio(Transport::Radio state);
   }
}

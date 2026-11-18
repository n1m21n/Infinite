#pragma once

#include <string>

#include "core/OscMessage.h"

// One UDP socket + one receive thread per port, shared by every OSC Receive
// node listening there (a plain bind() per node made the second node on a
// port silently deaf). Packets (messages or #bundles) are decoded once and
// each message is handed to every subscriber; subscribers filter by address.
class OscPortListener
{
public:
   virtual ~OscPortListener() = default;
   // Called on the hub's receive thread.
   virtual void OnOscMessage(const OscMessage::Message& msg) = 0;
};

namespace OscPortHub
{
   // Returns "" on success, else a short error (e.g. port busy).
   std::string Subscribe(int port, OscPortListener* l);
   // Blocks until the listener is guaranteed not to be called any more.
   void Unsubscribe(int port, OscPortListener* l);
}

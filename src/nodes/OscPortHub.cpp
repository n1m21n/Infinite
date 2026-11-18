#include "OscPortHub.h"

#include "platform/NetCompat.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
   struct Hub
   {
      int fd = -1;
      std::thread thread;
      std::atomic<bool> stop{ false };
      std::mutex mutex; // guards listeners; held while dispatching
      std::vector<OscPortListener*> listeners;
   };

   std::mutex gMapMutex;
   std::map<int, std::unique_ptr<Hub>> gHubs;
}

namespace OscPortHub
{
   std::string Subscribe(int port, OscPortListener* l)
   {
      std::lock_guard<std::mutex> g(gMapMutex);
      auto it = gHubs.find(port);
      if (it != gHubs.end())
      {
         std::lock_guard<std::mutex> hl(it->second->mutex);
         it->second->listeners.push_back(l);
         return "";
      }
      if (port < 1 || port > 65535)
         return "bad port";

      int fd = socket(AF_INET, SOCK_DGRAM, 0);
      if (fd < 0)
         return "socket() failed";
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(static_cast<uint16_t>(port));
      addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // loopback only, same policy as RemoteControl
      if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
      {
         NetCompat::NetClose(fd);
         return "port " + std::to_string(port) + " is busy";
      }

      auto hub = std::make_unique<Hub>();
      hub->fd = fd;
      hub->listeners.push_back(l);
      Hub* h = hub.get();
      h->thread = std::thread([h, fd]() {
         std::vector<uint8_t> buffer(65536); // max UDP datagram
         std::vector<OscMessage::Message> msgs;
         for (;;)
         {
            NetSsize n = NetCompat::NetRecv(fd, buffer.data(), buffer.size(), 0);
            if (h->stop.load())
               break;
            if (n <= 0)
               continue;
            msgs.clear();
            if (!OscMessage::DecodePacket(buffer.data(), static_cast<size_t>(n), msgs))
               continue;
            std::lock_guard<std::mutex> hl(h->mutex);
            for (const OscMessage::Message& m : msgs)
               for (OscPortListener* ln : h->listeners)
                  ln->OnOscMessage(m);
         }
      });
      gHubs[port] = std::move(hub);
      return "";
   }

   void Unsubscribe(int port, OscPortListener* l)
   {
      std::unique_ptr<Hub> dead;
      {
         std::lock_guard<std::mutex> g(gMapMutex);
         auto it = gHubs.find(port);
         if (it == gHubs.end())
            return;
         Hub* h = it->second.get();
         {
            std::lock_guard<std::mutex> hl(h->mutex);
            h->listeners.erase(std::remove(h->listeners.begin(), h->listeners.end(), l), h->listeners.end());
            if (!h->listeners.empty())
               return;
         }
         dead = std::move(it->second);
         gHubs.erase(it);
      }
      dead->stop = true;
      shutdown(dead->fd, SHUT_RDWR);
      NetCompat::NetClose(dead->fd);
      if (dead->thread.joinable())
         dead->thread.join();
   }
}

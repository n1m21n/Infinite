#include "OscControl.h"

#include "platform/NetCompat.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace OscControl
{
   namespace
   {
      std::mutex gQueueMutex;
      std::deque<Request> gQueue;
      std::thread gThread;
      std::atomic<bool> gStop{ false };
      int gSocket = -1;
      int gPort = -1;

      // Far above anything a controller sends per frame; a flood past it is
      // dropped rather than growing the queue without bound.
      constexpr size_t kMaxQueued = 4096;

      void ReceiveLoop(int fd)
      {
         uint8_t buffer[4096];
         while (!gStop.load())
         {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            const NetSsize n = NetCompat::NetRecvFrom(fd, buffer, sizeof(buffer), 0,
                                                      reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n <= 0)
               continue; // timeout (the stop flag is checked every 200 ms) or a bad packet
            std::vector<OscMessage::PacketMessage> msgs;
            if (!OscMessage::DecodePacket(buffer, static_cast<size_t>(n), msgs))
               continue;
            std::lock_guard<std::mutex> lock(gQueueMutex);
            for (auto& m : msgs)
            {
               if (gQueue.size() >= kMaxQueued)
                  break;
               Request req;
               req.address = std::move(m.address);
               req.args = std::move(m.args);
               req.from.ip = from.sin_addr.s_addr;
               req.from.port = from.sin_port;
               gQueue.push_back(std::move(req));
            }
         }
      }
   }

   bool Start(int port)
   {
      if (gSocket >= 0)
         return true;
      if (port <= 0 || port > 65535)
         return false;
      const int fd = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
      if (fd < 0)
         return false;
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(static_cast<uint16_t>(port));
      addr.sin_addr.s_addr = inet_addr("127.0.0.1");
      if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
      {
         NetCompat::NetClose(fd);
         return false;
      }
      // Not shutdown() to unblock the thread: on an unconnected UDP socket it
      // does not wake recvfrom on every platform. A short timeout does.
      NetCompat::NetSetRecvTimeoutMs(fd, 200);
      gSocket = fd;
      gPort = port;
      gStop = false;
      gThread = std::thread(ReceiveLoop, fd);
      return true;
   }

   void Stop()
   {
      if (gSocket < 0)
         return;
      gStop = true;
      if (gThread.joinable())
         gThread.join();
      NetCompat::NetClose(gSocket);
      gSocket = -1;
      gPort = -1;
      std::lock_guard<std::mutex> lock(gQueueMutex);
      gQueue.clear();
   }

   int BoundPort() { return gPort; }

   void DrainPending(const Handler& handler)
   {
      std::deque<Request> batch;
      {
         std::lock_guard<std::mutex> lock(gQueueMutex);
         batch.swap(gQueue);
      }
      for (const Request& req : batch)
         handler(req);
   }

   void Send(const Peer& to, const std::string& address, const std::vector<OscMessage::Arg>& args)
   {
      if (gSocket < 0)
         return;
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = to.port;
      addr.sin_addr.s_addr = to.ip;
      const std::vector<uint8_t> packet = OscMessage::EncodeMany(address, args);
      // From the server socket, so the reply's source port is the one the
      // client sent to - what TouchOSC-style "reply to sender" expects.
      NetCompat::NetSendTo(gSocket, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&addr),
                           sizeof(addr));
   }

   bool Client::Open(int serverPort)
   {
      mFd = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
      if (mFd < 0)
         return false;
      sockaddr_in me{};
      me.sin_family = AF_INET;
      me.sin_port = 0;
      me.sin_addr.s_addr = inet_addr("127.0.0.1");
      if (bind(mFd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0)
         return false;
      NetCompat::NetSetRecvTimeoutMs(mFd, 5);
      mServerPort = serverPort;
      return true;
   }

   Client::~Client()
   {
      if (mFd >= 0)
         NetCompat::NetClose(mFd);
   }

   void Client::Send(const std::string& address, const std::vector<OscMessage::Arg>& args)
   {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_port = htons(static_cast<uint16_t>(mServerPort));
      to.sin_addr.s_addr = inet_addr("127.0.0.1");
      const std::vector<uint8_t> packet = OscMessage::EncodeMany(address, args);
      NetCompat::NetSendTo(mFd, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
   }

   bool Client::Receive(std::string& outAddress, std::vector<OscMessage::Arg>& outArgs)
   {
      uint8_t buffer[4096];
      const NetSsize n = NetCompat::NetRecv(mFd, buffer, sizeof(buffer), 0);
      return n > 0 && OscMessage::DecodeMany(buffer, static_cast<size_t>(n), outAddress, outArgs);
   }
}

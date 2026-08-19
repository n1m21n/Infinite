#include "OscNodes.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>

#include "Transport.h"
#include "core/OscMessage.h"

// ---------------------------------------------------------------------------
// OscReceiveNode
// ---------------------------------------------------------------------------

OscReceiveNode::OscReceiveNode()
{
   mAddressFilter = address;
   RestartListenerIfNeeded();
}

OscReceiveNode::~OscReceiveNode()
{
   StopListener();
}

void OscReceiveNode::StopListener()
{
   if (mSocket < 0)
      return;
   mStop = true;
   // Unblocks the thread's recvfrom() the same way closing a socket already
   // does elsewhere in this codebase (RemoteControl's accept loop) - there is
   // no portable "cancel this blocking syscall" short of shutting the fd down.
   shutdown(mSocket, SHUT_RDWR);
   close(mSocket);
   mSocket = -1;
   if (mThread.joinable())
      mThread.join();
   mStop = false;
   mBoundPort = -1;
}

void OscReceiveNode::RestartListenerIfNeeded()
{
   if (port == mBoundPort)
      return;
   StopListener();

   int fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (fd < 0)
      return;

   sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_port = htons(static_cast<uint16_t>(port));
   addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // loopback only, same policy as RemoteControl

   if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
   {
      close(fd);
      return;
   }

   mSocket = fd;
   mBoundPort = port;

   mThread = std::thread([this, fd]() {
      uint8_t buffer[1024];
      for (;;)
      {
         ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
         if (mStop.load())
            break;
         if (n <= 0)
            continue;

         std::string addr;
         OscMessage::Arg arg;
         if (!OscMessage::Decode(buffer, static_cast<size_t>(n), addr, arg))
            continue;

         std::string filter;
         {
            std::lock_guard<std::mutex> lock(mFilterMutex);
            filter = mAddressFilter;
         }
         if (addr != filter)
            continue;

         float value = 0.0f;
         switch (arg.type)
         {
            case OscMessage::ArgType::Float:  value = arg.f; break;
            case OscMessage::ArgType::Int:    value = static_cast<float>(arg.i); break;
            case OscMessage::ArgType::String: continue; // no numeric value to report
         }
         mLastValue.store(value);
      }
   });
}

void OscReceiveNode::VisitParams(ParamVisitor& v)
{
   v.Int("port", port);
   v.Text("address", address);
   v.Float("low", low);
   v.Float("high", high);
}

float OscReceiveNode::Value01()
{
   // Live param edits (DrawOscReceiveParams) write straight into port/address
   // via ModSliderInt/InputText - there is no VisitParams pass in that path
   // (VisitParams only runs for save/load/undo/copy-paste; see ParamVisitor's
   // header comment) - so both a port and an address change have to be
   // noticed here instead. Value01() is exactly the one thing guaranteed to
   // run every frame this node is on canvas, since the editor draws every
   // modulator's value meter unconditionally (see the DrawModulatorMeter
   // call site).
   if (address != mLastAddress)
   {
      mLastAddress = address;
      std::lock_guard<std::mutex> lock(mFilterMutex);
      mAddressFilter = address;
   }
   if (port != mBoundPort)
      RestartListenerIfNeeded();

   const float raw = mLastValue.load();
   const float span = high - low;
   if (std::fabs(span) < 1e-9f)
      return 0.0f;
   const float v01 = (raw - low) / span;
   return v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
}

// ---------------------------------------------------------------------------
// OscSendNode
// ---------------------------------------------------------------------------

void OscSendNode::VisitParams(ParamVisitor& v)
{
   v.Text("host", host);
   v.Int("port", port);
   v.Text("address", address);
   v.Float("epsilon", epsilon);
   v.Float("intervalMs", intervalMs);
}

void OscSendNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (input == nullptr)
      return;

   const float value = input->Value01();
   const double now = Transport::Instance().Seconds();
   const bool changedEnough = mLastSent < 0.0f || std::fabs(value - mLastSent) >= epsilon;
   const bool intervalElapsed =
      mLastSentSeconds < 0.0 || (now - mLastSentSeconds) * 1000.0 >= static_cast<double>(intervalMs);
   if (!changedEnough && !intervalElapsed)
      return;

   // UDP send is connectionless and effectively non-blocking at this scale -
   // fine to fire directly from the main-thread tick rather than routing
   // through a background thread the way the receive side needs to.
   int fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (fd >= 0)
   {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(static_cast<uint16_t>(port));
      addr.sin_addr.s_addr = inet_addr(host.c_str());

      std::vector<uint8_t> packet = OscMessage::EncodeFloat(address, value);
      sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
      close(fd);
   }

   mLastSent = value;
   mLastSentSeconds = now;
}

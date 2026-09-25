#include "OscNodes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "Transport.h"
#include "core/OscMessage.h"
#include "platform/SocketCompat.h"

// ---------------------------------------------------------------------------
// OscHub: one listener thread per UDP port, shared by every node on it.
// ---------------------------------------------------------------------------
namespace
{
   uint32_t ReadU32(const uint8_t* p)
   {
      return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
   }

   // Length of an OSC string including its NUL padding, or 0 if it runs off
   // the end of the buffer.
   size_t PaddedStringLength(const uint8_t* p, size_t len)
   {
      size_t n = 0;
      while (n < len && p[n] != 0)
         n++;
      if (n >= len)
         return 0;
      return (n + 4) & ~(size_t)3;
   }

   struct Listener
   {
      int port = 0;
      int refs = 0;
      InfiniteSocket fd = kInfiniteInvalidSocket;
      std::thread thread;
      std::atomic<bool> stop { false };
      std::mutex mutex;
      std::map<std::string, std::vector<float>> values;
      std::string lastAddress;
      uint64_t serial = 0;

      void Store(const std::string& addr, std::vector<float>&& args)
      {
         std::lock_guard<std::mutex> lock(mutex);
         values[addr] = std::move(args);
         lastAddress = addr;
         serial++;
      }

      void ParseMessage(const uint8_t* p, size_t len)
      {
         const size_t addrLen = PaddedStringLength(p, len);
         if (addrLen == 0 || p[0] != '/')
            return;
         const std::string addr(reinterpret_cast<const char*>(p));
         std::vector<float> args;
         if (addrLen >= len || p[addrLen] != ',')
         {
            Store(addr, std::move(args)); // no type tags: an address-only "bang"
            return;
         }
         const uint8_t* tags = p + addrLen;
         const size_t tagLen = PaddedStringLength(tags, len - addrLen);
         if (tagLen == 0)
            return;
         const uint8_t* data = tags + tagLen;
         const uint8_t* end = p + len;
         for (const uint8_t* t = tags + 1; *t != 0; t++)
         {
            switch (*t)
            {
               case 'f':
               {
                  if (data + 4 > end) return;
                  const uint32_t u = ReadU32(data);
                  float f;
                  std::memcpy(&f, &u, 4);
                  args.push_back(f);
                  data += 4;
                  break;
               }
               case 'i':
               {
                  if (data + 4 > end) return;
                  args.push_back((float)(int32_t)ReadU32(data));
                  data += 4;
                  break;
               }
               case 'd':
               {
                  if (data + 8 > end) return;
                  const uint64_t u = ((uint64_t)ReadU32(data) << 32) | ReadU32(data + 4);
                  double d;
                  std::memcpy(&d, &u, 8);
                  args.push_back((float)d);
                  data += 8;
                  break;
               }
               case 'h':
               {
                  if (data + 8 > end) return;
                  const uint64_t u = ((uint64_t)ReadU32(data) << 32) | ReadU32(data + 4);
                  args.push_back((float)(int64_t)u);
                  data += 8;
                  break;
               }
               case 'T': args.push_back(1.0f); break;
               case 'F': args.push_back(0.0f); break;
               case 'N': case 'I': break;
               case 's': case 'S':
               {
                  const size_t n = PaddedStringLength(data, (size_t)(end - data));
                  if (n == 0) return;
                  data += n;
                  break;
               }
               case 'b':
               {
                  if (data + 4 > end) return;
                  const uint32_t n = ReadU32(data);
                  data += 4 + (((size_t)n + 3) & ~(size_t)3);
                  if (data > end) return;
                  break;
               }
               default:
                  return; // unknown tag: sizes unknown, stop here
            }
         }
         Store(addr, std::move(args));
      }

      void ParsePacket(const uint8_t* p, size_t len, int depth)
      {
         if (len < 4 || depth > 8)
            return;
         if (len >= 16 && std::memcmp(p, "#bundle", 8) == 0)
         {
            size_t off = 16; // "#bundle\0" + 8-byte time tag
            while (off + 4 <= len)
            {
               const uint32_t n = ReadU32(p + off);
               off += 4;
               if (n == 0 || off + n > len)
                  return;
               ParsePacket(p + off, n, depth + 1);
               off += n;
            }
            return;
         }
         ParseMessage(p, len);
      }

      bool Start()
      {
         if (!InfiniteSocketsReady())
            return false;
         InfiniteSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
         if (s == kInfiniteInvalidSocket)
            return false;
         sockaddr_in addr{};
         addr.sin_family = AF_INET;
         addr.sin_port = htons(static_cast<uint16_t>(port));
         addr.sin_addr.s_addr = htonl(INADDR_ANY); // LAN + loopback
         if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
         {
            InfiniteCloseSocket(s);
            return false;
         }
         fd = s;
         thread = std::thread([this]() {
            std::vector<uint8_t> buffer(65536);
            while (!stop.load())
            {
               const int n = recv(fd, reinterpret_cast<char*>(buffer.data()), (int)buffer.size(), 0);
               if (stop.load())
                  break;
               if (n <= 0)
                  continue;
               ParsePacket(buffer.data(), (size_t)n, 0);
            }
         });
         return true;
      }

      void Stop()
      {
         stop = true;
         if (fd != kInfiniteInvalidSocket)
         {
            InfiniteShutdownSocket(fd);
            InfiniteCloseSocket(fd);
            fd = kInfiniteInvalidSocket;
         }
         if (thread.joinable())
            thread.join();
      }
   };

   std::mutex gHubMutex;
   std::map<int, std::unique_ptr<Listener>>& Listeners()
   {
      static std::map<int, std::unique_ptr<Listener>> listeners;
      return listeners;
   }
}

namespace OscHub
{
   void Acquire(int port)
   {
      if (port <= 0 || port > 65535)
         return;
      std::lock_guard<std::mutex> lock(gHubMutex);
      auto& map = Listeners();
      auto it = map.find(port);
      if (it != map.end())
      {
         it->second->refs++;
         return;
      }
      auto l = std::make_unique<Listener>();
      l->port = port;
      l->refs = 1;
      l->Start(); // on failure the entry stays (refcounted) and reports closed
      map[port] = std::move(l);
   }

   void Release(int port)
   {
      std::unique_ptr<Listener> dead;
      {
         std::lock_guard<std::mutex> lock(gHubMutex);
         auto& map = Listeners();
         auto it = map.find(port);
         if (it == map.end())
            return;
         if (--it->second->refs > 0)
            return;
         dead = std::move(it->second);
         map.erase(it);
      }
      dead->Stop();
   }

   bool Get(int port, const std::string& address, std::vector<float>& outArgs)
   {
      std::lock_guard<std::mutex> lock(gHubMutex);
      auto& map = Listeners();
      auto it = map.find(port);
      if (it == map.end())
         return false;
      std::lock_guard<std::mutex> vlock(it->second->mutex);
      auto v = it->second->values.find(address);
      if (v == it->second->values.end())
         return false;
      outArgs = v->second;
      return true;
   }

   std::string LastAddress(int port, uint64_t* serial)
   {
      std::lock_guard<std::mutex> lock(gHubMutex);
      auto& map = Listeners();
      auto it = map.find(port);
      if (it == map.end())
      {
         if (serial) *serial = 0;
         return {};
      }
      std::lock_guard<std::mutex> vlock(it->second->mutex);
      if (serial) *serial = it->second->serial;
      return it->second->lastAddress;
   }

   bool PortOpen(int port)
   {
      std::lock_guard<std::mutex> lock(gHubMutex);
      auto& map = Listeners();
      auto it = map.find(port);
      return it != map.end() && it->second->fd != kInfiniteInvalidSocket;
   }
}

// ---------------------------------------------------------------------------
// OscReceiveNode
// ---------------------------------------------------------------------------

OscReceiveNode::OscReceiveNode()
{
   SyncPort();
}

OscReceiveNode::~OscReceiveNode()
{
   if (mBoundPort > 0)
      OscHub::Release(mBoundPort);
}

void OscReceiveNode::SyncPort()
{
   if (port == mBoundPort)
      return;
   if (mBoundPort > 0)
      OscHub::Release(mBoundPort);
   mBoundPort = port;
   OscHub::Acquire(mBoundPort);
}

void OscReceiveNode::VisitParams(ParamVisitor& v)
{
   v.Int("port", port);
   v.Text("address", address);
   v.Float("low", low);
   v.Float("high", high);
   v.Int("argIndex", argIndex);
}

float OscReceiveNode::Value01()
{
   // Value01() runs every frame this node is on canvas (its meter), so a
   // live port edit is noticed here.
   SyncPort();
   std::vector<float> args;
   if (OscHub::Get(mBoundPort, address, args) && !args.empty())
      mRaw = args[(size_t)std::clamp(argIndex, 0, (int)args.size() - 1)];
   const float span = high - low;
   if (std::fabs(span) < 1e-9f)
      return 0.0f;
   const float v01 = (mRaw - low) / span;
   return v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
}

// ---------------------------------------------------------------------------
// OscToCvNode
// ---------------------------------------------------------------------------

OscToCvNode::OscToCvNode()
{
   for (int c = 0; c < kChannels; c++)
   {
      mTaps[c].owner = this;
      mTaps[c].index = c;
      address[c] = c == 0 ? "/infinite/param1" : "";
      argIndex[c] = 0;
      inMin[c] = 0.0f;
      inMax[c] = 1.0f;
      invert[c] = false;
      smoothing[c] = 0.0f;
   }
   SyncPort();
}

OscToCvNode::~OscToCvNode()
{
   if (mBoundPort > 0)
      OscHub::Release(mBoundPort);
}

void OscToCvNode::SyncPort()
{
   if (port == mBoundPort)
      return;
   if (mBoundPort > 0)
      OscHub::Release(mBoundPort);
   mBoundPort = port;
   OscHub::Acquire(mBoundPort);
}

const char* OscToCvNode::OutputLabel(int index) const
{
   static const char* kLabels[kChannels] = { "cv1", "cv2", "cv3", "cv4", "cv5", "cv6", "cv7", "cv8" };
   return (index >= 0 && index < kChannels) ? kLabels[index] : "out";
}

IModulator* OscToCvNode::ModulatorOutput(int index)
{
   return (index >= 0 && index < kChannels) ? &mTaps[index] : nullptr;
}

void OscToCvNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   SyncPort();

   // Learn: the first address that arrives after arming goes to the channel.
   uint64_t serial = 0;
   const std::string last = OscHub::LastAddress(mBoundPort, &serial);
   if (learnChannel >= 0 && learnChannel < kChannels)
   {
      if (mLearnSerial == 0)
         mLearnSerial = serial == 0 ? 1 : serial;
      else if (serial != mLearnSerial && !last.empty())
      {
         address[learnChannel] = last;
         argIndex[learnChannel] = 0;
         learnChannel = -1;
         mLearnSerial = 0;
      }
   }
   else
      mLearnSerial = 0;

   std::vector<float> args;
   for (int c = 0; c < kChannels; c++)
   {
      if (address[c].empty() || !OscHub::Get(mBoundPort, address[c], args) || args.empty())
         continue;
      mHasValue[c] = true;
      mRaw[c] = args[(size_t)std::clamp(argIndex[c], 0, (int)args.size() - 1)];
      const float span = inMax[c] - inMin[c];
      float t = std::fabs(span) < 1e-9f ? 0.0f : (mRaw[c] - inMin[c]) / span;
      t = std::clamp(t, 0.0f, 1.0f);
      if (invert[c])
         t = 1.0f - t;
      const float k = std::clamp(smoothing[c], 0.0f, 0.99f);
      mValue[c] = mValue[c] * k + t * (1.0f - k);
   }
}

void OscToCvNode::VisitParams(ParamVisitor& v)
{
   v.Int("port", port);
   char key[32];
   for (int c = 0; c < kChannels; c++)
   {
      snprintf(key, sizeof(key), "address%d", c);
      v.Text(key, address[c]);
      snprintf(key, sizeof(key), "arg%d", c);
      v.Int(key, argIndex[c]);
      snprintf(key, sizeof(key), "inMin%d", c);
      v.Float(key, inMin[c]);
      snprintf(key, sizeof(key), "inMax%d", c);
      v.Float(key, inMax[c]);
      snprintf(key, sizeof(key), "invert%d", c);
      v.Bool(key, invert[c]);
      snprintf(key, sizeof(key), "smooth%d", c);
      v.Float(key, smoothing[c]);
   }
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
   if (!InfiniteSocketsReady())
      return;
   InfiniteSocket fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (fd != kInfiniteInvalidSocket)
   {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(static_cast<uint16_t>(port));
      addr.sin_addr.s_addr = inet_addr(host.c_str());

      std::vector<uint8_t> packet = OscMessage::EncodeFloat(address, value);
      sendto(fd, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
             reinterpret_cast<sockaddr*>(&addr), static_cast<int>(sizeof(addr)));
      InfiniteCloseSocket(fd);
   }

   mLastSent = value;
   mLastSentSeconds = now;
}

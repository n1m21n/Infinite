#include "OscNodes.h"

#include "platform/NetCompat.h"

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
   if (mBoundPort >= 0)
      OscPortHub::Unsubscribe(mBoundPort, this);
   mBoundPort = -1;
}

void OscReceiveNode::RestartListenerIfNeeded()
{
   StopListener();
   std::string err = OscPortHub::Subscribe(port, this);
   {
      std::lock_guard<std::mutex> lock(mFilterMutex);
      mBindError = err;
   }
   if (err.empty())
      mBoundPort = port;
   mRetryCountdown = 120;
}

void OscReceiveNode::OnOscMessage(const OscMessage::Message& msg)
{
   std::string filter;
   int idx;
   {
      std::lock_guard<std::mutex> lock(mFilterMutex);
      filter = mAddressFilter;
      idx = mArgIndexShared;
   }
   const bool learning = mLearning.load();
   if (!learning && !OscMessage::MatchAddress(filter, msg.address))
      return;
   if (idx < 0 || static_cast<size_t>(idx) >= msg.args.size())
      return;
   const OscMessage::Arg& arg = msg.args[static_cast<size_t>(idx)];
   if (arg.type == OscMessage::ArgType::String)
      return; // no numeric value to report
   const float value = arg.type == OscMessage::ArgType::Float ? arg.f : static_cast<float>(arg.i);
   if (learning)
   {
      std::lock_guard<std::mutex> lock(mFilterMutex);
      mLearnedAddress = msg.address;
      mLearning = false;
      return;
   }
   mLastValue.store(value);
}

void OscReceiveNode::VisitParams(ParamVisitor& v)
{
   v.Int("port", port);
   v.Text("address", address);
   v.Int("argIndex", argIndex);
   v.Float("low", low);
   v.Float("high", high);
}

float OscReceiveNode::Value01()
{
   // Live param edits (DrawOscReceiveParams) write straight into port/address
   // (there is no VisitParams pass in that path), so changes are noticed here:
   // Value01() runs every frame this node is on canvas.
   {
      std::lock_guard<std::mutex> lock(mFilterMutex);
      if (!mLearnedAddress.empty())
      {
         address = mLearnedAddress;
         mLearnedAddress.clear();
      }
      if (address != mLastAddress)
      {
         mLastAddress = address;
         mAddressFilter = address;
      }
      mArgIndexShared = argIndex;
   }
   if (port != mBoundPort)
   {
      // Retry a busy port about every 2 s so it recovers once freed.
      if (mBoundPort >= 0 || mRetryCountdown-- <= 0 || mLastBoundTry != port)
      {
         mLastBoundTry = port;
         RestartListenerIfNeeded();
      }
   }

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
      NetCompat::NetSendTo(fd, packet.data(), packet.size(), 0,
                           reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
      NetCompat::NetClose(fd);
   }

   mLastSent = value;
   mLastSentSeconds = now;
}

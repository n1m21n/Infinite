#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "INode.h"
#include "Modulation.h"
#include "OscPortHub.h"

// A UDP OSC listener exposed as a modulator: the last float value received
// at `address` on `port`, remapped through low/high the same way LFONode's
// output is (see ModulatorNodes.h's header comment - modulators report a
// zero output texture and the editor draws a value meter instead of a
// preview).
//
// The socket and receive thread are shared per port (OscPortHub), so several
// nodes can listen on one port, each with its own address.
class OscReceiveNode : public INode, public IModulator, public OscPortListener
{
public:
   static INode* Create() { return new OscReceiveNode(); }
   OscReceiveNode();
   ~OscReceiveNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   int port = 9000;
   std::string address = "/infinite/param1"; // exact, or with '*' / '?' wildcards
   int argIndex = 0;                         // which argument carries the value
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override;

   // OSC learn: the next numeric message that arrives on `port` fills `address`.
   void StartLearn() { mLearning = true; }
   void CancelLearn() { mLearning = false; }
   bool IsLearning() const { return mLearning.load(); }
   // Empty when listening fine, else why the port could not be opened.
   std::string BindError() const { std::lock_guard<std::mutex> l(mFilterMutex); return mBindError; }

   void OnOscMessage(const OscMessage::Message& msg) override;

private:
   void RestartListenerIfNeeded();
   void StopListener();

   std::atomic<float> mLastValue{ 0.5f };
   std::atomic<bool> mLearning{ false };
   int mBoundPort = -1;
   int mRetryCountdown = 0;
   int mLastBoundTry = -1;

   // The hub thread reads the filter; guarded because the UI can edit
   // `address` on the main thread while messages are arriving.
   mutable std::mutex mFilterMutex;
   std::string mAddressFilter;
   int mArgIndexShared = 0;
   std::string mLearnedAddress; // set by the hub thread, applied in Value01()
   std::string mBindError;
   std::string mLastAddress; // main-thread-only: last value mAddressFilter was synced from
};

// The first pure "sink" node in the codebase: no Value01(), no output
// texture - it exists purely for the side effect of sending a UDP OSC
// packet. Modulators are pull-based (Value01() only runs when something
// downstream asks), so nothing would ever drive this node's CookIfNeeded on
// its own; it is ticked once per frame from the same "always cook" walk that
// drives OutputNode/SyphonOutNode in main.cpp.
class OscSendNode : public INode
{
public:
   static INode* Create() { return new OscSendNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : nullptr; }

   // Reads `input`, and sends a UDP packet when its value has moved past
   // `epsilon` since the last send, or `intervalMs` has elapsed regardless -
   // the throttle that keeps a bound LFO from flooding the network every
   // frame while still guaranteeing periodic keep-alive traffic.
   void CookIfNeeded(int frameId) override;

   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   std::string host = "127.0.0.1";
   int port = 9000;
   std::string address = "/infinite/param1";
   float epsilon = 0.001f;   // minimum change in Value01() to trigger a send
   float intervalMs = 50.0f; // also send at least this often, even if unchanged

   void VisitParams(ParamVisitor& v) override;

   // Main-thread readout for the params panel - the last value actually put
   // on the wire, or the sentinel if nothing has sent yet.
   float LastSent() const { return mLastSent; }

private:
   float mLastSent = -1.0f; // sentinel: nothing sent yet
   double mLastSentSeconds = -1.0;
   int mLastCookFrame = -1;
};

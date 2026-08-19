#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "INode.h"
#include "Modulation.h"

// A UDP OSC listener exposed as a modulator: the last float value received
// at `address` on `port`, remapped through low/high the same way LFONode's
// output is (see ModulatorNodes.h's header comment - modulators report a
// zero output texture and the editor draws a value meter instead of a
// preview).
//
// One background thread per instance runs a blocking recvfrom loop; see the
// .cpp for the stop-flag/socket-close teardown that lets RemoveNodeByIndex's
// retire-then-destroy pattern (main.cpp) take a receive thread out cleanly
// without yanking a socket out from under it mid-recvfrom.
class OscReceiveNode : public INode, public IModulator
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
   std::string address = "/infinite/param1";
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override;

private:
   void RestartListenerIfNeeded();
   void StopListener();

   std::atomic<float> mLastValue{ 0.5f };
   std::thread mThread;
   std::atomic<bool> mStop{ false };
   int mSocket = -1;
   int mBoundPort = -1;

   // The receive thread reads this to filter incoming packets by address;
   // guarded because the UI can edit `address` on the main thread while the
   // thread is running.
   std::mutex mFilterMutex;
   std::string mAddressFilter;
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

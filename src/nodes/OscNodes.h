#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"

// Turbo: one shared UDP listener per port (OscHub), used by every OSC
// Receive / OSC to CV node on that port - two nodes on the same port used to
// fight over the socket and the second one never received anything. The
// listener binds all interfaces (LAN + loopback), so TouchOSC / Open Stage
// Control / TouchDesigner on another machine reach it, and it decodes
// bundles and every numeric argument (f, i, d, h, T/F).
namespace OscHub
{
   void Acquire(int port);
   void Release(int port);
   // False when nothing arrived yet on that address.
   bool Get(int port, const std::string& address, std::vector<float>& outArgs);
   // Most recent address seen on the port, with a counter that grows on
   // every message (for "learn" and the monitor line).
   std::string LastAddress(int port, uint64_t* serial = nullptr);
   bool PortOpen(int port);
}

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
   float RawValue() const { return mRaw; }
   bool PortOpen() const { return OscHub::PortOpen(mBoundPort); }

   int port = 9000;
   std::string address = "/infinite/param1";
   int argIndex = 0; // which argument of the message (0 = first)
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override;

private:
   void SyncPort();
   int mBoundPort = -1;
   float mRaw = 0.0f;
};

// Turbo: OSC to CV. Up to 8 OSC addresses (or 8 arguments of one address)
// turned into 8 modulator outputs, each with its own input range, invert and
// smoothing, plus "learn": arm a channel and move the control on the phone /
// controller - the next address that arrives is assigned to it.
class OscToCvNode : public INode
{
public:
   static constexpr int kChannels = 8;

   static INode* Create() { return new OscToCvNode(); }
   OscToCvNode();
   ~OscToCvNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   int OutputCount() const override { return kChannels; }
   const char* OutputLabel(int index) const override;
   IModulator* ModulatorOutput(int index) override;

   float Value(int ch) const { return (ch >= 0 && ch < kChannels) ? mValue[ch] : 0.0f; }
   float Raw(int ch) const { return (ch >= 0 && ch < kChannels) ? mRaw[ch] : 0.0f; }
   bool Received(int ch) const { return (ch >= 0 && ch < kChannels) && mHasValue[ch]; }
   bool PortOpen() const { return OscHub::PortOpen(mBoundPort); }
   std::string LastAddress() const { return OscHub::LastAddress(mBoundPort); }

   int port = 9000;
   std::string address[kChannels];
   int argIndex[kChannels];
   float inMin[kChannels];
   float inMax[kChannels];
   bool invert[kChannels];
   float smoothing[kChannels]; // 0 = instant .. 0.99 = very slow
   int learnChannel = -1;      // UI only

private:
   struct Tap : public IModulator
   {
      OscToCvNode* owner = nullptr;
      int index = 0;
      float Value01() override { return owner ? owner->Value(index) : 0.0f; }
   };

   void SyncPort();
   Tap mTaps[kChannels];
   float mValue[kChannels] = {};
   float mRaw[kChannels] = {};
   bool mHasValue[kChannels] = {};
   int mBoundPort = -1;
   uint64_t mLearnSerial = 0;
   int mLastCookFrame = -1;
};

class OscSendNode : public INode
{
public:
   static INode* Create() { return new OscSendNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : nullptr; }

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

   float LastSent() const { return mLastSent; }

private:
   float mLastSent = -1.0f; // sentinel: nothing sent yet
   double mLastSentSeconds = -1.0;
   int mLastCookFrame = -1;
};

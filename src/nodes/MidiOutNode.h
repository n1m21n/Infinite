#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "core/INode.h"
#include "core/NoteCable.h"
#include "platform/common/MidiOutSink.h"

// MIDI Out: plays a hardware or virtual MIDI synth from a note stream
// (docs/plans/midi-out/README.md). One node = one destination + one channel (D1, D2).
//
// Two-object rule as every audio node: this INode (main thread: params, device open, UI)
// owns an AudioMidiOutNode that turns the note inbox into MIDI bytes on the audio thread and
// pushes them into mSink, whose own thread talks to the OS (D5). CookIfNeeded does no DSP.
class AudioMidiOutNode;

class MidiOutNode : public INode
{
public:
   static INode* Create() { return new MidiOutNode(); }
   MidiOutNode();
   ~MidiOutNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* AudioNodeForNotePorts() override;

   // Saved by name, never by handle (D10): handles change every launch on every OS. Empty
   // means "no device chosen yet", which the node treats as silent, not as an error.
   std::string device;
   int channel = 1; // 1..16
   NoteCable noteInput;

   // Main thread, UI. All cheap.
   bool DeviceOpen() const { return mSink.IsOpen(); }
   const std::string& DeviceError() const { return mDeviceError; }
   unsigned long long SentCount() const { return mSink.SentCount(); }
   void Panic();                 // CC 123 + CC 120 on all 16 channels, and forget held notes
   void RefreshDeviceList();     // re-reads Platform::MidiOutListDevices for the dropdown
   double devicesListedAt = -10.0; // UI clock time of the last refresh, so the body re-reads about every 2 s
   const std::vector<std::string>& Devices() const { return mDevices; }

   // Headless tests: route to a fake instead of an OS device (no hardware in CI).
   void UseTestSink(std::function<void(const MidiOutSink::Msg&)> fn);
   MidiOutSink& Sink() { return mSink; }

private:
   // Declared before mAudioNode on purpose: members die in reverse order, so the audio half's
   // destructor can still push the last note-offs into a live sink (D9), and the sink's Close
   // then delivers them.
   MidiOutSink mSink;
   std::unique_ptr<AudioMidiOutNode> mAudioNode;

   std::string mOpenedDevice;     // the name mSink currently has open
   std::string mDeviceError;
   std::vector<std::string> mDevices;
   int mLastCookFrame = -1;
   int mRetryIn = 0;              // cooks until the next attempt after a failed open
   int mSwitchWait = 0;           // cooks spent waiting for held notes to clear before a switch
   bool mUsingTestSink = false;

   AudioMidiOutNode& Audio();
   void ServiceDevice();
};

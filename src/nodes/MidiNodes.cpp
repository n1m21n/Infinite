#include "MidiNodes.h"

#include <algorithm>
#include <sstream>

#include "Platform.h"
#include "Transport.h"

namespace
{
   float Remap(float v01, float low, float high)
   {
      return low + (high - low) * std::min(1.0f, std::max(0.0f, v01));
   }
}

MidiCCNode::~MidiCCNode()
{
   // The MIDI engine is process-wide; leave it running for any other MIDI nodes.
}

bool MidiCCNode::StartLearn()
{
   std::string error;
   if (!Platform::MidiStart(error))
      return false;
   mLearning = true;
   return true;
}

void MidiCCNode::CancelLearn()
{
   mLearning = false;
}

std::string MidiCCNode::BindingLabel() const
{
   if (!IsBound())
      return "unbound";
   std::string deviceName = Platform::MidiDeviceName((Platform::MidiDeviceId)device);
   std::ostringstream ss;
   if (!deviceName.empty())
      ss << deviceName << " \xC2\xB7 ";
   ss << "Ch " << (channel + 1) << " \xC2\xB7 " << (isNote ? "Note " : "CC ") << controller;
   return ss.str();
}

std::string MidiCCNode::Status() const
{
   if (mLearning)
      return "move a knob, fader or pad on your MIDI controller";
   if (!Platform::MidiIsRunning())
      return "not listening";
   std::string devices = Platform::MidiDeviceSummary();
   return devices.empty() ? "listening (no MIDI devices connected)" : "listening: " + devices;
}

void MidiCCNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mLearning)
   {
      Platform::MidiCCValue last;
      if (Platform::MidiPollLastTouched(last))
      {
         device = (int)last.device;
         channel = last.channel;
         controller = last.controller;
         isNote = last.isNote;
         mLearning = false;
      }
   }

   if (IsBound())
   {
      float raw = 0.0f;
      if (Platform::MidiRead((Platform::MidiDeviceId)device, channel, controller, isNote, raw))
      {
         if (invert)
            raw = 1.0f - raw;
         mValue = Remap(raw, low, high);
      }
   }
}

// ============================================================ MidiTriggerNode

const std::vector<std::string>& MidiTriggerNode::ModeNames()
{
   static const std::vector<std::string> names = { "Pad (trigger)", "Keyboard (any note)" };
   return names;
}

bool MidiTriggerNode::StartLearn()
{
   std::string error;
   if (!Platform::MidiStart(error))
      return false;
   mLearning = true;
   return true;
}

void MidiTriggerNode::CancelLearn()
{
   mLearning = false;
}

std::string MidiTriggerNode::BindingLabel() const
{
   if (!IsBound())
      return "unbound";
   std::string deviceName = Platform::MidiDeviceName((Platform::MidiDeviceId)device);
   std::ostringstream ss;
   if (!deviceName.empty())
      ss << deviceName << " \xC2\xB7 ";
   ss << "Ch " << (channel + 1) << " \xC2\xB7 ";
   if (mode == kKeyboard)
      ss << "any note (keyboard)";
   else
      ss << "Note " << note;
   return ss.str();
}

std::string MidiTriggerNode::Status() const
{
   if (mLearning)
      return mode == kKeyboard ? "play any note on your MIDI keyboard"
                                : "hit a pad or key on your MIDI controller";
   if (!Platform::MidiIsRunning())
      return "not listening";
   std::string devices = Platform::MidiDeviceSummary();
   return devices.empty() ? "listening (no MIDI devices connected)" : "listening: " + devices;
}

void MidiTriggerNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mLearning)
   {
      Platform::MidiCCValue last;
      if (Platform::MidiPollLastTouched(last) && last.isNote)
      {
         device = (int)last.device;
         channel = last.channel;
         if (mode == kKeyboard)
         {
            note = -1;
            Platform::MidiLastNote lastNote;
            mLastHitSeq = Platform::MidiChannelLastNote((Platform::MidiDeviceId)device, channel, lastNote)
                          ? lastNote.hitSeq : 0;
         }
         else
         {
            note = last.controller;
            mLastHitSeq = Platform::MidiNoteHitCount((Platform::MidiDeviceId)device, channel, note);
         }
         mLearning = false;
      }
   }

   const double now = Transport::Instance().Seconds();
   const double dt = std::max(0.0, std::min(0.25, now - mLastSeconds));
   mLastSeconds = now;

   if (!IsBound())
      return;

   if (mode == kKeyboard)
   {
      // A keyboard reports which key is down, not a bang: hold the played
      // note's number (0-127, normalised) until the next note replaces it.
      Platform::MidiLastNote last;
      if (Platform::MidiChannelLastNote((Platform::MidiDeviceId)device, channel, last) && last.hitSeq != mLastHitSeq)
      {
         mLastHitSeq = last.hitSeq;
         mEnvelope = std::min(1.0f, std::max(0.0f, (float)last.note / 127.0f));
      }
      return;
   }

   const unsigned int hits = Platform::MidiNoteHitCount((Platform::MidiDeviceId)device, channel, note);
   if (hits != mLastHitSeq)
   {
      mLastHitSeq = hits;
      float velocity = 1.0f;
      if (velocitySensitive)
         Platform::MidiRead((Platform::MidiDeviceId)device, channel, note, true, velocity);
      mEnvelope = velocitySensitive ? velocity : 1.0f;
   }
   else if (hold > 0.0f)
   {
      mEnvelope = std::max(0.0f, mEnvelope - (float)(dt / hold));
   }
   else
   {
      mEnvelope = 0.0f;
   }
}

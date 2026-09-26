#pragma once

#include <string>
#include <vector>

#include "Modulation.h"

// Infinite-Turbo: MIDI learn for every parameter that has a CV pin.
//
// A mapping ties one MIDI source - device + channel + CC or note number - to
// one (node, param). Sources are never confused: the device is part of the
// key, and two identical controllers get distinct device keys ("nanoKONTROL2"
// and "nanoKONTROL2 #2"). A mapping can also say "any device" / "any channel"
// so a patch moves to another machine or controller without relearning.
//
// Values are written only when a new MIDI value arrives, so the mouse keeps
// working on a mapped parameter; a wired modulation cable always wins.
namespace MidiMap
{
   enum Mode { kContinuous = 0, kMomentary = 1, kToggle = 2 };

   struct Mapping
   {
      int nodeIndex = -1;
      int paramIndex = -1;
      std::string deviceKey; // "" = any device
      int channel = -1;      // 0..15, -1 = any
      int number = 0;        // CC or note number
      bool isNote = false;
      int mode = kContinuous;
      bool softTakeover = false;
      bool invert = false;
      float outMin = 0.0f;   // fraction of the control's throw (its taper, see ParamRef::taper)
      float outMax = 1.0f;
      float smoothMs = 25.0f; // glide towards each new value (0 = jump)
      std::string paramName; // for the MIDI panel

      // runtime
      float lastRaw = -1.0f;
      unsigned int lastHits = 0;
      bool toggled = false;
      bool pickedUp = true;
      float lastWritten = 0.0f;
      bool hasWritten = false;
      double lastActivity = -1.0;
      int lastDir = 0;        // direction of the last accepted move (jitter filter)
      bool gliding = false;
      float glideTarget = 0.0f;
      double lastApply = -1.0;
   };

   // Throw position (0..1) <-> value along a widget taper (ParamRef::taper).
   float PosToValue(int taper, float pos01, float lo, float hi);
   float ValueToPos(int taper, float value, float lo, float hi);

   std::vector<Mapping>& All();
   Mapping* Find(int nodeIndex, int paramIndex);
   void Set(const Mapping& m); // replaces the mapping of the same (node, param)
   void Remove(int nodeIndex, int paramIndex);
   void RemoveNode(int nodeIndex);
   void Clear();

   // Learn: arm a parameter, the next MIDI control that moves is mapped to it.
   bool& LearnMode();
   void Arm(int nodeIndex, int paramIndex, const std::string& paramName);
   void Disarm();
   bool IsArmed(int nodeIndex, int paramIndex);
   bool AnyArmed();

   // Main thread, once per frame before parameters are applied: starts MIDI
   // when needed and completes a pending learn.
   void Update(double nowSeconds);
   // Writes the mapped MIDI value into the parameter when a new value
   // arrived. True when it wrote.
   bool Apply(const ParamRef& ref, double nowSeconds);

   // Recently received (for the pin blink).
   bool RecentlyActive(int nodeIndex, int paramIndex, double nowSeconds);

   // Devices currently connected: stable keys (name, "#2" for a duplicate).
   std::vector<std::string> DeviceKeys();
   std::string SourceLabel(const Mapping& m); // "nanoKONTROL2 ch1 CC 16"
   bool Rescan(std::string& outError);
   std::string Status();
}

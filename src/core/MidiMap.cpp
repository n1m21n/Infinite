#include "MidiMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Platform.h"

namespace MidiMap
{
   namespace
   {
      std::vector<Mapping> gMaps;
      bool gLearnMode = false;
      int gArmedNode = -1;
      int gArmedParam = -1;
      std::string gArmedName;
      bool gFlushPending = false;
      double gLastStartTry = -100.0;
      std::string gStatus;
      std::vector<Platform::MidiDeviceInfo> gDevices; // refreshed once per frame in Update()

      std::vector<Platform::MidiDeviceId> MatchingDevices(const Mapping& m)
      {
         std::vector<Platform::MidiDeviceId> out;
         for (const Platform::MidiDeviceInfo& d : gDevices)
            if (m.deviceKey.empty() || d.key == m.deviceKey)
               out.push_back(d.id);
         return out;
      }

      // Newest differing value across the matching sources (device/channel
      // wildcards), or false when nothing new arrived.
      bool ReadChanged(Mapping& m, float& outRaw)
      {
         const int chLo = m.channel < 0 ? 0 : m.channel;
         const int chHi = m.channel < 0 ? 15 : m.channel;
         for (Platform::MidiDeviceId dev : MatchingDevices(m))
            for (int ch = chLo; ch <= chHi; ch++)
            {
               float v = 0.0f;
               if (Platform::MidiRead(dev, ch, m.number, m.isNote, v) && v != m.lastRaw)
               {
                  outRaw = v;
                  return true;
               }
            }
         return false;
      }

      unsigned int NoteHits(const Mapping& m)
      {
         unsigned int total = 0;
         const int chLo = m.channel < 0 ? 0 : m.channel;
         const int chHi = m.channel < 0 ? 15 : m.channel;
         for (Platform::MidiDeviceId dev : MatchingDevices(m))
            for (int ch = chLo; ch <= chHi; ch++)
               total += Platform::MidiNoteHitCount(dev, ch, m.number);
         return total;
      }

      bool EnsureMidi(double now)
      {
         if (Platform::MidiIsRunning())
            return true;
         if (now - gLastStartTry < 2.0)
            return false;
         gLastStartTry = now;
         std::string error;
         if (!Platform::MidiStart(error))
         {
            gStatus = error.empty() ? "no MIDI input" : error;
            return false;
         }
         gStatus.clear();
         return true;
      }
   }

   float PosToValue(int taper, float pos, float lo, float hi)
   {
      pos = std::clamp(pos, 0.0f, 1.0f);
      if (taper == 1)
      {
         // Same console taper as the dB faders/knobs: unity at 75% of throw.
         static const float kPos[] = { 0.00f, 0.15f, 0.30f, 0.45f, 0.60f, 0.75f, 1.00f };
         static const float kDb[] = { -60.0f, -45.0f, -30.0f, -20.0f, -10.0f, 0.0f, 12.0f };
         for (int i = 0; i + 1 < 7; i++)
            if (pos <= kPos[i + 1])
               return kDb[i] + (kDb[i + 1] - kDb[i]) * (pos - kPos[i]) / (kPos[i + 1] - kPos[i]);
         return kDb[6];
      }
      if (taper == 2)
      {
         const float a = std::max(1.0f, lo), b = std::max(a + 1.0f, hi);
         return a * std::pow(b / a, pos);
      }
      return lo + (hi - lo) * pos;
   }

   float ValueToPos(int taper, float value, float lo, float hi)
   {
      if (taper == 1)
      {
         static const float kPos[] = { 0.00f, 0.15f, 0.30f, 0.45f, 0.60f, 0.75f, 1.00f };
         static const float kDb[] = { -60.0f, -45.0f, -30.0f, -20.0f, -10.0f, 0.0f, 12.0f };
         if (value <= kDb[0]) return 0.0f;
         if (value >= kDb[6]) return 1.0f;
         for (int i = 0; i + 1 < 7; i++)
            if (value <= kDb[i + 1])
               return kPos[i] + (kPos[i + 1] - kPos[i]) * (value - kDb[i]) / (kDb[i + 1] - kDb[i]);
         return 1.0f;
      }
      if (taper == 2)
      {
         const float a = std::max(1.0f, lo), b = std::max(a + 1.0f, hi);
         const float v = std::clamp(value, a, b);
         return std::clamp((std::log(v) - std::log(a)) / (std::log(b) - std::log(a)), 0.0f, 1.0f);
      }
      return hi != lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
   }

   std::vector<Mapping>& All() { return gMaps; }

   Mapping* Find(int nodeIndex, int paramIndex)
   {
      for (Mapping& m : gMaps)
         if (m.nodeIndex == nodeIndex && m.paramIndex == paramIndex)
            return &m;
      return nullptr;
   }

   void Set(const Mapping& m)
   {
      if (Mapping* existing = Find(m.nodeIndex, m.paramIndex))
         *existing = m;
      else
         gMaps.push_back(m);
   }

   void Remove(int nodeIndex, int paramIndex)
   {
      gMaps.erase(std::remove_if(gMaps.begin(), gMaps.end(), [&](const Mapping& m) {
                     return m.nodeIndex == nodeIndex && m.paramIndex == paramIndex;
                  }),
                  gMaps.end());
   }

   void RemoveNode(int nodeIndex)
   {
      gMaps.erase(std::remove_if(gMaps.begin(), gMaps.end(),
                                 [&](const Mapping& m) { return m.nodeIndex == nodeIndex; }),
                  gMaps.end());
      if (gArmedNode == nodeIndex)
         Disarm();
   }

   void Clear()
   {
      gMaps.clear();
      Disarm();
   }

   bool& LearnMode() { return gLearnMode; }

   void Arm(int nodeIndex, int paramIndex, const std::string& paramName)
   {
      gArmedNode = nodeIndex;
      gArmedParam = paramIndex;
      gArmedName = paramName;
      gFlushPending = true; // ignore whatever moved before arming
   }

   void Disarm()
   {
      gArmedNode = -1;
      gArmedParam = -1;
      gArmedName.clear();
   }

   bool IsArmed(int nodeIndex, int paramIndex) { return gArmedNode == nodeIndex && gArmedParam == paramIndex; }
   bool AnyArmed() { return gArmedNode >= 0; }

   void Update(double now)
   {
      if (!gLearnMode && gArmedNode < 0 && gMaps.empty())
         return;
      if (!EnsureMidi(now))
      {
         gDevices.clear();
         return;
      }
      gDevices = Platform::MidiDevices();
      if (gArmedNode < 0)
         return;
      Platform::MidiCCValue v;
      if (gFlushPending)
      {
         while (Platform::MidiPollLastTouched(v))
         {
         }
         gFlushPending = false;
         return;
      }
      if (!Platform::MidiPollLastTouched(v) || v.channel < 0 || v.controller < 0)
         return;
      // A note-off / zero velocity is not a "touch" worth learning from.
      if (v.isNote && v.value01 <= 0.0f)
         return;

      Mapping m;
      if (const Mapping* old = Find(gArmedNode, gArmedParam))
      {
         // Relearning keeps the range and options the user already set.
         m = *old;
      }
      m.nodeIndex = gArmedNode;
      m.paramIndex = gArmedParam;
      m.paramName = gArmedName;
      m.deviceKey.clear();
      for (const Platform::MidiDeviceInfo& d : gDevices)
         if (d.id == v.device)
            m.deviceKey = d.key;
      m.channel = v.channel;
      m.number = v.controller;
      m.isNote = v.isNote;
      if (!Find(gArmedNode, gArmedParam))
         m.mode = v.isNote ? kMomentary : kContinuous;
      m.lastRaw = -1.0f;
      m.lastHits = NoteHits(m);
      m.pickedUp = !m.softTakeover;
      m.lastActivity = now;
      Set(m);
      Disarm();
   }

   bool Apply(const ParamRef& ref, double now)
   {
      Mapping* m = Find(ref.nodeIndex, ref.paramIndex);
      if (m == nullptr || ref.value == nullptr)
         return false;
      const float lo = ref.minValue, hi = ref.maxValue;
      const float span = hi - lo;
      // Along the widget's own taper, so a hardware fader moves exactly like
      // the on-screen one (a dB fader has unity at 75% of its throw).
      auto toValue = [&](float t01) {
         const float t = m->invert ? 1.0f - t01 : t01;
         const float f = m->outMin + (m->outMax - m->outMin) * t;
         return PosToValue(ref.taper, f, lo, hi);
      };
      const double dt = m->lastApply < 0.0 ? 0.0 : std::max(0.0, now - m->lastApply);
      m->lastApply = now;
      // Glide towards the last target (continuous mode), so 7-bit steps and a
      // noisy fader do not read as jumps.
      auto glideStep = [&]() -> bool {
         if (!m->gliding)
            return false;
         const float cur = *ref.value;
         const float k = m->smoothMs > 0.0f ? (float)(1.0 - std::exp(-dt * 1000.0 / m->smoothMs)) : 1.0f;
         float next = cur + (m->glideTarget - cur) * std::clamp(k, 0.0f, 1.0f);
         if (std::fabs(m->glideTarget - next) <= std::fabs(span) * 0.0005f)
         {
            next = m->glideTarget;
            m->gliding = false;
         }
         *ref.value = next;
         m->lastWritten = next;
         m->hasWritten = true;
         return true;
      };

      // Something else (the mouse, a preset) moved the parameter since we last
      // wrote it: stop gliding, and with soft takeover wait for the control
      // to reach it.
      if (m->hasWritten && std::fabs(*ref.value - m->lastWritten) > std::fabs(span) * 0.001f)
      {
         m->gliding = false;
         if (m->softTakeover)
            m->pickedUp = false;
      }

      float target = *ref.value;
      if (m->mode == kToggle)
      {
         bool flip = false;
         if (m->isNote)
         {
            const unsigned int hits = NoteHits(*m);
            flip = hits != m->lastHits;
            m->lastHits = hits;
         }
         else
         {
            float raw = 0.0f;
            if (ReadChanged(*m, raw))
            {
               flip = raw >= 0.5f && m->lastRaw < 0.5f;
               m->lastRaw = raw;
            }
         }
         if (!flip)
            return false;
         m->toggled = !m->toggled;
         target = toValue(m->toggled ? 1.0f : 0.0f);
      }
      else
      {
         float raw = 0.0f;
         if (!ReadChanged(*m, raw))
            return glideStep();
         const float prevRaw = m->lastRaw;
         if (m->mode == kContinuous && prevRaw >= 0.0f)
         {
            // Jitter filter: a worn/noisy fader at rest flickers one step up
            // and down. A single-step move that reverses the last direction is
            // ignored; real movement (or two steps) passes at once.
            const float step = raw - prevRaw;
            const int dir = step > 0.0f ? 1 : -1;
            if (std::fabs(step) < 1.5f / 127.0f && m->lastDir != 0 && dir != m->lastDir)
               return glideStep();
            m->lastDir = dir;
         }
         m->lastRaw = raw;
         if (m->mode == kMomentary)
            target = toValue(raw > (m->isNote ? 0.0f : 0.5f) ? 1.0f : 0.0f);
         else
         {
            target = toValue(raw);
            if (m->softTakeover && !m->pickedUp)
            {
               const float cur = *ref.value;
               const float prev = prevRaw < 0.0f ? target : toValue(prevRaw);
               const bool close = std::fabs(target - cur) <= std::fabs(span) * 0.03f;
               const bool crossed = (prev - cur) * (target - cur) <= 0.0f;
               if (!close && !crossed)
               {
                  m->lastActivity = now;
                  return false;
               }
               m->pickedUp = true;
            }
            if (m->smoothMs > 0.0f)
            {
               m->glideTarget = target;
               m->gliding = true;
               m->lastActivity = now;
               glideStep();
               return true;
            }
         }
      }
      m->gliding = false;
      *ref.value = target;
      m->lastWritten = target;
      m->hasWritten = true;
      m->lastActivity = now;
      return true;
   }

   bool RecentlyActive(int nodeIndex, int paramIndex, double now)
   {
      const Mapping* m = Find(nodeIndex, paramIndex);
      return m != nullptr && m->lastActivity >= 0.0 && now - m->lastActivity < 0.15;
   }

   std::vector<std::string> DeviceKeys()
   {
      std::vector<std::string> out;
      for (const Platform::MidiDeviceInfo& d : Platform::MidiDevices())
         out.push_back(d.key);
      return out;
   }

   std::string SourceLabel(const Mapping& m)
   {
      char buf[160];
      const std::string dev = m.deviceKey.empty() ? std::string("any device") : m.deviceKey;
      char ch[16];
      if (m.channel < 0)
         snprintf(ch, sizeof(ch), "any ch");
      else
         snprintf(ch, sizeof(ch), "ch%d", m.channel + 1);
      snprintf(buf, sizeof(buf), "%s %s %s %d", dev.c_str(), ch, m.isNote ? "note" : "CC", m.number);
      return buf;
   }

   bool Rescan(std::string& outError)
   {
      const bool ok = Platform::MidiRescan(outError);
      gDevices = Platform::MidiDevices();
      gStatus = ok ? std::string() : outError;
      return ok;
   }

   std::string Status()
   {
      if (!gStatus.empty())
         return gStatus;
      return Platform::MidiIsRunning() ? "MIDI running" : "MIDI off (starts on first learn)";
   }
}

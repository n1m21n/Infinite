#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace MovementLog
{
   enum class Source : uint8_t
   {
      Hand = 0,
      Perf,
      Modulator,
      Expression,
      Gesture,
      Prediction,
      Other
   };

   enum Flags : uint8_t
   {
      kNone = 0,
      kCorrection = 1 << 0
   };

   enum class Mark : uint8_t
   {
      SessionStart = 0,
      SessionEnd,
      PatchLoaded,
      PatchNew,
      Undo,
      Redo
   };

   enum class BindingEvent : uint8_t
   {
      Bind = 1,
      Unbind,
      UnbindAll,
      SetRange
   };

   struct KeyRecord
   {
      uint32_t dt_ms = 0;
      uint32_t id = 0;
      uint64_t uid = 0;
      int32_t paramIndex = 0;
      std::string typeName;
      std::string name;
      float minValue = 0.0f;
      float maxValue = 1.0f;
      float step = 0.0f;
      bool isEnum = false;
      bool isBool = false;
      bool hasCurve = false;
   };

   struct ValRecord
   {
      uint32_t id = 0;
      uint32_t dt_ms = 0;
      uint16_t q = 0;
      Source source = Source::Hand;
      uint8_t flags = 0;
   };

   struct BindRecord
   {
      uint32_t dt_ms = 0;
      uint32_t id = 0;
      BindingEvent event = BindingEvent::Bind;
      uint64_t modNodeUid = 0;
      int32_t modOutputIndex = 0;
      float lo = 0.0f;
      float hi = 1.0f;
   };

   struct TransportRecord
   {
      uint32_t dt_ms = 0;
      bool isPlaying = false;
      float bpm = 120.0f;
      double beats = 0.0;
      int32_t beatsPerBar = 4;
   };

   struct MarkRecord
   {
      Mark mark = Mark::SessionStart;
      uint32_t dt_ms = 0;
      uint64_t dropped = 0; // SessionEnd only: events dropped under backpressure
   };

   struct Record
   {
      enum class Type : uint8_t
      {
         Key = 1,
         Val = 2,
         Bind = 3,
         Transport = 4,
         Mark = 5
      } type = Type::Key;

      KeyRecord key;
      ValRecord val;
      BindRecord bind;
      TransportRecord transport;
      MarkRecord mark;
   };

   // Lifecycle
   void Start(const std::string& customDir = "");
   void Stop();
   void SetEnabled(bool on);
   bool IsEnabled();

   // Configuration & stats
   void SetRetentionCapBytes(uint64_t bytes);
   uint64_t GetRetentionCapBytes();
   uint64_t GetLogFolderSizeBytes();
   std::string GetLogDirectory();
   uint64_t DroppedCount();
   // Deletes the oldest closed session files while the folder is over its cap, but never a file newer
   // than stats.bin's lastConsumed (its rows have not reached the statistics yet). Public for tests.
   void EnforceRetention(const std::string& dir);

   // Main thread logging hooks
   void NoteWriter(int nodeIndex, int paramIndex, Source s, uint8_t flags = 0);
   void NoteMark(Mark m);
   void NoteBinding(int nodeIndex, int paramIndex, BindingEvent evt, int modNodeIndex, float lo, float hi);
   // t must be a monotonic wall clock in seconds (not the transport, which freezes when paused).
   void Capture(double t, bool isNormalFrame);

   // Decoding & inspection
   bool ReadFile(const std::string& path, const std::function<bool(const Record&)>& callback);
   void DumpLog(const std::string& path, std::ostream& out);

   // UID lookup helper hook (maps nodeIndex -> uid efficiently)
   void SetNodeUidLookup(const std::function<uint64_t(int nodeIndex)>& lookup);
   // Node type name for a node index (KEY records; called once per new key).
   void SetNodeTypeLookup(const std::function<std::string(int nodeIndex)>& lookup);

   // Self test fixture
   bool RunMovementLogTest();
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Field
{
   // Ten box families from the Field Patcher spec (§8.4).
   enum class BoxFamily
   {
      Sources = 0,
      Arithmetic,
      Range,
      Waves,
      Logic,
      TimeMemory,
      Random,
      Geometry,
      Music,
      InletsOutlets,
      Count
   };

   // UI-level wire/pin kind (§8.6). The Field compiler only sees floats and vecs.
   enum class ValueKind
   {
      Any = 0,
      Number,
      Bool,    // on/off (0/1 float)
      Pulse,   // 1.0 for one tick
      Note,    // MIDI note float
      Vec3,    // 3D point / colour
      Vec2,    // 2D uv / point
      PerPoint // element-domain value
   };

   enum HostMask : uint32_t
   {
      HostNotes     = 1u << 0,
      HostPrimitive = 1u << 1,
      HostElement   = 1u << 2,
      HostPixel     = 1u << 3,
      HostSample    = 1u << 4,
      HostSynth     = 1u << 5,
      HostGraph     = 1u << 6,
      HostGeometry  = HostPrimitive | HostElement,
      HostAudio     = HostSample | HostSynth,
      HostAll       = 0x7Fu
   };

   struct PinSpec
   {
      std::string name;
      ValueKind kind = ValueKind::Number;
      double defaultValue = 0.0;
   };

   struct BoxSpec
   {
      std::string id;
      BoxFamily family = BoxFamily::Sources;
      std::string label;     // Friendly UI label (1-2 words, passed through T() in UI)
      std::string fieldWord; // Canonical Field word or operator
      std::vector<std::string> aliases;
      std::vector<PinSpec> inlets;
      std::vector<PinSpec> outlets;
      uint32_t hostMask = HostAll;
      bool isSugar = false;
   };

   const char* BoxFamilyName(BoxFamily family);
   const char* ValueKindGlyph(ValueKind kind);
   const std::vector<BoxSpec>& AllBoxSpecs();
   const BoxSpec* FindBoxSpec(const std::string& id);
   std::vector<const BoxSpec*> FindBoxSpecsByQuery(const std::string& query, uint32_t hostFilter = 0);
}

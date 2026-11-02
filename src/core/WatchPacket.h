#pragma once

#include <cstddef>
#include <cstdint>

// Wire format v1 of the Apple Watch link (docs/plans/apple-watch/README.md section 6).
// Pure encode/decode, no OS code: the Mac decodes what the watch sends, and the encoder is the reference the
// watch app's Swift packet is checked against. Little-endian, 44 bytes; extra trailing bytes are ignored so a
// later version can append fields.
namespace WatchPacket
{
   constexpr size_t kSize = 44;
   constexpr uint8_t kMagic0 = 'I', kMagic1 = 'W';
   constexpr uint8_t kVersion = 1;

   enum Flag : uint16_t
   {
      kTouching = 1u << 0,
      kDoubleTap = 1u << 1,       // edge: set for the packets around the tap
      kPad1 = 1u << 2,            // pads 1-4 are bits 2-5 (edges)
      kPad2 = 1u << 3,
      kPad3 = 1u << 4,
      kPad4 = 1u << 5,
      kRightWrist = 1u << 6,
      kCrownLeft = 1u << 7,
      kWorkoutRunning = 1u << 8,
   };
   constexpr uint16_t kAnyPad = kPad1 | kPad2 | kPad3 | kPad4;

   // Decoded, in physical units.
   struct Sample
   {
      uint8_t seq = 0;
      uint32_t timeMs = 0;       // the watch's clock
      uint16_t flags = 0;
      float q[4] = { 1, 0, 0, 0 };   // attitude w x y z (unit quaternion)
      float rot[3] = {};         // rotation rate, deg/s
      float accel[3] = {};       // user acceleration, g
      float gravity[3] = { 0, 0, -1 };   // g
      float crown = 0;           // accumulated turns
      float touchX = 0, touchY = 0;      // 0..1
      uint8_t heart = 0;         // bpm, 0 = unknown
      uint8_t battery = 0;       // percent
   };

   enum class Result { kOk, kShort, kBadMagic, kNewerVersion };

   // Fills `out` only on kOk. A packet older than v1 does not exist; a higher version is refused so the UI can
   // say "Update Infinite Remote on the watch".
   Result Decode(const uint8_t* data, size_t len, Sample& out);

   // Writes kSize bytes. Values beyond the field range saturate.
   void Encode(const Sample& s, uint8_t out[kSize]);
}

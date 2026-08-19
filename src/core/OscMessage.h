#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Hand-rolled OSC 1.0 message encode/decode - just enough of the spec for a
// single-argument message (float, int, or string), which is all Infinite's
// OSC Send/Receive nodes need. No bundles, no vendored library: the wire
// format is a handful of null-terminated, 4-byte-aligned fields, cheaper to
// write directly than to pull in a dependency for.
//
// Wire format: address pattern (null-padded to a 4-byte boundary), then a
// type-tag string starting with ',' (same padding rule), then one argument
// per tag char in big-endian byte order (OSC is always network-order).
namespace OscMessage
{
   enum class ArgType { Float, Int, String };

   struct Arg
   {
      ArgType type = ArgType::Float;
      float f = 0.0f;
      int32_t i = 0;
      std::string s;
   };

   // Builds a complete OSC packet: address pattern + typetag + one argument.
   std::vector<uint8_t> Encode(const std::string& address, const Arg& arg);
   std::vector<uint8_t> EncodeFloat(const std::string& address, float value);

   // Parses one OSC message packet (no bundle support - '#bundle' packets are
   // rejected). Returns false on a malformed packet. Only the first argument
   // is decoded; Infinite's nodes only ever need one.
   bool Decode(const uint8_t* data, size_t len, std::string& outAddress, Arg& outArg);
}

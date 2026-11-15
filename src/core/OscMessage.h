#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Hand-rolled OSC 1.0 message encode/decode - float, int and string
// arguments, which is all Infinite's OSC Send/Receive nodes and the OSC
// control API (core/OscControl.h) need. No bundles, no vendored library: the wire
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

   // Any number of arguments, including none (a query). False on a malformed
   // packet, a bundle, or an unsupported type tag.
   std::vector<uint8_t> EncodeMany(const std::string& address, const std::vector<Arg>& args);
   bool DecodeMany(const uint8_t* data, size_t len, std::string& outAddress, std::vector<Arg>& outArgs);

   struct PacketMessage
   {
      std::string address;
      std::vector<Arg> args;
   };

   // Decodes either a single OSC message or an OSC '#bundle' (recursively up to depth 8).
   bool DecodePacket(const uint8_t* data, size_t len, std::vector<PacketMessage>& outMessages);

   // OSC 1.0 address pattern matching with '*' and '?' wildcards (matching within path segments, not '/').
   bool MatchAddress(const std::string& pattern, const std::string& address);

   Arg FloatArg(float f);
   Arg IntArg(int32_t i);
   Arg StringArg(const std::string& s);
}

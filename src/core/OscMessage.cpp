#include "OscMessage.h"

#include <cstring>

namespace OscMessage
{
   namespace
   {
      void AppendPaddedString(std::vector<uint8_t>& out, const std::string& s)
      {
         out.insert(out.end(), s.begin(), s.end());
         out.push_back(0);
         while (out.size() % 4 != 0)
            out.push_back(0);
      }

      void AppendInt32(std::vector<uint8_t>& out, int32_t value)
      {
         uint32_t be = static_cast<uint32_t>(value);
         out.push_back(static_cast<uint8_t>((be >> 24) & 0xFF));
         out.push_back(static_cast<uint8_t>((be >> 16) & 0xFF));
         out.push_back(static_cast<uint8_t>((be >> 8) & 0xFF));
         out.push_back(static_cast<uint8_t>(be & 0xFF));
      }

      void AppendFloat32(std::vector<uint8_t>& out, float value)
      {
         uint32_t bits;
         std::memcpy(&bits, &value, sizeof(bits));
         AppendInt32(out, static_cast<int32_t>(bits));
      }

      // Reads a null-terminated, 4-byte-aligned string starting at `pos`.
      // Returns false (leaving pos/out untouched) if the data runs out
      // before a null terminator or the required padding is found.
      bool ReadPaddedString(const uint8_t* data, size_t len, size_t& pos, std::string& out)
      {
         size_t start = pos;
         size_t i = start;
         while (i < len && data[i] != 0)
            ++i;
         if (i >= len)
            return false; // no terminator found
         out.assign(reinterpret_cast<const char*>(data + start), i - start);

         size_t next = i + 1;
         while (next % 4 != 0)
            ++next;
         if (next > len)
            return false;
         pos = next;
         return true;
      }
   }

   namespace
   {
      bool ReadUInt32(const uint8_t* data, size_t len, size_t& pos, uint32_t& out)
      {
         if (pos + 4 > len)
            return false;
         out = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) |
               (static_cast<uint32_t>(data[pos + 2]) << 8) | static_cast<uint32_t>(data[pos + 3]);
         pos += 4;
         return true;
      }
   }

   Arg FloatArg(float f)
   {
      Arg a;
      a.type = ArgType::Float;
      a.f = f;
      return a;
   }

   Arg IntArg(int32_t i)
   {
      Arg a;
      a.type = ArgType::Int;
      a.i = i;
      return a;
   }

   Arg StringArg(const std::string& s)
   {
      Arg a;
      a.type = ArgType::String;
      a.s = s;
      return a;
   }

   std::vector<uint8_t> EncodeMany(const std::string& address, const std::vector<Arg>& args)
   {
      std::vector<uint8_t> out;
      AppendPaddedString(out, address);
      std::string typeTag = ",";
      for (const Arg& a : args)
         typeTag += a.type == ArgType::Float ? 'f' : (a.type == ArgType::Int ? 'i' : 's');
      AppendPaddedString(out, typeTag);
      for (const Arg& a : args)
      {
         switch (a.type)
         {
            case ArgType::Float:  AppendFloat32(out, a.f); break;
            case ArgType::Int:    AppendInt32(out, a.i); break;
            case ArgType::String: AppendPaddedString(out, a.s); break;
         }
      }
      return out;
   }

   bool DecodeMany(const uint8_t* data, size_t len, std::string& outAddress, std::vector<Arg>& outArgs)
   {
      if (data == nullptr || len == 0)
         return false;
      size_t pos = 0;
      std::string address;
      if (!ReadPaddedString(data, len, pos, address) || address.empty() || address[0] != '/')
         return false;
      std::vector<Arg> args;
      // A message with no type-tag string at all is allowed by OSC 1.0 and
      // means no arguments.
      if (pos < len)
      {
         std::string typeTag;
         if (!ReadPaddedString(data, len, pos, typeTag) || typeTag.empty() || typeTag[0] != ',')
            return false;
         for (size_t t = 1; t < typeTag.size(); t++)
         {
            Arg a;
            uint32_t be = 0;
            if (typeTag[t] == 'f')
            {
               if (!ReadUInt32(data, len, pos, be))
                  return false;
               a.type = ArgType::Float;
               std::memcpy(&a.f, &be, sizeof(a.f));
            }
            else if (typeTag[t] == 'i')
            {
               if (!ReadUInt32(data, len, pos, be))
                  return false;
               a.type = ArgType::Int;
               a.i = static_cast<int32_t>(be);
            }
            else if (typeTag[t] == 's')
            {
               a.type = ArgType::String;
               if (!ReadPaddedString(data, len, pos, a.s))
                  return false;
            }
            else if (typeTag[t] == 'T' || typeTag[t] == 'F')
            {
               a.type = ArgType::Int; // bool -> 1/0, no payload bytes
               a.i = typeTag[t] == 'T' ? 1 : 0;
            }
            else if (typeTag[t] == 'd' || typeTag[t] == 'h')
            {
               uint32_t hi = 0, lo = 0;
               if (!ReadUInt32(data, len, pos, hi) || !ReadUInt32(data, len, pos, lo))
                  return false;
               const uint64_t bits = (static_cast<uint64_t>(hi) << 32) | lo;
               a.type = ArgType::Float; // double / int64 both end up as float
               if (typeTag[t] == 'd')
               {
                  double d;
                  std::memcpy(&d, &bits, sizeof(d));
                  a.f = static_cast<float>(d);
               }
               else
                  a.f = static_cast<float>(static_cast<int64_t>(bits));
            }
            else
               return false;
            args.push_back(a);
         }
      }
      outAddress = address;
      outArgs = std::move(args);
      return true;
   }

   std::vector<uint8_t> Encode(const std::string& address, const Arg& arg)
   {
      std::vector<uint8_t> out;
      AppendPaddedString(out, address);

      std::string typeTag = ",";
      switch (arg.type)
      {
         case ArgType::Float:  typeTag += 'f'; break;
         case ArgType::Int:    typeTag += 'i'; break;
         case ArgType::String: typeTag += 's'; break;
      }
      AppendPaddedString(out, typeTag);

      switch (arg.type)
      {
         case ArgType::Float:  AppendFloat32(out, arg.f); break;
         case ArgType::Int:    AppendInt32(out, arg.i); break;
         case ArgType::String: AppendPaddedString(out, arg.s); break;
      }
      return out;
   }

   std::vector<uint8_t> EncodeFloat(const std::string& address, float value)
   {
      Arg arg;
      arg.type = ArgType::Float;
      arg.f = value;
      return Encode(address, arg);
   }

   bool Decode(const uint8_t* data, size_t len, std::string& outAddress, Arg& outArg)
   {
      std::string address;
      std::vector<Arg> args;
      if (!DecodeMany(data, len, address, args) || args.empty())
         return false;
      outAddress = address;
      outArg = args[0];
      return true;
   }

   namespace
   {
      bool DecodePacketRec(const uint8_t* data, size_t len, std::vector<Message>& out, int depth)
      {
         static const char kBundle[8] = { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0 };
         if (len >= 16 && std::memcmp(data, kBundle, 8) == 0)
         {
            if (depth > 8)
               return false;
            size_t pos = 16; // "#bundle\0" + 8-byte timetag (ignored)
            bool any = false;
            while (pos + 4 <= len)
            {
               uint32_t sz = 0;
               ReadUInt32(data, len, pos, sz);
               if (sz == 0 || sz > len - pos)
                  break; // truncated element - keep what we already got
               any |= DecodePacketRec(data + pos, sz, out, depth + 1);
               pos += sz;
            }
            return any;
         }
         Message m;
         if (!DecodeMany(data, len, m.address, m.args))
            return false;
         out.push_back(std::move(m));
         return true;
      }
   }

   bool DecodePacket(const uint8_t* data, size_t len, std::vector<Message>& out)
   {
      if (data == nullptr || len == 0)
         return false;
      return DecodePacketRec(data, len, out, 0);
   }

   bool MatchAddress(const std::string& pattern, const std::string& address)
   {
      // '*' = any run of characters except '/', '?' = one character except '/'.
      size_t p = 0, a = 0, starP = std::string::npos, starA = 0;
      while (a < address.size())
      {
         if (p < pattern.size() && pattern[p] == '*')
         {
            starP = p++;
            starA = a;
         }
         else if (p < pattern.size() && address[a] != '/' && pattern[p] == '?')
         {
            p++;
            a++;
         }
         else if (p < pattern.size() && pattern[p] == address[a])
         {
            p++;
            a++;
         }
         else if (starP != std::string::npos && address[starA] != '/')
         {
            p = starP + 1;
            a = ++starA;
         }
         else
            return false;
      }
      while (p < pattern.size() && pattern[p] == '*')
         p++;
      return p == pattern.size();
   }
}

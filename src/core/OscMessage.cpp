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
      if (data == nullptr || len == 0)
         return false;

      size_t pos = 0;
      std::string address;
      if (!ReadPaddedString(data, len, pos, address))
         return false;
      if (address.empty() || address[0] != '/')
         return false; // '#bundle' or otherwise malformed - not a message we handle

      std::string typeTag;
      if (!ReadPaddedString(data, len, pos, typeTag))
         return false;
      if (typeTag.empty() || typeTag[0] != ',' || typeTag.size() < 2)
         return false; // no arguments - nothing to decode

      const char tag = typeTag[1];
      if (tag == 'f')
      {
         if (pos + 4 > len)
            return false;
         uint32_t be = (static_cast<uint32_t>(data[pos]) << 24) |
                       (static_cast<uint32_t>(data[pos + 1]) << 16) |
                       (static_cast<uint32_t>(data[pos + 2]) << 8) |
                        static_cast<uint32_t>(data[pos + 3]);
         float value;
         std::memcpy(&value, &be, sizeof(value));
         outArg.type = ArgType::Float;
         outArg.f = value;
      }
      else if (tag == 'i')
      {
         if (pos + 4 > len)
            return false;
         uint32_t be = (static_cast<uint32_t>(data[pos]) << 24) |
                       (static_cast<uint32_t>(data[pos + 1]) << 16) |
                       (static_cast<uint32_t>(data[pos + 2]) << 8) |
                        static_cast<uint32_t>(data[pos + 3]);
         outArg.type = ArgType::Int;
         outArg.i = static_cast<int32_t>(be);
      }
      else if (tag == 's')
      {
         std::string s;
         if (!ReadPaddedString(data, len, pos, s))
            return false;
         outArg.type = ArgType::String;
         outArg.s = s;
      }
      else
      {
         return false; // unsupported type tag - not needed by these nodes
      }

      outAddress = address;
      return true;
   }
}

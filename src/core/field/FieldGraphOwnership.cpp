#include "FieldGraphOwnership.h"

#include <sstream>

namespace Field
{
   namespace
   {
      // Own escaping (Patch.cpp's EscapeLine/UnescapeLine are file-local and
      // Patch.cpp is on the must-not-modify list - see doc §4.3). Keys are
      // emit() identity keys ("name#k0.k1...") and never contain '=' or
      // whitespace by construction (FormatKeyToken uses '.'/'_' as
      // separators), but escape defensively anyway rather than assume.
      std::string Escape(const std::string& s)
      {
         std::string out;
         out.reserve(s.size());
         for (char c : s)
         {
            if (c == '\\') out += "\\\\";
            else if (c == ' ') out += "\\s";
            else if (c == '=') out += "\\e";
            else if (c == '\n') out += "\\n";
            else out += c;
         }
         return out;
      }

      std::string Unescape(const std::string& s)
      {
         std::string out;
         out.reserve(s.size());
         for (size_t i = 0; i < s.size(); ++i)
         {
            if (s[i] == '\\' && i + 1 < s.size())
            {
               char n = s[++i];
               if (n == 's') out += ' ';
               else if (n == 'e') out += '=';
               else if (n == 'n') out += '\n';
               else out += n;
            }
            else
            {
               out += s[i];
            }
         }
         return out;
      }
   }

   std::string GraphOwnershipMap::ToText() const
   {
      std::ostringstream oss;
      bool first = true;
      for (const auto& kv : mEntries)
      {
         if (!first) oss << " ";
         first = false;
         oss << Escape(kv.first) << "=" << kv.second;
      }
      return oss.str();
   }

   GraphOwnershipMap GraphOwnershipMap::FromText(const std::string& text)
   {
      GraphOwnershipMap map;
      std::istringstream iss(text);
      std::string token;
      while (iss >> token)
      {
         size_t eq = token.rfind('=');
         if (eq == std::string::npos) continue;
         std::string key = Unescape(token.substr(0, eq));
         int idx = std::atoi(token.c_str() + eq + 1);
         if (!key.empty())
            map.Set(key, idx);
      }
      return map;
   }
}

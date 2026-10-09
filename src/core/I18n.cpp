#include "I18n.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

namespace I18n
{
namespace
{
   constexpr char kCtxSep = '\x04';

   std::string gResourceDir;
   std::string gCurrent = "en";
   std::string gPending;
   bool gHasPending = false;

   // Each generation owns its strings; the previous one is kept until the next ApplyPending so
   // pointers handed out earlier in the frame that triggered the swap are still readable.
   struct Generation
   {
      Table table;
      std::unordered_map<std::string, std::string> labels; // L() results, node-stable
      std::unordered_map<std::string, std::string> lists;  // TList() results, node-stable
   };
   std::unique_ptr<Generation> gGen(new Generation());
   std::unique_ptr<Generation> gRetired;

   const std::vector<Language> kLanguages = {
      { "en", "English", false },
      { "es", "Espa\xC3\xB1ol", true },
      { "de", "Deutsch", true },
      { "zh", "\xE4\xB8\xAD\xE6\x96\x87\xE7\xAE\x80\xE4\xBD\x93", true },
      { "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", true },
      { "ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", true },
   };

   std::string ComposeKey(const char* ctx, const char* key)
   {
      std::string k;
      if (ctx != nullptr && ctx[0] != '\0')
      {
         k.assign(ctx);
         k.push_back(kCtxSep);
      }
      k.append(key);
      return k;
   }

   // Text before a trailing "##id" suffix; the whole string when there is none.
   std::string VisiblePart(const std::string& key)
   {
      const size_t p = key.find("##");
      return p == std::string::npos ? key : key.substr(0, p);
   }

   // Review hook (INFINITE_PSEUDO=1): every translated string grows by 40 % with '~' so layouts that only fit the
   // English text show themselves. Not a language and not in any menu; an environment variable like the other INFINITE_* review hooks.
   bool PseudoOn()
   {
      static const bool on = std::getenv("INFINITE_PSEUDO") != nullptr;
      return on;
   }
   std::string Pad(const std::string& s)
   {
      const std::vector<uint32_t> cps = Utf8ToCodepoints(s);
      return s + std::string((cps.size() * 2 + 4) / 5 + 1, '~');
   }

   const char* Lookup(const std::string& fullKey)
   {
      auto it = gGen->table.find(fullKey);
      return it == gGen->table.end() ? nullptr : it->second.c_str();
   }

   const char* Translate(const char* ctx, const char* key)
   {
      if (key == nullptr)
         return "";
      if (PseudoOn() && key[0] != '\0')
      {
         const std::string full = "p\x01" + ComposeKey(ctx, key);
         auto c = gGen->labels.find(full);
         if (c == gGen->labels.end())
         {
            const char* hit = gCurrent == "en" ? nullptr : Lookup(ComposeKey(ctx, key));
            c = gGen->labels.emplace(full, Pad(hit != nullptr ? hit : key)).first;
         }
         return c->second.c_str();
      }
      if (gCurrent == "en" || gGen->table.empty())
         return key;
      const char* hit = Lookup(ComposeKey(ctx, key));
      return hit != nullptr ? hit : key;
   }

   const char* Label(const char* ctx, const char* key)
   {
      if (key == nullptr)
         return "";
      // Hidden-label widgets ("##id"), explicit "###" labels and empty strings are IDs, not text.
      if (key[0] == '\0' || (key[0] == '#' && key[1] == '#') || std::strstr(key, "###") != nullptr)
         return key;
      const std::string full = ComposeKey(ctx, key);
      auto cached = gGen->labels.find(full);
      if (cached != gGen->labels.end())
         return cached->second.c_str();

      std::string visible = VisiblePart(key);
      const char* hit = nullptr;
      if (gCurrent != "en")
      {
         // Look up by the visible text so "Save##menu" and "Save##tb" share one translation.
         hit = Lookup(ComposeKey(ctx, visible.c_str()));
      }
      std::string out = hit != nullptr ? hit : visible;
      if (PseudoOn())
         out = Pad(out);
      out += "###";
      out += key;
      return gGen->labels.emplace(full, std::move(out)).first->second.c_str();
   }

   void AddCodepoints(const std::string& s, std::vector<uint32_t>& out)
   {
      for (uint32_t c : Utf8ToCodepoints(s))
         out.push_back(c);
   }

   void SortUnique(std::vector<uint32_t>& v)
   {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
   }
} // namespace

const std::vector<Language>& Languages()
{
   return kLanguages;
}

bool IsSupported(const std::string& code)
{
   for (const Language& l : kLanguages)
      if (code == l.code)
         return true;
   return false;
}

void SetResourceDir(const std::string& dir)
{
   gResourceDir = dir;
}

std::string MatchSupported(const std::vector<std::string>& prefs)
{
   for (const std::string& raw : prefs)
   {
      std::string p;
      for (char c : raw)
         p.push_back(c == '_' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      const std::string primary = p.substr(0, p.find('-'));
      if (primary == "zh")
      {
         // Only Simplified: zh, zh-cn, zh-sg, zh-hans[-*]. zh-hant / zh-tw / zh-hk are skipped.
         const bool traditional = p.find("hant") != std::string::npos || p.find("-tw") != std::string::npos ||
                                  p.find("-hk") != std::string::npos || p.find("-mo") != std::string::npos;
         if (!traditional)
            return "zh";
         continue;
      }
      if (IsSupported(primary))
         return primary;
   }
   return "en";
}

std::vector<uint32_t> Utf8ToCodepoints(const std::string& s)
{
   std::vector<uint32_t> out;
   const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
   const size_t n = s.size();
   for (size_t i = 0; i < n;)
   {
      uint32_t c = p[i];
      int len = 1;
      if (c >= 0xF0 && c < 0xF8)
      {
         c &= 0x07;
         len = 4;
      }
      else if (c >= 0xE0)
      {
         c &= 0x0F;
         len = 3;
      }
      else if (c >= 0xC0)
      {
         c &= 0x1F;
         len = 2;
      }
      else if (c >= 0x80)
      {
         i++; // stray continuation byte
         continue;
      }
      if (i + static_cast<size_t>(len) > n)
         break;
      for (int k = 1; k < len; k++)
         c = (c << 6) | (p[i + k] & 0x3F);
      i += static_cast<size_t>(len);
      out.push_back(c);
   }
   return out;
}

static std::string Unescape(const std::string& s)
{
   std::string out;
   out.reserve(s.size());
   for (size_t i = 0; i < s.size(); i++)
   {
      if (s[i] == '\\' && i + 1 < s.size())
      {
         const char n = s[++i];
         switch (n)
         {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'c': out.push_back(kCtxSep); break;
            case '\\': out.push_back('\\'); break;
            default: out.push_back('\\'); out.push_back(n); break;
         }
      }
      else
         out.push_back(s[i]);
   }
   return out;
}

size_t ParseTsv(const std::string& text, Table& out)
{
   size_t count = 0;
   std::istringstream in(text);
   std::string line;
   while (std::getline(in, line))
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      if (line.empty() || line[0] == '#')
         continue;
      const size_t tab = line.find('\t');
      if (tab == std::string::npos || tab == 0)
         continue;
      const std::string key = Unescape(line.substr(0, tab));
      std::string val = Unescape(line.substr(tab + 1));
      // "=en" marks a string that is intentionally the same as English.
      if (val == "=en")
         val = key.substr(key.find(kCtxSep) == std::string::npos ? 0 : key.find(kCtxSep) + 1);
      if (val.empty())
         continue;
      out[key] = std::move(val);
      count++;
   }
   return count;
}

bool LoadTsvFile(const std::string& path, Table& out)
{
   std::ifstream f(path, std::ios::binary);
   if (!f.is_open())
      return false;
   std::stringstream ss;
   ss << f.rdbuf();
   std::string text = ss.str();
   if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
       static_cast<unsigned char>(text[2]) == 0xBF)
      text.erase(0, 3);
   ParseTsv(text, out);
   return true;
}

bool RequestLanguage(const std::string& code)
{
   if (!IsSupported(code))
      return false;
   gPending = code;
   gHasPending = true;
   return true;
}

bool HasPending()
{
   return gHasPending;
}

void ApplyPending()
{
   if (!gHasPending)
      return;
   gHasPending = false;
   if (gPending == gCurrent && !gGen->table.empty())
      return;
   std::unique_ptr<Generation> next(new Generation());
   if (gPending != "en" && !gResourceDir.empty())
      LoadTsvFile(gResourceDir + "/" + gPending + ".tsv", next->table);
   // A language whose file is missing behaves as English but keeps the chosen code, so the
   // picker still shows what the user picked and the glyph/bake path stays consistent.
   gRetired = std::move(gGen);
   gGen = std::move(next);
   gCurrent = gPending;
}

const std::string& CurrentLanguage()
{
   return gCurrent;
}

const char* T(const char* key)
{
   return Translate(nullptr, key);
}

const char* TC(const char* context, const char* key)
{
   return Translate(context, key);
}

const char* TList(const char* items)
{
   if (items == nullptr)
      return "";
   std::string raw;
   const char* p = items;
   while (*p != '\0')
   {
      const size_t n = std::strlen(p);
      raw.append(p, n + 1);
      p += n + 1;
   }
   raw.push_back('\0');
   if (!PseudoOn() && (gCurrent == "en" || gGen->table.empty()))
      return items;
   auto cached = gGen->lists.find(raw);
   if (cached != gGen->lists.end())
      return cached->second.c_str();
   std::string out;
   for (const char* q = items; *q != '\0'; q += std::strlen(q) + 1)
   {
      const char* hit = Lookup(q);
      out += PseudoOn() ? Pad(hit != nullptr ? hit : q) : std::string(hit != nullptr ? hit : q);
      out.push_back('\0');
   }
   return gGen->lists.emplace(raw, std::move(out)).first->second.c_str();
}

const char* L(const char* key)
{
   return Label(nullptr, key);
}

const char* LC(const char* context, const char* key)
{
   return Label(context, key);
}

std::string IdPartOf(const std::string& label)
{
   const size_t p = label.find("###");
   return p == std::string::npos ? label : label.substr(p + 3);
}

std::string FormatSignature(const std::string& s)
{
   std::string sig;
   for (size_t i = 0; i < s.size(); i++)
   {
      if (s[i] != '%')
         continue;
      if (i + 1 < s.size() && s[i + 1] == '%')
      {
         i++;
         continue;
      }
      if (i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9')
         continue; // "100% on" is prose, not a spec
      size_t j = i + 1;
      while (j < s.size() && std::strchr("0123456789.-+ #*lhzjt", s[j]) != nullptr)
         j++;
      if (j < s.size())
      {
         // Keep precision/flags: "%.2f" and "%.0f" differ, "%5d" vs "%d" does not matter to safety
         // but a changed conversion letter does, so the signature is the letter plus any '.N'.
         const std::string spec = s.substr(i + 1, j - i - 1);
         const size_t dot = spec.find('.');
         sig.push_back(s[j]);
         if (dot != std::string::npos)
            sig += spec.substr(dot);
         i = j;
      }
   }
   return sig;
}

const Table& ActiveTable()
{
   return gGen->table;
}

std::vector<uint32_t> NativeNameGlyphs()
{
   std::vector<uint32_t> g;
   for (const Language& l : kLanguages)
      AddCodepoints(l.nativeName, g);
   SortUnique(g);
   return g;
}

std::vector<uint32_t> GlyphsForCurrentLanguage()
{
   std::vector<uint32_t> g = NativeNameGlyphs();
   for (const auto& kv : gGen->table)
      AddCodepoints(kv.second, g);
   SortUnique(g);
   return g;
}

bool CurrentLanguageNeedsCjk()
{
   return gCurrent == "zh" || gCurrent == "ja";
}

} // namespace I18n

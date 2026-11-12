// fork: JSON themes. Loader, hot reload, export. See docs/themes.md and docs/research/theme-json.md.
#include "app/ui/ThemeFiles.h"

#include "app/ui/design/Tokens.gen.h"
#include "core/CategoryColors.h"
#include "platform/AppPaths.h"

#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using CategoryColors::Color;
using ordered_json = nlohmann::ordered_json;

namespace ThemeFiles
{
namespace
{
struct TokenOverride
{
   const tok::ColorToken* token;
   bool hasDark = false, hasLight = false;
   tok::Rgba8 dark{}, light{};
};

struct Parsed
{
   CategoryColors::UserPreset preset;
   std::vector<TokenOverride> tokens;
};

std::vector<std::string> gMessages;
std::map<std::string, Parsed> gCache;      // path -> last good parse (a broken edit keeps the previous theme)
std::map<std::string, std::string> gSigs;  // path -> "mtime:size"
std::string gDirSig = "?";
std::string gAppliedTokensFor = "\x01";    // theme name whose token overrides are live ("" = defaults)
bool gForceApply = false;

const char* const kColorKeys[6] = { "windowBg", "panelBg", "text", "textDim", "border", "accent" };
const char* const kCableKeys[5] = { "Stream", "Modulation", "Audio", "Note", "Palette" };

CategoryColors::Color& UiField(CategoryColors::UiTheme& t, int i)
{
   CategoryColors::Color* f[6] = { &t.windowBg, &t.panelBg, &t.text, &t.textDim, &t.border, &t.accent };
   return *f[i];
}

std::string Hex(const Color& c)
{
   auto b = [](float v) { return (int)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
   char buf[16];
   std::snprintf(buf, sizeof buf, "#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
   return buf;
}

std::string Hex(const tok::Rgba8& c)
{
   char buf[16];
   if (c.a == 255)
      std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
   else
      std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
   return buf;
}

tok::Rgba8 ToRgba8(const tok::Rgbaf& c)
{
   auto b = [](float v) { return (unsigned char)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
   return { b(c.r), b(c.g), b(c.b), b(c.a) };
}

bool ParseHex(const std::string& s, tok::Rgba8& out)
{
   size_t i = (!s.empty() && s[0] == '#') ? 1 : 0;
   const size_t n = s.size() - i;
   if (n != 6 && n != 8)
      return false;
   unsigned v[4] = { 0, 0, 0, 255 };
   for (size_t k = 0; k < n / 2; k++)
   {
      unsigned x = 0;
      for (int d = 0; d < 2; d++)
      {
         const char ch = s[i + k * 2 + d];
         unsigned h;
         if (ch >= '0' && ch <= '9') h = ch - '0';
         else if (ch >= 'a' && ch <= 'f') h = ch - 'a' + 10;
         else if (ch >= 'A' && ch <= 'F') h = ch - 'A' + 10;
         else return false;
         x = x * 16 + h;
      }
      v[k] = x;
   }
   out = { (unsigned char)v[0], (unsigned char)v[1], (unsigned char)v[2], (unsigned char)v[3] };
   return true;
}

std::string LineOf(const std::string& text, size_t byte)
{
   int line = 1;
   for (size_t i = 0; i < text.size() && i + 1 < byte; i++)
      if (text[i] == '\n')
         line++;
   return std::to_string(line);
}

const tok::ColorToken* FindToken(const std::string& name)
{
   for (int i = 0; i < tok::kColorTokenCount; i++)
      if (name == tok::kColorTokens[i].name)
         return &tok::kColorTokens[i];
   return nullptr;
}

// Reads one colour value; returns an error text ("" = ok).
std::string ReadColor(const nlohmann::json& v, tok::Rgba8& out)
{
   if (!v.is_string())
      return "expected a string like \"#RRGGBB\"";
   if (!ParseHex(v.get<std::string>(), out))
      return "bad colour \"" + v.get<std::string>() + "\" (use #RRGGBB or #RRGGBBAA)";
   return {};
}

Color ToColor(const tok::Rgba8& c) { return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f }; }

std::string FileStem(const std::string& path) { return fs::path(AppPaths::FsPath(path)).stem().string(); }

// false = the file is unusable (message in err); warnings go to `warn`.
bool ParseFile(const std::string& path, Parsed& out, std::string& err, std::vector<std::string>& warn)
{
   std::ifstream in(AppPaths::FsPath(path), std::ios::binary);
   if (!in)
      { err = "cannot read the file"; return false; }
   std::stringstream ss;
   ss << in.rdbuf();
   const std::string text = ss.str();
   nlohmann::json j;
   try
   {
      j = nlohmann::json::parse(text);
   }
   catch (const nlohmann::json::parse_error& e)
   {
      err = "JSON error near line " + LineOf(text, e.byte) + ": " + e.what();
      return false;
   }
   if (!j.is_object())
      { err = "the top level must be a JSON object { ... }"; return false; }

   auto section = [&](const char* key) -> const nlohmann::json* {
      auto it = j.find(key);
      if (it == j.end())
         return nullptr;
      if (!it->is_object())
         { warn.push_back(std::string("\"") + key + "\" must be an object: ignored"); return nullptr; }
      return &*it;
   };

   CategoryColors::UserPreset& p = out.preset;
   p.file = path;
   p.name = j.value("name", std::string());
   if (p.name.empty())
      p.name = FileStem(path);

   // Base: a built-in preset only.
   std::string baseName = j.contains("base") && j["base"].is_string() ? j["base"].get<std::string>() : std::string();
   int base = baseName.empty() ? 0 : CategoryColors::FindPreset(baseName);
   if (base < 0 || base >= CategoryColors::BuiltinPresetCount())
   {
      warn.push_back("unknown \"base\" \"" + baseName + "\": using the first built-in theme");
      base = 0;
   }
   p.base = CategoryColors::PresetNames()[(size_t)base];
   p.ui = CategoryColors::UiThemeForPreset(base);
   p.categories = CategoryColors::PresetCategoryColors(base);

   if (const auto* c = section("colors"))
      for (auto it = c->begin(); it != c->end(); ++it)
      {
         if (!it.key().empty() && it.key()[0] == '_')
            continue;
         int idx = -1;
         for (int k = 0; k < 6; k++)
            if (it.key() == kColorKeys[k])
               idx = k;
         tok::Rgba8 col;
         if (idx < 0)
            warn.push_back("colors." + it.key() + ": unknown key (known: windowBg panelBg text textDim border accent)");
         else if (std::string e = ReadColor(it.value(), col); !e.empty())
            warn.push_back("colors." + it.key() + ": " + e);
         else
            UiField(p.ui, idx) = ToColor(col);
      }

   if (const auto* c = section("categories"))
      for (auto it = c->begin(); it != c->end(); ++it)
      {
         if (!it.key().empty() && it.key()[0] == '_')
            continue;
         const auto& names = CategoryColors::CategoryNames();
         tok::Rgba8 col;
         if (std::find(names.begin(), names.end(), it.key()) == names.end())
            warn.push_back("categories." + it.key() + ": unknown category");
         else if (std::string e = ReadColor(it.value(), col); !e.empty())
            warn.push_back("categories." + it.key() + ": " + e);
         else
            p.categories[it.key()] = ToColor(col);
      }

   if (const auto* c = section("cables"))
      for (auto it = c->begin(); it != c->end(); ++it)
      {
         if (!it.key().empty() && it.key()[0] == '_')
            continue;
         int idx = -1;
         for (int k = 0; k < 5; k++)
            if (it.key() == kCableKeys[k])
               idx = k;
         tok::Rgba8 col;
         if (idx < 0)
            warn.push_back("cables." + it.key() + ": unknown cable type (Stream Modulation Audio Note Palette)");
         else if (std::string e = ReadColor(it.value(), col); !e.empty())
            warn.push_back("cables." + it.key() + ": " + e);
         else
            p.cables[idx] = ToColor(col);
      }

   if (const auto* c = section("tokens"))
      for (auto it = c->begin(); it != c->end(); ++it)
      {
         if (!it.key().empty() && it.key()[0] == '_')
            continue;
         const tok::ColorToken* t = FindToken(it.key());
         if (!t)
            { warn.push_back("tokens." + it.key() + ": unknown token (see docs/theme-tokens.md)"); continue; }
         TokenOverride o;
         o.token = t;
         std::string e;
         if (it.value().is_object() && t->kind != tok::ColorKind::Rgba8)
         {
            if (it.value().contains("dark"))  { e = ReadColor(it.value()["dark"], o.dark);   o.hasDark = e.empty(); }
            if (e.empty() && it.value().contains("light")) { e = ReadColor(it.value()["light"], o.light); o.hasLight = e.empty(); }
         }
         else
         {
            e = ReadColor(it.value(), o.dark);
            o.hasDark = o.hasLight = e.empty();
            o.light = o.dark;
         }
         if (!e.empty())
            warn.push_back("tokens." + it.key() + ": " + e);
         else
            out.tokens.push_back(o);
      }
   return true;
}

void ResetTokens()
{
   for (int i = 0; i < tok::kColorTokenCount; i++)
   {
      const tok::ColorToken& t = tok::kColorTokens[i];
      switch (t.kind)
      {
         case tok::ColorKind::Pair8: *(tok::Pair8*)t.value = *(const tok::Pair8*)t.def; break;
         case tok::ColorKind::Rgba8: *(tok::Rgba8*)t.value = *(const tok::Rgba8*)t.def; break;
         case tok::ColorKind::Pairf: *(tok::Pairf*)t.value = *(const tok::Pairf*)t.def; break;
      }
   }
}

void ApplyTokens(const Parsed* theme)
{
   ResetTokens();
   if (!theme)
      return;
   for (const TokenOverride& o : theme->tokens)
   {
      const tok::ColorToken& t = *o.token;
      auto f = [](const tok::Rgba8& c) { return tok::Rgbaf{ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f }; };
      switch (t.kind)
      {
         case tok::ColorKind::Pair8:
            if (o.hasDark)  ((tok::Pair8*)t.value)->dark = o.dark;
            if (o.hasLight) ((tok::Pair8*)t.value)->light = o.light;
            break;
         case tok::ColorKind::Rgba8: *(tok::Rgba8*)t.value = o.dark; break;
         case tok::ColorKind::Pairf:
            if (o.hasDark)  ((tok::Pairf*)t.value)->dark = f(o.dark);
            if (o.hasLight) ((tok::Pairf*)t.value)->light = f(o.light);
            break;
      }
   }
}

std::vector<std::string> ListFiles()
{
   std::vector<std::string> files;
   std::error_code ec;
   const std::string dir = ThemesDir();
   if (dir.empty())
      return files;
   for (fs::directory_iterator it(AppPaths::FsPath(dir), ec), end; !ec && it != end; it.increment(ec))
   {
      const fs::path& p = it->path();
      if (p.extension() == ".json" && it->is_regular_file(ec))
         files.push_back(p.string());
   }
   std::sort(files.begin(), files.end());
   return files;
}

std::string Signature(const std::vector<std::string>& files, std::map<std::string, std::string>* sigs)
{
   std::string all;
   for (const std::string& f : files)
   {
      std::error_code ec;
      const long long t = (long long)fs::last_write_time(AppPaths::FsPath(f), ec).time_since_epoch().count();
      const unsigned long long sz = (unsigned long long)fs::file_size(AppPaths::FsPath(f), ec);
      const std::string s = std::to_string(t) + ":" + std::to_string(sz);
      if (sigs)
         (*sigs)[f] = s;
      all += f + "|" + s + "\n";
   }
   return all;
}

const Parsed* ActiveParsed()
{
   const int cur = CategoryColors::CurrentPreset();
   if (!CategoryColors::IsUserPreset(cur))
      return nullptr;
   auto it = gCache.find(CategoryColors::PresetFile(cur));
   return it == gCache.end() ? nullptr : &it->second;
}

std::string ActiveName()
{
   const int cur = CategoryColors::CurrentPreset();
   return CategoryColors::IsUserPreset(cur) ? CategoryColors::PresetFile(cur) : std::string();
}

bool DoReload()
{
   gMessages.clear();
   const std::vector<std::string> files = ListFiles();
   std::map<std::string, std::string> sigs;
   gDirSig = Signature(files, &sigs);

   std::map<std::string, Parsed> fresh;
   std::vector<CategoryColors::UserPreset> list;
   for (const std::string& f : files)
   {
      const std::string label = fs::path(AppPaths::FsPath(f)).filename().string();
      Parsed parsed;
      std::string err;
      std::vector<std::string> warn;
      if (ParseFile(f, parsed, err, warn))
         fresh[f] = std::move(parsed);
      else
      {
         auto old = gCache.find(f);
         gMessages.push_back(label + ": " + err + (old != gCache.end() ? " (keeping the previous version)" : " (theme skipped)"));
         if (old == gCache.end())
            continue;
         fresh[f] = old->second;
         warn.clear();
      }
      for (const std::string& w : warn)
         gMessages.push_back(label + ": " + w);
   }
   // Unique display names: not equal to a built-in or an earlier user theme.
   std::vector<std::string> used = CategoryColors::PresetNames();
   used.resize(std::min<size_t>(used.size(), (size_t)CategoryColors::BuiltinPresetCount()));
   for (auto& kv : fresh)
   {
      std::string name = kv.second.preset.name;
      for (int n = 2; std::find(used.begin(), used.end(), name) != used.end(); n++)
         name = kv.second.preset.name + " (" + std::to_string(n) + ")";
      kv.second.preset.name = name;
      used.push_back(name);
      list.push_back(kv.second.preset);
   }
   gCache = std::move(fresh);
   CategoryColors::SetUserPresets(list);
   gForceApply = true;
   return true;
}

} // namespace

std::string ThemesDir()
{
   const std::string base = AppPaths::SettingsDir();
   if (base.empty())
      return {};
   const std::string dir = base + "/themes";
   std::error_code ec;
   fs::create_directories(AppPaths::FsPath(dir), ec);
   return dir;
}

void Init() { DoReload(); }

bool Reload() { return DoReload(); }

const std::vector<std::string>& Messages() { return gMessages; }

bool Tick()
{
   using clock = std::chrono::steady_clock;
   static clock::time_point last{};
   const auto now = clock::now();
   bool changed = false;
   if (now - last > std::chrono::milliseconds(500))
   {
      last = now;
      if (Signature(ListFiles(), nullptr) != gDirSig)
         changed = DoReload();
   }
   const std::string active = ActiveName();
   if (changed || gForceApply || active != gAppliedTokensFor)
   {
      gForceApply = false;
      gAppliedTokensFor = active;
      ApplyTokens(ActiveParsed());
      return true;
   }
   return false;
}

std::string ExportJson(int idx)
{
   ordered_json j;
   const auto& names = CategoryColors::PresetNames();
   const bool user = CategoryColors::IsUserPreset(idx);
   j["_doc"] = "Infinite theme. Any key may be deleted: missing values come from \"base\" (a built-in theme). "
               "Colours are #RRGGBB or #RRGGBBAA. Keys starting with _ are ignored. Format: docs/themes.md";
   j["name"] = idx >= 0 && idx < (int)names.size() ? names[(size_t)idx] + (user ? "" : " (copy)") : "My theme";
   j["base"] = user ? CategoryColors::PresetBase(idx) : (idx >= 0 && idx < (int)names.size() ? names[(size_t)idx] : std::string());
   const CategoryColors::UiTheme& ui = CategoryColors::UiThemeForPreset(idx);
   const CategoryColors::Color* cols[6] = { &ui.windowBg, &ui.panelBg, &ui.text, &ui.textDim, &ui.border, &ui.accent };
   for (int k = 0; k < 6; k++)
      j["colors"][kColorKeys[k]] = Hex(*cols[k]);
   const auto cats = CategoryColors::PresetCategoryColors(idx);
   for (const std::string& c : CategoryColors::CategoryNames())
   {
      auto it = cats.find(c);
      if (it != cats.end())
         j["categories"][c] = Hex(it->second);
   }
   const bool light = (0.2126f * ui.windowBg.r + 0.7152f * ui.windowBg.g + 0.0722f * ui.windowBg.b > 0.5f);
   const auto cab = CategoryColors::PresetCableColors(idx);
   for (int k = 0; k < 5; k++)
   {
      auto it = cab.find(k);
      const Color c = it != cab.end() ? it->second : CategoryColors::DefaultCableColor((CategoryColors::CableType)k, light);
      j["cables"][kCableKeys[k]] = Hex(c);
   }
   const bool live = idx == CategoryColors::CurrentPreset();
   for (int i = 0; i < tok::kColorTokenCount; i++)
   {
      const tok::ColorToken& t = tok::kColorTokens[i];
      const void* src = live ? t.value : t.def;
      switch (t.kind)
      {
         case tok::ColorKind::Pair8:
            j["tokens"][t.name] = { { "dark", Hex(((const tok::Pair8*)src)->dark) }, { "light", Hex(((const tok::Pair8*)src)->light) } };
            break;
         case tok::ColorKind::Rgba8: j["tokens"][t.name] = Hex(*(const tok::Rgba8*)src); break;
         case tok::ColorKind::Pairf:
            j["tokens"][t.name] = { { "dark", Hex(ToRgba8(((const tok::Pairf*)src)->dark)) }, { "light", Hex(ToRgba8(((const tok::Pairf*)src)->light)) } };
            break;
      }
   }
   return j.dump(2) + "\n";
}

bool CreateFromCurrent(std::string* outName)
{
   const std::string dir = ThemesDir();
   if (dir.empty())
      return false;
   const int cur = CategoryColors::CurrentPreset();
   std::string name = CategoryColors::PresetNames()[(size_t)cur];
   std::string stem;
   for (char ch : "My " + name)
      stem += (std::isalnum((unsigned char)ch) ? ch : '-');
   std::string path;
   for (int n = 1;; n++)
   {
      path = dir + "/" + stem + (n > 1 ? "-" + std::to_string(n) : "") + ".json";
      std::error_code ec;
      if (!fs::exists(AppPaths::FsPath(path), ec))
         break;
   }
   std::string json = ExportJson(cur);
   {
      ordered_json j = ordered_json::parse(json);
      j["name"] = "My " + name + (path.find("-2.json") != std::string::npos ? " 2" : "");
      json = j.dump(2) + "\n";
      name = j["name"].get<std::string>();
   }
   std::ofstream out(AppPaths::FsPath(path), std::ios::binary);
   if (!out)
      return false;
   out << json;
   out.close();
   DoReload();
   const int idx = CategoryColors::FindPreset(name);
   if (idx >= 0)
      CategoryColors::SetPreset(idx);
   if (outName)
      *outName = name;
   return true;
}

bool SaveBaseColor(const char* key, float r, float g, float b)
{
   const int cur = CategoryColors::CurrentPreset();
   if (!CategoryColors::IsUserPreset(cur))
      return false;
   const std::string path = CategoryColors::PresetFile(cur);
   nlohmann::ordered_json j;
   {
      std::ifstream in(AppPaths::FsPath(path), std::ios::binary);
      if (!in)
         return false;
      try { in >> j; } catch (...) { return false; }
   }
   if (!j.is_object())
      return false;
   j["colors"][key] = Hex(Color{ r, g, b });
   std::ofstream out(AppPaths::FsPath(path), std::ios::binary);
   if (!out)
      return false;
   out << j.dump(2) << "\n";
   out.close();
   DoReload();
   return true;
}

} // namespace ThemeFiles

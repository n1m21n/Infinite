#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Interface translation. The English source string is the key (gettext style); a missing
// translation falls back to English. UI thread only: tables are swapped between frames by
// ApplyPending(), so a `const char*` returned by T()/L() stays valid for the rest of the frame
// it was fetched in, and for the application's lifetime when the language does not change.
//
// Never call setlocale / std::locale::global anywhere: Patch.cpp parses numbers with the C
// locale's '.', and a `de` locale would corrupt every patch (docs/plans/i18n D6).
namespace I18n
{
   struct Language
   {
      const char* code;       // "en", "es", ...
      const char* nativeName; // shown in the picker, always in its own script
      bool beta;              // machine-drafted until a native speaker signs off (D5)
   };

   const std::vector<Language>& Languages();
   bool IsSupported(const std::string& code);

   // Where <code>.tsv tables live (a directory). Set once at startup.
   void SetResourceDir(const std::string& dir);

   // Picks the first entry of an OS preference list ("de-DE", "zh-Hans-CN", ...) that is one of
   // the six languages; "en" when none is. Traditional Chinese is not matched.
   std::string MatchSupported(const std::vector<std::string>& osPreferences);

   // Queues a switch; the table is swapped by ApplyPending(), never mid-frame. Returns false for
   // an unsupported code. Loading is immediate-on-apply; a missing/corrupt file leaves English.
   bool RequestLanguage(const std::string& code);
   void ApplyPending(); // call between frames (and once before the first font bake)
   bool HasPending();
   const std::string& CurrentLanguage();

   // Plain text (Text, tooltips, format strings): "Speichern". No ID suffix.
   const char* T(const char* key);
   const char* TC(const char* context, const char* key);

   // Widget/window label: "Speichern###Save". The ID part is always the untranslated source
   // string, so imgui.ini state and widget IDs are identical in every language. A key that
   // already carries "##"/"###" or starts with "##" keeps its ID part untouched.
   const char* L(const char* key);
   const char* LC(const char* context, const char* key);

   // Sorted, unique code points that the active table (values only) plus every language's native
   // name need. Drives the CJK glyph merge in the font bake.
   std::vector<uint32_t> GlyphsForCurrentLanguage();
   // Code points of the native names only (always baked so the picker never shows '?').
   std::vector<uint32_t> NativeNameGlyphs();
   bool CurrentLanguageNeedsCjk();

   // ---- for the self-test / tooling ----
   using Table = std::unordered_map<std::string, std::string>;
   // Parses "key<TAB>text" lines; '#' lines and blanks skipped; \n \t \\ \c (context separator)
   // escapes. Returns the number of entries.
   size_t ParseTsv(const std::string& text, Table& out);
   bool LoadTsvFile(const std::string& path, Table& out);
   const Table& ActiveTable();
   std::vector<uint32_t> Utf8ToCodepoints(const std::string& s);
   // printf-spec signature of a string ("%d%s%.2f" -> "dsf"), for parity checks.
   std::string FormatSignature(const std::string& s);
   // Splits a label into the visible text and its ID part (the original). Test hook for L().
   std::string IdPartOf(const std::string& label);
}

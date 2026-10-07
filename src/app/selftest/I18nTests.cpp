// Interface-translation self-test (docs/plans/i18n). Headless, runs before glfwInit().
#include "app/AppShared.h"

#include <clocale>
#include <cstdlib>

namespace app
{
// ============================================================= INFINITE_I18NTEST
//
// Table integrity (printf parity, "=en" markers), the L() ID rule (the ID part is the untranslated
// source in every language), English fallback, live switching through all six languages, and that
// loading a table never touches the C locale (D6: Patch.cpp parses numbers with '.').
// Key coverage against src/ and glyph coverage of the Noto subsets live in
// tools/i18n/extract.py report (driver.sh runs it next to this).
bool RunI18nTest()
{
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      printf("I18NTEST %s: %s\n", what, cond ? "OK" : "FAIL");
      if (!cond)
         ok = false;
   };

   const std::string dir = BundledResourcePath("lang");
   I18n::SetResourceDir(dir);

   const char* kCodes[] = {"en", "es", "de", "zh", "ja", "ru"};
   check(I18n::Languages().size() == 6, "six languages registered");

   // Parity and escapes for every shipped table.
   for (const char* code : kCodes)
   {
      if (std::string(code) == "en")
         continue;
      I18n::Table t;
      const bool loaded = I18n::LoadTsvFile(dir + "/" + code + ".tsv", t);
      std::string what = std::string(code) + " table loads and is non-empty";
      check(loaded && !t.empty(), what.c_str());
      int bad = 0;
      for (const auto& kv : t)
      {
         if (kv.second == "=en")
            continue;
         if (I18n::FormatSignature(kv.first.substr(kv.first.find('\x04') == std::string::npos ? 0 : kv.first.find('\x04') + 1)) !=
             I18n::FormatSignature(kv.second))
         {
            printf("  format mismatch [%s]: %s\n", code, kv.first.c_str());
            ++bad;
         }
      }
      what = std::string(code) + " printf specs match English";
      check(bad == 0, what.c_str());
   }

   // Live switching: six switches, each lands, T() translates, L() keeps the English ID.
   const char* probe = "Settings";
   const std::string cLocaleBefore = setlocale(LC_NUMERIC, nullptr) ? setlocale(LC_NUMERIC, nullptr) : "";
   bool switchOk = true, idOk = true, enIsIdentity = true;
   for (int round = 0; round < 2; ++round)
      for (const char* code : kCodes)
      {
         I18n::RequestLanguage(code);
         I18n::ApplyPending();
         if (I18n::CurrentLanguage() != code)
            switchOk = false;
         const std::string shown = I18n::T(probe);
         const std::string label = I18n::L(probe);
         if (std::string(code) == "en")
         {
            if (shown != probe)
               enIsIdentity = false;
         }
         else if (shown == probe)
            switchOk = false; // pilot table translates "Settings" in every language
         if (I18n::IdPartOf(label) != probe)
            idOk = false;
         if (label.find("###") == std::string::npos && std::string(code) != "en")
            idOk = false;
      }
   check(switchOk, "twelve live switches land and translate");
   check(idOk, "L() ID part equals the English source in every language");
   check(enIsIdentity, "English returns the key unchanged");

   // Fallback, and ID-bearing / empty keys pass through untouched.
   I18n::RequestLanguage("de");
   I18n::ApplyPending();
   check(std::string(I18n::T("No such key anywhere")) == "No such key anywhere", "unknown key falls back to English");
   check(std::string(I18n::L("Save##fileSave")).find("fileSave") != std::string::npos, "L() keeps an existing ##suffix");
   check(std::string(I18n::L("##hidden")) == "##hidden", "L() leaves ##-only ids untouched");
   check(std::string(I18n::L("")) == "", "L() leaves empty strings untouched");

   // D6: nothing in the runtime changes the C locale, and number parsing still uses '.'.
   const std::string cLocaleAfter = setlocale(LC_NUMERIC, nullptr) ? setlocale(LC_NUMERIC, nullptr) : "";
   check(cLocaleBefore == cLocaleAfter, "C locale untouched by language switches");
   check(atof("0.5") == 0.5, "atof still parses '.' under de");

   // Glyph demand: CJK languages ask for glyphs, Latin/Cyrillic ones only need the native names.
   I18n::RequestLanguage("ja");
   I18n::ApplyPending();
   check(I18n::CurrentLanguageNeedsCjk() && !I18n::GlyphsForCurrentLanguage().empty(), "ja requests CJK glyphs");
   I18n::RequestLanguage("ru");
   I18n::ApplyPending();
   check(!I18n::CurrentLanguageNeedsCjk(), "ru needs no CJK face");

   printf("I18NTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}
} // namespace app

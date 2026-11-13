// fork: user themes as JSON files (<SettingsDir>/themes/*.json). Format: docs/themes.md.
#pragma once
#include <string>
#include <vector>

namespace ThemeFiles
{
   std::string ThemesDir();                       // created on demand
   void Init();                                   // load every theme file; call before CategoryColors::LoadPreference()
   bool Tick();                                   // per frame: polls the folder (~0.5 s), reloads on change, applies the active
                                                  // theme's token overrides. true = caller must run ApplyTheme()
   bool Reload();                                 // forced reload; true = something changed
   const std::vector<std::string>& Messages();    // errors and warnings of the last load, one per line
   std::string ExportJson(int presetIndex);       // full sample for the preset (all keys); live token values if it is the active one
   bool CreateFromCurrent(std::string* outName);  // writes a new themes/<name>.json from the active theme and selects it
   // Changes one of the six base colours ("windowBg", "panelBg", "text", "textDim", "border", "accent") of the active user
   // theme in its file. false if the active theme is built-in or the file is unreadable.
   bool SaveBaseColor(const char* key, float r, float g, float b);
}

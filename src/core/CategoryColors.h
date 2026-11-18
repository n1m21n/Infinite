#pragma once

#include <map>
#include <string>
#include <vector>

// Per-category node tinting, cable coloring, node opacity/rounding styling,
// with curated presets and full per-polarity user customizations.
// Preferences and overrides are persisted in ~/Library/Application Support/Infinite.
namespace CategoryColors
{
   struct Color
   {
      float r, g, b;
   };

   // The tones every theme defines for its editor chrome (background, panel,
   // text, border, accent).
   struct UiTheme
   {
      Color windowBg;
      Color panelBg;
      Color text;
      Color textDim;
      Color border;
      Color accent;
   };

   // WCAG contrast ratio (1..21) between two colours.
   float ContrastRatio(const Color& a, const Color& b);

   // The resting fill of a text field, which is where hint text and typed text actually sit: the
   // panel tinted toward the text colour by the strongest resting tint any field uses (FieldWell
   // 0.06, the library search 0.07). Both text tones keep 4.5:1 against it, not just the panel.
   constexpr float kFieldWellTint = 0.07f;
   Color FieldWellBg(const UiTheme& t);

   const UiTheme& CurrentUiTheme();
   bool IsThemeLight();

   // The UiTheme belonging to preset `index` (clamped), regardless of which
   // preset is currently active. Used to preview other presets (e.g. in the
   // theme picker dropdown) without switching the live theme.
   const UiTheme& UiThemeForPreset(int index);

   // Display names of theme presets in menu order.
   const std::vector<std::string>& PresetNames();
   int CurrentPreset();
   void SetPreset(int index); // clamps out-of-range, persists immediately
   void SetPresetTransient(int index); // same, but never written to the user's theme file (tests, gallery)

   // fork: user themes (<SettingsDir>/themes/*.json, see app/ui/ThemeFiles.cpp, docs/themes.md). They are appended
   // after the built-in presets, so indices of the built-ins never move.
   struct UserPreset
   {
      std::string name, file, base;
      UiTheme ui;
      std::map<std::string, Color> categories; // complete (already merged with the base preset)
      std::map<int, Color> cables;             // CableType -> colour; may be partial
   };
   int BuiltinPresetCount();
   bool IsUserPreset(int index);
   const std::string& PresetFile(int index);
   const std::string& PresetBase(int index);
   int FindPreset(const std::string& name); // -1 when unknown
   std::map<std::string, Color> PresetCategoryColors(int index);
   std::map<int, Color> PresetCableColors(int index);
   // Replaces every user theme; the active theme is kept by name (falls back to preset 0).
   void SetUserPresets(const std::vector<UserPreset>& themes);

   // All 10 node module categories
   const std::vector<std::string>& CategoryNames();

   // Returns effective color for category (active override if present, else preset color)
   const Color& ColorFor(const std::string& category);
   Color PresetColorFor(const std::string& category);
   bool HasCategoryColorOverride(const std::string& category, bool light);
   void SetCategoryColor(const std::string& category, const Color& color, bool light, bool saveToFile = true);
   void ResetCategoryColor(const std::string& category, bool light);
   void ResetAllCategoryColors(bool light);

   // Cable types
   enum class CableType
   {
      Stream = 0,     // Video, image, 3D geometry
      Modulation = 1, // Parameter modulation
      Audio = 2,      // Audio signals
      Note = 3,       // MIDI note events
      Palette = 4,    // Color palette bindings
      Count = 5
   };

   const std::vector<std::string>& CableTypeNames();
   const Color& CableColorFor(CableType type);
   Color DefaultCableColor(CableType type, bool light);
   bool HasCableColorOverride(CableType type, bool light);
   void SetCableColor(CableType type, const Color& color, bool light, bool saveToFile = true);
   void ResetCableColor(CableType type, bool light);
   void ResetAllCableColors(bool light);

   // Node styling (transparency, rounding, tint weight)
   float GetNodeOpacity();
   void SetNodeOpacity(float opacity, bool light, bool saveToFile = true);
   float DefaultNodeOpacity(bool light);
   bool HasNodeOpacityOverride(bool light);

   float GetNodeRounding();
   void SetNodeRounding(float radius, bool saveToFile = true);
   float DefaultNodeRounding();
   bool HasNodeRoundingOverride();

   float GetTintWeight();
   void SetTintWeight(float weight, bool light, bool saveToFile = true);
   float DefaultTintWeight(bool light);
   bool HasTintWeightOverride(bool light);

   // Manual UI scale multiplier, applied on top of the OS/monitor DPI scale.
   // 1.0 = no manual adjustment (the default).
   float GetUiScale();
   void SetUiScale(float scale, bool saveToFile = true);

   // Help tooltips (what a control does, its shortcut). Off by default; diagnostics such as error
   // messages and rejection reasons always show.
   bool GetTooltips();
   void SetTooltips(bool on, bool saveToFile = true);
   bool GetCookTimes();
   void SetCookTimes(bool on, bool saveToFile = true);
   bool GetReduceMotion();
   void SetReduceMotion(bool on, bool saveToFile = true);

   // Interface font id: "" is the bundled default (Inter); anything else names an entry in
   // main.cpp's interface font table. Unknown ids fall back to the default there.
   std::string GetUiFont();
   void SetUiFont(const std::string& id, bool saveToFile = true);

   // Interface language code ("en", "de", ...). Empty until the user picks one, so first launch can
   // follow the OS language; an explicit choice (including "en") is saved and always wins.
   std::string GetLanguage();
   void SetLanguage(const std::string& code, bool saveToFile = true);
   bool HasUiScaleOverride();

   // Reset all appearance overrides for active polarity (or both)
   void ResetAllAppearance(bool light);
   void ResetAllAppearanceBoth();

   // Reads saved preset from Infinite.theme and overrides from Infinite.appearance.
   void LoadPreference();
   void SaveAppearanceOverrides();

   // Semantic ranking for category ordering
   int SemanticRank(const std::string& category);
}

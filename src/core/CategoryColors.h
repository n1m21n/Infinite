#pragma once

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

   const UiTheme& CurrentUiTheme();
   bool IsThemeLight();

   // Display names of theme presets in menu order.
   const std::vector<std::string>& PresetNames();
   int CurrentPreset();
   void SetPreset(int index); // clamps out-of-range, persists immediately

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

   // Reset all appearance overrides for active polarity (or both)
   void ResetAllAppearance(bool light);
   void ResetAllAppearanceBoth();

   // Reads saved preset from Infinite.theme and overrides from Infinite.appearance.
   void LoadPreference();
   void SaveAppearanceOverrides();

   // Semantic ranking for category ordering
   int SemanticRank(const std::string& category);
}

#pragma once

#include <string>
#include <vector>

// Per-category node tinting (header/border/label), with a handful of curated
// presets rather than a free colour-picker per category - a fixed vocabulary
// of 11 categories doesn't need a full editor, and picking from known,
// already-balanced palettes (Nord, Dracula, ...) means every preset looks
// deliberate rather than user-mixed. The current pick is persisted next to
// the app's other preferences (see Patch::Recents for the same pattern).
namespace CategoryColors
{
   struct Color
   {
      float r, g, b;
   };

   // The same handful of tones every theme already defines for its own
   // editor chrome (background, panel, text, border, one accent) - applied
   // across ImGui's whole style, not just the node cards, so picking a
   // preset re-themes the app rather than just the graph.
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

   // Display names, in menu order. Index 0 ("Infinite") is the app's own
   // default palette, designed against this app's actual category list;
   // the rest are ports of well-known editor themes, picked for popularity
   // and for being subtle enough to sit behind low-alpha glass rather than
   // fight it.
   const std::vector<std::string>& PresetNames();

   int CurrentPreset();
   void SetPreset(int index); // clamps out-of-range, persists immediately

   // Falls back to a neutral grey for any category string not in the current
   // preset's table (a category added to RegisterNodes() with no matching
   // entry), so an unrecognised category dims rather than crashes.
   const Color& ColorFor(const std::string& category);

   // Reads the saved preset name (if any) from ~/Library/Application
   // Support/Infinite.theme. Call once at startup, before the first frame.
   void LoadPreference();

   // Where a category falls in the "2D/video, 3D, audio, then utility"
   // grouping used to order the Modules mode's category-filter dropdown
   // (see docs/plans - the docked node-browser panel's sort/filter strip).
   // Lower sorts first. Kept next to ColorFor rather than in main.cpp so the
   // catalogue order used for that dropdown and the colour code above can't
   // drift apart as categories are added. Does NOT feed ColorFor or
   // NodeFactory's own registration order - those are unaffected.
   // A category with no entry here (not in the table above) sorts last.
   int SemanticRank(const std::string& category);
}

#include "CategoryColors.h"

#include <cstdlib>
#include <fstream>
#include <map>

namespace CategoryColors
{
namespace
{
using Table = std::map<std::string, Color>;

struct Preset
{
   std::string name;
   Table categories; // one entry per NodeFactory category
   UiTheme ui;        // the rest of the app's chrome
};

// Every preset covers the same 11 category keys, in the order RegisterNodes()
// declares them. Ports of well-known themes only have 7-9 named accents, so
// a couple of the least-populated categories (Resynth, Modulators - each a
// single node type today) reuse a neighbouring hue rather than inventing a
// colour that isn't actually in the source palette. UI tones (window/panel/
// text/border/accent) are each theme's own real background/foreground/accent
// colours, not derived from the category hues.
const std::vector<Preset>& Presets()
{
   static const std::vector<Preset> presets = {
      { "Infinite", {
         { "Source",      { 0.290f, 0.871f, 0.502f } }, // #4ADE80
         { "Text",        { 0.176f, 0.831f, 0.749f } }, // #2DD4BF
         { "3D",          { 0.220f, 0.741f, 0.973f } }, // #38BDF8
         { "Compositing", { 0.506f, 0.549f, 0.973f } }, // #818CF8
         { "Color",       { 0.753f, 0.518f, 0.988f } }, // #C084FC
         { "Mask",        { 0.957f, 0.447f, 0.714f } }, // #F472B6
         { "Feedback",    { 0.984f, 0.443f, 0.522f } }, // #FB7185
         { "Resynth",     { 0.984f, 0.573f, 0.235f } }, // #FB923C
         { "Effects",     { 0.980f, 0.800f, 0.082f } }, // #FACC15
         { "Modulators",  { 0.639f, 0.902f, 0.208f } }, // #A3E635
         { "Output",      { 0.886f, 0.910f, 0.941f } }, // #E2E8F0
      },
      { { 0.039f, 0.043f, 0.059f },  // window  #0A0B0F
        { 0.094f, 0.106f, 0.137f },  // panel   #181B23
        { 0.933f, 0.941f, 0.965f },  // text    #EEF0F6
        { 0.569f, 0.596f, 0.678f },  // textDim #9198AD
        { 0.220f, 0.235f, 0.278f },  // border
        { 0.435f, 0.706f, 1.000f } } // accent  #6FB4FF
      },
      { "Nord", {
         { "Source",      { 0.639f, 0.745f, 0.549f } }, // #A3BE8C
         { "Text",        { 0.561f, 0.737f, 0.733f } }, // #8FBCBB
         { "3D",          { 0.533f, 0.753f, 0.816f } }, // #88C0D0
         { "Compositing", { 0.506f, 0.631f, 0.757f } }, // #81A1C1
         { "Color",       { 0.706f, 0.557f, 0.678f } }, // #B48EAD
         { "Mask",        { 0.749f, 0.380f, 0.416f } }, // #BF616A
         { "Feedback",    { 0.816f, 0.529f, 0.439f } }, // #D08770
         { "Resynth",     { 0.749f, 0.380f, 0.416f } }, // #BF616A (reuses Mask)
         { "Effects",     { 0.922f, 0.796f, 0.545f } }, // #EBCB8B
         { "Modulators",  { 0.369f, 0.506f, 0.675f } }, // #5E81AC
         { "Output",      { 0.925f, 0.937f, 0.957f } }, // #ECEFF4
      },
      { { 0.180f, 0.204f, 0.251f },  // window  nord0 #2E3440
        { 0.231f, 0.259f, 0.322f },  // panel   nord1 #3B4252
        { 0.925f, 0.937f, 0.957f },  // text    nord6 #ECEFF4
        { 0.298f, 0.337f, 0.416f },  // textDim nord3 #4C566A
        { 0.263f, 0.298f, 0.369f },  // border  nord2 #434C5E
        { 0.533f, 0.753f, 0.816f } } // accent  nord8 #88C0D0
      },
      { "Dracula", {
         { "Source",      { 0.314f, 0.980f, 0.482f } }, // #50FA7B
         { "Text",        { 0.545f, 0.914f, 0.992f } }, // #8BE9FD
         { "3D",          { 0.384f, 0.447f, 0.643f } }, // #6272A4
         { "Compositing", { 0.741f, 0.576f, 0.976f } }, // #BD93F9
         { "Color",       { 1.000f, 0.475f, 0.776f } }, // #FF79C6
         { "Mask",        { 1.000f, 0.333f, 0.333f } }, // #FF5555
         { "Feedback",    { 1.000f, 0.722f, 0.424f } }, // #FFB86C
         { "Resynth",     { 1.000f, 0.333f, 0.333f } }, // #FF5555 (reuses Mask)
         { "Effects",     { 0.945f, 0.980f, 0.549f } }, // #F1FA8C
         { "Modulators",  { 0.384f, 0.447f, 0.643f } }, // #6272A4 (reuses 3D)
         { "Output",      { 0.973f, 0.973f, 0.949f } }, // #F8F8F2
      },
      { { 0.157f, 0.165f, 0.212f },  // window  #282A36
        { 0.204f, 0.212f, 0.271f },  // panel   between bg and selection
        { 0.973f, 0.973f, 0.949f },  // text    #F8F8F2
        { 0.384f, 0.447f, 0.643f },  // textDim comment #6272A4
        { 0.267f, 0.278f, 0.353f },  // border  selection #44475A
        { 0.741f, 0.576f, 0.976f } } // accent  purple #BD93F9
      },
      { "Catppuccin Mocha", {
         { "Source",      { 0.651f, 0.890f, 0.631f } }, // #A6E3A1
         { "Text",        { 0.580f, 0.886f, 0.835f } }, // #94E2D5
         { "3D",          { 0.537f, 0.863f, 0.922f } }, // #89DCEB
         { "Compositing", { 0.537f, 0.706f, 0.980f } }, // #89B4FA
         { "Color",       { 0.796f, 0.651f, 0.969f } }, // #CBA6F7
         { "Mask",        { 0.961f, 0.761f, 0.906f } }, // #F5C2E7
         { "Feedback",    { 0.922f, 0.627f, 0.675f } }, // #EBA0AC
         { "Resynth",     { 0.980f, 0.702f, 0.529f } }, // #FAB387
         { "Effects",     { 0.976f, 0.886f, 0.686f } }, // #F9E2AF
         { "Modulators",  { 0.455f, 0.780f, 0.925f } }, // #74C7EC
         { "Output",      { 0.804f, 0.839f, 0.957f } }, // #CDD6F4
      },
      { { 0.118f, 0.118f, 0.180f },  // window  Base    #1E1E2E
        { 0.192f, 0.196f, 0.267f },  // panel   Surface0 #313244
        { 0.804f, 0.839f, 0.957f },  // text    Text    #CDD6F4
        { 0.424f, 0.439f, 0.525f },  // textDim Overlay0 #6C7086
        { 0.271f, 0.278f, 0.353f },  // border  Surface1 #45475A
        { 0.796f, 0.651f, 0.969f } } // accent  Mauve   #CBA6F7
      },
      { "Tokyo Night", {
         { "Source",      { 0.620f, 0.808f, 0.416f } }, // #9ECE6A
         { "Text",        { 0.490f, 0.812f, 1.000f } }, // #7DCFFF
         { "3D",          { 0.478f, 0.635f, 0.969f } }, // #7AA2F7
         { "Compositing", { 0.663f, 0.694f, 0.839f } }, // #A9B1D6
         { "Color",       { 0.733f, 0.604f, 0.969f } }, // #BB9AF7
         { "Mask",        { 0.969f, 0.463f, 0.557f } }, // #F7768E
         { "Feedback",    { 0.859f, 0.294f, 0.294f } }, // #DB4B4B
         { "Resynth",     { 0.878f, 0.686f, 0.408f } }, // #E0AF68
         { "Effects",     { 0.051f, 0.725f, 0.843f } }, // #0DB9D7
         { "Modulators",  { 0.267f, 0.616f, 0.671f } }, // #449DAB
         { "Output",      { 0.753f, 0.792f, 0.961f } }, // #C0CAF5
      },
      { { 0.102f, 0.106f, 0.149f },  // window  Background #1A1B26
        { 0.122f, 0.137f, 0.208f },  // panel   Sidebar bg #1F2335
        { 0.753f, 0.792f, 0.961f },  // text    Foreground #C0CAF5
        { 0.337f, 0.373f, 0.537f },  // textDim Comment    #565F89
        { 0.231f, 0.259f, 0.380f },  // border  Border     #3B4261
        { 0.478f, 0.635f, 0.969f } } // accent  Blue       #7AA2F7
      },
   };
   return presets;
}

int gCurrent = 0;

// Mirrors Patch.cpp's RecentsPath(): one flat preference file next to the
// app's other Application Support state, not a bundled settings format.
std::string ThemePath()
{
   const char* home = getenv("HOME");
   if (home == nullptr)
      return {};
   return std::string(home) + "/Library/Application Support/Infinite.theme";
}
}

const std::vector<std::string>& PresetNames()
{
   static std::vector<std::string> names;
   if (names.empty())
      for (const Preset& p : Presets())
         names.push_back(p.name);
   return names;
}

int CurrentPreset() { return gCurrent; }

void SetPreset(int index)
{
   const int count = (int)Presets().size();
   if (count == 0)
      return;
   gCurrent = std::max(0, std::min(index, count - 1));

   const std::string path = ThemePath();
   if (!path.empty())
   {
      std::ofstream file(path);
      file << Presets()[gCurrent].name << "\n";
   }
}

const Color& ColorFor(const std::string& category)
{
   static const Color kFallback = { 0.42f, 0.44f, 0.50f }; // unrecognised category
   const Table& table = Presets()[gCurrent].categories;
   auto it = table.find(category);
   return it != table.end() ? it->second : kFallback;
}

const UiTheme& CurrentUiTheme()
{
   return Presets()[gCurrent].ui;
}

void LoadPreference()
{
   const std::string path = ThemePath();
   if (path.empty())
      return;
   std::ifstream file(path);
   std::string name;
   if (!std::getline(file, name) || name.empty())
      return;
   const auto& presets = Presets();
   for (size_t i = 0; i < presets.size(); i++)
   {
      if (presets[i].name == name)
      {
         gCurrent = (int)i;
         return;
      }
   }
}
}

#include "CategoryColors.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include "platform/AppPaths.h"

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

// Every preset covers the same 9 category keys, in the order RegisterNodes()
// declares them. Ports of well-known themes only have 7-9 named accents, so
// a couple of the least-populated categories (Modulators, Utility) reuse a
// neighbouring hue rather than inventing a colour that isn't actually in the
// source palette. UI tones (window/panel/text/border/accent) are each
// theme's own real background/foreground/accent colours, not derived from
// the category hues.
//
// A category string a saved patch still carries from before the taxonomy
// simplification (e.g. "Text", "Color", "Mask", "Feedback", "Resynth",
// "AudioUtility", "Output", "OSC", "Audio") isn't a key here any more -
// ColorFor's fallback grey covers it. The node itself still loads and
// resolves correctly (NodeFactory looks nodes up by typeName, not category -
// see Patch.cpp); only its accent colour reverts to neutral until the patch
// is next saved, which re-writes it under its current category.
const std::vector<Preset>& Presets()
{
   static const std::vector<Preset> presets = {
      { "Infinite", {
         { "Source",      { 0.290f, 0.871f, 0.502f } }, // #4ADE80
         { "3D",          { 0.220f, 0.741f, 0.973f } }, // #38BDF8
         { "Compositing", { 0.506f, 0.549f, 0.973f } }, // #818CF8
         { "Effects",     { 0.980f, 0.800f, 0.082f } }, // #FACC15
         { "Modulators",  { 0.639f, 0.902f, 0.208f } }, // #A3E635
         { "Utility",     { 0.639f, 0.663f, 0.729f } }, // #A3A9BA muted grey-blue
         { "Notes",       { 0.298f, 0.851f, 0.392f } }, // #4CD964 green
         { "Synths",      { 0.412f, 0.573f, 0.965f } }, // #6992F6 blue
         { "AudioEffects", { 0.325f, 0.780f, 0.890f } }, // #53C7E3 cyan-blue
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
         { "3D",          { 0.533f, 0.753f, 0.816f } }, // #88C0D0
         { "Compositing", { 0.506f, 0.631f, 0.757f } }, // #81A1C1
         { "Effects",     { 0.922f, 0.796f, 0.545f } }, // #EBCB8B
         { "Modulators",  { 0.369f, 0.506f, 0.675f } }, // #5E81AC
         { "Utility",     { 0.369f, 0.506f, 0.675f } }, // #5E81AC (reuses Modulators)
         { "Notes",       { 0.639f, 0.745f, 0.549f } }, // #A3BE8C (reuses Source)
         { "Synths",      { 0.533f, 0.753f, 0.816f } }, // #88C0D0 (reuses 3D)
         { "AudioEffects", { 0.506f, 0.631f, 0.757f } }, // #81A1C1 (reuses Compositing)
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
         { "3D",          { 0.384f, 0.447f, 0.643f } }, // #6272A4
         { "Compositing", { 0.741f, 0.576f, 0.976f } }, // #BD93F9
         { "Effects",     { 0.945f, 0.980f, 0.549f } }, // #F1FA8C
         { "Modulators",  { 0.384f, 0.447f, 0.643f } }, // #6272A4 (reuses 3D)
         { "Utility",     { 0.384f, 0.447f, 0.643f } }, // #6272A4 (reuses 3D/Modulators)
         { "Notes",       { 0.314f, 0.980f, 0.482f } }, // #50FA7B (reuses Source)
         { "Synths",      { 0.545f, 0.914f, 0.992f } }, // #8BE9FD
         { "AudioEffects", { 0.741f, 0.576f, 0.976f } }, // #BD93F9 (reuses Compositing)
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
         { "3D",          { 0.537f, 0.863f, 0.922f } }, // #89DCEB
         { "Compositing", { 0.537f, 0.706f, 0.980f } }, // #89B4FA
         { "Effects",     { 0.976f, 0.886f, 0.686f } }, // #F9E2AF
         { "Modulators",  { 0.455f, 0.780f, 0.925f } }, // #74C7EC
         { "Utility",     { 0.455f, 0.780f, 0.925f } }, // #74C7EC (reuses Modulators)
         { "Notes",       { 0.651f, 0.890f, 0.631f } }, // #A6E3A1 (reuses Source)
         { "Synths",      { 0.537f, 0.706f, 0.980f } }, // #89B4FA (reuses Compositing)
         { "AudioEffects", { 0.537f, 0.863f, 0.922f } }, // #89DCEB (reuses 3D)
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
         { "3D",          { 0.478f, 0.635f, 0.969f } }, // #7AA2F7
         { "Compositing", { 0.663f, 0.694f, 0.839f } }, // #A9B1D6
         { "Effects",     { 0.051f, 0.725f, 0.843f } }, // #0DB9D7
         { "Modulators",  { 0.267f, 0.616f, 0.671f } }, // #449DAB
         { "Utility",     { 0.267f, 0.616f, 0.671f } }, // #449DAB (reuses Modulators)
         { "Notes",       { 0.620f, 0.808f, 0.416f } }, // #9ECE6A (reuses Source)
         { "Synths",      { 0.478f, 0.635f, 0.969f } }, // #7AA2F7 (reuses 3D)
         { "AudioEffects", { 0.490f, 0.812f, 1.000f } }, // #7DCFFF
      },
      { { 0.102f, 0.106f, 0.149f },  // window  Background #1A1B26
        { 0.122f, 0.137f, 0.208f },  // panel   Sidebar bg #1F2335
        { 0.753f, 0.792f, 0.961f },  // text    Foreground #C0CAF5
        { 0.337f, 0.373f, 0.537f },  // textDim Comment    #565F89
        { 0.231f, 0.259f, 0.380f },  // border  Border     #3B4261
        { 0.478f, 0.635f, 0.969f } } // accent  Blue       #7AA2F7
      },
      { "Catppuccin Latte", {
         { "Source",      { 0.251f, 0.627f, 0.169f } }, // Green   #40A02B
         { "3D",          { 0.016f, 0.647f, 0.898f } }, // Sky     #04A5E5
         { "Compositing", { 0.118f, 0.400f, 0.961f } }, // Blue    #1E66F5
         { "Effects",     { 0.875f, 0.557f, 0.114f } }, // Yellow  #DF8E1D
         { "Modulators",  { 0.125f, 0.624f, 0.710f } }, // Sapphire #209FB5
         { "Utility",     { 0.125f, 0.624f, 0.710f } }, // Sapphire (reuses Modulators)
         { "Notes",       { 0.251f, 0.627f, 0.169f } }, // Green
         { "Synths",      { 0.118f, 0.400f, 0.961f } }, // Blue
         { "AudioEffects", { 0.016f, 0.647f, 0.898f } }, // Sky
      },
      { { 0.937f, 0.945f, 0.961f },  // window  Base     #EFF1F5
        { 0.886f, 0.902f, 0.925f },  // panel   Mantle   #E6E9EF
        { 0.298f, 0.310f, 0.412f },  // text    Text     #4C4F69
        { 0.549f, 0.561f, 0.631f },  // textDim Overlay0 #8C8FA1
        { 0.760f, 0.780f, 0.835f },  // border  Surface1 #BCC0CC
        { 0.533f, 0.224f, 0.937f } } // accent  Mauve    #8839EF
      },
      { "GitHub Light", {
         { "Source",      { 0.102f, 0.498f, 0.216f } }, // Green  #1A7F37
         { "3D",          { 0.035f, 0.412f, 0.855f } }, // Blue   #0969DA
         { "Compositing", { 0.510f, 0.314f, 0.875f } }, // Purple #8250DF
         { "Effects",     { 0.604f, 0.404f, 0.000f } }, // Gold   #9A6700
         { "Modulators",  { 0.067f, 0.388f, 0.161f } }, // Forest #116329
         { "Utility",     { 0.396f, 0.427f, 0.463f } }, // #656D76
         { "Notes",       { 0.102f, 0.498f, 0.216f } },
         { "Synths",      { 0.035f, 0.412f, 0.855f } },
         { "AudioEffects", { 0.020f, 0.314f, 0.682f } },
      },
      { { 0.980f, 0.984f, 0.988f },  // window  #FAFBFC
        { 0.930f, 0.941f, 0.953f },  // panel   #EDF0F3
        { 0.122f, 0.137f, 0.157f },  // text    #1F2328
        { 0.396f, 0.427f, 0.463f },  // textDim #656D76
        { 0.780f, 0.812f, 0.847f },  // border  #C7CFD8
        { 0.035f, 0.412f, 0.855f } } // accent  #0969DA
      },
      { "Solarized Light", {
         { "Source",      { 0.522f, 0.600f, 0.000f } }, // Green   #859900
         { "3D",          { 0.149f, 0.545f, 0.824f } }, // Blue    #268BD2
         { "Compositing", { 0.424f, 0.443f, 0.769f } }, // Violet  #6C71C4
         { "Effects",     { 0.710f, 0.537f, 0.000f } }, // Yellow  #B58900
         { "Modulators",  { 0.165f, 0.631f, 0.596f } }, // Cyan
         { "Utility",     { 0.576f, 0.631f, 0.631f } }, // Base1   #93A1A1
         { "Notes",       { 0.522f, 0.600f, 0.000f } },
         { "Synths",      { 0.149f, 0.545f, 0.824f } },
         { "AudioEffects", { 0.165f, 0.631f, 0.596f } },
      },
      { { 0.992f, 0.965f, 0.890f },  // window  Base3   #FDF6E3
        { 0.925f, 0.898f, 0.812f },  // panel   Base2   #EBE5CF
        { 0.345f, 0.431f, 0.459f },  // text    Base00  #586E75
        { 0.576f, 0.631f, 0.631f },  // textDim Base1   #93A1A1
        { 0.810f, 0.776f, 0.655f },  // border  #CFC6A7
        { 0.149f, 0.545f, 0.824f } } // accent  Blue    #268BD2
      },
      { "Nord Light", {
         { "Source",      { 0.310f, 0.494f, 0.224f } }, // nord14 #4F7E39
         { "3D",          { 0.184f, 0.416f, 0.494f } }, // nord8  #2F6A7E
         { "Compositing", { 0.231f, 0.357f, 0.518f } }, // nord9  #3B5B84
         { "Effects",     { 0.494f, 0.388f, 0.145f } }, // nord13 #7E6325
         { "Modulators",  { 0.212f, 0.325f, 0.467f } },
         { "Utility",     { 0.298f, 0.337f, 0.416f } }, // nord3  #4C566A
         { "Notes",       { 0.310f, 0.494f, 0.224f } },
         { "Synths",      { 0.231f, 0.357f, 0.518f } },
         { "AudioEffects", { 0.184f, 0.416f, 0.494f } },
      },
      { { 0.925f, 0.937f, 0.957f },  // window  nord6   #ECEFF4
        { 0.882f, 0.902f, 0.929f },  // panel   nord5   #E1E6ED
        { 0.180f, 0.204f, 0.251f },  // text    nord0   #2E3440
        { 0.298f, 0.337f, 0.416f },  // textDim nord3   #4C566A
        { 0.790f, 0.820f, 0.870f },  // border  #C9D1DE
        { 0.369f, 0.506f, 0.675f } } // accent  nord10  #5E81AC
      },
   };
   return presets;
}

int gCurrent = 0;

// Mirrors Patch.cpp's RecentsPath(): one flat preference file next to the
// app's other Application Support state, not a bundled settings format.
std::string ThemePath()
{
   std::string dir = AppPaths::AppSupportDir();
   return dir.empty() ? std::string() : dir + "/Infinite.theme";
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

int SemanticRank(const std::string& category)
{
   // 2D/video, then 3D, then audio, then utility - see the header comment.
   static const std::vector<std::string> kOrder = {
      "Source", "Compositing", "Effects",
      "3D",
      "Notes", "Synths", "AudioEffects",
      "Modulators", "Utility",
   };
   for (size_t i = 0; i < kOrder.size(); i++)
      if (kOrder[i] == category)
         return (int)i;
   return (int)kOrder.size();
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

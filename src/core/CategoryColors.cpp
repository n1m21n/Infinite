#include "CategoryColors.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <iomanip>
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

// Every preset covers all 10 category keys, in the order RegisterNodes()
// declares them. Ports of well-known themes have accents mapped to categories.
const std::vector<Preset>& Presets()
{
   static const std::vector<Preset> presets = {
      { "Infinite", {
         { "Source",       { 0.290f, 0.871f, 0.502f } }, // #4ADE80
         { "3D",           { 0.220f, 0.741f, 0.973f } }, // #38BDF8
         { "Compositing",  { 0.506f, 0.549f, 0.973f } }, // #818CF8
         { "Effects",      { 0.980f, 0.800f, 0.082f } }, // #FACC15
         { "Modulators",   { 0.639f, 0.902f, 0.208f } }, // #A3E635
         { "Macros",       { 0.988f, 0.588f, 0.235f } }, // #FC963C orange/amber
         { "Utility",      { 0.639f, 0.663f, 0.729f } }, // #A3A9BA muted grey-blue
         { "Notes",        { 0.298f, 0.851f, 0.392f } }, // #4CD964 green
         { "Synths",       { 0.412f, 0.573f, 0.965f } }, // #6992F6 blue
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
         { "Source",       { 0.639f, 0.745f, 0.549f } }, // #A3BE8C
         { "3D",           { 0.533f, 0.753f, 0.816f } }, // #88C0D0
         { "Compositing",  { 0.506f, 0.631f, 0.757f } }, // #81A1C1
         { "Effects",      { 0.922f, 0.796f, 0.545f } }, // #EBCB8B
         { "Modulators",   { 0.369f, 0.506f, 0.675f } }, // #5E81AC
         { "Macros",       { 0.816f, 0.529f, 0.439f } }, // #D08770 nord12 orange
         { "Utility",      { 0.369f, 0.506f, 0.675f } }, // #5E81AC (reuses Modulators)
         { "Notes",        { 0.639f, 0.745f, 0.549f } }, // #A3BE8C (reuses Source)
         { "Synths",       { 0.533f, 0.753f, 0.816f } }, // #88C0D0 (reuses 3D)
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
         { "Source",       { 0.314f, 0.980f, 0.482f } }, // #50FA7B
         { "3D",           { 0.384f, 0.447f, 0.643f } }, // #6272A4
         { "Compositing",  { 0.741f, 0.576f, 0.976f } }, // #BD93F9
         { "Effects",      { 0.945f, 0.980f, 0.549f } }, // #F1FA8C
         { "Modulators",   { 0.384f, 0.447f, 0.643f } }, // #6272A4 (reuses 3D)
         { "Macros",       { 1.000f, 0.722f, 0.424f } }, // #FFB86C orange
         { "Utility",      { 0.384f, 0.447f, 0.643f } }, // #6272A4 (reuses 3D/Modulators)
         { "Notes",        { 0.314f, 0.980f, 0.482f } }, // #50FA7B (reuses Source)
         { "Synths",       { 0.545f, 0.914f, 0.992f } }, // #8BE9FD
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
         { "Source",       { 0.651f, 0.890f, 0.631f } }, // #A6E3A1
         { "3D",           { 0.537f, 0.863f, 0.922f } }, // #89DCEB
         { "Compositing",  { 0.537f, 0.706f, 0.980f } }, // #89B4FA
         { "Effects",      { 0.976f, 0.886f, 0.686f } }, // #F9E2AF
         { "Modulators",   { 0.455f, 0.780f, 0.925f } }, // #74C7EC
         { "Macros",       { 0.980f, 0.702f, 0.529f } }, // #FAB387 peach
         { "Utility",      { 0.455f, 0.780f, 0.925f } }, // #74C7EC (reuses Modulators)
         { "Notes",        { 0.651f, 0.890f, 0.631f } }, // #A6E3A1 (reuses Source)
         { "Synths",       { 0.537f, 0.706f, 0.980f } }, // #89B4FA (reuses Compositing)
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
         { "Source",       { 0.620f, 0.808f, 0.416f } }, // #9ECE6A
         { "3D",           { 0.478f, 0.635f, 0.969f } }, // #7AA2F7
         { "Compositing",  { 0.663f, 0.694f, 0.839f } }, // #A9B1D6
         { "Effects",      { 0.051f, 0.725f, 0.843f } }, // #0DB9D7
         { "Modulators",   { 0.267f, 0.616f, 0.671f } }, // #449DAB
         { "Macros",       { 1.000f, 0.612f, 0.380f } }, // #FF9E64 orange
         { "Utility",      { 0.267f, 0.616f, 0.671f } }, // #449DAB (reuses Modulators)
         { "Notes",        { 0.620f, 0.808f, 0.416f } }, // #9ECE6A (reuses Source)
         { "Synths",       { 0.478f, 0.635f, 0.969f } }, // #7AA2F7 (reuses 3D)
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
         { "Source",       { 0.251f, 0.627f, 0.169f } }, // Green   #40A02B
         { "3D",           { 0.016f, 0.647f, 0.898f } }, // Sky     #04A5E5
         { "Compositing",  { 0.118f, 0.400f, 0.961f } }, // Blue    #1E66F5
         { "Effects",      { 0.875f, 0.557f, 0.114f } }, // Yellow  #DF8E1D
         { "Modulators",   { 0.125f, 0.624f, 0.710f } }, // Sapphire #209FB5
         { "Macros",       { 0.996f, 0.549f, 0.365f } }, // Peach   #FE640B
         { "Utility",      { 0.125f, 0.624f, 0.710f } }, // Sapphire (reuses Modulators)
         { "Notes",        { 0.251f, 0.627f, 0.169f } }, // Green
         { "Synths",       { 0.118f, 0.400f, 0.961f } }, // Blue
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
         { "Source",       { 0.102f, 0.498f, 0.216f } }, // Green  #1A7F37
         { "3D",           { 0.035f, 0.412f, 0.855f } }, // Blue   #0969DA
         { "Compositing",  { 0.510f, 0.314f, 0.875f } }, // Purple #8250DF
         { "Effects",      { 0.604f, 0.404f, 0.000f } }, // Gold   #9A6700
         { "Modulators",   { 0.067f, 0.388f, 0.161f } }, // Forest #116329
         { "Macros",       { 0.855f, 0.388f, 0.051f } }, // Orange #DA620D
         { "Utility",      { 0.396f, 0.427f, 0.463f } }, // #656D76
         { "Notes",        { 0.102f, 0.498f, 0.216f } },
         { "Synths",       { 0.035f, 0.412f, 0.855f } },
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
         { "Source",       { 0.522f, 0.600f, 0.000f } }, // Green   #859900
         { "3D",           { 0.149f, 0.545f, 0.824f } }, // Blue    #268BD2
         { "Compositing",  { 0.424f, 0.443f, 0.769f } }, // Violet  #6C71C4
         { "Effects",      { 0.710f, 0.537f, 0.000f } }, // Yellow  #B58900
         { "Modulators",   { 0.165f, 0.631f, 0.596f } }, // Cyan
         { "Macros",       { 0.796f, 0.294f, 0.086f } }, // Orange  #CB4B16
         { "Utility",      { 0.576f, 0.631f, 0.631f } }, // Base1   #93A1A1
         { "Notes",        { 0.522f, 0.600f, 0.000f } },
         { "Synths",       { 0.149f, 0.545f, 0.824f } },
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
         { "Source",       { 0.310f, 0.494f, 0.224f } }, // nord14 #4F7E39
         { "3D",           { 0.184f, 0.416f, 0.494f } }, // nord8  #2F6A7E
         { "Compositing",  { 0.231f, 0.357f, 0.518f } }, // nord9  #3B5B84
         { "Effects",      { 0.494f, 0.388f, 0.145f } }, // nord13 #7E6325
         { "Modulators",   { 0.212f, 0.325f, 0.467f } },
         { "Macros",       { 0.749f, 0.380f, 0.286f } }, // nord12 #D08770
         { "Utility",      { 0.298f, 0.337f, 0.416f } }, // nord3  #4C566A
         { "Notes",        { 0.310f, 0.494f, 0.224f } },
         { "Synths",       { 0.231f, 0.357f, 0.518f } },
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

// Polarity-specific appearance overrides
std::map<std::string, Color> gCategoryOverridesDark;
std::map<std::string, Color> gCategoryOverridesLight;
std::map<int, Color> gCableOverridesDark;
std::map<int, Color> gCableOverridesLight;

float gNodeOpacityDark = -1.0f;
float gNodeOpacityLight = -1.0f;
float gTintWeightDark = -1.0f;
float gTintWeightLight = -1.0f;
float gNodeRounding = -1.0f;

std::string ThemePath()
{
   std::string dir = AppPaths::AppSupportDir();
   if (dir.empty())
      return std::string();
   const std::string path = dir + "/Infinite.theme";
   const std::string home = AppPaths::HomeDir();
   if (!home.empty())
      AppPaths::MigrateLegacyFile(home + "/Library/Application Support/Infinite.theme", path);
   return path;
}

std::string AppearancePath()
{
   std::string dir = AppPaths::AppSupportDir();
   if (dir.empty())
      return std::string();
   return dir + "/Infinite.appearance";
}

void LoadAppearanceOverrides()
{
   gCategoryOverridesDark.clear();
   gCategoryOverridesLight.clear();
   gCableOverridesDark.clear();
   gCableOverridesLight.clear();
   gNodeOpacityDark = -1.0f;
   gNodeOpacityLight = -1.0f;
   gTintWeightDark = -1.0f;
   gTintWeightLight = -1.0f;
   gNodeRounding = -1.0f;

   const std::string path = AppearancePath();
   if (path.empty())
      return;
   std::ifstream file(path);
   if (!file.is_open())
      return;

   std::string line;
   while (std::getline(file, line))
   {
      if (line.empty() || line[0] == '#')
         continue;
      const size_t eq = line.find('=');
      if (eq == std::string::npos)
         continue;
      const std::string key = line.substr(0, eq);
      const std::string val = line.substr(eq + 1);

      auto parseColor = [](const std::string& s, Color& out) -> bool {
         std::stringstream ss(s);
         char c1, c2;
         if (ss >> out.r >> c1 >> out.g >> c2 >> out.b && c1 == ',' && c2 == ',')
            return true;
         return false;
      };

      if (key.rfind("category.dark.", 0) == 0)
      {
         const std::string cat = key.substr(14);
         Color c;
         if (parseColor(val, c))
            gCategoryOverridesDark[cat] = c;
      }
      else if (key.rfind("category.light.", 0) == 0)
      {
         const std::string cat = key.substr(15);
         Color c;
         if (parseColor(val, c))
            gCategoryOverridesLight[cat] = c;
      }
      else if (key.rfind("cable.dark.", 0) == 0)
      {
         const int type = std::atoi(key.substr(11).c_str());
         Color c;
         if (parseColor(val, c))
            gCableOverridesDark[type] = c;
      }
      else if (key.rfind("cable.light.", 0) == 0)
      {
         const int type = std::atoi(key.substr(12).c_str());
         Color c;
         if (parseColor(val, c))
            gCableOverridesLight[type] = c;
      }
      else if (key == "node.dark.opacity")
      {
         gNodeOpacityDark = std::strtof(val.c_str(), nullptr);
      }
      else if (key == "node.light.opacity")
      {
         gNodeOpacityLight = std::strtof(val.c_str(), nullptr);
      }
      else if (key == "node.dark.tintWeight")
      {
         gTintWeightDark = std::strtof(val.c_str(), nullptr);
      }
      else if (key == "node.light.tintWeight")
      {
         gTintWeightLight = std::strtof(val.c_str(), nullptr);
      }
      else if (key == "node.rounding")
      {
         gNodeRounding = std::strtof(val.c_str(), nullptr);
      }
   }
}

} // anonymous namespace

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

const UiTheme& CurrentUiTheme()
{
   return Presets()[gCurrent].ui;
}

bool IsThemeLight()
{
   const UiTheme& t = CurrentUiTheme();
   return (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);
}

const std::vector<std::string>& CategoryNames()
{
   static const std::vector<std::string> kCategories = {
      "Source", "3D", "Compositing", "Effects", "Modulators",
      "Macros", "Utility", "Notes", "Synths", "AudioEffects"
   };
   return kCategories;
}

Color PresetColorFor(const std::string& category)
{
   static const Color kFallback = { 0.42f, 0.44f, 0.50f };
   const Table& table = Presets()[gCurrent].categories;
   auto it = table.find(category);
   return it != table.end() ? it->second : kFallback;
}

bool HasCategoryColorOverride(const std::string& category, bool light)
{
   const auto& map = light ? gCategoryOverridesLight : gCategoryOverridesDark;
   return map.find(category) != map.end();
}

void SetCategoryColor(const std::string& category, const Color& color, bool light, bool saveToFile)
{
   if (light)
      gCategoryOverridesLight[category] = color;
   else
      gCategoryOverridesDark[category] = color;
   if (saveToFile)
      SaveAppearanceOverrides();
}

void ResetCategoryColor(const std::string& category, bool light)
{
   if (light)
      gCategoryOverridesLight.erase(category);
   else
      gCategoryOverridesDark.erase(category);
   SaveAppearanceOverrides();
}

void ResetAllCategoryColors(bool light)
{
   if (light)
      gCategoryOverridesLight.clear();
   else
      gCategoryOverridesDark.clear();
   SaveAppearanceOverrides();
}

const Color& ColorFor(const std::string& category)
{
   const bool light = IsThemeLight();
   const auto& map = light ? gCategoryOverridesLight : gCategoryOverridesDark;
   auto it = map.find(category);
   if (it != map.end())
      return it->second;

   static const Color kFallback = { 0.42f, 0.44f, 0.50f };
   const Table& table = Presets()[gCurrent].categories;
   auto pit = table.find(category);
   return pit != table.end() ? pit->second : kFallback;
}

const std::vector<std::string>& CableTypeNames()
{
   static const std::vector<std::string> kNames = {
      "Image & 3D / Stream",
      "Modulation",
      "Audio",
      "Notes / MIDI",
      "Palette / Color"
   };
   return kNames;
}

Color DefaultCableColor(CableType type, bool light)
{
   switch (type)
   {
      case CableType::Stream:
         return light ? Color{ 55.0f / 255.0f, 62.0f / 255.0f, 78.0f / 255.0f }
                      : Color{ 230.0f / 255.0f, 235.0f / 255.0f, 245.0f / 255.0f };
      case CableType::Modulation:
         return light ? Color{ 215.0f / 255.0f, 120.0f / 255.0f, 10.0f / 255.0f }
                      : Color{ 255.0f / 255.0f, 190.0f / 255.0f, 90.0f / 255.0f };
      case CableType::Audio:
         return light ? Color{ 30.0f / 255.0f, 100.0f / 255.0f, 230.0f / 255.0f }
                      : Color{ 90.0f / 255.0f, 150.0f / 255.0f, 255.0f / 255.0f };
      case CableType::Note:
         return light ? Color{ 20.0f / 255.0f, 150.0f / 255.0f, 60.0f / 255.0f }
                      : Color{ 90.0f / 255.0f, 220.0f / 255.0f, 130.0f / 255.0f };
      case CableType::Palette:
         return light ? Color{ 170.0f / 255.0f, 40.0f / 255.0f, 180.0f / 255.0f }
                      : Color{ 223.0f / 255.0f, 107.0f / 255.0f, 232.0f / 255.0f };
      default:
         return Color{ 0.5f, 0.5f, 0.5f };
   }
}

bool HasCableColorOverride(CableType type, bool light)
{
   const auto& map = light ? gCableOverridesLight : gCableOverridesDark;
   return map.find((int)type) != map.end();
}

void SetCableColor(CableType type, const Color& color, bool light, bool saveToFile)
{
   if (light)
      gCableOverridesLight[(int)type] = color;
   else
      gCableOverridesDark[(int)type] = color;
   if (saveToFile)
      SaveAppearanceOverrides();
}

void ResetCableColor(CableType type, bool light)
{
   if (light)
      gCableOverridesLight.erase((int)type);
   else
      gCableOverridesDark.erase((int)type);
   SaveAppearanceOverrides();
}

void ResetAllCableColors(bool light)
{
   if (light)
      gCableOverridesLight.clear();
   else
      gCableOverridesDark.clear();
   SaveAppearanceOverrides();
}

const Color& CableColorFor(CableType type)
{
   const bool light = IsThemeLight();
   const auto& map = light ? gCableOverridesLight : gCableOverridesDark;
   auto it = map.find((int)type);
   if (it != map.end())
      return it->second;

   static Color cachedColors[5];
   cachedColors[(int)type] = DefaultCableColor(type, light);
   return cachedColors[(int)type];
}

float DefaultNodeOpacity(bool light)
{
   return light ? 0.95f : 0.784f;
}

bool HasNodeOpacityOverride(bool light)
{
   const float val = light ? gNodeOpacityLight : gNodeOpacityDark;
   return val >= 0.0f;
}

float GetNodeOpacity()
{
   const bool light = IsThemeLight();
   const float val = light ? gNodeOpacityLight : gNodeOpacityDark;
   return val >= 0.0f ? val : DefaultNodeOpacity(light);
}

void SetNodeOpacity(float opacity, bool light, bool saveToFile)
{
   if (light)
      gNodeOpacityLight = opacity;
   else
      gNodeOpacityDark = opacity;
   if (saveToFile)
      SaveAppearanceOverrides();
}

float DefaultNodeRounding()
{
   return 12.0f;
}

bool HasNodeRoundingOverride()
{
   return gNodeRounding >= 0.0f;
}

float GetNodeRounding()
{
   return gNodeRounding >= 0.0f ? gNodeRounding : DefaultNodeRounding();
}

void SetNodeRounding(float radius, bool saveToFile)
{
   gNodeRounding = radius;
   if (saveToFile)
      SaveAppearanceOverrides();
}

float DefaultTintWeight(bool light)
{
   return light ? 0.12f : 0.16f;
}

bool HasTintWeightOverride(bool light)
{
   const float val = light ? gTintWeightLight : gTintWeightDark;
   return val >= 0.0f;
}

float GetTintWeight()
{
   const bool light = IsThemeLight();
   const float val = light ? gTintWeightLight : gTintWeightDark;
   return val >= 0.0f ? val : DefaultTintWeight(light);
}

void SetTintWeight(float weight, bool light, bool saveToFile)
{
   if (light)
      gTintWeightLight = weight;
   else
      gTintWeightDark = weight;
   if (saveToFile)
      SaveAppearanceOverrides();
}

void ResetAllAppearance(bool light)
{
   ResetAllCategoryColors(light);
   ResetAllCableColors(light);
   if (light)
   {
      gNodeOpacityLight = -1.0f;
      gTintWeightLight = -1.0f;
   }
   else
   {
      gNodeOpacityDark = -1.0f;
      gTintWeightDark = -1.0f;
   }
   gNodeRounding = -1.0f;
   SaveAppearanceOverrides();
}

void ResetAllAppearanceBoth()
{
   ResetAllAppearance(false);
   ResetAllAppearance(true);
}

void SaveAppearanceOverrides()
{
   const std::string path = AppearancePath();
   if (path.empty())
      return;

   // If everything is default, remove the overrides file
   if (gCategoryOverridesDark.empty() && gCategoryOverridesLight.empty() &&
       gCableOverridesDark.empty() && gCableOverridesLight.empty() &&
       gNodeOpacityDark < 0.0f && gNodeOpacityLight < 0.0f &&
       gTintWeightDark < 0.0f && gTintWeightLight < 0.0f &&
       gNodeRounding < 0.0f)
   {
      std::remove(path.c_str());
      return;
   }

   std::ofstream file(path);
   if (!file.is_open())
      return;

   file << std::fixed << std::setprecision(4);
   for (const auto& kv : gCategoryOverridesDark)
      file << "category.dark." << kv.first << "=" << kv.second.r << "," << kv.second.g << "," << kv.second.b << "\n";
   for (const auto& kv : gCategoryOverridesLight)
      file << "category.light." << kv.first << "=" << kv.second.r << "," << kv.second.g << "," << kv.second.b << "\n";
   for (const auto& kv : gCableOverridesDark)
      file << "cable.dark." << kv.first << "=" << kv.second.r << "," << kv.second.g << "," << kv.second.b << "\n";
   for (const auto& kv : gCableOverridesLight)
      file << "cable.light." << kv.first << "=" << kv.second.r << "," << kv.second.g << "," << kv.second.b << "\n";
   if (gNodeOpacityDark >= 0.0f)
      file << "node.dark.opacity=" << gNodeOpacityDark << "\n";
   if (gNodeOpacityLight >= 0.0f)
      file << "node.light.opacity=" << gNodeOpacityLight << "\n";
   if (gTintWeightDark >= 0.0f)
      file << "node.dark.tintWeight=" << gTintWeightDark << "\n";
   if (gTintWeightLight >= 0.0f)
      file << "node.light.tintWeight=" << gTintWeightLight << "\n";
   if (gNodeRounding >= 0.0f)
      file << "node.rounding=" << gNodeRounding << "\n";
}

int SemanticRank(const std::string& category)
{
   // 2D/video, then 3D, then audio, then modulators & macros, then utility
   static const std::vector<std::string> kOrder = {
      "Source", "Compositing", "Effects",
      "3D",
      "Notes", "Synths", "AudioEffects",
      "Modulators", "Macros", "Utility",
   };
   for (size_t i = 0; i < kOrder.size(); i++)
      if (kOrder[i] == category)
         return (int)i;
   return (int)kOrder.size();
}

void LoadPreference()
{
   const std::string path = ThemePath();
   if (!path.empty())
   {
      std::ifstream file(path);
      std::string name;
      if (std::getline(file, name) && !name.empty())
      {
         const auto& presets = Presets();
         for (size_t i = 0; i < presets.size(); i++)
         {
            if (presets[i].name == name)
            {
               gCurrent = (int)i;
               break;
            }
         }
      }
   }
   LoadAppearanceOverrides();
}

} // namespace CategoryColors


#include "NodeFactory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "CategoryColors.h"
#include "INode.h"

namespace
{
   bool AlphabeticalLess(const std::string& a, const std::string& b)
   {
      const auto folded = [](unsigned char c) { return (unsigned char)std::tolower(c); };
      const bool less = std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
         [&](char ac, char bc) { return folded((unsigned char)ac) < folded((unsigned char)bc); });
      const bool greater = std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end(),
         [&](char bc, char ac) { return folded((unsigned char)bc) < folded((unsigned char)ac); });
      return less || (!less && !greater && a < b);
   }

   // Infinite-Turbo menu organisation. Categories stay one token (Patch.cpp
   // reads them with >>); DisplayName() in main.cpp gives them readable
   // captions. A node not listed keeps the category it registered with.
   const std::string& TurboCategory(const std::string& name, const std::string& fallback)
   {
      static const std::map<std::string, std::string> kMap = {
         // visual
         { "Text", "Source" },
         { "Shape", "Shapes" }, // Turbo 0.55: the named shapes register as Shapes
         { "Video", "Video" }, { "VMPC", "Video" }, { "Video In", "Video" }, { "Spout In", "Video" },
         { "Clip Matrix", "Video" }, { "Timeline", "Video" },
         // 3D, Turbo 0.55: sources (the primitives register as 3DSource) /
         // geometry operators (stay "3D") / scene and render
         { "Geometry", "3DSource" }, { "Model 3D", "3DSource" }, { "Text 3D", "3DSource" },
         { "Metaballs", "3DSource" }, { "Ocean", "3DSource" }, { "Curve", "3DSource" },
         { "Image to Points", "3DSource" }, { "Depth Projection", "3DSource" },
         { "Particle System", "3DSource" }, { "Cloth", "3DSource" },
         { "Camera", "3DScene" }, { "Light", "3DScene" }, { "HDRI", "3DScene" },
         { "Material", "3DScene" }, { "Mapping", "3DScene" }, { "Render 3D", "3DScene" },
         { "Resynthesize", "Effects" },
         { "Macro Knob", "Macros" }, { "Macro XY", "Macros" },
         { "Comment", "Utility" }, { "Group", "Utility" }, { "Null", "Utility" }, { "Viewport", "Utility" },
         // audio
         { "Audio In", "AudioIO" }, { "Audio Out", "AudioIO" }, { "Audio File", "AudioIO" },
         { "Audio Texture", "AudioVisual" }, { "Audio Color Ramp", "AudioVisual" },
         { "Audio Displacement", "AudioVisual" }, { "Audio Ribbon", "AudioVisual" },
         // notes, Turbo 0.55: what makes notes (NoteSource) before what
         // changes them (Notes)
         { "Keyboard", "NoteSource" }, { "MIDI Notes", "NoteSource" }, { "MIDI File", "NoteSource" },
         { "Chord Progression", "NoteSource" }, { "Note Sequencer", "NoteSource" },
         { "Random Note Generator", "NoteSource" }, { "Bouncing Balls", "NoteSource" },
         { "Predictive Notes", "Prediction" }, { "Predictive Quantize", "Prediction" },
         { "Predictive Velocity", "Prediction" }, { "Predictive Rhythm", "Prediction" },
         // control
         { "Math", "CVTools" }, { "Compare", "CVTools" }, { "Invert", "CVTools" }, { "Range to Range", "CVTools" },
         { "Smoothing", "CVTools" }, { "CV Recorder", "CVTools" }, { "Mod Depth", "CVTools" }, { "Null Modulator", "CVTools" }, { "CV to Pitch", "CVTools" },
         { "Audio Analyze", "Analysis" }, { "Image Analyze", "Analysis" }, { "Audio to CV", "Analysis" },
         { "Note to CV", "Analysis" }, { "Velocity to CV", "Analysis" }, { "Palette", "Analysis" },
         { "MIDI CC", "Control" }, { "MIDI Trigger", "Control" }, { "OSC Receive", "Control" },
         { "OSC Send", "Control" }, { "OSC to CV", "Control" },
      };
      auto it = kMap.find(name);
      return it != kMap.end() ? it->second : fallback;
   }

   // Turbo 0.55: menu order follows the signal path inside each family
   // (make, change, combine, send out), then the control family, utility
   // last. A category not listed goes after these, by family, then A-Z.
   int CategoryRank(const std::string& category)
   {
      static const char* const kOrder[] = {
         "Source", "Shapes", "Video", "Color", "Effects", "Mask", "Feedback", "Compositing", "Output",
         "3DSource", "3D", "3DScene",
         "AudioIO", "NoteSource", "Notes", "Prediction", "Synths", "AudioEffects", "AudioUtility", "AudioVisual",
         "Modulators", "CVTools", "Analysis", "Control", "Macros", "UI",
         "Utility",
      };
      for (int i = 0; i < (int)(sizeof(kOrder) / sizeof(kOrder[0])); i++)
         if (category == kOrder[i])
            return i;
      return -1;
   }

   bool CategoryLess(const std::string& a, const std::string& b)
   {
      const int rankA = CategoryRank(a);
      const int rankB = CategoryRank(b);
      if (rankA >= 0 || rankB >= 0)
      {
         if (rankA < 0 || rankB < 0)
            return rankA >= 0; // listed ones first
         return rankA < rankB;
      }
      const int familyA = CategoryColors::FamilyRank(a);
      const int familyB = CategoryColors::FamilyRank(b);
      return familyA != familyB ? familyA < familyB : AlphabeticalLess(a, b);
   }
}

NodeFactory& NodeFactory::Instance()
{
   static NodeFactory instance;
   return instance;
}

void NodeFactory::Register(const std::string& name, CreateNodeFn createFn, const std::string& registeredCategory)
{
   const std::string category = TurboCategory(name, registeredCategory);
   if (mFactoryMap.count(name) != 0)
   {
      // Refuse rather than overwrite: whichever registration came first is the
      // one every existing patch file already refers to by that name.
      fprintf(stderr, "node name collision: \"%s\" is already registered\n", name.c_str());
      mDuplicates.push_back(name);
      return;
   }
   mFactoryMap[name] = NodeInfo{ name, category, createFn };

   auto& list = mByCategory[category];
   if (list.empty())
      mCategoryOrder.insert(std::lower_bound(mCategoryOrder.begin(), mCategoryOrder.end(), category,
                                             CategoryLess), category);
   list.insert(std::lower_bound(list.begin(), list.end(), name, AlphabeticalLess), name);
}

std::string NodeFactory::CategoryOf(const std::string& requested) const
{
   auto it = mFactoryMap.find(CanonicalName(requested));
   return it != mFactoryMap.end() ? it->second.category : std::string();
}

const std::string& NodeFactory::CanonicalName(const std::string& name)
{
   // old name -> current name. Saved patches store the type name as text.
   static const std::map<std::string, std::string> kRenamed = {
      { "Set Color", "Set Vertex Color" }, // 0.41, same name as upstream
      { "Syphon In", "Spout In" },          // 0.43.2, Windows-only fork
      { "Syphon Out", "Spout Out" },
   };
   auto it = kRenamed.find(name);
   return it != kRenamed.end() ? it->second : name;
}

INode* NodeFactory::MakeNode(const std::string& requested)
{
   const std::string& name = CanonicalName(requested);
   auto it = mFactoryMap.find(name);
   if (it != mFactoryMap.end())
      return it->second.createFn();
   return nullptr;
}

const std::vector<std::string>& NodeFactory::GetNodesInCategory(const std::string& category) const
{
   static const std::vector<std::string> kEmpty;
   auto it = mByCategory.find(category);
   return it != mByCategory.end() ? it->second : kEmpty;
}

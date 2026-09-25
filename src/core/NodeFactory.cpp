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
         { "Video", "Video" }, { "VMPC", "Video" }, { "Video In", "Video" }, { "Syphon In", "Video" },
         { "Resynthesize", "Effects" },
         { "Comment", "Utility" }, { "Group", "Utility" }, { "Null", "Utility" }, { "Viewport", "Utility" },
         // audio
         { "Audio In", "AudioIO" }, { "Audio Out", "AudioIO" }, { "Audio File", "AudioIO" },
         { "Audio Texture", "AudioVisual" }, { "Audio Color Ramp", "AudioVisual" },
         { "Audio Displacement", "AudioVisual" }, { "Audio Ribbon", "AudioVisual" },
         { "Predictive Notes", "Prediction" }, { "Predictive Quantize", "Prediction" },
         { "Predictive Velocity", "Prediction" }, { "Predictive Rhythm", "Prediction" },
         // control
         { "Math", "CVTools" }, { "Compare", "CVTools" }, { "Invert", "CVTools" }, { "Range to Range", "CVTools" },
         { "Smoothing", "CVTools" }, { "Mod Depth", "CVTools" }, { "Null Modulator", "CVTools" }, { "CV to Pitch", "CVTools" },
         { "Audio Analyze", "Analysis" }, { "Image Analyze", "Analysis" }, { "Audio to CV", "Analysis" },
         { "Note to CV", "Analysis" }, { "Palette", "Analysis" },
         { "MIDI CC", "Control" }, { "MIDI Trigger", "Control" }, { "OSC Receive", "Control" },
         { "OSC Send", "Control" }, { "OSC to CV", "Control" },
      };
      auto it = kMap.find(name);
      return it != kMap.end() ? it->second : fallback;
   }

   bool CategoryLess(const std::string& a, const std::string& b)
   {
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

std::string NodeFactory::CategoryOf(const std::string& name) const
{
   auto it = mFactoryMap.find(name);
   return it != mFactoryMap.end() ? it->second.category : std::string();
}

INode* NodeFactory::MakeNode(const std::string& name)
{
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

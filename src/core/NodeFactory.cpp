#include "NodeFactory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

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

void NodeFactory::Register(const std::string& name, CreateNodeFn createFn, const std::string& category)
{
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

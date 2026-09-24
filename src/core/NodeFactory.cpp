#include "NodeFactory.h"

#include <cstdio>

#include "INode.h"

NodeFactory& NodeFactory::Instance()
{
   static NodeFactory factory;
   return factory;
}

void NodeFactory::Register(const std::string& name, CreateNodeFn createFn, const std::string& category)
{
   // emplace leaves an existing entry untouched, which is exactly the rule:
   // the first registration is the one every saved patch already refers to.
   const bool inserted = mEntries.emplace(name, Entry{ category, std::move(createFn) }).second;
   if (!inserted)
   {
      fprintf(stderr, "node name collision: \"%s\" is already registered\n", name.c_str());
      mDuplicates.push_back(name);
      return;
   }

   std::vector<std::string>& names = mNamesByCategory[category];
   if (names.empty())
      mCategoryOrder.push_back(category);
   names.push_back(name);
}

INode* NodeFactory::MakeNode(const std::string& name)
{
   const auto found = mEntries.find(name);
   return found != mEntries.end() ? found->second.create() : nullptr;
}

std::string NodeFactory::CategoryOf(const std::string& name) const
{
   const auto found = mEntries.find(name);
   return found != mEntries.end() ? found->second.category : std::string();
}

const std::vector<std::string>& NodeFactory::GetNodesInCategory(const std::string& category) const
{
   static const std::vector<std::string> kNone;
   const auto found = mNamesByCategory.find(category);
   return found != mNamesByCategory.end() ? found->second : kNone;
}

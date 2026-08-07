#include "NodeFactory.h"

#include <cstdio>

#include "INode.h"

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
      mCategoryOrder.push_back(category);
   list.push_back(name);
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

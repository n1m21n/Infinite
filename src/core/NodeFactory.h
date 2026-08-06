#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

class INode;

// Module registry, ported from BespokeSynth's ModuleFactory. Registration is
// manual/static (no reflection): most node classes supply a static Create(),
// wired in via REGISTER_NODE(...); parameterized node families (FilterNode,
// driven by a declarative FilterDef table) register a capturing lambda instead
// via RegisterNode() directly, so many spawnable node types can share one C++
// class without needing one Create() per variant.
class NodeFactory
{
public:
   typedef std::function<INode*()> CreateNodeFn;

   struct NodeInfo
   {
      std::string name;
      std::string category;
      CreateNodeFn createFn;
   };

   static NodeFactory& Instance();

   void Register(const std::string& name, CreateNodeFn createFn, const std::string& category);
   INode* MakeNode(const std::string& name);

   // category -> ordered list of node names, in registration order (used to
   // build the spawn menu grouped Source/Effects/Compositing/Text/Output).
   const std::vector<std::string>& GetCategories() const { return mCategoryOrder; }
   const std::vector<std::string>& GetNodesInCategory(const std::string& category) const;

private:
   std::map<std::string, NodeInfo> mFactoryMap;
   std::map<std::string, std::vector<std::string>> mByCategory;
   std::vector<std::string> mCategoryOrder;
};

#define REGISTER_NODE(klass, name, category) \
   NodeFactory::Instance().Register(#name, []() -> INode* { return klass::Create(); }, category);

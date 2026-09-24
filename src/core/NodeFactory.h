#pragma once

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class INode;

// Name -> constructor registry for every spawnable node type.
//
// Each type registers exactly once at startup, under the stable name that
// patch files store and a spawn-menu category. Most classes register through
// REGISTER_NODE with their static Create(); table-driven families (FilterNode,
// one registration per FilterDef row) call Register() with a capturing lambda,
// so a single C++ class can back many spawnable types.
//
// Registration order is preserved per category, because it is the order the
// spawn menu lists nodes in.
class NodeFactory
{
public:
   typedef std::function<INode*()> CreateNodeFn;

   static NodeFactory& Instance();

   // First registration of a name wins; a repeat is refused and recorded in
   // DuplicateNames() rather than silently replacing the original.
   void Register(const std::string& name, CreateNodeFn createFn, const std::string& category);

   // A fresh node of the named type, or nullptr when the name is unknown.
   INode* MakeNode(const std::string& name);

   // The category a node type was registered under, or "" when the name is unknown.
   std::string CategoryOf(const std::string& name) const;

   // Categories in the order their first node registered.
   const std::vector<std::string>& GetCategories() const { return mCategoryOrder; }

   // Node names in `category`, in registration order. Empty for an unknown category.
   const std::vector<std::string>& GetNodesInCategory(const std::string& category) const;

   // Names registered more than once. Always a bug: the second registration
   // was dropped, so whatever it meant to spawn is unreachable by that name.
   const std::vector<std::string>& DuplicateNames() const { return mDuplicates; }

private:
   struct Entry
   {
      std::string category;
      CreateNodeFn create;
   };

   std::unordered_map<std::string, Entry> mEntries;
   std::map<std::string, std::vector<std::string>> mNamesByCategory;
   std::vector<std::string> mCategoryOrder;
   std::vector<std::string> mDuplicates;
};

#define REGISTER_NODE(klass, name, category) \
   NodeFactory::Instance().Register(#name, []() -> INode* { return klass::Create(); }, category);

#pragma once
// What the chat knows about the open window: every node, and for each its params with their labels,
// ranges and options. The app fills it from the same data `infinite params --full` returns; the grammar
// and the reply links read it. Plain data, no app types, so tests build one by hand.
#include <string>
#include <vector>

namespace chat
{
   struct CatalogParam
   {
      std::string key;
      std::string label;
      std::string value;  // as saved
      std::string kind;   // "float", "int", "bool", "enum", or "" when no control was drawn
      float min = 0.0f, max = 1.0f;
      std::vector<std::string> options;
      bool modulatable = false;
      bool modulated = false;
      bool expression = false;
   };

   // Bookkeeping a node saves beside its params (a Field node's param id map): never offered, linked or reported.
   inline bool IsInternal(const CatalogParam& p) { return p.key.rfind("field_", 0) == 0; }

   // A value as a person reads it: enums by option name, bools as on/off, numbers to 3 significant digits.
   std::string ShownValue(const CatalogParam& p);

   struct CatalogNode
   {
      int index = -1;
      unsigned long long uid = 0;  // the node's own id: kept across undo and reload, when an index may move
      std::string type;
      std::string title;
      std::string category;
      bool modulator = false;  // drawn in the dropdown after `with` first
      std::vector<CatalogParam> params;
      const CatalogParam* Find(const std::string& keyOrLabel) const;
   };

   struct CatalogType
   {
      std::string name;
      std::string category;
   };

   struct Catalog
   {
      std::vector<CatalogNode> nodes;
      std::vector<CatalogType> types;
      std::vector<int> selected;  // node indices, picked first in the `@` dropdown
      const CatalogNode* Node(int index) const;
      // The shortest name that finds exactly this node in `infinite`: its title when that is one
      // word and unique, else its index.
      std::string CliRef(int index) const;
   };

   // Lower case, ASCII only (titles and keys are ASCII).
   std::string Lower(const std::string& s);
}

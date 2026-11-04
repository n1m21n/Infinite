#include "ChatCatalog.h"

#include <cctype>

namespace chat
{
   std::string Lower(const std::string& s)
   {
      std::string out = s;
      for (char& c : out)
         c = (char)std::tolower((unsigned char)c);
      return out;
   }

   const CatalogParam* CatalogNode::Find(const std::string& keyOrLabel) const
   {
      const std::string want = Lower(keyOrLabel);
      for (const CatalogParam& p : params)
         if (Lower(p.key) == want)
            return &p;
      for (const CatalogParam& p : params)
         if (!p.label.empty() && Lower(p.label) == want)
            return &p;
      return nullptr;
   }

   const CatalogNode* Catalog::Node(int index) const
   {
      for (const CatalogNode& n : nodes)
         if (n.index == index)
            return &n;
      return nullptr;
   }

   std::string Catalog::CliRef(int index) const
   {
      const CatalogNode* n = Node(index);
      if (n == nullptr || n->title.empty() || n->title.find_first_of(" \t'\"") != std::string::npos)
         return std::to_string(index);
      if (std::isdigit((unsigned char)n->title[0]))
         return std::to_string(index);
      int same = 0;
      for (const CatalogNode& other : nodes)
         same += other.title == n->title;  // `infinite` tries titles before types
      return same == 1 ? n->title : std::to_string(index);
   }
}

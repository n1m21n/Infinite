#include <cmath>
#include "ChatCatalog.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

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

   std::string ShownValue(const CatalogParam& p)
   {
      if (p.kind == "enum")
      {
         const int i = std::atoi(p.value.c_str());
         if (i >= 0 && i < (int)p.options.size())
            return p.options[(size_t)i];
      }
      if (p.kind == "bool")
         return p.value == "1" ? "on" : "off";
      if (p.kind == "float" || p.kind == "int")
      {
         char* end = nullptr;
         const double v = std::strtod(p.value.c_str(), &end);
         if (end != p.value.c_str() && *end == '\0')
         {
            char buf[32];
            // Three significant digits, but never an exponent: 12000 Hz reads as 12000, not 1.2e+04.
            if (std::fabs(v) >= 1000.0)
               std::snprintf(buf, sizeof buf, "%.0f", v);
            else
               std::snprintf(buf, sizeof buf, "%.3g", v);
            return buf;
         }
      }
      return p.value;
   }
}

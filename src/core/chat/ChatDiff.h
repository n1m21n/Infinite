#pragma once
// What a chat turn changed in the patch (docs/plans/agent-collab/README.md C6): the catalog and the
// cables before the turn against after it, in the chat's own words with `@title#index[.key]` links,
// so the reply links draw them. A long or multi-line text param (code) comes back as its own block.
#include "ChatCatalog.h"

#include <string>
#include <vector>

namespace chat
{
   struct CatalogLink
   {
      int src = -1, srcOutput = 0;
      int dst = -1;
      int dstSlot = -1;  // input slot, or -1 when the cable lands on a param or colour pin
      std::string dstPin;  // "p3" param pin, "c1" colour pin, "" input slot
      bool operator==(const CatalogLink& o) const
      {
         return src == o.src && srcOutput == o.srcOutput && dst == o.dst && dstSlot == o.dstSlot && dstPin == o.dstPin;
      }
   };

   struct Snapshot
   {
      Catalog catalog;
      std::vector<CatalogLink> links;
   };

   struct ChangeLine
   {
      std::string text;  // one line, links in `@title#index[.key]` form
      std::string code;  // set when the change is a body of code; drawn as a code block under the line
   };

   std::vector<ChangeLine> Diff(const Snapshot& before, const Snapshot& after);
}

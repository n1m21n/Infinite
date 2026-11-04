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
      // A value change: which param, and the value as saved right after the turn.
      int node = -1;
      std::string key;
      std::string after;
   };

   std::vector<ChangeLine> Diff(const Snapshot& before, const Snapshot& after);

   // What a value change's param reads now, when something has moved it since the turn (an edit,
   // randomize, a cable), so the card does not claim a value the canvas no longer shows. Empty when
   // it still holds, when the param is modulated (it moves by design) or when it is gone.
   std::string Drift(const ChangeLine& line, const Catalog& now);
}

#pragma once
// Agent text as the chat shows it (docs/plans/agent-collab/README.md C3): paragraphs, bullets,
// headings, code and pictures, with every node and param the agent names turned into a link.
// The agent writes `@<title>#<index>` and `@<title>#<index>.<key>` (the live-session skill's reply
// rule); a backticked node title also links, and a bare key links when its node is named in the
// same paragraph.
#include "ChatCatalog.h"

#include <string>
#include <vector>

namespace chat
{
   struct Seg
   {
      enum class Kind { Text, Bold, Code, Node, Param, Missing };
      Kind kind = Kind::Text;
      std::string text;  // what to draw (Node: the title, Param: the label or key)
      int node = -1;
      std::string key;
   };

   struct Block
   {
      enum class Kind { Para, Bullet, Heading, Code, Image };
      Kind kind = Kind::Para;
      std::vector<Seg> segs;  // Para, Bullet, Heading
      std::string text;       // Code: the lines; Image: the path
   };

   std::vector<Block> ParseReply(const std::string& text, const Catalog& catalog);

   // A picture file the agent pointed at: ends in .png .jpg .jpeg .gif .bmp or .webp.
   bool IsImagePath(const std::string& path);
}

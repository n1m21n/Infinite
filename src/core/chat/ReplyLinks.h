#pragma once
// Agent text as the chat shows it (docs/plans/agent-collab/README.md C3): paragraphs, bullets,
// headings, code and pictures, with every node and param the agent names turned into a link.
// The agent writes `@<title>#<index>` and `@<title>#<index>.<key>` (the live-session skill's reply
// rule); a backticked node title also links, and a bare key links when its node is named in the
// same paragraph. A markdown link to `verb:arg` is a button (Seg::Kind::Action).
#include "ChatCatalog.h"

#include <string>
#include <vector>

namespace chat
{
   struct Seg
   {
      enum class Kind { Text, Bold, Code, Node, Param, Missing, Action };
      Kind kind = Kind::Text;
      std::string text;  // what to draw (Node: the title, Param: the label or key, Action: the label)
      int node = -1;
      std::string key;
      // Action: a button the agent offers, written `[label](verb:arg)`. verb is add, set, delete,
      // undo, go or open; arg is a node type (add), `@node#i.key=value` (set), `@node#i[.key]`
      // (delete, go) or a panel name (open). node and key are filled from arg when it names them.
      std::string verb;
      std::string arg;
      bool dim = false;  // Text drawn in the quiet tint (a side note the panel adds, never parsed)
   };

   struct Block
   {
      enum class Kind { Para, Bullet, Heading, Code, Image, Audio, Video, File };
      Kind kind = Kind::Para;
      std::vector<Seg> segs;  // Para, Bullet, Heading
      std::string text;       // Code: the lines; Image, Audio, Video, File: the path
   };

   std::vector<Block> ParseReply(const std::string& text, const Catalog& catalog);

   // A picture file the agent pointed at: ends in .png .jpg .jpeg .gif .bmp or .webp.
   bool IsImagePath(const std::string& path);
   // A sound file the reply points at on its own line (wav, mp3, flac, ogg, aiff, m4a).
   bool IsAudioPath(const std::string& path);
   // A movie file by absolute path on its own line (mp4, mov, webm, mkv, m4v).
   bool IsVideoPath(const std::string& path);
   // Any other file an agent may have made, by absolute path on its own line (video, patch, code, archive, text, MIDI, model).
   bool IsFilePath(const std::string& path);
}

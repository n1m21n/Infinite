// NodeFinder: which nodes fit a plain-words ask ("blur the edges", "drum loop", "react to the bass"), ranked from the
// node guide cards, so an agent picks from the whole library instead of the handful its examples name.
#pragma once
#include <string>
#include <vector>

namespace chat
{
   struct NodeHit
   {
      std::string name;    // the card's "### Name", or the type for a node with no card
      std::string type;    // the node type to `add` ("Wavetable" for the Wavetable Synth card); "" when no type matched
      std::string kind;    // "SOURCE · IMAGE OUT"
      std::string purpose; // the card's first sentence after the kind
      int score = 0;
   };

   // Best first, at most `limit`; empty when no node shares a word with the ask. Ranks the guide's node cards together
   // with every node type's own control words (NodeVocab.gen.h, scripts/node_vocab.py); a type with no card is ranked
   // on its name, category and those words.
   std::vector<NodeHit> FindNodes(const std::string& ask, const std::string& guide, size_t limit = 8);

   // Everything known about one node, by card name or type: its guide card (what it does, Try, Chain), its in-app help
   // text, its control words and the type to add. Works for nodes with no card too; "" when no node has that name.
   std::string NodeDoc(const std::string& name, const std::string& guide);
}

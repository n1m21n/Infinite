#pragma once

#include <functional>
#include <string>
#include <vector>

class INode;

// Patch files: the whole graph written to disk and read back.
//
// The format is line-based text rather than JSON, for two reasons. It stays
// readable and diffable, which matters when a patch is the user's actual work;
// and it degrades gracefully - an unknown key or a node type that no longer
// exists is skipped with a warning instead of failing the whole load.
//
//   infinite-patch 1
//   node <index> <category> <type name to end of line>
//     pos <x> <y>
//     flags <showParams> <bypassed> <showMiniViewport>
//     f <name> <value>          float
//     i <name> <value>          int
//     b <name> <0|1>            bool
//     c <name> <r> <g> <b>      colour
//     s <name> <text to end of line>
//   end
//   cable <dstIndex> <dstSlot> <srcIndex>
//   geo <dstIndex> <dstSlot> <srcIndex>
//   mod <dstIndex> <dstParam> <srcIndex> <srcOutput>
//   pal <dstIndex> <dstColor> <srcIndex> <srcSwatch>
//
// Names may contain spaces, so anything free-form is always last on its line.
namespace Patch
{
   struct NodeRecord
   {
      int index = 0;
      std::string category;
      std::string typeName;
      float x = 0.0f, y = 0.0f;
      bool showParams = false;
      bool bypassed = false;
      bool showMiniViewport = false;
      // Raw key/value lines, replayed into the node through its ParamVisitor.
      std::vector<std::pair<std::string, std::string>> params;
   };

   struct CableRecord
   {
      int dstIndex = 0;
      int dstSlot = 0;
      int srcIndex = 0;
   };

   struct ModRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      int srcIndex = 0;
      int srcOutput = 0;
   };

   // A palette node driving one colour swatch on another node.
   struct PaletteRecord
   {
      int dstIndex = 0;
      int dstColor = 0;
      int srcIndex = 0;
      int srcSwatch = 0;
   };

   struct Data
   {
      std::vector<NodeRecord> nodes;
      std::vector<CableRecord> cables;   // image cables
      std::vector<CableRecord> geometry; // geometry, camera and light pins
      std::vector<ModRecord> modulation;
      std::vector<PaletteRecord> palette;
   };

   bool Write(const std::string& path, const Data& data, std::string& outError);
   bool Read(const std::string& path, Data& outData, std::string& outError);

   // Applies saved parameters to a node, and collects them from one.
   void SaveParams(INode* node, std::vector<std::pair<std::string, std::string>>& out);
   void LoadParams(INode* node, const std::vector<std::pair<std::string, std::string>>& in);

   // Most-recently-opened list, persisted next to the app's other preferences.
   const std::vector<std::string>& Recents();
   void NoteRecent(const std::string& path);
   void LoadRecents();
   void SaveRecents();
}

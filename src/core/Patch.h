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
//     flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams>
//     f <name> <value>          float
//     i <name> <value>          int
//     b <name> <0|1>            bool
//     c <name> <r> <g> <b>      colour
//     s <name> <text to end of line>
//   end
//   cable <dstIndex> <dstSlot> <srcIndex>
//   geo <dstIndex> <dstSlot> <srcIndex>
//   aud <dstIndex> <dstSlot> <srcIndex>
//   note <dstIndex> <dstSlot> <srcIndex>
//   mod <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre> [<lo> <hi> [<enabled>]]
//     polarity/depth/centre are trailing additions (per-binding modulation
//     polarity - see Modulation::Source): missing on older patches, where
//     >>'s failed-extraction behaviour leaves them at their defaults
//     (polarity 0 = absolute, depth 1.0, centre 0.0), i.e. today's override
//     behaviour, unchanged.
//     lo/hi are a further trailing addition (the destination-units range a
//     binding writes into its target - see Modulation::Source::lo/hi):
//     missing on any patch saved before this field existed, in which case
//     ModRecord::hasRange is left false and the range is derived from
//     polarity/depth/centre the first time the destination is drawn (see
//     Modulation::ResolvedSourceFor) rather than read from the file.
//     enabled is the last, and only ever written alongside lo/hi (never on
//     its own, since the tokens are positional and a lone enabled token
//     would decode as lo) - missing on any older patch, where it defaults
//     to true, matching a binding that has always been written.
//   pal <dstIndex> <dstColor> <srcIndex> <srcSwatch>
//   expr <dstIndex> <dstParam> <expression text to end of line>
//   glob <name> <expression text to end of line>
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
      bool showAdvancedParams = false; // audio nodes only, see GraphNode.h
      // Raw key/value lines, replayed into the node through its ParamVisitor.
      std::vector<std::pair<std::string, std::string>> params;
   };

   struct CableRecord
   {
      int dstIndex = 0;
      int dstSlot = 0;
      int srcIndex = 0;
      // Which of the source's note outputs this cable reads (NoteCable::
      // GetOutputSlot()). Unused (always 0) for cable/geo/aud records - only
      // Note Router has more than one note output.
      int srcOutput = 0;
   };

   struct ModRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      int srcIndex = 0;
      int srcOutput = 0;
      // See Modulation::Source::Polarity. 0 = absolute (default, today's
      // override behaviour), 1 = bipolar.
      int polarity = 0;
      float depth = 1.0f;
      float centre = 0.0f;
      // Destination-units range this binding writes into - see
      // Modulation::Source::lo/hi. hasRange is false only when this record
      // was read from a patch saved before lo/hi existed (no tokens present
      // on the "mod" line), in which case lo/hi below are left at their
      // defaults and must NOT be trusted - the range is derived from
      // polarity/depth/centre lazily instead. See the format comment above.
      float lo = 0.0f;
      float hi = 0.0f;
      bool hasRange = false;
      // See Modulation::Source::enabled. Only meaningful (and only ever
      // written) alongside lo/hi - see the format comment above.
      bool enabled = true;
   };

   // A palette node driving one colour swatch on another node.
   struct PaletteRecord
   {
      int dstIndex = 0;
      int dstColor = 0;
      int srcIndex = 0;
      int srcSwatch = 0;
   };

   // A typed algebraic expression driving one parameter directly, with no
   // modulator node involved - see Modulation::SetExpression.
   struct ExprRecord
   {
      int dstIndex = 0;
      int dstParam = 0;
      std::string text;
   };

   // One patch-wide named value an expression can read - see
   // core/ExprGlobals.h. Order is meaningful (a global may reference the ones
   // declared before it), so these are written and read as a list.
   struct GlobalRecord
   {
      std::string name;
      std::string expr;
   };

   struct Data
   {
      std::vector<NodeRecord> nodes;
      std::vector<CableRecord> cables;   // image cables
      std::vector<CableRecord> geometry; // geometry, camera and light pins
      std::vector<CableRecord> audio;    // audio cables
      std::vector<CableRecord> notes;    // note cables
      std::vector<ModRecord> modulation;
      std::vector<PaletteRecord> palette;
      std::vector<ExprRecord> expressions;
      std::vector<GlobalRecord> globals;
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

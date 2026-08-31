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
//   perfui <cellSize> <pageCount>
//   perfname <pageIndex> <page name to end of line>
//   perf <kind> <dstIndex> <dstParam> <dstParam2> <cellX> <cellY> <page> <colorR> <colorG> <colorB> <value> <value2> <boolName> <label to end of line>
//   perftarget <perfIndex> <dstIndex> <dstParam> <axis> <boolName>
//   transport <bpm> <tsNum> <tsDen> <key> <scale>
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
      // Which of the source's note/audio outputs this cable reads
      // (NoteCable::GetOutputSlot() / AudioCable::GetOutputSlot()). Unused
      // (always 0) for plain image cable records - only Note Router has more
      // than one note output, and only a node like VideoSourceNode (image +
      // audio on separate outputs) has more than one audio output.
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

   // One control on the performance matrix. `kind` is 0=Knob, 1=VFader, 2=HSlider,
   // 3=Toggle, 4=XYPad.
   // dstIndex/dstParam address a destination node parameter (-1 if unbound macro).
   // dstParam2 is used by the XY pad for its Y axis.
   struct PerfTarget
   {
      int dstIndex = -1;
      int dstParam = -1;
      std::string boolName;
   };

   struct PerfRecord
   {
      int   kind      = 0;
      int   dstIndex  = -1;
      int   dstParam  = -1;
      int   dstParam2 = -1;
      int   cellX = 0, cellY = 0;
      int   page  = 0;
      float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
      float value = 0.0f;
      float value2 = 0.0f;
      std::string boolName;   // toggle only
      std::string label;      // empty = inherit the source param's own name
      std::vector<PerfTarget> targets;
      std::vector<PerfTarget> targetsY;

      // MIDI mapping (0 = unbound device, channel 0-15, controller/note 0-127)
      int  midiDevice     = 0;
      int  midiChannel    = -1;
      int  midiController = -1;
      bool midiIsNote     = false;
      int  midiDeviceY    = 0;
      int  midiChannelY   = -1;
      int  midiControllerY = -1;
      bool midiIsNoteY    = false;
   };

   struct PerfLayoutRecord
   {
      int cellSize = 76;
      int pageCount = 1;
      std::vector<std::string> pageNames;
   };

   // Global transport state (docs/plans/audio/P3c-P3a2-design.md §0.2/§0.3) -
   // BPM, time signature, and key/scale all live on Transport rather than any
   // node, so without this record they silently reset to their defaults on
   // every load. Defaults here match Transport's own field initialisers, so
   // a patch saved before this record existed (or a `transport` line missing
   // some tokens, via >>'s failed-extraction behaviour) reads back unchanged.
   struct TransportRecord
   {
      float bpm = 120.0f;
      int timeSigNum = 4;
      int timeSigDen = 4;
      int key = 0;
      int scale = 0;
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
      std::vector<PerfRecord> performance;
      PerfLayoutRecord perfLayout;
      TransportRecord transport;
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

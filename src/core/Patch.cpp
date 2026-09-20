#include "Patch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "../arrange/ArrangeModel.h"
#include "INode.h"
#include "platform/AppPaths.h"

namespace Patch
{
namespace
{
   const char* kMagic = "infinite-patch";
   const int kVersion = 1;

   std::vector<std::string> sRecents;
   const size_t kMaxRecents = 10;

   std::string RecentsPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      if (dir.empty())
         return std::string();
      const std::string path = dir + "/Infinite.recents";
      // See ThemePath() in CategoryColors.cpp: same one-shot migration from the
      // flat pre-AppSupportDir location, so the recent-patch list survives the
      // upgrade instead of resetting on first launch.
      const std::string home = AppPaths::HomeDir();
      if (!home.empty())
         AppPaths::MigrateLegacyFile(home + "/Library/Application Support/Infinite.recents", path);
      return path;
   }

   // Written with %.9g so a float survives the round trip exactly rather than
   // drifting a little every time a patch is opened and saved again.
   std::string FloatToString(float v)
   {
      char buf[40];
      snprintf(buf, sizeof(buf), "%.9g", (double)v);
      return buf;
   }

   // Same idea as FloatToString, one extra significant digit for a double -
   // used for GestureSample::timeSec, where operator<<'s default 6-digit
   // precision would visibly coarsen a recording's timing on every re-save.
   std::string DoubleToString(double v)
   {
      char buf[48];
      snprintf(buf, sizeof(buf), "%.17g", v);
      return buf;
   }

   // Same escaping as Writer::Text/Reader::Text below, factored out for the
   // "expr" record - free text on a single line, outside of any node.
   std::string EscapeLine(const std::string& value)
   {
      std::string clean;
      for (char c : value)
      {
         if (c == '\\')
            clean += "\\\\";
         else if (c == '\n')
            clean += "\\n";
         else if (c != '\r')
            clean += c;
      }
      return clean;
   }

   std::string UnescapeLine(const std::string& raw)
   {
      std::string out;
      for (size_t i = 0; i < raw.size(); i++)
      {
         if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'n')
         {
            out += '\n';
            i++;
         }
         else if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\')
         {
            out += '\\';
            i++;
         }
         else
         {
            out += raw[i];
         }
      }
      return out;
   }

   class Writer : public ParamVisitor
   {
   public:
      std::vector<std::pair<std::string, std::string>> out;

      void Float(const char* name, float& value) override
      {
         out.push_back({ std::string("f ") + name, FloatToString(value) });
      }
      void Int(const char* name, int& value) override
      {
         out.push_back({ std::string("i ") + name, std::to_string(value) });
      }
      void Bool(const char* name, bool& value) override
      {
         out.push_back({ std::string("b ") + name, value ? "1" : "0" });
      }
      void Text(const char* name, std::string& value) override
      {
         // A newline cannot go in raw - the format is one parameter per line,
         // so it would be read back as the start of the next parameter. It is
         // escaped rather than folded to a space because for a Comment the line
         // breaks are the content, and because this same round trip is what
         // undo/redo and copy/paste use: folding lost a comment's shape on the
         // next undo, not just on the next save. Backslash is escaped too, so
         // unescaping on load has exactly one reading.
         out.push_back({ std::string("s ") + name, EscapeLine(value) });
      }
      void Color(const char* name, float rgb[3]) override
      {
         out.push_back({ std::string("c ") + name,
                         FloatToString(rgb[0]) + " " + FloatToString(rgb[1]) + " " +
                            FloatToString(rgb[2]) });
      }
   };

   class Reader : public ParamVisitor
   {
   public:
      std::map<std::string, std::string> values;

      // Every setter leaves the field untouched when the key is absent, so a
      // patch written before a parameter existed still loads and that parameter
      // simply keeps its default.
      void Float(const char* name, float& value) override
      {
         auto it = values.find(std::string("f ") + name);
         if (it != values.end())
            value = (float)atof(it->second.c_str());
      }
      void Int(const char* name, int& value) override
      {
         auto it = values.find(std::string("i ") + name);
         if (it != values.end())
            value = atoi(it->second.c_str());
      }
      void Bool(const char* name, bool& value) override
      {
         auto it = values.find(std::string("b ") + name);
         if (it != values.end())
            value = it->second == "1";
      }
      void Text(const char* name, std::string& value) override
      {
         auto it = values.find(std::string("s ") + name);
         if (it == values.end())
            return;
         // Undoes Writer::Text. Any other escape is left exactly as written:
         // patches saved before text was escaped at all never contain "\\n" or
         // "\\\\", so they read back unchanged.
         value = UnescapeLine(it->second);
      }
      void Color(const char* name, float rgb[3]) override
      {
         auto it = values.find(std::string("c ") + name);
         if (it == values.end())
            return;
         std::istringstream in(it->second);
         float r = rgb[0], g = rgb[1], b = rgb[2];
         in >> r >> g >> b;
         rgb[0] = r; rgb[1] = g; rgb[2] = b;
      }
   };
}

void SaveParams(INode* node, std::vector<std::pair<std::string, std::string>>& out)
{
   if (node == nullptr)
      return;
   Writer writer;
   node->VisitParams(writer);
   out = writer.out;
}

void LoadParams(INode* node, const std::vector<std::pair<std::string, std::string>>& in)
{
   if (node == nullptr)
      return;
   Reader reader;
   for (const auto& entry : in)
      reader.values[entry.first] = entry.second;
   node->VisitParams(reader);
}

bool Write(const std::string& path, const Data& data, std::string& outError)
{
   std::ofstream file(path);
   if (!file)
   {
      outError = "could not open " + path + " for writing";
      return false;
   }

   file << kMagic << " " << kVersion << "\n";
   for (const NodeRecord& node : data.nodes)
   {
      const float px = (std::isfinite(node.x) && std::abs(node.x) <= 1e6f && node.x > -2e9f) ? node.x : 0.0f;
      const float py = (std::isfinite(node.y) && std::abs(node.y) <= 1e6f && node.y > -2e9f) ? node.y : 0.0f;
      file << "node " << node.index << " " << node.category << " " << node.typeName << "\n";
      // Its own line, not an `s uid` param: FieldGraphNode already writes an
      // unrelated `s uid <hex>` param and the two would collide on load.
      if (node.uid != 0)
         file << "  uid " << node.uid << "\n";
      file << "  pos " << FloatToString(px) << " " << FloatToString(py) << "\n";
      file << "  flags " << (node.showParams ? 1 : 0) << " " << (node.bypassed ? 1 : 0) << " "
           << (node.showMiniViewport ? 1 : 0) << " " << (node.showAdvancedParams ? 1 : 0) << "\n";
      for (const auto& p : node.params)
         file << "  " << p.first << " " << p.second << "\n";
      file << "end\n";
   }
   // srcOutput is a later addition (build step 11, §5.3 - FieldPixelNode's
   // aux texture output is the first image source with more than one image
   // output): always written now, defaults to 0 via >>'s failed-extraction
   // behaviour on any patch saved before this field existed - same pattern
   // as "note"'s srcOutput and "mod"'s lo/hi/enabled below.
   for (const CableRecord& c : data.cables)
      file << "cable " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << " " << c.srcOutput << "\n";
   for (const CableRecord& c : data.geometry)
      file << "geo " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << " " << c.srcOutput << "\n";
   for (const CableRecord& c : data.audio)
      file << "aud " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << "\n";
   for (const CableRecord& c : data.notes)
      file << "note " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << " " << c.srcOutput << "\n";
   for (const ModRecord& m : data.modulation)
   {
      file << "mod " << m.dstIndex << " " << m.dstParam << " "
           << m.srcIndex << " " << m.srcOutput << " "
           << m.polarity << " " << FloatToString(m.depth) << " " << FloatToString(m.centre);
      // Only ever omitted for a binding that was loaded from a pre-lo/hi
      // patch and never actually drawn a frame before being saved again
      // (so EnsureRange never ran) - see ModRecord::hasRange. Everything
      // else always has a resolved range by the time it's saved.
      // enabled is positional and trails lo/hi, so a disabled binding that
      // hasn't resolved a range yet still needs one written to carry
      // enabled at all - force hasRange so the tokens line up.
      const bool hasRange = m.hasRange || !m.enabled || std::abs(m.curve) > 0.0001f;
      if (hasRange)
      {
         file << " " << FloatToString(m.lo) << " " << FloatToString(m.hi);
         if (!m.enabled || std::abs(m.curve) > 0.0001f)
         {
            file << " " << (m.enabled ? "1" : "0");
            if (std::abs(m.curve) > 0.0001f)
               file << " " << FloatToString(m.curve);
         }
      }
      file << "\n";
   }
   for (const PaletteRecord& p : data.palette)
      file << "pal " << p.dstIndex << " " << p.dstColor << " "
           << p.srcIndex << " " << p.srcSwatch << "\n";
   for (const ExprRecord& e : data.expressions)
   {
      file << "expr " << e.dstIndex << " " << e.dstParam << " " << EscapeLine(e.text) << "\n";
      if (std::abs(e.curve) > 0.0001f)
         file << "exprcurve " << e.dstIndex << " " << e.dstParam << " " << FloatToString(e.curve) << "\n";
   }
   for (const GlobalRecord& g : data.globals)
      file << "glob " << g.name << " " << EscapeLine(g.expr) << "\n";
   if (data.perfLayout.cellSize != 76 || data.perfLayout.pageCount > 1 || !data.perfLayout.pageNames.empty())
   {
      // Names go on their own `perfname` lines, one per page, rather than as
      // trailing tokens here. EscapeLine escapes backslashes and newlines but
      // NOT spaces, and every default page name has one ("Page 1", "Master
      // FX", "<name> Copy"), so the old trailing-token form split a single
      // name into several on reload. Free-form text has to be last on its own
      // line - the same rule the rest of this format already follows.
      file << "perfui " << data.perfLayout.cellSize << " " << data.perfLayout.pageCount << "\n";
      for (size_t i = 0; i < data.perfLayout.pageNames.size(); i++)
         file << "perfname " << i << " " << EscapeLine(data.perfLayout.pageNames[i]) << "\n";
   }
   for (size_t i = 0; i < data.performance.size(); i++)
   {
      const PerfRecord& p = data.performance[i];
      std::string boolToken = p.boolName.empty() ? "-" : p.boolName;
      file << "perf " << p.kind << " " << p.dstIndex << " " << p.dstParam << " " << p.dstParam2 << " "
           << p.cellX << " " << p.cellY << " " << p.page << " "
           << FloatToString(p.colorR) << " " << FloatToString(p.colorG) << " " << FloatToString(p.colorB) << " "
           << FloatToString(p.value) << " " << FloatToString(p.value2) << " "
           << boolToken << " " << EscapeLine(p.label) << "\n";
      for (const auto& t : p.targets)
      {
         std::string bTok = t.boolName.empty() ? "-" : t.boolName;
         file << "perftarget " << i << " " << t.dstIndex << " " << t.dstParam << " 0 " << bTok << "\n";
      }
      for (const auto& t : p.targetsY)
      {
         std::string bTok = t.boolName.empty() ? "-" : t.boolName;
         file << "perftarget " << i << " " << t.dstIndex << " " << t.dstParam << " 1 " << bTok << "\n";
      }
      if (p.midiDevice != 0)
         file << "perfmidi " << i << " 0 " << p.midiDevice << " " << p.midiChannel << " " << p.midiController << " " << (p.midiIsNote ? 1 : 0) << "\n";
      if (p.midiDeviceY != 0)
         file << "perfmidi " << i << " 1 " << p.midiDeviceY << " " << p.midiChannelY << " " << p.midiControllerY << " " << (p.midiIsNoteY ? 1 : 0) << "\n";
   }
   file << "transport " << FloatToString(data.transport.bpm) << " "
        << data.transport.timeSigNum << " " << data.transport.timeSigDen << " "
        << data.transport.key << " " << data.transport.scale << "\n";
   for (const GestureRecord& g : data.gestures)
   {
      file << "gesture " << g.dstIndex << " " << g.dstParam << " "
           << FloatToString(g.speed) << " " << (g.hasRangeOverride ? 1 : 0) << " "
           << FloatToString(g.rangeLo) << " " << FloatToString(g.rangeHi) << " "
           << g.samples.size();
      for (const GestureSample& s : g.samples)
         file << " " << FloatToString(s.value) << " " << DoubleToString(s.timeSec) << " "
              << (s.startsNewGrab ? 1 : 0);
      file << "\n";
      if (std::abs(g.curve) > 0.0001f)
         file << "gesturecurve " << g.dstIndex << " " << g.dstParam << " " << FloatToString(g.curve) << "\n";
   }
   for (size_t i = 0; i < data.streams.size(); i++)
   {
      const StreamRecord& s = data.streams[i];
      file << "stream " << s.type << " " << s.blendMode << " " << FloatToString(s.opacity) << " "
           << FloatToString(s.gainDb) << " " << FloatToString(s.pan) << " "
           << (s.enabled ? 1 : 0) << " " << s.groupId << " " << EscapeLine(s.name) << "\n";
      // The stream's own id trails the line it has always had, so an older
      // build reading a newer patch still gets the lane (it just ignores the
      // extra token, which lands after the name and so is part of the name -
      // hence a separate line instead).
      if (s.id != 0)
         file << "streamid " << i << " " << s.id << "\n";
      // `cliptick`, not `clip`: the old tag's fields are seconds in fixed
      // positions and reinterpreting them as ticks would silently corrupt
      // every pre-tick patch. New tag, new grammar, old tag stays readable.
      for (const ClipRecord& c : s.clips)
         file << "cliptick " << i << " " << c.id << " " << c.startTick << " " << c.lengthTick << " "
              << c.srcUid << " " << c.srcOutput << " " << c.fadeInTick << " " << c.fadeOutTick << " "
              << FloatToString(c.gainDb) << " " << (c.enabled ? 1 : 0) << " " << c.groupId << " "
              << FloatToString(c.colorR) << " " << FloatToString(c.colorG) << " " << FloatToString(c.colorB) << " "
              << EscapeLine(c.name) << "\n";
      // Own lines, not cliptick fields: cliptick ends in a to-end-of-line
      // name, so nothing can be appended after it.
      if (s.mute || s.solo)
         file << "streammix " << i << " " << (s.mute ? 1 : 0) << " " << (s.solo ? 1 : 0) << "\n";
      if (s.rowHeight != 0.0f)
         file << "streamrowheight " << i << " " << FloatToString(s.rowHeight) << "\n";
      for (const ClipRecord& c : s.clips)
         if (c.blendMode > 0)
            file << "clipblend " << i << " " << c.id << " " << c.blendMode << "\n";
      for (const ClipRecord& c : s.clips)
         if (c.pan != 0.0f || c.pitch != 0.0f || !c.syncToTempo)
            file << "clipaudio " << i << " " << c.id << " " << FloatToString(c.pan) << " "
                 << FloatToString(c.pitch) << " " << (c.syncToTempo ? 1 : 0) << "\n";
      for (const ClipRecord& c : s.clips)
         if (c.colorBrightness != 0.0f || c.colorContrast != 0.0f || c.colorSaturation != 1.0f)
            file << "clipgrade " << i << " " << c.id << " " << FloatToString(c.colorBrightness) << " "
                 << FloatToString(c.colorContrast) << " " << FloatToString(c.colorSaturation) << "\n";
      for (const ClipRecord& c : s.clips)
         if (c.opacity != 1.0f)
            file << "clipopacity " << i << " " << c.id << " " << FloatToString(c.opacity) << "\n";
      // Variable-length, so it runs to end of line and nothing may be
      // appended after it - see the format comment in Patch.h.
      for (const ClipRecord& c : s.clips)
         if (!c.bypassedModParams.empty())
         {
            file << "clipmodbypass " << i << " " << c.id;
            for (int paramIndex : c.bypassedModParams)
               file << " " << paramIndex;
            file << "\n";
         }
      for (const ClipRecord& c : s.clips)
         if (!c.retrigger)
            file << "clipretrigger " << i << " " << c.id << " 0\n";
      for (const ClipRecord& c : s.clips)
         if (c.sampleDropped)
            file << "clipsample " << i << " " << c.id << " 1\n";
      // BPM sync (step 3): only written for a Sample with a non-default
      // sampleBpm or a real sourceDurationSeconds, same append-only,
      // own-line convention as clipretrigger/clipsample above - an older
      // reader just skips a tag it doesn't know.
      for (const ClipRecord& c : s.clips)
         if (c.sampleDropped && (c.sampleBpm != 120.0f || c.sourceDurationSeconds != 0.0f))
            file << "clipbpm " << i << " " << c.id << " " << FloatToString(c.sampleBpm) << " "
                 << FloatToString(c.sourceDurationSeconds) << "\n";
      // Frozen native-tempo reference (see Clip::origBpm's own comment) -
      // own tag, same append-only convention, written whenever it's a real
      // captured value (not the -1 load-time sentinel) so an unsynced clip's
      // playback ratio round-trips instead of resetting to "native speed"
      // on every reload.
      for (const ClipRecord& c : s.clips)
         if (c.sampleDropped && c.origBpm > 0.0f)
            file << "cliporigbpm " << i << " " << c.id << " " << FloatToString(c.origBpm) << "\n";
      // Split-derived source offset: own tag, same append-only convention -
      // only written when non-zero (i.e. the clip is a split-off right
      // half) so an unsplit Sample's patch line stays exactly as it was.
      for (const ClipRecord& c : s.clips)
         if (c.sampleDropped && c.sourceOffsetSeconds != 0.0f)
            file << "clipsrcoffset " << i << " " << c.id << " " << FloatToString(c.sourceOffsetSeconds) << "\n";
   }
   for (const MarkerRecord& mk : data.markers)
      file << "marker " << mk.id << " " << mk.posTick << " " << mk.color << " " << EscapeLine(mk.name) << "\n";
   // Track groups: a new tag line, one per group, same shape as `marker` -
   // an older reader that doesn't know this tag simply skips the line
   // (see the "anything else is from a newer version" catch-all below).
   // The 4th token used to be `collapsed`, now unused (nested groups never
   // collapse) - written as a literal 0 placeholder so the token positions
   // stay append-only and an older reader (which still parses that slot as
   // collapsed, harmlessly) doesn't shift. `parentGroupId` is the new 5th
   // token, added after it for the same reason.
   for (const TrackGroupRecord& g : data.trackGroups)
      file << "trackgroup " << g.id << " " << g.color << " " << (g.enabled ? 1 : 0) << " "
           << (g.collapsed ? 1 : 0) << " " << g.parentGroupId << " " << EscapeLine(g.name) << "\n";
   {
      const ArrangeSettingsRecord& a = data.arrangeSettings;
      file << "arrange " << a.nextId << " " << a.timeDisplay << " " << a.snapDivision << " "
           << (a.snapTriplet ? 1 : 0) << " " << FloatToString(a.zoom) << " " << FloatToString(a.scroll) << " "
           << (a.loopEnabled ? 1 : 0) << " " << a.loopStart << " " << a.loopEnd << " " << a.dockSide << " "
           << a.renderWidth << " " << a.renderHeight << " " << a.renderFps << " " << a.renderSampleRate << " "
           << a.renderFormat << " " << a.renderRangeKind << " " << a.renderRangeStart << " "
           << a.renderRangeEnd << " " << a.renderAudioSource << " " << a.renderVideoSource << " "
           << EscapeLine(a.renderFolder) << "\n";
      // Separately-tagged, written only when non-default - same append-only
      // convention as clipretrigger/clipsample above, so an older reader
      // (which doesn't know this tag) just skips the line instead of
      // misparsing the fixed-order "arrange" line's trailing renderFolder.
      if (!a.importSyncToTempo)
         file << "arrangeimportsync 0\n";
   }
   if (data.viewport.open || !data.viewport.nodes.empty() || data.viewport.dock != 1 ||
       data.viewport.width != 320.0f || data.viewport.height != 260.0f)
   {
      file << "viewport " << (data.viewport.open ? 1 : 0) << " " << data.viewport.dock << " "
           << FloatToString(data.viewport.width) << " " << FloatToString(data.viewport.height);
      for (int nodeIdx : data.viewport.nodes)
         file << " " << nodeIdx;
      file << "\n";
   }

   if (!file.good())
   {
      outError = "write failed partway through " + path;
      return false;
   }
   return true;
}

bool Read(const std::string& path, Data& outData, std::string& outError)
{
   std::ifstream file(path);
   if (!file)
   {
      outError = "could not open " + path;
      return false;
   }

   std::string line;
   if (!std::getline(file, line))
   {
      outError = "file is empty";
      return false;
   }
   {
      std::istringstream header(line);
      std::string magic;
      int version = 0;
      header >> magic >> version;
      if (magic != kMagic)
      {
         outError = "not an Infinite patch";
         return false;
      }
      if (version > kVersion)
      {
         outError = "patch was written by a newer version of Infinite";
         return false;
      }
   }

   NodeRecord current;
   bool inNode = false;

   // Legacy `clip` lines are seconds; converting them needs the file's bpm,
   // which the `transport` line may carry *after* them. So they are parked
   // here and folded in once the whole file has been read.
   struct LegacyClip
   {
      int stream = -1;
      double startSeconds = 0.0, lengthSeconds = 1.0;
      int srcIndex = -1, srcOutput = 0;
      float fadeInSec = 0.0f, fadeOutSec = 0.0f, gainDb = 0.0f;
      float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
      std::string name;
   };
   std::vector<LegacyClip> legacyClips;
   bool sawArrangeLine = false;

   while (std::getline(file, line))
   {
      // Leading whitespace is cosmetic in the file, so strip it before parsing.
      size_t start = line.find_first_not_of(" \t");
      if (start == std::string::npos)
         continue;
      line = line.substr(start);

      std::istringstream in(line);
      std::string tag;
      in >> tag;

      if (tag == "node")
      {
         current = NodeRecord();
         in >> current.index >> current.category;
         std::getline(in, current.typeName);
         if (!current.typeName.empty() && current.typeName[0] == ' ')
            current.typeName.erase(0, 1);
         inNode = true;
      }
      else if (tag == "end")
      {
         if (inNode)
            outData.nodes.push_back(current);
         inNode = false;
      }
      else if (tag == "uid" && inNode)
      {
         in >> current.uid;
      }
      else if (tag == "pos" && inNode)
      {
         in >> current.x >> current.y;
         if (!std::isfinite(current.x) || std::abs(current.x) > 1e6f || current.x <= -2e9f) current.x = 0.0f;
         if (!std::isfinite(current.y) || std::abs(current.y) > 1e6f || current.y <= -2e9f) current.y = 0.0f;
      }
      else if (tag == "flags" && inNode)
      {
         // advanced defaults to 0 (via >>'s C++11 failed-extraction behaviour)
         // when reading a patch written before showAdvancedParams existed -
         // `in` is a fresh istringstream over just this line (see the
         // getline loop above), so a missing 4th token cannot corrupt any
         // later line's parsing the way it would on a shared whole-file stream.
         int show = 0, bypass = 0, miniViewport = 0, advanced = 0;
         in >> show >> bypass >> miniViewport >> advanced;
         current.showParams = show != 0;
         current.bypassed = bypass != 0;
         current.showMiniViewport = miniViewport != 0;
         current.showAdvancedParams = advanced != 0;
      }
      else if (inNode && (tag == "f" || tag == "i" || tag == "b" || tag == "c" || tag == "s"))
      {
         std::string name;
         in >> name;
         std::string value;
         std::getline(in, value);
         if (!value.empty() && value[0] == ' ')
            value.erase(0, 1);
         current.params.push_back({ tag + " " + name, value });
      }
      else if (tag == "cable" || tag == "geo")
      {
         CableRecord c;
         in >> c.dstIndex >> c.dstSlot >> c.srcIndex;
         // srcOutput is a later addition (build step 11, §5.3); missing on
         // older patches, where >>'s failed-extraction behaviour leaves it
         // at its default of 0 - every pre-step-11 image cable and every
         // geometry-slot binding only ever had output 0 anyway. A
         // modulator-input-slot binding (also stored as "geo" - see
         // ConnectGeometrySlot) with a non-zero source output on an old
         // patch predates this field entirely and was already being
         // silently truncated to output 0 on every prior save, so this is
         // strictly a fix, not a new regression.
         in >> c.srcOutput;
         if (tag == "cable")
            outData.cables.push_back(c);
         else
            outData.geometry.push_back(c);
      }
      else if (tag == "aud" || tag == "note")
      {
         CableRecord c;
         in >> c.dstIndex >> c.dstSlot >> c.srcIndex;
         if (tag == "aud")
            outData.audio.push_back(c);
         else
         {
            // srcOutput is a later addition (Note Router); missing on older
            // patches, where >>'s failed-extraction behaviour leaves it 0 -
            // every note source but Router only ever has output 0 anyway.
            in >> c.srcOutput;
            outData.notes.push_back(c);
         }
      }
      else if (tag == "mod")
      {
         // polarity/depth/centre are a later addition; missing on older
         // patches, where >>'s failed-extraction behaviour leaves these
         // initialised values in place - see the "flags" precedent above.
         ModRecord m;
         m.polarity = 0;
         m.depth = 1.0f;
         m.centre = 0.0f;
         in >> m.dstIndex >> m.dstParam >> m.srcIndex >> m.srcOutput >> m.polarity >> m.depth >> m.centre;
         // lo/hi are a later addition still; missing on any patch saved
         // before they existed (or a legacy binding this session never
         // resolved a range for - see the write site), where >>'s
         // failed-extraction behaviour leaves them at 0/0 and hasRange
         // false - the field comment on ModRecord::hasRange covers what
         // happens next (lazy derivation from polarity/depth/centre).
         m.hasRange = static_cast<bool>(in >> m.lo >> m.hi);
         // enabled trails lo/hi and is only ever written alongside them;
         // missing (any older patch, or a patch saved before this field
         // existed) leaves it at its default of true.
         int enabled = 1;
         m.enabled = !(in >> enabled) || enabled != 0;
         float curve = 0.0f;
         if (in >> curve)
            m.curve = curve;
         outData.modulation.push_back(m);
      }
      else if (tag == "pal")
      {
         PaletteRecord p;
         in >> p.dstIndex >> p.dstColor >> p.srcIndex >> p.srcSwatch;
         outData.palette.push_back(p);
      }
      else if (tag == "expr")
      {
         ExprRecord e;
         in >> e.dstIndex >> e.dstParam;
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         e.text = UnescapeLine(raw);
         outData.expressions.push_back(e);
      }
      else if (tag == "exprcurve")
      {
         int dstIndex = 0, dstParam = 0;
         float curve = 0.0f;
         if (in >> dstIndex >> dstParam >> curve)
         {
            for (ExprRecord& e : outData.expressions)
            {
               if (e.dstIndex == dstIndex && e.dstParam == dstParam)
               {
                  e.curve = curve;
                  break;
               }
            }
         }
      }
      else if (tag == "glob")
      {
         GlobalRecord g;
         in >> g.name;
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         g.expr = UnescapeLine(raw);
         if (!g.name.empty())
            outData.globals.push_back(g);
      }
      else if (tag == "perfui")
      {
         in >> outData.perfLayout.cellSize >> outData.perfLayout.pageCount;
         // Legacy (pre-`perfname`) form: names as trailing whitespace-
         // separated tokens. Still read so older patches keep whatever names
         // survived that encoding; anything written since is a perfname line.
         std::string nameToken;
         while (in >> nameToken)
            outData.perfLayout.pageNames.push_back(UnescapeLine(nameToken));
      }
      else if (tag == "perfname")
      {
         int page = -1;
         if (in >> page && page >= 0 && page < 1024)
         {
            std::string raw;
            std::getline(in, raw);
            if (!raw.empty() && raw[0] == ' ')
               raw.erase(0, 1);
            if ((int)outData.perfLayout.pageNames.size() <= page)
               outData.perfLayout.pageNames.resize(page + 1);
            outData.perfLayout.pageNames[page] = UnescapeLine(raw);
         }
      }
      else if (tag == "transport")
      {
         // Any missing trailing token (an older patch saved before this line
         // existed at all won't have the tag; a patch saved between adding
         // bpm/timesig and adding key/scale would be missing just those)
         // leaves the field at TransportRecord's own default via >>'s
         // failed-extraction behaviour, same precedent as "flags"/"mod" above.
         in >> outData.transport.bpm >> outData.transport.timeSigNum >> outData.transport.timeSigDen
            >> outData.transport.key >> outData.transport.scale;
      }
      else if (tag == "gesture")
      {
         GestureRecord g;
         int hasRange = 0;
         size_t count = 0;
         in >> g.dstIndex >> g.dstParam >> g.speed >> hasRange >> g.rangeLo >> g.rangeHi >> count;
         g.hasRangeOverride = hasRange != 0;
         // Stop as soon as a token fails to parse (a line truncated by manual
         // editing, say) rather than looping count times regardless - matches
         // the format's general "degrade gracefully" stance rather than
         // reading garbage into later samples.
         for (size_t i = 0; i < count && in; i++)
         {
            GestureSample s;
            int newGrab = 0;
            in >> s.value >> s.timeSec >> newGrab;
            if (!in)
               break;
            s.startsNewGrab = newGrab != 0;
            g.samples.push_back(s);
         }
         // Mirrors GestureRecorder::FinalizeSession/SetPlayback: a trace with
         // fewer than two samples has no movement to replay, so it is not a
         // playback loop and is dropped rather than kept as a no-op record.
         if (g.samples.size() >= 2)
            outData.gestures.push_back(std::move(g));
      }
      else if (tag == "gesturecurve")
      {
         int dstIndex = 0, dstParam = 0;
         float curve = 0.0f;
         if (in >> dstIndex >> dstParam >> curve)
         {
            for (GestureRecord& g : outData.gestures)
            {
               if (g.dstIndex == dstIndex && g.dstParam == dstParam)
               {
                  g.curve = curve;
                  break;
               }
            }
         }
      }
      else if (tag == "perf")
      {
         PerfRecord p;
         in >> p.kind >> p.dstIndex >> p.dstParam >> p.dstParam2
            >> p.cellX >> p.cellY >> p.page
            >> p.colorR >> p.colorG >> p.colorB;
         std::string tok1;
         if (in >> tok1)
         {
            char* endP = nullptr;
            float val1 = std::strtof(tok1.c_str(), &endP);
            if (endP != tok1.c_str() && *endP == '\0')
            {
               p.value = val1;
               std::string tok2;
               if (in >> tok2)
               {
                  float val2 = std::strtof(tok2.c_str(), &endP);
                  if (endP != tok2.c_str() && *endP == '\0')
                  {
                     p.value2 = val2;
                     in >> p.boolName;
                  }
                  else
                  {
                     p.boolName = tok2;
                  }
               }
            }
            else
            {
               p.boolName = tok1;
            }
         }
         if (p.boolName == "-")
            p.boolName.clear();
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         p.label = UnescapeLine(raw);
         outData.performance.push_back(p);
      }
      else if (tag == "perftarget")
      {
         int elemIdx = 0, dstIdx = -1, dstP = -1, axis = 0;
         std::string bTok;
         if (in >> elemIdx >> dstIdx >> dstP >> axis >> bTok)
         {
            if (elemIdx >= 0 && elemIdx < (int)outData.performance.size())
            {
               PerfTarget pt;
               pt.dstIndex = dstIdx;
               pt.dstParam = dstP;
               if (bTok != "-") pt.boolName = bTok;
               if (axis == 1)
                  outData.performance[elemIdx].targetsY.push_back(pt);
               else
                  outData.performance[elemIdx].targets.push_back(pt);
            }
         }
      }
      else if (tag == "perfmidi")
      {
         int elemIdx = 0, axis = 0, dev = 0, ch = -1, ctrl = -1, isNoteInt = 0;
         if (in >> elemIdx >> axis >> dev >> ch >> ctrl >> isNoteInt)
         {
            if (elemIdx >= 0 && elemIdx < (int)outData.performance.size())
            {
               if (axis == 1)
               {
                  outData.performance[elemIdx].midiDeviceY = dev;
                  outData.performance[elemIdx].midiChannelY = ch;
                  outData.performance[elemIdx].midiControllerY = ctrl;
                  outData.performance[elemIdx].midiIsNoteY = (isNoteInt != 0);
               }
               else
               {
                  outData.performance[elemIdx].midiDevice = dev;
                  outData.performance[elemIdx].midiChannel = ch;
                  outData.performance[elemIdx].midiController = ctrl;
                  outData.performance[elemIdx].midiIsNote = (isNoteInt != 0);
               }
            }
         }
      }
      else if (tag == "stream")
      {
         // Always pushed, even when malformed: a clip line refers to its
         // stream by position, so dropping one would shift every later clip
         // onto the wrong lane. Missing/garbage tokens leave defaults.
         StreamRecord s;
         in >> s.type >> s.blendMode >> s.opacity >> s.gainDb >> s.pan;
         // Trailing fields: a pre-groups patch's `stream` line has no more
         // numeric tokens here (next thing on the line is the escaped name),
         // so this extraction fails. Per C++11 that ZEROES the target on
         // failure - so `enabled` would land on false/disabled unless
         // explicitly restored to true here. groupId's zero-on-failure IS
         // the right default (0 = ungrouped), so it needs no such fixup.
         int enabled = 1;
         if (!(in >> enabled)) enabled = 1;
         in >> s.groupId;
         s.enabled = enabled != 0;
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         s.name = UnescapeLine(raw);
         if (s.type != kStreamVideo && s.type != kStreamAudio) s.type = kStreamVideo;
         if (s.blendMode < 0 || s.blendMode > 31) s.blendMode = 0;
         if (!std::isfinite(s.opacity)) s.opacity = 1.0f;
         s.opacity = std::clamp(s.opacity, 0.0f, 1.0f);
         if (!std::isfinite(s.gainDb)) s.gainDb = 0.0f;
         if (!std::isfinite(s.pan)) s.pan = 0.0f;
         s.pan = std::clamp(s.pan, -1.0f, 1.0f);
         outData.streams.push_back(std::move(s));
      }
      else if (tag == "streamid")
      {
         int streamIdx = -1;
         uint64_t id = 0;
         if (in >> streamIdx >> id && streamIdx >= 0 && streamIdx < (int)outData.streams.size())
            outData.streams[streamIdx].id = id;
      }
      else if (tag == "streammix")
      {
         int streamIdx = -1, mute = 0, solo = 0;
         if (in >> streamIdx >> mute >> solo && streamIdx >= 0 && streamIdx < (int)outData.streams.size())
         {
            outData.streams[streamIdx].mute = mute != 0;
            outData.streams[streamIdx].solo = solo != 0;
         }
      }
      else if (tag == "streamrowheight")
      {
         int streamIdx = -1;
         float rowHeight = 0.0f;
         if (in >> streamIdx >> rowHeight && streamIdx >= 0 && streamIdx < (int)outData.streams.size() &&
             std::isfinite(rowHeight))
            outData.streams[streamIdx].rowHeight = rowHeight;
      }
      else if (tag == "clipblend")
      {
         int streamIdx = -1, mode = 0;
         uint64_t clipId = 0;
         if (in >> streamIdx >> clipId >> mode && streamIdx >= 0 && streamIdx < (int)outData.streams.size() &&
             clipId != 0)
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
                  c.blendMode = (mode >= 0 && mode <= 31) ? mode : 0;
      }
      else if (tag == "clipaudio")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         float pan = 0.0f, pitch = 0.0f;
         int syncToTempo = 1;
         if (in >> streamIdx >> clipId >> pan >> pitch >> syncToTempo &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            if (!std::isfinite(pan)) pan = 0.0f;
            if (!std::isfinite(pitch)) pitch = 0.0f;
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
               {
                  c.pan = std::clamp(pan, -1.0f, 1.0f);
                  c.pitch = std::clamp(pitch, -24.0f, 24.0f);
                  c.syncToTempo = syncToTempo != 0;
               }
         }
      }
      else if (tag == "clipgrade")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         float brightness = 0.0f, contrast = 0.0f, saturation = 1.0f;
         if (in >> streamIdx >> clipId >> brightness >> contrast >> saturation &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            if (!std::isfinite(brightness)) brightness = 0.0f;
            if (!std::isfinite(contrast)) contrast = 0.0f;
            if (!std::isfinite(saturation)) saturation = 1.0f;
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
               {
                  c.colorBrightness = std::clamp(brightness, -1.0f, 1.0f);
                  c.colorContrast = std::clamp(contrast, -1.0f, 1.0f);
                  c.colorSaturation = std::clamp(saturation, 0.0f, 2.0f);
               }
         }
      }
      else if (tag == "clipmodbypass")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         if (in >> streamIdx >> clipId && streamIdx >= 0 &&
             streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            // Read to end of line. A negative index is not a param index
            // Modulation could ever have bound, so drop it rather than
            // carrying a value no lookup can match.
            std::vector<int> params;
            for (int paramIndex = 0; in >> paramIndex; )
               if (paramIndex >= 0)
                  params.push_back(paramIndex);
            std::sort(params.begin(), params.end());
            params.erase(std::unique(params.begin(), params.end()), params.end());
            if (!params.empty())
               for (ClipRecord& c : outData.streams[streamIdx].clips)
                  if (c.id == clipId)
                     c.bypassedModParams = params;
         }
      }
      else if (tag == "clipopacity")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         float opacity = 1.0f;
         if (in >> streamIdx >> clipId >> opacity &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            if (!std::isfinite(opacity)) opacity = 1.0f;
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
                  c.opacity = std::clamp(opacity, 0.0f, 1.0f);
         }
      }
      else if (tag == "clipretrigger")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         int retriggerVal = 1;
         if (in >> streamIdx >> clipId >> retriggerVal &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
               {
                  c.retrigger = (retriggerVal != 0);
               }
         }
      }
      else if (tag == "clipsample")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         int sampleVal = 0;
         if (in >> streamIdx >> clipId >> sampleVal &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
                  c.sampleDropped = (sampleVal != 0);
         }
      }
      else if (tag == "clipbpm")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         float sampleBpm = 120.0f, sourceDurationSeconds = 0.0f;
         if (in >> streamIdx >> clipId >> sampleBpm >> sourceDurationSeconds &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            if (!std::isfinite(sampleBpm) || sampleBpm <= 0.0f) sampleBpm = 120.0f;
            if (!std::isfinite(sourceDurationSeconds) || sourceDurationSeconds < 0.0f) sourceDurationSeconds = 0.0f;
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
               {
                  c.sampleBpm = sampleBpm;
                  c.sourceDurationSeconds = sourceDurationSeconds;
               }
         }
      }
      else if (tag == "cliporigbpm")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         float origBpm = -1.0f;
         if (in >> streamIdx >> clipId >> origBpm &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            if (!std::isfinite(origBpm) || origBpm <= 0.0f) origBpm = -1.0f;
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
                  c.origBpm = origBpm;
         }
      }
      else if (tag == "clipsrcoffset")
      {
         int streamIdx = -1;
         uint64_t clipId = 0;
         float sourceOffsetSeconds = 0.0f;
         if (in >> streamIdx >> clipId >> sourceOffsetSeconds &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() && clipId != 0)
         {
            if (!std::isfinite(sourceOffsetSeconds) || sourceOffsetSeconds < 0.0f) sourceOffsetSeconds = 0.0f;
            for (ClipRecord& c : outData.streams[streamIdx].clips)
               if (c.id == clipId)
                  c.sourceOffsetSeconds = sourceOffsetSeconds;
         }
      }
      else if (tag == "cliptick")
      {
         int streamIdx = -1;
         ClipRecord c;
         int enabled = 1;
         if (in >> streamIdx >> c.id >> c.startTick >> c.lengthTick >> c.srcUid &&
             streamIdx >= 0 && streamIdx < (int)outData.streams.size() &&
             c.startTick >= 0 && c.lengthTick > 0)
         {
            // Trailing settings: a missing token keeps ClipRecord's default
            // (C++11 failed-extraction), a garbage one reads as 0, so each is
            // sanitized below. Same forward-compat pattern as `cable`.
            in >> c.srcOutput >> c.fadeInTick >> c.fadeOutTick >> c.gainDb >> enabled >> c.groupId;
            c.enabled = enabled != 0;
            if (c.srcOutput < 0) c.srcOutput = 0;
            if (c.fadeInTick < 0) c.fadeInTick = 0;
            if (c.fadeOutTick < 0) c.fadeOutTick = 0;
            if (c.fadeInTick > c.lengthTick) c.fadeInTick = c.lengthTick;
            if (c.fadeOutTick > c.lengthTick) c.fadeOutTick = c.lengthTick;
            if (!std::isfinite(c.gainDb)) c.gainDb = 0.0f;
            if (in >> c.colorR >> c.colorG >> c.colorB) {}
            if (!std::isfinite(c.colorR)) c.colorR = 0.0f;
            if (!std::isfinite(c.colorG)) c.colorG = 0.0f;
            if (!std::isfinite(c.colorB)) c.colorB = 0.0f;
            c.colorR = std::clamp(c.colorR, 0.0f, 1.0f);
            c.colorG = std::clamp(c.colorG, 0.0f, 1.0f);
            c.colorB = std::clamp(c.colorB, 0.0f, 1.0f);
            std::string rawName;
            std::getline(in, rawName);
            if (!rawName.empty() && rawName[0] == ' ')
               rawName.erase(0, 1);
            c.name = UnescapeLine(rawName);
            outData.streams[streamIdx].clips.push_back(c);
         }
      }
      else if (tag == "clip")
      {
         // LEGACY seconds form. Parked, not stored: see legacyClips above.
         LegacyClip lc;
         int triggerMode = 0, loop = 0;
         float speed = 1.0f;
         if (in >> lc.stream >> lc.startSeconds >> lc.lengthSeconds >> lc.srcIndex &&
             lc.stream >= 0 && lc.stream < (int)outData.streams.size() &&
             std::isfinite(lc.startSeconds) && std::isfinite(lc.lengthSeconds) &&
             lc.startSeconds >= 0.0 && lc.lengthSeconds > 0.0)
         {
            // triggerMode/speed/loop are read and dropped - the engine never
            // used them (overhaul WP1). Phase 6 re-adds its own field when
            // retrigger is actually built.
            in >> lc.srcOutput >> triggerMode >> lc.fadeInSec >> lc.fadeOutSec >> lc.gainDb >> speed >> loop;
            if (lc.srcOutput < 0) lc.srcOutput = 0;
            const float len = (float)lc.lengthSeconds;
            if (!std::isfinite(lc.fadeInSec)) lc.fadeInSec = 0.0f;
            if (!std::isfinite(lc.fadeOutSec)) lc.fadeOutSec = 0.0f;
            lc.fadeInSec = std::clamp(lc.fadeInSec, 0.0f, len);
            lc.fadeOutSec = std::clamp(lc.fadeOutSec, 0.0f, len);
            if (!std::isfinite(lc.gainDb)) lc.gainDb = 0.0f;
            if (in >> lc.colorR >> lc.colorG >> lc.colorB) {}
            if (!std::isfinite(lc.colorR)) lc.colorR = 0.0f;
            if (!std::isfinite(lc.colorG)) lc.colorG = 0.0f;
            if (!std::isfinite(lc.colorB)) lc.colorB = 0.0f;
            lc.colorR = std::clamp(lc.colorR, 0.0f, 1.0f);
            lc.colorG = std::clamp(lc.colorG, 0.0f, 1.0f);
            lc.colorB = std::clamp(lc.colorB, 0.0f, 1.0f);
            std::string rawName;
            std::getline(in, rawName);
            if (!rawName.empty() && rawName[0] == ' ')
               rawName.erase(0, 1);
            lc.name = UnescapeLine(rawName);
            legacyClips.push_back(lc);
         }
      }
      else if (tag == "marker")
      {
         MarkerRecord mk;
         if (in >> mk.id >> mk.posTick >> mk.color && mk.posTick >= 0)
         {
            std::string raw;
            std::getline(in, raw);
            if (!raw.empty() && raw[0] == ' ')
               raw.erase(0, 1);
            mk.name = UnescapeLine(raw);
            outData.markers.push_back(mk);
         }
      }
      else if (tag == "trackgroup")
      {
         TrackGroupRecord g;
         int enabled = 1;
         if (in >> g.id >> g.color >> enabled && g.id != 0)
         {
            g.enabled = enabled != 0;
            int collapsed = 0;
            in >> collapsed;
            g.collapsed = (collapsed != 0);
            in >> g.parentGroupId;
            std::string raw;
            std::getline(in, raw);
            if (!raw.empty() && raw[0] == ' ')
               raw.erase(0, 1);
            g.name = UnescapeLine(raw);
            outData.trackGroups.push_back(g);
         }
      }
      else if (tag == "arrange")
      {
         ArrangeSettingsRecord& a = outData.arrangeSettings;
         int triplet = 0, loopOn = 0;
         in >> a.nextId >> a.timeDisplay >> a.snapDivision >> triplet >> a.zoom >> a.scroll
            >> loopOn >> a.loopStart >> a.loopEnd >> a.dockSide
            >> a.renderWidth >> a.renderHeight >> a.renderFps >> a.renderSampleRate
            >> a.renderFormat >> a.renderRangeKind >> a.renderRangeStart >> a.renderRangeEnd
            >> a.renderAudioSource >> a.renderVideoSource;
         a.snapTriplet = triplet != 0;
         a.loopEnabled = loopOn != 0;
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         a.renderFolder = UnescapeLine(raw);
         sawArrangeLine = true;
      }
      else if (tag == "arrangeimportsync")
      {
         int v = 1;
         in >> v;
         outData.arrangeSettings.importSyncToTempo = v != 0;
      }
      else if (tag == "viewport")
      {
         int open = 0, dock = 1;
         float width = 320.0f, height = 260.0f;
         if (in >> open >> dock >> width >> height)
         {
            outData.viewport.open = (open != 0);
            outData.viewport.dock = std::clamp(dock, 0, 3);
            if (std::isfinite(width) && width > 0.0f)
               outData.viewport.width = width;
            if (std::isfinite(height) && height > 0.0f)
               outData.viewport.height = height;
            int nodeIdx = -1;
            while (in >> nodeIdx)
            {
               outData.viewport.nodes.push_back(nodeIdx);
            }
         }
      }
      // Anything else is from a newer version and is deliberately ignored.
   }

   // Legacy seconds -> ticks, now that the file's own transport bpm is known
   // (the `transport` line may appear after the clips). Done here rather than
   // at parse time so a pre-tick patch lands on exactly the bar/beat it played
   // at, instead of on whatever the app's current tempo happens to be.
   if (!legacyClips.empty())
   {
      const double bpm = (std::isfinite(outData.transport.bpm) && outData.transport.bpm > 0.0f)
                             ? (double)outData.transport.bpm
                             : 120.0;
      auto toTicks = [bpm](double sec) -> int64_t {
         if (!(sec > 0.0)) return 0;
         return (int64_t)llround(sec * bpm / 60.0 * (double)Arrange::kPPQ);
      };
      for (const LegacyClip& lc : legacyClips)
      {
         if (lc.stream < 0 || lc.stream >= (int)outData.streams.size())
            continue;
         ClipRecord c;
         c.startTick = toTicks(lc.startSeconds);
         c.lengthTick = std::max<int64_t>(1, toTicks(lc.lengthSeconds));
         c.legacySrcIndex = lc.srcIndex;
         c.srcOutput = lc.srcOutput;
         c.fadeInTick = std::clamp<int64_t>(toTicks(lc.fadeInSec), 0, c.lengthTick);
         c.fadeOutTick = std::clamp<int64_t>(toTicks(lc.fadeOutSec), 0, c.lengthTick);
         c.gainDb = lc.gainDb;
         c.colorR = lc.colorR;
         c.colorG = lc.colorG;
         c.colorB = lc.colorB;
         c.name = lc.name;
         outData.streams[lc.stream].clips.push_back(c);
      }
      // Clips were written in lane order but the tick rounding above can make
      // two of them abut exactly; sorting here keeps the model's sorted
      // invariant true before anything else looks at them.
      for (StreamRecord& st : outData.streams)
         std::stable_sort(st.clips.begin(), st.clips.end(),
                          [](const ClipRecord& a, const ClipRecord& b) { return a.startTick < b.startTick; });
   }
   std::stable_sort(outData.markers.begin(), outData.markers.end(),
                    [](const MarkerRecord& a, const MarkerRecord& b) { return a.posTick < b.posTick; });
   if (sawArrangeLine)
   {
      ArrangeSettingsRecord& a = outData.arrangeSettings;
      if (a.timeDisplay != 0 && a.timeDisplay != 1) a.timeDisplay = 0;
      if (a.snapDivision < 0 || a.snapDivision > 64) a.snapDivision = 4; // 0 = snap off (WP6)
      if (!std::isfinite(a.zoom) || a.zoom <= 0.0f) a.zoom = 1.0f;
      if (!std::isfinite(a.scroll) || a.scroll < 0.0f) a.scroll = 0.0f;
      if (a.loopStart < 0) a.loopStart = 0;
      if (a.loopEnd < a.loopStart) a.loopEnd = a.loopStart;
      if (a.dockSide != 0 && a.dockSide != 1) a.dockSide = 0;
      a.renderWidth = std::clamp(a.renderWidth, 16, 16384);
      a.renderHeight = std::clamp(a.renderHeight, 16, 16384);
      a.renderFps = std::clamp(a.renderFps, 1, 240);
      if (a.renderSampleRate < 8000 || a.renderSampleRate > 192000) a.renderSampleRate = 48000;
      a.renderFormat = std::clamp(a.renderFormat, 0, 2);
      a.renderRangeKind = std::clamp(a.renderRangeKind, 0, 3);
      if (a.renderRangeStart < 0) a.renderRangeStart = 0;
      if (a.renderRangeEnd < 0) a.renderRangeEnd = 0;
      if (a.nextId < 1) a.nextId = 1;
   }

   // Ensure primary destination is in targets list if targets is empty
   for (auto& p : outData.performance)
   {
      if (p.targets.empty() && p.dstIndex >= 0 && p.dstParam >= 0)
      {
         PerfTarget pt;
         pt.dstIndex = p.dstIndex;
         pt.dstParam = p.dstParam;
         pt.boolName = p.boolName;
         p.targets.push_back(pt);
      }
      if (p.targetsY.empty() && p.dstIndex >= 0 && p.dstParam2 >= 0)
      {
         PerfTarget pt;
         pt.dstIndex = p.dstIndex;
         pt.dstParam = p.dstParam2;
         p.targetsY.push_back(pt);
      }
   }

   if (outData.nodes.empty())
   {
      outError = "patch contains no nodes";
      return false;
   }
   return true;
}

const std::vector<std::string>& Recents() { return sRecents; }

void NoteRecent(const std::string& path)
{
   if (path.empty())
      return;
   // Moved to the front rather than appended, so reopening a patch does not
   // leave duplicates scattered through the list.
   sRecents.erase(std::remove(sRecents.begin(), sRecents.end(), path), sRecents.end());
   sRecents.insert(sRecents.begin(), path);
   if (sRecents.size() > kMaxRecents)
      sRecents.resize(kMaxRecents);
   SaveRecents();
}

void LoadRecents()
{
   sRecents.clear();
   const std::string path = RecentsPath();
   if (path.empty())
      return;
   std::ifstream file(path);
   std::string line;
   while (std::getline(file, line) && sRecents.size() < kMaxRecents)
   {
      if (!line.empty())
         sRecents.push_back(line);
   }
}

void SaveRecents()
{
   const std::string path = RecentsPath();
   if (path.empty())
      return;
   std::ofstream file(path);
   for (const std::string& entry : sRecents)
      file << entry << "\n";
}
}

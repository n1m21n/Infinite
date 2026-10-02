#include "Patch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "INode.h"
#include "platform/SettingsPaths.h"

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
      const std::string dir = InfiniteSettingsDirectory();
      if (dir.empty())
         return std::string();
      return dir + "/Infinite.recents";
   }

   std::string AppSettingsPath()
   {
      const std::string dir = InfiniteSettingsDirectory();
      return dir.empty() ? std::string("Infinite.settings") : dir + "/Infinite.settings";
   }

   void WriteSettingsLines(std::ostream& file, const SceneSettings& s, const char* prefix)
   {
      const std::string p = (prefix != nullptr && prefix[0] != '\0') ? std::string(prefix) + " " : std::string();
      file << p << "audioOutputDeviceId " << s.audioOutputDeviceId << "\n";
      file << p << "audioInputDeviceId " << s.audioInputDeviceId << "\n";
      file << p << "audioSampleRate " << s.audioSampleRate << "\n";
      file << p << "audioBufferFrames " << s.audioBufferFrames << "\n";
      file << p << "audioDriver " << s.audioDriver << "\n";
      file << p << "audioOversample " << s.audioOversample << "\n";
      file << p << "targetFps " << s.targetFps << "\n";
      file << p << "vsync " << (s.vsync ? 1 : 0) << "\n";
      file << p << "snapToGrid " << (s.snapToGrid ? 1 : 0) << "\n";
      file << p << "gridSnap " << s.gridSnap << "\n";
      file << p << "zoomSensitivity " << s.zoomSensitivity << "\n";
      file << p << "minimapEnabled " << (s.minimapEnabled ? 1 : 0) << "\n";
      file << p << "minimapCorner " << s.minimapCorner << "\n";
      file << p << "minimapSize " << s.minimapSize << "\n";
      file << p << "minimapOpacity " << s.minimapOpacity << "\n";
      file << p << "nodePanelOpen " << (s.nodePanelOpen ? 1 : 0) << "\n";
      file << p << "nodePanelWidth " << s.nodePanelWidth << "\n";
      file << p << "viewportPanelDock " << s.viewportPanelDock << "\n";
      file << p << "viewportPanelWidth " << s.viewportPanelWidth << "\n";
      file << p << "viewportPanelHeight " << s.viewportPanelHeight << "\n";
      file << p << "themePreset " << s.themePreset << "\n";
      file << p << "diagnosticLog " << (s.diagnosticLog ? 1 : 0) << "\n";
      file << p << "autosaveEnabled " << (s.autosaveEnabled ? 1 : 0) << "\n";
      file << p << "autosaveSeconds " << s.autosaveSeconds << "\n";
      file << p << "audioAutoStart " << (s.audioAutoStart ? 1 : 0) << "\n";
   }

   void ReadSettingValue(const std::string& key, std::istringstream& in, SceneSettings& s)
   {
      s.present = true;
      if (key == "audioOutputDeviceId") in >> s.audioOutputDeviceId;
      else if (key == "audioInputDeviceId") in >> s.audioInputDeviceId;
      else if (key == "audioSampleRate") in >> s.audioSampleRate;
      else if (key == "audioBufferFrames") in >> s.audioBufferFrames;
      else if (key == "audioDriver") in >> s.audioDriver;
      else if (key == "audioOversample") in >> s.audioOversample;
      else if (key == "targetFps") in >> s.targetFps;
      else if (key == "vsync") { int v = 0; in >> v; s.vsync = v != 0; }
      else if (key == "snapToGrid") { int v = 0; in >> v; s.snapToGrid = v != 0; }
      else if (key == "gridSnap") in >> s.gridSnap;
      else if (key == "zoomSensitivity") in >> s.zoomSensitivity;
      else if (key == "minimapEnabled") { int v = 0; in >> v; s.minimapEnabled = v != 0; }
      else if (key == "minimapCorner") in >> s.minimapCorner;
      else if (key == "minimapSize") in >> s.minimapSize;
      else if (key == "minimapOpacity") in >> s.minimapOpacity;
      else if (key == "nodePanelOpen") { int v = 0; in >> v; s.nodePanelOpen = v != 0; }
      else if (key == "nodePanelWidth") in >> s.nodePanelWidth;
      else if (key == "viewportPanelDock") in >> s.viewportPanelDock;
      else if (key == "viewportPanelWidth") in >> s.viewportPanelWidth;
      else if (key == "viewportPanelHeight") in >> s.viewportPanelHeight;
      else if (key == "themePreset") in >> s.themePreset;
      else if (key == "diagnosticLog") { int v = 0; in >> v; s.diagnosticLog = v != 0; }
      else if (key == "autosaveEnabled") { int v = 0; in >> v; s.autosaveEnabled = v != 0; }
      else if (key == "autosaveSeconds") in >> s.autosaveSeconds;
      else if (key == "audioAutoStart") { int v = 0; in >> v; s.audioAutoStart = v != 0; }
   }

   // Written with %.9g so a float survives the round trip exactly rather than
   // drifting a little every time a patch is opened and saved again.
   std::string FloatToString(float v)
   {
      char buf[40];
      snprintf(buf, sizeof(buf), "%.9g", (double)v);
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

// Turbo 0.45: the writer / reader work on streams so the RPC (MCP) can
// round-trip patch text without touching the disk.
static bool WriteStream(std::ostream& file, const Data& data, std::string& outError, const std::string& path)
{
   file << kMagic << " " << kVersion << "\n";
   for (const NodeRecord& node : data.nodes)
   {
      const float px = (std::isfinite(node.x) && std::abs(node.x) <= 1e6f && node.x > -2e9f) ? node.x : 0.0f;
      const float py = (std::isfinite(node.y) && std::abs(node.y) <= 1e6f && node.y > -2e9f) ? node.y : 0.0f;
      file << "node " << node.index << " " << node.category << " " << node.typeName << "\n";
      if (node.uid != 0)
         file << "  uid " << node.uid << "\n";
      file << "  pos " << FloatToString(px) << " " << FloatToString(py) << "\n";
      file << "  flags " << (node.showParams ? 1 : 0) << " " << (node.bypassed ? 1 : 0) << " "
           << (node.showMiniViewport ? 1 : 0) << " " << (node.showAdvancedParams ? 1 : 0) << " "
           << (node.showPreview ? 1 : 0) << " " << node.colorTag << "\n";
      for (const auto& p : node.params)
         file << "  " << p.first << " " << p.second << "\n";
      file << "end\n";
   }
   // Turbo 0.44: a 4th token names the source output when it is not 0
   // (multi-output nodes); older readers ignore it.
   for (const CableRecord& c : data.cables)
   {
      file << "cable " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex;
      if (c.srcOutput != 0)
         file << " " << c.srcOutput;
      file << "\n";
   }
   for (const CableRecord& c : data.geometry)
      file << "geo " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << "\n";
   for (const CableRecord& c : data.audio)
   {
      file << "aud " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex;
      if (c.srcOutput != 0)
         file << " " << c.srcOutput;
      file << "\n";
   }
   for (const CableRecord& c : data.notes)
      file << "note " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << " " << c.srcOutput << "\n";
   for (const ModRecord& m : data.modulation)
      file << "mod " << m.dstIndex << " " << m.dstParam << " "
           << m.srcIndex << " " << m.srcOutput << " "
           << m.polarity << " " << FloatToString(m.depth) << " " << FloatToString(m.centre) << " "
           << FloatToString(m.inMin) << " " << FloatToString(m.inMax) << " "
           << FloatToString(m.outMin) << " " << FloatToString(m.outMax) << "\n";
   for (const PaletteRecord& p : data.palette)
      file << "pal " << p.dstIndex << " " << p.dstColor << " "
           << p.srcIndex << " " << p.srcSwatch << "\n";
   for (const ExprRecord& e : data.expressions)
      file << "expr " << e.dstIndex << " " << e.dstParam << " " << EscapeLine(e.text) << "\n";
   for (const GlobalRecord& g : data.globals)
      file << "glob " << g.name << " " << EscapeLine(g.expr) << "\n";
   for (const MidiMapRecord& m : data.midi)
   {
      // Device keys can hold spaces: percent-escape them into one token.
      std::string dev;
      for (char ch : m.deviceKey)
      {
         if (ch == ' ') dev += "%20";
         else if (ch == '%') dev += "%25";
         else dev += ch;
      }
      if (dev.empty())
         dev = "*";
      file << "midimap " << m.dstIndex << " " << m.dstParam << " " << (m.isNote ? 1 : 0) << " "
           << m.channel << " " << m.number << " " << m.mode << " " << (m.soft ? 1 : 0) << " "
           << (m.invert ? 1 : 0) << " " << FloatToString(m.outMin) << " " << FloatToString(m.outMax) << " "
           << dev << " " << EscapeLine(m.paramName) << "\n";
      file << "midismooth " << m.dstIndex << " " << m.dstParam << " " << FloatToString(m.smoothMs) << "\n";
   }
   file << "transport " << FloatToString(data.transport.bpm) << " "
        << data.transport.timeSigNum << " " << data.transport.timeSigDen << " "
        << data.transport.key << " " << data.transport.scale << "\n";
   for (const GestureRecord& g : data.gestures)
   {
      file << "gesture " << g.dstIndex << " " << g.dstParam << " " << FloatToString(g.speed) << " "
           << (g.hasRangeOverride ? 1 : 0) << " " << FloatToString(g.rangeLo) << " " << FloatToString(g.rangeHi)
           << " " << g.samples.size();
      for (const GestureSample& s : g.samples)
      {
         char t[40];
         snprintf(t, sizeof(t), "%.9g", s.timeSec);
         file << " " << FloatToString(s.value) << " " << t << " " << (s.startsNewGrab ? 1 : 0);
      }
      file << "\n";
      if (std::fabs(g.curve) > 0.0001f)
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

   if (data.settings.present)
      WriteSettingsLines(file, data.settings, "setting");

   if (!file.good())
   {
      outError = "write failed partway through " + path;
      return false;
   }
   return true;
}

static bool ReadStream(std::istream& file, Data& outData, std::string& outError)
{
   std::string line;
   if (!std::getline(file, line))
   {
      outError = "file is empty";
      return false;
   }
   // A Windows editor or an AI tool may write CRLF line ends and a UTF-8 BOM
   // (from upstream): std::getline keeps the '\r', which would end up inside
   // type names and string values, and the BOM would fail the magic check.
   if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
       (unsigned char)line[2] == 0xBF)
      line.erase(0, 3);
   if (!line.empty() && line.back() == '\r')
      line.pop_back();
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

   while (std::getline(file, line))
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
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
         current.hasPos = false; // set again by a `pos` line
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
         current.hasPos = true;
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
         // Preview defaults on so patches written before the fifth flag keep
         // their original appearance and behaviour.
         int show = 0, bypass = 0, miniViewport = 0, advanced = 0, preview = 1;
         in >> show >> bypass >> miniViewport >> advanced;
         if (!(in >> preview))
            preview = 1;
         current.showParams = show != 0;
         current.bypassed = bypass != 0;
         current.showMiniViewport = miniViewport != 0;
         current.showAdvancedParams = advanced != 0;
         current.showPreview = preview != 0;
         int tag = 0;
         if (in >> tag)
            current.colorTag = tag;
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
         if (!(in >> c.srcOutput) || c.srcOutput < 0)
            c.srcOutput = 0;
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
         {
            if (!(in >> c.srcOutput) || c.srcOutput < 0)
               c.srcOutput = 0;
            outData.audio.push_back(c);
         }
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
         float a = 0.0f, b = 1.0f, cc = 0.0f, d = 1.0f;
         if (in >> a >> b >> cc >> d)
         {
            m.inMin = a; m.inMax = b; m.outMin = cc; m.outMax = d;
         }
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
      else if (tag == "midismooth")
      {
         int dst = 0, param = 0;
         float ms = 25.0f;
         if (in >> dst >> param >> ms)
            for (MidiMapRecord& m : outData.midi)
               if (m.dstIndex == dst && m.dstParam == param)
                  m.smoothMs = ms;
      }
      else if (tag == "midimap")
      {
         MidiMapRecord m;
         int isNote = 0, soft = 0, invert = 0;
         std::string dev;
         if (in >> m.dstIndex >> m.dstParam >> isNote >> m.channel >> m.number >> m.mode >> soft >> invert
                >> m.outMin >> m.outMax >> dev)
         {
            m.isNote = isNote != 0;
            m.soft = soft != 0;
            m.invert = invert != 0;
            if (dev != "*")
            {
               for (size_t i = 0; i < dev.size(); i++)
               {
                  if (dev[i] == '%' && i + 2 < dev.size() + 0 && dev.compare(i, 3, "%20") == 0) { m.deviceKey += ' '; i += 2; }
                  else if (dev[i] == '%' && dev.compare(i, 3, "%25") == 0) { m.deviceKey += '%'; i += 2; }
                  else m.deviceKey += dev[i];
               }
            }
            std::string raw;
            std::getline(in, raw);
            if (!raw.empty() && raw[0] == ' ')
               raw.erase(0, 1);
            m.paramName = UnescapeLine(raw);
            outData.midi.push_back(m);
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
      else if (tag == "gesture")
      {
         GestureRecord g;
         int hasRange = 0;
         size_t count = 0;
         in >> g.dstIndex >> g.dstParam >> g.speed >> hasRange >> g.rangeLo >> g.rangeHi >> count;
         g.hasRangeOverride = hasRange != 0;
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
         if (g.samples.size() >= 2)
            outData.gestures.push_back(std::move(g));
      }
      else if (tag == "gesturecurve")
      {
         int dstIndex = 0, dstParam = 0;
         float curve = 0.0f;
         in >> dstIndex >> dstParam >> curve;
         for (GestureRecord& g : outData.gestures)
            if (g.dstIndex == dstIndex && g.dstParam == dstParam)
               g.curve = curve;
      }
      else if (tag == "transport")
      {
         // A missing trailing token keeps TransportRecord's default.
         in >> outData.transport.bpm >> outData.transport.timeSigNum >> outData.transport.timeSigDen
            >> outData.transport.key >> outData.transport.scale;
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
         outData.hasArrange = true;
      }
      else if (tag == "arrangeimportsync")
      {
         int v = 1;
         in >> v;
         outData.arrangeSettings.importSyncToTempo = v != 0;
      }
      else if (tag == "setting")
      {
         std::string key;
         in >> key;
         ReadSettingValue(key, in, outData.settings);
      }
      // Anything else is from a newer version and is deliberately ignored.
   }

   for (StreamRecord& st : outData.streams)
      std::stable_sort(st.clips.begin(), st.clips.end(),
                       [](const ClipRecord& a, const ClipRecord& b) { return a.startTick < b.startTick; });
   std::stable_sort(outData.markers.begin(), outData.markers.end(),
                    [](const MarkerRecord& a, const MarkerRecord& b) { return a.posTick < b.posTick; });
   if (outData.hasArrange)
   {
      ArrangeSettingsRecord& a = outData.arrangeSettings;
      if (a.timeDisplay != 0 && a.timeDisplay != 1) a.timeDisplay = 0;
      if (a.snapDivision < 0 || a.snapDivision > 64) a.snapDivision = 4;
      if (!std::isfinite(a.zoom) || a.zoom <= 0.0f) a.zoom = 1.0f;
      if (!std::isfinite(a.scroll) || a.scroll < 0.0f) a.scroll = 0.0f;
      if (a.loopStart < 0) a.loopStart = 0;
      if (a.loopEnd < a.loopStart) a.loopEnd = a.loopStart;
      if (a.dockSide != 0 && a.dockSide != 1) a.dockSide = 0;
      a.renderWidth = std::clamp(a.renderWidth, 16, 16384);
      a.renderHeight = std::clamp(a.renderHeight, 16, 16384);
      a.renderFps = std::clamp(a.renderFps, 1, 240);
      if (a.nextId < 1) a.nextId = 1;
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

bool LoadAppSettings(SceneSettings& out)
{
   std::ifstream file(AppSettingsPath());
   if (!file)
      return false;
   std::string line;
   while (std::getline(file, line))
   {
      std::istringstream in(line);
      std::string key;
      in >> key;
      if (!key.empty())
         ReadSettingValue(key, in, out);
   }
   return out.present;
}

bool SaveAppSettings(const SceneSettings& settings, std::string& outError)
{
   std::ofstream file(AppSettingsPath(), std::ios::trunc);
   if (!file)
   {
      outError = "could not open app settings for writing";
      return false;
   }
   WriteSettingsLines(file, settings, "");
   if (!file.good())
   {
      outError = "write failed while saving app settings";
      return false;
   }
   return true;
}

// Turbo 0.45: file and text front ends for the stream reader / writer.
bool Write(const std::string& path, const Data& data, std::string& outError)
{
   std::ofstream file(path);
   if (!file)
   {
      outError = "could not open " + path + " for writing";
      return false;
   }
   return WriteStream(file, data, outError, path);
}

bool Read(const std::string& path, Data& outData, std::string& outError)
{
   std::ifstream file(path);
   if (!file)
   {
      outError = "could not open " + path;
      return false;
   }
   return ReadStream(file, outData, outError);
}

bool WriteText(const Data& data, std::string& outText, std::string& outError)
{
   std::ostringstream out;
   if (!WriteStream(out, data, outError, "text"))
      return false;
   outText = out.str();
   return true;
}

bool ReadText(const std::string& text, Data& outData, std::string& outError)
{
   std::istringstream in(text);
   return ReadStream(in, outData, outError);
}
} // namespace Patch

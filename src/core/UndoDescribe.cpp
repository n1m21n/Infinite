#include "core/UndoDescribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace UndoDescribe
{
namespace
{
   std::string NodeKey(const Patch::NodeRecord& n) { return n.uid != 0 ? "u" + std::to_string(n.uid) : "i" + std::to_string(n.index); }

   // 0.2000 -> "0.2", 0.55 -> "0.55"; anything that is not a plain number is shortened text.
   std::string Show(const std::string& v)
   {
      char* end = nullptr;
      const double d = std::strtod(v.c_str(), &end);
      if (!v.empty() && end != nullptr && *end == '\0')
      {
         char buf[32];
         std::snprintf(buf, sizeof buf, "%.2f", d);
         std::string s = buf;
         while (s.size() > 1 && s.back() == '0')
            s.pop_back();
         if (!s.empty() && s.back() == '.')
            s.pop_back();
         return s;
      }
      return v.size() > 18 ? v.substr(0, 17) + "..." : v;
   }

   std::string Plural(size_t n, const char* one, const char* many)
   {
      return n == 1 ? std::string(one) : std::to_string(n) + " " + many;
   }

   size_t Differences(const std::vector<Patch::CableRecord>& a, const std::vector<Patch::CableRecord>& b, size_t& added, size_t& removed)
   {
      auto key = [](const Patch::CableRecord& c) {
         return std::to_string(c.dstIndex) + ":" + std::to_string(c.dstSlot) + "<" + std::to_string(c.srcIndex) + "." + std::to_string(c.srcOutput);
      };
      std::set<std::string> sa, sb;
      for (const auto& c : a) sa.insert(key(c));
      for (const auto& c : b) sb.insert(key(c));
      for (const auto& k : sb) if (!sa.count(k)) added++;
      for (const auto& k : sa) if (!sb.count(k)) removed++;
      return added + removed;
   }
}

std::string Change(const Patch::Data& before, const Patch::Data& after)
{
   std::map<std::string, const Patch::NodeRecord*> was, now;
   for (const auto& n : before.nodes) was[NodeKey(n)] = &n;
   for (const auto& n : after.nodes) now[NodeKey(n)] = &n;

   std::vector<const Patch::NodeRecord*> added, removed;
   for (const auto& kv : now) if (!was.count(kv.first)) added.push_back(kv.second);
   for (const auto& kv : was) if (!now.count(kv.first)) removed.push_back(kv.second);
   if (!added.empty() && removed.empty())
      return added.size() == 1 ? "Add " + added[0]->typeName : "Add " + std::to_string(added.size()) + " nodes";
   if (!removed.empty() && added.empty())
      return removed.size() == 1 ? "Delete " + removed[0]->typeName : "Delete " + std::to_string(removed.size()) + " nodes";
   if (!added.empty() && !removed.empty())
      return "Replace nodes";

   size_t cAdd = 0, cRem = 0;
   Differences(before.cables, after.cables, cAdd, cRem);
   Differences(before.geometry, after.geometry, cAdd, cRem);
   Differences(before.audio, after.audio, cAdd, cRem);
   Differences(before.notes, after.notes, cAdd, cRem);
   if (cAdd > 0 && cRem == 0) return "Connect " + Plural(cAdd, "cable", "cables");
   if (cRem > 0 && cAdd == 0) return "Disconnect " + Plural(cRem, "cable", "cables");
   if (cAdd > 0 && cRem > 0) return "Rewire cable";

   if (before.modulation.size() != after.modulation.size())
      return after.modulation.size() > before.modulation.size() ? "Add modulation" : "Remove modulation";

   // Per-node changes.
   size_t paramChanges = 0, bypassChanges = 0, moved = 0;
   std::string oneKey, oneOld, oneNew, oneType, bypassType;
   bool bypassOn = false;
   std::set<std::string> paramNodes;
   for (const auto& kv : now)
   {
      const Patch::NodeRecord& a = *was[kv.first];
      const Patch::NodeRecord& b = *kv.second;
      if (a.bypassed != b.bypassed)
      {
         bypassChanges++;
         bypassType = b.typeName;
         bypassOn = b.bypassed;
      }
      std::map<std::string, std::string> pa;
      for (const auto& p : a.params) pa[p.first] = p.second;
      for (const auto& p : b.params)
      {
         auto it = pa.find(p.first);
         if (it == pa.end() || it->second != p.second)
         {
            paramChanges++;
            paramNodes.insert(kv.first);
            oneKey = p.first;
            oneOld = it == pa.end() ? "" : it->second;
            oneNew = p.second;
            oneType = b.typeName;
         }
      }
      if (a.x != b.x || a.y != b.y)
         moved++;
   }
   if (paramChanges == 1)
   {
      if (oneKey.size() > 2 && oneKey[1] == ' ')
         oneKey.erase(0, 2);
      if (!oneKey.empty() && oneKey[0] >= 'a' && oneKey[0] <= 'z')
         oneKey[0] = (char)(oneKey[0] - 'a' + 'A');
      // "Blur: Amount 0.2 -> 0.55" - node first so rows from different nodes stay distinguishable.
      return oneType + ": " + oneKey + " " + Show(oneOld) + " \xE2\x86\x92 " + Show(oneNew);
   }
   // Convention: "<Verb> <count> <noun> on <where>" - never a bare "Edit 24 settings".
   if (paramChanges > 1)
      return "Edit " + Plural(paramChanges, "1 setting", "settings") + " on " +
             (paramNodes.size() == 1 ? oneType : std::to_string(paramNodes.size()) + " nodes");
   if (bypassChanges > 0)
      return bypassChanges == 1 ? (bypassOn ? "Bypass " : "Enable ") + bypassType : "Bypass " + std::to_string(bypassChanges) + " nodes";
   if (before.modulation.size() == after.modulation.size() && !before.modulation.empty())
   {
      for (size_t i = 0; i < before.modulation.size(); i++)
         if (before.modulation[i].depth != after.modulation[i].depth || before.modulation[i].centre != after.modulation[i].centre ||
             before.modulation[i].polarity != after.modulation[i].polarity)
            return "Edit modulation";
   }
   if (before.expressions.size() != after.expressions.size())
      return "Edit expression";
   for (size_t i = 0; i < before.expressions.size(); i++)
      if (before.expressions[i].text != after.expressions[i].text)
         return "Edit expression";
   if (moved > 0)
      return moved == 1 ? "Move node" : "Move " + std::to_string(moved) + " nodes";
   if (before.transport.bpm != after.transport.bpm)
      return "Tempo " + Show(std::to_string(before.transport.bpm)) + " \xE2\x86\x92 " + Show(std::to_string(after.transport.bpm));
   if (before.transport.key != after.transport.key || before.transport.scale != after.transport.scale)
      return "Change key or scale";
   if (before.transport.timeSigNum != after.transport.timeSigNum || before.transport.timeSigDen != after.transport.timeSigDen)
      return "Change time signature";
   if (before.performance.size() != after.performance.size())
      return after.performance.size() > before.performance.size() ? "Performance: Add control" : "Performance: Remove control";
   {
      size_t changed = 0;
      std::string what = "Edit control";
      for (size_t i = 0; i < before.performance.size(); i++)
      {
         const auto& a = before.performance[i];
         const auto& b = after.performance[i];
         const bool placed = a.cellX != b.cellX || a.cellY != b.cellY || a.page != b.page;
         const bool bound = a.dstIndex != b.dstIndex || a.dstParam != b.dstParam || a.dstParam2 != b.dstParam2 ||
                            a.boolName != b.boolName || a.targets.size() != b.targets.size() || a.targetsY.size() != b.targetsY.size();
         const bool midi = a.midiDevice != b.midiDevice || a.midiChannel != b.midiChannel || a.midiController != b.midiController ||
                           a.midiIsNote != b.midiIsNote;
         const bool look = a.kind != b.kind || a.label != b.label || a.colorR != b.colorR || a.colorG != b.colorG || a.colorB != b.colorB;
         if (placed || bound || midi || look)
         {
            changed++;
            what = placed ? "Move control" : bound ? "Assign control" : midi ? "Map control to MIDI"
                 : a.kind != b.kind ? "Change control type" : a.label != b.label ? "Rename control" : "Recolour control";
         }
      }
      if (changed == 1)
         return "Performance: " + what;
      if (changed > 1)
         return "Performance: Edit " + std::to_string(changed) + " controls";
   }
   if (before.perfLayout.pageCount != after.perfLayout.pageCount)
      return after.perfLayout.pageCount > before.perfLayout.pageCount ? "Performance: Add page" : "Performance: Remove page";
   if (before.perfLayout.pageNames != after.perfLayout.pageNames)
      return "Performance: Rename page";
   if (before.perfLayout.cellSize != after.perfLayout.cellSize)
      return "Performance: Resize grid";
   if (before.palette.size() != after.palette.size())
      return after.palette.size() > before.palette.size() ? "Link colour" : "Unlink colour";
   if (before.globals.size() != after.globals.size())
      return after.globals.size() > before.globals.size() ? "Add global" : "Remove global";
   for (size_t i = 0; i < before.globals.size(); i++)
      if (before.globals[i].name != after.globals[i].name || before.globals[i].expr != after.globals[i].expr)
         return "Edit global " + after.globals[i].name;
   if (before.gestures.size() != after.gestures.size())
      return after.gestures.size() > before.gestures.size() ? "Record gesture" : "Clear gesture";
   if (before.viewport.nodes != after.viewport.nodes)
      return "Viewport: Change cards";
   if (before.viewport.dock != after.viewport.dock || before.viewport.open != after.viewport.open)
      return "Viewport: Change layout";
   if (before.streams.size() != after.streams.size() || before.markers.size() != after.markers.size())
      return "Timeline: Edit";
   return "Edit patch";
}

namespace
{
   using namespace Arrange;

   struct ClipRef
   {
      const Clip* clip;
      const Lane* lane;
   };

   void Collect(const Model& m, std::map<uint64_t, ClipRef>& out)
   {
      for (const Lane& l : m.lanes)
      {
         for (const Clip& c : l.clips)
            out[c.id] = { &c, &l };
         for (const SessionSlot& sl : l.slots)
            if (sl.Has())
               out[sl.clip.id] = { &sl.clip, &l };
      }
   }

   // "1 note" / "3 notes".
   std::string Count(size_t n, const char* one, const char* many) { return n == 1 ? "1 " + std::string(one) : std::to_string(n) + " " + many; }
}

std::string ChangeArrange(const Model& before, const Model& after)
{
   // Lanes first: a page or track appearing, going, or having its own settings changed.
   if (after.lanes.size() != before.lanes.size())
   {
      const bool added = after.lanes.size() > before.lanes.size();
      const std::vector<Lane>& more = added ? after.lanes : before.lanes;
      const std::vector<Lane>& fewer = added ? before.lanes : after.lanes;
      for (const Lane& l : more)
      {
         bool found = false;
         for (const Lane& f : fewer)
            found = found || f.id == l.id;
         if (!found)
         {
            const char* kind = l.type == kLaneMidi ? "page" : l.type == kLaneAudio ? "audio track" : "video track";
            const char* who = l.type == kLaneMidi ? "Piano Roll: " : "Timeline: ";
            return std::string(who) + (added ? "Add " : "Delete ") + kind;
         }
      }
      return "Timeline: Edit tracks";
   }
   for (size_t i = 0; i < after.lanes.size(); i++)
   {
      const Lane& a = before.lanes[i];
      const Lane& b = after.lanes[i];
      if (a.id != b.id)
         return "Timeline: Reorder tracks";
      const char* who = b.type == kLaneMidi ? "Piano Roll: " : "Timeline: ";
      const char* kind = b.type == kLaneMidi ? "page" : "track";
      if (a.name != b.name)
         return std::string(who) + "Rename " + kind;
      if (a.mute != b.mute)
         return std::string(who) + (b.mute ? "Mute " : "Unmute ") + kind;
      if (a.solo != b.solo)
         return std::string(who) + (b.solo ? "Solo " : "Unsolo ") + kind;
      if (a.enabled != b.enabled)
         return std::string(who) + (b.enabled ? "Enable " : "Disable ") + kind;
      if (a.gainDb != b.gainDb)
         return "Timeline: Track gain";
      if (a.pan != b.pan)
         return "Timeline: Track pan";
      if (a.opacity != b.opacity)
         return "Timeline: Track opacity";
      if (a.noteNodeUid != b.noteNodeUid)
         return std::string(who) + (b.noteNodeUid != 0 ? "Assign node" : "Unlink node");
      if (a.scaleSnap != b.scaleSnap)
         return "Piano Roll: Scale snap";
      if (a.drumRows != b.drumRows)
         return "Piano Roll: Drum rows";
      if (a.colorR != b.colorR || a.colorG != b.colorG || a.colorB != b.colorB)
         return std::string(who) + "Recolour " + kind;
      if (a.rowHeight != b.rowHeight)
         return "Timeline: Resize track";
   }

   // Clips and slots, matched by id.
   std::map<uint64_t, ClipRef> was, now;
   Collect(before, was);
   Collect(after, now);
   std::vector<const ClipRef*> added, removed;
   for (const auto& kv : now)
      if (!was.count(kv.first))
         added.push_back(&kv.second);
   for (const auto& kv : was)
      if (!now.count(kv.first))
         removed.push_back(&kv.second);
   if (!added.empty() || !removed.empty())
   {
      const ClipRef& r = !added.empty() ? *added[0] : *removed[0];
      const bool midi = r.lane->type == kLaneMidi;
      const char* who = midi ? "Piano Roll: " : "Timeline: ";
      if (added.size() >= 1 && removed.size() == 1 && added.size() == 2)
         return std::string(who) + "Split clip";
      if (!added.empty() && !removed.empty())
         return std::string(who) + "Replace " + Count(added.size(), "clip", "clips");
      if (added.empty())
         return std::string(who) + "Delete " + Count(removed.size(), "clip", "clips");
      return std::string(who) + "Add " + Count(added.size(), "clip", "clips");
   }

   size_t moved = 0, trimmed = 0, notesAdded = 0, notesRemoved = 0, notesChanged = 0, other = 0;
   const Clip* anyClip = nullptr;
   const Lane* anyLane = nullptr;
   std::string otherLabel;
   bool notesMoved = false, notesVel = false, notesLen = false, notesPitch = false, notesMute = false;
   for (const auto& kv : now)
   {
      const Clip& a = *was[kv.first].clip;
      const Clip& b = *kv.second.clip;
      anyClip = &b;
      anyLane = kv.second.lane;
      if (a.start != b.start || kv.second.lane->id != was[kv.first].lane->id)
         moved++;
      else if (a.length != b.length || a.contentOffset != b.contentOffset || a.sourceOffsetSeconds != b.sourceOffsetSeconds)
         trimmed++;
      else if (a.name != b.name) { other++; otherLabel = "Rename clip"; }
      else if (a.enabled != b.enabled) { other++; otherLabel = b.enabled ? "Unmute clip" : "Mute clip"; }
      else if (a.colorR != b.colorR || a.colorG != b.colorG || a.colorB != b.colorB) { other++; otherLabel = "Recolour clip"; }
      else if (a.gainDb != b.gainDb) { other++; otherLabel = "Clip gain"; }
      else if (a.fadeIn != b.fadeIn || a.fadeOut != b.fadeOut) { other++; otherLabel = "Clip fade"; }
      else if (a.pan != b.pan) { other++; otherLabel = "Clip pan"; }
      else if (a.pitch != b.pitch) { other++; otherLabel = "Clip pitch"; }
      else if (a.speed != b.speed || a.reverse != b.reverse) { other++; otherLabel = a.reverse != b.reverse ? "Reverse clip" : "Clip speed"; }
      else if (a.opacity != b.opacity || a.colorBrightness != b.colorBrightness || a.colorContrast != b.colorContrast ||
               a.colorSaturation != b.colorSaturation || a.blendMode != b.blendMode) { other++; otherLabel = "Clip look"; }
      else if (a.midiLoop != b.midiLoop || a.loopStart != b.loopStart || a.loopLength != b.loopLength) { other++; otherLabel = "Clip loop"; }
      else if (a.groupId != b.groupId) { other++; otherLabel = b.groupId != 0 ? "Group clips" : "Ungroup clips"; }
      else if (a.syncToTempo != b.syncToTempo || a.sampleBpm != b.sampleBpm) { other++; otherLabel = "Clip tempo"; }
      if (a.notes.size() != b.notes.size() || !std::equal(a.notes.begin(), a.notes.end(), b.notes.begin(), b.notes.end()))
      {
         std::map<uint32_t, const Note*> na;
         for (const Note& n : a.notes)
            na[n.id] = &n;
         std::set<uint32_t> seen;
         for (const Note& n : b.notes)
         {
            auto it = na.find(n.id);
            if (it == na.end())
            {
               notesAdded++;
               continue;
            }
            seen.insert(n.id);
            const Note& o = *it->second;
            if (!(o == n))
            {
               notesChanged++;
               notesMoved = notesMoved || o.start != n.start;
               notesLen = notesLen || o.length != n.length;
               notesPitch = notesPitch || o.pitch != n.pitch;
               notesVel = notesVel || o.vel != n.vel;
               notesMute = notesMute || o.mute != n.mute;
            }
         }
         for (const auto& k : na)
            if (!seen.count(k.first))
               notesRemoved++;
      }
   }
   const size_t nn = notesAdded + notesRemoved + notesChanged;
   if (nn > 0)
   {
      if (notesAdded > 0 && notesRemoved == 0 && notesChanged == 0)
         return "Piano Roll: Add " + Count(notesAdded, "note", "notes");
      if (notesRemoved > 0 && notesAdded == 0 && notesChanged == 0)
         return "Piano Roll: Delete " + Count(notesRemoved, "note", "notes");
      if (notesAdded > 0 || notesRemoved > 0)
         return "Piano Roll: Edit " + Count(nn, "note", "notes");
      const std::string many = notesChanged == 1 ? "note" : std::to_string(notesChanged) + " notes";
      if (notesMute && !notesMoved && !notesLen && !notesPitch && !notesVel)
         return "Piano Roll: Mute " + many;
      if (notesVel && !notesMoved && !notesLen && !notesPitch)
         return "Piano Roll: Velocity of " + many;
      if (notesLen && !notesMoved && !notesPitch && !notesVel)
         return "Piano Roll: Resize " + many;
      if ((notesMoved || notesPitch) && !notesLen && !notesVel)
         return "Piano Roll: Move " + many;
      return "Piano Roll: Edit " + many;
   }
   const bool midi = anyLane != nullptr && anyLane->type == kLaneMidi;
   const char* who = midi ? "Piano Roll: " : "Timeline: ";
   if (moved > 0)
      return std::string(who) + "Move " + Count(moved, "clip", "clips");
   if (trimmed > 0)
      return std::string(who) + "Trim " + Count(trimmed, "clip", "clips");
   if (other == 1)
      return std::string(who) + otherLabel;
   if (other > 1)
      return std::string(who) + "Edit " + Count(other, "clip", "clips");
   (void)anyClip;

   // Markers, scenes, groups, settings.
   if (before.markers.size() != after.markers.size())
      return after.markers.size() > before.markers.size() ? "Timeline: Add marker" : "Timeline: Delete marker";
   for (size_t i = 0; i < after.markers.size(); i++)
   {
      const Marker& a = before.markers[i];
      const Marker& b = after.markers[i];
      if (a.pos != b.pos)
         return "Timeline: Move marker";
      if (a.name != b.name)
         return "Timeline: Rename marker";
      if (a.color != b.color)
         return "Timeline: Recolour marker";
   }
   if (before.scenes.size() != after.scenes.size())
      return after.scenes.size() > before.scenes.size() ? "Session: Add scene" : "Session: Delete scene";
   for (size_t i = 0; i < after.scenes.size(); i++)
   {
      const Scene& a = before.scenes[i];
      const Scene& b = after.scenes[i];
      if (a.name != b.name)
         return "Session: Rename scene";
      if (a.tempo != b.tempo || a.quantize != b.quantize || a.additive != b.additive || a.followNext != b.followNext || a.followBars != b.followBars)
         return "Session: Scene settings";
   }
   if (before.sessionQuantize != after.sessionQuantize)
      return "Session: Launch quantise";
   if (before.trackGroups.size() != after.trackGroups.size())
      return after.trackGroups.size() > before.trackGroups.size() ? "Timeline: Group tracks" : "Timeline: Ungroup tracks";
   if (before.trackGroups.size() == after.trackGroups.size() && !before.trackGroups.empty())
      for (size_t i = 0; i < after.trackGroups.size(); i++)
         if (before.trackGroups[i].name != after.trackGroups[i].name)
            return "Timeline: Rename group";
   const Settings& a = before.settings;
   const Settings& b = after.settings;
   if (a.loop.enabled != b.loop.enabled || a.loop.start != b.loop.start || a.loop.end != b.loop.end)
      return a.loop.enabled != b.loop.enabled ? (b.loop.enabled ? "Timeline: Loop on" : "Timeline: Loop off") : "Timeline: Set loop";
   if (a.snapDivision != b.snapDivision || a.snapTriplet != b.snapTriplet)
      return "Timeline: Snap";
   if (a.dockSide != b.dockSide)
      return "Timeline: Dock";
   if (a.renderWidth != b.renderWidth || a.renderHeight != b.renderHeight || a.renderFps != b.renderFps)
      return "Timeline: Render settings";
   return "Timeline: Edit";
}
}

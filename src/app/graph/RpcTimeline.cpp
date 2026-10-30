// The Timeline, Session, Piano Roll, Performance Matrix and Sample Studio over RPC
// (docs/plans/terminal/README.md D19). Every call is { noun, action, args, fields }: `args` are
// the words after the verb, `fields` the key=value ones. Times are in beats. Same rules as
// Rpc.cpp: main thread, between frames, one undo step per edit.
#include <algorithm>
#include "app/AppShared.h"
#include "core/FactoryPresets.h"

namespace app
{
   // ArrangeSessionGrid.cpp, PianoRollPanel.cpp.
   bool ArrangeSessionSceneAction(const std::string& action, int row);
   uint64_t ArrangeSessionAddSlotClip(uint64_t laneId, int row);
   bool PianoRollTransform(uint64_t laneId, const std::string& name);

   namespace
   {
      using json = nlohmann::json;
      using Arrange::BeatsToTicks;
      using Arrange::TicksToBeats;

      using Words = std::vector<std::string>;

      std::vector<uint64_t> Ids(const Words& args, size_t from)
      {
         std::vector<uint64_t> ids;
         for (size_t i = from; i < args.size(); i++)
            ids.push_back(strtoull(args[i].c_str(), nullptr, 10));
         return ids;
      }

      json ClipJson(const Arrange::Clip& c)
      {
         const GraphNode* src = FindNodeByUid(c.srcUid);
         return { { "id", c.id }, { "start", TicksToBeats(c.start) }, { "length", TicksToBeats(c.length) },
                  { "node", src != nullptr ? src->index : -1 }, { "output", c.srcOutput }, { "name", c.name },
                  { "enabled", c.enabled }, { "group", c.groupId }, { "gainDb", c.gainDb }, { "pan", c.pan },
                  { "pitch", c.pitch }, { "speed", c.speed }, { "reverse", c.reverse }, { "opacity", c.opacity },
                  { "brightness", c.colorBrightness }, { "contrast", c.colorContrast }, { "saturation", c.colorSaturation },
                  { "blendMode", c.blendMode }, { "fadeIn", TicksToBeats(c.fadeIn) }, { "fadeOut", TicksToBeats(c.fadeOut) },
                  { "retrigger", c.retrigger }, { "syncToTempo", c.syncToTempo }, { "sampleBpm", c.sampleBpm },
                  { "notes", c.notes.size() } };
      }

      json ModelJson()
      {
         json lanes = json::array();
         for (const Arrange::Lane& lane : gArrange.lanes)
         {
            json clips = json::array();
            for (const Arrange::Clip& c : lane.clips)
               clips.push_back(ClipJson(c));
            json slots = json::array();
            for (size_t row = 0; row < lane.slots.size(); row++)
               if (lane.slots[row].Has())
               {
                  const Arrange::SessionSlot& s = lane.slots[row];
                  slots.push_back({ { "row", row }, { "clip", ClipJson(s.clip) }, { "followAction", s.followAction },
                                    { "followBars", s.followBars }, { "loop", s.loop }, { "loopBeats", TicksToBeats(s.loopTicks) },
                                    { "quantize", s.quantize }, { "launchMode", s.launchMode } });
               }
            static const char* kTypes[] = { "video", "audio", "midi" };
            lanes.push_back({ { "id", lane.id }, { "type", kTypes[std::clamp(lane.type, 0, 2)] }, { "name", lane.name },
                              { "enabled", lane.enabled }, { "group", lane.groupId }, { "mute", lane.mute }, { "solo", lane.solo },
                              { "gainDb", lane.gainDb }, { "pan", lane.pan }, { "opacity", lane.opacity },
                              { "clips", clips }, { "slots", slots } });
         }
         json markers = json::array();
         for (const Arrange::Marker& m : gArrange.markers)
            markers.push_back({ { "id", m.id }, { "at", TicksToBeats(m.pos) }, { "name", m.name } });
         json groups = json::array();
         for (const Arrange::TrackGroup& g : gArrange.trackGroups)
            groups.push_back({ { "id", g.id }, { "name", g.name }, { "enabled", g.enabled }, { "collapsed", g.collapsed },
                               { "parent", g.parentGroupId } });
         json scenes = json::array();
         for (const Arrange::Scene& s : gArrange.scenes)
            scenes.push_back({ { "name", s.name }, { "tempo", s.tempo }, { "quantize", s.quantize }, { "additive", s.additive },
                               { "followNext", s.followNext }, { "followBars", s.followBars } });
         const Arrange::Settings& st = gArrange.settings;
         return { { "tracks", lanes }, { "markers", markers }, { "groups", groups }, { "scenes", scenes },
                  { "loop", { { "on", st.loop.enabled }, { "start", TicksToBeats(st.loop.start) }, { "end", TicksToBeats(st.loop.end) } } },
                  { "snap", st.snapDivision }, { "triplet", st.snapTriplet }, { "sessionQuantize", gArrange.sessionQuantize } };
      }

      template <class T>
      bool Assign(const json& fields, const char* key, T& out)
      {
         if (!fields.contains(key))
            return false;
         out = fields[key].get<T>();
         return true;
      }

      bool SetClipFields(Arrange::Clip& c, const json& f)
      {
         Assign(f, "name", c.name);
         Assign(f, "output", c.srcOutput);
         Assign(f, "enabled", c.enabled);
         Assign(f, "gainDb", c.gainDb);
         Assign(f, "pan", c.pan);
         Assign(f, "pitch", c.pitch);
         Assign(f, "speed", c.speed);
         Assign(f, "reverse", c.reverse);
         Assign(f, "opacity", c.opacity);
         Assign(f, "brightness", c.colorBrightness);
         Assign(f, "contrast", c.colorContrast);
         Assign(f, "saturation", c.colorSaturation);
         Assign(f, "blendMode", c.blendMode);
         Assign(f, "retrigger", c.retrigger);
         Assign(f, "transpose", c.transpose);
         Assign(f, "swing", c.swing);
         Assign(f, "velScale", c.velScale);
         Assign(f, "chanceScale", c.chanceScale);
         Assign(f, "midiLoop", c.midiLoop);
         if (f.contains("fadeIn"))
            c.fadeIn = std::clamp<Arrange::Tick>(BeatsToTicks(f["fadeIn"].get<double>()), 0, c.length);
         if (f.contains("fadeOut"))
            c.fadeOut = std::clamp<Arrange::Tick>(BeatsToTicks(f["fadeOut"].get<double>()), 0, c.length);
         if (f.contains("loopStart"))
            c.loopStart = BeatsToTicks(f["loopStart"].get<double>());
         if (f.contains("loopLength"))
            c.loopLength = BeatsToTicks(f["loopLength"].get<double>());
         if (f.contains("color"))
         {
            const uint32_t rgb = f["color"].get<uint32_t>();
            c.colorR = ((rgb >> 16) & 0xFF) / 255.0f;
            c.colorG = ((rgb >> 8) & 0xFF) / 255.0f;
            c.colorB = (rgb & 0xFF) / 255.0f;
         }
         return true;
      }

      // The source of a clip: "node" or "node:output" in `fields`.
      bool BindClip(Arrange::Clip& c, const json& fields, std::string& outError)
      {
         if (!fields.contains("node"))
            return true;
         const GraphNode* gn = FindNodeByIndex(fields["node"].get<int>());
         if (gn == nullptr)
         {
            outError = "unknown node index";
            return false;
         }
         c.srcUid = gn->uid;
         c.srcOutput = fields.value("output", 0);
         return true;
      }

      void Timeline(const std::string& noun, const std::string& action, const Words& args, const json& fields, json& outResult, std::string& outError)
      {
         const auto need = [&](size_t n)
         {
            if (args.size() >= n)
               return true;
            outError = "timeline " + noun + " " + action + " wants " + std::to_string(n) + " more word(s)";
            return false;
         };
         const auto refused = [&](const char* what) { outError = std::string(what) + ": no such id, or nothing to change"; };
         const auto done = [&](bool changed)
         {
            if (!changed)
               refused(action.c_str());
         };
         const auto id = [&](size_t i) { return strtoull(args[i].c_str(), nullptr, 10); };
         const auto beat = [&](size_t i) { return BeatsToTicks(atof(args[i].c_str())); };

         if (noun == "track")
         {
            if (action == "add")
            {
               const std::string type = args.empty() ? "video" : args[0];
               if (type != "video" && type != "audio" && type != "midi")
               {
                  outError = "a track is video, audio or midi";
                  return;
               }
               uint64_t made = 0;
               ArrangeEdit([&] {
                  made = Arrange::AddLane(gArrange, type == "video" ? Arrange::kLaneVideo : type == "audio" ? Arrange::kLaneAudio : Arrange::kLaneMidi);
                  if (args.size() > 1)
                     Arrange::FindLane(gArrange, made)->name = args[1];
               });
               gArrangePanelOpen = true;
               outResult = { { "id", made } };
            }
            else if (action == "remove" && need(1))
               done(ArrangeEdit([&] { Arrange::RemoveLane(gArrange, id(0)); }));
            else if (action == "duplicate" && need(1))
               done(ArrangeEdit([&] { Arrange::DuplicateLane(gArrange, id(0)); }));
            else if (action == "move" && need(2))
               done(ArrangeEdit([&] { Arrange::ReorderLane(gArrange, id(0), atoi(args[1].c_str())); }));
            else if (action == "link" && need(2))
            {
               const GraphNode* gn = FindNodeByIndex(atoi(args[1].c_str()));
               if (gn == nullptr)
                  outError = "unknown node index";
               else
                  done(ArrangeEdit([&] { Arrange::LinkLane(gArrange, id(0), gn->uid); }));
            }
            else if (action == "set" && need(1))
            {
               Arrange::Lane* lane = Arrange::FindLane(gArrange, id(0));
               if (lane == nullptr)
               {
                  refused("set");
                  return;
               }
               ArrangeEdit([&] {
                  Assign(fields, "name", lane->name);
                  Assign(fields, "enabled", lane->enabled);
                  Assign(fields, "mute", lane->mute);
                  Assign(fields, "solo", lane->solo);
                  Assign(fields, "gainDb", lane->gainDb);
                  Assign(fields, "pan", lane->pan);
                  Assign(fields, "opacity", lane->opacity);
                  Assign(fields, "scaleSnap", lane->scaleSnap);
                  Assign(fields, "drumRows", lane->drumRows);
                  gArrange.revision++;
               });
            }
            else if (outError.empty())
               outError = "timeline track add|remove|duplicate|move|link|set";
            return;
         }
         if (noun == "clip")
         {
            if (action == "add" && need(2))
            {
               Arrange::Clip c;
               c.start = beat(1);
               c.length = BeatsToTicks(fields.value("length", 4.0));
               if (!BindClip(c, fields, outError))
                  return;
               uint64_t made = 0;
               done(ArrangeEdit([&] { Arrange::PlaceOverwrite(gArrange, id(0), c, &made); }));
               outResult = { { "id", made } };
            }
            else if (action == "move" && need(2))
               done(ArrangeEdit([&] { Arrange::MoveClips(gArrange, { id(0) }, beat(1), args.size() > 2 ? atoi(args[2].c_str()) : 0); }));
            else if (action == "trim" && need(3))
               done(ArrangeEdit([&] { Arrange::TrimEdge(gArrange, id(0), args[1] == "end" ? Arrange::kEdgeEnd : Arrange::kEdgeStart, beat(2)); }));
            else if (action == "split" && need(2))
               done(ArrangeEdit([&] { Arrange::Split(gArrange, id(0), beat(1)); }));
            else if (action == "duplicate" && need(1))
               done(ArrangeEdit([&] { Arrange::DuplicateBlock(gArrange, Ids(args, 0)); }));
            else if (action == "delete" && need(1))
               done(ArrangeEdit([&] { Arrange::Delete(gArrange, Ids(args, 0)); }));
            else if (action == "enable" && need(1))
               done(ArrangeEdit([&] { Arrange::SetEnabled(gArrange, Ids(args, 0), Arrange::kToggle); }));
            else if (action == "group" && need(2))
               done(ArrangeEdit([&] { Arrange::Group(gArrange, Ids(args, 0)); }));
            else if (action == "ungroup" && need(1))
               done(ArrangeEdit([&] { Arrange::RemoveFromGroup(gArrange, Ids(args, 0)); }));
            else if (action == "set" && need(1))
            {
               Arrange::Clip* c = Arrange::FindClipOrSlot(gArrange, id(0));
               if (c == nullptr)
               {
                  refused("set");
                  return;
               }
               if (fields.contains("syncToTempo"))
                  ArrangeEdit([&] { ArrangeSetSampleSync(id(0), fields["syncToTempo"].get<bool>()); });
               if (fields.contains("sampleBpm"))
                  ArrangeEdit([&] { ArrangeSetSampleBpm(id(0), fields["sampleBpm"].get<float>()); });
               ArrangeEdit([&] {
                  if (BindClip(*c, fields, outError) && SetClipFields(*c, fields))
                     gArrange.revision++;
               });
            }
            else if (outError.empty())
               outError = "timeline clip add|move|trim|split|duplicate|delete|enable|group|ungroup|set";
            return;
         }
         if (noun == "marker")
         {
            if (action == "add" && need(1))
            {
               uint64_t made = 0;
               ArrangeEdit([&] { made = Arrange::AddMarker(gArrange, beat(0), args.size() > 1 ? args[1] : std::string()); });
               outResult = { { "id", made } };
            }
            else if (action == "move" && need(2))
               done(ArrangeEdit([&] { Arrange::MoveMarker(gArrange, id(0), beat(1)); }));
            else if (action == "rename" && need(2))
               done(ArrangeEdit([&] { Arrange::RenameMarker(gArrange, id(0), args[1]); }));
            else if (action == "delete" && need(1))
               done(ArrangeEdit([&] { Arrange::DeleteMarker(gArrange, id(0)); }));
            else if (action == "next" || action == "prev")
               done(ArrangeJumpToMarker(action == "next" ? 1 : -1));
            else if (outError.empty())
               outError = "timeline marker add|move|rename|delete|next|prev";
            return;
         }
         if (noun == "group")
         {
            if (action == "add" && need(1))
            {
               uint64_t made = 0;
               ArrangeEdit([&] { made = Arrange::GroupSelectedLanes(gArrange, Ids(args, 0)); });
               outResult = { { "id", made } };
            }
            else if (action == "remove" && need(1))
               done(ArrangeEdit([&] { Arrange::RemoveTrackGroup(gArrange, id(0), fields.value("tracks", false)); }));
            else if (action == "duplicate" && need(1))
               done(ArrangeEdit([&] { Arrange::DuplicateTrackGroup(gArrange, id(0)); }));
            else if (action == "rename" && need(2))
               done(ArrangeEdit([&] { Arrange::RenameTrackGroup(gArrange, id(0), args[1]); }));
            else if (action == "enable" && need(1))
               done(ArrangeEdit([&] { Arrange::SetTrackGroupEnabled(gArrange, id(0), Arrange::kToggle); }));
            else if (action == "join" && need(2))
               done(ArrangeEdit([&] { Arrange::SetLaneTrackGroup(gArrange, id(0), id(1)); }));
            else if (outError.empty())
               outError = "timeline group add|remove|duplicate|rename|enable|join";
            return;
         }
         if (noun == "loop")
         {
            Arrange::LoopRange loop = gArrange.settings.loop;
            if (args.size() >= 2)
            {
               loop.start = beat(0);
               loop.end = beat(1);
            }
            ArrangeSetLoop(action != "off", loop.start, loop.end);
            return;
         }
         if (noun == "snap")
         {
            ArrangeSetSnap(atoi(action.c_str()), !args.empty() && args[0] == "triplet");
            return;
         }
         if (noun == "render" && !action.empty())
         {
            Words all = args;
            all.insert(all.begin(), action);
            if (ArrangeRenderBusy())
            {
               outError = "a render is already running";
               return;
            }
            ArrangeCommitLaneScopedRenderJob(ArrangeBuildLaneScopedRenderJob(Ids(all, 0), fields.value("name", std::string("Track"))));
            return;
         }
         outError = "timeline [track|clip|marker|group|loop|snap|render] ...";
      }

      void Session(const std::string& noun, const Words& args, const json& fields, json& outResult, std::string& outError)
      {
         const auto need = [&](size_t n)
         {
            if (args.size() >= n)
               return true;
            outError = "session " + noun + " wants " + std::to_string(n) + " word(s)";
            return false;
         };
         const auto lane = [&](size_t i) { return strtoull(args[i].c_str(), nullptr, 10); };
         const auto row = [&](size_t i) { return atoi(args[i].c_str()); };
         if (noun == "stop")
         {
            if (args.empty())
               ArrangeSessionStopAll();
            else
               ArrangeSessionStopLane(lane(0));
         }
         else if (noun == "scene" && need(1))
            ArrangeSessionLaunchScene(row(0));
         else if (noun == "launch" && need(2))
            ArrangeSessionLaunch(lane(0), row(1));
         else if (noun == "launchrow" && need(2))
            ArrangeSessionLaunchGroupRow(lane(0), row(1));
         else if (noun == "record" && need(1))
            ArrangeSessionSetRecording(args[0] == "on");
         else if (noun == "add" && need(2))
            outResult = { { "id", ArrangeSessionAddSlotClip(lane(0), row(1)) } };
         else if ((noun == "insert" || noun == "duplicatescene" || noun == "deletescene") && need(1))
         {
            if (!ArrangeSessionSceneAction(noun, row(0)))
               outError = "no scene " + args[0];
         }
         else if (noun == "send" && need(1))
            ArrangeSessionSendSceneToTimeline(row(0));
         else if ((noun == "place" || noun == "duplicate" || noun == "clear" || noun == "copy" || noun == "paste" || noun == "cut") && need(2))
         {
            static const Words kKinds = { "place", "duplicate", "clear", "copy", "paste", "cut" };
            ArrangeSessionRunActionForTest((int)(std::find(kKinds.begin(), kKinds.end(), noun) - kKinds.begin()), lane(0), row(1));
         }
         else if (noun == "sceneset" && need(1))
         {
            if (row(0) < 0 || row(0) >= (int)gArrange.scenes.size())
            {
               outError = "no scene " + args[0];
               return;
            }
            ArrangeEdit([&] {
               Arrange::Scene& s = gArrange.scenes[(size_t)row(0)];
               Assign(fields, "name", s.name);
               Assign(fields, "tempo", s.tempo);
               Assign(fields, "quantize", s.quantize);
               Assign(fields, "additive", s.additive);
               Assign(fields, "followNext", s.followNext);
               Assign(fields, "followBars", s.followBars);
               gArrange.revision++;
            });
         }
         else if (noun == "slot" && need(2))
         {
            Arrange::Lane* l = Arrange::FindLane(gArrange, lane(0));
            if (l == nullptr || row(1) < 0 || row(1) >= (int)l->slots.size() || !l->slots[(size_t)row(1)].Has())
            {
               outError = "no clip in that slot";
               return;
            }
            ArrangeEdit([&] {
               Arrange::SessionSlot& s = l->slots[(size_t)row(1)];
               Assign(fields, "followAction", s.followAction);
               Assign(fields, "followBars", s.followBars);
               Assign(fields, "loop", s.loop);
               Assign(fields, "quantize", s.quantize);
               Assign(fields, "launchMode", s.launchMode);
               if (fields.contains("loopBeats"))
                  s.loopTicks = BeatsToTicks(fields["loopBeats"].get<double>());
               SetClipFields(s.clip, fields);
               gArrange.revision++;
            });
         }
         else if (noun == "quantize" && need(1))
            ArrangeEdit([&] {
               gArrange.sessionQuantize = row(0);
               gArrange.revision++;
            });
         else if (outError.empty())
            outError = "session scene|launch|launchrow|stop|record|add|insert|duplicatescene|deletescene|send|place|duplicate|clear|copy|paste|cut|sceneset|slot|quantize";
      }

      void Notes(const std::string& action, const Words& args, const json& fields, json& outResult, std::string& outError)
      {
         if (args.empty())
         {
            outError = "notes " + action + " <clip> ...";
            return;
         }
         const uint64_t clip = strtoull(args[0].c_str(), nullptr, 10);
         if (action == "transform")
         {
            // Works on a track, the page the Piano Roll shows.
            if (args.size() < 2 || !PianoRollTransform(clip, args[1]))
               outError = "notes transform <track> legato|humanize|reverse|invert|double|half|strum|chop|ramp|scale";
            return;
         }
         const Arrange::Clip* c = Arrange::FindClipOrSlot(gArrange, clip);
         if (c == nullptr)
         {
            outError = "unknown clip";
            return;
         }
         std::vector<uint32_t> ids;
         for (size_t i = 1; i < args.size(); i++)
            ids.push_back((uint32_t)strtoul(args[i].c_str(), nullptr, 10));
         if (ids.empty())
            for (const Arrange::Note& n : c->notes)
               ids.push_back(n.id);
         if (action == "get")
         {
            json notes = json::array();
            for (const Arrange::Note& n : c->notes)
               notes.push_back({ { "id", n.id }, { "start", TicksToBeats(n.start) }, { "length", TicksToBeats(n.length) },
                                 { "pitch", n.pitch }, { "vel", n.vel }, { "chance", n.chance }, { "mute", n.mute } });
            outResult = { { "notes", notes } };
         }
         else if (action == "add")
         {
            std::vector<Arrange::Note> notes;
            for (const json& spec : fields.value("notes", json::array()))
            {
               Arrange::Note n;
               n.start = BeatsToTicks(spec.value("start", 0.0));
               n.length = BeatsToTicks(spec.value("length", 0.25));
               n.pitch = (uint8_t)std::clamp(spec.value("pitch", 60), 0, 127);
               n.vel = (uint8_t)std::clamp(spec.value("vel", 100), 1, 127);
               notes.push_back(n);
            }
            // The short form: beat:pitch[:length[:velocity]].
            for (size_t i = 1; i < args.size(); i++)
            {
               double beat = 0.0, length = 0.25;
               int pitch = 60, vel = 100;
               if (sscanf(args[i].c_str(), "%lf:%d:%lf:%d", &beat, &pitch, &length, &vel) < 2)
               {
                  outError = "a note is beat:pitch[:length[:velocity]], not '" + args[i] + "'";
                  return;
               }
               Arrange::Note n;
               n.start = BeatsToTicks(beat);
               n.length = BeatsToTicks(length);
               n.pitch = (uint8_t)std::clamp(pitch, 0, 127);
               n.vel = (uint8_t)std::clamp(vel, 1, 127);
               notes.push_back(n);
            }
            std::vector<uint32_t> made;
            ArrangeEdit([&] { Arrange::AddNotes(gArrange, clip, notes, &made); });
            outResult = { { "ids", made } };
         }
         else if (action == "set")
         {
            std::vector<Arrange::Note> edited;
            for (Arrange::Note n : c->notes)
               if (std::find(ids.begin(), ids.end(), n.id) != ids.end())
               {
                  Assign(fields, "mute", n.mute);
                  Assign(fields, "chance", n.chance);
                  if (fields.contains("vel"))
                     n.vel = (uint8_t)std::clamp(fields["vel"].get<int>(), 1, 127);
                  if (fields.contains("pitch"))
                     n.pitch = (uint8_t)std::clamp(fields["pitch"].get<int>(), 0, 127);
                  if (fields.contains("length"))
                     n.length = BeatsToTicks(fields["length"].get<double>());
                  edited.push_back(n);
               }
            ArrangeEdit([&] { Arrange::EditNotes(gArrange, clip, edited); });
         }
         else if (action == "delete")
            ArrangeEdit([&] { Arrange::DeleteNotes(gArrange, clip, ids); });
         else if (action == "move")
            ArrangeEdit([&] { Arrange::MoveNotes(gArrange, clip, ids, BeatsToTicks(fields.value("beats", 0.0)), fields.value("pitch", 0)); });
         else if (action == "quantize")
         {
            Arrange::QuantizeSpec spec;
            spec.grid = BeatsToTicks(fields.value("grid", 0.25));
            spec.strength = fields.value("strength", 1.0f);
            spec.swing = fields.value("swing", 0.0f);
            spec.ends = fields.value("ends", false);
            ArrangeEdit([&] { Arrange::QuantizeNotes(gArrange, clip, ids, spec); });
         }
         else
            outError = "notes get|add|set|delete|move|quantize|transform";
      }

      void Perf(const std::string& action, const Words& args, const json& fields, json& outResult, std::string& outError)
      {
         const auto element = [&](size_t i) -> Patch::PerfRecord*
         {
            const int index = i < args.size() ? atoi(args[i].c_str()) : -1;
            if (index < 0 || index >= (int)gPerfElements.size())
            {
               outError = "no control " + (i < args.size() ? args[i] : std::string("given"));
               return nullptr;
            }
            return &gPerfElements[(size_t)index];
         };
         if (action.empty() || action == "get")
         {
            json controls = json::array();
            for (const Patch::PerfRecord& r : gPerfElements)
               controls.push_back({ { "kind", r.kind }, { "label", r.label }, { "page", r.page }, { "x", r.cellX }, { "y", r.cellY },
                                    { "node", r.dstIndex }, { "param", r.dstParam }, { "value", r.value } });
            outResult = { { "controls", controls }, { "pages", gPerfLayout.pageNames }, { "active", gPerfActivePage } };
         }
         else if (action == "add")
         {
            static const Words kKinds = { "knob", "fader", "slider", "toggle", "xy", "trigger", "number", "selector", "pan", "gate" };
            const auto it = args.empty() ? kKinds.end() : std::find(kKinds.begin(), kKinds.end(), args[0]);
            if (it == kKinds.end())
            {
               outError = "perf add knob|fader|slider|toggle|xy|trigger|number|selector|pan|gate [label]";
               return;
            }
            AddPerfElementToCurrentPage((int)(it - kKinds.begin()));
            if (args.size() > 1)
               gPerfElements.back().label = args[1];
            outResult = { { "control", gPerfElements.size() - 1 } };
         }
         else if (action == "remove")
         {
            if (element(0) != nullptr)
            {
               PushUndoCheckpoint();
               gPerfElements.erase(gPerfElements.begin() + atoi(args[0].c_str()));
               gPerfSelection.clear();
            }
         }
         else if (action == "assign")
         {
            Patch::PerfRecord* r = element(0);
            const GraphNode* gn = args.size() > 2 ? FindNodeByIndex(atoi(args[1].c_str())) : nullptr;
            int param = -1;
            if (gn != nullptr)
            {
               param = ParamIndexOfKeyOnNode(*gn, args[2]);
               if (param < 0)
                  param = ParamIndexOfKeyOnNode(*gn, "p." + args[2]);
            }
            const std::vector<ParamRef>& drawn = Modulation::Instance().FrameParams();
            if (gn != nullptr && param < 0 && std::none_of(drawn.begin(), drawn.end(), [&](const ParamRef& p) { return p.nodeIndex == gn->index; }))
            {
               // A closed node has no controls until something asks. The client retries on this text.
               gParamProbeNode = gn->index;
               outError = gn->typeName + " has no controls yet; retry on the next frame";
               return;
            }
            gParamProbeNode = -1;
            if (r != nullptr && param < 0)
               outError = "perf assign <control> <node> <key>; `infinite params <node>` lists the keys";
            else if (r != nullptr)
            {
               PushUndoCheckpoint();
               r->dstIndex = gn->index;
               r->dstParam = param;
            }
         }
         else if (action == "clear")
         {
            if (Patch::PerfRecord* r = element(0))
            {
               PushUndoCheckpoint();
               r->dstIndex = r->dstParam = r->dstParam2 = -1;
               r->targets.clear();
               r->targetsY.clear();
            }
         }
         else if (action == "unmidi")
         {
            if (Patch::PerfRecord* r = element(0))
            {
               PushUndoCheckpoint();
               r->midiDevice = r->midiDeviceY = 0;
               r->midiChannel = r->midiChannelY = -1;
               r->midiController = r->midiControllerY = -1;
            }
         }
         else if (action == "set")
         {
            if (Patch::PerfRecord* r = element(0))
            {
               PushUndoCheckpoint();
               Assign(fields, "label", r->label);
               Assign(fields, "value", r->value);
               Assign(fields, "x", r->cellX);
               Assign(fields, "y", r->cellY);
               Assign(fields, "page", r->page);
               if (fields.contains("color"))
               {
                  const uint32_t rgb = fields["color"].get<uint32_t>();
                  r->colorR = ((rgb >> 16) & 0xFF) / 255.0f;
                  r->colorG = ((rgb >> 8) & 0xFF) / 255.0f;
                  r->colorB = (rgb & 0xFF) / 255.0f;
               }
            }
         }
         else if (action == "edit")
            gPerfEditMode = args.empty() || args[0] == "on";
         else if (action == "page")
         {
            const std::string what = args.empty() ? std::string() : args[0];
            const int p = args.size() > 1 ? atoi(args[1].c_str()) : gPerfActivePage;
            if (what == "add" && gPerfLayout.pageCount < Patch::PerfLayoutRecord::kMaxPages)
            {
               PushUndoCheckpoint();
               const int made = gPerfLayout.pageCount++;
               gPerfLayout.pageNames.push_back("Page " + std::to_string(made + 1));
               gPerfActivePage = made;
            }
            else if (what == "go" && p >= 0 && p < gPerfLayout.pageCount)
               gPerfActivePage = p;
            else if (what == "rename" && args.size() > 2 && p >= 0 && p < (int)gPerfLayout.pageNames.size())
            {
               PushUndoCheckpoint();
               gPerfLayout.pageNames[(size_t)p] = args[2];
            }
            else if (what == "delete" && gPerfLayout.pageCount > 1 && p >= 0 && p < gPerfLayout.pageCount)
            {
               PushUndoCheckpoint();
               gPerfElements.erase(std::remove_if(gPerfElements.begin(), gPerfElements.end(), [p](const Patch::PerfRecord& r) { return r.page == p; }),
                                   gPerfElements.end());
               for (Patch::PerfRecord& r : gPerfElements)
                  if (r.page > p)
                     r.page--;
               gPerfLayout.pageCount--;
               if (p < (int)gPerfLayout.pageNames.size())
                  gPerfLayout.pageNames.erase(gPerfLayout.pageNames.begin() + p);
               gPerfActivePage = std::min(gPerfActivePage, gPerfLayout.pageCount - 1);
            }
            else if (what == "duplicate" && gPerfLayout.pageCount < Patch::PerfLayoutRecord::kMaxPages && p >= 0 && p < gPerfLayout.pageCount)
            {
               PushUndoCheckpoint();
               const int made = gPerfLayout.pageCount++;
               gPerfLayout.pageNames.push_back((p < (int)gPerfLayout.pageNames.size() ? gPerfLayout.pageNames[(size_t)p] : std::string("Page")) + " Copy");
               for (size_t i = 0, n = gPerfElements.size(); i < n; i++)
                  if (gPerfElements[i].page == p)
                  {
                     Patch::PerfRecord copy = gPerfElements[i];
                     copy.page = made;
                     gPerfElements.push_back(copy);
                  }
               gPerfActivePage = made;
            }
            else
               outError = "perf page add|go|rename|delete|duplicate [page] [name]";
         }
         else
            outError = "perf get|add|remove|assign|clear|unmidi|set|edit|page";
         if (outError.empty() && outResult.is_null())
            outResult = json::object();
      }

      // What a node's right-click menu does.
      void NodeMenu(const std::string& action, GraphNode& gn, const Words& args, json& outResult, std::string& outError)
      {
         const bool on = args.empty() || args[0] != "off";
         if (action == "rename" && !args.empty())
         {
            auto* group = dynamic_cast<GroupNode*>(gn.node.get());
            if (group == nullptr)
            {
               outError = "only groups have a name of their own";
               return;
            }
            PushUndoCheckpoint();
            group->label = args[0];
            gPatchDirty = true;
         }
         else if (action == "params")
            gn.showParams = on;
         else if (action == "viewport")
            gn.showMiniViewport = on;
         else if (action == "panel" && CanShowInViewportPanel(gn))
         {
            if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn.index) == gViewportPanelNodes.end())
               gViewportPanelNodes.push_back(gn.index);
            gViewportPanelOpen = true;
         }
         else if (action == "window" && CanShowInViewportPanel(gn))
         {
            ProjectorWindow* projector = FindProjectorWindow(gn.index);
            const std::string what = args.empty() ? std::string("open") : args[0];
            if (what == "close")
               CloseProjectorWindowFor(gn.index);
            else if (projector == nullptr)
               OpenProjectorWindow(glfwGetCurrentContext(), gn);
            else if (what == "fullscreen")
               ToggleProjectorFullscreen(*projector);
            else if (what == "display" && args.size() > 1)
               MoveProjectorToMonitor(*projector, atoi(args[1].c_str()));
         }
         else if (action == "timeline" || action == "session")
         {
            const int lane = args.empty() ? -1 : args[0] == "audio" ? (int)Arrange::kLaneAudio : (int)Arrange::kLaneVideo;
            const uint64_t made = action == "timeline" ? AddNodeToArrangeTimeline(gn.index, lane) : AddNodeToArrangeSession(gn.index, lane);
            if (made == 0)
               outError = gn.typeName + " makes neither picture nor sound";
            outResult = { { "clip", made } };
         }
         else if (action == "pianoroll" && !args.empty())
         {
            PianoRollOpenPage(strtoull(args[0].c_str(), nullptr, 10));
            gPianoRollOpen = true;
         }
         else if (action == "preset" && !args.empty())
         {
            const auto* presets = FactoryPresets::For(gn.typeName);
            int index = -1;
            for (int i = 0; presets != nullptr && i < (int)presets->size(); i++)
               if ((*presets)[(size_t)i].name == args[0])
                  index = i;
            if (index < 0)
            {
               outError = "no preset '" + args[0] + "' on " + gn.typeName;
               return;
            }
            PushUndoCheckpoint("Preset");
            FactoryPresets::Apply(gn.node.get(), gn.typeName, index);
            gPatchDirty = true;
         }
         else
            outError = "node <node> rename <name>, params|viewport on|off, panel, window [close|fullscreen|display <n>], timeline|session [video|audio], preset <name>";
      }

      void Library(const std::string& action, const Words& args, json& outResult, std::string& outError)
      {
         SampleScanner& scanner = !args.empty() && args[0] == "media" ? gMediaScanner : gSampleScanner;
         const std::string folder = args.size() > 1 ? args[1] : std::string();
         if (action == "add" && !folder.empty())
         {
            scanner.AddFolder(folder);
            scanner.StartScan(folder);
         }
         else if (action == "remove" && !folder.empty())
            scanner.RemoveFolder(folder);
         else if (action == "refresh")
            scanner.StartScan(folder);
         else if (action.empty() || action == "list")
            outResult = { { "samples", gSampleScanner.Folders() }, { "media", gMediaScanner.Folders() } };
         else
            outError = "library list | add|remove samples|media <folder> | refresh samples|media [folder]";
      }
   }

   bool HandleTimelineRpc(const std::string& method, const nlohmann::json& params, nlohmann::json& outResult, std::string& outError)
   {
      if (method != "timeline" && method != "session" && method != "notes" && method != "perf" && method != "studio" &&
          method != "node" && method != "library" && method != "mod_curve" && method != "launch_quantize")
         return false;
      const std::string noun = params.value("noun", std::string());
      const std::string action = params.value("action", std::string());
      const Words args = params.value("args", Words());
      const json fields = params.value("fields", json::object());
      try
      {
         if (method == "timeline" && noun.empty())
            outResult = ModelJson();
         else if (method == "timeline")
            Timeline(noun, action, args, fields, outResult, outError);
         else if (method == "session")
         {
            // The first words of the older call: { action: scene|stop, scene: n }.
            Words all = args;
            if (params.contains("scene"))
               all.push_back(std::to_string(params.value("scene", 0)));
            Session(noun.empty() ? action : noun, all, fields, outResult, outError);
         }
         else if (method == "notes")
            Notes(action, args, fields, outResult, outError);
         else if (method == "perf")
            Perf(action, args, fields, outResult, outError);
         else if (method == "library")
            Library(action, args, outResult, outError);
         else if (method == "launch_quantize")
         {
            const std::string what = params.value("what", std::string("launch"));
            if (what == "launch")
               Transport::Instance().SetLaunchQuantize(params.value("value", 0));
            else if (what == "quantize")
               ShowSetQuantize() = params.value("value", 0);
            else
               ShowSetFadeMs() = std::clamp(params.value("value", 0.0f), 0.0f, 8000.0f);
            gPatchDirty = true;
         }
         else if (method == "node" || method == "mod_curve")
         {
            GraphNode* gn = FindNodeByIndex(params.value("index", -1));
            if (gn == nullptr)
               outError = "unknown node index";
            else if (method == "node")
               NodeMenu(action, *gn, args, outResult, outError);
            else
            {
               const int param = std::max(ParamIndexOfKeyOnNode(*gn, params.value("key", std::string())),
                                          ParamIndexOfKeyOnNode(*gn, "p." + params.value("key", std::string())));
               if (param < 0)
                  outError = "'" + params.value("key", std::string()) + "' is not a modulatable control of " + gn->typeName;
               else
               {
                  PushUndoCheckpoint();
                  Modulation::Instance().SetCurve(gn->index, param, std::clamp(params.value("curve", 0.0f), -1.0f, 1.0f));
                  gPatchDirty = true;
               }
            }
         }
         else if (action == "set")
            SampleStudioApply(fields.value("text", std::string()));
         else
            outResult = { { "text", SampleStudioSerialize() } };
      }
      catch (const nlohmann::json::exception& e)
      {
         outError = std::string("bad field value: ") + e.what();
      }
      if (outError.empty() && outResult.is_null())
         outResult = json::object();
      return true;
   }
}

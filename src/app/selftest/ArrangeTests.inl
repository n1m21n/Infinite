// Part of src/main.cpp (CanvasHooks.inl, inside the frame loop): the
// Arrangement Timeline self-tests ported from upstream (src/app/frame/
// FrameTests1.cpp, FrameTests2.cpp, StageFrameTestsA.cpp), adapted to this
// fork where it differs - whole-patch undo (respawns the graph), File > New
// starts with an empty timeline, PatchJson has no timeline yet. Every line
// ends OK or contains FAIL; the last one is the verdict.

      if (getenv("INFINITE_ARRANGETEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         auto TmpPath = [](const char* name) { return (std::filesystem::temp_directory_path() / name).string(); };

         // Every section drives Arrange::Model directly - that is the point of
         // WP1. Resetting keeps revision climbing (WP5b: it is the only change
         // signal, so a model that rewinds it could land back on the value the
         // audio topology was last built at and never rebuild).
         auto seedModel = [](Arrange::Model& m, int videoLanes, int audioLanes)
         {
            const uint64_t rev = m.revision;
            m = Arrange::Model();
            m.revision = rev + 1;
            for (int i = 0; i < videoLanes; i++) Arrange::AddLane(m, Arrange::kLaneVideo);
            for (int i = 0; i < audioLanes; i++) Arrange::AddLane(m, Arrange::kLaneAudio);
         };

         // --- A. Model invariants under the edit ops -----------------------
         {
            Arrange::Model m;
            seedModel(m, 2, 1);
            bool aOk = true;
            std::string why;

            Arrange::Clip c;
            c.start = 0;
            c.length = Arrange::kTicksPerBar;
            c.srcUid = 7;
            uint64_t first = 0, second = 0;
            aOk = aOk && Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &first);
            c.start = Arrange::kTicksPerBar;
            aOk = aOk && Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &second);
            aOk = aOk && m.lanes[0].clips.size() == 2 && first != second;

            // Overwrite straddling both: the left one is truncated, the right
            // one has its head eaten - the exact case index-based editing kept
            // getting wrong.
            c.start = Arrange::kPPQ * 2;
            c.length = Arrange::kPPQ * 4;
            uint64_t third = 0;
            aOk = aOk && Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &third);
            aOk = aOk && m.lanes[0].clips.size() == 3;
            aOk = aOk && Arrange::Validate(m, &why);

            // Splitting inside a clip yields two, and the ids are distinct.
            uint64_t rightHalf = 0;
            aOk = aOk && Arrange::Split(m, third, Arrange::kPPQ * 4, &rightHalf);
            aOk = aOk && rightHalf != 0 && rightHalf != third;
            aOk = aOk && Arrange::Validate(m, &why);

            // A clip that strictly contains an overwrite splits into two, so
            // the lane grows by one rather than losing the tail.
            {
               Arrange::Model m2;
               seedModel(m2, 1, 0);
               Arrange::Clip big;
               big.start = 0;
               big.length = Arrange::kPPQ * 16;
               uint64_t bigId = 0;
               Arrange::PlaceOverwrite(m2, m2.lanes[0].id, big, &bigId);
               Arrange::Clip mid;
               mid.start = Arrange::kPPQ * 4;
               mid.length = Arrange::kPPQ * 4;
               Arrange::PlaceOverwrite(m2, m2.lanes[0].id, mid);
               aOk = aOk && m2.lanes[0].clips.size() == 3 && Arrange::Validate(m2, &why);
            }

            // Groups: two members live, one member deleted dissolves the group
            // rather than leaving the singleton Validate rejects.
            {
               uint64_t gid = 0;
               aOk = aOk && Arrange::Group(m, { first, second }, &gid) && gid != 0;
               aOk = aOk && Arrange::Validate(m, &why);
               aOk = aOk && Arrange::Delete(m, { first });
               const Arrange::Clip* survivor = Arrange::FindClip(m, second);
               aOk = aOk && survivor != nullptr && survivor->groupId == 0;
               aOk = aOk && Arrange::Validate(m, &why);
            }

            // MoveClips is all-or-nothing: onto a lane of the wrong type it
            // must refuse, leaving the model exactly as it was.
            {
               const uint64_t before = m.revision;
               const bool refused = !Arrange::MoveClips(m, { second }, 0, 2); // video -> audio
               aOk = aOk && refused && m.revision == before;
            }

            // Anything that reaches the model without an edit op - the legacy
            // UI bridge appends without sorting - must still come out sorted,
            // non-overlapping and valid.
            {
               Arrange::Model m3;
               seedModel(m3, 1, 0);
               Arrange::Clip a1, a2;
               a1.id = m3.NewId(); a1.start = Arrange::kPPQ * 4; a1.length = Arrange::kPPQ * 4;
               a2.id = m3.NewId(); a2.start = 0;                 a2.length = Arrange::kPPQ * 6;
               m3.lanes[0].clips.push_back(a1);   // deliberately out of order
               m3.lanes[0].clips.push_back(a2);   // and overlapping
               Arrange::Normalize(m3);
               aOk = aOk && m3.lanes[0].clips.size() == 2 &&
                     m3.lanes[0].clips[0].start == 0 &&
                     m3.lanes[0].clips[0].End() == Arrange::kPPQ * 4 &&
                     Arrange::Validate(m3, &why);
            }

            printf("arrange model ops: %s%s%s\n", aOk ? "OK" : "FAIL",
                   why.empty() ? "" : "  reason: ", why.c_str());
            allOk = allOk && aOk;
         }

         // --- B. Fuzz: 2000 seeded random ops, Validate after every one ----
         {
            Arrange::Model m;
            seedModel(m, 3, 2);
            std::mt19937 rng(0xA44A47E5u);
            auto rnd = [&rng](int lo, int hi) { return lo + (int)(rng() % (uint32_t)(hi - lo + 1)); };

            bool bOk = true;
            std::string why;
            int applied = 0;
            for (int i = 0; i < 2000 && bOk; i++)
            {
               // Collect the live ids fresh each iteration - an op may have
               // deleted or split anything from the previous one.
               std::vector<uint64_t> ids;
               for (const Arrange::Lane& l : m.lanes)
                  for (const Arrange::Clip& c : l.clips)
                     ids.push_back(c.id);

               // Group ids and marker ids, also refreshed every iteration.
               std::vector<uint64_t> groups;
               for (const Arrange::Lane& l : m.lanes)
                  for (const Arrange::Clip& c : l.clips)
                     if (c.groupId != 0 &&
                         std::find(groups.begin(), groups.end(), c.groupId) == groups.end())
                        groups.push_back(c.groupId);

               const int op = rnd(0, 14);
               bool changed = false;
               switch (op)
               {
                  case 0: case 1: case 2:
                  {
                     Arrange::Clip c;
                     c.start = (Arrange::Tick)rnd(0, 64) * (Arrange::kPPQ / 4);
                     c.length = (Arrange::Tick)rnd(1, 16) * (Arrange::kPPQ / 4);
                     c.srcUid = (uint64_t)rnd(1, 5);
                     c.fadeIn = (Arrange::Tick)rnd(0, 4) * (Arrange::kPPQ / 4);
                     c.fadeOut = (Arrange::Tick)rnd(0, 4) * (Arrange::kPPQ / 4);
                     c.enabled = rnd(0, 1) != 0;
                     changed = Arrange::PlaceOverwrite(m, m.lanes[rnd(0, (int)m.lanes.size() - 1)].id, c);
                     break;
                  }
                  case 3:
                     if (!ids.empty())
                        changed = Arrange::MoveClips(m, { ids[rnd(0, (int)ids.size() - 1)] },
                                                     (Arrange::Tick)rnd(-8, 8) * (Arrange::kPPQ / 4),
                                                     rnd(-1, 1));
                     break;
                  case 4:
                     if (!ids.empty())
                        changed = Arrange::TrimEdge(m, ids[rnd(0, (int)ids.size() - 1)], rnd(0, 1),
                                                    (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                     break;
                  case 5:
                     if (!ids.empty())
                        changed = Arrange::Split(m, ids[rnd(0, (int)ids.size() - 1)],
                                                 (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                     break;
                  case 6:
                     if (ids.size() >= 2)
                     {
                        std::vector<uint64_t> pick = { ids[rnd(0, (int)ids.size() - 1)],
                                                       ids[rnd(0, (int)ids.size() - 1)] };
                        changed = (rnd(0, 1) == 0) ? Arrange::Group(m, pick)
                                                   : Arrange::DuplicateBlock(m, pick);
                     }
                     break;
                  case 7:
                     if (!ids.empty())
                        changed = Arrange::Delete(m, { ids[rnd(0, (int)ids.size() - 1)] });
                     break;
                  case 8:
                     if (!ids.empty())
                        changed = Arrange::SetEnabled(m, { ids[rnd(0, (int)ids.size() - 1)] }, Arrange::kToggle);
                     break;
                  // The group ops. Ungroup/RemoveFromGroup are the two that
                  // can strand a singleton group, which is invariant 3.
                  case 9:
                     if (!groups.empty())
                     {
                        const uint64_t g = groups[rnd(0, (int)groups.size() - 1)];
                        const int which = rnd(0, 3);
                        if (which == 0)
                           changed = Arrange::Ungroup(m, { g });
                        else if (which == 1)
                        {
                           const std::vector<uint64_t> members = Arrange::ClipsInGroup(m, g);
                           if (!members.empty())
                              changed = Arrange::RemoveFromGroup(m, { members[rnd(0, (int)members.size() - 1)] });
                        }
                        else if (which == 2)
                           changed = Arrange::TrimGroupEdge(m, g, rnd(0, 1),
                                                            (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                        else
                           changed = Arrange::ScaleGroup(m, g, rnd(0, 1),
                                                         (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                     }
                     break;
                  case 10:
                     if (!ids.empty())
                     {
                        // ExpandSelectionToGroups has no side effect, but it
                        // must never return an id the model doesn't hold.
                        const std::vector<uint64_t> sel =
                            Arrange::ExpandSelectionToGroups(m, { ids[rnd(0, (int)ids.size() - 1)] });
                        for (uint64_t id : sel)
                           bOk = bOk && Arrange::FindClip(m, id) != nullptr;
                     }
                     break;
                  // Lane ops. Add is capped so the fuzz doesn't just grow
                  // lanes forever, and remove takes whole lanes of clips with
                  // it - the path most likely to strand a group.
                  case 11:
                     if (m.lanes.size() < 8)
                        changed = Arrange::AddLane(m, rnd(0, 1), rnd(-1, (int)m.lanes.size())) != 0;
                     break;
                  case 12:
                     if (m.lanes.size() > 2)
                        changed = Arrange::RemoveLane(m, m.lanes[rnd(0, (int)m.lanes.size() - 1)].id);
                     break;
                  case 13:
                     if (m.lanes.size() > 1)
                        changed = Arrange::ReorderLane(m, m.lanes[rnd(0, (int)m.lanes.size() - 1)].id,
                                                       rnd(0, (int)m.lanes.size() - 1));
                     break;
                  // Markers, and the "a node went away" path.
                  default:
                  {
                     const int which = rnd(0, 3);
                     if (which == 0)
                        changed = Arrange::AddMarker(m, (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4), "m") != 0;
                     else if (!m.markers.empty())
                     {
                        const uint64_t mk = m.markers[rnd(0, (int)m.markers.size() - 1)].id;
                        if (which == 1)
                           changed = Arrange::MoveMarker(m, mk, (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                        else if (which == 2)
                           changed = Arrange::DeleteMarker(m, mk);
                        else
                           changed = Arrange::ClearSource(m, (uint64_t)rnd(1, 5));
                     }
                     break;
                  }
               }
               applied += changed ? 1 : 0;
               if (!Arrange::Validate(m, &why))
               {
                  printf("arrange fuzz: invariant broken at op %d (%d): %s\n", i, op, why.c_str());
                  bOk = false;
               }
            }
            printf("arrange fuzz: 2000 ops, %d changed the model  %s\n", applied, bOk ? "OK" : "FAIL");
            allOk = allOk && bOk;
         }

         // --- C. Tick save/load round trip, including markers and settings --
         {
            NewPatch();
            // gNodes is a vector, so the second SpawnNode can reallocate and
            // invalidate the first pointer - read what is needed immediately.
            GraphNode* spawned = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            const uint64_t cubeUid = spawned ? spawned->uid : 0;
            spawned = SpawnNode("Sphere", "3D", 200.0f, 0.0f);
            const uint64_t sphereUid = spawned ? spawned->uid : 0;
            bool cOk = cubeUid != 0 && sphereUid != 0;
            if (cOk)
            {
               Arrange::Model& m = gArrange;
               seedModel(m, 1, 1);
               Arrange::Clip c;
               c.start = Arrange::kPPQ * 3;          // deliberately off the bar
               c.length = Arrange::kPPQ * 5;
               c.srcUid = cubeUid;
               c.fadeIn = Arrange::kPPQ / 3;         // a triplet, exact in ticks
               c.gainDb = -6.0f;
               c.enabled = false;
               c.name = "clip one";
               uint64_t idA = 0, idB = 0;
               Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &idA);
               c.start = Arrange::kPPQ * 9;
               c.srcUid = sphereUid;
               c.enabled = true;
               c.name.clear();
               Arrange::PlaceOverwrite(m, m.lanes[1].id, c, &idB);
               Arrange::Group(m, { idA, idB });
               Arrange::AddMarker(m, Arrange::kTicksPerBar * 2, "chorus", 0xFF00FF00u);
               m.settings.timeDisplay = 1;
               m.settings.snapDivision = 8;
               m.settings.loop.enabled = true;
               m.settings.loop.start = 0;
               m.settings.loop.end = Arrange::kTicksPerBar * 4;
               m.settings.renderFps = 30;
               const uint64_t savedNextId = m.nextId;

               const std::string path = TmpPath("arrange_selftest_tick.inf");
               SavePatchTo(path);
               LoadPatchFrom(path);
               std::remove(path.c_str());

               const Arrange::Model& r = gArrange;
               cOk = r.lanes.size() == 2 && r.lanes[0].clips.size() == 1 && r.lanes[1].clips.size() == 1;
               if (cOk)
               {
                  const Arrange::Clip& ra = r.lanes[0].clips[0];
                  const Arrange::Clip& rb = r.lanes[1].clips[0];
                  // Ticks are exact - no epsilon, which is the whole reason
                  // the time base moved off doubles.
                  cOk = ra.start == Arrange::kPPQ * 3 && ra.length == Arrange::kPPQ * 5 &&
                        ra.fadeIn == Arrange::kPPQ / 3 && ra.gainDb == -6.0f &&
                        !ra.enabled && ra.name == "clip one" && ra.id == idA &&
                        rb.id == idB && ra.groupId != 0 && ra.groupId == rb.groupId;
                  // srcUid survives the whole respawn, which srcIndex could not.
                  GraphNode* ca = FindNodeByUid(ra.srcUid);
                  GraphNode* cb = FindNodeByUid(rb.srcUid);
                  cOk = cOk && ca != nullptr && cb != nullptr && ca->typeName == "Cube" &&
                        cb->typeName == "Sphere";
                  cOk = cOk && r.markers.size() == 1 && r.markers[0].pos == Arrange::kTicksPerBar * 2 &&
                        r.markers[0].name == "chorus" && r.markers[0].color == 0xFF00FF00u;
                  cOk = cOk && r.settings.timeDisplay == 1 && r.settings.snapDivision == 8 &&
                        r.settings.loop.enabled && r.settings.loop.end == Arrange::kTicksPerBar * 4 &&
                        r.settings.renderFps == 30;
                  // nextId is persisted, not recomputed: a reload must not be
                  // able to hand out an id a deleted clip already used.
                  cOk = cOk && r.nextId >= savedNextId;
                  std::string why;
                  cOk = cOk && Arrange::Validate(r, &why);
               }
            }
            printf("arrange tick roundtrip: %s\n", cOk ? "OK" : "FAIL");
            allOk = allOk && cOk;
         }

         // --- D. Legacy seconds patch converts to ticks --------------------
         {
            // 120 bpm -> 1 beat = 0.5 s, so 1.0 s is exactly 2 beats and
            // 2.0 s is 4. The transport line deliberately sits AFTER the clip
            // to prove the conversion waits for the whole file.
            const std::string path = TmpPath("arrange_selftest_legacy.inf");
            {
               std::ofstream f(path);
               f << "infinite-patch 1\n";
               f << "node 1 3D Cube\n";
               f << "end\n";
               f << "stream 0 0 1 0 0 V\n";
               f << "clip 0 1 2 1 0 1 0.25 0 -3 0.5 1\n";
               f << "transport 120 4 4 0 0\n";
            }
            Patch::Data loaded;
            std::string err;
            const bool read = Patch::Read(path, loaded, err);
            std::remove(path.c_str());

            bool dOk = read && loaded.streams.size() == 1 && loaded.streams[0].clips.size() == 1;
            if (dOk)
            {
               const Patch::ClipRecord& c = loaded.streams[0].clips[0];
               dOk = c.startTick == Arrange::kPPQ * 2 && c.lengthTick == Arrange::kPPQ * 4 &&
                     c.fadeInTick == Arrange::kPPQ / 2 && c.gainDb == -3.0f &&
                     c.legacySrcIndex == 1 && c.srcUid == 0 && c.enabled;
            }
            printf("arrange legacy seconds -> ticks: %s\n", dOk ? "OK" : "FAIL");
            allOk = allOk && dOk;
         }

         // --- E. Undo/redo, interleaved with node add and delete -----------
         {
            NewPatch();
            GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            bool eOk = cube != nullptr;
            const uint64_t cubeUid = cube ? cube->uid : 0;
            const int cubeIndex = cube ? cube->index : -1;
            if (eOk)
            {
               seedModel(gArrange, 1, 0);
               Arrange::Clip c;
               c.length = Arrange::kTicksPerBar;
               c.srcUid = cubeUid;
               uint64_t clipId = 0;
               Arrange::PlaceOverwrite(gArrange, gArrange.lanes[0].id, c, &clipId);

               // A timeline-only gesture. Turbo: undo is a whole-patch snapshot
               // that respawns the graph, so the node comes back by uid (upstream
               // also keeps its pointer, which this fork does not promise).
               PushArrangeUndoSnapshot(gArrange);
               Arrange::MoveClips(gArrange, { clipId }, Arrange::kTicksPerBar, 0);
               eOk = eOk && gArrange.lanes[0].clips[0].start == Arrange::kTicksPerBar;

               Undo();
               eOk = eOk && gArrange.lanes[0].clips[0].start == 0;
               eOk = eOk && FindNodeByUid(cubeUid) != nullptr;   // same node, by uid
               Redo();
               eOk = eOk && gArrange.lanes[0].clips[0].start == Arrange::kTicksPerBar;
               Undo();

               // A graph gesture: deleting the node must leave the clip in
               // place but offline, and undo must re-attach it by uid.
               // Turbo: the undo above respawned the graph, so the saved index
               // is stale - look the node up again by uid.
               GraphNode* live = FindNodeByUid(cubeUid);
               eOk = eOk && live != nullptr && !gArrange.lanes.empty() && !gArrange.lanes[0].clips.empty();
               if (eOk)
               {
               (void)cubeIndex;
               const size_t clipsBefore = gArrange.lanes[0].clips.size();
               PushUndoCheckpoint();
               RemoveNodeByIndex(live->index);
               // Offline (WP5): the clip stays, its source is cleared to 0
               // so it draws "Unassigned"; the link lives in the undo entry.
               eOk = eOk && gArrange.lanes.size() == 1 &&
                     gArrange.lanes[0].clips.size() == clipsBefore &&
                     gArrange.lanes[0].clips[0].srcUid == 0 &&
                     FindNodeByUid(cubeUid) == nullptr;

               Undo();
               eOk = eOk && gArrange.lanes.size() == 1 && gArrange.lanes[0].clips.size() == clipsBefore;
               if (eOk)
               {
                  const uint64_t restored = gArrange.lanes[0].clips[0].srcUid;
                  GraphNode* back = FindNodeByUid(restored);
                  eOk = restored == cubeUid && back != nullptr && back->typeName == "Cube";
               }
               std::string why;
               eOk = eOk && Arrange::Validate(gArrange, &why);
               }
            }
            printf("arrange undo redo + node delete: %s\n", eOk ? "OK" : "FAIL");
            allOk = allOk && eOk;
         }

         // --- F. File->New clears the model --------------------------------
         {
            NewPatch();
            // Turbo: New starts with an empty timeline (upstream seeds four
            // video + four audio lanes); either way nothing may survive.
            size_t clips = 0;
            for (const Arrange::Lane& l : gArrange.lanes)
               clips += l.clips.size();
            const bool fOk = clips == 0 && gArrange.markers.empty() &&
                             Arrange::ArrangementEnd(gArrange) == 0;
            printf("arrange new patch: %s\n", fOk ? "OK" : "FAIL");
            allOk = allOk && fOk;
         }

         // --- G. Unknown tags and malformed lines ---------------------------
         {
            const std::string path = TmpPath("arrange_selftest_malformed.inf");
            {
               std::ofstream f(path);
               f << "infinite-patch 1\n";
               f << "node 1 3D Cube\n";
               f << "end\n";
               f << "stream 1\n";                      // malformed, kept with defaults
               f << "stream 0 0 1 0 0 V\n";
               f << "arrangefuture 1 2 3\n";           // from a newer build
               f << "cliptick 7 0 0 960 0\n";          // out-of-range lane, dropped
               f << "cliptick 0 0 0 0 0\n";            // zero length, dropped
               f << "cliptick 0 0 -5 960 0\n";         // negative start, dropped
               f << "cliptick 0 0 0 960 0 0 99999 0\n"; // fade past the end, clamped
               f << "marker 0 -4 0 bad\n";             // negative position, dropped
               f << "marker 0 1920 4278190080 good\n";
            }
            Patch::Data loaded;
            std::string err;
            const bool read = Patch::Read(path, loaded, err);
            std::remove(path.c_str());

            bool gOk = read && loaded.streams.size() == 2;
            if (gOk)
            {
               // The malformed `stream 1` line is kept with defaults rather
               // than dropped: the clip lines below address their lane by
               // position, so dropping it would move every later clip onto the
               // wrong lane. All four cliptick lines name lane 0, which IS
               // that malformed line - only the last one survives its checks.
               gOk = loaded.streams[0].type == 1 && loaded.streams[0].opacity == 1.0f &&
                     loaded.streams[0].name.empty() &&
                     loaded.streams[0].clips.size() == 1 &&
                     loaded.streams[0].clips[0].fadeInTick == 960 &&
                     loaded.streams[1].clips.empty() &&
                     loaded.markers.size() == 1 && loaded.markers[0].name == "good";
            }
            printf("arrange malformed input: %s\n", gOk ? "OK" : "FAIL");
            allOk = allOk && gOk;
         }

         // --- H. JSON parity -----------------------------------------------
         {
            NewPatch();
            GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            seedModel(gArrange, 1, 0);
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            c.srcUid = cube ? cube->uid : 0;
            Arrange::PlaceOverwrite(gArrange, gArrange.lanes[0].id, c);
            Arrange::AddMarker(gArrange, Arrange::kPPQ, "m");

            // Turbo: PatchJson carries no timeline yet, so the parity checked
            // here is the in-memory text form the RPC / MCP uses (WriteText /
            // ReadText), plus the new speed / reverse / session fields.
            if (!gArrange.lanes[0].clips.empty())
            {
               gArrange.lanes[0].clips[0].speed = 2.0f;
               gArrange.lanes[0].clips[0].reverse = true;
            }
            Arrange::Lane& slotLane = gArrange.lanes[0];
            slotLane.slots.resize(2);
            slotLane.slots[1].clip.id = gArrange.NewId();
            slotLane.slots[1].clip.length = Arrange::kTicksPerBar;
            slotLane.slots[1].loopTicks = Arrange::kPPQ * 2;
            slotLane.slots[1].followAction = Arrange::kFollowNext;
            gArrange.scenes.resize(2);
            gArrange.scenes[1].name = "drop";
            gArrange.scenes[1].tempo = 128.0f;
            gArrange.sessionQuantize = 1;
            const Arrange::Model sent = gArrange;
            std::string text, err;
            Patch::Data back;
            const bool wrote = Patch::WriteText(BuildPatchData(), text, err);
            const bool read = wrote && Patch::ReadText(text, back, err);
            Arrange::Model r;
            if (read)
               PatchDataToArrangeModel(back, r);
            bool hOk = read && r.lanes.size() == sent.lanes.size() && r.markers.size() == 1 &&
                       text.find("cliptick ") != std::string::npos && text.find("clipspeed ") != std::string::npos &&
                       text.find("slotclip ") != std::string::npos;
            if (hOk)
            {
               const Arrange::Clip& rc = r.lanes[0].clips[0];
               hOk = rc.speed == 2.0f && rc.reverse && r.lanes[0].slots.size() == 2 && r.lanes[0].slots[1].Has() &&
                     r.lanes[0].slots[1].loopTicks == Arrange::kPPQ * 2 &&
                     r.lanes[0].slots[1].followAction == Arrange::kFollowNext && r.scenes.size() == 2 &&
                     r.scenes[1].name == "drop" && r.scenes[1].tempo == 128.0f && r.sessionQuantize == 1;
            }
            printf("arrange text parity (speed, reverse, session): %s\n", hOk ? "OK" : "FAIL");
            allOk = allOk && hOk;
         }

         // --- I. Ids are unique and never reused ---------------------------
         // The two defects the WP1 review found, both of which reached disk:
         // the legacy UI's copy paths clone a clip record verbatim (id and
         // all), and undo used to restore nextId along with the snapshot.
         {
            NewPatch();
            seedModel(gArrange, 1, 0);
            const uint64_t laneId = gArrange.lanes[0].id;
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            uint64_t firstId = 0;
            Arrange::PlaceOverwrite(gArrange, laneId, c, &firstId);

            // (1) A duplicate id arriving from outside an edit op - exactly
            // what Cmd+D used to produce - must be re-minted, not accepted.
            Arrange::Clip clone = gArrange.lanes[0].clips[0];
            clone.start = Arrange::kTicksPerBar * 2;
            gArrange.lanes[0].clips.push_back(clone);   // same id, deliberately
            gArrange.revision++;                         // a direct edit is still a change
            Arrange::Normalize(gArrange);
            std::string why;
            bool iOk = gArrange.lanes[0].clips.size() == 2 &&
                       gArrange.lanes[0].clips[0].id != gArrange.lanes[0].clips[1].id &&
                       Arrange::Validate(gArrange, &why);

            // (2) nextId only ever climbs. Snapshot, spend an id, undo, and
            // the next id handed out must still be a fresh one.
            PushArrangeUndoSnapshot(gArrange);
            uint64_t spentId = 0;
            Arrange::Clip extra;
            extra.start = Arrange::kTicksPerBar * 8;
            extra.length = Arrange::kTicksPerBar;
            Arrange::PlaceOverwrite(gArrange, laneId, extra, &spentId);
            Undo();
            iOk = iOk && spentId != 0 && gArrange.nextId > spentId;

            Arrange::Clip after;
            after.start = Arrange::kTicksPerBar * 12;
            after.length = Arrange::kTicksPerBar;
            uint64_t afterId = 0;
            Arrange::PlaceOverwrite(gArrange, laneId, after, &afterId);
            iOk = iOk && afterId != spentId && Arrange::Validate(gArrange, &why);

            printf("arrange id uniqueness: %s\n", iOk ? "OK" : "FAIL");
            allOk = allOk && iOk;
         }

         // --- J. Lane mix + per-clip compositing: round trip, legacy, undo --
         {
            NewPatch();
            Arrange::Model& m = gArrange;
            seedModel(m, 1, 2);
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            uint64_t idNormal = 0, idScreen = 0;
            Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &idNormal);
            c.start = Arrange::kTicksPerBar * 2;
            c.blendMode = 5;
            Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &idScreen);
            m.lanes[0].opacity = 0.4f;
            m.lanes[1].mute = true;
            m.lanes[1].pan = -0.5f;
            m.lanes[1].gainDb = -3.0f;
            m.lanes[2].solo = true;

            const std::string path = TmpPath("arrange_selftest_mix.inf");
            SavePatchTo(path);
            LoadPatchFrom(path);
            std::remove(path.c_str());
            const Arrange::Model& r = gArrange;
            bool rtOk = r.lanes.size() == 3 && r.lanes[0].clips.size() == 2;
            if (rtOk)
            {
               const Arrange::Clip* a = Arrange::FindClip(r, idNormal);
               const Arrange::Clip* b = Arrange::FindClip(r, idScreen);
               rtOk = a != nullptr && b != nullptr && a->blendMode == 0 && b->blendMode == 5 &&
                      r.lanes[0].opacity == 0.4f && r.lanes[1].mute && !r.lanes[1].solo &&
                      r.lanes[1].pan == -0.5f && r.lanes[1].gainDb == -3.0f && r.lanes[2].solo &&
                      !r.lanes[2].mute && r.lanes[0].blendMode == 0;
            }

            // A file from before per-clip compositing: the stream line's
            // lane-wide mode lands on every clip that has no clipblend line
            // of its own, and the lane field is left at 0.
            const std::string legacyPath = TmpPath("arrange_selftest_laneblend.inf");
            {
               std::ofstream f(legacyPath);
               f << "infinite-patch 1\n";
               f << "node 1 3D Cube\n";
               f << "end\n";
               f << "stream 0 7 0.5 0 0 V\n";
               f << "streamid 0 50\n";
               f << "cliptick 0 51 0 960 0\n";
               f << "cliptick 0 52 1920 960 0\n";
               f << "clipblend 0 52 3\n";
               f << "clipblend 0 99 4\n"; // no such clip: ignored
               f << "stream 1 0 1 0 0 A\n";
               f << "streammix 1 1 0\n";
               f << "streammix 9 1 1\n"; // out-of-range stream: ignored
            }
            Patch::Data legacy;
            std::string err;
            const bool read = Patch::Read(legacyPath, legacy, err);
            std::remove(legacyPath.c_str());
            Arrange::Model lm;
            if (read)
               PatchDataToArrangeModel(legacy, lm);
            bool legOk = read && lm.lanes.size() == 2 && lm.lanes[0].clips.size() == 2;
            if (legOk)
            {
               const Arrange::Clip* l51 = Arrange::FindClip(lm, 51);
               const Arrange::Clip* l52 = Arrange::FindClip(lm, 52);
               legOk = l51 != nullptr && l52 != nullptr && l51->blendMode == 7 && l52->blendMode == 3 &&
                       lm.lanes[0].blendMode == 0 && lm.lanes[1].mute && !lm.lanes[1].solo;
               // And back out: the stream field is written as 0, each clip
               // carries its own mode.
               Patch::Data back;
               ArrangeModelToPatchData(lm, back);
               legOk = legOk && back.streams[0].blendMode == 0 && back.streams[0].clips[0].blendMode == 7 &&
                       back.streams[0].clips[1].blendMode == 3 && back.streams[1].mute;
            }

            // Mute and clip compositing are undo state; a gesture that ends
            // where it started leaves no entry.
            bool undoOk = true;
            {
               const uint64_t audioLane = gArrange.lanes[1].id;
               const bool wasMuted = Arrange::FindLane(gArrange, audioLane)->mute;
               ArrangeGestureBegin();
               Arrange::FindLane(gArrange, audioLane)->mute = !wasMuted;
               gArrange.revision++;
               ArrangeGestureEnd();
               Undo();
               undoOk = Arrange::FindLane(gArrange, audioLane)->mute == wasMuted;
               ArrangeEdit([&]()
               {
                  if (Arrange::Clip* cc = Arrange::FindClip(gArrange, idNormal))
                  {
                     cc->blendMode = 9;
                     gArrange.revision++;
                  }
               });
               const bool applied = Arrange::FindClip(gArrange, idNormal)->blendMode == 9;
               Undo();
               undoOk = undoOk && applied && Arrange::FindClip(gArrange, idNormal)->blendMode == 0;
               Redo();
               undoOk = undoOk && Arrange::FindClip(gArrange, idNormal)->blendMode == 9;
               const float panWas = Arrange::FindLane(gArrange, audioLane)->pan;
               ArrangeGestureBegin();
               Arrange::FindLane(gArrange, audioLane)->pan = 0.75f;
               gArrange.revision++;
               Arrange::FindLane(gArrange, audioLane)->pan = panWas; // dragged back
               gArrange.revision++;
               ArrangeGestureEnd();
               Undo(); // must undo the blend edit, not an empty pan gesture
               undoOk = undoOk && Arrange::FindClip(gArrange, idNormal)->blendMode == 0 &&
                        Arrange::FindLane(gArrange, audioLane)->pan == panWas;
            }
            const bool jOk = rtOk && legOk && undoOk;
            printf("arrange lane mix + clip compositing: %s (round trip %d, legacy lane blend %d, undo %d)\n",
                   jOk ? "OK" : "FAIL", (int)rtOk, (int)legOk, (int)undoOk);
            allOk = allOk && jOk;
         }

         printf("arrange test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      if (getenv("INFINITE_ARRANGEAUDIOTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         auto TmpPath = [](const char* name) { return (std::filesystem::temp_directory_path() / name).string(); };

         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kBpm = 120.0;            // 2 beats per second
         const double kSamplesPerBeat = kSr * 60.0 / kBpm;

         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         const bool savedMode = gArrangeTimelineMode;

         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 0.0f, 0.0f);
         const bool spawned = oscGn != nullptr && oscGn->node != nullptr;
         printf("arrange audio spawn oscillator: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {
            const uint64_t oscUid = oscGn->uid;
            const int oscIndex = oscGn->index;

            // One audio lane, clips filled in per section. Built straight into
            // gArrange (WP5b); revision keeps climbing across the reset so the
            // rebuild trigger never sees it rewind onto an old built value.
            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            Arrange::FindLane(gArrange, laneId)->name = "A1";
            (void)oscIndex;

            auto clearClips = [&]()
            {
               std::vector<uint64_t> ids;
               for (const Arrange::Clip& c : Arrange::FindLane(gArrange, laneId)->clips)
                  ids.push_back(c.id);
               Arrange::Delete(gArrange, ids);
            };
            auto addClip = [&](double startBeat, double lengthBeats, bool enabled)
            {
               Arrange::Clip c;
               c.start = Arrange::BeatsToTicks(startBeat);
               c.length = Arrange::BeatsToTicks(lengthBeats);
               c.srcUid = oscUid;
               c.enabled = enabled;
               Arrange::PlaceOverwrite(gArrange, laneId, c);
            };
            // Rebuilds the per-frame trigger fired while a render ran - it is
            // called once per block, so crossing a clip boundary with it
            // running is exactly the "a boundary must never rebuild" check.
            int staleRebuildsDuringRender = 0;

            gArrangeTimelineMode = true;
            tr.SetTempo((float)kBpm);
            tr.SetLoop(false, 0.0, 0.0);
            tr.SetPlaying(true);
            tr.NotifyAudioEngineStarted(kSr); // Turbo: the offline audio clock

            // Renders [startBeat, startBeat + numBlocks*kBlock samples) and
            // returns channel 0, concatenated. Rebuilds the topology first,
            // then prepares every node by hand: the PrepareToPlay loop inside
            // RebuildAudioTopology keys off a live device or an offline render
            // job, and this fixture has neither.
            // Params reach an AudioNode through its mailbox, which
            // CookIfNeeded fills - a node that has never been cooked runs on
            // its constructor defaults with an empty mailbox and produces
            // nothing. The main loop does this every frame; this fixture runs
            // its whole life inside one.
            int fixtureCookFrame = 1000000;
            auto cookAll = [&]()
            {
               fixtureCookFrame++;
               for (GraphNode& gn : gNodes)
                  gn.node->CookIfNeeded(fixtureCookFrame);
            };

            std::vector<float> lastChan1; // channel 1 of the most recent render()
            auto render = [&](double startBeat, int numBlocks)
            {
               ArrangeFrameUpdate(); // Turbo: lane mix + timeline mode reach the engine per frame
               RebuildAudioTopology();
               gArrangeAudioSig = ArrangeAudioSignature();
               cookAll();
               for (GraphNode& gn : gNodes)
                  if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(startBeat);

               std::vector<float> chan0((size_t)kBlock), chan1((size_t)kBlock);
               float* chans[2] = { chan0.data(), chan1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;

               std::vector<float> out;
               out.reserve((size_t)kBlock * (size_t)numBlocks);
               lastChan1.clear();
               staleRebuildsDuringRender = 0;
               for (int b = 0; b < numBlocks; b++)
               {
                  if (ArrangeAudioRebuildIfStale())
                     staleRebuildsDuringRender++;
                  AudioEngine::Instance().RenderOfflineBlock(buffer); // Turbo: advances the transport
                  out.insert(out.end(), chan0.begin(), chan0.end());
                  lastChan1.insert(lastChan1.end(), chan1.begin(), chan1.end());
               }
               return out;
            };

            // Peak |x| over the samples covering [fromBeat, toBeat) of a
            // render that started at `originBeat`.
            auto peakOverBeats = [&](const std::vector<float>& x, double originBeat,
                                     double fromBeat, double toBeat)
            {
               const long long lo = std::max(0LL, (long long)((fromBeat - originBeat) * kSamplesPerBeat));
               const long long hi = std::min((long long)x.size(), (long long)((toBeat - originBeat) * kSamplesPerBeat));
               float peak = 0.0f;
               for (long long i = lo; i < hi; i++)
                  peak = std::max(peak, std::fabs(x[(size_t)i]));
               return peak;
            };

            // --- A. Two abutting clips of the same node are both audible ----
            // The original bug: the per-frame set of active srcIndex never
            // changed across the seam, so no rebuild happened and the stale
            // single-clip window silenced everything after the first clip.
            {
               clearClips();
               addClip(0.0, 2.0, true);
               addClip(2.0, 2.0, true);
               const std::vector<float> x = render(0.0, 800); // 800*256 = 204800 samples = 4.27 beats

               const float first = peakOverBeats(x, 0.0, 0.2, 1.8);
               const float second = peakOverBeats(x, 0.0, 2.2, 3.8);
               // The seam itself: abutting windows skip the declick, so the
               // signal must run straight through rather than dip to silence.
               const float seam = peakOverBeats(x, 0.0, 1.98, 2.02);
               // Two clip boundaries crossed (beat 2 and beat 4) with the
               // per-frame trigger polled every block: zero rebuilds.
               const bool aOk = first > 0.05f && second > 0.05f && seam > 0.05f &&
                                staleRebuildsDuringRender == 0;
               printf("arrange audio abutting clips: %s (first %.4f, second %.4f, seam %.4f, boundary rebuilds %d)\n",
                      aOk ? "OK" : "FAIL", first, second, seam, staleRebuildsDuringRender);
               allOk = allOk && aOk;
            }

            // --- B. Onset lands on the scheduled sample --------------------
            {
               clearClips();
               addClip(2.0, 2.0, true);
               const std::vector<float> x = render(0.0, 800);

               const long long expected = (long long)(2.0 * kSamplesPerBeat);
               long long firstAudible = -1;
               for (size_t i = 0; i < x.size(); i++)
                  if (std::fabs(x[i]) > 1e-5f) { firstAudible = (long long)i; break; }
               // The declick ramp is zero at exactly the onset sample and the
               // oscillator's own phase starts near zero, so the first sample
               // over the noise floor lands a hair after the scheduled one -
               // never before it, and never a UI frame later.
               const bool bOk = firstAudible >= expected && (firstAudible - expected) <= 8;
               printf("arrange audio onset: %s (scheduled %lld, first audible %lld, error %lld samples)\n",
                      bOk ? "OK" : "FAIL", expected, firstAudible, firstAudible - expected);
               allOk = allOk && bOk;
            }

            // --- C. A disabled clip is silent -------------------------------
            // `enabled` was not read by the audio path at all before WP3.
            {
               clearClips();
               addClip(0.0, 4.0, false);
               const std::vector<float> x = render(0.0, 400);
               const float peak = peakOverBeats(x, 0.0, 0.0, 2.0);
               const bool cOk = peak < 1e-6f;
               printf("arrange audio disabled clip: %s (peak %.8f)\n", cOk ? "OK" : "FAIL", peak);
               allOk = allOk && cOk;
            }

            // --- D. Paused in Timeline mode is silent -----------------------
            {
               clearClips();
               addClip(0.0, 4.0, true);
               tr.SetPlaying(false);
               const std::vector<float> x = render(0.0, 200);
               const float peak = peakOverBeats(x, 0.0, 0.0, 1.0);
               tr.SetPlaying(true);
               const bool dOk = peak < 1e-6f;
               printf("arrange audio paused: %s (peak %.8f)\n", dOk ? "OK" : "FAIL", peak);
               allOk = allOk && dOk;
            }

            // --- E. Seeking from clip A into clip B of the same node --------
            // The other half of the original bug: the set of active srcIndex
            // is identical on both sides of the seek, so nothing rebuilt and
            // clip B played silence.
            {
               clearClips();
               addClip(0.0, 2.0, true);
               addClip(4.0, 2.0, true);
               render(0.5, 100);                     // land inside clip A
               const std::vector<float> x = render(4.5, 200); // jump into clip B
               const float peak = peakOverBeats(x, 4.5, 4.6, 5.5);
               const bool eOk = peak > 0.05f;
               printf("arrange audio seek across clips: %s (peak %.4f)\n", eOk ? "OK" : "FAIL", peak);
               allOk = allOk && eOk;
            }

            // --- F. A rebuild mid-clip does not break the signal ------------
            // Editing an unrelated lane rebuilds the whole topology. With the
            // schedule carried on the terminal (and PDC state living outside
            // the topology), the block after the rebuild must continue the
            // same envelope rather than restart it.
            {
               clearClips();
               addClip(0.0, 8.0, true);
               ArrangeFrameUpdate(); // Turbo: lane mix + timeline mode reach the engine per frame
               RebuildAudioTopology();
               gArrangeAudioSig = ArrangeAudioSignature();
               cookAll();
               for (GraphNode& gn : gNodes)
                  if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(1.0);

               std::vector<float> chan0((size_t)kBlock), chan1((size_t)kBlock);
               float* chans[2] = { chan0.data(), chan1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;

               // The rebuild goes through the same per-frame trigger the main
               // loop runs (WP5b: revision is the only change signal). It is
               // polled before every block, so the 39 blocks without an edit
               // are 39 no-op frames: exactly one rebuild over the whole run,
               // and the edit moves revision by exactly one.
               float beforePeak = 0.0f, afterPeak = 0.0f;
               const unsigned long long rebuildsBefore = gAudioTopologyRebuildCount;
               uint64_t otherLane = 0;
               bool bumpedOnce = false, noOpFrameQuiet = false;
               int triggered = 0;
               for (int b = 0; b < 40; b++)
               {
                  if (b == 20)
                  {
                     // An edit on a *different* lane - the clip under the
                     // playhead is untouched.
                     const uint64_t rev = gArrange.revision;
                     otherLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
                     bumpedOnce = gArrange.revision == rev + 1;
                  }
                  if (ArrangeAudioRebuildIfStale())
                     triggered++;
                  if (b == 20)
                     noOpFrameQuiet = !ArrangeAudioRebuildIfStale(); // same frame again: nothing changed
                  AudioEngine::Instance().RenderOfflineBlock(buffer); // Turbo: advances the transport
                  for (int i = 0; i < kBlock; i++)
                  {
                     if (b == 19) beforePeak = std::max(beforePeak, std::fabs(chan0[i]));
                     if (b == 20) afterPeak = std::max(afterPeak, std::fabs(chan0[i]));
                  }
               }
               const unsigned long long rebuilds = gAudioTopologyRebuildCount - rebuildsBefore;
               const bool fOk = beforePeak > 0.05f && afterPeak > 0.05f &&
                                std::fabs(afterPeak - beforePeak) < 0.25f * beforePeak &&
                                bumpedOnce && triggered == 1 && rebuilds == 1 && noOpFrameQuiet;
               printf("arrange audio rebuild mid-clip: %s (before %.4f, after %.4f, revision +1 %d, rebuilds %llu over 40 polled frames, no-op frame quiet %d)\n",
                      fOk ? "OK" : "FAIL", beforePeak, afterPeak, (int)bumpedOnce, rebuilds, (int)noOpFrameQuiet);
               allOk = allOk && fOk;
               Arrange::RemoveLane(gArrange, otherLane);
            }

            // --- H. Lane mix strip: mute, solo, pan, gain -------------------
            // The header's S / M / pan / gain, read by the schedule at
            // rebuild time. Mixer's rules: a muted lane is silent, and any
            // soloed audio lane silences every unsoloed one.
            {
               clearClips();
               addClip(0.0, 4.0, true);
               Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId);
               auto bump = [&]() { gArrange.revision++; };

               const std::vector<float> ref = render(0.0, 200);
               const float refL = peakOverBeats(ref, 0.0, 0.2, 1.0);
               const float refR = peakOverBeats(lastChan1, 0.0, 0.2, 1.0);

               ln->mute = true; bump();
               const float muted = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->mute = false; bump();

               const uint64_t soloLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
               Arrange::FindLane(gArrange, soloLane)->solo = true; bump();
               const float othersSoloed = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->solo = true; bump();
               const float bothSoloed = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               Arrange::RemoveLane(gArrange, soloLane);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->solo = false; bump();

               ln->pan = -1.0f; bump();
               const float hardL = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               const float hardLR = peakOverBeats(lastChan1, 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->pan = 0.0f;
               ln->gainDb = -6.0206f; bump();
               const float halfGain = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->gainDb = 0.0f; bump();

               // Centre is unity per side (equal-power * sqrt2), hard left is
               // sqrt2 on L and nothing on R.
               const bool hOk = refL > 0.05f && std::fabs(refL - refR) < 0.01f * refL && muted < 1e-6f &&
                                othersSoloed < 1e-6f && bothSoloed > 0.9f * refL &&
                                std::fabs(hardL - refL * 1.41421356f) < 0.02f * refL && hardLR < 1e-4f &&
                                std::fabs(halfGain - 0.5f * refL) < 0.02f * refL;
               printf("arrange audio lane mix: %s (centre L %.4f R %.4f, muted %.8f, other soloed %.8f, both soloed %.4f, "
                      "hard-left L %.4f R %.8f, -6dB %.4f)\n",
                      hOk ? "OK" : "FAIL", refL, refR, muted, othersSoloed, bothSoloed, hardL, hardLR, halfGain);
               allOk = allOk && hOk;
            }

            // --- G. Mode resets to Canvas on New and on Open ---------------
            {
               gArrangeTimelineMode = true;
               NewPatch();
               const bool afterNew = !gArrangeTimelineMode;

               gArrangeTimelineMode = true;
               const bool afterOpen = !LoadPatchFrom("/nonexistent-arrangeaudiotest.ifp") ||
                                      !gArrangeTimelineMode;
               // A failed open must NOT reset the mode - it never became a new
               // document - so re-check with a real round trip through a file
               // this fixture writes itself.
               bool afterRealOpen = true;
               {
                  const std::string path = TmpPath("infinite-arrangeaudiotest.inf");
                  SpawnNode("Oscillator", "Synthesizers", 0.0f, 0.0f); // a patch with no nodes will not save
                  const bool saved = SavePatchTo(path);
                  if (saved)
                  {
                     gArrangeTimelineMode = true;
                     const bool loaded = LoadPatchFrom(path);
                     afterRealOpen = loaded && !gArrangeTimelineMode;
                     if (!afterRealOpen)
                        printf("  [diag] saved %d loaded %d status '%s'\n", (int)saved, (int)loaded, gPatchStatus.c_str());
                     remove(path.c_str());
                  }
                  else
                  {
                     printf("  [diag] save failed: '%s'\n", gPatchStatus.c_str());
                  }
               }
               const bool gOk = afterNew && afterOpen && afterRealOpen;
               printf("arrange audio mode resets: %s (new %d, failed-open %d, open %d)\n",
                      gOk ? "OK" : "FAIL", (int)afterNew, (int)afterOpen, (int)afterRealOpen);
               allOk = allOk && gOk;
            }
         }

         tr.NotifyAudioEngineStopped();
         tr.SetPlaying(true);
         gArrangeTimelineMode = savedMode;
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         ArrangeFrameUpdate(); // Turbo: lane mix + timeline mode reach the engine per frame
         RebuildAudioTopology();
         gArrangeAudioSig = ArrangeAudioSignature();

         printf("arrange audio test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Turbo 0.72: INFINITE_ARRANGEMULTITRACKTEST - the owner's case
      // (docs/plans/TIMELINE-ANALYSIS.md 3.1): three audio tracks side by side,
      // each with its own source, must all be heard live and all reach a WAV
      // render. Sources are Oscillators, which play by themselves (a clip only
      // gates a live node: it does not record). Frame 4 measures the live mix
      // offline and queues a WAV render through the real queue; the main loop
      // pumps it; frame 90 reads the file back.
      if (getenv("INFINITE_ARRANGEMULTITRACKTEST") != nullptr && (frameId == 4 || frameId == 90))
      {
         static bool sOk = true;
         static float sSinglePeak = 0.0f;
         static std::string sWavPath;
         static bool sWavQueued = false;
         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kBpm = 120.0;
         const double kSamplesPerBeat = kSr * 60.0 / kBpm;
         if (frameId == 4)
         {
            NewPatch();
            const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
            const double engineSr = AudioEngine::Instance().SampleRate();
            AudioEngine::Instance().Stop();
            tr.NotifyAudioEngineStopped();

            std::vector<uint64_t> uids;
            for (int i = 0; i < 3; i++)
               if (GraphNode* gn = SpawnNode("Oscillator", "Synths", 200.0f * (float)i, 0.0f))
                  uids.push_back(gn->uid);
            const bool spawned = uids.size() == 3;
            printf("arrange multitrack spawn 3 oscillators: %s\n", spawned ? "OK" : "FAIL");
            sOk = sOk && spawned;

            std::vector<uint64_t> lanes;
            if (spawned)
            {
               // What the owner did: one clip per track, all at bar 1, 2 bars long.
               const uint64_t rev = gArrange.revision;
               gArrange = Arrange::Model();
               gArrange.revision = rev + 1;
               for (int i = 0; i < 3; i++)
               {
                  lanes.push_back(Arrange::AddLane(gArrange, Arrange::kLaneAudio));
                  Arrange::Clip c;
                  c.start = 0;
                  c.length = Arrange::kTicksPerBar * 2;
                  c.srcUid = uids[(size_t)i];
                  Arrange::PlaceOverwrite(gArrange, lanes.back(), c);
               }
               gArrangeTimelineMode = true;
               tr.SetTempo((float)kBpm);
               tr.SetLoop(false, 0.0, 0.0);
               tr.SetPlaying(true);
               tr.NotifyAudioEngineStarted(kSr);

               int cookFrame = 3000000;
               auto render = [&](double startBeat, int numBlocks)
               {
                  ArrangeFrameUpdate();
                  RebuildAudioTopology();
                  gArrangeAudioSig = ArrangeAudioSignature();
                  cookFrame++;
                  for (GraphNode& gn : gNodes)
                     gn.node->CookIfNeeded(cookFrame);
                  for (GraphNode& gn : gNodes)
                     if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
                        if (an->preparedForSampleRate != kSr)
                        {
                           an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                           an->preparedForSampleRate = kSr;
                        }
                  tr.SeekBeats(startBeat);
                  std::vector<float> ch0((size_t)kBlock), ch1((size_t)kBlock);
                  float* chans[2] = { ch0.data(), ch1.data() };
                  AudioBuffer buffer;
                  buffer.channels = chans;
                  buffer.numChannels = 2;
                  buffer.numFrames = kBlock;
                  float peak = 0.0f;
                  for (int b = 0; b < numBlocks; b++)
                  {
                     ArrangeAudioRebuildIfStale();
                     AudioEngine::Instance().RenderOfflineBlock(buffer);
                     for (int i = 0; i < kBlock; i++)
                        if ((double)(b * kBlock + i) > 0.25 * kSamplesPerBeat) // past the onset ramp
                           peak = std::max(peak, std::fabs(ch0[(size_t)i]));
                  }
                  return peak;
               };
               auto soloOnly = [&](int which)
               {
                  for (int i = 0; i < 3; i++)
                     Arrange::FindLane(gArrange, lanes[(size_t)i])->solo = (i == which);
                  gArrange.revision++;
               };
               const int blocks = (int)(2.0 * kSamplesPerBeat / kBlock); // two beats
               float single[3] = { 0.0f, 0.0f, 0.0f };
               for (int i = 0; i < 3; i++)
               {
                  soloOnly(i);
                  single[i] = render(0.0, blocks);
               }
               soloOnly(-1);
               const float all = render(0.0, blocks);
               sSinglePeak = std::min(single[0], std::min(single[1], single[2]));
               // Three identical oscillators in phase sum to ~3x one of them.
               const bool liveOk = sSinglePeak > 0.05f && all > 2.5f * sSinglePeak;
               printf("arrange multitrack live: %s (track peaks %.4f %.4f %.4f, all three %.4f)\n",
                      liveOk ? "OK" : "FAIL", single[0], single[1], single[2], all);
               sOk = sOk && liveOk;
               // Turbo 0.72: a stem render's scope silences the other tracks
               // (the same flag the render queue sets, without a device).
               gArrangeRenderActiveScope = { lanes[0] };
               const float stem = render(0.0, blocks);
               gArrangeRenderActiveScope.clear();
               const bool stemOk = std::fabs(stem - sSinglePeak) < 0.15f * sSinglePeak;
               printf("arrange multitrack stem scope: %s (stem %.4f, one track %.4f)\n", stemOk ? "OK" : "FAIL", stem,
                      sSinglePeak);
               sOk = sOk && stemOk;
               tr.NotifyAudioEngineStopped();
            }

            if (hadEngine)
            {
               std::string startErr;
               if (AudioEngine::Instance().Start(startErr))
                  tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
            }
            (void)engineSr;
            RebuildAudioTopology();
            if (spawned && AudioEngine::Instance().SampleRate() > 0.0)
            {
               sWavPath = (std::filesystem::temp_directory_path() / "infinite_arrange_multitrack.wav").string();
               std::remove(sWavPath.c_str());
               ArrangeRenderJob job;
               job.format = 2;
               job.start = 0;
               job.end = Arrange::kTicksPerBar; // one bar = 2 s at 120 bpm
               job.fps = 30;
               job.path = sWavPath;
               gArrangeRenderJobs.push_back(job);
               // Turbo 0.72: a stem of the first track only (Render track...).
               ArrangeRenderJob stemJob = job;
               stemJob.path = sWavPath.substr(0, sWavPath.size() - 4) + "_stem.wav";
               std::remove(stemJob.path.c_str());
               stemJob.laneScope = { gArrange.lanes[0].id };
               gArrangeRenderJobs.push_back(stemJob);
               sWavQueued = true;
            }
            else
               printf("arrange multitrack wav render: skipped (no audio device)\n");
         }
         else
         {
            if (sWavQueued)
            {
               // Float WAV written by ArrangeWavHeader: data starts at byte 58.
               auto readPeak = [](const std::string& path, size_t& frames)
               {
                  std::ifstream f(path, std::ios::binary);
                  std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                  float peak = 0.0f;
                  frames = 0;
                  if (bytes.size() > 58)
                  {
                     frames = (bytes.size() - 58) / 8;
                     const float* s = reinterpret_cast<const float*>(bytes.data() + 58);
                     for (size_t i = frames / 8; i < frames; i++) // past the onset
                        peak = std::max(peak, std::fabs(s[i * 2]));
                  }
                  return peak;
               };
               const std::string stemPath = sWavPath.substr(0, sWavPath.size() - 4) + "_stem.wav";
               size_t frames = 0, stemFrames = 0;
               const float peak = readPeak(sWavPath, frames);
               const float stemPeak = readPeak(stemPath, stemFrames);
               const size_t n = gArrangeRenderJobs.size();
               const bool done = n >= 2 && gArrangeRenderJobs[n - 2].status == 2 && gArrangeRenderJobs[n - 1].status == 2;
               const bool wavOk = done && frames > 0 && sSinglePeak > 0.0f && peak > 2.5f * sSinglePeak;
               printf("arrange multitrack wav render: %s (jobs done %d, %zu frames, peak %.4f, one track %.4f)\n",
                      wavOk ? "OK" : "FAIL", (int)done, frames, peak, sSinglePeak);
               // The stem holds one track: about one track's level, not three.
               const bool stemOk = done && stemFrames > 0 && std::fabs(stemPeak - sSinglePeak) < 0.15f * sSinglePeak;
               printf("arrange multitrack stem render: %s (stem peak %.4f, one track %.4f)\n", stemOk ? "OK" : "FAIL",
                      stemPeak, sSinglePeak);
               sOk = sOk && wavOk && stemOk;
               std::remove(sWavPath.c_str());
               std::remove(stemPath.c_str());
            }
            printf("arrange multitrack test: all  %s\n", sOk ? "OK" : "FAIL");
         }
      }

      // Turbo 0.72: INFINITE_ARRANGERECORDTEST - record to timeline. An
      // Oscillator is armed on a track and recorded for two beats through the
      // real path (ArrangeRecordStart -> engine tap -> WAV -> ArrangeRecordCommit);
      // then the Oscillator is DELETED and the track is played back: what is
      // heard can only come from the take's file. Offline, no audio device.
      if (getenv("INFINITE_ARRANGERECORDTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kBpm = 120.0;
         const double kSamplesPerBeat = kSr * 60.0 / kBpm;
         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();

         GraphNode* osc = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f);
         const uint64_t oscUid = osc != nullptr ? osc->uid : 0;
         const uint64_t rev = gArrange.revision;
         gArrange = Arrange::Model();
         gArrange.revision = rev + 1;
         const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
         const bool armed = oscUid != 0 && ArrangeSetRecordArm(laneId, oscUid);
         printf("arrange record arm: %s\n", armed ? "OK" : "FAIL");
         allOk = allOk && armed;

         int cookFrame = 4000000;
         auto prepare = [&]()
         {
            ArrangeFrameUpdate();
            RebuildAudioTopology();
            gArrangeAudioSig = ArrangeAudioSignature();
            cookFrame++;
            for (GraphNode& gn : gNodes)
               gn.node->CookIfNeeded(cookFrame);
            for (GraphNode& gn : gNodes)
               if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
                  if (an->preparedForSampleRate != kSr)
                  {
                     an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                     an->preparedForSampleRate = kSr;
                  }
         };
         auto run = [&](int numBlocks, float* peakOut)
         {
            std::vector<float> ch0((size_t)kBlock), ch1((size_t)kBlock);
            float* chans[2] = { ch0.data(), ch1.data() };
            AudioBuffer buffer;
            buffer.channels = chans;
            buffer.numChannels = 2;
            buffer.numFrames = kBlock;
            float peak = 0.0f;
            for (int b = 0; b < numBlocks; b++)
            {
               ArrangeAudioRebuildIfStale();
               AudioEngine::Instance().RenderOfflineBlock(buffer);
               for (int i = 0; i < kBlock; i++)
                  if ((double)(b * kBlock + i) > 0.25 * kSamplesPerBeat && (double)(b * kBlock + i) < 1.75 * kSamplesPerBeat)
                     peak = std::max(peak, std::fabs(ch0[(size_t)i]));
               if (b % 16 == 15)
                  ArrangeRecordDrain();
            }
            if (peakOut != nullptr)
               *peakOut = peak;
         };

         if (armed)
         {
            gArrangeTimelineMode = true;
            tr.SetTempo((float)kBpm);
            tr.SetLoop(false, 0.0, 0.0);
            tr.NotifyAudioEngineStarted(kSr);
            tr.SetPlaying(false);
            tr.SeekBeats(0.0);
            prepare();
            const int blocks = (int)(2.0 * kSamplesPerBeat / kBlock); // two beats
            const bool started = ArrangeRecordStart(kSr);
            float monitored = 0.0f;
            run(blocks, &monitored); // recording: the armed source is monitored
            ArrangeRecordStop();
            ArrangeRecordCommit();
            const Arrange::Lane* lane = Arrange::FindLane(gArrange, laneId);
            const Arrange::Clip* take = (lane != nullptr && lane->clips.size() == 1) ? &lane->clips[0] : nullptr;
            GraphNode* fileGn = take != nullptr ? FindNodeByUid(take->srcUid) : nullptr;
            const bool isFile = fileGn != nullptr && dynamic_cast<AudioFileNode*>(fileGn->node.get()) != nullptr;
            const double takeBeats = take != nullptr ? Arrange::TicksToBeats(take->length) : 0.0;
            const bool takeOk = started && !gArrangeRecording && take != nullptr && isFile && take->start == 0 &&
                                std::fabs(takeBeats - 2.0) < 0.05 && take->sampleDropped && !take->syncToTempo;
            printf("arrange record take: %s (started %d, clips %zu, file player %d, start %lld, length %.3f beats, monitored %.4f)\n",
                   takeOk ? "OK" : "FAIL", (int)started, lane != nullptr ? lane->clips.size() : (size_t)0, (int)isFile,
                   take != nullptr ? (long long)take->start : -1LL, takeBeats, monitored);
            allOk = allOk && takeOk;

            // The source goes away; only the file can still sound.
            ArrangeClearRecordArm(laneId);
            if (GraphNode* o = FindNodeByUid(oscUid))
            {
               PushUndoCheckpoint();
               RemoveNodeByIndex(o->index);
            }
            tr.SetPlaying(true);
            prepare();
            tr.SeekBeats(0.0);
            float played = 0.0f;
            run(blocks, &played);
            const bool playOk = takeOk && FindNodeByUid(oscUid) == nullptr && played > 0.05f &&
                                std::fabs(played - monitored) < 0.15f * std::max(monitored, 1e-6f);
            printf("arrange record playback without the source: %s (take peak %.4f, recorded live %.4f)\n",
                   playOk ? "OK" : "FAIL", played, monitored);
            allOk = allOk && playOk;

            // Turbo 0.72 (U3): loop recording - a one-bar loop recorded for
            // two and a half passes gives three takes: the last on the armed
            // track, the two before on muted take tracks under it.
            GraphNode* osc2 = SpawnNode("Oscillator", "Synths", 400.0f, 0.0f);
            const uint64_t osc2Uid = osc2 != nullptr ? osc2->uid : 0;
            const size_t lanesBefore = gArrange.lanes.size();
            const bool armed2 = osc2Uid != 0 && ArrangeSetRecordArm(laneId, osc2Uid);
            gArrange.settings.loop = { true, 0, Arrange::kTicksPerBar };
            PublishArrangeLoop();
            tr.SetPlaying(false);
            tr.SeekBeats(0.0);
            prepare();
            const bool started2 = armed2 && ArrangeRecordStart(kSr);
            run((int)(10.0 * kSamplesPerBeat / kBlock), nullptr); // 2.5 bars
            ArrangeRecordStop();
            ArrangeRecordCommit();
            int takeLanes = 0;
            for (const Arrange::Lane& l : gArrange.lanes)
               if (l.name.find(" take ") != std::string::npos && l.mute && l.clips.size() == 1)
                  takeLanes++;
            const Arrange::Lane* armedLane = Arrange::FindLane(gArrange, laneId);
            const Arrange::Clip* lastPass = nullptr;
            if (armedLane != nullptr)
               for (const Arrange::Clip& c : armedLane->clips)
                  if (c.name.find(".3") != std::string::npos)
                     lastPass = &c;
            const bool loopRecOk = started2 && takeLanes == 2 && gArrange.lanes.size() == lanesBefore + 2 &&
                                   lastPass != nullptr && lastPass->start == 0 &&
                                   std::fabs(Arrange::TicksToBeats(lastPass->length) - 2.0) < 0.1 && lastPass->sourceOffsetSeconds > 3.9f;
            printf("arrange record loop takes: %s (take tracks %d, last pass start %lld, %.3f beats, file offset %.3f s)\n",
                   loopRecOk ? "OK" : "FAIL", takeLanes, lastPass != nullptr ? (long long)lastPass->start : -1LL,
                   lastPass != nullptr ? Arrange::TicksToBeats(lastPass->length) : 0.0,
                   lastPass != nullptr ? lastPass->sourceOffsetSeconds : 0.0f);
            allOk = allOk && loopRecOk;
            gArrange.settings.loop.enabled = false;
            PublishArrangeLoop();
            tr.NotifyAudioEngineStopped();
         }

         tr.SetPlaying(true);
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         RebuildAudioTopology();
         printf("arrange record test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Turbo 0.72: INFINITE_ARRANGESESSIONTEST - the Session view engine
      // (upstream's, ported). A timeline clip on track 1 and a session slot
      // on track 2, two Oscillators: launching the slot plays it and, in
      // Session audio, strips the timeline clip; stopping the track silences
      // it. Offline, no device; launch grid off so moves land at once.
      if (getenv("INFINITE_ARRANGESESSIONTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kSamplesPerBeat = kSr * 60.0 / 120.0;
         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         GraphNode* a = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f);
         const uint64_t uidA = a != nullptr ? a->uid : 0;
         GraphNode* b = SpawnNode("Oscillator", "Synths", 220.0f, 0.0f);
         const uint64_t uidB = b != nullptr ? b->uid : 0;
         gArrange = Arrange::Model();
         gArrange.revision = 5000;
         const uint64_t laneA = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
         const uint64_t laneB = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
         Arrange::Clip c;
         c.length = Arrange::kTicksPerBar * 8;
         c.srcUid = uidA;
         Arrange::PlaceOverwrite(gArrange, laneA, c);
         ArrSess::SetSlotSource(laneB, 0, uidB);
         gArrange.sessionQuantize = 0; // launch at once
         tr.SetTempo(120.0f);
         tr.SetLoop(false, 0.0, 0.0);
         tr.SetLaunchQuantize(0);
         tr.NotifyAudioEngineStarted(kSr);
         tr.SetPlaying(false);
         gArrangeTimelineMode = true;
         gArrangeSessionMode = true; // Session audio already on: no device start from the launch

         int cookFrame = 5000000;
         auto render = [&](int numBlocks)
         {
            ArrangeFrameUpdate();
            RebuildAudioTopology();
            gArrangeAudioSig = ArrangeAudioSignature();
            cookFrame++;
            for (GraphNode& gn : gNodes)
               gn.node->CookIfNeeded(cookFrame);
            for (GraphNode& gn : gNodes)
               if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
                  if (an->preparedForSampleRate != kSr)
                  {
                     an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                     an->preparedForSampleRate = kSr;
                  }
            std::vector<float> ch0((size_t)kBlock), ch1((size_t)kBlock);
            float* chans[2] = { ch0.data(), ch1.data() };
            AudioBuffer buffer;
            buffer.channels = chans;
            buffer.numChannels = 2;
            buffer.numFrames = kBlock;
            float peak = 0.0f;
            for (int blk = 0; blk < numBlocks; blk++)
            {
               ArrangeSessionTick();
               ArrangeAudioRebuildIfStale();
               AudioEngine::Instance().RenderOfflineBlock(buffer);
               for (int i = 0; i < kBlock; i++)
                  if ((double)(blk * kBlock + i) > 0.25 * kSamplesPerBeat)
                     peak = std::max(peak, std::fabs(ch0[(size_t)i]));
            }
            return peak;
         };
         const int blocks = (int)(2.0 * kSamplesPerBeat / kBlock);

         const bool ok0 = uidA != 0 && uidB != 0 && !gArrange.lanes[1].slots.empty() && gArrange.lanes[1].slots[0].Has();
         ArrangeSessionLaunch(laneB, 0);
         const bool started = tr.IsPlaying();
         const float launched = render(blocks);
         const ArrangeSessionSlotState st = ArrangeSessionQuery(laneB, 0);
         // One oscillator, not two: the timeline clip on track 1 is out of Session audio.
         const bool playOk = ok0 && started && st.playing && launched > 0.3f && launched < 0.8f;
         printf("arrange session launch: %s (playing %d, peak %.4f - one oscillator, timeline clip stripped)\n",
                playOk ? "OK" : "FAIL", (int)st.playing, launched);
         allOk = allOk && playOk;

         ArrangeSessionStopLane(laneB);
         const float stopped = render(blocks);
         const bool stopOk = stopped < 1e-4f && !ArrangeSessionQuery(laneB, 0).playing;
         printf("arrange session stop: %s (peak %.6f)\n", stopOk ? "OK" : "FAIL", stopped);
         allOk = allOk && stopOk;

         // Back to the timeline: its clip plays again.
         gArrangeSessionMode = false;
         tr.SeekBeats(0.0);
         const float timeline = render(blocks);
         const bool backOk = timeline > 0.3f;
         printf("arrange session back to timeline: %s (peak %.4f)\n", backOk ? "OK" : "FAIL", timeline);
         allOk = allOk && backOk;

         tr.NotifyAudioEngineStopped();
         tr.SetPlaying(true);
         gArrangeTimelineMode = false;
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         RebuildAudioTopology();
         printf("arrange session test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Turbo 0.72 (U2/U3): INFINITE_ARRANGEEDITPLUSTEST - the editing and
      // automation layer: an automation lane driving a real parameter
      // (linear and step curves, write mode recording), the patch round trip
      // of automation / fade curves / content loop, ripple delete, range
      // duplicate and drag, Session clip drag and send to timeline, input
      // latency. Headless.
      if (getenv("INFINITE_ARRANGEEDITPLUSTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         tr.SetPlaying(false);
         tr.SetTempo(120.0f);

         // --- automation drives a parameter -------------------------------
         GraphNode* gn = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f);
         std::string param;
         if (gn != nullptr)
         {
            ArrangeAutoParamLister pl;
            gn->node->VisitParams(pl);
            for (const auto& p : pl.params)
            {
               ArrangeAutoParamFinder pf(p.first);
               gn->node->VisitParams(pf);
               if (pf.f != nullptr)
               {
                  param = p.first;
                  break;
               }
            }
         }
         bool autoOk = gn != nullptr && !param.empty();
         if (autoOk)
         {
            tr.SeekBeats(0.0);
            ArrangeAutoAdd(gn->uid, param, 0.0f);
            ArrangeAuto::Lane& al = gArrangeAuto.back();
            al.lo = 0.0f;
            al.hi = 1.0f;
            al.keys = { { 0, 0.2f, ArrangeAuto::kLinear }, { Arrange::kPPQ * 4, 0.6f, ArrangeAuto::kStep }, { Arrange::kPPQ * 8, 0.9f, ArrangeAuto::kLinear } };
            float got = -1.0f;
            tr.SeekBeats(2.0);
            ArrangeAutoApply();
            const bool mid = ArrangeAutoRead(al, got) && std::fabs(got - 0.4f) < 1e-3f;
            tr.SeekBeats(6.0);
            ArrangeAutoApply();
            float held = -1.0f;
            const bool step = ArrangeAutoRead(al, held) && std::fabs(held - 0.6f) < 1e-3f;
            // Write mode: while playing, a moved knob becomes keys.
            const size_t keysBefore = al.keys.size();
            gArrangeAutoWrite = { al.id };
            tr.SetPlaying(true);
            tr.SeekBeats(10.0);
            ArrangeAutoWriteValue(al, 0.33f);
            ArrangeAutoApply();
            tr.SeekBeats(11.0);
            ArrangeAutoWriteValue(al, 0.77f);
            ArrangeAutoApply();
            tr.SetPlaying(false);
            gArrangeAutoWrite.clear();
            float w10 = 0.0f, w11 = 0.0f;
            const bool wrote = al.keys.size() == keysBefore + 2 && ArrangeAuto::Eval(al, Arrange::kPPQ * 10, w10) &&
                               ArrangeAuto::Eval(al, Arrange::kPPQ * 11, w11) && std::fabs(w10 - 0.33f) < 1e-3f &&
                               std::fabs(w11 - 0.77f) < 1e-3f;
            autoOk = mid && step && wrote;
            printf("arrange automation drive: %s (linear mid %.3f, step hold %.3f, write keys +%d)\n", autoOk ? "OK" : "FAIL",
                   got, held, (int)(al.keys.size() - keysBefore));
         }
         else
            printf("arrange automation drive: FAIL (no float parameter on the Oscillator)\n");
         allOk = allOk && autoOk;

         // --- patch round trip: automation, fade curves, content loop ------
         {
            gArrange = Arrange::Model();
            gArrange.revision = 9000;
            const uint64_t lane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            c.fadeIn = Arrange::kPPQ;
            c.fadeInShape = Arrange::kFadeS;
            c.fadeOutShape = Arrange::kFadeFast;
            c.loopTicks = Arrange::kPPQ * 2;
            uint64_t cid = 0;
            Arrange::PlaceOverwrite(gArrange, lane, c, &cid);
            std::string text, err;
            Patch::Data back;
            const bool io = Patch::WriteText(BuildPatchData(), text, err) && Patch::ReadText(text, back, err);
            bool rtOk = io && back.automation.size() == gArrangeAuto.size() && !back.automation.empty() &&
                        back.automation[0].keys.size() == gArrangeAuto[0].keys.size() && back.automation[0].param == param;
            Arrange::Model r;
            if (io)
               PatchDataToArrangeModel(back, r);
            const Arrange::Clip* rc = io ? Arrange::FindClip(r, cid) : nullptr;
            rtOk = rtOk && rc != nullptr && rc->fadeInShape == Arrange::kFadeS && rc->fadeOutShape == Arrange::kFadeFast &&
                   rc->loopTicks == Arrange::kPPQ * 2;
            printf("arrange automation / fade curve / loop round trip: %s\n", rtOk ? "OK" : "FAIL");
            allOk = allOk && rtOk;
         }

         // --- ripple delete -----------------------------------------------
         {
            gArrange = Arrange::Model();
            gArrange.revision = 9100;
            const uint64_t lane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            uint64_t ids[3] = {};
            for (int k = 0; k < 3; k++)
            {
               Arrange::Clip c;
               c.start = Arrange::kTicksPerBar * k;
               c.length = Arrange::kTicksPerBar;
               Arrange::PlaceOverwrite(gArrange, lane, c, &ids[k]);
            }
            gArrangeRipple = 1;
            gArrangeSel = { ids[1] };
            ArrangeDeleteSelection();
            gArrangeRipple = 0;
            const Arrange::Clip* third = Arrange::FindClip(gArrange, ids[2]);
            const bool ripOk = Arrange::FindClip(gArrange, ids[1]) == nullptr && third != nullptr &&
                               third->start == Arrange::kTicksPerBar;
            printf("arrange ripple delete: %s (third clip now at %lld)\n", ripOk ? "OK" : "FAIL",
                   third != nullptr ? (long long)third->start : -1LL);
            allOk = allOk && ripOk;

            // --- range duplicate: the span lands right after itself -----
            gArrangeRange.active = true;
            gArrangeRange.start = 0;
            gArrangeRange.end = Arrange::kTicksPerBar / 2;
            gArrangeRange.laneLo = 0;
            gArrangeRange.laneHi = 0;
            const size_t before = gArrange.lanes[0].clips.size();
            ArrangeDuplicateRange();
            gArrangeRange = ArrangeRangeSel();
            bool dupOk = gArrange.lanes[0].clips.size() == before + 1; // the copy replaces the first clip's second half
            bool found = false;
            for (const Arrange::Clip& c : gArrange.lanes[0].clips)
               found = found || (c.start == Arrange::kTicksPerBar / 2 && c.length == Arrange::kTicksPerBar / 2);
            dupOk = dupOk && found;
            printf("arrange range duplicate: %s (%zu clips)\n", dupOk ? "OK" : "FAIL", gArrange.lanes[0].clips.size());
            allOk = allOk && dupOk;
         }

         // --- range drag: move to another track and time, then copy back ---
         {
            gArrange = Arrange::Model();
            gArrange.revision = 9200;
            const uint64_t a = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            const uint64_t b = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            Arrange::AddLane(gArrange, Arrange::kLaneVideo);
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            Arrange::PlaceOverwrite(gArrange, a, c);
            ArrangeRangeSel r;
            r.active = true;
            r.start = 0;
            r.end = Arrange::kTicksPerBar;
            r.laneLo = r.laneHi = 0;
            ArrangeMoveRangeBy(r, Arrange::kTicksPerBar * 2, 1, false);
            const Arrange::Lane* la = Arrange::FindLane(gArrange, a);
            const Arrange::Lane* lb = Arrange::FindLane(gArrange, b);
            bool moveOk = la->clips.empty() && lb->clips.size() == 1 && lb->clips[0].start == Arrange::kTicksPerBar * 2;
            r.start = Arrange::kTicksPerBar * 2;
            r.end = Arrange::kTicksPerBar * 3;
            r.laneLo = r.laneHi = 1;
            ArrangeMoveRangeBy(r, -Arrange::kTicksPerBar * 2, -1, true);     // copy: the original stays
            const bool refused = !ArrangeMoveRangeBy(r, 0, 1, false); // onto a video track: refused
            la = Arrange::FindLane(gArrange, a);
            lb = Arrange::FindLane(gArrange, b);
            moveOk = moveOk && refused && la->clips.size() == 1 && la->clips[0].start == 0 && lb->clips.size() == 1 &&
                     gArrange.lanes[2].clips.empty();
            printf("arrange range drag move / copy: %s\n", moveOk ? "OK" : "FAIL");
            allOk = allOk && moveOk;
         }

         // --- Session: drag a clip (swap / copy), send a scene to the timeline
         {
            gArrange = Arrange::Model();
            gArrange.revision = 9300;
            const uint64_t a = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            const uint64_t b = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            const uint64_t v = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
            Arrange::Lane* la = Arrange::FindLane(gArrange, a);
            la->slots.resize(1);
            la->slots[0].clip.id = gArrange.NewId();
            la->slots[0].clip.length = Arrange::kTicksPerBar * 4;
            la->slots[0].loopTicks = Arrange::kTicksPerBar * 2;
            gArrange.scenes.resize(1);
            ArrSess::MoveSlot(a, 0, a, 2, false);
            bool sessOk = ArrSess::FindSlot(a, 0) == nullptr && ArrSess::FindSlot(a, 2) != nullptr;
            ArrSess::MoveSlot(a, 2, b, 0, true);
            ArrSess::MoveSlot(a, 2, v, 0, false); // an audio clip on a video track: refused
            sessOk = sessOk && ArrSess::FindSlot(a, 2) != nullptr && ArrSess::FindSlot(b, 0) != nullptr &&
                     ArrSess::FindSlot(v, 0) == nullptr &&
                     ArrSess::FindSlot(b, 0)->clip.id != ArrSess::FindSlot(a, 2)->clip.id;
            tr.SeekBeats(0.0);
            ArrSess::SendSceneToTimeline(0);
            const Arrange::Lane* lb = Arrange::FindLane(gArrange, b);
            sessOk = sessOk && lb->clips.size() == 1 && lb->clips[0].start == 0 &&
                     lb->clips[0].length == Arrange::kTicksPerBar * 2 && Arrange::FindLane(gArrange, a)->clips.empty();
            printf("arrange session drag / send to timeline: %s (playhead now beat %.2f)\n", sessOk ? "OK" : "FAIL",
                   tr.Beats());
            allOk = allOk && sessOk;
            gArrangeSessSelLane = 0;
            gArrangeSessSelRow = -1;
         }

         // --- input latency: only a source fed by an Audio In is moved ------
         {
            GraphNode* osc = SpawnNode("Oscillator", "Synths", 0.0f, 200.0f);
            const bool latOk = osc != nullptr && ArrangeRecLatencySeconds(osc->uid, 48000.0) == 0.0;
            printf("arrange input latency (synth source untouched): %s\n", latOk ? "OK" : "FAIL");
            allOk = allOk && latOk;
         }

         gArrangeAuto.clear();
         tr.SetPlaying(true);
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         printf("arrange edit plus test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Turbo 0.72: INFINITE_ARRANGEDEMO - a small arrangement with the panel
      // open, a Range selection and the Range tool on, for screenshots of the
      // timeline UI (IMAGERESYNTH_SCREENSHOT + INFINITE_SCREENSHOT_FRAME).
      if (getenv("INFINITE_ARRANGEDEMO") != nullptr && frameId == 3)
      {
         NewPatch();
         std::vector<uint64_t> uids;
         const char* kinds[] = { "Oscillator", "Oscillator", "Oscillator" };
         for (int i = 0; i < 3; i++)
            if (GraphNode* gn = SpawnNode(kinds[i], "Synths", 220.0f * (float)i, 0.0f))
               uids.push_back(gn->uid);
         gArrange = Arrange::Model();
         gArrange.revision = 1000;
         Arrange::AddLane(gArrange, Arrange::kLaneVideo);
         for (size_t i = 0; i < uids.size(); i++)
         {
            const uint64_t lane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            for (int k = 0; k < 3; k++)
            {
               Arrange::Clip c;
               c.start = Arrange::kTicksPerBar * (Arrange::Tick)(k * 3 + (int)i);
               c.length = Arrange::kTicksPerBar * 2;
               c.srcUid = uids[i];
               c.fadeIn = k == 1 ? Arrange::kPPQ : 0;
               Arrange::PlaceOverwrite(gArrange, lane, c);
            }
         }
         Arrange::AddMarker(gArrange, Arrange::kTicksPerBar * 4, "verse");
         gArrange.settings.loop = { true, Arrange::kTicksPerBar * 2, Arrange::kTicksPerBar * 6 };
         gArrangeRange.active = true;
         gArrangeRange.start = Arrange::kTicksPerBar * 7;
         gArrangeRange.end = Arrange::kTicksPerBar * 9;
         gArrangeRange.laneLo = 1;
         gArrangeRange.laneHi = 2;
         ArrangeSetTool(ArrangeTool::Range);
         gArrangeRowSel = { gArrange.lanes[1].id }; // the inspector shows a track
         if (std::string(getenv("INFINITE_ARRANGEDEMO")) == "session")
         {
            // The Session view: a clip per track in the first rows, scene 1 launched.
            for (size_t i = 0; i < uids.size(); i++)
               for (int row = 0; row <= (int)i; row++)
                  ArrSess::SetSlotSource(gArrange.lanes[i + 1].id, row, uids[i]);
            gArrange.scenes.resize(3);
            gArrange.scenes[0].name = "intro";
            gArrangeSessionView = true;
            gArrangeSessSelLane = gArrange.lanes[2].id; // the inspector shows a Session clip
            gArrangeSessSelRow = 1;
            ArrangeSessionLaunchScene(0);
         }
         gArrangePanelOpen = true;
         gArrangePanelHeight = 340.0f;
         PublishArrangeLoop();
         printf("arrange demo: OK\n");
      }


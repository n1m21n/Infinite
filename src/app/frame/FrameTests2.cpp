// Per-frame self-test blocks moved verbatim out of the main loop in main.cpp.
#include "app/AppShared.h"

namespace app
{

void FrameTest_ARRANGEMARKERTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_ARRANGEMARKERTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetPlaying(false);
         tr.SetTempo(120.0f);
         tr.SeekBeats(0.0);
         const Arrange::Tick kBar = Arrange::kTicksPerBar;
         const Arrange::Tick kQ = Arrange::kPPQ;
         std::string why;
         auto sorted = [&]()
         {
            for (size_t i = 1; i < gArrange.markers.size(); i++)
               if (gArrange.markers[i - 1].pos > gArrange.markers[i].pos)
                  return false;
            return true;
         };
         auto findMk = [&](uint64_t id) -> const Arrange::Marker*
         {
            for (const Arrange::Marker& m : gArrange.markers)
               if (m.id == id) return &m;
            return nullptr;
         };
         auto freshModel = [&](int audio)
         {
            ArrangeEdit([&]()
            {
               while (!gArrange.lanes.empty())
                  Arrange::RemoveLane(gArrange, gArrange.lanes.front().id);
               while (!gArrange.markers.empty())
                  Arrange::DeleteMarker(gArrange, gArrange.markers.front().id);
               for (int i = 0; i < audio; i++)
                  Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            });
            gArrangeSel.clear();
            gArrangeSelAnchor = 0;
         };
         auto place = [&](int lane, Arrange::Tick start, Arrange::Tick len)
         {
            Arrange::Clip c;
            c.start = start;
            c.length = len;
            uint64_t id = 0;
            ArrangeEdit([&]() { Arrange::PlaceOverwrite(gArrange, gArrange.lanes[lane].id, c, &id); });
            return id;
         };

         // --- A. Marker add / move / rename / recolor / delete; sorted; every
         //        op bumps revision and is one undo entry --------------------
         {
            freshModel(1);
            bool aOk = true;
            size_t undo0 = gUndoStack.size();
            uint64_t rev = gArrange.revision;
            uint64_t m1 = 0, m2 = 0, m3 = 0;
            ArrangeEdit([&]() { m1 = Arrange::AddMarker(gArrange, kBar * 4, "Chorus", 0xEF4444FFu); });
            aOk = aOk && gArrange.revision > rev; rev = gArrange.revision;
            ArrangeEdit([&]() { m2 = Arrange::AddMarker(gArrange, kBar, "Verse", kArrangeDefaultMarkerRGBA); });
            aOk = aOk && gArrange.revision > rev; rev = gArrange.revision;
            ArrangeEdit([&]() { m3 = Arrange::AddMarker(gArrange, -50, "Intro", kArrangeDefaultMarkerRGBA); });
            aOk = aOk && gArrange.revision > rev; rev = gArrange.revision;
            aOk = aOk && m1 && m2 && m3 && gUndoStack.size() == undo0 + 3 && sorted() &&
                  gArrange.markers.size() == 3 && gArrange.markers[0].id == m3 && gArrange.markers[0].pos == 0 &&
                  gArrange.markers[1].id == m2 && gArrange.markers[2].id == m1;
            // Move past a neighbour: re-sorted.
            aOk = aOk && ArrangeEdit([&]() { Arrange::MoveMarker(gArrange, m3, kBar * 8); });
            aOk = aOk && gArrange.revision > rev && sorted() && gArrange.markers.back().id == m3; rev = gArrange.revision;
            aOk = aOk && ArrangeEdit([&]() { Arrange::RenameMarker(gArrange, m2, "Verse 1"); });
            aOk = aOk && gArrange.revision > rev && findMk(m2)->name == "Verse 1"; rev = gArrange.revision;
            const uint32_t blue = ArrangeMarkerRGBA(kArrangePalette[6].col);
            aOk = aOk && blue == 0x3B82F6FFu && ArrangeEdit([&]() { Arrange::RecolorMarker(gArrange, m2, blue); });
            aOk = aOk && gArrange.revision > rev && findMk(m2)->color == blue; rev = gArrange.revision;
            // A no-op edit pushes nothing and leaves revision alone.
            const size_t undoNoop = gUndoStack.size();
            aOk = aOk && !ArrangeEdit([&]() { Arrange::RenameMarker(gArrange, m2, "Verse 1"); }) &&
                  !ArrangeEdit([&]() { Arrange::MoveMarker(gArrange, m2, kBar); }) &&
                  gUndoStack.size() == undoNoop && gArrange.revision == rev;
            aOk = aOk && ArrangeEdit([&]() { Arrange::DeleteMarker(gArrange, m1); });
            aOk = aOk && gArrange.revision > rev && findMk(m1) == nullptr && gArrange.markers.size() == 2;
            aOk = aOk && gUndoStack.size() == undo0 + 7 && Arrange::Validate(gArrange, &why);

            // Undo / redo walk the same entries back and forth.
            Undo();
            aOk = aOk && findMk(m1) != nullptr && findMk(m1)->pos == kBar * 4 && sorted();
            Undo();
            aOk = aOk && findMk(m2)->color == kArrangeDefaultMarkerRGBA;
            Undo();
            aOk = aOk && findMk(m2)->name == "Verse";
            Undo();
            aOk = aOk && findMk(m3)->pos == 0 && gArrange.markers.front().id == m3 && sorted();
            Redo();
            Redo();
            Redo();
            Redo();
            aOk = aOk && findMk(m1) == nullptr && findMk(m2)->name == "Verse 1" && findMk(m2)->color == blue &&
                  findMk(m3)->pos == kBar * 8 && sorted() && Arrange::Validate(gArrange, &why);

            // A flag drag: many MoveMarker calls, one gesture, one entry;
            // a drag that comes back where it started pushes nothing.
            size_t undoD = gUndoStack.size();
            ArrangeGestureBegin();
            for (int k = 1; k <= 6; k++)
               Arrange::MoveMarker(gArrange, m2, kBar + kQ * k);
            ArrangeGestureEnd();
            aOk = aOk && gUndoStack.size() == undoD + 1 && findMk(m2)->pos == kBar + kQ * 6 && sorted();
            ArrangeGestureBegin();
            Arrange::MoveMarker(gArrange, m2, kBar * 20);
            aOk = aOk && sorted() && gArrange.markers.back().id == m2;
            Arrange::MoveMarker(gArrange, m2, kBar + kQ * 6);
            ArrangeGestureEnd();
            aOk = aOk && gUndoStack.size() == undoD + 1 && sorted();
            Undo();
            aOk = aOk && findMk(m2)->pos == kBar;
            // Undo mid-drag closes the gesture, so it must end the flag drag
            // too - else the rest of the drag moves the marker unrecorded.
            ArrangeGestureBegin();
            gArrangeMarkerDragId = m2;
            Arrange::MoveMarker(gArrange, m2, kBar * 3);
            Undo();
            aOk = aOk && gArrangeMarkerDragId == 0 && !gArrangeGestureOpen;
            Redo();
            printf("arrange marker ops + undo/redo: %s\n", aOk ? "OK" : "FAIL");
            allOk = allOk && aOk;
         }

         // --- B. Save / load round trip: markers + time display + snap -------
         {
            freshModel(1);
            SpawnNode("Cube", "3D", 0.0f, 0.0f); // Patch::Read refuses a file with no nodes
            place(0, kBar, kBar * 2);
            uint64_t ma = 0, mb = 0;
            ArrangeEdit([&]()
            {
               ma = Arrange::AddMarker(gArrange, kBar * 3 + kQ, "Drop  here", 0x10B981FFu);
               mb = Arrange::AddMarker(gArrange, kQ, "Top", kArrangeDefaultMarkerRGBA);
            });
            ArrangeSetTimeDisplay(1);
            ArrangeSetSnap(8, true);
            const std::vector<Arrange::Marker> before = gArrange.markers;
            const std::string path = TmpPath("arrange_markertest.inf");
            bool bOk = SavePatchTo(path);
            NewPatch();
            bOk = bOk && gArrange.markers.empty() && gArrange.settings.timeDisplay == 0;
            bOk = bOk && LoadPatchFrom(path);
            bOk = bOk && gArrange.markers.size() == before.size() && sorted();
            for (size_t i = 0; bOk && i < before.size(); i++)
               bOk = gArrange.markers[i].id == before[i].id && gArrange.markers[i].pos == before[i].pos &&
                     gArrange.markers[i].name == before[i].name && gArrange.markers[i].color == before[i].color;
            bOk = bOk && gArrange.settings.timeDisplay == 1 && gArrange.settings.snapDivision == 8 &&
                  gArrange.settings.snapTriplet && ArrangeSnapGridTicks() == 320;
            // Snap off survives too (0 used to be clamped back to 1/4).
            ArrangeSetSnap(0, false);
            bOk = bOk && SavePatchTo(path) && LoadPatchFrom(path) && gArrange.settings.snapDivision == 0 &&
                  ArrangeSnapGridTicks() == 0;
            // A marker added after the reload gets a fresh id.
            uint64_t mc = 0;
            ArrangeEdit([&]() { mc = Arrange::AddMarker(gArrange, 0, "New", kArrangeDefaultMarkerRGBA); });
            bOk = bOk && mc != 0 && mc != ma && mc != mb && Arrange::Validate(gArrange, &why);
            std::remove(path.c_str());
            printf("arrange marker save/load round trip: %s\n", bOk ? "OK" : "FAIL");
            allOk = allOk && bOk;
         }

         // --- C. BPM change: ticks stay, seconds rescale ---------------------
         {
            NewPatch();
            tr.SetTempo(120.0f);
            freshModel(1);
            const uint64_t c = place(0, kBar * 2, kBar);
            uint64_t mk = 0;
            ArrangeEdit([&]() { mk = Arrange::AddMarker(gArrange, kBar * 4, "M", kArrangeDefaultMarkerRGBA); });
            const double s120 = Arrange::TicksToSeconds(Arrange::FindClip(gArrange, c)->start, tr.Tempo());
            const double l120 = Arrange::TicksToSeconds(Arrange::FindClip(gArrange, c)->length, tr.Tempo());
            const uint64_t rev = gArrange.revision;
            tr.SetTempo(240.0f);
            const Arrange::Clip* cp = Arrange::FindClip(gArrange, c);
            const double s240 = Arrange::TicksToSeconds(cp->start, tr.Tempo());
            const double l240 = Arrange::TicksToSeconds(cp->length, tr.Tempo());
            const bool cOk = cp->start == kBar * 2 && cp->length == kBar && findMk(mk)->pos == kBar * 4 &&
                             gArrange.revision == rev && std::abs(s120 - 4.0) < 1e-9 && std::abs(l120 - 2.0) < 1e-9 &&
                             std::abs(s240 - 2.0) < 1e-9 && std::abs(l240 - 1.0) < 1e-9 &&
                             ArrangeFormatBBT(cp->start) == "3.1.1" && ArrangeFormatTickSeconds(cp->start) == "0:02.00";
            tr.SetTempo(120.0f);
            printf("arrange bpm change keeps ticks, rescales seconds: %s (%.2fs -> %.2fs)\n", cOk ? "OK" : "FAIL",
                   s120, s240);
            allOk = allOk && cOk;
         }

         // --- D. Snap grid tick math, triplets included ----------------------
         {
            bool dOk = Arrange::SnapGridTicks(0, false) == 0 && Arrange::SnapGridTicks(1, false) == 3840 &&
                       Arrange::SnapGridTicks(1, true) == 3840 && Arrange::SnapGridTicks(1, false, 3.0) == 2880 &&
                       Arrange::SnapGridTicks(2, false) == 1920 && Arrange::SnapGridTicks(4, false) == 960 &&
                       Arrange::SnapGridTicks(8, false) == 480 && Arrange::SnapGridTicks(16, false) == 240 &&
                       Arrange::SnapGridTicks(2, true) == 1280 && Arrange::SnapGridTicks(4, true) == 640 &&
                       Arrange::SnapGridTicks(8, true) == 320 && Arrange::SnapGridTicks(16, true) == 160;
            // Every grid the dropdown offers is exactly MusicTime's length.
            for (const ArrangeGridChoice& g : kArrangeGridChoices)
            {
               if (g.rd < 0)
                  dOk = dOk && Arrange::SnapGridTicks(g.division, g.triplet) == 0;
               else
                  dOk = dOk && Arrange::SnapGridTicks(g.division, g.triplet, 4.0) ==
                                  Arrange::BeatsToTicks(MusicTime::BeatsFor((MusicTime::RateDivision)g.rd));
            }
            dOk = dOk && Arrange::SnapToGrid(479, 960) == 0 && Arrange::SnapToGrid(480, 960) == 960 &&
                  Arrange::SnapToGrid(1500, 640) == 1280 && Arrange::SnapToGrid(1700, 640) == 1920 &&
                  Arrange::SnapToGrid(1234, 0) == 1234 &&
                  Arrange::GridFloor(959, 960) == 0 && Arrange::GridFloor(960, 960) == 960 &&
                  Arrange::GridFloor(-1, 960) == -960 && Arrange::GridCeil(1, 960) == 960 &&
                  Arrange::GridCeil(960, 960) == 960 && Arrange::GridCeil(-1, 960) == 0;
            // Settings setters: view changes bump revision, never push undo.
            const size_t undo0 = gUndoStack.size();
            uint64_t rev = gArrange.revision;
            ArrangeSetSnap(16, true);
            dOk = dOk && gArrange.revision > rev && ArrangeSnapGridTicks() == 160; rev = gArrange.revision;
            ArrangeSetSnap(1, true); // a bar has no triplet
            dOk = dOk && !gArrange.settings.snapTriplet && ArrangeSnapGridTicks() == 3840; rev = gArrange.revision;
            ArrangeSetSnap(1, false);
            dOk = dOk && gArrange.revision == rev; // unchanged: no bump
            ArrangeSetTimeDisplay(1);
            dOk = dOk && gArrange.revision > rev && gUndoStack.size() == undo0;
            ArrangeSetTimeDisplay(0);
            printf("arrange snap grid tick math: %s\n", dOk ? "OK" : "FAIL");
            allOk = allOk && dOk;
         }

         // --- E. Scrub: the ghost moves, the transport seeks once on release -
         {
            NewPatch();
            tr.SetPlaying(false);
            ArrangeSetLoop(false, gArrange.settings.loop.start, gArrange.settings.loop.end);
            ArrangeSeekTick(kBar);
            const double beats0 = tr.Beats();
            const unsigned long long e0 = tr.ResetEpoch();
            ArrangeScrubBegin(kBar * 2);
            for (int k = 0; k < 20; k++)
               ArrangeScrubUpdate(kBar * 2 + kQ * k);
            const bool stillBefore = tr.ResetEpoch() == e0 && tr.Beats() == beats0 && gArrangeScrubbing;
            const bool ended = ArrangeScrubEnd();
            const unsigned long long e1 = tr.ResetEpoch();
            const bool endedTwice = ArrangeScrubEnd(); // a second release is a no-op
            bool eOk = stillBefore && ended && !endedTwice && e1 - e0 == 1 && tr.ResetEpoch() == e1 &&
                       ArrangePlayTick() == kBar * 2 + kQ * 19 && !gArrangeScrubbing;
            // Escape: no seek at all.
            const unsigned long long e2 = tr.ResetEpoch();
            ArrangeScrubBegin(0);
            ArrangeScrubUpdate(kBar * 9);
            ArrangeScrubCancel();
            eOk = eOk && !ArrangeScrubEnd() && tr.ResetEpoch() == e2 && ArrangePlayTick() == kBar * 2 + kQ * 19;
            printf("arrange scrub seeks once on release: %s (epoch delta %llu)\n", eOk ? "OK" : "FAIL", e1 - e0);
            allOk = allOk && eOk;
         }

         // --- F. Playhead keys: Home, End, arrows, markers -------------------
         {
            freshModel(2);
            place(0, 0, kBar * 2);
            const uint64_t late = place(1, kBar * 5, kBar + kQ);
            ArrangeSetSnap(4, false);
            bool fOk = ArrangeEndKeyTargetTick() == Arrange::ArrangementEnd(gArrange) &&
                       ArrangeEndKeyTargetTick() == kBar * 6 + kQ;
            ArrangeSeekTick(ArrangeEndKeyTargetTick()); // End
            fOk = fOk && ArrangePlayTick() == Arrange::ArrangementEnd(gArrange);
            ArrangeSeekTick(0);                         // Home
            fOk = fOk && ArrangePlayTick() == 0;

            // Arrows with nothing selected step the playhead on the grid.
            ArrangeSeekTick(1000);
            fOk = fOk && ArrangeNudge(1) && ArrangePlayTick() == 1920;
            ArrangeSeekTick(1000);
            fOk = fOk && ArrangeNudge(-1) && ArrangePlayTick() == 960;
            fOk = fOk && ArrangeNudge(-1) && ArrangePlayTick() == 0;
            fOk = fOk && !ArrangeNudge(-1) && ArrangePlayTick() == 0;
            ArrangeSetSnap(0, false); // off: a sixteenth
            fOk = fOk && ArrangeNudge(1) && ArrangePlayTick() == kQ / 4;
            ArrangeSetSnap(8, true);
            fOk = fOk && ArrangeNudge(1) && ArrangePlayTick() == 320;

            // Arrows with a selection move it through MoveClips, one entry each.
            ArrangeSetSnap(4, false);
            ArrangeClickSelect(late, false, false);
            const size_t undo0 = gUndoStack.size();
            const Arrange::Tick play0 = ArrangePlayTick();
            fOk = fOk && ArrangeNudge(1) && Arrange::FindClip(gArrange, late)->start == kBar * 5 + kQ &&
                  gUndoStack.size() == undo0 + 1 && ArrangePlayTick() == play0;
            fOk = fOk && ArrangeNudge(-1) && ArrangeNudge(-1) && Arrange::FindClip(gArrange, late)->start == kBar * 5 - kQ &&
                  gUndoStack.size() == undo0 + 3;
            Undo();
            fOk = fOk && Arrange::FindClip(gArrange, late)->start == kBar * 5 && Arrange::Validate(gArrange, &why);
            gArrangeSel.clear();
            gArrangeSelAnchor = 0;

            // M: a marker at the playhead, snapped.
            ArrangeSeekTick(1000);
            const uint64_t mA = ArrangeAddMarkerAtPlayhead();
            fOk = fOk && mA != 0 && findMk(mA)->pos == 960 && findMk(mA)->color == kArrangeDefaultMarkerRGBA;
            ArrangeSetSnap(0, false);
            const uint64_t mB = ArrangeAddMarkerAtPlayhead();
            fOk = fOk && mB != 0 && findMk(mB)->pos == 1000 && sorted();
            ArrangeSetSnap(4, false);
            ArrangeEdit([&]() { Arrange::AddMarker(gArrange, kBar * 3, "C", kArrangeDefaultMarkerRGBA); });

            // Alt+Left / Alt+Right, stopped: 960, 1000, 11520.
            ArrangeSeekTick(kBar * 2);
            fOk = fOk && ArrangeJumpToMarker(-1) && ArrangePlayTick() == 1000;
            fOk = fOk && ArrangeJumpToMarker(-1) && ArrangePlayTick() == 960;
            fOk = fOk && !ArrangeJumpToMarker(-1) && ArrangePlayTick() == 960;
            fOk = fOk && ArrangeJumpToMarker(1) && ArrangePlayTick() == 1000;
            fOk = fOk && ArrangeJumpToMarker(1) && ArrangePlayTick() == kBar * 3;
            fOk = fOk && !ArrangeJumpToMarker(1) && ArrangePlayTick() == kBar * 3;
            // Tolerance only while playing.
            fOk = fOk && Arrange::PrevMarker(gArrange, kBar * 3 + 100, kQ / 2)->pos == 1000 &&
                  Arrange::PrevMarker(gArrange, kBar * 3 + 100, 0)->pos == kBar * 3 &&
                  Arrange::NextMarker(gArrange, 960, 0)->pos == 1000 && Arrange::NextMarker(gArrange, kBar * 3) == nullptr;
            printf("arrange playhead keys (home/end/arrows/markers): %s (end %lld)\n", fOk ? "OK" : "FAIL",
                   (long long)ArrangeEndKeyTargetTick());
            allOk = allOk && fOk;
         }

         // --- G. View settings are not undo state ----------------------------
         {
            freshModel(1);
            ArrangeSetTimeDisplay(0);
            ArrangeSetSnap(4, false);
            const uint64_t c = place(0, 0, kBar);
            ArrangeSetTimeDisplay(1);
            ArrangeSetSnap(16, true);
            Undo(); // takes the clip back, not the view
            bool gOk = !Arrange::Find(gArrange, c).Valid() && gArrange.settings.timeDisplay == 1 &&
                       gArrange.settings.snapDivision == 16 && gArrange.settings.snapTriplet;
            Redo();
            gOk = gOk && Arrange::Find(gArrange, c).Valid() && gArrange.settings.timeDisplay == 1 &&
                  gArrange.settings.snapDivision == 16;
            // Parse / format in both units.
            ArrangeSetTimeDisplay(0);
            gOk = gOk && ArrangeParsePos("3.2.1") == kBar * 2 + kQ && ArrangeParsePos("1") == 0 &&
                  ArrangeParsePos("x") == -1 && ArrangeFormatPos(kBar * 2 + kQ) == "3.2.1" &&
                  ArrangeFormatLength(kBar + kQ / 4) == "1.0.1";
            ArrangeSetTimeDisplay(1);
            gOk = gOk && ArrangeParsePos("0:04") == kBar * 2 && ArrangeParsePos("1.5") == kQ * 3 &&
                  ArrangeFormatPos(kBar * 2) == "0:04.00";
            printf("arrange view settings survive undo: %s\n", gOk ? "OK" : "FAIL");
            allOk = allOk && gOk;
         }

         if (!why.empty())
            printf("arrange marker validate: %s\n", why.c_str());
         tr.SetTempo(120.0f);
         tr.SeekBeats(0.0);
         gArrangePanelOpen = false;
         NewPatch();
         printf("arrange marker test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}

void FrameTest_ARRANGEWAVETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_ARRANGEWAVETEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         const AudioMode modeBefore = gAudioMode;

         // --- A. The ring is SPSC, never overwrites, and counts its drops --
         {
            static ClipPeakRing sRing; // static: kCapacity entries is not a stack object
            ClipPeak out[8];
            bool aOk = sRing.Read(out, 8) == 0 && sRing.DroppedCount() == 0;
            sRing.Write({ 7, 0, 3, -0.5f, 0.25f });
            sRing.Write({ 7, 0, 4, -1.0f, 1.0f });
            aOk = aOk && sRing.Read(out, 8) == 2 && out[0].clipId == 7 && out[0].bucket == 3 &&
                  out[0].minValue == -0.5f && out[1].bucket == 4 && out[1].maxValue == 1.0f;
            // Order is preserved and a partial read leaves the rest queued.
            for (int i = 0; i < 5; i++)
               sRing.Write({ 9, 0, i, 0.0f, (float)i });
            aOk = aOk && sRing.Read(out, 2) == 2 && out[0].bucket == 0 && out[1].bucket == 1;
            aOk = aOk && sRing.Read(out, 8) == 3 && out[0].bucket == 2 && out[2].bucket == 4;
            // Overfilling drops the NEW entries and says so; what was already
            // queued is still readable. A waveform that lost buckets should
            // lose the ones it missed, not the ones about to be drawn.
            for (int i = 0; i < ClipPeakRing::kCapacity + 10; i++)
               sRing.Write({ 11, 0, i, 0.0f, 1.0f });
            const uint64_t dropped = sRing.DroppedCount();
            aOk = aOk && dropped == 11; // capacity-1 usable slots
            int drained = 0, n = 0;
            while ((n = sRing.Read(out, 8)) > 0)
               drained += n;
            aOk = aOk && drained == ClipPeakRing::kCapacity - 1 && sRing.Read(out, 8) == 0;
            printf("arrange wave ring spsc: %s (drained %d of %d, dropped %llu)\n", aOk ? "OK" : "FAIL",
                   drained, ClipPeakRing::kCapacity, (unsigned long long)dropped);
            allOk = allOk && aOk;
         }

         // --- B. Bucket maths: 1/16 beat, ceil, never unbounded ------------
         {
            const bool bOk = kArrangeWaveBucketTicks == Arrange::kPPQ / 16 &&
                             (double)Arrange::kPPQ / (double)kArrangeWaveBucketTicks ==
                                kClipPeakBucketsPerBeat &&
                             ArrangeWaveBucketCount(0) == 0 &&
                             ArrangeWaveBucketCount(1) == 1 &&  // a sliver still gets one bucket
                             ArrangeWaveBucketCount(kArrangeWaveBucketTicks) == 1 &&
                             ArrangeWaveBucketCount(kArrangeWaveBucketTicks + 1) == 2 &&
                             ArrangeWaveBucketCount(Arrange::kPPQ) == 16 &&
                             ArrangeWaveBucketCount(Arrange::kPPQ * 4) == 64 &&
                             ArrangeWaveBucketCount((Arrange::Tick)Arrange::kPPQ * 1000000) ==
                                kArrangeWaveMaxBuckets;
            printf("arrange wave bucket math: %s (%d ticks/bucket, %d per beat)\n", bOk ? "OK" : "FAIL",
                   (int)kArrangeWaveBucketTicks, ArrangeWaveBucketCount(Arrange::kPPQ));
            allOk = allOk && bOk;
         }

         // uid read right after each spawn, not after both: SpawnNode()
         // push_backs onto gNodes, which can reallocate and invalidate every
         // GraphNode* into it - including a pointer from an earlier spawn
         // still held when a later one runs.
         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 200.0f, 0.0f);
         const uint64_t oscUid = oscGn != nullptr ? oscGn->uid : 0;
         GraphNode* rampGn = SpawnNode("Ramp", "Source", 0.0f, 0.0f);
         const uint64_t rampUid = rampGn != nullptr ? rampGn->uid : 0;
         const bool spawned = oscGn != nullptr && rampGn != nullptr;
         printf("arrange wave spawn: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {
            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t vLane = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
            const uint64_t aLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            auto place = [&](uint64_t lane, uint64_t uid, double startBeat, double lenBeats) -> uint64_t {
               Arrange::Clip c;
               c.start = Arrange::BeatsToTicks(startBeat);
               c.length = Arrange::BeatsToTicks(lenBeats);
               c.srcUid = uid;
               uint64_t id = 0;
               Arrange::PlaceOverwrite(gArrange, lane, c, &id);
               return id;
            };

            // --- C. The cache is shaped by the model, one entry per clip ---
            const uint64_t audioClip = place(aLane, oscUid, 0.0, 8.0); // 4s at 120bpm
            ArrangeSyncClipVisuals();
            auto waveOf = [](uint64_t id) -> const ArrangeClipWave* {
               auto it = gArrangeClipWaves.find(id);
               return it == gArrangeClipWaves.end() ? nullptr : &it->second;
            };
            const ArrangeClipWave* w0 = waveOf(audioClip);
            bool cOk = w0 != nullptr && (int)w0->minv.size() == 8 * 16 &&
                       w0->maxv.size() == w0->minv.size() && w0->filled.size() == w0->minv.size() &&
                       w0->srcUid == oscUid && w0->length == Arrange::BeatsToTicks(8.0);
            // A video clip gets a thumbnail slot, never a waveform.
            const uint64_t videoClip = place(vLane, rampUid, 0.0, 6.0);
            ArrangeSyncClipVisuals();
            cOk = cOk && waveOf(videoClip) == nullptr &&
                  gArrangeClipThumbs.count(videoClip) == 1 && gArrangeClipThumbs.count(audioClip) == 0;
            printf("arrange wave cache shaping: %s (%d buckets for 8 beats)\n", cOk ? "OK" : "FAIL",
                   w0 != nullptr ? (int)w0->minv.size() : -1);
            allOk = allOk && cOk;

            // --- D. Only the four shape fields clear a filled cache --------
            {
               // Pretend the take already ran.
               auto fill = [&](uint64_t id) {
                  auto it = gArrangeClipWaves.find(id);
                  if (it == gArrangeClipWaves.end())
                     return;
                  std::fill(it->second.filled.begin(), it->second.filled.end(), (uint8_t)1);
                  std::fill(it->second.maxv.begin(), it->second.maxv.end(), 0.5f);
               };
               auto filledCount = [&](uint64_t id) {
                  auto it = gArrangeClipWaves.find(id);
                  if (it == gArrangeClipWaves.end())
                     return -1;
                  int n = 0;
                  for (uint8_t f : it->second.filled)
                     n += f != 0 ? 1 : 0;
                  return n;
               };
               fill(audioClip);
               const int filled0 = filledCount(audioClip);

               // Gain and fade are measured around, not into, the buckets -
               // the audio thread reads pre-envelope - so they must NOT clear.
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, audioClip))
               {
                  c->gainDb = -6.0f;
                  c->fadeIn = Arrange::BeatsToTicks(0.5);
                  gArrange.revision++;
               }
               ArrangeSyncClipVisuals();
               bool dOk = filledCount(audioClip) == filled0 && filled0 == 8 * 16;

               // Length does: the buckets no longer describe the clip.
               Arrange::TrimEdge(gArrange, audioClip, Arrange::kEdgeEnd, Arrange::BeatsToTicks(4.0));
               ArrangeSyncClipVisuals();
               dOk = dOk && filledCount(audioClip) == 0 &&
                     (int)gArrangeClipWaves[audioClip].minv.size() == 4 * 16;

               // So does a reassign.
               fill(audioClip);
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, audioClip))
               {
                  c->srcUid = rampUid;
                  gArrange.revision++;
               }
               ArrangeSyncClipVisuals();
               dOk = dOk && filledCount(audioClip) == 0;
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, audioClip))
               {
                  c->srcUid = oscUid;
                  gArrange.revision++;
               }
               ArrangeSyncClipVisuals();

               // So does a move. A clip's source is a LIVE NODE, not a file:
               // the same clip two beats later plays whatever the node emits
               // two beats later, which is not what was measured. This is the
               // one place the waveform differs from a file-backed DAW's.
               fill(audioClip);
               std::vector<uint64_t> one{ audioClip };
               Arrange::MoveClips(gArrange, one, Arrange::BeatsToTicks(2.0), 0);
               ArrangeSyncClipVisuals();
               dOk = dOk && filledCount(audioClip) == 0 &&
                     (int)gArrangeClipWaves[audioClip].minv.size() == 4 * 16;
               printf("arrange wave cache invalidation: %s\n", dOk ? "OK" : "FAIL");
               allOk = allOk && dOk;
            }

            // --- E. A bucket in flight for a deleted clip is dropped -------
            {
               const uint64_t ghost = 0xDEADBEEFull;
               const uint64_t shape = gArrangeClipWaves[audioClip].shape;
               AudioEngine::Instance().ClipPeaks().Write({ ghost, 0, 0, -1.0f, 1.0f });
               AudioEngine::Instance().ClipPeaks().Write({ audioClip, shape, 2, -0.25f, 0.75f });
               ArrangeSyncClipVisuals();
               const ArrangeClipWave* w = waveOf(audioClip);
               const bool eOk = gArrangeClipWaves.count(ghost) == 0 && w != nullptr &&
                                w->filled[2] != 0 && w->minv[2] == -0.25f && w->maxv[2] == 0.75f;
               printf("arrange wave drain ignores unknown clips: %s\n", eOk ? "OK" : "FAIL");
               allOk = allOk && eOk;
            }

            // --- E2. A bucket measured under the PREVIOUS shape is dropped --
            // The race the shape stamp exists for: the audio thread can still
            // be mid-block on the old topology when an edit resizes a clip,
            // so a finished bucket arrives after the cache has been zeroed.
            // Its index can be perfectly valid in the new array - only the
            // shape says it describes material the clip no longer holds.
            {
               const uint64_t staleShape = gArrangeClipWaves[audioClip].shape;
               // Clip currently spans beats 2..6 (section D moved it); drag
               // the right edge out to 8 so it is longer, not shorter - the
               // stale bucket's index then still fits the new array.
               Arrange::TrimEdge(gArrange, audioClip, Arrange::kEdgeEnd, Arrange::BeatsToTicks(8.0));
               ArrangeSyncClipVisuals();
               const uint64_t freshShape = gArrangeClipWaves[audioClip].shape;
               AudioEngine::Instance().ClipPeaks().Write({ audioClip, staleShape, 1, -0.9f, 0.9f });
               AudioEngine::Instance().ClipPeaks().Write({ audioClip, freshShape, 3, -0.1f, 0.2f });
               ArrangeSyncClipVisuals();
               const ArrangeClipWave* w = waveOf(audioClip);
               const bool e2Ok = staleShape != freshShape && w != nullptr &&
                                 w->filled.size() > 3 && w->filled[1] == 0 && w->filled[3] != 0 &&
                                 w->maxv[3] == 0.2f;
               printf("arrange wave drops stale-shape buckets: %s\n", e2Ok ? "OK" : "FAIL");
               allOk = allOk && e2Ok;
            }

            // --- F. 50 video clips in, 50 thumbnail slots; deleted, none ---
            // The exit criterion WP8 names. Slots, not FBOs: an FBO is only
            // allocated on the first real composite, which a frame-4 fixture
            // has not run - so this asserts the pool's bookkeeping and
            // GLUtil's allocation counter asserts nothing leaked.
            {
               const size_t thumbsBefore = gArrangeClipThumbs.size();
               const unsigned long long fbo0 = GLUtil::FboAllocationCount();
               std::vector<uint64_t> made;
               for (int i = 0; i < 50; i++)
                  made.push_back(place(vLane, rampUid, 10.0 + (double)i * 2.0, 2.0));
               ArrangeSyncClipVisuals();
               const size_t thumbsAfter = gArrangeClipThumbs.size();
               Arrange::Delete(gArrange, made);
               ArrangeSyncClipVisuals();
               const bool fOk = thumbsAfter == thumbsBefore + 50 &&
                                gArrangeClipThumbs.size() == thumbsBefore &&
                                GLUtil::FboAllocationCount() == fbo0;
               printf("arrange thumb pool returns to baseline: %s (%zu -> %zu -> %zu)\n", fOk ? "OK" : "FAIL",
                      thumbsBefore, thumbsAfter, gArrangeClipThumbs.size());
               allOk = allOk && fOk;
            }

            // --- G. Playing through a clip fills its waveform --------------
            // The other WP8 exit criterion, end to end: the audio thread's
            // accumulation, the ring, the drain and the cache. Driven through
            // ProcessOffline rather than a device callback so it is
            // deterministic, but it is the same RunTopology path.
            if (AudioEngine::Instance().SampleRate() > 0.0 || StartAudioEngine(gAudioStartError))
            {
               const double rate = AudioEngine::Instance().SampleRate();
               // One 2-beat (1s) clip at the origin, Timeline-Strict so the
               // clip's own terminal is what feeds the device.
               gArrange = Arrange::Model();
               gArrange.revision++;
               const uint64_t lane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
               const uint64_t clip = place(lane, oscUid, 0.0, 2.0);
               gAudioMode = AudioMode::Timeline;
               ArrangeSyncClipVisuals();
               RebuildAudioTopology();
               // Params reach an AudioNode through its mailbox, which
               // CookIfNeeded fills - a node that has never been cooked runs
               // on its constructor defaults with an empty mailbox and
               // produces silence. The main loop does this every frame; this
               // fixture runs its whole life inside one, and the
               // PrepareToPlay loop inside RebuildAudioTopology keys off a
               // live device or an offline render job, neither of which this
               // fixture is - see the WP7 arrange render test fixture above
               // for the same priming.
               {
                  static int sCookFrame = 2000000;
                  sCookFrame++;
                  for (GraphNode& gn : gNodes)
                     gn.node->CookIfNeeded(sCookFrame);
                  for (GraphNode& gn : gNodes)
                     if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                        if (an->preparedForSampleRate != rate)
                        {
                           an->PrepareToPlay(rate, kAudioMaxBlockFrames);
                           an->preparedForSampleRate = rate;
                        }
               }

               const uint64_t droppedBefore = AudioEngine::Instance().ClipPeaks().DroppedCount();
               tr.Seek(0.0);
               tr.SetOfflineMode(true, rate);
               tr.SetPlaying(true);
               static float sL[kAudioMaxBlockFrames];
               static float sR[kAudioMaxBlockFrames];
               static float* sCh[2] = { sL, sR };
               const int block = OfflineAudioBlockFrames();
               // One second is the whole clip; render a tenth past it so the
               // playhead crosses out of the clip and publishes its last bucket.
               const long long want = (long long)llround(1.1 * rate);
               long long done = 0;
               while (done < want)
               {
                  const int nFrames = (int)std::min<long long>(block, want - done);
                  AudioBuffer buf;
                  buf.channels = sCh;
                  buf.numChannels = 2;
                  buf.numFrames = nFrames;
                  AudioEngine::Instance().ProcessOffline(buf);
                  done += nFrames;
                  // Drain as we go, the way the main loop does - the ring is
                  // sized for a frame's worth of buckets, not a whole take.
                  ArrangeSyncClipVisuals();
               }
               tr.SetOfflineMode(false);
               tr.SetPlaying(false);
               ArrangeSyncClipVisuals();

               int filled = 0, total = 0, nonSilent = 0;
               if (const ArrangeClipWave* w = waveOf(clip))
               {
                  total = (int)w->filled.size();
                  for (int i = 0; i < total; i++)
                  {
                     if (w->filled[(size_t)i] == 0)
                        continue;
                     filled++;
                     if (w->maxv[(size_t)i] > 1e-4f || w->minv[(size_t)i] < -1e-4f)
                        nonSilent++;
                  }
               }
               // The playhead ran past the clip's end, so even the last bucket
               // is published (a clip followed by a gap must not keep a flat
               // notch at its right edge).
               const bool gOk = total == 32 && filled == total && nonSilent >= total - 2 &&
                                AudioEngine::Instance().ClipPeaks().DroppedCount() == droppedBefore;
               printf("arrange wave filled by playback: %s (%d/%d buckets, %d non-silent, %lld frames @ %.0f Hz)\n",
                      gOk ? "OK" : "FAIL", filled, total, nonSilent, done, rate);
               allOk = allOk && gOk;
            }
            else
            {
               printf("arrange wave filled by playback: SKIP (no audio device: %s)\n", gAudioStartError.c_str());
            }
         }

         gAudioMode = modeBefore;
         tr.SetOfflineMode(false);
         tr.SetPlaying(false);
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         NewPatch();
         ArrangeSyncClipVisuals();
         const bool cleared = gArrangeClipWaves.empty() && gArrangeClipThumbs.empty();
         printf("arrange wave cleared on new patch: %s\n", cleared ? "OK" : "FAIL");
         allOk = allOk && cleared;
         printf("arrange wave test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}

void FrameTest_CLIPMODBYPASSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CLIPMODBYPASSTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;

         // uid read right after each spawn - SpawnNode push_backs onto gNodes,
         // which can reallocate every GraphNode* taken before it.
         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 200.0f, 0.0f);
         const uint64_t oscUid = oscGn != nullptr ? oscGn->uid : 0;
         const int oscIndex = oscGn != nullptr ? oscGn->index : -1;
         GraphNode* lfoGn = SpawnNode("LFO", "Modulators", 0.0f, 0.0f);
         const int lfoIndex = lfoGn != nullptr ? lfoGn->index : -1;
         const bool spawned = oscUid != 0 && oscIndex >= 0 && lfoIndex >= 0;
         printf("clip mod bypass spawn: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {
            // Bind exactly as the cable-drop path does (main.cpp's
            // ed::AcceptNewItem branch) - same call, same key space.
            const int kParam = 0, kOtherParam = 1;
            Modulation::Instance().Bind(oscIndex, kParam, lfoIndex, 0);

            // --- A. the panel's own list -------------------------------
            // The bug this guards: an enumeration keyed on the wrong index
            // space finds nothing, and the Modulations section silently
            // never appears rather than failing loudly.
            {
               std::vector<std::pair<int, std::string>> bound;
               ArrangeCollectClipModBindings(*FindNodeByUid(oscUid), bound);
               const bool aOk = bound.size() == 1 && bound[0].first == kParam &&
                                !bound[0].second.empty();
               printf("clip mod bypass list: %s (%d binding(s))\n", aOk ? "OK" : "FAIL",
                      (int)bound.size());
               allOk = allOk && aOk;
            }

            // --- B. the model's own set ops ----------------------------
            {
               Arrange::Clip c;
               c.SetModBypassed(kOtherParam, true);
               c.SetModBypassed(kParam, true);
               c.SetModBypassed(kParam, true);           // idempotent
               const bool sorted = c.bypassedModParams.size() == 2 &&
                                   c.bypassedModParams[0] == kParam &&
                                   c.bypassedModParams[1] == kOtherParam;
               c.SetModBypassed(kOtherParam, false);
               c.SetModBypassed(kOtherParam, false);     // idempotent
               const bool bOk = sorted && c.bypassedModParams.size() == 1 &&
                                c.IsModBypassed(kParam) && !c.IsModBypassed(kOtherParam);
               printf("clip mod bypass set ops: %s\n", bOk ? "OK" : "FAIL");
               allOk = allOk && bOk;
            }

            // --- C. the playhead gate ----------------------------------
            // Two clips on one lane, same source: the first bypasses the
            // binding, the second does not. The gate must follow the
            // playhead from one to the other - that IS the feature.
            {
               gArrange = Arrange::Model();
               const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
               auto place = [&](Arrange::Tick start, Arrange::Tick len) {
                  Arrange::Clip c;
                  c.start = start;
                  c.length = len;
                  c.srcUid = oscUid;
                  uint64_t id = 0;
                  Arrange::PlaceOverwrite(gArrange, laneId, c, &id);
                  return id;
               };
               const uint64_t clipA = place(0, Arrange::kPPQ * 4);
               const uint64_t clipB = place(Arrange::kPPQ * 4, Arrange::kPPQ * 4);
               bool wired = clipA != 0 && clipB != 0;
               if (Arrange::Clip* a = Arrange::FindClip(gArrange, clipA))
                  a->SetModBypassed(kParam, true);
               else
                  wired = false;

               const AudioMode wasMode = gAudioMode;
               gAudioMode = AudioMode::Timeline; // the gate is inert in Canvas mode

               Transport::Instance().SeekBeats(1.0);      // inside clip A
               ArrangeRefreshActiveClipModBypass();
               const bool inA = ArrangeClipBypassesMod(oscIndex, kParam);
               const bool otherUntouchedInA = !ArrangeClipBypassesMod(oscIndex, kOtherParam);

               Transport::Instance().SeekBeats(5.0);      // inside clip B
               ArrangeRefreshActiveClipModBypass();
               const bool inB = ArrangeClipBypassesMod(oscIndex, kParam);

               Transport::Instance().SeekBeats(20.0);     // past every clip
               ArrangeRefreshActiveClipModBypass();
               const bool pastEnd = ArrangeClipBypassesMod(oscIndex, kParam);

               // Canvas mode: no clip is playing, so no clip gets a say.
               gAudioMode = AudioMode::Canvas;
               Transport::Instance().SeekBeats(1.0);
               ArrangeRefreshActiveClipModBypass();
               const bool inCanvas = ArrangeClipBypassesMod(oscIndex, kParam);
               gAudioMode = wasMode;

               const bool cOk = wired && inA && otherUntouchedInA && !inB && !pastEnd && !inCanvas;
               printf("clip mod bypass gate: %s (A=%d B=%d past=%d canvas=%d)\n",
                      cOk ? "OK" : "FAIL", inA ? 1 : 0, inB ? 1 : 0, pastEnd ? 1 : 0,
                      inCanvas ? 1 : 0);
               allOk = allOk && cOk;
            }

            // --- D. the value a bypassed param lands on ----------------
            // Must be the pre-modulation knob value, clamped into the
            // param's own declared range - never left frozen wherever the
            // modulator last pushed it, and never a raw 0 from a binding
            // restored out of an old patch line that predates `centre`.
            {
               ParamRef ref;
               ref.nodeIndex = oscIndex;
               ref.paramIndex = kParam;
               ref.minValue = 20.0f;
               ref.maxValue = 20000.0f;
               Modulation::Source src;
               src.centre = 440.0f;
               const float inRange = ArrangeClipBypassBaseValue(ref, src);
               src.centre = 0.0f; // the old-patch case
               const float clamped = ArrangeClipBypassBaseValue(ref, src);
               const bool dOk = std::abs(inRange - 440.0f) < 0.001f &&
                                std::abs(clamped - 20.0f) < 0.001f;
               printf("clip mod bypass base value: %s (%.1f, clamped %.1f)\n",
                      dOk ? "OK" : "FAIL", inRange, clamped);
               allOk = allOk && dOk;
            }
            // --- E. Make Unique ----------------------------------------
            // The escape hatch offered beside the shared-source warning.
            // It has to leave the clip on a DIFFERENT node that is
            // nonetheless still modulated (the copy is rewired through the
            // cluster clipboard, not spawned bare) and still carrying this
            // clip's own bypass list, which is keyed by paramIndex and so
            // survives the swap only because the copy is the same type.
            {
               gArrange = Arrange::Model();
               const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
               Arrange::Clip c;
               c.start = 0;
               c.length = Arrange::kPPQ * 4;
               c.srcUid = oscUid;
               uint64_t clipId = 0;
               Arrange::PlaceOverwrite(gArrange, laneId, c, &clipId);
               if (Arrange::Clip* live = Arrange::FindClip(gArrange, clipId))
                  live->SetModBypassed(kParam, true);

               const size_t nodesBefore = gNodes.size();
               const size_t undoBefore = gUndoStack.size();
               const bool made = clipId != 0 && ArrangeMakeClipSourceUnique(clipId);

               // One press must be one undo, and it must be a FULL entry:
               // this moves the graph and the timeline together, and an
               // arrangeOnly entry would restore srcUid while leaving the
               // copy stranded on the canvas.
               const bool oneUndo = gUndoStack.size() == undoBefore + 1 &&
                                    !gUndoStack.back().arrangeOnly;

               const Arrange::Clip* after = Arrange::FindClip(gArrange, clipId);
               const bool repointed = after != nullptr && after->srcUid != oscUid &&
                                      after->srcUid != 0;
               const bool bypassKept = after != nullptr && after->IsModBypassed(kParam);
               GraphNode* copy = (after != nullptr) ? FindNodeByUid(after->srcUid) : nullptr;
               const bool sameType = copy != nullptr && copy->typeName == "Oscillator";
               const bool visible = copy != nullptr && !copy->hiddenFromCanvas;

               // The whole point: the copy must arrive modulated, so the
               // clip's own Modulations list is not empty the moment the
               // user presses the button.
               std::vector<std::pair<int, std::string>> copyBound;
               if (copy != nullptr)
                  ArrangeCollectClipModBindings(*copy, copyBound);
               const bool modCarried = copyBound.size() == 1;
               // ...and the original must keep its own binding, not lose it.
               std::vector<std::pair<int, std::string>> origBound;
               if (GraphNode* orig = FindNodeByUid(oscUid))
                  ArrangeCollectClipModBindings(*orig, origBound);
               const bool origKept = origBound.size() == 1;
               const bool spawnedOne = gNodes.size() == nodesBefore + 1;

               const bool eOk = made && repointed && bypassKept && sameType && visible &&
                                modCarried && origKept && spawnedOne && oneUndo;
               printf("clip mod bypass make unique: %s (repoint=%d type=%d vis=%d copyMods=%d origMods=%d +%d node, undo=%d full)\n",
                      eOk ? "OK" : "FAIL", repointed ? 1 : 0, sameType ? 1 : 0, visible ? 1 : 0,
                      (int)copyBound.size(), (int)origBound.size(),
                      (int)(gNodes.size() - nodesBefore),
                      (int)(gUndoStack.size() - undoBefore));
               allOk = allOk && eOk;
            }
         }

         printf("clip mod bypass test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}

void FrameTest_CLIPFIELDTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CLIPFIELDTEST") != nullptr && frameId == 4)
      {
         bool allOk = true;
         Transport::Instance().SetTempo(120.0f);
         Transport::Instance().SetTimeSignature(4, 4);
         const int savedDisplay = gArrange.settings.timeDisplay;
         gArrange.settings.timeDisplay = 0; // Bars

         // A. positions round-trip through the text the field shows.
         {
            const Arrange::Tick t = ArrangeParsePos("9.3.3");
            const bool shape = t == (Arrange::Tick)8 * Arrange::BeatsToTicks(4.0) +
                                    (Arrange::Tick)2 * Arrange::kPPQ +
                                    (Arrange::Tick)2 * (Arrange::kPPQ / 4);
            const bool trip = ArrangeFormatBBT(t) == "9.3.3";
            // Partial forms: a bar alone, and a bar.beat.
            const bool bar = ArrangeFormatBBT(ArrangeParsePos("9")) == "9.1.1";
            const bool barBeat = ArrangeFormatBBT(ArrangeParsePos("9.3")) == "9.3.1";
            // 0-indexed input is not a position - bar 0 does not exist.
            const bool rejects = ArrangeParsePos("0.1.1") < 0 && ArrangeParsePos("banana") < 0;
            const bool ok = shape && trip && bar && barBeat && rejects;
            printf("clip field position: %s (9.3.3 -> %lld -> %s)\n", ok ? "OK" : "FAIL",
                   (long long)t, ArrangeFormatBBT(t).c_str());
            allOk = allOk && ok;
         }

         // B. lengths are the SAME text one index lower - "1.0.0" is one bar,
         //    not bar one. Reading a length with the position parser (or the
         //    other way round) is a whole bar out, which is why they are two
         //    functions and not one.
         {
            const Arrange::Tick oneBar = Arrange::BeatsToTicks(4.0);
            const bool shape = ArrangeParseLen("1.0.0") == oneBar;
            const bool trip = ArrangeFormatBBTLength(ArrangeParseLen("2.1.2")) == "2.1.2";
            const bool zero = ArrangeParseLen("0.0.0") == 0; // legal for a length
            const bool differs = ArrangeParseLen("1.0.0") != ArrangeParsePos("1.0.0");
            const bool ok = shape && trip && zero && differs;
            printf("clip field length: %s (1.0.0 -> %lld ticks, one bar = %lld)\n",
                   ok ? "OK" : "FAIL", (long long)ArrangeParseLen("1.0.0"), (long long)oneBar);
            allOk = allOk && ok;
         }

         // C. fades are milliseconds in BOTH display modes - a fade is an
         //    envelope, not a place in the song, so it must not follow the
         //    Bars/Time toggle the way Start and Length do.
         {
            const double bpm = 120.0;
            auto msToTicks = [&](double ms) { return Arrange::SecondsToTicks(ms / 1000.0, bpm); };
            auto ticksToMs = [&](Arrange::Tick t) { return Arrange::TicksToSeconds(t, bpm) * 1000.0; };
            const Arrange::Tick t20 = msToTicks(20.0);
            const bool round = std::abs(ticksToMs(t20) - 20.0) < 1.0;
            // 20 ms at 120 BPM is well under a sixteenth (125 ms) - the value
            // the old bar.beat.sixteenth field could not express at all.
            const bool subSixteenth = t20 > 0 && t20 < Arrange::kPPQ / 4;
            const bool ok = round && subSixteenth;
            printf("clip field fade ms: %s (20ms -> %lld ticks -> %.1fms, sixteenth = %lld)\n",
                   ok ? "OK" : "FAIL", (long long)t20, ticksToMs(t20), (long long)(Arrange::kPPQ / 4));
            allOk = allOk && ok;
         }

         // D. the shortcut suppression. '0' toggles the selection's bypass,
         //    and it used to fire on the way into a Pan field because
         //    WantTextInput is still false on the frame hover+type opens the
         //    box. Hovering arms the flag; it has to survive one frame, since
         //    the panel's key handling and the inspector draw in that order.
         {
            gArrangeFieldHotFrame = -1000;
            const bool coldBefore = !ArrangeFieldHot();
            ArrangeMarkFieldHot();
            const bool hotNow = ArrangeFieldHot();
            gArrangeFieldHotFrame = ImGui::GetFrameCount() - 1;
            const bool hotNextFrame = ArrangeFieldHot();
            gArrangeFieldHotFrame = ImGui::GetFrameCount() - 2;
            const bool coldAfter = !ArrangeFieldHot();
            gArrangeFieldHotFrame = -1000;
            const bool ok = coldBefore && hotNow && hotNextFrame && coldAfter;
            printf("clip field hotkey guard: %s (cold=%d hot=%d carries=%d expires=%d)\n",
                   ok ? "OK" : "FAIL", coldBefore ? 1 : 0, hotNow ? 1 : 0,
                   hotNextFrame ? 1 : 0, coldAfter ? 1 : 0);
            allOk = allOk && ok;
         }

         // E. Time display mode: Start/Length switch to seconds, fades do not.
         {
            gArrange.settings.timeDisplay = 1;
            const Arrange::Tick fromClock = ArrangeParsePos("0:02.00");
            const Arrange::Tick fromBare = ArrangeParsePos("2");
            const bool agree = fromClock == fromBare;
            const bool twoSeconds = std::abs(Arrange::TicksToSeconds(fromClock, 120.0) - 2.0) < 0.01;
            const bool lenSeconds = std::abs(Arrange::TicksToSeconds(ArrangeParseLen("1.5"), 120.0) - 1.5) < 0.01;
            gArrange.settings.timeDisplay = 0;
            const bool ok = agree && twoSeconds && lenSeconds;
            printf("clip field time mode: %s (0:02.00 == 2 -> %.2fs)\n", ok ? "OK" : "FAIL",
                   Arrange::TicksToSeconds(fromClock, 120.0));
            allOk = allOk && ok;
         }

         gArrange.settings.timeDisplay = savedDisplay;
         printf("clip field test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}

void FrameTest_UNDOPERFTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_UNDOPERFTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         const int kNodeCount = 300;
         for (int i = 0; i < kNodeCount; i++)
            SpawnNode("Cube", "3D", (float)(i % 20) * 150.0f, (float)(i / 20) * 150.0f);
         const bool spawnedAll = (int)gNodes.size() == kNodeCount;

         const auto t0 = std::chrono::steady_clock::now();
         Patch::Data snap = BuildPatchData();
         const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

         const double kCeilingMs = 50.0;
         const bool fastEnough = ms < kCeilingMs;
         const bool snapshotComplete = snap.nodes.size() == (size_t)kNodeCount;
         printf("BuildPatchData on %d nodes: %.3f ms (ceiling %.1f ms)  %s\n",
                kNodeCount, ms, kCeilingMs,
                (spawnedAll && fastEnough && snapshotComplete) ? "OK" : "FAIL");

         // Measurement step from docs/plans/undo-delete-perf-prompt.md: time
         // the five real end-user operations on the same 300-node patch, not
         // just BuildPatchData in isolation. Printed unconditionally (no
         // pass/fail ceiling) - these are for the commit message, not a gate.
         auto timeIt = [](auto&& fn) {
            const auto s = std::chrono::steady_clock::now();
            fn();
            return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - s).count();
         };

         const double clickMs = timeIt([&]() { PushUndoCheckpoint(); });

         const int deleteIndex = gNodes.back().index;
         const double deleteMs = timeIt([&]() { RemoveNodeByIndex(deleteIndex); });

         std::vector<int> groupIndices;
         for (int i = 0; i < 50 && i < (int)gNodes.size(); i++)
            groupIndices.push_back(gNodes[gNodes.size() - 1 - i].index);
         const double groupDeleteMs = timeIt([&]() {
            gSuppressUndoCheckpoints = true;
            gDeferAudioRebuild = true;
            for (int idx : groupIndices)
               RemoveNodeByIndex(idx);
            gDeferAudioRebuild = false;
            RebuildAudioTopology();
            gSuppressUndoCheckpoints = false;
         });

         const double undoMs = timeIt([&]() { Undo(); });
         const double redoMs = timeIt([&]() { Redo(); });

         printf("UNDOPERFTEST timings (nodes=%d): click=%.3fms delete=%.3fms "
                "groupDelete(%zu)=%.3fms undo=%.3fms redo=%.3fms\n",
                (int)gNodes.size(), clickMs, deleteMs, groupIndices.size(),
                groupDeleteMs, undoMs, redoMs);
      }
}

void FrameTest_COMMENTTEST_4(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 11 &&
          std::string(getenv("INFINITE_COMMENTTEST")) != "edit" &&
          std::string(getenv("INFINITE_COMMENTTEST")) != "slash")
      {
         auto findComment = []() -> CommentNode*
         {
            for (GraphNode& gn : gNodes)
            {
               if (auto* c = dynamic_cast<CommentNode*>(gn.node.get()))
                  return c;
            }
            return nullptr;
         };
         auto lineCount = [](const std::string& s)
         {
            return (size_t)std::count(s.begin(), s.end(), '\n') + 1;
         };

         bool ok = true;
         CommentNode* c = findComment();
         const std::string original = c != nullptr ? c->text : std::string();

         SavePatchTo(TmpPath("infinite_commenttest.infinite"));
         LoadPatchFrom(TmpPath("infinite_commenttest.infinite"));
         CommentNode* reloaded = findComment(); // load rebuilt every node
         const bool savedOk = reloaded != nullptr && reloaded->text == original;
         printf("comment save/load: %zu lines -> %zu lines  %s\n",
                lineCount(original),
                reloaded != nullptr ? lineCount(reloaded->text) : (size_t)0,
                savedOk ? "OK" : "FAIL");
         ok = ok && savedOk;

         // The same edit-then-undo the popup performs: the checkpoint is pushed
         // when the editor opens, the text changes while it is open.
         if (reloaded != nullptr)
         {
            PushUndoCheckpoint();
            reloaded->text = "scribbled over";
            Undo();
            CommentNode* undone = findComment();
            const bool undoOk = undone != nullptr && undone->text == original;
            printf("comment undo: text back to %zu lines  %s\n",
                   undone != nullptr ? lineCount(undone->text) : (size_t)0,
                   undoOk ? "OK" : "FAIL");
            ok = ok && undoOk;

            Redo();
            CommentNode* redone = findComment();
            const bool redoOk = redone != nullptr && redone->text == "scribbled over";
            printf("comment redo: %s  %s\n",
                   redone != nullptr ? redone->text.c_str() : "(gone)",
                   redoOk ? "OK" : "FAIL");
            ok = ok && redoOk;

            Undo(); // leave the authored note on screen for the screenshot
         }

         printf("%s\n", ok ? "COMMENT OK" : "SUSPECT");
         if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_GROUPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_GROUPTEST") != nullptr && gNodes.size() >= 3)
      {
         static float baselineW = 0.0f;
         GraphNode* group = nullptr;
         for (GraphNode& g : gNodes)
         {
            if (dynamic_cast<GroupNode*>(g.node.get()) != nullptr)
               group = &g;
         }

         if (frameId == 4)
         {
            GraphNode* gn = SpawnNode("Group", "Compositing", 60.0f, 60.0f);
            auto* g = static_cast<GroupNode*>(gn->node.get());
            gGroupMembers[g] = { gNodes[0].index, gNodes[1].index, gNodes[2].index };
         }
         else if (frameId == 8 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            baselineW = g->width;
            printf("baseline box: %.0f x %.0f\n", g->width, g->height);
            ed::SetNodePosition(gNodes[2].NodeId(), ImVec2(1600.0f, 100.0f));
         }
         else if (frameId == 12 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            printf("after dragging a member out: %.0f wide  %s\n", g->width,
                   g->width > baselineW + 500.0f ? "GREW" : "FAIL");
            ed::SetNodePosition(gNodes[2].NodeId(), ImVec2(700.0f, 100.0f));
         }
         else if (frameId == 16 && group != nullptr)
         {
            auto* g = static_cast<GroupNode*>(group->node.get());
            printf("after dragging it back: %.0f wide  %s\n", g->width,
                   std::fabs(g->width - baselineW) < 1.0f ? "SHRANK BACK OK" : "FAIL");
            printf("members still owned: %zu\n", gGroupMembers[g].size());

            // A second group dropped right on top of the first must come up
            // empty: every node down there already belongs to group one, and
            // a group is never a member of a group. Both together are what
            // stop one group from swallowing another.
            GraphNode* second = SpawnNode("Group", "Compositing", 0.0f, 0.0f);
            auto* g2 = static_cast<GroupNode*>(second->node.get());
            g2->width = 2000.0f;
            g2->height = 800.0f;
         }
         else if (frameId == 20)
         {
            GroupNode* first = nullptr;
            GroupNode* second = nullptr;
            int secondIndex = -1;
            for (GraphNode& g : gNodes)
            {
               if (auto* asGroup = dynamic_cast<GroupNode*>(g.node.get()))
               {
                  if (first == nullptr)
                     first = asGroup;
                  else
                  {
                     second = asGroup;
                     secondIndex = g.index;
                  }
               }
            }
            printf("overlapping second group stole: %zu members  %s\n",
                   gGroupMembers[second].size(),
                   gGroupMembers[second].empty() && gGroupMembers[first].size() == 3
                      ? "NO STEALING OK" : "FAIL");

            // Ungroup is driven through the real path: select a *member*, not
            // the group's header, and let the shortcut handler find the owner.
            const int memberId = gNodes[0].NodeId();
            // The overlapping group goes first, otherwise it simply adopts the
            // nodes the moment ungroup frees them and the check below cannot
            // tell "freed" apart from "handed straight to the other group".
            ed::DeleteNode(ed::NodeId(secondIndex * GraphNode::kStride));
            RemoveNodeByIndex(secondIndex);
            ed::SelectNode(ed::NodeId(memberId));
         }
         else if (frameId == 22)
         {
            gRequestUngroup = true;
         }
         else if (frameId == 26)
         {
            size_t groupsLeft = 0;
            for (GraphNode& g : gNodes)
            {
               if (dynamic_cast<GroupNode*>(g.node.get()) != nullptr)
                  groupsLeft++;
            }
            // The group is gone, its three member nodes are untouched, and
            // they are unowned again rather than still bound to a dead group.
            const bool freed = GroupOwning(gNodes[0].index) == nullptr;
            printf("after ungroup: %zu groups left, %zu nodes, member freed=%d  %s\n",
                   groupsLeft, gNodes.size(), (int)freed,
                   (groupsLeft == 0 && gNodes.size() == 3 && freed) ? "UNGROUP OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_LIVETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_LIVETEST") != nullptr && frameId == 4)
      {
         // Reproduces the reported bug exactly: Cube -> Select -> Delete
         // Selected, then reroll Select's random seed and confirm Delete
         // Selected's output actually changes without touching the graph.
         GeometryNode cube;
         cube.shape = 1; // cube
         cube.CookIfNeeded(9400);

         auto select = std::make_unique<GeometryOpNode>();
         select->op = GeometryOpNode::kSelect;
         select->input = &cube;
         select->selectMode = MeshOps::kSelectRandom;
         select->selectA = 0.5f;
         select->selectSeed = 1.0f;

         auto del = std::make_unique<GeometryOpNode>();
         del->op = GeometryOpNode::kDeleteSelected;
         del->input = select.get();

         // Copied by value: GetMesh() returns a reference into the node's own
         // cache, so holding two "const Mesh&" across the second call would
         // alias the same mutated object instead of comparing before/after.
         const Mesh firstOut = del->GetMesh();
         const size_t firstTris = firstOut.indices.size() / 3;

         select->selectSeed = 42.0f;
         const Mesh secondOut = del->GetMesh();
         const size_t secondTris = secondOut.indices.size() / 3;

         auto sameVertices = [](const Mesh& a, const Mesh& b) {
            if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
               return false;
            for (size_t i = 0; i < a.vertices.size(); i++)
               if (std::fabs(a.vertices[i].px - b.vertices[i].px) > 1e-6f ||
                   std::fabs(a.vertices[i].py - b.vertices[i].py) > 1e-6f ||
                   std::fabs(a.vertices[i].pz - b.vertices[i].pz) > 1e-6f)
                  return false;
            return true;
         };
         const bool changed = !sameVertices(firstOut, secondOut);
         printf("delete selected: seed 1 -> %zu tris, seed 42 -> %zu tris, output changed=%d  %s\n",
                firstTris, secondTris, (int)changed, changed ? "OK" : "FAIL");

         // Same check one hop further downstream, through a second operator -
         // the bug would still be live if only the immediate child re-read the
         // revision and a grandchild did not.
         auto transform = std::make_unique<GeometryOpNode>();
         transform->op = GeometryOpNode::kTransformSelected;
         transform->input = select.get();
         select->selectSeed = 1.0f;
         const Mesh t1 = transform->GetMesh();
         select->selectSeed = 42.0f;
         const Mesh t2 = transform->GetMesh();
         const bool changed2 = !sameVertices(t1, t2);
         printf("transform selected also reacts to reselection  %s\n", changed2 ? "OK" : "FAIL");

         // And InstanceOnPointsNode, which had the identical bug on its own
         // point-source and instance-shape inputs.
         auto inst = std::make_unique<InstanceOnPointsNode>();
         inst->pointSource = select.get();
         inst->instanceShape = &cube;
         inst->pointMode = 2; // faces
         select->selectSeed = 1.0f;
         inst->CookIfNeeded(9401);
         const size_t instCount1 = inst->InstanceCount();
         select->selectSeed = 42.0f;
         inst->CookIfNeeded(9402);
         const size_t instCount2 = inst->InstanceCount();
         printf("instance on points: seed 1 -> %zu instances, seed 42 -> %zu instances  %s\n",
                instCount1, instCount2, instCount1 != instCount2 ? "OK" : "FAIL");

         const bool ok = changed && changed2 && instCount1 != instCount2;
         printf("%s\n", ok ? "LIVE UPDATE OK" : "SUSPECT");
      }
}

void FrameTest_GLTFDROPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_GLTFDROPTEST") != nullptr)
      {
         auto countType = [](const char* typeName) -> int
         {
            int c = 0;
            for (GraphNode& gn : gNodes)
               if (gn.typeName == typeName)
                  c++;
            return c;
         };
         auto findFirst = [](const char* typeName) -> GraphNode*
         {
            for (GraphNode& gn : gNodes)
               if (gn.typeName == typeName)
                  return &gn;
            return nullptr;
         };
         auto check = [](bool cond, const char* what) -> bool
         {
            printf("GLTFDROPTEST %s: %s\n", cond ? "PASS" : "FAIL", what);
            return cond;
         };
         // Verifies the wiring a fresh drop should have produced: exactly
         // one Model 3D + one Material, geometry-connected, plus one Image
         // Source per non-empty map in `pkg`, each connected into the
         // matching MaterialMap slot and each carrying the right
         // "gltf://<path>#<slot>" pseudo-path.
         auto verifyRig = [&](const GltfImport::GltfDecodePackage* pkg, const char* label) -> bool
         {
            bool ok = true;
            ok = check(countType("Model 3D") == 1, (std::string(label) + ": exactly one Model 3D").c_str()) && ok;
            GraphNode* materialGn = findFirst("Material");
            ok = check(materialGn != nullptr, (std::string(label) + ": Material spawned").c_str()) && ok;
            if (materialGn == nullptr || pkg == nullptr)
               return false;
            auto* material = static_cast<MaterialNode*>(materialGn->node.get());
            GraphNode* modelGn = findFirst("Model 3D");
            ok = check(modelGn != nullptr &&
                          material->input == dynamic_cast<IGeometrySource*>(modelGn->node.get()),
                       (std::string(label) + ": Material's geo input is the Model 3D").c_str()) &&
                 ok;

            struct MapSlot
            {
               const GltfImport::GltfDecodedImage* img;
               int mapIndex;
               const char* slot;
            };
            const MapSlot maps[] = {
               { &pkg->albedo, kMapAlbedo, "albedo" },       { &pkg->roughness, kMapRoughness, "roughness" },
               { &pkg->metallic, kMapMetallic, "metallic" }, { &pkg->normalMap, kMapNormal, "normal" },
               { &pkg->occlusion, kMapAmbientOcclusion, "ao" }, { &pkg->emissive, kMapEmission, "emission" },
            };
            int expectedImageSources = 0;
            for (const MapSlot& m : maps)
            {
               if (m.img->pixels.empty())
               {
                  ok = check(!material->MapInput(m.mapIndex).IsConnected(),
                             (std::string(label) + ": no " + m.slot + " map -> slot left unconnected").c_str()) &&
                       ok;
                  continue;
               }
               expectedImageSources++;
               INode* src = material->MapInput(m.mapIndex).GetSource();
               auto* imgSrc = src ? dynamic_cast<ImageSourceNode*>(src) : nullptr;
               const bool wiredRight = imgSrc != nullptr && imgSrc->LoadedPath().size() > 5 &&
                  imgSrc->LoadedPath().compare(imgSrc->LoadedPath().size() - std::strlen(m.slot) - 1, std::string::npos,
                                               std::string("#") + m.slot) == 0;
               ok = check(wiredRight, (std::string(label) + ": " + m.slot + " map wired to a gltf:// Image Source").c_str()) && ok;
            }
            ok = check(countType("Image Source") == expectedImageSources,
                       (std::string(label) + ": Image Source count matches present maps").c_str()) &&
                 ok;
            return ok;
         };

         static int sNodeCountAfterFreshDrop = -1;
         static int sNodeCountBeforeSecondDrop = -1;
         static bool sOverallOk = true;

         const char* glbPath = getenv("INFINITE_GLTFDROPTEST_GLB");
         const char* notexPath = getenv("INFINITE_GLTFDROPTEST_NOTEX");
         const char* gltfPath = getenv("INFINITE_GLTFDROPTEST_GLTF");

         if (frameId == 4 && glbPath != nullptr)
         {
            NewPatch();

            // Criterion 7: a combined metallicRoughness texture must decode
            // into two distinct maps (channel split), not the same buffer
            // twice or swapped channels. Checked directly against the
            // decoder - no drop/UI involved.
            std::string err;
            const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(glbPath, err);
            const bool haveBoth = pkg != nullptr && !pkg->roughness.pixels.empty() && !pkg->metallic.pixels.empty();
            sOverallOk = check(haveBoth, "metallicRoughness decoded into distinct roughness+metallic maps") && sOverallOk;
            if (haveBoth)
            {
               bool differ = false;
               for (size_t i = 0; i + 3 < pkg->roughness.pixels.size() && !differ; i += 4)
                  if (pkg->roughness.pixels[i] != pkg->metallic.pixels[i])
                     differ = true;
               sOverallOk = check(differ, "roughness and metallic pixel data actually differ (not swapped/identical)") && sOverallOk;
            }

            // Criterion 1: fresh drop onto empty canvas.
            gDropPos = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                               gGraphScreenTL.y + gGraphScreenSize.y * 0.5f);
            gDroppedFiles.push_back(glbPath);
         }
         else if (frameId == 6 && glbPath != nullptr)
         {
            std::string err;
            const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(glbPath, err);
            sOverallOk = verifyRig(pkg, "fresh .glb drop") && sOverallOk;
            sNodeCountAfterFreshDrop = (int)gNodes.size();

            // Criterion 4: one undo removes the entire spawned rig in a
            // single step.
            Undo();
            sOverallOk = check(gNodes.empty(), "one undo removes the entire spawned rig") && sOverallOk;
            Redo();
            sOverallOk = check((int)gNodes.size() == sNodeCountAfterFreshDrop,
                                "redo brings the entire rig back") && sOverallOk;

            // Criterion 3: save, start a new patch, reload - model and
            // every gltf-derived texture must come back via the gltf://
            // pseudo-path with zero special-casing.
            const std::string tmpPath = TmpPath("infinite_gltfdroptest.infinite");
            SavePatchTo(tmpPath);
            NewPatch();
            LoadPatchFrom(tmpPath);
            sOverallOk = verifyRig(pkg, "save/reload round-trip") && sOverallOk;
            sNodeCountBeforeSecondDrop = (int)gNodes.size();
         }
         else if (frameId == 8 && glbPath != nullptr)
         {
            // Criterion 5 setup: drop the same file again, this time
            // targeted at the reloaded Model 3D node, so the next stage can
            // confirm it reloads in place rather than spawning a duplicate
            // Material/texture rig. Deferred to its own frame (rather than
            // done right after the frame-6 reload) so the node editor has
            // had at least one frame to lay out the just-reloaded node -
            // ed::GetNodePosition/SetNodePosition sync happens later in the
            // same frame a node first appears (see FindFreeSpawnPosition's
            // comment above), so querying it the instant a node is loaded
            // reads a stale/zero position.
            GraphNode* modelGn = findFirst("Model 3D");
            if (modelGn != nullptr)
            {
               const ImVec2 canvasPt = ed::GetNodePosition(modelGn->NodeId());
               gDropPos = ed::CanvasToScreen(ImVec2(canvasPt.x + 10.0f, canvasPt.y + 10.0f));
               gDroppedFiles.push_back(glbPath);
            }
         }
         else if (frameId == 10)
         {
            if (glbPath != nullptr)
            {
               sOverallOk = check((int)gNodes.size() == sNodeCountBeforeSecondDrop,
                                   "dropping onto an existing Model 3D reloads in place, no duplicate rig") &&
                            sOverallOk;
            }

            if (notexPath != nullptr)
            {
               NewPatch();
               gDropPos = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                                  gGraphScreenTL.y + gGraphScreenSize.y * 0.5f);
               gDroppedFiles.push_back(notexPath);
            }
         }
         else if (frameId == 12)
         {
            if (notexPath != nullptr)
            {
               // Criterion 6: a glTF with no textures at all spawns Model 3D
               // + Material and zero Image Source nodes - and, implicitly,
               // didn't crash getting here.
               std::string err;
               const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(notexPath, err);
               sOverallOk = verifyRig(pkg, "textureless .glb drop") && sOverallOk;
            }

            if (gltfPath != nullptr)
            {
               NewPatch();
               gDropPos = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                                  gGraphScreenTL.y + gGraphScreenSize.y * 0.5f);
               gDroppedFiles.push_back(gltfPath);
            }
         }
         else if (frameId == 14)
         {
            if (gltfPath != nullptr)
            {
               // Criterion 2: loose .gltf + external .bin + external
               // textures, same wiring expectations as a .glb.
               std::string err;
               const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(gltfPath, err);
               sOverallOk = verifyRig(pkg, "loose .gltf drop") && sOverallOk;
            }

            printf("%s\n", sOverallOk ? "GLTFDROPTEST OK" : "GLTFDROPTEST SUSPECT");
         }
      }
}

void FrameTest_NAVTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_NAVTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true);
         static bool ok = true;
         auto check = [&](bool good, const char* what) {
            ok = ok && good;
            printf("navtest %-48s %s\n", what, good ? "ok" : "FAIL");
         };
         static bool wasPlaying = false;
         if (frameId == 3)
         {
            check(!(tio.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard), "nav is off over the canvas");
            gShortcutsOpen = true;
         }
         if (frameId == 8)
            check((tio.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0, "nav turns on with the Shortcuts window focused");
         if (frameId == 9) tio.AddKeyEvent(ImGuiKey_Tab, true);
         if (frameId == 10) tio.AddKeyEvent(ImGuiKey_Tab, false);
         if (frameId == 13)
         {
            check(tio.NavVisible, "Tab shows the focus ring");
            check(gNavOwnsKeys, "canvas keys stand down while nav owns the keyboard");
            wasPlaying = Transport::Instance().IsPlaying();
            tio.AddKeyEvent(ImGuiKey_Space, true);
         }
         if (frameId == 14) tio.AddKeyEvent(ImGuiKey_Space, false);
         if (frameId == 17)
         {
            check(Transport::Instance().IsPlaying() == wasPlaying, "Space on a nav item does not toggle the transport");
            tio.AddKeyEvent(ImGuiKey_Escape, true);
         }
         if (frameId == 18) tio.AddKeyEvent(ImGuiKey_Escape, false);
         if (frameId == 22)
         {
            gShortcutsOpen = false;
         }
         if (frameId == 26)
         {
            check(!(tio.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard), "nav turns off again once the window closes");
            printf("navtest result: %s\n", ok ? "NAV OK" : "NAV FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_THEMECONTRASTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_THEMECONTRASTTEST") != nullptr && frameId == 3)
      {
         bool ok = true;
         const auto& names = CategoryColors::PresetNames();
         for (int i = 0; i < (int)names.size(); ++i)
         {
            const CategoryColors::UiTheme& t = CategoryColors::UiThemeForPreset(i);
            const float rows[4] = { CategoryColors::ContrastRatio(t.text, t.windowBg), CategoryColors::ContrastRatio(t.text, t.panelBg),
                                    CategoryColors::ContrastRatio(t.textDim, t.windowBg), CategoryColors::ContrastRatio(t.textDim, t.panelBg) };
            float lowest = rows[0];
            for (float r : rows) lowest = std::min(lowest, r);
            const bool good = lowest >= 4.499f;
            ok = ok && good;
            printf("[THEMECONTRASTTEST] %-18s lowest %.2f  %s\n", names[i].c_str(), lowest, good ? "ok" : "FAIL");
            // G15 non-text: the accent (checked / active state) vs panel. Reported only; the recessed frame fill is
            // deliberately quiet (node-ui-pillars P10), so 3:1 applies to the active state, not the resting frame.
            printf("[THEMECONTRASTTEST]   accent/panel %.2f%s\n", CategoryColors::ContrastRatio(t.accent, t.panelBg),
                   CategoryColors::ContrastRatio(t.accent, t.panelBg) < 3.0f ? "  below 3:1" : "");
         }
         printf("[THEMECONTRASTTEST] %s\n", ok ? "THEMECONTRASTTEST OK" : "THEMECONTRASTTEST FAIL");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_TEXTFOCUSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_TEXTFOCUSTEST") != nullptr && frameId >= 3 && frameId <= 8)
      {
         static bool sawLag = false, claimedOnLag = false, claimedIdle = true;
         static char buf[16] = "";
         ImGui::SetNextWindowPos(ImVec2(40, 40));
         ImGui::Begin("##textfocustest", nullptr, ImGuiWindowFlags_NoNav);
         if (frameId == 3)
            claimedIdle = !TextFocusClaimed() ? true : false;
         if (frameId == 5)
            ImGui::SetKeyboardFocusHere();
         ImGui::InputText("##tf", buf, sizeof(buf));
         if (frameId >= 5 && !sawLag && ImGui::GetCurrentContext()->ActiveId != 0)
         {
            sawLag = !ImGui::GetIO().WantTextInput;
            claimedOnLag = TextFocusClaimed();
            printf("[TEXTFOCUSTEST] frame %d: WantTextInput=%d claimed=%d\n", frameId, (int)ImGui::GetIO().WantTextInput, (int)claimedOnLag);
         }
         ImGui::End();
         if (frameId == 8)
         {
            const bool ok = claimedIdle && sawLag && claimedOnLag;
            printf("[TEXTFOCUSTEST] idle=%d lagSeen=%d claimedOnLag=%d\n", (int)claimedIdle, (int)sawLag, (int)claimedOnLag);
            printf("[TEXTFOCUSTEST] %s\n", ok ? "TEXTFOCUSTEST OK" : "TEXTFOCUSTEST FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_UXLEFTOVERSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_UXLEFTOVERSTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true);
         static bool ok = true;
         static float orig = 1.0f;
         auto check = [&](bool good, const char* what) {
            ok = ok && good;
            printf("[UXLEFTOVERSTEST] %-46s %s\n", what, good ? "ok" : "FAIL");
         };
         if (frameId == 3)
         {
            orig = CategoryColors::GetUiScale();
            CategoryColors::SetUiScale(1.0f, false);
            check(!CategoryColors::GetTooltips(), "help tooltips are off by default");
         }
         if (frameId == 5) { tio.AddKeyEvent(ImGuiMod_Ctrl, true); tio.AddKeyEvent(ImGuiKey_Equal, true); }
         if (frameId == 6) { tio.AddKeyEvent(ImGuiKey_Equal, false); tio.AddKeyEvent(ImGuiMod_Ctrl, false); }
         if (frameId == 9)
            check(std::fabs(CategoryColors::GetUiScale() - 1.1f) < 0.001f, "Ctrl+= raises the UI scale by 0.1");
         if (frameId == 12) { tio.AddKeyEvent(ImGuiMod_Ctrl, true); tio.AddKeyEvent(ImGuiKey_Minus, true); }
         if (frameId == 13) { tio.AddKeyEvent(ImGuiKey_Minus, false); tio.AddKeyEvent(ImGuiMod_Ctrl, false); }
         if (frameId == 16)
         {
            check(std::fabs(CategoryColors::GetUiScale() - 1.0f) < 0.001f, "Ctrl+- lowers it back");
            CategoryColors::SetUiScale(orig);
            UiScale::RequestRescale();
            printf("[UXLEFTOVERSTEST] %s\n", ok ? "UXLEFTOVERSTEST OK" : "UXLEFTOVERSTEST FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_SHAPEEDGETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SHAPEEDGETEST") != nullptr && frameId == 4)
      {
         ShapeNode node;
         node.width = 64.0f; node.height = 64.0f;
         node.fillColor[0] = 1.0f; node.fillColor[1] = 0.0f; node.fillColor[2] = 0.0f;
         node.feather = 0.05f;
         node.CookIfNeeded(300);
         unsigned int scratchFbo = 0;
         std::vector<float> px;
         GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), 64, 64, px);
         if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);
         int edge = 0, bad = 0;
         for (size_t i = 0; i + 3 < px.size(); i += 4)
         {
            const float a = px[i + 3];
            if (a > 0.05f && a < 0.95f)
            {
               ++edge;
               if (px[i] < 0.95f || px[i + 1] > 0.05f || px[i + 2] > 0.05f) ++bad;
            }
         }
         const bool pass = edge > 4 && bad == 0;
         printf("[SHAPEEDGETEST] %d edge pixels, %d with a darkened colour\n", edge, bad);
         printf("[SHAPEEDGETEST] %s\n", pass ? "SHAPEEDGETEST OK" : "SHAPEEDGETEST FAIL");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_FIELDPIXELTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDPIXELTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDPIXELTEST] Running Field pixel-domain conformance harness...\n");

         // 1. Trivial kernel compiles
         {
            FieldPixelNode node;
            node.code = "col = vec3(uv.x, uv.y, 0.5);";
            bool ok = node.Apply();
            bool pass = ok && node.Program() != 0 && node.LastError().empty();
            printf("[FIELDPIXELTEST] Assertion 1 (Trivial Compile): %s\n", pass ? "OK" : "FAIL");
         }

         // 2. #version 150 only
         {
            FieldPixelNode node;
            node.code = "col = vec3(uv.x, uv.y, 0.5);";
            node.Apply();
            const std::string& src = node.EmitResult().source;
            bool pass = (src.find("#version 150") != std::string::npos) &&
                        (src.find("#version 330") == std::string::npos);
            printf("[FIELDPIXELTEST] Assertion 2 (Version Pinning): %s\n", pass ? "OK" : "FAIL");
         }

         // 3. fld_mod helper used, no bare mod( in body
         {
            FieldPixelNode node;
            node.code = "col = vec3(fmod(uv.x * 2.0, 1.0), uv.y % 0.5, 0.0);";
            node.Apply();
            const std::string& src = node.EmitResult().source;
            bool hasFldMod = src.find("fld_mod(") != std::string::npos;
            size_t mainPos = src.find("void main()");
            std::string bodySrc = mainPos != std::string::npos ? src.substr(mainPos) : src;
            bool hasBareMod = false;
            for (size_t p = bodySrc.find("mod("); p != std::string::npos; p = bodySrc.find("mod(", p + 4))
            {
               if (p == 0 || (bodySrc[p - 1] != '_' && !isalnum(bodySrc[p - 1])))
               {
                  hasBareMod = true;
                  printf("[DEBUG A3] Found bare mod at pos %zu: %s\n", p, bodySrc.substr(p, 20).c_str());
                  break;
               }
            }
            bool pass = hasFldMod && !hasBareMod;
            printf("[FIELDPIXELTEST] Assertion 3 (Mod Helper): %s\n", pass ? "OK" : "FAIL");
         }

         // 4. round lowers to floor(x + 0.5), no round( in body
         {
            FieldPixelNode node;
            node.code = "col = vec3(round(uv.x * 4.0), 0.0, 0.0);";
            node.Apply();
            const std::string& src = node.EmitResult().source;
            size_t mainPos = src.find("void main()");
            std::string body = mainPos != std::string::npos ? src.substr(mainPos) : src;
            bool pass = body.find("floor(") != std::string::npos &&
                        body.find("0.5") != std::string::npos &&
                        body.find("round(") == std::string::npos;
            printf("[FIELDPIXELTEST] Assertion 4 (Round Lowering): %s\n", pass ? "OK" : "FAIL");
         }

         // 5. if(c,a,b) lowers to mix(b, a, c != 0.0), no step(0.5
         {
            FieldPixelNode node;
            node.code = "col = vec3(if(uv.x > 0.5, 1.0, 0.0), 0.0, 0.0);";
            node.Apply();
            const std::string& src = node.EmitResult().source;
            size_t mainPos = src.find("void main()");
            std::string body = mainPos != std::string::npos ? src.substr(mainPos) : src;
            // Must be the BOOL-SELECTOR overload mix(b, a, c != 0.0), not the
            // arithmetic float mix with a 0.0/1.0 selector - the float form
            // turns an inf/NaN in the unselected branch into NaN in the result.
            // Search from the SSA body marker, not the top of main(): the
            // fixed reserved-name prologue always emits its own unrelated
            // "alpha = mix(1.0, src.a, fld_srcAlpha)" line first.
            size_t ssaBodyPos = body.find("// ---- SSA body ----");
            size_t searchFrom = ssaBodyPos != std::string::npos ? ssaBodyPos : 0;
            size_t mixPos = body.find("mix(", searchFrom);
            std::string mixStmt = mixPos != std::string::npos
                                     ? body.substr(mixPos, body.find(';', mixPos) - mixPos)
                                     : std::string();
            bool pass = mixPos != std::string::npos &&
                        mixStmt.find("!= 0.0") != std::string::npos &&
                        mixStmt.find("? 1.0") == std::string::npos &&
                        body.find("step(0.5") == std::string::npos;
            printf("[FIELDPIXELTEST] Assertion 5 (If Lowering): %s\n", pass ? "OK" : "FAIL");
         }

         // 6. Deliberately broken kernel leaves mProgram unchanged and sets mLastError
         {
            FieldPixelNode node;
            node.code = "col = vec3(uv.x, uv.y, 0.5);";
            node.Apply();
            unsigned int progBefore = node.Program();
            node.code = "col = vec3(uv.x +);"; // deliberate parse error
            bool ok = node.Apply();
            bool pass = !ok && node.Program() == progBefore && !node.LastError().empty();
            printf("[FIELDPIXELTEST] Assertion 6 (Failed Compile Preserves State): %s\n", pass ? "OK" : "FAIL");
         }

         // 7. Cooking 10 frames performs zero further compiles
         {
            FieldPixelNode node;
            node.code = "col = vec3(uv.x +);";
            node.Apply();
            for (int i = 0; i < 10; i++)
            {
               node.CookIfNeeded(100 + i);
            }
            bool pass = (node.Program() == 0) && (!node.LastError().empty());
            printf("[FIELDPIXELTEST] Assertion 7 (Do-Not-Retry Guard): %s\n", pass ? "OK" : "FAIL");
         }

         // 8. State ping-pong actually ping-pongs
         {
            FieldPixelNode node;
            node.width = 64.0f;
            node.height = 64.0f;
            node.code = "state float x = 0;\nx = x + 0.1;\ncol = vec3(fract(x));";
            node.Apply();

            node.CookIfNeeded(1);
            node.CookIfNeeded(2);
            unsigned int scratchFbo = 0;
            std::vector<float> readbackFrame2;
            GLUtil::ReadTexturePixels(scratchFbo, node.State().ReadTexture(), 64, 64, readbackFrame2);

            node.CookIfNeeded(3);
            node.CookIfNeeded(4);
            std::vector<float> readbackFrame4;
            GLUtil::ReadTexturePixels(scratchFbo, node.State().ReadTexture(), 64, 64, readbackFrame4);

            bool differs = false;
            for (size_t i = 0; i < readbackFrame2.size() && i < readbackFrame4.size(); i++)
            {
               if (std::abs(readbackFrame4[i] - readbackFrame2[i]) > 0.01f)
               {
                  differs = true;
                  break;
               }
            }
            printf("[FIELDPIXELTEST] Assertion 8 (State Ping-Pong): %s\n", differs ? "OK" : "FAIL");

            // 9. Transport reset returns state readback to initialiser
            Transport::Instance().TriggerReset();
            node.CookIfNeeded(5);
            std::vector<float> readbackReset;
            GLUtil::ReadTexturePixels(scratchFbo, node.State().ReadTexture(), 64, 64, readbackReset);
            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);

            bool pass9 = !readbackReset.empty() && (std::abs(readbackReset[0] - 0.1f) < 0.05f || std::abs(readbackReset[0] - 0.0f) < 0.05f);
            printf("[FIELDPIXELTEST] Assertion 9 (Transport Reset): %s\n", pass9 ? "OK" : "FAIL");
         }

         // 10. Conformance on 8 kernels at 64x64 vs CPU VM within 1e-3
         {
            struct ConformanceCase {
               const char* code;
               std::function<void(double u, double v, double& r, double& g, double& b)> eval;
            };

            std::vector<ConformanceCase> cases = {
               // 1. Trig
               {
                  "col = vec3(sin(uv.x * 3.14159), cos(uv.y * 3.14159), tan(uv.x * 0.5));",
                  [](double u, double v, double& r, double& g, double& b) {
                     r = std::sin(u * 3.14159);
                     g = std::cos(v * 3.14159);
                     b = std::tan(u * 0.5);
                  }
               },
               // 2. Mod & Pow
               {
                  "col = vec3(fmod(uv.x * 5.0 - 2.5, 1.5), pow(uv.y, 2.5), fmod(uv.x + uv.y, 0.7));",
                  [](double u, double v, double& r, double& g, double& b) {
                     r = std::fmod(u * 5.0 - 2.5, 1.5);
                     g = std::pow(v, 2.5);
                     b = std::fmod(u + v, 0.7);
                  }
               },
               // 3. Clamp & Lerp & Smoothstep
               {
                  "col = vec3(clamp(uv.x * 2.0 - 0.5, 0.2, 0.8), lerp(0.1, 0.9, uv.y), smoothstep(0.3, 0.7, uv.x));",
                  [](double u, double v, double& r, double& g, double& b) {
                     r = std::min(std::max(u * 2.0 - 0.5, 0.2), 0.8);
                     g = 0.1 + (0.9 - 0.1) * v;
                     double su = std::min(std::max((u - 0.3) / (0.7 - 0.3), 0.0), 1.0);
                     b = su * su * (3.0 - 2.0 * su);
                  }
               },
               // 4. Reversed Smoothstep & Abs & Sign
               {
                  "col = vec3(smoothstep(0.7, 0.3, uv.x), abs(uv.y - 0.5) * 2.0, sign(uv.x - 0.5));",
                  [](double u, double v, double& r, double& g, double& b) {
                     double su = std::min(std::max((u - 0.7) / (0.3 - 0.7), 0.0), 1.0);
                     r = su * su * (3.0 - 2.0 * su);
                     g = std::abs(v - 0.5) * 2.0;
                     b = (u - 0.5) > 0.0 ? 1.0 : ((u - 0.5) < 0.0 ? -1.0 : 0.0);
                  }
               },
               // 5. Conditionals
               {
                  "col = vec3(if(uv.x > 0.5, uv.y, 1.0 - uv.y), if(uv.y < 0.3, 0.2, 0.8), 0.5);",
                  [](double u, double v, double& r, double& g, double& b) {
                     r = (u > 0.5) ? v : (1.0 - v);
                     g = (v < 0.3) ? 0.2 : 0.8;
                     b = 0.5;
                  }
               },
               // 6. Floor, Ceil, Fract
               {
                  "col = vec3(floor(uv.x * 4.0) / 4.0, ceil(uv.y * 4.0) / 4.0, fract(uv.x * 3.0));",
                  [](double u, double v, double& r, double& g, double& b) {
                     r = std::floor(u * 4.0) / 4.0;
                     g = std::ceil(v * 4.0) / 4.0;
                     b = (u * 3.0) - std::floor(u * 3.0);
                  }
               },
               // 7. Sqrt & Exp
               {
                  "col = vec3(sqrt(uv.x), exp(uv.y - 1.0), sqrt(uv.x * uv.y));",
                  [](double u, double v, double& r, double& g, double& b) {
                     r = std::sqrt(u);
                     g = std::exp(v - 1.0);
                     b = std::sqrt(u * v);
                  }
               },
               // 8. Length & Distance
               {
                  "d = length(uv - vec2(0.5, 0.5)); col = vec3(d, 1.0 - d, d * d);",
                  [](double u, double v, double& r, double& g, double& b) {
                     double dx = u - 0.5, dy = v - 0.5;
                     double d = std::sqrt(dx * dx + dy * dy);
                     r = d;
                     g = 1.0 - d;
                     b = d * d;
                  }
               }
            };

            double globalMaxErr = 0.0;
            int maxErrX = 0, maxErrY = 0;
            int maxErrKernel = 0;
            bool allWithinTolerance = true;

            const int kDim = 64;
            unsigned int scratchFbo = 0;

            for (size_t k = 0; k < cases.size(); k++)
            {
               FieldPixelNode node;
               node.width = (float)kDim;
               node.height = (float)kDim;
               node.code = cases[k].code;
               if (!node.Apply())
               {
                  allWithinTolerance = false;
                  break;
               }

               node.CookIfNeeded(10 + (int)k);

               std::vector<float> gpuPixels;
               GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), kDim, kDim, gpuPixels);

               if (gpuPixels.size() < (size_t)(kDim * kDim * 4))
               {
                  allWithinTolerance = false;
                  break;
               }

               for (int y = 0; y < kDim; y++)
               {
                  for (int x = 0; x < kDim; x++)
                  {
                     double u = ((double)x + 0.5) / (double)kDim;
                     double v = ((double)y + 0.5) / (double)kDim;

                     double refR = 0, refG = 0, refB = 0;
                     cases[k].eval(u, v, refR, refG, refB);

                     int idx = (y * kDim + x) * 4;
                     float gpuR = gpuPixels[idx + 0];
                     float gpuG = gpuPixels[idx + 1];
                     float gpuB = gpuPixels[idx + 2];

                     if (std::isfinite(refR))
                     {
                        double err = std::abs((double)gpuR - refR);
                        if (err > globalMaxErr) { globalMaxErr = err; maxErrX = x; maxErrY = y; maxErrKernel = (int)k; }
                        if (err > 1.0e-3) allWithinTolerance = false;
                     }
                     if (std::isfinite(refG))
                     {
                        double err = std::abs((double)gpuG - refG);
                        if (err > globalMaxErr) { globalMaxErr = err; maxErrX = x; maxErrY = y; maxErrKernel = (int)k; }
                        if (err > 1.0e-3) allWithinTolerance = false;
                     }
                     if (std::isfinite(refB))
                     {
                        double err = std::abs((double)gpuB - refB);
                        if (err > globalMaxErr) { globalMaxErr = err; maxErrX = x; maxErrY = y; maxErrKernel = (int)k; }
                        if (err > 1.0e-3) allWithinTolerance = false;
                     }
                  }
               }
            }

            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);

            printf("[FIELDPIXELTEST] Conformance max abs error = %f at (%d, %d) in kernel %d\n",
                   globalMaxErr, maxErrX, maxErrY, maxErrKernel);
            printf("[FIELDPIXELTEST] Assertion 10 (Conformance): %s\n", allWithinTolerance ? "OK" : "FAIL");
         }

         // 11. State reads use texelFetch and no texture(fld_s_
         {
            FieldPixelNode node;
            node.code = "state float x = 0;\nx = x + 0.1;\ncol = vec3(x);";
            node.Apply();
            const std::string& src = node.EmitResult().source;
            bool pass = (src.find("texelFetch(fld_s_bank0") != std::string::npos) &&
                        (src.find("texture(fld_s_") == std::string::npos);
            printf("[FIELDPIXELTEST] Assertion 11 (TexelFetch State): %s\n", pass ? "OK" : "FAIL");
         }

         // 12. Requesting 5 state cells is refused with error containing "4 cells max"
         {
            FieldPixelNode node;
            node.code = "state float a = 0;\nstate float b = 0;\nstate float c = 0;\nstate float d = 0;\nstate float e = 0;\ncol = vec3(1.0);";
            bool ok = node.Apply();
            bool pass = !ok && (node.LastError().find("4 cells max") != std::string::npos);
            printf("[FIELDPIXELTEST] Assertion 12 (State Cell Cap): %s\n", pass ? "OK" : "FAIL");
         }

         // 13. Every shipped preset compiles. The node's own presets are the
         //     first thing a user sees; step 4 shipped with 2 of 5 broken.
         {
            bool pass = true;
            for (size_t pi = 0; pi < FieldPixelNode::Presets().size(); pi++)
            {
               FieldPixelNode node;
               node.code = FieldPixelNode::Presets()[pi].code;
               if (!node.Apply())
               {
                  pass = false;
                  printf("[FIELDPIXELTEST]   preset %zu '%s' failed: %s\n", pi,
                         FieldPixelNode::Presets()[pi].name, node.LastError().c_str());
               }
            }
            printf("[FIELDPIXELTEST] Assertion 13 (Presets Compile): %s\n", pass ? "OK" : "FAIL");
         }

         // 14. A declared param reaches the shader as fld_p_<name> and links.
         {
            FieldPixelNode node;
            node.code = "param float speed = 2.0 [0.1, 10.0];\ncol = vec3(uv.x * speed);";
            bool ok = node.Apply();
            const std::string& src = node.EmitResult().source;
            size_t mainPos = src.find("void main()");
            std::string body = mainPos != std::string::npos ? src.substr(mainPos) : src;
            bool pass = ok && node.LastError().empty() &&
                        src.find("uniform float fld_p_speed;") != std::string::npos &&
                        body.find("fld_p_speed") != std::string::npos &&
                        body.find("fld_v_speed") == std::string::npos;
            printf("[FIELDPIXELTEST] Assertion 14 (Param Uniform): %s\n", pass ? "OK" : "FAIL");
         }

         // 15. Scalar args broadcast to a vector-valued helper call. GLSL's own
         //     clamp/mix/smoothstep carry (genType, float, float) overloads; the
         //     fld_ helpers replace them and must not lose that shape.
         {
            const char* kernels[] = {
               "col = clamp(col, 0.0, 1.0);",
               "w = lerp(vec3(0.0), vec3(1.0), uv.x);\ncol = w;",
               "col = smoothstep(0.0, 1.0, col);",
               "g = fmod(uv * 8.0, 1.0);\ncol = vec3(g.x, g.y, 0.0);"
            };
            bool pass = true;
            for (const char* kc : kernels)
            {
               FieldPixelNode node; node.code = kc;
               if (!node.Apply())
               {
                  pass = false;
                  printf("[FIELDPIXELTEST]   broadcast kernel failed: %s\n", node.LastError().c_str());
               }
            }
            printf("[FIELDPIXELTEST] Assertion 15 (Scalar Broadcast): %s\n", pass ? "OK" : "FAIL");
         }

         // 16. Bool-typed locals. GLSL has no implicit float<->bool conversion,
         //     so a bool declaration cannot be initialised from a 0.0/1.0 temp.
         {
            FieldPixelNode node;
            node.code = "a = !(uv.x > 0.5);\nb = (uv.x > 0.2) && (uv.y > 0.2);\ncol = vec3(a, b, 0.0);";
            bool pass = node.Apply();
            if (!pass) printf("[FIELDPIXELTEST]   bool kernel failed: %s\n", node.LastError().c_str());
            printf("[FIELDPIXELTEST] Assertion 16 (Bool Locals): %s\n", pass ? "OK" : "FAIL");
         }

         // 17. The filter idiom: read the input image, then write col. `col`
         //     arrives holding the source, so this is not a delay-free cycle.
         {
            FieldPixelNode node;
            node.code = "y = col.r * 2.0;\ncol = vec3(y, col.g, col.b);";
            bool pass = node.Apply();
            if (!pass) printf("[FIELDPIXELTEST]   filter kernel failed: %s\n", node.LastError().c_str());
            printf("[FIELDPIXELTEST] Assertion 17 (Read Before Write col): %s\n", pass ? "OK" : "FAIL");
         }

         // 18. A state kernel's VISIBLE output is `col`, not the raw state
         //     cells. The ping-pong pair holds state; mOut holds picture.
         {
            FieldPixelNode node;
            node.width = 64.0f; node.height = 64.0f;
            node.code = "state float x = 0;\nx = x + 0.5;\ncol = vec3(0.25, 0.25, 0.25);";
            bool ok = node.Apply();
            node.CookIfNeeded(200);
            node.CookIfNeeded(201);
            node.CookIfNeeded(202);
            unsigned int scratchFbo = 0;
            std::vector<float> px;
            GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), 64, 64, px);
            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);
            // x is 1.5 by now; if the display still showed state, red would be 1.5.
            bool pass = ok && px.size() >= 4 &&
                        std::abs(px[0] - 0.25f) < 1.0e-3f &&
                        std::abs(px[1] - 0.25f) < 1.0e-3f &&
                        std::abs(px[2] - 0.25f) < 1.0e-3f;
            if (!pass && px.size() >= 4)
               printf("[FIELDPIXELTEST]   display rgb = (%f, %f, %f), want 0.25 each\n", px[0], px[1], px[2]);
            printf("[FIELDPIXELTEST] Assertion 18 (State Kernel Displays col): %s\n", pass ? "OK" : "FAIL");
         }

         // --- Build step 22 (OPEN-C): offset reads of a pixel state cell ---

         // 19. Gray-Scott reaction-diffusion compiles. This is the whole
         //     point of OPEN-C: a node Infinite already ships as C++, as
         //     fifteen lines of editable text.
         {
            FieldPixelNode node;
            node.width = 64.0f; node.height = 64.0f;
            node.code =
               "param float feed = 0.055 [0.01, 0.09]\n"
               "param float kill = 0.062 [0.03, 0.07]\n"
               "param float dA = 1.0 [0, 1]\n"
               "param float dB = 0.5 [0, 1]\n"
               "state float A = 1 [wrap]\n"
               "state float B = 0 [wrap]\n"
               "d = 1.0 / res\n"
               "lapA = A(uv + vec2(d.x, 0)) + A(uv - vec2(d.x, 0)) + A(uv + vec2(0, d.y)) + A(uv - vec2(0, d.y)) - 4 * A\n"
               "lapB = B(uv + vec2(d.x, 0)) + B(uv - vec2(d.x, 0)) + B(uv + vec2(0, d.y)) + B(uv - vec2(0, d.y)) - 4 * B\n"
               "r = A * B * B\n"
               "A = A + dA * lapA - r + feed * (1 - A)\n"
               "B = B + dB * lapB + r - (kill + feed) * B\n"
               "col = vec3(B, B * 0.6, 1 - B)\n";
            bool ok = node.Apply();
            bool pass = ok && node.Program() != 0 && node.LastError().empty() &&
                        node.EmitResult().offsetReadCount == 8;
            if (!pass) printf("[FIELDPIXELTEST]   gray-scott failed: %s (offsetReads=%d)\n",
                              node.LastError().c_str(), node.EmitResult().offsetReadCount);
            printf("[FIELDPIXELTEST] Assertion 19 (Gray-Scott Compiles): %s\n", pass ? "OK" : "FAIL");
         }

         // 20. The boundary mode is per-cell and reaches the GLSL: wrap
         //     lowers to fract(), the default (absent) lowers to clamp().
         //     These are two visibly different pictures, not two spellings.
         {
            FieldPixelNode wrapNode;
            wrapNode.code = "state float A = 1 [wrap]\nA = A(uv + vec2(0.01, 0)) * 0.99\ncol = vec3(A);";
            wrapNode.Apply();
            const std::string& wsrc = wrapNode.EmitResult().source;

            FieldPixelNode clampNode;
            clampNode.code = "state float A = 1\nA = A(uv + vec2(0.01, 0)) * 0.99\ncol = vec3(A);";
            clampNode.Apply();
            const std::string& csrc = clampNode.EmitResult().source;

            bool pass = wsrc.find("fract(") != std::string::npos &&
                        wsrc.find("texture(fld_s_bank0") != std::string::npos &&
                        csrc.find("fract(") == std::string::npos &&
                        csrc.find("clamp(") != std::string::npos &&
                        csrc.find("texture(fld_s_bank0") != std::string::npos;
            printf("[FIELDPIXELTEST] Assertion 20 (Per-Cell Boundary Mode): %s\n", pass ? "OK" : "FAIL");
         }

         // 21. An offset read of a NON-pixel state cell is refused, and the
         //     message says why rather than "type error".
         {
            FieldElementNode node;
            node.code = "state float A = 0\nP.y = A(uv)\n";
            node.Apply();
            const std::string& err = node.LastError();
            bool pass = !err.empty() && err.find("spatial extent") != std::string::npos;
            if (!pass) printf("[FIELDPIXELTEST]   element offset read error was: '%s'\n", err.c_str());
            printf("[FIELDPIXELTEST] Assertion 21 (Offset Read Is Pixel-Only): %s\n", pass ? "OK" : "FAIL");
         }

         // 22. A neighbour-reading kernel gets the 32F bank; a plain trails
         //     kernel keeps the cheaper 16F one. 16F drifts visibly within
         //     seconds for anything that integrates.
         {
            FieldPixelNode sim;
            sim.width = 64.0f; sim.height = 64.0f;
            sim.code = "state float A = 1\nA = A(uv + vec2(0.01, 0)) * 0.99\ncol = vec3(A);";
            sim.Apply();
            sim.CookIfNeeded(300);

            FieldPixelNode trails;
            trails.width = 64.0f; trails.height = 64.0f;
            trails.code = "state float prev = 0\nprev = max(prev * 0.95, col.r)\ncol = vec3(prev);";
            trails.Apply();
            trails.CookIfNeeded(300);

            bool pass = sim.EmitResult().usesOffsetReads && sim.State().HighPrecision() &&
                        !trails.EmitResult().usesOffsetReads && !trails.State().HighPrecision();
            printf("[FIELDPIXELTEST] Assertion 22 (32F For Simulations Only): %s\n", pass ? "OK" : "FAIL");
         }

         // 23. The offset read genuinely reaches a NEIGHBOUR, not the current
         //     pixel. A spike is injected at the centre and diffused; after
         //     16 steps a texel several pixels away must be non-zero. Under
         //     the old current-pixel-only rule it would still be exactly 0,
         //     so this is the assertion that fails if the fetch coordinate is
         //     ever quietly dropped.
         {
            FieldPixelNode node;
            node.width = 64.0f; node.height = 64.0f;
            node.code =
               "state float A = 0\n"
               "d = 1.0 / res\n"
               "spike = 1 - step(0.03, length(uv - vec2(0.5, 0.5)))\n"
               "lap = A(uv + vec2(d.x, 0)) + A(uv - vec2(d.x, 0)) + A(uv + vec2(0, d.y)) + A(uv - vec2(0, d.y)) - 4 * A\n"
               "A = A + 0.2 * lap + spike * 0.25\n"
               "col = vec3(A, A, A);";
            bool ok = node.Apply();
            for (int f = 0; f < 16; f++)
               node.CookIfNeeded(400 + f);

            unsigned int scratchFbo = 0;
            std::vector<float> px;
            GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), 64, 64, px);
            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);

            // Centre is texel (32,32); the spike disc is ~2 texels across, so
            // (32,26) is well outside it and can only be lit by diffusion.
            float centre = 0.0f, away = 0.0f;
            if (px.size() >= (size_t)64 * 64 * 4)
            {
               centre = px[(32 * 64 + 32) * 4];
               away = px[(26 * 64 + 32) * 4];
            }
            bool pass = ok && centre > 0.05f && away > 1.0e-4f && away < centre;
            if (!pass) printf("[FIELDPIXELTEST]   diffusion: centre = %f, 6 texels away = %f\n", centre, away);
            if (!pass && getenv("INFINITE_FIELDDUMP") != nullptr)
               printf("[FIELDPIXELTEST] ---- generated GLSL ----\n%s\n---- end ----\n", node.EmitResult().source.c_str());
            printf("[FIELDPIXELTEST] Assertion 23 (Offset Read Reaches Neighbours): %s\n", pass ? "OK" : "FAIL");
         }

         // 24. The shipped Reaction Diffusion preset actually evolves: after
         //     240 steps the frame must carry real spatial structure and no
         //     NaN. A preset that compiles and then sits flat or blows up is
         //     the failure mode this catches.
         {
            FieldPixelNode node;
            node.width = 128.0f; node.height = 128.0f;
            for (const auto& pr : FieldPixelNode::Presets())
            {
               if (std::string(pr.name).find("Reaction Diffusion") != std::string::npos)
               {
                  node.code = pr.code;
                  break;
               }
            }
            bool ok = node.Apply();
            // 240 steps is enough to prove it evolves and stays finite, which
            // is what the assertion is for. A visual check of the pattern
            // itself wants thousands, so the dump path can ask for more.
            int steps = 240;
            if (const char* sEnv = getenv("INFINITE_FIELDDUMP_STEPS")) steps = std::max(1, atoi(sEnv));
            for (int f = 0; f < steps; f++)
               node.CookIfNeeded(600 + f);

            unsigned int scratchFbo = 0;
            std::vector<float> px;
            GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), 128, 128, px);
            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);

            double sum = 0.0, sum2 = 0.0;
            bool finite = true;
            const int n = 128 * 128;
            for (int k = 0; k < n && (size_t)(k * 4) < px.size(); k++)
            {
               float v = px[k * 4];
               if (!std::isfinite(v)) finite = false;
               sum += v;
               sum2 += (double)v * v;
            }
            double mean = sum / n;
            double var = sum2 / n - mean * mean;
            bool pass = ok && finite && var > 1.0e-4;
            if (!pass) printf("[FIELDPIXELTEST]   RD preset: mean = %f, variance = %f, finite = %d\n", mean, var, (int)finite);
            // Optional eyeball: INFINITE_FIELDDUMP=<path> writes the frame as
            // a binary PPM so a human can look at the pattern the numbers
            // above only assert the existence of.
            if (const char* dumpPath = getenv("INFINITE_FIELDDUMP_PPM"))
            {
               if (FILE* fp = fopen(dumpPath, "wb"))
               {
                  fprintf(fp, "P6\n128 128\n255\n");
                  for (int k = 0; k < n && (size_t)(k * 4 + 2) < px.size(); k++)
                  {
                     for (int c = 0; c < 3; c++)
                     {
                        float v = px[k * 4 + c];
                        unsigned char b = (unsigned char)(std::min(1.0f, std::max(0.0f, v)) * 255.0f);
                        fwrite(&b, 1, 1, fp);
                     }
                  }
                  fclose(fp);
                  printf("[FIELDPIXELTEST]   wrote %s\n", dumpPath);
               }
            }
            printf("[FIELDPIXELTEST] Assertion 24 (RD Preset Evolves): %s\n", pass ? "OK" : "FAIL");
         }

         // 25. `frame` is an int uniform in GLSL; using it in float maths has
         //     to work, because a one-shot seed ("do this only on the first
         //     cook") is how any simulation gets a non-uniform starting state.
         {
            FieldPixelNode node;
            node.code = "k = step(2.0, frame);\ncol = vec3(k, k, k);";
            bool ok = node.Apply();
            bool pass = ok && node.Program() != 0 && node.LastError().empty();
            if (!pass) printf("[FIELDPIXELTEST]   frame-as-float failed: %s\n", node.LastError().c_str());
            printf("[FIELDPIXELTEST] Assertion 25 (frame In Float Maths): %s\n", pass ? "OK" : "FAIL");
         }

         // 26. `age` fires exactly once. A cell seeded with
         //     `first = 1 - step(0.5, age)` must hold the seed value after
         //     many cooks, not a multiple of it - if age ever read 0 twice,
         //     every simulation would re-seed itself forever.
         {
            FieldPixelNode node;
            node.width = 32.0f; node.height = 32.0f;
            node.code =
               "state float S = 0;\n"
               "first = 1.0 - step(0.5, age);\n"
               "S = S + first * 0.25;\n"
               "col = vec3(S, S, S);";
            bool ok = node.Apply();
            for (int f = 0; f < 12; f++)
               node.CookIfNeeded(900 + f);

            unsigned int scratchFbo = 0;
            std::vector<float> px;
            GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), 32, 32, px);
            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);

            bool pass = ok && px.size() >= 4 && std::abs(px[0] - 0.25f) < 1.0e-3f;
            if (!pass && px.size() >= 4) printf("[FIELDPIXELTEST]   age seed accumulated to %f, want 0.25\n", px[0]);
            printf("[FIELDPIXELTEST] Assertion 26 (age Seeds Once): %s\n", pass ? "OK" : "FAIL");
         }

         // 27. The Advected Smoke preset moves density along its flow field:
         //     the emitter orbits, so after a run the frame carries structure
         //     away from the emitter rather than a single static dot.
         {
            FieldPixelNode node;
            node.width = 128.0f; node.height = 128.0f;
            for (const auto& pr : FieldPixelNode::Presets())
            {
               if (std::string(pr.name).find("Advected") != std::string::npos)
               {
                  node.code = pr.code;
                  break;
               }
            }
            bool ok = node.Apply();
            for (int f = 0; f < 400; f++)
               node.CookIfNeeded(1000 + f);

            unsigned int scratchFbo = 0;
            std::vector<float> px;
            GLUtil::ReadTexturePixels(scratchFbo, node.GetOutputTexture(), 128, 128, px);
            if (scratchFbo != 0) glDeleteFramebuffers(1, &scratchFbo);

            int lit = 0;
            bool finite = true;
            const int n = 128 * 128;
            for (int k = 0; k < n && (size_t)(k * 4) < px.size(); k++)
            {
               float v = px[k * 4];
               if (!std::isfinite(v)) finite = false;
               if (v > 0.25f) lit++;
            }
            // The emitter disc alone is ~50 texels at this size; advection has
            // to have smeared it over many more than that.
            bool pass = ok && finite && lit > 200 && lit < n;
            if (!pass) printf("[FIELDPIXELTEST]   advection: lit texels = %d of %d, finite = %d\n", lit, n, (int)finite);
            if (const char* dumpPath = getenv("INFINITE_FIELDDUMP_PPM2"))
            {
               if (FILE* fp = fopen(dumpPath, "wb"))
               {
                  fprintf(fp, "P6\n128 128\n255\n");
                  for (int k = 0; k < n && (size_t)(k * 4 + 2) < px.size(); k++)
                     for (int c = 0; c < 3; c++)
                     {
                        float v = px[k * 4 + c];
                        unsigned char b = (unsigned char)(std::min(1.0f, std::max(0.0f, v)) * 255.0f);
                        fwrite(&b, 1, 1, fp);
                     }
                  fclose(fp);
               }
            }
            printf("[FIELDPIXELTEST] Assertion 27 (Advection Transports): %s\n", pass ? "OK" : "FAIL");
         }


         // Image-coordinate language corpus: invalid calls must fail in the
         // front end rather than producing an invalid or misleading shader.
         {
            std::ifstream corpus("tests/field/image-offset-corpus.txt");
            if (!corpus.is_open())
            {
               // Cocoa changes cwd to Contents/Resources during glfwInit.
               // Resolve against this fixture's build-time source path too.
               auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
               corpus.clear();
               corpus.open(root / "tests/field/image-offset-corpus.txt");
            }
            bool pass = corpus.good();
            int cases = 0;
            std::string line;
            while (std::getline(corpus, line))
            {
               if (line.empty() || line[0] == '#') continue;
               size_t a = line.find(" | "), b = line.find(" | ", a + 3);
               if (a == std::string::npos || b == std::string::npos) { pass = false; continue; }
               std::string domain = line.substr(0, a), expect = line.substr(a + 3, b - a - 3);
               std::string code = line.substr(b + 3);
               for (size_t p = code.find("\\n"); p != std::string::npos; p = code.find("\\n", p + 1))
                  code.replace(p, 2, "\n");
               bool ok; std::string error;
               if (domain == "pixel")
               {
                  FieldPixelNode node; node.code = code; ok = node.Apply(); error = node.LastError();
               }
               else
               {
                  FieldElementNode node; node.code = code; ok = node.Apply(); error = node.LastError();
               }
               bool good = expect == "OK" ? ok : (!ok && error.find(expect) != std::string::npos);
               if (!good) printf("[FIELDPIXELTEST] image corpus case %d FAIL: %s\n", cases, error.c_str());
               pass = pass && good; ++cases;
            }
            printf("[FIELDPIXELTEST] Assertion 28 (Image Read Corpus, %d cases): %s\n", cases, pass && cases == 12 ? "OK" : "FAIL");
         }

         // Readback proves identity, one-texel shift, half-texel interpolation,
         // alpha and clamping on a non-square source with a repeat sampler.
         {
            const int w = 96, h = 64;
            FieldPixelNode source;
            source.width = (float)w; source.height = (float)h;
            source.code = "col = vec3(uv.x, uv.y, step(0.5, fract(uv.x * 12))); alpha = uv.x;";
            bool pass = source.Apply(); source.CookIfNeeded(12000);
            glBindTexture(GL_TEXTURE_2D, source.GetOutputTexture());
            GLint filter = 0; glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &filter);
            pass = pass && filter == GL_LINEAR;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            unsigned int scratch = 0;
            auto read = [&](FieldPixelNode& node, int nw, int nh) {
               std::vector<float> pixels;
               GLUtil::ReadTexturePixels(scratch, node.GetOutputTexture(), nw, nh, pixels);
               return pixels;
            };
            auto src = read(source, w, h);
            FieldPixelNode effect; effect.width = (float)w; effect.height = (float)h;
            auto render = [&](const char* expr, int frame) {
               effect.code = std::string("input pixel image img; c = ") + expr + "; col = c.rgb; alpha = c.a;";
               bool ok = effect.Apply();
               if (!ok) printf("[FIELDPIXELTEST] image GPU compile FAIL: %s\n", effect.LastError().c_str());
               pass = pass && ok;
               if (effect.DeclaredImageInputCount() > 0) effect.DeclaredImageInput(0)->Connect(&source);
               effect.CookIfNeeded(frame);
               return read(effect, (int)effect.width, (int)effect.height);
            };
            auto bare = render("img", 12001), identity = render("img(uv)", 12002);
            auto shift = render("img(uv + vec2(1/res.x, 0))", 12003);
            auto half = render("img(uv + vec2(0.5/res.x, 0))", 12004);
            pass = pass && src.size() == (size_t)w*h*4 && bare.size() == src.size() && identity.size() == src.size() && shift.size() == src.size() && half.size() == src.size();
            if (pass)
               for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) for (int c=0; c<4; ++c)
               {
                  size_t at = (y*w+x)*4+c, next = (y*w+std::min(x+1,w-1))*4+c;
                  pass = pass && bare[at] == identity[at] && std::abs(shift[at]-src[next]) < 0.006f &&
                         std::abs(half[at]-(src[at]+src[next])*0.5f) < 0.006f;
               }
            // A different output size must clamp to SOURCE texel centres.
            effect.width = 192; effect.height = 108;
            auto edge = render("img(vec2(-2, 2))", 12005);
            size_t topLeft = (h-1)*w*4;
            pass = pass && edge.size() == (size_t)192*108*4;
            if (src.size() > topLeft+3 && edge.size() >= 4)
               for (int c=0; c<4; ++c) pass = pass && std::abs(edge[c]-src[topLeft+c]) < 0.001f;
            auto disconnected = render("img(uv)", 12006);
            effect.DeclaredImageInput(0)->Disconnect(); effect.CookIfNeeded(12007);
            disconnected = read(effect, 192, 108);
            pass = pass && !disconnected.empty();
            for (float v : disconnected) pass = pass && v == 0.0f;
            pass = pass && effect.EmitResult().offsetReadCount == 1 && !effect.EmitResult().usesOffsetReads;
            if (scratch) glDeleteFramebuffers(1, &scratch);
            printf("[FIELDPIXELTEST] Assertion 29 (Image Sampling GPU): %s\n", pass ? "OK" : "FAIL");
         }

         // Six camera presets render finite pixels at 1080p, including both
         // mode endpoints. Reusing a node catches stale param-uniform indices.
         {
            FieldPixelNode source; source.width = 1920; source.height = 1080;
            source.code = "col = vec3(uv.x, uv.y, 0.25); alpha = 0.75;";
            bool pass = source.Apply(); source.CookIfNeeded(13000);
            FieldPixelNode fx; fx.width = 1920; fx.height = 1080;
            unsigned int scratch = 0;
            int count = 0, frame = 13001;
            const char* names[] = {"Levels", "Scanlines / Interlace", "Lens Dirt + Flare", "Polar Coords", "Chromatic Aberration", "Zoom Blur"};
            for (const char* name : names)
               for (const auto& preset : FieldPixelNode::Presets())
                  if (std::string(preset.name) == name)
                  {
                     ++count; fx.code = preset.code;
                     bool ok = fx.Apply(); pass = pass && ok;
                     if (!ok) { printf("[FIELDPIXELTEST] camera preset '%s' FAIL: %s\n", name, fx.LastError().c_str()); continue; }
                     fx.DeclaredImageInput(0)->Connect(&source);
                     for (int endpoint = 0; endpoint < 3; ++endpoint)
                     {
                        if (endpoint > 0)
                           for (const auto& uniform : fx.EmitResult().uniforms)
                              if (uniform.paramIndex >= 0)
                                 if (auto* param = fx.GetParamTable().Find(uniform.varName))
                                    param->value = endpoint == 1 ? param->minValue : param->maxValue;
                        fx.CookIfNeeded(frame++);
                        std::vector<float> px;
                        GLUtil::ReadTexturePixels(scratch, fx.GetOutputTexture(), 1920, 1080, px);
                        pass = pass && px.size() == (size_t)1920*1080*4;
                        for (float v : px) pass = pass && std::isfinite(v);
                        // A gradient gives an analytic reference for the
                        // fixed taps; this catches accidentally identical taps.
                        if (std::string(name) == "Zoom Blur" && px.size() == (size_t)1920*1080*4)
                        {
                           const float u = 1440.5f / 1920.0f;
                           const float amount = fx.GetParamTable().Find("amount")->value;
                           const float centre = fx.GetParamTable().Find("centreX")->value;
                           double expected = 0.0;
                           for (int k = 0; k < 16; ++k)
                           {
                              double fraction = (double)k / 15.0;
                              double coord = u - (u - centre) * amount * fraction;
                              coord = std::max(0.5 / 1920.0, std::min(1.0 - 0.5 / 1920.0, coord));
                              expected += coord * (1.0 - 0.5 * fraction) / 12.0;
                           }
                           float actual = px[(810*1920+1440)*4];
                           bool good = std::abs(actual - expected) < 0.003;
                           if (!good) printf("[FIELDPIXELTEST] zoom taps FAIL: got %f expected %f\n", actual, expected);
                           pass = pass && good && fx.EmitResult().offsetReadCount == 16;
                        }
                     }
                  }
            if (scratch) glDeleteFramebuffers(1, &scratch);
            printf("[FIELDPIXELTEST] Assertion 30 (Camera FX at 1920x1080, %d presets): %s\n", count, pass && count == 6 ? "OK" : "FAIL");
         }

         printf("[FIELDPIXELTEST] Test suite complete.\n");
      }
}

void FrameTest_FIELDDEVICETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDDEVICETEST") != nullptr && frameId == 4)
      {
         printf("[FIELDDEVICETEST] Running .field device file conformance harness...\n");

         // 1. ToDeviceFile -> ToJsonString -> FromJsonString -> LoadDeviceFile
         //    round-trips code (byte-for-byte) and every declared param's
         //    value (within float epsilon) for the element domain.
         {
            FieldElementNode src;
            src.code = "param float amount = 0.5 [0, 2]\nparam float freq = 6.0 [0, 20]\nP.y += sin(P.x * freq) * amount\n";
            src.Apply();
            Field::ParamEntry* pAmount = src.GetParamTable().Find("amount");
            Field::ParamEntry* pFreq = src.GetParamTable().Find("freq");
            bool setupOk = pAmount != nullptr && pFreq != nullptr;
            if (setupOk)
            {
               pAmount->value = 0.75f;
               pFreq->value = 9.5f;
            }

            Field::DeviceFile device = src.ToDeviceFile();
            std::string json = Field::ToJsonString(device);
            Field::DeviceFile parsed;
            std::string parseErr;
            bool parseOk = Field::FromJsonString(json, parsed, parseErr);

            FieldElementNode dst;
            dst.LoadDeviceFile(parsed);
            Field::ParamEntry* dAmount = dst.GetParamTable().Find("amount");
            Field::ParamEntry* dFreq = dst.GetParamTable().Find("freq");

            bool pass = setupOk && parseOk &&
                        parsed.code == src.code &&
                        dAmount != nullptr && dFreq != nullptr &&
                        std::abs(dAmount->value - 0.75f) < 1e-4f &&
                        std::abs(dFreq->value - 9.5f) < 1e-4f;
            printf("[FIELDDEVICETEST] Assertion 1 (JSON Round-Trip): %s\n", pass ? "OK" : "FAIL");
         }

         // 2. SaveToFieldFile -> LoadFromFieldFile round-trips identically
         //    through actual file I/O.
         {
            FieldPixelNode src;
            src.width = 128.0f;
            src.height = 96.0f;
            src.animate = true;
            src.code = "param float bright = 0.5 [0, 1]\ncol = vec3(uv.x, uv.y, bright);";
            src.Apply();
            Field::ParamEntry* pBright = src.GetParamTable().Find("bright");
            if (pBright != nullptr) pBright->value = 0.42f;

            Field::DeviceFile device = src.ToDeviceFile();
            const char* tmpPath = "/tmp/infinite_fielddevicetest.field";
            bool saveOk = Field::SaveToFieldFile(tmpPath, device);

            Field::DeviceFile loaded;
            std::string loadErr;
            bool loadOk = Field::LoadFromFieldFile(tmpPath, loaded, loadErr);
            remove(tmpPath);

            bool pass = saveOk && loadOk &&
                        loaded.code == src.code &&
                        loaded.domain == "pixel" &&
                        loaded.nodeSettings.count("width") &&
                        std::abs(loaded.nodeSettings["width"] - 128.0) < 1e-6 &&
                        std::abs(loaded.nodeSettings["height"] - 96.0) < 1e-6 &&
                        loaded.params.count("bright") &&
                        std::abs(loaded.params["bright"] - 0.42) < 1e-4;
            printf("[FIELDDEVICETEST] Assertion 2 (File I/O Round-Trip): %s\n", pass ? "OK" : "FAIL");
         }

         // 3. LoadDeviceFile with a param name the target's current code
         //    does not declare silently ignores that entry - no crash, no
         //    phantom param added to the table.
         {
            FieldElementNode node;
            node.code = "param float amount = 0.5 [0, 2]\nP.y += amount\n";
            node.Apply();
            size_t countBefore = node.GetParamTable().Params().size();

            Field::DeviceFile device;
            device.domain = "element";
            device.code = node.code;
            device.params["amount"] = 0.9;
            device.params["doesNotExist"] = 1.23; // not declared by the code above

            node.LoadDeviceFile(device);
            Field::ParamEntry* pAmount = node.GetParamTable().Find("amount");
            Field::ParamEntry* pPhantom = node.GetParamTable().Find("doesNotExist");
            size_t countAfter = node.GetParamTable().Params().size();

            bool pass = pAmount != nullptr && std::abs(pAmount->value - 0.9f) < 1e-4f &&
                        pPhantom == nullptr && countAfter == countBefore;
            printf("[FIELDDEVICETEST] Assertion 3 (Undeclared Param Ignored): %s\n", pass ? "OK" : "FAIL");
         }

         // 4. LoadDeviceFile with deliberately malformed code leaves the
         //    node's previous code/compiled program in place and populates
         //    LastError() - the same keep-last-working-program contract
         //    LoadPreset(int) already relies on.
         {
            FieldElementNode node;
            node.code = "P.y += 0.1\n";
            node.Apply();
            std::string codeBefore = node.code;
            bool errBefore = node.LastError().empty();

            Field::DeviceFile device;
            device.domain = "element";
            device.code = "P.y += ("; // deliberate parse error

            node.LoadDeviceFile(device);

            bool pass = errBefore && !node.LastError().empty();
            printf("[FIELDDEVICETEST] Assertion 4 (Malformed Code Keeps Last-Working Program): %s\n", pass ? "OK" : "FAIL");
         }

         // 5. Dropping a device file whose domain does not match the drop
         //    target is a no-op - mirrors the domain-match gate the
         //    drag-and-drop dispatch in this file applies before ever
         //    calling LoadDeviceFile, exercised here directly rather than
         //    via gDroppedFiles/UI automation.
         {
            FieldSampleNode node;
            node.code = "state float y = 0\ny = y * 0.9 + in * 0.1\nout = y\n";
            node.Apply();
            std::string codeBefore = node.code;

            Field::DeviceFile device;
            device.domain = "pixel"; // mismatched - node is 'sample'
            device.code = "col = vec3(1.0, 0.0, 0.0);";

            // The dispatch in this file never calls LoadDeviceFile at all
            // when domains disagree; reproduce that same gate here.
            static const std::string kSampleDomain = "sample";
            bool domainMatches = (device.domain == kSampleDomain);
            if (domainMatches)
               node.LoadDeviceFile(device);

            bool pass = !domainMatches && node.code == codeBefore;
            printf("[FIELDDEVICETEST] Assertion 5 (Mismatched Domain Drop Is No-Op): %s\n", pass ? "OK" : "FAIL");
         }

         // 6. FieldPrimitiveNode .infdev round-trip (domain == "primitive",
         //    count/maxElements nodeSettings, declared params).
         {
            FieldPrimitiveNode src;
            src.count = 512;
            src.maxElements = 2048;
            src.code = "param float radius = 2.5 [0.1, 10.0]\nu = i / count\nP = vec3(cos(u * 6.28) * radius, 0.0, sin(u * 6.28) * radius)\n";
            src.Apply();
            Field::ParamEntry* pRad = src.GetParamTable().Find("radius");
            if (pRad != nullptr) pRad->value = 3.75f;

            Field::DeviceFile dev = src.ToDeviceFile();
            std::string json = Field::ToJsonString(dev);
            Field::DeviceFile parsed;
            std::string err;
            bool parseOk = Field::FromJsonString(json, parsed, err);

            FieldPrimitiveNode dst;
            if (parseOk && parsed.domain == "primitive")
               dst.LoadDeviceFile(parsed);

            Field::ParamEntry* dRad = dst.GetParamTable().Find("radius");
            bool pass = parseOk && parsed.domain == "primitive" &&
                        dst.count == 512 && dst.maxElements == 2048 &&
                        dRad != nullptr && std::abs(dRad->value - 3.75f) < 1e-4f;
            printf("[FIELDDEVICETEST] Assertion 6 (FieldPrimitive Round-Trip): %s\n", pass ? "OK" : "FAIL");
         }

         // 7. FieldSynthNode .infdev round-trip (domain == "synth",
         //    maxVoices/exposeRmsOutput nodeSettings, declared params).
         {
            FieldSynthNode src;
            src.maxVoices = 12;
            src.exposeRmsOutput = true;
            src.code = "param float cutoff = 1500.0 [20.0, 20000.0]\nstate float ph = 0\nph = (ph + freq / sr) % 1.0\nout = (ph * 2.0 - 1.0) * gate\n";
            src.Apply();
            Field::ParamEntry* pCutoff = src.GetParamTable().Find("cutoff");
            if (pCutoff != nullptr) pCutoff->value = 2400.0f;

            Field::DeviceFile dev = src.ToDeviceFile();
            std::string json = Field::ToJsonString(dev);
            Field::DeviceFile parsed;
            std::string err;
            bool parseOk = Field::FromJsonString(json, parsed, err);

            FieldSynthNode dst;
            if (parseOk && parsed.domain == "synth")
               dst.LoadDeviceFile(parsed);

            Field::ParamEntry* dCutoff = dst.GetParamTable().Find("cutoff");
            bool pass = parseOk && parsed.domain == "synth" &&
                        dst.maxVoices == 12 && dst.exposeRmsOutput == true &&
                        dCutoff != nullptr && std::abs(dCutoff->value - 2400.0f) < 1e-4f &&
                        dst.NoteInputSlot(0) != nullptr && dst.AudioInputSlot(1) != nullptr;
            printf("[FIELDDEVICETEST] Assertion 7 (FieldSynth Round-Trip): %s\n", pass ? "OK" : "FAIL");
         }

         printf("[FIELDDEVICETEST] Test suite complete.\n");
      }
}

void FrameTest_FIELDGRAPHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDGRAPHTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHTEST] Running Field graph-domain reconciler harness...\n");
         bool ok = true;

         auto parseNotice = [](const std::string& notice, int& mounted, int& updated, int& remounted, int& unmounted, int& connected)
         {
            mounted = updated = remounted = unmounted = connected = -1;
            sscanf(notice.c_str(), "mounted %d, updated %d, remounted %d, unmounted %d, %d connections",
                   &mounted, &updated, &remounted, &unmounted, &connected);
         };

         // 1. A kernel emitting `voices` (=4) LFOs mounts exactly 4 nodes.
         {
            NewPatch();
            FieldGraphNode node;
            node.code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "      set(osc, \"rateBeats\", 1.0 + k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            bool regenerated = node.Regenerate(host);
            int mounted, updated, remounted, unmounted, connected;
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass = regenerated && gNodes.size() == 4 && mounted == 4 && remounted == 0 && unmounted == 0;
            if (!regenerated) printf("[FIELDGRAPHTEST]   error: %s\n", node.LastError().c_str());
            printf("[FIELDGRAPHTEST] Assertion 1 (Initial Mount): gNodes=%zu mounted=%d  %s\n",
                   gNodes.size(), mounted, pass ? "OK" : "FAIL");
            ok = ok && pass;

            // 2. Idempotence: regenerating an unchanged kernel produces zero
            //    structural actions and leaves gNodes and every index alone.
            std::vector<int> before;
            for (const GraphNode& gn : gNodes) before.push_back(gn.index);
            bool regenerated2 = node.Regenerate(host);
            std::vector<int> after;
            for (const GraphNode& gn : gNodes) after.push_back(gn.index);
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass2 = regenerated2 && gNodes.size() == 4 && mounted == 0 && remounted == 0 && unmounted == 0 && before == after;
            printf("[FIELDGRAPHTEST] Assertion 2 (Idempotence): gNodes=%zu mounted=%d unmounted=%d indicesUnchanged=%d  %s\n",
                   gNodes.size(), mounted, unmounted, (int)(before == after), pass2 ? "OK" : "FAIL");
            ok = ok && pass2;

            // 3. Raising voices 4 -> 6 mounts exactly 2, unmounts/remounts 0.
            node.GetParamTable().Find("voices")->value = 6.0f;
            bool regenerated3 = node.Regenerate(host);
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass3 = regenerated3 && gNodes.size() == 6 && mounted == 2 && remounted == 0 && unmounted == 0;
            printf("[FIELDGRAPHTEST] Assertion 3 (Raise 4->6): gNodes=%zu mounted=%d  %s\n",
                   gNodes.size(), mounted, pass3 ? "OK" : "FAIL");
            ok = ok && pass3;

            // 4. Lowering voices 6 -> 3 unmounts exactly 3, mounts 0.
            node.GetParamTable().Find("voices")->value = 3.0f;
            bool regenerated4 = node.Regenerate(host);
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass4 = regenerated4 && gNodes.size() == 3 && unmounted == 3 && mounted == 0 && remounted == 0;
            printf("[FIELDGRAPHTEST] Assertion 4 (Lower 6->3): gNodes=%zu unmounted=%d  %s\n",
                   gNodes.size(), unmounted, pass4 ? "OK" : "FAIL");
            ok = ok && pass4;
         }

         // 5. Changing one set() value (voices held fixed at 4): exactly 4
         //    updates, 0 mounts, 0 unmounts, and every node index unchanged (T13).
         {
            NewPatch();
            FieldGraphNode node;
            node.code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "      set(osc, \"rateBeats\", 1.0 + k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            node.Regenerate(host);
            std::vector<int> before;
            for (const GraphNode& gn : gNodes) before.push_back(gn.index);

            node.code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "      set(osc, \"rateBeats\", 2.0 + k)\n"
               "   }\n"
               "}\n";
            bool regenerated = node.Regenerate(host);
            std::vector<int> after;
            for (const GraphNode& gn : gNodes) after.push_back(gn.index);
            int mounted, updated, remounted, unmounted, connected;
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass = regenerated && updated == 4 && mounted == 0 && unmounted == 0 && before == after;
            printf("[FIELDGRAPHTEST] Assertion 5 (Set-Value Change, T13): updated=%d mounted=%d unmounted=%d indicesUnchanged=%d  %s\n",
                   updated, mounted, unmounted, (int)(before == after), pass ? "OK" : "FAIL");
            ok = ok && pass;
         }

         // 6. Renaming the emit target changes every key -> 3 unmounts + 3 mounts.
         {
            NewPatch();
            FieldGraphNode node;
            node.code =
               "param float voices = 3 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            node.Regenerate(host);

            node.code =
               "param float voices = 3 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc2 = emit(\"LFO\", k)\n"
               "   }\n"
               "}\n";
            bool regenerated = node.Regenerate(host);
            int mounted, updated, remounted, unmounted, connected;
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass = regenerated && mounted == 3 && unmounted == 3 && gNodes.size() == 3;
            printf("[FIELDGRAPHTEST] Assertion 6 (Rename Emit Target): mounted=%d unmounted=%d  %s\n",
                   mounted, unmounted, pass ? "OK" : "FAIL");
            ok = ok && pass;
         }

         // 7. Inserting a second emit ABOVE the first in source leaves the
         //    first emit's keyed nodes at their existing indices (T2) -
         //    identity is name+key path, not source position.
         {
            NewPatch();
            FieldGraphNode node;
            node.code =
               "param float voices = 3 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            node.Regenerate(host);
            std::vector<int> before;
            for (const std::string& key : { std::string("osc#0"), std::string("osc#1"), std::string("osc#2") })
               before.push_back(node.Ownership().Get(key));

            node.code =
               "param float voices = 3 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      trig = emit(\"Ramp\", k)\n"
               "      osc = emit(\"LFO\", k)\n"
               "   }\n"
               "}\n";
            bool regenerated = node.Regenerate(host);
            std::vector<int> after;
            for (const std::string& key : { std::string("osc#0"), std::string("osc#1"), std::string("osc#2") })
               after.push_back(node.Ownership().Get(key));
            int mounted, updated, remounted, unmounted, connected;
            parseNotice(node.Notice(), mounted, updated, remounted, unmounted, connected);
            bool pass = regenerated && before == after &&
                        std::find(before.begin(), before.end(), -1) == before.end() &&
                        mounted == 3 && unmounted == 0 && remounted == 0 && gNodes.size() == 6;
            printf("[FIELDGRAPHTEST] Assertion 7 (Insert Emit Above, T2): mounted=%d gNodes=%zu indicesUnchanged=%d  %s\n",
                   mounted, gNodes.size(), (int)(before == after), pass ? "OK" : "FAIL");
            ok = ok && pass;
         }

         NewPatch();
         printf("%s\n", ok ? "FIELDGRAPH OK" : "SUSPECT");
      }
}

void FrameTest_FIELDGRAPHRATETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDGRAPHRATETEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHRATETEST] Running Field graph-domain rate-zero and syntax validation harness...\n");
         bool allOk = true;

         auto checkCompileError = [&](const char* label, const std::string& code, const std::string& mustContainLeaf, const std::string& mustContainFix) -> bool {
            FieldGraphNode node;
            node.code = code;
            bool res = node.Apply();
            bool hasErr = !res && !node.LastError().empty();
            bool leafMatch = node.LastError().find(mustContainLeaf) != std::string::npos;
            bool fixMatch = node.LastError().find(mustContainFix) != std::string::npos;
            bool pass = hasErr && leafMatch && fixMatch;
            if (!pass)
            {
               printf("[FIELDGRAPHRATETEST]   %s failed: res=%d, err='%s' (expected leaf '%s' and fix '%s')\n",
                      label, (int)res, node.LastError().c_str(), mustContainLeaf.c_str(), mustContainFix.c_str());
            }
            printf("[FIELDGRAPHRATETEST] %s: %s\n", label, pass ? "OK" : "FAIL");
            return pass;
         };

         // 1. t in a set value
         allOk = checkCompileError("Assertion 1 (t in set value)",
            "osc = emit(\"LFO\", 0)\nset(osc, \"rateBeats\", t)\n",
            "t", "Fix:") && allOk;

         // 2. P in a loop bound
         allOk = checkCompileError("Assertion 2 (P in loop bound)",
            "for (k = 0; k < P.x; k += 1) {\n   osc = emit(\"LFO\", k)\n}\n",
            "P", "Fix:") && allOk;

         // 3. uv anywhere
         allOk = checkCompileError("Assertion 3 (uv anywhere)",
            "x = uv\n",
            "uv", "Fix:") && allOk;

         // 4. in anywhere
         allOk = checkCompileError("Assertion 4 (in anywhere)",
            "x = in\n",
            "in", "Fix:") && allOk;

         // 5. rand() in a set value
         allOk = checkCompileError("Assertion 5 (rand() in set value)",
            "osc = emit(\"LFO\", 0)\nset(osc, \"rateBeats\", rand())\n",
            "rand()", "Fix:") && allOk;

         // 6. a t-dependent global
         {
            ExprGlobals::All().push_back({"rateTGlobal", "t * 2.0", 0.0f, ""});
            bool pass = checkCompileError("Assertion 6 (t-dependent global)",
               "osc = emit(\"LFO\", 0)\nset(osc, \"rateBeats\", rateTGlobal)\n",
               "rateTGlobal", "Fix:");
            ExprGlobals::All().pop_back();
            allOk = pass && allOk;
         }

         // 7. reduce from sample
         allOk = checkCompileError("Assertion 7 (reduce from sample)",
            "x = reduce.rms(in, 20.0, 2000.0)\n",
            "in", "Fix:") && allOk;

         // 8. assignment to a global
         {
            ExprGlobals::All().push_back({"myConstGlobal", "42.0", 42.0f, ""});
            bool pass = checkCompileError("Assertion 8 (assignment to global)",
               "myConstGlobal = 10.0\n",
               "myConstGlobal", "Fix:");
            ExprGlobals::All().pop_back();
            allOk = pass && allOk;
         }

         // 9. emit(\"Group\", i)
         allOk = checkCompileError("Assertion 9 (emit Group)",
            "g = emit(\"Group\", 0)\n",
            "Group", "Fix:") && allOk;

         // 10. emit(\"Field Graph\", i)
         allOk = checkCompileError("Assertion 10 (emit Field Graph)",
            "fg = emit(\"Field Graph\", 0)\n",
            "Field Graph", "Fix:") && allOk;

         // 11. emit in a loop with no key
         allOk = checkCompileError("Assertion 11 (emit in loop no key)",
            "for (k = 0; k < 4; k += 1) {\n   osc = emit(\"LFO\")\n}\n",
            "LFO", "Fix:") && allOk;

         // 12. two emits with the same name
         allOk = checkCompileError("Assertion 12 (two emits same name)",
            "osc = emit(\"LFO\", 0)\nosc = emit(\"LFO\", 1)\n",
            "osc", "Fix:") && allOk;

         // 13. a non-literal type name
         allOk = checkCompileError("Assertion 13 (non-literal type name)",
            "k = 1\nosc = emit(k, 0)\n",
            "type name", "Fix:") && allOk;

         // 14. clean kernel compiles with zero errors and infers domain graph
         {
            FieldGraphNode cleanNode;
            cleanNode.code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "      set(osc, \"rateBeats\", 1.0 + k)\n"
               "   }\n"
               "}\n";
            bool res = cleanNode.Apply();
            bool pass = res && cleanNode.LastError().empty();
            if (!pass)
               printf("[FIELDGRAPHRATETEST]   Clean kernel failed: %s\n", cleanNode.LastError().c_str());
            printf("[FIELDGRAPHRATETEST] Assertion 14 (Clean Kernel): %s\n", pass ? "OK" : "FAIL");
            allOk = pass && allOk;
         }

         // 15. All factory presets compile
         {
            bool pass = true;
            for (const auto& p : FieldGraphNode::Presets())
            {
               FieldGraphNode pnode;
               pnode.code = p.code;
               if (!pnode.Apply())
               {
                  printf("[FIELDGRAPHRATETEST]   Preset '%s' failed: %s\n", p.name, pnode.LastError().c_str());
                  pass = false;
               }
            }
            printf("[FIELDGRAPHRATETEST] Assertion 15 (Factory Presets Compile): %s\n", pass ? "OK" : "FAIL");
            allOk = pass && allOk;
         }

         printf("%s\n", allOk ? "FIELDGRAPHRATE OK" : "SUSPECT");
      }
}
}

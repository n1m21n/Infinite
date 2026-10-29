// Headless checks for the Sample Studio's slice document and render (docs/plans/sample-studio/SLICE-EDITOR.md §9):
// save -> load -> save gives the same bytes, edits keep the invariants, untouched slices render back to the source,
// and every per-slice control changes the sound it claims to. Exit code 0 when all pass.
#include "SliceRender.h"
#include "SliceSet.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
   int gFails = 0;
   void Check(bool ok, const char* what)
   {
      printf("%s %s\n", ok ? "ok  " : "FAIL", what);
      if (!ok)
         gFails++;
   }

   SourceAudio Noise(int frames, double sr)
   {
      SourceAudio s;
      s.sr = sr;
      std::mt19937 rng(7);
      std::uniform_real_distribution<float> d(-0.5f, 0.5f);
      for (int i = 0; i < frames; i++)
      {
         // Decaying hits every 6000 frames over noise, so the shaper and compressor have something to grab.
         const float env = std::exp(-(float)(i % 6000) / 900.0f);
         s.l.push_back(d(rng) * (0.1f + env));
         s.r.push_back(d(rng) * (0.1f + env));
      }
      return s;
   }

   double MaxDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
   {
      double m = 0.0;
      for (size_t i = from; i < to && i < a.size() && i < b.size(); i++)
         m = std::max(m, (double)std::fabs(a[i] - b[i]));
      return m;
   }

   double Rms(const std::vector<float>& x, size_t from, size_t to)
   {
      double s = 0.0;
      size_t n = 0;
      for (size_t i = from; i < to && i < x.size(); i++, n++)
         s += (double)x[i] * x[i];
      return n ? std::sqrt(s / (double)n) : 0.0;
   }
}

int main()
{
   const double sr = 48000.0;
   const SourceAudio src = Noise(48000, sr);

   // ---- the document ----
   SliceSet set;
   set.sampleBpm = 120.0f;
   set.Reset(0, src.Frames());
   Check(set.Count() == 1 && set.EndOf(0) == src.Frames(), "reset makes one slice over the sample");
   Check(set.SplitAt(12000) == 1 && set.SplitAt(24000) == 2 && set.SplitAt(36000) == 3, "split at three points");
   Check(set.SplitAt(12010) == -1, "split refuses a point next to a marker");
   Check(set.IndexAt(25000) == 2 && set.IndexAt(-5) == 0 && set.IndexAt(99999) == 3, "index at a frame");
   set.slices[1].name = "snare = hit 2%";
   set.slices[1].colour = 3;
   set.slices[1].p.gainDb = -3.5f;
   set.slices[2].p.reverse = true;
   set.slices[3].p.stretchMode = 2;
   set.slices[3].p.lengthRatio = 0.5f;
   set.wide.pitch = 2.0f;
   const std::string text = set.Serialize();
   SliceSet back;
   Check(SliceSet::Parse(text, back) && back == set, "save -> load gives the same set");
   Check(back.Serialize() == text, "save -> load -> save gives the same bytes");
   Check(back.slices[1].name == "snare = hit 2%", "a name with separators survives");
   SliceSet bad;
   Check(!SliceSet::Parse("slice id=1 s=0\n", bad), "no header is refused");
   Check(SliceSet::Parse("sliceset 1 end=100 next=1\nslice s=50\nslice s=0\nslice id=1 s=10 zz=4\n", bad) &&
             bad.Count() == 3 && bad.slices[0].start == 0 && bad.slices[0].id != bad.slices[2].id,
         "a hand-edited file loads sorted with unique ids and unknown keys ignored");
   const uint16_t gone = set.slices[2].id;
   Check(set.Join(2) && set.Count() == 3 && set.IndexOfId(gone) < 0, "join removes the marker");
   set.Move(1, 1, 64);
   Check(set.slices[1].start == 64, "move clamps against the slice before");
   set.FromEqual(8, 0, src.Frames());
   Check(set.Count() == 8 && !set.HandEdited(), "equal slices");
   set.FromGrid(sr * 0.125, 0, src.Frames());
   Check(set.Count() == 8, "grid slices");
   std::vector<int> on = { 0, 100, 6000, 12000, 18000 };
   std::vector<float> st = { 1e9f, 0.1f, 5.0f, 4.0f, 3.0f };
   set.FromOnsets(on, st, 0, src.Frames(), 4);
   Check(set.Count() == 4 && set.slices[1].start == 6000, "onsets keep the strongest");

   // ---- the render ----
   SliceSet plain;
   plain.Reset(0, src.Frames());
   for (int f : { 7000, 13000, 20011, 31000 })
      plain.SplitAt(f);
   SliceRenderCache cache;
   SliceRenderResult res;
   Check(RenderSliceSet(src, plain, &cache, nullptr, res), "render runs");
   Check(res.l.size() == src.l.size(), "untouched slices keep the length");
   Check(MaxDiff(res.l, src.l, 0, src.l.size()) < 1e-5 && MaxDiff(res.r, src.r, 0, src.r.size()) < 1e-5,
         "untouched slices render back to the source");

   SliceSet edited = plain;
   edited.slices[2].p.gainDb = -6.0f;
   SliceRenderResult r2;
   RenderSliceSet(src, edited, &cache, nullptr, r2);
   const size_t a2 = (size_t)edited.slices[2].start + 200, b2 = (size_t)edited.EndOf(2) - 200;
   Check(std::fabs(Rms(r2.l, a2, b2) / Rms(src.l, a2, b2) - 0.501) < 0.01, "gain -6 dB halves the slice");
   Check(MaxDiff(r2.l, src.l, 0, (size_t)edited.slices[2].start - 100) < 1e-5, "gain leaves the other slices alone");

   SliceSet muted = plain;
   muted.slices[1].p.mute = true;
   RenderSliceSet(src, muted, &cache, nullptr, r2);
   Check(Rms(r2.l, (size_t)muted.slices[1].start + 200, (size_t)muted.EndOf(1) - 200) < 1e-7, "mute silences");
   SliceSet solo = plain;
   solo.slices[3].p.solo = true;
   RenderSliceSet(src, solo, &cache, nullptr, r2);
   Check(Rms(r2.l, 1000, 6000) < 1e-7 && Rms(r2.l, (size_t)solo.slices[3].start + 200, (size_t)solo.EndOf(3) - 200) > 0.01,
         "solo silences the others");

   SliceSet half = plain;
   half.slices[1].p.stretchMode = 2;
   half.slices[1].p.lengthRatio = 0.5f;
   RenderSliceSet(src, half, &cache, nullptr, r2);
   const int64_t want = src.Frames() - (half.EndOf(1) - half.slices[1].start) / 2;
   Check(std::llabs((int64_t)r2.l.size() - want) <= 2, "beats stretch to half shortens the output by half the slice");

   SliceSet up = plain;
   up.slices[1].p.pitchSt = 12.0f;
   RenderSliceSet(src, up, &cache, nullptr, r2);
   Check(r2.l.size() == src.l.size(), "pitch +12 keeps the length");

   SliceSet panned = plain;
   panned.slices[2].p.pan = 1.0f;
   RenderSliceSet(src, panned, &cache, nullptr, r2);
   const size_t pa = (size_t)panned.slices[2].start + 200, pb = (size_t)panned.EndOf(2) - 200;
   Check(Rms(r2.l, pa, pb) < 1e-6 && std::fabs(Rms(r2.r, pa, pb) / Rms(src.r, pa, pb) - 1.414f) < 0.02f,
         "pan right: left silent, right +3 dB");

   std::vector<float> tl(src.l), tr(src.r);
   StretchBuffer(tl, tr, src.sr, (int64_t)tl.size() / 2);
   Check((int64_t)tl.size() == src.Frames() / 2 && Rms(tl, 0, tl.size()) > 0.5f * Rms(src.l, 0, src.l.size()), "tempo stretch halves the length");

   SliceSet rev = plain;
   rev.slices[2].p.reverse = true;
   RenderSliceSet(src, rev, &cache, nullptr, r2);
   const size_t ra = (size_t)rev.slices[2].start, rb = (size_t)rev.EndOf(2);
   Check(std::fabs(r2.l[ra + 500] - src.l[rb - 1 - 500]) < 1e-5, "reverse plays the slice backwards");

   SliceSet comp = plain;
   comp.slices[0].p.compAmount = 1.0f;
   comp.slices[0].p.compThreshDb = -30.0f;
   comp.slices[0].p.compRatio = 8.0f;
   comp.slices[0].p.compMakeupDb = -20.0f;
   RenderSliceSet(src, comp, &cache, nullptr, r2);
   Check(PeakOf(std::vector<float>(r2.l.begin(), r2.l.begin() + 6000), {}) <
             PeakOf(std::vector<float>(src.l.begin(), src.l.begin() + 6000), {}),
         "the compressor lowers the peak");

   SliceSet shp = plain;
   shp.slices[0].p.transient = 1.0f;
   RenderSliceSet(src, shp, &cache, nullptr, r2);
   Check(Rms(r2.l, 0, 300) > Rms(src.l, 0, 300) * 1.3, "transient + lifts the attack");

   SliceSet lp = plain;
   lp.slices[0].p.filterMode = 2;
   lp.slices[0].p.cutoffHz = 300.0f;
   RenderSliceSet(src, lp, &cache, nullptr, r2);
   Check(Rms(r2.l, 200, 6000) < Rms(src.l, 200, 6000) * 0.4, "lp 24 at 300 Hz darkens noise");

   SliceSet fade = plain;
   fade.slices[1].p.fadeInMs = 50.0f;
   RenderSliceSet(src, fade, &cache, nullptr, r2);
   Check(std::fabs(r2.l[(size_t)fade.slices[1].start + 100]) < 0.05f, "fade in starts from silence (after the crossfade)");

   SliceSet wide = plain;
   wide.wide.speed = 2.0f;
   RenderSliceSet(src, wide, &cache, nullptr, r2);
   Check(std::llabs((int64_t)r2.l.size() - src.Frames() / 2) <= 1, "speed 2x halves the length");
   wide = plain;
   wide.wide.pitch = 5.0f;
   RenderSliceSet(src, wide, &cache, nullptr, r2);
   Check(r2.l.size() == src.l.size(), "sample-wide pitch keeps the length");

   const double laid = SourceToLaid(half, res, 15000);
   Check(std::fabs(LaidToSource(plain, res, SourceToLaid(plain, res, 15000)) - 15000.0) < 1.0 && laid >= 0.0,
         "playhead maps out and back");

   std::atomic<bool> stop{ true };
   Check(!RenderSliceSet(src, plain, nullptr, &stop, r2), "a cancelled render says so");

   printf(gFails == 0 ? "SLICECHECK OK\n" : "SLICECHECK FAILED (%d)\n", gFails);
   return gFails == 0 ? 0 : 1;
}

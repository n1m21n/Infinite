#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The Sample Studio's edit document (docs/plans/sample-studio/SLICE-EDITOR.md §5): where the slices of a source sample
// start, and what each slice does to its audio. Plain data, no ImGui and no audio objects, so the headless tests drive
// it directly. Frames are source frames. A slice runs from its start to the next slice's start, the last one to
// `endFrame`; the first slice's start and `endFrame` are the sample's start and end markers.
// Serialize() writes a versioned text block that leaves defaults out, so a field added later loads as its default and
// save -> load -> save gives the same bytes.

struct SliceParams
{
   float gainDb = 0.0f;  // -60 or below is silence
   float pan = 0.0f;     // -1..+1, constant power
   float pitchSt = 0.0f; // -48..+48
   float fineCt = 0.0f;  // -100..+100
   int stretchMode = 1;  // 0 off (pitch resamples), 1 repitch, 2 beats, 3 tonal
   float lengthRatio = 1.0f; // 0.25..4, the slice's length against its source length
   bool fitToGrid = false;   // length snaps to whole 16ths of the sample tempo
   float compAmount = 0.0f;  // 0..1, the compressed signal's share (parallel)
   float compThreshDb = -18.0f, compRatio = 4.0f, compAttackMs = 10.0f, compReleaseMs = 120.0f, compMakeupDb = 0.0f;
   float transient = 0.0f; // -1..+1: the attack, down or up to 12 dB
   float decay = 0.0f;     // -1..+1: the body after the attack, down or up to 12 dB
   float fadeInMs = 0.0f, fadeOutMs = 0.0f;
   float fadeCurve = 0.0f; // -1 (fast) .. 0 (linear) .. +1 (slow)
   int filterMode = 0;     // index into SliceFilterNames()
   float cutoffHz = 20000.0f, res = 0.0f;
   bool reverse = false;
   int loopMode = 0; // 0 off, 1 forward, 2 ping-pong: fills a length longer than the slice
   float loopStart = 0.0f, loopEnd = 1.0f; // 0..1 of the slice
   bool mute = false, solo = false;
   int choke = 0; // 0 none, 1..8: a slice cuts the others in its group (carried to the Slicer)

   bool operator==(const SliceParams& o) const;
   bool operator!=(const SliceParams& o) const { return !(*this == o); }
};

// The filter modes a slice offers, a subset of SynthModes::FilterNames() in its order (node-ui-pillars P7).
const std::vector<const char*>& SliceFilterNames();
const char* SliceStretchName(int mode);

struct Slice
{
   uint16_t id = 0;   // stable while the slice exists; never reused within a set
   int64_t start = 0; // source frame
   std::string name;  // empty: shown as its number
   int colour = -1;   // index into the slice palette, -1: by position
   int note = -1;     // -1: 36 + index, as the Slicer plays it
   SliceParams p;
};

// Sample-wide settings, applied to the slices laid end to end (the Studio's settings before slices existed).
struct SampleWide
{
   float pitch = 0.0f, fine = 0.0f, speed = 1.0f, fadeInMs = 0.0f, fadeOutMs = 0.0f, gainDb = 0.0f;
   bool loop = true, reverse = false, pingPong = false;
   // Playback tempo: the project's (the default), or its own `ownBpm`. Either way the edit is stretched from the
   // sample's tempo to it, at the same pitch.
   bool followTempo = true;
   float ownBpm = 120.0f;
   bool operator==(const SampleWide& o) const;
};

struct SliceSet
{
   static constexpr int kMaxSlices = 64; // the Slicer's limit
   static constexpr int kBaseNote = 36;

   std::vector<Slice> slices; // sorted by start, never empty once Reset
   int64_t endFrame = 0;
   float sampleBpm = 0.0f; // the source's tempo; 0: unknown (the ruler shows seconds)
   float xfadeMs = 2.0f;   // crossfade where one slice hands over to the next
   uint16_t nextId = 1;
   SampleWide wide;

   bool Empty() const { return slices.empty(); }
   int Count() const { return (int)slices.size(); }
   int64_t StartFrame() const { return slices.empty() ? 0 : slices.front().start; }
   int64_t EndOf(int i) const { return i + 1 < Count() ? slices[(size_t)i + 1].start : endFrame; }
   int IndexAt(int64_t frame) const; // the slice holding `frame`, clamped to the first and last
   int IndexOfId(uint16_t id) const;
   int NoteOf(int i) const { return slices[(size_t)i].note >= 0 ? slices[(size_t)i].note : kBaseNote + i; }

   // One slice over [from, to): what a new source starts as. Keeps `wide`, sampleBpm and xfadeMs.
   void Reset(int64_t from, int64_t to);
   // Splits the slice holding `frame`; the new slice copies the old one's settings. Returns its index, or -1 when
   // the point is too close to a marker, outside the sample, or the set is full.
   int SplitAt(int64_t frame, int64_t minGap = 64);
   // Removes slice `i`'s start marker, so the slice before it takes its span (i >= 1).
   bool Join(int i);
   // Moves slice `i`'s start between its neighbours. Slice 0 moves the sample's start marker.
   void Move(int i, int64_t frame, int64_t minGap = 64);
   void MoveEnd(int64_t frame, int64_t minGap = 64);
   // Replace every slice inside [from, to) with new ones. `onsets` are relative to `from`, strongest kept first.
   void FromOnsets(const std::vector<int>& onsets, const std::vector<float>& strengths, int64_t from, int64_t to, int maxSlices);
   void FromGrid(double framesPerStep, int64_t from, int64_t to);
   void FromEqual(int n, int64_t from, int64_t to);
   // True when any slice was placed or changed by hand since the last auto-slice (asks before replacing).
   bool HandEdited() const { return mHandEdited; }
   void MarkHandEdited(bool v) { mHandEdited = v; }
   bool AnySolo() const;

   std::string Serialize() const;
   static bool Parse(const std::string& text, SliceSet& out);

   bool operator==(const SliceSet& o) const;
   bool operator!=(const SliceSet& o) const { return !(*this == o); }

private:
   void Fill(const std::vector<int64_t>& starts, int64_t to);
   bool mHandEdited = false;
};

// Copies every field that differs between `before` and `after` into `dst`: one edit applied to a multi-selection.
void CopyChangedFields(const SliceParams& before, const SliceParams& after, SliceParams& dst);

// The nearest frame within `window` of `frame` where the mono mix crosses zero, or `frame` when none does.
int64_t SnapToZeroCrossing(const float* l, const float* r, int64_t frames, int64_t frame, int window);

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "SliceSet.h"

// Offline render of a SliceSet over its source (docs/plans/sample-studio/SLICE-EDITOR.md §6). Each slice runs its
// own chain - read or stretch, fades, transient shaping, compressor, filter, gain and pan - then the slices are laid
// end to end. Each slice carries a tail of `xfadeMs` past its end that crossfades (linear, complementary) into the
// next slice's head, so slices nobody touched join back into the source bit for bit, and an edited one never clicks.
// The sample-wide settings (length-preserving pitch, speed, reverse, ping-pong, fades, gain) run last, on the whole.
// Worker thread only: it allocates and can take a second on a long sample. `cancel` stops it between slices.

struct SourceAudio
{
   std::vector<float> l, r; // equal length; a mono source has r == l
   double sr = 48000.0;
   int64_t Frames() const { return (int64_t)l.size(); }
};

struct SliceRenderResult
{
   std::vector<float> l, r;
   // Where each slice starts in the output before the sample-wide pass (one entry per slice, then the end), and
   // the length of that pass's input, so the panel can map the playhead back to a source frame.
   std::vector<int64_t> outStart;
   int64_t laidFrames = 0;
   bool bounced = false; // ping-pong doubled the output
};

// Per-slice renders kept between passes, keyed on the slice's bounds and settings: moving one marker re-renders the
// two slices it touches, not all sixty-four. Safe to share between a running job and the next one.
class SliceRenderCache
{
public:
   struct Entry
   {
      std::string key;
      std::vector<float> l, r;
      int64_t body = 0; // frames before the tail
   };
   bool Find(uint16_t id, const std::string& key, Entry& out);
   void Store(uint16_t id, Entry e);
   void Retain(const SliceSet& set); // drops entries for slices that no longer exist
private:
   std::mutex mMutex;
   std::map<uint16_t, Entry> mEntries;
};

// One slice through its chain: `body` frames plus a tail (when `withTail`). Exposed for the audition and the
// per-slice export.
void RenderSlice(const SourceAudio& src, const SliceSet& set, int index, bool withTail, std::vector<float>& l,
                 std::vector<float>& r, int64_t& body);

// The whole set. Returns false when cancelled.
bool RenderSliceSet(const SourceAudio& src, const SliceSet& set, SliceRenderCache* cache, const std::atomic<bool>* cancel,
                    SliceRenderResult& out);

// Length-preserving pitch for a whole buffer (Signalsmith Stretch), used by the sample-wide pitch.
void PitchShiftBuffer(std::vector<float>& l, std::vector<float>& r, double sr, float semis);

// The output frame a source frame plays at, and back, through `outStart` (before the sample-wide pass).
double SourceToLaid(const SliceSet& set, const SliceRenderResult& res, int64_t frame);
double LaidToSource(const SliceSet& set, const SliceRenderResult& res, double laid);

// The peak of a buffer pair, linear.
float PeakOf(const std::vector<float>& l, const std::vector<float>& r);

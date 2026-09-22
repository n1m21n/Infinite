#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The learned model behind the Predictive Notes node (docs/plans/prediction step-06, README §8.2).
//
// Multiple-viewpoint variable-order Markov: one model per viewpoint (pitch, onset spacing, duration,
// velocity), each blended PPM-style (Cleary & Witten 1984, escape method C) over orders
// 0..maxOrder. Deliberate simplifications versus the README table, all noted here so they are not
// mistaken for oversights:
//   * pitch is the absolute MIDI note, not interval+register. Absolute pitch keeps a learned scale
//     inside its scale when the walk continues; interval models need a separate key-fold.
//   * onset spacing is on a 1/24-beat grid (covers 1/16, 1/8 triplet, 1/16 triplet, 1/32 exactly)
//     with no micro-timing residual; spacing 0 is "same onset", which is how chords are learned.
//   * bend contour is not learned.
// Metric position (16 sixteenths per bar) is the context of the order-0 model of spacing, duration
// and velocity; higher orders condition on that viewpoint's own recent symbols.
//
// Everything a sampler touches is a fixed-capacity flat array built once by Build() and immutable
// afterwards, so the audio thread reads it with no lock and no allocation.
namespace NoteModel
{
   constexpr int kMaxOrder = 8;
   constexpr int kMaxEvents = 1024;
   constexpr int kTicksPerBeat = 24;
   constexpr int kMaxIoiTicks = 8 * kTicksPerBeat; // spacing symbol is 0..192
   constexpr int kDurBins = 16;
   constexpr int kVelBins = 8;
   constexpr int kMaxAlphabet = kMaxIoiTicks + 1;

   // Root-relative pitch (Predictive Rhythm, step-11): the key-fold this file's own comment above
   // says a non-absolute pitch model needs. Signed semitone interval from whatever root note was
   // selected at learn time, clamped to +-kRelPitchRange and stored as an offset symbol
   // 0..kRelPitchAlphabet-1 so it fits the same CtxSlot/PairSlot machinery every other viewpoint uses.
   constexpr int kRelPitchRange = 24; // +-2 octaves
   constexpr int kRelPitchAlphabet = kRelPitchRange * 2 + 1;

   enum Viewpoint { kPitch = 0, kIoi, kDur, kVel, kPitchRel, kNumViewpoints };

   // One learned note. `ioiTicks` is the spacing from the previous event's onset (0 = same onset,
   // 0 for the first event); `metric` is the sixteenth within the bar of this event's own onset.
   // `relPitch` is only populated by learners that key off a selected root (Predictive Rhythm); every
   // other producer leaves it at 0, which folds harmlessly into kPitchRel's "root" symbol.
   struct Event
   {
      uint8_t note = 60;
      uint8_t vel = 90;    // 0..127
      uint8_t metric = 0;  // 0..15
      int8_t relPitch = 0; // signed semitones from the root active when this note was learned
      uint16_t ioiTicks = 0;
      uint16_t durTicks = 0;
   };

   // Clamp + symbol conversion for relPitch <-> kPitchRel's alphabet.
   int RelPitchSymbol(int semitones);
   int SymbolToRelPitch(int symbol);

   struct CtxSlot
   {
      uint64_t key = 0; // 0 = empty
      uint32_t total = 0;
      uint16_t distinct = 0;
      uint16_t last = 0; // most recent continuation seen in this context
   };
   struct PairSlot
   {
      uint64_t key = 0;
      uint32_t count = 0;
      uint32_t pad = 0;
   };

   struct ViewModel
   {
      int alphabet = 0;
      std::vector<CtxSlot> ctx;
      std::vector<PairSlot> pair;
      uint64_t mask = 0;
   };

   struct Tables
   {
      int maxOrder = kMaxOrder;
      int nEvents = 0;
      ViewModel vp[kNumViewpoints];
      uint16_t tail[kNumViewpoints][kMaxOrder] = {}; // most recent symbol first
      float durMeanTicks[kDurBins] = {};
      float velMean[kVelBins] = {};
      int lowNote = 0, highNote = 127;
      std::vector<Event> events; // the training data; main thread only (save, meter)
   };

   // Symbol mapping.
   int DurBin(int ticks);
   int VelBin(int vel127);
   int MetricOf(double beatInBar);            // 0..15
   int SnapTicks(double beats);               // nearest 1/24 beat

   // Builds immutable tables from events. Worker thread; allocates.
   Tables* Build(const std::vector<Event>& events);

   // Save / load: versioned base64 of the training events. The tables are a pure function of the
   // events, so a loaded patch rebuilds bit-identical tables and plays without re-learning.
   std::string EncodeEvents(const std::vector<Event>& events);
   bool DecodeEvents(const std::string& text, std::vector<Event>& out);

   // Held-out check for the Learn meter (README §8.3): trains on the first 80% and returns the mean
   // cross-entropy per event, in bits, over pitch + spacing for the last 20% - once for the full
   // blend at `maxOrder` and once for the order-0 baseline. False when there is too little data.
   bool HeldOutCrossEntropy(const std::vector<Event>& events, int maxOrder, float& ceModel, float& ceBaseline);

   struct Params
   {
      float stray = 0.5f;   // 0 replay .. 0.5 faithful .. 1 uniform in range
      int memory = 4;       // max context order, 0..kMaxOrder
      float lengthSpread = 0.0f;
      float velSpread = 0.0f;
      int lowNote = 36, highNote = 96;
   };

   struct Out
   {
      int note = 60;
      float velocity = 0.7f;
      double onsetBeats = 0.0;
      double durBeats = 0.25;
      int ioiTicks = 0;
   };

   // Audio-thread sampler. No allocation: everything is a fixed member array. One Player follows one
   // Tables; when the tables are swapped the caller Reset()s it.
   class Player
   {
   public:
      void Reset(const Tables* t, uint32_t seed);
      // Next event after an onset at `prevOnsetBeats`.
      void Next(const Tables& t, const Params& p, double prevOnsetBeats, double beatsPerBar, Out& o);
      // Predictive Rhythm (step-11): samples rhythm/duration/velocity exactly like Next(), but
      // samples pitch from kPitchRel instead of kPitch and resolves it against `rootNote` - the
      // node's currently-selected root, which may differ from the root the pattern was learned
      // against (changing it transposes playback without relearning).
      void NextRhythm(const Tables& t, const Params& p, double prevOnsetBeats, double beatsPerBar,
                       int rootNote, Out& o);
      const Tables* Bound() const { return mBound; }

   private:
      int SampleView(const Tables& t, int v, int maxOrder, int metricCtx, float stray, bool replay, const Params& p);
      float Rand01();

      const Tables* mBound = nullptr;
      uint16_t mHist[kNumViewpoints][kMaxOrder] = {};
      int mHistLen[kNumViewpoints] = {};
      uint32_t mRng = 1;
      float mScratch[kMaxAlphabet + 4] = {};
   };
}

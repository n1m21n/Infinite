#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/QuantizedRestart.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioChordProgressionNode;

// Turbo: Chord Progression. A note source that plays a looped list of chords,
// each one held for its own length in bars, locked to the transport. Every
// chord is a set of keys on a two-octave keyboard (bit i of chordMask = key i,
// counted up from C of baseOctave), edited by clicking the keys in the node
// body or filled from a root + quality builder.
//
// Two-object rule as every note node: this INode owns the audio half, which
// reads the transport and emits the note-ons/offs; CookIfNeeded only pushes
// params.
class ChordProgressionNode : public INode, public INoteSource
{
public:
   static constexpr int kMaxChords = 16;
   static constexpr int kKeys = 24; // two octaves in the original saved mask
   // Turbo 0.51: four octaves per chord. Keys 0..23 live in chordMask, keys
   // 24..47 in maskHi, so old patches load unchanged.
   static constexpr int kKeysTotal = 48;
   static constexpr float kMinBars = 1.0f / 16.0f;

   static INode* Create() { return new ChordProgressionNode(); }
   ChordProgressionNode();
   ~ChordProgressionNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   int chordCount = 4;       // 1..kMaxChords
   int baseOctave = 3;       // -1..7, note/12-1 convention: 3 = C3 at key 0
   int transpose = 0;        // semitones, whole progression
   float velocity = 0.8f;    // 0..1
   float gate = 1.0f;        // 0.05..1 of each chord's length, 1 = legato
   bool bass = true;         // add the chord's lowest note an octave down

   // How each chord is played. Appended after the original params, so the
   // older knobs keep their modulation pin numbers.
   enum PlayMode
   {
      kBlock = 0,     // all notes together
      kStrumUp,       // low to high, `strum` ms apart
      kStrumDown,     // high to low
      kArpUp,         // one note per `rate` step, low to high
      kArpDown,
      kArpUpDown,     // ping-pong, ends not repeated
      kArpRandom,
      kPulse,         // the whole chord re-struck every step
      kAlberti,       // low - high - middle - high
      kBassChord,     // bass on the step, upper notes on the next (oom-pah)
      kNumPlayModes
   };
   static const std::vector<std::string>& PlayModeNames();
   static bool IsStepMode(int mode) { return mode >= kArpUp && mode < kNumPlayModes; }

   int playMode = kBlock;
   int rateDiv = 9;          // MusicTime::RateDivision, default 1/8 (kEighth)
   float strumMs = 40.0f;    // 0..250, strum modes
   int arpOctaves = 1;       // 1..3, arp modes

   // When on, each chord that starts playing sets the global key (its root)
   // and a matching scale, so Quantizer, Random Note and other key-aware
   // nodes follow the progression.
   bool setsKey = false;

   int chordMask[kMaxChords];
   float chordBars[kMaxChords]; // 1/16..16 (Turbo 0.51: was 0.5..16)
   // Turbo 0.51, appended to the saved keys: keys 24..47 and a slash bass
   // pitch class (-1 = none, else 0..11, played below the chord).
   int maskHi[kMaxChords];
   int slashBass[kMaxChords];

   uint64_t FullMask(int i) const
   {
      return (uint64_t)((unsigned)chordMask[i] & 0xFFFFFFu) | ((uint64_t)((unsigned)maskHi[i] & 0xFFFFFFu) << kKeys);
   }
   void SetFullMask(int i, uint64_t m)
   {
      chordMask[i] = (int)(m & 0xFFFFFFu);
      maskHi[i] = (int)((m >> kKeys) & 0xFFFFFFu);
   }

   // Slot editing (main thread). Each shifts every per-slot array and keeps
   // chordCount (max 16) and `selected` valid; callers push the undo step.
   struct Slot { int lo = 0, hi = 0, slash = -1; float bars = 1.0f; };
   Slot GetSlot(int i) const;
   void SetSlot(int i, const Slot& s);
   bool InsertSlot(int at, const Slot& s);   // at 0..chordCount; false when full
   bool DuplicateSlot(int i);                // copy lands right after i
   bool DeleteSlot(int i);                   // never below one chord
   bool MoveSlot(int from, int to);          // `to` = final index

   // Keyboard audition (main thread -> audio atomics): the notes sound until
   // PreviewNotes(.., false), or 1.5 s (so a lost mouse-up never sticks).
   void PreviewNotes(uint64_t fullMask, bool on);
   int viewOctave = 0; // UI only: which two of the four octaves the keyboard shows (0..2)

   // Turbo 0.50: quantized restart. RequestRestart() arms a re-anchor: at the
   // next grid line (restartQuant, transport-beat based) the progression
   // starts again from chord 1. The anchor itself is not saved (a loaded
   // patch starts from the transport's bar 1 as before).
   static const std::vector<std::string>& RestartQuantNames();
   int restartQuant = QuantizedRestart::kBar; // QuantizedRestart::Quant
   void RequestRestart();
   bool RestartArmed() const; // a restart waits for its grid line

   // UI-only state (saved so a reopened patch lands on the same chord).
   int selected = 0;
   int builderRoot = 0;      // 0..11
   int builderQuality = 0;   // index into QualityNames()

   // Main-thread readouts published by the audio half.
   int PlayingIndex() const;       // -1 while the transport is stopped
   float PlayingProgress() const;  // 0..1 through the playing chord
   float TotalBars() const;

   std::string ChordName(int index) const;

   static const std::vector<std::string>& QualityNames();
   // Turbo: group of each quality ("triads", "7ths", "altered"...) and the
   // order the builder dropdown lists them in (quality indices).
   static const std::vector<std::string>& QualityCategories();
   static const std::vector<int>& QualityDisplayOrder();
   static uint64_t BuildMask(int rootPc, int quality);
   // Next inversion, wrapping to root position after the last one.
   static uint64_t InvertMask(uint64_t mask);
   // `slash` = slash bass pitch class (-1 = the lowest key is the bass).
   static std::string NameForMask(uint64_t mask, int slash = -1);
   // Root pitch class (0..11) and quality index. True when the set matches a
   // table quality exactly; otherwise false with quality = the nearest family
   // (maj, min, 7, maj7, dim, sus...) from interval analysis, or -1.
   static bool AnalyseMask(uint64_t mask, int& rootPc, int& quality, int slash = -1);

   // Extension chips: intervals relative to the detected root, toggled on the
   // current mask (not overwriting it).
   static constexpr int kNumExtensions = 14;
   static const char* ExtensionName(int ext);
   static bool HasExtension(uint64_t mask, int rootPc, int ext);
   static uint64_t ToggleExtension(uint64_t mask, int rootPc, int ext);
   // Voicing ops: 0 close, 1 drop 2, 2 spread, 3 octave up, 4 octave down.
   static uint64_t Voice(uint64_t mask, int op);
   // The inversion / octave of `mask` that moves least from `prev` (0 = none).
   static uint64_t VoiceLead(uint64_t prev, uint64_t mask);
   // "Cmaj7(9,11)/E", "Am7", "F#m7b5", "C6/9" -> mask (low two octaves) + slash.
   static bool ParseChordSymbol(const std::string& text, uint64_t& mask, int& slash);
   // Length text: "2", "1.5", "3b" / "3 beats", "1.2" (bar.beat), "2:2" -> bars.
   static bool ParseBarsText(const std::string& text, double beatsPerBar, float& outBars);
   static std::string BarsLabel(float bars, double beatsPerBar); // "2 bar", "3 beat"
   // A scale (MusicTime::ScaleType) that fits a chord quality, -1 = none.
   static int ScaleForQuality(int quality);

private:
   std::unique_ptr<AudioChordProgressionNode> mAudioNode;
   int mLastCookFrame = -1;
   int mLastPublished = -1;
};

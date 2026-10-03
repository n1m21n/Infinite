#pragma once

#include <memory>
#include <string>
#include <vector>

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
   static constexpr int kKeys = 24; // two octaves per chord

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
   float chordBars[kMaxChords]; // 0.5..16

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
   static int BuildMask(int rootPc, int quality);
   static int InvertMask(int mask);
   static std::string NameForMask(int mask);
   // Root pitch class (0..11) and quality index of a recognised chord;
   // false (root = lowest note, quality -1) when it matches no quality.
   static bool AnalyseMask(int mask, int& rootPc, int& quality);
   // A scale (MusicTime::ScaleType) that fits a chord quality, -1 = none.
   static int ScaleForQuality(int quality);

private:
   std::unique_ptr<AudioChordProgressionNode> mAudioNode;
   int mLastCookFrame = -1;
   int mLastPublished = -1;
};

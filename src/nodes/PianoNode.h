#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioPianoNode;

// Physically modelled piano. Each note is struck by a felt hammer whose force
// grows as a power of its compression (Chaigne & Askenfelt 1994) against 1-3
// unison strings and a slower second polarisation. Each string is a waveguide
// loop: a loss filter set per note for its decay, four dispersion allpasses
// fitted so the partials stretch sharp like a stiff string (Van Duyne & Smith
// 1994; Rauhala & Valimaki 2006) and a Thiran allpass for the fraction of a
// sample, so the treble keeps its sustain. Detuned unisons give the beating
// and two-stage decay; a dense modal soundboard (DenseBody.h) colours the sum, wide in stereo. Dampers
// stop a note on note-off, except in the top octave and a half, which has
// none. Note-driven, 16 voices.
//
// model also picks the two electric pianos (ElectricPiano.h): a hammer on a
// Rhodes tine or a Wurlitzer reed, read by its pickup, through an amp. There
// the knobs keep their place: tone voices the pickup, detune mistunes the
// fork, stiffness is the bell of the overtones, body the amp's speaker.
class PianoNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 16;

   enum Model
   {
      kGrand = 0,
      kRhodes,
      kWurlitzer,
   };
   static const std::vector<std::string>& ModelNames();

   static INode* Create() { return new PianoNode(); }
   PianoNode();
   ~PianoNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   bool SweepSpectralSignature() const override { return true; }
   int SweepMeasureBlocks() const override { return 16; }
   void SweepPostAlter(const std::string& paramName, int& frameId) override;

   int ActiveVoices() const;

   int model = kGrand;
   float hardness = 0.5f;  // 0..1, hammer felt: soft and round -> hard and bright
   float tone = 0.5f;      // 0..1, how fast the upper partials die against the fundamental
   float decay = 0.5f;     // 0..1, sustain while held (0.5 = a grand's)
   float detune = 0.3f;    // 0..1, spread of the unison strings (0.3 = about 2 cents)
   float stiffness = 0.5f; // 0..1, inharmonicity: 0 harmonic, 0.5 a grand, 1 an upright's bass
   float body = 0.5f;      // 0..1, bare strings -> soundboard
   float release = 250.0f; // ms, how fast the dampers stop a note
   float tremolo = 0.0f;   // 0..1, electric only: the Wurlitzer's 5.6 Hz vibrato, a Suitcase's stereo pan
   float volume = 0.8f;

   NoteCable noteInput;

private:
   std::unique_ptr<AudioPianoNode> mAudioNode;
   int mLastCookFrame = -1;
};

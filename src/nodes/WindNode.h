#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioWindNode;

// Physically modelled wind instrument (digital waveguide bore, Smith 1986;
// McIntyre, Schumacher & Woodhouse 1983). Each note is a bore delay line
// closed by an excitation: a reed (clarinet: cylindrical bore, odd
// harmonics), a reed part-way along a conical bore (sax), an air jet across
// an open pipe (flute) or the player's lips (brass: a resonant lip valve
// that locks to one bore mode). Breath pressure follows the note (attack,
// release) with breath noise and vibrato on top. Note-driven, 8 voices.
class WindNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;

   enum Instrument
   {
      kClarinet = 0,
      kSax,
      kFlute,
      kBrass,
      kInstrumentCount
   };
   static const std::vector<std::string>& InstrumentNames();

   static INode* Create() { return new WindNode(); }
   WindNode();
   ~WindNode() override;

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

   int instrument = kClarinet;
   float breath = 0.6f;     // 0..1, blowing pressure: soft and dark -> loud, bright, overblown
   float tone = 0.5f;       // 0..1, bell loss: dull -> bright
   float noise = 0.15f;     // 0..1, breath noise in the air stream
   float vibrato = 0.0f;    // 0..1, 5.5 Hz breath vibrato depth
   float attack = 30.0f;    // ms, breath rise at note-on
   float release = 80.0f;   // ms, breath fall at note-off
   float volume = 0.8f;

   NoteCable noteInput;

private:
   std::unique_ptr<AudioWindNode> mAudioNode;
   int mLastCookFrame = -1;
};

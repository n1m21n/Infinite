#pragma once

#include <memory>
#include <string>

#include "audio/dsp/MetallicResonator.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioMetallicNode;

// Physical Modelling Metallic Synthesizer Node.
// Simulates bells, gongs, metallic bars, plates, chimes, tines, and strings
// via extended Karplus-Strong waveguide synthesis and inharmonic modal resonator banks.
//
// Polyphonic note-driven when cabled to a note stream (slot 0);
// free-running at `frequency` when unpatched for instant tactile sound design.
class MetallicNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;
   static constexpr int kScopeCacheCapacity = 128;

   static INode* Create() { return new MetallicNode(); }
   MetallicNode();
   ~MetallicNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   // Decimated output trace for the node body oscilloscope
   int ReadScope(float* out, int capacity);

   // Sounding voices count published by audio thread
   int ActiveVoices() const;

   // Manual strike trigger from UI button
   void TriggerStrike();

   // Apply material physics preset defaults
   void SetMaterialPreset(int preset);

   // ------------------------------------------------ Parameters
   int material = MetallicDsp::kSteel;  // Material physics model preset

   // Physical acoustics
   float transient = 1.0f;              // 0.05 .. 2.0 mallet strike hardness & snap
   float decay = 2.2f;                  // 0.05 .. 10.0s resonance decay time
   float stiffness = 0.25f;             // 0.0 .. 2.0 inharmonic dispersion / geometry mode

   // Tuning & Pitch
   float frequency = 220.0f;            // Free-running base pitch in Hz
   int octave = 0;                      // +/- 4 octaves
   int semi = 0;                        // +/- 12 semitones
   float fine = 0.0f;                   // +/- 50 cents fine tuning
   float glide = 0.0f;                  // Portamento time in seconds

   // Output Filter & Color
   int filterType = MetallicDsp::kFilterLP24; // LP12, LP24, HP12, BP, Bypass
   float filterCutoff = 12000.0f;       // 20 .. 20000 Hz
   float filterResonance = 0.2f;        // 0.0 .. 1.0 (mapped to Q)
   float drive = 0.15f;                 // 0.0 .. 1.0 metallic body saturation
   float width = 0.6f;                  // 0.0 .. 1.0 stereo modal diffusion
   float volume = 0.8f;                 // 0.0 .. 2.0 master output volume

   NoteCable noteInput;

   // UI oscilloscope cache
   float scopeCache[kScopeCacheCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

private:
   std::unique_ptr<AudioMetallicNode> mAudioNode;
   int mLastCookFrame = -1;
};

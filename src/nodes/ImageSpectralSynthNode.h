#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "audio/MusicTime.h"
#include "audio/dsp/SpectralAdditiveSynth.h"
#include "core/INode.h"
#include "core/ImageCable.h"
#include "core/NoteCable.h"

class AudioImageSpectralNode;

// Image-to-Spectral Additive Synthesizer Node (MetaSynth / Aphex Twin Spectrogram Resynthesis)
// Converts arbitrary 2D visual images, glyphs, drawings, and live video into a dense bank
// of 64..256 sine partials. Scans horizontally (Time axis X) across frequency bins (Vertical axis Y)
// with sample-accurate linear amplitude de-zippering, color-to-pan spatialization, transport sync,
// and polyphonic MIDI transpose.
class ImageSpectralSynthNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;
   static constexpr int kMaxUnison = 8;
   static constexpr int kScopeCapacity = 128;

   static INode* Create() { return new ImageSpectralSynthNode(); }
   static const std::vector<std::string>& ScanModeNames();
   static const std::vector<std::string>& FrequencyScaleNames();
   static const std::vector<std::string>& ColorModeNames();
   static const std::vector<std::string>& PartialsCountNames();
   static const std::vector<std::string>& FilterTypeNames();

   ImageSpectralSynthNode();
   ~ImageSpectralSynthNode() override;

   unsigned int GetOutputTexture() override { return mPreviewTex; }
   int GetOutputWidth() const override { return mPreviewSize; }
   int GetOutputHeight() const override { return mPreviewSize; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "image";
      if (slot == 1) return "note";
      return nullptr;
   }

   ImageCable& TextureInput() { return mTextureInput; }
   NoteCable& NoteInput() { return mNoteInput; }

   NoteCable* NoteInputSlot(int slot) override { return slot == 1 ? &mNoteInput : nullptr; }

   int ReadScope(float* out, int capacity);
   int ActiveVoices() const;
   float Playhead() const;
   void TriggerScan();

   // ------------------------------------------------ Parameters
   // Scanning & Playhead
   int scanMode = SpectralAdditiveDsp::kScanBpmSync;
   int rate = MusicTime::k1Bar;
   float scanSpeed = 1.0f;     // Speed multiplier (0.01 .. 20.0 Hz or rate scale)
   float position = 0.0f;      // 0..1 manual scrub position
   int direction = 0;          // 0 = Forward, 1 = Reverse

   // Frequency & Partial Spectrum
   int partialsChoice = 1;     // 0: 64 partials, 1: 128 partials, 2: 256 partials
   int freqScale = SpectralAdditiveDsp::kScaleLogarithmic;
   float minFreq = 40.0f;      // Hz (bottom of spectrogram)
   float maxFreq = 16000.0f;   // Hz (top of spectrogram)
   float rootFreq = 261.63f;   // Middle C nominal root Hz for harmonic/pitch

   // Pitch Tuning
   int octave = 0;             // +/- 4
   int semi = 0;               // +/- 12
   float fine = 0.0f;          // +/- 50 cents
   float glide = 0.0f;         // portamento seconds

   // Color & Image Conditioning
   int colorMode = SpectralAdditiveDsp::kColorHuePan;
   float threshold = 0.02f;    // 0..0.5 noise gate
   float contrast = 1.2f;      // 0.2..4.0 gamma power
   float brightness = 1.0f;    // 0.0..3.0 gain boost
   int invert = 0;             // 0 = normal, 1 = inverted

   // Voice & Output
   float volume = 0.8f;
   float pan = 0.0f;
   float stereoWidth = 1.0f;   // 0..2.0
   int unison = 1;             // 1..8 unison detuned voices
   float detune = 8.0f;        // cents

   // Analog Filter
   int filterType = 1;         // 0: Off, 1: LP12, 2: LP24, 3: HP12, 4: BP12
   float cutoff = 18000.0f;    // Hz
   float resonance = 0.15f;    // 0..1
   float drive = 0.0f;         // 0..1

   // Amp Envelope
   float ampAttack = 10.0f;    // ms
   float ampDecay = 200.0f;    // ms
   float ampSustain = 0.85f;   // 0..1
   float ampRelease = 250.0f;  // ms

   // UI oscilloscope cache
   float scopeCache[kScopeCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

private:
   ImageCable mTextureInput;
   NoteCable mNoteInput;
   std::unique_ptr<AudioImageSpectralNode> mAudioNode;

   // GPU Readback & Preview FBO
   unsigned int mFbo = 0;
   unsigned int mPreviewTex = 0;
   int mPreviewSize = 256;
   std::vector<uint8_t> mPixels;

   int mLastCookFrame = -1;
   unsigned long long mLastTexRev = 0;

   void EnsurePreviewResources(int size);
   void RenderPreview(int frameId);
};

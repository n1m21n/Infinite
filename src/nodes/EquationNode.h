#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/dsp/EquationDsp.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioEquationNode;

// Desmos-style Equation Oscillator Node.
// Graphs mathematical equations y = f(x, a, b, c, d) in an interactive X-Y
// Cartesian plane and evaluates them as an anti-aliased polyphonic synthesizer
// via real-time 10-level Fourier mip pyramid wavetables.
class EquationNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;
   static constexpr int kMaxUnison = 8;
   static constexpr int kScopeCapacity = 128;

   struct Preset
   {
      const char* name;
      const char* formula;
      int domainMode;
      float a, b, c, d;
   };

   static INode* Create() { return new EquationNode(); }
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   static const std::vector<std::string>& DomainNames();
   static const std::vector<std::string>& FilterTypeNames();

   EquationNode();
   ~EquationNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "notes";
      return nullptr;
   }

   NoteCable& NoteInput() { return mNoteInput; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &mNoteInput : nullptr; }

   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      if (paramName == "detune" || paramName == "stereoWidth")
         return { { "unison", 2.0f } };
      if (paramName == "filterAttack" || paramName == "filterDecay" ||
          paramName == "filterSustain" || paramName == "filterRelease")
         return { { "filterAmount", 2.0f } };
      return {};
   }

   int ReadScope(float* out, int capacity);
   int ActiveVoices() const;

   void LoadPreset(int index);
   bool CompileEquation();
   const std::string& LastError() const { return mLastError; }
   const std::vector<float>& PreviewCurve() const { return mPreviewCurve; }

   // ------------------------------------------------ Parameters
   std::string formula = "sin(2*pi*x) + a*sin(4*pi*x) + b*sin(6*pi*x) + c*sin(8*pi*x)";
   int presetIndex = 1;
   int domainMode = EquationDsp::kDomainZeroToOne;

   // Expression Knobs
   float knobA = 0.5f;
   float knobB = 0.25f;
   float knobC = 0.125f;
   float knobD = 0.0f;

   // Tuning & Voice
   float volume = 0.8f;
   float pan = 0.0f;
   float frequency = 220.0f;   // free-running Hz when no note cable is attached
   int octave = 0;             // +/- 4
   int semi = 0;               // +/- 12
   float fine = 0.0f;          // +/- 50 cents
   float glide = 0.0f;         // portamento seconds

   // Unison
   int unison = 1;             // 1..8 voices
   float detune = 12.0f;       // cents
   float stereoWidth = 0.5f;   // 0..1

   // Amp Envelope
   float ampAttack = 5.0f;     // ms
   float ampDecay = 250.0f;    // ms
   float ampSustain = 0.75f;   // 0..1
   float ampRelease = 200.0f;  // ms

   // Filter & Envelope
   int filterType = 1;         // 0: Off, 1: LP12, 2: LP24, 3: HP12, 4: BP
   float cutoff = 12000.0f;    // Hz
   float resonance = 0.2f;     // 0..1
   float filterAmount = 0.0f;  // -8..8 octaves
   float filterAttack = 5.0f;  // ms
   float filterDecay = 300.0f; // ms
   float filterSustain = 0.4f; // 0..1
   float filterRelease = 250.0f; // ms

   // Saturation / Drive
   float drive = 0.0f;

   // UI oscilloscope cache
   float scopeCache[kScopeCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

private:
   NoteCable mNoteInput;
   std::unique_ptr<AudioEquationNode> mAudioNode;

   EquationDsp::AstNodePtr mAst;
   std::string mLastError;
   std::vector<float> mPreviewCurve;

   int mLastCookFrame = -1;
   std::string mLastFormula;
   int mLastDomainMode = -1;
   float mLastKnobA = -999.0f;
   float mLastKnobB = -999.0f;
   float mLastKnobC = -999.0f;
   float mLastKnobD = -999.0f;

   void RebuildBank();
};

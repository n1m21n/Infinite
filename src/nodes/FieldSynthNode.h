#pragma once

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"
#include "Modulation.h"
#include "field/ParamTable.h"
#include "field/PinTable.h"
#include "field/SampleProgram.h"
#include "field/FieldDevice.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class AudioFieldSynthNode;

// Field 'synth' node (Step 21): a polyphonic note-driven synthesizer powered
// by Field's sample-domain register machine compiler.
// Two-object pair: FieldSynthNode (INode on main thread) and
// AudioFieldSynthNode (AudioNode on audio thread).
class FieldSynthNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new FieldSynthNode(); }
   FieldSynthNode();
   ~FieldSynthNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   // Inputs: slot 0 is notes, slot 1 is audio in (optional, e.g. for vocoding or audio-rate FM).
   // Dynamic pins, Step 25 (OPEN-D second audio input): declared `input
   // sample audio <name>` pins get a real connectable AudioCable each,
   // starting right after the native "notes"/"in" pair - same layering
   // scheme as FieldPixelNode::DeclaredImageInput.
   int NativeInputCount() const { return 2; }
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   AudioCable* AudioInputSlot(int slot) override
   {
      if (slot == 1) return &audioInput;
      int declIdx = slot - NativeInputCount();
      if (declIdx < 0 || declIdx >= Field::PinTable::kMaxDeclaredPins) return nullptr;
      int seen = 0;
      for (const auto& p : mInputPins.Pins())
      {
         if (!p.isDeclared || p.typeName != "audio") continue;
         if (seen == declIdx) return &mDeclaredAudioInputs[declIdx];
         seen++;
      }
      return nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "notes";
      if (slot == 1) return "in";
      int declIdx = slot - NativeInputCount();
      if (declIdx < 0) return nullptr;
      int seen = 0;
      for (const auto& p : mInputPins.Pins())
      {
         if (!p.isDeclared || p.typeName != "audio") continue;
         if (seen == declIdx) return p.name.c_str();
         seen++;
      }
      return nullptr;
   }

   int NativeOutputCount() const { return exposeRmsOutput ? 2 : 1; }
   int DeclaredOutputCount() const
   {
      int n = 0;
      for (const auto& p : mOutputPins.Pins())
         if (p.isDeclared) n++;
      return n;
   }
   int OutputCount() const override { return NativeOutputCount() + DeclaredOutputCount(); }
   const char* OutputLabel(int index) const override
   {
      if (index == 0) return "out";
      if (exposeRmsOutput && index == 1) return "rms";
      int declIdx = index - NativeOutputCount();
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx) return p.name.c_str();
         seen++;
      }
      return "out";
   }

   bool IsAudioOutputIndex(int index) const override
   {
      if (index == 0) return true;
      int declIdx = index - NativeOutputCount();
      if (declIdx < 0) return false;
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx) return p.typeName == "audio";
         seen++;
      }
      return false;
   }

   IModulator* ModulatorOutput(int index) override
   {
      if (exposeRmsOutput && index == 1)
         return static_cast<IModulator*>(&mRmsOutput);
      int declIdx = index - NativeOutputCount();
      if (declIdx < 0)
         return nullptr;
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx)
         {
            if (p.typeName == "audio")
               return nullptr;
            if (p.domainName == "frame")
               return static_cast<IModulator*>(&mRmsOutput);
            return (declIdx < Field::PinTable::kMaxDeclaredPins) ? &mDeclaredOutputMods[declIdx] : nullptr;
         }
         seen++;
      }
      return nullptr;
   }

   bool exposeRmsOutput = false;
   std::string pinRefusal;

   const Field::PinTable& OutputPinTable() const { return mOutputPins; }
   const Field::PinTable& InputPinTable() const { return mInputPins; }

   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);

   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   uint64_t FaultCount() const;
   bool ReadRmsLatest(float& out);
   int ReadScope(float* out, int capacity);

   std::string code;
   int maxVoices = 8;
   int presetIndex = 0;

   NoteCable noteInput;
   AudioCable audioInput;

   static constexpr int kScopeCacheCapacity = 128;
   float scopeCache[kScopeCacheCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

   struct RmsOutput : public IModulator
   {
      FieldSynthNode* owner = nullptr;
      float Value01() override
      {
         float v = 0.0f;
         if (owner)
            owner->ReadRmsLatest(v);
         return v;
      }
   };

private:
   std::unique_ptr<AudioFieldSynthNode> mAudioNode;
   RmsOutput mRmsOutput;
   Field::ParamTable mParamTable;

   Field::PinTable mOutputPins;
   Field::PinTable mInputPins;
   struct DeclaredOutputPlaceholder : public IModulator
   {
      float Value01() override { return 0.0f; }
   };
   DeclaredOutputPlaceholder mDeclaredOutputMods[Field::PinTable::kMaxDeclaredPins];
   // Step 25 (OPEN-D): one real AudioCable per declared `input sample audio
   // <name>` pin - see AudioInputSlot() above.
   AudioCable mDeclaredAudioInputs[Field::PinTable::kMaxDeclaredPins];

   std::vector<Field::SampleParamSlot> mCompiledParams;
   Field::SampleProgram mLastCompiled;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   int mLastCookFrame = -1;
};

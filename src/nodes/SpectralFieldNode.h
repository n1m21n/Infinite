#pragma once

#include "core/AudioCable.h"
#include "core/INode.h"
#include "field/FieldDevice.h"
#include "field/ParamTable.h"
#include "field/PinTable.h"
#include "field/SampleProgram.h"

#include <memory>
#include <string>
#include <vector>

class AudioSpectralFieldNode;

// Spectral Field: audio in -> STFT -> a Field kernel run once per bin ->
// inverse STFT -> audio out (docs/plans/spectral-field/README.md). Two-object
// pair like Field Effect: this INode compiles on the main thread,
// AudioSpectralFieldNode (in the .cpp) runs SpectralFieldEngine on the audio
// thread. Card: kernel params, spectrum, Size, Mix.
class SpectralFieldNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new SpectralFieldNode(); }
   SpectralFieldNode();
   ~SpectralFieldNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   INode* BypassSource() override { return audioInput.GetSource(); }
   // Slot 0 "in"; slot 1 is the kernel's 'input bin audio B' when declared.
   int NativeInputCount() const { return 1; }
   AudioCable* AudioInputSlot(int slot) override
   {
      if (slot == 0) return &audioInput;
      if (slot == 1 && !mBinInputName.empty()) return &binInput;
      return nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      if (slot == 0) return "in";
      if (slot == 1 && !mBinInputName.empty()) return mBinInputName.c_str();
      return nullptr;
   }

   bool Apply();
   const std::string& LastError() const { return mLastError; }
   NodeIssue Issue() const override { return NodeIssues::FieldCompile(mLastError); }
   std::string CodeError() const override { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   std::string pinRefusal;

   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);
   int presetIndex = 0;

   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   // Size choices: 512 .. 8192 (index 0..4), default 2048.
   static const std::vector<std::string>& SizeNames();
   static int SizeLog2ForIndex(int index);
   int sizeIndex = 2;
   float mix = 1.0f;

   // Latest 64-band input and output spectra (peak mag per band, linear),
   // drained from the audio thread. False until the first frame lands.
   static constexpr int kBands = 64;
   bool ReadSpectrum(float* bandsIn, float* bandsOut);
   float spectrumIn[kBands] = {};
   float spectrumOut[kBands] = {};
   bool spectrumValid = false;

   std::string code;
   AudioCable audioInput;
   AudioCable binInput;

private:
   std::unique_ptr<AudioSpectralFieldNode> mAudioNode;
   Field::ParamTable mParamTable;
   Field::PinTable mInputPins;
   std::vector<Field::SampleParamSlot> mCompiledParams;
   Field::SampleProgram mLastCompiled;
   std::string mBinInputName;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   int mLastCookFrame = -1;
};

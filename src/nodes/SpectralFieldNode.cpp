#include "SpectralFieldNode.h"

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "audio/ParamMailbox.h"
#include "audio/SampleSlot.h"
#include "audio/dsp/SpectralFieldEngine.h"
#include "field/BackendRegister.h"
#include "field/SampleProgram.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
   constexpr float kOutClamp = 4.0f;
   constexpr int kDrySize = SpectralFieldEngine::kMaxSize * 2; // power of two > max latency
}

// ------------------------------------------------------------- audio thread
class AudioSpectralFieldNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mEngine.Prepare(sampleRate);
      mEngine.SetSizeLog2(mSizeLog2.load(std::memory_order_relaxed));
      for (auto& d : mDry)
         d.assign(kDrySize, 0.0f);
      mDryPos = 0;
      mLatency.store(mEngine.Latency(), std::memory_order_relaxed);
      mPrepared = true;
   }

   void Reset() override
   {
      if (!mPrepared)
         return;
      mEngine.Reset();
      for (auto& d : mDry)
         std::fill(d.begin(), d.end(), 0.0f);
      mDryPos = 0;
   }

   int LatencySamples() const override { return mLatency.load(std::memory_order_relaxed); }

   // Main thread only.
   void PushProgram(Field::SampleProgram* prog) { mProgramSlot.Push(prog); }
   void DrainRetired() { mProgramSlot.DrainRetired(); }
   void PushParam(int mailboxId, float value) { mMailbox.Push(mailboxId, value); }
   void SetSizeLog2(int log2) { mSizeLog2.store(log2, std::memory_order_relaxed); }
   void SetMix(float m) { mMix.store(m, std::memory_order_relaxed); }
   MeterRing& SpectrumRing() { return mSpectrumRing; }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& buffer) override
   {
      if (mProgramSlot.SwapIn())
      {
         mActiveProgram = mProgramSlot.Active();
         mEngine.ProgramSwapped();
      }
      const int frames = buffer.numFrames;
      if (!mPrepared)
      {
         for (int ch = 0; ch < buffer.numChannels; ch++)
            std::fill(buffer.channels[ch], buffer.channels[ch] + frames, 0.0f);
         return;
      }

      const int log2 = mSizeLog2.load(std::memory_order_relaxed);
      if ((1 << log2) != mEngine.Size())
      {
         mEngine.SetSizeLog2(log2);
         mLatency.store(mEngine.Latency(), std::memory_order_relaxed);
      }
      const int latency = mEngine.Latency();

      const AudioBuffer* inBuf = (numInputs > 0) ? inputs[0] : nullptr;
      const AudioBuffer* bBuf = (numInputs > 1) ? inputs[1] : nullptr;
      const int inCh = (inBuf != nullptr) ? inBuf->numChannels : 0;
      const int bCh = (bBuf != nullptr) ? bBuf->numChannels : 0;
      // Stereo when either input is stereo; a mono source runs one channel.
      const int work = std::min(SpectralFieldEngine::kMaxChannels, std::max(1, std::max(inCh, bCh)));

      const Field::SampleProgram* prog =
         (mActiveProgram != nullptr && mActiveProgram->valid) ? mActiveProgram : nullptr;
      const int numParams = prog ? (int)prog->params.size() : 0;

      const float mixTarget = std::clamp(mMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
      const float mixStep = (mixTarget - mMixSmoothed) / (float)std::max(1, frames);
      const uint32_t framesBefore = mEngine.Frames();

      for (int i = 0; i < frames; i++)
      {
         for (int p = 0; p < numParams; p++)
            mParamVals[p] = mMailbox.SmoothedValue(prog->params[p].mailboxId);
         mMixSmoothed += mixStep;
         const int readPos = (mDryPos - latency) & (kDrySize - 1);
         float outv[SpectralFieldEngine::kMaxChannels] = {};
         for (int ch = 0; ch < work; ch++)
         {
            const float x = (inCh > 0) ? inBuf->channels[std::min(ch, inCh - 1)][i] : 0.0f;
            const float b = (bCh > 0) ? bBuf->channels[std::min(ch, bCh - 1)][i] : 0.0f;
            float wet = mEngine.Process(ch, x, b, prog, mParamVals);
            if (!(wet == wet))
               wet = 0.0f;
            mDry[ch][(size_t)mDryPos] = x;
            const float dry = mDry[ch][(size_t)readPos];
            outv[ch] = std::clamp(dry + (wet - dry) * mMixSmoothed, -kOutClamp, kOutClamp);
         }
         mDryPos = (mDryPos + 1) & (kDrySize - 1);
         for (int ch = 0; ch < buffer.numChannels; ch++)
            buffer.channels[ch][i] = outv[std::min(ch, work - 1)];
      }
      mMixSmoothed = mixTarget;

      if (mEngine.Frames() != framesBefore)
      {
         float bands[2 * SpectralFieldEngine::kBands];
         std::memcpy(bands, mEngine.BandsIn(), sizeof(float) * SpectralFieldEngine::kBands);
         std::memcpy(bands + SpectralFieldEngine::kBands, mEngine.BandsOut(), sizeof(float) * SpectralFieldEngine::kBands);
         mSpectrumRing.Write(bands, 2 * SpectralFieldEngine::kBands);
      }
   }

private:
   ParamMailbox mMailbox;
   SampleSlotT<Field::SampleProgram> mProgramSlot;
   Field::SampleProgram* mActiveProgram = nullptr;
   SpectralFieldEngine mEngine;
   std::vector<float> mDry[SpectralFieldEngine::kMaxChannels];
   int mDryPos = 0;
   float mParamVals[Field::kSampleMaxParams] = {};
   float mMixSmoothed = 1.0f;
   std::atomic<int> mSizeLog2 { 11 };
   std::atomic<float> mMix { 1.0f };
   std::atomic<int> mLatency { 2048 };
   MeterRing mSpectrumRing;
   double mSampleRate = 48000.0;
   bool mPrepared = false;
};

// -------------------------------------------------------------- presets
const std::vector<SpectralFieldNode::Preset>& SpectralFieldNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Low-pass",
        "param float cutoff = 2000 [20, 20000]\n"
        "mag = if(bin * binhz < cutoff, mag, 0)\n" },
      { "Gate",
        "param float thresh = -50 [-100, 0]\n"
        "mag = if(mag > 10 ^ (thresh / 20), mag, 0)\n" },
      { "Freeze",
        "param float hold = 0 [0, 1]\n"
        "state float m = 0\n"
        "state float f = 0\n"
        "m = if(hold > 0.5, m, mag)\n"
        "f = if(hold > 0.5, f, freq)\n"
        "mag = m\n"
        "freq = f\n" },
      { "Octave up",
        "mag = mag.at(bin / 2)\n"
        "freq = freq.at(bin / 2) * 2\n" },
      { "Freq shift",
        "param float shift = 200 [-2000, 2000]\n"
        "k = bin - shift / binhz\n"
        "mag = mag.at(k)\n"
        "freq = freq.at(k) + shift\n" },
      { "Flip",
        "k = bins - 1 - bin\n"
        "mag = mag.at(k)\n"
        "freq = sr / 2 - freq.at(k)\n" },
      { "Robot / whisper",
        "param float whisper = 0 [0, 1]\n"
        "phase = if(whisper > 0.5, rand() * 6.2831853, 0)\n" },
      { "Cross-synth",
        "input bin audio B\n"
        "phase = B.phase\n" },
   };
   return kPresets;
}

const std::vector<std::string>& SpectralFieldNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   return kNames;
}

const std::vector<std::string>& SpectralFieldNode::SizeNames()
{
   static const std::vector<std::string> kNames = { "512", "1024", "2048", "4096", "8192" };
   return kNames;
}

int SpectralFieldNode::SizeLog2ForIndex(int index)
{
   return SpectralFieldEngine::kMinLog2 + std::clamp(index, 0, 4);
}

void SpectralFieldNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      code = presets[index].code;
      if (Apply())
         mParamTable.ResetDeclaredToDefaults(); // a preset starts at its own declared values
   }
}

Field::DeviceFile SpectralFieldNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "bin";
   device.code = code;
   for (const auto& p : mParamTable.Params())
      if (p.isDeclared)
         device.params[p.name] = p.value;
   return device;
}

void SpectralFieldNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   if (Apply())
      mParamTable.ResetDeclaredToDefaults(); // params the file omits start at their defaults
   for (const auto& kv : device.params)
      if (Field::ParamEntry* p = mParamTable.Find(kv.first))
         p->value = kv.second;
}

// -------------------------------------------------------------- main thread
SpectralFieldNode::SpectralFieldNode()
   : mAudioNode(std::make_unique<AudioSpectralFieldNode>())
{
   code = Presets()[0].code;
   Apply();
}

SpectralFieldNode::~SpectralFieldNode() = default;

AudioNode* SpectralFieldNode::GetAudioNode() { return mAudioNode.get(); }

bool SpectralFieldNode::Apply()
{
   pinRefusal.clear();
   auto newProgram = std::make_unique<Field::SampleProgram>();
   Field::FieldError err;
   const Field::SampleProgram* prev = mLastCompiled.valid ? &mLastCompiled : nullptr;
   if (!Field::CompileSpectralProgram(code, prev, *newProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   {
      std::vector<Field::DeclaredPin> declIn;
      for (const auto& d : newProgram->declaredInputs)
         declIn.push_back({ d.name, d.typeName, d.domainName, false });
      std::string pinNotice, pinRefusalMsg;
      if (!Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, NativeInputCount(), pinNotice, pinRefusalMsg))
      {
         mLastError = pinRefusalMsg;
         pinRefusal = pinRefusalMsg;
         return false;
      }
      if (!pinNotice.empty())
         mNotice = pinNotice;
   }
   mBinInputName = newProgram->hasSpectralInput ? newProgram->spectralInputName : std::string();

   std::vector<Field::DeclaredParam> declared;
   for (const auto& p : newProgram->params)
   {
      Field::DeclaredParam dp;
      dp.name = p.name;
      dp.typeName = "float";
      dp.defaultValue = p.defaultValue;
      dp.minValue = p.minValue;
      dp.maxValue = p.maxValue;
      declared.push_back(dp);
   }
   mParamTable.Reconcile(declared, mNodeIndex, mNotice);
   mCompiledParams = newProgram->params;
   mLastCompiled = *newProgram;

   mAudioNode->PushProgram(newProgram.release());
   mLastError.clear();
   return true;
}

void SpectralFieldNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mAudioNode->DrainRetired();
   for (const auto& p : mCompiledParams)
      if (const Field::ParamEntry* entry = mParamTable.Find(p.name))
         mAudioNode->PushParam(p.mailboxId, entry->value);
   mAudioNode->SetSizeLog2(SizeLog2ForIndex(sizeIndex));
   mAudioNode->SetMix(mix);
}

bool SpectralFieldNode::ReadSpectrum(float* bandsIn, float* bandsOut)
{
   constexpr int kSet = 2 * kBands;
   float buf[MeterRing::kCapacity];
   const int count = mAudioNode->SpectrumRing().Read(buf, MeterRing::kCapacity);
   const int sets = count / kSet;
   if (sets > 0)
   {
      const float* last = buf + (sets - 1) * kSet;
      std::memcpy(spectrumIn, last, sizeof(float) * kBands);
      std::memcpy(spectrumOut, last + kBands, sizeof(float) * kBands);
      spectrumValid = true;
   }
   if (spectrumValid)
   {
      std::memcpy(bandsIn, spectrumIn, sizeof(float) * kBands);
      std::memcpy(bandsOut, spectrumOut, sizeof(float) * kBands);
   }
   return spectrumValid;
}

void SpectralFieldNode::VisitParams(ParamVisitor& v)
{
   v.Text("code", code);
   v.Int("size", sizeIndex);
   v.Float("mix", mix);
   sizeIndex = std::clamp(sizeIndex, 0, 4);
   mParamTable.VisitParams(v);
   std::string inPins = mInputPins.SerializePinMap();
   v.Text("__inputPins", inPins);
   mInputPins.DeserializePinMap(inPins);
}

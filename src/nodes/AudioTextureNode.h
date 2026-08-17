#pragma once

#include <memory>
#include <string>
#include <vector>
#include <OpenGL/gl3.h>

#include "core/INode.h"
#include "core/AudioCable.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"

class AudioTextureAudioSink;

class AudioTextureNode : public INode
{
public:
   enum Mode
   {
      kModeWaveform = 0,
      kModeSpectrum,
      kModeCount
   };

   static const std::vector<std::string>& ModeNames();
   static const std::vector<std::string>& WindowSizeNames();
   static const int kWindowSizes[];

   static INode* Create() { return new AudioTextureNode(); }
   AudioTextureNode();
   ~AudioTextureNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override;
   int GetOutputHeight() const override;
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;

   AudioNode* GetAudioNode();
   AudioCable& GetAudioInput() { return mAudioInput; }
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &mAudioInput : nullptr; }
   bool RequiresAudioProcessing() const override { return mAudioInput.IsConnected(); }

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode);
      v.Int("windowSizeIndex", windowSizeIndex);
      v.Float("gain", gain);
      v.Float("smoothing", smoothing);
   }

   int mode = kModeWaveform;
   int windowSizeIndex = 1; // 1024 default
   float gain = 1.0f;
   float smoothing = 0.0f;

private:
   void EnsureTexture(int w, int h);
   void DestroyTexture();

   std::unique_ptr<AudioTextureAudioSink> mAudioSink;
   AudioCable mAudioInput;

   unsigned int mTexture = 0;
   int mTexW = 0;
   int mTexH = 0;
   unsigned long long mRevision = 0;

   std::vector<float> mWindow;
   std::vector<float> mSmoothedData;
   int mLastCookFrame = -1;
};

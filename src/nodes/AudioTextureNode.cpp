#include "AudioTextureNode.h"
#include "audio/dsp/WaveTerrainDsp.h"
#include <algorithm>
#include <cmath>

static const std::vector<std::string> kModeNames = { "Waveform", "Spectrum", "Oscilloscope", "Spectrogram" };
static const std::vector<std::string> kWindowSizeNames = { "512", "1024", "2048", "4096" };
const int AudioTextureNode::kWindowSizes[] = { 512, 1024, 2048, 4096 };

const std::vector<std::string>& AudioTextureNode::ModeNames() { return kModeNames; }
const std::vector<std::string>& AudioTextureNode::WindowSizeNames() { return kWindowSizeNames; }

// ---------------------------------------------------------------------------
// AudioTextureAudioSink: Audio Thread Sink
// ---------------------------------------------------------------------------
class AudioTextureAudioSink : public AudioNode
{
public:
   AudioTextureAudioSink() = default;
   ~AudioTextureAudioSink() override = default;

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      if (numFrames <= 0)
         return;

      for (int ch = 0; ch < output.numChannels; ch++)
      {
         if (output.channels[ch])
            std::fill_n(output.channels[ch], numFrames, 0.0f);
      }

      const AudioBuffer* inBuf = nullptr;
      if (inputs != nullptr)
      {
         for (int s = 0; s < numInputs; s++)
         {
            if (inputs[s] != nullptr && inputs[s]->channels[0] != nullptr)
            {
               inBuf = inputs[s];
               break;
            }
         }
      }

      if (inBuf != nullptr)
      {
         const float* inL = inBuf->channels[0];
         const float* inR = (inBuf->numChannels > 1 && inBuf->channels[1] != nullptr) ? inBuf->channels[1] : inL;

         std::vector<float> mono(numFrames);
         for (int i = 0; i < numFrames; i++)
         {
            mono[i] = (inL[i] + inR[i]) * 0.5f;
            if (output.channels[0]) output.channels[0][i] = inL[i];
            if (output.numChannels > 1 && output.channels[1]) output.channels[1][i] = inR[i];
         }
         mMeterRing.Write(mono.data(), numFrames);
      }
   }

   int ReadSamples(float* out, int capacity)
   {
      return mMeterRing.Read(out, capacity);
   }

private:
   MeterRing mMeterRing;
};

// ---------------------------------------------------------------------------
// AudioTextureNode (Main Thread INode Image Source)
// ---------------------------------------------------------------------------
AudioTextureNode::AudioTextureNode()
{
   mAudioSink = std::make_unique<AudioTextureAudioSink>();
   mWindow.assign(1024, 0.0f);
   mSmoothedData.assign(1024, 0.0f);
}

AudioTextureNode::~AudioTextureNode()
{
   DestroyTexture();
}

AudioNode* AudioTextureNode::GetAudioNode()
{
   if (!mAudioSink)
      mAudioSink = std::make_unique<AudioTextureAudioSink>();
   return mAudioSink.get();
}

unsigned int AudioTextureNode::GetOutputTexture()
{
   if (!mAudioInput.IsConnected())
      return 0;
   return mTexture;
}

int AudioTextureNode::GetOutputWidth() const
{
   return mTexW;
}

int AudioTextureNode::GetOutputHeight() const
{
   return mTexH;
}

void AudioTextureNode::EnsureTexture(int w, int h)
{
   if (mTexture != 0 && mTexW == w && mTexH == h)
      return;

   if (mTexture != 0)
      glDeleteTextures(1, &mTexture);

   mTexW = w;
   mTexH = h;

   glGenTextures(1, &mTexture);
   glBindTexture(GL_TEXTURE_2D, mTexture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mTexW, mTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glBindTexture(GL_TEXTURE_2D, 0);
}

void AudioTextureNode::DestroyTexture()
{
   if (mTexture != 0)
   {
      glDeleteTextures(1, &mTexture);
      mTexture = 0;
      mTexW = 0;
      mTexH = 0;
   }
}

void AudioTextureNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!mAudioInput.IsConnected())
      return;

   const int winSize = (mode == kModeSpectrum || mode == kModeSpectrogram)
      ? 1024
      : kWindowSizes[std::clamp(windowSizeIndex, 0, 3)];

   if ((int)mWindow.size() != winSize)
      mWindow.assign(winSize, 0.0f);

   float tempBuf[4096];
   const int readCount = mAudioSink ? mAudioSink->ReadSamples(tempBuf, winSize) : 0;
   if (readCount > 0)
   {
      if (readCount >= winSize)
      {
         mWindow.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
      }
      else
      {
         const int keep = winSize - readCount;
         std::copy(mWindow.begin() + readCount, mWindow.end(), mWindow.begin());
         std::copy(tempBuf, tempBuf + readCount, mWindow.begin() + keep);
      }
   }

   const float sWeight = std::clamp(smoothing, 0.0f, 0.99f);

   if (mode == kModeWaveform)
   {
      const int W = winSize;
      const int H = winSize;
      EnsureTexture(W, H);

      if ((int)mSmoothedData.size() != W)
         mSmoothedData.assign(W, 0.0f);

      std::vector<uint8_t> row((size_t)W * 4);
      for (int i = 0; i < W; i++)
      {
         const float raw = mWindow[i] * gain;
         mSmoothedData[i] = mSmoothedData[i] * sWeight + raw * (1.0f - sWeight);
         const float norm = std::clamp((mSmoothedData[i] + 1.0f) * 0.5f, 0.0f, 1.0f);
         const uint8_t byte = (uint8_t)(norm * 255.0f);

         row[i * 4 + 0] = byte;
         row[i * 4 + 1] = byte;
         row[i * 4 + 2] = byte;
         row[i * 4 + 3] = 255;
      }

      std::vector<uint8_t> pixels((size_t)W * H * 4);
      for (int y = 0; y < H; y++)
      {
         std::copy(row.begin(), row.end(), pixels.begin() + (size_t)y * W * 4);
      }

      glBindTexture(GL_TEXTURE_2D, mTexture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindTexture(GL_TEXTURE_2D, 0);

      mRevision++;
   }
   else if (mode == kModeOscilloscope)
   {
      const int W = 512, H = 256;
      EnsureTexture(W, H);
      std::vector<uint8_t> pixels((size_t)W * H * 4, 0);
      for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
      auto drawPoint = [&](int x, int y) {
         for (int dy = -1; dy <= 1; ++dy)
         {
            const int yy = std::clamp(y + dy, 0, H - 1);
            const size_t p = ((size_t)yy * W + (size_t)std::clamp(x, 0, W - 1)) * 4;
            pixels[p] = 90; pixels[p + 1] = 220; pixels[p + 2] = 170;
         }
      };
      int prevY = H / 2;
      for (int x = 0; x < W; ++x)
      {
         const int sample = std::clamp((int)((long long)x * winSize / W), 0, winSize - 1);
         const float raw = std::clamp(mWindow[sample] * gain, -1.0f, 1.0f);
         const int y = (int)((1.0f - (raw + 1.0f) * 0.5f) * (H - 1));
         const int lo = std::min(prevY, y), hi = std::max(prevY, y);
         for (int yy = lo; yy <= hi; ++yy) drawPoint(x, yy);
         prevY = y;
      }
      glBindTexture(GL_TEXTURE_2D, mTexture);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      mRevision++;
   }
   else if (mode == kModeSpectrum || mode == kModeSpectrogram)
   {
      const int W = 512;
      const int H = 512;
      EnsureTexture(W, H);

      const int specSize = 512; // FFT bins 0..511 (lower half of the 1024-point transform)
      if ((int)mSmoothedData.size() != specSize)
         mSmoothedData.assign(specSize, 0.0f);

      float re[1024];
      float im[1024];
      for (int i = 0; i < 1024; i++)
      {
         // Hann window
         const float w = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * (float)i / 1023.0f));
         re[i] = mWindow[i] * w;
         im[i] = 0.0f;
      }

      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

      // Log-compress each bin's magnitude before smoothing. A harmonically
      // sparse source (e.g. a wavetable) is near-zero in linear magnitude
      // between its harmonics, so almost the whole spectrum would read as
      // black without dB compression.
      for (int i = 0; i < specSize; i++)
      {
         const float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) * (2.0f / 1024.0f) * gain;
         const float dB = 20.0f * log10f(mag + 1e-6f);
         const float specVal = std::clamp((dB + 60.0f) / 60.0f, 0.0f, 1.0f); // -60dB..0dB -> 0..1
         mSmoothedData[i] = mSmoothedData[i] * sWeight + specVal * (1.0f - sWeight);
      }

      const float sampleRate = 44100.0f;
      const float binWidth = sampleRate / 1024.0f;

      std::vector<uint8_t> row((size_t)W * 4);
      for (int x = 0; x < W; x++)
      {
         // Sample by log frequency (20Hz-20kHz) rather than linear bin index,
         // so harmonic content spreads across the visible width instead of
         // crowding into the first few columns.
         const float pos = (float)x / (float)(W - 1);
         const float freq = 20.0f * std::pow(1000.0f, pos);
         const float binF = std::clamp(freq / binWidth, 0.0f, (float)(specSize - 1));
         const int bin0 = std::clamp((int)binF, 0, specSize - 1);
         const int bin1 = std::clamp(bin0 + 1, 0, specSize - 1);
         const float frac = binF - (float)bin0;
         const float norm = mSmoothedData[bin0] * (1.0f - frac) + mSmoothedData[bin1] * frac;
         const uint8_t byte = (uint8_t)(std::clamp(norm, 0.0f, 1.0f) * 255.0f);

         row[x * 4 + 0] = byte;
         row[x * 4 + 1] = byte;
         row[x * 4 + 2] = byte;
         row[x * 4 + 3] = 255;
      }

      std::vector<uint8_t> pixels;
      if (mode == kModeSpectrogram)
      {
         if (mHistoryPixels.size() != (size_t)W * H * 4)
            mHistoryPixels.assign((size_t)W * H * 4, 0);
         std::move_backward(mHistoryPixels.begin(), mHistoryPixels.end() - (size_t)W * 4, mHistoryPixels.end());
         for (int x = 0; x < W; ++x)
         {
            const uint8_t v = row[(size_t)x * 4];
            mHistoryPixels[(size_t)x * 4 + 0] = (uint8_t)(v * 0.25f);
            mHistoryPixels[(size_t)x * 4 + 1] = (uint8_t)(v * 0.75f);
            mHistoryPixels[(size_t)x * 4 + 2] = v;
            mHistoryPixels[(size_t)x * 4 + 3] = 255;
         }
         pixels = mHistoryPixels;
      }
      else
      {
         pixels.resize((size_t)W * H * 4);
         for (int y = 0; y < H; y++)
            std::copy(row.begin(), row.end(), pixels.begin() + (size_t)y * W * 4);
      }

      glBindTexture(GL_TEXTURE_2D, mTexture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindTexture(GL_TEXTURE_2D, 0);

      mRevision++;
   }
}

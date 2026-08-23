#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "platform/AppPaths.h"

namespace
{
   void WriteU32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
   void WriteU16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

   int16_t FloatToPcm16(float v)
   {
      const float clamped = std::clamp(v, -1.0f, 1.0f);
      return (int16_t)std::lround(clamped * 32767.0f);
   }
}

bool WavWriter::Open(const std::string& path, double sampleRate, int numChannels)
{
   Close();

   mFile = fopen(path.c_str(), "wb");
   if (mFile == nullptr)
      return false;

   mPath = path;
   mNumChannels = std::max(1, numChannels);
   mFramesWritten = 0;

   const uint32_t sr = (uint32_t)std::lround(sampleRate);
   const uint16_t bitsPerSample = 16;
   const uint16_t blockAlign = (uint16_t)(mNumChannels * (bitsPerSample / 8));
   const uint32_t byteRate = sr * blockAlign;

   // Placeholder RIFF/data sizes - patched in Close() once the real frame
   // count is known, since Append() writes incrementally.
   fwrite("RIFF", 1, 4, mFile);
   WriteU32(mFile, 0);
   fwrite("WAVE", 1, 4, mFile);

   fwrite("fmt ", 1, 4, mFile);
   WriteU32(mFile, 16); // PCM fmt chunk size
   WriteU16(mFile, 1);  // PCM
   WriteU16(mFile, (uint16_t)mNumChannels);
   WriteU32(mFile, sr);
   WriteU32(mFile, byteRate);
   WriteU16(mFile, blockAlign);
   WriteU16(mFile, bitsPerSample);

   fwrite("data", 1, 4, mFile);
   WriteU32(mFile, 0);

   return true;
}

void WavWriter::Append(const float* interleaved, int frames)
{
   if (mFile == nullptr || frames <= 0)
      return;

   const int numSamples = frames * mNumChannels;
   static thread_local std::vector<int16_t> scratch;
   scratch.resize((size_t)numSamples);
   for (int i = 0; i < numSamples; i++)
      scratch[i] = FloatToPcm16(interleaved[i]);

   fwrite(scratch.data(), sizeof(int16_t), (size_t)numSamples, mFile);
   mFramesWritten += frames;
}

void WavWriter::Close()
{
   if (mFile == nullptr)
      return;

   const uint32_t dataBytes = (uint32_t)(mFramesWritten * mNumChannels * 2);
   const uint32_t riffSize = 36 + dataBytes;

   fseek(mFile, 4, SEEK_SET);
   WriteU32(mFile, riffSize);
   fseek(mFile, 40, SEEK_SET);
   WriteU32(mFile, dataBytes);

   fclose(mFile);
   mFile = nullptr;
}

#include <chrono>
#include <ctime>
#include <atomic>
#include <sys/stat.h>

namespace AudioRecordings
{
   std::string GetRecordingsDirectory()
   {
      std::string base = AppPaths::AppSupportDir();
      if (base.empty())
         base = AppPaths::TempDir() + "/Infinite";

      std::string dir = base + "/Recordings";
      AppPaths::EnsureDir(dir);
      return dir;
   }

   std::string GenerateFilePath(const std::string& prefix)
   {
      const std::string dir = GetRecordingsDirectory();
      auto now = std::chrono::system_clock::now();
      auto timeT = std::chrono::system_clock::to_time_t(now);
      std::tm tmVal {};
      localtime_r(&timeT, &tmVal);

      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
      static std::atomic<uint32_t> sCounter{ 0 };
      uint32_t count = ++sCounter;

      char filename[256];
      snprintf(filename, sizeof(filename), "recording_%s_%04d%02d%02d_%02d%02d%02d_%03d_%u.wav",
               prefix.c_str(),
               tmVal.tm_year + 1900, tmVal.tm_mon + 1, tmVal.tm_mday,
               tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec,
               (int)ms.count(), count);

      return dir + "/" + filename;
   }

   bool WriteWav(const std::string& path, const float* data, int frames, double sampleRate, int numChannels)
   {
      if (data == nullptr || frames <= 0 || sampleRate <= 0.0)
         return false;

      WavWriter writer;
      if (!writer.Open(path, sampleRate, numChannels))
         return false;

      writer.Append(data, frames);
      writer.Close();
      return true;
   }
}


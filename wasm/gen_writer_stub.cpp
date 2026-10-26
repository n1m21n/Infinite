// AudioRecordings without the mp3 encoder: the nodes link against these two; the page writes its own wav.
#include "audio/AudioFileWriter.h"

#include <cstdint>
#include <cstdio>

namespace AudioRecordings
{
   std::string GenerateFilePath(const std::string& prefix, const std::string& extension)
   {
      return "/tmp/" + prefix + "." + extension;
   }

   bool WriteWav(const std::string& path, const float* data, int frames, double sampleRate, int numChannels)
   {
      FILE* f = fopen(path.c_str(), "wb");
      if (!f)
         return false;
      const uint32_t bytes = (uint32_t)frames * numChannels * 4, rate = (uint32_t)sampleRate;
      const uint16_t fmt = 3, ch = (uint16_t)numChannels, align = (uint16_t)(numChannels * 4), bits = 32;
      const uint32_t riff = 36 + bytes, sixteen = 16, byteRate = rate * align;
      fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
      fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
      fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
      fwrite(data, 4, (size_t)frames * numChannels, f);
      fclose(f);
      return true;
   }
}

#include "core/BenchReport.h"

namespace Bench::GpuMem
{
   void RecordBuffer(unsigned int, GpuMemCategory, size_t, const char*) {}
}

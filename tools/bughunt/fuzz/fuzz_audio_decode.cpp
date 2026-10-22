// Audio files through the portable decoder (Windows and Linux; macOS uses
// AVFoundation for the app, but this file builds and runs anywhere):
// Platform::DecodeAudioFileToBuffer. The first byte picks the extension the
// decoder sees, the rest is the file. B-K-007 (AIFF rate/sign, header-sized
// allocations, FLAC).
#include "Platform.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

static std::string gBase;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
   char tmpl[] = "/tmp/fuzz_audio_XXXXXX";
   const int fd = mkstemp(tmpl);
   if (fd >= 0)
      close(fd);
   gBase = tmpl;
   return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size < 1 || size > (1u << 20))
      return 0;
   static const char* kExt[] = { ".wav", ".aif", ".aiff", ".flac", ".mp3" };
   const std::string path = gBase + kExt[data[0] % 5];
   if (FILE* f = fopen(path.c_str(), "wb"))
   {
      fwrite(data + 1, 1, size - 1, f);
      fclose(f);
   }
   Platform::SampleBuffer b;
   std::string err;
   Platform::DecodeAudioFileToBuffer(path, b, err);
   remove(path.c_str());
   return 0;
}

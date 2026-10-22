// HAP frames and movie containers: Hap::DecodeFrame on the bytes, and the
// movie-atom scan (Hap::Reader::Probe) on the same bytes as a file.
// B-K-003 (chunk table past the frame) and B-K-004 (64-bit atom size wrap).
#include "HapReader.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

static std::string gPath;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
   char tmpl[] = "/tmp/fuzz_hap_XXXXXX";
   const int fd = mkstemp(tmpl);
   if (fd >= 0)
      close(fd);
   gPath = tmpl;
   return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size > (1u << 20))
      return 0;
   // An exact-size heap copy, so ASan sees any read past the end.
   std::vector<uint8_t> buf(data, data + size);
   Hap::Format fmt;
   std::vector<uint8_t> out(Hap::TextureBytes(Hap::Format::Dxt1, 64, 64));
   std::string err;
   Hap::DecodeFrame(buf.data(), buf.size(), fmt, out, err);

   if (FILE* f = fopen(gPath.c_str(), "wb"))
   {
      fwrite(data, 1, size, f);
      fclose(f);
      Hap::Reader::Probe(gPath);
   }
   return 0;
}

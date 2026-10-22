// glTF/GLB import: GltfImport::DecodeCached on the bytes as a file. Each input
// gets a fresh path, so the asset cache never answers from an earlier one.
// B-K-005 (accessors read before cgltf_validate).
#include "GltfImport.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

// The importer decodes embedded textures through Platform; image decoding has
// its own fuzz surface, so here it always fails.
namespace Platform
{
   bool LoadImageRGBA(const std::string&, std::vector<unsigned char>&, int&, int&, std::string& e)
   {
      e = "fuzz stub";
      return false;
   }
   bool LoadImageRGBAFromMemory(const std::vector<unsigned char>&, std::vector<unsigned char>&, int&, int&,
                                std::string& e)
   {
      e = "fuzz stub";
      return false;
   }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size < 1 || size > (1u << 20))
      return 0;
   static unsigned long long n = 0;
   char path[96];
   // first byte: text glTF or binary GLB
   snprintf(path, sizeof(path), "/tmp/fuzz_gltf_%d_%llu.%s", (int)getpid(), n++, (data[0] & 1) ? "glb" : "gltf");
   if (FILE* f = fopen(path, "wb"))
   {
      fwrite(data + 1, 1, size - 1, f);
      fclose(f);
   }
   std::string err;
   GltfImport::DecodeCached(path, err);
   remove(path);
   return 0;
}

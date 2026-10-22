// Patch text: Patch::ReadText on arbitrary bytes. The reader is what every
// .inf, preset, template and pasted patch goes through. Loading into nodes is
// covered by param_fuzz.py; this covers the parser itself.
#include "Patch.h"

#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size > (1u << 18))
      return 0;
   const std::string text((const char*)data, size);
   Patch::Data d;
   std::string err;
   Patch::ReadText(text, d, err);
   return 0;
}

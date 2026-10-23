// Turbo: in-memory image decode for glTF/GLB textures (see Platform.h).
// Uses stb_image, whose implementation lives in EnvironmentNode.cpp.
#include <string>
#include <vector>

#include "Platform.h"

#define STBI_NO_STDIO
#include "stb_image.h"

namespace Platform
{
   bool LoadImageRGBAFromMemory(const std::vector<unsigned char>& bytes, std::vector<unsigned char>& outPixels,
                                int& outWidth, int& outHeight, std::string& outError)
   {
      outError.clear();
      if (bytes.empty())
      {
         outError = "empty image data";
         return false;
      }
      // Turbo 0.70: this thread's flag only. The global one raced with an HDRI
      // loading on the main thread, which then came out upside down.
      stbi_set_flip_vertically_on_load_thread(1);
      int w = 0, h = 0, comp = 0;
      stbi_uc* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
      stbi_set_flip_vertically_on_load_thread(0);
      if (data == nullptr)
      {
         const char* why = stbi_failure_reason();
         outError = why ? why : "image decode failed";
         return false;
      }
      outWidth = w;
      outHeight = h;
      outPixels.assign(data, data + (size_t)w * h * 4);
      stbi_image_free(data);
      return true;
   }
}

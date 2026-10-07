// PNG / image export (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // `keepPixels`, when given, receives the RGBA8 rows exactly as read back
   // (bottom row first), so a caller can measure the frame it just wrote.
   void ExportImage(INode* out, const std::string& path, int jpgQuality,
                    std::vector<unsigned char>* keepPixels, int outputIndex)
   {
      int w = out->GetOutputWidth();
      int h = out->GetOutputHeight();
      if (w <= 0 || h <= 0)
         return;

      std::vector<unsigned char> pixels(w * h * 4);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out->GetOutputTexture(outputIndex), 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glDeleteFramebuffers(1, &fbo);

      stbi_flip_vertically_on_write(1);

      std::string lowerPath = path;
      for (char& c : lowerPath) c = (char)tolower((unsigned char)c);

      if (lowerPath.length() >= 4 && (lowerPath.rfind(".jpg") == lowerPath.length() - 4 || lowerPath.rfind(".jpeg") == lowerPath.length() - 5))
      {
         // Convert RGBA to RGB for JPEG compatibility
         std::vector<unsigned char> rgb(w * h * 3);
         for (int i = 0; i < w * h; i++)
         {
            rgb[i * 3 + 0] = pixels[i * 4 + 0];
            rgb[i * 3 + 1] = pixels[i * 4 + 1];
            rgb[i * 3 + 2] = pixels[i * 4 + 2];
         }
         stbi_write_jpg(path.c_str(), w, h, 3, rgb.data(), jpgQuality);
      }
      else
      {
         stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4);
      }
      if (keepPixels != nullptr)
         keepPixels->swap(pixels);
   }


   // A node's image output as top-row-first RGBA8, straight alpha. Returns false when the node has
   // no image there. `opaque` forces alpha to 255 (the picture without its transparency).
   bool ReadNodeImageRgba8(INode* node, int outputIndex, bool opaque, int& w, int& h, std::vector<unsigned char>& rgba)
   {
      w = node->GetOutputWidth();
      h = node->GetOutputHeight();
      const unsigned int tex = node->GetOutputTexture(outputIndex);
      if (w <= 0 || h <= 0 || tex == 0)
         return false;
      std::vector<unsigned char> bottomFirst((size_t)w * h * 4);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, bottomFirst.data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glDeleteFramebuffers(1, &fbo);
      rgba.resize(bottomFirst.size());
      for (int y = 0; y < h; y++)
         std::memcpy(&rgba[(size_t)y * w * 4], &bottomFirst[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
      if (opaque)
         for (size_t i = 3; i < rgba.size(); i += 4)
            rgba[i] = 255;
      return true;
   }


   // Top-row-first RGBA8 to a PNG that says it is sRGB (stb writes no colour
   // chunk of its own).
   bool WriteSrgbPng(const std::string& path, int w, int h, const unsigned char* rgba)
   {
      std::vector<uint8_t> png;
      stbi_flip_vertically_on_write(0);
      const int ok = stbi_write_png_to_func(
         [](void* ctx, void* data, int size)
         {
            auto* bytes = static_cast<std::vector<uint8_t>*>(ctx);
            bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
         },
         &png, w, h, 4, rgba, w * 4);
      if (!ok || png.empty())
         return false;
      png = ContactSheet::TagSrgb(png);
      FILE* f = std::fopen(path.c_str(), "wb");
      if (f == nullptr)
         return false;
      const bool wrote = std::fwrite(png.data(), 1, png.size(), f) == png.size();
      std::fclose(f);
      return wrote;
   }


   // True when the node's output texture holds more than 8 bits per channel (RGBA16F / RGBA32F),
   // i.e. when a 16-bit PNG carries real precision instead of padded bytes.
   bool NodeImageIsFloat(INode* node, int outputIndex)
   {
      const unsigned int tex = node->GetOutputTexture(outputIndex);
      if (tex == 0)
         return false;
      GLint prev = 0, fmt = 0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
      glBindTexture(GL_TEXTURE_2D, tex);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
      glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
      return fmt == GL_RGBA16F || fmt == GL_RGBA32F;
   }


   // Same readback as ReadNodeImageRgba8 at 16 bits per channel, top row first, native endian.
   bool ReadNodeImageRgba16(INode* node, int outputIndex, bool opaque, int& w, int& h, std::vector<uint16_t>& rgba)
   {
      w = node->GetOutputWidth();
      h = node->GetOutputHeight();
      const unsigned int tex = node->GetOutputTexture(outputIndex);
      if (w <= 0 || h <= 0 || tex == 0)
         return false;
      std::vector<uint16_t> bottomFirst((size_t)w * h * 4);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT, bottomFirst.data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glDeleteFramebuffers(1, &fbo);
      rgba.resize(bottomFirst.size());
      for (int y = 0; y < h; y++)
         std::memcpy(&rgba[(size_t)y * w * 4], &bottomFirst[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4 * sizeof(uint16_t));
      if (opaque)
         for (size_t i = 3; i < rgba.size(); i += 4)
            rgba[i] = 65535;
      return true;
   }


   // Bilinear rescale of straight-alpha RGBA8, weighting colour by alpha so a transparent
   // texel's hidden colour never bleeds into an edge (that is the dark-fringe bug in miniature).
   std::vector<unsigned char> ResizeRgba8(const unsigned char* src, int w, int h, int nw, int nh)
   {
      std::vector<unsigned char> out((size_t)nw * nh * 4);
      for (int y = 0; y < nh; y++)
      {
         const float fy = std::max(0.0f, ((float)y + 0.5f) * (float)h / (float)nh - 0.5f);
         const int y0 = std::min(h - 1, (int)fy), y1 = std::min(h - 1, y0 + 1);
         const float ty = fy - (float)y0;
         for (int x = 0; x < nw; x++)
         {
            const float fx = std::max(0.0f, ((float)x + 0.5f) * (float)w / (float)nw - 0.5f);
            const int x0 = std::min(w - 1, (int)fx), x1 = std::min(w - 1, x0 + 1);
            const float tx = fx - (float)x0;
            const unsigned char* px[4] = { src + ((size_t)y0 * w + x0) * 4, src + ((size_t)y0 * w + x1) * 4,
                                           src + ((size_t)y1 * w + x0) * 4, src + ((size_t)y1 * w + x1) * 4 };
            const float wt[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
            float a = 0, r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++)
            {
               const float ak = (float)px[k][3] * wt[k];
               a += ak;
               r += (float)px[k][0] * ak;
               g += (float)px[k][1] * ak;
               b += (float)px[k][2] * ak;
            }
            unsigned char* d = &out[((size_t)y * nw + x) * 4];
            if (a > 0.0f)
            {
               d[0] = (unsigned char)std::lround(std::min(255.0f, r / a));
               d[1] = (unsigned char)std::lround(std::min(255.0f, g / a));
               d[2] = (unsigned char)std::lround(std::min(255.0f, b / a));
            }
            else
               d[0] = d[1] = d[2] = 0;
            d[3] = (unsigned char)std::lround(std::min(255.0f, a));
         }
      }
      return out;
   }


   // 16-bit RGBA PNG tagged sRGB. stb has no 16-bit writer, but its zlib encoder is reusable.
   extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int dataLen, int* outLen, int quality);

   std::vector<uint8_t> EncodePng16(int w, int h, const uint16_t* rgba, int level)
   {
      static uint32_t table[256];
      static bool tableReady = false;
      if (!tableReady)
      {
         for (uint32_t n = 0; n < 256; n++)
         {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
               c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
         }
         tableReady = true;
      }
      std::vector<uint8_t> raw((size_t)h * (1 + (size_t)w * 8));
      for (int y = 0; y < h; y++)
      {
         uint8_t* row = &raw[(size_t)y * (1 + (size_t)w * 8)];
         *row++ = 0; // filter: none
         const uint16_t* in = rgba + (size_t)y * w * 4;
         for (int i = 0; i < w * 4; i++)
         {
            *row++ = (uint8_t)(in[i] >> 8);
            *row++ = (uint8_t)(in[i] & 0xFF);
         }
      }
      int zlen = 0;
      unsigned char* z = stbi_zlib_compress(raw.data(), (int)raw.size(), &zlen, std::max(1, level));
      if (z == nullptr)
         return {};
      std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
      auto chunk = [&](const char* type, const uint8_t* data, size_t n)
      {
         const uint8_t len[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
         png.insert(png.end(), len, len + 4);
         const size_t at = png.size();
         png.insert(png.end(), type, type + 4);
         png.insert(png.end(), data, data + n);
         uint32_t c = 0xFFFFFFFFu;
         for (size_t i = at; i < png.size(); i++)
            c = table[(c ^ png[i]) & 0xFF] ^ (c >> 8);
         c ^= 0xFFFFFFFFu;
         const uint8_t crc[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
         png.insert(png.end(), crc, crc + 4);
      };
      const uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                                 (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 16, 6, 0, 0, 0 };
      chunk("IHDR", ihdr, 13);
      const uint8_t srgb = 0;
      chunk("sRGB", &srgb, 1);
      chunk("IDAT", z, (size_t)zlen);
      chunk("IEND", nullptr, 0);
      std::free(z);
      return png;
   }


   void ExportPng(OutputNode* out, const std::string& path)
   {
      ExportImage(out, path);
   }
}

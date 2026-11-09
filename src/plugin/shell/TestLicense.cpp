// Writes a test licence into a folder (docs/plans/vst-au/README.md 3.4/3.5): license.inflic,
// license-test-key.hex and a fresh empty refund list. Same fixed-seed key pair and payload as
// ShellCheck.cpp's LicenseFixture, so CI on Windows and Linux can run pluginval through the
// licensed path. Only an INFINITE_LICENSE_TEST_KEY build of the plugins accepts these keys.
//
//   plugin_test_license DIR
#include "monocypher/monocypher-ed25519.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
   std::string B64Url(const std::string& in)
   {
      static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
      std::string out;
      uint32_t acc = 0;
      int bits = 0;
      for (unsigned char c : in)
      {
         acc = (acc << 8) | c;
         bits += 8;
         while (bits >= 6)
         {
            bits -= 6;
            out += a[(acc >> bits) & 63];
         }
      }
      if (bits > 0)
         out += a[(acc << (6 - bits)) & 63];
      return out;
   }

   bool Write(const std::filesystem::path& path, const std::string& text)
   {
      std::ofstream f(path, std::ios::binary | std::ios::trunc);
      f << text;
      return bool(f);
   }
}

int main(int argc, char** argv)
{
   if (argc != 2)
   {
      std::fprintf(stderr, "usage: plugin_test_license DIR\n");
      return 2;
   }
   const std::filesystem::path dir = std::filesystem::u8path(argv[1]);
   std::error_code ec;
   std::filesystem::create_directories(dir, ec);
   uint8_t seed[32], secret[64], pub[32];
   for (int i = 0; i < 32; i++)
      seed[i] = uint8_t(0xA5 ^ (i * 7));
   crypto_ed25519_key_pair(secret, pub, seed);
   const std::string payload = "{\"v\":1,\"id\":\"INF-CHEC-K234\",\"email\":\"check@example.com\",\"txn\":\"txn_check\","
                               "\"seats\":1,\"major\":1,\"iat\":1762560000}";
   const std::string signedPart = "INF1." + B64Url(payload);
   uint8_t sig[64];
   crypto_ed25519_sign(sig, secret, reinterpret_cast<const uint8_t*>(signedPart.data()), signedPart.size());
   const std::string key = signedPart + "." + B64Url(std::string(reinterpret_cast<const char*>(sig), 64));
   char hex[65];
   for (int i = 0; i < 32; i++)
      std::snprintf(hex + 2 * i, 3, "%02x", pub[i]);
   const bool ok = Write(dir / "license.inflic", key + "\n") && Write(dir / "license-test-key.hex", std::string(hex) + "\n") &&
                   Write(dir / "license-revoked.json",
                         "{\"revoked\":[],\"fetched\":" + std::to_string((long long)std::time(nullptr)) + "}");
   if (!ok)
   {
      std::fprintf(stderr, "could not write %s\n", dir.u8string().c_str());
      return 1;
   }
   std::printf("%s\n", dir.u8string().c_str());
   return 0;
}

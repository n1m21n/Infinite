// Plugin licence check (License.h, docs/plans/vst-au/README.md 8.5).
#include "platform/AppPaths.h"
#include "core/License.h"

#include "monocypher/monocypher-ed25519.h"
#include "json/json.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#ifndef INFINITE_LICENSE_PUBKEY_HEX
#error "INFINITE_LICENSE_PUBKEY_HEX must come from worker/license/PUBLIC_KEY.txt (CMake)"
#endif

namespace License
{
   namespace
   {
      int HexNibble(char c)
      {
         if (c >= '0' && c <= '9') return c - '0';
         if (c >= 'a' && c <= 'f') return c - 'a' + 10;
         if (c >= 'A' && c <= 'F') return c - 'A' + 10;
         return -1;
      }

      bool Hex32(const std::string& hex, uint8_t out[32])
      {
         std::string h;
         for (char c : hex)
            if (HexNibble(c) >= 0)
               h += c;
         if (h.size() != 64)
            return false;
         for (int i = 0; i < 32; i++)
            out[i] = uint8_t(HexNibble(h[2 * i]) << 4 | HexNibble(h[2 * i + 1]));
         return true;
      }

      bool B64UrlDecode(const std::string& in, std::string& out)
      {
         out.clear();
         uint32_t acc = 0;
         int bits = 0;
         for (char c : in)
         {
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '-' || c == '+') v = 62;
            else if (c == '_' || c == '/') v = 63;
            else if (c == '=') break;
            else return false;
            acc = (acc << 6) | uint32_t(v);
            bits += 6;
            if (bits >= 8)
            {
               bits -= 8;
               out += char((acc >> bits) & 0xFF);
            }
         }
         return true;
      }

      // The public key: compiled in. A build configured with INFINITE_LICENSE_TEST_KEY (development
      // and the check tools only, never a shipped build) also takes $INFINITE_LICENSE_PUBKEY or
      // <Dir>/license-test-key.hex, so checks can sign their own keys.
      bool PublicKey(uint8_t out[32])
      {
#if defined(INFINITE_LICENSE_TEST_KEY) && INFINITE_LICENSE_TEST_KEY
         if (const char* env = getenv("INFINITE_LICENSE_PUBKEY"); env && Hex32(env, out))
            return true;
         std::ifstream f(AppPaths::FsPath(Dir()) / "license-test-key.hex");
         std::string line;
         if (f && std::getline(f, line) && Hex32(line, out))
            return true;
#endif
         return Hex32(INFINITE_LICENSE_PUBKEY_HEX, out);
      }

      bool ReadFile(const std::filesystem::path& p, std::string& out)
      {
         std::ifstream f(p, std::ios::binary);
         if (!f)
            return false;
         std::ostringstream ss;
         ss << f.rdbuf();
         out = ss.str();
         return true;
      }

      // Write next to it, then rename: another instance never reads half a file.
      bool WriteFile(const std::filesystem::path& p, const std::string& text)
      {
         std::error_code ec;
         std::filesystem::create_directories(p.parent_path(), ec);
         std::filesystem::path tmp = p; // p.string() would be ANSI on Windows
         tmp += ".tmp";
         {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f)
               return false;
            f << text;
            if (!f)
               return false;
         }
         std::filesystem::rename(tmp, p, ec);
         if (ec)
         {
            std::filesystem::remove(tmp, ec);
            return false;
         }
         return true;
      }

      bool KeyChar(char c)
      {
         return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
      }
   }

   std::string Extract(const std::string& text)
   {
      const size_t at = text.find("INF1.");
      if (at == std::string::npos)
         return std::string();
      // Mail clients wrap long lines: skip whitespace inside the key, stop at anything else.
      std::string key;
      for (size_t i = at; i < text.size() && key.size() < 4096; i++)
      {
         const char c = text[i];
         if (KeyChar(c))
            key += c;
         else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>')
            continue;
         else
            break;
      }
      // INF1.payload.sig: exactly two dots.
      size_t dots = 0;
      for (char c : key)
         dots += c == '.';
      while (dots > 2 && !key.empty())
      {
         dots -= key.back() == '.';
         key.pop_back();
      }
      return key;
   }

   std::string IdOf(const std::string& key)
   {
      const size_t a = key.find('.'), b = key.rfind('.');
      if (a == std::string::npos || b == a)
         return std::string();
      std::string payload;
      if (!B64UrlDecode(key.substr(a + 1, b - a - 1), payload))
         return std::string();
      const nlohmann::json j = nlohmann::json::parse(payload, nullptr, false);
      return j.is_object() && j.contains("id") && j["id"].is_string() ? j["id"].get<std::string>() : std::string();
   }

   Info Check(const std::string& keyIn, const std::vector<std::string>& revoked, int appMajor)
   {
      (void)appMajor;
      Info info;
      const std::string key = Extract(keyIn);
      if (key.empty())
      {
         info.status = keyIn.empty() ? Status::None : Status::Malformed;
         return info;
      }
      info.status = Status::Malformed;
      const size_t a = key.find('.'), b = key.rfind('.');
      if (a != 4 || b == a)
         return info;
      const std::string signedPart = key.substr(0, b); // "INF1.<payload>"
      std::string payload, sig;
      if (!B64UrlDecode(key.substr(a + 1, b - a - 1), payload) || !B64UrlDecode(key.substr(b + 1), sig) || sig.size() != 64)
         return info;
      const nlohmann::json j = nlohmann::json::parse(payload, nullptr, false);
      if (!j.is_object())
         return info;
      uint8_t pub[32];
      if (!PublicKey(pub) ||
          crypto_ed25519_check(reinterpret_cast<const uint8_t*>(sig.data()), pub, reinterpret_cast<const uint8_t*>(signedPart.data()),
                               signedPart.size()) != 0)
      {
         info.status = Status::BadSignature;
         return info;
      }
      info.id = j.value("id", std::string());
      info.email = j.value("email", std::string());
      info.major = j.contains("major") && j["major"].is_number() ? j["major"].get<int>() : 0;
      if (!j.contains("v") || !j["v"].is_number() || j["v"].get<int>() != 1)
         info.status = Status::WrongVersion;
      // Lifetime updates: a key from any major works in every later build. "major" stays in the
      // payload as a record of what was bought, and WrongMajor is never set.
      else if (!info.id.empty() && std::find(revoked.begin(), revoked.end(), info.id) != revoked.end())
         info.status = Status::Revoked;
      else
         info.status = Status::Valid;
      return info;
   }

   std::string Dir()
   {
      // UTF-8 on every OS: Windows' getenv and path::string() are ANSI, which mangles a user called "José".
      if (std::string env; AppPaths::GetEnvUtf8("INFINITE_LICENSE_DIR", env) && !env.empty())
         return env;
#if defined(_WIN32)
      if (std::string appdata; AppPaths::GetEnvUtf8("APPDATA", appdata) && !appdata.empty())
         return (AppPaths::FsPath(appdata) / "Infinite").u8string();
      return std::string();
#else
      // The account's real home, not $HOME: a sandboxed host points $HOME into its container.
      std::string home;
      if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir)
         home = pw->pw_dir;
      else if (const char* h = getenv("HOME"))
         home = h;
      if (home.empty())
         return std::string();
#if defined(__APPLE__)
      return home + "/Library/Application Support/Infinite";
#else
      if (const char* xdg = getenv("XDG_CONFIG_HOME"); xdg && *xdg)
         return std::string(xdg) + "/infinite";
      return home + "/.config/infinite";
#endif
#endif
   }

   bool ReadKey(std::string& key)
   {
      const std::string dir = Dir();
      return !dir.empty() && ReadFile(AppPaths::FsPath(dir) / "license.inflic", key);
   }

   bool WriteKey(const std::string& key)
   {
      const std::string dir = Dir();
      return !dir.empty() && WriteFile(AppPaths::FsPath(dir) / "license.inflic", key + "\n");
   }

   std::vector<std::string> ReadRevoked(int64_t* fetchedUnix)
   {
      std::vector<std::string> ids;
      if (fetchedUnix)
         *fetchedUnix = 0;
      std::string text;
      const std::string dir = Dir();
      if (dir.empty() || !ReadFile(AppPaths::FsPath(dir) / "license-revoked.json", text))
         return ids;
      const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
      if (!j.is_object())
         return ids;
      if (j.contains("revoked") && j["revoked"].is_array())
         for (const auto& v : j["revoked"])
            if (v.is_string())
               ids.push_back(v.get<std::string>());
      if (fetchedUnix && j.contains("fetched") && j["fetched"].is_number())
         *fetchedUnix = j["fetched"].get<int64_t>();
      return ids;
   }

   bool WriteRevoked(const std::string& json, int64_t fetchedUnix)
   {
      const nlohmann::json in = nlohmann::json::parse(json, nullptr, false);
      if (!in.is_object() || !in.contains("revoked") || !in["revoked"].is_array())
         return false; // fail open: the last list stays
      nlohmann::json out;
      out["revoked"] = nlohmann::json::array();
      for (const auto& v : in["revoked"])
         if (v.is_string())
            out["revoked"].push_back(v.get<std::string>());
      out["fetched"] = fetchedUnix;
      const std::string dir = Dir();
      return !dir.empty() && WriteFile(AppPaths::FsPath(dir) / "license-revoked.json", out.dump());
   }

   bool RefundCheckOn()
   {
      std::string text;
      const std::string dir = Dir();
      if (dir.empty() || !ReadFile(AppPaths::FsPath(dir) / "license-settings.json", text))
         return true;
      const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
      return !(j.is_object() && j.contains("refundCheck") && j["refundCheck"].is_boolean()) || j["refundCheck"].get<bool>();
   }

   void SetRefundCheck(bool on)
   {
      const std::string dir = Dir();
      if (!dir.empty())
         WriteFile(AppPaths::FsPath(dir) / "license-settings.json", nlohmann::json { { "refundCheck", on } }.dump());
   }

   std::string RevokedUrl()
   {
      if (const char* env = getenv("INFINITE_LICENSE_REVOKED_URL"); env && *env)
         return env;
      return kRevokedUrl;
   }

   Info Current(int appMajor)
   {
      std::string key;
      if (!ReadKey(key))
         return Info {};
      return Check(key, ReadRevoked(), appMajor);
   }

   std::string Message(const Info& info)
   {
      switch (info.status)
      {
      case Status::Valid: return "Licensed to " + info.email + " \xC2\xB7 " + info.id;
      case Status::None: return "Enter your key";
      case Status::Revoked: return "This key was refunded";
      case Status::WrongMajor: return "Your key covers " + std::to_string(info.major) + ".x";
      case Status::WrongVersion: return "This key needs a newer Infinite";
      case Status::Malformed:
      case Status::BadSignature: break;
      }
      return "Not a valid key";
   }
}

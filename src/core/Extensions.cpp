#include "Extensions.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "json.hpp"
#include "miniz.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

#ifndef INFINITE_VERSION_STRING
#define INFINITE_VERSION_STRING "0.0.0"
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Extensions
{
namespace
{
   const char* const kCatalogUrl =
      "https://github.com/n1m21n/Infinite/releases/latest/download/extensions.json";
   const uint64_t kMaxPackBytes = 1ull << 31; // 2 GB hard cap on one pack's unpacked size

   // ---- SHA-256 (FIPS 180-4) ----------------------------------------------
   struct Sha256
   {
      uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
      uint8_t buf[64] = {};
      size_t bufLen = 0;
      uint64_t total = 0;

      static uint32_t Rot(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

      void Block(const uint8_t* p)
      {
         static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
         uint32_t w[64];
         for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
         for (int i = 16; i < 64; i++)
         {
            uint32_t s0 = Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
         }
         uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
         for (int i = 0; i < 64; i++)
         {
            uint32_t S1 = Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
         }
         h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
      }

      void Update(const void* data, size_t len)
      {
         const uint8_t* p = (const uint8_t*)data;
         total += len;
         while (len > 0)
         {
            size_t n = std::min(len, sizeof(buf) - bufLen);
            memcpy(buf + bufLen, p, n);
            bufLen += n; p += n; len -= n;
            if (bufLen == 64) { Block(buf); bufLen = 0; }
         }
      }

      std::string Final()
      {
         uint64_t bits = total * 8;
         uint8_t pad = 0x80;
         Update(&pad, 1);
         uint8_t zero = 0;
         while (bufLen != 56) Update(&zero, 1);
         uint8_t len[8];
         for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
         Update(len, 8);
         char out[65];
         for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
         return std::string(out, 64);
      }
   };

   std::string Lower(std::string s)
   {
      for (char& c : s) c = (char)tolower((unsigned char)c);
      return s;
   }

   // A zip entry name is safe when it stays inside the pack folder.
   bool SafeEntryName(const std::string& n)
   {
      if (n.empty() || n[0] == '/' || n[0] == '\\' || n.find('\\') != std::string::npos ||
          n.find(':') != std::string::npos || n.find('\0') != std::string::npos)
         return false;
      size_t start = 0;
      while (start <= n.size())
      {
         size_t slash = n.find('/', start);
         std::string part = n.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
         if (part == "..")
            return false;
         if (slash == std::string::npos)
            break;
         start = slash + 1;
      }
      return true;
   }

   size_t WriteToFile(void* user, mz_uint64, const void* data, size_t n)
   {
      return fwrite(data, 1, n, (FILE*)user);
   }

   bool ReadPackJson(const std::string& dir, std::string& id, std::string& version)
   {
      std::ifstream in(AppPaths::FsPath(dir + "/pack.json"), std::ios::binary);
      if (!in)
         return false;
      std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      try
      {
         json j = json::parse(body);
         id = j.value("id", std::string());
         version = j.value("version", std::string());
         return IsValidId(id);
      }
      catch (...) { return false; }
   }

   // ---- async state --------------------------------------------------------
   std::mutex gMutex;
   State gShared;           // written by the worker under gMutex
   State gView;             // main-thread copy, refreshed by Poll()
   std::thread gWorker;
   std::atomic<bool> gBusy { false };
   std::atomic<bool> gCancel { false };

   template <class Fn> bool RunWorker(Fn fn)
   {
      if (gBusy.exchange(true))
         return false;
      if (gWorker.joinable())
         gWorker.join();
      gWorker = std::thread([fn]() { fn(); gBusy.store(false); });
      return true;
   }

   std::vector<std::pair<std::string, std::string>> ScanInstalled()
   {
      std::vector<std::pair<std::string, std::string>> out;
      const std::string root = RootDir();
      if (root.empty())
         return out;
      std::error_code ec;
      for (const auto& e : fs::directory_iterator(AppPaths::FsPath(root), ec))
      {
         const std::string name = e.path().filename().u8string();
         std::string id, ver;
         if (IsValidId(name) && ReadPackJson(root + "/" + name, id, ver) && id == name)
            out.emplace_back(id, ver);
      }
      return out;
   }

   void SetBusy(const std::string& id, const char* label)
   {
      std::lock_guard<std::mutex> l(gMutex);
      gShared.busyId = id;
      gShared.busyLabel = label;
      gShared.progress = -1.0f;
   }

   void Finish(bool ok, const std::string& msg)
   {
      std::lock_guard<std::mutex> l(gMutex);
      gShared.busyId.clear();
      gShared.busyLabel.clear();
      gShared.progress = -1.0f;
      gShared.installed = ScanInstalled();
      gShared.message = msg;
      gShared.messageIsError = !ok;
   }
}

std::string PlatformKey()
{
#if defined(__APPLE__)
   return "macos";
#elif defined(_WIN32)
   #if defined(_M_ARM64) || defined(__aarch64__)
   return "windows-arm64";
   #else
   return "windows-x64";
   #endif
#else
   return "linux-x64";
#endif
}

bool ParseManifest(const std::string& text, const std::string& platformKey,
                   std::vector<Pack>& out, std::string& err)
{
   out.clear();
   err.clear();
   try
   {
      json j = json::parse(text);
      if (!j.is_object() || j.value("schema", 0) != 1)
      {
         err = "unsupported extension catalog";
         return false;
      }
      if (!j.contains("packs") || !j["packs"].is_array())
      {
         err = "extension catalog has no packs";
         return false;
      }
      for (const json& p : j["packs"])
      {
         Pack pk;
         pk.id = p.value("id", std::string());
         if (!IsValidId(pk.id))
            continue;
         pk.name = p.value("name", pk.id);
         pk.purpose = p.value("purpose", std::string());
         pk.version = p.value("version", std::string());
         pk.size = p.value("size", (uint64_t)0);
         if (!p.contains("files") || !p["files"].is_object() || !p["files"].contains(platformKey))
            continue;
         const json& f = p["files"][platformKey];
         pk.url = f.value("url", std::string());
         pk.sha256 = Lower(f.value("sha256", std::string()));
         if (pk.url.rfind("https://", 0) != 0 || pk.sha256.size() != 64 || pk.version.empty())
            continue; // a pack we could not verify is not offered
         out.push_back(std::move(pk));
      }
      return true;
   }
   catch (...)
   {
      err = "invalid extension catalog";
      return false;
   }
}

std::string Sha256Hex(const void* data, size_t len)
{
   Sha256 s;
   s.Update(data, len);
   return s.Final();
}

bool IsValidId(const std::string& id)
{
   if (id.empty() || id.size() > 40)
      return false;
   for (char c : id)
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
         return false;
   return true;
}

std::string RootDir()
{
   if (const char* o = getenv("INFINITE_EXTENSIONS_DIR"))
      if (o[0] != '\0')
         return AppPaths::EnsureDir(o) ? std::string(o) : std::string();
   const std::string base = AppPaths::AppSupportDir();
   if (base.empty())
      return {};
   const std::string dir = base + "/extensions";
   return AppPaths::EnsureDir(dir) ? dir : std::string();
}

static std::string PackDirAny(const std::string& id)
{
   if (!IsValidId(id))
      return {};
   const std::string root = RootDir();
   if (root.empty())
      return {};
   const std::string dir = root + "/" + id;
   std::string pid, ver;
   return ReadPackJson(dir, pid, ver) && pid == id ? dir : std::string();
}

std::string PackDir(const std::string& id)
{
   const std::string dir = PackDirAny(id);
   return !dir.empty() && IsEnabled(id) ? dir : std::string();
}

bool IsEnabled(const std::string& id)
{
   const std::string dir = PackDirAny(id);
   return !dir.empty() && !fs::exists(AppPaths::FsPath(dir + "/.disabled"));
}

void SetEnabled(const std::string& id, bool on)
{
   const std::string dir = PackDirAny(id);
   if (dir.empty())
      return;
   const auto f = AppPaths::FsPath(dir + "/.disabled");
   std::error_code ec;
   if (on)
      fs::remove(f, ec);
   else
      std::ofstream(f).put('x');
}

std::string InstalledVersion(const std::string& id)
{
   const std::string dir = PackDirAny(id);
   std::string pid, ver;
   return !dir.empty() && ReadPackJson(dir, pid, ver) ? ver : std::string();
}

bool InstallZip(const std::string& zipBytes, const std::string& expectedSha256,
                const std::string& expectId, std::string& outId, std::string& err)
{
   outId.clear();
   err.clear();
   if (!expectedSha256.empty() && Sha256Hex(zipBytes.data(), zipBytes.size()) != Lower(expectedSha256))
   {
      err = "download is corrupt (checksum mismatch)";
      return false;
   }
   const std::string root = RootDir();
   if (root.empty())
   {
      err = "no writable extensions folder";
      return false;
   }

   mz_zip_archive zip;
   memset(&zip, 0, sizeof(zip));
   if (!mz_zip_reader_init_mem(&zip, zipBytes.data(), zipBytes.size(), 0))
   {
      err = "not a valid pack file";
      return false;
   }
   struct Closer { mz_zip_archive* z; ~Closer() { mz_zip_reader_end(z); } } closer { &zip };

   // pack.json first: it names the folder everything lands in.
   std::string id, version;
   {
      int idx = mz_zip_reader_locate_file(&zip, "pack.json", nullptr, 0);
      if (idx < 0)
      {
         err = "pack.json is missing";
         return false;
      }
      size_t n = 0;
      void* p = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &n, 0);
      if (!p)
      {
         err = "pack.json is unreadable";
         return false;
      }
      std::string body((const char*)p, n);
      mz_free(p);
      try
      {
         json j = json::parse(body);
         id = j.value("id", std::string());
         version = j.value("version", std::string());
      }
      catch (...) {}
      if (!IsValidId(id) || version.empty())
      {
         err = "pack.json is invalid";
         return false;
      }
      if (!expectId.empty() && id != expectId)
      {
         err = "pack id does not match";
         return false;
      }
   }

   // Validate every entry before writing anything.
   uint64_t total = 0;
   const mz_uint count = mz_zip_reader_get_num_files(&zip);
   for (mz_uint i = 0; i < count; i++)
   {
      mz_zip_archive_file_stat st;
      if (!mz_zip_reader_file_stat(&zip, i, &st) || !SafeEntryName(st.m_filename))
      {
         err = "pack contains an unsafe path";
         return false;
      }
      total += st.m_uncomp_size;
      if (total > kMaxPackBytes)
      {
         err = "pack is too large";
         return false;
      }
   }

   const std::string staging = root + "/.staging-" + id;
   const std::string finalDir = root + "/" + id;
   const std::string backup = root + "/.old-" + id;
   std::error_code ec;
   fs::remove_all(AppPaths::FsPath(staging), ec);
   fs::remove_all(AppPaths::FsPath(backup), ec);
   if (!AppPaths::EnsureDir(staging))
   {
      err = "cannot create the pack folder";
      return false;
   }

   auto fail = [&](const std::string& why)
   {
      std::error_code e2;
      fs::remove_all(AppPaths::FsPath(staging), e2);
      err = why;
      return false;
   };

   for (mz_uint i = 0; i < count; i++)
   {
      mz_zip_archive_file_stat st;
      mz_zip_reader_file_stat(&zip, i, &st);
      const std::string rel = st.m_filename;
      if (mz_zip_reader_is_file_a_directory(&zip, i))
      {
         AppPaths::EnsureDir(staging + "/" + rel);
         continue;
      }
      const fs::path target = AppPaths::FsPath(staging + "/" + rel);
      fs::create_directories(target.parent_path(), ec);
#if defined(_WIN32)
      FILE* f = _wfopen(target.c_str(), L"wb");
#else
      FILE* f = fopen(target.c_str(), "wb");
#endif
      if (!f)
         return fail("cannot write " + rel);
      bool ok = mz_zip_reader_extract_to_callback(&zip, i, WriteToFile, f, 0) != 0;
      ok = (fclose(f) == 0) && ok;
      if (!ok)
         return fail("cannot unpack " + rel);
   }

   // Swap in; the previous install stays recoverable until the new one is in place.
   const bool hadOld = fs::exists(AppPaths::FsPath(finalDir), ec);
   if (hadOld)
   {
      fs::rename(AppPaths::FsPath(finalDir), AppPaths::FsPath(backup), ec);
      if (ec)
         return fail("cannot replace the installed pack");
   }
   fs::rename(AppPaths::FsPath(staging), AppPaths::FsPath(finalDir), ec);
   if (ec)
   {
      if (hadOld)
      {
         std::error_code e2;
         fs::rename(AppPaths::FsPath(backup), AppPaths::FsPath(finalDir), e2);
      }
      return fail("cannot install the pack");
   }
   fs::remove_all(AppPaths::FsPath(backup), ec);
   outId = id;
   return true;
}

bool Remove(const std::string& id)
{
   if (!IsValidId(id))
      return false;
   const std::string root = RootDir();
   if (root.empty())
      return false;
   std::error_code ec;
   fs::remove_all(AppPaths::FsPath(root + "/" + id), ec);
   return !ec;
}

void RefreshCatalog()
{
   RunWorker([]()
   {
      { std::lock_guard<std::mutex> l(gMutex); gShared.catalogLoading = true; }
      std::string body, httpErr;
      const bool ok = Platform::HttpGet(kCatalogUrl, std::string("Infinite/") + INFINITE_VERSION_STRING,
                                        body, httpErr, 15);
      std::vector<Pack> packs;
      std::string err;
      const bool parsed = ok && ParseManifest(body, PlatformKey(), packs, err);
      std::lock_guard<std::mutex> l(gMutex);
      gShared.catalogLoading = false;
      gShared.installed = ScanInstalled();
      gShared.catalogLoaded = parsed;
      gShared.catalogError = parsed ? std::string() : (ok ? err : (httpErr.empty() ? "offline" : httpErr));
      if (parsed)
         gShared.catalog = std::move(packs);
   });
}

void InstallAsync(const Pack& pack)
{
   RunWorker([pack]()
   {
      SetBusy(pack.id, "Downloading");
      gCancel.store(false);
      const std::string root = RootDir();
      if (root.empty())
      {
         Finish(false, "Install failed: no writable extensions folder");
         return;
      }
      const std::string part = root + "/.download-" + pack.id + ".part";
      std::string httpErr;
      const bool got = Platform::HttpDownload(pack.url, std::string("Infinite/") + INFINITE_VERSION_STRING, part,
         [&pack](uint64_t done, uint64_t total)
         {
            const uint64_t denom = total ? total : pack.size;
            {
               std::lock_guard<std::mutex> l(gMutex);
               gShared.progress = denom ? (float)((double)done / (double)denom) : -1.0f;
               if (gShared.progress > 1.0f) gShared.progress = 1.0f;
            }
            return !gCancel.load();
         }, httpErr, 600);
      if (!got)
      {
         Finish(false, httpErr == "cancelled" ? std::string("Download cancelled")
                                              : "Download failed: " + (httpErr.empty() ? std::string("network error") : httpErr));
         return;
      }
      std::string body;
      {
         std::ifstream in(AppPaths::FsPath(part), std::ios::binary);
         body.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      }
      std::error_code rmEc;
      fs::remove(AppPaths::FsPath(part), rmEc);
      SetBusy(pack.id, "Installing");
      std::string id, err;
      if (InstallZip(body, pack.sha256, pack.id, id, err))
         Finish(true, pack.name + " installed");
      else
         Finish(false, "Install failed: " + err);
   });
}

void InstallFileAsync(const std::string& path)
{
   RunWorker([path]()
   {
      SetBusy("file", "Installing");
      std::ifstream in(AppPaths::FsPath(path), std::ios::binary);
      if (!in)
      {
         Finish(false, "Could not read the file");
         return;
      }
      std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      std::string id, err;
      if (InstallZip(body, std::string(), std::string(), id, err))
         Finish(true, id + " installed");
      else
         Finish(false, "Install failed: " + err);
   });
}

void RemoveAsync(const std::string& id)
{
   RunWorker([id]()
   {
      SetBusy(id, "Removing");
      if (Remove(id))
         Finish(true, id + " removed");
      else
         Finish(false, "Could not remove " + id);
   });
}

void CancelInstall()
{
   gCancel.store(true);
}

void Poll()
{
   static bool scanned = false;
   if (!scanned)
   {
      scanned = true;
      std::lock_guard<std::mutex> l(gMutex);
      gShared.installed = ScanInstalled();
   }
   std::unique_lock<std::mutex> l(gMutex, std::try_to_lock);
   if (l.owns_lock())
      gView = gShared;
}

const State& GetState()
{
   return gView;
}

void Shutdown()
{
   if (gWorker.joinable())
      gWorker.join();
}

}

// Linux VST3 hosting - port of src/platform/win/PluginVST3Win.cpp's
// structure, which is itself the portable ~75% of PluginVST3.mm (host glue
// classes, create/init/process/parameter/state logic) ported essentially
// unchanged, plus the genuinely OS-specific pieces actually needed for
// anything to instantiate at all (module loading, UTF-16 handling), plus the
// out-of-process scanning + crash-safety machinery (EnumerateVST3Plugins,
// DescribeVST3Bundle, the sentinel/blocklist pair under
// AppPaths::AppSupportDir()), with posix_spawn/pipe/waitpid standing in for
// CreateProcessW/pipes and dlopen/dlsym/dlclose standing in for
// LoadLibraryExW/GetProcAddress/FreeLibrary.
//
// Task 4.3 (X11 editors) is implemented below, near the end of this file:
// PluginVST3OpenEditor creates a top-level X11 window and attaches the
// plugin's IPlugView to it via kPlatformTypeX11EmbedWindowID, backed by one
// process-wide Steinberg::Linux::IRunLoop exposed through both paths the
// VST3 Linux spec requires (HostPlugFrame::queryInterface for editors, and
// HostApplication::queryInterface - the host-context object passed to
// IPluginFactory3::setHostContext - for factory-context timers with no
// editor open at all). The GLFW/X11-touching parts of this (the editor
// window plumbing, HostRunLoop, HostPlugFrame) are guarded by
// `#if !defined(INFINITE_VST3_SCANNER)`: this file is also compiled into
// infinite-vst3-scanner (CMakeLists.txt), which is deliberately not linked
// against GLFW or X11 (scanning is out-of-process and headless - see
// phase-04-vst3.md task 4.5's judgment call) and never calls any of the
// editor entry points, so it gets the original stub bodies instead. The
// crash guard (RunPluginCallGuarded, POSIX sigsetjmp/siglongjmp, ported
// unchanged from PluginVST3.mm) now also wraps every editor-facing plugin
// call (createView/getSize/attached/onSize/removed/setContentScaleFactor/
// setFrame), not just state save/restore.

#include "PluginVST3.h"

#if INFINITE_ENABLE_VST3

#include "PluginHandleInternalLinux.h"
#include "../AppPaths.h"
#include "../common/PathOpen.h"

#include <dlfcn.h>
#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "crude_json.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"

extern char** environ;

namespace
{
   namespace fs = std::filesystem;

   void VST3Trace(const char* fmt, ...)
   {
      static const bool enabled = getenv("INFINITE_VST3TRACE") != nullptr;
      if (!enabled)
         return;
      va_list args;
      va_start(args, fmt);
      std::fprintf(stderr, "[vst3] ");
      std::vfprintf(stderr, fmt, args);
      std::fprintf(stderr, "\n");
      va_end(args);
      std::fflush(stderr);
   }

   // ------------------------------------------------------------------------
   // UID and string utilities - identical to PluginVST3.mm/PluginVST3Win.cpp,
   // zero OS calls.
   // ------------------------------------------------------------------------

   std::string TUIDToHexString(const Steinberg::TUID tuid)
   {
      char hex[33];
      for (int i = 0; i < 16; i++)
         std::snprintf(hex + i * 2, 3, "%02X", (unsigned char)tuid[i]);
      hex[32] = '\0';
      return std::string(hex);
   }

   bool HexStringToTUID(const std::string& hex, Steinberg::TUID outTUID)
   {
      if (hex.length() != 32)
         return false;
      for (int i = 0; i < 16; i++)
      {
         unsigned int byteVal = 0;
         if (std::sscanf(hex.substr(i * 2, 2).c_str(), "%02x", &byteVal) != 1)
            return false;
         outTUID[i] = (char)(unsigned char)byteVal;
      }
      return true;
   }

   std::string MakeVST3Identifier(const Steinberg::TUID tuid)
   {
      return "vst3:" + TUIDToHexString(tuid);
   }

   bool ParseVST3Identifier(const std::string& id, Steinberg::TUID outTUID)
   {
      if (id.rfind("vst3:", 0) != 0)
         return false;
      return HexStringToTUID(id.substr(5), outTUID);
   }

   // Steinberg::Vst::TChar is always UTF-16 (char16_t), but unlike Windows,
   // wchar_t on Linux is 32-bit - there is no width-matching wchar_t helper
   // to reuse here, so this is a real (if minimal) UTF-16 <-> UTF-8 codec,
   // BMP + surrogate pairs, no error recovery beyond "stop at the first
   // malformed unit" (state/parameter strings from a well-behaved plugin
   // never exercise that path).
   std::string UTF16ToUTF8(const Steinberg::Vst::TChar* str)
   {
      std::string out;
      if (str == nullptr)
         return out;
      for (size_t i = 0; str[i] != 0; i++)
      {
         uint32_t cp = (uint16_t)str[i];
         if (cp >= 0xD800 && cp <= 0xDBFF && str[i + 1] != 0)
         {
            const uint32_t lo = (uint16_t)str[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
               cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
               i++;
            }
         }
         if (cp <= 0x7F)
         {
            out.push_back((char)cp);
         }
         else if (cp <= 0x7FF)
         {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
         }
         else if (cp <= 0xFFFF)
         {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
         }
         else
         {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
         }
      }
      return out;
   }

   void UTF8ToUTF16(const std::string& utf8, Steinberg::Vst::TChar* outStr, int maxChars)
   {
      if (outStr == nullptr || maxChars <= 0)
         return;
      int written = 0;
      size_t i = 0;
      while (i < utf8.size() && written < maxChars - 1)
      {
         const unsigned char c0 = (unsigned char)utf8[i];
         uint32_t cp = 0;
         size_t len = 1;
         if ((c0 & 0x80) == 0) { cp = c0; len = 1; }
         else if ((c0 & 0xE0) == 0xC0 && i + 1 < utf8.size()) { cp = c0 & 0x1F; len = 2; }
         else if ((c0 & 0xF0) == 0xE0 && i + 2 < utf8.size()) { cp = c0 & 0x0F; len = 3; }
         else if ((c0 & 0xF8) == 0xF0 && i + 3 < utf8.size()) { cp = c0 & 0x07; len = 4; }
         else { i++; continue; } // skip malformed byte

         bool ok = true;
         for (size_t k = 1; k < len; k++)
         {
            const unsigned char c = (unsigned char)utf8[i + k];
            if ((c & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (c & 0x3F);
         }
         if (!ok) { i++; continue; }
         i += len;

         if (cp <= 0xFFFF)
         {
            outStr[written++] = (Steinberg::Vst::TChar)cp;
         }
         else if (written + 1 < maxChars - 1 || (written + 1 == maxChars - 1))
         {
            cp -= 0x10000;
            if (written + 2 > maxChars - 1)
               break;
            outStr[written++] = (Steinberg::Vst::TChar)(0xD800 + (cp >> 10));
            outStr[written++] = (Steinberg::Vst::TChar)(0xDC00 + (cp & 0x3FF));
         }
      }
      outStr[written] = 0;
   }

   // ------------------------------------------------------------------------
   // Minimal base64, needed only for state save/restore blobs - identical to
   // PluginVST3Win.cpp's.
   // ------------------------------------------------------------------------

   std::string Base64Encode(const std::vector<uint8_t>& data)
   {
      static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string out;
      out.reserve(((data.size() + 2) / 3) * 4);
      size_t i = 0;
      while (i + 3 <= data.size())
      {
         const uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
         out.push_back(kTable[(n >> 18) & 0x3F]);
         out.push_back(kTable[(n >> 12) & 0x3F]);
         out.push_back(kTable[(n >> 6) & 0x3F]);
         out.push_back(kTable[n & 0x3F]);
         i += 3;
      }
      const size_t remaining = data.size() - i;
      if (remaining == 1)
      {
         const uint32_t n = data[i] << 16;
         out.push_back(kTable[(n >> 18) & 0x3F]);
         out.push_back(kTable[(n >> 12) & 0x3F]);
         out.push_back('=');
         out.push_back('=');
      }
      else if (remaining == 2)
      {
         const uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
         out.push_back(kTable[(n >> 18) & 0x3F]);
         out.push_back(kTable[(n >> 12) & 0x3F]);
         out.push_back(kTable[(n >> 6) & 0x3F]);
         out.push_back('=');
      }
      return out;
   }

   bool Base64Decode(const std::string& in, std::vector<uint8_t>& out)
   {
      auto decodeChar = [](char c) -> int
      {
         if (c >= 'A' && c <= 'Z') return c - 'A';
         if (c >= 'a' && c <= 'z') return c - 'a' + 26;
         if (c >= '0' && c <= '9') return c - '0' + 52;
         if (c == '+') return 62;
         if (c == '/') return 63;
         return -1;
      };
      out.clear();
      int vals[4];
      int count = 0;
      for (char c : in)
      {
         if (c == '=' || c == '\r' || c == '\n')
            continue;
         const int v = decodeChar(c);
         if (v < 0)
            return false;
         vals[count++] = v;
         if (count == 4)
         {
            const uint32_t n = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6) | vals[3];
            out.push_back((uint8_t)((n >> 16) & 0xFF));
            out.push_back((uint8_t)((n >> 8) & 0xFF));
            out.push_back((uint8_t)(n & 0xFF));
            count = 0;
         }
      }
      if (count == 3)
      {
         const uint32_t n = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6);
         out.push_back((uint8_t)((n >> 16) & 0xFF));
         out.push_back((uint8_t)((n >> 8) & 0xFF));
      }
      else if (count == 2)
      {
         const uint32_t n = (vals[0] << 18) | (vals[1] << 12);
         out.push_back((uint8_t)((n >> 16) & 0xFF));
      }
      else if (count == 1)
      {
         return false; // malformed
      }
      return true;
   }
}

namespace Platform
{
   // Identifier -> bundle path cache, exactly as PluginVST3.mm's/
   // PluginVST3Win.cpp's. Populated both by a desc.path that resolved
   // successfully and by every class DescribeVST3Bundle finds during a scan.
   static std::mutex gBundleMapMutex;
   static std::unordered_map<std::string, std::string> gVST3BundleMap;

   void CacheVST3BundlePath(const std::string& identifier, const std::string& bundlePath)
   {
      std::lock_guard<std::mutex> lock(gBundleMapMutex);
      gVST3BundleMap[identifier] = bundlePath;
   }

   std::string GetCachedVST3BundlePath(const std::string& identifier)
   {
      std::lock_guard<std::mutex> lock(gBundleMapMutex);
      auto it = gVST3BundleMap.find(identifier);
      return it != gVST3BundleMap.end() ? it->second : std::string();
   }
}

namespace
{
   namespace fsProbe = std::filesystem;

   // Forward declarations: DescribeVST3Bundle below needs these before their
   // full definitions, which live further down in the module-loading
   // section of this file.
   void* LoadVST3Module(const std::string& bundlePath, Steinberg::IPluginFactory** outFactory);
   void UnloadVST3Module(void* module);

   // ------------------------------------------------------------------------
   // User-added VST3 search folders - mirrors PluginVST3.mm/
   // PluginVST3Win.cpp's gExtraVST3SearchFolders exactly (same call site:
   // PluginScanner::Folders() via Platform::SetVST3SearchFolders).
   // ------------------------------------------------------------------------
   std::mutex gSearchFoldersMutex;
   std::vector<std::string> gExtraVST3SearchFolders;

   std::vector<std::string> GetExtraVST3SearchFolders()
   {
      std::lock_guard<std::mutex> lock(gSearchFoldersMutex);
      return gExtraVST3SearchFolders;
   }

   // ------------------------------------------------------------------------
   // Crash-safety: sentinel + blocklist
   //
   // DescribeVST3Bundle loads an arbitrary third party's compiled code
   // in-process (dlopen -> ModuleEntry -> read factory -> ModuleExit). A
   // hostile or simply broken bundle can segfault/abort from inside that
   // call, which a C++ try/catch cannot intercept. The sentinel records
   // which bundle is being probed *before* the call, so if this process is
   // dead the next time the app launches, the last-probed path is still
   // sitting in the sentinel file and gets blocklisted rather than killing
   // every future scan the same way. Port of PluginVST3Win.cpp's equivalent
   // section, AppPaths::AppSupportDir() standing in for %APPDATA%\Infinite.
   // ------------------------------------------------------------------------

   std::string SettingsDirForVST3()
   {
      std::string dir = AppPaths::AppSupportDir(); // creates if missing
      if (dir.empty())
         return {};
      if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
      {
         dir += "/plugin_drag_test";
         AppPaths::EnsureDir(dir);
      }
      return dir;
   }

   std::string VST3SentinelPath()
   {
      const std::string dir = SettingsDirForVST3();
      return dir.empty() ? std::string() : dir + "/PluginScanSentinel.txt";
   }

   std::string VST3BlocklistPath()
   {
      const std::string dir = SettingsDirForVST3();
      return dir.empty() ? std::string() : dir + "/PluginVST3Blocklist.json";
   }

   std::mutex gVST3SafetyMutex; // guards gBlocklist and every sentinel/failure op below
   std::vector<std::string> gBlocklist;
   std::vector<std::string> gScanFailures;
   std::vector<std::string> gUnsupportedPlugins; // guarded by gVST3SafetyMutex, see VST3ScanFailures
   bool gBlocklistLoaded = false;

   void LoadBlocklistLocked()
   {
      if (gBlocklistLoaded)
         return;
      gBlocklistLoaded = true;
      const std::string path = VST3BlocklistPath();
      if (path.empty())
         return;
      auto [json, ok] = crude_json::value::load(path);
      if (!ok || !json.is_array())
         return;
      for (const crude_json::value& v : json.get<crude_json::array>())
         if (v.is_string())
            gBlocklist.push_back(v.get<crude_json::string>());
   }

   void SaveBlocklistLocked()
   {
      const std::string path = VST3BlocklistPath();
      if (path.empty())
         return;
      crude_json::value json = crude_json::array {};
      for (const std::string& p : gBlocklist)
         json.push_back(crude_json::value(p));
      json.save(path, 2);
   }

   // Checked once per process, before the first bundle is ever probed: if the
   // previous run's sentinel is still sitting there non-empty, that run died
   // mid-probe of that exact bundle.
   void CheckSentinelForCrashLocked()
   {
      const std::string path = VST3SentinelPath();
      if (path.empty())
         return;
      FILE* f = OpenFileUtf8(path, "rb");
      if (f == nullptr)
         return;
      char buf[4096] = {};
      size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
      std::fclose(f);
      if (n == 0)
         return;
      std::string crashed(buf, n);
      while (!crashed.empty() && (crashed.back() == '\n' || crashed.back() == '\r'))
         crashed.pop_back();
      if (crashed.empty())
         return;

      LoadBlocklistLocked();
      if (std::find(gBlocklist.begin(), gBlocklist.end(), crashed) == gBlocklist.end())
      {
         VST3Trace("sentinel found non-empty at startup - blocklisting: %s", crashed.c_str());
         gBlocklist.push_back(crashed);
         SaveBlocklistLocked();
      }
      std::remove(path.c_str());
   }

   void EnsureSentinelCheckedOnce()
   {
      static std::once_flag once;
      std::call_once(once, [] {
         std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
         CheckSentinelForCrashLocked();
      });
   }

   bool IsBlocklistedPath(const std::string& bundlePath)
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      LoadBlocklistLocked();
      return std::find(gBlocklist.begin(), gBlocklist.end(), bundlePath) != gBlocklist.end();
   }

   // Written+flushed just before the in-process probe, cleared right after a
   // clean return (success or ordinary failure). Deliberately not RAII: the
   // whole point is to survive the case where the destructor never runs.
   // fsync() is _commit()'s POSIX equivalent - without it the write can
   // still be sitting in the page cache at the moment a crashing plugin
   // takes this process down with it.
   void WriteSentinel(const std::string& bundlePath)
   {
      const std::string path = VST3SentinelPath();
      if (path.empty())
         return;
      FILE* f = OpenFileUtf8(path, "wb");
      if (f == nullptr)
         return;
      std::fwrite(bundlePath.data(), 1, bundlePath.size(), f);
      std::fflush(f);
      fsync(fileno(f));
      std::fclose(f);
   }

   void ClearSentinel()
   {
      const std::string path = VST3SentinelPath();
      if (!path.empty())
         std::remove(path.c_str());
   }

   void RecordScanFailure(const std::string& bundlePath)
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      gScanFailures.push_back(bundlePath);
   }

   // Adds a bundle to the persisted blocklist immediately, in the same scan
   // that caught it crashing/hanging - unlike the sentinel path above (which
   // only catches a crash on the *next* launch), this fires the moment
   // ProbeVST3BundleOutOfProcess sees a dead or unresponsive child.
   void AddToBlocklistLocked(const std::string& bundlePath)
   {
      LoadBlocklistLocked();
      if (std::find(gBlocklist.begin(), gBlocklist.end(), bundlePath) == gBlocklist.end())
      {
         gBlocklist.push_back(bundlePath);
         SaveBlocklistLocked();
      }
   }

   // ------------------------------------------------------------------------
   // Out-of-process bundle probing
   //
   // DescribeVST3Bundle runs arbitrary third-party code in-process and is
   // only safe to call directly from the "--vst3-scan-bundle" child mode in
   // main.cpp, or from the dedicated infinite-vst3-scanner's own main
   // (src/scanner_main_linux.cpp), where a crash costs one disposable
   // process. EnumerateVST3Plugins below re-execs one of those binaries once
   // per batch instead of calling DescribeVST3Bundle itself, so a crashing
   // or hanging plugin never takes the scan - or the app - down with it.
   // ------------------------------------------------------------------------

   enum class ProbeOutcome
   {
      Success,   // child exited cleanly and described at least one class
      CleanMiss, // child exited cleanly but found nothing usable - not a crash
      Crashed,   // child was terminated, exited nonzero, or hung
   };

   // Tab-separated wire format, identical to PluginVST3Win.cpp's
   // ParseProbeOutput - no text-mode CRLF translation exists on Linux, but a
   // defensive trailing-'\r' strip costs nothing and keeps this byte-for-byte
   // compatible if the format is ever shared code.
   std::vector<Platform::PluginDesc> ParseProbeOutput(const std::string& output)
   {
      std::vector<Platform::PluginDesc> out;
      size_t pos = 0;
      while (pos < output.size())
      {
         size_t nl = output.find('\n', pos);
         std::string line = output.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
         pos = (nl == std::string::npos) ? output.size() : nl + 1;
         if (!line.empty() && line.back() == '\r')
            line.pop_back();
         if (line.empty())
            continue;

         std::vector<std::string> fields;
         size_t p = 0;
         while (true)
         {
            size_t tab = line.find('\t', p);
            fields.push_back(line.substr(p, tab == std::string::npos ? std::string::npos : tab - p));
            if (tab == std::string::npos)
               break;
            p = tab + 1;
         }
         if (fields.size() != 6)
            continue;

         Platform::PluginDesc d;
         d.format = fields[0];
         d.name = fields[1];
         d.manufacturer = fields[2];
         d.identifier = fields[3];
         d.path = fields[4];
         d.acceptsNotes = fields[5] == "1";
         out.push_back(std::move(d));
      }
      return out;
   }

   struct DrainResult
   {
      std::string output;
      bool timedOut = false;
   };

   // Reads everything available from a non-blocking pipe fd until EOF or the
   // deadline passes. Linux equivalent of PluginVST3Win.cpp's
   // DrainChildStdout (PeekNamedPipe + ReadFile) using poll()/read() on an
   // O_NONBLOCK fd instead.
   DrainResult DrainChildStdout(int readFd, pid_t childPid, std::chrono::steady_clock::time_point deadline)
   {
      DrainResult result;
      char buf[4096];
      for (;;)
      {
         if (std::chrono::steady_clock::now() >= deadline)
         {
            result.timedOut = true;
            break;
         }

         const ssize_t n = ::read(readFd, buf, sizeof(buf));
         if (n > 0)
         {
            result.output.append(buf, (size_t)n);
            continue;
         }
         if (n == 0)
            break; // EOF - child closed its write end (exited or explicitly closed)
         if (errno == EAGAIN || errno == EWOULDBLOCK)
         {
            // Nothing to read yet. Check whether the child already exited
            // (non-blocking waitpid) so a hung-but-silent child is still
            // bounded by the deadline above, not by this poll.
            int status = 0;
            const pid_t r = ::waitpid(childPid, &status, WNOHANG);
            if (r == childPid)
            {
               // Drain any final bytes written right before exit, then stop.
               const ssize_t last = ::read(readFd, buf, sizeof(buf));
               if (last > 0)
               {
                  result.output.append(buf, (size_t)last);
                  continue;
               }
               break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
         }
         break; // real read error
      }
      return result;
   }

   // Spawns `exe` with the given argv, redirecting stdout to a pipe this
   // function drains and stderr to /dev/null (third-party plugin code prints
   // whatever it wants there, and the scan has nowhere useful to put it).
   // Mirrors PluginVST3.mm's posix_spawn + pipe + select loop exactly (same
   // primitives, unlike Windows' CreateProcessW).
   bool RunProbeChild(const std::string& exe, const std::vector<std::string>& args,
                       std::chrono::seconds timeout, std::string& outOutput, bool& outTimedOut,
                       bool& outCleanExit)
   {
      outOutput.clear();
      outTimedOut = false;
      outCleanExit = false;

      int pipeFds[2] = { -1, -1 };
      if (::pipe(pipeFds) != 0)
         return false;
      const int readFd = pipeFds[0];
      const int writeFd = pipeFds[1];

      // Non-blocking read end so DrainChildStdout's loop can honour the
      // deadline against a hung child instead of blocking on read().
      const int flags = ::fcntl(readFd, F_GETFL, 0);
      ::fcntl(readFd, F_SETFL, flags | O_NONBLOCK);

      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      posix_spawn_file_actions_addclose(&actions, readFd);
      posix_spawn_file_actions_adddup2(&actions, writeFd, STDOUT_FILENO);
      posix_spawn_file_actions_addclose(&actions, writeFd);
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

      std::vector<char*> argv;
      argv.push_back(const_cast<char*>(exe.c_str()));
      for (const std::string& a : args)
         argv.push_back(const_cast<char*>(a.c_str()));
      argv.push_back(nullptr);

      pid_t pid = -1;
      const int rc = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
      posix_spawn_file_actions_destroy(&actions);

      ::close(writeFd); // parent's copy must close so EOF arrives on child exit

      if (rc != 0)
      {
         ::close(readFd);
         return false;
      }

      const auto deadline = std::chrono::steady_clock::now() + timeout;
      DrainResult drained = DrainChildStdout(readFd, pid, deadline);
      ::close(readFd);
      outOutput = std::move(drained.output);
      outTimedOut = drained.timedOut;

      int status = 0;
      if (outTimedOut)
      {
         VST3Trace("probe child timed out, terminating");
         ::kill(pid, SIGKILL);
         ::waitpid(pid, &status, 0);
      }
      else
      {
         // Child already hit EOF; reap it (bounded - it just closed its
         // stdout, so it is at or near exit).
         for (int i = 0; i < 500; i++)
         {
            const pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid)
               break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
         }
      }

      outCleanExit = !outTimedOut && WIFEXITED(status) && WEXITSTATUS(status) == 0;
      return true;
   }

   ProbeOutcome ProbeVST3BundleOutOfProcess(const std::string& bundlePath, std::vector<Platform::PluginDesc>& out)
   {
      std::string exe = Platform::ScannerExecutablePath();
      bool isDedicatedScanner = true;
      if (exe.empty() || !fsProbe::exists(exe))
      {
         exe = Platform::ExecutablePath();
         isDedicatedScanner = false;
      }
      if (exe.empty())
         return ProbeOutcome::Crashed;

      std::vector<std::string> args;
      if (!isDedicatedScanner)
         args.push_back("--vst3-scan-bundle");
      args.push_back(bundlePath);

      std::string output;
      bool timedOut = false;
      bool cleanExit = false;
      if (!RunProbeChild(exe, args, std::chrono::seconds(10), output, timedOut, cleanExit))
         return ProbeOutcome::Crashed;

      if (!cleanExit)
      {
         VST3Trace("bundle crashed or timed out during out-of-process probe: %s", bundlePath.c_str());
         return ProbeOutcome::Crashed;
      }

      std::vector<Platform::PluginDesc> parsed = ParseProbeOutput(output);
      if (parsed.empty())
         return ProbeOutcome::CleanMiss;

      for (Platform::PluginDesc& d : parsed)
         out.push_back(std::move(d));
      return ProbeOutcome::Success;
   }

   void ProbeVST3BundlesBatch(std::vector<std::string> bundlesToScan, std::vector<Platform::PluginDesc>& out)
   {
      const std::string exe = Platform::ScannerExecutablePath();
      if (exe.empty() || !fsProbe::exists(exe))
      {
         // No dedicated scanner built - fall back to probing one at a time
         // via the self re-exec path, same as macOS/Windows' fallback.
         for (const auto& path : bundlesToScan)
         {
            if (IsBlocklistedPath(path))
            {
               RecordScanFailure(path);
               continue;
            }
            const ProbeOutcome outcome = ProbeVST3BundleOutOfProcess(path, out);
            if (outcome == ProbeOutcome::Crashed)
            {
               RecordScanFailure(path);
               std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
               AddToBlocklistLocked(path);
            }
            else if (outcome == ProbeOutcome::CleanMiss)
            {
               RecordScanFailure(path);
            }
         }
         return;
      }

      // Hard bound on the retry loop below. Each non-clean pass is guaranteed
      // to remove at least one bundle (see the forward-progress block), so the
      // loop already terminates in <= N passes; this is a belt-and-suspenders
      // cap so a pathological case can never leave the scan spinning forever.
      const size_t maxIterations = bundlesToScan.size() * 2 + 8;
      size_t iterationGuard = 0;

      while (!bundlesToScan.empty())
      {
         if (++iterationGuard > maxIterations)
            break; // give up on the rest, publish whatever was described

         auto it = bundlesToScan.begin();
         while (it != bundlesToScan.end())
         {
            if (IsBlocklistedPath(*it))
            {
               RecordScanFailure(*it);
               it = bundlesToScan.erase(it);
            }
            else
            {
               ++it;
            }
         }
         if (bundlesToScan.empty())
            break;

         std::vector<std::string> args;
         args.push_back("--batch");
         for (const auto& b : bundlesToScan)
            args.push_back(b);

         std::string output;
         bool timedOut = false;
         bool cleanExit = false;
         if (!RunProbeChild(exe, args, std::chrono::seconds(60), output, timedOut, cleanExit))
            break;

         std::vector<Platform::PluginDesc> parsed = ParseProbeOutput(output);
         std::vector<std::string> describedPaths;
         for (const Platform::PluginDesc& d : parsed)
            if (!d.path.empty())
               describedPaths.push_back(d.path);
         for (Platform::PluginDesc& d : parsed)
            out.push_back(std::move(d));

         if (timedOut)
         {
            EnsureSentinelCheckedOnce();
            break;
         }
         if (cleanExit)
            break;

         // Non-zero/killed exit with a partial or empty parse: the sentinel
         // (written by whichever bundle the child was mid-probing) is what
         // identifies and blocklists the offender on the next launch, same
         // as the single-probe path.
         EnsureSentinelCheckedOnce();

         // Forward-progress guarantee: drop every bundle this pass already
         // described so the next pass neither re-lists them nor re-crashes
         // the child on its way back to the offender.
         for (const std::string& described : describedPaths)
         {
            auto jt = bundlesToScan.begin();
            while (jt != bundlesToScan.end())
            {
               if (*jt == described)
                  jt = bundlesToScan.erase(jt);
               else
                  ++jt;
            }
         }

         // If the pass described nothing at all, the batch child died on the
         // very first bundle (it probes in list order) - blocklist that
         // front bundle ourselves and move on.
         if (describedPaths.empty() && !bundlesToScan.empty())
         {
            const std::string offender = bundlesToScan.front();
            {
               std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
               AddToBlocklistLocked(offender);
            }
            RecordScanFailure(offender);
            bundlesToScan.erase(bundlesToScan.begin());
         }
      }
   }
}

namespace Platform
{
   void SetVST3SearchFolders(const std::vector<std::string>& folders)
   {
      std::lock_guard<std::mutex> lock(gSearchFoldersMutex);
      gExtraVST3SearchFolders = folders;
   }

   std::vector<std::string> VST3Blocklist()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      LoadBlocklistLocked();
      return gBlocklist;
   }

   void ClearVST3Blocklist()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      LoadBlocklistLocked();
      gBlocklist.clear();
      SaveBlocklistLocked();
   }

   std::vector<std::string> VST3ScanFailures()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      return gScanFailures;
   }

   std::vector<std::string> UnsupportedPluginsSeen()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      return gUnsupportedPlugins;
   }

   // Recursive folder walk building the batch DescribeVST3Bundle probes.
   // Like Windows (and unlike macOS, where only the directory-bundle form
   // exists), a plain single-file "Foo.vst3" shared object is also legal and
   // common on Linux, so both forms are collected here - the directory form
   // is not recursed into further once identified as a bundle. A top-level
   // ".so" that is not itself named "*.vst3" is the classic Linux VST2
   // module (conventionally under ~/.vst) - filename check only, never
   // loaded, only surfaced via UnsupportedPluginsSeen().
   void EnumerateVST3Plugins(const std::vector<std::string>& folders, std::vector<PluginDesc>& out)
   {
      EnsureSentinelCheckedOnce();
      {
         std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
         gScanFailures.clear();
         gUnsupportedPlugins.clear();
      }

      std::vector<std::string> bundlesToScan;
      std::vector<std::string> unsupported;
      for (const std::string& root : folders)
      {
         if (root.empty())
            continue;
         std::vector<fsProbe::path> dirStack;
         dirStack.push_back(root);

         while (!dirStack.empty())
         {
            fsProbe::path dir = std::move(dirStack.back());
            dirStack.pop_back();

            std::error_code ec;
            fsProbe::directory_iterator it(dir, fsProbe::directory_options::skip_permission_denied, ec);
            const fsProbe::directory_iterator end;
            if (ec)
               continue;

            while (it != end)
            {
               const fsProbe::directory_entry entry = *it;
               std::error_code entryEc;
               if (entry.is_directory(entryEc) && !entryEc)
               {
                  if (entry.path().extension() == ".vst3")
                     bundlesToScan.push_back(entry.path().string());
                  else
                     dirStack.push_back(entry.path());
               }
               else if (entry.is_regular_file(entryEc) && !entryEc && entry.path().extension() == ".vst3")
               {
                  bundlesToScan.push_back(entry.path().string());
               }
               else if (!entryEc)
               {
                  // A top-level file that isn't a ".vst3" - never anything
                  // living inside a ".vst3" bundle directory, since those
                  // are never pushed onto dirStack. A ".so" here is the
                  // classic Linux VST2 plugin binary.
                  const std::string ext = entry.path().extension().string();
                  if (ext == ".so")
                     unsupported.push_back(entry.path().string());
               }
               it.increment(ec);
               if (ec)
                  break;
            }
         }
      }

      {
         std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
         gUnsupportedPlugins = std::move(unsupported);
      }

      ProbeVST3BundlesBatch(std::move(bundlesToScan), out);
   }

   bool DescribeVST3Bundle(const std::string& bundlePath, std::vector<PluginDesc>& out)
   {
      EnsureSentinelCheckedOnce();
      if (IsBlocklistedPath(bundlePath))
      {
         VST3Trace("refusing blocklisted bundle: %s", bundlePath.c_str());
         return false;
      }

      // Sentinel window: everything between here and ClearSentinel() below
      // runs arbitrary third-party code in-process (dlopen, ModuleEntry, the
      // factory constructor). If this process doesn't survive that, the
      // next launch finds the sentinel still pointing at this exact bundle
      // and blocklists it instead of repeating the crash.
      WriteSentinel(bundlePath);

      Steinberg::IPluginFactory* factoryRaw = nullptr;
      void* module = LoadVST3Module(bundlePath, &factoryRaw);
      if (module == nullptr || factoryRaw == nullptr)
      {
         ClearSentinel();
         return false;
      }

      Steinberg::IPtr<Steinberg::IPluginFactory> factory(factoryRaw);
      Steinberg::IPtr<Steinberg::IPluginFactory2> factory2;
      factory->queryInterface(Steinberg::IPluginFactory2::iid, (void**)&factory2);

      const Steinberg::int32 numClasses = factory->countClasses();
      bool foundAny = false;

      for (Steinberg::int32 i = 0; i < numClasses; i++)
      {
         Steinberg::PClassInfo classInfo = {};
         Steinberg::PClassInfo2 classInfo2 = {};
         std::string category;
         std::string name;
         std::string vendor;
         std::string subCategories;
         Steinberg::TUID classId = {};

         if (factory2)
         {
            if (factory2->getClassInfo2(i, &classInfo2) == Steinberg::kResultOk)
            {
               category = classInfo2.category;
               name = classInfo2.name;
               vendor = classInfo2.vendor;
               subCategories = classInfo2.subCategories;
               std::memcpy(classId, classInfo2.cid, sizeof(Steinberg::TUID));
            }
         }
         else
         {
            if (factory->getClassInfo(i, &classInfo) == Steinberg::kResultOk)
            {
               category = classInfo.category;
               name = classInfo.name;
               std::memcpy(classId, classInfo.cid, sizeof(Steinberg::TUID));
            }
         }

         VST3Trace("  class[%d] category='%s' name='%s' uid=%s", (int)i, category.c_str(), name.c_str(),
                   TUIDToHexString(classId).c_str());

         if (category == kVstAudioEffectClass)
         {
            PluginDesc desc;
            desc.format = "vst3";
            desc.name = name;
            desc.manufacturer = vendor;
            desc.identifier = MakeVST3Identifier(classId);
            desc.path = bundlePath;

            std::string subCatLower = subCategories;
            std::transform(subCatLower.begin(), subCatLower.end(), subCatLower.begin(), ::tolower);
            desc.acceptsNotes = (subCatLower.find("instrument") != std::string::npos ||
                                 subCatLower.find("synth") != std::string::npos);

            CacheVST3BundlePath(desc.identifier, bundlePath);
            out.push_back(std::move(desc));
            foundAny = true;
         }
      }

      // Release our references to the factory *before* dlclose()'ing the
      // module. IPtr<>'s destructor calls the plugin's virtual release(),
      // which is code that lives inside the .so; if that call happens after
      // UnloadVST3Module() below has already unmapped it (factory/factory2
      // would otherwise be destroyed when this function returns, which is
      // after the dlclose() call), it's a use-after-unload into freed
      // memory - reliably a SIGSEGV, reproduced against a real VST3 plugin
      // (DPF's "Info" example) during Phase 4 task 4.4 CI-test work. This is
      // our bug (ordering), not the plugin's - Tier 1 per
      // .claude/skills/plugin-host-hardening, fixed here rather than
      // contained.
      factory2 = nullptr;
      factory = nullptr;

      UnloadVST3Module(module);
      ClearSentinel();
      return foundAny;
   }
}

namespace
{
   // ------------------------------------------------------------------------
   // MemoryStream for IBStream state save/restore - identical to
   // PluginVST3.mm/PluginVST3Win.cpp.
   // ------------------------------------------------------------------------

   class MemoryStream : public Steinberg::IBStream
   {
   public:
      MemoryStream() = default;
      explicit MemoryStream(const void* data, size_t size)
      {
         if (data != nullptr && size > 0)
            mBuffer.assign((const uint8_t*)data, (const uint8_t*)data + size);
      }

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IBStream::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesRead) override
      {
         if (buffer == nullptr || numBytes < 0)
            return Steinberg::kInvalidArgument;
         const size_t available = (mPos < mBuffer.size()) ? (mBuffer.size() - mPos) : 0;
         const size_t toRead = std::min((size_t)numBytes, available);
         if (toRead > 0)
         {
            std::memcpy(buffer, mBuffer.data() + mPos, toRead);
            mPos += toRead;
         }
         if (numBytesRead != nullptr)
            *numBytesRead = (Steinberg::int32)toRead;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesWritten) override
      {
         if (buffer == nullptr || numBytes < 0)
            return Steinberg::kInvalidArgument;
         if (mPos + (size_t)numBytes > mBuffer.size())
            mBuffer.resize(mPos + (size_t)numBytes);
         std::memcpy(mBuffer.data() + mPos, buffer, (size_t)numBytes);
         mPos += (size_t)numBytes;
         if (numBytesWritten != nullptr)
            *numBytesWritten = numBytes;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API seek(Steinberg::int64 offset, Steinberg::int32 mode, Steinberg::int64* result) override
      {
         Steinberg::int64 newPos = (Steinberg::int64)mPos;
         if (mode == Steinberg::IBStream::kIBSeekSet)
            newPos = offset;
         else if (mode == Steinberg::IBStream::kIBSeekCur)
            newPos += offset;
         else if (mode == Steinberg::IBStream::kIBSeekEnd)
            newPos = (Steinberg::int64)mBuffer.size() + offset;

         if (newPos < 0)
            return Steinberg::kInvalidArgument;
         mPos = (size_t)newPos;
         if (result != nullptr)
            *result = (Steinberg::int64)mPos;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API tell(Steinberg::int64* result) override
      {
         if (result != nullptr)
            *result = (Steinberg::int64)mPos;
         return Steinberg::kResultOk;
      }

      const std::vector<uint8_t>& getBuffer() const { return mBuffer; }
      size_t getSize() const { return mBuffer.size(); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::vector<uint8_t> mBuffer;
      size_t mPos = 0;
   };

   // ------------------------------------------------------------------------
   // Host Application Context - identical to PluginVST3.mm/PluginVST3Win.cpp.
   // ------------------------------------------------------------------------

#if !defined(INFINITE_VST3_SCANNER)
   // Forward declaration only - the concrete HostRunLoop class is defined
   // further down in this same anonymous namespace (guarded by
   // INFINITE_VST3_SCANNER, same as this forward declaration), alongside the
   // rest of the X11 editor plumbing. Declared here as the interface type
   // (not the concrete HostRunLoop type, which is still incomplete at this
   // point) so HostApplication::queryInterface below can call it without
   // needing HostRunLoop's full definition.
   Steinberg::Linux::IRunLoop* SharedRunLoop();
#endif

   class HostApplication : public Steinberg::Vst::IHostApplication
   {
   public:
      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IHostApplication::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
#if !defined(INFINITE_VST3_SCANNER)
         // Path (b) of the Linux run-loop spec (task 4.3): the host-context
         // object passed to IPluginFactory3::setHostContext (see
         // PluginVST3Create below) must also answer IRunLoop, so a plugin
         // can register fd handlers/timers with no editor open at all -
         // JUCE and DPF both rely on this. Not available in the out-of-
         // process scanner build (headless, no GLFW/X11 linked).
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid))
            return SharedRunLoop()->queryInterface(iid, obj);
#endif
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
      {
         UTF8ToUTF16("Infinite", name, 128);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void** obj) override;

   private:
      std::atomic<uint32_t> mRefCount { 1 };
   };

   class HostAttributeList : public Steinberg::Vst::IAttributeList
   {
   public:
      using AttrID = Steinberg::Vst::IAttributeList::AttrID;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IAttributeList::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API setInt(AttrID aid, Steinberg::int64 value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kInt;
         a.intValue = value;
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getInt(AttrID aid, Steinberg::int64& value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kInt)
            return Steinberg::kResultFalse;
         value = it->second.intValue;
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API setFloat(AttrID aid, double value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kFloat;
         a.floatValue = value;
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getFloat(AttrID aid, double& value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kFloat)
            return Steinberg::kResultFalse;
         value = it->second.floatValue;
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API setString(AttrID aid, const Steinberg::Vst::TChar* string) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kString;
         if (string != nullptr)
         {
            size_t len = 0;
            while (string[len] != 0)
               len++;
            a.stringValue.assign(string, string + len);
         }
         a.stringValue.push_back(0);
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getString(AttrID aid, Steinberg::Vst::TChar* string, Steinberg::uint32 sizeInBytes) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kString)
            return Steinberg::kResultFalse;
         const size_t haveBytes = it->second.stringValue.size() * sizeof(Steinberg::Vst::TChar);
         const size_t copyBytes = std::min<size_t>(haveBytes, sizeInBytes);
         std::memcpy(string, it->second.stringValue.data(), copyBytes);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API setBinary(AttrID aid, const void* data, Steinberg::uint32 sizeInBytes) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kBinary;
         const uint8_t* p = static_cast<const uint8_t*>(data);
         a.binaryValue.assign(p, p + sizeInBytes);
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getBinary(AttrID aid, const void*& data, Steinberg::uint32& sizeInBytes) override
      {
         if (!aid)
         {
            sizeInBytes = 0;
            return Steinberg::kInvalidArgument;
         }
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kBinary)
         {
            sizeInBytes = 0;
            return Steinberg::kResultFalse;
         }
         data = it->second.binaryValue.data();
         sizeInBytes = (Steinberg::uint32)it->second.binaryValue.size();
         return Steinberg::kResultTrue;
      }

   private:
      struct Attribute
      {
         enum class Type { kInt, kFloat, kString, kBinary } type = Type::kInt;
         Steinberg::int64 intValue = 0;
         double floatValue = 0.0;
         std::vector<Steinberg::Vst::TChar> stringValue;
         std::vector<uint8_t> binaryValue;
      };

      std::atomic<uint32_t> mRefCount { 1 };
      std::map<std::string, Attribute> mAttrs;
   };

   class HostMessage : public Steinberg::Vst::IMessage
   {
   public:
      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IMessage::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::FIDString PLUGIN_API getMessageID() override
      {
         return mMessageId.empty() ? nullptr : mMessageId.c_str();
      }

      void PLUGIN_API setMessageID(Steinberg::FIDString mid) override
      {
         mMessageId = (mid != nullptr) ? mid : "";
      }

      Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override
      {
         if (!mAttributes)
            mAttributes = Steinberg::owned(new HostAttributeList());
         return mAttributes.get();
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::string mMessageId;
      Steinberg::IPtr<Steinberg::Vst::IAttributeList> mAttributes;
   };

   Steinberg::tresult PLUGIN_API HostApplication::createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void** obj)
   {
      if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IMessage::iid) &&
          Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IMessage::iid))
      {
         *obj = new HostMessage();
         return Steinberg::kResultTrue;
      }
      if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IAttributeList::iid) &&
          Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IAttributeList::iid))
      {
         *obj = new HostAttributeList();
         return Steinberg::kResultTrue;
      }
      *obj = nullptr;
      return Steinberg::kNoInterface;
   }

   Steinberg::Vst::IHostApplication* SharedHostApplication()
   {
      static HostApplication* instance = new HostApplication();
      return instance;
   }

   // ------------------------------------------------------------------------
   // Real-time safe event list / parameter changes - identical to
   // PluginVST3.mm/PluginVST3Win.cpp.
   // ------------------------------------------------------------------------

   class HostEventList : public Steinberg::Vst::IEventList
   {
   public:
      static constexpr int kMaxEvents = 64;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IEventList::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::int32 PLUGIN_API getEventCount() override
      {
         return mCount.load(std::memory_order_relaxed);
      }

      Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override
      {
         const int count = mCount.load(std::memory_order_relaxed);
         if (index < 0 || index >= count)
            return Steinberg::kInvalidArgument;
         e = mEvents[index];
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override
      {
         int count = mCount.load(std::memory_order_relaxed);
         if (count >= kMaxEvents)
            return Steinberg::kResultFalse;
         mEvents[count] = e;
         mCount.store(count + 1, std::memory_order_relaxed);
         return Steinberg::kResultOk;
      }

      void clear() { mCount.store(0, std::memory_order_relaxed); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::atomic<int> mCount { 0 };
      Steinberg::Vst::Event mEvents[kMaxEvents] = {};
   };

   class HostParamValueQueue : public Steinberg::Vst::IParamValueQueue
   {
   public:
      static constexpr int kMaxPoints = 8;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IParamValueQueue::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return mParamId; }

      Steinberg::int32 PLUGIN_API getPointCount() override
      {
         return mPointCount.load(std::memory_order_relaxed);
      }

      Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                             Steinberg::Vst::ParamValue& value) override
      {
         const int count = mPointCount.load(std::memory_order_relaxed);
         if (index < 0 || index >= count)
            return Steinberg::kInvalidArgument;
         sampleOffset = mOffsets[index];
         value = mValues[index];
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value,
                                             Steinberg::int32& index) override
      {
         int count = mPointCount.load(std::memory_order_relaxed);
         if (count >= kMaxPoints)
         {
            index = count - 1;
            mOffsets[index] = sampleOffset;
            mValues[index] = value;
            return Steinberg::kResultOk;
         }
         index = count;
         mOffsets[index] = sampleOffset;
         mValues[index] = value;
         mPointCount.store(count + 1, std::memory_order_relaxed);
         return Steinberg::kResultOk;
      }

      void init(Steinberg::Vst::ParamID id)
      {
         mParamId = id;
         mPointCount.store(0, std::memory_order_relaxed);
      }

      void clear() { mPointCount.store(0, std::memory_order_relaxed); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      Steinberg::Vst::ParamID mParamId = 0;
      std::atomic<int> mPointCount { 0 };
      Steinberg::int32 mOffsets[kMaxPoints] = {};
      Steinberg::Vst::ParamValue mValues[kMaxPoints] = {};
   };

   class HostParameterChanges : public Steinberg::Vst::IParameterChanges
   {
   public:
      static constexpr int kMaxQueues = 32;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IParameterChanges::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::int32 PLUGIN_API getParameterCount() override
      {
         return mQueueCount.load(std::memory_order_relaxed);
      }

      Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
      {
         const int count = mQueueCount.load(std::memory_order_relaxed);
         if (index < 0 || index >= count)
            return nullptr;
         return &mQueues[index];
      }

      Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                    Steinberg::int32& index) override
      {
         int count = mQueueCount.load(std::memory_order_relaxed);
         for (int i = 0; i < count; i++)
         {
            if (mQueues[i].getParameterId() == id)
            {
               index = i;
               return &mQueues[i];
            }
         }
         if (count >= kMaxQueues)
         {
            index = -1;
            return nullptr;
         }
         index = count;
         mQueues[index].init(id);
         mQueueCount.store(count + 1, std::memory_order_relaxed);
         return &mQueues[index];
      }

      void clear() { mQueueCount.store(0, std::memory_order_relaxed); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::atomic<int> mQueueCount { 0 };
      HostParamValueQueue mQueues[kMaxQueues];
   };

   // ------------------------------------------------------------------------
   // Host Component Handler - identical to PluginVST3.mm/PluginVST3Win.cpp.
   // ------------------------------------------------------------------------

   class HostComponentHandler : public Steinberg::Vst::IComponentHandler,
                                public Steinberg::Vst::IComponentHandler2,
                                public Steinberg::Vst::IComponentHandlerBusActivation
   {
   public:
      explicit HostComponentHandler(Platform::PluginHandle* handle) : mHandle(handle) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
         }
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler2::iid))
         {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
            return Steinberg::kResultOk;
         }
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandlerBusActivation::iid))
         {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandlerBusActivation*>(this);
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override
      {
         RecordTouch(id);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                                Steinberg::Vst::ParamValue valueNormalized) override
      {
         (void)valueNormalized;
         RecordTouch(id);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override
      {
         (void)id;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
      {
         (void)flags;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override
      {
         (void)state;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString name) override
      {
         (void)name;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
      Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }

      Steinberg::tresult PLUGIN_API requestBusActivation(Steinberg::Vst::MediaType type,
                                                         Steinberg::Vst::BusDirection dir,
                                                         Steinberg::int32 index,
                                                         Steinberg::TBool state) override
      {
         (void)type;
         (void)dir;
         (void)index;
         (void)state;
         return Steinberg::kResultOk;
      }

      void detach() { mHandle = nullptr; }

   private:
      void RecordTouch(Steinberg::Vst::ParamID id);

      std::atomic<uint32_t> mRefCount { 1 };
      Platform::PluginHandle* mHandle = nullptr;
   };

   // ------------------------------------------------------------------------
   // Host-side connection proxy - identical to PluginVST3Win.cpp's. notify()
   // is called synchronously on whichever thread sent it, same documented
   // simplification as Windows (a real main-thread queue is an editor-phase
   // concern, and there is no editor yet on Linux either).
   // ------------------------------------------------------------------------
   class HostConnectionProxy : public Steinberg::Vst::IConnectionPoint
   {
   public:
      explicit HostConnectionProxy(Steinberg::Vst::IConnectionPoint* srcPoint) : mSrc(srcPoint) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IConnectionPoint::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override
      {
         if (other == nullptr)
            return Steinberg::kInvalidArgument;
         if (mDst || !mSrc)
            return Steinberg::kResultFalse;
         mDst = other;
         Steinberg::tresult res = mSrc->connect(this);
         if (res != Steinberg::kResultTrue)
            mDst = nullptr;
         return res;
      }

      Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override
      {
         if (other == nullptr)
            return Steinberg::kInvalidArgument;
         if (other != mDst.get())
            return Steinberg::kInvalidArgument;
         if (mSrc)
            mSrc->disconnect(this);
         mDst = nullptr;
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override
      {
         if (!mDst || message == nullptr)
            return Steinberg::kResultFalse;
         mDst->notify(message);
         return Steinberg::kResultTrue;
      }

      void DisconnectFromSource()
      {
         if (mDst)
            disconnect(mDst.get());
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> mSrc;
      Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> mDst;
   };
}

namespace Platform
{
   namespace
   {
      // Defined far below (near the state save/restore section), forward
      // declared here so HostRunLoop::Pump can route every onFDIsSet/onTimer
      // dispatch through the same crash guard used for state calls and
      // (later) editor calls - "every plugin-editor-facing call must go
      // through RunPluginCallGuarded", no second crash-guard mechanism.
      // Reachable as the qualified name Platform::RunPluginCallGuarded from
      // the anonymous namespace below regardless of this forward
      // declaration's own anonymous-namespace nesting.
      bool RunPluginCallGuarded(const char* what, PluginHandle* h, const std::function<void()>& fn);
   }
}

// GLFW/X11-native headers pull in Xlib's macro-heavy legacy API
// (#define None/Success/Bool/True/False/Status/... as bare preprocessor
// tokens) - included this late in the file, after everything above that
// could collide with those names (ProbeOutcome::Success, in the scanning
// section near the top, is the one that actually would), rather than up
// with the rest of the includes. See the file header comment for why this
// whole region is guarded by INFINITE_VST3_SCANNER.
#if !defined(INFINITE_VST3_SCANNER)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xutil.h>
#include <poll.h>

namespace
{
   // ------------------------------------------------------------------------
   // Editor windows (task 4.3) - X11 embedding of a plugin's IPlugView, plus
   // the one process-wide Steinberg::Linux::IRunLoop the VST3 Linux spec
   // requires (docs/plans/linux/phase-04-vst3.md 4.3; see also
   // https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Provide+A+Runloop+On+Linux/Index.html).
   // Exposed through both paths the spec calls for: HostPlugFrame's
   // queryInterface (for editors, via IPlugView::setFrame) below, and
   // HostApplication's queryInterface (the host-context object passed to
   // IPluginFactory3::setHostContext at instantiation - see PluginVST3Create
   // above) so a plugin can register fd handlers/timers with no editor open
   // at all, which JUCE and DPF both do. All registration and dispatch
   // happens on the main thread only, from PluginVST3PumpEditorEvents further
   // down - never a background poll() thread.
   // ------------------------------------------------------------------------

   // Process-wide X11 display, opened once, ALWAYS on our own dedicated
   // connection (XOpenDisplay(nullptr), which uses $DISPLAY - reaching
   // XWayland transparently when GLFW itself is on native Wayland).
   //
   // Deliberately NOT glfwGetX11Display() even when GLFW is on X11: that
   // would share GLFW's own connection, and GLFW's frame-loop pump
   // (_glfwPollEventsX11 in x11_window.c) does `while (XPending) {
   // XNextEvent(...); processEvent(...); }` every frame, unconditionally
   // draining the ENTIRE connection's event queue - including events for
   // windows it doesn't own. Its per-event window lookup (XFindContext)
   // fails for our editor window (GLFW never registered it), and on that
   // failure processEvent() just returns, silently discarding the event.
   // Since glfwPollEvents() runs once at the top of the frame loop, before
   // PluginVST3PumpEditorEvents() runs later in the same iteration, sharing
   // the connection would mean GLFW eats every one of our editor window's
   // events (WM_DELETE_WINDOW, ConfigureNotify) before we ever see them -
   // verified by reading the vendored GLFW source, not assumed. A second,
   // independent connection sidesteps this entirely: X11 windows are
   // server-side objects with connection-agnostic ids, so creating ours on
   // one connection and having the plugin's IPlugView::attached() embed
   // into it via that same numeric xid works regardless of which
   // connection selected input on it.
   Display* SharedX11Display(std::string* outError)
   {
      static Display* sDisplay = XOpenDisplay(nullptr);
      if (sDisplay == nullptr && outError != nullptr)
         *outError = "plugin editors need X11/XWayland";
      return sDisplay;
   }

   Atom WmDeleteWindowAtom(Display* display)
   {
      static Atom atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
      return atom;
   }

   // ---- IRunLoop ------------------------------------------------------
   // Single process-wide instance, shared by every hosted plugin - the spec
   // is explicit that this must be one run loop, not one per plugin/editor.
   // registerEventHandler/registerTimer can be called from a plugin's own
   // callback (re-arming a timer from onTimer is common), so Pump() below
   // snapshots before dispatching rather than iterating the live maps.
   class HostRunLoop : public Steinberg::Linux::IRunLoop
   {
   public:
      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override { return --mRefCount; }

      Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler,
                                                         Steinberg::Linux::FileDescriptor fd) override
      {
         if (handler == nullptr)
            return Steinberg::kInvalidArgument;
         std::lock_guard<std::mutex> lock(mMutex);
         mEventHandlers[fd] = handler;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) override
      {
         if (handler == nullptr)
            return Steinberg::kInvalidArgument;
         std::lock_guard<std::mutex> lock(mMutex);
         for (auto it = mEventHandlers.begin(); it != mEventHandlers.end();)
         {
            if (it->second == handler)
               it = mEventHandlers.erase(it);
            else
               ++it;
         }
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                                  Steinberg::Linux::TimerInterval milliseconds) override
      {
         if (handler == nullptr)
            return Steinberg::kInvalidArgument;
         std::lock_guard<std::mutex> lock(mMutex);
         TimerEntry& entry = mTimers[handler];
         entry.intervalMs = milliseconds;
         entry.nextFire = std::chrono::steady_clock::now() + std::chrono::milliseconds((long long)milliseconds);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override
      {
         if (handler == nullptr)
            return Steinberg::kInvalidArgument;
         std::lock_guard<std::mutex> lock(mMutex);
         mTimers.erase(handler);
         return Steinberg::kResultOk;
      }

      // Main-thread only, called every frame from PluginVST3PumpEditorEvents
      // regardless of whether any editor is open (factory-context timers
      // have no editor). Returns true if something fired.
      bool Pump()
      {
         bool any = false;

         std::vector<std::pair<int, Steinberg::IPtr<Steinberg::Linux::IEventHandler>>> handlersSnapshot;
         {
            std::lock_guard<std::mutex> lock(mMutex);
            handlersSnapshot.reserve(mEventHandlers.size());
            for (auto& kv : mEventHandlers)
               handlersSnapshot.emplace_back(kv.first, kv.second);
         }
         if (!handlersSnapshot.empty())
         {
            std::vector<pollfd> fds;
            fds.reserve(handlersSnapshot.size());
            for (auto& kv : handlersSnapshot)
               fds.push_back(pollfd { kv.first, POLLIN, 0 });
            if (poll(fds.data(), (nfds_t)fds.size(), 0) > 0)
            {
               for (size_t i = 0; i < fds.size(); i++)
               {
                  if (fds[i].revents & (POLLIN | POLLHUP | POLLERR))
                  {
                     any = true;
                     Steinberg::Linux::IEventHandler* eh = handlersSnapshot[i].second;
                     const int fd = fds[i].fd;
                     Platform::RunPluginCallGuarded("onFDIsSet", nullptr, [&] { eh->onFDIsSet(fd); });
                  }
               }
            }
         }

         std::vector<Steinberg::IPtr<Steinberg::Linux::ITimerHandler>> due;
         const auto now = std::chrono::steady_clock::now();
         {
            std::lock_guard<std::mutex> lock(mMutex);
            for (auto& kv : mTimers)
            {
               if (kv.second.nextFire <= now)
               {
                  due.push_back(kv.first);
                  kv.second.nextFire = now + std::chrono::milliseconds((long long)kv.second.intervalMs);
               }
            }
         }
         for (auto& t : due)
         {
            any = true;
            Steinberg::Linux::ITimerHandler* th = t;
            Platform::RunPluginCallGuarded("onTimer", nullptr, [&] { th->onTimer(); });
         }

         return any;
      }

   private:
      struct TimerEntry
      {
         Steinberg::Linux::TimerInterval intervalMs = 0;
         std::chrono::steady_clock::time_point nextFire;
      };

      std::atomic<uint32_t> mRefCount { 1 };
      std::mutex mMutex;
      std::map<Steinberg::Linux::FileDescriptor, Steinberg::IPtr<Steinberg::Linux::IEventHandler>> mEventHandlers;
      std::map<Steinberg::Linux::ITimerHandler*, TimerEntry> mTimers;
   };

   // The concrete singleton. Kept as its own accessor (rather than folded
   // into SharedRunLoop() below) because PluginVST3PumpEditorEvents, far
   // below, needs the concrete Pump() method - which isn't part of the
   // abstract Steinberg::Linux::IRunLoop interface SharedRunLoop() hands out
   // to plugins/HostApplication.
   HostRunLoop* HostRunLoopInstance()
   {
      static HostRunLoop* instance = new HostRunLoop();
      return instance;
   }

   Steinberg::Linux::IRunLoop* SharedRunLoop()
   {
      return HostRunLoopInstance();
   }

   // Main-thread only, called every frame from PluginVST3PumpEditorEvents -
   // fires due fd/timer callbacks whether or not any editor is currently
   // open (factory-context timers have none).
   bool PumpHostRunLoop()
   {
      return HostRunLoopInstance()->Pump();
   }

   // Host-side IPlugFrame - lets a resizable plugin GUI ask us to resize its
   // window, and (per the Linux run-loop spec) hands out the shared
   // IRunLoop to any editor that queries for it.
   class HostPlugFrame : public Steinberg::IPlugFrame
   {
   public:
      explicit HostPlugFrame(Platform::PluginHandle* handle) : mHandle(handle) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid))
            return SharedRunLoop()->queryInterface(iid, obj);
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

      void detach() { mHandle = nullptr; }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      Platform::PluginHandle* mHandle = nullptr;
   };
}
#endif // !INFINITE_VST3_SCANNER

namespace Platform
{
   // ------------------------------------------------------------------------
   // Internal VST3 state - Linux equivalent of PluginVST3Win.cpp's
   // PluginVST3State. Same shape minus the editor-window fields entirely (no
   // HWND/NSWindow analog exists yet - editors are task 4.3, X11) and minus
   // any DPI/content-scale bookkeeping that only exists to serve an editor.
   // ------------------------------------------------------------------------
   struct PluginVST3State
   {
      void* module = nullptr; // dlopen() handle
      Steinberg::IPtr<Steinberg::IPluginFactory> factory;
      Steinberg::IPtr<Steinberg::Vst::IComponent> component;
      Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
      Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
      Steinberg::IPtr<HostComponentHandler> componentHandler;

      Steinberg::IPtr<HostConnectionProxy> compToCtrlProxy;
      Steinberg::IPtr<HostConnectionProxy> ctrlToCompProxy;

      // PluginVST3Create resolves and instantiates synchronously before
      // returning (see PluginVST3Create below for why), so this is always
      // true by the time the handle is returned - kept anyway so
      // PluginVST3Poll's logic below matches PluginVST3.mm's/
      // PluginVST3Win.cpp's unchanged.
      std::mutex arrivalMutex;
      std::atomic<bool> arrived { false };
      std::string arrivedError;

      double sampleRate = 0.0;
      int maxBlockFrames = 0;
      int pluginInChannels = 0;
      int pluginOutChannels = 0;
      bool active = false;
      bool processing = false;

      Steinberg::Vst::ProcessData processData;
      // One AudioBusBuffers per bus the plugin declares - the spec requires
      // ProcessData's numInputs/numOutputs to equal the bus counts. Bus 0 is
      // the main bus wired to the host; aux buses (Kontakt's extra outs, a
      // compressor's sidechain) read silence and write into auxOutScratch.
      // The first pluginIn/OutChannels entries of the pointer storage are the
      // main bus's channels, rewritten every render.
      std::vector<Steinberg::Vst::AudioBusBuffers> inBuses;
      std::vector<Steinberg::Vst::AudioBusBuffers> outBuses;
      std::vector<float*> inChannelPtrs;
      std::vector<float*> outChannelPtrs;
      std::vector<float> outScratch;
      std::vector<float> zeroScratch;
      std::vector<float> auxOutScratch;

      HostEventList inputEvents;
      HostEventList outputEvents;
      HostParameterChanges inputParamChanges;
      HostParameterChanges outputParamChanges;
      Steinberg::Vst::ProcessContext processContext;
      Steinberg::int64 sampleTime = 0;

      std::atomic<bool> learning { false };
      std::atomic<unsigned long long> learnedAddress { 0 };
      std::atomic<bool> learnedValid { false };

      std::atomic<bool> inRender { false };

      // Set once a crash guard catches this instance faulting inside
      // getState/setState. See RunPluginCallGuarded below.
      std::atomic<bool> stateCallsUnstable { false };

#if !defined(INFINITE_VST3_SCANNER)
      // Editor state (task 4.3). Guarded the same as the rest of the X11
      // plumbing: HostPlugFrame only exists under this guard, and so does
      // the ::Window typedef (from X11/Xlib.h, only included here) - the
      // scanner build never opens an editor, so it carries none of this.
      Steinberg::IPtr<Steinberg::IPlugView> plugView;
      Steinberg::IPtr<HostPlugFrame> plugFrame;
      ::Window editorWindow = 0;
      std::atomic<bool> editorOpen { false };
      bool canResize = false;
      bool resizingFromPlugin = false;
      std::atomic<bool> editorUnstable { false };
#endif
   };

   // Defined further down, alongside the rest of the crash-guard machinery;
   // forward-declared here so PluginVST3Destroy below can use it ahead of
   // that point.
   namespace
   {
      bool RunPluginCallGuarded(const char* what, PluginHandle* h, const std::function<void()>& fn);
   }
}

namespace
{
   void HostComponentHandler::RecordTouch(Steinberg::Vst::ParamID id)
   {
      if (mHandle == nullptr || mHandle->vst3 == nullptr)
         return;
      if (!mHandle->vst3->learning.load(std::memory_order_relaxed))
         return;
      mHandle->vst3->learnedAddress.store((unsigned long long)id, std::memory_order_relaxed);
      mHandle->vst3->learnedValid.store(true, std::memory_order_release);
   }

#if !defined(INFINITE_VST3_SCANNER)
   // ------------------------------------------------------------------------
   // Editor window plumbing (task 4.3). Windows equivalent: the block right
   // above PluginVST3OpenEditor in PluginVST3Win.cpp (gWinOpenPluginEditorCount,
   // ApplyEditorContentScale, HostPlugFrame::resizeView). Defined here,
   // after PluginVST3State's full definition (above), rather than inside the
   // GLFW/X11 anonymous namespace next to HostPlugFrame's class body,
   // because both need PluginVST3State to be complete.
   // ------------------------------------------------------------------------

   // Process-wide count of open plugin editor windows, kept in lockstep with
   // every PluginVST3State's editorOpen flag (see SetPluginEditorOpenLinux
   // below) so PluginVST3AnyEditorOpen() is a cheap atomic read rather than a
   // walk over every handle.
   std::atomic<int> gLinuxOpenPluginEditorCount { 0 };

   void SetPluginEditorOpenLinux(Platform::PluginHandle* h, bool open)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      bool was = h->vst3->editorOpen.exchange(open, std::memory_order_acq_rel);
      if (was == open)
         return;
      if (open)
         gLinuxOpenPluginEditorCount.fetch_add(1, std::memory_order_relaxed);
      else
         gLinuxOpenPluginEditorCount.fetch_sub(1, std::memory_order_relaxed);
   }

   // Process-wide registry of every open editor's X11 window id, so
   // PluginVST3PumpEditorEvents (which drains XPending()/XNextEvent() for
   // the shared display, not per-window) can map an incoming XEvent's
   // .xany.window back to the PluginHandle it belongs to. Guarded by the
   // same mutex as the HostRunLoop's own maps would be, but kept separate
   // since it's a different lifetime concern (window existence, not fd/timer
   // registration).
   std::mutex gEditorWindowsMutex;
   std::map<::Window, Platform::PluginHandle*> gEditorWindows;

   void RegisterEditorWindow(::Window xid, Platform::PluginHandle* h)
   {
      std::lock_guard<std::mutex> lock(gEditorWindowsMutex);
      gEditorWindows[xid] = h;
   }

   void UnregisterEditorWindow(::Window xid)
   {
      std::lock_guard<std::mutex> lock(gEditorWindowsMutex);
      gEditorWindows.erase(xid);
   }

   // GLFW monitor content scale stands in for the per-monitor DPI query
   // Windows uses (EditorDpiForWindow) - Linux/X11 has no equivalent "which
   // monitor is this window mostly on, and what's its scale" primitive
   // exposed to us directly, and the app's own GLFWwindow* handle isn't
   // reachable from this file, so the primary monitor's content scale is
   // the best available signal (matches what a single-monitor or
   // uniformly-scaled multi-monitor desktop reports; a per-monitor-scaled
   // desktop would need real querying of the editor window's actual
   // monitor, which XRandR can do but isn't wired up this phase).
   void ApplyEditorContentScale(Platform::PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->plugView)
         return;
      Platform::PluginVST3State* v = h->vst3;

      float scaleX = 1.0f, scaleY = 1.0f;
      if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
         glfwGetMonitorContentScale(monitor, &scaleX, &scaleY);
      const float scale = scaleX > 0.0f ? scaleX : 1.0f;

      Platform::RunPluginCallGuarded("setContentScaleFactor", h, [&] {
         Steinberg::IPlugViewContentScaleSupport* scaleSupport = nullptr;
         if (v->plugView->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid,
                                         (void**)&scaleSupport) == Steinberg::kResultOk &&
             scaleSupport != nullptr)
         {
            scaleSupport->setContentScaleFactor(scale);
            scaleSupport->release();
         }
      });
   }

   // Out-of-line: HostPlugFrame is defined in the GLFW/X11 anonymous
   // namespace above, before PluginVST3State existed; this needs the full
   // PluginVST3State (for editorWindow/plugView), so it's defined down here
   // instead, same split Windows uses.
   Steinberg::tresult PLUGIN_API HostPlugFrame::resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize)
   {
      if (mHandle == nullptr || mHandle->vst3 == nullptr || newSize == nullptr)
         return Steinberg::kResultFalse;
      Platform::PluginVST3State* v = mHandle->vst3;
      // Editor may already be gone (window closed, or this call landed
      // after teardown) - the plugin's timer can fire against
      // half-torn-down state.
      if (v->editorWindow == 0 || v->plugView != view)
         return Steinberg::kResultFalse;

      const int width = newSize->right - newSize->left;
      const int height = newSize->bottom - newSize->top;
      if (width <= 0 || height <= 0)
         return Steinberg::kResultFalse;

      Display* display = SharedX11Display(nullptr);
      if (display == nullptr)
         return Steinberg::kResultFalse;

      v->resizingFromPlugin = true;
      XResizeWindow(display, v->editorWindow, (unsigned int)width, (unsigned int)height);
      XFlush(display);

      // Per IPlugFrame::resizeView's own doc comment: the host must call
      // IPlugView::onSize() after handling the resize.
      const bool ok = Platform::RunPluginCallGuarded("onSize", mHandle, [&] { view->onSize(newSize); });
      v->resizingFromPlugin = false;
      if (!ok)
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         return Steinberg::kResultFalse;
      }
      return Steinberg::kResultTrue;
   }

   // Full teardown: removed() while the window is still alive (same
   // ordering PluginVST3CloseEditor/PluginVST3Destroy use on Windows/macOS -
   // a plugin's GUI timer can be armed the instant attached() returns, so
   // detaching before destroying the window avoids a callback landing on a
   // window that no longer exists), then unregister and destroy the X11
   // window itself. Safe to call on a handle with no editor open.
   void CloseEditorAndDestroyWindow(Platform::PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      Platform::PluginVST3State* v = h->vst3;

      if (v->plugView)
      {
         Platform::RunPluginCallGuarded("removed", h, [&] { v->plugView->removed(); });
         v->plugView = nullptr;
      }
      if (v->plugFrame)
      {
         v->plugFrame->detach();
         v->plugFrame = nullptr;
      }

      SetPluginEditorOpenLinux(h, false);

      if (v->editorWindow != 0)
      {
         UnregisterEditorWindow(v->editorWindow);
         if (Display* display = SharedX11Display(nullptr))
         {
            XDestroyWindow(display, v->editorWindow);
            XFlush(display);
         }
         v->editorWindow = 0;
      }
   }
#endif // !INFINITE_VST3_SCANNER

   using GetPluginFactoryProc = Steinberg::IPluginFactory* (*)();
   // Matches VST3's module_linux.cpp: bool PLUGIN_API ModuleEntry(void*),
   // bool PLUGIN_API ModuleExit().
   using ModuleEntryProc = bool (*)(void*);
   using ModuleExitProc = bool (*)();

   // Mechanical swap for CFBundleLoadExecutable/LoadLibraryExW: a VST3
   // module on Linux is a shared object, either bare (older single-file
   // .vst3) or inside the standard bundle-folder layout
   // "<name>.vst3/Contents/<arch>-linux/<name>.so". ModuleEntry/ModuleExit
   // are the exact Linux-side equivalents of bundleEntry/bundleExit -
   // optional per the SDK, but not skipped for the same reason it isn't
   // optional on the other two platforms: plugins that load resources
   // relative to their own module fail at instantiation without it.
   void UnloadVST3Module(void* module)
   {
      if (module == nullptr)
         return;
      if (ModuleExitProc moduleExit = (ModuleExitProc)dlsym(module, "ModuleExit"))
         moduleExit();
      dlclose(module);
   }

   // Arch-specific bundle subfolder name, matching the VST3 SDK's
   // moduleinfo/bundle layout convention (module_linux.cpp derives this at
   // runtime from uname(); selecting it at compile time from the build
   // target is equivalent and matches this codebase's existing
   // __aarch64__/__x86_64__ precedent, e.g. src/audio/AudioEngine.cpp).
#if defined(__aarch64__)
   constexpr const char* kVst3ArchFolder = "aarch64-linux";
#else
   constexpr const char* kVst3ArchFolder = "x86_64-linux";
#endif

   // Bare-.so fallback when the bundle doesn't use the conventional
   // "<stem>.so" module name under Contents/<arch>-linux/: scan that folder
   // for any .so file. Some plugins name the module differently from the
   // bundle folder.
   std::string FindVst3ModuleInArchFolder(const std::string& archFolderPath)
   {
      std::error_code ec;
      if (!fs::is_directory(archFolderPath, ec))
         return {};
      for (const auto& entry : fs::directory_iterator(archFolderPath, ec))
      {
         if (ec)
            break;
         if (entry.is_regular_file(ec) && entry.path().extension() == ".so")
            return entry.path().string();
      }
      return {};
   }

   // Fallback when the arch folder for this build target isn't present at
   // all: scan every Contents/*-linux/ folder for a .so module.
   std::string FindVst3ModuleInAnyArchFolder(const std::string& bundlePath)
   {
      std::error_code ec;
      const std::string contentsPath = bundlePath + "/Contents";
      if (!fs::is_directory(contentsPath, ec))
         return {};
      for (const auto& archEntry : fs::directory_iterator(contentsPath, ec))
      {
         if (ec)
            break;
         if (!archEntry.is_directory(ec))
            continue;
         const std::string archName = archEntry.path().filename().string();
         if (archName.size() < 6 || archName.compare(archName.size() - 6, 6, "-linux") != 0)
            continue;
         std::string found = FindVst3ModuleInArchFolder(archEntry.path().string());
         if (!found.empty())
            return found;
      }
      return {};
   }

   void* LoadVST3Module(const std::string& bundlePath, Steinberg::IPluginFactory** outFactory)
   {
      if (outFactory != nullptr)
         *outFactory = nullptr;
      if (bundlePath.empty())
         return nullptr;

      std::error_code ec;
      std::string soPath = bundlePath;
      if (fs::is_directory(bundlePath, ec))
      {
         const std::string archFolder = bundlePath + "/Contents/" + kVst3ArchFolder;
         std::string conventional = archFolder + "/" + fs::path(bundlePath).stem().string() + ".so";
         if (fs::exists(conventional, ec))
         {
            soPath = conventional;
         }
         else
         {
            std::string found = FindVst3ModuleInArchFolder(archFolder);
            if (found.empty())
               found = FindVst3ModuleInAnyArchFolder(bundlePath);
            soPath = found.empty() ? conventional : found;
         }
      }

      std::error_code absEc;
      const std::string absPath = fs::absolute(soPath, absEc).string();
      void* module = dlopen(absPath.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (module == nullptr)
      {
         VST3Trace("dlopen failed: %s (%s)", soPath.c_str(), dlerror());
         return nullptr;
      }

      if (ModuleEntryProc moduleEntry = (ModuleEntryProc)dlsym(module, "ModuleEntry"))
      {
         if (!moduleEntry(module))
         {
            VST3Trace("ModuleEntry failed: %s", soPath.c_str());
            dlclose(module);
            return nullptr;
         }
      }

      GetPluginFactoryProc getFactory = (GetPluginFactoryProc)dlsym(module, "GetPluginFactory");
      if (getFactory == nullptr)
      {
         VST3Trace("no GetPluginFactory export: %s", soPath.c_str());
         UnloadVST3Module(module);
         return nullptr;
      }

      Steinberg::IPluginFactory* factory = getFactory();
      if (factory == nullptr)
      {
         VST3Trace("GetPluginFactory returned null: %s", soPath.c_str());
         UnloadVST3Module(module);
         return nullptr;
      }

      if (outFactory != nullptr)
         *outFactory = factory;
      return module;
   }
}

namespace Platform
{
   PluginHandle* PluginVST3Create(const PluginDesc& desc, double sampleRate, int maxBlockFrames)
   {
      PluginHandle* h = new PluginHandle();
      h->desc = desc;
      h->state = PluginLoadState::Pending;
      h->sampleRate = sampleRate;
      h->maxBlockFrames = maxBlockFrames > 0 ? maxBlockFrames : 512;

      PluginVST3State* v = new PluginVST3State();
      v->sampleRate = h->sampleRate;
      v->maxBlockFrames = h->maxBlockFrames;
      h->vst3 = v;

      struct TUIDHolder { Steinberg::TUID data; };
      TUIDHolder cidHolder {};
      if (!ParseVST3Identifier(desc.identifier, cidHolder.data))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = "invalid VST3 identifier: " + desc.identifier;
         v->arrived.store(true, std::memory_order_release);
         return h;
      }

      VST3Trace("resolve begin: name='%s' id='%s' path='%s'", desc.name.c_str(), desc.identifier.c_str(),
                desc.path.c_str());

      std::string bundlePath;
      std::string lastKnownPath;
      bool wasIndexed = false;

      if (!desc.path.empty() && fs::exists(desc.path))
      {
         bundlePath = desc.path;
         CacheVST3BundlePath(desc.identifier, bundlePath);
         VST3Trace("  resolved from desc.path");
      }
      else
      {
         if (!desc.path.empty())
         {
            wasIndexed = true;
            lastKnownPath = desc.path;
         }
         bundlePath = GetCachedVST3BundlePath(desc.identifier);
         if (!bundlePath.empty())
         {
            wasIndexed = true;
            lastKnownPath = bundlePath;
         }
         VST3Trace("  desc.path unusable (empty=%d); cache lookup -> '%s'", (int)desc.path.empty(),
                   bundlePath.c_str());
      }

      // Step 3: a targeted rescan, only if both of the above missed. Mirrors
      // PluginVST3.mm's/PluginVST3Win.cpp's equivalent step - the standard
      // Linux VST3 locations (see phase-04-vst3.md) stand in for the two
      // conventional Windows/macOS folders.
      if (bundlePath.empty())
      {
         std::vector<std::string> searchFolders;
         if (const char* home = getenv("HOME"))
            searchFolders.push_back(std::string(home) + "/.vst3");
         searchFolders.push_back("/usr/lib/vst3");
         searchFolders.push_back("/usr/local/lib/vst3");
         for (const auto& extra : GetExtraVST3SearchFolders())
            if (std::find(searchFolders.begin(), searchFolders.end(), extra) == searchFolders.end())
               searchFolders.push_back(extra);
         std::vector<PluginDesc> discovered;
         VST3Trace("  cache miss; targeted rescan of %d folder(s)", (int)searchFolders.size());
         for (const auto& f : searchFolders)
            VST3Trace("    folder: %s", f.c_str());
         EnumerateVST3Plugins(searchFolders, discovered);
         bundlePath = GetCachedVST3BundlePath(desc.identifier);
         VST3Trace("  rescan found %d plugin(s); cache lookup -> '%s'", (int)discovered.size(), bundlePath.c_str());
      }

      if (bundlePath.empty())
      {
         std::string message = "VST3 not resolvable: " + desc.identifier;
         if (wasIndexed)
            message += " (indexed at " + lastKnownPath + ", which no longer exists - rescan plugins)";
         else
            message += " (not found in the plugin index - rescan plugins)";
         {
            std::lock_guard<std::mutex> lock(v->arrivalMutex);
            v->arrivedError = message;
         }
         VST3Trace("  %s", message.c_str());
         v->arrived.store(true, std::memory_order_release);
         return h;
      }

      Steinberg::IPluginFactory* factoryRaw = nullptr;
      void* module = LoadVST3Module(bundlePath, &factoryRaw);
      if (module == nullptr || factoryRaw == nullptr)
      {
         {
            std::lock_guard<std::mutex> lock(v->arrivalMutex);
            v->arrivedError = "failed to load VST3 module";
         }
         v->arrived.store(true, std::memory_order_release);
         return h;
      }

      v->module = module;
      v->factory = factoryRaw;

      {
         Steinberg::IPtr<Steinberg::IPluginFactory3> factory3;
         if (v->factory->queryInterface(Steinberg::IPluginFactory3::iid, (void**)&factory3) ==
                Steinberg::kResultOk &&
             factory3)
            factory3->setHostContext((Steinberg::FUnknown*)SharedHostApplication());
      }

      Steinberg::Vst::IComponent* compRaw = nullptr;
      if (v->factory->createInstance(cidHolder.data, Steinberg::Vst::IComponent::iid, (void**)&compRaw) != Steinberg::kResultOk ||
          compRaw == nullptr)
      {
         {
            std::lock_guard<std::mutex> lock(v->arrivalMutex);
            v->arrivedError = "failed to create VST3 component instance";
         }
         v->arrived.store(true, std::memory_order_release);
         return h;
      }
      v->component = Steinberg::owned(compRaw);

      Steinberg::Vst::IEditController* ctrlRaw = nullptr;
      if (compRaw->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctrlRaw) == Steinberg::kResultOk &&
          ctrlRaw != nullptr)
      {
         v->controller = Steinberg::owned(ctrlRaw);
      }
      else
      {
         Steinberg::TUID controllerCID = {};
         if (compRaw->getControllerClassId(controllerCID) == Steinberg::kResultTrue)
         {
            if (v->factory->createInstance(controllerCID, Steinberg::Vst::IEditController::iid, (void**)&ctrlRaw) == Steinberg::kResultOk &&
                ctrlRaw != nullptr)
               v->controller = Steinberg::owned(ctrlRaw);
         }
      }

      Steinberg::Vst::IAudioProcessor* procRaw = nullptr;
      if (compRaw->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&procRaw) == Steinberg::kResultOk &&
          procRaw != nullptr)
      {
         v->processor = Steinberg::owned(procRaw);
      }

      v->arrived.store(true, std::memory_order_release);
      return h;
   }

   namespace
   {
      bool PluginVST3Configure(PluginHandle* h, std::string& outError)
      {
         PluginVST3State* v = h->vst3;
         if (v == nullptr || !v->component || !v->processor)
         {
            outError = "missing VST3 component or processor";
            return false;
         }

         if (v->processing)
         {
            v->processor->setProcessing(false);
            v->processing = false;
         }
         if (v->active)
         {
            v->component->setActive(false);
            v->active = false;
         }

         const double rate = h->sampleRate > 0.0 ? h->sampleRate : 48000.0;
         const int frames = std::min(std::max(h->maxBlockFrames, 1), 4096);

         // setBusArrangements and ProcessData must both cover EVERY bus the
         // plugin declares, not just the main one. Instruments with aux outs
         // (Kontakt) and effects with a sidechain input (most compressors)
         // correctly reject a one-bus call; the old code then "fell back" to
         // mono even though that call was rejected too, so those plugins were
         // rendered from their left channel only. Now: ask for stereo on the
         // main buses with every aux bus left as the plugin has it, and read
         // back whatever the plugin actually settled on.
         constexpr int kMaxBuses = 64;
         const int inBusCount =
            std::min(std::max((int)v->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput), 0), kMaxBuses);
         const int outBusCount =
            std::min(std::max((int)v->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput), 0), kMaxBuses);

         auto currentArrangement = [&](Steinberg::Vst::BusDirection dir, int index) {
            Steinberg::Vst::SpeakerArrangement arr = 0;
            if (v->processor->getBusArrangement(dir, index, arr) == Steinberg::kResultOk)
               return arr;
            Steinberg::Vst::BusInfo info = {};
            if (v->component->getBusInfo(Steinberg::Vst::kAudio, dir, index, info) == Steinberg::kResultOk &&
                info.channelCount == 1)
               return (Steinberg::Vst::SpeakerArrangement)Steinberg::Vst::SpeakerArr::kMono;
            return (Steinberg::Vst::SpeakerArrangement)Steinberg::Vst::SpeakerArr::kStereo;
         };
         std::vector<Steinberg::Vst::SpeakerArrangement> inArrs((size_t)inBusCount);
         std::vector<Steinberg::Vst::SpeakerArrangement> outArrs((size_t)outBusCount);
         for (int i = 0; i < inBusCount; i++)
            inArrs[(size_t)i] = currentArrangement(Steinberg::Vst::kInput, i);
         for (int i = 0; i < outBusCount; i++)
            outArrs[(size_t)i] = currentArrangement(Steinberg::Vst::kOutput, i);

         auto tryMainArrangement = [&](Steinberg::Vst::SpeakerArrangement mainIn,
                                       Steinberg::Vst::SpeakerArrangement mainOut) {
            std::vector<Steinberg::Vst::SpeakerArrangement> ins = inArrs;
            std::vector<Steinberg::Vst::SpeakerArrangement> outs = outArrs;
            if (!ins.empty())
               ins[0] = mainIn;
            if (!outs.empty())
               outs[0] = mainOut;
            return v->processor->setBusArrangements(ins.empty() ? nullptr : ins.data(), inBusCount,
                                                    outs.empty() ? nullptr : outs.data(), outBusCount) ==
                   Steinberg::kResultOk;
         };
         if (inBusCount + outBusCount > 0 &&
             !tryMainArrangement(Steinberg::Vst::SpeakerArr::kStereo, Steinberg::Vst::SpeakerArr::kStereo) &&
             !tryMainArrangement(Steinberg::Vst::SpeakerArr::kMono, Steinberg::Vst::SpeakerArr::kStereo))
            tryMainArrangement(Steinberg::Vst::SpeakerArr::kMono, Steinberg::Vst::SpeakerArr::kMono);

         for (int i = 0; i < inBusCount; i++)
            inArrs[(size_t)i] = currentArrangement(Steinberg::Vst::kInput, i);
         for (int i = 0; i < outBusCount; i++)
            outArrs[(size_t)i] = currentArrangement(Steinberg::Vst::kOutput, i);

         const int inChannels = inBusCount > 0 ? Steinberg::Vst::SpeakerArr::getChannelCount(inArrs[0]) : 0;
         const int outChannels = outBusCount > 0 ? Steinberg::Vst::SpeakerArr::getChannelCount(outArrs[0]) : 0;
         v->pluginInChannels = inChannels;
         v->pluginOutChannels = outChannels;

         // Every bus starts deactivated per the VST3 spec (IComponent::
         // activateBus). Strict SDK-based plugins (FabFilter, some Arturia)
         // honor that exactly and produce silence on an inactive bus, and
         // ignore notes on an inactive event bus - lenient ones (Kontakt,
         // most JUCE plugins) process regardless, which is how this hid.
         // Aux buses stay inactive; they still get buffers below.
         if (inBusCount > 0)
            v->component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
         if (outBusCount > 0)
            v->component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
         if (v->component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput) > 0)
            v->component->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, 0, true);

         Steinberg::Vst::ProcessSetup setup = {};
         setup.processMode = Steinberg::Vst::kRealtime;
         setup.symbolicSampleSize = Steinberg::Vst::kSample32;
         setup.maxSamplesPerBlock = frames;
         setup.sampleRate = rate;

         if (v->processor->setupProcessing(setup) != Steinberg::kResultOk)
         {
            outError = "VST3 setupProcessing failed";
            return false;
         }

         if (v->component->setActive(true) != Steinberg::kResultOk)
         {
            outError = "VST3 setActive failed";
            return false;
         }
         v->active = true;

         h->latencySamples = (int)v->processor->getLatencySamples();

         const Steinberg::tresult procRes = v->processor->setProcessing(true);
         if (procRes != Steinberg::kResultOk)
            VST3Trace("setProcessing(true) returned %d - continuing (optional call)", (int)procRes);
         v->processing = true;

         v->outScratch.assign((size_t)std::max(outChannels, 8) * (size_t)frames, 0.0f);
         v->zeroScratch.assign((size_t)frames, 0.0f);
         v->auxOutScratch.assign((size_t)frames, 0.0f);

         std::memset(&v->processData, 0, sizeof(v->processData));
         v->processData.processMode = Steinberg::Vst::kRealtime;
         v->processData.symbolicSampleSize = Steinberg::Vst::kSample32;

         size_t totalIn = 0;
         size_t totalOut = 0;
         for (const auto a : inArrs)
            totalIn += (size_t)Steinberg::Vst::SpeakerArr::getChannelCount(a);
         for (const auto a : outArrs)
            totalOut += (size_t)Steinberg::Vst::SpeakerArr::getChannelCount(a);
         v->inChannelPtrs.assign(totalIn, v->zeroScratch.data());
         v->outChannelPtrs.assign(totalOut, v->auxOutScratch.data());
         v->inBuses.assign((size_t)inBusCount, Steinberg::Vst::AudioBusBuffers {});
         v->outBuses.assign((size_t)outBusCount, Steinberg::Vst::AudioBusBuffers {});

         size_t offset = 0;
         for (int i = 0; i < inBusCount; i++)
         {
            const int ch = Steinberg::Vst::SpeakerArr::getChannelCount(inArrs[(size_t)i]);
            v->inBuses[(size_t)i].numChannels = ch;
            v->inBuses[(size_t)i].channelBuffers32 = v->inChannelPtrs.data() + offset;
            // Aux inputs (sidechain) are always fed silence - say so.
            v->inBuses[(size_t)i].silenceFlags = (i == 0 || ch <= 0) ? 0 : (ch >= 64 ? ~0ull : ((1ull << ch) - 1));
            offset += (size_t)std::max(ch, 0);
         }
         offset = 0;
         for (int i = 0; i < outBusCount; i++)
         {
            const int ch = Steinberg::Vst::SpeakerArr::getChannelCount(outArrs[(size_t)i]);
            v->outBuses[(size_t)i].numChannels = ch;
            v->outBuses[(size_t)i].channelBuffers32 = v->outChannelPtrs.data() + offset;
            offset += (size_t)std::max(ch, 0);
         }

         v->processData.numInputs = inBusCount;
         v->processData.inputs = inBusCount > 0 ? v->inBuses.data() : nullptr;
         v->processData.numOutputs = outBusCount;
         v->processData.outputs = outBusCount > 0 ? v->outBuses.data() : nullptr;

         v->processData.inputEvents = &v->inputEvents;
         v->processData.outputEvents = &v->outputEvents;
         v->processData.inputParameterChanges = &v->inputParamChanges;
         v->processData.outputParameterChanges = &v->outputParamChanges;

         std::memset(&v->processContext, 0, sizeof(v->processContext));
         v->processContext.sampleRate = rate;
         v->processContext.projectTimeSamples = 0;
         v->processContext.projectTimeMusic = 0.0;
         v->processContext.tempo = 120.0;
         v->processContext.timeSigNumerator = 4;
         v->processContext.timeSigDenominator = 4;
         v->processContext.state = Steinberg::Vst::ProcessContext::kPlaying |
                                   Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                                   Steinberg::Vst::ProcessContext::kTempoValid |
                                   Steinberg::Vst::ProcessContext::kTimeSigValid;
         v->processData.processContext = &v->processContext;

         return true;
      }
   }

   PluginLoadState PluginVST3Poll(PluginHandle* h, std::string& outError)
   {
      if (h == nullptr || h->vst3 == nullptr)
      {
         outError = "null VST3 plugin handle";
         return PluginLoadState::Failed;
      }
      PluginVST3State* v = h->vst3;
      if (h->state != PluginLoadState::Pending)
      {
         outError = h->loadError;
         return h->state;
      }
      if (!v->arrived.load(std::memory_order_acquire))
         return PluginLoadState::Pending;

      std::string arrivedErr;
      {
         std::lock_guard<std::mutex> lock(v->arrivalMutex);
         arrivedErr = v->arrivedError;
      }
      if (!arrivedErr.empty() || !v->component || !v->processor)
      {
         h->state = PluginLoadState::Failed;
         h->loadError = !arrivedErr.empty() ? arrivedErr : "VST3 failed to instantiate";
         outError = h->loadError;
         return h->state;
      }

      Steinberg::Vst::IHostApplication* hostApp = SharedHostApplication();
      if (v->component->initialize(hostApp) != Steinberg::kResultOk)
      {
         h->state = PluginLoadState::Failed;
         h->loadError = "VST3 component initialize failed";
         outError = h->loadError;
         return h->state;
      }

      if (v->controller)
      {
         v->controller->initialize(hostApp);
         v->componentHandler = new HostComponentHandler(h);
         v->controller->setComponentHandler(v->componentHandler);

         Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> compCP;
         Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> ctrlCP;
         if (v->component->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&compCP) == Steinberg::kResultOk &&
             v->controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&ctrlCP) == Steinberg::kResultOk)
         {
            if (compCP && ctrlCP && compCP != ctrlCP)
            {
               v->compToCtrlProxy = Steinberg::owned(new HostConnectionProxy(compCP));
               v->ctrlToCompProxy = Steinberg::owned(new HostConnectionProxy(ctrlCP));
               v->compToCtrlProxy->connect(ctrlCP);
               v->ctrlToCompProxy->connect(compCP);
            }
         }

         MemoryStream stateStream;
         if (v->component->getState(&stateStream) == Steinberg::kResultOk)
         {
            stateStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            v->controller->setComponentState(&stateStream);
         }
      }

      if (!PluginVST3Configure(h, outError))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = outError;
         return h->state;
      }

      h->state = PluginLoadState::Ready;
      outError.clear();
      return h->state;
   }

   bool PluginVST3Prepare(PluginHandle* h, double sampleRate, int maxBlockFrames, std::string& outError)
   {
      if (h == nullptr || h->vst3 == nullptr || h->state != PluginLoadState::Ready)
      {
         outError = "plugin not ready";
         return false;
      }
      const int frames = maxBlockFrames > 0 ? maxBlockFrames : h->maxBlockFrames;
      if (sampleRate <= 0.0)
         return true;
      if (std::abs(sampleRate - h->sampleRate) < 1e-6 && frames == h->maxBlockFrames && h->vst3->active)
         return true;

      h->sampleRate = sampleRate;
      h->maxBlockFrames = frames;
      h->vst3->sampleRate = sampleRate;
      h->vst3->maxBlockFrames = frames;

      if (!PluginVST3Configure(h, outError))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = outError;
         return false;
      }
      return true;
   }

   void PluginVST3Destroy(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;

      PluginVST3State* v = h->vst3;

#if !defined(INFINITE_VST3_SCANNER)
      // Tear down the editor window (if any) before anything else - it may
      // hold callbacks into componentHandler/controller below, and
      // CloseEditorAndDestroyWindow's own removed()-then-destroy ordering
      // depends on those still being valid.
      CloseEditorAndDestroyWindow(h);
#endif

      if (v->compToCtrlProxy)
      {
         v->compToCtrlProxy->DisconnectFromSource();
         v->compToCtrlProxy = nullptr;
      }
      if (v->ctrlToCompProxy)
      {
         v->ctrlToCompProxy->DisconnectFromSource();
         v->ctrlToCompProxy = nullptr;
      }

      if (v->componentHandler)
      {
         v->componentHandler->detach();
         v->componentHandler = nullptr;
      }

      // Wait out any in-flight render.
      for (int spins = 0; spins < 100000 && v->inRender.load(std::memory_order_acquire); spins++)
      {
      }

      if (v->processor && v->processing)
      {
         v->processor->setProcessing(false);
         v->processing = false;
      }
      if (v->component && v->active)
      {
         v->component->setActive(false);
         v->active = false;
      }

      if (v->controller)
      {
         v->controller->setComponentHandler(nullptr);
         v->controller->terminate();
         v->controller = nullptr;
      }
      if (v->component)
      {
         v->component->terminate();
         v->component = nullptr;
      }
      v->processor = nullptr;
      v->factory = nullptr;

      if (v->module != nullptr)
      {
         UnloadVST3Module(v->module);
         v->module = nullptr;
      }

      delete v;
      h->vst3 = nullptr;
      delete h;
   }

   void PluginVST3Render(PluginHandle* h, const float* const* in, int inChannels,
                         float* const* out, int outChannels, int numFrames)
   {
      if (h == nullptr || h->vst3 == nullptr || out == nullptr || numFrames <= 0)
         return;

      PluginVST3State* v = h->vst3;
      if (!v->processor || !v->processing)
         return;

      v->inRender.store(true, std::memory_order_release);

      const int pluginIn = v->pluginInChannels;
      const int pluginOut = v->pluginOutChannels;
      const int frames = numFrames > v->maxBlockFrames ? v->maxBlockFrames : numFrames;

      v->processData.numSamples = frames;
      v->processContext.projectTimeSamples = v->sampleTime;
      const double sr = h->sampleRate > 0.0 ? h->sampleRate : 48000.0;
      v->processContext.projectTimeMusic = (double)v->sampleTime / sr * (120.0 / 60.0);
      v->sampleTime += frames;

      // Aux inputs and any unconnected main input read zeroScratch; a plugin
      // is not supposed to write its inputs, but re-clear it so one that does
      // can't leak its last block into the next.
      std::memset(v->zeroScratch.data(), 0, (size_t)frames * sizeof(float));
      if (pluginIn > 0)
      {
         for (int ch = 0; ch < pluginIn; ch++)
         {
            const float* src = (in != nullptr && inChannels > 0)
                                  ? in[ch < inChannels ? ch : inChannels - 1]
                                  : nullptr;
            v->inChannelPtrs[ch] = const_cast<float*>(src != nullptr ? src : v->zeroScratch.data());
         }
      }

      const bool direct = (pluginOut == outChannels && pluginOut > 0);
      float* outScratchBase = v->outScratch.data();
      for (int ch = 0; ch < pluginOut; ch++)
      {
         v->outChannelPtrs[ch] = direct ? out[ch] : (outScratchBase + (size_t)ch * (size_t)v->maxBlockFrames);
      }

      const Steinberg::tresult res = v->processor->process(v->processData);

      if (res != Steinberg::kResultOk)
      {
         for (int ch = 0; ch < outChannels; ch++)
            if (out[ch] != nullptr)
               std::memset(out[ch], 0, (size_t)frames * sizeof(float));
         v->inputEvents.clear();
         v->inputParamChanges.clear();
         v->inRender.store(false, std::memory_order_release);
         return;
      }

      // Fewer plugin channels than host channels (a mono plugin into a
      // stereo cable) repeats the last plugin channel - it must not then be
      // zeroed again, which is what made mono-negotiated plugins left-only.
      if (!direct)
      {
         for (int ch = 0; ch < outChannels; ch++)
         {
            if (out[ch] == nullptr)
               continue;
            if (pluginOut <= 0)
            {
               std::memset(out[ch], 0, (size_t)frames * sizeof(float));
               continue;
            }
            const int srcCh = ch < pluginOut ? ch : pluginOut - 1;
            std::memcpy(out[ch], outScratchBase + (size_t)srcCh * (size_t)v->maxBlockFrames,
                        (size_t)frames * sizeof(float));
         }
      }

      v->inputEvents.clear();
      v->inputParamChanges.clear();

      v->inRender.store(false, std::memory_order_release);
   }

   void PluginVST3ScheduleMIDIEvent(PluginHandle* h, int frameOffset, const unsigned char* bytes, int byteCount)
   {
      if (h == nullptr || h->vst3 == nullptr || bytes == nullptr || byteCount <= 0)
         return;

      PluginVST3State* v = h->vst3;
      const unsigned char status = bytes[0] & 0xF0;
      const unsigned char channel = bytes[0] & 0x0F;
      const unsigned char note = (byteCount > 1) ? (bytes[1] & 0x7F) : 0;
      const unsigned char vel = (byteCount > 2) ? (bytes[2] & 0x7F) : 0;

      if (status == 0x90 && vel > 0)
      {
         Steinberg::Vst::Event e = {};
         e.type = Steinberg::Vst::Event::kNoteOnEvent;
         e.sampleOffset = frameOffset;
         e.noteOn.channel = channel;
         e.noteOn.pitch = (Steinberg::int16)note;
         e.noteOn.velocity = (float)vel / 127.0f;
         e.noteOn.length = 0;
         e.noteOn.tuning = 0.0f;
         e.noteOn.noteId = -1;
         v->inputEvents.addEvent(e);
      }
      else if (status == 0x80 || (status == 0x90 && vel == 0))
      {
         Steinberg::Vst::Event e = {};
         e.type = Steinberg::Vst::Event::kNoteOffEvent;
         e.sampleOffset = frameOffset;
         e.noteOff.channel = channel;
         e.noteOff.pitch = (Steinberg::int16)note;
         e.noteOff.velocity = (float)vel / 127.0f;
         e.noteOff.tuning = 0.0f;
         e.noteOff.noteId = -1;
         v->inputEvents.addEvent(e);
      }
   }

   int PluginVST3ParameterCount(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return 0;
      return (int)h->vst3->controller->getParameterCount();
   }

   namespace
   {
      void FillVST3ParamInfo(const Steinberg::Vst::ParameterInfo& p, PluginParamInfo& out)
      {
         out.address = (unsigned long long)p.id;
         out.displayName = UTF16ToUTF8(p.title);
         if (out.displayName.empty())
            out.displayName = UTF16ToUTF8(p.shortTitle);
         out.minValue = 0.0f;
         out.maxValue = 1.0f;
         out.defaultValue = (float)p.defaultNormalizedValue;
         out.unit = UTF16ToUTF8(p.units);
      }
   }

   bool PluginVST3ParameterInfo(PluginHandle* h, int index, PluginParamInfo& out)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return false;
      Steinberg::Vst::ParameterInfo info = {};
      if (h->vst3->controller->getParameterInfo(index, info) != Steinberg::kResultOk)
         return false;
      FillVST3ParamInfo(info, out);
      return true;
   }

   bool PluginVST3ParameterInfoByAddress(PluginHandle* h, unsigned long long address, PluginParamInfo& out)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return false;
      const int count = (int)h->vst3->controller->getParameterCount();
      for (int i = 0; i < count; i++)
      {
         Steinberg::Vst::ParameterInfo info = {};
         if (h->vst3->controller->getParameterInfo(i, info) == Steinberg::kResultOk)
         {
            if ((unsigned long long)info.id == address)
            {
               FillVST3ParamInfo(info, out);
               return true;
            }
         }
      }
      return false;
   }

   void PluginVST3SetParameter(PluginHandle* h, unsigned long long address, float value)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return;

      const Steinberg::Vst::ParamID pid = (Steinberg::Vst::ParamID)address;
      const Steinberg::Vst::ParamValue normVal = (Steinberg::Vst::ParamValue)std::clamp(value, 0.0f, 1.0f);
      h->vst3->controller->setParamNormalized(pid, normVal);

      Steinberg::int32 queueIdx = 0;
      Steinberg::Vst::IParamValueQueue* queue = h->vst3->inputParamChanges.addParameterData(pid, queueIdx);
      if (queue != nullptr)
      {
         Steinberg::int32 ptIdx = 0;
         queue->addPoint(0, normVal, ptIdx);
      }
   }

   bool PluginVST3GetParameter(PluginHandle* h, unsigned long long address, float& outValue)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return false;
      outValue = (float)h->vst3->controller->getParamNormalized((Steinberg::Vst::ParamID)address);
      return true;
   }

   void PluginVST3BeginLearn(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      h->vst3->learnedValid.store(false, std::memory_order_relaxed);
      h->vst3->learning.store(true, std::memory_order_release);
   }

   void PluginVST3EndLearn(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      h->vst3->learning.store(false, std::memory_order_release);
      h->vst3->learnedValid.store(false, std::memory_order_relaxed);
   }

   bool PluginVST3PollLearned(PluginHandle* h, unsigned long long& outAddress)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->learnedValid.load(std::memory_order_acquire))
         return false;
      outAddress = h->vst3->learnedAddress.load(std::memory_order_relaxed);
      h->vst3->learnedValid.store(false, std::memory_order_relaxed);
      return true;
   }

   // ------------------------------------------------------------------------
   // Crash guard for narrow, synchronous, main-thread calls into plugin code
   // - POSIX sigsetjmp/siglongjmp guard, ported unchanged from
   // PluginVST3.mm's equivalent section (Windows uses SEH instead; Linux has
   // the same signal model as macOS). Same discipline: narrow, synchronous
   // calls with no in-flight audio riding on them. Deliberately not used
   // around process(). Used for state save/restore below, and (task 4.3)
   // for every editor-facing plugin call further down this file
   // (createView/getSize/canResize/setFrame/attached/removed/onSize/
   // setContentScaleFactor) plus HostRunLoop's onFDIsSet/onTimer dispatch.
   // ------------------------------------------------------------------------
   namespace
   {
      thread_local sigjmp_buf gPluginCallJmpBuf;
      thread_local volatile sig_atomic_t gPluginCallGuardActive = 0;

      void PluginCallCrashHandler(int sig)
      {
         if (gPluginCallGuardActive)
         {
            gPluginCallGuardActive = 0;
            siglongjmp(gPluginCallJmpBuf, 1);
         }
         // Not inside a guarded call right now - a real crash, let it die normally.
         signal(sig, SIG_DFL);
         raise(sig);
      }

      void InstallPluginCallCrashHandlerOnce()
      {
         static std::once_flag once;
         std::call_once(once, []
         {
            struct sigaction sa = {};
            sa.sa_handler = PluginCallCrashHandler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGSEGV, &sa, nullptr);
            sigaction(SIGBUS, &sa, nullptr);
            sigaction(SIGILL, &sa, nullptr);
         });
      }

      bool RunPluginCallGuarded(const char* what, PluginHandle* h, const std::function<void()>& fn)
      {
         InstallPluginCallCrashHandlerOnce();
         gPluginCallGuardActive = 1;
         const bool crashed = (sigsetjmp(gPluginCallJmpBuf, 1) != 0);
         if (!crashed)
            fn();
         gPluginCallGuardActive = 0;
         if (crashed)
         {
            const char* name = (h != nullptr) ? h->desc.name.c_str() : "?";
            std::fprintf(stderr, "[VST3] plugin '%s' crashed inside %s - call aborted\n", name, what);
         }
         return !crashed;
      }
   }

   // ------------------------------------------------------------------------
   // Editor Window (task 4.3, X11). Every plugin-facing call below goes
   // through RunPluginCallGuarded (see above) - no second crash-guard
   // mechanism, per phase-04-vst3.md 4.3.
   // ------------------------------------------------------------------------

#if !defined(INFINITE_VST3_SCANNER)

   bool PluginVST3OpenEditor(PluginHandle* h, std::string& outError)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller || h->state != PluginLoadState::Ready)
      {
         outError = "plugin not loaded";
         return false;
      }
      PluginVST3State* v = h->vst3;

      if (v->editorUnstable.load(std::memory_order_relaxed))
      {
         outError = "plugin's editor crashed previously and is disabled for this session";
         return false;
      }

      if (v->editorWindow != 0)
      {
         Display* display = SharedX11Display(&outError);
         if (display == nullptr)
            return false;
         XMapRaised(display, v->editorWindow);
         XFlush(display);
         SetPluginEditorOpenLinux(h, true);
         return true;
      }

      Display* display = SharedX11Display(&outError);
      if (display == nullptr)
         return false;

      if (!v->plugView)
      {
         Steinberg::IPlugView* view = nullptr;
         if (!RunPluginCallGuarded("createView", h,
                [&] { view = v->controller->createView(Steinberg::Vst::ViewType::kEditor); }))
         {
            v->editorUnstable.store(true, std::memory_order_relaxed);
            outError = "plugin crashed creating its editor view";
            return false;
         }
         if (view == nullptr)
         {
            outError = "plugin has no custom GUI editor";
            return false;
         }
         v->plugView = Steinberg::owned(view);
      }

      Steinberg::ViewRect rect = {};
      if (!RunPluginCallGuarded("getSize", h, [&] { v->plugView->getSize(&rect); }))
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         outError = "plugin crashed sizing its editor view";
         return false;
      }
      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;
      if (width < 120 || height < 80)
      {
         width = 640;
         height = 420;
      }

      bool canResize = false;
      RunPluginCallGuarded("canResize", h, [&] { canResize = v->plugView->canResize() == Steinberg::kResultTrue; });

      const int screen = DefaultScreen(display);
      ::Window xid = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, (unsigned int)width,
                                         (unsigned int)height, 0, BlackPixel(display, screen),
                                         WhitePixel(display, screen));
      if (xid == 0)
      {
         outError = "failed to create editor window";
         return false;
      }

      XStoreName(display, xid, h->desc.name.c_str());

      Atom wmDelete = WmDeleteWindowAtom(display);
      XSetWMProtocols(display, xid, &wmDelete, 1);

      XSizeHints hints = {};
      hints.flags = PMinSize | (canResize ? 0 : PMaxSize);
      hints.min_width = canResize ? 1 : width;
      hints.min_height = canResize ? 1 : height;
      hints.max_width = width;
      hints.max_height = height;
      XSetWMNormalHints(display, xid, &hints);

      XSelectInput(display, xid, StructureNotifyMask);

      RegisterEditorWindow(xid, h);

      // Spec requires setFrame() before attached() - it is how a plugin with
      // a resizable GUI learns who to ask for a resize.
      if (!v->plugFrame)
         v->plugFrame = Steinberg::owned(new HostPlugFrame(h));
      if (!RunPluginCallGuarded("setFrame", h, [&] { v->plugView->setFrame(v->plugFrame); }))
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         outError = "plugin crashed setting its editor frame";
         UnregisterEditorWindow(xid);
         XDestroyWindow(display, xid);
         XFlush(display);
         return false;
      }

      v->editorWindow = xid;
      v->canResize = canResize;

      // Host owns content scaling and must tell the plugin before attached()
      // so it builds its GUI at the right size in the first place.
      ApplyEditorContentScale(h);

      if (!RunPluginCallGuarded("attached", h, [&] {
             v->plugView->attached(reinterpret_cast<void*>(static_cast<uintptr_t>(xid)),
                                   Steinberg::kPlatformTypeX11EmbedWindowID);
          }))
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         outError = "plugin crashed opening its editor";
         // The plugin may already have installed GUI timers/observers by
         // this point - removed() first while the window is still alive,
         // then tear down the window, not the reverse.
         RunPluginCallGuarded("removed", h, [&] { v->plugView->removed(); });
         v->plugView = nullptr;
         v->editorWindow = 0;
         UnregisterEditorWindow(xid);
         XDestroyWindow(display, xid);
         XFlush(display);
         return false;
      }

      XMapRaised(display, xid);
      XFlush(display);
      SetPluginEditorOpenLinux(h, true);
      return true;
   }

   void PluginVST3CloseEditor(PluginHandle* h)
   {
      CloseEditorAndDestroyWindow(h);
   }

   bool PluginVST3EditorIsOpen(PluginHandle* h)
   {
      return h != nullptr && h->vst3 != nullptr && h->vst3->editorOpen.load(std::memory_order_acquire);
   }

   bool PluginVST3AnyEditorOpen()
   {
      return gLinuxOpenPluginEditorCount.load(std::memory_order_relaxed) > 0;
   }

   // Called every frame regardless of whether any editor is open (factory-
   // context IRunLoop timers exist with no editor). Order: fd/timer
   // dispatch first, then drain pending X11 events for our own windows,
   // bounded by a wall-clock budget rather than a fixed handler-count cap
   // (yabridge/JUCE editors can emit thousands of events on a GUI change).
   bool PluginVST3PumpEditorEvents()
   {
      bool any = PumpHostRunLoop();

      Display* display = SharedX11Display(nullptr);
      if (display == nullptr)
         return any;

      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
      while (XPending(display) > 0)
      {
         XEvent event;
         XNextEvent(display, &event);
         any = true;

         PluginHandle* h = nullptr;
         {
            std::lock_guard<std::mutex> lock(gEditorWindowsMutex);
            auto it = gEditorWindows.find(event.xany.window);
            if (it != gEditorWindows.end())
               h = it->second;
         }
         if (h == nullptr || h->vst3 == nullptr)
         {
            if (std::chrono::steady_clock::now() >= deadline)
               break;
            continue;
         }
         PluginVST3State* v = h->vst3;

         if (event.type == ClientMessage &&
             (Atom)event.xclient.data.l[0] == WmDeleteWindowAtom(display))
         {
            CloseEditorAndDestroyWindow(h);
         }
         else if (event.type == ConfigureNotify && v->plugView && v->canResize && !v->resizingFromPlugin)
         {
            const int width = event.xconfigure.width;
            const int height = event.xconfigure.height;
            if (width > 0 && height > 0)
            {
               Steinberg::ViewRect newSize(0, 0, width, height);
               if (!RunPluginCallGuarded("onSize", h, [&] { v->plugView->onSize(&newSize); }))
                  v->editorUnstable.store(true, std::memory_order_relaxed);
            }
         }

         if (std::chrono::steady_clock::now() >= deadline)
            break;
      }

      return any;
   }

#else // INFINITE_VST3_SCANNER - headless scanner build, never opens an editor.

   bool PluginVST3OpenEditor(PluginHandle* h, std::string& outError)
   {
      (void)h;
      outError = "plugin editors are not available in the scanner process";
      return false;
   }

   void PluginVST3CloseEditor(PluginHandle* h)
   {
      (void)h;
   }

   bool PluginVST3EditorIsOpen(PluginHandle* h)
   {
      (void)h;
      return false;
   }

   bool PluginVST3AnyEditorOpen()
   {
      return false;
   }

   bool PluginVST3PumpEditorEvents()
   {
      return false;
   }

#endif // !INFINITE_VST3_SCANNER

   // ------------------------------------------------------------------------
   // State Save & Restore
   // ------------------------------------------------------------------------

   bool PluginVST3SaveState(PluginHandle* h, std::string& outBase64)
   {
      outBase64.clear();
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->component)
         return false;
      if (h->vst3->stateCallsUnstable.load(std::memory_order_relaxed))
         return false;

      MemoryStream compStream;
      Steinberg::tresult compResult = Steinberg::kResultFalse;
      if (!RunPluginCallGuarded("getState (component)", h, [&]
             { compResult = h->vst3->component->getState(&compStream); }))
      {
         h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
         return false;
      }
      if (compResult != Steinberg::kResultOk)
         return false;

      MemoryStream ctrlStream;
      if (h->vst3->controller)
      {
         if (!RunPluginCallGuarded("getState (controller)", h, [&]
                { h->vst3->controller->getState(&ctrlStream); }))
         {
            h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
            return false;
         }
      }

      const uint64_t compSize = (uint64_t)compStream.getSize();
      const uint64_t ctrlSize = (uint64_t)ctrlStream.getSize();

      std::vector<uint8_t> payload;
      payload.resize(sizeof(uint64_t) + compSize + sizeof(uint64_t) + ctrlSize);

      uint8_t* ptr = payload.data();
      std::memcpy(ptr, &compSize, sizeof(uint64_t));
      ptr += sizeof(uint64_t);
      if (compSize > 0)
      {
         std::memcpy(ptr, compStream.getBuffer().data(), (size_t)compSize);
         ptr += compSize;
      }
      std::memcpy(ptr, &ctrlSize, sizeof(uint64_t));
      ptr += sizeof(uint64_t);
      if (ctrlSize > 0)
      {
         std::memcpy(ptr, ctrlStream.getBuffer().data(), (size_t)ctrlSize);
      }

      outBase64 = Base64Encode(payload);
      return !outBase64.empty();
   }

   bool PluginVST3RestoreState(PluginHandle* h, const std::string& base64)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->component || base64.empty())
         return false;
      if (h->vst3->stateCallsUnstable.load(std::memory_order_relaxed))
         return false;

      std::vector<uint8_t> payload;
      if (!Base64Decode(base64, payload))
         return false;

      if (payload.size() < sizeof(uint64_t) * 2)
         return false;

      const uint8_t* ptr = payload.data();
      const uint8_t* end = payload.data() + payload.size();

      uint64_t compSize = 0;
      std::memcpy(&compSize, ptr, sizeof(uint64_t));
      ptr += sizeof(uint64_t);

      if (ptr + compSize > end)
         return false;
      if (compSize > 0)
      {
         bool ok = RunPluginCallGuarded("setState (component)", h, [&]
         {
            MemoryStream compStream(ptr, (size_t)compSize);
            h->vst3->component->setState(&compStream);
            if (h->vst3->controller)
            {
               compStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
               h->vst3->controller->setComponentState(&compStream);
            }
         });
         if (!ok)
         {
            h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
            return false;
         }
         ptr += compSize;
      }

      if (ptr + sizeof(uint64_t) <= end)
      {
         uint64_t ctrlSize = 0;
         std::memcpy(&ctrlSize, ptr, sizeof(uint64_t));
         ptr += sizeof(uint64_t);
         if (ctrlSize > 0 && ptr + ctrlSize <= end && h->vst3->controller)
         {
            if (!RunPluginCallGuarded("setState (controller)", h, [&]
                   {
                      MemoryStream ctrlStream(ptr, (size_t)ctrlSize);
                      h->vst3->controller->setState(&ctrlStream);
                   }))
            {
               h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
               return false;
            }
         }
      }

      return true;
   }
}

#endif // INFINITE_ENABLE_VST3

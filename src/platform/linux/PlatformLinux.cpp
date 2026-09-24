#include "platform/Platform.h"
#include "platform/AppPaths.h"
#include "platform/common/SubjectMaskOnnx.h"
#include "tinyfiledialogs.h"

#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <climits>
#include <cstdint>
#include <algorithm>
#include <dlfcn.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char** environ;

namespace
{
   bool IsHeadlessOrExitAfter()
   {
      if (std::getenv("INFINITE_EXITAFTER") != nullptr)
         return true;
      const char* disp = std::getenv("DISPLAY");
      const char* wayland = std::getenv("WAYLAND_DISPLAY");
      if ((!disp || disp[0] == '\0') && (!wayland || wayland[0] == '\0'))
         return true;
      return false;
   }

   bool CheckExecutableOnPath(const char* exe)
   {
      const char* pathEnv = std::getenv("PATH");
      if (!pathEnv) return false;
      std::string pathStr = pathEnv;
      size_t start = 0;
      while (start < pathStr.size())
      {
         size_t colon = pathStr.find(':', start);
         std::string dir = (colon == std::string::npos) ? pathStr.substr(start) : pathStr.substr(start, colon - start);
         if (!dir.empty())
         {
            std::string full = dir + "/" + exe;
            if (access(full.c_str(), X_OK) == 0)
               return true;
         }
         if (colon == std::string::npos) break;
         start = colon + 1;
      }
      return false;
   }

   bool sDialogBackendChecked = false;
   bool sDialogBackendPresent = false;

   void EnsureDialogBackendChecked()
   {
      if (sDialogBackendChecked) return;
      sDialogBackendChecked = true;
      const char* const candidates[] = {
         "zenity", "kdialog", "yad", "qarma", "matedialog"
      };
      for (const char* c : candidates)
      {
         if (CheckExecutableOnPath(c))
         {
            sDialogBackendPresent = true;
            break;
         }
      }
      if (!sDialogBackendPresent)
      {
         Platform::AppendLogLine("[WARNING] No GUI dialog helper (zenity, kdialog, yad, qarma, matedialog) found on PATH. Native file dialogs may fail silently.");
      }
   }

   std::string AppendExtensionIfMissing(const std::string& path, const char* ext)
   {
      if (path.empty() || !ext || ext[0] == '\0')
         return path;
      std::string dotExt = (ext[0] == '.') ? std::string(ext) : (std::string(".") + ext);
      if (path.size() >= dotExt.size())
      {
         const std::string end = path.substr(path.size() - dotExt.size());
         if (strcasecmp(end.c_str(), dotExt.c_str()) == 0)
            return path;
      }
      return path + dotExt;
   }
}

namespace Platform
{
   bool HasGuiDialogHelper()
   {
      EnsureDialogBackendChecked();
      return sDialogBackendPresent;
   }

   void PreventAppNap()
   {
      // No App Nap mechanism on Linux that affects GLFW.
   }

   namespace
   {
      // Projector pacing clock (see Platform.h). GLX/EGL/Wayland offer no
      // portable per-display vblank wait, so this is a steady timer at the
      // display's rate: the right cadence, not locked to scanout phase.
      struct DisplayRefreshClock
      {
         std::chrono::steady_clock::time_point last{};
         bool haveLast = false;
      };
      DisplayRefreshClock gRefreshClock;
   }

   bool WaitForDisplayRefresh(int /*x*/, int /*y*/, double refreshHz, int intervals)
   {
      using Clock = std::chrono::steady_clock;
      if (refreshHz <= 0.0 || intervals < 1)
         return false;
      DisplayRefreshClock& clock = gRefreshClock;
      const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / refreshHz));
      const Clock::time_point now = Clock::now();
      if (!clock.haveLast)
      {
         clock.last = now;
         clock.haveLast = true;
      }
      Clock::time_point target = clock.last + period * intervals;
      if (now >= target) // late: next tick on the same phase grid
         target = clock.last + period * ((now - clock.last) / period + 1);
      const auto slack = target - Clock::now();
      if (slack > std::chrono::milliseconds(2))
         std::this_thread::sleep_for(slack - std::chrono::milliseconds(1));
      while (Clock::now() < target)
         std::this_thread::yield();
      clock.last = target;
      return true;
   }

   void StopDisplayRefreshClock()
   {
      gRefreshClock.haveLast = false;
   }

   double PollTrackpadMagnificationDelta()
   {
      // GLFW does not expose trackpad magnification gestures on X11/Wayland.
      return 0.0;
   }

   double ProcessRssMb()
   {
      // /proc/self/status's VmRSS line is already in kB and needs no root
      // access, unlike /proc/self/smaps_rollup on some hardened kernels.
      std::ifstream in("/proc/self/status");
      if (!in.is_open())
         return -1.0;
      std::string line;
      while (std::getline(in, line))
      {
         if (line.rfind("VmRSS:", 0) != 0)
            continue;
         const long kb = std::strtol(line.c_str() + 6, nullptr, 10);
         return kb > 0 ? (double)kb / 1024.0 : -1.0;
      }
      return -1.0;
   }

   double ProcessFootprintMb()
   {
      // Resident plus swapped out, so pages pushed to swap under pressure
      // still count. Both lines are in kB in /proc/self/status.
      std::ifstream in("/proc/self/status");
      if (!in.is_open())
         return -1.0;
      long rssKb = -1;
      long swapKb = 0;
      std::string line;
      while (std::getline(in, line))
      {
         if (line.rfind("VmRSS:", 0) == 0)
            rssKb = std::strtol(line.c_str() + 6, nullptr, 10);
         else if (line.rfind("VmSwap:", 0) == 0)
            swapKb = std::strtol(line.c_str() + 7, nullptr, 10);
      }
      return rssKb > 0 ? (double)(rssKb + swapKb) / 1024.0 : -1.0;
   }

   std::string HwModelString()
   {
      // Populated by firmware/DMI on most desktops and laptops; world-
      // readable, no root needed. Absent in some VMs/containers, hence the
      // fallback rather than treating a miss as an error.
      std::ifstream in("/sys/devices/virtual/dmi/id/product_name");
      if (!in.is_open())
         return "unknown";
      std::string model;
      std::getline(in, model);
      while (!model.empty() && (model.back() == '\n' || model.back() == '\r' || model.back() == ' '))
         model.pop_back();
      return model.empty() ? "unknown" : model;
   }

   void AppendLogLine(const std::string& line)
   {
      std::fprintf(stderr, "%s\n", line.c_str());
      std::string dir = AppPaths::AppSupportDir();
      if (!dir.empty())
      {
         std::string logFile = dir + "/log.txt";
         FILE* f = std::fopen(logFile.c_str(), "a");
         if (f != nullptr)
         {
            std::fprintf(f, "%s\n", line.c_str());
            std::fclose(f);
         }
      }
   }

   void ShowFatalError(const std::string& title, const std::string& message)
   {
      std::fprintf(stderr, "[FATAL] %s: %s\n", title.c_str(), message.c_str());
      AppendLogLine(std::string("[FATAL] ") + title + ": " + message);

      if (!IsHeadlessOrExitAfter())
      {
         tinyfd_messageBox(title.c_str(), message.c_str(), "ok", "error", 1);
      }
   }

   std::string OpenImageDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.tif", "*.tiff",
         "*.tga", "*.webp", "*.hdr", "*.pic", "*.ppm", "*.pgm"
      };
      const char* res = tinyfd_openFileDialog(
         "Choose Image",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "Image files",
         0
      );
      return res ? std::string(res) : std::string();
   }

   std::string OpenHdrDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.hdr", "*.exr"
      };
      const char* res = tinyfd_openFileDialog(
         "Choose HDR Environment",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "HDR images (*.hdr, *.exr)",
         0
      );
      return res ? std::string(res) : std::string();
   }

   std::string OpenModelDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.obj", "*.ply", "*.stl"
      };
      const char* res = tinyfd_openFileDialog(
         "Choose Model",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "3D models (*.obj, *.ply, *.stl)",
         0
      );
      return res ? std::string(res) : std::string();
   }

   std::string OpenPatchDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.infinite", "*.inf"
      };
      const char* res = tinyfd_openFileDialog(
         "Open Patch",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "Infinite patches (*.infinite, *.inf)",
         0
      );
      return res ? std::string(res) : std::string();
   }

   std::string SavePatchDialog(const std::string& suggestedName)
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.infinite"
      };
      std::string defaultPath = suggestedName.empty() ? "Untitled.infinite" : suggestedName;
      defaultPath = AppendExtensionIfMissing(defaultPath, ".infinite");

      const char* res = tinyfd_saveFileDialog(
         "Save Patch",
         defaultPath.c_str(),
         1,
         filterPatterns,
         "Infinite patch (*.infinite)"
      );
      if (!res) return "";
      return AppendExtensionIfMissing(std::string(res), ".infinite");
   }

   std::string OpenDeviceDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.field", "*.infdev"
      };
      const char* res = tinyfd_openFileDialog(
         "Open Device",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "Field device (*.field, *.infdev)",
         0
      );
      return res ? std::string(res) : std::string();
   }

   std::string SaveDeviceDialog(const std::string& suggestedName)
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = {
         "*.field"
      };
      std::string defaultPath = suggestedName.empty() ? "Untitled.field" : suggestedName;
      defaultPath = AppendExtensionIfMissing(defaultPath, ".field");

      const char* res = tinyfd_saveFileDialog(
         "Save Device",
         defaultPath.c_str(),
         1,
         filterPatterns,
         "Field device (*.field)"
      );
      if (!res) return "";
      return AppendExtensionIfMissing(std::string(res), ".field");
   }

   std::string OpenFolderDialog(const char* title, const std::string& initialDir)
   {
      if (IsHeadlessOrExitAfter()) return "";
      EnsureDialogBackendChecked();
      const char* res = tinyfd_selectFolderDialog(
         title ? title : "Select Folder",
         initialDir.empty() ? nullptr : initialDir.c_str()
      );
      return res ? std::string(res) : std::string();
   }

   void OpenExternalUrl(const std::string& url)
   {
      if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
         return;

      pid_t pid;
      char* argv[] = {
         const_cast<char*>("xdg-open"),
         const_cast<char*>(url.c_str()),
         nullptr
      };
      if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0)
      {
         int status = 0;
         waitpid(pid, &status, WNOHANG);
      }
   }

   std::string UriEncodePath(const std::string& path)
   {
      static const char* hex = "0123456789ABCDEF";
      std::string out;
      out.reserve(path.size());
      for (unsigned char c : path)
      {
         if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
         else
         {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
         }
      }
      return out;
   }

   void RevealInFileManager(const std::string& path)
   {
      if (path.empty())
         return;
      struct stat st;
      if (stat(path.c_str(), &st) != 0)
         return;

      const std::string uri = "file://" + UriEncodePath(path);

      // org.freedesktop.FileManager1.ShowItems is the D-Bus method GNOME
      // Files, Nautilus, Dolphin and most other Linux file managers
      // implement to select a file in its folder - the equivalent of
      // macOS's activateFileViewerSelectingURLs and Windows's
      // "explorer /select,". Spawn gdbus (ships with GLib, present on any
      // GNOME/KDE desktop) rather than linking libdbus directly.
      bool revealed = false;
      if (CheckExecutableOnPath("gdbus"))
      {
         const std::string uriArray = "['" + uri + "']";
         pid_t pid;
         char* argv[] = {
            const_cast<char*>("gdbus"),
            const_cast<char*>("call"),
            const_cast<char*>("--session"),
            const_cast<char*>("--dest"),
            const_cast<char*>("org.freedesktop.FileManager1"),
            const_cast<char*>("--object-path"),
            const_cast<char*>("/org/freedesktop/FileManager1"),
            const_cast<char*>("--method"),
            const_cast<char*>("org.freedesktop.FileManager1.ShowItems"),
            const_cast<char*>(uriArray.c_str()),
            const_cast<char*>(""),
            nullptr
         };
         if (posix_spawnp(&pid, "gdbus", nullptr, nullptr, argv, environ) == 0)
         {
            int status = 0;
            revealed = (waitpid(pid, &status, 0) == pid) && WIFEXITED(status) && WEXITSTATUS(status) == 0;
         }
      }

      if (revealed)
         return;

      // Fall back to opening the containing folder with xdg-open, the same
      // helper OpenExternalUrl uses - no selection, but the file is at
      // least visible rather than never opened at all. Never hand the file
      // itself to xdg-open here: a reveal must not open or execute it.
      const size_t slash = path.find_last_of('/');
      const std::string parentDir = (slash == std::string::npos) ? "." : path.substr(0, slash);
      pid_t pid;
      char* argv[] = {
         const_cast<char*>("xdg-open"),
         const_cast<char*>(parentDir.c_str()),
         nullptr
      };
      if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0)
      {
         int status = 0;
         waitpid(pid, &status, WNOHANG);
      }
   }

   // libcurl C API subset needed for HttpGet
   typedef void CURL;
   typedef int CURLcode;
   typedef int CURLoption;
   typedef int CURLINFO;

   constexpr CURLcode CURLE_OK = 0;
   constexpr CURLoption CURLOPT_URL = 10002;
   constexpr CURLoption CURLOPT_USERAGENT = 10018;
   constexpr CURLoption CURLOPT_WRITEFUNCTION = 20011;
   constexpr CURLoption CURLOPT_WRITEDATA = 10001;
   constexpr CURLoption CURLOPT_TIMEOUT = 13;
   constexpr CURLoption CURLOPT_FOLLOWLOCATION = 52;
   constexpr CURLoption CURLOPT_FAILONERROR = 45;
   constexpr CURLoption CURLOPT_NOSIGNAL = 99;
   constexpr CURLINFO CURLINFO_RESPONSE_CODE = 0x200000 + 2;

   struct CurlApi
   {
      void* handle = nullptr;
      CURL* (*easy_init)(void) = nullptr;
      CURLcode (*easy_setopt)(CURL*, CURLoption, ...) = nullptr;
      CURLcode (*easy_perform)(CURL*) = nullptr;
      void (*easy_cleanup)(CURL*) = nullptr;
      CURLcode (*easy_getinfo)(CURL*, CURLINFO, ...) = nullptr;
      const char* (*easy_strerror)(CURLcode) = nullptr;

      bool Load()
      {
         if (handle) return true;
         const char* const libs[] = { "libcurl.so.4", "libcurl.so.3", "libcurl.so" };
         for (const char* lib : libs)
         {
            handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
            if (handle) break;
         }
         if (!handle) return false;

         easy_init = (CURL* (*)(void))dlsym(handle, "curl_easy_init");
         easy_setopt = (CURLcode (*)(CURL*, CURLoption, ...))dlsym(handle, "curl_easy_setopt");
         easy_perform = (CURLcode (*)(CURL*))dlsym(handle, "curl_easy_perform");
         easy_cleanup = (void (*)(CURL*))dlsym(handle, "curl_easy_cleanup");
         easy_getinfo = (CURLcode (*)(CURL*, CURLINFO, ...))dlsym(handle, "curl_easy_getinfo");
         easy_strerror = (const char* (*)(CURLcode))dlsym(handle, "curl_easy_strerror");

         if (!easy_init || !easy_setopt || !easy_perform || !easy_cleanup || !easy_getinfo)
         {
            dlclose(handle);
            handle = nullptr;
            return false;
         }
         return true;
      }
   };

   static size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
   {
      auto* body = static_cast<std::string*>(userdata);
      constexpr size_t kMaxBodyBytes = 1 * 1024 * 1024;
      size_t total = size * nmemb;
      if (body->size() + total > kMaxBodyBytes)
      {
         size_t canTake = (body->size() < kMaxBodyBytes) ? (kMaxBodyBytes - body->size()) : 0;
         body->append(ptr, canTake);
         return 0; // abort transfer by returning different size
      }
      body->append(ptr, total);
      return total;
   }

   bool HttpGet(const std::string& url, const std::string& userAgent,
                std::string& outBody, std::string& outError,
                int timeoutSeconds)
   {
      outBody.clear();
      outError.clear();

      if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
      {
         outError = "url must be http(s)";
         return false;
      }

      static CurlApi curl;
      if (!curl.Load())
      {
         outError = "libcurl could not be loaded via dlopen";
         return false;
      }

      CURL* ch = curl.easy_init();
      if (!ch)
      {
         outError = "curl_easy_init failed";
         return false;
      }

      curl.easy_setopt(ch, CURLOPT_URL, url.c_str());
      curl.easy_setopt(ch, CURLOPT_USERAGENT, userAgent.c_str());
      curl.easy_setopt(ch, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
      curl.easy_setopt(ch, CURLOPT_WRITEDATA, &outBody);
      curl.easy_setopt(ch, CURLOPT_TIMEOUT, (long)timeoutSeconds);
      curl.easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
      curl.easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);

      CURLcode res = curl.easy_perform(ch);

      long statusCode = 0;
      curl.easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &statusCode);
      curl.easy_cleanup(ch);

      if (res != CURLE_OK)
      {
         outBody.clear();
         const char* errStr = curl.easy_strerror ? curl.easy_strerror(res) : nullptr;
         outError = errStr ? errStr : ("curl error " + std::to_string(res));
         return false;
      }

      if (statusCode < 200 || statusCode >= 300)
      {
         outBody.clear();
         outError = "http status " + std::to_string(statusCode);
         return false;
      }

      return true;
   }

   void InitDocumentHandlingPreGlfw()
   {
   }

   void InitDocumentHandlingPostGlfw()
   {
   }

   bool PollPendingOpenFile(std::string& /*outPath*/)
   {
      // Same story as PlatformWin.cpp's stub: launch-time opening is already
      // covered generically in main.cpp (argv[1], checked once at startup
      // for a .inf/.infinite extension - see the "Exec=Infinite %f" AppImage
      // desktop entry in tools/linux/package-appimage.sh, which relies on
      // exactly that path). What's missing here is only the Finder-style
      // "app already running, OS asks it to open another file" event, which
      // has no portable equivalent on X11/Wayland without a full
      // single-instance/D-Bus-activation mechanism - not implemented (P5).
      return false;
   }

   std::string ExecutablePath()
   {
      char buf[PATH_MAX];
      ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
      if (len > 0)
      {
         buf[len] = '\0';
         return std::string(buf);
      }
      return "";
   }

   std::string ScannerExecutablePath()
   {
      std::string exe = ExecutablePath();
      size_t slash = exe.find_last_of('/');
      std::string dir = (slash != std::string::npos) ? exe.substr(0, slash) : ".";
      return dir + "/infinite-vst3-scanner";
   }

   void SuppressAppUIForHeadlessProcess()
   {
   }

   std::string MattingModelPath()
   {
      std::string exe = ExecutablePath();
      size_t slash = exe.find_last_of('/');
      if (slash == std::string::npos)
         return {};
      return exe.substr(0, slash + 1) + "assets/models/u2netp.onnx";
   }

   bool SubjectMask(const std::vector<unsigned char>& inputRgba, int width, int height,
                    MattingMode mode, std::vector<unsigned char>& outAlpha,
                    std::string& outError)
   {
      return OrtMatting::SubjectMask(MattingModelPath(), inputRgba, width, height, mode, outAlpha, outError);
   }

   std::string MattingBackend()
   {
      return OrtMatting::MattingBackend();
   }

   const std::vector<std::string>& MattingModeNames()
   {
      return OrtMatting::MattingModeNames();
   }
}

#include "platform/Platform.h"
#include "platform/AppPaths.h"
#include "platform/common/SingleInstance.h"
#include "platform/common/SubjectMaskOnnx.h"
#include "platform/linux/HostEnvironmentLinux.h"
#include "tinyfiledialogs.h"

#include <chrono>
#include <thread>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
   std::vector<std::string> PreferredLanguages()
   {
      // POSIX precedence: LANGUAGE (colon list, only honoured when the locale is not C), then
      // LC_ALL, LC_MESSAGES, LANG. "de_DE.UTF-8@euro" -> "de-DE"; "C"/"POSIX" carry no language.
      std::vector<std::string> out;
      auto addTag = [&out](std::string tag) {
         const size_t cut = tag.find_first_of(".@");
         if (cut != std::string::npos)
            tag.erase(cut);
         if (tag.empty() || tag == "C" || tag == "POSIX")
            return;
         for (char& c : tag)
            if (c == '_')
               c = '-';
         out.push_back(tag);
      };
      if (const char* langs = std::getenv("LANGUAGE"))
      {
         std::string all(langs);
         size_t start = 0;
         while (start <= all.size())
         {
            const size_t colon = all.find(':', start);
            addTag(all.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
            if (colon == std::string::npos)
               break;
            start = colon + 1;
         }
      }
      for (const char* var : { "LC_ALL", "LC_MESSAGES", "LANG" })
         if (const char* v = std::getenv(var))
            if (v[0] != '\0')
            {
               addTag(v);
               break;
            }
      return out;
   }

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

   void RevealLogs()
   {
      const std::string dir = AppPaths::AppSupportDir();
      if (dir.empty())
         return;
      const std::string file = dir + "/log.txt";
      if (FILE* f = std::fopen(file.c_str(), "a"))
         std::fclose(f);
      RevealInFileManager(file);
   }

   bool GpuPreferenceSupported() { return false; }
   int GpuPreference() { return 0; }
   void SetGpuPreference(int) {}

   void ShowFatalError(const std::string& title, const std::string& message)
   {
      std::fprintf(stderr, "[FATAL] %s: %s\n", title.c_str(), message.c_str());
      AppendLogLine(std::string("[FATAL] ") + title + ": " + message);

      if (!IsHeadlessOrExitAfter())
      {
         ScopedHostEnvironment hostEnv;
         tinyfd_messageBox(title.c_str(), message.c_str(), "ok", "error", 1);
      }
   }

   std::string OpenImageDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
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
      ScopedHostEnvironment hostEnv;
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
      ScopedHostEnvironment hostEnv;
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
      ScopedHostEnvironment hostEnv;
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
      ScopedHostEnvironment hostEnv;
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
      ScopedHostEnvironment hostEnv;
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

   std::string OpenExtensionPackDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = { "*.infpack", "*.zip" };
      const char* res = tinyfd_openFileDialog(
         "Install Extension Pack",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "Extension pack (*.infpack, *.zip)",
         0
      );
      return res ? std::string(res) : std::string();
   }

   std::string OpenShowSetDialog()
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = { "*.infshow" };
      const char* res = tinyfd_openFileDialog("Open Show Set", "", 1, filterPatterns, "Show set (*.infshow)", 0);
      return res ? std::string(res) : std::string();
   }

   std::string SaveShowSetDialog(const std::string& suggestedName)
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = { "*.infshow" };
      std::string defaultPath = suggestedName.empty() ? "Untitled.infshow" : suggestedName;
      defaultPath = AppendExtensionIfMissing(defaultPath, ".infshow");
      const char* res = tinyfd_saveFileDialog("Save Show Set", defaultPath.c_str(), 1, filterPatterns, "Show set (*.infshow)");
      if (!res) return "";
      return AppendExtensionIfMissing(std::string(res), ".infshow");
   }

   std::string SaveWavDialog(const std::string& suggestedName)
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
      EnsureDialogBackendChecked();
      const char* const filterPatterns[] = { "*.wav" };
      std::string defaultPath = AppendExtensionIfMissing(suggestedName.empty() ? "sample.wav" : suggestedName, ".wav");
      const char* res = tinyfd_saveFileDialog("Export wav", defaultPath.c_str(), 1, filterPatterns, "Wave file (*.wav)");
      if (!res) return "";
      return AppendExtensionIfMissing(std::string(res), ".wav");
   }

   std::string SaveRenderDialog(const std::string& suggestedName, bool video)
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
      EnsureDialogBackendChecked();
      const char* const videoPatterns[] = { "*.mp4", "*.mov" };
      const char* const imagePatterns[] = { "*.png", "*.jpg" };
      const char* res = tinyfd_saveFileDialog(video ? "Render Animation" : "Render Image", suggestedName.c_str(), 2,
                                              video ? videoPatterns : imagePatterns,
                                              video ? "Video (*.mp4, *.mov)" : "Image (*.png, *.jpg)");
      if (!res) return "";
      std::string path(res);
      const size_t dot = path.rfind('.');
      const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
      const bool known = video ? (ext == ".mp4" || ext == ".mov") : (ext == ".png" || ext == ".jpg" || ext == ".jpeg");
      return known ? path : path + (video ? ".mp4" : ".png");
   }

   std::string SaveDeviceDialog(const std::string& suggestedName)
   {
      if (IsHeadlessOrExitAfter()) return "";
      ScopedHostEnvironment hostEnv;
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
      ScopedHostEnvironment hostEnv;
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

      ScopedHostEnvironment hostEnv;

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

      ScopedHostEnvironment hostEnv;

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

   // ---- patch opener --------------------------------------------------------
   // The AppImage carries infinite.desktop + the MIME type inside itself, but nothing installs them, so out of the
   // box double-click opens nothing (or a copy an AppImage integrator registered earlier).

   static const char* kPatchMime = "application/x-infinite-patch";
   static const char* kOpenerDesktop = "infinite-patch.desktop";

   static std::string DataHome()
   {
      const char* x = std::getenv("XDG_DATA_HOME");
      return (x != nullptr && x[0] == '/') ? std::string(x) : AppPaths::HomeDir() + "/.local/share";
   }

   static bool RunHostTool(std::vector<std::string> args, std::string* out = nullptr)
   {
      ScopedHostEnvironment hostEnv;
      int pipeFd[2] = { -1, -1 };
      if (out != nullptr && pipe(pipeFd) != 0)
         return false;
      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      if (out != nullptr)
      {
         posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDOUT_FILENO);
         posix_spawn_file_actions_addclose(&actions, pipeFd[0]);
      }
      std::vector<char*> argv;
      for (std::string& a : args)
         argv.push_back(const_cast<char*>(a.c_str()));
      argv.push_back(nullptr);
      pid_t pid;
      const bool spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) == 0;
      posix_spawn_file_actions_destroy(&actions);
      if (out != nullptr)
      {
         close(pipeFd[1]);
         char buf[512];
         ssize_t n;
         while (spawned && (n = read(pipeFd[0], buf, sizeof(buf))) > 0)
            out->append(buf, (size_t)n);
         close(pipeFd[0]);
      }
      int status = 0;
      return spawned && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
   }

   // First Exec= token of a desktop entry, quotes and the %-codes dropped.
   static std::string DesktopExec(const std::string& desktopId)
   {
      std::vector<std::string> dirs { DataHome() };
      const char* sys = std::getenv("XDG_DATA_DIRS");
      std::string rest = (sys != nullptr && sys[0] != '\0') ? sys : "/usr/local/share:/usr/share";
      for (size_t start = 0; start <= rest.size();)
      {
         const size_t colon = rest.find(':', start);
         dirs.push_back(rest.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
         if (colon == std::string::npos)
            break;
         start = colon + 1;
      }
      for (const std::string& dir : dirs)
      {
         std::ifstream f(dir + "/applications/" + desktopId);
         std::string line;
         while (f.good() && std::getline(f, line))
         {
            if (line.rfind("Exec=", 0) != 0)
               continue;
            // Two escape levels, undone in order: the file format's (\\ is one backslash), then Exec's quoting.
            std::string v, token;
            for (size_t i = 5; i < line.size(); ++i)
               v += (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '\\') ? line[i++] : line[i];
            bool quoted = false;
            for (size_t i = 0; i < v.size(); ++i)
            {
               const char c = v[i];
               if (c == '"') { quoted = !quoted; continue; }
               if (c == '\\' && i + 1 < v.size()) { token += v[++i]; continue; }
               if (c == ' ' && !quoted) { if (!token.empty()) break; continue; }
               token += c;
            }
            return token;
         }
      }
      return {};
   }

   bool QueryPatchOpener(PatchOpener& out)
   {
      out = PatchOpener();
      const char* appImage = std::getenv("APPIMAGE");
      out.self = (appImage != nullptr && appImage[0] == '/') ? std::string(appImage) : ExecutablePath();
      if (out.self.empty())
         return false;
      // The AppImage's own mount point vanishes when it exits; only $APPIMAGE (the file itself) is a stable path.
      out.selfTemporary = out.self.rfind("/tmp/.mount_", 0) == 0;
      std::string id;
      if (!RunHostTool({ "xdg-mime", "query", "default", kPatchMime }, &id))
         return false;
      while (!id.empty() && (id.back() == '\n' || id.back() == ' '))
         id.pop_back();
      if (id.empty())
         return true;   // nothing registered
      out.opener = DesktopExec(id);
      if (out.opener.empty())
         out.opener = id;
      const std::string name = out.opener.substr(out.opener.find_last_of('/') + 1);
      std::string lower = name + " " + id;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      out.isSelf = out.opener == out.self;
      out.otherApp = lower.find("infinite") == std::string::npos;
      // AppImages are named Infinite-0.5.0-x86_64.AppImage; nothing else carries a version to read.
      for (size_t i = name.find('-'); i != std::string::npos; i = name.find('-', i + 1))
         if (i + 1 < name.size() && std::isdigit((unsigned char)name[i + 1]))
         {
            const size_t end = name.find_first_not_of("0123456789.", i + 1);
            out.version = name.substr(i + 1, end == std::string::npos ? std::string::npos : end - (i + 1));
            break;
         }
      return true;
   }

   bool MakeThisCopyPatchOpener(const PatchOpener& current, std::string& outError)
   {
      outError.clear();
      if (current.self.empty() || current.selfTemporary)
      {
         outError = "This copy has no stable path. Run the .AppImage file itself.";
         return false;
      }
      const std::string data = DataHome();
      std::error_code ec;
      std::filesystem::create_directories(data + "/mime/packages", ec);
      std::filesystem::create_directories(data + "/applications", ec);
      std::ofstream(data + "/mime/packages/infinite.xml")
         << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
            "  <mime-type type=\"" << kPatchMime << "\">\n"
            "    <comment>Infinite patch</comment>\n"
            "    <glob pattern=\"*.infinite\"/>\n"
            "    <glob pattern=\"*.inf\"/>\n"
            "  </mime-type>\n"
            "</mime-info>\n";
      // Exec quoting: inside "...", escape " ` $ \ with a backslash, and the file format doubles every backslash.
      std::string exec;
      for (char c : current.self)
      {
         if (c == '"' || c == '`' || c == '$' || c == '\\')
            exec += "\\\\";
         exec += c;
      }
      {
         std::ofstream f(data + "/applications/" + kOpenerDesktop);
         f << "[Desktop Entry]\nType=Application\nName=Infinite\nComment=Opens Infinite patches\n"
           << "Exec=\"" << exec << "\" %f\nIcon=infinite\nTerminal=false\nNoDisplay=true\n"
           << "MimeType=" << kPatchMime << ";\n";
         if (!f.good())
         {
            outError = "Could not write " + data + "/applications/" + kOpenerDesktop;
            return false;
         }
      }
      RunHostTool({ "update-mime-database", data + "/mime" });          // optional: absent on minimal systems
      RunHostTool({ "update-desktop-database", data + "/applications" });
      if (!RunHostTool({ "xdg-mime", "default", kOpenerDesktop, kPatchMime }))
      {
         outError = "xdg-mime is missing or refused (install xdg-utils).";
         return false;
      }
      PatchOpener after;
      if (!QueryPatchOpener(after) || !after.isSelf)
      {
         outError = "The desktop still opens patches with " + (after.opener.empty() ? std::string("nothing") : after.opener) + ".";
         return false;
      }
      return true;
   }

   // ---- in-place update -----------------------------------------------------

   static std::string sStagedUpdate;   // <dir>/.Infinite-update.AppImage, set by PrepareSelfUpdate

   static std::string SelfAppImage()
   {
      const char* a = std::getenv("APPIMAGE");
      return (a != nullptr && a[0] == '/') ? std::string(a) : std::string();
   }

   bool CanSelfUpdate(std::string& whyNot)
   {
      whyNot.clear();
      const std::string self = SelfAppImage();
      if (self.empty())
         whyNot = "Only the AppImage updates itself.";
      else if (access(self.substr(0, self.find_last_of('/')).c_str(), W_OK) != 0 || access(self.c_str(), W_OK) != 0)
         whyNot = "This account can't write to the folder Infinite is in.";
      return whyNot.empty();
   }

   bool PrepareSelfUpdate(const std::string& payload, const std::string& version, std::string& outError)
   {
      (void)version;   // an AppImage carries no readable version; the caller has already matched the release's SHA-256
      outError.clear();
      sStagedUpdate.clear();
      if (!CanSelfUpdate(outError))
         return false;
      // ELF with the AppImage type-2 magic ("AI\2" at offset 8): refuses an HTML error page or a truncated file.
      unsigned char head[11] = {};
      {
         std::ifstream f(payload, std::ios::binary);
         f.read(reinterpret_cast<char*>(head), sizeof(head));
         if (!f || std::memcmp(head, "\x7f" "ELF", 4) != 0 || std::memcmp(head + 8, "AI\x02", 3) != 0)
         {
            outError = "The download is not an AppImage.";
            return false;
         }
      }
      const std::string self = SelfAppImage();
      const std::string staged = self.substr(0, self.find_last_of('/')) + "/.Infinite-update.AppImage";
      std::error_code ec;
      std::filesystem::remove(staged, ec);
      // Copied (not moved) so it lands on this copy's own filesystem and the final swap is one rename.
      if (!std::filesystem::copy_file(payload, staged, ec) || chmod(staged.c_str(), 0755) != 0)
      {
         std::filesystem::remove(staged, ec);
         outError = "Could not copy the new version next to this one.";
         return false;
      }
      sStagedUpdate = staged;
      return true;
   }

   bool LaunchSelfUpdateHelper(bool relaunch, std::string& outError)
   {
      outError.clear();
      if (sStagedUpdate.empty())
      {
         outError = "No update is ready.";
         return false;
      }
      // Waits for this process, then one rename: the running AppImage's inode stays alive until it exits, and a
      // failed rename leaves this copy untouched. Detached (own session) so it outlives us.
      std::vector<std::string> args { "/bin/sh", "-c",
         "pid=$1; self=$2; staged=$3; relaunch=$4\n"
         "while kill -0 \"$pid\" 2>/dev/null; do sleep 0.2; done\n"
         "mv -f \"$staged\" \"$self\" || exit 1\n"
         "[ \"$relaunch\" = 1 ] && \"$self\" >/dev/null 2>&1 &\n"
         "exit 0\n",
         "infinite-update", std::to_string((int)getpid()), SelfAppImage(), sStagedUpdate, relaunch ? "1" : "0" };
      std::vector<char*> argv;
      for (std::string& a : args)
         argv.push_back(const_cast<char*>(a.c_str()));
      argv.push_back(nullptr);
      ScopedHostEnvironment hostEnv;   // the new copy must not inherit this AppImage's mount paths
      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
      pid_t pid;
      const bool spawned = posix_spawn(&pid, argv[0], nullptr, &attr, argv.data(), environ) == 0;
      posix_spawnattr_destroy(&attr);
      if (!spawned)
      {
         outError = "Could not start the update helper.";
         return false;
      }
      return true;
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

   struct CurlDownloadCtx
   {
      FILE* file = nullptr;
      const Platform::HttpProgress* progress = nullptr;
      bool cancelled = false;
   };

   static size_t CurlFileWrite(char* ptr, size_t size, size_t nmemb, void* userdata)
   {
      auto* ctx = static_cast<CurlDownloadCtx*>(userdata);
      return fwrite(ptr, size, nmemb, ctx->file) == nmemb ? size * nmemb : 0;
   }

   static int CurlXferInfo(void* userdata, long long dltotal, long long dlnow, long long, long long)
   {
      auto* ctx = static_cast<CurlDownloadCtx*>(userdata);
      if (ctx->progress && *ctx->progress && !(*ctx->progress)((uint64_t)dlnow, (uint64_t)dltotal))
      {
         ctx->cancelled = true;
         return 1; // non-zero aborts the transfer
      }
      return 0;
   }

   bool HttpDownload(const std::string& url, const std::string& userAgent,
                     const std::string& destPath, const HttpProgress& progress,
                     std::string& outError, int timeoutSeconds)
   {
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
      CurlDownloadCtx ctx;
      ctx.file = fopen(destPath.c_str(), "wb");
      ctx.progress = &progress;
      if (!ctx.file)
      {
         curl.easy_cleanup(ch);
         outError = "cannot write the download";
         return false;
      }
      constexpr CURLoption kNoProgress = 43;
      constexpr CURLoption kXferInfoFn = 20219;
      constexpr CURLoption kXferInfoData = 10057;
      curl.easy_setopt(ch, CURLOPT_URL, url.c_str());
      curl.easy_setopt(ch, CURLOPT_USERAGENT, userAgent.c_str());
      curl.easy_setopt(ch, CURLOPT_WRITEFUNCTION, CurlFileWrite);
      curl.easy_setopt(ch, CURLOPT_WRITEDATA, &ctx);
      curl.easy_setopt(ch, kNoProgress, 0L);
      curl.easy_setopt(ch, kXferInfoFn, CurlXferInfo);
      curl.easy_setopt(ch, kXferInfoData, &ctx);
      curl.easy_setopt(ch, CURLOPT_TIMEOUT, (long)timeoutSeconds);
      curl.easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
      curl.easy_setopt(ch, CURLOPT_FAILONERROR, 1L);
      curl.easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
      CURLcode res = curl.easy_perform(ch);
      long statusCode = 0;
      curl.easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &statusCode);
      curl.easy_cleanup(ch);
      const bool closed = fclose(ctx.file) == 0;
      if (res != CURLE_OK || !closed)
      {
         remove(destPath.c_str());
         if (ctx.cancelled)
            outError = "cancelled";
         else
         {
            const char* errStr = curl.easy_strerror ? curl.easy_strerror(res) : nullptr;
            outError = res != CURLE_OK ? (errStr ? errStr : ("curl error " + std::to_string(res)))
                                       : "cannot write the download";
         }
         return false;
      }
      // Same contract as the other platforms: one final report, cancel honoured.
      std::error_code sizeEc;
      const uint64_t finalSize = (uint64_t)std::filesystem::file_size(destPath, sizeEc);
      if (progress && !progress(finalSize, finalSize))
      {
         remove(destPath.c_str());
         outError = "cancelled";
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

   bool ForwardOpenToRunningInstance(const std::string& patchPath)
   {
      const std::string dir = AppPaths::AppSupportDir();
      if (dir.empty())
         return true;
      if (SingleInstance::BecomePrimary(dir))
         return true;
      // Someone else holds the lock. With no path there is nothing to hand over;
      // run as a second instance, as before.
      if (patchPath.empty())
         return true;
      return !SingleInstance::Forward(dir, patchPath);
   }

   bool PollPendingOpenFile(std::string& outPath)
   {
      static auto sLast = std::chrono::steady_clock::time_point();
      const auto now = std::chrono::steady_clock::now();
      if (now - sLast < std::chrono::milliseconds(250))
         return false;
      sLast = now;
      static const std::string sDir = AppPaths::AppSupportDir();
      return !sDir.empty() && SingleInstance::Poll(sDir, outPath);
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

   void AttachConsoleForHeadless()
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

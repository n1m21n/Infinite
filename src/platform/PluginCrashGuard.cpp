#include "platform/PluginCrashGuard.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "AppPaths.h"

namespace
{
   // Fixed buffer, filled in Init on a healthy heap: the crash path only reads it.
   char gMarkerPath[1024] = {};
   std::string gListPath;
   std::set<std::string> gCrashed;
   thread_local const char* tCurrent = nullptr;

   void SaveList()
   {
      if (gListPath.empty())
         return;
      std::ofstream out(AppPaths::FsPath(gListPath), std::ios::trunc);
      for (const std::string& id : gCrashed)
         out << id << "\n";
   }
}

namespace PluginCrashGuard
{
   std::string Init(const std::string& dir)
   {
      gCrashed.clear();
      gMarkerPath[0] = '\0';
      gListPath.clear();
      if (dir.empty())
         return std::string();

      const std::string marker = dir + "/plugin-crash-marker.txt";
      if (marker.size() >= sizeof(gMarkerPath))
         return std::string();
      gListPath = dir + "/plugin-crashed.txt";

      {
         std::ifstream in(AppPaths::FsPath(gListPath));
         std::string line;
         while (std::getline(in, line))
            if (!line.empty())
               gCrashed.insert(line);
      }

      std::string crashed;
      {
         std::ifstream in(AppPaths::FsPath(marker));
         std::getline(in, crashed);
      }
      if (!crashed.empty())
      {
         gCrashed.insert(crashed);
         SaveList();
      }
      std::remove(marker.c_str());

      std::memcpy(gMarkerPath, marker.c_str(), marker.size() + 1);
      return crashed;
   }

   Scope::Scope(const char* identifier)
   : mPrevious(tCurrent)
   {
      tCurrent = identifier;
   }

   Scope::~Scope()
   {
      tCurrent = mPrevious;
   }

   void NoteCrash()
   {
      const char* id = tCurrent;
      if (id == nullptr || id[0] == '\0' || gMarkerPath[0] == '\0')
         return;
#ifdef _WIN32
      const int fd = _open(gMarkerPath, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
      if (fd < 0)
         return;
      _write(fd, id, (unsigned int)std::strlen(id));
      _close(fd);
#else
      const int fd = open(gMarkerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0)
         return;
      const ssize_t written = write(fd, id, std::strlen(id));
      (void)written;
      close(fd);
#endif
   }

   bool CrashedBefore(const std::string& identifier)
   {
      return !identifier.empty() && gCrashed.count(identifier) != 0;
   }

   void Forget(const std::string& identifier)
   {
      if (gCrashed.erase(identifier) != 0)
         SaveList();
   }
}

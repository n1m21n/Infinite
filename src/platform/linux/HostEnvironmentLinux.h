#pragma once

#include <cstdlib>
#include <string>

namespace Platform
{
   // RAII guard that cleanses AppImage-bundled library paths from LD_LIBRARY_PATH
   // (and any other AppImage runtime overrides) before executing host system
   // utilities (zenity, kdialog, yad, qarma, matedialog, xdg-open, gdbus, etc.).
   //
   // Why:
   // In an AppImage, AppRun sets LD_LIBRARY_PATH to point to $APPDIR/usr/lib.
   // That directory contains bundled libraries from the build host (e.g. Ubuntu 22.04).
   // Host utilities (like /usr/bin/zenity or /usr/bin/xdg-open) are dynamically linked
   // against host libraries (e.g. host GTK4, Qt, libpng, glibc). If LD_LIBRARY_PATH
   // points to the AppImage's bundled libraries, the host binary loads the older/mismatched
   // bundled libraries instead of the host's, causing symbol lookup errors:
   // e.g. "zenity: symbol lookup error: /usr/lib/libgtk-4.so.1: undefined symbol: png_get_cICP" (Issue #25).
   //
   // This guard:
   // 1. If APPIMAGE_ORIGINAL_LD_LIBRARY_PATH is set (exported by AppRun), restores
   //    LD_LIBRARY_PATH to that original value (or unsets it if it was empty).
   // 2. Otherwise, if running inside an AppImage ($APPDIR or /tmp/.mount_ paths in LD_LIBRARY_PATH),
   //    strips those AppImage directories from LD_LIBRARY_PATH (or unsets it if nothing remains).
   // 3. On destruction, restores LD_LIBRARY_PATH to its exact previous state.
   struct ScopedHostEnvironment
   {
      std::string mSavedLdLibPath;
      bool mHadLdLibPath = false;

      ScopedHostEnvironment()
      {
         const char* cur = std::getenv("LD_LIBRARY_PATH");
         if (!cur)
            return;

         mHadLdLibPath = true;
         mSavedLdLibPath = cur;

         const char* orig = std::getenv("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH");
         if (orig)
         {
            if (orig[0] != '\0')
               setenv("LD_LIBRARY_PATH", orig, 1);
            else
               unsetenv("LD_LIBRARY_PATH");
            return;
         }

         // Fallback if APPIMAGE_ORIGINAL_LD_LIBRARY_PATH was not set:
         // Strip any component that points into the AppImage mount point ($APPDIR or /tmp/.mount_*).
         const char* appdir = std::getenv("APPDIR");
         const std::string appdirStr = appdir ? appdir : "";

         std::string cleaned;
         const std::string curStr = cur;
         size_t start = 0;
         while (start < curStr.size())
         {
            size_t colon = curStr.find(':', start);
            std::string dir = (colon == std::string::npos) ? curStr.substr(start) : curStr.substr(start, colon - start);
            bool isAppDir = false;
            if (!appdirStr.empty() && dir.rfind(appdirStr, 0) == 0)
               isAppDir = true;
            else if (dir.rfind("/tmp/.mount_", 0) == 0)
               isAppDir = true;

            if (!isAppDir && !dir.empty())
            {
               if (!cleaned.empty())
                  cleaned += ":";
               cleaned += dir;
            }
            if (colon == std::string::npos)
               break;
            start = colon + 1;
         }

         if (cleaned.empty())
            unsetenv("LD_LIBRARY_PATH");
         else
            setenv("LD_LIBRARY_PATH", cleaned.c_str(), 1);
      }

      ~ScopedHostEnvironment()
      {
         if (mHadLdLibPath)
            setenv("LD_LIBRARY_PATH", mSavedLdLibPath.c_str(), 1);
         else
            unsetenv("LD_LIBRARY_PATH");
      }

      ScopedHostEnvironment(const ScopedHostEnvironment&) = delete;
      ScopedHostEnvironment& operator=(const ScopedHostEnvironment&) = delete;
   };
}

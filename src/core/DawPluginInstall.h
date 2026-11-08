#pragma once
// Settings > DAW plugin (docs/plans/vst-au/README.md 8.11, step 3.6): copies the plugin bundles that
// ride inside the app into the user's plugin folders, and records which app is their engine.
//
//   source   macOS  Infinite.app/Contents/Resources/Plugins/*.{vst3,component}
//            Windows  <exe dir>\Plugins\*.vst3
//            Linux  $APPDIR/usr/share/infinite/Plugins, else <exe dir>/Plugins, else <exe dir>/../share/infinite/Plugins
//   target   macOS  ~/Library/Audio/Plug-Ins/{VST3,Components}
//            Windows  %LOCALAPPDATA%\Programs\Common\VST3 (per user, no admin)
//            Linux  ~/.vst3
//   record   <License::Dir()>/plugin-install.txt: line 1 the app version, line 2 the engine path,
//            line 3 the formats chosen ("vst3", "au", "vst3,au"; missing = all).
//            The shell reads line 2 to find its engine (Shell.cpp FindEngine).
//
// Copy-then-rename per bundle: a new copy lands beside the old one, the old one is renamed aside and
// the new one renamed in, then the old one is removed. On Windows a DAW holding the old DLL keeps
// the removal from finishing; it is retried on the next install.
#include <string>
#include <vector>

namespace DawPlugin
{
   struct Env
   {
      std::string exe;      // this process's executable
      std::string engine;   // what the shell should run: the AppImage file on Linux, else exe
      std::string version;  // INFINITE_VERSION_STRING
      std::string home;
      std::string localAppData; // Windows only
      std::string appDir;   // $APPDIR inside an AppImage
      std::string formats;  // "vst3", "au", "vst3,au"; empty = every format this OS has
   };
   // The formats an install chose (the installer's checkboxes), from `--install-plugins vst3,au`.
   bool HasFormat(const Env& env, const char* format);
   Env CurrentEnv(const std::string& exe, const std::string& version);

   struct Bundle
   {
      std::string name;   // "Infinite FX.vst3"
      std::string source; // full path inside the app
      std::string target; // full path in the plugin folder
   };

   struct Status
   {
      bool bundled = false;   // the app carries plugin bundles
      bool installed = false; // a record exists and every bundle is in place
      bool current = false;   // installed, same version, same engine
      std::string installedVersion;
      std::vector<std::string> folders; // target folders, for display
      std::vector<Bundle> bundles;
   };

   std::string SourceDir(const Env& env);
   std::vector<Bundle> Bundles(const Env& env);
   Status Describe(const Env& env);

   // Copies every bundle and writes the record. False and a message when a copy failed; bundles
   // already copied stay (each is complete, never half-written).
   bool Install(const Env& env, std::string& error);
   // Removes the bundles and the record.
   bool Uninstall(const Env& env, std::string& error);
   // At app launch: re-copies only when a record exists and is stale (other version, other engine,
   // a bundle missing). True when nothing was needed or the copy worked.
   bool RecopyIfStale(const Env& env, std::string& error);

   std::string RecordPath();
   // Line 2 of the record, empty when there is none. Used by the plugin shell.
   std::string RecordedEngine();
}

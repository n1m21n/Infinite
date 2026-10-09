#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Extension packs: optional downloads (models, runtime libraries) installed
// from Settings > Extensions into <AppSupport>/extensions/<id>/. Nodes that
// need a pack are always registered; they ask PackDir(id) at run time and show
// an install hint while it is empty (the same pattern as Ndi::StatusLine()).
// See docs/plans/extensions/README.md.
//
// A pack is a zip with a `pack.json` at its root: {"id": "...", "version": "..."}.
// The catalog is one JSON file on the GitHub release (schema 1):
//   { "schema": 1, "packs": [ { "id", "name", "purpose", "version", "size",
//       "files": { "macos": {"url","sha256"}, "windows-x64": {...},
//                  "windows-arm64": {...}, "linux-x64": {...} } } ] }
//
// The pure functions are safe on any thread. The async calls run one worker at
// a time; the UI thread reads State() after Poll().
namespace Extensions
{
   struct Pack
   {
      std::string id;
      std::string name;
      std::string purpose;
      std::string version;
      uint64_t size = 0;      // download size in bytes, 0 = unknown
      std::string url;        // for this platform
      std::string sha256;     // lowercase hex, for this platform
   };

   // "macos", "windows-x64", "windows-arm64", "linux-x64" for this build.
   std::string PlatformKey();

   // Pure. Keeps only packs that have a file for `platformKey`. False + err on
   // malformed JSON or an unsupported schema.
   bool ParseManifest(const std::string& json, const std::string& platformKey,
                      std::vector<Pack>& out, std::string& err);

   std::string Sha256Hex(const void* data, size_t len);

   // <AppSupport>/extensions, or $INFINITE_EXTENSIONS_DIR (tests). "" when unavailable.
   std::string RootDir();
   // The install folder of a pack, or "" when not installed (or `id` is invalid).
   std::string PackDir(const std::string& id);
   std::string InstalledVersion(const std::string& id); // "" when not installed
   bool IsValidId(const std::string& id);

   // Synchronous install from a zip in memory. Verifies the SHA-256 when
   // `expectedSha256` is non-empty, rejects entries that escape the pack folder,
   // checks pack.json (and `expectId` when non-empty), then swaps the folder in.
   // Replacing an installed pack keeps the old one if anything fails.
   bool InstallZip(const std::string& zipBytes, const std::string& expectedSha256,
                   const std::string& expectId, std::string& outId, std::string& err);
   bool Remove(const std::string& id);

   // ---- async, UI-facing ---------------------------------------------------
   struct State
   {
      std::vector<Pack> catalog;
      bool catalogLoaded = false;
      bool catalogLoading = false;
      std::string catalogError;
      std::string busyId;        // pack being installed, "" when idle
      std::string busyLabel;     // "Downloading", "Installing", "Removing"
      float progress = -1.0f;    // 0..1 while downloading, -1 when unknown
      std::vector<std::pair<std::string, std::string>> installed; // id, version
      std::string message;       // last result line
      bool messageIsError = false;
   };

   void RefreshCatalog();                       // fetch the manifest
   void InstallAsync(const Pack& pack);         // download + verify + unpack
   void InstallFileAsync(const std::string& path); // offline: a local pack file
   void RemoveAsync(const std::string& id);
   void CancelInstall();                        // stops a running download
   void Poll();                                 // main thread, once per frame
   const State& GetState();
   void Shutdown();
}

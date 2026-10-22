#pragma once

#include <string>
#include <vector>

#include "core/Patch.h"

// --- Save with media ------------------------------------------------------
// Copies every file a patch points at into one folder next to it, and rewrites
// those params as paths relative to the patch, so the patch and its folder can
// move together (another machine, a zip, a USB stick) and still open.
//
// A param is a file reference when its key names one (path, *Path, *_path, *File,
// file, folder, pathB) and its value is a file or folder on disk. The key alone is
// not enough: Gesture keeps its recorded points in `s path`. Outputs (record and
// export targets) are never collected.
// The idea (collecting a patch's media) comes from Ricardo Palmieri's Infinite-Turbo
// fork; this is a fresh implementation.
namespace MediaCollect
{
   enum class State
   {
      Copy,       // will be copied to `dest`
      Duplicate,  // the same file as an earlier item; shares its copy
      Missing,    // the patch names it but it is not on disk; left as written
   };

   struct Item
   {
      std::string source;   // as the patch had it (absolute)
      std::string dest;     // relative to the patch folder, '/' separated ("<dir>/<name>")
      State state = State::Copy;
      bool isFolder = false;
      int node = -1;        // index into Data::nodes of the first user
   };

   // True for a param key ("s path", "s pad3_path", ...) that names a file to read.
   bool IsMediaKey(const std::string& paramKey);

   // A path that does not start at a root, a drive, a home (~) or a URL scheme.
   bool IsRelative(const std::string& path);

   // Plans the copy into `mediaDir` (a folder name, relative to the patch) and rewrites
   // the params of every item that will be copied to its relative `dest`. Missing items
   // keep their old value, so the patch still says where the file was.
   std::vector<Item> Plan(Patch::Data& data, const std::string& mediaDir);

   // Copies every Copy item from its source to `patchDir`/dest. Returns false at the
   // first failure, with `error` naming the file.
   bool CopyAll(const std::vector<Item>& items, const std::string& patchDir, int& copied, std::string& error);

   // On load: a relative media path that exists next to the patch becomes absolute, so
   // nodes (which never see the patch folder) open it. Anything else is left alone.
   void ResolveRelative(Patch::Data& data, const std::string& patchDir);

   // On save: a media path inside `patchDir`/`mediaDir` is written relative again, so a
   // patch saved with its media stays movable after the next plain Save.
   void MakeRelative(Patch::Data& data, const std::string& patchDir, const std::string& mediaDir);

   // "<patch name> media" for ".../<patch name>.inf".
   std::string MediaDirFor(const std::string& patchPath);
}

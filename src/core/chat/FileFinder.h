// FileFinder: media files on this computer whose name holds every word of an ask ("kick", "logo png", "rain loop"),
// newest first, so an agent can bring the user's own file in with `infinite import` without a shell.
#pragma once
#include <string>
#include <vector>

namespace chat
{
   struct FileHit
   {
      std::string path;
      std::string kind; // audio, image, video, model, midi, patch
   };

   // Searches `roots` (missing ones skipped) a few folders deep. `kind` "" takes every kind. Bounded: at most
   // `maxVisits` entries are looked at, so a huge home folder answers in well under a second.
   std::vector<FileHit> FindFiles(const std::string& ask, const std::vector<std::string>& roots, const std::string& kind,
                                  size_t limit = 15, size_t maxVisits = 80000);
}

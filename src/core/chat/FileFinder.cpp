#include "core/chat/FileFinder.h"
#include "core/chat/ChatFiles.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace chat
{
   namespace
   {
      std::string Lower(std::string s)
      {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      }

      std::vector<std::string> Words(const std::string& text)
      {
         std::vector<std::string> out;
         std::string w;
         for (size_t i = 0; i <= text.size(); i++)
         {
            const unsigned char c = i < text.size() ? (unsigned char)text[i] : ' ';
            if (std::isalnum(c))
               w += (char)std::tolower(c);
            else if (!w.empty())
            {
               out.push_back(w);
               w.clear();
            }
         }
         return out;
      }

      // Folders that are whole apps, libraries or code trees: never where a user keeps a sample.
      bool SkipDir(const std::string& name)
      {
         if (name.empty() || name[0] == '.')
            return true;
         static const char* const kSkip[] = { "node_modules", "Library", "AppData", "build", "build-dev", "__pycache__", "venv" };
         for (const char* s : kSkip)
            if (name == s)
               return true;
         const size_t dot = name.rfind('.');
         if (dot == std::string::npos)
            return false;
         const std::string ext = Lower(name.substr(dot + 1));
         return ext == "app" || ext == "photoslibrary" || ext == "musiclibrary" || ext == "logicx" || ext == "band" ||
                ext == "framework" || ext == "bundle" || ext == "component" || ext == "vst3";
      }
   }

   std::vector<FileHit> FindFiles(const std::string& ask, const std::vector<std::string>& roots, const std::string& kind,
                                  size_t limit, size_t maxVisits)
   {
      // Kind words in the ask pick the kind and are not part of the name: "a kick sample", "the logo image".
      static const std::pair<const char*, const char*> kSay[] = {
         { "sample", "audio" }, { "samples", "audio" }, { "sound", "audio" }, { "audio", "audio" }, { "song", "audio" },
         { "image", "image" }, { "picture", "image" }, { "photo", "image" }, { "video", "video" }, { "clip", "video" },
         { "movie", "video" }, { "model", "model" }, { "mesh", "model" }, { "midi", "midi" }, { "patch", "patch" },
      };
      static const char* const kFiller[] = { "my", "the", "a", "an", "file", "files", "from", "on", "in", "of", "called", "named", "some", "that", "this" };
      std::string want = kind;
      std::vector<std::string> words;
      for (const std::string& w : Words(ask))
      {
         bool said = false;
         for (const auto& k : kSay)
            if (w == k.first)
            {
               if (want.empty())
                  want = k.second;
               said = true;
            }
         if (!said && std::find(std::begin(kFiller), std::end(kFiller), w) == std::end(kFiller))
            words.push_back(w);
      }

      struct Found
      {
         FileHit hit;
         fs::file_time_type when;
      };
      std::vector<Found> found;
      size_t visits = 0;
      for (const std::string& root : roots)
      {
         std::error_code ec;
         if (!fs::is_directory(fs::u8path(root), ec))
            continue;
         fs::recursive_directory_iterator it(fs::u8path(root), fs::directory_options::skip_permission_denied, ec), end;
         for (; !ec && it != end && visits < maxVisits; it.increment(ec))
         {
            visits++;
            const std::string name = it->path().filename().u8string();
            if (it->is_directory(ec))
            {
               if (SkipDir(name) || it.depth() >= 5)
                  it.disable_recursion_pending();
               continue;
            }
            const std::string k = FileKind(name);
            if (k.empty() || (want.empty() ? (k == "text" || k == "field") : k != want))
               continue;
            // Every word in the file name or its folder's name: "drums kick" finds drums/Kick 01.wav.
            const std::string hay = Lower(it->path().parent_path().filename().u8string() + " " + name);
            if (!std::all_of(words.begin(), words.end(), [&](const std::string& w) { return hay.find(w) != std::string::npos; }))
               continue;
            found.push_back({ { it->path().u8string(), k }, it->last_write_time(ec) });
         }
      }
      std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.when > b.when; });
      std::vector<FileHit> out;
      for (size_t i = 0; i < found.size() && i < limit; i++)
         out.push_back(found[i].hit);
      return out;
   }
}

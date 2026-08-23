#include "SampleScanner.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "crude_json.h"
#include "audio/MediaExtensions.h"
#include "platform/AppPaths.h"

namespace
{
   namespace fs = std::filesystem;

   // Mirrors main.cpp's settings-dir setup: one per-user directory. The
   // INFINITE_*DRAGTEST overrides below route test runs away from the user's
   // real index files - see the comment on them.
   std::string SettingsDir()
   {
      std::string dir = AppPaths::AppSupportDir();
      if (dir.empty())
         return std::string();
      // Mirrors main.cpp's INFINITE_DRAGTEST throwaway-settings-file pattern:
      // INFINITE_SAMPLERDRAGTEST/INFINITE_MEDIADRAGTEST drive real
      // AddFolder/RemoveFolder/StartScan calls against whatever this resolves
      // to, and without this override they were doing that against the
      // user's actual SampleFolders.json/SampleIndex.json or
      // MediaFolders.json/MediaIndex.json - wiping their real library
      // folders on every hygiene run. Route each to its own throwaway
      // subdirectory instead (kept separate so the two tests can't see each
      // other's index).
      if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr)
         dir += "/sampler_drag_test";
      else if (getenv("INFINITE_MEDIADRAGTEST") != nullptr)
         dir += "/media_drag_test";
      AppPaths::EnsureDir(dir); // portable mkdir; no-op when it already exists
      return dir;
   }

   std::string FoldersPath(SampleScanner::Kind kind)
   {
      const std::string dir = SettingsDir();
      if (dir.empty())
         return std::string();
      return dir + (kind == SampleScanner::Kind::Media ? "/MediaFolders.json" : "/SampleFolders.json");
   }

   std::string IndexPath(SampleScanner::Kind kind)
   {
      const std::string dir = SettingsDir();
      if (dir.empty())
         return std::string();
      return dir + (kind == SampleScanner::Kind::Media ? "/MediaIndex.json" : "/SampleIndex.json");
   }

   std::string ToLower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }

   // Discoverable, not a decode guarantee - AVAudioFile (the only decoder
   // Platform::DecodeAudioFileToBuffer uses) reads most of these reliably,
   // but FLAC support varies by macOS version. A file that lists here but
   // fails to decode later surfaces through SamplerNode::Status() rather
   // than being silently excluded from the index.
   bool HasAudioExtension(const fs::path& p)
   {
      static const char* kExts[] = { ".wav", ".aif", ".aiff", ".caf", ".m4a", ".mp3", ".flac" };
      const std::string ext = ToLower(p.extension().string());
      for (const char* e : kExts)
         if (ext == e)
            return true;
      return false;
   }

   // Same "discoverable, not a decode guarantee" caveat as HasAudioExtension
   // above - a listed file that fails to load surfaces as the node's own
   // error (ImageSourceNode/VideoSourceNode status), it isn't excluded from
   // the index. Lists come from MediaExtensions.h so the scanner classifies
   // a path identically to the OS drop handler and drag-resolution logic.
   bool HasMediaExtension(const fs::path& p)
   {
      const std::string ext = ToLower(p.extension().string());
      if (ext.empty() || ext[0] != '.')
         return false;
      const std::string bare = ext.substr(1);
      const auto& video = MediaExtensions::Video();
      const auto& image = MediaExtensions::Image();
      return std::find(video.begin(), video.end(), bare) != video.end() ||
             std::find(image.begin(), image.end(), bare) != image.end();
   }

   bool HasExtensionForKind(SampleScanner::Kind kind, const fs::path& p)
   {
      return kind == SampleScanner::Kind::Media ? HasMediaExtension(p) : HasAudioExtension(p);
   }
}

SampleScanner::SampleScanner(Kind kind) : mKind(kind) {}

SampleScanner::~SampleScanner()
{
   if (mScanThread.joinable())
      mScanThread.join();
}

void SampleScanner::AddFolder(const std::string& path)
{
   if (std::find(mFolders.begin(), mFolders.end(), path) != mFolders.end())
      return;
   mFolders.push_back(path);
   SaveFoldersToDisk();
}

void SampleScanner::RemoveFolder(const std::string& path)
{
   mFolders.erase(std::remove(mFolders.begin(), mFolders.end(), path), mFolders.end());
   SaveFoldersToDisk();
   ++mIndexVersion;
   // Entries from the removed folder stay in the index until the next
   // Refresh - matching "manual refresh only" (no implicit rescan on every
   // folder-list edit), same as the plan's Refresh-button-only contract.
}

void SampleScanner::StartScan(const std::string& folder)
{
   if (mScanning.exchange(true, std::memory_order_relaxed))
      return; // already in flight

   if (mScanThread.joinable())
      mScanThread.join(); // previous scan already finished; reap it before starting a new one

   mFilesFound.store(0, std::memory_order_relaxed);
   mScanningFolders = folder.empty() ? mFolders : std::vector<std::string> { folder };
   mScanThread = std::thread(&SampleScanner::ScanThreadMain, this, mScanningFolders);
}

void SampleScanner::ScanThreadMain(std::vector<std::string> folders)
{
   std::vector<Entry> found;

   for (const std::string& root : folders)
   {
      // Manual stack-based walk rather than recursive_directory_iterator:
      // per LWG2723, libc++ sends the iterator straight to end() the moment
      // increment() reports *any* error (not just permission-denied), which
      // on exFAT/removable volumes (I/O quirks, odd names, broken symlinks)
      // silently aborted the *entire* subtree scan after the first bad
      // entry - explaining large undercounts on big external drives. Here a
      // bad entry only ends that one directory's remaining siblings; every
      // other directory already queued on the stack still gets scanned.
      std::vector<fs::path> dirStack;
      dirStack.push_back(root);

      while (!dirStack.empty())
      {
         fs::path dir = std::move(dirStack.back());
         dirStack.pop_back();

         std::error_code ec;
         fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         const fs::directory_iterator end;
         if (ec)
            continue; // couldn't open this one directory; skip it, keep draining the stack

         while (it != end)
         {
            const fs::directory_entry entry = *it;
            std::error_code entryEc;
            const std::string name = entry.path().filename().string();
            const bool isHidden = (!name.empty() && name[0] == '.');
            const bool isSymlink = entry.is_symlink(entryEc) && !entryEc;

            if (!isHidden && !isSymlink && entry.is_directory(entryEc) && !entryEc)
            {
               const std::string ext = ToLower(entry.path().extension().string());
               if (ext != ".app" && ext != ".framework" && ext != ".plugin" && ext != ".bundle")
               {
                  dirStack.push_back(entry.path());
               }
            }
            else if (!isHidden && entry.is_regular_file(entryEc) && !entryEc && HasExtensionForKind(mKind, entry.path()))
            {
               Entry e;
               e.path = entry.path().string();
               e.fileName = name;
               e.fileNameLower = ToLower(name);
               e.folderRoot = root;
               found.push_back(std::move(e));
               mFilesFound.fetch_add(1, std::memory_order_relaxed);
            }

            it.increment(ec);
            if (ec)
               break; // this directory level ends here; sibling dirs on the stack are unaffected
         }
      }
   }

   {
      std::lock_guard<std::mutex> lock(mResultMutex);
      mPendingResult = std::move(found);
   }
   mResultReady.store(true, std::memory_order_release);
   mScanning.store(false, std::memory_order_relaxed);
}

void SampleScanner::PollResults()
{
   if (!mResultReady.load(std::memory_order_acquire))
      return;

   std::unique_lock<std::mutex> lock(mResultMutex, std::try_to_lock);
   if (!lock.owns_lock())
      return; // worker thread mid-write to mPendingResult; try again next frame

   // Replace only the entries that came from a folder this scan covered;
   // a single-folder Refresh must leave every other folder's index alone
   // rather than wiping the whole thing down to just what it found.
   std::vector<Entry> merged;
   merged.reserve(mIndex.size() + mPendingResult.size());
   for (Entry& e : mIndex)
      if (std::find(mScanningFolders.begin(), mScanningFolders.end(), e.folderRoot) == mScanningFolders.end())
         merged.push_back(std::move(e));
   for (Entry& e : mPendingResult)
      merged.push_back(std::move(e));

   mIndex = std::move(merged);
   mPendingResult.clear();
   mResultReady.store(false, std::memory_order_relaxed);
   ++mIndexVersion;
   lock.unlock();

   SaveIndexToDisk();
}

void SampleScanner::LoadFromDisk()
{
   const std::string foldersPath = FoldersPath(mKind);
   if (!foldersPath.empty())
   {
      auto [json, ok] = crude_json::value::load(foldersPath);
      if (ok && json.is_array())
      {
         for (const crude_json::value& v : json.get<crude_json::array>())
            if (v.is_string())
               mFolders.push_back(v.get<crude_json::string>());
      }
   }

   const std::string indexPath = IndexPath(mKind);
   if (!indexPath.empty())
   {
      auto [json, ok] = crude_json::value::load(indexPath);
      if (ok && json.is_array())
      {
         for (const crude_json::value& v : json.get<crude_json::array>())
         {
            if (!v.is_object())
               continue;
            Entry e;
            if (v["path"].is_string())
               e.path = v["path"].get<crude_json::string>();
            if (v["fileName"].is_string())
            {
               e.fileName = v["fileName"].get<crude_json::string>();
               e.fileNameLower = ToLower(e.fileName);
            }
            if (v["folderRoot"].is_string())
               e.folderRoot = v["folderRoot"].get<crude_json::string>();
            if (!e.path.empty())
               mIndex.push_back(std::move(e));
         }
      }
   }
   ++mIndexVersion;
}

void SampleScanner::SaveFoldersToDisk() const
{
   const std::string path = FoldersPath(mKind);
   if (path.empty())
      return;

   crude_json::value json = crude_json::array{};
   for (const std::string& f : mFolders)
      json.push_back(crude_json::value(f));
   json.save(path, 2);
}

void SampleScanner::SaveIndexToDisk() const
{
   const std::string path = IndexPath(mKind);
   if (path.empty())
      return;

   crude_json::value json = crude_json::array{};
   for (const Entry& e : mIndex)
   {
      crude_json::value entry = crude_json::object{};
      entry["path"] = e.path;
      entry["fileName"] = e.fileName;
      entry["folderRoot"] = e.folderRoot;
      json.push_back(std::move(entry));
   }
   json.save(path, 2);
}

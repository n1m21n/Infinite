#include "SampleScanner.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "crude_json.h"
#include "audio/MediaExtensions.h"
#include "platform/SettingsPaths.h"

namespace
{
   namespace fs = std::filesystem;

   // Mirrors Patch.cpp's RecentsPath / main.cpp's settings-dir setup: same
   // directory, same "getenv(HOME) or give up" fallback. Duplicated rather
   // than shared because Patch.cpp's helper isn't exposed outside main.cpp's
   // translation unit, and this is three lines.
   std::string SettingsDir()
   {
      std::string dir = InfiniteSettingsDirectory();
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
      std::error_code error;
      fs::create_directories(fs::u8path(dir), error);
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
      static const char* kExts[] = { ".wav", ".aif", ".aiff", ".caf", ".m4a", ".mp3", ".flac", ".ogg", ".wave" };
      const std::string ext = ToLower(p.extension().u8string());
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
      const std::string ext = ToLower(p.extension().u8string());
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

std::string SampleScanner::PathKey(const std::string& path)
{
   std::string k = path;
   for (char& c : k)
   {
      if (c == '\\')
         c = '/';
#ifdef _WIN32
      else if (c >= 'A' && c <= 'Z')
         c = (char)(c - 'A' + 'a');
#endif
   }
   // Keep "/" and "c:/" as they are; strip any other trailing separator.
   while (k.size() > 1 && k.back() == '/' && !(k.size() == 3 && k[1] == ':'))
      k.pop_back();
   return k;
}

std::string SampleScanner::NormalizeFolderSpelling(const std::string& path)
{
   std::string s = path;
#ifdef _WIN32
   std::replace(s.begin(), s.end(), '/', '\\');
#endif
   while (s.size() > 1 && (s.back() == '/' || s.back() == '\\') && !(s.size() == 3 && s[1] == ':'))
      s.pop_back();
   return s;
}

std::string SampleScanner::CanonicalFolder(const std::string& path)
{
   // Turbo 0.49 review: absolute, not canonical. canonical/weakly_canonical
   // resolve a mapped or subst drive to its UNC / target spelling, which
   // would no longer match the user's paths (or older saved roots); link
   // and alias detection uses RootIdentity instead.
   try
   {
      std::error_code ec;
      fs::path p = fs::absolute(fs::u8path(path), ec);
      if (ec)
         p = fs::u8path(path);
      p = p.lexically_normal();
      return NormalizeFolderSpelling(p.u8string());
   }
   catch (...)
   {
      return NormalizeFolderSpelling(path);
   }
}

std::string SampleScanner::RootIdentity(const std::string& path)
{
   try
   {
      std::error_code ec;
      const fs::path c = fs::canonical(fs::u8path(path), ec);
      if (!ec)
         return PathKey(c.u8string());
   }
   catch (...)
   {
   }
   return PathKey(path);
}

namespace
{
   // Canonical key of an existing directory (follows links and junctions),
   // falling back to the plain key when the OS cannot resolve it.
   std::string CanonicalDirKey(const fs::path& dir)
   {
      try
      {
         std::error_code ec;
         const fs::path c = fs::canonical(dir, ec);
         return SampleScanner::PathKey(ec ? dir.u8string() : c.u8string());
      }
      catch (...)
      {
         // Unconvertible name: a key no other directory can share, so the
         // dedupe sets never merge two such directories.
         static std::atomic<unsigned> sUnique { 0 };
         return "?" + std::to_string(sUnique.fetch_add(1, std::memory_order_relaxed));
      }
   }
}

SampleScanner::SampleScanner(Kind kind) : mKind(kind) {}

SampleScanner::~SampleScanner()
{
   mAbort.store(true, std::memory_order_relaxed);
   if (mScanThread.joinable())
      mScanThread.join();
}

int SampleScanner::FolderIndex(const std::string& path) const
{
   const std::string key = PathKey(path);
   for (size_t i = 0; i < mFolders.size(); i++)
      if (PathKey(mFolders[i]) == key)
         return (int)i;
   return -1;
}

int SampleScanner::SameRootIndex(const std::string& path) const
{
   const int idx = FolderIndex(path);
   if (idx >= 0)
      return idx;
   // Same folder under another spelling (mapped drive vs UNC, subst, link):
   // compare resolved identities, but keep each root's stored spelling.
   const std::string id = RootIdentity(path);
   for (size_t i = 0; i < mFolders.size(); i++)
      if (RootIdentity(mFolders[i]) == id)
         return (int)i;
   return -1;
}

void SampleScanner::AddFolder(const std::string& path, const std::string& label)
{
   if (path.empty())
      return;
   const std::string canon = CanonicalFolder(path);
   if (SameRootIndex(canon) >= 0)
      return;
   mFolders.push_back(canon);
   mLabels.push_back(label);
   SaveFoldersToDisk();
   StartScan();
}

void SampleScanner::RemoveFolder(const std::string& path)
{
   const int idx = FolderIndex(path);
   if (idx < 0)
      return;
   const std::string removed = mFolders[(size_t)idx];
   mFolders.erase(mFolders.begin() + idx);
   mLabels.erase(mLabels.begin() + idx);
   SaveFoldersToDisk();
   // Turbo 0.49: the removed root's files leave the list at once (they used
   // to linger until the next refresh), and a full rescan follows because a
   // root nested inside the removed one now owns its files again.
   mIndex.erase(std::remove_if(mIndex.begin(), mIndex.end(),
                               [&removed](const Entry& e) { return e.folderRoot == removed; }),
                mIndex.end());
   ++mIndexVersion;
   if (!mFolders.empty())
      StartScan();
   else
      SaveIndexToDisk();
}

void SampleScanner::SetFolderLabel(const std::string& path, const std::string& label)
{
   const int idx = FolderIndex(path);
   if (idx < 0 || mLabels[(size_t)idx] == label)
      return;
   mLabels[(size_t)idx] = label;
   SaveFoldersToDisk();
   ++mIndexVersion; // the panel's folder tree shows labels
}

std::string SampleScanner::FolderLabel(const std::string& path) const
{
   const int idx = FolderIndex(path);
   return idx < 0 ? std::string() : mLabels[(size_t)idx];
}

std::string SampleScanner::FolderDisplayName(const std::string& path) const
{
   const std::string label = FolderLabel(path);
   if (!label.empty())
      return label;
   const std::string key = PathKey(path);
   const size_t slash = key.find_last_of('/');
   if (slash == std::string::npos || slash + 1 >= key.size())
      return path;
   // Same offset in the original spelling (PathKey keeps the length apart
   // from trailing separators, which CanonicalFolder already removed).
   return path.size() >= key.size() ? path.substr(slash + 1, key.size() - slash - 1) : key.substr(slash + 1);
}

void SampleScanner::SeedDefaultFolders(const std::vector<std::pair<std::string, std::string>>& pathAndLabel)
{
   if (mDefaultsSeeded)
      return;
   mDefaultsSeeded = true;
   std::vector<std::string> added;
   for (const auto& [path, label] : pathAndLabel)
   {
      if (path.empty())
         continue;
      const std::string canon = CanonicalFolder(path);
      if (SameRootIndex(canon) >= 0)
         continue;
      mFolders.push_back(canon);
      mLabels.push_back(label);
      added.push_back(canon);
   }
   SaveFoldersToDisk();
   // Turbo 0.49 review: scan only the new roots, never the whole library at
   // launch (the other roots keep their cached index).
   if (!added.empty())
      StartScanRoots(added);
}

void SampleScanner::StartScan(const std::string& folder)
{
   StartScanRoots(folder.empty() ? mFolders : std::vector<std::string> { folder });
}

void SampleScanner::StartScanRoots(const std::vector<std::string>& roots)
{
   // mScanning stays true from here until PollResults has picked the result
   // up (the worker never clears it), so a scan finished but not yet merged
   // still counts as in flight and its result can't be attributed to a newer
   // scan. A request meanwhile is queued as a full rescan.
   if (mScanning.load(std::memory_order_acquire))
   {
      mRescanQueued = true;
      return;
   }
   if (mFolders.empty() || roots.empty())
      return;

   if (mScanThread.joinable())
      mScanThread.join(); // previous scan already finished; reap it before starting a new one

   mFilesFound.store(0, std::memory_order_relaxed);
   mResultReady.store(false, std::memory_order_relaxed);
   mScanningFolders = roots;
   mScanning.store(true, std::memory_order_release);
   try
   {
      mScanThread = std::thread(&SampleScanner::ScanThreadMain, this, mScanningFolders, mFolders);
   }
   catch (...)
   {
      mScanning.store(false, std::memory_order_release); // no thread: never leave IsScanning stuck
   }
}

void SampleScanner::ScanThreadMain(std::vector<std::string> folders, std::vector<std::string> allRoots)
{
   // Turbo 0.49 review: nothing may escape this thread (std::terminate). A
   // failed scan publishes "failed" and PollResults keeps the old index.
   std::vector<Entry> found;
   bool failed = false;
   try
   {
      ScanFolders(folders, allRoots, found);
   }
   catch (...)
   {
      failed = true;
   }
   try
   {
      std::lock_guard<std::mutex> lock(mResultMutex);
      mPendingResult = failed ? std::vector<Entry>() : std::move(found);
      mPendingFailed = failed;
   }
   catch (...)
   {
      // lock_guard on a std::mutex can only fail on a broken mutex; publish anyway.
      mPendingFailed = true;
   }
   mResultReady.store(true, std::memory_order_release);
}

void SampleScanner::ScanFolders(const std::vector<std::string>& folders, const std::vector<std::string>& allRoots,
                                std::vector<Entry>& found)
{
   // Turbo 0.49 dedupe: every root's canonical key (so a nested root is
   // walked only as itself), every directory walked so far (symlink /
   // junction loops and two links to one folder), and every file key.
   std::unordered_set<std::string> rootKeys;
   for (const std::string& r : allRoots)
   {
      try
      {
         rootKeys.insert(CanonicalDirKey(fs::u8path(r)));
      }
      catch (const std::bad_alloc&)
      {
         throw;
      }
      catch (...)
      {
         rootKeys.insert(PathKey(r)); // u8path rejected the string: compare by spelling
      }
   }
   std::unordered_set<std::string> visitedDirs;
   std::unordered_set<std::string> seenFiles;

   // Manual stack-based walk rather than recursive_directory_iterator:
   // per LWG2723, libc++ sends the iterator straight to end() the moment
   // increment() reports *any* error (not just permission-denied), which
   // on exFAT/removable volumes (I/O quirks, odd names, broken symlinks)
   // silently aborted the *entire* subtree scan after the first bad
   // entry - explaining large undercounts on big external drives. Here a
   // bad entry only ends that one directory's remaining siblings; every
   // other directory already queued on the stack still gets scanned.
   // Each item carries its root and its folder relative to that root.
   // Directories reached through a symlink wait in linkStack until every
   // real directory of every root is done, so a file reachable both ways
   // is listed under its real folder.
   struct WalkItem
   {
      fs::path dir;
      std::string rel;
      size_t root;
   };
   std::vector<WalkItem> dirStack, linkStack;
   for (size_t r = folders.size(); r-- > 0;)
   {
      try
      {
         dirStack.push_back({ fs::u8path(folders[r]), std::string(), r });
      }
      catch (const std::bad_alloc&)
      {
         throw;
      }
      catch (...)
      {
         // A root string u8path rejects (corrupt settings) is skipped.
      }
   }

   while ((!dirStack.empty() || !linkStack.empty()) && !mAbort.load(std::memory_order_relaxed))
   {
      std::vector<WalkItem>& from = dirStack.empty() ? linkStack : dirStack;
      WalkItem item = std::move(from.back());
      from.pop_back();
      const fs::path& dir = item.dir;
      const std::string& rel = item.rel;
      const std::string& root = folders[item.root];

      const std::string dirKey = CanonicalDirKey(dir);
      if (!rel.empty() && rootKeys.count(dirKey) > 0)
         continue; // another library root: listed under that root only
      if (!visitedDirs.insert(dirKey).second)
         continue; // reached before (loop, or a second link to it)

      std::error_code ec;
      fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
      const fs::directory_iterator end;
      if (ec)
         continue; // couldn't open this one directory; skip it, keep draining the stack

      while (it != end && !mAbort.load(std::memory_order_relaxed))
      {
         try
         {
            const fs::directory_entry entry = *it;
            std::error_code entryEc;
            const std::string name = entry.path().filename().u8string();
            const bool isHidden = (!name.empty() && name[0] == '.');

            // is_directory follows symlinks and junctions; visitedDirs above
            // is what keeps a link loop from recursing forever.
            if (!isHidden && entry.is_directory(entryEc) && !entryEc)
            {
               const std::string ext = ToLower(entry.path().extension().u8string());
               if (ext != ".app" && ext != ".framework" && ext != ".plugin" && ext != ".bundle")
               {
                  std::error_code linkEc;
                  const bool viaLink = entry.is_symlink(linkEc) && !linkEc;
                  (viaLink ? linkStack : dirStack)
                     .push_back({ entry.path(), rel.empty() ? name : rel + "/" + name, item.root });
               }
            }
            else if (!isHidden && entry.is_regular_file(entryEc) && !entryEc && HasExtensionForKind(mKind, entry.path()))
            {
               if (seenFiles.insert(dirKey + "/" + PathKey(name)).second)
               {
                  Entry e;
                  e.path = entry.path().u8string();
                  e.fileName = name;
                  e.fileNameLower = ToLower(name);
                  e.folderRoot = root;
                  e.relDir = rel;
                  found.push_back(std::move(e));
                  mFilesFound.fetch_add(1, std::memory_order_relaxed);
               }
            }
         }
         catch (const std::bad_alloc&)
         {
            throw;
         }
         catch (...)
         {
            // One unconvertible name (say, an unpaired UTF-16 surrogate) skips
            // that entry only.
         }

         it.increment(ec);
         if (ec)
            break; // this directory level ends here; sibling dirs on the stack are unaffected
      }
   }
}

void SampleScanner::PollResults()
{
   if (!mResultReady.load(std::memory_order_acquire))
      return;

   std::unique_lock<std::mutex> lock(mResultMutex, std::try_to_lock);
   if (!lock.owns_lock())
      return; // worker thread mid-write to mPendingResult; try again next frame

   const bool failed = mPendingFailed;
   const bool fullScan = mScanningFolders == mFolders;
   if (failed)
   {
      // Keep the existing index; nothing to merge.
   }
   else if (fullScan)
   {
      // Common case: the scan thread already deduped and filled relDir.
      mIndex = std::move(mPendingResult);
   }
   else
   {
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
   }
   mPendingResult.clear();
   mPendingFailed = false;
   mResultReady.store(false, std::memory_order_relaxed);
   lock.unlock();

   // The worker has published its last write; reap it and only then mark the
   // scan as over, so a queued rescan below always starts.
   if (mScanThread.joinable())
      mScanThread.join();
   mScanning.store(false, std::memory_order_release);

   // Roots may have changed while the scan ran: drop what no longer
   // belongs (cheap string compares unless it is a partial merge).
   if (fullScan && !failed)
      mIndex.erase(std::remove_if(mIndex.begin(), mIndex.end(),
                                  [this](const Entry& e) {
                                     return std::find(mFolders.begin(), mFolders.end(), e.folderRoot) == mFolders.end();
                                  }),
                   mIndex.end());
   else if (!failed)
      SanitizeIndex();
   ++mIndexVersion;

   SaveIndexToDisk();

   if (mRescanQueued)
   {
      mRescanQueued = false;
      StartScan();
   }
}

void SampleScanner::SanitizeIndex()
{
   // Each entry belongs to the deepest root whose key prefixes its path
   // (a nested root owns its files), and a path key appears once.
   std::vector<std::string> rootKeys;
   for (const std::string& f : mFolders)
      rootKeys.push_back(PathKey(f));
   std::unordered_set<std::string> seen;
   std::vector<Entry> kept;
   kept.reserve(mIndex.size());
   for (Entry& e : mIndex)
   {
      const std::string pk = PathKey(e.path);
      int best = -1;
      size_t bestLen = 0;
      for (size_t i = 0; i < rootKeys.size(); i++)
      {
         const std::string& rk = rootKeys[i];
         const size_t prefixLen = (!rk.empty() && rk.back() == '/') ? rk.size() : rk.size() + 1;
         if (pk.size() > prefixLen && pk.compare(0, rk.size(), rk) == 0 &&
             (rk.back() == '/' || pk[rk.size()] == '/') && rk.size() >= bestLen)
         {
            best = (int)i;
            bestLen = rk.size();
         }
      }
      if (best < 0)
      {
         // Turbo 0.49 review: an entry whose path no longer prefixes any root
         // spelling (saved under an older spelling of the same root) stays,
         // attached to its saved root when that root is still listed.
         const std::string frk = PathKey(e.folderRoot);
         for (size_t i = 0; i < rootKeys.size() && best < 0; i++)
            if (rootKeys[i] == frk)
               best = (int)i;
         if (best < 0 || !seen.insert(pk).second)
            continue;
         e.folderRoot = mFolders[(size_t)best];
         if (e.relDir.empty())
         {
            // Best effort: the part of the path below the saved root spelling.
            const std::string fk = frk.empty() || frk.back() == '/' ? frk : frk + "/";
            if (!fk.empty() && pk.size() > fk.size() && pk.compare(0, fk.size(), fk) == 0)
            {
               // Same offsets in the original spelling when the lengths agree.
               std::string rel = e.path.size() == pk.size() ? e.path.substr(fk.size()) : pk.substr(fk.size());
               std::replace(rel.begin(), rel.end(), '\\', '/');
               const size_t sl = rel.find_last_of('/');
               if (sl != std::string::npos)
                  e.relDir = rel.substr(0, sl);
            }
         }
         kept.push_back(std::move(e));
         continue;
      }
      if (!seen.insert(pk).second)
         continue;
      const std::string& rk = rootKeys[(size_t)best];
      const size_t relStart = rk.back() == '/' ? rk.size() : rk.size() + 1;
      // PathKey keeps the path's length (no trailing separator on a file),
      // so the offsets line up with the original spelling.
      std::string rel = e.path.size() == pk.size() ? e.path.substr(relStart) : pk.substr(relStart);
      for (char& c : rel)
         if (c == '\\')
            c = '/';
      const size_t slash = rel.find_last_of('/');
      e.relDir = slash == std::string::npos ? std::string() : rel.substr(0, slash);
      e.folderRoot = mFolders[(size_t)best];
      kept.push_back(std::move(e));
   }
   mIndex = std::move(kept);
}

void SampleScanner::LoadFromDisk()
{
   const std::string foldersPath = FoldersPath(mKind);
   if (!foldersPath.empty())
   {
      auto [json, ok] = crude_json::value::load(foldersPath);
      if (ok && json.is_array())
      {
         // Plain strings are roots (the pre-0.49 format, still written for
         // unlabelled roots); {path, label} objects are labelled roots; the
         // marker object records that the defaults were seeded. Older
         // builds skip non-strings, so the file stays readable by them.
         for (const crude_json::value& v : json.get<crude_json::array>())
         {
            std::string path, label;
            if (v.is_string())
               path = v.get<crude_json::string>();
            else if (v.is_object())
            {
               if (v.contains("turboDefaultsSeeded"))
                  mDefaultsSeeded = true;
               if (v.contains("path") && v["path"].is_string())
                  path = v["path"].get<crude_json::string>();
               if (v.contains("label") && v["label"].is_string())
                  label = v["label"].get<crude_json::string>();
            }
            if (path.empty())
               continue;
            // Turbo 0.49 review: separators and trailing separator only, never
            // canonicalised (a mapped / subst drive would turn into another
            // spelling and orphan its saved index entries). Duplicates by
            // PathKey only: no disk access at launch (a dead network drive).
            path = NormalizeFolderSpelling(path);
            if (FolderIndex(path) >= 0)
               continue;
            mFolders.push_back(path);
            mLabels.push_back(label);
         }
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
            if (v.contains("path") && v["path"].is_string())
               e.path = v["path"].get<crude_json::string>();
            if (v.contains("fileName") && v["fileName"].is_string())
            {
               e.fileName = v["fileName"].get<crude_json::string>();
               e.fileNameLower = ToLower(e.fileName);
            }
            if (v.contains("folderRoot") && v["folderRoot"].is_string())
               e.folderRoot = v["folderRoot"].get<crude_json::string>();
            if (!e.path.empty())
               mIndex.push_back(std::move(e));
         }
      }
   }
   // Turbo 0.49: indexes written by older builds could hold the same file
   // twice (nested roots, or a removed root's leftovers); clean on load.
   SanitizeIndex();
   ++mIndexVersion;
}

void SampleScanner::SaveFoldersToDisk() const
{
   const std::string path = FoldersPath(mKind);
   if (path.empty())
      return;

   crude_json::value json = crude_json::array{};
   for (size_t i = 0; i < mFolders.size(); i++)
   {
      if (mLabels[i].empty())
      {
         json.push_back(crude_json::value(mFolders[i]));
         continue;
      }
      crude_json::value root = crude_json::object{};
      root["path"] = mFolders[i];
      root["label"] = mLabels[i];
      json.push_back(std::move(root));
   }
   if (mDefaultsSeeded)
   {
      crude_json::value marker = crude_json::object{};
      marker["turboDefaultsSeeded"] = true;
      json.push_back(std::move(marker));
   }
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

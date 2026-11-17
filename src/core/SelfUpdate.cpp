#include "SelfUpdate.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "Extensions.h"
#include "UpdateCheck.h"
#include "miniz.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

#ifndef INFINITE_VERSION_STRING
#define INFINITE_VERSION_STRING "0.0.0"
#endif

namespace fs = std::filesystem;

namespace SelfUpdate
{
namespace
{
   std::mutex gMutex;
   Stage gStage = Stage::Idle;
   float gProgress = 0.0f;
   std::string gError;
   std::string gErrorCopy;   // handed out by reference on the main thread
   std::thread gWorker;
   std::atomic<bool> gCancel { false };

   void Set(Stage s, const std::string& err = std::string())
   {
      std::lock_guard<std::mutex> l(gMutex);
      gStage = s;
      gError = err;
   }

   std::string Version()
   {
      std::string v = UpdateCheck::ResultVersion();
      if (!v.empty() && (v[0] == 'v' || v[0] == 'V'))
         v.erase(0, 1);
      return v;
   }

   bool SafeEntryName(const std::string& n)
   {
      if (n.empty() || n[0] == '/' || n.find('\\') != std::string::npos || n.find(':') != std::string::npos)
         return false;
      for (size_t start = 0;;)
      {
         const size_t slash = n.find('/', start);
         if (n.compare(start, slash == std::string::npos ? std::string::npos : slash - start, "..") == 0)
            return false;
         if (slash == std::string::npos)
            return true;
         start = slash + 1;
      }
   }

   // Windows release zip: every entry under Infinite/. Unpacks to `dest`, returns dest/Infinite.
   [[maybe_unused]] bool Unzip(const std::string& zipPath, const std::string& dest, std::string& outDir, std::string& err)
   {
      mz_zip_archive zip;
      memset(&zip, 0, sizeof(zip));
      if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
      {
         err = "The download is not a zip.";
         return false;
      }
      struct Closer { mz_zip_archive* z; ~Closer() { mz_zip_reader_end(z); } } closer { &zip };
      std::error_code ec;
      fs::remove_all(AppPaths::FsPath(dest), ec);
      const mz_uint count = mz_zip_reader_get_num_files(&zip);
      for (mz_uint i = 0; i < count; i++)
      {
         mz_zip_archive_file_stat st;
         if (!mz_zip_reader_file_stat(&zip, i, &st) || !SafeEntryName(st.m_filename))
         {
            err = "The download has an unsafe file name.";
            return false;
         }
         const std::string rel = st.m_filename;
         const std::string target = dest + "/" + rel;   // miniz takes UTF-8 paths on every platform
         if (mz_zip_reader_is_file_a_directory(&zip, i))
         {
            fs::create_directories(AppPaths::FsPath(target), ec);
            continue;
         }
         fs::create_directories(AppPaths::FsPath(target).parent_path(), ec);
         if (!mz_zip_reader_extract_to_file(&zip, i, target.c_str(), 0))
         {
            err = "Could not unpack " + rel + ".";
            return false;
         }
      }
      outDir = dest + "/Infinite";
      return true;
   }

   void Join()
   {
      if (gWorker.joinable())
         gWorker.join();
   }
}

bool Available(std::string& whyNot)
{
   whyNot.clear();
   if (UpdateCheck::GetStatus() != UpdateCheck::Status::UpdateAvailable)
      whyNot = "No update found.";
   else if (UpdateCheck::AssetSha256().empty())
      whyNot = "This release has no download for this computer.";
   else
      Platform::CanSelfUpdate(whyNot);
   return whyNot.empty();
}

void Start()
{
   {
      std::lock_guard<std::mutex> l(gMutex);
      if (gStage == Stage::Downloading || gStage == Stage::Ready)
         return;
      gStage = Stage::Downloading;
      gProgress = 0.0f;
      gError.clear();
   }
   Join();
   gCancel.store(false);
   const std::string url = UpdateCheck::DownloadUrl(), sha = UpdateCheck::AssetSha256(), version = Version();
   gWorker = std::thread([url, sha, version]()
   {
      const std::string dir = AppPaths::AppSupportDir() + "/update";
      std::error_code ec;
      fs::remove_all(AppPaths::FsPath(dir), ec);
      if (!AppPaths::EnsureDir(dir))
         return Set(Stage::Failed, "Could not create " + dir + ".");
      const std::string name = AppPaths::FsPath(url).filename().u8string();
      const std::string file = dir + "/" + (name.empty() ? std::string("download") : name);
      std::string err;
      const bool got = Platform::HttpDownload(url, std::string("Infinite/") + INFINITE_VERSION_STRING, file,
         [](uint64_t done, uint64_t total)
         {
            std::lock_guard<std::mutex> l(gMutex);
            gProgress = total ? (float)((double)done / (double)total) : -1.0f;
            return !gCancel.load();
         }, err, 1800);
      if (!got)
         return Set(Stage::Failed, err == "cancelled" ? "Cancelled." : "Download failed: " + err);
      std::string body;
      {
         std::ifstream in(AppPaths::FsPath(file), std::ios::binary);
         body.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      }
      const bool match = Extensions::Sha256Hex(body.data(), body.size()) == sha;
      body.clear();
      body.shrink_to_fit();
      if (!match)
         return Set(Stage::Failed, "The download does not match the release. Nothing was changed.");
      std::string payload = file;
#if defined(_WIN32)
      if (!Unzip(file, dir + "/unpacked", payload, err))
         return Set(Stage::Failed, err);
#endif
      const bool staged = Platform::PrepareSelfUpdate(payload, version, err);
      // The download is spent either way: macOS and Linux copied it beside this copy; Windows keeps only the
      // unpacked folder, which the swap helper deletes.
      fs::remove(AppPaths::FsPath(file), ec);
      if (!staged)
         return Set(Stage::Failed, err);
      Set(Stage::Ready);
   });
}

Stage GetStage()
{
   std::lock_guard<std::mutex> l(gMutex);
   return gStage;
}

float Progress()
{
   std::lock_guard<std::mutex> l(gMutex);
   return gProgress;
}

const std::string& LastError()
{
   std::lock_guard<std::mutex> l(gMutex);
   gErrorCopy = gError;
   return gErrorCopy;
}

bool LaunchHelper(bool relaunch, std::string& outError)
{
   if (GetStage() != Stage::Ready)
   {
      outError = "No update is ready.";
      return false;
   }
   return Platform::LaunchSelfUpdateHelper(relaunch, outError);
}

void Shutdown()
{
   gCancel.store(true);
   Join();
}

bool StageLocal(const std::string& payload, const std::string& version, std::string& outError)
{
   if (!Platform::PrepareSelfUpdate(payload, version, outError))
      return false;
   Set(Stage::Ready);
   return true;
}
}

// Share patch: File > Share Patch... writes one .infshare file (see core/ShareBundle.h).
#include "app/AppShared.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "app/ui/design/components/ChipButton.h"
#include "core/Notices.h"
#include "core/ShareBundle.h"
#include "core/SplashScreen.h"

#ifdef _WIN32
#define environ _environ
#else
extern char** environ;
#endif

namespace app
{
   namespace
   {
      struct ShareJob
      {
         std::mutex mu;
         std::atomic<bool> running { false };
         std::atomic<bool> finished { false };
         bool ok = false;
         std::string message;   // result line, read once finished
         std::string outPath;
      };
      ShareJob gShareJob;

      std::string Quote(const std::string& s)
      {
         std::string q = "\"";
         for (const char c : s)
         {
            if (c == '"')
               q += "\\\"";
            else
               q += c;
         }
         return q + "\"";
      }

      // Runs the app's own headless mode on the cleaned copy; returns true on exit code 0.
      bool RunSelf(const std::vector<std::string>& args) { return std::system(SelfCommand(args).c_str()) == 0; }

      bool ReadFile(const std::string& path, std::string& out)
      {
         std::ifstream f(AppPaths::FsPath(path), std::ios::binary);
         if (!f)
            return false;
         std::ostringstream ss;
         ss << f.rdbuf();
         out = ss.str();
         return true;
      }

      // One bundle from a patch already written to `patchFile`; blocks until done.
      bool BuildBundleBlocking(const std::string& patchFile, const std::string& outPath, ShareBundle::Meta meta,
                               const std::string& workDir, std::string& message)
      {
         std::string text, error;
         bool ok = ReadFile(patchFile, text);
         message.clear();
         if (!ok)
            message = "could not read the saved copy of the patch";
         if (ok)
         {
            std::vector<std::string> blanked;
            const std::string cleaned = ShareBundle::CleanPatchText(text, &blanked);
            meta.blankedMedia = blanked;
            const std::string cleanedPath = workDir + "/patch.inf";
            std::ofstream(AppPaths::FsPath(cleanedPath), std::ios::binary) << cleaned;
            const std::string thumbPath = workDir + "/thumbnail.png";
            const std::string previewPath = workDir + "/preview.mp4";
            const bool haveThumb = RunSelf({ "--frame", cleanedPath, "1.0", thumbPath, "--lenient", "--timeout", "60" });
            const bool havePreview = RunSelf({ "--render", cleanedPath, previewPath, "--duration", "4", "--no-audio", "--lenient", "--timeout", "180" });
            std::vector<ShareBundle::Entry> entries;
            entries.push_back({ "patch.inf", cleaned });
            std::string bytes;
            if (haveThumb && ReadFile(thumbPath, bytes))
               entries.push_back({ "thumbnail.png", bytes });
            if (havePreview && ReadFile(previewPath, bytes))
               entries.push_back({ "preview.mp4", bytes });
            entries.push_back({ "share.json", ShareBundle::MetaJson(meta) });
            ok = ShareBundle::WriteZip(outPath, entries, error);
            if (!ok)
               message = error;
            else
            {
               message = "Saved " + outPath;
               if (!havePreview)
                  message += " (no preview video: the patch did not render headless)";
               if (!blanked.empty())
                  message += ". The patch needs " + std::to_string(blanked.size()) + " media file(s) from you.";
            }
         }
         std::error_code ec;
         std::filesystem::remove(AppPaths::FsPath(workDir + "/patch.inf"), ec);
         std::filesystem::remove(AppPaths::FsPath(workDir + "/thumbnail.png"), ec);
         std::filesystem::remove(AppPaths::FsPath(workDir + "/preview.mp4"), ec);
         return ok;
      }

      void BuildBundle(std::string patchFile, std::string outPath, ShareBundle::Meta meta, std::string workDir)
      {
         std::string message;
         const bool ok = BuildBundleBlocking(patchFile, outPath, meta, workDir, message);
         std::error_code ec;
         std::filesystem::remove(AppPaths::FsPath(patchFile), ec);
         {
            std::lock_guard<std::mutex> lock(gShareJob.mu);
            gShareJob.ok = ok;
            gShareJob.message = message;
            gShareJob.outPath = outPath;
         }
         gShareJob.finished = true;
         gShareJob.running = false;
      }
   }

   bool SelfChildKeepsEnv(const std::string& name)
   {
      if (name.compare(0, 9, "INFINITE_") != 0)
         return true;
      // Configuration a headless child needs to load the same patch the same way. Everything
      // else under INFINITE_ is a test, bench or harness switch (400+ of them), or a port the
      // parent already holds.
      static const char* kKeep[] = { "INFINITE_EXTENSIONS_DIR", "INFINITE_VST3_BROKEN_PLUGIN_DIR",
                                     "INFINITE_NO_UPDATE_CHECK", "INFINITE_NO_SINGLE_INSTANCE",
                                     "INFINITE_LANG", "INFINITE_AUDIO_BACKEND", "INFINITE_AUDIO_THREADS",
                                     "INFINITE_WAYLAND" };
      for (const char* k : kKeep)
         if (name == k)
            return true;
      return false;
   }

   std::string SelfCommand(const std::vector<std::string>& args)
   {
      // Unset in the child's shell, not in this process: other threads read the environment.
      std::vector<std::string> drop;
      for (char** e = environ; e != nullptr && *e != nullptr; e++)
      {
         const char* eq = strchr(*e, '=');
         const std::string name = eq ? std::string(*e, (size_t)(eq - *e)) : std::string(*e);
         if (!name.empty() && !SelfChildKeepsEnv(name))
            drop.push_back(name);
      }
#ifdef _WIN32
      std::string cmd;
      for (const std::string& n : drop)
         cmd += "set \"" + n + "=\"& ";
      cmd += Quote(Platform::ExecutablePath());
#else
      std::string cmd = drop.empty() ? std::string() : std::string("env");
      for (const std::string& n : drop)
         cmd += " -u " + n;
      if (!cmd.empty())
         cmd += " ";
      cmd += Quote(Platform::ExecutablePath());
#endif
      for (const std::string& a : args)
         cmd += " " + Quote(a);
#ifdef _WIN32
      cmd += " >NUL 2>&1";
      cmd = "\"" + cmd + "\"";
#else
      cmd += " >/dev/null 2>&1";
#endif
      return cmd;
   }

   bool BuildShareBundleBlocking(const std::string& patchFile, const std::string& outPath, const std::string& name,
                                 const std::string& workDir, std::string& message)
   {
      ShareBundle::Meta meta;
      meta.name = name;
      meta.appVersion = INFINITE_VERSION_STRING;
      AppPaths::EnsureDir(workDir);
      return BuildBundleBlocking(patchFile, outPath, meta, workDir, message);
   }

   SharePatchRequest gSharePatch;

   void SharePatchInteractive()
   {
      if (gShareJob.running)
         return;
      std::string name = "Untitled";
      if (!gPatchPath.empty())
      {
         const size_t slash = gPatchPath.find_last_of("/\\");
         name = slash == std::string::npos ? gPatchPath : gPatchPath.substr(slash + 1);
         const size_t dot = name.find_last_of('.');
         if (dot != std::string::npos && dot > 0)
            name = name.substr(0, dot);
      }
      snprintf(gSharePatch.nameBuf, sizeof(gSharePatch.nameBuf), "%s", name.c_str());
      gSharePatch.justOpened = true;
   }

   // Draws the name prompt and, once the job finishes, posts its result. Called every frame.
   void DrawSharePatchPopup()
   {
      if (gSharePatch.justOpened)
      {
         ImGui::OpenPopup("##sharepatch");
         gSharePatch.justOpened = false;
      }
      if (ImGui::BeginPopup("##sharepatch"))
      {
         ImGui::TextDisabled("%s", T("Share patch as:"));
         ImGui::SetNextItemWidth(260.0f);
         ImGui::InputText("##sharename", gSharePatch.nameBuf, sizeof(gSharePatch.nameBuf));
         ImGui::SetNextItemWidth(260.0f);
         ImGui::InputTextWithHint("##shareauthor", T("Author (optional)"), gSharePatch.authorBuf, sizeof(gSharePatch.authorBuf));
         ImGui::SetNextItemWidth(260.0f);
         ImGui::InputTextWithHint("##sharedesc", T("One line about it (optional)"), gSharePatch.descBuf, sizeof(gSharePatch.descBuf));
         ImGui::TextDisabled("%s", T("File paths are removed; media is not included."));
         if (ChipButton::Draw(L("Choose folder and share##sharepatchgo"), true, ImGui::GetFrameHeight()) && gSharePatch.nameBuf[0] != '\0')
         {
            const std::string dir = Platform::OpenFolderDialog("Where should the share bundle go?");
            if (!dir.empty())
            {
               ShareBundle::Meta meta;
               meta.name = gSharePatch.nameBuf;
               meta.author = gSharePatch.authorBuf;
               meta.description = gSharePatch.descBuf;
               meta.appVersion = INFINITE_VERSION_STRING;
               const std::string workDir = AppPaths::AppSupportDir() + "/ShareTmp";
               AppPaths::EnsureDir(workDir);
               const std::string patchFile = workDir + "/source.inf";
               Patch::Data data = BuildPatchData();
               std::string error;
               if (Patch::Write(patchFile, data, error))
               {
                  const std::string outPath = dir + "/" + ShareBundle::SafeName(meta.name) + ".infshare";
                  gShareJob.finished = false;
                  gShareJob.running = true;
                  std::thread(BuildBundle, patchFile, outPath, meta, workDir).detach();
                  gPatchStatus = "Building share bundle...";
               }
               else
                  Notices::Post(Notices::Level::Error, "patch.share", "Couldn't share the patch", error);
               ImGui::CloseCurrentPopup();
            }
         }
         ImGui::EndPopup();
      }

      if (gShareJob.finished.exchange(false))
      {
         std::lock_guard<std::mutex> lock(gShareJob.mu);
         gPatchStatus = gShareJob.ok ? "Shared" : "Share failed";
         Notices::Post(gShareJob.ok ? Notices::Level::Info : Notices::Level::Error, "patch.share",
                       gShareJob.ok ? "Share bundle ready" : "Couldn't share the patch", gShareJob.message);
      }
   }

   bool SharePatchBusy() { return gShareJob.running; }
}

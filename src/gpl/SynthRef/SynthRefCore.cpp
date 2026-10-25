// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
#include "SynthRefCore.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")

#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

const std::vector<SynthRefFirmware>& SynthRefFirmwares()
{
   static const std::vector<SynthRefFirmware> list = {
      { "smk37_open", "M-VAVE SMK-37 Pro", "SMK37 OPEN V01", "synthref_smk37_open.dll", SynthRefStatus::Supported,
        "M-VAVE's open firmware (GPL-3.0), the whole application built natively; bit-exact with its official simulator" },
      { "smk37_stock", "M-VAVE SMK-37 Pro", "Stock V016", nullptr, SynthRefStatus::Unavailable,
        "factory firmware for the device's JieLi chip; no open engine of it runs on a PC" },
      { "sloop_smk", "M-VAVE SMK-37 Pro", "SLOOP SMK 2.6 (SLOOP 2.8)", "synthref_sloop_smk.dll", SynthRefStatus::Experimental,
        "community GPL-3.0 firmware (Felucca for the SMK-37), its own engines, screen and panel; built without "
        "the all-rights-reserved Felucca artwork, so the knob-card icons are blank" },
      { "sloop_fm1", "M-VAVE FM-1", "SLOOP 2.8.1", nullptr, SynthRefStatus::Unavailable,
        "community GPL-3.0 firmware; its host preview has no screen, panel or preset loading yet" },
      { "fm1_open", "M-VAVE FM-1", "FM1 OPEN V14", "synthref_fm1_open.dll", SynthRefStatus::Supported,
        "M-VAVE's open FM-1 firmware (GPL-3.0), the whole application built natively" },
   };
   return list;
}

const SynthRefFirmware* SynthRefFindFirmware(const std::string& coreId)
{
   for (const SynthRefFirmware& f : SynthRefFirmwares())
      if (coreId == f.coreId)
         return &f;
   return nullptr;
}

namespace
{
   std::string WideToUtf8(const std::wstring& w)
   {
      if (w.empty())
         return {};
      const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
      std::string out((size_t)n, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
      return out;
   }

   fs::path ExeDir()
   {
      std::wstring buf(32768, L'\0');
      const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
      buf.resize(n);
      return fs::path(buf).parent_path();
   }

   // Copies of singleton cores live here, one per instance. A copy a crashed
   // session left behind is removed the next time a core opens (a copy still
   // loaded by another running Infinite cannot be deleted and is skipped).
   fs::path InstanceDir()
   {
      std::error_code ec;
      fs::path dir = fs::temp_directory_path(ec) / L"Infinite-Turbo" / L"synthref";
      fs::create_directories(dir, ec);
      return dir;
   }

   void SweepStaleCopies(const fs::path& dir)
   {
      static std::atomic<bool> swept { false };
      if (swept.exchange(true))
         return;
      std::error_code ec;
      for (const auto& e : fs::directory_iterator(dir, ec))
         if (e.path().extension() == L".dll")
            DeleteFileW(e.path().c_str());
   }
}

std::string SynthRefAppDir() { return (ExeDir() / L"synthref").u8string(); }

std::string SynthRefUserDir()
{
   PWSTR raw = nullptr;
   if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &raw)) || raw == nullptr)
      return {};
   fs::path dir = fs::path(raw) / L"Infinite-Turbo" / L"SynthRef";
   CoTaskMemFree(raw);
   std::error_code ec;
   fs::create_directories(dir, ec);
   return WideToUtf8(dir.wstring());
}

namespace
{
   std::wstring Utf8ToWide(const std::string& s)
   {
      if (s.empty())
         return {};
      const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
      std::wstring out((size_t)n, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
      return out;
   }

   std::string FileDialog(bool save, const wchar_t* title, const wchar_t* filter, const wchar_t* ext, const std::string& dir)
   {
      wchar_t file[MAX_PATH * 4] = {};
      const std::wstring initial = Utf8ToWide(dir);
      OPENFILENAMEW ofn = {};
      ofn.lStructSize = sizeof(ofn);
      ofn.hwndOwner = GetActiveWindow();
      ofn.lpstrFilter = filter;
      ofn.lpstrFile = file;
      ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
      ofn.lpstrTitle = title;
      ofn.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
      ofn.lpstrDefExt = ext;
      ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
      const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
      return ok ? WideToUtf8(file) : std::string();
   }
}

std::string SynthRefOpenFileDialog(const wchar_t* title, const wchar_t* filter, const std::string& dir)
{
   return FileDialog(false, title, filter, nullptr, dir);
}

std::string SynthRefSaveFileDialog(const wchar_t* title, const wchar_t* filter, const wchar_t* ext, const std::string& dir)
{
   return FileDialog(true, title, filter, ext, dir);
}

void SynthRefRevealFolder(const std::string& dir)
{
   if (!dir.empty())
      ShellExecuteW(nullptr, L"open", Utf8ToWide(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::unique_ptr<SynthRefCore> SynthRefCore::Open(const std::string& coreId, std::string& error)
{
   const SynthRefFirmware* fw = SynthRefFindFirmware(coreId);
   if (fw == nullptr)
   {
      error = "unknown firmware '" + coreId + "'";
      return nullptr;
   }
   if (fw->dll == nullptr || fw->status == SynthRefStatus::Unavailable)
   {
      error = std::string(fw->firmware) + " is unavailable: " + fw->note;
      return nullptr;
   }
   const fs::path original = ExeDir() / L"synthref" / L"cores" / fs::u8path(fw->dll);
   std::error_code ec;
   if (!fs::exists(original, ec))
   {
      error = "core file missing: " + original.u8string();
      return nullptr;
   }

   // Every core is loaded from a private copy first; whether the copy was
   // needed is only known after reading its info (SR_CORE_SINGLETON).
   static std::atomic<unsigned> counter { 0 };
   const fs::path dir = InstanceDir();
   SweepStaleCopies(dir);
   const fs::path copy = dir / (fs::u8path(coreId).wstring() + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                std::to_wstring(counter.fetch_add(1)) + L".dll");
   if (!CopyFileW(original.c_str(), copy.c_str(), FALSE))
   {
      error = "cannot copy the core to " + copy.u8string();
      return nullptr;
   }

   std::unique_ptr<SynthRefCore> core(new SynthRefCore());
   core->mCopyPath = copy.wstring();
   core->mCoreId = coreId;
   HMODULE module = LoadLibraryExW(copy.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
   if (module == nullptr)
   {
      error = "cannot load " + copy.u8string() + " (error " + std::to_string(GetLastError()) + ")";
      return nullptr;
   }
   core->mModule = module;
   auto entry = reinterpret_cast<SynthRefCoreEntryFn>(GetProcAddress(module, SYNTHREF_CORE_ENTRY));
   const SynthRefCoreApi* api = entry ? entry(SYNTHREF_CORE_ABI_VERSION) : nullptr;
   if (api == nullptr || api->abiVersion != SYNTHREF_CORE_ABI_VERSION || api->structSize < sizeof(SynthRefCoreApi))
   {
      error = "core ABI mismatch (host ABI " + std::to_string(SYNTHREF_CORE_ABI_VERSION) + ")";
      return nullptr;
   }
   const SynthRefCoreInfo* info = api->info();
   if (info == nullptr || info->structSize < sizeof(SynthRefCoreInfo) || info->blockFrames == 0 ||
       info->blockFrames > 4096 || info->channels < 1 || info->channels > 2 || info->sampleRate < 8000)
   {
      error = "core info invalid";
      return nullptr;
   }
   core->mApi = api;
   core->mInfo = info;
   core->mInst = api->create();
   if (core->mInst == nullptr)
   {
      error = "core refused to start";
      return nullptr;
   }
   return core;
}

SynthRefCore::~SynthRefCore()
{
   if (mApi && mInst)
      mApi->destroy(mInst);
   if (mModule)
      FreeLibrary(static_cast<HMODULE>(mModule));
   if (!mCopyPath.empty())
      DeleteFileW(mCopyPath.c_str());
}

// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
//
// Synth REF firmware registry and core loader. The registry is the reviewed
// list of instrument + firmware pairs Synth REF knows; only entries marked
// Supported or Experimental have a core DLL and can be selected. Cores are
// never discovered from disk: adding one means adding a registry entry, a
// core folder under src/gpl/SynthRef/cores/ and its build rule, after a
// licence and code review (docs/SYNTH-REF.md).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "SynthRefCoreApi.h"

enum class SynthRefStatus
{
   Supported,    // tested, works
   Experimental, // partly working
   Unavailable   // known, no executable core
};

struct SynthRefFirmware
{
   const char* coreId;     // saved in patches
   const char* instrument; // physical instrument
   const char* firmware;   // firmware / engine shown to the user
   const char* dll;        // core file in <exe>/synthref/cores, nullptr = none
   SynthRefStatus status;
   const char* note;       // why it is unavailable / what it is
};

const std::vector<SynthRefFirmware>& SynthRefFirmwares();
const SynthRefFirmware* SynthRefFindFirmware(const std::string& coreId);
// ROMs (firmware images as their authors distribute them, see rom/rom_core.c)
// are listed from Documents\Infinite-Turbo\SynthRef\roms with ids "rom:<file>".
void SynthRefRescanRoms();
std::string SynthRefRomPath(const std::string& coreId); // "" if not a ROM id

// One live core instance. Main thread creates and destroys it; every call
// into `api` must be serialised by the owner (SynthRefNode does it with its
// core mutex). A SR_CORE_SINGLETON core gets a private copy of its DLL file
// so instances never share firmware globals.
class SynthRefCore
{
public:
   static std::unique_ptr<SynthRefCore> Open(const std::string& coreId, std::string& error);
   ~SynthRefCore();
   SynthRefCore(const SynthRefCore&) = delete;
   SynthRefCore& operator=(const SynthRefCore&) = delete;

   const SynthRefCoreApi& Api() const { return *mApi; }
   const SynthRefCoreInfo& Info() const { return *mInfo; }
   void* Inst() const { return mInst; }
   const std::string& CoreId() const { return mCoreId; }

private:
   SynthRefCore() = default;
   void* mModule = nullptr; // HMODULE
   std::wstring mCopyPath;  // private DLL copy to delete, empty if none
   const SynthRefCoreApi* mApi = nullptr;
   const SynthRefCoreInfo* mInfo = nullptr;
   void* mInst = nullptr;
   std::string mCoreId;
};

// Documents\Infinite-Turbo\SynthRef (created on first use): the user's own
// banks, projects and controller profiles. Empty if unavailable.
std::string SynthRefUserDir();
// <exe>\synthref: bundled profiles and cores.
std::string SynthRefAppDir();
// Native file dialogs starting in `dir`; "" = cancelled. `filter` is a
// Windows filter string: "Label\0*.a;*.b\0...\0\0".
std::string SynthRefOpenFileDialog(const wchar_t* title, const wchar_t* filter, const std::string& dir);
std::string SynthRefSaveFileDialog(const wchar_t* title, const wchar_t* filter, const wchar_t* ext, const std::string& dir);
void SynthRefRevealFolder(const std::string& dir);

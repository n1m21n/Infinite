// Windows implementation of the Platform facade's audio-plugin surface.
//
// This is deliberate graceful degradation, matching how Syphon and Vision are
// handled elsewhere in the Windows port:
//
//   - Audio Units are macOS-only by definition; there is nothing to enumerate
//     or host, so EnumerateAudioUnits finds nothing and DescribeAudioUnitBundle
//     rejects every path.
//   - VST3 hosting stays behind INFINITE_ENABLE_VST3 exactly as on macOS
//     (licensing: the Steinberg SDK is GPLv3-or-proprietary, this repo is MIT
//     - see docs/plans/audio/plugin-hosting.md). The Windows build does not
//     define that macro today: hosting a VST3 for real means the full
//     IComponent/IEditController/process-adapter layer plus out-of-process
//     scanning, which is its own project. Everything below reports cleanly
//     rather than half-working: scans find nothing, instantiation produces a
//     Failed-state handle whose error text explains why, and the node shows
//     that status instead of spinning "loading..." forever.
//
// Every function tolerates a null handle - AudioPluginNode's retire path can
// hand us one while a topology swap is in flight.

#include "../Platform.h"

#include <string>
#include <vector>

namespace
{
   constexpr const char* kUnavailableError =
      "plugin hosting is not available in this build";

   // Minimal concrete handle: exists only so PluginCreate/PluginPoll round-trip
   // to a clean Failed state instead of crashing or hanging Pending.
   struct StubPluginHandle
   {
      Platform::PluginDesc desc;
      bool failedReported = false;
   };
}

namespace Platform
{
   void EnumerateAudioUnits(std::vector<PluginDesc>& out)
   {
      out.clear(); // no Audio Units off macOS
   }

   bool DescribeAudioUnitBundle(const std::string& bundlePath, std::vector<PluginDesc>& out)
   {
      (void)bundlePath;
      out.clear();
      return false;
   }

   void EnumerateVST3Plugins(const std::vector<std::string>& folders, std::vector<PluginDesc>& out)
   {
      (void)folders;
      out.clear();
   }

   bool DescribeVST3Bundle(const std::string& bundlePath, std::vector<PluginDesc>& out)
   {
      (void)bundlePath;
      out.clear();
      return false;
   }

   void CacheVST3BundlePath(const std::string& identifier, const std::string& bundlePath)
   {
      (void)identifier;
      (void)bundlePath;
   }

   void SetVST3SearchFolders(const std::vector<std::string>& folders)
   {
      (void)folders;
   }

   std::vector<std::string> VST3Blocklist()
   {
      return {};
   }

   void ClearVST3Blocklist() {}

   std::vector<std::string> VST3ScanFailures()
   {
      return {};
   }

   PluginHandle* PluginCreate(const PluginDesc& desc, double sampleRate, int maxBlockFrames)
   {
      (void)sampleRate;
      (void)maxBlockFrames;
      auto* handle = new StubPluginHandle();
      handle->desc = desc;
      return reinterpret_cast<PluginHandle*>(handle);
   }

   PluginLoadState PluginPoll(PluginHandle* handle, std::string& outError)
   {
      outError.clear();
      auto* plugin = reinterpret_cast<StubPluginHandle*>(handle);
      if (plugin == nullptr)
      {
         outError = kUnavailableError;
         return PluginLoadState::Failed;
      }
      // Report the failure once, then keep reporting it - the node latches
      // mLoadFailed on the first Failed and stops polling.
      if (!plugin->failedReported)
         plugin->failedReported = true;
      outError = kUnavailableError;
      return PluginLoadState::Failed;
   }

   bool PluginPrepare(PluginHandle* handle, double sampleRate, int maxBlockFrames,
                      std::string& outError)
   {
      (void)sampleRate;
      (void)maxBlockFrames;
      outError.clear();
      if (reinterpret_cast<StubPluginHandle*>(handle) == nullptr)
      {
         outError = kUnavailableError;
         return false;
      }
      return true; // nothing to prepare; keep the node's prepare bookkeeping happy
   }

   void PluginDestroy(PluginHandle* handle)
   {
      delete reinterpret_cast<StubPluginHandle*>(handle);
   }

   PluginDesc PluginDescriptionOf(PluginHandle* handle)
   {
      auto* plugin = reinterpret_cast<StubPluginHandle*>(handle);
      return plugin != nullptr ? plugin->desc : PluginDesc();
   }

   int PluginLatencySamples(PluginHandle* handle)
   {
      (void)handle;
      return 0;
   }

   void PluginRender(PluginHandle* handle, const float* const* in, int inChannels,
                     float* const* out, int outChannels, int numFrames)
   {
      (void)in;
      (void)inChannels;
      // Silence out: a Failed-state plugin never reaches ProcessBlock in the
      // node, but stay safe if topology timing ever changes under us.
      for (int ch = 0; ch < outChannels; ch++)
      {
         if (out[ch] != nullptr)
         {
            for (int i = 0; i < numFrames; i++)
               out[ch][i] = 0.0f;
         }
      }
   }

   void PluginScheduleMIDIEvent(PluginHandle* handle, int frameOffset,
                                const unsigned char* bytes, int byteCount)
   {
      (void)handle;
      (void)frameOffset;
      (void)bytes;
      (void)byteCount;
   }

   int PluginParameterCount(PluginHandle* handle)
   {
      (void)handle;
      return 0;
   }

   bool PluginParameterInfo(PluginHandle* handle, int index, PluginParamInfo& out)
   {
      (void)handle;
      (void)index;
      (void)out;
      return false;
   }

   bool PluginParameterInfoByAddress(PluginHandle* handle, unsigned long long address,
                                     PluginParamInfo& out)
   {
      (void)handle;
      (void)address;
      (void)out;
      return false;
   }

   void PluginSetParameter(PluginHandle* handle, unsigned long long address, float value)
   {
      (void)handle;
      (void)address;
      (void)value;
   }

   bool PluginGetParameter(PluginHandle* handle, unsigned long long address, float& outValue)
   {
      (void)handle;
      (void)address;
      outValue = 0.0f;
      return false;
   }

   void PluginBeginLearn(PluginHandle* handle)
   {
      (void)handle;
   }

   void PluginEndLearn(PluginHandle* handle)
   {
      (void)handle;
   }

   bool PluginPollLearned(PluginHandle* handle, unsigned long long& outAddress)
   {
      (void)handle;
      (void)outAddress;
      return false;
   }

   bool PluginOpenEditor(PluginHandle* handle, std::string& outError)
   {
      (void)handle;
      outError = kUnavailableError;
      return false;
   }

   void PluginCloseEditor(PluginHandle* handle)
   {
      (void)handle;
   }

   bool PluginEditorIsOpen(PluginHandle* handle)
   {
      (void)handle;
      return false;
   }

   bool AnyPluginEditorOpen()
   {
      return false;
   }

   bool PumpPluginEditorEvents()
   {
      return false;
   }

   bool PluginSaveState(PluginHandle* handle, std::string& outBase64)
   {
      (void)handle;
      outBase64.clear();
      return false;
   }

   bool PluginRestoreState(PluginHandle* handle, const std::string& base64)
   {
      (void)handle;
      (void)base64;
      return false;
   }
}

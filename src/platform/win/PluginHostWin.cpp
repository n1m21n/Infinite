// Windows implementation of the Platform facade's audio-plugin surface.
//
//   - Audio Units are macOS-only by definition; there is nothing to enumerate
//     or host, so EnumerateAudioUnits finds nothing and DescribeAudioUnitBundle
//     rejects every path.
//   - VST3 hosting mirrors macOS behind INFINITE_ENABLE_VST3 (on by default -
//     see CMakeLists.txt): when the flag is on, every PluginXxx entry point
//     below dispatches into the Windows VST3 backend
//     (src/platform/win/PluginVST3Win.cpp), which hosts the SDK's
//     IComponent/IEditController/IAudioProcessor directly, including a real
//     HWND-embedded editor window, out-of-process scanning via
//     infinite-vst3-scanner.exe, and sentinel/blocklist crash safety. Still
//     pending on Windows: full SEH crash guarding beyond state save/restore
//     and editor calls (Tier 3 - see PluginVST3Win.cpp's header for why that
//     tier isn't achievable this way at all). When the flag is off, every
//     plugin instantiation produces a Failed-state handle instead.
//
// Every function tolerates a null handle - AudioPluginNode's retire path can
// hand us one while a topology swap is in flight.

#include "../Platform.h"

#include <string>
#include <vector>

#if INFINITE_ENABLE_VST3
#include "PluginHandleInternalWin.h"
#include "../PluginVST3.h"
#endif

namespace
{
   constexpr const char* kUnavailableError =
      "plugin hosting is not available in this build";
}

#if !INFINITE_ENABLE_VST3
namespace
{
   // Minimal concrete handle: exists only so PluginCreate/PluginPoll round-trip
   // to a clean Failed state instead of crashing or hanging Pending, for
   // builds where INFINITE_ENABLE_VST3 is off.
   struct StubPluginHandle
   {
      Platform::PluginDesc desc;
      bool failedReported = false;
   };
}
#endif

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

#if !INFINITE_ENABLE_VST3
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
#endif // !INFINITE_ENABLE_VST3

#if INFINITE_ENABLE_VST3
   // EnumerateVST3Plugins, DescribeVST3Bundle, CacheVST3BundlePath,
   // SetVST3SearchFolders, VST3Blocklist, ClearVST3Blocklist, and
   // VST3ScanFailures are all defined directly in the Platform namespace by
   // PluginVST3Win.cpp when the flag is on - nothing to dispatch here.

   PluginHandle* PluginCreate(const PluginDesc& desc, double sampleRate, int maxBlockFrames)
   {
      if (desc.format == "vst3")
         return PluginVST3Create(desc, sampleRate, maxBlockFrames);

      // Windows has no AudioUnit backend, so any other format fails cleanly.
      PluginHandle* h = new PluginHandle();
      h->desc = desc;
      h->state = PluginLoadState::Failed;
      h->loadError = kUnavailableError;
      return h;
   }

   PluginLoadState PluginPoll(PluginHandle* handle, std::string& outError)
   {
      outError.clear();
      if (handle == nullptr)
      {
         outError = kUnavailableError;
         return PluginLoadState::Failed;
      }
      if (handle->vst3 != nullptr)
         return PluginVST3Poll(handle, outError);
      outError = handle->loadError.empty() ? kUnavailableError : handle->loadError;
      return handle->state;
   }

   bool PluginPrepare(PluginHandle* handle, double sampleRate, int maxBlockFrames, std::string& outError)
   {
      outError.clear();
      if (handle == nullptr)
      {
         outError = kUnavailableError;
         return false;
      }
      if (handle->vst3 != nullptr)
         return PluginVST3Prepare(handle, sampleRate, maxBlockFrames, outError);
      outError = kUnavailableError;
      return false;
   }

   void PluginDestroy(PluginHandle* handle)
   {
      if (handle == nullptr)
         return;
      if (handle->vst3 != nullptr)
      {
         PluginVST3Destroy(handle); // frees handle too
         return;
      }
      delete handle;
   }

   PluginDesc PluginDescriptionOf(PluginHandle* handle)
   {
      return handle != nullptr ? handle->desc : PluginDesc();
   }

   int PluginLatencySamples(PluginHandle* handle)
   {
      return handle != nullptr ? handle->latencySamples : 0;
   }

   void PluginRender(PluginHandle* handle, const float* const* in, int inChannels,
                     float* const* out, int outChannels, int numFrames)
   {
      if (handle != nullptr && handle->vst3 != nullptr)
      {
         PluginVST3Render(handle, in, inChannels, out, outChannels, numFrames);
         return;
      }
      (void)in;
      (void)inChannels;
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
      if (handle != nullptr && handle->vst3 != nullptr)
         PluginVST3ScheduleMIDIEvent(handle, frameOffset, bytes, byteCount);
   }

   int PluginParameterCount(PluginHandle* handle)
   {
      return (handle != nullptr && handle->vst3 != nullptr) ? PluginVST3ParameterCount(handle) : 0;
   }

   bool PluginParameterInfo(PluginHandle* handle, int index, PluginParamInfo& out)
   {
      return (handle != nullptr && handle->vst3 != nullptr) && PluginVST3ParameterInfo(handle, index, out);
   }

   bool PluginParameterInfoByAddress(PluginHandle* handle, unsigned long long address,
                                     PluginParamInfo& out)
   {
      return (handle != nullptr && handle->vst3 != nullptr) &&
             PluginVST3ParameterInfoByAddress(handle, address, out);
   }

   void PluginSetParameter(PluginHandle* handle, unsigned long long address, float value)
   {
      if (handle != nullptr && handle->vst3 != nullptr)
         PluginVST3SetParameter(handle, address, value);
   }

   bool PluginGetParameter(PluginHandle* handle, unsigned long long address, float& outValue)
   {
      outValue = 0.0f;
      return (handle != nullptr && handle->vst3 != nullptr) && PluginVST3GetParameter(handle, address, outValue);
   }

   void PluginBeginLearn(PluginHandle* handle)
   {
      if (handle != nullptr && handle->vst3 != nullptr)
         PluginVST3BeginLearn(handle);
   }

   void PluginEndLearn(PluginHandle* handle)
   {
      if (handle != nullptr && handle->vst3 != nullptr)
         PluginVST3EndLearn(handle);
   }

   bool PluginPollLearned(PluginHandle* handle, unsigned long long& outAddress)
   {
      return (handle != nullptr && handle->vst3 != nullptr) && PluginVST3PollLearned(handle, outAddress);
   }

   bool PluginOpenEditor(PluginHandle* handle, std::string& outError)
   {
      outError.clear();
      if (handle == nullptr || handle->vst3 == nullptr)
      {
         outError = kUnavailableError;
         return false;
      }
      return PluginVST3OpenEditor(handle, outError);
   }

   void PluginCloseEditor(PluginHandle* handle)
   {
      if (handle != nullptr && handle->vst3 != nullptr)
         PluginVST3CloseEditor(handle);
   }

   bool PluginEditorIsOpen(PluginHandle* handle)
   {
      return handle != nullptr && handle->vst3 != nullptr && PluginVST3EditorIsOpen(handle);
   }

   bool AnyPluginEditorOpen()
   {
      return PluginVST3AnyEditorOpen();
   }

   bool PumpPluginEditorEvents()
   {
      return PluginVST3PumpEditorEvents();
   }

   bool PluginSaveState(PluginHandle* handle, std::string& outBase64)
   {
      outBase64.clear();
      return (handle != nullptr && handle->vst3 != nullptr) && PluginVST3SaveState(handle, outBase64);
   }

   bool PluginRestoreState(PluginHandle* handle, const std::string& base64)
   {
      return (handle != nullptr && handle->vst3 != nullptr) && PluginVST3RestoreState(handle, base64);
   }

#else // !INFINITE_ENABLE_VST3

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
      (void)handle;
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

#endif // INFINITE_ENABLE_VST3
}

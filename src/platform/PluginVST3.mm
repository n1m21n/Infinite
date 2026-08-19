#include "PluginVST3.h"

#if INFINITE_ENABLE_VST3

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#import <Cocoa/Cocoa.h>
#import <CoreFoundation/CoreFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"

#include "PluginHandleInternal.h"
#include "crude_json.h"

namespace
{
   namespace fs = std::filesystem;

   // Dev-only tracing for VST3 bundle resolution, gated on INFINITE_VST3TRACE.
   // Bundle resolution happens on a background queue with no UI surface beyond
   // a one-line node status, so without this the only symptom of any failure
   // along the chain is the final "bundle not found on disk".
   void VST3Trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
   void VST3Trace(const char* fmt, ...)
   {
      static const bool enabled = getenv("INFINITE_VST3TRACE") != nullptr;
      if (!enabled)
         return;
      va_list args;
      va_start(args, fmt);
      std::fprintf(stderr, "[vst3] ");
      std::vfprintf(stderr, fmt, args);
      std::fprintf(stderr, "\n");
      va_end(args);
      std::fflush(stderr);
   }

   // ------------------------------------------------------------------------
   // UID and string utilities
   // ------------------------------------------------------------------------

   std::string TUIDToHexString(const Steinberg::TUID tuid)
   {
      char hex[33];
      for (int i = 0; i < 16; i++)
         std::snprintf(hex + i * 2, 3, "%02X", (unsigned char)tuid[i]);
      hex[32] = '\0';
      return std::string(hex);
   }

   bool HexStringToTUID(const std::string& hex, Steinberg::TUID outTUID)
   {
      if (hex.length() != 32)
         return false;
      for (int i = 0; i < 16; i++)
      {
         unsigned int byteVal = 0;
         if (std::sscanf(hex.substr(i * 2, 2).c_str(), "%02x", &byteVal) != 1)
            return false;
         outTUID[i] = (char)(unsigned char)byteVal;
      }
      return true;
   }

   std::string MakeVST3Identifier(const Steinberg::TUID tuid)
   {
      return "vst3:" + TUIDToHexString(tuid);
   }

   bool ParseVST3Identifier(const std::string& id, Steinberg::TUID outTUID)
   {
      if (id.rfind("vst3:", 0) != 0)
         return false;
      return HexStringToTUID(id.substr(5), outTUID);
   }

   std::string UTF16ToUTF8(const Steinberg::Vst::TChar* str)
   {
      if (str == nullptr)
         return std::string();
      size_t len = 0;
      while (str[len] != 0)
         len++;
      NSString* ns = [NSString stringWithCharacters:(const unichar*)str
                                             length:(NSUInteger)len];
      return ns != nil ? std::string([ns UTF8String]) : std::string();
   }

   void UTF8ToUTF16(const std::string& utf8, Steinberg::Vst::TChar* outStr, int maxChars)
   {
      if (outStr == nullptr || maxChars <= 0)
         return;
      @autoreleasepool
      {
         NSString* ns = [NSString stringWithUTF8String:utf8.c_str()];
         if (ns == nil)
         {
            outStr[0] = 0;
            return;
         }
         NSUInteger len = std::min((NSUInteger)[ns length], (NSUInteger)(maxChars - 1));
         [ns getCharacters:(unichar*)outStr range:NSMakeRange(0, len)];
         outStr[len] = 0;
      }
   }

}

namespace Platform
{
   // Bundle path cache mapping identifier -> bundle file path for quick instantiation
   static std::mutex gBundleMapMutex;
   static std::unordered_map<std::string, std::string> gVST3BundleMap;

   void CacheVST3BundlePath(const std::string& identifier, const std::string& bundlePath)
   {
      std::lock_guard<std::mutex> lock(gBundleMapMutex);
      gVST3BundleMap[identifier] = bundlePath;
   }

   std::string GetCachedVST3BundlePath(const std::string& identifier)
   {
      std::lock_guard<std::mutex> lock(gBundleMapMutex);
      auto it = gVST3BundleMap.find(identifier);
      if (it != gVST3BundleMap.end())
         return it->second;
      return std::string();
   }

   // User-added VST3 folders, mirrored from PluginScanner::Folders() so that
   // on-demand bundle resolution below (PluginVST3Create) searches the same
   // folders the scan does, not just the two OS-standard directories.
   static std::mutex gSearchFoldersMutex;
   static std::vector<std::string> gExtraVST3SearchFolders;

   void SetVST3SearchFolders(const std::vector<std::string>& folders)
   {
      std::lock_guard<std::mutex> lock(gSearchFoldersMutex);
      gExtraVST3SearchFolders = folders;
   }

   static std::vector<std::string> GetExtraVST3SearchFolders()
   {
      std::lock_guard<std::mutex> lock(gSearchFoldersMutex);
      return gExtraVST3SearchFolders;
   }

   // ------------------------------------------------------------------------
   // Crash-safety: sentinel + blocklist
   //
   // DescribeVST3Bundle loads an arbitrary third party's compiled code
   // in-process (CFBundleLoadExecutable -> bundleEntry -> read factory ->
   // bundleExit). A hostile or simply broken bundle can SIGSEGV or abort from
   // inside that call, which a C++ try/catch cannot intercept. The sentinel
   // records which bundle is being probed *before* the call, so if this
   // process is dead the next time the app launches, the last-probed path is
   // still sitting in the sentinel file and gets blocklisted rather than
   // killing every future scan the same way.
   // ------------------------------------------------------------------------

   std::string SettingsDirForVST3()
   {
      const char* home = getenv("HOME");
      if (home == nullptr)
         return std::string();
      std::string dir = std::string(home) + "/Library/Application Support/Infinite";
      if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
         dir += "/plugin_drag_test";
      mkdir(dir.c_str(), 0755);
      return dir;
   }

   std::string VST3SentinelPath()
   {
      const std::string dir = SettingsDirForVST3();
      return dir.empty() ? std::string() : dir + "/PluginScanSentinel.txt";
   }

   std::string VST3BlocklistPath()
   {
      const std::string dir = SettingsDirForVST3();
      return dir.empty() ? std::string() : dir + "/PluginVST3Blocklist.json";
   }

   std::mutex gVST3SafetyMutex; // guards gBlocklist and every sentinel/failure op below
   std::vector<std::string> gBlocklist;
   std::vector<std::string> gScanFailures;
   bool gBlocklistLoaded = false;

   void LoadBlocklistLocked()
   {
      if (gBlocklistLoaded)
         return;
      gBlocklistLoaded = true;
      const std::string path = VST3BlocklistPath();
      if (path.empty())
         return;
      auto [json, ok] = crude_json::value::load(path);
      if (!ok || !json.is_array())
         return;
      for (const crude_json::value& v : json.get<crude_json::array>())
         if (v.is_string())
            gBlocklist.push_back(v.get<crude_json::string>());
   }

   void SaveBlocklistLocked()
   {
      const std::string path = VST3BlocklistPath();
      if (path.empty())
         return;
      crude_json::value json = crude_json::array{};
      for (const std::string& p : gBlocklist)
         json.push_back(crude_json::value(p));
      json.save(path, 2);
   }

   // Checked once per process, before the first bundle is ever probed: if the
   // previous run's sentinel is still sitting there non-empty, that run died
   // mid-probe of that exact bundle.
   void CheckSentinelForCrashLocked()
   {
      const std::string path = VST3SentinelPath();
      if (path.empty())
         return;
      FILE* f = std::fopen(path.c_str(), "rb");
      if (f == nullptr)
         return;
      char buf[4096] = {};
      size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
      std::fclose(f);
      if (n == 0)
         return;
      std::string crashed(buf, n);
      while (!crashed.empty() && (crashed.back() == '\n' || crashed.back() == '\r'))
         crashed.pop_back();
      if (crashed.empty())
         return;

      LoadBlocklistLocked();
      if (std::find(gBlocklist.begin(), gBlocklist.end(), crashed) == gBlocklist.end())
      {
         VST3Trace("sentinel found non-empty at startup - blocklisting: %s", crashed.c_str());
         gBlocklist.push_back(crashed);
         SaveBlocklistLocked();
      }
      std::remove(path.c_str());
   }

   void EnsureSentinelCheckedOnce()
   {
      static dispatch_once_t once;
      dispatch_once(&once, ^{
         std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
         CheckSentinelForCrashLocked();
      });
   }

   bool IsBlocklistedPath(const std::string& bundlePath)
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      LoadBlocklistLocked();
      return std::find(gBlocklist.begin(), gBlocklist.end(), bundlePath) != gBlocklist.end();
   }

   // Written+flushed just before the in-process probe, cleared right after a
   // clean return (success or ordinary failure). Deliberately not RAII: the
   // whole point is to survive the case where the destructor never runs.
   void WriteSentinel(const std::string& bundlePath)
   {
      const std::string path = VST3SentinelPath();
      if (path.empty())
         return;
      FILE* f = std::fopen(path.c_str(), "wb");
      if (f == nullptr)
         return;
      std::fwrite(bundlePath.data(), 1, bundlePath.size(), f);
      std::fflush(f);
      fsync(fileno(f));
      std::fclose(f);
   }

   void ClearSentinel()
   {
      const std::string path = VST3SentinelPath();
      if (!path.empty())
         std::remove(path.c_str());
   }

   void RecordScanFailure(const std::string& bundlePath)
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      gScanFailures.push_back(bundlePath);
   }
}

namespace Platform
{
   std::vector<std::string> VST3Blocklist()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      LoadBlocklistLocked();
      return gBlocklist;
   }

   void ClearVST3Blocklist()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      LoadBlocklistLocked();
      gBlocklist.clear();
      SaveBlocklistLocked();
   }

   std::vector<std::string> VST3ScanFailures()
   {
      std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
      return gScanFailures;
   }
}

namespace
{

   // ------------------------------------------------------------------------
   // MemoryStream for IBStream state save/restore
   // ------------------------------------------------------------------------

   class MemoryStream : public Steinberg::IBStream
   {
   public:
      MemoryStream() = default;
      explicit MemoryStream(const void* data, size_t size)
      {
         if (data != nullptr && size > 0)
            mBuffer.assign((const uint8_t*)data, (const uint8_t*)data + size);
      }

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IBStream::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesRead) override
      {
         if (buffer == nullptr || numBytes < 0)
            return Steinberg::kInvalidArgument;
         const size_t available = (mPos < mBuffer.size()) ? (mBuffer.size() - mPos) : 0;
         const size_t toRead = std::min((size_t)numBytes, available);
         if (toRead > 0)
         {
            std::memcpy(buffer, mBuffer.data() + mPos, toRead);
            mPos += toRead;
         }
         if (numBytesRead != nullptr)
            *numBytesRead = (Steinberg::int32)toRead;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesWritten) override
      {
         if (buffer == nullptr || numBytes < 0)
            return Steinberg::kInvalidArgument;
         if (mPos + (size_t)numBytes > mBuffer.size())
            mBuffer.resize(mPos + (size_t)numBytes);
         std::memcpy(mBuffer.data() + mPos, buffer, (size_t)numBytes);
         mPos += (size_t)numBytes;
         if (numBytesWritten != nullptr)
            *numBytesWritten = numBytes;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API seek(Steinberg::int64 offset, Steinberg::int32 mode, Steinberg::int64* result) override
      {
         Steinberg::int64 newPos = (Steinberg::int64)mPos;
         if (mode == Steinberg::IBStream::kIBSeekSet)
            newPos = offset;
         else if (mode == Steinberg::IBStream::kIBSeekCur)
            newPos += offset;
         else if (mode == Steinberg::IBStream::kIBSeekEnd)
            newPos = (Steinberg::int64)mBuffer.size() + offset;

         if (newPos < 0)
            return Steinberg::kInvalidArgument;
         mPos = (size_t)newPos;
         if (result != nullptr)
            *result = (Steinberg::int64)mPos;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API tell(Steinberg::int64* result) override
      {
         if (result != nullptr)
            *result = (Steinberg::int64)mPos;
         return Steinberg::kResultOk;
      }

      const std::vector<uint8_t>& getBuffer() const { return mBuffer; }
      size_t getSize() const { return mBuffer.size(); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::vector<uint8_t> mBuffer;
      size_t mPos = 0;
   };

   // ------------------------------------------------------------------------
   // Host Application Context
   // ------------------------------------------------------------------------

   class HostApplication : public Steinberg::Vst::IHostApplication
   {
   public:
      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IHostApplication::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
      {
         UTF8ToUTF16("Infinite", name, 128);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void** obj) override
      {
         (void)cid;
         (void)_iid;
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
   };

   // One host context for the whole process, deliberately never destroyed: it
   // is handed to plugin factories via IPluginFactory3::setHostContext and to
   // IComponent/IEditController::initialize, and a plugin may hold the pointer
   // for as long as it is loaded.
   Steinberg::Vst::IHostApplication* SharedHostApplication()
   {
      static HostApplication* instance = new HostApplication();
      return instance;
   }

   // ------------------------------------------------------------------------
   // Real-time Safe Event List
   // ------------------------------------------------------------------------

   class HostEventList : public Steinberg::Vst::IEventList
   {
   public:
      static constexpr int kMaxEvents = 64;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IEventList::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::int32 PLUGIN_API getEventCount() override
      {
         return mCount.load(std::memory_order_relaxed);
      }

      Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override
      {
         const int count = mCount.load(std::memory_order_relaxed);
         if (index < 0 || index >= count)
            return Steinberg::kInvalidArgument;
         e = mEvents[index];
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override
      {
         int count = mCount.load(std::memory_order_relaxed);
         if (count >= kMaxEvents)
            return Steinberg::kResultFalse;
         mEvents[count] = e;
         mCount.store(count + 1, std::memory_order_relaxed);
         return Steinberg::kResultOk;
      }

      void clear()
      {
         mCount.store(0, std::memory_order_relaxed);
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::atomic<int> mCount { 0 };
      Steinberg::Vst::Event mEvents[kMaxEvents] = {};
   };

   // ------------------------------------------------------------------------
   // Real-time Safe Parameter Changes
   // ------------------------------------------------------------------------

   class HostParamValueQueue : public Steinberg::Vst::IParamValueQueue
   {
   public:
      static constexpr int kMaxPoints = 8;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IParamValueQueue::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return mParamId; }

      Steinberg::int32 PLUGIN_API getPointCount() override
      {
         return mPointCount.load(std::memory_order_relaxed);
      }

      Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                             Steinberg::Vst::ParamValue& value) override
      {
         const int count = mPointCount.load(std::memory_order_relaxed);
         if (index < 0 || index >= count)
            return Steinberg::kInvalidArgument;
         sampleOffset = mOffsets[index];
         value = mValues[index];
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value,
                                             Steinberg::int32& index) override
      {
         int count = mPointCount.load(std::memory_order_relaxed);
         if (count >= kMaxPoints)
         {
            index = count - 1;
            mOffsets[index] = sampleOffset;
            mValues[index] = value;
            return Steinberg::kResultOk;
         }
         index = count;
         mOffsets[index] = sampleOffset;
         mValues[index] = value;
         mPointCount.store(count + 1, std::memory_order_relaxed);
         return Steinberg::kResultOk;
      }

      void init(Steinberg::Vst::ParamID id)
      {
         mParamId = id;
         mPointCount.store(0, std::memory_order_relaxed);
      }

      void clear()
      {
         mPointCount.store(0, std::memory_order_relaxed);
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      Steinberg::Vst::ParamID mParamId = 0;
      std::atomic<int> mPointCount { 0 };
      Steinberg::int32 mOffsets[kMaxPoints] = {};
      Steinberg::Vst::ParamValue mValues[kMaxPoints] = {};
   };

   class HostParameterChanges : public Steinberg::Vst::IParameterChanges
   {
   public:
      static constexpr int kMaxQueues = 32;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IParameterChanges::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::int32 PLUGIN_API getParameterCount() override
      {
         return mQueueCount.load(std::memory_order_relaxed);
      }

      Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
      {
         const int count = mQueueCount.load(std::memory_order_relaxed);
         if (index < 0 || index >= count)
            return nullptr;
         return &mQueues[index];
      }

      Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                    Steinberg::int32& index) override
      {
         int count = mQueueCount.load(std::memory_order_relaxed);
         for (int i = 0; i < count; i++)
         {
            if (mQueues[i].getParameterId() == id)
            {
               index = i;
               return &mQueues[i];
            }
         }
         if (count >= kMaxQueues)
         {
            index = -1;
            return nullptr;
         }
         index = count;
         mQueues[index].init(id);
         mQueueCount.store(count + 1, std::memory_order_relaxed);
         return &mQueues[index];
      }

      void clear()
      {
         mQueueCount.store(0, std::memory_order_relaxed);
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::atomic<int> mQueueCount { 0 };
      HostParamValueQueue mQueues[kMaxQueues];
   };

   // ------------------------------------------------------------------------
   // Host Component Handler (routing learn mode beginEdit/performEdit)
   // ------------------------------------------------------------------------

   class HostComponentHandler : public Steinberg::Vst::IComponentHandler
   {
   public:
      explicit HostComponentHandler(Platform::PluginHandle* handle) : mHandle(handle) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
         }
         *obj = nullptr;
         return Steinberg::kNoInterface;
      }

      Steinberg::uint32 PLUGIN_API addRef() override { return ++mRefCount; }
      Steinberg::uint32 PLUGIN_API release() override
      {
         if (--mRefCount == 0)
         {
            delete this;
            return 0;
         }
         return mRefCount;
      }

      Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override
      {
         RecordTouch(id);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                                Steinberg::Vst::ParamValue valueNormalized) override
      {
         (void)valueNormalized;
         RecordTouch(id);
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override
      {
         (void)id;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
      {
         (void)flags;
         return Steinberg::kResultOk;
      }

      void detach() { mHandle = nullptr; }

   private:
      void RecordTouch(Steinberg::Vst::ParamID id);

      std::atomic<uint32_t> mRefCount { 1 };
      Platform::PluginHandle* mHandle = nullptr;
   };
}

namespace Platform
{
   // Internal VST3 state structure
   struct PluginVST3State
   {
      CFBundleRef bundle = nullptr;
      Steinberg::IPtr<Steinberg::IPluginFactory> factory;
      Steinberg::IPtr<Steinberg::Vst::IComponent> component;
      Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
      Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
      Steinberg::IPtr<Steinberg::IPlugView> plugView;
      Steinberg::IPtr<HostComponentHandler> componentHandler;

      // Pending instantiation arrival
      std::mutex arrivalMutex;
      std::atomic<bool> arrived { false };
      std::string arrivedError;

      // Processing state
      double sampleRate = 0.0;
      int maxBlockFrames = 0;
      int pluginInChannels = 0;
      int pluginOutChannels = 0;
      bool active = false;
      bool processing = false;

      // Preallocated realtime process data
      Steinberg::Vst::ProcessData processData;
      Steinberg::Vst::AudioBusBuffers inputBusBuffers[1] = {};
      Steinberg::Vst::AudioBusBuffers outputBusBuffers[1] = {};
      float* inChannelPtrs[8] = {};
      float* outChannelPtrs[8] = {};
      std::vector<float> inScratch;
      std::vector<float> outScratch;
      std::vector<float> zeroScratch;

      HostEventList inputEvents;
      HostEventList outputEvents;
      HostParameterChanges inputParamChanges;
      HostParameterChanges outputParamChanges;
      Steinberg::Vst::ProcessContext processContext;
      Steinberg::int64 sampleTime = 0;

      // Learn mode
      std::atomic<bool> learning { false };
      std::atomic<unsigned long long> learnedAddress { 0 };
      std::atomic<bool> learnedValid { false };

      // Concurrency & Editor
      std::atomic<bool> inRender { false };
      NSWindow* editorWindow = nil;
      InfinitePluginEditorDelegate* editorDelegate = nil;
      std::atomic<bool> editorOpen { false };
   };
}

namespace
{
   void HostComponentHandler::RecordTouch(Steinberg::Vst::ParamID id)
   {
      if (mHandle == nullptr || mHandle->vst3 == nullptr)
         return;
      if (!mHandle->vst3->learning.load(std::memory_order_relaxed))
         return;
      mHandle->vst3->learnedAddress.store((unsigned long long)id, std::memory_order_relaxed);
      mHandle->vst3->learnedValid.store(true, std::memory_order_release);
   }

   using GetPluginFactoryProc = Steinberg::IPluginFactory* (*)();
   using BundleEntryProc = bool (*)(CFBundleRef);
   using BundleExitProc = bool (*)();

   // Every macOS VST3 exports bundleEntry/bundleExit, and the module must be
   // handed its own CFBundleRef through bundleEntry before anything is
   // instantiated from its factory - that is how the plugin finds its own
   // resources. Skipping it is survivable for self-contained plugins (the
   // factory pointer still comes back), but plugins that load resources off
   // disk fail at *instantiation* instead: FabFilter throws an uncaught
   // EResource "Resource 'One.fil' not found" and takes the host down with it.
   void UnloadVST3Bundle(CFBundleRef bundle)
   {
      if (bundle == nullptr)
         return;
      if (BundleExitProc bundleExit =
             (BundleExitProc)CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")))
         bundleExit();
      CFRelease(bundle);
   }

   CFBundleRef LoadVST3Bundle(const std::string& bundlePath, Steinberg::IPluginFactory** outFactory)
   {
      // Plugins assume they are loaded into a running Cocoa app and touch NSApp
      // (or things that lazily need it) from inside their own module init.
      // GLFW creates it in the real app, so this is a no-op there; it matters
      // for anything that loads a plugin before the window system is up.
      static dispatch_once_t onceApp;
      dispatch_once(&onceApp, ^{ [NSApplication sharedApplication]; });

      if (outFactory != nullptr)
         *outFactory = nullptr;
      if (bundlePath.empty())
         return nullptr;

      @autoreleasepool
      {
         NSString* path = [NSString stringWithUTF8String:bundlePath.c_str()];
         NSURL* url = [NSURL fileURLWithPath:path];
         if (url == nil)
            return nullptr;

         CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, (__bridge CFURLRef)url);
         if (bundle == nullptr)
         {
            VST3Trace("CFBundleCreate failed: %s", bundlePath.c_str());
            return nullptr;
         }

         if (!CFBundleLoadExecutable(bundle))
         {
            VST3Trace("CFBundleLoadExecutable failed: %s", bundlePath.c_str());
            CFRelease(bundle);
            return nullptr;
         }

         // Before the factory is touched: see UnloadVST3Bundle above for why
         // this is not optional.
         if (BundleEntryProc bundleEntry =
                (BundleEntryProc)CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")))
         {
            if (!bundleEntry(bundle))
            {
               VST3Trace("bundleEntry failed: %s", bundlePath.c_str());
               CFRelease(bundle);
               return nullptr;
            }
         }

         GetPluginFactoryProc getFactory =
            (GetPluginFactoryProc)CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory"));
         if (getFactory == nullptr)
         {
            VST3Trace("no GetPluginFactory symbol: %s", bundlePath.c_str());
            UnloadVST3Bundle(bundle);
            return nullptr;
         }

         Steinberg::IPluginFactory* factory = getFactory();
         if (factory == nullptr)
         {
            VST3Trace("GetPluginFactory returned null: %s", bundlePath.c_str());
            UnloadVST3Bundle(bundle);
            return nullptr;
         }

         if (outFactory != nullptr)
            *outFactory = factory;
         return bundle;
      }
   }
}

namespace Platform
{
   void EnumerateVST3Plugins(const std::vector<std::string>& folders, std::vector<PluginDesc>& out)
   {
      EnsureSentinelCheckedOnce();
      {
         std::lock_guard<std::mutex> lock(gVST3SafetyMutex);
         gScanFailures.clear();
      }

      for (const std::string& root : folders)
      {
         if (root.empty())
            continue;
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
               continue;

            while (it != end)
            {
               const fs::directory_entry entry = *it;
               std::error_code entryEc;
               if (entry.is_directory(entryEc) && !entryEc)
               {
                  if (entry.path().extension() == ".vst3")
                  {
                     const std::string bundlePath = entry.path().string();
                     if (IsBlocklistedPath(bundlePath))
                     {
                        VST3Trace("skipping blocklisted bundle: %s", bundlePath.c_str());
                        RecordScanFailure(bundlePath);
                     }
                     else if (!DescribeVST3Bundle(bundlePath, out))
                     {
                        RecordScanFailure(bundlePath);
                     }
                  }
                  else
                  {
                     dirStack.push_back(entry.path());
                  }
               }
               it.increment(ec);
               if (ec)
                  break;
            }
         }
      }
   }

   bool DescribeVST3Bundle(const std::string& bundlePath, std::vector<PluginDesc>& out)
   {
      EnsureSentinelCheckedOnce();
      if (IsBlocklistedPath(bundlePath))
      {
         VST3Trace("refusing blocklisted bundle: %s", bundlePath.c_str());
         return false;
      }

      // Sentinel window: everything between here and ClearSentinel() below
      // runs arbitrary third-party code in-process (CFBundleLoadExecutable,
      // bundleEntry, the factory constructor). If this process doesn't
      // survive that, the next launch finds the sentinel still pointing at
      // this exact bundle and blocklists it instead of repeating the crash.
      WriteSentinel(bundlePath);

      Steinberg::IPluginFactory* factoryRaw = nullptr;
      CFBundleRef bundle = LoadVST3Bundle(bundlePath, &factoryRaw);
      if (bundle == nullptr || factoryRaw == nullptr)
      {
         ClearSentinel();
         return false;
      }

      Steinberg::IPtr<Steinberg::IPluginFactory> factory(factoryRaw);
      Steinberg::IPtr<Steinberg::IPluginFactory2> factory2;
      factory->queryInterface(Steinberg::IPluginFactory2::iid, (void**)&factory2);

      const Steinberg::int32 numClasses = factory->countClasses();
      bool foundAny = false;

      for (Steinberg::int32 i = 0; i < numClasses; i++)
      {
         Steinberg::PClassInfo classInfo = {};
         Steinberg::PClassInfo2 classInfo2 = {};
         std::string category;
         std::string name;
         std::string vendor;
         std::string subCategories;
         Steinberg::TUID classId = {};

         if (factory2)
         {
            if (factory2->getClassInfo2(i, &classInfo2) == Steinberg::kResultOk)
            {
               category = classInfo2.category;
               name = classInfo2.name;
               vendor = classInfo2.vendor;
               subCategories = classInfo2.subCategories;
               std::memcpy(classId, classInfo2.cid, sizeof(Steinberg::TUID));
            }
         }
         else
         {
            if (factory->getClassInfo(i, &classInfo) == Steinberg::kResultOk)
            {
               category = classInfo.category;
               name = classInfo.name;
               std::memcpy(classId, classInfo.cid, sizeof(Steinberg::TUID));
            }
         }

         VST3Trace("  class[%d] category='%s' name='%s' uid=%s", (int)i, category.c_str(), name.c_str(),
                   TUIDToHexString(classId).c_str());

         if (category == kVstAudioEffectClass)
         {
            PluginDesc desc;
            desc.format = "vst3";
            desc.name = name;
            desc.manufacturer = vendor;
            desc.identifier = MakeVST3Identifier(classId);
            desc.path = bundlePath;

            // acceptsNotes is true for Instruments and Synths
            std::string subCatLower = subCategories;
            std::transform(subCatLower.begin(), subCatLower.end(), subCatLower.begin(), ::tolower);
            desc.acceptsNotes = (subCatLower.find("instrument") != std::string::npos ||
                                 subCatLower.find("synth") != std::string::npos);

            CacheVST3BundlePath(desc.identifier, bundlePath);
            out.push_back(std::move(desc));
            foundAny = true;
         }
      }

      UnloadVST3Bundle(bundle);
      ClearSentinel();
      return foundAny;
   }

   PluginHandle* PluginVST3Create(const PluginDesc& desc, double sampleRate, int maxBlockFrames)
   {
      PluginHandle* h = new PluginHandle();
      h->desc = desc;
      h->state = PluginLoadState::Pending;
      h->sampleRate = sampleRate;
      h->maxBlockFrames = maxBlockFrames > 0 ? maxBlockFrames : 512;

      PluginVST3State* v = new PluginVST3State();
      v->sampleRate = h->sampleRate;
      v->maxBlockFrames = h->maxBlockFrames;
      h->vst3 = v;

      struct TUIDHolder { Steinberg::TUID data; };
      TUIDHolder cidHolder {};
      if (!ParseVST3Identifier(desc.identifier, cidHolder.data))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = "invalid VST3 identifier: " + desc.identifier;
         v->arrived.store(true, std::memory_order_release);
         return h;
      }

      // Background instantiation, so a slow plugin never stalls a frame - the
      // handle comes back Pending immediately and the node polls it.
      dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
         // Use h->desc, not the `desc` reference parameter, from here on: `desc`
         // is only guaranteed valid for the synchronous portion of this call.
         // Its ultimate referent (e.g. the Plugins panel's gPluginDragDesc
         // global) can be mutated or reset by the caller the instant this
         // function returns - which happens well before this block, queued
         // for a background thread, actually runs. h->desc was deep-copied
         // from desc back on the calling thread (right above, before
         // dispatch_async), so it is a stable, owned snapshot safe to read
         // here no matter what the caller does to its own copy afterward.
         const PluginDesc& desc = h->desc;
         VST3Trace("resolve begin: name='%s' id='%s' path='%s'", desc.name.c_str(), desc.identifier.c_str(),
                   desc.path.c_str());

         // Step 1: desc.path, if the caller gave us one and it still exists.
         // Step 2: the resolver cache, which LoadFromDisk (PluginScanner.cpp)
         // now reliably re-seeds from the persisted index at launch, so this
         // is no longer empty on every relaunch the way it used to be.
         // There is deliberately no step here that probes
         // "<root>/<desc.name>.vst3": desc.name is the factory class name,
         // not a filename, and matching it against a bundle filename produced
         // both false misses and wrong hits for no benefit once the index is
         // the source of truth.
         std::string bundlePath;
         std::string lastKnownPath; // for the terminal error message only
         bool wasIndexed = false;

         if (!desc.path.empty() && fs::exists(desc.path))
         {
            bundlePath = desc.path;
            CacheVST3BundlePath(desc.identifier, bundlePath);
            VST3Trace("  resolved from desc.path");
         }
         else
         {
            if (!desc.path.empty())
            {
               wasIndexed = true;
               lastKnownPath = desc.path;
            }
            bundlePath = GetCachedVST3BundlePath(desc.identifier);
            if (!bundlePath.empty())
            {
               wasIndexed = true;
               lastKnownPath = bundlePath;
            }
            VST3Trace("  desc.path unusable (empty=%d); cache lookup -> '%s'", (int)desc.path.empty(),
                      bundlePath.c_str());
         }

         // Step 3: a targeted rescan, only if both of the above missed.
         if (bundlePath.empty())
         {
            std::vector<std::string> searchFolders;
            searchFolders.push_back("/Library/Audio/Plug-Ins/VST3");
            const char* home = getenv("HOME");
            if (home != nullptr)
               searchFolders.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
            for (const auto& extra : GetExtraVST3SearchFolders())
               if (std::find(searchFolders.begin(), searchFolders.end(), extra) == searchFolders.end())
                  searchFolders.push_back(extra);
            std::vector<PluginDesc> discovered;
            VST3Trace("  cache miss; targeted rescan of %d folder(s)", (int)searchFolders.size());
            for (const auto& f : searchFolders)
               VST3Trace("    folder: %s", f.c_str());
            EnumerateVST3Plugins(searchFolders, discovered);
            bundlePath = GetCachedVST3BundlePath(desc.identifier);
            VST3Trace("  rescan found %d plugin(s); cache lookup -> '%s'", (int)discovered.size(),
                      bundlePath.c_str());
         }

         if (bundlePath.empty())
         {
            std::string message = "VST3 not resolvable: " + desc.identifier;
            if (wasIndexed)
               message += " (indexed at " + lastKnownPath + ", which no longer exists - rescan plugins)";
            else
               message += " (not found in the plugin index - rescan plugins)";
            {
               std::lock_guard<std::mutex> lock(v->arrivalMutex);
               v->arrivedError = message;
            }
            VST3Trace("  %s", message.c_str());
            v->arrived.store(true, std::memory_order_release);
            return;
         }

         Steinberg::IPluginFactory* factoryRaw = nullptr;
         CFBundleRef bundle = LoadVST3Bundle(bundlePath, &factoryRaw);
         if (bundle == nullptr || factoryRaw == nullptr)
         {
            {
               std::lock_guard<std::mutex> lock(v->arrivalMutex);
               v->arrivedError = "failed to load VST3 bundle executable";
            }
            v->arrived.store(true, std::memory_order_release);
            return;
         }

         v->bundle = bundle;
         v->factory = factoryRaw;

         // Must happen before createInstance: a plugin is entitled to use the
         // host context from inside its own factory, and several dereference it
         // unconditionally.
         {
            Steinberg::IPtr<Steinberg::IPluginFactory3> factory3;
            if (v->factory->queryInterface(Steinberg::IPluginFactory3::iid, (void**)&factory3) ==
                   Steinberg::kResultOk &&
                factory3)
               factory3->setHostContext((Steinberg::FUnknown*)SharedHostApplication());
         }

         Steinberg::Vst::IComponent* compRaw = nullptr;
         if (v->factory->createInstance(cidHolder.data, Steinberg::Vst::IComponent::iid, (void**)&compRaw) != Steinberg::kResultOk ||
             compRaw == nullptr)
         {
            {
               std::lock_guard<std::mutex> lock(v->arrivalMutex);
               v->arrivedError = "failed to create VST3 component instance";
            }
            v->arrived.store(true, std::memory_order_release);
            return;
         }
         v->component = compRaw;

         Steinberg::Vst::IEditController* ctrlRaw = nullptr;
         if (compRaw->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctrlRaw) == Steinberg::kResultOk &&
             ctrlRaw != nullptr)
         {
            v->controller = ctrlRaw;
         }
         else
         {
            Steinberg::TUID controllerCID = {};
            if (compRaw->getControllerClassId(controllerCID) == Steinberg::kResultTrue)
            {
               if (v->factory->createInstance(controllerCID, Steinberg::Vst::IEditController::iid, (void**)&ctrlRaw) == Steinberg::kResultOk)
                  v->controller = ctrlRaw;
            }
         }

         Steinberg::Vst::IAudioProcessor* procRaw = nullptr;
         if (compRaw->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&procRaw) == Steinberg::kResultOk &&
             procRaw != nullptr)
         {
            v->processor = procRaw;
         }

         v->arrived.store(true, std::memory_order_release);
      });

      return h;
   }

   namespace
   {
      bool PluginVST3Configure(PluginHandle* h, std::string& outError)
      {
         PluginVST3State* v = h->vst3;
         if (v == nullptr || !v->component || !v->processor)
         {
            outError = "missing VST3 component or processor";
            return false;
         }

         if (v->processing)
         {
            v->processor->setProcessing(false);
            v->processing = false;
         }
         if (v->active)
         {
            v->component->setActive(false);
            v->active = false;
         }

         const double rate = h->sampleRate > 0.0 ? h->sampleRate : 48000.0;
         const int frames = std::min(std::max(h->maxBlockFrames, 1), 4096);

         // Bus arrangement setup
         Steinberg::Vst::SpeakerArrangement inArr = Steinberg::Vst::SpeakerArr::kStereo;
         Steinberg::Vst::SpeakerArrangement outArr = Steinberg::Vst::SpeakerArr::kStereo;

         const int inBusCount = v->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
         const int outBusCount = v->component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);

         int inChannels = (inBusCount > 0) ? 2 : 0;
         int outChannels = (outBusCount > 0) ? 2 : 0;

         if (inBusCount > 0 && outBusCount > 0)
         {
            inArr = Steinberg::Vst::SpeakerArr::kStereo;
            outArr = Steinberg::Vst::SpeakerArr::kStereo;
            if (v->processor->setBusArrangements(&inArr, 1, &outArr, 1) != Steinberg::kResultOk)
            {
               inArr = Steinberg::Vst::SpeakerArr::kMono;
               outArr = Steinberg::Vst::SpeakerArr::kMono;
               if (v->processor->setBusArrangements(&inArr, 1, &outArr, 1) == Steinberg::kResultOk)
               {
                  inChannels = 1;
                  outChannels = 1;
               }
            }
         }
         else if (outBusCount > 0)
         {
            // Instrument with 0 input busses
            inChannels = 0;
            outArr = Steinberg::Vst::SpeakerArr::kStereo;
            if (v->processor->setBusArrangements(nullptr, 0, &outArr, 1) != Steinberg::kResultOk)
            {
               outArr = Steinberg::Vst::SpeakerArr::kMono;
               v->processor->setBusArrangements(nullptr, 0, &outArr, 1);
               outChannels = 1;
            }
         }

         v->pluginInChannels = inChannels;
         v->pluginOutChannels = outChannels;

         Steinberg::Vst::ProcessSetup setup = {};
         setup.processMode = Steinberg::Vst::kRealtime;
         setup.symbolicSampleSize = Steinberg::Vst::kSample32;
         setup.maxSamplesPerBlock = frames;
         setup.sampleRate = rate;

         if (v->processor->setupProcessing(setup) != Steinberg::kResultOk)
         {
            outError = "VST3 setupProcessing failed";
            return false;
         }

         if (v->component->setActive(true) != Steinberg::kResultOk)
         {
            outError = "VST3 setActive failed";
            return false;
         }
         v->active = true;

         // setProcessing is explicitly optional in the VST3 spec - a plugin that
         // has no start/stop notion may return kNotImplemented (Kilohearts'
         // snapins do), and that is not a load failure. Treating a non-OK
         // result as fatal here rejected perfectly good plugins, so the result
         // is only traced; the component is already active, which is what
         // actually gates process().
         const Steinberg::tresult procRes = v->processor->setProcessing(true);
         if (procRes != Steinberg::kResultOk)
            VST3Trace("setProcessing(true) returned %d - continuing (optional call)", (int)procRes);
         v->processing = true;

         // Realtime scratch buffers & process data setup
         v->inScratch.assign((size_t)8 * (size_t)frames, 0.0f);
         v->outScratch.assign((size_t)8 * (size_t)frames, 0.0f);
         v->zeroScratch.assign((size_t)frames, 0.0f);

         std::memset(&v->processData, 0, sizeof(v->processData));
         v->processData.processMode = Steinberg::Vst::kRealtime;
         v->processData.symbolicSampleSize = Steinberg::Vst::kSample32;

         if (inChannels > 0)
         {
            v->processData.numInputs = 1;
            v->processData.inputs = v->inputBusBuffers;
            v->inputBusBuffers[0].numChannels = inChannels;
            v->inputBusBuffers[0].silenceFlags = 0;
            v->inputBusBuffers[0].channelBuffers32 = v->inChannelPtrs;
         }
         else
         {
            v->processData.numInputs = 0;
            v->processData.inputs = nullptr;
         }

         v->processData.numOutputs = 1;
         v->processData.outputs = v->outputBusBuffers;
         v->outputBusBuffers[0].numChannels = outChannels;
         v->outputBusBuffers[0].silenceFlags = 0;
         v->outputBusBuffers[0].channelBuffers32 = v->outChannelPtrs;

         v->processData.inputEvents = &v->inputEvents;
         v->processData.outputEvents = &v->outputEvents;
         v->processData.inputParameterChanges = &v->inputParamChanges;
         v->processData.outputParameterChanges = &v->outputParamChanges;

         std::memset(&v->processContext, 0, sizeof(v->processContext));
         v->processContext.sampleRate = rate;
         v->processContext.state = Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
         v->processData.processContext = &v->processContext;

         return true;
      }
   }

   PluginLoadState PluginVST3Poll(PluginHandle* h, std::string& outError)
   {
      if (h == nullptr || h->vst3 == nullptr)
      {
         outError = "null VST3 plugin handle";
         return PluginLoadState::Failed;
      }
      PluginVST3State* v = h->vst3;
      if (h->state != PluginLoadState::Pending)
      {
         outError = h->loadError;
         return h->state;
      }
      if (!v->arrived.load(std::memory_order_acquire))
         return PluginLoadState::Pending;

      std::string arrivedErr;
      {
         std::lock_guard<std::mutex> lock(v->arrivalMutex);
         arrivedErr = v->arrivedError;
      }
      if (!arrivedErr.empty() || !v->component || !v->processor)
      {
         h->state = PluginLoadState::Failed;
         h->loadError = !arrivedErr.empty() ? arrivedErr : "VST3 failed to instantiate";
         outError = h->loadError;
         return h->state;
      }

      // Initialize on main thread. The host context is the process-wide one on
      // purpose: a plugin keeps this pointer for as long as it is loaded, so a
      // locally-scoped IPtr here would hand every plugin a pointer that dies
      // the moment this function returns.
      Steinberg::Vst::IHostApplication* hostApp = SharedHostApplication();
      if (v->component->initialize(hostApp) != Steinberg::kResultOk)
      {
         h->state = PluginLoadState::Failed;
         h->loadError = "VST3 component initialize failed";
         outError = h->loadError;
         return h->state;
      }

      if (v->controller)
      {
         v->controller->initialize(hostApp);
         v->componentHandler = new HostComponentHandler(h);
         v->controller->setComponentHandler(v->componentHandler);

         // Connect component and controller if separate
         Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> compCP;
         Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> ctrlCP;
         if (v->component->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&compCP) == Steinberg::kResultOk &&
             v->controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&ctrlCP) == Steinberg::kResultOk)
         {
            if (compCP && ctrlCP && compCP != ctrlCP)
            {
               compCP->connect(ctrlCP);
               ctrlCP->connect(compCP);
            }
         }

         // Sync component state to controller
         MemoryStream stateStream;
         if (v->component->getState(&stateStream) == Steinberg::kResultOk)
         {
            stateStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            v->controller->setComponentState(&stateStream);
         }
      }

      if (!PluginVST3Configure(h, outError))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = outError;
         return h->state;
      }

      h->state = PluginLoadState::Ready;
      outError.clear();
      return h->state;
   }

   bool PluginVST3Prepare(PluginHandle* h, double sampleRate, int maxBlockFrames, std::string& outError)
   {
      if (h == nullptr || h->vst3 == nullptr || h->state != PluginLoadState::Ready)
      {
         outError = "plugin not ready";
         return false;
      }
      const int frames = maxBlockFrames > 0 ? maxBlockFrames : h->maxBlockFrames;
      if (sampleRate <= 0.0)
         return true;
      if (std::abs(sampleRate - h->sampleRate) < 1e-6 && frames == h->maxBlockFrames && h->vst3->active)
         return true;

      h->sampleRate = sampleRate;
      h->maxBlockFrames = frames;
      h->vst3->sampleRate = sampleRate;
      h->vst3->maxBlockFrames = frames;

      if (!PluginVST3Configure(h, outError))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = outError;
         return false;
      }
      return true;
   }

   void PluginVST3Destroy(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;

      PluginVST3State* v = h->vst3;

      // Close editor window
      if (v->editorWindow != nil)
      {
         NSWindow* win = v->editorWindow;
         win.delegate = nil;
         v->editorDelegate = nil;
         v->editorWindow = nil;
         SetPluginEditorOpen(h, false);
         [win close];
      }

      if (v->plugView)
      {
         v->plugView->removed();
         v->plugView = nullptr;
      }

      if (v->componentHandler)
      {
         v->componentHandler->detach();
         v->componentHandler = nullptr;
      }

      // Wait out any in-flight render
      for (int spins = 0; spins < 100000 && v->inRender.load(std::memory_order_acquire); spins++)
      {
      }

      if (v->processor && v->processing)
      {
         v->processor->setProcessing(false);
         v->processing = false;
      }
      if (v->component && v->active)
      {
         v->component->setActive(false);
         v->active = false;
      }

      if (v->controller)
      {
         v->controller->setComponentHandler(nullptr);
         v->controller->terminate();
         v->controller = nullptr;
      }
      if (v->component)
      {
         v->component->terminate();
         v->component = nullptr;
      }
      v->processor = nullptr;
      v->factory = nullptr;

      if (v->bundle != nullptr)
      {
         UnloadVST3Bundle(v->bundle);
         v->bundle = nullptr;
      }

      delete v;
      h->vst3 = nullptr;
      delete h;
   }

   void PluginVST3Render(PluginHandle* h, const float* const* in, int inChannels,
                         float* const* out, int outChannels, int numFrames)
   {
      if (h == nullptr || h->vst3 == nullptr || out == nullptr || numFrames <= 0)
         return;

      PluginVST3State* v = h->vst3;
      if (!v->processor || !v->processing)
         return;

      v->inRender.store(true, std::memory_order_release);

      const int pluginIn = v->pluginInChannels;
      const int pluginOut = v->pluginOutChannels;
      const int frames = numFrames > v->maxBlockFrames ? v->maxBlockFrames : numFrames;

      v->processData.numSamples = frames;
      v->processContext.projectTimeSamples = v->sampleTime;
      v->sampleTime += frames;

      // Inputs
      if (pluginIn > 0)
      {
         for (int ch = 0; ch < pluginIn; ch++)
         {
            const float* src = (in != nullptr && inChannels > 0)
                                  ? in[ch < inChannels ? ch : inChannels - 1]
                                  : nullptr;
            v->inChannelPtrs[ch] = const_cast<float*>(src != nullptr ? src : v->zeroScratch.data());
         }
      }

      // Outputs: direct if channel counts match, otherwise render to scratch
      const bool direct = (pluginOut == outChannels);
      float* outScratchBase = v->outScratch.data();
      for (int ch = 0; ch < pluginOut; ch++)
      {
         v->outChannelPtrs[ch] = direct ? out[ch] : (outScratchBase + (size_t)ch * (size_t)v->maxBlockFrames);
      }

      const Steinberg::tresult res = v->processor->process(v->processData);

      if (res != Steinberg::kResultOk)
      {
         for (int ch = 0; ch < outChannels; ch++)
            if (out[ch] != nullptr)
               std::memset(out[ch], 0, (size_t)frames * sizeof(float));
         v->inputEvents.clear();
         v->inputParamChanges.clear();
         v->inRender.store(false, std::memory_order_release);
         return;
      }

      if (!direct)
      {
         for (int ch = 0; ch < outChannels; ch++)
         {
            if (out[ch] == nullptr)
               continue;
            const int srcCh = ch < pluginOut ? ch : pluginOut - 1;
            std::memcpy(out[ch], outScratchBase + (size_t)srcCh * (size_t)v->maxBlockFrames,
                        (size_t)frames * sizeof(float));
         }
      }

      // Zero any tail channels
      for (int ch = pluginOut; ch < outChannels; ch++)
         if (out[ch] != nullptr && !direct)
            std::memset(out[ch], 0, (size_t)frames * sizeof(float));

      // Clear input queues for next render block
      v->inputEvents.clear();
      v->inputParamChanges.clear();

      v->inRender.store(false, std::memory_order_release);
   }

   void PluginVST3ScheduleMIDIEvent(PluginHandle* h, int frameOffset, const unsigned char* bytes, int byteCount)
   {
      if (h == nullptr || h->vst3 == nullptr || bytes == nullptr || byteCount <= 0)
         return;

      PluginVST3State* v = h->vst3;
      const unsigned char status = bytes[0] & 0xF0;
      const unsigned char channel = bytes[0] & 0x0F;
      const unsigned char note = (byteCount > 1) ? (bytes[1] & 0x7F) : 0;
      const unsigned char vel = (byteCount > 2) ? (bytes[2] & 0x7F) : 0;

      if (status == 0x90 && vel > 0)
      {
         Steinberg::Vst::Event e = {};
         e.type = Steinberg::Vst::Event::kNoteOnEvent;
         e.sampleOffset = frameOffset;
         e.noteOn.channel = channel;
         e.noteOn.pitch = (Steinberg::int16)note;
         e.noteOn.velocity = (float)vel / 127.0f;
         e.noteOn.length = 0;
         e.noteOn.tuning = 0.0f;
         e.noteOn.noteId = -1;
         v->inputEvents.addEvent(e);
      }
      else if (status == 0x80 || (status == 0x90 && vel == 0))
      {
         Steinberg::Vst::Event e = {};
         e.type = Steinberg::Vst::Event::kNoteOffEvent;
         e.sampleOffset = frameOffset;
         e.noteOff.channel = channel;
         e.noteOff.pitch = (Steinberg::int16)note;
         e.noteOff.velocity = (float)vel / 127.0f;
         e.noteOff.tuning = 0.0f;
         e.noteOff.noteId = -1;
         v->inputEvents.addEvent(e);
      }
   }

   // ------------------------------------------------------------------------
   // Parameter Handling
   // ------------------------------------------------------------------------
   // Design decision: VST3 parameter ranges are normalized 0.0 .. 1.0.
   // PluginParamInfo's minValue is set to 0.0f and maxValue to 1.0f, with
   // defaultValue from defaultNormalizedValue. This provides exact round-trip
   // normalization across all VST3 parameters without non-linear skew.

   int PluginVST3ParameterCount(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return 0;
      return (int)h->vst3->controller->getParameterCount();
   }

   namespace
   {
      void FillVST3ParamInfo(const Steinberg::Vst::ParameterInfo& p, PluginParamInfo& out)
      {
         out.address = (unsigned long long)p.id;
         out.displayName = UTF16ToUTF8(p.title);
         if (out.displayName.empty())
            out.displayName = UTF16ToUTF8(p.shortTitle);
         out.minValue = 0.0f;
         out.maxValue = 1.0f;
         out.defaultValue = (float)p.defaultNormalizedValue;
         out.unit = UTF16ToUTF8(p.units);
      }
   }

   bool PluginVST3ParameterInfo(PluginHandle* h, int index, PluginParamInfo& out)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return false;
      Steinberg::Vst::ParameterInfo info = {};
      if (h->vst3->controller->getParameterInfo(index, info) != Steinberg::kResultOk)
         return false;
      FillVST3ParamInfo(info, out);
      return true;
   }

   bool PluginVST3ParameterInfoByAddress(PluginHandle* h, unsigned long long address, PluginParamInfo& out)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return false;
      const int count = (int)h->vst3->controller->getParameterCount();
      for (int i = 0; i < count; i++)
      {
         Steinberg::Vst::ParameterInfo info = {};
         if (h->vst3->controller->getParameterInfo(i, info) == Steinberg::kResultOk)
         {
            if ((unsigned long long)info.id == address)
            {
               FillVST3ParamInfo(info, out);
               return true;
            }
         }
      }
      return false;
   }

   void PluginVST3SetParameter(PluginHandle* h, unsigned long long address, float value)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return;

      const Steinberg::Vst::ParamID pid = (Steinberg::Vst::ParamID)address;
      const Steinberg::Vst::ParamValue normVal = (Steinberg::Vst::ParamValue)std::clamp(value, 0.0f, 1.0f);
      h->vst3->controller->setParamNormalized(pid, normVal);

      // Also push to inputParamChanges for realtime processor
      Steinberg::int32 queueIdx = 0;
      Steinberg::Vst::IParamValueQueue* queue = h->vst3->inputParamChanges.addParameterData(pid, queueIdx);
      if (queue != nullptr)
      {
         Steinberg::int32 ptIdx = 0;
         queue->addPoint(0, normVal, ptIdx);
      }
   }

   bool PluginVST3GetParameter(PluginHandle* h, unsigned long long address, float& outValue)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller)
         return false;
      outValue = (float)h->vst3->controller->getParamNormalized((Steinberg::Vst::ParamID)address);
      return true;
   }

   void PluginVST3BeginLearn(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      h->vst3->learnedValid.store(false, std::memory_order_relaxed);
      h->vst3->learning.store(true, std::memory_order_release);
   }

   void PluginVST3EndLearn(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      h->vst3->learning.store(false, std::memory_order_release);
      h->vst3->learnedValid.store(false, std::memory_order_relaxed);
   }

   bool PluginVST3PollLearned(PluginHandle* h, unsigned long long& outAddress)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->learnedValid.load(std::memory_order_acquire))
         return false;
      outAddress = h->vst3->learnedAddress.load(std::memory_order_relaxed);
      h->vst3->learnedValid.store(false, std::memory_order_relaxed);
      return true;
   }

   // ------------------------------------------------------------------------
   // Editor Window
   // ------------------------------------------------------------------------

   bool PluginVST3OpenEditor(PluginHandle* h, std::string& outError)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->controller || h->state != PluginLoadState::Ready)
      {
         outError = "plugin not loaded";
         return false;
      }
      PluginVST3State* v = h->vst3;

      if (v->editorWindow != nil)
      {
         [v->editorWindow makeKeyAndOrderFront:nil];
         SetPluginEditorOpen(h, true);
         return true;
      }

      if (!v->plugView)
      {
         Steinberg::IPlugView* view = v->controller->createView(Steinberg::Vst::ViewType::kEditor);
         if (view == nullptr)
         {
            outError = "plugin has no custom GUI editor";
            return false;
         }
         v->plugView = view;
      }

      Steinberg::ViewRect rect = {};
      v->plugView->getSize(&rect);
      CGFloat width = (CGFloat)(rect.right - rect.left);
      CGFloat height = (CGFloat)(rect.bottom - rect.top);
      if (width < 120.0 || height < 80.0)
      {
         width = 640.0;
         height = 420.0;
      }

      NSWindow* window = [[NSWindow alloc]
          initWithContentRect:NSMakeRect(0, 0, width, height)
                    styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                               NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                      backing:NSBackingStoreBuffered
                        defer:NO];
      window.releasedWhenClosed = NO;
      window.title = [NSString stringWithUTF8String:h->desc.name.c_str()];

      NSView* containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
      window.contentView = containerView;

      v->plugView->attached((__bridge void*)containerView, Steinberg::kPlatformTypeNSView);

      InfinitePluginEditorDelegate* delegate = [[InfinitePluginEditorDelegate alloc] init];
      delegate.handle = (void*)h;
      window.delegate = delegate;

      [window center];
      [window makeKeyAndOrderFront:nil];

      v->editorWindow = window;
      v->editorDelegate = delegate;
      SetPluginEditorOpen(h, true);
      return true;
   }

   void PluginVST3CloseEditor(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr || h->vst3->editorWindow == nil)
         return;
      [h->vst3->editorWindow orderOut:nil];
      SetPluginEditorOpen(h, false);
   }

   bool PluginVST3EditorIsOpen(PluginHandle* h)
   {
      return h != nullptr && h->vst3 != nullptr && h->vst3->editorOpen.load(std::memory_order_acquire);
   }

   // ------------------------------------------------------------------------
   // State Save & Restore
   // ------------------------------------------------------------------------

   bool PluginVST3SaveState(PluginHandle* h, std::string& outBase64)
   {
      outBase64.clear();
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->component)
         return false;

      MemoryStream compStream;
      if (h->vst3->component->getState(&compStream) != Steinberg::kResultOk)
         return false;

      MemoryStream ctrlStream;
      if (h->vst3->controller)
         h->vst3->controller->getState(&ctrlStream);

      const uint64_t compSize = (uint64_t)compStream.getSize();
      const uint64_t ctrlSize = (uint64_t)ctrlStream.getSize();

      std::vector<uint8_t> payload;
      payload.resize(sizeof(uint64_t) + compSize + sizeof(uint64_t) + ctrlSize);

      uint8_t* ptr = payload.data();
      std::memcpy(ptr, &compSize, sizeof(uint64_t));
      ptr += sizeof(uint64_t);
      if (compSize > 0)
      {
         std::memcpy(ptr, compStream.getBuffer().data(), (size_t)compSize);
         ptr += compSize;
      }
      std::memcpy(ptr, &ctrlSize, sizeof(uint64_t));
      ptr += sizeof(uint64_t);
      if (ctrlSize > 0)
      {
         std::memcpy(ptr, ctrlStream.getBuffer().data(), (size_t)ctrlSize);
      }

      @autoreleasepool
      {
         NSData* data = [NSData dataWithBytes:payload.data() length:payload.size()];
         NSString* encoded = [data base64EncodedStringWithOptions:0];
         if (encoded != nil)
            outBase64 = std::string([encoded UTF8String]);
      }
      return !outBase64.empty();
   }

   bool PluginVST3RestoreState(PluginHandle* h, const std::string& base64)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->component || base64.empty())
         return false;

      std::vector<uint8_t> payload;
      @autoreleasepool
      {
         NSString* encoded = [NSString stringWithUTF8String:base64.c_str()];
         if (encoded == nil)
            return false;
         NSData* data = [[NSData alloc] initWithBase64EncodedString:encoded options:0];
         if (data == nil)
            return false;
         payload.assign((const uint8_t*)[data bytes], (const uint8_t*)[data bytes] + [data length]);
      }

      if (payload.size() < sizeof(uint64_t) * 2)
         return false;

      const uint8_t* ptr = payload.data();
      const uint8_t* end = payload.data() + payload.size();

      uint64_t compSize = 0;
      std::memcpy(&compSize, ptr, sizeof(uint64_t));
      ptr += sizeof(uint64_t);

      if (ptr + compSize > end)
         return false;
      if (compSize > 0)
      {
         MemoryStream compStream(ptr, (size_t)compSize);
         h->vst3->component->setState(&compStream);
         if (h->vst3->controller)
         {
            compStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            h->vst3->controller->setComponentState(&compStream);
         }
         ptr += compSize;
      }

      if (ptr + sizeof(uint64_t) <= end)
      {
         uint64_t ctrlSize = 0;
         std::memcpy(&ctrlSize, ptr, sizeof(uint64_t));
         ptr += sizeof(uint64_t);
         if (ctrlSize > 0 && ptr + ctrlSize <= end && h->vst3->controller)
         {
            MemoryStream ctrlStream(ptr, (size_t)ctrlSize);
            h->vst3->controller->setState(&ctrlStream);
         }
      }

      return true;
   }
}

#endif // INFINITE_ENABLE_VST3

// Windows VST3 hosting - the portable ~75% of PluginVST3.mm (host glue
// classes, create/init/process/parameter/state logic) ported essentially
// unchanged, plus the genuinely Windows-specific pieces that are actually
// needed for anything to instantiate at all (DLL loading, UTF-16 handling),
// plus real editor-window (HWND) hosting with the same Tier-2 SEH crash-guard
// discipline used for state save/restore.
//
// Deliberately NOT ported in this phase (see the plan for the follow-up
// sessions each of these becomes):
//   - SEH crash guarding of realtime/instantiation calls (Tier 3 - not
//     guardable this way; would need out-of-process hosting).
//   - Out-of-process scanning, sentinel/blocklist persistence. Enumeration
//     stays routed through PluginHostWin.cpp's existing no-op, so bundle
//     resolution below only ever succeeds via desc.path or a same-session
//     cache hit - never a fresh disk scan. This is a known, called-out gap,
//     not a silent regression.

#include "PluginVST3.h"

#if INFINITE_ENABLE_VST3

#include "PluginHandleInternalWin.h"
#include "WinCommon.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace
{
   namespace fs = std::filesystem;

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
   // UID and string utilities - identical to PluginVST3.mm, zero OS calls.
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

   // Steinberg::Vst::TChar and wchar_t are both UTF-16 on Windows, so this is
   // a straight width copy through WinCommon's UTF-8 <-> UTF-16 helpers
   // rather than the NSString round-trip PluginVST3.mm uses.
   static_assert(sizeof(Steinberg::Vst::TChar) == sizeof(wchar_t),
                 "Steinberg::Vst::TChar must be UTF-16 to alias wchar_t on Windows");

   std::string UTF16ToUTF8(const Steinberg::Vst::TChar* str)
   {
      if (str == nullptr)
         return std::string();
      return WinCommon::WideToUtf8(std::wstring(reinterpret_cast<const wchar_t*>(str)));
   }

   void UTF8ToUTF16(const std::string& utf8, Steinberg::Vst::TChar* outStr, int maxChars)
   {
      if (outStr == nullptr || maxChars <= 0)
         return;
      const std::wstring wide = WinCommon::Utf8ToWide(utf8);
      const size_t len = std::min(wide.size(), (size_t)(maxChars - 1));
      std::memcpy(outStr, wide.data(), len * sizeof(Steinberg::Vst::TChar));
      outStr[len] = 0;
   }

   // ------------------------------------------------------------------------
   // Minimal base64, needed only for state save/restore blobs (no existing
   // helper elsewhere in the codebase to reuse).
   // ------------------------------------------------------------------------

   std::string Base64Encode(const std::vector<uint8_t>& data)
   {
      static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string out;
      out.reserve(((data.size() + 2) / 3) * 4);
      size_t i = 0;
      while (i + 3 <= data.size())
      {
         const uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
         out.push_back(kTable[(n >> 18) & 0x3F]);
         out.push_back(kTable[(n >> 12) & 0x3F]);
         out.push_back(kTable[(n >> 6) & 0x3F]);
         out.push_back(kTable[n & 0x3F]);
         i += 3;
      }
      const size_t remaining = data.size() - i;
      if (remaining == 1)
      {
         const uint32_t n = data[i] << 16;
         out.push_back(kTable[(n >> 18) & 0x3F]);
         out.push_back(kTable[(n >> 12) & 0x3F]);
         out.push_back('=');
         out.push_back('=');
      }
      else if (remaining == 2)
      {
         const uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
         out.push_back(kTable[(n >> 18) & 0x3F]);
         out.push_back(kTable[(n >> 12) & 0x3F]);
         out.push_back(kTable[(n >> 6) & 0x3F]);
         out.push_back('=');
      }
      return out;
   }

   bool Base64Decode(const std::string& in, std::vector<uint8_t>& out)
   {
      auto decodeChar = [](char c) -> int
      {
         if (c >= 'A' && c <= 'Z') return c - 'A';
         if (c >= 'a' && c <= 'z') return c - 'a' + 26;
         if (c >= '0' && c <= '9') return c - '0' + 52;
         if (c == '+') return 62;
         if (c == '/') return 63;
         return -1;
      };
      out.clear();
      int vals[4];
      int count = 0;
      for (char c : in)
      {
         if (c == '=' || c == '\r' || c == '\n')
            continue;
         const int v = decodeChar(c);
         if (v < 0)
            return false;
         vals[count++] = v;
         if (count == 4)
         {
            const uint32_t n = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6) | vals[3];
            out.push_back((uint8_t)((n >> 16) & 0xFF));
            out.push_back((uint8_t)((n >> 8) & 0xFF));
            out.push_back((uint8_t)(n & 0xFF));
            count = 0;
         }
      }
      if (count == 3)
      {
         const uint32_t n = (vals[0] << 18) | (vals[1] << 12) | (vals[2] << 6);
         out.push_back((uint8_t)((n >> 16) & 0xFF));
         out.push_back((uint8_t)((n >> 8) & 0xFF));
      }
      else if (count == 2)
      {
         const uint32_t n = (vals[0] << 18) | (vals[1] << 12);
         out.push_back((uint8_t)((n >> 16) & 0xFF));
      }
      else if (count == 1)
      {
         return false; // malformed
      }
      return true;
   }
}

namespace Platform
{
   // Identifier -> bundle path cache, exactly as PluginVST3.mm's. Real disk
   // scanning isn't ported yet (see file header), so this only ever gets
   // populated from a desc.path that resolved successfully - still useful
   // for re-resolving the same plugin later in the same process run.
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
      return it != gVST3BundleMap.end() ? it->second : std::string();
   }
}

namespace
{
   // ------------------------------------------------------------------------
   // MemoryStream for IBStream state save/restore - identical to PluginVST3.mm.
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
   // Host Application Context - identical to PluginVST3.mm.
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

      Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void** obj) override;

   private:
      std::atomic<uint32_t> mRefCount { 1 };
   };

   class HostAttributeList : public Steinberg::Vst::IAttributeList
   {
   public:
      using AttrID = Steinberg::Vst::IAttributeList::AttrID;

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IAttributeList::iid) ||
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

      Steinberg::tresult PLUGIN_API setInt(AttrID aid, Steinberg::int64 value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kInt;
         a.intValue = value;
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getInt(AttrID aid, Steinberg::int64& value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kInt)
            return Steinberg::kResultFalse;
         value = it->second.intValue;
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API setFloat(AttrID aid, double value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kFloat;
         a.floatValue = value;
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getFloat(AttrID aid, double& value) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kFloat)
            return Steinberg::kResultFalse;
         value = it->second.floatValue;
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API setString(AttrID aid, const Steinberg::Vst::TChar* string) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kString;
         if (string != nullptr)
         {
            size_t len = 0;
            while (string[len] != 0)
               len++;
            a.stringValue.assign(string, string + len);
         }
         a.stringValue.push_back(0);
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getString(AttrID aid, Steinberg::Vst::TChar* string, Steinberg::uint32 sizeInBytes) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kString)
            return Steinberg::kResultFalse;
         const size_t haveBytes = it->second.stringValue.size() * sizeof(Steinberg::Vst::TChar);
         const size_t copyBytes = std::min<size_t>(haveBytes, sizeInBytes);
         std::memcpy(string, it->second.stringValue.data(), copyBytes);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API setBinary(AttrID aid, const void* data, Steinberg::uint32 sizeInBytes) override
      {
         if (!aid)
            return Steinberg::kInvalidArgument;
         Attribute a;
         a.type = Attribute::Type::kBinary;
         const uint8_t* p = static_cast<const uint8_t*>(data);
         a.binaryValue.assign(p, p + sizeInBytes);
         mAttrs[aid] = std::move(a);
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API getBinary(AttrID aid, const void*& data, Steinberg::uint32& sizeInBytes) override
      {
         if (!aid)
         {
            sizeInBytes = 0;
            return Steinberg::kInvalidArgument;
         }
         auto it = mAttrs.find(aid);
         if (it == mAttrs.end() || it->second.type != Attribute::Type::kBinary)
         {
            sizeInBytes = 0;
            return Steinberg::kResultFalse;
         }
         data = it->second.binaryValue.data();
         sizeInBytes = (Steinberg::uint32)it->second.binaryValue.size();
         return Steinberg::kResultTrue;
      }

   private:
      struct Attribute
      {
         enum class Type { kInt, kFloat, kString, kBinary } type = Type::kInt;
         Steinberg::int64 intValue = 0;
         double floatValue = 0.0;
         std::vector<Steinberg::Vst::TChar> stringValue;
         std::vector<uint8_t> binaryValue;
      };

      std::atomic<uint32_t> mRefCount { 1 };
      std::map<std::string, Attribute> mAttrs;
   };

   class HostMessage : public Steinberg::Vst::IMessage
   {
   public:
      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IMessage::iid) ||
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

      Steinberg::FIDString PLUGIN_API getMessageID() override
      {
         return mMessageId.empty() ? nullptr : mMessageId.c_str();
      }

      void PLUGIN_API setMessageID(Steinberg::FIDString mid) override
      {
         mMessageId = (mid != nullptr) ? mid : "";
      }

      Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override
      {
         if (!mAttributes)
            mAttributes = Steinberg::owned(new HostAttributeList());
         return mAttributes.get();
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::string mMessageId;
      Steinberg::IPtr<Steinberg::Vst::IAttributeList> mAttributes;
   };

   Steinberg::tresult PLUGIN_API HostApplication::createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void** obj)
   {
      if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IMessage::iid) &&
          Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IMessage::iid))
      {
         *obj = new HostMessage();
         return Steinberg::kResultTrue;
      }
      if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IAttributeList::iid) &&
          Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IAttributeList::iid))
      {
         *obj = new HostAttributeList();
         return Steinberg::kResultTrue;
      }
      *obj = nullptr;
      return Steinberg::kNoInterface;
   }

   Steinberg::Vst::IHostApplication* SharedHostApplication()
   {
      static HostApplication* instance = new HostApplication();
      return instance;
   }

   // ------------------------------------------------------------------------
   // Real-time safe event list / parameter changes - identical to PluginVST3.mm.
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

      void clear() { mCount.store(0, std::memory_order_relaxed); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::atomic<int> mCount { 0 };
      Steinberg::Vst::Event mEvents[kMaxEvents] = {};
   };

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

      void clear() { mPointCount.store(0, std::memory_order_relaxed); }

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

      void clear() { mQueueCount.store(0, std::memory_order_relaxed); }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      std::atomic<int> mQueueCount { 0 };
      HostParamValueQueue mQueues[kMaxQueues];
   };

   // ------------------------------------------------------------------------
   // Host Component Handler - identical to PluginVST3.mm.
   // ------------------------------------------------------------------------

   class HostComponentHandler : public Steinberg::Vst::IComponentHandler,
                                public Steinberg::Vst::IComponentHandler2,
                                public Steinberg::Vst::IComponentHandlerBusActivation
   {
   public:
      explicit HostComponentHandler(Platform::PluginHandle* handle) : mHandle(handle) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
             Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
         {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
         }
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler2::iid))
         {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
            return Steinberg::kResultOk;
         }
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandlerBusActivation::iid))
         {
            addRef();
            *obj = static_cast<Steinberg::Vst::IComponentHandlerBusActivation*>(this);
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

      Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override
      {
         (void)state;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString name) override
      {
         (void)name;
         return Steinberg::kResultOk;
      }

      Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
      Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }

      Steinberg::tresult PLUGIN_API requestBusActivation(Steinberg::Vst::MediaType type,
                                                         Steinberg::Vst::BusDirection dir,
                                                         Steinberg::int32 index,
                                                         Steinberg::TBool state) override
      {
         (void)type;
         (void)dir;
         (void)index;
         (void)state;
         return Steinberg::kResultOk;
      }

      void detach() { mHandle = nullptr; }

   private:
      void RecordTouch(Steinberg::Vst::ParamID id);

      std::atomic<uint32_t> mRefCount { 1 };
      Platform::PluginHandle* mHandle = nullptr;
   };

   // ------------------------------------------------------------------------
   // Host-side connection proxy.
   //
   // On macOS this marshals notify() onto the main thread via
   // dispatch_async(dispatch_get_main_queue()). Windows has no equivalent
   // main-thread-post primitive yet (that only becomes necessary once the
   // editor phase lands and a plugin's GUI can legitimately push state from
   // the audio thread). For this foundation phase notify() is called
   // synchronously on whichever thread sent it - a documented simplification,
   // not an oversight - to be replaced with a real main-thread queue in the
   // editor-window follow-up session.
   // ------------------------------------------------------------------------
   class HostConnectionProxy : public Steinberg::Vst::IConnectionPoint
   {
   public:
      explicit HostConnectionProxy(Steinberg::Vst::IConnectionPoint* srcPoint) : mSrc(srcPoint) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IConnectionPoint::iid) ||
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

      Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override
      {
         if (other == nullptr)
            return Steinberg::kInvalidArgument;
         if (mDst || !mSrc)
            return Steinberg::kResultFalse;
         mDst = other;
         Steinberg::tresult res = mSrc->connect(this);
         if (res != Steinberg::kResultTrue)
            mDst = nullptr;
         return res;
      }

      Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override
      {
         if (other == nullptr)
            return Steinberg::kInvalidArgument;
         if (other != mDst.get())
            return Steinberg::kInvalidArgument;
         if (mSrc)
            mSrc->disconnect(this);
         mDst = nullptr;
         return Steinberg::kResultTrue;
      }

      Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override
      {
         if (!mDst || message == nullptr)
            return Steinberg::kResultFalse;
         mDst->notify(message);
         return Steinberg::kResultTrue;
      }

      void DisconnectFromSource()
      {
         if (mDst)
            disconnect(mDst.get());
      }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> mSrc;
      Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> mDst;
   };

   // ------------------------------------------------------------------------
   // Host-side IPlugFrame - required so a plugin's resizable GUI can ask us
   // to resize its window. Without this, resizeView() has no host object to
   // call, and a resizable-GUI plugin either can't resize or crashes trying.
   // ------------------------------------------------------------------------
   class HostPlugFrame : public Steinberg::IPlugFrame
   {
   public:
      explicit HostPlugFrame(Platform::PluginHandle* handle) : mHandle(handle) {}

      Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
      {
         if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid) ||
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

      Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

      void detach() { mHandle = nullptr; }

   private:
      std::atomic<uint32_t> mRefCount { 1 };
      Platform::PluginHandle* mHandle = nullptr;
   };
}

namespace Platform
{
   // ------------------------------------------------------------------------
   // Internal VST3 state - Windows equivalent of PluginVST3.mm's
   // PluginVST3State. Same shape minus the editor-window/NSWindow fields
   // (editor hosting is stubbed this phase) and minus the sentinel/blocklist
   // fields (scanning is deferred).
   // ------------------------------------------------------------------------
   struct PluginVST3State
   {
      HMODULE module = nullptr;
      Steinberg::IPtr<Steinberg::IPluginFactory> factory;
      Steinberg::IPtr<Steinberg::Vst::IComponent> component;
      Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
      Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;
      Steinberg::IPtr<Steinberg::IPlugView> plugView;
      Steinberg::IPtr<HostComponentHandler> componentHandler;

      Steinberg::IPtr<HostConnectionProxy> compToCtrlProxy;
      Steinberg::IPtr<HostConnectionProxy> ctrlToCompProxy;

      // On macOS this is set from a background dispatch_async block, polled
      // by PluginVST3Poll. Here PluginVST3Create resolves and instantiates
      // synchronously before returning (see PluginVST3Create below for why),
      // so this is always true by the time the handle is returned - kept
      // anyway so PluginVST3Poll's logic below matches PluginVST3.mm's
      // unchanged.
      std::mutex arrivalMutex;
      std::atomic<bool> arrived { false };
      std::string arrivedError;

      double sampleRate = 0.0;
      int maxBlockFrames = 0;
      int pluginInChannels = 0;
      int pluginOutChannels = 0;
      bool active = false;
      bool processing = false;

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

      std::atomic<bool> learning { false };
      std::atomic<unsigned long long> learnedAddress { 0 };
      std::atomic<bool> learnedValid { false };

      std::atomic<bool> inRender { false };

      // Set once a crash guard catches this instance faulting inside
      // getState/setState. See RunPluginCallGuarded below.
      std::atomic<bool> stateCallsUnstable { false };

      // Editor window state. Windows equivalent of the NSWindow/delegate
      // pair on PluginVST3.mm's PluginHandle - kept here instead, on the
      // opaque VST3-only state, because the process-wide open-editor
      // counter (gWinOpenPluginEditorCount, below) lives in this file
      // rather than in a shared Platform.mm the way Mac's does.
      HWND editorHwnd = nullptr;
      Steinberg::IPtr<HostPlugFrame> plugFrame;
      std::atomic<bool> editorOpen { false };

      // Distinguishes the host resizing the HWND because the plugin asked
      // (HostPlugFrame::resizeView) from the user dragging the window's own
      // resize border - SetWindowPos synchronously delivers WM_SIZE back
      // into the same WndProc before it returns, so without this flag
      // onSize() would be called twice for one logical resize.
      bool resizingFromPlugin = false;

      // Set once a crash guard catches this instance faulting inside
      // createView/getSize/attached/onSize. Independent from
      // stateCallsUnstable - a plugin can have a broken editor and a
      // perfectly fine getState(), or vice versa.
      std::atomic<bool> editorUnstable { false };
   };

   // Defined further down, alongside the rest of the crash-guard machinery;
   // forward-declared here (inside the same unnamed namespace nested in
   // Platform, which is shared across the whole translation unit) so it can
   // be used ahead of that point - by PluginVST3Destroy below, and by
   // HostPlugFrame::resizeView, which needs to call it but lives in a
   // different (global-scope) unnamed namespace and so must reach this one
   // through the qualified name Platform::RunPluginCallGuarded.
   namespace
   {
      bool RunPluginCallGuarded(const char* what, PluginHandle* h, const std::function<void()>& fn);
   }
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

   Steinberg::tresult PLUGIN_API HostPlugFrame::resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize)
   {
      if (mHandle == nullptr || mHandle->vst3 == nullptr || newSize == nullptr)
         return Steinberg::kResultFalse;
      Platform::PluginVST3State* v = mHandle->vst3;
      // Editor may already be gone (window closed, or this call landed
      // after teardown) - the plugin's timer can fire against
      // half-torn-down state.
      if (v->editorHwnd == nullptr || v->plugView != view)
         return Steinberg::kResultFalse;

      const int width = newSize->right - newSize->left;
      const int height = newSize->bottom - newSize->top;
      if (width <= 0 || height <= 0)
         return Steinberg::kResultFalse;

      RECT rect = { 0, 0, width, height };
      const DWORD style = (DWORD)GetWindowLongPtrW(v->editorHwnd, GWL_STYLE);
      const DWORD exStyle = (DWORD)GetWindowLongPtrW(v->editorHwnd, GWL_EXSTYLE);
      AdjustWindowRectEx(&rect, style, FALSE, exStyle);

      v->resizingFromPlugin = true;
      SetWindowPos(v->editorHwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

      // Per IPlugFrame::resizeView's own doc comment: the host must call
      // IPlugView::onSize() after handling the resize.
      const bool ok = Platform::RunPluginCallGuarded("onSize", mHandle, [&] { view->onSize(newSize); });
      v->resizingFromPlugin = false;
      if (!ok)
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         return Steinberg::kResultFalse;
      }
      return Steinberg::kResultTrue;
   }

   using GetPluginFactoryProc = Steinberg::IPluginFactory* (*)();
   using InitDllProc = bool (*)();
   using ExitDllProc = bool (*)();

   // Mechanical swap for CFBundleLoadExecutable/CFBundleGetFunctionPointerForName:
   // a VST3 module on Windows is a DLL, either bare (older single-file .vst3)
   // or inside the standard bundle-folder layout
   // "<name>.vst3/Contents/x86_64-win/<name>.vst3". InitDll/ExitDll are the
   // exact Windows-side equivalents of bundleEntry/bundleExit - optional, but
   // skipped for the same reason it isn't optional on Mac: plugins that load
   // resources relative to their own module fail at instantiation without it.
   void UnloadVST3Module(HMODULE module)
   {
      if (module == nullptr)
         return;
      if (ExitDllProc exitDll = (ExitDllProc)GetProcAddress(module, "ExitDll"))
         exitDll();
      FreeLibrary(module);
   }

   HMODULE LoadVST3Module(const std::string& bundlePath, Steinberg::IPluginFactory** outFactory)
   {
      if (outFactory != nullptr)
         *outFactory = nullptr;
      if (bundlePath.empty())
         return nullptr;

      std::error_code ec;
      std::string dllPath = bundlePath;
      if (fs::is_directory(bundlePath, ec))
      {
         dllPath = bundlePath + "\\Contents\\x86_64-win\\" +
                   fs::path(bundlePath).stem().string() + ".vst3";
      }

      HMODULE module = LoadLibraryW(WinCommon::Utf8ToWide(dllPath).c_str());
      if (module == nullptr)
      {
         VST3Trace("LoadLibraryW failed: %s", dllPath.c_str());
         return nullptr;
      }

      if (InitDllProc initDll = (InitDllProc)GetProcAddress(module, "InitDll"))
      {
         if (!initDll())
         {
            VST3Trace("InitDll failed: %s", dllPath.c_str());
            FreeLibrary(module);
            return nullptr;
         }
      }

      GetPluginFactoryProc getFactory = (GetPluginFactoryProc)GetProcAddress(module, "GetPluginFactory");
      if (getFactory == nullptr)
      {
         VST3Trace("no GetPluginFactory export: %s", dllPath.c_str());
         UnloadVST3Module(module);
         return nullptr;
      }

      Steinberg::IPluginFactory* factory = getFactory();
      if (factory == nullptr)
      {
         VST3Trace("GetPluginFactory returned null: %s", dllPath.c_str());
         UnloadVST3Module(module);
         return nullptr;
      }

      if (outFactory != nullptr)
         *outFactory = factory;
      return module;
   }

   // ------------------------------------------------------------------------
   // Editor window plumbing.
   //
   // Process-wide count of open plugin editor windows, kept in lockstep with
   // every PluginVST3State's editorOpen flag (see SetPluginEditorOpenWin
   // below) so PluginVST3AnyEditorOpen() is a cheap atomic read rather than a
   // walk over every handle.
   // ------------------------------------------------------------------------
   std::atomic<int> gWinOpenPluginEditorCount { 0 };

   void SetPluginEditorOpenWin(Platform::PluginHandle* h, bool open)
   {
      if (h == nullptr || h->vst3 == nullptr)
         return;
      bool was = h->vst3->editorOpen.exchange(open, std::memory_order_acq_rel);
      if (was == open)
         return;
      if (open)
         gWinOpenPluginEditorCount.fetch_add(1, std::memory_order_relaxed);
      else
         gWinOpenPluginEditorCount.fetch_sub(1, std::memory_order_relaxed);
   }

   constexpr wchar_t kEditorWindowClassName[] = L"InfinitePluginEditorWindow";

   // User clicked the window's own close button (or Alt-F4'd it). Treated
   // the same as the node's "close" toggle (PluginVST3CloseEditor's
   // counterpart on Mac): soft-close only - hide the window, flip the open
   // flag, but leave plugView/editorHwnd alive so reopening just re-shows
   // the plugin's real editor instead of re-requesting a view a second time.
   // Real teardown only happens in PluginVST3CloseEditor/PluginVST3Destroy.
   LRESULT CALLBACK PluginEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
   {
      if (msg == WM_NCCREATE)
      {
         auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
         SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)createStruct->lpCreateParams);
         return DefWindowProcW(hwnd, msg, wParam, lParam);
      }

      auto* h = reinterpret_cast<Platform::PluginHandle*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

      if (msg == WM_CLOSE)
      {
         SetPluginEditorOpenWin(h, false);
         ShowWindow(hwnd, SW_HIDE);
         return 0;
      }

      if (msg == WM_SIZE)
      {
         if (h != nullptr && h->vst3 != nullptr && h->vst3->plugView && !h->vst3->resizingFromPlugin &&
             wParam != SIZE_MINIMIZED)
         {
            RECT client = {};
            GetClientRect(hwnd, &client);
            Steinberg::ViewRect newSize(0, 0, client.right - client.left, client.bottom - client.top);
            Platform::PluginVST3State* v = h->vst3;
            if (!Platform::RunPluginCallGuarded("onSize", h, [&] { v->plugView->onSize(&newSize); }))
               v->editorUnstable.store(true, std::memory_order_relaxed);
         }
         return DefWindowProcW(hwnd, msg, wParam, lParam);
      }

      return DefWindowProcW(hwnd, msg, wParam, lParam);
   }

   ATOM RegisterEditorWindowClassOnce()
   {
      static const ATOM atom = [] {
         WNDCLASSEXW wc = {};
         wc.cbSize = sizeof(wc);
         wc.style = CS_HREDRAW | CS_VREDRAW;
         wc.lpfnWndProc = PluginEditorWndProc;
         wc.hInstance = GetModuleHandleW(nullptr);
         wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
         wc.lpszClassName = kEditorWindowClassName;
         return RegisterClassExW(&wc);
      }();
      return atom;
   }
}

namespace Platform
{
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

      // macOS defers this block to the main run loop (see PluginVST3.mm) because
      // several real plugins create AppKit objects synchronously inside their
      // own factory/createInstance and abort if that happens off the main
      // thread. Windows has no equivalent constraint here and no established
      // "post to main thread, non-blocking" primitive yet, so this phase runs
      // the whole resolve-and-instantiate sequence synchronously instead -
      // PluginCreate is already only ever called from the UI thread that owns
      // the topology, so this briefly blocks that thread exactly the way the
      // Mac path does once its deferred block actually runs.
      VST3Trace("resolve begin: name='%s' id='%s' path='%s'", desc.name.c_str(), desc.identifier.c_str(),
                desc.path.c_str());

      std::string bundlePath;
      std::string lastKnownPath;
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

      // Step 3 (targeted rescan) on macOS also searches user-added folders via
      // SetVST3SearchFolders; that plumbing isn't wired up on Windows yet
      // (EnumerateVST3Plugins is still the no-op stub - see PluginHostWin.cpp),
      // so a cache miss here just stays a miss rather than triggering a scan
      // that can't find anything. Known gap, not a silent regression.
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
         return h;
      }

      Steinberg::IPluginFactory* factoryRaw = nullptr;
      HMODULE module = LoadVST3Module(bundlePath, &factoryRaw);
      if (module == nullptr || factoryRaw == nullptr)
      {
         {
            std::lock_guard<std::mutex> lock(v->arrivalMutex);
            v->arrivedError = "failed to load VST3 module";
         }
         v->arrived.store(true, std::memory_order_release);
         return h;
      }

      v->module = module;
      v->factory = factoryRaw;

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
         return h;
      }
      v->component = Steinberg::owned(compRaw);

      Steinberg::Vst::IEditController* ctrlRaw = nullptr;
      if (compRaw->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctrlRaw) == Steinberg::kResultOk &&
          ctrlRaw != nullptr)
      {
         v->controller = Steinberg::owned(ctrlRaw);
      }
      else
      {
         Steinberg::TUID controllerCID = {};
         if (compRaw->getControllerClassId(controllerCID) == Steinberg::kResultTrue)
         {
            if (v->factory->createInstance(controllerCID, Steinberg::Vst::IEditController::iid, (void**)&ctrlRaw) == Steinberg::kResultOk &&
                ctrlRaw != nullptr)
               v->controller = Steinberg::owned(ctrlRaw);
         }
      }

      Steinberg::Vst::IAudioProcessor* procRaw = nullptr;
      if (compRaw->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&procRaw) == Steinberg::kResultOk &&
          procRaw != nullptr)
      {
         v->processor = Steinberg::owned(procRaw);
      }

      v->arrived.store(true, std::memory_order_release);
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

         if (inBusCount > 0)
            v->component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
         if (outBusCount > 0)
            v->component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);

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

         h->latencySamples = (int)v->processor->getLatencySamples();

         const Steinberg::tresult procRes = v->processor->setProcessing(true);
         if (procRes != Steinberg::kResultOk)
            VST3Trace("setProcessing(true) returned %d - continuing (optional call)", (int)procRes);
         v->processing = true;

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
         v->processContext.projectTimeSamples = 0;
         v->processContext.projectTimeMusic = 0.0;
         v->processContext.tempo = 120.0;
         v->processContext.timeSigNumerator = 4;
         v->processContext.timeSigDenominator = 4;
         v->processContext.state = Steinberg::Vst::ProcessContext::kPlaying |
                                   Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                                   Steinberg::Vst::ProcessContext::kTempoValid |
                                   Steinberg::Vst::ProcessContext::kTimeSigValid;
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

         Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> compCP;
         Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> ctrlCP;
         if (v->component->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&compCP) == Steinberg::kResultOk &&
             v->controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&ctrlCP) == Steinberg::kResultOk)
         {
            if (compCP && ctrlCP && compCP != ctrlCP)
            {
               v->compToCtrlProxy = Steinberg::owned(new HostConnectionProxy(compCP));
               v->ctrlToCompProxy = Steinberg::owned(new HostConnectionProxy(ctrlCP));
               v->compToCtrlProxy->connect(ctrlCP);
               v->ctrlToCompProxy->connect(compCP);
            }
         }

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

      // removed() must happen while the editor HWND is still alive - detach
      // the plugin from its view first, only then tear down the window it
      // was living in. (Guarded: a plugin whose editor already faulted once
      // can fault again here.)
      if (v->plugView)
      {
         RunPluginCallGuarded("removed", h, [&] { v->plugView->removed(); });
         v->plugView = nullptr;
      }

      if (v->editorHwnd != nullptr)
      {
         HWND hwnd = v->editorHwnd;
         v->editorHwnd = nullptr;
         SetPluginEditorOpenWin(h, false);
         DestroyWindow(hwnd);
      }

      if (v->plugFrame)
      {
         v->plugFrame->detach();
         v->plugFrame = nullptr;
      }

      if (v->compToCtrlProxy)
      {
         v->compToCtrlProxy->DisconnectFromSource();
         v->compToCtrlProxy = nullptr;
      }
      if (v->ctrlToCompProxy)
      {
         v->ctrlToCompProxy->DisconnectFromSource();
         v->ctrlToCompProxy = nullptr;
      }

      if (v->componentHandler)
      {
         v->componentHandler->detach();
         v->componentHandler = nullptr;
      }

      // Wait out any in-flight render.
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

      if (v->module != nullptr)
      {
         UnloadVST3Module(v->module);
         v->module = nullptr;
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
      const double sr = h->sampleRate > 0.0 ? h->sampleRate : 48000.0;
      v->processContext.projectTimeMusic = (double)v->sampleTime / sr * (120.0 / 60.0);
      v->sampleTime += frames;

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

      for (int ch = pluginOut; ch < outChannels; ch++)
         if (out[ch] != nullptr && !direct)
            std::memset(out[ch], 0, (size_t)frames * sizeof(float));

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
   // Crash guard - Windows SEH equivalent of PluginVST3.mm's sigsetjmp guard.
   // Same discipline: narrow, synchronous, main-thread-only calls with no
   // in-flight audio riding on them. Deliberately not used around process().
   // This phase only has state save/restore to guard - there's no editor to
   // open yet (see PluginVST3OpenEditor below).
   //
   // MSVC forbids C++ objects requiring unwinding in a function that also
   // uses __except (without /EHa) - this function has none: `fn` is a
   // reference parameter, `what`/`h` are raw pointers, nothing local needs a
   // destructor run across the guarded region.
   // ------------------------------------------------------------------------
   namespace
   {
      bool RunPluginCallGuarded(const char* what, PluginHandle* h, const std::function<void()>& fn)
      {
         __try
         {
            fn();
         }
         __except (EXCEPTION_EXECUTE_HANDLER)
         {
            const char* name = (h != nullptr) ? h->desc.name.c_str() : "?";
            std::fprintf(stderr, "[VST3] plugin '%s' crashed inside %s - call aborted\n", name, what);
            return false;
         }
         return true;
      }
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

      if (v->editorUnstable.load(std::memory_order_relaxed))
      {
         outError = "plugin's editor crashed previously and is disabled for this session";
         return false;
      }

      if (v->editorHwnd != nullptr)
      {
         ShowWindow(v->editorHwnd, SW_SHOW);
         SetForegroundWindow(v->editorHwnd);
         SetPluginEditorOpenWin(h, true);
         return true;
      }

      if (!v->plugView)
      {
         Steinberg::IPlugView* view = nullptr;
         if (!RunPluginCallGuarded("createView", h, [&] { view = v->controller->createView(Steinberg::Vst::ViewType::kEditor); }))
         {
            v->editorUnstable.store(true, std::memory_order_relaxed);
            outError = "plugin crashed creating its editor view";
            return false;
         }
         if (view == nullptr)
         {
            outError = "plugin has no custom GUI editor";
            return false;
         }
         v->plugView = Steinberg::owned(view);
      }

      Steinberg::ViewRect rect = {};
      if (!RunPluginCallGuarded("getSize", h, [&] { v->plugView->getSize(&rect); }))
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         outError = "plugin crashed sizing its editor view";
         return false;
      }
      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;
      if (width < 120 || height < 80)
      {
         width = 640;
         height = 420;
      }

      bool canResize = false;
      RunPluginCallGuarded("canResize", h, [&] { canResize = v->plugView->canResize() == Steinberg::kResultTrue; });

      RegisterEditorWindowClassOnce();

      DWORD style = WS_OVERLAPPEDWINDOW;
      if (!canResize)
         style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

      RECT wr = { 0, 0, width, height };
      AdjustWindowRectEx(&wr, style, FALSE, 0);

      HWND hwnd = CreateWindowExW(0, kEditorWindowClassName, WinCommon::Utf8ToWide(h->desc.name).c_str(),
                                  style, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), h);
      if (hwnd == nullptr)
      {
         outError = "failed to create editor window";
         return false;
      }

      // Spec requires setFrame() before attached() - it is how a plugin with
      // a resizable GUI learns who to ask for a resize. Serum2 relies on
      // this (calls plugFrame->resizeView() from its own GUI timer).
      if (!v->plugFrame)
         v->plugFrame = Steinberg::owned(new HostPlugFrame(h));
      if (!RunPluginCallGuarded("setFrame", h, [&] { v->plugView->setFrame(v->plugFrame); }))
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         outError = "plugin crashed setting its editor frame";
         DestroyWindow(hwnd);
         return false;
      }

      if (!RunPluginCallGuarded("attached", h, [&] { v->plugView->attached((void*)hwnd, Steinberg::kPlatformTypeHWND); }))
      {
         v->editorUnstable.store(true, std::memory_order_relaxed);
         outError = "plugin crashed opening its editor";
         // The plugin may already have installed GUI timers/observers by
         // this point (this is exactly the Serum2 crash shape: a fault mid-
         // attached() leaves them armed against half-constructed state).
         // removed() first while the window is still alive, then tear down
         // the window - not the reverse.
         RunPluginCallGuarded("removed", h, [&] { v->plugView->removed(); });
         v->plugView = nullptr;
         DestroyWindow(hwnd);
         return false;
      }

      v->editorHwnd = hwnd;
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
      SetPluginEditorOpenWin(h, true);
      return true;
   }

   void PluginVST3CloseEditor(PluginHandle* h)
   {
      if (h == nullptr || h->vst3 == nullptr || h->vst3->editorHwnd == nullptr)
         return;
      PluginVST3State* v = h->vst3;

      // Fully detach, not just hide: a merely-hidden window left the
      // plugin's view attached() and its GUI timer armed indefinitely.
      // removed() first while the window is still alive, matching the same
      // ordering used on the attached()-failure and destroy paths.
      if (v->plugView)
      {
         RunPluginCallGuarded("removed", h, [&] { v->plugView->removed(); });
         v->plugView = nullptr;
      }

      HWND hwnd = v->editorHwnd;
      v->editorHwnd = nullptr;
      SetPluginEditorOpenWin(h, false);
      DestroyWindow(hwnd);
   }

   bool PluginVST3EditorIsOpen(PluginHandle* h)
   {
      return h != nullptr && h->vst3 != nullptr && h->vst3->editorOpen.load(std::memory_order_acquire);
   }

   bool PluginVST3AnyEditorOpen()
   {
      return gWinOpenPluginEditorCount.load(std::memory_order_relaxed) > 0;
   }

   bool PluginVST3PumpEditorEvents()
   {
      MSG msg;
      bool any = false;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
      {
         any = true;
         TranslateMessage(&msg);
         DispatchMessageW(&msg);
      }
      return any;
   }

   // ------------------------------------------------------------------------
   // State Save & Restore
   // ------------------------------------------------------------------------

   bool PluginVST3SaveState(PluginHandle* h, std::string& outBase64)
   {
      outBase64.clear();
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->component)
         return false;
      if (h->vst3->stateCallsUnstable.load(std::memory_order_relaxed))
         return false;

      MemoryStream compStream;
      Steinberg::tresult compResult = Steinberg::kResultFalse;
      if (!RunPluginCallGuarded("getState (component)", h, [&]
             { compResult = h->vst3->component->getState(&compStream); }))
      {
         h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
         return false;
      }
      if (compResult != Steinberg::kResultOk)
         return false;

      MemoryStream ctrlStream;
      if (h->vst3->controller)
      {
         if (!RunPluginCallGuarded("getState (controller)", h, [&]
                { h->vst3->controller->getState(&ctrlStream); }))
         {
            h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
            return false;
         }
      }

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

      outBase64 = Base64Encode(payload);
      return !outBase64.empty();
   }

   bool PluginVST3RestoreState(PluginHandle* h, const std::string& base64)
   {
      if (h == nullptr || h->vst3 == nullptr || !h->vst3->component || base64.empty())
         return false;
      if (h->vst3->stateCallsUnstable.load(std::memory_order_relaxed))
         return false;

      std::vector<uint8_t> payload;
      if (!Base64Decode(base64, payload))
         return false;

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
         bool ok = RunPluginCallGuarded("setState (component)", h, [&]
         {
            MemoryStream compStream(ptr, (size_t)compSize);
            h->vst3->component->setState(&compStream);
            if (h->vst3->controller)
            {
               compStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
               h->vst3->controller->setComponentState(&compStream);
            }
         });
         if (!ok)
         {
            h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
            return false;
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
            if (!RunPluginCallGuarded("setState (controller)", h, [&]
                   {
                      MemoryStream ctrlStream(ptr, (size_t)ctrlSize);
                      h->vst3->controller->setState(&ctrlStream);
                   }))
            {
               h->vst3->stateCallsUnstable.store(true, std::memory_order_relaxed);
               return false;
            }
         }
      }

      return true;
   }
}

#endif // INFINITE_ENABLE_VST3

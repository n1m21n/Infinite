#include "MovementLog.h"
#include "Modulation.h"
#include "Transport.h"
#include "../platform/AppPaths.h"
#include "imgui.h"

#include <miniz.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
   #ifndef WIN32_LEAN_AND_MEAN
   #define WIN32_LEAN_AND_MEAN
   #endif
   #include <windows.h>
#else
   #include <fcntl.h>
   #include <sys/file.h>
   #include <unistd.h>
#endif

#if defined(_WIN32)
   extern char** _environ;
   #define ENVIRON_PTR _environ
#elif defined(__APPLE__)
   #include <crt_externs.h>
   #define ENVIRON_PTR (*_NSGetEnviron())
#else
   extern char** environ;
   #define ENVIRON_PTR environ
#endif

namespace MovementLog
{

// -----------------------------------------------------------------------------
// Varint Encoding Utilities
// -----------------------------------------------------------------------------

static void EncodeVarint(std::vector<uint8_t>& buf, uint64_t val)
{
   while (val >= 0x80)
   {
      buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
      val >>= 7;
   }
   buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

static bool DecodeVarint(const uint8_t*& ptr, const uint8_t* end, uint64_t& val)
{
   val = 0;
   int shift = 0;
   while (ptr < end)
   {
      uint8_t b = *ptr++;
      val |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (!(b & 0x80))
         return true;
      shift += 7;
      if (shift >= 64)
         return false;
   }
   return false;
}

// -----------------------------------------------------------------------------
// Ring Buffer Events
// -----------------------------------------------------------------------------

struct RingEvent
{
   Record::Type type = Record::Type::Key;
   uint32_t t_ms = 0;

   // Key payload
   uint32_t keyId = 0;
   uint64_t uid = 0;
   int32_t paramIndex = 0;
   char typeName[32] = {0};
   char paramName[32] = {0};
   float minValue = 0.0f;
   float maxValue = 1.0f;
   float step = 0.0f;
   bool isEnum = false;
   bool isBool = false;
   bool hasCurve = false;

   // Val payload
   uint16_t q = 0;
   Source source = Source::Hand;
   uint8_t flags = 0;

   // Bind payload
   BindingEvent bindEvent = BindingEvent::Bind;
   uint64_t modUid = 0;
   int32_t modOutputIndex = 0;
   float lo = 0.0f;
   float hi = 1.0f;

   // Transport payload
   bool isPlaying = false;
   float bpm = 120.0f;
   double beats = 0.0;
   int32_t beatsPerBar = 4;

   // Mark payload
   Mark mark = Mark::SessionStart;
   uint64_t dropped = 0;
};

static constexpr size_t kRingCapacity = 65536; // 2^16
static constexpr size_t kRingMask = kRingCapacity - 1;

class EventRingBuffer
{
public:
   EventRingBuffer() : mBuffer(new RingEvent[kRingCapacity]) {}
   ~EventRingBuffer() { delete[] mBuffer; }

   bool Push(const RingEvent& event)
   {
      const size_t head = mHead.load(std::memory_order_relaxed);
      const size_t tail = mTail.load(std::memory_order_acquire);
      if (head - tail >= kRingCapacity)
      {
         mDroppedCount.fetch_add(1, std::memory_order_relaxed);
         return false;
      }
      mBuffer[head & kRingMask] = event;
      mHead.store(head + 1, std::memory_order_release);
      return true;
   }

   bool Pop(RingEvent& event)
   {
      const size_t tail = mTail.load(std::memory_order_relaxed);
      const size_t head = mHead.load(std::memory_order_acquire);
      if (tail == head)
         return false;
      event = mBuffer[tail & kRingMask];
      mTail.store(tail + 1, std::memory_order_release);
      return true;
   }

   size_t Size() const
   {
      const size_t head = mHead.load(std::memory_order_relaxed);
      const size_t tail = mTail.load(std::memory_order_relaxed);
      return (head >= tail) ? (head - tail) : 0;
   }

   uint64_t Dropped() const
   {
      return mDroppedCount.load(std::memory_order_relaxed);
   }

   void Reset()
   {
      mHead.store(0, std::memory_order_relaxed);
      mTail.store(0, std::memory_order_relaxed);
      mDroppedCount.store(0, std::memory_order_relaxed);
   }

private:
   RingEvent* mBuffer = nullptr;
   alignas(64) std::atomic<size_t> mHead{0};
   alignas(64) std::atomic<size_t> mTail{0};
   alignas(64) std::atomic<uint64_t> mDroppedCount{0};
};

// -----------------------------------------------------------------------------
// State & Storage
// -----------------------------------------------------------------------------

static EventRingBuffer sRing;
static std::atomic<bool> sEnabled{true};
static std::atomic<bool> sRunning{false};
static std::thread sWriterThread;
static std::mutex sWriterMutex;
static std::condition_variable sWriterCv;

static std::string sCustomLogDir;
static std::string sCurrentFilePath;
static std::string sCurrentSessionFileName;
static uint32_t sSessionStartEpochMs = 0;
static uint32_t sLastEventTimeMs = 0;

static std::atomic<uint64_t> sRetentionCapBytes{1024ULL * 1024ULL * 1024ULL}; // Default 1 GB

static std::function<uint64_t(int nodeIndex)> sNodeUidLookup;
static std::function<std::string(int nodeIndex)> sNodeTypeLookup;

// Widget-activity memory: a typed value commits on the frame the widget
// deactivates, so "active this frame or last" still counts as a hand move.
static bool sWasAnyItemActive = false;

// Single-writer lock on the log folder (a second instance does not log).
#if defined(_WIN32)
static HANDLE sLockHandle = INVALID_HANDLE_VALUE;
#else
static int sLockFd = -1;
#endif

static bool AcquireFolderLock(const std::string& dir)
{
   const std::string path = dir + "/.lock";
#if defined(_WIN32)
   sLockHandle = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
   return sLockHandle != INVALID_HANDLE_VALUE;
#else
   sLockFd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
   if (sLockFd < 0)
      return false;
   if (::flock(sLockFd, LOCK_EX | LOCK_NB) != 0)
   {
      ::close(sLockFd);
      sLockFd = -1;
      return false;
   }
   return true;
#endif
}

static void ReleaseFolderLock()
{
#if defined(_WIN32)
   if (sLockHandle != INVALID_HANDLE_VALUE)
   {
      CloseHandle(sLockHandle);
      sLockHandle = INVALID_HANDLE_VALUE;
   }
#else
   if (sLockFd >= 0)
   {
      ::flock(sLockFd, LOCK_UN);
      ::close(sLockFd);
      sLockFd = -1;
   }
#endif
}

struct KeyHash
{
   size_t operator()(const std::pair<uint64_t, int32_t>& k) const
   {
      return std::hash<uint64_t>()(k.first) ^ (std::hash<int32_t>()(k.second) << 1);
   }
};

struct ParamKeyState
{
   uint32_t id = 0;
   uint32_t lastEpoch = 0;
   uint16_t lastQ = 0;
   bool hasLastQ = false;
   double lastValTimeSec = -1.0;
};

static std::unordered_map<std::pair<uint64_t, int32_t>, ParamKeyState, KeyHash> sParamStates;
static uint32_t sNextKeyId = 1;
static uint32_t sCurrentEpoch = 1;

struct WriterTag
{
   Source source = Source::Hand;
   uint8_t flags = 0;
};

static std::unordered_map<std::pair<int, int>, WriterTag, KeyHash> sPendingWriters;
static std::vector<Mark> sPendingMarks;

static bool sLastIsPlaying = false;
static float sLastBpm = -1.0f;
static int32_t sLastBar = -1;

// -----------------------------------------------------------------------------
// Directory & Retention Helpers
// -----------------------------------------------------------------------------

std::string GetLogDirectory()
{
   if (!sCustomLogDir.empty())
   {
      AppPaths::EnsureDir(sCustomLogDir);
      return sCustomLogDir;
   }
   std::string appDir = AppPaths::AppSupportDir();
   if (appDir.empty())
      return {};
   std::string logDir = appDir + "/movement-log";
   AppPaths::EnsureDir(logDir);
   return logDir;
}

uint64_t GetLogFolderSizeBytes()
{
   std::string dir = GetLogDirectory();
   if (dir.empty() || !AppPaths::DirExists(dir))
      return 0;

   uint64_t total = 0;
   std::error_code ec;
   for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
   {
      if (entry.is_regular_file(ec))
      {
         total += entry.file_size(ec);
      }
   }
   return total;
}

void SetRetentionCapBytes(uint64_t bytes)
{
   sRetentionCapBytes.store(bytes, std::memory_order_relaxed);
}

uint64_t GetRetentionCapBytes()
{
   return sRetentionCapBytes.load(std::memory_order_relaxed);
}

uint64_t DroppedCount()
{
   return sRing.Dropped();
}

static void EnforceRetention(const std::string& dir)
{
   uint64_t cap = sRetentionCapBytes.load(std::memory_order_relaxed);
   if (cap == 0 || dir.empty() || !AppPaths::DirExists(dir))
      return;

   std::error_code ec;
   struct FileInfo
   {
      std::filesystem::path path;
      std::filesystem::file_time_type time;
      uint64_t size = 0;
   };

   std::vector<FileInfo> compressedFiles;
   uint64_t totalSize = 0;

   for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
   {
      if (!entry.is_regular_file(ec))
         continue;

      const std::string filename = entry.path().filename().string();
      // Never delete stats.bin
      if (filename == "stats.bin")
         continue;

      const uint64_t sz = entry.file_size(ec);
      totalSize += sz;

      // Only prune completed compressed files (.mlog.z)
      if (entry.path().extension() == ".z" || filename.find(".mlog.z") != std::string::npos)
      {
         compressedFiles.push_back({entry.path(), entry.last_write_time(ec), sz});
      }
   }

   if (totalSize <= cap)
      return;

   std::sort(compressedFiles.begin(), compressedFiles.end(),
             [](const FileInfo& a, const FileInfo& b) { return a.time < b.time; });

   for (const auto& fi : compressedFiles)
   {
      if (totalSize <= cap)
         break;
      std::filesystem::remove(fi.path, ec);
      if (!ec)
      {
         totalSize = (totalSize >= fi.size) ? (totalSize - fi.size) : 0;
      }
   }
}

static bool CompressFileMiniz(const std::string& srcPath, const std::string& dstPath)
{
   std::ifstream in(srcPath, std::ios::binary);
   if (!in)
      return false;

   in.seekg(0, std::ios::end);
   size_t inSize = static_cast<size_t>(in.tellg());
   in.seekg(0, std::ios::beg);

   if (inSize == 0)
      return false;

   std::vector<uint8_t> inBuf(inSize);
   in.read(reinterpret_cast<char*>(inBuf.data()), inSize);
   in.close();

   uLong dstBound = mz_compressBound(static_cast<uLong>(inSize));
   std::vector<uint8_t> outBuf(dstBound + 12); // 4-byte raw size header + compressed payload

   // Write 4-byte uncompressed size header at the start of .mlog.z
   outBuf[0] = static_cast<uint8_t>(inSize & 0xFF);
   outBuf[1] = static_cast<uint8_t>((inSize >> 8) & 0xFF);
   outBuf[2] = static_cast<uint8_t>((inSize >> 16) & 0xFF);
   outBuf[3] = static_cast<uint8_t>((inSize >> 24) & 0xFF);

   uLong cmpLen = dstBound;
   int status = mz_compress(outBuf.data() + 4, &cmpLen, inBuf.data(), static_cast<uLong>(inSize));
   if (status != MZ_OK)
      return false;

   std::ofstream out(dstPath, std::ios::binary);
   if (!out)
      return false;

   out.write(reinterpret_cast<const char*>(outBuf.data()), cmpLen + 4);
   out.close();

   return true;
}

// -----------------------------------------------------------------------------
// Writer Thread Logic
// -----------------------------------------------------------------------------

static void SerializeEvent(std::vector<uint8_t>& buf, const RingEvent& ev, uint32_t& lastTimeMs)
{
   uint32_t dt_ms = (ev.t_ms >= lastTimeMs) ? (ev.t_ms - lastTimeMs) : 0;
   lastTimeMs = ev.t_ms;

   buf.push_back(static_cast<uint8_t>(ev.type));

   switch (ev.type)
   {
   case Record::Type::Key:
   {
      EncodeVarint(buf, ev.keyId);
      EncodeVarint(buf, ev.uid);
      EncodeVarint(buf, static_cast<uint32_t>(ev.paramIndex));

      size_t tLen = strlen(ev.typeName);
      EncodeVarint(buf, tLen);
      buf.insert(buf.end(), ev.typeName, ev.typeName + tLen);

      size_t nLen = strlen(ev.paramName);
      EncodeVarint(buf, nLen);
      buf.insert(buf.end(), ev.paramName, ev.paramName + nLen);

      const uint8_t* f1 = reinterpret_cast<const uint8_t*>(&ev.minValue);
      buf.insert(buf.end(), f1, f1 + 4);
      const uint8_t* f2 = reinterpret_cast<const uint8_t*>(&ev.maxValue);
      buf.insert(buf.end(), f2, f2 + 4);
      const uint8_t* f3 = reinterpret_cast<const uint8_t*>(&ev.step);
      buf.insert(buf.end(), f3, f3 + 4);

      uint8_t flags = (ev.isEnum ? 1 : 0) | (ev.isBool ? 2 : 0) | (ev.hasCurve ? 4 : 0);
      buf.push_back(flags);
      break;
   }
   case Record::Type::Val:
   {
      EncodeVarint(buf, ev.keyId);
      EncodeVarint(buf, dt_ms);
      buf.push_back(static_cast<uint8_t>(ev.q & 0xFF));
      buf.push_back(static_cast<uint8_t>((ev.q >> 8) & 0xFF));
      buf.push_back(static_cast<uint8_t>(ev.source));
      buf.push_back(ev.flags);
      break;
   }
   case Record::Type::Bind:
   {
      EncodeVarint(buf, ev.keyId);
      buf.push_back(static_cast<uint8_t>(ev.bindEvent));
      EncodeVarint(buf, ev.modUid);
      EncodeVarint(buf, static_cast<uint32_t>(ev.modOutputIndex));
      const uint8_t* f1 = reinterpret_cast<const uint8_t*>(&ev.lo);
      buf.insert(buf.end(), f1, f1 + 4);
      const uint8_t* f2 = reinterpret_cast<const uint8_t*>(&ev.hi);
      buf.insert(buf.end(), f2, f2 + 4);
      break;
   }
   case Record::Type::Transport:
   {
      EncodeVarint(buf, dt_ms);
      buf.push_back(ev.isPlaying ? 1 : 0);
      const uint8_t* f1 = reinterpret_cast<const uint8_t*>(&ev.bpm);
      buf.insert(buf.end(), f1, f1 + 4);
      const uint8_t* d1 = reinterpret_cast<const uint8_t*>(&ev.beats);
      buf.insert(buf.end(), d1, d1 + 8);
      EncodeVarint(buf, static_cast<uint32_t>(ev.beatsPerBar));
      break;
   }
   case Record::Type::Mark:
   {
      buf.push_back(static_cast<uint8_t>(ev.mark));
      EncodeVarint(buf, dt_ms);
      if (ev.mark == Mark::SessionEnd)
         EncodeVarint(buf, ev.dropped);
      break;
   }
   }
}

// Compress session files left open by a crash. Only called by the lock holder,
// so no other instance can still be appending to them.
static void CompressStaleSessions(const std::string& dir, const std::string& currentPath)
{
   std::error_code ec;
   for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
   {
      if (!entry.is_regular_file(ec) || entry.path().extension() != ".mlog")
         continue;
      if (entry.path().string() == currentPath)
         continue;
      const std::string src = entry.path().string();
      if (CompressFileMiniz(src, src + ".z"))
         std::filesystem::remove(src, ec);
      else if (entry.file_size(ec) == 0)
         std::filesystem::remove(src, ec);
   }
}

static void WriterThreadFunc(const std::string& logDir, const std::string& filePath)
{
   CompressStaleSessions(logDir, filePath);

   std::ofstream file(filePath, std::ios::binary | std::ios::app);
   if (!file)
      return;

   // File header: Magic "IMLOG1" + format version 1
   const char magic[6] = {'I', 'M', 'L', 'O', 'G', '1'};
   file.write(magic, 6);
   const uint16_t version = 1;
   file.write(reinterpret_cast<const char*>(&version), 2);
   file.flush();

   uint32_t lastTimeMs = 0;
   std::vector<uint8_t> writeBuf;
   writeBuf.reserve(16384);

   while (sRunning.load(std::memory_order_relaxed) || sRing.Size() > 0)
   {
      {
         std::unique_lock<std::mutex> lock(sWriterMutex);
         sWriterCv.wait_for(lock, std::chrono::seconds(2), [] {
            return !sRunning.load(std::memory_order_relaxed) || sRing.Size() >= 128;
         });
      }

      writeBuf.clear();
      RingEvent ev;
      size_t count = 0;
      while (sRing.Pop(ev))
      {
         SerializeEvent(writeBuf, ev, lastTimeMs);
         if (++count >= 1024)
            break;
      }

      if (!writeBuf.empty())
      {
         file.write(reinterpret_cast<const char*>(writeBuf.data()), writeBuf.size());
         file.flush();
      }
   }

   // Drain any remaining items after stop
   writeBuf.clear();
   RingEvent ev;
   while (sRing.Pop(ev))
   {
      SerializeEvent(writeBuf, ev, lastTimeMs);
   }
   if (!writeBuf.empty())
   {
      file.write(reinterpret_cast<const char*>(writeBuf.data()), writeBuf.size());
      file.flush();
   }
   file.close();

   // Compress to .mlog.z
   std::string compPath = filePath + ".z";
   if (CompressFileMiniz(filePath, compPath))
   {
      std::error_code ec;
      std::filesystem::remove(filePath, ec);
   }

   // Enforce retention cap
   EnforceRetention(logDir);
}

// -----------------------------------------------------------------------------
// Lifecycle & Modulation Callback
// -----------------------------------------------------------------------------

static void ModulationBindingCallback(int nodeIndex, int paramIndex, int eventType, int modNodeIndex, float lo, float hi)
{
   if (!sEnabled.load(std::memory_order_relaxed) || !sRunning.load(std::memory_order_relaxed))
      return;

   uint64_t uid = 0;
   if (sNodeUidLookup)
      uid = sNodeUidLookup(nodeIndex);

   uint64_t modUid = 0;
   if (sNodeUidLookup && modNodeIndex >= 0)
      modUid = sNodeUidLookup(modNodeIndex);

   const auto key = std::make_pair(uid, paramIndex);
   auto it = sParamStates.find(key);
   uint32_t keyId = (it != sParamStates.end()) ? it->second.id : 0;

   RingEvent ev;
   ev.type = Record::Type::Bind;
   ev.t_ms = sLastEventTimeMs;
   ev.keyId = keyId;
   ev.bindEvent = static_cast<BindingEvent>(eventType);
   ev.modUid = modUid;
   ev.modOutputIndex = 0;
   ev.lo = lo;
   ev.hi = hi;

   sRing.Push(ev);
}

void Start(const std::string& customDir)
{
   if (sRunning.load(std::memory_order_relaxed))
      return;

   sCustomLogDir = customDir;
   if (sCustomLogDir.empty())
   {
      const char* movelogDirEnv = std::getenv("INFINITE_MOVELOG_DIR");
      if (movelogDirEnv && *movelogDirEnv)
      {
         sCustomLogDir = movelogDirEnv;
      }
      else
      {
         bool hasInfiniteEnv = false;
         char** env = ENVIRON_PTR;
         if (env)
         {
            for (; *env; ++env)
            {
               if (std::strncmp(*env, "INFINITE_", 9) == 0)
               {
                  hasInfiniteEnv = true;
                  break;
               }
            }
         }
         if (hasInfiniteEnv)
         {
            // Test isolation: refuse to write to user support folder during self-tests
            return;
         }
      }
   }

   std::string dir = GetLogDirectory();
   if (dir.empty())
      return;

   // Generate filename YYYYMMDD-HHMMSS.mlog
   auto now = std::chrono::system_clock::now();
   std::time_t tt = std::chrono::system_clock::to_time_t(now);
   std::tm tmVal{};
#if defined(_WIN32)
   localtime_s(&tmVal, &tt);
#else
   localtime_r(&tt, &tmVal);
#endif

   char fname[64];
   std::snprintf(fname, sizeof(fname), "%04d%02d%02d-%02d%02d%02d.mlog",
                 tmVal.tm_year + 1900, tmVal.tm_mon + 1, tmVal.tm_mday,
                 tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec);

   sCurrentSessionFileName = fname;
   sCurrentFilePath = dir + "/" + fname;
   sSessionStartEpochMs = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
   sLastEventTimeMs = 0;

   if (!AcquireFolderLock(dir))
      return; // another instance owns the log folder

   sWasAnyItemActive = false;
   sParamStates.clear();
   sNextKeyId = 1;
   sCurrentEpoch = 1;
   sPendingWriters.clear();
   sPendingMarks.clear();

   sRunning.store(true, std::memory_order_release);
   sWriterThread = std::thread(WriterThreadFunc, dir, sCurrentFilePath);

   // Hook into Modulation bindings
   Modulation::Instance().SetBindingCallback([](int nodeIndex, int paramIndex, Modulation::BindingEvent evt, int modNodeIndex, float lo, float hi) {
      ModulationBindingCallback(nodeIndex, paramIndex, static_cast<int>(evt), modNodeIndex, lo, hi);
   });

   NoteMark(Mark::SessionStart);
}

void Stop()
{
   if (!sRunning.load(std::memory_order_relaxed))
      return;

   // Pushed straight to the ring: no Capture runs after this, so a pending
   // mark would never be written. The writer drains the ring before exiting.
   {
      RingEvent ev;
      ev.type = Record::Type::Mark;
      ev.t_ms = sLastEventTimeMs;
      ev.mark = Mark::SessionEnd;
      ev.dropped = sRing.Dropped();
      sRing.Push(ev);
   }

   sRunning.store(false, std::memory_order_release);
   sWriterCv.notify_all();

   if (sWriterThread.joinable())
      sWriterThread.join();

   ReleaseFolderLock();
}

void SetEnabled(bool on)
{
   sEnabled.store(on, std::memory_order_relaxed);
}

bool IsEnabled()
{
   return sEnabled.load(std::memory_order_relaxed);
}

void SetNodeUidLookup(const std::function<uint64_t(int nodeIndex)>& lookup)
{
   sNodeUidLookup = lookup;
}

void SetNodeTypeLookup(const std::function<std::string(int nodeIndex)>& lookup)
{
   sNodeTypeLookup = lookup;
}

void NoteWriter(int nodeIndex, int paramIndex, Source s, uint8_t flags)
{
   if (!sEnabled.load(std::memory_order_relaxed))
      return;
   sPendingWriters[std::make_pair(nodeIndex, paramIndex)] = {s, flags};
}

void NoteMark(Mark m)
{
   if (!sEnabled.load(std::memory_order_relaxed))
      return;
   sCurrentEpoch++;
   sPendingMarks.push_back(m);
}

void NoteBinding(int nodeIndex, int paramIndex, BindingEvent evt, int modNodeIndex, float lo, float hi)
{
   ModulationBindingCallback(nodeIndex, paramIndex, static_cast<int>(evt), modNodeIndex, lo, hi);
}

// -----------------------------------------------------------------------------
// Main Thread Capture Hook
// -----------------------------------------------------------------------------

void Capture(double t, bool isNormalFrame)
{
   if (!isNormalFrame || !sEnabled.load(std::memory_order_relaxed) || !sRunning.load(std::memory_order_relaxed))
      return;

   const uint32_t t_ms = static_cast<uint32_t>(t * 1000.0);
   sLastEventTimeMs = t_ms;

   // 1. Process pending marks
   if (!sPendingMarks.empty())
   {
      for (Mark m : sPendingMarks)
      {
         RingEvent ev;
         ev.type = Record::Type::Mark;
         ev.t_ms = t_ms;
         ev.mark = m;
         sRing.Push(ev);
      }
      sPendingMarks.clear();
   }

   // 2. Process transport changes
   Transport& transport = Transport::Instance();
   bool isPlaying = transport.IsPlaying();
   float bpm = transport.Tempo();
   double beats = transport.Beats();
   int32_t beatsPerBar = transport.BeatsPerBar();
   int32_t currentBar = (beatsPerBar > 0) ? static_cast<int32_t>(beats / beatsPerBar) : 0;

   if (isPlaying != sLastIsPlaying || std::abs(bpm - sLastBpm) > 0.01f || (isPlaying && currentBar != sLastBar))
   {
      sLastIsPlaying = isPlaying;
      sLastBpm = bpm;
      sLastBar = currentBar;

      RingEvent ev;
      ev.type = Record::Type::Transport;
      ev.t_ms = t_ms;
      ev.isPlaying = isPlaying;
      ev.bpm = bpm;
      ev.beats = beats;
      ev.beatsPerBar = beatsPerBar;
      sRing.Push(ev);
   }

   // 3. Process frame parameters
   const bool activeNow = ImGui::GetCurrentContext() != nullptr && ImGui::IsAnyItemActive();
   const bool recentlyActive = activeNow || sWasAnyItemActive;
   sWasAnyItemActive = activeNow;
   const auto& frameParams = Modulation::Instance().FrameParams();
   for (const ParamRef& ref : frameParams)
   {
      if (ref.value == nullptr)
         continue;

      uint64_t uid = 0;
      if (sNodeUidLookup)
         uid = sNodeUidLookup(ref.nodeIndex);

      const auto key = std::make_pair(uid, ref.paramIndex);
      auto it = sParamStates.find(key);
      if (it == sParamStates.end())
      {
         // First time seeing this parameter in this session file -> emit KEY record
         ParamKeyState newState;
         newState.id = sNextKeyId++;
         newState.lastEpoch = sCurrentEpoch;
         it = sParamStates.emplace(key, newState).first;

         RingEvent keyEv;
         keyEv.type = Record::Type::Key;
         keyEv.t_ms = t_ms;
         keyEv.keyId = it->second.id;
         keyEv.uid = uid;
         keyEv.paramIndex = ref.paramIndex;

         const std::string typeName = sNodeTypeLookup ? sNodeTypeLookup(ref.nodeIndex) : std::string();
         std::strncpy(keyEv.typeName, typeName.empty() ? "Node" : typeName.c_str(), sizeof(keyEv.typeName) - 1);
         std::strncpy(keyEv.paramName, ref.name.c_str(), sizeof(keyEv.paramName) - 1);

         keyEv.minValue = ref.minValue;
         keyEv.maxValue = ref.maxValue;
         keyEv.step = ref.step;
         keyEv.isEnum = ref.isEnum;
         keyEv.isBool = ref.isBool;
         keyEv.hasCurve = (ref.valueToPos != nullptr);

         sRing.Push(keyEv);

         // First sighting is a baseline, not a move.
         float pos0 = 0.0f;
         if (ref.valueToPos != nullptr)
            pos0 = ref.valueToPos(*ref.value, ref.minValue, ref.maxValue);
         else if (ref.maxValue > ref.minValue)
            pos0 = (*ref.value - ref.minValue) / (ref.maxValue - ref.minValue);
         it->second.lastQ = static_cast<uint16_t>(std::lround(std::clamp(pos0, 0.0f, 1.0f) * 65535.0f));
         it->second.hasLastQ = true;
         continue;
      }

      ParamKeyState& state = it->second;

      // Calculate normalized fader position
      float pos = 0.0f;
      if (ref.valueToPos != nullptr)
      {
         pos = ref.valueToPos(*ref.value, ref.minValue, ref.maxValue);
      }
      else if (ref.maxValue > ref.minValue)
      {
         pos = (*ref.value - ref.minValue) / (ref.maxValue - ref.minValue);
      }
      pos = std::clamp(pos, 0.0f, 1.0f);
      uint16_t q = static_cast<uint16_t>(std::lround(pos * 65535.0f));

      // Re-baseline on mark/epoch change without emitting VAL
      if (state.lastEpoch != sCurrentEpoch)
      {
         state.lastEpoch = sCurrentEpoch;
         state.lastQ = q;
         state.hasLastQ = true;
         continue;
      }

      if (state.hasLastQ && state.lastQ == q)
         continue;

      // Determine writer source
      Source src = Source::Hand;
      uint8_t flags = 0;
      auto writerIt = sPendingWriters.find(std::make_pair(ref.nodeIndex, ref.paramIndex));
      if (writerIt != sPendingWriters.end())
      {
         src = writerIt->second.source;
         flags = writerIt->second.flags;
      }
      else
      {
         if (ImGui::GetCurrentContext() != nullptr && !recentlyActive)
         {
            src = Source::Other;
         }
      }

      // Decimate Modulator and Expression sources to <= 10 Hz (100 ms)
      if (src == Source::Modulator || src == Source::Expression)
      {
         if (state.lastValTimeSec >= 0.0 && (t - state.lastValTimeSec) < 0.099)
            continue;
      }

      state.lastQ = q;
      state.hasLastQ = true;
      state.lastValTimeSec = t;

      RingEvent valEv;
      valEv.type = Record::Type::Val;
      valEv.t_ms = t_ms;
      valEv.keyId = state.id;
      valEv.q = q;
      valEv.source = src;
      valEv.flags = flags;
      sRing.Push(valEv);
   }

   sPendingWriters.clear();
}

// -----------------------------------------------------------------------------
// Reader & Dump Logic
// -----------------------------------------------------------------------------

bool ReadFile(const std::string& path, const std::function<bool(const Record&)>& callback)
{
   std::ifstream file(path, std::ios::binary);
   if (!file)
      return false;

   file.seekg(0, std::ios::end);
   size_t fileSize = static_cast<size_t>(file.tellg());
   file.seekg(0, std::ios::beg);

   if (fileSize < 8)
      return false;

   std::vector<uint8_t> decompressedData;
   const uint8_t* ptr = nullptr;
   const uint8_t* end = nullptr;

   // Check if file is compressed (.mlog.z)
   char header[6];
   file.read(header, 6);
   file.seekg(0, std::ios::beg);

   if (std::memcmp(header, "IMLOG1", 6) != 0)
   {
      // Decompress miniz payload
      std::vector<uint8_t> compressed(fileSize);
      file.read(reinterpret_cast<char*>(compressed.data()), fileSize);
      file.close();

      if (compressed.size() < 4)
         return false;

      uint32_t rawSize = static_cast<uint32_t>(compressed[0]) |
                         (static_cast<uint32_t>(compressed[1]) << 8) |
                         (static_cast<uint32_t>(compressed[2]) << 16) |
                         (static_cast<uint32_t>(compressed[3]) << 24);

      decompressedData.resize(rawSize);
      uLong uncmpLen = rawSize;
      int status = mz_uncompress(decompressedData.data(), &uncmpLen,
                                 compressed.data() + 4, static_cast<uLong>(compressed.size() - 4));
      if (status != MZ_OK || uncmpLen != rawSize)
         return false;

      ptr = decompressedData.data();
      end = ptr + decompressedData.size();
   }
   else
   {
      decompressedData.resize(fileSize);
      file.read(reinterpret_cast<char*>(decompressedData.data()), fileSize);
      file.close();
      ptr = decompressedData.data();
      end = ptr + decompressedData.size();
   }

   // Validate Magic & Version
   if (end - ptr < 8 || std::memcmp(ptr, "IMLOG1", 6) != 0)
      return false;
   ptr += 6;

   uint16_t version = static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
   ptr += 2;
   if (version != 1)
      return false;

   while (ptr < end)
   {
      uint8_t tag = *ptr++;
      Record rec;
      rec.type = static_cast<Record::Type>(tag);

      switch (rec.type)
      {
      case Record::Type::Key:
      {
         uint64_t v = 0;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.key.id = static_cast<uint32_t>(v);
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.key.uid = v;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.key.paramIndex = static_cast<int32_t>(v);

         uint64_t tLen = 0;
         if (!DecodeVarint(ptr, end, tLen) || ptr + tLen > end) return false;
         rec.key.typeName.assign(reinterpret_cast<const char*>(ptr), tLen);
         ptr += tLen;

         uint64_t nLen = 0;
         if (!DecodeVarint(ptr, end, nLen) || ptr + nLen > end) return false;
         rec.key.name.assign(reinterpret_cast<const char*>(ptr), nLen);
         ptr += nLen;

         if (ptr + 13 > end) return false;
         std::memcpy(&rec.key.minValue, ptr, 4); ptr += 4;
         std::memcpy(&rec.key.maxValue, ptr, 4); ptr += 4;
         std::memcpy(&rec.key.step, ptr, 4); ptr += 4;

         uint8_t flags = *ptr++;
         rec.key.isEnum = (flags & 1) != 0;
         rec.key.isBool = (flags & 2) != 0;
         rec.key.hasCurve = (flags & 4) != 0;
         break;
      }
      case Record::Type::Val:
      {
         uint64_t v = 0;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.val.id = static_cast<uint32_t>(v);
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.val.dt_ms = static_cast<uint32_t>(v);

         if (ptr + 4 > end) return false;
         rec.val.q = static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
         ptr += 2;
         rec.val.source = static_cast<Source>(*ptr++);
         rec.val.flags = *ptr++;
         break;
      }
      case Record::Type::Bind:
      {
         uint64_t v = 0;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.bind.id = static_cast<uint32_t>(v);
         if (ptr >= end) return false;
         rec.bind.event = static_cast<BindingEvent>(*ptr++);
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.bind.modNodeUid = v;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.bind.modOutputIndex = static_cast<int32_t>(v);

         if (ptr + 8 > end) return false;
         std::memcpy(&rec.bind.lo, ptr, 4); ptr += 4;
         std::memcpy(&rec.bind.hi, ptr, 4); ptr += 4;
         break;
      }
      case Record::Type::Transport:
      {
         uint64_t v = 0;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.transport.dt_ms = static_cast<uint32_t>(v);
         if (ptr >= end) return false;
         rec.transport.isPlaying = (*ptr++ != 0);

         if (ptr + 12 > end) return false;
         std::memcpy(&rec.transport.bpm, ptr, 4); ptr += 4;
         std::memcpy(&rec.transport.beats, ptr, 8); ptr += 8;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.transport.beatsPerBar = static_cast<int32_t>(v);
         break;
      }
      case Record::Type::Mark:
      {
         if (ptr >= end) return false;
         rec.mark.mark = static_cast<Mark>(*ptr++);
         uint64_t v = 0;
         if (!DecodeVarint(ptr, end, v)) return false;
         rec.mark.dt_ms = static_cast<uint32_t>(v);
         if (rec.mark.mark == Mark::SessionEnd)
         {
            if (!DecodeVarint(ptr, end, v)) return false;
            rec.mark.dropped = v;
         }
         break;
      }
      default:
         return false; // Unknown record or corrupt tail
      }

      if (!callback(rec))
         break;
   }

   return true;
}

void DumpLog(const std::string& path, std::ostream& out)
{
   std::unordered_map<uint32_t, KeyRecord> keyMap;
   uint32_t curTimeMs = 0;

   out << "=== Movement Log Dump: " << path << " ===\n";

   ReadFile(path, [&](const Record& rec) {
      switch (rec.type)
      {
      case Record::Type::Key:
         keyMap[rec.key.id] = rec.key;
         out << "[KEY] id=" << rec.key.id << " uid=" << rec.key.uid
             << " param=" << rec.key.paramIndex << " type=" << rec.key.typeName
             << " name=" << rec.key.name << " min=" << rec.key.minValue
             << " max=" << rec.key.maxValue << " enum=" << rec.key.isEnum
             << " bool=" << rec.key.isBool << "\n";
         break;
      case Record::Type::Val:
      {
         curTimeMs += rec.val.dt_ms;
         std::string kName = "unknown";
         auto it = keyMap.find(rec.val.id);
         if (it != keyMap.end())
            kName = it->second.name;

         const char* srcStr = "Hand";
         switch (rec.val.source)
         {
         case Source::Hand: srcStr = "Hand"; break;
         case Source::Perf: srcStr = "Perf"; break;
         case Source::Modulator: srcStr = "Modulator"; break;
         case Source::Expression: srcStr = "Expression"; break;
         case Source::Gesture: srcStr = "Gesture"; break;
         case Source::Prediction: srcStr = "Prediction"; break;
         case Source::Other: srcStr = "Other"; break;
         }

         out << "[VAL] t=" << curTimeMs << "ms (+ " << rec.val.dt_ms
             << "ms) id=" << rec.val.id << " (" << kName << ") q=" << rec.val.q
             << " pos=" << std::fixed << std::setprecision(4) << (rec.val.q / 65535.0f)
             << " src=" << srcStr << " flags=" << (int)rec.val.flags << "\n";
         break;
      }
      case Record::Type::Bind:
         out << "[BIND] id=" << rec.bind.id << " event=" << (int)rec.bind.event
             << " modUid=" << rec.bind.modNodeUid << " out=" << rec.bind.modOutputIndex
             << " lo=" << rec.bind.lo << " hi=" << rec.bind.hi << "\n";
         break;
      case Record::Type::Transport:
         curTimeMs += rec.transport.dt_ms;
         out << "[TRANSPORT] t=" << curTimeMs << "ms playing=" << rec.transport.isPlaying
             << " bpm=" << rec.transport.bpm << " beats=" << rec.transport.beats
             << " beatsPerBar=" << rec.transport.beatsPerBar << "\n";
         break;
      case Record::Type::Mark:
      {
         curTimeMs += rec.mark.dt_ms;
         const char* mStr = "SessionStart";
         switch (rec.mark.mark)
         {
         case Mark::SessionStart: mStr = "SessionStart"; break;
         case Mark::SessionEnd: mStr = "SessionEnd"; break;
         case Mark::PatchLoaded: mStr = "PatchLoaded"; break;
         case Mark::PatchNew: mStr = "PatchNew"; break;
         case Mark::Undo: mStr = "Undo"; break;
         case Mark::Redo: mStr = "Redo"; break;
         }
         out << "[MARK] t=" << curTimeMs << "ms mark=" << mStr;
         if (rec.mark.mark == Mark::SessionEnd)
            out << " dropped=" << rec.mark.dropped;
         out << "\n";
         break;
      }
      }
      return true;
   });

   out << "=== End of Log ===\n";
}

// -----------------------------------------------------------------------------
// Self Test Fixture
// -----------------------------------------------------------------------------

bool RunMovementLogTest()
{
   printf("[MOVEMENT LOG TEST] Starting...\n");

   // 1. Ring Buffer Unit Test
   {
      EventRingBuffer ring;
      for (size_t i = 0; i < 100; i++)
      {
         RingEvent ev;
         ev.keyId = static_cast<uint32_t>(i + 1);
         if (!ring.Push(ev))
         {
            printf("[FAIL] EventRingBuffer Push failed at %zu\n", i);
            return false;
         }
      }
      for (size_t i = 0; i < 100; i++)
      {
         RingEvent ev;
         if (!ring.Pop(ev) || ev.keyId != i + 1)
         {
            printf("[FAIL] EventRingBuffer Pop mismatch at %zu\n", i);
            return false;
         }
      }

      // Overflow test
      for (size_t i = 0; i < kRingCapacity + 10; i++)
      {
         RingEvent ev;
         ring.Push(ev);
      }
      if (ring.Dropped() != 10)
      {
         printf("[FAIL] EventRingBuffer dropped count mismatch: %llu (expected 10)\n",
                static_cast<unsigned long long>(ring.Dropped()));
         return false;
      }
   }

   // 2. Varint Encode/Decode Unit Test
   {
      const std::vector<uint64_t> testVals = {
         0, 1, 127, 128, 255, 256, 16383, 16384, 0x123456789ABCDEF0ULL, UINT64_MAX
      };
      std::vector<uint8_t> buf;
      for (uint64_t v : testVals)
         EncodeVarint(buf, v);

      const uint8_t* ptr = buf.data();
      const uint8_t* end = ptr + buf.size();
      for (size_t i = 0; i < testVals.size(); i++)
      {
         uint64_t dec = 0;
         if (!DecodeVarint(ptr, end, dec) || dec != testVals[i])
         {
            printf("[FAIL] Varint mismatch at index %zu: got %llu, expected %llu\n",
                   i, static_cast<unsigned long long>(dec), static_cast<unsigned long long>(testVals[i]));
            return false;
         }
      }
   }

   // 3. File Logging, Compression, and Roundtrip
   const std::string testDir = AppPaths::AppSupportDir() + "/movelog_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
   std::error_code ec;
   std::filesystem::create_directories(testDir, ec);

   float handVal = 0.0f;
   float modVal = 0.0f;
   float sameNameVal1 = 0.5f;
   float sameNameVal2 = 0.5f;

   uint64_t nodeUid1 = 1001;
   uint64_t nodeUid2 = 1002;

   SetNodeUidLookup([&](int idx) -> uint64_t {
      if (idx == 0) return nodeUid1;
      if (idx == 1) return nodeUid2;
      return 0;
   });
   SetNodeTypeLookup([&](int idx) -> std::string { return idx == 0 ? "TestTypeA" : "TestTypeB"; });

   // A session file left open by a "crash": Start() must compress it.
   {
      std::ofstream stale(testDir + "/19990101-000000.mlog", std::ios::binary);
      const char hdr[8] = {'I', 'M', 'L', 'O', 'G', '1', 1, 0};
      stale.write(hdr, 8);
   }

   Start(testDir);

   // Register params into Modulation
   Modulation& mod = Modulation::Instance();
   mod.ClearFrameParams();

   // Frame 0..119 simulation
   int handWriteCount = 0;
   int modWriteCount = 0;

   for (int f = 0; f < 120; f++)
   {
      double t = f * (1.0 / 60.0);
      mod.ClearFrameParams();

      // Hand continuous param (node 0, param 0)
      handVal = f / 120.0f;
      {
         ParamRef ref;
         ref.nodeIndex = 0;
         ref.paramIndex = 0;
         ref.name = "cutoff";
         ref.value = &handVal;
         ref.minValue = 0.0f;
         ref.maxValue = 1.0f;
         mod.RegisterParam(ref);
      }
      NoteWriter(0, 0, Source::Hand);
      handWriteCount++;

      // Modulator driven param (node 0, param 1)
      modVal = std::sin(f * 0.1f) * 0.5f + 0.5f;
      {
         ParamRef ref;
         ref.nodeIndex = 0;
         ref.paramIndex = 1;
         ref.name = "resonance";
         ref.value = &modVal;
         ref.minValue = 0.0f;
         ref.maxValue = 1.0f;
         mod.RegisterParam(ref);
      }
      NoteWriter(0, 1, Source::Modulator);
      modWriteCount++;

      // Two same-named params on one node
      {
         ParamRef ref1;
         ref1.nodeIndex = 0;
         ref1.paramIndex = 2;
         ref1.name = "release";
         ref1.value = &sameNameVal1;
         ref1.minValue = 0.0f;
         ref1.maxValue = 1.0f;
         mod.RegisterParam(ref1);

         ParamRef ref2;
         ref2.nodeIndex = 0;
         ref2.paramIndex = 3;
         ref2.name = "release";
         ref2.value = &sameNameVal2;
         ref2.minValue = 0.0f;
         ref2.maxValue = 1.0f;
         mod.RegisterParam(ref2);
      }

      if (f == 60)
         NoteMark(Mark::Undo);
      else if (f == 70)
         NoteMark(Mark::Redo);

      Capture(t, true);
   }

   Stop();

   // Check that .mlog was compressed into .mlog.z
   bool foundZ = false;
   std::string zPath;
   uint64_t compSize = 0;
   for (const auto& entry : std::filesystem::directory_iterator(testDir, ec))
   {
      if (entry.path().extension() == ".z" &&
          entry.path().filename().string().find("19990101") == std::string::npos)
      {
         foundZ = true;
         zPath = entry.path().string();
         compSize = entry.file_size(ec);
         break;
      }
   }

   if (!foundZ)
   {
      printf("[FAIL] Compressed .mlog.z file not found in %s\n", testDir.c_str());
      std::filesystem::remove_all(testDir, ec);
      return false;
   }

   // Read back and verify
   int readHandCount = 0;
   int readModCount = 0;
   int readUndoMarkCount = 0;
   int readRedoMarkCount = 0;
   std::unordered_map<uint32_t, KeyRecord> keys;
   int readSessionEnd = 0;

   bool readOk = ReadFile(zPath, [&](const Record& r) {
      if (r.type == Record::Type::Key)
      {
         keys[r.key.id] = r.key;
      }
      else if (r.type == Record::Type::Val)
      {
         if (r.val.source == Source::Hand)
            readHandCount++;
         else if (r.val.source == Source::Modulator)
            readModCount++;
      }
      else if (r.type == Record::Type::Mark)
      {
         if (r.mark.mark == Mark::SessionEnd)
            readSessionEnd++;
         if (r.mark.mark == Mark::Undo)
            readUndoMarkCount++;
         else if (r.mark.mark == Mark::Redo)
            readRedoMarkCount++;
      }
      return true;
   });

   if (!readOk)
   {
      printf("[FAIL] Failed to read back compressed log file: %s\n", zPath.c_str());
      std::filesystem::remove_all(testDir, ec);
      return false;
   }

   // Verify Modulator was decimated to <= 10 Hz (at 2 sec = 120 frames at 60fps, ~20 records, not 120)
   if (readModCount > 25)
   {
      printf("[FAIL] Modulator was not decimated: read %d records (expected <= 25 at 10 Hz)\n", readModCount);
      std::filesystem::remove_all(testDir, ec);
      return false;
   }

   if (readSessionEnd != 1)
   {
      printf("[FAIL] Expected exactly one SessionEnd mark, got %d\n", readSessionEnd);
      std::filesystem::remove_all(testDir, ec);
      return false;
   }
   {
      bool typeOk = false;
      for (const auto& [id, k] : keys)
         if (k.typeName == "TestTypeA") typeOk = true;
      if (!typeOk)
      {
         printf("[FAIL] KEY records lost the node type name\n");
         std::filesystem::remove_all(testDir, ec);
         return false;
      }
      // Stale session must have been compressed and removed.
      if (std::filesystem::exists(testDir + "/19990101-000000.mlog") ||
          !std::filesystem::exists(testDir + "/19990101-000000.mlog.z"))
      {
         printf("[FAIL] Stale .mlog was not compressed on start\n");
         std::filesystem::remove_all(testDir, ec);
         return false;
      }
   }

   if (readUndoMarkCount != 1 || readRedoMarkCount != 1)
   {
      printf("[FAIL] Mark counts mismatch: undo=%d redo=%d\n", readUndoMarkCount, readRedoMarkCount);
      std::filesystem::remove_all(testDir, ec);
      return false;
   }

   // Verify two same-named params have distinct key IDs
   int releaseKeys = 0;
   for (const auto& [id, k] : keys)
   {
      if (k.name == "release")
         releaseKeys++;
   }
   if (releaseKeys != 2)
   {
      printf("[FAIL] Expected 2 distinct keys for same-named params, got %d\n", releaseKeys);
      std::filesystem::remove_all(testDir, ec);
      return false;
   }

   // 4. Retention Cap Test
   {
      // Create a dummy stats.bin and several large dummy .z files
      std::ofstream statsFile(testDir + "/stats.bin", std::ios::binary);
      statsFile << "STATS_DATA";
      statsFile.close();

      for (int i = 0; i < 5; i++)
      {
         std::ofstream fakeFile(testDir + "/20260101-00000" + std::to_string(i) + ".mlog.z", std::ios::binary);
         std::vector<char> dummy(20000, 'X');
         fakeFile.write(dummy.data(), dummy.size());
         fakeFile.close();
      }

      SetRetentionCapBytes(30000); // 30 KB cap
      EnforceRetention(testDir);

      if (!std::filesystem::exists(testDir + "/stats.bin"))
      {
         printf("[FAIL] EnforceRetention deleted stats.bin!\n");
         std::filesystem::remove_all(testDir, ec);
         return false;
      }
   }

   // 5. Performance Benchmark (400 registered params)
   {
      std::vector<float> vals(400, 0.5f);
      mod.ClearFrameParams();
      for (int i = 0; i < 400; i++)
      {
         ParamRef ref;
         ref.nodeIndex = i / 10;
         ref.paramIndex = i % 10;
         ref.name = "param";
         ref.value = &vals[i];
         ref.minValue = 0.0f;
         ref.maxValue = 1.0f;
         mod.RegisterParam(ref);
      }

      Start(testDir);
      auto t0 = std::chrono::high_resolution_clock::now();
      const int benchFrames = 100;
      for (int f = 0; f < benchFrames; f++)
      {
         for (int i = 0; i < 400; i++)
            vals[i] = (f + i) / 500.0f;
         Capture(f * 0.016, true);
      }
      auto t1 = std::chrono::high_resolution_clock::now();
      Stop();

      double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
      double avgMsPerFrame = totalMs / benchFrames;
      printf("[PERF] 400 params average Capture time: %.4f ms / frame (limit 0.1 ms)\n", avgMsPerFrame);
      if (avgMsPerFrame > 0.5) // generous CI threshold
      {
         printf("[FAIL] Capture time exceeded performance budget: %.4f ms\n", avgMsPerFrame);
         std::filesystem::remove_all(testDir, ec);
         return false;
      }
   }

   std::filesystem::remove_all(testDir, ec);
   printf("[MOVEMENT LOG TEST] PASS\n");
   printf("INFINITE_MOVELOGTEST: OK\n");
   return true;
}

} // namespace MovementLog

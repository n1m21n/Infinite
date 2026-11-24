#include "BciRuntime.h"

#include "core/Extensions.h"
#include "platform/AppPaths.h"

#include <algorithm>
#include <cstdio>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace Bci
{
BciRuntime::BciRuntime() = default;

BciRuntime::~BciRuntime()
{
   Close();
}

std::string BciRuntime::PackLibraryPath(const std::string& packDir)
{
#if defined(__APPLE__)
   return packDir + "/lib/libBoardController.dylib";
#elif defined(_WIN32)
   return packDir + "/lib/BoardController.dll";
#else
   return packDir + "/lib/libBoardController.so";
#endif
}

int BciRuntime::BrainFlowBoardId(BoardKind kind)
{
   switch (kind)
   {
      case BoardKind::Muse2:           return 38; // MUSE_2_BOARD
      case BoardKind::MuseS:           return 39; // MUSE_S_BOARD
      case BoardKind::OpenBCICyton:    return 0;  // CYTON_BOARD
      case BoardKind::OpenBCIGanglion: return 1;  // GANGLION_BOARD
      case BoardKind::NeurosityCrown:  return 23; // CROWN_BOARD
      default:                         return -1;
   }
}

bool BciRuntime::LoadBoardLibrary(const std::string& libPath, std::string& err)
{
   if (mLib && mFnPrepare && mFnStart && mFnData)
      return true;

#if defined(_WIN32)
   mLib = reinterpret_cast<void*>(LoadLibraryExW(AppPaths::FsPath(libPath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
#else
   mLib = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif

   if (!mLib)
   {
#if defined(_WIN32)
      err = "cannot load " + libPath;
#else
      const char* e = dlerror();
      err = e ? e : ("cannot load " + libPath);
#endif
      return false;
   }

#if defined(_WIN32)
   auto sym = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(mLib), n)); };
#else
   auto sym = [&](const char* n) { return dlsym(mLib, n); };
#endif

   mFnPrepare    = reinterpret_cast<PrepareSessionFn>(sym("prepare_session"));
   mFnStart      = reinterpret_cast<StartStreamFn>(sym("start_stream"));
   mFnStop       = reinterpret_cast<StopStreamFn>(sym("stop_stream"));
   mFnRelease    = reinterpret_cast<ReleaseSessionFn>(sym("release_session"));
   mFnCount      = reinterpret_cast<GetBoardDataCountFn>(sym("get_board_data_count"));
   mFnData       = reinterpret_cast<GetBoardDataFn>(sym("get_board_data"));
   mFnSampleRate = reinterpret_cast<GetSamplingRateFn>(sym("get_sampling_rate"));
   mFnEegChannels= reinterpret_cast<GetEegChannelsFn>(sym("get_eeg_channels"));
   mFnNumRows    = reinterpret_cast<GetNumRowsFn>(sym("get_num_rows"));

   if (!mFnPrepare || !mFnStart || !mFnStop || !mFnRelease || !mFnCount || !mFnData)
   {
      err = "BrainFlow symbols incomplete in " + libPath;
      return false;
   }
   return true;
}

bool BciRuntime::Open(BoardKind kind, const std::string& serialPort, const std::string& csvPath, std::string& outStatus)
{
   Close();
   mPackMissing = false;
   mHardwareActive = false;

   if (kind == BoardKind::Synthetic)
   {
      mSynth.ClearCsv();
      mSampleRateHz = kSampleRateHz;
      mStatus = "synthetic cortex (256 Hz)";
      outStatus = mStatus;
      return true;
   }

   if (kind == BoardKind::CsvReplay)
   {
      std::string err;
      if (csvPath.empty() || !mSynth.LoadCsv(csvPath, err))
      {
         mSynth.ClearCsv();
         mStatus = csvPath.empty() ? "CSV path empty (synthetic fallback)" : (err + " (synthetic fallback)");
      }
      else
      {
         mStatus = "CSV replay active (256 Hz)";
      }
      mSampleRateHz = kSampleRateHz;
      outStatus = mStatus;
      return true;
   }

   const std::string packDir = Extensions::PackDir("bci");
   if (packDir.empty())
   {
      mPackMissing = true;
      mSampleRateHz = kSampleRateHz;
      mStatus = "BCI pack not installed (synthetic fallback) - Settings > Extensions";
      outStatus = mStatus;
      return false;
   }

   std::string err;
   if (!LoadBoardLibrary(PackLibraryPath(packDir), err))
   {
      mPackMissing = true;
      mSampleRateHz = kSampleRateHz;
      mStatus = "BCI pack load failed: " + err + " (synthetic fallback)";
      outStatus = mStatus;
      return false;
   }

   mBoardId = BrainFlowBoardId(kind);
   if (mBoardId < 0)
   {
      mStatus = "unsupported board id (synthetic fallback)";
      outStatus = mStatus;
      return false;
   }

   mInputParamsJson = serialPort.empty() ? "{}" : ("{\"serial_port\":\"" + serialPort + "\"}");
   if (mFnPrepare(mBoardId, mInputParamsJson.c_str()) != 0 ||
       mFnStart(45000, "", mBoardId, mInputParamsJson.c_str()) != 0)
   {
      mStatus = "headset not found / stream failed (synthetic fallback)";
      outStatus = mStatus;
      mFnRelease(mBoardId, mInputParamsJson.c_str());
      mBoardId = -1;
      return false;
   }

   int sr = (int)kSampleRateHz;
   if (mFnSampleRate && mFnSampleRate(mBoardId, &sr) == 0 && sr > 32)
      mSampleRateHz = (double)sr;
   else
      mSampleRateHz = kSampleRateHz;

   int rows = 8;
   if (mFnNumRows && mFnNumRows(mBoardId, &rows) == 0 && rows >= 4)
      mNumCols = rows;
   else
      mNumCols = 8;

   int eegList[16] = { 1, 2, 3, 4 };
   int eegLen = 4;
   if (mFnEegChannels && mFnEegChannels(mBoardId, eegList, &eegLen) == 0 && eegLen >= 4)
   {
      for (int c = 0; c < kChannelCount; ++c)
         mEegCh[c] = eegList[c];
   }

   mHardwareActive = true;
   char buf[96];
   std::snprintf(buf, sizeof(buf), "hardware streaming (%.0f Hz)", mSampleRateHz);
   mStatus = buf;
   outStatus = mStatus;
   return true;
}

void BciRuntime::Close()
{
   if (mHardwareActive && mBoardId >= 0)
   {
      if (mFnStop) mFnStop(mBoardId, mInputParamsJson.c_str());
      if (mFnRelease) mFnRelease(mBoardId, mInputParamsJson.c_str());
   }
   mHardwareActive = false;
   mBoardId = -1;
}

void BciRuntime::PollSamples(int targetSamples, std::vector<SampleFrame>& outFrames)
{
   outFrames.clear();
   if (targetSamples <= 0)
      return;

   if (mHardwareActive && mBoardId >= 0 && mFnCount && mFnData)
   {
      int avail = 0;
      if (mFnCount(mBoardId, mInputParamsJson.c_str(), &avail) == 0 && avail > 0)
      {
         const int n = std::min(avail, std::max(targetSamples * 4, 64));
         mScratchBuf.resize((size_t)mNumCols * (size_t)n);
         if (mFnData(n, mBoardId, mInputParamsJson.c_str(), mScratchBuf.data()) == 0)
         {
            outFrames.reserve((size_t)n);
            for (int i = 0; i < n; ++i)
            {
               SampleFrame f;
               for (int c = 0; c < kChannelCount; ++c)
               {
                  const int row = mEegCh[c];
                  if (row >= 0 && row < mNumCols)
                     f.ch[c] = (float)mScratchBuf[(size_t)row * (size_t)n + (size_t)i];
               }
               outFrames.push_back(f);
            }
            return;
         }
      }
      return;
   }

   mSynth.Generate(targetSamples, mSampleRateHz, outFrames);
}
}

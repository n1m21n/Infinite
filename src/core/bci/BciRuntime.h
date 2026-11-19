#pragma once

#include "BciTypes.h"
#include "SyntheticCortex.h"

#include <string>
#include <vector>

namespace Bci
{
   // Dynamic runtime loader for BrainFlow C++ C-ABI shared library (`Extensions::PackDir("bci")`).
   // Zero link-time dependency: when no hardware pack is installed or when Synthetic / CSV Replay
   // is selected, falls back seamlessly to the built-in biophysical `SyntheticCortex`.
   class BciRuntime
   {
   public:
      BciRuntime();
      ~BciRuntime();

      static std::string PackLibraryPath(const std::string& packDir);
      static int BrainFlowBoardId(BoardKind kind);

      // Opens either a live BrainFlow hardware stream (when `bci` extension pack is present)
      // or the built-in SyntheticCortex / CSV replay stream.
      bool Open(BoardKind kind, const std::string& serialPort, const std::string& csvPath, std::string& outStatus);
      void Close();

      bool IsHardwareActive() const { return mHardwareActive; }
      bool IsPackMissing() const { return mPackMissing; }
      double SampleRateHz() const { return mSampleRateHz; }
      const std::string& Status() const { return mStatus; }

      SyntheticCortex& Synth() { return mSynth; }
      const SyntheticCortex& Synth() const { return mSynth; }

      // Reads up to `maxSamples` frames (non-blocking for hardware, generated on demand for Synthetic).
      void PollSamples(int targetSamples, std::vector<SampleFrame>& outFrames);

   private:
      bool LoadLibrary(const std::string& libPath, std::string& err);

      void* mLib = nullptr;
      bool mHardwareActive = false;
      bool mPackMissing = false;
      int mBoardId = -1;
      int mNumCols = 0;
      int mEegCh[kChannelCount] = { 1, 2, 3, 4 };
      double mSampleRateHz = kSampleRateHz;
      std::string mInputParamsJson;
      std::string mStatus = "synthetic cortex ready";

      // Resolved BrainFlow C ABI function pointers (opaque, zero header dependency)
      using PrepareSessionFn = int (*)(int boardId, const char* paramsJson);
      using StartStreamFn = int (*)(int bufferSize, const char* streamerParams, int boardId, const char* paramsJson);
      using StopStreamFn = int (*)(int boardId, const char* paramsJson);
      using ReleaseSessionFn = int (*)(int boardId, const char* paramsJson);
      using GetBoardDataCountFn = int (*)(int boardId, const char* paramsJson, int* count);
      using GetBoardDataFn = int (*)(int numSamples, int boardId, const char* paramsJson, double* dataBuf);
      using GetSamplingRateFn = int (*)(int boardId, int* samplingRate);
      using GetEegChannelsFn = int (*)(int boardId, int* channels, int* len);
      using GetNumRowsFn = int (*)(int boardId, int* numRows);

      PrepareSessionFn mFnPrepare = nullptr;
      StartStreamFn mFnStart = nullptr;
      StopStreamFn mFnStop = nullptr;
      ReleaseSessionFn mFnRelease = nullptr;
      GetBoardDataCountFn mFnCount = nullptr;
      GetBoardDataFn mFnData = nullptr;
      GetSamplingRateFn mFnSampleRate = nullptr;
      GetEegChannelsFn mFnEegChannels = nullptr;
      GetNumRowsFn mFnNumRows = nullptr;

      std::vector<double> mScratchBuf;
      SyntheticCortex mSynth;
   };
}

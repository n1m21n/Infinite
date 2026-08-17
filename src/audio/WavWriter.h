#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Minimal 16-bit PCM RIFF/WAVE writer. Main thread only - Append is called
// from the per-frame drain of an AudioCaptureRing, never from the audio
// thread. Writes incrementally (so a recording's length is unbounded rather
// than needing the whole buffer in memory) and patches the RIFF/data chunk
// sizes on Close().
class WavWriter
{
public:
   ~WavWriter() { Close(); }

   bool IsOpen() const { return mFile != nullptr; }

   // Opens `path` and writes a placeholder header (sizes patched in on
   // Close()). Returns false (and leaves nothing open) if the file couldn't
   // be created.
   bool Open(const std::string& path, double sampleRate, int numChannels);

   // Converts `frames` interleaved float samples (numChannels() per frame,
   // already at Open()'s channel count) to 16-bit PCM and appends them.
   void Append(const float* interleaved, int frames);

   // Seeks back and patches the RIFF and data chunk sizes, then closes the
   // file. Safe to call on an already-closed writer.
   void Close();

   int64_t FramesWritten() const { return mFramesWritten; }
   int64_t BytesWritten() const { return mFramesWritten * mNumChannels * 2; }
   const std::string& Path() const { return mPath; }

private:
   FILE* mFile = nullptr;
   std::string mPath;
   int mNumChannels = 2;
   int64_t mFramesWritten = 0;
};

namespace AudioRecordings
{
   // Returns ~/Library/Application Support/Infinite/Recordings (or /tmp/Infinite/Recordings fallback).
   // Creates the directory if it does not exist.
   std::string GetRecordingsDirectory();

   // Generates a timestamped unique WAV filename inside GetRecordingsDirectory(),
   // e.g. ".../Recordings/recording_paulstretch_20260817_221530_001_1.wav".
   std::string GenerateFilePath(const std::string& prefix);

   // Writes mono or interleaved multi-channel float PCM audio frames directly to a 16-bit WAV file on disk.
   // Returns true on success.
   bool WriteWav(const std::string& path, const float* data, int frames, double sampleRate, int numChannels = 1);
}


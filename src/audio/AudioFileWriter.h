#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Multi-format audio file writer supporting WAV (.wav), FLAC (.flac), and MP3 (.mp3).
// Main thread only - Append is called from the per-frame drain of an AudioCaptureRing.
class AudioFileWriter
{
public:
   enum class Format
   {
      Auto,
      Wav,
      Flac,
      Mp3
   };

   AudioFileWriter();
   ~AudioFileWriter();

   bool IsOpen() const;

   // Opens `path` in the format deduced from extension (or explicitly specified).
   // Returns false if the file couldn't be created or initialized.
   bool Open(const std::string& path, double sampleRate, int numChannels, Format format = Format::Auto);

   // Converts `frames` interleaved float samples to the appropriate destination format and appends them.
   void Append(const float* interleaved, int frames);

   // Finalizes file headers / tags / encoders and closes the file. Safe to call multiple times.
   void Close();

   int64_t FramesWritten() const { return mFramesWritten; }
   int64_t BytesWritten() const;
   const std::string& Path() const { return mPath; }
   Format GetFormat() const { return mFormat; }

   static Format FormatFromPath(const std::string& path);
   static const char* ExtensionForFormat(Format format);

private:
   void CloseWav();
   void CloseFlac();
   void CloseMp3();

   std::string mPath;
   Format mFormat = Format::Wav;
   int mNumChannels = 2;
   double mSampleRate = 44100.0;
   int64_t mFramesWritten = 0;
   int64_t mBytesWrittenDirect = 0;

   // WAV state
   FILE* mFile = nullptr;

   // FLAC state (libFLAC stream encoder on Windows, named for the CoreAudio
   // handle it held before the port)
   void* mExtAudioFile = nullptr;

   // MP3 state
   void* mShineHandle = nullptr;
   FILE* mMp3File = nullptr;
};

// Aliased as WavWriter for backwards compatibility with any existing nodes
using WavWriter = AudioFileWriter;

namespace AudioRecordings
{
   std::string GetRecordingsDirectory();
   std::string GenerateFilePath(const std::string& prefix, const std::string& extension = "wav");
   bool WriteWav(const std::string& path, const float* data, int frames, double sampleRate, int numChannels = 1);
}

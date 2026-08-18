#include "AudioFileWriter.h"
#include "shine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <chrono>
#include <ctime>
#include <atomic>
#include <sys/stat.h>

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

namespace
{
   void WriteU32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
   void WriteU16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

   int16_t FloatToPcm16(float v)
   {
      const float clamped = std::clamp(v, -1.0f, 1.0f);
      return (int16_t)std::lround(clamped * 32767.0f);
   }

   std::string ToLower(const std::string& str)
   {
      std::string res = str;
      for (char& c : res)
         c = (char)tolower((unsigned char)c);
      return res;
   }
}

AudioFileWriter::AudioFileWriter() = default;

AudioFileWriter::~AudioFileWriter()
{
   Close();
}

bool AudioFileWriter::IsOpen() const
{
   return mFile != nullptr || mExtAudioFile != nullptr || (mMp3File != nullptr && mShineHandle != nullptr);
}

AudioFileWriter::Format AudioFileWriter::FormatFromPath(const std::string& path)
{
   std::string lower = ToLower(path);
   if (lower.length() >= 5 && lower.substr(lower.length() - 5) == ".flac")
      return Format::Flac;
   if (lower.length() >= 4 && lower.substr(lower.length() - 4) == ".mp3")
      return Format::Mp3;
   return Format::Wav;
}

const char* AudioFileWriter::ExtensionForFormat(Format format)
{
   switch (format)
   {
      case Format::Flac: return "flac";
      case Format::Mp3:  return "mp3";
      case Format::Wav:
      default:           return "wav";
   }
}

int64_t AudioFileWriter::BytesWritten() const
{
   if (mFormat == Format::Wav)
      return mFramesWritten * mNumChannels * 2;
   return mBytesWrittenDirect;
}

bool AudioFileWriter::Open(const std::string& path, double sampleRate, int numChannels, Format format)
{
   Close();

   if (sampleRate <= 0.0 || numChannels <= 0)
      return false;

   mPath = path;
   mSampleRate = sampleRate;
   mNumChannels = std::max(1, numChannels);
   mFramesWritten = 0;
   mBytesWrittenDirect = 0;

   if (format == Format::Auto)
      mFormat = FormatFromPath(path);
   else
      mFormat = format;

   if (mFormat == Format::Flac)
   {
      @autoreleasepool
      {
         AudioStreamBasicDescription clientFormat = {};
         clientFormat.mSampleRate = sampleRate;
         clientFormat.mFormatID = kAudioFormatLinearPCM;
         clientFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
         clientFormat.mBitsPerChannel = 32;
         clientFormat.mChannelsPerFrame = (UInt32)mNumChannels;
         clientFormat.mBytesPerFrame = (UInt32)(mNumChannels * sizeof(float));
         clientFormat.mFramesPerPacket = 1;
         clientFormat.mBytesPerPacket = clientFormat.mBytesPerFrame;

         AudioStreamBasicDescription outFormat = {};
         outFormat.mSampleRate = sampleRate;
         outFormat.mFormatID = kAudioFormatFLAC;
         outFormat.mChannelsPerFrame = (UInt32)mNumChannels;

         ExtAudioFileRef extRef = NULL;
         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         NSURL* fileURL = [NSURL fileURLWithPath:nsPath];
         [[NSFileManager defaultManager] removeItemAtURL:fileURL error:nil];

         OSStatus err = ExtAudioFileCreateWithURL((__bridge CFURLRef)fileURL,
                                                  kAudioFileFLACType,
                                                  &outFormat,
                                                  NULL,
                                                  kAudioFileFlags_EraseFile,
                                                  &extRef);
         if (err != noErr || extRef == NULL)
         {
            // Fallback to WAV if FLAC encoder fails
            mFormat = Format::Wav;
         }
         else
         {
            ExtAudioFileSetProperty(extRef, kExtAudioFileProperty_ClientDataFormat, sizeof(clientFormat), &clientFormat);
            mExtAudioFile = (void*)extRef;
            return true;
         }
      }
   }

   if (mFormat == Format::Mp3)
   {
      shine_config_t config;
      shine_set_config_mpeg_defaults(&config.mpeg);
      config.wave.samplerate = (int)std::lround(sampleRate);
      config.wave.channels = (mNumChannels == 1) ? PCM_MONO : PCM_STEREO;
      config.mpeg.bitr = 192;

      shine_t shine = shine_initialize(&config);
      if (!shine)
      {
         mFormat = Format::Wav;
      }
      else
      {
         mMp3File = fopen(path.c_str(), "wb");
         if (!mMp3File)
         {
            shine_close(shine);
            return false;
         }
         mShineHandle = (void*)shine;
         return true;
      }
   }

   // Default / WAV format
   mFormat = Format::Wav;
   mFile = fopen(path.c_str(), "wb");
   if (mFile == nullptr)
      return false;

   const uint32_t sr = (uint32_t)std::lround(sampleRate);
   const uint16_t bitsPerSample = 16;
   const uint16_t blockAlign = (uint16_t)(mNumChannels * (bitsPerSample / 8));
   const uint32_t byteRate = sr * blockAlign;

   fwrite("RIFF", 1, 4, mFile);
   WriteU32(mFile, 0);
   fwrite("WAVE", 1, 4, mFile);

   fwrite("fmt ", 1, 4, mFile);
   WriteU32(mFile, 16); // PCM fmt chunk size
   WriteU16(mFile, 1);  // PCM
   WriteU16(mFile, (uint16_t)mNumChannels);
   WriteU32(mFile, sr);
   WriteU32(mFile, byteRate);
   WriteU16(mFile, blockAlign);
   WriteU16(mFile, bitsPerSample);

   fwrite("data", 1, 4, mFile);
   WriteU32(mFile, 0);

   return true;
}

void AudioFileWriter::Append(const float* interleaved, int frames)
{
   if (frames <= 0 || !IsOpen())
      return;

   if (mFormat == Format::Flac && mExtAudioFile != nullptr)
   {
      AudioBufferList bufferList;
      bufferList.mNumberBuffers = 1;
      bufferList.mBuffers[0].mNumberChannels = (UInt32)mNumChannels;
      bufferList.mBuffers[0].mDataByteSize = (UInt32)(frames * mNumChannels * sizeof(float));
      bufferList.mBuffers[0].mData = (void*)interleaved;

      ExtAudioFileRef extRef = (ExtAudioFileRef)mExtAudioFile;
      ExtAudioFileWrite(extRef, (UInt32)frames, &bufferList);
      mFramesWritten += frames;
      mBytesWrittenDirect += (int64_t)(frames * mNumChannels * 1.5); // approximate FLAC compressed size
      return;
   }

   if (mFormat == Format::Mp3 && mShineHandle != nullptr && mMp3File != nullptr)
   {
      const int numSamples = frames * mNumChannels;
      static thread_local std::vector<int16_t> scratch;
      scratch.resize((size_t)numSamples);
      for (int i = 0; i < numSamples; i++)
         scratch[i] = FloatToPcm16(interleaved[i]);

      shine_t s = (shine_t)mShineHandle;
      const int samplesPerPass = shine_samples_per_pass(s) * mNumChannels;
      int offset = 0;
      while (offset + samplesPerPass <= numSamples)
      {
         int written = 0;
         unsigned char* data = shine_encode_buffer_interleaved(s, scratch.data() + offset, &written);
         if (written > 0 && data != nullptr)
         {
            fwrite(data, 1, (size_t)written, mMp3File);
            mBytesWrittenDirect += written;
         }
         offset += samplesPerPass;
      }
      if (offset < numSamples)
      {
         int remainingSamples = numSamples - offset;
         std::vector<int16_t> padded(samplesPerPass, 0);
         std::memcpy(padded.data(), scratch.data() + offset, remainingSamples * sizeof(int16_t));
         int written = 0;
         unsigned char* data = shine_encode_buffer_interleaved(s, padded.data(), &written);
         if (written > 0 && data != nullptr)
         {
            fwrite(data, 1, (size_t)written, mMp3File);
            mBytesWrittenDirect += written;
         }
      }
      mFramesWritten += frames;
      return;
   }

   if (mFormat == Format::Wav && mFile != nullptr)
   {
      const int numSamples = frames * mNumChannels;
      static thread_local std::vector<int16_t> scratch;
      scratch.resize((size_t)numSamples);
      for (int i = 0; i < numSamples; i++)
         scratch[i] = FloatToPcm16(interleaved[i]);

      fwrite(scratch.data(), sizeof(int16_t), (size_t)numSamples, mFile);
      mFramesWritten += frames;
   }
}

void AudioFileWriter::CloseFlac()
{
   if (mExtAudioFile != nullptr)
   {
      ExtAudioFileRef extRef = (ExtAudioFileRef)mExtAudioFile;
      ExtAudioFileDispose(extRef);
      mExtAudioFile = nullptr;

      // Update actual file size
      struct stat st;
      if (stat(mPath.c_str(), &st) == 0)
         mBytesWrittenDirect = st.st_size;
   }
}

void AudioFileWriter::CloseMp3()
{
   if (mShineHandle != nullptr)
   {
      shine_t s = (shine_t)mShineHandle;
      int written = 0;
      unsigned char* data = shine_flush(s, &written);
      if (written > 0 && data != nullptr && mMp3File != nullptr)
      {
         fwrite(data, 1, (size_t)written, mMp3File);
         mBytesWrittenDirect += written;
      }
      shine_close(s);
      mShineHandle = nullptr;
   }
   if (mMp3File != nullptr)
   {
      fclose(mMp3File);
      mMp3File = nullptr;

      struct stat st;
      if (stat(mPath.c_str(), &st) == 0)
         mBytesWrittenDirect = st.st_size;
   }
}

void AudioFileWriter::CloseWav()
{
   if (mFile == nullptr)
      return;

   const uint32_t dataBytes = (uint32_t)(mFramesWritten * mNumChannels * 2);
   const uint32_t riffSize = 36 + dataBytes;

   fseek(mFile, 4, SEEK_SET);
   WriteU32(mFile, riffSize);
   fseek(mFile, 40, SEEK_SET);
   WriteU32(mFile, dataBytes);

   fclose(mFile);
   mFile = nullptr;
}

void AudioFileWriter::Close()
{
   CloseWav();
   CloseFlac();
   CloseMp3();
}

namespace AudioRecordings
{
   std::string GetRecordingsDirectory()
   {
      const char* home = getenv("HOME");
      std::string base;
      if (home != nullptr)
         base = std::string(home) + "/Library/Application Support/Infinite";
      else
         base = "/tmp/Infinite";

      mkdir(base.c_str(), 0755);
      std::string dir = base + "/Recordings";
      mkdir(dir.c_str(), 0755);
      return dir;
   }

   std::string GenerateFilePath(const std::string& prefix, const std::string& extension)
   {
      const std::string dir = GetRecordingsDirectory();
      auto now = std::chrono::system_clock::now();
      auto timeT = std::chrono::system_clock::to_time_t(now);
      std::tm tmVal {};
      localtime_r(&timeT, &tmVal);

      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
      static std::atomic<uint32_t> sCounter{ 0 };
      uint32_t count = ++sCounter;

      char filename[256];
      snprintf(filename, sizeof(filename), "recording_%s_%04d%02d%02d_%02d%02d%02d_%03d_%u.%s",
               prefix.c_str(),
               tmVal.tm_year + 1900, tmVal.tm_mon + 1, tmVal.tm_mday,
               tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec,
               (int)ms.count(), count, extension.c_str());

      return dir + "/" + filename;
   }

   bool WriteWav(const std::string& path, const float* data, int frames, double sampleRate, int numChannels)
   {
      if (data == nullptr || frames <= 0 || sampleRate <= 0.0)
         return false;

      AudioFileWriter writer;
      if (!writer.Open(path, sampleRate, numChannels, AudioFileWriter::Format::Wav))
         return false;

      writer.Append(data, frames);
      writer.Close();
      return true;
   }
}

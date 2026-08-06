#pragma once

#include <string>
#include <vector>

// Thin macOS shims kept out of the C++ translation units.
namespace Platform
{
   // Native open panel filtered to image types. Returns "" if cancelled.
   std::string OpenImageDialog();

   // Decodes any image format the OS understands (png/jpeg/tiff/heic/webp/raw/...)
   // into tightly packed RGBA8, already row-flipped for OpenGL's bottom-up
   // texture convention. Returns false and fills outError on failure.
   bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& outPixels,
                      int& outWidth, int& outHeight, std::string& outError);

   // ---- video decoding ----------------------------------------------------
   // Opaque handle around an AVAssetReader that decodes frames in order.
   // Frames are delivered as RGBA8, already row-flipped for OpenGL.
   struct VideoHandle;

   std::string OpenVideoDialog();

   VideoHandle* VideoOpen(const std::string& path, std::string& outError);
   void VideoClose(VideoHandle* handle);
   int VideoWidth(VideoHandle* handle);
   int VideoHeight(VideoHandle* handle);
   double VideoDuration(VideoHandle* handle);

   // Decodes forward until the frame covering `seconds` is current. Returns true
   // when a new frame was produced (so the caller can skip re-uploading).
   // Seeking backwards restarts the reader, which is how looping works.
   bool VideoFrameAt(VideoHandle* handle, double seconds, std::vector<unsigned char>& outPixels);

   // ---- background removal ----
   // Uses Vision's on-device segmentation: no model download, no network, no
   // API key. Subject lifting (any salient foreground) needs macOS 14; person
   // segmentation works from macOS 12. Returns false with a reason otherwise.
   enum class MattingMode
   {
      Subject, // any salient foreground object (macOS 14+)
      Person   // people only (macOS 12+)
   };

   bool SubjectMask(const std::vector<unsigned char>& rgbaPixels, int width, int height,
                    MattingMode mode, std::vector<unsigned char>& outMask,
                    std::string& outError);

   // ---- audio input ----
   // Taps the default input device and keeps a running spectrum. Everything is
   // computed on the audio thread and read lock-free-ish by the render thread;
   // the graph only ever reads smoothed magnitudes, so a torn read is harmless.
   const int kAudioBands = 16;

   struct AudioLevels
   {
      float rms = 0.0f;
      float peak = 0.0f;
      float low = 0.0f;   // ~20-250 Hz
      float mid = 0.0f;   // ~250-2k
      float high = 0.0f;  // ~2k-16k
      float bands[kAudioBands] = { 0 };
      bool onset = false; // transient detected since the last read
   };

   // ---- audio file playback ----
   // Each player owns its own engine and its own analyser, so a file and the
   // live input can be analysed independently and at the same time.
   struct AudioPlayerHandle;

   std::string OpenAudioDialog();
   AudioPlayerHandle* AudioFileOpen(const std::string& path, std::string& outError);
   void AudioFileClose(AudioPlayerHandle* handle);
   void AudioFilePlay(AudioPlayerHandle* handle);
   void AudioFilePause(AudioPlayerHandle* handle);
   void AudioFileRestart(AudioPlayerHandle* handle);
   bool AudioFileIsPlaying(AudioPlayerHandle* handle);
   void AudioFileSetLoop(AudioPlayerHandle* handle, bool loop);
   void AudioFileSetVolume(AudioPlayerHandle* handle, float volume);
   void AudioFileSetMonitor(AudioPlayerHandle* handle, bool audible);
   double AudioFileDuration(AudioPlayerHandle* handle);
   double AudioFilePosition(AudioPlayerHandle* handle);
   bool AudioFileRead(AudioPlayerHandle* handle, AudioLevels& out);
   void AudioFileSetSmoothing(AudioPlayerHandle* handle, float attack, float release);
   void AudioFileSetGain(AudioPlayerHandle* handle, float gain);

   bool AudioStart(std::string& outError);
   void AudioStop();
   bool AudioIsRunning();
   std::string AudioDeviceName();
   // Returns false if audio is not running; `out` is then left at zero.
   bool AudioRead(AudioLevels& out);
   void AudioSetSmoothing(float attack, float release);
   void AudioSetGain(float gain);

   // ---- video recording ---------------------------------------------------
   struct RecorderHandle;

   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError);
   // `pixels` is RGBA8 bottom-up, exactly as glReadPixels returns it.
   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels);
   bool RecorderStop(RecorderHandle* handle, std::string& outError);
   int RecorderFrameCount(RecorderHandle* handle);
}

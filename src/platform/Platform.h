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

   // ---- 3D model loading --------------------------------------------------
   // Goes through ModelIO for the same reason image decoding goes through
   // ImageIO: the OS already reads OBJ, PLY, STL, USD and USDZ, and a bundled
   // importer would be a far heavier dependency than the code it replaces.
   // Not covered: glTF and FBX, which ModelIO does not read.
   //
   // Returns interleaved position/normal/uv vertices and triangle indices,
   // matching the layout of the Vertex struct in core/Mesh.h. Normals are
   // generated when the file has none.
   struct ModelVertex
   {
      float px = 0, py = 0, pz = 0;
      float nx = 0, ny = 0, nz = 1;
      float u = 0, v = 0;
   };

   std::string OpenModelDialog();

   // Patch files. Save returns the chosen path, or "" if cancelled.
   std::string OpenPatchDialog();
   std::string SavePatchDialog(const std::string& suggestedName);

   // ---- text outlines -----------------------------------------------------
   // Glyph outlines for a laid-out string, flattened to polygons in font units
   // normalised so cap height is roughly 1. Curves are subdivided here because
   // CoreGraphics has no public path-flattening call.
   //
   // Winding is whatever the font format uses and is deliberately not
   // normalised here - TrueType winds outer contours clockwise and CFF the
   // other way. The triangulator decides outline-versus-hole by nesting depth
   // instead, which works for both.
   struct TextContour
   {
      std::vector<float> points; // x,y pairs
   };

   // Installed font families, sorted, for the Text nodes' font pickers.
   const std::vector<std::string>& AvailableFontFamilies();

   bool GetTextOutlines(const std::string& text, const std::string& fontName,
                        float letterSpacing, std::vector<TextContour>& outContours,
                        std::string& outError);

   bool LoadModel(const std::string& path, std::vector<ModelVertex>& outVertices,
                  std::vector<unsigned int>& outIndices, std::string& outError);

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

   // audioPath is optional. When given, its samples are read independently of
   // any playback the Audio File node is doing and muxed in alongside the
   // video - recording does not depend on the file actually being audible.
   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError,
                                 const std::string& audioPath = std::string(),
                                 bool loopAudio = true);
   // `pixels` is RGBA8 bottom-up, exactly as glReadPixels returns it.
   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels);
   bool RecorderStop(RecorderHandle* handle, std::string& outError);
   int RecorderFrameCount(RecorderHandle* handle);

   // Inspects a finished recording. Used by the audio-mux self-test, and handy
   // for the UI later if recording ever needs to report back what it actually
   // wrote rather than what was asked for.
   struct MovieInfo
   {
      bool hasVideo = false;
      bool hasAudio = false;
      double duration = 0.0;
   };
   MovieInfo InspectMovie(const std::string& path);
}

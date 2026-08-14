#pragma once

#include <string>
#include <vector>

// Thin macOS shims kept out of the C++ translation units.
namespace Platform
{
   // Holds an NSProcessInfo activity token that opts the process out of App
   // Nap for as long as the app runs. Without this, macOS treats a window
   // that isn't receiving input as idle/background and throttles its timer
   // sources - GLFW's event-polling and vsync among them - so a clock-driven
   // redraw (e.g. Switcher 3D's auto-switch) stalls until a mouse event
   // wakes the process back up, then bursts through the backlog at once.
   // Call once at startup and keep the app running for the token to matter.
   void PreventAppNap();

   // Native open panel filtered to image types. Returns "" if cancelled.
   std::string OpenImageDialog();

   // Decodes any image format the OS understands (png/jpeg/tiff/heic/webp/raw/...)
   // into tightly packed RGBA8, already row-flipped for OpenGL's bottom-up
   // texture convention. Returns false and fills outError on failure.
   bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& outPixels,
                      int& outWidth, int& outHeight, std::string& outError);

   // Native open panel filtered to HDR equirectangular formats. ImageIO/NSImage
   // do not recognise .hdr, so this needs its own panel rather than reusing
   // OpenImageDialog - the same reason OpenModelDialog is separate from it.
   std::string OpenHdrDialog();

   // Decodes anything ImageIO can open (notably .exr, which macOS has read
   // natively since Ventura) into linear float RGB, via a CGBitmapContext in
   // the extended-linear-sRGB colour space so values above 1.0 survive rather
   // than clamping the way LoadImageRGBA's 8-bit path does. Not used for
   // .hdr: ImageIO does not read Radiance HDR, which is why that format goes
   // through stb_image instead.
   bool LoadImageFloatRGB(const std::string& path, std::vector<float>& outPixels,
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

   // ---- audio synthesis spike (P0 feasibility test, throwaway) ----
   // Raw AVAudioSourceNode generating a 440 Hz sine directly in its render
   // block, to measure whether a live synthesis callback coexists with the
   // render loop without glitches or a measurable FPS hit. Not a real node
   // type - see docs/plans/audio/P0-feasibility-prompt.md.
   struct AudioSpikeStats
   {
      double sampleRate = 0.0;
      int blockSize = 0;
      double maxJitterMs = 0.0;
      uint64_t callbackCount = 0;
   };

   bool AudioSpikeStart(std::string& outError);
   void AudioSpikeStop();
   AudioSpikeStats AudioSpikeGetStats();

   // ---- audio engine render callback bridge ----
   // Render callback bridge: called on the real-time audio thread. Must obey
   // every audio-thread prohibition in docs/plans/audio/P1a-engine-prompt.md -
   // no allocation, no locks, no std::function/map/string, no GL/ImGui/file I/O.
   // buffers is planar: buffers[ch][0..numFrames) for channel ch.
   typedef void (*AudioRenderCallback)(float** buffers, int numChannels, int numFrames, void* userData);

   // requestedDeviceId == 0 means "system default output device" (existing
   // behavior). requestedSampleRate == 0.0 / requestedBufferFrames == 0 mean
   // "use whatever the device is already running at" (also existing
   // behavior). A requested value the device rejects or clamps is not an
   // error - outSampleRate (and Platform::AudioDeviceBufferFrames below)
   // reflect what was actually negotiated, the same way the sample rate
   // readback already worked before this was added.
   bool AudioDeviceOpen(AudioRenderCallback callback, void* userData, double& outSampleRate, std::string& outError,
                        uint32_t requestedDeviceId = 0, double requestedSampleRate = 0.0,
                        int requestedBufferFrames = 0);
   void AudioDeviceClose();

   // Actual buffer frame size CoreAudio reports for the given device right
   // now (0 = system default output device). Buffer size is a device-wide
   // property, not something AVAudioEngine hands back per-open, so this
   // queries the hardware directly - lets the UI show what was really
   // negotiated after a requestedBufferFrames the device may have clamped.
   uint32_t AudioDeviceBufferFrames(uint32_t deviceId = 0);

   // ---- audio device enumeration (CoreAudio HAL) ----
   struct AudioDeviceInfo
   {
      std::string name;
      uint32_t deviceId = 0;
      bool isInput = false;
      bool isOutput = false;
   };

   std::vector<AudioDeviceInfo> AudioListDevices();

   // ---- sample library ----
   // Native open panel restricted to picking a single directory (no files).
   // Returns "" if cancelled. Used by the Samples search panel to add a
   // folder to scan.
   std::string OpenFolderDialog();

   // A whole audio file decoded to planar float PCM, ready for an audio-
   // thread node to read directly (no further per-block decoding). Decoding
   // itself only ever happens on the main thread, at load time.
   struct SampleBuffer
   {
      std::vector<float> channelData; // interleaved-free: channel 0 then channel 1, each numFrames long
      int channels = 0;
      int numFrames = 0;
      double sampleRate = 0.0;
   };

   // Decodes whatever AVFoundation reads natively (wav/aiff/caf/m4a/mp3/alac).
   // Returns false and fills outError on failure (missing file, unsupported
   // codec, etc). Main-thread only - not real-time safe.
   bool DecodeAudioFileToBuffer(const std::string& path, SampleBuffer& outBuffer, std::string& outError);

   // ---- audio input capture (for the node graph's Audio In node) ----
   // Taps the render device's own AVAudioEngine input node (the same engine
   // AudioDeviceOpen already runs) and buffers raw samples into a lock-free
   // ring, so AudioInputNode's audio-thread ProcessBlock can drain them each
   // block without touching Objective-C, taking a lock, or allocating.
   // Independent of AudioStart/AudioRead above, which is a separate node
   // (Audio Analyze) on its own separate engine instance.
   //
   // AddRef/RemoveRef just count how many AudioInputNode instances are alive
   // (call from the node's constructor/destructor - cheap, no Objective-C).
   // Pump (call every CookIfNeeded, main thread only) installs the tap once
   // the want-count is > 0 and the output device is open, and tears it down
   // once it drops to 0. Pumping every frame rather than once is what makes
   // this self-healing across the user stopping/restarting the output device
   // from the menu's audio toggle: AudioDeviceClose tears the tap down with
   // the rest of that engine, and the next Pump reinstalls it on the new one
   // without any node needing to notice the device came back.
   void AudioInputCaptureAddRef();
   void AudioInputCaptureRemoveRef();
   void AudioInputCapturePump(std::string& outError);
   bool AudioInputCaptureIsRunning();

   // Reads up to numFrames of the most recently captured audio into planar
   // per-channel buffers (outChannels[ch][0..numFrames)). Underrun frames are
   // zero-filled. Returns the channel count actually captured (0 if capture
   // isn't running yet, e.g. still waiting on the device to open or on mic
   // permission). Audio-thread safe: lock-free, no allocation.
   int AudioInputCaptureRead(float* const* outChannels, int numFrames, int maxChannels);

   // ---- MIDI input ----
   // Listens to every connected class-compliant MIDI source (Akai/Nektar/Pioneer
   // controllers all enumerate this way on macOS, no vendor driver needed) and
   // keeps a running table of the last value received for every (device,
   // channel, controller) triple seen, plus which one arrived most recently —
   // that's what MIDI Learn polls. Computed on Core MIDI's callback thread,
   // read the same lock-free-ish way as AudioLevels above.
   //
   // `device` identifies the physical source endpoint (its MIDIEndpointRef,
   // opaque otherwise) - two controllers both defaulting to channel 1 would
   // otherwise be indistinguishable, which is exactly the bug this fixes:
   // every binding is now scoped to the device it was learned on, not just
   // its channel/controller number. 0 = no device / wildcard-unbound.
   using MidiDeviceId = unsigned int;

   struct MidiCCValue
   {
      MidiDeviceId device = 0;
      int channel = -1;    // 0-15, -1 = none seen yet
      int controller = -1; // CC number 0-127, or note number for Note-On/Off
      bool isNote = false; // true if this came from a Note-On rather than a CC
      float value01 = 0.0f;
   };

   bool MidiStart(std::string& outError);
   void MidiStop();
   bool MidiIsRunning();
   std::string MidiDeviceSummary(); // comma-joined names of connected sources, for status text
   std::string MidiDeviceName(MidiDeviceId device); // "" if not currently connected

   // Current value for a specific (device, channel, controller) binding.
   // Returns false (value left at 0) if that binding has never been seen.
   bool MidiRead(MidiDeviceId device, int channel, int controller, bool isNote, float& outValue01);

   // The most recent CC/note touched on ANY connected source since the last
   // call — this is what a node in "Learn" mode polls each frame. Returns false
   // if nothing has moved since the last poll (consumed on read, like a queue
   // of depth 1, so two Learn-mode nodes don't both grab the same event and one
   // doesn't silently miss it while learning).
   bool MidiPollLastTouched(MidiCCValue& outLast);

   // Monotonically increasing count of Note-On events seen for a specific
   // (device, channel, note), for nodes that need to know *when* a new hit
   // happened rather than just the currently-held velocity (MidiRead only
   // reports the latter). Returns 0 if that note has never been hit.
   unsigned int MidiNoteHitCount(MidiDeviceId device, int channel, int note);

   // The most recent Note-On seen on a whole (device, channel), regardless of
   // which note - for "keyboard" nodes that report whichever key is currently
   // being played rather than one fixed pad note. hitSeq increments on every
   // Note-On so callers can tell a repeat of the same note from a held one.
   // Returns false (out left default) if that device/channel has never seen a note.
   struct MidiLastNote
   {
      int note = -1;
      float velocity01 = 0.0f;
      unsigned int hitSeq = 0;
   };

   bool MidiChannelLastNote(MidiDeviceId device, int channel, MidiLastNote& out);

   // ---- live note stream ---------------------------------------------------
   // Every Note-On and Note-Off, in order, for the MIDI Notes node to turn
   // into NoteEvents. This is a different shape of data from everything above
   // it: MidiRead/MidiChannelLastNote sample a *current value* under a mutex,
   // which is right for a modulator polled once a frame and wrong for a synth,
   // where a missed note-off is a stuck voice. So the note stream is its own
   // lock-free ring, written by the CoreMIDI read thread and readable from the
   // audio thread with no lock at all.
   //
   // Multiple consumers are supported by giving each one its own cursor rather
   // than a shared read index: the ring's write counter is monotonic, and a
   // consumer that has fallen more than the ring's capacity behind is skipped
   // forward to the oldest still-valid entry (dropping what it missed) instead
   // of reading torn data. Note-Off (0x80) and Note-On-with-velocity-0 both
   // arrive here as isNoteOn == false.
   struct MidiNoteMessage
   {
      MidiDeviceId device = 0;
      int channel = 0; // 0-15
      int note = 0;    // 0-127
      float velocity01 = 0.0f;
      bool isNoteOn = false;
   };

   // Pass the same `cursor` back on every call; initialise it to 0 and it will
   // be fast-forwarded to the live window on the first read. Returns how many
   // messages were written to `out`. Audio-thread safe: no locks, no
   // allocation.
   int MidiReadNotesSince(unsigned long long& cursor, MidiNoteMessage* out, int maxCount);

   // Current write position of the note ring - the value a consumer that only
   // wants *future* notes should seed its cursor with.
   unsigned long long MidiNoteStreamPosition();

   // ---- MIDI clock ---------------------------------------------------------
   // MIDI Clock (status byte 0xF8) is a realtime system message sent 24 times
   // per quarter note by a clock source (DJ mixer, drum machine, DAW) whenever
   // it's running - distinct from the CC/Note messages above. BPM isn't sent
   // directly; it's derived from the wall-clock spacing between pulses, smoothed
   // over a rolling window. 0xFA/0xFC (Start/Stop) reset that window so a
   // stopped-then-restarted clock doesn't average across the gap.
   bool MidiClockIsPresent(); // pulses seen within the last couple of seconds
   float MidiClockBpm();      // smoothed estimate; 0 if not enough pulses yet

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

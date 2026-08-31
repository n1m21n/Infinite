#pragma once

#include <string>
#include <vector>

// Opaque forward declaration - avoids pulling GLFW's headers into every TU
// that includes this facade just for the Windows-only output-window entry
// points below.
struct GLFWwindow;

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

   // ---- startup / crash diagnostics ---------------------------------------
   // Windows links the app with WIN32_EXECUTABLE (CMakeLists.txt) - a GUI
   // subsystem process with no console, so fprintf(stderr, ...) goes nowhere
   // and an unhandled exception unwinds silently: the window just vanishes.
   // macOS gets a `.ips` crash report and a visible terminal for free from
   // the OS, so both of these are real work on Windows and a no-op on macOS.

   // Installs a process-wide unhandled-exception handler that writes a
   // minidump plus a short text log to AppPaths::AppSupportDir() + "/crash/"
   // before the process dies, so a Windows crash leaves something to attach
   // to a report. Call once, early in main(), after argv-only setup. No-op
   // on macOS.
   void InstallCrashHandler();

   // Appends one line (a trailing newline is added) to a rolling
   // AppPaths::AppSupportDir() + "/log.txt", so a user who hits a startup
   // failure they can't see (see above) has something to attach to a report.
   // Best-effort: never throws, silently does nothing if the file can't be
   // opened. No-op on macOS - stderr already reaches a visible terminal or
   // Console.app there.
   void AppendLogLine(const std::string& line);

   // Reports a fatal startup failure through every channel a user might
   // actually see: stderr (unconditionally, matches prior behavior) and,
   // on Windows only, a MessageBoxW plus AppendLogLine. macOS relies on the
   // stderr print alone, since it reaches a visible terminal/Console.app.
   // Intended for failures before the main window exists (glfwInit,
   // glfwCreateWindow, GL context/loader setup) - call this instead of a
   // bare fprintf(stderr, ...) at any of those sites.
   void ShowFatalError(const std::string& title, const std::string& message);

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

   // True while the decoder has not yet read as far as the position last asked
   // for through VideoFrameAt - i.e. a false return from VideoFrameAt means
   // "not yet", not "never". Once it goes false the answer is settled and
   // waiting longer cannot change it, so it is safe to loop on.
   //
   // Always false on macOS, where VideoFrameAt decodes synchronously and has
   // therefore always caught up by the time it returns. On Windows decoding
   // runs on its own thread, so a caller stepping faster than real time (a
   // self-test or an offline analysis pass, never the render thread) has to
   // wait for the decoder instead of racing past it. The render thread must
   // NOT wait on this - holding the previous frame is the whole point.
   bool VideoDecodeIsCatchingUp(VideoHandle* handle);

   // Decodes a video container's audio track into the same planar-float
   // SampleBuffer (defined below) the audio-file path already produces.
   // Returns false with a reason in outError when the file has no audio
   // track - which is a normal case for a lot of VJ footage, not a failure -
   // vs. an actual decode failure; callers distinguish the two by checking
   // whether outError is exactly "no audio track in this file".
   //
   // Decodes the whole track up front, into memory (a three-minute stereo
   // 48kHz track is ~70MB - acceptable, and it's what SampleSlotT and the
   // sampler playback path already assume). Streaming decode from the audio
   // thread would be a much larger change and is deliberately not what this
   // does - don't "improve" it into one without deciding that separately.
   //
   // Main-thread only - not real-time safe. Cannot reuse DecodeAudioFileToBuffer:
   // that goes through AVAudioFile, an audio-file API that does not reliably
   // open movie containers, unlike AVAssetReader here (same class the video
   // decoder above already uses).
   struct SampleBuffer;
   bool DecodeVideoAudioTrackToBuffer(const std::string& path, SampleBuffer& outBuffer,
                                      std::string& outError);

   // ---- background removal ----
   // macOS uses Vision's on-device segmentation: no model download, no
   // network, no API key. Subject lifting (any salient foreground) needs
   // macOS 14; person segmentation works from macOS 12. Returns false with a
   // reason otherwise.
   //
   // Windows has no Vision equivalent, so it bundles a small on-device model
   // (u2netp, see assets/models/NOTICE.txt) run through ONNX Runtime with the
   // DirectML execution provider instead - still no network access or API
   // key at runtime, just a model file shipped with the build rather than
   // fetched from the OS. See src/platform/win/PlatformWin.cpp.
   enum class MattingMode
   {
      Subject, // any salient foreground object (macOS 14+)
      Person   // people only (macOS 12+)
   };

   bool SubjectMask(const std::vector<unsigned char>& rgbaPixels, int width, int height,
                    MattingMode mode, std::vector<unsigned char>& outMask,
                    std::string& outError);

   // Human-readable name of the compute path the background remover uses, for
   // the node UI - "Apple Vision (on-device)" on macOS, and on Windows either
   // "DirectML GPU (DX12)" or "CPU (DirectML unavailable)" depending on whether
   // the DirectML execution provider registered. Cheap to call every frame: it
   // does NOT construct the ONNX session, it only reports what the last (or
   // pending) SubjectMask call resolved to.
   std::string MattingBackend();

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
   // File playback moved into the DSP graph (AudioFileNode/AudioFilePlayerAudioNode
   // in src/nodes/AnalyzeNodes.cpp) - it decodes via DecodeAudioFileToBuffer below
   // and runs its own AudioNode like SamplerNode, rather than owning a private
   // AVAudioEngine here. OpenAudioDialog is still shared with the file picker.
   std::string OpenAudioDialog();

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

   // Headless round-trip check for the WASAPI PCM<->float conversion helpers
   // (docs/plans/windows-render/FIX_BRIEF.md addendum A1: WASAPI shared mode
   // can hand back a non-float mix format, and the render/capture paths must
   // convert rather than refuse). Prints "AUDIOPCMTEST OK"/"AUDIOPCMTEST
   // FAIL" and returns the same verdict as a bool - this is the one piece of
   // A1 that's actually CI-reachable (no device needed), so it's wired into
   // main.cpp's env-var test dispatch next to the other INFINITE_*TEST
   // fixtures. Trivially true on macOS, which never negotiates PCM here.
   bool AudioPcmConversionSelfTest();

   // ---- audio device recovery (config-change / sleep-wake) ----
   // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md.
   //
   // AVAudioEngineConfigurationChangeNotification can be posted on an
   // arbitrary thread, and NSWorkspace's sleep/wake notifications are not
   // guaranteed main-thread either by anything this file controls - nothing
   // downstream of them may touch the audio graph directly
   // (src/audio/AudioNode.h's RT-safety list covers the render thread, but
   // main-thread-only graph state like gNodes/AudioCable is just as
   // unsafe from a random notification thread). So these three functions
   // are the entire cross-thread surface: the notification handlers
   // installed by AudioDeviceOpen do nothing but latch an atomic flag, and
   // each accessor here consumes (clears) its flag on read. main.cpp's
   // per-frame loop polls all three once a frame and does the actual
   // Stop/Start work itself, through the same choke point Apply-audio-
   // settings already uses - see main.cpp's PollAudioRecovery.
   //
   // Consuming means a burst of N notifications between two polls collapses
   // to a single true - the poller does its own additional rate-limiting on
   // top of that (see PollAudioRecovery's comment), this alone does not
   // bound retry frequency.
   bool AudioDeviceConfigDidChange();
   bool AudioWillSleep();
   bool AudioDidWake();

   // Test-only: pokes the same flag AudioDeviceConfigDidChange() consumes,
   // without a real CoreAudio notification - INFINITE_AUDIORECOVERYTEST's
   // way of exercising PollAudioRecovery's restart/rate-limit logic
   // deterministically, since real device unplug/sleep-wake can't be
   // scripted headlessly.
   void AudioDeviceDebugSimulateConfigChange();

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
   // Returns "" if cancelled. `title` sets the panel's title (defaults to
   // the Samples search panel's "Add sample folder" wording); `initialDir`
   // seeds the panel's starting folder when non-empty.
   std::string OpenFolderDialog(const char* title = "Add sample folder",
                                const std::string& initialDir = std::string());

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

   // ---- audio plugin hosting -----------------------------------------------
   // Every Objective-C object involved in hosting a third-party plugin lives
   // behind this facade, for the same reason the rest of this header exists:
   // src/nodes/ and src/audio/ are pure C++ and the audio thread must never
   // send an Objective-C message or touch ARC. AudioPluginNode holds an opaque
   // PluginHandle* and calls PluginRender from ProcessBlock; that one function
   // is the only real-time-safe entry point here, and it does nothing but call
   // a render block cached on the main thread at prepare time.
   // Audio Unit and (optionally, build-time) VST3 plugin hosting backend.
   // VST3 support is gated behind INFINITE_ENABLE_VST3, off by default: the
   // Steinberg VST3 SDK is GPLv3-or-proprietary and this codebase is MIT, so
   // building with VST3 enabled changes the license of the distributed binary
   // - see LICENSE and docs/plans/audio/plugin-hosting.md. Every struct that
   // crosses this boundary carries a `format` string ("au" or "vst3") so both
   // backends share one surface.
   struct PluginDesc
   {
      std::string format = "au";
      std::string name;
      std::string manufacturer;
      // Stable identity, and what a patch file stores - NOT a filesystem path,
      // which moves when a plugin is reinstalled elsewhere. For AU this is
      // "au:<type>:<subtype>:<manufacturer>" built from the component's four-
      // char codes, e.g. "au:aufx:dely:appl". For VST3 this is the factory
      // class's FUID string, e.g. "vst3:<uid-hex>".
      std::string identifier;
      std::string path; // Bundle directory path (e.g. for VST3 bundles)

      // True for 'aumu' (instrument) and 'aumf' (music effect) components -
      // the two component types that can meaningfully consume MIDI. This is
      // what AudioPluginNode keys its note input pin's visibility off, and
      // what PluginConfigure below uses to decide whether to expect an input
      // bus at all.
      bool acceptsNotes = false;
   };

   // Every installed effect/music-effect/instrument Audio Unit, via
   // AVAudioUnitComponentManager (a registry query).
   void EnumerateAudioUnits(std::vector<PluginDesc>& out);

   // Resolves a dropped .component *bundle directory* back to the plugin(s) it
   // contains, by reading its Info.plist AudioComponents array. Returns false
   // if the path isn't a readable audio component bundle. One bundle can
   // declare several components, hence the vector.
   bool DescribeAudioUnitBundle(const std::string& bundlePath, std::vector<PluginDesc>& out);

   // Enumerates VST3 plugins (.vst3 bundles) found within the specified folders.
   // Gated by INFINITE_ENABLE_VST3 at build time (no-op when disabled). Each
   // bundle is probed out-of-process (see main.cpp's "--vst3-scan-bundle"
   // child mode) so a plugin that crashes or hangs while its factory is being
   // read only costs a dead/killed child, and the walk moves on to the next
   // bundle instead of taking the whole app down mid-scan.
   void EnumerateVST3Plugins(const std::vector<std::string>& folders, std::vector<PluginDesc>& out);

   // Absolute path to this process's own executable (argv[0] resolved via
   // _NSGetExecutablePath), used to re-exec ourselves for the VST3 scan's
   // "--vst3-scan-bundle" child mode.
   std::string ExecutablePath();
   std::string ScannerExecutablePath();

   // Claims the shared NSApplication singleton with the Prohibited activation
   // policy before anything else can - the Dock icon comes from activation
   // policy, not window visibility, so a hidden GLFW window (GLFW_VISIBLE =
   // false) still shows up in the Dock unless this runs first. Two callers:
   //
   //  - First thing in the "--vst3-scan-bundle" child process, before any
   //    plugin code runs. The child is a re-exec of this same app binary, so
   //    it shares our bundle identity - if a scanned plugin's Cocoa UI classes
   //    touch [NSApplication sharedApplication] during their static
   //    init/factory instantiation (common for VST3 bundles with a Cocoa
   //    editor), AppKit lazily creates a default-policy shared application for
   //    *this* process and it shows up as its own Dock icon/window, i.e. a
   //    spurious extra "Infinite" instance.
   //  - Before glfwInit() on a headless test/screenshot run (INFINITE_EXITAFTER
   //    / IMAGERESYNTH_SCREENSHOT), so the hygiene suite's fixtures don't flash
   //    an icon into the Dock even though their window is never shown.
   //
   // Creating the shared application ourselves first and setting it to
   // Prohibited claims that singleton before anything else can, so nothing
   // afterward - plugin or GLFW - can make the process visible.
   void SuppressAppUIForHeadlessProcess();

   // Resolves a dropped .vst3 bundle back to the plugin description(s) it contains.
   bool DescribeVST3Bundle(const std::string& bundlePath, std::vector<PluginDesc>& out);

   // Caches bundle path for a plugin identifier.
   void CacheVST3BundlePath(const std::string& identifier, const std::string& bundlePath);

   // User-added VST3 folders (PluginScanner::Folders()), kept in sync so that
   // PluginVST3Create's on-demand bundle resolution can search them too, not
   // just the two OS-standard VST3 directories. No-op when VST3 is disabled.
   void SetVST3SearchFolders(const std::vector<std::string>& folders);

   // Crash-safety for the VST3 scan. Each bundle is probed in a disposable
   // child process (see EnumerateVST3Plugins); if that child is killed by a
   // signal, exits nonzero, or simply hangs, its bundle path is appended to a
   // persisted blocklist immediately - same scan, no relaunch needed - and
   // skipped by every future scan. A sentinel file backs this up for the rare
   // case where the child dies before even that: it is written immediately
   // before the in-process probe inside the child (CFBundleLoadExecutable /
   // bundleEntry / bundleExit) and cleared right after a clean return, so a
   // sentinel still non-empty at the next launch blocklists that bundle too.
   // Surface both lists in the Plugins panel, with a way to clear the
   // blocklist and retry.
   std::vector<std::string> VST3Blocklist();
   void ClearVST3Blocklist();

   // Bundles the most recent EnumerateVST3Plugins() call could not describe
   // (corrupt bundle, wrong CPU architecture, no usable audio-effect class) by
   // path. Reset at the start of every call; read immediately after it
   // returns. Distinct from the blocklist: a scan failure here is a clean,
   // reported miss, not evidence the bundle is dangerous to retry.
   std::vector<std::string> VST3ScanFailures();

   struct PluginHandle;

   enum class PluginLoadState
   {
      Pending, // instantiation still in flight - keep polling, keep the UI live
      Ready,   // render block cached, safe to publish to the audio thread
      Failed   // see the outError from PluginPoll
   };

   // Starts instantiation and returns immediately. AUAudioUnit instantiation
   // is asynchronous (out-of-process AUv3s in particular can take seconds), so
   // this never blocks: the handle exists straight away in the Pending state
   // and the node polls it from CookIfNeeded. `sampleRate` may be 0 if the
   // device isn't open yet; PluginPrepare below re-does the format/render-
   // resource setup once a real rate is known.
   PluginHandle* PluginCreate(const PluginDesc& desc, double sampleRate, int maxBlockFrames);

   // Main thread only, call once per frame while Pending. The transition to
   // Ready happens inside this call: the completion handler runs on an
   // arbitrary queue and does nothing but stash the instantiated unit, and all
   // of the actual setup (bus formats, maximumFramesToRender,
   // allocateRenderResources, caching the render block) is done here, on the
   // main thread, where AUAudioUnit expects it.
   PluginLoadState PluginPoll(PluginHandle* handle, std::string& outError);

   // Re-negotiates bus formats and re-allocates render resources at a new
   // sample rate / block size. Main thread only, and NOT safe while the handle
   // is reachable from a published topology - AudioPluginNode calls it before
   // publishing. A no-op if nothing changed. Returns false and fills outError
   // if the plugin rejects the format.
   bool PluginPrepare(PluginHandle* handle, double sampleRate, int maxBlockFrames, std::string& outError);

   // Main thread only, and never while the handle is still reachable from a
   // published audio topology - see AudioPluginNode's retire discipline. Closes
   // the editor window first, removes the learn observer, and briefly spins
   // (main thread, bounded) if a render is in flight before tearing the unit
   // down.
   void PluginDestroy(PluginHandle* handle);

   PluginDesc PluginDescriptionOf(PluginHandle* handle);

   // Samples of processing latency the loaded plugin itself reports
   // (lookahead, oversampling, internal FFT windows, ...) at its currently
   // prepared sample rate. 0 for a plugin that reports none, hasn't finished
   // loading, or is null - never a special sentinel. Main thread only; the
   // value is refreshed each time render resources are (re)allocated (see
   // PluginConfigure/PluginVST3Configure) and read here, not recomputed, so
   // this is cheap enough to call every RebuildAudioTopology.
   int PluginLatencySamples(PluginHandle* handle);

   // THE real-time-safe entry point. Calls the cached AUAudioUnitRenderBlock
   // with a stack-allocated pull-input block that copies from `in`. No
   // Objective-C message send, no ARC retain/release, no allocation, no lock.
   // `in` may be null (treated as silence). Output is planar, one pointer per
   // channel, numFrames long.
   void PluginRender(PluginHandle* handle, const float* const* in, int inChannels,
                     float* const* out, int outChannels, int numFrames);

   // ALSO real-time-safe, same discipline as PluginRender: schedules a MIDI
   // event through a block cached on the main thread at prepare time (into
   // AUAudioUnit.scheduleMIDIEventBlock), no Objective-C message send, no ARC,
   // no allocation. `bytes` is a standard 1-3 byte MIDI message (note on/off,
   // channel 0). A no-op if the plugin never published a schedule block (an
   // effect with no MIDI input, or one that doesn't support it) - the node
   // degrades to rendering with no note input rather than crashing.
   void PluginScheduleMIDIEvent(PluginHandle* handle, int frameOffset, const unsigned char* bytes,
                                int byteCount);

   struct PluginParamInfo
   {
      unsigned long long address = 0;
      std::string displayName;
      float minValue = 0.0f;
      float maxValue = 1.0f;
      float defaultValue = 0.0f;
      std::string unit;
   };

   // The plugin's AUParameterTree, flattened (allParameters). Main thread only.
   int PluginParameterCount(PluginHandle* handle);
   bool PluginParameterInfo(PluginHandle* handle, int index, PluginParamInfo& out);
   bool PluginParameterInfoByAddress(PluginHandle* handle, unsigned long long address, PluginParamInfo& out);

   // Main thread only - AUParameter's setter is non-blocking and is the
   // supported way to drive a parameter from a host's own UI, so mapped params
   // go straight down this path from CookIfNeeded rather than through the
   // node's ParamMailbox (the plugin does its own smoothing). This is the one
   // deliberate exception to "every param reaches the audio thread through the
   // mailbox", and is why AudioPluginNode carries a documented
   // AUDIOPARAMSWEEPTEST baseline.
   void PluginSetParameter(PluginHandle* handle, unsigned long long address, float value);
   bool PluginGetParameter(PluginHandle* handle, unsigned long long address, float& outValue);

   // "Configure" / learn mode, the Ableton-style mapping gesture: while learn
   // is on, touching a control in the plugin's OWN editor window reports that
   // parameter's address here, and the node fills its next free mapping slot.
   //
   // Implemented with AUParameterTree's parameter observer, whose block is
   // called on an arbitrary thread - so it does nothing but store the address
   // into an atomic that PluginPollLearned drains from the main thread.
   // Parameter writes this host makes itself are tagged with the observer's own
   // token and therefore never echo back as a learned touch.
   void PluginBeginLearn(PluginHandle* handle);
   void PluginEndLearn(PluginHandle* handle);
   bool PluginPollLearned(PluginHandle* handle, unsigned long long& outAddress);

   // The plugin's own editor, in a separate native window (the app itself is
   // GLFW/ImGui/OpenGL and has no other NSWindow). Verified: a plain NSWindow
   // is created, displayed and dispatched to correctly with glfwPollEvents as
   // the only run-loop pump. The view comes from
   // -requestViewControllerWithCompletionHandler:, which is asynchronous, so
   // PluginOpenEditor returns true meaning "the request went out" and the
   // window appears a beat later.
   //
   // Closing the window (red button or PluginDestroy) routes back through
   // PluginCloseEditor so PluginEditorIsOpen never lies to the node.
   bool PluginOpenEditor(PluginHandle* handle, std::string& outError);
   void PluginCloseEditor(PluginHandle* handle);
   bool PluginEditorIsOpen(PluginHandle* handle);

   // True if any hosted plugin currently has an editor window on screen.
   bool AnyPluginEditorOpen();

   // Drains and dispatches pending main-run-loop work (AppKit events, plugin
   // editor redraws, XPC replies from an out-of-process AUv3) without blocking.
   // Main thread only. No-op if no editor window is open. Returns true if it
   // actually dispatched something.
   bool PumpPluginEditorEvents();

   // AUAudioUnit.fullState, keyed-archived and base64'd so it round-trips
   // through Patch.cpp's Text param (one backslash-escaped line, no length
   // cap). Returns false if the plugin publishes no state.
   bool PluginSaveState(PluginHandle* handle, std::string& outBase64);
   bool PluginRestoreState(PluginHandle* handle, const std::string& base64);

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
   // When liveAudioSampleRate > 0, an audio track is configured to receive
   // streaming audio via RecorderAppendAudio.
   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError,
                                 const std::string& audioPath = std::string(),
                                 bool loopAudio = true,
                                 double liveAudioSampleRate = 0.0,
                                 int liveAudioChannels = 2);
   // `pixels` is BGRA8 bottom-up by default - the GPU's native glReadPixels
   // format (GL_BGRA/GL_UNSIGNED_INT_8_8_8_8_REV) on essentially all desktop
   // GPUs, which turns the readback into a straight blit instead of a
   // driver-side conversion. See RecorderSetInputIsBgra below for the RGBA
   // fallback path.
   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels);

   // Tells the recorder whether pixels handed to RecorderAppend for the rest
   // of this take are BGRA8 (the default assumed at RecorderStart) or RGBA8 -
   // the fallback a caller uses when its GPU driver rejects a native BGRA
   // glReadPixels. Call before the first RecorderAppend of the take; the
   // format is fixed for the take's whole duration once appends begin.
   void RecorderSetInputIsBgra(RecorderHandle* handle, bool isBgra);

   // Hands the caller a buffer from the recorder's own pool, already sized
   // width*height*4. Recycling these is what lets the render thread hand off
   // ownership instead of copying into the encoder's memory on the spot.
   std::vector<unsigned char> RecorderAcquireFrameBuffer(RecorderHandle* handle);

   // Takes ownership. `repeatCount` writes the same frame that many times, for
   // the constant-frame-rate padding case; 1 is the normal path.
   bool RecorderAppend(RecorderHandle* handle, std::vector<unsigned char>&& pixels,
                       int repeatCount = 1);

   // Frames handed to the encoder that it has not written yet.
   int RecorderPendingFrameCount(RecorderHandle* handle);
   // Frames the encoder could not accept and discarded, for the whole take.
   int RecorderDroppedFrameCount(RecorderHandle* handle);

   // Test-only: overrides the queue's byte-budget admission ceiling (normally
   // a fixed 256MB) for this handle. Real hardware encoders drain fast enough
   // that reproducing a genuine over-budget rejection in a self-test needs an
   // artificially small ceiling rather than an artificially slow consumer -
   // see RunRecExportTest's `starved` variant in main.cpp.
   void RecorderSetTestQueueByteBudget(RecorderHandle* handle, size_t bytes);

   // Appends interleaved float audio frames to the movie's audio track.
   bool RecorderAppendAudio(RecorderHandle* handle, const float* interleavedSamples, int numFrames);

   // Drains the encoder queue and joins its worker before returning, so the
   // movie is complete rather than truncated. outFrameCount/outDroppedCount,
   // if given, are filled with the final totals after that drain - reading
   // RecorderFrameCount() beforehand can race the worker's last few writes.
   bool RecorderStop(RecorderHandle* handle, std::string& outError,
                     int* outFrameCount = nullptr, int* outDroppedCount = nullptr);

   // Aborts a take instead of finishing it: discards every frame still queued
   // for the encoder, skips the flush/finalize entirely, and deletes the
   // partial output file. Returns as fast as the encoder can be unblocked -
   // at worst one in-flight frame - where RecorderStop's cost scales with
   // however deep the queue happens to be. That difference is the whole
   // point: an offline render can queue frames far faster than the encoder
   // drains them, so routing Cancel through RecorderStop made cancelling a
   // long take take about as long as finishing it. The handle is destroyed
   // either way; nothing usable is left behind on disk.
   void RecorderCancel(RecorderHandle* handle);

   // Whether an append of `bytes` would be admitted right now, or dropped by
   // the encoder queue's byte-budget ceiling. The live path has no use for
   // this - it cannot stall a real-time take, so it accepts the drop - but an
   // offline render must never drop a frame it has already advanced the
   // transport and the audio clock for, so it asks first and waits instead.
   bool RecorderQueueHasRoom(RecorderHandle* handle, size_t bytes);

   // Pushes whatever live audio is parked in the recorder's own backlog into
   // the writer, if the writer will take it now. RecorderAppendAudio already
   // tries this on every append, so a take that keeps appending never needs
   // to call it - but a caller that has STOPPED appending (an offline render
   // waiting for the video queue to drain) does: on macOS the writer holds
   // the video input not-ready until the audio track catches up, and the only
   // thing that ever flushed parked audio was the next append. Waiting for
   // the encoder while the encoder waits for audio nobody will hand it is a
   // deadlock, and this is what breaks it. No-op where the platform's writer
   // takes audio synchronously (Windows).
   void RecorderFlushPendingAudio(RecorderHandle* handle);

   // One-line snapshot of the recorder's internal state - writer status, both
   // inputs' readiness, queue depth, audio backlog. Diagnostic only: the
   // difference between "the encoder is slow" and "the encoder is wedged" is
   // invisible from outside, and a stalled offline render is exactly when it
   // matters.
   // Asks the encoder to drain whatever is queued, right now, without waiting
   // for the writer to hand out a readiness callback.
   //
   // The macOS encoder runs off AVAssetWriterInput's pull API
   // (requestMediaDataWhenReadyOnQueue:), whose contract is that the block is
   // re-invoked when an input that went not-ready becomes ready again. In an
   // offline render that re-invocation has been observed never to arrive: the
   // video input reports not-ready with the writer still in
   // AVAssetWriterStatusWriting, its audio track fully written and ahead of
   // the picture, one frame queued, and every encoder thread idle. Nothing
   // else in the process is waiting on anything - the take simply stops.
   //
   // A live take rides through it (its next append re-arms the queue, and it
   // drops frames rather than waiting); an offline take cannot, because it
   // stops appending precisely while it waits for room. So the waiting side
   // kicks the encoder itself rather than trusting the callback to come.
   // Cheap and idempotent: a kick with nothing to drain returns immediately.
   void RecorderKickEncoder(RecorderHandle* handle);

   // Marks the take's audio track complete without finishing the video track
   // or the file. Only meaningful for an offline render, whose audio is
   // generated on a fixed budget and therefore finishes before the picture
   // does: the writer will not take more video while it is still expecting
   // audio for that stretch of the timeline, so the last frames of a take
   // whose audio has run out cannot be written until the audio input is
   // told there is no more coming. Idempotent, and appending audio after it
   // is a no-op rather than an error.
   void RecorderFinishAudioInput(RecorderHandle* handle);

   std::string RecorderDebugState(RecorderHandle* handle);
   int RecorderFrameCount(RecorderHandle* handle);

   // Inspects a finished recording. Used by the audio-mux self-test, and handy
   // for the UI later if recording ever needs to report back what it actually
   // wrote rather than what was asked for.
   struct MovieInfo
   {
      bool hasVideo = false;
      bool hasAudio = false;
      double duration = 0.0;
      int frameCount = 0;
      // Per-track durations, not just the asset's overall one (which is the
      // longer of the two). An export that wrote all its video but lost part
      // of its audio - or the reverse - has a perfectly plausible overall
      // duration and two tracks that disagree; this is the only way to see
      // that from outside the process.
      double videoDuration = 0.0;
      double audioDuration = 0.0;
   };
   MovieInfo InspectMovie(const std::string& path);

   // ---- Syphon inter-app video sharing ------------------------------------
   struct SyphonServerHandle;
   struct SyphonClientHandle;

   struct SyphonServerInfo
   {
      std::string appName;
      std::string serverName;
      std::string uuid;
   };

   // Syphon Server (Broadcast)
   SyphonServerHandle* SyphonServerCreate(const std::string& serverName);
   void SyphonServerUpdateName(SyphonServerHandle* handle, const std::string& serverName);
   void SyphonServerPublish(SyphonServerHandle* handle, unsigned int textureId, int width, int height, bool flipped = false);
   bool SyphonServerHasClients(SyphonServerHandle* handle);
   void SyphonServerDestroy(SyphonServerHandle* handle);

   // Syphon Directory & Client (Receiver)
   std::vector<SyphonServerInfo> SyphonGetAvailableServers();
   SyphonClientHandle* SyphonClientCreate();
   bool SyphonClientConnect(SyphonClientHandle* handle, const std::string& appName, const std::string& serverName, const std::string& uuid = "");
   bool SyphonClientIsConnected(SyphonClientHandle* handle);
   bool SyphonClientHasNewFrame(SyphonClientHandle* handle);
   // Returns the GL_TEXTURE_RECTANGLE texture name and dimensions from the latest Syphon frame, or 0 if none.
   unsigned int SyphonClientGetFrameTexture(SyphonClientHandle* handle, int& outWidth, int& outHeight);
   void SyphonClientDestroy(SyphonClientHandle* handle);

   // macOS Native Document Opening (Finder / AppleEvents)
   // Must be called before glfwInit() so the delegate methods exist on
   // GLFWApplicationDelegate by the time GLFW installs it as NSApp's delegate.
   void InitDocumentHandlingPreGlfw();
   // Must be called after glfwInit() to reinstall the Apple Event handler that
   // AppKit's -[NSApplication finishLaunching] (run inside glfwInit) clobbers.
   void InitDocumentHandlingPostGlfw();
   bool PollPendingOpenFile(std::string& outPath);

   // ---- live camera input (for Video In node) -----------------------------
   struct CameraDeviceInfo
   {
      std::string uniqueId;
      std::string localizedName;
      bool isDefault = false;
   };

   enum class CameraResolution
   {
      Auto = 0,
      Res1080p,
      Res720p,
      Res480p,
      Count
   };

   struct CameraHandle;

   std::vector<CameraDeviceInfo> CameraListDevices();
   CameraHandle* CameraOpen(const std::string& deviceId, CameraResolution res, bool mirrorX, std::string& outError);
   void CameraClose(CameraHandle* handle);
   bool CameraIsRunning(CameraHandle* handle);
   void CameraSetMirror(CameraHandle* handle, bool mirrorX);
   void CameraSetResolution(CameraHandle* handle, CameraResolution res);

   // Drains the latest frame into outPixels (RGBA8, GL bottom-up).
   // Returns true only when a new frame was received and written.
   bool CameraReadFrame(CameraHandle* handle, std::vector<unsigned char>& outPixels,
                        int& outWidth, int& outHeight, unsigned long long& outFrameSeq);

   // ---- networking (update checker) ----------------------------------------

   // Hands a URL to the OS's default browser. Fire-and-forget: there is no
   // success callback, and a malformed or non-http(s) URL is dropped rather
   // than passed through, so this can never be used to launch a local
   // executable by way of a file:// or shell URL.
   void OpenExternalUrl(const std::string& url);

   // Blocking HTTPS GET. Call from a worker thread, never the render or audio
   // thread. Returns false on any transport, TLS, or non-2xx failure and fills
   // outError; outBody is only valid when it returns true. Bounded by
   // timeoutSeconds and a hard response-size cap so a hung or hostile endpoint
   // can't stall or balloon the caller.
   bool HttpGet(const std::string& url, const std::string& userAgent,
                std::string& outBody, std::string& outError,
                int timeoutSeconds = 10);

   // ---- output/projector window (Windows only) ----------------------------
   // Windows has no OS-level "make this app window fullscreen" affordance -
   // maximise keeps the title bar and taskbar, and an ordinary window stays
   // activatable/non-topmost in the normal Z-order, so it drops behind the
   // editor the moment focus moves back to it. macOS instead gets a real
   // fullscreen Space via NSWindow (handled entirely with GLFW_DECORATED/
   // GLFW_FLOATING window attribs in main.cpp, no Platform.mm entry point
   // needed), so these are declared for _WIN32 only - Platform.mm provides
   // no definition and none should be added.
   //
   // Windows projector policy: a normal decorated window while the user is
   // positioning it, then borderless + HWND_TOPMOST on fullscreen instead of
   // exclusive fullscreen. SWP_NOACTIVATE means the editor keeps keyboard
   // focus while the output stays visible on the other display.
#if defined(_WIN32)
   // Applies the borderless/topmost/cursor policy above. When w/h are both
   // > 0 the window is also moved and resized to exactly that virtual-screen
   // rect in the SAME SetWindowPos that applies the style change; otherwise
   // position and size are left alone.
   //
   // That combined call is not a convenience - it is the fix for a real
   // Windows-only defect. glfwSetWindowSize sets the CLIENT area, so sizing a
   // still-decorated window to the monitor's full video mode and only then
   // stripping the frame leaves a borderless window bigger than the monitor,
   // overhanging bottom-right. macOS has no equivalent problem: its
   // GLFW_DECORATED path keeps the content rect fixed when the frame goes
   // away, so main.cpp's non-Windows branch deliberately still sizes first.
   void ConfigureOutputWindow(GLFWwindow* window, bool borderless, bool topmost, bool hideCursor,
                              int x = 0, int y = 0, int w = 0, int h = 0);
   void ReassertOutputWindowTopmost(GLFWwindow* window);
   void SetWindowIconFromResource(GLFWwindow* window);
#endif
}



#pragma once
// Plugin engine mode (docs/plans/vst-au/README.md 8.1, step 1.4).
//
// The Infinite binary started as `Infinite --plugin-engine <shm> <ppid> [--fx] [patch.inf]` is
// the engine for one plugin instance. Instead of opening an audio device, the AudioEngine is
// driven by blocks the shell posts over shared memory (PluginIpc::EngineSide):
//
//   shell block -> FollowHost(transport) -> split at MIDI offsets (32-sample grid)
//               -> inject MIDI -> render each segment (<= kAudioMaxBlockFrames) -> Complete
//
// Audio In reads the host's main / sidechain buses through ReadHostInput while a segment renders.
#include <cstdint>
#include <string>

namespace PluginEngine
{
   struct Options
   {
      std::string shmName;
      int64_t parentPid = 0;
      bool fx = false;          // FX build: bypass passes the main input through
      std::string patchPath;    // optional patch to load at start
   };

   // Parses `--plugin-engine <shm> <ppid> [--fx] [patch]` out of argv. False when absent.
   bool ParseArgs(int argc, char** argv, Options* out);

   // Opens the region, arms the parent watch and registers the host audio device with the
   // AudioEngine (Platform::AudioDeviceOverrideRef). Sets Platform::PluginModeFlag. False and an
   // error when the region cannot be opened.
   bool Init(const Options& opt, std::string& outError);

   bool Active();
   bool IsFx();

   // True once the shell asked the engine to quit (or the region went away). Main loop polls it.
   bool QuitRequested();

   // Audio thread, inside a render: copies the host bus into out (frames samples). bus 0 = main,
   // 1 = sidechain. channelOffset/mono follow Audio In's channelMode. Returns frames written
   // (0 = nothing: not inside a host render, or the bus is absent).
   int ReadHostInput(int bus, float* const* out, int frames, int channelOffset, bool mono);

   // Engine -> shell message types (PluginIpc::Channel `type`).
   enum MessageType : uint32_t
   {
      kMsgReady = 1, // patch loaded and audio running; payload: empty, or the start error text
   };

   // Main thread, once the patch is loaded and audio started: remembers the start error (empty =
   // fine). The shell is told on the third main-loop frame (MainFrameTick), when the first frames
   // have settled the topology, so an offline render that starts right away hears the full patch.
   void SetStarted(const std::string& error);
   // Main thread, once per main-loop frame.
   void MainFrameTick();

   // Blocks handled and transport jumps seen (tests). With INFINITE_PLUGIN_STATS=<path> set, the
   // first QuitRequested() that returns true writes "blocks N jumps M" there.
   uint64_t BlocksHandled();
   uint64_t JumpsSeen();
}

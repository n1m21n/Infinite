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
#include <cstddef>
#include <cstdint>
#include <string>

#include "plugin/ipc/PluginIpc.h"

namespace PluginEngine
{
   struct Options
   {
      std::string shmName;
      int64_t parentPid = 0;
      bool fx = false;          // FX build: bypass passes the main input through
      std::string patchPath;    // optional patch to load at start
      bool initialState = false; // --state: the shell sends the patch as the first message
   };

   // Parses `--plugin-engine <shm> <ppid> [--fx] [--state] [patch]` out of argv. False when absent.
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

   // Main thread: records the host main bus for a recorder outside the graph (Sample Studio).
   // AddRef starts the tap; ReadHostTap drains what arrived since `cursor` (start it at
   // TapPosition()). Returns frames read.
   void HostTapAddRef();
   void HostTapRemoveRef();
   uint64_t TapPosition();
   int ReadHostTap(float* l, float* r, int maxFrames, uint64_t& cursor);

   // PluginIpc::Channel message types. Engine -> shell below 100, shell -> engine from 100.
   enum MessageType : uint32_t
   {
      kMsgReady = 1,       // patch loaded and audio running; payload: empty, or the start error text
      kMsgState = 2,       // reply to kMsgGetState (same id); payload: the patch as .inf text
      kMsgStateLoaded = 3, // reply to kMsgSetState (same id); payload: empty, or the load error
      kMsgSlotEdit = 4,    // a bound Macro turned in Infinite; payload: SlotEdit
      kMsgSlotNames = 5,   // payload: 32 slot names, each '\0'-terminated UTF-8 ("" = unbound)
      // 6..10 are PluginFrames' (view) messages; one id space per direction.
      kMsgStateAuto = 11,  // unasked, every 2 s while the patch text changed; payload: the patch as .inf text
      kMsgLicense = 12,    // the weekly refund list names this key (License.h); payload: uint32 License::Status. The shell stops the engine
      kMsgGetState = 101,  // payload: none
      kMsgSetState = 102,  // payload: the patch as .inf text; kMsgReady follows once it settles
   };

   // Main thread: a message to the shell (view frames, cursor). False when not running or the ring
   // stayed full for timeoutMs.
   bool SendToShell(uint32_t type, const void* data, size_t size, int timeoutMs);

   // The app's patch <-> text (BuildPatchData + Patch::WriteText, Patch::ReadText + load), set by
   // Startup so this file stays free of app globals. Main thread only.
   using SaveStateFn = std::string (*)();
   using LoadStateFn = bool (*)(const std::string& text, std::string& error);
   void SetStateHandlers(SaveStateFn save, LoadStateFn load);

   // Startup, with Options::initialState: waits up to timeoutMs for the shell's first kMsgSetState
   // and returns its patch text (the shell's stored state, so the engine starts on it).
   bool TakeInitialState(std::string& text, int timeoutMs);

   // Main thread, once the patch is loaded and audio started: remembers the start error (empty =
   // fine). The shell is told on the third main-loop frame (MainFrameTick), when the first frames
   // have settled the topology, so an offline render that starts right away hears the full patch.
   void SetStarted(const std::string& error);
   // Main thread, once per main-loop frame: answers state requests, sends kMsgReady.
   void MainFrameTick();

   // ---- host slots (README 8.2, step 2.2) ----------------------------------------------------
   // The 128 host parameters drive Macro nodes whose "Host slot" field names them, and any control
   // given "Host automation" from its right-click menu. The audio thread
   // smooths each slot (one-pole 10 ms, stepped for switch-like macros) per 32-sample sub-block and
   // publishes a snapshot on a frame grid (round(sr/60/32)*32 samples). Offline, while a bound slot
   // moves, it parks at each grid point until the main thread has applied that snapshot and cooked
   // (a "served" frame, whose clock is pinned to the grid point), so a bounce is repeatable.
   constexpr int kSlotCount = PluginIpc::kHostSlots;
   using SlotMask = PluginIpc::SlotMask;
   // Sync: a newly bound Macro's value for a slot the host has no value for yet (no automation write).
   enum SlotEditKind : uint32_t { kSlotEditBegin = 0, kSlotEditPerform = 1, kSlotEditEnd = 2, kSlotEditSync = 3 };
   struct SlotEdit
   {
      uint32_t kind;
      uint32_t slot; // 0..kSlotCount-1
      float value;   // normalised 0..1
   };
   struct SlotFrame
   {
      bool served = false;    // the audio thread is parked on this snapshot (offline lock)
      SlotMask validMask;      // slots the host has set at least once
      float values[kSlotCount] = {};
   };
   // Main thread, at the start of the modulation pass: the latest snapshot. Pins the frame clock
   // when it serves a parked grid point; MainFrameTick of the next frame releases it.
   void BeginSlotFrame(SlotFrame* out);
   // Main thread: which slots have a Macro bound, and which of those are stepped (no smoothing).
   void SetSlotBindings(const SlotMask& boundMask, const SlotMask& steppedMask);
   // Main thread: Infinite -> host. The shell turns these into begin/perform/end edits.
   void SendSlotEdit(SlotEditKind kind, int slot, float value);
   void SendSlotNames(const std::string (&names)[kSlotCount]);
   // Main thread: false on a frame that must not publish a new ramp stamp (offline lock running,
   // this frame not served), so the served frame's stamp is the one the audio thread uses.
   bool SlotFramePublishes();
   // Grid points served / park timeouts (tests, stats file).
   uint64_t SlotParks();
   // Main thread: the name last sent for a slot ("" = no Macro on it), for the top bar's Slots list.
   std::string SlotName(int slot);
   // Blocks the shell had to answer with silence because the engine was late.
   uint64_t HostXruns();
   uint64_t SlotParkTimeouts();

   // Blocks handled and transport jumps seen (tests). With INFINITE_PLUGIN_STATS=<path> set, the
   // first QuitRequested() that returns true writes "blocks N jumps M" there.
   uint64_t BlocksHandled();
   uint64_t JumpsSeen();
}

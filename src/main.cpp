#define GLFW_INCLUDE_NONE
#include "platform/OpenGLHeaders.h"
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_node_editor.h"
#include "imgui_stdlib.h"
// For direct ImGuiInputTextState access: ImGui's own AutoSelectAll flag (and
// the select-all it triggers implicitly whenever focus arrives through
// SetKeyboardFocusHere's nav-activation path) can't be trusted to land
// before a same-frame keystroke is processed, so typed-param entry sets the
// cursor/selection state explicitly once the field is confirmed active.
#include "imgui_internal.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#include <functional>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <vector>

#include "core/NodeFactory.h"
#include "core/CategoryColors.h"
#include "core/GLUtil.h"
#include "core/GraphNode.h"
#include "core/FilterDefs.h"
#include "core/BlendModes.h"
#include "core/Transport.h"
#include "core/AudioTopologyRequest.h"
#include "core/Modulation.h"
#include "core/Expression.h"
#include "core/ExprGlobals.h"
#include "core/Palette.h"
#include "core/Patch.h"
#include "core/NodeViewport.h"
#include "core/AudioCable.h"
#include "core/NoteCable.h"
#include "core/RemoteControl.h"
#include "core/Base64.h"
#include "platform/McpBridge.h"
#include "platform/AISkillContent.h"
#include "app/ui/design/TokenColors.h" // Turbo 0.57: upstream design tokens (Infinite Dark theme)
#include "platform/ClaudeChat.h"
#include "core/RuntimeLog.h"
#include "core/PatchJson.h"
#include "platform/SettingsPaths.h"
#include "nodes/ImageSourceNode.h"
#include "nodes/ShapeNode.h"
#include "nodes/FormulaNode.h"
#include "nodes/FilterNode.h"
#include "nodes/BlendNode.h"
#include "nodes/LayerStackNode.h"
#include "nodes/TextNode.h"
#include "nodes/FitNode.h"
#include "nodes/VideoSourceNode.h"
#include "nodes/VideoInNode.h"
#include "nodes/NoiseNode.h"
#include "nodes/TextureNode.h"
#include "nodes/ResynthNode.h"
#include "nodes/MacroNodes.h"
#include "nodes/ScenesNode.h" // Turbo 0.50
#include "nodes/DriftNode.h"
#include "nodes/SlideshowNode.h"
#include "nodes/AudioMeterNode.h"
#include "nodes/UINodes.h"
#include "nodes/CurvesNode.h"
#include "nodes/RemoveBgNode.h"
#include "nodes/RampNode.h"
#include "nodes/ColorRampNode.h"
#include "nodes/PaletteNode.h"
#include "nodes/AnalyzeNodes.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/GeometryOpNodes.h"
#include "nodes/SceneNodes.h"
#include "nodes/EnvironmentNode.h"
#include "nodes/ModelSourceNode.h"
#include "nodes/Text3DNode.h"
#include "nodes/UtilityNodes.h"
#include "nodes/WirelessNodes.h"
#include "nodes/PointDistributionNodes.h"
#include "nodes/PathNode.h"
#include "nodes/CurveNode.h"
#include "nodes/OceanNode.h"
#include "nodes/SimulationNodes.h"
#include "nodes/GenerativeNodes.h"
#include "nodes/DrawNode.h"
#include "nodes/FeedbackNodes.h"
#include "nodes/SwitcherNode.h"
#include "nodes/Switcher3DNode.h"
#include "nodes/ModulatorNodes.h"
#include "nodes/OscNodes.h"
#include "nodes/PredictiveNotesNode.h"
#include "nodes/PredictiveQuantizeNode.h"
#include "nodes/PredictiveVelocityNode.h"
#include "nodes/PredictiveRhythmNode.h"
#include "nodes/MidiNodes.h"
#include "nodes/OutputNode.h"
#include "nodes/SyphonInNode.h"
#include "nodes/SyphonOutNode.h"
#include "nodes/ProjectionNode.h"
#include "nodes/AudioNodes.h"
#if INFINITE_GPL
// Turbo full build: GPL-3.0 modules (src/gpl/README.md).
#include "gpl/PunchFxNode.h"
#endif
#include "nodes/AudioEffectNode.h"
#include "nodes/WavetableNode.h"
#include "nodes/AnalogNode.h"
#include "nodes/DepthProjectionNode.h"
#include "nodes/GeometryTableNode.h"
#include "nodes/SlicerNode.h"
#include "nodes/MolderNode.h"
#include "nodes/GrainMolderNode.h"
#include "nodes/WaveTerrainNode.h"
#include "nodes/EquationNode.h"
#include "nodes/ImageSpectralSynthNode.h"
#include "nodes/AudioDisplacementNode.h"
#include "nodes/AudioTextureNode.h"
#include "nodes/AudioRibbonNode.h"
#include "nodes/AudioColorRampNode.h"
#include "nodes/OscillatorNode.h"
#include "nodes/MetallicNode.h"
#include "audio/Wavetable.h"
#include "nodes/NoteNodes.h"
#include "nodes/MidiOutNode.h" // Turbo 0.57 (upstream)
#include "nodes/SpatialMixerNode.h" // Turbo 0.57 (upstream)
#include "nodes/PatternPlayerNode.h" // Turbo 0.57
#include "nodes/NoteLooperNode.h" // Turbo 0.57
#include "nodes/ChordProgressionNode.h"
#include "nodes/MidiFileNode.h" // Turbo 0.48
#include "nodes/TransportControlNode.h"
#include "core/GestureRecorder.h"
#include "core/GltfImport.h"
#include "core/SysInfo.h"
#include "nodes/SamplerNode.h"
#include "nodes/PaulStretchNode.h"
#include "nodes/GranularNode.h"
#include "nodes/DrumSequencerNode.h"
#include "nodes/DrumPatterns.h"
#include "nodes/DrumMidiLibrary.h"
#include "nodes/AudioPluginNode.h"

// Infinite-Turbo build identity: CMake passes the project version; the
// build date is the compile date of main.cpp.
#ifndef INFINITE_TURBO_VERSION
#define INFINITE_TURBO_VERSION "dev"
#endif
#define INFINITE_TURBO_TITLE "Infinite-Turbo " INFINITE_TURBO_VERSION
static const char* kTurboBuildDate = __DATE__ " " __TIME__;
#include "nodes/LooperNode.h"
#include "nodes/TimelineNode.h"
#include "arrange/ArrangeModel.h"
#include "audio/dsp/SlicerDsp.h"
#include "nodes/MpcNode.h"
#include "nodes/SuperMixerNode.h"
#include "nodes/LayoutNode.h"
#include "nodes/VmpcNode.h"
#include "nodes/ClipMatrixNode.h"
#include "core/MidiMap.h"
#include "audio/SampleScanner.h"
#include "audio/PluginScanner.h"
#include "audio/MediaExtensions.h"

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioVoice.h"
#include "audio/ParamMailbox.h"
#include "audio/MeterRing.h"
#include "audio/DspMath.h"
#include "audio/NoteEventQueue.h"
#include "audio/MusicTime.h"
#include "audio/EffectDefs.h"
#include "audio/dsp/AudioFilterKernel.h"
#include "audio/dsp/EqKernel.h"
#include "audio/dsp/DynamicsKernel.h"
#include "audio/dsp/DelayKernel.h"
#include "audio/dsp/DriveKernel.h"
#include "audio/dsp/StereoKernel.h"
#include "audio/dsp/PitchShiftKernel.h"
#include "audio/dsp/ChorusKernel.h"
#include "audio/dsp/FlangerKernel.h"
#include "audio/dsp/PhaserKernel.h"
#include "audio/dsp/BitcrushKernel.h"
#include "audio/dsp/TransientShaperKernel.h"
#include "audio/dsp/StutterKernel.h"
#include "audio/dsp/RingModKernel.h"
#include "audio/dsp/TremoloKernel.h"
#include "audio/dsp/FormantFilterKernel.h"
#include "audio/dsp/WavetableShaperKernel.h"
#include "audio/dsp/LimiterKernel.h"
#include "audio/dsp/KeySnapKernel.h"

namespace ed = ax::NodeEditor;

// Turbo 0.57: the app's code lives in src/app/ as fragments included below in
// order, so it is still one translation unit: the same code, inlining and
// link as the old single 50k-line file. Folders:
//   ui/        widgets, theme, panels, previews, help
//   bodies/    per-node UI (DrawXxxBody / DrawXxxParams)
//   graph/     graph rules, audio topology, patch I/O, undo, RPC
//   frame/     main(): startup, the frame loop step by step, shutdown
//   selftest/  INFINITE_* test fixtures and hooks (dev runs only)
// A fragment sees only what the fragments before it declare: add code to the
// fragment of its kind, after what it uses.
namespace
{
   #include "app/Globals.inl" // constants, globals and shared state
   #include "app/ui/Widgets.inl" // deferred dropdown, modulatable params, readouts, sliders, MIDI learn pins, knobs
   #include "app/ui/PinsAndTheme.inl" // palette-bindable colours, pins, themes (incl. Infinite Dark)
   #include "app/graph/Graph.inl" // file dialogs, node lookup, pin counts, cable rules
   #include "app/bodies/VisualParams.inl" // per-node parameter UI of the visual nodes
   #include "app/bodies/Macros.inl" // macro and UI control node bodies
   #include "app/ui/AudioLayout.inl" // audio node body layout (v3), columns, waveform view
   #include "app/bodies/AudioSources.inl" // sampler, oscillators, wavetable, synths, meters, gain
   #include "app/bodies/DrumSequencer.inl" // Drum Sequencer and bundled pattern packs
   #include "app/ui/Library.inl" // sample / media library and plugin browser panels
   #include "app/bodies/NotesAndMix.inl" // audio in, mixer and note processing node bodies
   #include "app/bodies/NoteSources.inl" // note sources: keyboard, chords, Pattern Player, Note Looper, MIDI Out
   #include "app/bodies/AudioEffects.inl" // audio effect node bodies
   #include "app/bodies/TurboInstruments.inl" // Looper, MPC, Super Mixer, Analog, Slicer, Molders, Transport Control
   #include "app/bodies/AudioBody.inl" // DrawAudioNodeBody: the audio body dispatcher
   #include "app/bodies/SceneParams.inl" // media, geometry, 3D, material, blend and filter params
   #include "app/ui/Preview.inl" // previews, comments, groups, mini viewport, viewport panel
   #include "app/ui/Mapping.inl" // colour tags, MIDI mapping and modulation range editors
   #include "app/ui/Help.inl" // node help texts and the help window
   #include "app/graph/AudioGraph.inl" // audio topology, engine start, scene settings, device recovery
   #include "app/graph/PatchIO.inl" // patch save / load / autosave / recovery, undo
   #include "app/graph/Rpc.inl" // MCP remote control (RPC) helpers
   #include "app/ui/AppWindows.inl" // minimap, save prompts, window title, projector windows
   #include "app/bodies/LayoutVmpc.inl" // Layout and VMPC node bodies
}

#include "app/selftest/DspFixtures.inl" // audio node sweep discovery and the INFINITE_DSPTEST fixtures
#include "app/selftest/EngineTests.inl" // PDC, param sweep and plugin scan tests

int main(int argc, char** argv)
{
   #include "app/frame/Startup.inl" // startup: arguments, window, audio, editor, first patch

   StartupTrace("startup: entering main loop");
   while (!glfwWindowShouldClose(window))
   {
      #include "app/frame/FrameBegin.inl" // frame start: events, RPC, file watch, audio recovery, projectors
      #include "app/selftest/InputHooks.inl" // INFINITE_*TEST input injection hooks (dev runs only)
      #include "app/frame/FrameSetup.inl" // ImGui frame, transport tick, per-frame resets
      #include "app/frame/MenuBar.inl" // main window, recovery prompt, menu bar
      #include "app/frame/Canvas.inl" // canvas layout, docked panels, node editor begin, drops
      #include "app/selftest/CanvasHooks.inl" // INFINITE_*TEST canvas hooks (dev runs only)
      #include "app/frame/Nodes.inl" // per-node drawing loop
      #include "app/frame/Links.inl" // MIDI learn pick, cables, splicing, new links, keyboard, deletions, snapping
      #include "app/frame/Popups.inl" // search, spawn menu, dropdown popups
      #include "app/frame/ModMatrix.inl" // modulation matrix and editor-side windows
      #include "app/frame/Panels.inl" // node browser panel and windows outside the canvas
      #include "app/frame/Cook.inl" // expressions, modulation, cooking, rendering, swap
   }

   #include "app/frame/Shutdown.inl" // shutdown with watchdog
}

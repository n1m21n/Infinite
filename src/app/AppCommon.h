#pragma once
// Everything main.cpp used to include before its first declaration: the shared
// preamble of main.cpp and the translation units split out of it (src/app/).
#define GLFW_INCLUDE_NONE
#include "gl3.h"
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
   // No CF symbols remain in this file, but the include predates the Windows
   // port and macOS toolchains expect it in the preamble - keep it Apple-only.
   #include <CoreFoundation/CoreFoundation.h>
   // malloc_zone_statistics - used by INFINITE_FIELDSAMPLETEST's zero-
   // allocation check (reads the allocator's own counters rather than
   // overriding the process-wide operator new/delete).
   #include <malloc/malloc.h>
#endif

#include "imgui.h"
#include "platform/common/MidiCC14.h"
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
#include <cstdarg>
#include "TablerIcons.h"

#include "stb_image_write.h"
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unordered_set>
#include <filesystem>
#include <sys/stat.h>
#include "platform/AppPaths.h"
#include "platform/Platform.h"
#include "IconsLucide.h"
#include "core/SysInfo.h"
#include "core/BenchReport.h"
#include "core/BenchFrameTail.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

// INFINITE_MIDIPARSETEST's pure-parser hook, so the test can drive
// MidiLinux.cpp's ALSA sequencer event->table/ring code with synthetic
// snd_seq_event_t values and no real /dev/snd/seq - see
// docs/plans/linux/phase-02-audio-midi.md 2.5.2 and
// docs/plans/linux/validation.md's P0 spike (no kernel sound modules on
// GitHub Actions/OrbStack, so this is the primary MIDI proof on CI). Linux
// only, same footing as the existing _WIN32 branches in this file for
// platform-specific test dispatch - see linux-parity SS0's note that main.cpp
// currently carries no __linux__ branches, which this is the first of,
// exactly because there is no portable way to exercise an ALSA-specific
// parser from a Platform:: call all three platforms implement.
#if defined(__linux__)
#include "platform/linux/MidiParseTestHooks.h"
#endif

// Displayed shortcut labels: the modifier key shown in menus and the
// shortcuts reference differs by platform (Cmd doesn't exist on Windows),
// while the underlying handling already accepts Ctrl on both (see cmdOrCtrl).
#if defined(__APPLE__)
   #define MODKEY "Cmd"
#else
   #define MODKEY "Ctrl"
#endif

// Test fixtures live in the OS temp directory; /tmp was hardcoded before
// the Windows port.
inline std::string TmpPath(const std::string& name)
{
   return AppPaths::TempDir() + "/" + name;
}
#include <functional>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <unordered_map>
#include <condition_variable>
#include <deque>
#include <set>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include "core/NodeFactory.h"
#include "core/CategoryColors.h"
#include "core/UiScale.h"
#include "core/SplashScreen.h"
#include "core/LauncherCard.h"
#include "core/GLUtil.h"
#include "core/GraphNode.h"
#include "core/FilterDefs.h"
#include "core/BlendModes.h"
#include "core/Transport.h"
#include "core/AudioTopologyRequest.h"
#include "core/Modulation.h"
#include "core/MovementLog.h"
#include "core/MovementStats.h"
#include "core/AudioSummary.h"
#include "core/ColorStats.h"
#include "core/ContactSheet.h"
#include "core/HeadlessJob.h"
#include "core/HeadlessNotes.h"
#include "core/PatchSchema.h"
#include "core/PatchExplain.h"

#include "core/GestureRecorder.h"
#include "core/Expression.h"
#include "core/field/FieldTypes.h"
#include "core/field/FieldSwizzle.h"
#include "core/field/FieldLex.h"
#include "core/field/FieldParse.h"
#include "core/field/FieldIR.h"
#include "core/field/FieldBytecode.h"
#include "core/field/FieldVM.h"
#include "core/field/FieldRandom.h"
#include "core/field/ElementStore.h"
#include "core/field/ElementBackend.h"
#include "core/field/GlslBackend.h"
#include "core/field/Transfer.h"
#include "core/field/ReduceOps.h"
#include "core/field/BackendRegister.h"
#include "core/field/PinTable.h"
#include "core/ExprGlobals.h"
#include "core/AISkillContent.h"
#include "core/Palette.h"
#include "core/Patch.h"
#include "core/PatchLayout.h"
#include "arrange/ArrangeModel.h"
#include "arrange/ArrangeMediaImport.h"
#include "core/NodeViewport.h"
#include "core/AudioCable.h"
#include "core/NoteCable.h"
#include "core/RemoteControl.h"
#include "core/PatchJson.h"
#include "core/UpdateCheck.h"

#ifndef INFINITE_VERSION_STRING
#define INFINITE_VERSION_STRING "0.0.0"
#endif
#include "nodes/ImageSourceNode.h"
#include "nodes/SlideshowNode.h"
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
#include "nodes/CurvesNode.h"
#include "nodes/RemoveBgNode.h"
#include "nodes/RampNode.h"
#include "nodes/ColorRampNode.h"
#include "nodes/PaletteNode.h"
#include "nodes/AnalyzeNodes.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/GeometryOpNodes.h"
#include "nodes/FieldElementNode.h"
#include "nodes/FieldPrimitiveNode.h"
#include "nodes/FieldGraphNode.h"
#include "nodes/FieldPixelNode.h"
#include "nodes/FieldSampleNode.h"
#include "nodes/FieldSynthNode.h"
#include "nodes/SceneNodes.h"
#include "nodes/EnvironmentNode.h"
#include "nodes/ModelSourceNode.h"
#include "GltfImport.h"
#include "nodes/Text3DNode.h"
#include "nodes/UtilityNodes.h"
#include "nodes/PointDistributionNodes.h"
#include "nodes/DepthProjectionNode.h"
#include "nodes/PathNode.h"
#include "nodes/GeometryTableNode.h"
#include "nodes/CurveNode.h"
#include "nodes/OceanNode.h"
#include "nodes/SimulationNodes.h"
#include "nodes/GenerativeNodes.h"
#include "nodes/DrawNode.h"
#include "nodes/FeedbackNodes.h"
#include "nodes/SwitcherNode.h"
#include "nodes/Switcher3DNode.h"
#include "nodes/Group3DNode.h"
#include "nodes/ModulatorNodes.h"
#include "nodes/PredictionNodes.h"
#include "nodes/PredictiveNotesNode.h"
#include "nodes/PredictiveModulatorNode.h"
#include "nodes/PredictiveColoringNode.h"
#include "nodes/PredictiveQuantizeNode.h"
#include "nodes/PredictiveVelocityNode.h"
#include "nodes/PredictiveRhythmNode.h"
#include "nodes/OscNodes.h"
#include "nodes/MidiNodes.h"
#include "nodes/OutputNode.h"
#include "nodes/SyphonInNode.h"
#include "nodes/SyphonOutNode.h"
#include "nodes/NdiInNode.h"
#include "nodes/NdiOutNode.h"
#include "nodes/ProjectionNode.h"
#include "nodes/AudioNodes.h"
#include "nodes/AudioMeterNode.h"
#include "nodes/AudioEffectNode.h"
#include "nodes/WavetableNode.h"
#include "nodes/AnalogNode.h"
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
#include "audio/AudioFileWriter.h"
#include "nodes/NoteNodes.h"
#include "nodes/SamplerNode.h"
#include "nodes/SlicerNode.h"
#include "nodes/PaulStretchNode.h"
#include "nodes/MolderNode.h"
#include "nodes/GrainMolderNode.h"
#include "nodes/GranularNode.h"
#include "nodes/DrumSequencerNode.h"
#include "nodes/DrumPatterns.h"
#include "nodes/LooperNode.h"
#include "nodes/MpcNode.h"
#include "nodes/AudioPluginNode.h"
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
#include "audio/MidiFile.h"
#include "audio/MusicTime.h"
#include "audio/EffectDefs.h"
#include "audio/dsp/PortableFft.h"
#include "audio/dsp/PortableFftFixture.h"
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
#include "audio/dsp/ResonatorBankKernel.h"
#include "audio/dsp/CycleShaperKernel.h"
#include "audio/dsp/SpecBlurKernel.h"
#include "audio/dsp/KeySnapKernel.h"
#include "audio/dsp/SpectrumSlideKernel.h"
#include "audio/dsp/ShapeResonatorKernel.h"
#include "audio/dsp/SlicerDsp.h"
#include "audio/dsp/ReverbKernel.h"

namespace ed = ax::NodeEditor;

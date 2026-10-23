cmake_minimum_required(VERSION 3.25)

# Infinite-Turbo (for Windows)
# Unofficial Windows-only fork of Infinite (https://github.com/n1m21n/Infinite).
# macOS support was removed on purpose: every platform path below is Windows
# (MSVC 2022, x64, vcpkg static triplet, JUCE, OpenCV, Spout2, Windows ML).
project(InfiniteTurbo VERSION 0.70.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Infinite-Turbo builds only on Windows x64 with MSVC (Visual Studio 2022).")
endif()

# Windows ML's native C/C++ package requires C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(INFINITE_ENABLE_VST3 "Build the VST3 plugin backend (JUCE)" ON)
option(INFINITE_ENABLE_SPOUT "Build Spout input/output support" ON)
# Turbo 0.53: GPL-3.0 modules in src/gpl (full build). OFF = the MIT build,
# which is what release-windows.bat publishes. See src/gpl/README.md.
option(INFINITE_ENABLE_GPL "Build the GPL-3.0 modules in src/gpl (full Infinite-Turbo build)" ON)
option(INFINITE_ENABLE_DIRECTML_MATTING "Accelerate Remove Background with Windows ML / DirectML" ON)
option(INFINITE_ENABLE_AVX2 "Compile with /arch:AVX2 (any x64 CPU from ~2013 on)" ON)
option(INFINITE_ENABLE_ASIO "ASIO driver support (downloads the Steinberg ASIO SDK, GPLv3)" ON)
option(INFINITE_ENABLE_RTX "RTX render mode: NVIDIA OptiX ray tracing (full edition; needs the CUDA Toolkit to build)" ON)
option(INFINITE_ENABLE_RENDER_HQ "Render 3D HQ node: Filament-referenced PBR, particle impostors, shadows, GTAO, TAA (OFF: it draws as Render 3D)" ON)
option(INFINITE_ENABLE_LTCG "Whole-program optimization (/GL + /LTCG) in Release; slower link" OFF)
option(INFINITE_ENABLE_TRACY "Tracy profiler zones, CPU and GL GPU (BSD-3); connect the Tracy v0.11.1 GUI" OFF)

# GLFW is built from source and linked statically.
include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs videoio dnn)
# Turbo 0.53: video files decode on FFmpeg directly (HAP to GPU blocks, DXV,
# D3D11VA hardware decode). Same static FFmpeg the opencv4[ffmpeg] port uses.
find_package(FFMPEG REQUIRED)
find_package(assimp CONFIG REQUIRED)

set(INFINITE_HAS_WINDOWS_ML 0)
if(INFINITE_ENABLE_DIRECTML_MATTING)
    set(INFINITE_WINDOWSML_ROOT "$ENV{INFINITE_WINDOWSML_ROOT}" CACHE PATH
        "Extracted Microsoft.Windows.AI.MachineLearning NuGet package root")
    if(NOT INFINITE_WINDOWSML_ROOT AND DEFINED ENV{LOCALAPPDATA})
        set(INFINITE_WINDOWSML_ROOT
            "$ENV{LOCALAPPDATA}/InfiniteBuild/windowsml/Microsoft.Windows.AI.MachineLearning.2.2.12"
            CACHE PATH "Extracted Microsoft.Windows.AI.MachineLearning NuGet package root" FORCE)
    endif()
    if(NOT EXISTS "${INFINITE_WINDOWSML_ROOT}/build/cmake")
        message(FATAL_ERROR
            "Windows ML package was not found at '${INFINITE_WINDOWSML_ROOT}'. "
            "Run install-dependencies.bat, or configure with "
            "-DINFINITE_ENABLE_DIRECTML_MATTING=OFF for the CPU-only fallback.")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${INFINITE_WINDOWSML_ROOT}/build/cmake")
    find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)
    if(NOT TARGET WindowsML::OnnxRuntime OR NOT TARGET WindowsML::DirectML)
        message(FATAL_ERROR "The Windows ML package is missing its ONNX Runtime / DirectML CMake targets.")
    endif()

    # The NuGet package stores ONNX Runtime headers in its winml folder.
    # MSBuild normally publishes that layout through package properties,
    # but package 2.2.12 does not always propagate the include directory
    # to a non-MSBuild CMake consumer. Locate the header itself and expose
    # that exact directory to the native C++ target.
    find_path(INFINITE_WINDOWSML_ORT_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        PATHS "${INFINITE_WINDOWSML_ROOT}"
        PATH_SUFFIXES
            include
            include/winml
            build/native/include
            build/native/include/winml
            runtimes/win-x64/native/include
            runtimes/win-x64/native/include/winml
        NO_DEFAULT_PATH)
    if(NOT INFINITE_WINDOWSML_ORT_INCLUDE_DIR)
        file(GLOB_RECURSE INFINITE_WINDOWSML_ORT_HEADERS LIST_DIRECTORIES FALSE
            "${INFINITE_WINDOWSML_ROOT}/*onnxruntime_cxx_api.h")
        list(LENGTH INFINITE_WINDOWSML_ORT_HEADERS INFINITE_WINDOWSML_ORT_HEADER_COUNT)
        if(INFINITE_WINDOWSML_ORT_HEADER_COUNT GREATER 0)
            list(GET INFINITE_WINDOWSML_ORT_HEADERS 0 INFINITE_WINDOWSML_ORT_HEADER)
            get_filename_component(INFINITE_WINDOWSML_ORT_INCLUDE_DIR
                "${INFINITE_WINDOWSML_ORT_HEADER}" DIRECTORY)
        endif()
    endif()
    if(NOT INFINITE_WINDOWSML_ORT_INCLUDE_DIR)
        message(FATAL_ERROR
            "onnxruntime_cxx_api.h was not found inside '${INFINITE_WINDOWSML_ROOT}'. "
            "Run install-dependencies.bat again to repair Windows ML 2.2.12.")
    endif()
    set(INFINITE_HAS_WINDOWS_ML 1)
    message(STATUS "Windows ML C++ headers: ${INFINITE_WINDOWSML_ORT_INCLUDE_DIR}")
    message(STATUS "Remove Background acceleration: Windows ML + DirectML (DX12)")
else()
    message(STATUS "Remove Background acceleration: OpenCV CPU fallback")
endif()

if(INFINITE_ENABLE_SPOUT)
    # The vcpkg spout2 port builds both shared and static targets, then
    # removes bin/Spout.dll for static triplets. Its exported targets file
    # still validates that removed DLL and makes find_package fail before
    # Spout2::Spout_static can be selected. Import only the static library
    # that this application actually uses.
    if(NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR "Spout2 requires VCPKG_INSTALLED_DIR and VCPKG_TARGET_TRIPLET")
    endif()

    set(INFINITE_SPOUT2_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    find_path(INFINITE_SPOUT2_INCLUDE_DIR
        NAMES SpoutSender.h SpoutReceiver.h
        PATHS "${INFINITE_SPOUT2_ROOT}/include/SpoutGL"
        NO_DEFAULT_PATH
        REQUIRED)
    find_library(INFINITE_SPOUT2_LIBRARY_RELEASE
        NAMES Spout_static
        PATHS "${INFINITE_SPOUT2_ROOT}/lib"
        NO_DEFAULT_PATH
        REQUIRED)
    find_library(INFINITE_SPOUT2_LIBRARY_DEBUG
        NAMES Spout_static
        PATHS "${INFINITE_SPOUT2_ROOT}/debug/lib"
        NO_DEFAULT_PATH)
    if(NOT INFINITE_SPOUT2_LIBRARY_DEBUG)
        set(INFINITE_SPOUT2_LIBRARY_DEBUG "${INFINITE_SPOUT2_LIBRARY_RELEASE}")
    endif()

    add_library(InfiniteSpout2 STATIC IMPORTED GLOBAL)
    set_target_properties(InfiniteSpout2 PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release"
        IMPORTED_LOCATION_DEBUG "${INFINITE_SPOUT2_LIBRARY_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${INFINITE_SPOUT2_LIBRARY_RELEASE}"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        INTERFACE_INCLUDE_DIRECTORIES "${INFINITE_SPOUT2_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "opengl32;kernel32;user32;gdi32;winspool;comdlg32;advapi32;shell32;ole32;oleaut32;uuid;odbc32;odbccp32;d3d9;d3d11;dxgi;shlwapi;version")
endif()

# JUCE supplies the Windows WASAPI/MIDI/VST3 platform layer while the
# existing GLFW/ImGui renderer remains the application shell.
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(juce)

set(IMGUI_DIR external/imgui)
set(NODE_EDITOR_DIR external/imgui-node-editor)
set(STB_DIR external/stb)

set(INFINITE_PLATFORM_SOURCES
    src/platform/PlatformWindows.cpp
    src/platform/MidiOutWindows.cpp
    src/platform/common/MidiOutSink.cpp
    src/platform/VideoDecodeWindows.cpp
    src/platform/CameraWindows.cpp
    src/platform/ImageDecodeMemory.cpp
    src/platform/CrashHandlerWindows.cpp
    src/platform/PluginVST3Windows.cpp
    src/audio/AudioFileWriterWindows.cpp
    src/nodes/TextNodeWindows.cpp
    cmake/InfiniteWindows.rc)

add_executable(Infinite WIN32
    src/main.cpp
    src/core/GLUtil.cpp
    src/core/HapDecode.cpp
    src/core/VideoTexture.cpp
    src/core/CurveShape.cpp
    src/core/NodeFactory.cpp
    src/core/CategoryColors.cpp
    src/core/FilterDefs.cpp
    src/core/BlendModes.cpp
    src/core/Mesh.cpp
    src/core/MeshDecimate.cpp
    src/core/Delaunay.cpp
    src/core/CurveOps.cpp
    src/core/NodeViewport.cpp
    src/core/Transport.cpp
    src/core/AudioDecodeCache.cpp
    src/core/GestureRecorder.cpp
    src/core/GltfImport.cpp
    src/core/SysInfo.cpp
    src/core/Modulation.cpp
    src/core/Expression.cpp
    src/core/ExprGlobals.cpp
    src/core/Palette.cpp
    src/core/Patch.cpp
    src/core/PatchSchema.cpp
    src/core/HeadlessJob.cpp
    src/arrange/ArrangeModel.cpp
    src/core/RemoteControl.cpp
    src/platform/McpBridgeWindows.cpp
    src/platform/ClaudeChatWindows.cpp
    src/platform/McpShareWindows.cpp
    src/core/RuntimeLog.cpp
    src/core/PatchJson.cpp
    src/core/OscMessage.cpp
    ${INFINITE_PLATFORM_SOURCES}
    src/audio/AudioEngine.cpp
    src/audio/ParamMailbox.cpp
    src/audio/MeterRing.cpp
    src/audio/AudioCaptureRing.cpp
    external/shine/shine_mp3.c
    src/audio/AudioVoice.cpp
    src/audio/Wavetable.cpp
    src/audio/EffectDefs.cpp
    src/audio/dsp/AudioFilterKernel.cpp
    src/audio/dsp/EqKernel.cpp
    src/audio/dsp/DynamicsKernel.cpp
    src/audio/dsp/DelayKernel.cpp
    src/audio/dsp/ReverbKernel.cpp
    src/audio/dsp/DriveKernel.cpp
    src/audio/dsp/StereoKernel.cpp
    src/audio/dsp/PitchShiftKernel.cpp
    src/audio/dsp/SpectralPitchShifter.cpp
    src/audio/dsp/ChorusKernel.cpp
    src/audio/dsp/FlangerKernel.cpp
    src/audio/dsp/PhaserKernel.cpp
    src/audio/dsp/BitcrushKernel.cpp
    src/audio/dsp/TransientShaperKernel.cpp
    src/audio/dsp/StutterKernel.cpp
    src/audio/dsp/RingModKernel.cpp
    src/audio/dsp/FrequencyShifterKernel.cpp
    src/audio/dsp/TremoloKernel.cpp
    src/audio/dsp/FormantFilterKernel.cpp
    src/audio/dsp/WavetableShaperKernel.cpp
    src/audio/dsp/LimiterKernel.cpp
    src/nodes/ImageSourceNode.cpp
    src/nodes/ShapeNode.cpp
    src/nodes/FormulaNode.cpp
    src/nodes/FilterNode.cpp
    src/nodes/BlendNode.cpp
    src/nodes/LayerStackNode.cpp
    src/nodes/FitNode.cpp
    src/nodes/VideoSourceNode.cpp
    src/nodes/VideoInNode.cpp
    src/nodes/ModulatorNodes.cpp
    src/nodes/OscNodes.cpp
    src/nodes/NoiseNode.cpp
    src/nodes/TextureNode.cpp
    src/nodes/ResynthNode.cpp
    src/nodes/MacroNodes.cpp
    src/nodes/PredictionNodes.cpp
    src/nodes/PredictiveModulatorNode.cpp
    src/core/MovementLog.cpp
    src/core/MovementStats.cpp
    external/miniz.c
    src/nodes/AudioMeterNode.cpp
    src/nodes/SlideshowNode.cpp
    src/audio/dsp/CycleShaperKernel.cpp
    src/audio/dsp/ResonatorBankKernel.cpp
    src/audio/dsp/SpecBlurKernel.cpp
    src/audio/dsp/KeySnapKernel.cpp
    src/audio/dsp/SpectrumSlideKernel.cpp
    src/audio/dsp/SaturatorKernel.cpp
    src/audio/dsp/ShapeResonatorKernel.cpp
    src/audio/dsp/ConvolutionKernel.cpp
    src/audio/dsp/MeshModalSolver.cpp
    src/audio/dsp/SlicerDsp.cpp
    src/audio/dsp/MolderDsp.cpp
    src/audio/dsp/GrainMolderDsp.cpp
    src/nodes/UINodes.cpp
    src/nodes/CurvesNode.cpp
    src/nodes/RemoveBgNode.cpp
    src/nodes/DrawNode.cpp
    src/nodes/RampNode.cpp
    src/nodes/ColorRampNode.cpp
    src/nodes/PaletteNode.cpp
    src/nodes/AnalyzeNodes.cpp
    src/nodes/MidiNodes.cpp
    src/nodes/Geometry3DNodes.cpp
    src/nodes/Render3DPost.cpp
    src/nodes/Render3DTemporal.cpp
    src/nodes/Render3DCull.cpp
    src/nodes/Render3DMotion.cpp
    src/nodes/Render3DFxNode.cpp
    src/nodes/render_hq/RenderHqNode.cpp
    src/nodes/EnvironmentNode.cpp
    src/nodes/ModelSourceNode.cpp
    src/nodes/Text3DNode.cpp
    src/nodes/UtilityNodes.cpp
    src/nodes/WirelessNodes.cpp
    src/nodes/TimeMachineNode.cpp
    src/nodes/OpticalFlowNode.cpp
    src/nodes/FluidNode.cpp
    src/nodes/VolumeNode.cpp
    src/nodes/NdiInNode.cpp
    src/nodes/NdiOutNode.cpp
    src/platform/ndi/NdiRuntime.cpp
    src/core/SweepRevolve.cpp
    src/core/BevelInset.cpp
    src/core/ColorStats.cpp
    src/nodes/PredictiveColoringNode.cpp
    src/nodes/SweepRevolveNodes.cpp
    src/nodes/LSystemNode.cpp
    src/nodes/ProximityLinesNode.cpp
    src/nodes/RigidBodyNode.cpp
    src/nodes/FMNode.cpp
    src/nodes/StringNode.cpp
    src/nodes/PartialsNode.cpp
    src/nodes/PathNode.cpp
    src/nodes/OceanNode.cpp
    src/nodes/SimulationNodes.cpp
    src/nodes/GenerativeNodes.cpp
    src/nodes/GeometryOpNodes.cpp
    src/nodes/PointDistributionNodes.cpp
    src/nodes/SceneNodes.cpp
    src/nodes/FeedbackNodes.cpp
    src/nodes/SwitcherNode.cpp
    src/nodes/Switcher3DNode.cpp
    src/nodes/DelaunayNodes.cpp
    src/nodes/CurveOpsNode.cpp
    src/nodes/PopNode.cpp
    src/pop/PopData.cpp
    src/pop/PopGpu.cpp
    src/pop/PopKernelSource.cpp
    src/pop/PopCpu.cpp
    src/pop/PopOps.cpp
    src/pop/PopOpsGen.cpp
    src/pop/PopOpsA.cpp
    src/pop/PopOpsA2.cpp
    src/pop/PopOpsB.cpp
    src/pop/PopOpsB2.cpp
    src/pop/PopOpsB3.cpp
    src/pop/PopOpsPlatform.cpp
    src/pop/GpuAlgo.cpp
    src/pop/ParticleGpu.cpp
    src/nodes/CurveNode.cpp
    src/nodes/OutputNode.cpp
    src/nodes/SyphonInNode.cpp
    src/nodes/SyphonOutNode.cpp
    src/nodes/ProjectionNode.cpp
    src/nodes/AudioNodes.cpp
    src/nodes/NoteNodes.cpp
    src/nodes/WavetableNode.cpp
    src/nodes/AnalogNode.cpp
    src/nodes/SlicerNode.cpp
    src/nodes/MolderNode.cpp
    src/nodes/GrainMolderNode.cpp
    src/nodes/GeometryTableNode.cpp
    src/nodes/DepthProjectionNode.cpp
    src/nodes/ChordProgressionNode.cpp
    src/nodes/MidiOutNode.cpp
    src/nodes/SpatialMixerNode.cpp
    src/nodes/NotePatternLibrary.cpp
    src/nodes/PatternPlayerNode.cpp
    src/nodes/NoteLooperNode.cpp
    src/audio/dsp/HrtfSet.cpp
    src/audio/dsp/HrtfKemarData.cpp
    src/nodes/MidiFileNode.cpp
    src/core/MidiFile.cpp
    src/nodes/TransportControlNode.cpp
    src/nodes/WaveTerrainNode.cpp
    src/nodes/EquationNode.cpp
    src/nodes/ImageSpectralSynthNode.cpp
    src/nodes/AudioDisplacementNode.cpp
    src/nodes/AudioTextureNode.cpp
    src/nodes/AudioRibbonNode.cpp
    src/nodes/AudioColorRampNode.cpp
    src/audio/dsp/SpectralAdditiveSynth.cpp
    src/nodes/OscillatorNode.cpp
    src/nodes/MetallicNode.cpp
    src/nodes/AudioEffectNode.cpp
    src/nodes/SamplerNode.cpp
    src/nodes/PaulStretchNode.cpp
    src/nodes/GranularNode.cpp
    src/nodes/DrumSequencerNode.cpp
    src/nodes/DrumMidiLibrary.cpp
    src/nodes/AudioPluginNode.cpp
    src/nodes/LooperNode.cpp
    src/nodes/TimelineNode.cpp
    src/nodes/ClipMatrixNode.cpp
    src/nodes/MpcNode.cpp
    src/nodes/SuperMixerNode.cpp
    src/nodes/LayoutNode.cpp
    src/nodes/VmpcNode.cpp
    src/core/MidiMap.cpp
    src/audio/NoteModel.cpp
    src/nodes/PredictiveNotesNode.cpp
    src/nodes/PredictiveQuantizeNode.cpp
    src/nodes/PredictiveVelocityNode.cpp
    src/nodes/PredictiveRhythmNode.cpp
    src/audio/SampleScanner.cpp
    src/audio/PluginScanner.cpp


    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp

    ${NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
    ${NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${NODE_EDITOR_DIR}/crude_json.cpp
)

# Turbo 0.57: main.cpp includes the app's code from src/app/*.inl (one
# translation unit). Listed here only so IDEs show them; they are not compiled
# on their own, and MSVC tracks them as dependencies of main.cpp.
file(GLOB_RECURSE INFINITE_APP_FRAGMENTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.inl")
target_sources(Infinite PRIVATE ${INFINITE_APP_FRAGMENTS})
set_source_files_properties(${INFINITE_APP_FRAGMENTS} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/app" PREFIX "app" FILES ${INFINITE_APP_FRAGMENTS})

# GUI subsystem without a console window, keeping the portable main().
target_link_options(Infinite PRIVATE "/ENTRY:mainCRTStartup")

target_include_directories(Infinite PRIVATE
    src src/core src/nodes src/platform src/platform/ndi src/audio
    ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp
    ${NODE_EDITOR_DIR}
    ${STB_DIR}
    external
    external/json
    external/shine
    external/cgltf
    external/ndi/include
)

target_link_libraries(Infinite PRIVATE glfw strmiids dbghelp)

if(INFINITE_ENABLE_VST3)
    # Scan third-party DLLs in a deliberately tiny helper process. Launching
    # another copy of the full Infinite GUI here made application startup
    # failures indistinguishable from bad plugins and poisoned the blocklist.
    add_executable(infinite-vst3-scanner
        src/scanner_main_windows.cpp
        src/platform/PluginVST3Windows.cpp
        # The plugin play head (PluginVST3Windows.cpp) reads the Transport.
        src/core/Transport.cpp
    )
    target_include_directories(infinite-vst3-scanner PRIVATE
        src src/core src/nodes src/platform src/audio
    )
    target_compile_definitions(infinite-vst3-scanner PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        INFINITE_WINDOWS=1
        INFINITE_ENABLE_VST3=1
        INFINITE_VST3_SCANNER=1
    )
    target_link_libraries(infinite-vst3-scanner PRIVATE
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_processors
        # opengl32: PluginVST3Windows.cpp saves/restores the WGL context
        # around plugin UI code (GlContextKeeper).
        ole32 shell32 shlwapi version winmm ws2_32 opengl32
    )
    target_compile_options(infinite-vst3-scanner PRIVATE /utf-8 /permissive- /Zc:char8_t-)
    set_target_properties(infinite-vst3-scanner PROPERTIES
        OUTPUT_NAME "infinite-vst3-scanner")
    add_dependencies(Infinite infinite-vst3-scanner)
endif()

# imgui-node-editor 0.9.1 ships its own GetKeyIndex() shim, which collides with
# ImGui's obsolete-keyio copy. Dropping the obsolete API leaves only the shim.
target_compile_definitions(Infinite PRIVATE
    IMGUI_DISABLE_OBSOLETE_KEYIO
    IMGUI_DEFINE_MATH_OPERATORS
    INFINITE_TURBO=1
    INFINITE_TURBO_VERSION="${PROJECT_VERSION}"
)


target_compile_definitions(Infinite PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _USE_MATH_DEFINES
    GLEW_STATIC
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    INFINITE_WINDOWS=1
    INFINITE_ENABLE_VST3=$<BOOL:${INFINITE_ENABLE_VST3}>
    INFINITE_ENABLE_SPOUT=$<BOOL:${INFINITE_ENABLE_SPOUT}>
    INFINITE_GPL=$<BOOL:${INFINITE_ENABLE_GPL}>
    INFINITE_HAS_WINDOWS_ML=${INFINITE_HAS_WINDOWS_ML}
)
if(INFINITE_ENABLE_GPL)
    file(GLOB INFINITE_GPL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gpl/*.cpp")
    target_sources(Infinite PRIVATE ${INFINITE_GPL_SOURCES})
    message(STATUS "Infinite-Turbo edition: full (GPL-3.0 modules from src/gpl)")
else()
    message(STATUS "Infinite-Turbo edition: MIT (src/gpl left out)")
endif()

# ---- RTX render mode (Turbo 0.62) ------------------------------------------
# Render 3D's RTX mode: NVIDIA OptiX hardware ray tracing. Full edition only
# (not the MIT build). Needs a CUDA Toolkit with nvcc at build time (11.8 or
# newer; the newest install that has nvcc is used, or INFINITE_CUDA_ROOT) and
# fetches the OptiX 7.7 headers from github.com/NVIDIA/optix-dev. At run time
# only the NVIDIA driver is needed (nvcuda.dll is delay-loaded, so the exe
# still starts without one). Without any of this, RtxStub.cpp keeps the mode
# listed and Render 3D draws raster with a note.
set(INFINITE_RTX_BUILT OFF)
if(INFINITE_ENABLE_RTX AND INFINITE_ENABLE_GPL)
    set(INFINITE_CUDA_ROOT "" CACHE PATH "CUDA Toolkit folder for the RTX module (empty = newest with nvcc)")
    set(_rtx_cuda "${INFINITE_CUDA_ROOT}")
    if(NOT _rtx_cuda OR NOT EXISTS "${_rtx_cuda}/bin/nvcc.exe")
        set(_rtx_cuda "")
        file(TO_CMAKE_PATH "$ENV{ProgramFiles}" _rtx_pf)
        file(GLOB _rtx_cands "${_rtx_pf}/NVIDIA GPU Computing Toolkit/CUDA/v*")
        list(SORT _rtx_cands COMPARE NATURAL ORDER DESCENDING)
        foreach(_c IN LISTS _rtx_cands)
            if(EXISTS "${_c}/bin/nvcc.exe" AND EXISTS "${_c}/include/cudaGL.h" AND EXISTS "${_c}/lib/x64/cuda.lib")
                set(_rtx_cuda "${_c}")
                break()
            endif()
        endforeach()
    endif()
    if(_rtx_cuda)
        FetchContent_Declare(optixdev
            GIT_REPOSITORY https://github.com/NVIDIA/optix-dev.git
            GIT_TAG v7.7.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(optixdev) # headers only, no CMakeLists of its own
    endif()
    if(_rtx_cuda AND EXISTS "${optixdev_SOURCE_DIR}/include/optix.h")
        get_filename_component(_rtx_ccbin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(_rtx_out "${CMAKE_BINARY_DIR}/rtx")
        file(MAKE_DIRECTORY "${_rtx_out}")
        # MSVC newer than the toolkit knows: device code uses no STL, so the
        # version checks are switched off rather than failing the build.
        set(_rtx_flags -allow-unsupported-compiler -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
            -ccbin "${_rtx_ccbin}" -I "${optixdev_SOURCE_DIR}/include" -I "${CMAKE_CURRENT_SOURCE_DIR}/src/rtx"
            -arch=compute_75 -use_fast_math -m64)
        add_custom_command(OUTPUT "${_rtx_out}/RtxDevice.optixir"
            COMMAND "${_rtx_cuda}/bin/nvcc.exe" -optix-ir -rdc=true ${_rtx_flags}
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtx/RtxDevice.cu" -o "${_rtx_out}/RtxDevice.optixir"
            DEPENDS src/rtx/RtxDevice.cu src/rtx/RtxShared.h
            COMMENT "nvcc: RTX device programs (OptiX-IR)" VERBATIM)
        add_custom_command(OUTPUT "${_rtx_out}/RtxKernels.ptx"
            COMMAND "${_rtx_cuda}/bin/nvcc.exe" --ptx ${_rtx_flags}
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/rtx/RtxKernels.cu" -o "${_rtx_out}/RtxKernels.ptx"
            DEPENDS src/rtx/RtxKernels.cu
            COMMENT "nvcc: RTX helper kernels (PTX)" VERBATIM)
        add_custom_command(OUTPUT "${_rtx_out}/RtxEmbedded.cpp"
            COMMAND ${CMAKE_COMMAND} -DIN_IR=${_rtx_out}/RtxDevice.optixir -DIN_PTX=${_rtx_out}/RtxKernels.ptx
                    -DOUT=${_rtx_out}/RtxEmbedded.cpp -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RtxEmbed.cmake"
            DEPENDS "${_rtx_out}/RtxDevice.optixir" "${_rtx_out}/RtxKernels.ptx" cmake/RtxEmbed.cmake
            COMMENT "Embedding the RTX device code" VERBATIM)
        target_sources(Infinite PRIVATE src/rtx/RtxOptix.cpp "${_rtx_out}/RtxEmbedded.cpp")
        target_include_directories(Infinite PRIVATE "${_rtx_cuda}/include" "${optixdev_SOURCE_DIR}/include")
        target_link_libraries(Infinite PRIVATE "${_rtx_cuda}/lib/x64/cuda.lib" delayimp.lib)
        target_link_options(Infinite PRIVATE /DELAYLOAD:nvcuda.dll)
        set(INFINITE_RTX_BUILT ON)
        message(STATUS "RTX render mode: ON (CUDA ${_rtx_cuda}, OptiX 7.7 headers)")
    else()
        message(WARNING "RTX render mode: no CUDA Toolkit with nvcc found (or the OptiX headers did not download) - "
                        "building without it. Install the CUDA Toolkit or set INFINITE_CUDA_ROOT.")
    endif()
endif()
if(INFINITE_ENABLE_RENDER_HQ)
    # ---- Render 3D HQ (Turbo 0.71) -------------------------------------------
    # Its renderer is a module: without it, HqStub.cpp answers and the node
    # draws as Render 3D. Shaders are shaders/hq_*.glsl (embedded below).
    target_sources(Infinite PRIVATE src/nodes/render_hq/HqRenderer.cpp src/nodes/render_hq/HqPost.cpp)
    message(STATUS "Render 3D HQ: ON")
else()
    target_sources(Infinite PRIVATE src/nodes/render_hq/HqStub.cpp)
    message(STATUS "Render 3D HQ: OFF (the node draws as Render 3D)")
endif()
if(NOT INFINITE_RTX_BUILT)
    target_sources(Infinite PRIVATE src/nodes/RtxStub.cpp)
endif()
target_include_directories(Infinite PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(Infinite PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(Infinite PRIVATE
    OpenGL::GL
    GLEW::GLEW
    ${FFMPEG_LIBRARIES}
    ${OpenCV_LIBS}
    assimp::assimp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
    avrt d3d11 d3d12 dxgi dwmapi mf mfplat mfreadwrite mfuuid shlwapi version winmm ws2_32 windowscodecs ole32 shell32 comdlg32 gdiplus
)
if(INFINITE_HAS_WINDOWS_ML)
    target_include_directories(Infinite PRIVATE "${INFINITE_WINDOWSML_ORT_INCLUDE_DIR}")
    target_link_libraries(Infinite PRIVATE WindowsML::OnnxRuntime WindowsML::DirectML)
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:Infinite> $<TARGET_FILE_DIR:Infinite>
        COMMAND_EXPAND_LISTS
        VERBATIM)

    # DirectML.dll has no import library, so CMake does not include it in
    # TARGET_RUNTIME_DLLS. Copy the x64 runtime explicitly.
    file(GLOB_RECURSE INFINITE_DIRECTML_DLLS LIST_DIRECTORIES FALSE
        "${INFINITE_WINDOWSML_ROOT}/runtimes/win-x64/native/DirectML.dll"
        "${INFINITE_WINDOWSML_ROOT}/**/x64/DirectML.dll")
    list(LENGTH INFINITE_DIRECTML_DLLS INFINITE_DIRECTML_DLL_COUNT)
    if(INFINITE_DIRECTML_DLL_COUNT EQUAL 0)
        message(FATAL_ERROR "DirectML.dll was not found inside '${INFINITE_WINDOWSML_ROOT}'.")
    endif()
    list(GET INFINITE_DIRECTML_DLLS 0 INFINITE_DIRECTML_DLL)
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${INFINITE_DIRECTML_DLL}" $<TARGET_FILE_DIR:Infinite>
        VERBATIM)
endif()
if(INFINITE_ENABLE_SPOUT)
    target_link_libraries(Infinite PRIVATE InfiniteSpout2)
endif()
set_target_properties(Infinite PROPERTIES
    OUTPUT_NAME "Infinite-Turbo"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Infinite>")
# IBM Plex is an application asset, not a machine-level dependency. Keep
# the same relative path in local builds and packaged distributions.
add_custom_command(TARGET Infinite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/IBMPlexSans-Regular.ttf"
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts/IBMPlexSans-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL.txt"
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts/OFL.txt"
    # Turbo 0.51: optional interface font (Preferences), OFL licensed.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Regular.ttf"
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts/AtkinsonHyperlegible-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-AtkinsonHyperlegible.txt"
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts/OFL-AtkinsonHyperlegible.txt"
    # Turbo 0.57: Inter, the font of the "Infinite Dark" theme (upstream's design system), OFL licensed.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts/Inter-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Inter.txt"
        "$<TARGET_FILE_DIR:Infinite>/assets/fonts/OFL-Inter.txt"
    # Turbo 0.46: the example patch Infinite-Turbo opens with.
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Infinite>/assets/examples"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/examples/superSynthMCP.inf"
        "$<TARGET_FILE_DIR:Infinite>/assets/examples/superSynthMCP.inf"
    # Turbo 0.47: the drum kit the Drum Sequencer's pattern library loads.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits"
        "$<TARGET_FILE_DIR:Infinite>/assets/drumkits"
    # Turbo 0.59: example projects (FILE > Examples), shipped with every release.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "$<TARGET_FILE_DIR:Infinite>/examples"
    VERBATIM)
# Turbo 0.54: bundled MIDI pattern packs (both builds) and, in the full build
# only, SLOOP's kits rendered to samples (GPL-3.0 material, src/gpl/README.md).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/drum-patterns")
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/drum-patterns"
            "$<TARGET_FILE_DIR:Infinite>/assets/drum-patterns"
        VERBATIM)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/note-patterns")
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/note-patterns"
            "$<TARGET_FILE_DIR:Infinite>/assets/note-patterns"
        VERBATIM)
endif()
if(INFINITE_ENABLE_GPL)
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits-gpl"
            "$<TARGET_FILE_DIR:Infinite>/assets/drumkits-gpl"
        VERBATIM)
endif()
# The upstream codebase stores UTF-8 paths in std::string. Preserve that
# ABI while using C++20 for the Windows ML headers.
target_compile_options(Infinite PRIVATE /utf-8 /bigobj /permissive- /Zc:char8_t- /Zc:__cplusplus /MP)
if(TARGET infinite-vst3-scanner)
    target_compile_options(infinite-vst3-scanner PRIVATE /MP)
endif()

# ---- ASIO (Turbo) -----------------------------------------------------------
# Steinberg's ASIO SDK has been GPLv3 since 2.3.4 (Oct 2025), the same terms
# the VST3 build is already under. JUCE only needs common/iasiodrv.h. The zip
# is fetched once into the build tree; if the download fails the build goes
# on without ASIO (WASAPI shared / exclusive stay available) and says so.
if(INFINITE_ENABLE_ASIO)
    set(INFINITE_ASIO_DIR "${CMAKE_BINARY_DIR}/_deps/asiosdk")
    if(NOT EXISTS "${INFINITE_ASIO_DIR}/common/iasiodrv.h")
        set(_asio_zip "${CMAKE_BINARY_DIR}/_deps/asiosdk-2.3.4.zip")
        set(_asio_sha512 "57de2c0cd0df0783275987e08255abfa49da12982f9d462ac40b7f57300c36e024dcb65d100b799fb3c96a9c7c5ee86e61ceb0e68d2839324206c1629d3905ed")
        if(NOT EXISTS "${_asio_zip}")
            message(STATUS "Downloading Steinberg ASIO SDK 2.3.4")
            file(DOWNLOAD "https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip"
                 "${_asio_zip}" STATUS _asio_status TLS_VERIFY ON)
            list(GET _asio_status 0 _asio_code)
            if(NOT _asio_code EQUAL 0)
                file(REMOVE "${_asio_zip}")
            endif()
        endif()
        if(EXISTS "${_asio_zip}")
            file(SHA512 "${_asio_zip}" _asio_hash)
            if(_asio_hash STREQUAL _asio_sha512)
                set(_asio_tmp "${CMAKE_BINARY_DIR}/_deps/asiosdk-extract")
                file(REMOVE_RECURSE "${_asio_tmp}")
                file(ARCHIVE_EXTRACT INPUT "${_asio_zip}" DESTINATION "${_asio_tmp}")
                file(GLOB_RECURSE _asio_hdr "${_asio_tmp}/*/iasiodrv.h" "${_asio_tmp}/common/iasiodrv.h")
                if(_asio_hdr)
                    list(GET _asio_hdr 0 _asio_first)
                    get_filename_component(_asio_common "${_asio_first}" DIRECTORY)
                    get_filename_component(_asio_root "${_asio_common}" DIRECTORY)
                    file(REMOVE_RECURSE "${INFINITE_ASIO_DIR}")
                    file(RENAME "${_asio_root}" "${INFINITE_ASIO_DIR}")
                endif()
                file(REMOVE_RECURSE "${_asio_tmp}")
            else()
                message(WARNING "ASIO SDK checksum mismatch, ignoring the download")
                file(REMOVE "${_asio_zip}")
            endif()
        endif()
    endif()
    if(EXISTS "${INFINITE_ASIO_DIR}/common/iasiodrv.h")
        target_include_directories(Infinite PRIVATE "${INFINITE_ASIO_DIR}/common")
        target_compile_definitions(Infinite PRIVATE JUCE_ASIO=1)
        message(STATUS "ASIO support: ON (${INFINITE_ASIO_DIR})")
    else()
        message(WARNING "ASIO SDK not available (download failed?) - building without ASIO. "
                        "Delete build/windows-vs2022/_deps/asiosdk* and reconfigure to retry.")
    endif()
endif()

# Release codegen. /O2 is CMake's default; add intrinsics, favour speed,
# function-level linking, no buffer-security checks in the hot loops, and
# optionally AVX2 / whole-program optimization.
target_compile_options(Infinite PRIVATE $<$<CONFIG:Release>:/Oi /Ot /Gy /GS->)
target_link_options(Infinite PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
# Turbo 0.49: a PDB next to the Release exe (same optimised code), so a crash
# minidump from %LOCALAPPDATA%\Infinite\crash can be read with function names.
# The PDB is not packaged.
target_compile_options(Infinite PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(Infinite PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)
if(INFINITE_ENABLE_AVX2)
    target_compile_options(Infinite PRIVATE /arch:AVX2)
endif()
if(INFINITE_ENABLE_LTCG)
    target_compile_options(Infinite PRIVATE $<$<CONFIG:Release>:/GL>)
    target_link_options(Infinite PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# Turbo 0.70 (F2): Tracy profiler client (BSD-3, both editions), off by default.
# On demand: nothing is recorded until the Tracy GUI (same version) connects.
# Zones: frame stages, PointZ cooks and kernels, Render 3D, post, 3D previews
# (src/core/Profile.h).
if(INFINITE_ENABLE_TRACY)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(tracy)
    target_link_libraries(Infinite PRIVATE TracyClient)
    target_compile_definitions(Infinite PRIVATE INFINITE_TRACY=1)
endif()

# Turbo 0.70 (A5, R5): GLSL from shaders/ compiled into the exe (EmbeddedFile, see
# cmake/EmbedFiles.cmake), with AMD FidelityFX FSR 1's two headers (MIT, both editions):
# Render 3D's FSR upscale includes them in its shaders and computes their constants on
# the CPU. Fetched as files at configure time, like doctest below.
FetchContent_Declare(ffx_a
    URL https://raw.githubusercontent.com/GPUOpen-Effects/FidelityFX-FSR/v1.0.2/ffx-fsr/ffx_a.h
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_Declare(ffx_fsr1
    URL https://raw.githubusercontent.com/GPUOpen-Effects/FidelityFX-FSR/v1.0.2/ffx-fsr/ffx_fsr1.h
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(ffx_a ffx_fsr1)
file(GLOB INFINITE_SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
set(_embed_files ${INFINITE_SHADER_FILES} "${ffx_a_SOURCE_DIR}/ffx_a.h" "${ffx_fsr1_SOURCE_DIR}/ffx_fsr1.h")
string(JOIN "|" _embed_arg ${_embed_files})
set(_embed_out "${CMAKE_BINARY_DIR}/generated/EmbeddedFiles.cpp")
add_custom_command(OUTPUT "${_embed_out}"
    COMMAND ${CMAKE_COMMAND} "-DFILES=${_embed_arg}" "-DOUT=${_embed_out}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
    DEPENDS ${_embed_files} cmake/EmbedFiles.cmake
    COMMENT "Embedding shaders" VERBATIM)
target_sources(Infinite PRIVATE "${_embed_out}")
target_include_directories(Infinite PRIVATE "${ffx_a_SOURCE_DIR}" "${ffx_fsr1_SOURCE_DIR}")

# Turbo 0.70 (A7): unit tests (doctest, MIT) for code that needs no window and no
# GL context, starting with the patch reader / writer. Not part of the normal
# build; test-windows.bat builds and runs it (UNITTEST). doctest is one header,
# fetched as a file (its own CMakeLists predates CMake 4's minimum).
FetchContent_Declare(doctest
    URL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(doctest)
add_executable(InfiniteUnitTests EXCLUDE_FROM_ALL
    tests/unit/UnitMain.cpp
    tests/unit/PatchTests.cpp
    tests/unit/MeshTests.cpp # Turbo 0.71 (S1)
    src/core/Patch.cpp
    src/core/Mesh.cpp)
target_include_directories(InfiniteUnitTests PRIVATE src src/core "${doctest_SOURCE_DIR}")
target_compile_definitions(InfiniteUnitTests PRIVATE NOMINMAX _USE_MATH_DEFINES)
target_compile_options(InfiniteUnitTests PRIVATE /utf-8 /permissive- /Zc:char8_t- /Zc:__cplusplus)

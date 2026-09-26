cmake_minimum_required(VERSION 3.25)

# Infinite-Turbo (for Windows)
# Unofficial Windows-only fork of Infinite (https://github.com/n1m21n/Infinite).
# macOS support was removed on purpose: every platform path below is Windows
# (MSVC 2022, x64, vcpkg static triplet, JUCE, OpenCV, Spout2, Windows ML).
project(InfiniteTurbo VERSION 0.40.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Infinite-Turbo builds only on Windows x64 with MSVC (Visual Studio 2022).")
endif()

# Windows ML's native C/C++ package requires C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(INFINITE_ENABLE_VST3 "Build the VST3 plugin backend (JUCE)" ON)
option(INFINITE_ENABLE_SPOUT "Build Spout input/output support" ON)
option(INFINITE_ENABLE_DIRECTML_MATTING "Accelerate Remove Background with Windows ML / DirectML" ON)
option(INFINITE_ENABLE_AVX2 "Compile with /arch:AVX2 (any x64 CPU from ~2013 on)" ON)
option(INFINITE_ENABLE_LTCG "Whole-program optimization (/GL + /LTCG) in Release; slower link" OFF)

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
    src/platform/PluginVST3Windows.cpp
    src/audio/AudioFileWriterWindows.cpp
    src/nodes/TextNodeWindows.cpp
    cmake/InfiniteWindows.rc)

add_executable(Infinite WIN32
    src/main.cpp
    src/core/GLUtil.cpp
    src/core/CurveShape.cpp
    src/core/NodeFactory.cpp
    src/core/CategoryColors.cpp
    src/core/FilterDefs.cpp
    src/core/BlendModes.cpp
    src/core/Mesh.cpp
    src/core/NodeViewport.cpp
    src/core/Transport.cpp
    src/core/Modulation.cpp
    src/core/Expression.cpp
    src/core/ExprGlobals.cpp
    src/core/Palette.cpp
    src/core/Patch.cpp
    src/core/RemoteControl.cpp
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
    src/nodes/DriftNode.cpp
    src/nodes/AudioMeterNode.cpp
    src/nodes/SlideshowNode.cpp
    src/audio/dsp/CycleShaperKernel.cpp
    src/audio/dsp/ResonatorBankKernel.cpp
    src/audio/dsp/SpecBlurKernel.cpp
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
    src/nodes/EnvironmentNode.cpp
    src/nodes/ModelSourceNode.cpp
    src/nodes/Text3DNode.cpp
    src/nodes/UtilityNodes.cpp
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
    src/nodes/CurveNode.cpp
    src/nodes/OutputNode.cpp
    src/nodes/SyphonInNode.cpp
    src/nodes/SyphonOutNode.cpp
    src/nodes/ProjectionNode.cpp
    src/nodes/AudioNodes.cpp
    src/nodes/NoteNodes.cpp
    src/nodes/WavetableNode.cpp
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
    src/nodes/AudioPluginNode.cpp
    src/nodes/LooperNode.cpp
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

# GUI subsystem without a console window, keeping the portable main().
target_link_options(Infinite PRIVATE "/ENTRY:mainCRTStartup")

target_include_directories(Infinite PRIVATE
    src src/core src/nodes src/platform src/audio
    ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp
    ${NODE_EDITOR_DIR}
    ${STB_DIR}
    external/json
    external/shine
)

target_link_libraries(Infinite PRIVATE glfw strmiids)

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
        ole32 shell32 shlwapi version winmm ws2_32
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
    INFINITE_HAS_WINDOWS_ML=${INFINITE_HAS_WINDOWS_ML}
)
target_link_libraries(Infinite PRIVATE
    OpenGL::GL
    GLEW::GLEW
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
    avrt d3d11 d3d12 dxgi dwmapi mfplat mfreadwrite mfuuid shlwapi version winmm ws2_32 windowscodecs ole32 shell32 comdlg32 gdiplus
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
    VERBATIM)
# The upstream codebase stores UTF-8 paths in std::string. Preserve that
# ABI while using C++20 for the Windows ML headers.
target_compile_options(Infinite PRIVATE /utf-8 /bigobj /permissive- /Zc:char8_t- /Zc:__cplusplus /MP)
if(TARGET infinite-vst3-scanner)
    target_compile_options(infinite-vst3-scanner PRIVATE /MP)
endif()

# Release codegen. /O2 is CMake's default; add intrinsics, favour speed,
# function-level linking, no buffer-security checks in the hot loops, and
# optionally AVX2 / whole-program optimization.
target_compile_options(Infinite PRIVATE $<$<CONFIG:Release>:/Oi /Ot /Gy /GS->)
target_link_options(Infinite PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
if(INFINITE_ENABLE_AVX2)
    target_compile_options(Infinite PRIVATE /arch:AVX2)
endif()
if(INFINITE_ENABLE_LTCG)
    target_compile_options(Infinite PRIVATE $<$<CONFIG:Release>:/GL>)
    target_link_options(Infinite PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

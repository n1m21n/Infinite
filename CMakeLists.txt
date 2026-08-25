cmake_minimum_required(VERSION 3.16)

# Universal + a floor of macOS 11: UTType (file dialogs) and the modern
# AVAssetWriter APIs need 11.0, and shipping both slices means the DMG runs on
# Intel Macs as well as Apple Silicon. Must be set before project().
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(Infinite C CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

if(WIN32)
    # Windows ML's native C/C++ package requires C++20.
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# GLFW is built from source and linked statically rather than taken from
# Homebrew: a downloaded .app must not depend on /opt/homebrew existing.
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

# Optional VST3 plugin hosting backend. Off by default: the Steinberg VST3
# SDK is GPLv3-or-proprietary, and linking it makes the distributed binary
# GPLv3 too (see LICENSE). Do not flip this default without the user's
# explicit sign-off - it's a licensing decision, not a technical one.
if(WIN32)
    option(INFINITE_ENABLE_VST3 "Build the VST3 plugin backend" ON)
    option(INFINITE_ENABLE_SPOUT "Build Spout input/output support" ON)
    option(INFINITE_ENABLE_DIRECTML_MATTING "Accelerate Remove Background with Windows ML / DirectML" ON)
else()
    option(INFINITE_ENABLE_VST3 "Build the GPLv3 VST3 plugin backend" OFF)
endif()

if(WIN32)
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
endif()

set(IMGUI_DIR external/imgui)
set(NODE_EDITOR_DIR external/imgui-node-editor)
set(STB_DIR external/stb)
set(SYPHON_DIR external/syphon)

if(INFINITE_ENABLE_VST3 AND APPLE)
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")
    if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
        message(FATAL_ERROR "INFINITE_ENABLE_VST3 is ON, but Steinberg VST3 SDK was not found in external/vst3sdk.\n"
                            "Please initialize submodules with:\n"
                            "  git submodule update --init --recursive external/vst3sdk\n"
                            "or clone the VST3 SDK into external/vst3sdk.")
    endif()
endif()

if(APPLE)
set(SYPHON_SOURCES
    ${SYPHON_DIR}/src/SyphonCGL.c
    ${SYPHON_DIR}/src/SyphonDispatch.c
    ${SYPHON_DIR}/src/SyphonOpenGLFunctions.c
    ${SYPHON_DIR}/src/SyphonCFMessageReceiver.m
    ${SYPHON_DIR}/src/SyphonCFMessageSender.m
    ${SYPHON_DIR}/src/SyphonClient.m
    ${SYPHON_DIR}/src/SyphonClientBase.m
    ${SYPHON_DIR}/src/SyphonClientConnectionManager.m
    ${SYPHON_DIR}/src/SyphonGLShader.m
    ${SYPHON_DIR}/src/SyphonGLVertices.m
    ${SYPHON_DIR}/src/SyphonIOSurfaceImageCore.m
    ${SYPHON_DIR}/src/SyphonIOSurfaceImageLegacy.m
    ${SYPHON_DIR}/src/SyphonImage.m
    ${SYPHON_DIR}/src/SyphonImageBase.m
    ${SYPHON_DIR}/src/SyphonMessageQueue.m
    ${SYPHON_DIR}/src/SyphonMessageReceiver.m
    ${SYPHON_DIR}/src/SyphonMessageSender.m
    ${SYPHON_DIR}/src/SyphonMessaging.m
    ${SYPHON_DIR}/src/SyphonOpenGLClient.m
    ${SYPHON_DIR}/src/SyphonOpenGLImage.m
    ${SYPHON_DIR}/src/SyphonOpenGLServer.m
    ${SYPHON_DIR}/src/SyphonPrivate.m
    ${SYPHON_DIR}/src/SyphonServer.m
    ${SYPHON_DIR}/src/SyphonServerBase.m
    ${SYPHON_DIR}/src/SyphonServerConnectionManager.m
    ${SYPHON_DIR}/src/SyphonServerDirectory.m
    ${SYPHON_DIR}/src/SyphonServerGLShader.m
    ${SYPHON_DIR}/src/SyphonServerGLVertices.m
    ${SYPHON_DIR}/src/SyphonServerRendererCoreGL.m
    ${SYPHON_DIR}/src/SyphonServerRendererGL.m
    ${SYPHON_DIR}/src/SyphonServerRendererLegacyGL.m
)
else()
set(SYPHON_SOURCES)
endif()

if(APPLE)
    set(INFINITE_PLATFORM_SOURCES
        src/platform/Platform.mm
        src/audio/AudioFileWriter.mm
        src/nodes/TextNode.cpp)
elseif(WIN32)
    set(INFINITE_PLATFORM_SOURCES
        src/platform/PlatformWindows.cpp
        src/platform/PluginVST3Windows.cpp
        src/audio/AudioFileWriterWindows.cpp
        src/nodes/TextNodeWindows.cpp
        cmake/InfiniteWindows.rc)
else()
    message(FATAL_ERROR "Infinite currently supports macOS and Windows only")
endif()

add_executable(Infinite MACOSX_BUNDLE
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
    src/audio/SampleScanner.cpp
    src/audio/PluginScanner.cpp

    ${SYPHON_SOURCES}

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

if(WIN32)
    # Ship as a GUI application without the extra terminal window while
    # preserving the existing portable main(argc, argv) entry point.
    set_target_properties(Infinite PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(Infinite PRIVATE "/ENTRY:mainCRTStartup")
endif()

target_include_directories(Infinite PRIVATE
    src src/core src/nodes src/platform src/audio
    ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp
    ${NODE_EDITOR_DIR}
    ${STB_DIR}
    external/json
    external/shine
    ${SYPHON_DIR}/include
    ${SYPHON_DIR}/src
)

target_link_libraries(Infinite PRIVATE glfw)

if(WIN32)
    target_link_libraries(Infinite PRIVATE strmiids)
endif()

if(APPLE)
    set_source_files_properties(src/platform/Platform.mm src/audio/AudioFileWriter.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(${SYPHON_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-include;${CMAKE_CURRENT_SOURCE_DIR}/${SYPHON_DIR}/src/Syphon_Prefix.pch")
endif()

if(INFINITE_ENABLE_VST3)
    if(APPLE)
        target_compile_definitions(Infinite PRIVATE INFINITE_ENABLE_VST3=1)
        target_include_directories(Infinite PRIVATE
            ${VST3_SDK_DIR}
            ${VST3_SDK_DIR}/pluginterfaces
            ${VST3_SDK_DIR}/public.sdk
        )
        target_sources(Infinite PRIVATE
            src/platform/PluginVST3.mm
            ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
            ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
            ${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp
        )
        set_source_files_properties(src/platform/PluginVST3.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

        add_executable(infinite-vst3-scanner
        src/scanner_main.mm
        src/platform/PluginVST3.mm
        src/platform/Platform.mm
        ${SYPHON_SOURCES}
        ${NODE_EDITOR_DIR}/crude_json.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp
    )
    target_include_directories(infinite-vst3-scanner PRIVATE
        src src/core src/nodes src/platform src/audio
        ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp
        ${NODE_EDITOR_DIR}
        ${STB_DIR}
        external/json
        external/shine
        ${SYPHON_DIR}/include
        ${SYPHON_DIR}/src
        ${VST3_SDK_DIR}
        ${VST3_SDK_DIR}/pluginterfaces
        ${VST3_SDK_DIR}/public.sdk
    )
    target_compile_definitions(infinite-vst3-scanner PRIVATE
        INFINITE_ENABLE_VST3=1
        GL_SILENCE_DEPRECATION
        INFINITE_VST3_SCANNER=1
    )
    set_source_files_properties(src/scanner_main.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

        target_link_libraries(infinite-vst3-scanner PRIVATE
            "-framework Cocoa"
            "-framework CoreFoundation"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AVFoundation"
            "-framework CoreAudioKit"
            "-framework AppKit"
            "-framework CoreMedia"
            "-framework CoreMIDI"
            "-framework CoreVideo"
            "-framework CoreGraphics"
            "-framework ImageIO"
            "-framework UniformTypeIdentifiers"
            "-framework Accelerate"
            "-framework ModelIO"
            "-framework Vision"
            "-framework OpenGL"
            "-framework IOSurface"
        )
        target_link_options(infinite-vst3-scanner PRIVATE
            "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/cmake/Scanner-Info.plist"
        )
        add_dependencies(Infinite infinite-vst3-scanner)
        add_custom_command(TARGET Infinite POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "$<TARGET_FILE:infinite-vst3-scanner>"
                    "$<TARGET_BUNDLE_DIR:Infinite>/Contents/MacOS/infinite-vst3-scanner"
            COMMENT "Copying scanner helper into app bundle")
    elseif(WIN32)
        # Scan third-party DLLs in a deliberately tiny helper process. Launching
        # another copy of the full Infinite GUI here made application startup
        # failures indistinguishable from bad plugins and poisoned the blocklist.
        add_executable(infinite-vst3-scanner
            src/scanner_main_windows.cpp
            src/platform/PluginVST3Windows.cpp
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
endif()

# imgui-node-editor 0.9.1 ships its own GetKeyIndex() shim, which collides with
# ImGui's obsolete-keyio copy. Dropping the obsolete API leaves only the shim.
target_compile_definitions(Infinite PRIVATE
    IMGUI_DISABLE_OBSOLETE_KEYIO
    IMGUI_DEFINE_MATH_OPERATORS
)

if(WIN32)
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
        d3d11 d3d12 dxgi dwmapi mfplat mfreadwrite mfuuid shlwapi version winmm ws2_32 windowscodecs ole32 shell32 comdlg32 gdiplus
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
        OUTPUT_NAME "Infinite"
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
    target_compile_options(Infinite PRIVATE /utf-8 /bigobj /permissive- /Zc:char8_t-)
endif()

if(APPLE)
    target_link_libraries(Infinite PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework AppKit"
        "-framework UniformTypeIdentifiers"
        "-framework AVFoundation"
        "-framework Vision"
        "-framework ModelIO"
        "-framework Accelerate"
        "-framework CoreMedia"
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework IOSurface"
        # Plugin hosting: AUAudioUnit's -requestViewControllerWithCompletionHandler:
        # lives in CoreAudioKit (AUViewController.h), as does the
        # AUGenericViewController fallback for plugins with no editor of their
        # own. See Platform.h's audio plugin hosting section.
        "-framework CoreAudioKit"
    )
    target_compile_definitions(Infinite PRIVATE GL_SILENCE_DEPRECATION)

    set_target_properties(Infinite PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Infinite"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.namansoni.infinite"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_ICON_FILE "Infinite"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns"
    )

    # Rebuilding the executable inside a signed bundle invalidates the
    # signature, and an unsigned bundle hangs in glfwInit() on macOS rather
    # than failing loudly. Re-sign on every build so iterating never trips it.
    # The entitlements are not optional once plugins are hosted: see
    # cmake/Infinite.entitlements for which ones and why.
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND codesign --force --deep --sign -
                --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Infinite.entitlements"
                "$<TARGET_BUNDLE_DIR:Infinite>"
        COMMENT "Ad-hoc signing the bundle")

    target_sources(Infinite PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/InfinitePatch.icns")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/InfinitePatch.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

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

# Objective-C/Objective-C++ are Apple-only languages; the Windows port keeps
# the same source tree but swaps the platform layer for src/platform/win.
project(Infinite LANGUAGES C CXX)

# Single source of truth for the app version - drives the macOS bundle
# properties, the Windows .rc version block, and the in-app update checker's
# "am I current" comparison. Bump this (and INFINITE_VERSION_RC below it,
# kept in sync by hand) when cutting a release; nothing else should hardcode
# a version string.
set(INFINITE_VERSION "0.2.76")
# VERSIONINFO (src/platform/win/Infinite.rc.in) wants four dot/comma-separated
# numeric components but INFINITE_VERSION is three; kept as an explicit
# second value rather than derived, so bump both together when cutting a
# release.
set(INFINITE_VERSION_RC "0,2,76,0")
string(REPLACE "," "." INFINITE_VERSION_RC_STR "${INFINITE_VERSION_RC}")

if(APPLE)
    enable_language(OBJC OBJCXX)
elseif(WIN32)
    # Needed to compile src/platform/win/Infinite.rc (app icon + version
    # block). Harmless when no .rc is in the target's sources.
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the MSVC runtime statically so a downloaded Infinite.exe runs on a
# clean Windows install. The default (/MD) makes the exe depend on
# MSVCP140.dll + VCRUNTIME140.dll from the Visual C++ redistributable, which
# is NOT present on a stock Windows image - the failure is a launch-time
# "MSVCP140.dll was not found" dialog, which is exactly the trap a
# single-file portable build should not have. Same reasoning as building GLFW
# from source above: a downloaded build must not depend on something else
# being installed first.
#
# This must be set before the FetchContent_MakeAvailable calls below: every
# target in the build has to agree on the CRT or the link fails with
# mismatched _ITERATOR_DEBUG_LEVEL / RuntimeLibrary errors, and glfw and
# libFLAC are both built from source here. CMP0091 (NEW as of the 3.16
# minimum above) is what makes this variable authoritative instead of the
# old flag-munging approach.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/Zc:preprocessor)

    # One cl.exe invocation per translation unit instead of the Visual Studio
    # generator's default of batching every source in a target into a single
    # call. The batched form ends in a combined "Generating Code..." codegen
    # pass, and that pass hit a reproducible internal compiler error on x64
    # (MSVC 19.44) with no C1001 and no source location - so it was neither
    # fixable nor attributable to a file. Unbatched, each TU stands alone:
    # any future ICE names its own .cpp. Needs CMake 3.24; the variable is
    # simply ignored by older versions, so the guard is documentation only.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        set(CMAKE_VS_NO_COMPILE_BATCHING ON)
    endif()
endif()

# GLFW is built from source and linked statically rather than taken from
# Homebrew: a downloaded .app must not depend on /opt/homebrew existing. (On
# Windows the same reasoning applies to any vcpkg/system copy.)
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

# FLAC writing on Windows: dr_libs is decode-only, so the file writer links
# the real libFLAC instead (static, no Ogg). macOS keeps CoreAudio's
# ExtAudioFile and never compiles this in.
if(WIN32)
    set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
    set(WITH_OGG OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        flac
        GIT_REPOSITORY https://github.com/xiph/flac.git
        GIT_TAG 1.5.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(flac)

    # ONNX Runtime + DirectML EP, for Platform::SubjectMask's Windows
    # implementation (src/platform/win/PlatformWin.cpp) - on-device background
    # removal since Windows has no equivalent to Apple's Vision framework.
    #
    # Fetched as prebuilt NuGet packages rather than built from source: ONNX
    # Runtime is a very large build, and a .nupkg is just a zip, downloadable
    # straight from nuget.org without the nuget/dotnet CLI. Two separate
    # packages are needed:
    #   - Microsoft.ML.OnnxRuntime.DirectML: onnxruntime.dll/.lib built with
    #     the DirectML execution provider compiled in, plus
    #     dml_provider_factory.h (OrtSessionOptionsAppendExecutionProvider_DML).
    #     The plain onnxruntime-win-x64-*.zip GitHub release asset does NOT
    #     have DirectML support - confirmed by inspecting it - so it won't do.
    #   - Microsoft.AI.DirectML: DirectML.dll itself. onnxruntime.dll dynamically
    #     imports it (confirmed via string-scanning the DLL) but does not embed
    #     it, so it's a separate runtime dependency that must ship alongside.
    # Neither package's headers are otherwise touched - we only ever call the
    # ORT C++ API plus the one DML provider-registration function, never the
    # DirectML API directly, so Microsoft.AI.DirectML contributes just the
    # .dll, nothing to link or include.
    set(ONNXRUNTIME_DML_VERSION "1.20.1")
    set(DIRECTML_VERSION "1.15.4")

    # Matches the -A argument package.ps1/build.yml pass to cmake's Visual
    # Studio generator (the only generator this Windows port supports).
    set(INFINITE_WIN_ARCH "x64")
    if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
        set(INFINITE_WIN_ARCH "arm64")
    endif()

    FetchContent_Declare(
        onnxruntime_dml
        URL "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${ONNXRUNTIME_DML_VERSION}"
        URL_HASH SHA256=6763468507b7cfc777b1334b3e174c11a540ddacb7bd4354bc2e0ec89e56eec2
        DOWNLOAD_NAME "onnxruntime-directml-${ONNXRUNTIME_DML_VERSION}.zip"
    )
    FetchContent_Declare(
        directml_redist
        URL "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${DIRECTML_VERSION}"
        URL_HASH SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9
        DOWNLOAD_NAME "directml-${DIRECTML_VERSION}.zip"
    )
    FetchContent_MakeAvailable(onnxruntime_dml directml_redist)

    # Package layout, from inspecting the .nupkg contents directly:
    #   onnxruntime_dml: build/native/include/*.h, runtimes/win-<arch>/native/onnxruntime.{dll,lib}
    #   directml_redist: bin/<arch>-win/DirectML.dll  (note the reversed arch/win order)
    set(ONNXRUNTIME_INCLUDE_DIR "${onnxruntime_dml_SOURCE_DIR}/build/native/include")
    set(ONNXRUNTIME_LIB "${onnxruntime_dml_SOURCE_DIR}/runtimes/win-${INFINITE_WIN_ARCH}/native/onnxruntime.lib")
    set(ONNXRUNTIME_DLL "${onnxruntime_dml_SOURCE_DIR}/runtimes/win-${INFINITE_WIN_ARCH}/native/onnxruntime.dll")
    set(DIRECTML_DLL "${directml_redist_SOURCE_DIR}/bin/${INFINITE_WIN_ARCH}-win/DirectML.dll")
endif()

# VST3 plugin hosting backend. On by default: the Steinberg VST3 SDK is
# GPLv3-or-proprietary, and linking it makes the distributed binary GPLv3
# too (see LICENSE). Do not flip this default without the user's explicit
# sign-off - it's a licensing decision, not a technical one.
option(INFINITE_ENABLE_VST3 "Build the GPLv3 VST3 plugin backend" ON)

set(IMGUI_DIR external/imgui)
set(NODE_EDITOR_DIR external/imgui-node-editor)
set(STB_DIR external/stb)
set(CGLTF_DIR external/cgltf)
set(SYPHON_DIR external/syphon)
# Windows counterpart to SYPHON_DIR: Spout2's "SpoutGL" library, vendored as
# plain source (not FetchContent/vcpkg) the same way SYPHON_DIR is - see
# external/spout2/VENDORED_COMMIT.txt for the exact upstream commit. SpoutGL
# is the minimal DirectX11/OpenGL-interop core (Spout.*, SpoutSender.*,
# SpoutReceiver.*, ...) without the full repo's D3D12/example/installer
# clutter that its own CMakeLists would otherwise pull in.
set(SPOUT_DIR external/spout2/SPOUTSDK/SpoutGL)

if(INFINITE_ENABLE_VST3 AND NOT APPLE AND NOT WIN32)
    message(FATAL_ERROR "INFINITE_ENABLE_VST3 is only wired up on macOS and Windows right now; "
                        "this platform's layer reports plugin hosting as unavailable.")
endif()

if(INFINITE_ENABLE_VST3)
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")
    if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
        message(FATAL_ERROR "INFINITE_ENABLE_VST3 is ON, but Steinberg VST3 SDK was not found in external/vst3sdk.\n"
                            "Please initialize submodules with:\n"
                            "  git submodule update --init --recursive external/vst3sdk\n"
                            "or clone the VST3 SDK into external/vst3sdk.")
    endif()
endif()

# ---- per-platform sources ---------------------------------------------------
#
# Everything OS-specific funnels through src/platform/Platform.h, so exactly
# one platform backend TU set is ever compiled:
#
#   macOS : Platform.mm (CoreAudio/CoreMIDI/AVFoundation/...) + AudioFileWriter.mm
#           + the Syphon library
#   Win32 : src/platform/win/*.cpp (WASAPI/WinMM/Media Foundation/GDI+) +
#           AudioFileWriter.cpp (dr_libs/shine) + the glad GL loader +
#           the SpoutGL library (Windows counterpart to Syphon)

set(COMMON_SOURCES
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
    src/core/field/FieldLex.cpp
    src/core/field/FieldParse.cpp
    src/core/field/FieldIR.cpp
    src/core/field/FieldBytecode.cpp
    src/core/field/FieldVM.cpp
    src/core/field/FieldProgramCache.cpp
    src/core/ExprGlobals.cpp
    src/core/Palette.cpp
    src/core/Patch.cpp
    src/core/RemoteControl.cpp
    src/core/PatchJson.cpp
    src/core/OscMessage.cpp
    src/core/UpdateCheck.cpp
    src/core/AudioDecodeCache.cpp
    src/core/GltfImport.cpp
    src/audio/AudioEngine.cpp
    src/audio/ParamMailbox.cpp
    src/audio/MeterRing.cpp
    src/audio/AudioCaptureRing.cpp
    external/shine/shine_mp3.c
    src/audio/AudioVoice.cpp
    src/audio/Wavetable.cpp
    src/audio/EffectDefs.cpp
    src/audio/dsp/PortableFftFixture.cpp
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
    src/audio/dsp/ResonatorBankKernel.cpp
    src/audio/dsp/CycleShaperKernel.cpp
    src/audio/dsp/SpecBlurKernel.cpp
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
    src/nodes/GeometryTableNode.cpp
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
    src/nodes/TextNode.cpp
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
    src/audio/dsp/MolderDsp.cpp
    src/audio/dsp/GrainMolderDsp.cpp
    src/nodes/OscillatorNode.cpp
    src/nodes/MetallicNode.cpp
    src/nodes/AudioEffectNode.cpp
    src/nodes/SamplerNode.cpp
    src/nodes/PaulStretchNode.cpp
    src/nodes/MolderNode.cpp
    src/nodes/GrainMolderNode.cpp
    src/nodes/GranularNode.cpp
    src/nodes/DrumSequencerNode.cpp
    src/nodes/AudioPluginNode.cpp
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

set(APPLE_SOURCES
    src/platform/Platform.mm
    src/audio/AudioFileWriter.mm
    ${SYPHON_SOURCES}
)

set(SPOUT_SOURCES
    ${SPOUT_DIR}/Spout.cpp
    ${SPOUT_DIR}/SpoutGL.cpp
    ${SPOUT_DIR}/SpoutCopy.cpp
    ${SPOUT_DIR}/SpoutDirectX.cpp
    ${SPOUT_DIR}/SpoutFrameCount.cpp
    ${SPOUT_DIR}/SpoutGLextensions.cpp
    ${SPOUT_DIR}/SpoutReceiver.cpp
    ${SPOUT_DIR}/SpoutSender.cpp
    ${SPOUT_DIR}/SpoutSenderNames.cpp
    ${SPOUT_DIR}/SpoutSharedMemory.cpp
    ${SPOUT_DIR}/SpoutUtils.cpp
)

# Generated from the .in template so the version block tracks
# INFINITE_VERSION/INFINITE_VERSION_RC instead of carrying its own literal.
# The icon path is substituted as an absolute path because the generated .rc
# lives in the build dir, not next to Infinite.ico.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win/Infinite.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Infinite.rc
    @ONLY
)

set(WIN32_SOURCES
    # Icon + version info. MSVC and MinGW both compile .rc files when the
    # RC language is enabled (see enable_language below); without this the
    # exe shows Explorer's generic application icon everywhere.
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Infinite.rc
    src/platform/win/PlatformWin.cpp
    src/platform/win/CrashHandlerWin.cpp
    # GL-only half of the Spout bridge (glad), kept out of PlatformWin.cpp's
    # TU since that file also includes SpoutGL's headers, which drag in the
    # legacy <GL/gl.h> - see SpoutGLBridge.h for why the two can't mix.
    src/platform/win/SpoutGLBridge.cpp
    src/platform/win/AudioDeviceWin.cpp
    src/platform/win/MidiWin.cpp
    src/platform/win/MediaWin.cpp
    src/platform/win/MediaDecodeWin.cpp
    src/platform/win/PluginHostWin.cpp
    src/audio/AudioFileWriter.cpp
    # zlib backend for tinyexr's EXR decode (see MediaDecodeWin.cpp).
    external/miniz.c
    # GL loader for the non-Apple branch of core/gl3.h. Must load via
    # gladLoadGL((GLADloadfunc)glfwGetProcAddress) right after context
    # creation - main.cpp does this at both glfwMakeContextCurrent sites.
    external/glad/src/gl.c
    # Windows counterpart to APPLE_SOURCES' ${SYPHON_SOURCES}, backing the
    # Syphon In/Out nodes' Platform::Syphon* surface. Split into its own
    # translation unit (rather than living in PlatformWin.cpp) because it
    # needs SpoutGL's headers, which drag in DirectX11 and the legacy
    # <GL/gl.h> - see PlatformWinSyphon.cpp for why that must stay out of
    # PlatformWin.cpp (also compiled into the GL/DX-free scanner helper).
    src/platform/win/PlatformWinSyphon.cpp
    ${SPOUT_SOURCES}
)

# MSVC's x64 code generator hits an internal compiler error on src/main.cpp at
# /O2 - no C1001, no source line, just CL.exe exiting with -529706956. ARM64
# compiles the identical file, and it only started failing once a month of
# feature work landed in this translation unit.
#
# Two cheaper theories were tested and both failed. /Ob1 (inline only where
# marked) changed nothing, so it is not inlining. Giving main()'s 10k-line
# frame loop its own function via a lambda changed nothing either - which
# makes sense in hindsight: that moved the enormous body into the lambda's
# operator() instead of shrinking it, so the largest single function the
# backend has to work on was the same size as before.
#
# So the optimiser gets switched off for this one file. If the crash lives
# anywhere in the optimiser, /Od removes it.
#
# main.cpp is ImGui drawing, the node editor and the headless fixtures - no
# per-sample audio runs here, that is all in other TUs - so this is a real but
# modest UI cost, on Windows only. Do not try to confirm this landed by
# grepping the build log for the flag - MSBuild does not echo compiler flags at
# default verbosity, and CMake does not pass this one on the command line
# anyway: it becomes a per-file <Optimization>Disabled</Optimization> in
# Infinite.vcxproj. The build workflow dumps that node so it stays checkable.
#
# The real fix is for this file to stop being 37k lines. Until then, keep this
# scoped to the one file and the one compiler that needs it.
if(MSVC)
    set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "/Od")
endif()

add_executable(Infinite ${COMMON_SOURCES})

# Exposes INFINITE_VERSION to C++ as a string literal (UpdateCheck.cpp,
# User-Agent header, etc.).
target_compile_definitions(Infinite PRIVATE INFINITE_VERSION_STRING="${INFINITE_VERSION}")

if(APPLE)
    target_sources(Infinite PRIVATE ${APPLE_SOURCES})
endif()

if(WIN32)
    target_sources(Infinite PRIVATE ${WIN32_SOURCES})
endif()

target_include_directories(Infinite PRIVATE
    src src/core src/nodes src/platform src/audio
    ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp
    ${NODE_EDITOR_DIR}
    ${STB_DIR}
    ${CGLTF_DIR}
    external
    external/json
    external/shine
    external/glad/include
)

target_link_libraries(Infinite PRIVATE glfw)

if(WIN32)
    # SpoutGL's own headers (Spout.h chain) plus sse2neon.h, which its
    # SIMD-optimised pixel copy path (SpoutCopy.h/SpoutSenderNames.h) includes
    # only under _M_ARM64 - needed for the arm64 leg of the CI build even
    # though this repo has no other ARM NEON code.
    target_include_directories(Infinite PRIVATE
        ${SPOUT_DIR}
        external/sse2neon
    )

    # System libraries behind the Windows platform layer: WASAPI/MMDevice ride
    # on ole32+mfplat, MIDI on winmm, Media Foundation decode/muxing on
    # mfreadwrite+mfuuid, dialogs/text on shell32+gdiplus. Most of our TUs also
    # carry matching #pragma comment(lib) lines; this list is the belt to that
    # suspenders so MinGW-style toolchains link too.
    target_link_libraries(Infinite PRIVATE
        opengl32 winmm ole32 shell32 gdi32 user32 advapi32
        gdiplus dwmapi
        # avrt carries MMCSS (AvSetMmThreadCharacteristics), which the WASAPI
        # render thread needs to be scheduled against its buffer deadline
        # rather than as an ordinary thread.
        avrt
        # mfplat/mfreadwrite cover decode+muxing; mfuuid carries the MF GUIDs;
        # mf carries camera device activation (MFEnumDeviceSources /
        # MFCreateDeviceSource).
        mfplat mfreadwrite mfuuid mf
        FLAC::FLAC
        # WinHTTP backs Platform::HttpGet (UpdateCheck's GitHub Releases
        # request) - not otherwise linked anywhere in this target.
        winhttp
        "${ONNXRUNTIME_LIB}"
        # d3d11 + dxgi back SpoutGL's DirectX11 shared-texture path
        # (SpoutDirectX.cpp); dxgi also has its own #pragma comment(lib) but
        # MinGW-style toolchains don't process those, same reasoning as above.
        d3d11 dxgi
        # dbghelp backs Platform::InstallCrashHandler's MiniDumpWriteDump call
        # (CrashHandlerWin.cpp).
        dbghelp
    )
    target_include_directories(Infinite PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    # onnxruntime.dll, DirectML.dll (its own runtime dependency - see the
    # FetchContent comment above) and the bundled model weights all need to
    # sit next to Infinite.exe to run, exactly like infinite-vst3-scanner.exe
    # below. package.ps1 and .github/workflows/build.yml duplicate this same
    # copy for the distributed zip.
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}"
                "${DIRECTML_DLL}"
                "$<TARGET_FILE_DIR:Infinite>"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/assets/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/u2netp.onnx"
                "$<TARGET_FILE_DIR:Infinite>/assets/models/u2netp.onnx"
        COMMENT "Copying onnxruntime.dll, DirectML.dll and the u2netp model next to Infinite.exe")
    # M_PI etc. live behind _USE_MATH_DEFINES in MSVC's math.h; defining on the
    # command line lands before any cmath include.
    target_compile_definitions(Infinite PRIVATE
        _USE_MATH_DEFINES
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        IMGUI_DISABLE_OBSOLETE_KEYIO
        IMGUI_DEFINE_MATH_OPERATORS
    )
    # GUI-subsystem executable that still uses plain main(): MSVC's default
    # WinMain entry doesn't exist here, mainCRTStartup forwards to main().
    set_target_properties(Infinite PROPERTIES WIN32_EXECUTABLE TRUE)

    if(INFINITE_ENABLE_VST3)
        # Windows VST3 hosting: the portable SDK-hosting logic
        # (src/platform/win/PluginVST3Win.cpp) also hosts out-of-process
        # scanning + crash-safety (sentinel/blocklist under %APPDATA%\Infinite),
        # mirroring macOS. No codesign step is needed here, since Windows has
        # no equivalent requirement.
        target_compile_definitions(Infinite PRIVATE INFINITE_ENABLE_VST3=1)
        target_include_directories(Infinite PRIVATE
            ${VST3_SDK_DIR}
            ${VST3_SDK_DIR}/pluginterfaces
            ${VST3_SDK_DIR}/public.sdk
        )
        target_sources(Infinite PRIVATE
            src/platform/win/PluginVST3Win.cpp
            ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
            ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
            ${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp
        )

        # Out-of-process scanner helper, mirroring the macOS
        # infinite-vst3-scanner target below: a disposable console-subsystem
        # process that loads one plugin bundle at a time so a crashing or
        # hanging plugin only costs a dead child, never the app itself.
        add_executable(infinite-vst3-scanner
            src/scanner_main_win.cpp
            src/platform/win/PluginVST3Win.cpp
            src/platform/win/PlatformWin.cpp
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
            ${VST3_SDK_DIR}
            ${VST3_SDK_DIR}/pluginterfaces
            ${VST3_SDK_DIR}/public.sdk
            "${ONNXRUNTIME_INCLUDE_DIR}"
        )
        target_compile_definitions(infinite-vst3-scanner PRIVATE
            INFINITE_ENABLE_VST3=1
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _CRT_SECURE_NO_WARNINGS
            INFINITE_VST3_SCANNER=1
        )
        # Console subsystem, not WIN32_EXECUTABLE: this process only ever
        # talks to its parent over a stdout pipe (see RunProbeChild in
        # PluginVST3Win.cpp), so it needs plain main()/console CRT startup,
        # not the GUI subsystem Infinite.exe uses.
        target_link_libraries(infinite-vst3-scanner PRIVATE
            glfw "${ONNXRUNTIME_LIB}" ole32 shell32 gdi32 user32 advapi32 winhttp
        )
        add_dependencies(Infinite infinite-vst3-scanner)
        add_custom_command(TARGET Infinite POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "$<TARGET_FILE:infinite-vst3-scanner>"
                    "$<TARGET_FILE_DIR:Infinite>/infinite-vst3-scanner.exe"
            COMMENT "Copying scanner helper next to Infinite.exe")
    endif()
    target_link_options(Infinite PRIVATE /ENTRY:mainCRTStartup)
elseif(APPLE)
    target_include_directories(Infinite PRIVATE
        ${SYPHON_DIR}/include
        ${SYPHON_DIR}/src
    )
endif()

# imgui-node-editor 0.9.1 ships its own GetKeyIndex() shim, which collides with
# ImGui's obsolete-keyio copy. Dropping the obsolete API leaves only the shim.
# (Windows sets these above alongside its other defines; keep the macOS path.)
if(NOT WIN32)
    target_compile_definitions(Infinite PRIVATE
        IMGUI_DISABLE_OBSOLETE_KEYIO
        IMGUI_DEFINE_MATH_OPERATORS
    )
endif()

if(APPLE)
    set_source_files_properties(src/platform/Platform.mm src/audio/AudioFileWriter.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(${SYPHON_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-include;${CMAKE_CURRENT_SOURCE_DIR}/${SYPHON_DIR}/src/Syphon_Prefix.pch")

    if(INFINITE_ENABLE_VST3)
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
    endif()

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
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Infinite"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.namansoni.infinite"
        MACOSX_BUNDLE_BUNDLE_VERSION "${INFINITE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${INFINITE_VERSION}"
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

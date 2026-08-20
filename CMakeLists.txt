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

project(Infinite C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
option(INFINITE_ENABLE_VST3 "Build the GPLv3 VST3 plugin backend" OFF)

set(IMGUI_DIR external/imgui)
set(NODE_EDITOR_DIR external/imgui-node-editor)
set(STB_DIR external/stb)
set(SYPHON_DIR external/syphon)

if(INFINITE_ENABLE_VST3)
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")
    if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
        message(FATAL_ERROR "INFINITE_ENABLE_VST3 is ON, but Steinberg VST3 SDK was not found in external/vst3sdk.\n"
                            "Please initialize submodules with:\n"
                            "  git submodule update --init --recursive external/vst3sdk\n"
                            "or clone the VST3 SDK into external/vst3sdk.")
    endif()
endif()

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
    src/core/PatchJson.cpp
    src/core/OscMessage.cpp
    src/platform/Platform.mm
    src/audio/AudioEngine.cpp
    src/audio/ParamMailbox.cpp
    src/audio/MeterRing.cpp
    src/audio/AudioCaptureRing.cpp
    src/audio/AudioFileWriter.mm
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
    src/nodes/TextNode.cpp
    src/nodes/OutputNode.cpp
    src/nodes/SyphonInNode.cpp
    src/nodes/SyphonOutNode.cpp
    src/nodes/ProjectionNode.cpp
    src/nodes/AudioNodes.cpp
    src/nodes/NoteNodes.cpp
    src/nodes/WavetableNode.cpp
    src/nodes/WaveTerrainNode.cpp
    src/nodes/ImageSpectralSynthNode.cpp
    src/nodes/AudioDisplacementNode.cpp
    src/nodes/AudioTextureNode.cpp
    src/nodes/AudioRibbonNode.cpp
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
    )
    set_source_files_properties(src/scanner_main.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

    if(APPLE)
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
endif()

# imgui-node-editor 0.9.1 ships its own GetKeyIndex() shim, which collides with
# ImGui's obsolete-keyio copy. Dropping the obsolete API leaves only the shim.
target_compile_definitions(Infinite PRIVATE
    IMGUI_DISABLE_OBSOLETE_KEYIO
    IMGUI_DEFINE_MATH_OPERATORS
)

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

# The Infinite plugin shell (docs/plans/vst-au/README.md step 1.5): "Infinite Instrument.vst3" and
# "Infinite FX.vst3" from one source (Shell.cpp), plus the Steinberg validator and plugin_shell_check.
#
# A project of its own, beside the app build: the VST3 SDK's CMake (smtg_add_vst3plugin, the
# validator) stays out of the app's CMakeLists. Build through ibuild's lock, one build at a time:
#   python3 scripts/ibuild.py run -- cmake -S src/plugin/shell -B build-plugin -DCMAKE_BUILD_TYPE=Release
#   python3 scripts/ibuild.py run -- cmake --build build-plugin -j4
#
# The shell finds the engine at $INFINITE_PLUGIN_ENGINE, then Contents/Helpers/Infinite Engine.app
# inside the bundle (packaged in 3.6), then the installed Infinite.app.
cmake_minimum_required(VERSION 3.25)

set(INFINITE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
get_filename_component(INFINITE_ROOT "${INFINITE_ROOT}" ABSOLUTE)

# Same SDK lookup as the app: this checkout, else the main checkout's (worktrees have an empty submodule).
set(VST3_SDK_DIR "${INFINITE_ROOT}/external/vst3sdk" CACHE PATH "Steinberg VST3 SDK root")
if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
    execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
        WORKING_DIRECTORY "${INFINITE_ROOT}" OUTPUT_VARIABLE _common OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_common)
        get_filename_component(_main "${_common}" DIRECTORY)
        if(EXISTS "${_main}/external/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h")
            set(VST3_SDK_DIR "${_main}/external/vst3sdk" CACHE PATH "Steinberg VST3 SDK root" FORCE)
        endif()
    endif()
endif()
if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
    message(FATAL_ERROR "VST3 SDK not found; pass -DVST3_SDK_DIR=<path> or init external/vst3sdk")
endif()

# Version from the app's CMakeLists: one version for app and plugin.
file(STRINGS "${INFINITE_ROOT}/CMakeLists.txt" _ver REGEX "^set\\(INFINITE_VERSION \"[0-9.]+\"\\)")
string(REGEX REPLACE ".*\"([0-9.]+)\".*" "\\1" INFINITE_VERSION "${_ver}")

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)   # the validator
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)             # never touch ~/Library from a build
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(infinite-plugin VERSION ${INFINITE_VERSION} LANGUAGES C CXX OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(${VST3_SDK_DIR} vst3sdk)
smtg_enable_vst3_sdk()

find_package(Threads REQUIRED)
add_library(infinite_shell_ipc STATIC
    ${INFINITE_ROOT}/src/platform/common/Ipc.cpp
    ${INFINITE_ROOT}/src/plugin/ipc/PluginIpc.cpp
    ${INFINITE_ROOT}/src/core/License.cpp
    ${INFINITE_ROOT}/src/core/DawPluginInstall.cpp
    ${INFINITE_ROOT}/external/monocypher/monocypher.c
    ${INFINITE_ROOT}/external/monocypher/monocypher-ed25519.c
    ${INFINITE_ROOT}/external/miniz.c)
target_include_directories(infinite_shell_ipc PUBLIC ${INFINITE_ROOT}/src ${INFINITE_ROOT}/external)

# Licence (step 2.6): the public key from worker/license/PUBLIC_KEY.txt, the one the worker signs for.
# INFINITE_LICENSE_TEST_KEY lets the check tools sign their own keys ($INFINITE_LICENSE_PUBKEY or
# license-test-key.hex); development builds only, a shipped build is configured without it.
file(READ "${INFINITE_ROOT}/worker/license/PUBLIC_KEY.txt" INFINITE_LICENSE_PUBKEY_HEX)
string(STRIP "${INFINITE_LICENSE_PUBKEY_HEX}" INFINITE_LICENSE_PUBKEY_HEX)
option(INFINITE_LICENSE_TEST_KEY "Accept a test public key (checks only, never ship)" OFF)
target_compile_definitions(infinite_shell_ipc PUBLIC INFINITE_LICENSE_PUBKEY_HEX="${INFINITE_LICENSE_PUBKEY_HEX}")
if(INFINITE_LICENSE_TEST_KEY)
    message(WARNING "INFINITE_LICENSE_TEST_KEY is ON: this build accepts test licence keys; never ship it")
    target_compile_definitions(infinite_shell_ipc PUBLIC INFINITE_LICENSE_TEST_KEY=1)
endif()
target_link_libraries(infinite_shell_ipc PUBLIC Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(infinite_shell_ipc PUBLIC rt)
endif()
set_target_properties(infinite_shell_ipc PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(infinite_add_plugin target pkg fx)
    smtg_add_vst3plugin(${target} PACKAGE_NAME "${pkg}" SOURCES_LIST ${CMAKE_CURRENT_SOURCE_DIR}/Shell.cpp)
    target_compile_definitions(${target} PRIVATE INFINITE_PLUGIN_FX=${fx} INFINITE_PLUGIN_VERSION="${INFINITE_VERSION}")
    target_link_libraries(${target} PRIVATE sdk infinite_shell_ipc)
    if(APPLE)
        # The editor view (step 2.3); its Objective-C class is named per build (ShellViewMac.mm).
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ShellViewMac.mm)
        target_compile_definitions(${target} PRIVATE INFINITE_VIEW_CLASS=InfiniteShellView_${target})
        target_link_libraries(${target} PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework IOSurface")
    else()
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ShellViewNone.cpp)
    endif()
    if(APPLE)
        smtg_target_set_bundle(${target}
            BUNDLE_IDENTIFIER org.infinitedevices.plugin.${target}
            COMPANY_NAME "Infinite")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

infinite_add_plugin(InfiniteInstrument_vst3 "Infinite Instrument" 0)
infinite_add_plugin(InfiniteFX_vst3 "Infinite FX" 1)

# ---- AU v2 (macOS) through the SDK's auwrapper (step 1.6) -------------------------
# Apple's AudioUnitSDK (Apache-2.0), fetched at a pinned tag, never committed. Each .component
# carries a real copy of its .vst3 at Contents/Resources/plugin.vst3 and loads it in-process.
# auwrapper's base is MusicDeviceBase, so the FX build (type aumf) also gets MIDI.
if(APPLE)
    include(FetchContent)
    FetchContent_Declare(AudioUnitSDK
        GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
        GIT_TAG AudioUnitSDK-1.2.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(AudioUnitSDK)
    file(GLOB AUSDK_SOURCES ${audiounitsdk_SOURCE_DIR}/src/AudioUnitSDK/*.cpp)
    add_library(ausdk STATIC ${AUSDK_SOURCES})
    target_include_directories(ausdk PUBLIC ${audiounitsdk_SOURCE_DIR}/include)
    target_compile_features(ausdk PUBLIC cxx_std_20)   # AudioUnitSDK 1.2 uses std::erase_if

    string(REPLACE "." ";" _vparts "${INFINITE_VERSION}")
    list(GET _vparts 0 _vmaj)
    list(GET _vparts 1 _vmin)
    list(GET _vparts 2 _vpat)
    math(EXPR AU_VERSION_INT "(${_vmaj} << 16) | (${_vmin} << 8) | ${_vpat}")

    set(AUW ${VST3_SDK_DIR}/public.sdk/source/vst/auwrapper)
    function(infinite_add_au target vst3 name id type subtype tag channels)
        set(AU_NAME "${name}")
        set(AU_ID "${id}")
        set(AU_TYPE "${type}")
        set(AU_SUBTYPE "${subtype}")
        set(AU_TAG "${tag}")
        set(AU_CHANNELS "${channels}")
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/au-info.plist.in ${CMAKE_BINARY_DIR}/${target}-Info.plist @ONLY)
        add_library(${target} MODULE ${AUW}/auwrapper.mm ${AUW}/aucocoaview.mm ${AUW}/NSDataIBStream.mm)
        target_compile_definitions(${target} PRIVATE
            SMTG_AUWRAPPER_USES_AUSDK
            SMTG_AUCocoaUIBase_CLASS_NAME=SMTG_AUCocoaUIBase_${target}
            CA_USE_AUDIO_PLUGIN_ONLY=0)
        target_link_libraries(${target} PRIVATE sdk_hosting ausdk
            "-framework AudioUnit" "-framework CoreMIDI" "-framework AudioToolbox"
            "-framework CoreFoundation" "-framework Carbon" "-framework Cocoa" "-framework CoreAudio")
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION component
            OUTPUT_NAME "${name}"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/${target}-Info.plist
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/AU)
        add_dependencies(${target} ${vst3})
        # Runs on every build: a POST_BUILD step would skip it when only the VST3 changed.
        add_custom_target(${target}_bundle ALL
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${vst3}>"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/plugin.vst3"
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${target}>"
            VERBATIM)
        add_dependencies(${target}_bundle ${target} ${vst3})
    endfunction()

    # The Instrument's (inactive) sidechain bus is AU input element 0, so it validates as 2->2.
    set(_ch_inst "\t\t<dict><key>Inputs</key><string>0</string><key>Outputs</key><string>2</string></dict>\n\t\t<dict><key>Inputs</key><string>2</string><key>Outputs</key><string>2</string></dict>")
    set(_ch_fx "\t\t<dict><key>Inputs</key><string>2</string><key>Outputs</key><string>2</string></dict>\n\t\t<dict><key>Inputs</key><string>1</string><key>Outputs</key><string>2</string></dict>")
    infinite_add_au(InfiniteInstrument_au InfiniteInstrument_vst3 "Infinite Instrument" InfiniteInstrument_au aumu InIn Synthesizer "${_ch_inst}")
    infinite_add_au(InfiniteFX_au InfiniteFX_vst3 "Infinite FX" InfiniteFX_au aumf InFx Effects "${_ch_fx}")

    # Loads the installed AUs like Logic (in or out of process) against a real engine.
    add_executable(au_check AuCheck.mm)
    set_target_properties(au_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/Release)
    target_link_libraries(au_check PRIVATE "-framework AudioToolbox" "-framework Foundation" "-framework Cocoa" "-framework QuartzCore" "-framework IOSurface")
endif()

# Loads the bundles like a DAW and checks them against a real engine.
if(APPLE)
    add_executable(plugin_shell_check ShellCheck.cpp ShellCheckMac.mm ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    set_source_files_properties(${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(plugin_shell_check PRIVATE sdk_hosting infinite_shell_ipc "-framework Cocoa" "-framework QuartzCore" "-framework IOSurface")
    target_sources(plugin_shell_check PRIVATE ${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp)
    target_compile_definitions(plugin_shell_check PRIVATE INFINITE_SOURCE_ROOT="${INFINITE_ROOT}")
endif()

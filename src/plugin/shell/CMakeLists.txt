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
    ${INFINITE_ROOT}/src/plugin/ipc/PluginIpc.cpp)
target_include_directories(infinite_shell_ipc PUBLIC ${INFINITE_ROOT}/src)
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

# Loads the bundles like a DAW and checks them against a real engine.
if(APPLE)
    add_executable(plugin_shell_check ShellCheck.cpp ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    set_source_files_properties(${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(plugin_shell_check PRIVATE sdk_hosting "-framework Cocoa")
endif()

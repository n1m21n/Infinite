# Spike 1.0 of docs/plans/vst-au/README.md: a minimal VST3 (+ AU via auwrapper) shell
# that spawns a helper process and passes audio over shared memory.
# Standalone project, not part of the app build. Build through ibuild's lock:
#   scripts/ibuild.py run -- cmake -S tools/plugin/spike -B build-spike -DVST3_SDK_DIR=<sdk>
#   scripts/ibuild.py run -- cmake --build build-spike -j4
cmake_minimum_required(VERSION 3.25)

set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/vst3sdk" CACHE PATH "VST3 SDK root")
if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/ivstaudioprocessor.h")
    message(FATAL_ERROR "VST3 SDK not found at ${VST3_SDK_DIR}; pass -DVST3_SDK_DIR=<main checkout>/external/vst3sdk")
endif()

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)   # gives us the validator
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)              # run it by hand, see README
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)             # never touch ~/Library from a build
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")

project(infinite-plugin-spike VERSION 0.0.1 LANGUAGES C CXX OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(${VST3_SDK_DIR} vst3sdk)
smtg_enable_vst3_sdk()

add_library(spike_shm STATIC spike_shm.cpp)
target_include_directories(spike_shm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(infinite-spike-engine spike_engine.cpp)
target_link_libraries(infinite-spike-engine PRIVATE spike_shm)

add_executable(spike_bench spike_bench.cpp)
target_link_libraries(spike_bench PRIVATE spike_shm)

smtg_add_vst3plugin(InfiniteSpike spike_shell.cpp)
target_link_libraries(InfiniteSpike PRIVATE sdk spike_shm)
add_dependencies(InfiniteSpike infinite-spike-engine)
add_custom_command(TARGET InfiniteSpike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:InfiniteSpike>/Helpers"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:infinite-spike-engine>"
            "$<TARGET_BUNDLE_CONTENT_DIR:InfiniteSpike>/Helpers/infinite-spike-engine"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:InfiniteSpike>"
    VERBATIM)

add_executable(spike_hosttest spike_hosttest.cpp ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
set_source_files_properties(${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_link_libraries(spike_hosttest PRIVATE sdk_hosting "-framework Cocoa")

# ---- AU v2 through the SDK's auwrapper, without the Xcode generator --------------
# Apple's AudioUnitSDK (Apache-2.0) fetched at a pinned tag, never committed.
include(FetchContent)
FetchContent_Declare(AudioUnitSDK
    GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
    GIT_TAG AudioUnitSDK-1.2.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(AudioUnitSDK)
file(GLOB AUSDK_SOURCES ${audiounitsdk_SOURCE_DIR}/src/AudioUnitSDK/*.cpp)
add_library(ausdk STATIC ${AUSDK_SOURCES})
target_include_directories(ausdk PUBLIC ${audiounitsdk_SOURCE_DIR}/include)
target_compile_features(ausdk PUBLIC cxx_std_20)  # AudioUnitSDK 1.2 uses std::erase_if

set(AUW ${VST3_SDK_DIR}/public.sdk/source/vst/auwrapper)
add_library(InfiniteSpikeAU MODULE
    ${AUW}/auwrapper.mm ${AUW}/aucocoaview.mm ${AUW}/NSDataIBStream.mm)
target_compile_definitions(InfiniteSpikeAU PRIVATE
    SMTG_AUWRAPPER_USES_AUSDK
    SMTG_AUCocoaUIBase_CLASS_NAME=SMTG_AUCocoaUIBase_InfiniteSpike
    CA_USE_AUDIO_PLUGIN_ONLY=0)
target_link_libraries(InfiniteSpikeAU PRIVATE sdk_hosting ausdk
    "-framework AudioUnit" "-framework CoreMIDI" "-framework AudioToolbox"
    "-framework CoreFoundation" "-framework Carbon" "-framework Cocoa" "-framework CoreAudio")
set_target_properties(InfiniteSpikeAU PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION component
    OUTPUT_NAME "Infinite Spike"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/au-info.plist.in
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/AU)
add_dependencies(InfiniteSpikeAU InfiniteSpike)
# The wrapper loads Resources/plugin.vst3 in-process; a real copy (not a symlink) so the
# shell finds its helper beside itself and the bundle can be moved as one unit.
# Runs on every build (a POST_BUILD step would skip it when only the VST3 changed).
add_custom_target(InfiniteSpikeAU_bundle ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:InfiniteSpikeAU>/Resources/plugin.vst3"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:InfiniteSpikeAU>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:InfiniteSpike>"
            "$<TARGET_BUNDLE_CONTENT_DIR:InfiniteSpikeAU>/Resources/plugin.vst3"
    COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:InfiniteSpikeAU>"
    VERBATIM)
add_dependencies(InfiniteSpikeAU_bundle InfiniteSpikeAU InfiniteSpike)

add_executable(spike_auoop spike_auoop.mm)
target_link_libraries(spike_auoop PRIVATE "-framework AudioToolbox" "-framework Foundation")

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
set(INFINITE_VERSION "0.4.7")
# VERSIONINFO (src/platform/win/Infinite.rc.in) wants four dot/comma-separated
# numeric components but INFINITE_VERSION is three; kept as an explicit
# second value rather than derived, so bump both together when cutting a
# release.
set(INFINITE_VERSION_RC "0,4,7,0")
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

# Sanitizer builds: -DINFINITE_SANITIZE=address,undefined (or thread). Off by
# default and never part of a shipped build. Set before any target is created
# so every dependency built from source agrees on the runtime. Not combinable
# with the static MSVC CRT above, so non-MSVC only.
set(INFINITE_SANITIZE "" CACHE STRING "Comma-separated -fsanitize list (address,undefined | thread); empty = off")
if(INFINITE_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${INFINITE_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=${INFINITE_SANITIZE})
    message(STATUS "Sanitizers enabled: ${INFINITE_SANITIZE}")
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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# FLAC writing on Windows & Linux: dr_libs is decode-only, so the file writer links
# the real libFLAC instead (static, no Ogg). macOS keeps CoreAudio's
# ExtAudioFile and never compiles this in.
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
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

    if(WIN32)
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
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(ONNX_LINUX_ARCH "x64")
        set(ONNX_LINUX_SHA256 "67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(ONNX_LINUX_ARCH "aarch64")
            set(ONNX_LINUX_SHA256 "ae4fedbdc8c18d688c01306b4b50c63de3445cdf2dbd720e01a2fa3810b8106a")
        endif()

        FetchContent_Declare(
            onnxruntime_linux
            URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_DML_VERSION}/onnxruntime-linux-${ONNX_LINUX_ARCH}-${ONNXRUNTIME_DML_VERSION}.tgz"
            URL_HASH SHA256=${ONNX_LINUX_SHA256}
        )
        FetchContent_MakeAvailable(onnxruntime_linux)

        set(ONNXRUNTIME_INCLUDE_DIR "${onnxruntime_linux_SOURCE_DIR}/include")
        set(ONNXRUNTIME_LIB "${onnxruntime_linux_SOURCE_DIR}/lib/libonnxruntime.so")
        file(GLOB ONNXRUNTIME_SO_FILES "${onnxruntime_linux_SOURCE_DIR}/lib/libonnxruntime*.so*")
    endif()
endif()

# FFmpeg, for Platform::VideoOpen/RecorderStart/CameraOpen's decode/encode/mux
# surface on Linux (src/platform/linux/MediaLinux.cpp) - macOS uses
# AVFoundation and Windows uses Media Foundation, both OS-supplied; Linux has
# no OS-supplied decoder/encoder, so this is the one platform where the app
# itself bundles a codec library. Fetched as prebuilt shared libraries from
# BtbN/FFmpeg-Builds, the same shape as the ONNX Runtime FetchContent above.
#
# ---- Licence decision (see docs/plans/linux/phase-03-media.md) -----------
# INFINITE_ENABLE_GPL_CODECS gates which FFmpeg build is linked:
#   ON  (default on Linux): the "-gpl-shared" build, compiled with
#       --enable-gpl and libx264 linked in. This is what lets the recorder
#       write H.264 video, matching macOS (VideoToolbox) and Windows (Media
#       Foundation) - both OS-supplied H.264 encoders whose patent licence
#       was paid by the OS vendor. Bundling x264 makes this project the
#       distributor of an H.264 encoder for the first time on Linux; the
#       reasoning for why that's still the right default (posture parity
#       with OBS/Kdenlive/Shotcut/HandBrake/Blender, and why silently
#       recording a worse file than macOS/Windows would itself be a defect)
#       is in phase-03-media.md's "Licence decision" section, not repeated
#       here. The resulting Infinite binary is GPLv3 - see LICENSE.
#   OFF: the "-lgpl-shared" build, no --enable-gpl, no x264. Required to
#       exist and to actually work (not just compile) for distros that
#       cannot ship a bundled H.264 encoder (Fedora, RHEL, Flathub's main
#       repo) - see LICENSE's promise about INFINITE_ENABLE_VST3=OFF, which
#       this mirrors. RecorderStart's H.264 path is unavailable in this
#       configuration; video decode/playback (no libx264 needed for that)
#       still works, since decoding H.264 needs no patent-encumbered encoder.
#
# ---- Source offer (GPL section 6) -----------------------------------------
# BtbN distributes prebuilt binaries only; the obligation to provide
# corresponding source for a GPL-linked binary falls on whoever distributes
# THAT BINARY - this project, not BtbN. So the exact upstream FFmpeg and
# x264 SOURCE this build corresponds to is pinned here too, independent of
# BtbN's own build pipeline, and documented in THIRD_PARTY_NOTICES:
#   FFmpeg, git commit 29e619e767 on release/8.1 - the commit the pinned
#   archives' own version string (n8.1.3-9-g29e619e767) names, 9 commits
#   past the n8.1.3 release:
#     https://git.ffmpeg.org/gitweb/ffmpeg.git/snapshot/29e619e767.tar.gz
#   NOT ffmpeg-8.1.tar.xz - the release tarball is a different work from a
#   snapshot 9 commits later, and only the actual commit satisfies section 6.
#   x264: BtbN pulls x264's stable branch at build time and publishes no
#   commit id in the release metadata, so the commit has to be read back out
#   of the shipped libavcodec.so's build-configuration string. See
#   THIRD_PARTY_NOTICES, which also carries the outstanding release blocker:
#   the snapshot and the x264 source must be downloaded, their SHA256 sums
#   recorded, and both archived as release assets before the Linux binary is
#   distributed - a section 6 offer has to be fulfillable, not just written.
# These are documentation pins, not build inputs - this CMakeLists links the
# prebuilt BtbN binaries above, it does not build FFmpeg/x264 from source.
# Keep them in step with FFMPEG_ARCHIVE_VERSION below and with
# THIRD_PARTY_NOTICES: all three describe one release or none of them do.
#
# ---- Why FFMPEG_RELEASE_TAG is not "latest" --------------------------------
# BtbN/FFmpeg-Builds' "latest" GitHub release tag is a rolling pointer: its
# assets get replaced in place every time BtbN cuts a new autobuild, so a
# SHA256 pinned against "latest" today silently goes stale and turns into a
# hash-mismatch FetchContent failure on the next fresh configure (hit during
# Linux Phase 5 AppImage work, 2026-09-16, on an arm64 dev container). BtbN
# also publishes each autobuild under its own immutable tag
# (autobuild-YYYY-MM-DD-HH-MM), whose assets never change - pin to one of
# those instead, and the pinned SHA256 stays valid while the tag exists. BtbN prunes old autobuild
# tags after a couple of weeks (the 2026-09-15 pin 404'd on 2026-10-01), so re-pin before a release build.
#
# ---- How to re-pin when a newer FFmpeg build is wanted ---------------------
#   1. Find the desired autobuild tag and the exact version string embedded
#      in its asset filenames:
#        curl -s https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/tags/latest \
#          | grep -o '"tag_name": *"[^"]*"'   # confirms what "latest" points at today
#        curl -s https://api.github.com/repos/BtbN/FFmpeg-Builds/releases \
#          | grep -o '"tag_name": *"autobuild-[^"]*"' | head -1
#      then list that release's assets to read the version string (the
#      "n8.1.3-9-g29e619e767" part) out of an asset name like
#      ffmpeg-n8.1.3-9-g29e619e767-linux64-gpl-shared-8.1.tar.xz.
#   2. Download the four *-shared-8.1.tar.xz archives this file actually
#      links (linux64-gpl, linux64-lgpl, linuxarm64-gpl, linuxarm64-lgpl) from
#      that tag and `sha256sum` them directly - never copy a hash off a
#      webpage, GitHub does not publish per-asset checksums anywhere trusted.
#   3. Update FFMPEG_RELEASE_TAG, FFMPEG_ARCHIVE_VERSION, and all four
#      SHA256 pins below together, so they describe one consistent release.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(INFINITE_ENABLE_GPL_CODECS "Link GPL FFmpeg + libx264 for H.264 recording on Linux (OFF = LGPL FFmpeg, no H.264 encode)" ON)

    # Immutable BtbN/FFmpeg-Builds release tag and the version string its
    # asset filenames embed - see "How to re-pin" above. Verified 2026-10-01 (the 2026-09-15 tag was pruned upstream and 404'd in CI).
    set(FFMPEG_RELEASE_TAG "autobuild-2026-09-30-13-08")
    set(FFMPEG_ARCHIVE_VERSION "n8.1.3-9-g29e619e767")
    set(FFMPEG_LINUX_ARCH "linux64")
    set(FFMPEG_GPL_SHA256 "76649c19a7a4615b367251ba4a8adc643f65831278a00e7a0c38398f7f06484b")
    set(FFMPEG_LGPL_SHA256 "d4d7b6936f492c0b866b1cb4a29a1bbae09d6a064b4ec4eb90c95bed7969535a")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(FFMPEG_LINUX_ARCH "linuxarm64")
        set(FFMPEG_GPL_SHA256 "d5b52fcb4f8d86d997bb75f6c4b383ca0be3f4cd9efd94ab3b4f27b22984f91c")
        set(FFMPEG_LGPL_SHA256 "d44838f375c66cf31a8b1473b6f8bad76e5c7dac55870c4d96a75c907a52ba2d")
    endif()

    if(INFINITE_ENABLE_GPL_CODECS)
        set(FFMPEG_LICENCE_TAG "gpl")
        set(FFMPEG_SHA256 "${FFMPEG_GPL_SHA256}")
    else()
        set(FFMPEG_LICENCE_TAG "lgpl")
        set(FFMPEG_SHA256 "${FFMPEG_LGPL_SHA256}")
    endif()
    set(FFMPEG_ARCHIVE_NAME "ffmpeg-${FFMPEG_ARCHIVE_VERSION}-${FFMPEG_LINUX_ARCH}-${FFMPEG_LICENCE_TAG}-shared-8.1")

    FetchContent_Declare(
        ffmpeg_linux
        URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/${FFMPEG_RELEASE_TAG}/${FFMPEG_ARCHIVE_NAME}.tar.xz"
        URL_HASH SHA256=${FFMPEG_SHA256}
    )
    FetchContent_MakeAvailable(ffmpeg_linux)

    set(FFMPEG_INCLUDE_DIR "${ffmpeg_linux_SOURCE_DIR}/include")
    set(FFMPEG_LIB_DIR "${ffmpeg_linux_SOURCE_DIR}/lib")
    set(FFMPEG_LIBS
        "${FFMPEG_LIB_DIR}/libavformat.so"
        "${FFMPEG_LIB_DIR}/libavcodec.so"
        "${FFMPEG_LIB_DIR}/libavutil.so"
        "${FFMPEG_LIB_DIR}/libswscale.so"
        "${FFMPEG_LIB_DIR}/libswresample.so"
    )
    # Every libav*.so.N / libsw*.so.N.M.P file, so the actual runtime .so's
    # (not just the unversioned dev symlinks CMake links against) ship next to
    # the binary - dlopen/ld.so at runtime resolves by soname, not by the
    # unversioned name.
    file(GLOB FFMPEG_SO_FILES
        "${FFMPEG_LIB_DIR}/libavformat.so*"
        "${FFMPEG_LIB_DIR}/libavcodec.so*"
        "${FFMPEG_LIB_DIR}/libavutil.so*"
        "${FFMPEG_LIB_DIR}/libswscale.so*"
        "${FFMPEG_LIB_DIR}/libswresample.so*"
    )
    message(STATUS "Linux FFmpeg: ${FFMPEG_ARCHIVE_NAME} (INFINITE_ENABLE_GPL_CODECS=${INFINITE_ENABLE_GPL_CODECS})")
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
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "INFINITE_ENABLE_VST3 is only wired up on macOS, Windows and Linux right now; "
                            "this platform's layer reports plugin hosting as unavailable.")
    endif()
endif()

if(INFINITE_ENABLE_VST3)
    # A git worktree has an empty external/vst3sdk (submodules are per
    # checkout), so fall back to the main checkout's copy instead of cloning
    # the SDK again for every worktree. -DVST3_SDK_DIR=<path> overrides both.
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH "Steinberg VST3 SDK root")
    set(_vst3_probe "pluginterfaces/vst/ivstaudioprocessor.h")
    if(NOT EXISTS "${VST3_SDK_DIR}/${_vst3_probe}")
        execute_process(
            COMMAND git rev-parse --path-format=absolute --git-common-dir
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_common_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_git_common_dir)
            get_filename_component(_main_checkout "${_git_common_dir}" DIRECTORY)
            if(EXISTS "${_main_checkout}/external/vst3sdk/${_vst3_probe}")
                set(VST3_SDK_DIR "${_main_checkout}/external/vst3sdk" CACHE PATH "Steinberg VST3 SDK root" FORCE)
            endif()
        endif()
    endif()
    if(NOT EXISTS "${VST3_SDK_DIR}/${_vst3_probe}")
        # Only pluginterfaces and public.sdk are used; never --recursive
        # (it also clones vstgui4, doc and tutorials).
        message(FATAL_ERROR "INFINITE_ENABLE_VST3 is ON, but the Steinberg VST3 SDK was not found.\n"
                            "Point at an existing copy with -DVST3_SDK_DIR=<path>, or fetch only what the build uses:\n"
                            "  git submodule update --init external/vst3sdk\n"
                            "  git -C external/vst3sdk submodule update --init pluginterfaces public.sdk")
    endif()
    message(STATUS "VST3 SDK: ${VST3_SDK_DIR}")
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
    # src/app/**: code that used to live in src/main.cpp
    src/app/AppState.cpp
    src/app/Startup.cpp
    src/app/bodies/AudioBodyKit.cpp
    src/app/bodies/AudioNodeBodies.cpp
    src/app/bodies/FieldParams.cpp
    src/app/bodies/FxBodies1.cpp
    src/app/bodies/FxBodies2.cpp
    src/app/bodies/GeometryParams.cpp
    src/app/bodies/NoteBodies.cpp
    src/app/bodies/OscBodies.cpp
    src/app/bodies/ParamBodies1.cpp
    src/app/bodies/ParamBodies2.cpp
    src/app/bodies/PreviewBodies.cpp
    src/app/bodies/SamplerBodies.cpp
    src/app/bodies/SpatialMixerBody.cpp
    src/app/frame/CookFrame.cpp
    src/app/frame/FrameStatics.cpp
    src/app/frame/FrameTests1.cpp
    src/app/frame/FrameTests2.cpp
    src/app/frame/FrameTests3.cpp
    src/app/frame/FrameTests4.cpp
    src/app/frame/FrameTests5.cpp
    src/app/frame/FrameTests6.cpp
    src/app/frame/StageBenchHarness.cpp
    src/app/frame/StageDropHandling.cpp
    src/app/frame/StageFloating.cpp
    src/app/frame/StageFramePump.cpp
    src/app/frame/StageFrameTestsA.cpp
    src/app/frame/StageKeyboard.cpp
    src/app/frame/StageLayout.cpp
    src/app/frame/StageLinks.cpp
    src/app/frame/StageMenuBar.cpp
    src/app/frame/StageNodeBodies.cpp
    src/app/frame/StagePopupsA.cpp
    src/app/frame/StagePopupsB.cpp
    src/app/frame/StagePresent.cpp
    src/app/frame/StageSidePanels.cpp
    src/app/frame/StageTestsB.cpp
    src/app/graph/AudioTopology.cpp
    src/app/graph/Core.cpp
    src/app/graph/Disconnect.cpp
    src/app/graph/ExportImage.cpp
    src/app/graph/GraphHosts.cpp
    src/app/graph/NodeRegistry.cpp
    src/app/graph/PatchFiles.cpp
    src/app/graph/PatchIO.cpp
    src/app/graph/PatchLoad.cpp
    src/app/graph/Projector.cpp
    src/app/graph/Rpc.cpp
    src/app/graph/Wiring.cpp
    src/app/panels/ArrangePanel.cpp
    src/app/panels/ArrangeRender.cpp
    src/app/panels/ArrangeSupport1.cpp
    src/app/panels/ArrangeSupport2.cpp
    src/app/panels/HelpWindows.cpp
    src/app/panels/LibraryPanels.cpp
    src/app/panels/Minimap.cpp
    src/app/panels/ModMatrixPanel.cpp
    src/app/panels/PerfPanel.cpp
    src/app/panels/SettingsWindow.cpp
    src/app/panels/ViewportPanel.cpp
    src/app/selftest/AudioSweepTests.cpp
    src/app/selftest/AvRecTests.cpp
    src/app/selftest/BenchScenes.cpp
    src/app/selftest/DspFixtures1.cpp
    src/app/selftest/DspFixtures2.cpp
    src/app/selftest/DspFixtures3.cpp
    src/app/selftest/FieldTests1.cpp
    src/app/selftest/FieldTests2.cpp
    src/app/selftest/Headless.cpp
    src/app/selftest/I18nTests.cpp
    src/app/selftest/MiscTests.cpp
    src/app/selftest/SpatialTests.cpp
    src/app/selftest/PlatformTests.cpp
    src/app/ui/BrowserUi.cpp
    src/app/ui/ModWidgets.cpp
    src/app/ui/ParamWidgets.cpp
    src/app/ui/Theme.cpp
    src/core/GLUtil.cpp
    src/core/CurveShape.cpp
    src/core/NodeFactory.cpp
    src/core/CategoryColors.cpp
    src/core/I18n.cpp
    src/core/FilterDefs.cpp
    src/core/BlendModes.cpp
    src/core/Mesh.cpp
    src/core/NodeViewport.cpp
    src/core/Transport.cpp
    src/core/Modulation.cpp
    src/core/MovementLog.cpp
    src/core/MovementStats.cpp
    src/core/AudioSummary.cpp
    src/core/ColorStats.cpp
    src/core/ContactSheet.cpp
    src/core/HeadlessJob.cpp
    src/core/HeadlessNotes.cpp
    src/core/PatchSchema.cpp
    src/core/PatchExplain.cpp
    src/core/GestureRecorder.cpp
    src/core/Expression.cpp
    src/core/SysInfo.cpp
    src/core/BenchReport.cpp
    src/core/field/FieldLex.cpp
    src/core/field/FieldParse.cpp
    src/core/field/FieldIR.cpp
    src/core/field/FieldBytecode.cpp
    src/core/field/FieldVM.cpp
    src/core/field/FieldRandom.cpp
    src/core/field/FieldProgramCache.cpp
    src/core/field/FieldSwizzle.cpp
    src/core/field/ElementStore.cpp
    src/core/field/ElementBackend.cpp
    src/core/field/FieldState.cpp
    src/core/field/FieldCycles.cpp
    src/core/field/ParamTable.cpp
    src/core/field/FieldDevice.cpp
    src/core/field/PinTable.cpp
    src/core/field/GlslBackend.cpp
    src/core/field/PixelState.cpp
    src/core/field/Transfer.cpp
    src/core/field/ReduceOps.cpp
    src/core/field/BackendRegister.cpp
    src/core/field/FieldGraphKernel.cpp
    src/core/field/FieldGraphReconciler.cpp
    src/core/field/FieldGraphOwnership.cpp
    src/core/ExprGlobals.cpp
    src/core/Palette.cpp
    src/core/Patch.cpp
    src/core/PatchLayout.cpp
    src/arrange/ArrangeModel.cpp
    src/arrange/ArrangeMediaImport.cpp
    src/core/RemoteControl.cpp
    src/core/PatchJson.cpp
    src/core/OscMessage.cpp
    src/core/UpdateCheck.cpp
    src/core/AudioDecodeCache.cpp
    src/core/GltfImport.cpp
    src/audio/AudioEngine.cpp
    src/audio/ParamMailbox.cpp
    src/audio/MeterRing.cpp
    src/audio/MidiFile.cpp
    src/audio/AudioCaptureRing.cpp
    external/miniz.c
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
    src/audio/dsp/ReverbKernelLegacy.cpp
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
    src/audio/dsp/KeySnapKernel.cpp
    src/audio/dsp/SpectrumSlideKernel.cpp
    src/audio/dsp/ShapeResonatorKernel.cpp
    src/audio/dsp/MeshModalSolver.cpp
    src/nodes/ImageSourceNode.cpp
    src/nodes/SlideshowNode.cpp
    src/nodes/ShapeNode.cpp
    src/nodes/FormulaNode.cpp
    src/nodes/FilterNode.cpp
    src/nodes/BlendNode.cpp
    src/nodes/LayerStackNode.cpp
    src/nodes/FitNode.cpp
    src/nodes/VideoSourceNode.cpp
    src/nodes/VideoInNode.cpp
    src/nodes/ModulatorNodes.cpp
    src/nodes/PredictionNodes.cpp
    src/nodes/PredictiveNotesNode.cpp
    src/nodes/PredictiveColoringNode.cpp
    src/nodes/PredictiveQuantizeNode.cpp
    src/nodes/PredictiveVelocityNode.cpp
    src/nodes/PredictiveRhythmNode.cpp
    src/nodes/PredictiveModulatorNode.cpp
    src/audio/NoteModel.cpp
    src/nodes/OscNodes.cpp
    src/nodes/NoiseNode.cpp
    src/nodes/SketchNode.cpp
    src/core/sketch/SketchEngine.cpp
    src/app/bodies/SketchParams.cpp
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
    src/nodes/FieldElementNode.cpp
    src/nodes/FieldPrimitiveNode.cpp
    src/nodes/FieldSampleNode.cpp
    src/nodes/FieldSynthNode.cpp
    src/nodes/FieldPixelNode.cpp
    src/nodes/FieldGraphNode.cpp
    src/nodes/PointDistributionNodes.cpp
    src/nodes/DepthProjectionNode.cpp
    src/nodes/SceneNodes.cpp
    src/nodes/FeedbackNodes.cpp
    src/nodes/SwitcherNode.cpp
    src/nodes/Switcher3DNode.cpp
    src/nodes/CurveNode.cpp
    src/nodes/TextNode.cpp
    src/nodes/OutputNode.cpp
    src/nodes/SyphonInNode.cpp
    src/nodes/SyphonOutNode.cpp
    src/nodes/NdiInNode.cpp
    src/nodes/NdiOutNode.cpp
    src/platform/ndi/NdiRuntime.cpp
    src/nodes/ProjectionNode.cpp
    src/nodes/AudioNodes.cpp
    src/nodes/SpatialMixerNode.cpp
    src/audio/dsp/HrtfSet.cpp
    src/audio/dsp/HrtfKemarData.cpp
    src/nodes/AudioMeterNode.cpp
    src/nodes/NoteNodes.cpp
    src/nodes/WavetableNode.cpp
    src/nodes/AnalogNode.cpp
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
    src/audio/dsp/SlicerDsp.cpp
    src/nodes/OscillatorNode.cpp
    src/nodes/MetallicNode.cpp
    src/nodes/AudioEffectNode.cpp
    src/nodes/SamplerNode.cpp
    src/nodes/LooperNode.cpp
    src/nodes/MpcNode.cpp
    src/nodes/SlicerNode.cpp
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

# Interface translation (docs/plans/i18n): <code>.tsv tables and the CJK font subsets, copied
# into every platform's Resources/ layout below. Globbed so a new language is one new file.
file(GLOB INFINITE_LANG_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/lang/*.tsv")
file(GLOB INFINITE_NOTO_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Noto/*-Subset.otf")

set(APPLE_SOURCES
    src/platform/Platform.mm
    src/platform/HeadTracker.mm
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
    src/platform/HeadTrackerStub.cpp
    src/platform/win/TextWin.cpp
    src/platform/win/CrashHandlerWin.cpp
    # GL-only half of the Spout bridge (glad), kept out of PlatformWin.cpp's
    # TU since that file also includes SpoutGL's headers, which drag in the
    # legacy <GL/gl.h> - see SpoutGLBridge.h for why the two can't mix.
    src/platform/win/SpoutGLBridge.cpp
    src/platform/win/AudioDeviceWin.cpp
    src/platform/win/MidiWin.cpp
    src/platform/win/MediaWin.cpp
    src/platform/common/MediaDecodePortable.cpp
    src/platform/common/SubjectMaskOnnx.cpp
    src/platform/win/PluginHostWin.cpp
    src/audio/AudioFileWriter.cpp
    # zlib backend for tinyexr's EXR decode (see MediaDecodePortable.cpp).
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

set(LINUX_SOURCES
    src/platform/linux/PlatformLinux.cpp
    src/platform/HeadTrackerStub.cpp
    src/platform/linux/TextLinux.cpp
    src/platform/linux/CrashHandlerLinux.cpp
    src/platform/linux/AudioDeviceLinux.cpp
    src/platform/linux/MiniaudioImpl.cpp
    src/platform/linux/MidiLinux.cpp
    src/platform/linux/MediaLinux.cpp
    src/platform/linux/CameraLinux.cpp
    src/platform/linux/PluginHostLinux.cpp
    src/platform/linux/SyphonLinux.cpp
    src/platform/common/MediaDecodePortable.cpp
    src/platform/common/SubjectMaskOnnx.cpp
    src/audio/AudioFileWriter.cpp
    external/tinyfiledialogs/tinyfiledialogs.c
    external/miniz.c
    external/glad/src/gl.c
)

# (Was: /Od on src/main.cpp for an MSVC x64 internal compiler error caused by the
# 37k-line main().) main() is now a ~120-line driver and the frame body lives in
# src/app/Startup.cpp and src/app/frame/Stage*.cpp, so the override is gone. If CL.exe
# crashes again at /O2, scope /Od to the one Stage*.cpp file that does it:
#   set_source_files_properties(src/app/frame/<File>.cpp PROPERTIES COMPILE_OPTIONS "/Od")
# It did, in the self-test split: CL.exe died (exit -529706956) in both Windows x64 runs
# on DspFixtures2.cpp (CL prints a file's name when it starts compiling it, so the last
# name in the log is the one that crashed). The other self-test files over 3000 lines
# (same split, same shape) are scoped too so the next run does not crash one file later.
# Test-only code, so /Od costs nothing that matters. See .claude/skills/ci-failure-triage.
if(MSVC)
    set_source_files_properties(
        src/app/selftest/DspFixtures2.cpp
        src/app/selftest/DspFixtures3.cpp
        src/app/selftest/FieldTests2.cpp
        PROPERTIES COMPILE_OPTIONS "/Od")
endif()

# Dev-only tool: compiles a Field pixel/element source file through the same
# Lex/Parse/IR/GLSL pipeline FieldPixelNode::Apply() runs at load time, so a
# new preset can be checked (and its generated GLSL inspected) before it's
# added to Presets() - there is otherwise no way to know a preset compiles
# without loading it in the running app. Deliberately excludes Expression.cpp
# and links a stub ExprGlobals (see tools/field_preset_check.cpp) to stay a
# fast, dependency-free command-line build separate from the app target.
add_executable(field-preset-check
    tools/field_preset_check.cpp
    src/core/field/FieldLex.cpp
    src/core/field/FieldParse.cpp
    src/core/field/FieldIR.cpp
    src/core/field/FieldSwizzle.cpp
    src/core/field/FieldCycles.cpp
    src/core/field/GlslBackend.cpp
    src/core/field/Transfer.cpp
    src/core/field/ReduceOps.cpp
    src/core/field/BackendRegister.cpp
    src/core/field/ElementBackend.cpp
    src/core/field/ElementStore.cpp
    src/core/field/FieldRandom.cpp
    src/core/field/FieldState.cpp
)
target_include_directories(field-preset-check PRIVATE src src/core)

# Headless checks for the Sketch engine (docs/plans/sketch/README.md exit criteria
# that need no window): compile, params, determinism, abort, memory, errors.
add_executable(sketch-check src/core/sketch/SketchCheck.cpp src/core/sketch/SketchEngine.cpp)
target_include_directories(sketch-check PRIVATE src/core/sketch)
target_link_libraries(sketch-check PRIVATE sketch_deps lunasvg_lib)

# Sketch node dependencies (docs/plans/sketch/README.md): quickjs-ng (JS engine,
# no quickjs-libc so a patch cannot reach files or network) and plutovg (CPU
# Canvas-2D rasteriser, same pixels on all three OSes). Vendored as plain
# source, built as one static C library; see each VENDORED_COMMIT.txt.
add_library(sketch_deps STATIC
    external/quickjs-ng/quickjs.c
    external/quickjs-ng/libregexp.c
    external/quickjs-ng/libunicode.c
    external/quickjs-ng/dtoa.c
    external/plutovg/source/plutovg-blend.c
    external/plutovg/source/plutovg-canvas.c
    external/plutovg/source/plutovg-font.c
    external/plutovg/source/plutovg-matrix.c
    external/plutovg/source/plutovg-paint.c
    external/plutovg/source/plutovg-path.c
    external/plutovg/source/plutovg-rasterize.c
    external/plutovg/source/plutovg-surface.c
    external/plutovg/source/plutovg-ft-math.c
    external/plutovg/source/plutovg-ft-raster.c
    external/plutovg/source/plutovg-ft-stroker.c
)
set_target_properties(sketch_deps PROPERTIES C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(sketch_deps PUBLIC external/quickjs-ng external/plutovg/include)
target_compile_definitions(sketch_deps PRIVATE PLUTOVG_BUILD PLUTOVG_BUILD_STATIC _GNU_SOURCE)
target_compile_definitions(sketch_deps PUBLIC PLUTOVG_BUILD_STATIC)
if(MSVC)
    # quickjs-ng uses C11 atomics (MSVC 17.5+).
    target_compile_options(sketch_deps PRIVATE /w /std:c11 /experimental:c11atomics)
else()
    target_compile_options(sketch_deps PRIVATE -w)
endif()
if(NOT WIN32)
    target_link_libraries(sketch_deps PUBLIC m)
endif()

# SVG for Sketch (svgSet/svgDraw): lunasvg renders through the plutovg above, so
# an SVG and a sketch share one rasteriser. System fonts are off so text in an
# SVG draws the same on every OS (the app registers the bundled Inter instead).
add_library(lunasvg_lib STATIC
    external/lunasvg/source/graphics.cpp
    external/lunasvg/source/lunasvg.cpp
    external/lunasvg/source/svgelement.cpp
    external/lunasvg/source/svggeometryelement.cpp
    external/lunasvg/source/svglayoutstate.cpp
    external/lunasvg/source/svgpaintelement.cpp
    external/lunasvg/source/svgparser.cpp
    external/lunasvg/source/svgproperty.cpp
    external/lunasvg/source/svgrenderstate.cpp
    external/lunasvg/source/svgtextelement.cpp
)
set_target_properties(lunasvg_lib PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(lunasvg_lib PUBLIC external/lunasvg/include)
target_compile_definitions(lunasvg_lib PRIVATE LUNASVG_BUILD LUNASVG_DISABLE_LOAD_SYSTEM_FONTS)
target_compile_definitions(lunasvg_lib PUBLIC LUNASVG_BUILD_STATIC)
target_link_libraries(lunasvg_lib PUBLIC sketch_deps)
if(MSVC)
    target_compile_options(lunasvg_lib PRIVATE /w)
else()
    target_compile_options(lunasvg_lib PRIVATE -w)
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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    # ALSA (libasound2-dev, see tools/linux/deps-apt.sh) backs the sequencer
    # MIDI implementation in MidiLinux.cpp (snd_seq_*) - linked directly,
    # unlike miniaudio's audio backends, which are dlopen()'d at runtime (see
    # MiniaudioImpl.cpp's header comment). ALSA has no equivalent
    # runtime-probing API for the sequencer, and every desktop Linux target
    # this app ships to has libasound itself present even when no PCM
    # hardware does - it's the kernel-level ABI PipeWire/PulseAudio's own
    # sequencer bridges implement against, not an optional add-on package.
    pkg_check_modules(ALSA REQUIRED alsa)
    target_sources(Infinite PRIVATE ${LINUX_SOURCES})
    target_include_directories(Infinite PRIVATE
        ${FONTCONFIG_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${ALSA_INCLUDE_DIRS}
        external/miniaudio
    )
    target_link_libraries(Infinite PRIVATE ${ALSA_LIBRARIES})
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
    external/tinyfiledialogs
    external/glad/include
    # NDI headers only (MIT, dynamic loader); the runtime is never bundled.
    external/ndi/include
    src/platform/ndi
)

target_link_libraries(Infinite PRIVATE glfw sketch_deps lunasvg_lib)

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
        # Platform::ProcessRssMb() (GetProcessMemoryInfo), for the
        # INFINITE_BENCH suite's mem.rss_mb field.
        psapi
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

    # Infinite.com: console-subsystem launcher for Infinite.exe so cmd and
    # PowerShell wait for a headless run and see its exit code and stdout (R489).
    # cmd/PowerShell resolve `Infinite` to .com before .exe. Own PDB name, or it
    # would collide with Infinite.pdb in the same output folder.
    add_executable(infinite-cli src/cli_main_win.cpp)
    target_compile_definitions(infinite-cli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    set_target_properties(infinite-cli PROPERTIES
        OUTPUT_NAME Infinite
        SUFFIX ".com"
        PDB_NAME infinite-cli)
    add_dependencies(Infinite infinite-cli)

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
            # PlatformWin.cpp's matting entry points forward to OrtMatting,
            # which moved out of that file into common/ when Linux started
            # sharing it. This target compiles PlatformWin.cpp too, so it needs
            # the same partner file or it fails to link - and it is a separate
            # executable from Infinite, so WIN32_SOURCES does not reach it.
            src/platform/common/SubjectMaskOnnx.cpp
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
            glfw "${ONNXRUNTIME_LIB}" opengl32 ole32 shell32 gdi32 user32 advapi32 winhttp
        )
        add_dependencies(Infinite infinite-vst3-scanner)
        add_custom_command(TARGET Infinite POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "$<TARGET_FILE:infinite-vst3-scanner>"
                    "$<TARGET_FILE_DIR:Infinite>/infinite-vst3-scanner.exe"
            COMMENT "Copying scanner helper next to Infinite.exe")
    endif()

    # Bundled UI font (Inter, SIL OFL) and icon font (Lucide, ISC), mirroring
    # the macOS MACOSX_PACKAGE_LOCATION "Resources/..." layout above: no app
    # bundle on Windows, so BundledResourcePath() in main.cpp looks for these
    # next to Infinite.exe under Resources\fonts and Resources\icons instead.
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/icons"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-Regular.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-Medium.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-SemiBold.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/AtkinsonHyperlegible/AtkinsonHyperlegible-Regular.ttf"
                ${INFINITE_NOTO_FILES}
                "$<TARGET_FILE_DIR:Infinite>/Resources/fonts/"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/lang"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/lang"
                "$<TARGET_FILE_DIR:Infinite>/Resources/lang"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/external/icons/Lucide/lucide.ttf"
                "$<TARGET_FILE_DIR:Infinite>/Resources/icons/"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/drumkits/infinite-basic"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic"
                "$<TARGET_FILE_DIR:Infinite>/Resources/drumkits/infinite-basic"
        COMMENT "Copying bundled fonts and drum kit next to Infinite.exe")
    target_link_options(Infinite PRIVATE /ENTRY:mainCRTStartup)
elseif(APPLE)
    target_include_directories(Infinite PRIVATE
        ${SYPHON_DIR}/include
        ${SYPHON_DIR}/src
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(Infinite PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}" "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(Infinite PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
        FLAC::FLAC
        ${FONTCONFIG_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        "${ONNXRUNTIME_LIB}"
        ${FFMPEG_LIBS}
    )
    if(INFINITE_ENABLE_GPL_CODECS)
        target_compile_definitions(Infinite PRIVATE INFINITE_ENABLE_GPL_CODECS=1)
    endif()
    set_target_properties(Infinite PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib"
    )
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/icons"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-Regular.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-Medium.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-SemiBold.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/AtkinsonHyperlegible/AtkinsonHyperlegible-Regular.ttf"
                ${INFINITE_NOTO_FILES}
                "$<TARGET_FILE_DIR:Infinite>/Resources/fonts/"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/lang"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/lang"
                "$<TARGET_FILE_DIR:Infinite>/Resources/lang"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/external/icons/Lucide/lucide.ttf"
                "$<TARGET_FILE_DIR:Infinite>/Resources/icons/"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_1024.png"
                "$<TARGET_FILE_DIR:Infinite>/Resources/icons/icon_1024.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ONNXRUNTIME_SO_FILES}
                "$<TARGET_FILE_DIR:Infinite>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FFMPEG_SO_FILES}
                "$<TARGET_FILE_DIR:Infinite>/"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/assets/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/u2netp.onnx"
                "$<TARGET_FILE_DIR:Infinite>/assets/models/u2netp.onnx"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/Resources/drumkits/infinite-basic"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic"
                "$<TARGET_FILE_DIR:Infinite>/Resources/drumkits/infinite-basic"
        COMMENT "Copying bundled fonts, icon, drum kit, ONNX Runtime libraries and model next to Infinite")
    # THIRD_PARTY_NOTICES condition 3 / GPL source-offer condition 2: the
    # FFmpeg and x264 licence texts ship inside the bundle wherever the
    # Linux distributable carries licences, next to the .so files they cover
    # (see ${FFMPEG_ARCHIVE_NAME}/LICENSE.txt for the build's own declared
    # licence, and THIRD_PARTY_NOTICES at the repo root for the full pin).
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/licenses/ffmpeg"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Infinite>/licenses/x264"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/COPYING.GPLv3"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/COPYING.GPLv2"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/COPYING.LGPLv2.1"
                "${ffmpeg_linux_SOURCE_DIR}/LICENSE.txt"
                "$<TARGET_FILE_DIR:Infinite>/licenses/ffmpeg/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/x264/COPYING"
                "$<TARGET_FILE_DIR:Infinite>/licenses/x264/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES"
                "$<TARGET_FILE_DIR:Infinite>/licenses/"
        COMMENT "Copying FFmpeg/x264 third-party licence texts next to Infinite")

    if(INFINITE_ENABLE_VST3)
        # Linux VST3 hosting: src/platform/linux/PluginVST3Linux.cpp mirrors
        # PluginVST3Win.cpp's structure (out-of-process scanning + crash-safety
        # sentinel/blocklist under AppPaths::AppSupportDir(), dlopen instead of
        # LoadLibraryExW). Editors (X11/IRunLoop, task 4.3) open a top-level
        # X11 window and attach the plugin's IPlugView to it, backed by one
        # process-wide Steinberg::Linux::IRunLoop - see the file header
        # comment in PluginVST3Linux.cpp for the full picture.
        #
        # X11 is linked into the Infinite target only, not
        # infinite-vst3-scanner below: scanning is out-of-process and
        # headless (phase-04-vst3.md task 4.5's judgment call) and never
        # opens a display connection, so linking X11 there would be dead
        # weight. `libx11-dev` is already in tools/linux/deps-apt.sh.
        target_compile_definitions(Infinite PRIVATE INFINITE_ENABLE_VST3=1)
        target_include_directories(Infinite PRIVATE
            ${VST3_SDK_DIR}
            ${VST3_SDK_DIR}/pluginterfaces
            ${VST3_SDK_DIR}/public.sdk
        )
        target_sources(Infinite PRIVATE
            src/platform/linux/PluginVST3Linux.cpp
            ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
            ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
            ${VST3_SDK_DIR}/public.sdk/source/common/commoniids.cpp
        )
        target_link_libraries(Infinite PRIVATE X11)

        # Out-of-process scanner helper, mirroring infinite-vst3-scanner.exe on
        # Windows: a disposable process that probes one bundle (or a batch) at
        # a time so a crashing or hanging plugin only costs a dead child.
        # Links PlatformLinux.cpp (for ExecutablePath/AppSupportDir/etc, which
        # PluginVST3Linux.cpp calls into) and its SubjectMaskOnnx.cpp partner
        # (PlatformLinux.cpp forwards the matting entry points there), the same
        # way the Windows scanner target pulls in PlatformWin.cpp + ONNX.
        add_executable(infinite-vst3-scanner
            src/scanner_main_linux.cpp
            src/platform/linux/PluginVST3Linux.cpp
            src/platform/linux/PlatformLinux.cpp
            src/platform/common/SubjectMaskOnnx.cpp
            ${NODE_EDITOR_DIR}/crude_json.cpp
            external/tinyfiledialogs/tinyfiledialogs.c
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
            external/tinyfiledialogs
            ${VST3_SDK_DIR}
            ${VST3_SDK_DIR}/pluginterfaces
            ${VST3_SDK_DIR}/public.sdk
            "${ONNXRUNTIME_INCLUDE_DIR}"
        )
        target_compile_definitions(infinite-vst3-scanner PRIVATE
            INFINITE_ENABLE_VST3=1
            INFINITE_VST3_SCANNER=1
        )
        # dlopen()/dlclose() for the VST3 module itself (PluginVST3Linux.cpp)
        # and for PlatformLinux.cpp's own optional-library probing (libcurl,
        # etc - see linux-parity skill trap 3.4).
        target_link_libraries(infinite-vst3-scanner PRIVATE
            Threads::Threads
            ${CMAKE_DL_LIBS}
            m
            "${ONNXRUNTIME_LIB}"
        )
        set_target_properties(infinite-vst3-scanner PROPERTIES
            BUILD_RPATH_USE_ORIGIN ON
            INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib"
        )
        add_dependencies(Infinite infinite-vst3-scanner)
        add_custom_command(TARGET Infinite POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "$<TARGET_FILE:infinite-vst3-scanner>"
                    "$<TARGET_FILE_DIR:Infinite>/infinite-vst3-scanner"
            COMMENT "Copying scanner helper next to Infinite")
    endif()
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
    set_source_files_properties(src/platform/Platform.mm src/platform/HeadTracker.mm src/audio/AudioFileWriter.mm
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
        "-framework CoreMotion"
        "-framework QuartzCore"
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
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/InfinitePatch.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/InfiniteDevice.icns")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/InfinitePatch.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/InfiniteDevice.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # Bundled UI font (Inter, SIL OFL) and icon font (Lucide, ISC) - see
    # BundledResourcePath() in main.cpp for how the running app finds these
    # at Contents/Resources/fonts and Contents/Resources/icons regardless of
    # launch context (Finder vs. a direct exec). MACOSX_PACKAGE_LOCATION
    # resources are copied during the normal bundle build step, which runs
    # before the ad-hoc codesign POST_BUILD command above, so the signature
    # stays valid - same ordering the .icns files above already rely on.
    set(INFINITE_FONT_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-Medium.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Inter/Inter-SemiBold.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/AtkinsonHyperlegible/AtkinsonHyperlegible-Regular.ttf")
    list(APPEND INFINITE_FONT_FILES ${INFINITE_NOTO_FILES})
    set(INFINITE_ICON_FONT_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/external/icons/Lucide/lucide.ttf")
    target_sources(Infinite PRIVATE ${INFINITE_FONT_FILES} ${INFINITE_ICON_FONT_FILES} ${INFINITE_LANG_FILES})
    set_source_files_properties(${INFINITE_LANG_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/lang")
    set_source_files_properties(${INFINITE_FONT_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
    set_source_files_properties(${INFINITE_ICON_FONT_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/icons")

    # Drum Sequencer bundled kit (generated by tools/make-drumkit.py), found at
    # Contents/Resources/drumkits/infinite-basic via BundledResourcePath().
    set(INFINITE_DRUMKIT_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/01-kick.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/02-snare.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/03-closed-hat.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/04-open-hat.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/05-clap.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/06-low-tom.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/07-high-tom.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/drumkits/infinite-basic/08-bell.wav")
    target_sources(Infinite PRIVATE ${INFINITE_DRUMKIT_FILES})
    set_source_files_properties(${INFINITE_DRUMKIT_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/drumkits/infinite-basic")
endif()

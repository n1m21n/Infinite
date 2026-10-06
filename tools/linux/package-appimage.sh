#!/usr/bin/env bash
# Phase 5 task 5.2: assembles AppDir/ from a built Infinite and produces
# Infinite-x86_64.AppImage. Meant to run inside tools/linux/Dockerfile.release
# (glibc 2.35 floor - see that file's header comment) after
# `tools/linux/build.sh` has produced build-linux/Infinite. See
# docs/plans/linux/phase-05-release.md 5.2 and validation.md's "L3" section.
#
# Usage: tools/linux/package-appimage.sh [BUILD_DIR] [OUT_DIR]
#   BUILD_DIR defaults to build-linux (must already contain a Release build)
#   OUT_DIR   defaults to artifacts-linux
#
# NOTE ON ARCHITECTURE: this repo's local container (tools/linux/local.sh) is
# arm64 on Apple Silicon, so linuxdeploy/appimagetool are pinned by the
# ARCH-detected pair below - x86_64 in CI, aarch64 for a local dry run. An
# aarch64 AppImage produced this way is useless for real distribution; it
# only proves the packaging logic (AppDir layout, exclude list, runtime
# selection). Real x86_64 proof is CI-only (see local.sh's own header note).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-${REPO_ROOT}/build-linux}"
OUT_DIR="${2:-${REPO_ROOT}/artifacts-linux}"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/AppDir"

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64)  TOOL_ARCH="x86_64" ;;
  aarch64) TOOL_ARCH="aarch64" ;;
  arm64)   ARCH="aarch64"; TOOL_ARCH="aarch64" ;;   # macOS-container uname spelling
  *) echo "ERROR: unsupported arch $ARCH" >&2; exit 1 ;;
esac
if [ "$TOOL_ARCH" != "x86_64" ]; then
  echo "WARNING: building a $TOOL_ARCH AppImage as a packaging-logic proxy only." >&2
  echo "         The real release artifact must come from x86_64 CI." >&2
fi

echo "==> Checking required inputs in $BUILD_DIR"
for f in Infinite infinite-vst3-scanner; do
  if [ ! -f "$BUILD_DIR/$f" ]; then
    echo "ERROR: $BUILD_DIR/$f not found - run tools/linux/build.sh first" >&2
    exit 1
  fi
done

# ---------------------------------------------------------------------------
# 1. AppDir layout. The resource lookup is already exeDir/Resources
#    (Platform::ExecutablePath()-relative - see PlatformLinux.cpp), so
#    everything just needs to sit next to usr/bin/Infinite, unmodified.
# ---------------------------------------------------------------------------
echo "==> Assembling AppDir at $APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/mime/packages"
mkdir -p "$APPDIR/usr/share/metainfo"

cp "$BUILD_DIR/Infinite" "$APPDIR/usr/bin/Infinite"
cp "$BUILD_DIR/infinite-vst3-scanner" "$APPDIR/usr/bin/infinite-vst3-scanner"

if [ -d "$BUILD_DIR/Resources" ]; then
  cp -r "$BUILD_DIR/Resources" "$APPDIR/usr/bin/Resources"
else
  echo "ERROR: $BUILD_DIR/Resources missing (fonts/icons) - build incomplete" >&2
  exit 1
fi

mkdir -p "$APPDIR/usr/bin/assets/models"
if [ -f "$BUILD_DIR/assets/models/u2netp.onnx" ]; then
  cp "$BUILD_DIR/assets/models/u2netp.onnx" "$APPDIR/usr/bin/assets/models/u2netp.onnx"
elif [ -f "$REPO_ROOT/assets/models/u2netp.onnx" ]; then
  cp "$REPO_ROOT/assets/models/u2netp.onnx" "$APPDIR/usr/bin/assets/models/u2netp.onnx"
else
  echo "ERROR: u2netp.onnx not found in build or source tree" >&2
  exit 1
fi

# AppRun: linuxdeploy would normally generate one, but Infinite reads its
# resources relative to its own exe path (not $APPDIR), so a plain exec is
# both correct and avoids linuxdeploy's default AppRun setting LD_LIBRARY_PATH
# in a way that could shadow the excluded host GL/X11/audio libs below.
cat > "$APPDIR/AppRun" <<'APPRUN_EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
# Preserve the host's original LD_LIBRARY_PATH so child processes running host
# utilities (zenity, kdialog, xdg-open) can run against host libraries without
# symbol lookup errors from AppImage's bundled libs (Issue #25).
export APPIMAGE_ORIGINAL_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/Infinite" "$@"
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

# infinite.desktop: MimeType glob covers both patch extensions this project
# accepts on open (see src/main.cpp's kPatchExtension comment - `.inf` is
# APPLE-only; Linux, like Windows, *saves* `.infinite` by default but *loads*
# either). %f passes the opened path as argv[1], which main.cpp already
# checks at startup regardless of platform (no PollPendingOpenFile needed for
# this path - see that function's Linux stub for what it does NOT cover).
cat > "$APPDIR/infinite.desktop" <<'DESKTOP_EOF'
[Desktop Entry]
Type=Application
Name=Infinite
Comment=Real-time node-based creative coding
Exec=Infinite %f
Icon=infinite
Terminal=false
Categories=AudioVideo;Audio;Video;
MimeType=application/x-infinite-patch;
DESKTOP_EOF

cp "$REPO_ROOT/assets/linux/infinite.png" "$APPDIR/infinite.png"

# MIME type: glob both extensions patches can carry (see kPatchExtension
# comment above) so "Open With" and Exec=%f work no matter which platform
# saved the file.
cat > "$APPDIR/usr/share/mime/packages/infinite.xml" <<'MIME_EOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-infinite-patch">
    <comment>Infinite patch</comment>
    <glob pattern="*.infinite"/>
    <glob pattern="*.inf"/>
    <icon name="infinite"/>
  </mime-type>
</mime-info>
MIME_EOF

INFINITE_VERSION="$(grep -m1 -oE 'set\(INFINITE_VERSION "[^"]+"' "$REPO_ROOT/CMakeLists.txt" | sed -E 's/.*"([^"]+)".*/\1/')"
INFINITE_VERSION="${INFINITE_VERSION:-0.0.0}"

cat > "$APPDIR/usr/share/metainfo/infinite.appdata.xml" <<METAINFO_EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>com.n1m21n.Infinite</id>
  <name>Infinite</name>
  <summary>Real-time node-based creative coding</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>Proprietary</project_license>
  <description>
    <p>Infinite is a real-time node-based creative coding application for
    2D/3D visuals, audio synthesis and processing.</p>
  </description>
  <launchable type="desktop-id">infinite.desktop</launchable>
  <releases>
    <release version="${INFINITE_VERSION}" date="$(date -u +%Y-%m-%d)"/>
  </releases>
</component>
METAINFO_EOF

# ---------------------------------------------------------------------------
# 1b. onnxruntime and FFmpeg are FetchContent'd into
#     $BUILD_DIR/_deps/{onnxruntime,ffmpeg}_linux-src/lib and Infinite/
#     infinite-vst3-scanner carry an RPATH of literally
#     $ORIGIN/_deps/onnxruntime_linux-src/lib:$ORIGIN/_deps/ffmpeg_linux-src/lib
#     (see CMakeLists.txt's BUILD_RPATH for the Linux target) - correct only
#     while running straight out of the build tree, where that relative
#     layout still exists next to the binary. Once copied into
#     AppDir/usr/bin/, $ORIGIN becomes AppDir/usr/bin and that _deps/...
#     subtree doesn't exist there, so linuxdeploy's own dependency walk
#     can't resolve libonnxruntime.so.1/libavcodec.so.* at all (confirmed in
#     this session: linuxdeploy fails outright with "Could not find
#     dependency: libonnxruntime.so.1" without this fix). Fix: copy the
#     actual .so files into usr/lib/ and repoint RPATH at $ORIGIN/../lib,
#     which is exactly where linuxdeploy will also land every OTHER
#     dependency it collects normally, so both binaries end up sharing one
#     consistent RPATH.
#
#     That alone isn't enough, though: the FFmpeg .so files ship with NO
#     RPATH/RUNPATH of their own at all (verified with `readelf -d` in this
#     session - e.g. libavcodec.so.62 NEEDs libswresample.so.6 but carries no
#     rpath entry to say where to look). In the original _deps tree they only
#     resolve because whatever loads them first (Infinite, via its old
#     $ORIGIN/_deps/... rpath) also implicitly satisfies the linker's search
#     via that same directory being scanned for siblings - but here, once
#     copied to usr/lib on their own, linuxdeploy's *recursive* dependency
#     walk of libavcodec.so.62 itself (a separate pass from resolving
#     Infinite's direct deps) fails with "Could not find dependency:
#     libswresample.so.6" because nothing tells it to look in its own
#     directory. Fix: patchelf --set-rpath '$ORIGIN' onto every copied
#     onnxruntime/FFmpeg .so file too, so each can find its siblings
#     colocated in the same usr/lib directory regardless of who loads it.
# ---------------------------------------------------------------------------
echo "==> Bundling onnxruntime/FFmpeg shared libs and fixing RPATH"
mkdir -p "$APPDIR/usr/lib"
FOUND_PRIVATE_LIBS=0
COPIED_PRIVATE_LIBS=()
for libdir in "$BUILD_DIR"/_deps/onnxruntime_linux-src/lib "$BUILD_DIR"/_deps/ffmpeg_linux-src/lib; do
  if [ -d "$libdir" ]; then
    while IFS= read -r -d '' f; do
      cp -P "$f" "$APPDIR/usr/lib/"
      COPIED_PRIVATE_LIBS+=("$(basename "$f")")
    done < <(find "$libdir" -maxdepth 1 -name '*.so*' -print0)
    FOUND_PRIVATE_LIBS=1
  fi
done
if [ "$FOUND_PRIVATE_LIBS" -eq 0 ]; then
  echo "ERROR: no onnxruntime/ffmpeg _deps lib dirs found under $BUILD_DIR/_deps -" \
       "package-appimage.sh's RPATH fix depends on this layout; check CMakeLists.txt hasn't changed it." >&2
  exit 1
fi

if command -v patchelf >/dev/null 2>&1; then
  patchelf --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/Infinite"
  patchelf --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/infinite-vst3-scanner"
  for name in "${COPIED_PRIVATE_LIBS[@]}"; do
    f="$APPDIR/usr/lib/$name"
    # Symlinks (libswresample.so -> libswresample.so.6.3.102 etc.) aren't ELF
    # files - patchelf on one just follows/fails, so only patch real files.
    if [ -f "$f" ] && [ ! -L "$f" ]; then
      patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true
    fi
  done
else
  echo "ERROR: patchelf not found (add it to tools/linux/deps-apt.sh)" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# 2. Fetch pinned linuxdeploy + appimagetool. SHA256 pins below were computed
#    directly against the real downloaded assets (see the commit message /
#    session notes for the sha256sum output) and cross-checked against
#    GitHub's own reported asset digest, exactly like test-plugins.sh's
#    sha_check does for Surge XT - never hand-typed from a webpage.
#
#    linuxdeploy publishes only a rolling "continuous" release, so its pin is
#    the SHA256 of the binary itself - if upstream republishes it, this script
#    fails loudly on the mismatch rather than silently trusting whatever is
#    there that day. appimagetool has versioned tags, so it is pinned to 1.9.1
#    (the "continuous" build was republished 2026-10-04 and broke the old pin,
#    R572); bump the tag and the hash together, hash from the release's own digest.
# ---------------------------------------------------------------------------
sha_check() {
  local file="$1" expected="$2" actual
  if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$file" | awk '{print $1}')
  else
    actual=$(shasum -a 256 "$file" | awk '{print $1}')
  fi
  if [ "$actual" != "$expected" ]; then
    echo "ERROR: $file SHA256 mismatch: expected $expected, got $actual" >&2
    exit 1
  fi
  echo "==> $file SHA256 OK ($expected)"
}

declare -A LINUXDEPLOY_SHA256=(
  [x86_64]="8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1"
  [aarch64]="__NOT_PINNED__"
)
declare -A APPIMAGETOOL_SHA256=(
  [x86_64]="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
  [aarch64]="__NOT_PINNED__"
)

LINUXDEPLOY="$WORK/linuxdeploy-${TOOL_ARCH}.AppImage"
APPIMAGETOOL="$WORK/appimagetool-${TOOL_ARCH}.AppImage"

echo "==> Downloading linuxdeploy ($TOOL_ARCH)"
curl -fSL -o "$LINUXDEPLOY" \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${TOOL_ARCH}.AppImage"
if [ "${LINUXDEPLOY_SHA256[$TOOL_ARCH]}" != "__NOT_PINNED__" ]; then
  sha_check "$LINUXDEPLOY" "${LINUXDEPLOY_SHA256[$TOOL_ARCH]}"
else
  echo "WARNING: no pinned SHA256 for linuxdeploy-${TOOL_ARCH} (arm64 dry-run only); not verified." >&2
fi
chmod +x "$LINUXDEPLOY"

echo "==> Downloading appimagetool ($TOOL_ARCH)"
curl -fSL -o "$APPIMAGETOOL" \
  "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-${TOOL_ARCH}.AppImage"
if [ "${APPIMAGETOOL_SHA256[$TOOL_ARCH]}" != "__NOT_PINNED__" ]; then
  sha_check "$APPIMAGETOOL" "${APPIMAGETOOL_SHA256[$TOOL_ARCH]}"
else
  echo "WARNING: no pinned SHA256 for appimagetool-${TOOL_ARCH} (arm64 dry-run only); not verified." >&2
fi
chmod +x "$APPIMAGETOOL"

# Containers have no FUSE (validation.md's L3 recipe) - extract both tools
# rather than trying to mount them.
( cd "$WORK" && "$LINUXDEPLOY" --appimage-extract >/dev/null && mv squashfs-root linuxdeploy.extracted )
( cd "$WORK" && "$APPIMAGETOOL" --appimage-extract >/dev/null && mv squashfs-root appimagetool.extracted )
LINUXDEPLOY_BIN="$WORK/linuxdeploy.extracted/AppRun"
APPIMAGETOOL_BIN="$WORK/appimagetool.extracted/AppRun"

# ---------------------------------------------------------------------------
# 3. linuxdeploy collects shared-lib dependencies of Infinite and the
#    scanner into usr/lib/. Excluded: everything that must come from the
#    host, per phase-05-release.md and validation.md's minimal-install trap
#    (Audacity 4.0 precedent - never bundle GL). linuxdeploy's own
#    --exclude-library takes a glob against the *filename*, matched against
#    the community excludelist
#    (github.com/AppImage/pkg2appimage/blob/master/excludelist) but kept
#    explicit here rather than fetched at build time, per the plan.
# ---------------------------------------------------------------------------
EXCLUDE_LIBS=(
  # GL/EGL/GLX/OpenGL - GLFW dlopen()s these at runtime (glad loads through
  # glfwGetProcAddress); Infinite must never link them directly (see the
  # "Assert no direct OpenGL library linkage" CI step and the minimal-install
  # trap note above).
  "libGL.so*" "libEGL.so*" "libGLX.so*" "libOpenGL.so*"
  "libGLdispatch.so*" "libGLESv1_CM.so*" "libGLESv2.so*"
  # Audio: the host's actual sound stack (ALSA is linked directly per
  # CMakeLists.txt's comment on the sequencer MIDI API having no dlopen
  # equivalent, but its runtime .so must still come from the host, not be
  # bundled - a bundled libasound talking to a mismatched host PipeWire/Pulse
  # ALSA-shim is a worse failure mode than requiring the host's own).
  "libasound.so*" "libpulse.so*" "libpulse-simple.so*" "libjack.so*"
  # Fonts: fontconfig must resolve against the HOST's font configuration and
  # installed fonts, not a bundled one frozen at build time - see
  # linux-parity skill §3.1. FreeType is bundled only if not already present
  # on very old systems; exclude the common case.
  "libfontconfig.so*" "libfreetype.so*"
  # Windowing: X11/XCB/Wayland must be the host's own (protocol/ABI must
  # match the host compositor exactly); GLFW picks X11 or Wayland at
  # runtime via dlopen, matching the existing CI "no direct libX11" check.
  "libX11.so*" "libX11-xcb.so*" "libxcb*.so*" "libXau.so*" "libXdmcp.so*"
  "libXrandr.so*" "libXinerama.so*" "libXcursor.so*" "libXi.so*"
  "libXext.so*" "libXfixes.so*" "libXrender.so*" "libXss.so*" "libXtst.so*"
  "libwayland-client.so*" "libwayland-cursor.so*" "libwayland-egl.so*"
  "libxkbcommon.so*" "libxkbcommon-x11.so*"
  # glibc itself: the whole point of the Ubuntu-22.04 (glibc 2.35) build
  # floor is that users' own glibc satisfies this AppImage; bundling glibc
  # is unsupported by the classic AppImage approach and actively harmful
  # (mismatched NSS/dlopen behavior).
  "libc.so*" "libm.so*" "libpthread.so*" "libdl.so*" "librt.so*" "ld-linux*"
)

EXCLUDE_ARGS=()
for lib in "${EXCLUDE_LIBS[@]}"; do
  EXCLUDE_ARGS+=(--exclude-library="$lib")
done

echo "==> Running linuxdeploy"
export VERSION="$INFINITE_VERSION"
( cd "$WORK" && "$LINUXDEPLOY_BIN" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/Infinite" \
    --executable "$APPDIR/usr/bin/infinite-vst3-scanner" \
    --desktop-file "$APPDIR/infinite.desktop" \
    --icon-file "$APPDIR/infinite.png" \
    "${EXCLUDE_ARGS[@]}" )

# ---------------------------------------------------------------------------
# 4. appimagetool with the static type2-runtime, so the produced AppImage
#    does not depend on libfuse.so.2 at all (current appimagetool
#    "continuous" releases embed it already - verified below with ldd on
#    the produced file's runtime rather than assumed).
# ---------------------------------------------------------------------------
OUT_APPIMAGE="$OUT_DIR/Infinite-${TOOL_ARCH}.AppImage"
if [ "$TOOL_ARCH" = "x86_64" ]; then
  OUT_APPIMAGE="$OUT_DIR/Infinite-x86_64.AppImage"
fi
rm -f "$OUT_APPIMAGE"

echo "==> Running appimagetool -> $OUT_APPIMAGE"
ARCH="$TOOL_ARCH" "$APPIMAGETOOL_BIN" "$APPDIR" "$OUT_APPIMAGE"

chmod +x "$OUT_APPIMAGE"
echo "==> Built $OUT_APPIMAGE ($(du -h "$OUT_APPIMAGE" | cut -f1))"

echo "==> Verifying embedded runtime has no libfuse.so.2 dependency"
RUNTIME_BYTES=4194304   # 4 MiB covers every published type2-runtime header
head -c "$RUNTIME_BYTES" "$OUT_APPIMAGE" > "$WORK/runtime-head"
if command -v ldd >/dev/null 2>&1 && file "$WORK/runtime-head" 2>/dev/null | grep -qi "ELF"; then
  if ldd "$WORK/runtime-head" 2>/dev/null | grep -qi "libfuse"; then
    echo "ERROR: embedded runtime links libfuse - not the static type2-runtime" >&2
    exit 1
  fi
  echo "OK: embedded runtime has no libfuse.so.2 dependency (or is static/non-dynamic)."
else
  echo "NOTE: could not run ldd directly on the runtime header (expected for a"
  echo "      statically-linked/non-native-arch runtime) - checked instead via"
  echo "      'strings' for a libfuse.so reference:"
  if strings "$WORK/runtime-head" 2>/dev/null | grep -qi "libfuse\.so"; then
    echo "ERROR: found a libfuse.so reference in the runtime header" >&2
    exit 1
  fi
  echo "OK: no libfuse.so string found in the runtime header."
fi

echo "==> Verifying ldd on the bundled Infinite binary for GL libs (minimal-install trap)"
if ldd "$APPDIR/usr/bin/Infinite" 2>/dev/null | grep -E "libOpenGL|libGL\.so|libGLX|libEGL"; then
  echo "ERROR: Infinite links a GL library directly - see audacity#12093" >&2
  exit 1
fi
echo "OK: no GL library directly linked."

echo "==> Done."

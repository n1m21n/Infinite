#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-linux}"
CC_FAMILY="${CC_FAMILY:-clang}"

if [ "$CC_FAMILY" = "clang" ]; then
  export CC="${CC:-clang}"
  export CXX="${CXX:-clang++}"
elif [ "$CC_FAMILY" = "gcc" ]; then
  export CC="${CC:-gcc}"
  export CXX="${CXX:-g++}"
fi

CMAKE_FLAGS=(
  -S .
  -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
)

# SANITIZE=address,undefined (or thread) builds an instrumented binary; use a
# separate BUILD_DIR so it does not clobber the release tree.
if [ -n "${SANITIZE:-}" ]; then
  CMAKE_FLAGS+=(-DINFINITE_SANITIZE="$SANITIZE" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
fi

# VULKAN=1 builds the engine's Vulkan backend (INFINITE_RHI=vulkan; lavapipe in this container).
if [ -n "${VULKAN:-}" ]; then
  CMAKE_FLAGS+=(-DINFINITE_VULKAN=ON)
fi

if command -v ccache >/dev/null 2>&1; then
  CMAKE_FLAGS+=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
fi

echo "==> Configuring with ${CC} / ${CXX} in ${BUILD_DIR}..."
cmake "${CMAKE_FLAGS[@]}"

echo "==> Building Infinite..."
# TARGET builds one target (default: all). JOBS caps the compile jobs (an 8 GB host cannot take ninja's default).
ninja -C "$BUILD_DIR" ${JOBS:+-j "$JOBS"} ${TARGET:-}

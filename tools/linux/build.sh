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

if command -v ccache >/dev/null 2>&1; then
  CMAKE_FLAGS+=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
fi

echo "==> Configuring with ${CC} / ${CXX} in ${BUILD_DIR}..."
cmake "${CMAKE_FLAGS[@]}"

echo "==> Building Infinite..."
ninja -C "$BUILD_DIR"

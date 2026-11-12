#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_NAME="infinite-linux-dev"

if ! docker info >/dev/null 2>&1; then
  echo "ERROR: Docker / OrbStack is not running." >&2
  echo "Please install OrbStack (brew install --cask orbstack) and open it once." >&2
  exit 1
fi

# Passed into the container when set on the host (VULKAN=1 JOBS=3 tools/linux/local.sh build).
PASS_ENV=(-e VULKAN -e JOBS -e TARGET -e BUILD_DIR -e SANITIZE -e INFINITE_RHI)
# AB_DIR mounts the Mac sweep's case directory at the same path, read-only (tools/linux/vk-cases.sh).
if [ -n "${AB_DIR:-}" ]; then
  PASS_ENV+=(-e AB_DIR -e CASE_TIMEOUT -v "$AB_DIR":"$AB_DIR":ro)
fi

cmd="${1:-shell}"
shift || true

build_image_if_needed() {
  if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "==> Building local container image ${IMAGE_NAME}..."
    docker build -t "$IMAGE_NAME" -f "$REPO_ROOT/tools/linux/Dockerfile" "$REPO_ROOT"
  fi
}

build_image_if_needed

case "$cmd" in
  build)
    docker run --rm "${PASS_ENV[@]}" -v "$REPO_ROOT":/src -v infinite-ccache:/root/.ccache -w /src "$IMAGE_NAME" tools/linux/build.sh "$@"
    ;;
  test)
    docker run --rm "${PASS_ENV[@]}" -v "$REPO_ROOT":/src -v infinite-ccache:/root/.ccache -w /src "$IMAGE_NAME" tools/linux/xvfb-harness.sh "$@"
    ;;
  shell)
    docker run --rm -it -v "$REPO_ROOT":/src -v infinite-ccache:/root/.ccache -w /src "$IMAGE_NAME" bash
    ;;
  *)
    docker run --rm "${PASS_ENV[@]}" -v "$REPO_ROOT":/src -v infinite-ccache:/root/.ccache -w /src "$IMAGE_NAME" "$cmd" "$@"
    ;;
esac

#!/usr/bin/env bash
# Row 9.5: render A/B cases with the engine's Vulkan backend on lavapipe (software Vulkan), inside the container.
# The patches are the Mac sweep's ~/.cache/infinite-sweep/ab/<case>/inf_settle.inf, mounted at the same path.
#   tools/linux/vk-cases.sh OUT_DIR SIZE case [case ...]      e.g. artifacts-linux/vk95 256x256 cornell light_area
set -uo pipefail
OUT="$1"; SIZE="$2"; shift 2
AB="${AB_DIR:?AB_DIR = the mounted sweep ab directory}"
BIN="${BIN:-build-linux/Infinite}"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe INFINITE_NO_UPDATE_CHECK=1
export INFINITE_RHI=vulkan INFINITE_LIVE_ENGINE="${INFINITE_LIVE_ENGINE:-15}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$(mktemp -d)}" DISPLAY=":99"
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill "$XVFB_PID" 2>/dev/null' EXIT
sleep 1
vulkaninfo --summary 2>/dev/null | grep -E "deviceName|apiVersion" | head -2
mkdir -p "$OUT"
for c in "$@"; do
  t0=$(date +%s)
  mkdir -p "$OUT/f_$c"
  rm -f "$OUT/$c.png"
  "$BIN" --frame "$AB/$c/inf_settle.inf" 0 "$OUT/f_$c/" --size "$SIZE" --timeout "${CASE_TIMEOUT:-1100}" > "$OUT/$c.log" 2>&1
  rc=$?
  if [ -f "$OUT/f_$c/frame_00000.png" ]; then mv "$OUT/f_$c/frame_00000.png" "$OUT/$c.png"; fi
  echo "VKCASE $c rc=$rc $(( $(date +%s) - t0 )) s $( [ -f "$OUT/$c.png" ] && echo picture || echo 'no picture' )"
done

# 9.6 Vulkan speed on real Windows and Linux GPUs: run sheet (owner's hardware)

Run this once 9.3 and 9.4 are built, on each Windows or Linux machine with a discrete or integrated GPU.

## Setup

| Step | Windows | Linux |
|---|---|---|
| Driver | latest vendor driver (NVIDIA, AMD or Intel) | Mesa 24+ (AMD/Intel) or the NVIDIA proprietary driver |
| Check Vulkan | `vulkaninfo --summary` | `vulkaninfo --summary` |
| Build | `python scripts/ibuild.py build --mode release` with `-DINFINITE_VULKAN=ON` | same |

## Runs

| # | Command | What it measures |
|---|---|---|
| 1 | `INFINITE_ENGINETEST=1 Infinite` | R1 row: GL vs Vulkan ms at 1080p, 8 lights |
| 2 | `INFINITE_ENGINETEST=1 INFINITE_ENGINETEST_RT=1 INFINITE_ENGINETEST_GI=1 Infinite` | live ray shadows and GI ms |
| 3 | `python3 tools/blender-ab/sweep.py --engine live --tag v_<gpu> run cornell interior materials deer bed` | live FLIP and time per case |
| 4 | `python3 tools/blender-ab/sweep.py --engine settle --spp 16 --tag v_<gpu> run cornell interior materials deer` | settle FLIP and time |

## Numbers to send back

| Field | Example |
|---|---|
| OS, GPU, driver | Windows 11, RTX 3060, 566.36 |
| `VK_KHR_ray_query` present | yes / no (compute BVH used) |
| R1: GL ms, Vulkan ms, speedup | 9.1, 4.2, 2.17x |
| Live RT+GI frame ms | 21.4 |
| Sweep 3 and 4: the `*_live.json` and `*_settle_s16.json` files | attach |
| Any `LastError()` line Render 3D shows | text |

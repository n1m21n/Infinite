// fork: global "Preview quality" (mini-viewport resolution divisor). 0 = full, 1 = 1/2, 2 = 1/4.
#pragma once
#include <algorithm>
#include <cstdlib>
inline int gPreviewQuality = 0;
// A/B switch for measurements: INFINITE_FORK_LEGACY=1 restores pre-fork behaviour (no viewport throttle/cull, sync readbacks).
inline bool ForkLegacyMode() { static const bool v = std::getenv("INFINITE_FORK_LEGACY") != nullptr; return v; }
inline float PreviewQualityScale() { return 1.0f / (float)(1 << std::clamp(gPreviewQuality, 0, 2)); }

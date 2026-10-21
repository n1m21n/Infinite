#pragma once

// INFINITE_NO_FMA, first statement of a function body: the compiler must not
// fuse a multiply and an add in that body into one rounded op. Clang on arm64
// fuses by default (x86-64 builds have no FMA to fuse into, MSVC does not by
// default), which makes the same DSP round differently per platform. The
// four-voice lane kernels (dsp/VoiceLanes.h) never fuse, so every scalar
// kernel they mirror carries this, and lanes and scalar agree to the bit.
#if defined(__clang__)
#define INFINITE_NO_FMA _Pragma("clang fp contract(off)")
#else
#define INFINITE_NO_FMA
#endif

#pragma once

// std::_Exit skips atexit, and atexit is where the llvm profile runtime writes
// its .profraw. Every _Exit path calls this first, so a coverage build
// (-DINFINITE_COVERAGE=ON, tools/bughunt/coverage.py) records headless runs
// too. A no-op in every other build.
#if defined(INFINITE_COVERAGE_BUILD)
extern "C" int __llvm_profile_write_file(void);
inline void FlushCoverageProfile() { __llvm_profile_write_file(); }
#else
inline void FlushCoverageProfile() {}
#endif

#pragma once

// Self-test for the PortableFft backend, kept out of main.cpp deliberately.
//
// Two reasons it lives in its own translation unit rather than beside the
// other fixtures: main.cpp is ~37k lines and adding this function to it was
// enough to make MSVC's x64 code generator hit an internal compiler error, and
// PortableFft.h is a header-only FFT that has no business being inlined into
// the UI translation unit just so a test can call it.
//
// Runs on every platform, including Apple, so that a Mac build gates the
// non-Apple audio backend rather than leaving it to Windows CI alone.
bool RunPortableFftFixture();

#pragma once
// plugin_shell_check's AppKit helpers (ShellCheckMac.mm).
#include <cstdint>
#include <vector>

namespace ViewCheck
{
   // Offscreen borderless window; returns its content view (NSView*) and the backing scale.
   void* NewParent(int w, int h, double* scale);
   void FreeParent(void* parent);
   // The editor's NSView once attached (the parent's first subview).
   void* Child(void* parent);
   // The frame the view shows (its layer's IOSurface): size in pixels and a 96 x 96 sample grid.
   bool Surface(void* view, int* w, int* h, std::vector<uint32_t>* samples);
   // NSEvents sent to the view. mods are NSEventModifierFlags; kind 0 move, 1 down, 2 up.
   void Key(void* view, unsigned short keyCode, const char* chars, unsigned long mods, bool down);
   void Flags(void* view, unsigned short keyCode, unsigned long mods);
   void Mouse(void* view, int kind, double x, double y);
   // Runs the main run loop (the view's timer and the processor/controller relay) for ms.
   void Pump(int ms);
}

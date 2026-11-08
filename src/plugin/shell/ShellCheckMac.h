#pragma once
// plugin_shell_check's AppKit helpers (ShellCheckMac.mm).
#include <cstdint>
#include <string>
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
   // The view's native overlay (step 2.4): the visible label's text ("" when hidden) and the
   // visible button's title ("" when hidden); Press clicks that button.
   std::string Label(void* view);
   std::string Button(void* view);
   void Press(void* view);
   // The licence sheet's key field (step 2.6): types text into it and presses Return. False when
   // no editable field is showing.
   bool TypeKey(void* view, const std::string& text);
   // On-screen windows owned by process pid (CGWindowList, layer 0).
   int OnScreenWindows(int pid);
   // The login screen is up: the window server stops updating what is on screen.
   bool ScreenLocked();
   // Runs the main run loop (the view's timer and the processor/controller relay) for ms.
   void Pump(int ms);
}

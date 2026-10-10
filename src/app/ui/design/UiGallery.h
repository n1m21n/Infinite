// Component gallery (plan G27): every component class in its resting states, on one opaque full-window page.
// Not a menu item: it is reachable only through the INFINITE_UIGALLERY self-test/golden harness, so no
// shipped build exposes it. tools/design/golden.py renders it in light and dark and diffs against goldens.
#pragma once
#include "imgui.h"

namespace UiGallery
{
   // Draws the gallery over the whole display for this frame.
   void Draw();
}

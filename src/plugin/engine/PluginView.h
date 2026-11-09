#pragma once
// Engine side of the plugin view (docs/plans/vst-au/README.md 8.3, step 2.3): sizes the hidden
// window to the view, feeds the view's mouse and keys to ImGui, and reads each drawn frame back into
// the shared-memory triple buffer (src/plugin/ipc/PluginFrames.h). Main thread only. Every call is a
// no-op outside plugin mode or while no view is open.
#include <cstddef>
#include <cstdint>

struct GLFWwindow;

namespace PluginView
{
   // HostAudio's message loop: kMsgView, kMsgViewInput and kMsgPopout.
   void HandleMessage(uint32_t type, const uint8_t* data, size_t size);

   // A view is open and showing frames (not popped out).
   bool Open();

   // Pop-out (step 2.4): the hidden window becomes a real window on screen, at full frame rate, and
   // the view stops reading frames back and says so. Asked from the top bar, the view ("Bring back")
   // or the window's close button; applied at the next BeginFrame. No-op outside plugin mode.
   void RequestPopped(bool popped);
   bool Popped();

   // Picture/canvas switch (step 2.5): the view shows the first Output picture full size instead of
   // the canvas. A user switch tells the shell, which saves it in the project; the saved value comes
   // back as kMsgPicture.
   void SetPicture(bool on);
   bool Picture();

   // After ImGui_ImplGlfw_NewFrame, before ImGui::NewFrame: shows or hides the window for a pop-out
   // request, applies the view's size to the hidden window and queues the view's input on ImGui's
   // event queue.
   void BeginFrame(GLFWwindow* window);

   // After ImGui_ImplOpenGL3_RenderDrawData into the default framebuffer (fbW x fbH pixels): starts
   // this frame's readback and publishes the previous one. Also tells the view about cursor changes.
   void Capture(int fbW, int fbH);

   // Stats line: frames published and the mean readback cost (map + copy) in microseconds.
   uint64_t FramesPublished();
   uint64_t MeanReadbackUs();
}

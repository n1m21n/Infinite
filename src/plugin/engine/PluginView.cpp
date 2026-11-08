#include "plugin/engine/PluginView.h"

#include "core/gl3.h"
#include "platform/Platform.h"
#include "platform/common/Ipc.h"
#include "plugin/engine/HostAudio.h"
#include "plugin/ipc/PluginFrames.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <vector>

// Not in the backend's header, but external: GLFW key -> ImGuiKey, the mapping the window uses.
ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);

namespace PluginView
{
   namespace
   {
      PluginFrames::ViewState gView;
      bool gSizeApplied = true;
      // With no view open the hidden window shrinks: its drawables at 1600x1000 @2x were ~120 MB
      // of IOSurfaces per engine doing nothing (step 2.7, M-9). The view's size comes back on open.
      bool gShrunk = false;
      constexpr int kClosedW = 400, kClosedH = 250;
      std::vector<PluginFrames::InputEvent> gInput;

      Ipc::SharedMemory gRegion;
      PluginFrames::Header* gFrames = nullptr;
      uint32_t gBack = 0;
      uint64_t gSeq = 0;

      // Two pack buffers: frame N is read into one at the end of its frame and mapped at the start of
      // the next (after the swap), so the map finds the transfer done without waiting a whole frame.
      GLuint gPbo[2] = {};
      size_t gPboBytes[2] = {};
      int gPboW[2] = {}, gPboH[2] = {};
      bool gPboFull[2] = {};
      int gPboNext = 0;

      int gCursor = -1;
      bool gPopped = false, gWantPopped = false;
      bool gPicture = false;
      uint64_t gReadbackUsSum = 0, gReadbacks = 0;

      void DropPbos()
      {
         for (int i = 0; i < 2; i++)
         {
            if (gPbo[i])
               glDeleteBuffers(1, &gPbo[i]);
            gPbo[i] = 0;
            gPboBytes[i] = 0;
            gPboFull[i] = false;
         }
      }

      void CloseRegion()
      {
         Ipc::SharedMemoryClose(&gRegion);
         gFrames = nullptr;
      }

      // A region whose buffers hold `bytes`; a new one (and its name to the view) when it does not.
      bool EnsureRegion(uint64_t bytes)
      {
         if (gFrames && gFrames->capacity >= bytes)
            return true;
         CloseRegion();
         // Headroom, so dragging the view a little larger does not make a region per frame.
         const uint64_t capacity = ((bytes + bytes / 4) + (1u << 20) - 1) & ~uint64_t((1u << 20) - 1);
         const std::string name = Ipc::SharedMemoryUniqueName("infF");
         if (!Ipc::SharedMemoryCreate(name, PluginFrames::RegionBytes(capacity), &gRegion))
            return false;
         gFrames = static_cast<PluginFrames::Header*>(gRegion.data);
         PluginFrames::Init(gFrames, capacity);
         gBack = 0;
         PluginFrames::FramesInfo info;
         std::snprintf(info.name, sizeof info.name, "%s", name.c_str());
         info.bytes = gRegion.size;
         PluginEngine::SendToShell(PluginFrames::kMsgFrames, &info, sizeof info, 1000);
         return true;
      }

      // Start of a frame: the frame read back at the end of the last one, flipped to top-down rows,
      // into the triple buffer.
      void Drain()
      {
         const int prev = 1 - gPboNext;
         GLint prevPack = 0;
         glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPack);
         if (gPboFull[prev])
         {
            gPboFull[prev] = false;
            const uint64_t t0 = Ipc::MonotonicNs();
            const int w = gPboW[prev], h = gPboH[prev];
            const size_t stride = size_t(w) * 4;
            if (EnsureRegion(uint64_t(stride) * uint64_t(h)))
            {
               glBindBuffer(GL_PIXEL_PACK_BUFFER, gPbo[prev]);
               if (const uint8_t* src = static_cast<const uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(stride * size_t(h)), GL_MAP_READ_BIT)))
               {
                  uint8_t* dst = PluginFrames::Buffer(gFrames, gBack);
                  for (int y = 0; y < h; y++)
                     std::memcpy(dst + size_t(y) * stride, src + size_t(h - 1 - y) * stride, stride);
                  glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                  PluginFrames::BufferInfo& info = gFrames->info[gBack];
                  info.width = uint32_t(w);
                  info.height = uint32_t(h);
                  info.stride = uint32_t(stride);
                  info.seq = ++gSeq;
                  gBack = PluginFrames::Publish(gFrames, gBack);
                  gReadbackUsSum += (Ipc::MonotonicNs() - t0) / 1000;
                  gReadbacks++;
               }
            }
         }
         glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(prevPack));
      }

      void ApplyMods(ImGuiIO& io, uint32_t mods)
      {
         io.AddKeyEvent(ImGuiMod_Ctrl, (mods & GLFW_MOD_CONTROL) != 0);
         io.AddKeyEvent(ImGuiMod_Shift, (mods & GLFW_MOD_SHIFT) != 0);
         io.AddKeyEvent(ImGuiMod_Alt, (mods & GLFW_MOD_ALT) != 0);
         io.AddKeyEvent(ImGuiMod_Super, (mods & GLFW_MOD_SUPER) != 0);
      }

      uint32_t CursorFor(ImGuiMouseCursor c)
      {
         switch (c)
         {
         case ImGuiMouseCursor_None: return PluginFrames::kCursorHidden;
         case ImGuiMouseCursor_TextInput: return PluginFrames::kCursorText;
         case ImGuiMouseCursor_ResizeAll: return PluginFrames::kCursorResizeAll;
         case ImGuiMouseCursor_ResizeNS: return PluginFrames::kCursorResizeNS;
         case ImGuiMouseCursor_ResizeEW: return PluginFrames::kCursorResizeEW;
         case ImGuiMouseCursor_ResizeNESW: return PluginFrames::kCursorResizeNESW;
         case ImGuiMouseCursor_ResizeNWSE: return PluginFrames::kCursorResizeNWSE;
         case ImGuiMouseCursor_Hand: return PluginFrames::kCursorHand;
         case ImGuiMouseCursor_NotAllowed: return PluginFrames::kCursorNotAllowed;
         default: return PluginFrames::kCursorArrow;
         }
      }
   }

   void HandleMessage(uint32_t type, const uint8_t* data, size_t size)
   {
      if (type == PluginFrames::kMsgView && size >= sizeof(PluginFrames::ViewState))
      {
         PluginFrames::ViewState v;
         std::memcpy(&v, data, sizeof v);
         v.width = std::clamp<uint32_t>(v.width, 64, 8192);
         v.height = std::clamp<uint32_t>(v.height, 64, 8192);
         gSizeApplied = gSizeApplied && v.width == gView.width && v.height == gView.height;
         gView = v;
         if (!gView.open)
         {
            // No view: no readback, and the region and pack buffers go (a closed view costs nothing).
            gInput.clear();
            gCursor = -1;
            DropPbos();
            CloseRegion();
         }
      }
      else if (type == PluginFrames::kMsgPopout && size >= sizeof(uint32_t))
      {
         uint32_t on = 0;
         std::memcpy(&on, data, sizeof on);
         gWantPopped = on != 0;
      }
      else if (type == PluginFrames::kMsgPicture && size >= sizeof(uint32_t))
      {
         uint32_t on = 0;
         std::memcpy(&on, data, sizeof on);
         gPicture = on != 0;
      }
      else if (type == PluginFrames::kMsgViewInput && gView.open && !gPopped)
      {
         const size_t n = size / sizeof(PluginFrames::InputEvent);
         const size_t at = gInput.size();
         gInput.resize(at + n);
         std::memcpy(gInput.data() + at, data, n * sizeof(PluginFrames::InputEvent));
      }
   }

   bool Open() { return Platform::PluginModeActive() && gView.open != 0 && !gPopped; }

   void RequestPopped(bool popped)
   {
      if (Platform::PluginModeActive())
         gWantPopped = popped;
   }

   bool Popped() { return gPopped; }

   void SetPicture(bool on)
   {
      if (!Platform::PluginModeActive() || on == gPicture)
         return;
      gPicture = on;
      const uint32_t v = on ? 1 : 0;
      PluginEngine::SendToShell(PluginFrames::kMsgPictureMode, &v, sizeof v, 500);
   }

   bool Picture() { return Platform::PluginModeActive() && gPicture; }

   namespace
   {
      void ApplyPopped(GLFWwindow* window)
      {
         if (!window || gWantPopped == gPopped)
            return;
         gPopped = gWantPopped;
         if (gPopped)
         {
            // The window keeps what the view showed: its size in points, centred on the main screen.
            const int w = gView.open ? int(gView.width) : 1280, h = gView.open ? int(gView.height) : 800;
            glfwSetWindowSize(window, w, h);
            gShrunk = false;
            if (GLFWmonitor* m = glfwGetPrimaryMonitor())
            {
               int mx = 0, my = 0, mw = 0, mh = 0;
               glfwGetMonitorWorkarea(m, &mx, &my, &mw, &mh);
               glfwSetWindowPos(window, mx + std::max(0, (mw - w) / 2), my + std::max(0, (mh - h) / 2));
            }
            gPboFull[0] = gPboFull[1] = false; // no stale frame when it comes back
            Platform::AllowPluginEngineWindows();
            glfwShowWindow(window);
            glfwFocusWindow(window);
         }
         else
         {
            glfwHideWindow(window);
            gSizeApplied = false; // back to the view's size
            gInput.clear();
         }
         const uint32_t on = gPopped ? 1 : 0;
         PluginEngine::SendToShell(PluginFrames::kMsgPoppedOut, &on, sizeof on, 500);
      }
   }

   void BeginFrame(GLFWwindow* window)
   {
      if (!Platform::PluginModeActive())
         return;
      ApplyPopped(window);
      if (!Open())
      {
         if (window && !gPopped && !gShrunk)
         {
            glfwSetWindowSize(window, kClosedW, kClosedH);
            gShrunk = true;
            gSizeApplied = false;
         }
         return;
      }
      Drain();
      if (!gSizeApplied && window)
      {
         // The hidden window takes the view's size in points; its framebuffer follows the backing
         // scale of the screen it is on, and ImGui lays out in points as in the app.
         glfwSetWindowSize(window, int(gView.width), int(gView.height));
         gSizeApplied = true;
         gShrunk = false;
      }
      if (gInput.empty())
         return;
      ImGuiIO& io = ImGui::GetIO();
      const float ps = ImGui_ImplGlfw_GetPointScale();
      for (const PluginFrames::InputEvent& e : gInput)
      {
         switch (e.type)
         {
         case PluginFrames::kInMouseMove:
            io.AddMousePosEvent(e.x / ps, e.y / ps);
            break;
         case PluginFrames::kInMouseButton:
            ApplyMods(io, e.mods);
            io.AddMousePosEvent(e.x / ps, e.y / ps);
            if (e.a >= 0 && e.a < ImGuiMouseButton_COUNT)
               io.AddMouseButtonEvent(e.a, e.b != 0);
            break;
         case PluginFrames::kInScroll:
            io.AddMouseWheelEvent(e.x, e.y);
            break;
         case PluginFrames::kInKey:
         {
            ApplyMods(io, e.mods);
            if (e.b == 2)
               break; // ImGui repeats a held key itself
            const ImGuiKey k = ImGui_ImplGlfw_KeyToImGuiKey(e.a, 0);
            if (k != ImGuiKey_None)
               io.AddKeyEvent(k, e.b != 0);
            break;
         }
         case PluginFrames::kInText:
            if (e.a > 0)
               io.AddInputCharacter(uint32_t(e.a));
            break;
         case PluginFrames::kInFocus:
            io.AddFocusEvent(e.b != 0);
            break;
         case PluginFrames::kInLeave:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
         default:
            break;
         }
      }
      gInput.clear();
   }

   void Capture(int fbW, int fbH)
   {
      if (!Open() || fbW <= 0 || fbH <= 0)
         return;

      const uint32_t cursor = CursorFor(ImGui::GetMouseCursor());
      if (int(cursor) != gCursor)
      {
         gCursor = int(cursor);
         PluginEngine::SendToShell(PluginFrames::kMsgViewCursor, &cursor, sizeof cursor, 100);
      }

      GLint prevPack = 0, prevRead = 0;
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPack);
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 4);

      // This frame into one buffer.
      const int cur = gPboNext;
      const size_t bytes = size_t(fbW) * size_t(fbH) * 4;
      if (!gPbo[cur])
         glGenBuffers(1, &gPbo[cur]);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, gPbo[cur]);
      if (gPboBytes[cur] != bytes)
      {
         glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_READ);
         gPboBytes[cur] = bytes;
      }
      glReadPixels(0, 0, fbW, fbH, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
      gPboW[cur] = fbW;
      gPboH[cur] = fbH;
      gPboFull[cur] = true;
      gPboNext = 1 - cur;

      glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(prevPack));
      glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(prevRead));
   }

   uint64_t FramesPublished() { return gSeq; }
   uint64_t MeanReadbackUs() { return gReadbacks ? gReadbackUsSum / gReadbacks : 0; }
}

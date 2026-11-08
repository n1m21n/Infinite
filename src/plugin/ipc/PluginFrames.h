#pragma once
// Engine -> plugin view pixels (docs/plans/vst-au/README.md 8.3, step 2.3).
//
// The engine draws its UI as in the app, into its hidden window, then reads the frame back (two PBOs,
// mapped at the start of the next frame, after the swap, so the map does not wait on the GPU) and copies it top-down into a shared-memory
// triple buffer. The plugin view in the host opens the same region and shows the newest frame on
// its 120 Hz timer. Neither side ever waits for the other:
//
//   engine (writer) owns `back`    fill buffer[back], then back <- exchange(middle, back | dirty)
//   view   (reader) owns `front`   if middle is dirty: front <- exchange(middle, front)
//
// The engine creates the region (it knows the frame size) and makes a new, larger one when a frame
// no longer fits; each new region's name reaches the view as kMsgFrames, through the processor.
// Pixels are 8-bit BGRA, opaque, rows top-down (CoreGraphics kCGImageAlphaNoneSkipFirst |
// kCGBitmapByteOrder32Little; Windows BI_RGB; X11 ZPixmap depth 24).
//
// View input travels the other way as InputEvent arrays: view -> controller -> processor (IMessage)
// -> engine (kMsgViewInput). Positions are in view points, top-left origin; keys are GLFW key codes
// and mods GLFW mod bits, so the engine feeds them to ImGui exactly as its own window would.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace PluginFrames
{
   constexpr uint32_t kMagic = 0x46464E49; // "INFF"
   constexpr uint32_t kVersion = 1;
   constexpr uint32_t kBuffers = 3;
   constexpr uint32_t kDirty = 4;

   struct BufferInfo
   {
      uint32_t width = 0, height = 0, stride = 0; // pixels, pixels, bytes per row
      uint32_t pad = 0;
      uint64_t seq = 0; // frame number, 1-based
   };

   struct alignas(64) Header
   {
      uint32_t magic, version;
      uint64_t capacity;     // bytes per buffer
      uint64_t bufferOffset; // buffer i at bufferOffset + i * capacity
      alignas(64) std::atomic<uint32_t> middle;
      std::atomic<uint64_t> published;
      BufferInfo info[kBuffers];
   };

   inline uint64_t BufferOffset() { return (uint64_t(sizeof(Header)) + 4095u) & ~uint64_t(4095u); }
   inline uint64_t RegionBytes(uint64_t capacity) { return BufferOffset() + capacity * kBuffers; }
   inline uint8_t* Buffer(Header* h, uint32_t i) { return reinterpret_cast<uint8_t*>(h) + h->bufferOffset + uint64_t(i) * h->capacity; }

   // Creator, on a fresh zeroed region. The writer starts with back = 0, the reader with front = 2.
   inline void Init(Header* h, uint64_t capacity)
   {
      h->magic = kMagic;
      h->version = kVersion;
      h->capacity = capacity;
      h->bufferOffset = BufferOffset();
      h->middle.store(1, std::memory_order_relaxed);
      h->published.store(0, std::memory_order_relaxed);
   }

   inline bool Valid(const Header* h, size_t regionBytes)
   {
      return h && regionBytes >= sizeof(Header) && h->magic == kMagic && h->version == kVersion &&
             RegionBytes(h->capacity) <= regionBytes;
   }

   // Writer: buffer[back] and info[back] are filled. Returns the next back buffer.
   inline uint32_t Publish(Header* h, uint32_t back)
   {
      const uint32_t next = h->middle.exchange(back | kDirty, std::memory_order_acq_rel) & 3u;
      h->published.fetch_add(1, std::memory_order_release);
      return next;
   }

   // Reader: true and a new front when a newer frame is there.
   inline bool Acquire(Header* h, uint32_t* front)
   {
      if (!(h->middle.load(std::memory_order_acquire) & kDirty))
         return false;
      *front = h->middle.exchange(*front, std::memory_order_acq_rel) & 3u;
      return true;
   }

   // ---- messages (PluginIpc::Channel types; the shell's processor relays them) -------------
   // shell -> engine
   constexpr uint32_t kMsgView = 103;      // payload: ViewState
   constexpr uint32_t kMsgViewInput = 104; // payload: InputEvent[]
   // engine -> shell
   constexpr uint32_t kMsgFrames = 6;      // payload: FramesInfo (a new region)
   constexpr uint32_t kMsgViewCursor = 7;  // payload: uint32_t Cursor

   struct ViewState
   {
      uint32_t open = 0;            // 0: no view, the engine stops reading frames back
      uint32_t width = 0, height = 0; // points
      float scale = 1.0f;            // the view's backing scale
   };

   struct FramesInfo
   {
      char name[64] = {};
      uint64_t bytes = 0; // region size
   };

   enum Cursor : uint32_t
   {
      kCursorArrow = 0, kCursorText, kCursorResizeAll, kCursorResizeNS, kCursorResizeEW,
      kCursorResizeNESW, kCursorResizeNWSE, kCursorHand, kCursorNotAllowed, kCursorHidden
   };

   enum InputType : uint32_t
   {
      kInMouseMove = 1, // x, y
      kInMouseButton,   // a = button (0 left, 1 right, 2 middle), b = down; x, y
      kInScroll,        // x, y = wheel delta (lines; +y up)
      kInKey,           // a = GLFW key, b = down (1), up (0) or repeat (2)
      kInText,          // a = Unicode code point
      kInFocus,         // b = focused
      kInLeave          // the pointer left the view
   };

   struct InputEvent
   {
      uint32_t type = 0;
      int32_t a = 0, b = 0;
      uint32_t mods = 0; // GLFW_MOD_* bits
      float x = 0.0f, y = 0.0f;
   };
}

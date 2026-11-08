#pragma once
// The plugin's editor view (docs/plans/vst-au/README.md 8.3, step 2.3). It shows the engine's UI:
// the engine draws as in the app and publishes each frame into a shared-memory triple buffer
// (src/plugin/ipc/PluginFrames.h); the view blits the newest one on a 120 Hz timer and sends mouse
// and keys back. Everything the user sees (canvas, picture switch, Slots, xruns, pop-out) is drawn
// by the engine in its top bar; the view draws natively only while there are no frames to show
// ("Starting" / "Engine stopped" + Restart, "In its own window" + Bring back, "No graphics on
// this machine", and the licence sheet: paste or drop a key, Buy, Refund check).
//
// macOS: ShellViewMac.mm (NSView). Windows and Linux views come in chunk 3; there createView
// returns nullptr and hosts show their generic parameter editor.
#include "plugin/ipc/PluginFrames.h"
#include "public.sdk/source/common/pluginview.h"

#include <cstdint>
#include <string>

namespace InfinitePlugin
{
   class ShellView;

   // The controller, as the view sees it. UI thread.
   struct ViewHost
   {
      virtual ~ViewHost() = default;
      // Size in points, backing scale; open = 0 when the view goes away.
      virtual void ViewState(const PluginFrames::ViewState& state) = 0;
      virtual void ViewInput(const PluginFrames::InputEvent* events, uint32_t count) = 0;
      virtual void ViewRestart() = 0;
      // "Bring back": the engine's popped-out window returns into the view.
      virtual void ViewPopIn() = 0;
      // Licence sheet (step 2.6): text pasted or dropped (a key, or a file holding one).
      virtual void ViewLicense(const std::string& text) = 0;
      // About once a second while the sheet shows: a key entered in another instance starts this one.
      virtual void ViewLicenseRecheck() = 0;
      // The view is being destroyed: forget the pointer.
      virtual void ViewGone(ShellView* view) = 0;
   };

   enum class EngineStatus : int
   {
      Starting = 0,
      Running = 1,
      Stopped = 2,
      // The engine could not open a GL window and quit (kMsgNoGraphics); no restart is offered.
      NoGraphics = 3,
      // No valid licence key (step 2.6): no engine; the view shows the licence sheet with SetLicense's text.
      Unlicensed = 4,
   };

   class ShellView : public Steinberg::CPluginView
   {
   public:
      static bool Supported();
      static ShellView* Create(ViewHost* host);

      // From the controller (UI thread).
      virtual void SetFrames(const char* name, uint64_t bytes) = 0;
      virtual void SetCursor(uint32_t cursor) = 0;
      virtual void SetEngine(EngineStatus status) = 0;
      // The licence sheet's message ("Enter your key", "This key was refunded"); before SetEngine(Unlicensed).
      virtual void SetLicense(const std::string& message) = 0;
      // The engine's window is out on its own: no frames, a "Bring back" button instead.
      virtual void SetPopped(bool popped) = 0;
      // The controller is going away first: the view stops calling it.
      virtual void DropHost() = 0;

   protected:
      explicit ShellView(const Steinberg::ViewRect* r) : CPluginView(r) {}
   };
}

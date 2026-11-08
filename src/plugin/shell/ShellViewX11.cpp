// Linux editor view (docs/plans/vst-au/README.md 8.3, step 3.2): an X11 child window inside the
// host's (kPlatformTypeX11EmbedWindowID). Same contract as ShellViewMac.mm and ShellViewWin.cpp:
// the host's IRunLoop runs a ~120 Hz timer that takes the newest engine frame from the shared-memory
// triple buffer and puts it (XPutImage straight from the front buffer); X events come in through the
// run loop's fd handler and go back as InputEvents in points. With no frames the view draws its own
// overlay: "Starting", "Engine stopped" + Restart, "In its own window" + Bring back, "No graphics
// on this machine", or the licence sheet (type or paste a key, Buy, Weekly refund check).
//
// Threads: on Linux the controller's engine messages arrive on the reader thread (UiRelay), so the
// Set* calls only record the newest value under a mutex; the timer applies them on the UI thread.
#include "plugin/shell/ShellView.h"
#include "core/License.h"
#include "platform/common/Ipc.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <spawn.h>
#include <string>
#include <vector>

// Last: Xlib's macros (None, Bool, Status, True) must not meet the SDK headers.
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

extern char** environ;

using namespace Steinberg;
using PluginFrames::InputEvent;

namespace
{
   enum : uint32_t
   {
      kGlfwModShift = 0x1, kGlfwModCtrl = 0x2, kGlfwModAlt = 0x4, kGlfwModSuper = 0x8, kGlfwModCaps = 0x10,
   };
   enum : int
   {
      kIdNone = 0, kIdRestart, kIdKey, kIdPaste, kIdBuy, kIdRefund,
   };
   constexpr unsigned long kBg = 0x17171A, kFg = 0xBEBEC3, kButton = 0x2C2C31, kField = 0x0F0F11, kLine = 0x46464C;

   uint32_t Mods(unsigned state)
   {
      uint32_t m = 0;
      if (state & ShiftMask) m |= kGlfwModShift;
      if (state & ControlMask) m |= kGlfwModCtrl;
      if (state & Mod1Mask) m |= kGlfwModAlt;
      if (state & Mod4Mask) m |= kGlfwModSuper;
      if (state & LockMask) m |= kGlfwModCaps;
      return m;
   }

   int GlfwKey(KeySym k)
   {
      if (k >= XK_a && k <= XK_z) return int(k - XK_a) + 65;
      if (k >= XK_A && k <= XK_Z) return int(k - XK_A) + 65;
      if (k >= XK_0 && k <= XK_9) return int(k - XK_0) + 48;
      if (k >= XK_F1 && k <= XK_F12) return 290 + int(k - XK_F1);
      if (k >= XK_KP_0 && k <= XK_KP_9) return 320 + int(k - XK_KP_0);
      switch (k)
      {
      case XK_space: return 32; case XK_apostrophe: return 39; case XK_comma: return 44; case XK_minus: return 45;
      case XK_period: return 46; case XK_slash: return 47; case XK_semicolon: return 59; case XK_equal: return 61;
      case XK_bracketleft: return 91; case XK_backslash: return 92; case XK_bracketright: return 93; case XK_grave: return 96;
      case XK_Escape: return 256; case XK_Return: return 257; case XK_Tab: case XK_ISO_Left_Tab: return 258;
      case XK_BackSpace: return 259; case XK_Insert: return 260; case XK_Delete: return 261; case XK_Right: return 262;
      case XK_Left: return 263; case XK_Down: return 264; case XK_Up: return 265; case XK_Prior: return 266;
      case XK_Next: return 267; case XK_Home: return 268; case XK_End: return 269;
      case XK_KP_Decimal: return 330; case XK_KP_Divide: return 331; case XK_KP_Multiply: return 332;
      case XK_KP_Subtract: return 333; case XK_KP_Add: return 334; case XK_KP_Enter: return 335;
      case XK_Shift_L: return 340; case XK_Control_L: return 341; case XK_Alt_L: return 342; case XK_Super_L: return 343;
      case XK_Shift_R: return 344; case XK_Control_R: return 345; case XK_Alt_R: case XK_ISO_Level3_Shift: return 346;
      case XK_Super_R: return 347;
      default: return -1;
      }
   }

   // The typed character for a key press (Latin-1 and Unicode keysyms; no input method).
   uint32_t CodePoint(KeySym k, const char* buf, int n)
   {
      if ((k >= 0x20 && k <= 0x7E) || (k >= 0xA0 && k <= 0xFF))
         return uint32_t(k);
      if ((k & 0xFF000000) == 0x01000000)
         return uint32_t(k & 0x00FFFFFF);
      if (n == 1 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7F)
         return static_cast<unsigned char>(buf[0]);
      return 0;
   }

   void AppendUtf8(std::string& s, uint32_t cp)
   {
      if (cp < 0x80) s += char(cp);
      else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
      else { s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 0x3F)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
   }

   unsigned FontCursorFor(uint32_t c)
   {
      switch (c)
      {
      case PluginFrames::kCursorText: return XC_xterm;
      case PluginFrames::kCursorResizeAll: return XC_fleur;
      case PluginFrames::kCursorResizeNS: return XC_sb_v_double_arrow;
      case PluginFrames::kCursorResizeEW: return XC_sb_h_double_arrow;
      case PluginFrames::kCursorResizeNESW: return XC_bottom_left_corner;
      case PluginFrames::kCursorResizeNWSE: return XC_bottom_right_corner;
      case PluginFrames::kCursorHand: return XC_hand2;
      case PluginFrames::kCursorNotAllowed: return XC_X_cursor;
      default: return XC_left_ptr;
      }
   }

   class X11View;

   // The run loop's callbacks; owned by the view, so the refcount is a no-op.
   struct EventHandler final : Linux::IEventHandler
   {
      X11View* view = nullptr;
      void PLUGIN_API onFDIsSet(Linux::FileDescriptor) override;
      tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
      {
         QUERY_INTERFACE(iid, obj, FUnknown::iid, Linux::IEventHandler)
         QUERY_INTERFACE(iid, obj, Linux::IEventHandler::iid, Linux::IEventHandler)
         *obj = nullptr;
         return kNoInterface;
      }
      uint32 PLUGIN_API addRef() override { return 1; }
      uint32 PLUGIN_API release() override { return 1; }
   };
   struct TimerHandler final : Linux::ITimerHandler
   {
      X11View* view = nullptr;
      void PLUGIN_API onTimer() override;
      tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
      {
         QUERY_INTERFACE(iid, obj, FUnknown::iid, Linux::ITimerHandler)
         QUERY_INTERFACE(iid, obj, Linux::ITimerHandler::iid, Linux::ITimerHandler)
         *obj = nullptr;
         return kNoInterface;
      }
      uint32 PLUGIN_API addRef() override { return 1; }
      uint32 PLUGIN_API release() override { return 1; }
   };

   class X11View final : public InfinitePlugin::ShellView, public IPlugViewContentScaleSupport
   {
   public:
      explicit X11View(InfinitePlugin::ViewHost* host) : ShellView(&kDefault), host_(host)
      {
         events_.view = this;
         timer_.view = this;
      }
      ~X11View() override
      {
         if (win_)
            SendState(false);
         // First, while every member is alive: the controller stops calling in from the reader thread.
         if (host_)
            host_->ViewGone(this);
         host_ = nullptr;
         Teardown();
      }

      tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
      {
         QUERY_INTERFACE(iid, obj, IPlugViewContentScaleSupport::iid, IPlugViewContentScaleSupport)
         return CPluginView::queryInterface(iid, obj);
      }
      uint32 PLUGIN_API addRef() override { return CPluginView::addRef(); }
      uint32 PLUGIN_API release() override { return CPluginView::release(); }

      tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override
      {
         if (factor <= 0.0f)
            return kInvalidArgument;
         scale_ = factor;
         SendState(win_ != 0);
         return kResultTrue;
      }

      tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
      {
         return type && std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ? kResultTrue : kResultFalse;
      }
      tresult PLUGIN_API attached(void* parent, FIDString type) override
      {
         if (isPlatformTypeSupported(type) != kResultTrue || !parent)
            return kResultFalse;
         return CPluginView::attached(parent, type);
      }
      tresult PLUGIN_API canResize() override { return kResultTrue; }
      tresult PLUGIN_API checkSizeConstraint(ViewRect* r) override
      {
         if (!r)
            return kInvalidArgument;
         r->right = r->left + std::max<int32>(r->getWidth(), int32(kMinW * scale_));
         r->bottom = r->top + std::max<int32>(r->getHeight(), int32(kMinH * scale_));
         return kResultTrue;
      }
      tresult PLUGIN_API onSize(ViewRect* r) override
      {
         CPluginView::onSize(r);
         if (win_)
         {
            XResizeWindow(dpy_, win_, unsigned(std::max<int32>(1, rect.getWidth())), unsigned(std::max<int32>(1, rect.getHeight())));
            XFlush(dpy_);
            Redraw();
            SendState(true);
         }
         return kResultTrue;
      }
      tresult PLUGIN_API onFocus(TBool state) override
      {
         Push({ PluginFrames::kInFocus, 0, state ? 1 : 0, 0, 0, 0 });
         return kResultTrue;
      }

      void attachedToParent() override
      {
         dpy_ = XOpenDisplay(nullptr);
         if (!dpy_)
            return;
         const int screen = DefaultScreen(dpy_);
         XSetWindowAttributes a {};
         a.colormap = DefaultColormap(dpy_, screen);
         a.border_pixel = 0;
         a.background_pixel = kBg;
         a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask |
                        KeyReleaseMask | FocusChangeMask | LeaveWindowMask | EnterWindowMask | StructureNotifyMask;
         win_ = XCreateWindow(dpy_, Window(reinterpret_cast<uintptr_t>(systemWindow)), 0, 0,
                              unsigned(std::max<int32>(1, rect.getWidth())), unsigned(std::max<int32>(1, rect.getHeight())), 0,
                              DefaultDepth(dpy_, screen), InputOutput, DefaultVisual(dpy_, screen),
                              CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &a);
         if (!win_)
         {
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
            return;
         }
         visual_ = DefaultVisual(dpy_, screen);
         depth_ = DefaultDepth(dpy_, screen);
         gc_ = XCreateGC(dpy_, win_, 0, nullptr);
         font_ = XLoadQueryFont(dpy_, "-*-dejavu sans-medium-r-normal--15-*-*-*-*-*-iso10646-1");
         if (!font_)
            font_ = XLoadQueryFont(dpy_, "-*-helvetica-medium-r-normal--14-*-*-*-*-*-*-*");
         if (!font_)
            font_ = XLoadQueryFont(dpy_, "fixed");
         if (font_)
            XSetFont(dpy_, gc_, font_->fid);
         clipboard_ = XInternAtom(dpy_, "CLIPBOARD", False);
         utf8_ = XInternAtom(dpy_, "UTF8_STRING", False);
         pasteProp_ = XInternAtom(dpy_, "INFINITE_PASTE", False);
         Bool supported = False;
         XkbSetDetectableAutoRepeat(dpy_, True, &supported); // repeats come as presses only
         XMapWindow(dpy_, win_);
         XFlush(dpy_);

         if (plugFrame)
         {
            Linux::IRunLoop* loop = nullptr;
            if (plugFrame->queryInterface(Linux::IRunLoop::iid, reinterpret_cast<void**>(&loop)) == kResultTrue && loop)
            {
               loop_ = loop; // keeps the reference until removed
               loop_->registerEventHandler(&events_, ConnectionNumber(dpy_));
               loop_->registerTimer(&timer_, 8);
            }
         }
         ApplyPending();
         Redraw();
         SendState(true);
      }

      void removedFromParent() override
      {
         SendState(false);
         Teardown();
      }

      // ---- From the controller: any thread (the reader thread on Linux) ----
      void SetFrames(const char* name, uint64_t bytes) override
      {
         std::lock_guard<std::mutex> lock(pendingMutex_);
         pending_.frames = true;
         pending_.framesName = name ? name : "";
         pending_.framesBytes = bytes;
      }
      void SetCursor(uint32_t c) override
      {
         std::lock_guard<std::mutex> lock(pendingMutex_);
         pending_.cursor = true;
         pending_.cursorId = c;
      }
      void SetEngine(InfinitePlugin::EngineStatus s) override
      {
         std::lock_guard<std::mutex> lock(pendingMutex_);
         pending_.engine = true;
         pending_.status = s;
      }
      void SetLicense(const std::string& message) override
      {
         std::lock_guard<std::mutex> lock(pendingMutex_);
         pending_.license = true;
         pending_.licenseText = message;
      }
      void SetPopped(bool popped) override
      {
         std::lock_guard<std::mutex> lock(pendingMutex_);
         pending_.popped = true;
         pending_.poppedOn = popped;
      }
      void DropHost() override { host_ = nullptr; } // UI thread (terminate, a second createView)

      // ---- Run loop (UI thread) ----
      void OnEvents() { Pump(); }
      void OnTimer()
      {
         ApplyPending();
         Pump();
         Tick();
      }

   private:
      static constexpr int32 kMinW = 640, kMinH = 400;
      static inline const ViewRect kDefault { 0, 0, 1280, 800 };

      struct Pending
      {
         bool frames = false, cursor = false, engine = false, license = false, popped = false;
         std::string framesName;
         uint64_t framesBytes = 0;
         uint32_t cursorId = 0;
         InfinitePlugin::EngineStatus status = InfinitePlugin::EngineStatus::Starting;
         std::string licenseText;
         bool poppedOn = false;
      };

      struct Box
      {
         int x = 0, y = 0, w = 0, h = 0;
         bool Has(int px, int py) const { return w > 0 && px >= x && py >= y && px < x + w && py < y + h; }
      };

      float Px(int v) const { return float(v) / scale_; }

      void Push(const InputEvent& e)
      {
         if (host_ && win_ && !Overlay())
            host_->ViewInput(&e, 1);
      }

      void SendState(bool open)
      {
         if (!host_)
            return;
         PluginFrames::ViewState st;
         st.open = open ? 1 : 0;
         st.width = uint32_t(std::max(1.0f, float(rect.getWidth()) / scale_));
         st.height = uint32_t(std::max(1.0f, float(rect.getHeight()) / scale_));
         st.scale = scale_;
         host_->ViewState(st);
      }

      bool Unlicensed() const { return status_ == InfinitePlugin::EngineStatus::Unlicensed; }
      bool Overlay() const { return status_ != InfinitePlugin::EngineStatus::Running || popped_ || !shown_; }

      void ApplyPending()
      {
         Pending p;
         {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            p = std::move(pending_);
            pending_ = Pending {};
         }
         bool overlay = false;
         if (p.frames)
            OpenFrames(p.framesName, p.framesBytes);
         if (p.cursor)
            ApplyCursor(p.cursorId);
         if (p.license)
         {
            license_ = p.licenseText;
            overlay = true;
         }
         if (p.popped && popped_ != p.poppedOn)
         {
            popped_ = p.poppedOn;
            shown_ = false;
            overlay = true;
         }
         if (p.engine)
         {
            status_ = p.status;
            if (status_ != InfinitePlugin::EngineStatus::Running)
               shown_ = false;
            overlay = true;
         }
         if (overlay)
            Redraw();
      }

      void OpenFrames(const std::string& name, uint64_t bytes)
      {
         CloseRegion();
         if (name.empty() || bytes < sizeof(PluginFrames::Header))
            return;
         if (!Ipc::SharedMemoryOpen(name.c_str(), size_t(bytes), &region_))
            return;
         auto* h = static_cast<PluginFrames::Header*>(region_.data);
         if (!PluginFrames::Valid(h, region_.size))
         {
            CloseRegion();
            return;
         }
         frames_ = h;
         front_ = 2;
      }

      void ApplyCursor(uint32_t c)
      {
         cursorId_ = c;
         if (!win_ || Overlay())
            return;
         XDefineCursor(dpy_, win_, CursorFor(c));
         XFlush(dpy_);
      }

      Cursor CursorFor(uint32_t c)
      {
         if (c == PluginFrames::kCursorHidden)
         {
            if (!blank_)
            {
               static const char bits[1] = { 0 };
               Pixmap pm = XCreateBitmapFromData(dpy_, win_, bits, 1, 1);
               XColor black {};
               blank_ = XCreatePixmapCursor(dpy_, pm, pm, &black, &black, 0, 0);
               XFreePixmap(dpy_, pm);
            }
            return blank_;
         }
         const unsigned shape = FontCursorFor(c);
         for (auto& [s, cur] : cursors_)
            if (s == shape)
               return cur;
         const Cursor cur = XCreateFontCursor(dpy_, shape);
         cursors_.push_back({ shape, cur });
         return cur;
      }

      void Pump()
      {
         if (!dpy_)
            return;
         while (dpy_ && XPending(dpy_) > 0)
         {
            XEvent ev;
            XNextEvent(dpy_, &ev);
            Handle(ev);
         }
      }

      void Handle(XEvent& ev)
      {
         switch (ev.type)
         {
         case Expose:
            if (ev.xexpose.count == 0)
               Redraw();
            break;
         case MotionNotify:
            Push({ PluginFrames::kInMouseMove, 0, 0, Mods(ev.xmotion.state), Px(ev.xmotion.x), Px(ev.xmotion.y) });
            break;
         case LeaveNotify:
            Push({ PluginFrames::kInLeave, 0, 0, 0, 0, 0 });
            break;
         case ButtonPress: case ButtonRelease:
         {
            const bool down = ev.type == ButtonPress;
            const unsigned b = ev.xbutton.button;
            if (down)
               XSetInputFocus(dpy_, win_, RevertToParent, CurrentTime);
            if (b >= 4 && b <= 7)
            {
               if (down)
               {
                  const float d = (b == 4 || b == 6) ? 1.0f : -1.0f;
                  Push({ PluginFrames::kInScroll, 0, 0, Mods(ev.xbutton.state), b >= 6 ? -d : 0.0f, b <= 5 ? d : 0.0f });
               }
               break;
            }
            if (Overlay())
            {
               if (down && b == 1)
                  Click(ev.xbutton.x, ev.xbutton.y);
               break;
            }
            const int button = b == 1 ? 0 : b == 3 ? 1 : b == 2 ? 2 : -1;
            if (button >= 0)
               Push({ PluginFrames::kInMouseButton, button, down ? 1 : 0, Mods(ev.xbutton.state), Px(ev.xbutton.x), Px(ev.xbutton.y) });
            break;
         }
         case KeyPress: case KeyRelease:
         {
            const bool down = ev.type == KeyPress;
            const KeySym base = XLookupKeysym(&ev.xkey, 0);
            char buf[16] = {};
            KeySym sym = 0;
            const int n = down ? XLookupString(&ev.xkey, buf, int(sizeof buf), &sym, nullptr) : 0;
            if (Overlay())
            {
               if (down && Unlicensed())
                  FieldKey(sym ? sym : base, ev.xkey.state, CodePoint(sym, buf, n));
               break;
            }
            const int key = GlfwKey(base);
            if (key >= 0)
            {
               bool repeat = false;
               if (down)
               {
                  repeat = std::find(held_.begin(), held_.end(), key) != held_.end();
                  if (!repeat)
                     held_.push_back(key);
               }
               else
                  held_.erase(std::remove(held_.begin(), held_.end(), key), held_.end());
               Push({ PluginFrames::kInKey, key, down ? (repeat ? 2 : 1) : 0, Mods(ev.xkey.state), 0, 0 });
            }
            if (down && !(ev.xkey.state & (ControlMask | Mod4Mask)))
               if (const uint32_t cp = CodePoint(sym, buf, n))
                  Push({ PluginFrames::kInText, int32_t(cp), 0, Mods(ev.xkey.state), 0, 0 });
            break;
         }
         case FocusIn:
            Push({ PluginFrames::kInFocus, 0, 1, 0, 0, 0 });
            break;
         case FocusOut:
            held_.clear();
            Push({ PluginFrames::kInFocus, 0, 0, 0, 0, 0 });
            break;
         case SelectionNotify:
            if (ev.xselection.property != 0)
               SubmitLicense(ReadPaste());
            break;
         default:
            break;
         }
      }

      // ---- Licence sheet ----
      void FieldKey(KeySym sym, unsigned state, uint32_t cp)
      {
         if (sym == XK_Return || sym == XK_KP_Enter)
         {
            SubmitLicense(field_);
            return;
         }
         if ((state & ControlMask) && (sym == XK_v || sym == XK_V))
         {
            RequestPaste();
            return;
         }
         if (sym == XK_BackSpace)
         {
            while (!field_.empty() && (static_cast<unsigned char>(field_.back()) & 0xC0) == 0x80)
               field_.pop_back();
            if (!field_.empty())
               field_.pop_back();
         }
         else if (cp && !(state & ControlMask) && field_.size() < 4096)
            AppendUtf8(field_, cp);
         else
            return;
         Redraw();
      }

      void RequestPaste()
      {
         XConvertSelection(dpy_, clipboard_, utf8_, pasteProp_, win_, CurrentTime);
         XFlush(dpy_);
      }

      std::string ReadPaste()
      {
         Atom type = 0;
         int format = 0;
         unsigned long count = 0, after = 0;
         unsigned char* data = nullptr;
         std::string out;
         if (XGetWindowProperty(dpy_, win_, pasteProp_, 0, 16384, True, AnyPropertyType, &type, &format, &count, &after,
                                &data) == Success && data)
         {
            if (format == 8)
               out.assign(reinterpret_cast<const char*>(data), size_t(count));
            XFree(data);
         }
         return out;
      }

      void SubmitLicense(const std::string& text)
      {
         if (host_ && Unlicensed() && !text.empty())
            host_->ViewLicense(text);
      }

      void Click(int x, int y)
      {
         if (restartBox_.Has(x, y) && host_)
         {
            if (popped_)
               host_->ViewPopIn();
            else
               host_->ViewRestart();
            return;
         }
         if (!Unlicensed())
            return;
         if (pasteBox_.Has(x, y))
            RequestPaste();
         else if (buyBox_.Has(x, y))
         {
            char cmd[] = "xdg-open";
            std::string url = License::kBuyUrl;
            char* argv[] = { cmd, url.data(), nullptr };
            pid_t pid = 0;
            posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ);
         }
         else if (refundBox_.Has(x, y))
         {
            License::SetRefundCheck(!License::RefundCheckOn());
            Redraw();
         }
      }

      // ---- Drawing ----
      int TextWidth(const std::string& s) const { return font_ ? XTextWidth(font_, s.c_str(), int(s.size())) : int(s.size()) * 7; }
      int Ascent() const { return font_ ? font_->ascent : 11; }

      void DrawText(int cx, int y, const std::string& s, unsigned long color)
      {
         XSetForeground(dpy_, gc_, color);
         XDrawString(dpy_, win_, gc_, cx - TextWidth(s) / 2, y + Ascent(), s.c_str(), int(s.size()));
      }

      void DrawButton(Box& box, int cx, int y, int w, int h, const std::string& label)
      {
         box = { cx - w / 2, y, w, h };
         XSetForeground(dpy_, gc_, kButton);
         XFillRectangle(dpy_, win_, gc_, box.x, box.y, unsigned(w), unsigned(h));
         XSetForeground(dpy_, gc_, kLine);
         XDrawRectangle(dpy_, win_, gc_, box.x, box.y, unsigned(w - 1), unsigned(h - 1));
         DrawText(cx, y + (h - Ascent()) / 2 - 1, label, kFg);
      }

      void Redraw()
      {
         if (!win_)
            return;
         restartBox_ = pasteBox_ = buyBox_ = refundBox_ = Box {};
         if (!Overlay())
         {
            XDefineCursor(dpy_, win_, CursorFor(cursorId_));
            Tick(true);
            return;
         }
         XUndefineCursor(dpy_, win_);
         const int W = rect.getWidth(), H = rect.getHeight();
         auto px = [this](int v) { return int(float(v) * scale_ + 0.5f); };
         XSetForeground(dpy_, gc_, kBg);
         XFillRectangle(dpy_, win_, gc_, 0, 0, unsigned(std::max(1, W)), unsigned(std::max(1, H)));
         using InfinitePlugin::EngineStatus;
         const bool stopped = status_ == EngineStatus::Stopped;
         const bool popped = popped_ && status_ == EngineStatus::Running;
         const int lh = px(22), bh = px(28);
         if (Unlicensed())
         {
            const int fw = std::max(px(120), std::min(px(360), W - px(32))), fh = px(26);
            int y = (H - (lh + px(12) + fh + px(12) + bh + px(10) + px(20))) / 2;
            DrawText(W / 2, y, license_, kFg);
            y += lh + px(12);
            XSetForeground(dpy_, gc_, kField);
            XFillRectangle(dpy_, win_, gc_, (W - fw) / 2, y, unsigned(fw), unsigned(fh));
            XSetForeground(dpy_, gc_, kLine);
            XDrawRectangle(dpy_, win_, gc_, (W - fw) / 2, y, unsigned(fw - 1), unsigned(fh - 1));
            std::string shown = field_.empty() ? std::string("INF1...") : field_ + "_";
            while (shown.size() > 1 && TextWidth(shown) > fw - px(12))
               shown.erase(0, 1);
            XSetForeground(dpy_, gc_, field_.empty() ? kLine : kFg);
            XDrawString(dpy_, win_, gc_, (W - fw) / 2 + px(6), y + (fh + Ascent()) / 2 - 1, shown.c_str(), int(shown.size()));
            y += fh + px(12);
            const int bw = px(96), gap = px(8);
            DrawButton(pasteBox_, W / 2 - (bw + gap) / 2, y, bw, bh, "Paste key");
            DrawButton(buyBox_, W / 2 + (bw + gap) / 2, y, bw, bh, "Buy");
            y += bh + px(10);
            const int box = px(14), label = TextWidth("Weekly refund check");
            const int x0 = W / 2 - (box + px(8) + label) / 2;
            refundBox_ = { x0, y, box + px(8) + label, px(20) };
            XSetForeground(dpy_, gc_, kLine);
            XDrawRectangle(dpy_, win_, gc_, x0, y + px(3), unsigned(box), unsigned(box));
            if (License::RefundCheckOn())
            {
               XSetForeground(dpy_, gc_, kFg);
               XFillRectangle(dpy_, win_, gc_, x0 + px(3), y + px(6), unsigned(box - px(5)), unsigned(box - px(5)));
            }
            XSetForeground(dpy_, gc_, kFg);
            XDrawString(dpy_, win_, gc_, x0 + box + px(8), y + px(3) + box - 2, "Weekly refund check", 19);
         }
         else
         {
            const std::string text = status_ == EngineStatus::NoGraphics ? "No graphics on this machine"
                                   : stopped                             ? "Engine stopped"
                                   : popped                              ? "In its own window"
                                                                         : "Starting";
            const bool button = stopped || popped;
            const int top = (H - (lh + (button ? px(12) + bh : 0))) / 2;
            DrawText(W / 2, top, text, kFg);
            if (button)
               DrawButton(restartBox_, W / 2, top + lh + px(12), px(110), bh, popped ? "Bring back" : "Restart");
         }
         XFlush(dpy_);
      }

      // repaint: put the current frame again even when no new one came (an Expose).
      void Tick(bool repaint = false)
      {
         if (!win_)
            return;
         if (Unlicensed() && host_ && ++recheckTicks_ >= 120)
         {
            recheckTicks_ = 0;
            host_->ViewLicenseRecheck();
         }
         if (!frames_ || popped_)
            return;
         const bool fresh = PluginFrames::Acquire(frames_, &front_);
         if (!fresh && !(repaint && haveFrame_))
            return;
         const PluginFrames::BufferInfo info = frames_->info[front_];
         if (info.width == 0 || info.height == 0 || uint64_t(info.stride) * info.height > frames_->capacity)
            return;
         haveFrame_ = true;
         if (!shown_ && status_ == InfinitePlugin::EngineStatus::Running)
         {
            shown_ = true;
            Redraw(); // drops the overlay; comes back here to put the frame
            return;
         }
         if (Overlay() || depth_ < 24)
            return;
         XImage* img = XCreateImage(dpy_, visual_, unsigned(depth_), ZPixmap, 0,
                                    reinterpret_cast<char*>(PluginFrames::Buffer(frames_, front_)), info.width, info.height,
                                    32, int(info.stride));
         if (!img)
            return;
         const unsigned w = std::min<unsigned>(info.width, unsigned(std::max<int32>(1, rect.getWidth())));
         const unsigned h = std::min<unsigned>(info.height, unsigned(std::max<int32>(1, rect.getHeight())));
         XPutImage(dpy_, win_, gc_, img, 0, 0, 0, 0, w, h);
         img->data = nullptr; // the shared buffer is not the image's to free
         XDestroyImage(img);
         XFlush(dpy_);
      }

      void CloseRegion()
      {
         frames_ = nullptr;
         haveFrame_ = false;
         Ipc::SharedMemoryClose(&region_);
      }

      void Teardown()
      {
         if (loop_)
         {
            loop_->unregisterTimer(&timer_);
            loop_->unregisterEventHandler(&events_);
            loop_->release();
            loop_ = nullptr;
         }
         if (dpy_)
         {
            for (auto& c : cursors_)
               XFreeCursor(dpy_, c.second);
            cursors_.clear();
            if (blank_)
               XFreeCursor(dpy_, blank_);
            blank_ = 0;
            if (font_)
               XFreeFont(dpy_, font_);
            font_ = nullptr;
            if (gc_)
               XFreeGC(dpy_, gc_);
            gc_ = nullptr;
            if (win_)
               XDestroyWindow(dpy_, win_);
            win_ = 0;
            XSync(dpy_, False);
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
         }
         CloseRegion();
      }

      InfinitePlugin::ViewHost* host_ = nullptr;
      EventHandler events_;
      TimerHandler timer_;
      Linux::IRunLoop* loop_ = nullptr;
      Display* dpy_ = nullptr;
      Window win_ = 0;
      Visual* visual_ = nullptr;
      int depth_ = 0;
      GC gc_ = nullptr;
      XFontStruct* font_ = nullptr;
      Atom clipboard_ = 0, utf8_ = 0, pasteProp_ = 0;
      std::vector<std::pair<unsigned, Cursor>> cursors_;
      Cursor blank_ = 0;
      std::vector<int> held_;
      Box restartBox_, pasteBox_, buyBox_, refundBox_;

      std::mutex pendingMutex_;
      Pending pending_;

      Ipc::SharedMemory region_ {};
      PluginFrames::Header* frames_ = nullptr;
      uint32_t front_ = 2;
      bool haveFrame_ = false, shown_ = false, popped_ = false;
      uint32_t cursorId_ = 0;
      int recheckTicks_ = 0;
      float scale_ = 1.0f;
      InfinitePlugin::EngineStatus status_ = InfinitePlugin::EngineStatus::Starting;
      std::string license_ = "Enter your key";
      std::string field_;
   };

   void PLUGIN_API EventHandler::onFDIsSet(Linux::FileDescriptor) { view->OnEvents(); }
   void PLUGIN_API TimerHandler::onTimer() { view->OnTimer(); }
}

namespace InfinitePlugin
{
   bool ShellView::Supported() { return true; }
   ShellView* ShellView::Create(ViewHost* host) { return new X11View(host); }
}

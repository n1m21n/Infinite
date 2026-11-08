// Windows editor view (docs/plans/vst-au/README.md 8.3, step 3.1): a child HWND inside the host's
// window. Same contract as ShellViewMac.mm: a ~120 Hz timer takes the newest engine frame from the
// shared-memory triple buffer and paints it (StretchDIBits straight from the front buffer, which the
// view owns until its next Acquire); mouse, wheel, keys, text and focus go back as InputEvents in
// points; with no frames the view shows native controls ("Starting", "Engine stopped" + Restart,
// "In its own window" + Bring back, "No graphics on this machine", or the licence sheet: key field,
// Paste key, Buy, Weekly refund check, and a dropped key file).
//
// Sizes: VST3 view rects on Windows are physical pixels; the host tells the DPI scale through
// IPlugViewContentScaleSupport, else the window's DPI is used. The engine gets points + scale.
#include "plugin/shell/ShellView.h"
#include "plugin/shell/SheetStyle.h"
#include "core/License.h"
#include "platform/common/Ipc.h"

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#ifndef INFINITE_VIEW_CLASS
#define INFINITE_VIEW_CLASS InfiniteShellView
#endif
#define INFINITE_WSTR2(x) L## #x
#define INFINITE_WSTR(x) INFINITE_WSTR2(x)

using namespace Steinberg;
using PluginFrames::InputEvent;

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
   enum : uint32_t
   {
      kGlfwModShift = 0x1, kGlfwModCtrl = 0x2, kGlfwModAlt = 0x4, kGlfwModSuper = 0x8, kGlfwModCaps = 0x10,
   };
   enum : int
   {
      kIdRestart = 101, kIdKey, kIdPaste, kIdBuy, kIdRefund,
   };
   constexpr UINT_PTR kTimer = 1;
   const wchar_t* const kClassName = INFINITE_WSTR(INFINITE_VIEW_CLASS);

   HINSTANCE Module() { return reinterpret_cast<HINSTANCE>(&__ImageBase); }

   namespace Sheet = InfinitePlugin::Sheet;
   COLORREF Ref(uint32_t c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

   void RoundFill(HDC dc, const RECT& r, int radius, uint32_t fill, uint32_t edge)
   {
      HBRUSH b = CreateSolidBrush(Ref(fill));
      HPEN p = CreatePen(PS_SOLID, 1, Ref(edge));
      HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
      RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
      SelectObject(dc, ob);
      SelectObject(dc, op);
      DeleteObject(b);
      DeleteObject(p);
   }

   std::wstring Wide(const std::string& s)
   {
      if (s.empty())
         return {};
      const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
      std::wstring w(size_t(n), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
      return w;
   }
   std::string Utf8(const std::wstring& w)
   {
      if (w.empty())
         return {};
      const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
      std::string s(size_t(n), '\0');
      WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
      return s;
   }

   uint32_t Mods()
   {
      uint32_t m = 0;
      if (GetKeyState(VK_SHIFT) & 0x8000) m |= kGlfwModShift;
      if (GetKeyState(VK_CONTROL) & 0x8000) m |= kGlfwModCtrl;
      if (GetKeyState(VK_MENU) & 0x8000) m |= kGlfwModAlt;
      if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= kGlfwModSuper;
      if (GetKeyState(VK_CAPITAL) & 1) m |= kGlfwModCaps;
      return m;
   }

   int GlfwKey(WPARAM vk, LPARAM lp)
   {
      const bool ext = (lp >> 24) & 1;
      if (vk >= 'A' && vk <= 'Z') return int(vk);
      if (vk >= '0' && vk <= '9') return int(vk);
      if (vk >= VK_F1 && vk <= VK_F12) return 290 + int(vk - VK_F1);
      if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return 320 + int(vk - VK_NUMPAD0);
      switch (vk)
      {
      case VK_SPACE: return 32; case VK_OEM_7: return 39; case VK_OEM_COMMA: return 44; case VK_OEM_MINUS: return 45;
      case VK_OEM_PERIOD: return 46; case VK_OEM_2: return 47; case VK_OEM_1: return 59; case VK_OEM_PLUS: return 61;
      case VK_OEM_4: return 91; case VK_OEM_5: return 92; case VK_OEM_6: return 93; case VK_OEM_3: return 96;
      case VK_ESCAPE: return 256; case VK_RETURN: return ext ? 335 : 257; case VK_TAB: return 258; case VK_BACK: return 259;
      case VK_INSERT: return 260; case VK_DELETE: return 261; case VK_RIGHT: return 262; case VK_LEFT: return 263;
      case VK_DOWN: return 264; case VK_UP: return 265; case VK_PRIOR: return 266; case VK_NEXT: return 267;
      case VK_HOME: return 268; case VK_END: return 269;
      case VK_DECIMAL: return 330; case VK_DIVIDE: return 331; case VK_MULTIPLY: return 332; case VK_SUBTRACT: return 333;
      case VK_ADD: return 334;
      case VK_SHIFT: return (MapVirtualKeyW(UINT((lp >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX) == VK_RSHIFT) ? 344 : 340;
      case VK_CONTROL: return ext ? 345 : 341;
      case VK_MENU: return ext ? 346 : 342;
      case VK_LWIN: return 343; case VK_RWIN: return 347;
      default: return -1;
      }
   }

   LPCWSTR CursorFor(uint32_t c)
   {
      switch (c)
      {
      case PluginFrames::kCursorText: return IDC_IBEAM;
      case PluginFrames::kCursorResizeAll: return IDC_SIZEALL;
      case PluginFrames::kCursorResizeNS: return IDC_SIZENS;
      case PluginFrames::kCursorResizeEW: return IDC_SIZEWE;
      case PluginFrames::kCursorResizeNESW: return IDC_SIZENESW;
      case PluginFrames::kCursorResizeNWSE: return IDC_SIZENWSE;
      case PluginFrames::kCursorHand: return IDC_HAND;
      case PluginFrames::kCursorNotAllowed: return IDC_NO;
      default: return IDC_ARROW;
      }
   }

   class WinView final : public InfinitePlugin::ShellView, public IPlugViewContentScaleSupport
   {
   public:
      explicit WinView(InfinitePlugin::ViewHost* host) : ShellView(&kDefault), host_(host) {}
      ~WinView() override
      {
         if (hwnd_)
            SendState(false);
         Teardown();
         if (host_)
            host_->ViewGone(this);
      }

      // FUnknown: CPluginView's refcount, plus content scale support.
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
         hostScale_ = factor;
         SendState(hwnd_ != nullptr);
         return kResultTrue;
      }

      tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
      {
         return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
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
         const float s = Scale();
         r->right = r->left + std::max<int32>(r->getWidth(), int32(kMinW * s));
         r->bottom = r->top + std::max<int32>(r->getHeight(), int32(kMinH * s));
         return kResultTrue;
      }
      tresult PLUGIN_API onSize(ViewRect* r) override
      {
         CPluginView::onSize(r);
         if (hwnd_)
         {
            SetWindowPos(hwnd_, nullptr, 0, 0, rect.getWidth(), rect.getHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
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
         RegisterClassOnce();
         hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, rect.getWidth(),
                                 rect.getHeight(), static_cast<HWND>(systemWindow), nullptr, Module(), this);
         if (!hwnd_)
            return;
         // Inter and Geist Mono from the engine app (process-private), else Segoe UI and Consolas.
         static bool brand = [] {
            const std::wstring dir = Wide(InfinitePlugin::Sheet::EngineFontsDir());
            int ok = 0;
            if (!dir.empty())
               for (const wchar_t* f : { L"Inter-Regular.ttf", L"Inter-SemiBold.ttf", L"GeistMono-Regular.ttf" })
                  ok += AddFontResourceExW((dir + f).c_str(), FR_PRIVATE, nullptr) > 0;
            return ok == 3;
         }();
         const float sc = Scale();
         auto font = [&](int pt, int weight, const wchar_t* face, const wchar_t* fallback) {
            return CreateFontW(-int(pt * sc + 0.5f), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                               brand ? face : fallback);
         };
         font_ = font(Sheet::kTextSize, FW_NORMAL, L"Inter", L"Segoe UI");
         titleFont_ = font(Sheet::kTitleSize, FW_SEMIBOLD, L"Inter", L"Segoe UI");
         buttonFont_ = font(Sheet::kTextSize, FW_SEMIBOLD, L"Inter", L"Segoe UI");
         monoFont_ = font(Sheet::kMonoSize, FW_NORMAL, L"Geist Mono", L"Consolas");
         auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            HWND h = CreateWindowExW(0, cls, text, WS_CHILD | style, 0, 0, 10, 10, hwnd_,
                                     reinterpret_cast<HMENU>(INT_PTR(id)), Module(), nullptr);
            SendMessageW(h, WM_SETFONT, WPARAM(font_), TRUE);
            return h;
         };
         // Licence sheet look: SheetStyle.h. Buttons are owner-drawn (WM_DRAWITEM), the card and
         // the key well are painted behind the controls (Paint).
         label_ = make(L"STATIC", L"", SS_CENTER | SS_ENDELLIPSIS, 0);
         title_ = make(L"STATIC", Wide(Sheet::kTitle).c_str(), SS_LEFT, 0);
         SendMessageW(title_, WM_SETFONT, WPARAM(titleFont_), TRUE);
         restart_ = make(L"BUTTON", L"Restart", BS_OWNERDRAW, kIdRestart);
         keyField_ = make(L"EDIT", L"", ES_AUTOHSCROLL, kIdKey);
         SendMessageW(keyField_, WM_SETFONT, WPARAM(monoFont_), TRUE);
         SendMessageW(keyField_, EM_SETCUEBANNER, TRUE, LPARAM(L"INF1…"));
         paste_ = make(L"BUTTON", L"Paste key", BS_OWNERDRAW, kIdPaste);
         buy_ = make(L"BUTTON", L"Buy", BS_OWNERDRAW, kIdBuy);
         for (HWND b : { restart_, paste_, buy_ })
            SendMessageW(b, WM_SETFONT, WPARAM(buttonFont_), TRUE);
         refund_ = make(L"BUTTON", L"Weekly refund check", BS_AUTOCHECKBOX, kIdRefund);
         // Return in the key field submits it.
         SetWindowSubclassCompat(keyField_);
         DragAcceptFiles(hwnd_, TRUE);
         SetTimer(hwnd_, kTimer, 8, nullptr);
         UpdateOverlay();
         SendState(true);
      }

      void removedFromParent() override
      {
         SendState(false);
         Teardown();
      }

      void SetFrames(const char* name, uint64_t bytes) override
      {
         CloseRegion();
         if (!name || !*name || bytes < sizeof(PluginFrames::Header))
            return;
         if (!Ipc::SharedMemoryOpen(name, size_t(bytes), &region_))
            return;
         auto* h = static_cast<PluginFrames::Header*>(region_.data);
         if (!PluginFrames::Valid(h, region_.size))
         {
            CloseRegion();
            return;
         }
         frames_ = h;
         front_ = 2;
         haveFrame_ = false;
      }

      void SetCursor(uint32_t c) override
      {
         cursorId_ = c;
         if (hwnd_)
         {
            POINT p;
            GetCursorPos(&p);
            if (WindowFromPoint(p) == hwnd_)
               ::SetCursor(c == PluginFrames::kCursorHidden ? nullptr : LoadCursorW(nullptr, CursorFor(c)));
         }
      }

      void DropHost() override { host_ = nullptr; }

      void SetEngine(InfinitePlugin::EngineStatus s) override
      {
         status_ = s;
         if (s != InfinitePlugin::EngineStatus::Running)
            shown_ = false;
         UpdateOverlay();
      }

      void SetLicense(const std::string& message) override
      {
         license_ = message;
         UpdateOverlay();
      }

      void SetPopped(bool popped) override
      {
         if (popped_ == popped)
            return;
         popped_ = popped;
         shown_ = false;
         UpdateOverlay();
      }

      LRESULT Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
      {
         switch (msg)
         {
         case WM_TIMER:
            if (wp == kTimer)
               Tick();
            return 0;
         case WM_ERASEBKGND:
            return 1;
         case WM_PAINT:
            Paint();
            return 0;
         case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && !Overlay())
            {
               ::SetCursor(cursorId_ == PluginFrames::kCursorHidden ? nullptr : LoadCursorW(nullptr, CursorFor(cursorId_)));
               return TRUE;
            }
            break;
         case WM_MOUSEMOVE:
            if (!tracking_)
            {
               TRACKMOUSEEVENT t { sizeof(t), TME_LEAVE, h, 0 };
               TrackMouseEvent(&t);
               tracking_ = true;
            }
            Push({ PluginFrames::kInMouseMove, 0, 0, Mods(), Px(GET_X_LPARAM(lp)), Px(GET_Y_LPARAM(lp)) });
            return 0;
         case WM_MOUSELEAVE:
            tracking_ = false;
            Push({ PluginFrames::kInLeave, 0, 0, 0, 0, 0 });
            return 0;
         case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
         case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
         {
            const bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            const int button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? 1 : 2;
            if (down)
            {
               SetFocus(h);
               SetCapture(h);
               buttons_ |= 1u << button;
            }
            else
            {
               buttons_ &= ~(1u << button);
               if (!buttons_)
                  ReleaseCapture();
            }
            Push({ PluginFrames::kInMouseButton, button, down ? 1 : 0, Mods(), Px(GET_X_LPARAM(lp)), Px(GET_Y_LPARAM(lp)) });
            return 0;
         }
         case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
         {
            const float d = float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA);
            Push({ PluginFrames::kInScroll, 0, 0, Mods(), msg == WM_MOUSEHWHEEL ? -d : 0.0f, msg == WM_MOUSEWHEEL ? d : 0.0f });
            return 0;
         }
         case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
         case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
         {
            const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            const int key = GlfwKey(wp, lp);
            if (key >= 0)
               Push({ PluginFrames::kInKey, key, down ? (((lp >> 30) & 1) ? 2 : 1) : 0, Mods(), 0, 0 });
            if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
               break; // Alt+F4 and the host's menus still work
            return 0;
         }
         case WM_CHAR:
         {
            uint32_t cp = uint32_t(wp);
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
               highSurrogate_ = cp;
               return 0;
            }
            if (cp >= 0xDC00 && cp <= 0xDFFF && highSurrogate_)
               cp = 0x10000 + ((highSurrogate_ - 0xD800) << 10) + (cp - 0xDC00);
            highSurrogate_ = 0;
            if (cp >= 32 && cp != 127)
               Push({ PluginFrames::kInText, int32_t(cp), 0, Mods(), 0, 0 });
            return 0;
         }
         case WM_SETFOCUS:
            Push({ PluginFrames::kInFocus, 0, 1, 0, 0, 0 });
            return 0;
         case WM_KILLFOCUS:
            Push({ PluginFrames::kInFocus, 0, 0, 0, 0, 0 });
            return 0;
         case WM_DPICHANGED_AFTERPARENT:
            SendState(true);
            return 0;
         case WM_CTLCOLORSTATIC: // labels and the refund checkbox
         {
            HDC dc = HDC(wp);
            const HWND c = HWND(lp);
            const bool onCard = Unlicensed();
            SetTextColor(dc, Ref(c == title_ ? Sheet::kInk : c == refund_ ? Sheet::kInk3 : Sheet::kInk2));
            SetBkColor(dc, Ref(onCard ? Sheet::kCard : Sheet::kGround));
            return LRESULT(onCard ? Brush(cardBrush_, Sheet::kCard) : Background());
         }
         case WM_CTLCOLOREDIT:
         {
            HDC dc = HDC(wp);
            SetTextColor(dc, Ref(Sheet::kInk));
            SetBkColor(dc, Ref(Sheet::kDeep));
            return LRESULT(Brush(deepBrush_, Sheet::kDeep));
         }
         case WM_DRAWITEM:
            DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
         case WM_COMMAND:
            Command(LOWORD(wp));
            return 0;
         case WM_DROPFILES:
         {
            HDROP drop = HDROP(wp);
            wchar_t path[MAX_PATH * 2] = {};
            if (CanDrop() && DragQueryFileW(drop, 0, path, UINT(std::size(path))))
               SubmitLicense(ReadSmallFile(path));
            DragFinish(drop);
            return 0;
         }
         default:
            break;
         }
         return DefWindowProcW(h, msg, wp, lp);
      }

      void KeyFieldReturn() { Command(kIdKey); }

   private:
      static constexpr int32 kMinW = 640, kMinH = 400;
      static inline const ViewRect kDefault { 0, 0, 1280, 800 };

      static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
      {
         if (msg == WM_NCCREATE)
         {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(cs->lpCreateParams));
         }
         auto* self = reinterpret_cast<WinView*>(GetWindowLongPtrW(h, GWLP_USERDATA));
         if (self && self->hwnd_ == h)
            return self->Proc(h, msg, wp, lp);
         return DefWindowProcW(h, msg, wp, lp);
      }

      // The key field: Return submits, everything else is the stock EDIT behaviour.
      static LRESULT CALLBACK KeyFieldProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
      {
         auto old = reinterpret_cast<WNDPROC>(GetPropW(h, L"InfiniteOldProc"));
         if (msg == WM_CHAR && wp == VK_RETURN)
         {
            if (auto* self = reinterpret_cast<WinView*>(GetWindowLongPtrW(GetParent(h), GWLP_USERDATA)))
               self->KeyFieldReturn();
            return 0;
         }
         if (msg == WM_GETDLGCODE)
            return DLGC_WANTALLKEYS | (old ? CallWindowProcW(old, h, msg, wp, lp) : 0);
         return old ? CallWindowProcW(old, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
      }
      static void SetWindowSubclassCompat(HWND h)
      {
         SetPropW(h, L"InfiniteOldProc", reinterpret_cast<HANDLE>(GetWindowLongPtrW(h, GWLP_WNDPROC)));
         SetWindowLongPtrW(h, GWLP_WNDPROC, LONG_PTR(&KeyFieldProc));
      }

      static void RegisterClassOnce()
      {
         WNDCLASSEXW wc {};
         if (GetClassInfoExW(Module(), kClassName, &wc))
            return;
         wc.cbSize = sizeof(wc);
         wc.style = CS_DBLCLKS;
         wc.lpfnWndProc = &WndProc;
         wc.hInstance = Module();
         wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
         wc.lpszClassName = kClassName;
         RegisterClassExW(&wc);
      }

      HBRUSH Background() { return Brush(bg_, Sheet::kGround); }
      static HBRUSH Brush(HBRUSH& b, uint32_t c)
      {
         if (!b)
            b = CreateSolidBrush(Ref(c));
         return b;
      }

      // Ember with a Midnight label (Paste key), surface-2 with ink (Buy, Restart).
      void DrawButton(const DRAWITEMSTRUCT& di)
      {
         const bool primary = di.hwndItem == paste_;
         const bool down = (di.itemState & ODS_SELECTED) != 0;
         RECT r = di.rcItem;
         FillRect(di.hDC, &r, Unlicensed() ? Brush(cardBrush_, Sheet::kCard) : Background());
         const uint32_t fill = primary ? (down ? Sheet::kEmberDown : Sheet::kEmber) : (down ? Sheet::kLine : Sheet::kRaised);
         RoundFill(di.hDC, r, int(Sheet::kButtonR * Scale() + 0.5f), fill, fill);
         wchar_t text[64] = {};
         GetWindowTextW(di.hwndItem, text, int(std::size(text)));
         SetBkMode(di.hDC, TRANSPARENT);
         SetTextColor(di.hDC, Ref(primary ? Sheet::kGround : Sheet::kInk));
         HGDIOBJ of = SelectObject(di.hDC, buttonFont_);
         DrawTextW(di.hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
         SelectObject(di.hDC, of);
      }

      int TextW(HWND h, HFONT f) const
      {
         wchar_t text[256] = {};
         GetWindowTextW(h, text, int(std::size(text)));
         HDC dc = GetDC(hwnd_);
         HGDIOBJ of = SelectObject(dc, f);
         SIZE sz {};
         GetTextExtentPoint32W(dc, text, int(wcslen(text)), &sz);
         SelectObject(dc, of);
         ReleaseDC(hwnd_, dc);
         return sz.cx;
      }

      float Scale() const
      {
         if (hostScale_ > 0.0f)
            return hostScale_;
         if (hwnd_)
         {
            using DpiFn = UINT(WINAPI*)(HWND);
            static auto dpiFor = reinterpret_cast<DpiFn>(
               reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
            if (dpiFor)
               if (const UINT dpi = dpiFor(hwnd_))
                  return float(dpi) / 96.0f;
         }
         return 1.0f;
      }
      float Px(int v) const { return float(v) / Scale(); }

      void Push(const InputEvent& e)
      {
         if (host_ && hwnd_ && !Overlay())
            host_->ViewInput(&e, 1);
      }

      void SendState(bool open)
      {
         if (!host_)
            return;
         const float s = Scale();
         PluginFrames::ViewState st;
         st.open = open ? 1 : 0;
         st.width = uint32_t(std::max(1.0f, float(rect.getWidth()) / s));
         st.height = uint32_t(std::max(1.0f, float(rect.getHeight()) / s));
         st.scale = s;
         host_->ViewState(st);
      }

      bool Unlicensed() const { return status_ == InfinitePlugin::EngineStatus::Unlicensed; }
      bool CanDrop() const { return Unlicensed() && host_; }
      bool Overlay() const
      {
         using InfinitePlugin::EngineStatus;
         return status_ != EngineStatus::Running || popped_ || !shown_;
      }

      void SubmitLicense(const std::string& text)
      {
         if (host_ && Unlicensed())
            host_->ViewLicense(text);
      }

      static std::string ReadSmallFile(const wchar_t* path)
      {
         HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
         if (f == INVALID_HANDLE_VALUE)
            return {};
         std::string out;
         LARGE_INTEGER size {};
         if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= 65536)
         {
            out.resize(size_t(size.QuadPart));
            DWORD got = 0;
            if (!ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr))
               got = 0;
            out.resize(got);
         }
         CloseHandle(f);
         return out;
      }

      std::string Clipboard()
      {
         std::string out;
         if (!OpenClipboard(hwnd_))
            return out;
         if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h)))
            {
               out = Utf8(w);
               GlobalUnlock(h);
            }
         CloseClipboard();
         return out;
      }

      void Command(int id)
      {
         switch (id)
         {
         case kIdRestart:
            if (!host_)
               return;
            if (popped_)
               host_->ViewPopIn();
            else
               host_->ViewRestart();
            break;
         case kIdKey:
         {
            const int n = GetWindowTextLengthW(keyField_);
            std::wstring w(size_t(n) + 1, L'\0');
            GetWindowTextW(keyField_, w.data(), n + 1);
            w.resize(size_t(n));
            SubmitLicense(Utf8(w));
            break;
         }
         case kIdPaste:
            SubmitLicense(Clipboard());
            break;
         case kIdBuy:
            ShellExecuteW(nullptr, L"open", Wide(License::kBuyUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
         case kIdRefund:
            License::SetRefundCheck(SendMessageW(refund_, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
         default:
            break;
         }
      }

      void Layout()
      {
         if (!hwnd_)
            return;
         RECT c;
         GetClientRect(hwnd_, &c);
         const int W = c.right, H = c.bottom;
         const float s = Scale();
         auto px = [s](int v) { return int(v * s + 0.5f); };
         const int bh = px(Sheet::kButtonH), lh = px(20);
         if (IsWindowVisible(keyField_))
         {
            // The card (SheetStyle.h), centred; contents left-aligned at kPad.
            const int pad = px(Sheet::kPad), gap = px(Sheet::kGap);
            const int cw = std::max(px(200), std::min(px(Sheet::kCardW), W - px(32)));
            const int inner = cw - 2 * pad;
            const int th = px(28), rh = px(20), wh = px(Sheet::kWellH);
            const int ch = pad + th + px(4) + lh + px(16) + wh + gap + bh + gap + rh + pad;
            const int cx = (W - cw) / 2, cy = std::max(0, (H - ch) / 2);
            card_ = { cx, cy, cx + cw, cy + ch };
            const int x = cx + pad;
            int y = cy + pad;
            MoveWindow(title_, x, y, inner, th, TRUE);
            y += th + px(4);
            MoveWindow(label_, x, y, inner, lh, TRUE);
            y += lh + px(16);
            well_ = { x, y, x + inner, y + wh };
            const int fh = px(18);
            MoveWindow(keyField_, x + px(12), y + (wh - fh) / 2, inner - px(24), fh, TRUE);
            y += wh + gap;
            const int pw = TextW(paste_, buttonFont_) + px(2 * Sheet::kButtonPadX);
            const int uw = TextW(buy_, buttonFont_) + px(2 * Sheet::kButtonPadX);
            MoveWindow(paste_, x, y, pw, bh, TRUE);
            MoveWindow(buy_, x + pw + px(8), y, uw, bh, TRUE);
            y += bh + gap;
            MoveWindow(refund_, x, y, inner, rh, TRUE);
            return;
         }
         card_ = well_ = RECT {};
         const bool button = IsWindowVisible(restart_) != 0;
         const int total = lh + (button ? px(16) + bh : 0);
         const int top = (H - total) / 2;
         MoveWindow(label_, 0, top, W, lh, TRUE);
         const int rw = TextW(restart_, buttonFont_) + px(2 * Sheet::kButtonPadX);
         MoveWindow(restart_, (W - rw) / 2, top + lh + px(16), rw, bh, TRUE);
      }

      void UpdateOverlay()
      {
         if (!hwnd_)
            return;
         using InfinitePlugin::EngineStatus;
         const bool stopped = status_ == EngineStatus::Stopped;
         const bool noGl = status_ == EngineStatus::NoGraphics;
         const bool popped = popped_ && status_ == EngineStatus::Running;
         const bool unlicensed = Unlicensed();
         const bool overlay = Overlay();
         const int key = unlicensed ? SW_SHOW : SW_HIDE;
         ShowWindow(keyField_, key);
         ShowWindow(paste_, key);
         ShowWindow(buy_, key);
         ShowWindow(refund_, key);
         ShowWindow(title_, key);
         LONG_PTR st = GetWindowLongPtrW(label_, GWL_STYLE) & ~LONG_PTR(SS_TYPEMASK);
         SetWindowLongPtrW(label_, GWL_STYLE, st | (unlicensed ? SS_LEFT : SS_CENTER));
         if (unlicensed)
            SendMessageW(refund_, BM_SETCHECK, License::RefundCheckOn() ? BST_CHECKED : BST_UNCHECKED, 0);
         const std::wstring text = unlicensed ? Wide(license_)
                                 : noGl ? L"No graphics on this machine"
                                 : stopped ? L"Engine stopped"
                                 : popped ? L"In its own window"
                                          : L"Starting";
         SetWindowTextW(label_, text.c_str());
         ShowWindow(label_, overlay ? SW_SHOW : SW_HIDE);
         SetWindowTextW(restart_, popped ? L"Bring back" : L"Restart");
         ShowWindow(restart_, (stopped || popped) ? SW_SHOW : SW_HIDE);
         Layout();
         InvalidateRect(hwnd_, nullptr, TRUE);
      }

      void Tick()
      {
         if (Unlicensed() && host_ && ++recheckTicks_ >= 120)
         {
            recheckTicks_ = 0;
            host_->ViewLicenseRecheck();
         }
         if (!frames_ || popped_)
            return;
         if (!PluginFrames::Acquire(frames_, &front_))
            return;
         const PluginFrames::BufferInfo info = frames_->info[front_];
         if (info.width == 0 || info.height == 0 || uint64_t(info.stride) * info.height > frames_->capacity)
            return;
         haveFrame_ = true;
         if (!shown_ && status_ == InfinitePlugin::EngineStatus::Running)
         {
            shown_ = true;
            UpdateOverlay();
         }
         InvalidateRect(hwnd_, nullptr, FALSE);
      }

      void Paint()
      {
         PAINTSTRUCT ps;
         HDC dc = BeginPaint(hwnd_, &ps);
         RECT c;
         GetClientRect(hwnd_, &c);
         if (Overlay() || !haveFrame_ || !frames_)
         {
            FillRect(dc, &c, Background());
            if (Unlicensed() && card_.right > card_.left)
            {
               const float s = Scale();
               RoundFill(dc, card_, int(Sheet::kCardR * s + 0.5f), Sheet::kCard, Sheet::kRaised);
               RoundFill(dc, well_, int(Sheet::kWellR * s + 0.5f), Sheet::kDeep, Sheet::kLine);
            }
         }
         else
         {
            const PluginFrames::BufferInfo info = frames_->info[front_];
            BITMAPINFO bmi {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = LONG(info.stride / 4);
            bmi.bmiHeader.biHeight = -LONG(info.height); // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, 0, 0, c.right, c.bottom, 0, 0, int(info.width), int(info.height),
                          PluginFrames::Buffer(frames_, front_), &bmi, DIB_RGB_COLORS, SRCCOPY);
         }
         EndPaint(hwnd_, &ps);
      }

      void CloseRegion()
      {
         frames_ = nullptr;
         haveFrame_ = false;
         Ipc::SharedMemoryClose(&region_);
      }

      void Teardown()
      {
         if (hwnd_)
         {
            KillTimer(hwnd_, kTimer);
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_); // children go with it
            hwnd_ = nullptr;
         }
         if (font_)
            DeleteObject(font_);
         for (HGDIOBJ o : { HGDIOBJ(bg_), HGDIOBJ(cardBrush_), HGDIOBJ(deepBrush_), HGDIOBJ(titleFont_), HGDIOBJ(buttonFont_), HGDIOBJ(monoFont_) })
            if (o)
               DeleteObject(o);
         font_ = titleFont_ = buttonFont_ = monoFont_ = nullptr;
         bg_ = cardBrush_ = deepBrush_ = nullptr;
         CloseRegion();
      }

      InfinitePlugin::ViewHost* host_ = nullptr;
      HWND hwnd_ = nullptr, label_ = nullptr, restart_ = nullptr, keyField_ = nullptr, paste_ = nullptr,
           buy_ = nullptr, refund_ = nullptr, title_ = nullptr;
      HFONT font_ = nullptr, titleFont_ = nullptr, buttonFont_ = nullptr, monoFont_ = nullptr;
      HBRUSH bg_ = nullptr, cardBrush_ = nullptr, deepBrush_ = nullptr;
      RECT card_ {}, well_ {}; // licence sheet, painted behind the controls (client pixels)
      Ipc::SharedMemory region_ {};
      PluginFrames::Header* frames_ = nullptr;
      uint32_t front_ = 2;
      bool haveFrame_ = false, shown_ = false, popped_ = false, tracking_ = false;
      uint32_t buttons_ = 0, cursorId_ = 0, highSurrogate_ = 0;
      int recheckTicks_ = 0;
      float hostScale_ = 0.0f;
      InfinitePlugin::EngineStatus status_ = InfinitePlugin::EngineStatus::Starting;
      std::string license_;
   };
}

namespace InfinitePlugin
{
   bool ShellView::Supported() { return true; }
   ShellView* ShellView::Create(ViewHost* host) { return new WinView(host); }
}

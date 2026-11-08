// macOS editor view (ShellView.h). A layer-backed NSView: each new engine frame is copied from the
// shared-memory triple buffer into one of two IOSurfaces and set as the layer's contents, so Core
// Animation never reads memory the engine is writing. Input goes back as PluginFrames::InputEvent.
//
// The Objective-C class name carries the build (INFINITE_VIEW_CLASS): Instrument and FX are loaded
// side by side in one host and must not share a class.
#include "plugin/shell/ShellView.h"
#include "core/License.h"

#include "platform/common/Ipc.h"

#import <Carbon/Carbon.h> // kVK_* key codes (header constants only)
#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef INFINITE_VIEW_CLASS
#define INFINITE_VIEW_CLASS InfiniteShellView
#endif
#define INFINITE_CAT2(a, b) a##b
#define INFINITE_CAT(a, b) INFINITE_CAT2(a, b)
#define INFINITE_RESTART_CLASS INFINITE_CAT(INFINITE_VIEW_CLASS, Restart)

using namespace Steinberg;
using PluginFrames::InputEvent;

namespace
{
   // GLFW key and mod values (the engine feeds them to ImGui through the GLFW backend's table).
   enum : int
   {
      kGlfwModShift = 0x1, kGlfwModCtrl = 0x2, kGlfwModAlt = 0x4, kGlfwModSuper = 0x8, kGlfwModCaps = 0x10,
   };

   int GlfwKey(unsigned short code)
   {
      switch (code)
      {
      case kVK_ANSI_A: return 65; case kVK_ANSI_B: return 66; case kVK_ANSI_C: return 67; case kVK_ANSI_D: return 68;
      case kVK_ANSI_E: return 69; case kVK_ANSI_F: return 70; case kVK_ANSI_G: return 71; case kVK_ANSI_H: return 72;
      case kVK_ANSI_I: return 73; case kVK_ANSI_J: return 74; case kVK_ANSI_K: return 75; case kVK_ANSI_L: return 76;
      case kVK_ANSI_M: return 77; case kVK_ANSI_N: return 78; case kVK_ANSI_O: return 79; case kVK_ANSI_P: return 80;
      case kVK_ANSI_Q: return 81; case kVK_ANSI_R: return 82; case kVK_ANSI_S: return 83; case kVK_ANSI_T: return 84;
      case kVK_ANSI_U: return 85; case kVK_ANSI_V: return 86; case kVK_ANSI_W: return 87; case kVK_ANSI_X: return 88;
      case kVK_ANSI_Y: return 89; case kVK_ANSI_Z: return 90;
      case kVK_ANSI_0: return 48; case kVK_ANSI_1: return 49; case kVK_ANSI_2: return 50; case kVK_ANSI_3: return 51;
      case kVK_ANSI_4: return 52; case kVK_ANSI_5: return 53; case kVK_ANSI_6: return 54; case kVK_ANSI_7: return 55;
      case kVK_ANSI_8: return 56; case kVK_ANSI_9: return 57;
      case kVK_Space: return 32; case kVK_ANSI_Quote: return 39; case kVK_ANSI_Comma: return 44;
      case kVK_ANSI_Minus: return 45; case kVK_ANSI_Period: return 46; case kVK_ANSI_Slash: return 47;
      case kVK_ANSI_Semicolon: return 59; case kVK_ANSI_Equal: return 61; case kVK_ANSI_LeftBracket: return 91;
      case kVK_ANSI_Backslash: return 92; case kVK_ANSI_RightBracket: return 93; case kVK_ANSI_Grave: return 96;
      case kVK_Escape: return 256; case kVK_Return: return 257; case kVK_Tab: return 258; case kVK_Delete: return 259;
      case kVK_ForwardDelete: return 261; case kVK_RightArrow: return 262; case kVK_LeftArrow: return 263;
      case kVK_DownArrow: return 264; case kVK_UpArrow: return 265; case kVK_PageUp: return 266;
      case kVK_PageDown: return 267; case kVK_Home: return 268; case kVK_End: return 269;
      case kVK_F1: return 290; case kVK_F2: return 291; case kVK_F3: return 292; case kVK_F4: return 293;
      case kVK_F5: return 294; case kVK_F6: return 295; case kVK_F7: return 296; case kVK_F8: return 297;
      case kVK_F9: return 298; case kVK_F10: return 299; case kVK_F11: return 300; case kVK_F12: return 301;
      case kVK_ANSI_Keypad0: return 320; case kVK_ANSI_Keypad1: return 321; case kVK_ANSI_Keypad2: return 322;
      case kVK_ANSI_Keypad3: return 323; case kVK_ANSI_Keypad4: return 324; case kVK_ANSI_Keypad5: return 325;
      case kVK_ANSI_Keypad6: return 326; case kVK_ANSI_Keypad7: return 327; case kVK_ANSI_Keypad8: return 328;
      case kVK_ANSI_Keypad9: return 329; case kVK_ANSI_KeypadDecimal: return 330; case kVK_ANSI_KeypadDivide: return 331;
      case kVK_ANSI_KeypadMultiply: return 332; case kVK_ANSI_KeypadMinus: return 333; case kVK_ANSI_KeypadPlus: return 334;
      case kVK_ANSI_KeypadEnter: return 335; case kVK_ANSI_KeypadEquals: return 336;
      case kVK_Shift: return 340; case kVK_Control: return 341; case kVK_Option: return 342; case kVK_Command: return 343;
      case kVK_RightShift: return 344; case kVK_RightControl: return 345; case kVK_RightOption: return 346;
      case kVK_RightCommand: return 347;
      default: return -1;
      }
   }

   uint32_t Mods(NSEventModifierFlags f)
   {
      uint32_t m = 0;
      if (f & NSEventModifierFlagShift) m |= kGlfwModShift;
      if (f & NSEventModifierFlagControl) m |= kGlfwModCtrl;
      if (f & NSEventModifierFlagOption) m |= kGlfwModAlt;
      if (f & NSEventModifierFlagCommand) m |= kGlfwModSuper;
      if (f & NSEventModifierFlagCapsLock) m |= kGlfwModCaps;
      return m;
   }

   // The modifier a flagsChanged key code toggles.
   NSEventModifierFlags FlagFor(unsigned short code)
   {
      switch (code)
      {
      case kVK_Shift: case kVK_RightShift: return NSEventModifierFlagShift;
      case kVK_Control: case kVK_RightControl: return NSEventModifierFlagControl;
      case kVK_Option: case kVK_RightOption: return NSEventModifierFlagOption;
      case kVK_Command: case kVK_RightCommand: return NSEventModifierFlagCommand;
      default: return 0;
      }
   }

   NSCursor* CursorFor(uint32_t c)
   {
      switch (c)
      {
      case PluginFrames::kCursorText: return [NSCursor IBeamCursor];
      case PluginFrames::kCursorResizeAll: return [NSCursor closedHandCursor];
      case PluginFrames::kCursorResizeNS: return [NSCursor resizeUpDownCursor];
      case PluginFrames::kCursorResizeEW: return [NSCursor resizeLeftRightCursor];
      case PluginFrames::kCursorResizeNESW:
      case PluginFrames::kCursorResizeNWSE: return [NSCursor crosshairCursor];
      case PluginFrames::kCursorHand: return [NSCursor pointingHandCursor];
      case PluginFrames::kCursorNotAllowed: return [NSCursor operationNotAllowedCursor];
      default: return [NSCursor arrowCursor];
      }
   }

   class MacView;
}

@interface INFINITE_VIEW_CLASS : NSView
{
@public
   MacView* owner;
   NSTrackingArea* tracking;
   NSCursor* cursor;
}
@end

@interface INFINITE_RESTART_CLASS : NSObject
{
@public
   MacView* owner;
}
- (void)restart:(id)sender;
- (void)pasteKey:(id)sender;
- (void)enterKey:(id)sender;
- (void)buy:(id)sender;
- (void)refundCheck:(id)sender;
@end

namespace
{
   class MacView final : public InfinitePlugin::ShellView
   {
   public:
      explicit MacView(InfinitePlugin::ViewHost* host) : ShellView(&kDefault), host_(host) {}
      ~MacView() override
      {
         if (view_)
            SendState(false); // destroyed without removed()
         Teardown();
         if (host_)
            host_->ViewGone(this);
      }

      // ---- IPlugView ----
      tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
      {
         return type && std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
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
         r->right = r->left + std::max<int32>(r->getWidth(), kMinW);
         r->bottom = r->top + std::max<int32>(r->getHeight(), kMinH);
         return kResultTrue;
      }
      tresult PLUGIN_API onSize(ViewRect* r) override
      {
         CPluginView::onSize(r);
         if (view_)
         {
            [view_ setFrame:NSMakeRect(0, 0, rect.getWidth(), rect.getHeight())];
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
         NSView* parent = (NSView*)systemWindow;
         view_ = [[INFINITE_VIEW_CLASS alloc] initWithFrame:NSMakeRect(0, 0, rect.getWidth(), rect.getHeight())];
         view_->owner = this;
         view_.wantsLayer = YES;
         view_.layer.opaque = YES;
         bg_ = CGColorCreateGenericRGB(0.09, 0.09, 0.10, 1.0);
         view_.layer.backgroundColor = bg_;
         view_.layer.contentsGravity = kCAGravityResize;
         view_.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;

         label_ = [NSTextField labelWithString:@""];
         label_.textColor = [NSColor colorWithWhite:0.75 alpha:1.0];
         label_.font = [NSFont systemFontOfSize:13];
         [label_ retain];
         [view_ addSubview:label_];
         restart_ = [NSButton buttonWithTitle:@"Restart" target:nil action:nil];
         [restart_ retain];
         restart_.bezelStyle = NSBezelStyleRounded;
         [view_ addSubview:restart_];
         restartTarget_ = [[INFINITE_RESTART_CLASS alloc] init];
         restartTarget_->owner = this;
         restart_.target = restartTarget_;
         restart_.action = @selector(restart:);

         // Licence sheet (step 2.6): shown only while Unlicensed.
         keyField_ = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 24)];
         keyField_.placeholderString = @"INF1\u2026";
         keyField_.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
         keyField_.target = restartTarget_;
         keyField_.action = @selector(enterKey:);
         [keyField_.cell setScrollable:YES];
         [view_ addSubview:keyField_];
         paste_ = [NSButton buttonWithTitle:@"Paste key" target:restartTarget_ action:@selector(pasteKey:)];
         [paste_ retain];
         paste_.bezelStyle = NSBezelStyleRounded;
         [view_ addSubview:paste_];
         buy_ = [NSButton buttonWithTitle:@"Buy" target:restartTarget_ action:@selector(buy:)];
         [buy_ retain];
         buy_.bezelStyle = NSBezelStyleRounded;
         [view_ addSubview:buy_];
         refund_ = [NSButton checkboxWithTitle:@"Weekly refund check" target:restartTarget_ action:@selector(refundCheck:)];
         [refund_ retain];
         refund_.state = License::RefundCheckOn() ? NSControlStateValueOn : NSControlStateValueOff;
         [view_ addSubview:refund_];
         [view_ registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];

         [parent addSubview:view_];
         UpdateOverlay();
         Layout();

         // 120 Hz on the main run loop, in every mode (a host's modal loop or a live resize keeps it).
         // Twice the engine's frame rate halves the wait for a new frame; a tick with none is one
         // atomic load.
         MacView* self = this;
         timer_ = [NSTimer timerWithTimeInterval:1.0 / 120.0 repeats:YES block:^(NSTimer*) { self->Tick(); }];
         [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
         [timer_ retain];
         SendState(true);
      }

      void removedFromParent() override
      {
         SendState(false);
         Teardown();
      }

      // ---- ShellView ----
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
      }

      void SetCursor(uint32_t c) override
      {
         cursorId_ = c;
         if (!view_)
            return;
         view_->cursor = CursorFor(c);
         NSPoint p = [view_ convertPoint:[view_.window mouseLocationOutsideOfEventStream] fromView:nil];
         if (view_.window && NSPointInRect(p, view_.bounds))
            [view_->cursor set];
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
         shown_ = false; // back in: the overlay stays until the next frame lands
         UpdateOverlay();
      }

      // ---- from the NSView ----
      void Push(const InputEvent& e) { if (host_ && view_) host_->ViewInput(&e, 1); }

      NSPoint Point(NSEvent* ev) const { return [view_ convertPoint:ev.locationInWindow fromView:nil]; }

      void Mouse(NSEvent* ev, uint32_t type, int button, int down)
      {
         const NSPoint p = Point(ev);
         Push({ type, button, down, Mods(ev.modifierFlags), float(p.x), float(p.y) });
      }

      void Scroll(NSEvent* ev)
      {
         double dx = ev.scrollingDeltaX, dy = ev.scrollingDeltaY;
         if (ev.hasPreciseScrollingDeltas)
         {
            dx *= 0.1;
            dy *= 0.1;
         }
         if (dx != 0.0 || dy != 0.0)
            Push({ PluginFrames::kInScroll, 0, 0, Mods(ev.modifierFlags), float(dx), float(dy) });
      }

      void Key(NSEvent* ev, bool down)
      {
         const int key = GlfwKey(ev.keyCode);
         const uint32_t mods = Mods(ev.modifierFlags);
         if (key >= 0)
            Push({ PluginFrames::kInKey, key, down ? (ev.isARepeat ? 2 : 1) : 0, mods, 0, 0 });
         // Text: plain and shifted typing only (no IME in 2.3; README 8.3 limits). Command and
         // Control chords are shortcuts, never text.
         if (!down || (ev.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)))
            return;
         NSString* chars = ev.characters;
         for (NSUInteger i = 0; i < chars.length; i++)
         {
            unichar c = [chars characterAtIndex:i];
            uint32_t cp = c;
            if (CFStringIsSurrogateHighCharacter(c) && i + 1 < chars.length)
               cp = CFStringGetLongCharacterForSurrogatePair(c, [chars characterAtIndex:++i]);
            if (cp < 32 || cp == 127 || (cp >= 0xF700 && cp <= 0xF8FF))
               continue; // control and function keys
            Push({ PluginFrames::kInText, int32_t(cp), 0, mods, 0, 0 });
         }
      }

      void Flags(NSEvent* ev)
      {
         const int key = GlfwKey(ev.keyCode);
         const NSEventModifierFlags flag = FlagFor(ev.keyCode);
         if (key < 0 || !flag)
            return;
         Push({ PluginFrames::kInKey, key, (ev.modifierFlags & flag) ? 1 : 0, Mods(ev.modifierFlags), 0, 0 });
      }

      void Leave() { Push({ PluginFrames::kInLeave, 0, 0, 0, 0, 0 }); }
      void BackingChanged() { SendState(true); }
      // The overlay's one button: "Bring back" while popped out, else "Restart".
      void Restart()
      {
         if (!host_)
            return;
         if (popped_)
            host_->ViewPopIn();
         else
            host_->ViewRestart();
      }
      NSCursor* Cursor() const { return CursorFor(cursorId_); }

      // ---- licence sheet ----
      bool Unlicensed() const { return status_ == InfinitePlugin::EngineStatus::Unlicensed; }
      void SubmitLicense(const std::string& text)
      {
         if (host_ && Unlicensed())
            host_->ViewLicense(text);
      }
      // A button, not only Cmd+V in the field: some hosts keep Cmd+V for themselves.
      void PasteKey()
      {
         NSString* str = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
         SubmitLicense(str ? std::string(str.UTF8String) : std::string());
      }
      void EnterKey() { SubmitLicense(std::string(keyField_.stringValue.UTF8String ?: "")); }
      void Buy() { [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@(License::kBuyUrl)]]; }
      void RefundCheck() { License::SetRefundCheck(refund_.state == NSControlStateValueOn); }

      // A dropped .inflic (or any small text file holding a key), or dropped text.
      bool CanDrop() const { return Unlicensed() && host_; }
      bool Drop(NSPasteboard* pb)
      {
         if (!CanDrop())
            return false;
         NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[ [NSURL class] ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
         if (urls.count > 0)
         {
            NSData* data = [NSData dataWithContentsOfURL:urls[0]];
            if (!data || data.length > 65536)
            {
               SubmitLicense(std::string());
               return true;
            }
            SubmitLicense(std::string(static_cast<const char*>(data.bytes), data.length));
            return true;
         }
         NSString* str = [pb stringForType:NSPasteboardTypeString];
         if (!str)
            return false;
         SubmitLicense(std::string(str.UTF8String));
         return true;
      }

   private:
      static constexpr int32 kMinW = 640, kMinH = 400;
      static inline const ViewRect kDefault { 0, 0, 1280, 800 };

      void SendState(bool open)
      {
         if (!host_)
            return;
         PluginFrames::ViewState s;
         s.open = open ? 1 : 0;
         s.width = uint32_t(std::max<int32>(rect.getWidth(), 1));
         s.height = uint32_t(std::max<int32>(rect.getHeight(), 1));
         s.scale = view_ && view_.window ? float(view_.window.backingScaleFactor) : 2.0f;
         if (view_)
            view_.layer.contentsScale = s.scale;
         host_->ViewState(s);
      }

      void Layout()
      {
         if (!view_)
            return;
         const NSRect b = view_.bounds;
         [label_ sizeToFit];
         const NSSize ls = label_.frame.size;
         const NSSize bs = restart_.frame.size;
         const bool button = !restart_.hidden;
         if (!keyField_.hidden)
         {
            // label / key field / [Paste key] [Buy] / refund check, centred.
            const CGFloat fw = std::floor(std::max<CGFloat>(120, std::min<CGFloat>(360, b.size.width - 32)));
            [paste_ sizeToFit];
            [buy_ sizeToFit];
            [refund_ sizeToFit];
            const NSSize ps = paste_.frame.size, us = buy_.frame.size, rs = refund_.frame.size;
            const CGFloat total = ls.height + 12 + 24 + 12 + ps.height + 10 + rs.height;
            CGFloat y = std::floor((b.size.height - total) * 0.5);
            [label_ setFrameOrigin:NSMakePoint(std::floor((b.size.width - ls.width) * 0.5), y)];
            y += ls.height + 12;
            [keyField_ setFrame:NSMakeRect(std::floor((b.size.width - fw) * 0.5), y, fw, 24)];
            y += 24 + 12;
            const CGFloat row = ps.width + 8 + us.width;
            [paste_ setFrameOrigin:NSMakePoint(std::floor((b.size.width - row) * 0.5), y)];
            [buy_ setFrameOrigin:NSMakePoint(std::floor((b.size.width - row) * 0.5) + ps.width + 8, y)];
            y += ps.height + 10;
            [refund_ setFrameOrigin:NSMakePoint(std::floor((b.size.width - rs.width) * 0.5), y)];
            return;
         }
         const CGFloat total = ls.height + (button ? 12 + bs.height : 0);
         const CGFloat top = (b.size.height - total) * 0.5; // flipped: y down
         [label_ setFrameOrigin:NSMakePoint(std::floor((b.size.width - ls.width) * 0.5), std::floor(top))];
         [restart_ setFrameOrigin:NSMakePoint(std::floor((b.size.width - bs.width) * 0.5), std::floor(top + ls.height + 12))];
      }

      void UpdateOverlay()
      {
         if (!view_)
            return;
         using InfinitePlugin::EngineStatus;
         const bool stopped = status_ == EngineStatus::Stopped;
         const bool noGl = status_ == EngineStatus::NoGraphics;
         const bool popped = popped_ && status_ == EngineStatus::Running;
         const bool unlicensed = status_ == EngineStatus::Unlicensed;
         const bool overlay = stopped || noGl || popped || unlicensed || !shown_;
         keyField_.hidden = !unlicensed;
         paste_.hidden = !unlicensed;
         buy_.hidden = !unlicensed;
         refund_.hidden = !unlicensed;
         if (unlicensed)
            refund_.state = License::RefundCheckOn() ? NSControlStateValueOn : NSControlStateValueOff;
         label_.stringValue = unlicensed ? [NSString stringWithUTF8String:license_.c_str()] ?: @""
                            : noGl ? @"No graphics on this machine"
                            : stopped ? @"Engine stopped"
                            : popped ? @"In its own window"
                                     : @"Starting";
         label_.hidden = !overlay;
         restart_.title = popped ? @"Bring back" : @"Restart";
         restart_.hidden = !(stopped || popped);
         if (overlay)
            view_.layer.contents = nil;
         Layout();
      }

      bool EnsureSurface(int i, uint32_t w, uint32_t h)
      {
         if (surf_[i] && IOSurfaceGetWidth(surf_[i]) == w && IOSurfaceGetHeight(surf_[i]) == h)
            return true;
         if (surf_[i])
            CFRelease(surf_[i]);
         NSDictionary* props = @{
            (id)kIOSurfaceWidth: @(w),
            (id)kIOSurfaceHeight: @(h),
            (id)kIOSurfaceBytesPerElement: @4,
            (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
         };
         surf_[i] = IOSurfaceCreate((CFDictionaryRef)props);
         return surf_[i] != nullptr;
      }

      void Tick()
      {
         // A key entered in another instance (or another host) starts this one within a second.
         if (Unlicensed() && host_ && view_ && ++recheckTicks_ >= 120)
         {
            recheckTicks_ = 0;
            host_->ViewLicenseRecheck();
         }
         if (!frames_ || !view_ || popped_)
            return;
         if (!PluginFrames::Acquire(frames_, &front_))
            return;
         const PluginFrames::BufferInfo info = frames_->info[front_];
         if (info.width == 0 || info.height == 0 || uint64_t(info.stride) * info.height > frames_->capacity)
            return;
         const uint64_t t0 = Ipc::MonotonicNs();
         const int i = next_;
         if (!EnsureSurface(i, info.width, info.height))
            return;
         IOSurfaceLock(surf_[i], 0, nullptr);
         uint8_t* dst = static_cast<uint8_t*>(IOSurfaceGetBaseAddress(surf_[i]));
         const size_t dstStride = IOSurfaceGetBytesPerRow(surf_[i]);
         const uint8_t* src = PluginFrames::Buffer(frames_, front_);
         if (dstStride == info.stride)
            std::memcpy(dst, src, size_t(info.stride) * info.height);
         else
            for (uint32_t y = 0; y < info.height; y++)
               std::memcpy(dst + y * dstStride, src + size_t(y) * info.stride, size_t(info.width) * 4);
         IOSurfaceUnlock(surf_[i], 0, nullptr);
         [CATransaction begin];
         [CATransaction setDisableActions:YES];
         view_.layer.contents = (id)surf_[i];
         [CATransaction commit];
         next_ = 1 - i;
         blitUsSum_ += (Ipc::MonotonicNs() - t0) / 1000;
         if (!blits_)
            firstSeq_ = info.seq;
         lastSeq_ = info.seq;
         blits_++;
         if (!shown_ && status_ == InfinitePlugin::EngineStatus::Running)
         {
            shown_ = true;
            UpdateOverlay();
         }
      }

      void CloseRegion()
      {
         frames_ = nullptr;
         Ipc::SharedMemoryClose(&region_);
      }

      void Teardown()
      {
         if (timer_)
         {
            [timer_ invalidate];
            [timer_ release];
            timer_ = nil;
         }
         if (view_)
         {
            WriteStats();
            view_->owner = nullptr;
            restart_.target = nil;
            keyField_.target = nil;
            paste_.target = nil;
            buy_.target = nil;
            refund_.target = nil;
            [view_ unregisterDraggedTypes];
            [view_ removeFromSuperview];
            [label_ release];
            [restart_ release];
            [keyField_ release];
            [paste_ release];
            [buy_ release];
            [refund_ release];
            keyField_ = nil;
            paste_ = nil;
            buy_ = nil;
            refund_ = nil;
            restartTarget_->owner = nullptr;
            [restartTarget_ release];
            [view_ release];
            CGColorRelease(bg_);
            view_ = nil;
            label_ = nil;
            restart_ = nil;
            restartTarget_ = nil;
            bg_ = nullptr;
         }
         for (IOSurfaceRef& s : surf_)
            if (s)
            {
               CFRelease(s);
               s = nullptr;
            }
         CloseRegion();
         shown_ = false;
      }

      // Development and tests (tools/plugin): $INFINITE_PLUGIN_VIEW_STATS gets one line per closed
      // view: frames shown, the mean blit (copy into the surface + commit) in microseconds, and the engine
// frame numbers of the first and last frame shown.
      void WriteStats()
      {
         const char* path = getenv("INFINITE_PLUGIN_VIEW_STATS");
         if (!path || !*path)
            return;
         if (FILE* f = std::fopen(path, "a"))
         {
            std::fprintf(f, "view frames %llu blitUs %llu firstSeq %llu lastSeq %llu\n", (unsigned long long)blits_,
                         (unsigned long long)(blits_ ? blitUsSum_ / blits_ : 0), (unsigned long long)firstSeq_,
                         (unsigned long long)lastSeq_);
            std::fclose(f);
         }
      }

      InfinitePlugin::ViewHost* host_;
      INFINITE_VIEW_CLASS* view_ = nil;
      NSTextField* label_ = nil;
      NSButton* restart_ = nil;
      NSTextField* keyField_ = nil;
      NSButton* paste_ = nil;
      NSButton* buy_ = nil;
      NSButton* refund_ = nil;
      std::string license_ = "Enter your key";
      int recheckTicks_ = 0;
      INFINITE_RESTART_CLASS* restartTarget_ = nil; // NSButton holds its target weakly
      NSTimer* timer_ = nil;
      CGColorRef bg_ = nullptr;
      Ipc::SharedMemory region_;
      PluginFrames::Header* frames_ = nullptr;
      uint32_t front_ = 2;
      IOSurfaceRef surf_[2] = {};
      int next_ = 0;
      uint32_t cursorId_ = PluginFrames::kCursorArrow;
      InfinitePlugin::EngineStatus status_ = InfinitePlugin::EngineStatus::Starting;
      bool shown_ = false;
      bool popped_ = false;
      uint64_t blitUsSum_ = 0, blits_ = 0, firstSeq_ = 0, lastSeq_ = 0;

   };
}

@implementation INFINITE_VIEW_CLASS
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }
- (void)updateLayer {}

- (void)updateTrackingAreas
{
   if (tracking)
   {
      [self removeTrackingArea:tracking];
      [tracking release];
   }
   tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                           options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingCursorUpdate |
                                                   NSTrackingActiveAlways | NSTrackingInVisibleRect
                                             owner:self
                                          userInfo:nil];
   [self addTrackingArea:tracking];
   [super updateTrackingAreas];
}

- (void)dealloc
{
   if (tracking)
   {
      [self removeTrackingArea:tracking];
      [tracking release];
   }
   [super dealloc];
}

- (void)cursorUpdate:(NSEvent*)event
{
   if (owner)
      [owner->Cursor() set];
}

- (void)viewDidChangeBackingProperties
{
   [super viewDidChangeBackingProperties];
   if (owner)
      owner->BackingChanged();
}

- (void)viewDidMoveToWindow
{
   [super viewDidMoveToWindow];
   if (owner)
      owner->BackingChanged();
}

- (void)mouseMoved:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseMove, 0, 0); }
- (void)mouseDragged:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseMove, 0, 0); }
- (void)rightMouseDragged:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseMove, 0, 0); }
- (void)otherMouseDragged:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseMove, 0, 0); }
- (void)mouseDown:(NSEvent*)e
{
   [self.window makeFirstResponder:self];
   if (owner) owner->Mouse(e, PluginFrames::kInMouseButton, 0, 1);
}
- (void)mouseUp:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseButton, 0, 0); }
- (void)rightMouseDown:(NSEvent*)e
{
   [self.window makeFirstResponder:self];
   if (owner) owner->Mouse(e, PluginFrames::kInMouseButton, 1, 1);
}
- (void)rightMouseUp:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseButton, 1, 0); }
- (void)otherMouseDown:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseButton, int(e.buttonNumber > 4 ? 4 : e.buttonNumber), 1); }
- (void)otherMouseUp:(NSEvent*)e { if (owner) owner->Mouse(e, PluginFrames::kInMouseButton, int(e.buttonNumber > 4 ? 4 : e.buttonNumber), 0); }
- (void)mouseExited:(NSEvent*)e { if (owner) owner->Leave(); }
- (void)scrollWheel:(NSEvent*)e { if (owner) owner->Scroll(e); }
- (void)keyDown:(NSEvent*)e { if (owner) owner->Key(e, true); }
- (void)keyUp:(NSEvent*)e { if (owner) owner->Key(e, false); }
- (void)flagsChanged:(NSEvent*)e { if (owner) owner->Flags(e); }

// Command chords reach a focused view as key equivalents first. Infinite gets them (undo, copy,
// zoom), except the ones a host owns: save, quit, close, hide, minimise.
- (BOOL)performKeyEquivalent:(NSEvent*)e
{
   if (!owner || self.window.firstResponder != self || e.type != NSEventTypeKeyDown)
      return [super performKeyEquivalent:e];
   switch (e.keyCode)
   {
   case kVK_ANSI_S: case kVK_ANSI_Q: case kVK_ANSI_W: case kVK_ANSI_H: case kVK_ANSI_M:
      return [super performKeyEquivalent:e];
   default:
      break;
   }
   owner->Key(e, true);
   NSEvent* up = [NSEvent keyEventWithType:NSEventTypeKeyUp location:e.locationInWindow modifierFlags:e.modifierFlags
                                 timestamp:e.timestamp windowNumber:e.windowNumber context:nil characters:e.characters
               charactersIgnoringModifiers:e.charactersIgnoringModifiers isARepeat:NO keyCode:e.keyCode];
   owner->Key(up, false); // AppKit sends no keyUp for a handled key equivalent
   return YES;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info
{
   return owner && owner->CanDrop() ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info
{
   return owner && owner->Drop(info.draggingPasteboard);
}

- (BOOL)becomeFirstResponder
{
   if (owner) owner->Push({ PluginFrames::kInFocus, 0, 1, 0, 0, 0 });
   return YES;
}
- (BOOL)resignFirstResponder
{
   if (owner) owner->Push({ PluginFrames::kInFocus, 0, 0, 0, 0, 0 });
   return YES;
}
@end

@implementation INFINITE_RESTART_CLASS
- (void)restart:(id)sender
{
   if (owner)
      owner->Restart();
}
- (void)pasteKey:(id)sender { if (owner) owner->PasteKey(); }
- (void)enterKey:(id)sender { if (owner) owner->EnterKey(); }
- (void)buy:(id)sender { if (owner) owner->Buy(); }
- (void)refundCheck:(id)sender { if (owner) owner->RefundCheck(); }
@end

namespace InfinitePlugin
{
   bool ShellView::Supported() { return true; }
   ShellView* ShellView::Create(ViewHost* host) { return new MacView(host); }
}

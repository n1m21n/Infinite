// plugin_shell_check's view helpers (ShellCheck.cpp, step 2.3): an offscreen window to attach the
// editor to, the pixels the view is showing (its layer's IOSurface), and synthesized AppKit events
// sent straight to the view, as a host's window would.
#include "ShellCheckMac.h"

#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

namespace ViewCheck
{
   void* NewParent(int w, int h, double* scale)
   {
      [NSApplication sharedApplication];
      NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
      win.releasedWhenClosed = NO;
      if (scale)
         *scale = win.backingScaleFactor;
      return (void*)win.contentView;
   }

   void FreeParent(void* parent)
   {
      NSView* v = (NSView*)parent;
      NSWindow* win = v.window;
      [win close];
      [win release];
   }

   void* Child(void* parent)
   {
      NSView* v = (NSView*)parent;
      return v.subviews.count ? (void*)v.subviews[0] : nullptr;
   }

   bool Surface(void* view, int* w, int* h, std::vector<uint32_t>* samples)
   {
      NSView* v = (NSView*)view;
      if (!v || !v.layer)
         return false;
      id contents = v.layer.contents;
      if (!contents || CFGetTypeID((CFTypeRef)contents) != IOSurfaceGetTypeID())
         return false;
      IOSurfaceRef s = (IOSurfaceRef)contents;
      *w = int(IOSurfaceGetWidth(s));
      *h = int(IOSurfaceGetHeight(s));
      if (samples)
      {
         samples->clear();
         IOSurfaceLock(s, kIOSurfaceLockReadOnly, nullptr);
         const uint8_t* base = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(s));
         const size_t stride = IOSurfaceGetBytesPerRow(s);
         for (int y = 0; y < 96; y++)
            for (int x = 0; x < 96; x++)
            {
               const size_t px = size_t((x * 2 + 1) * *w / 192), py = size_t((y * 2 + 1) * *h / 192);
               samples->push_back(*reinterpret_cast<const uint32_t*>(base + py * stride + px * 4));
            }
         IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, nullptr);
      }
      return true;
   }

   void Key(void* view, unsigned short keyCode, const char* chars, unsigned long mods, bool down)
   {
      NSView* v = (NSView*)view;
      NSString* c = [NSString stringWithUTF8String:chars ? chars : ""];
      NSEvent* e = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp location:NSZeroPoint
                               modifierFlags:mods timestamp:[NSProcessInfo processInfo].systemUptime
                                windowNumber:v.window.windowNumber context:nil characters:c
                 charactersIgnoringModifiers:c isARepeat:NO keyCode:keyCode];
      if (down)
         [v keyDown:e];
      else
         [v keyUp:e];
   }

   void Flags(void* view, unsigned short keyCode, unsigned long mods)
   {
      NSView* v = (NSView*)view;
      NSEvent* e = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint modifierFlags:mods
                                   timestamp:[NSProcessInfo processInfo].systemUptime windowNumber:v.window.windowNumber
                                     context:nil characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:keyCode];
      [v flagsChanged:e];
   }

   void Mouse(void* view, int kind, double x, double y)
   {
      NSView* v = (NSView*)view;
      const NSPoint inWindow = [v convertPoint:NSMakePoint(x, y) toView:nil];
      const NSEventType type = kind == 1 ? NSEventTypeLeftMouseDown : kind == 2 ? NSEventTypeLeftMouseUp : NSEventTypeMouseMoved;
      NSEvent* e = [NSEvent mouseEventWithType:type location:inWindow modifierFlags:0
                                     timestamp:[NSProcessInfo processInfo].systemUptime windowNumber:v.window.windowNumber
                                       context:nil eventNumber:0 clickCount:1 pressure:kind == 1 ? 1.0f : 0.0f];
      if (kind == 1)
         [v mouseDown:e];
      else if (kind == 2)
         [v mouseUp:e];
      else
         [v mouseMoved:e];
   }

   std::string Label(void* view)
   {
      for (NSView* sub in ((NSView*)view).subviews)
         if ([sub isKindOfClass:[NSTextField class]] && !sub.hidden)
            return std::string(((NSTextField*)sub).stringValue.UTF8String ?: "");
      return std::string();
   }

   std::string Button(void* view)
   {
      for (NSView* sub in ((NSView*)view).subviews)
         if ([sub isKindOfClass:[NSButton class]] && !sub.hidden)
            return std::string(((NSButton*)sub).title.UTF8String ?: "");
      return std::string();
   }

   bool Snapshot(void* view, const char* pngPath)
   {
      NSView* v = (NSView*)view;
      [v.window displayIfNeeded];
      [v displayIfNeeded];
      const CGFloat scale = v.window ? v.window.backingScaleFactor : 2.0;
      const size_t w = size_t(v.bounds.size.width * scale), h = size_t(v.bounds.size.height * scale);
      CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, 0, cs, kCGImageAlphaPremultipliedLast);
      CGColorSpaceRelease(cs);
      if (!ctx)
         return false;
      CGContextScaleCTM(ctx, scale, scale);
      if (v.isFlipped)
      {
         CGContextTranslateCTM(ctx, 0, v.bounds.size.height);
         CGContextScaleCTM(ctx, 1, -1);
      }
      [v.layer renderInContext:ctx];
      CGImageRef img = CGBitmapContextCreateImage(ctx);
      CGContextRelease(ctx);
      NSBitmapImageRep* rep = [[[NSBitmapImageRep alloc] initWithCGImage:img] autorelease];
      CGImageRelease(img);
      NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
      return [png writeToFile:@(pngPath) atomically:YES];
   }

   void Press(void* view)
   {
      for (NSView* sub in ((NSView*)view).subviews)
         if ([sub isKindOfClass:[NSButton class]] && !sub.hidden)
         {
            [(NSButton*)sub performClick:nil];
            return;
         }
   }

   bool TypeKey(void* view, const std::string& text)
   {
      for (NSView* sub in ((NSView*)view).subviews)
         if ([sub isKindOfClass:[NSTextField class]] && !sub.hidden && ((NSTextField*)sub).editable)
         {
            NSTextField* f = (NSTextField*)sub;
            f.stringValue = [NSString stringWithUTF8String:text.c_str()] ?: @"";
            [f sendAction:f.action to:f.target];
            return true;
         }
      return false;
   }

   int OnScreenWindows(int pid)
   {
      int n = 0;
      CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
      if (!list)
         return 0;
      for (NSDictionary* w in (NSArray*)list)
         if ([w[(id)kCGWindowOwnerPID] intValue] == pid && [w[(id)kCGWindowLayer] intValue] == 0)
            n++;
      CFRelease(list);
      return n;
   }

   bool ScreenLocked()
   {
      CFDictionaryRef d = CGSessionCopyCurrentDictionary();
      if (!d)
         return false;
      const bool locked = [((NSDictionary*)d)[@"CGSSessionScreenIsLocked"] boolValue];
      CFRelease(d);
      return locked;
   }

   void Pump(int ms)
   {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, ms / 1000.0, false);
   }
}

#include "platform/WatchLinkInternal.h"

// Windows and Linux: no radio yet (Block C: WinRT GattServiceProvider, BlueZ GATT server). The table in
// WatchLink.cpp still works, so the node and its tests behave the same; Status says "macOS only for now".
namespace WatchLink
{
   namespace Transport
   {
      bool Supported() { return false; }
      void Begin() {}
      void End() {}
   }
}

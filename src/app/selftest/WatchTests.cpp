// INFINITE_WATCHPACKETTEST and INFINITE_WATCHNODETEST: headless proofs of the Apple Watch link
// (docs/plans/apple-watch/README.md, Tests). Both are early exits before glfwInit(). WatchLink is disabled
// here (a terminal-launched process can be killed by TCC on its first Bluetooth call), so the node is driven
// only through WatchLink::InjectPacket.
#include "app/AppShared.h"
#include "core/WatchPacket.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace app
{
   bool RunWatchPacketTest()
   {
      setvbuf(stdout, nullptr, _IONBF, 0);
      bool ok = true;
      auto check = [&](bool c, const char* what) {
         printf("WATCHPACKETTEST %s  %s\n", what, c ? "OK" : "FAIL");
         ok = ok && c;
      };
      using namespace WatchPacket;

      Sample a;
      a.seq = 200; a.timeMs = 0xFEDCBA98u;
      a.flags = kTouching | kPad3 | kRightWrist | kWorkoutRunning;
      const float h = std::sqrt(0.5f);
      a.q[0] = h; a.q[1] = 0; a.q[2] = h; a.q[3] = 0;
      a.rot[0] = -720.f; a.rot[1] = 12.3f; a.rot[2] = 450.f;
      a.accel[0] = -2.5f; a.accel[1] = 0.001f; a.accel[2] = 3.999f;
      a.gravity[0] = 0.25f; a.gravity[1] = -0.5f; a.gravity[2] = -0.8f;
      a.crown = -3.5f; a.touchX = 0.25f; a.touchY = 1.0f;
      a.heart = 143; a.battery = 77;

      uint8_t buf[kSize + 3] = {};
      Encode(a, buf);
      check(buf[0] == 'I' && buf[1] == 'W' && buf[2] == 1 && buf[3] == 200, "header bytes: magic, version 1, seq");
      check(buf[4] == 0x98 && buf[7] == 0xFE, "watch time is little-endian");

      Sample b;
      check(Decode(buf, kSize, b) == Result::kOk, "decodes its own packet");
      auto near = [](float x, float y, float tol) { return std::fabs(x - y) <= tol; };
      bool same = b.seq == a.seq && b.timeMs == a.timeMs && b.flags == a.flags && b.heart == 143 && b.battery == 77;
      for (int i = 0; i < 4; i++) same = same && near(b.q[i], a.q[i], 1e-3f);
      for (int i = 0; i < 3; i++)
         same = same && near(b.rot[i], a.rot[i], 0.06f) && near(b.accel[i], a.accel[i], 0.001f) &&
                near(b.gravity[i], a.gravity[i], 0.001f);
      same = same && near(b.crown, a.crown, 0.001f) && near(b.touchX, a.touchX, 1e-4f) && near(b.touchY, a.touchY, 1e-4f);
      check(same, "round trip keeps every field within its resolution");

      Sample sat;
      sat.rot[0] = 99999.f; sat.accel[0] = -99.f;
      uint8_t sb[kSize];
      Encode(sat, sb);
      Sample sd;
      Decode(sb, kSize, sd);
      check(near(sd.rot[0], 3276.7f, 0.1f) && near(sd.accel[0], -32.768f, 0.01f), "out-of-range values saturate");

      // The rest are refusals; `out` must stay untouched.
      Sample keep = a;
      uint8_t bad[kSize];
      std::memcpy(bad, buf, kSize);
      bad[0] = 'X';
      check(Decode(bad, kSize, keep) == Result::kBadMagic && keep.seq == 200, "bad magic refused");
      check(Decode(buf, kSize - 1, keep) == Result::kShort && keep.seq == 200, "short packet refused");
      check(Decode(buf, 0, keep) == Result::kShort && Decode(nullptr, 44, keep) == Result::kShort, "empty input refused");
      std::memcpy(bad, buf, kSize);
      bad[2] = 2;
      check(Decode(bad, kSize, keep) == Result::kNewerVersion && keep.seq == 200, "newer version refused");
      check(Decode(bad, 3, keep) == Result::kNewerVersion, "newer version is named even when the packet is short");
      check(Decode(buf, kSize + 3, keep) == Result::kOk, "trailing bytes are ignored");

      printf("WATCHPACKETTEST %s\n", ok ? "PASS" : "FAIL");
      return ok;
   }
}

namespace app
{
   bool RunWatchNodeTest() { return false; }   // Step 5
}

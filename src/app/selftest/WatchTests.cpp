// INFINITE_WATCHPACKETTEST and INFINITE_WATCHNODETEST: headless proofs of the Apple Watch link
// (docs/plans/apple-watch/README.md, Tests). Both are early exits before glfwInit(). WatchLink is disabled
// here (a terminal-launched process can be killed by TCC on its first Bluetooth call), so the node is driven
// only through WatchLink::InjectPacket.
#include "app/AppShared.h"
#include "core/WatchPacket.h"
#include "nodes/WatchNode.h"
#include "platform/WatchLink.h"

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
   namespace
   {
      // One watch on a 50 Hz clock, fed to the node only through WatchLink::InjectPacket.
      struct Wrist
      {
         const char* name = "Test Watch";
         uint32_t t = 1000;
         uint8_t seq = 0;
         float tiltDeg = 0;     // roll about x
         float rot[3] = {};
         float accel[3] = {};
         uint16_t flags = 0;

         void Send()
         {
            WatchPacket::Sample s;
            s.seq = seq++;
            s.timeMs = t;
            s.flags = flags;
            const float a = tiltDeg * 0.0174532925f * 0.5f;
            s.q[0] = std::cos(a); s.q[1] = std::sin(a); s.q[2] = 0; s.q[3] = 0;
            for (int i = 0; i < 3; i++) { s.rot[i] = rot[i]; s.accel[i] = accel[i]; }
            uint8_t buf[WatchPacket::kSize];
            WatchPacket::Encode(s, buf);
            WatchLink::InjectPacket(name, buf, sizeof(buf));
            t += 20;
         }
         void Still(int n, WatchNode* node = nullptr)
         {
            for (float& v : rot) v = 0;
            for (float& v : accel) v = 0;
            for (int i = 0; i < n; i++) { Send(); if (node) node->Value(WatchNode::kConnected); }
         }
      };
   }

   bool RunWatchNodeTest()
   {
      setvbuf(stdout, nullptr, _IONBF, 0);
      WatchLink::Disable();   // a terminal-launched process is killed by TCC on its first Bluetooth call
      bool ok = true;
      auto check = [&](bool c, const char* what) {
         printf("WATCHNODETEST %s  %s\n", what, c ? "OK" : "FAIL");
         ok = ok && c;
      };
      auto near = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };
      using N = WatchNode;
      uint32_t clock = 1000;

      auto fresh = [&](Wrist& w, N& n) {
         n.watch = w.name;
         n.smooth = 0.f;   // exact values; smoothing has its own knob
         w.t = clock;
         w.flags = 0;
         w.tiltDeg = 0;
      };

      // 1. Before any packet: disconnected, outputs at their rest values.
      {
         N n;
         n.watch = "Nobody";
         check(n.OutputCount() == 22 && n.Value(N::kConnected) == 0.f, "22 outputs, disconnected before any packet");
         check(near(n.Value(N::kRoll), 0.5f, 1e-4f) && n.Value(N::kShake) == 0.f, "rests at 0.5 with no triggers");
      }

      // 2. Still pose gives 0.5 after Recenter, tilt moves roll about 45 deg per 0.25.
      {
         Wrist w; N n; fresh(w, n);
         w.tiltDeg = 30;
         w.Still(3, &n);
         check(n.Value(N::kConnected) == 1.f, "connected once packets arrive");
         n.Recenter();
         w.Still(5, &n);
         check(near(n.Value(N::kRoll), 0.5f, 0.01f) && near(n.Value(N::kPitch), 0.5f, 0.01f) &&
               near(n.Value(N::kYaw), 0.5f, 0.01f), "still pose reads 0.5 on roll, pitch and yaw after Recenter");
         check(near(n.Value(N::kRotZ), 0.5f, 0.01f) && near(n.Value(N::kAccelX), 0.5f, 0.01f) &&
               near(n.Value(N::kEnergy), 0.f, 0.01f), "still rates 0.5, energy 0");
         w.tiltDeg = 30 + 45;
         w.Still(5, &n);
         check(near(n.Value(N::kRoll), 0.75f, 0.02f), "45 degrees of roll with range 90 reads 0.75");
         check(near(n.Value(N::kPitch), 0.5f, 0.02f), "pitch stays put while rolling");
         w.flags = WatchPacket::kRightWrist;
         w.Still(2, &n);
         check(near(n.Value(N::kRoll), 0.25f, 0.02f), "right wrist mirrors roll");
         clock = w.t + 400;
         WatchLink::InjectDisconnect(w.name);
      }

      // 3. A z-spike fires flick right once, the backswing and a repeat inside the gap do not; later ones do.
      {
         Wrist w; N n; fresh(w, n);
         w.Still(4, &n);
         w.rot[2] = 500.f;
         w.Send();
         check(n.FireCount(N::kFlickRight) == 1 && n.Value(N::kFlickRight) == 1.f, "500 deg/s z-spike fires flick right");
         w.rot[2] = 450.f; w.Send(); n.Value(N::kConnected);
         w.rot[2] = -450.f; w.Send(); n.Value(N::kConnected);   // backswing
         w.rot[2] = 500.f; w.Send(); n.Value(N::kConnected);
         check(n.FireCount(N::kFlickRight) == 1 && n.FireCount(N::kFlickLeft) == 0, "refractory gap holds the same flick, backswing and repeat");
         w.Still(16, &n);   // 320 ms
         w.rot[2] = 500.f; w.Send();
         check(n.FireCount(N::kFlickRight) == 2, "fires again after the gap");
         w.Still(16, &n);
         w.rot[2] = -500.f; w.Send();
         check(n.FireCount(N::kFlickLeft) == 1 && n.FireCount(N::kFlickRight) == 2, "negative z-spike fires flick left");
         w.Still(16, &n);
         w.rot[2] = 300.f; w.Send();
         check(n.FireCount(N::kFlickRight) == 2, "a 300 deg/s turn is below the threshold");
         n.sens = 2.f;
         w.Still(16, &n);
         w.rot[2] = 300.f; w.Send();
         check(n.FireCount(N::kFlickRight) == 3, "sens 2 lowers the threshold");
         check(n.Value(N::kShake) == 0.f && n.FireCount(N::kStrike) == 0, "a flick is not a shake or a strike");
         clock = w.t + 400;
         WatchLink::InjectDisconnect(w.name);
      }

      // 4. A shake pattern fires shake once, and its peaks are not strikes.
      {
         Wrist w; N n; fresh(w, n);
         w.Still(4, &n);
         for (int i = 0; i < 10; i++)
         {
            w.accel[0] = (i % 2 == 0) ? 1.6f : -1.6f;
            w.Send(); n.Value(N::kConnected);
            w.Send(); n.Value(N::kConnected);   // two samples per swing: 40 ms
         }
         check(n.FireCount(N::kShake) == 1, "alternating 1.6 g along x fires shake once");
         check(n.FireCount(N::kStrike) == 0 && n.FireCount(N::kFlickRight) + n.FireCount(N::kFlickLeft) == 0, "no strike or flick from a shake");
         check(n.Value(N::kEnergy) > 0.5f, "energy follows the movement");
         w.Still(60, &n);
         check(n.Value(N::kEnergy) < 0.05f, "energy releases when still");
         clock = w.t + 400;
         WatchLink::InjectDisconnect(w.name);
      }

      // 5. A hard push that stops fires strike; a push that lasts does not; a slow rocking does not shake.
      {
         Wrist w; N n; fresh(w, n);
         w.Still(4, &n);
         w.accel[0] = 3.0f; w.Send(); n.Value(N::kConnected);
         w.accel[0] = 0.2f; w.Send(); n.Value(N::kConnected);
         check(n.FireCount(N::kStrike) == 1, "3 g that falls away within 120 ms fires strike");
         w.Still(16, &n);
         w.accel[0] = 3.0f;
         for (int i = 0; i < 10; i++) { w.Send(); n.Value(N::kConnected); }
         w.accel[0] = 0.f; w.Send(); n.Value(N::kConnected);
         check(n.FireCount(N::kStrike) == 1, "a push held for 200 ms is not a strike");
         w.Still(16, &n);
         for (int i = 0; i < 12; i++) { w.accel[0] = (i % 2 == 0) ? 1.0f : -1.0f; w.Send(); n.Value(N::kConnected); }
         check(n.FireCount(N::kShake) == 0, "1 g rocking stays under the shake threshold");
         clock = w.t + 400;
         WatchLink::InjectDisconnect(w.name);
      }

      // 6. Taps are edges: a held flag makes one tap.
      {
         Wrist w; N n; fresh(w, n);
         w.Still(3, &n);
         w.flags = WatchPacket::kDoubleTap; w.Send(); n.Value(N::kConnected);
         w.Send(); n.Value(N::kConnected);   // still held: no second tap
         w.flags = 0; w.Send(); n.Value(N::kConnected);
         w.flags = WatchPacket::kPad2; w.Send(); n.Value(N::kConnected);
         check(n.FireCount(N::kTap) == 2, "a double tap and a pad each make one tap");
         clock = w.t + 400;
         WatchLink::InjectDisconnect(w.name);
      }

      // 7. Disconnect: connected drops to 0, everything else holds; a newer-version watch is named.
      {
         Wrist w; N n; fresh(w, n);
         w.Still(3, &n);
         n.Recenter();
         w.tiltDeg = 45;
         w.Still(4, &n);
         const float roll = n.Value(N::kRoll);
         check(near(roll, 0.75f, 0.02f) && n.Value(N::kConnected) == 1.f, "connected at 45 degrees");
         WatchLink::InjectDisconnect(w.name);
         check(n.Value(N::kConnected) == 0.f, "connected drops to 0 on disconnect");
         check(n.Value(N::kRoll) == roll && near(n.Value(N::kPitch), 0.5f, 0.02f), "other outputs hold their last value");
         w.tiltDeg = 0;
         w.Still(3, &n);
         check(n.Value(N::kConnected) == 1.f && near(n.Value(N::kRoll), 0.5f, 0.02f), "a reconnect recentres on the new pose");

         uint8_t buf[WatchPacket::kSize] = {};
         WatchPacket::Sample s;
         WatchPacket::Encode(s, buf);
         buf[2] = 9;
         WatchLink::InjectPacket(w.name, buf, sizeof(buf));
         n.Value(N::kConnected);
         check(std::string(n.Status()).find("Update Infinite Remote") != std::string::npos, "a newer packet version says to update the watch app");
         WatchLink::InjectDisconnect(w.name);
      }

      printf("WATCHNODETEST %s\n", ok ? "PASS" : "FAIL");
      return ok;
   }
}

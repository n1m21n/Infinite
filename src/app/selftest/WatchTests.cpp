// INFINITE_WATCHPACKETTEST and INFINITE_WATCHNODETEST: headless proofs of the Apple Watch link
// (docs/plans/apple-watch/README.md, Tests). Both are early exits before glfwInit(). WatchLink is disabled
// here (a terminal-launched process can be killed by TCC on its first Bluetooth call), so the node is driven
// only through WatchLink::InjectPacket.
#include <string>
#include "app/AppShared.h"
#include "core/WatchPacket.h"
#include "nodes/WatchNode.h"
#include "platform/WatchLink.h"
#include "platform/WatchSetup.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

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

      {
         // The watch app's Swift encoder is pinned to this same hex (apps/watch/Sources/GoldenPacket.swift).
         Sample g;
         g.seq = 7; g.timeMs = 123456; g.flags = kTouching | kWorkoutRunning;
         g.q[0] = 0.9f; g.q[1] = 0.1f; g.q[2] = -0.2f; g.q[3] = 0.3f;
         g.rot[0] = 12.3f; g.rot[1] = -45.6f; g.rot[2] = 78.9f;
         g.accel[0] = 0.5f; g.accel[1] = -1.25f; g.accel[2] = 2.0f;
         g.gravity[0] = 0.1f; g.gravity[1] = -0.2f; g.gravity[2] = 0.97f;
         g.crown = 0.123f; g.touchX = 0.25f; g.touchY = 0.75f; g.heart = 72; g.battery = 88;
         uint8_t gb[kSize];
         Encode(g, gb);
         char hex[kSize * 2 + 1];
         for (size_t i = 0; i < kSize; i++)
            snprintf(hex + i * 2, 3, "%02x", gb[i]);
         check(std::string(hex) == "4957010740e2010001013373cd0c66e666267b0038fe1503f4011efbd007640038ffca037b000040ffbf4858",
               "golden packet bytes match the watch app's Swift encoder");
      }

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

   namespace
   {
      // A toolchain that never touches the machine: the probe is set by the test, and "launching the install" only
      // counts (the real one opens a Terminal tab and runs install_watch_app.sh, which writes status.json).
      class FakeTools : public WatchSetup::IToolchain
      {
      public:
         WatchSetup::Probe probe;
         bool launchOk = true;
         int launches = 0, opened = 0;
         WatchSetup::Probe Scan() override { return probe; }
         bool LaunchInstall() override { launches++; return launchOk; }
         void OpenXcodeAccounts() override { opened++; }
         void OpenXcode() override { opened++; }
         void OpenDeviceHelp() override { opened++; }
      };
   }

   bool RunWatchSetupTest()
   {
      using namespace WatchSetup;
      bool ok = true;
      auto check = [&](bool c, const char* what)
      {
         printf("WATCHSETUPTEST %s  %s\n", what, c ? "OK" : "FAIL");
         if (!c) ok = false;
      };

      // parsers, on captured tool output
      const std::string teamsText =
         "{\n    \"me@example.com\" =     (\n                {\n            isFreeProvisioningTeam = 1;\n"
         "            teamID = ABCDE12345;\n            teamName = \"Me (Personal Team)\";\n            teamType = Individual;\n"
         "        },\n                {\n            isFreeProvisioningTeam = 0;\n            teamID = ZZZZZ99999;\n"
         "            teamName = \"Studio\";\n        }\n    );\n}\n";
      const std::vector<Team> teams = ParseTeams(teamsText);
      check(teams.size() == 2 && teams[0].id == "ABCDE12345" && teams[0].personal && teams[1].name == "Studio",
            "teams parsed from `defaults read`");
      check(PickTeam(teams) && PickTeam(teams)->id == "ABCDE12345", "the free Personal Team is picked");
      check(PickTeam({}) == nullptr && ParseTeams("The domain/default pair does not exist").empty(), "no account gives no team");
      const std::string devJson =
         R"({"result":{"devices":[)"
         R"({"identifier":"AAA","deviceProperties":{"name":"My iPhone","developerModeStatus":"enabled"},"hardwareProperties":{"deviceType":"iPhone"}},)"
         R"({"identifier":"BBB","deviceProperties":{"name":"My Watch","developerModeStatus":"disabled"},"hardwareProperties":{"deviceType":"appleWatch"}}]}})";
      const std::vector<Device> devs = ParseDevices(devJson);
      check(devs.size() == 2 && !devs[0].isWatch && devs[0].developerMode && devs[1].isWatch && !devs[1].developerMode,
            "devices parsed from devicectl json");
      check(ParseDevices("not json").empty(), "bad json gives no devices");

      const std::string stateFile = (std::filesystem::temp_directory_path() / "infinite-watchsetup-status.json").string();
      std::remove(stateFile.c_str());
      // what install_watch_app.sh writes
      auto writeStatus = [&](const char* st, int step, const char* msg, long long installedAt, long long updatedAt)
      {
         std::ofstream out(stateFile, std::ios::trunc);
         out << "{\"step\":" << step << ",\"total\":5,\"state\":\"" << st << "\",\"message\":\"" << msg << "\",\"installedAt\":"
             << installedAt << ",\"updatedAt\":" << updatedAt << "}\n";
      };
      time_t clock = 1000000;
      FakeTools* tools = new FakeTools();
      Checklist list(std::unique_ptr<IToolchain>(tools), stateFile, [&] { return clock; });
      auto state = [&](int i) { return list.Steps()[i].state; };

      check(state(0) == StepState::kPending && list.Steps()[0].text == "Checking...", "pending before the first scan");
      check(list.ExpiryText().empty() && list.DaysLeft() == -1, "no expiry before an install");

      tools->probe.supported = false;
      list.RefreshNow();
      check(state(0) == StepState::kMissing && !list.AllReady(), "unsupported system: nothing is ready");
      tools->probe.supported = true;

      list.RefreshNow();
      check(state(0) == StepState::kMissing && list.Steps()[0].canOpen, "no Xcode: step 1 open button");
      list.OpenStep(StepId::kXcode);
      check(tools->opened == 1, "the Open button reaches the toolchain");

      tools->probe.xcodeInstalled = true;
      tools->probe.xcodeVersion = "16.2";
      list.RefreshNow();
      check(state(0) == StepState::kOk && state(1) == StepState::kMissing && list.Steps()[1].canOpen, "Xcode but no Apple ID");

      tools->probe.teams = teams;
      list.RefreshNow();
      check(state(1) == StepState::kOk && list.ChosenTeam().id == "ABCDE12345" && state(2) == StepState::kMissing,
            "Apple ID found, team chosen, no devices yet");

      tools->probe.devices = { devs[0] };
      list.RefreshNow();
      check(state(2) == StepState::kMissing && list.Steps()[2].text.find("watch") != std::string::npos, "iPhone but no watch");
      tools->probe.devices = { devs[0], devs[1] };
      list.RefreshNow();
      check(state(2) == StepState::kMissing && list.Steps()[2].text.find("Developer Mode") != std::string::npos &&
               !list.AllReady(), "watch without Developer Mode");
      list.StartInstall();
      check(tools->launches == 0, "install is refused until the first three steps are green");

      tools->probe.devices[1].developerMode = true;
      list.RefreshNow();
      check(list.AllReady() && state(3) == StepState::kPending && list.ChosenWatchId() == "BBB", "all green: install is offered");

      // pressing the button launches the terminal once and writes "running" before the script has said anything
      list.StartInstall();
      check(tools->launches == 1 && list.Install().phase == InstallPhase::kRunning && state(3) == StepState::kPending,
            "Install launches the terminal tab and shows running");
      list.StartInstall();
      check(tools->launches == 1, "a second press while running does nothing");
      writeStatus("running", 4, "building for your watch", 0, clock);
      list.PollStatus();
      check(list.Install().step == 4 && list.Install().total == 5 && list.Install().progress == "building for your watch",
            "the card reads step and message from status.json");

      // a failure: the reason and the fix, no install date
      writeStatus("failed", 4, "The build failed: No profiles for the bundle id. To fix: sign in under Xcode > Settings > Accounts.", 0, clock);
      list.PollStatus();
      check(list.Install().phase == InstallPhase::kFailed && list.Install().error.find("No profiles") != std::string::npos &&
               list.DaysLeft() == -1 && state(3) == StepState::kMissing, "failure shows the reason and saves no date");

      // the terminal could not open
      tools->launchOk = false;
      list.StartInstall();
      check(list.Install().phase == InstallPhase::kFailed && list.Install().error == "Could not open the Terminal.", "launch failure is reported");
      tools->launchOk = true;

      // success: the date comes from installedAt in the file
      writeStatus("ok", 5, "Installed. Open Infinite Remote on the watch.", clock, clock);
      list.PollStatus();
      check(list.Install().phase == InstallPhase::kDone && state(3) == StepState::kOk && list.DaysLeft() == 7 &&
               list.ExpiryText() == "watch app expires in 7 days" && !list.ReinstallDue(), "installed: expires in 7 days");

      clock += 5 * 86400;
      check(list.DaysLeft() == 2 && list.ReinstallDue() && list.ExpiryText() == "watch app expires in 2 days", "5 days later: reinstall is offered");
      clock += 86400 + 100;
      check(list.DaysLeft() == 1 && list.ExpiryText() == "watch app expires in 1 day", "singular day");
      clock += 3 * 86400;
      check(list.DaysLeft() == 0 && list.ExpiryText() == "watch app has expired" && list.ReinstallDue(), "expired");

      // a reinstall keeps counting from the old install until the new one is in
      const int launchesBefore = tools->launches;
      list.StartInstall();
      check(tools->launches == launchesBefore + 1 && list.Install().phase == InstallPhase::kRunning && list.DaysLeft() == 0,
            "Reinstall keeps the old install date while running");
      writeStatus("failed", 2, "Your watch is not visible to this Mac.", 1000000, clock);
      list.PollStatus();
      check(list.DaysLeft() == 0 && list.Install().phase == InstallPhase::kFailed, "a failed reinstall keeps the old date");

      // a terminal closed mid-install must not leave the button stuck
      writeStatus("running", 4, "building for your watch", 1000000, clock);
      clock += 30 * 60;
      list.PollStatus();
      check(list.Install().phase == InstallPhase::kFailed && list.Install().error.find("did not finish") != std::string::npos,
            "a stale running status becomes a failure");

      {
         // the date survives a restart: it is in the file
         writeStatus("ok", 5, "Installed.", 1000000, 1000000);
         Checklist again(std::unique_ptr<IToolchain>(new FakeTools()), stateFile, [&] { return (time_t)1000000 + 6 * 86400 + 100; });
         check(again.DaysLeft() == 1 && again.Install().phase == InstallPhase::kDone, "install date is kept in status.json");
      }
      check(ParseStatusFile("garbage").present == false && ParseStatusFile("{\"state\":\"weird\"}").present == false, "bad status files are ignored");

      tools->probe.projectBundled = false;
      list.RefreshNow();
      check(list.Steps()[3].state == StepState::kMissing, "a build without the project says so");

      std::remove(stateFile.c_str());
      printf("WATCHSETUPTEST %s\n", ok ? "PASS" : "FAIL");
      return ok;
   }
}

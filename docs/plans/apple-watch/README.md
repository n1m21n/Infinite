# Apple Watch: your wrist as a modulator

Status: plan, 2026-11-02. Not built. New node, so the owner approves this plan before Block A starts.
Tracker: this file (top table). Branches: `feature/watch-a`, `feature/watch-b`, `feature/watch-c`, one per block, a commit per step.
Skills to load before building: `node-ui-pillars`, `windows-parity`, `linux-parity` (Block C), `tool-first`.

| Block | What | State |
|---|---|---|
| A | Infinite side: Bluetooth link, packet decoder, `Apple Watch` node, gestures, help, selftest | built on `feature/watch-a`, WATCHPACKETTEST + WATCHNODETEST pass; not merged. For Block B: the Mac also exposes a 4th GATT characteristic `name` (9A1F0004, write: the watch sends its display name), and the watch must subscribe to `control` (9A1F0003) or the Mac treats it as gone |
| B | watchOS app "Infinite Remote": sensors, crown, touch pad, double tap, Bluetooth sender | written on `feature/watch-b` (`apps/watch/`, build-it-yourself, no store) plus the in-app "Set up watch app" window (D-B1 to D-B5). WATCHPACKETTEST (golden bytes), WATCHNODETEST and WATCHSETUPTEST pass. NOT compiled for watchOS and install.sh never run against real Xcode (no Xcode on the build Mac); real-watch test pending |
| C | Haptics back to the wrist, Windows + Linux link, TestFlight + App Store | not started |

## 1. The idea

```
  Apple Watch (any user)                    Mac running Infinite
  +----------------------------+            +-------------------------------------+
  | Infinite Remote (watchOS)  |  Bluetooth | WatchLink (CoreBluetooth peripheral)|
  |  CoreMotion  100 Hz        |  LE, GATT  |   decode packet  -> latest sample   |
  |  Digital Crown, touch pad  | ---------> |   gesture detectors (flick, shake)  |
  |  double tap, pads          |  ~50 Hz    |              |                      |
  |  heart rate (HealthKit)    |  44 bytes  |   Apple Watch node: 22 outputs      |
  |  workout session = stays   | <--------- |     -> any slider's mod pin         |
  |  alive with screen down    |  haptic    |                                     |
  +----------------------------+  (Block C) +-------------------------------------+
```

The watch talks straight to the Mac. No iPhone relay, no Wi-Fi, no OSC setup.

## 2. What the watch can give us (research)

Only what a third-party app can read **live**. Blood oxygen, ECG, skin temperature, noise level and the AssistiveTouch gestures (clench, pinch) are not readable live, so they are out.

| Source | API | Rate | Model | Use in Infinite |
|---|---|---|---|---|
| Attitude (roll, pitch, yaw) | `CMMotionManager.deviceMotion` | up to 100 Hz | all | aim, steer, tilt-to-filter |
| Gravity vector | same | 100 Hz | all | drift-free tilt |
| Rotation rate (gyro) | same | 100 Hz | all | speed of turn, flick detect |
| User acceleration | same | 100 Hz | all | energy, strike, shake |
| High-rate batches (800 Hz accel, 200 Hz motion) | `CMBatchedSensorManager` | batched, laggy | S9+/Ultra 2, workout only | not used: arrives in batches, too late for live play |
| Digital Crown | SwiftUI `.digitalCrownRotation` | per frame | all | a big, precise knob |
| Touch screen | SwiftUI `DragGesture` | per frame | all | XY pad |
| Double tap (finger pinch) | `.handGestureShortcut(.primaryAction)` | event | S9+, Ultra 2, watchOS 11+ | hands-free trigger |
| On-screen pads | SwiftUI buttons | event | all | 4 triggers |
| Heart rate | `HKWorkoutSession` + `HKLiveWorkoutBuilder` | every ~1-5 s | all | slow mood modulator |
| Wrist side, crown side | `WKInterfaceDevice.wristLocation/crownOrientation` | once | all | mirror axes so left/right wrist feel the same |
| Haptic | `WKInterfaceDevice.play(_:)` | event | all | Block C: beat or trigger felt on the wrist |

Sources: [Apple CMBatchedSensorManager](https://developer.apple.com/documentation/coremotion/cmbatchedsensormanager), [WWDC23 Core Motion notes](https://wwdcnotes.com/notes/wwdc23/10179), [Enabling double tap](https://developer.apple.com/documentation/watchos-apps/enabling-double-tap), [TN3135 low-level networking on watchOS](https://developer.apple.com/documentation/technotes/tn3135-low-level-networking-on-watchos), [WatchConnectivity real-time thread](https://developer.apple.com/forums/thread/788376).

## 3. How the data gets to the Mac (the hard part)

| Route | Latency | Why not / why |
|---|---|---|
| Watch -> iPhone (WatchConnectivity) -> Wi-Fi OSC -> Infinite | 100 ms to seconds, bursts | Apple says WatchConnectivity is not for real-time data. Needs the iPhone app open too. **Rejected** |
| Watch -> Wi-Fi socket / WebSocket -> Infinite | ~20 ms | watchOS forbids sockets and `URLSessionWebSocketTask` except for audio-streaming or CallKit apps (TN3135). Faking an audio session is an App Review risk. **Rejected** |
| **Watch (Bluetooth central) -> Mac (Bluetooth peripheral)** | 15-40 ms | Core Bluetooth central works on watchOS; macOS can be a peripheral (`CBPeripheralManager`). No phone, no network, no setup. **Chosen** |

Latency budget: sensor 10 ms + Bluetooth connection interval 15-30 ms + Infinite frame 16 ms = **~40-60 ms**. Fine for visuals and for modulating audio params; not tight enough to play drum hits "on the beat" from a strike (say so in help).

## 4. Decisions (made; owner can override)

| # | Question | Decision | Why |
|---|---|---|---|
| D1 | Transport | Bluetooth LE GATT, Mac = peripheral, watch = central | Section 3 |
| D2 | One node or several | **One `Apple Watch` node (Modulators)**, 22 outputs, like Hand Track | Users think "my watch", one picker. Hand Track already proves the multi-output card |
| D3 | Where gestures are detected | **On the Mac**, from the raw stream | Tuning ships with Infinite releases, no App Store review per tweak, and old watch apps keep working |
| D4 | Keep alive with wrist down | `HKWorkoutSession` (activity "other"), started by a Play button on the watch | The standard way a watch app keeps sensors and Bluetooth running with the screen off. Also gives heart rate for free |
| D5 | Several watches | Up to 4 connected at once; node `watch` dropdown picks by name ("Naman's Apple Watch"), saved by name | Dancers / band members each wear one. Peripheral accepts several centrals for free |
| D6 | Pairing | None. Mac advertises "Infinite on <Mac name>"; the watch lists nearby Macs, tap to connect, remembers the last one | No codes. Bluetooth range keeps it local |
| D7 | Rate | 50 Hz default, 100 Hz option on the watch | 50 Hz is smooth at 60 fps with Smoothing; halves battery cost |
| D8 | Values | Every output 0..1 (bipolar ones centred on 0.5), Recenter button sets current pose = 0.5 | Matches every other modulator |
| D9 | Platforms | macOS in Block A/B. Windows (WinRT `GattServiceProvider`) and Linux (BlueZ GATT server over D-Bus) in Block C. Stubs report "macOS only for now" | Most watch owners are on a Mac; ship the core first |
| D10 | Haptics back | Block C, one modulator input pin `haptic`: a rise above 0.5 taps the wrist | Cheap via a notify characteristic; makes it an instrument, not just a sensor |
| D11 | Name | Node `Apple Watch`, app `Infinite Remote` | Descriptive use of the product name is allowed; the app name avoids it |
| D12 | Out of scope | Raw 800 Hz batches, blood oxygen, ECG, altimeter, iPhone motion | Not live, or not useful at 60 fps |

## 5. The node: `Apple Watch` (Modulators)

```
 +--------------------------------------------+
 | Apple Watch                     [connected]|
 | watch   [Naman's Apple Watch]              |
 | smooth  ------o------    sens  ----o---    |
 | [ Recenter ]   wrist: left, crown right    |
 |  roll ......o  pitch ...o..  yaw ..o....   |   <- small live meters
 +--------------------------------------------+
   outputs (22):
   connected  roll  pitch  yaw
   rot x  rot y  rot z            (gyro speed, signed, 0.5 = still)
   accel x  accel y  accel z      (user accel, signed)
   energy                         (how hard you move, 0..1)
   crown  crown speed
   touch x  touch y  touching
   tap                            (double tap or any pad, 1 for 80 ms)
   flick left  flick right  shake  strike   (triggers, 1 for 80 ms)
   heart                          (40..200 bpm -> 0..1)
```

Params (saved): `watch` (name string), `smooth` (0..1, one-euro filter like Track nodes), `sens` (gesture threshold scale 0.5..2), `range` (degrees for full swing, default 90). Recenter is a button, not saved.

### The math

Orientation relative to the recentred pose (quaternions, no gimbal lock):

```
q_rel = conj(q_ref) * q_now
roll, pitch, yaw = euler(q_rel)              // degrees
out   = clamp(0.5 + angle / (2 * range), 0, 1)
mirror roll + yaw when wristLocation == right   (D11 row "wrist side")
```

Drift: yaw drifts slowly without a magnetometer reference; roll and pitch are gravity-locked and do not. Help says "Recenter if yaw wanders".

Signed rates: `rot = 0.5 + clamp(w / 720 deg/s, -1, 1) * 0.5`; accel the same with 4 g full scale.

Energy (smoothed movement):

```
e_raw  = |userAccel| (g)
energy = envelope(e_raw), attack 20 ms, release 300 ms, mapped 0..1.5 g -> 0..1
```

Gestures (each with a 250 ms refractory gap, thresholds x `sens`):

| Gesture | Rule |
|---|---|
| flick left / right | `rotRate.z` peak beyond +-400 deg/s, sign picks the side (mirrored on right wrist) |
| shake | 4+ sign changes of `userAccel.x` with `|a| > 1.2 g` inside 600 ms |
| strike | `userAccel` along the forearm (x) > 2.5 g, then falls below 1 g within 120 ms |
| tap | double tap or a pad edge from the watch flags |

Crown: the watch sends accumulated rotation; node output wraps nothing, it clamps 0..1 (one full crown turn = 0.25). Crown speed = signed, 0.5 = still.

## 6. The wire protocol (v1)

GATT service `Infinite Remote` (fixed 128-bit UUID in `WatchLink.h`), two characteristics:

| Characteristic | Direction | Property |
|---|---|---|
| `sample` | watch -> Mac | write without response |
| `control` | Mac -> watch | notify (Block C: haptic, rate request) |

`sample` packet, little-endian, 44 bytes (fits the smallest watch write length of 182):

| Bytes | Field | Encoding |
|---|---|---|
| 0-1 | magic `'I','W'` | |
| 2 | version | `1` |
| 3 | seq | u8, wraps; Mac counts drops |
| 4-7 | watch time | u32 ms |
| 8-9 | flags | bit0 touching, bit1 double tap edge, bit2-5 pad 1-4 edges, bit6 right wrist, bit7 crown left, bit8 workout running |
| 10-17 | attitude quaternion w x y z | 4 x int16 Q15 |
| 18-23 | rotation rate x y z | 3 x int16, 1 unit = 0.1 deg/s |
| 24-29 | user accel x y z | 3 x int16, 1 unit = 1 mg |
| 30-35 | gravity x y z | 3 x int16, 1 unit = 1 mg |
| 36-37 | crown | int16, 1 unit = 1/1000 turn, accumulated |
| 38-41 | touch x, y | 2 x u16, 0..65535 |
| 42 | heart | u8 bpm, 0 = unknown |
| 43 | battery | u8 percent |

Unknown version = node shows "Update Infinite Remote on the watch". Extra trailing bytes are ignored, so v2 can append fields.

## 7. Infinite side: files and threads (Block A)

```
 CoreBluetooth queue (own dispatch queue)        main thread, once per frame
 +----------------------------------+            +-------------------------------+
 | didReceiveWrite -> decode packet |  lock-free | WatchLink::Read(name, sample) |
 | -> per-watch slot, seqlock write | ---------> | Apple Watch node: euler, map, |
 +----------------------------------+  latest    | gestures, smoothing, taps     |
                                     sample      +-------------------------------+
```

| File | What |
|---|---|
| `src/platform/WatchLink.h` | API in the style of `HeadTracker.h`: ref-counted `Start/Stop`, `Disable()` for headless/selftest, `ListWatches()`, `Read(name, Sample&)`, `Status()`, `InjectPacket()` for tests |
| `src/platform/WatchLink.mm` | macOS: `CBPeripheralManager`, advertise service, accept writes from up to 4 centrals, decode into per-watch seqlocked slots |
| `src/platform/WatchLinkStub.cpp` | Windows/Linux until Block C: `Supported()` false, status "macOS only for now" |
| `src/core/WatchPacket.h/.cpp` | Pure decode + encode (encode is for tests and the watch's reference), no OS code |
| `src/nodes/WatchNode.h/.cpp` | The node. Outputs via the `Tap : IModulator` pattern of `TrackNodeBase.h:100` (copy it, or lift it to a shared header if it stays small). Gesture detectors live here |
| `src/app/bodies/...` | Card body: watch dropdown (no chevron), Recenter, three mini meters, status line |
| `cmake/Info.plist.in` | add `NSBluetoothAlwaysUsageDescription` ("Infinite listens for your Apple Watch over Bluetooth.") |
| `CMakeLists.txt` | link `CoreBluetooth` on macOS; stub on other OSes |

Registration and help (the four tables, both directions; see memory "node help coverage"):
`src/app/graph/NodeRegistry.cpp` (Modulators), `src/core/AISkillContent.h` node row, `src/app/panels/HelpWindows.cpp` both lists with a `#if __APPLE__` branch, `src/app/selftest/ModAudit.cpp` `Reg<WatchNode>`.

Headless rule: like `HeadTracker::Disable()`, a terminal-launched process can be killed by TCC on the first Bluetooth call; selftests call `WatchLink::Disable()` and drive the node only through `InjectPacket`.

### Tests (sized to the bug)

| Test | Proves |
|---|---|
| `WATCHPACKETTEST` | encode -> decode round trip, bad magic / short packet / new version handled |
| `WATCHNODETEST` | injected still pose -> all bipolar outputs 0.5 after Recenter; injected 500 deg/s z spike -> `flick right` fires once, then refractory; shake pattern fires `shake`; disconnected -> `connected` 0 and outputs hold |
| Manual (owner) | real watch: connect, roll drives a slider, flick fires, wrist down keeps streaming |

## 8. watchOS app "Infinite Remote" (Block B)

Lives in `apps/watch/` with an XcodeGen `project.yml` (text, reviewable; the `.xcodeproj` is generated, not committed). Independent watch app (no iPhone app needed), watchOS 10+, double tap needs watchOS 11.

```
 Screen 1: Connect          Screen 2: Play (TabView page)      Screen 3: Pads
 +----------------+         +--------------------+             +------------+
 | Nearby Macs    |         |  [ XY touch pad  ] |             | [1]   [2]  |
 |  > Studio Mac  |  tap -> |  crown = knob ring |  swipe ->   | [3]   [4]  |
 |    Air         |         |  [ Play / Stop ]   |             |  dbl tap=1 |
 +----------------+         +--------------------+             +------------+
```

| Swift file | What |
|---|---|
| `RemoteApp.swift` | App, `TabView` pages, keeps last Mac |
| `MotionSource.swift` | `CMMotionManager.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: queue)` at 50/100 Hz |
| `Keeper.swift` | default: `WKExtendedRuntimeSession` (no entitlement). With `INFINITE_WATCH_HEALTH=1`: `HKWorkoutSession` + `HKLiveWorkoutBuilder`, heart rate from `statistics(for: .heartRate)`; HealthKit entitlement + `NSHealthShareUsageDescription`, `NSMotionUsageDescription` |
| `BLELink.swift` | `CBCentralManager`, scan for the service UUID, connect, `writeValue(.withoutResponse)` gated on `canSendWriteWithoutResponse`, reconnect on drop |
| `Packet.swift` | Same layout as section 6; a unit test asserts byte equality with a golden packet produced by `WatchPacket.cpp` encode |
| `PlayView.swift` | `DragGesture(minimumDistance: 0)` XY pad, `.digitalCrownRotation`, Play/Stop button with `.handGestureShortcut(.primaryAction)` while not playing, pad button during play |

Simulator note: the watch simulator has no Bluetooth. Block B checks UI and packet bytes in the simulator; the link itself is tested on the owner's watch.

Distribution (decided 2026-11-02): the owner has no paid Apple Developer account, so there is no TestFlight and no App Store. The app lives in the repo and each user builds it in Xcode with their own free Apple ID (a "Personal Team") and installs it on their own watch. **macOS users only**, because building needs a Mac with Xcode. Free signing expires after 7 days, so the user re-runs the build weekly. Free teams need a unique bundle id per person, so the id prefix is a user-set value in `apps/watch/Config/Local.xcconfig` (with `DEVELOPMENT_TEAM`), never hard-coded.

Build-it-yourself flow (details in `apps/watch/README.md`):

```
 install Xcode -> sign in with Apple ID -> Developer Mode (iPhone, watch)
   -> set team + bundle prefix in Local.xcconfig -> scripts/build_watch_app.sh
   -> trust the developer on the watch -> Apple Watch node + Play on the watch
   -> repeat the build every 7 days
```

Keep-alive without HealthKit: free personal teams may not get the HealthKit entitlement. Default build uses `WKExtendedRuntimeSession` (physical-therapy session type, `WKBackgroundModes` in Info.plist, no entitlement), heart byte = 0. Building with `INFINITE_WATCH_HEALTH=1` switches to `HKWorkoutSession` + heart rate (`Keeper.swift`) for users whose team allows HealthKit. The Mac side is identical in both cases.

### One-click setup from inside Infinite (decided 2026-11-02, D-B1 to D-B5)

| Id | Decision |
|---|---|
| D-B1 | The source ships inside `Infinite.app/Contents/Resources/WatchRemote/`. The `.xcodeproj` is generated at Infinite build time with xcodegen (release builds need xcodegen; a dev build without it ships sources only and the checklist says the project is missing). Users need neither a repo clone nor xcodegen. Mac builds only. |
| D-B2 | The Apple Watch node card has a "Set up watch app" button that opens a checklist window. Each step checks itself every few seconds and turns green alone: (1) Xcode installed (`xcode-select -p`, `xcodebuild -version`); (2) Apple ID in Xcode (`defaults read com.apple.dt.Xcode IDEProvisioningTeams`, the free Personal Team is picked automatically); (3) iPhone and watch visible with Developer Mode on (`xcrun devicectl list devices`); (4) "Install on my watch". An unmet step shows one sentence and an Open button where it helps. The website page `website/watch.html` is the same guide with screenshots, generated from `apps/watch/README.md` (the single source) by `tools/gen-watch-page.py`. |
| D-B3 | "Install on my watch" runs `apps/watch/install.sh` (the same script `scripts/build_watch_app.sh` uses) on a background thread: copy the bundled project to `~/Library/Application Support/Infinite/WatchRemote/`, set `DEVELOPMENT_TEAM` and a bundle id `org.infinitedevices.remote.<8 hex of a hash of the team id>`, `xcodebuild -allowProvisioningUpdates` for the watch, then `xcrun devicectl device install app`. The card shows a progress line and the last error line. On any failure it opens the project in Xcode with the team filled in and says to press Run. |
| D-B4 | The install date is saved (`watch_install.txt` in the app support folder). The card shows "watch app expires in N days"; from 2 days before it offers a one-click "Reinstall" (same path as D-B3). |
| D-B5 | Selftests never run xcodebuild or devicectl. The checks and the install sit behind `WatchSetup::IToolchain`; `WATCHSETUPTEST` fakes it and walks the checklist through every state. |

## 9. Block C

1. `control` notify + node input pin `haptic` -> `WKInterfaceDevice.current().play(.click)` on rising edge.
2. Windows link: WinRT `GattServiceProvider` (Windows 10 1803+, adapter must support peripheral role; status says so when it does not).
3. Linux link: BlueZ `GattManager1.RegisterApplication` over D-Bus (sd-bus or GDBus).
4. Website + manual page, one tutorial film only if the owner asks. (TestFlight / App Store are dropped: no paid developer account, see section 8.)

## 10. Risks

| Risk | Plan |
|---|---|
| Free team has no HealthKit entitlement | Default build uses `WKExtendedRuntimeSession` (physical therapy, no entitlement); `INFINITE_WATCH_HEALTH=1` opts into the workout session |
| Free signing expires after 7 days; unique bundle id needed per person | README says re-run weekly; bundle prefix lives in `Local.xcconfig` |
| Users without a Mac cannot build the app | Accepted: macOS users only |
| Mac Bluetooth busy (AirPods, keyboard) raises latency | Measure on the owner's Mac in Block B; drop to 30 Hz automatically if writes back up |
| Yaw drift | Recenter button, and help points users to roll/pitch for long holds |
| macOS kills headless runs on Bluetooth | `WatchLink::Disable()` in every selftest path, like HeadTracker |

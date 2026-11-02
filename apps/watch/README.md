# Infinite Remote (Apple Watch)

Sends your wrist motion, Digital Crown, touch pad and taps from an Apple Watch to the **Apple Watch** node in Infinite, over Bluetooth. No iPhone app, no pairing code.

There is no App Store version. You build the app yourself with a free Apple ID and install it on your own watch. **This needs a Mac with Xcode**, so it is for macOS users only.

## Install

Do these once. Each step in Infinite's **Set up watch app** window turns green by itself when it is done.

1. Install **Xcode** from the App Store and open it once.
2. In Xcode: **Settings > Accounts**, add your Apple ID. A "Personal Team" appears.
3. Turn on Developer Mode on the iPhone paired with the watch (**Settings > Privacy & Security > Developer Mode**, then restart), and on the watch (same place). Connect the iPhone to the Mac once with a cable and tap Trust. Keep the watch unlocked.
4. In Infinite, add the **Apple Watch** node (Modulators) and press **Set up watch app**. When the first three steps are green, press **Install on my watch**. It builds the app with your team and installs it; the first build takes a few minutes.
5. On the watch: **Settings > General > VPN & Device Management**, tap your Apple ID and **Trust**. Open Infinite Remote.

No Infinite yet, or you work from a clone of the repository? Run `scripts/build_watch_app.sh` (needs `brew install xcodegen`). The first run creates `apps/watch/Config/Local.xcconfig`: put your team id in `DEVELOPMENT_TEAM` and a prefix that is unique to you (like `com.yourname`) in `INFINITE_BUNDLE_PREFIX`, then run it again and press Run in Xcode. `scripts/build_watch_app.sh --install DEVICE-ID` builds and installs from the terminal (`xcrun devicectl list devices` shows the id).

## Use

1. In Infinite, the **Apple Watch** node allows Bluetooth when asked.
2. On the watch open **Infinite Remote**. Your Mac shows up under *Connect*: tap it. It reconnects by itself next time.
3. Swipe to the Play page and press **Play**. On watchOS 11 a double tap presses Play, and while playing it presses **Tap**.
4. Press **Recenter** on the node whenever yaw wanders.

## Every 7 days

A free Apple ID signs the app for 7 days. The Apple Watch node shows "watch app expires in N days" and, from 2 days before, a **Reinstall** button: one click does the same as Install on my watch. From a clone, run `scripts/build_watch_app.sh` again.

## Two keep-alive modes

| Build | How the app stays awake with the wrist down | Heart rate | Needs |
|---|---|---|---|
| Default (`INFINITE_WATCH_HEALTH = 0`) | extended runtime session (physical therapy type) | not sent | nothing, works on free teams; it runs about an hour per start |
| Health (`INFINITE_WATCH_HEALTH = 1` in `Local.xcconfig`, or `INFINITE_WATCH_HEALTH=1 scripts/build_watch_app.sh`) | workout session | sent | a team that allows the HealthKit capability (Xcode shows an error at signing if yours does not) |

## Troubleshooting

| Problem | What to do |
|---|---|
| The Mac is not listed | Infinite must be running with an Apple Watch node in the patch. Bluetooth on in both. Wait a few seconds, then reopen the app. |
| "Allow Bluetooth in Settings" on the watch | Watch Settings > Privacy & Security > Bluetooth: allow Infinite Remote. |
| The Mac asks for Bluetooth | System Settings > Privacy & Security > Bluetooth: allow Infinite, then restart Infinite. |
| Xcode: "Failed to register bundle identifier" | The prefix is already taken. Choose another `INFINITE_BUNDLE_PREFIX`. |
| Install on my watch fails | Xcode opens with your team filled in: pick your watch next to Run and press Run. The last error line is also shown in Infinite. |
| Xcode: "No account" or no team | Add your Apple ID in Xcode > Settings > Accounts and copy the team id into `DEVELOPMENT_TEAM`. |
| Xcode: HealthKit capability not available | Use the default build (`INFINITE_WATCH_HEALTH = 0`). |
| "Untrusted developer" or the app will not open | Trust your Apple ID on the watch (step 6). After 7 days, run the build again. |
| The watch is not in Xcode's device list | Both devices unlocked, Developer Mode on, iPhone connected to the Mac once with a cable. |
| Node says "Update Infinite Remote on the watch" | Build the app again from the current Infinite folder. |

## Tests

`apps/watch/Tests/run_golden.sh` checks that the watch's packet encoder gives the same bytes as the Mac's `WatchPacket.cpp` (the Mac side asserts the same bytes in `INFINITE_WATCHPACKETTEST=1`). It needs only the Swift compiler. The Xcode test target `InfiniteRemoteTests` runs the same check.

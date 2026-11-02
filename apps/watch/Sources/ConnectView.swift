import SwiftUI

/// Page 1: nearby Macs running Infinite. The Mac used last time reconnects by itself.
struct ConnectView: View {
    @EnvironmentObject var remote: Remote
    @EnvironmentObject var link: BLELink

    var body: some View {
        List {
            Section(status) {
                if case .connected = link.state {
                    Button("Disconnect") { remote.stop(); link.disconnect() }
                }
                ForEach(link.macs) { mac in
                    Button(mac.name) { link.connect(mac) }
                }
                if link.macs.isEmpty, link.state == .scanning {
                    Text("Open Infinite on your Mac and add an Apple Watch node.").font(.footnote).foregroundStyle(.secondary)
                }
            }
            if !remote.sensorsAvailable {
                Text("This watch has no motion sensors available.").font(.footnote)
            }
        }
        .navigationTitle("Connect")
    }

    private var status: String {
        switch link.state {
        case .off: return "Bluetooth is off"
        case .unauthorized: return "Allow Bluetooth in Settings"
        case .scanning: return "Looking for Macs"
        case .connecting(let n): return "Connecting to \(n)"
        case .connected(let n): return "Connected to \(n)"
        }
    }
}

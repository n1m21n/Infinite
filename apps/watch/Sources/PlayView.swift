import SwiftUI

/// Page 2: XY touch pad, Digital Crown, Play / Stop.
struct PlayView: View {
    @EnvironmentObject var remote: Remote
    @EnvironmentObject var link: BLELink
    @State private var crown = 0.0
    @State private var dot: CGPoint?

    var body: some View {
        VStack(spacing: 6) {
            GeometryReader { geo in
                ZStack {
                    RoundedRectangle(cornerRadius: 12).fill(Color.white.opacity(0.12))
                    if let d = dot {
                        Circle().fill(Color.orange).frame(width: 18, height: 18).position(d)
                    } else {
                        Text(remote.playing ? "touch" : "").font(.caption2).foregroundStyle(.secondary)
                    }
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        let x = min(max(v.location.x, 0), geo.size.width)
                        let y = min(max(v.location.y, 0), geo.size.height)
                        dot = CGPoint(x: x, y: y)
                        // y is flipped: the top of the pad is 1, so "up" reads as more.
                        remote.setTouch(x: Float(x / geo.size.width), y: Float(1 - y / geo.size.height), down: true)
                    }
                    .onEnded { _ in
                        dot = nil
                        remote.endTouch()
                    })
            }
            controls
        }
        .focusable()
        .digitalCrownRotation($crown, from: -30, through: 30, sensitivity: .medium, isContinuous: false, isHapticFeedbackEnabled: true)
        .onChange(of: crown) { _, v in remote.setCrown(v) }
        .navigationTitle(link.isConnected ? "Play" : "Not connected")
    }

    @ViewBuilder private var controls: some View {
        if remote.playing {
            HStack {
                tapButton
                Button("Stop") { remote.stop() }.tint(.red)
            }
        } else {
            playButton
        }
    }

    // The double-tap gesture (watchOS 11) presses whichever button carries the primary action.
    @ViewBuilder private var playButton: some View {
        let b = Button("Play") { remote.play() }.tint(.green).disabled(!link.isConnected)
        if #available(watchOS 11.0, *) { b.handGestureShortcut(.primaryAction) } else { b }
    }

    @ViewBuilder private var tapButton: some View {
        let b = Button("Tap") { remote.press(pad: nil) }.tint(.orange)
        if #available(watchOS 11.0, *) { b.handGestureShortcut(.primaryAction) } else { b }
    }
}

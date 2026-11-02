import Foundation
import SwiftUI
import WatchKit

/// Everything the pages share: the latest touch, crown and button state, and the loop that turns each motion
/// frame into a packet. Touch/crown/pads are written from the UI, packets are built on the motion queue.
final class Remote: ObservableObject {
    let link = BLELink()
    private let motion = MotionSource()
    private let keeper: Keeper = makeKeeper()

    @Published private(set) var playing = false
    @Published private(set) var sensorsAvailable = true
    @Published private(set) var packetsSent = 0

    private let lock = NSLock()
    private var touchX: Float = 0.5, touchY: Float = 0.5
    private var touching = false
    private var crown: Float = 0
    private var edges: Packet.Flags = []
    private var seq: UInt8 = 0
    private let started = Date()

    init() {
        WKInterfaceDevice.current().isBatteryMonitoringEnabled = true
        sensorsAvailable = motion.isAvailable
        keeper.onEnded = { [weak self] in DispatchQueue.main.async { self?.stop() } }
    }

    // MARK: from the UI

    func setTouch(x: Float, y: Float, down: Bool) {
        lock.lock(); touchX = x; touchY = y; touching = down; lock.unlock()
    }

    func endTouch() {
        lock.lock(); touching = false; lock.unlock()
    }

    func setCrown(_ turns: Double) {
        lock.lock(); crown = Float(turns); lock.unlock()
    }

    /// pad index 0...3, or nil for the double-tap gesture
    func press(pad: Int?) {
        lock.lock()
        edges.insert(pad.map { Packet.Flags.pad($0) } ?? .doubleTap)
        lock.unlock()
        WKInterfaceDevice.current().play(.click)
    }

    func play() {
        guard !playing else { return }
        playing = true
        keeper.start()
        motion.start { [weak self] frame in self?.tick(frame) }
    }

    func stop() {
        guard playing else { return }
        playing = false
        motion.stop()
        keeper.stop()
    }

    // MARK: motion queue

    private func tick(_ f: MotionFrame) {
        let device = WKInterfaceDevice.current()
        lock.lock()
        var p = Packet()
        p.seq = seq
        p.timeMs = UInt32(truncatingIfNeeded: Int(Date().timeIntervalSince(started) * 1000))
        p.flags = edges
        if touching { p.flags.insert(.touching) }
        if device.wristLocation == .right { p.flags.insert(.rightWrist) }
        if device.crownOrientation == .left { p.flags.insert(.crownLeft) }
        p.flags.insert(.workoutRunning)
        p.q = f.q; p.rot = f.rot; p.accel = f.accel; p.gravity = f.gravity
        p.crown = crown
        p.touchX = touchX; p.touchY = touchY
        p.heart = keeper.heart
        p.battery = UInt8(max(0, min(100, device.batteryLevel * 100)))
        let pending = edges
        lock.unlock()

        if link.send(p.encode()) {
            lock.lock(); seq &+= 1; edges.subtract(pending); lock.unlock()
            DispatchQueue.main.async { self.packetsSent += 1 }
        }
    }
}

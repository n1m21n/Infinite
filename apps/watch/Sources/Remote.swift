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
    private var heldRot: (Float, Packet)?
    private var heldAccel: (Float, Packet)?
    private let pendingEdgeMask: Packet.Flags = [.doubleTap, .pad(0), .pad(1), .pad(2), .pad(3)]

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
        lock.unlock()

        // The radio refuses writes while its queue is full, which is exactly when the wrist moves fast. Refused
        // packets are not thrown away: the strongest rotation and the strongest acceleration since the last write
        // are kept and go out first when the radio is ready again. seq counts every tick, so the Mac sees what
        // was still lost as gaps.
        lock.lock()
        seq &+= 1
        var queue: [Packet] = []
        while let h = takeHeld() { queue.append(h) }
        queue.append(p)
        lock.unlock()

        var sent = 0
        for (i, pk) in queue.enumerated() {
            if link.send(pk.encode()) {
                sent += 1
                lock.lock(); edges.subtract(pendingEdgeMask.intersection(pk.flags)); lock.unlock()
            } else {
                lock.lock(); for rest in queue[i...] { putBack(rest) }; lock.unlock()
                break
            }
        }
        if sent > 0 { DispatchQueue.main.async { self.packetsSent += sent } }
    }

    // MARK: held peaks (called with the lock held)

    /// The oldest held packet first; the same packet can be both peaks and goes out once.
    private func takeHeld() -> Packet? {
        let a = heldRot?.1, b = heldAccel?.1
        guard let first = [a, b].compactMap({ $0 }).min(by: { $0.timeMs < $1.timeMs }) else { return nil }
        if a?.timeMs == first.timeMs { heldRot = nil }
        if b?.timeMs == first.timeMs { heldAccel = nil }
        return first
    }

    private func putBack(_ p: Packet) {
        let m = (p.rot[0] * p.rot[0] + p.rot[1] * p.rot[1] + p.rot[2] * p.rot[2]).squareRoot()
        let a = (p.accel[0] * p.accel[0] + p.accel[1] * p.accel[1] + p.accel[2] * p.accel[2]).squareRoot()
        if heldRot == nil || m > heldRot!.0 { heldRot = (m, p) }
        if heldAccel == nil || a > heldAccel!.0 { heldAccel = (a, p) }
    }
}

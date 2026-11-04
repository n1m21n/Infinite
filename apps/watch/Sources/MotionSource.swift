import CoreMotion
import Foundation
import WatchKit

/// One reading of the wrist sensors in the units Packet uses.
struct MotionFrame {
    var q: [Float]        // w x y z
    var rot: [Float]      // deg/s
    var accel: [Float]    // g
    var gravity: [Float]  // g
}

/// Device motion at 50 Hz, delivered on its own queue. The reference frame is arbitrary-yaw, gravity vertical:
/// the Mac side recentres, so the absolute heading does not matter.
final class MotionSource {
    private let manager = CMMotionManager()
    private let queue = OperationQueue()
    static let rate = 50.0

    init() {
        queue.name = "infinite.motion"
        queue.maxConcurrentOperationCount = 1
        queue.qualityOfService = .userInteractive
    }

    var isAvailable: Bool { manager.isDeviceMotionAvailable }

    func start(_ handler: @escaping (MotionFrame) -> Void) {
        guard manager.isDeviceMotionAvailable, !manager.isDeviceMotionActive else { return }
        manager.deviceMotionUpdateInterval = 1.0 / MotionSource.rate
        manager.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: queue) { motion, _ in
            guard let m = motion else { return }
            let a = m.attitude.quaternion
            let r = m.rotationRate
            let deg = Float(180.0 / Double.pi)
            handler(MotionFrame(
                q: [Float(a.w), Float(a.x), Float(a.y), Float(a.z)],
                rot: [Float(r.x) * deg, Float(r.y) * deg, Float(r.z) * deg],
                accel: [Float(m.userAcceleration.x), Float(m.userAcceleration.y), Float(m.userAcceleration.z)],
                gravity: [Float(m.gravity.x), Float(m.gravity.y), Float(m.gravity.z)]))
        }
    }

    func stop() {
        manager.stopDeviceMotionUpdates()
    }
}

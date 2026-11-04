import Foundation

/// One fixed packet and the bytes `WatchPacket::Encode` in src/core/WatchPacket.cpp produces for it.
/// The same hex is asserted by WATCHPACKETTEST on the Mac, so a change on either side fails a test.
enum GoldenPacket {
    static let hex = "4957010740e2010001013373cd0c66e666267b0038fe1503f4011efbd007640038ffca037b000040ffbf4858"

    static var sample: Packet {
        var p = Packet()
        p.seq = 7
        p.timeMs = 123456
        p.flags = [.touching, .workoutRunning]
        p.q = [0.9, 0.1, -0.2, 0.3]
        p.rot = [12.3, -45.6, 78.9]
        p.accel = [0.5, -1.25, 2.0]
        p.gravity = [0.1, -0.2, 0.97]
        p.crown = 0.123
        p.touchX = 0.25
        p.touchY = 0.75
        p.heart = 72
        p.battery = 88
        return p
    }

    /// nil when the Swift encoder matches the golden bytes, else a one-line description of the difference.
    static func mismatch() -> String? {
        let got = sample.encode()
        if got.count != Packet.size { return "size \(got.count), want \(Packet.size)" }
        let gotHex = got.map { String(format: "%02x", $0) }.joined()
        return gotHex == hex ? nil : "got \(gotHex)\nwant \(hex)"
    }
}

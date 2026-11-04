import Foundation

/// Wire format v1 of the Apple Watch link (docs/plans/apple-watch/README.md section 6), 44 bytes, little-endian.
/// `src/core/WatchPacket.cpp` is the reference; GoldenPacket.swift pins both to the same bytes.
struct Packet {
    static let size = 44

    struct Flags: OptionSet {
        let rawValue: UInt16
        static let touching = Flags(rawValue: 1 << 0)
        static let doubleTap = Flags(rawValue: 1 << 1)   // edge
        static let pad1 = Flags(rawValue: 1 << 2)        // pads 1-4 are bits 2-5 (edges)
        static let pad2 = Flags(rawValue: 1 << 3)
        static let pad3 = Flags(rawValue: 1 << 4)
        static let pad4 = Flags(rawValue: 1 << 5)
        static let rightWrist = Flags(rawValue: 1 << 6)
        static let crownLeft = Flags(rawValue: 1 << 7)
        static let workoutRunning = Flags(rawValue: 1 << 8)

        static func pad(_ index: Int) -> Flags { Flags(rawValue: UInt16(1) << UInt16(2 + index)) }
    }

    var seq: UInt8 = 0
    var timeMs: UInt32 = 0
    var flags: Flags = []
    var q: [Float] = [1, 0, 0, 0]            // attitude w x y z
    var rot: [Float] = [0, 0, 0]             // rotation rate, deg/s
    var accel: [Float] = [0, 0, 0]           // user acceleration, g
    var gravity: [Float] = [0, 0, -1]        // g
    var crown: Float = 0                     // accumulated turns
    var touchX: Float = 0                    // 0...1
    var touchY: Float = 0                    // 0...1
    var heart: UInt8 = 0                     // bpm, 0 = unknown
    var battery: UInt8 = 0                   // percent

    /// value * scale, rounded to nearest-even (like std::nearbyint) and saturated to Int16.
    private static func q16(_ v: Float, _ scale: Float) -> Int16 {
        let r = (v * scale).rounded(.toNearestOrEven)
        return Int16(max(-32768, min(32767, r)))
    }

    private static func u16(_ v: Float) -> UInt16 {
        UInt16(max(0, min(65535, (v * 65535).rounded(.toNearestOrEven))))
    }

    func encode() -> Data {
        var d = Data(capacity: Packet.size)
        func put16(_ v: UInt16) { d.append(UInt8(v & 0xff)); d.append(UInt8(v >> 8)) }
        func putS16(_ v: Int16) { put16(UInt16(bitPattern: v)) }
        d.append(contentsOf: [0x49, 0x57, 1, seq])           // 'I' 'W', version 1, seq
        put16(UInt16(timeMs & 0xffff)); put16(UInt16(timeMs >> 16))
        put16(flags.rawValue)
        for i in 0..<4 { putS16(Packet.q16(q[i], 32768)) }
        for i in 0..<3 { putS16(Packet.q16(rot[i], 10)) }
        for i in 0..<3 { putS16(Packet.q16(accel[i], 1000)) }
        for i in 0..<3 { putS16(Packet.q16(gravity[i], 1000)) }
        putS16(Packet.q16(crown, 1000))
        put16(Packet.u16(touchX)); put16(Packet.u16(touchY))
        d.append(heart); d.append(battery)
        return d
    }
}

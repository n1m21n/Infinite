import XCTest
@testable import InfiniteRemote

final class PacketTests: XCTestCase {
    func testMatchesWatchPacketEncode() {
        XCTAssertNil(GoldenPacket.mismatch())
    }

    func testSaturatesAndFlags() {
        var p = Packet()
        p.rot = [1e6, -1e6, 0]
        p.touchX = -1
        p.touchY = 2
        p.flags = [.pad(3)]
        let d = p.encode()
        XCTAssertEqual(d.count, Packet.size)
        XCTAssertEqual(Int16(bitPattern: UInt16(d[18]) | UInt16(d[19]) << 8), 32767)
        XCTAssertEqual(Int16(bitPattern: UInt16(d[20]) | UInt16(d[21]) << 8), -32768)
        XCTAssertEqual(UInt16(d[38]) | UInt16(d[39]) << 8, 0)
        XCTAssertEqual(UInt16(d[40]) | UInt16(d[41]) << 8, 65535)
        XCTAssertEqual(UInt16(d[8]) | UInt16(d[9]) << 8, 1 << 5)
    }
}

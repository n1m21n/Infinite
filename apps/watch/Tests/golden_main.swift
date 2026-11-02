// macOS runner for the golden packet check, no Xcode project needed:  apps/watch/Tests/run_golden.sh
import Foundation

if let diff = GoldenPacket.mismatch() {
    print("GOLDEN PACKET FAIL\n\(diff)")
    exit(1)
}
print("GOLDEN PACKET PASS")

// Spike 1.0 (docs/plans/vst-au/README.md 12.1): the shared-memory block protocol
// between the plugin shell (host process) and the engine helper (own process).
//
// Audio flows as two sample rings indexed by absolute frame position:
//   shell writes input frames [P, P+n) and posts a block descriptor;
//   engine processes them and writes output frames [P, P+n), then advances doneFrames.
//   shell reads output frames [R-L, R-L+n): L = reported latency (one max block),
//   positions below zero read as silence. It only waits for doneFrames >= R-L+n,
//   which with variable block sizes is always <= what was posted before this call,
//   so in the normal case the engine has had one whole host period to cook.
//
// Wake-ups use os_sync_wait_on_address (macOS 14.4+, shared flag) with a polling
// fallback on older systems. Only plain loads/stores and one wake per block on the
// host audio thread.
#pragma once

#include <atomic>
#include <cstdint>

namespace infspike {

constexpr uint32_t kMagic = 0x494E4631;  // "INF1"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxBlock = 8192;   // the SDK validator uses 8192
constexpr uint32_t kRingFrames = 32768;  // power of two, >= 4 * kMaxBlock
constexpr uint32_t kRingMask = kRingFrames - 1;
constexpr uint32_t kDescCount = 256;     // power of two
constexpr uint32_t kChannels = 2;

struct BlockDesc {
    uint64_t startFrame;  // absolute position of the first frame
    uint32_t frames;
    uint32_t offline;
};

struct alignas(64) Shm {
    uint32_t magic;
    uint32_t version;
    double sampleRate;
    uint32_t maxBlock;
    uint32_t pad0;

    alignas(64) std::atomic<uint64_t> postedFrames;  // shell -> engine
    std::atomic<uint32_t> postedSeq;                 // wait word for the engine
    alignas(64) std::atomic<uint64_t> doneFrames;    // engine -> shell
    std::atomic<uint32_t> doneSeq;                   // wait word for the shell
    alignas(64) std::atomic<uint64_t> heartbeat;     // engine bumps every loop
    std::atomic<uint32_t> attached;                  // engine sets 1 when mapped
    std::atomic<uint32_t> quit;                      // shell sets 1 to stop

    BlockDesc desc[kDescCount];
    float inRing[kChannels][kRingFrames];
    float outRing[kChannels][kRingFrames];
};

// Wait until *word != expected or timeoutNs passes (0 = forever). Returns false on timeout.
bool WaitWord(std::atomic<uint32_t>* word, uint32_t expected, uint64_t timeoutNs);
void WakeWord(std::atomic<uint32_t>* word);
uint64_t NowNs();

}  // namespace infspike

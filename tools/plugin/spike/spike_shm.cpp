#include "spike_shm.h"

#include <mach/mach_time.h>
#include <time.h>
#include <os/os_sync_wait_on_address.h>

namespace infspike {

uint64_t NowNs() { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

bool WaitWord(std::atomic<uint32_t>* word, uint32_t expected, uint64_t timeoutNs) {
    const uint64_t start = NowNs();
    while (word->load(std::memory_order_acquire) == expected) {
        uint64_t left = 0;
        if (timeoutNs) {
            const uint64_t el = NowNs() - start;
            if (el >= timeoutNs) return false;
            left = timeoutNs - el;
        }
        if (__builtin_available(macOS 14.4, *)) {
            if (timeoutNs)
                os_sync_wait_on_address_with_timeout(word, expected, sizeof(uint32_t),
                                                     OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                                     OS_CLOCK_MACH_ABSOLUTE_TIME, left);
            else
                os_sync_wait_on_address(word, expected, sizeof(uint32_t),
                                        OS_SYNC_WAIT_ON_ADDRESS_SHARED);
        } else {
            struct timespec ts{0, 50000};  // 50 us poll on old systems
            nanosleep(&ts, nullptr);
        }
    }
    return true;
}

void WakeWord(std::atomic<uint32_t>* word) {
    if (__builtin_available(macOS 14.4, *))
        os_sync_wake_by_address_any(word, sizeof(uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
}

}  // namespace infspike

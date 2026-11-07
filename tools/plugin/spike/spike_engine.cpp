// Spike engine helper: maps the shm, cooks every posted block, exits with its parent.
// Stand-in for the Infinite engine in plugin mode (step 1.4). The "graph" here is a
// gain of 0.5 on the input plus a quiet 440 Hz sine, so a host test can tell the
// engine really ran.
//
// usage: infinite-spike-engine <shm-name> <parent-pid>
#include "spike_shm.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

using namespace infspike;

static void WatchParent(pid_t parent) {
    std::thread([parent] {
        int kq = kqueue();
        struct kevent ev;
        EV_SET(&ev, parent, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        if (kevent(kq, &ev, 1, nullptr, 0, nullptr) == -1) _exit(0);  // parent already gone
        struct kevent out;
        kevent(kq, nullptr, 0, &out, 1, nullptr);
        _exit(0);
    }).detach();
}

static void MakeRealtime(double sampleRate, uint32_t block) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    const double nsPerTick = double(tb.numer) / tb.denom;
    const double periodNs = 1e9 * block / sampleRate;
    thread_time_constraint_policy_data_t p;
    p.period = uint32_t(periodNs / nsPerTick);
    p.computation = uint32_t(periodNs * 0.5 / nsPerTick);
    p.constraint = uint32_t(periodNs * 0.9 / nsPerTick);
    p.preemptible = 1;
    thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                      (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <shm-name> <parent-pid>\n", argv[0]);
        return 2;
    }
    WatchParent(pid_t(atoi(argv[2])));

    int fd = shm_open(argv[1], O_RDWR, 0);
    if (fd < 0) { perror("shm_open"); return 1; }
    void* mem = mmap(nullptr, sizeof(Shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }
    Shm* s = static_cast<Shm*>(mem);
    if (s->magic != kMagic || s->version != kVersion) { fprintf(stderr, "bad shm header\n"); return 1; }

    MakeRealtime(s->sampleRate > 0 ? s->sampleRate : 48000.0, s->maxBlock ? s->maxBlock : 512);
    s->attached.store(1, std::memory_order_release);

    double phase = 0.0;
    uint64_t done = s->doneFrames.load(std::memory_order_relaxed);
    uint32_t descRead = 0;
    uint32_t seenSeq = s->postedSeq.load(std::memory_order_acquire);
    while (!s->quit.load(std::memory_order_acquire)) {
        s->heartbeat.fetch_add(1, std::memory_order_relaxed);
        const uint64_t posted = s->postedFrames.load(std::memory_order_acquire);
        if (posted == done) {
            WaitWord(&s->postedSeq, seenSeq, 100'000'000);  // 100 ms, then re-check quit
            seenSeq = s->postedSeq.load(std::memory_order_acquire);
            continue;
        }
        // Cook every whole block posted so far, in order.
        while (done < posted) {
            const BlockDesc d = s->desc[descRead & (kDescCount - 1)];
            ++descRead;
            const double inc = 2.0 * M_PI * 440.0 / (s->sampleRate > 0 ? s->sampleRate : 48000.0);
            for (uint32_t i = 0; i < d.frames; ++i) {
                const uint32_t idx = uint32_t((d.startFrame + i) & kRingMask);
                const float tone = 0.05f * float(std::sin(phase));
                phase += inc;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                for (uint32_t c = 0; c < kChannels; ++c)
                    s->outRing[c][idx] = 0.5f * s->inRing[c][idx] + tone;
            }
            done = d.startFrame + d.frames;
            s->doneFrames.store(done, std::memory_order_release);
            s->doneSeq.fetch_add(1, std::memory_order_release);
            WakeWord(&s->doneSeq);
        }
    }
    munmap(mem, sizeof(Shm));
    return 0;
}

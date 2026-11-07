// B-5 baseline: shared-memory round trip (post, wake, engine cooks, wake back) for one
// block, p50 / p99, at block sizes 32..2048, paced like a host at 48 kHz.
//
// usage: spike_bench <path-to-infinite-spike-engine> [iterations-per-size]
// prints one JSON object per block size.
#include "spike_shm.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <new>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;
using namespace infspike;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <engine> [iters]\n", argv[0]); return 2; }
    const int iters = argc > 2 ? atoi(argv[2]) : 2000;
    const double sr = 48000.0;
    const uint32_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048};

    for (uint32_t block : sizes) {
        char name[32];
        snprintf(name, sizeof name, "/infspkb.%d.%u", getpid(), block);
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, sizeof(Shm)) != 0) { perror("shm"); return 1; }
        void* mem = mmap(nullptr, sizeof(Shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        Shm* s = new (mem) Shm();
        s->magic = kMagic; s->version = kVersion; s->sampleRate = sr; s->maxBlock = block;

        char pidStr[16]; snprintf(pidStr, sizeof pidStr, "%d", getpid());
        char* av[] = {argv[1], name, pidStr, nullptr};
        pid_t pid;
        const uint64_t tSpawn = NowNs();
        if (posix_spawn(&pid, argv[1], nullptr, nullptr, av, environ) != 0) { perror("spawn"); return 1; }
        while (!s->attached.load(std::memory_order_acquire)) usleep(100);
        const double attachMs = (NowNs() - tSpawn) / 1e6;

        std::vector<double> us;
        us.reserve(iters);
        uint64_t posted = 0;
        uint32_t desc = 0;
        const uint64_t periodNs = uint64_t(1e9 * block / sr);
        uint64_t next = NowNs();
        for (int i = 0; i < iters + 50; ++i) {
            next += periodNs;
            s->desc[desc++ & (kDescCount - 1)] = BlockDesc{posted, block, 0};
            posted += block;
            const uint64_t t0 = NowNs();
            s->postedFrames.store(posted, std::memory_order_release);
            s->postedSeq.fetch_add(1, std::memory_order_release);
            WakeWord(&s->postedSeq);
            while (s->doneFrames.load(std::memory_order_acquire) < posted) {
                const uint32_t seq = s->doneSeq.load(std::memory_order_acquire);
                if (s->doneFrames.load(std::memory_order_acquire) >= posted) break;
                WaitWord(&s->doneSeq, seq, 50'000'000);
            }
            if (i >= 50) us.push_back((NowNs() - t0) / 1e3);  // skip warm-up
            const uint64_t now = NowNs();
            if (next > now) { struct timespec ts{0, long(next - now)}; nanosleep(&ts, nullptr); }
        }
        s->quit.store(1); s->postedSeq.fetch_add(1); WakeWord(&s->postedSeq);
        int st; waitpid(pid, &st, 0);
        munmap(mem, sizeof(Shm));
        shm_unlink(name);

        std::sort(us.begin(), us.end());
        auto pct = [&](double p) { return us[std::min(us.size() - 1, size_t(p * us.size()))]; };
        const double blockUs = 1e6 * block / sr;
        printf("{\"block\": %u, \"block_us\": %.1f, \"p50_us\": %.1f, \"p99_us\": %.1f, "
               "\"max_us\": %.1f, \"p99_pct_of_block\": %.1f, \"attach_ms\": %.2f}\n",
               block, blockUs, pct(0.5), pct(0.99), us.back(), 100.0 * pct(0.99) / blockUs, attachMs);
        fflush(stdout);
    }
    return 0;
}

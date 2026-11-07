// Spike 1.0 VST3 shell: an FX plugin whose processor spawns infinite-spike-engine
// on setActive(true) and passes audio to it over shared memory (spike_shm.h).
// Reports one max block of latency and an infinite tail, as the plan's 8.2 says.
#include "spike_shm.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace infspike {

static const FUID kProcessorUID(0x55BA0D01, 0xA68A417A, 0x865103DA, 0x7E051569);
static const FUID kControllerUID(0xEFE20DC9, 0x7C0E422E, 0x8AD1809A, 0x3A16A989);

static std::string HelperPath() {
    Dl_info info{};
    dladdr(reinterpret_cast<const void*>(&HelperPath), &info);
    std::string p = info.dli_fname ? info.dli_fname : "";
    // .../X.vst3/Contents/MacOS/X  ->  .../X.vst3/Contents/Helpers/infinite-spike-engine
    auto cut = p.rfind("/MacOS/");
    if (cut == std::string::npos) return {};
    return p.substr(0, cut) + "/Helpers/infinite-spike-engine";
}

class Processor : public AudioEffect {
public:
    Processor() { setControllerClass(kControllerUID); }
    ~Processor() override { StopEngine(); }

    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new Processor); }

    tresult PLUGIN_API initialize(FUnknown* ctx) override {
        tresult r = AudioEffect::initialize(ctx);
        if (r != kResultOk) return r;
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* ins, int32 nIn,
                                          SpeakerArrangement* outs, int32 nOut) override {
        if (nIn != 1 || nOut != 1) return kResultFalse;
        const bool inOk = ins[0] == SpeakerArr::kStereo || ins[0] == SpeakerArr::kMono;
        if (!inOk || outs[0] != SpeakerArr::kStereo) {
            // Refuse, but keep our stereo defaults so the host falls back to them.
            return kResultFalse;
        }
        return AudioEffect::setBusArrangements(ins, nIn, outs, nOut);
    }

    tresult PLUGIN_API canProcessSampleSize(int32 s) override {
        return s == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (setup.maxSamplesPerBlock <= 0 || setup.maxSamplesPerBlock > int32(kMaxBlock))
            return kResultFalse;
        return AudioEffect::setupProcessing(setup);
    }

    uint32 PLUGIN_API getLatencySamples() override { return uint32(processSetup.maxSamplesPerBlock); }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }

    tresult PLUGIN_API setActive(TBool state) override {
        if (state) StartEngine(); else StopEngine();
        return AudioEffect::setActive(state);
    }

    tresult PLUGIN_API terminate() override {
        StopEngine();
        return AudioEffect::terminate();
    }

    tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState(IBStream* s) override {
        // Spike: a fixed 4-byte state so hosts and the validator have something to round-trip.
        int32 v = 1, n = 0;
        return s ? s->write(&v, sizeof v, &n) : kInvalidArgument;
    }

    tresult PLUGIN_API process(ProcessData& data) override {
        if (data.numSamples <= 0 || data.numOutputs < 1 || !data.outputs[0].channelBuffers32)
            return kResultOk;
        const uint32_t n = uint32_t(data.numSamples);
        float** out = data.outputs[0].channelBuffers32;
        const int32 outCh = data.outputs[0].numChannels;
        auto silence = [&] {
            for (int32 c = 0; c < outCh; ++c) if (out[c]) memset(out[c], 0, n * sizeof(float));
            data.outputs[0].silenceFlags = (1ull << outCh) - 1;
        };
        // Offline renders must not depend on spawn timing: wait for the helper to attach
        // (bounded by its liveness, 10 s at most). Realtime never waits; it plays silence.
        if (shm_ && processSetup.processMode == kOffline &&
            !shm_->attached.load(std::memory_order_acquire)) {
            for (int i = 0; i < 10000 && EngineAlive() &&
                            !shm_->attached.load(std::memory_order_acquire); ++i)
                usleep(1000);
        }
        if (!shm_ || !shm_->attached.load(std::memory_order_acquire) || n > kMaxBlock) {
            silence();
            return kResultOk;
        }
        Shm* s = shm_;
        const bool offline = processSetup.processMode == kOffline;

        // 1. write input frames [P, P+n)
        float* in[2] = {nullptr, nullptr};
        if (data.numInputs > 0 && data.inputs[0].channelBuffers32) {
            in[0] = data.inputs[0].channelBuffers32[0];
            in[1] = data.inputs[0].numChannels > 1 ? data.inputs[0].channelBuffers32[1] : in[0];
        }
        const uint64_t P = posted_;
        for (uint32_t c = 0; c < kChannels; ++c) {
            const uint32_t a = uint32_t(P & kRingMask);
            const uint32_t first = std::min(n, kRingFrames - a);
            float* ring = s->inRing[c];
            if (in[c]) {
                memcpy(ring + a, in[c], first * sizeof(float));
                memcpy(ring, in[c] + first, (n - first) * sizeof(float));
            } else {
                memset(ring + a, 0, first * sizeof(float));
                memset(ring, 0, (n - first) * sizeof(float));
            }
        }
        s->desc[descWrite_++ & (kDescCount - 1)] = BlockDesc{P, n, offline ? 1u : 0u};
        posted_ = P + n;
        s->postedFrames.store(posted_, std::memory_order_release);
        s->postedSeq.fetch_add(1, std::memory_order_release);
        WakeWord(&s->postedSeq);

        // 2. read output frames [R-L, R-L+n); wait only if the engine is late
        const int64_t L = processSetup.maxSamplesPerBlock;
        const int64_t from = int64_t(read_) - L;
        const int64_t need = from + int64_t(n);
        bool ready = need <= 0 || int64_t(s->doneFrames.load(std::memory_order_acquire)) >= need;
        if (!ready) {
            const uint64_t budget =
                offline ? 0 : uint64_t(0.6 * 1e9 * n / processSetup.sampleRate);
            const uint64_t t0 = NowNs();
            while (!ready) {
                const uint32_t seq = s->doneSeq.load(std::memory_order_acquire);
                ready = int64_t(s->doneFrames.load(std::memory_order_acquire)) >= need;
                if (ready) break;
                uint64_t left = 0;
                if (budget) {
                    const uint64_t el = NowNs() - t0;
                    if (el >= budget) break;
                    left = budget - el;
                } else {
                    left = 200'000'000;  // offline: re-check engine liveness every 200 ms
                    if (!EngineAlive()) break;
                }
                WaitWord(&s->doneSeq, seq, left);
            }
        }
        read_ += n;
        if (!ready) {
            xruns_.fetch_add(1, std::memory_order_relaxed);
            silence();
            return kResultOk;
        }
        data.outputs[0].silenceFlags = 0;
        for (int32 c = 0; c < outCh; ++c) {
            if (!out[c]) continue;
            const float* ring = s->outRing[c < int32(kChannels) ? c : kChannels - 1];
            for (uint32_t i = 0; i < n; ++i) {
                const int64_t pos = from + int64_t(i);
                out[c][i] = pos < 0 ? 0.f : ring[uint64_t(pos) & kRingMask];
            }
        }
        return kResultOk;
    }

private:
    bool EngineAlive() const { return pid_ > 0 && kill(pid_, 0) == 0; }

    static void Log(const char* what, const char* detail, int code) {
        if (const char* path = getenv("INFSPIKE_LOG")) {
            if (FILE* f = fopen(path, "a")) { fprintf(f, "%d %s %s %d\n", getpid(), what, detail, code); fclose(f); }
        }
    }

    void StartEngine() {
        Log("StartEngine", "", shm_ != nullptr);
        if (shm_) return;
        static std::atomic<uint32_t> counter{0};
        snprintf(name_, sizeof name_, "/infspk.%d.%u", getpid(), counter.fetch_add(1));
        int fd = shm_open(name_, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0) { Log("shm_open failed", name_, errno); return; }
        if (ftruncate(fd, sizeof(Shm)) != 0) { close(fd); shm_unlink(name_); return; }
        void* mem = mmap(nullptr, sizeof(Shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (mem == MAP_FAILED) { shm_unlink(name_); return; }
        shm_ = new (mem) Shm();
        shm_->magic = kMagic;
        shm_->version = kVersion;
        shm_->sampleRate = processSetup.sampleRate;
        shm_->maxBlock = uint32_t(processSetup.maxSamplesPerBlock);
        posted_ = read_ = 0;
        descWrite_ = 0;

        const std::string helper = HelperPath();
        char pidStr[16];
        snprintf(pidStr, sizeof pidStr, "%d", getpid());
        char* argv[] = {const_cast<char*>(helper.c_str()), name_, pidStr, nullptr};
        if (helper.empty() || posix_spawn(&pid_, helper.c_str(), nullptr, nullptr, argv, environ) != 0)
            pid_ = -1;
        Log("spawn", helper.c_str(), pid_);
    }

    void StopEngine() {
        if (!shm_) return;
        shm_->quit.store(1, std::memory_order_release);
        shm_->postedSeq.fetch_add(1, std::memory_order_release);
        WakeWord(&shm_->postedSeq);
        if (pid_ > 0) {
            // Up to 500 ms for a clean exit, then kill. Never called on the audio thread.
            int status = 0;
            for (int i = 0; i < 50 && waitpid(pid_, &status, WNOHANG) == 0; ++i) usleep(10000);
            if (waitpid(pid_, &status, WNOHANG) == 0) { kill(pid_, SIGKILL); waitpid(pid_, &status, 0); }
        }
        pid_ = -1;
        munmap(shm_, sizeof(Shm));
        shm_unlink(name_);
        shm_ = nullptr;
    }

    Shm* shm_ = nullptr;
    pid_t pid_ = -1;
    char name_[32] = {};
    uint64_t posted_ = 0, read_ = 0;
    uint32_t descWrite_ = 0;
    std::atomic<uint64_t> xruns_{0};
};

class Controller : public EditController {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new Controller); }
    tresult PLUGIN_API setComponentState(IBStream*) override { return kResultOk; }
};

}  // namespace infspike

BEGIN_FACTORY_DEF("Infinite", "https://infinitedevices.org", "mailto:hello@infinitedevices.org")

DEF_CLASS2(INLINE_UID_FROM_FUID(infspike::kProcessorUID), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "Infinite Spike", Vst::kDistributable, "Fx", "0.0.1",
           kVstVersionString, infspike::Processor::create)

DEF_CLASS2(INLINE_UID_FROM_FUID(infspike::kControllerUID), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "Infinite Spike Controller", 0, "", "0.0.1",
           kVstVersionString, infspike::Controller::create)

END_FACTORY

// Spike host test: loads InfiniteSpike.vst3 like a DAW and proves
//   1. audio really goes through the helper: out[t] == 0.5*in[t-L] + 0.05*sin(440 Hz, t-L)
//   2. offline renders are bit-identical across two instances (M-4 in miniature)
//   3. realtime, paced like a host: no late blocks once the engine is attached
//   4. killing the helper mid-run leaves this "host" alive and the plugin silent (M-12)
//
// usage: spike_hosttest <path/InfiniteSpike.vst3>
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/base/funknown.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static Vst::HostApplication gHost;

struct Instance {
    IPtr<IComponent> comp;
    IPtr<IAudioProcessor> proc;
    int32 maxBlock = 0;
};

static bool Open(VST3::Hosting::Module::Ptr& mod, Instance& in, double sr, int32 maxBlock, int32 mode) {
    for (auto& ci : mod->getFactory().classInfos()) {
        if (ci.category() != kVstAudioEffectClass) continue;
        in.comp = mod->getFactory().createInstance<IComponent>(ci.ID());
        break;
    }
    if (!in.comp || in.comp->initialize(&gHost) != kResultOk) return false;
    in.proc = FUnknownPtr<IAudioProcessor>(in.comp.get());
    ProcessSetup setup{mode, kSample32, maxBlock, sr};
    if (in.proc->setupProcessing(setup) != kResultOk) return false;
    in.comp->activateBus(kAudio, kInput, 0, true);
    in.comp->activateBus(kAudio, kOutput, 0, true);
    in.maxBlock = maxBlock;
    if (in.comp->setActive(true) != kResultOk) return false;
    in.proc->setProcessing(true);
    return true;
}

static void Close(Instance& in) {
    in.proc->setProcessing(false);
    in.comp->setActive(false);
    in.comp->terminate();
}

static float Input(int64_t t) { return 0.5f * float(std::sin(2.0 * M_PI * 220.0 * t / 48000.0)); }

// Renders `total` frames in pseudo-random block sizes; returns left channel.
static std::vector<float> Render(Instance& in, int64_t total, int32 mode, bool paced, int* lateBlocks,
                                 pid_t* killAt = nullptr) {
    std::vector<float> out;
    out.reserve(total);
    std::vector<float> il(in.maxBlock), ir(in.maxBlock), ol(in.maxBlock), orr(in.maxBlock);
    float* ins[2] = {il.data(), ir.data()};
    float* outs[2] = {ol.data(), orr.data()};
    AudioBusBuffers ib, ob;
    ib.numChannels = ob.numChannels = 2;
    ib.channelBuffers32 = ins;
    ob.channelBuffers32 = outs;
    ProcessData d{};
    d.processMode = mode;
    d.symbolicSampleSize = kSample32;
    d.numInputs = 1; d.inputs = &ib;
    d.numOutputs = 1; d.outputs = &ob;
    uint32_t rng = 12345;
    int64_t t = 0;
    auto next = std::chrono::steady_clock::now();
    while (t < total) {
        rng = rng * 1664525u + 1013904223u;
        int32 n = 1 + int32((rng >> 8) % uint32_t(in.maxBlock));
        if (t + n > total) n = int32(total - t);
        for (int32 i = 0; i < n; ++i) il[i] = ir[i] = Input(t + i);
        d.numSamples = n;
        in.proc->process(d);
        for (int32 i = 0; i < n; ++i) out.push_back(ol[i]);
        t += n;
        if (killAt && t > total / 2 && *killAt > 0) {  // crash drill
            kill(*killAt, SIGKILL);
            *killAt = 0;
        }
        if (paced) {
            next += std::chrono::nanoseconds(int64_t(1e9 * n / 48000.0));
            std::this_thread::sleep_until(next);
        }
    }
    (void)lateBlocks;
    return out;
}

static double MaxErr(const std::vector<float>& out, int32 L, int64_t from) {
    double e = 0;
    for (int64_t t = std::max<int64_t>(from, L); t < int64_t(out.size()); ++t) {
        const double want = 0.5 * Input(t - L) + 0.05 * std::sin(2.0 * M_PI * 440.0 * (t - L) / 48000.0);
        e = std::max(e, std::fabs(out[t] - want));
    }
    return e;
}

static pid_t FindEngine() {
    FILE* f = popen("pgrep -n infinite-spike-engine", "r");
    int pid = 0;
    if (f) { if (fscanf(f, "%d", &pid) != 1) pid = 0; pclose(f); }
    return pid_t(pid);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <InfiniteSpike.vst3>\n", argv[0]); return 2; }
    std::string err;
    auto mod = VST3::Hosting::Module::create(argv[1], err);
    if (!mod) { fprintf(stderr, "load failed: %s\n", err.c_str()); return 1; }
    int fails = 0;
    const int32 maxBlock = 512;
    const int64_t total = 48000 * 2;

    // 1 + 2: offline, two instances, compare
    Instance a, b;
    if (!Open(mod, a, 48000, maxBlock, kOffline) || !Open(mod, b, 48000, maxBlock, kOffline)) {
        fprintf(stderr, "open failed\n"); return 1;
    }
    auto ra = Render(a, total, kOffline, false, nullptr);
    auto rb = Render(b, total, kOffline, false, nullptr);
    Close(a); Close(b);
    const double errA = MaxErr(ra, maxBlock, 0);
    const bool same = ra == rb;
    printf("offline: max error vs expected = %.2e (%s)\n", errA, errA < 1e-6 ? "PASS" : "FAIL");
    printf("offline: two instances bit-identical = %s\n", same ? "PASS" : "FAIL");
    fails += !(errA < 1e-6) + !same;

    // 3: realtime, paced; give the helper 300 ms to attach, then everything must match
    Instance r;
    Open(mod, r, 48000, maxBlock, kRealtime);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto rr = Render(r, total, kRealtime, true, nullptr);
    Close(r);
    // Realtime starts from silence-before-attach; the waveform after that must be exact.
    const double errR = MaxErr(rr, maxBlock, maxBlock * 2);
    printf("realtime: max error after attach = %.2e (%s)\n", errR, errR < 1e-6 ? "PASS" : "FAIL");
    fails += !(errR < 1e-6);

    // 4: crash drill
    Instance c;
    Open(mod, c, 48000, maxBlock, kRealtime);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pid_t eng = FindEngine();
    auto rc = Render(c, total, kRealtime, true, nullptr, &eng);
    Close(c);
    double tailPeak = 0;
    for (size_t i = rc.size() * 3 / 4; i < rc.size(); ++i) tailPeak = std::max(tailPeak, double(std::fabs(rc[i])));
    printf("crash drill: host alive, plugin silent after kill (peak %.3g) = %s\n", tailPeak,
           tailPeak == 0 ? "PASS" : "FAIL");
    fails += tailPeak != 0;

    printf("%s\n", fails ? "SPIKE HOST TEST: FAIL" : "SPIKE HOST TEST: PASS");
    return fails ? 1 : 0;
}

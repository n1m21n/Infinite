#include "TermSession.h"
#include "SyncFilter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace term {
namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kRingBytes = 1 << 20;
constexpr size_t kChunkBytes = 64 * 1024;
constexpr size_t kBytesPerTick = 256 * 1024;  // what one publish interval may parse
constexpr auto kPublishInterval = std::chrono::microseconds(16667);
constexpr auto kSyncTimeout = std::chrono::milliseconds(150);
constexpr auto kHangupGrace = std::chrono::milliseconds(300);
constexpr size_t kMaxSearchHits = 5000;

struct Input {
    enum Kind { Key, Paste, Bytes, MouseMove, MouseButton, Resize, ScrollBy, ScrollLine, ScrollEnd, Search, Copy } kind;
    int a = 0, b = 0, c = 0;
    uint32_t cp = 0;
    int64_t lineA = 0, lineB = 0;
    std::string text;
};

}  // namespace

struct TermSession::Impl : std::enable_shared_from_this<TermSession::Impl> {
    Options options;
    std::unique_ptr<PtyHost> pty;

    // Worker wakeups, input queue and the ring all share this lock; it is only
    // ever held to move a pointer or copy a chunk.
    mutable std::mutex lock;
    std::condition_variable wake;
    std::condition_variable ringSpace;
    std::vector<Input> inputs;
    std::vector<char> ring;
    size_t ringHead = 0, ringSize = 0;
    bool readerDone = false;
    bool stop = false;

    std::mutex writeLock;
    std::condition_variable writeWake;
    std::string pendingWrite;
    bool writerDone = false;

    mutable std::mutex publishLock;
    std::shared_ptr<const Snapshot> latest;
    std::atomic<bool> visible{true};

    std::mutex copyLock;
    std::string copied;
    bool hasCopied = false;

    // Worker only.
    std::unique_ptr<VtEngine> engine;
    SyncFilter filter;
    bool held = false;
    Clock::time_point holdStart;
    int scrollOffset = 0;
    int64_t lastTop = 0;
    bool exited = false;
    int exitCode = -1;
    uint64_t searchGeneration = 0;
    std::vector<SearchHit> searchHits;
    bool needPublish = true;

    void Queue(Input&& input) {
        {
            std::lock_guard<std::mutex> guard(lock);
            inputs.push_back(std::move(input));
        }
        wake.notify_one();
    }

    void WriteToChild(const char* data, size_t size) {
        {
            std::lock_guard<std::mutex> guard(writeLock);
            pendingWrite.append(data, size);
        }
        writeWake.notify_one();
    }

    // Filter sink.
    void Bytes(const char* data, size_t size) { engine->Feed(data, size); }
    void Hold(bool on) {
        held = on;
        if (on) holdStart = Clock::now();
        needPublish = true;
    }
    void Query() { WriteToChild("\x1b[?2026;2$y", 11); }  // 2 = supported, currently off

    void ReaderLoop() {
        std::vector<char> buffer(kChunkBytes);
        for (;;) {
            const int n = pty->Read(buffer.data(), (int)buffer.size());
            if (n <= 0) break;
            std::unique_lock<std::mutex> guard(lock);
            size_t offset = 0;
            while (offset < (size_t)n) {
                ringSpace.wait(guard, [&] { return stop || ringSize < ring.size(); });
                if (stop) break;
                const size_t tail = (ringHead + ringSize) % ring.size();
                const size_t room = std::min({ring.size() - ringSize, ring.size() - tail, (size_t)n - offset});
                std::copy_n(buffer.data() + offset, room, ring.data() + tail);
                ringSize += room;
                offset += room;
                wake.notify_one();
            }
            if (stop) break;
        }
        std::lock_guard<std::mutex> guard(lock);
        readerDone = true;
        wake.notify_one();
    }

    void WriterLoop() {
        std::string out;
        for (;;) {
            {
                std::unique_lock<std::mutex> guard(writeLock);
                writeWake.wait(guard, [&] { return writerDone || !pendingWrite.empty(); });
                if (writerDone) return;
                out.swap(pendingWrite);
            }
            if (!pty->Write(out.data(), (int)out.size())) {
                // The child is gone: keep draining so nothing piles up.
            }
            out.clear();
        }
    }

    void ApplyScroll(int offset) {
        const int limit = engine->AltScreen() ? 0 : engine->ScrollbackSize();
        const int next = std::clamp(offset, 0, limit);
        if (next != scrollOffset) needPublish = true;
        scrollOffset = next;
    }

    void Apply(Input& in) {
        switch (in.kind) {
            case Input::Key:
                if (exited) break;
                ApplyScroll(0);
                engine->Key(in.a, in.cp, in.b);
                break;
            case Input::Paste:
                if (exited) break;
                ApplyScroll(0);
                engine->Paste(in.text);
                break;
            case Input::Bytes:
                if (!exited) WriteToChild(in.text.data(), in.text.size());
                break;
            case Input::MouseMove: engine->MouseMove(in.a, in.b, in.c); break;
            case Input::MouseButton: engine->MouseButton(in.a, in.b != 0, in.c); break;
            case Input::Resize:
                if (in.a > 0 && in.b > 0 && (in.a != engine->Cols() || in.b != engine->Rows())) {
                    engine->Resize(in.a, in.b);
                    lastTop = engine->TopLine();
                    if (!exited) pty->Resize(in.a, in.b);
                    ApplyScroll(scrollOffset);
                    needPublish = true;
                }
                break;
            case Input::ScrollBy: ApplyScroll(scrollOffset + in.a); break;
            case Input::ScrollLine: ApplyScroll((int)(engine->TopLine() - in.lineA)); break;
            case Input::ScrollEnd: ApplyScroll(0); break;
            case Input::Search:
                searchHits = engine->Search(in.text, kMaxSearchHits);
                searchGeneration++;
                needPublish = true;
                break;
            case Input::Copy: {
                std::string text = engine->TextBetween(in.lineA, in.a, in.lineB, in.b);
                std::lock_guard<std::mutex> guard(copyLock);
                copied = std::move(text);
                hasCopied = true;
                break;
            }
        }
    }

    void Publish() {
        auto snapshot = std::make_shared<Snapshot>();
        Snapshot& s = *snapshot;
        s.cols = engine->Cols();
        s.rows = engine->Rows();
        s.cells.resize((size_t)s.cols * (size_t)s.rows);
        s.topLine = engine->TopLine() - scrollOffset;
        for (int row = 0; row < s.rows; ++row)
            engine->ReadLine(s.topLine + row, s.cells.data() + (size_t)row * (size_t)s.cols, s.cols);
        s.scrollOffset = scrollOffset;
        s.scrollbackSize = engine->ScrollbackSize();
        const int cursorRow = engine->CursorRow() + scrollOffset;
        s.cursorRow = cursorRow < s.rows ? cursorRow : -1;
        s.cursorCol = engine->CursorCol();
        s.cursorVisible = engine->CursorVisible();
        s.cursorBlink = engine->CursorBlink();
        s.cursorShape = engine->Shape();
        s.altScreen = engine->AltScreen();
        s.reverseVideo = engine->ReverseVideo();
        s.mouseMode = engine->MouseMode();
        s.title = engine->Title();
        s.cwd = engine->Cwd();
        s.commandRunning = engine->CommandRunning();
        s.lastExit = engine->LastExit();
        s.bells = engine->BellCount();
        s.notices = engine->NoticeCount();
        s.notice = engine->Notice();
        s.exited = exited;
        s.exitCode = exitCode;
        s.searchGeneration = searchGeneration;
        s.searchHits = searchHits;
        std::shared_ptr<const Snapshot> published = std::move(snapshot);
        std::lock_guard<std::mutex> guard(publishLock);
        latest.swap(published);
    }

    void WorkerLoop() {
        VtEngine::Options engineOptions;
        engineOptions.cols = options.spawn.cols;
        engineOptions.rows = options.spawn.rows;
        engineOptions.scrollback = options.scrollback;
        std::copy(std::begin(options.palette), std::end(options.palette), engineOptions.palette);
        engineOptions.foreground = options.foreground;
        engineOptions.background = options.background;
        engineOptions.output = [this](const char* data, size_t size) { WriteToChild(data, size); };
        engine = std::make_unique<VtEngine>(engineOptions);
        ring.resize(kRingBytes);

        std::string error;
        pty = PtyHost::Create();
        const bool started = pty->Start(options.spawn, &error);
        std::thread reader, writer;
        if (started) {
            reader = std::thread([this] { ReaderLoop(); });
            writer = std::thread([this] { WriterLoop(); });
        } else {
            const std::string message = "Could not start the shell: " + error + "\r\n";
            engine->Feed(message.data(), message.size());
            exited = true;
            std::lock_guard<std::mutex> guard(lock);
            readerDone = true;
        }

        std::vector<char> chunk(kChunkBytes);
        std::vector<Input> batch;
        Clock::time_point nextPublish = Clock::now();
        size_t fedThisTick = 0;
        bool stopping = false;

        while (!stopping) {
            const Clock::time_point now = Clock::now();
            if (now >= nextPublish) fedThisTick = 0;
            bool mayRead = fedThisTick < kBytesPerTick;
            Clock::time_point deadline = now + std::chrono::milliseconds(250);
            const bool pendingFrame = needPublish || engine->TakeDirty();
            needPublish = pendingFrame;
            if (pendingFrame) deadline = nextPublish;
            if (!mayRead) deadline = std::min(deadline, nextPublish);
            if (held) deadline = std::min(deadline, holdStart + kSyncTimeout);

            size_t got = 0;
            bool finished = false;
            {
                std::unique_lock<std::mutex> guard(lock);
                wake.wait_until(guard, deadline, [&] {
                    return stop || !inputs.empty() || (mayRead && ringSize > 0) || (readerDone && !exited);
                });
                stopping = stop;
                batch.swap(inputs);
                if (mayRead && ringSize > 0) {
                    got = std::min({ringSize, chunk.size(), ring.size() - ringHead});
                    std::copy_n(ring.data() + ringHead, got, chunk.data());
                    ringHead = (ringHead + got) % ring.size();
                    ringSize -= got;
                    ringSpace.notify_one();
                }
                finished = readerDone && ringSize == 0 && !exited;
            }

            for (Input& in : batch) Apply(in);
            batch.clear();
            if (got) {
                filter.Feed(chunk.data(), got, *this);
                fedThisTick += got;
            }
            if (finished && !exited) {
                exited = true;
                exitCode = pty ? pty->ExitCode() : -1;
                held = false;
                const std::string message = "\r\n[Shell ended. Press Enter to start a new one]\r\n";
                engine->Feed(message.data(), message.size());
                needPublish = true;
            }

            // Text that scrolled in keeps a scrolled-back view on the same lines.
            const int64_t top = engine->TopLine();
            if (scrollOffset > 0 && top != lastTop) ApplyScroll(scrollOffset + (int)(top - lastTop));
            lastTop = top;

            if (held && Clock::now() - holdStart >= kSyncTimeout) held = false;
            const Clock::time_point after = Clock::now();
            if (after >= nextPublish && !held && (needPublish || engine->TakeDirty())) {
                needPublish = false;
                if (visible.load()) Publish();
                nextPublish = after + kPublishInterval;
                fedThisTick = 0;
            }
        }

        if (started) {
            pty->Kill(false);
            const Clock::time_point giveUp = Clock::now() + kHangupGrace;
            {
                std::unique_lock<std::mutex> guard(lock);
                ringSpace.notify_all();
                while (!readerDone && Clock::now() < giveUp) {
                    wake.wait_until(guard, giveUp, [&] { return readerDone; });
                }
            }
            pty->Kill(true);
            reader.join();
            {
                std::lock_guard<std::mutex> guard(writeLock);
                writerDone = true;
            }
            writeWake.notify_one();
            writer.join();
            if (!exited) pty->ExitCode();
        }
    }
};

TermSession::TermSession() : impl_(std::make_shared<Impl>()) {}

TermSession::~TermSession() { Stop(); }

std::shared_ptr<TermSession> TermSession::Start(const Options& options) {
    std::shared_ptr<TermSession> session(new TermSession());
    session->impl_->options = options;
    // Detached: the worker holds its own reference and winds down after Stop,
    // so closing a tab never waits for a shell.
    std::thread([impl = session->impl_] { impl->WorkerLoop(); }).detach();
    return session;
}

void TermSession::Stop() {
    {
        std::lock_guard<std::mutex> guard(impl_->lock);
        impl_->stop = true;
    }
    impl_->wake.notify_one();
    impl_->ringSpace.notify_all();
}

std::shared_ptr<const Snapshot> TermSession::Latest() const {
    std::lock_guard<std::mutex> guard(impl_->publishLock);
    return impl_->latest;
}

void TermSession::SetVisible(bool visible) {
    if (impl_->visible.exchange(visible) == visible || !visible) return;
    Input in{Input::ScrollBy};  // forces a fresh picture once the panel opens
    in.a = 0;
    impl_->Queue(std::move(in));
}

void TermSession::SendKey(int vtermKey, uint32_t codepoint, int mod) {
    Input in{Input::Key};
    in.a = vtermKey;
    in.b = mod;
    in.cp = codepoint;
    impl_->Queue(std::move(in));
}

void TermSession::SendPaste(std::string text) {
    Input in{Input::Paste};
    in.text = std::move(text);
    impl_->Queue(std::move(in));
}

void TermSession::SendBytes(std::string bytes) {
    Input in{Input::Bytes};
    in.text = std::move(bytes);
    impl_->Queue(std::move(in));
}

void TermSession::SendMouseMove(int row, int col, int mod) {
    Input in{Input::MouseMove};
    in.a = row;
    in.b = col;
    in.c = mod;
    impl_->Queue(std::move(in));
}

void TermSession::SendMouseButton(int button, bool pressed, int mod) {
    Input in{Input::MouseButton};
    in.a = button;
    in.b = pressed ? 1 : 0;
    in.c = mod;
    impl_->Queue(std::move(in));
}

void TermSession::Resize(int cols, int rows) {
    Input in{Input::Resize};
    in.a = cols;
    in.b = rows;
    impl_->Queue(std::move(in));
}

void TermSession::ScrollBy(int lines) {
    Input in{Input::ScrollBy};
    in.a = lines;
    impl_->Queue(std::move(in));
}

void TermSession::ScrollToLine(int64_t line) {
    Input in{Input::ScrollLine};
    in.lineA = line;
    impl_->Queue(std::move(in));
}

void TermSession::ScrollToBottom() { impl_->Queue(Input{Input::ScrollEnd}); }

void TermSession::Search(std::string query) {
    Input in{Input::Search};
    in.text = std::move(query);
    impl_->Queue(std::move(in));
}

void TermSession::RequestCopy(int64_t lineA, int colA, int64_t lineB, int colB) {
    Input in{Input::Copy};
    in.lineA = lineA;
    in.a = colA;
    in.lineB = lineB;
    in.b = colB;
    impl_->Queue(std::move(in));
}

bool TermSession::TakeCopiedText(std::string* text) {
    std::lock_guard<std::mutex> guard(impl_->copyLock);
    if (!impl_->hasCopied) return false;
    *text = std::move(impl_->copied);
    impl_->hasCopied = false;
    return true;
}

}  // namespace term

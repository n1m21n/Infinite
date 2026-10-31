// Headless checks for the terminal core (docs/plans/terminal/README.md Block A).
// Build target: terminal-check. Exit 0 = all passed.
#include "SyncFilter.h"
#include "TermSession.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

using namespace term;

static int gFailed = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            ++gFailed;                                             \
        }                                                          \
    } while (0)

static std::string RowText(const VtEngine& e, int row) {
    return e.TextBetween(e.TopLine() + row, 0, e.TopLine() + row, e.Cols());
}

static VtEngine::Options Small(int cols = 20, int rows = 5) {
    VtEngine::Options o;
    o.cols = cols;
    o.rows = rows;
    return o;
}

static void Feed(VtEngine& e, const std::string& s) { e.Feed(s.data(), s.size()); }

static void CheckGrid() {
    VtEngine e(Small());
    Feed(e, "hello\r\n\x1b[31mred\x1b[0m");
    CHECK(RowText(e, 0) == "hello");
    CHECK(RowText(e, 1) == "red");
    CHECK(e.CursorRow() == 1 && e.CursorCol() == 3);
    Cell row[20];
    e.ReadLine(e.TopLine() + 1, row, 20);
    CHECK(!(row[0].flags & kCellDefaultFg));
    CHECK(row[3].flags & kCellDefaultFg);
    Feed(e, "\x1b]2;my title\x07\x1b]7;file://host/tmp/a%20b\x07\x1b]133;C\x07");
    CHECK(e.Title() == "my title");
    CHECK(e.Cwd() == "/tmp/a b");
    CHECK(e.CommandRunning());
    Feed(e, "\x1b]133;D;3\x07");
    CHECK(!e.CommandRunning() && e.LastExit() == 3);
}

static void CheckWide() {
    VtEngine e(Small());
    Feed(e, "a\xe4\xbd\xa0" "b");  // a, a wide CJK, b
    Cell row[20];
    e.ReadLine(e.TopLine(), row, 20);
    CHECK(row[1].ch == 0x4F60 && row[1].width == 2);
    CHECK(row[3].ch == 'b');
    CHECK(e.CursorCol() == 4);
    CHECK(RowText(e, 0) == "a\xe4\xbd\xa0" "b");
}

static void CheckAltScreen() {
    VtEngine e(Small());
    Feed(e, "main");
    Feed(e, "\x1b[?1049h\x1b[2J\x1b[Halt");
    CHECK(e.AltScreen());
    CHECK(RowText(e, 0) == "alt");
    Feed(e, "\x1b[?1049l");
    CHECK(!e.AltScreen());
    CHECK(RowText(e, 0) == "main");
}

static void CheckScrollback() {
    VtEngine::Options o = Small(10, 3);
    o.scrollback = 5;
    VtEngine e(o);
    for (int i = 0; i < 20; ++i) Feed(e, "line" + std::to_string(i) + "\r\n");
    CHECK(e.ScrollbackSize() == 5);
    CHECK(e.TopLine() == 18);
    const int64_t oldest = e.OldestLine();
    CHECK(e.TextBetween(oldest, 0, oldest, 10) == "line13");
    const auto hits = e.Search("LINE19", 10);
    CHECK(hits.size() == 1 && hits[0].col == 0);
    // Wrapped text copies as one line.
    VtEngine w(Small(10, 3));
    Feed(w, "0123456789abcde");
    CHECK(w.TextBetween(w.TopLine(), 0, w.TopLine() + 1, 10) == "0123456789abcde");
}

static void CheckResize() {
    VtEngine e(Small(10, 3));
    Feed(e, "0123456789abcde");
    e.Resize(20, 3);
    CHECK(e.Cols() == 20);
    CHECK(RowText(e, 0) == "0123456789abcde");  // reflowed
    e.Resize(5, 3);
    CHECK(RowText(e, 0).size() == 5);
    e.Resize(80, 24);
    CHECK(e.Rows() == 24);
}

struct RecordSink {
    std::string out;
    void Bytes(const char* d, size_t n) { out.append(d, n); }
    void Hold(bool on) { out += on ? "<H>" : "<R>"; }
    void Query() { out += "<Q>"; }
};

static void CheckSyncFilter() {
    const std::string in = "ab\x1b[?2026hcd\x1b[?2026lef\x1b[?2026$pgh\x1b[?2025hij\x1b[?2026;1h\x1b\x1b[?2026l";
    const std::string want = "ab<H>cd<R>ef<Q>gh\x1b[?2025hij\x1b[?2026;1h\x1b<R>";
    // Every way of cutting it in two, and byte by byte.
    for (size_t cut = 0; cut <= in.size(); ++cut) {
        RecordSink sink;
        SyncFilter f;
        f.Feed(in.data(), cut, sink);
        f.Feed(in.data() + cut, in.size() - cut, sink);
        CHECK(sink.out == want);
    }
    RecordSink sink;
    SyncFilter f;
    for (char c : in) f.Feed(&c, 1, sink);
    CHECK(sink.out == want);
}

static void CheckColorQuery() {
    std::string out;
    VtEngine::Options o = Small();
    o.foreground = PackRgb(36, 40, 50);
    o.background = PackRgb(250, 251, 253);
    o.output = [&](const char* d, size_t n) { out.append(d, n); };
    VtEngine e(o);
    Feed(e, "\033]11;?\033\\");
    CHECK(out == "\033]11;rgb:fafa/fbfb/fdfd\033\\");
    out.clear();
    Feed(e, "\033]10;?\007");
    CHECK(out == "\033]10;rgb:2424/2828/3232\033\\");
    out.clear();
    const uint32_t palette[16] = {};
    e.SetColors(palette, PackRgb(1, 2, 3), PackRgb(14, 15, 19));
    Feed(e, "\033]11;?\033\\");
    CHECK(out == "\033]11;rgb:0e0e/0f0f/1313\033\\");
}

static bool WaitFor(const std::function<bool()>& ok, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (ok()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ok();
}

static std::string Screen(const std::shared_ptr<const Snapshot>& s) {
    std::string out;
    if (!s) return out;
    for (const Cell& c : s->cells) out += c.ch ? (c.ch < 128 ? (char)c.ch : '?') : ' ';
    return out;
}

#if !defined(_WIN32)
static TermSession::Options Sh(const char* script) {
    TermSession::Options o;
    o.spawn.shell = "/bin/sh";
    o.spawn.args = {"-c", script};
    o.spawn.cols = 40;
    o.spawn.rows = 10;
    o.spawn.env = {{"TERM", "xterm-256color"}};
    return o;
}

static void CheckSession() {
    {  // Output arrives, the exit code is seen, nothing hangs.
        auto s = TermSession::Start(Sh("printf 'hi-from-child'; exit 7"));
        CHECK(WaitFor([&] {
            auto snap = s->Latest();
            return snap && snap->exited;
        }, 3000));
        auto snap = s->Latest();
        CHECK(snap && snap->exitCode == 7);
        CHECK(Screen(snap).find("hi-from-child") != std::string::npos);
    }
    {  // Input round trip and resize: stty sees the new size.
        auto s = TermSession::Start(Sh("read line; printf 'got:%s ' \"$line\"; stty size; sleep 5"));
        CHECK(WaitFor([&] { return s->Latest() != nullptr; }, 2000));
        s->Resize(50, 12);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        s->SendBytes("ping\n");
        CHECK(WaitFor([&] { return Screen(s->Latest()).find("got:ping 12 50") != std::string::npos; }, 3000));
    }
    {  // The sync query is answered, and held output shows only after release.
        auto s = TermSession::Start(Sh(
            "stty raw -echo; printf '\\033[?2026$p'; r=$(dd bs=1 count=11 2>/dev/null); "
            "case \"$r\" in *2026*2*y) printf 'query-ok';; esac; printf '\\033[?2026h'; printf 'held'; sleep 0.05; printf '\\033[?2026l'; sleep 5"));
        CHECK(WaitFor([&] { return Screen(s->Latest()).find("held") != std::string::npos; }, 3000));
        CHECK(Screen(s->Latest()).find("query-ok") != std::string::npos);
    }
    {  // A flood is throttled and stays usable; Stop returns at once.
        auto s = TermSession::Start(Sh("yes flood"));
        CHECK(WaitFor([&] {
            auto snap = s->Latest();
            return snap && snap->scrollbackSize > 100;
        }, 3000));
        const auto before = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) s->Latest();
        s->Stop();
        const auto took = std::chrono::steady_clock::now() - before;
        CHECK(took < std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // detached workers finish
}
#endif

int main() {
    CheckGrid();
    CheckWide();
    CheckAltScreen();
    CheckScrollback();
    CheckResize();
    CheckSyncFilter();
    CheckColorQuery();
#if !defined(_WIN32)
    CheckSession();
#endif
    if (gFailed == 0) std::printf("terminal-check: all passed\n");
    return gFailed ? 1 : 0;
}

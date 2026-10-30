// TermSession: one shell and its screen, run off the main thread. Three threads:
// reader (child to a bounded ring, which is what throttles `yes`), writer (input
// to the child) and worker (owns the screen). The main thread only queues input
// and takes the latest snapshot, so it never waits on the shell
// (docs/plans/terminal/README.md D4).
#pragma once
#include "PtyHost.h"
#include "VtEngine.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace term {

struct Snapshot {
    int cols = 0;
    int rows = 0;
    std::vector<Cell> cells;  // rows * cols of what the view shows
    int64_t topLine = 0;      // absolute line of the first row shown
    int scrollOffset = 0;     // 0 = following the live screen
    int scrollbackSize = 0;
    int cursorRow = 0;  // in view rows; -1 = scrolled out of view
    int cursorCol = 0;
    bool cursorVisible = true;
    bool cursorBlink = true;
    CursorShape cursorShape = CursorShape::Block;
    bool altScreen = false;
    bool reverseVideo = false;
    int mouseMode = 0;
    std::string title;
    std::string cwd;
    bool commandRunning = false;
    bool busy = false;  // a program is running in the foreground
    int lastExit = -1;
    uint32_t bells = 0;
    uint32_t notices = 0;
    std::string notice;
    bool exited = false;
    int exitCode = -1;
    uint64_t searchGeneration = 0;
    std::vector<SearchHit> searchHits;
};

class TermSession {
public:
    struct Options {
        PtySpawn spawn;
        int scrollback = 10000;
        uint32_t palette[16] = {};
        uint32_t foreground = PackRgb(229, 229, 229);
        uint32_t background = PackRgb(0, 0, 0);
    };

    // Returns at once; the shell starts on the session's own thread.
    static std::shared_ptr<TermSession> Start(const Options& options);
    ~TermSession();

    // Current even while hidden; safe from any thread.
    bool Busy() const;

    // Ends the shell in the background. Never blocks.
    void Stop();

    // nullptr until the first frame. Cheap: swaps a pointer.
    std::shared_ptr<const Snapshot> Latest() const;
    // A closed panel costs nothing: no snapshot is built while this is false.
    void SetVisible(bool visible);

    void SendKey(int vtermKey, uint32_t codepoint, int mod);  // jumps back to the live screen
    void SendPaste(std::string text);
    void SendBytes(std::string bytes);  // raw, as if the child's input
    void SendMouseMove(int row, int col, int mod);
    void SendMouseButton(int button, bool pressed, int mod);
    void Resize(int cols, int rows);
    void ScrollBy(int lines);  // positive = back in history
    void ScrollToLine(int64_t line);
    void ScrollToBottom();

    // Answered through the Snapshot's search fields.
    void Search(std::string query);
    // Answered through TakeCopiedText.
    void RequestCopy(int64_t lineA, int colA, int64_t lineB, int colB);
    bool TakeCopiedText(std::string* text);

private:
    TermSession();
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace term

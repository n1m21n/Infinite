// VtEngine: the terminal screen model. libvterm stays behind this header
// (docs/plans/terminal/README.md). Not thread safe: the session worker owns it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace term {

enum CellFlag : uint16_t {
    kCellBold = 1 << 0,
    kCellItalic = 1 << 1,
    kCellUnderline = 1 << 2,
    kCellReverse = 1 << 3,
    kCellStrike = 1 << 4,
    kCellDim = 1 << 5,
    kCellDefaultFg = 1 << 8,
    kCellDefaultBg = 1 << 9,
};

// Colours are 0xAABBGGRR, the same layout as ImU32.
inline constexpr uint32_t PackRgb(uint32_t r, uint32_t g, uint32_t b) {
    return r | (g << 8) | (b << 16) | 0xFF000000u;
}

struct Cell {
    uint32_t ch = 0;  // 0 = empty; the base code point only
    uint32_t fg = 0;
    uint32_t bg = 0;
    uint16_t flags = kCellDefaultFg | kCellDefaultBg;
    uint8_t width = 1;  // 2 = wide; the next cell has width 0
    uint8_t pad = 0;
};
static_assert(sizeof(Cell) == 16, "Cell is copied for the whole screen every published frame");

enum class CursorShape : uint8_t { Block, Underline, Bar };

struct SearchHit {
    int64_t line = 0;  // absolute line, see VtEngine::TopLine
    int col = 0;
    int length = 0;
};

class VtEngine {
public:
    struct Options {
        int cols = 80;
        int rows = 24;
        int scrollback = 10000;
        uint32_t palette[16] = {};  // all zero = libvterm's
        uint32_t foreground = PackRgb(229, 229, 229);
        uint32_t background = PackRgb(0, 0, 0);
        std::function<void(const char*, size_t)> output;  // replies and encoded input, to the child
    };

    explicit VtEngine(const Options& options);
    ~VtEngine();

    void Feed(const char* data, size_t size);
    void Resize(int cols, int rows);
    // Colours for what comes next; cells already drawn keep theirs, except default ones, which the view resolves.
    void SetColors(const uint32_t* palette, uint32_t foreground, uint32_t background);

    // key is a VTermKey; 0 sends codepoint instead. mod: bit 1 shift, 2 alt, 4 ctrl.
    void Key(int key, uint32_t codepoint, int mod);
    void Paste(const std::string& text);
    void MouseMove(int row, int col, int mod);
    void MouseButton(int button, bool pressed, int mod);

    int Cols() const;
    int Rows() const;
    int ScrollbackSize() const { return (int)scrollback_.size(); }

    // Absolute line numbers never shift while text scrolls: line TopLine() is
    // the first line of the live screen.
    int64_t TopLine() const { return pushed_; }
    int64_t OldestLine() const { return pushed_ - (int64_t)scrollback_.size(); }
    void ReadLine(int64_t line, Cell* out, int cols) const;

    std::string TextBetween(int64_t lineA, int colA, int64_t lineB, int colB) const;
    std::vector<SearchHit> Search(const std::string& query, size_t maxHits) const;

    int CursorRow() const { return cursorRow_; }
    int CursorCol() const { return cursorCol_; }
    bool CursorVisible() const { return cursorVisible_; }
    bool CursorBlink() const { return cursorBlink_; }
    CursorShape Shape() const { return cursorShape_; }
    bool AltScreen() const { return altScreen_; }
    bool ReverseVideo() const { return reverse_; }
    int MouseMode() const { return mouseMode_; }  // 0 = off
    const std::string& Title() const { return title_; }
    const std::string& Cwd() const { return cwd_; }  // OSC 7
    int LastExit() const { return lastExit_; }       // OSC 133;D, -1 = none
    bool CommandRunning() const { return commandRunning_; }
    uint32_t BellCount() const { return bells_; }
    uint32_t NoticeCount() const { return notices_; }  // OSC 9
    const std::string& Notice() const { return notice_; }

    bool TakeDirty();

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl_;

    struct Line {
        std::vector<Cell> cells;
        bool continuation = false;  // wrapped from the line above
    };
    bool LineAt(int64_t line, std::vector<Cell>* cells, bool* continuation) const;

    std::deque<Line> scrollback_;
    int scrollbackLimit_ = 10000;
    int64_t pushed_ = 0;
    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;
    bool cursorBlink_ = true;
    CursorShape cursorShape_ = CursorShape::Block;
    bool altScreen_ = false;
    bool reverse_ = false;
    int mouseMode_ = 0;
    std::string title_;
    std::string cwd_;
    int lastExit_ = -1;
    bool commandRunning_ = false;
    uint32_t bells_ = 0;
    uint32_t notices_ = 0;
    std::string notice_;
    bool dirty_ = true;
};

}  // namespace term

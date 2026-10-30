#include "VtEngine.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <vterm.h>
}

namespace term {
namespace {

VTermColor ToVterm(uint32_t c) {
    VTermColor out;
    vterm_color_rgb(&out, (uint8_t)(c & 0xFF), (uint8_t)((c >> 8) & 0xFF), (uint8_t)((c >> 16) & 0xFF));
    return out;
}

void AppendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += (char)c;
    } else if (c < 0x800) {
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += (char)(0xE0 | (c >> 12));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    } else {
        out += (char)(0xF0 | (c >> 18));
        out += (char)(0x80 | ((c >> 12) & 0x3F));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
}

uint32_t Fold(uint32_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

}  // namespace

struct VtEngine::Impl {
    VtEngine* self = nullptr;
    VTerm* vt = nullptr;
    VTermScreen* screen = nullptr;
    std::function<void(const char*, size_t)> output;
    std::string titleBuffer;
    std::string oscBuffer;
    int oscCommand = -1;

    Cell Convert(const VTermScreenCell& in) const {
        Cell out;
        out.ch = in.chars[0] == (uint32_t)-1 ? 0 : in.chars[0];
        out.width = (uint8_t)std::max(1, (int)in.width);
        uint16_t flags = 0;
        if (in.attrs.bold) flags |= kCellBold;
        if (in.attrs.italic) flags |= kCellItalic;
        if (in.attrs.underline) flags |= kCellUnderline;
        if (in.attrs.reverse) flags |= kCellReverse;
        if (in.attrs.strike) flags |= kCellStrike;
        VTermColor fg = in.fg, bg = in.bg;
        if (VTERM_COLOR_IS_DEFAULT_FG(&fg)) flags |= kCellDefaultFg;
        else {
            vterm_screen_convert_color_to_rgb(screen, &fg);
            out.fg = PackRgb(fg.rgb.red, fg.rgb.green, fg.rgb.blue);
        }
        if (VTERM_COLOR_IS_DEFAULT_BG(&bg)) flags |= kCellDefaultBg;
        else {
            vterm_screen_convert_color_to_rgb(screen, &bg);
            out.bg = PackRgb(bg.rgb.red, bg.rgb.green, bg.rgb.blue);
        }
        out.flags = flags;
        return out;
    }

    void HandleOsc() {
        if (oscCommand == 7) {
            // file://host/path
            std::string path = oscBuffer;
            if (path.rfind("file://", 0) == 0) {
                const size_t slash = path.find('/', 7);
                path = slash == std::string::npos ? "" : path.substr(slash);
            }
            std::string decoded;
            for (size_t i = 0; i < path.size(); ++i) {
                if (path[i] == '%' && i + 2 < path.size()) {
                    decoded += (char)std::strtol(path.substr(i + 1, 2).c_str(), nullptr, 16);
                    i += 2;
                } else {
                    decoded += path[i];
                }
            }
            self->cwd_ = decoded;
        } else if (oscCommand == 9) {
            self->notice_ = oscBuffer;
            self->notices_++;
        } else if (oscCommand == 133 && !oscBuffer.empty()) {
            if (oscBuffer[0] == 'C') self->commandRunning_ = true;
            if (oscBuffer[0] == 'D') {
                self->commandRunning_ = false;
                self->lastExit_ = oscBuffer.size() > 2 ? std::atoi(oscBuffer.c_str() + 2) : 0;
            }
        }
        self->dirty_ = true;
    }

    static int OnDamage(VTermRect, void* user) {
        static_cast<Impl*>(user)->self->dirty_ = true;
        return 1;
    }
    static int OnMoveCursor(VTermPos pos, VTermPos, int, void* user) {
        VtEngine* e = static_cast<Impl*>(user)->self;
        e->cursorRow_ = pos.row;
        e->cursorCol_ = pos.col;
        e->dirty_ = true;
        return 1;
    }
    static int OnProp(VTermProp prop, VTermValue* value, void* user) {
        Impl* impl = static_cast<Impl*>(user);
        VtEngine* e = impl->self;
        switch (prop) {
            case VTERM_PROP_CURSORVISIBLE: e->cursorVisible_ = value->boolean; break;
            case VTERM_PROP_CURSORBLINK: e->cursorBlink_ = value->boolean; break;
            case VTERM_PROP_ALTSCREEN: e->altScreen_ = value->boolean; break;
            case VTERM_PROP_REVERSE: e->reverse_ = value->boolean; break;
            case VTERM_PROP_MOUSE: e->mouseMode_ = value->number; break;
            case VTERM_PROP_CURSORSHAPE:
                e->cursorShape_ = value->number == VTERM_PROP_CURSORSHAPE_UNDERLINE ? CursorShape::Underline
                                : value->number == VTERM_PROP_CURSORSHAPE_BAR_LEFT  ? CursorShape::Bar
                                                                                    : CursorShape::Block;
                break;
            case VTERM_PROP_TITLE:
                if (value->string.initial) impl->titleBuffer.clear();
                impl->titleBuffer.append(value->string.str, value->string.len);
                if (value->string.final) e->title_ = impl->titleBuffer;
                break;
            default: break;
        }
        e->dirty_ = true;
        return 1;
    }
    static int OnBell(void* user) {
        static_cast<Impl*>(user)->self->bells_++;
        return 1;
    }
    static int OnPushLine(int cols, const VTermScreenCell* cells, bool continuation, void* user) {
        Impl* impl = static_cast<Impl*>(user);
        VtEngine* e = impl->self;
        Line line;
        line.continuation = continuation;
        int used = cols;
        while (used > 0 && (cells[used - 1].chars[0] == 0 || cells[used - 1].chars[0] == (uint32_t)-1) &&
               VTERM_COLOR_IS_DEFAULT_BG(&cells[used - 1].bg))
            --used;
        line.cells.resize((size_t)used);
        for (int i = 0; i < used; ++i) line.cells[(size_t)i] = impl->Convert(cells[i]);
        e->scrollback_.push_back(std::move(line));
        e->pushed_++;
        while ((int)e->scrollback_.size() > e->scrollbackLimit_) e->scrollback_.pop_front();
        e->dirty_ = true;
        return 1;
    }
    static int OnPopLine(int cols, VTermScreenCell* cells, void* user) {
        VtEngine* e = static_cast<Impl*>(user)->self;
        if (e->scrollback_.empty()) return 0;
        const Line& line = e->scrollback_.back();
        for (int i = 0; i < cols; ++i) {
            VTermScreenCell cell{};
            cell.width = 1;
            vterm_color_indexed(&cell.fg, 0);
            cell.fg.type = VTERM_COLOR_DEFAULT_FG;
            vterm_color_indexed(&cell.bg, 0);
            cell.bg.type = VTERM_COLOR_DEFAULT_BG;
            if (i < (int)line.cells.size()) {
                const Cell& c = line.cells[(size_t)i];
                cell.chars[0] = c.ch;
                cell.width = (char)c.width;
                cell.attrs.bold = (c.flags & kCellBold) != 0;
                cell.attrs.italic = (c.flags & kCellItalic) != 0;
                cell.attrs.underline = (c.flags & kCellUnderline) != 0;
                cell.attrs.reverse = (c.flags & kCellReverse) != 0;
                cell.attrs.strike = (c.flags & kCellStrike) != 0;
                if (!(c.flags & kCellDefaultFg)) cell.fg = ToVterm(c.fg);
                if (!(c.flags & kCellDefaultBg)) cell.bg = ToVterm(c.bg);
            }
            cells[i] = cell;
        }
        e->scrollback_.pop_back();
        e->pushed_--;
        e->dirty_ = true;
        return 1;
    }
    static int OnClear(void* user) {
        VtEngine* e = static_cast<Impl*>(user)->self;
        e->scrollback_.clear();
        e->dirty_ = true;
        return 1;
    }
    static int OnOsc(int command, VTermStringFragment frag, void* user) {
        Impl* impl = static_cast<Impl*>(user);
        if (command != 7 && command != 9 && command != 133) return 0;
        if (frag.initial) {
            impl->oscCommand = command;
            impl->oscBuffer.clear();
        }
        if (impl->oscBuffer.size() + frag.len < 4096) impl->oscBuffer.append(frag.str, frag.len);
        if (frag.final) impl->HandleOsc();
        return 1;
    }
    static void OnOutput(const char* data, size_t size, void* user) {
        Impl* impl = static_cast<Impl*>(user);
        if (impl->output) impl->output(data, size);
    }
};

VtEngine::VtEngine(const Options& options) : impl_(std::make_unique<Impl>()) {
    impl_->self = this;
    impl_->output = options.output;
    scrollbackLimit_ = std::max(0, options.scrollback);
    impl_->vt = vterm_new(options.rows, options.cols);
    vterm_set_utf8(impl_->vt, 1);
    vterm_output_set_callback(impl_->vt, &Impl::OnOutput, impl_.get());
    impl_->screen = vterm_obtain_screen(impl_->vt);

    static const VTermScreenCallbacks kScreen = [] {
        VTermScreenCallbacks c{};
        c.damage = &Impl::OnDamage;
        c.movecursor = &Impl::OnMoveCursor;
        c.settermprop = &Impl::OnProp;
        c.bell = &Impl::OnBell;
        c.sb_popline = &Impl::OnPopLine;
        c.sb_clear = &Impl::OnClear;
        c.sb_pushline4 = &Impl::OnPushLine;
        return c;
    }();
    static const VTermStateFallbacks kFallbacks = [] {
        VTermStateFallbacks f{};
        f.osc = &Impl::OnOsc;
        return f;
    }();
    vterm_screen_set_callbacks(impl_->screen, &kScreen, impl_.get());
    vterm_screen_callbacks_has_pushline4(impl_->screen);
    vterm_screen_set_unrecognised_fallbacks(impl_->screen, &kFallbacks, impl_.get());
    vterm_screen_enable_altscreen(impl_->screen, 1);
    vterm_screen_enable_reflow(impl_->screen, true);
    vterm_screen_set_damage_merge(impl_->screen, VTERM_DAMAGE_SCROLL);

    VTermState* state = vterm_obtain_state(impl_->vt);
    vterm_state_set_bold_highbright(state, 0);
    const VTermColor fg = ToVterm(options.foreground), bg = ToVterm(options.background);
    vterm_state_set_default_colors(state, &fg, &bg);
    for (int i = 0; i < 16; ++i)
        if (options.palette[i]) {
            const VTermColor c = ToVterm(options.palette[i]);
            vterm_state_set_palette_color(state, i, &c);
        }
    vterm_screen_reset(impl_->screen, 1);
}

VtEngine::~VtEngine() {
    if (impl_->vt) vterm_free(impl_->vt);
}

void VtEngine::Feed(const char* data, size_t size) {
    if (size == 0) return;
    vterm_input_write(impl_->vt, data, size);
    vterm_screen_flush_damage(impl_->screen);
}

void VtEngine::Resize(int cols, int rows) {
    if (cols == Cols() && rows == Rows()) return;
    vterm_set_size(impl_->vt, rows, cols);
    vterm_screen_flush_damage(impl_->screen);
    dirty_ = true;
}

void VtEngine::Key(int key, uint32_t codepoint, int mod) {
    const VTermModifier m = (VTermModifier)mod;
    if (key) vterm_keyboard_key(impl_->vt, (VTermKey)key, m);
    else vterm_keyboard_unichar(impl_->vt, codepoint, m);
}

void VtEngine::Paste(const std::string& text) {
    vterm_keyboard_start_paste(impl_->vt);
    if (impl_->output) impl_->output(text.data(), text.size());
    vterm_keyboard_end_paste(impl_->vt);
}

void VtEngine::MouseMove(int row, int col, int mod) { vterm_mouse_move(impl_->vt, row, col, (VTermModifier)mod); }

void VtEngine::MouseButton(int button, bool pressed, int mod) {
    vterm_mouse_button(impl_->vt, button, pressed, (VTermModifier)mod);
}

int VtEngine::Cols() const {
    int rows = 0, cols = 0;
    vterm_get_size(impl_->vt, &rows, &cols);
    return cols;
}

int VtEngine::Rows() const {
    int rows = 0, cols = 0;
    vterm_get_size(impl_->vt, &rows, &cols);
    return rows;
}

void VtEngine::ReadLine(int64_t line, Cell* out, int cols) const {
    for (int i = 0; i < cols; ++i) out[i] = Cell{};
    if (line >= pushed_) {
        const int row = (int)(line - pushed_);
        if (row >= Rows()) return;
        const int width = std::min(cols, Cols());
        for (int col = 0; col < width; ++col) {
            VTermScreenCell cell;
            if (vterm_screen_get_cell(impl_->screen, VTermPos{row, col}, &cell)) out[col] = impl_->Convert(cell);
        }
        return;
    }
    const int64_t index = line - OldestLine();
    if (index < 0) return;
    const Line& stored = scrollback_[(size_t)index];
    std::copy_n(stored.cells.begin(), std::min((size_t)cols, stored.cells.size()), out);
}

bool VtEngine::LineAt(int64_t line, std::vector<Cell>* cells, bool* continuation) const {
    if (line < OldestLine() || line >= pushed_ + Rows()) return false;
    if (line >= pushed_) {
        cells->resize((size_t)Cols());
        ReadLine(line, cells->data(), Cols());
        const VTermLineInfo* info = vterm_state_get_lineinfo(vterm_obtain_state(impl_->vt), (int)(line - pushed_));
        *continuation = info && info->continuation;
    } else {
        const Line& stored = scrollback_[(size_t)(line - OldestLine())];
        *cells = stored.cells;
        *continuation = stored.continuation;
    }
    return true;
}

std::string VtEngine::TextBetween(int64_t lineA, int colA, int64_t lineB, int colB) const {
    if (lineB < lineA || (lineB == lineA && colB < colA)) {
        std::swap(lineA, lineB);
        std::swap(colA, colB);
    }
    lineA = std::max(lineA, OldestLine());
    std::string out;
    std::vector<Cell> cells;
    bool continuation = false;
    for (int64_t line = lineA; line <= lineB; ++line) {
        if (!LineAt(line, &cells, &continuation)) break;
        if (line > lineA && !continuation) out += '\n';
        const int first = line == lineA ? colA : 0;
        int last = line == lineB ? std::min(colB, (int)cells.size()) : (int)cells.size();
        if (line != lineB) {
            while (last > first && cells[(size_t)last - 1].ch == 0) --last;
        }
        std::string text;
        for (int col = first; col < last; ++col) {
            const Cell& c = cells[(size_t)col];
            if (c.ch) AppendUtf8(text, c.ch);
            else if (col == 0 || cells[(size_t)col - 1].width != 2) text += ' ';
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        out += text;
    }
    return out;
}

std::vector<SearchHit> VtEngine::Search(const std::string& query, size_t maxHits) const {
    std::vector<SearchHit> hits;
    if (query.empty()) return hits;
    std::vector<uint32_t> needle;
    for (size_t i = 0; i < query.size();) {  // UTF-8 to code points
        const unsigned char c = (unsigned char)query[i];
        const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        uint32_t cp = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : extra == 1 ? c & 0x1F : c;
        for (int k = 1; k <= extra && i + (size_t)k < query.size(); ++k)
            cp = (cp << 6) | ((unsigned char)query[i + (size_t)k] & 0x3F);
        needle.push_back(Fold(cp));
        i += (size_t)extra + 1;
    }
    std::vector<Cell> cells;
    bool continuation = false;
    const int64_t end = pushed_ + Rows();
    for (int64_t line = OldestLine(); line < end && hits.size() < maxHits; ++line) {
        if (!LineAt(line, &cells, &continuation)) continue;
        const int n = (int)cells.size(), m = (int)needle.size();
        for (int col = 0; col + m <= n && hits.size() < maxHits; ++col) {
            int k = 0;
            while (k < m && Fold(cells[(size_t)(col + k)].ch) == needle[(size_t)k]) ++k;
            if (k == m) {
                hits.push_back({line, col, m});
                col += m - 1;
            }
        }
    }
    return hits;
}

bool VtEngine::TakeDirty() {
    const bool was = dirty_;
    dirty_ = false;
    return was;
}

}  // namespace term

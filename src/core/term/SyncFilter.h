// SyncFilter: takes DEC mode 2026 (synchronized output) out of the byte stream
// before libvterm sees it. Set holds the screen, reset releases it, a query is
// answered by us. Sequences split across reads are handled; anything that only
// looks like the start of one goes through untouched.
#pragma once
#include <cstddef>
#include <cstring>

namespace term {

class SyncFilter {
public:
    // Sink needs: Bytes(const char*, size_t), Hold(bool), Query()
    template <class Sink>
    void Feed(const char* data, size_t size, Sink& sink) {
        static constexpr char kPrefix[] = "\x1b[?2026";
        static constexpr int kPrefixLen = 7;
        size_t start = 0;  // first byte of the pending pass-through run
        for (size_t i = 0; i < size; ++i) {
            const char c = data[i];
            if (match_ < kPrefixLen) {
                if (c == kPrefix[match_]) {
                    if (match_ == 0 && i > start) sink.Bytes(data + start, i - start);
                    ++match_;
                    start = i + 1;
                } else if (match_ > 0) {
                    Mismatch(sink, kPrefix, match_, "", 0);
                    start = i--;  // this byte may start a new match
                }
            } else if (match_ == kPrefixLen) {
                if (c == 'h' || c == 'l') {
                    match_ = 0;
                    sink.Hold(c == 'h');
                    start = i + 1;
                } else if (c == '$') {
                    match_ = kPrefixLen + 1;
                    start = i + 1;
                } else {
                    Mismatch(sink, kPrefix, kPrefixLen, "", 0);
                    start = i--;
                }
            } else {
                if (c == 'p') {
                    match_ = 0;
                    sink.Query();
                    start = i + 1;
                } else {
                    Mismatch(sink, kPrefix, kPrefixLen, "$", 1);
                    start = i--;
                }
            }
        }
        if (match_ == 0 && size > start) sink.Bytes(data + start, size - start);
    }

private:
    template <class Sink>
    void Mismatch(Sink& sink, const char* prefix, int count, const char* tail, size_t tailSize) {
        sink.Bytes(prefix, (size_t)count);
        if (tailSize) sink.Bytes(tail, tailSize);
        match_ = 0;
    }

    int match_ = 0;
};

}  // namespace term

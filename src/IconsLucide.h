#pragma once

// A handful of named codepoints from the Lucide icon font
// (external/icons/Lucide/lucide.ttf, ISC license) - not the full IconFont
// generated header IconFontCppHeaders ships for Lucide, deliberately: we
// only merge the glyph ranges we actually use (see the font setup in
// main()), so only the icons actually referenced from the app are named
// here. Add an entry (and widen the merged glyph range next to the font
// load) when a new icon earns a real call site - do not pre-populate this
// with icons nothing uses yet.
//
// Codepoints come from external/icons/Lucide/info.json (lucide-static
// package), which assigns each icon a fixed Private-Use-Area codepoint in
// the font. e.g. "search": { "unicode": "&#57681;" } -> 57681 decimal ->
// 0xE151.
namespace IconsLucide
{
   inline constexpr const char* Search = "\xee\x85\x91"; // U+E151 "search"
}

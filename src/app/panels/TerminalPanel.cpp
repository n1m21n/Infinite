// Terminal panel (docs/plans/terminal/README.md, Block A): docked bottom or top like the Piano Roll. The shell runs
// on its own threads (core/term/TermSession.h); this file only draws the latest snapshot and queues input, so the
// frame never waits on a shell. Open state, dock and height are app preferences; patches never touch the terminal.
#include "app/AppShared.h"
#include "app/panels/TerminalDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/PanelFrame.h"
#include "core/term/TermSession.h"
#include "imgui_internal.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <vterm.h>
}

namespace app
{

namespace
{
   constexpr float kMinHeight = 160.0f;
   constexpr float kTabRowH = 26.0f;
   constexpr float kLineHeight = 1.3f;
   constexpr int kWheelLines = 3;

   struct Tab
   {
      std::shared_ptr<term::TermSession> session;
      int cols = 0, rows = 0;
      uint32_t seenBells = 0;
      bool attention = false;  // bell or notice while in the background
      // Selection, in absolute lines so it stays on its text while output scrolls.
      bool hasSelection = false;
      int64_t selLineA = 0, selLineB = 0;
      int selColA = 0, selColB = 0;
      int lastMouseRow = -1, lastMouseCol = -1;
      bool mouseHeld = false;
   };

   std::vector<std::unique_ptr<Tab>> sTabs;
   int sActive = 0;
   bool sSelecting = false;
   bool sSearchOpen = false;
   bool sSearchFocus = false;
   bool sSearchActive = false;  // the find field has the keys, not the shell
   char sSearchText[256] = {};
   int sSearchIndex = -1;
   uint64_t sSearchSeen = 0;
   double sLastInputTime = 0.0;
   ImFont* sFont = nullptr;
   ImFont* sFontBold = nullptr;
   bool sShowQuitWarning = false;
   std::function<void()> sQuitAction;

   ImU32 Ansi(int index)
   {
      static const tok::Pair8* const kColors[] = {
         &tok::terminal_ansi0, &tok::terminal_ansi1, &tok::terminal_ansi2, &tok::terminal_ansi3,
         &tok::terminal_ansi4, &tok::terminal_ansi5, &tok::terminal_ansi6, &tok::terminal_ansi7,
         &tok::terminal_ansi8, &tok::terminal_ansi9, &tok::terminal_ansi10, &tok::terminal_ansi11,
         &tok::terminal_ansi12, &tok::terminal_ansi13, &tok::terminal_ansi14, &tok::terminal_ansi15,
      };
      return tok::U32(*kColors[index], false);
   }

   std::unique_ptr<Tab> NewTab(int cols, int rows)
   {
      term::TermSession::Options o;
      o.spawn.cols = std::max(2, cols);
      o.spawn.rows = std::max(2, rows);
      if (!gPatchPath.empty())
      {
         const size_t slash = gPatchPath.find_last_of("/\\");
         if (slash != std::string::npos)
            o.spawn.cwd = gPatchPath.substr(0, slash);
      }
      o.spawn.env = {
         { "TERM", "xterm-256color" },
         { "COLORTERM", "truecolor" },
         { "TERM_PROGRAM", "Infinite" },
         // Infinite is not on Claude Code's list of terminals with synchronized output.
         { "CLAUDE_CODE_FORCE_SYNC_OUTPUT", "1" },
      };
      if (const char* lang = getenv("LANG"); lang == nullptr || lang[0] == '\0')
         o.spawn.env.push_back({ "LANG", "en_US.UTF-8" });
      for (int i = 0; i < 16; ++i)
         o.palette[i] = Ansi(i);
      o.foreground = tok::U32(tok::terminal_fg, false);
      o.background = tok::U32(tok::terminal_bg, false);
      auto tab = std::make_unique<Tab>();
      tab->cols = o.spawn.cols;
      tab->rows = o.spawn.rows;
      tab->session = term::TermSession::Start(o);
      return tab;
   }

   void CloseTab(int index)
   {
      sTabs[(size_t)index]->session->Stop();
      sTabs.erase(sTabs.begin() + index);
      sActive = std::clamp(sActive, 0, std::max(0, (int)sTabs.size() - 1));
      if (sTabs.empty())
      {
         gTerminalOpen = false;
         gTerminalFocus = false;
      }
   }

   std::string TabTitle(const term::Snapshot* snap)
   {
      if (snap == nullptr)
         return "shell";
      if (!snap->title.empty())
         return snap->title;
      if (!snap->cwd.empty())
      {
         const size_t slash = snap->cwd.find_last_of('/');
         return slash == std::string::npos || slash + 1 == snap->cwd.size() ? snap->cwd : snap->cwd.substr(slash + 1);
      }
      return "shell";
   }

   std::string ShellQuote(const std::string& path)
   {
#if defined(_WIN32)
      return "\"" + path + "\"";
#else
      std::string out = "'";
      for (char c : path)
      {
         if (c == '\'')
            out += "'\\''";
         else
            out += c;
      }
      return out + "'";
#endif
   }

   bool IsWordChar(uint32_t c)
   {
      return c > 0x7F || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c != 0 && std::strchr("_-./~:+@%", (int)c) != nullptr);
   }

   // The URL under a cell, if any, read from the visible row.
   std::string UrlAt(const term::Snapshot& snap, int row, int col)
   {
      const term::Cell* cells = snap.cells.data() + (size_t)row * (size_t)snap.cols;
      std::string line;
      for (int i = 0; i < snap.cols; ++i)
         line += cells[i].ch >= 0x21 && cells[i].ch < 0x7F ? (char)cells[i].ch : ' ';
      for (const char* scheme : { "https://", "http://" })
      {
         size_t at = 0;
         while ((at = line.find(scheme, at)) != std::string::npos)
         {
            size_t end = line.find(' ', at);
            if (end == std::string::npos)
               end = line.size();
            if ((size_t)col >= at && (size_t)col < end)
            {
               std::string url = line.substr(at, end - at);
               while (!url.empty() && std::strchr(".,;:)]}>'\"", url.back()) != nullptr)
                  url.pop_back();
               return url;
            }
            at = end;
         }
      }
      return {};
   }

   ImU32 CellFg(const term::Cell& c)
   {
      return (c.flags & term::kCellDefaultFg) ? tok::U32(tok::terminal_fg, false) : (ImU32)c.fg;
   }
   ImU32 CellBg(const term::Cell& c)
   {
      return (c.flags & term::kCellDefaultBg) ? tok::U32(tok::terminal_bg, false) : (ImU32)c.bg;
   }

   void DrawCells(ImDrawList* dl, const term::Snapshot& snap, ImVec2 origin, float cw, float ch, float px,
                  const Tab& tab)
   {
      const ImU32 defaultBg = tok::U32(tok::terminal_bg, false);
      // Selection and search are drawn with the backgrounds, under the text.
      const auto selected = [&](int64_t line, int col)
      {
         if (!tab.hasSelection)
            return false;
         int64_t la = tab.selLineA, lb = tab.selLineB;
         int ca = tab.selColA, cb = tab.selColB;
         if (lb < la || (lb == la && cb < ca))
         {
            std::swap(la, lb);
            std::swap(ca, cb);
         }
         if (line < la || line > lb)
            return false;
         if (line == la && col < ca)
            return false;
         return !(line == lb && col >= cb);
      };
      std::vector<char> matched;
      if (sSearchOpen && !snap.searchHits.empty())
      {
         matched.assign(snap.cells.size(), 0);
         for (const term::SearchHit& hit : snap.searchHits)
         {
            const int64_t row = hit.line - snap.topLine;
            if (row < 0 || row >= snap.rows)
               continue;
            for (int k = 0; k < hit.length && hit.col + k < snap.cols; ++k)
               matched[(size_t)row * (size_t)snap.cols + (size_t)(hit.col + k)] = 1;
         }
      }

      for (int row = 0; row < snap.rows; ++row)
      {
         const term::Cell* cells = snap.cells.data() + (size_t)row * (size_t)snap.cols;
         const float y = origin.y + (float)row * ch;
         const int64_t line = snap.topLine + row;
         int runStart = 0;
         ImU32 runColor = defaultBg;
         for (int col = 0; col <= snap.cols; ++col)
         {
            ImU32 bg = defaultBg;
            if (col < snap.cols)
            {
               const term::Cell& c = cells[col];
               bg = (c.flags & term::kCellReverse) ? CellFg(c) : CellBg(c);
               if (selected(line, col))
                  bg = tok::U32(tok::terminal_selection, false);
               else if (!matched.empty() && matched[(size_t)row * (size_t)snap.cols + (size_t)col])
                  bg = tok::U32(tok::terminal_match, false);
            }
            if (col == snap.cols || bg != runColor)
            {
               if (runColor != defaultBg && col > runStart)
                  dl->AddRectFilled(ImVec2(origin.x + (float)runStart * cw, y), ImVec2(origin.x + (float)col * cw, y + ch), runColor);
               runStart = col;
               runColor = bg;
            }
         }
      }

      const float textOffset = std::floor((ch - px) * 0.5f);
      std::string run;
      for (int row = 0; row < snap.rows; ++row)
      {
         const term::Cell* cells = snap.cells.data() + (size_t)row * (size_t)snap.cols;
         const float y = origin.y + (float)row * ch;
         int col = 0;
         while (col < snap.cols)
         {
            const term::Cell& c = cells[col];
            if (c.ch <= 0x20)
            {
               ++col;
               continue;
            }
            const ImU32 fg = (c.flags & term::kCellReverse) ? CellBg(c) : CellFg(c);
            const float x = origin.x + (float)col * cw;
            if (TerminalDraw::IsShape(c.ch))
            {
               TerminalDraw::Shape(dl, c.ch, x, y, cw, ch, fg);
               ++col;
               continue;
            }
            ImFont* font = (c.flags & term::kCellBold) ? sFontBold : sFont;
            run.clear();
            int end = col;
            if (c.ch < 0x7F)
            {
               // ASCII of one style draws as one string: far fewer draw calls for a screen of text.
               for (; end < snap.cols; ++end)
               {
                  const term::Cell& n = cells[end];
                  if (n.ch <= 0x20 || n.ch >= 0x7F || n.flags != c.flags || n.fg != c.fg || n.bg != c.bg)
                     break;
                  run += (char)n.ch;
               }
            }
            else
            {
               ImWchar wide[2] = { (ImWchar)c.ch, 0 };
               char utf8[8];
               ImTextStrToUtf8(utf8, sizeof utf8, wide, wide + 1);
               run = utf8;
               end = col + std::max<int>(1, c.width);
            }
            dl->AddText(font, px, ImVec2(x, y + textOffset), fg, run.c_str(), run.c_str() + run.size());
            const float x1 = origin.x + (float)end * cw;
            if (c.flags & term::kCellUnderline)
               dl->AddRectFilled(ImVec2(x, y + ch - 2.0f), ImVec2(x1, y + ch - 1.0f), fg);
            if (c.flags & term::kCellStrike)
               dl->AddRectFilled(ImVec2(x, y + std::floor(ch * 0.5f)), ImVec2(x1, y + std::floor(ch * 0.5f) + 1.0f), fg);
            col = end;
         }
      }
   }

   void DrawCursor(ImDrawList* dl, const term::Snapshot& snap, ImVec2 origin, float cw, float ch, bool focused)
   {
      if (!snap.cursorVisible || snap.cursorRow < 0 || snap.scrollOffset > 0)
         return;
      const double now = ImGui::GetTime();
      // Solid while typing; blink only once input stops.
      const bool blinkOff = focused && snap.cursorBlink && now - sLastInputTime > 0.6 && std::fmod(now, 1.06) > 0.53;
      if (blinkOff)
         return;
      const float x = origin.x + (float)snap.cursorCol * cw, y = origin.y + (float)snap.cursorRow * ch;
      const ImU32 color = tok::U32(tok::terminal_cursor, false);
      if (!focused)
         dl->AddRect(ImVec2(x, y), ImVec2(x + cw, y + ch), color);
      else if (snap.cursorShape == term::CursorShape::Bar)
         dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 2.0f, y + ch), color);
      else if (snap.cursorShape == term::CursorShape::Underline)
         dl->AddRectFilled(ImVec2(x, y + ch - 2.0f), ImVec2(x + cw, y + ch), color);
      else
      {
         dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + ch), color);
         const term::Cell& c = snap.cells[(size_t)snap.cursorRow * (size_t)snap.cols + (size_t)snap.cursorCol];
         if (c.ch > 0x20 && !TerminalDraw::IsShape(c.ch) && c.ch < 0x10000)
         {
            ImWchar wide[2] = { (ImWchar)c.ch, 0 };
            char utf8[8];
            ImTextStrToUtf8(utf8, sizeof utf8, wide, wide + 1);
            const float px = ImGui::GetFontSize();
            dl->AddText((c.flags & term::kCellBold) ? sFontBold : sFont, px, ImVec2(x, y + std::floor((ch - px) * 0.5f)),
                        tok::U32(tok::terminal_bg, false), utf8);
         }
      }
   }

   int Modifiers(const ImGuiIO& io, bool physicalCtrl)
   {
      return (io.KeyShift ? VTERM_MOD_SHIFT : 0) | (io.KeyAlt ? VTERM_MOD_ALT : 0) | (physicalCtrl ? VTERM_MOD_CTRL : 0);
   }

   void HandleKeyboard(Tab& tab, const term::Snapshot* snap)
   {
      ImGuiIO& io = ImGui::GetIO();
#if defined(__APPLE__)
      const bool physicalCtrl = io.KeySuper;  // ImGui swaps Cmd and Ctrl on macOS
      const bool appMod = io.KeyCtrl && !io.KeyShift;
#else
      const bool physicalCtrl = io.KeyCtrl;
      const bool appMod = io.KeyCtrl && io.KeyShift;
#endif
      const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
      const auto repeated = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };

      if (appMod)
      {
         if (pressed(ImGuiKey_C) && tab.hasSelection)
            tab.session->RequestCopy(tab.selLineA, tab.selColA, tab.selLineB, tab.selColB);
         if (pressed(ImGuiKey_V))
         {
            if (const char* text = ImGui::GetClipboardText(); text != nullptr && text[0] != '\0')
               tab.session->SendPaste(text);
         }
         if (pressed(ImGuiKey_F))
         {
            sSearchOpen = true;
            sSearchFocus = true;
         }
         if (pressed(ImGuiKey_T))
         {
            sTabs.push_back(NewTab(tab.cols, tab.rows));
            sActive = (int)sTabs.size() - 1;
         }
         if (pressed(ImGuiKey_W))
         {
            CloseTab(sActive);
            io.InputQueueCharacters.resize(0);
            return;
         }
         if (pressed(ImGuiKey_K))
            tab.session->SendBytes("\x0c");
         io.InputQueueCharacters.resize(0);
         return;
      }
      const int mod = Modifiers(io, physicalCtrl);
      const auto send = [&](int key, uint32_t cp = 0, int withMod = -1)
      {
         tab.session->SendKey(key, cp, withMod < 0 ? mod : withMod);
         sLastInputTime = ImGui::GetTime();
      };

      if (snap != nullptr && snap->exited)
      {
         if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))
         {
            const int cols = tab.cols, rows = tab.rows;
            tab.session->Stop();
            *sTabs[(size_t)sActive] = *NewTab(cols, rows);
         }
         io.InputQueueCharacters.resize(0);
         return;
      }

      static const struct { ImGuiKey key; int vterm; } kKeys[] = {
         { ImGuiKey_Enter, VTERM_KEY_ENTER }, { ImGuiKey_KeypadEnter, VTERM_KEY_ENTER }, { ImGuiKey_Tab, VTERM_KEY_TAB },
         { ImGuiKey_Backspace, VTERM_KEY_BACKSPACE }, { ImGuiKey_Escape, VTERM_KEY_ESCAPE },
         { ImGuiKey_UpArrow, VTERM_KEY_UP }, { ImGuiKey_DownArrow, VTERM_KEY_DOWN },
         { ImGuiKey_LeftArrow, VTERM_KEY_LEFT }, { ImGuiKey_RightArrow, VTERM_KEY_RIGHT },
         { ImGuiKey_Insert, VTERM_KEY_INS }, { ImGuiKey_Delete, VTERM_KEY_DEL }, { ImGuiKey_Home, VTERM_KEY_HOME },
         { ImGuiKey_End, VTERM_KEY_END }, { ImGuiKey_PageUp, VTERM_KEY_PAGEUP }, { ImGuiKey_PageDown, VTERM_KEY_PAGEDOWN },
      };
      if (io.KeyShift && pressed(ImGuiKey_PageUp))
      {
         tab.session->ScrollBy(tab.rows - 1);
         return;
      }
      if (io.KeyShift && pressed(ImGuiKey_PageDown))
      {
         tab.session->ScrollBy(-(tab.rows - 1));
         return;
      }
      for (const auto& k : kKeys)
         if (repeated(k.key))
            send(k.vterm);
      for (int i = 0; i < 12; ++i)
         if (repeated((ImGuiKey)(ImGuiKey_F1 + i)))
            send(VTERM_KEY_FUNCTION(i + 1));

      if (physicalCtrl)
      {
         // Control characters never reach the text queue.
         for (int key = ImGuiKey_A; key <= ImGuiKey_Z; ++key)
            if (repeated((ImGuiKey)key))
               send(0, (uint32_t)('a' + (key - ImGuiKey_A)));
         static const struct { ImGuiKey key; char c; } kPunct[] = {
            { ImGuiKey_Space, ' ' }, { ImGuiKey_LeftBracket, '[' }, { ImGuiKey_RightBracket, ']' },
            { ImGuiKey_Backslash, '\\' }, { ImGuiKey_Slash, '/' }, { ImGuiKey_Minus, '-' },
         };
         for (const auto& k : kPunct)
            if (repeated(k.key))
               send(0, (uint32_t)k.c);
         io.InputQueueCharacters.resize(0);
         return;
      }
      // Text, with what the OS keyboard layout made of it.
      for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
      {
         const ImWchar c = io.InputQueueCharacters[i];
         if (c >= 0x20 && c != 0x7F)
         {
#if defined(__APPLE__)
            send(0, (uint32_t)c, 0);  // Option composes characters here
#else
            send(0, (uint32_t)c, io.KeyAlt ? VTERM_MOD_ALT : 0);
#endif
         }
      }
      io.InputQueueCharacters.resize(0);
   }

   void HandleMouse(Tab& tab, const term::Snapshot& snap, ImVec2 origin, ImVec2 size, float cw, float ch)
   {
      ImGuiIO& io = ImGui::GetIO();
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      const ImVec2 mouse = io.MousePos;
      const int col = std::clamp((int)((mouse.x - origin.x) / cw), 0, snap.cols - 1);
      const int row = std::clamp((int)((mouse.y - origin.y) / ch), 0, snap.rows - 1);
      const int64_t line = snap.topLine + row;
      const int vmod = (io.KeyShift ? VTERM_MOD_SHIFT : 0) | (io.KeyAlt ? VTERM_MOD_ALT : 0);
      if (hovered)
         ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

      // Shift always selects, even in a program that takes the mouse.
      const bool reported = snap.mouseMode != 0 && !io.KeyShift;

      if (hovered && io.MouseWheel != 0.0f)
      {
         const int ticks = (int)std::ceil(std::fabs(io.MouseWheel));
         const bool up = io.MouseWheel > 0.0f;
         if (reported)
         {
            tab.session->SendMouseMove(row, col, vmod);
            for (int i = 0; i < ticks; ++i)
            {
               tab.session->SendMouseButton(up ? 4 : 5, true, vmod);
               tab.session->SendMouseButton(up ? 4 : 5, false, vmod);
            }
         }
         else if (snap.altScreen)
         {
            for (int i = 0; i < ticks * kWheelLines; ++i)
               tab.session->SendKey(up ? VTERM_KEY_UP : VTERM_KEY_DOWN, 0, 0);
         }
         else
         {
            tab.session->ScrollBy((up ? 1 : -1) * ticks * kWheelLines);
         }
      }

      if (reported)
      {
         if (hovered && (row != tab.lastMouseRow || col != tab.lastMouseCol))
         {
            tab.session->SendMouseMove(row, col, vmod);
            tab.lastMouseRow = row;
            tab.lastMouseCol = col;
         }
         static const int kButtons[] = { 1, 3, 2 };  // ImGui left, right, middle
         for (int b = 0; b < 3; ++b)
         {
            if (hovered && ImGui::IsMouseClicked(b))
               tab.session->SendMouseButton(kButtons[b], true, vmod);
            if (ImGui::IsMouseReleased(b))
               tab.session->SendMouseButton(kButtons[b], false, vmod);
         }
         tab.hasSelection = false;
         return;
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         if (io.KeyCtrl || io.KeySuper)  // Cmd or Ctrl + click opens a link
         {
            const std::string url = UrlAt(snap, row, col);
            if (!url.empty())
            {
               Platform::OpenExternalUrl(url);
               return;
            }
         }
         const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
         const term::Cell* cells = snap.cells.data() + (size_t)row * (size_t)snap.cols;
         tab.selLineA = tab.selLineB = line;
         tab.selColA = tab.selColB = col;
         tab.hasSelection = clicks > 1;
         if (clicks == 2)
         {
            int a = col, b = col + 1;
            while (a > 0 && IsWordChar(cells[a - 1].ch))
               --a;
            while (b < snap.cols && IsWordChar(cells[b].ch))
               ++b;
            tab.selColA = a;
            tab.selColB = b;
         }
         else if (clicks >= 3)
         {
            tab.selColA = 0;
            tab.selColB = snap.cols;
         }
         sSelecting = clicks == 1;
      }
      if (sSelecting && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
      {
         tab.selLineB = line;
         tab.selColB = col;
         tab.hasSelection = tab.selLineA != tab.selLineB || tab.selColA != tab.selColB;
         if (mouse.y < origin.y)
            tab.session->ScrollBy(1);
         else if (mouse.y > origin.y + size.y)
            tab.session->ScrollBy(-1);
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         sSelecting = false;
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
      {
         if (const char* text = ImGui::GetClipboardText(); text != nullptr && text[0] != '\0')
            tab.session->SendPaste(text);
      }
   }

   void DrawTabRow(const term::Snapshot* active)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 start = ImGui::GetCursorScreenPos();
      const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      float x = start.x;
      int closeIndex = -1;
      for (int i = 0; i < (int)sTabs.size(); ++i)
      {
         Tab& tab = *sTabs[(size_t)i];
         const std::shared_ptr<const term::Snapshot> snap = tab.session->Latest();
         if (i != sActive && snap && snap->bells != tab.seenBells)
            tab.attention = true;
         if (snap)
            tab.seenBells = snap->bells;
         if (i == sActive)
            tab.attention = false;
         std::string title = TabTitle(snap.get());
         if (title.size() > 28)
            title = title.substr(0, 27) + "...";
         const float width = ImGui::CalcTextSize(title.c_str()).x + 44.0f;
         ImGui::PushID(i);
         ImGui::SetCursorScreenPos(ImVec2(x, start.y));
         ImGui::InvisibleButton("##tab", ImVec2(width, kTabRowH - 4.0f));
         const bool hovered = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked())
         {
            sActive = i;
            gTerminalFocus = true;
         }
         if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
            closeIndex = i;
         if (i == sActive || hovered)
            dl->AddRectFilled(ImVec2(x, start.y), ImVec2(x + width, start.y + kTabRowH - 4.0f),
                              ImGui::GetColorU32(ImGuiCol_FrameBg), tok::radius_field);
         // Status dot: running, attention, or a failed last command.
         const bool busy = snap && snap->busy;
         const bool failed = snap && snap->exited && snap->exitCode != 0;
         const ImU32 dot = failed ? Ansi(1) : tab.attention ? Ansi(3) : busy ? Ansi(2) : dimColor;
         dl->AddCircleFilled(ImVec2(x + 10.0f, start.y + (kTabRowH - 4.0f) * 0.5f), 3.0f, dot);
         dl->AddText(ImVec2(x + 19.0f, start.y + (kTabRowH - 4.0f - ImGui::GetFontSize()) * 0.5f),
                     i == sActive ? textColor : dimColor, title.c_str());
         if (hovered || i == sActive)
         {
            ImGui::SetCursorScreenPos(ImVec2(x + width - 20.0f, start.y + 3.0f));
            ImGui::InvisibleButton("##close", ImVec2(16.0f, 16.0f));
            if (ImGui::IsItemClicked())
               closeIndex = i;
            const ImVec2 c(x + width - 12.0f, start.y + 11.0f);
            const ImU32 mark = ImGui::IsItemHovered() ? textColor : dimColor;
            dl->AddLine(ImVec2(c.x - 3.5f, c.y - 3.5f), ImVec2(c.x + 3.5f, c.y + 3.5f), mark, 1.25f);
            dl->AddLine(ImVec2(c.x + 3.5f, c.y - 3.5f), ImVec2(c.x - 3.5f, c.y + 3.5f), mark, 1.25f);
         }
         ImGui::PopID();
         x += width + 4.0f;
      }
      ImGui::SetCursorScreenPos(ImVec2(x, start.y));
      ImGui::InvisibleButton("##newtab", ImVec2(22.0f, kTabRowH - 4.0f));
      {
         const ImVec2 c(x + 11.0f, start.y + (kTabRowH - 4.0f) * 0.5f);
         const ImU32 mark = ImGui::IsItemHovered() ? textColor : dimColor;
         dl->AddLine(ImVec2(c.x - 4.5f, c.y), ImVec2(c.x + 4.5f, c.y), mark, 1.25f);
         dl->AddLine(ImVec2(c.x, c.y - 4.5f), ImVec2(c.x, c.y + 4.5f), mark, 1.25f);
         if (ImGui::IsItemClicked() && active != nullptr)
         {
            sTabs.push_back(NewTab(active->cols, active->rows));
            sActive = (int)sTabs.size() - 1;
            gTerminalFocus = true;
         }
      }
      if (closeIndex >= 0)
         CloseTab(closeIndex);
      ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + kTabRowH));
   }

   void DrawSearchRow(Tab& tab, const term::Snapshot* snap)
   {
      const float width = ImGui::GetContentRegionAvail().x;
      ImGui::SetNextItemWidth(std::min(280.0f, width - 90.0f));
      if (sSearchFocus)
      {
         ImGui::SetKeyboardFocusHere();
         sSearchFocus = false;
      }
      const bool edited = FieldWell::InputTextWithHint("##termsearch", T("Find in terminal"), sSearchText, sizeof sSearchText);
      const bool focused = ImGui::IsItemActive();
      sSearchActive = focused;
      if (edited)
      {
         tab.session->Search(sSearchText);
         sSearchIndex = -1;
      }
      const int count = snap ? (int)snap->searchHits.size() : 0;
      // A fresh result set lands on the newest match.
      if (snap && snap->searchGeneration != sSearchSeen)
      {
         sSearchSeen = snap->searchGeneration;
         sSearchIndex = count - 1;
         if (count > 0)
            tab.session->ScrollToLine(snap->searchHits[(size_t)sSearchIndex].line);
      }
      ImGui::SameLine();
      char label[32];
      if (sSearchText[0] != '\0')
      {
         std::snprintf(label, sizeof label, "%d / %d", count > 0 ? sSearchIndex + 1 : 0, count);
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("%s", label);
      }
      if (focused && count > 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, true))
      {
         sSearchIndex = (sSearchIndex + (ImGui::GetIO().KeyShift ? 1 : count - 1)) % count;
         tab.session->ScrollToLine(snap->searchHits[(size_t)sSearchIndex].line);
         sSearchFocus = true;
      }
      if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      {
         sSearchOpen = false;
         sSearchActive = false;
         gTerminalFocus = true;
         tab.session->ScrollToBottom();
      }
   }

   // INFINITE_TERMINALTEST: a real shell in the real panel. Echoes a marker with a resized grid, then exits 0 or 1.
   void RunSelfTest(Tab& tab, const term::Snapshot* snap)
   {
      static double sStart = ImGui::GetTime();
      static bool sSent = false;
      if (snap == nullptr)
         return;
      if (!sSent)
      {
         tab.session->SendBytes("printf 'infinite-%s\\n' terminal-ok\n");
         sSent = true;
      }
      const std::string want = "infinite-terminal-ok";
      std::string screen;
      for (const term::Cell& c : snap->cells)
         screen += c.ch >= 0x20 && c.ch < 0x7F ? (char)c.ch : ' ';
      const bool pass = screen.find(want) != std::string::npos;
      if (pass || ImGui::GetTime() - sStart > 10.0)
      {
         std::printf("[TerminalTest] %s cols=%d rows=%d\n", pass ? "PASS" : "FAIL", snap->cols, snap->rows);
         std::fflush(stdout);
         std::_Exit(pass ? 0 : 1);
      }
   }

   void DrawPanel()
   {
      if (sTabs.empty())
      {
         sTabs.push_back(NewTab(80, 24));
         sActive = 0;
      }
      if (sFont == nullptr)
      {
         ImGui::TextDisabled("%s", T("The terminal font is missing from this install."));
         return;
      }
      Tab* tab = sTabs[(size_t)sActive].get();
      std::shared_ptr<const term::Snapshot> snap = tab->session->Latest();
      for (const auto& t : sTabs)
         t->session->SetVisible(t.get() == tab);

      DrawTabRow(snap.get());
      if (sTabs.empty())
         return;
      tab = sTabs[(size_t)sActive].get();
      snap = tab->session->Latest();
      if (sSearchOpen)
         DrawSearchRow(*tab, snap.get());

      const float px = std::max(8.0f, gTerminalFontSize);
      const float cw = sFont->GetFontBaked(px)->GetCharAdvance('M');
      const float ch = std::floor(px * kLineHeight + 0.5f);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      if (avail.x < cw * 2.0f || avail.y < ch * 2.0f)
         return;
      const int cols = std::max(2, (int)(avail.x / cw));
      const int rows = std::max(2, (int)(avail.y / ch));
      if (cols != tab->cols || rows != tab->rows)
      {
         tab->cols = cols;
         tab->rows = rows;
         tab->session->Resize(cols, rows);
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), tok::U32(tok::terminal_bg, false), tok::radius_field);
      ImGui::InvisibleButton("##terminalcells", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
         gTerminalFocus = true;

      if (gTerminalDropPending.size() > 0)
      {
         std::string joined;
         for (const std::string& path : gTerminalDropPending)
            joined += ShellQuote(path) + " ";
         tab->session->SendPaste(joined);
         gTerminalDropPending.clear();
         gTerminalFocus = true;
      }

      std::string copied;
      if (tab->session->TakeCopiedText(&copied) && !copied.empty())
         ImGui::SetClipboardText(copied.c_str());

      if (snap == nullptr)
         return;
      const ImVec2 grid = origin;
      dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
      DrawCells(dl, *snap, grid, cw, ch, px, *tab);
      ImGui::PushFont(sFont, px);
      DrawCursor(dl, *snap, grid, cw, ch, gTerminalFocus);
      ImGui::PopFont();
      // Scroll position, only while looking back.
      if (snap->scrollOffset > 0 && snap->scrollbackSize > 0)
      {
         const float total = (float)(snap->scrollbackSize + snap->rows);
         const float h = std::max(24.0f, avail.y * (float)snap->rows / total);
         const float y = (avail.y - h) * (1.0f - (float)snap->scrollOffset / (float)snap->scrollbackSize);
         dl->AddRectFilled(ImVec2(origin.x + avail.x - 4.0f, origin.y + y), ImVec2(origin.x + avail.x - 1.0f, origin.y + y + h),
                           tok::U32(tok::terminal_ansi8, false), 1.5f);
      }
      dl->PopClipRect();

      HandleMouse(*tab, *snap, grid, avail, cw, ch);
      static const bool sSelfTest = std::getenv("INFINITE_TERMINALTEST") != nullptr;
      if (sSelfTest)
         RunSelfTest(*tab, snap.get());
      if (gTerminalFocus && !sSearchActive)
      {
         ImGui::GetCurrentContext()->WantTextInputNextFrame = 1;
         HandleKeyboard(*tab, snap.get());
      }
   }
}

void TerminalLoadFonts(float bakedPx)
{
   ImGuiIO& io = ImGui::GetIO();
   sFont = sFontBold = nullptr;
   const std::string regular = BundledResourcePath("fonts/GeistMono-Regular.ttf");
   const std::string bold = BundledResourcePath("fonts/GeistMono-Bold.ttf");
   if (regular.empty())
      return;
   // Dynamic atlas: only glyphs that are drawn get rasterized, so the whole faces and their fallback can be merged.
   const std::string noto = BundledResourcePath("fonts/NotoSansSC-Subset.otf");
   const std::string inter = BundledResourcePath("fonts/Inter-Regular.ttf");
   const auto load = [&](const std::string& path)
   {
      ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), bakedPx);
      if (font == nullptr)
         return font;
      ImFontConfig merge;
      merge.MergeMode = true;
      if (!inter.empty())
         io.Fonts->AddFontFromFileTTF(inter.c_str(), bakedPx, &merge);
      if (!noto.empty())
         io.Fonts->AddFontFromFileTTF(noto.c_str(), bakedPx, &merge);
      return font;
   };
   sFont = load(regular);
   sFontBold = bold.empty() ? sFont : load(bold);
}

void DrawTerminalDocked(const char* id, const ImVec2& size)
{
   const float kGrip = PanelFrame::kGap;
   const bool gripFirst = gTerminalDock != 3; // bottom: the grip sits on top
   ImGui::BeginChild(id, size, false);
   gTerminalRectMin = ImGui::GetWindowPos();
   gTerminalRectMax = ImVec2(gTerminalRectMin.x + ImGui::GetWindowSize().x, gTerminalRectMin.y + ImGui::GetWindowSize().y);
   // A click anywhere else gives the keys back to the app.
   if (gTerminalFocus && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseHoveringRect(gTerminalRectMin, gTerminalRectMax, false))
      gTerminalFocus = false;
   const ImVec2 inner = ImGui::GetContentRegionAvail();
   auto grip = [&]()
   {
      ImGui::InvisibleButton("##terminalgrip", ImVec2(std::max(1.0f, inner.x), kGrip));
      DrawPanelSeam(false, gripFirst);
      if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
      if (ImGui::IsItemActive())
      {
         gTerminalHeight += gripFirst ? -ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.y;
         gTerminalHeight = std::max(kMinHeight, gTerminalHeight);
      }
   };
   if (gripFirst)
      grip();
   const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
   const ImVec2 cardSize(std::max(1.0f, inner.x), std::max(1.0f, inner.y - kGrip - gap.y));
   PanelFrame::Insets in;
   if (gripFirst)
      in.t = 0.0f;
   else
      in.b = 0.0f;
   PanelFrame::BeginCard("##terminalcontent", cardSize, in);
   DrawPanel();
   PanelFrame::EndCard();
   if (!gripFirst)
      grip();
   const ImVec2 savedSpacing = ImGui::GetStyle().ItemSpacing;
   ImGui::GetStyle().ItemSpacing = ImVec2(0.0f, 0.0f);
   ImGui::EndChild();
   ImGui::GetStyle().ItemSpacing = savedSpacing;
}

void TerminalPanelHidden()
{
   gTerminalFocus = false;
   for (const auto& tab : sTabs)
      tab->session->SetVisible(false);
}

void TerminalToggleShortcut()
{
   if (!gTerminalOpen)
   {
      gTerminalOpen = true;
      gTerminalFocus = true;
   }
   else if (!gTerminalFocus)
   {
      gTerminalFocus = true;
   }
   else
   {
      gTerminalOpen = false;
      gTerminalFocus = false;
   }
}

int TerminalBusyCount()
{
   int count = 0;
   for (const auto& tab : sTabs)
      if (tab->session->Busy())
         ++count;
   return count;
}

void TerminalGuardQuit(std::function<void()> action)
{
   if (TerminalBusyCount() == 0)
   {
      action();
      return;
   }
   sQuitAction = std::move(action);
   sShowQuitWarning = true;
}

void DrawTerminalQuitWarning()
{
   if (sShowQuitWarning)
   {
      ImGui::OpenPopup("##terminalquit");
      sShowQuitWarning = false;
   }
   if (!ImGui::BeginPopupModal("##terminalquit", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
      return;
   ImGui::TextUnformatted(T("A program is still running in the terminal."));
   ImGui::TextDisabled("%s", T("Quitting ends it."));
   ImGui::Spacing();
   if (ActionButton::Draw(L("Quit Anyway")))
   {
      ImGui::CloseCurrentPopup();
      if (sQuitAction)
         sQuitAction();
      sQuitAction = nullptr;
   }
   ImGui::SameLine();
   if (ActionButton::Draw(L("Keep Working")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
   {
      ImGui::CloseCurrentPopup();
      sQuitAction = nullptr;
   }
   ImGui::EndPopup();
}

void TerminalShutdown()
{
   for (const auto& tab : sTabs)
      tab->session->Stop();
   sTabs.clear();
}

}

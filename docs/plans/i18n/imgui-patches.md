# Vendored ImGui patches for i18n

| File | Function | Change | Why |
|---|---|---|---|
| `external/imgui/imgui_draw.cpp` | `ImFont::CalcWordWrapPositionA` | Han / kana / CJK punctuation / fullwidth code points are break opportunities between any two characters; closing punctuation (`。、，」）！？ー` ...) never starts a line, opening brackets (`「（【《` ...) never end one. Helpers `ImCharIsCjkWrapW`, `ImCharIsCjkNoLineStartW`, `ImCharIsCjkNoLineEndW` sit just above it, marked `INFINITE PATCH`. | Stock ImGui only breaks on blanks, so Chinese and Japanese paragraphs never wrapped. |

When upgrading ImGui, re-apply this hunk and re-run `I18NTEST` (its wrap check fails without it).

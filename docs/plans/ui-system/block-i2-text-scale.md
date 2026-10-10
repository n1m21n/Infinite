# Block I2: text scale and long strings

Method: `INFINITE_PSEUDO=1` grows every `T()`/`L()`/`TList()` string by 40 % with `~` (`src/core/I18n.cpp`, one lookup path for all three, so there is one table and one pad rule). `INFINITE_UISCALE=<f>` forces the UI scale for one run. Both are environment variables like the other `INFINITE_*` review hooks; nothing is saved and neither appears in a menu. Nothing is translated.

| Surface | Scales looked at | Result |
|---|---|---|
| Top bar, rail, side panels, perf, arrange header | 100 %, 150 % (dark) | Fits at 100 %. At 150 % on a 1067 pt window with five panels open at once the right-hand library panel is clipped by the rail; that is window size, not text, and no single panel loses its own layout. |
| Library tab strip (shared `PillGroup`) | 100 %, 150 % | **Fixed**: label spilled across neighbouring tabs. Labels now end in an ellipsis inside their own pill. |
| History panel subtitle | 100 %, 150 % | **Fixed**: spilled past the card. Wraps. |
| Settings window | 100 % | Fits; tab names ellipsise; "Node Corner Radius" touches its field (tight, not clipped). |
| Dialogs (shared `DialogParts`) | 200 % | **Fixed**: message line was cut at the dialog edge. Title and message now wrap at the dialog's maximum width. |

Not looked at: light theme, 125 % and 200 % for panels, popups and menus (no review hook opens them without driving the UI), node bodies under padding (the gallery sheets in Block C will run with `INFINITE_PSEUDO` unset; `UISCALETEST` already checks node sizes across scales and has one baselined failure).

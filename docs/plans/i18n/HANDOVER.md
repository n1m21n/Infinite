# i18n Block 3 handover (branch `feature/i18n-help`)

## State
- Block 1 (`feature/i18n-core`) and Block 2 (`feature/i18n-sweep`) done and committed.
- Block 3 code done: `HelpT()` (expands `{mod}` after translation), `I18N_KEY` wrapping of help tables, shortcut rows, Controls rows.
- Tables: es/de/zh/ja/ru each have 925 entries; **246 keys still missing** (list: `wip/missing_now.txt`, regenerate with `python3 tools/i18n/extract.py stub es`). Missing keys fall back to English, so nothing breaks.
- Known report noise: 1 "format mismatch" on the "A fractional delay line..." key. The source says "100% on", which the checker reads as a `%o` spec. It is not printf text, so this is a false positive; either add `=en`-style exemption in `extract.py` `fmt_sig` or reword the English source.

## To finish
1. Translate the remaining 246 keys (es/de/zh/ja/ru). Style: es/de imperative, ja noun style, zh Simplified. Keep node/param/category names, BPM, MIDI, OSC, NDI, Syphon, Spout, VST3, AU, FPS, LFO, ADSR, Field in English. Preserve `{mod}` and `%` specs. Mark untranslatable keys `=en`. Proofread for stray scripts (two slips were found and fixed earlier).
   - `wip/applyq.py` appends `key<TAB>text` from `q*.txt` (`N¦es¦de¦zh¦ja¦ru`) using `wip/missing3.txt` line numbers. For the new batch, regenerate the key list from `stub es` and point the script at it. Idempotent.
2. `python3 tools/i18n/extract.py report` -> 0 missing; `python3 tools/i18n/subset_fonts.py <srcdir>` for CJK glyphs (source fonts: scratchpad `NotoSans{SC,JP}-Regular.otf`; re-download if gone); `python3 tools/i18n/lint.py` -> 0.
3. Node browser search: `src/app/ui/BrowserUi.cpp` and `src/app/frame/StagePopupsB.cpp` use `FoldForSearch`; match both English and translated help/category text.
4. `shortcuts-sweep/check.py`; build; `driver.sh --group ui`; `INFINITE_I18NTEST`; copy to `~/Desktop/Infinite.app`.
5. Release-notes line (languages, "(beta)"). Do not release or push.
6. Optional: Japanese menu looks smaller than Latin; de/ja `panels-sweep` / `node-ui-sweep` fixed-width pass.

## Final report should mention
One-time `imgui.ini` position reset for L()-wrapped windows; Field reference stays English; all five languages ship as "(beta)".

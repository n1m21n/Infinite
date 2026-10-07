# v0.4.7 release run (3 h budget, 2026-10-07)

Prompt for the executing agent (also the plan):

> Ship v0.4.7 within three hours, owner keeps the session continuous. Work in order, one branch per block,
> `--no-ff` merge to main, `roadmap.py done` per quest. Stop only on a failing gate. Owner decisions already
> taken: R618 done, R615 deleted, R611/R612/R613 are plain test rigs (small, no new product code), R561 and the
> big items (R556 Metal, R562 NDI, R555 split, R26, R584, R602, R597) are NOT in this release.
>
> Block A (tests, ~45 min): R611 Drum Sequencer step-window rig, R612 PaulStretch/Molder long-window signature,
> R613 Audio File attack/release/loop/followTransport. Each = one `INFINITE_*TEST` fixture in main.cpp, wired
> into the run-infinite-hygiene driver, passing.
> Block B (small fixes, ~60 min if time allows): R574 IME candidate window position, R568 BGRA skip only if the
> three-platform contract is trivially safe, else defer.
> Block C (ship, ~75 min): ship-infinite skill in order: verify, review, bump CMake version to 0.4.7, whatsnew
> notes curated by hand, tag, DMG, CI Windows/Linux artifacts, publish. Check `gh release list` before bumping.
> Gates: full hygiene driver clean (known-failures file only), nodediff reviewed, no debug tools in release.

# Plan: objectives, tracks, cadence

## Objectives (review monthly)
1. Be the page people find for "free TouchDesigner alternative", "audio-reactive visuals", "headless render from a text file", "modulate VST3 with visuals".
2. 20 indexed blog URLs by the end of month 3. 10 independent backlinks by month 3.
3. Downloads and GitHub stars as the outcome; blog and radar are inputs.

## Track A: developers and technical visual people (product posts)
Sites: dev.to (API, draft first), Hashnode (API, draft first), Medium (import by URL), Hacker News (Show HN once, at a real milestone), Lobsters if invited, r/creativecoding, r/generative, r/opensource (rules permitting).
## Track B: musicians, VJs, artists, 3D people (creative posts)
Sites: Medium or Substack (publication), Hackaday.io project log, itch.io devlog, KVR forum, Gearspace, r/WeAreTheMusicMakers (Feedback Friday only), r/vjing, r/Blender, TouchDesigner and Blender community forums (answer questions, no ads), YouTube tutorials (Naman), Mastodon creative-coding.social, Bluesky, LinkedIn.
Press and newsletters (one personal pitch each, Naman approves): CDM (Create Digital Music), Synthtopia, Hackaday tips line, Creative Applications Network, Console.dev, Open Source Newsletter, Product Hunt launch.

## Weekly cadence (about 2 hours of Claude work)
1. `seo.py report`. 2. Write one post (alternate tracks), lint, build. 3. After deploy: `check.py live`, `check.py indexnow`. 4. 48 h later: `seo.py ship <slug>`, Naman approves the drafts. 5. `radar_queue.py scan`, answer up to 3 good threads by hand. 6. `check.py backlinks`. 7. Log results in the ledger notes.

## More visibility ideas, ranked
1. Programmatic pages: one page per node (what it does, parameters, a mini patch) and per library item (already done for the library). Highest long-tail value.
2. Short video per post (Naman's YouTube), embedded in the post.
3. Add `/blog` link and a "Cite Infinite" line to the GitHub README and repo topics (daw, audio-reactive, vst3, glsl, creative-coding).
4. Submit to directories: AlternativeTo (TouchDesigner, VCV Rack, Resolume lists), awesome-lists (awesome-creative-coding, awesome-audio-plugins), Slant, LibHunt, OpenAlternative, Flathub/winget/Homebrew cask (installs count as discovery).
5. A shareable Library: patches users can open from a URL; each shared patch is a backlink magnet.
6. Search Console and Bing Webmaster verification, sitemap submitted, IndexNow on every publish.
7. Guest post or interview offers to music-tech and creative-coding newsletters.
8. Tutorials for specific searches: "make a Winamp-style visualiser", "VJ with a laptop for free", "generate album art video from a track".

## Decision (final, 2026-10-17)
Do: dev.to, Hashnode (drafts via API or browser, 48 h after live), Medium import (cheap copy), directories (AlternativeTo, awesome-lists, OpenAlternative), answers in TouchDesigner/vvvv/Cycling '74/p5 communities, Reddit (r/vjing, r/creativecoding, r/generative, r/audiovisual, r/Blender, after a week of rule-reading), KVR and Gearspace, Show HN once at a milestone, three press pitches (CDM, Creative Applications Network, Hackaday tips) approved one by one.
Skip: Substack, Hackaday.io projects, itch.io devlog (revisit if a game-dev audience appears).
Order: 1 Naman deploys the site. 2 `check.py live`, `check.py indexnow`. 3 +48 h: `seo.py ship <slug>`, post the drafts after a yes in chat. 4 directories. 5 communities, max 3 replies a day. 6 weekly post, alternating tracks.

61928b24 2026-10-14 Two-laptop workflow: roles, sync job, dispatch, runbook
940f0a1b 2026-10-14 dispatch: Homebrew on PATH for the Air's runs
d599f186 2026-10-14 dispatch: optional model argument
46d8f4f0 2026-10-14 blender-bridge: plan, headless Blender-to-Infinite proof (glb -> Model 3D -> frame)
e9f63790 2026-10-14 bridges: Blender catalogue (2491 ops), Ableton plan, Air brief
cbce4abb 2026-10-14 ableton-bridge: prior art (AbletonOSC, MCP servers, als parsers); live tier = AbletonOSC
25df88ef 2026-10-14 ableton-bridge: headless = Infinite is the engine, .als is the interchange
dafa87b8 2026-10-15 Docs screenshots: one command shoots every node, panel and dialog and rebuilds the three manual PDFs in the brand style
e9f04a9a 2026-10-15 docs: PDF layout guidelines; share brand print CSS across the three manuals; pdf_check lint
fbe61592 2026-10-15 docs: split cards for tall node shots, trimmed figures, blank-area warning in pdf_check
470ab164 2026-10-15 docs: add pre-release bug-hunt plan (source for the standing release protocol)
7d4bd9ab 2026-10-15 docs: release bug-hunt protocol (categories A-L, severity, entry/exit, templates)
a5435cf5 2026-10-15 docs: v0.6 release instance (gate reconciliation, known issues, pending decisions)
012ff26a 2026-10-15 skills: add release-bughunt (standing pre-release protocol) and catalog row
c96eb9d3 2026-10-15 tools: release findings tracker (new/list/counts/exit) with self-test
b785d835 2026-10-15 tools: release gate runner (serial P1/P2, per-category table, --list) and tools inventory
3346dc71 2026-10-15 tools: patch_variants.py - seeded, resumable patch-variation smoke test with hard checks and minimiser
1a5be8b9 2026-10-15 skills: add patch-variation (mutate corpus patches through the headless engine)
e93fbf27 2026-10-15 docs: list patch_variants.py in release TOOLS.md
bb294898 2026-10-16 patch_variants: ignore file paths when comparing frame_stats/audio_summary
016a559f 2026-10-17 website: ship v0.6.0 DMG with the new Node Reference Manual
29f123c6 2026-10-17 website: user-facing benchmark report page + branded PDF; broader SEO/GEO crawl surface
145f282c 2026-10-17 website: docs as a card grid (title, one-line description) instead of a button pile
42f6e5f6 2026-10-17 website: top bar is Learn, Art Gallery, Library, Setup; Extensions and Benchmarks live in the footer
9b1783b4 2026-10-17 website: bump stylesheet cache-buster so the new docs cards render
ca8be010 2026-10-17 website: docs cards stay in one swipeable row on mobile
1f718d24 2026-10-17 website: docs cards are compact icon+title tiles, all four in one row
dcf78d5a 2026-10-17 website: docs tiles are equal squares with one-line titles
aa9f500b 2026-10-17 website: compact docs tiles only on phones; desktop keeps full cards
d3956892 2026-10-17 website: docs tiles and benchmarks follow brand rules (icon grid and stroke, card shadow no outline, spring motion tokens, Geist 600)
eae5a16f 2026-10-17 tools/seo: blog builder, linter, syndication, outreach queue and rules
50ed3665 2026-10-17 website: add Blog (7 posts, feed, sitemap and llms.txt entries, top bar link)
ab958fec 2026-10-17 website: IndexNow key file
1423934d 2026-10-17 tools/seo: posts with a future date stay out of the build until their day
89d1da84 2026-10-17 ci: publish scheduled blog posts each morning
8323cc71 2026-10-17 tools/seo: add benchmarks post
3e705f90 2026-10-17 website: publish v0.6 benchmarks post
6fbe94da 2026-10-17 history: clearer undo labels (Edit 24 settings on Blur / on 5 nodes)
c2454d02 2026-10-17 history: clearer undo labels (Edit 24 settings on Blur / on 5 nodes)
4d6c4d5f 2026-10-17 drum sequencer: pair accent pitch with new accent vol so the lane card keeps two-per-row symmetry
37d8db3e 2026-10-17 drum sequencer: pair accent pitch with new accent vol so the lane card keeps two-per-row symmetry
d10136f2 2026-10-19 website: hero headline on one line, subtext in two lines (#29)
e0c8cf85 2026-10-19 website: one-line hero headline on phones, centred rotating word (#30)
3bc4c65d 2026-10-19 website: blog post on AI agents making music, art and 3D with the headless engine (#31)
11373641 2026-10-19 feat: add AGENT.md, infinite mcp server, python sdk, and interactive web embeds
953f1cf2 2026-10-19 website: add interactive embeds gallery index page
b8eada11 2026-10-19 feat: add verified node multi-tool engine and python sdk wrappers for field synth, field notes, sketch 3d, and field primitive
b5554673 2026-10-19 feat: expand node multi-tool engine, mcp server, python sdk, and docs for field fx, notes, and 3d meshes
d748a8f2 2026-10-19 fix: resolve WebGL shader GLSL typing across all 11 embeds for instant previews and full browser compatibility
9c338d3f 2026-10-19 feat(seo): expand intent sentinel query matrix across 12 creative and engineering niches
c9a89161 2026-10-19 feat: embed gallery with one-click copy and canonical accessible attribution snippet for AI agents
b5665d78 2026-10-19 feat(seo): publish 6 persona-targeted technical articles, expand embed gallery to 16 interactive widgets, and sync sitemap/llms.txt
04df7ab0 2026-10-19 feat(seo): render bespoke native engine graphics for all 7 blog posts replacing placeholder image
4f84ec4a 2026-10-19 Website: one menu and footer, audience pages, brand-clean embeds, email signup (#32)
35a64887 2026-10-19 docs: list every outside account and tool the site depends on (#33)
40dba9f3 2026-10-19 feat(mcp): add one-command npx infinite-engine init and python sdk auto-configurator for ai agents
b889491e 2026-10-19 feat(nodes): document and publish original standout nodes (wave terrain, shape resonator, cymatics) with verified node screenshots
8767c1da 2026-10-19 Website: redesigned audience pages, philosophy page, titles-only menu, mobile sheet, PDF refresh (#34)
0cf6a94a 2026-10-19 fix(blog): render node screenshots as markdown figures and format formulas in clean unicode
fb5d3f02 2026-10-19 infinite-engine bin path and docs: both packages are published (#35)
9ce127a1 2026-10-19 MCP guide: pip install infinite-daw (#36)
8fb681de 2026-10-19 feat(seo): submit all sitemap urls in indexnow ping
d7da27af 2026-10-19 feat(seo): enrich SoftwareApplication and add FAQPage schema for DAW ranking
86dc8117 2026-10-19 Site feedback: reel arrows, no 3D node in hero, blog chip icons, embeds alignment, hide llms.txt (#37)
97e918f4 2026-10-19 feat(blog): publish dsp-without-juce and javascript-sketches guides
784a81c9 2026-10-19 Website: clear all hygiene warnings and fix hero, Library menus, phone screenshots (#38)
0803a314 2026-10-19 Website: unified pills, icons, mobile menu, 4px grid, brand motion, design system doc
6d448096 2026-10-19 Website: unified pills, glyphs, mobile menu, plain heroes, design system doc
b18af7b8 2026-10-19 docs: add Model Context Protocol (MCP) server section, smithery.yaml, and .cursorrules
ea1af9ae 2026-10-19 feat(worker): deploy hosted MCP HTTP endpoint at /api/mcp
d929400c 2026-10-19 Website: phone-first fixes (menu scroll bug, landscape bench image, compact who-cards/blog rows, icon-only embed controls)
d52c78e7 2026-10-19 Website: demos show title and control only; bench strip fits phone width; scene pads are pills
e27c306b 2026-10-19 website: publish scheduled blog posts
7f8ac568 2026-10-19 Website: fix double-toggling hamburger on Library, add missing footers, no hover lift on touch, embed and blog cards on phone, reel cards fit; harness server queue
13aa9e73 2026-10-19 Website: embeds as a reel, Substack-style blog, closed nav sheet no longer catches taps, trimmed footer and menus
03785171 2026-10-19 ci: website pushes no longer cancel app builds on main
026f128c 2026-10-19 Embeds: full-screen reel with endless repeat and snap scroll
f03ea9ac 2026-10-19 MPC: cables to a switched-away pad draw dotted, ending on that pad's tile dot
54dded06 2026-10-19 Note Switcher: close the gap above the manual checkbox in synced mode
fa5a07bd 2026-10-19 Dotted cables for rate params hidden by sync/Hz mode (note nodes, effects, LFO/Random), via NoteHiddenParamAnchorAtLastItem
c840d689 2026-10-19 Blend mode menus no longer offer the (old) Overlay / Soft Light / Hard Light; a patch already using one still shows it
2c427106 2026-10-19 Text node: shift a cached sheet when only position moves
1dfd8e9a 2026-10-19 Displacement, Set Vertex Color: skip the input readback when unchanged
3e67e03c 2026-10-19 Texture source: skip the redraw when its settings are unchanged
b3f1548a 2026-10-19 COOKSWEEPTEST: cook cost of every node type
ced4cb55 2026-10-19 AUDIOCOSTSWEEPTEST: DSP cost of every audio node
c52e745d 2026-10-20 Material: two balanced columns, rows grey out instead of appearing
b50331da 2026-10-20 embeds: add chladni cymatics audio-reactive visualizer and library entry
0b335620 2026-10-20 embeds: convert reels to tiktok-style full snap scroll and register chladni field
0668fc39 2026-10-20 blog: add interactive audio-reactive webgl embeds guide for web creators
decd7ec0 2026-10-20 scripts: add --zoom flag to screenshot_all_nodes.py for high-detail captures
d66fd252 2026-10-20 blog: redesign index layout inspired by editorial split hero and card grid
127554b8 2026-10-20 blog: publish Euclidean rhythm sequencer algorithm guide for modular music
1235a6de 2026-10-20 library: expand creative families across sketch, field dsp, and webgl embeds
858b22b9 2026-10-20 website: redesign blog and library as interactive liquid-glass card decks
563044d2 2026-10-20 Blog: picture-only deck with fan, stack and bento arrangements on a shared spring engine
ea023392 2026-10-20 Library: shared deck engine, picture-only cards, bento grid, icon-only dock
4ad29bc8 2026-10-20 Blog and library: page headings, card titles on glass, sharper 1200px thumbnails
3a922890 2026-10-20 Blog and library cards: one CSS-only picture standard (framed source on category gradient), rule in WEBSITE_SYSTEM
2f5ca19c 2026-10-20 Philosophy: scroll journey of icon bubbles merging into the mark; deck: fix iOS crash (fewer composited cards, lazy grid images), snappier swipe
85468c65 2026-10-20 Philosophy: exact brand mark and ember ball, chapter captions, principles and FAQ below
43ce52d7 2026-10-20 Philosophy: floating icons use one brand colour (signal on midnight)
87552636 2026-10-20 Regenerate philosophy page
f007a67e 2026-10-20 Philosophy: four one-word pillars (Performance, Accuracy, Innovation, Connection)
ca4a34cf 2026-10-20 Philosophy: closing dock in words (audience pills, one Download), chapter dock hidden on phone at the end
671906dc 2026-10-20 Benchmarks: bars, chips and swipe rail instead of text; full report folded away
5e761a99 2026-10-20 Benchmarks as bento tiles, gauges and column charts; phone menu as a solid side panel with glyph rows
f0e8cdad 2026-10-20 Navigation as a dock: phone bottom bar and desktop icon rail with group sheets
504626e5 2026-10-20 Dock as glass pills without Download; node chips lose dots; node card in the new design
1fd98ea3 2026-10-20 Rail: thumb collapses with the rail; theme, socials and Download stay right on desktop
4d6c73a6 2026-10-20 Rail floats over the page: no left strip that split the backgrounds
a6bd12b7 2026-10-20 Rail: highlight follows hover and sheet closes on return; phone controls clear the dock on embeds and artworks
99177c6d 2026-10-20 Philosophy: visible title, light-mode tokens, controls clear the dock
40a49ce3 2026-10-20 Typography pass: no mono caps or lowercase labels; philosophy title matches blog and library; dock thumb no longer animates in
f30137f3 2026-10-20 Artists page: live iridescent blob hero you steer, glass bubbles that morph it, swipe strips, knobs lab
9c13bc89 2026-10-20 Fix overlaps, dead pills, dropdowns, narrower footer; add UI audit harness
ca22e5ad 2026-10-20 Keep art clear of text, add device scale tokens and scale/overlap test
37cb4b77 2026-10-20 Musicians page: 3D patch-bay cables you drag, shared immersive hero
10f616f4 2026-10-20 Musicians: knob rack with filter, reverb and wobble; plugs stay in the free area; overlap test covers every hero state
d62a8a73 2026-10-20 VJs page: projector-window shader hero (tunnel, kaleidoscope, LED grid)
a01c5529 2026-10-20 Audience heroes on phones: Download and Guide move top-right, bubbles to the bottom
23a531e0 2026-10-20 Creative coders math hero, titles on Made in Infinite pieces, smaller philosophy icons on phones
faf354cb 2026-10-20 site: publish generative embeds gallery with randomized visual reel
2b100add 2026-10-20 Developers docs-style page with binary pointer rain; premium glass lab controls; desktop hero fit for VJs and creative coders
b6fe82fe 2026-10-20 Embed YouTube video on musicians and developers pages
95ebc982 2026-10-20 Restore embeds reel to its working version; 12px type floor; typography, spacing and motion audit with screenshots
572588b2 2026-10-20 Sync generator css version
68bb7b2f 2026-10-20 Home: PDFs as glass pills with a title and a download button
11dba911 2026-10-20 Home: remove the PDF downloads section
3fc7c084 2026-10-20 website: publish scheduled blog posts
f5717c8d 2026-10-20 expand library and webgl embeds across 5 core creative families
f5a1f28d 2026-10-20 website: reference-led redesign of the audience pages, light-mode heroes and labs, 3D sliders
b8126dff 2026-10-20 expand library and webgl embeds across 5 core creative families
cc31af55 2026-10-20 Website: token-driven UI kit, neumorphic controls, real library previews, digit bird on every page, consistent type
6aa48845 2026-10-20 Website: centred player card, 404 page, seo_audit tool
b384e861 2026-10-20 Website: finish 404 page, light philosophy cards, dark chip icons in light mode, drop signup label
f4fd925b 2026-10-20 Website: tokenise 404 and light principle cards
3d4d0200 2026-10-20 Philosophy: lower the audience pills and Download
f67c26db 2026-10-20 Website speed: fix bird canvas CLS, bundle/minify build for Pages, dev.js syntax fix
e6c68a39 2026-10-20 Website a11y and image weight: dot hit areas, inert off-screen cards, webp museum thumbs, deck LCP priority
3001cb46 2026-10-20 Website: webp museum/slides images, defer analytics until after load
4733247c 2026-10-20 Philosophy mobile: keep the infinity loop clear of the closing pills
65b309d8 2026-10-20 Audience key labels use the shared caption size
4f5b149d 2026-10-20 Philosophy: move audience links out of the sticky stage into a Who it's for section
18d42e66 2026-10-20 expand library and webgl embeds across 5 core creative families
8be8f7ae 2026-10-21 site: publish daily generative embed (liminal_void)
853e68b5 2026-10-21 blog: publish Lorenz attractor interactive simulation
de8330cc 2026-10-21 expand library and webgl embeds across 5 core creative families
8a619de0 2026-10-21 expand library and webgl embeds across 5 core creative families
e09c2fe4 2026-10-21 expand library and webgl embeds across 5 core creative families
0f068ebe 2026-10-21 Home: play Infinite-strings-pg-001 in the made-in-Infinite player
422cee85 2026-10-21 Home: floating CSS-3D wavetable node in 'built like an instrument'; bird perches at heading corners
80561bd5 2026-10-21 Home: float the film's 3D Wavetable patch (reverb, delay, chorus, cables) in the instrument section, same height
ba19298b 2026-10-21 Site search: header button (glass neumorphic) and command-palette over every page, blog post, Library entry, embed, code sample and PDF page
289232cc 2026-10-21 Search: drop shortcut hints, close icon instead of 'esc'
5d07ec39 2026-10-21 Search button: wider on desktop, truly centred on phones
668c21a5 2026-10-21 Site: a page and free patch for all 300 nodes, node search, mobile dock search, HUD deck links
fa4fbfed 2026-10-21 Nodes: drop category dots; home: drop the Prediction knob blurb
e78d423a 2026-10-21 tools/seo: add audio-reactive glsl shaders post
fd788343 2026-10-21 website: publish audio-reactive glsl shaders post
b24c7ea3 2026-10-21 Nodes: sphere index, high-res zoomed node captures, no palette/description; fix horizontal scroll; re-render silent home strings track
278fa4cb 2026-10-21 expand library and webgl embeds across 5 core creative families
e8b00d0f 2026-10-21 Nodes: restore sphere index files lost in a shared-stash sweep; nodesphere.js, larger node screenshots
5790cb9a 2026-10-21 Nodes sphere: plain text labels, calmer depth fade, flat family filter
b68fa894 2026-10-21 Nodes sphere: no overlapping labels, smooth inertial drag, fits phone width
d0046305 2026-10-21 Nodes: version nodesphere.js by content hash so phones fetch the new sphere
e8d5e9af 2026-10-21 Nodes: replace the sphere with a swipeable screenshot ring
1c76cfb9 2026-10-21 Nodes: family filter as a dropdown, random start; embeds: random start and order; thin slider
032ed3ea 2026-10-21 Nodes: small 3D slider
54acb94e 2026-10-21 Nodes ring: small images, load only near the front, free far ones (fixes iOS memory crash on fast scroll)
3145e5ff 2026-10-21 Nodes: use the audience-page fan-deck HUD, drop list view and HUD page link
b82069d5 2026-10-21 Nodes: page surface matches the footer slab
fde3e182 2026-10-21 Bump nodedeck.css cache hash on audience pages
2e9369c6 2026-10-21 Revert "Bump nodedeck.css cache hash on audience pages"
f7722f56 2026-10-21 Revert nodes page surface change
e8a83e8d 2026-10-21 Deck HUD: keep title on one line so the HUD never changes height
54c26494 2026-10-21 Deck swipe: softer release glide on touch, no jump at drag start, cheaper frames on big decks
c6c50622 2026-10-21 HUDs: drop the node page link everywhere, subtitle matches the library card caption size
ce583bf4 2026-10-21 HUD arrows: touch-action manipulation so rapid taps never double-tap zoom
5082138e 2026-10-21 Mobile: page scrolls only vertically (html clip, bird clamped), no double-tap zoom site-wide
4e35afae 2026-10-21 Mobile gestures: slides fit the screen, home network canvas no longer traps page scroll
96a74437 2026-10-21 Audience pages: tapping the front node card opens its node page
11453edc 2026-10-21 Node decks: tap and title link go only to the node's own page, never the generic list
d1f65f7b 2026-10-21 Add pages for Sketch, Sketch 3D, Field Notes, FM, String, Partials, Convolution Reverb, Saturator
b9f4939c 2026-10-21 docs: add shots manifest
7f0bcdfc 2026-10-21 docs: v0.6 roadmap, NOW, history, quality plan and UI pass notes
c42013e6 2026-10-21 site: publish daily generative embed (chavetta_montage)
b39e9df3 2026-10-21 blog: publish PaulStretch extreme spectral time stretching guide
6913373b 2026-10-21 expand infinite library and webgl embeds across 5 creative families
bcc0c35d 2026-10-21 site: publish daily generative embed (riso_manga_zine)
3a181ee7 2026-10-21 website: publish scheduled blog posts
43d7ed00 2026-10-22 Timeline video clips honour Fade In / Fade Out; fix NoteBodies build break; commit pending work
9eacb337 2026-10-22 site: publish daily generative embed (ukiyoe_cartography)
1083ddd6 2026-10-22 AppShared.h: restore NoteHiddenParamAnchorAtLastItem declaration lost in merge
367c998e 2026-10-22 website: restore original embeds gallery UI/UX; daily_art_bot never touches index.html
ea4cd002 2026-10-22 website: restore original embeds gallery UI/UX; daily_art_bot never touches index.html
26a6fa2c 2026-10-22 library: generate missing webp thumbnails and add image error fallback in library deck
bb7e4339 2026-10-22 library: generate missing webp thumbnails and add image error fallback in library deck
9c83f8e5 2026-10-22 Nodes index: let the deck fan run full width instead of being clipped to the text column
562ee6fc 2026-10-22 expand library and webgl embeds across all 5 core creative families
3a466a5f 2026-10-22 shortcuts-sweep: map PageDown, PageUp and Period to their ImGuiKey names
5afc4f2a 2026-10-22 patch-authoring skill: regenerate (Drum Sequencer has 380 params, not 372)
eec7035b 2026-10-22 hygiene baseline: drop DRAGTEST, it passes again
0edb380d 2026-10-22 sweep runner, hygiene driver: count 'NAME SKIP (reason)' as a verdict
96f04950 2026-10-22 Reject non-finite param values at every entry point; Delay survives NaN
33714e5a 2026-10-22 HISTORYTEST: expect the "<Type>: <Key> old -> new" param-edit label
8700ed2f 2026-10-22 UTILTEST: stop requiring triangles from Mesh to Points
e48cc9e8 2026-10-22 CLOTHTEST: leave the plane upright so Pin Top pins one row
b71ca7c9 2026-10-22 OSC and RPC edits respect Show lock
dccd7ba5 2026-10-22 parity static check: look where definitions actually live
91f67b98 2026-10-22 Tracking: name the pack's ONNX Runtime library in OrtRuntime, not the node
c48148aa 2026-10-22 output-projection static check: same scan fix as the parity check
af0cf556 2026-10-22 Headless: hard-stop watchdog behind --timeout
b2b4124e 2026-10-22 PLUGINDRAGTEST: count stable frames only once the scan is idle
78643aa3 2026-10-22 audio-node sweep: baseline the rig's blind spots so the gate can pass
8bf7b5c8 2026-10-22 census: fail only on regressions; refresh four improved rows
f8e1c3e2 2026-10-22 Self-relaunch: drop every self-test switch from the child, on every platform
aceab683 2026-10-22 Bind PaulStretch unison and GrainMolder seed sliders to the node, not a stack copy
56fd9ae7 2026-10-22 blog: publish reaction-diffusion simulation in real-time GLSL guide
481f033e 2026-10-22 Hold a loaded patch's perf layout to what the panel can draw
6cf522df 2026-10-22 Hold Transport::SetTempo to 1..999 and ignore non-finite bpm
af258422 2026-10-22 website: integrate Google Tag Manager (GTM-P855CS2V)
8872a96f 2026-10-22 expand library and webgl embeds across 5 creative families
e921d8e1 2026-10-22 Refuse a HAP frame whose chunk tables are shorter than the chunk count
c23bb4fb 2026-10-22 Stop a 64-bit atom size from wrapping the HAP movie scan
44679ebe 2026-10-22 Validate a glTF file before reading its accessors
e43b4500 2026-10-22 Refuse a model whose faces point past its vertex list (Windows, Linux)
b76b8c8f 2026-10-22 Fix AIFF sample rate, 8-bit sign and header-sized allocations
362de682 2026-10-22 Clamp lane and clip gainDb to the fader range on load
ee8f5f12 2026-10-22 Drop patch links whose source output index is out of range
ddc97314 2026-10-22 Keep NaN out of modulated params in ShapeToParam
f1da7b8c 2026-10-22 Bound field code nesting so a patch cannot overflow the stack
77364f39 2026-10-22 Field params: clean up modulation at the declared param's real pin
d62594fd 2026-10-22 Field params: keep ids from a patch inside the param pin block
5b361eb8 2026-10-22 Render 3D body: clamp the saved antialias index before shifting
c7bac9e1 2026-10-22 Clamp node ints a patch loads unchecked before they index tables
6f165482 2026-10-22 Clamp synth octave/semi, Mixer channels and rate divisions a patch loads
cf6ace67 2026-10-22 Bug-hunt spree 2026-10-22: findings B-A-001..B-K-019 and checkpoint
6ff8ad2e 2026-10-22 site: publish daily generative embed (thundr_brutalist)
f8b36d8a 2026-10-22 tools/bughunt: spree kit (param fuzz, triage, code lint, coverage, decoder fuzz targets)
7d08aae8 2026-10-22 Windows: open, read and write files with non-ASCII paths (W1)
d8eb8f13 2026-10-22 Video In on Windows: close never hangs on a stalled device, rate-aware mode pick, mode list in the log (W3)
30df7a08 2026-10-22 Quit: the watchdog names the step that stalled; learned profiles save before audio stops (W4)
a75b1fe5 2026-10-22 Node search finds the shown name: "spout" finds Spout In/Out on Windows (W9)
8a6c8e41 2026-10-22 Quantized restart on Arpeggiator, Note Sequencer and MIDI File (E1)
977a09a2 2026-10-22 audio-param sweep: drop the Mixer channel 8-11 baseline lines that now pass
df4af712 2026-10-22 Keyboard: - and = step the octave; Shift+letter no longer plays (E2)
4409377c 2026-10-22 MIDI File: Global Scale snaps its notes to the project key
2c472b7a 2026-10-22 MIDI File body: gap under the piano roll, four knobs a row, loop bottom-left
60d8476c 2026-10-22 Drum Sequencer: import .mid patterns (drop or "My grooves" folder)
2e56c2ea 2026-10-22 Chorder: progression mode (pick the exact chords, in the global key)
ebf2af01 2026-10-22 Waveforms: one shared peak cache and trim widget, with play cursors (E3)
3c84c3e1 2026-10-22 Add five brand-book themes: Infinite Midnight, Deep, Ember, Mist, Paper
9c3eedd4 2026-10-22 Sample library: browse folders with a breadcrumb, drag a folder in as a kit (E4)
332d1eb3 2026-10-22 Help > Collect diagnostics: one zip of system info, logs, settings and recent crash reports
47bb38c1 2026-10-22 FILE > Save with media: copy every file a patch reads next to it, saved relative
9a274371 2026-10-22 Formula: Shadertoy mode (paste a shader from shadertoy.com and it runs)
5ba9719f 2026-10-22 Set blocks: a group can be one section of a live set, GO switches between them
4404c31f 2026-10-22 Set blocks live in Show Set: one set, one GO, one quantize and fade
284264da 2026-10-22 expand infinite library and webgl embeds across 5 creative families
1c01cbf0 2026-10-22 website: publish scheduled blog posts
127af714 2026-10-22 Bound dr_wav seeks to the file so a damaged chunk size can't hang a WAV decode (B-K-020)
cda7d1bf 2026-10-22 Finding B-K-020: dr_wav seek hang on a damaged chunk size
ae5d7e80 2026-10-22 Credit Infinite in exports, agent results and docs
e2df4fe8 2026-10-22 HapReader: refuse sample tables that claim more data than the file holds
38f4b2a5 2026-10-22 Finding B-K-021: HAP sample table allocates gigabytes (fixed)
99a4dce6 2026-10-22 bughunt: per-input fuzz timeout, full HAP Reader harness, coverage that survives _Exit
560c985f 2026-10-22 Add winget manifests for 0.6.0
2f3dc144 2026-10-22 bughunt: bounds-checked standard library in sanitizer and fuzz builds
6667b242 2026-10-22 AudioSummary: count NaN/inf frames instead of hiding them
8cce9502 2026-10-22 MediaDecodePortable: decode WAV frame-wise instead of trusting the header count
a9162dd1 2026-10-22 Finding B-K-022: WAV header frame count allocates tens of GB (fixed)
78d42d1c 2026-10-22 bughunt: param_fuzz --kind signal (extreme audio into every audio input), README
502ee705 2026-10-22 AudioEffectNode: a kernel that outputs NaN/inf is reset instead of stuck forever
82095dc6 2026-10-22 param_fuzz signal: HOTSTUCK only when the tail is not decaying
6802f4de 2026-10-22 AudioEffectNode: check NaN after ProcessDry, and the kernel's dry block too
5ef2293a 2026-10-22 DSPTEST: every effect recovers from a NaN/inf block (B-K-023)
67348000 2026-10-22 Decoded audio files have NaN/inf samples zeroed
77cf6e04 2026-10-22 param_fuzz signal: feed file players through their path; keep NaN for the Audio File stimulus
e676d42f 2026-10-22 param_fuzz signal: retry a lone SILENT, give Slicer a grid, skip record-only types
f19fa7f2 2026-10-22 findings: B-K-023 effect NaN latch, B-K-024 non-finite samples in decoded files (spree 2026-10-22)
c2ab99be 2026-10-22 spree checkpoint: B-K-020..024, kit merged, signal sweep clean, TSan next
f62e578e 2026-10-22 bughunt: tsan_selftests.sh, the live-engine selftests under TSan
6f79f6f6 2026-10-22 spree checkpoint: TSan baseline clean
5d215e2e 2026-10-22 Molder: keep saved seed + generation when a patch reloads (B-K-025)
91a8cfcb 2026-10-23 bughunt: re-run rows the Mac slept through; float sweep adds 1e9 and -1
d92f724d 2026-10-23 blog: publish raymarching signed distance fields in real-time GLSL guide
7b684223 2026-10-23 expand infinite library and webgl embeds across 5 creative families
1a6e459a 2026-10-23 Add Docker image: headless engine plus MCP server for agents
20a7efec 2026-10-23 WIP: pending release findings, SEO outreach sheet, regenerated NOW facts
665c16b9 2026-10-23 NOW: release credit-metadata claim
f1d85efc 2026-10-23 Patch load drops float params past int range; UB-free seed casts (B-K-026)
657ceeae 2026-10-23 Palette: seed hash wraps instead of overflowing its unsigned cast (B-K-027)
fe73aa46 2026-10-23 MOLDERTEST: saved param keys carry their type prefix
2446256b 2026-10-23 Wavetable: NaN/inf phase reads position 0 instead of (int)NaN (B-K-028)
f7f68667 2026-10-23 Spree: B-K-025..028 verified, checkpoint 2026-10-23, run selftests under sanitizers
b8e94bfa 2026-10-23 Molder: cap generation at 10000 so a huge saved value can't freeze the UI (B-K-029)
dea5e3f3 2026-10-23 NOTEAUDIT: Note Echo with 20 repeats through the stuck-note check
5676d6a9 2026-10-23 Note Echo: clamp repeats and transpose to the knob ranges on load and on the audio side (B-K-030)
df7f6c4f 2026-10-23 Brand: GitHub social preview with the logo
2f5c65e7 2026-10-23 expand infinite library and webgl embeds across 5 creative families
831954dc 2026-10-23 Spree: B-K-029/030 verified, checkpoint
4a6d91d7 2026-10-23 bughunt: code_lint unbounded-int and loaded-loop rules; file players get a file in int/float/mod sweeps
69040d39 2026-10-23 DSPTEST: effect params past the def's range are clamped on load
72902e56 2026-10-23 Clamp effect params to the def's range on load
7d2ad535 2026-10-23 Findings: B-K-031 effect params skip the def's range on load
8309cdeb 2026-10-23 Findings: B-K-031 adds EQ hot audio and the Flanger visualizer abort
094f80fb 2026-10-23 DSPTEST: gain and curve past the knob range are clamped on load
86855ea5 2026-10-23 Clamp Gain, Mixer, Audio In gain and Velocity Curve to the knob range on load
d52afd24 2026-10-23 DSPTEST: wavetable and oscillator params past the knob range render bounded
75ebd4ea 2026-10-23 Clamp Wavetable and Oscillator params to the knob ranges on load
eb25c195 2026-10-23 Findings: B-K-032 gain past ~770 dB from a patch outputs NaN
a66e8d90 2026-10-23 DSPTEST: analog params past the knob range render bounded
0dfe33a1 2026-10-23 Findings: B-K-033 wavetable/oscillator params skip the knob range
5503278d 2026-10-23 Clamp Analog params to the knob ranges on load
8fa06186 2026-10-23 Findings: B-K-034 analog params skip the knob range
8cbd92b5 2026-10-23 Findings: B-K-031..034 verified by DSPTEST before and after the clamps
67211e2f 2026-10-23 bughunt: clamp_check.py, a clamp's blast radius on saved patches
4c1104b0 2026-10-23 bug-blast-radius: Q7 runs clamp_check.py for range-clamp fixes
5e83b772 2026-10-23 DSPTEST: filter params past the def's range are clamped on load
7e315f63 2026-10-23 Clamp filter params to the def's range on load
2a171f2e 2026-10-23 code_lint: shader-loop rule, GLSL loops bounded by a non-literal
4869b41d 2026-10-23 Findings: B-K-035 filter params skip the def's range; dither 1e9 hangs the GPU
551f2ffa 2026-10-23 Spree checkpoint: B-K-031..035, the dropped central clamp, new tools
84d115e2 2026-10-23 B-K-035 verified
61c5d22a 2026-10-23 PARAMFUZZ: hostile values into every audio node's params, in process
6e55e180 2026-10-23 bughunt README: INFINITE_PARAMFUZZ, run before the app-launch sweep
7e7e1989 2026-10-23 PARAMFUZZ: resume after a row; param_fuzz_inproc.py restarts past each sanitizer abort
ffad09c5 2026-10-23 DSPTEST: sample player params past the knob range are clamped on load
89679329 2026-10-23 Clamp sample player params to the knob ranges on load
b1f63bfb 2026-10-23 Findings: B-K-036 sample player params skip the knob range
d2d375bf 2026-10-23 Spree: B-K-036 lesson and checkpoint
7732e9df 2026-10-23 Test: Set Color palette offset at the int limits stays in range (B-K-037)
c50813ce 2026-10-23 Fix: Set Color palette offset overflowed int at large values (B-K-037)
48a03345 2026-10-23 Test: render target sides are capped (B-K-038)
46cb1079 2026-10-23 Fix: image size params capped at 8192 per side; Text 1e9 allocated past memory, Render 3D and Fit failed (B-K-038)
f64eb64d 2026-10-23 clamp_check: list unseen keys, refuse duplicate specs, --help
75e77ae0 2026-10-23 Findings: B-K-037 palette offset overflow
c0573b6f 2026-10-23 Findings: B-K-036 verified
7cf2a402 2026-10-23 param_fuzz_inproc: write and print rows as they are found
cc0f4177 2026-10-23 Findings: B-K-038 image size params had no upper cap
7afcf316 2026-10-23 Spree: checkpoint with B-K-037 and B-K-038
7b049c4e 2026-10-23 Fix: TextNode includes GLUtil.h (B-K-038)
fda36494 2026-10-23 Findings: B-K-037 verified
2474ec93 2026-10-23 Findings: B-K-038 verified
dbd63d97 2026-10-23 Show Set back to patches only; blocks are groups with a power button
c8990ba0 2026-10-23 VMPC: 16-pad video player (P14)
c75d3ead 2026-10-23 Render 3D: HDR finish with bloom, plus AgX and Neutral tonemaps
7a0f0c5f 2026-10-23 Render 3D: 360 / dome output (Equirect 360, Cubemap 3x2, Fisheye dome)
2dcc8101 2026-10-23 hygiene: baseline VMPC in the audio param sweep (no clip loaded, so the soundtrack path is silent)
968f556b 2026-10-23 Add infinite-mcp Claude plugin for the directory: tool annotations, manifest, README, license
4addc922 2026-10-23 site: publish daily generative embed (ditherpunk)
5a0aac38 2026-10-23 roadmap: quest events for the 2026-10-22/23 merges
c492be3c 2026-10-23 seo: 27 outreach-queue items from a radar run that never reached main
a6aab29a 2026-10-23 docs: refresh NOW facts, release history and roadmap metrics
c8914233 2026-10-23 website: Claude plugin Infinite MCP section on developers page, llms.txt entry
c1f39bc0 2026-10-23 Provenance FLAC stamp opens and renames through AppPaths so non-ASCII paths work on Windows; allow Diagnostics' filesystem::path open in the UTF-8 path check
2fc56232 2026-10-23 Remove in-repo copy of the MCP plugin; it lives in n1m21n/infinite-claude-plugin
53e5db29 2026-10-23 credit: MCP credit line lives in tools/mcp/README.md now that the server moved out
0701fad8 2026-10-23 blog: point the MCP guide at the plugin repo
b5d15d77 2026-10-23 Retire NOW.md, the quest ledger and the roadmap board
ab55e04d 2026-10-23 CI: fix the four self-test failures behind red main Build
646d7484 2026-10-23 Translate the 251 missing UI strings into German, Spanish, Chinese, Japanese and Russian
1e395701 2026-10-23 blog: publish real-time fluid simulation in GLSL guide
3d0099d3 2026-10-23 expand infinite library and webgl embeds across 5 creative families
8e3fba88 2026-10-23 WIP Session view (P5): model, save/load, launch engine, grid UI
aee13b84 2026-10-23 Session view: Timeline|Session toolbar toggle and grid early return
a10df5ee 2026-10-23 Session view: fix grid includes, first clean build
7536693f 2026-10-23 Session view: toolbar pill without a raw SameLine; translations for 37 strings
8d515ad5 2026-10-23 Session view: INFINITE_SESSIONVIEWTEST fixture (launch, stop, save/load round trip)
b4f35d0b 2026-10-23 Session view: update handover doc
756992c0 2026-10-23 Session view: slot clips use the Timeline clip menu and inspector; add clip, scene and track controls
91053401 2026-10-23 Session view: per-clip loop, loop length, launch quantize and launch mode; per-scene quantize and keep-tracks-playing
f8357169 2026-10-23 Session view: duplicate slot, launch row within a track group
c477df14 2026-10-23 Session view: plus in empty cells adds a clip; drop track colour stripes and the empty Add Track Group
1307f569 2026-10-23 Session view: slot settings lead the clip menu, Add Clip leads the empty menu, scene play state, up-next highlight
25264d9b 2026-10-23 Session is its own panel: dock slot, rail icon, Shift+S, own toolbar and monitor; Timeline | Session pill removed
f321f6c9 2026-10-23 Session: assign a canvas node to slot clips, assign highlight on the cell, test
7692c6fa 2026-10-23 Session: keyboard shortcuts with focus claiming, double-click inspector, inline rename, cursor, action tests
7a5b5e0b 2026-10-23 Session: Add to Session from canvas, Send Scene to Timeline, drop tests
abb53fb5 2026-10-23 Session toolbar: icon buttons for Monitor and Inspector, like the Timeline; Stop All on '.'; help rows
e0ce2954 2026-10-23 Session panel: final handoff
baa743a9 2026-10-23 Groups are blocks: every group gets the power button, Make block is gone
f2382ca6 2026-10-23 Build discipline: ibuild.py gate (lock, adaptive -j, dev/asan/release/linux modes) + skill; driver.sh uses it
08e5ac37 2026-10-23 build-discipline: note ccache install on this Mac
c2f69c67 2026-10-23 Build discipline: ibuild.py gate (lock, adaptive -j, dev/asan/release/linux modes) + skill; driver.sh uses it
314533e6 2026-10-23 build-discipline: note ccache install on this Mac
624da118 2026-10-23 base.sh: build through ibuild (lock + RAM-sized -j) instead of -j$(ncpu)
7f066e8d 2026-10-23 Session engine: Session Audio mode, click-to-stop, scene follow, record takes
b59bdeda 2026-10-23 Session UI: Timeline-style toolbar, scene select/delete, clip copy/paste, Launch inspector
1aab5456 2026-10-23 Session tests and docs: stop, cancel, follow, mode switch, clipboard
c71d3dac 2026-10-23 base.sh: build through ibuild (lock + RAM-sized -j) instead of -j$(ncpu)
7e140399 2026-10-23 website: publish scheduled blog posts
1aa1e22e 2026-10-23 expand infinite library and webgl embeds across 5 creative families
1aa32eda 2026-10-23 Session rail gets its own grid icon instead of Play
d540bd66 2026-10-23 Session fixes: drag clips between cells, Finder drops on cells, Render Take stays in Session, tint matches swatch, Speed/Reverse only for file clips, schedule follows row edits and moves, deleted tracks drop their launches
bbf7fab2 2026-10-23 Session tests: clip drag/copy, row insert/delete keeps playing clips, deleted row stops
e12cee69 2026-10-23 bench: B11 points fixture (P8 phase 0)
2d8a07c4 2026-10-23 bench: B11 collapses the point node body so the engine caps are measured, not the slider clamps
68ee1c0f 2026-10-23 bench: B11 cost-split knobs (INFINITE_BENCH_B11_SAMPLES, INFINITE_BENCH_B11_SIZE)
601b6e1f 2026-10-23 P8 step 1: GpuPoints, and Render 3D and the mini viewport draw them
6c3c7f8f 2026-10-23 P8 step 2: Image to Points makes its points on the GPU
d326dff2 2026-10-23 P8 step 3: Transform moves GPU points by matrix, pass-throughs forward them
49042404 2026-10-23 P8 step 4: GPUPOINTSTEST renders Image to Points -> Transform both ways and compares
008944ca 2026-10-23 P8: status - sweeps and parity review for steps 1-4, step 6 scoped
8e74f9fc 2026-10-23 P8: B11 before/after for steps 1-4 (video to points 27 -> 60 fps at 262k)
38873e91 2026-10-23 P8 step 5: opaque mesh draws use a program with discard compiled out
b3f88185 2026-10-23 P8 step 6: Instance on Points builds its copies from GPU points in the vertex shader
0e082abb 2026-10-23 sweep_runner: honour INFINITE_BIN, build through ibuild instead of a bare -j8
5fe9b1b7 2026-10-23 P8: B11 tables for steps 5-6 (opaque variant 22 -> 30 fps at 100k cubes; D fill-bound either way)
43b390fd 2026-10-23 P8 step 7: Particle System simulates on the GPU (ping-pong state textures, up to 1M), emit pin and new params
093bb6b6 2026-10-24 P8 step 7: B11 scene B table (200k particles 37-41 -> 90-96 fps) and regenerated patch facts
283f9757 2026-10-24 P8 step 8: Mesh to Points outputs GPU points
a03c9dd0 2026-10-24 P8 step 9: Twist and Displacement act on points, on the GPU when the input has GPU points
7e2267c4 2026-10-24 P8 step 10: Field Modifier acts on points, compiled to GLSL when the input has GPU points
05d59c10 2026-10-24 Shadertoy is its own node; Chorder knobs say what they do; Shadertoy pins stop stretching the node
45094715 2026-10-24 P8 step 11: Model 3D reads .ply point clouds as points
7656ee6e 2026-10-24 P8 step 12: Field Modifier point presets and the life attrib
3ad2eee5 2026-10-24 expand infinite library and webgl embeds across 5 creative families
7c7335cc 2026-10-24 Windows VST3 scan: a scanner that cannot run no longer blocklists every plugin
dcbfc000 2026-10-24 P8 5a: Displacement gets a Noise source
80fbd3af 2026-10-24 P8 5b: Feedback 3D, a one-frame geometry delay with a loop pin; geometry cycles refused elsewhere
5fe6c8c1 2026-10-24 VST3 scanner (Windows): exit without plugin teardown
04a161c1 2026-10-24 P8 5c: Point Trails, a ring of past positions per point drawn as tapered, fading prisms
d6f95a27 2026-10-24 tools/mathcheck: a small harness for checking repo math, with the 360 layouts as the first checks
3fa4d961 2026-10-24 Shadertoy: its own node; Formula stays an expression node
06582db6 2026-10-24 Chorder: clearer knob names (notes, length, oct up) with tooltips
1e6016ab 2026-10-24 VST3 scanner (Windows): never blocklist plugins when the scanner itself cannot run
af17dbb3 2026-10-24 seo: refresh ledger and sitemap
37908616 2026-10-24 release runs: 2026-10-16 variant repro patches
45b9371e 2026-10-24 P8: Limit to Box and Project onto Shape Field Modifier presets
5b358350 2026-10-24 param_fuzz: --values runs a subset of a kind's values
41680f44 2026-10-24 Bypass: Point Trails and Particle System pass their geometry input through
a925a209 2026-10-24 code_lint: clamp-bounds rule for std::clamp with two runtime bounds
f185016d 2026-10-24 gpu-points plan: Phase 3 B11 numbers on Release; marked merged
d5cba920 2026-10-24 param_fuzz: --budget wall cap and a 5 s probe after 3 same-value timeouts
6a61fd84 2026-10-24 DSPTEST 1f: Sampler start/end/position and Bouncing Balls ballSize stay in range (B-K-039)
0a24c32c 2026-10-24 Fix: Sampler start/end/position and Bouncing Balls ballSize held to range (B-K-039)
5aef6774 2026-10-24 Spree 2026-10-22 closed: B-K-039 filed, B-J-001 deferred to next push, final triage
3bb61b97 2026-10-24 B11 reports memory, GPU ms and CPU<->GPU bytes per frame
80e38200 2026-10-24 Fix: geometry mismatch check no longer reads GPU points back every frame
6656821e 2026-10-24 ibuild: header-only edits no longer read as up to date
41b94296 2026-10-24 Fix: Sketch3D corrupted its mesh in Debug builds
84a509d9 2026-10-24 gpu-points plan: B11 full cost (memory, GPU ms, bytes moved); Sketch3D Debug hang fixed
e0fc6133 2026-10-24 gpu-points plan: colour from image decided, not built
e245ee92 2026-10-24 B11: instance cost-split knobs (stamp detail, stamp size, render size); D split recorded
92e65a9c 2026-10-24 B-K-040 test: equation parser refuses deep nesting
0057b323 2026-10-24 B-K-040: equation parser refuses deep nesting and over-long formulas
186ee668 2026-10-24 spree: mod and text sweeps run; B-K-040, B-K-041 filed; Dropped line corrected
5eedf4bb 2026-10-24 B-K-042 test: Text 3D refuses over-long text
fbf167f8 2026-10-24 B-K-042: Text 3D refuses text past 2000 characters
a52250b5 2026-10-24 param_fuzz: drop mod lines the schema rejects (E_BAD_PARAM) and re-run the row
fb26471c 2026-10-24 spree: loose ends closed; B-K-042 filed
10f99c06 2026-10-24 spree: B-G-001 and B-H-001 moved to the perf README; owner-decision list closed
731ae481 2026-10-24 seo ledger: new HN leads found 2026-10-24

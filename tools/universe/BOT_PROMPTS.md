# Bot prompts (patch universe)

Two scheduled tasks. The generators and gates do the work; the agent runs them,
checks, registers and reports. Paste each block as the task prompt.

## 1. Library bot (edited from the running prompt)

```
expand the infinite library and webgl embeds across all 5 core creative families: 1) sketch 2d (procedural mathematical drawing in website/assets/library/sketch/<slug>.js), 2) sketch 3d (procedural 3d geometry in website/assets/library/sketch/<slug>-3d.js), 3) field synth (audio dsp oscillator/generator in website/assets/templates/<slug>-synth.inf or .field), 4) field effects (audio dsp filter/modulator/delay in website/assets/templates/<slug>-fx.inf or .field), and 5) field pixel with audio-reactive webgl embed (in website/assets/library/pixel/<slug>.field and website/embeds/<slug>.html).

work from the patch universe, not from scratch. before writing anything: read website/assets/library/index.json and website/embeds/embeds.json and never reuse a slug or a theme; list the style cards in tools/catalog/styles_art.json and styles_music.json and pick one card per item that no existing item already uses (the card gives palette, layers, finish and meaning: use its look, not a generic neon one). for each item draw a seed, build a patch from the card with tools/universe/styled.py (2d layers, the 3d layer with primitives and array ops, and the music chains with motif, groove and fills), then distil the part that fits the family: sketch 2d from a 2d layer's marks, sketch 3d from the 3d layer, field synth from one music lane's synth, field fx from the finish or note-effect chain, field pixel from a layer's shader with audio-reactive uniforms (bass, mid, high).

keep one pack to ten items, two per family, one theme per family. visual generation is 1:1 square (1080x1080). every .inf/.field must pass `Infinite --validate`; every synth and fx must pass `--audio-summary` (not silent, lufs between -26 and -8, peak under -0.5 dbfs); every sketch and pixel must render a non-flat frame (luminance 14 to 228, interest at least 0.45). previews are real engine frames (`--frame`) saved as website/assets/library/previews/<slug>.jpg, never drawn by hand. drop any item that fails a gate; a pack of eight is fine, a pack of ten that includes a failing one is not.

register items into website/assets/library/index.json and website/embeds/embeds.json (title, one plain sentence, lowercase tags). rebuild all site catalogs and seo pages by running python3 tools/website/gen_embeds.py, python3 tools/website/sync_chrome.py, and python3 tools/website/gen_seo.py. run verification via git status (only this pack's files may change) and the pre-push checks, commit with a clean smallcase message with no attribution lines, and push to origin/main. if a gate or check fails twice for the same style, stop, note "needs a style fix" in the commit-free report and do not push.
```

## 2. Samples bot (audio only, no LLM judgement)

```
run the infinite samples bot from the patch-universe checkout. sound only; every sample offers both .wav and .inf downloads with credits embedded, and no sample counts appear on the site.

1. engine & generation:
   the closed-loop sample generator engine uses the role critic (redraws below P 0.5 up to 2 times, keeping the best heard take), real instrument models (piano, rhodes, wurlitzer, strings, winds), and weighted real-song chord progressions.
   if generator logic or styles changed in src/app/graph/SampleStyles.cpp, GenerateSamplesNode.cpp, RoleCritic.cpp, or wasm/gen_main.cpp, rebuild the wasm first (source ~/emsdk/emsdk_env.sh && cmake --build build-wasm -j2) and copy infinite_gen_wasm.{js,wasm,data} to website/gen/.
   generate library: cd build-wasm && node ../tools/web_gen_library.js infinite_gen_wasm.js ../website/samples 8
   (writes recipes rendered on play/download in browser, about 8 per category for ~120 recipes per round; older audio-* files stay). then python3 tools/web_library_names.py.
   music styles in tools/catalog/styles_music.json are the only source of variety; never write or edit .inf files by hand.
2. read the log: report made count, failed takes and the reason (role critic under P 0.5, spectral gate, loudness, silence). a style failing twice in a row is reported as "needs a style fix" and not retried.
3. publish & git: if the total (recipes + audio-* items) is at least 200 and every category has 7 or more, run python3 tools/website/gen_seo.py and python3 tools/website/gen_og_cards.py, check git status shows only samples, website/gen and og files, run the pre-push checks, commit in small steps with a clean smallcase message and no attribution lines, rebase on origin/main if needed, and push to origin/main. otherwise stop and report.
```

## Where an LLM is worth it
Titles and one-line descriptions for curated items, and turning repeated drop
reasons into style-card fixes. Everything else runs unattended.

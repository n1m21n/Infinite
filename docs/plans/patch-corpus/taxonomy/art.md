# Visual art taxonomy (internal)

Drives picture generation for visual and audio+visual samples. Data: `tools/catalog/styles_art.json` (30 styles). The first 15 are the existing movements in `tools/spread/art_taxonomy.py` (palette and name are read from there so the two never drift); 15 are new and lean on electronic and digital culture.

## Why

Random node chains give pictures with no direction: a blob, some noise, a blur. An art movement pins down a palette, the kind of marks, the kind of processing, and how much things move. Choose those first and the same random wiring reads as a deliberate look.

| Axis | Lands in the patch as |
|---|---|
| Palette | Color Ramp / gradientmap stops, source colour params |
| Marks | `sources` pool: Line, Rectangle, Ring, Noise, Wireframe, Text... |
| Processing | `fx` pool: halftone, pixelate, ripple, kaleidoscope, bloom... |
| Compositing | `comp` pool: posterize, dither, threshold, Feedback, Trails... |
| Motion | `motion` 0..1 sets modulation depth on visual params |
| Ban list | nodes that would break the look (glitch in Minimal Signal, blur in Ditherpunk) |
| Pairing | `pairs` = music styles that fit, used for audio+visual samples |

## The 30 styles

| Family | Styles |
|---|---|
| Digital counter-culture | Y2K acid, glitch and datamosh, ditherpunk, cyberpunk neon, acid graphics, demoscene plasma, pixel art 8-bit |
| Screen and signal | analog CRT, vector wireframe, cymatics and Chladni, minimal signal, data landscape |
| Net-born retro | vaporwave collage, synthwave grid, holographic iridescence |
| Graphic and print | neo-brutalist poster, technical blueprint, Thundr brutalism, ukiyo-e cartography, riso zine, Bauhaus geometry, suprematist forms |
| Avant-garde | retro-surreal collage, Chavetta montage, lilliputian surrealism, liminal void |
| Optical and kinetic | op art |
| Analog psychedelia | indie psychedelia |
| Algorithmic and architectural | generative plotter line art, brutalist concrete |

## Lineages

- Algorithmic art: Vera Molnar, Manfred Mohr, Frieder Nake (1960s plotters); Tyler Hobbs and the plotter community now.
- Demoscene and tracker culture: Amiga and C64 groups, Future Crew, Farbrausch; pouet.net archive.
- Electronic-music design: The Designers Republic and Warp; rave flyers 1991-93; Peter Saville's Unknown Pleasures (1979).
- Net aesthetics: vaporwave (2010-12), synthwave revival, Y2K chrome and holographic foil.
- Signal art: Ryoji Ikeda, Carsten Nicolai, Hans Jenny's cymatics, oscilloscope music.
- Early modernism: Bauhaus, Suprematism, Constructivism, Op art (Bridget Riley, Vasarely).

## References

- Lev Manovich, *The Language of New Media* (2001).
- Michael Rush, *New Media in Art*; Lauren Rosati and others on generative art history.
- Rosa Menkman, *The Glitch Moment(um)* (2011): glitch as an aesthetic with its own theory.
- Josef Albers, *Interaction of Color* (1963): why palettes are chosen in relation, not alone.
- Peter Weibel et al., *Ctrl [Space]*; Zentrum fur Kunst und Medien catalogues.

## Use

1. Pick a style (weights by family). 2. Wire sources then fx then comp from its pools, never from `ban`. 3. Apply the palette to colour params. 4. Scale modulation by `motion`. 5. For audio+visual, take a music style from `pairs`. 6. The quality gate keeps the existing interest score and adds a palette check (the poster's dominant colours must sit near the style palette). Style id is stored in internal sample meta only.

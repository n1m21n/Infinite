# Electronic music taxonomy (internal)

Drives sample generation. Never shown on the site as classification text. Data lives in `tools/catalog/styles_music.json`; this is the reasoning behind it.

## Why a taxonomy

The first universe samples sounded like glitch because a random chain of nodes has no tempo, key, groove or arrangement idea. Real electronic genres are defined by a few measurable things. Fix those and a chain of random nodes starts to sound like music.

| Axis | What the genre pins down | Where it lands in a patch |
|---|---|---|
| Tempo | BPM band | `transport` line |
| Key and mode | minor / dorian / phrygian / pentatonic | `transport` key + scale (MusicTime ScaleType) |
| Pulse and swing | straight 4/4, broken 2-step, shuffled | Drum Sequencer `steps`, `swing`, `rate` |
| Drum grammar | where kick, snare, hats, clap fall | Drum Sequencer `laneN_stepK` (kit order: kick, snare, closed hat, open hat, clap, low tom, high tom, bell) |
| Bass figure | root-heavy, off-beat, syncopated | scale-degree string, 16th grid |
| Timbre | which synth families, which effects | node pools; `ban` list keeps stutter / bitcrush out of styles that never use them |
| Motion | how much the modulators move | `mod` 0..1 sets mod depth and count |
| Glitch tolerance | 0 = never, 1 = the genre is glitch | gates Stutter, Bitcrush, stop-start |

## Families and sub-genres (28 styles)

| Family | Styles | Defining trait |
|---|---|---|
| house | deep, acid, tech, progressive, French filter | four-on-the-floor, offbeat open hat, 118-130 BPM |
| techno | Detroit, minimal, dub, industrial | kick-driven, 122-150 BPM, machine timbres, repetition as form |
| trance | classic, psytrance | 132-148 BPM, rolling 16th bass, long builds |
| drum and bass | liquid, neurofunk, jungle | 160-178 BPM, two-step break (kick 1, snare 2 and 4 of the half-time bar) |
| bass music | UK garage, dubstep, footwork | shuffled 2-step; half-time 140; 160 BPM triple-feel juke |
| breaks | breakbeat / big beat, electro | broken kick, 808-era snare and clap |
| ambient | ambient, dark ambient | no pulse needed, slow harmony, long reverb |
| experimental | IDM, glitch | the only family where stutter / bitcrush are on purpose |
| downtempo | trip-hop style downtempo, lo-fi beats | 70-100 BPM, swing 0.15-0.22, minor 7th colour |
| retro | synthwave, vaporwave | 80s chord language; slowed, washed vaporwave |
| hard | gabber | 165-190 BPM, distorted kick |

## Cultures and lineages the table draws on

- Chicago house and acid (1984-88): Warehouse, Trax, Phuture, the Roland TR-909 and TB-303.
- Detroit techno (mid 80s): the Belleville Three, Underground Resistance; Berlin dub techno via Basic Channel (1993).
- UK rave to jungle to drum and bass (1990-95): breakbeat science, the Amen break, pirate radio.
- 2-step garage to dubstep (1997-2006): the London bass lineage, Burial's Untrue.
- Warp's "Artificial Intelligence" (1992) and IDM; the 2000s "clicks and cuts" glitch scene.
- Chicago juke / footwork (2000s); Rotterdam gabber (1990s).
- Internet-born styles: vaporwave (2010-12), lo-fi beats radio (2010s), synthwave revival (2000s-2010s).

## References (read, not scraped)

- Simon Reynolds, *Energy Flash* (1998, rev. *Generation Ecstasy*): rave, techno, jungle, trance history.
- Bill Brewster and Frank Broughton, *Last Night a DJ Saved My Life*: DJ and club culture.
- Kodwo Eshun, *More Brilliant Than the Sun*: sonic fiction, Afrofuturism in electronic music.
- Mark Fisher, *Ghosts of My Life*: hauntology (relevant to vaporwave, dark ambient, Burial).
- Kim Cascone, "The Aesthetics of Failure: Post-Digital Tendencies in Contemporary Computer Music" (Computer Music Journal, 2000): the glitch genre's own framing.
- Ishkur's Guide to Electronic Music: a family-tree reference, used here only as a cross-check of the sub-genre list.

## How the table is used

1. A sample picks a style at random with weights by family (so no family swamps the page).
2. The generator writes the `transport` line (BPM inside the band, key from the style's scale list), builds the Drum Sequencer from the style's patterns, and draws synth and effect nodes only from the style's pools.
3. The ban list is applied before wiring. Glitch tolerance above 0.4 re-allows stutter and bitcrush.
4. A quality gate measures the render (see step 4 of the plan): tempo within 6% of the band, onset regularity, not silent, not flat. Glitch styles are exempt from the regularity check.
5. The style id goes into the internal sample meta only.

## Not covered on purpose

Non-electronic genres, vocal genres, and regional styles that rely on live instruments. They stay out so every style can be made with the nodes Infinite has.

## Gaps found on review (what the table cannot say yet)

The 28 styles pin tempo, key, pulse, drums, a bass line, synth and fx pools, glitch and motion. Genres are also defined by things the table has no field for. Same principle as the visual system: a style is a distribution over axes, so add the missing axes.

| Missing axis | Why it matters | Example | Lands in a patch as |
|---|---|---|---|
| Harmony | chords, not only a root line, make house, trance, lo-fi and synthwave | deep house m7 stabs; trance i-VI-III-VII; lo-fi ii-V | chord degrees per bar, Chorder / Note Stack, voicing and inversion |
| Melodic motif | trance leads, acid lines, IDM hooks | 303 acid line with slides and accents | motif degrees + rhythm, Glide, Arpeggiator |
| Timbre signature | each genre has one sound that names it | reese bass, supersaw, 303 squelch, 808 sub, Amen break, FM bell | synth choice + param ranges per role, not only a pool |
| Sidechain / pump | the breathing of house, trance, big room | kick ducks bass and pads | Dynamics / gain modulated by the kick lane |
| Groove detail | swing per lane, ghost notes, velocity accents, humanise | UK garage hat swing, boom-bap lag | per-lane swing, velocity pattern, Humanizer amount |
| Fills and variation | an 8-second sample needs a change in it | snare roll at the end, hat open on bar 4 | pattern mutation rule per bar |
| Space | reverb size and filter movement define ambient, dub | dub echo throws, long hall | send levels, delay time as a beat fraction |
| Loudness target | genre norms differ (gabber hot, ambient quiet) | -8 LUFS vs -20 | limiter ceiling per style, used by the gate |

## Styles to add (all makeable with the existing nodes)

| Family | Style | BPM | Defining pattern |
|---|---|---|---|
| house | Afro house | 118-124 | polyrhythmic percussion, deep bass, call-response vocal chops |
| house | Amapiano | 110-114 | log-drum bass (sub with pitch glide), shuffled hats, piano chords |
| house | Tropical / melodic house | 118-124 | plucks, airy pads, arpeggio |
| house | Italo disco / Eurodance | 120-135 | octave bass, bright arps, clap on 2 and 4 |
| techno | Hard techno | 145-160 | distorted rolling kick, rumble bass |
| techno | Ambient techno / microhouse | 118-126 | soft kick, tiny clicks, long filter moves |
| techno | EBM / electro-industrial | 110-130 | sequenced bass, snare gate, shouty |
| trance | Hard trance / hardstyle | 140-155 | reverse bass, distorted kick, screech lead |
| breaks | Breakcore / speedcore | 180-240 | chopped Amen, pitched kicks |
| breaks | Miami bass / electro | 120-130 | 808 pattern, long sub, claps |
| breaks | Baltimore / Jersey club | 130-140 | kick triplets, bed-squeak sample, clap 2 and 4 |
| bass music | Future garage | 130-135 | 2-step, washed pads, vocal-like chop |
| bass music | Trap (electronic) | 130-150 | half-time snare, hat rolls, 808 slides |
| bass music | Moombahton / dembow | 108-115 | dembow kick-snare, bright lead |
| bass music | Wonky / glitch hop | 85-110 | swung off-grid, wobbly synths |
| retro | Minimal wave / synth-pop | 100-125 | analog bass, gated snare |
| retro | Chiptune | 120-160 | pulse waves, arpeggios, noise drums |
| experimental | Drone / kosmische | no pulse | long sequences, slow filter, ring-mod |
| downtempo | Chillout / Balearic | 90-115 | gentle shuffle, guitar-like plucks |

## Build order for music

1. Add the missing axes to `styles_music.json` as optional keys (chords, motif, timbre, sidechain, groove, fill, loudness); a style without a key keeps today's behaviour, so the existing 28 do not change.
2. Teach the generator the two highest-yield ones first: **chords** and **sidechain**; they change the sound of most of the 28.
3. Add the styles above in family batches; each gets a BPM-band and onset check like the rest.
4. Gate with the loudness target per style instead of one global floor.

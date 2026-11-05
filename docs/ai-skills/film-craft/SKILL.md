---
name: infinite-film-craft
description: Film, music-video and live-visual craft mapped onto Infinite - structure (acts, beats, sections), camera language in the 3D View, cutting and syncing picture to music, lighting, and render or live delivery - so an ask like "make a music video for this track", "a cinematic opener" or "visuals for my set" becomes a plan of scenes, nodes and a render. Use whenever the user wants a film, video, loop, VJ set, projection or anything that unfolds over time.
---

# Film craft in Infinite

A film in Infinite is a patch that changes over time: scenes are clips or switched branches,
the camera is a node, the music drives the cut, and the result renders to a file or plays live.

## Structure first

| Length | Shape |
|---|---|
| loop (4-16 bars) | one idea, one move, end meets start |
| short (15-60 s) | hook in the first 2 s, one build, one payoff |
| music video | follow the song: intro, verse, chorus, bridge, last chorus, outro |
| live set | scenes per track section, launched from Session or a Show Set |

Write the plan as a table before building: section, bars, picture idea, what moves, what drives it.
Changes land on bar lines; big changes on 8 or 16 bars. Bars to seconds: `bars * 4 * 60 / bpm`.

## Scenes and cutting

| Need | In Infinite |
|---|---|
| switch looks on the beat | Switcher (2D) or Switcher 3D, index modulated by Pattern or MIDI Trigger |
| arrange in time | Timeline panel: clips on lanes (`[Open timeline](open:timeline)`) |
| launch live | Session panel, Show Set for whole patches |
| crossfade | Blend with its mix on an Envelope or MSEG |
| match cut | keep the shape or colour across the switch, change everything else |
| hide a hard moment | cut away on a hit to a close-up or a flash (exposure spike) |

## Camera language (3D)

Build with 3D nodes -> Camera and Light -> Render 3D; compose in the 3D View
(`[Open 3D View](open:view3d)`).

| Shot | Use | Camera move |
|---|---|---|
| wide, establishing | where we are | slow push or none |
| medium | the subject in its space | gentle orbit |
| close-up | detail, emotion, the hit | still, or a small push on the beat |
| low angle | power, scale | camera below the subject looking up |
| top down | pattern, map, graphic | camera straight down, slow rotate |
| orbit | reveal form | LFO on the camera's rotation, rate in bars |
| push in | tension, focus | MSEG on distance over a build |

- Motivate every move: music, story or reveal. A still camera makes a move mean something.
- One move per shot. Ease in and out (MSEG, Smoothing); linear moves read as mechanical.
- Lights: key, fill, rim. Rim light separates the subject from a dark background. HDRI for reflective work.

## Sync to music

| Picture | Driven by |
|---|---|
| cut, switch | Pattern in beats, MIDI Trigger, or kick through Audio to CV |
| pulse, flash | Envelope on the kick or snare; Audio Analyze bands |
| slow evolution | LFO in bars, MSEG over a section |
| intensity | loudness from Audio Analyze to bloom, displace, particle rate |

Hit the downbeat; anticipate big hits by a frame or two of build-up.

## Look and finish

- One colour grade per film: Curves, color adjustments or a lut, same on every scene.
- Grain and vignette last, lightly (addnoise, vignette).
- Text: see infinite-visual-craft typography; titles hold at least 2 s.

## Delivery

| Out | How |
|---|---|
| file | `infinite render p.inf out.mp4` (headless) or the app's render; `infinite docs headless` |
| still | `infinite frame p.inf <t> out.png`, `infinite shot <node>` |
| live screen or projector | Output, Projection |
| other apps | Syphon Out, NDI Out; OSC Send and MIDI Out for control |

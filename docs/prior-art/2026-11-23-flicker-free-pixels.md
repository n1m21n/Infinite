# Prior art: predicting temporal aliasing of procedural pixel kernels from code

Date: 2026-11-23. Read-only research. Papers, abstracts and documentation only; no source code opened.
Study: `docs/research/flicker-free-pixels`.

## Problem
Has anyone published or shipped a compiler pass or tool that bounds how fast a procedural shader changes
over **time** (including inputs modulated from outside, such as audio), and uses that to pick temporal
supersampling or a shutter?

| # | Source | What it is | Status | Their method | Opened? |
|---|---|---|---|---|---|
| 1 | Dorn, Barnes, Lawrence, Weimer, "Towards Automatic Band-Limited Procedural Shaders", Pacific Graphics 2015. https://web.eecs.umich.edu/~weimerw/p/dorn-pg2015.pdf | Analytic band-limiting of shader functions as a function of sampling rate | Published | Closed-form convolution where possible, approximations otherwise; spatial | Full text (pdftotext); no time dimension found |
| 2 | Yang & Barnes, "Approximate Program Smoothing Using Mean-Variance Statistics, with Application to Procedural Shader Bandlimiting", EG 2018. https://arxiv.org/abs/1706.01208 | Compiler that approximates a program convolved with a Gaussian; per-node rules chosen by genetic search (1-3 h tuning per shader) | Published | Spatial smoothing, input σ = half a pixel; animated shaders tested per frame | Full text (arXiv HTML + pdftotext) |
| 3 | Yang, Nehab, Sander, "Amortized Supersampling". https://hhoppe.com/supersample.pdf | Reprojection reuses procedural shading across frames | Published | Temporal reuse; states it cannot handle arbitrary temporal changes | Search listing and summary |
| 4 | Unity URP motion vectors; UE5 animated textures under TAA (docs, forums) | Temporal AA and motion blur need motion vectors; animated procedural textures blur or ghost | Shipped | Motion vectors | Search listings |
| 5 | Blur Busters, "The Stroboscopic Effect of Finite Frame Rate Displays"; Wikipedia, stroboscopic / wagon-wheel effect | Background on temporal aliasing on displays | Articles | — | Search listings |
| 6 | Infinite's own `docs/research/aliasing-aware-field` | Smoothness order of operations sets the alias slope (jump ≈ 6, kink ≈ 12, C1 ≈ 18 dB per doubling); confirmed on 174 of 174 informative kernels | Our study | Periodic analysis on the interpreter | Ours |

## Patterns
- Automatic shader band-limiting exists in **space** (items 1, 2) and works by replacing the program with a
  smoothed one, not by predicting a sample count.
- Time is handled by reuse or motion vectors (items 3, 4), which assume content moves; a pattern that
  changes in place has no motion vector.
- No source treated inputs modulated from outside (audio, LFOs) as a source of temporal bandwidth.

## Verdict
- Static prediction of temporal aliasing for procedural pixel kernels, with modulated inputs, choosing a
  shutter: **not found.**
- Searches run (2026-11-23): "temporal antialiasing procedural shader time dimension automatic bandlimit";
  "temporal aliasing generative visuals procedural animation motion blur shader automatic";
  "stroboscopic temporal aliasing music visualizer VJ software"; full reads of items 1 and 2. Absence in
  these searches is not proof of absence.

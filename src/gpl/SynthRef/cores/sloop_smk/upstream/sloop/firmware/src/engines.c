/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
/* SLOOP 2.5: the big per-part state of GRAIN, FM6 and PHYS shares one arena per synth part (a part plays one
 * engine at a time: on a switch the old engine fades out, its voices end, then the new one starts, voice.c
 * engine_block). The first engine to ask for it after another one gets it cleared, as at power-on. */
static void *eng_arena_of(const track_t *t, uint32_t eng);
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i) { return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS); }
static inline int32_t soft_knee(int32_t y, int32_t k)   /* linear up to k, then only the peaks saturate */
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}
/* (pitch_inc: felucca_tables.h, SLOOP 2.5: from the top octave) */
#include "eng_analog.c"
#include "eng_digital.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_drawbar.c"
#include "eng_grain.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0); SLOOP 2.4 */
#include "eng_phys.c"           /* PHYS: physical models, from Felucca 1.0 (DaisySP, Rings: MIT); SLOOP 2.5 */
#include "eng_noise.c"          /* NOISE: from Felucca 1.0; SLOOP 2.5 */

typedef union {
    gr_part_t grain;
    drw_part_t drawbar;                  /* eng_drawbar.c: the part's wheels, percussion and rotary (sloop-lab) */
    fm6_part_t fm6;                      /* eng_fm6.c: six notes and the operator buses */
    uint32_t phys[PHYS_ARENA / 4u];      /* eng_phys.c: two SYMP slots or three small ones */
} eng_arena_t;
static eng_arena_t eng_arena[NPART] __attribute__((section(".pool")));
static uint8_t eng_arena_own[NPART];            /* the engine + 1 whose state is in it, 0 = none */
#ifdef ARENA_STATS
static uint32_t eng_arena_claims, eng_arena_bad;   /* host tests: claims, accesses by an engine the part does not play */
#endif
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NPART)
        return 0;
#ifdef ARENA_STATS
    if (t->engine != eng)
        eng_arena_bad++;
#endif
    if (eng_arena_own[p] != eng + 1u) {
        memset(&eng_arena[p], 0, sizeof eng_arena[p]);
        eng_arena_own[p] = (uint8_t)(eng + 1u);
#ifdef ARENA_STATS
        eng_arena_claims++;
#endif
    }
    return &eng_arena[p];
}

/* fx.c units_split: rendering t this block would draw rng() (libc.c), so t stays on core 0, where the draws
 * keep their order: a sounding voice's slot is not on the part's MODEL (phys_render resets it with a draw).
 * When the arena or the layout is not PHYS's yet, every slot starts over (phys_slot_of): taken as a draw */
static int phys_render_draws(const track_t *t, int32_t model)   /* model: the MODEL this block renders with (fx.c) */
{
    uint32_t p = (uint32_t)(t - trk), md = (uint32_t)model % PM_COUNT, big = md == PM_SYMP, i;
    uint32_t sz = big ? PHYS_SLOT_BIG : PHYS_SLOT_SMALL;
    const uint8_t *a = (const uint8_t *)&eng_arena[p % NPART];
    if (p >= NPART)
        return 0;
    if (eng_arena_own[p] != ENGI_PHYS + 1u || phys_lay[p] != big + 1u)
        return 1;
    for (i = 0; i < phys_poly(t); i++)
        if (t->v[i].active && ((const phys_slot_t *)(a + i * sz))->model != md)
            return 1;
    return 0;
}
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_DIGITAL, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE,
                                                    &ENG_FORMANT, &ENG_TRIO, &ENG_DRAWBAR,   /* 7: ENGI_DRAWBAR (eng_drawbar.c) */
                                                    &ENG_GRAIN,
                                                    &ENG_FM6,    /* 9 (ENGI_FM6): always; SLICE after it (core.h) */
                                                    &ENG_PHYS, &ENG_NOISE,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#endif
};
_Static_assert(ENGI_FM6 == 9u, "ENGINES[ENGI_FM6] is FM6");
_Static_assert(ENGI_GRAIN == 8u && ENGI_PHYS == 10u && ENGI_NOISE == 11u && ENGI_SLICE == 12u, "ENGINES[] order");

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
    return e < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
}

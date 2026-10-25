/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* PHYS: physical models. The DSP is ported to fixed point from DaisySP (Electrosmith, Emilie Gillet;
 * MIT: phys_dsp.c) and Rings' sympathetic strings (Emilie Gillet; MIT: phys_symp.c); this file is the
 * Felucca engine around them.
 *
 * MODEL picks one of four (the values are stored: new ones are appended):
 *   MODAL   a bank of 12 band-pass modes (bells, bars, plates): STRC stretches the partials
 *           (below 0.25 compressed, 0.25..0.3 harmonic, above more and more inharmonic), BRIT
 *           keeps the high modes ringing and opens the strike, DAMP the decay (127 rings on),
 *           POS the strike position (the mode amplitudes follow cos(2 pi pos i), 0..1/4).
 *           Struck by an impulse into a low-pass; with BOW > 0 sustained by random hits (bowed /
 *           blown) at that level instead
 *   STRNG   an extended Karplus-Strong string, plucked by a noise burst one period long: STRC
 *           below ~0.25 a curved bridge (buzzy, sitar-like), above it dispersion (stiff, piano-
 *           like), BRIT the burst's tone and the loop filter, DAMP the decay, POS the pluck
 *           position (a comb on the burst, 0 = none); BOW > 0 again sustains it by random hits
 *   MEMB    a struck drum head: the modal bank with the modes of a circular membrane. HARM blends an
 *           ideal head (inharmonic: toms, timpani) into a loaded one (harmonic overtones: a tabla-like
 *           hand drum), POS goes from the centre (round, only the circular modes) to the rim, BEND
 *           drops the pitch after the strike (up to 12 semitones); BRIT, DAMP as MODAL
 *   SYMP    the plucked string with three sympathetic strings that ring along (a sitar's, a
 *           harp's): CHRD tunes them (OCT 5TH 4TH MAJ MIN SUS 7TH above the note, ROOT: the track's
 *           scale root, its fifth and octave around C3), SYMP how much they take from the string, BUZZ
 *           the curved bridge; BRIT, DAMP as STRNG
 * ACC is the accent at full velocity (brighter, longer, louder strike; velocity scales it), EXC
 * mixes the exciter itself into the output (the mallet / pick). The track's ENV / LFO -> FLT move BRIT.
 *
 * Polyphony 3 per part (engine_t.poly): each part's voices 0..2 own a state slot (SYMP's, the largest:
 * the string's 512 + 128 sample lines, Q20, and three 256-sample sympathetic lines, 16-bit). SLOOP 2.5:
 * the slots live in the part's engine arena (engines.c eng_arena_of), shared with GRAIN and FM6. Voice amplitude: the track's ADSR as for every
 * engine; the presets hold SUS at 127 so the model's own decay is heard and REL damps it after the key.
 * Cost (host, 8 notes asked = 3 voices): see the README and cpu_baseline.txt. */
#include "phys_dsp.c"
#include "phys_symp.c"

enum { PM_MODAL, PM_STRING, PM_MEMB, PM_SYMP, PM_COUNT };
#define PHYS_POLY 3
/* output: model 1.0 -> this (VOICE_FS scale), then a soft knee; per model (a strike of all the modes in
 * phase peaks high but is short) */
static const int32_t PHYS_GAIN[PM_COUNT] = {56000, 48000, 135000, 36000};   /* (SLOOP 2.5: x2, x4, x1.5, x3: the
                                                                               * level of SLOOP's other engines) */

typedef struct {
    uint8_t model;               /* PM_* the state belongs to */
    union {
        px_modal_t m;            /* MODAL, MEMB */
        px_string_t s;
        px_symp_t y;
    } u;
} phys_slot_t;


static const char *const N_PHYS_MODEL[] = {"MODAL", "STRNG", "MEMB", "SYMP"};
static const char *const N_PHYS_CHORD[] = {"OCT", "5TH", "4TH", "MAJ", "MIN", "SUS", "7TH", "ROOT", 0};
/* per model: STRC .. EXC under their own names (phys_desc; range and default as edit[], 0 = edit[]) */
static const param_desc_t PHYS_D_HARM = {"HARM", F_PCT, 0, 127, 64, 0, 0};
static const param_desc_t PHYS_D_BEND = {"BEND", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t PHYS_D_CHRD = {"CHRD", F_INT, 0, 127, 64, N_PHYS_CHORD, 0};
static const param_desc_t PHYS_D_SYMP = {"SYMP", F_PCT, 0, 127, 20, 0, 0};
static const param_desc_t PHYS_D_BUZZ = {"BUZZ", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t *const PHYS_DESC[PM_COUNT][8] = {
    [PM_MEMB] = {0, &PHYS_D_HARM, 0, 0, 0, 0, &PHYS_D_BEND, 0},
    [PM_SYMP] = {0, &PHYS_D_CHRD, 0, 0, &PHYS_D_SYMP, 0, &PHYS_D_BUZZ, 0},
};

static const param_desc_t *phys_desc(const track_t *t, uint32_t k)
{
    return k < 8u ? PHYS_DESC[(uint32_t)t->p[P_E0] % PM_COUNT][k] : 0;
}

/* SYMP: the sympathetic strings above the note, cents (CHRD's names) */
static const int16_t PHYS_CHORDS[7][PX_NSYMP] = {
    {-1200, 1200, 2400}, {700, 1200, 1900}, {500, 1200, 1700}, {400, 700, 1200},
    {300, 700, 1200}, {200, 700, 1400}, {400, 700, 1000},
};
static const int8_t PHYS_DETUNE[PX_NSYMP] = {1, -1, 0};   /* 1/16 semitone (6 cents): the strings beat a little */

/* SLOOP 2.5: the slots live in the part's engine arena (engines.c), in one of two layouts over the same
 * bytes: SYMP (the largest: a string and three sympathetic lines) two slots, the other models three
 * smaller ones (a string or a modal bank). A switch of layout starts the part's notes over, as a MODEL
 * change under a note always did. SYMP plays two voices (phys_poly), the others three. */
#define PHYS_SLOT_BIG ((sizeof(phys_slot_t) + 7u) & ~7u)
#define PHYS_SLOT_SMALL ((__builtin_offsetof(phys_slot_t, u) + sizeof(px_string_t) + 7u) & ~7u)
#define PHYS_ARENA (2u * PHYS_SLOT_BIG > 3u * PHYS_SLOT_SMALL ? 2u * PHYS_SLOT_BIG : 3u * PHYS_SLOT_SMALL)
_Static_assert(sizeof(px_modal_t) <= sizeof(px_string_t), "a small PHYS slot holds MODAL and MEMB too");
static uint8_t phys_lay[NPART];          /* 1 + the layout the slots are in (0 small, 1 SYMP), 0 = none yet */
static uint32_t phys_poly(const track_t *t)
{
    return (uint32_t)t->p[P_E0] % PM_COUNT == PM_SYMP ? 2u : 3u;
}
static phys_slot_t *phys_slot_of(track_t *t, voice_t *v)
{
    uint32_t i, p, big, k, sz;
    uint8_t *a;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    p = (uint32_t)(t - trk);
    i = (uint32_t)(v - t->v);
    big = (uint32_t)t->p[P_E0] % PM_COUNT == PM_SYMP;
    if (i >= phys_poly(t))
        return 0;
    a = (uint8_t *)eng_arena_of(t, ENGI_PHYS);
    sz = big ? PHYS_SLOT_BIG : PHYS_SLOT_SMALL;
    if (phys_lay[p] != big + 1u) {                       /* the other layout: every slot starts over */
        for (k = 0; k < phys_poly(t); k++)
            ((phys_slot_t *)(a + k * sz))->model = 0xFF;
        phys_lay[p] = (uint8_t)(big + 1u);
    }
    return (phys_slot_t *)(a + i * sz);
}

/* a clean state for model md (only its own part of the union: a small slot holds no more) */
static void phys_reset(phys_slot_t *S, uint32_t md, uint32_t seed)
{
    uint32_t i, *w = (uint32_t *)&S->u;
    uint32_t n = md == PM_SYMP ? sizeof(px_symp_t) : md == PM_STRING ? sizeof(px_string_t) : sizeof(px_modal_t);
    for (i = 0; i < n / 4u; i++)
        w[i] = 0;
    S->model = (uint8_t)md;
    seed |= 1u;
    switch (md) {
    case PM_STRING:
    case PM_SYMP:                                        /* (the main string is the union's px_string_t) */
        S->u.s.rng = seed;
        S->u.s.src = 1u << 30;
        break;
    default:
        S->u.m.rng = seed;
        break;
    }
}

static void phys_note_on(track_t *t, voice_t *v)
{
    phys_slot_t *S = phys_slot_of(t, v);
    uint32_t md = (uint32_t)t->p[P_E0] % PM_COUNT;
    if (!S)
        return;
    if ((!v->env && !v->env_out) || S->model != md)     /* a fresh voice (not a retrigger): from rest */
        phys_reset(S, md, rng());
    switch (md) {
    case PM_MODAL:
    case PM_MEMB:
        S->u.m.trig = 1;                                 /* a retrigger strikes the ringing body again */
        break;
    default:                                             /* STRNG, SYMP */
        S->u.s.trig = 1;
        break;
    }
}

/* SYMP: the sympathetic strings' pitches (cycles Q32) by CHRD: a chord above the note, or ROOT */
static __attribute__((noinline)) void phys_symp_pitches(const track_t *t, const vmod_t *m, uint32_t *f)
{
    uint32_t c = (uint32_t)t->p[P_E1] >> 4, k;          /* 0..7 */
    for (k = 0; k < PX_NSYMP; k++) {
        int32_t p16 = PHYS_DETUNE[k];
        p16 += c < 7u ? m->pitch16 + ((PHYS_CHORDS[c][k] * 41) >> 8)   /* cents -> 1/16 semitones (0.16) */
                      : (48 + t->p[P_ROOT] + (k == 1u ? 7 : k == 2u ? 12 : 0)) * 16;
        f[k] = pitch_inc(clamp(p16, 0, 2047));
    }
}

static void phys_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    phys_slot_t *S = phys_slot_of(t, v);
    uint32_t md = (uint32_t)p[P_E0] % PM_COUNT, i, f0 = m->inc;
    int32_t y[CTL], ax[CTL];
    int32_t strc = p[P_E1] * 516, brit = clamp(p[P_E2] * 516 + m->cutoff * 2, 0, 65536);   /* Q16 0..1 */
    int32_t damp = p[P_E3] * 516, pos = p[P_E4], exc = p[P_E7] * 258;   /* exc Q15 */
    int32_t bow = p[P_E6] * 206;                         /* BOW: Dust at up to 0.4 (DaisySP's sustain: 1) */
    int32_t acc = clamp(p[P_E5] * v->vel * 4, 0, 65536);   /* ACC at full velocity, Q16 */
    int32_t g = PHYS_GAIN[md];
    if (!S)
        return;
    if (n > CTL)
        n = CTL;
    if (S->model != md)                                  /* MODEL changed under a sounding note */
        phys_reset(S, md, rng());
    switch (md) {
    case PM_MODAL: {
        px_modal_blk_t B;
        px_modal_block(&B, &S->u.m, f0, strc, brit, damp, acc, pos * 129, bow);   /* pos 0..1/4 */
        px_modal_run(&B, &S->u.m, y, ax, n);
        break;
    }
    case PM_MEMB: {
        px_modal_blk_t B;
        px_memb_block(&B, &S->u.m, f0, strc, brit, damp, acc, pos * 516, p[P_E6] * 6192);   /* BEND 0..12 st */
        px_modal_run(&B, &S->u.m, y, ax, n);
        break;
    }
    case PM_STRING: {
        px_string_blk_t B;
        px_string_block(&B, &S->u.s, f0, strc, brit, damp, acc, bow, pos * 516);   /* 0..1/2 period */
        px_string_excite(&B, &S->u.s, ax, n);
        px_string_run(&B, &S->u.s, ax, y, n);
        break;
    }
    default: {                                           /* SYMP */
        px_string_blk_t B;
        uint32_t f[PX_NSYMP];
        phys_symp_pitches(t, m, f);
        px_string_block(&B, &S->u.y.s, f0, 16384 - p[P_E6] * 129, brit, damp, acc, 0, 0);   /* BUZZ: STRC 0.25 .. 0 */
        px_string_excite(&B, &S->u.y.s, ax, n);
        px_string_run(&B, &S->u.y.s, ax, y, n);
        px_symp_block(&S->u.y, f, brit, damp, pos * 516);
        px_symp_run(&S->u.y, y, y, n);
        break;
    }
    }
    for (i = 0; i < n; i++) {
        int32_t s = px_m(y[i] + px_m(ax[i], exc, 15), g, 20), a = s < 0 ? -s : s;
        if (a > 24000) {                                 /* soft knee: only peaks saturate */
            a = 24000 + (softclip(clamp(a - 24000, 0, 1 << 20) * 2) >> 1);
            s = s < 0 ? -a : a;
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
}

/* MODAL partials by STRC: 34 harmonic, 70 1 : 2.83 : 5.48 : 8.90 (a free bar, a chime: 1 : 2.76 : 5.40 : 8.93),
 * 98 1 : 3.57 : 7.65, 116 1 : 4.0 : 9.0 (a tuned bar: 1 : 4 : 10) */
static const preset_t PHYS_PRESETS[] = {
    /* name, {MODEL, STRC, BRIT, DAMP, POS, ACC, BOW, EXC}, {A D S R}, fenv, mono */
    {"BELL TREE", {PM_MODAL, 70, 100, 106, 12, 100, 0, 6}, {0, 100, 127, 96}, 0, 0, FX(0, 20, 30, 60)},
    {"MODAL BAR", {PM_MODAL, 116, 38, 40, 24, 90, 0, 30}, {0, 90, 127, 52}, 0, 0, FX(0, 0, 15, 30)},
    {"STR PLUCK", {PM_STRING, 56, 72, 84, 40, 96, 0, 0}, {0, 100, 127, 58}, 0, 0, FX(0, 10, 30, 30)},
    {"BOWED MTL", {PM_MODAL, 98, 70, 112, 10, 80, 90, 0}, {60, 90, 127, 90}, 0, 0, FX(0, 40, 20, 70)},
    {"THUMB PNO", {PM_MODAL, 34, 16, 56, 100, 90, 0, 48}, {0, 100, 127, 60}, 0, 0, FX(0, 10, 20, 40)},
    /* MEMB: {MODEL, HARM, BRIT, DAMP, POS, ACC, BEND, EXC} */
    {"HAND DRUM", {PM_MEMB, 112, 76, 56, 44, 100, 14, 24}, {0, 100, 127, 64}, 0, 0, FX(0, 0, 10, 30)},
    {"MEMB TOMS", {PM_MEMB, 0, 52, 40, 30, 100, 40, 30}, {0, 100, 127, 70}, 0, 0, FX(0, 0, 0, 40)},
    /* SYMP: {MODEL, CHRD, BRIT, DAMP, SYMP, ACC, BUZZ, EXC} */
    {"SITAR", {PM_SYMP, 127, 80, 88, 64, 100, 92, 0}, {0, 100, 127, 90}, 0, 0, FX(0, 10, 20, 50)},
    {"PHYS HARP", {PM_SYMP, 8, 62, 74, 60, 90, 0, 0}, {0, 100, 127, 80}, 0, 0, FX(0, 20, 20, 60)},
    /* SLOOP 2.5: more models. STRNG: {MODEL, STRC, BRIT, DAMP, POS, ACC, BOW, EXC} */
    {"NYLON GTR", {PM_STRING, 40, 50, 80, 30, 80, 0, 10}, {0, 100, 127, 50}, 0, 0, FX(0, 10, 15, 35)},
    {"STEEL GTR", {PM_STRING, 56, 85, 88, 22, 96, 0, 6}, {0, 100, 127, 56}, 0, 0, FX(0, 15, 18, 35)},
    {"MUTED GTR", {PM_STRING, 50, 40, 55, 30, 90, 0, 20}, {0, 100, 127, 20}, 0, 0, FX(0, 0, 20, 20)},
    {"PLUCK BASS", {PM_STRING, 60, 45, 74, 35, 100, 0, 8}, {0, 100, 127, 30}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -12)},
    {"KOTO", {PM_STRING, 20, 70, 82, 15, 96, 0, 4}, {0, 100, 127, 60}, 0, 0, FX(0, 10, 25, 45)},
    {"BANJO", {PM_STRING, 70, 100, 60, 12, 110, 0, 12}, {0, 100, 127, 40}, 0, 0, FX(0, 0, 15, 25)},
    {"CELLO BOW", {PM_MODAL, 34, 60, 110, 12, 70, 90, 0}, {60, 90, 127, 70}, 0, 0, FX(0, 20, 10, 55)},
    /* SYMP: {MODEL, CHRD, BRIT, DAMP, SYMP, ACC, BUZZ, EXC} */
    {"TANPURA", {PM_SYMP, 0, 70, 120, 90, 80, 110, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 10, 10, 60)},
    /* MODAL: chimes, bells, bars, a glass bowl, a pan, a block */
    {"CHIMES", {PM_MODAL, 70, 110, 118, 8, 110, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 15, 25, 65)},
    {"BIG BELL", {PM_MODAL, 80, 90, 124, 16, 120, 0, 8}, {0, 100, 127, 110}, 0, 0, FX(0, 10, 20, 70), XP(P_TRANS + 1, -12)},
    {"VIBRA BAR", {PM_MODAL, 116, 60, 100, 20, 80, 0, 10}, {0, 100, 127, 70}, 0, 0, FX(0, 20, 15, 40),
     XP(P_LD_AMP + 1, 20, P_LRATE + 1, 70)},
    {"GLASS BOWL", {PM_MODAL, 98, 90, 120, 6, 60, 70, 0}, {70, 90, 127, 100}, 0, 0, FX(0, 30, 20, 75)},
    {"STEEL PAN", {PM_MODAL, 36, 80, 90, 30, 100, 0, 20}, {0, 100, 127, 60}, 0, 0, FX(0, 10, 20, 35)},
    {"WOOD BLOCK", {PM_MODAL, 116, 30, 35, 40, 110, 0, 60}, {0, 100, 127, 20}, 0, 0, FX(0, 0, 10, 20)},
    /* MEMB: {MODEL, HARM, BRIT, DAMP, POS, ACC, BEND, EXC} */
    {"TABLA", {PM_MEMB, 120, 70, 70, 50, 110, 20, 30}, {0, 100, 127, 50}, 0, 0, FX(0, 0, 10, 25)},
    {"CONGA", {PM_MEMB, 90, 60, 50, 40, 100, 8, 40}, {0, 100, 127, 40}, 0, 0, FX(0, 0, 10, 25)},
    {"TIMPANI", {PM_MEMB, 10, 50, 100, 20, 110, 6, 10}, {0, 100, 127, 90}, 0, 0, FX(0, 0, 0, 50), XP(P_TRANS + 1, -12)},
};

static const engine_t ENG_PHYS = {
    .name = "PHYS",
    .page_title = {"BODY", "EXCT"},
    .edit = {
        {"MODEL", F_ENUM, 0, PM_COUNT - 1, 0, N_PHYS_MODEL, 0},
        {"STRC", F_PCT, 0, 127, 64, 0, 0},
        {"BRIT", F_PCT, 0, 127, 80, 0, 0},
        {"DAMP", F_PCT, 0, 127, 80, 0, 0},
        {"POS", F_PCT, 0, 127, 20, 0, 0},
        {"ACC", F_PCT, 0, 127, 90, 0, 0},
        {"BOW", F_PCT, 0, 127, 0, 0, 0},
        {"EXC", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = PHYS_PRESETS,
    .npresets = NELEM(PHYS_PRESETS),
    .note_on = phys_note_on,
    .render = phys_render,
    .fil_page = -1,
    .color = 0xFD8F,
    .macro = {P_E1, P_E2, P_E3, P_E4},
    .poly = PHYS_POLY,
    .desc = phys_desc,
};

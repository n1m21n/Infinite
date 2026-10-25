/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chip voice: pulse/triangle/saw/noise/wave RAM, quantised amplitude and a held
 * (downsampled) output; SWEEP bends the pitch down after note-on.
 *
 * WAVE = WRAM plays a 32-sample, 4-bit wave table, read step-wise (no interpolation);
 * DUTY then picks one of 16 built-in tables (DUTY / 8, shown as "WAV#" with its name).
 * CHIP = STEP: a 4-bit stepped volume envelope with a frame-clock divider replaces the
 * ADSR curve (lofi_amp), the triangle becomes a 32-step 4-bit staircase, the pulse
 * duties are 12.5 / 25 / 50 / 75 %; CRSH then sets the envelope (shown as "DCY"). */
static const char *const N_CHIP[] = {"4BIT", "4B/2", "8BIT", "1BIT", "STEP"};
static const char *const N_RWAVE[] = {"PLS", "TRI", "SAW", "NOIS", "WRAM"};
static const char *const N_RARP[] = {"OFF", "OCT", "MAJ", "MIN"};
enum { CHIP_4BIT, CHIP_4B2, CHIP_8BIT, CHIP_1BIT, CHIP_STEP };
enum { RW_PLS, RW_TRI, RW_SAW, RW_NOIS, RW_WRAM };

/* wave RAM: 16 tables x 32 samples x 4 bits, two samples a byte (high nibble first);
 * our own shapes, from simple formulas (squares, sine, triangle, saws, harmonic sums,
 * one formant peak for the vowels). DC is taken out per table in lofi_render. */
static const char *const N_WRAM[] = {"SQ50", "SQ25", "SQ12", "SINE", "TRI", "SAW", "SAW2", "BAS1",
                                     "BAS2", "ORGN", "HOLW", "VOXA", "VOXO", "VOXE", "BUZZ", "STAIR", 0};
static const uint8_t WRAM[16][16] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* SQ50 */
    {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* SQ25 */
    {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* SQ12 */
    {0x89, 0xAC, 0xDE, 0xEF, 0xFF, 0xEE, 0xDC, 0xA9, 0x86, 0x53, 0x21, 0x10, 0x00, 0x11, 0x23, 0x56},   /* SINE */
    {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10},   /* TRI */
    {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},   /* SAW */
    {0x01, 0x12, 0x33, 0x45, 0x56, 0x77, 0x89, 0x9A, 0x56, 0x67, 0x88, 0x9A, 0xAB, 0xCC, 0xDE, 0xEF},   /* SAW2: + its octave */
    {0xEE, 0xEF, 0xFF, 0xFF, 0xFF, 0xEE, 0xED, 0xDC, 0x11, 0x10, 0x00, 0x00, 0x00, 0x11, 0x12, 0x23},   /* BAS1: sloped square */
    {0xCE, 0xFF, 0xED, 0xBA, 0x9A, 0xBD, 0xEF, 0xFE, 0xC9, 0x64, 0x21, 0x00, 0x00, 0x01, 0x24, 0x69},   /* BAS2: 1 + 2 + 3 */
    {0x8B, 0xEF, 0xED, 0xBA, 0x9A, 0xAA, 0x98, 0x77, 0x88, 0x87, 0x65, 0x55, 0x65, 0x42, 0x10, 0x14},   /* ORGN: 1..4 */
    {0x8D, 0xFE, 0xDD, 0xDD, 0xCD, 0xDD, 0xDE, 0xFD, 0x82, 0x01, 0x22, 0x22, 0x32, 0x22, 0x21, 0x02},   /* HOLW: odd */
    {0x8F, 0xFC, 0xA7, 0x67, 0x89, 0x88, 0x88, 0x87, 0x88, 0x77, 0x77, 0x76, 0x78, 0x98, 0x53, 0x00},   /* VOXA: "ah" */
    {0x8F, 0xEE, 0xFD, 0xDB, 0x98, 0x66, 0x66, 0x77, 0x88, 0x89, 0x99, 0x97, 0x64, 0x22, 0x01, 0x10},   /* VOXO: "oh" */
    {0x7F, 0x96, 0xCF, 0xBB, 0xCC, 0xBB, 0xAA, 0x98, 0x77, 0x65, 0x54, 0x43, 0x34, 0x40, 0x39, 0x60},   /* VOXE: "ee" */
    {0xF3, 0x00, 0x12, 0x22, 0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22},   /* BUZZ: spike + ring */
    {0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00},   /* STAIR: 4 levels */
};
#define Q4 2184                  /* one 4-bit step (2 * 2184 * 7.5 = 32760) */

/* CHIP = STEP: CRSH as the envelope, CRSH / 4 = b: 0 constant volume, 1..16 a one-shot decay
 * (divider period 0..15), 17..31 a looping one (period 1..15) */
static const char *const N_DCY[] = {"CONST", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10",
                                    "D11", "D12", "D13", "D14", "D15", "L1", "L2", "L3", "L4", "L5", "L6", "L7",
                                    "L8", "L9", "L10", "L11", "L12", "L13", "L14", "L15", 0};
static const param_desc_t LOFI_WAVNUM = {"WAV#", F_INT, 0, 127, 64, N_WRAM, 0};   /* = edit[2] but the names */
static const param_desc_t LOFI_DCY = {"DCY", F_INT, 0, 127, 0, N_DCY, 0};        /* = edit[3] but the names */

static const param_desc_t *lofi_desc(const track_t *t, uint32_t k)
{
    if (k == 2u && t->p[P_E1] == RW_WRAM)
        return &LOFI_WAVNUM;
    if (k == 3u && t->p[P_E0] == CHIP_STEP)
        return &LOFI_DCY;
    return 0;
}

/* the stepped envelope: volume 15..0 in v->s[6] bits 0..3, its divider in bits 4..7, a 240 Hz
 * frame clock (Q16 phase in v->s[7]); each frame the divider counts down and, at 0, reloads the
 * period and takes one step off the volume (a looping one starts again at 15 after 0) */
#define FRAME_Q16 ((240u * CTL * 65536u) / FS)       /* frames per control tick, Q16 */
static int32_t step_period(int32_t b) { return b <= 16 ? b - 1 : b - 16; }

static void lofi_note_on(track_t *t, voice_t *v)
{
    v->s[0] = 0;                 /* held sample */
    v->s[1] = 0;                 /* hold counter */
    v->s[2] = 0x7FFF;            /* LFSR */
    v->s[3] = 0;                 /* sweep, 1/16 st */
    v->s[4] = 0;                 /* 1-pole lp state */
    v->s[5] = 0;                 /* arp counter */
    v->s[6] = 15 | (step_period(t->p[P_E3] >> 2) & 15) << 4;   /* stepped env: every note-on restarts it */
    v->s[7] = 0;
}

/* CHIP = STEP: the voice amplitude is the stepped envelope, not the ADSR curve. The ADSR only
 * gates: held, it is kept at full; after note-off the staircase keeps running (as the chip's
 * envelope never sees a note-off) and the voice is cut when the ADSR release has fallen to 1 %
 * (about the REL time; REL 0 cuts at once) or a one-shot decay has reached 0. */
static int32_t lofi_amp(track_t *t, voice_t *v, int32_t adsr)
{
    int32_t b = t->p[P_E3] >> 2, vol = v->s[6] & 15, div = (v->s[6] >> 4) & 15;
    if (t->p[P_E0] != CHIP_STEP)
        return adsr;
    if (!v->active)                                     /* the ADSR ended it (or the voice was given up) */
        return 0;
    if (v->stage == 3u) {
        if (v->env < (1 << 24) / 100 || (b >= 1 && b <= 16 && !vol)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
            return 0;
        }
    } else {
        v->env = 1 << 24;                               /* the release starts from full */
    }
    if (!b)
        return 15 * Q4;
    v->s[7] += (int32_t)FRAME_Q16;
    while (v->s[7] >= 65536) {
        v->s[7] -= 65536;
        if (div) {
            div--;
        } else {
            div = step_period(b);
            if (vol)
                vol--;
            else if (b > 16)
                vol = 15;
        }
    }
    v->s[6] = vol | div << 4;
    return vol * Q4;
}

static void lofi_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint8_t STEP_DUTY[4] = {1, 2, 4, 6};   /* eighths: 12.5 / 25 / 50 / 75 % */
    const int16_t *p = t->p;
    uint32_t chip = (uint32_t)p[P_E0], wave = (uint32_t)p[P_E1], i;
    uint32_t stepc = chip == CHIP_STEP;
    uint32_t duty = stepc ? 0x20000000u * STEP_DUTY[(p[P_E2] / 32) & 3]
                          : 0x20000000u * (1u + (uint32_t)p[P_E2] / 32u);   /* 12.5 / 25 / 37.5 / 50 % */
    int32_t crush = stepc ? 0 : p[P_E3];                /* STEP: CRSH is the envelope */
    int32_t bits = chip == CHIP_8BIT ? 8 : chip == CHIP_1BIT ? 1 : 4;
    int32_t hold = 1 + crush / 8 + (chip == 1u ? 1 : 0);
    int32_t lpk = 3000 + (p[P_E7] << 8) + (m->cutoff > 0 ? m->cutoff : 0);
    static const int8_t ARPS[4][3] = {{0, 0, 0}, {0, 12, 0}, {0, 4, 7}, {0, 3, 7}};
    int32_t ar = p[P_E6];
    int32_t pitch, held = v->s[0], cnt = v->s[1], lp = v->s[4];   /* state in locals: out[] may alias v->s[] */
    uint32_t inc, ph0 = v->ph[0], lfsr = (uint32_t)v->s[2];
    const uint8_t *wr = WRAM[((uint32_t)p[P_E2] >> 3) & 15u];
    int32_t wdc = 0;
    if (wave == RW_WRAM) {                              /* the table's mean: no DC from lopsided shapes */
        int32_t sum = 0;
        for (i = 0; i < 16u; i++)
            sum += bits == 1 ? ((wr[i] >> 7) + ((wr[i] >> 3) & 1)) * 2 - 2 : (wr[i] >> 4) + (wr[i] & 15) - 15;
        wdc = bits == 1 ? sum * 32767 / 32 : sum * 2 * Q4 / 32;   /* (2 v - 15) Q4, or +-32767 at 1 bit */
    }
    /* sweep down, vibrato (uses the track LFO through m->pitch16 already) */
    if (v->s[3] < 4096)                                 /* past 2047 + an arp step the pitch is 0 anyway */
        v->s[3] += p[P_E4] / 8;
    v->s[5]++;
    pitch = m->pitch16 - v->s[3] + ARPS[ar & 3][(v->s[5] / 28) % 3] * 16;   /* ~50 Hz, chip-style */
    inc = pitch_inc(clamp(pitch, 0, 2047));
    if (p[P_E5])
        inc += (uint32_t)(((int32_t)(inc >> 12) * (((osc_sine(v->ph[1]) >> 8) * p[P_E5]) >> 4)) >> 4);   /* no overflow */
    v->ph[1] += 0x01000000u;
    if (lpk > 32767)
        lpk = 32767;
    for (i = 0; i < n; i++) {
        int32_t s;
        if (--cnt <= 0) {
            uint32_t q = bits < 16;                     /* quantise (the 4-bit tables are already) */
            cnt = hold;
            switch (wave) {
            case RW_TRI:
                if (stepc) {                            /* 32 steps, 4 bits: 15..0, 0..15 */
                    uint32_t k = ph0 >> 27;
                    s = (int32_t)(k < 16u ? 15u - k : k - 16u) * (2 * Q4) - 15 * Q4;
                    q = 0;
                } else {
                    s = osc_tri(ph0);
                }
                break;
            case RW_SAW:
                s = (int32_t)(ph0 >> 16) - 32768;
                break;
            case RW_NOIS: {
                uint32_t l = lfsr;
                if ((ph0 + inc * (uint32_t)hold) < ph0 || chip == 3u)
                    l = (l >> 1) | (((l ^ (l >> 1)) & 1u) << 14);
                lfsr = l;
                s = (l & 1u) ? 32767 : -32768;
                break;
            }
            case RW_WRAM: {                             /* step-wise, no interpolation */
                uint32_t k = ph0 >> 27, b = wr[k >> 1];
                b = (k & 1u) ? b & 15u : b >> 4;
                s = (bits == 1 ? (b >= 8u ? 32767 : -32767) : ((int32_t)b * 2 - 15) * Q4) - wdc;
                q = 0;
                break;
            }
            default:                                    /* minus its mean: no DC for narrow pulses */
                s = (ph0 < duty ? 32767 : -32768) - ((int32_t)(duty >> 16) - 32768);
                break;
            }
            if (q)                                      /* rounded, not floored (floor = DC) */
                s = ((s + (1 << (15 - bits))) >> (16 - bits)) << (16 - bits);
            if (crush > 64)
                s = (s >> 12) << 12;
            held = s;
        }
        ph0 += inc;
        lp += mulq15(clamp(held - lp, -65535, 65535), lpk);   /* (a WAVE / CHIP change mid-note: no wrap) */
        out[i] += mulq15(mulq15(lp, amp_at(m, i)), VOICE_FS);
    }
    v->ph[0] = ph0;
    v->s[0] = held;
    v->s[1] = cnt;
    v->s[2] = (int32_t)lfsr;
    v->s[4] = lp;
}

static const preset_t LOFI_PRESETS[] = {
    {"GAME LEAD", {0, 0, 32, 0, 0, 20, 0, 127}, {0, 60, 90, 30}, 0, 1, FX(0, 0, 36, 18)},
    {"GB BASS", {1, 1, 0, 0, 0, 0, 0, 90}, {0, 50, 70, 20}, 0, 1, FX(0, 0, 6, 0), XP(P_TRANS + 1, -12)},
    {"8BIT ARP", {2, 0, 96, 0, 0, 0, 2, 110}, {0, 60, 80, 40}, 0, 0, FX(0, 0, 30, 20), ARP(1, 2, 2, 40)},
    /* SLOOP 2.5: {CHIP, WAVE, DUTY, CRSH, SWP, VIB, ARP, TONE} */
    {"NES BASS", {2, 1, 64, 0, 0, 0, 0, 127}, {0, 50, 80, 20}, 0, 1, FX(0, 0, 0, 4), XP(P_TRANS + 1, -12)},
    {"CHIP CHORD", {2, 0, 32, 0, 0, 0, 2, 110}, {0, 60, 80, 40}, 0, 0, FX(0, 0, 30, 20)},
    {"1BIT BEEP", {3, 0, 64, 0, 0, 0, 0, 100}, {0, 40, 60, 20}, 0, 0, FX(0, 0, 25, 15)},
    {"WAVE LEAD", {0, 4, 40, 0, 0, 40, 0, 120}, {2, 60, 100, 30}, 0, 1, FX(0, 0, 30, 20), XP(P_GLIDE + 1, 30)},
    {"CHIP NOISE", {2, 3, 64, 0, 90, 0, 0, 127}, {0, 55, 0, 24}, 0, 0, FX(0, 0, 10, 10)},
};

static const engine_t ENG_LOFI = {
    "LOFI", {"CHIP", "MOTN"},
    {
        {"CHIP", F_ENUM, 0, 4, 0, N_CHIP, 0},
        {"WAVE", F_ENUM, 0, 4, 0, N_RWAVE, 0},
        {"DUTY", F_INT, 0, 127, 64, 0, 0},              /* WRAM: the table, DUTY / 8 (lofi_desc) */
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},               /* STEP: the envelope (lofi_desc) */
        {"SWP", F_PCT, 0, 127, 0, 0, 0},
        {"VIB", F_PCT, 0, 127, 0, 0, 0},
        {"ARP", F_ENUM, 0, 3, 0, N_RARP, 0},
        {"TONE", F_PCT, 0, 127, 127, 0, 0},
    },
    LOFI_PRESETS, sizeof(LOFI_PRESETS) / sizeof(LOFI_PRESETS[0]), 1, lofi_note_on, lofi_render,
    0x3F2C, {P_E1, P_E2, P_E3, P_REL}, 0, lofi_amp, lofi_desc,
};

/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesised drum kits (drums.c kits DRUM_SAMPLED..): analogue-style models in fixed point, in the
 * spirit of the classic drum machines. Every sound of a kit is one dsnd_t, written in musical units
 * by tools/gen_drumkits.py (felucca_drumkits.h). A sound is up to four layers into one filter:
 *   tone   SINE / TRI / SQUARE / FM / BELL (two squares 1 : 1.48), with an exponential pitch drop
 *          (BEND semitones over BTIME); its envelope HOLDs at full, then decays. A second partial
 *          (T2: a sine at RATIO x the tone, decaying twice as fast): the second mode of a drum head
 *   click  the attack: a 1-2 ms burst of bright noise (the beater, the stick)
 *   noise  WHITE / METAL (six squares at the 808 cymbal ratios) / CYM (metal + white) / CHIP
 *          (15-bit LFSR at its own clock); its own hold and decay. CLAP: three bursts, then the tail
 *   filter a resonant state-variable filter (LP / BP / HP) on the noise, or on everything; its
 *          cutoff follows the pitch envelope (FENV) and the velocity (softer = darker); a
 *          12 dB/oct high-pass (HPF) on the noise before it
 *   out    drive (tanh), the kit's crush (bit depth, sample-and-hold), the sound's level
 * The envelopes, the pitch and the filter run at the control rate (CTL samples) and are ramped
 * per sample. Cost: ~50-60 integer ops per voice and sample (METAL: +6 adds). */
enum { DW_NONE, DW_SINE, DW_TRI, DW_SQUARE, DW_FM, DW_BELL };
enum { DN_NONE, DN_WHITE, DN_METAL, DN_CYM, DN_CHIP, DN_CLAP = 0x10 };
enum { DF_OFF, DF_LP, DF_BP, DF_HP, DF_ALL = 4 };   /* flt: mode | DF_ALL (the tone too) | res << 3 */
typedef struct {
    uint8_t wave, src;           /* DW_*, DN_* (| DN_CLAP) */
    uint8_t pitch, fine;         /* the tone: MIDI note + 1/16 semitones (METAL / CYM: their base) */
    uint8_t bend, btime;         /* pitch drop: semitones at the hit, DECAY_K index of its fall */
    uint8_t hold, decay, tlev;   /* tone: hold (2 ms units), DECAY_K index, level 0..127 */
    uint8_t t2, t2lev;           /* second partial: ratio x32 (0 = none), level 0..127 */
    uint8_t click;               /* attack burst level 0..127 */
    uint8_t nlev, nhold, ndec;   /* noise: level 0..127, hold (2 ms units), DECAY_K index */
    uint8_t flt, fcut;           /* filter: DF_* | res (0..31) << 3; CUTOFF_HZ index */
    int8_t fenv;                 /* cutoff offset at the hit (CUTOFF_HZ steps, follows the pitch drop) */
    uint8_t hpf;                 /* CUTOFF_HZ index of the noise high-pass, 0 = off */
    uint8_t chip;                /* CHIP noise: clock note */
    uint8_t drive, level;        /* drive 0..127; level: 128 + 4 x dB (-32 .. +31.75 dB) */
} dsnd_t;
#define DS_LANES 16              /* kick snare clap hat open-hat tom-lo tom-hi crash ride shaker conga
                                  * rim cowbell clave kick-2 snare-2 */
typedef struct {
    const char *name, *style;
    uint8_t crush;               /* low nibble: bits dropped, high nibble: sample-and-hold - 1 */
    dsnd_t s[DS_LANES];
} dkit_t;
#include "felucca_drumkits.h"    /* DS_KITS[], DS_NKITS (tools/gen_drumkits.py) */

/* GM note -> synth lane and a pitch offset in semitones */
static const struct { uint8_t lane; int8_t semi; } DS_MAP[128 - 35] = {
    /* 35 */ {14, 0}, {0, 0}, {11, 0}, {1, 0}, {2, 0}, {15, 0}, {5, -3}, {3, 0}, {5, 0}, {3, -1},
    /* 45 */ {5, 3}, {4, 0}, {6, -3}, {6, 0}, {7, 0}, {6, 3}, {8, 0}, {7, 3}, {8, 6}, {9, 4},
    /* 55 */ {7, 5}, {12, 0}, {7, 1}, {9, -5}, {8, 1}, {10, 9}, {10, 6}, {10, 3}, {10, 0}, {10, -5},
    /* 65 */ {6, 5}, {6, 1}, {12, 7}, {12, 3}, {9, -3}, {9, 0}, {13, 9}, {13, 6}, {9, -7}, {9, -9},
    /* 75 */ {13, 0}, {13, 4}, {13, -1}, {10, 12}, {10, 8}, {8, 24}, {8, 20}, {9, 2},
    /* 83..127: shaker */
};
static uint32_t ds_lane(uint32_t note, int32_t *semi)
{
    *semi = 0;
    if (note < 35u)
        return 0;
    if (note >= 83u)
        return 9;
    *semi = DS_MAP[note - 35u].semi;
    return DS_MAP[note - 35u].lane;
}

typedef struct {
    const dsnd_t *d;
    uint32_t ph, inc, inc_to;    /* tone phase; increment now and at the end of the block */
    uint32_t ph2, ph3, mph[6];   /* BELL / FM second oscillator, the second partial, the metal squares */
    uint32_t minc[6];
    int32_t pe;                  /* pitch envelope Q15 (32767 = BEND semitones) */
    int32_t amp, amp_to;         /* tone envelope Q15, now and at the end of the block */
    int32_t nz, nz_to;           /* noise envelope Q15 */
    int32_t ck, ck_to;           /* click envelope Q15 */
    uint32_t kpe, ka, kn;        /* per-block decay factors Q16 */
    uint32_t t;                  /* samples since the hit (CLAP bursts) */
    uint16_t hold, nhold;        /* blocks left at full level */
    int32_t base16;              /* pitch in 1/16 semitones */
    int32_t cut;                 /* filter cutoff (CUTOFF_HZ index << 8) without the envelope */
    int32_t f1, f2, fk;          /* SVF states, damping (Q12) */
    int32_t a1, a2, a3;          /* SVF coefficients (dsp.c tsvf_coef) */
    int32_t hp1, hp2, ahp;       /* noise high-pass states, coefficient Q15 (0 = off) */
    int32_t rnd, cprev;          /* white noise state, the click's last noise sample */
    uint32_t lfsr, cacc, cinc;   /* CHIP noise */
    int32_t gain, lg;            /* velocity Q15, the sound's level Q10 */
    int32_t held, holdn;         /* crush: sample-and-hold */
    uint8_t crush, bursts, fmode;
    int8_t dk;                   /* (2.7) the DECAY's DECAY_K steps (the CLAP's tail after its bursts) */
} dsv_t;

/* per-sample DECAY_K (Q16) -> per-block (CTL = 32 samples) by squaring five times */
static uint32_t ds_k32(uint32_t idx)
{
    uint32_t k = DECAY_K[idx & 127u], i;
    for (i = 0; i < 5u; i++)
        k = (k * k) >> 16;
    return k;
}
static int32_t ds_onepole(uint32_t cut)                 /* CUTOFF_HZ index -> a = g / (1 + g), Q15 */
{
    uint32_t g = SVF_G[cut & 127u];
    return (int32_t)((g << 15) / (4096u + g));
}
static uint32_t ds_inc(int32_t p16) { return pitch_inc(clamp(p16, 0, 127 * 16 + 15)); }
static uint16_t ds_blocks(uint32_t units2ms) { return (uint16_t)(units2ms * 2u * FS / 1000u / CTL); }

/* the filter's coefficients for this block: the cutoff plus the envelope's share */
static void ds_filter(dsv_t *s)
{
    const dsnd_t *d = s->d;
    int32_t cut = s->cut + ((s->pe * (int32_t)d->fenv) >> 7), i, g, den;   /* (pe Q15 x steps << 8) */
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + s->fk)) >> 12);
    s->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    s->a2 = (s->a1 * g) >> 12;
    s->a3 = (s->a2 * g) >> 12;
}

/* the 808 cymbal oscillators (205.3 304.4 369.6 522.7 540 800 Hz) as 1/16 semitones above the first */
static const int16_t DS_METAL[6] = {0, 109, 163, 259, 268, 377};

/* (2.7, the DRUMS screen's SOUND page) a sound's DECAY, -4 .. +3 (0: as designed): the decays' DECAY_K steps (x0.25 ..
 * x3 the time, 5.4 % a step) and the holds scaled alike (Q8) */
static const int8_t DK_IDX[8] = {-26, -17, -10, -4, 0, 6, 13, 21};
static const uint16_t DK_SCALE[8] = {64, 102, 154, 205, 256, 358, 512, 768};
/* tune16: the sound's TUNE in 1/16 semitones; dsel: its DECAY + 4 (0 .. 7) */
static void ds_on(dsv_t *s, const dsnd_t *snd, uint32_t crush, uint32_t note, uint32_t vel, int32_t tune16, uint32_t dsel)
{
    int32_t dk = DK_IDX[dsel & 7u];
    int32_t semi;
    uint32_t lane = ds_lane(note, &semi), i;
    const dsnd_t *d = &snd[lane];
    memset(s, 0, sizeof *s);
    s->d = d;
    s->crush = (uint8_t)crush;
    s->gain = (int32_t)vel * 258;
    {   /* level: quarter dB from 128; 24 quarters = 6 dB = x2 (0.02 dB off per 6 dB) */
        static const uint16_t QDB[24] = {1024, 1054, 1085, 1116, 1149, 1182, 1217, 1252, 1289, 1326, 1365, 1405,
                                         1446, 1488, 1532, 1576, 1622, 1669, 1718, 1768, 1820, 1873, 1928, 1984};
        int32_t e = (int32_t)d->level - 128 + 192, q = e / 24 - 8;   /* (+192: positive for the divide) */
        uint32_t g = QDB[e % 24];
        s->lg = (int32_t)(q >= 0 ? g << q : g >> -q);
    }
    s->base16 = ((int32_t)d->pitch + semi) * 16 + d->fine + tune16;
    s->pe = d->bend || d->fenv ? 32767 : 0;
    s->kpe = ds_k32(d->btime);
    s->inc = s->inc_to = ds_inc(s->base16 + ((s->pe * (int32_t)d->bend * 16) >> 15));
    s->amp = s->amp_to = d->wave ? (int32_t)d->tlev * 258 : 0;
    s->ka = ds_k32((uint32_t)clamp((int32_t)d->decay + dk, 0, 127));
    s->hold = ds_blocks(d->hold);
    s->dk = (int8_t)dk;
    if (dk)
        s->hold = (uint16_t)(((uint32_t)s->hold * DK_SCALE[dsel & 7u]) >> 8);
    s->nz = s->nz_to = (d->src & 15u) ? (int32_t)d->nlev * 258 : 0;
    s->kn = (d->src & DN_CLAP) ? ds_k32(12) : ds_k32((uint32_t)clamp((int32_t)d->ndec + dk, 0, 127));   /* CLAP: short bursts first */
    s->nhold = (d->src & DN_CLAP) ? 0 : ds_blocks(d->nhold);
    if (dk)
        s->nhold = (uint16_t)(((uint32_t)s->nhold * DK_SCALE[dsel & 7u]) >> 8);
    s->ck = s->ck_to = (int32_t)d->click * 258;
    s->bursts = 1;
    s->fmode = d->flt & 7u;
    s->fk = 8192 - (int32_t)(d->flt >> 3) * 245;        /* damping 2.0 .. ~0.15 (Q12), as tsvf_coef */
    /* softer hits are darker: about an octave from the hardest to a ghost note */
    s->cut = ((int32_t)d->fcut << 8) + ((int32_t)vel - 110) * 40;
    s->ahp = d->hpf ? ds_onepole(d->hpf) : 0;
    if (!d->wave && tune16) {                           /* (2.7.2) a tone-less sound (closed hat, shaker, clap): TUNE
                                                         * shifts its brightness too, so it reacts like resampling a
                                                         * sample kit (~1.168 CUTOFF_HZ steps a semitone). The tonal
                                                         * sounds and the metal cymbals already track TUNE via base16. */
        int32_t co8 = tune16 * 1197 / 64;               /* the TUNE as a CUTOFF_HZ index offset, Q8 */
        s->cut += co8;
        if (d->hpf)
            s->ahp = ds_onepole((uint32_t)clamp((int32_t)d->hpf + co8 / 256, 1, 127));
    }
    if (s->fmode & 3u)
        ds_filter(s);
    s->rnd = (int32_t)(0x9E3779B9u ^ (note * 2654435761u) ^ rng());
    s->lfsr = 0x4001u;
    {   /* CHIP clock: 8x that note; saturated (the top notes would wrap 32 bits: a dull noise) */
        uint32_t ci = pitch_inc(clamp((int32_t)d->chip, 0, 127) * 16);
        s->cinc = ci >= 0x20000000u ? 0xFFFFFFFFu : ci << 3;
    }
    for (i = 0; i < 6u; i++)
        s->minc[i] = ds_inc(s->base16 + DS_METAL[i]);
}

/* one CTL block of control: the envelopes' targets at its end */
static void ds_control(dsv_t *s)
{
    const dsnd_t *d = s->d;
    s->amp = s->amp_to;
    s->nz = s->nz_to;
    s->ck = s->ck_to;
    s->inc = s->inc_to;
    if (s->hold)
        s->hold--;
    else
        s->amp_to = (int32_t)(((uint32_t)s->amp * s->ka) >> 16);
    if ((d->src & DN_CLAP) && s->bursts < 4u && s->t >= s->bursts * 420u) {
        s->nz = (int32_t)d->nlev * 258;                 /* bursts at 0, 9.5, 19 ms; the 4th is the tail */
        if (++s->bursts == 4u) {
            s->kn = ds_k32((uint32_t)clamp((int32_t)d->ndec + s->dk, 0, 127));
            s->nhold = ds_blocks(d->nhold);
        }
    }
    if (s->nhold)
        s->nhold--;
    else
        s->nz_to = (int32_t)(((uint32_t)s->nz * s->kn) >> 16);
    s->ck_to = (s->ck * 3) >> 3;                        /* ~1 ms: gone in a few blocks */
    s->pe = (int32_t)(((uint32_t)s->pe * s->kpe) >> 16);
    s->inc_to = ds_inc(s->base16 + ((s->pe * (int32_t)d->bend * 16) >> 15));
    if ((s->fmode & 3u) && d->fenv)
        ds_filter(s);
}

static int ds_alive(const dsv_t *s)
{
    if ((s->d->src & DN_CLAP) && s->bursts < 4u)
        return 1;                                       /* bursts still to come */
    return s->hold || s->amp > 8 || s->amp_to > 8 || s->nz > 8 || s->nz_to > 8 || s->ck > 8;
}

/* n (<= CTL) samples of voice s into out[] (Q15-ish, peak ~ 32767); returns 0 when it ended */
static int ds_render(dsv_t *s, int32_t *out, uint32_t n)
{
    const dsnd_t *d = s->d;
    uint32_t i, src = d->src & 15u, wave = d->wave, fmode = s->fmode & 3u, fall = s->fmode & DF_ALL;
    /* (2.6, a whistle on the kicks) the levels glide to their targets exactly: a0 + r (i + 1) / CTL. A step of
     * r >> CTL_LOG2 a sample fell up to CTL - 1 short and jumped the rest at every block: a click 1378 times a
     * second under the kick's long sine (tones at 1.4 / 2.8 kHz). The pitch keeps its step (a frequency, no jump) */
    int32_t a0 = s->amp, ra = s->amp_to - s->amp, z0 = s->nz, rn = s->nz_to - s->nz, c0 = s->ck, rc = s->ck_to - s->ck;
    int32_t di = (int32_t)(s->inc_to - s->inc) >> CTL_LOG2;
    int32_t a = s->amp, z = s->nz, c = s->ck, inc = (int32_t)s->inc;
    int32_t drive = 16 + d->drive, bits = s->crush & 15, hold = (s->crush >> 4) + 1;
    int32_t t2l = (int32_t)d->t2lev * 258;
    for (i = 0; i < n; i++) {
        int32_t tone = 0, nz = 0, x;
        if (wave) {
            switch (wave) {
            case DW_SINE: tone = sine_i(s->ph); break;
            case DW_TRI: tone = osc_tri(s->ph); break;
            case DW_SQUARE: tone = (int32_t)s->ph < 0 ? -24000 : 24000; break;
            case DW_FM:                                 /* ratio 1.41: metallic, index follows the tone */
                tone = sine_i(s->ph + ((uint32_t)(sine_i(s->ph2) * a) << 1));
                s->ph2 += (uint32_t)inc + ((uint32_t)inc >> 2) + ((uint32_t)inc >> 3) + ((uint32_t)inc >> 5);
                break;
            default:                                    /* BELL: two squares */
                tone = ((int32_t)s->ph < 0 ? -12000 : 12000) + ((int32_t)s->ph2 < 0 ? -12000 : 12000);
                s->ph2 += (uint32_t)inc + ((uint32_t)inc >> 1) - ((uint32_t)inc >> 6);
                break;
            }
            s->ph += (uint32_t)inc;
            tone = mulq15(tone, a);
            if (d->t2) {                                /* the second mode: a sine, decaying twice as fast */
                s->ph3 += ((uint32_t)inc >> 5) * d->t2;
                tone += mulq15(mulq15(sine_i(s->ph3), mulq15(a, a)), t2l);
            }
        }
        if (src) {
            if (src == DN_WHITE) {
                nz = (int32_t)noise32(&s->rnd) >> 17;
            } else if (src == DN_CHIP) {
                s->cacc += s->cinc;
                if (s->cacc < s->cinc) {                /* wrapped: clock the LFSR */
                    uint32_t b = (s->lfsr ^ (s->lfsr >> 1)) & 1u;
                    s->lfsr = (s->lfsr >> 1) | (b << 14);
                }
                nz = (s->lfsr & 1u) ? 20000 : -20000;
            } else {                                    /* METAL / CYM: six squares (+ white) */
                s->mph[0] += s->minc[0];              /* unrolled: one sign bit each */
                s->mph[1] += s->minc[1];
                s->mph[2] += s->minc[2];
                s->mph[3] += s->minc[3];
                s->mph[4] += s->minc[4];
                s->mph[5] += s->minc[5];
                nz = (int32_t)(((s->mph[0] >> 31) + (s->mph[1] >> 31) + (s->mph[2] >> 31) +
                                (s->mph[3] >> 31) + (s->mph[4] >> 31) + (s->mph[5] >> 31)) * 9600u) - 28800;
                if (src == DN_CYM)
                    nz += (int32_t)noise32(&s->rnd) >> 18;
            }
            if (wave == DW_BELL)
                nz += tone, tone = 0;
            if (s->ahp) {                               /* two one-pole high-passes: 12 dB / oct */
                s->hp1 += mulq15(nz - s->hp1, s->ahp);
                nz -= s->hp1;
                s->hp2 += mulq15(nz - s->hp2, s->ahp);
                nz -= s->hp2;
            }
            nz = mulq15(nz, z);
            if (d->src & DN_CLAP)
                nz <<= 1;                               /* band-passed: as loud as the others */
        }
        if (c) {                                        /* the click: differentiated noise, bright */
            int32_t w = (int32_t)noise32(&s->rnd) >> 17;
            nz += mulq15(w - s->cprev, c);
            s->cprev = w;
        }
        x = fall ? tone + nz : nz;
        if (fmode) {                                    /* SVF (dsp.c tsvf_lp), with the BP / HP outs */
            int32_t v3 = x - s->f2;
            int32_t v1 = (s->a1 * s->f1 + s->a2 * v3) >> 13;
            int32_t v2 = s->f2 + ((s->a2 * s->f1 + s->a3 * v3) >> 13);
            s->f1 = clamp(2 * v1 - s->f1, -150000, 150000);
            s->f2 = clamp(2 * v2 - s->f2, -150000, 150000);
            x = fmode == DF_LP ? v2 : fmode == DF_BP ? v1 : x - ((s->fk * v1) >> 12) - v2;
        }
        x += fall ? 0 : tone;
        if (d->drive)
            x = softclip((x * drive) >> 4);
        if (s->crush) {
            if (--s->holdn <= 0) {
                s->holdn = hold;
                s->held = (x >> bits) << bits;
            }
            x = s->held;
        }
        out[i] = ((mulq15(x, s->gain) >> 2) * s->lg) >> 8;
        a = a0 + ((ra * (int32_t)(i + 1u)) >> CTL_LOG2);   /* (|r| < 2^17: no overflow) */
        z = z0 + ((rn * (int32_t)(i + 1u)) >> CTL_LOG2);
        c = c0 + ((rc * (int32_t)(i + 1u)) >> CTL_LOG2);
        inc += di;
    }
    s->t += n;
    ds_control(s);
    return ds_alive(s);
}

/* ---- SLOOP 2.5: your own synthesised kits, SYN1..SYN4 (drums.c DRUM_SYN..), edited in the web editor's DRUM
 * SYNTH page (editor_dsyn.c). Each is a kit as DS_KITS has them: 16 dsnd_t and the crush, with a name. They live
 * in RAM (the drum voices play them from there, so an edit is heard at once) and in flash after the settings
 * (project.c settings_write: one A/B object, so a cut-off write keeps the last copy). Never stored: copies of
 * 808, 909, TRAP and TECHNO. Every value from outside (the editor, a backup, flash) goes through dsu_fix, so the
 * voices only ever see values the factory kits could hold. */
#define DSU_N 4u
#define DSU_MAGIC 0x31555344u                     /* "DSU1" */
typedef struct {
    char name[8];                                 /* ASCII, 0-padded (not terminated when 8 long) */
    uint8_t crush, src, rsv[2];                   /* the kit's crush; src: the factory kit it started from */
    dsnd_t s[DS_LANES];
} dsu_kit_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, n;                              /* 1, DSU_N */
    dsu_kit_t k[DSU_N];
} dsu_bank_t;
_Static_assert(sizeof(dsnd_t) == 22u && sizeof(dsu_kit_t) == 364u && sizeof(dsu_bank_t) == 1464u, "drum synth bank layout");
static dsu_bank_t dsu;
static volatile uint8_t dsu_dirty;                /* RAM differs from flash (an edit not stored yet) */
static const uint8_t DSU_DEF[DSU_N] = {0, 1, 5, 12};   /* 808, 909, TRAP, TECHNO */

static void dsu_from_factory(uint32_t k, uint32_t src)
{
    dsu_kit_t *u = &dsu.k[k % DSU_N];
    const dkit_t *f = &DS_KITS[src % DS_NKITS];
    uint32_t i;
    memset(u, 0, sizeof *u);
    for (i = 0; i < 8u && f->name[i]; i++)
        u->name[i] = f->name[i];
    u->crush = f->crush;
    u->src = (uint8_t)(src % DS_NKITS);
    memcpy(u->s, f->s, sizeof u->s);
}
static void dsu_defaults(void)
{
    uint32_t k;
    dsu.magic = DSU_MAGIC;
    dsu.ver = 1;
    dsu.n = DSU_N;
    for (k = 0; k < DSU_N; k++)
        dsu_from_factory(k, DSU_DEF[k]);
}
/* a sound into the ranges the voices expect (ds_on / ds_render index tables with them) */
static void dsu_fix_sound(dsnd_t *d)
{
    if (d->wave > DW_BELL) d->wave = DW_SINE;
    if ((d->src & 15u) > DN_CHIP || (d->src & ~(uint8_t)(15u | DN_CLAP))) d->src = DN_WHITE;
    if (d->pitch > 127u) d->pitch = 127;
    d->fine &= 15u;
    if (d->bend > 96u) d->bend = 96;
    d->btime &= 127u; d->decay &= 127u; d->ndec &= 127u;
    if (d->tlev > 127u) d->tlev = 127;
    if (d->t2lev > 127u) d->t2lev = 127;
    if (d->click > 127u) d->click = 127;
    if (d->nlev > 127u) d->nlev = 127;
    d->fcut &= 127u; d->hpf &= 127u; d->chip &= 127u;
    if (d->drive > 127u) d->drive = 127;
}
static void dsu_fix_kit(dsu_kit_t *u)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        if (u->name[i] && (u->name[i] < 32 || u->name[i] > 126))
            u->name[i] = ' ';
    u->src %= DS_NKITS;
    u->rsv[0] = u->rsv[1] = 0;
    for (i = 0; i < DS_LANES; i++)
        dsu_fix_sound(&u->s[i]);
}
static int dsu_valid(const dsu_bank_t *b) { return b->magic == DSU_MAGIC && b->ver == 1u && b->n == DSU_N; }
static const dsu_kit_t *dsu_kit(uint32_t k)       /* (RAM never set up: the defaults, e.g. without flash) */
{
    if (!dsu_valid(&dsu))
        dsu_defaults();
    return &dsu.k[k % DSU_N];
}
/* the GM note each synthesised lane answers to with no pitch offset (DS_MAP): the editor's audition */
static const uint8_t DS_LANE_NOTE[DS_LANES] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};

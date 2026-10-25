/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter icons: a 12 x 12 px, 2-bit glyph left of each column label.
 * Art: assets/icons.png + icons.json -> build/gen/felucca_icons.h
 * (tools/gen_icons.py). Which icon a parameter gets is decided here, by its label.
 * FELUCCA_ICONS=0 turns them off (labels get their full width back). */
#include "felucca_icons.h"
#ifndef FELUCCA_ICONS
#define FELUCCA_ICONS (FELUCCA_ICONS_N > 0)   /* on when the atlas has icons */
#endif
#if FELUCCA_ICONS && FELUCCA_ICONS_N == 0
#error "FELUCCA_ICONS=1 but felucca_icons.h has no icons (assets/icons.png missing?)"
#endif
#define ICON_NONE 0xFFu                   /* no icon (empty column) */
#define ICON_AUTO 0xFEu                   /* draw_column: look the label up */
#define ICON_GAP 2                        /* px between the icon and the label */

#if FELUCCA_ICONS
typedef struct {
    const char *label;
    uint8_t icon;
} icon_map_t;

/* label -> icon. Labels shared by several parameters (RATE, WAVE, SET) are
 * told apart by descriptor in param_icon(). Unknown labels get ICON_GENERIC. */
static const icon_map_t ICON_MAP[] = {
    /* track: ENV, LFO, destinations */
    {"LVL", ICON_LEVEL}, {"ATK", ICON_ATTACK}, {"DEC", ICON_DECAY}, {"SUS", ICON_SUSTAIN},
    {"REL", ICON_RELEASE}, {"FLT", ICON_CUTOFF}, {"PIT", ICON_PITCH}, {"SHP", ICON_SHAPE},
    {"FX", ICON_MOD}, {"RATE", ICON_RATE}, {"WAVE", ICON_WAVE}, {"PHS", ICON_PHASE},
    {"FADE", ICON_FADE}, {"AMP", ICON_LEVEL},
    /* arp, scale, pattern */
    {"MODE", ICON_ARP}, {"OCT", ICON_OCTAVE}, {"GATE", ICON_GATE}, {"SWG", ICON_SWING},
    {"PROB", ICON_PROB}, {"HOLD", ICON_HOLD}, {"ORD", ICON_ORDER}, {"ROOT", ICON_PITCH},
    {"SCL", ICON_SCALE}, {"QNT", ICON_QUANTIZE}, {"TRN", ICON_TRANSPOSE}, {"LEN", ICON_LENGTH},
    {"DIV", ICON_DIVISION},
    /* fx sends, voice */
    {"DST", ICON_DIST}, {"CHO", ICON_CHORUS}, {"DLY", ICON_DELAY}, {"REV", ICON_REVERB},
    {"VCE", ICON_VOICE}, {"GLD", ICON_GLIDE}, {"GLMOD", ICON_GLIDE}, {"PRIO", ICON_ORDER},
    {"ALLOC", ICON_VOICE}, {"DTUNE", ICON_DETUNE}, {"PAN", ICON_PAN}, {"MUTE", ICON_MUTE},
    /* global */
    {"BPM", ICON_TEMPO}, {"CLK", ICON_TEMPO}, {"TUNE", ICON_TUNE}, {"TIME", ICON_TIME},
    {"FDBK", ICON_FEEDBACK}, {"COLR", ICON_TONE}, {"MIX", ICON_MIX}, {"SIZE", ICON_SIZE},
    {"DAMP", ICON_DAMP}, {"CRT", ICON_RATE}, {"CDP", ICON_MOD}, {"MIDI", ICON_MIDI},
    {"SYNC", ICON_TEMPO}, {"ROUT", ICON_MIX}, {"CPU", ICON_CHIP}, {"SLOT", ICON_SAVE}, {"SECT", ICON_SAVE},
    {"LOAD", ICON_LOAD}, {"SAVE", ICON_SAVE}, {"ENG", ICON_WAVE}, {"CLRSQ", ICON_CLEAR},
    {"INIT", ICON_CLEAR}, {"ERASE", ICON_CLEAR}, {"CH", ICON_MIDI}, {"LEVEL", ICON_LEVEL},
    {"OUT", ICON_MIDI}, {"IN", ICON_MIDI}, {"CH1", ICON_MIDI}, {"CH2", ICON_MIDI}, {"CH3", ICON_MIDI}, {"CH4", ICON_MIDI},   /* (2.6:
                                                                 * GLO > MIDI, MIDI 2) */
    {"OTHER", ICON_MIDI}, {"BEND", ICON_TUNE},
    /* engines (eng_*.c edit[] labels) */
    {"DTN", ICON_DETUNE}, {"NOIS", ICON_NOISE}, {"CUT", ICON_CUTOFF}, {"RES", ICON_RESO},
    {"DRV", ICON_DRIVE}, {"KTR", ICON_KEYTRACK}, {"ALG", ICON_ALGORITHM}, {"R2", ICON_RATIO},
    {"R3", ICON_RATIO}, {"R4", ICON_RATIO}, {"IDX", ICON_MOD}, {"MDEC", ICON_DECAY},
    {"FB", ICON_FEEDBACK}, {"CHIP", ICON_CHIP}, {"DUTY", ICON_PULSE}, {"CRSH", ICON_BITS},
    {"SWP", ICON_SWEEP}, {"VIB", ICON_VIBRATO}, {"ARP", ICON_ARP}, {"TONE", ICON_TONE},
    {"SET", ICON_SAMPLE}, {"BITS", ICON_BITS}, {"LOOP", ICON_LOOP}, {"WAVE2", ICON_WAVE},
    {"DCW", ICON_SHAPE}, {"ENV", ICON_ENV}, {"LINE", ICON_MIX}, {"SUB", ICON_SUB},
    {"FOLD", ICON_FOLD}, {"WAV#", ICON_WAVE}, {"DCY", ICON_DECAY}, {"INT2", ICON_TRANSPOSE},
    {"INT3", ICON_TRANSPOSE}, {"PW", ICON_PULSE},
    {"REG", ICON_DRAWBAR}, {"BODY", ICON_LEVEL}, {"TOP", ICON_TONE}, {"PERC", ICON_DECAY},
    {"CLICK", ICON_ATTACK}, {"ROTR", ICON_VIBRATO},
    {"SRC", ICON_SAMPLE}, {"START", ICON_STEPS}, {"PTCH", ICON_PITCH}, {"DCAY", ICON_DECAY},
    {"POS", ICON_PHASE}, {"DENS", ICON_GRAIN}, {"SPRD", ICON_NOISE},
    {"VOWL", ICON_VOICE}, {"VOWL2", ICON_VOICE}, {"TALK", ICON_SWEEP}, {"SHIFT", ICON_TRANSPOSE},
    {"BUZZ", ICON_PULSE}, {"BRTH", ICON_NOISE}, {"Q", ICON_RESO}, {"RAND", ICON_PROB},
    /* fixed columns drawn by ui_draw.c (STEP page, preset browser, SYSTEM) */
    {"NOTE", ICON_PITCH}, {"PITCH", ICON_PITCH}, {"FORCE", ICON_ACCENT}, {"STEP", ICON_STEPS}, {"FLAG", ICON_ACCENT}, {"ACC", ICON_ACCENT}, {"SLD", ICON_SLIDE}, {"USB", ICON_MIDI},
    {"TRACK", ICON_MIX},                  /* TRACKS page (LEVEL, LEN, PAN: above) */
    {"SLCR", ICON_SLICE}, {"PAT", ICON_STEPS}, {"DEPTH", ICON_MIX},   /* SLICER page (RATE: param_icon) */
};

UI_SMALL static uint32_t icon_for_label(const char *l)
{
    uint32_t i;
    if (!l || !l[0] || l[0] == '-')
        return ICON_NONE;
    for (i = 0; i < sizeof(ICON_MAP) / sizeof(ICON_MAP[0]); i++)
        if (str_eq(l, ICON_MAP[i].label))
            return ICON_MAP[i].icon;
    return ICON_GENERIC;
}

/* WAVE / WAVE2 set to a shape: that shape's icon (value names of the engines and the LFO) */
static const icon_map_t WAVE_ICON[] = {
    {"SIN", ICON_W_SIN}, {"TRI", ICON_W_TRI}, {"SAW", ICON_W_SAW}, {"SQR", ICON_W_SQR},
    {"PLS", ICON_W_PLS}, {"PWM", ICON_W_PWM}, {"S&H", ICON_W_SH}, {"NOIS", ICON_NOISE},
    {"DSIN", ICON_W_DSIN}, {"SPLS", ICON_W_SPLS}, {"RSAW", ICON_W_RSAW}, {"RTRI", ICON_W_RTRI},
    {"RTRP", ICON_W_RTRP},
};

UI_SMALL static uint32_t param_icon(const param_desc_t *d, int32_t v)
{
    uint32_t i;
    if (!d)
        return ICON_NONE;
    if (d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max &&
        (str_eq(d->label, "WAVE") || str_eq(d->label, "WAVE2")))
        for (i = 0; i < sizeof(WAVE_ICON) / sizeof(WAVE_ICON[0]); i++)
            if (str_eq(d->names[v], WAVE_ICON[i].label))
                return WAVE_ICON[i].icon;
    if (d == &TP[P_LWAVE])
        return ICON_LFO_WAVE;                 /* "WAVE" is also the oscillator wave */
    if (d == &TP[P_ARATE] || d == &TP[P_SLRATE])
        return ICON_DIVISION;                 /* arp / SLICER RATE is a note division, not Hz */
    if (d->names == N_TRIO_MODE)
        return ICON_CUTOFF;                   /* TRIO's MODE is the filter type, not the arp mode */
#if FELUCCA_SLICE
    if (d->names == N_SLC_DIV)
        return ICON_SLICE;                    /* SLICE: DIV is the slicing, MODE the gate, REV the direction */
    if (d->names == N_SLC_MODE)
        return ICON_GATE;
    if (d->names == N_SLC_REV)
        return ICON_ORDER;
#endif
    return icon_for_label(d->label);
}

/* engine name (ENGINES[]->name, or "DRUM") -> icon */
UI_SMALL static uint32_t engine_icon(const char *name)
{
    static const icon_map_t M[] = {
        {"ANALOG", ICON_WAVE}, {"DIGITAL", ICON_ALGORITHM}, {"PHASE", ICON_PHASE}, {"LOFI", ICON_BITS},
        {"SAMPLE", ICON_SAMPLE}, {"VOICE", ICON_MOUTH}, {"TRIO", ICON_TRIO}, {"WHEEL", ICON_DRAWBAR},
        {"SLICE", ICON_SLICE},
        {"GRAIN", ICON_GRAIN},
        {"DRUM", ICON_DRUM},
    };
    uint32_t i;
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        if (str_eq(name, M[i].label))
            return M[i].icon;
    return ICON_GENERIC;
}

/* 2-bit icon, ink levels 1..3 = a third .. all of colour c (blended onto black, like cv_text) */
UI_SMALL static void cv_icon(int32_t x, int32_t y, uint32_t id, uint16_t c)
{
    uint16_t ramp[4];
    uint32_t a, i, j;
    const uint8_t *p;
    if (id >= FELUCCA_ICONS_N)
        return;
    for (a = 0; a < 4u; a++)
        ramp[a] = rgb565_over(c, a, 3u);               /* (2.7: onto the background; on black as before) */
    p = ICON_DATA[id];
    for (j = 0; j < ICON_CELL; j++)
        for (i = 0; i < ICON_CELL; i++) {
            uint32_t k = j * ICON_CELL + i, v = (p[k >> 2] >> (6u - 2u * (k & 3u))) & 3u;
            if (v)
                cv_pset(x + (int32_t)i, y + (int32_t)j, ramp[v]);
        }
}
#else
UI_SMALL static uint32_t icon_for_label(const char *l) { (void)l; return ICON_NONE; }
UI_SMALL static uint32_t param_icon(const param_desc_t *d, int32_t v) { (void)d; (void)v; return ICON_NONE; }
UI_SMALL static uint32_t engine_icon(const char *name) { (void)name; return ICON_NONE; }
UI_SMALL static void cv_icon(int32_t x, int32_t y, uint32_t id, uint16_t c) { (void)x; (void)y; (void)id; (void)c; }
#endif

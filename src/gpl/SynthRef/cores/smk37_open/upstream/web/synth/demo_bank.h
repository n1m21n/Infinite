/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Synthesized vocal samples for browser initialization. */
#ifndef FM1_DEMO_BANK_H
#define FM1_DEMO_BANK_H
#include <math.h>

static void demo_put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8*i));
}
struct demo_formant { double a, r2, gain, y1, y2; };
static const uint8_t *demo_bank(unsigned *bytes)
{
    static uint8_t *bank;
    static unsigned total;
    static const char *const names[SAMPLE_DRUM_PADS] = {
        "BWOOM", "PAH", "HEY", "WAH", "HAHA", "YOI", "TSI", "SHAA"
    };
    /* Bass/backbeat/response, three character sounds, closed/open mouth hats.
     * Five-character names also remain distinct on the hardware LCD. */
    static const unsigned duration[SAMPLE_DRUM_PADS] = {14080,8960,10240,15360,30080,14720,2880,10240};
    static const double vowels[5][3] = {
        {330,850,2400}, {760,1250,2700}, {500,1900,2650},
        {310,2300,3000}, {460,900,2500}
    }; /* oo, ah, eh, ee, oh */
    static const uint8_t from[SAMPLE_DRUM_PADS] = {0,1,2,0,1,4,3,1};
    static const uint8_t to[SAMPLE_DRUM_PADS] = {4,1,3,1,1,3,3,1};
    static const double pitch[SAMPLE_DRUM_PADS][2] = {
        {135,64},{185,135},{245,175},{140,95},{260,170},{170,340},{250,250},{190,130}
    };
    if (!bank) {
        uint8_t *candidate = calloc(SAMPLE_DRUM_BANK_BYTES, 1);
        float *work = malloc(32000 * sizeof *work);
        int16_t *pcm = malloc(32000 * sizeof *pcm);
        unsigned offset = SAMPLE_DRUM_BANK_HEADER;
        uint32_t random = 1;
        if (!candidate || !work || !pcm) goto failed;
        for (unsigned pad = 0; pad < SAMPLE_DRUM_PADS; ++pad) {
            struct demo_formant filter[3] = {{0}};
            const unsigned frames = duration[pad];
            double phase = 0, previous = 0, x1 = 0, x2 = 0, low_noise = 0;
            double peak = 0, mean = 0;
            for (unsigned i = 0; i < frames; ++i) {
                const double t = i / 32000.0, u = (double)i / (frames - 1);
                double slide = 1-exp(-u*4), envelope = exp(-u*(pad == 0 ? 5 : 2.2));
                double hz = pitch[pad][0] + (pitch[pad][1]-pitch[pad][0])*slide;
                if (pad == 4) {
                    /* Four separated, descending "ha" pulses. */
                    double syllable = t*4.7, pulse = syllable-floor(syllable);
                    envelope *= sin(3.141592653589793*fmin(1,pulse/0.72));
                    hz += 45*sin(6.283185307179586*pulse);
                }
                if (pad == 5) hz += 35*sin(u*3.141592653589793);
                hz *= 1 + 0.012*sin(t*33);
                phase += hz/32000; phase -= floor(phase);
                double glottal = phase < 0.40 ? 0.5-0.5*cos(3.141592653589793*phase/0.40)
                                 : phase < 0.58 ? cos(3.141592653589793*(phase-0.40)/0.36) : 0;
                random = random*1664525u + 1013904223u;
                double noise = (double)(random >> 8)/8388608.0-1;
                low_noise += (noise-low_noise)*0.18;
                double breath = noise-low_noise;
                double source = (glottal-previous)*140/hz + noise*0.004;
                previous = glottal;
                if (pad == 6) source = noise*0.20;
                if (pad == 7) source = source*0.6 + noise*0.12;
                double voice = 0;
                for (unsigned j = 0; j < 3; ++j) {
                    struct demo_formant *f = &filter[j];
                    if (!(i & 31U)) {
                        double frequency = vowels[from[pad]][j] + (vowels[to[pad]][j]-vowels[from[pad]][j])*slide;
                        double r = exp(-3.141592653589793*(90+60*j)/32000);
                        f->a = 2*r*cos(6.283185307179586*frequency/32000);
                        f->r2 = r*r; f->gain = (1-r)*0.5;
                    }
                    double y = f->gain*(source-x2) + f->a*f->y1-f->r2*f->y2;
                    f->y2 = f->y1; f->y1 = y;
                    voice += y*(j == 0 ? 1 : j == 1 ? 0.75 : 0.35);
                }
                x2 = x1; x1 = source;
                double value = voice*14;
                if (pad == 0) value += 0.22*sin(6.283185307179586*phase)*exp(-t*9);
                if (pad == 1 || pad == 2) value += breath*0.18*exp(-t*100);
                if (pad == 6) value += breath*0.28*exp(-t*48);
                if (pad == 7) value += breath*0.12*exp(-t*10);
                value *= envelope;
                work[i] = (float)value; mean += value;
            }
            mean /= frames;
            for (unsigned i = 0; i < frames; ++i) {
                double fade = fmin(1, fmin(i/48.0, (frames-1-i)/512.0));
                work[i] = (float)((work[i]-mean)*fade);
                if (fabs(work[i]) > peak) peak = fabs(work[i]);
            }
            if (!(peak > 0)) goto failed;
            double level = pad == 0 ? 23500 : pad == 6 || pad == 7 ? 16500 : 21000;
            for (unsigned i = 0; i < frames; ++i) pcm[i] = (int16_t)(work[i]*level/peak);
            unsigned size = ((frames+SAMPLE_DRUM_CHUNK_FRAMES-1)/SAMPLE_DRUM_CHUNK_FRAMES)*SAMPLE_DRUM_CHUNK_BYTES;
            if (size > SAMPLE_DRUM_BANK_BYTES-offset) goto failed;
            uint8_t *encoded = fm1_sea_encode(pcm, frames, sample_dqt);
            if (!encoded) goto failed;
            uint8_t *entry = candidate+32+pad*28;
            demo_put32(entry,offset); demo_put32(entry+4,frames); demo_put32(entry+8,size);
            memcpy(entry+12,names[pad],strlen(names[pad]));
            memcpy(candidate+offset,encoded,size); offset += size; free(encoded);
        }
        /* Existing 512-byte, 16-entry disk layout; the last eight are empty.
         * Eight playable pads; retain the compatible on-disk format. */
        for (unsigned pad = SAMPLE_DRUM_PADS; pad < 16; ++pad) demo_put32(candidate+32+pad*28,offset);
        demo_put32(candidate,0x53314d46); demo_put32(candidate+4,1);
        demo_put32(candidate+8,offset); demo_put32(candidate+20,32000);
        uint32_t crc = UINT32_MAX;
        for (unsigned i = 0; i < offset; ++i) {
            crc ^= candidate[i];
            for (unsigned b = 0; b < 8; ++b) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1)));
        }
        demo_put32(candidate+12,~crc);
        free(work); free(pcm); total = offset; bank = candidate;
        goto ready;
failed:
        free(work); free(pcm); free(candidate);
        if (bytes) *bytes = 0;
        return NULL;
    }
ready:
    if (bytes) *bytes = total;
    return bank;
}
#endif
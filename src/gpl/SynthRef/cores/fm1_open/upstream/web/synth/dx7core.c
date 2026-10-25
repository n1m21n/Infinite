/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#include "engine.h"
#include <stddef.h>
#include <string.h>
#include "dx7synth.h"
#include "dx7core.h"

enum { BIT_DEPTH = 14, ENV_MAX = 1 << BIT_DEPTH };
enum FmOperatorFlags {
    OUT_BUS_ONE = 1 << 0,
    OUT_BUS_TWO = 1 << 1,
    OUT_BUS_ADD = 1 << 2,
    IN_BUS_ONE = 1 << 4,
    IN_BUS_TWO = 1 << 5,
    FB_IN = 1 << 6,
    FB_OUT = 1 << 7
};

/* DX7 phase is a modulo-2^32 accumulator.  Spell the wrap in unsigned
 * arithmetic so -Ofast cannot treat a signed overflow as unreachable. */
FM1_DSP_CODE static inline int phase_add(int phase, int delta)
{
    return (int)((u32)phase + (u32)delta);
}

FM1_DSP_CODE static inline int phase_advance_block_64(int phase, int frequency)
{
    return (int)((u32)phase +
                 (u32)frequency * (u32)SYNTH_DX7_BLOCK_SAMPLES);
}


static FM1_DSP_DATA const u8 algorithms[32][FM_OP_CNT] = {
    { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, // 1
    { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 }, // 2
    { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 }, // 3
    { 0x41, 0x11, 0x94, 0x01, 0x11, 0x14 }, // 4
    { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, // 5
    { 0x41, 0x94, 0x01, 0x14, 0x01, 0x14 }, // 6
    { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, // 7
    { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 }, // 8
    { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 }, // 9
    { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 }, // 10
    { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, // 11
    { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 }, // 12
    { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, // 13
    { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 }, // 14
    { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 }, // 15
    { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 }, // 16
    { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, // 17
    { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 }, // 18
    { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, // 19
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 }, // 20
    { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 }, // 21
    { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 }, // 22
    { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, // 23
    { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 }, // 24
    { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, // 25
    { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 }, // 26
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 }, // 27
    { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 }, // 28
    { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, // 29
    { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 }, // 30
    { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, // 31
    { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 }, // 32
};

FM1_DSP_CODE static void compute_fb4(int *output,t_dx7op_params *params,int gaincur, int gaindst,int *fb_buf, int feedback_shift)
{
    int dgain[3];
    int gain[3];
    int phase[3];
    int y0 = fb_buf[0];
    int y = fb_buf[1];

    phase[0] = params[0].phase;
    phase[1] = params[1].phase;
    phase[2] = params[2].phase;

    // gaincur 已是换算后的衰减域值(0=最响,ENV_MAX=静音)，0 为首块哨兵，从静音淡入；
    // gaindst 是 dx7note 本块写入的原始包络，这里按主循环规则换算并写回
    int g1_op1 = params[1].gaincur == 0 ? (ENV_MAX - 1) : params[1].gaincur;
    int g2_op1 = ENV_MAX - (params[1].gaindst >> 14);
    int g1_op2 = params[2].gaincur == 0 ? (ENV_MAX - 1) : params[2].gaincur;
    int g2_op2 = ENV_MAX - (params[2].gaindst >> 14);
    params[1].gaindst = g2_op1;
    params[2].gaindst = g2_op2;

    gain[0] = gaincur;
    gain[1] = g1_op1;
    gain[2] = g1_op2;

    dgain[0] = (gaindst-gaincur+32)>>6;
    dgain[1] = (g2_op1 - g1_op1 + 32) >> 6;
    dgain[2] = (g2_op2 - g1_op2 + 32) >> 6;

    for(int i = 0;i < 64; i++){
        int scaled_fb = (y0 + y) >> (feedback_shift + 1);

        // op 0
        gain[0] += dgain[0];
        y0 = y;
        y = mk_sin(phase_add(phase[0], scaled_fb),gain[0]);
        phase[0] = phase_add(phase[0], params[0].freq);

        // op 1
        gain[1] += dgain[1];
        y = mk_sin(phase_add(phase[1], y),gain[1]);
        phase[1] = phase_add(phase[1], params[1].freq);

        // op 2
        gain[2] += dgain[2];
        y = mk_sin(phase_add(phase[2], y),gain[2]);
        phase[2] = phase_add(phase[2], params[2].freq);

        output[i] = y;

    }
    fb_buf[0] = y0;
    fb_buf[1] = y;

}

FM1_DSP_CODE static void compute_fb6(int *output,t_dx7op_params *params,int gaincur, int gaindst,int *fb_buf, int feedback_shift)
{
    int dgain[2];
    int gain[2];
    int phase[2];
    int y0 = fb_buf[0];
    int y = fb_buf[1];

    phase[0] = params[0].phase;
    phase[1] = params[1].phase;


    // gaincur 已是换算后的衰减域值(0=最响,ENV_MAX=静音)，0 为首块哨兵，从静音淡入；
    // gaindst 是 dx7note 本块写入的原始包络，这里按主循环规则换算并写回
    int g1_op1 = params[1].gaincur == 0 ? (ENV_MAX - 1) : params[1].gaincur;
    int g2_op1 = ENV_MAX - (params[1].gaindst >> 14);
    params[1].gaindst = g2_op1;

    gain[0] = gaincur;
    gain[1] = g1_op1;

    dgain[0] = (gaindst-gaincur+32)>>6;
    dgain[1] = (g2_op1 - g1_op1 + 32) >> 6;


    for(int i = 0;i < 64; i++){
        int scaled_fb = (y0 + y) >> (feedback_shift + 1);

        // op 0
        gain[0] += dgain[0];
        y0 = y;
        y = mk_sin(phase_add(phase[0], scaled_fb),gain[0]);
        phase[0] = phase_add(phase[0], params[0].freq);

        // op 1
        gain[1] += dgain[1];
        y = mk_sin(phase_add(phase[1], y),gain[1]);
        phase[1] = phase_add(phase[1], params[1].freq);

        output[i] = y;

    }
    fb_buf[0] = y0;
    fb_buf[1] = y;

}

#define DX7_OPERATOR_WRAPPER static

DX7_OPERATOR_WRAPPER void compute_64(int *output, const int *input,
                                     int phase, int freq, int gaincur,
                                     int gaindst, u8 add)
{
    int dgain = (gaindst - gaincur +
                 (SYNTH_DX7_BLOCK_SAMPLES >> 1)) >> 6;
    if (add) {
      for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; i++) {
        gaincur += dgain;
        *output++ += mk_sin(phase_add(phase, *input++), gaincur);
        phase = phase_add(phase, freq);
      }
    }
    else {
        for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; i++) {
        gaincur += dgain;
        *output++ = mk_sin(phase_add(phase, *input++), gaincur);
        phase = phase_add(phase, freq);
        }
    }
}

DX7_OPERATOR_WRAPPER void compute_pure_64(int *output, int phase, int freq,
                                          int gaincur, int gaindst, u8 add)
{
    int dgain = (gaindst - gaincur +
                 (SYNTH_DX7_BLOCK_SAMPLES >> 1)) >> 6;
    if (add) {
        for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; i++) {
            gaincur += dgain;
            *output++ += (mk_sin(phase,gaincur));
            phase = phase_add(phase, freq);
        }
    }
    else {
        for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; i++) {
            gaincur += dgain;
            *output++ = (mk_sin(phase,gaincur));
            phase = phase_add(phase, freq);
      }
    }
}
DX7_OPERATOR_WRAPPER void compute_fb_64(int *output, int phase, int freq,
                                        int gaincur, int gaindst,
                                        int *fb_buf, int fb_shift, u8 add)
{
    int dgain = (gaindst - gaincur +
                 (SYNTH_DX7_BLOCK_SAMPLES >> 1)) >> 6;
    int y0 = fb_buf[0];
    int y = fb_buf[1];
    if (add) {
        for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; i++) {
            gaincur += dgain;
            int scaled_fb = (y0 + y) >> (fb_shift+1);
            y0 = y;
            y = mk_sin(phase_add(phase, scaled_fb),gaincur);
            *output++ += y;
            phase = phase_add(phase, freq);
        }
    }
    else {
        for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; i++) {
            gaincur += dgain;
            int scaled_fb = (y0 + y) >> (fb_shift+1);
            y0 = y;
            y = mk_sin(phase_add(phase, scaled_fb),gaincur);
            *output++ = y;
            phase = phase_add(phase, freq);
        }
    }
  fb_buf[0] = y0;
  fb_buf[1] = y;
}

#undef DX7_OPERATOR_WRAPPER

FM1_DSP_CODE void dx7core_compute_64(int *output, int *scratch, t_dx7op_params *params,
                        int algorithm, int *fb_buf, int feedback_shift)
{
    const int kLevelThresh = ENV_MAX-100;
    int *buf = scratch;
    /* All three buses are guarded by has_contents.  A bus's first producer is
     * forced to overwrite the full block, so the normal operator path needs no
     * speculative clear.  If no carrier produces the final output at all, the
     * block is cleared once after dispatch to avoid reusing a preceding block. */
    u8  has_contents[3] = {0,0,0};
    const u8 *alg;
    bool fb_on = feedback_shift < 16;

    algorithm &= 31;
    alg = algorithms[algorithm];

    for (int op = 0; op < FM_OP_CNT; op++) {
        int flags = alg[op];
        if (op == 0 && fb_on && (algorithm == 3 || algorithm == 5))
            flags = 0xc4;
        u8 add = (flags & OUT_BUS_ADD) != 0;
        t_dx7op_params *param = &params[op];
        int inbus = (flags >> 4) & 3;
        int outbus = flags & 3;
        int *outptr = (outbus == 0) ? output :
                      buf + (outbus - 1) * SYNTH_DX7_BLOCK_SAMPLES;
        int gain1 = param->gaincur ==0? (ENV_MAX - 1):param->gaincur;
        int gain2 =ENV_MAX-(param->gaindst>>14);
        param->gaindst = gain2;
        if (gain1 <= kLevelThresh || gain2 <= kLevelThresh) {
          if (!has_contents[outbus]) add = false;
          if (inbus == 0 || !has_contents[inbus]) {
            if ((flags & 0xc0) == 0xc0 && fb_on) {
                switch(algorithm)
                {
                  case 3:
                    compute_fb4(outptr,params,gain1,gain2,fb_buf,DSP_MIN(feedback_shift+2,16));
                    params[1].phase = phase_advance_block_64(
                        params[1].phase, params[1].freq);
                    params[2].phase = phase_advance_block_64(
                        params[2].phase, params[2].freq);
                    op += 2;
                    break;

                  case 5:
                    compute_fb6(outptr,params,gain1,gain2,fb_buf,DSP_MIN(feedback_shift+2,16));
                    params[1].phase = phase_advance_block_64(
                        params[1].phase, params[1].freq);
                    op++;
                    break;

                  case 31:
                    compute_fb_64(outptr, param->phase, param->freq, gain1,
                                  gain2, fb_buf,
                                  DSP_MIN(feedback_shift + 2, 16), add);
                    break;
                  default:
                    compute_fb_64(outptr, param->phase, param->freq, gain1,
                                  gain2, fb_buf, feedback_shift, add);
                    break;
                }
            }
            else compute_pure_64(outptr, param->phase, param->freq, gain1,
                                 gain2, add);
          }
          else compute_64(outptr,
                          buf + (inbus - 1) * SYNTH_DX7_BLOCK_SAMPLES,
                          param->phase, param->freq, gain1, gain2, add);
          has_contents[outbus] = true;
        }
        else if(!add) has_contents[outbus] = false;
        param->phase = phase_advance_block_64(param->phase, param->freq);
    }

    if (!has_contents[0]) {
        for (int i = 0; i < SYNTH_DX7_BLOCK_SAMPLES; ++i)
            output[i] = 0;
    }
}

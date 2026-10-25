/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020 Electrosmith, Emilie Gillet; 2026 M-VAVE.
 * DaisySP drum adaptation with FM1 supplementary percussion. */
#pragma once
#include "drum808.h"
#include "dsp_math.h"
enum {D8S_BD,D8S_SD,D8S_LT,D8S_MT,D8S_HT,D8S_LC,D8S_MC,D8S_HC,
      D8S_RS,D8S_CL,D8S_MA,D8S_CP,D8S_CB,D8S_CH,D8S_OH,D8S_CY,D8S_NUM};
enum {D8P_LEVEL,D8P_TUNE,D8P_DECAY,D8P_DRIVE,D8P_DIST,D8P_REV,D8P_DLY,D8P_X1,D8P_X2,D8P_NUM};
typedef struct {
    uint8_t active,sound;
    unsigned age,quiet,pulse_count,fm_count;
    uint32_t random,phase[6],increment[6];
    float level,env,decay,pulse,pulse_lp,fm_lp,retrigger,low,tone;
    float frequency,q,accent,attack,snappy,gain,velocity,rev,dly,drive,drive_norm,crush_step;
    float peak,last,tail_step,tone_coefficient,pulse_gain,choke,choke_decay,transient_decay;
    dsp_svf_t filter[4];
} d8_voice_t;
typedef struct {
    uint8_t pot[D8S_NUM][D8P_NUM],sw[D8_NUM],tpan[D8_NUM],kpot[3];
    d8_voice_t bd,sd,tom[3],rs,cl,ma,cp,cb,ch,oh,cy;
    float pan_l[D8_NUM],pan_r[D8_NUM],pan_lc[D8_NUM],pan_rc[D8_NUM];
} drum808_t;
void drum808_init(drum808_t *d);
void drum808_trigger(drum808_t *d,int part,float velocity);
void drum808_set(drum808_t *d,int part,int parameter,int value);
int drum808_get(const drum808_t *d,int part,int parameter);
int drum808_active(const drum808_t *d);
void drum808_render(drum808_t *d,float *dry,float *rev,float *dly,int count);
void drum808_render_st(drum808_t *d,float *left,float *right,float *rev,float *dly,int count);
void drum808_pan_settle(drum808_t *d);
void drum808_advance_idle(drum808_t *d,int count);

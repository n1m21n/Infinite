/* SPDX-License-Identifier: MIT
 * License: see ../../THIRD_PARTY.md.
 * FM1 adaptation 2026-10-09: audio placement and const volume tables. */
/* emu2149.h */
#ifndef _EMU2149_H_
#define _EMU2149_H_

#include <stdint.h>
#include "engine.h"

#define PSG_MASK_CH(x) (1<<(x))

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct __PSG
  {

    /* Volume Table */
    const uint32_t *voltbl;

    uint8_t reg[0x20];
    int32_t out;

    uint32_t clk, rate, base_incr;
    uint8_t quality;
    uint8_t clk_div;

    uint16_t count[3];
    uint8_t volume[3];
    uint16_t freq[3];
    uint8_t edge[3];
    uint8_t tmask[3];
    uint8_t nmask[3];
    uint32_t mask;

    uint32_t base_count;

    uint8_t env_ptr;
    uint8_t env_face;

    uint8_t env_continue;
    uint8_t env_attack;
    uint8_t env_alternate;
    uint8_t env_hold;
    uint8_t env_pause;

    uint16_t env_freq;
    uint32_t env_count;

    uint32_t noise_seed;
    uint8_t noise_scaler;
    uint8_t noise_count;
    uint8_t noise_freq;

    /* rate converter */
    uint32_t realstep;
    uint32_t psgtime;
    uint32_t psgstep;

    uint32_t freq_limit;

    /* I/O Ctrl */
    uint8_t adr;

    /* output of channels */
    int16_t ch_out[3];

  } PSG;

  FM1_DSP_CODE void PSG_setQuality (PSG * psg, uint8_t q);
  FM1_DSP_CODE void PSG_setClock(PSG *psg, uint32_t clk);
  FM1_DSP_CODE void PSG_setClockDivider(PSG *psg, uint8_t enable);
  FM1_DSP_CODE void PSG_setRate (PSG * psg, uint32_t rate);
  PSG *PSG_new (uint32_t clk, uint32_t rate);
  FM1_DSP_CODE void PSG_reset (PSG *);
  FM1_DSP_CODE void PSG_delete (PSG *);
  FM1_DSP_CODE void PSG_writeReg (PSG *, uint32_t reg, uint32_t val);
  FM1_DSP_CODE void PSG_writeIO (PSG * psg, uint32_t adr, uint32_t val);
  FM1_DSP_CODE uint8_t PSG_readReg (PSG * psg, uint32_t reg);
  FM1_DSP_CODE uint8_t PSG_readIO (PSG * psg);
  FM1_DSP_CODE int16_t PSG_calc (PSG *);
  /* Add unscaled chip output to a disjoint int32 block, without allocation. */
  FM1_DSP_CODE void PSG_mix (PSG *, int32_t *output, unsigned frames);
  FM1_DSP_CODE void PSG_setVolumeMode (PSG * psg, int type);
  FM1_DSP_CODE uint32_t PSG_setMask (PSG *, uint32_t mask);
  FM1_DSP_CODE uint32_t PSG_toggleMask (PSG *, uint32_t mask);

#ifdef __cplusplus
}
#endif

/* deprecated interfaces */
#define PSG_set_quality PSG_setQuality
#define PSG_set_rate PSG_setRate
#define PSG_set_clock PSG_setClock

#endif

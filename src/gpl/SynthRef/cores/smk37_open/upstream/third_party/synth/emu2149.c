/* SPDX-License-Identifier: MIT
 * License: see ../../THIRD_PARTY.md.
 * FM1 adaptation 2026-10-09: SRAM placement, const tables, and clear the
 * channel output when the original Nyquist guard mutes a high note. */
/**
 * emu2149 v1.42
 * https://github.com/digital-sound-antiques/emu2149
 * Copyright (C) 2001-2022 Mitsutaka Okazaki
 *
 * This source refers to the following documents. The author would like to thank all the authors who have
 * contributed to the writing of them.
 * - psg.vhd        -- 2000 written by Kazuhiro Tsujikawa.
 * - s_fme7.c       -- 1999,2000 written by Mamiya (NEZplug).
 * - ay8910.c       -- 1998-2001 Author unknown (MAME).
 * - MSX-Datapack   -- 1991 ASCII Corp.
 * - AY-3-8910 data sheet
 */
#include <stdlib.h>
#include <string.h>
#include "emu2149.h"
#include <stddef.h>

static FM1_DSP_DATA const uint32_t voltbl[2][32] = {
  /* YM2149 - 32 steps */
  {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09,
   0x0B, 0x0D, 0x0F, 0x12,
   0x16, 0x1A, 0x1F, 0x25, 0x2D, 0x35, 0x3F, 0x4C, 0x5A, 0x6A, 0x7F, 0x97,
   0xB4, 0xD6, 0xFF, 0xFF},
  /* AY-3-8910 - 16 steps */
  {0x00, 0x00, 0x03, 0x03, 0x04, 0x04, 0x06, 0x06, 0x09, 0x09, 0x0D, 0x0D,
   0x12, 0x12, 0x1D, 0x1D,
   0x22, 0x22, 0x37, 0x37, 0x4D, 0x4D, 0x62, 0x62, 0x82, 0x82, 0xA6, 0xA6,
   0xD0, 0xD0, 0xFF, 0xFF}
};

static FM1_DSP_DATA const uint8_t regmsk[16] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0x3f,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff
};

#define GETA_BITS 24

static FM1_DSP_CODE void
internal_refresh (PSG * psg)
{
  uint32_t f_master = psg->clk;

  if (psg->clk_div)
    f_master /= 2;

  if (psg->quality)
  {
    psg->base_incr = 1 << GETA_BITS;
    psg->realstep = f_master;
    psg->psgstep = psg->rate * 8;
    psg->psgtime = 0;
    psg->freq_limit = (uint32_t)(f_master / 16 / (psg->rate / 2));
  }
  else
  {
    psg->base_incr = (uint32_t)((double)f_master * (1 << GETA_BITS) / 8 / psg->rate);
    psg->freq_limit = 0;
  }
}

void
PSG_setClock(PSG *psg, uint32_t clock)
{
  if (psg->clk != clock) {
    psg->clk = clock;
    internal_refresh(psg);
  }
}

void
PSG_setClockDivider(PSG *psg, uint8_t enable)
{
  if (psg->clk_div != enable) {
    psg->clk_div = enable;
    internal_refresh (psg);
  }
}

void
PSG_setRate (PSG * psg, uint32_t rate)
{
  uint32_t r = rate ? rate : 44100;
  if (psg->rate != r) {
    psg->rate = r;
    internal_refresh(psg);
  }
}

void
PSG_setQuality (PSG * psg, uint8_t q)
{
  if (psg->quality != q) {
    psg->quality = q;
    internal_refresh(psg);
  }
}

PSG *
PSG_new (uint32_t clock, uint32_t rate)
{
  PSG *psg;

  psg = (PSG *) calloc (1, sizeof (PSG));
  if (psg == NULL)
    return NULL;

  PSG_setVolumeMode(psg, 0);
  psg->clk = clock;
  psg->clk_div = 0;
  psg->rate = rate ? rate : 44100;
  psg->quality = 0;
  internal_refresh(psg);
  PSG_setMask(psg, 0x00);
  return psg;
}

void
PSG_setVolumeMode (PSG * psg, int type)
{
  switch (type)
  {
  case 1:
    psg->voltbl = voltbl[0]; /* YM2149 */
    break;
  case 2:
    psg->voltbl = voltbl[1]; /* AY-3-8910 */
    break;
  default:
    psg->voltbl = voltbl[0]; /* fallback: YM2149 */
    break;
  }
}

uint32_t
PSG_setMask (PSG *psg, uint32_t mask)
{
  uint32_t ret = 0;
  if(psg)
  {
    ret = psg->mask;
    psg->mask = mask;
  }
  return ret;
}

uint32_t
PSG_toggleMask (PSG *psg, uint32_t mask)
{
  uint32_t ret = 0;
  if(psg)
  {
    ret = psg->mask;
    psg->mask ^= mask;
  }
  return ret;
}

void
PSG_reset (PSG * psg)
{
  int i;

  psg->base_count = 0;

  for (i = 0; i < 3; i++)
  {
    psg->count[i] = 0;
    psg->freq[i] = 0;
    psg->edge[i] = 0;
    psg->volume[i] = 0;
    psg->ch_out[i] = 0;
  }

  psg->mask = 0;

  for (i = 0; i < 16; i++)
    psg->reg[i] = 0;
  psg->adr = 0;

  psg->noise_seed = 0xffff;
  psg->noise_scaler = 0;
  psg->noise_count = 0;
  psg->noise_freq = 0;

  psg->env_ptr = 0;
  psg->env_freq = 0;
  psg->env_count = 0;
  psg->env_pause = 1;

  psg->out = 0;

}

void
PSG_delete (PSG * psg)
{
  free (psg);
}

uint8_t
PSG_readIO (PSG * psg)
{
  return (uint8_t) (psg->reg[psg->adr]);
}

uint8_t
PSG_readReg (PSG * psg, uint32_t reg)
{
  return (uint8_t) (psg->reg[reg & 0x1f]);
}

void
PSG_writeIO (PSG * psg, uint32_t adr, uint32_t val)
{
  if (adr & 1)
    PSG_writeReg (psg, psg->adr, val);
  else
    psg->adr = val & 0x1f;
}

static FM1_DSP_CODE inline void
update_output (PSG * psg)
{

  int i, noise;
  uint8_t incr;

  psg->base_count += psg->base_incr;
  incr = (psg->base_count >> GETA_BITS);
  psg->base_count &= (1 << GETA_BITS) - 1;

  /* Envelope */
  psg->env_count += incr;

  if (psg->env_count >= psg->env_freq)
  {
    if (!psg->env_pause)
    {
      if(psg->env_face)
        psg->env_ptr = (psg->env_ptr + 1) & 0x3f ;
      else
        psg->env_ptr = (psg->env_ptr + 0x3f) & 0x3f;
    }

    if (psg->env_ptr & 0x20) /* if carry or borrow */
    {
      if (psg->env_continue)
      {
        if (psg->env_alternate^psg->env_hold) psg->env_face ^= 1;
        if (psg->env_hold) psg->env_pause = 1;
        psg->env_ptr = psg->env_face ? 0 : 0x1f;
      }
      else
      {
        psg->env_pause = 1;
        psg->env_ptr = 0;
      }
    }

    if (psg->env_freq >= incr)
      psg->env_count -= psg->env_freq;
    else
      psg->env_count = 0;
  }

  /* Noise */
  psg->noise_count += incr;
  if (psg->noise_count >= psg->noise_freq)
  {
    psg->noise_scaler ^= 1;
    if (psg->noise_scaler)
    {
      if (psg->noise_seed & 1)
        psg->noise_seed ^= 0x24000;
      psg->noise_seed >>= 1;
    }

    if (psg->noise_freq >= incr)
      psg->noise_count -= psg->noise_freq;
    else
      psg->noise_count = 0;
  }
  noise = psg->noise_seed & 1;

  /* Tone */
  for (i = 0; i < 3; i++)
  {
    psg->count[i] += incr;
    if (psg->count[i] >= psg->freq[i])
    {
      psg->edge[i] = !psg->edge[i];

      if (psg->freq[i] >= incr)
        psg->count[i] -= psg->freq[i];
      else
        psg->count[i] = 0;
    }

    if (0 < psg->freq_limit && psg->freq[i] <= psg->freq_limit && psg->nmask[i])
    {
      /* Mute the channel if the pitch is higher than the Nyquist frequency at the current sample rate,
       * to prevent aliased or broken tones from being generated. Of course, this logic doesn't exist
       * on the actual chip, but practically all tones higher than the Nyquist frequency are usually
       * removed by a low-pass circuit somewhere, so we here halt the output. */
      psg->ch_out[i] = 0;
      continue;
    }

    if (psg->mask & PSG_MASK_CH(i))
    {
      psg->ch_out[i] = 0;
      continue;
    }

    if ((psg->tmask[i]||psg->edge[i]) && (psg->nmask[i]||noise))
    {
      if (!(psg->volume[i] & 32))
        psg->ch_out[i] = (psg->voltbl[psg->volume[i] & 31] << 4);
      else
        psg->ch_out[i] = (psg->voltbl[psg->env_ptr] << 4);
    }
    else
    {
      psg->ch_out[i] = 0;
    }
  }
}

static FM1_DSP_CODE inline int16_t
mix_output(PSG *psg)
{
  return (int16_t)(psg->ch_out[0] + psg->ch_out[1] + psg->ch_out[2]);
}

int16_t
PSG_calc (PSG * psg)
{
  if (!psg->quality)
  {
    update_output(psg);
    psg->out = mix_output(psg);
  }
  else
  {
    /* Simple rate converter. */
    while (psg->realstep > psg->psgtime)
    {
      psg->psgtime += psg->psgstep;
      update_output(psg);
      psg->out += mix_output(psg);
      psg->out >>= 1;
    }
    psg->psgtime -= psg->realstep;
  }
  return psg->out;
}

/* YM/AY tone mode uses fixed channel volumes, with noise disconnected from
 * the mixer and the envelope stopped. Keep the original chip-clock filter;
 * cache channel controls and advance the inaudible noise state once a block. */
static FM1_DSP_CODE int
fixed_block_ready(const PSG *p, unsigned frames)
{
  if (!frames || frames > 128 || !p->quality ||
      p->base_incr != (1U << GETA_BITS) || p->base_count >= (1U << GETA_BITS) ||
      p->realstep != 1789772 || p->psgstep != 352800 || p->psgtime >= p->psgstep ||
      !p->env_pause || p->env_ptr >= 32 || p->env_freq || p->env_count ||
      p->noise_scaler > 1)
    return 0;
  return 1;
}
static FM1_DSP_CODE int
tone_block_ready(const PSG *p, unsigned frames)
{
  if (!fixed_block_ready(p, frames)) return 0;
  for (unsigned i = 0; i < 3; ++i)
    if (!p->nmask[i] || p->tmask[i] || (p->volume[i] & 32) || p->edge[i] > 1)
      return 0;
  return 1;
}

static FM1_DSP_CODE void
tone_noise_advance(PSG *p, unsigned clocks)
{
  unsigned count = p->noise_count, frequency = p->noise_freq, flips;
  if (!frequency) {
    count = 0;
    flips = clocks;
  } else if (count < frequency) {
    flips = (count + clocks) / frequency;
    count = (count + clocks) % frequency;
  } else {
    /* A live period change can leave the old count above the new period.
     * Preserve the original uint8 wrap and one subtraction per clock. */
    flips = 0;
    for (unsigned i = 0; i < clocks; ++i) {
      count = (uint8_t)(count + 1);
      if (count >= frequency) {
        count -= frequency;
        ++flips;
      }
    }
  }
  unsigned shifts = (flips + (p->noise_scaler == 0)) / 2;
  p->noise_count = (uint8_t)count;
  p->noise_scaler ^= (uint8_t)(flips & 1);
  uint32_t seed = p->noise_seed;
  while (shifts--) {
    if (seed & 1) seed ^= 0x24000;
    seed >>= 1;
  }
  p->noise_seed = seed;
}

static FM1_DSP_CODE void
mix_tone_block(PSG *p, int32_t *output, unsigned frames)
{
  unsigned clocks = (frames * 1789772U + 352799U - p->psgtime) / 352800U;
  unsigned period[3], edge[3], count[3];
  int32_t level[3];
  for (unsigned i = 0; i < 3; ++i) {
    period[i] = p->freq[i];
    edge[i] = p->edge[i];
    count[i] = p->count[i];
    level[i] = (p->mask & PSG_MASK_CH(i)) ||
                       (p->freq_limit && period[i] <= p->freq_limit)
                   ? 0 : (int16_t)(p->voltbl[p->volume[i] & 31] << 4);
  }
  uint32_t time = p->psgtime;
  int32_t sample = p->out;
  for (unsigned frame = 0; frame < frames; ++frame) {
    while (time < 1789772U) {
      time += 352800U;
      int32_t sum = 0;
      for (unsigned i = 0; i < 3; ++i) {
        count[i] = (uint16_t)(count[i] + 1);
        if (count[i] >= period[i]) {
          edge[i] ^= 1;
          count[i] = period[i] ? count[i] - period[i] : 0;
        }
        if (edge[i]) sum += level[i];
      }
      sample = (sample + (int16_t)sum) >> 1;
    }
    time -= 1789772U;
    output[frame] += (int16_t)sample;
  }
  p->psgtime = time;
  p->out = sample;
  for (unsigned i = 0; i < 3; ++i) {
    p->count[i] = (uint16_t)count[i];
    p->edge[i] = (uint8_t)edge[i];
    p->ch_out[i] = (int16_t)(edge[i] ? level[i] : 0);
  }
  tone_noise_advance(p, clocks);
}

/* The app's NOISE/MIX modes use fixed volumes and a paused envelope too.
 * Cache their eight possible channel sums once per block; preserve the chip
 * clock, its one-pole rate conversion and the exact LFSR sequence. Other
 * register combinations still use the unrestricted emulator. */
static FM1_DSP_CODE int
noise_block_ready(const PSG *p, unsigned frames)
{
  if (!fixed_block_ready(p, frames)) return 0;
  unsigned mask = p->tmask[0] != 0;
  for (unsigned i = 0; i < 3; ++i)
    if (p->nmask[i] || (p->tmask[i] != 0) != mask ||
        (p->volume[i] & 32) || p->edge[i] > 1 ||
        p->count[i] > 4095 || p->freq[i] > 4095)
      return 0;
  return 1;
}
static FM1_DSP_CODE void
noise_tones_advance(PSG *p, unsigned clocks)
{
  for (unsigned i = 0; i < 3; ++i) {
    unsigned count = p->count[i], period = p->freq[i], flips;
    if (!period) {
      count = 0;
      flips = clocks;
    } else if (count < period) {
      flips = (count + clocks) / period;
      count = (count + clocks) % period;
    } else {
      /* Changing a live period retains the old counter. It can take several
       * clocks to settle; one subtraction per clock is part of the sound. */
      flips = 0;
      for (unsigned n = 0; n < clocks; ++n) {
        if (++count >= period) { count -= period; ++flips; }
      }
    }
    p->count[i] = (uint16_t)count;
    p->edge[i] ^= (uint8_t)(flips & 1U);
  }
}
static FM1_DSP_CODE void
mix_noise_block(PSG *p, int32_t *output, unsigned frames)
{
  int32_t level[3], sums[8];
  for (unsigned i = 0; i < 3; ++i)
    level[i] = p->mask & PSG_MASK_CH(i) ? 0 : (int16_t)(p->voltbl[p->volume[i] & 31] << 4);
  for (unsigned i = 0; i < 8; ++i)
    sums[i] = (int16_t)((i & 1 ? level[0] : 0) +
                        (i & 2 ? level[1] : 0) + (i & 4 ? level[2] : 0));
  unsigned tones = !p->tmask[0];
  unsigned clocks = tones ? 0 : (frames * 1789772U + 352799U - p->psgtime) / 352800U;
  unsigned period[3], count[3], edges = p->edge[0] | (p->edge[1] << 1) | (p->edge[2] << 2);
  for (unsigned i = 0; i < 3; ++i) {
    period[i] = p->freq[i];
    count[i] = p->count[i];
  }
  unsigned noise_count = p->noise_count, scaler = p->noise_scaler;
  uint32_t seed = p->noise_seed, time = p->psgtime;
  int32_t sample = p->out;
  for (unsigned frame = 0; frame < frames; ++frame) {
    while (time < 1789772U) {
      time += 352800U;
      noise_count = (uint8_t)(noise_count + 1);
      if (noise_count >= p->noise_freq) {
        scaler ^= 1;
        if (scaler) {
          if (seed & 1) seed ^= 0x24000;
          seed >>= 1;
        }
        noise_count = p->noise_freq ? noise_count - p->noise_freq : 0;
      }
      if (tones) for (unsigned i = 0; i < 3; ++i) {
        if (++count[i] >= period[i]) {
          edges ^= 1U << i;
          count[i] = period[i] ? count[i] - period[i] : 0;
        }
      }
      sample = (sample + ((seed & 1) ? sums[tones ? edges : 7] : 0)) >> 1;
    }
    time -= 1789772U;
    output[frame] += (int16_t)sample;
  }
  p->out = sample;
  p->psgtime = time;
  p->noise_seed = seed;
  p->noise_count = (uint8_t)noise_count;
  p->noise_scaler = (uint8_t)scaler;
  if (tones) for (unsigned i = 0; i < 3; ++i) {
    p->count[i] = (uint16_t)count[i];
    p->edge[i] = (uint8_t)((edges >> i) & 1U);
  }
  if (!tones) noise_tones_advance(p, clocks);
  for (unsigned i = 0; i < 3; ++i)
    p->ch_out[i] = (int16_t)((p->noise_seed & 1) && (p->tmask[i] || p->edge[i]) ? level[i] : 0);
}

void
PSG_mix (PSG *psg, int32_t *output, unsigned frames)
{
  if (tone_block_ready(psg, frames)) {
    mix_tone_block(psg, output, frames);
    return;
  }
  if (noise_block_ready(psg, frames)) {
    mix_noise_block(psg, output, frames);
    return;
  }
  for (unsigned i = 0; i < frames; ++i)
    output[i] += PSG_calc(psg);
}

void
PSG_writeReg (PSG * psg, uint32_t reg, uint32_t val)
{
  int c;

  if (reg > 15) return;

  val &= regmsk[reg];

  psg->reg[reg] = (uint8_t) val;

  switch (reg)
  {
  case 0:
  case 2:
  case 4:
  case 1:
  case 3:
  case 5:
    c = reg >> 1;
    psg->freq[c] = ((psg->reg[c * 2 + 1] & 15) << 8) + psg->reg[c * 2];
    break;

  case 6:
    psg->noise_freq = val & 31;
    break;

  case 7:
    psg->tmask[0] = (val & 1);
    psg->tmask[1] = (val & 2);
    psg->tmask[2] = (val & 4);
    psg->nmask[0] = (val & 8);
    psg->nmask[1] = (val & 16);
    psg->nmask[2] = (val & 32);
    break;

  case 8:
  case 9:
  case 10:
    psg->volume[reg - 8] = val << 1;
    break;

  case 11:
  case 12:
    psg->env_freq = (psg->reg[12] << 8) + psg->reg[11];
    break;

  case 13:
    psg->env_continue = (val >> 3) & 1;
    psg->env_attack = (val >> 2) & 1;
    psg->env_alternate = (val >> 1) & 1;
    psg->env_hold = val & 1;
    psg->env_face = psg->env_attack;
    psg->env_pause = 0;
    psg->env_ptr = psg->env_face ? 0 : 0x1f;
    break;

  case 14:
  case 15:
  default:
    break;
  }

  return;
}

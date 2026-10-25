/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_FM_PATCH_H
#define FM1_FM_PATCH_H
#include "engine.h"
enum { FM1_FM_PROGRAMS = 128, FM1_FM_FACTORY_PROGRAMS = 16 };
/* Expanded DX7 voices; MIDI programs 0..127, panel positions 1..128. */
FM1_DSP_CODE void groove_fm_program(uint8_t patch[156], unsigned program);
/* Built-in DX7 bank. Demo projects keep their separate recipe patches. */
FM1_DSP_CODE void groove_fm_factory(uint8_t patch[156], unsigned program);
FM1_DSP_CODE void groove_fm_unpack(uint8_t patch[156], const uint8_t vmem[128]);
void groove_fm_pack(uint8_t vmem[128], const uint8_t patch[156]);
void groove_fm_library_init(void);
unsigned groove_fm_library_available(void);
int groove_fm_library_recover(void);
int groove_fm_library_read(unsigned bank, uint8_t vmem[4096]);
int groove_fm_library_write(unsigned bank, const uint8_t vmem[4096]);
#endif

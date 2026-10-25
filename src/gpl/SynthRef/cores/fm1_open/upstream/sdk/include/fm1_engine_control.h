/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_ENGINE_CONTROL_H
#define FM1_ENGINE_CONTROL_H
#include "fm1.h"
/* I2S/DMA interrupt only, once per block, never concurrently or recursively.
 * Fill FM1_BLOCK_FRAMES stereo frames with the instrument's signed Q23 output.
 * Master output gain, USB/Bluetooth mixing and recording belong to APP.
 * frames must equal FM1_BLOCK_FRAMES; output must hold FM1_BLOCK_WORDS words.
 * Returns 0, or -1 for an invalid pointer/count without writing output. */
FM1_AUDIO_CODE int fm1_engine_render(int32_t *output, unsigned frames);
/* Control task only. Nestable, brief instrument edit transaction. */
void fm1_engine_pause(void);
void fm1_engine_resume(void);
/* Stop playback during an edit transaction; read the global output level. */
void fm1_engine_stop(void);
FM1_AUDIO_CODE unsigned fm1_engine_output_level(void);
struct fm1_engine_stats { uint32_t submitted, completed, underruns, midi_dropped; };
void fm1_engine_stats_read(struct fm1_engine_stats *output);
#endif

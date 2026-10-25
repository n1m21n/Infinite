/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_AUDIO_IO_H
#define FM1_AUDIO_IO_H
#include "fm1.h"

/* Audio ISR only; one consumer/producer, once per FM1_BLOCK_FRAMES block.
 * These calls copy PCM; they never retain caller buffers, wait or allocate.
 * Buffers hold FM1_BLOCK_WORDS interleaved L/R samples, at the board rate.
 * Read returns FM1_BLOCK_FRAMES or 0 (inactive/underrun); -1 rejects invalid
 * pointers/count, -2 means USB changed stream during the copy. Use samples
 * only on a full read. Invalid calls do not consume or write any samples. */
FM1_AUDIO_CODE int fm1_usb_audio_read(int32_t *pcm, unsigned frames, uint16_t gains_q7[2]);
FM1_AUDIO_CODE int fm1_bluetooth_audio_read(int16_t *pcm, unsigned frames);
/* USB PCM is signed Q23; gains are host L/R playback volume (128 = unity),
 * including host mute. Bluetooth PCM is signed 16-bit. Mixing is APP-owned.
 * The gains array must not overlap pcm. */

/* Queue final signed Q23 output for USB recording; host capture gain/mute is
 * applied by the transport. Return queued frames (possibly partial), 0 when
 * inactive/full, or the errors above. Caller buffer remains unchanged. */
FM1_AUDIO_CODE int fm1_usb_audio_write(const int32_t *pcm, unsigned frames);
#endif

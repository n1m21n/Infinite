/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_DEVICE_H
#define FM1_DEVICE_H
#include "fm1.h"
/* Fixed board services for source-built panel/instrument adapters.
 * start owns TIMER4/TIMER5; scan runs in the short input ISR. ADC channels are
 * the fixed board values already used by those adapters. Delay is task-only. */
void fm1_device_start(void (*scan)(void));
uint32_t fm1_device_ticks(void);
void fm1_device_delay(unsigned ms);
void fm1_device_fence(void);
int fm1_device_adc(unsigned channel);
/* Control task only. Very short panel-state access; restore the matching token.
 * Do not wait, draw, write Flash or call the RTOS while interrupts are held. */
uint32_t fm1_device_irq_save(void);
void fm1_device_irq_restore(uint32_t state);
/* Fixed-board interrupt routing; priority follows the existing platform. */
void fm1_device_irq_attach(unsigned index, void (*handler)(void), unsigned priority);
uint32_t fm1_device_clock(void);
void fm1_device_adc_register(unsigned channel);
/* Board wiring hook: nonzero enables the fitted TRS MIDI receiver. */
uint8_t fm1_io_trs_midi_enabled(void);
int fm1_device_usb_connected(void);
void fm1_device_recovery(void);
/* Read-only user mapping for immutable samples; NULL for invalid ranges.
 * Caller owns sample/cache lifetimes and must pause its engine across format
 * changes. Writes through this SDK invalidate hardware caches. */
const uint8_t *fm1_flash_map(uint32_t offset, size_t bytes);
int fm1_flash_invalidate(uint32_t offset, size_t bytes);
#endif

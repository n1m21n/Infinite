/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef SMK37_BOARD_H
#define SMK37_BOARD_H
#include <stdint.h>
/* GPIO numbers follow AC791N: 16 * port + pin; USB DPB/DMB are 150/151. */
int smk37_gpio_direction_input(unsigned pin);
int smk37_gpio_direction_output(unsigned pin, unsigned value);
int smk37_gpio_set_direction(unsigned pin, unsigned input);
int smk37_gpio_set_die(unsigned pin, unsigned enabled);
int smk37_gpio_set_pull_up(unsigned pin, unsigned enabled);
int smk37_gpio_set_pull_down(unsigned pin, unsigned enabled);
void smk37_adc_pmu_detect_en(unsigned channel);
void smk37_adc_reference(unsigned channel, uint16_t value);
/* Private VM namespace: 51..54, versioned calibration records only. */
int smk37_syscfg_read(uint16_t id, void *data, uint16_t bytes);
int smk37_syscfg_write(uint16_t id, void *data, uint16_t bytes);
unsigned smk37_pad_history_step(int16_t history[4], int value);
void smk37_swap_pixels(uint16_t *pixels, unsigned words);
#endif

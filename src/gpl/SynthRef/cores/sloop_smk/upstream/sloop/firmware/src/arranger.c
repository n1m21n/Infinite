/* SPDX-License-Identifier: GPL-3.0-only */
/* Firmware adapter. Scene snapshots are already resident in RAM. No flash
 * operation, UI draw or allocation is allowed from these audio callbacks. */
#include "arranger.h"
static arr_config_t arrangement;
static arr_clock_t arrangement_clock;
static uint8_t arrangement_enabled;
static uint32_t arrangement_ready(void);              /* project.c */
static void arrangement_apply(uint32_t scene);         /* project.c */

static int arrangement_start(void)
{
    int scene;
    if (!arrangement_enabled) return 1;
    scene = arr_begin(&arrangement_clock, &arrangement, arrangement_ready());
    if (scene < 0) return 0;
    arrangement_apply((uint32_t)scene);
    song.rec = 0;                                    /* song playback does not overwrite patterns */
    return 1;
}

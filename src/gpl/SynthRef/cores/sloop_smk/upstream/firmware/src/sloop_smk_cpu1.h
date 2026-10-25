/* SPDX-License-Identifier: GPL-3.0-only */
/* Sloop 2.8 render-unit API over the existing SMK checked mailbox/lifecycle.
 * Never run a second worker or re-execute a partly advanced render unit. */
#pragma once
#include "../../sloop/firmware/src/cpu1.h"
#if SLOOP_DUAL_CORE
static cpu1_job_t smk_cpu1_fn;
static uint32_t smk_cpu1_arg, smk_cpu1_blocks;
static void smk_cpu1_job(void *unused)
{
    (void)unused;
    smk_cpu1_fn(smk_cpu1_arg);
}
static int cpu1_run(cpu1_job_t fn, uint32_t arg)
{
    smk_cpu1_fn = fn;
    smk_cpu1_arg = arg;
    if (!audio_worker_submit(smk_cpu1_job, 0)) return 0;
    smk_cpu1_blocks++;
    return 1;
}
static int cpu1_wait(void) { return audio_worker_join() ? 0 : -1; }
static int cpu1_on(void) { return audio_worker_online; }
#else
static int cpu1_run(cpu1_job_t fn, uint32_t arg) { (void)fn; (void)arg; return 0; }
static int cpu1_wait(void) { return 0; }
static int cpu1_on(void) { return 0; }
#endif
static uint32_t cpu1_ticks(void)
{
#ifdef SLOOP_SMK_HARDWARE
    return fm1_ticks();
#else
    return 0;
#endif
}
static const char *cpu1_label(void)
{
    return cpu1_on() ? (cpu1_units ? "2 CORES" : "C1 OFF") : "1 CORE";
}

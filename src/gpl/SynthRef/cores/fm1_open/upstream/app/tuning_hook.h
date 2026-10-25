/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_TUNING_HOOK_H
#define FM1_TUNING_HOOK_H
#include "instrument.h"
/* Optional on older SDKs. Test these weak references before calling. */
extern int groove_tuning_capture(struct groove_tuning t[GROOVE_TRACKS]) __attribute__((weak));
extern int groove_tuning_apply(const struct groove_tuning t[GROOVE_TRACKS]) __attribute__((weak));
#endif

/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only interpretation of the stock Pro 016 usrtrim record.
 * No flash address, erase, write, or automatic default generation is provided.
 * Caller supplies the actual unit's decoded record and retains the raw backup.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define SMK37_TRIM_DATA_BYTES 548u
#define SMK37_TRIM_RECORD_BYTES 556u
#define SMK37_TRIM_KEYS_OFFSET 168u

static inline unsigned smk37_trim_u16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* Decode only after validation; reject incomplete/erased/corrupt records.
 * Unknown header and non-key data remain opaque and must be preserved. */
static inline int smk37_trim_keys_valid(const uint8_t *record, size_t size)
{
    static const uint8_t marker[8] = {'t','r','i','m','d','a','t',0};
    unsigned key, j;
    if (!record || size < SMK37_TRIM_RECORD_BYTES) return 0;
    for (j = 0; j < 8; ++j)
        if (record[SMK37_TRIM_DATA_BYTES + j] != marker[j]) return 0;
    for (key = 0; key < 37; ++key) {
        const uint8_t *p = record + SMK37_TRIM_KEYS_OFFSET + key * 10;
        unsigned previous = smk37_trim_u16(p);
        if (previous > 1023) return 0;
        for (j = 1; j < 5; ++j) {
            unsigned current = smk37_trim_u16(p + j * 2);
            if (current <= previous || current > 1023) return 0;
            previous = current;
        }
    }
    return 1;
}

/* Timing-to-response interpolation from 0x0201C20E..0x0201C29A.
 * duration is the stock consumer's elapsed iteration count, not milliseconds.
 * Zero means release. Caller must first validate the complete trim record.
 * Return -1 for an invalid key/curve; never synthesize calibration silently.
 * Clamp the interpolation's top endpoint to the documented 10-bit range.
 */
static inline int smk37_trim_key_response(const uint8_t *record, unsigned key,
                                         uint32_t duration)
{
    const uint8_t *p;
    unsigned x, lower, upper, segment, response;
    if (!record || key >= 37) return -1;
    p = record + SMK37_TRIM_KEYS_OFFSET + key * 10;
    lower = smk37_trim_u16(p);
    if (lower > 1023) return -1;
    for (segment = 1; segment < 5; ++segment) {
        upper = smk37_trim_u16(p + segment * 2);
        if (upper <= lower || upper > 1023) return -1;
        lower = upper;
    }
    if (!duration) return 0;
    x = duration > 1022 ? 1 : 1023 - duration;
    lower = smk37_trim_u16(p);
    if (x <= lower) return 1;
    for (segment = 1; segment < 5; ++segment) {
        upper = smk37_trim_u16(p + segment * 2);
        if (x <= upper) {
            response = (segment - 1) * 256 + (x - lower) * 256 / (upper - lower);
            return response > 1023 ? 1023 : response ? (int)response : 1;
        }
        lower = upper;
    }
    return 1023;
}

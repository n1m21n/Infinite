/* SPDX-License-Identifier: MIT */
#ifndef GROOVE_CLOCK_SYNC_H
#define GROOVE_CLOCK_SYNC_H

/* Owned by each audio worker. BLE timestamps are milliseconds modulo 8192.
 * Buffer received pulses for 25 ms; never invent clocks between packets.
 * The slow phase correction follows oscillator drift without copying radio
 * delivery jitter into the sequencer. USB/DIN retain immediate delivery. */
enum { CLOCK_PULSES = 16, CLOCK_LAG = FM1_SAMPLE_RATE / 40U };
struct groove_clock_sync {
    uint32_t now, due, filtered, sum, elapsed, pulse[CLOCK_PULSES];
    uint16_t stamp, bpm;
    uint8_t fraction, count, active, read, write, have_clock, padding[2];
};
_Static_assert(sizeof(struct groove_clock_sync) == 96, "clock sync stride");

static FM1_DSP_CODE void clock_sync_clear(struct groove_clock_sync *s)
{
    s->active = s->read = s->write = s->fraction = s->count = 0;
    s->sum = s->elapsed = s->have_clock = 0;
}
static FM1_DSP_CODE void clock_sync_measure(struct groove_clock_sync *s, unsigned frames)
{
    s->sum += frames;
    if (++s->count != 24) return;
    if (s->sum) {
        unsigned difference = s->sum > s->filtered ? s->sum - s->filtered : s->filtered - s->sum;
        s->filtered = difference > s->filtered / 8U ? s->sum : (3U * s->filtered + s->sum + 2U) / 4U;
        unsigned bpm = (FM1_SAMPLE_RATE * 60U + s->filtered / 2U) / s->filtered;
        s->bpm = (uint16_t)(bpm > 999U ? 999U : bpm);
    }
    s->sum = s->count = 0;
}
static FM1_DSP_CODE int clock_sync_receive(struct groove_clock_sync *s, unsigned status, unsigned stamp)
{
    unsigned delta = (stamp - s->stamp) & 8191U;
    if ((uint8_t)(s->write - s->read) == CLOCK_PULSES) return -1;
    if (!s->active || delta > 2000U) {
        clock_sync_clear(s);
        s->active = 1;
        s->due = s->now + CLOCK_LAG;
    } else {
        unsigned scaled = delta * (FM1_SAMPLE_RATE / 100U) + s->fraction;
        unsigned frames = scaled / 10U;
        s->fraction = (uint8_t)(scaled % 10U);
        s->due += frames;
        int error = (int32_t)(s->now + CLOCK_LAG - s->due);
        /* A stall beyond the buffer's scope establishes a new time origin.
         * Normal phase corrections are bounded to 0.36 ms per clock. */
        if (error > (int)FM1_SAMPLE_RATE / 4 || error < -(int)FM1_SAMPLE_RATE / 4)
            s->due = s->now + CLOCK_LAG;
        else {
            int correction = error / 64;
            if (correction > 16) correction = 16;
            if (correction < -16) correction = -16;
            s->due += correction;
        }
        s->elapsed += frames;
    }
    s->stamp = (uint16_t)stamp;
    if (status == 0xf8) {
        if (s->have_clock) clock_sync_measure(s, s->elapsed);
        s->have_clock = 1; s->elapsed = 0;
    } else s->sum = s->count = s->elapsed = s->have_clock = 0;
    /* The top two bits carry Clock/Start/Continue/Stop. Modular deadlines
     * have a +/- 3.3 hour horizon; the queue itself spans at most seconds. */
    unsigned kind = status == 0xf8 ? 0 : status - 0xf9U;
    s->pulse[s->write++ & (CLOCK_PULSES - 1U)] = (s->due & 0x3fffffffU) | (kind << 30);
    return 0;
}
static FM1_DSP_CODE int clock_sync_render(struct groove_clock_sync *s)
{
    uint32_t event = s->pulse[s->read & (CLOCK_PULSES - 1U)];
    int ready = s->read != s->write && (int32_t)((s->now - event) << 2) >= 0;
    s->now += FM1_BLOCK_FRAMES;
    if (ready) ++s->read;
    return ready ? (int)(event >> 30) + 1 : 0;
}
#endif

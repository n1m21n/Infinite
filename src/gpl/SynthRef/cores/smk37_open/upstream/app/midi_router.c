/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "instrument.h"
#include "fm1_engine_control.h"
#include <string.h>
#ifdef FM1_HOST_TEST
#include <stdatomic.h>
#define WORD _Atomic(unsigned)
#else
#define WORD volatile unsigned
#endif

/* Device preferences, not project data: 4 channel nibbles, 8 two-bit modes,
 * ports/local/clock/live flags, and Thru/source-clock fields. */
static struct groove_midi_config config;
enum { LOCAL_OFF = 8, CLOCK_OUT = 16, LIVE_OUT = 32, SEQ_OFF = 64, QUEUE = 128 };
enum { ARP_OWNER = 8, LIVE_OWNER = 16, THRU_OWNER = 24, OWNERS = 40 };
struct events { WORD read, write, lost; unsigned seen; uint32_t data[QUEUE]; } __attribute__((aligned(32)));
/* One producer per audio owner and one for the MIDI routing task. */
static struct events events[3] __attribute__((aligned(32)));
struct output {
    uint8_t held[OWNERS][16]; /* SEQ, ARP, physical, then Thru channel owners */
    uint16_t channels, recover, sustain, sent_sustain;
    uint8_t reset, stop;
};
static struct output output[3];
static unsigned ready_seen, auto_source = 3;
static uint32_t clock_seen, clock_phase;
static uint32_t resume_phase;
static unsigned clock_first, resume_first, master_running;
static uint32_t dropped;
static void release_owner(unsigned owner, unsigned ports);
/* Boards with independent held-key state discard its old MIDI ownership when
 * routing changes. FM1's common panel already retries released keys itself. */
extern void groove_midi_panel_reset(void) __attribute__((weak));
extern void groove_midi_panel_track_reset(unsigned track) __attribute__((weak));

static FM1_AUDIO_CODE void barrier(void)
{
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
}
static unsigned size(unsigned status) { return status >= 0xf8 ? 1 : (status & 0xe0U) == 0xc0U ? 2 : 3; }
static unsigned channel(unsigned track) { return (config.channel[track >> 1] >> (4U * (track & 1U))) & 15U; }
FM1_AUDIO_CODE unsigned groove_midi_mode(unsigned track)
{ return (config.mode[track >> 2] >> (2U * (track & 3U))) & 3U; }
void groove_midi_defaults(struct groove_midi_config *c)
{
    static const uint8_t channels[4] = {0x10, 0x32, 0x54, 0x96};
    memcpy(c->channel, channels, sizeof channels);
    /* Product defaults: SEQ BOTH on all eight tracks, KEY OUT and SEQ OUT
     * ON. Available ports only select destinations, not the default modes.
     * Restoring valid preferences retains the user's explicit choices. */
    memset(c->mode, 0x55, sizeof c->mode);
    c->flags = (uint8_t)(fm1_midi_output_ports() | LIVE_OUT | CLOCK_OUT);
    c->thru = (fm1_midi_input_ports() & 1U) ? 0 : 8;
}
int groove_midi_config_valid(const struct groove_midi_config *c)
{
    if ((c->flags & 0x80U) || (c->thru & 0x80U) || ((c->thru >> 3) & 3U) == 3) return 0;
    for (unsigned t = 0; t < 8; ++t) if (((c->mode[t >> 2] >> (2U * (t & 3U))) & 3U) == 3) return 0;
    return 1;
}
void groove_midi_config_read(struct groove_midi_config *c) { *c = config; }
static void recover(unsigned port)
{
    struct output *o = &output[port];
    if (!o->recover) o->reset = 0;
    o->recover |= o->channels;
    o->sent_sustain = 0;
    if ((config.flags & CLOCK_OUT) && (config.flags & (1U << port))) o->stop = 1;
    memset(o->held, 0, sizeof o->held);
}
void groove_midi_panic(void)
{
    fm1_engine_pause();
    for (unsigned i = 0; i < 3; ++i) {
        events[i].read = events[i].write; events[i].seen = events[i].lost;
        output[i].sustain = 0; recover(i);
    }
    fm1_engine_resume();
}
__attribute__((cold, minsize)) void groove_midi_config_apply(const struct groove_midi_config *c)
{
    if (!memcmp(&config, c, sizeof config)) return;
    unsigned modes = (config.mode[0] ^ c->mode[0]) | ((config.mode[1] ^ c->mode[1]) << 8);
    unsigned changed = config.flags ^ c->flags;
    if (!memcmp(config.channel, c->channel, sizeof config.channel) && config.thru == c->thru &&
        !(changed & ~(SEQ_OFF | CLOCK_OUT))) {
        /* SEQ mode/output edits leave transport, ARP and live controls intact.
         * Release changed pattern gates using the OLD local/output routing. */
        fm1_engine_pause();
        groove_midi_poll();
        for (unsigned t = 0; t < 8; ++t, modes >>= 2) {
            if (modes & 3U) groove_sequence_release(t);
            if ((changed & SEQ_OFF) || (modes & 3U)) release_owner(t, config.flags & 7U);
        }
        if ((changed & CLOCK_OUT) && (config.flags & CLOCK_OUT))
            for (unsigned p = 0; p < 3; ++p) if (config.flags & (1U << p)) output[p].stop = 1;
        config = *c; barrier();
        if ((changed & CLOCK_OUT) && !groove_clock_mode()) groove_midi_clock_reset();
        fm1_engine_resume();
        return;
    }
    fm1_engine_pause();
    fm1_engine_stop();
    groove_midi_panic();
    config = *c;
    barrier();
    groove_midi_clock_reset();
    fm1_engine_resume();
    if (groove_midi_panel_reset) groove_midi_panel_reset();
    const uint8_t reset = 0xff; (void)fm1_midi_post(&reset, 1);
}
void groove_midi_clock_reset(void)
{
    auto_source = 3; clock_phase = resume_phase = 0;
    clock_first = resume_first = 1; master_running = 0;
}
void groove_midi_init(void)
{
    memset(events, 0, sizeof events); memset(output, 0, sizeof output);
    groove_midi_defaults(&config);
    ready_seen = dropped = clock_seen = 0; groove_midi_clock_reset();
}
unsigned groove_midi_setting(unsigned track, unsigned parameter)
{
    switch (parameter) {
    case 0: return groove_midi_mode(track);
    case 1: return channel(track);
    case 2: return config.flags & 7U;
    case 3: return !(config.flags & LOCAL_OFF);
    case 4: return !!(config.flags & LIVE_OUT);
    case 5: return !!(config.flags & CLOCK_OUT);
    case 6: return (config.thru >> 5) & 3U;
    case 7: return (config.thru >> 3) & 3U;
    case 8: return config.thru & 7U;
    case 9: return (config.flags & SEQ_OFF) ? ((config.flags & CLOCK_OUT) ? 2 : 0) : 1;
    default: return 0;
    }
}
static FM1_AUDIO_CODE void enqueue(unsigned lane, unsigned owner, unsigned ports, unsigned status, unsigned a, unsigned b)
{
    if (!ports && status != 0xf6) return;
    struct events *q = &events[lane];
    unsigned w = q->write;
    if (w - q->read == QUEUE) { ++q->lost; return; }
    /* MIDI data bytes are seven-bit. Their spare bit stores owner bit 5,
     * keeping the fixed-size queue and three destination bits unchanged. */
    q->data[w & (QUEUE - 1)] = status | (a << 8) | (b << 16) |
        ((owner & 31U) << 24) | ((owner & 32U) << 18) | (ports << 29);
    barrier(); q->write = w + 1U;
}
FM1_AUDIO_CODE void groove_midi_note(unsigned track, unsigned note, unsigned velocity)
{
    if ((config.flags & SEQ_OFF) || !groove_midi_mode(track)) return;
    unsigned ch = (config.channel[track >> 1] >> (4U * (track & 1U))) & 15U;
    enqueue(track & 1U, track, config.flags & 7U, (velocity ? 0x90U : 0x80U) | ch, note, velocity);
}
FM1_AUDIO_CODE void groove_midi_arp_note(unsigned track, unsigned note, unsigned velocity)
{
    enqueue(track & 1U, ARP_OWNER + track, config.flags & 7U,
            (velocity ? 0x90U : 0x80U) | channel(track), note, velocity);
}
FM1_AUDIO_CODE void groove_midi_track_reset(unsigned track)
{ enqueue(track & 1U, track, config.flags & 7U, 0xf6, 0, 0); }
FM1_AUDIO_CODE void groove_midi_transport(unsigned owner, uint8_t status)
{
    if (owner || !(config.flags & CLOCK_OUT) || groove_clock_mode()) return;
    if (status == 0xfa || status == 0xff) {
        clock_phase = resume_phase = 0; clock_first = resume_first = 1;
        master_running = status == 0xfa;
        enqueue(0, 0, config.flags & 7U, 0xf2, 0, 0);
    } else if (status == 0xfb) {
        /* Free-running clocks during Stop must not move the paused musical
         * phase. Continue resumes its remaining fraction of a MIDI pulse. */
        if (!master_running) { clock_phase = resume_phase; clock_first = resume_first; }
        master_running = 1;
    } else if (status == 0xfc && master_running) {
        resume_phase = clock_phase; resume_first = clock_first; master_running = 0;
    }
    enqueue(0, 0, config.flags & 7U, status == 0xff ? 0xfc : status, 0, 0);
}
FM1_AUDIO_CODE void groove_midi_clock(unsigned owner, unsigned tempo)
{
    if (owner || !(config.flags & CLOCK_OUT) || groove_clock_mode()) return;
    enum { PERIOD = FM1_SAMPLE_RATE * 60U };
    if (clock_first || clock_phase >= PERIOD) {
        if (!clock_first) clock_phase -= PERIOD;
        clock_first = 0;
        enqueue(0, 0, config.flags & 7U, 0xf8, 0, 0);
    }
    clock_phase += tempo * 24U * FM1_BLOCK_FRAMES;
}
static unsigned owner_channel(unsigned owner) { return owner < THRU_OWNER ? channel(owner & 7U) : owner - THRU_OWNER; }
static int held_elsewhere(const struct output *o, unsigned owner, unsigned ch, unsigned note)
{
    /* Thru has exactly one owner per channel; the other fifteen cannot
     * share this note. SEQ, ARP and physical owners use track maps. */
    unsigned thru = THRU_OWNER + ch;
    if (owner != thru && (o->held[thru][note >> 3] & (1U << (note & 7U)))) return 1;
    for (unsigned i = 0; i < THRU_OWNER; ++i)
        if (i != owner && (o->held[i][note >> 3] & (1U << (note & 7U))) && owner_channel(i) == ch) return 1;
    return 0;
}
static void deliver(unsigned port, unsigned owner, const uint8_t *m)
{
    struct output *o = &output[port];
    unsigned ch = m[0] & 15U, type = m[0] & 0xf0U, note = m[1], bit = 1U << (note & 7U);
    if (o->recover || o->stop) return;
    if (m[0] < 0xf0) o->channels |= (uint16_t)(1U << ch);
    if (type == 0x90 || type == 0x80) {
        uint8_t *held = &o->held[owner][note >> 3];
        unsigned on = type == 0x90 && m[2];
        unsigned drum = owner < THRU_OWNER && groove_pad_count(owner & 7U);
        if (!!(*held & bit) == !!on && !(on && drum)) return;
        if (on) *held |= bit; else *held &= ~bit;
        if (held_elsewhere(o, owner, ch, note) && !(on && drum)) return;
        if (on && (o->sustain & (1U << ch)) && !(o->sent_sustain & (1U << ch))) {
            uint8_t pedal[3] = {(uint8_t)(0xb0 | ch), 64, 127};
            if (fm1_midi_send_port(port, pedal, 3)) { recover(port); ++dropped; return; }
            o->sent_sustain |= (uint16_t)(1U << ch);
        }
    }
    if (fm1_midi_send_port(port, m, size(m[0]))) { recover(port); ++dropped; }
    else if (type == 0xb0 && m[1] == 64) {
        if (m[2] >= 64) o->sent_sustain |= (uint16_t)(1U << ch);
        else o->sent_sustain &= (uint16_t)~(1U << ch);
    } else if (type == 0xb0 && (m[1] == 120 || m[1] == 123)) {
        for (unsigned i = 0; i < OWNERS; ++i) if (owner_channel(i) == ch) memset(o->held[i], 0, 16);
    } else if (type == 0xb0 && m[1] == 121) {
        o->sent_sustain &= (uint16_t)~(1U << ch);
    }
}
static void send_event(unsigned owner, unsigned ports, const uint8_t *m)
{
    for (unsigned p = 0; p < 3; ++p) if (ports & (1U << p)) {
        /* Desired state must follow a controller reset even if the port is
         * disconnected or congested; recovery must not revive an old pedal. */
        if ((m[0] & 0xf0U) == 0xb0 && (m[1] == 64 || m[1] == 121)) {
            unsigned bit = 1U << (m[0] & 15U);
            if (m[1] == 64 && m[2] >= 64) output[p].sustain |= bit; else output[p].sustain &= ~bit;
        }
        if (ready_seen & (1U << p)) deliver(p, owner, m);
    }
}
static void release_owner(unsigned owner, unsigned ports)
{
    unsigned ch = owner_channel(owner);
    for (unsigned p = 0; p < 3; ++p) if (ports & (1U << p)) {
        for (unsigned byte = 0; byte < 16; ++byte) {
            unsigned held = output[p].held[owner][byte];
            while (held) {
                unsigned bit = (unsigned)__builtin_ctz(held);
                held &= held - 1;
                uint8_t off[3] = {(uint8_t)(0x80 | ch), (uint8_t)(byte * 8 + bit), 0};
                deliver(p, owner, off);
            }
        }
    }
}
void groove_midi_poll(void)
{
    unsigned ready = fm1_midi_ready_ports();
    for (unsigned p = 0; p < 3; ++p) {
        if ((ready_seen & ~ready) & (1U << p)) recover(p);
        if (!(ready & (1U << p))) continue;
        struct output *o = &output[p];
        if (o->stop) {
            const uint8_t stop = 0xfc;
            if (fm1_midi_send_port(p, &stop, 1)) continue;
            o->stop = 0;
        }
        /* Recovery is bounded and independent per port. Retain desired pedal
         * state, restoring it only before a subsequent accepted Note On. */
        for (unsigned work = 0; work < 16 && o->recover; ++work) {
            unsigned ch = (unsigned)__builtin_ctz(o->recover);
            static const uint8_t neutral[5][3] = {{0xb0,64,0},{0xb0,120,0},{0xb0,1,0},{0xe0,0,64},{0xd0,0,0}};
            uint8_t m[3]; memcpy(m, neutral[o->reset], 3); m[0] |= ch;
            if (fm1_midi_send_port(p, m, size(m[0]))) break;
            if (++o->reset == 5) { o->reset = 0; o->recover &= (uint16_t)~(1U << ch); o->channels &= (uint16_t)~(1U << ch); }
        }
    }
    ready_seen = ready;
    for (unsigned i = 0; i < 3; ++i) {
        struct events *q = &events[i];
        if (q->lost != q->seen) {
            dropped += q->lost - q->seen; q->seen = q->lost;
            q->read = q->write;
            for (unsigned p = 0; p < 3; ++p) recover(p);
        }
    }
    /* Round robin: a dense chord on one owner cannot starve the other's gates
     * or source-routed Thru. Maximum work per control iteration is fixed. */
    for (unsigned work = 0; work < QUEUE; ++work) {
        unsigned found = 0;
        for (unsigned i = 0; i < 3; ++i) {
            struct events *q = &events[i]; unsigned r = q->read;
            if (r == q->write) continue;
            barrier(); uint32_t event = q->data[r & (QUEUE - 1)]; barrier(); q->read = r + 1;
            unsigned owner = ((event >> 24) & 31U) | ((event >> 18) & 32U), ports = event >> 29;
            uint8_t m[3] = {(uint8_t)event, (uint8_t)(event >> 8), (uint8_t)((event >> 16) & 127U)};
            if (m[0] == 0xff) {
                for (unsigned p = 0; p < 3; ++p) if (ports & (1U << p)) { output[p].sustain = 0; recover(p); }
                if (groove_midi_panel_reset) groove_midi_panel_reset();
            }
            else if (m[0] == 0xf6) {
                if (owner < ARP_OWNER) release_owner(owner, ports);
                owner &= 7U;
                release_owner(owner + ARP_OWNER, ports);
                release_owner(owner + LIVE_OWNER, ports);
                if (groove_midi_panel_track_reset) groove_midi_panel_track_reset(owner);
            }
            else send_event(owner, ports, m);
            found = 1;
        }
        if (!found) break;
    }
}
int groove_midi_local(const uint8_t *m, size_t n)
{
    if (!m || !n || n > 3 || m[0] < 0x80 || (n > 1 && m[1] >= 128) || (n > 2 && m[2] >= 128)) return -1;
    if (m[0] >= 0xf0) {
        if (m[0] == 0xff) groove_midi_panic();
        return fm1_midi_post(m, n);
    }
    unsigned ch = m[0] & 15U, targets = 0;
    if (n != size(m[0])) return -1;
    for (unsigned t=0;t<GROOVE_TRACKS;++t)
        if (groove_track_role(t) != GROOVE_TRACK_OFF && groove_midi_input_channel(t)==ch) targets |= 1U<<t;
    /* An Off/unmapped channel is intentionally silent, not a queue failure.
     * The panel interprets failure as a lost key and performs a global reset. */
    if (!targets) return 0;
    int result = (config.flags & LOCAL_OFF) ? 0 : fm1_midi_post(m, n);
    if (result) { groove_midi_panic(); return result; }
    if (!(config.flags & LIVE_OUT)) return 0;
    unsigned note = (m[0] & 0xe0U) == 0x80U, sent_channels = 0;
    for (unsigned track=0;track<GROOVE_TRACKS;++track) if (targets & (1U<<track)) {
        /* Suppress raw keys only while this ARP actually owns note output. */
        if (note && !(config.flags & LOCAL_OFF) && groove_arp_enabled(track)) continue;
        unsigned out = channel(track), bit = 1U << out;
        /* Layered tracks share channel controls, but each retains its own
         * note ownership so releasing one layer cannot cut off another. */
        if (!note && (sent_channels & bit)) continue;
        sent_channels |= bit;
        uint8_t data[3] = {(uint8_t)((m[0] & 0xf0U) | out), m[1], n > 2 ? m[2] : 0};
        send_event(LIVE_OWNER + track, config.flags & 7U, data);
    }
    return 0;
}
void groove_midi_fader(unsigned track, unsigned value)
{
    if (!(config.flags & LIVE_OUT)) return;
    uint8_t m[3] = {(uint8_t)(0xb0U | channel(track)), 7, (uint8_t)value};
    send_event(LIVE_OWNER + track, config.flags & 7U, m);
}
int groove_midi_clock_accept(unsigned source, uint8_t status)
{
    if (source >= 3) return 0;
    if (!groove_clock_mode()) return !(config.flags & CLOCK_OUT); /* legacy remote transport */
    unsigned choice = (config.thru >> 5) & 3U;
    if (choice) return source == (choice == 3 ? FM1_MIDI_DIN : choice);
    uint32_t now = fm1_millis();
    if (status == 0xf8 || status == 0xfa || status == 0xfb || status == 0xf2) {
        if (auto_source == 3 || now - clock_seen >= 2000U) auto_source = source;
        if (auto_source == source) clock_seen = now;
    }
    return source == auto_source;
}
void fm1_app_midi_received(unsigned source, const uint8_t *m, size_t n)
{
    if (source >= 3 || !m || !n || n > 3) return;
    unsigned clock = m[0] == 0xf8 || m[0] == 0xfa || m[0] == 0xfb || m[0] == 0xfc || m[0] == 0xf2;
    if (clock && !groove_midi_clock_accept(source, m[0])) return;
    (void)fm1_midi_post(m, n);
    if (m[0] == 0xff) enqueue(2, 0, (config.flags | config.thru) & 7U, 0xff, 0, 0);
    if (((config.thru >> 3) & 3U) == source && m[0] != 0xff && (m[0] < 0xf0 || clock)) {
        unsigned ports = config.thru & 7U & ~(1U << source) & fm1_midi_output_ports();
        enqueue(2, THRU_OWNER + (m[0] & 15U), ports, m[0], n > 1 ? m[1] : 0, n > 2 ? m[2] : 0);
    }
}
uint32_t groove_midi_dropped(void) { return dropped; }

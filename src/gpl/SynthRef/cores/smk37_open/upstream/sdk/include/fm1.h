/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_H
#define FM1_H

#include <stddef.h>
#include <stdint.h>

#define FM1_API_VERSION 1U
#include "fm1_block.h"

#ifdef FM1_HOST_TEST
#define FM1_AUDIO_CODE
#define FM1_AUDIO_DATA
#else
/* Audio code and every callee must be safe during Flash programming. */
#define FM1_AUDIO_CODE __attribute__((section(".volatile_ram_code"), noinline, used))
/* Initialized lookup tables used by audio, including const tables. Large
 * zero-initialized working buffers should stay in ordinary internal BSS. */
#define FM1_AUDIO_DATA __attribute__((section(".volatile_ram.fm1"), aligned(4)))
#endif

/* Control task context: queue complete raw MIDI messages for USB/BLE output.
 * Returns 0 when queued, -1 for invalid arguments/unavailable transport ring,
 * -2 when busy/full. Retry later on -2; success is not a host delivery receipt. */
int fm1_midi_send(const uint8_t *message, size_t bytes);
/* Per-port output uses independent bounded queues. A slow/unavailable port
 * cannot backpressure another. Control task only, complete messages <=128B.
 * -1 invalid/offline, -2 full; 0 queued, not a delivery receipt. */
enum { FM1_MIDI_DIN, FM1_MIDI_USB, FM1_MIDI_BLE };
unsigned fm1_midi_output_ports(void); /* physically supported output bit mask */
unsigned fm1_midi_input_ports(void);  /* physically supported input bit mask */
unsigned fm1_midi_ready_ports(void);  /* currently available output bit mask */
int fm1_midi_send_port(unsigned port, const uint8_t *message, size_t bytes);
/* Routing-task callback for a complete short message. Default: local input.
 * No blocking, Flash, UI, allocation or audio edits here; queue deferred work. */
void fm1_app_midi_received(unsigned source, const uint8_t *message, size_t bytes);
/* Read only inside fm1_app_sysex; retain alongside a deferred request. */
unsigned fm1_midi_input_source(void);

/* Queue local keys/controllers into the instrument's MIDI input.
 * Complete short messages only (status + 7-bit data, no SysEx/running status).
 * Returns 0 when queued, -1 for invalid input or queue overflow. Overflow
 * requests a reset before later events. Control task context only; no ISR. */
int fm1_midi_post(const uint8_t *message, size_t bytes);
/* Independent Bluetooth music and MIDI, including simultaneous use. Control
 * task only. A zero mask disables both links and discovery; the stack
 * stays initialized. Settings are accepted before stack startup. Returns
 * 0 accepted, -1 invalid, -2 busy (retry later). No Flash write is performed. */
enum { FM1_BT_AUDIO = 1, FM1_BT_MIDI = 2 };
struct fm1_bluetooth_status {
    uint8_t features, ready, audio_connected, audio_playing;
};
int fm1_bluetooth_configure(unsigned features);
void fm1_bluetooth_get_status(struct fm1_bluetooth_status *status);
/* BLE MIDI: one connection, selectable Peripheral (phone/computer connects)
 * or Central (FM1 connects to a MIDI peripheral). Classic Bluetooth music is
 * independent. These calls belong to the control task, never audio/ISR/tick.
 * Commands are asynchronous: 0 queued, -1 invalid, -2 busy, -3 not ready.
 * Switching roles disconnects BLE; power-on defaults to Peripheral.
 * Scan is active for 15 seconds. Entries stay at their indexes until a new
 * scan or role change. Only connectable MIDI advertisers enter the list;
 * discovery verifies their GATT MIDI service before allowing data.
 * No automatic connection to an unselected device. Names are ASCII sanitized. */
enum fm1_ble_role { FM1_BLE_PERIPHERAL, FM1_BLE_CENTRAL };
enum fm1_ble_state {
    FM1_BLE_OFF, FM1_BLE_IDLE, FM1_BLE_ADVERTISING, FM1_BLE_SCANNING,
    FM1_BLE_CONNECTING, FM1_BLE_DISCOVERING, FM1_BLE_CONNECTED,
    FM1_BLE_READY, FM1_BLE_DISCONNECTING
};
enum fm1_ble_error {
    FM1_BLE_OK, FM1_BLE_STACK_ERROR, FM1_BLE_TIMEOUT, FM1_BLE_NOT_MIDI,
    FM1_BLE_GATT_ERROR, FM1_BLE_LINK_LOST,
    FM1_BLE_PAIRING_FAILED, FM1_BLE_KEY_MISSING, FM1_BLE_BOND_SAVE_FAILED
};
#define FM1_BLE_MAX_DEVICES 8U
struct fm1_ble_device {
    char name[25];
    uint8_t address[6], address_type; /* controller byte order, used unchanged */
    int8_t rssi;
    uint8_t midi; /* MIDI service advertised; discovery still required */
};
struct fm1_ble_status {
    uint8_t role, state, count, error, pending;
};
int fm1_ble_set_role(enum fm1_ble_role role);
int fm1_ble_scan(void);
int fm1_ble_connect(unsigned index);
int fm1_ble_disconnect(void);
/* Disconnect BLE, then forget its saved pairing keys on the Bluetooth task.
 * Classic audio keys are retained. The peer must also forget FM1 before a
 * fresh pairing. Uses the same asynchronous result/status as other commands. */
int fm1_ble_forget_bonds(void);
void fm1_ble_get_status(struct fm1_ble_status *status);
int fm1_ble_get_device(unsigned index, struct fm1_ble_device *device);
/* Optional Control task hook for complete SysEx frames, including F0/F7.
 * Maximum 1024 bytes; larger or interrupted frames are discarded. USB, BLE
 * and UART assemble frames separately. No receive-timestamp scheduling.
 * Copy or queue the frame; the transport retains ownership after return.
 * The default ignores application SysEx. Firmware update messages are handled
 * before this hook. No storage writes from the routing task. */
void fm1_app_sysex(const uint8_t *message, size_t bytes);

/* Output timing diagnostics. Read from the control task. */
struct fm1_audio_stats {
    uint32_t blocks, short_intervals, overruns, peak_ticks;
};
void fm1_audio_stats_read(struct fm1_audio_stats *stats);

/* Panel API: control task only. */
#define FM1_SCREEN_WIDTH 240U
#define FM1_SCREEN_HEIGHT 240U
struct fm1_controls {
    uint32_t keys;                  /* bits 0..26: F3..G5 */
    uint32_t buttons;               /* physical panel buttons 0..13 */
    int16_t encoders[7];            /* signed detents since the last read */
    uint16_t knob, battery;         /* ADC values, 0..1023 */
};
void fm1_controls_init(void);
void fm1_controls_read(struct fm1_controls *state);
void fm1_led_set(unsigned key, int on); /* 0..13 buttons, 14..40 note keys */
/* Control millisecond clock, wrapping at 2^32. Measure intervals by unsigned
 * subtraction; do not compare absolute timestamps across a wrap. */
uint32_t fm1_millis(void);
/* Request deferred control work after queuing a message. */
void fm1_control_request(void);
void fm1_screen_init(void);
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t rgb565);
/* Native-endian RGB565. Complete before return: the caller can reuse pixels.
 * Rectangle must be inside 240x240; count is the number of available pixels. */
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h,
                    const uint16_t *pixels, size_t count);
#define FM1_SCREEN_DMA_PIXELS (240U * 16U)
/* Nonblocking band transfer, at most FM1_SCREEN_DMA_PIXELS. On success (0), the RAM
 * buffer is byte-swapped in place and borrowed until pending returns 0.
 * Do not read/write it while pending. Return 1 means busy (buffer unchanged),
 * negative means invalid input/transport error. Control task only. */
int fm1_screen_submit(unsigned x, unsigned y, unsigned w, unsigned h,
                      uint16_t *pixels, size_t count);
/* 1 while DMA runs, 0 when idle, negative on timeout (DMA has been stopped). */
int fm1_screen_pending(void);

/* Persistent user Flash. Offsets start at zero within this firmware's user
 * partition. The SDK discovers its address; firmware/boot/VM/BTIF are excluded.
 * Call read/write/erase only from the control task with interrupts enabled.
 * Never from audio, timer, MIDI or SysEx callbacks.
 * Writes/erases briefly silence audio and synchronize hardware caches.
 * A full instrument already uses this same partition for its own data format;
 * it is not extra free space alongside its existing projects/samples.
 * No allocation or automatic erase. Erase clears exactly one aligned sector.
 * Write verifies 1->0 transitions and readback, splitting pages internally.
 * Input must stay stable and must not alias the mapped user partition.
 * This raw API is not transactional: interrupted writes may be partial.
 * Use CRC/versioned records and two sectors if previous data must survive.
 * Size returns zero when the fixed reservation could not be validated. */
#define FM1_FLASH_SECTOR_BYTES 4096U
enum fm1_flash_result {
    FM1_FLASH_OK = 0,
    FM1_FLASH_INVALID = -1,
    FM1_FLASH_IO = -2,
    FM1_FLASH_BUSY = -3,
    FM1_FLASH_NEEDS_ERASE = -4,
    FM1_FLASH_CONTEXT = -5,
    FM1_FLASH_UNAVAILABLE = -6
};
uint32_t fm1_flash_size(void);
int fm1_flash_read(uint32_t offset, void *data, size_t bytes);
int fm1_flash_write(uint32_t offset, const void *data, size_t bytes);
int fm1_flash_erase(uint32_t offset);

#endif

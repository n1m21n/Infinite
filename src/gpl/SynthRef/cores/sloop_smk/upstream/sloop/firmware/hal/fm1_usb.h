/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 USB0 full-speed device controller, register level only.
 * The protocol (descriptors, EP0 requests,
 * MIDI / CDC / SysEx) is the driver src/usb.c, shared with the update loader
 * and the host tests; this header is every USB register access it makes.
 *
 *   fm1_usb_reset()        CON0 = CON1 = 0, pads detached; wait >= 20 ms
 *   fm1_usb_attach(ep0)    clock, EP DMA defaults, PHY on, D+ pull-up
 *   fm1_usb_off()          detach: controller off, pads inputs
 *   SIE (MUSB byte map):   fm1_usb_sie_on, _wr_start / _rd_start, _done, _data
 *   EP DMA:                fm1_usb_ep0_buf, fm1_usb_ep_txbuf / _rxbuf,
 *                          fm1_usb_ep0_send / fm1_usb_ep_send (csync first),
 *                          fm1_usb_rx_sync (ssync before reading an RX buffer)
 *   EP4 (own slots): fm1_usb_ep4_txbuf, fm1_usb_ep4_send (csync first);
 *                          isochronous mode is TXCSR2 bit 6 through the SIE (usb.c)
 *   fm1_usb_ep_enable(m)   CON0 &= ~(m << 19), m = bit per endpoint
 *   fm1_usb_sof_take()     SOF pending -> clear, 1
 * No IRQ: everything is polled (usb_poll from the TIMER5 ISR). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_USB_CON0 (*(volatile uint32_t *)0x11800u)
#define FM1_USB_CON1 (*(volatile uint32_t *)0x11804u)
#define FM1_USB_EP_CNT(n) (*(volatile uint32_t *)(0x11808u + 4u * (n)))          /* EP0..3 */
#define FM1_USB_EP0_ADR (*(volatile uint32_t *)0x11818u)
#define FM1_USB_EP_TADR(n) (*(volatile uint32_t *)(0x1181Cu + 8u * ((n) - 1u)))  /* EP1..3 */
#define FM1_USB_EP_RADR(n) (*(volatile uint32_t *)(0x11820u + 8u * ((n) - 1u)))  /* EP1..3 */
#define FM1_USB_EP4_CNT (*(volatile uint32_t *)0x11834u)
#define FM1_USB_EP4_TADR (*(volatile uint32_t *)0x11838u)
#define FM1_USB_EP4_RADR (*(volatile uint32_t *)0x1183Cu)
#define FM1_USB_IO_CON0 (*(volatile uint32_t *)0x51000u)
#define FM1_CLK_CON1 (*(volatile uint32_t *)0x10010u)

FM1_INLINE void fm1_usb_reset(void)
{
    FM1_USB_CON0 = 0;
    FM1_USB_CON1 = 0;
    FM1_USB_IO_CON0 = 0x0Cu;
}

FM1_INLINE void fm1_usb_attach(void *ep0)
{
    uint32_t n;
    FM1_CLK_CON1 &= ~3u;                                /* USB clock = PLL48M */
    FM1_USB_EP0_ADR = (uint32_t)(uintptr_t)ep0;
    for (n = 1; n <= 3u; n++)
        FM1_USB_EP_RADR(n) = (uint32_t)(uintptr_t)ep0;
    FM1_USB_EP4_RADR = (uint32_t)(uintptr_t)ep0;
    FM1_USB_IO_CON0 |= (1u << 10) | (1u << 9);
    FM1_USB_IO_CON0 &= ~(1u << 11);
    FM1_USB_IO_CON0 |= 1u << 12;
    FM1_USB_CON0 &= ~0x4035u;
    FM1_USB_CON1 = 0;
    FM1_USB_CON0 |= 1u;
    FM1_USB_IO_CON0 &= ~0xF0u;
    FM1_USB_IO_CON0 |= 0x40u;                           /* D+ pull-up: attach */
    FM1_USB_CON0 |= 0x3Cu;
}

FM1_INLINE void fm1_usb_off(void)
{
    FM1_USB_CON0 = 0;
    FM1_USB_IO_CON0 = 0x0Cu;
}

/* SIE access through CON1: only while CON0.bit2 (USB_NRST) is set */
FM1_INLINE uint32_t fm1_usb_sie_on(void) { return FM1_USB_CON0 & 4u; }
FM1_INLINE void fm1_usb_sie_wr_start(uint32_t r, uint32_t v)
{
    FM1_USB_CON1 = ((r << 8) & 0xFFFF00u) | (v & 0xFFu);
    __asm__ volatile("csync" ::: "memory");
}
FM1_INLINE void fm1_usb_sie_rd_start(uint32_t r)
{
    FM1_USB_CON1 = ((r << 8) & 0xFFBF00u) | 0x4000u;
    __asm__ volatile("csync" ::: "memory");
}
FM1_INLINE uint32_t fm1_usb_sie_done(void) { return FM1_USB_CON1 & 0x8000u; }
FM1_INLINE uint32_t fm1_usb_sie_data(void) { return FM1_USB_CON1 & 0xFFu; }

FM1_INLINE void fm1_usb_ep0_buf(void *p) { FM1_USB_EP0_ADR = (uint32_t)(uintptr_t)p; }
FM1_INLINE void fm1_usb_ep_txbuf(uint32_t ep, void *p) { FM1_USB_EP_TADR(ep) = (uint32_t)(uintptr_t)p; }
FM1_INLINE void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { FM1_USB_EP_RADR(ep) = (uint32_t)(uintptr_t)p; }
FM1_INLINE void fm1_usb_ep0_send(void *p, uint32_t n)
{
    __asm__ volatile("csync" ::: "memory");             /* the buffer is written before the DMA starts */
    FM1_USB_EP0_ADR = (uint32_t)(uintptr_t)p;
    FM1_USB_EP_CNT(0) = n;
}
FM1_INLINE void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n)
{
    __asm__ volatile("csync" ::: "memory");
    FM1_USB_EP_TADR(ep) = (uint32_t)(uintptr_t)p;
    FM1_USB_EP_CNT(ep) = n;
}
/* EP4 IN (the USB audio stream, isochronous): its DMA address and count have their own
 * registers (SDK usb_set_dma_taddr / usb_write_ep_cnt, id 0, ep 4) (after Felucca 1.0) */
FM1_INLINE void fm1_usb_ep4_txbuf(void *p) { FM1_USB_EP4_TADR = (uint32_t)(uintptr_t)p; }
FM1_INLINE void fm1_usb_ep4_send(void *p, uint32_t n)
{
    __asm__ volatile("csync" ::: "memory");
    FM1_USB_EP4_TADR = (uint32_t)(uintptr_t)p;
    FM1_USB_EP4_CNT = n;
    __asm__ volatile("csync" ::: "memory");
}
FM1_INLINE void fm1_usb_rx_sync(void) { __asm__ volatile("ssync" ::: "memory"); }

FM1_INLINE void fm1_usb_ep_enable(uint32_t eps) { FM1_USB_CON0 &= ~(eps << 19); }

FM1_INLINE uint32_t fm1_usb_sof_take(void)
{
    if (!(FM1_USB_CON0 & (1u << 13)))
        return 0;
    FM1_USB_CON0 |= 1u << 12;
    return 1;
}

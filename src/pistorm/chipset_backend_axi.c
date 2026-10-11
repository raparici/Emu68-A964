/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The chipset transport over the AXI seam to the FPGA (the ps_protocol.h contract). */

#include "support.h"
#include <stdint.h>
#include "amiga_memmap.h"
#include "jit_levers.h"

#ifdef CHIPSET_AXI

#ifndef AXI_SEAM_BASE
#define AXI_SEAM_BASE   0xC0000000u
#endif

#define OFF_ADDR    0x00u          /* m68k word addr (addr>>1) */
#define OFF_WDATA   0x04u          /* 16/32-bit write data (m68k order; 32-bit iff longword) */
#define OFF_CTRL    0x08u          /* trigger + mode; the write triggers */
#define OFF_RESULT  0x0Cu          /* {busy, cerr, 14'rsvd, rdata[15:0]} - poll+data */
#define OFF_STATUS  0x10u          /* LEGACY alias: bit0 busy, bit1 cerr (kept, unused) */
#define OFF_OVL     0x14u          /* bit0 overlay, bit1 reset strobe (== axi_seam_slave.v +0x14) */
#define OFF_RDATA_HI 0x18u
#define OFF_IPL     0x01000000u    /* arm_irq: bit3 reset_n, [2:0] IPL active-low */
#define OFF_RSTEPOCH 0x1Cu
#define RSTEPOCH_CHIP 0x1u         /* write: chipset-only reset request */
#define RSTEPOCH_SEAM 0x2u         /* write: seam-only reset request   */

#define SEAM_WR(off, v)  wr32le(AXI_SEAM_BASE + (off), (uint32_t)(v))
#define SEAM_RD(off)     rd32le(AXI_SEAM_BASE + (off))
static int g_rstepoch_cap = -1;
static int seam_rstepoch_capable(void)
{
    if (g_rstepoch_cap < 0)
        g_rstepoch_cap = ((SEAM_RD(OFF_RSTEPOCH) >> 28) == 0xEu) ? 1 : 0;
    return g_rstepoch_cap;
}

#define CTRL_READ   0x1u
#define CTRL_WRITE  0x2u
#define CTRL_BE_LO  (0x1u << 2)   /* byteenable 01: lower 16, word 0, addr[0]=0 */
#define CTRL_BE_HI  (0x2u << 2)   /* byteenable 10: upper 16, word 1, addr[0]=1 */
#define CTRL_LONGWORD (0x1u << 4)

/* Folded RESULT flags (bits [31:30] of OFF_RESULT). */
#define RES_BUSY    (0x1u << 31)  /* cycle in flight / not yet complete */
#define RES_CERR    (0x1u << 30)  /* sticky DTACK-timeout error (informational) */
#define RES_FULL    (0x1u << 29)  /* the request FIFO cannot take another trigger              */
#define RES_OVF     (0x1u << 28)  /* STICKY: a trigger WAS refused = a chip cycle was LOST      */
#define RES_DONE(r) (((r) >> 16) & 0xFFu)   /* retired-transaction counter, wraps at 256        */

static uint8_t  g_done_seen;
static uint8_t  g_done_pushed;      /* triggers issued since that observation             */
static uint8_t  g_done_alive = 1;   /* 0 = gateware has no counter -> legacy path         */

#define SEAM_TRIGGER(c) do { g_done_pushed++; SEAM_WR(OFF_CTRL, (c)); } while (0)

static inline void seam_done_reseed(void)
{
    g_done_seen   = RES_DONE(SEAM_RD(OFF_RESULT));
    g_done_pushed = 0;
}

#define XACT_TIMEOUT 100000u      /* DTACK can stall many 7 MHz cycles; guard it */

#define XACT_CERR_RETRIES 16u

static volatile int g_seam_in_reset;

volatile int g_reset_latched;
static void seam_wait_reset_release(void)
{
    uint32_t guard = 200000u;    /* OVL stretch is 10.24 us; external resets are ms-scale */
    while (!(SEAM_RD(OFF_IPL) & 0x8u) && --guard)
        ;
    if (!guard)
        kprintf_fb("[seam] chipset reset stuck low; proceeding (DTACK watchdog backstop)\n");
    g_seam_in_reset = 0;
}

static uint32_t g_xact_pending;      /* 1 = a write was triggered and not yet retired          */
static uint32_t g_xact_p_addr;       /* ...its payload, kept for the bounded CERR retry        */
static uint32_t g_xact_p_data;
static uint32_t g_xact_p_ctrl;

static void seam_drain(void)
{
    if (!g_xact_pending) return;
    for (int attempt = 0; ; attempt++) {
        uint32_t guard = XACT_TIMEOUT, r;
        do { r = SEAM_RD(OFF_RESULT); } while ((r & RES_BUSY) && --guard);
        if (!(r & RES_BUSY) && (r & RES_CERR) && attempt < (int)XACT_CERR_RETRIES) {
            SEAM_WR(OFF_ADDR,  g_xact_p_addr >> 1);
            SEAM_WR(OFF_WDATA, g_xact_p_data & 0xffffu);
            asm volatile("dsb" ::: "memory");
            SEAM_TRIGGER(g_xact_p_ctrl);
            asm volatile("dsb" ::: "memory");
            (void)SEAM_RD(OFF_RESULT);
            continue;
        }
        break;
    }
    g_xact_pending = 0;
}
void seam_drain_public(void) { seam_drain(); }

static uint32_t transport_xact(uint32_t dir, uint32_t size, uint32_t addr, uint32_t data)
{
    uint32_t ctrl = dir;
    if (size == 1)
        ctrl |= (addr & 1) ? CTRL_BE_HI : CTRL_BE_LO;
    else
        ctrl |= CTRL_BE_LO | CTRL_BE_HI;                /* full 16-bit word */

    if (__builtin_expect(g_seam_in_reset, 0)) {
        g_reset_latched = 1;
        return (dir == CTRL_READ) ? 0xffffu : 0;
    }

    uint32_t guard, r;
#ifdef EMU68_UAESND
    if (dir == CTRL_WRITE && addr == 0x00DFF09Cu &&
        (data & 0x2000u) && !(data & 0x8000u)) {
        extern volatile uint32_t g_uaesnd_intreq_clear_hint;
        g_uaesnd_intreq_clear_hint = 1u;
    }
#endif
#ifdef EMU68_SEAM_POSTED_WR
    seam_drain();                          /* retire the previous posted write, if any */
#endif
    for (int attempt = 0; ; attempt++) {
        SEAM_WR(OFF_ADDR, addr >> 1);      /* m68k word address (23-bit) */
        if (dir == CTRL_WRITE)
            SEAM_WR(OFF_WDATA, data & 0xffff);
        asm volatile("dsb" ::: "memory");   /* order the setup before the trigger */
        SEAM_TRIGGER(ctrl);           /* trigger */
#ifdef EMU68_SEAM_POSTED_WR
        if (dir == CTRL_WRITE) {
            g_xact_p_addr = addr; g_xact_p_data = data; g_xact_p_ctrl = ctrl;
            g_xact_pending = 1;
            asm volatile("dsb" ::: "memory");
            return 0;
        }
#endif
        asm volatile("dsb" ::: "memory");
        if (__builtin_expect(!g_done_alive, 0))
            (void)SEAM_RD(OFF_RESULT);

        guard = XACT_TIMEOUT;
        if (__builtin_expect(g_done_alive, 1)) {
            uint8_t want = (uint8_t)(g_done_seen + g_done_pushed);
            do { r = SEAM_RD(OFF_RESULT); }
            while (((RES_DONE(r) != want) || (r & RES_BUSY)) && --guard);
            if (__builtin_expect(guard != 0, 1)) {
                g_done_seen = RES_DONE(r); g_done_pushed = 0;
            } else if (RES_DONE(r) == g_done_seen) {
                /* the counter never moved through a whole guard: this gateware has none */
                g_done_alive = 0;
                seam_done_reseed();
            } else {
                seam_done_reseed();
            }
        } else {
            do { r = SEAM_RD(OFF_RESULT); } while ((r & RES_BUSY) && --guard);
        }

        if (__builtin_expect((r & (RES_BUSY | RES_CERR)) &&
                             !(SEAM_RD(OFF_IPL) & 0x8u), 0)) {
            g_seam_in_reset = 1;
            g_reset_latched = 1;
            r = 0xffffu;
            break;
        }

        if (__builtin_expect(!(r & RES_BUSY) && (r & RES_CERR), 0) && attempt < (int)XACT_CERR_RETRIES) {
            continue;
        }
        break;
    }
    if (dir == CTRL_READ && addr == 0x00DFF01Eu) {
        uint32_t v = (r & 0xffffu) & ~0x4000u;
#ifdef EMU68_UAESND
        extern uint32_t uaesnd_exter_line(void);
        if (uaesnd_exter_line())
            v |= 0x2000u;
#endif
        return v;
    }
#ifdef EMU68_BLTBUSY_PACE
    if (dir == CTRL_READ && addr == 0x00DFF002u &&
        (r & ((size == 1u) ? 0x40u : 0x4000u))) {
#ifndef EMU68_BLTBUSY_PACE_N
#define EMU68_BLTBUSY_PACE_N 2048
#endif
        uint32_t _n = g_bltpace_ticks;
        if (__builtin_expect(_n == BLTPACE_TICKS_LEGACY, 0)) {
            for (volatile int _p = 0; _p < (EMU68_BLTBUSY_PACE_N); _p++) ;   /* no usable counter */
        } else if (_n) {
            (void)bltpace_wait_ticks(_n);
        }
    }
#endif
    return (dir == CTRL_READ) ? (r & 0xffffu) : 0;
}

static uint32_t transport_xact_lw(uint32_t dir, uint32_t addr, uint32_t data)
{
    uint32_t ctrl = dir | CTRL_BE_LO | CTRL_BE_HI | CTRL_LONGWORD;
    if (__builtin_expect(g_seam_in_reset, 0)) {
        return (dir == CTRL_READ) ? 0xffffffffu : 0;
    }

    uint32_t guard, r = 0;
#ifdef EMU68_SEAM_POSTED_WR
    seam_drain();
#endif
    for (int attempt = 0; ; attempt++) {
        uint32_t done0 = (uint32_t)g_done_seen;
        SEAM_WR(OFF_ADDR, addr >> 1);
        if (dir == CTRL_WRITE)
            SEAM_WR(OFF_WDATA, data);       /* full 32-bit longword (hi=A, lo=A+2) */
        asm volatile("dsb" ::: "memory");
        SEAM_TRIGGER(ctrl);            /* trigger */

        asm volatile("dsb" ::: "memory");

        guard = XACT_TIMEOUT;
        if (__builtin_expect(g_done_alive, 1)) {
            uint8_t want = (uint8_t)(done0 + g_done_pushed);
            do { r = SEAM_RD(OFF_RESULT); }
            while (((RES_DONE(r) != want) || (r & RES_BUSY)) && --guard);
            if (__builtin_expect(guard != 0, 1)) {
                g_done_seen = RES_DONE(r); g_done_pushed = 0;
            } else if (RES_DONE(r) == g_done_seen) {
                /* a whole guard with the counter never moving: this gateware has none */
                g_done_alive = 0;
                seam_done_reseed();
            } else {
                seam_done_reseed();
            }
        } else {
            /* legacy: no retirement counter - settle the posted trigger, then poll busy */
            (void)SEAM_RD(OFF_RESULT);
            do { r = SEAM_RD(OFF_RESULT); } while ((r & RES_BUSY) && --guard);
        }

        if (__builtin_expect((r & RES_BUSY) && !(SEAM_RD(OFF_IPL) & 0x8u), 0)) {
            seam_wait_reset_release();
            guard = XACT_TIMEOUT;
            do { r = SEAM_RD(OFF_RESULT); } while ((r & RES_BUSY) && --guard);
        }

        if (__builtin_expect(!(r & RES_BUSY) && (r & RES_CERR), 0) && attempt < (int)XACT_CERR_RETRIES) {
            if (!(SEAM_RD(OFF_IPL) & 0x8u)) {
                seam_wait_reset_release();
            }
            continue;
        }
        break;
    }

    if (dir != CTRL_READ)
        return 0;
    {
        uint32_t _hi = SEAM_RD(OFF_RDATA_HI);
        return (_hi << 16) | (r & 0xffffu);   /* (A<<16) | (A+2) */
    }
}

static inline int transport_ipl(void)
{
    uint32_t r = SEAM_RD(OFF_IPL);
    for (int guard = 8; guard > 0; guard--) {
        uint32_t r2 = SEAM_RD(OFF_IPL);
        if (r2 == r)
            break;
        r = r2;
    }
    g_seam_in_reset = !(r & 0x8u);
#ifdef EMU68_BLITFAIR
    { extern int g_blitter_busy; g_blitter_busy = (int)((r >> 4) & 1u); }
#endif
    if (!(r & 0x8u))            /* reset_n deasserted -> m68k in reset, mask IPL */
        return 0;
    return (~r) & 7u;           /* active-low IPL[2:0] -> 0..7 (0b111 == no IRQ) */
}

void ps_write_8 (unsigned int address, unsigned int data) { transport_xact(CTRL_WRITE, 1, address, data & 0xff); }

#define OFF_GUEST_CREDIT 0x30u
void ps_guestpace_feed(unsigned int credits) { SEAM_WR(OFF_GUEST_CREDIT, credits); }
unsigned int ps_guestpace_level(void) { return SEAM_RD(OFF_GUEST_CREDIT); }
void ps_write_16(unsigned int address, unsigned int data)
{
    if (address & 1) {
        transport_xact(CTRL_WRITE, 1, address,     (data >> 8) & 0xff);
        transport_xact(CTRL_WRITE, 1, address + 1,  data       & 0xff);
        return;
    }
    transport_xact(CTRL_WRITE, 2, address, data & 0xffff);
}
void ps_write_32(unsigned int address, unsigned int value)
{
    if (!(address & 1)) {
        transport_xact_lw(CTRL_WRITE, address, value);
        return;
    }
    ps_write_16(address,     (value >> 16) & 0xffff);
    ps_write_16(address + 2,  value        & 0xffff);   /* (odd A: ps_write_16 byte-splits) */
}
unsigned int ps_read_8 (unsigned int address)
{
    unsigned int v = transport_xact(CTRL_READ, 1, address, 0) & 0xff;
    return v;
}
unsigned int ps_read_16(unsigned int address)
{
    if (address & 1) {
        unsigned int hi = transport_xact(CTRL_READ, 1, address,     0) & 0xff;
        unsigned int lo = transport_xact(CTRL_READ, 1, address + 1, 0) & 0xff;
        return (hi << 8) | lo;
    }
    {
        unsigned int v = transport_xact(CTRL_READ, 2, address, 0) & 0xffff;
        return v;
    }
}
unsigned int ps_read_32(unsigned int address)
{
    if (!(address & 1)) {
        unsigned int v = transport_xact_lw(CTRL_READ, address, 0);
        return v;
    }
    unsigned int hi = ps_read_16(address);
    unsigned int lo = ps_read_16(address + 2);
    return (hi << 16) | lo;                             /* (odd A: ps_read_16 byte-splits) */
}

int  chipset_pending_ipl(void) { return transport_ipl(); }

int chipset_alive(void) { return !!(SEAM_RD(OFF_IPL) & 0x8u); }
int chipset_seam_in_reset(void) { return g_seam_in_reset; }
void chipset_ack_ipl(int level)
{
    if (level != 7) return;              /* Paula IPL is level-sensitive - nothing to do */
    (void)ps_read_16(0x00FFFFFEu);       /* the cart's int7_ack cycle */
}
unsigned int ps_get_ipl_zero(void) { return transport_ipl() == 0; }

volatile uint32_t g_guest_reset_pulsed = 0;

void ps_pulse_reset(void)
{
    g_guest_reset_pulsed = 1;
#ifdef EMU68_SEAM_POSTED_WR
    seam_drain();               /* never pulse the chipset with a chip cycle still in flight */
#endif
    if (seam_rstepoch_capable())
        SEAM_WR(OFF_RSTEPOCH, RSTEPOCH_CHIP);   /* self-clearing stretched strobe */
    else {
        SEAM_WR(OFF_OVL, 0x2u);
        asm volatile("dsb" ::: "memory");
        SEAM_WR(OFF_OVL, 0x0u);
    }
    asm volatile("dsb" ::: "memory");
    uint32_t fall_guard = 4096;
    while ((SEAM_RD(OFF_IPL) & 0x8u) && --fall_guard)
        ;
    seam_wait_reset_release();
}

void seam_flush_bridge(void)
{
    if (!seam_rstepoch_capable())
        return;
    SEAM_WR(OFF_RSTEPOCH, RSTEPOCH_SEAM);
    asm volatile("dsb" ::: "memory");
    for (volatile int i = 0; i < 8000; i++) ;
}

void ps_setup_protocol(void) { /* nothing to arm on AXI (no mailbox magic) */ }
void ps_housekeeper(void)    { }

#endif /* CHIPSET_AXI */

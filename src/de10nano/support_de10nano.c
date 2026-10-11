/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* DE10-Nano low-level support: UART, cache maintenance, kprintf. */

#include <stdint.h>
#include <stdarg.h>

#include "support.h"

#ifndef __aarch64__
asm(
"       .globl __aeabi_uidiv                \n"
"       .type __aeabi_uidiv,%function       \n"
"__aeabi_uidiv:                             \n"
"       push {%lr}                          \n"
"       sub %sp, %sp, #8                    \n"
"       push {%r0, %r1}                     \n"
"       pop  {%r1, %r2}                     \n"
"       mov %r0, %sp                        \n"
"       bl uidiv                            \n"
"       pop {%r0}                           \n"
"       add %sp, %sp, #4                    \n"
"       pop {%pc}                           \n"

"       .globl __aeabi_uidivmod             \n"
"       .type __aeabi_uidivmod,%function    \n"
"__aeabi_uidivmod:                          \n"
"       push {%lr}                          \n"
"       sub %sp, %sp, #8                    \n"
"       push {%r0, %r1}                     \n"
"       pop  {%r1, %r2}                     \n"
"       mov %r0, %sp                        \n"
"       bl uidiv                            \n"
"       pop {%r0}                           \n"
"       pop {%r1}                           \n"
"       pop {%pc}                           \n"

"       .globl __aeabi_idivmod             \n"
"       .type __aeabi_idivmod,%function    \n"
"__aeabi_idivmod:                          \n"
"       push {%lr}                         \n"
"       sub %sp, %sp, #8                   \n"
"       push {%r0, %r1}                    \n"
"       pop  {%r1, %r2}                    \n"
"       mov %r0, %sp                       \n"
"       bl sidiv_tail                      \n"
"       pop {%r0}                          \n"
"       pop {%r1}                          \n"
"       pop {%pc}                          \n"

"       .globl __aeabi_idiv                \n"
"       .type __aeabi_idiv,%function       \n"
"__aeabi_idiv:                             \n"
"       push {%lr}                         \n"
"       sub %sp, %sp, #8                   \n"
"       push {%r0, %r1}                    \n"
"       pop  {%r1, %r2}                    \n"
"       mov %r0, %sp                       \n"
"       bl sidiv_tail                      \n"
"       pop {%r0}                          \n"
"       add %sp, %sp, #4                   \n"
"       pop {%pc}                          \n"

"       .globl emu68_uldivmod_lepair       \n"
"       .type emu68_uldivmod_lepair,%function \n"
"emu68_uldivmod_lepair:                    \n"
"       push {%r4, %r5, %r6, %r7, %lr}    \n"
"       mov %r4, %r0                       \n"
"       mov %r5, %r1                       \n"
"       mov %r6, %r2                       \n"
"       mov %r7, %r3                       \n"
"       sub %sp, %sp, #16                  \n"
"       mov %r0, %sp                       \n"
"       mov %r2, %r5                       \n"
"       mov %r3, %r4                       \n"
"       push {%r6}                         \n"
"       push {%r7}                         \n"
"       bl uldiv                           \n"
"       add %sp, %sp, #8                   \n"
"       ldr %r0, [%sp, #4]                 \n"
"       ldr %r1, [%sp, #0]                 \n"
"       ldr %r2, [%sp, #12]                \n"
"       ldr %r3, [%sp, #8]                 \n"
"       add %sp, %sp, #16                  \n"
"       pop {%r4, %r5, %r6, %r7, %pc}     \n"

"       .globl emu68_ldivmod_lepair        \n"
"       .type emu68_ldivmod_lepair,%function \n"
"emu68_ldivmod_lepair:                     \n"
"       push {%r4, %r5, %r6, %r7, %lr}    \n"
"       mov %r4, %r0                       \n"
"       mov %r5, %r1                       \n"
"       mov %r6, %r2                       \n"
"       mov %r7, %r3                       \n"
"       sub %sp, %sp, #16                  \n"
"       mov %r0, %sp                       \n"
"       mov %r2, %r5                       \n"
"       mov %r3, %r4                       \n"
"       push {%r6}                         \n"
"       push {%r7}                         \n"
"       bl sldiv_tail                      \n"
"       add %sp, %sp, #8                   \n"
"       ldr %r0, [%sp, #4]                 \n"
"       ldr %r1, [%sp, #0]                 \n"
"       ldr %r2, [%sp, #12]                \n"
"       ldr %r3, [%sp, #8]                 \n"
"       add %sp, %sp, #16                  \n"
"       pop {%r4, %r5, %r6, %r7, %pc}     \n"
);
#endif

#define UART0_PHYS      0xFFC02000u
#define UART0_BASE      0xF2002000u              /* virtual, via the HPS window   */

#define UART_RBR        0x00                     /* R: receive buffer             */
#define UART_THR        0x00
#define UART_DLL        0x00
#define UART_IER        0x04
#define UART_DLH        0x04
#define UART_IIR        0x08                     /* R: interrupt id                */
#define UART_FCR        0x08
#define UART_LCR        0x0C
#define UART_MCR        0x10
#define UART_LSR        0x14                     /* R:  line status                */
#define UART_USR        0x7C                     /* R:  DW UART status (busy)       */

#define LCR_DLAB        (1u << 7)                /* divisor-latch access           */
#define LCR_8N1         0x03u                    /* 8 data bits, 1 stop, no parity  */
#define FCR_FIFO_EN     0x01u                    /* enable RX/TX FIFOs             */
#define FCR_RX_RST      0x02u
#define FCR_TX_RST      0x04u
#define LSR_THRE        (1u << 5)                /* transmit holding reg empty     */
#define USR_BUSY        (1u << 0)                /* DW UART busy (config guard)     */

#define UART_INPUT_CLK  100000000u
#define UART_BAUD       115200u

static int serial_up = 0;

static inline void wait_txready(void)
{
    uint32_t guard = 1000000u;
    while ((rd32le(UART0_BASE + UART_LSR) & LSR_THRE) == 0 && --guard)
        ;
}

static inline void putByte(void *io_base, char chr)
{
    (void)io_base;
    if (!serial_up)
        return;

    if (chr == '\n') {
        wait_txready();
        wr32le(UART0_BASE + UART_THR, '\r');
    }
    wait_txready();
    wr32le(UART0_BASE + UART_THR, (uint8_t)chr);
}

void setup_serial(void)
{
    uint32_t divisor = UART_INPUT_CLK / (16u * UART_BAUD);   /* 16x oversampling */

    wr32le(UART0_BASE + UART_IER, 0);

    for (uint32_t g = 0; g < 100000u; g++) {
        if ((rd32le(UART0_BASE + UART_USR) & USR_BUSY) == 0)
            break;
    }

    /* Program the divisor: LCR.DLAB=1, write DLL/DLH, then LCR = 8N1 (DLAB=0). */
    wr32le(UART0_BASE + UART_LCR, LCR_DLAB);
    wr32le(UART0_BASE + UART_DLL, divisor & 0xff);
    wr32le(UART0_BASE + UART_DLH, (divisor >> 8) & 0xff);
    wr32le(UART0_BASE + UART_LCR, LCR_8N1);

    /* Enable + reset FIFOs. */
    wr32le(UART0_BASE + UART_FCR, FCR_FIFO_EN | FCR_RX_RST | FCR_TX_RST);

    /* No modem-control handshake (RTS/DTR asserted for good measure). */
    wr32le(UART0_BASE + UART_MCR, 0x03u);

    serial_up = 1;
}

volatile unsigned char print_lock = 0;

void kprintf(const char * restrict format, ...)
{
    if (!serial_up) return;   /* see vkprintf: no-op before the atomic spinlock when serial is off */
    va_list v;
    va_start(v, format);
    while (__atomic_test_and_set(&print_lock, __ATOMIC_ACQUIRE)) asm volatile("yield");
    vkprintf_pc(putByte, (void*)UART0_BASE, format, v);
    __atomic_clear(&print_lock, __ATOMIC_RELEASE);
    va_end(v);
}

void vkprintf(const char * restrict format, va_list args)
{
    if (!serial_up) return;
    while (__atomic_test_and_set(&print_lock, __ATOMIC_ACQUIRE)) asm volatile("yield");
    vkprintf_pc(putByte, (void*)UART0_BASE, format, args);
    __atomic_clear(&print_lock, __ATOMIC_RELEASE);
}

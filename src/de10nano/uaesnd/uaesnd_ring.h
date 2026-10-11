/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* uaesnd: the PCM ring to the Main on core #0. */

#ifndef UAESND_RING_H
#define UAESND_RING_H

#include <stdint.h>

#define UAESND_OUT_RATE     48000u
#define UAESND_OUT_CHANS    2u
#define UAESND_BYTES_FRAME  (UAESND_OUT_CHANS * 2u)

#define UAESND_LEAD_FRAMES  (UAESND_OUT_RATE / 8u)      /* 125 ms */

#define UAESND_ST_ARMED     0x00000001u  /* ring armed, magic published        */
#define UAESND_ST_PRODUCED  0x00000002u  /* at least one frame ever produced   */
#define UAESND_ST_MAINSEEN  0x00000004u  /* core#0 has moved CONS at least once*/
#define UAESND_ST_UNDERRUN  0x00000008u  /* ring ever ran dry (pump starved)   */
#define UAESND_ST_OVERRUN   0x00000010u  /* producer ever found the ring full  */
#define UAESND_ST_CONSBAD   0x00000020u  /* CONS ever ran AHEAD of PROD (bug)  */
#define UAESND_ST_PLAYING   0x00000040u  /* a guest stream has ever played     */
#define UAESND_ST_SRC_CHIP  0x00000080u  /* a stream's PCM came from CHIP RAM  */
#define UAESND_ST_SRC_FAST  0x00000100u  /* ...and from FAST RAM               */
#define UAESND_ST_BADSET    0x00000200u  /* a sample set failed validation     */
#define UAESND_ST_STAGING     0x00000400u
#define UAESND_ST_STG_NOFMT   0x00000800u  /* a set S_FMT cannot express       */
#define UAESND_ST_STG_CONSBAD 0x00001000u  /* Main's S_CONS ran ahead of PROD  */

void uaesnd_ring_arm(void);
void uaesnd_tick(void);

#endif /* UAESND_RING_H */

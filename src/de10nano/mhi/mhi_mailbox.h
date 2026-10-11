/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* MHI: the control mailbox shared with the guest's mhibigmig.library. */

#ifndef MHI_MAILBOX_H
#define MHI_MAILBOX_H

/* Where the mailbox lives inside the ZZ9000 board register window. */
#define MHI_MBOX_OFF      0x1C00u   /* offset within the ZZ9000 register page */
#define MHI_MBOX_LEN      0x0100u   /* 256 bytes reserved for the mailbox      */

#define MHI_REG_MAGIC       0x00u   /* RO  reads MHI_MAGIC once fw is present  */
#define MHI_REG_VERSION     0x04u   /* RO  MHI_PROTO_VERSION                   */
#define MHI_REG_CMD         0x08u   /* WO  write a MHI_CMD_* -> fw acts        */
#define MHI_REG_ARG0        0x0Cu   /* RW  command arg 0 (e.g. buffer pointer) */
#define MHI_REG_ARG1        0x10u   /* RW  command arg 1 (e.g. size / param id)*/
#define MHI_REG_ARG2        0x14u   /* RW  command arg 2 (e.g. param value)    */
#define MHI_REG_STATE       0x18u   /* RO  MHI_FWSTATE_* (the decode state)    */
#define MHI_REG_CONSUMED    0x1Cu   /* RO  cumulative guest bytes accepted      */
#define MHI_REG_PRODUCED    0x20u   /* RO  cumulative PCM frames produced (tlm) */
#define MHI_REG_HZ          0x24u   /* RO  decoded sample rate, 0 until known   */
#define MHI_REG_CHANNELS    0x28u   /* RO  decoded channel count (1 or 2)       */
#define MHI_REG_QFREE       0x2Cu   /* RO  free slots in the descriptor ring    */
#define MHI_REG_UNDERRUNS   0x30u   /* RO  PCM ring underruns (telemetry)       */
#define MHI_REG_ERROR       0x34u   /* RO  last decode error count (telemetry)  */
#define MHI_REG_PLAYED      0x38u
#define MHI_REG_CAPS        0x3Cu
#define MHI_REG_SINK_LEAD   0x40u
#define MHI_REG_MAIN_REV    0x44u

#define MHI_MAGIC           0x4D484931u   /* 'M''H''I''1' */
#define MHI_PROTO_VERSION   2u

#define MHI_CMD_NONE        0u
#define MHI_CMD_ALLOC       1u
#define MHI_CMD_FREE        2u
#define MHI_CMD_QUEUE       3u   /* enqueue: ARG0 = guest buf ptr, ARG1 = size  */
#define MHI_CMD_PLAY        4u   /* start / resume playback                     */
#define MHI_CMD_STOP        5u   /* stop, reset the decode position             */
#define MHI_CMD_PAUSE       6u
#define MHI_CMD_SETVOL      7u
#define MHI_CMD_SETPAN      8u

#define MHI_FWSTATE_IDLE        0u
#define MHI_FWSTATE_PLAYING     1u
#define MHI_FWSTATE_STOPPED     2u
#define MHI_FWSTATE_OUT_OF_DATA 3u   /* queue starved; new data auto-resumes     */
#define MHI_FWSTATE_PAUSED      4u

#define MHI_DESC_RING           16u

#endif /* MHI_MAILBOX_H */

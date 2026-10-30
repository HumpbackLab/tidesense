/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TIDESENSE_C906_IVE_SHARED_H
#define TIDESENSE_C906_IVE_SHARED_H
#include "../c906-tpu/shared.h"
#define IVE_PROBE_OPERATION 5u
#define IVE_META_OFF 1032192u
#define IVE_MAGIC 0x49564531u
#define IVE_VERSION 1u
#define IVE_ARENA_BYTES (4u*1024u*1024u)
#define IVE_PROGRAM_OFF 4096u
#define IVE_PROGRAM_BYTES 32768u
#define IVE_PROGRAMS 6u
#define IVE_HIST_OFF 262144u
#define IVE_SCRATCH_OFF 524288u
#define IVE_SCRATCH_BYTES (2u*1024u*1024u)
#define IVE_ST_OUT_OFF (3u*1024u*1024u)
#define IVE_PACK_OFF (7u*1024u*1024u/2u)
#define IVE_FLAG_HIST 1u
#define IVE_FLAG_ST 2u
#define IVE_CMD_NORM 0xffecu
#define IVE_CMD_DELAY 0xfff0u
#define IVE_CMD_WAIT 0xfff4u
#define IVE_CMD_IF_SAT 0xfff8u
#define IVE_CMD_CLEAR 0xfffcu
struct ive_reg_command {probe_u32 offset,value;};
struct ive_program {probe_u32 count,reserved[15];struct ive_reg_command commands[(IVE_PROGRAM_BYTES-64)/8];};
struct ive_control {
    probe_u32 magic,version,arena,arena_bytes;
    probe_u32 flags,quality,capabilities,status;
    probe_u32 width,height,stride,test_program;
    probe_u32 repeats,completed,last_register,last_value;
    probe_u32 pack_input,histogram_calls,st_calls,reserved;
    probe_u64 histogram_ticks,st_ticks,pack_ticks,run_ticks[64];
};
#ifdef __cplusplus
extern "C" {
#endif
/* Pure register generation. No Linux IVE device access or submission. */
int ive_make_program(struct ive_program*,unsigned kind,unsigned width,unsigned height,unsigned stride,
                     probe_u32 source,probe_u32 destination,probe_u32 scratch,unsigned quality);
#ifdef __cplusplus
}
#endif
#endif

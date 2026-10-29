#ifndef TIDESENSE_C906_TPU_SHARED_H
#define TIDESENSE_C906_TPU_SHARED_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef __u32 probe_u32;
typedef __s32 probe_s32;
typedef __u64 probe_u64;
#else
#include <stdint.h>
typedef uint32_t probe_u32;
typedef int32_t probe_s32;
typedef uint64_t probe_u64;
#endif

#define TPU_PROBE_CMD 0x4e
#define TPU_PROBE_MAGIC 0x54505543u
#define TPU_PROBE_VERSION 1u
#define TPU_PROBE_BYTES (1024u * 1024u)
#define TPU_PROBE_CONTROL_BYTES 16384u
#define TPU_PROBE_DMA_OFF 16384u
#define TPU_PROBE_INPUT_OFF 131072u
#define TPU_PROBE_OUTPUT_OFF 262144u
#define TPU_PROBE_MAX_RUNS 1000u
#define TPU_PROBE_REGS 12u
#define TPU_PROBE_READ 0u
#define TPU_PROBE_RUN 1u
#define TPU_PROBE_CLOCK 3u /* optional rdtime / host monotonic alignment */

struct tpu_probe_control {
    probe_u32 magic, version, operation, repeats;
    probe_u32 dmabuf_bytes, timeout_us, completed, reserved;
    probe_s32 status;
    probe_u32 regs_before[TPU_PROBE_REGS];
    probe_u32 regs_after[TPU_PROBE_REGS];
    probe_u64 total_ticks;
    probe_u64 run_ticks[TPU_PROBE_MAX_RUNS];
};

/* CV181x DMA header and 224-byte CPU synchronization descriptor. */
struct tpu_probe_dma_header {
    unsigned short magic_m, magic_s;
    probe_u32 size, cpu_count, tiu_count, tdma_count;
    probe_u32 clock_rate, pmu_size, pmu_offset;
    probe_u32 arraybase[8]; /* base0L, base0H, ... base3L, base3H */
    probe_u32 arraybase_extra[8]; /* base4L, base4H, ... base7L, base7H */
    probe_u32 reserved[8];
};

struct tpu_probe_cpu_desc {
    probe_u32 type, num_tiu, num_tdma, offset_tiu, offset_tdma;
    probe_u32 original_tiu, original_tdma;
    char reserved[196];
};

#endif

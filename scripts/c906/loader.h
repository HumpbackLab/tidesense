#ifndef TIDESENSE_C906_LOADER_H
#define TIDESENSE_C906_LOADER_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define C906_FIRMWARE_BASE 0x8ff40000u
#define C906_FIRMWARE_SIZE 0x000c0000u
#define C906_RELOAD_CAP_CMD 0x4c
#define C906_RELOAD_PARK_CMD 0x4d
#define C906_RELOAD_MAGIC 0x52393036u
#define C906_RELOAD_SIGNATURE "TS_C906_RELOAD_V1"
#define C906_GATE_PARKED 1u
#define C906_GATE_RUN 2u
#define C906_GATE_READY 3u
#define C906_GATE_CODE_OFF 256u

struct c906_reload_gate {
    __u32 magic;
    __u32 phase;
    __u32 entry;
    __u32 reserved;
};

struct c906_reload_request {
    __u64 image;
    __u32 image_bytes;
    __s32 result;
    __u64 park_us;
    __u64 copy_us;
    __u64 startup_us;
    __u64 total_us;
    __u32 transfer_config;
    __u32 reserved;
};

struct c906_loader_status {
    __u32 gate_phase;
    __u32 transfer_config;
    __u32 firmware_magic;
    __s32 echo_result;
    __u64 echo_us;
    __u32 reload_supported;
    __u32 reserved;
};

#define C906_IOCTL_STATUS _IOR('C', 1, struct c906_loader_status)
#define C906_IOCTL_RELOAD _IOWR('C', 2, struct c906_reload_request)

#endif
